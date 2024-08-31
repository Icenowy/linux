/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/scatterlist.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/sh_fsi.h>
#include "th1520-pcm.h"
#include "th1520-audio-cpr.h"
#include "th1520-tdm.h"
#include <linux/dmaengine.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>
#include <linux/mfd/syscon.h>

#define TH1520_RATES SNDRV_PCM_RATE_8000_384000
#define I2S_DMA_MAXBURST    (4)
#define TH1520_FMTS (SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8)

static int th1520_tdm_dai_probe(struct snd_soc_dai *dai)
{
    struct th1520_tdm_priv *tdm = snd_soc_dai_get_drvdata(dai);

    if (tdm) {
        snd_soc_dai_init_dma_data(dai, NULL, &tdm->dma_params_rx);
    } else {
        return -EIO;
    }

    return 0;
}

static int th1520_tdm_dai_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
    struct th1520_tdm_priv *priv = snd_soc_dai_get_drvdata(dai);

    if (priv->slot_num != 1) {
        return 0;
    }
    
    return pm_runtime_get_sync(dai->dev);
}

static void th1520_tdm_snd_rxctrl(struct th1520_tdm_priv *priv, char on)
{
    u32 dmactl;
    u32 tdmen;

    if (priv->slot_num != 1) {
        return;
    }

    regmap_update_bits(priv->regmap, TDM_DMACTL,
            DMACTL_DMAEN_MSK, DMACTL_DMAEN_SEL(on));
    regmap_update_bits(priv->regmap, TDM_TDMEN,
            TDMCTL_TDMEN_MSK, TDMCTL_TDMEN_SEL(on));
    regmap_read(priv->regmap, TDM_DMACTL, &dmactl);
    regmap_read(priv->regmap, TDM_TDMEN, &tdmen);
    pr_debug("%s TDM_DMACTL=0x%x TDM_TDMEN=0x%x\n", __func__, dmactl, tdmen);
    return;
}

static void th1520_tdm_dai_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct th1520_tdm_priv *priv = snd_soc_dai_get_drvdata(dai);

    if (priv->slot_num != 1) {
        return;
    }

    if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		th1520_tdm_snd_rxctrl(priv, 0);

	pm_runtime_put(dai->dev);
    return;
}

static int th1520_tdm_dai_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
    int ret = 0;
    struct th1520_tdm_priv *priv = snd_soc_dai_get_drvdata(dai);

    switch(cmd) {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_RESUME:
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
            th1520_tdm_snd_rxctrl(priv, 1);
            priv->state = TDM_STATE_RUNNING;
            break;
        case SNDRV_PCM_TRIGGER_STOP:
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
            priv->state = TDM_STATE_IDLE;
        case SNDRV_PCM_TRIGGER_SUSPEND:
            th1520_tdm_snd_rxctrl(priv, 0);
            break;
        default:
            return -EINVAL;
    }    

    return ret;
}

static int th1520_tdm_set_fmt_dai(struct snd_soc_dai *dai, unsigned int fmt)
{
    struct th1520_tdm_priv *priv = snd_soc_dai_get_drvdata(dai);

    if (priv->slot_num != 1) {
        return 0;
    }

    pm_runtime_resume_and_get(priv->dev);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		priv->mode = TDM_MODE_MASTER;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		priv->mode = TDM_MODE_SLAVE;
		break;
	default:
		pr_err("Unknown fmt dai\n");
		return -EINVAL;
	}

    switch(fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
        case SND_SOC_DAIFMT_DSP_B:
            break;
        default:
            pr_err("Unknown fmt dai\n");
            return -EIO;
    }

	regmap_update_bits(priv->regmap, TDM_TDMCTL,
			TDMCTL_MODE_MSK,
			TDMCTL_MODE_SEL(TDM_MODE_MASTER));
	regmap_update_bits(priv->regmap, TDM_TDMCTL,
			TDMCTL_SPEDGE_MSK,
			TDMCTL_SPEDGE_SEL(1));
    regmap_update_bits(priv->regmap, TDM_DMADL,
            DMACTL_DMADL_MSK, DMACTL_DMADL_SEL(0));

    pm_runtime_put_sync(priv->dev);

    return 0;
}

static u32 th1520_special_sample_rates[] = { 11025, 22050, 44100, 88200 };
#define AUDIO_DIVCLK0  49152000
#define AUDIO_DIVCLK1 135475200

static void th1520_tdm_set_div(struct th1520_tdm_priv *priv, struct snd_pcm_hw_params *params)
{
    bool is_divclk1 = false; //audio_divclk1 for 44.1k...etc. audio_divclk0 for 48k....etc
    u32 src_clk;
    u32 div0, i, width;
    u32 sample_rate = params_rate(params);

    switch(params_format(params)) {
        case SNDRV_PCM_FORMAT_S16_LE:
            width = 16;
            break;
        case SNDRV_PCM_FORMAT_S24_LE:
            width = 24;
            break;
        case SNDRV_PCM_FORMAT_S32_LE:
            width = 32;
            break;
        default:
            pr_err("Unknown data format\n");
            return;
    }

    for (i = 0; i < ARRAY_SIZE(th1520_special_sample_rates); i++) {
        if (th1520_special_sample_rates[i] == sample_rate) {
            is_divclk1 = true;
            break;
        }
    }

    if (is_divclk1 == false) { // audio_divclk0=49152000 for 48k
        regmap_update_bits(priv->audio_cpr_regmap,
                        CPR_PERI_CLK_SEL_REG, CPR_TDM_SRC_SEL_MSK, CPR_TDM_SRC_SEL(0));
        src_clk = AUDIO_DIVCLK0;
    } else { // audio_divclk1=135475200 for 44.1k
        regmap_update_bits(priv->audio_cpr_regmap,
                        CPR_PERI_CLK_SEL_REG, CPR_TDM_SRC_SEL_MSK, CPR_TDM_SRC_SEL(2));
        src_clk = AUDIO_DIVCLK1;
    }
    div0 = src_clk / (sample_rate * (width * priv->slots));
	regmap_update_bits(priv->regmap, TDM_DIV0_LEVEL,
			TDMCTL_DIV0_MASK, div0);
    pr_debug("src_clk=%d sample_rate=%d priv->slots=%d width=%d div0=%d\n", src_clk, sample_rate, priv->slots, width, div0);
}


static int th1520_tdm_dai_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
    struct th1520_tdm_priv *priv =  snd_soc_dai_get_drvdata(dai);
    u32 datawth, chn_num;
    u32 tdmctl;

    if ( params_channels(params) != 1) {
        pr_err("Not support channel num\n");
        return -EINVAL;
    }

    if (priv->slot_num != 1) {
        return 0;
    }

    switch(params_format(params)) {
        case SNDRV_PCM_FORMAT_S16_LE:
            datawth = TDMCTL_DATAWTH_16BIT_PACKED; // TDMCTL_DATAWTH_16BIT
            break;
        case SNDRV_PCM_FORMAT_S24_LE:
            datawth = TDMCTL_DATAWTH_24BIT;
            break;
        case SNDRV_PCM_FORMAT_S32_LE:
            datawth = TDMCTL_DATAWTH_32BIT;
            break;
        default:
            pr_err("Unknown data format\n");
            return -EINVAL;
    }

    switch(priv->slots) {
        case 2:
            chn_num = TDMCTL_CHNUM_2;
            break;
        case 4:
            chn_num = TDMCTL_CHNUM_4;
            break;
        case 6:
            chn_num = TDMCTL_CHNUM_6;
            break;
        case 8:
            chn_num = TDMCTL_CHNUM_8;
            break;
        default:
            pr_err("Not support slot num\n");
            return -EINVAL;
    }

	regmap_update_bits(priv->regmap, TDM_TDMCTL,
			TDMCTL_DATAWTH_MSK, TDMCTL_DATAWTH_SEL(datawth));
	regmap_update_bits(priv->regmap, TDM_TDMCTL,
			TDMCTL_CHNUM_MSK, TDMCTL_CHNUM_SEL(chn_num));
    regmap_read(priv->regmap, TDM_TDMCTL, &tdmctl);
    pr_debug("%s TDM_TDMCTL=0x%x\n", __func__, tdmctl); 
    th1520_tdm_set_div(priv, params);

    return 0;
}


static const struct snd_soc_dai_ops th1520_tdm_dai_ops = {
    .probe = th1520_tdm_dai_probe,
    .startup = th1520_tdm_dai_startup,
    .shutdown = th1520_tdm_dai_shutdown,
    .trigger = th1520_tdm_dai_trigger,
    .set_fmt = th1520_tdm_set_fmt_dai,
    .hw_params = th1520_tdm_dai_hw_params,
};

static struct snd_soc_dai_driver th1520_tdm_soc_dai = {
    .capture = {
        .rates = TH1520_RATES,
        .formats = TH1520_FMTS,
        .channels_min = 1,
        .channels_max = 1,
    },
    .ops = &th1520_tdm_dai_ops,
};

static const struct snd_soc_component_driver th1520_tdm_soc_component = {
	.name		= "th1520_tdm",
};

static bool th1520_tdm_wr_reg(struct device *dev, unsigned int reg)
{
    return true;
}

static bool th1520_tdm_rd_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static const struct regmap_config th1520_tdm_regmap_config = {
        .reg_bits = 32,
        .reg_stride = 4,
        .val_bits = 32,
        .max_register = TDM_DIV0_LEVEL,
        .writeable_reg = th1520_tdm_wr_reg,
        .readable_reg = th1520_tdm_rd_reg,
        .cache_type = REGCACHE_NONE,
};

static int th1520_tdm_runtime_suspend(struct device *dev)
{
    struct th1520_tdm_priv *priv = dev_get_drvdata(dev);

    if (priv->slot_num != 1) {
        return 0;
    }

    clk_disable_unprepare(priv->clk);

	return 0;
}

static int th1520_tdm_runtime_resume(struct device *dev)
{
    struct th1520_tdm_priv *priv = dev_get_drvdata(dev);
    int ret;

    if (priv->slot_num != 1) {
        return 0;
    }

    ret = clk_prepare_enable(priv->clk);
    if (ret) {
            dev_err(priv->dev, "clock enable failed %d\n", ret);
            return ret;
    }

    return ret;
}

#ifdef CONFIG_PM_SLEEP
static int th1520_tdm_suspend(struct device *dev)
{
    struct th1520_tdm_priv *priv = dev_get_drvdata(dev);

    if (priv->slot_num != 1)  {
        return 0;
    }

    pm_runtime_get_sync(dev);
    
	regmap_read(priv->regmap, TDM_TDMCTL, &priv->suspend_tdmctl);
	regmap_read(priv->regmap, TDM_CHOFFSET1, &priv->suspend_choffset1);
	regmap_read(priv->regmap, TDM_CHOFFSET2, &priv->suspend_choffset2);
	regmap_read(priv->regmap, TDM_CHOFFSET3, &priv->suspend_choffset3);
	regmap_read(priv->regmap, TDM_CHOFFSET4, &priv->suspend_choffset4);
	regmap_read(priv->regmap, TDM_FIFOTL1, &priv->suspend_fifotl1);
	regmap_read(priv->regmap, TDM_FIFOTL2, &priv->suspend_fifotl2);
	regmap_read(priv->regmap, TDM_FIFOTL3, &priv->suspend_fifotl3);
	regmap_read(priv->regmap, TDM_FIFOTL4, &priv->suspend_fifotl4);
	regmap_read(priv->regmap, TDM_IMR, &priv->suspend_imr);
	regmap_read(priv->regmap, TDM_DMADL, &priv->suspend_dmadl);
	regmap_read(priv->regmap, TDM_DIV0_LEVEL, &priv->suspend_div0level);

    regmap_read(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG, &priv->cpr_peri_div_sel);
    regmap_read(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG, &priv->cpr_peri_clk_sel);
    reset_control_assert(priv->rst);

    pm_runtime_put_sync(dev);

	return 0;
}

static int th1520_tdm_resume(struct device *dev)
{
    struct th1520_tdm_priv *priv = dev_get_drvdata(dev);
    int ret;

    if (priv->slot_num != 1) {
        return 0;
    }
    pm_runtime_get_sync(dev);

    reset_control_deassert(priv->rst);
    regmap_write(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG, priv->cpr_peri_div_sel);
    regmap_write(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG, priv->cpr_peri_clk_sel);


	regmap_write(priv->regmap, TDM_TDMCTL, priv->suspend_tdmctl);
	regmap_write(priv->regmap, TDM_CHOFFSET1, priv->suspend_choffset1);
	regmap_write(priv->regmap, TDM_CHOFFSET2, priv->suspend_choffset2);
	regmap_write(priv->regmap, TDM_CHOFFSET3, priv->suspend_choffset3);
	regmap_write(priv->regmap, TDM_CHOFFSET4, priv->suspend_choffset4);
	regmap_write(priv->regmap, TDM_FIFOTL1, priv->suspend_fifotl1);
	regmap_write(priv->regmap, TDM_FIFOTL2, priv->suspend_fifotl2);
	regmap_write(priv->regmap, TDM_FIFOTL3, priv->suspend_fifotl3);
	regmap_write(priv->regmap, TDM_FIFOTL4, priv->suspend_fifotl4);
	regmap_write(priv->regmap, TDM_IMR, priv->suspend_imr);
	regmap_write(priv->regmap, TDM_DMADL, priv->suspend_dmadl);
	regmap_write(priv->regmap, TDM_DIV0_LEVEL, priv->suspend_div0level);

    pm_runtime_put_sync(dev);

    return ret;
}
#endif

static const struct of_device_id th1520_tdm_of_match[] = {
	{ .compatible = "xuantie,th1520-tdm"},
	{},
};
MODULE_DEVICE_TABLE(of, th1520_tdm_of_match);

irqreturn_t tdm_interrupt(int irq, void* dev_id)
{
    //struct th1520_tdm_priv* priv = (struct th1520_tdm_priv*)dev_id;
    return IRQ_HANDLED;
}

static int th1520_tdm_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    const char *sprop;
    const uint32_t *iprop;
    struct th1520_tdm_priv *tdm_priv;
    struct resource *res;
    struct device *dev = &pdev->dev;
    struct reset_control *resets;

    int data_register, ret = 0;

    tdm_priv = devm_kzalloc(&pdev->dev, sizeof(struct th1520_tdm_priv), GFP_KERNEL);
    if (!tdm_priv) {
        return -ENOMEM;
    }

    tdm_priv->dev = dev;

    iprop = of_get_property(np, "th1520,tdm_slots", NULL);
    if (iprop) {
        if (be32_to_cpup(iprop) == 2 || be32_to_cpup(iprop) == 4 || be32_to_cpup(iprop) == 6 || be32_to_cpup(iprop) == 8) {
            tdm_priv->slots = be32_to_cpup(iprop);
        } else {
            dev_err(dev, "invalid th1520,tdm_slots\n");
            return -EINVAL;
        }
    } else {
        tdm_priv->slots = 8;
    }

    iprop = of_get_property(np, "th1520,tdm_slot_num", NULL);
    if (iprop) {
        if (be32_to_cpup(iprop) >=1 && be32_to_cpup(iprop) <=8 ) {
            tdm_priv->slot_num = be32_to_cpup(iprop);
            sprintf(tdm_priv->name, "tdm-%d", tdm_priv->slot_num);
        } else {
            dev_err(dev, "invalid th1520,tdm_slot_num\n");
            return -EINVAL;
        }
    } else {
        dev_err(dev, "invalid th1520,tdm_slot_num\n");
        return -EINVAL;
    }

    dev_set_drvdata(dev, tdm_priv);

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);



    if (tdm_priv->slot_num == 1) {
        tdm_priv->clk = devm_clk_get(&pdev->dev, "pclk");
        if (IS_ERR(tdm_priv->clk))
                    return PTR_ERR(tdm_priv->clk);

        tdm_priv->regs = devm_ioremap_resource(dev, res);
        if (IS_ERR(tdm_priv->regs)) {
            return PTR_ERR(tdm_priv->regs);
        }

        tdm_priv->regmap = devm_regmap_init_mmio(dev, tdm_priv->regs,
                                                    &th1520_tdm_regmap_config);
        if (IS_ERR(tdm_priv->regmap)) {
            dev_err(dev, "Failed to initialise managed register map\n");
            return PTR_ERR(tdm_priv->regmap);
        }

        tdm_priv->audio_cpr_regmap = syscon_regmap_lookup_by_phandle(np, "audio-cpr-regmap");
        if (IS_ERR(tdm_priv->audio_cpr_regmap)) {
            dev_err(dev, "cannot find regmap for audio cpr register\n");
            return -EINVAL;
        }
        //AUDIO_DIV1 set to 1/6. 812.8512MHz / 6 = 135.4752MHz
        regmap_update_bits(tdm_priv->audio_cpr_regmap,
                                CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV1_SEL_MSK, CPR_AUDIO_DIV1_SEL(5));
        //AUDIO_DIV0 set to 1/6. 294.912MHz / 6 = 49.152MHz
        regmap_update_bits(tdm_priv->audio_cpr_regmap,
                                CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV0_SEL_MSK, CPR_AUDIO_DIV0_SEL(5));

        resets = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
        if (IS_ERR(resets)) {
            ret = PTR_ERR(resets);
            return ret;
        }
        tdm_priv->rst = resets;

        reset_control_deassert(tdm_priv->rst);

        pm_runtime_enable(&pdev->dev);
        pm_runtime_resume_and_get(&pdev->dev); // clk gate is enabled by hardware as default register value
        pm_runtime_put_sync(&pdev->dev);

        tdm_priv->irq = platform_get_irq(pdev, 0);
        if (tdm_priv->irq== 0) {
            dev_err(dev, "could not map IRQ.\n");
            return -ENXIO;
        }

        //ret = request_irq(tdm_priv->irq , tdm_interrupt,
        //		IRQF_SHARED|IRQF_TRIGGER_RISING, "AUDIO_TDM_IRQ", (char *)tdm_priv);
        //if (ret) {
        //	dev_err(dev, "%s[%d]:request irq error!\n", __func__, __LINE__);
        //	return ret;
        //}
    }

    switch(tdm_priv->slot_num) {
        case 1:
            data_register = TDM_LDR1;
            break;
        case 2:
            data_register = TDM_RDR1;
            break;
        case 3:
            data_register = TDM_LDR2;
            break;
        case 4:
            data_register = TDM_RDR2;
            break;
        case 5:
            data_register = TDM_LDR3;
            break;
        case 6:
            data_register = TDM_RDR3;
            break;
        case 7:
            data_register = TDM_LDR4;
            break;
        case 8:
            data_register = TDM_RDR4;
            break;
        default:
            dev_err(dev, "invalid slot_num\n");
            return -EINVAL;
    }

    tdm_priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
    tdm_priv->dma_params_rx.maxburst = I2S_DMA_MAXBURST;
    tdm_priv->dma_params_rx.addr = res->start + data_register;

    ret = th1520_pcm_dma_init(pdev, TH1520_TDM_DMABUF_SIZE);
	if (ret) {
		dev_err(dev, "th1520_pcm_dma_init error\n");
		return -EIO;
	}

	th1520_tdm_soc_dai.name = tdm_priv->name;
	ret = devm_snd_soc_register_component(dev, &th1520_tdm_soc_component,
                        &th1520_tdm_soc_dai, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd component register\n");
	}

    return ret;
}

static int th1520_tdm_remove(struct platform_device *pdev)
{
    struct th1520_tdm_priv *tdm_priv = dev_get_drvdata(&pdev->dev);
    pm_runtime_disable(&pdev->dev);
    if (!pm_runtime_status_suspended(&pdev->dev))
            th1520_tdm_runtime_suspend(&pdev->dev);
    clk_disable_unprepare(tdm_priv->clk);
    return 0;
}

static const struct dev_pm_ops th1520_tdm_pm_ops = {
    SET_RUNTIME_PM_OPS(th1520_tdm_runtime_suspend, th1520_tdm_runtime_resume, NULL)
    SET_SYSTEM_SLEEP_PM_OPS(th1520_tdm_suspend,
                    th1520_tdm_resume)
};

static struct platform_driver th1520_tdm_driver = {
    .driver = {
        .name = "th1520-tdm-audio",
        .pm = &th1520_tdm_pm_ops,
        .of_match_table = th1520_tdm_of_match,
    },
    .probe = th1520_tdm_probe,
    .remove = th1520_tdm_remove,
};

module_platform_driver(th1520_tdm_driver);

MODULE_AUTHOR("nanli.yd <nanli.yd@linux.alibaba.com>");
MODULE_DESCRIPTION("XuanTie TH1520 audio driver");
MODULE_LICENSE("GPL v2");

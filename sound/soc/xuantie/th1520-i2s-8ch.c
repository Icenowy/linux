/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XuanTie TH1520 audio I2S audio support
 *
 * Copyright (C) 2024 Alibaba Group Holding Limited.
 *
 * Author: Shuofeng Ren <shuofeng.rsf@linux.alibaba.com>
 * Author: David Li <davidli.li@linux.alibaba.com>
 *
 */
//#define DEBUG

#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "th1520-audio-cpr.h"
#include "th1520-i2s.h"
#include "th1520-pcm.h"

#define TH1520_I2S_DMABUF_SIZE	(64 * 1024 * 10)

static int i2s_8ch_probe_flag = 0;

static void th1520_i2s_8ch_set_div_sclk(struct th1520_i2s_priv *chip,
					u32 sample_rate, unsigned int div_val)
{
	u32 div;
	u32 div0;
	u32 cpr_div = (IIS_SRC_CLK/AUDIO_IIS_SRC0_CLK)-1;
	if(!chip->regs)
		return;

	div = AUDIO_IIS_SRC0_CLK / IIS_MCLK_SEL;
	div0 = (div + div % sample_rate) / sample_rate / div_val;
	writel(div0, chip->regs + I2S_DIV0_LEVEL);
	th1520_audio_cpr_set(chip, CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV0_SEL_MSK,
			     CPR_AUDIO_DIV0_SEL(cpr_div));
}

static inline void th1520_snd_txctrl(struct th1520_i2s_priv *chip, bool on)
{
	u32 dma_en = 0;
	u32 i2s_8ch_en = 0;
	u32 i2s_8ch_imr = 0;

	if(!chip->regs)
		return;

	if (on) {
		dma_en |= DMACR_TDMAE_EN;
		i2s_8ch_en |= IISEN_I2SEN;
		writel(dma_en, chip->regs + I2S_DMACR);
		writel(i2s_8ch_en, chip->regs + I2S_IISEN);
	} else {
		dma_en &= ~DMACR_TDMAE_EN;
		i2s_8ch_en &= ~IISEN_I2SEN;
		i2s_8ch_imr  = readl(chip->regs + I2S_IMR);
		i2s_8ch_imr &= ~(IMR_TXUIRM_INTR_MSK);
		i2s_8ch_imr &= ~(IMR_TXEIM_INTR_MSK);
		writel(i2s_8ch_imr, chip->regs + I2S_IMR);
		writel(dma_en, chip->regs + I2S_DMACR);
		writel(i2s_8ch_en, chip->regs + I2S_IISEN);
	}
}

static inline void th1520_snd_rxctrl(struct th1520_i2s_priv *chip, bool on)
{
	u32 dma_en = 0;
	u32 i2s_8ch_en = 0;

	if(!chip->regs)
		return;

	if (on) {
		dma_en |= DMACR_RDMAE_EN;
		i2s_8ch_en |= IISEN_I2SEN;
		writel(I2S_DMA_RX_THRESHOLD, chip->regs + I2S_DMARDLR);
	} else {
		dma_en &= ~DMACR_RDMAE_EN;
		i2s_8ch_en &= ~IISEN_I2SEN;
	}

	writel(dma_en, chip->regs + I2S_DMACR);
	writel(i2s_8ch_en, chip->regs + I2S_IISEN);
}

static int th1520_i2s_8ch_dai_startup(struct snd_pcm_substream *substream,
				      struct snd_soc_dai *dai)
{
	return 0;
}

static void th1520_i2s_8ch_dai_shutdown(struct snd_pcm_substream *substream,
					struct snd_soc_dai *dai)
{
	struct th1520_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		th1520_snd_rxctrl(priv, 0);

	clk_disable_unprepare(priv->clk);
}

/**
 * th1520_i2s_8ch_dai_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 */
static int th1520_i2s_8ch_dai_trigger(struct snd_pcm_substream *substream,
				      int cmd, struct snd_soc_dai *dai)
{
	int ret = 0;

	struct th1520_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	if(!priv->regmap)
		return 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (tx) {
			th1520_snd_txctrl(priv, 1);
			priv->state |= I2S_STATE_TX_RUNNING;
		}
		else {
			th1520_snd_rxctrl(priv, 1);
			priv->state |= I2S_STATE_RX_RUNNING;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (tx) {
			// work around for DMAC stop issue
			dmaengine_terminate_async(snd_dmaengine_pcm_get_chan(substream));
			th1520_snd_txctrl(priv, 0);
			priv->state &= ~I2S_STATE_TX_RUNNING;
		} else {
			th1520_snd_rxctrl(priv, 0);
			priv->state &= ~I2S_STATE_RX_RUNNING;
		}
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (tx) {
		 	// work around for DMAC stop issue
			dmaengine_pause(snd_dmaengine_pcm_get_chan(substream)); 
			th1520_snd_txctrl(priv, 0);
		} else {
			th1520_snd_rxctrl(priv, 0);
		}
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int th1520_i2s_8ch_set_fmt_dai(struct snd_soc_dai *cpu_dai,
				      unsigned int fmt)
{
	struct th1520_i2s_priv *priv = snd_soc_dai_get_drvdata(cpu_dai);
	u32 cnfout = 0;
	u32 cnfin = 0;

	if(!priv->regmap)
		return 0;

	pm_runtime_resume_and_get(priv->dev);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		cnfin |= CNFIN_I2S_RXMODE_MASTER_MODE;
		cnfout |= IISCNFOUT_TSAFS_I2S;
		cnfout &= ~IISCNFOUT_I2S_TXMODE_SLAVE;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		cnfin &= ~CNFIN_I2S_RXMODE_MASTER_MODE;
		cnfout |= IISCNFOUT_TSAFS_RIGHT_JUSTIFIED;
		cnfout |= IISCNFOUT_I2S_TXMODE_SLAVE;
		break;
	default:
		pr_err("Unknown fmt dai\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		cnfout |= IISCNFOUT_TSAFS_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		cnfout |= IISCNFOUT_TSAFS_RIGHT_JUSTIFIED;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		cnfout |= IISCNFOUT_TSAFS_LEFT_JUSTIFIED;
		break;
	default:
		pr_err("Unknown fmt dai\n");
		return -EINVAL;
	}

	regmap_update_bits(priv->regmap, I2S_IISCNF_OUT, IISCNFOUT_TSAFS_MSK,
			   cnfout);

	cnfin |= CNFIN_I2S_RXMODE_MASTER_MODE;
	regmap_update_bits(priv->regmap, I2S_IISCNF_IN, CNFIN_I2S_RXMODE_Msk,
			   cnfin);

	pm_runtime_put_sync(priv->dev);

	return 0;
}

static int th1520_i2s_8ch_dai_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params,
					struct snd_soc_dai *dai)
{
	u32 val;
	u32 rate;
	u32 funcmode;
	u32 iiscnf_in;
	u32 iiscnf_out;
	u32 i2s_8ch_en;
	u32 len = 0;
	u32 sclk_sel = 0;
	u32 channels = params_channels(params);
	struct th1520_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	
	if(!priv->regs || !priv->regmap)
		return 0;

	rate = params_rate(params);

	iiscnf_out = readl(priv->regs + I2S_IISCNF_OUT);
	iiscnf_in = readl(priv->regs + I2S_IISCNF_IN);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		val |= I2S_DATA_8BIT_WIDTH_32BIT;
		len = 32;
                break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= I2S_DATA_WIDTH_16BIT;
		len = 32;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val |= I2S_DATA_WIDTH_24BIT;
		len = 32;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= I2S_DATA_WIDTH_32BIT;
		len = 32;
		break;
	default:
		pr_err("Unknown data format\n");
		return -EINVAL;
	}

	sclk_sel = len*STEREO_CHANNEL;

	switch (sclk_sel) {
	case 16:
		val |= FSSTA_SCLK_SEL_16;
		break;
	case 32:
		val |= FSSTA_SCLK_SEL_32;
		break;
	case 48:
		val |= FSSTA_SCLK_SEL_48;
		break;
	case 64:
		val |= FSSTA_SCLK_SEL_64;
		break;
	default:
		pr_err("Not support channel num %d\n", channels);
		return -EINVAL;
	}

	i2s_8ch_en &= ~IISEN_I2SEN;
	writel(i2s_8ch_en, priv->regs + I2S_IISEN);

	regmap_update_bits(priv->regmap, I2S_FSSTA,
			   FSSTA_DATAWTH_Msk | FSSTA_SCLK_SEL_Msk, val);
	funcmode = readl(priv->regs + I2S_FUNCMODE);
	if (tx) {
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_CH1_ENABLE;
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode |= FUNCMODE_TMODE;
	} else {
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode |= FUNCMODE_CH0_ENABLE;
		funcmode |= FUNCMODE_CH1_ENABLE;
		funcmode |= FUNCMODE_CH2_ENABLE;
		funcmode |= FUNCMODE_CH3_ENABLE;
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode |= FUNCMODE_RMODE;
	}

	writel(funcmode, priv->regs + I2S_FUNCMODE);

	if (channels == MONO_SOURCE) {
		iiscnf_out |= IISCNFOUT_TX_VOICE_EN_MONO;
		iiscnf_in |= CNFIN_RX_CH_SEL_LEFT;
		iiscnf_in |= CNFIN_RVOICEEN_MONO;
	} else {
		iiscnf_out &= ~IISCNFOUT_TX_VOICE_EN_MONO;
		iiscnf_in &= ~CNFIN_RX_CH_SEL_LEFT;
		iiscnf_in &= ~CNFIN_RVOICEEN_MONO;
	}
	iiscnf_in |= CNFIN_I2S_RXMODE_MASTER_MODE;

	if (tx)
		writel(iiscnf_out, priv->regs + I2S_IISCNF_OUT);
	else
		writel(iiscnf_in, priv->regs + I2S_IISCNF_IN);

	th1520_i2s_8ch_set_div_sclk(priv, rate, DIV_DEFAULT);

	return 0;
}

static int th1520_hdmi_dai_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{

	u32 val;
	u32 rate;
	u32 funcmode;
	u32 iiscnf_out;
	u32 i2s_8ch_en;
	u32 len = 0;
	u32 channels = params_channels(params);
	struct th1520_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	if(!priv->regs || !priv->regmap)
		return 0;

	rate = params_rate(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= I2S_DATA_WIDTH_16BIT;
		len = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val |= I2S_DATA_WIDTH_24BIT;
		len = 24;
		break;
	default:
		pr_err("Unknown data format\n");
		return -EINVAL;
	}

	val |= FSSTA_SCLK_SEL_64;

	i2s_8ch_en &= ~IISEN_I2SEN;
	writel(i2s_8ch_en, priv->regs + I2S_IISEN);

	regmap_update_bits(priv->regmap, I2S_FSSTA,
			   FSSTA_DATAWTH_Msk | FSSTA_SCLK_SEL_Msk, val);
	funcmode = readl(priv->regs + I2S_FUNCMODE);
	if (tx) {
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode |= FUNCMODE_TMODE;
	} else {
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode |= FUNCMODE_RMODE;
	}

	writel(funcmode, priv->regs + I2S_FUNCMODE);

	iiscnf_out = readl(priv->regs + I2S_IISCNF_OUT);
	if (channels == MONO_SOURCE)
		iiscnf_out |= IISCNFOUT_TX_VOICE_EN_MONO;
	else
		iiscnf_out &= ~IISCNFOUT_TX_VOICE_EN_MONO;

	writel(iiscnf_out, priv->regs + I2S_IISCNF_OUT);

	th1520_i2s_8ch_set_div_sclk(priv, rate, DIV_DEFAULT);

	return 0;
}

static int th1520_i2s_8ch_dai_probe(struct snd_soc_dai *dai)
{
	struct th1520_i2s_priv *i2s_8ch = snd_soc_dai_get_drvdata(dai);

	if(i2s_8ch)
		snd_soc_dai_init_dma_data(dai, &i2s_8ch->dma_params_tx,
					  &i2s_8ch->dma_params_rx);

	return 0;
}

static const struct snd_soc_dai_ops th1520_i2s_8ch_dai_ops = {
	.probe		= th1520_i2s_8ch_dai_probe,
	.startup	= th1520_i2s_8ch_dai_startup,
	.shutdown	= th1520_i2s_8ch_dai_shutdown,
	.trigger	= th1520_i2s_8ch_dai_trigger,
	.set_fmt	= th1520_i2s_8ch_set_fmt_dai,
	.hw_params	= th1520_i2s_8ch_dai_hw_params,
};

static const struct snd_soc_dai_ops th1520_hdmi_dai_ops = {
	.startup        = th1520_i2s_8ch_dai_startup,
	.shutdown       = th1520_i2s_8ch_dai_shutdown,
	.trigger        = th1520_i2s_8ch_dai_trigger,
	.set_fmt        = th1520_i2s_8ch_set_fmt_dai,
	.hw_params      = th1520_hdmi_dai_hw_params,
};

static struct snd_soc_dai_driver th1520_i2s_8ch_soc_dai[] = {
	{
		.playback	= {
			.rates		= TH1520_RATES,
			.formats	= TH1520_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.capture	= {
			.rates		= TH1520_RATES,
			.formats	= TH1520_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops		= &th1520_i2s_8ch_dai_ops,
	},
};


static const struct snd_soc_component_driver th1520_i2s_8ch_soc_component = {
	.name		= "th1520_i2s_8ch",
};

static const struct regmap_config th1520_i2s_8ch_regmap_config = {
        .reg_bits	= 32,
        .reg_stride	= 4,
        .val_bits	= 32,
        .max_register	= I2S_DR4,
        .writeable_reg	= th1520_i2s_wr_reg,
        .readable_reg	= th1520_i2s_rd_reg,
        .cache_type	= REGCACHE_NONE,
};

static int th1520_i2s_8ch_runtime_suspend(struct device *dev)
{
	struct th1520_i2s_priv *priv = dev_get_drvdata(dev);

	if(!priv->regmap)
		return 0;

	regcache_cache_only(priv->regmap, true);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int th1520_i2s_8ch_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct th1520_i2s_priv *priv = dev_get_drvdata(dev);

	if(!priv->regmap)
		return 0;

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(priv->dev, "clock enable failed %d\n", ret);
		return ret;
	}

	regcache_cache_only(priv->regmap, false);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int th1520_i2s_8ch_suspend(struct device *dev)
{
	struct th1520_i2s_priv *priv = dev_get_drvdata(dev);

	if(!priv->regmap)
		return 0;

	pm_runtime_get_sync(dev);

	regmap_read(priv->regmap, I2S_DIV0_LEVEL, &priv->suspend_div0_level);
	regmap_read(priv->regmap, I2S_DIV3_LEVEL, &priv->suspend_div3_level);
	regmap_read(priv->regmap, I2S_IISCNF_IN, &priv->suspend_iiscnf_in);
	regmap_read(priv->regmap, I2S_FSSTA, &priv->suspend_fssta);
	regmap_read(priv->regmap, I2S_IISCNF_OUT, &priv->suspend_ii2cnf_out);
	regmap_read(priv->regmap, I2S_FADTLR, &priv->suspend_fadtlr);
	regmap_read(priv->regmap, I2S_SCCR, &priv->suspend_sccr);
	regmap_read(priv->regmap, I2S_TXFTLR, &priv->suspend_txftlr);
	regmap_read(priv->regmap, I2S_RXFTLR, &priv->suspend_rxftlr);
	regmap_read(priv->regmap, I2S_IMR, &priv->suspend_imr);
	regmap_read(priv->regmap, I2S_DMATDLR, &priv->suspend_dmatdlr);
	regmap_read(priv->regmap, I2S_DMARDLR, &priv->suspend_dmardlr);
	regmap_read(priv->regmap, I2S_FUNCMODE, &priv->suspend_funcmode);

	regmap_read(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG,
		    &priv->cpr_peri_div_sel);
	regmap_read(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG,
		    &priv->cpr_peri_ctrl);
	regmap_read(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG,
		    &priv->cpr_peri_clk_sel);
	reset_control_assert(priv->rst);

	pm_runtime_put_sync(dev);

	return 0;
}

static int th1520_i2s_8ch_resume(struct device *dev)
{
	int ret;
	struct th1520_i2s_priv *priv = dev_get_drvdata(dev);

	if(!priv->regmap)
		return 0;

	pm_runtime_get_sync(dev);

	reset_control_deassert(priv->rst);

	regmap_update_bits(priv->audio_cpr_regmap, CPR_IP_RST_REG,
			   CPR_I2S8CH_SRST_N_SEL_MSK, CPR_I2S8CH_SRST_N_SEL(1));

	regmap_write(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG,
		     priv->cpr_peri_ctrl);

	regmap_write(priv->regmap, I2S_IISEN, 0);
	regmap_write(priv->regmap, I2S_FSSTA, priv->suspend_fssta);
	regmap_write(priv->regmap, I2S_FUNCMODE,
		     priv->suspend_funcmode | FUNCMODE_TMODE_WEN |
			FUNCMODE_RMODE_WEN);
	regmap_write(priv->regmap, I2S_IISCNF_IN, priv->suspend_iiscnf_in);
	regmap_write(priv->regmap, I2S_IISCNF_OUT, priv->suspend_ii2cnf_out);
	regmap_write(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG,
		     priv->cpr_peri_clk_sel);
	regmap_write(priv->regmap, I2S_DIV0_LEVEL, priv->suspend_div0_level);
	regmap_write(priv->regmap, I2S_DIV3_LEVEL, priv->suspend_div3_level);
	regmap_write(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG,
		     priv->cpr_peri_div_sel);

	pm_runtime_put_sync(dev);

	return ret;
}
#endif

static const struct of_device_id th1520_i2s_8ch_of_match[] = {
	{ .compatible = "xuantie,th1520-i2s-8ch"},
	{},
};
MODULE_DEVICE_TABLE(of, th1520_i2s_8ch_of_match);

struct th1520_i2s_priv *host_priv;

static int th1520_audio_i2s_8ch_probe(struct platform_device *pdev)
{

	int ret;
	unsigned int irq;
	const char *sprop;
	const uint32_t *iprop;
	struct resource *res;
	struct th1520_i2s_priv *priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct reset_control *resets;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);

	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	if (strstr(pdev->name, AUDIO_I2S_8CH_SD0)) {
		strcpy(priv->name, AUDIO_I2S_8CH_SD0);
	}
	else if (strstr(pdev->name, AUDIO_I2S_8CH_SD1)) {
		strcpy(priv->name, AUDIO_I2S_8CH_SD1);
	}
	else if (strstr(pdev->name, AUDIO_I2S_8CH_SD2)) {
		strcpy(priv->name, AUDIO_I2S_8CH_SD2);
	}
	else if (strstr(pdev->name, AUDIO_I2S_8CH_SD3)) {
		strcpy(priv->name, AUDIO_I2S_8CH_SD3);
	}
	else {
		pr_err("unsupport audio dev name: %s\n", pdev->name);
		return -EINVAL;
	}

	dev_set_drvdata(&pdev->dev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if(i2s_8ch_probe_flag) {
		priv->regs = NULL;
		priv->regmap = NULL;
		priv->audio_cpr_regmap = NULL;
	} else {
		i2s_8ch_probe_flag = 1;
		host_priv = priv;

		priv->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->regs))
			return PTR_ERR(priv->regs);

		priv->regmap = devm_regmap_init_mmio(&pdev->dev, priv->regs,
						&th1520_i2s_8ch_regmap_config);
		if (IS_ERR(priv->regmap)) {
			dev_err(&pdev->dev,
				"Failed to initialise managed register map\n");
			return PTR_ERR(priv->regmap);
		}

		priv->audio_cpr_regmap = syscon_regmap_lookup_by_phandle(np,
						"audio-cpr-regmap");
		if (IS_ERR(priv->audio_cpr_regmap)) {
			dev_err(dev,
				"cannot find regmap for audio cpr register\n");
			return -EINVAL;
		}

		// enable i2s sync
		th1520_audio_cpr_set(priv, CPR_PERI_CTRL_REG,
						CPR_VAD_I2SIN_SYNC_MSK, CPR_VAD_I2SIN_SYNC_EN);

		resets = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
		if (IS_ERR(resets)) {
			ret = PTR_ERR(resets);
			return ret;
		}
		priv->rst = resets;

		priv->clk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(priv->clk))
			return PTR_ERR(priv->clk);

		reset_control_deassert(priv->rst);
		pm_runtime_enable(&pdev->dev);
		// clk gate is enabled by hardware as default register value
		pm_runtime_resume_and_get(&pdev->dev);
		pm_runtime_put_sync(&pdev->dev);

		irq = platform_get_irq(pdev, 0);

		if (!res || (int)irq <= 0) {
			dev_err(&pdev->dev,
				"Not enough th1520 platform resources.\n");
			return -ENODEV;
		}
	}

	priv->audio_pin_regmap = NULL;

	priv->dma_params_tx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	priv->dma_params_tx.maxburst = I2S_DMA_MAXBURST;
	priv->dma_params_rx.maxburst = I2S_DMA_MAXBURST;

	if (!strcmp(priv->name, AUDIO_I2S_8CH_SD0)) {
		priv->dma_params_tx.addr = res->start + I2S_DR;
		priv->dma_params_rx.addr = res->start + I2S_DR;
	} else if (!strcmp(priv->name, AUDIO_I2S_8CH_SD1)) {
		priv->dma_params_tx.addr = res->start + I2S_DR1;
		priv->dma_params_rx.addr = res->start + I2S_DR1;
	} else if (!strcmp(priv->name, AUDIO_I2S_8CH_SD2)) {
		priv->dma_params_tx.addr = res->start + I2S_DR2;
		priv->dma_params_rx.addr = res->start + I2S_DR2;
	} else if (!strcmp(priv->name, AUDIO_I2S_8CH_SD3)) {
		priv->dma_params_tx.addr = res->start + I2S_DR3;
		priv->dma_params_rx.addr = res->start + I2S_DR3;
	}

	th1520_pcm_probe(pdev, priv, TH1520_I2S_DMABUF_SIZE);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &th1520_i2s_8ch_soc_component,
					      th1520_i2s_8ch_soc_dai,
					    ARRAY_SIZE(th1520_i2s_8ch_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd component register\n");
		goto err_pm_disable;
	}

	return ret;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int th1520_i2s_8ch_remove(struct platform_device *pdev)
{
        struct th1520_i2s_priv *priv = dev_get_drvdata(&pdev->dev);

        pm_runtime_disable(&pdev->dev);
        if (!pm_runtime_status_suspended(&pdev->dev))
                th1520_i2s_8ch_runtime_suspend(&pdev->dev);

        clk_disable_unprepare(priv->clk);

        return 0;
}

static const struct dev_pm_ops th1520_i2s_8ch_pm_ops = {
	SET_RUNTIME_PM_OPS(th1520_i2s_8ch_runtime_suspend,
        		   th1520_i2s_8ch_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(th1520_i2s_8ch_suspend,
				th1520_i2s_8ch_resume)
};

static struct platform_driver th1520_i2s_8ch_driver = {
	.driver = {
		.name		= "th1520-pcm-audio-8ch",
		.pm		= &th1520_i2s_8ch_pm_ops,
		.of_match_table = th1520_i2s_8ch_of_match,
	},
	.probe	= th1520_audio_i2s_8ch_probe,
	.remove	= th1520_i2s_8ch_remove,
};

module_platform_driver(th1520_i2s_8ch_driver);

MODULE_AUTHOR("shuofeng.ren <shuofeng.rsf@linux.alibaba.com>");
MODULE_DESCRIPTION("XuanTie TH1520 audio driver");
MODULE_LICENSE("GPL v2");

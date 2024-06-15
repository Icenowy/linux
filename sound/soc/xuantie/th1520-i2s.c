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

#define DEBUG

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

#define TH1520_I2S_DMABUF_SIZE	(64 * 1024)

static u32 th1520_special_sample_rates[] = { 11025, 22050, 44100, 88200 };

static void th1520_i2s_set_div_sclk(struct th1520_i2s_priv *chip,
				    u32 sample_rate, unsigned int div_val)
{
	int i;
	u32 div;
	u32 div0;
	u32 i2s_src_clk = 0;
	u32 cpr_div = (IIS_SRC_CLK / AUDIO_IIS_SRC0_CLK) - 1;

	if (!strcmp(chip->name, AP_I2S)) {
		div = IIS_SRC_CLK / IIS_MCLK_SEL;
	} else {
		for (i = 0; i < ARRAY_SIZE(th1520_special_sample_rates); i++) {
			if (th1520_special_sample_rates[i] == sample_rate) {
				i2s_src_clk = 1;
				break;
			}
		}
		if (! strcmp(chip->name, AUDIO_I2S0)) {
			if (!i2s_src_clk) {
				th1520_audio_cpr_set(chip, CPR_PERI_CLK_SEL_REG,
						     CPR_I2S0_SRC_SEL_MSK,
						     CPR_I2S0_SRC_SEL(0));
				div = AUDIO_IIS_SRC0_CLK / IIS_MCLK_SEL;
			} else {
				th1520_audio_cpr_set(chip, CPR_PERI_CLK_SEL_REG,
						     CPR_I2S0_SRC_SEL_MSK,
						     CPR_I2S0_SRC_SEL(2));
				div = AUDIO_IIS_SRC1_CLK / IIS_MCLK_SEL;
			}
		} else if (!strcmp(chip->name, AUDIO_I2S1)) {
			if (!i2s_src_clk) {
				th1520_audio_cpr_set(chip, CPR_PERI_CLK_SEL_REG,
						     CPR_I2S1_SRC_SEL_MSK,
						     CPR_I2S1_SRC_SEL(0));
				div = AUDIO_IIS_SRC0_CLK / IIS_MCLK_SEL;
			} else {
				th1520_audio_cpr_set(chip, CPR_PERI_CLK_SEL_REG,
						     CPR_I2S1_SRC_SEL_MSK,
						     CPR_I2S1_SRC_SEL(2));
				div = AUDIO_IIS_SRC1_CLK / IIS_MCLK_SEL;
			}
		} else if (!strcmp(chip->name, AUDIO_I2S2)) {
			if (!i2s_src_clk) {
				th1520_audio_cpr_set(chip, CPR_PERI_CLK_SEL_REG, 
						     CPR_I2S2_SRC_SEL_MSK,
						     CPR_I2S2_SRC_SEL(0));
				div = AUDIO_IIS_SRC0_CLK / IIS_MCLK_SEL;
			} else {
				th1520_audio_cpr_set(chip, CPR_PERI_CLK_SEL_REG,
						     CPR_I2S2_SRC_SEL_MSK,
						     CPR_I2S2_SRC_SEL(2));
				div = AUDIO_IIS_SRC1_CLK / IIS_MCLK_SEL;
			}
		}
	}

	div0 = (div + div % sample_rate) / sample_rate / div_val;

	writel(div0, chip->regs + I2S_DIV0_LEVEL);
	th1520_audio_cpr_set(chip, CPR_PERI_DIV_SEL_REG, CPR_AUDIO_DIV0_SEL_MSK,
			     CPR_AUDIO_DIV0_SEL(cpr_div));
}

static inline void th1520_snd_txctrl(struct th1520_i2s_priv *chip, bool on)
{
	u32 dma_en = 0;
	u32 i2s_en = 0;
	u32 i2s_status = 0;
	unsigned long flags;

	/* get current dma status, save rx configuration */
	dma_en = readl(chip->regs + I2S_DMACR);
	if (on) {
		dma_en |= DMACR_TDMAE_EN;
		i2s_en |= IISEN_I2SEN;
		writel(dma_en, chip->regs + I2S_DMACR);
		writel(i2s_en, chip->regs + I2S_IISEN);
	} else {
		dma_en &= ~DMACR_TDMAE_EN;
		local_irq_save(flags);
		do {
			i2s_status  = readl(chip->regs + I2S_SR);
		} while ((i2s_status & SR_TXBUSY_STATUS) || !(i2s_status & SR_TFNF_TX_FIFO_NOT_FULL));
		writel(dma_en, chip->regs + I2S_DMACR);
		local_irq_restore(flags);
		/*
		* The enablement of I2S can onlybe truned off when
		* the DMA configuration for RX and TX is completely disabled.
		*/
		if ( ((DMACR_TDMAE_MSK | DMACR_RDMAE_MSK) & dma_en) == 0) {
			i2s_en &= ~IISEN_I2SEN;
			writel(i2s_en, chip->regs + I2S_IISEN);
		}
	}
}

static inline void th1520_snd_rxctrl(struct th1520_i2s_priv *chip, bool on)
{
	u32 dma_en;
	u32 i2s_en;

	/* get current dma status, save tx configuration */
	dma_en = readl(chip->regs + I2S_DMACR);
	if (on) {
		dma_en |= DMACR_RDMAE_EN;
		i2s_en |= IISEN_I2SEN;
		writel(dma_en, chip->regs + I2S_DMACR);
		writel(i2s_en, chip->regs + I2S_IISEN);
	} else {
		dma_en &= ~DMACR_RDMAE_EN;
		writel(dma_en, chip->regs + I2S_DMACR);
		/*
		* The enablement of I2S can onlybe truned off when
		* the DMA confgiguratio for RX and TX is completely disabled.
		*/
		if ( ((DMACR_TDMAE_MSK | DMACR_RDMAE_MSK) & dma_en) == 0) {
			i2s_en &= ~IISEN_I2SEN;
			writel(i2s_en, chip->regs + I2S_IISEN);
		}
	}
}

static int th1520_i2s_dai_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	pr_debug("%s: %s %s\n", __func__, substream->pcm->name, dai->name);
	return 0;
}

static void th1520_i2s_dai_shutdown(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct th1520_i2s_priv *i2s_private = snd_soc_dai_get_drvdata(dai);

	pr_debug("%s: %s %s\n", __func__, substream->pcm->name, dai->name);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		th1520_snd_rxctrl(i2s_private, 0);
}

/**
 * th1520_i2s_dai_trigger: start and stop the DMA transfer.
 *
 * This function is called by ALSA to start, stop, pause, and resume the DMA
 * transfer of data.
 */
static int th1520_i2s_dai_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *dai)
{
	int ret = 0;

	struct th1520_i2s_priv *priv = snd_soc_dai_get_drvdata(dai);
        bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	pr_debug("%s: %s %s cmd=%d tx=%d\n", __func__, substream->pcm->name,
		dai->name, cmd, tx);

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
		if (tx) {
			th1520_snd_txctrl(priv, 0);
			priv->state &= ~I2S_STATE_TX_RUNNING;
		} else {
			th1520_snd_rxctrl(priv, 0);
			priv->state &= ~I2S_STATE_RX_RUNNING;
		}
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
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

static int th1520_i2s_set_fmt_dai(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	u32 cnfout = 0;
	u32 cnfin = 0;
	struct th1520_i2s_priv *i2s_private = snd_soc_dai_get_drvdata(cpu_dai);

	pr_debug("i2s fmt: <0x%x>\n", fmt);
	pm_runtime_resume_and_get(i2s_private->dev);

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

	regmap_update_bits(i2s_private->regmap, I2S_IISCNF_OUT,
			   IISCNFOUT_TSAFS_MSK, cnfout);

	cnfin |= CNFIN_I2S_RXMODE_MASTER_MODE;

	regmap_update_bits(i2s_private->regmap, I2S_IISCNF_IN,
			   CNFIN_I2S_RXMODE_Msk,
			   cnfin);

	pm_runtime_put_sync(i2s_private->dev);

	return 0;
}

static int th1520_i2s_dai_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	u32 val;
	u32 len = 0;
	u32 sclk_sel = 0;
	u32 rate;
	u32 funcmode;
	u32 iiscnf_out;
	u32 iiscnf_in;
	u32 i2s_en = 0;
	struct th1520_i2s_priv *i2s_private = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

	u32 channels = params_channels(params);

	rate = params_rate(params);

	iiscnf_out = readl(i2s_private->regs + I2S_IISCNF_OUT);
	iiscnf_in = readl(i2s_private->regs + I2S_IISCNF_IN);

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
		val |= I2S_DATA_24BIT_WIDTH_32BIT;
		len = 32;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= I2S_DATA_WIDTH_32BIT;
		len = 32;
		break;
	default:
		pr_err("Unknown data format: %d\n", params_format(params));
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

	/* 
	 * FUNCMODE,I2S_IISCNF_OUT,I2S_IISCNF_IN registers,
	 * it is impossible to write to this register when I2S is enabled
	 */
	i2s_en &= ~IISEN_I2SEN;
	writel(i2s_en, i2s_private->regs + I2S_IISEN);

	regmap_update_bits(i2s_private->regmap, I2S_FSSTA,
			   FSSTA_DATAWTH_Msk | FSSTA_SCLK_SEL_Msk, val);
	funcmode = readl(i2s_private->regs + I2S_FUNCMODE);
	if (tx) {
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode |= FUNCMODE_TMODE;
	} else {
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode |= FUNCMODE_RMODE;
	}
	funcmode |= FUNCMODE_CH0_ENABLE;

	writel(funcmode, i2s_private->regs + I2S_FUNCMODE);

	pr_debug("%s: %s %s channels=%d rate=%d fm=%x\n", __func__,
	       substream->pcm->name, dai->name, channels, rate, funcmode);
	if (channels == MONO_SOURCE) {
		iiscnf_out |= IISCNFOUT_TX_VOICE_EN_MONO;
		iiscnf_in |= CNFIN_RX_CH_SEL_LEFT;
		iiscnf_in |= CNFIN_RVOICEEN_MONO;
	} else {
		iiscnf_out &= ~IISCNFOUT_TX_VOICE_EN_MONO;
		iiscnf_in &= ~CNFIN_RX_CH_SEL_LEFT;
		iiscnf_in &= ~CNFIN_RVOICEEN_MONO;
	}

	if (tx)
		writel(iiscnf_out, i2s_private->regs + I2S_IISCNF_OUT);
	else
		writel(iiscnf_in, i2s_private->regs + I2S_IISCNF_IN);

	th1520_i2s_set_div_sclk(i2s_private, rate, DIV_DEFAULT);

	/* Turn on the I2S enable switch ahead of time, 
	 * and start the MCLK before the PA is turned on
	 * to solve the pop noise caused by the sudden change in I2S startup.
	 */
	i2s_en |= IISEN_I2SEN;
	writel(i2s_en, i2s_private->regs + I2S_IISEN);

	return 0;
}

static int th1520_hdmi_dai_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	u32 val;
	u32 len = 0;
	u32 funcmode;
	u32 iiscnf_out;
	u32 i2s_en;
	u32 rate = params_rate(params);
	u32 channels = params_channels(params);
	struct th1520_i2s_priv *i2s_private = snd_soc_dai_get_drvdata(dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;

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
			pr_err("Unknown data format: %d\n", 
			       params_format(params));
			return -EINVAL;
	}

	val |= FSSTA_SCLK_SEL_64;

	i2s_en &= ~IISEN_I2SEN;
	writel(i2s_en, i2s_private->regs + I2S_IISEN);

	regmap_update_bits(i2s_private->regmap, I2S_FSSTA,
			   FSSTA_DATAWTH_Msk | FSSTA_SCLK_SEL_Msk, val);
	funcmode = readl(i2s_private->regs + I2S_FUNCMODE);
	if (tx) {
		funcmode |= FUNCMODE_TMODE_WEN;
		funcmode &= ~FUNCMODE_TMODE;
		funcmode |= FUNCMODE_TMODE;
	} else {
		funcmode |= FUNCMODE_RMODE_WEN;
		funcmode &= ~FUNCMODE_RMODE;
		funcmode |= FUNCMODE_RMODE;
	}

	writel(funcmode, i2s_private->regs + I2S_FUNCMODE);

	iiscnf_out = readl(i2s_private->regs + I2S_IISCNF_OUT);
	if (channels == MONO_SOURCE)
		iiscnf_out |= IISCNFOUT_TX_VOICE_EN_MONO;
	else
		iiscnf_out &= ~IISCNFOUT_TX_VOICE_EN_MONO;

	writel(iiscnf_out, i2s_private->regs + I2S_IISCNF_OUT);

	th1520_i2s_set_div_sclk(i2s_private, rate, DIV_DEFAULT);

	return 0;
}

static int th1520_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct th1520_i2s_priv *i2s = snd_soc_dai_get_drvdata(dai);
	pr_debug("%s: %p %p\n", __func__, i2s->base, i2s->regs);

	if(i2s)
		snd_soc_dai_init_dma_data(dai, &i2s->dma_params_tx,
					  &i2s->dma_params_rx);

	return 0;
}

static const struct snd_soc_dai_ops th1520_i2s_dai_ops = {
	.probe		= th1520_i2s_dai_probe,
	.startup	= th1520_i2s_dai_startup,
	.shutdown	= th1520_i2s_dai_shutdown,
	.trigger	= th1520_i2s_dai_trigger,
	.set_fmt	= th1520_i2s_set_fmt_dai,
	.hw_params	= th1520_i2s_dai_hw_params,
};

static const struct snd_soc_dai_ops th1520_hdmi_dai_ops = {
	.probe		= th1520_i2s_dai_probe,
	.startup        = th1520_i2s_dai_startup,
	.shutdown       = th1520_i2s_dai_shutdown,
	.trigger        = th1520_i2s_dai_trigger,
	.set_fmt        = th1520_i2s_set_fmt_dai,
	.hw_params      = th1520_hdmi_dai_hw_params,
};

static struct snd_soc_dai_driver th1520_i2s_soc_dai[] = {
	{
		.name			= "th1520-i2s-dai",
		.playback = {
			.rates		= TH1520_RATES,
			.formats	= TH1520_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.capture = {
			.rates		= TH1520_RATES,
			.formats	= TH1520_FMTS,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops 			= &th1520_i2s_dai_ops,
		.symmetric_rate 	= 1,
	},
	{
		.name			= "th1520-hdmi-dai",
		.playback = {
			.rates		= TH1520_RATES,
			.formats	= SNDRV_PCM_FMTBIT_S24_LE |
					  SNDRV_PCM_FMTBIT_S16_LE,
			.channels_min	= 1,
			.channels_max	= 2,
		},
		.ops 			= &th1520_hdmi_dai_ops,
	},
};


static const struct snd_soc_component_driver th1520_i2s_soc_component = {
	.name		= "th1520_i2s",
};

static const struct regmap_config th1520_i2s_regmap_config = {
        .reg_bits	= 32,
        .reg_stride	= 4,
        .val_bits	= 32,
        .max_register	= I2S_DR4,
        .writeable_reg	= th1520_i2s_wr_reg,
        .readable_reg	= th1520_i2s_rd_reg,
        .cache_type	= REGCACHE_NONE,
};

static int th1520_i2s_runtime_suspend(struct device *dev)
{
	struct th1520_i2s_priv *i2s_priv = dev_get_drvdata(dev);
	pr_debug("%s: %s\n", __func__, i2s_priv->name);

	regcache_cache_only(i2s_priv->regmap, true);
	clk_disable_unprepare(i2s_priv->clk);

	return 0;
}

static int th1520_i2s_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct th1520_i2s_priv *i2s_priv = dev_get_drvdata(dev);
	pr_debug("%s: %s\n", __func__, i2s_priv->name);

	ret = clk_prepare_enable(i2s_priv->clk);
	if (ret) {
		dev_err(i2s_priv->dev, "clock enable failed %d\n", ret);
		return ret;
	}

	regcache_cache_only(i2s_priv->regmap, false);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int th1520_i2s_suspend(struct device *dev)
{
    struct th1520_i2s_priv *priv = dev_get_drvdata(dev);

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

	if (strcmp(priv->name, AP_I2S)) {
		regmap_read(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG,
			    &priv->cpr_peri_div_sel);
		regmap_read(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG,
			    &priv->cpr_peri_ctrl);
		regmap_read(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG,
			    &priv->cpr_peri_clk_sel);
	}
	reset_control_assert(priv->rst);

	pm_runtime_put_sync(dev);

	return 0;
}

static int th1520_i2s_resume(struct device *dev)
{
	struct th1520_i2s_priv *priv = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);

	reset_control_deassert(priv->rst);
	if (strcmp(priv->name, AP_I2S)) {
		regmap_write(priv->audio_cpr_regmap, CPR_PERI_CTRL_REG,
			     priv->cpr_peri_ctrl);
		regmap_write(priv->audio_cpr_regmap, CPR_PERI_CLK_SEL_REG,
			     priv->cpr_peri_clk_sel);
		regmap_write(priv->audio_cpr_regmap, CPR_PERI_DIV_SEL_REG,
			     priv->cpr_peri_div_sel);
	}

	regmap_write(priv->regmap, I2S_IISEN, 0);
	regmap_write(priv->regmap, I2S_FSSTA, priv->suspend_fssta);
	regmap_write(priv->regmap, I2S_FUNCMODE,
		     priv->suspend_funcmode |
		     	FUNCMODE_TMODE_WEN |
		     	FUNCMODE_RMODE_WEN);
	regmap_write(priv->regmap, I2S_IISCNF_IN, priv->suspend_iiscnf_in);
	regmap_write(priv->regmap, I2S_IISCNF_OUT, priv->suspend_ii2cnf_out);
	regmap_write(priv->regmap, I2S_DIV0_LEVEL, priv->suspend_div0_level);
	regmap_write(priv->regmap, I2S_DIV3_LEVEL, priv->suspend_div3_level);

	pm_runtime_put_sync(dev);

    return 0;
}
#endif

static const struct of_device_id th1520_i2s_of_match[] = {
	{ .compatible = "xuantie,th1520-i2s"},
	{},
};
MODULE_DEVICE_TABLE(of, th1520_i2s_of_match);

static ssize_t th1520_i2s_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
        return 0;
}

static ssize_t th1520_i2s_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        u32 value, i;
        struct th1520_i2s_priv *priv = dev_get_drvdata(dev);

        for (i = I2S_IISEN; i <= I2S_DR4; i+=0x4) {
			value = readl(priv->regs + i);
			printk("i2s reg[0x%x]=0x%x\n", i, value);
        }

        for (i = 0; i <= 0xfc ; i+=0x4) {
			regmap_read(priv->audio_cpr_regmap,  i, &value);
			printk("cpr reg[0x%x]=0x%x\n", i, value);
        }

        return 0;
}

static DEVICE_ATTR(registers, 0644, th1520_i2s_show, th1520_i2s_store);

static struct attribute *th1520_i2s_debug_attrs[] = {
        &dev_attr_registers.attr,
        NULL,
};

static struct attribute_group th1520_i2s_debug_attr_group = {
        .name   = "th1520_i2s_debug",
        .attrs  = th1520_i2s_debug_attrs,
};


static int th1520_audio_i2s_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int irq;
	const char *sprop;
	const uint32_t *iprop;
	struct resource *res;
	struct th1520_i2s_priv *i2s_priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct reset_control *resets;

	pr_info("%s 1\n", __func__);
	i2s_priv = devm_kzalloc(&pdev->dev, sizeof(*i2s_priv), GFP_KERNEL);

	if (!i2s_priv)
		return -ENOMEM;

	pr_info("%s 2\n", __func__);
	i2s_priv->dev = dev;

	if (strstr(pdev->name, AP_I2S)) {
		strcpy(i2s_priv->name, AP_I2S);
	}
	else if (strstr(pdev->name, AUDIO_I2S0)) {
		strcpy(i2s_priv->name, AUDIO_I2S0);
	}
	else if (strstr(pdev->name, AUDIO_I2S1)) {
		strcpy(i2s_priv->name, AUDIO_I2S1);
	}
	else if (strstr(pdev->name, AUDIO_I2S2)) {
		strcpy(i2s_priv->name, AUDIO_I2S2);
	}
	else {
		pr_err("unsupport audio dev name: %s\n", pdev->name);
		return -EINVAL;
	}

	pr_info("%s 3\n", __func__);
	dev_set_drvdata(&pdev->dev, i2s_priv);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	i2s_priv->regs = devm_ioremap_resource(dev, res);

	if (IS_ERR(i2s_priv->regs))
		return PTR_ERR(i2s_priv->regs);

	pr_info("%s 4\n", __func__);
	i2s_priv->regmap = devm_regmap_init_mmio(&pdev->dev, i2s_priv->regs,
						 &th1520_i2s_regmap_config);
	if (IS_ERR(i2s_priv->regmap)) {
			dev_err(&pdev->dev,
				"Failed to initialise managed register map\n");
			return PTR_ERR(i2s_priv->regmap);
	}

	pr_info("%s 5\n", __func__);
	if (strcmp(i2s_priv->name, AP_I2S)) {
		i2s_priv->audio_cpr_regmap =
			syscon_regmap_lookup_by_phandle(np, "audio-cpr-regmap");
		if (IS_ERR(i2s_priv->audio_cpr_regmap)) {
			dev_err(&pdev->dev,
				"cannot find regmap for audio cpr register\n");
		} else
			th1520_audio_cpr_set(i2s_priv, CPR_PERI_DIV_SEL_REG,
					     CPR_AUDIO_DIV1_SEL_MSK,
					     CPR_AUDIO_DIV1_SEL(5));

		// enable i2s sync
		th1520_audio_cpr_set(i2s_priv, CPR_PERI_CTRL_REG,
						CPR_I2S_SYNC_MSK, CPR_I2S_SYNC_EN);
	}

	pr_info("%s 6\n", __func__);
	resets = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (IS_ERR(resets)) {
		ret = PTR_ERR(resets);
		return ret;
	}
	pr_info("%s 7\n", __func__);
	i2s_priv->rst = resets;

	irq = platform_get_irq(pdev, 0);

	if (!res || (int)irq <= 0) {
		dev_err(&pdev->dev, "Not enough th1520 platform resources.\n");
		return -ENODEV;
	}

	pr_info("%s 8\n", __func__);
	i2s_priv->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(i2s_priv->clk))
                return PTR_ERR(i2s_priv->clk);

	pr_info("%s 9\n", __func__);
	reset_control_deassert(i2s_priv->rst);

	pm_runtime_enable(&pdev->dev);
	// clk gate is enabled by hardware as default register value
	pm_runtime_resume_and_get(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);

	i2s_priv->dma_params_tx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s_priv->dma_params_rx.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	i2s_priv->dma_params_tx.maxburst = I2S_DMA_MAXBURST;
	i2s_priv->dma_params_rx.maxburst = I2S_DMA_MAXBURST;

	i2s_priv->dma_params_tx.addr = res->start + I2S_DR;
	i2s_priv->dma_params_rx.addr = res->start + I2S_DR;

	th1520_pcm_probe(pdev, i2s_priv, TH1520_I2S_DMABUF_SIZE);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &th1520_i2s_soc_component,
					      th1520_i2s_soc_dai,
					      ARRAY_SIZE(th1520_i2s_soc_dai));
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot snd component register\n");
		goto err_pm_disable;
	}

	pr_info("%s 10\n", __func__);
	ret = sysfs_create_group(&dev->kobj, &th1520_i2s_debug_attr_group);
	if (ret) {
			pr_err("failed to create attr group\n");
	}

	pr_info("%s 11\n", __func__);
	return ret;

err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int th1520_i2s_remove(struct platform_device *pdev)
{
        struct th1520_i2s_priv *i2s_priv = dev_get_drvdata(&pdev->dev);

        pm_runtime_disable(&pdev->dev);
        if (!pm_runtime_status_suspended(&pdev->dev))
                th1520_i2s_runtime_suspend(&pdev->dev);

        clk_disable_unprepare(i2s_priv->clk);

        return 0;
}

static const struct dev_pm_ops th1520_i2s_pm_ops = {
        SET_RUNTIME_PM_OPS(th1520_i2s_runtime_suspend,
        		   th1520_i2s_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(th1520_i2s_suspend, th1520_i2s_resume)
};

static struct platform_driver th1520_i2s_driver = {
	.driver 	= {
		.name		= "th1520-pcm-audio",
		.pm		= &th1520_i2s_pm_ops,
		.of_match_table = th1520_i2s_of_match,
	},
	.probe		= th1520_audio_i2s_probe,
	.remove		= th1520_i2s_remove,
};

module_platform_driver(th1520_i2s_driver);

MODULE_AUTHOR("shuofeng.ren <shuofeng.rsf@linux.alibaba.com>");
MODULE_DESCRIPTION("Xuantie TH1520 audio driver");
MODULE_LICENSE("GPL v2");

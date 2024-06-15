/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XuanTie TH1520 audio driver for ASoC PCM DAI for HDMI
 *
 * Copyright (C) 2024 Alibaba Group Holding Limited.
 *
 * Author: Yan Dong <nanli.yd@alibaba-inc.com>
 */
 
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/soc.h>

static const struct snd_soc_dapm_widget th1520_hdmi_pcm_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route th1520_hdmi_pcm_routes[] = {
	{ "Capture", NULL, "RX" },
	{ "TX", NULL, "Playback" },
};

static struct snd_soc_dai_driver th1520_hdmi_pcm_dai[] = {
	{
		.name		= "hdmi-pcm",
		.playback	= {
			.stream_name	= "Playback",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.formats	= SNDRV_PCM_FMTBIT_S32_LE |
					  SNDRV_PCM_FMTBIT_S24_LE |
					  SNDRV_PCM_FMTBIT_S20_LE |
					  SNDRV_PCM_FMTBIT_S16_LE |
					  SNDRV_PCM_FMTBIT_S8,
		},
		.capture	= {
			.stream_name	= "Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= SNDRV_PCM_RATE_8000_192000,
			.formats	= SNDRV_PCM_FMTBIT_S32_LE |
					  SNDRV_PCM_FMTBIT_S24_LE |
					  SNDRV_PCM_FMTBIT_S20_LE |
					  SNDRV_PCM_FMTBIT_S16_LE |
					  SNDRV_PCM_FMTBIT_S8,
		},
	},
};

static const struct snd_soc_component_driver soc_component_dev_th1520_hdmi_pcm = {
	.dapm_widgets		= th1520_hdmi_pcm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(th1520_hdmi_pcm_widgets),
	.dapm_routes		= th1520_hdmi_pcm_routes,
	.num_dapm_routes	= ARRAY_SIZE(th1520_hdmi_pcm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int th1520_hdmi_pcm_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
					&soc_component_dev_th1520_hdmi_pcm,
					th1520_hdmi_pcm_dai,
					ARRAY_SIZE(th1520_hdmi_pcm_dai));
}

static int th1520_hdmi_pcm_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct platform_device_id th1520_hdmi_pcm_driver_ids[] = {
	{
		.name		= "th1520-hdmi-pcm",
	},
	{},
};
MODULE_DEVICE_TABLE(platform, th1520_hdmi_pcm_driver_ids);

#if defined(CONFIG_OF)
static const struct of_device_id th1520_hdmi_pcm_codec_of_match[] = {
	{
		.compatible	= "xuantie,th1520-hdmi-pcm",
	},
	{},
};
MODULE_DEVICE_TABLE(of, th1520_hdmi_pcm_codec_of_match);
#endif

static struct platform_driver th1520_hdmi_pcm_driver = {
	.driver		= {
		.name		= "th1520-hdmi-pcm",
		.of_match_table	= of_match_ptr(th1520_hdmi_pcm_codec_of_match),
	},
	.probe		= th1520_hdmi_pcm_probe,
	.remove		= th1520_hdmi_pcm_remove,
	.id_table	= th1520_hdmi_pcm_driver_ids,
};

module_platform_driver(th1520_hdmi_pcm_driver);

MODULE_AUTHOR("Yan Dong <nanli.yd@alibaba-inc.com>");
MODULE_DESCRIPTION("ASoC PCM DAI driver for HDMI");
MODULE_LICENSE("GPL v2");

/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XuanTie TH1520 ALSA Soc Audio Layer DMA init function.
 *
 * Copyright (C) 2024 Alibaba Group Holding Limited.
 *
 * Author: Shuofeng Ren <shuofeng.rsf@linux.alibaba.com>
 *
 */
 
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <sound/dmaengine_pcm.h>

#include "th1520-pcm.h"

static bool filter(struct dma_chan *chan, void *param)
{
	chan->private = param;

	return true;
}

static const struct snd_dmaengine_pcm_config th1520_dmaengine_pcm_config = {
	.prepare_slave_config	= snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn	= filter,
};

int th1520_pcm_dma_init(struct platform_device *pdev, size_t size)
{
	struct snd_dmaengine_pcm_config *config;

	config = devm_kzalloc(&pdev->dev, 
			      sizeof(struct snd_dmaengine_pcm_config),
			      GFP_KERNEL);
	if (!config)
		return -ENOMEM;
	*config = th1520_dmaengine_pcm_config;

	return devm_snd_dmaengine_pcm_register(&pdev->dev, config,
					       SND_DMAENGINE_PCM_FLAG_COMPAT);
}

MODULE_AUTHOR("shuofeng.ren <shuofeng.rsf@linux.alibaba.com>");
MODULE_DESCRIPTION("Xuantie TH1520 audio driver");
MODULE_LICENSE("GPL v2");
/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XuanTie TH1520 ALSA Soc Audio Layer
 *
 * Copyright (C) 2024 Alibaba Group Holding Limited.
 *
 * Author: Shuofeng Ren <shuofeng.rsf@linux.alibaba.com>
 *
 */
 
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <sound/dmaengine_pcm.h>

#include "th1520-i2s.h"
#include "th1520-pcm.h"

int th1520_audio_cpr_set(struct th1520_i2s_priv *chip, unsigned int cpr_off,
			 unsigned int mask, unsigned int val)
{
       if(!chip->audio_cpr_regmap)
               return 0;

       return regmap_update_bits(chip->audio_cpr_regmap, cpr_off, mask, val);
}

bool th1520_i2s_wr_reg(struct device *dev, unsigned int reg)
{
	return true;
}

bool th1520_i2s_rd_reg(struct device *dev, unsigned int reg)
{
	return true;
}

int th1520_pcm_probe(struct platform_device *pdev, struct th1520_i2s_priv *i2s,
		     size_t size)
{
	int ret = th1520_pcm_dma_init(pdev, size);

	if (ret)
		pr_err("th1520_pcm_dma_init error\n");

	return 0;
}

MODULE_AUTHOR("shuofeng.ren <shuofeng.rsf@linux.alibaba.com>");
MODULE_DESCRIPTION("Xuantie TH1520 audio driver");
MODULE_LICENSE("GPL v2");

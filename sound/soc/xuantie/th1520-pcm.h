/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XuanTie TH1520 Audio PCM support
 *
 * Copyright (C) 2024 Alibaba Group Holding Limited.
 *
 * Author: Shuofeng Ren <shuofeng.rsf@linux.alibaba.com>
 *
 */

#ifndef _TH1520_PCM_H
#define _TH1520_PCM_H

#include <linux/device.h>

int th1520_pcm_dma_init(struct platform_device *pdev, size_t size);

#endif /* _TH1520_PCM_H */

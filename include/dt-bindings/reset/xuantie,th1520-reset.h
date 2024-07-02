/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2021 Alibaba, Inc.
 *
 * Author: zenglinghui  <zenglinghui.zlh@linux.alibaba.com>
 */

#ifndef DT_BINDING_RESET_TH1520_H
#define DT_BINDING_RESET_TH1520_H

#define TH1520_RESET_WDT0 0
#define TH1520_RESET_WDT1 1
#define TH1520_RESET_HDMI_I2S 2
#define TH1520_RESET_NPU 3

// vpsys reset
#define TH1520_RESET_FCE 20

// audiosys reset
#define TH1520_RESET_AUD_I2S0 30
#define TH1520_RESET_AUD_I2S1 31
#define TH1520_RESET_AUD_I2S2 32
#define TH1520_RESET_AUD_I2S8CH 33
#define TH1520_RESET_AUD_TDM 34
#define TH1520_RESET_AUD_SPDIF0 35
#define TH1520_RESET_AUD_SPDIF1 36

#endif

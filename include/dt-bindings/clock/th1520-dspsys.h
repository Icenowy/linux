/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Alibaba Group Holding Limited.
 */

#ifndef _TH1520_DSPSYS_H
#define _TH1520_DSPSYS_H
// gate
#define CLKGEN_DSP0_PCLK			0
#define CLKGEN_DSP0_CCLK                        1
#define CLKGEN_DSP1_PCLK                        2
#define CLKGEN_DSP1_CCLK                        3
#define CLKGEN_X2X_X4_DSPSLV_DSP0_ACLK_M        4
#define CLKGEN_X2X_X4_DSPSLV_DSP1_ACLK_M        5
#define CLKGEN_AXI4_DSPSYS_SLV_ACLK             6
#define CLKGEN_AXI4_DSPSYS_ACLK                 7
#define CLKGEN_IOPMP_DSP0_PCLK                  8
#define CLKGEN_IOPMP_DSP1_PCLK                  9
#define CLKGEN_AXI4_DSPSYS_SLV_PCLK             10
#define CLKGEN_AXI4_DSPSYS_PCLK                 11
#define CLKGEN_X2X_DSP0_ACLK_S			12
#define CLKGEN_X2X_DSP2_ACLK_S			13
// MUX
#define DSPSYS_DSP0_CLK_SWITCH          14
#define DSPSYS_DSP1_CLK_SWITCH          15
// DIV
#define DSPSYS_DSP_CLK              16
#define DSPSYS_DSP0_CLK_CDE         17
#define DSPSYS_DSP1_CLK_CDE         18

#define TH1520_CLKGEN_DSPSYS_CLK_END		19

#endif

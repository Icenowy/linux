#ifndef _TH1520_TDM_H
#define _TH1520_TDM_H

#include <linux/io.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/scatterlist.h>
#include <linux/sh_dma.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/pcm.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/dmaengine_pcm.h>

#include "th1520-pcm.h"
#include <linux/spinlock.h>

#define TDM_TDMEN   0x00
#define TDM_TDMCTL  0x04
#define TDM_CHOFFSET1   0x08
#define TDM_CHOFFSET2   0x0c
#define TDM_CHOFFSET3   0x10
#define TDM_CHOFFSET4   0x14
#define TDM_FIFOTL1	0x18
#define TDM_FIFOTL2	0x1C
#define TDM_FIFOTL3	0x20
#define TDM_FIFOTL4	0x24
#define TDM_SR	0x28
#define TDM_IMR	0x2C
#define TDM_ISR	0x30
#define TDM_RISR	0x34
#define TDM_ICR	0x38
#define TDM_DMACTL	0x3C
#define TDM_DMADL	0x40
#define TDM_LDR1	0x44
#define TDM_RDR1	0x48
#define TDM_LDR2	0x4C
#define TDM_RDR2	0x50
#define TDM_LDR3	0x54
#define TDM_RDR3	0x58
#define TDM_LDR4	0x5C
#define TDM_RDR4	0x60
#define TDM_DIV0_LEVEL	0x64

#define TDM_MODE_MASTER 0x1
#define TDM_MODE_SLAVE 0x0
#define TDMCTL_MODE_POS                             (0U) 
#define TDMCTL_MODE_MSK                         (0x1U << TDMCTL_MODE_POS)
#define TDMCTL_MODE_SEL(X)                  (X << TDMCTL_MODE_POS)
#define TDMCTL_DATAWTH_POS                  (4U)
#define TDMCTL_DATAWTH_MSK              (0x3U << TDMCTL_DATAWTH_POS)
#define TDMCTL_DATAWTH_SEL(X)                (X << TDMCTL_DATAWTH_POS)
#define TDMCTL_CHNUM_POS                        (8U)
#define TDMCTL_CHNUM_MSK              (0x3U << TDMCTL_CHNUM_POS)
#define TDMCTL_CHNUM_SEL(X)                (X << TDMCTL_CHNUM_POS)
#define TDMCTL_CHNUM_2                      0x0
#define TDMCTL_CHNUM_4                      0x1
#define TDMCTL_CHNUM_6                      0x2
#define TDMCTL_CHNUM_8                      0x3
#define TDMCTL_SPEDGE_POS                        (13U)
#define TDMCTL_SPEDGE_MSK              (0x1U << TDMCTL_SPEDGE_POS)
#define TDMCTL_SPEDGE_SEL(X)                (X << TDMCTL_SPEDGE_POS)

#define TDMCTL_DATAWTH_16BIT_PACKED 0x0
#define TDMCTL_DATAWTH_16BIT 0x1
#define TDMCTL_DATAWTH_24BIT    0x2
#define TDMCTL_DATAWTH_32BIT 0x3

#define TDMCTL_DIV0_MASK 0xFFF

#define DMACTL_DMAEN_POS                          (0U)
#define DMACTL_DMAEN_MSK                          (0x1U << DMACTL_DMAEN_POS)
#define DMACTL_DMAEN_SEL(X)                            (X << DMACTL_DMAEN_POS)

#define DMACTL_DMADL_POS                          (0U)
#define DMACTL_DMADL_MSK                          (0x3U << DMACTL_DMADL_POS)
#define DMACTL_DMADL_SEL(X)                            (X << DMACTL_DMADL_POS)

#define TDMCTL_TDMEN_POS                          (0U)
#define TDMCTL_TDMEN_MSK                          (0x1U << TDMCTL_TDMEN_POS)
#define TDMCTL_TDMEN_SEL(X)                            (X << TDMCTL_TDMEN_POS)

#define TH1520_TDM_DMABUF_SIZE     (64 * 1024)
#define TDM_STATE_IDLE				0
#define TDM_STATE_RUNNING	1

struct th1520_tdm_priv {
    void __iomem *regs;
    struct regmap *regmap;
    struct regmap *audio_cpr_regmap;
    struct reset_control *rst;
    struct clk *clk;
    struct snd_dmaengine_dai_dma_data dma_params_rx;
    unsigned int cfg_off;
    struct device *dev; 
    char name[16];
    char mode;
    char slots;
    char slot_num;
    unsigned int irq;
    u32 suspend_tdmctl;
    u32 suspend_choffset1;
    u32 suspend_choffset2;
    u32 suspend_choffset3;
    u32 suspend_choffset4;
    u32 suspend_fifotl1;
    u32 suspend_fifotl2;
    u32 suspend_fifotl3;
    u32 suspend_fifotl4;
    u32 suspend_imr;
    u32 suspend_dmadl;
    u32 suspend_div0level;
	u32 cpr_peri_div_sel;
	u32 cpr_peri_clk_sel;
    u32 state;
};


#endif

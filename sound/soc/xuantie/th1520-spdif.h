#ifndef _TH1520_SPDIF_H_
#define _TH1520_SPDIF_H_

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

#define SPDIF_EN   0x00
#define SPDIF_TX_EN  0x04
#define SPDIF_TX_CTL   0x08
#define SPDIF_TX_CS_A   0x0c
#define SPDIF_TX_USER_A0   0x10
#define SPDIF_TX_USER_A1   0x14
#define SPDIF_TX_USER_A2	0x18
#define SPDIF_TX_USER_A3	0x1C
#define SPDIF_TX_USER_A4	0x20
#define SPDIF_TX_USER_A5	0x24
#define SPDIF_TX_FIFO_DR	0x28
#define SPDIF_TX_FIFO_TH	0x2C
#define SPDIF_TX_FIFO_DL	0x30
#define SPDIF_TX_DMA_EN	0x34
#define SPDIF_TX_DMA_TH	0x38
#define SPDIF_SR	0x3C
#define SPDIF_IMR	0x40
#define SPDIF_ISR	0x44
#define SPDIF_RISR	0x48
#define SPDIF_ICR	0x4C
#define SPDIF_RX_EN	0x50
#define SPDIF_RX_CTL	0x54
#define SPDIF_RX_CS_A	0x58
#define SPDIF_RX_USER_A0	0x5C
#define SPDIF_RX_USER_A1	0x60
#define SPDIF_RX_USER_A2	0x64
#define SPDIF_RX_USER_A3    0x68
#define SPDIF_RX_USER_A4    0x6C
#define SPDIF_RX_USER_A5    0x70
#define SPDIF_RX_FIFO_DR    0x74
#define SPDIF_RX_FIFO_TH    0x78
#define SPDIF_RX_FIFO_DL    0x7C
#define SPDIF_RX_DMA_EN     0x80
#define SPDIF_RX_DMA_TH     0x84
#define SPDIF_TX_CS_B   0x88
#define SPDIF_TX_USER_B0    0x8C
#define SPDIF_TX_USER_B1    0x90
#define SPDIF_TX_USER_B2    0x94
#define SPDIF_TX_USER_B3    0x98
#define SPDIF_TX_USER_B4    0x9C
#define SPDIF_TX_USER_B5    0xa0
#define SPDIF_RX_CS_B   0xa4
#define SPDIF_RX_USER_B0    0xa8
#define SPDIF_RX_USER_B1    0xaC
#define SPDIF_RX_USER_B2    0xb0
#define SPDIF_RX_USER_B3    0xb4
#define SPDIF_RX_USER_B4    0xb8
#define SPDIF_RX_USER_B5    0xbC

/* SPDIF_EN */
#define SPDIF_SPDIFEN_POS                             (0U) 
#define SPDIF_SPDIFEN_MSK                         (0x1U << SPDIF_SPDIFEN_POS)
#define SPDIF_SPDIFEN_SEL(X)                  (X << SPDIF_SPDIFEN_POS)

/* SPDIF_TX_EN */
#define SPDIF_TXEN_POS                             (0U) 
#define SPDIF_TXEN_MSK                         (0x1U << SPDIF_TXEN_POS)
#define SPDIF_TXEN_SEL(X)                  (X << SPDIF_TXEN_POS)

/* SPDIF_TX_CTL */
#define SPDIF_TX_DIV_POS                             (9U) 
#define SPDIF_TX_DIV_MSK                         (0x7FU << SPDIF_TX_DIV_POS)
#define SPDIF_TX_DIV_SEL(X)                  (X << SPDIF_TX_DIV_POS)
#define SPDIF_TX_DIV_BYPASS_POS                             (8U) 
#define SPDIF_TX_DIV_BYPASS_MSK                         (0x1U << SPDIF_TX_DIV_BYPASS_POS)
#define SPDIF_TX_DIV_BYPASS_SEL(X)                  (X << SPDIF_TX_DIV_BYPASS_POS)
#define SPDIF_TX_CH_SEL_POS                             (4U) 
#define SPDIF_TX_CH_SEL_MSK                         (0x1U << SPDIF_TX_CH_SEL_POS)
#define SPDIF_TX_CH_SEL_SEL(X)                  (X << SPDIF_TX_CH_SEL_POS)
#define SPDIF_TX_DATAMODE_POS                             (0U) 
#define SPDIF_TX_DATAMODE_MSK                         (0x3U << SPDIF_TX_DATAMODE_POS)
#define SPDIF_TX_DATAMODE_SEL(X)                  (X << SPDIF_TX_DATAMODE_POS)

#define SPDIF_TX_DATAMODE_2CH       0x0
#define SPDIF_TX_DATAMODE_1CH       0x1
#define SPDIF_TX_DATAMODE_16BIT_PACKED 0x3
#define SPDIF_TX_DATAMODE_16BIT 0x2
#define SPDIF_TX_DATAMODE_20BIT    0x1
#define SPDIF_TX_DATAMODE_24BIT 0x0

/* SPDIF_TX_CS_A */
#define SPDIF_TX_T_FS_SEL_POS           (24U)
#define SPDIF_TX_T_FS_SEL_MSK                         (0xFU << SPDIF_TX_T_FS_SEL_POS)
#define SPDIF_TX_T_FS_SEL_SEL(X)                  (X << SPDIF_TX_T_FS_SEL_POS)
#define SPDIF_TX_T_CH_NUM_POS           (20U)
#define SPDIF_TX_T_CH_NUM_MSK                         (0xFU << SPDIF_TX_T_CH_NUM_POS)
#define SPDIF_TX_T_CH_NUM_SEL(X)                  (X << SPDIF_TX_T_CH_NUM_POS)

/* SPDIF_RX_DMA_TH */
#define SPDIF_RDMA_TH_POS           (0U)
#define SPDIF_RDMA_TH_MSK                         (0xFU << SPDIF_RDMA_TH_POS)
#define SPDIF_RDMA_TH_SEL(X)                  (X << SPDIF_RDMA_TH_POS)

/* SPDIF_TX_CS_B */
#define SPDIF_TX_T_B_FS_SEL_POS           (24U)
#define SPDIF_TX_T_B_FS_SEL_MSK                         (0xFU << SPDIF_TX_T_B_FS_SEL_POS)
#define SPDIF_TX_T_B_FS_SEL_SEL(X)                  (X << SPDIF_TX_T_B_FS_SEL_POS)
#define SPDIF_TX_T_B_CH_NUM_POS           (20U)
#define SPDIF_TX_T_B_CH_NUM_MSK                         (0xFU << SPDIF_TX_T_B_CH_NUM_POS)
#define SPDIF_TX_T_B_CH_NUM_SEL(X)                  (X << SPDIF_TX_T_B_CH_NUM_POS)

/* SPDIF_TX_DMA_EN */
#define SPDIF_TDMA_EN_POS                             (0U) 
#define SPDIF_TDMA_EN_MSK                         (0x1U << SPDIF_TDMA_EN_POS)
#define SPDIF_TDMA_EN_SEL(X)                  (X << SPDIF_TDMA_EN_POS)

/* SPDIF_RX_EN */
#define SPDIF_RXEN_POS                             (0U) 
#define SPDIF_RXEN_MSK                         (0x1U << SPDIF_RXEN_POS)
#define SPDIF_RXEN_SEL(X)                  (X << SPDIF_RXEN_POS)

/* SPDIF_RX_CTL */
#define SPDIF_RX_DIV_POS                             (13U) 
#define SPDIF_RX_DIV_MSK                         (0x7FU << SPDIF_RX_DIV_POS)
#define SPDIF_RX_DIV_SEL(X)                  (X << SPDIF_RX_DIV_POS)
#define SPDIF_RX_DIV_BYPASS_POS                             (12U) 
#define SPDIF_RX_DIV_BYPASS_MSK                         (0x1U << SPDIF_RX_DIV_BYPASS_POS)
#define SPDIF_RX_DIV_BYPASS_SEL(X)                  (X << SPDIF_RX_DIV_BYPASS_POS)
#define SPDIF_RX_VALID_EN_POS                             (8U) 
#define SPDIF_RX_VALID_EN_MSK                         (0x1U << SPDIF_RX_VALID_EN_POS)
#define SPDIF_RX_VALID_EN_SEL(X)                  (X << SPDIF_RX_VALID_EN_POS)
#define SPDIF_RX_PARITY_EN_POS                             (4U) 
#define SPDIF_RX_PARITY_EN_MSK                         (0x1U << SPDIF_RX_PARITY_EN_POS)
#define SPDIF_RX_PARITY_EN_SEL(X)                  (X << SPDIF_RX_PARITY_EN_POS)
#define SPDIF_RX_DATAMODE_POS                             (0U) 
#define SPDIF_RX_DATAMODE_MSK                         (0x3U << SPDIF_RX_DATAMODE_POS)
#define SPDIF_RX_DATAMODE_SEL(X)                  (X << SPDIF_RX_DATAMODE_POS)

/* SPDIF_RX_DMA_EN */
#define SPDIF_RDMA_EN_POS                             (0U) 
#define SPDIF_RDMA_EN_MSK                         (0x1U << SPDIF_RDMA_EN_POS)
#define SPDIF_RDMA_EN_SEL(X)                  (X << SPDIF_RDMA_EN_POS)


#define CPR_PERI_DIV_SEL_REG  0x004 /*audio sys clock div Register*/
#define CPR_PERI_CLK_SEL_REG  0x008 /*audio sys clock selection Register*/
#define CPR_PERI_CTRL_REG  0x00C /*Peripheral control signal configuration register*/
#define CPR_IP_CG_REG   0x010 /* ip clock gate register */
#define CPR_IP_RST_REG  0x014

/* AUDIO SYS DIV SEL REG, offset: 0x4 */
#define CPR_AUDIO_DIV1_CG_POS                     (17U)
#define CPR_AUDIO_DIV1_CG_MSK                     (0x1U << CPR_AUDIO_DIV1_CG_POS)
#define CPR_AUDIO_DIV1_CG(X)                      (X << CPR_AUDIO_DIV1_CG_POS)
#define CPR_AUDIO_DIV1_SEL_POS                     (12U)
#define CPR_AUDIO_DIV1_SEL_MSK                     (0x1FU << CPR_AUDIO_DIV1_SEL_POS)
#define CPR_AUDIO_DIV1_SEL(X)                      (X << CPR_AUDIO_DIV1_SEL_POS)
#define CPR_AUDIO_DIV0_CG_POS                     (9U)
#define CPR_AUDIO_DIV0_CG_MSK                     (0x1U << CPR_AUDIO_DIV0_CG_POS)
#define CPR_AUDIO_DIV0_CG(X)                      (X << CPR_AUDIO_DIV0_CG_POS)
#define CPR_AUDIO_DIV0_SEL_POS                  (4U)
#define CPR_AUDIO_DIV0_SEL_MSK               (0x1FU << CPR_AUDIO_DIV0_SEL_POS)
#define CPR_AUDIO_DIV0_SEL(X)                      (X << CPR_AUDIO_DIV0_SEL_POS)
/* CPR_PERI_CLK_SEL_REG  0x008 */
#define CPR_SPDIF_SRC_SEL_POS                     (24U)
#define CPR_SPDIF_SRC_SEL_MSK                     (0x3U << CPR_SPDIF_SRC_SEL_POS)
#define CPR_SPDIF_SRC_SEL(X)                          (X << CPR_SPDIF_SRC_SEL_POS)
/* PERI_CTRL_REG, Offset: 0xC */
#define CPR_SPDIF_SYNC_POS                (14U)
#define CPR_SPDIF_SYNC_MSK                (0x1U << CPR_SPDIF_SYNC_POS)
#define CPR_SPDIF_SYNC_EN                 (CPR_SPDIF_SYNC_MSK)
/* CPR_IP_CG_REG   0x010 */
#define CPR_SPDIF0_CG_SEL_POS                      (23U)
#define CPR_SPDIF0_CG_SEL_MSK                       (0x1U << CPR_SPDIF0_CG_SEL_POS)
#define CPR_SPDIF0_CG_SEL(X)                            (X << CPR_SPDIF0_CG_SEL_POS)
#define CPR_SPDIF1_CG_SEL_POS                      (24U)
#define CPR_SPDIF1_CG_SEL_MSK                       (0x1U << CPR_SPDIF1_CG_SEL_POS)
#define CPR_SPDIF1_CG_SEL(X)                            (X << CPR_SPDIF1_CG_SEL_POS)
/* CPR_IP_RST_REG */
#define CPR_SPDIF0_SRST_N_SEL_POS                      (23U)
#define CPR_SPDIF0_SRST_N_SEL_MSK                       (0x1U << CPR_SPDIF0_SRST_N_SEL_POS)
#define CPR_SPDIF0_SRST_N_SEL(X)                            (X << CPR_SPDIF0_SRST_N_SEL_POS)
#define CPR_SPDIF1_SRST_N_SEL_POS                      (24U)
#define CPR_SPDIF1_SRST_N_SEL_MSK                       (0x1U << CPR_SPDIF1_SRST_N_SEL_POS)
#define CPR_SPDIF1_SRST_N_SEL(X)                            (X << CPR_SPDIF1_SRST_N_SEL_POS)

#define TH1520_SPDIF_DMABUF_SIZE     (64 * 1024)
#define SPDIF_STATE_IDLE				0
#define SPDIF_STATE_TX_RUNNING	    1
#define SPDIF_STATE_RX_RUNNING	    2

struct th1520_spdif_priv {
    void __iomem *regs;
    struct regmap *regmap;
    struct regmap *audio_cpr_regmap;
    struct reset_control *rst;
    struct clk *clk;
    struct snd_dmaengine_dai_dma_data dma_params_tx;
    struct snd_dmaengine_dai_dma_data dma_params_rx;
    struct device *dev; 
    char name[16];
    unsigned int irq;
    u32 suspend_tx_en;
    u32 suspend_tx_ctl;
    u32 suspend_tx_fifo_th;
    u32 suspend_tx_fifo_dl;
    u32 suspend_tx_dma_en;
    u32 suspend_tx_dma_th;
    u32 suspend_spdif_imr;
    u32 suspend_rx_en;
    u32 suspend_rx_ctl;
    u32 suspend_rx_fifo_th;
    u32 suspend_rx_fifo_dl;
    u32 suspend_rx_dma_en;
    u32 suspend_rx_dma_th;
    u32 cpr_peri_div_sel;
    u32 cpr_peri_ctrl;
    u32 cpr_peri_clk_sel;
    u32 state;
};


#endif

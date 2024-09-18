/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2021 VERISILICON
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2021 VERISILICON
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/
#include "hantrodec.h"
#include "dwl_defs.h"
#include "subsys.h"
#include "io_tools.h"
#include "osal.h"

#include "xtensa_api.h"
//#include "../src/libxmp/xmp-library.h" //For atomic operations
#include <xtensa/xtruntime.h> //interrupt for xtensa

#include <stdint.h>
#include <string.h>

#undef PDEBUG
#ifdef HANTRODEC_DEBUG
#  ifdef __KERNEL__
#    define PDEBUG(fmt, args...) printk( KERN_INFO "hantrodec: " fmt, ## args)
#  else
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, ...)
#endif
/*-----------------------------------------------------------------------------------------
************************CPU Xtensa OS FreeRTOS PORTING LAYER*******************************
------------------------------------------------------------------------------------------*/
static u32 g_vc8000_int_enable_mask = 0;
//VCMD for netint
#define SYS_REG_INT_TOP_BASE     (0x02800000)
//ENCODER
#if 0
#define CPU_INT_IRQ              8 /* All Encoder Modules' interrupt will be connected to CPU IRQ 8 */
#define SYS_INT_MASK             (0x220)
#define SYS_REG_INT_VAL          (SYS_REG_INT_TOP_BASE + 0x3c)
#define SYS_REG_INT_STAT         (SYS_REG_INT_TOP_BASE + 0x40)
#define SYS_REG_INT_EN           (SYS_REG_INT_TOP_BASE + 0x44)
#else
//DECODER
#define CPU_INT_IRQ              9
#define SYS_INT_MASK             (0x001)
#define SYS_REG_INT_VAL          (SYS_REG_INT_TOP_BASE + 0x48)
#define SYS_REG_INT_STAT         (SYS_REG_INT_TOP_BASE + 0x4c)
#define SYS_REG_INT_EN           (SYS_REG_INT_TOP_BASE + 0x50)
#endif

/* hantro G1 regs config including dec and pp */
#define HANTRO_DEC_ORG_REGS             60
#define HANTRO_PP_ORG_REGS              41

#define HANTRO_DEC_EXT_REGS             27
#define HANTRO_PP_EXT_REGS              9

//#define HANTRO_G1_DEC_TOTAL_REGS        (HANTRO_DEC_ORG_REGS + HANTRO_DEC_EXT_REGS)
#define HANTRO_PP_TOTAL_REGS            (HANTRO_PP_ORG_REGS + HANTRO_PP_EXT_REGS)
#define HANTRO_G1_DEC_REGS              155 /*G1 total regs*/

//#define HANTRO_DEC_ORG_FIRST_REG        0
//#define HANTRO_DEC_ORG_LAST_REG         59
//#define HANTRO_DEC_EXT_FIRST_REG        119
//#define HANTRO_DEC_EXT_LAST_REG         145

#define HANTRO_PP_ORG_FIRST_REG         60
#define HANTRO_PP_ORG_LAST_REG          100
#define HANTRO_PP_EXT_FIRST_REG         146
#define HANTRO_PP_EXT_LAST_REG          154

/* hantro G2 reg config */
#define HANTRO_G2_DEC_REGS              337 /*G2 total regs*/
#define HANTRO_G2_DEC_FIRST_REG         0
#define HANTRO_G2_DEC_LAST_REG          HANTRO_G2_DEC_REGS-1

/* hantro VC8000D reg config */
#define HANTRO_VC8000D_REGS             503 /*VC8000D total regs*/
#define HANTRO_VC8000D_FIRST_REG        0
#define HANTRO_VC8000D_LAST_REG         HANTRO_VC8000D_REGS-1

/* Logic module IRQs */
#define HXDEC_NO_IRQ                    -1

#define MAX(a, b)                       (((a) > (b)) ? (a) : (b))

#define DEC_IO_SIZE_MAX                 (MAX(MAX(HANTRO_G2_DEC_REGS, HANTRO_G1_DEC_REGS), HANTRO_VC8000D_REGS) * 4)

/* User should modify these configuration if do porting to own platform. */
/* Please guarantee the base_addr, io_size, dec_irq belong to same core. */

/* Defines use kernel clk cfg or not**/
//#define CLK_CFG
#if 0
#ifdef CLK_CFG
#define CLK_ID                          "hantrodec_clk"  /*this id should conform with platform define*/
#endif
#endif

/* Logic module base address */
#define SOCLE_LOGIC_0_BASE              (0x02310000 + 0x2000)
#define SOCLE_LOGIC_1_BASE              -1 //(0x02350000 + 0x2000)
#define SOCLE_LOGIC_2_BASE              -1 //(0x02390000 + 0x2000)
#define SOCLE_LOGIC_3_BASE              -1 //(0x023D0000 + 0x2000)

//#define VEXPRESS_LOGIC_0_BASE           0xFC010000
//#define VEXPRESS_LOGIC_1_BASE           0xFC020000

#define DEC_IO_SIZE_0                   DEC_IO_SIZE_MAX /* bytes */
#define DEC_IO_SIZE_1                   DEC_IO_SIZE_MAX /* bytes */
#define DEC_IO_SIZE_2                   DEC_IO_SIZE_MAX /* bytes */
#define DEC_IO_SIZE_3                   DEC_IO_SIZE_MAX /* bytes */

#define DEC_IRQ_0                       9 //3
#define DEC_IRQ_1                       HXDEC_NO_IRQ

#define IS_G1(hw_id)                    (((hw_id) == 0x6731)? 1 : 0)
#define IS_G2(hw_id)                    (((hw_id) == 0x6732)? 1 : 0)
#define IS_VC8000D(hw_id)               (((hw_id) == 0x8001)? 1 : 0)

static const int DecHwId[] = {
  0x6731, /* G1 */
  0x6732, /* G2 */
  0x8001  /* VC8000D */
};

unsigned long base_port = 0;
volatile unsigned char *reg = NULL;
int vcmd = 0;

unsigned long multicorebase[HXDEC_MAX_CORES] = {
  SOCLE_LOGIC_0_BASE,
  SOCLE_LOGIC_1_BASE,
  SOCLE_LOGIC_2_BASE,
  SOCLE_LOGIC_3_BASE
};

int irq[HXDEC_MAX_CORES] = {
  DEC_IRQ_0,
  -1,
  -1,
  -1
};

unsigned int iosize[HXDEC_MAX_CORES] = {
  DEC_IO_SIZE_0,
  DEC_IO_SIZE_1,
  DEC_IO_SIZE_2,
  DEC_IO_SIZE_3
};

/* Because one core may contain multi-pipeline, so multicore base may be changed */
unsigned long multicorebase_actual[HXDEC_MAX_CORES];

struct subsys_config vpu_subsys[MAX_SUBSYS_NUM];

int elements = 4;

#if 0
#ifdef CLK_CFG
struct clk *clk_cfg;
int is_clk_on;
struct timer_list timer;
#endif
#endif

static int hantrodec_major = 0; /* dynamic allocation */

/* here's all the must remember stuff */
typedef struct {
  char *buffer;
  volatile unsigned int iosize[HXDEC_MAX_CORES];
  volatile u8 *hwregs[HXDEC_MAX_CORES];
  volatile int irq[HXDEC_MAX_CORES];
  int hw_id[HXDEC_MAX_CORES];
  int cores;
  //struct fasync_struct *async_queue_dec;
  //struct fasync_struct *async_queue_pp;
} hantrodec_t;

typedef struct {
  u32 cfg[HXDEC_MAX_CORES];              /* indicate the supported format */
  u32 cfg_backup[HXDEC_MAX_CORES];       /* back up of cfg */
  int its_main_core_id[HXDEC_MAX_CORES]; /* indicate if main core exist */
  int its_aux_core_id[HXDEC_MAX_CORES];  /* indicate if aux core exist */
} core_cfg;

static hantrodec_t hantrodec_data; /* dynamic allocation? */

static int ReserveIO(void);
static void ReleaseIO(void);

static void ResetAsic(hantrodec_t * dev);

#ifdef HANTRODEC_DEBUG
static void dump_regs(hantrodec_t *dev);
#endif

/* Interrupt */
/*********************request_irq, disable_irq, enable_irq need to be provided by customer*********************/
static int RegisterIRQ(i32 i, IRQHandler isr, i32 flag, const char* name, void *data);
static void IntEnableIRQ(u32 irq);
static void IntDisableIRQ(u32 irq);
static void IntClearIRQStatus(u32 irq, u32 type);
static u32 IntGetIRQStatus(u32 irq);
static inline uint32_t ReadInterruptStatus(void);

/* IRQ handler */
static irqreturn_t hantrodec_isr(void *args);
static u32 dec_regs[HXDEC_MAX_CORES][DEC_IO_SIZE_MAX/4];

#undef down_interruptible
#undef up
#define down_interruptible(a)              sem_wait(a)
#define up(a)                              sem_post(a)
sem_t dec_core_sem;
sem_t pp_core_sem;

static int dec_irq = 0;
static int pp_irq = 0;

/*********************the atomic operations need to be provided by customer*********************/
atomic_t irq_rx = ATOMIC_INIT(0);
atomic_t irq_tx = ATOMIC_INIT(0);

static int dec_owner[HXDEC_MAX_CORES];
static int pp_owner[HXDEC_MAX_CORES];
static int CoreHasFormat(const u32 *cfg, int core, u32 format);
#if 0
/* spinlock_t owner_lock = SPIN_LOCK_UNLOCKED; */
DEFINE_SPINLOCK(owner_lock);
DECLARE_WAIT_QUEUE_HEAD(dec_wait_queue);
DECLARE_WAIT_QUEUE_HEAD(pp_wait_queue);
DECLARE_WAIT_QUEUE_HEAD(hw_queue);

#ifdef CLK_CFG
DEFINE_SPINLOCK(clk_lock);
#endif
#endif

pthread_mutex_t owner_lock = PTHREAD_MUTEX_INITIALIZER;
sem_t dec_wait_queue;
sem_t pp_wait_queue;
sem_t hw_queue;

#define DWL_CLIENT_TYPE_H264_DEC         1U
#define DWL_CLIENT_TYPE_MPEG4_DEC        2U
#define DWL_CLIENT_TYPE_JPEG_DEC         3U
#define DWL_CLIENT_TYPE_PP               4U
#define DWL_CLIENT_TYPE_VC1_DEC          5U
#define DWL_CLIENT_TYPE_MPEG2_DEC        6U
#define DWL_CLIENT_TYPE_VP6_DEC          7U
#define DWL_CLIENT_TYPE_AVS_DEC          8U
#define DWL_CLIENT_TYPE_RV_DEC           9U
#define DWL_CLIENT_TYPE_VP8_DEC          10U
#define DWL_CLIENT_TYPE_VP9_DEC          11U
#define DWL_CLIENT_TYPE_HEVC_DEC         12U

static core_cfg config;

static void ReadCoreConfig(hantrodec_t *dev) {
  int c;
  u32 reg, tmp, mask;

  memset(config.cfg, 0, sizeof(config.cfg));

  for(c = 0; c < dev->cores; c++) {
    /* Decoder configuration */
    if (IS_G1(dev->hw_id[c])) {
      reg = ioread32((void*)(dev->hwregs[c] + HANTRODEC_SYNTH_CFG * 4));

      tmp = (reg >> DWL_H264_E) & 0x3U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has H264\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

      tmp = (reg >> DWL_JPEG_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has JPEG\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

      tmp = (reg >> DWL_HJPEG_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has HJPEG\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

      tmp = (reg >> DWL_MPEG4_E) & 0x3U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has MPEG4\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

      tmp = (reg >> DWL_VC1_E) & 0x3U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has VC1\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC: 0;

      tmp = (reg >> DWL_MPEG2_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has MPEG2\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

      tmp = (reg >> DWL_VP6_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has VP6\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

      reg = ioread32((void*)(dev->hwregs[c] + HANTRODEC_SYNTH_CFG_2 * 4));

      /* VP7 and WEBP is part of VP8 */
      mask =  (1 << DWL_VP8_E) | (1 << DWL_VP7_E) | (1 << DWL_WEBP_E);
      tmp = (reg & mask);
      if(tmp & (1 << DWL_VP8_E))
        printk(KERN_INFO "hantrodec: core[%d] has VP8\n", c);
      if(tmp & (1 << DWL_VP7_E))
        printk(KERN_INFO "hantrodec: core[%d] has VP7\n", c);
      if(tmp & (1 << DWL_WEBP_E))
        printk(KERN_INFO "hantrodec: core[%d] has WebP\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC : 0;

      tmp = (reg >> DWL_AVS_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has AVS\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC: 0;

      tmp = (reg >> DWL_RV_E) & 0x03U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has RV\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

      /* Post-processor configuration */
      reg = ioread32((void*)(dev->hwregs[c] + HANTROPP_SYNTH_CFG * 4));

      tmp = (reg >> DWL_G1_PP_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has PP\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
    } else if((IS_G2(dev->hw_id[c]))) {
      reg = ioread32((void*)(dev->hwregs[c] + HANTRODEC_CFG_STAT * 4));

      tmp = (reg >> DWL_G2_HEVC_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has HEVC\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_HEVC_DEC : 0;

      tmp = (reg >> DWL_G2_VP9_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has VP9\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP9_DEC : 0;

      /* Post-processor configuration */
      reg = ioread32((void*)(dev->hwregs[c] + HANTRODECPP_SYNTH_CFG * 4));

      tmp = (reg >> DWL_G2_PP_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has PP\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
    } else if((IS_VC8000D(dev->hw_id[c])) && config.its_main_core_id[c] < 0) {
      reg = ioread32((void*)(dev->hwregs[c] + HANTRODEC_SYNTH_CFG * 4));

      tmp = (reg >> DWL_H264_E) & 0x3U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has H264\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

      tmp = (reg >> DWL_H264HIGH10_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has H264HIGH10\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

      tmp = (reg >> DWL_JPEG_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has JPEG\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

      tmp = (reg >> DWL_HJPEG_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has HJPEG\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

      tmp = (reg >> DWL_MPEG4_E) & 0x3U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has MPEG4\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

      tmp = (reg >> DWL_VC1_E) & 0x3U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has VC1\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC: 0;

      tmp = (reg >> DWL_MPEG2_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has MPEG2\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

      tmp = (reg >> DWL_VP6_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has VP6\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

      reg = ioread32((void*)(dev->hwregs[c] + HANTRODEC_SYNTH_CFG_2 * 4));

      /* VP7 and WEBP is part of VP8 */
      mask =  (1 << DWL_VP8_E) | (1 << DWL_VP7_E) | (1 << DWL_WEBP_E);
      tmp = (reg & mask);
      if(tmp & (1 << DWL_VP8_E))
        printk(KERN_INFO "hantrodec: core[%d] has VP8\n", c);
      if(tmp & (1 << DWL_VP7_E))
        printk(KERN_INFO "hantrodec: core[%d] has VP7\n", c);
      if(tmp & (1 << DWL_WEBP_E))
        printk(KERN_INFO "hantrodec: core[%d] has WebP\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC : 0;

      tmp = (reg >> DWL_AVS_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has AVS\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC: 0;

      tmp = (reg >> DWL_RV_E) & 0x03U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has RV\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

      reg = ioread32((void*)(dev->hwregs[c] + HANTRODEC_SYNTH_CFG_3 * 4));

      tmp = (reg >> DWL_HEVC_E) & 0x07U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has HEVC\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_HEVC_DEC : 0;

      tmp = (reg >> DWL_VP9_E) & 0x07U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has VP9\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP9_DEC : 0;

      /* Post-processor configuration */
      reg = ioread32((void*)(dev->hwregs[c] + HANTRODECPP_CFG_STAT * 4));

      tmp = (reg >> DWL_PP_E) & 0x01U;
      if(tmp) printk(KERN_INFO "hantrodec: core[%d] has PP\n", c);
      config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;

      if (config.its_aux_core_id[c] >= 0) {
        /* set main_core_id and aux_core_id */
        reg = ioread32((void*)(dev->hwregs[c] + HANTRODEC_SYNTH_CFG_2 * 4));

        tmp = (reg >> DWL_H264_PIPELINE_E) & 0x01U;
        if(tmp) printk(KERN_INFO "hantrodec: core[%d] has pipeline H264\n", c);
        config.cfg[config.its_aux_core_id[c]] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

        tmp = (reg >> DWL_JPEG_PIPELINE_E) & 0x01U;
        if(tmp) printk(KERN_INFO "hantrodec: core[%d] has pipeline JPEG\n", c);
        config.cfg[config.its_aux_core_id[c]] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;
      }
    }
  }
  memcpy(config.cfg_backup, config.cfg, sizeof(config.cfg));
}

static int CoreHasFormat(const u32 *cfg, int core, u32 format) {
  return (cfg[core] & (1 << format)) ? 1 : 0;
}

int GetDecCore(long core, hantrodec_t *dev, int filp, unsigned long format) {
  int success = 0;
  unsigned long flags;

  spin_lock_irqsave(&owner_lock, flags);
  if(CoreHasFormat(config.cfg, core, format) && dec_owner[core] == 0 /*&& config.its_main_core_id[core] >= 0*/) {
    dec_owner[core] = filp;
    success = 1;

    /* If one main core takes one format which doesn't supported by aux core, set aux core's cfg to none video format support */
    if (config.its_aux_core_id[core] >= 0 &&
        !CoreHasFormat(config.cfg, config.its_aux_core_id[core], format)) {
      config.cfg[config.its_aux_core_id[core]] = 0;
    }
    /* If one aux core takes one format, set main core's cfg to aux core supported video format */
    else if (config.its_main_core_id[core] >= 0) {
      config.cfg[config.its_main_core_id[core]] = config.cfg[core];
    }
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  return success;
}

int GetDecCoreAny(long *core, hantrodec_t *dev, int filp,
                  unsigned long format) {
  int success = 0;
  long c;

  *core = -1;

  for(c = 0; c < dev->cores; c++) {
    /* a free core that has format */
    /* xuanji: simplely impto get core for single core, exists issue */
    if(GetDecCore(c, dev, filp, format)) {
      success = 1;
      *core = c;
      break;
    }
  }

  return success;
}

int GetDecCoreID(hantrodec_t *dev, int filp,
                 unsigned long format) {
  long c;
  unsigned long flags;

  int core_id = -1;

  for(c = 0; c < dev->cores; c++) {
    /* a core that has format */
    spin_lock_irqsave(&owner_lock, flags);
    if(CoreHasFormat(config.cfg, c, format)) {
      core_id = c;
      spin_unlock_irqrestore(&owner_lock, flags);
      break;
    }
    spin_unlock_irqrestore(&owner_lock, flags);
  }
  return core_id;
}

#if 0
static int hantrodec_choose_core(int is_g1) {
  volatile unsigned char *reg = NULL;
  unsigned int blk_base = 0x38320000;

  PDEBUG("hantrodec_choose_core\n");
  if (!request_mem_region(blk_base, 0x1000, "blk_ctl")) {
    printk(KERN_INFO "blk_ctl: failed to reserve HW regs\n");
    return -EBUSY;
  }

  reg = (volatile u8 *) ioremap_nocache(blk_base, 0x1000);

  if (reg == NULL ) {
    printk(KERN_INFO "blk_ctl: failed to ioremap HW regs\n");
    if (reg)
      iounmap((void *)reg);
    release_mem_region(blk_base, 0x1000);
    return -EBUSY;
  }

  // G1 use, set to 1; G2 use, set to 0, choose the one you are using
  if (is_g1)
    iowrite32(0x1, (void*)(reg + 0x14));  // VPUMIX only use G1, user should modify the reg according to platform design
  else
    iowrite32(0x0, (void*)(reg + 0x14)); // VPUMIX only use G2, user should modify the reg according to platform design

  if (reg)
    iounmap((void *)reg);
  release_mem_region(blk_base, 0x1000);
  PDEBUG("hantrodec_choose_core OK!\n");
  return 0;
}
#endif

long ReserveDecoder(hantrodec_t *dev, int filp, unsigned long format) {
  long core = -1;

  /* reserve a core */
  if (down_interruptible(&dec_core_sem))
    return -ERESTARTSYS;

  /* lock a core that has specific format*/
  if(wait_event_interruptible(hw_queue,GetDecCoreAny(&core, dev, filp, format) != 0 ))
    return -ERESTARTSYS;
  /* xuanji: simplely imp to get core for single core, exists issue */
  GetDecCoreAny(&core, dev, filp, format);

#if 0
  if(IS_G1(dev->hw_id[core])) {
    if (0 == hantrodec_choose_core(1))
      printk("G1 is reserved\n");
    else
      return -1;
  } else {
    if (0 == hantrodec_choose_core(0))
      printk("G2 is reserved\n");
    else
      return -1;
  }
#endif

  return core;
}

void ReleaseDecoder(hantrodec_t *dev, long core) {
  u32 status;
  unsigned long flags;

  status = ioread32((void*)(dev->hwregs[core] + HANTRODEC_IRQ_STAT_DEC_OFF));

  /* make sure HW is disabled */
  if(status & HANTRODEC_DEC_E) {
    printk(KERN_INFO "hantrodec: DEC[%li] still enabled -> reset\n", core);

    /* abort decoder */
    status |= HANTRODEC_DEC_ABORT | HANTRODEC_DEC_IRQ_DISABLE;
    iowrite32(status, (void*)(dev->hwregs[core] + HANTRODEC_IRQ_STAT_DEC_OFF));
  }

  spin_lock_irqsave(&owner_lock, flags);

  /* If aux core released, revert main core's config back */
  if (config.its_main_core_id[core] >= 0) {
    config.cfg[config.its_main_core_id[core]] = config.cfg_backup[config.its_main_core_id[core]];
  }

  /* If main core released, revert aux core's config back */
  if (config.its_aux_core_id[core] >= 0) {
    config.cfg[config.its_aux_core_id[core]] = config.cfg_backup[config.its_aux_core_id[core]];
  }

  dec_owner[core] = 0;

  spin_unlock_irqrestore(&owner_lock, flags);

  up(&dec_core_sem);

  wake_up_interruptible_all(&hw_queue);
}

long ReservePostProcessor(hantrodec_t *dev, int filp) {
  unsigned long flags;

  long core = 0;

  /* single core PP only */
  if (down_interruptible(&pp_core_sem))
    return -ERESTARTSYS;

  spin_lock_irqsave(&owner_lock, flags);

  pp_owner[core] = filp;

  spin_unlock_irqrestore(&owner_lock, flags);

  return core;
}

void ReleasePostProcessor(hantrodec_t *dev, long core) {
  unsigned long flags;

  u32 status = ioread32((void*)(dev->hwregs[core] + HANTRO_IRQ_STAT_PP_OFF));

  /* make sure HW is disabled */
  if(status & HANTRO_PP_E) {
    printk(KERN_INFO "hantrodec: PP[%li] still enabled -> reset\n", core);

    /* disable IRQ */
    status |= HANTRO_PP_IRQ_DISABLE;

    /* disable postprocessor */
    status &= (~HANTRO_PP_E);
    iowrite32(0x10, (void*)(dev->hwregs[core] + HANTRO_IRQ_STAT_PP_OFF));
  }

  spin_lock_irqsave(&owner_lock, flags);

  pp_owner[core] = 0;

  spin_unlock_irqrestore(&owner_lock, flags);

  up(&pp_core_sem);
}

long ReserveDecPp(hantrodec_t *dev, int filp, unsigned long format) {
  /* reserve core 0, DEC+PP for pipeline */
  unsigned long flags;

  long core = 0;

  /* check that core has the requested dec format */
  if(!CoreHasFormat(config.cfg, core, format))
    return -EFAULT;

  /* check that core has PP */
  if(!CoreHasFormat(config.cfg, core, DWL_CLIENT_TYPE_PP))
    return -EFAULT;

  /* reserve a core */
  if (down_interruptible(&dec_core_sem))
    return -ERESTARTSYS;

  /* wait until the core is available */
  if(wait_event_interruptible(hw_queue, GetDecCore(core, dev, filp, format) != 0)) {
    up(&dec_core_sem);
    return -ERESTARTSYS;
  }
  /* xuanji: simplely imp to get core for single core, exists issue */
  GetDecCore(core, dev, filp, format);

  if (down_interruptible(&pp_core_sem)) {
    ReleaseDecoder(dev, core);
    return -ERESTARTSYS;
  }

  spin_lock_irqsave(&owner_lock, flags);
  pp_owner[core] = filp;
  spin_unlock_irqrestore(&owner_lock, flags);

  return core;
}

long DecFlushRegs(hantrodec_t *dev, struct core_desc *core) {
  long ret = 0, i;

  u32 id = core->id;

#if 1
  ret = copy_from_user(dec_regs[id], core->regs, HANTRO_VC8000D_REGS*4);
  if (ret) {
    PDEBUG("Hantrodec copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  /* write all regs but the status reg[1] to hardware */
  iowrite32(0x0, (void*)(dev->hwregs[id] + 4));
  for(i = 2; i <= HANTRO_VC8000D_LAST_REG; i++) {
    iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id] + i*4));
  }
#else
  if (IS_G1(dev->hw_id[id])) {
    /* copy original dec regs to kernal space*/
    ret = copy_from_user(dec_regs[id], core->regs, HANTRO_DEC_ORG_REGS*4);
    if (ret) {
      PDEBUG("copy_from_user failed, returned %li\n", ret);
      return -EFAULT;
    }
#ifdef USE_64BIT_ENV
    /* copy extended dec regs to kernal space*/
    ret = copy_from_user(dec_regs[id] + HANTRO_DEC_EXT_FIRST_REG,
                         core->regs + HANTRO_DEC_EXT_FIRST_REG,
                         HANTRO_DEC_EXT_REGS*4);
#endif
    if (ret) {
      PDEBUG("copy_from_user failed, returned %li\n", ret);
      return -EFAULT;
    }

    /* write dec regs but the status reg[1] to hardware */
    /* both original and extended regs need to be written */
    for(i = 2; i <= HANTRO_DEC_ORG_LAST_REG; i++) {
      iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
    }
#ifdef USE_64BIT_ENV
    for(i = HANTRO_DEC_EXT_FIRST_REG; i <= HANTRO_DEC_EXT_LAST_REG; i++)
      iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);
#endif
  } else {
    ret = copy_from_user(dec_regs[id], core->regs, HANTRO_G2_DEC_REGS*4);
    if (ret) {
      PDEBUG("copy_from_user failed, returned %li\n", ret);
      return -EFAULT;
    }

    /* write all regs but the status reg[1] to hardware */
    for(i = 2; i <= HANTRO_G2_DEC_LAST_REG; i++) {
#if 0
      if(i==2)
        //dec_regs[id][i] = 0x78777777;
        //dec_regs[id][i] = 0x78778787;
        //c_regs[id][i] = 0x78888888; //64bit
      {
        printk("reg2=%08x\n",dec_regs[id][i]);
        //dec_regs[id][i] = 0xF0F00000;//128bit 0xF0F0F000
      }
      //dec_regs[id][i] = 0xF0F0000F;//128bit 0xF0F00000 for big endian
      if(i==58) {
        printk("reg58=%08x\n",dec_regs[id][i]);
        //dec_regs[id][i]= 0x210;//128bit
        //dec_regs[id][i]= 0x110;//64bit
      }
      if(i==3) {
        printk("reg3=%08x\n",dec_regs[id][i]);
        //dec_regs[id][i] |= 0x00F00000;//128bit
        //dec_regs[id][i]= 0x110;//64bit
      }
#endif
      iowrite32(dec_regs[id][i], dev->hwregs[id] + i*4);

    }
  }
#endif

  /* write the status register, which may start the decoder */
  iowrite32(dec_regs[id][1], (void*)(dev->hwregs[id] + 4));

  PDEBUG("Hantrodec flushed registers on core %d\n", id);

  return 0;
}


long DecWriteRegs(hantrodec_t *dev, struct core_desc *core)
{
  long ret = 0, i;
  u32 id = core->id;
  i = core->reg_id;

  ret = copy_from_user(dec_regs[id] + core->reg_id, core->regs + core->reg_id, 4);

  if (ret) {
    PDEBUG("Hantrodec copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id] + i*4));
  return 0;
}

long DecReadRegs(hantrodec_t *dev, struct core_desc *core)
{
  long ret, i;
  u32 id = core->id;
  i = core->reg_id;

  /* user has to know exactly what they are asking for */
  //if(core->size != (HANTRO_VC8000D_REGS * 4))
  //  return -EFAULT;

  /* read specific registers from hardware */
  i = core->reg_id;
  dec_regs[id][i] = ioread32((void*)(dev->hwregs[id] + i*4));

  /* put registers to user space*/
  ret = copy_to_user(core->regs + core->reg_id, dec_regs[id] + core->reg_id, 4);
  if (ret) {
    PDEBUG("Hantrodec copy_to_user failed, returned %li\n", ret);
    return -EFAULT;
  }
  return 0;
}

long DecRefreshRegs(hantrodec_t *dev, struct core_desc *core) {
  long ret, i;
  u32 id = core->id;

#if 1
  //for(i = 0; i <= HANTRO_DEC_ORG_LAST_REG; i++) {
  for(i = 0; i <= HANTRO_VC8000D_LAST_REG; i++) {
    dec_regs[id][i] = ioread32((void*)(dev->hwregs[id] + i*4));
  }
  //ret = copy_to_user(core->regs, dec_regs[id], HANTRO_DEC_ORG_REGS*4);
  ret = copy_to_user(core->regs, dec_regs[id], HANTRO_G2_DEC_REGS*4);
  if (ret) {
    PDEBUG("Hantrodec copy_to_user failed, returned %li\n", ret);
    return -EFAULT;
  }
#else

  if (IS_G1(dev->hw_id[id])) {
#ifdef USE_64BIT_ENV
    /* user has to know exactly what they are asking for */
    if(core->size != (HANTRO_DEC_TOTAL_REGS * 4))
      return -EFAULT;
#else
    /* user has to know exactly what they are asking for */
    //if(core->size != (HANTRO_DEC_ORG_REGS * 4))
    //  return -EFAULT;
#endif
    /* read all registers from hardware */
    /* both original and extended regs need to be read */
    for(i = 0; i <= HANTRO_DEC_ORG_LAST_REG; i++)
      dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
#ifdef USE_64BIT_ENV
    for(i = HANTRO_DEC_EXT_FIRST_REG; i <= HANTRO_DEC_EXT_LAST_REG; i++)
      dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);
#endif
    /* put registers to user space*/
    /* put original registers to user space*/
    ret = copy_to_user(core->regs, dec_regs[id], HANTRO_DEC_ORG_REGS*4);
#ifdef USE_64BIT_ENV
    /*put extended registers to user space*/
    ret = copy_to_user(core->regs + HANTRO_DEC_EXT_FIRST_REG,
                       dec_regs[id] + HANTRO_DEC_EXT_FIRST_REG,
                       HANTRO_DEC_EXT_REGS * 4);
#endif
    if (ret) {
      PDEBUG("copy_to_user failed, returned %li\n", ret);
      return -EFAULT;
    }
  } else {
    /* user has to know exactly what they are asking for */
    if(core->size != (HANTRO_G2_DEC_REGS * 4))
      return -EFAULT;

    /* read all registers from hardware */
    for(i = 0; i <= HANTRO_G2_DEC_LAST_REG; i++)
      dec_regs[id][i] = ioread32(dev->hwregs[id] + i*4);

    /* put registers to user space*/
    ret = copy_to_user(core->regs, dec_regs[id], HANTRO_G2_DEC_REGS*4);
    if (ret) {
      PDEBUG("copy_to_user failed, returned %li\n", ret);
      return -EFAULT;
    }
  }
#endif
  return 0;
}

static int CheckDecIrq(hantrodec_t *dev, int id) {
  unsigned long flags;
  int rdy = 0;

  const u32 irq_mask = (1 << id);

  spin_lock_irqsave(&owner_lock, flags);

  if(dec_irq & irq_mask) {
    /* reset the wait condition(s) */
    dec_irq &= ~irq_mask;
    rdy = 1;
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  return rdy;
}

long WaitDecReadyAndRefreshRegs(hantrodec_t *dev, struct core_desc *core) {
  u32 id = core->id;
  long ret;

  PDEBUG("Hantrodec wait_event_interruptible DEC[%d]\n", id);
  ret = wait_event_interruptible(dec_wait_queue, CheckDecIrq(dev, id));
  if(ret) {
    PDEBUG("Hantrodec DEC[%d]  wait_event_interruptible interrupted\n", id);
    return -ERESTARTSYS;
  }
  /* xuanji: simplely imp to get core for single core, exists issue */
  CheckDecIrq(dev, id);

  atomic_inc(&irq_tx);

  /* refresh registers */
  return DecRefreshRegs(dev, core);
}

long PPFlushRegs(hantrodec_t *dev, struct core_desc *core) {
  long ret = 0;
  u32 id = core->id;
  u32 i;

  /* copy original dec regs to kernal space*/
  ret = copy_from_user(dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
                       core->regs + HANTRO_PP_ORG_FIRST_REG,
                       HANTRO_PP_ORG_REGS*4);
  if(sizeof(addr_t) == 8) {
    /* copy extended dec regs to kernal space*/
    ret = copy_from_user(dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                         core->regs + HANTRO_PP_EXT_FIRST_REG,
                         HANTRO_PP_EXT_REGS*4);
  }
  if (ret) {
    PDEBUG("Hantrodec copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  /* write all regs but the status reg[1] to hardware */
  /* both original and extended regs need to be written */
  for(i = HANTRO_PP_ORG_FIRST_REG + 1; i <= HANTRO_PP_ORG_LAST_REG; i++)
    iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id] + i*4));
  if(sizeof(addr_t) == 8) {
    for(i = HANTRO_PP_EXT_FIRST_REG; i <= HANTRO_PP_EXT_LAST_REG; i++)
      iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id] + i*4));
  }
  /* write the stat reg, which may start the PP */
  iowrite32(dec_regs[id][HANTRO_PP_ORG_FIRST_REG],
            (void*)(dev->hwregs[id] + HANTRO_PP_ORG_FIRST_REG * 4));

  return 0;
}

long PPRefreshRegs(hantrodec_t *dev, struct core_desc *core) {
  long i, ret;
  u32 id = core->id;
  if(sizeof(addr_t) == 8) {
    /* user has to know exactly what they are asking for */
    if(core->size != (HANTRO_PP_TOTAL_REGS * 4))
      return -EFAULT;
  } else {
    /* user has to know exactly what they are asking for */
    if(core->size != (HANTRO_PP_ORG_REGS * 4))
      return -EFAULT;
  }

  /* read all registers from hardware */
  /* both original and extended regs need to be read */
  for(i = HANTRO_PP_ORG_FIRST_REG; i <= HANTRO_PP_ORG_LAST_REG; i++)
    dec_regs[id][i] = ioread32((void*)(dev->hwregs[id] + i*4));
  if(sizeof(addr_t) == 8) {
    for(i = HANTRO_PP_EXT_FIRST_REG; i <= HANTRO_PP_EXT_LAST_REG; i++)
      dec_regs[id][i] = ioread32((void*)(dev->hwregs[id] + i*4));
  }
  /* put registers to user space*/
  /* put original registers to user space*/
  ret = copy_to_user(core->regs + HANTRO_PP_ORG_FIRST_REG,
                     dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
                     HANTRO_PP_ORG_REGS*4);
  if(sizeof(addr_t) == 8) {
    /* put extended registers to user space*/
    ret = copy_to_user(core->regs + HANTRO_PP_EXT_FIRST_REG,
                       dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                       HANTRO_PP_EXT_REGS * 4);
  }
  if (ret) {
    PDEBUG("Hantrodec copy_to_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  return 0;
}

static int CheckPPIrq(hantrodec_t *dev, int id) {
  unsigned long flags;
  int rdy = 0;

  const u32 irq_mask = (1 << id);

  spin_lock_irqsave(&owner_lock, flags);

  if(pp_irq & irq_mask) {
    /* reset the wait condition(s) */
    pp_irq &= ~irq_mask;
    rdy = 1;
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  return rdy;
}

long WaitPPReadyAndRefreshRegs(hantrodec_t *dev, struct core_desc *core) {
  u32 id = core->id;

  PDEBUG("Hantrodec wait_event_interruptible PP[%d]\n", id);

  if(wait_event_interruptible(pp_wait_queue, CheckPPIrq(dev, id))) {
    PDEBUG("Hantrodec PP[%d]  wait_event_interruptible interrupted\n", id);
    return -ERESTARTSYS;
  }
  /* xuanji: simplely imp to get core for single core, exists issue */
  CheckPPIrq(dev, id);

  atomic_inc(&irq_tx);

  /* refresh registers */
  return PPRefreshRegs(dev, core);
}

static int CheckCoreIrq(hantrodec_t *dev, const int filp, int *id) {
  unsigned long flags;
  int rdy = 0, n = 0;

  do {
    u32 irq_mask = (1 << n);

    spin_lock_irqsave(&owner_lock, flags);

    if(dec_irq & irq_mask) {
      if (dec_owner[n] == filp) {
        /* we have an IRQ for our client */

        /* reset the wait condition(s) */
        dec_irq &= ~irq_mask;

        /* signal ready core no. for our client */
        *id = n;

        rdy = 1;

        spin_unlock_irqrestore(&owner_lock, flags);
        break;
      } else if(dec_owner[n] == 0) {
        /* zombie IRQ */
        printk(KERN_INFO "IRQ on core[%d], but no owner!!!\n", n);

        /* reset the wait condition(s) */
        dec_irq &= ~irq_mask;
      }
    }

    spin_unlock_irqrestore(&owner_lock, flags);

    n++; /* next core */
  } while(n < dev->cores);

  return rdy;
}

long WaitCoreReady(hantrodec_t *dev, const int filp, int *id) {
  PDEBUG("Hantrodec wait_event_interruptible CORE\n");

  if(wait_event_interruptible(dec_wait_queue, CheckCoreIrq(dev, filp, id))) {
    PDEBUG("Hantrodec CORE  wait_event_interruptible interrupted\n");
    return -ERESTARTSYS;
  }
  /* xuanji: simplely imp to get core for single core, exists issue */
  CheckCoreIrq(dev, filp, id);

  atomic_inc(&irq_tx);

  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_ioctl
 Description     : communication method to/from the user space

 Return type     : long
------------------------------------------------------------------------------*/

long hantrodec_ioctl(int filp, unsigned int cmd, void *arg) {
  int err = 0;
  long tmp;
#if 0
#ifdef CLK_CFG
  unsigned long flags;
#endif
#endif

#ifdef HW_PERFORMANCE
  struct timeval *end_time_arg;
#endif

  PDEBUG("Hantrodec ioctl cmd 0x%08x\n", cmd);
  /*
   * extract the type and number bitfields, and don't decode
   * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
   */
  if (_IOC_TYPE(cmd) != HANTRODEC_IOC_MAGIC &&
      //_IOC_TYPE(cmd) != HANTRO_IOC_MMU &&
      _IOC_TYPE(cmd) != HANTRO_VCMD_IOC_MAGIC)
    return -ENOTTY;
  if ((_IOC_TYPE(cmd) == HANTRODEC_IOC_MAGIC &&
      _IOC_NR(cmd) > HANTRODEC_IOC_MAXNR) ||
      //(_IOC_TYPE(cmd) == HANTRO_IOC_MMU &&
      //_IOC_NR(cmd) > HANTRO_IOC_MMU_MAXNR) ||
      (_IOC_TYPE(cmd) == HANTRO_VCMD_IOC_MAGIC &&
      _IOC_NR(cmd) > HANTRO_VCMD_IOC_MAXNR))
    return -ENOTTY;

  /*
   * the direction is a bitmask, and VERIFY_WRITE catches R/W
   * transfers. `Type' is user-oriented, while
   * access_ok is kernel-oriented, so the concept of "read" and
   * "write" is reversed
   */
  if (_IOC_DIR(cmd) & _IOC_READ)
    err = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));
  else if (_IOC_DIR(cmd) & _IOC_WRITE)
    err = !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));

  if (err)
    return -EFAULT;

#if 0
#ifdef CLK_CFG
  spin_lock_irqsave(&clk_lock, flags);
  if (clk_cfg!=NULL && !IS_ERR(clk_cfg)&&(is_clk_on==0)) {
    printk("Hantrodec turn on clock by user\n");
    if (clk_enable(clk_cfg)) {
      spin_unlock_irqrestore(&clk_lock, flags);
      return -EFAULT;
    } else
      is_clk_on=1;
  }
  spin_unlock_irqrestore(&clk_lock, flags);
  mod_timer(&timer, jiffies + 10*HZ); /*the interval is 10s*/
#endif
#endif

  switch (cmd) {
  case HANTRODEC_IOC_CLI: {
    __u32 id;
    __get_user(id, (__u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    disable_irq(hantrodec_data.irq[id]);
    break;
  }
  case HANTRODEC_IOC_STI: {
    __u32 id;
    __get_user(id, (__u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    enable_irq(hantrodec_data.irq[id]);
    break;
  }
  case HANTRODEC_IOCGHWOFFSET: {
    __u32 id;
    __get_user(id, (__u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }

    __put_user(multicorebase_actual[id], (unsigned long *) arg);
    break;
  }
  case HANTRODEC_IOCGHWIOSIZE: {
    __u32 id;
    __u32 io_size;
    __get_user(id, (__u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    io_size = hantrodec_data.iosize[id];
    __put_user(io_size, (u32 *) arg);

    return 0;
  }
  case HANTRODEC_IOC_MC_OFFSETS: {
    tmp = copy_to_user((unsigned long *) arg, multicorebase_actual, sizeof(multicorebase_actual));
    if (err) {
      PDEBUG("Hantrodec copy_to_user failed, returned %li\n", tmp);
      return -EFAULT;
    }
    break;
  }
  case HANTRODEC_IOC_MC_CORES:
    __put_user(hantrodec_data.cores, (unsigned int *) arg);
    PDEBUG("Hantrodec hantrodec_data.cores=%d\n", hantrodec_data.cores);
    break;
  case HANTRODEC_IOCS_DEC_PUSH_REG: {
    struct core_desc core;
    uint32_t curr_int_status = 0;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    DecFlushRegs(&hantrodec_data, &core);
    curr_int_status = ReadInterruptStatus();
    break;
  }
  case HANTRODEC_IOCS_DEC_WRITE_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    DecWriteRegs(&hantrodec_data, &core);
    break;
  }
  case HANTRODEC_IOCS_PP_PUSH_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    PPFlushRegs(&hantrodec_data, &core);
    break;
  }
  case HANTRODEC_IOCS_DEC_PULL_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return DecRefreshRegs(&hantrodec_data, &core);
  }
  case HANTRODEC_IOCS_DEC_READ_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return DecReadRegs(&hantrodec_data, &core);
  }
  case HANTRODEC_IOCS_PP_PULL_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return PPRefreshRegs(&hantrodec_data, &core);
  }
  case HANTRODEC_IOCH_DEC_RESERVE: {
    u32 format = 0;
    __get_user(format, (unsigned long *)arg);
    PDEBUG("Hantrodec Reserve DEC core, format = %li\n", format);
    return ReserveDecoder(&hantrodec_data, filp, format);
  }
  case HANTRODEC_IOCT_DEC_RELEASE: {
    u32 core = *(unsigned long *)arg;
    if(core >= hantrodec_data.cores || dec_owner[core] != filp) {
      PDEBUG("Hantrodec bogus DEC release, core = %li\n", core);
      return -EFAULT;
    }

    PDEBUG("Hantrodec Release DEC, core = %li\n", core);

    ReleaseDecoder(&hantrodec_data, core);

    break;
  }
  case HANTRODEC_IOCQ_PP_RESERVE:
    return ReservePostProcessor(&hantrodec_data, filp);
  case HANTRODEC_IOCT_PP_RELEASE: {
    u32 core = 0;
    __get_user(core, (unsigned long *)arg);
    if(core != 0 || pp_owner[core] != filp) {
      PDEBUG("Hantrodec bogus PP release %li\n", core);
      return -EFAULT;
    }

    ReleasePostProcessor(&hantrodec_data, core);

    break;
  }
  case HANTRODEC_IOCX_DEC_WAIT: {
    struct core_desc core;

    /* get registers from user space */
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return WaitDecReadyAndRefreshRegs(&hantrodec_data, &core);
  }
  case HANTRODEC_IOCX_PP_WAIT: {
    struct core_desc core;

    /* get registers from user space */
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("Hantrodec copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return WaitPPReadyAndRefreshRegs(&hantrodec_data, &core);
  }
  case HANTRODEC_IOCG_CORE_WAIT: {
    int id;
    uint32_t curr_int_status = 0;
    curr_int_status = ReadInterruptStatus();
    tmp = WaitCoreReady(&hantrodec_data, filp, &id);
    __put_user(id, (int *) arg);
    return tmp;
  }
  case HANTRODEC_IOX_ASIC_ID: {
    u32 id;
    __get_user(id, (u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    id = ioread32((void*)(hantrodec_data.hwregs[id]));
    __put_user(id, (u32 *) arg);
    return 0;
  }
  case HANTRODEC_IOCG_CORE_ID: {
    u32 format = 0;
    __get_user(format, (unsigned long *)arg);

    PDEBUG("Hantrodec Get DEC Core_id, format = %li\n", format);
    return GetDecCoreID(&hantrodec_data, filp, format);
  }
  case HANTRODEC_IOX_ASIC_BUILD_ID: {
    u32 id, hw_id;
    __get_user(id, (u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    hw_id = ioread32((void*)(hantrodec_data.hwregs[id]));
    if (IS_G1(hw_id >> 16) || IS_G2(hw_id >> 16))
      __put_user(hw_id, (u32 *) arg);
    else {
      hw_id = ioread32((void*)(hantrodec_data.hwregs[id] + HANTRODEC_HW_BUILD_ID_OFF));
      __put_user(hw_id, (u32 *) arg);
    }
    return 0;
  }
  case HANTRODEC_DEBUG_STATUS: {
    printk(KERN_INFO "hantrodec: dec_irq     = 0x%08x \n", dec_irq);
    printk(KERN_INFO "hantrodec: pp_irq      = 0x%08x \n", pp_irq);

    printk(KERN_INFO "hantrodec: IRQs received/sent2user = %d / %d \n",
           atomic_read(&irq_rx), atomic_read(&irq_tx));

    for (tmp = 0; tmp < hantrodec_data.cores; tmp++) {
      printk(KERN_INFO "hantrodec: dec_core[%li] %s\n",
             tmp, dec_owner[tmp] == NULL ? "FREE" : "RESERVED");
      printk(KERN_INFO "hantrodec: pp_core[%li]  %s\n",
             tmp, pp_owner[tmp] == NULL ? "FREE" : "RESERVED");
    }
    return 0;
  }
  case HANTRODEC_IOX_SUBSYS: {
    struct subsys_desc subsys = {0};
    /* TODO(min): check all the subsys */
    if (vcmd) {
      subsys.subsys_vcmd_num = 1;
      subsys.subsys_num = subsys.subsys_vcmd_num;
    } else {
      subsys.subsys_num = hantrodec_data.cores;
      subsys.subsys_vcmd_num = 0;
    }
    copy_to_user((u32 *) arg, &subsys, sizeof(struct subsys_desc));
    return 0;
  }
  case HANTRODEC_IOCX_POLL: {
    hantrodec_isr(&hantrodec_data);
    return 0;
  }
  default:
    if (_IOC_TYPE(cmd) == HANTRO_VCMD_IOC_MAGIC) {
      return (hantrovcmd_ioctl(filp, cmd, arg));
    }
    return -ENOTTY;
  }

  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_open
 Description     : open method

 Return type     : int
------------------------------------------------------------------------------*/

int hantrodec_open(int *inode, int filp) {
  PDEBUG("Hantrodec dev opened\n");
  
  if (vcmd)
    hantrovcmd_open(inode, filp);

  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_release
 Description     : Release driver

 Return type     : int
------------------------------------------------------------------------------*/

int hantrodec_release(int *inode, int filp) {
  int n;
  hantrodec_t *dev = &hantrodec_data;

  PDEBUG("Hantrodec closing ...\n");

  if (vcmd) {
    hantrovcmd_release(inode, filp);
    return 0;
  }

  for(n = 0; n < dev->cores; n++) {
    if(dec_owner[n] == filp) {
      PDEBUG("Hantrodec releasing dec core %i lock\n", n);
      ReleaseDecoder(dev, n);
    }
  }

  for(n = 0; n < 1; n++) {
    if(pp_owner[n] == filp) {
      PDEBUG("Hantrodec releasing pp core %i lock\n", n);
      ReleasePostProcessor(dev, n);
    }
  }

  PDEBUG("Hantrodec closed\n");
  return 0;
}

#if 0
#ifdef CLK_CFG
void hantrodec_disable_clk(unsigned long value) {
  unsigned long flags;
  /*entering this function means decoder is idle over expiry.So disable clk*/
  if (clk_cfg!=NULL && !IS_ERR(clk_cfg)) {
    spin_lock_irqsave(&clk_lock, flags);
    if (is_clk_on==1) {
      clk_disable(clk_cfg);
      is_clk_on = 0;
      printk("turned off hantrodec clk\n");
    }
    spin_unlock_irqrestore(&clk_lock, flags);
  }
}
#endif
#endif

/* VFS methods */
/*
static struct file_operations hantrodec_fops = {
  .owner = THIS_MODULE,
  .open = hantrodec_open,
  .release = hantrodec_release,
  .unlocked_ioctl = hantrodec_ioctl,
  .fasync = NULL
};
*/

/*------------------------------------------------------------------------------
 Function name   : hantrodec_init
 Description     : Initialize the driver

 Return type     : int
------------------------------------------------------------------------------*/
int __init hantrodec_init(void) {
  int result, i;

  PDEBUG("Hantrodec module init\n");

  CheckSubsysCoreArray(vpu_subsys, &vcmd);

  if (vcmd) {
    result = hantrovcmd_init();
    if (result) return result;

    return 0;
  }

  printk(KERN_INFO "hantrodec: dec/pp kernel module. \n");

  /* If base_port is set, use that for single core legacy mode */
  if(base_port != 0) {
    multicorebase[0] = base_port;
    elements = 1;
    printk(KERN_INFO "hantrodec: Init single core at 0x%08lx IRQ=%i\n",
           multicorebase[0], irq[0]);
  } else {
    printk(KERN_INFO "hantrodec: Init multi core[0] at 0x%16lx\n"
           "                      core[1] at 0x%16lx\n"
           "                      core[2] at 0x%16lx\n"
           "                      core[3] at 0x%16lx\n"
           "           IRQ_0=%i\n"
           "           IRQ_1=%i\n",
           multicorebase[0], multicorebase[1],
           multicorebase[2], multicorebase[3],
           irq[0],irq[1]);
  }

  hantrodec_data.cores = 0;

  hantrodec_data.iosize[0] = DEC_IO_SIZE_0;
  hantrodec_data.irq[0] = irq[0];
  hantrodec_data.iosize[1] = DEC_IO_SIZE_1;
  hantrodec_data.irq[1] = irq[1];

  for(i=0; i< HXDEC_MAX_CORES; i++) {
    hantrodec_data.hwregs[i] = 0;
    /* If user gave less core bases that we have by default,
     * invalidate default bases
     */
    if(elements && i>=elements) {
      multicorebase[i] = -1;
    }
  }

  //hantrodec_data.async_queue_dec = NULL;
  //hantrodec_data.async_queue_pp = NULL;

  result = register_chrdev(hantrodec_major, "hantrodec", &hantrodec_fops);
  if(result < 0) {
    printk(KERN_INFO "hantrodec: unable to get major %d\n", hantrodec_major);
    goto err;
  } else if(result != 0) { /* this is for dynamic major */
    hantrodec_major = result;
  }
  
#if 0
#ifdef CLK_CFG
  /* first get clk instance pointer */
  clk_cfg = clk_get(NULL, CLK_ID);
  if (!clk_cfg||IS_ERR(clk_cfg)) {
    printk("get handrodec clk failed!\n");
    goto err;
  }

  /* prepare and enable clk */
  if(clk_prepare_enable(clk_cfg)) {
    printk("try to enable handrodec clk failed!\n");
    goto err;
  }
  is_clk_on = 1;

  /*init a timer to disable clk*/
  init_timer(&timer);
  timer.function = &hantrodec_disable_clk;
  timer.expires =  jiffies + 100*HZ; //the expires time is 100s
  add_timer(&timer);
#endif
#endif

  result = ReserveIO();
  if(result < 0) {
    goto err;
  }

  memset(dec_owner, 0, sizeof(dec_owner));
  memset(pp_owner, 0, sizeof(pp_owner));

  sema_init(&dec_core_sem, hantrodec_data.cores);
  sema_init(&pp_core_sem, 1);

  sem_init(&dec_wait_queue, 0, 0);
  sem_init(&pp_wait_queue, 0, 0);
  sem_init(&hw_queue, 0, hantrodec_data.cores);
  
  /* read configuration fo all cores */
  ReadCoreConfig(&hantrodec_data);

  /* reset hardware */
  ResetAsic(&hantrodec_data);

  /* register irq for each core */
  if(irq[0] > 0) {
    result = request_irq(irq[0], hantrodec_isr, IRQF_SHARED, "hantrodec", (void *) &hantrodec_data);

    if(result != 0) {
      if(result == -EINVAL) {
        printk(KERN_ERR "hantrodec: Bad irq number or handler\n");
      } else if(result == -EBUSY) {
        printk(KERN_ERR "hantrodec: IRQ <%d> busy, change your config\n",
               hantrodec_data.irq[0]);
      }

      ReleaseIO();
      goto err;
    }
  } else {
    printk(KERN_INFO "hantrodec: IRQ not in use!\n");
  }

  if(irq[1] > 0) {
    result = request_irq(irq[1], hantrodec_isr,
//#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
//                         SA_INTERRUPT | SA_SHIRQ,
//#else
                         IRQF_SHARED,
//#endif
                         "hantrodec", (void *) &hantrodec_data);

    if(result != 0) {
      if(result == -EINVAL) {
        printk(KERN_ERR "hantrodec: Bad irq number or handler\n");
      } else if(result == -EBUSY) {
        printk(KERN_ERR "hantrodec: IRQ <%d> busy, change your config\n",
               hantrodec_data.irq[1]);
      }

      ReleaseIO();
      goto err;
    }
  } else {
    printk(KERN_INFO "hantrodec: IRQ not in use!\n");
  }

  printk(KERN_INFO "hantrodec: module inserted. Major = %d\n", hantrodec_major);

  return 0;

err:
  ReleaseIO();
  printk(KERN_INFO "hantrodec: module not inserted\n");
  unregister_chrdev(hantrodec_major, "hantrodec");
  return result;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_cleanup
 Description     : clean up

 Return type     : int
------------------------------------------------------------------------------*/
void __exit hantrodec_cleanup(void) {
  hantrodec_t *dev = &hantrodec_data;
  int n = 0;
  /* reset hardware */
  ResetAsic(dev);

  /* free the IRQ */
  for (n = 0; n < dev->cores; n++) {
    if(dev->irq[n] != -1) {
      free_irq(dev->irq[n], (void *) dev);
    }
  }

  ReleaseIO();

#if 0
#ifdef CLK_CFG
  if (clk_cfg!=NULL && !IS_ERR(clk_cfg)) {
    clk_disable_unprepare(clk_cfg);
    is_clk_on = 0;
    printk("turned off hantrodec clk\n");
  }

  /*delete timer*/
  del_timer(&timer);
#endif
#endif
  unregister_chrdev(hantrodec_major, "hantrodec");

  printk(KERN_INFO "hantrodec: module removed\n");
  return;
}
/*------------------------------------------------------------------------------
 Function name   : CheckHwId
 Return type     : int
------------------------------------------------------------------------------*/
static int CheckHwId(hantrodec_t * dev) {
  long int hwid;
  int i;
  size_t num_hw = sizeof(DecHwId) / sizeof(*DecHwId);

  int found = 0;

  for (i = 0; i < dev->cores; i++) {
    if (dev->hwregs[i] != NULL ) {
      hwid = readl(dev->hwregs[i]);
      printk(KERN_INFO "hantrodec: core %d HW ID=0x%16lx\n", i, hwid);
      hwid = (hwid >> 16) & 0xFFFF; /* product version only */

      while (num_hw--) {
        if (hwid == DecHwId[num_hw]) {
          printk(KERN_INFO "hantrodec: Supported HW found at 0x%16lx\n",
                 multicorebase_actual[i]);
          found++;
          dev->hw_id[i] = hwid;
          break;
        }
      }
      if (!found) {
        printk(KERN_INFO "hantrodec: Unknown HW found at 0x%16lx\n",
               multicorebase_actual[i]);
        return 0;
      }
      found = 0;
      num_hw = sizeof(DecHwId) / sizeof(*DecHwId);
    }
  }

  return 1;
}

/*------------------------------------------------------------------------------
 Function name   : ReserveIO
 Description     : IO reserve

 Return type     : int
------------------------------------------------------------------------------*/
static int ReserveIO(void) {
  int i = 0;
  long int hwid = 0;
  u32 reg = 0;

  memcpy(multicorebase_actual, multicorebase, HXDEC_MAX_CORES * sizeof(unsigned long));
  memcpy((unsigned int*)(hantrodec_data.iosize), iosize, HXDEC_MAX_CORES * sizeof(unsigned int));
  memcpy((unsigned int*)(hantrodec_data.irq), irq, HXDEC_MAX_CORES * sizeof(int));

  for (i = 0; i < HXDEC_MAX_CORES; i++) {
    if (multicorebase_actual[i] != -1) {
      if (!request_mem_region(multicorebase_actual[i], hantrodec_data.iosize[i],
                              "hantrodec0")) {
        printk(KERN_INFO "hantrodec: failed to reserve HW regs\n");
        return -EBUSY;
      }

      hantrodec_data.hwregs[i] = (volatile u8 *) ioremap_nocache(multicorebase_actual[i],
                                 hantrodec_data.iosize[i]);

      if (hantrodec_data.hwregs[i] == NULL ) {
        printk(KERN_INFO "hantrodec: failed to ioremap HW regs\n");
        ReleaseIO();
        return -EBUSY;
      }
      hantrodec_data.cores++;
      config.its_main_core_id[i] = -1;
      config.its_aux_core_id[i] = -1;

      hwid = ((readl(hantrodec_data.hwregs[i])) >> 16) & 0xFFFF; /* product version only */

      if (IS_VC8000D(hwid)) {
        reg = readl(hantrodec_data.hwregs[i] + HANTRODEC_SYNTH_CFG_2_OFF);
        if (((reg >> DWL_H264_PIPELINE_E) & 0x01U) || ((reg >> DWL_JPEG_PIPELINE_E) & 0x01U)) {
          i++;
          config.its_aux_core_id[i-1] = i;
          config.its_main_core_id[i] = i-1;
          config.its_aux_core_id[i] = -1;
          multicorebase_actual[i] = multicorebase_actual[i-1] + 0x800;
          hantrodec_data.iosize[i] = hantrodec_data.iosize[i-1];
          memcpy(multicorebase_actual+i+1, multicorebase+i,
                 (HXDEC_MAX_CORES - i - 1) * sizeof(unsigned long));
          memcpy((unsigned int*)hantrodec_data.iosize+i+1, iosize+i,
                 (HXDEC_MAX_CORES - i - 1) * sizeof(unsigned int));
          if (!request_mem_region(multicorebase_actual[i], hantrodec_data.iosize[i],
                              "hantrodec0")) {
            printk(KERN_INFO "hantrodec: failed to reserve HW regs\n");
            return -EBUSY;
          }

          hantrodec_data.hwregs[i] = (volatile u8 *) ioremap_nocache(multicorebase_actual[i],
                                     hantrodec_data.iosize[i]);

          if (hantrodec_data.hwregs[i] == NULL ) {
            printk(KERN_INFO "hantrodec: failed to ioremap HW regs\n");
            ReleaseIO();
              return -EBUSY;
          }
          hantrodec_data.cores++;
        }
      }
    }
  }

  /* check for correct HW */
  if (!CheckHwId(&hantrodec_data)) {
    ReleaseIO();
    return -EBUSY;
  }

  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : releaseIO
 Description     : release

 Return type     : void
------------------------------------------------------------------------------*/

static void ReleaseIO(void) {
  int i = 0;
  for (i = 0; i < hantrodec_data.cores; i++) {
    if (hantrodec_data.hwregs[i])
      iounmap((void *) hantrodec_data.hwregs[i]);
    release_mem_region(multicorebase_actual[i], hantrodec_data.iosize[i]);
  }
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_isr
 Description     : interrupt handler

 Return type     : irqreturn_t
------------------------------------------------------------------------------*/
static irqreturn_t hantrodec_isr(void *args) {
  unsigned long flags = 0;
  unsigned int handled = 0;
  int i = 0;
  volatile u8 *hwregs = NULL;

  hantrodec_t *dev = (hantrodec_t *) args;
  u32 irq_status_dec = 0;

  spin_lock_irqsave(&owner_lock, flags);
  IntClearIRQStatus(irq[0], 0);

  for(i=0; i<dev->cores; i++) {
    volatile u8 *hwregs = dev->hwregs[i];

    /* interrupt status register read */
    irq_status_dec = ioread32((void*)(hwregs + HANTRODEC_IRQ_STAT_DEC_OFF));

    if(irq_status_dec & HANTRODEC_DEC_IRQ) {
      /* clear dec IRQ */
      irq_status_dec &= (~HANTRODEC_DEC_IRQ);
      iowrite32(irq_status_dec, (void*)(hwregs + HANTRODEC_IRQ_STAT_DEC_OFF));

      PDEBUG("Hantrodec decoder IRQ received! core %d\n", i);

      atomic_inc(&irq_rx);

      dec_irq |= (1 << i);

      wake_up_interruptible_all(&dec_wait_queue);
      handled++;
    }
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  if(!handled) {
    PDEBUG("Hantrodec IRQ received, but not hantrodec's!\n");
  }

  (void)flags;
  return IRQ_RETVAL(handled);
}

/*------------------------------------------------------------------------------
 Function name   : ResetAsic
 Description     : reset asic

 Return type     :
------------------------------------------------------------------------------*/
void ResetAsic(hantrodec_t * dev) {
  int i, j;
  u32 status;

  for (j = 0; j < dev->cores; j++) {
    status = ioread32((void *)(dev->hwregs[j] + HANTRODEC_IRQ_STAT_DEC_OFF));

    if( status & HANTRODEC_DEC_E) {
      /* abort with IRQ disabled */
      status = HANTRODEC_DEC_ABORT | HANTRODEC_DEC_IRQ_DISABLE;
      iowrite32(status, (void *)(dev->hwregs[j] + HANTRODEC_IRQ_STAT_DEC_OFF));
    }

    if (IS_G1(dev->hw_id[j]))
      /* reset PP */
      iowrite32(0, (void *)(dev->hwregs[j] + HANTRO_IRQ_STAT_PP_OFF));

    for (i = 4; i < dev->iosize[j]; i += 4) {
      iowrite32(0, (void *)(dev->hwregs[j] + i));
    }
  }
}

/*------------------------------------------------------------------------------
 Function name   : dump_regs
 Description     : Dump registers

 Return type     :
------------------------------------------------------------------------------*/
#ifdef HANTRODEC_DEBUG
void dump_regs(hantrodec_t *dev) {
  int i,c;

  PDEBUG("Hantrodec Reg Dump Start\n");
  for(c = 0; c < dev->cores; c++) {
    for(i = 0; i < dev->iosize[c]; i += 4*4) {
      PDEBUG("\toffset %04X: %08X  %08X  %08X  %08X\n", i,
             ioread32(dev->hwregs[c] + i),
             ioread32(dev->hwregs[c] + i + 4),
             ioread32(dev->hwregs[c] + i + 16),
             ioread32(dev->hwregs[c] + i + 24));
    }
  }
  PDEBUG("Hantrodec Reg Dump End\n");
}
#endif

av_unused int RegisterIRQ(i32 irq_offset, IRQHandler isr_handler, i32 flag, const char* name, void* data) {
//_xtos_set_interrupt_handle
  u32 irq_mask = 1 << irq_offset; //irq_offset -> Irq[0] is 3
  u32 enabled_int = 0;
  if ((xthal_get_intenable() & irq_mask) == 0)
  {
    /* Clear MCPU DB2 interrupt before enable */
    xthal_set_intclear(irq_mask);
    /* Assign Interrupt handler */
    xt_set_interrupt_handler(irq_offset, (xt_handler) isr_handler, data);

    *((volatile uint32_t *)0x0280004c) = 0x1; // clear current irq  for slice 0
    *((volatile uint32_t *)0x02800050) = 0x1; // enable dec irq  for slice 0

    xt_ints_on(irq_mask);
  }
  enabled_int = xthal_get_intenable();
  printf("INTs are enabled: 0x%x\n", enabled_int);
  return 0;
}

av_unused void IntEnableIRQ(u32 irq_offset) {
  //_xtos_interrupt_enable
  u32 irq_mask = 1 << irq_offset;
  u32 enabled_int = 0;
  xt_ints_on(irq_mask);

  enabled_int = xthal_get_intenable();
  printf("INTs are enabled 2: 0x%x\n", enabled_int);
}

av_unused void IntDisableIRQ(u32 irq_offset) {
  //_xtos_interrupt_disable
  u32 irq_mask = 1 << irq_offset;
  u32 enabled_int = 0;
  xt_ints_off(irq_mask);
  enabled_int = xthal_get_intenable();
  printf("INTs are disabled 3: 0x%x\n", enabled_int);
}

av_unused void IntClearIRQStatus(u32 irq_offset, u32 type) {
  //xthal_set_intclear
  *((volatile uint32_t *)0x0280004c) = 0x1; // clear current irq for slice 0
  u32 irq_mask = 1 << irq_offset;
  xthal_set_intclear(irq_mask);
}

static inline uint32_t ReadInterruptStatus(void) {
  uint32_t interrupt;
  __asm__ __volatile__("rsr %0, interrupt": "=a"(interrupt));
  return interrupt;
}
//For debug
av_unused u32 IntGetIRQStatus(u32 irq_offset) {
  uint32_t ret_val;
  //ReadInterruptStatus
  ret_val = ReadInterruptStatus();
  //mask other interrupts ?
  ret_val &= (1 << irq_offset);
  // return 1 if requested interrupt is set
  if (ret_val)
    ret_val = 1;
  return 0;
}

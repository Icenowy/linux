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

#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/mod_devicetable.h>
#include "subsys.h"
#include "hantroaxife.h"
#include <asm/irq.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>
#include "kernel_allocator.h"

//vpu devfreq
#include <linux/devfreq.h>
#include <linux/of_device.h>
#include <linux/pm_opp.h>

#include <linux/devfreq-event.h>
#include <linux/export.h>

#include <linux/regulator/consumer.h>
#include "dec_devfreq.h"
#ifdef CONFIG_TH1520_SYSTEM_MONITOR
#include <soc/xuantie/th1520_system_monitor.h>
#endif

#undef PDEBUG
#ifdef HANTRODEC_DEBUG
#  ifdef __KERNEL__
#    define PDEBUG(fmt, args...) printk( KERN_INFO "hantrodec: " fmt, ## args)
#  else
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) pr_debug("hantrodec: " fmt, ## args)
#endif
int debug_pr_devfreq_info = 0;

#define PCI_VENDOR_ID_HANTRO            0x10ee// 0x1ae0//0x16c3
#define PCI_DEVICE_ID_HANTRO_PCI        0x8014//0x001a// 0xabcd

/* Base address DDR register */
#define PCI_DDR_BAR 0

/* Base address got control register */
#define PCI_CONTROL_BAR                 4

/* PCIe hantro driver offset in control register */
#define HANTRO_REG_OFFSET0               0x600000
#define HANTRO_REG_OFFSET1               0x700000

/* TODO(mheikkinen) Implement multicore support. */
struct pci_dev *gDev = NULL;     /* PCI device structure. */
unsigned long gBaseHdwr;         /* PCI base register address (Hardware address) */
unsigned long gBaseDDRHw;        /* PCI base register address (memalloc) */
u32  gBaseLen;                   /* Base register address Length */

/* hantro G1 regs config including dec and pp */
//#define HANTRO_DEC_ORG_REGS             60
//#define HANTRO_PP_ORG_REGS              41

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
#define HANTRODEC_HWBUILD_ID_OFF (309 * 4)

/* Logic module IRQs */
#define HXDEC_NO_IRQ                    -1

#define MAX(a, b)                       (((a) > (b)) ? (a) : (b))

#define DEC_IO_SIZE_MAX                 (MAX(MAX(HANTRO_G2_DEC_REGS, HANTRO_G1_DEC_REGS), HANTRO_VC8000D_REGS) * 4)

/* User should modify these configuration if do porting to own platform. */
/* Please guarantee the base_addr, io_size, dec_irq belong to same core. */

/* Defines use kernel clk cfg or not**/
//#define CLK_CFG
#ifdef CLK_CFG
#define CLK_ID                          "hantrodec_clk"  /*this id should conform with platform define*/
#endif

/* Logic module base address */
#define SOCLE_LOGIC_0_BASE              0x38300000
#define SOCLE_LOGIC_1_BASE              0x38310000

#define VEXPRESS_LOGIC_0_BASE           0xFC010000
#define VEXPRESS_LOGIC_1_BASE           0xFC020000

#define DEC_IO_SIZE_0                   DEC_IO_SIZE_MAX /* bytes */
#define DEC_IO_SIZE_1                   DEC_IO_SIZE_MAX /* bytes */

#define DEC_IRQ_0                       HXDEC_NO_IRQ
#define DEC_IRQ_1                       HXDEC_NO_IRQ

#define IS_G1(hw_id)                    (((hw_id) == 0x6731)? 1 : 0)
#define IS_G2(hw_id)                    (((hw_id) == 0x6732)? 1 : 0)
#define IS_VC8000D(hw_id)               (((hw_id) == 0x8001)? 1 : 0)
#define IS_BIGOCEAN(hw_id)              (((hw_id) == 0xB16D)? 1 : 0)

/* Some IPs HW configuration paramters for APB Filter */
/* Because now such information can't be read from APB filter configuration registers */
/* The fixed value have to be used */
#define VC8000D_NUM_MASK_REG            336
#define VC8000D_NUM_MODE                4
#define VC8000D_MASK_REG_OFFSET         4096
#define VC8000D_MASK_BITS_PER_REG       1

#define VC8000DJ_NUM_MASK_REG           332
#define VC8000DJ_NUM_MODE               1
#define VC8000DJ_MASK_REG_OFFSET        4096
#define VC8000DJ_MASK_BITS_PER_REG      1

#define AV1_NUM_MASK_REG                303
#define AV1_NUM_MODE                    1
#define AV1_MASK_REG_OFFSET             4096
#define AV1_MASK_BITS_PER_REG           1

#define AXIFE_NUM_MASK_REG              144
#define AXIFE_NUM_MODE                  1
#define AXIFE_MASK_REG_OFFSET           4096
#define AXIFE_MASK_BITS_PER_REG         1

#define VC8000D_MAX_CONFIG_LEN          32

#define VC8000D_PM_TIMEOUT              100 /* ms */
/*************************************************************/

/*********************local variable declaration*****************/

static const int DecHwId[] = {
  0x6731, /* G1 */
  0x6732, /* G2 */
  0xB16D, /* BigOcean */
  0x8001  /* VC8000D */
};

unsigned long base_port = -1;
unsigned int pcie = 0;
volatile unsigned char *reg = NULL;
unsigned int reg_access_opt = 0;
unsigned int vcmd = 0;
unsigned long alloc_size = 512;
unsigned long alloc_base = 0x1c0000000;

unsigned long multicorebase[HXDEC_MAX_CORES] = {
  HANTRO_REG_OFFSET0,
  HANTRO_REG_OFFSET1,
  0,
  0
};

int irq[HXDEC_MAX_CORES] = {
  131,
  DEC_IRQ_1,
  -1,
  -1
};

unsigned int iosize[HXDEC_MAX_CORES] = {
  DEC_IO_SIZE_0,
  DEC_IO_SIZE_1,
  -1,
  -1
};

/* Because one core may contain multi-pipeline, so multicore base may be changed */
unsigned long multicorebase_actual[HXDEC_MAX_CORES];

struct subsys_config vpu_subsys[MAX_SUBSYS_NUM];

struct apbfilter_cfg apbfilter_cfg[MAX_SUBSYS_NUM][HW_CORE_MAX];

struct axife_cfg axife_cfg[MAX_SUBSYS_NUM];
int elements = 2;

#ifdef CLK_CFG
struct clk *clk_cfg;
int is_clk_on;
struct timer_list timer;
#endif

/* module_param(name, type, perm) */
//module_param(base_port, ulong, 0);
module_param(pcie, uint, 0);
//module_param_array(irq, int, &elements, 0);
module_param_array(multicorebase, ulong, &elements, 0644);
module_param(reg_access_opt, uint, 0);
module_param(vcmd, uint, 0);
module_param(alloc_base, ulong, 0);
module_param(alloc_size, ulong, 0);


static int hantrodec_major = 0; /* dynamic allocation */
static int hantrodec_minor = 0; /* dynamic allocation */
static struct cdev hantrodec_cdev;
static dev_t hantrodec_devt;
static struct class *hantrodec_class;

static struct dentry *root_debugfs_dir = NULL;

/* here's all the must remember stuff */
typedef struct {
  char *buffer;
  volatile unsigned int iosize[HXDEC_MAX_CORES];
  /* mapped address to different HW cores regs*/
  volatile u8 *hwregs[HXDEC_MAX_CORES][HW_CORE_MAX];
  /* mapped address to different HW cores regs*/
  volatile u8 *apbfilter_hwregs[HXDEC_MAX_CORES][HW_CORE_MAX];
  volatile int irq[HXDEC_MAX_CORES];
  int hw_id[HXDEC_MAX_CORES][HW_CORE_MAX];
  /* Requested client type for given core, used when a subsys has multiple
     decoders, e.g., VC8000D+VC8000DJ+BigOcean */
  int client_type[HXDEC_MAX_CORES];
  int cores;
  struct fasync_struct *async_queue_dec;
  struct fasync_struct *async_queue_pp;
  struct platform_device *pdev;
  struct clk *cclk;
  struct clk *aclk;
  struct clk *pclk;
  char config_buf[VC8000D_MAX_CONFIG_LEN];
  int has_power_domains;
  struct decoder_devfreq devfreq;
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

/* IRQ handler */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
static irqreturn_t hantrodec_isr(int irq, void *dev_id, struct pt_regs *regs);
#else
static irqreturn_t hantrodec_isr(int irq, void *dev_id);
#endif

static u32 dec_regs[HXDEC_MAX_CORES][DEC_IO_SIZE_MAX/4];
static u32 apbfilter_regs[HXDEC_MAX_CORES][DEC_IO_SIZE_MAX/4+1];
/* shadow_regs used to compare whether it's necessary to write to registers */
static u32 shadow_dec_regs[HXDEC_MAX_CORES][DEC_IO_SIZE_MAX/4];

struct semaphore dec_core_sem;
struct semaphore pp_core_sem;

static int dec_irq = 0;
static int pp_irq = 0;

atomic_t irq_rx = ATOMIC_INIT(0);
atomic_t irq_tx = ATOMIC_INIT(0);

static struct file* dec_owner[HXDEC_MAX_CORES];
static struct file* pp_owner[HXDEC_MAX_CORES];
static int CoreHasFormat(const u32 *cfg, int core, u32 format);

/* spinlock_t owner_lock = SPIN_LOCK_UNLOCKED; */
DEFINE_SPINLOCK(owner_lock);

DECLARE_WAIT_QUEUE_HEAD(dec_wait_queue);
DECLARE_WAIT_QUEUE_HEAD(pp_wait_queue);
DECLARE_WAIT_QUEUE_HEAD(hw_queue);
#ifdef CLK_CFG
DEFINE_SPINLOCK(clk_lock);
#endif

#define DWL_CLIENT_TYPE_H264_DEC        1U
#define DWL_CLIENT_TYPE_MPEG4_DEC       2U
#define DWL_CLIENT_TYPE_JPEG_DEC        3U
#define DWL_CLIENT_TYPE_PP              4U
#define DWL_CLIENT_TYPE_VC1_DEC         5U
#define DWL_CLIENT_TYPE_MPEG2_DEC       6U
#define DWL_CLIENT_TYPE_VP6_DEC         7U
#define DWL_CLIENT_TYPE_AVS_DEC         8U
#define DWL_CLIENT_TYPE_RV_DEC          9U
#define DWL_CLIENT_TYPE_VP8_DEC         10U
#define DWL_CLIENT_TYPE_VP9_DEC         11U
#define DWL_CLIENT_TYPE_HEVC_DEC        12U
#define DWL_CLIENT_TYPE_ST_PP           14U
#define DWL_CLIENT_TYPE_H264_MAIN10     15U
#define DWL_CLIENT_TYPE_AVS2_DEC        16U
#define DWL_CLIENT_TYPE_AV1_DEC         17U
#define DWL_CLIENT_TYPE_BO_AV1_DEC      31U

#define BIGOCEANDEC_CFG 1
#define BIGOCEANDEC_AV1_E 5

static core_cfg config;

static void ReadCoreConfig(hantrodec_t *dev) {
  int c, j;
  u32 reg, tmp, mask;

  memset(config.cfg, 0, sizeof(config.cfg));

  for(c = 0; c < dev->cores; c++) {
    for (j = 0; j < HW_CORE_MAX; j++) {
      if (j != HW_VC8000D && j != HW_VC8000DJ && j != HW_BIGOCEAN)
        continue;
      if (!dev->hwregs[c][j])   /* NOT defined core type */
        continue;
      /* Decoder configuration */
      if (IS_G1(dev->hw_id[c][j])) {
        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODEC_SYNTH_CFG * 4));

        tmp = (reg >> DWL_H264_E) & 0x3U;
        if(tmp) pr_info("hantrodec: subsys[%d] has H264\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

        tmp = (reg >> DWL_JPEG_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has JPEG\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

        tmp = (reg >> DWL_HJPEG_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has HJPEG\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

        tmp = (reg >> DWL_MPEG4_E) & 0x3U;
        if(tmp) pr_info("hantrodec: subsys[%d] has MPEG4\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

        tmp = (reg >> DWL_VC1_E) & 0x3U;
        if(tmp) pr_info("hantrodec: subsys[%d] has VC1\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC: 0;

        tmp = (reg >> DWL_MPEG2_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has MPEG2\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

        tmp = (reg >> DWL_VP6_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has VP6\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODEC_SYNTH_CFG_2 * 4));

        /* VP7 and WEBP is part of VP8 */
        mask =  (1 << DWL_VP8_E) | (1 << DWL_VP7_E) | (1 << DWL_WEBP_E);
        tmp = (reg & mask);
        if(tmp & (1 << DWL_VP8_E))
          pr_info("hantrodec: subsys[%d] has VP8\n", c);
        if(tmp & (1 << DWL_VP7_E))
          pr_info("hantrodec: subsys[%d] has VP7\n", c);
        if(tmp & (1 << DWL_WEBP_E))
          pr_info("hantrodec: subsys[%d] has WebP\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC : 0;

        tmp = (reg >> DWL_AVS_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has AVS\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC: 0;

        tmp = (reg >> DWL_RV_E) & 0x03U;
        if(tmp) pr_info("hantrodec: subsys[%d] has RV\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

        /* Post-processor configuration */
        reg = ioread32((void*)(dev->hwregs[c][j] + HANTROPP_SYNTH_CFG * 4));

        tmp = (reg >> DWL_G1_PP_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has PP\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
      } else if((IS_G2(dev->hw_id[c][j]))) {
        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODEC_CFG_STAT * 4));

        tmp = (reg >> DWL_G2_HEVC_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has HEVC\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_HEVC_DEC : 0;

        tmp = (reg >> DWL_G2_VP9_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has VP9\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP9_DEC : 0;

        /* Post-processor configuration */
        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODECPP_SYNTH_CFG * 4));

        tmp = (reg >> DWL_G2_PP_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has PP\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
      } else if((IS_VC8000D(dev->hw_id[c][j])) && config.its_main_core_id[c] < 0) {
        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODEC_SYNTH_CFG * 4));
        pr_info("hantrodec: subsys[%d] swreg[%d] = 0x%08x\n", c, HANTRODEC_SYNTH_CFG, reg);

        tmp = (reg >> DWL_H264_E) & 0x3U;
        if(tmp) pr_info("hantrodec: subsys[%d] has H264\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

        tmp = (reg >> DWL_H264HIGH10_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has H264HIGH10\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

        tmp = (reg >> DWL_AVS2_E) & 0x03U;
        if(tmp) pr_info("hantrodec: subsys[%d] has AVS2\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_AVS2_DEC : 0;

        tmp = (reg >> DWL_JPEG_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has JPEG\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

        tmp = (reg >> DWL_HJPEG_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has HJPEG\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

        tmp = (reg >> DWL_MPEG4_E) & 0x3U;
        if(tmp) pr_info("hantrodec: subsys[%d] has MPEG4\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

        tmp = (reg >> DWL_VC1_E) & 0x3U;
        if(tmp) pr_info("hantrodec: subsys[%d] has VC1\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC: 0;

        tmp = (reg >> DWL_MPEG2_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has MPEG2\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

        tmp = (reg >> DWL_VP6_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has VP6\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODEC_SYNTH_CFG_2 * 4));
        pr_info("hantrodec: subsys[%d] swreg[%d] = 0x%08x\n", c, HANTRODEC_SYNTH_CFG_2, reg);

        /* VP7 and WEBP is part of VP8 */
        mask =  (1 << DWL_VP8_E) | (1 << DWL_VP7_E) | (1 << DWL_WEBP_E);
        tmp = (reg & mask);
        if(tmp & (1 << DWL_VP8_E))
          pr_info("hantrodec: subsys[%d] has VP8\n", c);
        if(tmp & (1 << DWL_VP7_E))
          pr_info("hantrodec: subsys[%d] has VP7\n", c);
        if(tmp & (1 << DWL_WEBP_E))
          pr_info("hantrodec: subsys[%d] has WebP\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC : 0;

        tmp = (reg >> DWL_AVS_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has AVS\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC: 0;

        tmp = (reg >> DWL_RV_E) & 0x03U;
        if(tmp) pr_info("hantrodec: subsys[%d] has RV\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODEC_SYNTH_CFG_3 * 4));
        pr_info("hantrodec: subsys[%d] swreg[%d] = 0x%08x\n", c, HANTRODEC_SYNTH_CFG_3, reg);

        tmp = (reg >> DWL_HEVC_E) & 0x07U;
        if(tmp) pr_info("hantrodec: subsys[%d] has HEVC\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_HEVC_DEC : 0;

        tmp = (reg >> DWL_VP9_E) & 0x07U;
        if(tmp) pr_info("hantrodec: subsys[%d] has VP9\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_VP9_DEC : 0;

        /* Post-processor configuration */
        reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODECPP_CFG_STAT * 4));

        tmp = (reg >> DWL_PP_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has PP\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;

        config.cfg[c] |= 1 << DWL_CLIENT_TYPE_ST_PP;

        if (config.its_aux_core_id[c] >= 0) {
          /* set main_core_id and aux_core_id */
          reg = ioread32((void*)(dev->hwregs[c][j] + HANTRODEC_SYNTH_CFG_2 * 4));

          tmp = (reg >> DWL_H264_PIPELINE_E) & 0x01U;
          if(tmp) pr_info("hantrodec: subsys[%d] has pipeline H264\n", c);
          config.cfg[config.its_aux_core_id[c]] |= tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

          tmp = (reg >> DWL_JPEG_PIPELINE_E) & 0x01U;
          if(tmp) pr_info("hantrodec: subsys[%d] has pipeline JPEG\n", c);
          config.cfg[config.its_aux_core_id[c]] |= tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;
        }
      } else if (IS_BIGOCEAN(dev->hw_id[c][j])) {
        reg = ioread32((void*)(dev->hwregs[c][j] + BIGOCEANDEC_CFG * 4));

        tmp = (reg >> BIGOCEANDEC_AV1_E) & 0x01U;
        if(tmp) pr_info("hantrodec: subsys[%d] has AV1 (BigOcean)\n", c);
        config.cfg[c] |= tmp ? 1 << DWL_CLIENT_TYPE_BO_AV1_DEC : 0;
      }
    }
  }
  memcpy(config.cfg_backup, config.cfg, sizeof(config.cfg));
}

static int CoreHasFormat(const u32 *cfg, int core, u32 format) {
  return (cfg[core] & (1 << format)) ? 1 : 0;
}

int GetDecCore(long core, hantrodec_t *dev, struct file* filp, unsigned long format) {
  int success = 0;
  unsigned long flags;

  spin_lock_irqsave(&owner_lock, flags);
  if(CoreHasFormat(config.cfg, core, format) && dec_owner[core] == NULL /*&& config.its_main_core_id[core] >= 0*/) {
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

int GetDecCoreAny(long *core, hantrodec_t *dev, struct file* filp,
                  unsigned long format) {
  int success = 0;
  long c;

  *core = -1;

  for(c = 0; c < dev->cores; c++) {
    /* a free core that has format */
    if(GetDecCore(c, dev, filp, format)) {
      success = 1;
      *core = c;
      break;
    }
  }

  return success;
}

int GetDecCoreID(hantrodec_t *dev, struct file* filp,
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
    pr_info("blk_ctl: failed to reserve HW regs\n");
    return -EBUSY;
  }

  reg = (volatile u8 *) ioremap_nocache(blk_base, 0x1000);

  if (reg == NULL ) {
    pr_info("blk_ctl: failed to ioremap HW regs\n");
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

long ReserveDecoder(hantrodec_t *dev, struct file* filp, unsigned long format) {
  long core = -1;

  /* reserve a core */
  if (down_interruptible(&dec_core_sem))
    return -ERESTARTSYS;

  /* lock a core that has specific format*/
  if(wait_event_interruptible(hw_queue,
                              GetDecCoreAny(&core, dev, filp, format) != 0 ))
    return -ERESTARTSYS;

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

  dev->client_type[core] = format;
  return core;
}

void ReleaseDecoder(hantrodec_t *dev, long core) {
  u32 status;
  unsigned long flags;

  PDEBUG("ReleaseDecoder %ld\n", core);

  if (dev->client_type[core] == DWL_CLIENT_TYPE_BO_AV1_DEC)
    status = ioread32((void*)(dev->hwregs[core][HW_BIGOCEAN] + BIGOCEAN_IRQ_STAT_DEC_OFF));
  else
    status = ioread32((void*)(dev->hwregs[core][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));

  /* make sure HW is disabled */
  if(status & HANTRODEC_DEC_E) {
    pr_info("hantrodec: DEC[%li] still enabled -> reset\n", core);

    /* abort decoder */
    status |= HANTRODEC_DEC_ABORT | HANTRODEC_DEC_IRQ_DISABLE;
    iowrite32(status, (void*)(dev->hwregs[core][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));
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

  dec_owner[core] = NULL;

  spin_unlock_irqrestore(&owner_lock, flags);

  up(&dec_core_sem);

  wake_up_interruptible_all(&hw_queue);
}

long ReservePostProcessor(hantrodec_t *dev, struct file* filp) {
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

  u32 status = ioread32((void*)(dev->hwregs[core][HW_VC8000D] + HANTRO_IRQ_STAT_PP_OFF));

  /* make sure HW is disabled */
  if(status & HANTRO_PP_E) {
    pr_info("hantrodec: PP[%li] still enabled -> reset\n", core);

    /* disable IRQ */
    status |= HANTRO_PP_IRQ_DISABLE;

    /* disable postprocessor */
    status &= (~HANTRO_PP_E);
    iowrite32(0x10, (void*)(dev->hwregs[core][HW_VC8000D] + HANTRO_IRQ_STAT_PP_OFF));
  }

  spin_lock_irqsave(&owner_lock, flags);

  pp_owner[core] = NULL;

  spin_unlock_irqrestore(&owner_lock, flags);

  up(&pp_core_sem);
}

long ReserveDecPp(hantrodec_t *dev, struct file* filp, unsigned long format) {
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
  if(wait_event_interruptible(hw_queue,
                              GetDecCore(core, dev, filp, format) != 0)) {
    up(&dec_core_sem);
    return -ERESTARTSYS;
  }

  if (down_interruptible(&pp_core_sem)) {
    ReleaseDecoder(dev, core);
    return -ERESTARTSYS;
  }

  spin_lock_irqsave(&owner_lock, flags);
  pp_owner[core] = filp;
  spin_unlock_irqrestore(&owner_lock, flags);

  return core;
}

#ifdef HANTRODEC_DEBUG
static u32 flush_count = 0; /* times of calling of DecFlushRegs */
static u32 flush_regs = 0;  /* total number of registers flushed */
#endif

long DecFlushRegs(hantrodec_t *dev, struct core_desc *core) {
  long ret = 0, i;
#ifdef HANTRODEC_DEBUG
  int reg_wr = 2;
#endif
  u32 id = core->id;
  u32 type = core->type;

  PDEBUG("hantrodec: DecFlushRegs\n");
  PDEBUG("hantrodec: id = %d, type = %d, size = %d, reg_id = %d\n",
                    core->id, core->type, core->size, core->reg_id);

  if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
    type = HW_VC8000DJ;
  if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
    type = HW_BIGOCEAN;

  if (id >= MAX_SUBSYS_NUM ||
      !vpu_subsys[id].base_addr ||
      core->type >= HW_CORE_MAX ||
      !vpu_subsys[id].submodule_hwregs[type])
    return -EINVAL;

  PDEBUG("hantrodec: submodule_iosize = %d\n", vpu_subsys[id].submodule_iosize[type]);

  ret = copy_from_user(dec_regs[id], core->regs, vpu_subsys[id].submodule_iosize[type]);
  if (ret) {
    PDEBUG("copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  if (type == HW_VC8000D || type == HW_BIGOCEAN || type == HW_VC8000DJ) {
    /* write all regs but the status reg[1] to hardware */
    if (reg_access_opt) {
      for(i = 3; i < vpu_subsys[id].submodule_iosize[type]/4; i++) {
        /* check whether register value is updated. */
        if (dec_regs[id][i] != shadow_dec_regs[id][i]) {
          iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id][type] + i*4));
          shadow_dec_regs[id][i] = dec_regs[id][i];
#ifdef HANTRODEC_DEBUG
          reg_wr++;
#endif
        }
      }
    } else {
      for(i = 3; i < vpu_subsys[id].submodule_iosize[type]/4; i++) {
        iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id][type] + i*4));
#ifdef VALIDATE_REGS_WRITE
        if (dec_regs[id][i] != ioread32((void*)(dev->hwregs[id][type] + i*4)))
          pr_info("hantrodec: swreg[%ld]: read %08x != write %08x *\n",
                 i, ioread32((void*)(dev->hwregs[id][type] + i*4)), dec_regs[id][i]);
#endif
      }
#ifdef HANTRODEC_DEBUG
      reg_wr = vpu_subsys[id].submodule_iosize[type]/4 - 1;
#endif
    }

    /* write swreg2 for AV1, in which bit0 is the start bit */
    iowrite32(dec_regs[id][2], (void*)(dev->hwregs[id][type] + 8));
    shadow_dec_regs[id][2] = dec_regs[id][2];

    /* write the status register, which may start the decoder */
    iowrite32(dec_regs[id][1], (void*)(dev->hwregs[id][type] + 4));
    shadow_dec_regs[id][1] = dec_regs[id][1];

#ifdef HANTRODEC_DEBUG
    flush_count++;
    flush_regs += reg_wr;


    PDEBUG("flushed registers on core %d\n", id);
    PDEBUG("%d DecFlushRegs: flushed %d/%d registers (dec_mode = %d, avg %d regs per flush)\n",
           flush_count, reg_wr, flush_regs, dec_regs[id][3]>>27, flush_regs/flush_count);
#endif
  } else {
    /* write all regs but the status reg[1] to hardware */
    for(i = 0; i < vpu_subsys[id].submodule_iosize[type]/4; i++) {
      iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id][type] + i*4));
#ifdef VALIDATE_REGS_WRITE
      if (dec_regs[id][i] != ioread32((void*)(dev->hwregs[id][type] + i*4)))
        pr_info("hantrodec: swreg[%ld]: read %08x != write %08x *\n",
               i, ioread32((void*)(dev->hwregs[id][type] + i*4)), dec_regs[id][i]);
#endif
    }
  }

  return 0;
}


long DecWriteRegs(hantrodec_t *dev, struct core_desc *core)
{
  long ret = 0;
  u32 i = core->reg_id;
  u32 id = core->id;
  u32 type = core->type;

  PDEBUG("hantrodec: DecWriteRegs\n");
  PDEBUG("hantrodec: id = %d, type = %d, size = %d, reg_id = %d\n",
          core->id, core->type, core->size, core->reg_id);

  if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
    type = HW_VC8000DJ;
  if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
    type = HW_BIGOCEAN;

  if (id >= MAX_SUBSYS_NUM ||
      !vpu_subsys[id].base_addr ||
      type >= HW_CORE_MAX ||
      !vpu_subsys[id].submodule_hwregs[type] ||
      (core->size & 0x3) ||
      core->reg_id * 4 + core->size > vpu_subsys[id].submodule_iosize[type])
    return -EINVAL;

  ret = copy_from_user(dec_regs[id], core->regs, core->size);
  if (ret) {
    PDEBUG("copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  for (i = core->reg_id; i < core->reg_id + core->size/4; i++) {
    PDEBUG("hantrodec: write %08x to reg[%d] core %d\n", dec_regs[id][i-core->reg_id], i, id);
    iowrite32(dec_regs[id][i-core->reg_id], (void*)(dev->hwregs[id][type] + i*4));
    if (type == HW_VC8000D)
      shadow_dec_regs[id][i] = dec_regs[id][i-core->reg_id];
  }
  return 0;
}

long DecWriteApbFilterRegs(hantrodec_t *dev, struct core_desc *core)
{
  long ret = 0;
  u32 i = core->reg_id;
  u32 id = core->id;

  PDEBUG("hantrodec: DecWriteApbFilterRegs\n");
  PDEBUG("hantrodec: id = %d, type = %d, size = %d, reg_id = %d\n",
          core->id, core->type, core->size, core->reg_id);

  if (id >= MAX_SUBSYS_NUM ||
      !vpu_subsys[id].base_addr ||
      core->type >= HW_CORE_MAX ||
      !vpu_subsys[id].submodule_hwregs[core->type] ||
      (core->size & 0x3) ||
      core->reg_id * 4 + core->size > vpu_subsys[id].submodule_iosize[core->type] + 4)
    return -EINVAL;

  ret = copy_from_user(apbfilter_regs[id], core->regs, core->size);
  if (ret) {
    PDEBUG("copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  for (i = core->reg_id; i < core->reg_id + core->size/4; i++) {
    PDEBUG("hantrodec: write %08x to reg[%d] core %d\n", dec_regs[id][i-core->reg_id], i, id);
    iowrite32(apbfilter_regs[id][i-core->reg_id], (void*)(dev->apbfilter_hwregs[id][core->type] + i*4));
  }
  return 0;
}

long DecReadRegs(hantrodec_t *dev, struct core_desc *core)
{
  long ret;
  u32 id = core->id;
  u32 i = core->reg_id;
  u32 type = core->type;

  PDEBUG("hantrodec: DecReadRegs\n");
  PDEBUG("hantrodec: id = %d, type = %d, size = %d, reg_id = %d\n",
          core->id, core->type, core->size, core->reg_id);

  if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
    type = HW_VC8000DJ;
  if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
    type = HW_BIGOCEAN;

  if (id >= MAX_SUBSYS_NUM ||
      !vpu_subsys[id].base_addr ||
      type >= HW_CORE_MAX ||
      !vpu_subsys[id].submodule_hwregs[type] ||
      (core->size & 0x3) ||
      core->reg_id * 4 + core->size > vpu_subsys[id].submodule_iosize[type])
    return -EINVAL;

  /* read specific registers from hardware */
  for (i = core->reg_id; i < core->reg_id + core->size/4; i++) {
    dec_regs[id][i-core->reg_id] = ioread32((void*)(dev->hwregs[id][type] + i*4));
    PDEBUG("hantrodec: read %08x from reg[%d] core %d\n", dec_regs[id][i-core->reg_id], i, id);
    if (type == HW_VC8000D)
      shadow_dec_regs[id][i] = dec_regs[id][i];
  }

  /* put registers to user space*/
  ret = copy_to_user(core->regs, dec_regs[id], core->size);
  if (ret) {
    PDEBUG("copy_to_user failed, returned %li\n", ret);
    return -EFAULT;
  }
  return 0;
}

long DecRefreshRegs(hantrodec_t *dev, struct core_desc *core) 
{
  long ret, i;
  u32 id = core->id;
  u32 type = core->type;

  PDEBUG("hantrodec: DecRefreshRegs\n");
  PDEBUG("hantrodec: id = %d, type = %d, size = %d, reg_id = %d\n",
                    core->id, core->type, core->size, core->reg_id);

  if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
    type = HW_VC8000DJ;
  if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
    type = HW_BIGOCEAN;

  if (id >= MAX_SUBSYS_NUM ||
      !vpu_subsys[id].base_addr ||
      type >= HW_CORE_MAX ||
      !vpu_subsys[id].submodule_hwregs[type])
    return -EINVAL;

  PDEBUG("hantrodec: submodule_iosize = %d\n", vpu_subsys[id].submodule_iosize[type]);

  if (!reg_access_opt) {
    for(i = 0; i < vpu_subsys[id].submodule_iosize[type]/4; i++) {
      dec_regs[id][i] = ioread32((void*)(dev->hwregs[id][type] + i*4));
    }
  } else {
    // only need to read swreg1,62(?),63,168,169
#define REFRESH_REG(idx) i = (idx); shadow_dec_regs[id][i] = dec_regs[id][i] = ioread32((void*)(dev->hwregs[id][type] + i*4))
    REFRESH_REG(0);
    REFRESH_REG(1);
    REFRESH_REG(62);
    REFRESH_REG(63);
    REFRESH_REG(168);
    REFRESH_REG(169);
#undef REFRESH_REG
  }

  ret = copy_to_user(core->regs, dec_regs[id], vpu_subsys[id].submodule_iosize[type]);
  if (ret) {
    PDEBUG("copy_to_user failed, returned %li\n", ret);
    return -EFAULT;
  }
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

  PDEBUG("wait_event_interruptible DEC[%d]\n", id);
#ifdef USE_SW_TIMEOUT
  u32 status;
  ret = wait_event_interruptible_timeout(dec_wait_queue, CheckDecIrq(dev, id), msecs_to_jiffies(2000));
  if(ret < 0) {
    PDEBUG("DEC[%d]  wait_event_interruptible interrupted\n", id);
    return -ERESTARTSYS;
  } else if (ret == 0) {
    PDEBUG("DEC[%d]  wait_event_interruptible timeout\n", id);
    status = ioread32((void*)(dev->hwregs[id][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));
    /* check if HW is enabled */
    if(status & HANTRODEC_DEC_E) {
      pr_info("hantrodec: DEC[%d] reset becuase of timeout\n", id);

      /* abort decoder */
      status |= HANTRODEC_DEC_ABORT | HANTRODEC_DEC_IRQ_DISABLE;
      iowrite32(status, (void*)(dev->hwregs[id][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));
    }
  }
#else
  ret = wait_event_interruptible(dec_wait_queue, CheckDecIrq(dev, id));
  if(ret) {
    PDEBUG("DEC[%d]  wait_event_interruptible interrupted\n", id);
    return -ERESTARTSYS;
  }
#endif
  atomic_inc(&irq_tx);

  /* refresh registers */
  return DecRefreshRegs(dev, core);
}

#if 0
long PPFlushRegs(hantrodec_t *dev, struct core_desc *core) {
  long ret = 0;
  u32 id = core->id;
  u32 i;

  /* copy original dec regs to kernal space*/
  ret = copy_from_user(dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
                       core->regs + HANTRO_PP_ORG_FIRST_REG,
                       HANTRO_PP_ORG_REGS*4);
  if (sizeof(void *) == 8) {
    /* copy extended dec regs to kernal space*/
    ret = copy_from_user(dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                         core->regs + HANTRO_PP_EXT_FIRST_REG,
                         HANTRO_PP_EXT_REGS*4);
  }
  if (ret) {
    PDEBUG("copy_from_user failed, returned %li\n", ret);
    return -EFAULT;
  }

  /* write all regs but the status reg[1] to hardware */
  /* both original and extended regs need to be written */
  for(i = HANTRO_PP_ORG_FIRST_REG + 1; i <= HANTRO_PP_ORG_LAST_REG; i++)
    iowrite32(dec_regs[id][i], (void*)(dev->hwregs[id] + i*4));
  if (sizeof(void *) == 8) {
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
  if (sizeof(void *) == 8) {
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
  if (sizeof(void *) == 8) {
    for(i = HANTRO_PP_EXT_FIRST_REG; i <= HANTRO_PP_EXT_LAST_REG; i++)
      dec_regs[id][i] = ioread32((void*)(dev->hwregs[id] + i*4));
  }
  /* put registers to user space*/
  /* put original registers to user space*/
  ret = copy_to_user(core->regs + HANTRO_PP_ORG_FIRST_REG,
                     dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
                     HANTRO_PP_ORG_REGS*4);
  if (sizeof(void *) == 8) {
    /* put extended registers to user space*/
    ret = copy_to_user(core->regs + HANTRO_PP_EXT_FIRST_REG,
                       dec_regs[id] + HANTRO_PP_EXT_FIRST_REG,
                       HANTRO_PP_EXT_REGS * 4);
  }
  if (ret) {
    PDEBUG("copy_to_user failed, returned %li\n", ret);
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

  PDEBUG("wait_event_interruptible PP[%d]\n", id);

  if(wait_event_interruptible(pp_wait_queue, CheckPPIrq(dev, id))) {
    PDEBUG("PP[%d]  wait_event_interruptible interrupted\n", id);
    return -ERESTARTSYS;
  }

  atomic_inc(&irq_tx);

  /* refresh registers */
  return PPRefreshRegs(dev, core);
}
#endif

static int CheckCoreIrq(hantrodec_t *dev, const struct file *filp, int *id) {
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
      } else if(dec_owner[n] == NULL) {
        /* zombie IRQ */
        pr_info("IRQ on core[%d], but no owner!!!\n", n);

        /* reset the wait condition(s) */
        dec_irq &= ~irq_mask;
      }
    }

    spin_unlock_irqrestore(&owner_lock, flags);

    n++; /* next core */
  } while(n < dev->cores);

  return rdy;
}

long WaitCoreReady(hantrodec_t *dev, const struct file *filp, int *id) {
  long ret;
  PDEBUG("wait_event_interruptible CORE\n");
#ifdef USE_SW_TIMEOUT
  u32 i, status;
  ret = wait_event_interruptible_timeout(dec_wait_queue, CheckCoreIrq(dev, filp, id), msecs_to_jiffies(2000));
  if(ret < 0) {
    PDEBUG("CORE  wait_event_interruptible interrupted\n");
    return -ERESTARTSYS;
  } else if (ret == 0) {
    PDEBUG("CORE  wait_event_interruptible timeout\n");
    for(i = 0; i < dev->cores; i++) {
      status = ioread32((void*)(dev->hwregs[i][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));
      /* check if HW is enabled */
      if((status & HANTRODEC_DEC_E) && dec_owner[i] == filp) {
        pr_info("hantrodec: CORE[%d] reset becuase of timeout\n", i);
        *id = i;
        /* abort decoder */
        status |= HANTRODEC_DEC_ABORT | HANTRODEC_DEC_IRQ_DISABLE;
        iowrite32(status, (void*)(dev->hwregs[i][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));
        break;
      }
    }
  }
#else
  ret = wait_event_interruptible(dec_wait_queue, CheckCoreIrq(dev, filp, id));
  if(ret) {
    PDEBUG("CORE[%d] wait_event_interruptible interrupted with 0x%x\n", *id, ret);
    return -ERESTARTSYS;
  }
#endif
  atomic_inc(&irq_tx);

  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_ioctl
 Description     : communication method to/from the user space

 Return type     : long
------------------------------------------------------------------------------*/

static long hantrodec_ioctl(struct file *filp, unsigned int cmd,
                            unsigned long arg) {
  int err = 0;
  long tmp;
  u32 i = 0;
#ifdef CLK_CFG
  unsigned long flags;
#endif
  long ret;
#ifdef HW_PERFORMANCE
  struct timeval *end_time_arg;
#endif

  PDEBUG("ioctl cmd 0x%08x\n", cmd);
  /*
   * extract the type and number bitfields, and don't decode
   * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
   */
  if (_IOC_TYPE(cmd) != HANTRODEC_IOC_MAGIC &&
      _IOC_TYPE(cmd) != HANTRO_IOC_MMU &&
      _IOC_TYPE(cmd) != MEMORY_IOC_MAGIC &&
      _IOC_TYPE(cmd) != HANTRO_VCMD_IOC_MAGIC)
    return -ENOTTY;
  if ((_IOC_TYPE(cmd) == HANTRODEC_IOC_MAGIC &&
      _IOC_NR(cmd) > HANTRODEC_IOC_MAXNR) ||
      (_IOC_TYPE(cmd) == HANTRO_IOC_MMU &&
      _IOC_NR(cmd) > HANTRO_IOC_MMU_MAXNR) ||
      (_IOC_TYPE(cmd) == MEMORY_IOC_MAGIC &&
      _IOC_NR(cmd) > MEMORY_IOC_MAXNR) ||
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
    err = !access_ok((void *) arg, _IOC_SIZE(cmd));
#else
    err = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));
#endif
  else if (_IOC_DIR(cmd) & _IOC_WRITE)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
    err = !access_ok((void *) arg, _IOC_SIZE(cmd));
#else
    err = !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));
#endif

  if (err)
    return -EFAULT;

#ifdef CLK_CFG
  spin_lock_irqsave(&clk_lock, flags);
  if (clk_cfg!=NULL && !IS_ERR(clk_cfg)&&(is_clk_on==0)) {
    printk("turn on clock by user\n");
    if (clk_enable(clk_cfg)) {
      spin_unlock_irqrestore(&clk_lock, flags);
      return -EFAULT;
    } else
      is_clk_on=1;
  }
  spin_unlock_irqrestore(&clk_lock, flags);
  mod_timer(&timer, jiffies + 10*HZ); /*the interval is 10s*/
#endif

  switch (cmd) {
  case HANTRODEC_IOC_CLI: {
    __u32 id;
    __get_user(id, (__u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    decoder_dev_clk_lock();
    disable_irq(hantrodec_data.irq[id]);
    decoder_dev_clk_unlock();
    break;
  }
  case HANTRODEC_IOC_STI: {
    __u32 id;
    __get_user(id, (__u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    decoder_dev_clk_lock();
    enable_irq(hantrodec_data.irq[id]);
    decoder_dev_clk_unlock();
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
    struct regsize_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct regsize_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);

      return -EFAULT;
    }

    if(core.id >= MAX_SUBSYS_NUM /*hantrodec_data.cores*/) {

      return -EFAULT;
    }

    if (core.type == HW_SHAPER) {
      u32 asic_id;
      /* Shaper is configured with l2cache. */
      if (vpu_subsys[core.id].submodule_hwregs[HW_L2CACHE]) {
        decoder_dev_clk_lock();
        asic_id = ioread32((void*)vpu_subsys[core.id].submodule_hwregs[HW_L2CACHE]);
        decoder_dev_clk_unlock();
        switch ((asic_id >> 16) & 0x3) {
        case 1: /* cache only */
          core.size = 0; break;
        case 0: /* cache + shaper */
        case 2: /* shaper only*/
          core.size = vpu_subsys[core.id].submodule_iosize[HW_L2CACHE];
          break;
        default:

          return -EFAULT;
        }
      } else
        core.size = 0;
    } else {
      core.size = vpu_subsys[core.id].submodule_iosize[core.type];
      if (core.type == HW_VC8000D && !core.size &&
          vpu_subsys[core.id].submodule_hwregs[HW_VC8000DJ]) {
        /* If VC8000D doesn't exists, while VC8000DJ exists, return VC8000DJ. */
        core.size = vpu_subsys[core.id].submodule_iosize[HW_VC8000DJ];
      }
    }
    copy_to_user((u32 *) arg, &core, sizeof(struct regsize_desc));


    return 0;
  }
  case HANTRODEC_IOC_MC_OFFSETS: {
    tmp = copy_to_user((unsigned long *) arg, multicorebase_actual, sizeof(multicorebase_actual));
    if (err) {
      PDEBUG("copy_to_user failed, returned %li\n", tmp);
      return -EFAULT;
    }
    break;
  }
  case HANTRODEC_IOC_MC_CORES:
    __put_user(hantrodec_data.cores, (unsigned int *) arg);
    PDEBUG("hantrodec_data.cores=%d\n", hantrodec_data.cores);
    break;
  case HANTRODEC_IOCS_DEC_PUSH_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }
    decoder_dev_clk_lock();
    ret = DecFlushRegs(&hantrodec_data, &core);
    decoder_dev_clk_unlock();
    return ret;
  }
  case HANTRODEC_IOCS_DEC_WRITE_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }
    decoder_dev_clk_lock();
    ret = DecWriteRegs(&hantrodec_data, &core);
    decoder_dev_clk_unlock();
    return ret;
  }
  case HANTRODEC_IOCS_DEC_WRITE_APBFILTER_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return DecWriteApbFilterRegs(&hantrodec_data, &core);
  }
  case HANTRODEC_IOCS_PP_PUSH_REG: {
#if 0
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    PPFlushRegs(&hantrodec_data, &core);
#else
    return EINVAL;
#endif
  }
  case HANTRODEC_IOCS_DEC_PULL_REG: {
	printk("%s:case HANTRODEC_IOCS_DEC_PULL_REG\n",__func__);
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

	printk("%s:return DecRefreshRegs!\n",__func__);
    decoder_dev_clk_lock();
    ret =  DecRefreshRegs(&hantrodec_data, &core);
    decoder_dev_clk_unlock();
    return ret;
  }
  case HANTRODEC_IOCS_DEC_READ_REG: {
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }
    decoder_dev_clk_lock();
    ret =  DecReadRegs(&hantrodec_data, &core);
    decoder_dev_clk_unlock();
    return ret;
  }
  case HANTRODEC_IOCS_PP_PULL_REG: {
#if 0
    struct core_desc core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return PPRefreshRegs(&hantrodec_data, &core);
#else
    return EINVAL;
#endif
  }
  case HANTRODEC_IOCH_DEC_RESERVE: {
    u32 format = 0;
    __get_user(format, (unsigned long *)arg);
    PDEBUG("Reserve DEC core, format = %li\n", format);
    decoder_dev_clk_lock();
    ret =  ReserveDecoder(&hantrodec_data, filp, format);
    decoder_dev_clk_unlock();
    return ret;
  }
  case HANTRODEC_IOCT_DEC_RELEASE: {
    u32 core = 0;
    __get_user(core, (unsigned long *)arg);
    if(core >= hantrodec_data.cores || dec_owner[core] != filp) {
      PDEBUG("bogus DEC release, core = %li\n", core);
      return -EFAULT;
    }

    PDEBUG("Release DEC, core = %li\n", core);
    decoder_dev_clk_lock();
    ReleaseDecoder(&hantrodec_data, core);
    decoder_dev_clk_unlock();
    break;
  }
  case HANTRODEC_IOCQ_PP_RESERVE:
#if 0
    return ReservePostProcessor(&hantrodec_data, filp);
#else
    return EINVAL;
#endif
  case HANTRODEC_IOCT_PP_RELEASE: {
#if 0
    if(arg != 0 || pp_owner[arg] != filp) {
      PDEBUG("bogus PP release %li\n", arg);
      return -EFAULT;
    }

    ReleasePostProcessor(&hantrodec_data, arg);
    break;
#else
    return EINVAL;
#endif
  }
  case HANTRODEC_IOCX_DEC_WAIT: {
    struct core_desc core;

    /* get registers from user space */
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return WaitDecReadyAndRefreshRegs(&hantrodec_data, &core);
  }
  case HANTRODEC_IOCX_PP_WAIT: {
#if 0
    struct core_desc core;

    /* get registers from user space */
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_desc));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    return WaitPPReadyAndRefreshRegs(&hantrodec_data, &core);
#else
    return EINVAL;
#endif
  }
  case HANTRODEC_IOCG_CORE_WAIT: {
    int id;
    tmp = WaitCoreReady(&hantrodec_data, filp, &id);
    __put_user(id, (int *) arg);
    return tmp;
  }
  case HANTRODEC_IOX_ASIC_ID: {
    struct core_param core;

    /* get registers from user space*/
    tmp = copy_from_user(&core, (void*)arg, sizeof(struct core_param));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    if (core.id >= MAX_SUBSYS_NUM /*hantrodec_data.cores*/ ||
       ((core.type == HW_VC8000D || core.type == HW_VC8000DJ) &&
        !vpu_subsys[core.id].submodule_iosize[core.type == HW_VC8000D] &&
        !vpu_subsys[core.id].submodule_iosize[core.type == HW_VC8000DJ]) ||
       ((core.type != HW_VC8000D && core.type != HW_VC8000DJ) &&
        !vpu_subsys[core.id].submodule_iosize[core.type])) {
      return -EFAULT;
    }

    core.size = vpu_subsys[core.id].submodule_iosize[core.type];
    decoder_dev_clk_lock();
    if (vpu_subsys[core.id].submodule_hwregs[core.type])
      core.asic_id = ioread32((void*)hantrodec_data.hwregs[core.id][core.type]);
    else if (core.type == HW_VC8000D &&
             hantrodec_data.hwregs[core.id][HW_VC8000DJ]) {
      core.asic_id = ioread32((void*)hantrodec_data.hwregs[core.id][HW_VC8000DJ]);
    } else
      core.asic_id = 0;
    decoder_dev_clk_unlock();
    copy_to_user((u32 *) arg, &core, sizeof(struct core_param));

    return 0;
  }
  case HANTRODEC_IOCG_CORE_ID: {
    u32 format = 0;
    __get_user(format, (unsigned long *)arg);

    PDEBUG("Get DEC Core_id, format = %li\n", format);
    return GetDecCoreID(&hantrodec_data, filp, format);
  }
  case HANTRODEC_IOX_ASIC_BUILD_ID: {
    u32 id, hw_id;
    __get_user(id, (u32*)arg);

    if(id >= hantrodec_data.cores) {
      return -EFAULT;
    }
    if (hantrodec_data.hwregs[id][HW_VC8000D] ||
        hantrodec_data.hwregs[id][HW_VC8000DJ]) {
      volatile u8 *hwregs;
      decoder_dev_clk_lock();
      /* VC8000D first if it exists, otherwise VC8000DJ. */
      if (hantrodec_data.hwregs[id][HW_VC8000D])
        hwregs = hantrodec_data.hwregs[id][HW_VC8000D];
      else
        hwregs = hantrodec_data.hwregs[id][HW_VC8000DJ];
      hw_id = ioread32((void*)hwregs);
      if (IS_G1(hw_id >> 16) || IS_G2(hw_id >> 16) ||
          (IS_VC8000D(hw_id >> 16) && ((hw_id & 0xFFFF) == 0x6010)))
        __put_user(hw_id, (u32 *) arg);
      else {
        hw_id = ioread32((void*)(hwregs + HANTRODEC_HW_BUILD_ID_OFF));
        __put_user(hw_id, (u32 *) arg);
      }
    } else if (hantrodec_data.hwregs[id][HW_BIGOCEAN]) {
      hw_id = ioread32((void*)(hantrodec_data.hwregs[id][HW_BIGOCEAN]));
      if (IS_BIGOCEAN(hw_id >> 16))
        __put_user(hw_id, (u32 *) arg);
      else {
        decoder_dev_clk_unlock();
        return -EFAULT;
      }
    }
    decoder_dev_clk_unlock();
    return 0;
  }
  case HANTRODEC_DEBUG_STATUS: {
    pr_info("hantrodec: dec_irq     = 0x%08x \n", dec_irq);
    pr_info("hantrodec: pp_irq      = 0x%08x \n", pp_irq);

    pr_info("hantrodec: IRQs received/sent2user = %d / %d \n",
           atomic_read(&irq_rx), atomic_read(&irq_tx));

    for (tmp = 0; tmp < hantrodec_data.cores; tmp++) {
      pr_info("hantrodec: dec_core[%li] %s\n",
             tmp, dec_owner[tmp] == NULL ? "FREE" : "RESERVED");
      pr_info("hantrodec: pp_core[%li]  %s\n",
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
    decoder_dev_clk_lock();
    hantrodec_isr(0, &hantrodec_data);
    decoder_dev_clk_unlock();
    return 0;
  }
  case HANTRODEC_IOC_APBFILTER_CONFIG: {
    struct apbfilter_cfg tmp_apbfilter;

    /* get registers from user space*/
    tmp = copy_from_user(&tmp_apbfilter, (void*)arg, sizeof(struct apbfilter_cfg));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    if(tmp_apbfilter.id >= MAX_SUBSYS_NUM || tmp_apbfilter.type >= HW_CORE_MAX) {
      return -EFAULT;
    }

    apbfilter_cfg[tmp_apbfilter.id][tmp_apbfilter.type].id = tmp_apbfilter.id;
    apbfilter_cfg[tmp_apbfilter.id][tmp_apbfilter.type].type = tmp_apbfilter.type;

    memcpy(&tmp_apbfilter, &(apbfilter_cfg[tmp_apbfilter.id][tmp_apbfilter.type]), sizeof(struct apbfilter_cfg));

    copy_to_user((u32 *) arg, &tmp_apbfilter, sizeof(struct apbfilter_cfg));

    return 0;
  }
  case HANTRODEC_IOC_AXIFE_CONFIG: {
    struct axife_cfg tmp_axife;

    /* get registers from user space*/
    tmp = copy_from_user(&tmp_axife, (void*)arg, sizeof(struct axife_cfg));
    if (tmp) {
      PDEBUG("copy_from_user failed, returned %li\n", tmp);
      return -EFAULT;
    }

    if(tmp_axife.id >= MAX_SUBSYS_NUM) {
      return -EFAULT;
    }

    axife_cfg[tmp_axife.id].id = tmp_axife.id;

    memcpy(&tmp_axife, &(axife_cfg[tmp_axife.id]), sizeof(struct axife_cfg));

    copy_to_user((u32 *) arg, &tmp_axife, sizeof(struct axife_cfg));

    return 0;
  }
  default: {
    if(_IOC_TYPE(cmd) == HANTRO_IOC_MMU) {
      volatile u8* mmu_hwregs[MAX_SUBSYS_NUM][2];
      for (i = 0; i < MAX_SUBSYS_NUM; i++ ) {
         mmu_hwregs[i][0] = hantrodec_data.hwregs[i][HW_MMU];
         mmu_hwregs[i][1] = hantrodec_data.hwregs[i][HW_MMU_WR];
      }
      long retval = MMUIoctl(cmd, filp, arg, mmu_hwregs);
      return retval;
    } else if (_IOC_TYPE(cmd) == HANTRO_VCMD_IOC_MAGIC) {
      return (hantrovcmd_ioctl(filp, cmd, arg));
    } else if (_IOC_TYPE(cmd) == MEMORY_IOC_MAGIC) {
      return (allocator_ioctl(filp, cmd, arg));
    }
    return -ENOTTY;
  }
  }
  return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_open
 Description     : open method

 Return type     : int
------------------------------------------------------------------------------*/

static int hantrodec_open(struct inode *inode, struct file *filp) {
  PDEBUG("dev opened\n");
  if (vcmd)
    hantrovcmd_open(inode, filp);

  allocator_open(inode, filp);

  return 0;
}
/*------------------------------------------------------------------------------
 Function name   : hantrodec_flush
 Description     : wait working hardware stop

 Return type     : int
------------------------------------------------------------------------------*/

static int hantrodec_flush(struct file *filp, fl_owner_t id)
{
  if(vcmd)
    return hantrovcmd_flush(filp,id);
  else
    return 0;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_release
 Description     : Release driver

 Return type     : int
------------------------------------------------------------------------------*/

static int hantrodec_release(struct inode *inode, struct file *filp) {
  int n;
  hantrodec_t *dev = &hantrodec_data;

  PDEBUG("closing ...\n");

  if (vcmd) {
    hantrovcmd_release(inode, filp);
    MMURelease(filp, hantrodec_data.hwregs[0][HW_MMU]);
    allocator_release(inode, filp);
    return 0;
  }

  for(n = 0; n < dev->cores; n++) {
    if(dec_owner[n] == filp) {
      PDEBUG("releasing dec core %i lock\n", n);
      ReleaseDecoder(dev, n);
    }
  }

  for(n = 0; n < 1; n++) {
    if(pp_owner[n] == filp) {
      PDEBUG("releasing pp core %i lock\n", n);
      ReleasePostProcessor(dev, n);
    }
  }

  MMURelease(filp, hantrodec_data.hwregs[0][HW_MMU]);
  
  allocator_release(inode, filp);

  PDEBUG("closed\n");
  return 0;
}

#ifdef CLK_CFG
void hantrodec_disable_clk(unsigned long value) {
  unsigned long flags;
  /*entering this function means decoder is idle over expiry.So disable clk*/
  if (clk_cfg!=NULL && !IS_ERR(clk_cfg)) {
    spin_lock_irqsave(&clk_lock, flags);
    if (is_clk_on==1) {
      clk_disable(clk_cfg);
      is_clk_on = 0;
      pr_info("turned off hantrodec clk\n");
    }
    spin_unlock_irqrestore(&clk_lock, flags);
  }
}
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 4, 0)
static pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (!pfn_valid(pfn)) {
		return pgprot_noncached(vma_prot);
	} else if (file->f_flags & O_SYNC) {
		return pgprot_writecombine(vma_prot);
	}

	return vma_prot;
}
#endif

static int mmap_cmdbuf_mem(struct file *file, struct vm_area_struct *vma)
{
   size_t size = vma->vm_end - vma->vm_start;
	 phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

	/* Does it even fit in phys_addr_t? */
	if (offset >> PAGE_SHIFT != vma->vm_pgoff)
		return -EINVAL;

	/* It's illegal to wrap around the end of the physical address space. */
	if (offset + (phys_addr_t)size - 1 < offset)
		return -EINVAL;


	vma->vm_page_prot = phys_mem_access_prot(file, vma->vm_pgoff,
						 size,
						 vma->vm_page_prot);

	/* Remap-pfn-range will mark the range VM_IO */
	if (remap_pfn_range(vma,
			    vma->vm_start,
			    vma->vm_pgoff,
			    size,
			    vma->vm_page_prot)) {
		return -EAGAIN;
	}

	return 0; 
}

static int mmap_mem(struct file *file, struct vm_area_struct *vma)
{
   size_t size = vma->vm_end - vma->vm_start;
   phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

   if (hantro_cmdbuf_range(offset,size)){
      return mmap_cmdbuf_mem(file,vma); 
   }else{
      return allocator_mmap(file,vma);
   }
}

/* VFS methods */
static struct file_operations hantrodec_fops = {
  .owner = THIS_MODULE,
  .open = hantrodec_open,
  .flush = hantrodec_flush,
  .release = hantrodec_release,
  .unlocked_ioctl = hantrodec_ioctl,
  .mmap = mmap_mem,
  .fasync = NULL
};

static int PcieInit(void) {
  int i;

  gDev = pci_get_device(PCI_VENDOR_ID_HANTRO, PCI_DEVICE_ID_HANTRO_PCI, gDev);
  if (NULL == gDev) {
    pr_info("Init: Hardware not found.\n");
    goto out;
  }

  if (0 > pci_enable_device(gDev)) {
    pr_info("PcieInit: Device not enabled.\n");
    goto out;
  }

  gBaseHdwr = pci_resource_start (gDev, PCI_CONTROL_BAR);
  if (0 == gBaseHdwr) {
    pr_info("PcieInit: Base Address not set.\n");
    goto out_pci_disable_device;
  }
  pr_info("Base hw val 0x%X\n", (unsigned int)gBaseHdwr);

  gBaseLen = pci_resource_len (gDev, PCI_CONTROL_BAR);
  pr_info("Base hw len 0x%X\n", (unsigned int)gBaseLen);

  for (i = 0; i < MAX_SUBSYS_NUM; i++) {
    if (vpu_subsys[i].base_addr) {
      vpu_subsys[i].base_addr += gBaseHdwr;
      multicorebase[i] += gBaseHdwr;
    }
  }

  gBaseDDRHw = pci_resource_start (gDev, PCI_DDR_BAR);
  if (0 == gBaseDDRHw) {
    pr_info("PcieInit: Base Address not set.\n");
    goto out_pci_disable_device;
  }
  pr_info("Base memory val 0x%llx\n", (unsigned int)gBaseDDRHw);

  gBaseLen = pci_resource_len (gDev, PCI_DDR_BAR);
  pr_info("Base memory len 0x%x\n", (unsigned int)gBaseLen);

  return 0;

out_pci_disable_device:
  pci_disable_device(gDev);

out:
  return -1;
}

static void dump_vpu_subsys(struct subsys_config *cfg)
{
	int i;
	pr_info("lucz: dumping subsys_config[0]\n");
	pr_info("  base_addr=0x%llx\n", cfg->base_addr);
	if (cfg->base_addr == 0) {
		pr_info("  base_addr=0, not dump any more\n");
		return;
	}
	pr_info("  irq=%d\n", cfg->irq);
	pr_info("  subsys_type=%u\n", cfg->subsys_type);
	pr_info("  submodule_offset=");
	for (i = 0; i < HW_CORE_MAX; i++) {
		pr_info("    0x%x,", cfg->submodule_offset[i]);
	}
	pr_info("\n");
	pr_info("  submodule_iosize=");
	for (i = 0; i < HW_CORE_MAX; i++) {
		pr_info("    %d,", cfg->submodule_iosize[i]);
	}
	pr_info("\n");
	pr_info("  submodule_hwregs=");
	for (i = 0; i < HW_CORE_MAX; i++) {
		pr_info("    %p,", cfg->submodule_hwregs[i]);
	}
	pr_info("\n");
	pr_info("  has_apbfilter=");
	for (i = 0; i < HW_CORE_MAX; i++) {
		pr_info("    %d,", cfg->has_apbfilter[i]);
	}
	pr_info("\n");
}

static ssize_t decoder_config_write(struct file *filp,
				const char __user *userbuf,
				size_t count, loff_t *ppos)
{
	hantrodec_t *dev = &hantrodec_data;
	unsigned long value;
	int ret;

	if (count > VC8000D_MAX_CONFIG_LEN)
		count = VC8000D_MAX_CONFIG_LEN;
	else if (count <= 2)
		return 0;

	ret = copy_from_user(dev->config_buf, userbuf, count);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	//pr_info("hantrodec config: %s\n", dev->config_buf);
	switch (dev->config_buf[0]) {
		case 'd':
			value = simple_strtoul(&(dev->config_buf[1]), NULL, 10);
			pm_runtime_set_autosuspend_delay(&dev->pdev->dev, value);
			pr_info("Set pm runtime auto suspend delay to %ldms\n", value);
			break;
		case 'p':
			if (strncmp(&(dev->config_buf[0]),"pfreq-en",8) == 0)
			{
				debug_pr_devfreq_info = 1;
			}
			else if (strncmp(&(dev->config_buf[0]),"pfreq-dis",9) == 0)
			{
				debug_pr_devfreq_info = 0;
			}
			pr_info("cmd %s set debug_pr_devfreq_info %ld \n",&(dev->config_buf[0]),debug_pr_devfreq_info);
			break;
		default:
			printk(KERN_WARNING "Unsupported config!\n");
	}

out:
	return ret < 0 ? ret : count;
}

static ssize_t decoder_config_read(struct file *filp,
				char __user *userbuf,
				size_t count, loff_t *ppos)
{
	hantrodec_t *dev = &hantrodec_data;
	memset(dev->config_buf, 0, VC8000D_MAX_CONFIG_LEN);
	return 0;
}

static const struct file_operations decoder_debug_ops = {
	.write	= decoder_config_write,
	.read	= decoder_config_read,
	.open	= simple_open,
	.llseek	= generic_file_llseek,
};

static int decoder_add_debugfs(struct platform_device *pdev)
{
	root_debugfs_dir = debugfs_create_dir("vc8000d",NULL);
	if (!root_debugfs_dir) {
		dev_err(&pdev->dev, "Failed to create vc8000d debugfs\n");
		return -EINVAL;
	}

  dev_info(&pdev->dev, "Create vc8000d debugfs.\n");

	debugfs_create_file("config", 0600, root_debugfs_dir,
		&hantrodec_data, &decoder_debug_ops);
	return 0;
}

/*------------------------------------------------------------
platform register

------------------------------------------------------------*/

static const struct of_device_id isp_of_match[] = {
	{ .compatible = "thead,light-vc8000d",  },
	{ .compatible = "xuantie,th1520-vc8000d",  },
	{ /* sentinel */  },
};


static int check_power_domain(void)
{
	struct device_node *dn = NULL;
	struct property *info = NULL;
	dn = of_find_node_by_name(NULL, "vdec");
	if (dn != NULL)
		info = of_find_property(dn, "power-domains", NULL);
	pr_info("%s, %d: power gating is %s\n", __func__, __LINE__,
		(info == NULL) ? "disabled" : "enabled");
	return (info == NULL) ? 0 : 1;
}

static int decoder_runtime_suspend(struct device *dev)
{
	hantrodec_t *decdev = &hantrodec_data;

	PDEBUG("%s, %d: Disable clock\n", __func__, __LINE__);

	clk_disable_unprepare(decdev->cclk);
	clk_disable_unprepare(decdev->aclk);
	clk_disable_unprepare(decdev->pclk);
	//decoder_devfreq_set_rate(dev); //for the last set rate request,may not handled in cmd
	decoder_devfreq_suspend(decoder_get_devfreq_priv_data());
	return 0;
}

bool dec_power_domains_chk_stay_on = 0;
static bool decoder_check_power_stay_on(struct device *dev,void* mmu_hwregs)
{
  /*check state of power if not powered off,we preferd MMU reg to check*/
  if (hantrodec_data.hwregs[0][HW_MMU])
    return MMU_CheckPowerStayOn(mmu_hwregs);
  else
    return 0;
}

static int decoder_runtime_resume(struct device *dev)
{
	hantrodec_t *decdev = &hantrodec_data;
	int ret;
	bool dec_power_domains_stay_on_flag = 0;

	ret = clk_prepare_enable(decdev->cclk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable core clock\n");
		return ret;
	}

	ret = clk_prepare_enable(decdev->aclk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable axi clock\n");
		clk_disable_unprepare(decdev->cclk);
		return ret;
	}

	ret = clk_prepare_enable(decdev->pclk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable apb clock\n");
		clk_disable_unprepare(decdev->cclk);
		clk_disable_unprepare(decdev->aclk);
		return ret;
	}

	if (hantrodec_data.has_power_domains) {
		if (hantrodec_data.hwregs[0][HW_MMU]) {
			int i;
			volatile u8* mmu_hwregs[MAX_SUBSYS_NUM][2];
			for (i = 0; i < MAX_SUBSYS_NUM; i++ ) {
				mmu_hwregs[i][0] = hantrodec_data.hwregs[i][HW_MMU];
				mmu_hwregs[i][1] = hantrodec_data.hwregs[i][HW_MMU_WR];
			}
			if(dec_power_domains_chk_stay_on) {
				dec_power_domains_stay_on_flag = decoder_check_power_stay_on(dev,mmu_hwregs);
			}
			if(!dec_power_domains_stay_on_flag)
				MMURestore(mmu_hwregs);
		}
		if(!dec_power_domains_stay_on_flag)
		{
			hantrovcmd_reset(false);
		}
		else
			pr_info("%s, %d,hantrovcmd not need reset\n",__func__, __LINE__);
	}

	PDEBUG("%s, %d: Enabled clock  %d\n", __func__, __LINE__);
	decoder_devfreq_resume(decoder_get_devfreq_priv_data());
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int decoder_suspend(struct device *dev)
{
	pr_info("%s, %d: enter state %d\n", __func__, __LINE__,pm_suspend_target_state);
	hantrovcmd_suspend_record();
	/*pm_runtime_force_suspend will check current clk state*/

	PDEBUG("  suspend pm_count %ld\n",atomic_read(&dev->power.usage_count));
	return pm_runtime_force_suspend(dev);

}

static int decoder_resume(struct device *dev)
{
	int ret;
	if(pm_suspend_target_state != PM_SUSPEND_MEM) /**power domains stays on if  is suspend to disk **/
		dec_power_domains_chk_stay_on = 1;

	ret = pm_runtime_force_resume(dev);

	dec_power_domains_chk_stay_on = 0;

	if (ret < 0)
		return ret;

	ret = hantrovcmd_resume_start();

	pr_info("%s, %d: exit resume pm_count %ld\n", __func__, __LINE__,atomic_read(&dev->power.usage_count));
	return ret;
}
#endif

/******************************************************************************\
******************************* VPU Devfreq support START***********************
\******************************************************************************/
unsigned int g_cur_devfreq = 594000000;

static void decoder_devfreq_update_utilization(struct decoder_devfreq *devfreq)
{
    ktime_t now, last;
    ktime_t busy;
    now = ktime_get();
    last = devfreq->time_last_update;

    if (devfreq->busy_count > 0) {
          busy = ktime_sub(now, last);
          devfreq->busy_time += busy;
          #ifndef CONFIG_PM_DEVFREQ
            devfreq->based_maxfreq_last_busy_t = busy;
          #else
          if(devfreq->max_freq)
              devfreq->based_maxfreq_last_busy_t = busy/(devfreq->max_freq/devfreq->cur_devfreq);
          else
              devfreq->based_maxfreq_last_busy_t = busy;
          #endif
          devfreq->based_maxfreq_busy_time +=  devfreq->based_maxfreq_last_busy_t;
          devfreq->busy_record_count++;
    }
    else
    {
      if(devfreq->busy_time > 0)  //if first time in not recorded busy time,ignore idle time.
          devfreq->idle_time += ktime_sub(now, last);
    }
    devfreq->time_last_update = now;
}

static void decoder_devfreq_reset(struct decoder_devfreq *devfreq)
{
    devfreq->busy_time = 0;
    devfreq->idle_time = 0;
    devfreq->time_last_update = ktime_get();
}
void decoder_devfreq_reset_profile_record(struct decoder_devfreq *devfreq)
{
    devfreq->based_maxfreq_busy_time = 0;
    devfreq->busy_record_count = 0;
}

void decoder_devfreq_record_busy(struct decoder_devfreq *devfreq)
{
    unsigned long irqflags;
    int busy_count;
    if (!devfreq)
      return;
    //when devfreq not enabled,need into record time also.
    decoder_dev_clk_lock();
    spin_lock_irqsave(&devfreq->lock, irqflags);
    busy_count = devfreq->busy_count;
    PDEBUG("record_busy:busy_count = %d\n",busy_count);
    if(devfreq->busy_count > 0)
    { 
        devfreq->busy_count++;
        spin_unlock_irqrestore(&devfreq->lock, irqflags);
        decoder_dev_clk_unlock();
        return;
    }

    decoder_devfreq_update_utilization(devfreq);

    devfreq->busy_count++;

    spin_unlock_irqrestore(&devfreq->lock, irqflags);

    if(!busy_count)
        decoder_devfreq_set_rate(&hantrodec_data.pdev->dev);

    decoder_dev_clk_unlock();
}

void decoder_devfreq_record_idle(struct decoder_devfreq *devfreq)
{
    unsigned long irqflags;

    if (!devfreq)
      return;
    spin_lock_irqsave(&devfreq->lock, irqflags);
    PDEBUG("record_idle:busy_count = %d\n",devfreq->busy_count);
    if(devfreq->busy_count > 1)
    { 
      devfreq->busy_count--;
      spin_unlock_irqrestore(&devfreq->lock, irqflags);
      return;
    }

    decoder_devfreq_update_utilization(devfreq);

    WARN_ON(--devfreq->busy_count < 0);

    spin_unlock_irqrestore(&devfreq->lock, irqflags);
#ifdef DEV_FREQ_DEBUG    
    unsigned long busy_time,idle_time;
    busy_time = ktime_to_us(devfreq->busy_time);
    idle_time = ktime_to_us(devfreq->idle_time);
    pr_info("busy %lu idle %lu %lu %%  \n",
        busy_time,idle_time,
        busy_time / ( (idle_time+busy_time) / 100) );
#endif
}
struct decoder_devfreq * decoder_get_devfreq_priv_data(void)
{
    return &hantrodec_data.devfreq;
}
/* only reset record time now */
int decoder_devfreq_resume(struct decoder_devfreq *devfreq)
{
    unsigned long irqflags;

    if (!devfreq->df)
        return 0;

    spin_lock_irqsave(&devfreq->lock, irqflags);
    devfreq->busy_count = 0;//need reset avoid up
    decoder_devfreq_reset(devfreq);

    spin_unlock_irqrestore(&devfreq->lock, irqflags);

    return devfreq_resume_device(devfreq->df);
}

int decoder_devfreq_suspend(struct decoder_devfreq *devfreq)
{
    if (!devfreq->df)
        return 0;
    wake_up_all(&devfreq->target_freq_wait_queue);
    return devfreq_suspend_device(devfreq->df);
}

void decoder_dev_clk_lock(void)
{
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
    if(!devfreq->df)
       return;
    mutex_lock(&devfreq->clk_mutex);
}

void decoder_dev_clk_unlock(void)
{
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
    if(!devfreq->df)
       return;
    mutex_unlock(&devfreq->clk_mutex);
}


bool hantrovcmd_devfreq_check_state(void);

/* set rate need clk disabled,so carefully calling this function
 * which will disabled clk
*/
int decoder_devfreq_set_rate(struct device * dev)
{
    int ret;
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
    hantrodec_t *decdev = &hantrodec_data;
    if (!devfreq->df)
        return 0;
    if(!devfreq->update_freq_flag)
        return 0;
    if(debug_pr_devfreq_info)
        pr_info("start set rate %ldMHz \n",devfreq->next_target_freq/1000/1000);
    if( !hantrovcmd_devfreq_check_state() ) {
        pr_info("devfreq check state not ok\n");
        return 0;
    }
    clk_disable_unprepare(decdev->cclk);
    ret = dev_pm_opp_set_rate(dev, devfreq->next_target_freq);
    if(ret) {
        pr_err("set rate %ld MHz failed \n",devfreq->next_target_freq/1000/1000);
    } else {
        devfreq->cur_devfreq = devfreq->next_target_freq;
    }
    devfreq->update_freq_flag = false;
    wake_up_all(&devfreq->target_freq_wait_queue);
    ret = clk_prepare_enable(decdev->cclk);

    if(debug_pr_devfreq_info)
        pr_info("finished set rate \n");
    if (ret < 0) {
        dev_err(dev, "could not prepare or enable core clock\n");
        return ret;
    }
    return 0;
}

static int decoder_devfreq_target(struct device * dev, unsigned long *freq, u32 flags)
{
    int ret;
    struct dev_pm_opp *opp;
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
    opp = devfreq_recommended_opp(dev, freq, flags);
    if (IS_ERR(opp)) {
        dev_info(dev, "Failed to find opp for %lu Hz\n", *freq);
        return PTR_ERR(opp);
    }
    dev_pm_opp_put(opp);
    
    devfreq->next_target_freq = *freq;
    if(*freq != devfreq->cur_devfreq)
    {
        devfreq->update_freq_flag = true;
        if( !wait_event_timeout( devfreq->target_freq_wait_queue, (!devfreq->update_freq_flag),
                msecs_to_jiffies(100) ) )
        {
            if(debug_pr_devfreq_info) //usually last req,but all vcmd done
                dev_info(dev,"devfreq target freq set : wait queue timeout\n");
        }
    }
     
    return 0;
}

static int decoder_devfreq_get_status(struct device *dev, struct devfreq_dev_status *stat)
{
    unsigned long irqflags;
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
    stat->current_frequency = devfreq->cur_devfreq;
    spin_lock_irqsave(&devfreq->lock, irqflags);

    decoder_devfreq_update_utilization(devfreq);

    stat->total_time = ktime_to_ns(ktime_add(devfreq->busy_time,
                devfreq->idle_time));
    stat->busy_time = ktime_to_ns(devfreq->busy_time);

    decoder_devfreq_reset(devfreq);

    spin_unlock_irqrestore(&devfreq->lock, irqflags);

    if(debug_pr_devfreq_info){
        dev_info(dev, "busy %lu total %lu %lu %% freq %lu MHz\n",
            stat->busy_time/1000, stat->total_time/1000,
            stat->busy_time / (stat->total_time / 100),
            stat->current_frequency / 1000 / 1000);
    }
    return 0;
}

static int decoder_devfreq_get_cur_freq(IN struct device *dev, OUT unsigned long *freq)
{
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
    *freq = devfreq->cur_devfreq;
    return 0;
}
#ifdef CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND
struct devfreq_simple_ondemand_data decoder_gov_data;
#endif
static struct devfreq_dev_profile decoder_devfreq_gov_data =
{
    .polling_ms = 100,
    .target = decoder_devfreq_target,
    .get_dev_status = decoder_devfreq_get_status,
    .get_cur_freq = decoder_devfreq_get_cur_freq,
};

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
static struct monitor_dev_profile decoder_dev_monitor = {
	.type = MONITOR_TPYE_DEV,
};
#endif

void decoder_devfreq_fini(struct device *dev)
{
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
#ifdef CONFIG_TH1520_SYSTEM_MONITOR
    if(devfreq->mdev_info)
      th1520_system_monitor_unregister(devfreq->mdev_info);
#endif

    if (devfreq->df) {
        devm_devfreq_remove_device(dev, devfreq->df);
        devfreq->df = NULL;
    }

    if (devfreq->opp_of_table_added) {
        dev_pm_opp_of_remove_table(dev);
        devfreq->opp_of_table_added = false;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
    if (devfreq->clkname_opp_table) {
        dev_pm_opp_put_clkname(devfreq->clkname_opp_table);
        devfreq->clkname_opp_table = NULL;
    }
#else
    if(devfreq->token >= 0)
        dev_pm_opp_put_clkname(devfreq->token);
#endif
    mutex_destroy(&devfreq->clk_mutex);
}

int decoder_devfreq_init(struct device *dev)
{
    struct devfreq *df;
    struct clk *new_clk;
    struct opp_table *opp_table;
    int ret = 0;
    struct decoder_devfreq *devfreq = decoder_get_devfreq_priv_data();
    
    memset(devfreq,0,sizeof(struct decoder_devfreq));
    spin_lock_init(&devfreq->lock);
    init_waitqueue_head(&devfreq->target_freq_wait_queue);
    mutex_init(&devfreq->clk_mutex);

#ifdef CONFIG_PM_DEVFREQ
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
    opp_table = dev_pm_opp_set_clkname(dev,"cclk");
    if(IS_ERR(opp_table)) {
        pr_err("dec set cclk failed\n");
        ret = PTR_ERR(opp_table);
		    goto err_fini;
    }
    devfreq->clkname_opp_table = opp_table;
#else
    devfreq->token = dev_pm_opp_set_clkname(dev,"cclk");
    if (devfreq->token < 0) {
        pr_err("enc set cclk failed\n");
        ret = devfreq->token;
        goto err_fini;
    }
#endif

    ret = dev_pm_opp_of_add_table(dev);
    if(ret) {
        pr_info("dec opp table not found in dtb\n");
        goto err_fini;
    }
    devfreq->opp_of_table_added = true;

    new_clk = devm_clk_get(dev, "cclk");

    devfreq->cur_devfreq = clk_get_rate(new_clk);

    decoder_devfreq_gov_data.initial_freq = devfreq->cur_devfreq;

#ifdef CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND
    decoder_gov_data.upthreshold = 80;
    decoder_gov_data.downdifferential = 10;

    df = devm_devfreq_add_device(dev,
                &decoder_devfreq_gov_data,
                DEVFREQ_GOV_SIMPLE_ONDEMAND,
                &decoder_gov_data);
    
    if(IS_ERR(df)) {
        pr_err("Error: init devfreq %lx %ld\n", (unsigned long)dev,(long)df);
        devfreq->df = NULL;
        ret = PTR_ERR(df);
        goto err_fini;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
    unsigned long *freq_table = df->profile->freq_table;
    if (freq_table[0] < freq_table[df->profile->max_state - 1]) {
        devfreq->max_freq = freq_table[df->profile->max_state - 1];
    } else {
        devfreq->max_freq  = freq_table[0];
    }
#else
    unsigned long *freq_table = df->freq_table;
    if (freq_table[0] < freq_table[df->max_state - 1]) {
        devfreq->max_freq = freq_table[df->max_state - 1];
    } else {
        devfreq->max_freq  = freq_table[0];
    }
#endif
    pr_info("device max freq %ld\n",devfreq->max_freq);
    df->suspend_freq = 0; // not set freq when suspend,not suitable for async set rate
    devfreq->df = df;

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
    decoder_dev_monitor.data = devfreq->df;
    devfreq->mdev_info = th1520_system_monitor_register(dev, &decoder_dev_monitor);
    if (IS_ERR(devfreq->mdev_info))
      devfreq->mdev_info = NULL;
#endif

#endif
#endif
    return 0;

err_fini:
    decoder_devfreq_fini(dev);
    return ret;
}

/******************************************************************************\
******************************* VPU Devfreq support END ************************
\******************************************************************************/
void vdec_vcmd_profile_update(struct work_struct *work);
static DECLARE_DELAYED_WORK(vdec_cmd_profile_work,vdec_vcmd_profile_update);
static ktime_t last_update;
static long update_period_ms = 0;


struct vcmd_profile vdec_vcmd_profile;
void vdec_vcmd_profile_update(struct work_struct *work)
{
    //update busy time
    ktime_t now,during;
    struct decoder_devfreq *devfreq;
    devfreq = decoder_get_devfreq_priv_data();
    now = ktime_get();
    during = ktime_sub(now,last_update);
    last_update = now;
    vdec_vcmd_profile.dev_loading_percent = ktime_to_us(devfreq->based_maxfreq_busy_time) * 100/ktime_to_us(during);
    if(vdec_vcmd_profile.dev_loading_percent > vdec_vcmd_profile.dev_loading_max_percent)
      vdec_vcmd_profile.dev_loading_max_percent = vdec_vcmd_profile.dev_loading_percent;
    pr_debug("based_maxfreq_busy_time %lldms,during period %lld ms",ktime_to_us(devfreq->based_maxfreq_busy_time)/1000,ktime_to_ms(during));

    if(devfreq->busy_record_count > 0)
      vdec_vcmd_profile.avg_hw_proc_us = ktime_to_us(devfreq->based_maxfreq_busy_time)/devfreq->busy_record_count;
    else
      vdec_vcmd_profile.avg_hw_proc_us = 0;

    vdec_vcmd_profile.last_hw_proc_us = ktime_to_us(devfreq->based_maxfreq_last_busy_t);
    vdec_vcmd_profile.proced_count  = devfreq->busy_record_count;
    decoder_devfreq_reset_profile_record(devfreq);
    if(update_period_ms > 0)
      schedule_delayed_work(&vdec_cmd_profile_work, msecs_to_jiffies(update_period_ms));
}

static ssize_t log_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    ssize_t len = 0;

    const char *module_version = "1.0.0";
    int dev_id = 0;


    len += scnprintf(buf + len, PAGE_SIZE - len,
        "[VDEC] Version: %s q\n"
        "----------------------------------------MODULE PARAM-----------------------------\n"
        "updatePeriod_ms\n"
        "       %d\n"
        "----------------------------------------MODULE STATUS------------------------------\n"
        "DevId         DevLoading_%%   DevLoadingMax_%%\n"
        " %d                 %d               %d\n"

        " avg_hw_proc_us     last_hw_proc_us    proced_count\n"
        " %d                       %d                   %d \n"
        "cur_submit_vcmd     cur_complete_vcmd   vcmd_num_share_irq\n"
        " %d                       %d                   %d \n"
        "----------------------------------------EXCEPTION INFO-----------------------------------------\n"
        "BusErr    Abort     Timeout    CmdErr\n"
        " %d         %d         %d        %d \n",
        module_version, update_period_ms,
        dev_id, vdec_vcmd_profile.dev_loading_percent, vdec_vcmd_profile.dev_loading_max_percent,

        vdec_vcmd_profile.avg_hw_proc_us, vdec_vcmd_profile.last_hw_proc_us,vdec_vcmd_profile.proced_count,
        vdec_vcmd_profile.cur_submit_vcmd_id,vdec_vcmd_profile.cur_complete_vcmd_id,vdec_vcmd_profile.vcmd_num_share_irq,
        vdec_vcmd_profile.vcmd_buserr_cnt, vdec_vcmd_profile.vcmd_abort_cnt, vdec_vcmd_profile.vcmd_timeout_cnt, vdec_vcmd_profile.vcmd_cmderr_cnt);


    return len;
}
static ssize_t log_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    /******************clear *********************/
    vdec_vcmd_profile.vcmd_buserr_cnt = 0;
    vdec_vcmd_profile.vcmd_abort_cnt  = 0;
    vdec_vcmd_profile.vcmd_timeout_cnt = 0;
    vdec_vcmd_profile.vcmd_cmderr_cnt = 0;

    vdec_vcmd_profile.dev_loading_max_percent = 0;
    vdec_vcmd_profile.last_hw_proc_us = 0;
    return count;
}


/******************updatePeriod ************************************/
static ssize_t updatePeriod_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf,"%u\n",update_period_ms);
}

static ssize_t updatePeriod_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    char *start = (char *)buf;
    long old_period = update_period_ms;
    update_period_ms = simple_strtoul(start, &start, 0);
    if(old_period == 0 && update_period_ms)
      schedule_delayed_work(&vdec_cmd_profile_work,msecs_to_jiffies(update_period_ms));
    return count;
}
/******************define log *************************************/
static struct kobj_attribute log_attr = __ATTR(log, 0664, log_show, log_store);
/******************define updatePeriod_ms*************************************/
static struct kobj_attribute updatePeriod_attr = __ATTR(updatePeriod_ms, 0664, updatePeriod_show, updatePeriod_store);


static struct attribute *attrs[] = {
    &log_attr.attr,
    &updatePeriod_attr.attr,
    NULL,   // must be NULL
};


static struct attribute_group vdec_dev_attr_group = {
    .name = "info", // dir name
    .attrs = attrs,
};


static int decoder_hantrodec_probe(struct platform_device *pdev)
{
  printk("enter %s\n",__func__);
  printk("pcie=%d\n",pcie);
  int result, i;
  struct resource *mem;
  //struct decoder_driver_device *pdriver_dev;
  enum MMUStatus status = 0;
  enum MMUStatus mmu_status = MMU_STATUS_FALSE;
  volatile u8* mmu_hwregs[MAX_SUBSYS_NUM][2];
  //pdriver_dev = devm_kzalloc(&pdev->dev,sizeof(struct decoder_driver_device),GFP_KERNEL);
 // if(pdriver_dev == NULL)
  //{
//	  pr_err("%s:alloc struct deocder_driver_device error!\n",__func__);
//	  return -ENOMEM;
 /// }

  //pdriver_dev->hantrodec_class = class_create(THIS_MODULE,"hantrodec");
  printk("%s:init variable is ok!\n",__func__);
  mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
  printk("%s:get resource is ok!\n",__func__);
  //devm_ioremap_resource(&pdev->dev,mem);
  if(mem->start)
  	base_port = mem->start;
  else
	  printk("%s:mem->start is not exist!\n",__func__);
  printk("%s:start get irq!\n",__func__);


  PDEBUG("module init\n");

  CheckSubsysCoreArray(vpu_subsys, &vcmd);
  irq[0] = platform_get_irq(pdev,0);
  printk("%s:get irq!\n",__func__);
  printk("%s:base_port=0x%llx,irq=%d\n",__func__,base_port,irq[0]);
  printk("%s:pcie=%d\n",__func__,pcie);

  if (pcie) {
    result = PcieInit();
    if(result)
      goto err;
  }

  pr_info("hantrodec: dec/pp kernel module. \n");

  /* If base_port is set when insmod, use that for single core legacy mode. */
  if (base_port != -1) {
    multicorebase[0] = base_port;
    if (pcie)
      multicorebase[0] += HANTRO_REG_OFFSET0;
    elements = 1;
    vpu_subsys[0].base_addr = base_port;
    pr_info("hantrodec: Init single core at 0x%08lx IRQ=%i\n",
           multicorebase[0], irq[0]);
  } else {
    pr_info("hantrodec: Init multi core[0] at 0x%16lx\n"
           "                      core[1] at 0x%16lx\n"
           "                      core[2] at 0x%16lx\n"
           "                      core[3] at 0x%16lx\n"
           "           IRQ_0=%i\n"
           "           IRQ_1=%i\n",
           multicorebase[0], multicorebase[1],
           multicorebase[2], multicorebase[3],
           irq[0],irq[1]);
  }

  hantrodec_data.pdev = pdev;
  hantrodec_data.cores = 0;

  hantrodec_data.iosize[0] = DEC_IO_SIZE_0;
  hantrodec_data.irq[0] = irq[0];
  hantrodec_data.iosize[1] = DEC_IO_SIZE_1;
  hantrodec_data.irq[1] = irq[1];

  //extern void dump_core_array(void);
  //dump_vpu_subsys(&(vpu_subsys[0]));
  //dump_core_array();
  pr_info("hantrodec_data.irq=%d\n",
         hantrodec_data.irq[0]);

  for(i=0; i< HXDEC_MAX_CORES; i++) {
    int j;
    for (j = 0; j < HW_CORE_MAX; j++)
      hantrodec_data.hwregs[i][j] = 0;
    /* If user gave less core bases that we have by default,
     * invalidate default bases
     */
    if(elements && i>=elements) {
      multicorebase[i] = 0;
    }
  }

  hantrodec_data.async_queue_dec = NULL;
  hantrodec_data.async_queue_pp = NULL;

    hantrodec_data.has_power_domains = check_power_domain();

    hantrodec_data.cclk = devm_clk_get(&pdev->dev, "cclk");
    if (IS_ERR(hantrodec_data.cclk)) {
        dev_err(&pdev->dev, "failed to get core clock\n");
        goto err;
    }

    hantrodec_data.aclk = devm_clk_get(&pdev->dev, "aclk");
    if (IS_ERR(hantrodec_data.aclk)) {
        dev_err(&pdev->dev, "failed to get axi clock\n");
        goto err;
    }

    hantrodec_data.pclk = devm_clk_get(&pdev->dev, "pclk");
    if (IS_ERR(hantrodec_data.pclk)) {
        dev_err(&pdev->dev, "failed to get apb clock\n");
        goto err;
    }

    //pm_runtime_set_autosuspend_delay(&pdev->dev, VC8000D_PM_TIMEOUT);
    //pm_runtime_use_autosuspend(&pdev->dev);
    pm_runtime_enable(&pdev->dev);
    if (!pm_runtime_enabled(&pdev->dev)) {
      if (decoder_runtime_resume(&pdev->dev))
      {
        pm_runtime_disable(&pdev->dev);
        //pm_runtime_dont_use_autosuspend(&pdev->dev);
      }
    }
    pm_runtime_resume_and_get(&pdev->dev);

    if (hantrodec_major == 0)
    {
        result = alloc_chrdev_region(&hantrodec_devt, 0, 1, "hantrodec");
        if (result != 0)
        {
            printk(KERN_ERR "%s: alloc_chrdev_region error\n", __func__);
            goto err;
        }
        hantrodec_major = MAJOR(hantrodec_devt);
        hantrodec_minor = MINOR(hantrodec_devt);
    }
    else
    {
        hantrodec_devt = MKDEV(hantrodec_major, hantrodec_minor);
        result = register_chrdev_region(hantrodec_devt, 1, "hantrodec");
        if (result)
        {
            printk(KERN_ERR "%s: register_chrdev_region error\n", __func__);
            goto err;
        }
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    hantrodec_class = class_create(THIS_MODULE, "hantrodec");
#else
    hantrodec_class = class_create("hantrodec");
#endif
    if (IS_ERR(hantrodec_class))
    {
        printk(KERN_ERR "%s, %d: class_create error!\n", __func__, __LINE__);
        goto err;
    }
	hantrodec_devt = MKDEV(hantrodec_major, hantrodec_minor);

	cdev_init(&hantrodec_cdev, &hantrodec_fops);
	result = cdev_add(&hantrodec_cdev, hantrodec_devt, 1);
	if ( result )
	{
		printk(KERN_ERR "%s, %d: cdev_add error!\n", __func__, __LINE__);
		goto err;
	}

	device_create(hantrodec_class, NULL, hantrodec_devt,
			NULL, "hantrodec");

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

  result = ReserveIO();
  if(result < 0) {
    goto err;
  }

  for (i = 0; i < hantrodec_data.cores; i++) {
    AXIFEEnable(hantrodec_data.hwregs[i][HW_AXIFE]);
  }

  /* MMU only initial once No matter how many MMU we have */
  if (hantrodec_data.hwregs[0][HW_MMU]) {
    status = MMUInit(hantrodec_data.hwregs[0][HW_MMU]);
    if(status == MMU_STATUS_NOT_FOUND)
      pr_info("MMU does not exist!\n");
    else if(status != MMU_STATUS_OK)
      goto err;
    else
      pr_info("MMU detected!\n");

    for (i = 0; i < MAX_SUBSYS_NUM; i++ ) {
      mmu_hwregs[i][0] = hantrodec_data.hwregs[i][HW_MMU];
      mmu_hwregs[i][1] = hantrodec_data.hwregs[i][HW_MMU_WR];
    }
    mmu_status = MMUEnable(mmu_hwregs);
  }

  allocator_init(&pdev->dev);

  decoder_add_debugfs(pdev);

  if (vcmd) {
    /* unmap and release mem region for VCMD, since it will be mapped and
       reserved again in hantro_vcmd.c*/
    for (i = 0; i < hantrodec_data.cores; i++) {
      if (hantrodec_data.hwregs[i][HW_VCMD]) {
        iounmap((void *)hantrodec_data.hwregs[i][HW_VCMD]);
        release_mem_region(vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[HW_VCMD],
                           vpu_subsys[i].submodule_iosize[HW_VCMD]);
        hantrodec_data.hwregs[i][HW_VCMD] = 0;
      }
    }
    result = hantrovcmd_init(pdev);
    //devfreq
    pr_info("start vdec devfreq init \n");
    if(decoder_devfreq_init(&pdev->dev) ) {
      pr_err("vdec devfreq not enabled\n");
    } else {
      pr_info("vdec devfreq init ok\n");
    }

    result = sysfs_create_group(&pdev->dev.kobj, &vdec_dev_attr_group);
    if(result)
      pr_warn("vdec create sysfs failed\n");
    pm_runtime_mark_last_busy(&pdev->dev);
    pm_runtime_put_sync_suspend(&pdev->dev);
    if (result) return result;

    pr_info("PM runtime was enable\n");

    return 0;
  }

  memset(dec_owner, 0, sizeof(dec_owner));
  memset(pp_owner, 0, sizeof(pp_owner));

  sema_init(&dec_core_sem, hantrodec_data.cores);
  sema_init(&pp_core_sem, 1);

  /* read configuration fo all cores */
  ReadCoreConfig(&hantrodec_data);

  /* reset hardware */
  ResetAsic(&hantrodec_data);

  /* register irq for each core */
  if(irq[0] > 0) {
    result = request_irq(irq[0], hantrodec_isr,
//#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
//                         SA_INTERRUPT | SA_SHIRQ,
//#else
//                         IRQF_SHARED,
//#endif
			 IRQF_TRIGGER_RISING,
                         "hantrodec", (void *) &hantrodec_data);

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
    pr_info("hantrodec: IRQ not in use!\n");
  }

  if(irq[1] > 0) {
    result = request_irq(irq[1], hantrodec_isr,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18))
                         SA_INTERRUPT | SA_SHIRQ,
#else
                         IRQF_SHARED,
#endif
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
    pr_info("hantrodec: IRQ not in use!\n");
  }

  for (i = 0; i < hantrodec_data.cores; i++) {
    volatile u8 *hwregs = hantrodec_data.hwregs[i][HW_VC8000D];
    if (hwregs) {
      pr_info("hantrodec: VC8000D [%d] has build id 0x%08x\n",
             i, ioread32((void*)(hwregs + HANTRODEC_HWBUILD_ID_OFF)));
    }
  }

  pm_runtime_mark_last_busy(&pdev->dev);
  pm_runtime_put_sync_suspend(&pdev->dev);
  pr_info("hantrodec: module inserted. Major = %d\n", hantrodec_major);

  /* Please call the TEE functions to set VC8000D DRM relative registers here */

  return 0;

err:
  if (root_debugfs_dir) {
    debugfs_remove_recursive(root_debugfs_dir);
    root_debugfs_dir = NULL;
  }
  ReleaseIO();
  pr_info("hantrodec: module not inserted\n");
  pm_runtime_mark_last_busy(&pdev->dev);
  pm_runtime_put_sync_suspend(&pdev->dev);
  unregister_chrdev_region(hantrodec_devt, 1);
  return result;
}


static int decoder_hantrodec_remove(struct platform_device *pdev)
{
  if (root_debugfs_dir) {
    debugfs_remove_recursive(root_debugfs_dir);
    root_debugfs_dir = NULL;
  }
  cancel_delayed_work_sync(&vdec_cmd_profile_work);
  pm_runtime_resume_and_get(&pdev->dev);
  /* When vcmd is true, irq free  in hantrovcmd_cleanup!  
    When vcmd is flase, it is not need because in line 2528 freed */
  #if 0
  if(!vcmd){ 
    if(irq[0] > 0)
    {
      free_irq(irq[0],(void *) &hantrodec_data);
    }
    if(irq[1] > 0)
    {
      free_irq(irq[1],(void *) &hantrodec_data);
    }
  }
  #endif
  hantrodec_t *dev = &hantrodec_data;
  int i, n =0;
  volatile u8* mmu_hwregs[MAX_SUBSYS_NUM][2];

  for (i = 0; i < MAX_SUBSYS_NUM; i++ ) {
    mmu_hwregs[i][0] = dev->hwregs[i][HW_MMU];
    mmu_hwregs[i][1] = dev->hwregs[i][HW_MMU_WR];
  }
  if (dev->hwregs[0][HW_MMU] || dev->hwregs[1][HW_MMU] ||
      dev->hwregs[2][HW_MMU] || dev->hwregs[3][HW_MMU])
    MMUCleanup(mmu_hwregs);

  if (vcmd) {
    hantrovcmd_cleanup(pdev);
  } else {
    /* reset hardware */
    ResetAsic(dev);

    /* free the IRQ */
    for (n = 0; n < dev->cores; n++) {
      if(dev->irq[n] != -1) {
        free_irq(dev->irq[n], (void *) dev);
      }
    }
  }
  ReleaseIO();

#ifdef CLK_CFG
  if (clk_cfg!=NULL && !IS_ERR(clk_cfg)) {
    clk_disable_unprepare(clk_cfg);
    is_clk_on = 0;
    printk("turned off hantrodec clk\n");
  }

  /*delete timer*/
  del_timer(&timer);
#endif

  pm_runtime_mark_last_busy(&pdev->dev);
  pm_runtime_put_sync_suspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		decoder_runtime_suspend(&pdev->dev);

  decoder_devfreq_fini(&pdev->dev);

  cdev_del(&hantrodec_cdev);
  device_destroy(hantrodec_class, hantrodec_devt);
  unregister_chrdev_region(hantrodec_devt, 1);
  class_destroy(hantrodec_class);
  sysfs_remove_group(&pdev->dev.kobj,&vdec_dev_attr_group);

  pr_info("hantrodec: module removed\n");
  return 0;
}


static const struct dev_pm_ops decoder_runtime_pm_ops = {
  SET_SYSTEM_SLEEP_PM_OPS(decoder_suspend, decoder_resume)
	SET_RUNTIME_PM_OPS(decoder_runtime_suspend, decoder_runtime_resume, NULL)
};

static struct platform_driver decoder_hantrodec_driver = {
	.probe  = decoder_hantrodec_probe,
	.remove = decoder_hantrodec_remove,
	.driver = {
		.name   = "decoder_hantrodec",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(isp_of_match),
    .pm = &decoder_runtime_pm_ops,
	}
};


/*------------------------------------------------------------------------------
 Function name   : hantrodec_init
 Description     : Initialize the driver

 Return type     : int
------------------------------------------------------------------------------*/

int __init hantrodec_init(void) {
	int ret = 0;
	printk("enter %s\n",__func__);
	ret = platform_driver_register(&decoder_hantrodec_driver);
	if(ret)
	{
		pr_err("register platform driver failed!\n");
	}
	return ret;
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_cleanup
 Description     : clean up

 Return type     : int
------------------------------------------------------------------------------*/

void __exit hantrodec_cleanup(void) {
	printk("enter %s\n",__func__);
	platform_driver_unregister(&decoder_hantrodec_driver);
	return;
}

/*------------------------------------------------------------------------------
 Function name   : CheckHwId
 Return type     : int
------------------------------------------------------------------------------*/
static int CheckHwId(hantrodec_t * dev) {
  int hwid;
  int i, j;
  size_t num_hw = sizeof(DecHwId) / sizeof(*DecHwId);

  int found = 0;

  for (i = 0; i < dev->cores; i++) {
    for (j = 0; j < HW_CORE_MAX; j++) {
      if ((j == HW_VC8000D || j == HW_BIGOCEAN || j == HW_VC8000DJ) &&
           dev->hwregs[i][j] != NULL) {
        hwid = readl(dev->hwregs[i][j]);
        pr_info("hantrodec: core %d HW ID=0x%08x\n", i, hwid);
        hwid = (hwid >> 16) & 0xFFFF; /* product version only */
        while (num_hw--) {
          if (hwid == DecHwId[num_hw]) {
            pr_info("hantrodec: Supported HW found at 0x%16lx\n",
                   vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[j]);
            found++;
            dev->hw_id[i][j] = hwid;
            break;
          }
        }
        if (!found) {
          pr_info("hantrodec: Unknown HW found at 0x%16lx\n",
                 multicorebase_actual[i]);
          return 0;
        }
        found = 0;
        num_hw = sizeof(DecHwId) / sizeof(*DecHwId);
      }
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
  int i, j;
  long int hwid;
  u32  axife_config;

  memcpy(multicorebase_actual, multicorebase, HXDEC_MAX_CORES * sizeof(unsigned long));
  memcpy((unsigned int*)(hantrodec_data.iosize), iosize, HXDEC_MAX_CORES * sizeof(unsigned int));
  memcpy((unsigned int*)(hantrodec_data.irq), irq, HXDEC_MAX_CORES * sizeof(int));

  for (i = 0; i < MAX_SUBSYS_NUM; i++) {
    if (!vpu_subsys[i].base_addr) continue;

    for (j = 0; j < HW_CORE_MAX; j++) {
      if (vpu_subsys[i].submodule_iosize[j]) {
        pr_info("hantrodec: base=0x%16lx, iosize=%d\n",
                          vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[j],
                          vpu_subsys[i].submodule_iosize[j]);

        if (!request_mem_region(vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[j],
                                vpu_subsys[i].submodule_iosize[j],
                                "hantrodec0")) {
          pr_info("hantrodec: failed to reserve HW %d regs\n", j);
          return -EBUSY;
        }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0))
        vpu_subsys[i].submodule_hwregs[j] =
          hantrodec_data.hwregs[i][j] =
            (volatile u8 *) ioremap_nocache(vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[j],
                                            vpu_subsys[i].submodule_iosize[j]);
#else
        vpu_subsys[i].submodule_hwregs[j] =
          hantrodec_data.hwregs[i][j] =
            (volatile u8 *) ioremap(vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[j],
                                            vpu_subsys[i].submodule_iosize[j]);
#endif

        if (hantrodec_data.hwregs[i][j] == NULL) {
          pr_info("hantrodec: failed to ioremap HW %d regs\n", j);
          release_mem_region(vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[j],
                             vpu_subsys[i].submodule_iosize[j]);
          return -EBUSY;
        } else {
          if (vpu_subsys[i].has_apbfilter[j]) {
            apbfilter_cfg[i][j].has_apbfilter = 1;
            hwid = ioread32((void*)(hantrodec_data.hwregs[i][HW_VC8000D]));
            if (IS_BIGOCEAN(hwid & 0xFFFF)) {
              if (j == HW_BIGOCEAN) {
                apbfilter_cfg[i][j].nbr_mask_regs = AV1_NUM_MASK_REG;
                apbfilter_cfg[i][j].num_mode = AV1_NUM_MODE;
                apbfilter_cfg[i][j].mask_reg_offset = AV1_MASK_REG_OFFSET;
                apbfilter_cfg[i][j].mask_bits_per_reg = AV1_MASK_BITS_PER_REG;
                apbfilter_cfg[i][j].page_sel_addr = apbfilter_cfg[i][j].mask_reg_offset + apbfilter_cfg[i][j].nbr_mask_regs * 4;
              }
              if (j == HW_AXIFE) {
                apbfilter_cfg[i][j].nbr_mask_regs = AXIFE_NUM_MASK_REG;
                apbfilter_cfg[i][j].num_mode = AXIFE_NUM_MODE;
                apbfilter_cfg[i][j].mask_reg_offset = AXIFE_MASK_REG_OFFSET;
                apbfilter_cfg[i][j].mask_bits_per_reg = AXIFE_MASK_BITS_PER_REG;
                apbfilter_cfg[i][j].page_sel_addr = apbfilter_cfg[i][j].mask_reg_offset + apbfilter_cfg[i][j].nbr_mask_regs * 4;
              }
            } else {
              hwid = ioread32((void*)(hantrodec_data.hwregs[i][HW_VC8000D] + HANTRODEC_HW_BUILD_ID_OFF));
              if (hwid == 0x1F58) {
                if (j == HW_VC8000D) {
                  apbfilter_cfg[i][j].nbr_mask_regs = VC8000D_NUM_MASK_REG;
                  apbfilter_cfg[i][j].num_mode = VC8000D_NUM_MODE;
                  apbfilter_cfg[i][j].mask_reg_offset = VC8000D_MASK_REG_OFFSET;
                  apbfilter_cfg[i][j].mask_bits_per_reg = VC8000D_MASK_BITS_PER_REG;
                  apbfilter_cfg[i][j].page_sel_addr = apbfilter_cfg[i][j].mask_reg_offset + apbfilter_cfg[i][j].nbr_mask_regs * 4;
                }
                if (j == HW_AXIFE) {
                  apbfilter_cfg[i][j].nbr_mask_regs = AXIFE_NUM_MASK_REG;
                  apbfilter_cfg[i][j].num_mode = AXIFE_NUM_MODE;
                  apbfilter_cfg[i][j].mask_reg_offset = AXIFE_MASK_REG_OFFSET;
                  apbfilter_cfg[i][j].mask_bits_per_reg = AXIFE_MASK_BITS_PER_REG;
                  apbfilter_cfg[i][j].page_sel_addr = apbfilter_cfg[i][j].mask_reg_offset + apbfilter_cfg[i][j].nbr_mask_regs * 4;
                }
              } else if (hwid == 0x1F59) {
                if (j == HW_VC8000DJ) {
                  apbfilter_cfg[i][j].nbr_mask_regs = VC8000DJ_NUM_MASK_REG;
                  apbfilter_cfg[i][j].num_mode = VC8000DJ_NUM_MODE;
                  apbfilter_cfg[i][j].mask_reg_offset = VC8000DJ_MASK_REG_OFFSET;
                  apbfilter_cfg[i][j].mask_bits_per_reg = VC8000DJ_MASK_BITS_PER_REG;
                  apbfilter_cfg[i][j].page_sel_addr = apbfilter_cfg[i][j].mask_reg_offset + apbfilter_cfg[i][j].nbr_mask_regs * 4;
                }
                if (j == HW_AXIFE) {
                  apbfilter_cfg[i][j].nbr_mask_regs = AXIFE_NUM_MASK_REG;
                  apbfilter_cfg[i][j].num_mode = AXIFE_NUM_MODE;
                  apbfilter_cfg[i][j].mask_reg_offset = AXIFE_MASK_REG_OFFSET;
                  apbfilter_cfg[i][j].mask_bits_per_reg = AXIFE_MASK_BITS_PER_REG;
                  apbfilter_cfg[i][j].page_sel_addr = apbfilter_cfg[i][j].mask_reg_offset + apbfilter_cfg[i][j].nbr_mask_regs * 4;
                }
              } else {
                  pr_info("hantrodec: furture APBFILTER can read those configure parameters from REG\n");
              }
            }
            hantrodec_data.apbfilter_hwregs[i][j] = hantrodec_data.hwregs[i][j] + apbfilter_cfg[i][j].mask_reg_offset;
          } else {
            apbfilter_cfg[i][j].has_apbfilter = 0;
          }

          if (j == HW_AXIFE) {
            hwid = ioread32((void*)(hantrodec_data.hwregs[i][j] + HANTRODEC_HW_BUILD_ID_OFF));
            axife_config = ioread32((void*)(hantrodec_data.hwregs[i][j]));
            axife_cfg[i].axi_rd_chn_num = axife_config & 0x7F;
            axife_cfg[i].axi_wr_chn_num = (axife_config >> 7) & 0x7F;
            axife_cfg[i].axi_rd_burst_length = (axife_config >> 14) & 0x1F;
            axife_cfg[i].axi_wr_burst_length = (axife_config >> 22) & 0x1F;
            axife_cfg[i].fe_mode = 0; /*need to read from reg in furture*/
            if (hwid == 0x1F66) {
              axife_cfg[i].fe_mode = 1;
            }
          }
        }
        config.its_main_core_id[i] = -1;
        config.its_aux_core_id[i] = -1;

        pr_info("hantrodec: HW %d reg[0]=0x%08x\n", j, readl(hantrodec_data.hwregs[i][j]));

#ifdef SUPPORT_2ND_PIPELINES
	if (j != HW_VC8000D) continue;
        hwid = ((readl(hantrodec_data.hwregs[i][HW_VC8000D])) >> 16) & 0xFFFF; /* product version only */

        if (IS_VC8000D(hwid)) {
          u32 reg;
          /*TODO(min): DO NOT support 2nd pipeline. */
          reg = readl(hantrodec_data.hwregs[i][HW_VC8000D] + HANTRODEC_SYNTH_CFG_2_OFF);
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
              pr_info("hantrodec: failed to reserve HW regs\n");
              return -EBUSY;
            }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0))
            hantrodec_data.hwregs[i][HW_VC8000D] = (volatile u8 *) ioremap_nocache(multicorebase_actual[i],
                                       hantrodec_data.iosize[i]);
#else
            hantrodec_data.hwregs[i][HW_VC8000D] = (volatile u8 *) ioremap(multicorebase_actual[i],
                                       hantrodec_data.iosize[i]);
#endif

            if (hantrodec_data.hwregs[i][HW_VC8000D] == NULL ) {
              pr_info("hantrodec: failed to ioremap HW regs\n");
              ReleaseIO();
                return -EBUSY;
            }
            hantrodec_data.cores++;
          }
        }
#endif
      } else {
        hantrodec_data.hwregs[i][j] = NULL;
      }
    }
    hantrodec_data.cores++;
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
  int i, j;
  for (i = 0; i < hantrodec_data.cores; i++) {
    for (j = 0; j < HW_CORE_MAX; j++) {
      if (hantrodec_data.hwregs[i][j]) {
        iounmap((void *) hantrodec_data.hwregs[i][j]);
        release_mem_region(vpu_subsys[i].base_addr + vpu_subsys[i].submodule_offset[j],
                           vpu_subsys[i].submodule_iosize[j]);
        hantrodec_data.hwregs[i][j] = 0;
      }
    }
  }
}

/*------------------------------------------------------------------------------
 Function name   : hantrodec_isr
 Description     : interrupt handler

 Return type     : irqreturn_t
------------------------------------------------------------------------------*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
irqreturn_t hantrodec_isr(int irq, void *dev_id, struct pt_regs *regs)
#else
irqreturn_t hantrodec_isr(int irq, void *dev_id)
#endif
{
  printk("%s:start!\n",__func__);
  unsigned long flags;
  unsigned int handled = 0;
  int i;
  volatile u8 *hwregs;

  hantrodec_t *dev = (hantrodec_t *) dev_id;
  u32 irq_status_dec;

  spin_lock_irqsave(&owner_lock, flags);

  for(i=0; i<dev->cores; i++) {
    volatile u8 *hwregs = dev->hwregs[i][HW_VC8000D];

    /* interrupt status register read */
    irq_status_dec = ioread32((void*)(hwregs + HANTRODEC_IRQ_STAT_DEC_OFF));

    if(irq_status_dec & HANTRODEC_DEC_IRQ) {
      /* clear dec IRQ */
      irq_status_dec &= (~HANTRODEC_DEC_IRQ);
      iowrite32(irq_status_dec, (void*)(hwregs + HANTRODEC_IRQ_STAT_DEC_OFF));

      PDEBUG("decoder IRQ received! core %d\n", i);

      atomic_inc(&irq_rx);

      dec_irq |= (1 << i);

      wake_up_interruptible_all(&dec_wait_queue);
      handled++;
    }
  }

  spin_unlock_irqrestore(&owner_lock, flags);

  if(!handled) {
    PDEBUG("IRQ received, but not hantrodec's!\n");
  }

  (void)hwregs;
  printk("%s:end!\n",__func__);
  return IRQ_RETVAL(handled);
}

/*------------------------------------------------------------------------------
 Function name   : ResetAsic
 Description     : reset asic (only VC8000D supports reset)

 Return type     :
------------------------------------------------------------------------------*/
void ResetAsic(hantrodec_t * dev) {
  int i, j;
  u32 status;

  for (j = 0; j < dev->cores; j++) {
    if (!dev->hwregs[j][HW_VC8000D]) continue;

    status = ioread32((void*)(dev->hwregs[j][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));

    if( status & HANTRODEC_DEC_E) {
      /* abort with IRQ disabled */
      status = HANTRODEC_DEC_ABORT | HANTRODEC_DEC_IRQ_DISABLE;
      iowrite32(status, (void*)(dev->hwregs[j][HW_VC8000D] + HANTRODEC_IRQ_STAT_DEC_OFF));
    }

    if (IS_G1(dev->hw_id[j][HW_VC8000D]))
      /* reset PP */
      iowrite32(0, (void*)(dev->hwregs[j][HW_VC8000D] + HANTRO_IRQ_STAT_PP_OFF));

    for (i = 4; i < dev->iosize[j]; i += 4) {
      iowrite32(0, (void*)(dev->hwregs[j][HW_VC8000D] + i));
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

  PDEBUG("Reg Dump Start\n");
  for(c = 0; c < dev->cores; c++) {
    for(i = 0; i < dev->iosize[c]; i += 4*4) {
      PDEBUG("\toffset %04X: %08X  %08X  %08X  %08X\n", i,
             ioread32(dev->hwregs[c][HW_VC8000D] + i),
             ioread32(dev->hwregs[c][HW_VC8000D] + i + 4),
             ioread32(dev->hwregs[c][HW_VC8000D] + i + 16),
             ioread32(dev->hwregs[c][HW_VC8000D] + i + 24));
    }
  }
  PDEBUG("Reg Dump End\n");
}
#endif


module_init( hantrodec_init);
module_exit( hantrodec_cleanup);

/* module description */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("VeriSilicon Microelectronics ");
MODULE_DESCRIPTION("driver module for Hantro video decoder VC8000D");


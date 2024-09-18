/*!
 *****************************************************************************
 * Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 ("GPL")in which case the provisions of
 * GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms
 * of GPL, and not to allow others to use your version of this file under the
 * terms of the MIT license, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by GPL
 * as set out in the file called "GPLHEADER" included in this distribution. If
 * you do not delete the provisions above, a recipient may use your version of
 * this file under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT_COPYING".
 *
 *****************************************************************************/
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/mod_devicetable.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/platform_device.h>

#include <hwdefs/vha_cr_gyrus.h>
#include <hwdefs/nn_sys_cr_gyrus.h>
#include <hwdefs/gyrus_system.h>

#define DEVICE_NAME "3NXF_plat"

#include <nexef_plat.h>
/* NNPU needed includes */
#define SUPPORT_RGX
#include <tc_drv.h>

//region Defines
/*
 * We don't support Apollo base board here, but to have a
 * proper error message if someone use the wrong baseboard,
 * we have a little bit of code to report it.
 *
 * For that we need to have the Apollo PCI IDs
 */
#define PCI_APOLLO_VENDOR_ID (0x1010)
#define PCI_APOLLO_DEVICE_ID (0x1CF2)

#define IS_APOLLO_DEVICE(devid) ((devid) == PCI_APOLLO_DEVICE_ID)

#define PCI_SIRIUS_VENDOR_ID (0x1AEE)
#define PCI_SIRIUS_DEVICE_ID (0x1020)
#define IS_SIRIUS_DEVICE(devid) ((devid) == PCI_SIRIUS_DEVICE_ID)

/*
 * from Odin Lite TRM rev 1.0.88
 */
#define PCI_ODIN_VENDOR_ID (0x1AEE)
#define PCI_ODIN_DEVICE_ID (0x1010)

#define IS_ODIN_DEVICE(devid) ((devid) == PCI_ODIN_DEVICE_ID)

/* Odin - System control register bar */
#define PCI_ODIN_SYS_CTRL_REGS_BAR (0)

#define PCI_ODIN_SYS_CTRL_BASE_OFFSET (0x0000)
/* srs_core */
#define PCI_ODIN_CORE_ID                        (0x0000)
#define PCI_ODIN_CORE_REVISION                  (0x0004)
#define PCI_ODIN_CORE_CHANGE_SET                (0x0008)
#define PCI_ODIN_CORE_USER_ID                   (0x000C)
#define PCI_ODIN_CORE_USER_BUILD                (0x0010)
/* Resets */
#define PCI_ODIN_CORE_INTERNAL_RESETN           (0x0080)
#define PCI_ODIN_CORE_EXTERNAL_RESETN           (0x0084)
#define PCI_ODIN_CORE_EXTERNAL_RESET            (0x0088)
#define PCI_ODIN_CORE_INTERNAL_AUTO_RESETN      (0x008C)
/* Clock */
#define PCI_ODIN_CORE_CLK_GEN_RESET             (0x0090)
/* Interrupts */
#define PCI_ODIN_CORE_INTERRUPT_STATUS          (0x0100)
#define PCI_ODIN_CORE_INTERRUPT_ENABLE          (0x0104)
#define PCI_ODIN_CORE_INTERRUPT_CLR             (0x010C)
#define PCI_ODIN_CORE_INTERRUPT_TEST            (0x0110)
/* GPIOs */
#define PCI_ODIN_CORE_NUM_GPIO                  (0x0180)
#define PCI_ODIN_CORE_GPIO_EN                   (0x0184)
#define PCI_ODIN_CORE_GPIO                      (0x0188)
/* DUT Ctrl */
#define PCI_ODIN_CORE_NUM_DUT_CTRL              (0x0190)
#define PCI_ODIN_CORE_DUT_CTRL1                 (0x0194)
#define PCI_ODIN_CORE_DUT_CTRL2                 (0x0198)
#define PCI_ODIN_CORE_NUM_DUT_STAT              (0x019C)
#define PCI_ODIN_CORE_DUT_STAT1                 (0x01A0)
#define PCI_ODIN_CORE_DUT_STAT2                 (0x01A4)
/* LEDs! */
#define PCI_ODIN_CORE_DASH_LEDS                 (0x01A8)
/* Core stuff */
#define PCI_ODIN_CORE_CORE_STATUS               (0x0200)
#define PCI_ODIN_CORE_CORE_CONTROL              (0x0204)
#define PCI_ODIN_CORE_REG_BANK_STATUS           (0x0208)
#define PCI_ODIN_CORE_MMCM_LOCK_STATUS          (0x020C)
#define PCI_ODIN_CORE_GIST_STATUS               (0x0210)

#define PCI_ODIN_MMCM_LOCK_STATUS_DUT_CORE      (1 << 0)
#define PCI_ODIN_MMCM_LOCK_STATUS_DUT_IF        (1 << 1)

/* core bits definitions */
#define INTERNAL_RESET_INTERNAL_RESETN_PIKE     (1 << 7)
#define EXTERNAL_RESET_EXTERNAL_RESETN_SPI      (1 << 1)
#define EXTERNAL_RESET_EXTERNAL_RESETN_DUT      (1 << 0)

#define EXTERNAL_RESET_DUT_CORE_MMCM            (1 << 1)

#define DUT_CTRL1_DUT_MST_OFFSET                (1 << 31)
#define ODIN_CORE_CONTROL_DUT_OFFSET_SHIFT      (24)
#define ODIN_CORE_CONTROL_DUT_OFFSET_MASK       (0x7 << ODIN_CORE_CONTROL_DUT_OFFSET_SHIFT)

/* interrupt bits definitions */
#define INT_INTERRUPT_MASTER_ENABLE             (1 << 31)
#define INT_INTERRUPT_DUT0                      (1 << 0)
#define INT_INTERRUPT_PDP                       (1 << 1)
#define INT_INTERRUPT_DUT1                      (1 << 9)

/* odn_clk_blk */
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV1  (0x0020)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV2  (0x0024)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV3  (0x001C)
#define PCI_ODIN_CLK_BLK_DUT_REG_CLK_OUT_DIV1   (0x0028)
#define PCI_ODIN_CLK_BLK_DUT_REG_CLK_OUT_DIV2   (0x002C)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT1     (0x0050)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT2     (0x0054)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT3     (0x004C)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_IN_DIV    (0x0058)
#define PCI_ODIN_CLK_BLK_DUT_SYS_CLK_OUT_DIV1   (0x0220)
#define PCI_ODIN_CLK_BLK_DUT_SYS_CLK_OUT_DIV2   (0x0224)
#define PCI_ODIN_CLK_BLK_DUT_SYS_CLK_OUT_DIV3   (0x021C)
#define PCI_ODIN_CLK_BLK_DUT_MEM_CLK_OUT_DIV1   (0x0228)
#define PCI_ODIN_CLK_BLK_DUT_MEM_CLK_OUT_DIV2   (0x022C)
#define PCI_ODIN_CLK_BLK_DUT_SYS_CLK_MULT1      (0x0250)
#define PCI_ODIN_CLK_BLK_DUT_SYS_CLK_MULT2      (0x0254)
#define PCI_ODIN_CLK_BLK_DUT_SYS_CLK_MULT3      (0x024C)
#define PCI_ODIN_CLK_BLK_DUT_SYS_CLK_IN_DIV     (0x0258)
#define PCI_ODIN_CLK_BLK_PDP_PIXEL_CLK_OUT_DIV1 (0x0620)
#define PCI_ODIN_CLK_BLK_PDP_PIXEL_CLK_OUT_DIV2 (0x0624)
#define PCI_ODIN_CLK_BLK_PDP_PIXEL_CLK_OUT_DIV3 (0x061C)
#define PCI_ODIN_CLK_BLK_PDP_MEM_CLK_OUT_DIV1   (0x0628)
#define PCI_ODIN_CLK_BLK_PDP_MEM_CLK_OUT_DIV2   (0x062C)
#define PCI_ODIN_CLK_BLK_PDP_PIXEL_CLK_MULT1    (0x0650)
#define PCI_ODIN_CLK_BLK_PDP_PIXEL_CLK_MULT2    (0x0654)
#define PCI_ODIN_CLK_BLK_PDP_PIXEL_CLK_MULT3    (0x064C)
#define PCI_ODIN_CLK_BLK_PDP_PIXEL_CLK_IN_DIV   (0x0658)

#define PCI_ODIN_CORE_REG_SIZE                  (0x1000)

/* Odin - Device Under Test (DUT) register bar */
#define PCI_ODIN_DUT_REGS_BAR (2)
/* Odin - Device Under Test (DUT) memory bar */
#define PCI_ODIN_DUT_MEM_BAR  (4)

/* Odin clock related infos */
#define PCI_ODIN_INPUT_CLOCK_SPEED              (100000000U)
#define PCI_ODIN_INPUT_CLOCK_SPEED_MIN          (10000000U)
#define PCI_ODIN_INPUT_CLOCK_SPEED_MAX          (933000000U)
#define PCI_ODIN_OUTPUT_CLOCK_SPEED_MIN         (4690000U)
#define PCI_ODIN_OUTPUT_CLOCK_SPEED_MAX         (933000000U)
#define PCI_ODIN_VCO_MIN                        (600000000U)
#define PCI_ODIN_VCO_MAX                        (1440000000U)
#define PCI_ODIN_PFD_MIN                        (10000000U)
#define PCI_ODIN_PFD_MAX                        (500000000U)
/*
 * Max values that can be set in DRP registers771
 */
#define PCI_ODIN_OREG_VALUE_MAX                 (126.875f)
#define PCI_ODIN_MREG_VALUE_MAX                 (126.875f)
#define PCI_ODIN_DREG_VALUE_MAX                 (126U)

/*
 * DUT core clock input divider, multiplier and out divider.
 */
#define ODN_DUT_CORE_CLK_OUT_DIVIDER1                (0x0028)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER1_HI_TIME_MASK   (0x00000FC0U)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER1_HI_TIME_SHIFT  (6)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER1_LO_TIME_MASK   (0x0000003FU)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER1_LO_TIME_SHIFT  (0)

#define ODN_DUT_CORE_CLK_OUT_DIVIDER2                (0x002C)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER2_EDGE_MASK      (0x00000080U)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER2_EDGE_SHIFT     (7)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER2_NOCOUNT_MASK   (0x00000040U)
#define ODN_DUT_CORE_CLK_OUT_DIVIDER2_NOCOUNT_SHIFT  (6)

#define ODN_DUT_CORE_CLK_MULTIPLIER1                 (0x0050)
#define ODN_DUT_CORE_CLK_MULTIPLIER1_HI_TIME_MASK    (0x00000FC0U)
#define ODN_DUT_CORE_CLK_MULTIPLIER1_HI_TIME_SHIFT   (6)
#define ODN_DUT_CORE_CLK_MULTIPLIER1_LO_TIME_MASK    (0x0000003FU)
#define ODN_DUT_CORE_CLK_MULTIPLIER1_LO_TIME_SHIFT   (0)

#define ODN_DUT_CORE_CLK_MULTIPLIER2                 (0x0054)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_FRAC_MASK       (0x00007000U)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_FRAC_SHIFT      (12)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_FRAC_EN_MASK    (0x00000800U)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_FRAC_EN_SHIFT   (11)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_EDGE_MASK       (0x00000080U)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_EDGE_SHIFT      (7)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_NOCOUNT_MASK    (0x00000040U)
#define ODN_DUT_CORE_CLK_MULTIPLIER2_NOCOUNT_SHIFT   (6)

#define ODN_DUT_CORE_CLK_IN_DIVIDER1                 (0x0058)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_EDGE_MASK       (0x00002000U)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_EDGE_SHIFT      (13)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_NOCOUNT_MASK    (0x00001000U)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_NOCOUNT_SHIFT   (12)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_HI_TIME_MASK    (0x00000FC0U)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_HI_TIME_SHIFT   (6)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_LO_TIME_MASK    (0x0000003FU)
#define ODN_DUT_CORE_CLK_IN_DIVIDER1_LO_TIME_SHIFT   (0)

/*
 * DUT interface clock input divider, multiplier and out divider.
 */
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER1               (0x0220)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER1_HI_TIME_MASK  (0x00000FC0U)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER1_HI_TIME_SHIFT (6)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER1_LO_TIME_MASK  (0x0000003FU)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER1_LO_TIME_SHIFT (0)

#define ODN_DUT_IFACE_CLK_OUT_DIVIDER2               (0x0224)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER2_EDGE_MASK     (0x00000080U)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER2_EDGE_SHIFT    (7)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER2_NOCOUNT_MASK  (0x00000040U)
#define ODN_DUT_IFACE_CLK_OUT_DIVIDER2_NOCOUNT_SHIFT (6)

#define ODN_DUT_IFACE_CLK_MULTIPLIER1                (0x0250)
#define ODN_DUT_IFACE_CLK_MULTIPLIER1_HI_TIME_MASK   (0x00000FC0U)
#define ODN_DUT_IFACE_CLK_MULTIPLIER1_HI_TIME_SHIFT  (6)
#define ODN_DUT_IFACE_CLK_MULTIPLIER1_LO_TIME_MASK   (0x0000003FU)
#define ODN_DUT_IFACE_CLK_MULTIPLIER1_LO_TIME_SHIFT  (0)

#define ODN_DUT_IFACE_CLK_MULTIPLIER2                (0x0254)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_FRAC_MASK      (0x00007000U)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_FRAC_SHIFT     (12)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_FRAC_EN_MASK   (0x00000800U)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_FRAC_EN_SHIFT  (11)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_EDGE_MASK      (0x00000080U)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_EDGE_SHIFT     (7)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_NOCOUNT_MASK   (0x00000040U)
#define ODN_DUT_IFACE_CLK_MULTIPLIER2_NOCOUNT_SHIFT  (6)

#define ODN_DUT_IFACE_CLK_IN_DIVIDER1                (0x0258)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_EDGE_MASK      (0x00002000U)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_EDGE_SHIFT     (13)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_NOCOUNT_MASK   (0x00001000U)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_NOCOUNT_SHIFT  (12)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_HI_TIME_MASK   (0x00000FC0U)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_HI_TIME_SHIFT  (6)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_LO_TIME_MASK   (0x0000003FU)
#define ODN_DUT_IFACE_CLK_IN_DIVIDER1_LO_TIME_SHIFT  (0)


#define NEXEF_ROGUE_REG_BAR (PCI_ODIN_DUT_REGS_BAR)
#define NEXEF_ROGUE_REG_SIZE (_RGXREG_SIZE)
#define NEXEF_ROGUE_REG_OFFSET (_RGXREG_START)
#define NEXEF_NNPU_PDEV_NAME "rogue-regs"

#define NEXEF_NNA_REG_BAR (PCI_ODIN_DUT_REGS_BAR)
#define NEXEF_NNA_REG_SIZE (_REG_NNA_SIZE)
#define NEXEF_NNA_REG_OFFSET (_REG_NNA_START)
#define NEXEF_NNA_PDEV_NAME "nna-regs"

#define NEXEF_NNSYS_REG_BAR (PCI_ODIN_DUT_REGS_BAR)
#define NEXEF_NNSYS_REG_SIZE (_REG_NNSYS_SIZE)
#define NEXEF_NNSYS_REG_OFFSET (_REG_NNSYS_START)

#define NEXEF_NNPU_HEAP_SIZE (128*1024*1024)

//endregion Defines

//region Struct and Prototypes

static const struct pci_device_id pci_pci_ids[] = {
        /* We don't support the Apollo/TCF board, but we request for it to display a nice
         * friendly error message
         */
        { PCI_DEVICE(PCI_APOLLO_VENDOR_ID, PCI_APOLLO_DEVICE_ID), },

        /* There is currently no plan to use the Orion/Sirius platform, but I still can use
         * it to do some test. It is really close to the Odin/Sleipnir platform
         */
        { PCI_DEVICE(PCI_SIRIUS_VENDOR_ID, PCI_SIRIUS_DEVICE_ID), },

        { PCI_DEVICE(PCI_ODIN_VENDOR_ID, PCI_ODIN_DEVICE_ID), },
        { 0, }
};
MODULE_DEVICE_TABLE(pci, pci_pci_ids);

/* We need the NNA reg bank because the secure bit is currently not handled by the NNA driver */
enum { CORE_REG_BANK = 0, NNSYS_REG_BANK,
        NNA_REG_BANK,
        REG_BANK_COUNT /* Must be the last */};

struct mem_region {
    resource_size_t base;
    resource_size_t size;
};

struct platdev_export_info {
    /* General infos */
    struct mem_region dut_mem;

    /* Rogue export infos */
    int rogue_mem_mode;
    struct platform_device *rogue_pdev;
    struct mem_region rogue_heap_mem;
    struct mem_region rogue_pdp_heap_mem;

    /* NNA export infos */
    struct platform_device *nna_pdev;
    struct mem_region nna_heap_mem;
};

struct nexefdrv_prvdata {
    int irq;

    struct {
        int bar;
        unsigned long addr;
        unsigned long size;
        void __iomem *km_addr;
    } reg_bank[REG_BANK_COUNT];

    struct platdev_export_info plat_exports;

    struct pci_dev *pci_dev;
};

struct img_pci_driver {
    struct pci_dev *pci_dev;
    struct pci_driver pci_driver;
    struct delayed_work irq_work;
};

static ulong maxmapsizeMB = (sizeof(void *) == 4) ? 400 : 1024;


static int nexef_plat_probe(struct pci_dev *pci_dev,
                          const struct pci_device_id *id);
static void nexef_plat_remove(struct pci_dev *dev);

static int nexef_plat_suspend(struct device *dev);
static int nexef_plat_resume(struct device *dev);

static SIMPLE_DEV_PM_OPS(nexef_pm_plat_ops,
        nexef_plat_suspend, nexef_plat_resume);


static int nexef_register_rogue_plat_device(struct nexefdrv_prvdata *priv_data);
static void nexef_unregister_rogue_plat_device(struct nexefdrv_prvdata *priv_data);

static int nexef_register_nna_plat_device(struct nexefdrv_prvdata *priv_data);
static void nexef_unregister_nna_plat_device(struct nexefdrv_prvdata *priv_data);

static int nexef_nnsys_init(struct pci_dev *pci_dev, struct nexefdrv_prvdata *priv_data);
static void nexef_nnsys_unlock(struct nexefdrv_prvdata *priv_data);
static void nexef_nnsys_configure(struct nexefdrv_prvdata *priv_data);

static int nexef_nna_init(struct pci_dev *pci_dev, struct nexefdrv_prvdata *priv_data);
static void nexef_nna_unlock(struct nexefdrv_prvdata *priv_data);


//endregion Struct and Prototypes

//region Kernel module parameters

/* Parameters applicable when using bus master mode */
static unsigned long contig_phys_start;
module_param(contig_phys_start, ulong, 0444);
MODULE_PARM_DESC(contig_phys_start, "Physical address of start of contiguous region");

static uint32_t contig_size;
module_param(contig_size, uint, 0444);
MODULE_PARM_DESC(contig_size, "Size of contiguous region: takes precedence over any PCI based memory");

static unsigned long pci_size;
module_param(pci_size, ulong, 0444);
MODULE_PARM_DESC(pci_size, "physical size in bytes. when 0 (the default), use all memory in the PCI bar");

static unsigned long pci_offset;
module_param(pci_offset, ulong, 0444);
MODULE_PARM_DESC(pci_offset, "offset from PCI bar start. (default: 0)");

#ifdef CONFIG_SET_FPGA_CLOCK
static int odin_fpga_dut_clock = 25000000;
module_param(odin_fpga_dut_clock, int, 0444);
MODULE_PARM_DESC(odin_fpga_dut_clock, "DUT clock speed");

static int odin_fpga_mem_clock = 25000000;
module_param(odin_fpga_mem_clock, int, 0444);
MODULE_PARM_DESC(odin_fpga_mem_clock, "Memory clock speed");
#endif

static ssize_t info_show(struct device_driver *drv, char *buf)
{
    return sprintf(buf, "PCI 3NX-F Platform driver version : N/A\n");
}

static DRIVER_ATTR_RO(info);
static struct attribute *drv_attrs[] = {
        &driver_attr_info.attr,
        NULL
};
ATTRIBUTE_GROUPS(drv);

static struct img_pci_driver nexef_pci_drv = {
        .pci_driver = {
                .name = "nexef_plat_pci",
                .id_table = pci_pci_ids,
                .probe = nexef_plat_probe,
                .remove = nexef_plat_remove,
                .driver = {
                        .groups = drv_groups,
                        .pm = &nexef_pm_plat_ops,
                }
        },
};
//endregion Kernel module parameters

//region Utility functions
/*
 * __readreg32 - Generic PCI bar read functions
 */
static inline unsigned int __readreg32(struct nexefdrv_prvdata *data,
                                       int bank, unsigned long offset)
{
    void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
                                         offset);
    return ioread32(reg);
}

/*
 * __writereg32 - Generic PCI bar write functions
 */
static inline void __writereg32(struct nexefdrv_prvdata *data,
                                int bank, unsigned long offset, int val)
{
    void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
                                         offset);
    iowrite32(val, reg);
}

/*
 * __readreg64 - Generic PCI bar read functions
 */
static inline uint64_t __readreg64(struct nexefdrv_prvdata *data,
                                       int bank, unsigned long offset)
{
    void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
                                         offset);
    return (uint64_t)ioread32(reg) | ((uint64_t)ioread32(reg + 4) << 32);
}

/*
 * __writereg64 - Generic PCI bar write functions
 */
static inline void __writereg64(struct nexefdrv_prvdata *data,
                                int bank, unsigned long offset, uint64_t val)
{
    void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
                                         offset);
    iowrite32(val & 0xFFFFFFFF, reg);
    iowrite32(val >> 32, reg + 4);
}

//endregion Utility functions

//region Odin handling functions
/*
 * odin_core_writereg32 - Write to Odin control registers
 */
static inline void odin_core_writereg32(struct nexefdrv_prvdata *data,
                                        unsigned long offset, int val)
{
    __writereg32(data, CORE_REG_BANK, offset, val);
}

/*
 * odin_core_readreg32 - Read Odin control registers
 */
static inline unsigned int odin_core_readreg32(struct nexefdrv_prvdata *data,
                                               unsigned long offset)
{
    return __readreg32(data, CORE_REG_BANK, offset);
}

static inline unsigned int odin_core_polreg32(struct nexefdrv_prvdata *data, unsigned long offset, uint32_t mask)
{
    int timeout = 50;
    uint32_t read_value;

    while(timeout > 0)
    {
        read_value = odin_core_readreg32(data, offset) & mask;

        if (read_value != 0)
            break;

        msleep(20);

        timeout--;
    }

    if (timeout == 0)
    {
        dev_err(&data->pci_dev->dev, " %s(%08lX, %08X) timeout\n", __func__, offset, mask);
        return -ETIME;
    }

    return 0;
}

static void odin_set_mem_mode_lma(struct nexefdrv_prvdata *data)
{
    uint32_t val;

    /* Enable memory offset to be applied to DUT and PDP1 */
    /*
     * 31: Set Enable DUT Offset
     * 11: JTAG EN
     * 9 CORE CLK DIV4
     * 4 PLL_BYPASS
     */
    odin_core_writereg32(data, PCI_ODIN_CORE_DUT_CTRL1, 0x80000A10);

    /* Apply memory offset to GPU and PDP1 to point to DDR memory.
     * Enable HDMI.
     */
    val = (0x4 << 24) | /* DUT_OFFSET */
          (0x4 << 16) | /* PDP1_OFFSET */
          (0x2 << 10) | /* HDMI Module Enable */
          (0x1 << 13);  /* MCU Communicator */
    odin_core_writereg32(data, PCI_ODIN_CORE_CORE_CONTROL, val);
}

/*
 * reset_dut - Reset the Device Under Test
 */
static void reset_dut(struct nexefdrv_prvdata *data)
{

    uint32_t internal_rst = odin_core_readreg32(data, PCI_ODIN_CORE_INTERNAL_RESETN);
    uint32_t external_rst = odin_core_readreg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN);

    dev_dbg(&data->pci_dev->dev, "going to reset DUT fpga!\n");

    odin_core_writereg32(data, PCI_ODIN_CORE_INTERNAL_RESETN,
                         internal_rst & ~(INTERNAL_RESET_INTERNAL_RESETN_PIKE));
    odin_core_writereg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN,
                         external_rst & ~(EXTERNAL_RESET_EXTERNAL_RESETN_DUT));

    udelay(50); /* arbitrary delays, just in case! */

    odin_core_writereg32(data, PCI_ODIN_CORE_INTERNAL_RESETN, internal_rst);
    odin_core_writereg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN, external_rst);

    udelay(50);

    nexef_nnsys_unlock(data);

    /* Call the NNA unlock function to make sure NNA driver can access it without any issue
     * The security bit is reverted after each reset.
     */
    nexef_nna_unlock(data);

    dev_dbg(&data->pci_dev->dev, "DUT fpga reset done!\n");
}

#ifdef CONFIG_SET_FPGA_CLOCK
/*
 * Returns the divider group register fields for the specified counter value.
 * See Xilinx Application Note xapp888.
 */
static void odin_mmcm_reg_param_calc(uint32_t value, uint32_t *low, uint32_t *high,
                                     uint32_t *edge, uint32_t *no_count)
{
    if (value == 1U) {
        *no_count = 1U;
        *edge = 0;
        *high = 0;
        *low = 0;
    } else {
        *no_count = 0;
        *edge = value % 2U;
        *high = value >> 1;
        *low = (value + *edge) >> 1U;
    }
}

/* GPU clock functions use these macros: */
#define REG_FIELD_GET(v, str) \
	(uint32_t)(((v) & (s##_MASK)) >> (s##_SHIFT))
#define REG_FIELD_SET(v, f, str) \
	v = (uint32_t)(((v) & (uint32_t)~(str##_MASK)) | \
		  (uint32_t)(((f) << (str##_SHIFT)) & (str##_MASK)))

/*
 * Returns the MMCM Input Divider, FB Multiplier and Output Divider values for
 * the specified input frequency and target output frequency.
 * Function doesn't support fractional values for multiplier and output divider
 * As per Xilinx 7 series FPGAs clocking resources user guide, aims for highest
 * VCO and smallest D and M.
 * Configured for Xilinx Virtex7 speed grade 2.
 */
static int odin_mmcm_counter_calc(struct device *dev,
                                  uint32_t freq_input, uint32_t freq_output,
                                  uint32_t *d, uint32_t *m, uint32_t *o)
{
    uint32_t d_min, d_max;
    uint32_t m_min, m_max, m_ideal;
    uint32_t d_cur, m_cur, o_cur;
    uint32_t best_diff, d_best, m_best, o_best;

    /*
     * Check specified input frequency is within range
     */
    if (freq_input < PCI_ODIN_INPUT_CLOCK_SPEED_MIN) {
        dev_err(dev, "Input frequency (%u hz) below minimum supported value (%u hz)\n",
                freq_input, PCI_ODIN_INPUT_CLOCK_SPEED_MIN);
        return -EINVAL;
    }
    if (freq_input > PCI_ODIN_INPUT_CLOCK_SPEED_MAX) {
        dev_err(dev, "Input frequency (%u hz) above maximum supported value (%u hz)\n",
                freq_input, PCI_ODIN_INPUT_CLOCK_SPEED_MAX);
        return -EINVAL;
    }

    /*
     * Check specified target frequency is within range
     */
    if (freq_output < PCI_ODIN_OUTPUT_CLOCK_SPEED_MIN) {
        dev_err(dev, "Output frequency (%u hz) below minimum supported value (%u hz)\n",
                freq_input, PCI_ODIN_OUTPUT_CLOCK_SPEED_MIN);
        return -EINVAL;
    }
    if (freq_output > PCI_ODIN_OUTPUT_CLOCK_SPEED_MAX) {
        dev_err(dev, "Output frequency (%u hz) above maximum supported value (%u hz)\n",
                freq_output, PCI_ODIN_OUTPUT_CLOCK_SPEED_MAX);
        return -EINVAL;
    }

    /*
     * Calculate min and max for Input Divider.
     * Refer Xilinx 7 series FPGAs clocking resources user guide
     * equation 3-6 and 3-7
     */
    d_min = DIV_ROUND_UP(freq_input, PCI_ODIN_PFD_MAX);
    d_max = min(freq_input/PCI_ODIN_PFD_MIN, (uint32_t)PCI_ODIN_DREG_VALUE_MAX);

    /*
     * Calculate min and max for Input Divider.
     * Refer Xilinx 7 series FPGAs clocking resources user guide.
     * equation 3-8 and 3-9
     */
    m_min = DIV_ROUND_UP((PCI_ODIN_VCO_MIN * d_min), freq_input);
    m_max = min(((PCI_ODIN_VCO_MAX * d_max) / freq_input),
                (uint32_t)PCI_ODIN_MREG_VALUE_MAX);

    for (d_cur = d_min; d_cur <= d_max; d_cur++) {
        /*
         * Refer Xilinx 7 series FPGAs clocking resources user guide.
         * equation 3-10
         */
        m_ideal = min(((d_cur * PCI_ODIN_VCO_MAX)/freq_input), m_max);

        for (m_cur = m_ideal; m_cur >= m_min; m_cur -= 1) {
            /**
             * Skip if VCO for given 'm' and 'd' value is not an
             * integer since fractional component is not supported
             */
            if (((freq_input * m_cur) % d_cur) != 0)
                continue;

            /**
             * Skip if divider for given 'm' and 'd' value is not
             * an integer since fractional component is not
             * supported
             */
            if ((freq_input * m_cur) % (d_cur * freq_output) != 0)
                continue;

            /**
             * Calculate output divider value.
             */
            o_cur = (freq_input * m_cur)/(d_cur * freq_output);

            *d = d_cur;
            *m = m_cur;
            *o = o_cur;
            return 0;
        }
    }

    /* Failed to find exact optimal solution with high VCO. Brute-force find a suitable config,
     * again prioritising high VCO, to get lowest jitter */
    d_min = 1; d_max = (uint32_t)PCI_ODIN_DREG_VALUE_MAX;
    m_min = 1; m_max = (uint32_t)PCI_ODIN_MREG_VALUE_MAX;
    best_diff = 0xFFFFFFFF;

    for (d_cur = d_min; d_cur <= d_max; d_cur++) {
        for (m_cur = m_max; m_cur >= m_min; m_cur -= 1) {
            uint32_t pfd, vco, o_avg, o_min, o_max;

            pfd = freq_input / d_cur;
            vco = pfd * m_cur;

            if (pfd < PCI_ODIN_PFD_MIN)
                continue;

            if (pfd > PCI_ODIN_PFD_MAX)
                continue;

            if (vco < PCI_ODIN_VCO_MIN)
                continue;

            if (vco > PCI_ODIN_VCO_MAX)
                continue;

            /* A range of -1/+3 around o_avg gives us 100kHz granularity. It can be extended further. */
            o_avg = vco / freq_output;
            o_min = (o_avg >= 2) ? (o_avg - 1) : 1;
            o_max = o_avg + 3;
            if (o_max > (uint32_t)PCI_ODIN_OREG_VALUE_MAX)
                o_max = (uint32_t)PCI_ODIN_OREG_VALUE_MAX;

            for (o_cur = o_min; o_cur <= o_max; o_cur++) {
                uint32_t freq_cur, diff_cur;

                freq_cur = vco / o_cur;

                if (freq_cur > freq_output)
                    continue;

                diff_cur = freq_output - freq_cur;

                if (diff_cur == 0) {
                    /* Found an exact match */
                    *d = d_cur;
                    *m = m_cur;
                    *o = o_cur;
                    return 0;
                }

                if (diff_cur < best_diff) {
                    best_diff = diff_cur;
                    d_best = d_cur;
                    m_best = m_cur;
                    o_best = o_cur;
                }
            }
        }
    }

    if (best_diff != 0xFFFFFFFF) {
        dev_warn(dev, "Odin: Found similar freq of %u Hz\n", freq_output - best_diff);
        *d = d_best;
        *m = m_best;
        *o = o_best;
        return 0;
    }

    dev_err(dev, "Odin: Unable to find integer values for d, m and o for requested frequency (%u)\n",
            freq_output);

    return -ERANGE;
}

static int odin_set_dut_core_clk(struct nexefdrv_prvdata *data, uint32_t input_clk, uint32_t output_clk)
{
    int err = 0;
    uint32_t in_div, mul, out_div;
    uint32_t high_time, low_time, edge, no_count;
    uint32_t value;
    struct device *dev = &data->pci_dev->dev;

    err = odin_mmcm_counter_calc(dev, input_clk, output_clk, &in_div,
                                 &mul, &out_div);
    if (err != 0)
        return err;

    /* Put DUT into reset */
    odin_core_writereg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN, EXTERNAL_RESET_EXTERNAL_RESETN_SPI);
    msleep(20);

    /* Put DUT Core MMCM into reset */
    odin_core_writereg32(data, PCI_ODIN_CORE_CLK_GEN_RESET, EXTERNAL_RESET_DUT_CORE_MMCM);
    msleep(20);

    /* Calculate the register fields for output divider */
    odin_mmcm_reg_param_calc(out_div, &high_time, &low_time,
                             &edge, &no_count);

    /* Read-modify-write the required fields to output divider register 1 */
    value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV1);
    REG_FIELD_SET(value, high_time,
                  ODN_DUT_CORE_CLK_OUT_DIVIDER1_HI_TIME);
    REG_FIELD_SET(value, low_time,
                  ODN_DUT_CORE_CLK_OUT_DIVIDER1_LO_TIME);
    odin_core_writereg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV1, value);

    /* Read-modify-write the required fields to output divider register 2 */
    value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV2);
    REG_FIELD_SET(value, edge,
                  ODN_DUT_CORE_CLK_OUT_DIVIDER2_EDGE);
    REG_FIELD_SET(value, no_count,
                  ODN_DUT_CORE_CLK_OUT_DIVIDER2_NOCOUNT);
    odin_core_writereg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV2, value);

    /* Calculate the register fields for multiplier */
    odin_mmcm_reg_param_calc(mul, &high_time, &low_time,
                             &edge, &no_count);

    /* Read-modify-write the required fields to multiplier register 1*/
    value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT1);
    REG_FIELD_SET(value, high_time,
                  ODN_DUT_CORE_CLK_MULTIPLIER1_HI_TIME);
    REG_FIELD_SET(value, low_time,
                  ODN_DUT_CORE_CLK_MULTIPLIER1_LO_TIME);
    odin_core_writereg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT1, value);

    /* Read-modify-write the required fields to multiplier register 2 */
    value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT2);
    REG_FIELD_SET(value, edge,
                  ODN_DUT_CORE_CLK_MULTIPLIER2_EDGE);
    REG_FIELD_SET(value, no_count,
                  ODN_DUT_CORE_CLK_MULTIPLIER2_NOCOUNT);
    odin_core_writereg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT2, value);

    /* Calculate the register fields for input divider */
    odin_mmcm_reg_param_calc(in_div, &high_time, &low_time,
                             &edge, &no_count);

    /* Read-modify-write the required fields to input divider register 1 */
    value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_IN_DIV);
    REG_FIELD_SET(value, high_time,
                  ODN_DUT_CORE_CLK_IN_DIVIDER1_HI_TIME);
    REG_FIELD_SET(value, low_time,
                  ODN_DUT_CORE_CLK_IN_DIVIDER1_LO_TIME);
    REG_FIELD_SET(value, edge,
                  ODN_DUT_CORE_CLK_IN_DIVIDER1_EDGE);
    REG_FIELD_SET(value, no_count,
                  ODN_DUT_CORE_CLK_IN_DIVIDER1_NOCOUNT);
    odin_core_writereg32(data, PCI_ODIN_CLK_BLK_DUT_CORE_CLK_IN_DIV, value);

    /* Bring DUT clock MMCM out of reset */
    odin_core_writereg32(data, PCI_ODIN_CORE_CLK_GEN_RESET, 0);

    err = odin_core_polreg32(data, PCI_ODIN_CORE_MMCM_LOCK_STATUS, PCI_ODIN_MMCM_LOCK_STATUS_DUT_CORE);
    if (err != 0) {
        dev_err(dev, "MMCM failed to lock for DUT core\n");
        return err;
    }

    /* Bring DUT out of reset */
    odin_core_writereg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN,
            EXTERNAL_RESET_EXTERNAL_RESETN_SPI | EXTERNAL_RESET_EXTERNAL_RESETN_DUT);
    msleep(20);

    dev_info(dev, "DUT core clock set-up successful at %dHz\n", output_clk);

    return err;
}

static int odin_set_dut_if_clk(struct nexefdrv_prvdata *data, uint32_t input_clk, uint32_t output_clk)
{
	int err = 0;
	uint32_t in_div, mul, out_div;
	uint32_t high_time, low_time, edge, no_count;
	uint32_t value;
	struct device *dev = &tc->pdev->dev;

	err = odin_mmcm_counter_calc(dev, input_clk, output_clk,
				     &in_div, &mul, &out_div);
	if (err != 0)
		return err;

	/* Put DUT into reset */
	iowrite32(ODN_EXTERNAL_RESETN_DUT_SPI_MASK,
		  base + ODN_CORE_EXTERNAL_RESETN);
	msleep(20);

	/* Put DUT Core MMCM into reset */
	iowrite32(ODN_CLK_GEN_RESET_DUT_IF_MMCM_MASK,
		  base + ODN_CORE_CLK_GEN_RESET);
	msleep(20);

	/* Calculate the register fields for output divider */
	odin_mmcm_reg_param_calc(out_div, &high_time, &low_time,
				 &edge, &no_count);

	/* Read-modify-write the required fields to output divider register 1 */
	value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_MEM_CLK_OUT_DIV1);
	REG_FIELD_SET(value, high_time,
			ODN_DUT_IFACE_CLK_OUT_DIVIDER1_HI_TIME);
	REG_FIELD_SET(value, low_time,
			ODN_DUT_IFACE_CLK_OUT_DIVIDER1_LO_TIME);
	iowrite32(value, clk_blk_base + ODN_DUT_IFACE_CLK_OUT_DIVIDER1);

	/* Read-modify-write the required fields to output divider register 2 */
	value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_MEM_CLK_OUT_DIV2);
	REG_FIELD_SET(value, edge,
			ODN_DUT_IFACE_CLK_OUT_DIVIDER2_EDGE);
	REG_FIELD_SET(value, no_count,
			ODN_DUT_IFACE_CLK_OUT_DIVIDER2_NOCOUNT);
	iowrite32(value, clk_blk_base + ODN_DUT_IFACE_CLK_OUT_DIVIDER2);

	/* Calculate the register fields for multiplier */
	odin_mmcm_reg_param_calc(mul, &high_time, &low_time, &edge, &no_count);

	/* Read-modify-write the required fields to multiplier register 1*/
	value = odin_core_readreg32(data, PCI_ODIN_CLK_BLK_DUT_MEM_CLK_MUL);
	value = ioread32(clk_blk_base + ODN_DUT_IFACE_CLK_MULTIPLIER1);
	REG_FIELD_SET(value, high_time,
			ODN_DUT_IFACE_CLK_MULTIPLIER1_HI_TIME);
	REG_FIELD_SET(value, low_time,
			ODN_DUT_IFACE_CLK_MULTIPLIER1_LO_TIME);
	iowrite32(value, clk_blk_base + ODN_DUT_IFACE_CLK_MULTIPLIER1);

	/* Read-modify-write the required fields to multiplier register 2 */
	value = ioread32(clk_blk_base + ODN_DUT_IFACE_CLK_MULTIPLIER2);
	REG_FIELD_SET(value, edge,
			ODN_DUT_IFACE_CLK_MULTIPLIER2_EDGE);
	REG_FIELD_SET(value, no_count,
			ODN_DUT_IFACE_CLK_MULTIPLIER2_NOCOUNT);
	iowrite32(value, clk_blk_base + ODN_DUT_IFACE_CLK_MULTIPLIER2);

	/* Calculate the register fields for input divider */
	odin_mmcm_reg_param_calc(in_div, &high_time, &low_time,
				 &edge, &no_count);

	/* Read-modify-write the required fields to input divider register 1 */
	value = ioread32(clk_blk_base + ODN_DUT_IFACE_CLK_IN_DIVIDER1);
	REG_FIELD_SET(value, high_time,
			 ODN_DUT_IFACE_CLK_IN_DIVIDER1_HI_TIME);
	REG_FIELD_SET(value, low_time,
			 ODN_DUT_IFACE_CLK_IN_DIVIDER1_LO_TIME);
	REG_FIELD_SET(value, edge,
			 ODN_DUT_IFACE_CLK_IN_DIVIDER1_EDGE);
	REG_FIELD_SET(value, no_count,
			 ODN_DUT_IFACE_CLK_IN_DIVIDER1_NOCOUNT);
	iowrite32(value, clk_blk_base + ODN_DUT_IFACE_CLK_IN_DIVIDER1);

	/* Bring DUT interface clock MMCM out of reset */
	odin_core_writereg32(data, PCI_ODIN_CORE_CLK_GEN_RESET, 0);

    err = odin_core_polreg32(data, PCI_ODIN_CORE_MMCM_LOCK_STATUS, PCI_ODIN_MMCM_LOCK_STATUS_DUT_IF);
	if (err != 0) {
		dev_err(dev, "MMCM failed to lock for DUT IF\n");
		return err;
	}

	/* Bring DUT out of reset */
    odin_core_writereg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN,
            EXTERNAL_RESET_EXTERNAL_RESETN_SPI | EXTERNAL_RESET_EXTERNAL_RESETN_DUT);
	msleep(20);

	dev_info(dev, "DUT IF clock set-up successful at %dHz\n", output_clk);

	return err;
}
#endif

/*
 * odin_isr_clear - Clear an interrupt
 *
 *
 * note: the reason of that function is unclear, it is taken from Apollo/Atlas code that have
 * the same interrupt handler as Odin, is it because of a bug?
 */
static void odin_isr_clear(struct nexefdrv_prvdata *data, unsigned int intstatus)
{
    unsigned int max_retries = 1000;

    while ((odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_STATUS) & intstatus) && max_retries--)
        odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_CLR,
                             (INT_INTERRUPT_MASTER_ENABLE | intstatus));
}


typedef void (*interrupt_callback_handler)(void *);
struct interrupt_handlers {
    interrupt_callback_handler handler;
    void * data;
    uint8_t enabled;
};
static struct interrupt_handlers pdev_int_handlers[TC_INTERRUPT_COUNT];

/*
 * pci_isr_cb - Low latency interrupt handler
 */
static irqreturn_t pci_isr_cb(int irq, void *dev_id)
{
    uint32_t intstatus;

    struct pci_dev *pcidev = (struct pci_dev *)dev_id;
    struct nexefdrv_prvdata *data = dev_get_drvdata(&pcidev->dev);

    irqreturn_t ret = IRQ_NONE;

    if (dev_id == NULL) {
        /* Spurious interrupt: not yet initialised. */
        pr_warn("Spurious interrupt data/dev_id not initialised!\n");
        goto exit;
    }

    /* Read interrupt status register */
    intstatus = odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_STATUS);

    dev_dbg(&pcidev->dev,
             "%s: Got an interrupt....\n",
             __func__);

    /* Now handle the ints */
    if (intstatus & INT_INTERRUPT_DUT0) {
        /* Check who called and say hello */
        dev_dbg(&pcidev->dev,
                "%s: Got a valid interrupt, trying to do something with it....\n",
                __func__);

        /* Check NNA event register */
        if ( (__readreg32(data, NNA_REG_BANK, VHA_CR_OS0_VHA_EVENT_STATUS) != 0) &&
             pdev_int_handlers[TC_INTERRUPT_TC5_PDP].enabled ) {

            dev_dbg(&pcidev->dev,
                     "%s: NNA interrupt....\n",
                     __func__);

            if ( pdev_int_handlers[TC_INTERRUPT_TC5_PDP].handler != NULL ) {
                pdev_int_handlers[TC_INTERRUPT_TC5_PDP].handler(pdev_int_handlers[TC_INTERRUPT_TC5_PDP].data);
            }
            else {
                WARN_ON(pdev_int_handlers[TC_INTERRUPT_TC5_PDP].handler == NULL);
            }

        }
        else if (pdev_int_handlers[TC_INTERRUPT_EXT].enabled) {

            /* Else it must be from the NNPU */
            dev_dbg(&pcidev->dev,
                     "%s: Probably a NNPU interrupt....\n",
                     __func__);
            if ( pdev_int_handlers[TC_INTERRUPT_EXT].handler != NULL ) {
                pdev_int_handlers[TC_INTERRUPT_EXT].handler(pdev_int_handlers[TC_INTERRUPT_EXT].data);
            }
            else {
                WARN_ON(pdev_int_handlers[TC_INTERRUPT_EXT].handler == NULL);
            }

        }
        else {
            dev_warn(&pcidev->dev, "Received an interrupt from DUT when no proper handling being registered.");
        }
    }
    else {
        /* most likely this is a shared interrupt line */
        dev_dbg(&pcidev->dev,
                "%s: unexpected or spurious interrupt [%x] (shared IRQ?)!\n",
                __func__, intstatus);
        /* WARN_ON(1); */

        goto exit;
    }

    /* Ack the ints */
    odin_isr_clear(data, intstatus);

exit:
    return ret;
}

static inline void odin_reset_int(struct nexefdrv_prvdata *data) {
    odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE, 0);
    odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_CLR, 0xFFFFFFFF);
}

/*
 * odin_enable_int - Enable an interrupt
 */
static inline void odin_enable_int(struct nexefdrv_prvdata *data,
                                   uint32_t intmask)
{
    uint32_t irq_enabled = odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE);
    //intmask &= INT_INTERRUPT_DUT0;

    odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE, irq_enabled | intmask | INT_INTERRUPT_MASTER_ENABLE);
}

/*
 * odin_disable_int - Disable an interrupt
 */
static inline void odin_disable_int(struct nexefdrv_prvdata *data,
                                    uint32_t intmask)
{
    uint32_t irq_enabled = odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE);
    //intmask &= INT_INTERRUPT_DUT0;

    odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE,
                         irq_enabled & ~intmask);
}

/*
 * odin_allocate_registers - Allocate memory for a register (or memory) bank
 * @data: pointer to the data
 * @bank: bank to set
 * @bar: BAR where the register are
 * @base: base address in the BAR
 * @size: size of the register set
 */
static inline int odin_allocate_registers(struct pci_dev *pci_dev,
                                          struct nexefdrv_prvdata *data, int bank,
                                          int bar, unsigned long base, unsigned long size)
{
    unsigned long bar_size = pci_resource_len(pci_dev, bar);
    unsigned long bar_addr = pci_resource_start(pci_dev, bar);
    unsigned long bar_max_size = bar_size - base;
    BUG_ON((base > bar_size) || ((base+size) > bar_size));

    data->reg_bank[bank].bar = bar;
    data->reg_bank[bank].addr = bar_addr + base;
    data->reg_bank[bank].size = min(size, bar_max_size);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
    data->reg_bank[bank].km_addr = devm_ioremap_nocache(
            &pci_dev->dev, data->reg_bank[bank].addr,
            data->reg_bank[bank].size);
#else
    data->reg_bank[bank].km_addr = devm_ioremap(
            &pci_dev->dev, data->reg_bank[bank].addr,
            data->reg_bank[bank].size);
#endif

    dev_dbg(&pci_dev->dev, "[bank %u] bar:%d addr:0x%lx size:0x%lx km:0x%p\n",
             bank, bar, data->reg_bank[bank].addr,
             data->reg_bank[bank].size,
             data->reg_bank[bank].km_addr);

    return data->reg_bank[bank].km_addr == NULL;
}

//endregion Odin handling functions

//region Specific NNA handling functions

/* The function here are to handly the secure reigster from the NNA.
 * The NNA driver currently don't know how to handle it
 */
static int nexef_nna_init(struct pci_dev *pci_dev, struct nexefdrv_prvdata *priv_data)
{
    int ret = 0;
    struct device *dev = &pci_dev->dev;
    /* Allocate nna registers registers */

    ret = odin_allocate_registers(pci_dev, priv_data,
                                  NNA_REG_BANK, NEXEF_NNA_REG_BAR,
                                  NEXEF_NNA_REG_OFFSET, NEXEF_NNA_REG_SIZE);
    if (ret) {
        dev_err(dev, "Can't allocate memory for nna regs!");
        ret = -ENOMEM;
        goto out;
    }

out:
    return ret;
}

static void nexef_nna_unlock(struct nexefdrv_prvdata *priv_data)
{
    __writereg32(priv_data, NNA_REG_BANK, VHA_CR_SOCIF_BUS_SECURE, 0);
}


//endregion Specific NNA handling functions

//region NN_SYS related functions

static int nexef_nnsys_init(struct pci_dev *pci_dev, struct nexefdrv_prvdata *priv_data)
{
    int ret = 0;
    struct device *dev = &pci_dev->dev;

    /* Allocate nnsys registers registers */
    ret = odin_allocate_registers(pci_dev, priv_data,
                                  NNSYS_REG_BANK, NEXEF_NNSYS_REG_BAR,
                                  NEXEF_NNSYS_REG_OFFSET, NEXEF_NNSYS_REG_SIZE);
    if (ret) {
        dev_err(dev, "Can't allocate memory for nnsys regs!");
        ret = -ENOMEM;
        goto out;
    }

out:
    return ret;
}

static void nexef_nnsys_unlock(struct nexefdrv_prvdata *priv_data)
{
    __writereg32(priv_data, NNSYS_REG_BANK, NN_SYS_CR_SOCIF_BUS_SECURE, 0);
}

static void nexef_nnsys_configure(struct nexefdrv_prvdata *priv_data)
{
    /* Power up everything */
    __writereg32(priv_data, NNSYS_REG_BANK, NN_SYS_CR_POWER_EVENT,
                 NN_SYS_CR_POWER_EVENT_DOMAIN_NNSYS_EN | NN_SYS_CR_POWER_EVENT_REQUEST_POWER_UP | NN_SYS_CR_POWER_EVENT_TYPE_EN);
    __writereg32(priv_data, NNSYS_REG_BANK, NN_SYS_CR_POWER_EVENT,
            NN_SYS_CR_POWER_EVENT_DOMAIN_NNA_EN | NN_SYS_CR_POWER_EVENT_REQUEST_POWER_UP | NN_SYS_CR_POWER_EVENT_TYPE_EN);
    /* Doc talk about OCM power, but does not exist in the CR file ?! */

    /* Disable OCM */
    __writereg64(priv_data, NNSYS_REG_BANK, NN_SYS_CR_NOC_LOWER_ADDR1, 0xFFFFFFFF10000000);
    __writereg64(priv_data, NNSYS_REG_BANK, NN_SYS_CR_NOC_UPPER_ADDR1, 0xFFFFFFFFFFFFFFFF);
}

//endregion NN_SYS related functions

//region Kernel related functions

static int nexef_plat_probe(struct pci_dev *pci_dev,
                          const struct pci_device_id *id)
{
    int ret = 0;
    struct nexefdrv_prvdata *data;
    size_t maxmapsize = maxmapsizeMB * 1024 * 1024;
    unsigned long dut_base_mem, dut_mem_size;
    struct device *dev = &pci_dev->dev;

    dev_dbg(dev, "probing device, pci_dev: %p\n", dev);

    if (IS_APOLLO_DEVICE(id->device)) {
        dev_err(dev, "This driver can't work with an APOLLO baseboard. Please check the hardware you are using!\n");
        goto out;
    }

    if (IS_SIRIUS_DEVICE(id->device)) {
        dev_warn(dev, "This driver is not design to work on an Orion system. As it is really similar" \
        "to an Odin baseboard it may work or not. Use at your own risk.");
    }

    /* Enable the device */
    if (pci_enable_device(pci_dev))
        goto out;

    /* Reserve PCI I/O and memory resources */
    if (pci_request_region(pci_dev, 1, "odin-regs"))
        goto out_disable;

    /* Create a kernel space mapping for each of the bars */
    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data) {
        dev_err(dev, "Memory allocation error, aborting.\n");
        ret = -ENOMEM;
        goto out_release;
    }

    dev_dbg(dev, "allocated nexefdrv_prvdata @ %p\n", data);
    memset(data, 0, sizeof(*data));

    /* Allocate odin core registers */
    ret = odin_allocate_registers(pci_dev, data,
                                  CORE_REG_BANK, PCI_ODIN_SYS_CTRL_REGS_BAR,
                                  PCI_ODIN_SYS_CTRL_BASE_OFFSET,
                                  PCI_ODIN_CORE_REG_SIZE);
    if (ret) {
        dev_err(dev, "Can't allocate memory for odin regs!");
        ret = -ENOMEM;
        goto out_release;
    }

    /* Display some infos */
    {
        uint32_t odin_id  = odin_core_readreg32(data, PCI_ODIN_CORE_ID);
        uint32_t odin_rev = odin_core_readreg32(data, PCI_ODIN_CORE_REVISION);
        uint32_t odin_cs  = odin_core_readreg32(data, PCI_ODIN_CORE_CHANGE_SET);
        uint32_t odin_ui  = odin_core_readreg32(data, PCI_ODIN_CORE_USER_ID);
        uint32_t odin_ub  = odin_core_readreg32(data, PCI_ODIN_CORE_USER_BUILD);

        dev_info(dev, "Found Odin lite board v%d.%d (ID:%X CS:%X UI:%X UB:%X)",
                (odin_rev >> 8) & 0xF, odin_rev & 0xF, odin_id & 0x7, odin_cs, odin_ui, odin_ub);
    }

#ifdef CONFIG_SET_FPGA_CLOCK
    odin_set_dut_core_clk(data, PCI_ODIN_INPUT_CLOCK_SPEED, odin_fpga_dut_clock);

#endif
    /* Call NN_SYS init */
    ret = nexef_nnsys_init(pci_dev, data);
    if (ret) {
        dev_err(dev, "nnsys register allocation failed!\n");
        goto out_release;
    }

    ret = nexef_nna_init(pci_dev, data);
    if (ret) {
        dev_err(dev, "nna register allocation failed!\n");
        goto out_release;
    }

    /* Get DUT memory infos */
    dut_mem_size = pci_resource_len(pci_dev, PCI_ODIN_DUT_MEM_BAR);
    if (dut_mem_size > maxmapsize)
        dut_mem_size = maxmapsize;

    dut_base_mem = pci_resource_start(pci_dev, PCI_ODIN_DUT_MEM_BAR) +
                   pci_offset;

    /* change alloc size according to module parameter */
    if (pci_size)
        dut_mem_size = pci_size;

    dev_info(dev, "DUT Memory: bar: %d addr: 0x%lx size: 0x%lx\n",
             PCI_ODIN_DUT_MEM_BAR,
             dut_base_mem,
             dut_mem_size);

    /* Get the IRQ...*/
    data->irq = pci_dev->irq;
    data->pci_dev = pci_dev;
    dev_set_drvdata(&pci_dev->dev, data);
    nexef_pci_drv.pci_dev = pci_dev;

    dev_dbg(dev, "Going to reset DUT... (First time)\n");
    reset_dut(data);

    dev_dbg(dev, "Reseting interrupts\n");
    odin_reset_int(data);
    dev_dbg(dev, "Enabling interrupts\n");
    odin_enable_int(data, INT_INTERRUPT_DUT0 | INT_INTERRUPT_PDP);

    /*
     * Reset FPGA DUT only after disabling clocks in
     * vha_add_dev()-> get properties.
     * This workaround is required to ensure that
     * clocks (on daughter board) are enabled for test slave scripts to
     * read FPGA build version register.
     * NOTE: Asserting other bits like DDR reset bit cause problems
     * with bus mastering feature, thus results in memory failures.
     */
    dev_dbg(dev, "Going to reset DUT... (Second time)\n");
    reset_dut(data);

    odin_set_mem_mode_lma(data);

    /* Configure NN_SYS */
    dev_info(dev, "Configuring NN_SYS\n");
    nexef_nnsys_configure(data);

    /* Install the ISR callback...*/
    dev_dbg(dev, "Trying to insert IRQ handler\n");
    ret = devm_request_irq(dev, data->irq, &pci_isr_cb, IRQF_SHARED, DEVICE_NAME,
                           (void *)pci_dev);
    if (ret) {
        dev_err(dev, "failed to request irq!\n");
        goto out_disable_int;
    }
    dev_dbg(dev, "registered irq %d\n", data->irq);

    /* Fill in export infos */
    data->plat_exports.dut_mem.base = dut_base_mem;
    data->plat_exports.dut_mem.size = dut_mem_size;
    /* Set NNPU parameters */
    data->plat_exports.rogue_mem_mode = TC_MEMORY_LOCAL;
    data->plat_exports.rogue_heap_mem.base = data->plat_exports.dut_mem.base;
    data->plat_exports.rogue_heap_mem.size = NEXEF_NNPU_HEAP_SIZE;
    data->plat_exports.rogue_pdp_heap_mem.base = data->plat_exports.rogue_heap_mem.base +
            data->plat_exports.rogue_heap_mem.size;
    data->plat_exports.rogue_pdp_heap_mem.size = (data->plat_exports.dut_mem.size / 2) -
            data->plat_exports.rogue_heap_mem.size;

    data->plat_exports.nna_heap_mem.base = data->plat_exports.rogue_pdp_heap_mem.base +
            data->plat_exports.rogue_pdp_heap_mem.size;
    data->plat_exports.nna_heap_mem.size = (data->plat_exports.dut_mem.size / 2);

    dev_info(dev, "DUT Memory regions:\n");
    dev_info(dev, "DUT Mem  : %08llx-%08llx (size: %08llx)\n",
             data->plat_exports.dut_mem.base,
             data->plat_exports.dut_mem.base + data->plat_exports.dut_mem.size,
             data->plat_exports.dut_mem.size);
    dev_info(dev, "NNPU heap: %08llx-%08llx (size: %08llx)\n",
            data->plat_exports.rogue_heap_mem.base,
            data->plat_exports.rogue_heap_mem.base + data->plat_exports.rogue_heap_mem.size,
             data->plat_exports.rogue_heap_mem.size);
    dev_info(dev, "NNPU pdp : %08llx-%08llx (size: %08llx)\n",
             data->plat_exports.rogue_pdp_heap_mem.base,
             data->plat_exports.rogue_pdp_heap_mem.base + data->plat_exports.rogue_pdp_heap_mem.size,
             data->plat_exports.rogue_pdp_heap_mem.size);
    dev_info(dev, "NNA      : %08llx-%08llx (size: %08llx)\n",
             data->plat_exports.nna_heap_mem.base,
             data->plat_exports.nna_heap_mem.base + data->plat_exports.nna_heap_mem.size,
             data->plat_exports.nna_heap_mem.size);

    /* We now are ready to create the platform drivers */
    ret = nexef_register_rogue_plat_device(data);
    if (ret) {
        dev_err(dev, "cannot create NNPU platform device!\n");
        goto out_disable_int;
    }

    ret = nexef_register_nna_plat_device(data);
    if (ret) {
        dev_err(dev, "cannot create NNA platform device!\n");
        goto out_disable_int;
    }

    return ret;

out_disable_int:
    /* Make sure int are no longer enabled */
    odin_disable_int(data, INT_INTERRUPT_DUT0);

out_release:
    pci_release_regions(pci_dev);

out_disable:
    pci_disable_device(pci_dev);

out:
    return ret;
}

//region nn_sys related functions

//endregion nn_sys related functions

static void nexef_plat_remove(struct pci_dev *pcidev)
{
    struct nexefdrv_prvdata *priv_data = dev_get_drvdata(&pcidev->dev);

    dev_dbg(&pcidev->dev, "removing device\n");

    if (priv_data == NULL) {
        dev_err(&pcidev->dev, "PCI priv data missing!\n");
    } else {
        /*
         * We  need to disable interrupts for the
         * embedded device via the fpga interrupt controller...
         */
        odin_disable_int(priv_data, INT_INTERRUPT_DUT0);

        /* Unregister int */
        devm_free_irq(&pcidev->dev, priv_data->irq, pcidev);

        /* Unregister all potential platoform device allocated */
        nexef_unregister_rogue_plat_device(priv_data);
        nexef_unregister_nna_plat_device(priv_data);
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
	/* Release any declared mem regions */
	dma_release_declared_memory(&pcidev->dev);
#endif
    pci_release_regions(pcidev);
    pci_disable_device(pcidev);

    /* Just to make sure */
    nexef_pci_drv.pci_dev = NULL;
}

#ifdef CONFIG_PM
static int nexef_plat_suspend(struct device *dev)
{
    /* nothing, for now, to be done here */
	return 0;
}

static int nexef_plat_resume(struct device *dev)
{
    /* nothing, for now, to be done here */
	return 0;
}
#endif

static int nexef_plat_init(void)
{
    int ret;

    ret = pci_register_driver(&nexef_pci_drv.pci_driver);
    if (ret) {
        pr_err("failed to register PCI driver!\n");
        return ret;
    }

    /* pci_dev should be set in probe */
    if (!nexef_pci_drv.pci_dev) {
        pr_err("failed to find compatible NeXeF PCI device!\n");
        pci_unregister_driver(&nexef_pci_drv.pci_driver);
        return -ENODEV;
    }

    return 0;
}

static void nexef_plat_exit(void)
{
    /* Not sure we have thing to be done here... */
    if (nexef_pci_drv.pci_dev) {
        pci_unregister_driver(&nexef_pci_drv.pci_driver);
    }
}

module_init(nexef_plat_init);
module_exit(nexef_plat_exit);
MODULE_LICENSE("GPL");

//endregion Kernel related functions

//region NNPU needed exported functions

int tc_enable(struct device *dev);
void tc_disable(struct device *dev);
int tc_set_interrupt_handler(struct device *dev, int interrupt_id,
                             void (*handler_function)(void *), void *data);
int tc_enable_interrupt(struct device *dev, int interrupt_id);
int tc_disable_interrupt(struct device *dev, int interrupt_id);
int tc_sys_info(struct device *dev, uint32_t *tmp, uint32_t *pll);
int tc_sys_strings(struct device *dev,
                   char *str_fpga_rev, size_t size_fpga_rev,
                   char *str_tcf_core_rev, size_t size_tcf_core_rev,
                   char *str_tcf_core_target_build_id,
                   size_t size_tcf_core_target_build_id,
                   char *str_pci_ver, size_t size_pci_ver,
                   char *str_macro_ver, size_t size_macro_ver);
int tc_core_clock_speed(struct device *dev);

#define FUNC_IN() pr_debug(">>> %s():%d\n", __func__, __LINE__)

int tc_enable(struct device *dev)
{
    //struct pci_dev *pdev;
    FUNC_IN();
    //pdev = to_pci_dev(dev);

    return 0; //pci_enable_device(pdev);
}
EXPORT_SYMBOL(tc_enable);

void tc_disable(struct device *dev)
{

    //struct pci_dev *pdev;
    FUNC_IN();
    //pdev = to_pci_dev(dev);

    //pci_disable_device(pdev);
}
EXPORT_SYMBOL(tc_disable);

static char *int_names[] = {
    "PDP",
    "NNPU",
    "NNA"
};

int tc_set_interrupt_handler(struct device *dev, int interrupt_id,
                             void (*handler_function)(void *), void *data)
{
    int err = -1;

    FUNC_IN();

    if ( (interrupt_id >= 0) && (interrupt_id < TC_INTERRUPT_COUNT) ) {
        dev_info(dev, "Registering interrupt handler (%p) for %s [data: %p]", handler_function, int_names[interrupt_id], data);
        err = 0;

        pdev_int_handlers[interrupt_id].handler = handler_function;
        pdev_int_handlers[interrupt_id].data = data;
        pdev_int_handlers[interrupt_id].enabled = 0;
    }
    else {
        dev_warn(dev, "%s: Invalid interrupt id %d!", __func__, interrupt_id);
    }

    return err;
}
EXPORT_SYMBOL(tc_set_interrupt_handler);

int tc_enable_interrupt(struct device *dev, int interrupt_id)
{
    int err = -1;

    FUNC_IN();

    if ( (interrupt_id >= 0) && (interrupt_id < TC_INTERRUPT_COUNT) ) {
        dev_info(dev, "Enabling interrupt handler for %s\n", int_names[interrupt_id]);

        err = 0;
        pdev_int_handlers[interrupt_id].enabled = 1;
    }
    else {
        dev_warn(dev, "%s: Invalid interrupt id %d!", __func__, interrupt_id);
    }

    return err;
}
EXPORT_SYMBOL(tc_enable_interrupt);

int tc_disable_interrupt(struct device *dev, int interrupt_id)
{
    int err = -1;

    FUNC_IN();

    if ( (interrupt_id >= 0) && (interrupt_id < TC_INTERRUPT_COUNT) ) {
        dev_info(dev, "Disabling interrupt handler for %s\n", int_names[interrupt_id]);

        err = 0;
        pdev_int_handlers[interrupt_id].enabled = 0;
    }
    else {
        dev_warn(dev, "%s: Invalid interrupt id %d!", __func__, interrupt_id);
    }

    return err;
}
EXPORT_SYMBOL(tc_disable_interrupt);

int tc_sys_info(struct device *dev, uint32_t *tmp, uint32_t *pll)
{
    *tmp = 0;
    *pll = 0;
    return 0;
}
EXPORT_SYMBOL(tc_sys_info);

int tc_sys_strings(struct device *dev,
                   char *str_fpga_rev, size_t size_fpga_rev,
                   char *str_tcf_core_rev, size_t size_tcf_core_rev,
                   char *str_tcf_core_target_build_id,
                   size_t size_tcf_core_target_build_id,
                   char *str_pci_ver, size_t size_pci_ver,
                   char *str_macro_ver, size_t size_macro_ver)
{
    struct nexefdrv_prvdata *priv_data = dev_get_drvdata(dev);
    uint32_t odin_rev, odin_cs;

    FUNC_IN();

    odin_rev = odin_core_readreg32(priv_data, PCI_ODIN_CORE_REVISION);
    odin_cs  = odin_core_readreg32(priv_data, PCI_ODIN_CORE_CHANGE_SET);

    snprintf(str_fpga_rev, size_fpga_rev, "3NX-F odin build\n");
    snprintf(str_tcf_core_rev, size_tcf_core_rev, "%d.%d", (odin_rev >> 8) & 0xF, odin_rev & 0xF);
    snprintf(str_tcf_core_target_build_id, size_tcf_core_target_build_id, "%d", odin_cs);
    snprintf(str_pci_ver, size_pci_ver, "??\n");
    snprintf(str_macro_ver, size_macro_ver, "??\n");

    return 0;
}
EXPORT_SYMBOL(tc_sys_strings);

int tc_core_clock_speed(struct device *dev)
{
    FUNC_IN();
    return 25000000L;
}
EXPORT_SYMBOL(tc_core_clock_speed);

//endregion NNPU needed exported functions

//region Platform related functions

static uint64_t nexef_get_rogue_dma_mask(struct platdev_export_info *export_info)
{
    /* Does not access system memory, so there is no DMA limitation */
    if (export_info->rogue_mem_mode == TC_MEMORY_LOCAL)
        return DMA_BIT_MASK(64);

    return DMA_BIT_MASK(32);
}

static int nexef_register_rogue_plat_device(struct nexefdrv_prvdata *priv_data)
{
    int err = 0;
	struct resource nexef_rogue_resources[] = {
            DEFINE_RES_MEM_NAMED(NEXEF_ROGUE_REG_OFFSET +
                                 pci_resource_start(priv_data->pci_dev,
                                                    NEXEF_ROGUE_REG_BAR),
                                 NEXEF_ROGUE_REG_SIZE, NEXEF_NNPU_PDEV_NAME),
	};
	struct tc_rogue_platform_data pdata = {
		.mem_mode = priv_data->plat_exports.rogue_mem_mode,
		.tc_memory_base = priv_data->plat_exports.dut_mem.base,
		.rogue_heap_memory_base = priv_data->plat_exports.rogue_heap_mem.base,
		.rogue_heap_memory_size = priv_data->plat_exports.rogue_heap_mem.size,
		.pdp_heap_memory_base = priv_data->plat_exports.rogue_pdp_heap_mem.base,
		.pdp_heap_memory_size = priv_data->plat_exports.rogue_pdp_heap_mem.size,
	};
	struct platform_device_info odin_rogue_dev_info = {
		.parent = &priv_data->pci_dev->dev,
		.name = TC_DEVICE_NAME_ROGUE,
		.id = -2,
		.res = nexef_rogue_resources,
		.num_res = ARRAY_SIZE(nexef_rogue_resources),
		.data = &pdata,
		.size_data = sizeof(pdata),
		.dma_mask = nexef_get_rogue_dma_mask(&priv_data->plat_exports),
	};

	priv_data->plat_exports.rogue_pdev = platform_device_register_full(&odin_rogue_dev_info);

	if (IS_ERR(priv_data->plat_exports.rogue_pdev)) {
		err = PTR_ERR(priv_data->plat_exports.rogue_pdev);
		dev_err(&priv_data->pci_dev->dev,
			"Failed to register `%s' device (%d)\n", TC_DEVICE_NAME_ROGUE, err);
        priv_data->plat_exports.rogue_pdev = NULL;
	}
	return err;
}

static int nexef_register_nna_plat_device(struct nexefdrv_prvdata *priv_data)
{
    int err = 0;

    struct resource nexef_nna_resources[] = {
            DEFINE_RES_MEM_NAMED(NEXEF_NNA_REG_OFFSET +
                                 pci_resource_start(priv_data->pci_dev,
                                                    NEXEF_NNA_REG_BAR),
                                 NEXEF_NNA_REG_SIZE, NEXEF_NNA_PDEV_NAME),
    };
    struct nexef_nna_platform_data pdata = {
            // tc->dut2_mem_base - tc->tc_mem.base
            .nna_memory_base = priv_data->plat_exports.nna_heap_mem.base,
            .nna_memory_offset = priv_data->plat_exports.nna_heap_mem.base - priv_data->plat_exports.dut_mem.base,
            .nna_memory_size = priv_data->plat_exports.nna_heap_mem.size,
    };
    struct platform_device_info nexef_nna_dev_info = {
            .parent = &priv_data->pci_dev->dev,
            .name = NEXEF_NNA_DEVICE_NAME,
            .id = -2,
            .res = nexef_nna_resources,
            .num_res = ARRAY_SIZE(nexef_nna_resources),
            .data = &pdata,
            .size_data = sizeof(pdata),
            //.dma_mask = nexef_get_rogue_dma_mask(tc),
    };

    priv_data->plat_exports.nna_pdev = platform_device_register_full(&nexef_nna_dev_info);

    if (IS_ERR(priv_data->plat_exports.nna_pdev)) {
        err = PTR_ERR(priv_data->plat_exports.nna_pdev);
        dev_err(&priv_data->pci_dev->dev,
                "Failed to register `%s' device (%d)\n", NEXEF_NNA_DEVICE_NAME, err);
        priv_data->plat_exports.nna_pdev = NULL;
    }

    return err;
}

static void nexef_unregister_rogue_plat_device(struct nexefdrv_prvdata *priv_data)
{
    if (priv_data->plat_exports.rogue_pdev) {
        dev_dbg(&priv_data->pci_dev->dev, "Unregistering NNPU platform device");
        platform_device_unregister(priv_data->plat_exports.rogue_pdev);
    }
}

static void nexef_unregister_nna_plat_device(struct nexefdrv_prvdata *priv_data)
{
    if (priv_data->plat_exports.nna_pdev) {
        dev_dbg(&priv_data->pci_dev->dev, "Unregistering NNA platform device");
        platform_device_unregister(priv_data->plat_exports.nna_pdev);
    }
}

//endregion Platform related functions

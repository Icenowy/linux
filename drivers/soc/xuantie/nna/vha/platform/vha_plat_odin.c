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
#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,10,0)
#include <linux/dma-mapping.h>
#else
#include <linux/dma-map-ops.h>
#endif
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/mod_devicetable.h>
#include <linux/workqueue.h>

#include "uapi/version.h"
#include "vha_common.h"
#include "vha_plat.h"

#if defined(CFG_SYS_VAGUS)
#include <hwdefs/nn_sys_cr_vagus.h>
#endif

#if defined(CFG_SYS_VAGUS)
#include <hwdefs/vagus_system.h>
#elif defined(CFG_SYS_AURA)
#include <hwdefs/aura_system.h>
#elif defined(CFG_SYS_MIRAGE)
#include <hwdefs/mirage_system.h>
#endif

#define DEVICE_NAME "vha"

#define IS_ODIN_DEVICE(devid) ((devid) == PCI_ODIN_DEVICE_ID)

/*
 * from Odin Lite TRM rev 1.0.88
 */

#define PCI_ODIN_VENDOR_ID (0x1AEE)
#define PCI_ODIN_DEVICE_ID (0x1010)

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

/* core bits definitions */
#define INTERNAL_RESET_INTERNAL_RESETN_PIKE     (1 << 7)
#define EXTERNAL_RESET_EXTERNAL_RESETN_DUT      (1 << 0)

#define DUT_CTRL1_DUT_MST_OFFSET                (1 << 31)
#define ODIN_CORE_CONTROL_DUT_OFFSET_SHIFT      (24)
#define ODIN_CORE_CONTROL_DUT_OFFSET_MASK       (0x7 << ODIN_CORE_CONTROL_DUT_OFFSET_SHIFT)

/* interrupt bits definitions */
#define INT_INTERRUPT_MASTER_ENABLE             (1 << 31)
#define INT_INTERRUPT_DUT0                      (1 << 0)
#define INT_INTERRUPT_DUT1                      (1 << 9)

/* srs_clk_blk */
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV1  (0x0020)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV2  (0x0024)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_OUT_DIV3  (0x001C)
#define PCI_ODIN_CLK_BLK_DUT_REG_CLK_OUT_DIV1   (0x0028)
#define PCI_ODIN_CLK_BLK_DUT_REG_CLK_OUT_DIV2   (0x002C)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT1     (0x0050)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT2     (0x0054)
#define PCI_ODIN_CLK_BLK_DUT_CORE_CLK_MULT3     (0x004C)
#define PCI_ODIN_CLK_BLK_DUT_CORE_VLK_IN_DIV    (0x0058)
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
#define PCI_ODIN_DUT_MEM_BAR  (4)

/* Number of core cycles used to measure the core clock frequency */
#define FREQ_MEASURE_CYCLES 0x7fffff

/* Parameters applicable when using bus master mode */
static unsigned long contig_phys_start;
module_param(contig_phys_start, ulong, 0444);
MODULE_PARM_DESC(contig_phys_start, "Physical address of start of contiguous region");

static uint32_t contig_size;
module_param(contig_size, uint, 0444);
MODULE_PARM_DESC(contig_size, "Size of contiguous region: takes precedence over any PCI based memory");

static uint32_t fpga_heap_type = IMG_MEM_HEAP_TYPE_UNIFIED;
module_param(fpga_heap_type, uint, 0444);
MODULE_PARM_DESC(fpga_heap_type, "Fpga primary heap type");

static unsigned long pci_size;
module_param(pci_size, ulong, 0444);
MODULE_PARM_DESC(pci_size, "physical size in bytes. when 0 (the default), use all memory in the PCI bar");

static unsigned long pci_offset;
module_param(pci_offset, ulong, 0444);
MODULE_PARM_DESC(pci_offset, "offset from PCI bar start. (default: 0)");

static unsigned short pool_alloc_order;
module_param(pool_alloc_order, ushort, 0444);
MODULE_PARM_DESC(pool_alloc_order,
		"Carveout pool allocation order, depends on PAGE_SIZE, \
		for CPU PAGE_SIZE=4kB, 0-4kB, 1-8kB, 2-16kB, 3-32kB, 4-64kB");

/* Newest version of ODIN allow for dual DUT devices, this parameter allow at load time to select which DUT to use */
static unsigned long dut_id = 0;
module_param(dut_id, ulong, 0444);
MODULE_PARM_DESC(dut_id, "DUT the driver try to address. valid: {0, 1}, (default: 0)");

static bool mem_static_kptr = true;
module_param(mem_static_kptr, bool, 0444);
MODULE_PARM_DESC(mem_static_kptr,
		"Creates static kernel mapping for fpga memory");

/* Maximum DUT_ID allowed */
#define MAX_DUT_ID (1)

static uint32_t odin_dut_register_offset[] = {
				0x00000000, /* DUT 0 */
				0x02000000, /* DUT 1 */
};

static uint32_t odin_dut_interrupt_bit[] = {
				INT_INTERRUPT_DUT0, /* DUT 0 */
				INT_INTERRUPT_DUT1, /* DUT 1 */
};

/*
 * Special handling (not implemented) is required for the VHA device
 * to be able to access both carveout buffers (internal memory) and
 * dmabuf buffers (system memory).The latter have to go through
 * the system bus to be accessed whereas the former do not.
 */

#if !defined(FPGA_BUS_MASTERING) && !defined(CONFIG_GENERIC_ALLOCATOR)
#error Neither FPGA_BUS_MASTERING or GENERIC_ALLOCATOR is defined
#endif

static struct heap_config vha_dev_fpga_heap_configs[] = {
	/* Primary heap used for internal allocations */
#if CONFIG_GENERIC_ALLOCATOR
	{
		.type = IMG_MEM_HEAP_TYPE_CARVEOUT,
		/* .options.carveout to be filled at run time */
		/* .to_dev_addr to be filled at run time */
	},
#endif
};
static const int vha_dev_fpga_heaps = sizeof(vha_dev_fpga_heap_configs)/
	sizeof(*vha_dev_fpga_heap_configs);

static struct heap_config vha_plat_fpga_heap_configs[] = {
	/* Secondary heap used for importing an external memory */
#if defined(FPGA_BUS_MASTERING)
#error Bus mastering not supported for now.
	{
		.type = -1, /* selected with fpga_heap_type */
		.options = {
			.unified.gfp_type = GFP_DMA32 | __GFP_ZERO,
			.coherent.gfp_flags = GFP_DMA32 | __GFP_ZERO,
		},
		.to_dev_addr = NULL,
	},
	{
		.type = IMG_MEM_HEAP_TYPE_ANONYMOUS,
	},
#endif
#if CONFIG_DMA_SHARED_BUFFER
	{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		.to_dev_addr = NULL,
#if !defined(FPGA_BUS_MASTERING)
		.options.dmabuf = {
				.use_sg_dma = true,
		},
#endif
	},
#else
#warning "Memory importing not supported!"
#endif
};

static const int vha_plat_fpga_heaps = sizeof(vha_plat_fpga_heap_configs)/
	sizeof(*vha_plat_fpga_heap_configs);

static const struct pci_device_id pci_pci_ids[] = {
	{ PCI_DEVICE(PCI_ODIN_VENDOR_ID, PCI_ODIN_DEVICE_ID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_pci_ids);

enum { CORE_REG_BANK = 0,
	NNA_REG_BANK, MEM_REG_BANK,
	REG_BANK_COUNT /* Must be the last */};

struct imgpci_prvdata {
	int irq;

	struct {
		int bar;
		unsigned long addr;
		unsigned long size;
		void __iomem *km_addr;
	} reg_bank[REG_BANK_COUNT];

	struct pci_dev *pci_dev;
};


struct img_pci_driver {
	struct pci_dev *pci_dev;
	struct pci_driver pci_driver;
	struct delayed_work irq_work;
};

static int vha_plat_probe(struct pci_dev *pci_dev,
		const struct pci_device_id *id);
static void vha_plat_remove(struct pci_dev *dev);

static int vha_plat_suspend(struct device *dev);
static int vha_plat_resume(struct device *dev);

static SIMPLE_DEV_PM_OPS(vha_pm_plat_ops,
		vha_plat_suspend, vha_plat_resume);

static ssize_t info_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "VHA Odin driver version : " VERSION_STRING "\n");
}

static inline uint64_t __readreg64(struct imgpci_prvdata *data,
		int bank, unsigned long offset) __maybe_unused;
static inline void __writereg64(struct imgpci_prvdata *data,
		int bank, unsigned long offset, uint64_t val) __maybe_unused;

static DRIVER_ATTR_RO(info);
static struct attribute *drv_attrs[] = {
	&driver_attr_info.attr,
	NULL
};

ATTRIBUTE_GROUPS(drv);

static struct img_pci_driver vha_pci_drv = {
	.pci_driver = {
		.name = "vha_pci",
		.id_table = pci_pci_ids,
		.probe = vha_plat_probe,
		.remove = vha_plat_remove,
		.driver = {
			.groups = drv_groups,
			.pm = &vha_pm_plat_ops,
		}
	},
};

static ulong maxmapsizeMB = (sizeof(void *) == 4) ? 400 : 4096;

/**
 * __readreg32 - Generic PCI bar read functions
 * @data: pointer to the data
 * @bank: register bank
 * @offset: offset within bank
 */
static inline unsigned int __readreg32(struct imgpci_prvdata *data,
		int bank, unsigned long offset)
{
	void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
			offset);
	return ioread32(reg);
}

/**
 * __writereg32 - Generic PCI bar write functions
 * @data: pointer to the data
 * @bank: register bank
 * @offset: offset within bank
 * @val: value to be written
 */
static inline void __writereg32(struct imgpci_prvdata *data,
		int bank, unsigned long offset, int val)
{
	void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
			offset);
	iowrite32(val, reg);
}

/*
 * __readreg64 - Generic PCI bar read functions
 * @data: pointer to the data
 * @bank: register bank
 * @offset: offset within bank
 */
static inline uint64_t __readreg64(struct imgpci_prvdata *data,
		int bank, unsigned long offset)
{
		void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
																				 offset);
		return (uint64_t)ioread32(reg) | ((uint64_t)ioread32(reg + 4) << 32);
}

/*
 * __writereg64 - Generic PCI bar write functions
 * @data: pointer to the data
 * @bank: register bank
 * @offset: offset within bank
 * @val: value to be written
 */
static inline void __writereg64(struct imgpci_prvdata *data,
		int bank, unsigned long offset, uint64_t val)
{
		void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
																				 offset);
		iowrite32(val & 0xFFFFFFFF, reg);
		iowrite32(val >> 32, reg + 4);
}

/**
 * odin_core_writereg32 - Write to Odin control registers
 * @data: pointer to the data
 * @offset: offset within bank
 * @val: value to be written
 */
static inline void odin_core_writereg32(struct imgpci_prvdata *data,
		unsigned long offset, int val)
{
	__writereg32(data, CORE_REG_BANK, offset, val);
}

/**
 * odin_core_readreg32 - Read Odin control registers
 * @data: pointer to the data
 * @offset: offset within bank
 */
static inline unsigned int odin_core_readreg32(struct imgpci_prvdata *data,
		unsigned long offset)
{
	return __readreg32(data, CORE_REG_BANK, offset);
}

/**
 * reset_dut - Reset the Device Under Test
 * @data: pointer to the data
 */
static void reset_dut(struct imgpci_prvdata *data)
{

	uint32_t internal_rst = odin_core_readreg32(data, PCI_ODIN_CORE_INTERNAL_RESETN);
	uint32_t external_rst = odin_core_readreg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN);

	dev_dbg(&data->pci_dev->dev, "going to reset DUT fpga!\n");

	odin_core_writereg32(data, PCI_ODIN_CORE_INTERNAL_RESETN,
		internal_rst & ~(INTERNAL_RESET_INTERNAL_RESETN_PIKE));
		odin_core_writereg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN,
		external_rst & ~(EXTERNAL_RESET_EXTERNAL_RESETN_DUT));

	udelay(100); /* arbitrary delays, just in case! */

	odin_core_writereg32(data, PCI_ODIN_CORE_INTERNAL_RESETN, internal_rst);
	odin_core_writereg32(data, PCI_ODIN_CORE_EXTERNAL_RESETN, external_rst);

	msleep(100);

	dev_dbg(&data->pci_dev->dev, "DUT fpga reset done!\n");
}

/**
 * pci_thread_irq - High latency interrupt handler
 * @irq: irq number
 * @dev_id: pointer to private data
 */
static irqreturn_t pci_thread_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;

	return vha_handle_thread_irq(&dev->dev);
}

/**
 * odin_isr_clear - Clear an interrupt
 * @data: pointer to the data
 * @intstatus: interrupt status
 *
 * note: the reason of that function is unclear, it is taken from Apollo/Atlas code that have
 * the same interrupt handler as Odin, is it because of a bug?
 */
static void odin_isr_clear(struct imgpci_prvdata *data, unsigned int intstatus)
{
	unsigned int max_retries = 1000;

	while ((odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_STATUS) & intstatus) && max_retries--)
		odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_CLR,
			(INT_INTERRUPT_MASTER_ENABLE | intstatus));
}


/**
 * pci_isr_cb - Low latency interrupt handler
 * @irq: irq number
 * @dev_id: pointer to private data
 */
static irqreturn_t pci_isr_cb(int irq, void *dev_id)
{
	uint32_t intstatus;

	struct pci_dev *dev = (struct pci_dev *)dev_id;
	struct imgpci_prvdata *data;

	irqreturn_t ret = IRQ_NONE;

	if (dev_id == NULL) {
		/* Spurious interrupt: not yet initialised. */
		pr_warn("Spurious interrupt data/dev_id not initialised!\n");
		goto exit;
	}

	data = vha_get_plat_data(&dev->dev);

	if (data == NULL) {
		/* Spurious interrupt: not yet initialised. */
		pr_warn("Invalid driver private data!\n");
		goto exit;
	}

	/* Read interrupt status register */
	intstatus = odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_STATUS);

	/* Now handle the ints */
	if (intstatus & odin_dut_interrupt_bit[dut_id]) {
		/* call real irq handler */
		ret = vha_handle_irq(&dev->dev);
	} else {
		/* most likely this is a shared interrupt line */
		dev_dbg(&dev->dev,
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

static inline void odin_reset_int(struct imgpci_prvdata *data) {
	odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE, 0);
	odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_CLR, 0xFFFFFFFF);
}

/**
 * odin_enable_int - Enable an interrupt
 * @data: pointer to the data
 * @intmask: interrupt mask
 */
static inline void odin_enable_int(struct imgpci_prvdata *data,
		uint32_t intmask)
{
	uint32_t irq_enabled = odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE);
	intmask &= odin_dut_interrupt_bit[dut_id];

	odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE, irq_enabled | intmask | INT_INTERRUPT_MASTER_ENABLE);
}

/**
 * odin_disable_int - Disable an interrupt
 * @data: pointer to the data
 * @intmask: interrupt mask
 */
static inline void odin_disable_int(struct imgpci_prvdata *data,
		uint32_t intmask)
{
	uint32_t irq_enabled = odin_core_readreg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE);
	intmask &= odin_dut_interrupt_bit[dut_id];

	odin_core_writereg32(data, PCI_ODIN_CORE_INTERRUPT_ENABLE,
		irq_enabled & ~intmask);
}

/**
 * odin_allocate_registers - Allocate memory for a register (or memory) bank
 * @pci_dev: pointer to pci device
 * @data: pointer to the data
 * @bank: bank to set
 * @bar: BAR where the register are
 * @base: base address in the BAR
 * @size: size of the register set
 */
static inline int odin_allocate_registers(struct pci_dev *pci_dev,
		struct imgpci_prvdata *data, int bank,
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

	pr_debug("[bank %u] bar:%d addr:0x%lx size:0x%lx km:0x%px\n",
			bank, bar, data->reg_bank[bank].addr,
			data->reg_bank[bank].size,
			data->reg_bank[bank].km_addr);

	return data->reg_bank[bank].km_addr == NULL;
}

int vha_plat_deinit(void)
{
	struct pci_dev *dev = vha_pci_drv.pci_dev;
	int ret;

	if (dev) {
		struct imgpci_prvdata *data = vha_get_plat_data(&dev->dev);

		if (data) {
			/* reset the hardware */
			reset_dut(data);
		} else {
			dev_dbg(&dev->dev,
					"%s: prv data not found, HW reset omitted\n",
					__func__);
		}
	} else {
		/*pr_debug("%s: dev missing, HW reset omitted\n", __func__);*/
	}

	/* Unregister the driver from the OS */
	pci_unregister_driver(&(vha_pci_drv.pci_driver));

	ret = vha_deinit();
	if (ret)
		pr_err("VHA driver deinit failed\n");

	return ret;
}

#define NNA_REG_BAR (PCI_ODIN_DUT_REGS_BAR)
#ifdef CFG_SYS_VAGUS
#define NNA_REG_SIZE (_REG_SIZE + _REG_NNSYS_SIZE)
#else
#define NNA_REG_SIZE (_REG_SIZE)
#endif

#define NNA_REG_OFFSET (_REG_START)


#ifdef CONFIG_GENERIC_ALLOCATOR
static phys_addr_t carveout_to_dev_addr(union heap_options *options,
					phys_addr_t addr)
{
	phys_addr_t base = options->carveout.phys;
	size_t size = options->carveout.size;
	unsigned long offset = options->carveout.offs;

	if (addr - offset >= base && addr < base + size - offset)
		return addr - base;

	pr_err("%s: unexpected addr! base 0x%llx size %zu offs %zu addr 0x%llx\n",
			__func__, base, size, offset, addr);
	WARN_ON(1);

	return addr;
}

static phys_addr_t carveout_to_host_addr(union heap_options *options,
					phys_addr_t addr)
{
	phys_addr_t base = options->carveout.phys;
	size_t size = options->carveout.size;
	unsigned long offset = options->carveout.offs;

	if (addr < size - offset)
		return base + addr;

	pr_err("%s: unexpected addr! base %llx size %zu offs %zu addr %#llx\n",
				 __func__, base, size, offset, addr);
	WARN_ON(1);
	return addr;
}

static void *carveout_get_kptr(phys_addr_t addr,
		size_t size, enum img_mem_attr mattr)
{
	/*
	 * Device memory is I/O memory and as a rule, it cannot
	 * be dereferenced safely without memory barriers, that
	 * is why it is guarded by __iomem (return of ioremap)
	 * and checked by sparse. It is accessed only through
	 * ioread32(), iowrit32(), etc.
	 *
	 * In x86 this memory can be dereferenced and safely
	 * accessed, i.e.  a * __iomem pointer can be casted to
	 * a regular void* * pointer.  We cast this here
	 * assuming FPGA is x86 and add __force to silence the
	 * sparse warning
	 *
	 * Note: System memory carveout can be used with cached turned on.
	 * */
	void *kptr = NULL;

	if (mattr & IMG_MEM_ATTR_UNCACHED)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		kptr = (void * __force *)ioremap_nocache(addr, size);
#else
		kptr = (void * __force *)ioremap(addr, size);
#endif
	else if (mattr & IMG_MEM_ATTR_CACHED)
		kptr = (void * __force *)ioremap_cache(addr, size);
	else if (mattr & IMG_MEM_ATTR_WRITECOMBINE)
		kptr = (void * __force *)ioremap_wc(addr, size);

	/*pr_debug(
		"Mapping %zu bytes into kernel memory (Phys:%08llX, Kptr:%p)\n",
		size, addr, kptr);
	pr_debug("[%c%c%c]\n",
			 (mattr & IMG_MEM_ATTR_UNCACHED) ? 'U' : '.',
			 (mattr & IMG_MEM_ATTR_CACHED) ? 'C' : '.',
			 (mattr & IMG_MEM_ATTR_WRITECOMBINE) ? 'W' : '.');*/
	return kptr;
}

static int carveout_put_kptr(void *addr)
{
/*	pr_debug("Unmapping kernel memory (Phys: %p)\n", addr);*/
	iounmap(addr);
	return 0;
}
#endif

/*
 * IO hooks.
 * NOTE: using spinlock to avoid
 * problems with multi threaded IO access
 */
static DEFINE_SPINLOCK(io_irq_lock);
static unsigned long io_irq_flags;

uint64_t vha_plat_read64(void *addr)
{
	u64 val;
	spin_lock_irqsave(&io_irq_lock, io_irq_flags);
	val =(uint64_t)readl((const volatile void __iomem *)addr) |
				((uint64_t)readl((const volatile void __iomem *)addr + 4) << 32);
	spin_unlock_irqrestore(&io_irq_lock, io_irq_flags);
	return val;
}

void vha_plat_write64(void *addr, uint64_t val)
{
	spin_lock_irqsave(&io_irq_lock, io_irq_flags);
	writel((uint32_t)(val & 0xffffffff), (volatile void __iomem *)addr);
	writel((uint32_t)(val >> 32),        (volatile void __iomem *)addr + 4);
	spin_unlock_irqrestore(&io_irq_lock, io_irq_flags);
}

static int vha_plat_probe(struct pci_dev *pci_dev,
		const struct pci_device_id *id)
{
	int ret = 0;
	struct imgpci_prvdata *data;
	size_t maxmapsize = maxmapsizeMB * 1024 * 1024;
	unsigned long vha_base_mem, vha_mem_size;
	struct device *dev = &pci_dev->dev;
	int heap;
	uint32_t tmp;

	dev_dbg(dev, "probing device, pci_dev: %p\n", dev);

	/* Enable the device */
	if (pci_enable_device(pci_dev))
		goto out_free;

	dev_info(dev, "%s dma_get_mask : %#llx\n", __func__, dma_get_mask(dev));

	if (dev->dma_mask) {
		dev_info(dev, "%s dev->dma_mask : %p : %#llx\n",
				__func__, dev->dma_mask, *dev->dma_mask);
	} else {
		dev_info(dev, "%s mask unset, setting coherent\n", __func__);
		dev->dma_mask = &dev->coherent_dma_mask;
	}

	ret = dma_set_mask(dev, DMA_BIT_MASK(36));
	if (ret) {
		dev_err(dev, "%s failed to set dma mask\n", __func__);
		goto out_disable;
	}
	dev_info(dev, "%s dma_set_mask %#llx\n", __func__, dma_get_mask(dev));

	/* Reserve PCI I/O and memory resources */
	if (pci_request_regions(pci_dev, "imgpci"))
		goto out_disable;

	/* Create a kernel space mapping for each of the bars */
	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_err("Memory allocation error, aborting.\n");
		ret = -ENOMEM;
		goto out_release;
	}

	dev_dbg(dev, "allocated imgpci_prvdata @ %p\n", data);
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

		pr_info("Found Odin lite board v%d.%d (ID:%X CS:%X UI:%X UB:%X)",
			(odin_rev >> 8) & 0xF, odin_rev & 0xF, odin_id & 0x7, odin_cs, odin_ui, odin_ub);
	}


	
	if (dut_id > MAX_DUT_ID) {
		dev_err(dev, "Invalid DUT number (%lu), setting it to 0\n", dut_id);
		dut_id = 0;
	}

	/* Allocate NNA register space */
	ret = odin_allocate_registers(pci_dev, data,
				NNA_REG_BANK, NNA_REG_BAR,
				NNA_REG_OFFSET + odin_dut_register_offset[dut_id],
				NNA_REG_SIZE);
	if (ret) {
		dev_err(dev, "Can't allocate memory for vha regs!");
		ret = -ENOMEM;
		goto out_release;
	}

	/* Allocate DUT memory space */
	vha_mem_size = pci_resource_len(pci_dev, PCI_ODIN_DUT_MEM_BAR);
	if (vha_mem_size > maxmapsize)
		vha_mem_size = maxmapsize;

	vha_base_mem = pci_resource_start(pci_dev, PCI_ODIN_DUT_MEM_BAR);

	/* change alloc size according to module parameter */
	if (pci_size)
		vha_mem_size = pci_size;

	/* allocating memory only when static kernel mapping is requested,
	 * so hand set values here: */
	data->reg_bank[MEM_REG_BANK].bar = PCI_ODIN_DUT_MEM_BAR;
	data->reg_bank[MEM_REG_BANK].addr = vha_base_mem;
	data->reg_bank[MEM_REG_BANK].size = vha_mem_size;
	if (mem_static_kptr) {
		data->reg_bank[MEM_REG_BANK].km_addr = devm_ioremap(
			&pci_dev->dev, data->reg_bank[MEM_REG_BANK].addr,
			data->reg_bank[MEM_REG_BANK].size);
		if (data->reg_bank[MEM_REG_BANK].km_addr == NULL) {
			dev_err(dev, "Can't allocate memory for vha regs!");
			ret = -ENOMEM;
			goto out_release;
		}
	}

	pr_debug("[bank %u] bar: %d addr: 0x%lx (kptr:%p) size: 0x%lx\n",
			MEM_REG_BANK, PCI_ODIN_DUT_MEM_BAR,
				data->reg_bank[MEM_REG_BANK].addr,
				data->reg_bank[MEM_REG_BANK].km_addr,
				data->reg_bank[MEM_REG_BANK].size);

#ifdef FPGA_BUS_MASTERING
	tmp = odin_core_readreg32(data, PCI_ODIN_CORE_DUT_CTRL1);
	tmp &= ~DUT_CTRL1_DUT_MST_OFFSET;
	odin_core_writereg32(data, PCI_ODIN_CORE_DUT_CTRL1, tmp);

	tmp = odin_core_readreg32(data, PCI_ODIN_CORE_CORE_CONTROL);
	tmp &= ODIN_CORE_CONTROL_DUT_OFFSET_MASK;
	odin_core_writereg32(data, PCI_ODIN_CORE_CORE_CONTROL, tmp);
#else
	/* Set the Odin board in a similar way as the Apollo is,
	 * DUT memory starting at 0x0 instead of 0x4_0000_0000
	 */
	tmp = odin_core_readreg32(data, PCI_ODIN_CORE_DUT_CTRL1);
	tmp |= DUT_CTRL1_DUT_MST_OFFSET;
	odin_core_writereg32(data, PCI_ODIN_CORE_DUT_CTRL1, tmp);

	tmp = odin_core_readreg32(data, PCI_ODIN_CORE_CORE_CONTROL);
	tmp &= ODIN_CORE_CONTROL_DUT_OFFSET_MASK;
	tmp |= (4 << ODIN_CORE_CONTROL_DUT_OFFSET_SHIFT);
	odin_core_writereg32(data, PCI_ODIN_CORE_CORE_CONTROL, tmp);
#endif

	/* Get the IRQ...*/
	data->irq = pci_dev->irq;
	data->pci_dev = pci_dev;
	vha_pci_drv.pci_dev = pci_dev;

	reset_dut(data);

	odin_reset_int(data);
	odin_enable_int(data, odin_dut_interrupt_bit[dut_id]);

	for (heap = 0; heap < vha_dev_fpga_heaps; heap++) {
		struct heap_config *cfg = &vha_dev_fpga_heap_configs[heap];

#ifdef CONFIG_GENERIC_ALLOCATOR
		if (cfg->type == IMG_MEM_HEAP_TYPE_CARVEOUT) {
			if (contig_size && contig_phys_start) {
				/*
				 * 2 types of carveout memory are supported:
				 * memory carved out of the main DDR
				 * memory region.
				 * eg: linux boot option memmap=512M$0x5CAFFFFF
				 * This is configured using module parameters:
				 * contig_phys_start and size
				 * DDR populated in the actual PCI card,
				 * in BAR 4.
				 * The module parameters take precedence
				 * over PCI memory.
				 */
				cfg->options.carveout.phys = contig_phys_start;
				cfg->options.carveout.size = contig_size;
				cfg->to_dev_addr = NULL;
				cfg->to_host_addr = NULL;
				/*dev_info(dev, "using %dMB CARVEOUT at x%lx\n",
					 contig_size/1024/1024,
					 contig_phys_start);*/
			} else {
				cfg->options.carveout.phys =
						data->reg_bank[MEM_REG_BANK].addr;
				if (mem_static_kptr)
					cfg->options.carveout.kptr =
							data->reg_bank[MEM_REG_BANK].km_addr;
				cfg->options.carveout.size =
						data->reg_bank[MEM_REG_BANK].size;
				cfg->options.carveout.offs = pci_offset;
				cfg->to_dev_addr = carveout_to_dev_addr;
				cfg->to_host_addr = carveout_to_host_addr;
			/*  dev_info(dev,
					"using %zuMB CARVEOUT from PCI at 0x%x\n",
					cfg->options.carveout.size/1024/1024,
					cfg->options.carveout.phys);*/
			}
			/* IO memory access callbacks */
			if (!mem_static_kptr) {
				/* Dynamic kernel memory mapping */
				cfg->options.carveout.get_kptr = carveout_get_kptr;
				cfg->options.carveout.put_kptr = carveout_put_kptr;
			}
			/* Allocation order */
			cfg->options.carveout.pool_order = pool_alloc_order;

			break;
		}
#endif

		if (cfg->type == IMG_MEM_HEAP_TYPE_COHERENT) {
			ret = dma_declare_coherent_memory(dev,
					contig_phys_start,
					contig_phys_start,
					contig_size
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,1,0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,14,0)
					, DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE
#else
													 , DMA_MEMORY_EXCLUSIVE
#endif
#endif
					);
			if (ret == 0) {
				dev_err(dev, "failed to initialize coherent memory!\n");
				/* We will fallback to the
				 * default pool anyway
					 goto out_release; */
			}
			break;
		}
	}

	ret = vha_add_dev(dev,
			vha_dev_fpga_heap_configs,
			vha_dev_fpga_heaps,
			data,
			data->reg_bank[NNA_REG_BANK].km_addr,
			data->reg_bank[NNA_REG_BANK].size);
	if (ret) {
		dev_err(dev, "failed to initialize driver core!\n");
		goto out_heap_deinit;
	}

	/*
	 * Reset FPGA DUT only after disabling clocks in
	 * vha_add_dev()-> get properties.
	 * This workaround is required to ensure that
	 * clocks (on daughter board) are enabled for test slave scripts to
	 * read FPGA build version register.
	 * NOTE: Asserting other bits like DDR reset bit cause problems
	 * with bus mastering feature, thus results in memory failures.
	 */
	reset_dut(data);

	{

		/*uint32_t fpga_rev = odin_readreg32(data, 1,
				FPGA_IMAGE_REV_OFFSET) & FPGA_IMAGE_REV_MASK;
		dev_dbg(dev, "fpga image revision: 0x%x\n", fpga_rev);
		if (!fpga_rev || fpga_rev == 0xdead1) {
			dev_err(dev, "fpga revision incorrect (0x%x)!\n",
					fpga_rev);
			goto out_rm_dev;
		}*/
	}

	/* Install the ISR callback...*/
	ret = devm_request_threaded_irq(dev, data->irq, &pci_isr_cb,
			&pci_thread_irq, IRQF_SHARED, DEVICE_NAME,
			(void *)pci_dev);
	if (ret) {
		dev_err(dev, "failed to request irq!\n");
		goto out_rm_dev;
	}
	dev_dbg(dev, "registered irq %d\n", data->irq);

	/* Try to calibrate the core if needed */
	ret = vha_dev_calibrate(dev, FREQ_MEASURE_CYCLES);
	if (ret) {
		dev_err(dev, "%s: Failed to start clock calibration!\n", __func__);
		goto out_rm_dev;
	}
	return ret;

out_rm_dev:
	vha_rm_dev(dev);

out_heap_deinit:
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
	/* Release any declared mem regions */
	dma_release_declared_memory(dev);
#endif

	/* Make sure int are no longer enabled */
	odin_disable_int(data, odin_dut_interrupt_bit[dut_id]);

out_release:
	pci_release_regions(pci_dev);

out_disable:
	pci_disable_device(pci_dev);

out_free:
	return ret;
}

static void vha_plat_remove(struct pci_dev *dev)
{
	struct imgpci_prvdata *data = vha_get_plat_data(&dev->dev);

	dev_dbg(&dev->dev, "removing device\n");


	if (data == NULL) {
		dev_err(&dev->dev, "PCI priv data missing!\n");
	} else {
		/*
		 * We  need to disable interrupts for the
		 * embedded device via the fpga interrupt controller...
		 */
		odin_disable_int(data, odin_dut_interrupt_bit[dut_id]);

		/* Unregister int */
		devm_free_irq(&dev->dev, data->irq, dev);

	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
	/* Release any declared mem regions */
	dma_release_declared_memory(&dev->dev);
#endif
	pci_release_regions(dev);
	pci_disable_device(dev);

	vha_rm_dev(&dev->dev);
}

#ifdef CONFIG_PM
static int vha_plat_suspend(struct device *dev)
{
	return vha_suspend_dev(dev);
}

static int vha_plat_resume(struct device *dev)
{
	return vha_resume_dev(dev);
}
#endif

int vha_plat_init(void)
{
	int ret;

#if 0
#ifdef FPGA_BUS_MASTERING
	vha_plat_fpga_heap_configs[0].type = fpga_heap_type;
#endif
#endif

	ret = vha_init_plat_heaps(vha_plat_fpga_heap_configs, vha_plat_fpga_heaps);
	if(ret) {
		pr_err("failed to initialize global heaps\n");
		return -ENOMEM;
	}

	ret = pci_register_driver(&vha_pci_drv.pci_driver);
	if (ret) {
		pr_err("failed to register PCI driver!\n");
		return ret;
	}

	/* pci_dev should be set in probe */
	if (!vha_pci_drv.pci_dev) {
		pr_err("failed to find VHA PCI dev!\n");
		pci_unregister_driver(&vha_pci_drv.pci_driver);
		return -ENODEV;
	}

	return 0;
}

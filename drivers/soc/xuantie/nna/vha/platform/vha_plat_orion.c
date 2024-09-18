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

/*
 * Things left to be done at a later point as of 28/02/2019:
 *
 * - Maybe add code to set the DUT clock
 * FIXME: Find a way to get DUT register size from .def files
 */

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

#if defined(CFG_SYS_AURA)
#include <hwdefs/aura_system.h>
#elif defined(CFG_SYS_MIRAGE)
#include <hwdefs/mirage_system.h>
#else
#error System configuration not supported!
#endif

#define DEVICE_NAME "vha"

#define IS_SIRIUS_DEVICE(devid) ((devid) == PCI_SIRIUS_DEVICE_ID)

/*
 * from Sirius TRM rev 1.0.3
 */

#define PCI_SIRIUS_VENDOR_ID (0x1AEE)
#define PCI_SIRIUS_DEVICE_ID (0x1020)

/* Sirius - System control register bar */
#define PCI_SIRIUS_SYS_CTRL_REGS_BAR (0)

#define PCI_SIRIUS_SYS_CTRL_BASE_OFFSET (0x0000)
/* srs_core */
#define PCI_SIRIUS_SRS_CORE_ID                        (0x0000)
#define PCI_SIRIUS_SRS_CORE_REVISION                  (0x0004)
#define PCI_SIRIUS_SRS_CORE_CHANGE_SET                (0x0008)
#define PCI_SIRIUS_SRS_CORE_USER_ID                   (0x000C)
#define PCI_SIRIUS_SRS_CORE_USER_BUILD                (0x0010)
#define PCI_SIRIUS_SRS_CORE_SOFT_RESETN               (0x0080)
#define PCI_SIRIUS_SRS_CORE_DUT_SOFT_RESETN           (0x0084)
#define PCI_SIRIUS_SRS_CORE_SOFT_AUTO_RESETN          (0x0088)
#define PCI_SIRIUS_SRS_CORE_CLK_GEN_RESET             (0x0090)
#define PCI_SIRIUS_SRS_CORE_NUM_GPIO                  (0x0180)
#define PCI_SIRIUS_SRS_CORE_GPIO_EN                   (0x0184)
#define PCI_SIRIUS_SRS_CORE_GPIO                      (0x0188)
#define PCI_SIRIUS_SRS_CORE_SPI_MASTER_IFACE          (0x018C)
#define PCI_SIRIUS_SRS_CORE_SYS_IP_STATUS             (0x0200)
#define PCI_SIRIUS_SRS_CORE_CORE_CONTROL              (0x020D)
#define PCI_SIRIUS_SRS_CORE_REG_BANK_STATUS           (0x0208)
#define PCI_SIRIUS_SRS_CORE_MMCM_LOCK_STATUS          (0x020C)
#define PCI_SIRIUS_SRS_CORE_GIST_STATUS               (0x0210)
#define PCI_SIRIUS_SRS_CORE_SENSOR_BOARD              (0x0214)

/* srs_core bits definitions */
#define DUT_SOFT_RESETN_DUT_SOFT_RESETN_EXTERNAL      (1 << 0)

/* srs_clk_blk */
#define PCI_SIRIUS_CLOCK_CTRL_BASE_OFFSET (0x2000)

#define PCI_SIRIUS_SRS_CLK_BLK_DUT_CORE_CLK_OUT_DIV1  (0x0020)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_CORE_CLK_OUT_DIV2  (0x0024)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_CORE_CLK_OUT_DIV3  (0x001C)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_REG_CLK_OUT_DIV1   (0x0028)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_REG_CLK_OUT_DIV2   (0x002C)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_CORE_CLK_MULT1     (0x0050)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_CORE_CLK_MULT2     (0x0054)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_CORE_CLK_MULT3     (0x004C)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_CORE_VLK_IN_DIV    (0x0058)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_SYS_CLK_OUT_DIV1   (0x0220)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_SYS_CLK_OUT_DIV2   (0x0224)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_SYS_CLK_OUT_DIV3   (0x021C)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_MEM_CLK_OUT_DIV1   (0x0228)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_MEM_CLK_OUT_DIV2   (0x022C)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_SYS_CLK_MULT1      (0x0250)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_SYS_CLK_MULT2      (0x0254)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_SYS_CLK_MULT3      (0x024C)
#define PCI_SIRIUS_SRS_CLK_BLK_DUT_SYS_CLK_IN_DIV     (0x0258)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_PIXEL_CLK_OUT_DIV1 (0x0620)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_PIXEL_CLK_OUT_DIV2 (0x0624)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_PIXEL_CLK_OUT_DIV3 (0x061C)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_MEM_CLK_OUT_DIV1   (0x0628)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_MEM_CLK_OUT_DIV2   (0x062C)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_PIXEL_CLK_MULT1    (0x0650)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_PIXEL_CLK_MULT2    (0x0654)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_PIXEL_CLK_MULT3    (0x064C)
#define PCI_SIRIUS_SRS_CLK_BLK_PDP_PIXEL_CLK_IN_DIV   (0x0658)

#define PCI_SIRIUS_SRS_REG_SIZE                       (0x1000)

/* Interrupts are part of CORE */
#define PCI_SIRIUS_CORE_INTERRUPT_STATUS                (0x0218)
#define PCI_SIRIUS_CORE_INTERRUPT_ENABLE                (0x021C)
#define PCI_SIRIUS_CORE_INTERRUPT_CLR                   (0x0220)
#define PCI_SIRIUS_CORE_INTERRUPT_TEST                  (0x0224)
#define PCI_SIRIUS_CORE_INTERRUPT_TIMEOUT_CLR           (0x0228)

#define PCI_SIRIUS_CORE_INTERRUPT_TIMEOUT_CLR_CLR       (1 << 1)

/* interrupt bits definitions */
#define SIRIUS_INTERRUPT_MASTER_ENABLE                  (1 << 31)

#define SIRIUS_INTERRUPT_DUT0                           (1 << 0)
#define SIRIUS_INTERRUPT_DUT1                           (1 << 1)
#define SIRIUS_INTERRUPT_I2C                            (1 << 2)
#define SIRIUS_INTERRUPT_SPI                            (1 << 3)
#define SIRIUS_INTERRUPT_PDP                            (1 << 1)
#define SIRIUS_INTERRUPT_APM                            (1 << 4)
#define SIRIUS_INTERRUPT_ALL (SIRIUS_INTERRUPT_DUT0 | SIRIUS_INTERRUPT_DUT1 | SIRIUS_INTERRUPT_I2C | \
			SIRIUS_INTERRUPT_SPI | SIRIUS_INTERRUPT_PDP | SIRIUS_INTERRUPT_APM)


/* Sirius - Device Under Test (DUT) register bar */
#define PCI_SIRIUS_DUT_REGS_BAR (2)
#define PCI_SIRIUS_DUT_MEM_BAR  (4)

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

enum pci_irq_type {
	IRQ_TYPE_AUTO = 0,
	IRQ_TYPE_INTA = 1,
	IRQ_TYPE_MSI  = 2,
};

static unsigned long pci_irq_type = IRQ_TYPE_AUTO;
module_param(pci_irq_type, ulong, 0444);
MODULE_PARM_DESC(pci_irq_type, "Type of IRQ: 0: Auto, 1: INTA, 2: MSI");

/* Some Orion DUT images include two of them, so we need to allow to select which one to use at load time */
static unsigned long dut_id = 0;
module_param(dut_id, ulong, 0444);
MODULE_PARM_DESC(dut_id, "DUT the driver try to address. valid: {0, 1}, (default: 0)");

/* Maximum DUT_ID allowed */
#define MAX_DUT_ID (1)

static uint32_t sirius_dut_register_offset[] = {
	0x00000000, /* DUT 0 */
	0x20000000, /* DUT 1 */ 
};

static uint32_t sirius_dut_interrupt_bit[] = {
	SIRIUS_INTERRUPT_DUT0, /* DUT 0 */
	SIRIUS_INTERRUPT_DUT1, /* DUT 1 */
};


/*
 * Special handling (not implemented) is required for the VHA device
 * to be able to access both carveout buffers (internal memory) and
 * dmabuf buffers (system memory).The latter have to go through
 * the system bus to be accessed whereas the former do not.
 */
static struct heap_config vha_plat_fpga_heap_configs[] = {
	/* Primary heap used for internal allocations */
#ifdef FPGA_BUS_MASTERING
#error Bus mastering not supported
	{
		.type = -1, /* selected with fpga_heap_type */
		.options = {
			.unified.gfp_type = GFP_DMA32 | __GFP_ZERO,
			.coherent.gfp_flags = GFP_DMA32 | __GFP_ZERO,
		},
		.to_dev_addr = NULL,
		.to_host_addr = NULL,
	},
#elif CONFIG_GENERIC_ALLOCATOR
	{
		.type = IMG_MEM_HEAP_TYPE_CARVEOUT,
		/* .options.carveout to be filled at run time */
		/* .to_dev_addr to be filled at run time */
		/* .to_host_addr to be filled at run time */
		.cache_attr = IMG_MEM_ATTR_WRITECOMBINE,
	},
#else
#error Neither FPGA_BUS_MASTERING or CONFIG_GENERIC_ALLOCATOR was defined
#endif

	/* Secondary heap used for importing an external memory */
#ifdef CONFIG_DMA_SHARED_BUFFER
	{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		.to_dev_addr = NULL,
		.to_host_addr = NULL,
	},
#else
#warning "Memory importing not supported!"
#endif
};

static const int vha_plat_fpga_heaps = sizeof(vha_plat_fpga_heap_configs)/
	sizeof(*vha_plat_fpga_heap_configs);

static const struct pci_device_id pci_pci_ids[] = {
	{ PCI_DEVICE(PCI_SIRIUS_VENDOR_ID, PCI_SIRIUS_DEVICE_ID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_pci_ids);

enum { SRS_REG_BANK, INTC_REG_BANK, DUT_REG_BANK, DUT_MEM_BANK };

struct imgpci_prvdata {
	int irq;

	struct {
		int bar;
		unsigned long addr;
		unsigned long size;
		void __iomem *km_addr;
	} reg_bank[4];

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
	return sprintf(buf, "VHA Orion driver version : " VERSION_STRING "\n");
}

static DRIVER_ATTR_RO(info);
static struct attribute *drv_attrs[] = {
	&driver_attr_info.attr,
	NULL
};

ATTRIBUTE_GROUPS(drv);

static struct img_pci_driver vha_pci_drv = {
	.pci_driver = {
		.name = "vha_orion",
		.id_table = pci_pci_ids,
		.probe = vha_plat_probe,
		.remove = vha_plat_remove,
		.driver = {
			.groups = drv_groups,
			.pm = &vha_pm_plat_ops,
		}
	},
};

static ulong maxmapsizeMB = (sizeof(void *) == 4) ? 400 : 1024;

/*
 * __regreg32 - Generic PCI bar read functions
 */
static inline unsigned int __readreg32(struct imgpci_prvdata *data,
		int bank, unsigned long offset)
{
	void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
			offset);
	return ioread32(reg);
}

/*
 * __writereg32 - Generic PCI bar write functions
 */
static inline void __writereg32(struct imgpci_prvdata *data,
		int bank, unsigned long offset, int val)
{
	void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr +
			offset);
	/*pr_err(">>> Writing to bank %d, offset 0x%04X value 0x%08X\n",
	 * bank, offset, val);*/
	iowrite32(val, reg);
}

/*
 * sirius_core_writereg32 - Write to Sirius control registers
 */
static inline void sirius_core_writereg32(struct imgpci_prvdata *data,
		unsigned long offset, int val)
{
	__writereg32(data, SRS_REG_BANK, offset, val);
}

/*
 * sirius_core_readreg32 - Read Sirius control registers
 */
static inline unsigned int sirius_core_readreg32(struct imgpci_prvdata *data,
		unsigned long offset)
{
	return __readreg32(data, SRS_REG_BANK, offset);
}

/*
 * sirius_intc_writereg32 - Write to Sirius control registers
 */
static inline void sirius_intc_writereg32(struct imgpci_prvdata *data,
		unsigned long offset, int val)
{
	__writereg32(data, INTC_REG_BANK, offset, val);
}

/*
 * sirius_intc_readreg32 - Read Sirius control registers
 */
static inline unsigned int sirius_intc_readreg32(struct imgpci_prvdata *data,
		unsigned long offset)
{
	return __readreg32(data, INTC_REG_BANK, offset);
}

/*
 * reset_dut - Reset the Device Under Test
 */
static void reset_dut(struct imgpci_prvdata *data)
{
	dev_dbg(&data->pci_dev->dev, "going to reset DUT fpga!\n");

	sirius_core_writereg32(data, PCI_SIRIUS_SRS_CORE_DUT_SOFT_RESETN, 0);

	udelay(100); /* arbitrary delays, just in case! */

	sirius_core_writereg32(data,
			PCI_SIRIUS_SRS_CORE_DUT_SOFT_RESETN,
			DUT_SOFT_RESETN_DUT_SOFT_RESETN_EXTERNAL);

	msleep(500);

	dev_dbg(&data->pci_dev->dev, "DUT fpga reset done!\n");
}

/*
 * sirius_enable_int - Enable an interrupt
 */
static inline void sirius_enable_int(struct imgpci_prvdata *data, uint32_t intmask)
{
	uint32_t irq_enabled = sirius_core_readreg32(data, PCI_SIRIUS_CORE_INTERRUPT_ENABLE);

	/* Only accept to enable DUT interrupt */
	intmask &= sirius_dut_interrupt_bit[dut_id];

	sirius_core_writereg32(data, PCI_SIRIUS_CORE_INTERRUPT_ENABLE,
							 irq_enabled | intmask | SIRIUS_INTERRUPT_MASTER_ENABLE);
}

/*
 * sirius_disable_int - Disable an interrupt
 */
static inline void sirius_disable_int(struct imgpci_prvdata *data, uint32_t intmask)
{
	uint32_t irq_enabled = sirius_core_readreg32(data, PCI_SIRIUS_CORE_INTERRUPT_ENABLE);

	/* Only accept to disable DUT interrupt */
	intmask &= sirius_dut_interrupt_bit[dut_id];

	sirius_core_writereg32(data, PCI_SIRIUS_CORE_INTERRUPT_ENABLE,
							 irq_enabled & ~intmask);
}

/*
 * sirius_read_int_status - Read interrupt status
 */
static inline uint32_t sirius_read_int_status(struct imgpci_prvdata *data)
{
	return sirius_core_readreg32(data, PCI_SIRIUS_CORE_INTERRUPT_STATUS);
}

/*
 * sirius_ack_int - Ack interrupts
 */
static inline void sirius_ack_int(struct imgpci_prvdata *data, uint32_t intstatus)
{
	unsigned int max_retries = 1000;

	while ((sirius_core_readreg32(data, PCI_SIRIUS_CORE_INTERRUPT_STATUS) & intstatus) && max_retries--)
		sirius_core_writereg32(data, PCI_SIRIUS_CORE_INTERRUPT_CLR,
								 (SIRIUS_INTERRUPT_MASTER_ENABLE | intstatus));

		/**
		 * Temporary until FPGA is updated:
		 * Clear the "timeout" regardless to it's status to prevent some bugs in there
		 */
	sirius_core_writereg32(data, PCI_SIRIUS_CORE_INTERRUPT_TIMEOUT_CLR, PCI_SIRIUS_CORE_INTERRUPT_TIMEOUT_CLR_CLR);
}

/*
 * pci_thread_irq - High latency interrupt handler
 */
static irqreturn_t pci_thread_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;

	return vha_handle_thread_irq(&dev->dev);
}

/*
 * pci_isr_cb - Low latency interrupt handler
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
	intstatus = sirius_read_int_status(data);

	/* Now handle the ints */
	if (intstatus & sirius_dut_interrupt_bit[dut_id]) {
		/* call real irq handler */
		ret = vha_handle_irq(&dev->dev);
	} else {
		/* Code made on purpose, on this target, the INT number cannot
		 * be shared as we are using MSI. So any interrupt which are not
		 * from the DUT are clearly spurious and unwanted interrupts and
		 * meaning that one device on Sirius is not properly configured.
		 */
		dev_warn(&dev->dev,
				"%s: unexpected or spurious interrupt [%x]!\n",
				__func__, intstatus);
		WARN_ON(1);
	}

	/* Ack the ints */
	sirius_ack_int(data, intstatus);

exit:
	return ret;
}

/**
 * sirius_allocate_registers - Allocate memory for a register (or memory) bank
 * @pci_dev: the pci device
 * @data: pointer to the data
 * @bank: bank to set
 * @bar: BAR where the register are
 * @base: base address in the BAR
 * @size: size of the register set
 */
static inline int sirius_allocate_registers(struct pci_dev *pci_dev,
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

	pr_debug("[bank %u] bar:%d addr:%pa size:0x%lx km:0x%p\n",
			bank, bar, &data->reg_bank[bank].addr,
			data->reg_bank[bank].size,
			&data->reg_bank[bank].km_addr);

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
		pr_debug("%s: dev missing, HW reset omitted\n",
				__func__);
	}

	/* Unregister the driver from the OS */
	pci_unregister_driver(&(vha_pci_drv.pci_driver));

	ret = vha_deinit();
	if (ret)
		pr_err("VHA driver deinit failed\n");

	return ret;
}

#define VHA_REGISTERS_START                        (_REG_START)
#define VHA_REGISTERS_END                          (_REG_SIZE)

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

	pr_debug("Mapping %zu bytes into kernel memory (Phys:%pa, Kptr:%p)\n", size, &addr, &kptr);
	pr_debug("[%c%c%c]\n",
			 (mattr & IMG_MEM_ATTR_UNCACHED) ? 'U' : '.',
			 (mattr & IMG_MEM_ATTR_CACHED) ? 'C' : '.',
			 (mattr & IMG_MEM_ATTR_WRITECOMBINE) ? 'W' : '.');

	return kptr;
}

static int carveout_put_kptr(void *addr)
{
	pr_debug("Unmapping kernel memory (Phys: %p)\n", addr);
	iounmap(addr);
	return 0;
}
#endif

/*
 * IO hooks: We are on a 32bit system so only 32bit access available.
 * NOTE: customer may want to use spinlock to avoid
 * problems with multi threaded IO access.
 *
 */
uint64_t vha_plat_read64(void *addr)
{
	return (uint64_t)readl(addr) | ((uint64_t)readl(addr + 4) << 32);
}

void vha_plat_write64(void *addr, uint64_t val)
{
	writel(val & 0xffffffff, addr);
	writel(((uint64_t)val >> 32), addr + 4);
}

static int vha_plat_probe(struct pci_dev *pci_dev,
		const struct pci_device_id *id)
{
	int ret = 0;
	unsigned int int_type;
	struct imgpci_prvdata *data;
	size_t maxmapsize = maxmapsizeMB * 1024 * 1024;
	unsigned long vha_base_mem, vha_mem_size;
	struct device *dev = &pci_dev->dev;
	int heap;

	dev_dbg(dev, "probing device, pci_dev: %p\n", dev);

	/* Enable the device */
	if (pci_enable_device(pci_dev))
		goto out_free;

	dev_info(dev, "%s dma_get_mask : %#llx\n",
			__func__, dma_get_mask(dev));

	if (dev->dma_mask) {
		dev_info(dev, "%s dev->dma_mask : %p : %#llx\n",
				__func__, dev->dma_mask, *dev->dma_mask);
	} else {
		dev_info(dev, "%s mask unset, setting coherent\n",
				__func__);
		dev->dma_mask = &dev->coherent_dma_mask;
	}

	dev_info(dev, "%s dma_set_mask %#llx\n",
			__func__, dma_get_mask(dev));
	ret = dma_set_mask(dev, dma_get_mask(dev));

	if (ret) {
		dev_err(dev, "%s failed to set dma mask\n", __func__);
		goto out_disable;
	}

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

	/* Allocate sirius base registers */
	ret = sirius_allocate_registers(pci_dev, data,
			SRS_REG_BANK, PCI_SIRIUS_SYS_CTRL_REGS_BAR,
			PCI_SIRIUS_SYS_CTRL_BASE_OFFSET,
			PCI_SIRIUS_SRS_REG_SIZE);
	if (ret) {
		dev_err(dev, "Can't allocate memory for sirius regs!");
		ret = -ENOMEM;
		goto out_release;
	}


	/* FIXME: Check if there is any way to know how many DUTs are on the system */
	if (dut_id > MAX_DUT_ID) {
		dev_err(dev, "Invalid DUT number (%ld), setting it to 0\n", dut_id);
		dut_id = 0;
	}

	/* Allocate DUT register space */
	ret = sirius_allocate_registers(pci_dev, data,
			DUT_REG_BANK, PCI_SIRIUS_DUT_REGS_BAR,
			VHA_REGISTERS_START + sirius_dut_register_offset[dut_id],
			VHA_REGISTERS_END);
	if (ret) {
		dev_err(dev, "Can't allocate memory for vha regs!");
		ret = -ENOMEM;
		goto out_release;
	}

	/* Allocate DUT memory space */
	vha_mem_size = pci_resource_len(pci_dev, PCI_SIRIUS_DUT_MEM_BAR);
	if (vha_mem_size > maxmapsize)
		vha_mem_size = maxmapsize;

	vha_base_mem = pci_resource_start(pci_dev, PCI_SIRIUS_DUT_MEM_BAR);

	/* change alloc size according to module parameter */
	if (pci_size)
		vha_mem_size = pci_size;

	/* We are not really allocating memory for that reg bank,
	 * so hand set values here: */
	data->reg_bank[DUT_MEM_BANK].bar = PCI_SIRIUS_DUT_MEM_BAR;
	data->reg_bank[DUT_MEM_BANK].addr = vha_base_mem;
	data->reg_bank[DUT_MEM_BANK].size = vha_mem_size;
	pr_debug("[bank %u] bar: %d addr: %pa size: 0x%lx\n",
			DUT_MEM_BANK, PCI_SIRIUS_DUT_MEM_BAR,
			&data->reg_bank[DUT_MEM_BANK].addr,
			data->reg_bank[DUT_MEM_BANK].size);


	/* Allocate MSI IRQ if any */
	switch (pci_irq_type) {
	default:
		int_type = PCI_IRQ_ALL_TYPES;
		break;
	case IRQ_TYPE_INTA:
		int_type = PCI_IRQ_LEGACY;
		break;
	case IRQ_TYPE_MSI:
		int_type = PCI_IRQ_MSI | PCI_IRQ_MSIX;
		break;
	}

	ret = pci_alloc_irq_vectors(pci_dev, 1, 1, int_type);
	if (ret < 0) {
		dev_err(dev, "Can't reserve requested interrupt!");
		goto out_release;
	}

	/* Get the proper IRQ */
	data->irq  = pci_irq_vector(pci_dev, 0);
	data->pci_dev = pci_dev;
	vha_pci_drv.pci_dev = pci_dev;

	reset_dut(data);

	/*
	 * We need to enable interrupts for the embedded device
	 * via the fpga interrupt controller...
	 */
	sirius_enable_int(data, sirius_dut_interrupt_bit[dut_id]);

#if 0
	/* Sirius does not seems to be able to do bus mastering,
	 * at least there is not configuration for it */

#ifdef FPGA_BUS_MASTERING
	dev_dbg(dev, "enabling FPGA bus mastering\n");
	sirius_core_writereg32(data, test_ctrl_reg, 0x0);
#else
	/* Route to internal RAM - this is reset value */
	dev_dbg(dev, "disabling FPGA bus mastering\n");
	sirius_core_writereg32(data, test_ctrl_reg, 0x1);
#endif

#endif

	/* patch heap config with PCI memory addresses */
	for (heap = 0; heap < vha_plat_fpga_heaps; heap++) {
		struct heap_config *cfg = &vha_plat_fpga_heap_configs[heap];

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
				dev_info(dev, "using %dMB CARVEOUT at %pa\n",
					 contig_size/1024/1024,
					 &contig_phys_start);
			} else {
				cfg->options.carveout.phys =
					data->reg_bank[DUT_MEM_BANK].addr;
				cfg->options.carveout.size =
					data->reg_bank[DUT_MEM_BANK].size;
				cfg->options.carveout.offs = pci_offset;
				cfg->to_dev_addr = carveout_to_dev_addr;
				cfg->to_host_addr = carveout_to_host_addr;
				dev_info(dev, "using %zuMB CARVEOUT from PCI at %pa\n",
					 cfg->options.carveout.size/1024/1024,
					 &cfg->options.carveout.phys);
			}
			/* IO memory access callbacks */
			cfg->options.carveout.get_kptr = carveout_get_kptr;
			cfg->options.carveout.put_kptr = carveout_put_kptr;

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
				/* We will fallback to the default pool anyway
					 goto out_release; */
			}
			break;
		}
	}

	ret = vha_add_dev(dev, vha_plat_fpga_heap_configs,
			vha_plat_fpga_heaps, data,
			data->reg_bank[DUT_REG_BANK].km_addr,
			data->reg_bank[DUT_REG_BANK].size);
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

	/* Install the ISR callback...*/
	ret = devm_request_threaded_irq(dev,
			data->irq, &pci_isr_cb,
			&pci_thread_irq, IRQF_SHARED,
			DEVICE_NAME, (void *)pci_dev);
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
	sirius_disable_int(data, sirius_dut_interrupt_bit[dut_id]);

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
		sirius_disable_int(data, sirius_dut_interrupt_bit[dut_id]);

		/* Unregister int */
		devm_free_irq(&dev->dev, data->irq, dev);

		pci_free_irq_vectors(dev);
#if 0
#ifdef FPGA_BUS_MASTERING
		/* Route to internal RAM - this is reset value */
		dev_dbg(&dev->dev, "disabling FPGA bus mastering\n");
		sirius_core_writereg32(data, PCI_SIRIUS_SYS_CTRL_REGS_BAR,
				test_ctrl_reg, 0x1);
#endif
#endif
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

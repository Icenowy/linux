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
#include <linux/pm_runtime.h>
#include <linux/mod_devicetable.h>

#include "uapi/version.h"
#include "vha_common.h"
#include "vha_plat.h"

#define DEVICE_NAME "vha"

#define IS_APOLLO_DEVICE(devid) ((devid) == PCI_APOLLO_DEVICE_ID)

/*
 * from TCF Support FPGA.Technical Reference
 * Manual.1.0.92.Internal Atlas GEN.External.doc:
 */
/* Altas - System control register bar */
#define PCI_ATLAS_SYS_CTRL_REGS_BAR (0)
/* Altas - System control register offset */
#define PCI_ATLAS_SYS_CTRL_REGS_OFFSET (0x0000)
/* Atlas - Offset of INTERRUPT_STATUS */
/*#define PCI_ATLAS_INTERRUPT_STATUS (0x00E0)*/
/* Atlas - Offset of INTERRUPT_ENABLE */
/*#define PCI_ATLAS_INTERRUPT_ENABLE (0x00F0)*/
/* Atlas - Offset of INTERRUPT_CLEAR */
/*#define PCI_ATLAS_INTERRUPT_CLEAR (0x00F8)*/
/* Atlas - Master interrupt enable */
#define PCI_ATLAS_MASTER_ENABLE (1<<31)
/* Atlas - Device interrupt */
#define PCI_ATLAS_DEVICE_INT (1<<13)
/* Atlas - SCB Logic soft reset */
#define PCI_ATLAS_SCB_RESET (1<<4)
/* Atlas - PDP2 soft reset */
#define PCI_ATLAS_PDP2_RESET (1<<3)
/* Atlas - PDP1 soft reset */
#define PCI_ATLAS_PDP1_RESET (1<<2)
/* Atlas - soft reset the DDR logic */
#define PCI_ATLAS_DDR_RESET (1<<1)
/* Atlas - soft reset the device under test */
#define PCI_ATLAS_DUT_RESET (1<<0)
#define PCI_ATLAS_RESET_REG_OFFSET (0x0080)
#define PCI_ATLAS_RESET_BITS (PCI_ATLAS_DDR_RESET | PCI_ATLAS_DUT_RESET \
		| PCI_ATLAS_PDP1_RESET | PCI_ATLAS_PDP2_RESET | \
		PCI_ATLAS_SCB_RESET)

/* Apollo - Offset of INTERRUPT_STATUS */
#define PCI_APOLLO_INTERRUPT_STATUS (0x00C8)
/* Apollo - Offset of INTERRUPT_ENABLE */
#define PCI_APOLLO_INTERRUPT_ENABLE (0x00D8)
/* Apollo - Offset of INTERRUPT_CLEAR */
#define PCI_APOLLO_INTERRUPT_CLEAR (0x00E0)
/* Apollo - DCM Logic soft reset */
#define PCI_APOLLO_DCM_RESET (1<<10)
#define PCI_APOLLO_RESET_BITS (PCI_ATLAS_RESET_BITS | PCI_APOLLO_DCM_RESET)

#define PCI_ATLAS_TEST_CTRL (0xb0)
#define PCI_APOLLO_TEST_CTRL (0x98)

#define PCI_ATLAS_VENDOR_ID (0x1010)
#define PCI_ATLAS_DEVICE_ID (0x1CF1)
#define PCI_APOLLO_DEVICE_ID (0x1CF2)

/* Number of core cycles used to measure the core clock frequency */
#define FREQ_MEASURE_CYCLES 0x7fffff

/*#define FPGA_IMAGE_REV_OFFSET (0x604)
 #define FPGA_IMAGE_REV_MASK (0xFFFF)*/

/* Parameters applicable when using bus master mode */
static unsigned long contig_phys_start;
module_param(contig_phys_start, ulong, 0444);
MODULE_PARM_DESC(contig_phys_start,
		"Physical address of start of contiguous region");
static uint32_t contig_size;
module_param(contig_size, uint, 0444);
MODULE_PARM_DESC(contig_size,
		"Size of contiguous region: takes precedence over any PCI based memory");
static uint32_t fpga_heap_type = IMG_MEM_HEAP_TYPE_UNIFIED;
module_param(fpga_heap_type, uint, 0444);
MODULE_PARM_DESC(fpga_heap_type, "Fpga primary heap type");

static unsigned long pci_size;
module_param(pci_size, ulong, 0444);
MODULE_PARM_DESC(pci_size,
		"physical size in bytes, when 0 (default), use all memory in the PCI bar");

static unsigned long pci_offset;
module_param(pci_offset, ulong, 0444);
MODULE_PARM_DESC(pci_offset, "offset from PCI bar start. (default: 0)");

static bool mem_static_kptr = true;
module_param(mem_static_kptr, bool, 0444);
MODULE_PARM_DESC(mem_static_kptr,
		"Creates static kernel mapping for fpga memory");

/*
 * Special handling (not implemented) is required for the VHA device
 * to be able to access both carvout buffers (internal memory) and
 * dmabuf buffers (system memory).The latter have to go through
 * the system bus to be accessed whereas the former do not.
 */
static struct heap_config vha_plat_fpga_heap_configs[] = {
	/* Primary heap used for internal allocations */
#ifdef FPGA_BUS_MASTERING
	{
		.type = -1, /* selected with fpga_heap_type */
		.options = {
			.unified.gfp_type = GFP_DMA32 | __GFP_ZERO,
			.unified.max_order = 4,
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
	},
#else
#error Neither FPGA_BUS_MASTERING or CONFIG_GENERIC_ALLOCATOR was defined
#endif

	/* Secondary heap used for importing an external memory */
#ifdef FPGA_BUS_MASTERING
	{
		.type = IMG_MEM_HEAP_TYPE_ANONYMOUS,
	},
#endif
#if CONFIG_DMA_SHARED_BUFFER
	{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		.to_dev_addr = NULL,
#ifndef FPGA_BUS_MASTERING
		.options.dmabuf = {
				.use_sg_dma = true,
		},
#endif
	},
#endif
};

static const int vha_plat_fpga_heaps =
	sizeof(vha_plat_fpga_heap_configs)/sizeof(*vha_plat_fpga_heap_configs);

static const struct pci_device_id pci_pci_ids[] = {
	{ PCI_DEVICE(PCI_ATLAS_VENDOR_ID, PCI_ATLAS_DEVICE_ID), },
	{ PCI_DEVICE(PCI_ATLAS_VENDOR_ID, PCI_APOLLO_DEVICE_ID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_pci_ids);

struct imgpci_prvdata {
	int irq;
	struct {
		unsigned long addr;
		unsigned long size;
		void __iomem *km_addr;
	} memmap[3];
	struct pci_dev *pci_dev;
};


struct img_pci_driver {
	struct pci_dev *pci_dev;
	struct pci_driver pci_driver;
};

static int vha_plat_probe(struct pci_dev *pci_dev,
		const struct pci_device_id *id);
static void vha_plat_remove(struct pci_dev *dev);

static int vha_plat_suspend(struct device *dev);
static int vha_plat_resume(struct device *dev);
static int vha_plat_runtime_idle(struct device *dev);
static int vha_plat_runtime_suspend(struct device *dev);
static int vha_plat_runtime_resume(struct device *dev);

static struct dev_pm_ops vha_pm_plat_ops = {
#ifdef FPGA_BUS_MASTERING
	/* Runtime pm will not work with fpga internal memory
	 * because pci bus driver suspend is also called,
	 * which disables core/mem clocks */
	SET_RUNTIME_PM_OPS(vha_plat_runtime_suspend,
			vha_plat_runtime_resume, vha_plat_runtime_idle)
#endif
	SET_SYSTEM_SLEEP_PM_OPS(vha_plat_suspend, vha_plat_resume)
};

static ssize_t info_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "VHA FPGA driver version : " VERSION_STRING "\n");
}

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
			.pm = &vha_pm_plat_ops,
			.groups = drv_groups,
		}
	},
};

static ulong maxmapsizeMB = (sizeof(void *) == 4) ? 400 : 1024;

static int interrupt_status_reg = -1;
static int interrupt_clear_reg = -1;
static int interrupt_enable_reg = -1;
static int test_ctrl_reg = -1;

static unsigned int fpga_readreg32(struct imgpci_prvdata *data,
		int bar, unsigned long offset
)
{
	void __iomem *reg =
		(void __iomem *)(data->memmap[bar].km_addr + offset);
	return ioread32(reg);
}

static void fpga_writereg32(struct imgpci_prvdata *data,
		int bar, unsigned long offset, int val)
{
	void __iomem *reg =
		(void __iomem *)(data->memmap[bar].km_addr + offset);
	iowrite32(val, reg);
}

static void reset_fpga(struct pci_dev *dev,
		struct imgpci_prvdata *data, unsigned int mask)
{
	uint32_t bits = 0;

	if (!dev)
		return;

	bits = PCI_APOLLO_RESET_BITS;

	dev_dbg(&dev->dev, "reset fpga!\n");
	bits &= mask;

	if (bits) {
		uint32_t val = fpga_readreg32(data, 0, PCI_ATLAS_RESET_REG_OFFSET);

		val &= ~bits;
		fpga_writereg32(data, 0, PCI_ATLAS_RESET_REG_OFFSET, val);
		udelay(100); /* arbitrary delays, just in case! */
		val |= bits;
		fpga_writereg32(data, 0, PCI_ATLAS_RESET_REG_OFFSET, val);
		/* If not only DUT is reset, add a delay */
		if (mask != PCI_ATLAS_DUT_RESET)
			msleep(100);
		else
			udelay(100); /* arbitrary delays, just in case! */
	}

	dev_dbg(&dev->dev, "reset fpga done!\n");
}

static void fpga_clear_irq(struct imgpci_prvdata *data, unsigned int intstatus)
{
	unsigned int max_retries = 1000;

	while (fpga_readreg32(data, PCI_ATLAS_SYS_CTRL_REGS_BAR,
				interrupt_status_reg) && max_retries--)
		fpga_writereg32(data, PCI_ATLAS_SYS_CTRL_REGS_BAR,
				interrupt_clear_reg,
				(PCI_ATLAS_MASTER_ENABLE | intstatus));
}

static irqreturn_t pci_thread_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;

	return vha_handle_thread_irq(&dev->dev);
}

static irqreturn_t pci_isrcb(int irq, void *dev_id)
{
	unsigned int intstatus;
	struct pci_dev *dev = (struct pci_dev *)dev_id;
	struct imgpci_prvdata *data = vha_get_plat_data(&dev->dev);
	irqreturn_t ret = IRQ_NONE;

	if (data == NULL || dev_id == NULL) {
		/* spurious interrupt: not yet initialised. */
		goto exit;
	}

	intstatus = fpga_readreg32(data,
			PCI_ATLAS_SYS_CTRL_REGS_BAR,
			interrupt_status_reg);

	if (intstatus) {

		ret = vha_handle_irq(&dev->dev);
		/*
		 * We need to clear interrupts for the embedded device
		 * via the fpga interrupt controller...
		 */
		fpga_clear_irq(data, intstatus);
	} else {
		/* either a spurious interrupt, or, more likely
		 * a shared interrupt line, which will be handled by another driver
		*/
		goto exit;
	}

exit:

	return ret;
}

/*
 * IO hooks : Address bus for hw registers is 32bit!
 */
uint64_t vha_plat_read64(void *addr)
{
	return (uint64_t)readl((const volatile void __iomem *)addr) |
				((uint64_t)readl((const volatile void __iomem *)addr + 4) << 32);
}

void vha_plat_write64(void *addr, uint64_t val)
{
	writel((uint32_t)(val & 0xffffffff), (volatile void __iomem *)addr);
	writel((uint32_t)(val >> 32),        (volatile void __iomem *)addr + 4);
}

int vha_plat_deinit(void)
{
	struct pci_dev *dev = vha_pci_drv.pci_dev;
	int ret;

	if (dev) {
		struct imgpci_prvdata *data = vha_get_plat_data(&dev->dev);

		if (data) {
			/* reset the hardware */
			reset_fpga(data->pci_dev, data, ~0);
		} else {
			dev_dbg(&dev->dev,
				"%s: prv data not found, HW reset omitted\n",
				__func__);
		}
	} else {
		pr_debug("%s: dev missing, HW reset omitted\n", __func__);
	}

	/* Unregister the driver from the OS */
	pci_unregister_driver(&(vha_pci_drv.pci_driver));

	ret = vha_deinit();
	if (ret)
		pr_err("VHA driver deinit failed\n");

	return ret;
}

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

	return kptr;
}

static int carveout_put_kptr(void *addr)
{
	iounmap((volatile void __iomem *)addr);
	return 0;
}
#endif

static int vha_plat_probe(struct pci_dev *pci_dev,
		const struct pci_device_id *id)
{
	int bar, ret = 0;
	struct imgpci_prvdata *data;
	size_t maxmapsize = maxmapsizeMB * 1024 * 1024;
	struct device *dev = &pci_dev->dev;
	int heap;

	dev_dbg(dev, "probing device, pci_dev\n");

	/* Enable the device */
	if (pci_enable_device(pci_dev))
		goto out_free;

	if (dev->dma_mask) {
		dev_info(dev, "%s dev->dma_mask : %#llx\n",
			 __func__, *dev->dma_mask);
	} else {
		dev_info(dev, "%s mask unset, setting coherent\n", __func__);
		dev->dma_mask = &dev->coherent_dma_mask;
	}
	dev_info(dev, "%s dma_set_mask %#llx\n", __func__, dma_get_mask(dev));
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
	dev_dbg(dev, "allocated imgpci_prvdata @ %p\n", data);
	memset(data, 0, sizeof(*data));
	for (bar = 0; bar < 3; bar++) {

		data->memmap[bar].size = pci_resource_len(pci_dev, bar);
		data->memmap[bar].addr = pci_resource_start(pci_dev, bar);
		if (bar == 2) {
			if (pci_size)
				data->memmap[bar].size = pci_size;
			/* ioremap fpga memory only when static mode is used */
			if (!mem_static_kptr)
				continue;
		}

		if (data->memmap[bar].size > maxmapsize) {
			/*
			 * We avoid mapping too big regions: we do not need
			 * such a big amount of memory and some times we do
			 * not have enough contiguous 'vmallocable' memory.
			 */
			dev_warn(dev, "not mapping all mem for bar %u\n", bar);
			data->memmap[bar].size = maxmapsize;
		}
		data->memmap[bar].km_addr = devm_ioremap(dev,
				pci_resource_start(pci_dev, bar),
				data->memmap[bar].size);

		dev_dbg(dev, "[bar %u] addr: 0x%lx size: 0x%lx km: 0x%p\n",
				bar, data->memmap[bar].addr,
				data->memmap[bar].size,
				data->memmap[bar].km_addr);
	}

	/* Get the IRQ...*/
	data->irq = pci_dev->irq;
	data->pci_dev = pci_dev;
	vha_pci_drv.pci_dev = pci_dev;

	reset_fpga(pci_dev, data, ~0);

	interrupt_status_reg = PCI_ATLAS_SYS_CTRL_REGS_OFFSET +
		PCI_APOLLO_INTERRUPT_STATUS;
	interrupt_clear_reg = PCI_ATLAS_SYS_CTRL_REGS_OFFSET +
		PCI_APOLLO_INTERRUPT_CLEAR;
	interrupt_enable_reg = PCI_ATLAS_SYS_CTRL_REGS_OFFSET +
		PCI_APOLLO_INTERRUPT_ENABLE;
	test_ctrl_reg = PCI_ATLAS_SYS_CTRL_REGS_OFFSET +
		PCI_APOLLO_TEST_CTRL;

	/*
	 * We need to enable interrupts for the embedded device
	 * via the fpga interrupt controller...
	 */
	{
		unsigned int ena;

		ena = fpga_readreg32(data, PCI_ATLAS_SYS_CTRL_REGS_BAR,
						 interrupt_enable_reg);
		ena |= PCI_ATLAS_MASTER_ENABLE | PCI_ATLAS_DEVICE_INT;

		fpga_writereg32(data, PCI_ATLAS_SYS_CTRL_REGS_BAR,
				interrupt_enable_reg, ena);

		fpga_clear_irq(data, ena);
	}

#ifdef FPGA_BUS_MASTERING
	dev_dbg(dev, "enabling FPGA bus mastering\n");
	fpga_writereg32(data, PCI_ATLAS_SYS_CTRL_REGS_BAR, test_ctrl_reg, 0x0);
#else
	/* Route to internal RAM - this is reset value */
	dev_dbg(dev, "disabling FPGA bus mastering\n");
	fpga_writereg32(data, PCI_ATLAS_SYS_CTRL_REGS_BAR, test_ctrl_reg, 0x1);
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
				dev_info(dev, "using %dMB CARVEOUT at x%lx\n",
					 contig_size/1024/1024,
					 contig_phys_start);
			} else {
				cfg->options.carveout.phys =
					data->memmap[2].addr;
				if (mem_static_kptr)
					cfg->options.carveout.kptr =
							data->memmap[2].km_addr;
				cfg->options.carveout.size =
					data->memmap[2].size;
				cfg->options.carveout.offs = pci_offset;
				cfg->to_dev_addr = carveout_to_dev_addr;
				cfg->to_host_addr = carveout_to_host_addr;
				dev_info(dev, "using %zuMB CARVEOUT from PCI at 0x%llx\n",
					 cfg->options.carveout.size/1024/1024,
					 cfg->options.carveout.phys);
			}
			/* IO memory access callbacks */
			if (!mem_static_kptr) {
				/* Dynamic kernel memory mapping */
				cfg->options.carveout.get_kptr = carveout_get_kptr;
				cfg->options.carveout.put_kptr = carveout_put_kptr;
			}

			break;
		}
#endif

		if (cfg->type == IMG_MEM_HEAP_TYPE_COHERENT) {
			ret = dma_declare_coherent_memory(dev,
					contig_phys_start, contig_phys_start,
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
				/*
				 * We will fallback to the default pool anyway
				 * goto out_release;
				 */
			}
			break;
		}
	}
#ifdef FPGA_BUS_MASTERING
	/* Allow the core driver to control pm_runtime */
	pm_runtime_allow(dev);
#endif

	ret = vha_add_dev(dev, vha_plat_fpga_heap_configs,
			vha_plat_fpga_heaps, data,
			data->memmap[1].km_addr, data->memmap[1].size);
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
	reset_fpga(pci_dev, data, PCI_ATLAS_DUT_RESET);
	{

		/*uint32_t fpga_rev = fpga_readreg32(data, 1,
				FPGA_IMAGE_REV_OFFSET) & FPGA_IMAGE_REV_MASK;
		dev_dbg(dev, "fpga image revision: 0x%x\n", fpga_rev);
		if (!fpga_rev || fpga_rev == 0xdead1) {
			dev_err(dev, "fpga revision incorrect (0x%x)!\n",
					fpga_rev);
			goto out_rm_dev;
		}*/
	}

	/* Install the ISR callback...*/
	ret = devm_request_threaded_irq(dev, data->irq, &pci_isrcb,
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
/*out_release:*/
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
		fpga_writereg32(data, PCI_ATLAS_SYS_CTRL_REGS_BAR,
				interrupt_enable_reg, 0x00000000);

#ifdef FPGA_BUS_MASTERING
		/* Route to internal RAM - this is reset value */
		dev_dbg(&dev->dev, "disabling FPGA bus mastering\n");
		fpga_writereg32(data,
				PCI_ATLAS_SYS_CTRL_REGS_BAR,
				test_ctrl_reg, 0x1);
#endif
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
	/* Release any declared mem regions */
	dma_release_declared_memory(&dev->dev);
#endif
	pci_release_regions(dev);
	pci_disable_device(dev);

	vha_rm_dev(&dev->dev);
#ifdef FPGA_BUS_MASTERING
	pm_runtime_forbid(&dev->dev);
#endif
}

#ifdef CONFIG_PM
static int vha_plat_suspend(struct device *dev)
{
	struct pci_dev *pci_dev = vha_pci_drv.pci_dev;
	struct imgpci_prvdata *data = vha_get_plat_data(dev);
	int ret;

	ret = vha_suspend_dev(dev);
	if (ret) {
		dev_dbg(dev, "suspend device\n");
		reset_fpga(pci_dev, data, PCI_ATLAS_DUT_RESET);
	} else
		dev_err(dev, "failed to suspend!\n");

	return ret;
}

static int vha_plat_resume(struct device *dev)
{
	struct pci_dev *pci_dev = vha_pci_drv.pci_dev;
	struct imgpci_prvdata *data = vha_get_plat_data(dev);
	int ret;

	dev_dbg(dev, "resume device\n");
	reset_fpga(pci_dev, data, PCI_ATLAS_DUT_RESET);
	ret = vha_resume_dev(dev);
	if (ret)
		dev_err(dev, "failed to resume!\n");

	return ret;
}

static int __maybe_unused vha_plat_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static int __maybe_unused vha_plat_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static int __maybe_unused vha_plat_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}
#endif

int vha_plat_init(void)
{
	int ret;

#ifdef FPGA_BUS_MASTERING
	vha_plat_fpga_heap_configs[0].type = fpga_heap_type;
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

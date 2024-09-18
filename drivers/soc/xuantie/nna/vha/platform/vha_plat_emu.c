/*!
 *****************************************************************************
 *
 * @File       vha_plat_emu.c
 * ---------------------------------------------------------------------------
 *
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

#include "uapi/version.h"
#include "vha_common.h"
#include "vha_plat.h"

#define DEVICE_NAME "vha"

/*
 * Spec:
 * Emulator PCIe In-Circuit Interface Card.Technical
 *   Reference Manual.1.0.24.External PSTDRW.External
 */

/* Emulator address range 0x4000-0x4FFF */
#define PCI_EMU_SYS_CTRL_REGS_BAR (0)
/* Offset of INTERRUPT_ENABLE */
#define PCI_EMU_INTERRUPT_ENABLE_OFS (0x4048)
/* master interrupt enable - default high */
#define PCI_EMU_IRQ_ENABLE (1<<0)
#define PCI_EMU_IRQ_HIGH (1<<1)

/* Emulator reset offset */
#define PCI_EMU_RESET_OFS (0x4000)
/* Emulator reset bits */
#define PCI_EMU_RESET_LOGIC (1<<0)
#define PCI_EMU_RESET_DUT   (1<<1)

#define PCI_EMU_VENDOR_ID (0x1010)
#define PCI_EMU_DEVICE_ID (0x1CE3)

#define NUM_EMU_BARS      3
#define EMU_REG_BAR       PCI_EMU_SYS_CTRL_REGS_BAR
#define NNA_REG_BAR       1
#define NNA_MEM_BAR       2

/* Number of core cycles used to measure the core clock frequency */
#define FREQ_MEASURE_CYCLES 0x7fffff

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

static unsigned long poll_interrupts = 1;   /* Enabled by default */
module_param(poll_interrupts, ulong, 0444);
MODULE_PARM_DESC(poll_interrupts, "Poll for interrupts? 0: No, 1: Yes");

static unsigned long irq_poll_delay_us = 10000; /* 10 ms */
module_param(irq_poll_delay_us, ulong, 0444);
MODULE_PARM_DESC(irq_poll_delay_us, "Delay in us between each interrupt poll");

static bool mem_static_kptr = true;
module_param(mem_static_kptr, bool, 0444);
MODULE_PARM_DESC(mem_static_kptr,
		"Creates static kernel mapping for fpga memory");

static struct heap_config vha_plat_emu_heap_configs[] = {
#ifdef CONFIG_GENERIC_ALLOCATOR
	{
		.type = IMG_MEM_HEAP_TYPE_CARVEOUT,
		/* .options.carveout to be filled at run time */
		/* .to_dev_addr to be filled at run time */
		/* .to_host_addr to be filled at run time */
	},
#else
#error CONFIG_GENERIC_ALLOCATOR was not defined
#endif
#if CONFIG_DMA_SHARED_BUFFER
	{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		.to_dev_addr = NULL,
		.options.dmabuf = {
				.use_sg_dma = true,
		},
	},
#else
#warning "Memory importing not supported!"
#endif
};

static const int vha_plat_emu_heaps =
	sizeof(vha_plat_emu_heap_configs)/sizeof(*vha_plat_emu_heap_configs);

static const struct pci_device_id pci_pci_ids[] = {
	{ PCI_DEVICE(PCI_EMU_VENDOR_ID, PCI_EMU_DEVICE_ID) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_pci_ids);

struct imgpci_prvdata {
	int irq;
	struct {
		unsigned long addr;
		unsigned long size;
		void __iomem *km_addr;
	} memmap[NUM_EMU_BARS];
	struct pci_dev *pci_dev;
	int irq_poll;
	struct delayed_work irq_work;
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

static SIMPLE_DEV_PM_OPS(vha_pm_plat_ops,
		vha_plat_suspend, vha_plat_resume);

static ssize_t info_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "VHA EMU driver version : " VERSION_STRING "\n");
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
			.groups = drv_groups,
			.pm = &vha_pm_plat_ops,
		}
	},
};

static ulong maxmapsizeMB = (sizeof(void *) == 4) ? 400 : 1024;

#if 0
static unsigned int emu_readreg32(struct imgpci_prvdata *data,
		int bar, unsigned long offset
)
{
	void __iomem *reg =
		(void __iomem *)(data->memmap[bar].km_addr + offset);
	return ioread32(reg);
}
#endif

static void emu_writereg32(struct imgpci_prvdata *data,
		int bar, unsigned long offset, int val)
{
	void __iomem *reg =
		(void __iomem *)(data->memmap[bar].km_addr + offset);
	iowrite32(val, reg);
}

static void reset_emu(struct pci_dev *dev,
		struct imgpci_prvdata *data)
{
	if (!dev)
		return;

	emu_writereg32(data, PCI_EMU_SYS_CTRL_REGS_BAR,
			PCI_EMU_RESET_OFS,
			~(PCI_EMU_RESET_LOGIC|PCI_EMU_RESET_DUT));
	mdelay(100);
	emu_writereg32(data, PCI_EMU_SYS_CTRL_REGS_BAR,
			PCI_EMU_RESET_OFS,
			PCI_EMU_RESET_LOGIC|PCI_EMU_RESET_DUT);
}

static irqreturn_t pci_thread_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;

	return vha_handle_thread_irq(&dev->dev);
}

static irqreturn_t pci_handle_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;
	struct imgpci_prvdata *data = vha_get_plat_data(&dev->dev);
	irqreturn_t ret = IRQ_NONE;

	if (data == NULL || dev_id == NULL) {
		/* spurious interrupt: not yet initialised. */
		goto exit;
	}

	ret = vha_handle_irq(&dev->dev);
exit:
	return ret;
}

/* Interrupt polling function */
static void pci_poll_interrupt(struct work_struct *work)
{
	struct imgpci_prvdata *data = container_of(work,
			struct imgpci_prvdata, irq_work.work);
	struct pci_dev *dev = data->pci_dev;
	int ret;

	if (!data->irq_poll)
		return;

	preempt_disable();
	ret = vha_handle_irq(&dev->dev);
	preempt_enable();
	if (ret == IRQ_WAKE_THREAD)
		vha_handle_thread_irq(&dev->dev);

	/* retrigger */
	schedule_delayed_work(&data->irq_work,
			usecs_to_jiffies(irq_poll_delay_us));
}

/*
 * IO hooks.
 * NOTE: customer may want to use spinlock to avoid
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

int vha_plat_deinit(void)
{
	struct pci_dev *dev = vha_pci_drv.pci_dev;
	int ret;

	if (dev) {
		struct imgpci_prvdata *data = vha_get_plat_data(&dev->dev);

		if (data) {
			if (poll_interrupts) {
				data->irq_poll = 0;
				cancel_delayed_work_sync(&data->irq_work);
			}
			/* reset the emulator */
			reset_emu(data->pci_dev, data);
			emu_writereg32(data,
					PCI_EMU_SYS_CTRL_REGS_BAR,
					PCI_EMU_INTERRUPT_ENABLE_OFS,
					~PCI_EMU_IRQ_ENABLE);
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
#ifdef CONFIG_GENERIC_ALLOCATOR
	int heap;
#endif

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
	for (bar = 0; bar < NUM_EMU_BARS; bar++) {
		data->memmap[bar].addr = pci_resource_start(pci_dev, bar);
		data->memmap[bar].size = pci_resource_len(pci_dev, bar);
		if (data->memmap[bar].size > maxmapsize) {
			/*
			 * We avoid mapping too big regions: we do not need
			 * such a big amount of memory and some times we do
			 * not have enough contiguous 'vmallocable' memory.
			 */
			dev_warn(dev, "not mapping all mem for bar %u\n", bar);
			data->memmap[bar].size = maxmapsize;
		}

		if (bar == NNA_MEM_BAR) {
			/* Change memory size according to module parameter */
			if (pci_size)
				data->memmap[bar].size = pci_size;

			/* ioremap fpga memory only when static mode is used */
			if (!mem_static_kptr)
				continue;
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		data->memmap[bar].km_addr = devm_ioremap_nocache(dev,
				pci_resource_start(pci_dev, bar),
				data->memmap[bar].size);
#else
		data->memmap[bar].km_addr = devm_ioremap(dev,
				pci_resource_start(pci_dev, bar),
				data->memmap[bar].size);
#endif

		dev_dbg(dev, "[bar %u] addr: 0x%lx size: 0x%lx km: 0x%p\n",
				bar, data->memmap[bar].addr,
				data->memmap[bar].size,
				data->memmap[bar].km_addr);
	}

	/* Get the IRQ...*/
	data->irq = pci_dev->irq;
	data->pci_dev = pci_dev;
	vha_pci_drv.pci_dev = pci_dev;

	reset_emu(pci_dev, data);

	if (!poll_interrupts) {
		/* Enable interrupts */
		emu_writereg32(data, PCI_EMU_SYS_CTRL_REGS_BAR,
				PCI_EMU_INTERRUPT_ENABLE_OFS,
				PCI_EMU_IRQ_ENABLE | PCI_EMU_IRQ_HIGH);
	}


	/* patch heap config with PCI memory addresses */
	for (heap = 0; heap < vha_plat_emu_heaps; heap++) {
		struct heap_config *cfg = &vha_plat_emu_heap_configs[heap];
#ifdef CONFIG_GENERIC_ALLOCATOR
		if (cfg->type == IMG_MEM_HEAP_TYPE_CARVEOUT) {
			cfg->options.carveout.phys = data->memmap[NNA_MEM_BAR].addr;
			if (mem_static_kptr)
				cfg->options.carveout.kptr =
						data->memmap[NNA_MEM_BAR].km_addr;
			cfg->options.carveout.size = data->memmap[NNA_MEM_BAR].size;
			cfg->options.carveout.offs = pci_offset;
			cfg->to_dev_addr = carveout_to_dev_addr;
			cfg->to_host_addr = carveout_to_host_addr;

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
	}

	ret = vha_add_dev(dev, vha_plat_emu_heap_configs,
			vha_plat_emu_heaps, data,
			data->memmap[NNA_REG_BAR].km_addr, data->memmap[NNA_REG_BAR].size);
	if (ret) {
		dev_err(dev, "failed to intialize driver core!\n");
		goto out_release;
	}

	if (!poll_interrupts) {
		/* Install the ISR callback...*/
		ret = devm_request_threaded_irq(dev, data->irq, &pci_handle_irq,
				&pci_thread_irq, IRQF_SHARED, DEVICE_NAME,
				(void *)pci_dev);
		if (ret) {
			dev_err(dev, "failed to request irq!\n");
			goto out_rm_dev;
		}
		dev_dbg(dev, "registered irq %d\n", data->irq);
	} else {
		INIT_DELAYED_WORK(&data->irq_work, pci_poll_interrupt);
		data->irq_poll = 1;
		/* Start the interrupt poll */
		schedule_delayed_work(&data->irq_work,
				usecs_to_jiffies(irq_poll_delay_us));
	}

	/* Try to calibrate the core if needed */
	ret = vha_dev_calibrate(dev, FREQ_MEASURE_CYCLES);
	if (ret) {
		dev_err(dev, "%s: Failed to start clock calibration!\n", __func__);
		goto out_rm_dev;
	}
	return ret;

out_rm_dev:
	vha_rm_dev(dev);
out_release:
	pci_release_regions(pci_dev);
out_disable:
	pci_disable_device(pci_dev);
out_free:
	return ret;
}

static void vha_plat_remove(struct pci_dev *dev)
{
	dev_dbg(&dev->dev, "removing device\n");

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

/*
 * coding style for emacs
 *
 * Local variables:
 * indent-tabs-mode: t
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 */

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

#include "uapi/version.h"
#include "vha_common.h"
#include "vha_plat.h"

#define DEVICE_NAME "vha"

static unsigned long carveout_phys_start;
module_param(carveout_phys_start, ulong, 0444);
MODULE_PARM_DESC(carveout_phys_start,
		"Physical address of start of carveout region");
static uint32_t carveout_size;
module_param(carveout_size, uint, 0444);
MODULE_PARM_DESC(carveout_size,
		"Size of carveout region: takes precedence over any PCI based memory");

#define IMG_PCI_VENDOR_ID 0x1010
#define IMG_PCI_DEVICE_ID 0x1002
#define PCI_BAR_DEV       0
/* PCI and PCI-E do not support 64bit devices on BAR1: 64bit uses 2 bars */
#define PCI_BAR_RAM       2
#define NUM_PCI_BARS      3

/* Number of core cycles used to measure the core clock frequency */
#define FREQ_MEASURE_CYCLES 0x7fffff

static struct heap_config vha_plat_pci_heap_configs[] = {
	/* first entry is the default heap */
	{
		.type = IMG_MEM_HEAP_TYPE_CARVEOUT,
		/* .options.carveout to be filled at run time */
		/* .to_dev_addr to be filled at run time */
		/* .to_host_addr to be filled at run time */
	},
	{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		/*.to_dev_addr = NULL,*/
		/*.to_host_addr = NULL,*/
	},
};

static const int vha_plat_fpga_heaps =
	sizeof(vha_plat_pci_heap_configs)/sizeof(*vha_plat_pci_heap_configs);

static const struct pci_device_id pci_pci_ids[] = {
	{ PCI_DEVICE(IMG_PCI_VENDOR_ID, IMG_PCI_DEVICE_ID), },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_pci_ids);

struct imgpci_prvdata {
	int irq;
	struct {
		unsigned long addr;
		unsigned long size;
		void __iomem *km_addr;
	} memmap[NUM_PCI_BARS];
	struct pci_dev *pci_dev;
};


struct img_pci_driver {
	struct pci_dev   *pci_dev;
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
	return sprintf(buf, "VHA dummy driver version : " VERSION_STRING "\n");
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

static irqreturn_t pci_thread_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;

	return vha_handle_thread_irq(&dev->dev);
}

static irqreturn_t pci_handle_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;
	irqreturn_t ret = IRQ_NONE;

	ret = vha_handle_irq(&dev->dev);

	return ret;
}

/*
 * IO hooks.
 * NOTE: customer may want to use spinlock to avoid
 * problems with multi threaded IO access
 */
uint64_t vha_plat_read64(void *addr)
{
	return readq(addr);
}

void vha_plat_write64(void *addr, uint64_t val)
{
	writeq(val, addr);
}

int vha_plat_deinit(void)
{
	int ret;

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

	if (addr >= base && addr < base + size)
		return addr - base;

	pr_err("%s: unexpected addr! base %llx size %zu addr %#llx\n",
				 __func__, base, size, addr);
	WARN_ON(1);
	return addr;
}

static phys_addr_t carveout_to_host_addr(union heap_options *options,
					phys_addr_t addr)
{
	phys_addr_t base = options->carveout.phys;
	size_t size = options->carveout.size;

	if (addr < size)
		return base + addr;

	pr_err("%s: unexpected addr! base %llx size %zu addr %#llx\n",
				 __func__, base, size, addr);
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
	iounmap(addr);
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

	dev_info(dev, "probed a VHA device, pci_dev: %x:%x\n",
		 pci_dev->vendor, pci_dev->device);

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
	for (bar = 0; bar < NUM_PCI_BARS; bar += PCI_BAR_RAM-PCI_BAR_DEV) {
		data->memmap[bar].addr = pci_resource_start(pci_dev, bar);
		data->memmap[bar].size = pci_resource_len(pci_dev, bar);
		if (bar == PCI_BAR_RAM) {
			/* Don't ioremap pci memory - it is mapped on demand */
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
		dev_info(dev, "[bar %u] addr: 0x%lx size: 0x%lx km: 0x%p\n",
				bar, data->memmap[bar].addr,
				data->memmap[bar].size,
				data->memmap[bar].km_addr);
	}

	/* Get the IRQ...*/
	data->irq = pci_dev->irq;
	data->pci_dev = pci_dev;
	vha_pci_drv.pci_dev = pci_dev;

	/* patch heap config with PCI memory addresses */
	for (heap = 0; heap < vha_plat_fpga_heaps; heap++) {
		struct heap_config *cfg = &vha_plat_pci_heap_configs[heap];

#ifdef CONFIG_GENERIC_ALLOCATOR
		if (cfg->type == IMG_MEM_HEAP_TYPE_CARVEOUT) {

			if (carveout_size && carveout_phys_start) {
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
				cfg->options.carveout.phys =
					carveout_phys_start;
				cfg->options.carveout.size =
					carveout_size;
				cfg->to_dev_addr = carveout_to_dev_addr;
				cfg->to_host_addr = carveout_to_host_addr;
				dev_info(dev, "using %dMB CARVEOUT at x%lx\n",
					 carveout_size/1024/1024,
					 carveout_phys_start);
			} else if (data->memmap[PCI_BAR_RAM].size) {
				cfg->options.carveout.phys =
					data->memmap[PCI_BAR_RAM].addr;
				if (carveout_size)
					cfg->options.carveout.size =
						carveout_size;
				else
					cfg->options.carveout.size =
						data->memmap[PCI_BAR_RAM].size;
				cfg->to_dev_addr = carveout_to_dev_addr;
				cfg->to_host_addr = carveout_to_host_addr;
				dev_info(dev, "using %ldMB CARVEOUT from PCI bar %d\n",
					 cfg->options.carveout.size/1024/1024,
					 PCI_BAR_RAM);
			}
			/* IO memory access callbacks */
			cfg->options.carveout.get_kptr = carveout_get_kptr;
			cfg->options.carveout.put_kptr = carveout_put_kptr;
			break;
		}
#endif
		/* THIS IS HACKY - just for testing dmabuf importing on qemu.
		 * Dma buf config - using carveout type for dmabuf exporter.
		 * Assuming memory just after carveout_phys_start +
		 * carveout_size and size of carveout_size */
		if (cfg->type == IMG_MEM_HEAP_TYPE_DMABUF) {
			cfg->options.carveout.phys =
				carveout_phys_start + carveout_size;
			cfg->options.carveout.size = carveout_size;
			cfg->to_dev_addr = carveout_to_dev_addr;
		}
	}

	ret = vha_add_dev(dev, vha_plat_pci_heap_configs,
			vha_plat_fpga_heaps, data,
			data->memmap[PCI_BAR_DEV].km_addr,
			data->memmap[PCI_BAR_DEV].size);
	if (ret) {
		dev_err(dev, "failed to intialize driver core!\n");
		goto out_release;
	}

	/* Install the ISR callback...*/
	ret = devm_request_threaded_irq(dev, data->irq, &pci_handle_irq,
			&pci_thread_irq, IRQF_SHARED, DEVICE_NAME,
			(void *)pci_dev);
	if (ret) {
		dev_err(dev, "failed to request irq!\n");
		goto out_rm_dev;
	}
	dev_dbg(dev, "registerd irq %d\n", data->irq);

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

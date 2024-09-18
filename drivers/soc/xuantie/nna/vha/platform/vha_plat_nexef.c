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
#include <linux/pm.h>
#include <linux/mod_devicetable.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include <linux/of_platform.h>

#include "vha_common.h"
#include "vha_plat.h"


#include <nexef_plat.h>
/* NNPU TC exports we need*/
#include <tc_drv.h>

#define DEVICE_NAME "vha"

/*
 * Special handling (not implemented) is required for the VHA device
 * to be able to access both carveout buffers (internal memory) and
 * dmabuf buffers (system memory).The latter have to go through
 * the system bus to be accessed whereas the former do not.
 */
static struct heap_config vha_plat_fpga_heap_configs[] = {
        /* Primary heap used for internal allocations */
#ifdef FPGA_BUS_MASTERING
#error Bus mastering not supported on this platform
#elif CONFIG_GENERIC_ALLOCATOR
{
		.type = IMG_MEM_HEAP_TYPE_CARVEOUT,
		/* .options.carveout to be filled at run time */
		/* .to_dev_addr to be filled at run time */
	},
#else
#error Neither FPGA_BUS_MASTERING or CONFIG_GENERIC_ALLOCATOR was defined
#endif

        /* Secondary heap used for importing an external memory */
#ifdef CONFIG_DMA_SHARED_BUFFER
{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		.to_dev_addr = NULL,
	},
#else
#warning "Memory importing not supported!"
#endif
};

/* Number of core cycles used to measure the core clock frequency */
#define FREQ_MEASURE_CYCLES 0x7fffff

static const int vha_plat_fpga_heaps = sizeof(vha_plat_fpga_heap_configs)/
                                       sizeof(*vha_plat_fpga_heap_configs);

static int vha_plat_probe(struct platform_device *pdev);
static int vha_plat_remove(struct platform_device *pdev);

static int vha_plat_suspend(struct platform_device *pdev, pm_message_t state);
static int vha_plat_resume(struct platform_device *pdev);

enum {
    PLATFORM_IS_NEXEF = 1,
};

static struct platform_device_id nna_platform_device_id_table[] = {
        { .name = NEXEF_NNA_DEVICE_NAME, .driver_data = PLATFORM_IS_NEXEF },
        { },
};

static struct platform_driver vha_platform_drv = {
        .probe = vha_plat_probe,
        .remove = vha_plat_remove,
        .suspend = vha_plat_suspend,
        .resume = vha_plat_resume,
        .driver = {
            .owner = THIS_MODULE,
            .name = DEVICE_NAME,
        },
        .id_table = nna_platform_device_id_table,
};

struct nna_driver_priv {
    struct platform_device *pdev;

    void __iomem *nna_regs;
    uint32_t      nna_size;

    /* Work for the threaded interrupt. */
    struct work_struct work;
};

/*
 * reset_dut - Reset the Device Under Test
 */
static void reset_dut(struct device *dev)
{
    /* Nothing yet until Odin baseboard is updated to support that */
    //tc_dut2_reset(dev);
}

/*
 * pci_thread_irq - High latency interrupt handler
 */
static void nna_soft_isr_cb(struct work_struct *work)
{
    struct nna_driver_priv *priv = container_of(work, struct nna_driver_priv, work);
    struct platform_device *pdev = priv->pdev;
    struct device *dev = &pdev->dev;

    vha_handle_thread_irq(dev);
}

/*
 * pci_isr_cb - Low latency interrupt handler
 */
static void nna_hard_isr_cb(void *pdev_id)
{
    struct platform_device *pdev = (struct platform_device *)pdev_id;
    struct device *dev = &pdev->dev;
    struct nna_driver_priv *priv = (struct nna_driver_priv *)vha_get_plat_data(dev);

    irqreturn_t ret = IRQ_NONE;

    ret = vha_handle_irq(dev);

    if (ret == IRQ_WAKE_THREAD) {
        schedule_work(&priv->work);
    }
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

static int vha_plat_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct nna_driver_priv *priv;
    struct device *dev = &pdev->dev;
    struct nexef_nna_platform_data *platdata;
    struct resource *nna_registers;
    uint64_t vha_mem_base, vha_mem_size;
    uint64_t vha_mem_phys_offset = 0;
    int heap;

    dev_info(dev, "%s dma_get_mask : %#llx\n", __func__, dma_get_mask(dev));

    priv = devm_kzalloc(dev, sizeof(struct nna_driver_priv), GFP_KERNEL);
    if (!priv) {
        ret = -ENOMEM;
        goto out_no_free;
    }

    priv->pdev = pdev;

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
        goto out_dma_free;
    }


    /* Allocate dut2 registers */
    nna_registers = platform_get_resource_byname(pdev,
            IORESOURCE_MEM, "nna-regs");
    if (!nna_registers) {
        ret = -EIO;
        goto out_dma_free;
    }

    priv->nna_regs = devm_ioremap_resource(dev, nna_registers);
    if (!priv->nna_regs) {
        ret = -EIO;
        goto out_dma_free;
    }
    priv->nna_size = nna_registers->end - nna_registers->start;

    /* Get infos for DUT memory */
    platdata = dev_get_platdata(dev);

    /* Get out mem specs */
    vha_mem_size = platdata->nna_memory_size;
    vha_mem_base = platdata->nna_memory_base;
    vha_mem_phys_offset = platdata->nna_memory_offset;

    dev_dbg(dev, "PCI memory: base: %#llX - size: %#llX - offset: %#llX",
            vha_mem_base, vha_mem_size, vha_mem_phys_offset);

    /* patch heap config with PCI memory addresses */
    for (heap = 0; heap < vha_plat_fpga_heaps; heap++) {
        struct heap_config *cfg = &vha_plat_fpga_heap_configs[heap];

        switch(cfg->type) {

        case IMG_MEM_HEAP_TYPE_CARVEOUT:
            cfg->options.carveout.phys = vha_mem_base;
            cfg->options.carveout.size = vha_mem_size;
            cfg->options.carveout.offs = vha_mem_phys_offset;

            cfg->to_dev_addr = carveout_to_dev_addr;
			cfg->to_host_addr = carveout_to_host_addr;

			/* IO memory access callbacks */
			cfg->options.carveout.get_kptr = carveout_get_kptr;
			cfg->options.carveout.put_kptr = carveout_put_kptr;

			break;

        case IMG_MEM_HEAP_TYPE_DMABUF: /* Nothing to do here */
            break;

        default:
            dev_err(dev, "Unsupported heap type %d!\n", cfg->type);
            break;
		}
    }

    reset_dut(dev->parent);

    ret = vha_add_dev(dev,
                      vha_plat_fpga_heap_configs,
                      vha_plat_fpga_heaps,
                      priv,
					  priv->nna_regs,
					  priv->nna_size);
    if (ret) {
        dev_err(dev, "failed to initialize driver core!\n");
        goto out_dma_free;
    }

    /*
     * Reset FPGA DUT only after disabling clocks in
     * vha_add_dev()-> get properties.
     * This workaround is required to ensure that
     * clocks (on daughter board) are enabled for test slave scripts to
     * read FPGA build version register.
     */
    reset_dut(dev->parent);

    /* Install the ISR callback...*/
    INIT_WORK(&priv->work, nna_soft_isr_cb);

    ret = tc_set_interrupt_handler(dev->parent, TC_INTERRUPT_NNA, nna_hard_isr_cb, pdev);

    ret |= tc_enable_interrupt(dev->parent, TC_INTERRUPT_NNA);
    if (ret) {
        dev_err(dev, "failed to request irq!\n");
        goto out_rm_dev;
    }

		/* Try to calibrate the core if needed */
		ret = vha_dev_calibrate(dev, FREQ_MEASURE_CYCLES);
		if (ret) {
			dev_err(dev, "%s: Failed to start clock calibration!\n", __func__);
			goto out_rm_dev;
		}
		return ret;

out_rm_dev:
    /* Disable interrupt handler just in case it is enable which fail */
    tc_set_interrupt_handler(dev->parent, TC_INTERRUPT_NNA, NULL, NULL);

    vha_rm_dev(dev);

out_dma_free:
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
	/* Release any declared mem regions */
	dma_release_declared_memory(dev);
#endif

out_no_free:
    return ret;
}

static int vha_plat_remove(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct nna_driver_priv *priv =
            (struct nna_driver_priv *)vha_get_plat_data(dev);

    dev_dbg(dev, "removing device\n");

    /* Disable interrupts */
    tc_disable_interrupt(dev->parent, TC_INTERRUPT_NNA);
    tc_set_interrupt_handler(dev->parent, TC_INTERRUPT_NNA, NULL, NULL);
    /* Make sure there is no work in the queue */
    if (priv)
        cancel_work_sync(&priv->work);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,4,0)
	/* Release any declared mem regions */
	dma_release_declared_memory(dev);
#endif

    vha_rm_dev(dev);

    return 0;
}

#ifdef CONFIG_PM
static int vha_plat_suspend(struct platform_device *pdev, pm_message_t state)
{
    struct device *dev = &pdev->dev;
	return vha_suspend_dev(dev);
}

static int vha_plat_resume(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
	return vha_resume_dev(dev);
}
#endif

/* Functions called by vha_core */
int vha_plat_init(void)
{
    int ret;

    ret = platform_driver_register(&vha_platform_drv);
    if (ret) {
        pr_err("failed to register platform driver!\n");
        return ret;
    }

    return 0;
}

int vha_plat_deinit(void)
{
    int ret;

    //reset_dut();

    platform_driver_unregister(&vha_platform_drv);

    ret = vha_deinit();
    if (ret)
        pr_err("VHA driver deinit failed\n");

    return ret;
}

/*
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

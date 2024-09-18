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
#elif defined(CFG_SYS_MAGNA)
#include <hwdefs/magna_system.h>
#endif


#define DEVICE_NAME "vha"

#define IS_FROST_DEVICE(devid) ((devid) == PCI_FROST_DEVICE_ID)

/*
 * from ICE2 card Frost.Technical Reference Manual.docx
 */

#define PCI_FROST_VENDOR_ID (0x1AEE)
#define PCI_FROST_DEVICE_ID (0x1030)

/* Frost - System control register bar */
#define PCI_FROST_SYS_CTRL_REGS_BAR (0)

#define PCI_FROST_SYS_CTRL_BASE_OFFSET           (0x0000)
/* props */
#define PCI_FROST_CORE_ID                        (0x0000)
#define PCI_FROST_CORE_REVISION                  (0x0004)
#define PCI_FROST_CORE_CHANGE_SET                (0x0008)
#define PCI_FROST_CORE_USER_ID                   (0x000C)
#define PCI_FROST_CORE_USER_BUILD                (0x0010)
#define PCI_FROST_CORE_SW_IF_VERSION             (0x0014)
#define PCI_FROST_CORE_UC_IF_VERSION             (0x0018)
/* Interrupt mode */
#define PCI_FROST_CORE_EMU_INTERRUPT_CTRL        (0x0048)
/* Resets */
#define PCI_FROST_CORE_INTERNAL_RESETN           (0x0080)
#define PCI_FROST_CORE_EXTERNAL_RESETN           (0x0084)
#define PCI_FROST_CORE_INTERNAL_AUTO_RESETN      (0x008C)
/* Interrupts */
#define PCI_FROST_CORE_INTERRUPT_STATUS          (0x0100)
#define PCI_FROST_CORE_INTERRUPT_ENABLE          (0x0104)
#define PCI_FROST_CORE_INTERRUPT_CLR             (0x010C)
#define PCI_FROST_CORE_INTERRUPT_TEST            (0x0110)
#define PCI_FROST_CORE_INTERRUPT_TIMEOUT_CLR     (0x0114)
#define PCI_FROST_CORE_INTERRUPT_TIMEOUT         (0x0118)
/* MISC */
#define PCI_FROST_CORE_SYSTEM_ID                 (0x0120)
/* LEDs! */
#define PCI_FROST_CORE_DASH_LEDS                 (0x01A8)
/* Core stuff */
#define PCI_FROST_CORE_PCIE_TO_EMU_ADDR_OFFSET   (0x0204)
#define PCI_FROST_CORE_EMU_TO_PCIE_ADDR_OFFSET   (0x0208)
#define PCI_FROST_CORE_CORE_CONTROL              (0x0210)
#define PCI_FROST_CORE_EMU_CLK_CNT               (0x0214)

/* Interrupt bits */
#define PCI_FROST_CORE_EMU_INTERRUPT_CTRL_ENABLE (1 << 0)
#define PCI_FROST_CORE_EMU_INTERRUPT_CTRL_SENSE  (1 << 1)

/* core bits definitions */
#define INTERNAL_RESET_INTERNAL_RESETN_CMDA      (1 << 0)
#define INTERNAL_RESET_INTERNAL_RESETN_GIST      (1 << 1)
#define EXTERNAL_RESET_EXTERNAL_RESETN_EMU       (1 << 0)
#define INTERNAL_AUTO_RESETN_AUX                 (1 << 0)

/* interrupt bits definitions */
#define INT_INTERRUPT_MASTER_ENABLE              (0)  /*(1 << 31) - disabled */
#define INT_INTERRUPT_IRQ_TEST                   (1 << 30)
#define INT_INTERRUPT_CDMA                       (1 << 1)
#define INT_INTERRUPT_EMU                        (1 << 0)

#define INT_TEST_INTERRUPT_TEST                  (1 << 0)
#define INTERRUPT_MST_TIMEOUT_CLR                (1 << 1)
#define INTERRUPT_MST_TIMEOUT                    (1 << 0)

#define PCI_FROST_CORE_REG_SIZE                  (0x1000)

/* Frost - Device Under Test (DUT) register bar */
#define PCI_FROST_DUT_REG_BAR (2)
#define PCI_FROST_DUT_MEM_BAR (4)

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

static unsigned long poll_interrupts = 1;
module_param(poll_interrupts, ulong, 0444);
MODULE_PARM_DESC(poll_interrupts, "Poll for interrupts? 0: No, 1: Yes");

static unsigned long irq_poll_delay_us = 10000; /* 10 ms */
module_param(irq_poll_delay_us, ulong, 0444);
MODULE_PARM_DESC(irq_poll_delay_us, "Delay in us between each interrupt poll");

static bool irq_self_test;
module_param(irq_self_test, bool, 0444);
MODULE_PARM_DESC(irq_self_test, "Enable self irq test board feature");

static struct heap_config vha_dev_frost_heap_configs[] = {
	/* Primary heap used for internal allocations */
#if CONFIG_GENERIC_ALLOCATOR
	{
		.type = IMG_MEM_HEAP_TYPE_CARVEOUT,
		/* .options.carveout to be filled at run time */
		/* .to_dev_addr to be filled at run time */
	},
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

static const int vha_dev_frost_heaps = sizeof(vha_dev_frost_heap_configs)/
	sizeof(*vha_dev_frost_heap_configs);

static const struct pci_device_id pci_pci_ids[] = {
	{ PCI_DEVICE(PCI_FROST_VENDOR_ID, PCI_FROST_DEVICE_ID), },
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
	int irq_poll;
	struct delayed_work irq_work;
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
	return sprintf(buf, "VHA Frost driver version : " VERSION_STRING "\n");
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

static ulong maxmapsizeMB = (sizeof(void *) == 4) ? 400 : 2048;

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
		void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr + offset);
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
		void __iomem *reg = (void __iomem *)(data->reg_bank[bank].km_addr + offset);
		iowrite32(val & 0xFFFFFFFF, reg);
		iowrite32(val >> 32, reg + 4);
}

/**
 * frost_core_writereg32 - Write to Frost control registers
 * @data: pointer to the data
 * @offset: offset within bank
 * @val: value to be written
 */
static inline void frost_core_writereg32(struct imgpci_prvdata *data,
		unsigned long offset, int val)
{
	__writereg32(data, CORE_REG_BANK, offset, val);
}

/**
 * frost_core_readreg32 - Read Frost control registers
 * @data: pointer to the data
 * @offset: offset within bank
 */
static inline unsigned int frost_core_readreg32(struct imgpci_prvdata *data,
		unsigned long offset)
{
	return __readreg32(data, CORE_REG_BANK, offset);
}


static inline void frost_reset_int(struct imgpci_prvdata *data) {
	frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_ENABLE, 0);
	frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_CLR, 0xFFFFFFFF);
	frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_TIMEOUT, 0xFFFFFFFF);
	/* SENSE shall be low, because polarity is reversed */
	frost_core_writereg32(data, PCI_FROST_CORE_EMU_INTERRUPT_CTRL,
			PCI_FROST_CORE_EMU_INTERRUPT_CTRL_ENABLE);
}

/**
 * frost_enable_int - Enable an interrupt
 * @data: pointer to the data
 * @intmask: interrupt mask
 */
static inline void frost_enable_int(struct imgpci_prvdata *data,
		uint32_t intmask)
{
	uint32_t irq_enabled = frost_core_readreg32(data, PCI_FROST_CORE_INTERRUPT_ENABLE);

	frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_ENABLE, irq_enabled | intmask | INT_INTERRUPT_MASTER_ENABLE);
}

/**
 * frost_disable_int - Disable an interrupt
 * @data: pointer to the data
 * @intmask: interrupt mask
 */
static inline void frost_disable_int(struct imgpci_prvdata *data,
		uint32_t intmask)
{
	uint32_t irq_enabled = frost_core_readreg32(data, PCI_FROST_CORE_INTERRUPT_ENABLE);

	frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_ENABLE,
		irq_enabled & ~intmask);
}

/**
 * frost_test_int - Test an interrupt
 * @data: pointer to the data
 */
static inline void frost_test_int(struct imgpci_prvdata *data) {
	frost_enable_int(data, INT_INTERRUPT_IRQ_TEST);
	pr_warn("%s: trigger interrupt!\n", __func__);
	frost_core_writereg32(data, PCI_FROST_CORE_EMU_INTERRUPT_CTRL,
			PCI_FROST_CORE_EMU_INTERRUPT_CTRL_SENSE); 	/* SENSE shall be high */
	frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_TEST, INT_TEST_INTERRUPT_TEST);
}

/**
 * reset_dut - Reset the Device Under Test
 * @data: pointer to the data
 */
static void reset_dut(struct imgpci_prvdata *data)
{

	uint32_t internal_rst = frost_core_readreg32(data, PCI_FROST_CORE_INTERNAL_RESETN);
	uint32_t external_rst = frost_core_readreg32(data, PCI_FROST_CORE_EXTERNAL_RESETN);

	dev_dbg(&data->pci_dev->dev, "going to reset DUT frost!\n");

	frost_core_writereg32(data, PCI_FROST_CORE_INTERNAL_RESETN,
		internal_rst & ~(INTERNAL_RESET_INTERNAL_RESETN_GIST|
			INTERNAL_RESET_INTERNAL_RESETN_CMDA));
		frost_core_writereg32(data, PCI_FROST_CORE_EXTERNAL_RESETN,
		external_rst & ~(EXTERNAL_RESET_EXTERNAL_RESETN_EMU));

	udelay(100); /* arbitrary delays, just in case! */

	frost_core_writereg32(data, PCI_FROST_CORE_INTERNAL_RESETN, internal_rst);
	frost_core_writereg32(data, PCI_FROST_CORE_EXTERNAL_RESETN, external_rst);

	msleep(100);

	dev_dbg(&data->pci_dev->dev, "DUT frost reset done!\n");
}

/**
 * pci_thread_irq - High latency interrupt handler
 * @irq: irq number
 * @dev_id: pointer to private data
 */
static irqreturn_t frost_thread_irq(int irq, void *dev_id)
{
	struct pci_dev *dev = (struct pci_dev *)dev_id;

	return vha_handle_thread_irq(&dev->dev);
}

/**
 * frost_isr_clear - Clear an interrupt
 * @data: pointer to the data
 * @intstatus: interrupt status
 *
 * note: the reason of that function is unclear, it is taken from Apollo/Atlas code that have
 * the same interrupt handler as Frost, is it because of a bug?
 */
static void frost_isr_clear(struct imgpci_prvdata *data, unsigned int intstatus)
{
	unsigned int max_retries = 1000;

	while ((frost_core_readreg32(data, PCI_FROST_CORE_INTERRUPT_STATUS) & intstatus) && max_retries--) {
		frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_CLR,
			(INT_INTERRUPT_MASTER_ENABLE | intstatus));
	}

	if (!max_retries) {
		pr_warn("Can't clear irq ! disabling interrupts!\n");
		frost_reset_int(data);
	}
}


/**
 * pci_isr_cb - Low latency interrupt handler
 * @irq: irq number
 * @dev_id: pointer to private data
 */
static irqreturn_t frost_isr_cb(int irq, void *dev_id)
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
	intstatus = frost_core_readreg32(data, PCI_FROST_CORE_INTERRUPT_STATUS);

	/* Clear timeout bit just for sanity */
	frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_TIMEOUT_CLR,
			INTERRUPT_MST_TIMEOUT_CLR);

	if (intstatus & INT_INTERRUPT_IRQ_TEST) {
		/* Handle test int */
		pr_warn("Test interrupt OK! Switch back to normal mode!\n");
		frost_core_writereg32(data, PCI_FROST_CORE_INTERRUPT_TEST, 0);
		/* Disable irqs */
		frost_reset_int(data);
		ret = IRQ_HANDLED;
	}

	if (intstatus & INT_INTERRUPT_EMU) {
		/* call real irq handler */
		ret = vha_handle_irq(&dev->dev);
	}

	if (unlikely(intstatus == 0)) {
		/* most likely this is a shared interrupt line */
		dev_dbg(&dev->dev,
				"%s: unexpected or spurious interrupt [%x] (shared IRQ?)!\n",
			__func__, intstatus);
		goto exit;
	}

	/* Ack the ints */
	frost_isr_clear(data, intstatus);
exit:
	return ret;
}

/* Interrupt polling function */
static void frost_poll_interrupt(struct work_struct *work)
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

#if 0
	{
		uint32_t clk_cnt = frost_core_readreg32(data, PCI_FROST_CORE_EMU_CLK_CNT);
		pr_debug("%s: EMU clk_cnt%u\n", __func__, clk_cnt);
	}
#endif
	/* retrigger */
	schedule_delayed_work(&data->irq_work,
			usecs_to_jiffies(irq_poll_delay_us));
}

/**
 * frost_allocate_registers - Allocate memory for a register (or memory) bank
 * @pci_dev: pointer to pci device
 * @data: pointer to the data
 * @bank: bank to set
 * @bar: BAR where the register are
 * @base: base address in the BAR
 * @size: size of the register set
 */
static inline int frost_allocate_registers(struct pci_dev *pci_dev,
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
			if (poll_interrupts) {
				data->irq_poll = 0;
				cancel_delayed_work_sync(&data->irq_work);
			}

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

#define NNA_REG_BAR (PCI_FROST_DUT_REG_BAR)
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

	/* Allocate frost core registers */
	ret = frost_allocate_registers(pci_dev, data,
				CORE_REG_BANK, PCI_FROST_SYS_CTRL_REGS_BAR,
				PCI_FROST_SYS_CTRL_BASE_OFFSET,
				PCI_FROST_CORE_REG_SIZE);
	if (ret) {
		dev_err(dev, "Can't allocate memory for frost regs!");
		ret = -ENOMEM;
		goto out_release;
	}

	/* Display some infos */
	{
		uint32_t frost_id  = frost_core_readreg32(data, PCI_FROST_CORE_ID);
		uint32_t frost_rev = frost_core_readreg32(data, PCI_FROST_CORE_REVISION);
		uint32_t frost_cs  = frost_core_readreg32(data, PCI_FROST_CORE_CHANGE_SET);
		uint32_t frost_ui  = frost_core_readreg32(data, PCI_FROST_CORE_USER_ID);
		uint32_t frost_ub  = frost_core_readreg32(data, PCI_FROST_CORE_USER_BUILD);
		uint32_t frost_swif = frost_core_readreg32(data, PCI_FROST_CORE_SW_IF_VERSION);
		uint32_t frost_ucif = frost_core_readreg32(data, PCI_FROST_CORE_UC_IF_VERSION);

		pr_info("Found Frost board v%d.%d (ID:%X CS:%X UI:%X UB:%X SWIF:%X UCIF:%X)",
			(frost_rev >> 16) & 0xFFFF, frost_rev & 0xFFFF,
			frost_id, frost_cs, frost_ui, frost_ub, frost_swif, frost_ucif);
	}

	/* Allocate NNA register space */
	ret = frost_allocate_registers(pci_dev, data,
				NNA_REG_BANK, NNA_REG_BAR,
				NNA_REG_OFFSET,
				NNA_REG_SIZE);
	if (ret) {
		dev_err(dev, "Can't allocate memory for vha regs!");
		ret = -ENOMEM;
		goto out_release;
	}

	/* Allocate DUT memory space */
	vha_mem_size = pci_resource_len(pci_dev, PCI_FROST_DUT_MEM_BAR);
	if (vha_mem_size > maxmapsize)
		vha_mem_size = maxmapsize;

	vha_base_mem = pci_resource_start(pci_dev, PCI_FROST_DUT_MEM_BAR);

	/* change alloc size according to module parameter */
	if (pci_size)
		vha_mem_size = pci_size;

	/* We are not really allocating memory for that reg bank,
	 * so hand set values here: */
	data->reg_bank[MEM_REG_BANK].bar = PCI_FROST_DUT_MEM_BAR;
	data->reg_bank[MEM_REG_BANK].addr = vha_base_mem;
	data->reg_bank[MEM_REG_BANK].size = vha_mem_size;
	pr_debug("[bank %u] bar:%d addr: 0x%lx size: 0x%lx\n",
			MEM_REG_BANK, PCI_FROST_DUT_MEM_BAR,
				data->reg_bank[MEM_REG_BANK].addr,
				data->reg_bank[MEM_REG_BANK].size);

	/* Get the IRQ...*/
	data->irq = pci_dev->irq;
	data->pci_dev = pci_dev;
	vha_pci_drv.pci_dev = pci_dev;

	reset_dut(data);

	for (heap = 0; heap < vha_dev_frost_heaps; heap++) {
		struct heap_config *cfg = &vha_dev_frost_heap_configs[heap];

#ifdef CONFIG_GENERIC_ALLOCATOR
		if (cfg->type == IMG_MEM_HEAP_TYPE_CARVEOUT) {
			cfg->options.carveout.phys =
				data->reg_bank[MEM_REG_BANK].addr;
			cfg->options.carveout.size =
				data->reg_bank[MEM_REG_BANK].size;
			cfg->options.carveout.offs = pci_offset;
			cfg->to_dev_addr = carveout_to_dev_addr;
			cfg->to_host_addr = carveout_to_host_addr;
			/* IO memory access callbacks */
			cfg->options.carveout.get_kptr = carveout_get_kptr;
			cfg->options.carveout.put_kptr = carveout_put_kptr;
			/* Allocation order */
			cfg->options.carveout.pool_order = pool_alloc_order;
			break;
		}
#endif
	}

	ret = vha_add_dev(dev,
			vha_dev_frost_heap_configs,
			vha_dev_frost_heaps,
			data,
			data->reg_bank[NNA_REG_BANK].km_addr,
			data->reg_bank[NNA_REG_BANK].size);
	if (ret) {
		dev_err(dev, "failed to initialize driver core!\n");
		goto out_deinit;
	}

	if (!poll_interrupts) {
		/* Reset irqs at first */
		frost_reset_int(data);

		/* Install the ISR callback...*/
		ret = devm_request_threaded_irq(dev, data->irq, &frost_isr_cb,
				&frost_thread_irq, IRQF_SHARED, DEVICE_NAME,
				(void *)pci_dev);
		if (ret) {
			dev_err(dev, "failed to request irq!\n");
			goto out_rm_dev;
		}
		dev_dbg(dev, "registered irq %d\n", data->irq);

		if (irq_self_test) {
			/* Trigger Test interrupt */
			frost_test_int(data);
			/* Give some time to trigger test IRQ */
			msleep(10);
		} else {
			frost_enable_int(data, INT_INTERRUPT_EMU);
		}
	} else {
		INIT_DELAYED_WORK(&data->irq_work, frost_poll_interrupt);
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

out_deinit:
	/* Make sure int are no longer enabled */
	frost_disable_int(data, INT_INTERRUPT_EMU);
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
	} else if (!poll_interrupts) {
		/*
		 * We  need to disable interrupts for the
		 * embedded device via the frost interrupt controller...
		 */
		frost_disable_int(data, INT_INTERRUPT_EMU);

		/* Unregister int */
		devm_free_irq(&dev->dev, data->irq, dev);
	}

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

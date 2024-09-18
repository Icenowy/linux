/*!
 *****************************************************************************
 *
 * @File       vha_plat_dt.c
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


#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/version.h>

#include <img_mem_man.h>
#include "vha_common.h"
#include "uapi/version.h"
#include "vha_plat.h"
#include "vha_plat_dt.h"

#if defined(CFG_SYS_VAGUS)
#include <hwdefs/vagus_system.h>
#elif defined(CFG_SYS_AURA)
#include <hwdefs/aura_system.h>
#elif defined(CFG_SYS_MIRAGE)
#include <hwdefs/mirage_system.h>
#endif

#define DEVICE_NAME "vha"

/* Number of core cycles used to measure the core clock frequency */
#define FREQ_MEASURE_CYCLES 0xfffffff

static bool poll_interrupts;   /* Disabled by default */
module_param(poll_interrupts, bool, 0444);
MODULE_PARM_DESC(poll_interrupts, "Poll for interrupts? 0: No, 1: Yes");

static unsigned int irq_poll_interval_ms = 100; /* 100 ms */
module_param(irq_poll_interval_ms, uint, 0444);
MODULE_PARM_DESC(irq_poll_interval_ms, "Time in ms between each interrupt poll");

/* Global timer used when irq poll mode is switched on.
 * NOTE: only single core instance is supported in polling mode */
static struct poll_timer {
	struct platform_device *pdev;
	struct timer_list tmr;
	bool enabled;

} irq_poll_timer;

static irqreturn_t dt_plat_thread_irq(int irq, void *dev_id)
{
	struct platform_device *ofdev = (struct platform_device *)dev_id;

	return vha_handle_thread_irq(&ofdev->dev);
}

static irqreturn_t dt_plat_isrcb(int irq, void *dev_id)
{
	struct platform_device *ofdev = (struct platform_device *)dev_id;

	if (!ofdev)
		return IRQ_NONE;

	return vha_handle_irq(&ofdev->dev);
}

/* Interrupt polling function */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
static void dt_plat_poll_interrupt(struct timer_list *t)
{
	struct poll_timer *poll_timer = from_timer(poll_timer, t, tmr);
#else
static void dt_plat_poll_interrupt(unsigned long ctx)
{
	struct poll_timer *poll_timer = (struct poll_timer *)ctx;
#endif
	struct platform_device *ofdev = poll_timer->pdev;
	int ret;

	if (!poll_timer->enabled)
		return;

	preempt_disable();
	ret = vha_handle_irq(&ofdev->dev);
	preempt_enable();
	if (ret == IRQ_WAKE_THREAD)
		vha_handle_thread_irq(&ofdev->dev);

	/* retrigger */
	mod_timer(&poll_timer->tmr,
			jiffies + msecs_to_jiffies(irq_poll_interval_ms));
}

static int vha_plat_probe(struct platform_device *ofdev)
{
	int ret, module_irq;
	struct resource res;
	void __iomem *reg_addr;
	uint32_t reg_size, core_size;

	ret = of_address_to_resource(ofdev->dev.of_node, 0, &res);
	if (ret) {
		dev_err(&ofdev->dev, "missing 'reg' property in device tree\n");
		return ret;
	}
	pr_info("%s: registers %#llx-%#llx\n", __func__,
		(unsigned long long)res.start, (unsigned long long)res.end);

	module_irq = irq_of_parse_and_map(ofdev->dev.of_node, 0);
	if (module_irq == 0) {
		dev_err(&ofdev->dev, "could not map IRQ\n");
		return -ENXIO;
	}

	/* Assuming DT holds a single registers space entry that covers all regions,
	 * So we need to do the split accordingly */
	reg_size = res.end - res.start + 1;

#ifdef CFG_SYS_VAGUS
	core_size = _REG_SIZE + _REG_NNSYS_SIZE;
#else
	core_size = _REG_SIZE;
#endif
	if ((res.start + _REG_START) > res.end) {
		dev_err(&ofdev->dev, "wrong system conf for core region!\n");
		return -ENXIO;
	}

	if ((res.start + _REG_START + core_size) > res.end) {
		dev_warn(&ofdev->dev, "trimming system conf for core region!\n");
		core_size = reg_size - _REG_START;
	}


#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
	reg_addr = devm_ioremap_nocache(&ofdev->dev, res.start +
			_REG_START, core_size);
#else
	reg_addr = devm_ioremap(&ofdev->dev, res.start +
			_REG_START, core_size);
#endif
	if (!reg_addr) {
		dev_err(&ofdev->dev, "failed to map core registers\n");
		return -ENXIO;
	}

	ret = vha_plat_dt_hw_init(ofdev);
	if (ret) {
		dev_err(&ofdev->dev, "failed to init platform-specific hw!\n");
		goto out_add_dev;
	}

	/* no 'per device' memory heaps used */
	ret = vha_add_dev(&ofdev->dev, NULL, 0,
				NULL /* plat priv data */, reg_addr, core_size);
	if (ret) {
		dev_err(&ofdev->dev, "failed to intialize driver core!\n");
		goto out_add_dev;
	}

	if (!poll_interrupts) {
		ret = devm_request_threaded_irq(&ofdev->dev, module_irq, &dt_plat_isrcb,
				&dt_plat_thread_irq, IRQF_SHARED, DEVICE_NAME, ofdev);
		if (ret) {
			dev_err(&ofdev->dev, "failed to request irq\n");
			goto out_irq;
		}
	} else {
		irq_poll_timer.pdev = ofdev;
		irq_poll_timer.enabled = true;
		/* Setup and start poll timer */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
		timer_setup(&irq_poll_timer.tmr, dt_plat_poll_interrupt, 0);
#else
		setup_timer(&irq_poll_timer.tmr, dt_plat_poll_interrupt,
				(uintptr_t)&irq_poll_timer);
#endif
		mod_timer(&irq_poll_timer.tmr,
				jiffies + msecs_to_jiffies(irq_poll_interval_ms));
	}

	/* Try to calibrate the core if needed */
	ret = vha_dev_calibrate(&ofdev->dev, FREQ_MEASURE_CYCLES);
	if (ret) {
		dev_err(&ofdev->dev, "%s: Failed to start clock calibration!\n", __func__);
		goto out_irq;
	}
	return ret;

out_irq:
	vha_rm_dev(&ofdev->dev);
out_add_dev:
	devm_iounmap(&ofdev->dev, reg_addr);
	return ret;
}

static int vha_plat_remove(struct platform_device *ofdev)
{
	vha_rm_dev(&ofdev->dev);

	vha_plat_dt_hw_destroy(ofdev);

	return 0;
}

#ifdef CONFIG_PM
static int vha_plat_suspend(struct device *dev)
{
	struct platform_device *ofdev =
		container_of(dev, struct platform_device, dev);
	int ret = 0;

	ret = vha_suspend_dev(dev);
	if (ret)
		dev_err(dev, "failed to suspend the core!\n");
	else {
		ret = vha_plat_dt_hw_suspend(ofdev);
		if (ret)
			dev_err(dev, "failed to suspend platform-specific hw!\n");
	}

	return ret;
}

static int vha_plat_resume(struct device *dev)
{
	struct platform_device *ofdev =
		container_of(dev, struct platform_device, dev);
	int ret = 0;

	ret = vha_plat_dt_hw_resume(ofdev);
	if (ret)
		dev_err(dev, "failed to resume platform-specific hw!\n");
	else {
		ret = vha_resume_dev(dev);
		if (ret)
			dev_err(dev, "failed to resume the core!\n");
	}

	return ret;
}

static int vha_plat_runtime_idle(struct device *dev)
{
	/* Eg. turn off external clocks */
	return 0;
}

static int vha_plat_runtime_suspend(struct device *dev)
{
	/* Nothing to do */
	return 0;
}

static int vha_plat_runtime_resume(struct device *dev)
{
	/* Eg. turn on external clocks */
	return 0;
}

#endif

static struct dev_pm_ops vha_pm_plat_ops = {
	SET_RUNTIME_PM_OPS(vha_plat_runtime_suspend,
			vha_plat_runtime_resume, vha_plat_runtime_idle)
	SET_SYSTEM_SLEEP_PM_OPS(vha_plat_suspend, vha_plat_resume)
};

static ssize_t info_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "VHA DT driver version : " VERSION_STRING "\n");
}

static DRIVER_ATTR_RO(info);
static struct attribute *drv_attrs[] = {
	&driver_attr_info.attr,
	NULL
};

ATTRIBUTE_GROUPS(drv);

static struct platform_driver vha_plat_drv = {
	.probe  = vha_plat_probe,
	.remove = vha_plat_remove,
	.driver = {
		.name = VHA_PLAT_DT_NAME,
		.groups = drv_groups,
		.owner = THIS_MODULE,
		.of_match_table = vha_plat_dt_of_ids,
		.pm = &vha_pm_plat_ops,
	},
};

int vha_plat_init(void)
{
	int ret = 0;
	struct heap_config *heap_configs;
	int num_heaps;

	vha_plat_dt_get_heaps(&heap_configs, &num_heaps);
	ret = vha_init_plat_heaps(heap_configs, num_heaps);
	if (ret) {
		pr_err("failed to initialize global heaps\n");
		return -ENOMEM;
	}

	ret = platform_driver_register(&vha_plat_drv);
	if (ret) {
		pr_err("failed to register VHA driver!\n");
		return ret;
	}

	return 0;
}

int vha_plat_deinit(void)
{
	int ret;

	if (poll_interrupts) {
		irq_poll_timer.enabled = false;
		del_timer_sync(&irq_poll_timer.tmr);
	}

	/* Unregister the driver from the OS */
	platform_driver_unregister(&vha_plat_drv);

	ret = vha_deinit();
	if (ret)
		pr_err("VHA driver deinit failed\n");

	return ret;
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

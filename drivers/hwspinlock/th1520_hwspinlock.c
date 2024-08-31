// SPDX-License-Identifier: GPL-2.0
/*
 * Th1520 hardware spinlock driver
 *
 * Copyright (C) 2020-2025 Alibaba Group Holding Limited
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/hwspinlock.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "hwspinlock_internal.h"

struct th1520_hwspinlock {
	void __iomem *io_base;
	struct hwspinlock_device bank;
};

#define	HW_SPINLOCK_NUMBER	64
#define HW_SPINLOCK_OFFSET(x)	(0x4 * (x))

static int th1520_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	return !readl(lock_addr);
}

static void th1520_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *lock_addr = lock->priv;

	writel(0, lock_addr);
}

static const struct hwspinlock_ops th1520_hwspinlock_ops = {
	.trylock = th1520_hwspinlock_trylock,
	.unlock = th1520_hwspinlock_unlock,
};

static int th1520_hwspinlock_probe(struct platform_device *pdev)
{
	struct th1520_hwspinlock *hwspin;
	struct hwspinlock *hwlock;
	int idx, ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	hwspin = devm_kzalloc(&pdev->dev,
			      struct_size(hwspin, bank.lock,
					  HW_SPINLOCK_NUMBER),
			      GFP_KERNEL);
	if (!hwspin)
		return -ENOMEM;

	/* retrieve io base */
	hwspin->io_base = of_iomap(pdev->dev.of_node, 0);
	if (!hwspin->io_base)
		return -ENOMEM;
	pr_info("io_base: 0x%lx\n", (long)hwspin->io_base);

	for (idx = 0; idx < HW_SPINLOCK_NUMBER; idx++) {
		hwlock = &hwspin->bank.lock[idx];
		hwlock->priv = hwspin->io_base + HW_SPINLOCK_OFFSET(idx);
	}

	platform_set_drvdata(pdev, hwspin);

	pm_runtime_enable(&pdev->dev);

	ret = hwspin_lock_register(&hwspin->bank, &pdev->dev,
				   &th1520_hwspinlock_ops, 0,
				   HW_SPINLOCK_NUMBER);
	if (ret)
		goto reg_failed;

	return 0;

reg_failed:
	pm_runtime_disable(&pdev->dev);
	iounmap(hwspin->io_base);

	return ret;
}

static int th1520_hwspinlock_remove(struct platform_device *pdev)
{
	struct th1520_hwspinlock *hwspin = platform_get_drvdata(pdev);
	int ret;

	ret = hwspin_lock_unregister(&hwspin->bank);
	if (ret) {
		dev_err(&pdev->dev, "%s failed: %d\n", __func__, ret);
		return ret;
	}

	pm_runtime_disable(&pdev->dev);

	iounmap(hwspin->io_base);

	return 0;
}

static const struct of_device_id th1520_hwpinlock_ids[] = {
	{ .compatible = "th1520,hwspinlock", },
	{},
};
MODULE_DEVICE_TABLE(of, th1520_hwpinlock_ids);

static struct platform_driver th1520_hwspinlock_driver = {
	.probe = th1520_hwspinlock_probe,
	.remove = th1520_hwspinlock_remove,
	.driver = {
		.name = "th1520_hwspinlock",
		.of_match_table = of_match_ptr(th1520_hwpinlock_ids),
	},
};

module_platform_driver(th1520_hwspinlock_driver);

MODULE_LICENSE("GPL v2");

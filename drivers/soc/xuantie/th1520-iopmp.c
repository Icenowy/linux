/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021-2024 Alibaba Group Holding Limited.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <dt-bindings/soc/xuantie,th1520-iopmp.h>

#define TH1520_PMP_MISC_CTRL		0x04
#define TH1520_PMP_DUMMY_ADDR		0x08
#define TH1520_PMP_PAGE_LOCK0		0x40
#define TH1520_PMP_ATTR_CFG0		0x80
#define TH1520_DEFAULT_PMP_ATTR_CFG	0xc0
#define TH1520_PMP_REGION0_SADDR		0x280
#define TH1520_PMP_REGION0_EADDR		0x284

#define TH1520_PMP_PAGE_LOCK0_REGIONS		0xFFFF
#define TH1520_PMP_PAGE_LOCK0_DEFAULT_CFG	BIT(16)
#define TH1520_PMP_PAGE_LOCK0_BYPASS_EN		BIT(17)
#define TH1520_PMP_PAGE_LOCK0_DUMMY_ADDR		BIT(18)

#define TH1520_IOPMP_REGION_NUM		16
#define TH1520_IOPMP_ATTR_CFG_DEFAULT	0xFFFFFFFF
#define TH1520_IOPMP_DUMMY_ADDR		0x800000
#define TH1520_IOPMP_CTRL_BYPASS		0xFF000000

struct th1520_iopmp_entry {
        u32 reg_start[TH1520_IOPMP_REGION_NUM];
        u32 reg_end[TH1520_IOPMP_REGION_NUM];
        u32 entry_valid_num;
};

struct th1520_iopmp_cur_sys_entry {
	int iopmp_type;
	u32 start_addr;
	u32 end_addr;
	u32 attr;
	int lock;
};

struct th1520_iopmp_info {
	struct device *dev;
	int entries;
	struct th1520_iopmp_list *iopmp_list;
	struct th1520_iopmp_cur_sys_entry cur_entry;
	struct th1520_iopmp_entry iopmp_entry[];
};

static struct th1520_iopmp_list {
	int iopmp_type;
	resource_size_t offset;
	void __iomem *base;
	bool bypass_en;
	bool is_default_region;
	u32 dummy_slave;
	u32 attr;
	int lock;
	struct th1520_iopmp_entry iopmp_entry;
} th1520_iopmp_lists[] = {
	{IOPMP_EMMC, 0xFFFC028000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_SDIO0, 0xFFFC029000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_SDIO1, 0xFFFC02A000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_USB0, 0xFFFC02E000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_AO, 0xFFFFC21000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_AUD, 0xFFFFC22000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_CHIP_DBG, 0xFFFFC37000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_EIP120I, 0xFFFF220000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_EIP120II, 0xFFFF230000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_EIP120III, 0xFFFF240000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_ISP0, 0xFFF4080000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_ISP1, 0xFFF4081000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_DW200, 0xFFF4082000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_VIPRE, 0xFFF4083000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_VENC, 0xFFFCC60000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_VDEC, 0xFFFCC61000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_G2D, 0xFFFCC62000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_FCE, 0xFFFCC63000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_NPU, 0xFFFF01C000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP0_DPU, 0xFFFF520000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP1_DPU, 0xFFFF521000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_GPU, 0xFFFF522000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_GMAC1, 0xFFFC001000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_GMAC2, 0xFFFC002000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_DMAC, 0xFFFFC20000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_TEE_DMAC, 0xFFFF250000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_DSP0, 0xFFFF058000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
	{IOPMP_DSP1, 0xFFFF059000, NULL, false, false, TH1520_IOPMP_DUMMY_ADDR, 0xFFFFFFFF, 0x7FFFF, {{0,},{0,},0} },
};

static const struct th1520_iopmp_driver_data {
	struct th1520_iopmp_list *iopmp_list;
} th1520_iopmp_data = {
	.iopmp_list = th1520_iopmp_lists,
};

static int th1520_iopmp_get_entries(struct th1520_iopmp_list *match)
{
	if (match == th1520_iopmp_lists)
		return ARRAY_SIZE(th1520_iopmp_lists);

	return 0;
}

static int th1520_iopmp_get_type(struct th1520_iopmp_info *info, struct device_node *entry_np)
{
	int entry_size = info->entries;
	struct th1520_iopmp_list *match = info->iopmp_list;
	unsigned long type;

	if (kstrtoul(entry_np->name, 0, &type))
		return -EINVAL;

	if (type < match[0].iopmp_type || type > match[entry_size - 1].iopmp_type)
		return -EINVAL;

	return type;
}

static bool th1520_iopmp_type_valid(struct th1520_iopmp_info *info, int tap)
{
	int entry_size = info->entries;
	struct th1520_iopmp_list *match = info->iopmp_list;

	if (tap < match[0].iopmp_type || tap > match[entry_size - 1].iopmp_type)
		return false;

	return true;
}

static void th1520_iopmp_set_attr_bypass(struct th1520_iopmp_info *info, int type)
{
	void __iomem *regs;
	u32 val;

	if (!info->iopmp_list[type].base) {
		regs = devm_ioremap(info->dev, info->iopmp_list[type].offset, PAGE_SIZE);
		if (!regs) {
			dev_err(info->dev, "ioremap failed\n");
			return;
		}
		info->iopmp_list[type].base = regs;
	}

	val = readl(info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	if (val & TH1520_PMP_PAGE_LOCK0_BYPASS_EN) {
		dev_info(info->dev, "iopmp bypass_en is already locked, ignore the tap[%d] setting\n", type);
		return;
	}

	val = readl(info->iopmp_list[type].base + TH1520_PMP_MISC_CTRL);
	val |= TH1520_IOPMP_CTRL_BYPASS;
	writel(val, info->iopmp_list[type].base + TH1520_PMP_MISC_CTRL);

	/* lock the iopmp */
	val = readl(info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	val |= TH1520_PMP_PAGE_LOCK0_BYPASS_EN;
	writel(val, info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
}

static void th1520_iopmp_set_attr_default(struct th1520_iopmp_info *info, int type, int attr)
{
	void __iomem *regs;
	u32 val;

	if (!info->iopmp_list[type].base) {
		regs = devm_ioremap(info->dev, info->iopmp_list[type].offset, PAGE_SIZE);
		if (!regs) {
			dev_err(info->dev, "ioremap failed\n");
			return;
		}
		info->iopmp_list[type].base = regs;
	}

	val = readl(info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	if (val & TH1520_PMP_PAGE_LOCK0_DEFAULT_CFG) {
		dev_info(info->dev, "iopmp default cfg is already locked, ignore the tap[%d] setting\n", type);
		return;
	}

	val = readl(info->iopmp_list[type].base + TH1520_PMP_MISC_CTRL);
	if (val & TH1520_IOPMP_CTRL_BYPASS) {
		dev_info(info->dev, "iopmp bypass is already enabled, ignore the tap[%d] setting\n", type);
		return;
	}

	writel(attr, info->iopmp_list[type].base + TH1520_DEFAULT_PMP_ATTR_CFG);

	/* lock the iopmp */
	val = readl(info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	val |= TH1520_PMP_PAGE_LOCK0_DEFAULT_CFG;
	writel(val, info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
}

static void th1520_iopmp_set_attr(struct th1520_iopmp_info *info, int type, int attr,
				 int dummy_slave, struct th1520_iopmp_entry *entry)
{
	void __iomem *regs;
	bool dummy_slave_lock = false;
	u32 val;
	int i;

	if (!info->iopmp_list[type].base) {
		regs = devm_ioremap(info->dev, info->iopmp_list[type].offset, PAGE_SIZE);
		if (!regs) {
			dev_err(info->dev, "ioremap failed\n");
			return;
		}
		info->iopmp_list[type].base = regs;
	}

	val = readl(info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	if (val & TH1520_PMP_PAGE_LOCK0_REGIONS) {
		dev_info(info->dev, "iopmp regions cfg is already locked, ignore the tap[%d] setting\n", type);
		return;
	}

	if (val & TH1520_PMP_PAGE_LOCK0_DUMMY_ADDR)
		dummy_slave_lock = true;

	val = readl(info->iopmp_list[type].base + TH1520_PMP_MISC_CTRL);
	if (val & TH1520_IOPMP_CTRL_BYPASS) {
		dev_info(info->dev, "iopmp bypass is already enabled, ignore the tap[%d] setting\n", type);
		return;
	}

	for (i = 0; i < entry->entry_valid_num; i++) {
		writel(entry->reg_start[i],
		       info->iopmp_list[type].base + TH1520_PMP_REGION0_SADDR + (i << 3));
		writel(entry->reg_end[i],
		       info->iopmp_list[type].base + TH1520_PMP_REGION0_EADDR + (i << 3));
		writel(attr, info->iopmp_list[type].base + TH1520_PMP_ATTR_CFG0 + (i << 2));
	}

	if (!dummy_slave_lock)
		writel(dummy_slave, info->iopmp_list[type].base + TH1520_PMP_DUMMY_ADDR);

	/* lock the iopmp */
	val = readl(info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	val |= TH1520_PMP_PAGE_LOCK0_REGIONS | TH1520_PMP_PAGE_LOCK0_DUMMY_ADDR;
	writel(val, info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
}

static int th1520_iopmp_sys_set(struct th1520_iopmp_info *info)
{
	void __iomem *regs;
	int type = info->cur_entry.iopmp_type;
	bool dummy_slave_lock = false;
	u32 val;

	if (!info->iopmp_list[type].base) {
		regs = devm_ioremap(info->dev, info->iopmp_list[type].offset, PAGE_SIZE);
		if (!regs) {
			dev_err(info->dev, "ioremap failed\n");
			return -ENOMEM;
		}
		info->iopmp_list[type].base = regs;
	}

	val = readl(info->iopmp_list[type].base + TH1520_PMP_MISC_CTRL);
	if (val & TH1520_IOPMP_CTRL_BYPASS) {
		dev_err(info->dev, "iopmp bypass is already enabled, ignore the tap[%d] setting\n", type);
		return -EINVAL;
	}

	val = readl(info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	if (val & TH1520_PMP_PAGE_LOCK0_REGIONS) {
		dev_err(info->dev, "iopmp regions cfg is already locked, ignore the tap[%d] setting\n", type);
		return -EINVAL;
	}

	if (val & TH1520_PMP_PAGE_LOCK0_DUMMY_ADDR)
		dummy_slave_lock = true;

	val = readl(info->iopmp_list[type].base + TH1520_PMP_MISC_CTRL);
	if (val & TH1520_IOPMP_CTRL_BYPASS) {
		dev_err(info->dev, "iopmp bypass is already enabled, ignore the tap[%d] setting\n", type);
		return -EINVAL;
	}

	/* set one region from userspace's config */
	writel(info->cur_entry.start_addr,
	       info->iopmp_list[type].base + TH1520_PMP_REGION0_SADDR);
	writel(info->cur_entry.end_addr,
	       info->iopmp_list[type].base + TH1520_PMP_REGION0_EADDR);
	writel(info->cur_entry.attr, info->iopmp_list[type].base + TH1520_PMP_ATTR_CFG0);

	if (!dummy_slave_lock)
		writel(TH1520_IOPMP_DUMMY_ADDR, info->iopmp_list[type].base + TH1520_PMP_DUMMY_ADDR);

	/* lock the iopmp */
	if (info->cur_entry.lock) {
		val = TH1520_PMP_PAGE_LOCK0_REGIONS | TH1520_PMP_PAGE_LOCK0_DEFAULT_CFG |
		      TH1520_PMP_PAGE_LOCK0_DUMMY_ADDR | TH1520_PMP_PAGE_LOCK0_BYPASS_EN;
		writel(val, info->iopmp_list[type].base + TH1520_PMP_PAGE_LOCK0);
	}

	return 0;
}

static ssize_t th1520_iopmp_tap_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	ssize_t ret;

	ret = sprintf(buf, "%d\n", info->cur_entry.iopmp_type);
	return ret;
}

static ssize_t th1520_iopmp_start_addr_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	ssize_t ret;

	ret = sprintf(buf, "0x%x\n", info->cur_entry.start_addr);
	return ret;
}

static ssize_t th1520_iopmp_end_addr_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	ssize_t ret;

	ret = sprintf(buf, "0x%x\n", info->cur_entry.end_addr);
	return ret;
}

static ssize_t th1520_iopmp_attr_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	ssize_t ret;

	ret = sprintf(buf, "0x%x\n", info->cur_entry.attr);
	return ret;
}

static ssize_t th1520_iopmp_lock_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	ssize_t ret;

	ret = sprintf(buf, "%s\n", info->cur_entry.lock ? "locked" : "no lock");
	return ret;
}

static ssize_t th1520_iopmp_tap_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (th1520_iopmp_type_valid(info, (int)val))
		info->cur_entry.iopmp_type = val;
	else
		dev_err(dev, "invalid tap value\n");

	return count;
}

static ssize_t th1520_iopmp_start_addr_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	info->cur_entry.start_addr = (u32)val;

	return count;
}

static ssize_t th1520_iopmp_end_addr_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;
	if (!val)
		return -EINVAL;

	info->cur_entry.end_addr = (u32)val;

	return count;
}

static ssize_t th1520_iopmp_attr_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	info->cur_entry.attr = (u32)val;

	return count;
}

static ssize_t th1520_iopmp_lock_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	unsigned long val;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	info->cur_entry.lock = (int)val;

	return count;
}

static ssize_t th1520_iopmp_set_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 0, &val))
		return -EINVAL;
	if (!val)
		return count;

	if (!th1520_iopmp_type_valid(info, info->cur_entry.iopmp_type)) {
		dev_err(dev, "invalid iopmp tap ID\n");
		return -EINVAL;
	}

	if (info->cur_entry.end_addr <= info->cur_entry.start_addr) {
		dev_err(dev, "invalid end_addr/start_addr setting\n");
		return -EINVAL;
	}

	ret = th1520_iopmp_sys_set(info);
	if (ret)
		return -EINVAL;

	return count;
}

static int th1520_iopmp_config(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct th1520_iopmp_info *info = platform_get_drvdata(pdev);
	struct th1520_iopmp_list *list = info->iopmp_list;
	int i;

	for (i = 0; i < info->entries; i++) {
		if (list[i].iopmp_entry.entry_valid_num > 0) {
			/* config the iopmp entry */
			th1520_iopmp_set_attr(info, i,
				list[i].attr,
				list[i].dummy_slave,
				&list[i].iopmp_entry);

		} else {
			/* config the iopmp entry */
			if (list[i].bypass_en)
				th1520_iopmp_set_attr_bypass(info, i);
			else if (list[i].is_default_region)
				th1520_iopmp_set_attr_default(info, i, list[i].attr);
		}
	}

	return 0;
}


static DEVICE_ATTR(th1520_iopmp_tap, 0644, th1520_iopmp_tap_show, th1520_iopmp_tap_store);
static DEVICE_ATTR(th1520_iopmp_start_addr, 0644, th1520_iopmp_start_addr_show, th1520_iopmp_start_addr_store);
static DEVICE_ATTR(th1520_iopmp_end_addr, 0644, th1520_iopmp_end_addr_show, th1520_iopmp_end_addr_store);
static DEVICE_ATTR(th1520_iopmp_attr, 0644, th1520_iopmp_attr_show, th1520_iopmp_attr_store);
static DEVICE_ATTR(th1520_iopmp_lock, 0644, th1520_iopmp_lock_show, th1520_iopmp_lock_store);
static DEVICE_ATTR(th1520_iopmp_set, 0644, NULL, th1520_iopmp_set_store);

static struct attribute *th1520_iopmp_sysfs_entries[] = {
	&dev_attr_th1520_iopmp_tap.attr,
	&dev_attr_th1520_iopmp_start_addr.attr,
	&dev_attr_th1520_iopmp_end_addr.attr,
	&dev_attr_th1520_iopmp_attr.attr,
	&dev_attr_th1520_iopmp_lock.attr,
	&dev_attr_th1520_iopmp_set.attr,
	NULL
};

static const struct attribute_group dev_attr_th1520_iopmp_group = {
	.attrs = th1520_iopmp_sysfs_entries,
};

static int th1520_iopmp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct th1520_iopmp_driver_data *drvdata = NULL;
	struct th1520_iopmp_list *list;
	struct th1520_iopmp_info *info;
	struct device_node *entry_np;
	int size, entries;
	int ret;
	int j;

	drvdata = device_get_match_data(dev);
	if (!drvdata) {
		dev_err(dev, "cannot get driver data\n");
		return -ENOENT;
	}
	list = drvdata->iopmp_list;
	entries = th1520_iopmp_get_entries(list);
	if (entries <= 0)
		return -ENOENT;

	size = sizeof(struct th1520_iopmp_entry) * entries + sizeof(*info);
	info = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->entries = entries;
	info->iopmp_list = list;
	info->dev = dev;

	for_each_child_of_node(dev->of_node, entry_np) {
		int attr = TH1520_IOPMP_ATTR_CFG_DEFAULT;
		int dummy_slave = TH1520_IOPMP_DUMMY_ADDR;
		bool is_default_region = false;
		bool bypass_en = false;
		int region_size;
		int type;

		if (!of_device_is_available(entry_np))
			continue;

		type = th1520_iopmp_get_type(info, entry_np);
		if (type < 0) {
			dev_err(dev, "invalid iopmp tap:%d\n", type);
			continue;
		}

		ret = of_property_read_u32(entry_np, "attr", &attr);
		if (!ret && (attr == 0))
			attr = TH1520_IOPMP_ATTR_CFG_DEFAULT;

		ret = of_property_read_u32(entry_np, "dummy_slave", &dummy_slave);
		if (!ret && (dummy_slave == 0))
			dummy_slave = TH1520_IOPMP_DUMMY_ADDR;

		region_size = of_property_count_elems_of_size(entry_np, "regions", sizeof(u32));
		if (region_size <= 0) {
			is_default_region = of_property_read_bool(entry_np, "is_default_region");
			bypass_en = of_property_read_bool(entry_np, "bypass_en");

			if (!is_default_region && !bypass_en) {
				dev_err(dev, "missing/invalid reg property\n");
				continue;
			}
		} else {
			region_size >>= 1;
			if (region_size > TH1520_IOPMP_REGION_NUM) {
				dev_err(dev, "region size is invalid\n");
				continue;
			}

			for (j = 0; j < region_size; j++) {
				ret = of_property_read_u32_index(entry_np, "regions",
						j << 1, &list[type].iopmp_entry.reg_start[j]);
				if (ret)
					break;
				ret = of_property_read_u32_index(entry_np, "regions",
						(j << 1) + 1, &list[type].iopmp_entry.reg_end[j]);
				if (ret)
					break;

				/* check the region valid, otherwise drop the iopmp setting */
				if (j && list[type].iopmp_entry.reg_end[j - 1] > list[type].iopmp_entry.reg_start[j])
					break;
			}

			if (j < region_size)
				continue;

			list[type].iopmp_entry.entry_valid_num = region_size;
		}

		// store valid config
		list[type].attr = attr;
		list[type].dummy_slave = dummy_slave;
		list[type].is_default_region = is_default_region;
		list[type].bypass_en = bypass_en;
	}

	/* init the cur entry config */
	info->cur_entry.iopmp_type = -1;
	info->cur_entry.start_addr = 0;
	info->cur_entry.end_addr = 0;
	info->cur_entry.attr = 0;
	info->cur_entry.lock = false;

	platform_set_drvdata(pdev, info);

	th1520_iopmp_config(&pdev->dev);

	ret = sysfs_create_group(&pdev->dev.kobj, &dev_attr_th1520_iopmp_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create th1520 iopmp debug sysfs.\n");
		return ret;
	}

	return 0;
}

static int th1520_iopmp_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static int __maybe_unused th1520_iopmp_noirq_suspend(struct device *dev)
{
	// nothing to do
	return 0;
}

static int __maybe_unused th1520_iopmp_noirq_resume(struct device *dev)
{
	/* reinit iopmp register setting
	   ensure system clock enabled before here
	*/
	th1520_iopmp_config(dev);

	return 0;
}

static const struct dev_pm_ops th1520_iopmp_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(th1520_iopmp_noirq_suspend, th1520_iopmp_noirq_resume)
};

static const struct of_device_id th1520_iopmp_of_match[] = {
	{ .compatible = "xuantie,th1520-iopmp", .data = &th1520_iopmp_data},
	{},
};

static struct platform_driver th1520_iopmp_driver = {
	.driver = {
		.name           = "th1520-iopmp",
		.of_match_table = of_match_ptr(th1520_iopmp_of_match),
		.pm     	= &th1520_iopmp_pm_ops,
	},
	.probe = th1520_iopmp_probe,
	.remove = th1520_iopmp_remove,
};

static int __init th1520_iopmp_init(void)
{
        return platform_driver_register(&th1520_iopmp_driver);
}
arch_initcall(th1520_iopmp_init);

MODULE_ALIAS("platform:th1520-iopmp");
MODULE_AUTHOR("fugang.duan <duanfugang.dfg@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead Light iopmp driver");
MODULE_LICENSE("GPL v2");

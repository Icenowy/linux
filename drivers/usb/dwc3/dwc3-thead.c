// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-thead.c - T-HEAD platform specific glue layer
 *
 * Inspired by dwc3-of-simple.c
 *
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (C) 2023 Jisheng Zhang <jszhang@kernel.org>
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "core.h"

#define USB_SSP_EN		0x34
#define  REF_SSP_EN		BIT(0)
#define USB_SYS			0x3c
#define  COMMONONN		BIT(0)

#define USB3_DRD_SWRST		0x14
#define  USB3_DRD_PRST		BIT(0)
#define  USB3_DRD_PHYRST	BIT(1)
#define  USB3_DRD_VCCRST	BIT(2)
#define  USB3_DRD_RSTMASK	(USB3_DRD_PRST | USB3_DRD_PHYRST | USB3_DRD_VCCRST)

struct dwc3_thead {
	void __iomem		*base;
	struct regmap		*misc_sysreg;
	struct regulator	*vbus;
};

static void dwc3_thead_optimize_power(struct dwc3_thead *thead)
{
	u32 val;

	/* config usb top within USB ctrl & PHY reset */
	regmap_update_bits(thead->misc_sysreg, USB3_DRD_SWRST,
			   USB3_DRD_RSTMASK, USB3_DRD_PRST);

	/*
	 * dwc reg also need to be configed to save power
	 * 1. set USB_SYS[COMMONONN]
	 * 2. set DWC3_GCTL[SOFITPSYNC](done by core.c)
	 * 3. set GUSB3PIPECTL[SUSPENDEN] (done by core.c)
	 */
	val = readl(thead->base + USB_SYS);
	val |= COMMONONN;
	writel(val, thead->base + USB_SYS);
	val = readl(thead->base + USB_SSP_EN);
	val |= REF_SSP_EN;
	writel(val, thead->base + USB_SSP_EN);

	regmap_update_bits(thead->misc_sysreg, USB3_DRD_SWRST,
			   USB3_DRD_RSTMASK, USB3_DRD_RSTMASK);
}

static int dwc3_thead_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct dwc3_thead *thead;
	int ret;

	thead = devm_kzalloc(&pdev->dev, sizeof(*thead), GFP_KERNEL);
	if (!thead)
		return -ENOMEM;

	platform_set_drvdata(pdev, thead);

	ret = devm_regulator_get_enable_optional(dev, "vbus");
	if (ret < 0 && ret != -ENODEV)
		return ret;

	thead->misc_sysreg = syscon_regmap_lookup_by_phandle(np, "thead,misc-sysreg");
	if (IS_ERR(thead->misc_sysreg))
		return PTR_ERR(thead->misc_sysreg);

	thead->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(thead->base))
		return PTR_ERR(thead->base);

	dwc3_thead_optimize_power(thead);

	return devm_of_platform_populate(dev);
}

static const struct of_device_id dwc3_thead_of_match[] = {
	{ .compatible = "thead,th1520-usb" },
	{ },
};
MODULE_DEVICE_TABLE(of, dwc3_thead_of_match);

static struct platform_driver dwc3_thead_driver = {
	.probe		= dwc3_thead_probe,
	.driver		= {
		.name	= "dwc3-thead",
		.of_match_table	= dwc3_thead_of_match,
	},
};
module_platform_driver(dwc3_thead_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DWC3 T-HEAD Glue Driver");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");

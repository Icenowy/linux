// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-xuantie.c - XuanTie platform specific glue layer
 *
 * Inspired by dwc3-of-simple.c
 *
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (C) 2023 Jisheng Zhang <jszhang@kernel.org>
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

#include "core.h"

/* USB3_DRD registers */
#define USB_CLK_GATE_STS		0x0
#define USB_LOGIC_ANALYZER_TRACE_STS0	0x4
#define USB_LOGIC_ANALYZER_TRACE_STS1	0x8
#define USB_GPIO				0xc
#define USB_DEBUG_STS0			0x10
#define USB_DEBUG_STS1			0x14
#define USB_DEBUG_STS2			0x18
#define USBCTL_CLK_CTRL0		0x1c
#define USBPHY_CLK_CTRL1		0x20
#define USBPHY_TEST_CTRL0		0x24
#define USBPHY_TEST_CTRL1		0x28
#define USBPHY_TEST_CTRL2		0x2c
#define USBPHY_TEST_CTRL3		0x30
#define USB_SSP_EN				0x34
#define USB_HADDR_SEL			0x38
#define USB_SYS					0x3c
#define USB_HOST_STATUS			0x40
#define USB_HOST_CTRL			0x44
#define USBPHY_HOST_CTRL		0x48
#define USBPHY_HOST_STATUS		0x4c
#define USB_TEST_REG0			0x50
#define USB_TEST_REG1			0x54
#define USB_TEST_REG2			0x58
#define USB_TEST_REG3			0x5c

/* Bit fields */
/* USB_SYS */
#define TEST_POWERDOWN_SSP	BIT(2)
#define TEST_POWERDOWN_HSP	BIT(1)
#define COMMONONN			BIT(0)

/* USB_SSP_EN */
#define REF_SSP_EN			BIT(0)

/* USBPHY_HOST_CTRL */
#define HOST_U2_PORT_DISABLE	BIT(6)
#define HOST_U3_PORT_DISABLE	BIT(5)

/* MISC_SYSREG registers */
#define USB3_DRD_SWRST			0x14

/* Bit fields */
/* USB3_DRD_SWRST */
#define USB3_DRD_VCCRST		BIT(2)
#define USB3_DRD_PHYRST		BIT(1)
#define USB3_DRD_PRST		BIT(0)
#define USB3_DRD_MASK		GENMASK(2, 0)

/* USB as host or device*/
#define USB_AS_HOST         (true)
#define USB_AS_DEVICE       (false)

static bool usb_role = USB_AS_HOST;
module_param(usb_role, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(usb_role, "USB role");

struct dwc3_xuantie {
	struct device			*dev;
	struct clk_bulk_data	*clks;
	int						num_clocks;

	void __iomem			*usb3_drd_base;
	struct regmap			*misc_sysreg;

	struct gpio_desc		*hubswitch;
	struct regulator		*hub1v2;
	struct regulator		*hub5v;
	struct regulator		*vbus;
};

static void dwc3_xuantie_deassert(struct dwc3_xuantie *xuantie)
{
	/* 1. reset assert */
	regmap_update_bits(xuantie->misc_sysreg, USB3_DRD_SWRST,
				USB3_DRD_MASK, USB3_DRD_PRST);

	/*
	 *	2. Common Block Power-Down Control.
	 *	Controls the power-down signals in the PLL block
	 *	when the USB 3.0 femtoPHY is in Suspend or Sleep mode.
	 */
	writel(COMMONONN, xuantie->usb3_drd_base + USB_SYS);

	/*
	 *	3. Reference Clock Enable for SS function.
	 *	Enables the reference clock to the prescaler.
	 *	The ref_ssp_en signal must remain de-asserted until
	 *	the reference clock is running at the appropriate frequency,
	 *	at which point ref_ssp_en can be asserted.
	 *	For lower power states, ref_ssp_en can also be de-asserted.
	 */
	writel(REF_SSP_EN, xuantie->usb3_drd_base + USB_SSP_EN);

	/* 4. set host ctrl */
	writel(0x1101, xuantie->usb3_drd_base + USB_HOST_CTRL);

	/* 5. reset deassert */
	regmap_update_bits(xuantie->misc_sysreg, USB3_DRD_SWRST,
				USB3_DRD_MASK, USB3_DRD_MASK);

	/* 6. wait deassert complete */
	udelay(10);
}

static void dwc3_xuantie_assert(struct dwc3_xuantie *xuantie)
{
	/* close ssp */
	writel(0, xuantie->usb3_drd_base + USB_SSP_EN);

	/* reset assert usb */
	regmap_update_bits(xuantie->misc_sysreg, USB3_DRD_SWRST,
				USB3_DRD_MASK, 0);

}

static int dwc3_xuantie_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*np  = dev->of_node;
	struct dwc3_xuantie	*xuantie;
	int					ret;

	xuantie = devm_kzalloc(&pdev->dev, sizeof(*xuantie), GFP_KERNEL);
	if (!xuantie)
		return -ENOMEM;

	platform_set_drvdata(pdev, xuantie);
	xuantie->dev = &pdev->dev;

	xuantie->misc_sysreg = syscon_regmap_lookup_by_phandle(np, "usb3-misc-regmap");
	if (IS_ERR(xuantie->misc_sysreg)) {
		dev_err(dev, "failed to get regmap - %ld\n", PTR_ERR(xuantie->misc_sysreg));


		return PTR_ERR(xuantie->misc_sysreg);
	}

	xuantie->usb3_drd_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xuantie->usb3_drd_base)) {
		dev_err(dev, "failed to get resource - %ld\n", PTR_ERR(xuantie->usb3_drd_base));
		return PTR_ERR(xuantie->usb3_drd_base);
	}

	ret = clk_bulk_get_all(xuantie->dev, &xuantie->clks);
	if (ret < 0) {
		dev_err(dev, "failed to get clk - %d\n", ret);
		goto err;
	}

	xuantie->num_clocks = ret;

	ret = clk_bulk_prepare_enable(xuantie->num_clocks, xuantie->clks);
	if (ret)
		goto err;

	dwc3_xuantie_deassert(xuantie);

	ret = of_platform_populate(np, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to register dwc3 core - %d\n", ret);
		goto err_clk_put;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_sync(dev);

	device_enable_async_suspend(dev);

	dev_info(dev,"th1520 dwc3 probe ok!\n");

	return 0;

err_clk_put:
	clk_bulk_disable_unprepare(xuantie->num_clocks, xuantie->clks);
	clk_bulk_put_all(xuantie->num_clocks, xuantie->clks);
err:
	return ret;
}

static int dwc3_xuantie_remove(struct platform_device *pdev)
{
	struct dwc3_xuantie	*xuantie = platform_get_drvdata(pdev);

	dwc3_xuantie_assert(xuantie);

	of_platform_depopulate(xuantie->dev);

	clk_bulk_disable_unprepare(xuantie->num_clocks, xuantie->clks);
	clk_bulk_put_all(xuantie->num_clocks, xuantie->clks);

	pm_runtime_disable(xuantie->dev);
	pm_runtime_set_suspended(xuantie->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int dwc3_xuantie_pm_suspend(struct device *dev)
{
	struct dwc3_xuantie *xuantie = dev_get_drvdata(dev);

	dwc3_xuantie_assert(xuantie);

	clk_bulk_disable(xuantie->num_clocks, xuantie->clks);

	return 0;
}


static int dwc3_xuantie_pm_resume(struct device *dev)
{
	struct dwc3_xuantie *xuantie = dev_get_drvdata(dev);

	dwc3_xuantie_deassert(xuantie);

	return 0;
}

static const struct dev_pm_ops dwc3_xuantie_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_xuantie_pm_suspend, dwc3_xuantie_pm_resume)
};
#define DEV_PM_OPS	(&dwc3_xuantie_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id dwc3_xuantie_of_match[] = {
	{ .compatible = "xuantie,th1520-usb" },
	{ },
};
MODULE_DEVICE_TABLE(of, dwc3_xuantie_of_match);

static struct platform_driver dwc3_xuantie_driver = {
	.probe		= dwc3_xuantie_probe,
	.remove		= dwc3_xuantie_remove,
	.driver		= {
		.name	= "dwc3-xuantie",
		.pm	= DEV_PM_OPS,
		.of_match_table	= dwc3_xuantie_of_match,
	},
};

module_platform_driver(dwc3_xuantie_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DWC3 XuanTie Glue Driver");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");

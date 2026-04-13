// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Institute of Software, Chinese Academy of Sciences (ISCAS)
 *
 * Authors:
 * Icenowy Zheng <zhengxingda@iscas.ac.cn>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#define USB_SYSCON_OFFSET		0xf000

/* All the below registers are in the USB syscon region */
#define USB_SSP_EN			0x34
#define USB_SSP_EN_REF_SSP_EN		BIT(0)

#define USB_SYS				0x3c
#define USB_SYS_COMMONONN		BIT(0)

/* The last register according to the manual */
#define USB_TEST_REG3			0x5c

struct th1520_usb_phy {
	struct platform_device *pdev;
	struct phy *phy;
	struct regmap *regmap;
	struct clk *ref_clk;
	struct reset_control *phy_reset;
};

static int th1520_usb_phy_init(struct phy *phy)
{
	struct th1520_usb_phy *th1520_phy = phy_get_drvdata(phy);
	int ret;

	ret = clk_prepare_enable(th1520_phy->ref_clk);
	if (ret)
		return ret;

	ret = reset_control_assert(th1520_phy->phy_reset);
	if (ret)
		goto err_disable_clk;

	/*
	 * Do some initial PHY setup:
	 * - Set COMMONONN to allow the PHY to automatically power down.
	 * - Set REF_SSP_EN to enable feeding reference clock to SuperSpeed
	 *   PHY clock PLL.
	 */
	regmap_set_bits(th1520_phy->regmap, USB_SYS, USB_SYS_COMMONONN);
	regmap_set_bits(th1520_phy->regmap, USB_SSP_EN, USB_SSP_EN_REF_SSP_EN);

	ret = reset_control_deassert(th1520_phy->phy_reset);
	if (ret)
		goto err_disable_clk;

	fsleep(10);

	return 0;

err_disable_clk:
	clk_disable_unprepare(th1520_phy->ref_clk);
	return ret;
}

static int th1520_usb_phy_exit(struct phy *phy)
{
	struct th1520_usb_phy *th1520_phy = phy_get_drvdata(phy);
	int ret;

	ret = reset_control_assert(th1520_phy->phy_reset);
	clk_disable_unprepare(th1520_phy->ref_clk);

	return ret;
}

static const struct phy_ops th1520_usb_phy_ops = {
	.init		= th1520_usb_phy_init,
	.exit		= th1520_usb_phy_exit,
	.owner		= THIS_MODULE,
};

static const struct regmap_config phy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = USB_TEST_REG3,
};

static int th1520_usb_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct th1520_usb_phy *th1520_phy;
	struct reset_control *bus_reset;
	struct clk *bus_clk;
	void __iomem *base;
	int ret;

	th1520_phy = devm_kzalloc(dev, sizeof(*th1520_phy), GFP_KERNEL);
	if (!th1520_phy)
		return -ENOMEM;

	th1520_phy->pdev = pdev;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	th1520_phy->ref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(th1520_phy->ref_clk))
		return PTR_ERR(th1520_phy->ref_clk);

	/* Enable the bus clock and leave it that way */
	bus_clk = devm_clk_get_enabled(dev, "bus");
	if (IS_ERR(bus_clk))
		return PTR_ERR(bus_clk);

	/* De-assert the bus reset and leave it that way */
	bus_reset = devm_reset_control_get_exclusive_deasserted(dev, "bus");
	if (IS_ERR(bus_reset))
		return PTR_ERR(bus_reset);

	th1520_phy->phy_reset = devm_reset_control_get_exclusive(dev, "phy");
	if (IS_ERR(th1520_phy->phy_reset))
		return PTR_ERR(th1520_phy->phy_reset);

	/*
	 * Schematics of several boards (Lichee Module 4A/Milk-V Meles)
	 * describe this power rail as always-on.
	 */
	ret = devm_regulator_get_enable(dev, "avdd33-usb3");
	if (ret)
		return ret;

	th1520_phy->regmap = devm_regmap_init_mmio(dev,
						   base + USB_SYSCON_OFFSET,
						   &phy_regmap_config);
	if (IS_ERR(th1520_phy->regmap))
		return dev_err_probe(dev, PTR_ERR(th1520_phy->regmap),
				     "Failed to init regmap\n");

	th1520_phy->phy = devm_phy_create(dev, dev->of_node, &th1520_usb_phy_ops);
	if (IS_ERR(th1520_phy->phy)) {
		dev_err(dev, "failed to create PHY\n");
		return PTR_ERR(th1520_phy->phy);
	}

	phy_set_drvdata(th1520_phy->phy, th1520_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id th1520_usb_phy_of_table[] = {
	{ .compatible = "thead,th1520-usb-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, th1520_usb_phy_of_table);

static struct platform_driver th1520_usb_phy_driver = {
	.driver = {
		.name = "th1520-usb-phy",
		.of_match_table = th1520_usb_phy_of_table,
	},
	.probe = th1520_usb_phy_probe,
};

module_platform_driver(th1520_usb_phy_driver);

MODULE_DESCRIPTION("T-Head TH1520 USB PHY driver");
MODULE_LICENSE("GPL");

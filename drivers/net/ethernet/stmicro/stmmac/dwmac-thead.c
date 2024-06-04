// SPDX-License-Identifier: GPL-2.0
/*
 * T-HEAD DWMAC platform driver
 *
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (C) 2023 Jisheng Zhang <jszhang@kernel.org>
 *
 */

#include <linux/bitfield.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "stmmac_platform.h"

#define GMAC_CLK_EN			0x00
#define  GMAC_TX_CLK_EN			BIT(1)
#define  GMAC_TX_CLK_N_EN		BIT(2)
#define  GMAC_TX_CLK_OUT_EN		BIT(3)
#define  GMAC_RX_CLK_EN			BIT(4)
#define  GMAC_RX_CLK_N_EN		BIT(5)
#define  GMAC_EPHY_REF_CLK_EN		BIT(6)
#define GMAC_RXCLK_DELAY_CTRL		0x04
#define  GMAC_RXCLK_BYPASS		BIT(15)
#define  GMAC_RXCLK_INVERT		BIT(14)
#define  GMAC_RXCLK_DELAY_MASK		GENMASK(4, 0)
#define  GMAC_RXCLK_DELAY_VAL(x)	FIELD_PREP(GMAC_RXCLK_DELAY_MASK, (x))
#define GMAC_TXCLK_DELAY_CTRL		0x08
#define  GMAC_TXCLK_BYPASS		BIT(15)
#define  GMAC_TXCLK_INVERT		BIT(14)
#define  GMAC_TXCLK_DELAY_MASK		GENMASK(4, 0)
#define  GMAC_TXCLK_DELAY_VAL(x)	FIELD_PREP(GMAC_RXCLK_DELAY_MASK, (x))
#define GMAC_PLLCLK_DIV			0x0c
#define  GMAC_PLLCLK_DIV_EN		BIT(31)
#define  GMAC_PLLCLK_DIV_MASK		GENMASK(7, 0)
#define  GMAC_PLLCLK_DIV_NUM(x)		FIELD_PREP(GMAC_PLLCLK_DIV_MASK, (x))
#define GMAC_CLK_PTP			0x14
#define  GMAC_CLK_PTP_DIV_EN		BIT(31)
#define  GMAC_CLK_PTP_DIV_MASK		GENMASK(7, 0)
#define  GMAC_CLK_PTP_DIV_NUM(x)	FIELD_PREP(GMAC_CLK_PTP_DIV_MASK, (x))
#define GMAC_GTXCLK_SEL			0x18
#define  GMAC_GTXCLK_SEL_PLL		BIT(0)
#define GMAC_INTF_CTRL			0x1c
#define  PHY_INTF_MASK			BIT(0)
#define  PHY_INTF_RGMII			FIELD_PREP(PHY_INTF_MASK, 1)
#define  PHY_INTF_MII_GMII		FIELD_PREP(PHY_INTF_MASK, 0)
#define GMAC_TXCLK_OEN			0x20
#define  TXCLK_DIR_MASK			BIT(0)
#define  TXCLK_DIR_OUTPUT		FIELD_PREP(TXCLK_DIR_MASK, 0)
#define  TXCLK_DIR_INPUT		FIELD_PREP(TXCLK_DIR_MASK, 1)

#define GMAC_GMII_RGMII_RATE	125000000
#define GMAC_MII_RATE		25000000
#define GMAC_PTP_CLK_RATE	50000000 //50MHz

struct thead_dwmac {
	struct plat_stmmacenet_data *plat;
	struct regmap *apb_regmap;
	struct device *dev;
	u32 rx_delay;
	u32 tx_delay;
};

static int thead_dwmac_set_phy_if(struct plat_stmmacenet_data *plat)
{
	struct thead_dwmac *dwmac = plat->bsp_priv;
	u32 phyif;

	switch (plat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		phyif = PHY_INTF_MII_GMII;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		phyif = PHY_INTF_RGMII;
		break;
	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return -EINVAL;
	};

	regmap_write(dwmac->apb_regmap, GMAC_INTF_CTRL, phyif);

	return 0;
}

static int thead_dwmac_set_txclk_dir(struct plat_stmmacenet_data *plat)
{
	struct thead_dwmac *dwmac = plat->bsp_priv;
	u32 txclk_dir;

	switch (plat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		txclk_dir = TXCLK_DIR_INPUT;
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		txclk_dir = TXCLK_DIR_OUTPUT;
		break;
	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return -EINVAL;
	};

	regmap_write(dwmac->apb_regmap, GMAC_TXCLK_OEN, txclk_dir);

	return 0;
}

static void thead_dwmac_fix_speed(void *priv, unsigned int speed, unsigned int mode)
{
	struct thead_dwmac *dwmac = priv;
	struct plat_stmmacenet_data *plat = dwmac->plat;
	unsigned long rate;
	u32 div;

	switch (plat->mac_interface) {
	/* For MII, rxc/txc is provided by phy */
	case PHY_INTERFACE_MODE_MII:
		return;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rate = clk_get_rate(plat->stmmac_clk);
		if (!rate || rate % GMAC_GMII_RGMII_RATE != 0 ||
		    rate % GMAC_MII_RATE != 0) {
			dev_err(dwmac->dev, "invalid gmac rate %ld\n", rate);
			return;
		}

		regmap_update_bits(dwmac->apb_regmap, GMAC_PLLCLK_DIV, GMAC_PLLCLK_DIV_EN, 0);

		switch (speed) {
		case SPEED_1000:
			div = rate / GMAC_GMII_RGMII_RATE;
			break;
		case SPEED_100:
			div = rate / GMAC_MII_RATE;
			break;
		case SPEED_10:
			div = rate * 10 / GMAC_MII_RATE;
			break;
		default:
			dev_err(dwmac->dev, "invalid speed %u\n", speed);
			return;
		}
		regmap_update_bits(dwmac->apb_regmap, GMAC_PLLCLK_DIV,
				   GMAC_PLLCLK_DIV_MASK, GMAC_PLLCLK_DIV_NUM(div));

		regmap_update_bits(dwmac->apb_regmap, GMAC_PLLCLK_DIV,
				   GMAC_PLLCLK_DIV_EN, GMAC_PLLCLK_DIV_EN);
		break;
	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return;
	}
}

static int thead_dwmac_enable_clk(struct plat_stmmacenet_data *plat)
{
	struct thead_dwmac *dwmac = plat->bsp_priv;
	u32 reg;

	switch (plat->mac_interface) {
	case PHY_INTERFACE_MODE_MII:
		reg = GMAC_RX_CLK_EN | GMAC_TX_CLK_EN;
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		/* use pll */
		regmap_write(dwmac->apb_regmap, GMAC_GTXCLK_SEL, GMAC_GTXCLK_SEL_PLL);

		reg = GMAC_TX_CLK_EN | GMAC_TX_CLK_N_EN | GMAC_TX_CLK_OUT_EN |
		      GMAC_RX_CLK_EN | GMAC_RX_CLK_N_EN;
		break;

	default:
		dev_err(dwmac->dev, "unsupported phy interface %d\n",
			plat->mac_interface);
		return -EINVAL;
	}

	regmap_write(dwmac->apb_regmap, GMAC_CLK_EN, reg);

	return 0;
}
static void thead_dwmac_set_ptp_clk(struct plat_stmmacenet_data *plat_dat,unsigned int ptp_clk_rate)
{
	unsigned int div;
	struct thead_dwmac *dwmac = plat_dat->bsp_priv;

	unsigned long src_freq = clk_get_rate(plat_dat->stmmac_clk);

	if(!ptp_clk_rate || !src_freq)
	{
		pr_warn("invalid gmac pll freq %lu or ptp_clk_rate %d\n", src_freq,ptp_clk_rate);
		return;
	}
	/* disable clk_div */
	regmap_update_bits(dwmac->apb_regmap, GMAC_CLK_PTP, GMAC_CLK_PTP_DIV_EN, 0);

	div = src_freq / ptp_clk_rate;
	regmap_update_bits(dwmac->apb_regmap, GMAC_CLK_PTP,
			GMAC_CLK_PTP_DIV_MASK, GMAC_CLK_PTP_DIV_NUM(div));

	/* enable clk_div */
	regmap_update_bits(dwmac->apb_regmap, GMAC_CLK_PTP,
			GMAC_CLK_PTP_DIV_EN, GMAC_CLK_PTP_DIV_EN);
	return ;
}

static int thead_dwmac_init(struct platform_device *pdev,
			    struct plat_stmmacenet_data *plat)
{
	struct thead_dwmac *dwmac = plat->bsp_priv;
	int ret;

	ret = thead_dwmac_set_phy_if(plat);
	if (ret)
		return ret;

	ret = thead_dwmac_set_txclk_dir(plat);
	if (ret)
		return ret;

	regmap_write(dwmac->apb_regmap, GMAC_RXCLK_DELAY_CTRL,
		     GMAC_RXCLK_DELAY_VAL(dwmac->rx_delay));
	regmap_write(dwmac->apb_regmap, GMAC_TXCLK_DELAY_CTRL,
		     GMAC_TXCLK_DELAY_VAL(dwmac->tx_delay));

	thead_dwmac_fix_speed(dwmac, SPEED_1000, 0);

	thead_dwmac_set_ptp_clk(plat,GMAC_PTP_CLK_RATE);

	return thead_dwmac_enable_clk(plat);
}

static int thead_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources stmmac_res;
	struct thead_dwmac *dwmac;
	struct device_node *np = pdev->dev.of_node;
	u32 delay_ps;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to get resources\n");

	plat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat),
				     "dt configuration failed\n");

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	if (!of_property_read_u32(np, "rx-internal-delay-ps", &delay_ps))
		dwmac->rx_delay = delay_ps;
	if (!of_property_read_u32(np, "tx-internal-delay-ps", &delay_ps))
		dwmac->tx_delay = delay_ps;

	dwmac->apb_regmap = syscon_regmap_lookup_by_phandle(np, "thead,gmacapb");
	if (IS_ERR(dwmac->apb_regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(dwmac->apb_regmap),
				     "Failed to get gmac apb syscon\n");

	dwmac->dev = &pdev->dev;
	dwmac->plat = plat;
	plat->bsp_priv = dwmac;
	plat->fix_mac_speed = thead_dwmac_fix_speed;

	ret = thead_dwmac_init(pdev, plat);
	if (ret)
		return ret;

	return stmmac_dvr_probe(&pdev->dev, plat, &stmmac_res);
}

static const struct of_device_id thead_dwmac_match[] = {
	{ .compatible = "thead,th1520-dwmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, thead_dwmac_match);

static struct platform_driver thead_dwmac_driver = {
	.probe = thead_dwmac_probe,
	.remove_new = stmmac_pltfr_remove,
	.driver = {
		.name = "thead-dwmac",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = thead_dwmac_match,
	},
};
module_platform_driver(thead_dwmac_driver);

MODULE_AUTHOR("T-HEAD");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("T-HEAD dwmac platform driver");
MODULE_LICENSE("GPL");

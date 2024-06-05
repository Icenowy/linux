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
#include <linux/pm_runtime.h>

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
	struct clk *gmac_axi_aclk;
	struct clk *gmac_axi_pclk;
};

#define  pm_debug dev_dbg	/* for suspend/resume interface debug info */

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
			    void *bsp_priv)
{
	struct thead_dwmac *dwmac = bsp_priv;
	int ret;

	ret = thead_dwmac_set_phy_if(dwmac->plat);
	if (ret)
		return ret;

	ret = thead_dwmac_set_txclk_dir(dwmac->plat);
	if (ret)
		return ret;

	regmap_write(dwmac->apb_regmap, GMAC_RXCLK_DELAY_CTRL,
		     GMAC_RXCLK_DELAY_VAL(dwmac->rx_delay));
	regmap_write(dwmac->apb_regmap, GMAC_TXCLK_DELAY_CTRL,
		     GMAC_TXCLK_DELAY_VAL(dwmac->tx_delay));

	thead_dwmac_fix_speed(dwmac, SPEED_1000, 0);

	thead_dwmac_set_ptp_clk(dwmac->plat,GMAC_PTP_CLK_RATE);

	return thead_dwmac_enable_clk(dwmac->plat);
}

int thead_dwmac_clk_enable(struct platform_device *pdev, void *bsp_priv)
{
	struct thead_dwmac *thead_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);

	ret = clk_prepare_enable(thead_plat_dat->gmac_axi_aclk);
	if (ret) {
		dev_err(dev, "Failed to enable clk 'gmac_axi_aclk'\n");
		return -EINVAL;
	}
	ret = clk_prepare_enable(thead_plat_dat->gmac_axi_pclk);
	if (ret) {
		clk_disable_unprepare(thead_plat_dat->gmac_axi_aclk);
		dev_err(dev, "Failed to enable clk 'gmac_axi_pclk'\n");
		return -EINVAL;
	}
	
	return ret;
}

void thead_dwmac_clk_disable(struct platform_device *pdev, void *bsp_priv)
{
	struct thead_dwmac *thead_plat_dat = bsp_priv;
	struct device *dev = &pdev->dev;
	pm_debug(dev,"enter %s()\n",__func__);
	
	clk_disable_unprepare(thead_plat_dat->gmac_axi_aclk);
	clk_disable_unprepare(thead_plat_dat->gmac_axi_pclk);

	return ;
}

/**
 * dwmac1000_validate_mcast_bins - validates the number of Multicast filter bins
 * @dev: struct device of the platform device
 * @mcast_bins: Multicast filtering bins
 * Description:
 * this function validates the number of Multicast filtering bins specified
 * by the configuration through the device tree. The Synopsys GMAC supports
 * 64 bins, 128 bins, or 256 bins. "bins" refer to the division of CRC
 * number space. 64 bins correspond to 6 bits of the CRC, 128 corresponds
 * to 7 bits, and 256 refers to 8 bits of the CRC. Any other setting is
 * invalid and will cause the filtering algorithm to use Multicast
 * promiscuous mode.
 */
static int dwmac1000_validate_mcast_bins(struct device *dev, int mcast_bins)
{
	int x = mcast_bins;

	switch (x) {
	case HASH_TABLE_SIZE:
	case 128:
	case 256:
		break;
	default:
		x = 0;
		dev_info(dev, "Hash table entries set to unexpected value %d\n",
			 mcast_bins);
		break;
	}
	return x;
}

/**
 * dwmac1000_validate_ucast_entries - validate the Unicast address entries
 * @dev: struct device of the platform device
 * @ucast_entries: number of Unicast address entries
 * Description:
 * This function validates the number of Unicast address entries supported
 * by a particular Synopsys 10/100/1000 controller. The Synopsys controller
 * supports 1..32, 64, or 128 Unicast filter entries for it's Unicast filter
 * logic. This function validates a valid, supported configuration is
 * selected, and defaults to 1 Unicast address if an unsupported
 * configuration is selected.
 */
static int dwmac1000_validate_ucast_entries(struct device *dev,
					    int ucast_entries)
{
	int x = ucast_entries;

	switch (x) {
	case 1 ... 32:
	case 64:
	case 128:
		break;
	default:
		x = 1;
		dev_info(dev, "Unicast table entries set to unexpected value %d\n",
			 ucast_entries);
		break;
	}
	return x;
}

static int thead_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat;
	struct stmmac_resources stmmac_res;
	struct thead_dwmac *dwmac;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	u32 delay_ps;
	int ret;

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to get resources\n");

	plat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat),
				     "dt configuration failed\n");

	of_property_read_u32(np, "max-frame-size", &plat->maxmtu);
	of_property_read_u32(np, "snps,multicast-filter-bins",
					&plat->multicast_filter_bins);
	of_property_read_u32(np, "snps,perfect-filter-entries",
					&plat->unicast_filter_entries);
	plat->unicast_filter_entries = dwmac1000_validate_ucast_entries(
			&pdev->dev, plat->unicast_filter_entries);
	plat->multicast_filter_bins = dwmac1000_validate_mcast_bins(
			&pdev->dev, plat->multicast_filter_bins);
	plat->has_gmac = 1;
	plat->pmt = 1;

	if (!of_property_read_u32(np, "rx-internal-delay-ps", &delay_ps))
		dwmac->rx_delay = delay_ps;
	if (!of_property_read_u32(np, "tx-internal-delay-ps", &delay_ps))
		dwmac->tx_delay = delay_ps;

	dwmac->apb_regmap = syscon_regmap_lookup_by_phandle(np, "thead,gmacapb");
	if (IS_ERR(dwmac->apb_regmap)) {
		ret = dev_err_probe(&pdev->dev, PTR_ERR(dwmac->apb_regmap),
				     "Failed to get gmac apb syscon\n");
		goto err_remove_config_dt;
	}
	dwmac->gmac_axi_aclk = devm_clk_get(dev, "axi_aclk");
	if (IS_ERR(dwmac->gmac_axi_aclk)) {
		dev_err(dev, "gmac axi_aclk not exist, skipped it\n");
	}
	dwmac->gmac_axi_pclk = devm_clk_get(dev, "axi_pclk");
	if (IS_ERR(dwmac->gmac_axi_pclk)) {
		dev_err(dev, "gmac axi_pclk not exist, skipped it\n");
	}

	dwmac->dev = &pdev->dev;
	dwmac->plat = plat;
	plat->bsp_priv = dwmac;
	plat->fix_mac_speed = thead_dwmac_fix_speed;
	plat->init = thead_dwmac_init;

	ret = thead_dwmac_clk_enable(pdev,dwmac);
	if (ret)
		goto err_remove_config_dt;

	ret = thead_dwmac_init(pdev, dwmac);
	if (ret)
		goto err_exit;

	ret = stmmac_dvr_probe(&pdev->dev, plat, &stmmac_res);
	if (ret)
		goto err_exit;
	
	return 0;

err_exit:
	dev_err(dev,"%s: dwmac probe faild,ret%d\n",__func__,ret);
	thead_dwmac_clk_disable(pdev, dwmac);
err_remove_config_dt:
	stmmac_remove_config_dt(pdev, plat);
	return ret;
}
/**
 * thead_dwmac_suspend
 * @dev: device pointer
 * Description: this function is invoked when suspend the driver and it direcly
 * call the main suspend function and then, if required, on some platform, it
 * can call an exit helper.
 */
static int __maybe_unused thead_dwmac_suspend(struct device *dev)
{
	int ret;
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev,"enter %s()\n",__func__);
	ret = stmmac_suspend(dev);
	if (priv->plat->exit)
		priv->plat->exit(pdev, priv->plat->bsp_priv);
	
	return ret;
}

/**
 * thead_dwmac_resume
 * @dev: device pointer
 * Description: this function is invoked when resume the driver before calling
 * the main resume function, on some platforms, it can call own init helper
 * if required.
 */
static int __maybe_unused thead_dwmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev,"enter %s()\n",__func__);

	pm_runtime_get_sync(dev);
	if (priv->plat->init)
		priv->plat->init(pdev, priv->plat->bsp_priv);
	pm_runtime_put(dev);

	return stmmac_resume(dev);
}

static int __maybe_unused thead_dwmac_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	pm_debug(dev,"enter %s()\n",__func__);
	stmmac_bus_clks_config(priv, false);
	thead_dwmac_clk_disable(pdev, priv->plat->bsp_priv);
	return 0;
}

static int __maybe_unused thead_dwmac_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct platform_device *pdev = to_platform_device(dev);
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);
	ret = stmmac_bus_clks_config(priv, true);
	if(ret)
		return ret;
	ret = thead_dwmac_clk_enable(pdev, priv->plat->bsp_priv);
	if(ret)
		return ret;

	return 0;
}

static int __maybe_unused thead_dwmac_noirq_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);
	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* Disable clock in case of PWM is off */
		clk_disable_unprepare(priv->plat->clk_ptp_ref);

		ret = pm_runtime_force_suspend(dev);
		if (ret)
			return ret;
	}

	return 0;
}

static int __maybe_unused thead_dwmac_noirq_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;
	pm_debug(dev,"enter %s()\n",__func__);
	if (!netif_running(ndev))
		return 0;

	if (!device_may_wakeup(priv->device) || !priv->plat->pmt) {
		/* enable the clk previously disabled */
		ret = pm_runtime_force_resume(dev);
		if (ret)
			return ret;

		ret = clk_prepare_enable(priv->plat->clk_ptp_ref);
		if (ret < 0) {
			netdev_warn(priv->dev,
				    "failed to enable PTP reference clock: %pe\n",
				    ERR_PTR(ret));
			return ret;
		}
	}

	return 0;
}

/*similar with stmmac_pltfr_pm_ops,but clks enable/disable add this drv need */
const struct dev_pm_ops thead_dwmac_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(thead_dwmac_suspend, thead_dwmac_resume)
	SET_RUNTIME_PM_OPS(thead_dwmac_runtime_suspend, thead_dwmac_runtime_resume, NULL)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(thead_dwmac_noirq_suspend, thead_dwmac_noirq_resume)
};

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
		.pm = &thead_dwmac_pm_ops,
		.of_match_table = thead_dwmac_match,
	},
};
module_platform_driver(thead_dwmac_driver);

MODULE_AUTHOR("T-HEAD");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("T-HEAD dwmac platform driver");
MODULE_LICENSE("GPL");

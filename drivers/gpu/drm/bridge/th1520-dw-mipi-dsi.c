// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Institute of Software, Chinese Academy of Sciences (ISCAS)
 *
 * Author:
 *   Icenowy Zheng <zhengxingda@iscas.ac.cn>
 *
 * Based on dw_mipi_dsi-stm.c, which is:
 *   Copyright (C) STMicroelectronics SA 2017
 *
 * Based on dw-mipi-dsi-rockchip.c, which is:
 *   Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Based on vendor kernel phy-dw-mipi-dphy.c, which is:
 *   Copyright (C) Alibaba Group Holding Limited.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/phy/phy-mipi-dphy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>

/* DPHY registers */

#define PLL_DELAY_LP_TX_START_LP11	0x02
#define TXDINDLYTIME_9_2		GENMASK(7,  0)

#define PLL_FSM_CNTRL			0x03

#define PLL_PROP_CHARGE_PUMP_CTRL	0x0e
#define PLL_PROP_CNTRL			GENMASK(5,  0)

#define PLL_INT_CHARGE_PUMP_CTRL	0x0f
#define PLL_INT_CNTRL			GENMASK(5,  0)

#define PLL_VCO_CTRL			0x12
#define PLL_VCO_CNTRL_OVR_EN		BIT(6)
#define PLL_VCO_CNTRL_OVR		GENMASK(5,  0)

#define PLL_GMP_CTRL_DIGITAL_TEST	0x13
#define PLL_TESTLOCK			BIT(7)
#define PLL_GMP_CNTRL			GENMASK(5,  4)
#define TSTPLLDIG			GENMASK(3,  1)
#define TSTPLLDIG_LOCK			(0x0 << 1)
#define TSTPLLDIG_REFCLK		(0x1 << 1)
#define TSTPLLDIG_FBCLK			(0x2 << 1)
#define TSTPLLDIG_INT_CLKIN		(0x3 << 1)
#define TSTPLLDIG_LOCK_FROM_DETECTOR	(0x4 << 1)
#define TSTPLLDIG_OUTPUT		(0x5 << 1)
#define TSTPLLDIG_BUF_VCOCLK		(0x6 << 1)
#define TSTPLLDIG_UPDATEPLL		(0x7 << 1)

#define PLL_N_DIV_FSM_SIG		0x17
#define PLL_N_OVR			GENMASK(3,  0)

#define PLL_M_DIV			0x18
#define PLL_M_OVR_4_0			GENMASK(4,  0)
#define PLL_M_OVR_9_5			GENMASK(4,  0)

#define PLL_N_AND_M_DIV_CTRL		0x19
#define PLL_M_OVR_EN			BIT(5)
#define PLL_N_OVR_EN			BIT(4)

#define PLL_CHARGE_PUMP_BIAS_CTRL	0x1c
#define PLL_CPBIAS_CNTRL		GENMASK(6,  0)

#define PLL_ANALOG_PROGRAM_CONTROL	0x1f
#define PLL_MPLL_PROG_5_0		GENMASK(5,  0)
#define PLL_MPLL_PROG_11_6		GENMASK(5,  0)
#define PLL_MPLL_PROG_16_12		GENMASK(4,  0)

#define HS_FREQ_RANGE_OF_OPERATION	0x44
#define HSFREQRANGE_OVR_EN		BIT(7)
#define HSFREQRANGE_OVR			GENMASK(6,  0)
#define HSFREQRANGE			HSFREQRANGE_OVR

#define PG_LP_BIAS_LANE0		0x4a
#define PRG_ON_LANE0			BIT(6)

#define SR_FSM_OVR_CNTRL		0xa0
#define SR_DDL_LOOP_CONF		0xa3

/* syscon registers */

#define MIPIDSI0_SYSREG			0x8
#define MIPIDSI1_SYSREG			0xc
#define MIPIDSI_HSFREQRANGE		GENMASK(9,  3)
#define MIPIDSI_CFGCLKFREQRANGE		GENMASK(15, 10)
#define MIPIDSI_CFGCLKFREQRANGE_DEF	(0x1c << 10)

struct pll_hsfreqrange {
	unsigned int max_mbps;
	u8 hsfreqrange;
};

static const struct pll_hsfreqrange hsfreqrange_table[] = {
	{ 80,  0x00 },
	{ 90,  0x10 },
	{ 100, 0x20 },
	{ 110, 0x30 },
	{ 120, 0x01 },
	{ 130, 0x11 },
	{ 140, 0x21 },
	{ 150, 0x31 },
	{ 160, 0x02 },
	{ 170, 0x12 },
	{ 180, 0x22 },
	{ 190, 0x32 },
	{ 205, 0x03 },
	{ 220, 0x13 },
	{ 235, 0x23 },
	{ 250, 0x33 },
	{ 275, 0x04 },
	{ 300, 0x14 },
	{ 325, 0x25 },
	{ 350, 0x35 },
	{ 400, 0x05 },
	{ 450, 0x16 },
	{ 500, 0x26 },
	{ 550, 0x37 },
	{ 600, 0x07 },
	{ 650, 0x18 },
	{ 700, 0x28 },
	{ 750, 0x39 },
	{ 800, 0x09 },
	{ 850, 0x19 },
	{ 900, 0x29 },
	{ 950, 0x3A },
	{ 1000, 0x0A },
	{ 1050, 0x1A },
	{ 1100, 0x2A },
	{ 1150, 0x3B },
	{ 1200, 0x0B },
	{ 1250, 0x1B },
	{ 1300, 0x2B },
	{ 1350, 0x3C },
	{ 1400, 0x0C },
	{ 1450, 0x1C },
	{ 1500, 0x2C },
	{ 1550, 0x3D },
	{ 1600, 0x0D },
	{ 1650, 0x1D },
	{ 1700, 0x2E },
	{ 1750, 0x3E },
	{ 1800, 0x0E },
	{ 1850, 0x1E },
	{ 1900, 0x2F },
	{ 1950, 0x3F },
	{ 2000, 0x0F },
	{ 2050, 0x40 },
	{ 2100, 0x41 },
	{ 2150, 0x42 },
	{ 2200, 0x43 },
	{ 2250, 0x44 },
	{ 2300, 0x45 },
	{ 2350, 0x46 },
	{ 2400, 0x47 },
	{ 2450, 0x48 },
	{ 2500, 0x49 },
};

struct hstt {
	unsigned int max_mbps;
	struct dw_mipi_dsi_dphy_timing timing;
};

#define HSTT(_max_mbps, _c_lp2hs, _c_hs2lp, _d_lp2hs, _d_hs2lp)	\
{					\
	.max_mbps = _max_mbps,		\
	.timing = {			\
		.clk_lp2hs = _c_lp2hs,	\
		.clk_hs2lp = _c_hs2lp,	\
		.data_lp2hs = _d_lp2hs,	\
		.data_hs2lp = _d_hs2lp,	\
	}				\
}

static struct hstt hstt_table[] = {
	HSTT(80, 21, 17, 15, 10),
	HSTT(90, 23, 17, 16, 10),
	HSTT(100, 22, 17, 16, 10),
	HSTT(110, 25, 18, 17, 11),
	HSTT(120, 26, 20, 18, 11),
	HSTT(130, 27, 19, 19, 11),
	HSTT(140, 27, 19, 19, 11),
	HSTT(150, 28, 20, 20, 12),
	HSTT(160, 30, 21, 22, 13),
	HSTT(170, 30, 21, 23, 13),
	HSTT(180, 31, 21, 23, 13),
	HSTT(190, 32, 22, 24, 13),
	HSTT(205, 35, 22, 25, 13),
	HSTT(220, 37, 26, 27, 15),
	HSTT(235, 38, 28, 27, 16),
	HSTT(250, 41, 29, 30, 17),
	HSTT(275, 43, 29, 32, 18),
	HSTT(300, 45, 32, 35, 19),
	HSTT(325, 48, 33, 36, 18),
	HSTT(350, 51, 35, 40, 20),
	HSTT(400, 59, 37, 44, 21),
	HSTT(450, 65, 40, 49, 23),
	HSTT(500, 71, 41, 54, 24),
	HSTT(550, 77, 44, 57, 26),
	HSTT(600, 82, 46, 64, 27),
	HSTT(650, 87, 48, 67, 28),
	HSTT(700, 94, 52, 71, 29),
	HSTT(750, 99, 52, 75, 31),
	HSTT(800, 105, 55, 82, 32),
	HSTT(850, 110, 58, 85, 32),
	HSTT(900, 115, 58, 88, 35),
	HSTT(950, 120, 62, 93, 36),
	HSTT(1000, 128, 63, 99, 38),
	HSTT(1050, 132, 65, 102, 38),
	HSTT(1100, 138, 67, 106, 39),
	HSTT(1150, 146, 69, 112, 42),
	HSTT(1200, 151, 71, 117, 43),
	HSTT(1250, 153, 74, 120, 45),
	HSTT(1300, 160, 73, 124, 46),
	HSTT(1350, 165, 76, 130, 47),
	HSTT(1400, 172, 78, 134, 49),
	HSTT(1450, 177, 80, 138, 49),
	HSTT(1500, 183, 81, 143, 52),
	HSTT(1550, 191, 84, 147, 52),
	HSTT(1600, 194, 85, 152, 52),
	HSTT(1650, 201, 86, 155, 53),
	HSTT(1700, 208, 88, 161, 53),
	HSTT(1750, 212, 89, 165, 53),
	HSTT(1800, 220, 90, 171, 54),
	HSTT(1850, 223, 92, 175, 54),
	HSTT(1900, 231, 91, 180, 55),
	HSTT(1950, 236, 95, 185, 56),
	HSTT(2000, 243, 97, 190, 56),
	HSTT(2050, 248, 99, 194, 58),
	HSTT(2100, 252, 100, 199, 59),
	HSTT(2150, 259, 102, 204, 61),
	HSTT(2200, 266, 105, 210, 62),
	HSTT(2250, 269, 109, 213, 63),
	HSTT(2300, 272, 109, 217, 65),
	HSTT(2350, 281, 112, 225, 66),
	HSTT(2400, 283, 115, 226, 66),
	HSTT(2450, 282, 115, 226, 67),
	HSTT(2500, 281, 118, 227, 67),
};

struct dsi_chip_data {
	u64 reg;

	u32 sysreg;
};

struct th1520_dw_mipi_dsi {
	struct device *dev;

	struct dw_mipi_dsi *dmd;
	struct dw_mipi_dsi_plat_data pdata;
	const struct dsi_chip_data *cdata;

	struct regmap *syscon;
	struct clk *clk_pix;
	struct clk *clk_ref;
	struct clk *clk_cfg;

	unsigned int lane_mbps;
	u8 hsfreqrange;
};

static int dsi_phy_init(void *priv_data)
{
	struct th1520_dw_mipi_dsi *dsi = priv_data;
	u32 val;

	val = FIELD_PREP(MIPIDSI_HSFREQRANGE, dsi->hsfreqrange) |
	      MIPIDSI_CFGCLKFREQRANGE_DEF;

	regmap_update_bits(dsi->syscon, dsi->cdata->sysreg,
			   MIPIDSI_HSFREQRANGE | MIPIDSI_CFGCLKFREQRANGE, val);

	/* disable slew rate calibration */
	dw_mipi_dsi_phy_write(dsi->dmd, SR_DDL_LOOP_CONF, 0);
	dw_mipi_dsi_phy_write(dsi->dmd, SR_FSM_OVR_CNTRL, 1);

	/* configure prg_on_lane0 */
	dw_mipi_dsi_phy_write(dsi->dmd, PG_LP_BIAS_LANE0, PRG_ON_LANE0);

	/* configure PLL charge pump */
	if (dsi->lane_mbps >= 1150)
		dw_mipi_dsi_phy_write(dsi->dmd, PLL_PROP_CHARGE_PUMP_CTRL, 0xE);
	else
		dw_mipi_dsi_phy_write(dsi->dmd, PLL_PROP_CHARGE_PUMP_CTRL, 0x8);
	dw_mipi_dsi_phy_write(dsi->dmd, PLL_CHARGE_PUMP_BIAS_CTRL, 0x10);

	return 0;
}

static void dsi_phy_power_on(void *priv_data)
{
	struct th1520_dw_mipi_dsi *dsi = priv_data;

	clk_prepare_enable(dsi->clk_pix);
}

static void dsi_phy_power_off(void *priv_data)
{
	struct th1520_dw_mipi_dsi *dsi = priv_data;

	clk_disable_unprepare(dsi->clk_pix);
}

static unsigned int
calculate_lane_mbps(const struct drm_display_mode *mode,
		    unsigned long mode_flags,
		    u32 lanes, u32 format)
{
	int bpp = mipi_dsi_pixel_format_to_bpp(format);
	union phy_configure_opts phy_opts;

	/*
	 * FIXME: the extra bandwidth headroom logic mimics what the Rockchip
	 * driver does, and test shows that it's necessary for the hardware
	 * to work with the mainline dw-mipi-dsi common code.
	 */
	phy_mipi_dphy_get_default_config(mode->clock * 1000 * 10 / 8,
					 bpp, lanes,
					 &phy_opts.mipi_dphy);

	return DIV_ROUND_UP(phy_opts.mipi_dphy.hs_clk_rate, USEC_PER_SEC);
}

static int
dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
		  unsigned long mode_flags, u32 lanes, u32 format,
		  unsigned int *lane_mbps)
{
	struct th1520_dw_mipi_dsi *dsi = priv_data;
	int i;

	*lane_mbps = calculate_lane_mbps(mode, mode_flags, lanes, format);

	for (i = 0; i < ARRAY_SIZE(hsfreqrange_table); i++)
		if (*lane_mbps <= hsfreqrange_table[i].max_mbps)
			break;

	if (i == ARRAY_SIZE(hsfreqrange_table))
		return -EINVAL;

	*lane_mbps = hsfreqrange_table[i].max_mbps;
	dsi->lane_mbps = *lane_mbps;
	dsi->hsfreqrange = hsfreqrange_table[i].hsfreqrange;

	return 0;
}

static int
dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
		   struct dw_mipi_dsi_dphy_timing *timing)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hstt_table); i++)
		if (lane_mbps <= hstt_table[i].max_mbps)
			break;

	if (i == ARRAY_SIZE(hstt_table))
		return -EINVAL;

	*timing = hstt_table[i].timing;

	return 0;
}

static const struct dw_mipi_dsi_phy_ops th1520_dw_mipi_dsi_phy_ops = {
	.init = dsi_phy_init,
	.power_on = dsi_phy_power_on,
	.power_off = dsi_phy_power_off,
	.get_lane_mbps = dsi_get_lane_mbps,
	.get_timing = dsi_phy_get_timing,
};

static enum drm_mode_status
dsi_mode_valid(void *priv_data, const struct drm_display_mode *mode,
	       unsigned long mode_flags, u32 lanes, u32 format)
{
	unsigned int lane_mbps, max_mbps;

	lane_mbps = calculate_lane_mbps(mode, mode_flags, lanes, format);
	max_mbps = hsfreqrange_table[ARRAY_SIZE(hsfreqrange_table) - 1].max_mbps;

	if (lane_mbps > max_mbps)
		return MODE_NOCLOCK;

	return MODE_OK;
}

static struct dsi_chip_data th1520_chip_data[] = {
	{
		.reg = 0xffef500000,
		.sysreg = MIPIDSI0_SYSREG,

		/*
		 * TODO: The user manual shows a mux register for DSI0 but the
		 * vendor driver doesn't touch it either.
		 */
	},
	{
		.reg = 0xffef510000,
		.sysreg = MIPIDSI1_SYSREG,
	},
	{ /* sentinel */ },
};

static int th1520_dw_mipi_dsi_probe(struct platform_device *pdev)
{
	const struct dsi_chip_data *cdata;
	struct device *dev = &pdev->dev;
	struct th1520_dw_mipi_dsi *dsi;
	struct resource *res;
	void __iomem *base;
	int i, ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->dev = dev;

	dsi->syscon = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
	if (IS_ERR(dsi->syscon)) {
		ret = PTR_ERR(dsi->syscon);
		dev_err_probe(dev, ret, "Unable to get system controller regmap\n");
		return ret;
	}

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base)) {
		dev_err_probe(dev, ret, "Unable to get DSI registers\n");
		return PTR_ERR(base);
	}

	cdata = of_device_get_match_data(dev);
	i = 0;
	while (cdata[i].reg) {
		if (cdata[i].reg == res->start) {
			dsi->cdata = &cdata[i];
			break;
		}

		i++;
	}

	if (!dsi->cdata) {
		dev_err(dev, "Failed to find chip data for this controller\n");
		return -EINVAL;
	}

	dsi->clk_ref = devm_clk_get_enabled(dev, "ref");
	if (IS_ERR(dsi->clk_ref)) {
		ret = PTR_ERR(dsi->clk_ref);
		dev_err_probe(dev, ret, "Unable to get PHY reference clock\n");
		return ret;
	}

	dsi->clk_cfg = devm_clk_get_enabled(dev, "phy_cfg");
	if (IS_ERR(dsi->clk_cfg)) {
		ret = PTR_ERR(dsi->clk_cfg);
		dev_err_probe(dev, ret, "Unable to get PHY configuration clock\n");
		return ret;
	}

	dsi->clk_pix = devm_clk_get(dev, "pixel");
	if (IS_ERR(dsi->clk_pix)) {
		ret = PTR_ERR(dsi->clk_pix);
		dev_err_probe(dev, ret, "Unable to get PHY pixel clock\n");
		return ret;
	}

	dsi->pdata.base = base;
	dsi->pdata.priv_data = dsi;
	dsi->pdata.max_data_lanes = 4;
	dsi->pdata.phy_ops = &th1520_dw_mipi_dsi_phy_ops;
	dsi->pdata.mode_valid = dsi_mode_valid;

	platform_set_drvdata(pdev, dsi);

	dsi->dmd = dw_mipi_dsi_probe(pdev, &dsi->pdata);
	if (IS_ERR(dsi->dmd)) {
		ret = PTR_ERR(dsi->dmd);
		dev_err_probe(dev, ret, "Failed to initialize mipi dsi host\n");
		return ret;
	}

	return 0;
}

static void th1520_dw_mipi_dsi_remove(struct platform_device *pdev)
{
	struct th1520_dw_mipi_dsi *dsi = platform_get_drvdata(pdev);

	dw_mipi_dsi_remove(dsi->dmd);
}

static const struct of_device_id th1520_dw_mipi_dsi_dt_ids[] = {
	{ .compatible = "thead,th1520-dw-mipi-dsi", .data = &th1520_chip_data },
	{ },
};
MODULE_DEVICE_TABLE(of, th1520_dw_mipi_dsi_dt_ids);

static struct platform_driver th1520_dw_mipi_dsi_driver = {
	.probe		= th1520_dw_mipi_dsi_probe,
	.remove		= th1520_dw_mipi_dsi_remove,
	.driver		= {
		.of_match_table = th1520_dw_mipi_dsi_dt_ids,
		.name	= "th1520-dw-mipi-dsi",
	},
};

module_platform_driver(th1520_dw_mipi_dsi_driver);

MODULE_AUTHOR("Icenowy Zheng <zhengxingda@iscas.ac.cn>");
MODULE_DESCRIPTION("T-Head TH1520 MIPI DSI host controller driver");
MODULE_LICENSE("GPL");

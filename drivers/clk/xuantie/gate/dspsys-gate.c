/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Alibaba Group Holding Limited.
 */

#include <dt-bindings/clock/th1520-dspsys.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include "clk-gate.h"
#include "../clk.h"

static struct clk *gates[TH1520_CLKGEN_DSPSYS_CLK_END];
static struct clk_onecell_data clk_gate_data;
static const char * const dsp0_cclk_sels[] = {"gmac_pll_foutpostdiv", "dspsys_dsp_clk"};
static const char * const dsp1_cclk_sels[] = {"gmac_pll_foutpostdiv", "dspsys_dsp_clk"};

static int xuantie_dspsys_clk_probe(struct platform_device *pdev)
{
	struct regmap *dspsys_regmap, *tee_dspsys_regmap;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *np_reg = of_parse_phandle(np, "dspsys-regmap", 0);
	void __iomem *gate_base;
	int ret;

	dspsys_regmap = syscon_regmap_lookup_by_phandle(np, "dspsys-regmap");
	if (IS_ERR(dspsys_regmap)) {
		dev_err(&pdev->dev, "cannot find regmap for dsp system register\n");
		return PTR_ERR(dspsys_regmap);
	}

	tee_dspsys_regmap = syscon_regmap_lookup_by_phandle(np, "tee-dspsys-regmap");
	if (IS_ERR(tee_dspsys_regmap)) {
		dev_warn(&pdev->dev, "cannot find regmap for tee dsp system register\n");
		tee_dspsys_regmap = NULL;
	}
	gate_base = of_iomap(np_reg,0);
	// MUX 
	gates[DSPSYS_DSP0_CLK_SWITCH] = xuantie_th1520_clk_mux_flags("dspsys_dsp0_clk_switch", gate_base + 0x1c, 0, 1, dsp0_cclk_sels, ARRAY_SIZE(dsp0_cclk_sels), 0);
	gates[DSPSYS_DSP1_CLK_SWITCH] = xuantie_th1520_clk_mux_flags("dspsys_dsp1_clk_switch", gate_base + 0x20, 0, 1, dsp1_cclk_sels, ARRAY_SIZE(dsp1_cclk_sels), 0);

	// DIV & CDE
	gates[DSPSYS_DSP_CLK] = xuantie_th1520_clk_fixed_factor("dspsys_dsp_clk", "video_pll_foutvco", 1, 3);
	gates[DSPSYS_DSP0_CLK_CDE] = xuantie_clk_th1520_divider("dspsys_dsp0_clk_cde", "dspsys_dsp0_clk_switch", gate_base + 0x0, 0, 3, 4, MUX_TYPE_CDE, 0, 7);
	gates[DSPSYS_DSP1_CLK_CDE] = xuantie_clk_th1520_divider("dspsys_dsp1_clk_cde", "dspsys_dsp1_clk_switch", gate_base + 0x4, 0, 3, 4, MUX_TYPE_CDE, 0, 7);

	// gate
	gates[CLKGEN_DSP0_PCLK] = xuantie_gate_clk_register("clkgen_dsp0_pclk", NULL, dspsys_regmap,
							  0x24, 0, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_DSP1_PCLK] = xuantie_gate_clk_register("clkgen_dsp1_pclk", NULL, dspsys_regmap,
							  0x24, 1, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_DSP1_CCLK] = xuantie_gate_clk_register("clkgen_dsp1_cclk", "dspsys_dsp1_clk_cde", dspsys_regmap,
							  0x24, 2, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_DSP0_CCLK] = xuantie_gate_clk_register("clkgen_dsp0_cclk", "dspsys_dsp0_clk_cde", dspsys_regmap,
							  0x24, 3, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_X2X_DSP2_ACLK_S] = xuantie_gate_clk_register("clkgen_x2x_dsp2_aclk_s", NULL, dspsys_regmap,
							  0x24, 4, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_X2X_DSP0_ACLK_S] = xuantie_gate_clk_register("clkgen_x2x_dsp0_aclk_s", NULL, dspsys_regmap,
							  0x24, 5, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_X2X_X4_DSPSLV_DSP1_ACLK_M] = xuantie_gate_clk_register("clkgen_x2x_x4_dspslv_dsp1_aclk_m",
							  NULL, dspsys_regmap, 0x24, 6, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_X2X_X4_DSPSLV_DSP0_ACLK_M] = xuantie_gate_clk_register("clkgen_x2x_x4_dspslv_dsp0_aclk_m",
							  NULL, dspsys_regmap, 0x24, 7, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_AXI4_DSPSYS_SLV_ACLK] = xuantie_gate_clk_register("clkgen_axi4_dspsys_slv_aclk", NULL, dspsys_regmap,
							  0x24, 20, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_AXI4_DSPSYS_SLV_PCLK] = xuantie_gate_clk_register("clkgen_axi4_dspsys_slv_pclk", NULL, dspsys_regmap,
							  0x24, 21, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_AXI4_DSPSYS_ACLK] = xuantie_gate_clk_register("clkgen_axi4_dspsys_aclk", NULL, dspsys_regmap,
							  0x24, 23, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_AXI4_DSPSYS_PCLK] = xuantie_gate_clk_register("clkgen_axi4_dspsys_pclk", NULL, dspsys_regmap,
							  0x24, 24, GATE_NOT_SHARED, NULL, dev);
	if (tee_dspsys_regmap) {
		gates[CLKGEN_IOPMP_DSP1_PCLK] = xuantie_gate_clk_register("clkgen_iopmp_dsp1_pclk", NULL, tee_dspsys_regmap,
							  0x24, 25, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_IOPMP_DSP0_PCLK] = xuantie_gate_clk_register("clkgen_iopmp_dsp0_pclk", NULL, tee_dspsys_regmap,
							  0x24, 26, GATE_NOT_SHARED, NULL, dev);
	}

	clk_gate_data.clks = gates;
	clk_gate_data.clk_num = ARRAY_SIZE(gates);

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &clk_gate_data);
	if (ret < 0) {
		dev_err(dev, "failed to register gate clks for th1520 dspsys\n");
		goto unregister_clks;
	}

	dev_info(dev, "succeed to register dspsys gate clock provider\n");

	return 0;

unregister_clks:
	xuantie_unregister_clocks(gates, ARRAY_SIZE(gates));
	return ret;
}

static const struct of_device_id dspsys_clk_gate_of_match[] = {
	{ .compatible = "xuantie,dspsys-gate-controller" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, dspsys_clk_gate_of_match);

static struct platform_driver xuantie_dspsys_clk_driver = {
	.probe = xuantie_dspsys_clk_probe,
	.driver = {
		.name = "dspsys-clk-gate-provider",
		.of_match_table = of_match_ptr(dspsys_clk_gate_of_match),
	},
};

module_platform_driver(xuantie_dspsys_clk_driver);
MODULE_AUTHOR("wei.liu <lw312886@linux.alibaba.com>");
MODULE_DESCRIPTION("XuanTie Th1520 Fullmask dspsys clock gate provider");
MODULE_LICENSE("GPL v2");

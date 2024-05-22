/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Alibaba Group Holding Limited.
 */
#include <dt-bindings/clock/th1520-miscsys.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include "clk-gate.h"
#include "../clk.h"
static struct clk *gates[CLKGEN_MISCSYS_CLK_END];
static struct clk_onecell_data clk_gate_data;
static int thead_miscsys_clk_probe(struct platform_device *pdev)
{
	struct regmap *miscsys_regmap, *tee_miscsys_regmap = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	miscsys_regmap = syscon_regmap_lookup_by_phandle(np, "miscsys-regmap");
	if (IS_ERR(miscsys_regmap)) {
		dev_err(&pdev->dev, "cannot find regmap for misc system register\n");
		return PTR_ERR(miscsys_regmap);
	}
	tee_miscsys_regmap = syscon_regmap_lookup_by_phandle(np, "tee-miscsys-regmap");
	if (IS_ERR(tee_miscsys_regmap)) {
		dev_err(&pdev->dev, "cannot find regmap for tee misc system register\n");
		return PTR_ERR(tee_miscsys_regmap);
	}
	/* we assume that the gate clock is a root clock  */
	gates[CLKGEN_MISCSYS_MISCSYS_ACLK] = thead_gate_clk_register("clkgen_missys_aclk", NULL,
								miscsys_regmap, 0x100, 0, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_MISCSYS_USB3_DRD_CLK] = thead_gate_clk_register("clkgen_usb3_drd_clk", NULL,
								miscsys_regmap, 0x104, 0, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_MISCSYS_USB3_DRD_CTRL_REF_CLK] = thead_gate_clk_register("clkgen_usb3_drd_ctrl_ref_clk", "osc_24m",
								miscsys_regmap, 0x104, 1, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_MISCSYS_USB3_DRD_PHY_REF_CLK] = thead_gate_clk_register("clkgen_usb3_drd_phy_ref_clk", "osc_24m",
								miscsys_regmap, 0x104, 2, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_MISCSYS_USB3_DRD_SUSPEND_CLK] = thead_gate_clk_register("clkgen_usb3_drd_suspend_clk", NULL,
								miscsys_regmap, 0x104, 3, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_MISCSYS_EMMC_CLK] = thead_gate_clk_register("clkgen_emmc_clk", "osc_24m",
								miscsys_regmap, 0x108, 0, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_MISCSYS_SDIO0_CLK] = thead_gate_clk_register("clkgen_sdio0_clk", "osc_24m",
								miscsys_regmap, 0x10c, 0, GATE_NOT_SHARED, NULL, dev);
	gates[CLKGEN_MISCSYS_SDIO1_CLK] = thead_gate_clk_register("clkgen_sdio1_clk", "osc_24m",
								miscsys_regmap, 0x110, 0, GATE_NOT_SHARED, NULL, dev);
	if (tee_miscsys_regmap) {
		gates[CLKGEN_MISCSYS_AHB2_TEESYS_HCLK] = thead_gate_clk_register("clkgen_ahb2_teesys_hclk", NULL,
								tee_miscsys_regmap, 0x120, 0, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_APB3_TEESYS_HCLK] = thead_gate_clk_register("clkgen_apb3_teesys_hclk", NULL,
								tee_miscsys_regmap, 0x120, 1, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_AXI4_TEESYS_ACLK] = thead_gate_clk_register("clkgen_axi4_teesys_aclk", NULL,
								tee_miscsys_regmap, 0x120, 2, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_EIP120SI_CLK] = thead_gate_clk_register("clkgen_eip120si_clk", NULL,
								tee_miscsys_regmap, 0x120, 3, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_EIP120SII_CLK] = thead_gate_clk_register("clkgen_eip120sii_clk", NULL,
								tee_miscsys_regmap, 0x120, 4, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_EIP120SIII_CLK] = thead_gate_clk_register("clkgen_eip120siii_clk", NULL,
								tee_miscsys_regmap, 0x120, 5, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_TEEDMAC_CLK] = thead_gate_clk_register("clkgen_teedmac_clk", NULL,
								tee_miscsys_regmap, 0x120, 6, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_EIP150B_HCLK] = thead_gate_clk_register("clkgen_eip150b_hclk", NULL,
								tee_miscsys_regmap, 0x120, 7, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_OCRAM_HCLK] = thead_gate_clk_register("clkgen_ocram_hclk", NULL,
								tee_miscsys_regmap, 0x120, 8, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_EFUSE_PCLK] = thead_gate_clk_register("clkgen_efuse_pclk", NULL,
								tee_miscsys_regmap, 0x120, 9, GATE_NOT_SHARED, NULL, dev);
		gates[CLKGEN_MISCSYS_TEE_SYSREG_PCLK] = thead_gate_clk_register("clkgen_tee_sysreg_pclk", NULL,
								tee_miscsys_regmap, 0x120, 10, GATE_NOT_SHARED, NULL, dev);
	}
	clk_gate_data.clks = gates;
	clk_gate_data.clk_num = ARRAY_SIZE(gates);
	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &clk_gate_data);
	if (ret < 0) {
		dev_err(dev, "failed to register gate clks for th1520 miscsys\n");
		goto unregister_clks;
	}
	dev_info(dev, "succeed to register miscsys gate clock provider\n");
	return 0;
unregister_clks:
	thead_unregister_clocks(gates, ARRAY_SIZE(gates));
	return ret;
}
static const struct of_device_id miscsys_clk_gate_of_match[] = {
	{ .compatible = "thead,miscsys-gate-controller" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, miscsys_clk_gate_of_match);
static struct platform_driver thead_miscsys_clk_driver = {
	.probe = thead_miscsys_clk_probe,
	.driver = {
		.name = "miscsys-clk-gate-provider",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(miscsys_clk_gate_of_match),
	},
};
module_platform_driver(thead_miscsys_clk_driver);
MODULE_AUTHOR("wei.liu <lw312886@linux.alibaba.com>");
MODULE_AUTHOR("Esther.Z <Esther.Z@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead th1520 Fullmask miscsys clock gate provider");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Alibaba Group Holding Limited.
 */

#include <dt-bindings/clock/th1520-fm-ap-clock.h>
#include <dt-bindings/clock/th1520-vosys.h>
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
#include "../clk.h"

static struct clk *gates[TH1520_CLKGEN_VOSYS_CLK_END];
static struct clk_onecell_data clk_gate_data;

static int xuantie_vosys_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *gate_base;
	int ret;

	gate_base = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(gate_base)))
		return PTR_ERR(gate_base);

	/* we assume that the gate clock is a root clock  */
	gates[TH1520_CLKGEN_AXI4_VO_PCLK] = xuantie_clk_th1520_gate("clkgen_axi4_vo_pclk", NULL,
								gate_base + 0x50, 22);
	gates[TH1520_CLKGEN_IOPMP_VOSYS_DPU_PCLK] = xuantie_clk_th1520_gate("clkgen_iopmp_dpu_pclk", NULL,
								gate_base + 0x50, 23);
	gates[TH1520_CLKGEN_IOPMP_VOSYS_DPU1_PCLK] = xuantie_clk_th1520_gate("clkgen_iopmp_dpu1_pclk", NULL,
								gate_base + 0x50, 24);
	gates[TH1520_CLKGEN_IOPMP_VOSYS_GPU_PCLK] = xuantie_clk_th1520_gate("clkgen_iopmp_gpu_pclk", NULL,
								gate_base + 0x50, 25);
	gates[TH1520_CLKGEN_HDMI_PCLK] = xuantie_clk_th1520_gate("clkgen_hdmi_pclk", NULL, gate_base + 0x50, 11);
	gates[TH1520_CLKGEN_MIPIDSI0_PCLK] = xuantie_clk_th1520_gate("clkgen_mipidsi0_pclk", NULL,
								  gate_base + 0x50, 13);
	gates[TH1520_CLKGEN_MIPIDSI1_PCLK] = xuantie_clk_th1520_gate("clkgen_mipidsi1_pclk", NULL,
								  gate_base + 0x50, 14);
	gates[TH1520_CLKGEN_AXI4_VO_ACLK] = xuantie_clk_th1520_gate("clkgen_axi4_vo_aclk", NULL,
								  gate_base + 0x50, 0);
	gates[TH1520_CLKGEN_IOPMP_GPU_ACLK] = xuantie_clk_th1520_gate("clkgen_iopmp_gpu_aclk", NULL,
								  gate_base + 0x50, 29);
	gates[TH1520_CLKGEN_IOPMP_DPU_ACLK] = xuantie_clk_th1520_gate("clkgen_iopmp_dpu_aclk", NULL,
								  gate_base + 0x50, 28);
	gates[TH1520_CLKGEN_IOPMP_DPU1_ACLK] = xuantie_clk_th1520_gate("clkgen_iopmp_dpu1_aclk", NULL,
								  gate_base + 0x50, 27);
	gates[TH1520_CLKGEN_X2H_DPU_ACLK] = xuantie_clk_th1520_gate("clkgen_x2h_dpu_aclk", NULL, gate_base + 0x50, 21);
	gates[TH1520_CLKGEN_X2H_DPU1_ACLK] = xuantie_clk_th1520_gate("clkgen_x2h_dpu1_aclk", NULL, gate_base + 0x50, 20);
	gates[TH1520_CLKGEN_MIPIDSI0_PIXCLK] = xuantie_clk_th1520_gate("clkgen_mipidsi0_pixclk", NULL, gate_base + 0x50, 30);
	gates[TH1520_CLKGEN_HDMI_PIXCLK] = xuantie_clk_th1520_gate("clkgen_hdmi_pixclk", NULL, gate_base + 0x54, 0);
	gates[TH1520_CLKGEN_MIPIDSI1_PIXCLK] = xuantie_clk_th1520_gate("clkgen_mipidsi1_pixclk", NULL, gate_base + 0x50, 31);
	gates[TH1520_CLKGEN_HDMI_SFR_CLK] = xuantie_clk_th1520_gate("clkgen_hdmi_sfr_clk", NULL, gate_base + 0x50, 10);
	gates[TH1520_CLKGEN_HDMI_CEC_CLK] = xuantie_clk_th1520_gate("clkgen_hdmi_cec_cclk", NULL, gate_base + 0x50, 12);
	gates[TH1520_CLKGEN_HDMI_I2S_CLK] = xuantie_clk_th1520_gate("clkgen_hdmi_i2s_clk", NULL, gate_base + 0x50, 19);
	gates[TH1520_CLKGEN_MIPIDSI0_CFG_CLK] = xuantie_clk_th1520_gate("clkgen_mipidsi0_cfg_clk", NULL, gate_base + 0x50, 15);
	gates[TH1520_CLKGEN_MIPIDSI1_CFG_CLK] = xuantie_clk_th1520_gate("clkgen_mipidsi1_cfg_clk", NULL, gate_base + 0x50, 16);
	gates[TH1520_CLKGEN_MIPIDSI0_REFCLK] = xuantie_clk_th1520_gate("clkgen_mipidsi0_refclk", NULL, gate_base + 0x50, 17);
	gates[TH1520_CLKGEN_MIPIDSI1_REFCLK] = xuantie_clk_th1520_gate("clkgen_mipidsi1_refclk", NULL, gate_base + 0x50, 18);
	gates[TH1520_CLKGEN_GPU_CORE_CLK] = xuantie_clk_th1520_gate("clkgen_gpu_core_clk", "vosys_aclk_m", gate_base + 0x50, 3);
	gates[TH1520_CLKGEN_GPU_CFG_ACLK] = xuantie_clk_th1520_gate("clkgen_gpu_cfg_aclk", NULL, gate_base + 0x50, 4);
	gates[TH1520_CLKGEN_DPU_HCLK] = xuantie_clk_th1520_gate("clkgen_dpu_hclk", NULL, gate_base + 0x50, 7);
	gates[TH1520_CLKGEN_DPU_ACLK] = xuantie_clk_th1520_gate("clkgen_dpu_aclk", NULL, gate_base + 0x50, 8);
	gates[TH1520_CLKGEN_DPU_CCLK] = xuantie_clk_th1520_gate("clkgen_dpu_cclk", NULL, gate_base + 0x50, 9);
	gates[TH1520_CLKGEN_DPU_PIXCLK0] = xuantie_clk_th1520_gate("clkgen_dpu_pixclk0", NULL, gate_base + 0x50, 5);
	gates[TH1520_CLKGEN_DPU_PIXCLK1] = xuantie_clk_th1520_gate("clkgen_dpu_pixclk1", NULL, gate_base + 0x50, 6);

	clk_gate_data.clks = gates;
	clk_gate_data.clk_num = ARRAY_SIZE(gates);

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &clk_gate_data);
	if (ret < 0) {
		dev_err(dev, "failed to register gate clks for th1520 vosys\n");
		goto unregister_clks;
	}

	dev_info(dev, "succeed to register vosys gate clock provider\n");

	return 0;

unregister_clks:
	xuantie_unregister_clocks(gates, ARRAY_SIZE(gates));
	return ret;
}

static const struct of_device_id vosys_clk_gate_of_match[] = {
	{ .compatible = "xuantie,vosys-gate-controller" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, vosys_clk_gate_of_match);

static struct platform_driver xuantie_vosys_clk_driver = {
	.probe = xuantie_vosys_clk_probe,
	.driver = {
		.name = "vosys-clk-gate-provider",
		.of_match_table = of_match_ptr(vosys_clk_gate_of_match),
	},
};

module_platform_driver(xuantie_vosys_clk_driver);
MODULE_AUTHOR("wei.liu <lw312886@linux.alibaba.com>");
MODULE_DESCRIPTION("XuanTie Th1520 Fullmask vosys clock gate provider");
MODULE_LICENSE("GPL");

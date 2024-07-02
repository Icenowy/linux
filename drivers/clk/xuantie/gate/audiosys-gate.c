/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Alibaba Group Holding Limited.
 */

#include <dt-bindings/clock/th1520-fm-ap-clock.h>
#include <dt-bindings/clock/th1520-audiosys.h>
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

static struct clk *gates[TH1520_CLKGEN_AUDIO_CLK_END];
static struct clk_onecell_data clk_gate_data;

static int th1520_audiosys_clk_probe(struct platform_device *pdev)
{
	struct regmap *audiosys_regmap;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	audiosys_regmap = syscon_regmap_lookup_by_phandle(np, "audiosys-regmap");
	if (IS_ERR(audiosys_regmap)) {
		dev_err(&pdev->dev, "cannot find regmap for vi system register\n");
		return PTR_ERR(audiosys_regmap);
	}

	printk("%s audiosys_regmap=0x%px\n", __func__, audiosys_regmap);

	/* we assume that the gate clock is a root clock  */
	gates[TH1520_CLKGEN_AUDIO_CPU] = xuantie_gate_clk_register("clkgen_audiosys_cpu_clk", NULL,
									audiosys_regmap, 0x10, 0, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_SRAM0] = xuantie_gate_clk_register("clkgen_audiosys_sram0_clk", NULL,
									audiosys_regmap, 0x10, 1, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_SRAM1] = xuantie_gate_clk_register("clkgen_audiosys_sram1_clk", NULL,
									audiosys_regmap, 0x10, 2, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_DMA] = xuantie_gate_clk_register("clkgen_audiosys_dma_clk", NULL,
									audiosys_regmap, 0x10, 3, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_BSM] = xuantie_gate_clk_register("clkgen_audiosys_bsm_clk", NULL,
									audiosys_regmap, 0x10, 4, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_TIMER] = xuantie_gate_clk_register("clkgen_audiosys_timer_clk", NULL,
									audiosys_regmap, 0x10, 8, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_TIMER_CNT1] = xuantie_gate_clk_register("clkgen_audiosys_timer_cnt1_clk", NULL,
									audiosys_regmap, 0x10, 9, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_TIMER_CNT2] = xuantie_gate_clk_register("clkgen_audiosys_timer_cnt2_clk", NULL,
									audiosys_regmap, 0x10, 10, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_TIMER_CNT3] = xuantie_gate_clk_register("clkgen_audiosys_timer_cnt3_clk", NULL,
									audiosys_regmap, 0x10, 11, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_TIMER_CNT4] = xuantie_gate_clk_register("clkgen_audiosys_timer_cnt4_clk", NULL,
									audiosys_regmap, 0x10, 12, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_WDR] = xuantie_gate_clk_register("clkgen_audiosys_wdr_clk", NULL,
									audiosys_regmap, 0x10, 13, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_I2C0] = xuantie_gate_clk_register("clkgen_audiosys_i2c0_clk", NULL,
									audiosys_regmap, 0x10, 14, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_I2C1] = xuantie_gate_clk_register("clkgen_audiosys_i2c1_clk", NULL,
									audiosys_regmap, 0x10, 15, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_UART] = xuantie_gate_clk_register("clkgen_audiosys_uart_clk", NULL,
									audiosys_regmap, 0x10, 16, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_I2S0] = xuantie_gate_clk_register("clkgen_audiosys_i2s0_clk", NULL,
									audiosys_regmap, 0x10, 17, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_I2S1] = xuantie_gate_clk_register("clkgen_audiosys_i2s1_clk", NULL,
									audiosys_regmap, 0x10, 18, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_I2S2] = xuantie_gate_clk_register("clkgen_audiosys_i2s2_clk", NULL,
									audiosys_regmap, 0x10, 19, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_I2S8CH] = xuantie_gate_clk_register("clkgen_audiosys_i2s8ch_clk", NULL,
									audiosys_regmap, 0x10, 20, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_TDM] = xuantie_gate_clk_register("clkgen_audiosys_tdm_clk", NULL,
									audiosys_regmap, 0x10, 21, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_GPIO] = xuantie_gate_clk_register("clkgen_audiosys_gpio_clk", NULL,
									audiosys_regmap, 0x10, 22, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_SPDIF0] = xuantie_gate_clk_register("clkgen_audiosys_spdif0_clk", NULL,
									audiosys_regmap, 0x10, 23, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_SPDIF1] = xuantie_gate_clk_register("clkgen_audiosys_spdif1_clk", NULL,
									audiosys_regmap, 0x10, 24, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_VAD] = xuantie_gate_clk_register("clkgen_audiosys_vad_clk", NULL,
									audiosys_regmap, 0x10, 25, GATE_NOT_SHARED, NULL, dev);
	gates[TH1520_CLKGEN_AUDIO_IOMUX] = xuantie_gate_clk_register("clkgen_audiosys_iomux_clk", NULL,
									audiosys_regmap, 0x10, 26, GATE_NOT_SHARED, NULL, dev);

	clk_gate_data.clks = gates;
	clk_gate_data.clk_num = ARRAY_SIZE(gates);

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &clk_gate_data);
	if (ret < 0) {
		dev_err(dev, "failed to register gate clks for th1520 audiosys\n");
		goto unregister_clks;
	}

	dev_info(dev, "succeed to register audiosys gate clock provider\n");

	return 0;

unregister_clks:
	xuantie_unregister_clocks(gates, ARRAY_SIZE(gates));
	return ret;
}

static const struct of_device_id audiosys_clk_gate_of_match[] = {
	{ .compatible = "xuantie,audiosys-gate-controller" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, audiosys_clk_gate_of_match);

static struct platform_driver th1520_audiosys_clk_driver = {
	.probe = th1520_audiosys_clk_probe,
	.driver = {
		.name = "audiosys-clk-gate-provider",
		.of_match_table = of_match_ptr(audiosys_clk_gate_of_match),
	},
};

module_platform_driver(th1520_audiosys_clk_driver);
MODULE_AUTHOR("nanli.yd <nanli.yd@linux.alibaba.com>");
MODULE_DESCRIPTION("XuanTie Th1520 Fullmask audiosys clock gate provider");
MODULE_LICENSE("GPL v2");

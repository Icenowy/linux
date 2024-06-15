// SPDX-License-Identifier: GPL-2.0-only
/*
 * XuanTie TH1520 System Reset Controller driver
 *
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 *
 * Author: zenglinghui.zlh <zenglinghui.zlh@linux.alibaba.com>
 *
 */

#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/regmap.h>
#include <dt-bindings/reset/thead,th1520-reset.h>

struct th1520_rst_signal {
	unsigned int offset, bit;
};

struct th1520_rst_variant {
	const struct th1520_rst_signal *signals;
	unsigned int signals_num;
};

struct th1520_rst {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
	const struct th1520_rst_signal *signals;
};

enum th1520_rst_registers {
	RST_WDT0 = 0x0034,
	RST_WDT1 = 0x0038,
	RST_I2S = 0x01ac,
	RST_NPU = 0x01b0,
};

enum th1520_vpsys_rst_registers {
	RST_FCE = 0x0004,
};

enum th1520_audiosys_src_registers {
	RST_AUD_I2S	= 0x0014,
};

static int th1520_reset_update(struct th1520_rst *rst, unsigned long id,
			       unsigned int value)
{
	const struct th1520_rst_signal *signal = &rst->signals[id];

	return regmap_update_bits(rst->regmap, signal->offset, signal->bit,
				  value);
}

static const struct th1520_rst_signal th1520_rst_signals[] = {
	[TH1520_RESET_WDT0] = { RST_WDT0, BIT(0) },
	[TH1520_RESET_WDT1] = { RST_WDT1, BIT(0) },
	[TH1520_RESET_HDMI_I2S] = { RST_I2S, BIT(0) },
	[TH1520_RESET_NPU] = { RST_NPU, BIT(0) },
};

static const struct th1520_rst_signal th1520_vpsys_rst_signals[] = {
	[TH1520_RESET_FCE] = { RST_FCE, BIT(0)|BIT(1)|BIT(4)|BIT(5) },
};

static const struct th1520_rst_signal th1520_audiosys_rst_signals[] = {
	[TH1520_RESET_AUD_I2S0] = { RST_AUD_I2S, BIT(17) },
	[TH1520_RESET_AUD_I2S1] = { RST_AUD_I2S, BIT(18) },
	[TH1520_RESET_AUD_I2S2] = { RST_AUD_I2S, BIT(19) },
	[TH1520_RESET_AUD_I2S8CH] = { RST_AUD_I2S, BIT(20) },
	[TH1520_RESET_AUD_TDM] = { RST_AUD_I2S, BIT(21) },
	[TH1520_RESET_AUD_SPDIF0] = { RST_AUD_I2S, BIT(23) },
	[TH1520_RESET_AUD_SPDIF1] = { RST_AUD_I2S, BIT(24) },
};

static struct th1520_rst *to_th1520_rst(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct th1520_rst, rcdev);
}

static int th1520_reset_set(struct reset_controller_dev *rcdev,
			    unsigned long id, bool assert)
{
	struct th1520_rst *rst = to_th1520_rst(rcdev);
	const unsigned int bit = rst->signals[id].bit;
	unsigned int value = assert ? bit : 0;

	return th1520_reset_update(rst, id, value);
}

static int th1520_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return th1520_reset_set(rcdev, id, false);
}

static int th1520_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return th1520_reset_set(rcdev, id, true);
}

static const struct reset_control_ops th1520_rst_ops = {
	.assert = th1520_reset_assert,
	.deassert = th1520_reset_deassert,
};

static const struct th1520_rst_variant variant_th1520 = {
	.signals = th1520_rst_signals,
	.signals_num = ARRAY_SIZE(th1520_rst_signals),
};

static const struct th1520_rst_variant variant_th1520_vpsys = {
	.signals = th1520_vpsys_rst_signals,
	.signals_num = ARRAY_SIZE(th1520_vpsys_rst_signals),
};

static const struct th1520_rst_variant variant_th1520_audiosys = {
	.signals = th1520_audiosys_rst_signals,
	.signals_num = ARRAY_SIZE(th1520_audiosys_rst_signals),
};

static int th1520_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct th1520_rst *rst;
	struct regmap_config config = { .name = "rst" };
	const struct th1520_rst_variant *variant = of_device_get_match_data(dev);

	rst = devm_kzalloc(dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	rst->signals = variant->signals;
	rst->regmap = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(rst->regmap))
		return PTR_ERR(rst->regmap);

	regmap_attach_dev(dev, rst->regmap, &config);

	rst->rcdev.owner = THIS_MODULE;
	rst->rcdev.dev = dev;
	rst->rcdev.of_node = dev->of_node;
	rst->rcdev.ops = &th1520_rst_ops;
	rst->rcdev.nr_resets = variant->signals_num;

	return devm_reset_controller_register(dev, &rst->rcdev);
}

static const struct of_device_id th1520_reset_dt_ids[] = {
	{ .compatible = "xuantie,th1520-reset", .data = &variant_th1520 },
	{ .compatible = "xuantie,th1520-vpsys-reset", .data = &variant_th1520_vpsys },
	{ .compatible = "xuantie,th1520-audiosys-reset", .data = &variant_th1520_audiosys },
	{ /* sentinel */ },
};

static struct platform_driver th1520_reset_driver = {
	.probe	= th1520_reset_probe,
	.driver = {
		.name		= "th1520-reset",
		.of_match_table	= th1520_reset_dt_ids,
	},
};
builtin_platform_driver(th1520_reset_driver);

MODULE_AUTHOR("zenglinghui.zlh <zenglinghui.zlh@linux.alibaba.com>");
MODULE_DESCRIPTION("XuanTie th1520 reset driver");
MODULE_LICENSE("GPL v2");

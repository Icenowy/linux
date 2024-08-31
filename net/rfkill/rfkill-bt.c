// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2022 Alibaba Group Holding Limited.
 * Copyright (C) 2021-2022 Jiandong Yu <yjd01941629@alibaba-inc.com>
 */

#include <linux/rfkill.h>
#include <linux/delay.h>
#include <dt-bindings/gpio/gpio.h>
#include "../../drivers/gpio/gpiolib-of.h"
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif

#if 0
#define DBG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)
#else
#define DBG(x...)
#endif

#define LOG(x...)   printk(KERN_INFO "[BT_RFKILL]: "x)

struct rfkill_bt_data {
	struct rfkill_platform_data	*pdata;
	struct platform_device      *pdev;
	struct rfkill				*rfkill_dev;
};

#define NAME_SIZE   64
struct ctrl_gpio {
	int     io;
	char    name[NAME_SIZE];
	int     enable;
};

struct rfkill_platform_data {
	char                 *name;
	enum rfkill_type     type;
	struct ctrl_gpio   power_n;
};

static struct rfkill_bt_data *g_rfkill = NULL;

static const char bt_name[] = "bt";

static int rfkill_setup_gpio(struct platform_device *pdev, struct ctrl_gpio *gpio,
							const char *prefix, const char *name)
{
	if (gpio_is_valid(gpio->io)) {
		int ret = 0;

		sprintf(gpio->name, "%s_%s", prefix, name);
		ret = devm_gpio_request(&pdev->dev, gpio->io, gpio->name);
		if (ret) {
			LOG("Failed to get %s gpio.\n", gpio->name);
			return -1;
		}
	}

	return 0;
}

static int rfkill_set_power(void *data, bool blocked)
{
	struct rfkill_bt_data *rfkill = data;
	struct ctrl_gpio *poweron = &rfkill->pdata->power_n;

	DBG("blocked state:%d\n", blocked);

	if (!blocked) {
		if (gpio_is_valid(poweron->io)) {
			gpio_direction_output(poweron->io, !poweron->enable);
			msleep(20);
			gpio_direction_output(poweron->io, poweron->enable);
			msleep(20);
			}
		LOG("Turn on the power of the BT module\n");
	} else {
		if (gpio_is_valid(poweron->io)) {
			gpio_direction_output(poweron->io, !poweron->enable);
			msleep(20);
		}

		LOG("Turn off the power of the BT module\n");
	}

	return 0;
}

static const struct rfkill_ops rfkill_ops = {
	.set_block = rfkill_set_power,
};

#ifdef CONFIG_OF
static int bluetooth_platdata_parse_dt(struct device *dev, struct rfkill_platform_data *data)
{
	struct device_node *node = dev->of_node;
	struct gpio_desc *desc;
	int gpio;
	unsigned long flags;

	if (!node)
		return -ENODEV;

	memset(data, 0, sizeof(*data));

	gpio = -EINVAL;
	desc = of_find_gpio(node, "BT,power", 0, &flags);
	if (!IS_ERR(desc))
		gpio = desc_to_gpio(desc);
	LOG("%s: BT,power-gpios = %d\n", __func__, gpio);
	if (gpio_is_valid(gpio)) {
		data->power_n.io = gpio;
		data->power_n.enable = (flags == GPIO_ACTIVE_HIGH) ? 1 : 0;
		LOG("%s: get property: BT,power-gpios = %d.\n", __func__, gpio);
	} else {
		data->power_n.io = -1;
	}

	return 0;
}
#endif //CONFIG_OF

static int rfkill_probe(struct platform_device *pdev)
{
	struct rfkill_bt_data *rfkill;
	struct rfkill_platform_data *pdata = pdev->dev.platform_data;
	int ret = 0;

	if (!pdata) {
	#ifdef CONFIG_OF
		pdata = devm_kzalloc(&pdev->dev, sizeof(struct rfkill_platform_data), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = bluetooth_platdata_parse_dt(&pdev->dev, pdata);
		if (ret < 0) {
	#endif
			LOG("%s: Invalid platform data\n", __func__);
			return ret;
	#ifdef CONFIG_OF
		}
	#endif
	}

	pdata->name = (char *)bt_name;
	pdata->type = RFKILL_TYPE_BLUETOOTH;

	rfkill = devm_kzalloc(&pdev->dev, sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		return -ENOMEM;

	rfkill->pdata = pdata;
	rfkill->pdev = pdev;
	g_rfkill = rfkill;

	DBG("setup gpio\n");

	ret = rfkill_setup_gpio(pdev, &pdata->power_n, pdata->name, "bt_power_en");
	if (ret)
		goto fail_gpio;

	DBG("alloc rfkill\n");
	rfkill->rfkill_dev = rfkill_alloc(pdata->name, &pdev->dev, pdata->type,
									&rfkill_ops, rfkill);
	if (!rfkill->rfkill_dev)
		goto fail_alloc;

	rfkill_set_states(rfkill->rfkill_dev, true, false);
	ret = rfkill_register(rfkill->rfkill_dev);
	if (ret < 0)
		goto fail_rfkill;

	// bt turn off power
	if (gpio_is_valid(pdata->power_n.io))
		gpio_direction_output(pdata->power_n.io, !pdata->power_n.enable);

	platform_set_drvdata(pdev, rfkill);

	LOG("%s device registered.\n", pdata->name);

	return 0;

fail_rfkill:
fail_alloc:
	rfkill_destroy(rfkill->rfkill_dev);

fail_gpio:
	g_rfkill = NULL;

	return ret;
}

static int rfkill_remove(struct platform_device *pdev)
{
	struct rfkill_bt_data *rfkill = platform_get_drvdata(pdev);

	rfkill_unregister(rfkill->rfkill_dev);
	rfkill_destroy(rfkill->rfkill_dev);

	// free gpio
	if (gpio_is_valid(rfkill->pdata->power_n.io))
		gpio_free(rfkill->pdata->power_n.io);

	g_rfkill = NULL;

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id bt_platdata_of_match[] = {
	{ .compatible = "bluetooth-platdata" },
	{ }
};
MODULE_DEVICE_TABLE(of, bt_platdata_of_match);
#endif //CONFIG_OF

static struct platform_driver rfkill_driver = {
	.probe = rfkill_probe,
	.remove = rfkill_remove,
	.driver = {
		.name = "rfkill_bt",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(bt_platdata_of_match),
	},
};

static int __init rfkill_init(void)
{
	return platform_driver_register(&rfkill_driver);
}

static void __exit rfkill_exit(void)
{
	platform_driver_unregister(&rfkill_driver);
}

module_init(rfkill_init);
module_exit(rfkill_exit);

MODULE_DESCRIPTION("rfkill for Bluetooth");
MODULE_LICENSE("GPL");

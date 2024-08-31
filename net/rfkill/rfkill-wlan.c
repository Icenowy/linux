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

#define LOG(x...)   printk(KERN_INFO "[WLAN_RFKILL]: "x)

struct rfkill_wlan_data {
	struct wifi_moudle_gpios *pdata;
};

struct ctrl_gpio {
	int     io; //The number of GPIO
	char    name[64];
	int     enable; //Effective level of GPIO
};

struct wifi_moudle_gpios {
	bool power_remain;
	struct ctrl_gpio   power_n; //PMU_EN
};

static struct rfkill_wlan_data *g_rfkill = NULL;
static int power_on_time = 0;
static int power_on_when_resume = 0; //Used to control whether power switch control is supported.

static const char wlan_name[] =  "wifi";

int wifi_power(int on)
{
	struct rfkill_wlan_data *rfkill = g_rfkill;
	struct ctrl_gpio *power_on;

	LOG("%s: %d\n", __func__, on);

	if (!rfkill) {
		LOG("%s: rfkill_wlan_data initialization failed\n", __func__);
		return -1;
	}

	if (rfkill->pdata->power_remain && power_on_time) {
		LOG("%s: The power supply of the WiFi module is set to normally open. Skip power control", __func__);
		return 0;
	} else {
		power_on_time++;
	}

	power_on = &rfkill->pdata->power_n;

	if (on) {
		if (gpio_is_valid(power_on->io)) {
			gpio_direction_output(power_on->io, power_on->enable);
			msleep(500);
		}

		LOG("Turn on the power of the WiFi module. %d\n", power_on->io);
	} else {
		if (gpio_is_valid(power_on->io)) {
			gpio_direction_output(power_on->io, !(power_on->enable));
			msleep(100);
		}

		LOG("Turn off the power of the WiFi module.\n");
	}

	return 0;
}

static int rfkill_setup_gpio(struct ctrl_gpio *gpio, const char *prefix, const char *name)
{
	if (gpio_is_valid(gpio->io)) {
		int ret = 0;

		sprintf(gpio->name, "%s_%s", prefix, name);
		ret = gpio_request(gpio->io, gpio->name);
		if (ret) {
			LOG("Failed to get %s gpio.\n", gpio->name);
			return -1;
		}
	}

	return 0;
}

#ifdef CONFIG_OF
static int wlan_platdata_parse_dt(struct device *dev, struct wifi_moudle_gpios *data)
{
	struct device_node *node = dev->of_node;
	struct gpio_desc *desc;
	int gpio;
	unsigned long flags;

	if (!node)
		return -ENODEV;

	memset(data, 0, sizeof(*data));

	if (of_find_property(node, "power_on_after_init", NULL)) {
		data->power_remain = true;
		LOG("%s: WiFi module maintains power on normally after initialization.\n", __func__);
	} else {
		data->power_remain = false;
		LOG("%s: Enable power control of WiFi modules.\n", __func__);
	}

	gpio = -EINVAL;
	desc = of_find_gpio(node, "WIFI,poweren", 0, &flags);
	if (!IS_ERR(desc))
		gpio = desc_to_gpio(desc);
	LOG("%s: The power of the WiFi module is controlled by GPIO.\n", __func__);
	if (gpio_is_valid(gpio)) {
		data->power_n.io = gpio;
		data->power_n.enable = (flags == GPIO_ACTIVE_HIGH) ? 1 : 0;
		LOG("%s: get property: WIFI,poweren-gpios = %d, flags = %d.\n", __func__, gpio, flags);
	} else {
		data->power_n.io = -1;
	}

	if (of_find_property(node, "power_on_when_resume", NULL)) {
		power_on_when_resume = true;
		LOG("%s: Turn off the power during suspension and turn it on when resuming, power_on_when_resume = %d.\n", __func__, power_on_when_resume);
	} else {
		power_on_when_resume = false;
		LOG("%s: power is only turned on during initialization and is not controlled afterwards, power_on_when_resume = %d.\n", __func__, power_on_when_resume);
	}

	return 0;
}
#endif //CONFIG_OF

static int rfkill_wlan_probe(struct platform_device *pdev)
{
	struct rfkill_wlan_data *rfkill;
	struct wifi_moudle_gpios *pdata = pdev->dev.platform_data;
	int ret = -1;

	if (!pdata) {
#ifdef CONFIG_OF
		pdata = kzalloc(sizeof(struct wifi_moudle_gpios), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = wlan_platdata_parse_dt(&pdev->dev, pdata);
		if (ret < 0) {
#endif
			LOG("%s: Invalid platform data\n", __func__);
			return ret;
#ifdef CONFIG_OF
		}
#endif
	}

	rfkill = kzalloc(sizeof(*rfkill), GFP_KERNEL);
	if (!rfkill)
		goto rfkill_alloc_fail;

	rfkill->pdata = pdata;
	g_rfkill = rfkill;

	LOG("%s: setup gpio\n", __func__);

	ret = rfkill_setup_gpio(&pdata->power_n, wlan_name, "wlan_power_en");
	if (ret)
		goto fail_alloc;

	// Turn off wifi power as default
	if (gpio_is_valid(pdata->power_n.io))
		gpio_direction_output(pdata->power_n.io, !pdata->power_n.enable);

	if (pdata->power_remain)
		wifi_power(1);

	return 0;

fail_alloc:
	kfree(rfkill);
rfkill_alloc_fail:
	kfree(pdata);

	g_rfkill = NULL;

	return ret;
}

static int rfkill_wlan_remove(struct platform_device *pdev)
{
	struct rfkill_wlan_data *rfkill = platform_get_drvdata(pdev);

	if (gpio_is_valid(rfkill->pdata->power_n.io))
		gpio_free(rfkill->pdata->power_n.io);

	kfree(rfkill);
	g_rfkill = NULL;

	return 0;
}

static int rfkill_wlan_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rfkill_wlan_data *rfkill = g_rfkill;
	struct ctrl_gpio *power_on;

	power_on = &rfkill->pdata->power_n;

	// turn off the wifi's power
	if (power_on_when_resume) {
		if (gpio_is_valid(power_on->io)) {
			gpio_set_value(power_on->io, !(power_on->enable));
			msleep(100);
		}

		LOG("Turn off the power of the WiFi module.\n");
	}

	return 0;
}

static int rfkill_wlan_resume(struct platform_device *pdev)
{
	struct rfkill_wlan_data *rfkill = g_rfkill;
	struct ctrl_gpio *power_on;

	power_on = &rfkill->pdata->power_n;

	// turn on the wifi's power
	if (power_on_when_resume) {
		if (gpio_is_valid(power_on->io)) {
			gpio_set_value(power_on->io, power_on->enable);
			msleep(500);
		}

		LOG("Turn on the power of the WiFi module. %d\n", power_on->io);
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id wlan_platdata_of_match[] = {
	{ .compatible = "wlan-platdata" },
	{ }
};
MODULE_DEVICE_TABLE(of, wlan_platdata_of_match);
#endif //CONFIG_OF

static struct platform_driver rfkill_wlan_driver = {
	.probe = rfkill_wlan_probe,
	.remove = rfkill_wlan_remove,
	.suspend = rfkill_wlan_suspend,
	.resume = rfkill_wlan_resume,
	.driver = {
		.name = "wlan-platdata",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(wlan_platdata_of_match),
	},
};

static int __init rfkill_wlan_init(void)
{
	return platform_driver_register(&rfkill_wlan_driver);
}

static void __exit rfkill_wlan_exit(void)
{
	platform_driver_unregister(&rfkill_wlan_driver);
}

module_init(rfkill_wlan_init);
module_exit(rfkill_wlan_exit);

MODULE_DESCRIPTION("rfkill for wifi");
MODULE_LICENSE("GPL");

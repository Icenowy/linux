#include <linux/module.h> 
#include <linux/init.h>
#include <linux/gpio.h>	
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/kobject.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/input.h>
#include <linux/syscore_ops.h>
#include "hc32fx_mcu.h"

#if 1
#define DBG(x...) printk(x)
#else
#define DBG(x...) do { } while (0)
#endif
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct i2c_client *g_client;

struct hc32fx {
	int	gpio_int;
	struct regmap *regmap;
	struct input_dev *input;
};

int debug_array[] = {
	HC32FX_MCUVERSION_00,
	HC32FX_MCUSTATUS_10,
	HC32FX_SYSPOWERCONTROL_20,
	HC32FX_MCUUPDATESTATUS_E0,
	HC32FX_BOOTLOADERMODE_F1,
};

static int debug_mode(void)
{
	u32 rbuf;
	int i,ret;
	struct hc32fx *hc32fx = i2c_get_clientdata(g_client);

	for(i=0; i<ARRAY_SIZE(debug_array); i++)
	{
		ret = regmap_read(hc32fx->regmap, debug_array[i], &rbuf);
		if(ret < 0)
			printk("read reg:0x%x fail \n", debug_array[i]);
		else 
			printk("read reg:0x%x val:0x%x \n", debug_array[i], rbuf);
	}

	return 0;
}

static ssize_t debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{	

	if (!strncmp(buf, "debug", 5)) {
		debug_mode();
	}

	return count;
}
static DEVICE_ATTR_WO(debug);

static struct attribute *debug_attrs[] = {
	&dev_attr_debug.attr,
	NULL,
};

static struct attribute_group debug_attr_group = {
	.attrs = debug_attrs,
};

static void hc32fx_shutdown(struct i2c_client *client)
{
	return;
}

static int hc32fx_suspend(struct device *dev)
{
	return 0;
}

static  int hc32fx_resume(struct device *dev)
{
	return 0;
}

static int hc32fx_parse_dt(struct hc32fx *hc32fx)
{
	return 0;
}

static int hc32fx_init(struct hc32fx *hc32fx)
{
	// poweron, reset...
	return 0;
}

static bool hc32fx_readable(struct device *dev, unsigned int reg)
{
	if (reg <= HC32FX_MAXREG_FF)
		return true;
	else
		return false;
}

static bool hc32fx_volatile(struct device *dev, unsigned int reg)
{
	if (reg <= HC32FX_MAXREG_FF)
		return true;
	else
		return false;
}

static const struct regmap_config hc32fx_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= HC32FX_MAXREG_FF,
	.cache_type	= REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
	.volatile_reg = hc32fx_volatile,
	.readable_reg = hc32fx_readable,
};

static irqreturn_t hc32fx_irq_handler_thread(int irq, void *private)
{
	DBG("%s %d \n", __func__, __LINE__);
	struct hc32fx *hc32fx = (struct hc32fx *)private;

	input_report_key(hc32fx->input, KEY_POWER, 1);
	input_sync(hc32fx->input);

	input_report_key(hc32fx->input, KEY_POWER, 0);
	input_sync(hc32fx->input);
	
	return IRQ_HANDLED;
}

static void TH1520_syscore_shutdown(void)
{
	int ret;
	struct hc32fx *hc32fx = i2c_get_clientdata(g_client);

	if (system_state == SYSTEM_POWER_OFF) {
		printk("power off %s\n", __func__);
		ret = regmap_write(hc32fx->regmap, HC32FX_SYSPOWERCONTROL_20, POWER_OFF);
		if (ret) {
			printk(" @@@@@@@@@@@@ fail power off %s %d \n",
			       __func__, __LINE__);
		}
		mdelay(10);

		while (1)
		;
	}
}

static struct syscore_ops TH1520_syscore_ops = {
	.shutdown = TH1520_syscore_shutdown,
};

static int hc32fx_probe(struct i2c_client *client)
{
	int ret;
	struct hc32fx *hc32fx;

	DBG("%s %d start\n", __func__, __LINE__);

	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_I2C\n");
		return -EIO;
	}

	hc32fx = devm_kzalloc(&client->dev, sizeof(struct hc32fx), GFP_KERNEL);
	if (!hc32fx) {
		dev_err(&client->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	hc32fx->regmap = devm_regmap_init_i2c(client, &hc32fx_regmap_config);
	if (IS_ERR(hc32fx->regmap))
		return PTR_ERR(hc32fx->regmap);

	i2c_set_clientdata(client, hc32fx);
	g_client = client;

	hc32fx->input = devm_input_allocate_device(&client->dev);
	if (!hc32fx->input) {
		dev_err(&client->dev, "Can't allocate power button\n");
		return -ENOMEM;
	}

	hc32fx->input->name = "TH1520 pwrkey";
	hc32fx->input->phys = "TH1520_pwrkey/input0";
	hc32fx->input->id.bustype = BUS_HOST;
	input_set_capability(hc32fx->input, EV_KEY, KEY_POWER);

	ret = input_register_device(hc32fx->input);
	if (ret) {
		dev_err(&client->dev, "Can't register power button: %d\n", ret);
		return ret;
	}

	ret = hc32fx_parse_dt(hc32fx);
	if (ret < 0) {
		dev_err(&client->dev, "[mcu] parse dt failed!\n");
		return ret;
	}

	ret = hc32fx_init(hc32fx);
	if (ret < 0) {
		dev_err(&client->dev, "[mcu] init failed!\n");
		return ret;
	}

	ret = sysfs_create_group(&client->dev.kobj, &debug_attr_group);
	if(ret) {
		dev_warn(&client->dev, "attr group create failed\n");
	}

	if (client->irq < 0) {
		dev_err(&client->dev, "No irq resource found.\n");
		return client->irq;
	}

	ret = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					hc32fx_irq_handler_thread,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "hc32fx_irq",
					hc32fx);
	if (ret) {
		dev_err(&client->dev, "request irq failed\n");
		goto irq_fail;
	}
	enable_irq_wake(client->irq);

	register_syscore_ops(&TH1520_syscore_ops);
	DBG("%s %d end\n", __func__, __LINE__);
	return 0;
irq_fail:
	sysfs_remove_group(&client->dev.kobj, &debug_attr_group);
	return ret;
}

static struct of_device_id hc32fx_of_match[] = {
	{.compatible = "hc32fx-mcu"},
	{}
}
MODULE_DEVICE_TABLE(of, hc32fx_of_match);

static const struct i2c_device_id hc32fx_id[] = {
	{ "hc32fx", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hc32fx_id);

static const struct dev_pm_ops hc32fx_pm_ops = {
	.suspend = hc32fx_suspend,
	.resume = hc32fx_resume,
};

static struct i2c_driver hc32fx_i2c_driver = {
	.driver = {
		.name = "hc32fx_driver",
		.pm = &hc32fx_pm_ops,
		.of_match_table = of_match_ptr(hc32fx_of_match),
	},
	.probe = hc32fx_probe,
	.shutdown = hc32fx_shutdown,
	.id_table = hc32fx_id,
};

module_i2c_driver(hc32fx_i2c_driver);
MODULE_AUTHOR("lin_jiayong@techvision.com.cn");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("driver for HC32F005C6UA MCU");

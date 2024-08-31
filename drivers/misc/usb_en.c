#include <dt-bindings/gpio/gpio.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>

struct usb_priv {
	unsigned int usbhost_en;
	unsigned int usbhub_en;
	unsigned int usb_sw;
};
struct usb_priv *priv;

static struct of_device_id usb_en_of_match[] = {
	{ .compatible = "usb_en" },
	{ }
};

MODULE_DEVICE_TABLE(of, usb_en_of_match);

static int usb_en_probe(struct platform_device *pdev)
{
	int ret=-1;
	struct device_node *node = pdev->dev.of_node;

	struct usb_priv *usb_priv = devm_kzalloc(&pdev->dev, sizeof(*usb_priv), GFP_KERNEL);
	if (!usb_priv) {
		printk("%s failed to allocate driver data\n", __func__);
		return -ENOMEM;
	}
	priv = usb_priv;

	ret = of_get_named_gpio_flags(node, "host-en-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can not read property host-en-gpio\n");
	} else {
		usb_priv->usbhost_en = ret;
		ret = devm_gpio_request(&pdev->dev, usb_priv->usbhost_en,
					"usbhost_en");
		if (ret < 0) {
			dev_err(&pdev->dev, "usbhost_en request fail\n");
		} else {
			gpio_direction_output(usb_priv->usbhost_en, 1);
		}
	}

	ret = of_get_named_gpio_flags(node, "hub-en-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can not read property hub-en-gpio\n");
	} else {
		usb_priv->usbhub_en = ret;
		ret = devm_gpio_request(&pdev->dev, usb_priv->usbhub_en,
					"usbhub_en");
		if (ret < 0) {
			dev_err(&pdev->dev, "usbhub_en request fail\n");
		} else {
			gpio_direction_output(usb_priv->usbhub_en, 1);
		}
	}

	ret = of_get_named_gpio_flags(node, "usb_sw-gpio", 0, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can not read property usb_sw-gpio\n");
	} else {
		usb_priv->usb_sw = ret;
		ret = devm_gpio_request(&pdev->dev, usb_priv->usb_sw,
					"usb_sw");
		if (ret < 0) {
			dev_err(&pdev->dev, "usb_sw request fail\n");
		} else {
			gpio_direction_output(usb_priv->usb_sw, 0);
		}
	}

    return 0;
}

static int usb_en_remove(struct platform_device *pdev)
{
    printk("func: _________%s\n", __func__); 
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int usb_en_suspend(struct device *dev)
{
    printk("func: _________%s\n", __func__); 
	return 0;
}

static int usb_en_resume(struct device *dev)
{
    printk("func: _________%s\n", __func__); 
	return 0;
}
#endif

static const struct dev_pm_ops usb_en_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = usb_en_suspend,
	.resume = usb_en_resume,
	.poweroff = usb_en_suspend,
	.restore = usb_en_resume,
#endif
};

static struct platform_driver usb_en_driver = {
	.driver		= {
		.name		= "usb_en",
		.owner		= THIS_MODULE,
		.pm		= &usb_en_pm_ops,
		.of_match_table	= of_match_ptr(usb_en_of_match),
	},
	.probe		= usb_en_probe,
	.remove		= usb_en_remove,
};

module_platform_driver(usb_en_driver);
MODULE_DESCRIPTION("usb power control Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:usb_en");

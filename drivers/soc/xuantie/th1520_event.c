// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/firmware/xuantie/ipc.h>
#include <linux/firmware/xuantie/th1520_event.h>

/*
 * AON SRAM total size is 0x10000, reserve 0x100 for event.
 * Notice: c902 *.ld also need resize.
 * -------------- 0xff_ffef8000
 * |		|
 * |		|
 * |		|
 * |  c902	|
 * |		|
 * |		|
 * |		|
 * -------------- 0xff_fff07f00
 * |   reserve	|
 * |		|
 * --------------
 */
#define TH1520_AON_SRAM_LEN	0x10000
#define TH1520_AON_SRAM_RESERV	(TH1520_AON_SRAM_LEN - 0x100)
#define TH1520_EVENT_OFFSET	(TH1520_AON_SRAM_RESERV + 0x10)
#define TH1520_EVENT_CHECK	(TH1520_EVENT_OFFSET + 0x4)

#define TH1520_EVENT_MAGIC	0x5A5A5A5A

struct th1520_aon_msg_event_ctrl {
	struct th1520_aon_rpc_msg_hdr hdr;
	u32 reserve_offset;
	u32 reserved[5];
} __packed __aligned(1);

struct th1520_event {
	struct device *dev;

	struct th1520_aon_ipc *ipc_handle;
	struct th1520_aon_msg_event_ctrl msg;

	struct regmap *aon_iram;
	bool init;
};

struct th1520_event *th1520_event;

static void th1520_event_msg_hdr_fill(struct th1520_aon_rpc_msg_hdr *hdr, enum th1520_aon_sys_func func)
{
	hdr->svc = (uint8_t)TH1520_AON_RPC_SVC_SYS;
	hdr->func = (uint8_t)func;
	hdr->size = TH1520_AON_RPC_MSG_NUM;
}

static int th1520_event_aon_reservemem(struct th1520_event *event)
{
	struct th1520_aon_ipc *ipc = event->ipc_handle;
	struct th1520_aon_rpc_ack_common ack_msg;
	int ret = 0;

	dev_dbg(event->dev, "aon reservemem...\n");

	th1520_event_msg_hdr_fill(&event->msg.hdr, TH1520_AON_SYS_FUNC_AON_RESERVE_MEM);

	RPC_SET_BE32(&event->msg.reserve_offset, 0, TH1520_EVENT_OFFSET);

	ret = th1520_aon_call_rpc(ipc, &event->msg, &ack_msg, true);
	if (ret)
		dev_err(event->dev, "failed to set aon reservemem\n");

	return ret;
}

int th1520_event_set_rebootmode(enum th1520_rebootmode_index mode)
{
	int ret;

	if (!th1520_event || !th1520_event->init)
		return -EINVAL;

	ret = regmap_write(th1520_event->aon_iram, TH1520_EVENT_OFFSET, mode);
	if (ret) {
		dev_err(th1520_event->dev, "set rebootmode failed,ret:%d\n", ret);
		return ret;
	}

	dev_info(th1520_event->dev, "set rebootmode:0x%x\n", mode);

	return 0;
}
EXPORT_SYMBOL_GPL(th1520_event_set_rebootmode);

int th1520_event_get_rebootmode(enum th1520_rebootmode_index *mode)
{
	int ret;

	if (!th1520_event || !th1520_event->init)
		return -EINVAL;

	ret = regmap_read(th1520_event->aon_iram, TH1520_EVENT_OFFSET, mode);
	if (ret) {
		dev_err(th1520_event->dev, "get rebootmode failed,ret:%d\n", ret);
		return ret;
	}
	dev_dbg(th1520_event->dev, "%s get rebootmode:0x%x\n", __func__, *mode);

	return 0;
}
EXPORT_SYMBOL_GPL(th1520_event_get_rebootmode);

static int th1520_event_check_powerup(void)
{
	enum th1520_rebootmode_index mode;
	unsigned int val;
	int ret;

	if (!th1520_event->init)
		return -EINVAL;

	ret = regmap_read(th1520_event->aon_iram, TH1520_EVENT_CHECK, &val);
	if (ret) {
		dev_err(th1520_event->dev, "get magicnum failed,ret:%d\n", ret);
		return ret;
	}
	ret = regmap_read(th1520_event->aon_iram, TH1520_EVENT_OFFSET, &mode);
	if (ret) {
		dev_err(th1520_event->dev, "get rebootmode failed,ret:%d\n", ret);
		return ret;
	}
	dev_info(th1520_event->dev, "magicnum:0x%x mode:0x%x\n", val, mode);

	/* powerup means SRAM data is randam */
	if (val != TH1520_EVENT_MAGIC && mode != TH1520_EVENT_PMIC_ONKEY)
		th1520_event_set_rebootmode(TH1520_EVENT_PMIC_POWERUP);

	ret = regmap_write(th1520_event->aon_iram, TH1520_EVENT_CHECK, TH1520_EVENT_MAGIC);
	if (ret) {
		dev_err(th1520_event->dev, "set magicnum failed,ret:%d\n", ret);
		return ret;
	}

	return 0;
}

static ssize_t rebootmode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	enum th1520_rebootmode_index mode;

	if (kstrtouint(buf, 0, &mode) < 0)
		return -EINVAL;
	th1520_event_set_rebootmode(mode);

	return count;
}

static ssize_t
rebootmode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	enum th1520_rebootmode_index mode;

	th1520_event_get_rebootmode(&mode);

	return sprintf(buf, "0x%x\n", mode);
}
static DEVICE_ATTR_RW(rebootmode);

static struct attribute *event_attrs[] = {
	&dev_attr_rebootmode.attr,
	NULL
};
ATTRIBUTE_GROUPS(event);

static int th1520_event_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int th1520_event_release(struct inode *inode, struct file *f)
{
	return 0;
}

static long th1520_event_ioctl(struct file *f, unsigned int ioctl,
			    unsigned long arg)
{
	return 0;
}

static const struct file_operations th1520_event_fops = {
	.owner          = THIS_MODULE,
	.release        = th1520_event_release,
	.open           = th1520_event_open,
	.unlocked_ioctl = th1520_event_ioctl,
};

static struct miscdevice th1520_event_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "th1520-event",
	.fops = &th1520_event_fops,
};

static int th1520_event_probe(struct platform_device *pdev)
{
	struct device		*dev = &pdev->dev;
	struct device_node	*np = dev->of_node;
	struct th1520_event	*event;
	int			ret;

	event = devm_kzalloc(&pdev->dev, sizeof(*event), GFP_KERNEL);
	if (!event)
		return -ENOMEM;

	ret = th1520_aon_get_handle(&(event->ipc_handle));
	if (ret == -EPROBE_DEFER)
		return ret;

	platform_set_drvdata(pdev, event);
	event->dev = &pdev->dev;

	event->aon_iram = syscon_regmap_lookup_by_phandle(np, "aon-iram-regmap");
	if (IS_ERR(event->aon_iram))
		return PTR_ERR(event->aon_iram);

	ret = misc_register(&th1520_event_misc);
	if (ret < 0)
		return ret;

	ret = th1520_event_aon_reservemem(event);
	if (ret) {
		dev_err(dev, "set aon reservemem failed!\n");
		return -EPERM;
	}
	event->init = true;
	th1520_event = event;

	ret = th1520_event_check_powerup();
	if (ret) {
		dev_err(dev, "check powerup failed!\n");
		th1520_event = NULL;
		return -EPERM;
	}
	dev_info(dev, "th1520-event driver init successfully\n");

	return 0;
}

static int th1520_event_remove(struct platform_device *pdev)
{
	misc_deregister(&th1520_event_misc);

	return 0;
}

static const struct of_device_id th1520_event_of_match[] = {
	{ .compatible = "xuantie,th1520-event" },
	{ },
};
MODULE_DEVICE_TABLE(of, th1520_event_of_match);

static struct platform_driver th1520_event_driver = {
	.probe		= th1520_event_probe,
	.remove		= th1520_event_remove,
	.driver		= {
		.name	= "th1520-event",
		.dev_groups	= event_groups,
		.of_match_table	= th1520_event_of_match,
	},
};

module_platform_driver(th1520_event_driver);

MODULE_DESCRIPTION("th1520-event driver");
MODULE_LICENSE("GPL v2");

/*
 * Copyright (C) 2023 Alibaba Group Holding Limited.
 *
 * derived from the omap-rpmsg implementation.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/th1520_rpmsg.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/th1520_rpmsg.h>
#include <linux/th1520_proc_debug.h>
#ifdef CONFIG_PM_SLEEP
#include <linux/firmware/thead/ipc.h>
//#include <linux/firmware/thead/light_event.h>
#endif
#define MBOX_MAX_MSG_LEN 28
#define WJ_MBOX_SEND_MAX_MESSAGE_LENGTH 28
#define HEXDUMP_BYTES_PER_LINE 28
#define HEXDUMP_LINE_LEN ((HEXDUMP_BYTES_PER_LINE * 4) + 2)
#define HEXDUMP_MAX_LEN \
	(HEXDUMP_LINE_LEN * (MBOX_MAX_MSG_LEN / HEXDUMP_BYTES_PER_LINE))

//extern struct th1520_rpmsg_vproc *pri_rpdev;
static struct dentry *root_debugfs_dir;

struct mbox_client_th1520_device {
	struct device *dev;
	void __iomem *tx_mmio;
	void __iomem *rx_mmio;
	struct mbox_chan *tx_channel;
	struct mbox_chan *rx_channel;
	char *rx_buffer;
	struct regmap *audio_mbox_regmap;
	char *message;
	spinlock_t lock;
};

struct mbox_client_th1520_device *tdev_priv;

static volatile uint32_t *p_mbox_reg;
static volatile uint32_t *p_mbox_reg1;
static volatile uint32_t *p_mbox_reg2;

/*
 * For now, allocate 256 buffers of 512 bytes for each side. each buffer
 * will then have 16B for the msg header and 496B for the payload.
 * This will require a total space of 256KB for the buffers themselves, and
 * 3 pages for every vring (the size of the vring depends on the number of
 * buffers it supports).
 */
#define RPMSG_NUM_BUFS (512)
//#define RPMSG_BUF_SIZE		(512)
//#define RPMSG_BUFS_SPACE	(RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)

/*
 * The alignment between the consumer and producer parts of the vring.
 * Note: this is part of the "wire" protocol. If you change this, you need
 * to update your BIOS image as well
 */
#define RPMSG_VRING_ALIGN (4096)

/* With 256 buffers, our vring will occupy 3 pages */
#define RPMSG_RING_SIZE                                                   \
	((DIV_ROUND_UP(vring_size(RPMSG_NUM_BUFS / 2, RPMSG_VRING_ALIGN), \
		       PAGE_SIZE)) *                                      \
	 PAGE_SIZE)

#define to_th1520_virdev(vd) container_of(vd, struct th1520_virdev, vdev)
#define to_th1520_rpdev(vd, id) \
	container_of(vd, struct th1520_rpmsg_vproc, ivdev[id])

struct th1520_rpmsg_vq_info {
	__u16 num; /* number of entries in the virtio_ring */
	__u16 vq_id; /* a globaly unique index of this virtqueue */
	void *addr; /* address where we mapped the virtio ring */
	struct th1520_rpmsg_vproc *rpdev;
};

static u64 th1520_rpmsg_get_features(struct virtio_device *vdev)
{
	/* VIRTIO_RPMSG_F_NS has been made private */
	return 1 << 0;
}

static int th1520_rpmsg_finalize_features(struct virtio_device *vdev)
{
	/* Give virtio_ring a chance to accept features */
	vring_transport_features(vdev);
	return 0;
}

/* kick the remote processor, and let it know which virtqueue to poke at */
static bool th1520_rpmsg_notify(struct virtqueue *vq)
{
	unsigned int mu_rpmsg = 0;
	int ret;
	struct th1520_rpmsg_vq_info *rpvq = vq->priv;

#ifdef CONFIG_PM_SLEEP
	if (rpvq->rpdev->sleep_flag) {
		dev_err(tdev_priv->dev,
			"dev in deep sleep, Channel cannot do Tx+++\n");
		return -EINVAL;
	}
#endif

	mu_rpmsg = rpvq->vq_id << 16;
	mutex_lock(&rpvq->rpdev->lock);

	//pr_info("th1520 rpmsg: notify %d\n", rpvq->rpdev->first_notify);
	if (unlikely(rpvq->rpdev->first_notify > 0)) {
		rpvq->rpdev->first_notify--;
		if (!tdev_priv->tx_channel) {
			dev_err(tdev_priv->dev, "Channel cannot do Tx+++\n");
			return -EINVAL;
		}

		ret = mbox_send_message(tdev_priv->tx_channel, "Hello, Queue!");
	} else {
		*p_mbox_reg1 |= 1 << 0;
		*p_mbox_reg2 |= 1 << 0;
	}
	mutex_unlock(&rpvq->rpdev->lock);

	return true;
}

static int th1520_mu_rpmsg_callback(struct notifier_block *this,
				    unsigned long index, void *data)
{
	phys_addr_t mu_msg = (phys_addr_t)data;
	struct th1520_virdev *virdev;

	virdev = container_of(this, struct th1520_virdev, nb);

	pr_debug("th1520 rpmsg: %s notifier_call mu_msg: 0x%pa\n", __func__,
		 &mu_msg);
	/* ignore vq indices which are clearly not for us */
	mu_msg = mu_msg >> 16;
	if (mu_msg < virdev->base_vq_id || mu_msg > virdev->base_vq_id + 1) {
		pr_debug("th1520 rpmsg: mu_msg 0x%pa is invalid\n", &mu_msg);
		//return NOTIFY_DONE;
	}

	mu_msg -= virdev->base_vq_id;
	pr_debug("%smu_msg 0x%pa base_vq_id 0x%xvirdev num_of_vqs0x%x\n",
		 __func__, &mu_msg, virdev->base_vq_id, virdev->num_of_vqs);

	/*
	 * Currently both PENDING_MSG and explicit-virtqueue-index
	 * messaging are supported.
	 * Whatever approach is taken, at this point 'mu_msg' contains
	 * the index of the vring which was just triggered.
	 */
	//if (mu_msg < virdev->num_of_vqs)
	vring_interrupt(mu_msg, virdev->vq[mu_msg]);

	return NOTIFY_DONE;
}

static int th1520_mu_rpmsg_register_nb(struct th1520_rpmsg_vproc *rpdev,
				       struct notifier_block *nb)
{
	if ((rpdev == NULL) || (nb == NULL))
		return -EINVAL;

	blocking_notifier_chain_register(&(rpdev->notifier), nb);

	return 0;
}

static int th1520_mu_rpmsg_unregister_nb(struct th1520_rpmsg_vproc *rpdev,
					 struct notifier_block *nb)
{
	if ((rpdev == NULL) || (nb == NULL))
		return -EINVAL;

	blocking_notifier_chain_unregister(&(rpdev->notifier), nb);

	return 0;
}

static struct virtqueue *rp_find_vq(struct virtio_device *vdev,
				    unsigned int index,
				    void (*callback)(struct virtqueue *vq),
				    const char *name, bool ctx)
{
	struct th1520_virdev *virdev = to_th1520_virdev(vdev);
	struct th1520_rpmsg_vproc *rpdev =
		to_th1520_rpdev(virdev, virdev->base_vq_id / 2);
	struct th1520_rpmsg_vq_info *rpvq;
	struct virtqueue *vq;
	int err;
	//static void __iomem *brd_io;

	rpvq = kmalloc(sizeof(*rpvq), GFP_KERNEL);
	if (!rpvq)
		return ERR_PTR(-ENOMEM);

	/* ioremap'ing normal memory, so we cast away sparse's complaints */
	//rpvq->addr = (__force void *) ioremap_nocache(virdev->vring[index],
	//						RPMSG_RING_SIZE);
	rpvq->addr =
		(__force void *)ioremap(virdev->vring[index], RPMSG_RING_SIZE);
	if (!rpvq->addr) {
		err = -ENOMEM;
		goto free_rpvq;
	}

	p_mbox_reg = ioremap((phys_addr_t)0xffefc48000, 25);
	p_mbox_reg1 = p_mbox_reg + 4;
	p_mbox_reg2 = p_mbox_reg + 5;

	memset_io(rpvq->addr, 0, RPMSG_RING_SIZE);

	pr_debug("vring%d: phys 0x%x, virt 0x%p\n", index, virdev->vring[index],
		 rpvq->addr);

	vq = vring_new_virtqueue(index, RPMSG_NUM_BUFS / 2, RPMSG_VRING_ALIGN,
				 vdev, true, ctx, rpvq->addr,
				 th1520_rpmsg_notify, callback, name);
	if (!vq) {
		pr_err("th1520 rpmsg: vring_new_virtqueue failed\n");
		err = -ENOMEM;
		goto unmap_vring;
	}

	virdev->vq[index] = vq;
	vq->priv = rpvq;
	/* system-wide unique id for this virtqueue */
	rpvq->vq_id = virdev->base_vq_id + index;
	rpvq->rpdev = rpdev;
	mutex_init(&rpdev->lock);

	return vq;

unmap_vring:
	/* iounmap normal memory, so make sparse happy */
	iounmap((__force void __iomem *)rpvq->addr);
free_rpvq:
	kfree(rpvq);
	return ERR_PTR(err);
}

static void th1520_rpmsg_del_vqs(struct virtio_device *vdev)
{
	struct virtqueue *vq, *n;
	struct th1520_virdev *virdev = to_th1520_virdev(vdev);
	struct th1520_rpmsg_vproc *rpdev =
		to_th1520_rpdev(virdev, virdev->base_vq_id / 2);

	list_for_each_entry_safe(vq, n, &vdev->vqs, list) {
		struct th1520_rpmsg_vq_info *rpvq = vq->priv;

		iounmap(rpvq->addr);
		vring_del_virtqueue(vq);
		kfree(rpvq);
	}

	th1520_mu_rpmsg_unregister_nb(rpdev, &virdev->nb);
}

static int th1520_rpmsg_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
				 struct virtqueue *vqs[],
				 vq_callback_t *callbacks[],
				 const char *const names[], const bool *ctx,
				 struct irq_affinity *desc)
{
	struct th1520_virdev *virdev = to_th1520_virdev(vdev);
	struct th1520_rpmsg_vproc *rpdev =
		to_th1520_rpdev(virdev, virdev->base_vq_id / 2);
	int i, err;

	/* we maintain two virtqueues per remote processor (for RX and TX) */
	if (nvqs != 2)
		return -EINVAL;

	for (i = 0; i < nvqs; ++i) {
		vqs[i] = rp_find_vq(vdev, i, callbacks[i], names[i],
				    ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	virdev->num_of_vqs = nvqs;

	virdev->nb.notifier_call = th1520_mu_rpmsg_callback;
	th1520_mu_rpmsg_register_nb(rpdev, &virdev->nb);

	return 0;

error:
	th1520_rpmsg_del_vqs(vdev);
	return err;
}

static void th1520_rpmsg_reset(struct virtio_device *vdev)
{
	dev_dbg(&vdev->dev, "reset!\n");
}

static u8 th1520_rpmsg_get_status(struct virtio_device *vdev)
{
	return 0;
}

static void th1520_rpmsg_set_status(struct virtio_device *vdev, u8 status)
{
	dev_dbg(&vdev->dev, "%s new status: %d\n", __func__, status);
}

static void th1520_rpmsg_vproc_release(struct device *dev)
{
	/* this handler is provided so driver core doesn't yell at us */
}

static struct virtio_config_ops th1520_rpmsg_config_ops = {
	.get_features = th1520_rpmsg_get_features,
	.finalize_features = th1520_rpmsg_finalize_features,
	.find_vqs = th1520_rpmsg_find_vqs,
	.del_vqs = th1520_rpmsg_del_vqs,
	.reset = th1520_rpmsg_reset,
	.set_status = th1520_rpmsg_set_status,
	.get_status = th1520_rpmsg_get_status,
};

static struct th1520_rpmsg_vproc th1520_rpmsg_vprocs[] = {
	{
		.rproc_name = "m4",
	},
	{
		.rproc_name = "m4",
	},
};

static const struct of_device_id th1520_rpmsg_dt_ids[] = {
	{
		.compatible = "th1520,th1520-rpmsg",
		.data = (void *)TH1520_RPMSG,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, th1520_rpmsg_dt_ids);

static int set_vring_phy_buf(struct platform_device *pdev,
			     struct th1520_rpmsg_vproc *rpdev, int vdev_nums)
{
	struct resource *res;
	resource_size_t size;
	unsigned int start, end;
	int i, ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res) {
		size = resource_size(res);
		start = res->start;
		end = res->start + size;
		for (i = 0; i < vdev_nums; i++) {
			rpdev->ivdev[i].vring[0] = start;
			rpdev->ivdev[i].vring[1] = start + 0x8000;
			start += 0x10000;
			if (start > end) {
				pr_err("Too small memory size %x!\n",
				       (u32)size);
				ret = -EINVAL;
				break;
			}
		}
	} else {
		return -ENOMEM;
	}

	return ret;
}

static void rpmsg_work_handler(struct work_struct *work)
{
	phys_addr_t message = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct th1520_rpmsg_vproc *rpdev =
		container_of(dwork, struct th1520_rpmsg_vproc, rpmsg_work);

	//spin_lock_irqsave(&rpdev->mu_lock, flags);
	blocking_notifier_call_chain(&(rpdev->notifier), 4, (void *)message);
	//spin_unlock_irqrestore(&rpdev->mu_lock, flags);
}

struct th1520_rpmsg_vproc *pri_rpdev;
EXPORT_SYMBOL_GPL(pri_rpdev);

int get_audio_log_mem(struct device *dev, phys_addr_t *mem, size_t *mem_size)
{
	struct resource r;
	struct device_node *node;
	int ret;

	*mem = 0;
	*mem_size = 0;

	node = of_parse_phandle(dev->of_node, "log-memory-region", 0);
	if (!node) {
		dev_err(dev, "no memory-region specified\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, &r);
	if (ret) {
		dev_err(dev, "memory-region get resource faild\n");
		return -EINVAL;
	}

	*mem = r.start;
	*mem_size = resource_size(&r);
	return 0;
}

static int th1520_rpmsg_probe(struct platform_device *pdev)
{
	int core_id, j, ret = 0;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct th1520_rpmsg_vproc *rpdev;
	char dir_name[32] = { 0x0 };
	if (of_property_read_u32(np, "multi-core-id", &core_id))
		core_id = 0;
	rpdev = &th1520_rpmsg_vprocs[core_id];
	rpdev->core_id = core_id;
	rpdev->variant =
		(enum th1520_rpmsg_variants)of_device_get_match_data(dev);
	spin_lock_init(&rpdev->mu_lock);

	pri_rpdev = rpdev;

	INIT_DELAYED_WORK(&(rpdev->rpmsg_work), rpmsg_work_handler);
	BLOCKING_INIT_NOTIFIER_HEAD(&(rpdev->notifier));
#ifdef CONFIG_PM_SLEEP
	sema_init(&rpdev->pm_sem, 0);
#endif
	pr_info("th1520 rpmsg: Ready for cross core communication!\n");

	ret = of_property_read_u32(np, "vdev-nums", &rpdev->vdev_nums);
	if (ret) {
		rpdev->vdev_nums = 1;
	}

	if (rpdev->vdev_nums > MAX_VDEV_NUMS) {
		pr_err("th1520 rpmsg: vdev-nums exceed the max %d\n",
		       MAX_VDEV_NUMS);
		return -EINVAL;
	}

	rpdev->first_notify = rpdev->vdev_nums;

	pr_info("th1520 rpmsg: rproc_name = %s", rpdev->rproc_name);
	if (!strcmp(rpdev->rproc_name, "m4")) {
		ret = set_vring_phy_buf(pdev, rpdev, rpdev->vdev_nums);
		if (ret) {
			pr_err("th1520 rpmsg: No vring buffer.\n");
			return -ENOMEM;
		}
	} else {
		pr_err("th1520 rpmsg: No remote processor.\n");
		return -ENODEV;
	}

	for (j = 0; j < rpdev->vdev_nums; j++) {
		pr_debug("%s rpdev%d vdev%d: vring0 0x%x, vring1 0x%x\n",
			 __func__, rpdev->core_id, rpdev->vdev_nums,
			 rpdev->ivdev[j].vring[0], rpdev->ivdev[j].vring[1]);
		rpdev->ivdev[j].vdev.id.device = VIRTIO_ID_RPMSG;
		rpdev->ivdev[j].vdev.config = &th1520_rpmsg_config_ops;
		rpdev->ivdev[j].vdev.dev.parent = &pdev->dev;
		rpdev->ivdev[j].vdev.dev.release = th1520_rpmsg_vproc_release;
		rpdev->ivdev[j].base_vq_id = j * 2;

		ret = register_virtio_device(&rpdev->ivdev[j].vdev);
		if (ret) {
			pr_err("th1520 rpmsg: %s failed to register rpdev: %d\n",
			       __func__, ret);
			return ret;
		}
	}

	ret = get_audio_log_mem(dev, &rpdev->log_phy, &rpdev->log_size);
	if (ret) {
		return ret;
	}
	rpdev->log_mem = ioremap(rpdev->log_phy, rpdev->log_size);
	if (!IS_ERR(rpdev->log_mem)) {
		pr_info("virtual_log_mem=0x%p, phy base=0x%pa\n",
			rpdev->log_mem, &rpdev->log_phy);
	} else {
		rpdev->log_mem = NULL;
		dev_err(dev, "%s:get audio log region fail\n", __func__);
		return -1;
	}

	sprintf(dir_name, "audio_proc");
	rpdev->proc_dir = proc_mkdir(dir_name, NULL);
	if (NULL != rpdev->proc_dir) {
		rpdev->log_ctrl = th1520_create_panic_log_proc(rpdev->log_phy,
							       rpdev->proc_dir,
							       rpdev->log_mem,
							       rpdev->log_size);
	} else {
		dev_err(dev, "create %s fail\n", dir_name);
		return ret;
	}

	platform_set_drvdata(pdev, rpdev);

	return ret;
}

#ifdef CONFIG_PM_SLEEP

typedef enum {
	RPMSG_MAILBOX_TYPE_PM = 0xA0,
	RPMSG_MAILBOX_TYPE_MAX
} rpmsg_mailbox_message_type_en;

typedef enum {
	RPMSG_PM_CTRL = 0x50,
	RPMSG_PM_GET,
	RPMSG_PM_STATUS,
	RPMSG_PM_MAX
} rpmsg_pm_message_type_en;

typedef enum {
	TH1520_PM_DISABLE = 0xA0,
	TH1520_PM_OFF,
	TH1520_PM_HW_VAD,
	TH1520_PM_TYPE_MAX
} th1520_pm_type_en;

typedef enum {
	TH1520_PM_WAKEUP = 0x50,
	TH1520_PM_SLEEP,
	TH1520_PM_STATUS_MAX
} th1520_pm_status_en;

#define MAX_PM_NOTIFY_TIME 10
#define MAX_PM_ASK_TIME 10

static int th1520_rpmsg_sleep_notify(struct virtqueue *vq,
				     th1520_pm_type_en type)
{
	int ret;
	struct th1520_rpmsg_vq_info *rpvq = vq->priv;
	uint8_t sleep_ctrl[4] = { RPMSG_MAILBOX_TYPE_PM, RPMSG_PM_CTRL, type,
				  '\n' };
	mutex_lock(&rpvq->rpdev->lock);
	ret = mbox_send_message(tdev_priv->tx_channel, sleep_ctrl);
	if (ret < 0) {
		pr_err("sleep notify faild %d", ret);
		mutex_unlock(&rpvq->rpdev->lock);
		return ret;
	}
	mutex_unlock(&rpvq->rpdev->lock);
	return 0;
}

static int th1520_rpmsg_sleep_ask(struct virtqueue *vq)
{
	int ret;
	struct th1520_rpmsg_vq_info *rpvq = vq->priv;
	uint8_t sleep_get[3] = { RPMSG_MAILBOX_TYPE_PM, RPMSG_PM_GET, '\n' };
	mutex_lock(&rpvq->rpdev->lock);
	ret = mbox_send_message(tdev_priv->tx_channel, sleep_get);
	if (ret < 0) {
		pr_err("sleep ask send faild %d", ret);
		mutex_unlock(&rpvq->rpdev->lock);
		return ret;
	}
	mutex_unlock(&rpvq->rpdev->lock);
	return 0;
}

static int th1520_rpmsg_suspend(struct device *dev)

{
	int try_num = 0;
	struct th1520_rpmsg_vproc *rpdev = dev_get_drvdata(dev);

	//clk_disable_unprepare(rpdev->mu_clk);
	th1520_rpmsg_sleep_notify(rpdev->ivdev[0].vq[0], TH1520_PM_OFF);
	try_num++;
	if (down_timeout(&rpdev->pm_sem, msecs_to_jiffies(200)) < 0) {
		pr_info("Wait pm_sem timeout\n");
	}
	while (!rpdev->sleep_flag) {
		th1520_rpmsg_sleep_notify(rpdev->ivdev[0].vq[0], TH1520_PM_OFF);
		if (down_timeout(&rpdev->pm_sem, msecs_to_jiffies(200)) < 0) {
			pr_info("Wait pm_sem timeout\n");
		}
		if (try_num++ > MAX_PM_NOTIFY_TIME) {
			pr_err("sleep notify faild after try %d time",
			       MAX_PM_NOTIFY_TIME);
			return -1;
		}
	}
	pr_info("%s,%d,try %d times, exist", __func__, __LINE__, try_num);
	return 0;
}

#define C906_RESET_REG 0xfffff4403c

static void reset_audio(void)
{
	uint64_t *v_addr = ioremap((phys_addr_t)C906_RESET_REG, 4);
	if (!v_addr) {
		pr_err("io remap failed\r\n");
		return;
	}
	writel(0x37, (volatile void *)v_addr);
	writel(0x3f, (volatile void *)v_addr);
	iounmap((volatile void __iomem *)C906_RESET_REG);
}

static int th1520_rpmsg_resume(struct device *dev)
{
	struct th1520_rpmsg_vproc *rpdev = dev_get_drvdata(dev);
	int ret;
	int try_num = 0;
	int rst_flag = 0;

	while (rpdev->sleep_flag) {
		ret = th1520_rpmsg_sleep_ask(rpdev->ivdev[0].vq[0]);
		if (down_timeout(&rpdev->pm_sem, msecs_to_jiffies(200)) < 0) {
			pr_info("Wait pm_sem timeout\n");
		}
		if (try_num++ > MAX_PM_ASK_TIME) {
			pr_err("sleep status check faild after try %d time",
			       MAX_PM_ASK_TIME);
			if (!rst_flag) {
				pr_info("Reset audio directly now");
				reset_audio();
				rst_flag = 1;
				try_num = 0;
			} else {
				pr_err("sleep states check failed after Reset audio");
				return -1;
			}
		}
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(th1520_rpmsg_pm_ops, th1520_rpmsg_suspend,
			 th1520_rpmsg_resume);

static struct platform_driver th1520_rpmsg_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "th1520-rpmsg",
		   .of_match_table = th1520_rpmsg_dt_ids,
		   .pm = &th1520_rpmsg_pm_ops,
		   },
	.probe = th1520_rpmsg_probe,
};

static int __init th1520_rpmsg_init(void)
{
	int ret;

	ret = platform_driver_register(&th1520_rpmsg_driver);
	if (ret)
		pr_err("th1520 rpmsg: Unable to initialize\n");
	else
		pr_info("th1520 rpmsg: driver is registered.\n");

	return ret;
}

MODULE_AUTHOR(",Inc.");
MODULE_DESCRIPTION("remote processor messaging virtio device");
MODULE_LICENSE("GPL v2");
late_initcall(th1520_rpmsg_init);

static ssize_t mbox_client_th1520_message_write(struct file *filp,
						const char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct mbox_client_th1520_device *tdev = filp->private_data;
	void *data;
	int ret;

	if (!tdev->tx_channel) {
		dev_err(tdev->dev, "Channel cannot do Tx\n");
		return -EINVAL;
	}

	if (count > WJ_MBOX_SEND_MAX_MESSAGE_LENGTH)
		count = WJ_MBOX_SEND_MAX_MESSAGE_LENGTH;

	tdev->message = kzalloc(MBOX_MAX_MSG_LEN, GFP_KERNEL);
	if (!tdev->message)
		return -ENOMEM;

	ret = copy_from_user(tdev->message, userbuf, count);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	data = tdev->message;
	print_hex_dump(KERN_INFO, __func__, DUMP_PREFIX_NONE, 16, 1,
		       tdev->message, MBOX_MAX_MSG_LEN, true);

	ret = mbox_send_message(tdev->tx_channel, data);
	if (ret < 0)
		dev_err(tdev->dev, "Failed to send message via mailbox\n");

out:
	kfree(tdev->message);
	return ret < 0 ? ret : count;
}

static ssize_t mbox_client_th1520_message_read(struct file *filp,
					       char __user *userbuf,
					       size_t count, loff_t *ppos)
{
	struct mbox_client_th1520_device *tdev = filp->private_data;
	unsigned long flags;

	print_hex_dump(KERN_INFO, __func__, DUMP_PREFIX_NONE, 16, 1,
		       tdev->rx_buffer, MBOX_MAX_MSG_LEN, true);
	spin_lock_irqsave(&tdev->lock, flags);
	memset(tdev->rx_buffer, 0, MBOX_MAX_MSG_LEN);
	spin_unlock_irqrestore(&tdev->lock, flags);

	return MBOX_MAX_MSG_LEN;
}

static const struct file_operations mbox_client_th1520_message_ops = {
	.write = mbox_client_th1520_message_write,
	.read = mbox_client_th1520_message_read,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

static int index_names = 0;
static bool debugfs_dir_created = false;
static const char *file_names[] = { "mbox-client0", "mbox-client1" };

static int
mbox_client_th1520_add_debugfs(struct platform_device *pdev,
			       struct mbox_client_th1520_device *tdev)
{
	if (!debugfs_initialized())
		return 0;

	if (index_names > 2) {
		dev_err(&pdev->dev, "Max device index is 2\n");
		return 0;
	}

	if (!debugfs_dir_created) {
		root_debugfs_dir = debugfs_create_dir("mailbox", NULL);
		if (!root_debugfs_dir) {
			dev_err(&pdev->dev,
				"Failed to create mailbox debugfs\n");
			return -EINVAL;
		}
		debugfs_dir_created = true;
	}

	debugfs_create_file(file_names[index_names], 0600, root_debugfs_dir,
			    tdev, &mbox_client_th1520_message_ops);

	index_names++;
	return 0;
}

static void mbox_client_th1520_receive_message(struct mbox_client *client,
					       void *message)
{
	struct mbox_client_th1520_device *tdev = dev_get_drvdata(client->dev);
	char *data = message;

	spin_lock(&tdev->lock);
	memcpy(tdev->rx_buffer, data, MBOX_MAX_MSG_LEN);
	spin_unlock(&tdev->lock);

	schedule_delayed_work(&(pri_rpdev->rpmsg_work), 0);
#ifdef CONFIG_PM_SLEEP
	if (data[0] == RPMSG_MAILBOX_TYPE_PM && data[1] == RPMSG_PM_STATUS) {
		if (data[2] == TH1520_PM_WAKEUP) {
			pri_rpdev->sleep_flag = 0;
			up(&pri_rpdev->pm_sem);
			pr_info("audio wakeup");
		} else if (data[2] == TH1520_PM_SLEEP) {
			pri_rpdev->sleep_flag = 1;
			up(&pri_rpdev->pm_sem);
			pr_info("audio sleep");
		}
	}
#endif
	//print_hex_dump(KERN_INFO, __func__, DUMP_PREFIX_NONE, 16, 1, tdev->rx_buffer, MBOX_MAX_MSG_LEN, true);
}

static struct mbox_chan *
mbox_client_th1520_request_channel(struct platform_device *pdev,
				   const char *name)
{
	struct mbox_client *client;
	struct mbox_chan *channel;

	client = devm_kzalloc(&pdev->dev, sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->dev = &pdev->dev;
	client->tx_block = true;
	client->knows_txdone = false;
	client->tx_tout = 500;
	client->rx_callback = mbox_client_th1520_receive_message;

	channel = mbox_request_channel_byname(client, name);
	if (IS_ERR(channel)) {
		devm_kfree(&pdev->dev, client);
		dev_warn(&pdev->dev, "Failed to request %s channel\n", name);
		return NULL;
	}

	return channel;
}

static int mbox_client_th1520_probe(struct platform_device *pdev)
{
	struct mbox_client_th1520_device *tdev;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	static int chan_idx = 1;

	tdev = devm_kzalloc(&pdev->dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	tdev_priv = tdev;

	if (!chan_idx)
		tdev->tx_channel =
			mbox_client_th1520_request_channel(pdev, "902");
	else
		tdev->tx_channel =
			mbox_client_th1520_request_channel(pdev, "906");

	if (!tdev->tx_channel) {
		dev_err(&pdev->dev, "Request channel failed\n");
		return -EPROBE_DEFER;
	}
	chan_idx++;

	/* In fact, rx_channel is same with tx_channel in C-SKY's mailbox */
	tdev->rx_channel = tdev->tx_channel;

	tdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, tdev);

	tdev->audio_mbox_regmap =
		syscon_regmap_lookup_by_phandle(np, "audio-mbox-regmap");
	if (IS_ERR(tdev->audio_mbox_regmap)) {
		dev_err(&pdev->dev,
			"cannot find regmap for audio mbox register\n");
	} else {
		dev_dbg(&pdev->dev, "audio_mbox_regmap ok\n");
	}

	spin_lock_init(&tdev->lock);

	tdev->rx_buffer =
		devm_kzalloc(&pdev->dev, MBOX_MAX_MSG_LEN, GFP_KERNEL);
	if (!tdev->rx_buffer)
		return -ENOMEM;

	ret = mbox_client_th1520_add_debugfs(pdev, tdev);
	if (ret)
		return ret;

	dev_err(&pdev->dev, "Successfully registered\n");

	return 0;
}

static int mbox_client_th1520_remove(struct platform_device *pdev)
{
	struct mbox_client_th1520_device *tdev = platform_get_drvdata(pdev);

	debugfs_remove_recursive(root_debugfs_dir);

	if (tdev->tx_channel)
		mbox_free_channel(tdev->tx_channel);

	if (tdev->rx_channel && tdev->rx_channel != tdev->tx_channel)
		mbox_free_channel(tdev->rx_channel);

	return 0;
}

static const struct of_device_id mbox_client_th1520_match[] = {
	{ .compatible = "thead,th1520-mbox-client" },
	{},
};

static struct platform_driver mbox_client_th1520_driver = {
	.driver = {
		.name = "thead,th1520-mbox-client",
		.of_match_table = mbox_client_th1520_match,
	},
	.probe  = mbox_client_th1520_probe,
	.remove = mbox_client_th1520_remove,
};
module_platform_driver(mbox_client_th1520_driver);

MODULE_AUTHOR("Alibaba Group Holding Limited");
MODULE_DESCRIPTION("Thead Light mailbox IPC client driver");
MODULE_LICENSE("GPL v2");

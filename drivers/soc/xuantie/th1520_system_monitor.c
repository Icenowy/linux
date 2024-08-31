// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 *
 * Additional modifications by Hengqiang Ming <minghq@linux.alibaba.com>>
 */

#include <dt-bindings/soc/th1520_system_status.h>
#include <linux/clk-provider.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <soc/xuantie/th1520_system_monitor.h>
#include <soc/xuantie/th1520_system_status.h>

#define VIDEO_1080P_SIZE (1920 * 1080)
#define VIDEO_2K_SIZE (2560 * 1440)

struct video_info {
	unsigned int width;
	unsigned int height;
	unsigned int videoFramerate;
	struct list_head node;
};

struct system_monitor_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t n);
};

struct system_monitor {
	struct device *dev;
	struct notifier_block status_nb;
	struct kobject *kobj;

	unsigned int delay;
	unsigned long video_status;
};

static unsigned long system_status;
static unsigned long ref_count[32] = {0};

static DEFINE_MUTEX(system_status_mutex);
static DEFINE_SPINLOCK(video_info_lock);

static DECLARE_RWSEM(mdev_list_lock);

static LIST_HEAD(video_info_list);
static LIST_HEAD(monitor_dev_list);
static struct system_monitor *system_monitor;

static BLOCKING_NOTIFIER_HEAD(system_status_notifier_list);

int th1520_register_system_status_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&system_status_notifier_list, nb);
}
EXPORT_SYMBOL(th1520_register_system_status_notifier);

int th1520_unregister_system_status_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&system_status_notifier_list, nb);
}
EXPORT_SYMBOL(th1520_unregister_system_status_notifier);

static int th1520_system_status_notifier_call_chain(unsigned long val)
{
	int ret = blocking_notifier_call_chain(&system_status_notifier_list, val, NULL);

	return notifier_to_errno(ret);
}

static int th1520_register_dev_status_notifier(struct monitor_dev_info *info,
								struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&info->dev_notifier, nb);
}

static int th1520_unregister_dev_status_notifier(struct monitor_dev_info *info,
								struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&info->dev_notifier, nb);
}

static int th1520_dev_status_notifier_call_chain(struct monitor_dev_info *info,
								unsigned long val)
{
	int ret = blocking_notifier_call_chain(&info->dev_notifier, val, NULL);

	return notifier_to_errno(ret);
}

void th1520_set_system_status(unsigned long status)
{
	unsigned long old_system_status;
	unsigned int single_state_offset;

	mutex_lock(&system_status_mutex);

	old_system_status = system_status;

	while (status) {
		single_state_offset = fls(status) - 1;
		status &= ~(1 << single_state_offset);
		if (ref_count[single_state_offset] == 0)
			system_status |= 1 << single_state_offset;
		ref_count[single_state_offset]++;
	}

	if (old_system_status != system_status) {
		th1520_system_status_notifier_call_chain(system_status);
		dev_dbg(system_monitor->dev, "Status change from:%ld to:%ld\n"
								,old_system_status, system_status);
	}

	mutex_unlock(&system_status_mutex);
}
EXPORT_SYMBOL(th1520_set_system_status);

void th1520_clear_system_status(unsigned long status)
{
	unsigned long old_system_status;
	unsigned int single_state_offset;

	mutex_lock(&system_status_mutex);

	old_system_status = system_status;

	while (status) {
		single_state_offset = fls(status) - 1;
		status &= ~(1 << single_state_offset);
		if (ref_count[single_state_offset] == 0) {
			continue;
		} else {
			if (ref_count[single_state_offset] == 1)
				system_status &= ~(1 << single_state_offset);
			ref_count[single_state_offset]--;
		}
	}

	if (old_system_status != system_status) {
		th1520_system_status_notifier_call_chain(system_status);
		dev_dbg(system_monitor->dev, "Status change from:%ld to:%ld\n"
						,old_system_status, system_status);
	}

	mutex_unlock(&system_status_mutex);
}
EXPORT_SYMBOL(th1520_clear_system_status);

unsigned long th1520_get_system_status(void)
{
	return system_status;
}
EXPORT_SYMBOL(th1520_get_system_status);

static void th1520_set_dev_status(struct monitor_dev_info *info, unsigned long status)
{
	unsigned long old_system_status;
	unsigned int state_offset;

	if ((status & DEV_STATUS_MASK) == 0) {
		dev_err(info->dev, "Invalid module status: 0x%lx\n", status);
		return;
	}

	mutex_lock(&info->status_lock);

	old_system_status = info->dev_monitor_status;

	state_offset = fls(status) - 1;
	if (info->ref_count[state_offset] == 0)
		info->dev_monitor_status |= (status & DEV_STATUS_MASK);
	info->ref_count[state_offset]++;

	if (old_system_status != info->dev_monitor_status)
		th1520_dev_status_notifier_call_chain(info, info->dev_monitor_status);

	mutex_unlock(&info->status_lock);
}

static void th1520_clear_dev_status(struct monitor_dev_info *info, unsigned long status)
{
	unsigned long old_system_status;
	unsigned int state_offset;

	if ((status & DEV_STATUS_MASK) == 0) {
		dev_err(info->dev, "Invalid module status: 0x%lx\n", status);
		return;
	}

	mutex_lock(&info->status_lock);

	old_system_status = info->dev_monitor_status;

	state_offset = fls(status) - 1;
	if (info->ref_count[state_offset]){
		if (info->ref_count[state_offset] == 1)
			info->dev_monitor_status &= ~(status & DEV_STATUS_MASK);
		info->ref_count[state_offset]--;
	}

	if (old_system_status != info->dev_monitor_status)
		th1520_dev_status_notifier_call_chain(info, info->dev_monitor_status);

	mutex_unlock(&info->status_lock);
}

static unsigned long th1520_get_video_param(char **str)
{
	char *p;
	unsigned long val = 0;

	strsep(str, "=");
	p = strsep(str, ",");
	if (p) {
		if (kstrtoul(p, 10, &val))
			return 0;
	}

	return val;
}

/*
 * 0,width=val,height=val,framerate=val: to clear video information
 * 1,width=val,height=val,framerate=val: to set video information
 */
static struct video_info *th1520_parse_video_info(const char *buf)
{
	struct video_info *video_info;
	const char *cp = buf;
	char *str, *p;
	int ntokens = 0;

	while ((cp = strpbrk(cp + 1, ",")))
		ntokens++;

	video_info = kzalloc(sizeof(*video_info), GFP_KERNEL);
	if (!video_info)
		return NULL;

	INIT_LIST_HEAD(&video_info->node);

	str = kstrdup(buf, GFP_KERNEL);
	p = str;
	strsep(&p, ",");
	video_info->width = th1520_get_video_param(&p);
	video_info->height = th1520_get_video_param(&p);
	video_info->videoFramerate = th1520_get_video_param(&p);
	pr_info("%c,width=%d,height=%d,framerate=%d\n",
		 buf[0],
		 video_info->width,
		 video_info->height,
		 video_info->videoFramerate);
	kfree(str);

	return video_info;
}

static struct video_info *th1520_find_video_info(const char *buf)
{
	struct video_info *info, *video_info;

	video_info = th1520_parse_video_info(buf);

	if (!video_info)
		return NULL;

	spin_lock(&video_info_lock);
	list_for_each_entry(info, &video_info_list, node) {
		if (info->width == video_info->width &&
		    info->height == video_info->height &&
			info->videoFramerate == video_info->videoFramerate) {
			spin_unlock(&video_info_lock);
			kfree(video_info);
			return info;
		}
	}

	spin_unlock(&video_info_lock);
	kfree(video_info);

	return NULL;
}

static void th1520_add_video_info(struct video_info *video_info)
{
	if (video_info) {
		spin_lock(&video_info_lock);
		list_add(&video_info->node, &video_info_list);
		spin_unlock(&video_info_lock);
	}
}

static void th1520_del_video_info(struct video_info *video_info)
{
	if (video_info) {
		spin_lock(&video_info_lock);
		list_del(&video_info->node);
		spin_unlock(&video_info_lock);
		kfree(video_info);
	}
}

static void th1520_update_video_status(void)
{
	struct video_info *video_info;
	unsigned int max_res = 0, res = 0, max_video_framerate=0;
	unsigned long old_video_status = 0, target_video_status = 0;

	spin_lock(&video_info_lock);
	if (list_empty(&video_info_list)) {
		old_video_status = system_monitor->video_status;
		system_monitor->video_status = 0;
		spin_unlock(&video_info_lock);
		th1520_clear_system_status(old_video_status);
		return;
	}

	old_video_status = system_monitor->video_status;
	list_for_each_entry(video_info, &video_info_list, node) {
	res = video_info->width * video_info->height;
		if (res > max_res)
			max_res = res;
		if (video_info->videoFramerate > max_video_framerate)
			max_video_framerate = video_info->videoFramerate;
	}

	if (max_res <= VIDEO_1080P_SIZE) {
		if (max_video_framerate >= 60)
			target_video_status = SYS_STATUS_VIDEO_1080P_60F;
		else
			target_video_status = SYS_STATUS_VIDEO_1080P;
	} else if (max_res <= VIDEO_2K_SIZE) {
		if (max_video_framerate >= 60)
			target_video_status = SYS_STATUS_VIDEO_2K_60F;
		else
			target_video_status = SYS_STATUS_VIDEO_2K;
	} else {
		if (max_video_framerate >= 60)
			target_video_status = SYS_STATUS_VIDEO_4K_60F;
		else
			target_video_status = SYS_STATUS_VIDEO_4K;
	}

	if (old_video_status != target_video_status) {
		system_monitor->video_status = target_video_status;
		spin_unlock(&video_info_lock);
		th1520_set_system_status(target_video_status);
		th1520_clear_system_status(old_video_status);
		return;
	}

	spin_unlock(&video_info_lock);
}

void th1520_update_system_status_byname(const char *buf)
{
	struct video_info *video_info;

	if (!buf)
		return;

	switch (buf[0]) {
	case '0':
		/* clear video info */
		video_info = th1520_find_video_info(buf);
		if (video_info) {
			th1520_del_video_info(video_info);
			th1520_update_video_status();
		}
		break;
	case '1':
		/* set video info */
		video_info = th1520_parse_video_info(buf);
		if (video_info) {
			th1520_add_video_info(video_info);
			th1520_update_video_status();
		}
		break;
	case 'P':
		/* set performance status */
		th1520_set_system_status(SYS_STATUS_PERFORMANCE);
		break;
	case 'p':
		/* clear performance status */
		th1520_clear_system_status(SYS_STATUS_PERFORMANCE);
		break;
	case 'L':
		/* set launch status */
		th1520_set_system_status(SYS_STATUS_LAUNCH);
		break;
	case 'l':
		/* clear launch status */
		th1520_clear_system_status(SYS_STATUS_LAUNCH);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(th1520_update_system_status_byname);

static void th1520_update_dev_monitor_status_byname(struct monitor_dev_info *info,
							const char *buf)
{
	if (!buf)
		return;

	switch (buf[0]) {
		case 'P':
			th1520_set_dev_status(info, DEV_STATUS_HIGH);
			break;
		case 'p':
			th1520_clear_dev_status(info, DEV_STATUS_HIGH);
			break;
		case 'M':
			th1520_set_dev_status(info, DEV_STATUS_MEDIUM);
			break;
		case 'm':
			th1520_clear_dev_status(info, DEV_STATUS_MEDIUM);
			break;
		case 'L':
			th1520_set_dev_status(info, DEV_STATUS_LOW);
			break;
		case 'l':
			th1520_clear_dev_status(info, DEV_STATUS_LOW);
			break;
		default:
			break;
	}
}

static void th1520_dev_status_limit_freq(struct monitor_dev_info *info,
							unsigned long status)
{
	unsigned long target_freq = FREQ_QOS_MIN_DEFAULT_VALUE;

	if (info->dev_qos_high_limit && (status & DEV_STATUS_HIGH))
		target_freq = info->dev_qos_high_limit;
	else if (info->dev_qos_mid_limit && (status & DEV_STATUS_MEDIUM))
		target_freq = info->dev_qos_mid_limit;
	else
		target_freq = info->dev_qos_low_limit;

	spin_lock(&info->minfreq_lock);

	if (target_freq == info->cur_min_limit) {
		spin_unlock(&info->minfreq_lock);
		return;
	}

	info->cur_min_limit = target_freq;

	spin_unlock(&info->minfreq_lock);

	th1520_monitor_dev_rate_adjust(info, target_freq);
}

static int th1520_dev_status_notifier(struct notifier_block *nb,
					   unsigned long status, void *ptr)
{
	struct monitor_dev_info *info = container_of(nb, struct monitor_dev_info, status_nb);

	th1520_dev_status_limit_freq(info, status);

	return NOTIFY_OK;
}

static ssize_t status_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	unsigned int status = th1520_get_system_status();

	return sprintf(buf, "0x%x\n", status);
}

static ssize_t status_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t n)
{
	if (!n)
		return -EINVAL;

	th1520_update_system_status_byname(buf);

	return n;
}

static ssize_t video_info_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	struct video_info *info;
	ssize_t pos = 0;

	spin_lock(&video_info_lock);

	list_for_each_entry(info, &video_info_list, node) {
		pos += sprintf(buf + pos, "Width=%u,Height=%u,Framerate=%u\n",
						info->width, info->height, info->videoFramerate);
	}

	spin_unlock(&video_info_lock);

	return pos;
}

static ssize_t monitor_status_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	struct monitor_dev_info *info = container_of(kobj, struct monitor_dev_info, kobj);

	return sprintf(buf, "0x%lx\n", info->dev_monitor_status);
}

static ssize_t monitor_status_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	if (!n)
		return -EINVAL;

	struct monitor_dev_info *info = container_of(kobj, struct monitor_dev_info, kobj);

	th1520_update_dev_monitor_status_byname(info, buf);

	return n;
}

static ssize_t dev_status_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct monitor_dev_info *info = container_of(kobj, struct monitor_dev_info, kobj);
	ssize_t len = 0;

	unsigned long low_count = info->ref_count[DEV_STATUS_OFFSET + 0];
	unsigned long medium_count = info->ref_count[DEV_STATUS_OFFSET + 1];
	unsigned long high_count = info->ref_count[DEV_STATUS_OFFSET + 2];

	len += sprintf(buf + len, "---------------- Dev Status Count ----------------\n");
	len += sprintf(buf + len, "DEV_STATUS_LOW\tDEV_STATUS_MEDIUM\tDEV_STATUS_HIGH\n");
	len += sprintf(buf + len, "%lu\t\t%lu\t\t\t%lu\n", low_count, medium_count, high_count);

	return len;
}

static struct system_monitor_attr status =
	__ATTR(system_status, 0644, status_show, status_store);

static struct system_monitor_attr video_info =
	__ATTR(video_online_info, 0444, video_info_show, NULL);

static struct system_monitor_attr dev_monitor_status =
	__ATTR(status, 0644, monitor_status_show, monitor_status_store);

static struct system_monitor_attr dev_status_count =
	__ATTR(status_count, 0444, dev_status_count_show, NULL);

static struct attribute *dev_monitor_sysfs_attrs[] = {
	&dev_monitor_status.attr,
	&dev_status_count.attr,
	NULL,
};

static struct attribute_group dev_monitor_sysfs_attr_group = {
	.name = NULL,
	.attrs = dev_monitor_sysfs_attrs,
};

static struct kobj_type monitor_dev_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
};

static int monitor_device_parse_status_config(struct device_node *np,
					      struct monitor_dev_info *info)
{
	int ret = 0;

	ret = of_property_read_u32(np, "video-4k-minfreq",
					&info->video_4k_limit);
	ret &= of_property_read_u32(np, "video-2k-minfreq",
					&info->video_2k_limit);
	ret &= of_property_read_u32(np, "video-1080p-minfreq",
				    &info->video_1080p_limit);
	ret &= of_property_read_u32(np, "qos-mid-minfreq",
					&info->dev_qos_mid_limit);
	ret &= of_property_read_u32(np, "vsync-minfreq",
				    &info->vsync_limit);

	return ret;
}

static int monitor_device_parse_dt(struct device *dev,
				   struct monitor_dev_info *info)
{
	struct device_node *np;
	int ret;

	if (info->devp->type == MONITOR_TPYE_CPU) {
		np = dev->of_node;
	} else {
		np = of_parse_phandle(dev->of_node, "operating-points-v2", 0);
	}

	if (!np) {
		dev_err(dev, "%s:Fail to get operating-points\n", __FUNCTION__);
		return -EINVAL;
	}

	ret = monitor_device_parse_status_config(np, info);

	of_node_put(np);

	return ret;
}

int th1520_monitor_dev_rate_adjust(struct monitor_dev_info *info,
					  unsigned long val)
{
	if (info->devp->type == MONITOR_TPYE_CPU) {
		freq_qos_update_request(&info->min_sta_freq_req, val/1000);
	} else {
		dev_pm_qos_update_request(&info->dev_min_freq_req, val / 1000);
	}

	return 0;
}
EXPORT_SYMBOL(th1520_monitor_dev_rate_adjust);

static int
th1520_system_monitor_freq_qos_requset(struct monitor_dev_info *info)
{
	struct devfreq *devfreq;
	struct cpufreq_policy *policy;
	int ret;

	if (info->devp->type == MONITOR_TPYE_CPU) {
		policy = cpufreq_cpu_get(0);
		ret = freq_qos_add_request(&policy->constraints,
				&info->min_sta_freq_req,
				FREQ_QOS_MIN,
				FREQ_QOS_MIN_DEFAULT_VALUE);
	} else {
		devfreq = (struct devfreq *)info->devp->data;
		ret = dev_pm_qos_add_request(devfreq->dev.parent,
				&info->dev_min_freq_req,
				DEV_PM_QOS_MIN_FREQUENCY,
				FREQ_QOS_MIN_DEFAULT_VALUE);
	}

	return ret;
}

static void init_opp(struct device *dev, struct monitor_dev_info *info)
{
	struct dev_pm_opp *opp;
	unsigned long freq = FREQ_QOS_MIN_DEFAULT_VALUE;

	opp = dev_pm_opp_find_freq_ceil(dev, &freq);
	info->dev_qos_low_limit = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);

	freq = FREQ_QOS_MAX_DEFAULT_VALUE;
	opp = dev_pm_opp_find_freq_floor(dev, &freq);
	info->dev_qos_high_limit= dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);
}

struct monitor_dev_info *
th1520_system_monitor_register(struct device *dev, struct monitor_dev_profile *devp)
{
	struct monitor_dev_info *info;
	int ret = 0;

	if (!system_monitor)
		return ERR_PTR(-ENOMEM);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);
	info->dev = dev;
	info->devp = devp;

	mutex_init(&info->status_lock);
	spin_lock_init(&info->minfreq_lock);

	if (monitor_device_parse_dt(dev, info)) {
		devp->is_checked = true;
		dev_warn(dev, "fail to parse the status_config!\n");
	}

	init_opp(dev, info);

	ret = th1520_system_monitor_freq_qos_requset(info);
	if (ret < 0) {
		dev_err(dev, "fail to regist to qos\n");
		kfree(info);
		return ERR_PTR(-EINVAL);
	}

	BLOCKING_INIT_NOTIFIER_HEAD(&info->dev_notifier);

	ret = kobject_init_and_add(&info->kobj, &monitor_dev_ktype,
				system_monitor->kobj, "%s", dev_name(dev));
	if (ret)
		dev_err(dev, "Failed to init dev kobj to system_monitor\n");

	if (sysfs_create_group(&info->kobj, &dev_monitor_sysfs_attr_group))
		dev_err(dev, "failed to create dev_monitor status sysfs\n");

	info->status_nb.notifier_call = th1520_dev_status_notifier;
	th1520_register_dev_status_notifier(info, &info->status_nb);

	down_write(&mdev_list_lock);
	list_add(&info->node, &monitor_dev_list);
	up_write(&mdev_list_lock);

	return info;
}
EXPORT_SYMBOL(th1520_system_monitor_register);

void th1520_system_monitor_unregister(struct monitor_dev_info *info)
{
	if (!info)
		return;

	down_write(&mdev_list_lock);
	list_del(&info->node);
	up_write(&mdev_list_lock);

	th1520_unregister_dev_status_notifier(info, &info->status_nb);

	sysfs_remove_group(&info->kobj, &dev_monitor_sysfs_attr_group);
	kobject_put(&info->kobj);

	if (info->devp->type == MONITOR_TPYE_CPU) {
		freq_qos_remove_request(&info->min_sta_freq_req);
	} else {
		dev_pm_qos_remove_request(&info->dev_min_freq_req);
	}

	mutex_destroy(&info->status_lock);
	kfree(info);
}
EXPORT_SYMBOL(th1520_system_monitor_unregister);

static int th1520_system_status_common_limit_freq(struct monitor_dev_info *info, unsigned long status)
{
	int ret = 0;
	unsigned long target_freq = 0;

	if (info->dev_qos_high_limit && (status & SYS_STATUS_PERFORMANCE)) {
		target_freq = info->dev_qos_high_limit;
		goto out;
	}

	if (info->video_4k_limit && (status & SYS_STATUS_VIDEO4K_LIMIT))
		target_freq = info->video_4k_limit;
	else if (info->video_2k_limit && (status & SYS_STATUS_VIDEO2K_LIMIT))
		target_freq = info->video_2k_limit;
	else if (info->video_1080p_limit && (status & SYS_STATUS_VIDEO_LIMIT))
		target_freq = info->video_1080p_limit;

	if (info->vsync_limit && (status & SYS_STATUS_VSYNC)) {
		if(info->vsync_limit > target_freq)
			target_freq = info->vsync_limit;
	}

	if (!target_freq)
		target_freq = info->dev_qos_low_limit;

out:
	spin_lock(&info->minfreq_lock);

	if (target_freq == info->cur_min_limit) {
		spin_unlock(&info->minfreq_lock);
		return 0;
	}

	info->cur_min_limit = target_freq;

	spin_unlock(&info->minfreq_lock);

	if (target_freq)
		ret = th1520_monitor_dev_rate_adjust(info, target_freq);
	else
		ret = th1520_monitor_dev_rate_adjust(info, FREQ_QOS_MIN_DEFAULT_VALUE);

	return ret;
}

static void th1520_system_status_cpu_limit_freq(struct monitor_dev_info *info, unsigned long status)
{
	if (info->dev_qos_high_limit && (status & SYS_STATUS_CPU))
		th1520_monitor_dev_rate_adjust(info, info->dev_qos_high_limit);
	else
		th1520_system_status_common_limit_freq(info, status);
}

static void th1520_system_status_dev_limit_freq(struct monitor_dev_info *info, unsigned long status)
{
	if(info->devp->qos_rate_adjust)
	 	info->devp->qos_rate_adjust(info, status);
	else
		th1520_system_status_common_limit_freq(info, status);
}

static void th1520_system_status_limit_freq(unsigned long status)
{
	struct monitor_dev_info *info;

	down_read(&mdev_list_lock);

	list_for_each_entry(info, &monitor_dev_list, node) {
		if (info->devp->type == MONITOR_TPYE_CPU)
			th1520_system_status_cpu_limit_freq(info, status);
		else
			th1520_system_status_dev_limit_freq(info, status);
	}

	up_read(&mdev_list_lock);
}

static int th1520_system_status_notifier(struct notifier_block *nb,
					   unsigned long status, void *ptr)
{
	th1520_system_status_limit_freq(status);

	return NOTIFY_OK;
}

static int th1520_system_monitor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	system_monitor = devm_kzalloc(dev, sizeof(struct system_monitor),
				      GFP_KERNEL);
	if (!system_monitor) {
		return -ENOMEM;
	}
	system_monitor->dev = dev;

	system_monitor->kobj = kobject_create_and_add("system_monitor", NULL);
	if (!system_monitor->kobj){
		return -ENOMEM;
	}

	if (sysfs_create_file(system_monitor->kobj, &status.attr))
		dev_err(dev, "failed to create system status sysfs\n");

	if (sysfs_create_file(system_monitor->kobj, &video_info.attr))
		dev_err(dev, "failed to create system video_info sysfs\n");

	system_monitor->status_nb.notifier_call =
		th1520_system_status_notifier;
	th1520_register_system_status_notifier(&system_monitor->status_nb);

	dev_info(dev, "system_monitor init done!\n");

	return 0;
}

static const struct of_device_id th1520_system_monitor_of_match[] = {
	{
		.compatible = "th1520,system-monitor",
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, th1520_system_monitor_of_match);

static struct platform_driver th1520_system_monitor_driver = {
	.probe	= th1520_system_monitor_probe,
	.driver = {
		.name	= "th1520-system-monitor",
		.of_match_table = th1520_system_monitor_of_match,
	},
};
module_platform_driver(th1520_system_monitor_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Hengqiang Ming <minghq@linux.alibaba.com>");
MODULE_DESCRIPTION("th1520 system monitor driver");

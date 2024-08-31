
#ifndef __SOC_TH1520_SYSTEM_MONITOR_H
#define __SOC_TH1520_SYSTEM_MONITOR_H

#include <linux/pm_opp.h>
#include <linux/pm_qos.h>

enum monitor_dev_type {
	MONITOR_TPYE_CPU = 0,	/* CPU */
	MONITOR_TPYE_DEV,	/* GPU, NPU, VPU and so on */
};

struct monitor_dev_info {
	struct device *dev;
	struct monitor_dev_profile *devp;
	struct list_head node;
	struct kobject kobj;
	struct blocking_notifier_head dev_notifier;
	struct notifier_block status_nb;
	struct mutex status_lock;
	spinlock_t minfreq_lock;

	struct dev_pm_qos_request dev_min_freq_req;
	struct freq_qos_request min_sta_freq_req;

	unsigned long dev_monitor_status;
	unsigned long ref_count[32];

	unsigned long dev_qos_low_limit;
	unsigned long dev_qos_high_limit;
	unsigned int dev_qos_mid_limit;
	unsigned int video_4k_limit;
	unsigned int video_2k_limit;
	unsigned int video_1080p_limit;
	unsigned int vsync_limit;
	unsigned int cur_min_limit;
};

struct monitor_dev_profile {
	enum monitor_dev_type type;
	void *data;
	bool is_checked;
	int (*qos_rate_adjust)(struct monitor_dev_info *info, unsigned long status);
};

#if IS_ENABLED(CONFIG_TH1520_SYSTEM_MONITOR)
struct monitor_dev_info *
th1520_system_monitor_register(struct device *dev, struct monitor_dev_profile *devp);
int th1520_monitor_dev_rate_adjust(struct monitor_dev_info *info, unsigned long val);
void th1520_system_monitor_unregister(struct monitor_dev_info *info);
#else
static struct monitor_dev_info *
th1520_system_monitor_register(struct device *dev, struct monitor_dev_profile *devp)
{
	return ERR_PTR(-ENOTSUPP);
};
static int
th1520_system_status_common_limit_freq(struct monitor_dev_info *info, unsigned long status)
{
	return 0;
};
static int th1520_monitor_dev_rate_adjust(struct monitor_dev_info *info, unsigned long val)
{
	return 0;
};
static void th1520_system_monitor_unregister(struct monitor_dev_info *info)
{
};
#endif

#endif
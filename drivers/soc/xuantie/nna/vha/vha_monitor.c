#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/version.h>
#include <uapi/vha.h>
#include "vha_common.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Imagination");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
MODULE_IMPORT_NS(VHA_CORE);
#endif

#define VHA_MONITOR_NUM_PRIO 3

static uint32_t cmd_count[VHA_MONITOR_NUM_PRIO] = {0};
static bool check_session_switch = false;
uint32_t last_session_id = 0;
uint32_t last_priority = 0;
bool last_last_subsegment = true;

static void observe_enqueue_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint32_t priority)
{
	cmd_count[priority]++;
}

static void observe_submitted_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid, bool last_subsegment, uint32_t priority)
{
	uint32_t i = 0;
	for (i = priority + 1; i < VHA_MONITOR_NUM_PRIO; i++) {
		if (cmd_count[i] > 0 ) {
			pr_err("ERROR: low priority inference submitted while higher priority one queued!!!\n");
		}
	}

	if (check_session_switch && last_session_id != sessionid && last_priority >= priority) {
		pr_err("ERROR: Session changed before the last subsegment!!!\n");
	}

	if (priority > last_priority && !last_last_subsegment) {
		pr_debug("vha low priority subsegment has been preempted\n");
	}

	if (last_subsegment)
		check_session_switch = false;
	else {
		last_session_id = sessionid;
		last_priority = priority;
		check_session_switch = true;
	}
	last_last_subsegment = last_subsegment;
}

static void observe_completed_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid,
		uint64_t status, uint64_t cycles, uint64_t mem_usage, uint32_t priority)
{
	cmd_count[priority]--;
}

static void observe_canceled_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint32_t priority)
{
	cmd_count[priority]--;
	check_session_switch = false;
	last_last_subsegment = true;
}

static void observe_error_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint64_t status)
{
	check_session_switch = false;
	last_last_subsegment = true;
}

static int vhamonitor_open(struct inode *inode, struct file *filep)
{
	return nonseekable_open(inode, filep);
}
static int vhamonitor_release(struct inode *inode, struct file *filep)
{
	return 0;
}

static const struct file_operations vhamonitor_fops = {
	.owner   = THIS_MODULE,
	.open    = vhamonitor_open,
	.release = vhamonitor_release,
	.llseek  = no_llseek,
};

static struct miscdevice vhamonitormisc = {
	.name = VHA_SCOPE_DEV_NAME,
	.fops = &vhamonitor_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

static int __init vhamonitor_init(void)
{
	int ret = 0;

	/* register as observer of VHA events */
	vha_observe_event_enqueue(&observe_enqueue_event);
	vha_observe_event_submit(&observe_submitted_event);
	vha_observe_event_complete(&observe_completed_event);
	vha_observe_event_cancel(&observe_canceled_event);
	vha_observe_event_error(&observe_error_event);

	ret = misc_register(&vhamonitormisc);
	if (ret)
		pr_err("%s: misc_register failed\n", __func__);

	return ret;
}
static void __exit vhamonitor_exit(void)
{
	misc_deregister(&vhamonitormisc);

	/* unregister for VHA events */
	vha_observe_event_enqueue(NULL);
	vha_observe_event_submit(NULL);
	vha_observe_event_complete(NULL);
	vha_observe_event_cancel(NULL);
	vha_observe_event_error(NULL);
}
module_init(vhamonitor_init);
module_exit(vhamonitor_exit);

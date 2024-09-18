#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/atomic.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <uapi/vha.h>
#include "vha_common.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Imagination");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
MODULE_IMPORT_NS(VHA_CORE);
#endif

static uint64_t gettimestamp64(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,17,0)
	struct timespec64 ts;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,18,0)
	ktime_get_boottime_ts64(&ts);
#else /* < LINUX_VERSION_CODE < 4.18.0 & LINUX_VERSION_CODE >= 3.17.0 */
	getnstimeofday64(&ts);
#endif

#else /* < LINUX_VERSION_CODE < 3.17.0 */
	struct timespec64 ts;

	ktime_get_ts64(&ts);
#endif
	return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/*
 * This kernel module is dependent on vha.ko. it hooks into vha.ko, in order to
 * provide timing information about HW events to PVRScope
 */
/* fifo contains up to 2k events */
#define NUM_FIFO_RECORDS 2048

/* for simplicity, only allow a single connection to this device */
static atomic_t open_count;

/* unique sequence number for each event, for detecting loss of events */
static atomic_t seqno;

/* the event fifo contains VHA timing event information */
static DEFINE_KFIFO(vha_events_fifo, struct vha_timing_data, NUM_FIFO_RECORDS);

/* push event data into the fifo */
static void add_timing_event(uint32_t devid, int type, uint32_t cycles)
{
	struct vha_timing_data record;
	uint64_t timestamp = gettimestamp64();

	record.evt_type = VHA_EVENT_TIMING;
	record.seqno = atomic_inc_return(&seqno);
	record.timestamp_hi = timestamp >> 32;
	record.timestamp_lo = timestamp & 0xffffffff;
	record.dev_id = devid;
	record.type = type;
	record.cycles = cycles;
	/* should be filled from vha_trace_ctx.
	 * at kernel level it could use task_tgid_nr(current);
	 */
	record.pid = 0;
	/*record.tid = (unsigned)task_pid_nr(current); */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
	if (kfifo_put(&vha_events_fifo, &record) != 1)
#else
	if (kfifo_put(&vha_events_fifo, record) != 1)
#endif
		pr_err("%s: failed to record event into fifo\n", __func__);
}

/* push SUBMIT event into the fifo */
static void observe_enqueue_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint32_t priority)
{
	add_timing_event(devid, VHA_EVENT_TYPE_ENQUEUE, 0);
}

/* push SUBMIT event into the fifo */
static void observe_submitted_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid, bool last_subsegment, uint32_t priority)
{
	add_timing_event(devid, VHA_EVENT_TYPE_SUBMIT, 0);
}

/* push COMPLETED event into the fifo */
static void observe_completed_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid,
		uint64_t status, uint64_t cycles, uint64_t mem_usage, uint32_t priority)
{
	/* only successful events are logged */
	if (status == 0) {
		add_timing_event(devid, VHA_EVENT_TYPE_COMPLETE, (uint32_t)cycles);
	}
}

/* push ERROR event into the fifo */
static void observe_error_event(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint64_t status)
{
	add_timing_event(devid, VHA_EVENT_TYPE_ERROR, 0);
}

static int vhainfo_open(struct inode *inode, struct file *filep)
{
	struct miscdevice *misc = filep->private_data;

	if (atomic_inc_return(&open_count) != 1) {
		dev_err(misc->this_device, "%s: 1 connection already\n",
			__func__);
		atomic_dec(&open_count);
		return -EALREADY;
	}

	/* register as observer of VHA events */
	vha_observe_event_enqueue(&observe_enqueue_event);
	vha_observe_event_submit(&observe_submitted_event);
	vha_observe_event_complete(&observe_completed_event);
	vha_observe_event_error(&observe_error_event);

	return nonseekable_open(inode, filep);
}
static int vhainfo_release(struct inode *inode, struct file *filep)
{
	/* unregister for VHA events */
	vha_observe_event_submit(NULL);
	vha_observe_event_complete(NULL);
	atomic_dec(&open_count);

	return 0;
}

/* read from event fifo:
 * returns number of chars added to buffer
 * or 0 if fifo empty
 * or -ve error code
 */
static ssize_t vhainfo_read(struct file *filep,
			char __user *buf, size_t count, loff_t *ppos)
{
	unsigned int copied = 0;
	int ret;

	ret = kfifo_to_user(&vha_events_fifo, buf, count, &copied);

	if(copied > 0)
		printk("read(): wants %lu read %d\n", count, copied);
	if (ret)
		return ret;
	return copied;
}


static const struct file_operations vhainfo_fops = {
	.owner   = THIS_MODULE,
	.open    = vhainfo_open,
	.release = vhainfo_release,
	.read    = vhainfo_read,
	.llseek  = no_llseek,
};

static struct miscdevice vhainfomisc = {
	.name = VHA_SCOPE_DEV_NAME,
	.fops = &vhainfo_fops,
	.minor = MISC_DYNAMIC_MINOR,
};

static int __init vhainfo_init(void)
{
	int ret = 0;

	ret = misc_register(&vhainfomisc);
	if (ret)
		pr_err("%s: misc_register failed\n", __func__);

	return ret;
}
static void __exit vhainfo_exit(void)
{
	misc_deregister(&vhainfomisc);
}
module_init(vhainfo_init);
module_exit(vhainfo_exit);

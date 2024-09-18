#ifndef __DEC_DEVFREQ_H__
#define __DEC_DEVFREQ_H__

#include <linux/spinlock.h>
#include <linux/ktime.h>
struct devfreq;
struct opp_table;

struct encoder_devfreq {
	int busy_count;
    struct devfreq *df;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
    struct opp_table *clkname_opp_table;
#else
	int token;
#endif
	bool opp_of_table_added;
	bool update_freq_flag;
	unsigned long next_target_freq;
	unsigned long cur_devfreq;
	unsigned long max_freq;
	wait_queue_head_t target_freq_wait_queue;
#ifdef CONFIG_TH1520_SYSTEM_MONITOR
	struct monitor_dev_info *mdev_info;
#endif

	ktime_t busy_time;
	ktime_t idle_time;
	ktime_t time_last_update;
	ktime_t based_maxfreq_busy_time;
	ktime_t based_maxfreq_last_busy_t;
	int busy_record_count;

	/*
	 * Protect busy_time, idle_time, time_last_update and busy_count
	 * because these can be updated concurrently, for example by the GP
	 * and PP interrupts.
	 */
	spinlock_t lock;

	struct mutex clk_mutex; /* clk freq changed lock,for vdec cannot changed clk rate in hw working*/
};
void encoder_devfreq_fini(struct device *dev);
int encoder_devfreq_init(struct device *dev) ;
void encoder_devfreq_record_busy(struct encoder_devfreq *devfreq);
void encoder_devfreq_record_idle(struct encoder_devfreq *devfreq);
struct encoder_devfreq * encoder_get_devfreq_priv_data(void);
int encoder_devfreq_resume(struct encoder_devfreq *devfreq);
int encoder_devfreq_suspend(struct encoder_devfreq *devfreq);
int encoder_devfreq_set_rate(struct device * dev);

void encoder_dev_clk_lock(void);
void encoder_dev_clk_unlock(void);
#endif

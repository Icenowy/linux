/*!
 *****************************************************************************
 *
 * @File       vha_devfreq.c
 * ---------------------------------------------------------------------------
 *
 * Copyright (C) 2020 Alibaba Group Holding Limited
 *
 *****************************************************************************/
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/regulator/consumer.h>
#include <linux/devfreq.h>
#include <linux/pm_opp.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include "vha_common.h"
#ifdef CONFIG_PM_DEVFREQ
#include <../drivers/devfreq/governor.h>
#endif

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
#include <soc/xuantie/th1520_system_monitor.h>
#include <dt-bindings/soc/th1520_system_status.h>
#endif

#ifdef CONFIG_DEVFREQ_THERMAL
#include <linux/devfreq_cooling.h>
#endif

/* Default constants for DevFreq-Simple-Ondemand (DFSO) */
#define VHA_DEVFREQ_GOVERNOR_NAME "vha_ondemand"
#define DFSO_UPTHRESHOLD	(90)
#define DFSO_DOWNDIFFERENCTIAL	(5)
#define TH1520_NPUFREQ_PCLKNUM	3

struct governor_vhaondemand_date {
	unsigned int upthreshold;
    unsigned int downdifferential;
};

struct vhadevfreq_load_data {
    struct TIMESPEC old_mark;
    struct TIMESPEC new_mark;
    uint64_t total_proc_us;
};

struct vha_devfreq_device {
    struct devfreq *devfreq;
	struct device *dev;
    struct opp_table *opp_table;

    struct governor_vhaondemand_date vhademand_date;
    struct vhadevfreq_load_data vha_load_data;
    unsigned long current_freq;

	struct mutex lock;

#ifdef CONFIG_DEVFREQ_THERMAL
	struct thermal_cooling_device *devfreq_cooling;
#endif

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
    struct monitor_dev_info *mdev_info;
#endif
};

enum TH1520_NPUFREQ_PARENT_CLKS {
    NPU_CCLK,
	GMAC_PLL_FOUTPOSTDIV,
	NPU_CCLK_OUT_DIV,
};

static int num_clks;
static struct clk_bulk_data clks[] = {
    { .id = "cclk" },
	{ .id = "gmac_pll_foutpostdiv" },
	{ .id = "npu_cclk_out_div" },
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
static int vha_devfreq_opp_helper(struct dev_pm_set_opp_data *data)
{
    struct device *dev = data->dev;
    struct clk *clk_vha = data->clk;
    unsigned long freq = data->new_opp.rate;
    unsigned long old_freq = data->old_opp.rate;
    unsigned long curr_freq;
    int ret = 0;

    if (freq == old_freq) {
        return ret;
    }

    ret = strcmp(__clk_get_name(clk_get_parent(clk_vha)),
	            __clk_get_name(clks[NPU_CCLK_OUT_DIV].clk));

    if (!ret && freq < clk_get_rate(clks[GMAC_PLL_FOUTPOSTDIV].clk))
    {
        clk_set_parent(clk_vha, clks[GMAC_PLL_FOUTPOSTDIV].clk);

        ret = clk_set_rate(clks[NPU_CCLK_OUT_DIV].clk, freq);
        if (ret) {
            dev_err(dev, "%s: Failed to set NPU_CCLK_OUT_DIV freq: %d.\n",
                __func__, ret);
            ret = -EINVAL;
        }
        udelay(1);

        clk_set_parent(clk_vha, clks[NPU_CCLK_OUT_DIV].clk);

        goto check_clk;
    }

    ret = clk_set_rate(clk_vha, freq);
    if (ret) {
        dev_err(dev, "%s: Failed to set freq: %d.\n", __func__, ret);
        ret = -EINVAL;
    }

check_clk:
    curr_freq = clk_get_rate(clk_vha);
    if (curr_freq != freq) {
        dev_err(dev, "Get wrong frequency, Request %lu, Current %lu.\n",
			freq, curr_freq);
		ret = -EINVAL;
    }

    return ret;
}
#else
static int vha_devfreq_opp_helper(struct device *dev, struct opp_table *opp_table,
			struct dev_pm_opp *opp, void *data, bool scaling_down)
{
    struct clk *clk_vha = clks[NPU_CCLK].clk;
    unsigned long freq = *(unsigned long *)data;
    unsigned long curr_freq;
    int ret = 0;

    ret = strcmp(__clk_get_name(clk_get_parent(clk_vha)),
	            __clk_get_name(clks[NPU_CCLK_OUT_DIV].clk));

    if (!ret && freq < clk_get_rate(clks[GMAC_PLL_FOUTPOSTDIV].clk))
    {
        clk_set_parent(clk_vha, clks[GMAC_PLL_FOUTPOSTDIV].clk);

        ret = clk_set_rate(clks[NPU_CCLK_OUT_DIV].clk, freq);
        if (ret) {
            dev_err(dev, "%s: Failed to set NPU_CCLK_OUT_DIV freq: %d.\n",
                __func__, ret);
            ret = -EINVAL;
        }
        udelay(1);

        clk_set_parent(clk_vha, clks[NPU_CCLK_OUT_DIV].clk);

        goto check_clk;
    }

    ret = clk_set_rate(clk_vha, freq);
    if (ret) {
        dev_err(dev, "%s: Failed to set freq: %d.\n", __func__, ret);
        ret = -EINVAL;
    }

check_clk:
    curr_freq = clk_get_rate(clk_vha);
    if (curr_freq != freq) {
        dev_err(dev, "Get wrong frequency, Request %lu, Current %lu.\n",
			freq, curr_freq);
		ret = -EINVAL;
    }

    return ret;
}
#endif

static int vhafreq_target(struct device *dev, unsigned long *freq,
                     u32 flags)
{
    struct vha_devfreq_device *vhafreq_dev = vha_devfreq_get_drvdata(dev);
    struct dev_pm_opp *opp;
    int ret = 0;

    opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
        dev_err(dev, "Failed to find opp for %lu Hz.\n", *freq);
		return PTR_ERR(opp);
    }
    dev_pm_opp_put(opp);

    mutex_lock(&vhafreq_dev->lock);

    ret = dev_pm_opp_set_rate(dev, *freq);
    if (!ret) {
		if (vhafreq_dev->devfreq)
			vhafreq_dev->devfreq->last_status.current_frequency = *freq;
	} else {
        dev_err(dev, "Failed to set opp for %lu Hz.\n", *freq);
    }

    mutex_unlock(&vhafreq_dev->lock);

    dev_dbg(dev, "%s: set the target freq : %lu.\n", __func__, *freq);

    return ret;
}

static int vhafreq_get_dev_status(struct device *dev,
					 struct devfreq_dev_status *stat)
{
    struct vha_devfreq_device *vhafreq_dev = vha_devfreq_get_drvdata(dev);
    uint64_t busytime, proc_total_time, proc_cur_time;

    mutex_lock(&vhafreq_dev->lock);

    vha_get_cnntotal_proc_us(dev, &proc_total_time, &proc_cur_time);

    /* Calculate the busy_time */
    busytime = proc_total_time + proc_cur_time;
    if (busytime < vhafreq_dev->vha_load_data.total_proc_us) {
        busytime = 0;
    } else {
        busytime = busytime - vhafreq_dev->vha_load_data.total_proc_us;
    }

    vhafreq_dev->vha_load_data.total_proc_us = proc_total_time + proc_cur_time;

    /* Calculate the total_time */
    GETNSTIMEOFDAY(&vhafreq_dev->vha_load_data.new_mark);
    if (!get_timespan_us(&vhafreq_dev->vha_load_data.old_mark, 
                        &vhafreq_dev->vha_load_data.new_mark, &proc_cur_time)) 
		return -EINVAL;

    vhafreq_dev->vha_load_data.old_mark = vhafreq_dev->vha_load_data.new_mark;

    /* correct the busytime */
    if (busytime > proc_cur_time) {
        dev_dbg(dev,"busytime :%lu bigger, totaltime :%lu .\n", busytime, proc_cur_time);
        busytime = proc_cur_time;
    } else if (busytime < 0) {
        busytime = 0;
    } 

    stat->busy_time = busytime;
    stat->total_time = proc_cur_time;

    mutex_unlock(&vhafreq_dev->lock);
    
    return 0;
}

static int vhafreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
    unsigned long current_freq;

    current_freq = clk_get_rate(clks[NPU_CCLK].clk);
    *freq = current_freq;

	return 0;
}

static struct devfreq_dev_profile devfreq_vha_profile = {
	.polling_ms	= 5,
	.target		= vhafreq_target,
	.get_dev_status	= vhafreq_get_dev_status,
	.get_cur_freq	= vhafreq_get_cur_freq,
};

#ifdef CONFIG_PM_DEVFREQ
static int devfreq_vha_ondemand_func(struct devfreq *df, unsigned long *freq)
{
    int err;
	struct devfreq_dev_status *stat;
	unsigned long long a, b;
    struct governor_vhaondemand_date *data = df->data;
    unsigned int dfso_upthreshold = DFSO_UPTHRESHOLD;
	unsigned int dfso_downdifferential = DFSO_DOWNDIFFERENCTIAL;

    if (data) {
        if (data->upthreshold)
			dfso_upthreshold = data->upthreshold;
		if (data->downdifferential)
			dfso_downdifferential = data->downdifferential;
    }

    if (dfso_upthreshold > 100 ||
	    dfso_upthreshold < dfso_downdifferential)
		return -EINVAL;

    err = devfreq_update_stats(df);
    if (err)
		return err;

    stat = &df->last_status;

    /* Assume MAX if it is going to be divided by zero */
	if (stat->total_time == 0) {
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

    /* Prevent overflow */
	if (stat->busy_time >= (1 << 24) || stat->total_time >= (1 << 24)) {
		stat->busy_time >>= 7;
		stat->total_time >>= 7;
	}

	/* Set MAX if it's busy enough */
	if (stat->busy_time * 100 >
	    stat->total_time * dfso_upthreshold) {
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

	/* Set MAX if we do not know the initial frequency */
	if (stat->current_frequency == 0) {
		*freq = DEVFREQ_MAX_FREQ;
		return 0;
	}

	/* Keep the current frequency */
	if (stat->busy_time * 100 >
	    stat->total_time * (dfso_upthreshold - dfso_downdifferential)) {
		*freq = stat->current_frequency;
		return 0;
	}

	/* Set the desired frequency based on the load */
	a = stat->busy_time;
	a *= stat->current_frequency;
	b = div_u64(a, stat->total_time);
	b *= 100;
	b = div_u64(b, (dfso_upthreshold - dfso_downdifferential / 2));
	*freq = (unsigned long) b;

	return 0;
}

static void devfreq_status_update(struct device *dev) {
    struct vha_devfreq_device *vhafreq_dev = vha_devfreq_get_drvdata(dev);
    struct vhadevfreq_load_data *load_date = &vhafreq_dev->vha_load_data;
    uint64_t cur_proc_us, cnntotal_time;

    GETNSTIMEOFDAY(&load_date->new_mark);
    load_date->old_mark = load_date->new_mark;

    vha_get_cnntotal_proc_us(dev, &cnntotal_time, &cur_proc_us);

    load_date->total_proc_us = cnntotal_time + cur_proc_us;
}

static int devfreq_vha_ondemand_handler(struct devfreq *devfreq,
					  unsigned int event, void *data)
{
    struct device *dev = devfreq->dev.parent;

    switch (event) {
	case DEVFREQ_GOV_START:
        devfreq_status_update(dev);
		devfreq_monitor_start(devfreq);
		break;

	case DEVFREQ_GOV_STOP:
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_UPDATE_INTERVAL:
		devfreq_update_interval(devfreq, (unsigned int *)data);
		break;

	case DEVFREQ_GOV_SUSPEND:
		devfreq_monitor_suspend(devfreq);
		break;

	case DEVFREQ_GOV_RESUME:
        devfreq_status_update(dev);
		devfreq_monitor_resume(devfreq);
		break;

	default:
		break;
	}
	return 0;
}

static struct devfreq_governor devfreq_vha_ondemand = {
	.name = "vha_ondemand",
	.get_target_freq = devfreq_vha_ondemand_func,
	.event_handler = devfreq_vha_ondemand_handler,
};
#endif

#ifdef CONFIG_DEVFREQ_THERMAL
static struct devfreq_cooling_power vha_cooling_power = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
	.get_static_power = NULL,
    .dyn_power_coeff = 1000,
#endif
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
static int opp_config_id = 0;
struct dev_pm_opp_config vha_dev_config = {
    .config_clks = vha_devfreq_opp_helper,
};
#endif

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
static int vha_qos_rate_adjust(struct monitor_dev_info *info, unsigned long status)
{
    int ret = 0;

    if (status & (SYS_STATUS_DSP | SYS_STATUS_PERFORMANCE))
        ret = th1520_monitor_dev_rate_adjust(info, FREQ_QOS_MAX_DEFAULT_VALUE);
    else
        ret = th1520_monitor_dev_rate_adjust(info, FREQ_QOS_MIN_DEFAULT_VALUE);

    return ret;
}

static struct monitor_dev_profile npu_dev_monitor = {
	.type = MONITOR_TPYE_DEV,
	.qos_rate_adjust = vha_qos_rate_adjust,
};
#endif

static int vha_devfreq_opp_init(struct device *dev)
{
    struct opp_table *opp_table = NULL, *reg_opp_table = NULL, *clk_opp_table = NULL;
    struct vha_devfreq_device *vhafreq_dev = vha_devfreq_get_drvdata(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
    const char * const reg_names[] = {"soc_dvdd08_ap", NULL};
    const char *clk_names[] = { "cclk", NULL };
    int ret;

    vha_dev_config.regulator_names = reg_names;
    vha_dev_config.clk_names = clk_names;

    opp_config_id = dev_pm_opp_set_config(dev, &vha_dev_config);
    if (opp_config_id < 0) {
        dev_err(dev, "Failed to set opp_config.\n");
        ret = opp_config_id;
        return ret;
    }
#else
    const char * const reg_names[] = {"soc_dvdd08_ap"};
    int ret;

    clk_opp_table = dev_pm_opp_set_clkname(dev, "cclk");
	if (IS_ERR(clk_opp_table)) {
        dev_err(dev, "Failed to set opp clkname.\n");
		return PTR_ERR(clk_opp_table);
	}

    reg_opp_table = dev_pm_opp_set_regulators(dev, reg_names, 1);
    if (IS_ERR(reg_opp_table)) {
		dev_err(dev, "Failed to set regulators.\n");
		ret = PTR_ERR(reg_opp_table);
        goto clk_opp_table_put;
	}

    opp_table = dev_pm_opp_register_set_opp_helper(dev, vha_devfreq_opp_helper);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "Failed to set vha opp helper.\n");
		ret = PTR_ERR(opp_table);
        goto reg_opp_table_put;
	}

    vhafreq_dev->opp_table = opp_table;
#endif

    ret = dev_pm_opp_of_add_table(dev);
    if(ret){
		dev_err(dev, "Failed to add vha opp table.\n");
		goto opp_helper_unregist;
	}

    return 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
opp_helper_unregist:
    dev_pm_opp_clear_config(opp_config_id);
#else
opp_helper_unregist:
    if(opp_table)
        dev_pm_opp_unregister_set_opp_helper(opp_table);
reg_opp_table_put:
    if(reg_opp_table)
        dev_pm_opp_put_regulators(reg_opp_table);
clk_opp_table_put:
    if(clk_opp_table)
        dev_pm_opp_put_clkname(clk_opp_table);
#endif
    return ret;
}

int vha_devfreq_init(struct device *dev)
{
    struct vha_devfreq_device *devfreq_vhadev;
	struct devfreq_dev_profile *dp = &devfreq_vha_profile;
    int ret = 0;

    devfreq_vhadev = devm_kzalloc(dev, sizeof(struct vha_devfreq_device), GFP_KERNEL);
	if (!devfreq_vhadev)
		return -ENOMEM;

    devfreq_vhadev->dev = dev;
    mutex_init(&devfreq_vhadev->lock);
    vha_dev_add_devfreq(dev, devfreq_vhadev);

#ifdef CONFIG_PM_DEVFREQ
	ret = devfreq_add_governor(&devfreq_vha_ondemand);
	if (ret) {
		dev_err(dev, "%s: Failed to add vha_ondemand governor.\n", __func__);
		goto free_freqdev_dev;
	}
#endif

    num_clks = TH1520_NPUFREQ_PCLKNUM;
	ret = clk_bulk_get(dev, num_clks, clks);
	if (ret) {
        dev_err(dev, "%s: Failed to register clk_bulk_get.\n", __func__);
        goto free_freqdev_dev;
    }

    dp->initial_freq = clk_get_rate(clks[NPU_CCLK].clk);

    ret = vha_devfreq_opp_init(dev);
    if (ret) {
        dev_err(dev, "%s: Failed to vha_devfreq_opp_init.\n", __func__);
        goto vha_clks_put;
    }

    devfreq_vhadev->devfreq = devm_devfreq_add_device(dev, dp,
										VHA_DEVFREQ_GOVERNOR_NAME,
										&devfreq_vhadev->vhademand_date);
    if (IS_ERR_OR_NULL(devfreq_vhadev->devfreq)) {
		dev_err(dev, "%s: Failed to register vha to devfreq.\n", __func__);
		ret = PTR_ERR(devfreq_vhadev->devfreq);
		goto free_opp_table;
	}

	ret = devm_devfreq_register_opp_notifier(dev, devfreq_vhadev->devfreq);
	if (ret < 0) {
		dev_err(dev, "%s: Failed to register vha to opp notifier.\n", __func__);
		goto opp_notifier_failed;
	}

#ifdef CONFIG_DEVFREQ_THERMAL
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
    if (of_property_read_u32(dev->of_node, "dynamic-power-coefficient",
					(u32 *)&vha_cooling_power.dyn_power_coeff))
        dev_err(dev, "%s: Failed to read dynamic power coefficient property.\n", __func__);
#endif
    devfreq_vhadev->devfreq_cooling = of_devfreq_cooling_register_power(
		dev->of_node, devfreq_vhadev->devfreq, &vha_cooling_power);
	if (IS_ERR_OR_NULL(devfreq_vhadev->devfreq_cooling)){
        dev_err(dev, "%s: Failed to register vha to devfreq_cooling.\n", __func__);
        goto cooling_failed;
    }
#endif

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
    npu_dev_monitor.data = devfreq_vhadev->devfreq;
    devfreq_vhadev->mdev_info = th1520_system_monitor_register(dev, &npu_dev_monitor);
	if (IS_ERR(devfreq_vhadev->mdev_info))
		devfreq_vhadev->mdev_info = NULL;
#endif
    dev_info(dev, "%s: Success to register the NPU to DevFreq.\n", __func__);

    return 0;

#ifdef CONFIG_DEVFREQ_THERMAL
cooling_failed:
	devfreq_unregister_opp_notifier(dev, devfreq_vhadev->devfreq);
#endif
opp_notifier_failed:
    devm_devfreq_remove_device(dev, devfreq_vhadev->devfreq);
free_opp_table:
    dev_pm_opp_of_remove_table(dev);
vha_clks_put:
    clk_bulk_put(num_clks, clks);
free_freqdev_dev:
    devm_kfree(dev, devfreq_vhadev);
    return ret;
}

void vha_devfreq_term(struct device *dev)
{
    struct vha_devfreq_device *devfreq_vhadev = vha_devfreq_get_drvdata(dev);

    if (devfreq_vhadev){
#ifdef CONFIG_TH1520_SYSTEM_MONITOR
        if(devfreq_vhadev->mdev_info)
            th1520_system_monitor_unregister(devfreq_vhadev->mdev_info);
#endif

#ifdef CONFIG_DEVFREQ_THERMAL
        if (devfreq_vhadev->devfreq_cooling){
            devfreq_cooling_unregister(devfreq_vhadev->devfreq_cooling);
        }
#endif
        if (devfreq_vhadev->devfreq) {
            devfreq_unregister_opp_notifier(dev, devfreq_vhadev->devfreq);

            devm_devfreq_remove_device(dev, devfreq_vhadev->devfreq);

            dev_pm_opp_of_remove_table(devfreq_vhadev->dev);

#ifdef CONFIG_PM_DEVFREQ
            devfreq_remove_governor(&devfreq_vha_ondemand);
#endif
	    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
            dev_pm_opp_clear_config(opp_config_id);
#else
        if (devfreq_vhadev->opp_table) {
            dev_pm_opp_put_clkname(devfreq_vhadev->opp_table);
            dev_pm_opp_unregister_set_opp_helper(devfreq_vhadev->opp_table);
        }
#endif

        clk_bulk_put(num_clks, clks);

        devm_kfree(dev, devfreq_vhadev);
    }
}

int vha_devfreq_suspend(struct device *dev)
{
    struct vha_devfreq_device *devfreq_vhadev = vha_devfreq_get_drvdata(dev);
    int ret = 0;

    if (devfreq_vhadev){
        ret = devfreq_suspend_device(devfreq_vhadev->devfreq);
        if (ret < 0){
            dev_err(dev, "%s: Failed to suspend the vha_devfreq.\n", __func__);
        }
    }
    
    return ret;
}

int vha_devfreq_resume(struct device *dev)
{
    struct vha_devfreq_device *devfreq_vhadev = vha_devfreq_get_drvdata(dev);
    int ret = 0;

    if (devfreq_vhadev){
        ret = devfreq_resume_device(devfreq_vhadev->devfreq);
        if (ret < 0){
            dev_err(dev, "%s: Failed to resume the vha_devfreq.\n", __func__);
        }
    }

    return ret;
}
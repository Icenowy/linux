/*!
 *****************************************************************************
 *
 * @File       vha_common.h
 * ---------------------------------------------------------------------------
 *
 * Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 ("GPL")in which case the provisions of
 * GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms
 * of GPL, and not to allow others to use your version of this file under the
 * terms of the MIT license, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by GPL
 * as set out in the file called "GPLHEADER" included in this distribution. If
 * you do not delete the provisions above, a recipient may use your version of
 * this file under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT_COPYING".
 *
 *****************************************************************************/


#ifndef VHA_COMMON_H
#define VHA_COMMON_H

#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/dcache.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
/* struct timespec64 */
#include <linux/time64.h>

#include <uapi/vha.h>
#include <img_mem_man.h>
#include <vha_drv_common.h>
#include "vha_plat.h"

#define CMD_IS_CNN(cmd) \
				(((cmd)->user_cmd.cmd_type == VHA_CMD_CNN_SUBMIT) || \
				 ((cmd)->user_cmd.cmd_type == VHA_CMD_CNN_SUBMIT_MULTI))

#ifdef CONFIG_VHA_DUMMY
#  ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
#    define CMD_EXEC_ON_HW(cmd) CMD_IS_CNN(cmd)
#  else
#    define CMD_EXEC_ON_HW(cmd) 0
#  endif
#  define VHA_IS_DUMMY(vha) 1
#else
#  define CMD_EXEC_ON_HW(cmd) CMD_IS_CNN(cmd)
#  define VHA_IS_DUMMY(vha) 0
#endif

/* Macro for checking BRN bit map */
#define VHA_IS_BRN(map, brn) \
	(((struct vha_hw_brns *)(&(map)))->bit.bRN##brn)

#ifdef CONFIG_HW_MULTICORE
#define VHA_NUM_CORES CONFIG_VHA_NCORES

#if (VHA_NUM_CORES == 0) || (VHA_NUM_CORES > VHA_MAX_CORES)
#error "Wrong core number configuration!"
#endif

#define VHA_NUM_WMS          VHA_NUM_CORES
#define VHA_GET_CORE_MASK(n) (0xff >> (VHA_MAX_CORES - (n)))
#define VHA_GET_WM_MASK(n)   (0xff >> (VHA_MAX_CORES - (n)))

#define VHA_MAX_PRIORITIES 3
#else
#define VHA_NUM_CORES 1
#define VHA_NUM_WMS   0

#if defined(HW_AX3) && defined(VHA_USE_LO_PRI_SUB_SEGMENTS)
#define VHA_MAX_PRIORITIES 3
#else
#define VHA_MAX_PRIORITIES 1
#endif
#endif

#define VHA_CNN_CMD 0
#define VHA_CMD_MAX VHA_NUM_CORES

#define VHA_DEFAULT_PRI 0
#define VHA_INVALID_PRI 0xff

#define VHA_INVALID_ID -1

#define VHA_COMBINED_CRC_CORE_OFFSET 0x80

/* Max hw contexts supported by MMU */
#if defined(HW_AX2) || defined(CONFIG_HW_MULTICORE)
#define VHA_MMU_MAX_HW_CTXS 32
#define VHA_MMU_AUX_HW_CTX_SHIFT 1
#elif defined(HW_AX3)
/* 8 per OS, 4 model requestor, 4 Input/Output requestors */
#define VHA_MMU_MAX_HW_CTXS 8
#define VHA_MMU_AUX_HW_CTX_SHIFT 4
#else
#error "Wrong MMU configuration"
#endif

/* Enable MMU multiple hw contexts support */
#define VHA_MMU_MULTI_HW_CTX_SUPPORT

#ifdef VHA_MMU_MULTI_HW_CTX_SUPPORT
#if defined(HW_AX2) || defined(CONFIG_HW_MULTICORE)
#  define VHA_MMU_GET_CTXID(session) \
		(((session->mmu_ctxs[0].id-1) + session->vha->mmu_ctx_default) \
		% VHA_MMU_MAX_HW_CTXS);
#elif defined(HW_AX3)
#  define VHA_MMU_GET_CTXID(session) \
		((_OSID_ * VHA_MMU_MAX_HW_CTXS) + \
		(((session->mmu_ctxs[0].id-1) + session->vha->mmu_ctx_default) \
		% (VHA_MMU_MAX_HW_CTXS / 2)));
#endif
#else
#  define VHA_MMU_GET_CTXID(session) \
	(session->vha->mmu_ctx_default % VHA_MMU_MAX_HW_CTXS)
#endif

/*
	AX2 uses only single MMU sw/hw context,
	AX3 may use dual sw contexts (due to dual hw contexts), first for model,
	second for IO requestors,
	when using single sw context, both hw contexts are programmed
	with the same PC address -> so called "mirrored pages tables" mode
	Note: MMU mirrored context support/mirrored pages tables
	mode is enabled by default
	Multicore AX3 uses only single MMU sw/hw context, but has possibility
	to use dual hw contexts in the future */

/* Define model requestor context index */
#define VHA_MMU_REQ_MODEL_CTXID 0

/* Define number of mmu sw contexts & requestors used within session */
#if !defined(VHA_MMU_MIRRORED_CTX_SUPPORT) && defined(HW_AX3)
#define VHA_MMU_MAX_SW_CTXS 2
#define VHA_MMU_REQ_IO_CTXID 1
#else /* VHA_MMU_MIRRORED_SW_SUPPORT || HW_AX2 || CONFIG_HW_MULTICORE */
#define VHA_MMU_MAX_SW_CTXS 1
#define VHA_MMU_REQ_IO_CTXID 0
#endif

/* Max number of alternative addresses supported */
#if defined(HW_AX2)
#define VHA_MAX_ALT_ADDRS 8
#elif defined(HW_AX3)
#define VHA_MAX_ALT_ADDRS 16
#endif

#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
		/* Dummy hw processing cycles in case none are provided in MBS. */
#   define VHA_DUMMY_HW_PROCESSING_TIME_CYCLES 40000
#endif

/* Debug statistics types */
#define VHA_CNN_DBG_MODE_PERF 0
#define VHA_CNN_DBG_MODE_BAND 1
#define CNN_DBG_MODE_ON(type, session) \
	(session->cnn_dbg.cnn_dbg_modes[VHA_CNN_DBG_MODE_##type] > 0)
#define GET_CNN_DBG_MODE(type, session) \
	(session->cnn_dbg.cnn_dbg_modes[VHA_CNN_DBG_MODE_##type])

extern int debug_log_enable;
#if defined(CONFIG_DYNAMIC_DEBUG) || \
	(defined(CONFIG_DYNAMIC_DEBUG_CORE) && defined(DYNAMIC_DEBUG_MODULE)) || \
     defined(DEBUG)
#else
#define dev_dbg(dev, fmt, ...)						\
({									\
	do {								\
        if(debug_log_enable)                \
		   printk(KERN_DEBUG,dev_fmt(fmt), ##__VA_ARGS__); \
	}while(0);\
})
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
# define TIMESPEC timespec
# define TIMESPEC_VALID(ts) timespec_valid(ts)
# define TIMESPEC_COMPARE(from,to) timespec_compare(from, to)
# define GETNSTIMEOFDAY(ts) getnstimeofday(ts);
#else
# define TIMESPEC timespec64
# define TIMESPEC_VALID(ts) timespec64_valid(ts)
# define TIMESPEC_COMPARE(from,to) timespec64_compare(from, to)
# define GETNSTIMEOFDAY(ts) ktime_get_real_ts64(ts);
# endif

/* hw command */
struct vha_hwcmd {
	struct vha_cmd *cmd;
	struct vha_dev *dev;
};

#ifdef CONFIG_HW_MULTICORE
/* Stats per core. */
struct vha_core_stats {
	/* Total processing time. */
	uint64_t total_proc_us;
	/* Utilization in percent. */
	uint32_t utilization;
	/* Total kicks per core */
	uint32_t kicks;
	/* Total kicks that were queued per core */
	uint32_t kicks_queued;
	/* Total kicks that were completed per core */
	uint32_t kicks_completed;
	/* Total kicks that were cancelled */
	uint32_t kicks_cancelled;
	/* Total kicks that were aborted */
	uint32_t kicks_aborted;
};
/* Stats per WM. */
struct vha_wm_stats {
	/* Hw processing start timestamp */
	struct TIMESPEC hw_proc_start;
	/* Hw processing end timestamp */
	struct TIMESPEC hw_proc_end;
	/* Previous hw processing end timestamp */
	struct TIMESPEC hw_proc_end_prev;
	/* Total processing time. */
	uint64_t total_proc_us;
	/* Utilization in percent. */
	uint32_t utilization;
	/* Total kicks */
	uint32_t kicks;
	/* Total kicks that were queued */
	uint32_t kicks_queued;
	/* Total kicks that were completed */
	uint32_t kicks_completed;
	/* Total kicks that were cancelled */
	uint32_t kicks_cancelled;
	/* Total kicks that were aborted */
	uint32_t kicks_aborted;
};
#define VHA_INC_CORE_GROUP_STAT(_vha_, _stat_, _core_mask_) \
	{ \
		uint8_t mask = _core_mask_; \
		while (mask != 0) { \
			uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(mask); \
			mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id)); \
			_vha_->stats.core_stats[curr_core_id]._stat_++; \
		} \
	}
#define VHA_SET_CORE_GROUP_STAT(_vha_, _stat_, _core_mask_, _val_) \
	{ \
		uint8_t mask = _core_mask_; \
		while (mask != 0) { \
			uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(mask); \
			mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id)); \
			_vha_->stats.core_stats[curr_core_id]._stat_ = _val_; \
		} \
	}
#define VHA_UPDATE_CORE_GROUP_STAT(_vha_, _stat_, _core_mask_, _val_) \
	{ \
		uint8_t mask = _core_mask_; \
		while (mask != 0) { \
			uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(mask); \
			mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id)); \
			_vha_->stats.core_stats[curr_core_id]._stat_ += _val_; \
		} \
	}
#define VHA_INC_WM_STAT(_vha_, _stat_, _wm_id_) \
	_vha_->stats.wm_stats[_wm_id_]._stat_++
#define VHA_SET_WM_STAT(_vha_, _stat_, _wm_id_, _val_) \
	_vha_->stats.wm_stats[_wm_id_]._stat_ = _val_
#define VHA_UPDATE_WM_STAT(_vha_, _stat_, _wm_id_, _val_) \
	_vha_->stats.wm_stats[_wm_id_]._stat_ += _val_
#define VHA_WM_STAT_SHIFT_PROC_END(_vha_, _wm_id_) \
	_vha_->stats.wm_stats[_wm_id_].hw_proc_end_prev = \
		_vha_->stats.wm_stats[_wm_id_].hw_proc_end
#define VHA_INC_WL_STAT(_vha_, _stat_, _cmd_) \
	do { \
		VHA_INC_WM_STAT(_vha_, _stat_, _cmd_->hw_sched_info.wm_id); \
		VHA_INC_CORE_GROUP_STAT(_vha_, _stat_, _cmd_->hw_sched_info.core_mask); \
	} while(0)
#define VHA_SET_WL_STAT(_vha_, _stat_, _cmd_, _val_) \
	do { \
		VHA_SET_WM_STAT(_vha_, _stat_, _cmd_->hw_sched_info.wm_id, _val_); \
		VHA_SET_CORE_GROUP_STAT(_vha_, _stat_, _cmd_->hw_sched_info.core_mask, _val_); \
	} while(0)
#define VHA_UPDATE_WL_STAT(_vha_, _stat_, _cmd_, _val_) \
	do { \
		VHA_UPDATE_WM_STAT(_vha_, _stat_, _cmd_->hw_sched_info.wm_id, _val_); \
		VHA_UPDATE_CORE_GROUP_STAT(_vha_, _stat_, _cmd_->hw_sched_info.core_mask, _val_); \
	} while(0)

/* WL processing related memory stats. */
struct vha_mem_stats {
	/* LOCM read data in number of transactions */
	uint32_t locm_rd_transactions;
	/* LOCM write data in number of transactions */
	uint32_t locm_wr_transactions;
	/* LOCM Mask Write data in number of transactions */
	uint32_t locm_mwr_transactions;
	/* SOCM read data in number of transactions */
	uint32_t socm_rd_transactions;
	/* SOCM write data in number of transactions */
	uint32_t socm_wr_transactions;
	/* SOCM Mask Write data in number of transactions */
	uint32_t socm_mwr_transactions;
	/* DDR read data in number of transactions */
	uint32_t ddr_rd_transactions;
	/* DDR write data in number of transactions */
	uint32_t ddr_wr_transactions;
	/* DDR Mask Write data in number of transactions */
	uint32_t ddr_mwr_transactions;
	/* LOCM read data in memory words */
	uint32_t locm_rd_words;
	/* LOCM write data in memory words */
	uint32_t locm_wr_words;
	/* SOCM read data in memory words */
	uint32_t socm_rd_words;
	/* SOCM write data in memory words */
	uint32_t socm_wr_words;
	/* DDR read data in memory words */
	uint32_t ddr_rd_words;
	/* DDR write data in memory words */
	uint32_t ddr_wr_words;
};

/* Scheduling stats */
struct vha_sched_stats {
	/* Mean time from submit to kick for all priorities */
	uint64_t mt_submit_to_kick_cnt[VHA_MAX_PRIORITIES];
	uint64_t mt_submit_to_kick_ns[VHA_MAX_PRIORITIES];
};
# define VHA_UPDATE_SCHED_STAT_MTSTK(_vha_, _cmd_, _span_) \
	do { \
		(_vha_)->stats.sched_stats.mt_submit_to_kick_ns[(_cmd_)->user_cmd.priority] = \
			(((_vha_)->stats.sched_stats.mt_submit_to_kick_cnt[(_cmd_)->user_cmd.priority] * \
				(_vha_)->stats.sched_stats.mt_submit_to_kick_ns[(_cmd_)->user_cmd.priority]) + \
					(uint64_t)((_span_)->tv_sec) * 1000000000ULL + (uint64_t)((_span_)->tv_nsec)) / \
						((_vha_)->stats.sched_stats.mt_submit_to_kick_cnt[(_cmd_)->user_cmd.priority] + 1); \
		(_vha_)->stats.sched_stats.mt_submit_to_kick_cnt[(_cmd_)->user_cmd.priority]++; \
	} while(0)

#endif

struct vha_stats {
	/* Total time the core has powered on */
	uint64_t uptime_ms;
	/* Latest processing time of the core (CNN) */
	uint64_t last_proc_us;
	/* Total number of hw kicks for which a failure has been detected */
	uint64_t total_failures;
	/* Total cnn add cmds */
	uint64_t cnn_add_cmds;
	/* Total cnn polled cmds */
	uint64_t cnn_polled;
	/* Total cnn responsed cmds */
	uint64_t cnn_responsed;
	/* Total cnn kicks */
	uint64_t cnn_kicks;
	/* Total cnn kicks that were queued */
	uint64_t cnn_kicks_queued;
	/* Total cnn kicks that were completed */
	uint64_t cnn_kicks_completed;
	/* Total cnn kicks that were cancelled */
	uint64_t cnn_kicks_cancelled;
	/* Total cnn kicks that were interrupted during processing */
	uint64_t cnn_kicks_aborted;
	/* CNN total processing time */
	uint64_t cnn_total_proc_us;
	/* CNN last processing time */
	uint64_t cnn_last_proc_us;
	/* CNN average processing time */
	uint64_t cnn_avg_proc_us;
	/* CNN last estimated processing time (based on hw cycles & freq) */
	uint64_t cnn_last_est_proc_us;
	/* CNN average estimated processing time */
	uint64_t cnn_avg_est_proc_us;
	/* CNN total hw cycles */
	uint64_t cnn_total_cycles;
	/* CNN last processing time in hw cycles */
	uint64_t cnn_last_cycles;
	/* CNN/Cluster utilization */
	uint32_t cnn_utilization;
	/* Total memory used by the last session */
	uint32_t mem_usage_last;
	/* Total memory used for MMU pages tables by the last session */
	uint32_t mmu_usage_last;
	/* Hw power on timestamp (temporary var) */
	struct TIMESPEC hw_start;
#ifdef CONFIG_HW_MULTICORE
	/* Stats per core */
	struct vha_core_stats core_stats[VHA_NUM_CORES];
	/* Stats per WM */
	struct vha_wm_stats wm_stats[VHA_NUM_CORES];
	/* Memory stats for last WL */
	struct vha_mem_stats last_mem_stats;
	/* Scheduling stats */
	struct vha_sched_stats sched_stats;
#else
	/* Temporary vars */
	/* Hw processing start timestamp  */
	struct TIMESPEC hw_proc_start;
	/* Hw processing end timestamp */
	struct TIMESPEC hw_proc_end;
	/* Previous Hw processing end timestamp */
	struct TIMESPEC hw_proc_end_prev;
#endif
};

/* state of the VHA device: power up or down */
enum vha_state {
	VHA_STATE_OFF,
	VHA_STATE_ON
};

enum vha_mmu_mode {
	VHA_MMU_DISABLED = 0,
	VHA_MMU_DIRECT   = 1, /* 1:1 mapping */
	VHA_MMU_40BIT    = 40
};

enum vha_ll_mode {
	VHA_LL_DISABLED = 0,
	VHA_LL_SW_KICK,
	VHA_LL_SELF_KICK
};

/* Fault injection selection bits */
enum vha_fault_inject {
	VHA_FI_OPEN       = 1,
	VHA_FI_READ       = 2,
	VHA_FI_WRITE      = 4,
	VHA_FI_IOCTL      = 8,
	VHA_FI_MMAP       = 16,
	VHA_FI_CMD_WORKER = 32,
	VHA_FI_IRQ_WORKER = 64,
	VHA_FI_UM         = 128,
};

#ifdef CONFIG_HW_MULTICORE
/* Maximum scheduling sequence entries */
#define VHA_MC_SCHED_SEQ_LEN_MAX 32

struct vha_mc_irq_status {
	uint64_t event_source;
	uint64_t sys_events;
	uint64_t wm_events[VHA_NUM_WMS];
	uint64_t core_events[VHA_NUM_CORES];
	uint64_t ic_events[VHA_NUM_CORES];
};
struct vha_hw_sched_info {
	uint8_t assignment_id;
	uint8_t wm_id;
	uint8_t core_mask;
	bool queued;
	bool freed;
};
typedef struct vha_sched_local_data *vha_sched_data;
struct vha_hw_sched_status {
	uint8_t num_cores_free;
	uint8_t free_core_mask;
	uint8_t num_wms_free;
	uint8_t free_wm_mask;
	struct vha_hw_sched_info assignments[VHA_NUM_CORES];
	vha_sched_data sched_data;
};
#endif

#if defined(CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME) && \
	defined(CONFIG_HW_MULTICORE)
struct vha_dummy_work {
	struct delayed_work dummy_dwork;
	uint8_t  wm_id;
	struct vha_dev *vha;
};
#endif

/* node for heaps list */
struct vha_heap {
	int id;
	bool global;            /* if true then the heap is owned by driver, not vha_dev instance */
	struct list_head list;  /* Entry in <struct vha_drv:heaps> */
};

/* structure represents apm request */
struct vha_apm_work {
	struct delayed_work dwork;
	uint8_t core_mask;
	uint32_t delay_ms;
	struct vha_dev *vha;
};

/* printk helper for converting core_id to BVNC */
#define core_id_quad(core_id) \
    (core_id >> 48), (core_id >> 32) & 0xffff, (core_id >> 16) & 0xffff, (core_id & 0xffff)

/* represents a single VHA core, containing a number of CNN devices */
struct vha_dev {
	unsigned int               id;
	struct mutex               lock;
	struct device             *dev;
	struct list_head           list;            /* entry in <struct vha_drv:devices> */
	struct list_head           sessions;        /* list of um sessions: vha_session */
	struct list_head           sched_sessions[VHA_MAX_PRIORITIES]; /* scheduling list of um sessions*/
	uint32_t                   pri_q_counters[VHA_MAX_PRIORITIES]; /* priority queue WL counters */
	/* Available device memory heaps. List of <struct vha_heap>, stores OCM heaps */
	struct list_head           heaps;
	struct vha_hw_props        hw_props;  /* HW properties */
#ifdef CONFIG_HW_MULTICORE
	uint8_t                    full_core_mask;     /* Mask representing all available cores */
	uint8_t                    active_core_mask;   /* Mask representing all cores powered up */
	uint8_t                    apm_core_mask;      /* Mask representing all cores under APM */
	struct vha_hw_sched_status hw_sched_status;    /* cluster scheduling data */
	uint16_t                   wm_cmd_id_count;    /* WL id counter */
	uint64_t                   wm_core_assignment; /* CORE_ASSIGNMENT reg replacement */
#endif
	bool                       no_clock_disable;/* true means, clocks are always ON */
	int                        pm_delay;        /* delay in ms, before powering off the core that's idle */
	uint8_t                    mmu_mode;        /* 0:no-mmu 1:direct mapping else 40bit */
	uint8_t                    mmu_ctx_default; /* 0:VHA_MMU_MAX_HW_CTXS */
	uint32_t                   mmu_page_size;
	bool                       mmu_base_pf_test;/* MMU base address fault test */
	int                        mmu_no_map_count;/* do not map into the device after the Nth buffer */
#ifdef CONFIG_HW_MULTICORE
	uint32_t                   scheduling_sequence[VHA_MC_SCHED_SEQ_LEN_MAX];
	int32_t                    scheduling_sequence_len;
	uint32_t                   scheduling_counter;
	uint32_t                   stalling_sysbus_host_stall_ratio;
	uint32_t                   stalling_membus_sys_stall_ratio;
#endif
	unsigned long              ocm_paddr;       /* onchip memory start address */
	uint8_t                    cache_sync;
	enum   vha_state           state;           /* the device is up or down */
	struct vha_apm_work        apm_dworks[VHA_NUM_CORES]; /* APM delayed work */
	spinlock_t                 irq_lock;
	pid_t                      irq_bh_pid;
	unsigned long              irq_flags;
#ifdef CONFIG_HW_MULTICORE
	struct vha_mc_irq_status   irq_status;
#else
	uint64_t                   irq_status;
	uint8_t                    irq_count;
	uint8_t                    stream_count;
#endif
	int                        int_heap_id;     /* heap id for internal
			allocations such as crc/debug data, MMU page tables */

	struct vha_hwcmd           pendcmd[VHA_NUM_CORES];   /* pending command */
	struct vha_hwcmd           queuedcmd[VHA_NUM_CORES]; /* queued command */

	int                        fp_bufid;	/* fault page buffer id */
	/* active mmu context (id of ctx) */
	int                        active_mmu_ctx;
	/* ref counters for mmu hw contexts */
	int                        mmu_ctxs[VHA_MMU_MAX_HW_CTXS*VHA_MMU_AUX_HW_CTX_SHIFT];
	/* Indicates if hw is ready to process commands */
	bool                       is_ready;

	void __iomem              *reg_base;
	uint64_t                   reg_size;
	void                      *plat_data;
	void 					  *devfreq_data;

	struct miscdevice          miscdev;	/* UM interface */
	void                      *dbgfs_ctx;
#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
	void                      *sc_dbgfs_ctx;
	struct delayed_work        swd_dwork;
	uint32_t                   swd_period;
	uint64_t                   swd_timeout_default;
	uint32_t                   swd_timeout_m0;
	uint32_t                   swd_timeout_m1;
#endif
	struct work_struct         worker;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
	struct workqueue_struct    *vha_workqueue;
#endif
	/* Indicates if driver should perform calibration during load phase */
	bool                       do_calibration;
	uint32_t                   calibration_cycles;
	/* Core clock frequency measured during driver load phase
	 * or declared in the platform file */
	int                        freq_khz;
	struct vha_stats           stats;
#if defined(HW_AX2)
	uint64_t                   wdt_mode;
#endif

	/* Indicates fault injection mode, bitwise value
	 * 1-open,2-read(poll),4-write,8-ioctl,16-mmap,32-cmd worker,64-irq worker
	 * 128-user space - propagates errors for syscalls only
	 * 0 means all disabled
	 */
	uint8_t                    fault_inject;
	/* Used for simulating system level suspend/resume functionality */
	uint32_t                   suspend_interval_msec;
	struct delayed_work        suspend_dwork;
#ifdef VHA_EVENT_INJECT
	/* requested values as written to debugfs nodes */
	struct {
#ifdef CONFIG_HW_MULTICORE
		uint64_t               vha_cr_core_event;
		uint64_t               vha_cr_sys_event;
		uint64_t               vha_cr_interconnect_event;
		uint64_t               vha_cr_wm_event;
		uint64_t               conf_err;
		uint64_t               parity_poll_err_reg;
#else
		uint64_t               vha_cr_event;
#endif
	} injection;
#endif

	uint8_t                    low_latency; /* Low latency mode */
	int                        hw_bypass;   /* Hardware bypass counter */
	bool                       cnn_combined_crc_enable;
	bool                       dump_buff_digest;
#ifdef VHA_SCF
	/* Flag to forcefully disable parity checking */
	bool                       parity_disable;
	bool                       confirm_config_reg;
#endif
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
	/* Hw processing time simulation delayed work */
#ifdef CONFIG_HW_MULTICORE
	struct vha_dummy_work      dummy_dworks[VHA_NUM_CORES];
#else
	struct delayed_work        dummy_dwork;
#endif
#endif /* CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME */
};


#ifdef CONFIG_HW_MULTICORE
/* WL kick id count field. */
#define VHA_WL_KICK_ID_COUNT_MASK  0x0fff
#define VHA_WL_KICK_ID_COUNT_SHIFT 0
/* WL kick id WM id field. */
#define VHA_WL_KICK_ID_WM_ID_MASK  0xf000
#define VHA_WL_KICK_ID_WM_ID_SHIFT 12
#endif
/* contains a user command message */
struct vha_cmd {
	struct list_head         list[VHA_MAX_PRIORITIES]; /* entry into list vha_session.cmds[*] */
	struct vha_session      *session;           /* session this command belongs to          */
	struct vha_rsp          *rsp;               /* command response                         */
	bool                     in_hw;             /* currently being processed by hw          */
	bool                     queued;            /* currently waiting to processed by hw     */
	bool                     inbufs_ready;      /* all input buffers are ready              */
	bool                     rolled_back;       /* WL was rolled back at least once         */
#ifndef CONFIG_HW_MULTICORE
	uint32_t                 subsegs_completed; /* subsegment processing completed          */
	uint32_t                 subseg_current;    /* index of current subsegment              */
	uint64_t                 proc_us;           /* WL processing time (all subsegs incl.)   */
	uint32_t                 hw_cycles;         /* WL processing cycles (all subsegs incl.) */
	uint32_t                 stream_size;       /* actual size of command stream            */
#endif
	size_t                   size;              /* actual size of user_cmd, in bytes        */
	struct TIMESPEC          hw_proc_start;     /* Hw processing start timestamp            */
	struct TIMESPEC          submit_ts;         /* WL submit timestamp                      */
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
	uint32_t                 dummy_exec_time;   /* Estimated exec time for dummy driver     */
	bool                     dummy_kicked;      /* Dummy execution kicked                   */
#endif
#ifdef CONFIG_HW_MULTICORE
	struct vha_hw_sched_info hw_sched_info;
	uint16_t                 wm_cmd_id;
#endif
#ifdef VHA_SCF
	struct                   completion conf_done;
	bool                     conf_top_error;
	uint32_t                 conf_core_error;
	uint8_t                  layer_count[VHA_NUM_CORES];
	uint8_t                  pass_count[VHA_NUM_CORES];
#endif
	struct vha_user_cmd      user_cmd;		 /* must be last!!! 					 */
};
#ifndef CONFIG_HW_MULTICORE
#define VHA_CMD_SUBSEG_NUM(cmd) \
	(((struct vha_user_cnn_submit_cmd*)&cmd->user_cmd)->subseg_num)
#endif

/* contains a user response message */
struct vha_rsp {
	struct list_head    list;    /* entry into list vha_session.rsps */
	struct vha_session *session;
	size_t              size;
	struct vha_user_rsp user_rsp;
};

/* Status of command execution attempt. */
enum do_cmd_status {
	CMD_OK = 0,      /* command scheduled */
	CMD_DONE,        /* command processed immediately */
	CMD_IN_HW,       /* command already in hardware */
	CMD_WAIT_INBUFS, /* command waiting for input buffers */
	CMD_HW_BUSY      /* hardware is busy with other command */
};

struct cnn_dbg {
	struct vha_buffer *cnn_crc_buf[VHA_NUM_CORES];
	uint32_t           cnn_crc_mode;
	uint32_t           cnn_crc_size_kB;
	struct vha_buffer *cnn_combined_crc;
#ifdef HW_AX3
	uint32_t           cnn_crc_mask;
#endif
	struct vha_buffer *cnn_dbg_buf[VHA_NUM_CORES];
	uint32_t           cnn_dbg_modes[2];
	uint32_t           cnn_dbg_size_kB;
	int                cnn_dbg_flush; /* mode for flushing debug bufs */
	bool               cnn_dbg_pdump_enable;
};

struct vha_mmu_context {
	struct mmu_ctx    *ctx;
	/* mmu context id (associated with session number) */
	int                id;
	/* mmu hardware context number for this session */
	uint8_t            hw_id;    /* 0 - VHA_MMU_MAX_HW_CTXS-1 */
	uint32_t           pc_baddr; /* page catalogue base address */
	int                pc_bufid; /* page catalogue buffer id */
};

/* connection between a user space file handle and a particular device */
struct vha_session {
	struct vha_dev        *vha;                            /* associated device pointer */
	struct list_head       list;                           /* entry in <struct vha_dev:sessions> */
	struct list_head       sched_list[VHA_MAX_PRIORITIES]; /* entry in <struct vha_dev:sched_sessions> */
	struct list_head       bufs;                           /* list of the session's buffers */
	struct list_head       cmds[VHA_MAX_PRIORITIES];       /* list of commands to be sent to hw
																type: struct vha_cmd */
	struct list_head       rsps;                           /* list of responses to be sent to user process
																type: struct vha_rsp */
	struct mem_ctx         *mem_ctx;
	uint32_t               id;

	bool                   oom;        /* out of memory */
	bool                   freeing;

	/* Mmu hw contexts */
	struct vha_mmu_context mmu_ctxs[VHA_MMU_MAX_SW_CTXS];

	struct idr             onchip_maps; /* id alloc for on-chip ram mappings */
	wait_queue_head_t      wq;          /* for waking up on completion of job */
	struct mmu_vaa        *vaa_ctx;     /* virtual address allocator used
											for device buffers allocated in the kernel */
	struct dentry         *dbgfs;       /* file in debugfs */
	struct cnn_dbg         cnn_dbg;
	uint64_t         		kicks;
	uint64_t         		polled;
	uint64_t         		responsed;
	uint64_t         		avg_proc_us;
	uint64_t         		total_proc_us;
	uint64_t         		last_proc_us;
};

/* pdump cache info structure used for LDB commands */
struct vha_pcache {
	bool     valid;
	uint32_t offset;
	uint32_t size;
};

/* Type of the buffer saying what MMU requestors group it belongs to */
enum vha_req_type {
	VHA_REQ_MODEL = 0,
	VHA_REQ_IO
};

#ifdef KERNEL_DMA_FENCE_SUPPORT
/* Buffer synchronisation data. */
struct vha_buf_sync_info {
	int                 in_sync_fd;   /* input sync file descriptor */
	struct file        *in_sync_file; /* input sync file */
	struct dma_fence   *in_fence;     /* input fence */
	struct dma_fence_cb in_sync_cb;   /* input fence callback */
};
#endif

/*
 * A single buffer, which may be used by a command as either an input or
 * an output buffer.
 * Buffers have state: they are either
 *    filled (ready to be read)
 *    or empty (waiting to be written to)
 */
struct vha_buffer {
	struct list_head    list;        /* entry in vha_session.bufs */
	struct list_head    onchip_maps; /* list of vha_onchip_map's*/
	size_t              size;        /* size of buffer */
	void               *dbgfs_priv;  /* debugfs private data */
	uint32_t            id;          /* unique id for the buffer */
	enum vha_buf_status status;      /* is buffer ready to be read */
	enum img_mem_attr   attr;        /* memory attributes */
	void               *kptr;        /* kernel mode pointer */
	uint64_t            devvirt;     /* device virtual address */
	char                name[9];     /* short name for buffer */
	struct vha_pcache   pcache;      /* pdump LDB cache */
	struct vha_session *session;     /* session this buffer belongs to */

	enum vha_req_type   req_type;    /* requestor type */

	bool                inval;       /* needs invalidation? */
	bool                flush;       /* needs flushing? */

#ifdef KERNEL_DMA_FENCE_SUPPORT
	struct vha_buf_sync_info sync_info;
#endif
};

struct vha_onchip_map {
	struct list_head    list;    /* entry in vha_buffer.onchip_maps */
	uint32_t            mapid;   /* unique id for the mapping */
	uint32_t            bufid;   /* bufid of the releated buffer */
	uint64_t            devvirt; /* device virtual address */
};

/* Helper structure for error definition */
#ifdef CONFIG_HW_MULTICORE
enum vha_reset_type {
	VHA_RESET_TYPE_NONE = 0,
	VHA_RESET_TYPE_WM,
	VHA_RESET_TYPE_MMU,
	VHA_RESET_TYPE_FULL
};
#endif
struct vha_biterr {
	int e;
	uint64_t b;
	const char* s;
#ifdef CONFIG_HW_MULTICORE
	enum vha_reset_type reset_type;
#endif
	uint64_t rsp_err;
};

/* Helper structure for debugfs */
struct vha_reg {
	char *name;
	unsigned offset;
	uint64_t mask;
};

struct vha_regset {
	const struct vha_reg *regs;
	int nregs;
	struct vha_dev *vha;
};

struct vha_mh_config_regs {
	uint64_t cnn_preload_control;
	uint64_t req_ctxt_override;
	uint64_t slc_control;
};

struct vha_crc_config_regs {
	uint64_t crc_control;
	uint64_t crc_mask_ctrl;
	uint64_t crc_address[VHA_MAX_CORES];
	uint64_t crc_combined_address[VHA_MAX_CORES];
};

#ifdef VHA_EVENT_INJECT
#define CONF_ERR_TOP 0x1
#define CONF_ERR_BOTTOM 0x2

int __EVENT_INJECT(void);
#endif

bool get_timespan_us(struct TIMESPEC *from, struct TIMESPEC *to, uint64_t *result);

int vha_deinit(void);
uint64_t vha_buf_addr(struct vha_session *session, struct vha_buffer *buf);
int vha_map_buffer(struct vha_session *session,
		uint32_t buf_id, uint64_t virt_addr, uint32_t map_flags);
int vha_unmap_buffer(struct vha_session *session, uint32_t buf_id);
int vha_map_to_onchip(struct vha_session *session,
		uint32_t buf_id, uint64_t virt_addr, uint32_t page_size,
		unsigned int num_pages, uint32_t page_idxs[], uint32_t *mapid);

irqreturn_t vha_handle_irq(struct device *dev);
irqreturn_t vha_handle_thread_irq(struct device *dev);

#ifdef VHA_SCF
void vha_start_swd(struct vha_dev *vha, int cmd_idx);
#endif
void vha_sched_apm(struct vha_dev *vha, struct vha_apm_work *apm_work);
enum do_cmd_status vha_do_cmd(struct vha_cmd *cmd);
void vha_scheduler_loop(struct vha_dev *vha);
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
void vha_dummy_worker(struct work_struct *work);
#endif

int vha_suspend_dev(struct device *dev);
int vha_resume_dev(struct device *dev);

void vha_rm_dev(struct device *dev);
int vha_add_dev(struct device *dev,
		const struct heap_config heap_configs[], const int heaps,
		void *plat_data, void __iomem *reg_base, uint32_t reg_size);
int vha_dev_calibrate(struct device *dev, uint32_t cycles);

int vha_add_cmd(struct vha_session *session, struct vha_cmd *cmd);
int vha_rm_cmds(struct vha_session *session, uint32_t cmd_id,
		uint32_t cmd_id_mask, bool respond);
int vha_add_buf(struct vha_session *session,
		uint32_t buf_id, size_t size, const char *name, enum img_mem_attr attr);
int vha_rm_buf(struct vha_session *session, uint32_t buf_id);
int vha_set_buf_status(struct vha_session *session, uint32_t buf_id,
		enum vha_buf_status stat, int in_sync_fd, bool out_sync_sig);
#ifdef KERNEL_DMA_FENCE_SUPPORT
int vha_create_output_sync(struct vha_session *session, uint32_t buf_id_count,
		uint32_t *buf_ids);
int vha_merge_input_syncs(struct vha_session *session, uint32_t in_sync_fd_count,
		int *in_sync_fds);
int vha_release_syncs(struct vha_session *session, uint32_t buf_id_count,
		uint32_t *buf_ids);
void vha_rm_buf_fence(struct vha_session *session, struct vha_buffer *buf);
#endif
bool vha_buf_needs_inval(struct vha_session *session, uint32_t buf_id);
bool vha_buf_needs_flush(struct vha_session *session, uint32_t buf_id);
struct vha_buffer *vha_find_bufid(const struct vha_session *session,
		uint32_t buf_id);
struct vha_buffer *vha_find_bufvaddr(const struct vha_session *session,
		uint64_t virt_addr);
void vha_chk_cmd_queues(struct vha_dev *vha, bool threaded);
int vha_add_session(struct vha_session *session);
void vha_rm_session(struct vha_session *session);
bool vha_rm_session_cmds(struct vha_session *session);
bool vha_rm_session_cmds_masked(struct vha_session *session, uint32_t cmd_id,
		uint32_t cmd_id_mask);
bool vha_is_busy(struct vha_dev *vha);
bool vha_is_queue_full(struct vha_dev *vha, struct vha_cmd *cmd);
bool vha_is_waiting_for_inputs(struct vha_session *session,
	struct vha_cmd *cmd);
int vha_init_plat_heaps(const struct heap_config heap_configs[], int heaps);
int vha_early_init(void);
bool vha_ext_cache_sync(struct vha_dev *vha);

void vha_dump_digest(struct vha_session *session, struct vha_buffer *buf,
		struct vha_cmd *cmd);

static inline struct vha_dev* vha_dev_get_drvdata(struct device* dev) {
	struct vha_dev_common* vdc = dev_get_drvdata(dev);
	if(!vdc)
		return NULL;
	return vdc->vha_dev;
}

static inline void *vha_get_plat_data(struct device *dev)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);

	if (vha)
		return vha->plat_data;
	return NULL;
}

static inline void *vha_devfreq_get_drvdata(struct device* dev) 
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	if (vha)
		return vha->devfreq_data;
	return NULL;
}

static inline void vha_dev_add_devfreq(struct device *dev, void *vha_devfreq)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	vha->devfreq_data = vha_devfreq;
}

int vha_api_add_dev(struct device *dev, struct vha_dev *vha, unsigned int id);
int vha_api_rm_dev(struct device *dev, struct vha_dev *vha);

int vha_mmu_setup(struct vha_session *session);
#ifdef CONFIG_HW_MULTICORE
void vha_mmu_status(struct vha_dev *vha, uint8_t core_mask);
#else
void vha_mmu_status(struct vha_dev *vha);
#endif
int vha_mmu_flush_ctx(struct vha_dev *vha, int ctx_id);
int vha_mmu_callback(enum img_mmu_callback_type callback_type,
			int buf_id, void *data);

int vha_do_cnn_cmd(struct vha_cmd *cmd);
void vha_cmd_notify(struct vha_cmd *cmd);

bool vha_check_calibration(struct vha_dev *vha);

void vha_dev_hwwdt_setup(struct vha_dev *vha, uint64_t cycles, uint64_t mode);
int vha_dev_hwwdt_calculate(struct vha_dev *vha, struct vha_cmd *cmd,
		uint64_t *cycles, uint64_t *mode);
int vha_dev_prepare(struct vha_dev *dev);
void vha_dev_setup(struct vha_dev *dev);
void vha_dev_wait(struct vha_dev *dev);
#ifndef CONFIG_HW_MULTICORE
uint32_t vha_dev_kick_prepare(struct vha_dev *vha,
		struct vha_cmd *cmd, int ctx_id);
#else
int vha_dev_scheduler_init(struct vha_dev *vha);
int vha_dev_scheduler_deinit(struct vha_dev *vha);
bool vha_dev_dbg_params_init(struct vha_dev *vha);
bool vha_dev_dbg_params_check(struct vha_dev *vha);
#endif
void vha_dev_mh_setup(struct vha_dev *vha, int ctx_id, struct vha_mh_config_regs *regs);
int vha_dev_get_props(struct vha_dev *vha, uint32_t ocm_size);
void vha_dev_ocm_configure(struct vha_dev *vha);

#ifdef CONFIG_HW_MULTICORE
int vha_dev_schedule_cmd(struct vha_dev *vha, struct vha_cmd *cmd);
void vha_dev_free_cmd_res(struct vha_dev *vha, struct vha_cmd *cmd, bool update_stats);
void vha_dev_update_per_core_kicks(uint8_t core_mask, uint32_t *kicks_array);
#endif

bool vha_dev_check_hw_capab(struct vha_dev* vha,
		uint64_t expected_hw_capab);

int vha_dev_start(struct vha_dev *vha);
int vha_dev_stop(struct vha_dev *vha, bool reset);
int vha_dev_suspend_work(struct vha_dev *vha);
void vha_update_utilization(struct vha_dev *vha);

bool vha_rollback_cmds(struct vha_dev *vha);
void vha_dev_apm_stop(struct vha_dev *vha, struct vha_apm_work *apm_work);

void vha_cnn_start_calib(struct vha_dev *vha);
void vha_cnn_update_stats(struct vha_dev *vha);
void vha_cnn_dump_status(struct vha_dev *vha);
#ifdef CONFIG_HW_MULTICORE
void vha_cnn_cmd_completed(struct vha_cmd *cmd, uint64_t status, int err,  uint64_t rsp_err_flags);
#else
void vha_cnn_cmd_completed(struct vha_cmd *cmd, int status);
#endif

#ifdef CONFIG_HW_MULTICORE
uint8_t vha_wm_get_cores(struct vha_dev *vha, uint8_t wm_id);
void vha_wm_assign_cores(struct vha_dev *vha, uint8_t wm_id, uint8_t core_mask, uint64_t *core_assignment);
void vha_wm_release_cores(struct vha_dev *vha, uint8_t core_mask, bool to_pdump);
int vha_wm_reset(struct vha_dev *vha, struct vha_hw_sched_info *sched_info);
void vha_wm_status(struct vha_dev *vha, uint8_t wm_id, uint8_t core_mask);
void vha_wm_hwwdt_calculate(struct vha_dev *vha, struct vha_cmd *cmd,
		uint64_t *wl_cycles, uint64_t *core_cycles);
void vha_wm_hwwdt_setup(struct vha_dev *vha, struct vha_cmd *cmd,
		uint64_t wl_cycles, uint64_t core_cycles);
void vha_wm_intmem_setup(struct vha_dev *vha, uint8_t wm_id);
#endif

#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
void wd_timer_callback(struct work_struct *work);
#endif

void vha_dbg_init(struct vha_dev *vha);
void vha_dbg_deinit(struct vha_dev *vha);
struct dentry* vha_dbg_get_sysfs(struct vha_dev *vha);
int vha_dbg_create_hwbufs(struct vha_session *session);
void vha_dbg_prepare_hwbufs(struct vha_session *session, struct vha_cmd *cmd,
		struct vha_crc_config_regs *regs);
void vha_dbg_flush_hwbufs(struct vha_session *session, char checkpoint, uint8_t mask);
void vha_dbg_stop_hwbufs(struct vha_session *session, uint8_t mask);
void vha_dbg_destroy_hwbufs(struct vha_session *session);
int vha_dbg_alloc_hwbuf(struct vha_session *session, size_t size,
		struct vha_buffer **buffer, const char *name, bool map);
void vha_dbg_hwbuf_cleanup(struct vha_session *session,
		struct vha_buffer *buf);

uint64_t vha_dbg_rtm_read(struct vha_dev *vha, uint64_t addr);
#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
void vha_sc_dbg_init(struct vha_dev *vha, struct dentry *debugfs_dir);
void vha_sc_dbg_deinit(struct vha_dev *vha);
void vha_update_crcs(struct vha_dev *vha, uint32_t crcs[VHA_NUM_CORES], int n);
#endif /* VHA_SCF && CONFIG_HW_MULTICORE */
int vha_pdump_init(struct vha_dev *vha, struct pdump_descr* pdump);
void vha_pdump_deinit(struct pdump_descr* pdump);
void vha_pdump_ldb_buf(struct vha_session *session, uint32_t pdump_num,
		struct vha_buffer *buffer, uint32_t offset, uint32_t len, bool cache);
void vha_pdump_sab_buf(struct vha_session *session, uint32_t pdump_num,
		struct vha_buffer *buffer, uint32_t offset, uint32_t len);

/* devfreq support */
int vha_devfreq_init(struct device *dev);
void vha_devfreq_term(struct device *dev);
int vha_devfreq_suspend(struct device *dev);
int vha_devfreq_resume(struct device *dev);
int vha_get_cnntotal_proc_us(struct device *dev, uint64_t *proc_us, uint64_t *cur_proc_us);
int vha_currcmd_exetime_req(struct vha_dev *vha, uint64_t *proc_us);

/*
 * register event observers, notified when significant events occur
 * Only a single observer per event!
 */
struct vha_observers {
	void (*enqueued)(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint32_t priority);
	void (*submitted)(uint32_t devid, uint32_t sessionid, uint32_t cmdid, bool last_subsegment, uint32_t priority);
	void (*completed)(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint64_t status,
			uint64_t cycles, uint64_t mem_usage, uint32_t priority);
	void (*canceled)(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint32_t priority);
	void (*error)(uint32_t devid, uint32_t sessionid, uint32_t cmdid, uint64_t status);
};
extern struct vha_observers vha_observers;
/*
 * register a listener for ENQUEUE events,
 * when requests have been enqueued for submission
*/
extern void vha_observe_event_enqueue(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint32_t priority));
/*
 * register a listener for SUBMIT events,
 * when requests have been submitted to the HW
 */
extern void vha_observe_event_submit(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							bool last_subsegment,
							uint32_t priority));
/*
 * register a listener for HW COMPLETE events,
 * when hardware has completed a submission
 */
extern void vha_observe_event_complete(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint64_t status,
							uint64_t cycles,
							uint64_t mem_usage,
							uint32_t priority));
/*
 * register a listener for HW COMPLETE events,
 * when hardware has completed a submission
 */
extern void vha_observe_event_cancel(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint32_t priority));
/*
 * register a listener for ERROR events,
 * when the HW error occurs
 */
extern void vha_observe_event_error(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint64_t status));

#endif /* VHA_COMMON_H */

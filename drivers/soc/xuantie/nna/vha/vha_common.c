/*
 *****************************************************************************
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

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/moduleparam.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/delay.h>

#include <linux/pm_runtime.h>
#include <linux/debugfs.h>

#include <linux/crc32.h>

#include <uapi/vha.h>
#include <uapi/version.h>
#include "vha_common.h"
#include "vha_plat.h"
#include <vha_regs.h>

#ifdef KERNEL_DMA_FENCE_SUPPORT
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/sync_file.h>
#include <linux/file.h>
#include <linux/kernel.h>
#endif

#if !defined(HW_AX2) && !defined(HW_AX3)
#error No HW architecture series defined. Either HW_AX2 or HW_AX3 must be defined
#elseif defined(HW_AX2) && defined(HW_AX3)
#error Invalid HW architecture series define. Only one of HW_AX2 or HW_AX3 must be defined.
#endif

#undef linux
#define CREATE_TRACE_POINTS
#include <vha_trace_point.h>

#define MIN_ONCHIP_MAP 1
#define MAX_ONCHIP_MAP 128

static uint8_t mmu_mode = VHA_MMU_40BIT;
module_param(mmu_mode, byte, 0444);
MODULE_PARM_DESC(mmu_mode,
	"MMU mode: 0=no-MMU, 1=direct (1:1) mappings or 40=40bit (default)");
static uint32_t mmu_ctx_default;
module_param(mmu_ctx_default, uint, 0444);
MODULE_PARM_DESC(mmu_ctx_default, "MMU default context id(0:31) to be used");
static uint32_t mmu_page_size;  /* 0-4kB */
module_param(mmu_page_size, uint, 0444);
MODULE_PARM_DESC(mmu_page_size,
	"MMU page size: 0-4kB, 1-16kB, 2-64kB, 3-256kB, 4-1MB; 5-2MB");

static bool no_clock_disable = false;
module_param(no_clock_disable, bool, 0444);
MODULE_PARM_DESC(no_clock_disable,
		"if Y, the device is not disabled when inactive, otherwise APM is used");

static int pm_delay = 100;
module_param(pm_delay, int, S_IRUSR | S_IRGRP);
MODULE_PARM_DESC(pm_delay, "Delay, in ms, before powering off the core that's idle");

static int freq_khz = -1;
module_param(freq_khz, int, 0444);
MODULE_PARM_DESC(freq_khz,
		"core frequency in kHz, -1=start self measurement during driver load, 0=use platform defined value, otherwise (>0) declared value is used");
static uint32_t hw_bypass;
module_param(hw_bypass, uint, 0444);
MODULE_PARM_DESC(hw_bypass,
		"Number of cnn kicks(segments) to be bypassed within the session, 0=none");
static uint32_t slc_bypass;
module_param(slc_bypass, uint, 0444);
MODULE_PARM_DESC(slc_bypass, "SLC bypass mode");
#if defined(HW_AX2) || defined(CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME)
static uint32_t low_latency = VHA_LL_SW_KICK;
#elif defined(HW_AX3) && defined(VHA_USE_LO_PRI_SUB_SEGMENTS)
static uint32_t low_latency = VHA_LL_DISABLED;
#else
static uint32_t low_latency = VHA_LL_SELF_KICK;
#endif
module_param(low_latency, uint, 0444);
MODULE_PARM_DESC(low_latency, "Low latency mode: 0-disabled, 1-sw kick, 2-self kick");

static bool zero_buffers;
module_param(zero_buffers, bool, 0444);
MODULE_PARM_DESC(zero_buffers, "fill every allocated buffer with zeros");

static bool dump_buff_digest = 0;
module_param(dump_buff_digest, bool, 0444);
MODULE_PARM_DESC(dump_buff_digest, "Calculate & dump digest for in/out buffers. This is crc32");

static unsigned long onchipmem_phys_start= VHA_OCM_ADDR_START;
module_param(onchipmem_phys_start, ulong, 0444);
MODULE_PARM_DESC(onchipmem_phys_start,
	"Physical address of start of on-chip ram. '0xFs' means that ocm is disabled");
static uint32_t onchipmem_size;
module_param(onchipmem_size, uint, 0444);
MODULE_PARM_DESC(onchipmem_size,
	"Size of on-chip memory in bytes");

/* bringup test: force MMU fault with MMU base register */
static bool test_mmu_base_pf;
module_param(test_mmu_base_pf, bool, 0444);
MODULE_PARM_DESC(test_mmu_base_pf,
	"Bringup test: force MMU page fault on first access");

/* bringup test: do not map into the device after the Nth buffer */
static int32_t test_mmu_no_map_count = -1;
module_param(test_mmu_no_map_count, int, 0444);
MODULE_PARM_DESC(test_mmu_no_map_count,
	"Bringup test: force MMU page faults if count >= 0");

#ifdef VHA_SCF
static bool parity_disable = false;
module_param(parity_disable, bool, 0444);
MODULE_PARM_DESC(parity_disable,
		"if Y, the core parity feature will be disabled, if it is supported");

static bool confirm_config_reg = false;
module_param(confirm_config_reg, bool, 0444);
MODULE_PARM_DESC(confirm_config_reg,
		"Enables confirmation of register writes");
#endif

static bool test_without_bvnc_check;
module_param(test_without_bvnc_check, bool, 0444);
MODULE_PARM_DESC(test_without_bvnc_check,
		"When set BVNC check is ignored, allowing to kick the hw");

/* Fault inject parameter is only applicable when
 * kernel fault injection feature is enabled
 * in the kernel options -> CONFIG_FAULT_INJECTION=y
 * See Documentation/fault-injection/
 */
static uint8_t fault_inject;
module_param(fault_inject, byte, 0444);
MODULE_PARM_DESC(fault_inject,
		"Enable fault injection using bitwise value: 1-open,2-read,4-write,8-ioctl,16-mmap,32-cmd worker,64-irq worker,128-user space");

/* Interval in milliseconds for testing/simulating system suspend/resume functionality */
static uint8_t suspend_interval_msec;
module_param(suspend_interval_msec, byte, 0444);
MODULE_PARM_DESC(suspend_interval_msec,
		"Test suspend/resume interval, 0=disabled, otherwise defines interval in milliseconds");

#ifdef VHA_SCF
static bool cnn_combined_crc_enable = true;
#else
static bool cnn_combined_crc_enable = false;
#endif
module_param(cnn_combined_crc_enable, bool, 0444);
MODULE_PARM_DESC(cnn_combined_crc_enable,
	"Enables the combined CRC feature");
#ifdef VHA_SCF
static u32 swd_period = 10;
module_param(swd_period, uint, 0444);
MODULE_PARM_DESC(swd_period,
		"The timer expiration period in miliseconds, 0=disable");

static unsigned long swd_timeout_default = 0;
module_param(swd_timeout_default, ulong, 0444);
MODULE_PARM_DESC(swd_timeout_default,
		"The default expected execution time in us, 0=use MBS values only");

static u32 swd_timeout_m0 = 100;
module_param(swd_timeout_m0, uint, 0444);
MODULE_PARM_DESC(swd_timeout_m0,
		"The m0 value in the expected execution time equation: T = (T0 * m0)/100 + m1");

static u32 swd_timeout_m1 = 10000;
module_param(swd_timeout_m1, uint, 0444);
MODULE_PARM_DESC(swd_timeout_m1,
		"The m1 value in the expected execution time equation:  T = (T0 * m0)/100 + m1");
#endif

/* Event observers, to be notified when significant events occur */
struct vha_observers vha_observers;

/* Driver context */
static struct {
	/* Available driver memory heaps. List of <struct vha_heap> */
	struct list_head heaps;

	/* Memory Management context for driver */
	struct mem_ctx *mem_ctx;

	/* List of associated <struct vha_dev> */
	struct list_head devices;

	unsigned int num_devs;

	int initialised;
} drv;

/* Session id counter. */
static uint32_t vha_session_id_cnt = 0;
/* Reset counter. */
static uint32_t vha_reset_cnt = 0;

static void cmd_worker(struct work_struct *work);

static const size_t mmu_page_size_kb_lut[] =
		{ 4096, 16384, 65536, 262144, 1048576, 2097152};

#ifdef CONFIG_FUNCTION_ERROR_INJECTION
noinline int __IOPOLL64_RET(int ret) {
	return ret;
}

#include <asm-generic/error-injection.h>
/* this is the placeholder function to support error code injection from
 * all IOPOLL_PDUMP* macros
 */
ALLOW_ERROR_INJECTION(__IOPOLL64_RET, ERRNO);

#ifdef VHA_EVENT_INJECT
/*
 * called in __handle_event_injection()
 * if normal circumstances, return 0 and do not inject EVENT
 * otherwise, return -errno
 */
noinline int __EVENT_INJECT(void) {
	return 0;
}
ALLOW_ERROR_INJECTION(__EVENT_INJECT, ERRNO);
#endif /* VHA_EVENT_INJECT */

#endif

/* Calculate current timespan for the given timestamp */
bool get_timespan_us(struct TIMESPEC *from, struct TIMESPEC *to, uint64_t *result)
{
	long long total = 0;

	if (!TIMESPEC_VALID(from) || !TIMESPEC_VALID(to))
		return false;

	if (TIMESPEC_COMPARE(from, to) >= 0)
		return false;

	total = NSEC_PER_SEC * to->tv_sec +
				to->tv_nsec;
	total -= NSEC_PER_SEC * from->tv_sec +
			from->tv_nsec;
	do_div(total, 1000UL);
	*result = total;

	return true;
}

/* Used for simulating system level suspend/resume functionality */
static void suspend_test_worker(struct work_struct *work)
{
	struct vha_dev *vha = container_of(work, struct vha_dev, suspend_dwork.work);
	int ret;

	/* Make resume/suspend cycle */
	ret = vha_suspend_dev(vha->dev);
	WARN_ON(ret != 0);
	vha_resume_dev(vha->dev);

	mutex_lock(&vha->lock);
	/* Retrigger suspend worker */
	schedule_delayed_work(&vha->suspend_dwork,
			msecs_to_jiffies(vha->suspend_interval_msec));
	mutex_unlock(&vha->lock);
}

/*
 * Initialize common platform (driver) memory heaps.
 * device (cluster) heaps are initialized in vha_init()
 */
int vha_init_plat_heaps(const struct heap_config heap_configs[], int heaps)
{
	int i;
	int ret = 0;
	/* Initialise memory management component */
	for (i = 0; i < heaps; i++) {
		struct vha_heap *heap;

		pr_debug("%s: adding platform heap of type %d\n",
			__func__, heap_configs[i].type);

		heap = kzalloc(sizeof(struct vha_heap), GFP_KERNEL);
		if (!heap) {
			ret = -ENOMEM;
			goto drv_heap_add_failed;
		}
		heap->global = true;
		ret = img_mem_add_heap(&heap_configs[i], &heap->id);
		if (ret < 0) {
			pr_err("%s: failed to init platform heap (type %d)!\n",
				__func__, heap_configs[i].type);
			kfree(heap);
			goto drv_heap_add_failed;
		}
		list_add(&heap->list, &drv.heaps);
	}

	return ret;

drv_heap_add_failed:
	while (!list_empty(&drv.heaps)) {
		struct vha_heap *heap;

		heap = list_first_entry(&drv.heaps, struct vha_heap, list);
		list_del(&heap->list);
		img_mem_del_heap(heap->id);
		kfree(heap);
	}
	return ret;
}

int vha_early_init(void)
{
	int ret;
	INIT_LIST_HEAD(&drv.heaps);
	INIT_LIST_HEAD(&drv.devices);

	/* Create memory management context for HW buffers */
	ret = img_mem_create_proc_ctx(&drv.mem_ctx);
	if (ret) {
		pr_err("%s: failed to create mem context (err:%d)!\n",
			__func__, ret);
		drv.mem_ctx = NULL;
	}
	return ret;
}

/*
 * Lazy intialization of main driver context (when first core is probed)
 */
static int vha_init(struct vha_dev *vha,
			const struct heap_config heap_configs[], int heaps)
{
	struct device *dev = vha->dev;
	int ret, i;

#ifdef CONFIG_HW_MULTICORE
	ret = vha_dev_scheduler_init(vha);
	if (ret != 0) {
		dev_err(dev, "%s: failed initializing scheduler!\n", __func__);
		return ret;
	}
	if (!vha_dev_dbg_params_init(vha)) {
		dev_err(dev, "%s: invalid debug params detected!\n", __func__);
		return -EINVAL;
	}
#endif

	/* Initialise local device (cluster) heaps */
	for (i = 0; i < heaps; i++) {
		struct vha_heap *heap;

		dev_dbg(dev, "%s: adding device heap of type %d\n",
			__func__, heap_configs[i].type);

		heap = kzalloc(sizeof(struct vha_heap), GFP_KERNEL);
		if (!heap) {
			ret = -ENOMEM;
			goto heap_add_failed;
		}

		ret = img_mem_add_heap(&heap_configs[i], &heap->id);
		if (ret < 0) {
			dev_err(dev, "%s: failed to init device heap (type %d)!\n",
				__func__, heap_configs[i].type);
			kfree(heap);
			goto heap_add_failed;
		}
		list_add(&heap->list, &vha->heaps);
	}

	/* now copy platform (global) heap id's to device vha_heap list, the global heap id's are
	 * not owned by vha_dev anyway (heap->global=true)
	 * This is done for vha_ioctl_query_heaps() to be able to report both platform
	 * and device heaps easily. */
	{
		struct list_head* pos;
		list_for_each_prev(pos, &drv.heaps) {
			struct vha_heap* heap = list_entry(pos, struct vha_heap, list);
			struct vha_heap* heap_copy = kmemdup(heap, sizeof(*heap), GFP_KERNEL);
			if(!heap_copy) {
				ret = -ENOMEM;
				goto heap_add_failed;
			}
			INIT_LIST_HEAD(&heap_copy->list);
			list_add(&heap_copy->list, &vha->heaps);
		}
	}

	/* initialize local ocm cluster heaps */
	if (vha->hw_props.locm_size_bytes && onchipmem_phys_start == ~0)
		dev_warn(dev, "%s: Onchip memory physical address not set!\n",
						__func__);
	/* OCM heap type is automatically appended */
	if (vha->hw_props.locm_size_bytes && onchipmem_phys_start != ~0) {
		struct heap_config heap_cfg;
		struct vha_heap *heap;

		memset(&heap_cfg, 0, sizeof(heap_cfg));
		heap_cfg.type = IMG_MEM_HEAP_TYPE_OCM;
		heap_cfg.options.ocm.phys = onchipmem_phys_start;
		heap_cfg.options.ocm.size = vha->hw_props.locm_size_bytes;
		heap_cfg.options.ocm.hattr = IMG_MEM_HEAP_ATTR_LOCAL;

		dev_dbg(dev, "%s: adding heap of type %d\n",
				__func__, heap_cfg.type);

		heap = kzalloc(sizeof(struct vha_heap), GFP_KERNEL);
		if (!heap) {
			ret = -ENOMEM;
			goto heap_add_failed;
		}

		ret = img_mem_add_heap(&heap_cfg, &heap->id);
		if (ret < 0) {
			dev_err(dev, "%s: failed to init heap (type %d)!\n",
				__func__, heap_cfg.type);
			kfree(heap);
			goto heap_add_failed;
		}
		list_add(&heap->list, &vha->heaps);
	}
#ifdef CONFIG_HW_MULTICORE
	if (vha->hw_props.socm_size_bytes && onchipmem_phys_start != ~0) {
		struct heap_config heap_cfg;
		struct vha_heap *heap;

		memset(&heap_cfg, 0, sizeof(heap_cfg));
		heap_cfg.type = IMG_MEM_HEAP_TYPE_OCM;
		heap_cfg.options.ocm.phys = onchipmem_phys_start +
				vha->hw_props.locm_size_bytes + IMG_MEM_VA_GUARD_GAP;
		heap_cfg.options.ocm.size = vha->hw_props.socm_size_bytes;
		heap_cfg.options.ocm.hattr = IMG_MEM_HEAP_ATTR_SHARED;

		dev_dbg(dev, "%s: adding heap of type %d\n",
				__func__, heap_cfg.type);

		heap = kzalloc(sizeof(struct vha_heap), GFP_KERNEL);
		if (!heap) {
			ret = -ENOMEM;
			goto heap_add_failed;
		}

		ret = img_mem_add_heap(&heap_cfg, &heap->id);
		if (ret < 0) {
			dev_err(dev, "%s: failed to init heap (type %d)!\n",
				__func__, heap_cfg.type);
			kfree(heap);
			goto heap_add_failed;
		}
		list_add(&heap->list, &vha->heaps);
	}
#endif

	{
		/* now get the last entry and make it responsible for internal allocations
		 * use last entry because list_add() inserts at the head
		 * When choosing the internal alloc heap, the device local heaps take precedence over
		 * global platform heaps */
		struct vha_heap* heap = list_last_entry(&vha->heaps, struct vha_heap, list);
		if(!heap) {
			dev_err(dev, "%s: failed to locate heap for internal alloc\n",
				__func__);
			ret = -EINVAL;
			/* Loop registered heaps just for sanity */
			goto heap_add_failed;
		}
		vha->int_heap_id = heap->id;
		dev_dbg(dev, "%s: using heap %d for internal alloc\n",
				__func__, vha->int_heap_id);
	}
	/* Do not proceed if internal heap not defined */

	drv.initialised = 1;

	dev_dbg(dev, "%s: vha drv init done\n", __func__);
	return 0;

heap_add_failed:
	while (!list_empty(&vha->heaps)) {
		struct vha_heap *heap;

		heap = list_first_entry(&vha->heaps, struct vha_heap, list);
		list_del(&heap->list);
		if(!heap->global)
			img_mem_del_heap(heap->id);
		kfree(heap);
	}
	return ret;
}

int vha_deinit(void)
{
	/* Destroy memory management context */
	if (drv.mem_ctx) {
		size_t mem_usage;
		uint32_t MB, bytes, kB;

		img_mem_get_usage(drv.mem_ctx, &mem_usage, NULL);
		MB = mem_usage / (1024 * 1024);
		bytes = mem_usage - (MB * (1024 * 1024));
		kB = (bytes * 1000) / (1024 * 1024);

		pr_debug("%s: Total kernel memory used: %u.%u MB\n",
				__func__, MB, kB);

		img_mem_destroy_proc_ctx(drv.mem_ctx);
		drv.mem_ctx = NULL;
	}

	/* Deinitialize memory management component */
	while (!list_empty(&drv.heaps)) {
		struct vha_heap *heap;

		heap = list_first_entry(&drv.heaps, struct vha_heap, list);
		BUG_ON(!heap->global);
		list_del(&heap->list);
		img_mem_del_heap(heap->id);
		kfree(heap);
	}

	drv.initialised = 0;
	return 0;
}

/*
 * Returns: true if hardware has required capabilities, false otherwise.
 * Implementation is a simple check of expected BVNC against hw CORE_ID
 */
bool vha_dev_check_hw_capab(struct vha_dev* vha, uint64_t expected_hw_capab)
{
	uint64_t __maybe_unused hw = vha->hw_props.core_id
		& VHA_CR_CORE_ID_BVNC_CLRMSK;
	uint64_t __maybe_unused mbs = expected_hw_capab
		& VHA_CR_CORE_ID_BVNC_CLRMSK;

	if (!test_without_bvnc_check) {
		img_pdump_printf(
						"IF SKIP_COREID_CHECK\n"
						"COM Skip COREID Check\n"
						"ELSE SKIP_COREID_CHECK\n"
			"COM CHECKING CORE_ID: expecting BVNC:%llu.%llu.%llu.%llu\n",
			core_id_quad(expected_hw_capab));
		IOPOLL64_PDUMP(expected_hw_capab, 1, 1,
					VHA_CR_CORE_ID_BVNC_CLRMSK,
					VHA_CR_CORE_ID);
				img_pdump_printf(
						"FI SKIP_COREID_CHECK\n");
	}

	if ((expected_hw_capab >> 48) != HW_SERIES) {
		dev_err(vha->dev,
			"%s: network was compiled for incorrect hardware series: expected %llu / found %u\n",
			__func__,
			(expected_hw_capab >> 48), HW_SERIES);
		return false;
	}

#ifndef CONFIG_VHA_DUMMY
	if (hw != mbs) {
		dev_warn(vha->dev,
			"%s: network was compiled for an incorrect hardware variant (BVNC): "
			"found %llu.%llu.%llu.%llu, expected %llu.%llu.%llu.%llu\n",
			__func__,
			core_id_quad(vha->hw_props.core_id),
			core_id_quad(expected_hw_capab));
		/* Conditionally allow the hw to be kicked */
		if (test_without_bvnc_check)
			dev_warn(vha->dev, "%s: trying to kick the hw ... ", __func__);
		else {
			dev_err(vha->dev, "%s: can't kick the hardware!", __func__);
			return false;
		}
	}
#endif
	return true;
}

/* notify the user space if a response msg is ready */
void vha_cmd_notify(struct vha_cmd *cmd)
{
	struct vha_session *session = cmd->session;
	struct vha_rsp *rsp = cmd->rsp;
	dev_dbg(session->vha->dev, "%s: 0x%08x/%u\n",
			__func__, cmd->user_cmd.cmd_id, session->id);

	if (rsp) {
		cmd->rsp = NULL;
		list_add_tail(&rsp->list, &session->rsps);
	}
	wake_up(&session->wq);
	/* we are done with this cmd, let's free it */
	list_del(&cmd->list[cmd->user_cmd.priority]);
	kfree(cmd);
}

static void vha_measure_core_freq(struct vha_dev *vha)
{
	if (vha->stats.last_proc_us) {
		uint64_t proc = vha->stats.last_proc_us;
		do_div(proc, 1000UL);
		if (proc) {
			uint64_t cycles = vha->calibration_cycles;
			do_div(cycles, proc);
			vha->freq_khz = cycles;
			dev_info(vha->dev,
			"%s: Measured core clock frequency[kHz]: %u\n",
			__func__, vha->freq_khz);
			return;
		}
	}

	dev_info(vha->dev,
		"%s: Can't measure core clock frequency!\n",
		__func__);
}

bool vha_check_calibration(struct vha_dev *vha)
{
	if (vha->stats.last_proc_us) {
		/* Core may have been kicked to
		 * measure frequency */
		if (vha->do_calibration) {
			vha_dev_stop(vha, true);
			vha_measure_core_freq(vha);
			vha->do_calibration = false;
			/* Something may have been scheduled in
			 * the middle so poke the worker */
			vha_chk_cmd_queues(vha, false);
			return true;
		}
	}

	return false;
}

/*
 * A session represents a single device and a set of buffers
 * to be used for inferences.
 * If required, buffers will be allocated for hardware CRC and DEBUG.
 */
int vha_add_session(struct vha_session *session)
{
	struct vha_dev *vha = session->vha;
	int ret;
	struct mmu_config mmu_config;
	int ctx_id;
	uint8_t pri;

	img_pdump_printf("-- OPEN_BEGIN\n");
	img_pdump_printf("-- VHA driver session started\n");
	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

#ifdef CONFIG_VHA_DUMMY
	if (list_empty(&vha->sessions) && !vha->do_calibration)
		vha_dev_start(vha);
#endif

	idr_init(&session->onchip_maps);

	memset(&mmu_config, 0, sizeof(mmu_config));
	/* Create a memory context for this session */
	if (vha->mmu_mode == VHA_MMU_DISABLED) {
		/* if MMU is disabled,
		 * bypass the mmu hw layer,
		 * but still need do the buffer
		 * allocation through img_mem api
		 */
		mmu_config.bypass_hw = true;
#ifdef CONFIG_HW_MULTICORE
		mmu_config.bypass_offset = IMG_MEM_VA_HEAP1_BASE;
#endif
	}

#ifdef VHA_SCF
	/* Do not calculate parity when core does not support it,
	 * or we forced the core to disable it */
	if (vha->hw_props.supported.parity &&
			!vha->parity_disable) {
		mmu_config.use_pte_parity = true;
		dev_dbg(vha->dev,
					"%s: Enabling MMU parity protection!\n",
					__func__);
	}
#endif

	mmu_config.addr_width = vha->hw_props.mmu_width;
	mmu_config.alloc_attr = IMG_MEM_ATTR_MMU | /* Indicate MMU allocation */
		IMG_MEM_ATTR_WRITECOMBINE;
	mmu_config.page_size = mmu_page_size_kb_lut[vha->mmu_page_size];
	img_pdump_printf("-- MMU context: using %zukB MMU pages, %lukB CPU pages\n",
			mmu_page_size_kb_lut[vha->mmu_page_size]/1024, PAGE_SIZE/1024);

	/* Update current MMU page size, so that the correct
	 * granularity is used when generating virtual addresses */
	vha->hw_props.mmu_pagesize = mmu_config.page_size;

	/* Update clock frequency stored in props */
	vha->hw_props.clock_freq = vha->freq_khz;

	for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++) {
		ret = img_mmu_ctx_create(vha->dev, &mmu_config,
					session->mem_ctx, vha->int_heap_id,
					vha_mmu_callback, session,
					&session->mmu_ctxs[ctx_id].ctx);
		if (ret < 0) {
			dev_err(vha->dev, "%s: failed to create sw mmu context%d!\n",
				__func__, ctx_id);
			goto out_unlock;
		}

		if (vha->mmu_mode != VHA_MMU_DISABLED) {
			/* Store mmu context id */
			session->mmu_ctxs[ctx_id].id = ret;

			ret = img_mmu_get_pc(session->mmu_ctxs[ctx_id].ctx,
					&session->mmu_ctxs[ctx_id].pc_baddr,
					&session->mmu_ctxs[ctx_id].pc_bufid);
			if (ret) {
				dev_err(vha->dev, "%s: failed to get PC for context%d!\n",
						__func__, ctx_id);
				ret = -EFAULT;
				goto out_free_mmu_ctx;
			}
		}
	}

#ifndef CONFIG_HW_MULTICORE
	if (vha->hw_props.locm_size_bytes && onchipmem_phys_start != ~0) {
		/* OCM data is considered as IO (or shared)*/
		ret = img_mmu_init_cache(session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].ctx,
				onchipmem_phys_start, vha->hw_props.locm_size_bytes
#if defined(CFG_SYS_VAGUS)
				+ sizeof(uint32_t)
#endif
				);
		if (ret < 0) {
			dev_err(vha->dev, "%s: failed to create init cache!\n",
					__func__);
			goto out_free_mmu_ctx;
		}
		vha_dev_ocm_configure(vha);
	}
#endif

	/* enable CRC and DEBUG registers */
	ret = vha_dbg_create_hwbufs(session);
	if (ret)
		goto out_free_mmu_ctx;

	img_pdump_printf("-- OPEN_END\n");

	/* Used for simulating system level suspend/resume functionality */
	if (list_empty(&vha->sessions) && vha->suspend_interval_msec) {
		INIT_DELAYED_WORK(&vha->suspend_dwork, suspend_test_worker);
		/* Start suspend worker */
		schedule_delayed_work(&vha->suspend_dwork,
				msecs_to_jiffies(vha->suspend_interval_msec));
	}

	/* Assign session id. */
	session->id = vha_session_id_cnt++;

	list_add_tail(&session->list, &vha->sessions);
	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++) {
		struct vha_session *aux_head = list_prev_entry(session, list);
		list_add(&session->sched_list[pri], &aux_head->sched_list[pri]);
	}

	/* All mmu contextes are successfully created,
	   it is safe to incremet the counters and assign id. */
	if (vha->mmu_mode != VHA_MMU_DISABLED)
		for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++) {
			uint8_t hw_ctxid = 0;
			/* Assign mmu hardware context */
			hw_ctxid = VHA_MMU_GET_CTXID(session);
			hw_ctxid += (VHA_MMU_AUX_HW_CTX_SHIFT*ctx_id);
			vha->mmu_ctxs[hw_ctxid]++;
			session->mmu_ctxs[ctx_id].hw_id = hw_ctxid;
		}

	dev_dbg(vha->dev,
			"%s: %p ctxid:%d\n", __func__, session,
			session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].id);

	trace_vha_session_in(session->id, 0);

	mutex_unlock(&vha->lock);
	return ret;

out_free_mmu_ctx:
	for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++)
		if (session->mmu_ctxs[ctx_id].ctx)
			img_mmu_ctx_destroy(session->mmu_ctxs[ctx_id].ctx);
out_unlock:
	mutex_unlock(&vha->lock);
	return ret;
}

static void vha_clean_onchip_maps(struct vha_session *session, struct vha_buffer *buf)
{
	struct vha_onchip_map *onchip_map = NULL, *tmp = NULL;

	WARN_ON(!buf);
	WARN_ON(!session);

	list_for_each_entry_safe(onchip_map, tmp, &buf->onchip_maps, list) {
		idr_remove(&session->onchip_maps, onchip_map->mapid);
		list_del(&onchip_map->list);
		kfree(onchip_map);
	}
}

#ifdef KERNEL_DMA_FENCE_SUPPORT
void vha_rm_buf_fence(struct vha_session *session, struct vha_buffer *buf)
{
	struct vha_buf_sync_info *sync_info = &buf->sync_info;
	img_mem_remove_fence(session->mem_ctx, buf->id);
	if (sync_info->in_fence) {
		if (!dma_fence_is_signaled(sync_info->in_fence))
			dma_fence_remove_callback(sync_info->in_fence, &sync_info->in_sync_cb);
		if (sync_info->in_sync_file) {
			fput(sync_info->in_sync_file);
			sync_info->in_sync_file = NULL;
		}
		sync_info->in_sync_fd = VHA_SYNC_NONE;
		dma_fence_put(sync_info->in_fence);
		sync_info->in_fence = NULL;
		memset(&sync_info->in_sync_cb, 0, sizeof(struct dma_fence_cb));
	}
}
#endif

#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
void vha_start_swd(struct vha_dev *vha,  int cmd_idx)
{
	if (vha->swd_period) {
		schedule_delayed_work(&vha->swd_dwork, msecs_to_jiffies(vha->swd_period));
	}
}
#endif

void vha_rm_session(struct vha_session *session)
{
	struct vha_dev *vha = session->vha;
	struct vha_session *cur_session, *tmp_session;
	struct vha_rsp *cur_rsp, *tmp_rsp;
	struct vha_buffer *cur_buf, *tmp_buf;
	bool reschedule = false;
	int ctx_id;
	uint8_t pri;

	mutex_lock(&vha->lock);

	img_pdump_printf("-- FREE_END\n");
	session->freeing = false;

	img_pdump_printf("-- CLOSE_BEGIN\n");

	/* Remove pend/queued session commands. */
	reschedule = vha_rm_session_cmds(session);

	/* Remove responses for session related commands. */
	list_for_each_entry_safe(cur_rsp, tmp_rsp, &session->rsps, list) {
		dev_warn(vha->dev,
				"Removing a session while the rsp is still pending\n");
		list_del(&cur_rsp->list);
		kfree(cur_rsp);
	}

	/* Disable CRC and DEBUG capture. */
#ifdef CONFIG_HW_MULTICORE
	vha_dbg_stop_hwbufs(session, vha->full_core_mask);
#else
	vha_dbg_stop_hwbufs(session, 0);
#endif
	vha_dbg_destroy_hwbufs(session);

	list_for_each_entry_safe(cur_buf, tmp_buf, &session->bufs, list) {
		dev_warn(vha->dev,
				"Removing a session while the buffer wasn't freed\n");
#ifdef KERNEL_DMA_FENCE_SUPPORT
		vha_rm_buf_fence(session, cur_buf);
#endif
		vha_clean_onchip_maps(session, cur_buf);
		list_del(&cur_buf->list);
		kfree(cur_buf);
	}

	/* Remove link from VHA's list. */
	list_for_each_entry_safe(cur_session, tmp_session,
				&vha->sessions, list) {
		if (cur_session == session)
			list_del(&cur_session->list);
	}
	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++) {
		list_for_each_entry_safe(cur_session, tmp_session,
					&vha->sched_sessions[pri], sched_list[pri]) {
			if (cur_session == session)
				list_del(&cur_session->sched_list[pri]);
		}
	}

	/* Reset hardware if required. */
	if ((list_empty(&vha->sessions) && !vha->do_calibration)
			|| reschedule
			)
		vha_dev_stop(vha, reschedule);

#ifndef CONFIG_HW_MULTICORE
	img_mmu_clear_cache(session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].ctx);
#endif

	/* Delete session's MMU memory contexts. */
	for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++) {
		img_mmu_ctx_destroy(session->mmu_ctxs[ctx_id].ctx);

		if (vha->mmu_mode != VHA_MMU_DISABLED) {
			uint8_t hw_ctxid = session->mmu_ctxs[ctx_id].hw_id;
			WARN_ON(!vha->mmu_ctxs[hw_ctxid]);
			if (vha->mmu_ctxs[hw_ctxid])
				vha->mmu_ctxs[hw_ctxid]--;
		}
	}

	/* Update mem stats - max memory usage in this session. */
	img_mem_get_usage(session->mem_ctx,
			(size_t *)&vha->stats.mem_usage_last, NULL);
	{
		uint32_t MB = vha->stats.mem_usage_last / (1024 * 1024);
		uint32_t bytes = vha->stats.mem_usage_last -
			(MB * (1024 * 1024));
		uint32_t kB = (bytes * 1000) / (1024 * 1024);

		dev_dbg(vha->dev,
			"%s: Total user memory used in session: %u.%u MB\n",
			__func__, MB, kB);
	}
	img_mmu_get_usage(session->mem_ctx,
			(size_t *)&vha->stats.mmu_usage_last, NULL);

	vha->active_mmu_ctx = VHA_INVALID_ID;
	img_pdump_printf("-- VHA driver session complete\n");
	img_pdump_printf("-- CLOSE_END\n");

	/* Used for simulating system level suspend/resume functionality */
	if (list_empty(&vha->sessions) && vha->suspend_interval_msec) {
		mutex_unlock(&vha->lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
		flush_scheduled_work();
#else
		flush_workqueue(vha->vha_workqueue);
#endif
		cancel_delayed_work_sync(&vha->suspend_dwork);
		mutex_lock(&vha->lock);
	}

	trace_vha_session_out(session->id, session->kicks);

	mutex_unlock(&vha->lock);

	/* Reschedule once the session is removed. */
	if (reschedule)
		vha_chk_cmd_queues(vha, true);
}

static int vha_alloc_common(struct vha_dev *vha)
{
#if 0
	img_pdump_printf("-- INIT_BEGIN\n");

	img_pdump_printf("-- INIT_END\n");
#endif
	return 0;
}

/****************** vha sysfs definition *************************************/
static ssize_t
BVNC_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	struct vha_hw_props *props = &vha->hw_props;

	return snprintf(buf, 4*6, "%hu.%hu.%hu.%hu\n",
			(unsigned short)(props->core_id >> 48),
			(unsigned short)(props->core_id >> 32),
			(unsigned short)(props->core_id >> 16),
			(unsigned short)props->core_id);
}

static ssize_t log_store(struct device *dev, struct device_attribute *attr, 
			const char *buf, size_t count)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);

	mutex_lock(&vha->lock);
	memset(&vha->stats, 0, sizeof(struct vha_stats));
	vha_reset_cnt++;
	mutex_unlock(&vha->lock);

	return count;
}

static ssize_t log_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	struct vha_session *session = NULL;
	int ret = 0;
	size_t mem_val = 0;

	mutex_lock(&vha->lock);

	len += scnprintf(buf + len, PAGE_SIZE - len,
		"[NPU] Driver Version: " VERSION_STRING "\n"
		"---------------------------------------MODULE STATUS--------------------------------------\n"
		"DevId    DevStatus    DevSessionNum    DevLoadingAvg_%%    TotalTasks    CompletedTasks\n"
		"  %d         %d             %d               %d               %lld            %lld\n"
		"-----------------------------------------MEM INFO-----------------------------------------\n"
		"MMU_page_size      MMU_mode\n"
		"    %ld              %d\n"
		"---------------------------------------INSTANCE INFO--------------------------------------\n"
		"AvgHwProcUs       LastHwProcUs       TotalHwProcUs       LastMemUsage       LastMmuUsage\n"
		"   %lld                 %lld               %lld                %d                %d\n"
		"--------------------------------------EXCEPTION INFO--------------------------------------\n"
		"total_failures      reset\n"
		"     %lld             %d\n",
		vha->id, vha->state, vha_session_id_cnt, vha->stats.cnn_utilization/10,
					vha->stats.cnn_kicks, vha->stats.cnn_kicks_completed,
		mmu_page_size_kb_lut[vha->mmu_page_size], vha->mmu_mode,
		vha->stats.cnn_avg_proc_us, vha->stats.last_proc_us, vha->stats.cnn_total_proc_us,
					vha->stats.mem_usage_last, vha->stats.mmu_usage_last,
		vha->stats.total_failures, vha_reset_cnt);

	list_for_each_entry(session, &vha->sessions, list) {
		if (!ret++) {
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"---------------------------------------SESSION INFO--------------------------------------\n"
				"SessionId     MemUsage      Cmds      AvgProcUs      LastProcUs     TotalProcUs\n");
		}
		img_mem_get_usage(session->mem_ctx, &mem_val, NULL);
		len += scnprintf(buf + len, PAGE_SIZE - len,
			"    %-7d   %-12ld  %-8lld  %-13lld  %-13lld  %lld\n",
			session->id, mem_val, session->kicks, session->avg_proc_us,
					session->last_proc_us, session->total_proc_us);
	}

	mutex_unlock(&vha->lock);

	return len;
}

static DEVICE_ATTR_RO(BVNC);

static struct attribute *vha_sysfs_entries[] = {
	&dev_attr_BVNC.attr,
	NULL,
};

static struct device_attribute dev_attr_log = __ATTR(log, 0664, log_show, log_store);

static struct attribute *vha_sysfs_attrs[] = {
	&dev_attr_log.attr,
	NULL,
};

static const struct attribute_group vha_attr_group = {
	.name = NULL,     /* put in device directory */
	.attrs  = vha_sysfs_entries,
};

static struct attribute_group vha_dev_attr_group = {
	.name = "info",    /* put in info directory */
	.attrs = vha_sysfs_attrs,
};

void vha_sched_apm(struct vha_dev *vha, struct vha_apm_work *apm_work)
{
	unsigned long work_at = jiffies + msecs_to_jiffies(apm_work->delay_ms);
	int ret;

	dev_dbg(vha->dev, "%s: core_mask:%#x delay:%d\n",
			__func__, apm_work->core_mask, apm_work->delay_ms);

	/*
	 * Try to queue the work.
	 */
	ret = schedule_delayed_work(&apm_work->dwork,
								work_at - jiffies);
	if (!ret) {
		/* Work is already in the queue.
		 * Canceling & rescheduling might be problematic,
		 * so just modify to postpone.
		 */
		mod_delayed_work(system_wq, &apm_work->dwork,
								work_at - jiffies);
	}
}

static void vha_apm_worker(struct work_struct *work)
{
	struct vha_apm_work *apm_work =
			container_of(work, struct vha_apm_work, dwork.work);
	struct vha_dev *vha = apm_work->vha;

	mutex_lock(&vha->lock);
	dev_dbg(vha->dev, "%s: apm expired! core_mask:%#x\n",
			__func__, apm_work->core_mask);
	vha_dev_apm_stop(vha, apm_work);
	mutex_unlock(&vha->lock);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
#define VHA_MAX_NODE_NAME 16
static int vha_workqueue_alloc(struct vha_dev *vha, unsigned int id)
{
	char wq_name[VHA_MAX_NODE_NAME] = { 0 };

	snprintf(wq_name, sizeof(wq_name)-1, "vha%d", vha->id);

	vha->vha_workqueue = alloc_workqueue(wq_name, 0, 0);
	if (IS_ERR_OR_NULL(vha->vha_workqueue)) {
		vha->vha_workqueue = NULL;
		return -ENOMEM;
	}

	return 0;
}
#endif

int vha_add_dev(struct device *dev,
		const struct heap_config heap_configs[], const int heaps,
		void *plat_data, void __iomem *reg_base, uint32_t reg_size)
{
	struct vha_dev_common* vha_common;
	struct vha_dev *vha;
	int ret;
	uint8_t id, pri;

	/* Validate module params. */
	ret = -EINVAL;
	if (low_latency > VHA_LL_SELF_KICK) {
		dev_err(dev, "%s: Unsupported low latency mode %u!\n", __func__, low_latency);
		goto out_validate_params;
	} else if ((mmu_mode != VHA_MMU_DISABLED) &&
				(mmu_mode != VHA_MMU_DIRECT) &&
				(mmu_mode != VHA_MMU_40BIT)) {
		dev_err(dev, "%s: Unsupported MMU mode %u!\n", __func__, mmu_mode);
		goto out_validate_params;
	} else if (mmu_ctx_default >= VHA_MMU_MAX_HW_CTXS) {
		dev_err(dev, "%s: Unsupported MMU context id %u!\n", __func__, mmu_ctx_default);
		goto out_validate_params;
	} else if (mmu_page_size > ARRAY_SIZE(mmu_page_size_kb_lut)) {
		dev_err(dev, "%s: Unsupported MMU page size %u!\n", __func__, mmu_page_size);
		goto out_validate_params;
	}
	ret = 0;

	vha_common = devm_kzalloc(dev, sizeof(struct vha_dev_common), GFP_KERNEL);
	if (!vha_common)
		return -ENOMEM;

	vha = devm_kzalloc(dev, sizeof(struct vha_dev), GFP_KERNEL);
	if (!vha) {
		ret = -ENOMEM;
		goto out_free_dev;
	}

	vha_common->vha_dev = vha;

	dev_dbg(dev, "%s: allocated vha_dev @ %px\n", __func__, vha);
	vha->dev                   = dev;
	vha->reg_base              = reg_base;
	vha->reg_size              = reg_size;
	vha->plat_data             = plat_data;
	vha->fault_inject          = fault_inject;
	vha->suspend_interval_msec = suspend_interval_msec;
	vha->hw_bypass             = hw_bypass;
	vha->low_latency           = low_latency;
	vha->no_clock_disable      = no_clock_disable;
	vha->pm_delay              = pm_delay;
	vha->mmu_mode              = mmu_mode;
	vha->mmu_ctx_default       = mmu_ctx_default;
	vha->mmu_page_size         = mmu_page_size;
	vha->mmu_base_pf_test      = test_mmu_base_pf;
	vha->mmu_no_map_count      = test_mmu_no_map_count;
	vha->ocm_paddr             = onchipmem_phys_start;
#ifdef VHA_SCF
	vha->parity_disable        = parity_disable;
	vha->confirm_config_reg    = confirm_config_reg;
#endif
	vha->cnn_combined_crc_enable = cnn_combined_crc_enable;
	vha->active_mmu_ctx        = VHA_INVALID_ID;
	vha->dump_buff_digest      = dump_buff_digest;

	/* Enable and configure pm_runtime*/
	if (!pm_runtime_enabled(vha->dev))
		pm_runtime_enable(vha->dev);
	pm_runtime_set_autosuspend_delay(vha->dev, VHA_CORE_SUSPEND_DELAY);
	pm_runtime_use_autosuspend(vha->dev);
	/* Resume device so that we can read the core props */
	if (pm_runtime_status_suspended(vha->dev))
		pm_runtime_get_sync(vha->dev);

	/* Read HW properties */
	ret = vha_dev_get_props(vha, onchipmem_size);
	if (ret) {
		dev_err(dev, "%s: could not get vha properties at %px\n",
			__func__, (__force void *)vha->reg_base);
		pm_runtime_put_sync_suspend(vha->dev);
		goto out_free_dev;
	}

	if (test_without_bvnc_check)
		vha->hw_props.skip_bvnc_check = true;

	mutex_init(&vha->lock);
	spin_lock_init(&vha->irq_lock);
	INIT_LIST_HEAD(&vha->sessions);
	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++)
		INIT_LIST_HEAD(&vha->sched_sessions[pri]);
	INIT_LIST_HEAD(&vha->heaps);

	ret = vha_init(vha, heap_configs, heaps);
	if (ret) {
		dev_err(dev, "%s: main component initialisation failed!",
			__func__);
		goto out_free_dev;
	}

	/* Initialise command data pump worker */
	INIT_WORK(&vha->worker, cmd_worker);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
	ret = vha_workqueue_alloc(vha, drv.num_devs);
	if (ret) {
		dev_err(dev, "%s: vha_workqueue alloc failed!", __func__);
		goto out_alloc_workqueue;
	}
#endif

#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
	/* Initialise hw processing time simulation worker */
#ifdef CONFIG_HW_MULTICORE
	{
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id ++) {
			INIT_DELAYED_WORK(&vha->dummy_dworks[id].dummy_dwork,
								vha_dummy_worker);
			vha->dummy_dworks[id].wm_id = id;
			vha->dummy_dworks[id].vha = vha;
		}
	}
#else
	INIT_DELAYED_WORK(&vha->dummy_dwork, vha_dummy_worker);
#endif
#endif

	dev_set_drvdata(dev, vha_common);

	ret = vha_api_add_dev(dev, vha, drv.num_devs);
	if (ret) {
		dev_err(dev, "%s: failed to add UM node!", __func__);
		goto out_add_dev;
	}

	vha_dbg_init(vha);
	ret = vha_pdump_init(vha, &vha_common->pdump);
	if (ret == 0)
		vha->hw_props.use_pdump = true;
	if (ret == -EPERM)
		goto out_alloc_common;
	else
		ret = 0;

	ret = vha_alloc_common(vha);
	if (ret) {
		dev_err(dev, "%s: failed to allocate common dev buffers!",
				__func__);
		goto out_alloc_common;
	}

#ifdef CONFIG_PM_DEVFREQ
	ret = vha_devfreq_init(vha->dev);
	if (ret) {
		dev_err(vha->dev, "failed to add vha dev to devfreq!\n");
	}
#endif

	pm_runtime_put_sync_autosuspend(vha->dev);

	/* Add device to driver context */
	list_add(&vha->list, &drv.devices);
	drv.num_devs++;

	if (sysfs_create_group(&dev->kobj, &vha_attr_group))
		dev_err(dev, "failed to create sysfs entries\n");

	if (sysfs_create_group(&dev->kobj, &vha_dev_attr_group))
		dev_err(dev, "failed to create info sysfs entries\n");

	vha->freq_khz = freq_khz;
#ifndef CONFIG_VHA_DUMMY
	if (vha->freq_khz < 0)
		vha->do_calibration = true; /* ??? OS0 ? */

	if (vha->freq_khz <= 0)
		vha->freq_khz = VHA_CORE_CLOCK_MHZ * 1000;

	if (vha->do_calibration)
		dev_info(dev, "%s: Core freq[kHz]: to be calibrated",
				__func__);
	else
		dev_info(dev, "%s: Core freq[kHz]: %u",
				__func__, vha->freq_khz);
#else
#  ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
	vha->freq_khz = VHA_CORE_CLOCK_MHZ * 1000;
	dev_info(dev, "%s: Core freq[kHz]: %u (faked for DUMMY device)",
			__func__, vha->freq_khz);
#  endif
#endif

	for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
		vha->apm_dworks[id].vha = vha;
		vha->apm_dworks[id].core_mask = 1 << id;
		vha->apm_dworks[id].delay_ms = vha->pm_delay;
		INIT_DELAYED_WORK(&vha->apm_dworks[id].dwork, vha_apm_worker);
	}

#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
	/* Initialise the SW wachdog */
	INIT_DELAYED_WORK(&vha->swd_dwork, wd_timer_callback);

	vha->swd_period = swd_period;
	vha->swd_timeout_default = swd_timeout_default;
	vha->swd_timeout_m0 = swd_timeout_m0;
	vha->swd_timeout_m1 = swd_timeout_m1;
#endif

	return ret;
out_alloc_common:
	vha_api_rm_dev(dev, vha);
	vha_dbg_deinit(vha);
out_add_dev:
	dev_set_drvdata(dev, NULL);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
	destroy_workqueue(vha->vha_workqueue);
out_alloc_workqueue:
#endif
	vha_deinit();
out_free_dev:
	devm_kfree(dev, vha);
	devm_kfree(dev, vha_common);
out_validate_params:
	return ret;
}

static void vha_free_common(struct vha_dev *vha)
{
	if (vha->fp_bufid) {
		img_mem_free(drv.mem_ctx, vha->fp_bufid);
		vha->fp_bufid = VHA_INVALID_ID;
	}
}

void vha_rm_dev(struct device *dev)
{
	struct vha_dev *vha;
	struct vha_dev_common* vha_common;
	int ret;
	uint8_t id, pri;

	vha_common = dev_get_drvdata(dev);
	BUG_ON(vha_common == NULL);
	vha = vha_common->vha_dev;

	if (!vha) {
		pr_err("%s: vha ptr is invalid!\n", __func__);
		return;
	}

	if (dev != vha->dev) {
		pr_err("%s: vha->dev is not properly initialised! (%p!=%p)\n", __func__, dev, vha->dev);
		return;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
	flush_scheduled_work();
#else
	flush_workqueue(vha->vha_workqueue);
#endif

	for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
		cancel_delayed_work_sync(&vha->apm_dworks[id].dwork);

#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
	cancel_delayed_work_sync(&vha->swd_dwork);
#endif

#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
#ifdef CONFIG_HW_MULTICORE
	{
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			cancel_delayed_work_sync(&vha->dummy_dworks[id].dummy_dwork);
	}
#else
	cancel_delayed_work_sync(&vha->dummy_dwork);
#endif
#endif
	if (!pm_runtime_status_suspended(vha->dev))
		pm_runtime_put_sync_suspend(vha->dev);
	pm_runtime_dont_use_autosuspend(vha->dev);
	pm_runtime_disable(vha->dev);

#ifdef CONFIG_PM_DEVFREQ
	vha_devfreq_term(dev);
#endif

	vha_free_common(vha);
#ifdef CONFIG_HW_MULTICORE
	vha_dev_scheduler_deinit(vha);
#endif

	while (!list_empty(&vha->heaps)) {
		struct vha_heap *heap = list_first_entry(&vha->heaps, struct vha_heap, list);
		list_del(&heap->list);
		if(!heap->global) /* remove only device heaps */
			img_mem_del_heap(heap->id);
		kfree(heap);
	}

	ret = vha_api_rm_dev(dev, vha);
	if (ret)
		dev_err(dev, "%s: failed to remove UM node!\n", __func__);

	list_del(&vha->sessions);
	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++)
		list_del(&vha->sched_sessions[pri]);
	list_del(&vha->list);
	list_del(&vha->heaps);
	BUG_ON(!drv.num_devs--);
	sysfs_remove_group(&dev->kobj, &vha_attr_group);
	sysfs_remove_group(&dev->kobj, &vha_dev_attr_group);

	vha_dbg_deinit(vha);
	vha_pdump_deinit(&vha_common->pdump);
	dev_set_drvdata(dev, NULL);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 0)
	destroy_workqueue(vha->vha_workqueue);
#endif

	devm_kfree(dev, vha);
	devm_kfree(dev, vha_common);
}

/* performs device self test operations */
int vha_dev_calibrate(struct device *dev, uint32_t cycles)
{
	int ret = 0;
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	if (!vha) {
		WARN_ON(1);
		return -EFAULT;
	}

	mutex_lock(&vha->lock);
	if (vha->do_calibration) {
		vha->calibration_cycles = cycles;
		dev_info(dev, "%s: Starting core frequency measurement (%d)...",
				__func__, cycles);
		ret = vha_dev_start(vha);
		if (ret)
			goto calib_err;
#if (defined(HW_AX2) || defined(CONFIG_HW_MULTICORE))
		vha_cnn_start_calib(vha);
#endif
	}
calib_err:
	mutex_unlock(&vha->lock);
	return ret;
}

/* map buffer into the device */
int vha_map_to_onchip(struct vha_session *session,
		uint32_t buf_id, uint64_t virt_addr, uint32_t page_size,
		unsigned int num_pages, uint32_t page_idxs[], uint32_t *mapid)
{
	struct vha_dev *vha = session->vha;
	struct vha_onchip_map *onchip_map = NULL;
	struct vha_buffer *buf = NULL;
	int map_id = *mapid;
	int ret = 0;
	int i = 0;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	buf = vha_find_bufid(session, buf_id);
	if (!buf) {
		dev_err(vha->dev, "%s: buffer id %d not found\n", __func__, buf_id);
		ret = -EINVAL;
		goto out_unlock;
	}

	if (map_id == 0) {
		onchip_map = kzalloc(sizeof(struct vha_onchip_map), GFP_KERNEL);
		if (!onchip_map) {
			ret = -ENOMEM;
			goto out_unlock;
		}

		map_id = idr_alloc(&session->onchip_maps, onchip_map,
					MIN_ONCHIP_MAP, MAX_ONCHIP_MAP, GFP_KERNEL);
		if (map_id < 0) {
			dev_err(vha->dev, "%s: idr_alloc failed\n", __func__);
			ret = map_id;
			goto alloc_id_failed;
		}

		ret = img_mmu_map(session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].ctx,
				session->mem_ctx, buf_id,
				virt_addr, IMG_MMU_PTE_FLAG_NONE);
		if (ret) {
			dev_err(vha->dev, "%s: map failed!\n", __func__);
			ret = -EFAULT;
			goto mmu_map_failed;
		}

		onchip_map->devvirt = virt_addr;
		onchip_map->mapid = map_id;
		onchip_map->bufid = buf_id;
		list_add(&onchip_map->list, &buf->onchip_maps);

		*mapid = map_id;
	} else {
		onchip_map = idr_find(&session->onchip_maps, map_id);
		if (!onchip_map) {
			dev_err(vha->dev, "%s: idr_find failed\n", __func__);
			ret = -EINVAL;
			goto out_unlock;
		}
	}

	for (i = 0; i < num_pages; i++) {
		ret = img_mmu_move_pg_to_cache(
				session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].ctx,
				session->mem_ctx, buf_id,
				onchip_map->devvirt, page_size, page_idxs[i]);
		if (ret) {
			dev_warn(vha->dev, "%s: moving a page to on chip ram failed!\n", __func__);
			goto out_unlock;
		}
	}

	dev_dbg(vha->dev, "%s: mapped buf %s (%u) to %#llx, num_pages: %d\n",
		__func__, buf->name, buf_id, virt_addr, num_pages);

	mutex_unlock(&vha->lock);
	return 0;

mmu_map_failed:
	idr_remove(&session->onchip_maps, map_id);
alloc_id_failed:
	kfree(onchip_map);
out_unlock:
	mutex_unlock(&vha->lock);
	return ret;
}

/* map buffer into the device */
int vha_map_buffer(struct vha_session *session,
		uint32_t buf_id, uint64_t virt_addr,
		uint32_t map_flags)
{
	struct vha_dev *vha = session->vha;
	uint32_t flags = IMG_MMU_PTE_FLAG_NONE;
	struct vha_buffer *buf = NULL;
	int ret = 0;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	if ((map_flags & (VHA_MAP_FLAG_READ_ONLY|VHA_MAP_FLAG_WRITE_ONLY)) ==
			(VHA_MAP_FLAG_READ_ONLY|VHA_MAP_FLAG_WRITE_ONLY)) {
		dev_err(vha->dev, "%s: invalid mapping flags combination: 0x%x\n",
			__func__, map_flags);
		ret = -EINVAL;
		goto out_unlock;
	}

	/* Convert permission flags to internal definitions */
	if (map_flags & VHA_MAP_FLAG_READ_ONLY)
		flags |= IMG_MMU_PTE_FLAG_READ_ONLY;

	/* Note: VHA_MAP_FLAG_WRITE_ONLY is not supported by the mmuv3 hw */

	/* Direct 1:1 mappings */
	if (vha->mmu_mode == VHA_MMU_DIRECT) {
		uint64_t *phys = img_mem_get_page_array(session->mem_ctx,
						buf_id);
		WARN_ON(!phys);
		/* Override virtual address,
		 * only applicable for physically contiguous memory regions */
		if (phys && phys[0]) {
			virt_addr = phys[0];
			dev_dbg(vha->dev,
					"%s: using direct mapping!\n",
					__func__);
		} else {
			dev_err(vha->dev,
					"%s: not contiguous memory!\n",
					__func__);
		}
	}

	buf = vha_find_bufid(session, buf_id);

#ifdef CONFIG_HW_MULTICORE
	if (buf->attr & IMG_MEM_ATTR_OCM) {
		uint64_t *phys = img_mem_get_page_array(session->mem_ctx,
								buf_id);
		/* Virtual == physical */
		buf->devvirt = phys[0];
		dev_dbg(vha->dev,
				"%s: buf %s (%u), is OCM buffer, no MMU mapping needed!\n",
				__func__, buf->name, buf_id);

		goto out_unlock;
	}
#endif

	/* force MMU fault after N buffer map operations */
	if (vha->mmu_no_map_count != 0) {
		int ctx_id;
		if (map_flags & VHA_MAP_FLAG_MODEL) {
			ctx_id = VHA_MMU_REQ_MODEL_CTXID;
			buf->req_type = VHA_REQ_MODEL;
		} else if (map_flags & VHA_MAP_FLAG_IO) {
			ctx_id = VHA_MMU_REQ_IO_CTXID;
			buf->req_type = VHA_REQ_IO;
		} else {
			WARN_ONCE(1, "No requestor flags!");
			ctx_id = VHA_MMU_REQ_IO_CTXID;
			buf->req_type = VHA_REQ_IO;
		}
		ret = img_mmu_map(session->mmu_ctxs[ctx_id].ctx,
				session->mem_ctx, buf_id, virt_addr, flags);
		if (ret || buf == NULL) {
			dev_err(vha->dev, "%s: map failed!\n", __func__);
			goto out_unlock;
		}
		if (vha->mmu_no_map_count >= 0)
			--vha->mmu_no_map_count;
	} else
		dev_info(vha->dev, "Bringup test: MMU no map count = %d\n",
			vha->mmu_no_map_count);

	buf->devvirt = virt_addr;
	dev_dbg(vha->dev, "%s: mapped buf %s (%u) to %#llx, flags: 0x%x\n",
		__func__, buf->name, buf_id, virt_addr, map_flags);

out_unlock:
	mutex_unlock(&vha->lock);
	return ret;
}

/* unmap buffer from the device */
int vha_unmap_buffer(struct vha_session *session,
		uint32_t buf_id)
{
	struct vha_dev *vha = session->vha;
	struct vha_buffer *buf = NULL;
	int ret = 0;
	int ctx_id;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	buf = vha_find_bufid(session, buf_id);

#ifdef CONFIG_HW_MULTICORE
	if (buf->attr & IMG_MEM_ATTR_OCM) {
		dev_dbg(vha->dev,
				"%s: buf %s (%u) is OCM buffer, no MMU unmapping needed!\n",
				__func__, buf->name, buf_id);
		buf->devvirt = ~0ULL;
		goto out_unlock;
	}
#endif

	if (buf->req_type == VHA_REQ_MODEL)
		ctx_id = VHA_MMU_REQ_MODEL_CTXID;
	else
		ctx_id = VHA_MMU_REQ_IO_CTXID;

	ret = img_mmu_unmap(session->mmu_ctxs[ctx_id].ctx,
				session->mem_ctx, buf_id);
	if (ret || buf == NULL) {
		dev_err(vha->dev, "%s: unmap failed!\n", __func__);
		goto out_unlock;
	}

	buf->devvirt = 0ULL;

	vha_clean_onchip_maps(session, buf);

	dev_dbg(vha->dev, "%s: unmapped buf %s(%u)\n",
		__func__, buf->name, buf_id);

out_unlock:
	mutex_unlock(&vha->lock);
	return ret;
}

/*
 * return either dev virtual address or physical address of buffer
 * phys address only applicable if contiguous memory
 * virtual address only if MMU enabled
 */
uint64_t vha_buf_addr(struct vha_session *session, struct vha_buffer *buf)
{
	struct vha_dev *vha = session->vha;

	if (vha->mmu_mode == VHA_MMU_DISABLED) {
		uint64_t *phys;

		/* no-MMU mode */
		if (vha->hw_props.dummy_dev)
			return 0; /* no-MMU: dummy hardware */

		phys = img_mem_get_page_array(session->mem_ctx, buf->id);
		if (phys)
			/*
			 * no-MMU: carveout memory
			 * Get the address that dev expects.
			 */
			return img_mem_get_dev_addr(session->mem_ctx,
						buf->id, phys[0]);

		dev_err(vha->dev, "%s: ERROR: buffer %x is not contiguous\n",
			__func__, buf->id);
		return 0; /* no-MMU: system memory */
	}

	/* mmu mode */
	if (buf == NULL)
		return 0;  /* error */

	return buf->devvirt; /* MMU mode: virt address */
}

struct vha_buffer *vha_find_bufid(const struct vha_session *session, uint32_t buf_id)
{
	struct vha_buffer *buf;

	list_for_each_entry(buf, &session->bufs, list) {
		if (buf_id == buf->id)
			return buf;
	}
	return NULL;
}

struct vha_buffer *vha_find_bufvaddr(const struct vha_session *session,
		uint64_t virt_addr)
{
	struct vha_buffer *buf;

	list_for_each_entry(buf, &session->bufs, list) {
		/* check if virtual address belongs to specific buffer */
		if (virt_addr >= buf->devvirt &&
				virt_addr < (buf->devvirt + buf->size))
			return buf;
	}
	return NULL;
}

/* when a buffer is allocated or imported, it is added to session.bufs */
int vha_add_buf(struct vha_session *session,
		uint32_t buf_id, size_t size, const char *name, enum img_mem_attr attr)
{
	struct vha_buffer *buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	struct vha_dev *vha = session->vha;
	int ret = 0;

	dev_dbg(vha->dev, "%s buf '%.*s' id:%d\n", __func__,
		(int)(sizeof(buf->name))-1, name, buf_id);

	if (buf == NULL)
		return -ENOMEM;

	buf->id  = buf_id;
	buf->size   = size;
	strncpy(buf->name, name, sizeof(buf->name)-1);
	buf->attr = attr;
	buf->status = VHA_BUF_UNFILLED;
	buf->session = session;
#ifdef KERNEL_DMA_FENCE_SUPPORT
	buf->sync_info.in_sync_fd = VHA_SYNC_NONE;
#endif
	list_add(&buf->list, &session->bufs);
	INIT_LIST_HEAD(&buf->onchip_maps);
	if (!(attr & IMG_MEM_ATTR_OCM))
		img_pdump_printf("-- <-- New buffer name: %s\n", buf->name);

	if (zero_buffers && !(buf->attr & IMG_MEM_ATTR_NOMAP)) {
		ret = img_mem_map_km(session->mem_ctx, buf_id);
		if (ret) {
			dev_err(session->vha->dev, "failed to map buff %x to km: %d\n",
				buf_id, ret);
			ret = -EFAULT;
			goto out_err;
		}
		buf->kptr = img_mem_get_kptr(session->mem_ctx, buf_id);

		{
			void *ptr = buf->kptr;
			int max_chunk = 1 * 1024 * 1024;
			while (size) {
				int chunk_size = size > max_chunk ?
						max_chunk : size;
				pr_debug("memset buf chunk %d!\n", chunk_size);
				memset(ptr, 0, chunk_size);
				ptr += chunk_size;
				size -= chunk_size;
				schedule();
			}
		}
		ret = img_mem_unmap_km(session->mem_ctx, buf->id);
		if (ret) {
			dev_err(session->vha->dev,
				"%s: failed to unmap buff %x from km: %d\n",
				__func__, buf->id, ret);
			ret = -EFAULT;
			goto out_err;
		}
		buf->kptr = NULL;
	}

	return 0;

out_err:
	list_del(&buf->list);
	kfree(buf);
	return ret;
}

/* remove buffer from the session */
int vha_rm_buf(struct vha_session *session, uint32_t buf_id)
{
	struct vha_buffer *buf = vha_find_bufid(session, buf_id);

	dev_dbg(session->vha->dev, "%s buf_id:%d\n", __func__, buf_id);
	if (buf == NULL) {
		dev_err(session->vha->dev, "%s: could not find buf %x\n",
			__func__, buf_id);
		return -EINVAL;
	}

#ifdef KERNEL_DMA_FENCE_SUPPORT
	vha_rm_buf_fence(session, buf);
#endif
	vha_clean_onchip_maps(session, buf);

	list_del(&buf->list);
	kfree(buf);

	return 0;
}

/* process the cmd if everything is ready */
enum do_cmd_status vha_do_cmd(struct vha_cmd *cmd)
{
	struct vha_session *session = cmd->session;
	struct vha_dev* vha = session->vha;

	/* already submitted, wait until processed */
	if (cmd->in_hw)
		return CMD_IN_HW;

	/* check all input buffers are filled and ready to go */
	if (vha_is_waiting_for_inputs(session, cmd))
		return CMD_WAIT_INBUFS;

#if !defined(CONFIG_VHA_DUMMY) && !defined(CONFIG_HW_MULTICORE)
	if (!session->vha->is_ready)
		return CMD_HW_BUSY;
#endif

	/* check hw availability (if needed) */
#ifdef CONFIG_HW_MULTICORE
	/* Attempt to schedule command on available cores. */
	if (vha_dev_schedule_cmd(session->vha, cmd) != 0)
#else
	/* Check if the core's queue is full. */
	if (vha_is_queue_full(session->vha, cmd))
#endif
		return CMD_HW_BUSY;

	if (cmd->user_cmd.cmd_type == VHA_CMD_CNN_SUBMIT &&
			!session->vha->stats.cnn_kicks)
		img_pdump_printf("-- ALLOC_END\n");

	/* at this point we should be able to process the cmd */
	if (vha_do_cnn_cmd(cmd) != 0)
		return CMD_DONE;

	return CMD_OK;
}

/* check if there is any work to be done */
static void cmd_worker(struct work_struct *work)
{
	struct vha_dev *vha = container_of(work, struct vha_dev, worker);

	dev_dbg(vha->dev, "%s\n", __func__);
	mutex_lock(&vha->lock);

#ifdef CONFIG_FAULT_INJECTION
	if (task_pid_nr(current) != vha->irq_bh_pid) {
		if (vha->fault_inject & VHA_FI_CMD_WORKER)
			current->make_it_fail = true;
		else
			current->make_it_fail = false;
	}
#endif

	if (vha->do_calibration) {
		/* Postpone any worker tasks. */
		dev_dbg(vha->dev, "%s: Postpone worker task!\n", __func__);
		goto exit;
	}

	/* Execute the main scheduling loop. */
	vha_scheduler_loop(vha);

exit:
#ifdef CONFIG_FAULT_INJECTION
	if (task_pid_nr(current) != vha->irq_bh_pid) {
		if (vha->fault_inject & VHA_FI_CMD_WORKER)
			current->make_it_fail = false;
	}
#endif
	mutex_unlock(&vha->lock);
}

/* this is wrapper func for scheduling command worker task */
void vha_chk_cmd_queues(struct vha_dev *vha, bool threaded)
{
	dev_dbg(vha->dev, "%s threaded:%u\n", __func__, threaded);
	if (threaded) {
		/* If work has been already scheduled from other context,
		 * the below call does nothing (returns false).
		 * However the worker is only used as command data pump,
		 * so it is not necessary to do any kind of rescheduling,
		 * as it will be executed anyway!
		 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
		schedule_work(&vha->worker);  /* call asynchronously */
#else
		queue_work(vha->vha_workqueue, &vha->worker);  /* call asynchronously */
#endif
	} else {
		/* Direct calls must be always invoked
		 * with vha_dev.lock == locked
		 */
		BUG_ON(!mutex_is_locked(&vha->lock));
		mutex_unlock(&vha->lock);
		cmd_worker(&vha->worker);  /* call synchronously */
		mutex_lock(&vha->lock);
	}
}

#ifdef KERNEL_DMA_FENCE_SUPPORT
/* input buffer sync callback */
static void _vha_in_buf_sync_cb(struct dma_fence *fence,
		struct dma_fence_cb *cb)
{
	struct vha_buffer *buf = container_of(cb, struct vha_buffer, sync_info.in_sync_cb);

	vha_set_buf_status(buf->session, buf->id, VHA_BUF_FILLED_BY_SW,
			VHA_SYNC_NONE, false);
	fput(buf->sync_info.in_sync_file);
	dma_fence_put(fence);
	memset(&buf->sync_info, 0, sizeof(struct vha_buf_sync_info));
	buf->sync_info.in_sync_fd = VHA_SYNC_NONE;
}
#endif

/* set buffer status per user request: either filled or unfilled */
int vha_set_buf_status(struct vha_session *session, uint32_t buf_id,
		enum vha_buf_status status, int in_sync_fd, bool out_sync_sig)
{
	struct vha_buffer *buf = vha_find_bufid(session, buf_id);

	if (buf == NULL) {
		dev_err(session->vha->dev, "%s: invalid buf id:%d\n",
			__func__, buf_id);
		return -EINVAL;
	}

	dev_dbg(session->vha->dev, "%s: id:%d curr:%d new:%d sig:%d\n",
			__func__, buf->id, buf->status, status, out_sync_sig);
	/* If buffer has been filled by HW,
	 * mark that it probably needs invalidation, not necessarily,
	 * as it can be the input for the next hw segment,
	 * and may not be mapped by the UM */
	if (buf->status != VHA_BUF_FILLED_BY_HW &&
			status == VHA_BUF_FILLED_BY_HW) {
		buf->inval = true;
#ifdef KERNEL_DMA_FENCE_SUPPORT
		buf->status = status;
#endif
	}

	/* If buffer has been filled by SW,
	 * mark that it needs flushing */
	if (buf->status == VHA_BUF_UNFILLED &&
			status == VHA_BUF_FILLED_BY_SW) {
		buf->flush = true;
#ifdef KERNEL_DMA_FENCE_SUPPORT
		if (in_sync_fd > 0) {
			if (buf->sync_info.in_sync_fd < 0) {
				int ret = 0;
				struct file *sync_file;
				struct dma_fence *fence;

				sync_file = fget(in_sync_fd);
				if (sync_file == NULL) {
					dev_err(session->vha->dev, "%s: could not get file for fd=%d and buf %d\n",
						__func__, in_sync_fd, buf_id);
					return -EINVAL;
				}

				fence = sync_file_get_fence(in_sync_fd);
				if (!fence) {
					fput(sync_file);
					dev_err(session->vha->dev, "%s: could not get fence for fd=%d and buf %d\n",
						__func__, in_sync_fd, buf_id);
					return -EINVAL;
				}

				ret = dma_fence_add_callback(fence, &buf->sync_info.in_sync_cb,
																		_vha_in_buf_sync_cb);
				if (ret) {
					if (dma_fence_is_signaled(fence)) {
						dma_fence_put(fence);
						buf->status = status;
					} else
						dev_err(session->vha->dev, "%s: could not set cb for fd=%d and buf %x\n",
										__func__, in_sync_fd, buf_id);
					fput(sync_file);
					return ret;
				}
				buf->sync_info.in_fence = fence;
				buf->sync_info.in_sync_file = sync_file;
				buf->sync_info.in_sync_fd = in_sync_fd;
			} else if (in_sync_fd != buf->sync_info.in_sync_fd) {
				dev_err(session->vha->dev, "%s: buf %d has already assigned sync file fd=%d\n",
					__func__, buf_id, in_sync_fd);
				return -EINVAL;
			}
		}
		else {
			if (out_sync_sig)
				img_mem_signal_fence(session->mem_ctx, buf->id);
			buf->status = status;
		}
#endif
	}

	/* If buffer has been filled by SW,
	 * after being filled by the hw, flush it too */
	if (buf->status == VHA_BUF_FILLED_BY_HW &&
			status == VHA_BUF_FILLED_BY_SW) {
		buf->flush = true;
	}

#ifdef KERNEL_DMA_FENCE_SUPPORT
	if (status != VHA_BUF_FILLED_BY_SW)
#endif
		buf->status = status;

	/* Poke the command queue only when filled by SW */
	if (status == VHA_BUF_FILLED_BY_SW) {
		/* We are already locked!
		 * Run in separate thread
		 */
		vha_chk_cmd_queues(session->vha, true);
	}
	return 0;
}

bool vha_buf_needs_inval(struct vha_session *session, uint32_t buf_id)
{
	struct vha_buffer *buf = vha_find_bufid(session, buf_id);
	bool inval;

	if (buf == NULL) {
		dev_err(session->vha->dev, "%s: invalid buf id:%d\n",
			__func__, buf_id);
		return false;
	}

	/* Buffer that has been allocated as HW access only
	 * does not need invalidation */
	if (buf->attr & (IMG_MEM_ATTR_NOMAP|IMG_MEM_ATTR_NOSYNC)) {
		dev_dbg(session->vha->dev, "%s: id:%d (skip)\n",
				__func__, buf->id);
		return false;
	}

	dev_dbg(session->vha->dev, "%s: id:%d (%d)\n",
			__func__, buf->id, buf->inval);

	inval = buf->inval;
	buf->inval = false;

	return inval;
}

bool vha_buf_needs_flush(struct vha_session *session, uint32_t buf_id)
{
	struct vha_buffer *buf = vha_find_bufid(session, buf_id);
	bool flush;

	if (buf == NULL) {
		dev_err(session->vha->dev, "%s: invalid buf id:%d\n",
			__func__, buf_id);
		return false;
	}
	dev_dbg(session->vha->dev, "%s: id:%d (%d)\n",
			__func__, buf->id, buf->flush);

	flush = buf->flush;
	buf->flush = false;

	return flush;
}

#ifdef KERNEL_DMA_FENCE_SUPPORT
struct vha_sync_cb_data {
	struct dma_fence_cb cb;
	union {
		struct sync_file *sync_file;
		struct file *file;
	};
};

static void _vha_out_sync_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct vha_sync_cb_data *cb_data =
			container_of(cb, struct vha_sync_cb_data, cb);
	fput(cb_data->sync_file->file);
	dma_fence_put(fence);
	kfree(cb_data);
}

int vha_create_output_sync(struct vha_session *session, uint32_t buf_id_count,
		uint32_t *buf_ids)
{
	int i;
	int ret = -ENOMEM;
	int sync_fd = VHA_SYNC_NONE;
	struct device *dev = session->vha->dev;
	struct dma_fence_array *fence_array = NULL;
	struct vha_sync_cb_data *cb_data = NULL;
	struct dma_fence **fences =
			(struct dma_fence **)kmalloc_array(sizeof(struct buffer_fence*),
																				buf_id_count, GFP_KERNEL);
	if (fences == NULL) {
		dev_err(dev, "%s: failed allocating fence container for %u buffers\n",
			__func__, buf_id_count);
		return -ENOMEM;
	}

	cb_data = kzalloc(sizeof(struct vha_sync_cb_data), GFP_KERNEL);
	if (cb_data == NULL) {
		dev_err(dev, "%s: failed allocating fence callback for %u buffers\n",
			__func__, buf_id_count);
		kfree(fences);
		return -ENOMEM;
	}

	for (i = 0; i < buf_id_count; i++) {
		fences[i] = img_mem_add_fence(session->mem_ctx, buf_ids[i]);
		if (!fences[i]) {
			dev_err(dev, "%s: failed allocating fence for buffer id=%u\n",
				__func__, buf_ids[i]);
			goto err_fences;
		}
	}

	fence_array = dma_fence_array_create(buf_id_count, fences,
									dma_fence_context_alloc(1), 1, false);
	if (fence_array == NULL) {
		dev_err(dev, "%s: failed allocating fence array for %u buffers\n",
			__func__, buf_id_count);
		goto err_fences;
	}

	cb_data->sync_file = sync_file_create(&fence_array->base);
	if (cb_data->sync_file == NULL) {
		dev_err(dev, "%s: failed creating sync file for %u buffers\n",
					__func__, buf_id_count);
		goto error_sf;
	}

	sync_fd = get_unused_fd_flags(O_CLOEXEC);
	if (sync_fd < 0) {
		dev_err(dev, "%s: failed creating file descriptor for %u buffers\n",
					__func__, buf_id_count);
		ret = sync_fd;
		goto error_fd;
	}

	ret = dma_fence_add_callback(&fence_array->base, &cb_data->cb,
															_vha_out_sync_cb);
	if (ret < 0) {
		dev_err(dev, "%s: failed adding callback file descriptor for %u buffers\n",
					__func__, buf_id_count);
		goto error_fd;
	}

	fd_install(sync_fd, cb_data->sync_file->file);
	fget(sync_fd);

	return sync_fd;

error_fd:
	fput(cb_data->sync_file->file);
	dma_fence_put(&fence_array->base);
error_sf:
	dma_fence_put(&fence_array->base);
err_fences:
	i--;
	for (; i >= 0; i--) {
		img_mem_remove_fence(session->mem_ctx, buf_ids[i]);
	}
	kfree(cb_data);
	return ret;
}

/* input sync callback */
static void _vha_in_sync_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct vha_sync_cb_data *cb_data =
			container_of(cb, struct vha_sync_cb_data, cb);
	fput(cb_data->file);
	dma_fence_put(fence);
	kfree(cb_data);
}
/* merged input sync callback */
static void _vha_in_merged_sync_cb(struct dma_fence *fence,
		struct dma_fence_cb *cb)
{
	struct vha_sync_cb_data *cb_data =
			container_of(cb, struct vha_sync_cb_data, cb);
	fput(cb_data->sync_file->file);
	dma_fence_put(fence);
}

int vha_merge_input_syncs(struct vha_session *session, uint32_t in_sync_fd_count,
		int *in_sync_fds)
{
	struct device *dev = session->vha->dev;
	int i, actual_count = 0;
	int ret = -ENOMEM;
	int sync_fd = VHA_SYNC_NONE;
	struct dma_fence_array *fence_array = NULL;
	struct vha_sync_cb_data *cb_data = NULL;
	struct vha_sync_cb_data *in_sync_cbs = NULL;
	struct dma_fence **fences;
	void *dma_fence_mem;
	struct file *f;

	/* Special cases. */
	if (in_sync_fd_count == 0) {
		dev_err(dev, "%s: requested 0 sync_fds to merge\n", __func__);
		return -EINVAL;
	} else if (in_sync_fd_count == 1) {
		struct file *f;
		struct dma_fence *fence;
		f = fget(in_sync_fds[0]);
		if (f == NULL) {
			dev_err(dev, "%s: could not get file for input sync fd=%d\n",
							__func__, in_sync_fds[0]);
			return -EINVAL;
		}
		fence = sync_file_get_fence(in_sync_fds[0]);
		if (!fence) {
			fput(f);
			dev_err(dev, "%s: could not get fence for input sync fd=%d\n",
							__func__, in_sync_fds[0]);
			return -EINVAL;
		}
		cb_data = kmalloc(sizeof(struct vha_sync_cb_data), GFP_KERNEL);
		if (cb_data == NULL) {
			fput(f);
			dma_fence_put(fence);
			dev_err(dev, "%s: failed allocating callback data for input sync fd=%d\n",
							__func__, in_sync_fds[0]);
			return -ENOMEM;
		}
		if (dma_fence_add_callback(fence, &cb_data->cb, _vha_in_sync_cb)) {
			if (dma_fence_is_signaled(fence)) {
				dev_warn(dev, "%s: input sync fd=%d already signalled\n",
								__func__, in_sync_fds[0]);
				ret = -EINVAL;
			} else {
				dev_err(dev, "%s: could not add fence callback for input sync fd=%d\n",
								__func__, in_sync_fds[0]);
				ret = -EFAULT;
			}
			fput(f);
			dma_fence_put(fence);
			kfree(cb_data);
			return ret;
		}
		cb_data->file = f;
		return in_sync_fds[0];
	}

	dma_fence_mem =
			kmalloc_array(
					(sizeof(struct dma_fence*) + sizeof(struct vha_sync_cb_data)),
					in_sync_fd_count + sizeof(struct vha_sync_cb_data), GFP_KERNEL);
	if (dma_fence_mem == NULL) {
		dev_err(dev, "%s: failed allocating fence container for %u buffers\n",
						__func__, in_sync_fd_count);
		return -ENOMEM;
	}
	fences = (struct dma_fence**)dma_fence_mem;
	in_sync_cbs = (struct vha_sync_cb_data *)(dma_fence_mem +
										sizeof(struct dma_fence*) * in_sync_fd_count);
	cb_data = (struct vha_sync_cb_data *)(dma_fence_mem +
								(sizeof(struct dma_fence*) + sizeof(struct vha_sync_cb_data)) *
										in_sync_fd_count);

	for (i = 0; i < in_sync_fd_count; i++) {
		struct dma_fence *fence;
		f = fget(in_sync_fds[i]);
		if (f == NULL) {
			dev_warn(dev, "%s: could not get file for fd=%d; will not use it\n",
							__func__, in_sync_fds[i]);
			continue;
		}
		fence = sync_file_get_fence(in_sync_fds[i]);
		if (!fence) {
			fput(f);
			dev_warn(dev, "%s: could not get fence for fd=%d; will not use it\n",
							__func__, in_sync_fds[i]);
			continue;
		}
		if (dma_fence_add_callback(fence, &in_sync_cbs[actual_count].cb,
															_vha_in_sync_cb)) {
			if (dma_fence_is_signaled(fence)) {
				dev_warn(dev, "%s: input sync fd=%d already signalled\n",
								__func__, in_sync_fds[i]);
			} else {
				dev_err(dev, "%s: could not add fence callback for input sync fd=%d;"
								" will not use it\n", __func__, in_sync_fds[i]);
			}
			fput(f);
			dma_fence_put(fence);
			continue;
		}
		dma_fence_get(fence); /* should be freed in dma_fence_array_release() */
		in_sync_cbs[actual_count].file = f;
		fences[actual_count] = fence;
		actual_count++;
	}
	if (actual_count == 0) {
		dev_err(dev, "%s: failed merging input fences\n", __func__);
		kfree(dma_fence_mem);
		return -EINVAL;
	}

	fence_array = dma_fence_array_create(actual_count, fences,
									dma_fence_context_alloc(1), 1, false);
	if (fence_array == NULL) {
		dev_err(dev, "%s: failed allocating fence array for %u buffers\n",
						__func__, in_sync_fd_count);
		kfree(dma_fence_mem);
		return -ENOMEM;
	}

	cb_data->sync_file = sync_file_create(&fence_array->base);
	if (cb_data->sync_file == NULL) {
		dev_err(dev, "%s: failed creating sync file for %u buffers\n",
						__func__, in_sync_fd_count);
		goto error_sf;
	}

	sync_fd = get_unused_fd_flags(O_CLOEXEC);
	if (sync_fd < 0) {
		dev_err(dev, "%s: failed creating file descriptor for %u buffers\n",
						__func__, in_sync_fd_count);
		ret = sync_fd;
		goto error_fd;
	}

	ret = dma_fence_add_callback(&fence_array->base, &cb_data->cb,
															_vha_in_merged_sync_cb);
	if (ret < 0) {
		dev_err(dev, "%s: failed adding callback file descriptor for %u buffers\n",
						__func__, in_sync_fd_count);
		goto error_fd;
	}

	fd_install(sync_fd, cb_data->sync_file->file);
	fget(sync_fd);

	return sync_fd;

error_fd:
	fput(cb_data->sync_file->file);
	dma_fence_put(&fence_array->base);
error_sf:
	for (i = 0; i < actual_count; i++) {
		fput(in_sync_cbs[actual_count].file);
		dma_fence_put(fences[actual_count]);
	}
	dma_fence_put(&fence_array->base);
	return ret;
}

int vha_release_syncs(struct vha_session *session, uint32_t buf_id_count,
		uint32_t *buf_ids)
{
	struct device *dev = session->vha->dev;
	int i;

	for (i = 0; i < buf_id_count; i++) {
		struct vha_buffer *buf = vha_find_bufid(session, buf_ids[i]);
		if (buf == NULL) {
			dev_warn(dev, "%s: could not find buf %u\n", __func__, buf_ids[i]);
		} else {
			vha_rm_buf_fence(session, buf);
		}
	}

	return 0;
}
#endif

/* validate and queue a message from a user
 * called with mutex locked */
int vha_add_cmd(struct vha_session *session, struct vha_cmd *cmd)
{
	uint32_t i;
	struct device *dev = session->vha->dev;
	struct vha_user_cmd *user_cmd = &cmd->user_cmd;
	/* number of words in vha_user_cmd->data[0] */
	uint32_t num_params = (cmd->size - sizeof(struct vha_user_cmd))/sizeof(uint32_t);
	uint32_t pri_q_count = 1;

#ifdef CONFIG_HW_MULTICORE
	if (user_cmd->cmd_type == VHA_CMD_CNN_SUBMIT) {
		dev_err(dev, "%s: invalid cmd type 0x%x\n", __func__, user_cmd->cmd_type);
		return -EINVAL;
	}
#endif

	if (user_cmd->num_bufs > num_params * sizeof(uint32_t)) {
		dev_err(dev, "%s: invalid number of buffers in message: in:%x total:%x>%lx\n",
			__func__, user_cmd->num_inbufs, user_cmd->num_bufs,
			num_params * sizeof(uint32_t));
		return -EINVAL;
	}

	if (user_cmd->num_bufs > VHA_MAX_ALT_ADDRS) {
		dev_err(dev, "%s: invalid number of buffers in message: %x max:%x\n",
			__func__, user_cmd->num_bufs, VHA_MAX_ALT_ADDRS);
		return -EINVAL;
	}

	if (!session->vha->cnn_combined_crc_enable && (cmd->user_cmd.flags & VHA_CHECK_CRC)) {
		dev_err(dev, "%s: Trying to perform CRC check while combined CRCs are disabled!,"
					 " try cnn_combined_crc_enable=1\n", __func__);
		return -EINVAL;
	}

	if (user_cmd->priority >= VHA_MAX_PRIORITIES) {
#if defined(CONFIG_HW_MULTICORE) || (defined(HW_AX3) && defined(VHA_USE_LO_PRI_SUB_SEGMENTS))
		dev_warn(dev, "%s: Priority %u too high. Setting to max supported priority: %u.\n",
				__func__, user_cmd->priority, VHA_MAX_PRIORITIES - 1);
		user_cmd->priority = VHA_MAX_PRIORITIES - 1;
#else
		dev_warn_once(dev, "%s: Priorities not supported.\n", __func__);
		user_cmd->priority = VHA_DEFAULT_PRI;
#endif
	}

	switch(cmd->user_cmd.cmd_type) {
		case VHA_CMD_CNN_SUBMIT:
		{
			struct vha_user_cnn_submit_cmd* submit_cmd =
					(struct vha_user_cnn_submit_cmd*)user_cmd;

			/* subsegments cannot be handled with low latency enabled */
			if ((submit_cmd->subseg_num > 1) && (session->vha->low_latency != VHA_LL_DISABLED)) {
				dev_err(dev, "%s: Subsegments are not supported with low latency enabled\n", __func__);
				return -EINVAL;
			}
			/* include subsegments in priority counters */
			pri_q_count = submit_cmd->subseg_num;

			/* check input and output buffers are valid */
			for (i = 0; i < user_cmd->num_bufs; i++) {
				uint32_t buf_id = user_cmd->data[i];

				if (vha_find_bufid(session, buf_id) == NULL) {
					dev_err(dev, "%s: unrecognised buf id[%u]:%x\n",
						__func__, i, buf_id);
					return -EINVAL;
				}
			}
			/* send out a event notifications when submit is enqueued */
			if (vha_observers.enqueued)
				vha_observers.enqueued(session->vha->id, session->id,
								cmd->user_cmd.cmd_id, cmd->user_cmd.priority);
			break;
		}
		case VHA_CMD_CNN_SUBMIT_MULTI:
		{
			uint32_t num_cmd_bufs = 0;

			/* check if command stream buffers are valid */
			for (i = 0; i < VHA_MAX_CORES; i++) {
				uint32_t buf_id = user_cmd->data[i];

				if (buf_id == 0)
					break;
				if (vha_find_bufid(session, buf_id) == NULL) {
					dev_err(dev, "%s: unrecognised cmdstr buf id[%u]:%x\n",
						__func__, i, buf_id);
					return -EINVAL;
				}
				num_cmd_bufs++;
			}
			/* check input and output buffers are valid */
			for (i = VHA_MAX_CORES; i < (user_cmd->num_bufs - 1); i++) {
				uint32_t buf_id = user_cmd->data[i];

				if (vha_find_bufid(session, buf_id) == NULL) {
					dev_err(dev, "%s: unrecognised buf id[%u]:%x\n",
						__func__, i, buf_id);
					return -EINVAL;
				}
			}
			/* send out a event notifications when submit is enqueued */
			if (vha_observers.enqueued)
				vha_observers.enqueued(session->vha->id, session->id,
								cmd->user_cmd.cmd_id, cmd->user_cmd.priority);
			break;
		}
		case VHA_CMD_CNN_PDUMP_MSG:
		{
			struct pdump_descr* pdump = vha_pdump_dev_get_drvdata(dev);
			if (!img_pdump_enabled(pdump)) {
				kfree(cmd);
				/* Silently ignore this pdump message */
				return 0;
			}
		}
	}
	/* add the command to the pending list */
	list_add_tail(&cmd->list[cmd->user_cmd.priority], &session->cmds[cmd->user_cmd.priority]);
	GETNSTIMEOFDAY(&cmd->submit_ts);
	session->vha->pri_q_counters[cmd->user_cmd.priority] += pri_q_count;

	/* We are already locked!
	 * Run in separate thread
	 */
	vha_chk_cmd_queues(session->vha, true);

	return 0;
}

int vha_suspend_dev(struct device *dev)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	int ret;
	mutex_lock(&vha->lock);

#ifdef CONFIG_PM_DEVFREQ
	ret = vha_devfreq_suspend(dev);
	if (ret)
		dev_err(dev, "%s: Failed to suspend the vha devfreq!\n", __func__);
#endif

	dev_dbg(dev, "%s: taking a nap!\n", __func__);

	ret = vha_dev_suspend_work(vha);

	mutex_unlock(&vha->lock);

	return ret;
}

int vha_resume_dev(struct device *dev)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	int ret;

	mutex_lock(&vha->lock);
	dev_dbg(dev, "%s: waking up!\n", __func__);
	/* Call the worker */
	vha_chk_cmd_queues(vha, true);

#ifdef CONFIG_PM_DEVFREQ
	ret = vha_devfreq_resume(dev);
	if (ret)
		dev_err(dev, "%s: Failed to resume the vha devfreq!\n", __func__);
#endif

	mutex_unlock(&vha->lock);

	return 0;
}

void vha_dump_digest(struct vha_session *session, struct vha_buffer *buf,
		struct vha_cmd *cmd)
{
	struct vha_dev *vha = session->vha;
	int ret;

	if (!vha->dump_buff_digest)
		return;

	if (!(buf->attr & IMG_MEM_ATTR_NOMAP)) {
		ret = img_mem_map_km(session->mem_ctx, buf->id);
		if (ret) {
			dev_err(session->vha->dev, "failed to map buff %x to km: %d\n",
				buf->id, ret);
			return;
		}
		buf->kptr = img_mem_get_kptr(session->mem_ctx, buf->id);

		dev_info(vha->dev, "%s: buff id:%d name:%s digest is [crc32]:%#x\n",
				__func__, buf->id, buf->name, crc32(0, buf->kptr, buf->size));

		ret = img_mem_unmap_km(session->mem_ctx, buf->id);
		if (ret) {
			dev_err(session->vha->dev,
				"%s: failed to unmap buff %x from km: %d\n",
				__func__, buf->id, ret);
		}
		buf->kptr = NULL;
	}
}

int vha_get_cnntotal_proc_us(struct device *dev, uint64_t *proc_us, uint64_t *cur_proc_us)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	if (!vha)
		return -EFAULT;

	*proc_us = vha->stats.cnn_total_proc_us;

	vha_currcmd_exetime_req(vha, cur_proc_us);
	
	return 0;
}


/*
 * register event observers.
 * only a SINGLE observer for each type of event.
 * unregister by passing NULL parameter
*/
void vha_observe_event_enqueue(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint32_t priority))
{
	if (func && vha_observers.enqueued)
		pr_warn("%s: vha_observer for ENQUEUED events is already set to '%pf'\n",
			__func__, vha_observers.enqueued);
	vha_observers.enqueued = func;
}
EXPORT_SYMBOL(vha_observe_event_enqueue);

void vha_observe_event_submit(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							bool last_subsegment,
							uint32_t priority))
{
	if (func && vha_observers.submitted)
		pr_warn("%s: vha_observer for SUBMITTED events is already set to '%pf'\n",
			__func__, vha_observers.submitted);
	vha_observers.submitted = func;
}
EXPORT_SYMBOL(vha_observe_event_submit);

void vha_observe_event_complete(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint64_t status,
							uint64_t cycles,
							uint64_t mem_usage,
							uint32_t priority))
{
	if (func && vha_observers.completed)
		pr_warn("%s: vha_observer for COMPLETED events is already set to '%pf'\n",
			__func__, vha_observers.completed);
	vha_observers.completed = func;
}
EXPORT_SYMBOL(vha_observe_event_complete);

void vha_observe_event_cancel(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint32_t priority))
{
	if (func && vha_observers.canceled)
		pr_warn("%s: vha_observer for CANCELED events is already set to '%pf'\n",
			__func__, vha_observers.canceled);
	vha_observers.canceled = func;
}
EXPORT_SYMBOL(vha_observe_event_cancel);

void vha_observe_event_error(void (*func)(uint32_t devid,
							uint32_t sessionid,
							uint32_t cmdid,
							uint64_t status))
{
	if (func && vha_observers.error)
		pr_warn("%s: vha_observer for ERROR events is already set to '%pf'\n",
			__func__, vha_observers.error);
	vha_observers.error = func;
}
EXPORT_SYMBOL(vha_observe_event_error);

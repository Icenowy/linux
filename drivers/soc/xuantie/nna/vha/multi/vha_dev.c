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

#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/preempt.h>
#include <linux/pm_runtime.h>
#include <linux/random.h>
#include <linux/slab.h>

#include <uapi/vha.h>
#include <uapi/vha_errors.h>
#include "vha_common.h"
#include "vha_plat.h"
#include "vha_regs.h"
#include "vha_mt19937.h"

static uint32_t shared_onchipmem_size;
module_param(shared_onchipmem_size, uint, 0444);
MODULE_PARM_DESC(shared_onchipmem_size,
	"Size of shared on-chip memory in bytes");

/* WM debug statistics types */
#define VHA_WM_DBG_MODE_PERF 0
#define VHA_WM_DBG_MODE_BAND 1
#define WM_DBG_MODE_ON(type) \
	(wm_dbg_ctrl[VHA_WM_DBG_MODE_##type])

static uint32_t wm_dbg_ctrl[2] = { 0, 0 };
module_param_array(wm_dbg_ctrl, uint, NULL, 0444);
MODULE_PARM_DESC(wm_dbg_ctrl,
	"WM DEBUG CONTROL: switch for PERF and BAND: 0=disable 1=enable");

static uint32_t slc_hash_mode;
module_param(slc_hash_mode, uint, 0444);
MODULE_PARM_DESC(slc_hash_mode,
	"SLC_CTRL_HASH_MODE: Address decoding for SLC. 0-none; 1-pvr_v3; 2-linear; 3-in_page. See TRM");

#ifdef VHA_SCF
static uint32_t sys_ram_correction_threshold = 0;
module_param(sys_ram_correction_threshold, uint, 0444);
MODULE_PARM_DESC(sys_ram_correction_threshold,
	"Threshold for system level ram correction");

static uint32_t core_host_ram_correction_threshold = 0;
module_param(core_host_ram_correction_threshold, uint, 0444);
MODULE_PARM_DESC(core_host_ram_correction_threshold,
	"Threshold for host core level ram correction");

static uint32_t core_wm_ram_correction_threshold = 0;
module_param(core_wm_ram_correction_threshold, uint, 0444);
MODULE_PARM_DESC(core_wm_ram_correction_threshold,
	"Threshold for wm core level ram correction");
#endif

#define CONF_WRITES_WAIT_TIMEOUT_MS 20

#define SCHED_SEQ_CORES_MASK   0xff
#define SCHED_SEQ_CORES_SHIFT  0
#define SCHED_SEQ_WM_ID_MASK   0x7
#define SCHED_SEQ_WM_ID_SHIFT  8
#define SCHED_SEQ_GET_CORES(idx) \
	((vha->scheduling_sequence[idx] >> SCHED_SEQ_CORES_SHIFT) & SCHED_SEQ_CORES_MASK)
#define SCHED_SEQ_GET_WM(idx) \
	((vha->scheduling_sequence[idx] >> SCHED_SEQ_WM_ID_SHIFT) & SCHED_SEQ_WM_ID_MASK)
/*
 * scheduling_sequence can be used to force execution on specific WMs/cores.
 * It encodes the WM id (byte1) and the core mask (byte0), for example:
 * scheduling_sequence=0x001 -> WM0/core0
 * scheduling_sequence=0x204 -> WM2/core2
 * scheduling_sequence=0x520 -> WM5/core5
 * scheduling_sequence=0x310 -> WM3/core4
 */
static int32_t scheduling_sequence_len;
static uint32_t scheduling_sequence[VHA_MC_SCHED_SEQ_LEN_MAX] = { 0 };
module_param_array(scheduling_sequence, uint, &scheduling_sequence_len, 0444);
MODULE_PARM_DESC(scheduling_sequence, "multicore scheduling sequence");

#define MAX_STALLING_DATA_ENTRIES 2
static int32_t stalling_data_len;
static uint32_t stalling[MAX_STALLING_DATA_ENTRIES] = { 0 };
module_param_array(stalling, uint, &stalling_data_len, 0444);
MODULE_PARM_DESC(stalling, "stalling data");

static bool test_direct_events;
module_param(test_direct_events, bool, 0444);
MODULE_PARM_DESC(test_direct_events,
		"When set CORE&INTERCONNECT events are directly sent to host, to WM, otherwise");

static int32_t pri_windows_list_len;
static uint32_t pri_windows_list[VHA_MAX_PRIORITIES] = { 0 };
module_param_array(pri_windows_list, uint, &pri_windows_list_len, 0444);
MODULE_PARM_DESC(pri_windows_list,
		"priority window size list starting from lowest; all 0s mean no starvation avoidance");

/* Priority scheduler local data. */
struct vha_sched_local_data {
	void *rand_gen_handle;
};
/* Priority window array. */
/* NOTE: Setting all to 0 implies strict priority scheduling (no starvation avoidance). */
static uint32_t pri_windows[VHA_MAX_PRIORITIES] = {0};

/* Parity related defines. */
#ifdef VHA_SCF
	#define VHA_PARITY_READ_COUNT_MAX  4
#endif

struct vha_errcode {
	uint8_t e;
	const char* s;
	enum vha_reset_type reset_type;
	uint64_t rsp_err;
};

/* SYS event errors. */
#define ERR_SYS_EVENT_DESC(b) VHA_SYS_EVENT_TYPE(b), __stringify(b)
static const struct vha_biterr sys_err_bits[] = {
	{-EIO,       ERR_SYS_EVENT_DESC(AXI_ERROR       ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SYS_AXI_ERROR)},
	{-EFAULT,    ERR_SYS_EVENT_DESC(MMU_PAGE_FAULT  ), VHA_RESET_TYPE_MMU,  VHA_RSP_ERROR(HW_SYS_MMU_PAGE_FAULT)},
	{-ETIMEDOUT, ERR_SYS_EVENT_DESC(SYS_MEM_WDT     ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SYS_SYS_MEM_WDT)},
#ifdef VHA_SCF
	/*
	 * Unfortunately, hw guys did not specify the way to identify the failed
	 * WM. Waiting for them to fix this. */
	{-EIO,       ERR_SYS_EVENT_DESC(AXI_MEMORY_PARITY_ERROR), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SYS_AXI_MEMORY_PARITY_ERROR)},
	{-EIO,       ERR_SYS_EVENT_DESC(MMU_PARITY_ERROR       ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SYS_MMU_PARITY_ERROR)}, /*VHA_RESET_TYPE_MMU},*/
	{-EIO,       ERR_SYS_EVENT_DESC(RAM_CORRECTION         ), VHA_RESET_TYPE_NONE, VHA_RSP_ERROR(HW_SYS_RAM_CORRECTION)},
	{-EIO,       ERR_SYS_EVENT_DESC(RAM_DETECTION          ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SYS_RAM_DETECTION)},
	{-EIO,       ERR_SYS_EVENT_DESC(LSYNC_INV_REQ          ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SYS_LSYNC_INV_REQ)},
	{-EIO,       ERR_SYS_EVENT_DESC(LOGIC_ERROR            ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SYS_LOGIC_ERROR)},
	{-EIO,       VHA_REG_PARITY_ERROR_EN, __stringify(PARITY_ERROR), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(SW_SYS_EVNT_PARITY_ERROR)},
#endif
	{0}
};

/* WM event errors. */
#define ERR_WM_EVENT_DESC(b) VHA_WM_EVENT_TYPE(b), __stringify(b)
static const struct vha_biterr wm_err_bits[] = {
	{-ETIMEDOUT, ERR_WM_EVENT_DESC(WM_WL_WDT     ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_EVNT_WM_WL_WDT)},
	{-ETIMEDOUT, ERR_WM_EVENT_DESC(WM_WL_IDLE_WDT), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_EVNT_WM_WL_IDLE_WDT)},
	{-ETIMEDOUT, ERR_WM_EVENT_DESC(WM_SOCIF_WDT  ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_EVNT_WM_SOCIF_WDT)},
	{-EFAULT,    ERR_WM_EVENT_DESC(LOGIC_FAULT   ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_EVNT_LOGIC_FAULT)},
#ifdef VHA_SCF
	{-EIO,       VHA_REG_PARITY_ERROR_EN, __stringify(PARITY_ERROR), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(SW_EVNT_WM_PARITY_ERROR)},
#endif
	{0}
};

/* WM response FIFO status error codes. */
#define ERR_WM_RSP_STATUS_DESC(v)  VHA_WM_RESPONSE_ERROR_CODE(v), __stringify(v)
static const struct vha_errcode wm_rsp_err_codes[] = {
	{ERR_WM_RSP_STATUS_DESC(CORE_IRQ_BEFORE_KICK   ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_IRQ_BEFORE_KICK)},
	{ERR_WM_RSP_STATUS_DESC(INDIRECT_MASK_SET_ERROR), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_INDIRECT_MASK_SET_ERROR)},
	{ERR_WM_RSP_STATUS_DESC(KICK_CORE_ACCESS_ERROR ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_KICK_CORE_ACCESS_ERROR)},
	{ERR_WM_RSP_STATUS_DESC(CNN_CONTROL_START_HIGH ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CNN_CONTROL_START_HIGH)},
	{ERR_WM_RSP_STATUS_DESC(CNN_STATUS_ERROR       ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CNN_STATUS_ERROR)},
	{ERR_WM_RSP_STATUS_DESC(INT_CORE_ACCESS_ERROR  ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_INT_CORE_ACCESS_ERROR)},
	{ERR_WM_RSP_STATUS_DESC(CORE_EVENT_ERROR       ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_EVENT_ERROR)},
	{ERR_WM_RSP_STATUS_DESC(CORE_EVENT_NOT_CLEARED ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_EVENT_NOT_CLEARED)},
	{ERR_WM_RSP_STATUS_DESC(CORE_EVENT_IRQ_HIGH    ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_EVENT_IRQ_HIGH)},
	{ERR_WM_RSP_STATUS_DESC(INTERCONNECT_ERROR     ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_INTERCONNECT_ERROR)},
};

/* CNN core status errors. */
#define ERR_CORE_STATUS_DESC(b) VHA_CORE_STATUS(b), __stringify(b)
static const struct vha_biterr core_err_bits[] = {
	{-EIO,       ERR_CORE_STATUS_DESC(LOGIC_ERROR    ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_LOGIC_ERROR)},
	{-EIO,       ERR_CORE_STATUS_DESC(RAM_CORRECTION ), VHA_RESET_TYPE_NONE, VHA_RSP_ERROR(HW_RAM_CORRECTION)},
	{-EIO,       ERR_CORE_STATUS_DESC(RAM_DETECTION  ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_RAM_DETECTION)},
	{-EIO,       ERR_CORE_STATUS_DESC(CORE_SYNC_ERROR), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_CORE_SYNC_ERROR)},
	{-ETIMEDOUT, ERR_CORE_STATUS_DESC(CORE_WDT       ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_WDT)},
	{-ETIMEDOUT, ERR_CORE_STATUS_DESC(CORE_MEM_WDT   ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_MEM_WDT)},
	{-EIO,       ERR_CORE_STATUS_DESC(CNN_ERROR      ), VHA_RESET_TYPE_WM,   VHA_RSP_ERROR(HW_CORE_CNN_ERROR)},
	{0}
};

/* Interconnect status errors. */
#define ERR_IC_STATUS_DESC(b) VHA_IC_STATUS(b), __stringify(b)
static const struct vha_biterr ic_err_bits[] = {
	{-EIO,       ERR_IC_STATUS_DESC(LOCKSTEP_ERROR         ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_LOCKSTEP_ERROR)},
	{-EIO,       ERR_IC_STATUS_DESC(LOGIC_ERROR            ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_IC_LOGIC_ERROR)},
	{-EIO,       ERR_IC_STATUS_DESC(SOCIF_READ_MISMATCH    ), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SOCIF_READ_MISMATCH)},
	{-EIO,       ERR_IC_STATUS_DESC(SOCIF_READ_UNRESPONSIVE), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(HW_SOCIF_READ_UNRESPONSIVE)},
#ifdef VHA_SCF
	{-EIO,       VHA_REG_PARITY_ERROR_EN, __stringify(PARITY_ERROR), VHA_RESET_TYPE_FULL, VHA_RSP_ERROR(SW_IC_PARITY_ERROR)},
#endif
	{0}
};

bool vha_dev_dbg_params_check(struct vha_dev *vha)
{
	if (vha->scheduling_sequence_len > 0) {
		uint32_t i;
		for (i = 0; i < vha->scheduling_sequence_len; i++) {
			uint8_t wm_id = SCHED_SEQ_GET_WM(i);
			uint8_t core_mask = SCHED_SEQ_GET_CORES(i);
			if ((wm_id >= vha->hw_props.num_cnn_core_devs) ||
				(~vha->full_core_mask & core_mask)) {
				dev_info(vha->dev,
						"%u/0x%02x -> %u/0x%02x (0x%02x)",
						wm_id, core_mask,
						vha->hw_props.num_cnn_core_devs, vha->full_core_mask,
						(~vha->full_core_mask & core_mask));
				dev_err(vha->dev,
						"'scheduling_sequence' contains cores that do not exist on this h/w.\n");
				return false;
			}
		}
	}
	return true;
}

bool vha_dev_dbg_params_init(struct vha_dev *vha)
{
	vha->scheduling_sequence_len = scheduling_sequence_len;
	memcpy(vha->scheduling_sequence,
		scheduling_sequence, sizeof(scheduling_sequence));
	vha->scheduling_counter = 0;

	vha->stalling_sysbus_host_stall_ratio = stalling[0];
	vha->stalling_membus_sys_stall_ratio  = stalling[1];

	return vha_dev_dbg_params_check(vha);
}

int vha_dev_scheduler_init(struct vha_dev *vha)
{
	int ret;
	uint32_t seed, i;
	bool use_default_pri_windows = true;

	vha->hw_sched_status.num_cores_free = vha->hw_props.num_cnn_core_devs;
	vha->hw_sched_status.num_wms_free   = vha->hw_props.num_cnn_core_devs;
	vha->hw_sched_status.free_core_mask =
				VHA_GET_CORE_MASK(vha->hw_props.num_cnn_core_devs);
	vha->hw_sched_status.free_wm_mask =
				VHA_GET_WM_MASK(vha->hw_props.num_cnn_core_devs);
	vha->full_core_mask = vha->hw_sched_status.free_core_mask;
	vha->wm_core_assignment = (uint64_t)(
			VHA_CR_CORE_ASSIGNMENT_CORE_7_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_6_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_5_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_4_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_3_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_2_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_1_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_UNALLOCATED);
	vha->active_core_mask = 0;
	vha->apm_core_mask = 0;

	/* Allocate priority scheduler data. */
	vha->hw_sched_status.sched_data = kzalloc(sizeof(struct vha_sched_local_data), GFP_KERNEL);
	if (vha->hw_sched_status.sched_data == NULL) {
		dev_err(vha->dev, "%s: failed allocating scheduler data\n", __func__);
		return -ENOMEM;
	}
	/* Initialise random number generator for priority scheduling. */
	get_random_bytes(&seed, sizeof(seed));
	ret = vha_mt19937_init(seed, &vha->hw_sched_status.sched_data->rand_gen_handle);
	if (ret != 0) {
		dev_err(vha->dev, "%s: failed initialising random generator\n", __func__);
		kfree(vha->hw_sched_status.sched_data);
		return ret;
	}
	/* Attempt to set priority windows passed on to kernel module. */
	if (pri_windows_list_len == VHA_MAX_PRIORITIES) {
		uint32_t num_zeros = 0;
		for (i = 0; i < VHA_MAX_PRIORITIES; i++)
			if (pri_windows_list[i] == 0)
				num_zeros++;
		if (num_zeros < VHA_MAX_PRIORITIES)
		{
			dev_warn(vha->dev, "%s: some priority windows are set to 0; "
					"using default settings\n", __func__);
		} else {
			memcpy(pri_windows, pri_windows_list, sizeof(pri_windows));
			use_default_pri_windows = false;
		}
	} else if (pri_windows_list_len > 0) {
		dev_warn(vha->dev, "%s: too few priority windows provided (needed %u); "
				"using default settings\n", __func__, VHA_MAX_PRIORITIES);
	}
	/* Calculate priority windows. */
	if (use_default_pri_windows) {
#define BASE_PRI_WINDOW_WIDTH 30
		for (i = 0; i < VHA_MAX_PRIORITIES; i++)
			pri_windows[i] = BASE_PRI_WINDOW_WIDTH + (i * 2 * BASE_PRI_WINDOW_WIDTH);
#undef BASE_PRI_WINDOW_WIDTH
	}

	return 0;
}

int vha_dev_scheduler_deinit(struct vha_dev *vha)
{
	int ret;

	if (vha->hw_sched_status.sched_data == NULL) {
		dev_warn(vha->dev, "%s: scheduler not initialised\n", __func__);
		return 0;
	}
	ret = vha_mt19937_deinit(vha->hw_sched_status.sched_data->rand_gen_handle);
	if (ret != 0) {
		dev_err(vha->dev, "%s: failed deinitialising random generator\n", __func__);
	}
	kfree(vha->hw_sched_status.sched_data);

	return ret;
}

void vha_dev_mh_setup(struct vha_dev *vha, int ctx_id, struct vha_mh_config_regs *regs)
{
	uint64_t val64 = 0;

	regs->cnn_preload_control |= VHA_CR_SETBITS(OS0_CNN_PRELOAD_CONTROL,
								CBUF_N_REQS, VHA_CR_CNN_PRELOAD_CTRL_N_64);
	/* Setup preload for MMM */
	regs->cnn_preload_control |= VHA_CR_SETBITS(OS0_CNN_PRELOAD_CONTROL,
								MMM_RD_N_REQS, VHA_CR_CNN_PRELOAD_CTRL_N_256);
	regs->cnn_preload_control |= VHA_CR_SETBITS(OS0_CNN_PRELOAD_CONTROL,
								MMM_WR_N_REQS, VHA_CR_CNN_PRELOAD_CTRL_N_256);

	img_pdump_printf("-- MH setup:%d\n", ctx_id);
	IOWRITE64_CR_PDUMP(regs->cnn_preload_control, OS0_CNN_PRELOAD_CONTROL);

	regs->req_ctxt_override = VHA_SET_FIELD_SIMPLE_VAL(REQ_CTXT_OVERRIDE, OVERRIDE_OS0, EN);
	IOWRITE64_CR_PDUMP(regs->req_ctxt_override, REQ_CTXT_OVERRIDE);

	if (slc_hash_mode) {
		regs->slc_control = VHA_CR_SETBITS(SLC_CTRL, HASH_MODE,
				slc_hash_mode);
		IOWRITE64_CR_PDUMP(val64, SLC_CTRL);
	}
}

static int set_power_event(struct vha_dev *vha, uint64_t event)
{
	int ret=0;
	uint64_t val64;
	/* Clear any pending power events */
	IOWRITE64_CR_PDUMP(0, POWER_EVENT);
	/* Confirm no power events are pending */
	ret = IOPOLL64_CR_PDUMP(0, 100, 1000,
			((uint64_t)VHA_CR_BITMASK(SYS_EVENT_STATUS, POWER_COMPLETE) |
			 (uint64_t)VHA_CR_BITMASK(SYS_EVENT_STATUS, POWER_ABORT)),
			SYS_EVENT_STATUS);
	if(ret)
		return ret;
	/* Trigger power event */
	IOWRITE64_CR_PDUMP(event, POWER_EVENT);
	/* Wait for power complete */
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_STATUS, POWER_COMPLETE, EN);
	ret = IOPOLL64_CR_PDUMP(val64, 100, 1000,
			(uint64_t)VHA_CR_BITMASK(SYS_EVENT_STATUS, POWER_COMPLETE),
			SYS_EVENT_STATUS);
	if(ret)
		return ret;
	/* Switch off power event */
	IOWRITE64_CR_PDUMP(0, POWER_EVENT);
	/* Clear power complete event status */
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_CLEAR, POWER_COMPLETE, EN);
	IOWRITE64_CR_PDUMP(val64, SYS_EVENT_CLEAR);
	/* Confirm power complete is cleared */
	ret = IOPOLL64_CR_PDUMP(0, 100, 1000,
			(uint64_t)VHA_CR_BITMASK(SYS_EVENT_STATUS, POWER_COMPLETE),
			SYS_EVENT_STATUS);
	return ret;
}

#ifdef VHA_SCF
static void ecc_correction_setup(struct vha_dev *vha)
{
	uint64_t val64;

	val64 = VHA_CR_SETBITS(SYS_EVENT_THRESHOLD, RAM_CORRECTION,
		sys_ram_correction_threshold);
	IOWRITE64_CR_PDUMP(val64, SYS_EVENT_THRESHOLD);

	val64 = VHA_CR_SETBITS(CORE_EVENT_WM_THRESHOLD, RAM_CORRECTION,
		core_wm_ram_correction_threshold);
	IOWRITE64_CR_PDUMP(val64, CORE_EVENT_WM_THRESHOLD);

	val64 = VHA_CR_SETBITS(CORE_EVENT_HOST_THRESHOLD, RAM_CORRECTION,
		core_host_ram_correction_threshold);
	IOWRITE64_CR_PDUMP(val64, CORE_EVENT_HOST_THRESHOLD);
}
#endif

static int vha_dev_prepare_cores(struct vha_dev *vha, uint8_t core_mask)
{
	/* Enabling selected cores on the platform
	 * Note: don't touch TLC, is an always ON domain */
	uint64_t val64 = VHA_CR_SETBITS(POWER_EVENT, DOMAIN,
				(core_mask << 1)) |
				VHA_SET_FIELD_SIMPLE_VAL(POWER_EVENT, TYPE, POWER_UP) |
				VHA_SET_FIELD_SIMPLE_VAL(POWER_EVENT, REQ, EN);

	img_pdump_printf("-- Trigger POWER UP domain event\n");
	return set_power_event(vha, val64);
}

static int vha_dev_flush_cores(struct vha_dev *vha, uint8_t core_mask)
{
	uint64_t val64;

	img_pdump_printf("-- Deselect any cores\n");
	IOWRITE64_CR_PDUMP(0, CORE_CTRL_INDIRECT);
	/* Disabling selected cores on the platform
	 * Note: don't touch TLC, is an always ON domain */
	val64 = VHA_CR_SETBITS(POWER_EVENT, DOMAIN, (core_mask << 1)) |
			VHA_SET_FIELD_SIMPLE_VAL(POWER_EVENT, TYPE, POWER_DOWN) |
			VHA_SET_FIELD_SIMPLE_VAL(POWER_EVENT, REQ, EN);

	img_pdump_printf("-- Trigger POWER DOWN domain event\n");
	return set_power_event(vha, val64);
}

void vha_dev_setup(struct vha_dev *vha)
{
	uint64_t val64;

	img_pdump_printf("-- MMU set virtual address range0:%#llx-%#llx\n",
			IMG_MEM_VA_HEAP1_BASE, IMG_MEM_VA_HEAP1_SIZE);
	val64 = (uint64_t)vha->mmu_page_size <<
			VHA_CR_MMU_PAGE_SIZE_RANGE_ONE_PAGE_SIZE_SHIFT;
	val64 |= VHA_CR_ALIGN_SETBITS(MMU_PAGE_SIZE_RANGE_ONE,
		BASE_ADDR, IMG_MEM_VA_HEAP1_BASE);
	val64 |= VHA_CR_ALIGN_SETBITS(MMU_PAGE_SIZE_RANGE_ONE,
		END_ADDR, (IMG_MEM_VA_HEAP1_BASE + IMG_MEM_VA_HEAP1_SIZE));
	IOWRITE64_PDUMP(val64, VHA_CR_MMU_PAGE_SIZE_RANGE_ONE);

	img_pdump_printf("-- MMU set virtual address range1:%#llx-%#llx\n",
			IMG_MEM_VA_HEAP2_BASE, IMG_MEM_VA_HEAP2_SIZE);
	val64 = (uint64_t)vha->mmu_page_size <<
			VHA_CR_MMU_PAGE_SIZE_RANGE_TWO_PAGE_SIZE_SHIFT ;
	val64 |= VHA_CR_ALIGN_SETBITS(MMU_PAGE_SIZE_RANGE_TWO,
		BASE_ADDR, IMG_MEM_VA_HEAP2_BASE);
	val64 |= VHA_CR_ALIGN_SETBITS(MMU_PAGE_SIZE_RANGE_TWO,
		END_ADDR, (IMG_MEM_VA_HEAP2_BASE + IMG_MEM_VA_HEAP2_SIZE));
	IOWRITE64_PDUMP(val64, VHA_CR_MMU_PAGE_SIZE_RANGE_TWO);

#ifdef VHA_SCF
	ecc_correction_setup(vha);
#endif
}

void vha_dev_wait(struct vha_dev *vha)
{
	/* Nothing to do */
}

static void vha_dev_disable_events(struct vha_dev *vha, uint8_t core_mask, bool sys_release)
{
	uint8_t id;

	if (sys_release) {
		img_pdump_printf("-- Disable SYS events\n");
		IOWRITE64_CR_PDUMP(0, SYS_EVENT_ENABLE);

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			img_pdump_printf("-- Select WM%u\n", id);
			VHA_LOCK_WM();
			VHA_SELECT_WM(id);
			img_pdump_printf("-- Clear WM%u events\n", id);
			IOWRITE64_CR_PDUMP(VHA_WM_EVENTS_DEFAULT, WM_EVENT_CLEAR);
			img_pdump_printf("-- Disable WM%u events\n", id);
			IOWRITE64_CR_PDUMP(0, WM_EVENT_ENABLE);
			VHA_UNLOCK_WM();
		}
	}

	img_pdump_printf("-- Select cores\n");
	IOWRITE64_CR_PDUMP((uint64_t)core_mask, CORE_CTRL_INDIRECT);

	if (test_direct_events) {
		img_pdump_printf("-- Disable CORE events to HOST\n");
		IOWRITE64_CR_PDUMP(0, CORE_EVENT_HOST_ENABLE);

		img_pdump_printf("-- Disable INTERCONNECT events to HOST\n");
		IOWRITE64_CR_PDUMP(0, INTERCONNECT_EVENT_HOST_ENABLE);
	} else {
		img_pdump_printf("-- Disable CORE events to WM\n");
		IOWRITE64_CR_PDUMP(0, CORE_EVENT_WM_ENABLE);

		img_pdump_printf("-- Disable INTERCONNECT events to WM\n");
		IOWRITE64_CR_PDUMP(0, INTERCONNECT_EVENT_WM_ENABLE);
	}
}

static void vha_dev_ready(struct vha_dev *vha, uint8_t core_mask, bool sys_setup)
{
	uint8_t id;

	if (sys_setup) {
		img_pdump_printf("-- Enable SYS events\n");
		IOWRITE64_CR_PDUMP(VHA_SYS_EVENTS_DEFAULT, SYS_EVENT_ENABLE);
		img_pdump_printf("-- Clear SYS events\n");
		IOWRITE64_CR_PDUMP(VHA_SYS_EVENTS_DEFAULT, SYS_EVENT_CLEAR);

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			img_pdump_printf("-- Select WM%u\n", id);
			VHA_LOCK_WM();
			VHA_SELECT_WM(id);
			img_pdump_printf("-- Enable WM%u events\n", id);
			IOWRITE64_CR_PDUMP(VHA_WM_EVENTS_DEFAULT, WM_EVENT_ENABLE);
			img_pdump_printf("-- Clear WM%u events\n", id);
			IOWRITE64_CR_PDUMP(VHA_WM_EVENTS_DEFAULT, WM_EVENT_CLEAR);
			VHA_UNLOCK_WM();
		}
	}

	img_pdump_printf("-- Select cores\n");
	IOWRITE64_CR_PDUMP((uint64_t)core_mask, CORE_CTRL_INDIRECT);

	if (test_direct_events) {
		img_pdump_printf("-- Enable CORE events to HOST\n");
		IOWRITE64_CR_PDUMP(VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_HOST_ENABLE);
		img_pdump_printf("-- Clear CORE events on HOST\n");
		IOWRITE64_CR_PDUMP(VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_HOST_CLEAR);

		img_pdump_printf("-- Enable INTERCONNECT events to HOST\n");
		IOWRITE64_CR_PDUMP(VHA_IC_EVENTS_DEFAULT, INTERCONNECT_EVENT_HOST_ENABLE);
		img_pdump_printf("-- Clear INTERCONNECT events on HOST\n");
		IOWRITE64_CR_PDUMP(VHA_IC_EVENTS_DEFAULT, INTERCONNECT_EVENT_HOST_CLEAR);
	} else {
		img_pdump_printf("-- Enable CORE events to WM\n");
		IOWRITE64_CR_PDUMP(VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_WM_ENABLE);
		img_pdump_printf("-- Clear CORE events on WM\n");
		IOWRITE64_CR_PDUMP(VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_WM_CLEAR);

		img_pdump_printf("-- Enable INTERCONNECT events to WM\n");
		IOWRITE64_CR_PDUMP(VHA_IC_EVENTS_DEFAULT, INTERCONNECT_EVENT_WM_ENABLE);
		img_pdump_printf("-- Clear INTERCONNECT events on WM\n");
		IOWRITE64_CR_PDUMP(VHA_IC_EVENTS_DEFAULT, INTERCONNECT_EVENT_WM_CLEAR);
	}
}

/* Global reset */
static int vha_dev_reset(struct vha_dev *vha, uint8_t core_mask, bool sys_reset)
{
	uint64_t val64 = 0;
	uint8_t mask = 0;
	uint8_t id;
	int ret = 0;

	WARN_ON(!mutex_is_locked(&vha->lock));

	dev_dbg(vha->dev, "%s core mask:%#x\n", __func__, core_mask);

	img_pdump_printf("-- Top level RESET sequence BEGIN\n");
	/* Perform reset procedure */

	if (sys_reset) {
		/* First reset all WMs with cores assigned. */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			struct vha_hw_sched_info sched_info = {0};
			sched_info.core_mask = vha_wm_get_cores(vha, id);
			if (sched_info.core_mask) {
				sched_info.wm_id = id;
				vha_wm_reset(vha, &sched_info);
				core_mask &= ~sched_info.core_mask;
			}
		}
	}

	/* Core reset procedure. */
	img_pdump_printf("-- Resetting cores\n");

	/* Proceed core by core, unassigned cores only */
	for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
		if (!(core_mask & VHA_CORE_ID_TO_MASK(id)))
			continue;

		/* Reset Assertion */

		/* 1.  Select current core. */
		img_pdump_printf("-- Select core%u\n", id);
		mask = VHA_CORE_ID_TO_MASK(id);
		IOWRITE64_CR_PDUMP(mask, CORE_CTRL_INDIRECT);
		/* 3. Disable page fault interrupts for core while resetting. */
		img_pdump_printf("-- Disable page fault interrupts for core%u\n", id);
		val64 = IOREAD64_CR_REGIO(SYS_EVENT_ENABLE);
		val64 &= ~(VHA_CR_SETBITS(SYS_EVENT_ENABLE, MMU_PAGE_FAULT, mask));
		IOWRITE64_CR_PDUMP(val64, SYS_EVENT_ENABLE);
		/* 4. Force global clocks to ON on current core (others set to AUT0). */
		img_pdump_printf("-- Force global clocks ON for core%u (others set to AUTO)\n", id);
		val64 = VHA_SYS_CLOCK_MODE(INTERCONNECT, ON) |
				VHA_SYS_CLOCK_MODE_MULTI(CORE, ON, mask) |
				VHA_SYS_CLOCK_MODE_MULTI(CORE, AUTO, (uint8_t)~mask) |
				VHA_SYS_CLOCK_MODE_MULTI(NOC, AUTO, ~0) |
				VHA_SYS_CLOCK_MODE_MULTI(WM, AUTO, ~0) |
				VHA_SYS_CLOCK_MODE(AXI, AUTO) |
				VHA_SYS_CLOCK_MODE(SLC, AUTO) |
				VHA_SYS_CLOCK_MODE(LSYNC, AUTO) |
				VHA_SYS_CLOCK_MODE(SOCM, AUTO) |
				VHA_SYS_CLOCK_MODE(REGBANK, AUTO);
		IOWRITE64_CR_PDUMP(val64, SYS_CLK_CTRL0);
		/* 5. Set all core level clocks to AUTO. */
		img_pdump_printf("-- Set all core%u level clocks to AUTO\n", id);
		val64 = VHA_MAIN_CLOCKS_DEFAULT(AUTO);
		IOWRITE64_CR_PDUMP(val64, CLK_CTRL0);
		/* 6. Move core into soft reset. */
		img_pdump_printf("-- Perform soft reset on core%u\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_SOFT_RESET, CORE_RESET, EN);
		IOWRITE64_CR_PDUMP(val64, CORE_SOFT_RESET);
		/*    Dummy read to avoid race conditions in the hw. */
		val64 = IOREAD64_CR_PDUMP(CORE_SOFT_RESET);
		/*    Clear reset. */
		IOWRITE64_CR_PDUMP(0, CORE_SOFT_RESET);
		/* 7. Wait until core memory bus reset has completed. */
		img_pdump_printf("-- Wait until core%u memory bus reset has completed\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_STATUS, MEMBUS_RESET_DONE, EN);
		ret = IOPOLL64_CR_PDUMP(val64, 100, 1000,
				(uint64_t)VHA_CR_BITMASK(CORE_EVENT_HOST_STATUS, MEMBUS_RESET_DONE),
				CORE_EVENT_HOST_STATUS);
		if(ret)
			return ret;
		/* 8. Clear core memory bus reset interrupt. */
		img_pdump_printf("-- Clear core%u memory bus reset interrupt\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_CLEAR, MEMBUS_RESET_DONE, EN);
		IOWRITE64_CR_PDUMP(val64, CORE_EVENT_HOST_CLEAR);
		/* 9. Clear the core indirect register. */
		img_pdump_printf("-- Deselect core%u\n", id);
		IOWRITE64_CR_PDUMP(0, CORE_CTRL_INDIRECT);
		/* 10. Ensure no resets are pending. */
		img_pdump_printf("-- Ensure no resets are pending\n");
		IOWRITE64_CR_PDUMP(0, SYS_RESET_CTRL);
		/* 11. Move current core into full reset state. */
		img_pdump_printf("-- Move core%u into full reset state\n", id);
		val64 = VHA_CR_SETBITS(SYS_RESET_CTRL, CORE, mask);
		IOWRITE64_CR_PDUMP(val64, SYS_RESET_CTRL);
		/* 12. Dummy read to avoid race conditions in the hw. */
		val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);

		/* Reset Deassertion */

		/* 1. Move current core out of reset state. */
		img_pdump_printf("-- Move core%u out of reset state\n", id);
		val64 &= ~(VHA_CR_SETBITS(SYS_RESET_CTRL, CORE, mask));
		IOWRITE64_CR_PDUMP(val64, SYS_RESET_CTRL);
		/*    Dummy read to avoid race conditions in the hw. */
		val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);
		/* 2. Select current core again. */
		img_pdump_printf("-- Select core%u again\n", id);
		IOWRITE64_CR_PDUMP(mask, CORE_CTRL_INDIRECT);
		/* 5. Force core clocks to ON for everything. */
		img_pdump_printf("-- Force core clocks ON for everything\n");
		val64 = VHA_MAIN_CLOCKS_DEFAULT(ON);
		IOWRITE64_CR_PDUMP(val64, CLK_CTRL0);
		/* 6. Perform core level RAM initialisation. */
		img_pdump_printf("-- Perform core%u level RAM initialisation\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(FUSA_CONTROL, ECC_INIT_KICK, EN);
		IOWRITE64_CR_PDUMP(val64, FUSA_CONTROL);
		/* 7. Perform LOCM scrubbing. */
		img_pdump_printf("-- Perform core%u LOCM scrubbing\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(LOCM_SCRUB_CTRL, KICK, EN);
		IOWRITE64_CR_PDUMP(val64, LOCM_SCRUB_CTRL);
		/* 8. Wait until the RAM initialisation sequence has completed. */
		img_pdump_printf("-- Wait until the RAM initialisation sequence has completed\n");
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_STATUS, RAM_INIT_DONE, EN);
		ret = IOPOLL64_CR_PDUMP(val64, 100, 1000,
				(uint64_t)VHA_CR_BITMASK(CORE_EVENT_HOST_STATUS, RAM_INIT_DONE),
				CORE_EVENT_HOST_STATUS);
		if(ret)
			return ret;
		/* 9. Clear core RAM reset interrupt. */
		img_pdump_printf("-- Clear core%u RAM reset interrupt\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_CLEAR, RAM_INIT_DONE, EN);
		IOWRITE64_CR_PDUMP(val64, CORE_EVENT_HOST_CLEAR);
		/*    Confirm that 'RAM_INIT_DONE' field is cleared. */
		img_pdump_printf("-- Confirm that core%u RAM reset interrupt is cleared\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_STATUS, RAM_INIT_DONE, EN);
		ret = IOPOLL64_CR_PDUMP(0ULL, 10, 100, val64, CORE_EVENT_HOST_STATUS);
		if(ret)
			return ret;
		/* 10. Wait until the LOCM scrubbing sequence has completed. */
		img_pdump_printf("-- Wait until the LOCM scrubbing sequence has completed.\n");
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_STATUS, LOCM_SCRUB_DONE, EN);
		ret = IOPOLL64_CR_PDUMP(val64, 100, 1000,
				(uint64_t)VHA_CR_BITMASK(CORE_EVENT_HOST_STATUS, LOCM_SCRUB_DONE),
				CORE_EVENT_HOST_STATUS);
		if(ret)
			return ret;
		/* 11. Deassert core LOCM scrubbing. */
		img_pdump_printf("-- Deassert core%u LOCM scrubbing\n", id);
		IOWRITE64_CR_PDUMP(0, LOCM_SCRUB_CTRL);
		/* 12. Clear core LOCM scrub interrupt. */
		img_pdump_printf("-- Clear core%u LOCM scrub interrupt\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_CLEAR, LOCM_SCRUB_DONE, EN);
		IOWRITE64_CR_PDUMP(val64, CORE_EVENT_HOST_CLEAR);
		/*     Confirm that 'LOCM_SCRUB_DONE' field is cleared. */
		img_pdump_printf("-- Confirm that core%u LOCM scrub interrupt is cleared\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_STATUS, LOCM_SCRUB_DONE, EN);
		ret = IOPOLL64_CR_PDUMP(0ULL, 10, 100, val64, CORE_EVENT_HOST_STATUS);
		if(ret)
			return ret;
		/* 13. Enable the interrupts from core to WM. */
		img_pdump_printf("-- Enable CORE events to WM\n");
		IOWRITE64_CR_PDUMP(VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_WM_ENABLE);
		/* 14. Clear all status from CORE_EVENT_WM (clears the RAM_INIT_DONE). */
		img_pdump_printf("-- Clear CORE events on WM\n");
		IOWRITE64_CR_PDUMP(VHA_CORE_EVENTS_DEFAULT |
				VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_WM_CLEAR, RAM_INIT_DONE, EN) |
				VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_WM_CLEAR, LOCM_SCRUB_DONE, EN) |
				VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_WM_CLEAR, MEMBUS_RESET_DONE, EN),
				CORE_EVENT_WM_CLEAR);
		/* 15. Enable the interrupts from interconnect to WM */
		img_pdump_printf("-- Enable INTERCONNECT events to WM\n");
		IOWRITE64_CR_PDUMP(VHA_IC_EVENTS_DEFAULT, INTERCONNECT_EVENT_WM_ENABLE);
		/* 16. Disable all interrupts from the CORE to the HOST */
		img_pdump_printf("-- Disable CORE events on host\n");
		IOWRITE64_CR_PDUMP(0, CORE_EVENT_HOST_ENABLE);
		/* 17. Set all core level clocks back to AUTO. */
		img_pdump_printf("-- Set all core%u level clocks back to AUTO\n", id);
		val64 = VHA_MAIN_CLOCKS_DEFAULT(AUTO);
		IOWRITE64_CR_PDUMP(val64, CLK_CTRL0);
		/* 18. Set core global clock back to AUTO. */
		img_pdump_printf("-- Set core%u global clock back to AUTO (others set to AUTO)\n", id);
		val64 = VHA_SYS_CLOCKS_DEFAULT(AUTO);
		IOWRITE64_CR_PDUMP(val64, SYS_CLK_CTRL0);

		/* Setup stalling if requested. */
		if (vha->stalling_membus_sys_stall_ratio != 0)
			IOWRITE64_CR_REGIO(vha->stalling_membus_sys_stall_ratio,
								NN_SYS2_MEMBUS_SYS_STALL_RATIO);
	}

	if (!sys_reset)
		return 0;

	dev_dbg(vha->dev, "%s handling system level reset\n", __func__);

	/* Move the rest of modules into reset state. */
	img_pdump_printf("-- Move other modules into reset state\n");
	val64 = VHA_SET_FIELD_SIMPLE_FULL(SYS_RESET_CTRL, WM) |
					VHA_SET_FIELD_SIMPLE_VAL(SYS_RESET_CTRL, INTERCONNECT, EN) |
					VHA_SET_FIELD_SIMPLE_VAL(SYS_RESET_CTRL, SLC, EN) |
					VHA_SET_FIELD_SIMPLE_VAL(SYS_RESET_CTRL, MH, EN);
	IOWRITE64_CR_PDUMP(val64, SYS_RESET_CTRL);
	/* Dummy read to avoid race conditions in the hw */
	val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);
	/* Move the rest of modules out of reset state. */
	img_pdump_printf("-- Move other modules out of reset state\n");
	IOWRITE64_CR_PDUMP(0ULL, SYS_RESET_CTRL);
	/* Dummy read to avoid race conditions in the hw */
	val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);
	/* Wait until core memory bus reset has completed. */
	img_pdump_printf("-- Wait until sys memory bus reset has completed\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_STATUS, MEMBUS_RESET_DONE, EN);
	ret = IOPOLL64_CR_PDUMP(val64, 100, 1000,
			(uint64_t)VHA_CR_BITMASK(SYS_EVENT_STATUS, MEMBUS_RESET_DONE),
			SYS_EVENT_STATUS);
	if(ret)
		return ret;
	/* Clear memory bus reset status. */
	img_pdump_printf("-- Clear memory bus reset status\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_CLEAR, MEMBUS_RESET_DONE, EN);
	IOWRITE64_CR_PDUMP(val64, SYS_EVENT_CLEAR);
	/* Force all system level clocks on. */
	img_pdump_printf("-- Force all system level clocks ON (except core)\n");
	val64 = IOREAD64_CR_REGIO(SYS_CLK_CTRL0);
	val64 &= VHA_SYS_CLOCKS_CORE_FULL_MASK;
	val64 |= VHA_SYS_CLOCKS_RESET(ON);
	IOWRITE64_CR_PDUMP(val64, SYS_CLK_CTRL0);
	/* Initiate system RAM initialisation. */
	img_pdump_printf("-- Initiate system RAM initialisation\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_RAM_INIT, KICK, EN);
	IOWRITE64_CR_PDUMP(val64, SYS_RAM_INIT);
	/* Initiate system SOCM scrubbing. */
	img_pdump_printf("-- Initiate system SOCM scrubbing\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SOCM_SCRUB_CTRL, KICK, EN);
	IOWRITE64_CR_PDUMP(val64, SOCM_SCRUB_CTRL);
	/* Wait until the RAM initialisation sequence has completed. */
	img_pdump_printf("-- Wait until the RAM initialisation sequence has completed\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_STATUS, RAM_INIT_DONE, EN);
	ret = IOPOLL64_CR_PDUMP(val64, 100, 1000,
			(uint64_t)VHA_CR_BITMASK(SYS_EVENT_STATUS, RAM_INIT_DONE),
			SYS_EVENT_STATUS);
	if(ret)
		return ret;
	/* Wait until the SOCM scrubbing sequence has completed. */
	img_pdump_printf("-- Wait until the SOCM scrubbing sequence has completed\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_STATUS, SOCM_SCRUB_DONE, EN);
	ret = IOPOLL64_CR_PDUMP(val64, 100, 1000,
			(uint64_t)VHA_CR_BITMASK(SYS_EVENT_STATUS, SOCM_SCRUB_DONE),
			SYS_EVENT_STATUS);
	if(ret)
		return ret;
	/* Deassert system SOCM scrubbing */
	img_pdump_printf("-- Deassert system SOCM scrubbing\n");
	IOWRITE64_CR_PDUMP(0, SOCM_SCRUB_CTRL);
	img_pdump_printf("-- Clear sys events\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_CLEAR, RAM_INIT_DONE, EN) |
			VHA_SET_FIELD_SIMPLE_VAL(SYS_EVENT_CLEAR, SOCM_SCRUB_DONE, EN);
	IOWRITE64_CR_PDUMP(val64, SYS_EVENT_CLEAR);
	/* Set all clocks back to AUTO. */
	img_pdump_printf("-- Set all sys clocks back to AUTO\n");
	val64 = VHA_SYS_CLOCKS_DEFAULT(AUTO);
	IOWRITE64_CR_PDUMP(val64, SYS_CLK_CTRL0);
	/* Reset the system level register banks. */
	img_pdump_printf("-- Reset the system level register banks\n");
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_RESET_CTRL, REGBANK, EN);
	IOWRITE64_CR_PDUMP(val64, SYS_RESET_CTRL);
	/* Dummy read to avoid race conditions in the hw */
	val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);
	/* Clear reset */
	IOWRITE64_CR_PDUMP(0, SYS_RESET_CTRL);
	/* Dummy read to avoid race conditions in the hw */
	val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);
	img_pdump_printf("-- Top level RESET sequence END\n");

	vha->wm_core_assignment = (uint64_t)(
			VHA_CR_CORE_ASSIGNMENT_CORE_7_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_6_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_5_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_4_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_3_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_2_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_1_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_UNALLOCATED);

	/* Setup stalling if requested. */
	if (vha->stalling_sysbus_host_stall_ratio != 0)
		IOWRITE64_CR_REGIO(vha->stalling_sysbus_host_stall_ratio,
							NN_SYS2_SYSBUS_HOST_STALL_RATIO);

	return ret;
}

static void vha_dev_enable_clocks(struct vha_dev *vha, uint8_t core_mask)
{
	uint64_t sys_clks = 0;
	uint64_t main_clks = 0;

	/* Always AUTO gating when needed */
	sys_clks = VHA_SYS_CLOCKS_DEFAULT(AUTO);
	main_clks = VHA_MAIN_CLOCKS_DEFAULT(AUTO);
	/* Enable sys clocks */
	img_pdump_printf("-- Enable SYS clocks\n");
	IOWRITE64_CR_PDUMP(sys_clks, SYS_CLK_CTRL0);
	/* Dummy SYS clocks status read*/
	sys_clks = IOREAD64_CR_PDUMP(SYS_CLK_STATUS0);
	/* Enable main clocks on all cores */
	img_pdump_printf("-- Enable MAIN clocks on cores\n");
	IOWRITE64_CR_PDUMP((uint64_t)core_mask, CORE_CTRL_INDIRECT);
	IOWRITE64_CR_PDUMP(main_clks, CLK_CTRL0);
}

static int vha_dev_disable_clocks(struct vha_dev *vha, uint8_t core_mask, bool sys_release)
{
	uint64_t sys_clks = 0;
	uint8_t id;
	int ret = 0;

	if (sys_release) {
		/* Number of WMs equal to number of cores */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			VHA_LOCK_WM();
			VHA_SELECT_WM(id);
			/* Check WM is idle, handle parity */
			img_pdump_printf("-- Wait for WM%d IDLE state\n", id);
			ret = IOPOLL64_CR_PDUMP_PARITY(VHA_CR_WM_STATUS_STATE_IDLE, 100, 1000,
					(uint64_t)VHA_CR_WM_STATUS_STATE_MASK,
					WM_STATUS);
			VHA_UNLOCK_WM();
			if(ret) {
				struct vha_hw_sched_info sched_info = {
						.wm_id = id,
						.core_mask = 0
				};
				dev_err(vha->dev, "Performing wm%d reset due to HW error detection.", id);
				vha_wm_reset(vha, &sched_info);
				dev_err(vha->dev, "%s Waiting for WM%d IDLE state failed!",
						__func__, id);
				return ret;
			}
		}
	}
	vha_wm_release_cores(vha, core_mask, true);

	img_pdump_printf("-- Address cores\n");
	IOWRITE64_CR_PDUMP((uint64_t)core_mask, CORE_CTRL_INDIRECT);

	/* If auto gating was turned on, wait for clocks GATED state on all cores */
	img_pdump_printf("-- Wait for clocks IDLE state\n");
	ret = IOPOLL64_CR_PDUMP(0, 100, 1000,
			VHA_CR_CLK_STATUS0_MASKFULL,
			CLK_STATUS0);
	if(ret) {
		dev_err(vha->dev, "%s Waiting for clocks IDLE state failed!\n",
				__func__);
		return ret;
	}

	if (sys_release) {
		/* Wait for MMU,CCM,RDI,XBAR IDLE state */
		img_pdump_printf("-- Wait for memory bus interface IDLE state\n");
		ret = IOPOLL64_CR_PDUMP(VHA_CR_SLC_IDLE_MASKFULL, 1000, 1000,
				VHA_CR_SLC_IDLE_MASKFULL,
				SLC_IDLE);
		if(ret) {
			dev_err(vha->dev, "%s Waiting for memory bus interface IDLE state failed\n",
					__func__);
			return ret;
		}
	}
	/* Finally disable core clocks */
	img_pdump_printf("-- Disable MAIN clocks\n");
	IOWRITE64_CR_PDUMP(0, CLK_CTRL0); /* main */

	if (sys_release) {
		/* Finally disable sys clocks */
		img_pdump_printf("-- Disable SYS clocks (except REGBANK)\n");
		sys_clks = VHA_SYS_CLOCK_MODE(REGBANK, AUTO);
		IOWRITE64_CR_PDUMP(sys_clks, SYS_CLK_CTRL0); /* sys */
	}

	return ret;
}

void vha_update_utilization(struct vha_dev *vha)
{
	uint8_t i;
	uint64_t tmp;
	uint64_t core_total_proc_us = 0ULL;
	for (i = 0; i < vha->hw_props.num_cnn_core_devs; i++) {
		/* Calculate core utilization. */
		tmp = vha->stats.core_stats[i].total_proc_us;
		do_div(tmp, vha->stats.uptime_ms);
		vha->stats.core_stats[i].utilization = tmp;
		/* Calculate WM utilization. */
		tmp = vha->stats.wm_stats[i].total_proc_us;
		do_div(tmp, vha->stats.uptime_ms);
		vha->stats.wm_stats[i].utilization = tmp;
		/* Calculate cumulative core processing time. */
		core_total_proc_us += vha->stats.core_stats[i].total_proc_us;
	}
	/* Calculate cluster utilization. */
	tmp = core_total_proc_us;
	do_div(tmp, (vha->stats.uptime_ms * vha->hw_props.num_cnn_core_devs));
	vha->stats.cnn_utilization = tmp;
}

#ifdef VHA_EVENT_INJECT
/*
 * Inject EVENT_STATUS bits, requested by respective debugfs nodes, to
 * the registers defined by the currently handled WM.
 */
static inline void __inject_event_regs(struct vha_dev* vha, struct vha_mc_irq_status* irq_status)
{
	int id, wm_id;
	u32 mask, wm_mask;
	uint64_t vha_cr_sys_event = vha->injection.vha_cr_sys_event & VHA_CR_SYS_EVENT_INJECT_MASKFULL;
	uint64_t vha_cr_wm_event = vha->injection.vha_cr_wm_event & VHA_CR_WM_EVENT_INJECT_MASKFULL;
	uint64_t vha_cr_core_event = vha->injection.vha_cr_core_event & VHA_CR_CORE_EVENT_INJECT_MASKFULL;
	uint64_t vha_cr_interconnect_event = vha->injection.vha_cr_interconnect_event & VHA_CR_INTERCONNECT_EVENT_INJECT_MASKFULL;


	if(!__EVENT_INJECT())
		return;

	if(vha_cr_sys_event) {
		IOWRITE64_CR_REGIO(vha_cr_sys_event, SYS_EVENT_INJECT);
	}

	/* handle WM event injection */
	wm_mask = VHA_CR_GETBITS(HOST_EVENT_SOURCE, WM, irq_status->event_source);
	if(!wm_mask)
	  return;
	spin_lock_irqsave(&vha->irq_lock, vha->irq_flags);
	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		if(~wm_mask & (1 << wm_id))
			continue; /* inject only to currently handled WM's */
		if(vha_cr_wm_event) {
			VHA_SELECT_WM(wm_id);
			IOWRITE64_CR_REGIO(vha_cr_wm_event, WM_EVENT_INJECT);
		}
		/* now handle WM's core and ic injections . IC sources are the same as core sources */
		if(!vha_cr_core_event && !vha_cr_interconnect_event)
			continue;
		/* get cores handled by specific WM, inject errors only to those cores */
		mask = vha_wm_get_cores(vha, wm_id);
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if(~mask & (1 << id))
				continue; /* inject only to currently handled CORE's */
			if(vha_cr_core_event) {
				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(CORE_CTRL_INDIRECT,
																	MASK, (1 << id)),
																	CORE_CTRL_INDIRECT);
				IOWRITE64_CR_REGIO(vha_cr_core_event, CORE_EVENT_INJECT);
			}
			if(vha_cr_interconnect_event) {
				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(IC_CORE_INDIRECT,
																	MASK, (1 << id)),
																	IC_CORE_INDIRECT);
				IOWRITE64_CR_REGIO(vha_cr_interconnect_event, INTERCONNECT_EVENT_INJECT);
			}
		}
	}
	/* read new injected event sources */
	irq_status->event_source |= IOREAD64_CR_REGIO(HOST_EVENT_SOURCE);
	spin_unlock_irqrestore(&vha->irq_lock, vha->irq_flags);
}

static inline void __inject_parity_err(struct vha_dev* vha, struct vha_mc_irq_status* irq_status) {
	int id, wm_id;
	u32 mask, wm_mask;

	if(!__EVENT_INJECT())
		return;

	if (VHA_REG_GET_PARITY_ERROR(vha->injection.vha_cr_sys_event)) {
		VHA_REG_SET_PARITY_ERROR(irq_status->sys_events);
		irq_status->event_source |= VHA_CR_HOST_EVENT_SOURCE_SYS_EN;
	}

	wm_mask = VHA_CR_GETBITS(HOST_EVENT_SOURCE, WM, irq_status->event_source);
	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		if(~wm_mask & (1 << wm_id))
			continue; /* inject only to currently handled WM's */
		if (VHA_REG_GET_PARITY_ERROR(vha->injection.vha_cr_wm_event)) {
			VHA_REG_SET_PARITY_ERROR(irq_status->wm_events[wm_id]);
			irq_status->event_source |= 1 << (wm_id + VHA_CR_HOST_EVENT_SOURCE_WM_SHIFT);
		}

		mask = vha_wm_get_cores(vha, wm_id);
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if(~mask & (1 << id))
				continue; /* inject only to currently handled CORE's */
			if (VHA_REG_GET_PARITY_ERROR(vha->injection.vha_cr_interconnect_event)) {
				VHA_REG_SET_PARITY_ERROR(irq_status->ic_events[id]);
				irq_status->event_source |= 1 << (id + VHA_CR_HOST_EVENT_SOURCE_IC_SHIFT);
			}
		}
	}

}
#endif

/* Top half */
irqreturn_t vha_handle_irq(struct device *dev)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	irqreturn_t ret = IRQ_NONE;
	struct vha_mc_irq_status irq_status = {0};
	uint32_t multi_src_mask = 0;
	uint8_t id;
	struct TIMESPEC hw_proc_end[VHA_NUM_CORES] = {{0}};
	bool hw_proc_end_recorded[VHA_NUM_CORES] = {0};

#define CHECK_FOR_DEAD_HW(r) \
	if (r == VHA_DEAD_HW || r == ~0) { \
		WARN_ONCE(1, "Hardware is dead!"); \
		if (!in_interrupt()) \
			mutex_unlock(&vha->lock); \
		return IRQ_NONE; \
	}

	/* ML: This thing is a complete mess in regdef file. The field is present
	 * in most of these EVENT regs, but its definition varies a lot, so no idea
	 * what it is actually meant to mean.
	 */
#define CHECK_FOR_LOGIC_ERROR(s, r) \
	if (r & VHA_##s##_EVENT_TYPE(LOGIC_ERROR)) { \
		WARN_ONCE(1, "Parity error detected!"); \
		if (!in_interrupt()) \
			mutex_unlock(&vha->lock); \
		return IRQ_NONE; \
	}

	if (!vha)
		return IRQ_NONE;

	/* Note: Top half can be called from the platform worker thread */
	if (!in_interrupt())
		mutex_lock(&vha->lock);

	irq_status.event_source = IOREAD64_CR_REGIO(HOST_EVENT_SOURCE);
	/* On fpga platform it is possible to get a spurious interrupt when the hw died.
	 * Do not proceed, just throw a warning. */
	CHECK_FOR_DEAD_HW(irq_status.event_source);

#ifdef VHA_EVENT_INJECT
	__inject_event_regs(vha, &irq_status);
#endif

	if (VHA_CR_GETBITS(HOST_EVENT_SOURCE, SYS, irq_status.event_source)) {
		/* Read events. */
		irq_status.sys_events = IOREAD64_CR_REGIO(SYS_EVENT_STATUS);
		/* Just in case check for dead hw. */
		CHECK_FOR_DEAD_HW(irq_status.sys_events);
#ifdef VHA_SCF
		if (vha->hw_props.supported.parity && !vha->parity_disable) {
			uint32_t i;
			for (i = 0; i < VHA_PARITY_READ_COUNT_MAX; i++) {
				/* Finish if bit parity is ok */
				if (!img_mem_calc_parity(irq_status.sys_events))
					break;
				/* Otherwise re-read the reg. */
				irq_status.sys_events = IOREAD64_CR_REGIO(SYS_EVENT_STATUS);
			}
			/* Raise an error if maximum re-read count is reached. */
			if (i == VHA_PARITY_READ_COUNT_MAX) {
				dev_err(dev, "SYS_EVENT_STATUS register parity error!\n");
				/* Use the real event to indicate the error */
				VHA_REG_SET_PARITY_ERROR(irq_status.sys_events);
			}
		}
#endif
		/* Check for hw logic error. */
		/* ML: ??? */
		//CHECK_FOR_LOGIC_ERROR(SYS, irq_status.sys_events);

		/* wake thread even if only parity error is set. Erroneous event may occur that only
		 * parity is set among other bits
		 */
		if (irq_status.sys_events & (VHA_SYS_EVENTS_DEFAULT | VHA_REG_PARITY_ERROR_EN)) {
			/* Clear interrupts (best not to write pdump in ISR). */
			IOWRITE64_CR_REGIO(irq_status.sys_events & VHA_SYS_EVENTS_DEFAULT,
								SYS_EVENT_CLEAR);
			ret = IRQ_WAKE_THREAD;
		}
	}
	/* Read WM event source mask. */
	multi_src_mask = (uint32_t)VHA_CR_GETBITS(HOST_EVENT_SOURCE, WM,
												irq_status.event_source);
	if (multi_src_mask) {
		spin_lock_irqsave(&vha->irq_lock, vha->irq_flags);
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if (multi_src_mask & (1 << id)) {
				/* Select WM to read events from. */
				VHA_SELECT_WM(id);
				/* Read events. */
				irq_status.wm_events[id] = IOREAD64_CR_REGIO(WM_EVENT_STATUS);
				/* Just in case check for dead hw. */
				CHECK_FOR_DEAD_HW(irq_status.wm_events[id]);

				/* Record hw processing end timestamps */
				GETNSTIMEOFDAY(&hw_proc_end[id]);
				hw_proc_end_recorded[id] = true;
#ifdef VHA_SCF
				if (vha->hw_props.supported.parity && !vha->parity_disable) {
					uint32_t i;
					for (i = 0; i < VHA_PARITY_READ_COUNT_MAX; i++) {
						/* Finish if parity is ok */
						if (!img_mem_calc_parity(irq_status.wm_events[id]))
							break;
						/* Otherwise re-read the reg. */
						irq_status.wm_events[id] = IOREAD64_CR_REGIO(WM_EVENT_STATUS);
					}
					/* Raise an error if maximum re-read count is reached. */
					if (i == VHA_PARITY_READ_COUNT_MAX) {
						dev_err(dev, "WM_EVENT_STATUS[%u] register parity error!\n", id);
						/* Use the real event to indicate the error */
						VHA_REG_SET_PARITY_ERROR(irq_status.wm_events[id]);
					}
				}
#endif
				{
					/* Post check for AXI bus errors */
					uint64_t ace_status = IOREAD64(vha->reg_base, VHA_CR_ACE_STATUS);
					if (ace_status) {
						dev_err(vha->dev, "AXI bus protocol error: %#llx\n",
									ace_status);
						/* Use AXI error event to indicate that */
						irq_status.event_source |= VHA_CR_SETBITS(HOST_EVENT_SOURCE, SYS, 1);
						irq_status.sys_events |=  VHA_CR_SETBITS(SYS_EVENT_TYPE, AXI_ERROR, 1);
					}
				}

				/* wake thread even if only parity error is set. Erroneous event may occur that only
				 * parity is set among other bits
				 */
				if (irq_status.wm_events[id] & (VHA_WM_EVENTS_DEFAULT | VHA_REG_PARITY_ERROR_EN)) {
					/* Events can't be cleared, disable to avoid interrupt storm */
					IOWRITE64_CR_REGIO(0, WM_EVENT_ENABLE);
					ret = IRQ_WAKE_THREAD;
				}
			}
		}
		spin_unlock_irqrestore(&vha->irq_lock, vha->irq_flags);
	}
	/* Read CORE event source mask. */
	multi_src_mask = (uint32_t)VHA_CR_GETBITS(HOST_EVENT_SOURCE, CORE,
												irq_status.event_source);
	/* Note: Direct (Host) core event is only used for frequency measurement,
	 * Indirect (WM) core events are read in bottom handler */
	if (multi_src_mask) {
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if (multi_src_mask & (1 << id)) {
				/* Select core to read events from. */
				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(CORE_CTRL_INDIRECT,
													MASK, (1 << id)),
													CORE_CTRL_INDIRECT);
				/* Read events. */
				/* In normal operation CORE events are routed to WM,
				 * therefore there's no need to handle parity here
				 */
				irq_status.core_events[id] = IOREAD64_CR_REGIO(CORE_EVENT_HOST_STATUS);
				/* Just in case check for dead hw. */
				CHECK_FOR_DEAD_HW(irq_status.core_events[id]);
				/* Check for hw logic error. */
				/* ML: ??? */
				//CHECK_FOR_LOGIC_ERROR(CORE, irq_status.core_events[id]);
				if (irq_status.core_events[id] & VHA_CORE_EVENTS_DEFAULT) {
					/* Clear interrupts (best not to write pdump in ISR). */
					IOWRITE64_CR_REGIO(irq_status.core_events[id] & VHA_CORE_EVENTS_DEFAULT,
										CORE_EVENT_HOST_CLEAR);
					/* Record hw processing end timestamps */
					/*
					 *       for regular workloads. This stat update is used
					 *       only for cluster clock measurement, so it is
					 *       executed only once after module is loaded. */
					GETNSTIMEOFDAY(&hw_proc_end[id]);
					hw_proc_end_recorded[id] = true;
					ret = IRQ_WAKE_THREAD;
				}
			}
		}
	}
	/* Read IC event source mask. */
	multi_src_mask = (uint32_t)VHA_CR_GETBITS(HOST_EVENT_SOURCE, IC,
												irq_status.event_source);
	/* Indirect (WM) interconnect events are read in bottom handler */
	if (multi_src_mask) {
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if (ret || (multi_src_mask & (1 << id))) {
				/* Select IC to read events from. */
				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(IC_CORE_INDIRECT,
													MASK, (1 << id)),
													IC_CORE_INDIRECT);
				/* Read events. */
				/* In normal operation IC events are routed to WM,
				 * therefore there's no need to handle parity here
				 */
				irq_status.ic_events[id] = IOREAD64_CR_REGIO(INTERCONNECT_EVENT_HOST_STATUS);
#ifdef VHA_SCF
				if (vha->hw_props.supported.parity && !vha->parity_disable) {
					uint32_t i;
					for (i = 0; i < VHA_PARITY_READ_COUNT_MAX; i++) {
						/* Finish if parity is ok */
						if (!img_mem_calc_parity(irq_status.ic_events[id]))
							break;
						/* Otherwise re-read the reg. */
						irq_status.ic_events[id] = IOREAD64_CR_REGIO(INTERCONNECT_EVENT_HOST_STATUS);
					}
					/* Raise an error if maximum re-read count is reached. */
					if (i == VHA_PARITY_READ_COUNT_MAX) {
						dev_err(dev, "WM_EVENT_STATUS[%u] register parity error!\n", id);
						/* Use the real event to indicate the error */
						VHA_REG_SET_PARITY_ERROR(irq_status.ic_events[id]);
					}
				}
#endif
				/* Just in case check for dead hw. */
				CHECK_FOR_DEAD_HW(irq_status.ic_events[id]);
				/* Check for hw logic error. */
				/* ML: ??? */
				//CHECK_FOR_LOGIC_ERROR(IC, irq_status.ic_events[id]);
				if (multi_src_mask && (irq_status.ic_events[id] & (VHA_IC_EVENTS_DEFAULT | VHA_REG_PARITY_ERROR_EN))) {
					/* Clear interrupts (best not to write pdump in ISR). */
					IOWRITE64_CR_REGIO(irq_status.ic_events[id] & VHA_IC_EVENTS_DEFAULT,
										INTERCONNECT_EVENT_HOST_CLEAR);
					ret = IRQ_WAKE_THREAD;
				}
			}
		}
	}

#ifdef VHA_EVENT_INJECT
	__inject_parity_err(vha, &irq_status);
#endif

	if (!in_interrupt())
		mutex_unlock(&vha->lock);

	if (ret == IRQ_WAKE_THREAD) {
		spin_lock(&vha->irq_lock);
		/* Store all the event info. */
		vha->irq_status.event_source |= irq_status.event_source;
		vha->irq_status.sys_events   |= irq_status.sys_events;
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			vha->irq_status.wm_events[id] |= irq_status.wm_events[id];
			if (hw_proc_end_recorded[id]) {
				/* Record hw processing end timestamps */
				VHA_WM_STAT_SHIFT_PROC_END(vha, id);
				VHA_SET_WM_STAT(vha, hw_proc_end, id, hw_proc_end[id]);
			}
		}
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			vha->irq_status.core_events[id] |= irq_status.core_events[id];
			vha->irq_status.ic_events[id]   |= irq_status.ic_events[id];
		}
		spin_unlock(&vha->irq_lock);
	}

#undef CHECK_FOR_DEAD_HW
#undef CHECK_FOR_LOGIC_ERROR
	if (ret) {
		dev_dbg(dev, "IRQ EVT:0x%08llx SYS:0x%08llx\n", irq_status.event_source, irq_status.sys_events);
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			if (irq_status.wm_events[id] || irq_status.core_events[id] || irq_status.ic_events[id])
				dev_dbg(dev, "WM%d:0x%08llx CORE%d:0x%08llx IC%d:0x%08llx\n",
						id, irq_status.wm_events[id], id, irq_status.core_events[id],
				id, irq_status.ic_events[id]);
	}

	return ret;
}

static void vha_do_queued_cmd(struct vha_dev *vha, uint8_t wm_id)
{
	struct vha_cmd *cmd, *pend;

	cmd = vha->queuedcmd[wm_id].cmd;

#if defined(DEBUG)
	{
		char queued_txt[24] = "none";
		char pending_txt[24] = "none";
		if (cmd)
			snprintf(queued_txt, 24, "0x%08x/%u",
					cmd->user_cmd.cmd_id, cmd->session->id);
		if (vha->pendcmd[wm_id].cmd)
			snprintf(pending_txt, 24, "0x%08x/%u",
					vha->pendcmd[wm_id].cmd->user_cmd.cmd_id,
					vha->pendcmd[wm_id].cmd->session->id);
		dev_dbg(vha->dev,
				"%s: WM%u pending %s, queued %s\n",
				__func__, wm_id, pending_txt, queued_txt);
	}
#endif

	if (!cmd || (cmd &&
				 ((vha->low_latency == VHA_LL_DISABLED ||
					 vha->low_latency == VHA_LL_SELF_KICK) ||
					!cmd->queued))) {
		dev_dbg(vha->dev, "%s: skipping!\n", __func__);
		return;
	}

	/* store actual pending command as it will be modified */
	pend = vha->pendcmd[wm_id].cmd;

	/* at this point we should be able to process the cmd */
	vha_do_cnn_cmd(cmd);

	/* restore pending */
	vha->pendcmd[wm_id].cmd = pend;
}

/*
 * Roll back commands for a particular WM.
 */
static bool vha_rollback_wm_cmds(struct vha_dev *vha, uint8_t wm_id,
		bool free_res)
{
	bool processing = false;
#if defined(DEBUG)
	char queued_txt[24] = "none";
	char pending_txt[24] = "none";
#endif
	/* Not processed commands are still on the pending list
	 * of each session, so just mark the hw pending lists as empty */
	if (vha->pendcmd[wm_id].cmd) {
#if defined(DEBUG)
		snprintf(pending_txt, 24, "0x%08x/%u",
				vha->pendcmd[wm_id].cmd->user_cmd.cmd_id,
				vha->pendcmd[wm_id].cmd->session->id);
#endif
		if (free_res) {
			/* Free command resources. */
			vha_wm_release_cores(vha,
					vha->pendcmd[wm_id].cmd->hw_sched_info.core_mask, false);
			vha_dev_free_cmd_res(vha, vha->pendcmd[wm_id].cmd, false);
			vha->pri_q_counters[vha->pendcmd[wm_id].cmd->user_cmd.priority]++;
		}
		VHA_INC_WL_STAT(vha, kicks_aborted, vha->pendcmd[wm_id].cmd);
		vha->stats.cnn_kicks_aborted++;
		vha->pendcmd[wm_id].cmd->in_hw = false;
		vha->pendcmd[wm_id].cmd->queued = false;
		vha->pendcmd[wm_id].cmd->rolled_back = true;
		vha->pendcmd[wm_id].cmd = NULL;
		processing = true;
	}
	/* low_latency ...*/
	if (vha->queuedcmd[wm_id].cmd) {
#if defined(DEBUG)
		snprintf(queued_txt, 24, "0x%08x/%u",
				vha->queuedcmd[wm_id].cmd->user_cmd.cmd_id,
				vha->queuedcmd[wm_id].cmd->session->id);
#endif
		/* Free command resources. */
		vha_wm_release_cores(vha,
				vha->queuedcmd[wm_id].cmd->hw_sched_info.core_mask, false);
		vha_dev_free_cmd_res(vha, vha->queuedcmd[wm_id].cmd, false);
		if (vha->low_latency == VHA_LL_SELF_KICK) {
			VHA_INC_WL_STAT(vha, kicks_aborted, vha->queuedcmd[wm_id].cmd);
			vha->stats.cnn_kicks_aborted++;
			vha->pri_q_counters[vha->queuedcmd[wm_id].cmd->user_cmd.priority]++;
		}
		vha->queuedcmd[wm_id].cmd->in_hw = false;
		vha->queuedcmd[wm_id].cmd->queued = false;
		vha->queuedcmd[wm_id].cmd->rolled_back = true;
		vha->queuedcmd[wm_id].cmd = NULL;
	}
#if defined(DEBUG)
	dev_dbg(vha->dev, "%s: WM%u pending %s, queued %s\n",
			__func__, wm_id, pending_txt, queued_txt);
#endif

	return processing;
}

bool vha_rollback_cmds(struct vha_dev *vha)
{
	uint32_t wm_id;
	bool processing = false;

	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		bool wm_processing = vha_rollback_wm_cmds(vha, wm_id, true);
		processing = processing || wm_processing;
	}

	return processing;
}

static void vha_stop_processing(struct vha_dev *vha)
{
	uint32_t wm_id;

	VHA_LOCK_WM();
	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++)
		if (vha->pendcmd[wm_id].cmd != NULL) {
			uint64_t wm_mask = VHA_CR_SETBITS(HOST_EVENT_SOURCE, WM, VHA_WM_ID_TO_MASK(wm_id));
			vha_wm_reset(vha, &vha->pendcmd[wm_id].cmd->hw_sched_info);
			VHA_SELECT_WM(wm_id);
			/* Remove WM related interrupt info if it happens to be set. */
			if (vha->irq_status.event_source & wm_mask)
			{
				/* Unset the WM related source bit. */
				vha->irq_status.event_source &= ~wm_mask;
				/* Clear all WM related events. */
				IOWRITE64_CR_REGIO(vha->irq_status.wm_events[wm_id] & VHA_WM_EVENTS_DEFAULT,
									WM_EVENT_CLEAR);
				vha->irq_status.wm_events[wm_id] = 0ULL;
			}
		}
	VHA_UNLOCK_WM();
}

int vha_dev_suspend_work(struct vha_dev *vha)
{
	bool processing = false;
	int ret;

	/* Check if anything is being processed right now. */
	vha_stop_processing(vha);
	/* Rollback commands after hw is stopped. */
	processing = vha_rollback_cmds(vha);
	/* Forcing hardware disable. */
	ret = vha_dev_stop(vha, processing);

	return ret;
}

/*
 * Handles the command (of given cmd_idx) already processed by the hw.
 */
static bool vha_handle_cmd(struct vha_dev *vha, uint8_t wm_id, uint64_t status,
		int err, uint64_t rsp_err_flags)
{
	struct vha_cmd *cmd = NULL;

	if (wm_id >= vha->hw_props.num_cnn_core_devs)
		return false;

	cmd = vha->pendcmd[wm_id].cmd;
	if (unlikely(!cmd)) {
		dev_dbg(vha->dev, "No command. Probably it has been aborted\n");
		return false;
	}

	vha_cnn_cmd_completed(cmd, status, err, rsp_err_flags);

	if (status) {
		/* Rollback any queued command ... */
		vha_rollback_wm_cmds(vha, wm_id, false);
		/* Notify immediately current command */
		vha_cmd_notify(cmd);

		return false;
	}

	if (vha->queuedcmd[wm_id].cmd)
		vha->pendcmd[wm_id].cmd = vha->queuedcmd[wm_id].cmd;
	else
		vha->pendcmd[wm_id].cmd = NULL;

	vha->queuedcmd[wm_id].cmd = NULL;
	if (vha->pendcmd[wm_id].cmd)
		dev_dbg(vha->dev, "%s: WM%u 0x%08x/%u -> new pending 0x%08x/%u\n",
				__func__, wm_id, cmd->user_cmd.cmd_id, cmd->session->id,
				vha->pendcmd[wm_id].cmd->user_cmd.cmd_id,
				vha->pendcmd[wm_id].cmd->session->id);
	else
		dev_dbg(vha->dev, "%s: WM%u 0x%08x/%u -> no new pending\n",
				__func__, wm_id, cmd->user_cmd.cmd_id, cmd->session->id);

	vha_cmd_notify(cmd);

	return true;
}

void vha_dev_update_per_core_kicks(uint8_t core_mask, uint32_t *kicks_array)
{
	while (core_mask != 0) {
		uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(core_mask);
		core_mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id));
		kicks_array[curr_core_id]++;
	}
}

static int vha_report_wm_rsp_failure(struct vha_dev *vha, uint8_t wm_id,
		uint64_t wm_rsp_status, uint64_t *core_status, uint64_t *ic_status,
		enum vha_reset_type *reset_type, uint64_t *error_flags)
{
	uint8_t err_code = VHA_WM_RESPONSE_GET_ERROR_CODE(wm_rsp_status);
	int cmdid = -1;
	int sesid = -1;
	uint32_t i = 0;
	int err = -EIO;

	if (vha->pendcmd[wm_id].cmd) {
		cmdid = vha->pendcmd[wm_id].cmd->user_cmd.cmd_id;
		sesid = vha->pendcmd[wm_id].cmd->session->id;
	}
	if (vha_observers.error)
		vha_observers.error(vha->id, sesid, cmdid, wm_rsp_status);

	if (VHA_REG_GET_PARITY_ERROR(wm_rsp_status)) {
		dev_err(vha->dev, " WM%u response error: PARITY\n", wm_id);
		*reset_type = VHA_RESET_TYPE_WM;
		*error_flags |= VHA_RSP_ERROR(SW_WM_PARITY_ERROR);
	} else if (VHA_REG_GET_WL_ID_MISMATCH_ERROR(wm_rsp_status)) {
		dev_err(vha->dev, " WM%u response error: WL_ID_MISMATCH\n", wm_id);
		*reset_type = VHA_RESET_TYPE_WM;
		*error_flags |= VHA_RSP_ERROR(SW_WL_ID_MISMATCH_ERROR);
	} else if (VHA_REG_GET_CONF_ERROR(wm_rsp_status)) {
		dev_err(vha->dev, " WM%u response error: CONFIRMATION_WRITES\n", wm_id);
		*reset_type = VHA_RESET_TYPE_WM;
		*error_flags |= VHA_RSP_ERROR(SW_CONF_ERROR);
	} else if (VHA_REG_GET_COMBINED_CRC_ERROR(wm_rsp_status)) {
		dev_err(vha->dev, " WM%u response error: COMBINED_CRC\n", wm_id);
		*reset_type = VHA_RESET_TYPE_WM;
		*error_flags |= VHA_RSP_ERROR(SW_CRC_MISMATCH_ERROR);
	} else {
		while (i < ARRAY_SIZE(wm_rsp_err_codes)) {
			if (wm_rsp_err_codes[i].e == err_code) {
				uint8_t core_id = VHA_WM_RESPONSE_GET_FAILED_CORE_IDX(wm_rsp_status);
				/* Store reset type. */
				*reset_type = wm_rsp_err_codes[i].reset_type;
				/* Error that caused the Workload Manager to halt*/
				dev_err(vha->dev, " WM%u error code:%d -> %s, failure on core%u\n",
						wm_id, err_code, wm_rsp_err_codes[i].s, core_id);
				*error_flags |=  wm_rsp_err_codes[i].rsp_err;
				if (core_id < vha->hw_props.num_cnn_core_devs) {
					i = 0;
					while (core_err_bits[i].e != 0) {
						if (core_status[core_id] & core_err_bits[i].b) {
							dev_err(vha->dev, "         %s\n", core_err_bits[i].s);
							err = core_err_bits[i].e;
						}
						i++;
					}
					i = 0;
					while (ic_err_bits[i].e != 0) {
						if (ic_status[core_id] & ic_err_bits[i].b) {
							dev_err(vha->dev, "         %s\n", ic_err_bits[i].s);
							err = ic_err_bits[i].e;
						}
						i++;
					}
				} else
					dev_err(vha->dev, "         invalid FAILED_CORE_ID, should be <%u\n",
							vha->hw_props.num_cnn_core_devs);
				goto exit;
			}
			i++;
		}

		dev_err(vha->dev, " invalid WM ERROR_CODE: %u\n", err_code);
	}

exit:
	return err;
}

static void vha_handle_sys_failure(struct vha_dev *vha, uint64_t status, int err, uint64_t rsp_err_flags)
{
	int cmdid = -1;
	int sesid = -1;
	uint32_t wm_id;
	struct vha_cmd *cmd = NULL;

	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		cmd = vha->pendcmd[wm_id].cmd;
		if (cmd) {
			cmdid = cmd->user_cmd.cmd_id;
			sesid = cmd->session->id;
		}
		if (vha_observers.error)
			vha_observers.error(vha->id, sesid, cmdid, status);
		cmdid = -1;

		if (cmd) {
			/* Update stats. */
			vha->stats.total_failures++;
			vha->stats.cnn_kicks_completed++;
			VHA_INC_WL_STAT(vha, kicks_completed, cmd);
			vha_wm_reset(vha, &cmd->hw_sched_info);
			/* Free command resources. */
			vha_wm_release_cores(vha, cmd->hw_sched_info.core_mask, false);
			vha_dev_free_cmd_res(vha, cmd, true);
		}
		/* Move command queue. */
		vha_do_queued_cmd(vha, wm_id);
		/* Handle actual command */
		vha_handle_cmd(vha, wm_id, status, err, rsp_err_flags);
	}
}

static void vha_handle_wm_failure(struct vha_dev *vha, uint8_t wm_id,
		uint64_t status, int err, uint64_t rsp_err_flags)
{
	int cmdid = -1;
	int sesid = -1;
	struct vha_cmd *cmd = NULL;

	cmd = vha->pendcmd[wm_id].cmd;
	if (cmd) {
		cmdid = cmd->user_cmd.cmd_id;
		sesid = cmd->session->id;
	}
	if (vha_observers.error)
		vha_observers.error(vha->id, sesid, cmdid, status);

	if (cmd) {
		/* Update stats. */
		vha->stats.total_failures++;
		vha->stats.cnn_kicks_completed++;
		VHA_INC_WL_STAT(vha, kicks_completed, cmd);
		/* Free command resources. */
		vha_wm_release_cores(vha, cmd->hw_sched_info.core_mask, false);
		vha_dev_free_cmd_res(vha, cmd, true);
	}
	/* Move command queue. */
	vha_do_queued_cmd(vha, wm_id);
	/* Handle actual command */
	vha_handle_cmd(vha, wm_id, status, err, rsp_err_flags);
}

static enum vha_reset_type vha_sys_get_reset_type(struct vha_dev *vha,
		uint64_t event_mask) {
	enum vha_reset_type sys_reset_type = VHA_RESET_TYPE_NONE;
#ifdef VHA_SCF
	uint64_t sys_err_events = VHA_SYS_ERR_EVENTS | VHA_REG_PARITY_ERROR_EN;
#else
	uint64_t sys_err_events = VHA_SYS_ERR_EVENTS;
#endif
	if (event_mask & sys_err_events) {
		uint32_t i = 0;
		while (sys_err_bits[i].e != 0) {
			if (event_mask & sys_err_bits[i].b) {
				/* Indicate the highest reset level of all errors. */
				if (sys_err_bits[i].reset_type > sys_reset_type)
					sys_reset_type = sys_err_bits[i].reset_type;
			}
			i++;
		}
	}

	return sys_reset_type;
}

static void vha_sys_get_wm_reset_types(struct vha_dev *vha, uint64_t event_mask,
		enum vha_reset_type *wm_reset_types) {
	uint8_t wm_id;
	uint8_t pf_errors;
#ifdef VHA_SCF
//	uint8_t parity_errors;
#endif

	/* Check MMU page fault errors. */
	pf_errors = (uint8_t)VHA_CR_GETBITS(SYS_EVENT_STATUS, MMU_PAGE_FAULT,
										event_mask);
	if (pf_errors) {
		wm_id = 0;
		while(wm_id < vha->hw_props.num_cnn_core_devs) {
			if (pf_errors & (1 << wm_id))
				wm_reset_types[wm_id] = VHA_RESET_TYPE_MMU;
			else
				wm_reset_types[wm_id] = VHA_RESET_TYPE_NONE;
			wm_id++;
		}
	}
#ifdef VHA_SCF
	/* Check MMU parity errors. */
	
//	uint8_t parity_errors = (uint8_t)VHA_CR_GETBITS(SYS_EVENT_STATUS, MMU_PARITY_ERROR,
//												event_mask);
//	if (parity_errors) {
//		wm_id = 0;
//		while(wm_id < vha->hw_props.num_cnn_core_devs) {
//			if (parity_errors & (1 << wm_id))
//				wm_reset_types[wm_id] = VHA_RESET_TYPE_MMU;
//			else
//				wm_reset_types[wm_id] = VHA_RESET_TYPE_NONE;
//			wm_id++;
//		}
//	}
#endif
}

static enum vha_reset_type vha_wm_get_reset_type(struct vha_dev *vha,
		uint64_t event_mask) {
	enum vha_reset_type wm_reset_type = VHA_RESET_TYPE_NONE;
#ifdef VHA_SCF
	uint64_t wm_err_events = VHA_WM_ERR_EVENTS | VHA_REG_PARITY_ERROR_EN;
#else
	uint64_t wm_err_events = VHA_WM_ERR_EVENTS;
#endif
	if (event_mask & wm_err_events) {
		uint32_t i = 0;
		while (wm_err_bits[i].e != 0) {
			if (event_mask & wm_err_bits[i].b) {
				/* Indicate the highest reset level of all errors. */
				if (wm_err_bits[i].reset_type > wm_reset_type)
					wm_reset_type = wm_err_bits[i].reset_type;
			}
			i++;
		}
	}

	return wm_reset_type;
}

static enum vha_reset_type vha_core_get_reset_type(struct vha_dev *vha,
		uint64_t event_mask) {
	enum vha_reset_type core_reset_type = VHA_RESET_TYPE_NONE;
	uint64_t core_err_events = VHA_CORE_ERR_EVENTS;

	if (event_mask & core_err_events) {
		uint32_t i = 0;
		while (core_err_bits[i].e != 0) {
			if (event_mask & core_err_bits[i].b) {
				/* Indicate the highest reset level of all errors. */
				if (core_err_bits[i].reset_type > core_reset_type)
					core_reset_type = core_err_bits[i].reset_type;
			}
			i++;
		}
	}

	return core_reset_type;
}

static enum vha_reset_type vha_ic_get_reset_type(struct vha_dev *vha,
		uint64_t event_mask) {
	enum vha_reset_type ic_reset_type = VHA_RESET_TYPE_NONE;
#ifdef VHA_SCF
	uint64_t ic_err_events = VHA_IC_ERR_EVENTS | VHA_REG_PARITY_ERROR_EN;
#else
	uint64_t ic_err_events = VHA_IC_ERR_EVENTS;
#endif

	if (event_mask & ic_err_events) {
		uint32_t i = 0;
		while (ic_err_bits[i].e != 0) {
			if (event_mask & ic_err_bits[i].b) {
				/* Indicate the highest reset level of all errors. */
				if (ic_err_bits[i].reset_type > ic_reset_type)
					ic_reset_type = ic_err_bits[i].reset_type;
			}
			i++;
		}
	}

	return ic_reset_type;
}

static int vha_report_sys_failures(struct vha_dev *vha, uint64_t event_mask, uint64_t *error_flags)
{
	int error = 0;
	uint32_t i;
	bool print_header = true;
	uint8_t pf_status;

	/* Print event status in human readable form. */
	i = 0;
	while (sys_err_bits[i].e != 0) {
		if (event_mask & sys_err_bits[i].b) {
			if (print_header) {
				dev_err(vha->dev, " SYS event status:\n");
				print_header = false;
			}
			dev_err(vha->dev, "     %s\n", sys_err_bits[i].s);
			/* Convert from register bits into POSIX errno.
			 * If multiple errors, then arbitrary errno choice. */
			error = sys_err_bits[i].e;
			*error_flags |=  sys_err_bits[i].rsp_err;
		}
		i++;
	}

	if (error) {
		dev_err(vha->dev, " SYS failure:\n");
		dev_err(vha->dev, "  SYS_CLK_STATUS0:   0x%016llx\n",
				IOREAD64_CR_REGIO(SYS_CLK_STATUS0));
		dev_err(vha->dev, "  SYS_EVENT_STATUS:  0x%016llx\n",
				event_mask);
		for (i = 0; i < vha->hw_props.num_cnn_core_devs; i++) {
			if (vha->active_core_mask & (1 << i)) {
				/* Select core to read clocks from. */
				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(CORE_CTRL_INDIRECT,
													MASK, (1 << i)),
													CORE_CTRL_INDIRECT);
				dev_err(vha->dev, "  CORE%u CLK_STATUS0: 0x%016llx\n",
						i, IOREAD64_CR_REGIO(CLK_STATUS0));
			}
		}
	}

	if (error == -ETIMEDOUT) {
		dev_err(vha->dev, "  SLC_STATUS1:       0x%016llx\n",
				IOREAD64_CR_REGIO(SLC_STATUS1));
		dev_err(vha->dev, "  SLC_STATUS2:       0x%016llx\n",
				IOREAD64_CR_REGIO(SLC_STATUS2));
		dev_err(vha->dev, "  SLC_IDLE:          0x%016llx\n",
				IOREAD64_CR_REGIO(SLC_IDLE));
	}

	/* Additionally report MMU PF failure if occurred. */
	pf_status = (uint8_t)VHA_CR_GETBITS(SYS_EVENT_STATUS, MMU_PAGE_FAULT,
										event_mask);
	if (pf_status) {
		/* dump mmu status */
		vha_mmu_status(vha, pf_status);
	}

	return error;
}

static int vha_report_wm_failures(struct vha_dev *vha, uint8_t wm_id, uint64_t event_mask, uint64_t *error_flags)
{
	int error = 0;
	uint32_t i;
	bool print_header = true;

	/* Print event status in human readable form. */
	i = 0;
	print_header = true;
	while (wm_err_bits[i].e != 0) {
		if (event_mask & wm_err_bits[i].b) {
			if (print_header) {
				dev_err(vha->dev, " WM%u event status:\n", wm_id);
				print_header = false;
			}
			dev_err(vha->dev, "     %s\n", wm_err_bits[i].s);
			/* Convert from register bits into POSIX errno.
			 * If multiple errors, then arbitrary errno choice. */
			error = wm_err_bits[i].e;
			*error_flags |=  wm_err_bits[i].rsp_err;
		}
		i++;
	}

	if (error == -ETIMEDOUT) {
		vha_wm_status(vha, wm_id, vha_wm_get_cores(vha, wm_id));
	}
	return error;
}

static int vha_report_core_failures(struct vha_dev *vha, uint8_t core_id, uint64_t event_mask, uint64_t *error_flags)
{
	int error = 0;
	uint32_t i;
	bool print_header = true;

	/* Print event status in human readable form. */
	i = 0;
	print_header = true;
	while (core_err_bits[i].e != 0) {
		if (event_mask & core_err_bits[i].b) {
			if (print_header) {
				dev_err(vha->dev, " Core %u event status:\n", core_id);
				print_header = false;
			}
			dev_err(vha->dev, "     %s\n", core_err_bits[i].s);
			/* Convert from register bits into POSIX errno.
			 * If multiple errors, then arbitrary errno choice. */
			error = core_err_bits[i].e;
			*error_flags |=  core_err_bits[i].rsp_err;
		}
		i++;
	}

	return error;
}

static int vha_report_ic_failures(struct vha_dev *vha, uint8_t core_id, uint64_t event_mask, uint64_t *error_flags)
{
	int error = 0;
	uint32_t i;
	bool print_header = true;

	/* Print event status in human readable form. */
	i = 0;
	print_header = true;
	while (ic_err_bits[i].e != 0) {
		if (event_mask & ic_err_bits[i].b) {
			if (print_header) {
				dev_err(vha->dev, " IC %u event status:\n", core_id);
				print_header = false;
			}
			dev_err(vha->dev, "     %s\n", ic_err_bits[i].s);
			/* Convert from register bits into POSIX errno.
			 * If multiple errors, then arbitrary errno choice. */
			error = ic_err_bits[i].e;
			*error_flags |=  ic_err_bits[i].rsp_err;
		}
		i++;
	}

	return error;
}

static uint8_t vha_events_process_errors(struct vha_dev *vha,
		struct vha_mc_irq_status *irq_status, bool *full_reset,
		bool *process_sys_events, uint64_t *error_flags) {

	int error = 0;
	int wm_error = 0;
	int core_error = 0;
	int ic_error = 0;
	uint8_t wm_process_mask = 0;
	uint8_t wm_source_mask = 0;
	uint8_t wm_id;
	enum vha_reset_type reset_type = VHA_RESET_TYPE_NONE;
	enum vha_reset_type wm_reset_type = VHA_RESET_TYPE_NONE;
	enum vha_reset_type wm_reset_types[VHA_NUM_CORES] = {0};
	uint64_t sys_err_status = 0;
	uint64_t wm_err_status_full_reset = 0;
	uint64_t wm_err_statuses[VHA_NUM_CORES] = {0};
	enum vha_reset_type core_reset_type = 0;
	enum vha_reset_type ic_reset_type = 0;
	uint8_t core_id;

#define COMBINE_SYS_WM_STATUS(s, w) \
	(((w & ~((uint64_t)VHA_WM_ERR_EVENTS)) | s) | \
	 ((w & ((uint64_t)VHA_WM_ERR_EVENTS)) << 32))
#define INSERT_WM_ERROR(s, e) \
	((s | ((uint64_t)e)) << 32)

	/* Assume no full reset. */
	*full_reset = false;
	/* Assume no SYS events. */
	*process_sys_events = false;

	/* Process SYS events. */
	if (VHA_CR_GETBITS(HOST_EVENT_SOURCE, SYS, irq_status->event_source)) {
#ifdef VHA_SCF
		uint64_t sys_err_events = VHA_SYS_ERR_EVENTS | VHA_REG_PARITY_ERROR_EN;
#else
		uint64_t sys_err_events = VHA_SYS_ERR_EVENTS;
#endif
		sys_err_status = irq_status->sys_events & sys_err_events;
		if (sys_err_status) {
			/* Determine reset types. */
			reset_type = vha_sys_get_reset_type(vha, irq_status->sys_events);
			if (reset_type < VHA_RESET_TYPE_FULL) {
				vha_sys_get_wm_reset_types(vha, irq_status->sys_events, wm_reset_types);
				wm_id = 0;
				while(wm_id < vha->hw_props.num_cnn_core_devs) {
					if (wm_reset_types[wm_id] > VHA_RESET_TYPE_NONE)
						wm_err_statuses[wm_id] = sys_err_status;
					wm_id++;
				}
			}
			/* Report SYS errors. */
			error = vha_report_sys_failures(vha, irq_status->sys_events, error_flags);
		}
		/* If no full reset is requested at this stage
		 * and there are non-error SYS events raised,
		 * signal them to be processed too. */
		if ((reset_type < VHA_RESET_TYPE_FULL) &&
			(irq_status->sys_events & ~sys_err_events))
			*process_sys_events = true;
	}

	/* Process WM events. */
	/* Read WM event source mask. */
	wm_source_mask = (uint32_t)VHA_CR_GETBITS(HOST_EVENT_SOURCE, WM,
												irq_status->event_source);
	if (wm_source_mask)
		for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++)
			if (wm_source_mask & (1 << wm_id)) {
#ifdef VHA_SCF
				uint64_t wm_err_events = VHA_WM_ERR_EVENTS | VHA_REG_PARITY_ERROR_EN;
#else
				uint64_t wm_err_events = VHA_WM_ERR_EVENTS;
#endif
				uint64_t wm_err_status = irq_status->wm_events[wm_id] & wm_err_events;
				if (wm_err_status) {
					/* If no full reset is requested... */
					if (reset_type < VHA_RESET_TYPE_FULL) {
						/* Determine reset type for this WM. */
						wm_reset_type = vha_wm_get_reset_type(
								vha, irq_status->wm_events[wm_id]);
						/* If full reset is requested for this WM, just skip
						 * checking other ones. Otherwise update reset type
						 * for this WM if needed. */
						if (wm_reset_type == VHA_RESET_TYPE_FULL) {
							reset_type = VHA_RESET_TYPE_FULL;
							wm_err_status_full_reset = wm_err_status;
						} else if (wm_reset_type > wm_reset_types[wm_id])
							wm_reset_types[wm_id] = wm_reset_type;
					}
					/* Compose accumulated error status. */
					wm_err_statuses[wm_id] =
							COMBINE_SYS_WM_STATUS(sys_err_status, wm_err_status);
					/* Report WM errors. */
					wm_error = vha_report_wm_failures(vha, wm_id,
												irq_status->wm_events[wm_id], error_flags);
					/* If no SYS error reported, get the first WM one. */
					if (error == 0)
						error = wm_error;
				}
			}

	/* Process core events */
	for (core_id = 0; core_id < vha->hw_props.num_cnn_core_devs; core_id++)
		if (irq_status->core_events[core_id] & VHA_CORE_ERR_EVENTS) {
			/* Determine reset type for this Core. */
			core_reset_type = vha_core_get_reset_type(vha, irq_status->core_events[core_id]);

			/* We do not reset the core itself, instead, we need to reset
			   the WM that used it, so let's find it */
			for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++)
				if (vha->pendcmd[wm_id].cmd != NULL) {
					uint8_t  core_mask = vha->pendcmd[wm_id].cmd->hw_sched_info.core_mask;
					if (core_mask & (1 << core_id)) {
						/* Override wm reset type */
						if (core_reset_type == VHA_RESET_TYPE_FULL)
							reset_type = VHA_RESET_TYPE_FULL;
						else if (core_reset_type > wm_reset_types[wm_id])
							wm_reset_types[wm_id] = core_reset_type;

						core_error = vha_report_core_failures(vha, core_id,
											irq_status->core_events[core_id], error_flags);

						/* If no SYS or WM error reported, get the first Core one. */
						if (error == 0)
							error = core_error;
						/* Add core error to this WM's status. */
						wm_err_statuses[wm_id] =
								INSERT_WM_ERROR(wm_err_statuses[wm_id], VHA_REG_WM_CORE_ERROR_EN);
					}
				}
		}

	/* Process IC events */
	for (core_id = 0; core_id < vha->hw_props.num_cnn_core_devs; core_id++) {
#ifdef VHA_SCF
		uint64_t ic_err_events = VHA_IC_ERR_EVENTS | VHA_REG_PARITY_ERROR_EN;
#else
		uint64_t ic_err_events = VHA_IC_ERR_EVENTS;
#endif
		if (irq_status->ic_events[core_id] & ic_err_events) {
			/* Determine reset type for this Core. */
			ic_reset_type = vha_ic_get_reset_type(vha, irq_status->ic_events[core_id]);

			/* We do not reset the core itself, instead, we need to reset
			   the WM that used it, so let's find it */
			for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++)
				if (vha->pendcmd[wm_id].cmd != NULL) {
					uint8_t  core_mask = vha->pendcmd[wm_id].cmd->hw_sched_info.core_mask;
					if (core_mask & (1 << core_id)) {
						/* Override wm reset type */
						if (ic_reset_type == VHA_RESET_TYPE_FULL)
							reset_type = VHA_RESET_TYPE_FULL;
						else if (ic_reset_type > wm_reset_types[wm_id])
							wm_reset_types[wm_id] = ic_reset_type;

						ic_error = vha_report_ic_failures(vha, core_id,
											irq_status->ic_events[core_id], error_flags);

						/* If no SYS or WM error reported, get the first Core one. */
						if (error == 0)
							error = ic_error;
						/* Add IC error to this WM's status. */
						wm_err_statuses[wm_id] =
								INSERT_WM_ERROR(wm_err_statuses[wm_id], VHA_REG_WM_IC_ERROR_EN);
					}
				}
		}
	}


	/* Perform selective resets. */
	if (reset_type < VHA_RESET_TYPE_FULL) {
		int ret;
		for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
			struct vha_cmd *cmd = vha->pendcmd[wm_id].cmd;

			switch (wm_reset_types[wm_id]) {
			case VHA_RESET_TYPE_MMU:
				if (cmd) {
					/* Invalidate MMU. */
					ret = vha_mmu_flush_ctx(vha, cmd->session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].hw_id);
					if(ret) {
						
						dev_err(vha->dev, "Error during MMU flush, doing full reset\n");
						wm_err_status_full_reset = wm_err_statuses[wm_id];
						reset_type = VHA_RESET_TYPE_FULL;
						break;
					}
				}
				// fall through
			case VHA_RESET_TYPE_WM:
				dev_err(vha->dev, "Performing wm%d reset due to HW error detection.", wm_id);
				if (cmd)
					/* Reset WM and assigned cores. */
					ret = vha_wm_reset(vha, &cmd->hw_sched_info);
				else {
					/* Just reset WM. */
					struct vha_hw_sched_info sched_info = {
							.wm_id = wm_id,
							.core_mask = 0
					};
					ret = vha_wm_reset(vha, &sched_info);
				}
				if(ret) {
					dev_err(vha->dev, "Error during WM%d reset, doing full reset\n", wm_id);
					wm_err_status_full_reset = wm_err_statuses[wm_id];
					reset_type = VHA_RESET_TYPE_FULL;
					break;
				}
				VHA_LOCK_WM();
				VHA_SELECT_WM(wm_id);
				/* Clear all WM related events. */
				IOWRITE64_CR_REGIO(VHA_WM_EVENTS_DEFAULT, WM_EVENT_CLEAR);
				/* Re-enable WM events here as this WM will not be handled further. */
				IOWRITE64_CR_REGIO(VHA_WM_EVENTS_DEFAULT, WM_EVENT_ENABLE);
				VHA_UNLOCK_WM();
				/* Handle pending command. */
				vha_handle_wm_failure(vha, wm_id, wm_err_statuses[wm_id], error, *error_flags);
				break;
			case VHA_RESET_TYPE_NONE:
				/* Mark WM source for normal processing if it was signalled. */
				if (cmd)
					wm_process_mask |= wm_source_mask & (1 << wm_id);
				break;
			default:
				break;
			}
		}
	}
	/* check once again, reset_type may have been updated due to failure during reset procedure */
	if(reset_type == VHA_RESET_TYPE_FULL){
		/* Handle all pending commands. */
		vha_handle_sys_failure(vha,
			COMBINE_SYS_WM_STATUS(sys_err_status, wm_err_status_full_reset), error, *error_flags);
		/* Full reset is requested anyway, so skip processing further SYS events. */
		*process_sys_events = false;
		/* Full reset will be executed outside. Just indicate here
		 * that it's required.*/
		*full_reset = true;
	}

	return wm_process_mask;
}

/* if vha event register reports WM events, so handle them */
static void vha_handle_wm_response(struct vha_dev *vha, uint8_t wm_id,
		uint64_t response_status, uint64_t *core_status_array,
		uint64_t *ic_status_array, bool *full_reset, uint64_t *error_flags)
{
	enum vha_reset_type reset_type = VHA_RESET_TYPE_NONE;
	int err = *error_flags ? -EIO : 0 ;

	if (response_status &
			(VHA_WM_RESPONSE_STATUS(WL_FAILURE) |
			 VHA_REG_PARITY_ERROR_EN |
			 VHA_REG_WL_ID_MISMATCH_ERROR_EN |
			 VHA_REG_CONF_ERROR_EN |
			 VHA_REG_COMBINED_CRC_ERROR_EN)) {
		err = vha_report_wm_rsp_failure(vha, wm_id, response_status,
							core_status_array, ic_status_array, &reset_type, error_flags);
	}

	/* Move command queue. */
	switch (reset_type) {
	case VHA_RESET_TYPE_NONE:
		vha_do_queued_cmd(vha, wm_id);
		break;
	case VHA_RESET_TYPE_WM:
		if (!*full_reset && vha->pendcmd[wm_id].cmd) {
			dev_err(vha->dev, "Performing wm%d reset due to HW error detection.", wm_id);
			if (vha_wm_reset(vha, &vha->pendcmd[wm_id].cmd->hw_sched_info)) {
				dev_err(vha->dev, "%s: Error during WM%u reset, forcing full reset upon finish",
						__func__, wm_id);
				*full_reset = true;
			}
		}
		break;
	case VHA_RESET_TYPE_FULL:
		*full_reset = true;
		break;
	default:
		break;
	}
	/* Handle actual command */
	if (vha_handle_cmd(vha, wm_id, response_status, err, *error_flags) == false)
		reset_type = VHA_RESET_TYPE_NONE;
}

#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
/* Simulating hw execution time by scheduling this delayed work. */
void vha_dummy_worker(struct work_struct *work)
{
	struct vha_dummy_work *dummy_work =
					container_of(work, struct vha_dummy_work, dummy_dwork.work);
	struct vha_dev *vha = dummy_work->vha;
	struct vha_cmd *cmd;

	mutex_lock(&vha->lock);

	cmd = vha->pendcmd[dummy_work->wm_id].cmd;
	if (cmd) {
		uint64_t error_flags = 0;
		bool full_reset = false;
		/* Record hw processing end timestamps */
		VHA_WM_STAT_SHIFT_PROC_END(vha, cmd->hw_sched_info.wm_id);
		GETNSTIMEOFDAY(&vha->stats.wm_stats[cmd->hw_sched_info.wm_id].hw_proc_end);
		/* Update per core/WM stats. */
		VHA_INC_WL_STAT(vha, kicks_completed, cmd);
		vha->stats.cnn_kicks_completed++;
		/* Free command resources. */
		if (!vha->hw_sched_status.assignments[cmd->hw_sched_info.assignment_id].queued)
			vha_wm_release_cores(vha, cmd->hw_sched_info.core_mask, false);
		vha_dev_free_cmd_res(vha, cmd, true);
		/* Handle current pending command */
		vha_handle_wm_response(vha, dummy_work->wm_id, 0, NULL, NULL, &full_reset, &error_flags);
		/* Schedule following commands */
		vha_chk_cmd_queues(vha, true);
	}

	mutex_unlock(&vha->lock);
}
#endif

#ifdef VHA_SCF
static void vha_handle_conf_status(struct vha_dev *vha, struct vha_cmd *cmd, bool *full_reset, uint64_t *status)
{
	if (wait_for_completion_timeout(&cmd->conf_done, msecs_to_jiffies(CONF_WRITES_WAIT_TIMEOUT_MS))) {
		if (cmd->conf_top_error) {
			dev_err(vha->dev, "CONF_ERR_TOP\n");
			*full_reset = true;
			VHA_REG_SET_CONF_ERROR(*status);
			return;
		}
		if (cmd->conf_core_error) {
			dev_err(vha->dev, "CONF_ERR_BOTTOM\n");
			VHA_REG_SET_CONF_ERROR(*status);
			return;
		}
	} else {
		dev_err(vha->dev, "Confirmation writes procedure failed!\n");
		VHA_REG_SET_CONF_ERROR(*status);
	}
}


static void vha_check_crc(struct vha_dev *vha, struct vha_cmd *cmd, uint64_t *status)
{
	struct vha_session *session = cmd->session;
	struct vha_hw_sched_info *sched_info = &cmd->hw_sched_info;
	uint32_t core_id = 0;
	uint32_t idx = 0;
	uint8_t num_cores = VHA_CORE_MASK_TO_NUM(sched_info->core_mask);
	uint32_t crcs[VHA_MAX_CORES];
	uint32_t *golden_crcs = NULL;
	struct vha_buffer *buf = session->cnn_dbg.cnn_combined_crc;
	bool crc_enabled = !!(cmd->user_cmd.flags & VHA_CHECK_CRC);

	if (!buf || !buf->kptr) {
		dev_err(vha->dev, "%s: Invalid crc buf\n", __func__);
		return;
	}

	img_mem_sync_device_to_cpu(session->mem_ctx, buf->id);
	for (core_id = 0; core_id < VHA_MAX_CORES; core_id++)
		if (sched_info->core_mask & (1 << core_id)) {
			memcpy(&crcs[idx], (uint8_t*)buf->kptr + core_id * VHA_COMBINED_CRC_CORE_OFFSET, sizeof(crcs[0]));
			idx++;
		}

	vha_update_crcs(vha, crcs, num_cores);

	if (crc_enabled) {
		struct vha_user_cnn_submit_multi_cmd *cnn_user_cmd =
			(struct vha_user_cnn_submit_multi_cmd *)&cmd->user_cmd;
		golden_crcs = cnn_user_cmd->crcs;

		for (idx = 0; idx < num_cores; idx++)
			if (crcs[idx] != golden_crcs[idx]) {
				VHA_REG_SET_COMBINED_CRC_ERROR(*status);
				dev_err(vha->dev, "%s: combined CRC mismatch !!!\n"
								  "\tcrc %x\n"
								  "\tgolden_crc %x\n", __func__, crcs[idx], golden_crcs[idx]);
			} else {
				dev_info(vha->dev, "%s: combined CRC ok, crc %x\n", __func__, crcs[idx]);
			}
	}
}
#endif

/* Bottom half */
irqreturn_t vha_handle_thread_irq(struct device *dev)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	irqreturn_t ret = IRQ_HANDLED;
	struct vha_mc_irq_status irq_status;
	uint64_t multi_src_mask = 0;
	uint8_t id;
	uint8_t wm_id;
	uint8_t wm_process_mask = 0;
	bool full_reset = false;
	bool process_sys_events = false;
	uint64_t error_flags = 0;

	if (!vha)
		return IRQ_NONE;

	mutex_lock(&vha->lock);

#ifdef CONFIG_FAULT_INJECTION
	if (!vha->irq_bh_pid)
		vha->irq_bh_pid = task_pid_nr(current);

	if (vha->fault_inject & VHA_FI_IRQ_WORKER)
		current->make_it_fail = true;
	else
		current->make_it_fail = false;
#endif

	spin_lock_irq(&vha->irq_lock);
	irq_status = vha->irq_status;
	memset(&vha->irq_status, 0, sizeof(vha->irq_status));
	if (irq_status.sys_events || vha->do_calibration) {
		uint64_t proc_time = 0;

		if (get_timespan_us(&vha->stats.wm_stats[VHA_CALIBRATION_WM_ID].hw_proc_start,
							&vha->stats.wm_stats[VHA_CALIBRATION_WM_ID].hw_proc_end,
							&proc_time)) {
			vha->stats.last_proc_us = proc_time;
		} else {
			vha->stats.last_proc_us = 0;
		}
	}
	spin_unlock_irq(&vha->irq_lock);

	/* Read CORE event source mask. */
	multi_src_mask = (uint32_t)VHA_CR_GETBITS(HOST_EVENT_SOURCE, CORE,
												irq_status.event_source);
	/* Check for clock calibration first. */
	if ((multi_src_mask == VHA_CALIBRATION_CORE_MASK) &&
			(irq_status.core_events[VHA_CALIBRATION_CORE_ID] &
									VHA_CORE_EVENT_TYPE(CORE_WDT))) {
		if (vha_check_calibration(vha)) {
			goto calibration_end;
		}
	}

	/* Read core/interconnect events if System or WM event occurred */
	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		uint8_t mask = 0;

		if (irq_status.wm_events[wm_id])
			mask = vha_wm_get_cores(vha, wm_id);

		if (irq_status.sys_events)
			mask |= (1 << wm_id);

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if(mask & (1 << id)) {
				/* Select core to read events from. */
				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(CORE_CTRL_INDIRECT,
													MASK, (1 << id)),
													CORE_CTRL_INDIRECT);

				irq_status.core_events[id] |= IOREAD64_CR_REGIO(CORE_EVENT_WM_STATUS);
				if (irq_status.core_events[id] & VHA_CORE_ERR_EVENTS) {
					IOWRITE64_CR_REGIO(irq_status.core_events[id] & VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_WM_CLEAR);
					irq_status.core_events[id] |= IOREAD64_CR_REGIO(CORE_EVENT_WM_STATUS);
				}

				/* Select IC to read events from. */
				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(IC_CORE_INDIRECT,
													MASK, (1 << id)),
													IC_CORE_INDIRECT);

				irq_status.ic_events[id] |= IOREAD64_CR_REGIO(INTERCONNECT_EVENT_WM_STATUS);
				if (irq_status.ic_events[id] & VHA_IC_ERR_EVENTS) {
					IOWRITE64_CR_REGIO(irq_status.ic_events[id] & VHA_IC_EVENTS_DEFAULT, INTERCONNECT_EVENT_WM_CLEAR);
					irq_status.ic_events[id] |= IOREAD64_CR_REGIO(INTERCONNECT_EVENT_WM_STATUS);
				}
			}
		}
	}

	/* Process errors first. */
	wm_process_mask = vha_events_process_errors(vha, &irq_status,
											&full_reset, &process_sys_events, &error_flags);

	/* Process non-error system events. */
	if (process_sys_events) {
		/* Handle normal system events. */
	}

	/* Process non-failed WM events. */
	if (wm_process_mask) {
		uint64_t rsp_err_status = 0ULL;
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			if (wm_process_mask & (1 << id)) {
				if (irq_status.wm_events[id] & VHA_WM_EVENTS) {
					uint16_t wm_cmd_id;
					uint64_t status;
#ifdef VHA_SCF
					uint64_t wm_rsp_err_events =
							((VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_MASKFULL |
								VHA_REG_PARITY_ERROR_EN | VHA_REG_WL_ID_MISMATCH_ERROR_EN) &
								~((uint64_t)VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_SUCCESS_EN |
										(uint64_t)VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_PARITY_EN));
#else
					uint64_t wm_rsp_err_events =
							((VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_MASKFULL |
								VHA_REG_WL_ID_MISMATCH_ERROR_EN) &
								~(VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_SUCCESS_EN |
									VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_PARITY_EN));
#endif
					struct vha_cmd *cmd = vha->pendcmd[id].cmd;

					if (cmd != NULL) {
						/* Select WM to read response from. */
						VHA_LOCK_WM();
						VHA_SELECT_WM(id);
						/* Handle RESPONSE_FIFO. */
						/* Read RESPONSE_FIFO_WL_STATUS. */
						status = IOREAD64_CR_REGIO(WM_RESPONSE_FIFO_WL_STATUS);
#ifdef VHA_SCF
						if (vha->hw_props.supported.parity && !vha->parity_disable) {
							uint32_t i;
							for (i = 0; i < VHA_PARITY_READ_COUNT_MAX; i++) {
								/* Finish if parity is ok */
								if (!img_mem_calc_parity(status))
									break;
								/* Otherwise re-read the reg. */
								status = IOREAD64_CR_REGIO(WM_RESPONSE_FIFO_WL_STATUS);
							}
							/* Raise an error if maximum re-read count is reached. */
							if (i == VHA_PARITY_READ_COUNT_MAX) {
								dev_err(dev, "WM_RESPONSE_FIFO_WL_STATUS register parity error!\n");
								/* Use the real event to indicate the error */
								VHA_REG_SET_PARITY_ERROR(status);
								dev_info(dev, "status: 0x%016llx!\n", status);
							}
						}
#endif
						/* Read RESPONSE_FIFO_WL_ID. */
						wm_cmd_id = IOREAD64_CR_REGIO(WM_RESPONSE_FIFO_WL_ID);
						/* Gather and process perf/stats data. */
						if (WM_DBG_MODE_ON(PERF))
							vha->stats.cnn_last_cycles = IOREAD64_CR_REGIO(WM_RESPONSE_FIFO_WL_PERF);
						if (WM_DBG_MODE_ON(BAND)) {
#define GET_MEM_STAT_TRANS(stat, reg) \
		vha->stats.last_mem_stats.stat##_transactions = \
			IOREAD64_CR_REGIO(WM_RESPONSE_FIFO_WL_BW_##reg)
#define GET_MEM_STAT_WORDS(stat, reg) \
		vha->stats.last_mem_stats.stat##_words = \
			IOREAD64_CR_REGIO(WM_RESPONSE_FIFO_WL_BW_##reg##_WORD)

							GET_MEM_STAT_TRANS(locm_rd,  LOCM_RD);
							GET_MEM_STAT_TRANS(locm_wr,  LOCM_WR);
							GET_MEM_STAT_TRANS(locm_mwr, LOCM_MWR);
							GET_MEM_STAT_TRANS(socm_rd,  SOCM_RD);
							GET_MEM_STAT_TRANS(socm_wr,  SOCM_WR);
							GET_MEM_STAT_TRANS(socm_mwr, SOCM_MWR);
							GET_MEM_STAT_TRANS(ddr_rd,   DDR_RD);
							GET_MEM_STAT_TRANS(ddr_wr,   DDR_WR);
							GET_MEM_STAT_TRANS(ddr_mwr,  DDR_MWR);

							GET_MEM_STAT_WORDS(locm_rd, LOCM_RD);
							GET_MEM_STAT_WORDS(locm_wr, LOCM_WR);
							GET_MEM_STAT_WORDS(socm_rd, SOCM_RD);
							GET_MEM_STAT_WORDS(socm_wr, SOCM_WR);
							GET_MEM_STAT_WORDS(ddr_rd,  DDR_RD);
							GET_MEM_STAT_WORDS(ddr_wr,  DDR_WR);
#undef GET_MEM_STAT_TRANS
#undef GET_MEM_STAT_WORDS
						}
						/* Pop response from RESPONSE_FIFO. */
						IOWRITE64_CR_REGIO(VHA_CR_WM_RESPONSE_FIFO_READ_FIFO_READ_EN,
											WM_RESPONSE_FIFO_READ);
						IOWRITE64_CR_REGIO(VHA_WM_EVENTS_DEFAULT, WM_EVENT_ENABLE);
						VHA_UNLOCK_WM();
						/* Check if id matches the command. */
						if (VHA_CR_GETBITS(WM_RESPONSE_FIFO_WL_ID, WL_ID, wm_cmd_id) !=
															cmd->wm_cmd_id) {
							dev_err(vha->dev, "%s: WM%u WL id mismatch for cmd 0x%08x/%u: "
									"0x%04x vs. 0x%04x\n", __func__, id,
									cmd->user_cmd.cmd_id, cmd->session->id, cmd->wm_cmd_id,
									(uint16_t)VHA_CR_GETBITS(
											WM_RESPONSE_FIFO_WL_ID, WL_ID,
											wm_cmd_id));
							/* Indicate WL id mismatch. */
							VHA_REG_SET_WL_ID_MISMATCH_ERROR(status);
						}
						/* Leave only potential errors. */
						status &= wm_rsp_err_events;
						/* Store the latest error status for potential full_reset. */
						if (status)
							rsp_err_status = status;
#ifdef VHA_SCF
						if (vha->confirm_config_reg)
							vha_handle_conf_status(vha, cmd, &full_reset, &status);

						if (vha->cnn_combined_crc_enable)
							vha_check_crc(vha, cmd, &status);
#endif
						/* Update per core/WM stats. */
						VHA_INC_WL_STAT(vha, kicks_completed, cmd);

						/* Free command resources. */
						if (!vha->hw_sched_status.assignments[cmd->hw_sched_info.assignment_id].queued)
							vha_wm_release_cores(vha, cmd->hw_sched_info.core_mask, false);
						vha_dev_free_cmd_res(vha, cmd, true);

						/* Finally handle the response. */
						vha_handle_wm_response(vha, id, status, irq_status.core_events,
												irq_status.ic_events, &full_reset, &error_flags);

						if (status)
							vha->stats.total_failures++;
						vha->stats.cnn_kicks_completed++;
					} else {
						WARN_ON(1);
					}
				} else {
					/* Ignore or report??? */
				}
			}
		/* If any of processed WLs required full reset, all the WLs being
		 * currently processed need to be failed and rolled back.
		 * The reset itself will be executed at the end of the handler. */
		if (full_reset)
			vha_handle_sys_failure(vha, rsp_err_status, -EIO, error_flags);
	}

	/* Read core event source mask. */
	/* Debug purpose only ... */
	multi_src_mask = (uint32_t)VHA_CR_GETBITS(HOST_EVENT_SOURCE, CORE,
												irq_status.event_source);
	if (multi_src_mask)
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			if (multi_src_mask & (1 << id)) {
				if (irq_status.core_events[id] & VHA_CORE_ERR_EVENTS)
					dev_err(vha->dev, "%s: Core%d error event has been detected: %llx\n",
							__func__, id, irq_status.core_events[id]);
			}
	/* Read IC event source mask. */
	/* Debug purpose only ... */
	multi_src_mask = (uint32_t)VHA_CR_GETBITS(HOST_EVENT_SOURCE, IC,
												irq_status.event_source);
	if (multi_src_mask)
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			if (multi_src_mask & (1 << id)) {
				if (irq_status.ic_events[id] & VHA_IC_ERR_EVENTS)
					dev_err(vha->dev, "%s: Interconnect%d error event has been detected: %llx\n",
							__func__, id, irq_status.ic_events[id]);
			}

calibration_end:
	if (full_reset) {
		dev_err(vha->dev, "Performing full system reset due to HW error detection.");
		/* Stop cores and execute the actual full reset finally. */
		ret = vha_dev_stop(vha, true);
		/* Check queues ... */
		vha_chk_cmd_queues(vha, true);
	} else {
		/* Run in BH context! */
		vha_chk_cmd_queues(vha, false);
	}

#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_IRQ_WORKER)
		current->make_it_fail = false;
#endif
	mutex_unlock(&vha->lock);

	return ret;
}

#ifdef CONFIG_VHA_DUMMY
static int vha_dummy_dev_start(struct vha_dev *vha)
{
	if (vha->state == VHA_STATE_ON)
		return 0; /* not an error */

	vha->state = VHA_STATE_ON;
	/* Remember the time hw is powered on */
	GETNSTIMEOFDAY(&vha->stats.hw_start);
	return 0;
}

static int vha_dummy_dev_stop(struct vha_dev *vha)
{
	uint64_t tmp = 0;
	struct TIMESPEC now;

	if (vha->state == VHA_STATE_OFF)
		return -1;

	vha->state = VHA_STATE_OFF;
	/* Update the up time of the core */
	GETNSTIMEOFDAY(&now);
	if (get_timespan_us(&vha->stats.hw_start, &now, &tmp)) {
		do_div(tmp, 1000UL);
		vha->stats.uptime_ms += tmp;
		if (vha->stats.uptime_ms)
			vha_update_utilization(vha);
		else
			dev_dbg(vha->dev,
				"%s Too short execution time to calculate utilization!\n",
				__func__);
	} else
		WARN_ON(1);

	return 0;
}
#endif

int vha_dev_start(struct vha_dev *vha)
{
	int ret = 0;
	uint8_t core_mask;
	uint8_t active_core_mask = vha->full_core_mask;
	int id;

#if defined(VHA_ENHANCED_APM) && !defined(CONFIG_VHA_DUMMY)
	active_core_mask &= ~vha->hw_sched_status.free_core_mask;
#endif

	if (vha->do_calibration)
		active_core_mask |= VHA_CALIBRATION_CORE_MASK;

	/* If device disabled & no core active */
	if (vha->state == VHA_STATE_OFF && !vha->active_core_mask) {
		pm_runtime_get_sync(vha->dev);
		dev_dbg(vha->dev, "%s system power up\n", __func__);
	}

	/* Cancel any APM request for active cores that are busy at this point */
	{
		/* Find active cores that are busy and under APM */
		uint8_t apm_core_mask = active_core_mask & vha->apm_core_mask;
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if (apm_core_mask & (1 << id))
				cancel_delayed_work(&vha->apm_dworks[id].dwork);
		}
		vha->apm_core_mask &= ~(apm_core_mask);
	}

	/* Find cores that have to be powered on */
	core_mask = (vha->active_core_mask ^ active_core_mask) &
			~vha->active_core_mask;
	if (core_mask) {
		dev_dbg(vha->dev, "%s core mask:%#x  (%#x -> %#x)\n",
				__func__, core_mask, vha->active_core_mask, active_core_mask);

		/////////////// POWER ON //////////////////////////
		img_pdump_printf("-- POWER_ON_BEGIN\n");
		/* Prepare device cores ...  */
		ret = vha_dev_prepare_cores(vha, core_mask);
		if (ret) {
			dev_err(vha->dev, "%s: Error preparing device cores!\n", __func__);
			goto error;
		}
		/* Enable device cores clocks */
		vha_dev_enable_clocks(vha, core_mask);
		/* Reset device cores & system for the very first time */
		ret = vha_dev_reset(vha, core_mask,
				vha->active_core_mask ? false : true);
		if (ret){
			dev_err(vha->dev, "%s: Error reseting device cores!\n", __func__);
			goto error;
		}
		/* Enable device cores clocks */
		vha_dev_enable_clocks(vha,  core_mask);
		img_pdump_printf("-- POWER_ON_END\n");
		/////////////////////////////////////////////////////

		vha_dev_ready(vha, core_mask,
				vha->active_core_mask ? false : true);

		/* Store actual status about active cores */
		vha->active_core_mask = active_core_mask;
	}

	if (vha->state == VHA_STATE_OFF) {
		/* Call device specific setup */
		vha_dev_setup(vha);
		/* Remember the time device is powered on */
		GETNSTIMEOFDAY(&vha->stats.hw_start);

		vha->state = VHA_STATE_ON;
#ifdef VHA_SCF
		/* Start the SW watchdog */
		vha_start_swd(vha, 0);
#endif
	}

	return 0;
error:
	pm_runtime_put_sync(vha->dev);
	vha->state = VHA_STATE_OFF;
	vha->active_core_mask = 0;
	return ret;
}

static int vha_dev_stop_cores(struct vha_dev *vha, uint8_t core_mask, bool reset)
{
	int ret = 0;

	if (core_mask) {
		/* Store actual status about active cores */
		vha->active_core_mask &= ~core_mask;

		/* Disable events at first */
		vha_dev_disable_events(vha, core_mask,
				vha->active_core_mask ? false : true);

		/////////////// POWER_OFF //////////////////////////
		img_pdump_printf("-- POWER_OFF_BEGIN\n");
		/* Reset core in case of error or pending inference */
		if (reset) {
			ret = vha_dev_reset(vha, core_mask,
					vha->active_core_mask ? false : true);
			if(ret)
				dev_warn(vha->dev,
						"%s: Problem with resetting device cores!\n",
						__func__);
		}

		/* Disable device clocks */
		ret = vha_dev_disable_clocks(vha, core_mask,
				vha->active_core_mask ? false : true);
		if(ret)
			dev_warn(vha->dev,
					"%s: Problem with disabling clocks for cores!\n",
					__func__);

		/* Execute any outstanding routines to flush the device cores */
		ret = vha_dev_flush_cores(vha, core_mask);
		if(ret)
			dev_warn(vha->dev,
					"%s: Problem with flushing device cores!\n",
					__func__);
		img_pdump_printf("-- POWER_OFF_END\n");
		/////////////////////////////////////////////////////
	}

	/* If device enabled & no core active */
	if (vha->state == VHA_STATE_ON && !vha->active_core_mask) {
		int id;

		/* Cancel APM requests if we are about to power off the device */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			cancel_delayed_work(&vha->apm_dworks[id].dwork);
		vha->apm_core_mask = 0;

		dev_dbg(vha->dev, "%s system power down\n", __func__);

		vha->state = VHA_STATE_OFF;

		/* Update the up time of the device */
		if (!vha->do_calibration) {
			uint64_t tmp = 0;
			struct TIMESPEC now;
			GETNSTIMEOFDAY(&now);
			if (get_timespan_us(&vha->stats.hw_start, &now, &tmp)) {
				do_div(tmp, 1000UL);
				vha->stats.uptime_ms += tmp;
				if (vha->stats.uptime_ms)
					vha_update_utilization(vha);
				else
					dev_dbg(vha->dev,
							"%s Too short execution time to calculate utilization!\n",
							__func__);
			} else
				WARN_ON(1);
		}

		vha->active_mmu_ctx = VHA_INVALID_ID;

		spin_lock_irq(&vha->irq_lock);
		memset(&vha->irq_status, 0, sizeof(vha->irq_status));
		spin_unlock_irq(&vha->irq_lock);

		if (reset) {
			pm_runtime_mark_last_busy(vha->dev);
			pm_runtime_put_sync_autosuspend(vha->dev);
		} else {
			pm_runtime_put_sync(vha->dev);
		}
	}

	return ret;
}

int vha_dev_stop(struct vha_dev *vha, bool reset)
{
	int ret = 0;
	uint8_t active_core_mask = 0;
	uint8_t core_mask;

#if defined(VHA_ENHANCED_APM) && !defined(CONFIG_VHA_DUMMY)
	active_core_mask = vha->full_core_mask &
			~vha->hw_sched_status.free_core_mask;
#endif

	if (vha->do_calibration)
		active_core_mask &= ~VHA_CALIBRATION_CORE_MASK;

	/* Find cores that have to be powered off */
	core_mask = (vha->active_core_mask ^ active_core_mask) &
			vha->active_core_mask;

	if (core_mask)
		dev_dbg(vha->dev, "%s core mask:%#x  (%#x -> %#x)\n",
				__func__, core_mask, vha->active_core_mask, active_core_mask);

	ret = vha_dev_stop_cores(vha, core_mask, reset);

	return ret;
}

static bool vha_is_mmu_ctx_shared(struct vha_cmd *cmd)
{
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;

	/* If the session of the command we are trying to execute shares
	 * the hw mmu ctx with different session */
	if (vha->mmu_ctxs[session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id] > 1) {
		uint8_t id;

		/* Check currently processed commands */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			/* Check if the mmu hw context is same as current command */
			if (vha->pendcmd[id].cmd != NULL &&
					vha->pendcmd[id].cmd->session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id ==
							session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id)
				return true;
		}
	}

	return false;
}

int vha_dev_schedule_cmd(struct vha_dev *vha, struct vha_cmd *cmd)
{
	struct vha_hw_sched_status *status = &vha->hw_sched_status;
	struct vha_hw_sched_info *info;
	struct vha_user_cnn_submit_multi_cmd* user_cmd =
			(struct vha_user_cnn_submit_multi_cmd*)&cmd->user_cmd;
	uint8_t wm_id;
	uint8_t core_id = 0;
	uint8_t core_mask = 0;
	uint8_t assignment_id;
	uint8_t i;

	/* If no command provided, just check if anything can potentially
	 * be scheduled. */
	if (cmd == NULL) {
		/* Calculate the number of cores in use. */
		uint8_t num_used_cores = 0;
		uint8_t assignment_id;
		for (assignment_id = 0; assignment_id < VHA_NUM_CORES; assignment_id++)
			if (status->assignments[assignment_id].core_mask) {
				uint8_t num_cores = VHA_CORE_MASK_TO_NUM(status->assignments[assignment_id].core_mask);
				num_used_cores += num_cores;
				if (vha->low_latency != VHA_LL_DISABLED)
					if (status->assignments[assignment_id].queued)
						num_used_cores += num_cores;
			}
		/* If all the cores are in use, nothing can be scheduled. */
		if (num_used_cores ==
				(vha->hw_props.num_cnn_core_devs * ((vha->low_latency != VHA_LL_DISABLED) ? 2 : 1)))
			return -1;
		return 0;
	}

	if  (cmd->user_cmd.cmd_type != VHA_CMD_CNN_SUBMIT_MULTI)
		return 0;

#define VHA_LL_BRANCH(l) \
		{ \
			if (vha->low_latency == VHA_LL_DISABLED) \
				return -1; \
			else \
				goto l; \
		}

	/* Check for shared mmu hardware context, as we can't schedule command
	 * on free cores, while other currently processing cores use the same
	 * mmu hw context, because data would be overwritten */
	if (vha_is_mmu_ctx_shared(cmd)) {
		dev_dbg(vha->dev, "%s: Postpone command due to shared mmu context!\n",
				__func__);
		return -1;
	}

	info = &cmd->hw_sched_info;
	/* If external scheduling is requested... */
	if (vha->scheduling_sequence_len > 0) {
		uint8_t wm_mask;
		/* Queueing is not supported for external scheduling. */
		if (status->num_cores_free < user_cmd->num_cores)
			return -1;
		/* Read scheduling data for this workload from scheduling sequence. */
		wm_id = SCHED_SEQ_GET_WM(vha->scheduling_counter);
		wm_mask = VHA_WM_ID_TO_MASK(wm_id);
		core_mask = SCHED_SEQ_GET_CORES(vha->scheduling_counter);
		/* Sanity check the data. */
		if (((status->free_wm_mask & wm_mask) == 0) ||
			((status->free_core_mask & core_mask) != core_mask))
			return -1;
		status->free_core_mask &= ~(core_mask);
		/* Increment scheduling counter. */
		vha->scheduling_counter =
				(vha->scheduling_counter + 1) %
					vha->scheduling_sequence_len;
	} else {
		/* Check if there are cores available. */
		if (status->num_cores_free < user_cmd->num_cores)
			VHA_LL_BRANCH(attempt_to_queue_multi);
		/* Check if there is a WM available. */
		if (status->num_wms_free == 0)
			VHA_LL_BRANCH(attempt_to_queue_multi);
		/* Find a free WM. */
		wm_id = ffs(status->free_wm_mask) - 1;
		/*  Check if enough cores are available. */
		if (user_cmd->num_cores > status->num_cores_free)
			VHA_LL_BRANCH(attempt_to_queue_multi);
		/* Find the required number of free cores. */
		for (i = 0; i < user_cmd->num_cores; i++) {
			core_id = ffs(status->free_core_mask) - 1;
			core_mask |= VHA_CORE_ID_TO_MASK(core_id);
			status->free_core_mask &= ~(core_mask);
		}
	}

	/* Update resource status. */
	for (assignment_id = 0; assignment_id < VHA_NUM_CORES; assignment_id++)
		if (status->assignments[assignment_id].core_mask == 0)
			break;
	if (assignment_id == VHA_NUM_CORES) {
		dev_info(vha->dev, "%s: Scheduling data inconsistency detected!\n", __func__);
		return -1;
	}
	status->assignments[assignment_id].assignment_id = assignment_id;
	status->assignments[assignment_id].wm_id = wm_id;
	status->assignments[assignment_id].core_mask = core_mask;
	status->num_cores_free -= user_cmd->num_cores;
	status->num_wms_free--;
	status->free_wm_mask &= ~(VHA_WM_ID_TO_MASK(wm_id));

	/* Store command scheduling info. */
	*info = status->assignments[assignment_id];

	goto skip_label_attempt_to_queue_multi;

attempt_to_queue_multi:
	/* Check if there is an assignment matching this scheduling request
	 * available in the list of assignments. */
	for (assignment_id = 0; assignment_id < VHA_NUM_CORES; assignment_id++)
		/* If this assignment is not queued already and it has the same
		 * number of cores as this scheduling request... */
		if (!status->assignments[assignment_id].queued &&
				status->assignments[assignment_id].core_mask &&
				(VHA_CORE_MASK_TO_NUM(status->assignments[assignment_id].core_mask) ==
																user_cmd->num_cores)) {
			wm_id = status->assignments[assignment_id].wm_id;
			if (vha->low_latency == VHA_LL_SELF_KICK
					/* If the current command we are trying to queue belongs
					 * to a different session than the pending one. */
					&& (vha->pendcmd[wm_id].cmd != NULL &&
						vha->pendcmd[wm_id].cmd->session != cmd->session)
					/* If the session of the command we are trying to queue shares
					 * the hw mmu ctx with the session of pending cmd */
					&& (cmd->session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id ==
							vha->pendcmd[wm_id].cmd->session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id)
					/* Sanity if hw mmu ctx is really shared at this point. */
					&& (vha->mmu_ctxs[cmd->session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id] > 1)
				) {
				/* Skip this assignment. */
				continue;
			}
			/* Make the assignment queued. */
			status->assignments[assignment_id].queued = true;
			/* Store command scheduling info. */
			*info = status->assignments[assignment_id];
			break;
		}
	/* Fail if no matching assignments found. */
	if (assignment_id == VHA_NUM_CORES)
		return -1;

skip_label_attempt_to_queue_multi:

	/* For hw commands... */
	if (CMD_EXEC_ON_HW(cmd)) {
		if (!VHA_IS_DUMMY(vha)) {
			int tries = 3; /* magic number, just try harder to start the device */
			/* Start device. */
			while(tries--) {
				if (vha_dev_start(vha))
					dev_warn(vha->dev, "%s: Error starting device cores. Try once again.", __func__);
				else
					break;
			}
		}
#ifdef CONFIG_VHA_DUMMY
		else
			vha_dummy_dev_start(vha);
#endif
	}

#undef VHA_LL_BRANCH

	dev_dbg(vha->dev, "%s: cmd 0x%08x/%u scheduled on WM%u/core(s) 0x%02x\n",
			__func__, cmd->user_cmd.cmd_id, cmd->session->id,
			info->wm_id, info->core_mask);
	return 0;
}

void vha_dev_free_cmd_res(struct vha_dev *vha, struct vha_cmd *cmd, bool update_stats)
{
	struct vha_hw_sched_status *status = &vha->hw_sched_status;
	struct vha_hw_sched_info *info = &cmd->hw_sched_info;
	struct vha_user_cnn_submit_multi_cmd* user_cmd =
		(struct vha_user_cnn_submit_multi_cmd*)&cmd->user_cmd;

	if (update_stats) {
		uint64_t proc_time = 0;
		struct TIMESPEC *from = &cmd->hw_proc_start;
		struct TIMESPEC *to = &vha->stats.wm_stats[info->wm_id].hw_proc_end;

		if (TIMESPEC_COMPARE(&vha->stats.wm_stats[info->wm_id].hw_proc_end_prev,
								&cmd->hw_proc_start) >= 0)
			from = &vha->stats.wm_stats[info->wm_id].hw_proc_end_prev;

		if (get_timespan_us(from, to, &proc_time)) {
			vha->stats.last_proc_us = proc_time;
		} else {
			vha->stats.last_proc_us = 0;
		}
		/* Update WL stats. */
		VHA_UPDATE_WL_STAT(vha, total_proc_us, cmd, vha->stats.last_proc_us);
		/* Update common stats. */
		vha_cnn_update_stats(vha);
	}


	/* If assignment for this workload is queued... */
	if (status->assignments[info->assignment_id].queued) {
		/* Just mark it as not queued again. */
		status->assignments[info->assignment_id].queued = false;
		/* Clear scheduling info for this workload. */
		info->freed = true;
		/* Do not update the scheduling status. */
		return;
	}

	/* Update the scheduling status. */
	status->num_cores_free += user_cmd->num_cores;
	status->free_core_mask |= info->core_mask;
	status->num_wms_free++;
	status->free_wm_mask |= VHA_WM_ID_TO_MASK(info->wm_id);
	/* Clear the assignment and scheduling info for this workload. */
	memset(&status->assignments[info->assignment_id], 0,
			sizeof(struct vha_hw_sched_info));
	info->freed = true;
}

static void sched_apm_multi(struct vha_dev *vha)
{
	struct vha_apm_work *apm_work = NULL;
	/* Find active cores that are free and not under APM */
	uint8_t apm_core_mask = vha->active_core_mask &
			vha->hw_sched_status.free_core_mask &
			~vha->apm_core_mask;
	int id;

	/* Skip if nothing has changed */
	if (!apm_core_mask)
		return;

	dev_dbg(vha->dev, "%s core mask:%#x\n", __func__, apm_core_mask);

	/* Schedule for all cores, separately  */
	for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
		if (apm_core_mask & (1 << id)) {
			apm_work = &vha->apm_dworks[id];
			apm_work->core_mask = 1 << id;
			apm_work->delay_ms = vha->pm_delay;
			vha_sched_apm(vha, apm_work);
		}
	}

	/* Record actual status */
	vha->apm_core_mask |= apm_core_mask;
}
//#define MTSTK_MEASURE_MULTI_PRI
#ifdef MTSTK_MEASURE_MULTI_PRI
static uint8_t get_active_pri_count(struct vha_dev *vha)
{
	uint8_t pris;
	uint8_t pri_count = 0;

	/* Calculate the number of priority levels with anything to schedule. */
	for (pris = 0; pris < VHA_MAX_PRIORITIES; pris++)
		if (vha->pri_q_counters[pris] > 0) {
			pri_count++;
		}

	return pri_count;
}
#endif

static void vha_get_time_span(struct TIMESPEC *start, struct TIMESPEC *end,
	struct TIMESPEC *span)
{
	if (start == NULL || end == NULL || span == NULL)
		return;

	/* Calculate the seconds span. */
	span->tv_sec = end->tv_sec - start->tv_sec;
	/* If there more than a second span, move one second to nanoseconds
	 * to avoid potential negative nanosecond values. */
	if (span->tv_sec > 0) {
		span->tv_sec--;
		end->tv_nsec += 1000000000;
	}
	/* Calculate the nanoseconds span. */
	span->tv_nsec = end->tv_nsec - start->tv_nsec;
	/* If nanoseconds include a second, move it to seconds. */
	if (span->tv_nsec > 1000000000) {
		span->tv_sec++;
		span->tv_nsec -= 1000000000;
	}
}

static uint8_t vha_scheduler_get_priority(struct vha_dev *vha)
{
	uint8_t ret_pri = 0;
	uint8_t pris;
	uint8_t pri_count = 0;
	uint32_t curr_window = 0;
	uint32_t curr_limit = 0;
	uint32_t rand_val;

	/* Calculate current total window width. */
	for (pris = 0; pris < VHA_MAX_PRIORITIES; pris++)
		if (vha->pri_q_counters[pris] > 0) {
			curr_window += pri_windows[pris];
			ret_pri = pris;
			pri_count++;
		}

	/* If there's no priority with WLs to schedule, just return 0. */
	if (pri_count == 0)
		return VHA_INVALID_PRI;

	/* If there's only one priority with WLs to schedule, just return it. */
	if (pri_count == 1)
		return ret_pri;

	/* If starvation avoidance is disabled, just return the highest priority
	 *  with WLs to schedule. */
	if (curr_window == 0)
		return ret_pri;

	/* If starvation avoidance is enabled, use 'lottery' based approach. */

	/* Generate random value within the current window. */
	vha_mt19937_gen_range(vha->hw_sched_status.sched_data->rand_gen_handle,
							0, curr_window, &rand_val);

	/* Choose priority based on the value generated and available priorities. */
	for (pris = 0; pris < VHA_MAX_PRIORITIES; pris++)
		if (vha->pri_q_counters[pris] > 0) {
			curr_limit += pri_windows[pris];
			if (rand_val <= curr_limit) {
				ret_pri = pris;
				break;
			}
		}

	return ret_pri;
}

static void vha_scheduler_set_starting_session(struct vha_dev *vha,
	uint8_t priority, struct vha_session *session)
{
	/* Set a starting point session for next scheduling round. */
	if (session != list_entry(&vha->sched_sessions[priority],
								struct vha_session, sched_list[priority]))
		while(list_first_entry(&vha->sched_sessions[priority],
								struct vha_session, sched_list[priority]) != session)
			list_rotate_left(&vha->sched_sessions[priority]);
}

void vha_scheduler_loop(struct vha_dev *vha)
{
	struct vha_cmd *cmd, *tmp;
	struct vha_session *session = NULL;
	bool scheduled = false;
	enum do_cmd_status cmd_status = CMD_OK;
	uint8_t current_pri = VHA_DEFAULT_PRI;

	bool log_pri_sched_info = true;

	if (vha_dev_schedule_cmd(vha, NULL) != 0) {
		/* Postpone worker task if nothing can be scheduled. */
		dev_dbg(vha->dev, "%s Nothing can be scheduled at the moment. "
				"Postpone worker task!\n", __func__);
		return;
	}

#ifdef MTSTK_MEASURE_MULTI_PRI
	log_pri_sched_info = (get_active_pri_count(vha) > 1) ? true : false;
#endif

	/* Main scheduling loop. */
	do {
		scheduled = false;
		current_pri = vha_scheduler_get_priority(vha);
		if (current_pri == VHA_INVALID_PRI)
			break;
		list_for_each_entry(session, &vha->sched_sessions[current_pri], sched_list[current_pri]) {
			list_for_each_entry_safe(cmd, tmp, &session->cmds[current_pri], list[current_pri]) {
#if defined(VHA_ENHANCED_APM)
				/* Schedule APM/power down cores in the middle if possible */
				if (!VHA_IS_DUMMY(vha)) {
					if (!vha->no_clock_disable) {
						if (!vha->pm_delay) {
							if (vha_dev_stop(vha, false)) {
								dev_warn(vha->dev, "%s: Failed to soft stop device. Trying harder with reset",
											__func__);
								if (vha_dev_stop(vha, true))
									dev_err(vha->dev, "%s: Failed to stop device with reset!", __func__);
							}
						} else
							sched_apm_multi(vha);
					}
				}
#endif
				/* Skip this workload as it's already scheduled. */
				if (cmd->hw_sched_info.core_mask && !cmd->hw_sched_info.freed)
					continue;

				/* Attempt to schedule command for execution. */
				cmd_status = vha_do_cmd(cmd);

				if ((cmd_status == CMD_OK) || (cmd_status == CMD_HW_BUSY)) {
					if (cmd_status == CMD_OK) {
						scheduled = true;
						if (log_pri_sched_info && !cmd->rolled_back) {
							struct TIMESPEC sched_ts, sched_span = {0};
							GETNSTIMEOFDAY(&sched_ts);
							vha_get_time_span(&cmd->submit_ts, &sched_ts, &sched_span);

#ifdef LOG_PRI_SCHEDULING_INFO
							dev_info(vha->dev, "@@@ scheduled 0x%08x/%u/%u, span: %llu\n",
									cmd->user_cmd.cmd_id, session->id, cmd->user_cmd.priority,
									(uint64_t)sched_span.tv_sec * 1000000000ULL +
																(uint64_t)sched_span.tv_nsec);
#endif

							VHA_UPDATE_SCHED_STAT_MTSTK(vha, cmd, &sched_span);
						}
						session = list_next_entry(session, sched_list[current_pri]);
					}
					vha_scheduler_set_starting_session(vha, current_pri, session);
					goto exit_session_loop;
				}
			}
		}
exit_session_loop:;
	/* Iterate until a workload was scheduled and no other can be scheduled. */
	} while (vha_dev_schedule_cmd(vha, NULL) == 0 && scheduled);

	/* Schedule APM/power down cores if possible at end */
	if (!VHA_IS_DUMMY(vha)) {
		bool skip = vha->no_clock_disable;
#if !defined(VHA_ENHANCED_APM)
		skip |= vha_is_busy(vha);
#endif
		if (!skip) {
			if (!vha->pm_delay) {
				if (vha_dev_stop(vha, false)) {
					dev_warn(vha->dev, "%s: Failed to soft stop device. Trying harder with reset",
							__func__);
					if (vha_dev_stop(vha, true))
						dev_err(vha->dev, "%s: Failed to stop device with reset!", __func__);
				}
			} else
				sched_apm_multi(vha);
		}
	}
#ifdef CONFIG_VHA_DUMMY
	else if (!vha_is_busy(vha))
		vha_dummy_dev_stop(vha);
#endif
}

bool vha_rm_session_cmds(struct vha_session *session)
{
	struct vha_dev *vha = session->vha;
	bool reschedule = false;
	uint32_t wm_id;
	struct vha_hw_sched_info sched_info = {0};
	struct vha_cmd *cur_cmd, *tmp_cmd;
	uint8_t pri;

	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		bool pend_removed = false;
		bool queued_removed = false;

		/* Check if pend/queued WLs will be removed. */
		if (vha->pendcmd[wm_id].cmd &&
				vha->pendcmd[wm_id].cmd->session == session) {
			dev_warn(vha->dev,
					"Removing a session while cnn cmd is still pending\n");
			pend_removed = true;
			sched_info = vha->pendcmd[wm_id].cmd->hw_sched_info;
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
			cancel_delayed_work(&vha->dummy_dworks[wm_id].dummy_dwork);
#endif
		}
		if (vha->queuedcmd[wm_id].cmd &&
				vha->queuedcmd[wm_id].cmd->session == session) {
			dev_warn(vha->dev,
					"Removing a session while cnn cmd is still queued\n");
			queued_removed = true;
			sched_info = vha->queuedcmd[wm_id].cmd->hw_sched_info;
		}

		/* Update session scheduling. */
		if (vha->queuedcmd[wm_id].cmd &&
				(pend_removed && !queued_removed)) {
			if (vha->queuedcmd[wm_id].cmd->session !=
					list_entry(&vha->sched_sessions[vha->queuedcmd[wm_id].cmd->user_cmd.priority],
							struct vha_session, sched_list[vha->queuedcmd[wm_id].cmd->user_cmd.priority]))
				while(list_first_entry(&vha->sched_sessions[vha->queuedcmd[wm_id].cmd->user_cmd.priority],
						struct vha_session, sched_list[vha->queuedcmd[wm_id].cmd->user_cmd.priority]) !=
											vha->queuedcmd[wm_id].cmd->session)
					list_rotate_left(&vha->sched_sessions[vha->queuedcmd[wm_id].cmd->user_cmd.priority]);
		}

		/* Remove pend/queued WLs if needed. */
		if (pend_removed || queued_removed) {
			uint64_t wm_mask = VHA_CR_SETBITS(HOST_EVENT_SOURCE, WM, VHA_WM_ID_TO_MASK(wm_id));
			/* Reset WM/cores. */
			vha_wm_reset(vha, &sched_info);
			VHA_LOCK_WM();
			VHA_SELECT_WM(wm_id);
			/* Remove WM related interrupt info if it happens to be set. */
			if (vha->irq_status.event_source & wm_mask)
			{
				/* Unset the WM related source bit. */
				vha->irq_status.event_source &= ~wm_mask;
				/* Clear all WM related events. */
				IOWRITE64_CR_REGIO(vha->irq_status.wm_events[wm_id] & VHA_WM_EVENTS_DEFAULT,
									WM_EVENT_CLEAR);
				vha->irq_status.wm_events[wm_id] = 0ULL;
			}
			/* Re-enable WM events here as this WM will not be handled further. */
			IOWRITE64_CR_REGIO(VHA_WM_EVENTS_DEFAULT, WM_EVENT_ENABLE);
			VHA_UNLOCK_WM();
			/* Rollback all WLs from this WM. */
			vha_rollback_wm_cmds(vha, wm_id, true);
			/* Need to reschedule too. */
			reschedule = true;
		}
	}

	/* Remove session related commands. */
	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++) {
		list_for_each_entry_safe(cur_cmd, tmp_cmd, &session->cmds[pri], list[pri]) {
			/* rsp didn't make it to rsps list, free it now */
			kfree(cur_cmd->rsp);

			list_del(&cur_cmd->list[cur_cmd->user_cmd.priority]);
			vha->pri_q_counters[cur_cmd->user_cmd.priority]--;
			kfree(cur_cmd);
		}
	}

	return reschedule;
}

bool vha_rm_session_cmds_masked(struct vha_session *session, uint32_t cmd_id,
		uint32_t cmd_id_mask)
{
	struct vha_dev *vha = session->vha;
	bool reschedule = false;
	uint32_t wm_id;
	struct vha_hw_sched_info sched_info = {0};

	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		bool pend_removed = false;
		bool queued_removed = false;

		/* Check if pend/queued WLs will be removed. */
		if (vha->pendcmd[wm_id].cmd &&
				(vha->pendcmd[wm_id].cmd->session == session) &&
				(vha->pendcmd[wm_id].cmd->user_cmd.cmd_id & cmd_id_mask)
																	== cmd_id) {
			pend_removed = true;
			sched_info = vha->pendcmd[wm_id].cmd->hw_sched_info;
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
			cancel_delayed_work(&vha->dummy_dworks[wm_id].dummy_dwork);
#endif
			VHA_INC_WL_STAT(vha, kicks_cancelled, vha->pendcmd[wm_id].cmd);
			vha->stats.cnn_kicks_cancelled++;
		}
		if (vha->queuedcmd[wm_id].cmd &&
				(vha->queuedcmd[wm_id].cmd->session == session) &&
				(vha->queuedcmd[wm_id].cmd->user_cmd.cmd_id & cmd_id_mask)
																	== cmd_id) {
			sched_info = vha->queuedcmd[wm_id].cmd->hw_sched_info;
			queued_removed = true;
			if (vha->low_latency == VHA_LL_SELF_KICK) {
				VHA_INC_WL_STAT(vha, kicks_cancelled, vha->queuedcmd[wm_id].cmd);
				vha->stats.cnn_kicks_cancelled++;
			}
		}

		/* Update session scheduling. */
		if (vha->queuedcmd[wm_id].cmd &&
				(pend_removed && !queued_removed)) {
			if (vha->queuedcmd[wm_id].cmd->session !=
						list_entry(&vha->sched_sessions[vha->queuedcmd[wm_id].cmd->user_cmd.priority],
								struct vha_session, sched_list[vha->queuedcmd[wm_id].cmd->user_cmd.priority]))
				while(list_first_entry(&vha->sched_sessions[vha->queuedcmd[wm_id].cmd->user_cmd.priority],
						struct vha_session, sched_list[vha->queuedcmd[wm_id].cmd->user_cmd.priority]) !=
								vha->queuedcmd[wm_id].cmd->session)
					list_rotate_left(&vha->sched_sessions[vha->queuedcmd[wm_id].cmd->user_cmd.priority]);
		}

		/* Remove pend/queued WLs if needed. */
		if (pend_removed || queued_removed) {
			uint64_t wm_mask = VHA_CR_SETBITS(HOST_EVENT_SOURCE, WM, VHA_WM_ID_TO_MASK(wm_id));
			/* Reset WM/cores. */
			vha_wm_reset(vha, &sched_info);
			VHA_LOCK_WM();
			VHA_SELECT_WM(wm_id);
			/* Remove WM related interrupt info if it happens to be set. */
			if (vha->irq_status.event_source & wm_mask)
			{
				/* Unset the WM related source bit. */
				vha->irq_status.event_source &= ~wm_mask;
				/* Clear all WM related events. */
				IOWRITE64_CR_REGIO(vha->irq_status.wm_events[wm_id] & VHA_WM_EVENTS_DEFAULT,
									WM_EVENT_CLEAR);
				vha->irq_status.wm_events[wm_id] = 0ULL;
			}
			/* Re-enable WM events here as this WM will not be handled further. */
			IOWRITE64_CR_REGIO(VHA_WM_EVENTS_DEFAULT, WM_EVENT_ENABLE);
			VHA_UNLOCK_WM();
			/* Rollback all WLs from this WM. */
			vha_rollback_wm_cmds(vha, wm_id, true);
			/* Correct aborted stats. */
			if (queued_removed) {
				VHA_UPDATE_WM_STAT(vha, kicks_aborted, sched_info.wm_id, -1);
				VHA_UPDATE_CORE_GROUP_STAT(vha, kicks_aborted, sched_info.core_mask, -1);
				vha->stats.cnn_kicks_aborted--;
			}
			if (pend_removed) {
				VHA_UPDATE_WM_STAT(vha, kicks_aborted, sched_info.wm_id, -1);
				VHA_UPDATE_CORE_GROUP_STAT(vha, kicks_aborted, sched_info.core_mask, -1);
				vha->stats.cnn_kicks_aborted--;
			}
			reschedule = true;
		}
	}

	return reschedule;
}

int vha_rm_cmds(struct vha_session *session, uint32_t cmd_id,
		uint32_t cmd_id_mask, bool respond)
{
	struct vha_dev *vha = session->vha;
	struct vha_cmd *cur_cmd, *tmp_cmd;
	struct vha_rsp *cur_rsp, *tmp_rsp;
	bool reschedule = false;
	bool respond_aux = false;
	int ret = 0;
	uint8_t pri;

	mutex_lock(&vha->lock);

	/* Remove pend/queued session commands that match the cmd_id. */
	reschedule = vha_rm_session_cmds_masked(session, cmd_id, cmd_id_mask);

	/* Remove session related commands matching command id template. */
	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++) {
		list_for_each_entry_safe(cur_cmd, tmp_cmd, &session->cmds[pri], list[pri]) {
			if ((cur_cmd->user_cmd.cmd_id & cmd_id_mask) == cmd_id) {

#ifdef KERNEL_DMA_FENCE_SUPPORT
				switch (cur_cmd->user_cmd.cmd_type)
				{
				case VHA_CMD_CNN_SUBMIT_MULTI:
				{
					struct vha_user_cnn_submit_multi_cmd *cnn_cmd =
							(struct vha_user_cnn_submit_multi_cmd *)&cur_cmd->user_cmd;
					int j;
					for (j = 0; j < (cnn_cmd->msg.num_bufs - cnn_cmd->num_cores); j++) {
						struct vha_buffer *buf = vha_find_bufid(session, cnn_cmd->bufs[j]);
						if (buf == NULL) {
							dev_warn(vha->dev, "%s: could not find buf %x\n", __func__,
											cnn_cmd->bufs[j]);
						} else {
							vha_rm_buf_fence(session, buf);
						}
					}
					break;
				}
				default:
					dev_warn(vha->dev, "%s: invalid cmd type %x\n", __func__,
								cur_cmd->user_cmd.cmd_type);
					break;
				}
#endif

				/* rsp didn't make it to rsps list; free it now. */
				kfree(cur_cmd->rsp);

				list_del(&cur_cmd->list[cur_cmd->user_cmd.priority]);
				vha->pri_q_counters[cur_cmd->user_cmd.priority]--;
				kfree(cur_cmd);

				/* There were commands matching command id template in the list,
				 * so respond to wake user space. */
				respond_aux = true;
			}
		}
	}

	/* Remove responses for session related commands
	 * matching command id template. */
	list_for_each_entry_safe(cur_rsp, tmp_rsp, &session->rsps, list) {
		if ((cur_rsp->user_rsp.cmd_id & cmd_id_mask) == cmd_id) {
			list_del(&cur_rsp->list);
			kfree(cur_rsp);
			respond_aux = true;
		}
	}

	/* Reset hardware if required. */
	if (reschedule)
		ret = vha_dev_stop(vha, reschedule);

	/* Generate "cancel" response if any commands matching command id template
	 * were removed. */
	if (respond_aux && respond) {
		/* Calculate space for the response. */
		size_t sz = sizeof(struct vha_rsp)
			+ sizeof(struct vha_user_cnn_submit_rsp)
			- sizeof(struct vha_user_rsp);
		/* Allocate space for standard response. */
		struct vha_rsp *rsp = kzalloc(sz, GFP_KERNEL);
		if (rsp == NULL) {
			dev_crit(session->vha->dev,
					"Failed to allocate memory to notify cancel for cmds 0x%08x\n", cmd_id);
			session->oom = true;
		} else {
			rsp->size = sizeof(struct vha_user_cnn_submit_rsp);
			rsp->user_rsp.cmd_id = cmd_id;
			list_add_tail(&rsp->list, &session->rsps);
		}
		wake_up(&session->wq);
	}

	mutex_unlock(&vha->lock);

	/* Just return in case of oom. */
	if (session->oom)
		return -ENOMEM;

	/* Reschedule once all commands matching command id template are removed. */
	if (reschedule)
		vha_chk_cmd_queues(vha, true);

	return ret;
}

bool vha_is_busy(struct vha_dev *vha)
{
	return (vha->hw_sched_status.num_cores_free < vha->hw_props.num_cnn_core_devs);
}

/* check all input buffers are filled and ready to go */
bool vha_is_waiting_for_inputs(struct vha_session *session,
	struct vha_cmd *cmd)
{
	if (!cmd->inbufs_ready) {
		const struct vha_user_cnn_submit_multi_cmd *user_cmd =
			(struct vha_user_cnn_submit_multi_cmd *)&cmd->user_cmd;
		int i;

		for (i = 0; i < cmd->user_cmd.num_inbufs - user_cmd->num_cores; i++) {
			struct vha_buffer *buf = vha_find_bufid(session, user_cmd->bufs[i]);

			if (buf && buf->status == VHA_BUF_UNFILLED) {
				dev_dbg(session->vha->dev,
					"%s: cmd %u waiting for input "
					"buf %d to be ready\n",
					__func__,
					cmd->user_cmd.cmd_id,
					buf->id);
				return true;
			}
		}
	}

	cmd->inbufs_ready = true;
	return false;
}

void vha_dev_apm_stop(struct vha_dev *vha, struct vha_apm_work *apm_work)
{
	/* Find active cores that are not busy and under APM for this apm request */
	uint8_t apm_core_mask = vha->active_core_mask &
			vha->hw_sched_status.free_core_mask &
			vha->apm_core_mask &
			apm_work->core_mask;

	vha->apm_core_mask &= ~apm_core_mask;

	if (vha->do_calibration)
		return;

	if (apm_core_mask) {
		dev_dbg(vha->dev, "%s core mask:%#x\n", __func__, apm_core_mask);
		if (vha_dev_stop_cores(vha, apm_core_mask, false)) {
			dev_warn(vha->dev, "%s: Failed to soft stop cores. Trying harder with reset",
				__func__);
			if (vha_dev_stop_cores(vha, apm_core_mask, true))
				dev_err(vha->dev, "%s: Failed to stop cores with reset!", __func__);
		}
	}
}

int vha_dev_get_props(struct vha_dev *vha, uint32_t onchipmem_size)
{
	struct vha_hw_props *props = &vha->hw_props;
	uint64_t ip_config;
	uint32_t locm_size_kb = 0;
	uint32_t socm_size_kb = 0;
	uint8_t socm_num_sb, socm_num_ba, socm_num_bg;
	uint8_t ext_mem_bus_width;

	memset(props, 0, sizeof(*props));

#ifdef CONFIG_VHA_DUMMY
	/* Note: dummy dev always reads zeroes from registers */
	props->product_id = 0x8070605040302010ULL;
	props->core_id = (long)HW_SERIES << (int)VHA_CR_CORE_ID_BRANCH_ID_SHIFT;
	props->core_id += 0x010203040506ULL;   // provide a dummy core id
	props->dummy_dev = true;
	props->num_cnn_core_devs = VHA_NUM_CORES;
#else
	props->product_id = IOREAD64_CR_REGIO(PRODUCT_ID);
	props->core_id = IOREAD64_CR_REGIO(CORE_ID);
#endif
	props->skip_bvnc_check = false;
	/*
	 * New mmu version 3 and onwards operates on 40bit physical & virtual addresses
	 */
	props->mmu_width = 40;

	/* HW from 1.1 onwards */
	ip_config = IOREAD64_CR_REGIO(CORE_IP_CONFIG);
#ifdef HW_AX3
	props->mmu_ver = VHA_CR_GETBITS(CORE_IP_CONFIG, MMU_VERSION, ip_config);
#endif
	/* Mirage uses MMU version 3 hardware */
	if (!props->mmu_ver)
		props->mmu_ver = 3;
			;
	/* Read num cores supported (number of WMs must be the same). */
	if (VHA_CR_GETBITS(CORE_IP_CONFIG, CNN_SUPPORTED, ip_config)) {
		uint64_t ip_config1 = IOREAD64_CR_REGIO(CORE_IP_CONFIG1);
		props->num_cnn_core_devs =
				1 + VHA_CR_GETBITS(CORE_IP_CONFIG1, NUM_CORES_MIN1, ip_config1);
	}
	if (VHA_CR_GETBITS(CORE_IP_CONFIG, RTM_SUPPORTED, ip_config))
		props->supported.rtm = 1;
#ifdef HW_AX3
	if (VHA_CR_GETBITS(CORE_IP_CONFIG, PARITY_REGISTERS, ip_config))
		props->supported.parity = 1;

#if defined(CONFIG_VHA_DUMMY) && defined(VHA_SCF)
	/* Force parity for pdump generation */
	props->supported.parity = 1;
#endif
#endif

	if ((props->num_cnn_core_devs == 0)
		|| VHA_CR_GETBITS(CORE_ID, BRANCH_ID, props->core_id) != HW_SERIES) {
		dev_err(vha->dev, "%s: Wrong core configuration detected. "
			"Expected BVNC %d.x.x.x, got %llu.x.x.x. "
			"Maybe kernel module was built with wrong params.\n",
			__func__, HW_SERIES,
			VHA_CR_GETBITS(CORE_ID, BRANCH_ID, props->core_id));
		return -ENODEV;
	}

	dev_info(vha->dev, "%s: Product id: %#llx\n",
			__func__, props->product_id);
	dev_info(vha->dev, "%s: Core id: %#llx\n",
			__func__, props->core_id);
	dev_info(vha->dev, "%s: MMU version:%d (%dbit)\n",
			__func__, props->mmu_ver, props->mmu_width);
	dev_dbg(vha->dev, "%s: supported: %#x\n",
			__func__, props->features);
	{
		uint64_t tmp = IOREAD64_CR_REGIO(CORE_IP_INTEGRATOR_ID);
		dev_dbg(vha->dev, "%s: ip integrator id: %#llx\n",
				__func__, tmp);
		tmp = IOREAD64_CR_REGIO(CORE_IP_CHANGELIST);
		dev_dbg(vha->dev, "%s: ip change list: %llu\n", __func__, tmp);
	}

	/* Read OCM info */
	{
		uint64_t ip_config1 = IOREAD64_CR_REGIO(CORE_IP_CONFIG1);
		/* Power of 2 Look-up table */
		uint8_t pow_2_lut[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

		/* LOCM per core size */
		locm_size_kb = VHA_CR_GETBITS(CORE_IP_CONFIG1, CORE_OCM_RAM_SIZE_4KB, ip_config1) * 4;
		/* SOCM total size */
		socm_size_kb = VHA_CR_GETBITS(CORE_IP_CONFIG1, SYS_OCM_RAM_SIZE_4KB, ip_config1) * 4;
		/* SOCM number of subbanks per bank array which is stored in hw reg as Log2  */
		socm_num_sb = pow_2_lut[VHA_CR_GETBITS(CORE_IP_CONFIG1, SYS_OCM_NUM_SUBBANKS_LOG2, ip_config1)];
		/* SOCM number of arrays per bank group */
		socm_num_ba = 1 + VHA_CR_GETBITS(CORE_IP_CONFIG1, SYS_OCM_NUM_BANK_ARRAYS_MIN1, ip_config1);
		/* SOCM number of bank groups */
		socm_num_bg = 1 + VHA_CR_GETBITS(CORE_IP_CONFIG1, SYS_OCM_NUM_BANK_GROUPS_MIN1, ip_config1);
		/* External memory interface width which is stored in hw reg as 8 * Log2 */
		ext_mem_bus_width = 8 * pow_2_lut[VHA_CR_GETBITS(CORE_IP_CONFIG1, EXT_MEM_BUS_WIDTH, ip_config1)];
	}

	if (locm_size_kb) {
		props->locm_size_bytes = locm_size_kb * 1024;
		/* User may wanted to limit local OCM ... */
		if (onchipmem_size) {
			if (onchipmem_size < props->locm_size_bytes) {
				dev_warn(vha->dev, "%s:Limiting local onchip memory to %u bytes (available:%u)\n",
						__func__, onchipmem_size, props->locm_size_bytes);
				props->locm_size_bytes = onchipmem_size;
			} else if (onchipmem_size > props->locm_size_bytes) {
				dev_warn(vha->dev, "%s: User defined local onchip memory size exceeded (%u > %u))\n",
						__func__, onchipmem_size, props->locm_size_bytes);
			}
		}
	} else {
		props->locm_size_bytes = onchipmem_size;
	}

	if (socm_size_kb) {
		props->socm_size_bytes = socm_size_kb * 1024;
		/* User may wanted to limit shared OCM ... */
		if (shared_onchipmem_size) {
			if (shared_onchipmem_size < props->socm_size_bytes) {
				dev_warn(vha->dev, "%s:Limiting shared onchip memory to %u bytes (available:%u)\n",
						__func__, shared_onchipmem_size, props->socm_size_bytes);
				props->socm_size_bytes = shared_onchipmem_size;
			} else if (shared_onchipmem_size > props->socm_size_bytes) {
				dev_warn(vha->dev, "%s: User defined shared onchip memory size exceeded (%u > %u))\n",
						__func__, shared_onchipmem_size, props->socm_size_bytes);
			}
		}
		{
			/* SOCM per core must be must be a multiple of socm_total_sb & ext_mem_bus_width */
			uint16_t socm_total_sb = socm_num_sb * socm_num_ba * socm_num_bg;
			if (socm_total_sb && ext_mem_bus_width) {
				/* The below division will round down */
				props->socm_core_size_bytes = props->socm_size_bytes /
						(props->num_cnn_core_devs * socm_total_sb * ext_mem_bus_width);
				/* Scale it back */
				props->socm_core_size_bytes *= socm_total_sb * ext_mem_bus_width;
			} else {
				/* Divide by number of cores as for dummy driver */
				props->socm_core_size_bytes = shared_onchipmem_size / props->num_cnn_core_devs;
				dev_warn(vha->dev, "%s: Shared onchip memory size per core can't be rounded"
						" based on SB:%d BA:%d BG:%d BW:%d!\n", __func__,
						socm_num_sb, socm_num_ba, socm_num_bg, ext_mem_bus_width);
			}
		}
	} else {
		props->socm_size_bytes = shared_onchipmem_size;
		/* Just divide by number of cores (dummy driver) */
		props->socm_core_size_bytes = shared_onchipmem_size / props->num_cnn_core_devs;
	}

	dev_info(vha->dev, "%s: Total onchip memory, Local: %u [kB], Shared total: %u [kB]"
			" per core: %u [kB]\n", __func__, props->locm_size_bytes / 1024,
			props->socm_size_bytes / 1024, props->socm_core_size_bytes / 1024);

	dev_info(vha->dev, "%s: Devices: DUMMY:%u CNN:%u\n", __func__,
			props->dummy_dev ? props->num_cnn_core_devs : 0,
			props->dummy_dev ? 0 : props->num_cnn_core_devs);

	return 0;
}

/* prepare CRC and DEBUG data buffers */
void vha_dbg_prepare_hwbufs(struct vha_session *session, struct vha_cmd *cmd,
		struct vha_crc_config_regs *regs)
{
	struct vha_dev *vha = session->vha;
	uint8_t mask = cmd->hw_sched_info.core_mask;

	if (session->cnn_dbg.cnn_crc_buf[0] || vha->cnn_combined_crc_enable) {
		uint8_t id;

		/* Note: all buffers have the same size */
		img_pdump_printf("-- Select cores\n");
		IOWRITE64_CR_PDUMP((uint64_t)mask, CORE_CTRL_INDIRECT);

		/* enable CRC: address + mode */
		if (session->cnn_dbg.cnn_crc_buf[0])
			regs->crc_control |= VHA_CR_SETBITS(OS0_CNN_CRC_CONTROL, CNN_CRC_ENABLE,
								 session->cnn_dbg.cnn_crc_mode);
		if (vha->cnn_combined_crc_enable)
			regs->crc_control |= VHA_CR_SETBITS(OS0_CNN_CRC_CONTROL, COMBINED_CNN_CRC_ENABLE, 1);
		img_pdump_printf("-- CRC_CONTROL=%llx buf 'CRC' size=%zx\n",
				regs->crc_control,
				session->cnn_dbg.cnn_crc_buf[0] ? session->cnn_dbg.cnn_crc_buf[0]->size : 0);
		IOWRITE64_CR_PDUMP(regs->crc_control, OS0_CNN_CRC_CONTROL);
		img_pdump_printf("-- CRC_MASK=%#x\n", session->cnn_dbg.cnn_crc_mask);
		IOWRITE64_CR_PDUMP(session->cnn_dbg.cnn_crc_mask, OS0_CNN_CRC_MASK_CTRL);
		regs->crc_mask_ctrl = session->cnn_dbg.cnn_crc_mask;

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if (mask & (1 << id)) {
				/* Select core to be set */
				IOWRITE64_CR_PDUMP(VHA_CR_SETBITS(CORE_CTRL_INDIRECT, MASK, (1 << id)),
										 CORE_CTRL_INDIRECT);
				if (session->cnn_dbg.cnn_crc_buf[0]) {
					struct vha_buffer *buf = session->cnn_dbg.cnn_crc_buf[id];
					IOWRITE_PDUMP_BUFADDR(session, buf, 0, VHA_CR_OS0_CNN_CRC_ADDRESS);
					SET_BUFADDR(session, buf, 0, &regs->crc_address[id]);
				}

				if (vha->cnn_combined_crc_enable) {
					struct vha_buffer *buf = session->cnn_dbg.cnn_combined_crc;
					IOWRITE_PDUMP_BUFADDR(session, buf, id * VHA_COMBINED_CRC_CORE_OFFSET,
						VHA_CR_OS0_COMBINED_CNN_CRC_ADDRESS);
					SET_BUFADDR(session, buf, id * VHA_COMBINED_CRC_CORE_OFFSET, &regs->crc_combined_address[id]);
				}
			}
		}
	}
	if (session->cnn_dbg.cnn_dbg_buf[0] && session->cnn_dbg.cnn_dbg_pdump_enable) {
		uint64_t val64;
		uint8_t id;

		/* Note: all buffers have the same size */
		img_pdump_printf("-- Select cores\n");
		IOWRITE64_CR_PDUMP((uint64_t)mask, CORE_CTRL_INDIRECT);

		/* enable DEBUG: address, perf mode, band mode */
		img_pdump_printf("-- DEBUG_CONTROL=%u,%u buf 'DBG' size=%zx\n",
				GET_CNN_DBG_MODE(PERF, session), GET_CNN_DBG_MODE(BAND, session),
				session->cnn_dbg.cnn_dbg_buf[0]->size);

		val64 = VHA_CR_ALIGN_SETBITS(OS0_CNN_DEBUG_SIZE_LEGACY,
						CNN_DEBUG_SIZE, session->cnn_dbg.cnn_dbg_buf[0]->size);
		IOWRITE64_CR_PDUMP(val64, OS0_CNN_DEBUG_SIZE_LEGACY);

		/* Set the CONTROL register only if requested */
		if (CNN_DBG_MODE_ON(PERF, session) || CNN_DBG_MODE_ON(BAND, session)) {

			val64 = VHA_CR_SETBITS(OS0_CNN_DEBUG_CONTROL, CNN_PERF_ENABLE,
									GET_CNN_DBG_MODE(PERF, session));
			val64 |= VHA_CR_SETBITS(OS0_CNN_DEBUG_CONTROL, CNN_BAND_ENABLE,
									GET_CNN_DBG_MODE(BAND, session));
			img_pdump_printf("IF DUMP_DBG\n");
			IOWRITE64_CR_PDUMP(val64, OS0_CNN_DEBUG_CONTROL);
			img_pdump_printf("FI DUMP_DBG\n");
		}

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			if (mask & (1 << id)) {
				struct vha_buffer *buf = session->cnn_dbg.cnn_dbg_buf[id];
				/* Select core to be set */
				IOWRITE64_CR_PDUMP(VHA_CR_SETBITS(CORE_CTRL_INDIRECT, MASK, (1 << id)),
												 CORE_CTRL_INDIRECT);
				IOWRITE_PDUMP_BUFADDR(session, buf, 0, VHA_CR_OS0_CNN_DEBUG_ADDRESS);
			}
		}
	}

	/* WM Performance & Bandwidth measurement */
	if (WM_DBG_MODE_ON(PERF) || WM_DBG_MODE_ON(BAND)) {
		uint64_t dbg_ctrl = 0;

		img_pdump_printf("IF CHECK_PERF_BW\n");
		if (WM_DBG_MODE_ON(PERF)) /* PERF */
			dbg_ctrl = VHA_SET_FIELD_SIMPLE_VAL(WM_DEBUG_CONTROL, PERF_ENABLE, EN);
		if (WM_DBG_MODE_ON(BAND)) { /* BW */
			uint64_t hw_brns = cmd->user_cmd.cmd_type == VHA_CMD_CNN_SUBMIT_MULTI ?
					((struct vha_user_cnn_submit_multi_cmd*)&cmd->user_cmd)->hw_brns :
					((struct vha_user_cnn_submit_cmd*)&cmd->user_cmd)->hw_brns;

			IOWRITE64_CR_PDUMP(VHA_CR_NOC_BWM_CONTROL_MASKFULL, NOC_BWM_CONTROL);
			dbg_ctrl |= VHA_SET_FIELD_SIMPLE_VAL(WM_DEBUG_CONTROL, BW_ENABLE, EN);
			if (VHA_IS_BRN(hw_brns, 71649)) {
				img_pdump_printf("-- BRN71649_START\n");
				IOWRITE64_CR_PDUMP(16, IDLE_HYSTERESIS_COUNT);
				IOWRITE64_CR_PDUMP(16, PWR_MAN_HYSTERESIS);
				img_pdump_printf("-- BRN71649_END\n");
			}
		}

		IOWRITE64_CR_PDUMP(dbg_ctrl, WM_DEBUG_CONTROL);
		img_pdump_printf("FI CHECK_PERF_BW\n");
	}
}

/* flush CRC and DEBUG data buffers */
void vha_dbg_flush_hwbufs(struct vha_session *session, char checkpoint, uint8_t mask)
{
	struct vha_dev *vha = session->vha;

	if (session->cnn_dbg.cnn_dbg_flush != checkpoint)
		return;

	if (session->cnn_dbg.cnn_crc_buf[0] || vha->cnn_combined_crc_enable) {
		int id;
		/* Note: all buffers have the same size */
		/*
		 * TOBEDONE: calculate CRC buffer size based
		 * on num passes, num layers, etc
		 */
		img_pdump_printf("-- Save signatures\n");
		img_pdump_printf("IF SKIP_CHECK_CRCS\n");
		img_pdump_printf("COM Not checking CRCs!\n");
		img_pdump_printf("ELSE SKIP_CHECK_CRCS\n");
		img_pdump_printf("COM Checking CRCs ...\n");
		if (session->cnn_dbg.cnn_crc_buf[0]) {
			for (id = 0; id < session->vha->hw_props.num_cnn_core_devs; id++) {
				if (mask & (1 << id)) {
					struct vha_buffer *buf = session->cnn_dbg.cnn_crc_buf[id];
					vha_pdump_sab_buf(session, PDUMP_CRC, buf, 0, buf->size);
				}
			}
		}
		if (vha->cnn_combined_crc_enable) {
			struct vha_buffer *buf = session->cnn_dbg.cnn_combined_crc;
			vha_pdump_sab_buf(session, PDUMP_CRC_CMB, buf, 0, buf->size);
		}
		img_pdump_printf("FI SKIP_CHECK_CRCS\n");
	}
	if (session->cnn_dbg.cnn_dbg_buf[0] && session->cnn_dbg.cnn_dbg_pdump_enable) {
		int id;

		img_pdump_printf("-- Save DEBUG info\n");
		img_pdump_printf("IF DUMP_DBG\n");
		img_pdump_printf("COM Dumping debug data ...\n");
		for (id = 0; id < session->vha->hw_props.num_cnn_core_devs; id++) {
			if (mask & (1 << id)) {
				struct vha_buffer *buf = session->cnn_dbg.cnn_dbg_buf[id];
				vha_pdump_sab_buf(session, PDUMP_DBG, buf, 0, buf->size);
			}
		}
		img_pdump_printf("ELSE DUMP_DBG\n");
		img_pdump_printf("COM Not dumping debug data!\n");
		img_pdump_printf("FI DUMP_DBG\n");
	}
}

/* stop capturing CRC and DEBUG data */
void vha_dbg_stop_hwbufs(struct vha_session *session, uint8_t mask)
{
	struct vha_dev *vha = session->vha;

	/* Flush hw debug buffers */
	vha_dbg_flush_hwbufs(session, 0, mask);

	if (session->cnn_dbg.cnn_crc_buf[0]) {
		img_pdump_printf("-- Select cores\n");
		IOWRITE64_CR_PDUMP((uint64_t)mask, CORE_CTRL_INDIRECT);
		IOWRITE64_CR_PDUMP(0, OS0_CNN_CRC_CONTROL);
	}
	if (session->cnn_dbg.cnn_dbg_buf[0]) {
		uint64_t size = 0;
		int id;

		for (id = 0; id < session->vha->hw_props.num_cnn_core_devs; id++) {
			uint64_t val;
			if (mask & (1 << id)) {
				val = IOREAD64_CR_REGIO(OS0_CNN_DEBUG_STATUS);
				if (val > size)
					size = val;
			}
		}

		if (CNN_DBG_MODE_ON(PERF, session) || CNN_DBG_MODE_ON(BAND, session)) {
			img_pdump_printf("IF DUMP_DBG\n");
			img_pdump_printf("-- Select cores\n");
			IOWRITE64_CR_PDUMP((uint64_t)mask, CORE_CTRL_INDIRECT);
			IOWRITE64_CR_PDUMP(0, OS0_CNN_DEBUG_CONTROL);
			/* just give a hint in the pdump:
			 * dummy device returns 0 */
			img_pdump_printf(
				"-- POL64 :REG:%#x 0 0 0 1 1 -- DEBUG_STATUS=%llx\n",
				VHA_CR_OS0_CNN_DEBUG_STATUS,
				size);
			img_pdump_printf("FI DUMP_DBG\n");
		}
	}
}

uint64_t vha_dbg_rtm_read(struct vha_dev *vha, uint64_t addr)
{
	
	return 0ULL;
}

const struct vha_reg vha_regs[] = {
#define REG_DESC(reg) VHA_CR_##reg, VHA_CR_##reg##_MASKFULL
	{"product_id           ", REG_DESC(PRODUCT_ID)},
	{"core_id              ", REG_DESC(CORE_ID)},
	{"integrator_id        ", REG_DESC(CORE_IP_INTEGRATOR_ID)},
	{"ip_changelist        ", REG_DESC(CORE_IP_CHANGELIST)},
	{"core_ip_config       ", REG_DESC(CORE_IP_CONFIG)},
#undef REG_DESC
	{NULL                   , 0},
};

#ifdef VHA_SCF
void wd_timer_callback(struct work_struct *work)
{
	struct vha_dev *vha =
			container_of(work, struct vha_dev, swd_dwork.work);
	struct vha_cmd *cmd = NULL;
	unsigned int wm_id;

	mutex_lock(&vha->lock);

	for (wm_id = 0; wm_id < vha->hw_props.num_cnn_core_devs; wm_id++) {
		cmd = vha->pendcmd[wm_id].cmd;
		if (cmd) {
			uint8_t core_mask = vha_wm_get_cores(vha, wm_id);
			uint8_t layer_count;
			uint8_t pass_count;
			bool lockup = false;
			uint64_t exec_time_us;
			uint64_t cmd_time_us;
			struct TIMESPEC now;

			GETNSTIMEOFDAY(&now);

			if (cmd->user_cmd.flags & VHA_EXEC_TIME_SET) {
				struct vha_user_cnn_submit_multi_cmd *cnn_user_cmd =
					(struct vha_user_cnn_submit_multi_cmd *)&cmd->user_cmd;
				cmd_time_us = cnn_user_cmd->exec_time;
			} else if (vha->swd_timeout_default)
				cmd_time_us = vha->swd_timeout_default;
			else //SW WDT disabled for this cmd
				continue;

			cmd_time_us *= vha->swd_timeout_m0;

			if (get_timespan_us(&cmd->hw_proc_start, &now, &exec_time_us)) {
				uint64_t expected_exec_time = do_div(cmd_time_us, 100) + vha->swd_timeout_m1;

				if (exec_time_us > expected_exec_time) {
					lockup = true;
					dev_err(vha->dev, "SW WDT lockup detected\n"
									  "    measured time: %llu\n"
									  "    cmd time: %llu\n"
									  "    cmd expected_exec_time: %llu\n",
									  exec_time_us, cmd_time_us, expected_exec_time);
				}
			}

			while (core_mask != 0 && !lockup) {
				uint32_t core_id = VHA_CORE_MASK_TO_ID(core_mask);
				uint64_t cnn_status;
				uint64_t cnn_status2;

				core_mask &= ~(VHA_CORE_ID_TO_MASK(core_id));

				IOWRITE64_CR_REGIO(VHA_CR_SETBITS(CORE_CTRL_INDIRECT, MASK, (1 << core_id)),
										 CORE_CTRL_INDIRECT);

				cnn_status = IOREAD64_CR_REGIO(OS0_CNN_STATUS);
				cnn_status2 = IOREAD64_CR_REGIO(OS0_CNN_STATUS2);

				layer_count = VHA_CR_GETBITS_OS(CNN_STATUS, LAYER_COUNT, cnn_status);
				pass_count = VHA_CR_GETBITS_OS(CNN_STATUS2, PASS_COUNT, cnn_status2);

				if (cmd->layer_count[core_id] == layer_count &&
					cmd->pass_count[core_id] == pass_count) {
					lockup = true;
					dev_err(vha->dev, "SW WDT lockup detected\n"
									  "    layer_count:  %d\n"
									  "    pass_count: %d\n", layer_count, pass_count);
				}

				cmd->layer_count[core_id] = layer_count;
				cmd->pass_count[core_id] = pass_count;
			}

			if (lockup) {
				if (vha_observers.error)
					vha_observers.error(vha->id, cmd->session->id, cmd->user_cmd.cmd_id,  -EIO);

				/* Update stats. */
				vha->stats.total_failures++;
				vha->stats.cnn_kicks_completed++;
				VHA_INC_WL_STAT(vha, kicks_completed, cmd);
				vha_wm_reset(vha, &cmd->hw_sched_info);
				/* Free command resources. */
				vha_wm_release_cores(vha, cmd->hw_sched_info.core_mask, false);
				vha_dev_free_cmd_res(vha, cmd, true);

				/* Move command queue. */
				vha_do_queued_cmd(vha, wm_id);
				/* Handle actual command */
				vha_handle_cmd(vha, wm_id,  -EIO, -EIO, VHA_RSP_ERROR(SW_WDT_EXPIRED));
			}
		}
	}

	if (vha->state == VHA_STATE_ON)
		schedule_delayed_work(&vha->swd_dwork, msecs_to_jiffies(vha->swd_period));

	mutex_unlock(&vha->lock);
}
#endif

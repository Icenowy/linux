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

#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/moduleparam.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <uapi/vha.h>
#include "vha_common.h"
#include "vha_plat.h"
#include "vha_regs.h"

#if defined(CFG_SYS_VAGUS)
#include <hwdefs/nn_sys_cr_vagus.h>
#endif

#include <vha_trace_point.h>

#define ERR_EVENT_DESC(b) VHA_CR_OS(VHA_EVENT_STATUS_VHA_##b##_EN), __stringify(b)

static void vha_dev_disable_events(struct vha_dev *vha)
{
	img_pdump_printf("-- Clear CNN events\n");
	IOWRITE64_PDUMP(VHA_EVNTS_DEFAULT, VHA_CR_OS(VHA_EVENT_CLEAR));
	img_pdump_printf("-- Disable CNN events\n");
	IOWRITE64_PDUMP(0, VHA_CR_OS(VHA_EVENT_ENABLE));
	/* Clear the START bit !
	 * Note: It is stated that writing 0 to this bit has no effect,
	 * however in error cases, some hw blocks may start
	 * to process previous requests after turning on the clocks
	 * which was previously disabled */
	IOWRITE64_PDUMP(0, VHA_CR_OS(CNN_CONTROL));

	/* Disable core events */
	img_pdump_printf("-- Disable CORE events\n");
	IOWRITE64_PDUMP(0, VHA_CR_OS(VHA_EVENT_ENABLE));
}

__maybe_unused
static void vha_dev_enable_clocks(struct vha_dev *vha)
{
	uint64_t __maybe_unused sys_clks = 0;
	uint64_t __maybe_unused main_clks = 0;

	/* Always AUTO gating  when needed */
	sys_clks = VHA_SYS_CLOCKS_DEFAULT(AUTO);
	main_clks = VHA_MAIN_CLOCKS_DEFAULT(AUTO);
	/* Enable sys clocks ! */
	img_pdump_printf("-- Enable SYS clocks\n");
	IOWRITE64_PDUMP(sys_clks, VHA_CR_SYS_CLK_CTRL0);
	/* Enable main clocks ! */
	img_pdump_printf("-- Enable MAIN clocks\n");
	IOWRITE64_PDUMP(main_clks, VHA_CR_CLK_CTRL0);
#if defined(CFG_SYS_VAGUS)
	img_pdump_printf("-- Enable NN_SYS clocks\n");
	IOWRITE64_PDUMP_REGIO(NN_SYS_CR_CLK_CTRL_MODE_AUTO,
			NN_SYS_CR_BASE, NN_SYS_CR_CLK_CTRL, "REG_NNSYS");
#endif
}

static void vha_dev_ready(struct vha_dev *vha)
{
#ifndef CONFIG_VHA_DUMMY
	if (!vha->is_ready)
		return;
#endif
	dev_dbg(vha->dev, "%s\n", __func__);

	vha_dev_wait(vha);

	/* Finally enable ALL events */
	img_pdump_printf("-- Enable ALL events\n");
	IOWRITE64_PDUMP(VHA_EVNTS_DEFAULT, VHA_CR_OS(VHA_EVENT_ENABLE));
	img_pdump_printf("-- Clear ALL events\n");
	IOWRITE64_PDUMP(VHA_EVNTS_DEFAULT, VHA_CR_OS(VHA_EVENT_CLEAR));
#ifdef HW_AX2
	img_pdump_printf("-- Clear CNN status\n");
	IOWRITE64_PDUMP(0, VHA_CR_OS(CNN_STATUS));
#endif
	img_pdump_printf("-- Clear MMU fault status\n");
	IOWRITE64_PDUMP(0, VHA_CR_OS(MMU_FAULT_STATUS1));
	img_pdump_printf("-- Clear SLC debug status\n");
	IOWRITE64_PDUMP(0, VHA_CR_SLC_STATUS_DEBUG);
	img_pdump_printf("-- Reset PERF counters\n");
	IOWRITE64_PDUMP(0, VHA_CR_PERF_RESET_FULL);
}

__maybe_unused
static int vha_dev_reset(struct vha_dev *vha)
{
	img_pdump_printf("-- Set RESET bits\n");
#if defined(CFG_SYS_VAGUS)
	IOWRITE64_PDUMP_REGIO(NN_SYS_CR_RESET_CTRL_NN_SYS_EN,
			NN_SYS_CR_BASE, NN_SYS_CR_RESET_CTRL, "REG_NNSYS");
#endif
	/* Perform reset procedure */
	IOWRITE64_PDUMP(VHA_RESET_DEFAULT, VHA_CR_RESET_CTRL);

	/* poll for reset deassertion
	 * count=16, delay=256cycles
	 */
	img_pdump_printf("-- Wait for RESET deassertion\n");
#if defined(CFG_SYS_VAGUS)
	IOPOLL64_PDUMP_REGIO(0, 16, 256, NN_SYS_CR_RESET_CTRL_MASKFULL,
			NN_SYS_CR_BASE, NN_SYS_CR_RESET_CTRL, "REG_NNSYS");
#endif
	IOPOLL64_PDUMP(0, 16, 256, VHA_CR_RESET_CTRL_MASKFULL,
					VHA_CR_RESET_CTRL);
	return 0;
}

__maybe_unused
static int vha_dev_disable_clocks(struct vha_dev *vha)
{
	/* If auto gating was turned on, wait for clocks idle state */
	img_pdump_printf("-- Wait for clocks IDLE state\n");
	IOPOLL64_PDUMP(0, 1000, 1000,
			VHA_CR_CLK_STATUS0_MASKFULL,
			VHA_CR_CLK_STATUS0);
#if defined(CFG_SYS_VAGUS)
	IOPOLL64_PDUMP_REGIO(0, 100, 1000, NN_SYS_CR_CLK_STATUS_MASKFULL,
			NN_SYS_CR_BASE, NN_SYS_CR_CLK_STATUS, "REG_NNSYS");
#endif
	/* Wait for MMU,CCM,RDI,XBAR  IDLE state */
	img_pdump_printf("-- Wait for memory bus interface IDLE state\n");
	IOPOLL64_PDUMP(0xFFFF, 100, 1000, VHA_CR_SLC_IDLE_MASKFULL,
			VHA_CR_SLC_IDLE);

	/* Finally disable clocks */
	img_pdump_printf("-- Disable MAIN clocks\n");
	IOWRITE64_PDUMP(0, VHA_CR_CLK_CTRL0); /* main */
	img_pdump_printf("-- Disable SYS clocks\n");
	IOWRITE64_PDUMP(0, VHA_CR_SYS_CLK_CTRL0); /* sys */
#if defined(CFG_SYS_VAGUS)
	img_pdump_printf("-- NN_SYS clocks\n");
	IOWRITE64_PDUMP_REGIO(0, NN_SYS_CR_BASE,
			NN_SYS_CR_CLK_CTRL, "REG_NNSYS"); /* nn_sys */
#endif
	return 0;
}

/* start the device */
int vha_dev_start(struct vha_dev *vha)
{
	int ret = 0;

	/* Cancel APM request if new inference comes */
	cancel_delayed_work(&vha->apm_dworks[0].dwork);

	if (vha->state == VHA_STATE_ON)
		return 0; /* not an error */

	dev_dbg(vha->dev, "%s\n", __func__);

/* Assuming OS0 is the privileged one */
#if _OSID_ == 0 /* For HW_AX2 this is always true */
	pm_runtime_get_sync(vha->dev);
	/////////////// POWER ON //////////////////////////
	img_pdump_printf("-- POWER_ON_BEGIN\n");
	
	/* Prepare device ...  */
	ret = vha_dev_prepare(vha);
	if (ret) {
		dev_err(vha->dev, "%s: Error preparing device!\n", __func__);
		return ret;
	}
	/* Reset device */
	ret = vha_dev_reset(vha);
	if (ret){
		dev_err(vha->dev, "%s: Error reseting device!\n", __func__);
		return ret;
	}
	/* Enable device clocks */
	vha_dev_enable_clocks(vha);
	img_pdump_printf("-- POWER_ON_END\n");
	/* Call device specific setup */
	vha_dev_setup(vha);
	/////////////////////////////////////////////////////
#endif

	vha_dev_ready(vha);

	vha->state = VHA_STATE_ON;
	/* Remember the time hw is powered on */
	GETNSTIMEOFDAY(&vha->stats.hw_start);
	return ret;
}

/* stop the device */
int vha_dev_stop(struct vha_dev *vha, bool reset)
{
	int ret = 0;

	if (vha->state == VHA_STATE_OFF)
		return 0;  /* not an error */

	/* Cancel APM request if we are about to power off the core */
	cancel_delayed_work(&vha->apm_dworks[0].dwork);

	dev_dbg(vha->dev, "%s\n", __func__);
	/* Disable events at first */
	vha_dev_disable_events(vha);

	vha->is_ready = false;
/* Assuming OS0 is the privileged one */
#if _OSID_ == 0 /* For HW_AX2 */
	/////////////// POWER_OFF //////////////////////////
	img_pdump_printf("-- POWER_OFF_BEGIN\n");
	/* Reset core in case of error or pending inference */
	if (reset) {
		/* ensure that clocks are set to AUTO before reset */
		vha_dev_enable_clocks(vha);
		ret = vha_dev_reset(vha);
	}
	if(ret)
		dev_warn(vha->dev,
			"%s: Problem with resetting device!\n",
			__func__);

	/* Disable device clocks */
	ret = vha_dev_disable_clocks(vha);
	if(ret)
		dev_warn(vha->dev,
					"%s: Problem with disabling clocks!\n",
					__func__);

	img_pdump_printf("-- POWER_OFF_END\n");
	/////////////////////////////////////////////////////
	if (reset) {
		pm_runtime_mark_last_busy(vha->dev);
		pm_runtime_put_sync_autosuspend(vha->dev);
	} else {
		pm_runtime_put_sync(vha->dev);
	}
#endif

	vha->state = VHA_STATE_OFF;
	/* Update the up time of the core */
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
	vha->irq_status = 0;
	vha->irq_count = 0;
	vha->stream_count = 0;
	spin_unlock_irq(&vha->irq_lock);

	return ret;
}

void vha_update_utilization(struct vha_dev *vha)
{
	uint64_t tmp;
	tmp = vha->stats.cnn_total_proc_us;
	do_div(tmp, vha->stats.uptime_ms);
	vha->stats.cnn_utilization = tmp;
}

#ifdef VHA_EVENT_INJECT
/*
 * Inject EVENT_STATUS bits, requested by respective debugfs nodes, to
 * the status register.
 */
static inline void __inject_event_regs(struct vha_dev* vha, uint64_t* event_status)
{
	if(!__EVENT_INJECT())
		return;

	if (*event_status & (1 << VHA_CR_VHA_EVENT_STATUS_TYPE_VHA_CNN0_COMPLETE_SHIFT))
		*event_status |= vha->injection.vha_cr_event;
}
#endif

/* Top half */
irqreturn_t vha_handle_irq(struct device *dev)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	int ret = IRQ_HANDLED;
	uint64_t event_status;

	if (!vha)
		return IRQ_NONE;

	event_status = IOREAD64(vha->reg_base, VHA_CR_OS(VHA_EVENT_STATUS));
	event_status &= IOREAD64(vha->reg_base, VHA_CR_OS(VHA_EVENT_ENABLE));
	/* On fpga platform it is possible to get
	 * a spurious interrupt when the hw died
	 * Do not proceed, just throw a warning */
	if (event_status == VHA_DEAD_HW || event_status == ~0) {
		WARN_ONCE(1, "Hardware is dead!");
		return IRQ_NONE;
	}

#ifdef VHA_EVENT_INJECT
	__inject_event_regs(vha, &event_status);
#endif

#ifdef VHA_SCF
	if (vha->hw_props.supported.parity &&
			!vha->parity_disable) {
		bool par_bit = img_mem_calc_parity(event_status &
				~VHA_CR_BITMASK(VHA_EVENT_STATUS_TYPE, PARITY));
		if (par_bit !=
				VHA_CR_GETBITS(VHA_EVENT_STATUS_TYPE, PARITY,
						event_status)) {
			dev_err(dev, "Event status register parity error!\n");
			/* Use the real event to indicate the error */
			event_status |=  VHA_CR_OS(VHA_EVENT_STATUS_VHA_PARITY_ERROR_EN);
		}
		/* Clear the PARITY bit - it's not a valid event */
		VHA_CR_CLEARBITS(event_status, VHA_EVENT_STATUS_TYPE, PARITY);
	}
#endif

	if (event_status & VHA_EVNTS_DEFAULT) {
		uint64_t cnn_status;
		uint8_t count;

		/* clear the interrupt:
		 * best not to write pdump in interrupts */
		IOWRITE64(vha->reg_base, VHA_CR_OS(VHA_EVENT_CLEAR),
				event_status & VHA_EVNTS_DEFAULT);

		/* Read the stream count as single IRQ may be raised for multiple kicks */
		cnn_status = IOREAD64(vha->reg_base, VHA_CR_OS(CNN_STATUS));

#ifdef VHA_SCF
		if (vha->hw_props.supported.parity &&
				!vha->parity_disable) {
			bool par_bit = img_mem_calc_parity(cnn_status &
					~VHA_CR_BITMASK_OS(CNN_STATUS, PARITY));
			if (par_bit != VHA_CR_GETBITS_OS(CNN_STATUS, PARITY, cnn_status)) {
				dev_err(dev, "CNN status register parity error!\n");
				/* Use the real event to indicate the error */
				event_status |=  VHA_CR_OS(VHA_EVENT_STATUS_VHA_PARITY_ERROR_EN);
			}
		}
#endif
		if (vha->is_ready) {
			/* Post check for AXI bus errors */
			uint64_t ace_status = IOREAD64(vha->reg_base, VHA_CR_ACE_STATUS);
			if (ace_status) {
				dev_err(vha->dev, "AXI bus protocol error: %#llx\n",
							ace_status);
				/* Use AXI error event to indicate that */
				event_status |=  VHA_CR_OS(VHA_EVENT_STATUS_VHA_AXI_ERROR_EN);
			}
		}

		/* Read the stream count as single IRQ may be raised for multiple kicks */
		count = VHA_CR_GETBITS_OS(CNN_STATUS, STREAM_COUNT, cnn_status);

		spin_lock(&vha->irq_lock);
		/* store the status to be processed later */
		if (vha->do_calibration ||
				vha_is_busy(vha)) {
			vha->irq_status |= event_status;

			if (vha->low_latency == VHA_LL_SELF_KICK)
				/* Two separate IRQs may be raised for multiple kicks */
				vha->irq_count += count - vha->stream_count;
			else
				/* Only single IRQ may be raised otherwise ... */
				vha->irq_count = count - vha->stream_count;

			vha->stream_count = count;
			/* Record hw processing end timestamps */
			vha->stats.hw_proc_end_prev = vha->stats.hw_proc_end;
			GETNSTIMEOFDAY(&vha->stats.hw_proc_end);
		} else {
			/* Command may have been aborted before this handler is executed */
			vha->irq_status = 0;
			vha->irq_count = 0;
			vha->stream_count = 0;
		}
		spin_unlock(&vha->irq_lock);

		ret = IRQ_WAKE_THREAD;
	} else
		return IRQ_NONE;

	dev_dbg(dev, "IRQ 0x%08llx\n", event_status);

	return ret;
}

static bool vha_rollback_cnn_cmds(struct vha_dev *vha)
{
	bool processing = false;
	/* Not processed commands are still on the pending list
	 * of each session, so just mark the hw pending lists as empty */
	if (vha->pendcmd[VHA_CNN_CMD].cmd) {
		struct vha_cmd *pendcmd = vha->pendcmd[VHA_CNN_CMD].cmd;
		pendcmd->in_hw = false;
		pendcmd->queued = false;
		pendcmd->rolled_back = true;
		processing = true;
		vha->stats.cnn_kicks_aborted += pendcmd->subseg_current;
		vha->stats.cnn_kicks_completed -= pendcmd->subsegs_completed;
		vha->pri_q_counters[pendcmd->user_cmd.priority] += pendcmd->subseg_current;
		pendcmd->subseg_current = 0;
		pendcmd->subsegs_completed = 0;
		vha->pendcmd[VHA_CNN_CMD].cmd = NULL;
	}
	/* low_latency ...*/
	if (vha->queuedcmd[VHA_CNN_CMD].cmd) {
		struct vha_cmd *queuedcmd = vha->queuedcmd[VHA_CNN_CMD].cmd;
		queuedcmd->in_hw = false;
		queuedcmd->queued = false;
		queuedcmd->rolled_back = true;
		vha->stats.cnn_kicks_aborted += queuedcmd->subseg_current;
		vha->stats.cnn_kicks_completed -= queuedcmd->subsegs_completed;
		vha->pri_q_counters[queuedcmd->user_cmd.priority] += queuedcmd->subseg_current;
		queuedcmd->subseg_current = 0;
		queuedcmd->subsegs_completed = 0;
		vha->queuedcmd[VHA_CNN_CMD].cmd = NULL;
	}
	dev_dbg(vha->dev, "%s: (%d)\n", __func__, processing);

	return processing;
}

bool vha_rollback_cmds(struct vha_dev *vha)
{
	return vha_rollback_cnn_cmds(vha);
}

static bool vha_is_processing(struct vha_dev *vha)
{
	return vha->pendcmd[VHA_CNN_CMD].cmd != NULL;
}

int vha_dev_suspend_work(struct vha_dev *vha)
{
	bool processing = false;
	int ret;

	/* Check if anything is being processed right now. */
	processing = vha_is_processing(vha);
	/* Forcing hardware disable. */
	ret = vha_dev_stop(vha, processing);
	/* Rollback commands after hw is stopped. */
	vha_rollback_cmds(vha);

	return ret;
}

/*
 * handles the command already processed by the hw.
 */
static bool vha_handle_cmd(struct vha_dev *vha, int status)
{
	struct vha_cmd *cmd = NULL;

	cmd = vha->pendcmd[VHA_CNN_CMD].cmd;
	if (unlikely(!cmd)) {
		dev_dbg(vha->dev, "No command. Probably it has been aborted\n");
		return false;
	}

	{
		uint64_t proc_time = 0;
		struct TIMESPEC *from = &cmd->hw_proc_start;
		struct TIMESPEC *to = &vha->stats.hw_proc_end;

		if (TIMESPEC_COMPARE(&vha->stats.hw_proc_end_prev, &cmd->hw_proc_start) >= 0)
			from = &vha->stats.hw_proc_end_prev;

		if (get_timespan_us(from, to, &proc_time)) {
			vha->stats.last_proc_us = proc_time;
		} else {
			vha->stats.last_proc_us = 0;
		}
		/* Update cnn stats */
		vha_cnn_update_stats(vha);

		/* Update cmd stats. */
		cmd->proc_us += vha->stats.cnn_last_proc_us;
		cmd->hw_cycles += vha->stats.cnn_last_cycles;
	}

	/* Mark this subsegment as completed. */
	if (status == 0)
		vha->pendcmd[VHA_CNN_CMD].cmd->subsegs_completed++;
	/* If this isn't the last subsegment, just return to process the next one. */
	if ((cmd->subseg_current < VHA_CMD_SUBSEG_NUM(cmd)) && (status == 0)) {
		vha->pendcmd[VHA_CNN_CMD].cmd->in_hw = false;
		vha->pendcmd[VHA_CNN_CMD].cmd = NULL;
		return true;
	}

	vha_cnn_cmd_completed(cmd, status);

	if (status) {
		/* Rollback any queued command ... */
		vha_rollback_cnn_cmds(vha);
		/* Adjust for just rolled back pending cmd. */
		vha->pri_q_counters[cmd->user_cmd.priority] -= VHA_CMD_SUBSEG_NUM(cmd);
		/* Notify immediately current command */
		vha_cmd_notify(cmd);

		return false;
	}

	if (vha->queuedcmd[VHA_CNN_CMD].cmd)
		vha->pendcmd[VHA_CNN_CMD].cmd = vha->queuedcmd[VHA_CNN_CMD].cmd;
	else
		vha->pendcmd[VHA_CNN_CMD].cmd = NULL;

	vha->queuedcmd[VHA_CNN_CMD].cmd = NULL;
	dev_dbg(vha->dev,
			"%s: %p -> new pending %p\n",
			__func__, cmd, vha->pendcmd[VHA_CNN_CMD].cmd);

	vha_cmd_notify(cmd);

	return true;
}

static void vha_do_queued_cmd(struct vha_dev *vha)
{
	struct vha_cmd *cmd, *pend;

	cmd = vha->queuedcmd[VHA_CNN_CMD].cmd;

	dev_dbg(vha->dev,
			"%s: queued %p pending %p\n",
			__func__, cmd, vha->pendcmd[VHA_CNN_CMD].cmd);

	if (!cmd || (cmd &&
				((vha->low_latency == VHA_LL_DISABLED ||
				vha->low_latency == VHA_LL_SELF_KICK) ||
						!cmd->queued))) {
		dev_dbg(vha->dev, "%s: skipping!\n", __func__);
		return;
	}

	/* store actual pending command as it will be modified */
	pend = vha->pendcmd[VHA_CNN_CMD].cmd;

	/* at this point we should be able to process the cmd */
	vha_do_cnn_cmd(cmd);

	/* restore pending */
	vha->pendcmd[VHA_CNN_CMD].cmd = pend;
}

static int vha_report_failure(struct vha_dev *vha, uint64_t status,
		const struct vha_biterr bits[], int bits_size)
{
	int error = 0;
	int i;
	int cmdid = -1;
	int sesid = -1;

	if (vha->pendcmd[VHA_CNN_CMD].cmd) {
		cmdid = vha->pendcmd[VHA_CNN_CMD].cmd->user_cmd.cmd_id;
		sesid = vha->pendcmd[VHA_CNN_CMD].cmd->session->id;
	}

	if (vha_observers.error)
		vha_observers.error(vha->id, sesid, cmdid, status);

	/* event status in human readable form */
	for (i = 0; i < bits_size; i++) {
		if (status & bits[i].b) {
			dev_err(vha->dev,
				" event status: %s\n",
				bits[i].s);
			/* convert from register bits into POSIX errno
			* if multiple errors, then arbitrary errno choice */
			error = bits[i].e;
		}
	}

	return error;
}

/* if vha event register reports CNN events, so handle them */
static int vha_handle_cnn_event(struct vha_dev *vha, uint64_t event_status)
{
	int err = 0;

	if (vha_check_calibration(vha))
		return 0;

	if (event_status & VHA_CNN_ERR_EVNTS) {
		static const struct vha_biterr err_bits[] = {
			{-ETIMEDOUT, ERR_EVENT_DESC(CNN0_MEM_WDT)},
#ifdef HW_AX2
			{-ETIMEDOUT, ERR_EVENT_DESC(CNN0_WDT)},
#endif
			{-EIO,       ERR_EVENT_DESC(CNN0_ERROR)}
		};

		err = vha_report_failure(vha,
				event_status, err_bits, ARRAY_SIZE(err_bits));

		vha_cnn_dump_status(vha);
	}

	/* Poke the hw if there were already
	 * command queued in the hw */
	if (!err)
		vha_do_queued_cmd(vha);
	/* Handle actual command */
	if (vha_handle_cmd(vha, err) == false)
		err = -ENOENT;

	return err;
}

#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
/* Simulating hw execution time by scheduling this delayed work. */
void vha_dummy_worker(struct work_struct *work)
{
	struct vha_dev *vha = container_of(work, struct vha_dev, dummy_dwork.work);

	mutex_lock(&vha->lock);

	if (vha->pendcmd[VHA_CNN_CMD].cmd) {
		/* Record hw processing end timestamps */
		vha->stats.hw_proc_end_prev = vha->stats.hw_proc_end;
		GETNSTIMEOFDAY(&vha->stats.hw_proc_end);
		/* Handle current pending command */
		vha_handle_cnn_event(vha, VHA_CNN_CMPLT_EVNT);
		vha->stats.cnn_kicks_completed++;
		/* Schedule following commands */
		vha_chk_cmd_queues(vha, true);
	}

	mutex_unlock(&vha->lock);
}
#endif

/* Bottom half */
irqreturn_t vha_handle_thread_irq(struct device *dev)
{
	struct vha_dev *vha = vha_dev_get_drvdata(dev);
	irqreturn_t ret = IRQ_HANDLED;
	uint64_t status;
	uint8_t count, c = 0;
	int err = 0;

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
	status = vha->irq_status;
	vha->irq_status = 0;
	count = vha->irq_count;
	vha->irq_count = 0;
	if (!count) {
		uint64_t proc_time = 0;

		if (get_timespan_us(&vha->stats.hw_proc_start, &vha->stats.hw_proc_end,
					&proc_time)) {
			vha->stats.last_proc_us = proc_time;
		} else {
			vha->stats.last_proc_us = 0;
		}
	}
	spin_unlock_irq(&vha->irq_lock);
	/* Command may have been aborted before this handler is executed */
	if (!status)
		goto exit;

	/* There can be two inferences already finished for self kick mode,
	 * otherwise, only single inference at the time */
	if ((vha->low_latency == VHA_LL_SELF_KICK && count > 2) ||
			(vha->low_latency != VHA_LL_SELF_KICK && count > 1))
		WARN_ON(1);

	dev_dbg(dev, "%s: status:%llx count:%d\n",
			__func__, status, count);

	do {
		if (status & VHA_CORE_EVNTS) {
			static const struct vha_biterr err_bits[] = {
				{-EIO,       ERR_EVENT_DESC(AXI_ERROR)},
				{-EFAULT,    ERR_EVENT_DESC(MMU_PAGE_FAULT)},
#ifdef HW_AX3
#ifdef VHA_SCF
				{-EIO,       ERR_EVENT_DESC(MMU_PARITY_ERROR)},
				{-EIO,       ERR_EVENT_DESC(PARITY_ERROR)},
				{-EIO,       ERR_EVENT_DESC(LOCKSTEP_ERROR)},
#endif
				{-ETIMEDOUT, ERR_EVENT_DESC(HL_WDT)},
				{-EIO,       ERR_EVENT_DESC(ERROR)}
#endif
			};

#ifdef HW_AX3
			if (status & VHA_EVENT_TYPE(HL_WDT)
					&& vha->is_ready)
				if (vha_check_calibration(vha))
					break;

			if ((status & VHA_CORE_EVNTS)==
					VHA_EVENT_TYPE(READY)
					&& !vha->is_ready) {
				vha->is_ready = true;
				vha_dev_ready(vha);
				if (vha->do_calibration) {
					vha_cnn_start_calib(vha);
					break;
				} else
					vha_chk_cmd_queues(vha, true);
			}
#endif

			err = vha_report_failure(vha, status,
					err_bits, ARRAY_SIZE(err_bits));
			if (err) {
				dev_err(vha->dev, "NNA hw failure: %llx\n", status);
				dev_err(vha->dev, "   CLK_STATUS0:%llx ",
					IOREAD64(vha->reg_base, VHA_CR_CLK_STATUS0));
				dev_err(vha->dev, " VHA_EVENT_STATUS:%llx ", status);
			}

			if (status & VHA_EVENT_TYPE(MMU_PAGE_FAULT))
				/* dump mmu status */
				vha_mmu_status(vha);
		}

		/* If no core level error process cnn events */
		if (!err && status & VHA_CNN_EVNTS)
			err = vha_handle_cnn_event(vha, status);
#ifdef HW_AX3
		else if (status == VHA_EVENT_TYPE(ERROR)) {
			/* Resubmit command next time if no CNN error detected
			 * and only ERROR bit is set.
			 * That means other OS caused the error */
			vha_rollback_cnn_cmds(vha);
		}
#endif
		else if (err && vha->is_ready) { /* Core level error */
			if (vha_handle_cmd(vha, err) == false)
				err = -ENOENT;
		}

		c++;
	} while (c < count && !err);

	if (err) {
		vha->stats.total_failures += count ? count : 1;
		vha_dev_stop(vha, true);
		/* Check queues ... */
		vha_chk_cmd_queues(vha, true);
	} else {
		/* Run in BH context! */
		vha_chk_cmd_queues(vha, false);
	}
	vha->stats.cnn_kicks_completed += count;

exit:
#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_IRQ_WORKER)
		current->make_it_fail = false;
#endif
	trace_vha_irq(vha->id, status, count, vha->stats.last_proc_us);
	mutex_unlock(&vha->lock);

	return ret;
}

bool vha_rm_session_cmds(struct vha_session *session)
{
	struct vha_dev *vha = session->vha;
	bool pend_removed = false;
	bool queued_removed = false;
	bool reschedule = false;
	struct vha_cmd *cur_cmd, *tmp_cmd;
	uint8_t pri;

	/* Check if pend/queued commands will be removed. */
	if (vha->pendcmd[VHA_CNN_CMD].cmd &&
			vha->pendcmd[VHA_CNN_CMD].cmd->session == session) {
		dev_warn(vha->dev,
				"Removing a session while cnn cmd is still pending\n");
		pend_removed = true;
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
		cancel_delayed_work(&vha->dummy_dwork);
#endif
	}
	if (vha->queuedcmd[VHA_CNN_CMD].cmd &&
			vha->queuedcmd[VHA_CNN_CMD].cmd->session == session) {
		dev_warn(vha->dev,
				"Removing a session while cnn cmd is still queued\n");
		queued_removed = true;
	}

	/* Update session scheduling. */
	if (vha->queuedcmd[VHA_CNN_CMD].cmd &&
			(pend_removed && !queued_removed)) {
		uint8_t pri = vha->queuedcmd[VHA_CNN_CMD].cmd->user_cmd.priority;
		if (vha->queuedcmd[VHA_CNN_CMD].cmd->session !=
					list_entry(&vha->sched_sessions[pri], struct vha_session,
								sched_list[pri]))
			while(list_first_entry(&vha->sched_sessions[pri], struct vha_session,
						sched_list[pri]) != vha->queuedcmd[VHA_CNN_CMD].cmd->session)
				list_rotate_left(&vha->sched_sessions[pri]);
	}

	/* Remove pend/queued commands if needed. */
	if (pend_removed || queued_removed) {
		vha_rollback_cnn_cmds(vha);
		/* Need to reschedule too. */
		reschedule = true;
	}

	/* Remove session related commands. */
	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++) {
		list_for_each_entry_safe(cur_cmd, tmp_cmd, &session->cmds[pri], list[pri]) {
			/* rsp didn't make it to rsps list, free it now */
			kfree(cur_cmd->rsp);

			list_del(&cur_cmd->list[cur_cmd->user_cmd.priority]);
			vha->pri_q_counters[cur_cmd->user_cmd.priority] -=
								(VHA_CMD_SUBSEG_NUM(cur_cmd) - cur_cmd->subseg_current);
			if (vha_observers.canceled)
				vha_observers.canceled(vha->id, session->id, cur_cmd->user_cmd.cmd_id,
										cur_cmd->user_cmd.priority);
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
	bool pend_removed = false;
	uint32_t pend_aborted_kicks_adj_val = 0;
	bool queued_removed = false;
	uint32_t queued_aborted_kicks_adj_val = 0;

	/* Check if pend/queued commands will be removed. */
	if (vha->pendcmd[VHA_CNN_CMD].cmd &&
			(vha->pendcmd[VHA_CNN_CMD].cmd->session == session) &&
			(vha->pendcmd[VHA_CNN_CMD].cmd->user_cmd.cmd_id & cmd_id_mask)
																	== cmd_id) {
		pend_removed = true;
		vha->stats.cnn_kicks_cancelled += vha->pendcmd[VHA_CNN_CMD].cmd->subseg_current;
		pend_aborted_kicks_adj_val = vha->pendcmd[VHA_CNN_CMD].cmd->subseg_current;
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
		cancel_delayed_work(&vha->dummy_dwork);
#endif
	}
	if (vha->queuedcmd[VHA_CNN_CMD].cmd &&
			(vha->queuedcmd[VHA_CNN_CMD].cmd->session == session) &&
			(vha->queuedcmd[VHA_CNN_CMD].cmd->user_cmd.cmd_id & cmd_id_mask)
																	== cmd_id) {
		queued_removed = true;
		vha->stats.cnn_kicks_cancelled += vha->queuedcmd[VHA_CNN_CMD].cmd->subseg_current;
		queued_aborted_kicks_adj_val = vha->pendcmd[VHA_CNN_CMD].cmd->subseg_current;
	}

	/* Update session scheduling. */
	if (vha->queuedcmd[VHA_CNN_CMD].cmd &&
			(pend_removed && !queued_removed)) {
		uint8_t pri = vha->queuedcmd[VHA_CNN_CMD].cmd->user_cmd.priority;
		if (vha->queuedcmd[VHA_CNN_CMD].cmd->session !=
					list_entry(&vha->sched_sessions[pri], struct vha_session,
								sched_list[pri]))
			while(list_first_entry(&vha->sched_sessions[pri], struct vha_session,
						sched_list[pri]) != vha->queuedcmd[VHA_CNN_CMD].cmd->session)
				list_rotate_left(&vha->sched_sessions[pri]);
	}

	/* Remove pend/queued commands if needed. */
	if (pend_removed || queued_removed) {
		vha_rollback_cnn_cmds(vha);
		/* Correct aborted stats. */
		if (queued_removed)
			vha->stats.cnn_kicks_aborted -= queued_aborted_kicks_adj_val;
		if (pend_removed)
			vha->stats.cnn_kicks_aborted -= pend_aborted_kicks_adj_val;
		reschedule = true;
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
				case VHA_CMD_CNN_SUBMIT:
				{
					struct vha_user_cnn_submit_cmd *cnn_cmd =
							(struct vha_user_cnn_submit_cmd *)&cur_cmd->user_cmd;
					int j;
					for (j = 0; j < (cnn_cmd->msg.num_bufs - 1); j++) {
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
				vha->pri_q_counters[cur_cmd->user_cmd.priority] -=
								(VHA_CMD_SUBSEG_NUM(cur_cmd) - cur_cmd->subseg_current);
				if (vha_observers.canceled)
					vha_observers.canceled(vha->id, session->id, cur_cmd->user_cmd.cmd_id,
											cur_cmd->user_cmd.priority);
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
#ifndef CONFIG_VHA_DUMMY
	if (!vha->is_ready)
		return true;
#endif

	if (vha->low_latency != VHA_LL_DISABLED) {
		return vha->pendcmd[VHA_CNN_CMD].cmd != NULL ||
				vha->queuedcmd[VHA_CNN_CMD].cmd != NULL;
	}
	return vha->pendcmd[VHA_CNN_CMD].cmd != NULL;
}

/* returns true if the cmd queue is full */
bool vha_is_queue_full(struct vha_dev *vha, struct vha_cmd *cmd)
{
	if (vha->low_latency != VHA_LL_DISABLED) {
		if (vha->low_latency == VHA_LL_SELF_KICK
#ifdef HW_AX3
			/* if current command we are trying to queue belongs to a different session than pending one */
			&& (vha->pendcmd[VHA_CNN_CMD].cmd != NULL && cmd != NULL &&
					vha->pendcmd[VHA_CNN_CMD].cmd->session != cmd->session)
			/* if session of the command we are trying to queue, shares the hw mmu ctx with the session of pending cmd */
			&& (cmd->session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id ==
					vha->pendcmd[VHA_CNN_CMD].cmd->session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id)
			/* Sanity if hw mmu ctx is really shared at this point */
			&& (vha->mmu_ctxs[cmd->session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id] > 1)
			) {
#else
			) {
			dev_warn(vha->dev, "%s: LL=2 not supported!\n", __func__);
#endif
			/* skip low latency mode */
			return vha->pendcmd[VHA_CNN_CMD].cmd != NULL;
		}

		return vha->pendcmd[VHA_CNN_CMD].cmd != NULL &&
					vha->queuedcmd[VHA_CNN_CMD].cmd != NULL;
	}
	return vha->pendcmd[VHA_CNN_CMD].cmd != NULL;
}

/* check all input buffers are filled and ready to go */
bool vha_is_waiting_for_inputs(struct vha_session *session,
	struct vha_cmd *cmd)
{
	if (!cmd->inbufs_ready) {
		const struct vha_user_cnn_submit_cmd *user_cmd =
			(struct vha_user_cnn_submit_cmd *)&cmd->user_cmd;
		int i;

		for (i = 0; i < cmd->user_cmd.num_inbufs - 1; i++) {
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

static bool vha_can_schedule(struct vha_dev *vha)
{
#ifndef CONFIG_VHA_DUMMY
	if (!vha->is_ready)
		return false;
#endif

	if (vha->low_latency != VHA_LL_DISABLED) {
		return vha->pendcmd[VHA_CNN_CMD].cmd == NULL ||
				vha->queuedcmd[VHA_CNN_CMD].cmd == NULL;
	}
	return vha->pendcmd[VHA_CNN_CMD].cmd == NULL;
}

static void vha_scheduler_set_starting_session(struct vha_dev *vha,
	uint8_t priority, struct vha_session *session, bool set_next)
{
	/* Rotate scheduling list to the current session
	 * to make it a starting point for the next scheduling round. */
	if (session != list_entry(&vha->sched_sessions[priority],
								struct vha_session, sched_list[priority]))
		while(list_first_entry(&vha->sched_sessions[priority],
								struct vha_session, sched_list[priority]) != session)
			list_rotate_left(&vha->sched_sessions[priority]);
	/* Set a starting point session for the next scheduling round
	 * to next to the current one if requested. */
	if (set_next)
		list_rotate_left(&vha->sched_sessions[priority]);
}

static uint8_t vha_scheduler_get_priority(struct vha_dev *vha)
{
	uint8_t pri;

	/* Calculate current total window width. */
	for (pri = VHA_MAX_PRIORITIES - 1; (int8_t)pri >= 0; pri--)
		if (vha->pri_q_counters[pri] > 0)
			return pri;

	/* If there's no priority with WLs to schedule, just return 0. */
	return VHA_INVALID_PRI;
}

void vha_scheduler_loop(struct vha_dev *vha)
{
	struct vha_cmd *cmd, *tmp;
	struct vha_session *session = NULL;
	enum do_cmd_status cmd_status = CMD_OK;
	bool scheduled = false;
	uint8_t current_pri = VHA_DEFAULT_PRI;

	if (vha_is_queue_full(vha, NULL)) {
		/* Postpone worker task if command queue is full. */
		dev_dbg(vha->dev, "%s Queue full. Postpone worker task!\n", __func__);
		return;
	}

	do {
		scheduled = false;
		current_pri = vha_scheduler_get_priority(vha);
		if (current_pri == VHA_INVALID_PRI)
			break;
		list_for_each_entry(session, &vha->sched_sessions[current_pri], sched_list[current_pri]) {
			list_for_each_entry_safe(cmd, tmp, &session->cmds[current_pri], list[current_pri]) {

				/* For hw commands... */
				if (CMD_EXEC_ON_HW(cmd)) {
					if (!VHA_IS_DUMMY(vha)) {
						/* Start device. */
						if(vha_dev_start(vha))
							return;
					}
				}

				/* Skip this workload as it's already scheduled. */
				if (cmd->queued || cmd->in_hw)
					continue;

				dev_dbg(vha->dev, "%s cur_prio=<%d>\n", __func__,current_pri);
				/* Attempt to schedule command for execution. */
				cmd_status = vha_do_cmd(cmd);

				/* Update scheduling loop based on command scheduling status. */
				if ((cmd_status == CMD_OK) || (cmd_status == CMD_HW_BUSY)) {
					bool set_next = false;
					if (cmd_status == CMD_OK) {
						scheduled = true;
						if (cmd->subseg_current == VHA_CMD_SUBSEG_NUM(cmd))
							set_next = true;
					}
					vha_scheduler_set_starting_session(vha, current_pri, session, set_next);
					goto exit_session_loop;
				}
			}
		}
exit_session_loop:;
	/* Iterate until a workload was scheduled and no other can be scheduled. */
	} while (vha_can_schedule(vha) && scheduled);

	if (!VHA_IS_DUMMY(vha)) {
		/* Schedule APM if needed */
		if (!vha_is_busy(vha) &&
				!vha->no_clock_disable) {
			if (!vha->pm_delay) {
				if (vha_dev_stop(vha, false)) {
					dev_warn(vha->dev, "%s: Failed to soft stop device. trying with reset",
						__func__);
					if (vha_dev_stop(vha, true))
						dev_err(vha->dev, "%s: Failed to stop device with reset!", __func__);
				}
			}
			else {
				vha->apm_dworks[0].delay_ms = vha->pm_delay;
				vha_sched_apm(vha, &vha->apm_dworks[0]);
			}
		}
	}
}

void vha_dev_apm_stop(struct vha_dev *vha, struct vha_apm_work *apm_work)
{
	if (!vha->do_calibration &&
			(vha->pendcmd[VHA_CNN_CMD].cmd == NULL &&
			vha->queuedcmd[VHA_CNN_CMD].cmd == NULL))
		if (vha_dev_stop(vha, false)) {
			dev_warn(vha->dev, "%s: Failed to soft stop device. trying with reset",
				__func__);
			if (vha_dev_stop(vha, true))
				dev_err(vha->dev, "%s: Failed to stop device with reset!", __func__);
		}
}

int vha_dev_get_props(struct vha_dev *vha, uint32_t onchipmem_size)
{
	struct vha_hw_props *props = &vha->hw_props;
	uint64_t ip_config;
	uint32_t ocm_size_kb = 0;

	memset(props, 0, sizeof(*props));

#ifdef CONFIG_VHA_DUMMY
	/* Note: dummy dev always reads zeroes from registers */
	props->product_id  = 0x8070605040302010ULL;
	props->core_id  = (long)HW_SERIES << (int)VHA_CR_CORE_ID_BRANCH_ID_SHIFT;
	props->core_id += 0x010203040505ULL;   // provide a dummy core id
	props->dummy_dev = true;
	props->num_cnn_core_devs = 1;
#else
	props->product_id  = IOREAD64(vha->reg_base, VHA_CR_PRODUCT_ID);
	props->core_id  = IOREAD64(vha->reg_base, VHA_CR_CORE_ID);
#endif
	props->skip_bvnc_check = false;
	/*
	 * New mmu version 3 and onwards operates on 40bit physical & virtual addresses
	 */
	props->mmu_width = 40;

	/* HW from 1.1 onwards */
	ip_config = IOREAD64(vha->reg_base, VHA_CR_CORE_IP_CONFIG);
#ifdef HW_AX3
	props->mmu_ver = VHA_CR_GETBITS(CORE_IP_CONFIG, MMU_VERSION, ip_config);
#endif
	/* Mirage uses MMU version 3 hardware */
	if (!props->mmu_ver)
		props->mmu_ver = 3;
	if (VHA_CR_GETBITS(CORE_IP_CONFIG, CNN_SUPPORTED, ip_config))
		props->num_cnn_core_devs = 1;
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

	props->soc_axi  = IOREAD64(vha->reg_base, VHA_CR_SOC_AXI);

	dev_info(vha->dev, "%s: Product id: %#llx\n",
			__func__, props->product_id);
	dev_info(vha->dev, "%s: Core id: %#llx\n",
			__func__, props->core_id);
	dev_info(vha->dev, "%s: MMU version:%d (%dbit)\n",
			__func__, props->mmu_ver, props->mmu_width);
	dev_dbg(vha->dev, "%s: supported: %#x\n",
			__func__, props->features);
	dev_dbg(vha->dev, "%s: soc_axi: %#llx\n",
			__func__, props->soc_axi);
	{
		uint64_t tmp = IOREAD64(vha->reg_base,
				VHA_CR_CORE_IP_INTEGRATOR_ID);
		dev_dbg(vha->dev, "%s: ip integrator id: %#llx\n",
				__func__, tmp);
		tmp = IOREAD64(vha->reg_base, VHA_CR_CORE_IP_CHANGELIST);
		dev_dbg(vha->dev, "%s: ip change list: %llu\n", __func__, tmp);
	}

#if defined(CFG_SYS_VAGUS)
	ocm_size_kb = IOREAD64(vha->reg_base, NN_SYS_CR(CORE_IP_CONFIG)) &
				~NN_SYS_CR_CORE_IP_CONFIG_NN_SYS_OCM_RAM_SIZE_4KB_CLRMSK;
	ocm_size_kb *= 4;
#endif

	if (ocm_size_kb) {
		vha->hw_props.locm_size_bytes = ocm_size_kb * 1024;
		/* User may wanted to limit OCM ... */
		if (onchipmem_size) {
			if (onchipmem_size < vha->hw_props.locm_size_bytes) {
				dev_warn(vha->dev, "%s:Limiting onchip memory to %u bytes (available:%u)\n",
						__func__, onchipmem_size, vha->hw_props.locm_size_bytes);
				vha->hw_props.locm_size_bytes = onchipmem_size;
			} else if (onchipmem_size > vha->hw_props.locm_size_bytes) {
				dev_err(vha->dev, "%s: User defined onchip memory size exceeded (%u > %u))\n",
						__func__, onchipmem_size, vha->hw_props.locm_size_bytes);
			}
		}
	} else {
		vha->hw_props.locm_size_bytes = onchipmem_size;
	}

	dev_info(vha->dev, "%s: Total onchip memory: %u [kB]\n",
			__func__, vha->hw_props.locm_size_bytes / 1024);

	dev_info(vha->dev, "%s: Devices: DUMMY:%u CNN:%u\n", __func__,
			props->dummy_dev ? props->num_cnn_core_devs : 0,
			props->dummy_dev ? 0 : props->num_cnn_core_devs);

	return 0;
}

void vha_dev_ocm_configure(struct vha_dev *vha)
{
#if defined(CFG_SYS_VAGUS)
	dev_dbg(vha->dev, "%s: OCM address range: %#lx - %#lx\n",
			__func__, vha->ocm_paddr,
			vha->ocm_paddr + vha->hw_props.locm_size_bytes - 1);
	IOWRITE64(vha->reg_base, NN_SYS_CR(NOC_LOWER_ADDR1), vha->ocm_paddr);
	IOWRITE64(vha->reg_base, NN_SYS_CR(NOC_UPPER_ADDR1),
			vha->ocm_paddr + vha->hw_props.locm_size_bytes - 1);
	img_pdump_printf("-- Setup NN_SYS OCM phys address range\n"
		"WRW "_PMEM_":$0 :OCM:BLOCK_CACHE:0x0\n"
		"WRW64 :REG_NNSYS:%#x "_PMEM_":$0\n"
		"WRW "_PMEM_":$0 :OCM:BLOCK_CACHE:%#x\n"
		"WRW64 :REG_NNSYS:%#x "_PMEM_":$0\n",
		NN_SYS_CR_NOC_LOWER_ADDR1, vha->hw_props.locm_size_bytes-1,
		NN_SYS_CR_NOC_UPPER_ADDR1);
#endif
}

/* prepare CRC and DEBUG data buffers */
void vha_dbg_prepare_hwbufs(struct vha_session *session, struct vha_cmd *cmd,
		struct vha_crc_config_regs *regs)
{
	struct vha_dev *vha = session->vha;
	(void)cmd;

	if (session->cnn_dbg.cnn_crc_buf[0]) {
		struct vha_buffer *buf = session->cnn_dbg.cnn_crc_buf[0];
		uint64_t val64;

		/* enable CRC: address + mode */
		val64 = VHA_CR_SETBITS_OS(CNN_CRC_CONTROL, CNN_CRC_ENABLE,
				session->cnn_dbg.cnn_crc_mode);
		img_pdump_printf("-- CRC_CONTROL=%u buf 'CRC' size=%zx\n",
				session->cnn_dbg.cnn_crc_mode, buf->size);
		IOWRITE_PDUMP_BUFADDR(session, buf, 0, VHA_CR_OS(CNN_CRC_ADDRESS));

		IOWRITE64_PDUMP(val64, VHA_CR_OS(CNN_CRC_CONTROL));

#ifdef HW_AX3
		img_pdump_printf("-- CRC_MASK=%#x\n", session->cnn_dbg.cnn_crc_mask);
		IOWRITE64_PDUMP(session->cnn_dbg.cnn_crc_mask, VHA_CR_OS(CNN_CRC_MASK_CTRL));
#endif
	}
	if (session->cnn_dbg.cnn_dbg_buf[0] && session->cnn_dbg.cnn_dbg_pdump_enable) {
		struct vha_buffer *buf = session->cnn_dbg.cnn_dbg_buf[0];
		uint64_t val64;

		/* enable DEBUG: address, perf mode, band mode */
		img_pdump_printf("-- DEBUG_CONTROL=%u,%u buf 'DBG' size=%zx\n",
				GET_CNN_DBG_MODE(PERF, session), GET_CNN_DBG_MODE(BAND, session),
				buf->size);
		IOWRITE_PDUMP_BUFADDR(session, buf, 0,
							VHA_CR_OS(CNN_DEBUG_ADDRESS));
		val64 = VHA_CR_ALIGN_SETBITS_OS(CNN_DEBUG_SIZE,
								CNN_DEBUG_SIZE,
								buf->size);
		IOWRITE64_PDUMP(val64, VHA_CR_OS(CNN_DEBUG_SIZE));

		/* Set the CONTROL register only if requested */
		if (CNN_DBG_MODE_ON(PERF, session) || CNN_DBG_MODE_ON(BAND, session)) {
			val64 = VHA_CR_SETBITS_OS(CNN_DEBUG_CONTROL, CNN_PERF_ENABLE,
										GET_CNN_DBG_MODE(PERF, session));
			val64 |= VHA_CR_SETBITS_OS(CNN_DEBUG_CONTROL, CNN_BAND_ENABLE,
										GET_CNN_DBG_MODE(BAND, session));
			IOWRITE64_PDUMP(val64, VHA_CR_OS(CNN_DEBUG_CONTROL));
		}
	}
}

/* flush CRC and DEBUG data buffers */
void vha_dbg_flush_hwbufs(struct vha_session *session, char checkpoint, uint8_t mask)
{
	struct vha_dev* vha = session->vha;
	(void)mask;
	if (session->cnn_dbg.cnn_dbg_flush != checkpoint)
		return;

	if (session->cnn_dbg.cnn_crc_buf[0]) {
		struct vha_buffer *buf = session->cnn_dbg.cnn_crc_buf[0];
		/*
		 * TOBEDONE: calculate CRC buffer size based
		 * on num passes, num layers, etc
		 */
		img_pdump_printf("-- Save signatures\n");
		img_pdump_printf("IF CHECK_CRCS\n");
		img_pdump_printf("COM Checking CRCs ...\n");
		vha_pdump_sab_buf(session, PDUMP_CRC,
					buf, 0, buf->size);
		img_pdump_printf("ELSE CHECK_CRCS\n");
		img_pdump_printf("COM Not checking CRCs!\n");
		img_pdump_printf("FI CHECK_CRCS\n");
	}
	if (session->cnn_dbg.cnn_dbg_buf[0] && session->cnn_dbg.cnn_dbg_pdump_enable) {
		struct vha_buffer *buf = session->cnn_dbg.cnn_dbg_buf[0];
		/* read the size of the DEBUG buffer */
		uint64_t size = IOREAD64(vha->reg_base, VHA_CR_OS(CNN_DEBUG_STATUS));
		/*
		 * SAB the DBG buffer, even though "it is not deterministic"
		 */
		size = VHA_CR_GETBITS_OS(CNN_DEBUG_STATUS,
							CNN_DEBUG_OFFSET,
							size);
		img_pdump_printf("-- Save DEBUG info\n");
		
		vha_pdump_sab_buf(session, PDUMP_DBG, buf, 0, buf->size);
	}
}

/* stop capturing CRC and DEBUG data */
void vha_dbg_stop_hwbufs(struct vha_session *session, uint8_t mask)
{
	struct vha_dev *vha = session->vha;
	(void)mask;

	/* Flush hw debug buffers */
	vha_dbg_flush_hwbufs(session, 0, 0);

	if (session->cnn_dbg.cnn_crc_buf[0]) {
		IOWRITE64_PDUMP(0, VHA_CR_OS(CNN_CRC_CONTROL));
	}
	if (session->cnn_dbg.cnn_dbg_buf[0]) {
		/* read the size of the DEBUG buffer */
		uint64_t size = IOREAD64(vha->reg_base, VHA_CR_OS(CNN_DEBUG_STATUS));

		if (CNN_DBG_MODE_ON(PERF, session) || CNN_DBG_MODE_ON(BAND, session)) {
			IOWRITE64_PDUMP(0, VHA_CR_OS(CNN_DEBUG_CONTROL));
			/* just give a hint in the pdump:
			 * dummy device returns 0 */
			img_pdump_printf(
					"-- POL64 :REG:%#x 0 0 0 1 1 -- DEBUG_STATUS=%llx\n",
					 VHA_CR_OS(CNN_DEBUG_STATUS),
				size);
		}
	}
}

uint64_t vha_dbg_rtm_read(struct vha_dev *vha, uint64_t addr)
{
	/* Turn on all clocks forcefully */
	IOWRITE64(vha->reg_base, VHA_CR_SYS_CLK_CTRL0, VHA_SYS_CLOCKS_DEFAULT(ON));
	IOWRITE64(vha->reg_base, VHA_CR_CLK_CTRL0, VHA_MAIN_CLOCKS_DEFAULT(ON));

	/* Set up address of the signal */
	IOWRITE64(vha->reg_base, VHA_CR_RTM_CTRL, addr | VHA_CR_RTM_CTRL_RTM_ENABLE_EN);

	
	/* but N_OF_RTM_STAGES is not accessible by SW*/
	/* so waiting 1 ms for now */
	msleep(1);

	/* Read the data */
	return IOREAD64(vha->reg_base, VHA_CR_RTM_DATA);
}

int vha_currcmd_exetime_req(struct vha_dev *vha, uint64_t *proc_us)
{
	uint64_t proc_time = 0;
	struct vha_cmd *cmd = NULL;
	struct TIMESPEC to;

	cmd = vha->pendcmd[VHA_CNN_CMD].cmd;
	if(!cmd || !cmd->in_hw) {
		goto err_out;
	}

	{
		struct TIMESPEC from = vha->stats.hw_proc_start;
		GETNSTIMEOFDAY(&to);

		if (cmd->subsegs_completed == cmd->subseg_current) {
			*proc_us = 0;
		} else if (get_timespan_us(&from, &to, &proc_time)) {
			*proc_us = proc_time;
		} else {
			goto err_out;
		}
	}

	return 0;

err_out:
	*proc_us = 0;
	return -1;
}

/* List of predefined registers to be shown in debugfs */
const struct vha_reg vha_regs[] = {
#define REG_DESC(reg) VHA_CR_##reg, VHA_CR_##reg##_MASKFULL
#define REG_DESC_OS(reg) VHA_CR_OS(reg), VHA_CR_OS(reg##_MASKFULL)
	{"main_clocks_control  ", REG_DESC(CLK_CTRL0)},
	{"main_clocks_status   ", REG_DESC(CLK_STATUS0)},
	{"sys_clocks_control   ", REG_DESC(SYS_CLK_CTRL0)},
	{"sys_clocks_status    ", REG_DESC(SYS_CLK_STATUS0)},
	{"product_id           ", REG_DESC(PRODUCT_ID)},
	{"core_id              ", REG_DESC(CORE_ID)},
	{"soc_axi              ", REG_DESC(SOC_AXI)},
	{"integrator_id        ", REG_DESC(CORE_IP_INTEGRATOR_ID)},
	{"ip_changelist        ", REG_DESC(CORE_IP_CHANGELIST)},
	{"core_ip_config       ", REG_DESC(CORE_IP_CONFIG)},
	{"reset                ", REG_DESC(RESET_CTRL)},
	{"event_enable         ", REG_DESC_OS(VHA_EVENT_ENABLE)},
	{"event_status         ", REG_DESC_OS(VHA_EVENT_STATUS)},
	{"cnn_control          ", REG_DESC_OS(CNN_CONTROL)},
	{"cnn_status           ", REG_DESC_OS(CNN_STATUS)},
#ifdef HW_AX2
	{"cnn_wdt_cmpmatch     ", REG_DESC(CNN_WDT_COMPAREMATCH)},
	{"cnn_wdt_control      ", REG_DESC(CNN_WDT_CTRL)},
	{"cnn_wdt_timer        ", REG_DESC(CNN_WDT_TIMER)},
#endif
	{"cnn_mem_wdt_cmpmatch ", REG_DESC(CNN_MEM_WDT_COMPAREMATCH)},
	{"cnn_mem_wdt_control  ", REG_DESC(CNN_MEM_WDT_CTRL)},
	{"cnn_mem_wdt_timer    ", REG_DESC(CNN_MEM_WDT_TIMER)},
	{"mmu_control          ", REG_DESC_OS(MMU_CTRL)},
	{"mmu_context          ", REG_DESC_OS(MMU_CBASE_MAPPING_CONTEXT)},
	{"mmu_mapping          ", REG_DESC_OS(MMU_CBASE_MAPPING)},
	{"mmu_status           ", REG_DESC(MMU_STATUS)},
	{"mmu_fault_status1    ", REG_DESC_OS(MMU_FAULT_STATUS1)},
	{"mmu_fault_status2    ", REG_DESC_OS(MMU_FAULT_STATUS2)},
	{"slc_control          ", REG_DESC(SLC_CTRL)},
#if 0
	{"slc_bypass_control   ", REG_DESC(SLC_BYPASS_CTRL)},
#endif
	{"slc_status1          ", REG_DESC(SLC_STATUS1)},
	{"slc_status2          ", REG_DESC(SLC_STATUS2)},
	{"slc_status3          ", REG_DESC(SLC_STATUS3)},
	{"slc_idle             ", REG_DESC(SLC_IDLE)},
	{"bif_outstanding_read ", REG_DESC(BIF_OUTSTANDING_READ)},
#undef REG_DESC
#undef REG_DESC_OS
	{NULL                   , 0},
};


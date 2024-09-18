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
#include <linux/moduleparam.h>
#include <linux/delay.h>

#include <uapi/vha.h>
#include "vha_common.h"
#include "vha_plat.h"
#include "vha_regs.h"

#include <vha_trace_point.h>

static uint32_t cnn_pdump_poll_count = 10000000;
module_param(cnn_pdump_poll_count, uint, 0444);
MODULE_PARM_DESC(cnn_pdump_poll_count,
		"PDUMP: Number of times to poll for CNN status");

static bool cnn_preloads_disable;
module_param(cnn_preloads_disable, bool, 0444);
MODULE_PARM_DESC(cnn_preloads_disable,
		"Disables CNN preloads");

/*
 * submit a command stream to the CNN hardware
 * input buffers:
 *   command
 *   input
 *   coeff
 * output buffers:
 *   output
 *   accum_load
 * data:
 *   none
 */
static int do_cmd_cnn_submit(struct vha_cmd *cmd)
{
	int i;
	uint32_t val32;
	const struct vha_user_cnn_submit_cmd *user_cmd =
		(struct vha_user_cnn_submit_cmd *)&cmd->user_cmd;
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	struct vha_buffer *buf = NULL;
	struct vha_onchip_map *onchip_map = NULL;
	int ret = -EINVAL;
	uint64_t alt_addrs_used = 0;
	size_t user_cmd_size;

	if (vha->hw_bypass) {
		ret = -EAGAIN;
		dev_info(vha->dev, "%s skip\n", __func__);
		goto out_error;
	}

	img_pdump_printf("-- CNN_SETUP_BEGIN\n");
	/* Wait for the previous kick to be accepted */
	if (vha->low_latency != VHA_LL_DISABLED) {
		/* Sanity wait for the kick bit to be deasserted */
		IOPOLL64_PDUMP(0, 1000, 10, (uint64_t)VHA_CR_OS(CNN_CONTROL_START_EN),
							VHA_CR_OS(CNN_CONTROL));
		if (cmd->queued &&
				vha->low_latency == VHA_LL_SW_KICK)
			goto hw_kick;
	}

	if (vha->pendcmd[VHA_CNN_CMD].cmd != NULL &&
				vha->low_latency == VHA_LL_DISABLED) {
		dev_err(vha->dev, "%s: trying to submit cnn cmd when hw busy!\n",
			__func__);
		goto out_error;
	}

	user_cmd_size = sizeof(*user_cmd);
	if (user_cmd->subseg_num > 0)
		user_cmd_size += (user_cmd->subseg_num - 1) * sizeof(struct vha_subseg_info);
	if (cmd->size != user_cmd_size) {
		dev_err(vha->dev, "%s: command buffer wrong size: %zu/%zu",
			__func__, cmd->size, sizeof(*user_cmd));
		goto out_error;
	}

	if (!vha_dev_check_hw_capab(vha, user_cmd->expected_ip_capab)) {
		ret = -ENODEV;
		goto out_error;
	}

	/* at least CMD and (IN or OUT)*/
	if (user_cmd->msg.num_inbufs < 2 ||
		/* and maybe TMP and others */
		user_cmd->msg.num_bufs > VHA_CORE_MAX_ALT_ADDRS) {
		dev_err(vha->dev, "%s: wrong number of bufs: %u,%u\n",
			__func__,
			user_cmd->msg.num_inbufs, user_cmd->msg.num_bufs);
		goto out_error;
	}

	if (user_cmd->onchipram_map_id != 0) {
		onchip_map = idr_find(&session->onchip_maps, user_cmd->onchipram_map_id);
		if (!onchip_map) {
			dev_warn(vha->dev, "%s: idr_find failed\n", __func__);
		}
	}

	/*
	 * write buffer address to each register,
	 * and pdump LDB each of the the input buffers
	 */
	img_pdump_printf("-- Load inputs\n");
	for (i = 0; i < user_cmd->msg.num_bufs; i++) {
		uint32_t offset;
		uint32_t size;
		uint32_t reg;

		/* buffer id == 0 means no buffer */
		if (user_cmd->msg.data[i] == 0)
			continue;

		buf = vha_find_bufid(session, user_cmd->msg.data[i]);
		if (buf == NULL) {
			dev_err(vha->dev, "%s: invalid buffer id:%d\n",
				__func__, user_cmd->msg.data[i]);
			goto out_error;
		}
		if (buf->id == user_cmd->cmdbuf) {
			/* cmdstream always starts at offset 0 */
			if (user_cmd->subseg_info[cmd->subseg_current].cmdbuf_size)
				size = cmd->stream_size = user_cmd->subseg_info[cmd->subseg_current].cmdbuf_size;
			else
				size = cmd->stream_size = buf->size;

			offset = user_cmd->subseg_info[cmd->subseg_current].cmdbuf_offset;
			if (size == 0) {
				dev_err(vha->dev,
					"%s: invalid cmdstream size\n",
					__func__);
				goto out_error;
			}
			reg = VHA_CR_OS(CNN_CMD_BASE_ADDRESS);
			img_pdump_printf("-- Setup command stream\n");
		} else {
			/*
			 * offset can be specified for all
			 * buffers except cmdstream buf
			 */
			offset = user_cmd->bufoffsets[i-1];
			size = user_cmd->bufsizes[i-1];

			if (size + offset > buf->size) {
				dev_err(vha->dev,
					"%s: invalid size+offset: %x+%x > %zx\n",
					__func__, size, offset, buf->size);
				goto out_error;
			}

			reg = VHA_CR_OS(CNN_ALT_ADDRESS0)
				+ user_cmd->regidx[i-1]
				* (VHA_CR_OS(CNN_ALT_ADDRESS1)
				- VHA_CR_OS(CNN_ALT_ADDRESS0));
			/* record what alt address is in use */
			alt_addrs_used |= 1 << user_cmd->regidx[i-1];
#if defined(HW_AX3)
			/* Alternative addresses from 8 to 15 are
			 * located in different place */
			if (user_cmd->regidx[i-1] >= 8) {
				reg = VHA_CR_OS(CNN_ALT_ADDRESS8)
				+ (user_cmd->regidx[i-1] - 8)
				* (VHA_CR_OS(CNN_ALT_ADDRESS1)
				- VHA_CR_OS(CNN_ALT_ADDRESS0));
			}
			alt_addrs_used |= buf->req_type <<
				(VHA_CR_OS(CNN_ALT_ADDRESS_USED_ALT_ADDR0_BUF_TYPE_SHIFT) +
				user_cmd->regidx[i-1]);
#elif defined(HW_AX2)
			if (user_cmd->regidx[i-1] > 8) {
				dev_err(vha->dev,
						"%s: extended alternative addresses not supported!\n",
						__func__);
				goto out_error;
			}
#endif
		}
		/* pdump the input buffers (not filled by the hw),
		 * try to cache buffers filled by SW,
		 * to avoid unnecessary LDBs */
		if (i < user_cmd->msg.num_inbufs &&
				!(buf->status == VHA_BUF_FILLED_BY_HW))
			vha_pdump_ldb_buf(session, PDUMP_PRM,
					buf, offset, size,
					buf->status == VHA_BUF_FILLED_BY_SW);

		vha_dump_digest(session, buf, cmd);
		/*
		 * write to all of the index registers.
		 * in no-MMU mode, write phys address of a contig buffer.
		 * in MMU mode, write virt address of buffer.
		 * If onchip_map selected, use different virt address of buffer
		 */
		if (onchip_map != NULL && onchip_map->bufid == buf->id)
			IOWRITE64_PDUMP(onchip_map->devvirt + offset, reg);
		else
			IOWRITE_PDUMP_BUFADDR(session, buf, offset, reg);

		if (vha_buf_needs_flush(session, buf->id))
			img_mem_sync_cpu_to_device(session->mem_ctx, buf->id);
	}

	if (!cnn_preloads_disable) {
		/* Inform the hw what alt addresses are in use,
		 * so the command decoder can prefetch */
		img_pdump_printf("-- Setup CNN prefetch register\n");
		IOWRITE64_PDUMP(alt_addrs_used, VHA_CR_OS(CNN_ALT_ADDRESS_USED));
	}

	/* write the stream size only */
	ret = 0;
	if (vha->pendcmd[VHA_CNN_CMD].cmd) {
		vha->queuedcmd[VHA_CNN_CMD].cmd = cmd;
		cmd->queued = true;
		vha->stats.cnn_kicks_queued++;
		img_pdump_printf("-- CNN already kicked queueing!\n");
		dev_dbg(vha->dev, "%s: -> kicked:%p queueing:%p\n",
					__func__, vha->pendcmd[VHA_CNN_CMD].cmd, cmd);
		if (vha->low_latency == VHA_LL_SW_KICK)
			return ret;
	}
hw_kick:
	/* Change mmu context */
	ret = vha_mmu_setup(cmd->session);
	if (ret) {
		dev_err(vha->dev,
			"%s: Error during MMU setup!\n", __func__);
			goto out_error;
	}
	/* Setup memory stuff */
	vha_dev_mh_setup(vha, session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id, NULL);

	/* Prepare debug buffer registers */
	vha_dbg_prepare_hwbufs(session, cmd, NULL);

	/* Setup cnn hw watchdog before kicking the hw */
	{
		uint64_t cycles, mode;

		ret = vha_dev_hwwdt_calculate(vha, cmd, &cycles, &mode);
		if (!ret)
			vha_dev_hwwdt_setup(session->vha, cycles, mode);
		else if (ret != -EIO) {
			dev_err(vha->dev,
				"%s: can't obtain HWWDT info!\n",
				__func__);
				goto out_error;
		}
	}

	if (CMD_EXEC_ON_HW(cmd)) {
		cmd->in_hw = true;
		if (!cmd->queued)
			vha->pendcmd[VHA_CNN_CMD].cmd = cmd;
	}
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
	/* Mark kick for dummy driver */
	cmd->dummy_kicked = true;
#endif

	/* Consider this cmd as kicked. */
	vha->pri_q_counters[cmd->user_cmd.priority]--;
	cmd->subseg_current++;

	ret = 0;
	/* Setup kick info */
	val32 = vha_dev_kick_prepare(vha, cmd,
			session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id);

	img_pdump_printf("-- CNN_SETUP_END\n");

	/* Remember the time cnn is kicked */
	trace_vha_hwexec_in(session->id, cmd->subseg_current);
	GETNSTIMEOFDAY(&cmd->hw_proc_start);
	vha->stats.hw_proc_start = cmd->hw_proc_start;
	/* Need to generate proper pdump */
	if (cmd->queued &&
			vha->low_latency == VHA_LL_SW_KICK) {
		/* Do not write to pdump
		 * this needs to be done after irq POL*/
		IOWRITE64(vha->reg_base, VHA_CR_OS(CNN_CONTROL), val32);
		dev_dbg(vha->dev, "%s: CNN kick queued (%p)!\n",
					__func__, cmd);
		cmd->queued = false;
	} else {
		img_pdump_printf("-- CNN_KICK_BEGIN\n");
		img_pdump_printf("-- CNN kick!\n");
		IOWRITE64_PDUMP(val32, VHA_CR_OS(CNN_CONTROL));
		dev_dbg(vha->dev, "%s: CNN kick %s (%p)!\n",
					__func__, cmd->queued ? "queued" : "", cmd);
		img_pdump_printf("-- CNN_KICK_END\n");
	}

	vha->stats.cnn_kicks++;

	/* notify any observers of the submit event */
	if (vha_observers.submitted)
		vha_observers.submitted(vha->id, session->id, cmd->user_cmd.cmd_id,
								(cmd->subseg_current == VHA_CMD_SUBSEG_NUM(cmd)),
								cmd->user_cmd.priority);

out_error:
	if (ret != 0) {
		/* Consider this cmd as kicked for errors too. */
		vha->pri_q_counters[cmd->user_cmd.priority]--;
		cmd->subseg_current++;
	}
	return ret;
}

/*
 * append a string to the pdump TXT file
 * buffers:
 *   none
 * data:
 *   string to be printed
 */
static int do_cmd_cnn_pdump_msg(const struct vha_cmd *cmd)
{
	const struct vha_user_cmd *user_cmd = &cmd->user_cmd;
	struct vha_session *session = cmd->session;
	struct vha_dev* vha = session->vha;
	int ret = 0;

	if (user_cmd->num_inbufs != 0 || user_cmd->num_bufs != 0) {
		dev_err(session->vha->dev, ">0 buffers in cmd is wrong\n");
		ret = -EINVAL;
	}
	/* remember the pdump message may not be null terminated */
	img_pdump_printf("%.*s\n", (int)cmd->size, (char *)user_cmd->data);
	return ret;
}

/*
 * Simple procedure that generates watchdog interrupt
 */
void vha_cnn_start_calib(struct vha_dev *vha)
{
	uint64_t clk;
	uint32_t start;

	/* Setup hw watchdog before kicking the hw */
	vha_dev_hwwdt_setup(vha, vha->calibration_cycles, 0);

	/* Disabling command decoder, so we can generate wdt interrupt,
	 * without providing any buffer address */
	clk = IOREAD64(vha->reg_base, VHA_CR_CLK_CTRL0);
	VHA_CR_CLEARBITS(clk, CLK_CTRL0, CNN_CMD);
	IOWRITE64(vha->reg_base, VHA_CR_CLK_CTRL0, clk);

	/* To be sure the cmd clock has switched off*/
	udelay(100);

	/* Enable MMU bypass */
	IOWRITE64_PDUMP(VHA_CR_OS(MMU_CTRL_BYPASS_EN),
		VHA_CR_OS(MMU_CTRL));

	/* Set minimal command stream size */
	start = (2048/32-1) << VHA_CR_OS(CNN_CONTROL_CMD_SIZE_MIN1_SHIFT);
	start |= VHA_CR_OS(CNN_CONTROL_START_EN);
	/* write the START bit */
	IOWRITE64(vha->reg_base, VHA_CR_OS(CNN_CONTROL), start);
	/* Remember the time cnn is kicked */
	GETNSTIMEOFDAY(&vha->stats.hw_proc_start);
}

void vha_cnn_update_stats(struct vha_dev *vha)
{
	vha->stats.cnn_last_proc_us =
		vha->stats.last_proc_us;
	vha->stats.cnn_total_proc_us +=
		vha->stats.last_proc_us;

	if (vha->stats.cnn_kicks) {
		uint64_t avg = vha->stats.cnn_total_proc_us;
		do_div(avg, vha->stats.cnn_kicks);
		vha->stats.cnn_avg_proc_us = avg;
	}
#if defined(HW_AX2)
	vha->stats.cnn_last_cycles =
			IOREAD64(vha->reg_base, VHA_CR_CNN_WDT_TIMER);
#elif defined(HW_AX3)
	vha->stats.cnn_last_cycles =
			IOREAD64(vha->reg_base, VHA_CR_OS(CNN_PERFORMANCE));
#endif
	if (vha->stats.cnn_last_cycles && vha->freq_khz) {
		uint64_t est_proc_us = 1000UL * vha->stats.cnn_last_cycles;
		do_div(est_proc_us, vha->freq_khz);
		vha->stats.cnn_last_est_proc_us = est_proc_us;
	}
	vha->stats.cnn_total_cycles += vha->stats.cnn_last_cycles;
	if (vha->stats.cnn_kicks &&
			vha->stats.cnn_total_cycles && vha->freq_khz) {
		uint64_t avg = 1000UL * vha->stats.cnn_total_cycles;
		do_div(avg, vha->stats.cnn_kicks);
		do_div(avg, vha->freq_khz);
		vha->stats.cnn_avg_est_proc_us = avg;
	}
}

/*
 * a command has completed. sent notification to user
 */
void vha_cnn_cmd_completed(struct vha_cmd *cmd, int status)
{
	struct vha_session *session = cmd->session;
	struct vha_dev* vha = session->vha;
	struct vha_rsp *rsp = NULL;
	int i;
	struct vha_user_cnn_submit_rsp * cnn_submit_rsp = NULL;

	const struct vha_user_cmd *user_cmd = &cmd->user_cmd;

	switch (user_cmd->cmd_type) {
	case VHA_CMD_CNN_SUBMIT:
	{
		size_t mem_usage;
		/* allocate sufficient space for the response */
		size_t sz = sizeof(*rsp)
			+ sizeof(struct vha_user_cnn_submit_rsp)
			- sizeof(struct vha_user_rsp);
		uint32_t status_mask;
		uint32_t ready_mask;
		uint32_t cmpl_val = VHA_CR_OS(VHA_EVENT_STATUS_VHA_CNN0_COMPLETE_EN);
#if defined(HW_AX2)
		/* status change: wait for any status change:
		 * WDT, MMU_PF, ERROR, COMPLETE
		 */
		status_mask = 0xffffffff;
		ready_mask = 0xffffffff;
#elif defined(HW_AX3)
		/* status mask: wait for a status change: either ERROR, COMPLETE:
		 * note that, unlike the live driver, pdump will ignore the MMU_PF,
		 * which will have to be detected by the WDT
		 */
		status_mask = VHA_CR_OS(VHA_EVENT_STATUS_VHA_ERROR_CLRMSK)
				| VHA_CR_OS(VHA_EVENT_STATUS_VHA_CNN0_COMPLETE_CLRMSK);
		ready_mask = VHA_CR_OS(VHA_EVENT_STATUS_VHA_READY_CLRMSK);

		/* Ignore PARITY when waiting for status change */
		status_mask &= VHA_CR_OS(VHA_EVENT_STATUS_PARITY_CLRMSK);
#ifdef VHA_SCF
		if (session->vha->hw_props.supported.parity &&
				!session->vha->parity_disable) {
			/* If complete bit is set then parity bit must be set as well ! */
			cmpl_val |= VHA_CR_OS(VHA_EVENT_STATUS_PARITY_EN);
		}
#else
		/* Ignore PARITY, so that non-SCF pdump may work with SC CSIM */
		ready_mask &= VHA_CR_OS(VHA_EVENT_STATUS_PARITY_CLRMSK);
#endif
#endif
		rsp = kzalloc(sz, GFP_KERNEL);
		if (rsp == NULL) {
			session->oom = true;
			return;
		}

		cnn_submit_rsp = (struct vha_user_cnn_submit_rsp*)&rsp->user_rsp;
		rsp->size = sizeof(struct vha_user_cnn_submit_rsp);

		if (session->vha->hw_bypass) {
			session->vha->hw_bypass--;
			break;
		}

		img_pdump_printf("-- CNN_WAIT_BEGIN\n");
		/* pdump POL for status change
		 * count=cnn_pdump_poll_count, delay=1000cycles
		 */
		img_pdump_printf("-- Wait for any CNN status\n"
				"POL :REG:%#x 0 %#x 3 %u 1000\n",
				VHA_CR_OS(VHA_EVENT_STATUS),
				status_mask,
				cnn_pdump_poll_count);

		/* quick pdump POL for the status complete flag only:
		 * count=1, delay=10cycles
		 */
		img_pdump_printf("-- Check for CNN_COMPLETE flag only\n"
				"POL :REG:%#x %#x 0x%x 0 1 10\n",
				VHA_CR_OS(VHA_EVENT_STATUS),
				cmpl_val,
				ready_mask);
#ifdef VHA_SCF
		if (session->vha->hw_props.supported.parity &&
				!session->vha->parity_disable) {
			/* Check CNN_STATUS parity */
			uint32_t cnn_status = VHA_CR_SETBITS_OS(CNN_STATUS,
					STREAM_COUNT, 1);
			cnn_status |= VHA_CR_SETBITS_OS(CNN_STATUS,
					PARITY, 1);
			img_pdump_printf("-- Check for CNN_STATUS parity\n"
					"POL :REG:%#x %#x 0xffffffff 0 1 10\n",
					VHA_CR_OS(CNN_STATUS), cnn_status);
		}
#endif
		/* quick pdump POL for AXI errors:
		 * count=1, delay=10cycles
		 */
		img_pdump_printf("-- Post check of AXI status\n"
				"POL :REG:%#x 0 0xffffffff 0 1 10\n",
				VHA_CR_ACE_STATUS);

		/* We do clear interrupts in the irq handler,
		 * but this is not recorded into pdump because
		 * of the irq context, so do it here */
		img_pdump_printf("-- Clear CNN events\n"
				"WRW64 :REG:%#x %#x\n",
				VHA_CR_OS(VHA_EVENT_CLEAR),
				VHA_CR_OS(VHA_EVENT_CLEAR_VHA_CNN0_COMPLETE_EN) |
				VHA_CNN_ERR_EVNTS);

		/* Try to flush hw debug buffers first
		 * - this does pdump SAB when proper checkpoint is set */
		vha_dbg_flush_hwbufs(session, 1, 0);

		/* pdump SAB for each of the output buffers */
		img_pdump_printf("-- Save outputs\n");
		for (i = user_cmd->num_inbufs; i < user_cmd->num_bufs; i++) {
			struct vha_buffer *buf;
			struct vha_user_cnn_submit_cmd *msg =
				container_of(user_cmd,
						struct vha_user_cnn_submit_cmd,
						msg);
			uint32_t offset;
			uint32_t size;

			buf = vha_find_bufid(session, user_cmd->data[i]);
			if (buf == NULL) {
				dev_err(session->vha->dev,
						"%s: invalid buffer id:%d\n",
						__func__, user_cmd->data[i]);
				continue;
			}
			if (buf->id == msg->cmdbuf) {
				offset = 0;
				size = buf->size;
			} else {
				offset = msg->bufoffsets[i-1];
				size = msg->bufsizes[i-1];
			}

			vha_pdump_sab_buf(session, PDUMP_RES,
					buf, offset, size);

			/* Update status, do not signal fence yet,
			 * it's is done explicitly below, after cache invalidation */
			vha_set_buf_status(session, buf->id, VHA_BUF_FILLED_BY_HW,
					VHA_SYNC_NONE, false);

			if (vha_buf_needs_inval(session, buf->id) && !status)
				img_mem_sync_device_to_cpu(session->mem_ctx, buf->id);

#ifdef KERNEL_DMA_FENCE_SUPPORT
			img_mem_signal_fence(session->mem_ctx, buf->id);
#endif
			vha_dump_digest(session, buf, cmd);
		}

		if (session->vha->low_latency == VHA_LL_SW_KICK) {
			struct vha_cmd *cmd =
				session->vha->queuedcmd[VHA_CNN_CMD].cmd;

			if (cmd && cmd->queued) {
				/* Setup kick info */
				uint64_t val = vha_dev_kick_prepare(session->vha, cmd,
						session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id);
				img_pdump_printf("-- CNN kick (queued)!\n");
				img_pdump_printf("WRW64 :REG:%#x %#llx\n",
					VHA_CR_OS(CNN_CONTROL), val);
			}
		}
		img_pdump_printf("-- CNN_WAIT_END\n");

		img_mem_get_usage(session->mem_ctx, NULL, &mem_usage);
		/* send out an event when submit is complete */
		if (vha_observers.completed)
			vha_observers.completed(
				session->vha->id,
				session->id,
				user_cmd->cmd_id,
				status,
				session->vha->stats.cnn_last_cycles,
				mem_usage,
				user_cmd->priority);

		/* post some metrics about the hw to user space */
#ifdef MEM_USAGE_LAST_METRICS_ARE_AVAILABLE
		cnn_submit_rsp->mem_usage = mem_usage;
#else
		cnn_submit_rsp->mem_usage = ~0;
#endif
		cnn_submit_rsp->last_proc_us = cmd->proc_us;
		cnn_submit_rsp->hw_cycles = cmd->hw_cycles;
		dev_dbg(session->vha->dev, "%s: %p, hw_cycles %llx\n", __func__,
				cmd, session->vha->stats.cnn_last_cycles);

		session->last_proc_us = cmd->proc_us;
		session->total_proc_us += cmd->proc_us;
		if (session->kicks) {
			uint64_t avg = session->total_proc_us;
			do_div(avg, session->kicks);
			session->avg_proc_us = avg;
		}

		if (session->vha->stats.cnn_last_cycles > (uint32_t)~0)
			dev_warn(session->vha->dev,
				"%s: hw_cycles %llx exceeds 32bit limit\n",
				__func__,
				session->vha->stats.cnn_last_cycles);
		break;
	}
	case VHA_CMD_CNN_PDUMP_MSG:
	default:
		/* allocate space for standard response */
		rsp = kzalloc(sizeof(*rsp), GFP_KERNEL);
		if (rsp == NULL) {
			session->oom = true;
			return;
		}
		rsp->size = sizeof(rsp->user_rsp);
		break;
	}

	if (user_cmd->flags & VHA_CMDFLAG_NOTIFY) {
		rsp->user_rsp.cmd_id = cmd->user_cmd.cmd_id;
		rsp->user_rsp.err_no = session->vha->hw_bypass ? 0 : status;

		cmd->rsp = rsp;
	} else
		kfree(rsp);
}

/*
 * Perform a command, as requested by user.
 * note: this function is called with vha_dev.lock == locked
 */
int vha_do_cnn_cmd(struct vha_cmd *cmd)
{
	struct vha_session *session = cmd->session;
	const struct vha_user_cmd *user_cmd = &cmd->user_cmd;
	int status = -EINVAL;

	dev_dbg(session->vha->dev,
		"CNN command: id:%x type:%x nin:%x nbufs:%x\n",
		user_cmd->cmd_id, user_cmd->cmd_type,
		user_cmd->num_inbufs, user_cmd->num_bufs);
#if 0
	print_hex_dump_debug("VHA CMD: ", DUMP_PREFIX_NONE, 4, 4,
				user_cmd, ALIGN(cmd->size, 4), false);
#endif

	switch (user_cmd->cmd_type) {
	case VHA_CMD_CNN_SUBMIT:
		status = do_cmd_cnn_submit(cmd);
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
		if (cmd->dummy_kicked) {
			struct vha_dev *vha = cmd->session->vha;
			const struct vha_user_cnn_submit_cmd *cnn_user_cmd =
				(struct vha_user_cnn_submit_cmd *)&cmd->user_cmd;
			uint32_t estimated_cycles = cnn_user_cmd->estimated_cycles;
			if (estimated_cycles == 0)
				estimated_cycles = VHA_DUMMY_HW_PROCESSING_TIME_CYCLES;
			cmd->dummy_exec_time = (estimated_cycles / (vha->freq_khz / 1000));
			schedule_delayed_work(&vha->dummy_dwork,
														usecs_to_jiffies(cmd->dummy_exec_time));
			cmd->dummy_kicked = false;
		}
#endif
		break;
	case VHA_CMD_CNN_PDUMP_MSG:
		status = do_cmd_cnn_pdump_msg(cmd);
	default:
		break;
	}

	/*
	 * Immediately send notification to user if not using hw at all
	 * or submitting failed.
	 */
	if (!CMD_EXEC_ON_HW(cmd) || status) {
		vha_cnn_cmd_completed(cmd, status);
		vha_cmd_notify(cmd);
		return 1;
	}

	return 0;
}

void vha_cnn_dump_status(struct vha_dev *vha)
{
	struct device *dev = vha->dev;

	dev_err(dev, " CNN_STATUS:%llx ",
		IOREAD64(vha->reg_base,
			VHA_CR_OS(CNN_STATUS)));
#ifdef HW_AX2
	dev_err(dev, " CNN_WDT_COMPAREMATCH:%llx ",
		IOREAD64(vha->reg_base,
			VHA_CR_CNN_WDT_COMPAREMATCH));
	dev_err(dev, " CNN_WDT_TIMER:%llx ",
		IOREAD64(vha->reg_base,
			VHA_CR_CNN_WDT_TIMER));
#endif
	dev_err(dev, " CNN_MEM_WDT_COMPAREMATCH:%llx ",
		IOREAD64(vha->reg_base,
			VHA_CR_CNN_MEM_WDT_COMPAREMATCH));
	dev_err(dev, " CNN_MEM_WDT_TIMER:%llx ",
		IOREAD64(vha->reg_base,
			VHA_CR_CNN_MEM_WDT_TIMER));
	dev_err(dev, " BIF_OUTSTANDING_READ:%llx\n",
		IOREAD64(vha->reg_base,
			VHA_CR_BIF_OUTSTANDING_READ));
}

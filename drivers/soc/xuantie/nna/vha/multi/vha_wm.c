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
#include <uapi/vha_errors.h>
#include "vha_common.h"
#include "vha_plat.h"
#include "vha_regs.h"

static uint32_t cnn_pdump_poll_count = 10000000;
module_param(cnn_pdump_poll_count, uint, 0444);
MODULE_PARM_DESC(cnn_pdump_poll_count,
		"PDUMP: Number of times to poll for CNN status");

static uint32_t wm_pdump_poll_count = 100;
module_param(wm_pdump_poll_count, uint, 0444);
MODULE_PARM_DESC(wm_pdump_poll_count,
		"PDUMP: Number of times to poll for WM status");

static bool cnn_preloads_disable;
module_param(cnn_preloads_disable, bool, 0444);
MODULE_PARM_DESC(cnn_preloads_disable,
		"Disables CNN preloads");

static uint32_t cnn_hl_wdt_cycles = VHA_CORE_WDT_CYCLES;
module_param(cnn_hl_wdt_cycles, uint, 0444);
MODULE_PARM_DESC(cnn_hl_wdt_cycles,
		"High level core watchdog cycles");

static uint32_t cnn_hl_wdt_mode = VHA_CR_CNN_WDT_CTRL_CNN_WDT_CTRL_KICK_PASS;
module_param(cnn_hl_wdt_mode, uint, 0444);
MODULE_PARM_DESC(cnn_hl_wdt_mode,
		"High level core watchdog mode: 1-pass; 2-layer group. See TRM");

static uint32_t cnn_mem_wdt_cycles = VHA_CORE_MEM_WDT_CYCLES;
module_param(cnn_mem_wdt_cycles, uint, 0444);
MODULE_PARM_DESC(cnn_mem_wdt_cycles,
		"Core memory watchdog cycles");

static uint32_t cnn_mem_wdt_mode = VHA_CR_CNN_MEM_WDT_CTRL_CNN_MEM_WDT_CTRL_KICK_PASS;
module_param(cnn_mem_wdt_mode, uint, 0444);
MODULE_PARM_DESC(cnn_mem_wdt_mode,
		"Core memory watchdog mode: 0-disabled; "
		"1-CMD Parser starts a pass or CMD parser is kicked; "
		"2-CMD parser is kicked. See TRM");

static bool use_estimated_cycles_for_wm_wdt = false;
module_param(use_estimated_cycles_for_wm_wdt, bool, 0444);
MODULE_PARM_DESC(use_estimated_cycles_for_wm_wdt,
		"WM workload watchdog cycles source: "
		"false-the value from the wm_wl_wdt_cycles parameter will be used; "
		"true-the value from the MBS SEGMENT_ESTIMATED_CYCLES filed will be used");

static uint32_t wm_wl_wdt_estimated_cycles_margin = 0;
module_param(wm_wl_wdt_estimated_cycles_margin, uint, 0444);
MODULE_PARM_DESC(wm_wl_wdt_estimated_cycles_margin,
		"WM workload watchdog cycles margin added to the SEGMENT_ESTIMATED_CYCLES"
		" value, used only if use_estimated_cycles_for_wm_wdt==true");

static uint32_t wm_wl_wdt_cycles = VHA_WM_WDT_CYCLES;
module_param(wm_wl_wdt_cycles, uint, 0444);
MODULE_PARM_DESC(wm_wl_wdt_cycles,
		"WM workload watchdog cycles");

static uint32_t wm_wl_wdt_mode = VHA_CR_WM_WL_WDT_CTRL_WL_WDT_CTRL_KICK_WL;
module_param(wm_wl_wdt_mode, uint, 0444);
MODULE_PARM_DESC(wm_wl_wdt_mode,
		"WM workload watchdog mode: 0-disabled; 1-enabled. See TRM");

static uint32_t socm_xor_bits[2] = { 0, 0 };
module_param_array(socm_xor_bits, uint, NULL, 0444);
MODULE_PARM_DESC(socm_xor_bits,
	"SOCM Hashing: This parameter reflects SOCM_B7_XOR_BITS & SOCM_B8_XOR_BITS"
	"hw registers. If not set the default values are used. See TRM.");

/*
 * Internal memory layout:
 * .onchipmem_phys_start
 * LOCM - <onchipmem_size>
 * 4k GUARD PAGE
 * WM0 SOCM - <shared_onchipmem_size>
 * 4k GUARD PAGE
 * WM1 SOCM
 * 4k GUARD PAGE
 * ...
 * WMn SOCM
 * 4k GUARD PAGE
 * WM0 LL SYNC buffer- 4k PAGE
 * 4k GUARD PAGE
 * WM1 LL SYNC buffer- 4k PAGE
 * 4k GUARD PAGE
 * ...
 * WMn LL SYNC buffer- 4k PAGE
 * 4k GUARD PAGE
 */
#define LLSYNC_SIZE 0x1000

struct vha_config_regs {
	uint64_t core_assignment;
	uint64_t cnn_control[VHA_MAX_CORES];
	uint64_t cmd_base_addr[VHA_MAX_CORES];
	uint64_t cnn_alt_addr[VHA_CORE_MAX_ALT_ADDRS];
	uint64_t locm_base_addr;
	uint64_t socm_circ_buff_size;
	uint64_t socm_base_addr;
	uint64_t socm_buf_assignment;
	uint64_t socm_b7_xor_bits;
	uint64_t socm_b8_xor_bits;
	uint64_t low_level_sync_base_addr;
	uint64_t cnn_alt_addr_used;
	uint64_t cnn_vcore_mapping;
};

/* Note:
 * The SOCM_BUF_<X>_WM_MAPPING and the CORE_<X>_WM_MAPPING registers muse be configured to be the same
 * thus we use the core_mask for a given WM. */
static uint64_t wm_assign_socm(struct vha_dev *vha, uint64_t socm_buf_addr,
		uint8_t wm_id, uint8_t core_mask, uint32_t circ_buf_offs, struct vha_config_regs* regs)
{
	uint64_t socm_buf_assignment = IOREAD64_CR_REGIO(SOCM_BUF_ASSIGNMENT);
	uint32_t assignment_field_shift =
				VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_1_WM_MAPPING_SHIFT -
					VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_0_WM_MAPPING_SHIFT;
	uint64_t assignment_field_mask =
					~VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_0_WM_MAPPING_CLRMSK;
	uint64_t base_addr = socm_buf_addr;
	uint32_t socm_chunk_size = vha->hw_props.socm_core_size_bytes *
				VHA_CORE_MASK_TO_NUM(vha_wm_get_cores(vha, wm_id));

	/* Use different address for each WM to make debugging easier */
	base_addr += wm_id * (vha->hw_props.socm_size_bytes + IMG_MEM_VA_GUARD_GAP);
	/* Virtual base address must be 256 byte aligned */
	base_addr = ALIGN(base_addr, 256);
	/* Chunk size used to calculate the offset must be 128 byte aligned */
	socm_chunk_size = ALIGN(socm_chunk_size, 128);

	/* circ_buf_offs = 0 means that the circular buffer is disabled */
	if (circ_buf_offs && socm_chunk_size && circ_buf_offs <= socm_chunk_size) {
		regs->socm_circ_buff_size = socm_chunk_size - circ_buf_offs;
	} else {
		regs->socm_circ_buff_size = 0;
	}

	regs->socm_base_addr = base_addr;
	dev_dbg(vha->dev, "%s: set SOCM WM%u address -> %#llx\n",
			__func__,  wm_id, base_addr);

	while (core_mask != 0) {
		uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(core_mask);

		core_mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id));

		socm_buf_assignment &=
			~(assignment_field_mask << (curr_core_id * assignment_field_shift));
		socm_buf_assignment |= wm_id << (curr_core_id * assignment_field_shift);
	}

	regs->socm_buf_assignment = socm_buf_assignment;
	dev_dbg(vha->dev, "%s: assigned SOCM bufs for WM%u: 0x%llx\n",
					__func__, wm_id, socm_buf_assignment);

	if (socm_xor_bits[0]) {
		regs->socm_b7_xor_bits = socm_xor_bits[0];
	}

	if (socm_xor_bits[1]) {
		regs->socm_b8_xor_bits = socm_xor_bits[1];
	}

	return base_addr - socm_buf_addr;
}

static bool vha_wm_setup_config_regs_multi(struct vha_cmd *cmd, struct vha_config_regs* regs)
{
	int i;
	bool ret = false;
	const struct vha_user_cnn_submit_multi_cmd *user_submit_cmd =
		(struct vha_user_cnn_submit_multi_cmd *)&cmd->user_cmd;
	struct vha_hw_sched_info *sched_info = &cmd->hw_sched_info;
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	uint32_t val32 = 0;
	struct vha_buffer *buf = NULL;
	uint64_t *reg = NULL;

	uint32_t core_mask;
	uint64_t vcore_map = 0;
	uint32_t vcore_field_shift =
		VHA_CR_OS0_CNN_VCORE_MAPPING_VCORE1_SHIFT -
							VHA_CR_OS0_CNN_VCORE_MAPPING_VCORE0_SHIFT;

	if (cmd->size != sizeof(*user_submit_cmd)) {
		dev_err(vha->dev, "%s: command buffer wrong size: %zu/%zu",
			__func__, cmd->size, sizeof(*user_submit_cmd));
		goto out_error;
	}

	if (!vha_dev_check_hw_capab(vha, user_submit_cmd->expected_ip_capab))
		goto out_error;

	/* At least num cores CMDs and IN */
	if (user_submit_cmd->msg.num_inbufs < (user_submit_cmd->num_cores + 1) ||
		/* At least OUT */
		(user_submit_cmd->msg.num_inbufs - user_submit_cmd->num_cores
				>= user_submit_cmd->msg.num_bufs) ||
		/* And maybe TMP and others */
		user_submit_cmd->msg.num_bufs > VHA_CORE_MAX_ALT_ADDRS) {
		dev_err(vha->dev, "%s: wrong number of bufs: %u,%u\n",
				__func__,
				user_submit_cmd->msg.num_inbufs,
				user_submit_cmd->msg.num_bufs);
		goto out_error;
	}

	/* Number of cores. */
	if ((user_submit_cmd->num_cores < 1) ||
		(user_submit_cmd->num_cores > vha->hw_props.num_cnn_core_devs)) {
		dev_err(vha->dev, "%s: wrong number of cores: %u\n",
			__func__,
			user_submit_cmd->num_cores);
		goto out_error;
	}

	/* Number of cmd streams must match number of cores. */
	for (i = 0; i < user_submit_cmd->num_cores; i++)
		if (user_submit_cmd->cmdbuf[i] == 0)
			break;

	if ((i < user_submit_cmd->num_cores) ||
		((user_submit_cmd->num_cores < VHA_MAX_CORES) &&
		(user_submit_cmd->cmdbuf[i] != 0))) {
		for (; i < VHA_MAX_CORES; i++)
			if (user_submit_cmd->cmdbuf[i] == 0)
				break;

		dev_err(vha->dev, "%s: wrong number of cmd streams: %u,%u\n",
			__func__,
			i, user_submit_cmd->num_cores);
		goto out_error;
	}

	/* Make WM<->cores binding. */
	vha_wm_assign_cores(vha, sched_info->wm_id, sched_info->core_mask, &regs->core_assignment);
	dev_dbg(vha->dev, "%s: assigned cores for WM%u: 0x%02x\n",
		__func__, sched_info->wm_id, vha_wm_get_cores(vha, sched_info->wm_id));

	/* write buffer address to each register,
	 * and pdump LDB each of the the input buffers */
	img_pdump_printf("-- Load inputs\n");

	/* First program cmd stream addrs. */
	core_mask = sched_info->core_mask;
	if (VHA_CORE_MASK_TO_NUM(core_mask) != user_submit_cmd->num_cores) {
		dev_err(vha->dev, "%s: invalid core_mask!\n", __func__);
		goto out_error;
	}

	for (i = 0; i < user_submit_cmd->num_cores; i++) {
		uint64_t curr_core;
		uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(core_mask);

		buf = vha_find_bufid(session, user_submit_cmd->cmdbuf[i]);
		if (buf == NULL) {
			dev_err(vha->dev, "%s: invalid buffer id:%d\n",
				__func__, user_submit_cmd->cmdbuf[i]);
			goto out_error;
		}
		if (buf->size == 0) {
			dev_err(vha->dev, "%s: invalid cmdstream size\n", __func__);
			goto out_error;
		}

		/* Choose next core from the WM set. */
		curr_core = VHA_CORE_ID_TO_MASK(curr_core_id);
		core_mask &= ~((uint32_t)curr_core);

		val32 = min(2048U, (uint32_t)buf->size)/32 - 1;
		val32 = VHA_CR_SETBITS(OS0_CNN_CONTROL, CMD_SIZE_MIN1, val32) |
					VHA_CR_SETBITS(OS0_CNN_CONTROL, CTXT_PASID,
						session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id) |
					VHA_CR_SETBITS(OS0_CNN_CONTROL, CTXT_PASID_IO,
						session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].hw_id);
		regs->cnn_control[curr_core_id] = val32;

		/* Pdump the cmd stream buffers. */
		vha_pdump_ldb_buf(session, PDUMP_PRM,
				buf, 0, buf->size,
				buf->status == VHA_BUF_FILLED_BY_SW);

		/* Write to core's cmd register.
		 * In no-MMU mode, write phys address of a contig buffer.
		 * In MMU mode, write virt address of buffer. */
		SET_BUFADDR(session, buf, 0, &regs->cmd_base_addr[curr_core_id]);

		/* Map this core. */
		vcore_map |= curr_core_id << (i * vcore_field_shift);

		if (vha_buf_needs_flush(session, buf->id))
			img_mem_sync_cpu_to_device(session->mem_ctx, buf->id);
	}

	/* Command stream buffers are already handled */
	for (i = 0; i < (user_submit_cmd->msg.num_bufs - 1); i++) {
		uint32_t offset;
		uint32_t size;

		buf = vha_find_bufid(session, user_submit_cmd->bufs[i]);
		if (buf == NULL) {
			dev_err(vha->dev, "%s: invalid buffer id:%d\n",
					__func__, user_submit_cmd->bufs[i]);
			goto out_error;
		}

		/* offset can be specified for all
		 * buffers except cmdstream buf */
		offset = user_submit_cmd->bufoffsets[i];
		size = user_submit_cmd->bufsizes[i];

		if (size + offset > buf->size) {
			dev_err(vha->dev, "%s: invalid size+offset: %x+%x > %zx\n",
					__func__, size, offset, buf->size);
			goto out_error;
		}

		/* Calculate reg address */
		reg = &regs->cnn_alt_addr[user_submit_cmd->regidx[i]];
		/* Record what alt address is in use */
		regs->cnn_alt_addr_used |= 1 << user_submit_cmd->regidx[i];
		regs->cnn_alt_addr_used |= buf->req_type <<
			(VHA_CR_OS0_CNN_ALT_ADDRESS_USED_ALT_ADDR0_BUF_TYPE_SHIFT +
			user_submit_cmd->regidx[i]);

		if (user_submit_cmd->onchipram_bufs[VHA_LOCAL_OCM] == buf->id) {
			/* Check against overflow */
			if (buf->devvirt + vha->hw_props.locm_size_bytes +
					IMG_MEM_VA_GUARD_GAP > IMG_MEM_VA_HEAP1_BASE) {
				dev_err(vha->dev, "%s: LOCM overflow!\n", __func__);
				goto out_error;
			}

			/* Setup Local OCM */
			regs->locm_base_addr = buf->devvirt;
			dev_dbg(vha->dev, "%s: set LOCM address -> %#llx\n",
					__func__, buf->devvirt);
		}

		if (user_submit_cmd->onchipram_bufs[VHA_SHARED_OCM] == buf->id) {
			/* Check against overflow */
			if (buf->devvirt + vha->hw_props.socm_size_bytes +
					IMG_MEM_VA_GUARD_GAP > IMG_MEM_VA_HEAP1_BASE) {
				dev_err(vha->dev, "%s: SOCM overflow!\n", __func__);
				goto out_error;
			}
			/* Setup Shared OCM */
			offset = wm_assign_socm(vha, buf->devvirt,
					sched_info->wm_id, sched_info->core_mask,
					user_submit_cmd->shared_circ_buf_offs, regs);
			/* Check against overflow */
			if (regs->socm_base_addr + vha->hw_props.socm_size_bytes +
					IMG_MEM_VA_GUARD_GAP > IMG_MEM_VA_HEAP1_BASE) {
				dev_err(vha->dev, "%s: SOCM overflow!\n", __func__);
				goto out_error;
			}
		}

		/* pdump the input buffers (not filled by the hw),
		 * try to cache buffers filled by SW,
		 * to avoid unnecessary LDBs */
		if (i < user_submit_cmd->msg.num_inbufs - user_submit_cmd->num_cores &&
				!(buf->status == VHA_BUF_FILLED_BY_HW))
			vha_pdump_ldb_buf(session, PDUMP_PRM,
					buf, offset, size,
					buf->status == VHA_BUF_FILLED_BY_SW);

		/* Write to the index register.
		 * In no-MMU mode, write phys address of a contig buffer.
		 * In MMU mode, write virt address of buffer. */
		SET_BUFADDR(session, buf, offset, reg);

		if (vha_buf_needs_flush(session, buf->id))
			img_mem_sync_cpu_to_device(session->mem_ctx, buf->id);
	}

	if (vha->ocm_paddr != ~0) {
		/* Low level sync buffer address
		 * It has fixed size = 512 bytes but we operate on 4k pages
		 * It is placed after SOCM
		 * including gap page between LOCM&SOCM and after SOCM.
		 */
		uint64_t ll_sync_addr = vha->ocm_paddr +
				vha->hw_props.locm_size_bytes + IMG_MEM_VA_GUARD_GAP +
				vha->hw_props.num_cnn_core_devs * (vha->hw_props.socm_size_bytes + IMG_MEM_VA_GUARD_GAP);
		/* Add offset based on WM id */
		ll_sync_addr += sched_info->wm_id * (LLSYNC_SIZE +
				IMG_MEM_VA_GUARD_GAP);

		/* Check against overflow */
		if (ll_sync_addr + LLSYNC_SIZE +
				IMG_MEM_VA_GUARD_GAP > IMG_MEM_VA_HEAP1_BASE) {
			dev_err(vha->dev, "%s: LLSYNC overflow!\n", __func__);
			goto out_error;
		}

		/* Setup low level sync buffer address */
		regs->low_level_sync_base_addr = ll_sync_addr;
		dev_dbg(vha->dev, "%s: set LLSYNC address -> %#llx\n",
				__func__, ll_sync_addr);
	}

	ret = true;
	/* Program core mappings. */
	regs->cnn_vcore_mapping = vcore_map;

out_error:
	return ret;
}

static bool vha_wm_write_config_regs(struct vha_cmd *cmd, struct vha_config_regs* regs)
{
	struct vha_hw_sched_info *sched_info = &cmd->hw_sched_info;
	uint8_t wm_id = sched_info->wm_id;
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	uint32_t reg_size = VHA_CR_OS0_CNN_ALT_ADDRESS1 - VHA_CR_OS0_CNN_ALT_ADDRESS0;
	uint32_t reg_base = VHA_CR_OS0_CNN_ALT_ADDRESS0;
	uint32_t reg_idx_offset = 0;
	uint32_t core_id = 0;
	int i;

	img_pdump_printf("-- Assign cores 0x%02x to WM%u\n", sched_info->core_mask, wm_id);
	IOWRITE64_CR_PDUMP(regs->core_assignment, CORE_ASSIGNMENT);

	for (core_id = 0; core_id < VHA_MAX_CORES; core_id++) {
		if (sched_info->core_mask & (1 << core_id)) {
			uint64_t curr_core = VHA_CORE_ID_TO_MASK(core_id);

			img_pdump_printf("-- Select core: %llu\n", curr_core);
			IOWRITE64_CR_PDUMP(curr_core, CORE_CTRL_INDIRECT);

			img_pdump_printf("-- Setup command stream for core %u\n", core_id);
			IOWRITE64_CR_PDUMP(regs->cnn_control[core_id], OS0_CNN_CONTROL);
			IOWRITE64_CR_PDUMP(regs->cmd_base_addr[core_id], OS0_CNN_CMD_BASE_ADDRESS);
		}
	}

	/* Operate only on a core assigned to this WM. */
	img_pdump_printf("-- Select only cores assigned to WM: %u\n",
							sched_info->core_mask);
	IOWRITE64_CR_PDUMP(sched_info->core_mask, CORE_CTRL_INDIRECT);
	/* Make WM<->core binding. */

	if (regs->socm_base_addr != ~0) {
		img_pdump_printf("-- Set SOCM circular buffer size for WM%d\n", wm_id);
		IOWRITE64_CR_PDUMP(regs->socm_circ_buff_size, SOCM_CIRCULAR_BUFFER_SIZE);

		img_pdump_printf("-- Set SOCM WM%u address\n", wm_id);
		IOWRITE64_CR_PDUMP(regs->socm_base_addr, SOCM_BASE_ADDR);

		img_pdump_printf("-- Assign SOCM bufs 0x%02x to WM%u\n", sched_info->core_mask, wm_id);
		IOWRITE64_CR_PDUMP(regs->socm_buf_assignment, SOCM_BUF_ASSIGNMENT);

		if (regs->socm_b7_xor_bits)
			IOWRITE64_CR_PDUMP(regs->socm_b7_xor_bits, SOCM_B7_XOR_BITS);
		if (regs->socm_b8_xor_bits)
			IOWRITE64_CR_PDUMP(regs->socm_b8_xor_bits, SOCM_B8_XOR_BITS);
	}

	if (regs->locm_base_addr != ~0) {
		img_pdump_printf("-- Set LOCM address\n");
		IOWRITE64_CR_PDUMP(regs->locm_base_addr, OS0_LOCM_BASE_ADDR);
	}

	for (i = 0; i < VHA_CORE_MAX_ALT_ADDRS; i++) {
		if (i >= 8) {
			reg_base = VHA_CR_OS0_CNN_ALT_ADDRESS8;
			reg_idx_offset = 8;
		}

		if (regs->cnn_alt_addr_used & (1 << i)) {
			img_pdump_printf("-- Set ALT_%d address\n", i);
			IOWRITE64_PDUMP(regs->cnn_alt_addr[i], reg_base + (i - reg_idx_offset) * reg_size);
		}
	}

	if (regs->low_level_sync_base_addr != ~0) {
		/* Setup low level sync buffer address */
		img_pdump_printf("-- Set LLSYNC address\n");
		IOWRITE64_CR_PDUMP(regs->low_level_sync_base_addr, LOW_LEVEL_SYNC_BASE_ADDR);
	}

	if (!cnn_preloads_disable) {
		/* Inform the hw what alt addresses are in use,
		 * so the command decoder can prefetch */
		img_pdump_printf("-- Setup CNN prefetch register\n");
		IOWRITE64_CR_PDUMP(regs->cnn_alt_addr_used, OS0_CNN_ALT_ADDRESS_USED);
	}

	/* Program core mapping. */
	img_pdump_printf("-- Program virtual core mappings\n");
	IOWRITE64_CR_PDUMP(regs->cnn_vcore_mapping,	OS0_CNN_VCORE_MAPPING);

	return true;
}

#ifdef VHA_SCF
#ifdef VHA_EVENT_INJECT
#define CHECK_TOP_REG(_val_, _reg_)	do {		\
	uint64_t val64 = IOREAD64_CR_REGIO(_reg_);	\
	if((vha->injection.conf_err & CONF_ERR_TOP) && __EVENT_INJECT())	\
		val64 = ~val64;							\
	if (val64 != _val_) {						\
		cmd->conf_top_error = true;				\
		dev_err(vha->dev, "Confirmation writes mismatch, top register: 0x%x \n 	\
		 expected: 0x%016llx actual: 0x%016llx\n", VHA_CR_##_reg_, (uint64_t)_val_, val64);		\
		goto out_error;							\
	}} while(0)

#define CHECK_CR_CORE_REG(_val_, _reg_, _core_id_)	do {	\
	uint64_t val64 = IOREAD64_CR_REGIO(_reg_);				\
	if((vha->injection.conf_err & CONF_ERR_BOTTOM) && __EVENT_INJECT()) \
		val64 = ~val64;										\
	if (val64 != _val_) {									\
		cmd->conf_core_error |= 1 << _core_id_;				\
		dev_err(vha->dev, "Confirmation writes mismatch, core register: 0x%x \n  \
		 expected: 0x%016llx actual: 0x%016llx\n", VHA_CR_##_reg_, (uint64_t)_val_, val64);		\
	}} while(0)

#define CHECK_CORE_REG(_val_, _reg_, _core_id_)	do { 		\
	uint64_t val64 = IOREAD64_REGIO(_reg_); 				\
	if((vha->injection.conf_err & CONF_ERR_BOTTOM) && __EVENT_INJECT()) \
		val64 = ~val64;										\
	if (val64 != _val_) {									\
		cmd->conf_core_error |= 1 << _core_id_;				\
		dev_err(vha->dev, "Confirmation writes mismatch, core register: 0x%x \n  \
		 expected: 0x%016llx actual: 0x%016llx\n", _reg_, (uint64_t)_val_, val64);		\
	}} while(0)
#else
#define CHECK_TOP_REG(_val_, _reg_)	do {		\
	uint64_t val64 = IOREAD64_CR_REGIO(_reg_);	\
	if (val64 != _val_) {						\
		cmd->conf_top_error = true;				\
		dev_err(vha->dev, "Confirmation writes mismatch, top register: 0x%x \n 	\
		 expected: 0x%016llx actual: 0x%016llx\n", VHA_CR_##_reg_, (uint64_t)_val_, val64);		\
		goto out_error;							\
	}} while(0)

#define CHECK_CR_CORE_REG(_val_, _reg_, _core_id_)	do {	\
	uint64_t val64 = IOREAD64_CR_REGIO(_reg_);				\
	if (val64 != _val_) {									\
		cmd->conf_core_error |= 1 << _core_id_;				\
		dev_err(vha->dev, "Confirmation writes mismatch, core register: 0x%x \n  \
		 expected: 0x%016llx actual: 0x%016llx\n", VHA_CR_##_reg_, (uint64_t)_val_, val64);		\
	}} while(0)

#define CHECK_CORE_REG(_val_, _reg_, _core_id_)	do { 		\
	uint64_t val64 = IOREAD64_REGIO(_reg_); 				\
	if (val64 != _val_) {									\
		cmd->conf_core_error |= 1 << _core_id_;				\
		dev_err(vha->dev, "Confirmation writes mismatch, core register: 0x%x \n  \
		 expected: 0x%016llx actual: 0x%016llx\n", _reg_, (uint64_t)_val_, val64);		\
	}} while(0)
#endif

static bool vha_wm_confirm_config_regs(struct vha_cmd *cmd, struct vha_config_regs* regs)
{
	struct vha_hw_sched_info *sched_info = &cmd->hw_sched_info;
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	uint32_t reg_size = VHA_CR_OS0_CNN_ALT_ADDRESS1 - VHA_CR_OS0_CNN_ALT_ADDRESS0;
	uint32_t reg_base = VHA_CR_OS0_CNN_ALT_ADDRESS0;
	uint32_t reg_idx_offset = 0;
	uint32_t core_id = 0;
	int i;

	CHECK_TOP_REG(regs->core_assignment, CORE_ASSIGNMENT);

	for (core_id = 0; core_id < VHA_MAX_CORES; core_id++) {
		reg_base = VHA_CR_OS0_CNN_ALT_ADDRESS0;
		reg_idx_offset = 0;

		if (sched_info->core_mask & (1 << core_id)) {
			uint64_t curr_core = VHA_CORE_ID_TO_MASK(core_id);

			IOWRITE64_CR_REGIO(curr_core, CORE_CTRL_INDIRECT);

			CHECK_CR_CORE_REG(regs->cnn_control[core_id], OS0_CNN_CONTROL, core_id);
			CHECK_CR_CORE_REG(regs->cmd_base_addr[core_id], OS0_CNN_CMD_BASE_ADDRESS, core_id);

			if (regs->socm_base_addr != ~0) {
				CHECK_CR_CORE_REG(regs->socm_circ_buff_size, SOCM_CIRCULAR_BUFFER_SIZE, core_id);
				CHECK_CR_CORE_REG(regs->socm_base_addr, SOCM_BASE_ADDR, core_id);
				CHECK_CR_CORE_REG(regs->socm_buf_assignment, SOCM_BUF_ASSIGNMENT, core_id);

				if (regs->socm_b7_xor_bits)
					CHECK_CR_CORE_REG(regs->socm_b7_xor_bits, SOCM_B7_XOR_BITS, core_id);
				if (regs->socm_b8_xor_bits)
					CHECK_CR_CORE_REG(regs->socm_b8_xor_bits, SOCM_B8_XOR_BITS, core_id);
			}

			if (regs->locm_base_addr != ~0) {
				CHECK_CR_CORE_REG(regs->locm_base_addr, OS0_LOCM_BASE_ADDR, core_id);
			}

			for (i = 0; i < VHA_CORE_MAX_ALT_ADDRS; i++) {
				if (i >= 8) {
					reg_base = VHA_CR_OS0_CNN_ALT_ADDRESS8;
					reg_idx_offset = 8;
				}

				if (regs->cnn_alt_addr_used & (1 << i))
					CHECK_CORE_REG(regs->cnn_alt_addr[i], reg_base + (i - reg_idx_offset) * reg_size, core_id);
			}

			if (regs->low_level_sync_base_addr != ~0) {
				CHECK_CR_CORE_REG(regs->low_level_sync_base_addr, LOW_LEVEL_SYNC_BASE_ADDR, core_id);
			}

			if (!cnn_preloads_disable)
				CHECK_CR_CORE_REG(regs->cnn_alt_addr_used, OS0_CNN_ALT_ADDRESS_USED, core_id);

			CHECK_CR_CORE_REG(regs->cnn_vcore_mapping, OS0_CNN_VCORE_MAPPING, core_id);
		}
	}
out_error:
	return cmd->conf_top_error;
}

static bool vha_wm_confirm_mmu_regs(struct vha_cmd *cmd)
{
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	uint32_t ctx_id = 0;

	if (vha->mmu_mode == VHA_MMU_DISABLED) {
		CHECK_TOP_REG(VHA_CR_OS(MMU_CTRL_BYPASS_EN), OS0_MMU_CTRL);
		return 0;
	}

	for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++) {
		IOWRITE64_CR_REGIO(session->mmu_ctxs[ctx_id].hw_id, OS0_MMU_CBASE_MAPPING_CONTEXT);
		CHECK_TOP_REG(session->mmu_ctxs[ctx_id].pc_baddr, OS0_MMU_CBASE_MAPPING);
	}

out_error:
	return cmd->conf_top_error;
}

static bool vha_wm_confirm_mh_regs(struct vha_cmd *cmd, struct vha_mh_config_regs * regs) {
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;

	CHECK_TOP_REG(regs->cnn_preload_control, OS0_CNN_PRELOAD_CONTROL);
	CHECK_TOP_REG(regs->req_ctxt_override, REQ_CTXT_OVERRIDE);

	if (regs->slc_control)
		CHECK_TOP_REG(regs->slc_control, SLC_CTRL);

out_error:
	return cmd->conf_top_error;
}

static bool vha_wm_confirm_crc_regs(struct vha_cmd *cmd, struct vha_crc_config_regs * regs) {
	struct vha_hw_sched_info *sched_info = &cmd->hw_sched_info;
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	uint32_t core_id = 0;

	if (session->cnn_dbg.cnn_crc_buf[0] || vha->cnn_combined_crc_enable ) {
		for (core_id = 0; core_id < VHA_MAX_CORES; core_id++) {
			if (sched_info->core_mask & (1 << core_id)) {
				uint64_t curr_core = VHA_CORE_ID_TO_MASK(core_id);

				IOWRITE64_CR_REGIO(curr_core, CORE_CTRL_INDIRECT);

				CHECK_CR_CORE_REG(regs->crc_control, OS0_CNN_CRC_CONTROL, core_id);
				CHECK_CR_CORE_REG(regs->crc_mask_ctrl, OS0_CNN_CRC_MASK_CTRL, core_id);

				if (session->cnn_dbg.cnn_crc_buf[0])
					CHECK_CR_CORE_REG(regs->crc_address[core_id],
										OS0_CNN_CRC_ADDRESS, core_id);

				if (vha->cnn_combined_crc_enable)
					CHECK_CR_CORE_REG(regs->crc_combined_address[core_id],
					 					OS0_COMBINED_CNN_CRC_ADDRESS, core_id);
			}
		}
	}

	return false;
}
#endif
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
static int do_cmd_cnn_submit(struct vha_cmd *cmd, uint64_t *rsp_err_flags)
{
	const struct vha_user_cmd *user_cmd =
		(struct vha_user_cmd *)&cmd->user_cmd;
	struct vha_session *session = cmd->session;
	struct vha_hw_sched_info *sched_info = &cmd->hw_sched_info;
	struct vha_dev *vha = session->vha;
	int ret = -EINVAL;
	struct vha_config_regs regs;
	struct vha_mh_config_regs mh_regs;
	struct vha_crc_config_regs crc_regs;
#ifdef VHA_SCF
	int i;
#endif

	memset(&regs, 0, sizeof(regs));
	memset(&mh_regs, 0, sizeof(mh_regs));
	memset(&crc_regs, 0, sizeof(crc_regs));

	regs.socm_base_addr = ~0;
	regs.locm_base_addr = ~0;
	regs.low_level_sync_base_addr = ~0;

#ifdef VHA_SCF
	//initialize progress counters with max values possible
	for (i = 0; i < VHA_NUM_CORES; i++) {
		cmd->layer_count[i] = ~0;
		cmd->pass_count[i] = ~0;
	}
#endif

	if (vha->hw_bypass) {
		ret = -EAGAIN;
		dev_info(vha->dev, "%s skip\n", __func__);
		*rsp_err_flags |= VHA_RSP_ERROR(SW_SKIP_CMD);
		goto out_error;
	}

	img_pdump_printf("-- WM_SETUP_BEGIN\n");

	/* Select WM to submit this cmd to. */
	img_pdump_printf("-- Select WM%u\n", sched_info->wm_id);
	VHA_LOCK_WM();
	VHA_SELECT_WM(sched_info->wm_id);

	/* Wait for the previous kick to be accepted */
	if (vha->low_latency != VHA_LL_DISABLED) {
		/* Sanity wait for the WM kick bit to be deasserted */
		ret = IOPOLL64_CR_PDUMP(0, 1000, 10,
				(uint64_t)VHA_CR_BITMASK(WM_WL_CONTROL, WL_START),
				WM_WL_CONTROL);
		VHA_UNLOCK_WM();
		if(ret) {
			dev_err(vha->dev, "%s: WM%u kick bit read-back failed!\n",
					__func__, sched_info->wm_id);
			*rsp_err_flags |= VHA_RSP_ERROR(SW_KICK_BIT_READ_BACK_FAILURE);
			goto out_error;
		}
		if (cmd->queued &&
				vha->low_latency == VHA_LL_SW_KICK)
			goto hw_kick;
	} else {
		VHA_UNLOCK_WM();
	}

	ret = -EINVAL;

	if (vha->pendcmd[sched_info->wm_id].cmd != NULL &&
				vha->low_latency == VHA_LL_DISABLED) {
		dev_err(vha->dev, "%s: trying to submit workload on WM%u when hw busy!\n",
			__func__, sched_info->wm_id);
		*rsp_err_flags |= VHA_RSP_ERROR(SW_HW_BUSY);
		goto out_error;
	}

	if (user_cmd->cmd_type == VHA_CMD_CNN_SUBMIT_MULTI)
	{
		if (!vha_wm_setup_config_regs_multi(cmd, &regs)) {
			dev_err(vha->dev, "%s: invalid cmd info\n", __func__);
			*rsp_err_flags |= VHA_RSP_ERROR(SW_INVALID_CMD_INFO);
			goto out_error;
		}
	}
	else {
		dev_err(vha->dev, "%s: invalid cmd type %u\n",
				__func__, user_cmd->cmd_type);
		*rsp_err_flags |= VHA_RSP_ERROR(SW_INVALID_CMD_TYPE);
		ret = -EINVAL;
		goto out_error;
	}

	vha_wm_write_config_regs(cmd, &regs);
	/* write the stream size only */
	ret = 0;
	if (vha->pendcmd[cmd->hw_sched_info.wm_id].cmd) {
		vha->queuedcmd[cmd->hw_sched_info.wm_id].cmd = cmd;
		cmd->queued = true;
		vha->stats.cnn_kicks_queued++;
		img_pdump_printf("-- WM%u already kicked queueing!\n",
							cmd->hw_sched_info.wm_id);
		dev_dbg(vha->dev, "%s: WM%u already kicked. "
				"Queueing -> kicked: 0x%08x/%u, queueing: 0x%08x/%u\n",
				__func__, cmd->hw_sched_info.wm_id,
				vha->pendcmd[cmd->hw_sched_info.wm_id].cmd->user_cmd.cmd_id,
				vha->pendcmd[cmd->hw_sched_info.wm_id].cmd->session->id,
				cmd->user_cmd.cmd_id, session->id);
		if (vha->low_latency == VHA_LL_SW_KICK)
			return ret;
	}
hw_kick:
	/* Operate only on cores assigned to this WM. */
	img_pdump_printf("-- Select cores\n");
	IOWRITE64_CR_PDUMP(vha_wm_get_cores(vha, cmd->hw_sched_info.wm_id),
			CORE_CTRL_INDIRECT);

	/* Change mmu context */
	ret = vha_mmu_setup(cmd->session);
	if (ret) {
		dev_err(vha->dev, "%s: Error while MMU setup!\n", __func__);
		*rsp_err_flags |= VHA_RSP_ERROR(SW_MMU_SETUP_FAILURE);
		goto out_error;
	}
	/* Setup memory stuff */
	vha_dev_mh_setup(vha, session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].hw_id, &mh_regs);

	/* Prepare debug buffer registers */
	vha_dbg_prepare_hwbufs(session, cmd, &crc_regs);

	/* Setup cnn hw watchdog before kicking the hw */
	{
		uint64_t wl_cycles, core_cycles;

		vha_wm_hwwdt_calculate(vha, cmd, &wl_cycles, &core_cycles);
		vha_wm_hwwdt_setup(vha, cmd, wl_cycles, core_cycles);
	}

	img_pdump_printf("-- Select WM%d\n", cmd->hw_sched_info.wm_id);
	/* Select WM to setup. */
	VHA_LOCK_WM();
	VHA_SELECT_WM(cmd->hw_sched_info.wm_id);
	/* Generate and set workload id. */
	cmd->wm_cmd_id = ++vha->wm_cmd_id_count;
	cmd->wm_cmd_id = (cmd->wm_cmd_id & VHA_WL_KICK_ID_COUNT_MASK) |
						(cmd->hw_sched_info.wm_id << VHA_WL_KICK_ID_WM_ID_SHIFT);
	img_pdump_printf("-- Set workload id: %u\n", cmd->wm_cmd_id);
	IOWRITE64_CR_PDUMP(VHA_CR_SETBITS(WM_WL_ID, WL_ID, cmd->wm_cmd_id), WM_WL_ID);
	VHA_UNLOCK_WM();

	if (CMD_EXEC_ON_HW(cmd)) {
		cmd->in_hw = true;
		if (!cmd->queued)
			vha->pendcmd[cmd->hw_sched_info.wm_id].cmd = cmd;
	}
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
	/* Mark kick for dummy driver */
	cmd->dummy_kicked = true;
#endif

	/* Consider this WL as kicked. */
	vha->pri_q_counters[cmd->user_cmd.priority]--;

	img_pdump_printf("-- WM_SETUP_END\n");

	/* Remember the time cnn is kicked */
	GETNSTIMEOFDAY(&cmd->hw_proc_start);
	VHA_SET_WM_STAT(vha, hw_proc_start, cmd->hw_sched_info.wm_id, cmd->hw_proc_start);
	/* Need to generate proper pdump */
	if (cmd->queued &&
			vha->low_latency == VHA_LL_SW_KICK) {
		/* Do not write to pdump
		 * this needs to be done after irq POL*/
		VHA_LOCK_WM();
		VHA_SELECT_WM(cmd->hw_sched_info.wm_id);
		IOWRITE64_CR_REGIO(VHA_CR_WM_WL_CONTROL_WL_START_EN, WM_WL_CONTROL);
		VHA_UNLOCK_WM();
		VHA_INC_WL_STAT(vha, kicks_queued, cmd);
		dev_dbg(vha->dev, "%s: WM%u kick queued for cmd id 0x%08x/%u (WL kick id: 0x%08x)!\n",
				__func__, sched_info->wm_id, cmd->user_cmd.cmd_id, session->id, cmd->wm_cmd_id);
		cmd->queued = false;
	} else {
		img_pdump_printf("-- WM_KICK_BEGIN\n");
		img_pdump_printf("-- Select WM%u\n", sched_info->wm_id);
		VHA_LOCK_WM();
		VHA_SELECT_WM(cmd->hw_sched_info.wm_id);
		img_pdump_printf("-- WM kick!\n");
		IOWRITE64_CR_PDUMP(VHA_CR_WM_WL_CONTROL_WL_START_EN, WM_WL_CONTROL);
		VHA_UNLOCK_WM();
		if (cmd->queued)
			VHA_INC_WL_STAT(vha, kicks_queued, cmd);
		dev_dbg(vha->dev, "%s: WM%u %skick for cmd id 0x%08x/%u (WL kick id: 0x%08x)!\n",
				__func__, sched_info->wm_id, cmd->queued ? "queued " : "",
				cmd->user_cmd.cmd_id, session->id, cmd->wm_cmd_id);
		img_pdump_printf("-- WM_KICK_END\n");
	}

#ifdef VHA_SCF
	if (vha->confirm_config_reg) {
		if (vha_wm_confirm_config_regs(cmd, &regs))
			goto out_complete;
		if (vha_wm_confirm_mmu_regs(cmd))
			goto out_complete;
		if (vha_wm_confirm_mh_regs(cmd, &mh_regs))
			goto out_complete;
		vha_wm_confirm_crc_regs(cmd, &crc_regs);
out_complete:
		complete(&cmd->conf_done);
	}
#endif

	/* Update kick stats. */
	vha->stats.cnn_kicks++;
	VHA_INC_WL_STAT(vha, kicks, cmd);

	/* Notify any observers of the submit event. */
	if (vha_observers.submitted)
		vha_observers.submitted(vha->id, session->id, user_cmd->cmd_id, false, user_cmd->priority);


out_error:
	if (ret != 0) {
		/* Consider this WL as kicked for errors too. */
		vha->pri_q_counters[cmd->user_cmd.priority]--;
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
	uint64_t core_mask = VHA_CALIBRATION_CORE_MASK;
	uint64_t core_assignment;
	uint64_t val64 = 0;

	/* Use WM0 and core 0. */
	vha_wm_assign_cores(vha, VHA_CALIBRATION_WM_ID, VHA_CALIBRATION_CORE_MASK, &core_assignment);
	IOWRITE64_CR_PDUMP(core_assignment, CORE_ASSIGNMENT);

	/* Operate only on core 0. */
	IOWRITE64_CR_REGIO(core_mask, CORE_CTRL_INDIRECT);

	/* Setup core WDTs. */
	IOWRITE64_CR_REGIO(vha->calibration_cycles, CNN_WDT_COMPAREMATCH);
	val64 = VHA_SET_FIELD_SIMPLE_VAL(CNN_WDT_CTRL, MODE, KICK_PASS);
	IOWRITE64_CR_REGIO(val64, CNN_WDT_CTRL);

	IOWRITE64_CR_REGIO(VHA_CORE_MEM_WDT_CYCLES, CNN_MEM_WDT_COMPAREMATCH);
	val64 = VHA_SET_FIELD_SIMPLE_VAL(CNN_MEM_WDT_CTRL, MODE, KICK_PASS);
	IOWRITE64_CR_REGIO(val64, CNN_MEM_WDT_CTRL);

	/* Disabling command decoder, so we can generate WDT interrupt
	 * without providing any buffer address. */
	val64 = IOREAD64_CR_REGIO(CLK_CTRL0);
	VHA_CR_CLEARBITS(val64, CLK_CTRL0, CNN_CMD);
	IOWRITE64_CR_REGIO(val64, CLK_CTRL0);

	/* To be sure the command decoder clock has switched off. */
	udelay(100);

	/* Enable core only events */
	IOWRITE64_CR_REGIO(VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_HOST_ENABLE);
	IOWRITE64_CR_REGIO(VHA_CORE_EVENTS_DEFAULT, CORE_EVENT_HOST_CLEAR);

	/* Set minimum command stream size. */
	val64 = VHA_CR_SETBITS(OS0_CNN_CONTROL, CMD_SIZE_MIN1, (2048U/32-1));
	IOWRITE64_CR_REGIO(val64, OS0_CNN_CONTROL);

	/* Enable MMU bypass */
	IOWRITE64_PDUMP(VHA_CR_OS(MMU_CTRL_BYPASS_EN),
		VHA_CR_OS(MMU_CTRL));

	VHA_LOCK_WM();
	/* Select WM0 for calibration. */
	VHA_SELECT_WM(VHA_CALIBRATION_WM_ID);
	/* Disable WM events */
	IOWRITE64_CR_REGIO(0, WM_EVENT_ENABLE);
	/* Start WM0. */
	IOWRITE64_CR_REGIO(VHA_CR_WM_WL_CONTROL_WL_START_EN, WM_WL_CONTROL);
	VHA_UNLOCK_WM();
	/* Remember the time WM0 is kicked */
	GETNSTIMEOFDAY(&vha->stats.wm_stats[VHA_CALIBRATION_WM_ID].hw_proc_start);
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
void vha_cnn_cmd_completed(struct vha_cmd *cmd, uint64_t status, int err, uint64_t rsp_err_flags)
{
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	struct vha_rsp *rsp = NULL;
	int i;
	struct vha_user_cnn_submit_rsp * cnn_submit_rsp = NULL;

	const struct vha_user_cmd *user_cmd = &cmd->user_cmd;

	switch (user_cmd->cmd_type) {
	case VHA_CMD_CNN_SUBMIT_MULTI:
	{
		size_t mem_usage;
		/* allocate sufficient space for the response */
		size_t sz = sizeof(*rsp)
			+ sizeof(struct vha_user_cnn_submit_rsp)
			- sizeof(struct vha_user_rsp);
#ifdef VHA_SCF
		uint64_t wm_fifo_ready =
				VHA_CR_WM_EVENT_STATUS_TYPE_RESPONSE_FIFO_READY_EN |
				VHA_CR_WM_EVENT_STATUS_TYPE_PARITY_EN;
		uint64_t wm_fifo_mask =
				VHA_WM_EVENTS_DEFAULT | VHA_CR_WM_EVENT_STATUS_TYPE_PARITY_EN;

		uint64_t wm_fifo_status_success =
				VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_SUCCESS_EN |
				VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_PARITY_EN;
		uint64_t wm_fifo_status_mask =
				VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_MASKFULL;
#else
		uint64_t wm_fifo_ready =
				VHA_CR_WM_EVENT_STATUS_TYPE_RESPONSE_FIFO_READY_EN;
		uint64_t wm_fifo_mask = VHA_WM_EVENTS_DEFAULT;

		uint64_t wm_fifo_status_success =
				VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_SUCCESS_EN;
		uint64_t wm_fifo_status_mask =
				VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_MASKFULL &
				~VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_PARITY_EN;
#endif
		uint64_t wm_src_mask = VHA_CR_SETBITS(HOST_EVENT_SOURCE, WM,
								VHA_WM_ID_TO_MASK(cmd->hw_sched_info.wm_id)) |
								VHA_SET_FIELD_SIMPLE_VAL(HOST_EVENT_SOURCE, SYS, EN) |
								VHA_SET_FIELD_SIMPLE_FULL(HOST_EVENT_SOURCE, CORE) |
								VHA_SET_FIELD_SIMPLE_FULL(HOST_EVENT_SOURCE, IC);
		uint32_t num_cores;
		uint32_t outbuf_offset;
		uint32_t outbuf_last_idx;
		uint32_t outbuf_data_offset;
		uint32_t* bufoffsets;
		uint32_t* bufsizes;
		struct vha_user_cnn_submit_multi_cmd *msg;

		rsp = kzalloc(sz, GFP_KERNEL);
		if (rsp == NULL) {
			session->oom = true;
			return;
		}

		cnn_submit_rsp = (struct vha_user_cnn_submit_rsp*)&rsp->user_rsp;
		rsp->size = sizeof(struct vha_user_cnn_submit_rsp);

		if (vha->hw_bypass) {
			vha->hw_bypass--;
			break;
		}

		dev_dbg(vha->dev, "%s: 0x%08x/%u\n", __func__, cmd->user_cmd.cmd_id, session->id);

		img_pdump_printf("-- WM_WAIT_BEGIN\n");
		/* pdump POL for event source change
		 * count=cnn_pdump_poll_count, delay=1000cycles */
		img_pdump_printf("-- Wait for WM%u or any event source to be signalled\n"
				"POL :REG:%#x 0 %#llx 3 %u 1000\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_HOST_EVENT_SOURCE,
				wm_src_mask,
				cnn_pdump_poll_count);

		/* quick pdump POL for the related WM source flag only:
		 * count=1, delay=10cycles */
		img_pdump_printf("-- Check for WM%u source, all COREs/ICs & SYS\n"
				"POL :REG:%#x %#llx 0x%llx 0 %u 10\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_HOST_EVENT_SOURCE,
				VHA_CR_SETBITS(HOST_EVENT_SOURCE, WM,
						VHA_WM_ID_TO_MASK(cmd->hw_sched_info.wm_id)),
				wm_src_mask,
				wm_pdump_poll_count);

		/* quick pdump POL for the FIFO_READY flag only in related WM:
		 * count=1, delay=10cycles */
		img_pdump_printf("-- Select WM%u\n"
				"WRW64 :REG:%#x %#llx\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_TLC_WM_INDIRECT,
				(uint64_t)cmd->hw_sched_info.wm_id);
		img_pdump_printf("-- Check for WM%u FIFO_READY flag\n"
				"POL :REG:%#x %#llx 0x%llx 0 1 10\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_WM_EVENT_STATUS,
				wm_fifo_ready,
				wm_fifo_mask);

		/* quick pdump POL for AXI errors:
		 * count=1, delay=10cycles
		 */
		img_pdump_printf("-- Post check of AXI status\n"
				"POL :REG:%#x 0 0xffffffff 0 1 10\n",
				VHA_CR_ACE_STATUS);

		/* We do clear interrupts in the irq handler,
		 * but this is not recorded into pdump because
		 * of the irq context, so do it here */
		img_pdump_printf("-- Clear SYS events\n"
				"WRW64 :REG:%#x %#x\n",
				VHA_CR_SYS_EVENT_CLEAR,
				VHA_SYS_EVENTS_DEFAULT);
		img_pdump_printf("-- Clear WM%u events\n"
				"WRW64 :REG:%#x %#x\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_WM_EVENT_CLEAR,
				VHA_WM_EVENTS_DEFAULT);
		img_pdump_printf("-- Select core assigned to WM%u\n"
				"WRW64 :REG:%#x %#x\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_CORE_CTRL_INDIRECT,
				cmd->hw_sched_info.core_mask);
		img_pdump_printf("-- Clear core events\n"
				"WRW64 :REG:%#x %#x\n",
				VHA_CR_CORE_EVENT_HOST_CLEAR,
				VHA_CORE_EVENTS_DEFAULT);

		img_pdump_printf("-- Check RESPONSE_FIFO status for WM%u\n"
				"POL :REG:%#x %#llx 0x%llx 0 1 10\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_WM_RESPONSE_FIFO_WL_STATUS,
				wm_fifo_status_success,
				wm_fifo_status_mask);

		img_pdump_printf("-- Check RESPONSE_FIFO workload id for WM%u\n"
				"POL :REG:%#x %#llx 0x%llx 0 1 10\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_WM_RESPONSE_FIFO_WL_ID,
				(uint64_t)cmd->wm_cmd_id,
				VHA_CR_WM_RESPONSE_FIFO_WL_ID_MASKFULL);

		/* Pop the RESPONSE_FIFO */
		img_pdump_printf("-- Pop RESPONSE_FIFO for WM%u\n"
				"WRW64 :REG:%#x %#x\n",
				cmd->hw_sched_info.wm_id,
				VHA_CR_WM_RESPONSE_FIFO_READ,
				VHA_CR_WM_RESPONSE_FIFO_READ_FIFO_READ_EN);

#ifdef CONFIG_VHA_DUMMY
		vha_wm_release_cores(session->vha,
				cmd->hw_sched_info.core_mask, true);
#endif
		/* Try to flush hw debug buffers first
		 * - this does pdump SAB when proper checkpoint is set */
		vha_dbg_flush_hwbufs(session, 1, cmd->hw_sched_info.core_mask);

		/* pdump SAB for each of the output buffers */
		img_pdump_printf("-- Save outputs\n");
		msg = container_of(user_cmd, struct vha_user_cnn_submit_multi_cmd, msg);
		num_cores = msg->num_cores;
		outbuf_offset = VHA_MAX_CORES + (user_cmd->num_inbufs - num_cores);
		outbuf_last_idx = VHA_MAX_CORES + user_cmd->num_bufs - 1;
		outbuf_data_offset = user_cmd->num_inbufs - num_cores;
		bufoffsets = msg->bufoffsets;
		bufsizes = msg->bufsizes;

		/* There should be at least on output buffer */
		WARN_ON(outbuf_last_idx <= outbuf_offset);

		for (i = outbuf_offset; i < outbuf_last_idx; i++) {
			struct vha_buffer *buf;
			uint32_t offset;
			uint32_t size;

			buf = vha_find_bufid(session, user_cmd->data[i]);
			if (buf == NULL) {
				dev_err(vha->dev,
						"%s: invalid buffer id:%d\n",
						__func__, user_cmd->data[i]);
				continue;
			}
			offset = bufoffsets[outbuf_data_offset];
			size = bufsizes[outbuf_data_offset];
			outbuf_data_offset++;

			vha_pdump_sab_buf(session, PDUMP_RES, buf, offset, size);

			/* Update status, do not signal fence yet,
			 * it's is done explicitly below, after cache invalidation */
			vha_set_buf_status(session, buf->id, VHA_BUF_FILLED_BY_HW,
					VHA_SYNC_NONE, false);

			if (vha_buf_needs_inval(session, buf->id) && !status)
				img_mem_sync_device_to_cpu(session->mem_ctx, buf->id);

#ifdef KERNEL_DMA_FENCE_SUPPORT
			img_mem_signal_fence(session->mem_ctx, buf->id);
#endif
		}

		if (session->vha->low_latency == VHA_LL_SW_KICK) {
			struct vha_cmd *qcmd =
				session->vha->queuedcmd[cmd->hw_sched_info.wm_id].cmd;

			if (qcmd && qcmd->queued) {
				/* Setup kick info */
				img_pdump_printf("-- CNN kick (queued)!\n");
				img_pdump_printf("WRW64 :REG:%#x %#x\n",
						VHA_CR_WM_WL_CONTROL, VHA_CR_WM_WL_CONTROL_WL_START_EN);
			}
		}
		img_pdump_printf("-- WM_WAIT_END\n");

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
		cnn_submit_rsp->last_proc_us = session->vha->stats.cnn_last_proc_us;
		cnn_submit_rsp->hw_cycles = session->vha->stats.cnn_last_cycles;
		dev_dbg(session->vha->dev, "%s: 0x%08x/%u, hw_cycles %llx\n", __func__,
				cmd->user_cmd.cmd_id, session->id, session->vha->stats.cnn_last_cycles);

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
		rsp->user_rsp.err_no = session->vha->hw_bypass ? 0 : err;
		rsp->user_rsp.rsp_err_flags = rsp_err_flags;

		cmd->rsp = rsp;
	} else
		kfree(rsp);
}

static uint32_t get_estimated_cycles(const struct vha_user_cmd *user_cmd)
{
	const struct vha_user_cnn_submit_multi_cmd *cnn_user_cmd =
		(struct vha_user_cnn_submit_multi_cmd *)user_cmd;
	return cnn_user_cmd->estimated_cycles;
}

/*
 * Perform a command, as requested by user.
 * note: this function is called with vha_dev.lock == locked
 */
int vha_do_cnn_cmd(struct vha_cmd *cmd)
{
	struct vha_session *session = cmd->session;
	struct vha_dev *vha = session->vha;
	const struct vha_user_cmd *user_cmd = &cmd->user_cmd;
	int err = -EINVAL;
	uint64_t rsp_err_flags = 0;

	dev_dbg(vha->dev,
			"%s: WL id:0x%08x type:%x nin:%x nbufs:%x\n",
			__func__, user_cmd->cmd_id, user_cmd->cmd_type,
			user_cmd->num_inbufs, user_cmd->num_bufs);
	print_hex_dump_debug("VHA CMD: ", DUMP_PREFIX_NONE, 4, 4,
							user_cmd, ALIGN(cmd->size, 4), false);

	switch (user_cmd->cmd_type) {
		case VHA_CMD_CNN_SUBMIT_MULTI:
			err = do_cmd_cnn_submit(cmd, &rsp_err_flags);
#ifdef CONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
			if (cmd->dummy_kicked) {
				uint32_t estimated_cycles = get_estimated_cycles(user_cmd);

				if (estimated_cycles == 0)
					estimated_cycles = VHA_DUMMY_HW_PROCESSING_TIME_CYCLES;
				cmd->dummy_exec_time = (estimated_cycles / (vha->freq_khz / 1000));
				if (cmd->hw_sched_info.wm_id < vha->hw_props.num_cnn_core_devs)
					schedule_delayed_work(
							&vha->dummy_dworks[cmd->hw_sched_info.wm_id].dummy_dwork,
							usecs_to_jiffies(cmd->dummy_exec_time));
				cmd->dummy_kicked = false;
			}
#endif
			break;
		case VHA_CMD_CNN_PDUMP_MSG:
			err = do_cmd_cnn_pdump_msg(cmd);
			break;
		default:
			break;
	}

	/*
	 * Immediately send notification to user if not using hw at all
	 * or submitting failed.
	 */
	if (!CMD_EXEC_ON_HW(cmd) || err) {
		bool is_cnn_cmd = CMD_IS_CNN(cmd);
		vha_cnn_cmd_completed(cmd,
			err ? (uint64_t)VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_WL_FAILURE_EN : 0ULL, err, rsp_err_flags);
		if (is_cnn_cmd) {
			if (rsp_err_flags & VHA_RSP_ERROR(SW_MMU_SETUP_FAILURE))
				vha_wm_release_cores(vha, cmd->hw_sched_info.core_mask, false);
			/* Free current command */
			vha_dev_free_cmd_res(vha, cmd, false);
		}
		vha_cmd_notify(cmd);

		if (is_cnn_cmd) {
			if (rsp_err_flags & VHA_RSP_ERROR(SW_MMU_SETUP_FAILURE)) {
				/* Rollback commands being processed to perform full reset */
				vha_rollback_cmds(vha);
				/* Perform stop & reset eventually*/
				vha_dev_stop(vha, true);
				/* Reschedule commands */
				vha_chk_cmd_queues(vha, true);
			}
		}
		return 1;
	}

	return 0;
}

uint8_t vha_wm_get_cores(struct vha_dev *vha, uint8_t wm_id)
{
	uint8_t core_mask = 0;
	uint64_t wm_core_assignment;

#define CHECK_CORE_ASSIGNMENT(c) \
		if (wm_id == VHA_CR_GETBITS(CORE_ASSIGNMENT, CORE_##c##_WM_MAPPING, \
									wm_core_assignment)) \
			core_mask |= (1 << c);

	wm_core_assignment = vha->wm_core_assignment;
	dev_dbg(vha->dev, "%s: %llx\n", __func__, wm_core_assignment);
	CHECK_CORE_ASSIGNMENT(0);
	CHECK_CORE_ASSIGNMENT(1);
	CHECK_CORE_ASSIGNMENT(2);
	CHECK_CORE_ASSIGNMENT(3);
	CHECK_CORE_ASSIGNMENT(4);
	CHECK_CORE_ASSIGNMENT(5);
	CHECK_CORE_ASSIGNMENT(6);
	CHECK_CORE_ASSIGNMENT(7);

#undef CHECK_CORE_ASSIGNMENT

	return core_mask;
}

void vha_wm_assign_cores(struct vha_dev *vha, uint8_t wm_id, uint8_t core_mask, uint64_t *core_assignment)
{
	uint64_t wm_core_assignment = vha->wm_core_assignment;
	uint32_t assignment_field_shift =
					VHA_CR_CORE_ASSIGNMENT_CORE_1_WM_MAPPING_SHIFT -
								VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_SHIFT;
	uint64_t assignment_field_mask =
							~VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_CLRMSK;
	uint64_t wm_core_assignment_orig = wm_core_assignment;

	while (core_mask != 0) {
		uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(core_mask);

		core_mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id));

		wm_core_assignment &=
			~(assignment_field_mask << (curr_core_id * assignment_field_shift));
		wm_core_assignment |= wm_id << (curr_core_id * assignment_field_shift);
	}

	dev_dbg(vha->dev, "%s: %llx -> %llx\n", __func__, wm_core_assignment_orig, wm_core_assignment);
	*core_assignment = wm_core_assignment;
	vha->wm_core_assignment = wm_core_assignment;
}

static void wm_release_socm(struct vha_dev *vha, uint8_t core_mask, bool to_pdump)
{
	uint64_t cur_assignment = IOREAD64_CR_REGIO(SOCM_BUF_ASSIGNMENT);
	uint32_t assignment_field_shift =
				VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_1_WM_MAPPING_SHIFT -
					VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_0_WM_MAPPING_SHIFT;
	uint64_t assignment_field_mask =
					~VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_0_WM_MAPPING_CLRMSK;
	uint64_t new_assignment = cur_assignment;
	uint64_t mask = core_mask;

	while (mask != 0) {
			uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(mask);

			mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id));

			new_assignment &=
				~(assignment_field_mask << (curr_core_id * assignment_field_shift));
			new_assignment |= VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_0_WM_MAPPING_UNALLOCATED
					<< (curr_core_id * assignment_field_shift);
	}

	if (cur_assignment == new_assignment) {
		dev_dbg(vha->dev, "%s: %llx -> %llx (no change)\n", __func__, cur_assignment, new_assignment);
		return;
	}

	dev_dbg(vha->dev, "%s: %llx -> %llx\n", __func__, cur_assignment, new_assignment);
	if (to_pdump) {
		img_pdump_printf("-- Release SOCM on cores 0x%02x\n", core_mask);
		IOWRITE64_CR_PDUMP(new_assignment, SOCM_BUF_ASSIGNMENT);
	} else
		IOWRITE64_CR_REGIO(new_assignment, SOCM_BUF_ASSIGNMENT);
}

void vha_wm_release_cores(struct vha_dev *vha, uint8_t core_mask, bool to_pdump)
{
	uint64_t cur_assignment = vha->wm_core_assignment;
	uint32_t assignment_field_shift =
					VHA_CR_CORE_ASSIGNMENT_CORE_1_WM_MAPPING_SHIFT -
								VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_SHIFT;
	uint64_t assignment_field_mask =
							~VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_CLRMSK;
	uint64_t new_assignment = cur_assignment;
	uint64_t mask = core_mask;

	wm_release_socm(vha, core_mask, to_pdump);

	while (mask != 0) {
		uint32_t curr_core_id = VHA_CORE_MASK_TO_ID(mask);

		mask &= ~(VHA_CORE_ID_TO_MASK(curr_core_id));

		new_assignment &=
			~(assignment_field_mask << (curr_core_id * assignment_field_shift));
		new_assignment |=
			VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_UNALLOCATED <<
										(curr_core_id * assignment_field_shift);
	}

	if (cur_assignment == new_assignment) {
		dev_dbg(vha->dev, "%s: %llx -> %llx (no change)\n", __func__, cur_assignment, new_assignment);
		return;
	}

	dev_dbg(vha->dev, "%s: %llx -> %llx\n", __func__, cur_assignment, new_assignment);
	if (to_pdump) {
		img_pdump_printf("-- Release cores 0x%02x\n", core_mask);
		IOWRITE64_CR_PDUMP(new_assignment, CORE_ASSIGNMENT);
	} else
		IOWRITE64_CR_REGIO(new_assignment, CORE_ASSIGNMENT);
	vha->wm_core_assignment = new_assignment;
}

int vha_wm_reset(struct vha_dev *vha, struct vha_hw_sched_info *sched_info)
{
	uint64_t val64 = 0;
	uint64_t wm_reset_val64 = 0;
	uint8_t wm_cores_mask = 0;
	uint8_t core_mask = 0;
	uint8_t id;
	int ret = 0;

	dev_dbg(vha->dev, "%s: WM%d\n", __func__, sched_info->wm_id);

	img_pdump_printf("-- WM level RESET sequence BEGIN\n");

	/* Perform reset procedure */

	/* Operate only on cores assigned to this WM. */
	wm_cores_mask = sched_info->core_mask;

	/* Core Level Reset Assertion:
	 * 4. Force global clocks on current core (others set to AUT0). */
	img_pdump_printf("-- Force global clocks ON for all cores assigned to WM %u"
						" (others set to AUTO)\n", sched_info->wm_id);
	val64 = VHA_SYS_CLOCK_MODE(INTERCONNECT, ON) |
			VHA_SYS_CLOCK_MODE_MULTI(CORE, ON, wm_cores_mask) |
			VHA_SYS_CLOCK_MODE_MULTI(CORE, AUTO, (uint8_t)~wm_cores_mask) |
			VHA_SYS_CLOCK_MODE_MULTI(NOC, AUTO, ~0) |
			VHA_SYS_CLOCK_MODE_MULTI(WM, AUTO, ~0) |
			VHA_SYS_CLOCK_MODE(AXI, AUTO) |
			VHA_SYS_CLOCK_MODE(SLC, AUTO) |
			VHA_SYS_CLOCK_MODE(LSYNC, AUTO) |
			VHA_SYS_CLOCK_MODE(SOCM, AUTO) |
			VHA_SYS_CLOCK_MODE(REGBANK, AUTO);
	IOWRITE64_CR_PDUMP(val64, SYS_CLK_CTRL0);

	/* WM reset procedure start. */
	/* Move this WM into reset state. */
	img_pdump_printf("-- Move WM%u into reset state\n", sched_info->wm_id);
	wm_reset_val64 = VHA_CR_SETBITS(SYS_RESET_CTRL, WM, VHA_WM_ID_TO_MASK(sched_info->wm_id));
	IOWRITE64_CR_PDUMP(wm_reset_val64, SYS_RESET_CTRL);
	/* Dummy read to avoid race conditions in the hw */
	val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);

	/* Core Level Reset Reset Sequence */

	/* Proceed core by core. */
	while (wm_cores_mask) {
		/* Reset Assertion */

		/* 1. Select current core. */
		id = ffs(wm_cores_mask) - 1;
		img_pdump_printf("-- Select core%u\n", id);
		core_mask = VHA_CORE_ID_TO_MASK(id);
		wm_cores_mask &= ~core_mask;
		IOWRITE64_CR_PDUMP(core_mask, CORE_CTRL_INDIRECT);
		/* 3. Disable page fault interrupts for core while resetting. */
		img_pdump_printf("-- Disable page fault interrupts for core%u\n", id);
		val64 = IOREAD64_CR_REGIO(SYS_EVENT_ENABLE);
		val64 &= ~(VHA_CR_SETBITS(SYS_EVENT_ENABLE, MMU_PAGE_FAULT, core_mask));
		IOWRITE64_CR_PDUMP(val64, SYS_EVENT_ENABLE);
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
		ret = IOPOLL64_CR_PDUMP(val64, 1000, 1000,
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
		IOWRITE64_CR_PDUMP(wm_reset_val64, SYS_RESET_CTRL);
		/* 11. Move current core into full reset state. Leave WM in reset. */
		img_pdump_printf("-- Move core%u into full reset state\n", id);
		val64 =  VHA_CR_SETBITS(SYS_RESET_CTRL, CORE, core_mask);
		val64 |= wm_reset_val64;
		IOWRITE64_CR_PDUMP(val64, SYS_RESET_CTRL);
		/* 12. Dummy read to avoid race conditions in the hw. */
		val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);

		/* Reset Deassertion */

		/* 1. Move current core out of reset state. */
		img_pdump_printf("-- Move core%u out of reset state\n", id);
		IOWRITE64_CR_PDUMP(wm_reset_val64, SYS_RESET_CTRL);
		/*    Dummy read to avoid race conditions in the hw. */
		val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);
		/* 2. Select current core again. */
		img_pdump_printf("-- Select core%u again\n", id);
		IOWRITE64_CR_PDUMP(core_mask, CORE_CTRL_INDIRECT);
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
		ret = IOPOLL64_CR_PDUMP(0ULL, 10, 10, val64, CORE_EVENT_HOST_STATUS);
		if(ret)
			return ret;
		/* 10. Wait until the LOCM scrubbing sequence has completed. */
		img_pdump_printf("-- Wait until the LOCM scrubbing sequence has completed.\n");
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_STATUS, LOCM_SCRUB_DONE, EN);
		ret = IOPOLL64_CR_PDUMP(val64, 1000, 1000,
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
		/*    Confirm that 'LOCM_SCRUB_DONE' field is cleared. */
		img_pdump_printf("-- Confirm that core%u LOCM scrub interrupt is cleared\n", id);
		val64 = VHA_SET_FIELD_SIMPLE_VAL(CORE_EVENT_HOST_STATUS, LOCM_SCRUB_DONE, EN);
		ret = IOPOLL64_CR_PDUMP(0ULL, 10, 10, val64, CORE_EVENT_HOST_STATUS);
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
		/* 15. Enable the interrupts from interconnect to WM. */
		img_pdump_printf("-- Enable INTERCONNECT events to WM\n");
		IOWRITE64_CR_PDUMP(VHA_IC_EVENTS_DEFAULT, INTERCONNECT_EVENT_WM_ENABLE);
		/* 16. Disable all interrupts from the CORE to the HOST. */
		img_pdump_printf("-- Disable CORE events on host\n");
		IOWRITE64_CR_PDUMP(0, CORE_EVENT_HOST_ENABLE);
		/* 17. Set all core level clocks back to AUTO. */
		img_pdump_printf("-- Set all core%u level clocks back to AUTO\n", id);
		val64 = VHA_MAIN_CLOCKS_DEFAULT(AUTO);
		IOWRITE64_CR_PDUMP(val64, CLK_CTRL0);
		/* 18. Set core global clock back to AUTO. */
		img_pdump_printf("-- Set core%u global clock back to AUTO (others set to ON or AUTO)\n", id);
		if (wm_cores_mask == 0) {
			val64 = VHA_SYS_CLOCKS_DEFAULT(AUTO);
			IOWRITE64_CR_PDUMP(val64, SYS_CLK_CTRL0);
		} else {
			val64 = VHA_SYS_CLOCK_MODE(INTERCONNECT, ON) |
					VHA_SYS_CLOCK_MODE_MULTI(CORE, ON, wm_cores_mask) |
					VHA_SYS_CLOCK_MODE_MULTI(CORE, AUTO, (uint8_t)~wm_cores_mask) |
					VHA_SYS_CLOCK_MODE_MULTI(NOC, AUTO, ~0) |
					VHA_SYS_CLOCK_MODE_MULTI(WM, AUTO, ~0) |
					VHA_SYS_CLOCK_MODE(AXI, AUTO) |
					VHA_SYS_CLOCK_MODE(SLC, AUTO) |
					VHA_SYS_CLOCK_MODE(LSYNC, AUTO) |
					VHA_SYS_CLOCK_MODE(SOCM, AUTO) |
					VHA_SYS_CLOCK_MODE(REGBANK, AUTO);
			IOWRITE64_CR_PDUMP(val64, SYS_CLK_CTRL0);
		}

		/* Setup stalling if requested. */
		if (vha->stalling_membus_sys_stall_ratio != 0)
			IOWRITE64_CR_REGIO(vha->stalling_membus_sys_stall_ratio,
								NN_SYS2_MEMBUS_SYS_STALL_RATIO);
	}

	/* WM reset procedure end. */
	/* Move this WM out of reset state. */
	img_pdump_printf("-- Move WM%u out of reset state\n", sched_info->wm_id);
	IOWRITE64_CR_PDUMP(0ULL, SYS_RESET_CTRL);
	/* Dummy read to avoid race conditions in the hw */
	val64 = IOREAD64_CR_PDUMP(SYS_RESET_CTRL);
	img_pdump_printf("-- WM level RESET sequence END\n");

	return 0;
}

void vha_wm_hwwdt_calculate(struct vha_dev *vha, struct vha_cmd *cmd,
		uint64_t *wl_cycles, uint64_t *core_cycles)
{
	if (use_estimated_cycles_for_wm_wdt) {
		/* Using values defined in MBS */
		*wl_cycles = (uint64_t)get_estimated_cycles(&cmd->user_cmd) +
						(uint64_t)wm_wl_wdt_estimated_cycles_margin;
		*core_cycles = cnn_hl_wdt_cycles;
	} else {
		/* Using values defined as kernel param */
		*wl_cycles = wm_wl_wdt_cycles;
		*core_cycles = cnn_hl_wdt_cycles;
	}
}

void vha_wm_hwwdt_setup(struct vha_dev *vha, struct vha_cmd *cmd,
						uint64_t wl_cycles, uint64_t core_cycles)
{
	uint64_t val64 = 0;
	uint64_t hw_brns =
			((struct vha_user_cnn_submit_multi_cmd*)&cmd->user_cmd)->hw_brns;
	uint8_t wm_id = cmd->hw_sched_info.wm_id;

	img_pdump_printf("-- Set SYSTEM watchdogs \n");
	/* Setup system WDTs. */
	IOWRITE64_CR_PDUMP(VHA_SYS_MEM_WDT_CYCLES, SYS_MEM_WDT_COMPAREMATCH);
	val64 = VHA_SET_FIELD_SIMPLE_VAL(SYS_MEM_WDT_CTRL, MODE, KICK_WL);
	IOWRITE64_CR_PDUMP(val64, SYS_MEM_WDT_CTRL);

	img_pdump_printf("-- Set WM%d watchdogs \n", wm_id);
	VHA_LOCK_WM();
	VHA_SELECT_WM(wm_id);
	/* Setup WM WDTs. */
	IOWRITE64_CR_PDUMP(wl_cycles, WM_WL_WDT_COMPAREMATCH);
	//val64 = VHA_SET_FIELD_SIMPLE_VAL(WM_WL_WDT_CTRL, MODE, KICK_WL);
	val64 = VHA_CR_SETBITS(WM_WL_WDT_CTRL, MODE, wm_wl_wdt_mode);
	IOWRITE64_CR_PDUMP(val64, WM_WL_WDT_CTRL);

	IOWRITE64_CR_PDUMP(VHA_WM_IDLE_WDT_CYCLES, WM_WL_IDLE_WDT_COMPAREMATCH);
	val64 = VHA_SET_FIELD_SIMPLE_VAL(WM_WL_IDLE_WDT_CTRL, MODE, ENABLED);
	IOWRITE64_CR_PDUMP(val64, WM_WL_IDLE_WDT_CTRL);

	IOWRITE64_CR_PDUMP(VHA_WM_SOCIF_WDT_CYCLES, WM_SOCIF_WDT_COMPAREMATCH);
	val64 = VHA_SET_FIELD_SIMPLE_VAL(WM_SOCIF_WDT_CTRL, MODE, ENABLED);
	IOWRITE64_CR_PDUMP(val64, WM_SOCIF_WDT_CTRL);
	VHA_UNLOCK_WM();

	/* Operate only on cores assigned to this WM. */
	img_pdump_printf("-- Select cores\n");
	IOWRITE64_CR_PDUMP(vha_wm_get_cores(vha, wm_id),
			CORE_CTRL_INDIRECT);
	img_pdump_printf("-- Set CORE watchdogs \n");
	/* Setup core WDTs. */
	IOWRITE64_CR_PDUMP(core_cycles, CNN_WDT_COMPAREMATCH);
	val64 = VHA_CR_SETBITS(CNN_WDT_CTRL, MODE, cnn_hl_wdt_mode);
	IOWRITE64_CR_PDUMP(val64, CNN_WDT_CTRL);

	if (VHA_IS_BRN(hw_brns, 71556) ||
			VHA_IS_BRN(hw_brns, 71338))
		/* Always set max value */
		IOWRITE64_CR_PDUMP(VHA_CR_CNN_MEM_WDT_COMPAREMATCH_MASKFULL, CNN_MEM_WDT_COMPAREMATCH);
	else
		IOWRITE64_CR_PDUMP(cnn_mem_wdt_cycles, CNN_MEM_WDT_COMPAREMATCH);

	val64 = VHA_CR_SETBITS(CNN_MEM_WDT_CTRL, MODE, cnn_mem_wdt_mode);
	IOWRITE64_CR_PDUMP(val64, CNN_MEM_WDT_CTRL);

	val64 = VHA_CR_SETBITS(CNN_CORE_SYNC_WDT_CTRL, ENABLE,
							VHA_CR_CNN_CORE_SYNC_WDT_CTRL_ENABLE_EN) |
			VHA_CR_SETBITS(CNN_CORE_SYNC_WDT_CTRL, VALUE,
							VHA_CORE_SYNC_WDT_CYCLES);
	IOWRITE64_CR_PDUMP(val64, CNN_CORE_SYNC_WDT_CTRL);
}

void vha_wm_status(struct vha_dev *vha, uint8_t wm_id, uint8_t core_mask)
{
	uint64_t wm_status;

	dev_err(vha->dev, " WM%u failure:\n", wm_id);
	/* Select WM to read from. */
	VHA_LOCK_WM();
	VHA_SELECT_WM(wm_id);
	wm_status = IOREAD64_CR_REGIO(WM_STATUS);
	VHA_UNLOCK_WM();

	dev_err(vha->dev, "  WM_STATUS:      0x%016llx\n",
			wm_status);
	dev_err(vha->dev, "  LLSYNC_STATUS:  0x%016llx\n",
			IOREAD64_CR_REGIO(LOW_LEVEL_SYNC_STATUS));

	while (core_mask != 0) {
		uint32_t core_id = VHA_CORE_MASK_TO_ID(core_mask);

		dev_err(vha->dev, "  core%u:\n", core_id);

		IOWRITE64_CR_REGIO(VHA_CR_SETBITS(CORE_CTRL_INDIRECT, MASK, (1 << core_id)),
										 CORE_CTRL_INDIRECT);

		dev_err(vha->dev, "    CNN_STATUS:  0x%016llx\n",
				IOREAD64_CR_REGIO(OS0_CNN_STATUS));
		dev_err(vha->dev, "    CNN_STATUS2: 0x%016llx\n",
				IOREAD64_CR_REGIO(OS0_CNN_STATUS2));
		{
			uint64_t reg = VHA_CR_CORE0_LAST_NNA_SYNC_ID +
					core_id * (VHA_CR_CORE1_LAST_NNA_SYNC_ID - VHA_CR_CORE0_LAST_NNA_SYNC_ID);
			dev_err(vha->dev, "    LAST_NNA_SYNC_ID: 0x%016llx\n",
					IOREAD64(vha->reg_base, reg));
			reg = VHA_CR_CORE0_LAST_MMM_SYNC_ID +
					core_id * (VHA_CR_CORE1_LAST_MMM_SYNC_ID - VHA_CR_CORE0_LAST_MMM_SYNC_ID);
			dev_err(vha->dev, "    LAST_MMM_SYNC_ID: 0x%016llx\n",
					IOREAD64(vha->reg_base, reg));
		}

		core_mask &= ~(VHA_CORE_ID_TO_MASK(core_id));
	}
}


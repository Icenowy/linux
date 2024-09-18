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

#include <uapi/vha.h>
#include "vha_common.h"
#include "vha_plat.h"
#include <vha_regs.h>

int vha_mmu_flush_ctx(struct vha_dev *vha, int ctx_id)
{
	int ret;
	uint64_t inval =
			VHA_SET_FIELD_SIMPLE_VAL(OS0_MMU_CTRL_INVAL, PC, EN) |
			VHA_SET_FIELD_SIMPLE_VAL(OS0_MMU_CTRL_INVAL, PD, EN) |
			VHA_SET_FIELD_SIMPLE_VAL(OS0_MMU_CTRL_INVAL, PT, EN);
	uint64_t pend = VHA_SET_FIELD_SIMPLE_VAL(OS0_MMU_CTRL_INVAL_STATUS, PENDING, EN);

	/* No need to handle mmu cache, when core is already offline */
	if (vha->state == VHA_STATE_OFF)
		return 0;

#ifdef VHA_SCF
	if (vha->hw_props.supported.parity && !vha->parity_disable) {
		/* If pending bit is set then parity bit must be set as well ! */
		pend |= VHA_SET_FIELD_SIMPLE_VAL(OS0_MMU_CTRL_INVAL_STATUS, PARITY, EN);
	}
#endif
	ret = IOPOLL64_CR_PDUMP_PARITY(0, 30, 150, pend, OS0_MMU_CTRL_INVAL_STATUS);
	if (ret) {
		dev_err(vha->dev, "Error during MMU ctx %d flush\n", ctx_id);
	} else {
		if (unlikely(ctx_id == VHA_INVALID_ID))
			inval |= VHA_SET_FIELD_SIMPLE_VAL(OS0_MMU_CTRL_INVAL, ALL_CONTEXTS, EN);
		else {
			inval |= VHA_CR_SETBITS(OS0_MMU_CTRL_INVAL, CONTEXT, (uint64_t)ctx_id);
		}
		inval |= VHA_CR_SETBITS(OS0_MMU_CTRL_INVAL, CONTEXT, (uint64_t)ctx_id);
		dev_dbg(vha->dev, "%s: ctx_id:%d (0x%llx)\n", __func__, ctx_id, inval);

		img_pdump_printf("-- MMU invalidate TLB caches\n");
		IOWRITE64_CR_PDUMP(inval, OS0_MMU_CTRL_INVAL);
	}

	return ret;
}

/* this function is called from img_mmu, to handle cache issues */
int vha_mmu_callback(enum img_mmu_callback_type callback_type,
			int buf_id, void *data)
{
	struct vha_session *session = data;
	struct vha_dev *vha = session->vha;
	int ctx_id;
	int ret = 0;

	if (!vha)
		return 0;

	for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++)
		ret |= vha_mmu_flush_ctx(vha, session->mmu_ctxs[ctx_id].hw_id);

	if (ret) {
		dev_err(vha->dev, "Error during MMU flush (2), resetting the HW...\n");
		/* Rollback commands being processed */
		vha_rollback_cmds(vha);
		/* Perform full reset */
		vha_dev_stop(vha, true);
		/* Reschedule */
		vha_chk_cmd_queues(vha, true);
	}

	return ret;
}

static void do_mmu_ctx_setup(struct vha_dev *vha,
			uint8_t hw_id, int pc_bufid, uint32_t pc_baddr)
{
	img_pdump_printf("-- Setup MMU context:%d\n", hw_id);
	IOWRITE64_CR_PDUMP(hw_id, OS0_MMU_CBASE_MAPPING_CONTEXT);

	if (!vha->mmu_base_pf_test) {
		IOWRITE64(vha->reg_base, VHA_CR_OS0_MMU_CBASE_MAPPING, pc_baddr);

		/* This is physical address so we need use MEM_OS0:BLOCK tag
		 * when pdump'ing. */
		img_pdump_printf("-- Setup MMU base address\n"
				"WRW "_PMEM_":$0 "_PMEM_":BLOCK_%d:0 -- 'PC'\n"
				"SHR "_PMEM_":$0 "_PMEM_":$0 %d\n"
				"WRW64 :REG:%#x "_PMEM_":$0\n", pc_bufid,
				IMG_MMU_PC_ADDR_SHIFT,
				VHA_CR_OS0_MMU_CBASE_MAPPING);
		dev_dbg(vha->dev, "%s: setting hardware ctx id:%u\n", __func__, hw_id);
	} else
		dev_info(vha->dev, "Bringup test: force MMU base page fault\n");
}

int vha_mmu_setup(struct vha_session *session)
{
	struct vha_dev *vha = session->vha;
	int ctx_id;
	int ret = 0;

	for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++)
		dev_dbg(vha->dev,
				"%s: mode:%d session ctxid:%x active ctxid:%x\n",
				__func__, vha->mmu_mode,
				session->mmu_ctxs[ctx_id].id,
				vha->active_mmu_ctx);

	if (vha->mmu_mode == VHA_MMU_DISABLED) {
		img_pdump_printf("-- MMU bypass ON\n");
		IOWRITE64_PDUMP(VHA_CR_OS(MMU_CTRL_BYPASS_EN),
			VHA_CR_OS(MMU_CTRL));
		return 0;
	}

	/* Using model context to track active context */
	if (session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].id == vha->active_mmu_ctx)
		return 0;

	img_pdump_printf("-- MMU_SETUP_BEGIN\n");
	img_pdump_printf("-- MMU bypass OFF\n");
	IOWRITE64_PDUMP(0, VHA_CR_OS(MMU_CTRL));

	for (ctx_id = 0; ctx_id < ARRAY_SIZE(session->mmu_ctxs); ctx_id++) {
		do_mmu_ctx_setup(vha, session->mmu_ctxs[ctx_id].hw_id,
				session->mmu_ctxs[ctx_id].pc_bufid,
				session->mmu_ctxs[ctx_id].pc_baddr);

		/* If there are multiple sessions using the same mmu hardware context
		 * we need to flush caches for the old context (id is the same).
		 * This will happen when number of processes is > VHA_MMU_MAX_HW_CTXS */
		if (vha->mmu_ctxs[session->mmu_ctxs[ctx_id].hw_id] > 1) {
			dev_dbg(vha->dev, "%s: flushing shared ctx id:%u\n",
						__func__, session->mmu_ctxs[ctx_id].hw_id);
			ret = vha_mmu_flush_ctx(vha, session->mmu_ctxs[ctx_id].hw_id);
			if (ret) {
				dev_err(vha->dev, "Error during MMU flush, resetting the HW...\n");
				goto mmu_setup_err;
			}
		}
	}

	/* Using model context to track context change */
	vha->active_mmu_ctx = session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].id;
	dev_dbg(vha->dev, "%s: update ctx id active:%x pc:%#x\n",
			__func__, vha->active_mmu_ctx,
			session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].pc_baddr <<
			VHA_CR_OS0_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT);
mmu_setup_err:
	img_pdump_printf("-- MMU_SETUP_END\n");
	return ret;
}

void vha_mmu_status(struct vha_dev *vha, uint8_t core_mask)
{
	const char levels[][5] = {"PT", "PD", "PC", "BASE"};
	uint32_t core_mmu_fault_reg_set_base = VHA_CR_CORE0_MMU_FAULT_STATUS1;
	uint32_t core_mmu_fault_reg_set_offset =
			VHA_CR_CORE1_MMU_FAULT_STATUS1 - VHA_CR_CORE0_MMU_FAULT_STATUS1;
	uint32_t core_mmu_fault_status1_offset =
			VHA_CR_CORE0_MMU_FAULT_STATUS1 - core_mmu_fault_reg_set_base;
	uint32_t core_mmu_fault_status2_offset =
			VHA_CR_CORE0_MMU_FAULT_STATUS2 - core_mmu_fault_reg_set_base;

	while (core_mask) {
		uint8_t id = ffs(core_mask) - 1;
		uint64_t status1 = IOREAD64_REGIO(core_mmu_fault_reg_set_base +
										id* core_mmu_fault_reg_set_offset +
										core_mmu_fault_status1_offset);
		uint64_t status2 = IOREAD64_REGIO(core_mmu_fault_reg_set_base +
										id* core_mmu_fault_reg_set_offset +
										core_mmu_fault_status2_offset);

#define MMU_FAULT_GETBITS(sreg, field, val) \
	_get_bits(val, VHA_CR_CORE0_MMU_FAULT_ ## sreg ## _ ## field ## _SHIFT, \
			~VHA_CR_CORE0_MMU_FAULT_ ## sreg ## _ ## field ## _CLRMSK)

		uint64_t addr  = MMU_FAULT_GETBITS(STATUS1, ADDRESS, status1);
		uint8_t level  = MMU_FAULT_GETBITS(STATUS1, LEVEL, status1);
		uint8_t req_id = MMU_FAULT_GETBITS(STATUS1, REQ_ID, status1);
		uint8_t ctx    = MMU_FAULT_GETBITS(STATUS1, CONTEXT, status1);
		uint8_t rnw    = MMU_FAULT_GETBITS(STATUS1, RNW, status1);
		uint8_t type   = MMU_FAULT_GETBITS(STATUS1, TYPE, status1);
		uint8_t fault  = MMU_FAULT_GETBITS(STATUS1, FAULT, status1);

		uint8_t bif_id    = MMU_FAULT_GETBITS(STATUS2, BIF_ID, status2);
		uint8_t tlb_entry = MMU_FAULT_GETBITS(STATUS2, TLB_ENTRY, status2);
		uint8_t slc_bank  = MMU_FAULT_GETBITS(STATUS2, BANK, status2);
		uint64_t mapping  = 0;

#undef MMU_FAULT_GETBITS

		/* Select context and read current pc */
		IOWRITE64_CR_REGIO(ctx, OS0_MMU_CBASE_MAPPING_CONTEXT);
		mapping = IOREAD64_CR_REGIO(OS0_MMU_CBASE_MAPPING);

		/* false alarm ? */
		if (!fault)
			return;

		dev_dbg(vha->dev, "%s: Core%u MMU FAULT: s1:%llx s2:%llx\n",
				__func__, id, status1, status2);

		dev_warn(vha->dev, "%s: MMU fault while %s @ 0x%llx\n",
				__func__, (rnw) ? "reading" : "writing", addr << 4);
		dev_warn(vha->dev, "%s: level:%s Requestor:%x Context:%x Type:%s\n",
				__func__, levels[level], req_id, ctx,
				(type == 0) ? "VALID" :
				(type == 2) ? "READ-ONLY" :
				"UNKNOWN");
		dev_warn(vha->dev, "%s: bif_id:%x tlb_entry:%x slc_bank:%x\n",
				__func__, bif_id, tlb_entry, slc_bank);
		dev_warn(vha->dev, "%s: current mapping@context%d:%#llx\n",
				__func__, ctx,
				mapping <<
				VHA_CR_OS0_MMU_CBASE_MAPPING_BASE_ADDR_ALIGNSHIFT);

		core_mask &= ~(VHA_CORE_ID_TO_MASK(id));
	}
}

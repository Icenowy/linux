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

#include <linux/device.h>
#include <linux/moduleparam.h>

#include "vha_common.h"
#include "vha_plat.h"
#include "vha_regs.h"

static uint32_t os_priority = _OSID_;
module_param(os_priority, uint, 0444);
MODULE_PARM_DESC(os_priority, "Kick priority for this driver instance: <0,3>");

static uint32_t prio_limits;
module_param(prio_limits, uint, 0444);
MODULE_PARM_DESC(prio_limits, "Priority limits. Valid for OS0 only. See TRM");

static uint32_t hl_wdt_cycles = VHA_CORE_WDT_CYCLES;
module_param(hl_wdt_cycles, uint, 0444);
MODULE_PARM_DESC(hl_wdt_cycles, "High level watchdog cycles");

static uint32_t hl_wdt_mode = 1;
module_param(hl_wdt_mode, uint, 0444);
MODULE_PARM_DESC(hl_wdt_mode, "High level watchdog mode: 1-pass; 2-layer group. See TRM");

void vha_dev_mh_setup(struct vha_dev *vha, int ctx_id, struct vha_mh_config_regs *regs)
{
	uint64_t val64 = 0;

	val64 |= VHA_CR_SETBITS_OS(CNN_PRELOAD_CONTROL, CBUF_N_REQS,
				VHA_CR_CNN_PRELOAD_CTRL_N_64);
	/* Setup preload for MMM */
	val64 |= VHA_CR_SETBITS_OS(CNN_PRELOAD_CONTROL, MMM_RD_N_REQS, VHA_CR_CNN_PRELOAD_CTRL_N_256);
	val64 |= VHA_CR_SETBITS_OS(CNN_PRELOAD_CONTROL, MMM_WR_N_REQS, VHA_CR_CNN_PRELOAD_CTRL_N_256);

	IOWRITE64_PDUMP(val64, VHA_CR_OS(CNN_PRELOAD_CONTROL));
}

void vha_dev_hwwdt_setup(struct vha_dev *vha, uint64_t cycles, uint64_t mode)
{
	img_pdump_printf("-- Setup High level watchdog\n");
	IOWRITE64_PDUMP((cycles & VHA_CR_CNN_HL_WDT_COMPAREMATCH_MASKFULL),
			VHA_CR_CNN_HL_WDT_COMPAREMATCH);
	IOWRITE64_PDUMP(hl_wdt_mode,
			VHA_CR_CNN_HL_WDT_CTRL);
	IOWRITE64_PDUMP(0, VHA_CR_CNN_HL_WDT_TIMER);

	/* Setup memory watchdog */
	IOWRITE64(vha->reg_base, VHA_CR_CNN_MEM_WDT_COMPAREMATCH, VHA_CORE_MEM_WDT_CYCLES);
	IOWRITE64(vha->reg_base, VHA_CR_CNN_MEM_WDT_CTRL,
			VHA_CR_CNN_MEM_WDT_CTRL_CNN_MEM_WDT_CTRL_KICK_PASS);
	IOWRITE64(vha->reg_base, VHA_CR_CNN_MEM_WDT_TIMER, 0);
}

int vha_dev_hwwdt_calculate(struct vha_dev *vha, struct vha_cmd *cmd,
		uint64_t *cycles, uint64_t *mode)
{
	if (!cycles || !mode)
		return -EINVAL;

	return -EIO;
}

int vha_dev_prepare(struct vha_dev *vha)
{
	/* Enable core events */
	img_pdump_printf("-- Enable CORE events\n");
	IOWRITE64_PDUMP(VHA_CORE_EVNTS, VHA_CR_OS(VHA_EVENT_ENABLE));
	img_pdump_printf("-- Clear CORE events\n");
	IOWRITE64_PDUMP(VHA_CORE_EVNTS, VHA_CR_OS(VHA_EVENT_CLEAR));

	return 0;
}

void vha_dev_setup(struct vha_dev *vha)
{
	uint64_t val64;

	vha_dev_hwwdt_setup(vha, hl_wdt_cycles, 0);
	if (prio_limits) {
		img_pdump_printf("-- Set priority limits\n");
		IOWRITE64_PDUMP(prio_limits, VHA_CR_CNN_CMD_PRIORITY_LIMITS);
	}

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
}

void vha_dev_wait(struct vha_dev *vha)
{
	uint32_t ready_val = VHA_CR_OS(VHA_EVENT_STATUS_VHA_READY_EN);
	uint32_t ready_mask = 0xffffffff;
	/* Ignore PARITY when waiting for status change */
	uint32_t status_mask = VHA_CR_OS(VHA_EVENT_STATUS_PARITY_CLRMSK);

#ifdef VHA_SCF
		if (vha->hw_props.supported.parity &&
				!vha->parity_disable) {
			/* If READY bit is set then parity bit must be set as well ! */
			ready_val |= VHA_CR_OS(VHA_EVENT_STATUS_PARITY_EN);
		}
#else
		/* Ignore PARITY, so that non-SCF pdump may work with SC CSIM */
		ready_mask &= VHA_CR_OS(VHA_EVENT_STATUS_PARITY_CLRMSK);
#endif

	/* Wait for READY interrupt as well
	 * pdump POL for any status flag:
	 * count=100, delay=100cycles
	 */
	img_pdump_printf("-- Wait for any CORE status change\n"
			"POL :REG:%#x 0 %#x 3 1000 1000\n",
			 VHA_CR_OS(VHA_EVENT_STATUS), status_mask);

	/* quick pdump POL for the status READY flag only:
	 * count=1, delay=10cycles
	 */
	img_pdump_printf("-- Check for READY flag only\n"
			"POL :REG:%#x %#x %#x 0 1 10\n",
			 VHA_CR_OS(VHA_EVENT_STATUS),
			 ready_val, ready_mask);
	/* We do clear interrupts in the irq handler,
	 * but this is not recorded into pdump because
	 * of the irq context, so do it here */
	img_pdump_printf("-- Clear CORE events\n"
			"WRW64 :REG:%#x %#x\n",
			 VHA_CR_OS(VHA_EVENT_CLEAR),
			 VHA_CR_OS(VHA_EVENT_CLEAR_VHA_READY_EN) |
			 VHA_CR_OS(VHA_EVENT_CLEAR_VHA_ERROR_EN) |
			 VHA_CR_OS(VHA_EVENT_CLEAR_VHA_HL_WDT_EN));
}

uint32_t vha_dev_kick_prepare(struct vha_dev *vha,
				 struct vha_cmd *cmd, int ctx_id)
{
	/* write to the START bit */
	uint32_t val32 = (min(2048U, cmd->stream_size)/32-1)
		<< VHA_CR_OS(CNN_CONTROL_CMD_SIZE_MIN1_SHIFT);
	val32 |= VHA_CR_OS(CNN_CONTROL_START_EN);

	/* This is odd, hw uses two contexts, we provide the base one,
	 * but the other is always used in pair */
	img_pdump_printf("-- CNN setup CTXT_PASID:%d PRIO:%d\n",
			ctx_id, os_priority);
	val32 |= VHA_CR_SETBITS_OS(CNN_CONTROL,
			CTXT_PASID, ctx_id);
	val32 |= VHA_CR_SETBITS_OS(CNN_CONTROL,
			PRIORITY, os_priority);

	return val32;
}

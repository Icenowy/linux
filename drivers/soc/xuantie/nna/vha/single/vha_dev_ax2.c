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

static long cnn_wdt_cycles = VHA_CORE_WDT_CYCLES;
module_param(cnn_wdt_cycles, long, 0444);
MODULE_PARM_DESC(cnn_wdt_cycles,
		"CNN hw watchdog expiration cycles, -1=use estimated cycles, 0=disable watchdog, >0=predefined");
static uint32_t cnn_wdt_cycles_margin = 40;
module_param(cnn_wdt_cycles_margin, uint, 0444);
MODULE_PARM_DESC(cnn_wdt_cycles_margin,
		 "CNN estimated hw watchdog percentage overhead. default:40% additional margin added");

void vha_dev_mh_setup(struct vha_dev *vha, int ctx_id, struct vha_mh_config_regs *regs)
{
	uint64_t val64 = 0;
	uint8_t burst = ilog2(VHA_CORE_MH_MAX_BURST_LENGTH/32);

	WARN_ON(burst & ~VHA_CR_MH_CONTROL_MAX_BURST_LENGTH_MASK);
	val64 |= VHA_CR_SETBITS(CNN_CMD_MH_CONTROL,
			MAX_BURST_LENGTH, burst);
	val64 |= VHA_CR_SETBITS(CNN_CMD_MH_CONTROL,
			GPU_PIPE_COHERENT, VHA_CORE_MH_GPU_PIPE_COHERENT_TYPE);
	val64 |= VHA_CR_SETBITS(CNN_CMD_MH_CONTROL,
			SLC_CACHE_POLICY, VHA_CORE_MH_SLC_CACHE_POLICY_TYPE);
	val64 |= VHA_CR_SETBITS(CNN_CMD_MH_CONTROL,
			PERSISTENCE, VHA_CORE_MH_PERSISTENCE_PRIO);

	img_pdump_printf("-- CNN mem hierarchy setup CTXT_PASID:%d\n", ctx_id);
	val64 |= VHA_CR_SETBITS(CNN_CMD_MH_CONTROL,
			CTXT_PASID, ctx_id);

	/* Note: CMD reg has different layout than IBUF,CBUF,ABUFF,OUPACK */
	IOWRITE64_PDUMP(val64, VHA_CR_CNN_CMD_MH_CONTROL);

	val64 = 0;
	val64 |= VHA_CR_SETBITS(CNN_IBUF_MH_CONTROL,
			MAX_BURST_LENGTH, burst);
	val64 |= VHA_CR_SETBITS(CNN_IBUF_MH_CONTROL,
			GPU_PIPE_COHERENT, VHA_CORE_MH_GPU_PIPE_COHERENT_TYPE);
	val64 |= VHA_CR_SETBITS(CNN_IBUF_MH_CONTROL,
			PERSISTENCE, VHA_CORE_MH_PERSISTENCE_PRIO);

	IOWRITE64_PDUMP(val64, VHA_CR_CNN_IBUF_MH_CONTROL);
	IOWRITE64_PDUMP(val64, VHA_CR_CNN_CBUF_MH_CONTROL);
	IOWRITE64_PDUMP(val64, VHA_CR_CNN_ABUF_MH_CONTROL);
	IOWRITE64_PDUMP(val64, VHA_CR_CNN_OUTPACK_MH_CONTROL);
	IOWRITE64_PDUMP(val64, VHA_CR_CNN_ELEMENTOPS_MH_CONTROL);
}

void vha_dev_hwwdt_setup(struct vha_dev *vha, uint64_t cycles, uint64_t mode)
{
	if (!mode)
		mode = VHA_CR_CNN_WDT_CTRL_CNN_WDT_CTRL_KICK_PASS;

	dev_dbg(vha->dev, "%s: cycles:%llx mode:%llx\n", __func__, cycles, mode);
	/* Note: Do not pdump the main watchdog as it may trigger
	 * during memory latency/stalling testing */
	if (cycles) {
		IOWRITE64(vha->reg_base, VHA_CR_CNN_WDT_COMPAREMATCH,
			cycles & VHA_CR_CNN_WDT_COMPAREMATCH_MASKFULL);
		IOWRITE64(vha->reg_base, VHA_CR_CNN_WDT_CTRL,
			mode & VHA_CR_CNN_WDT_CTRL_CNN_WDT_CTRL_MASK);
	} else {
		IOWRITE64(vha->reg_base, VHA_CR_CNN_WDT_CTRL,
			VHA_CR_CNN_WDT_CTRL_CNN_WDT_CTRL_NONE);
	}
	/* Clear timer value just for sanity */
	IOWRITE64(vha->reg_base, VHA_CR_CNN_WDT_TIMER, 0);
	/* Note: We are not enabling MEM_WDT because it will not detect
	 * issues due to the BIF/MMU or the customers memory fabric.
	 * We could in theory enable this in customer systems,
	 * but there is always a risk that it would result in false negatives
	 * if there memory latency went very high temporarily.
	 * HW team set this watchdog externally */
#if 0
	IOWRITE64(vha->reg_base, VHA_CR_CNN_MEM_WDT_COMPAREMATCH, 0xfffff);
	IOWRITE64(vha->reg_base, VHA_CR_CNN_MEM_WDT_CTRL,
		VHA_CR_CNN_MEM_WDT_CTRL_CNN_MEM_WDT_CTRL_KICK_PASS);
	/* Clear timer value */
	IOWRITE64(vha->reg_base + VHA_CR_CNN_MEM_WDT_TIMER, 0);
#endif
}

int vha_dev_hwwdt_calculate(struct vha_dev *vha, struct vha_cmd *cmd,
		uint64_t *cycles, uint64_t *mode)
{
	const struct vha_user_cnn_submit_cmd *user_cmd =
		(struct vha_user_cnn_submit_cmd *)&cmd->user_cmd;

	if (!cycles || !mode)
		return -EINVAL;

	if (user_cmd && user_cmd->estimated_cycles && cnn_wdt_cycles == -1) {
		/* allow 40%, by default, above the estimated cycle count.
		 * Clamp at uint32_t maximum */
		uint64_t wdt_cycles = user_cmd->estimated_cycles;
		uint64_t margin = wdt_cycles * cnn_wdt_cycles_margin;

		do_div(margin, 100UL);
		dev_dbg(vha->dev,
			"%s: estimated wdt cycles:%llx + margin:%llx\n",
			__func__, wdt_cycles, margin);
		wdt_cycles += margin;
		if (wdt_cycles > 0xffffffff)
			wdt_cycles = 0xffffffff;
		/* estimated cycle is per segment */
		*cycles = wdt_cycles;
		*mode = VHA_CR_CNN_WDT_CTRL_CNN_WDT_CTRL_KICK;
	} else {
		/* default value is per pass.
		 * If default is 0 cycles it disables the watchdog */
		*cycles = cnn_wdt_cycles;
		*mode = VHA_CR_CNN_WDT_CTRL_CNN_WDT_CTRL_KICK_PASS;
	}
	vha->wdt_mode = *mode;

	return 0;
}

int vha_dev_prepare(struct vha_dev *vha)
{
	/* Nothing to do */
	return 0;
}

void vha_dev_setup(struct vha_dev *vha)
{
	vha->is_ready = true;
}

void vha_dev_wait(struct vha_dev *vha)
{
	/* Nothing to do */
}

uint32_t vha_dev_kick_prepare(struct vha_dev *vha,
				 struct vha_cmd *cmd, int ctx_id)
{
	/* write to the START bit */
	uint32_t val = (min(2048U, cmd->stream_size)/32-1)
		<< VHA_CR_OS(CNN_CONTROL_CMD_SIZE_MIN1_SHIFT);
	val |= VHA_CR_OS(CNN_CONTROL_START_EN);

	return val;
}

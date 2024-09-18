/*!
 *****************************************************************************
 *
 * @File       vha_errors.h
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

#ifndef _VHA_ERRORS_H
#define _VHA_ERRORS_H

#if defined(__cplusplus)
extern "C" {
#endif

enum {
	/* System errors */
	VHA_RSP_ERROR_HW_SYS_AXI_ERROR_SHF = 0,
	VHA_RSP_ERROR_HW_SYS_MMU_PAGE_FAULT_SHF,
	VHA_RSP_ERROR_HW_SYS_SYS_MEM_WDT_SHF,
	VHA_RSP_ERROR_HW_SYS_AXI_MEMORY_PARITY_ERROR_SHF,
	VHA_RSP_ERROR_HW_SYS_MMU_PARITY_ERROR_SHF,
	VHA_RSP_ERROR_HW_SYS_RAM_CORRECTION_SHF,
	VHA_RSP_ERROR_HW_SYS_RAM_DETECTION_SHF,
	VHA_RSP_ERROR_HW_SYS_LSYNC_INV_REQ_SHF,
	VHA_RSP_ERROR_HW_SYS_LOGIC_ERROR_SHF,
	VHA_RSP_ERROR_SW_SYS_EVNT_PARITY_ERROR_SHF,
	VHA_RSP_ERROR_SW_WDT_EXPIRED_SHF,
	/* WM event errors */
	VHA_RSP_ERROR_HW_EVNT_WM_WL_WDT_SHF,
	VHA_RSP_ERROR_HW_EVNT_WM_WL_IDLE_WDT_SHF,
	VHA_RSP_ERROR_HW_EVNT_WM_SOCIF_WDT_SHF,
	VHA_RSP_ERROR_HW_EVNT_LOGIC_FAULT_SHF,
	VHA_RSP_ERROR_SW_EVNT_WM_PARITY_ERROR_SHF,
	/* WM response FIFO errors */
	VHA_RSP_ERROR_HW_CORE_IRQ_BEFORE_KICK_SHF,
	VHA_RSP_ERROR_HW_INDIRECT_MASK_SET_ERROR_SHF,
	VHA_RSP_ERROR_HW_KICK_CORE_ACCESS_ERROR_SHF,
	VHA_RSP_ERROR_HW_CNN_CONTROL_START_HIGH_SHF,
	VHA_RSP_ERROR_HW_CNN_STATUS_ERROR_SHF,
	VHA_RSP_ERROR_HW_INT_CORE_ACCESS_ERROR_SHF,
	VHA_RSP_ERROR_HW_CORE_EVENT_ERROR_SHF,
	VHA_RSP_ERROR_HW_CORE_EVENT_NOT_CLEARED_SHF,
	VHA_RSP_ERROR_HW_CORE_EVENT_IRQ_HIGH_SHF,
	VHA_RSP_ERROR_HW_INTERCONNECT_ERROR_SHF,
	VHA_RSP_ERROR_SW_WM_PARITY_ERROR_SHF,
	VHA_RSP_ERROR_SW_WL_ID_MISMATCH_ERROR_SHF,
	VHA_RSP_ERROR_SW_CONF_ERROR_SHF,
	VHA_RSP_ERROR_SW_CRC_MISMATCH_ERROR_SHF,
	/* CNN core status errors. */
	VHA_RSP_ERROR_HW_CORE_LOGIC_ERROR_SHF,
	VHA_RSP_ERROR_HW_RAM_CORRECTION_SHF,
	VHA_RSP_ERROR_HW_RAM_DETECTION_SHF,
	VHA_RSP_ERROR_HW_CORE_SYNC_ERROR_SHF,
	VHA_RSP_ERROR_HW_CORE_WDT_SHF,
	VHA_RSP_ERROR_HW_CORE_MEM_WDT_SHF,
	VHA_RSP_ERROR_HW_CORE_CNN_ERROR_SHF,
	/* Interconnect status errors. */
	VHA_RSP_ERROR_HW_LOCKSTEP_ERROR_SHF,
	VHA_RSP_ERROR_HW_IC_LOGIC_ERROR_SHF,
	VHA_RSP_ERROR_HW_SOCIF_READ_MISMATCH_SHF,
	VHA_RSP_ERROR_HW_SOCIF_READ_UNRESPONSIVE_SHF,
	VHA_RSP_ERROR_SW_IC_PARITY_ERROR_SHF,
	/* Workload submit errors. */
	VHA_RSP_ERROR_SW_SKIP_CMD_SHF,
	VHA_RSP_ERROR_SW_KICK_BIT_READ_BACK_FAILURE_SHF,
	VHA_RSP_ERROR_SW_HW_BUSY_SHF,
	VHA_RSP_ERROR_SW_INVALID_CMD_INFO_SHF,
	VHA_RSP_ERROR_SW_INVALID_CMD_TYPE_SHF,
	VHA_RSP_ERROR_SW_MMU_SETUP_FAILURE_SHF
};

#define VHA_RSP_ERROR(err) (1ull << (VHA_RSP_ERROR_##err##_SHF))

#if defined(__cplusplus)
}
#endif

#endif /* _VHA_ERRORS_H */

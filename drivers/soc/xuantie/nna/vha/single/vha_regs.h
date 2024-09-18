/*!
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
#include "../vha_io.h"

#if defined(HW_AX2)
#include <hwdefs/vha_cr_mirage.h>
#elif defined(HW_AX3)
#include <hwdefs/vha_cr_aura.h>
#else
#error "No HW layout defined"
#endif

#if defined(CFG_SYS_MAGNA)
#include <hwdefs/magna_system.h>
#elif defined(CFG_SYS_VAGUS)
#include <hwdefs/vagus_system.h>
#elif defined(CFG_SYS_AURA)
#include <hwdefs/aura_system.h>
#elif defined(CFG_SYS_MIRAGE)
#include <hwdefs/mirage_system.h>
#endif

/* HW Series AURA or MIRAGE */
#if defined(HW_AX2)
#define HW_SERIES (23U)
#elif defined(HW_AX3)
#if defined(CONFIG_VHA_NEXEF)
/* 3NX-F use a different B. value */
#define HW_SERIES (32U)
#else
#define HW_SERIES (28U)
#endif
#else
#error "No HW Series defined"
#endif

/* Events macros definition */
#define VHA_EVENT_TYPE(name) \
		VHA_CR_VHA_EVENT_TYPE_VHA_##name##_EN

#if defined(HW_AX2)
#define VHA_CNN_ERR_EVNTS (VHA_EVENT_TYPE(CNN0_ERROR) |\
			VHA_EVENT_TYPE(CNN0_MEM_WDT) |\
			VHA_EVENT_TYPE(CNN0_WDT))

#define VHA_CORE_EVNTS (VHA_EVENT_TYPE(MMU_PAGE_FAULT) |\
			VHA_EVENT_TYPE(AXI_ERROR))
#elif defined(HW_AX3)
#define VHA_CNN_ERR_EVNTS (VHA_EVENT_TYPE(CNN0_ERROR) |\
			VHA_EVENT_TYPE(CNN0_MEM_WDT))

#ifdef VHA_SCF
#define VHA_CORE_EVNTS ( \
			VHA_EVENT_TYPE(MMU_PARITY_ERROR) |\
			VHA_EVENT_TYPE(PARITY_ERROR) |\
			VHA_EVENT_TYPE(LOCKSTEP_ERROR) |\
			VHA_EVENT_TYPE(READY) |\
			VHA_EVENT_TYPE(ERROR) |\
			VHA_EVENT_TYPE(HL_WDT) |\
			VHA_EVENT_TYPE(MMU_PAGE_FAULT) |\
			VHA_EVENT_TYPE(AXI_ERROR))
#else  /*!VHA_SCF */
#define VHA_CORE_EVNTS ( \
			VHA_EVENT_TYPE(READY) |\
			VHA_EVENT_TYPE(ERROR) |\
			VHA_EVENT_TYPE(HL_WDT) |\
			VHA_EVENT_TYPE(MMU_PAGE_FAULT) |\
			VHA_EVENT_TYPE(AXI_ERROR))
#endif /* VHA_SCF */
#endif  /* HW_AX3 */

/* ignore bottom 4 bits of CONFIG_ID: they identify different build variants */
#define VHA_CR_CORE_ID_BVNC_CLRMSK (0xfffffffffffffff0ULL)
#define VHA_CNN_CMPLT_EVNT (VHA_EVENT_TYPE(CNN0_COMPLETE))
#define VHA_CNN_EVNTS (VHA_CNN_ERR_EVNTS | VHA_CNN_CMPLT_EVNT)

#define VHA_EVNTS_DEFAULT ( ( \
		VHA_CNN_EVNTS | VHA_CORE_EVNTS \
		) & VHA_CR_OS(VHA_EVENT_ENABLE_MASKFULL))

#define VHA_SYS_CLOCK_MODE(name, mode) \
		VHA_CR_SYS_CLK_CTRL0_##name##_##mode \

#define VHA_SYS_CLOCKS_DEFAULT(mode) ( (\
			VHA_SYS_CLOCK_MODE(SLC, mode) \
			) & VHA_CR_SYS_CLK_CTRL0_MASKFULL)

/* Clocks macros definition */
#define VHA_MAIN_CLOCK_MODE(name, mode) \
		VHA_CR_CLK_CTRL0_##name##_##mode \

#if defined(HW_AX2)
#define VHA_MAIN_CLOCKS_DEFAULT(mode) ( (\
			VHA_MAIN_CLOCK_MODE(CNN_EWO, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_PACK, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_OIN, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_POOL, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_SB, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_XBAR, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_NORM, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_ACT, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_ACCUM, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CNV, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CBUF, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_IBUF, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CMD, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN, mode) | \
			VHA_MAIN_CLOCK_MODE(SLC, mode) | \
			VHA_MAIN_CLOCK_MODE(BIF, mode) \
			) & VHA_CR_CLK_CTRL0_MASKFULL)
#elif defined(HW_AX3)
#define VHA_MAIN_CLOCKS_DEFAULT(mode) ( (\
			VHA_MAIN_CLOCK_MODE(CNN_CORE_XBAR, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_MMM, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_EWO, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_PACK, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_OIN, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_POOL, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_SB, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_NORM, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_ACT, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_ACCUM, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CNV, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CBUF, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_IBUF, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CMD, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_TRS_A, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_TRS_B, mode) | \
			VHA_MAIN_CLOCK_MODE(SLC, mode) | \
			VHA_MAIN_CLOCK_MODE(BIF, mode) \
			) & VHA_CR_CLK_CTRL0_MASKFULL)
#endif

/* Reset macros definition */
#define VHA_RESET_EN(name) \
		VHA_CR_RESET_CTRL_VHA_##name##_EN

#define VHA_RESET_DEFAULT ( ( \
			VHA_RESET_EN(SYS_SOFT_RESET) | \
			VHA_RESET_EN(AXI_SOFT_RESET) | \
			VHA_RESET_EN(CNN0_SOFT_RESET) | \
			VHA_RESET_EN(SLC_SOFT_RESET) | \
			VHA_RESET_EN(BIF_SOFT_RESET) | \
			VHA_RESET_EN(SOFT_RESET) \
			) & VHA_CR_RESET_CTRL_MASKFULL)

/* NN_SYS register macros */
#define NN_SYS_CR_BASE \
		(_REG_NNSYS_START)

#define NN_SYS_CR(reg) \
		(_REG_NNSYS_START + NN_SYS_CR_##reg)

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
#ifndef VHA_REGS_H
#define VHA_REGS_H

#include "../vha_io.h"

#if defined(HW_AX3)
#include <hwdefs/vha_cr_magna.h>
#else
#error "No HW layout defined"
#endif

#if defined(CFG_SYS_MAGNA)
#include <hwdefs/magna_system.h>
#endif

#define HW_SERIES (28U)

/* General register macros */
#define VHA_GET_FIELD_FULL_MASK(reg, field) \
		(VHA_CR_BITMASK(reg, field) >> VHA_CR_##reg##_##field##_SHIFT)
#define VHA_SET_FIELD_SIMPLE_VAL(reg, field, type) \
		(VHA_CR_##reg##_##field##_##type)
#define VHA_SET_FIELD_SIMPLE_FULL(reg, field) \
		VHA_CR_BITMASK(reg, field)

#define VHA_WM_ID_TO_MASK(i)   (1 << i)
#define VHA_CORE_ID_TO_MASK(i) (1 << i)
#define VHA_WM_MASK_TO_ID(m) ({	\
		uint8_t _ret_ = m;		\
		WARN_ON(!_ret_);	\
		do {		\
			if (!_ret_) break;	\
			_ret_ = ffs(_ret_) - 1; \
		} while (0);	\
		_ret_;	\
	})
#define VHA_CORE_MASK_TO_ID(m) VHA_WM_MASK_TO_ID(m)
#define VHA_CORE_MASK_TO_NUM(m) ({	\
		uint8_t _ret_ = m;		\
		WARN_ON(!_ret_);	\
		do {		\
			if (!_ret_) break;	\
			_ret_ = _ret_ - ((_ret_ >> 1) & 0x55);	\
			_ret_ = (_ret_ & 0x33) + ((_ret_ >> 2) & 0x33);	\
			_ret_ = (((_ret_ + (_ret_ >> 4)) & 0x0F) * 0x01);	\
		} while (0);	\
		_ret_;	\
	})

#if defined(CONFIG_VHA_DUMMY)
#define VHA_LOCK_WM()
#define VHA_UNLOCK_WM()
#else
#define VHA_LOCK_WM() spin_lock_irq(&vha->irq_lock)
#define VHA_UNLOCK_WM() spin_unlock(&vha->irq_lock)
#endif

#define VHA_SELECT_WM(wm) ({	\
		uint64_t reg = 0;\
		uint8_t c = 10;\
		IOWRITE64_CR_PDUMP(VHA_CR_SETBITS(TLC_WM_INDIRECT, ADDRESS, (uint64_t)wm), \
											 TLC_WM_INDIRECT);	\
		do {	\
			reg = IOREAD64_CR_REGIO(TLC_WM_INDIRECT); \
			c--;\
		} while ((reg != wm) && c > 0);	\
		WARN_ON(c == 0);	\
	})

/* Clock calibration defines. */
#define VHA_CALIBRATION_WM_ID     0
#define VHA_CALIBRATION_CORE_ID   0
#define VHA_CALIBRATION_CORE_MASK (1 << VHA_CALIBRATION_CORE_ID)

/* Event macro definitions */
#define VHA_SYS_EVENT_TYPE(name) \
		VHA_CR_BITMASK(SYS_EVENT_TYPE, name)
#define VHA_WM_EVENT_TYPE(name) \
		VHA_CR_BITMASK(WM_EVENT_TYPE, name)
#define VHA_CORE_EVENT_TYPE(name) \
		VHA_CR_BITMASK(CORE_EVENT_TYPE, name)
#define VHA_IC_EVENT_TYPE(name) \
		VHA_CR_BITMASK(INTERCONNECT_EVENT_TYPE, name)

#define VHA_SYS_EVENTS ( \
		VHA_SYS_EVENT_TYPE(RAM_INIT_DONE    ) | \
		VHA_SYS_EVENT_TYPE(MEMBUS_RESET_DONE))
#ifdef VHA_SCF
#define VHA_SYS_SCF_ERR_EVENTS ( \
		VHA_SYS_EVENT_TYPE(LOGIC_ERROR  ) | \
		VHA_SYS_EVENT_TYPE(RAM_CORRECTION  ) | \
		VHA_SYS_EVENT_TYPE(RAM_DETECTION   ) | \
		VHA_SYS_EVENT_TYPE(MMU_PARITY_ERROR) | \
		VHA_SYS_EVENT_TYPE(AXI_MEMORY_PARITY_ERROR))

#else
#define VHA_SYS_SCF_ERR_EVENTS (0)
#endif
#define VHA_SYS_ERR_EVENTS ( \
		VHA_SYS_SCF_ERR_EVENTS | \
		VHA_SYS_EVENT_TYPE(LSYNC_INV_REQ   ) | \
		VHA_SYS_EVENT_TYPE(SYS_MEM_WDT     ) | \
		VHA_SYS_EVENT_TYPE(MMU_PAGE_FAULT  ) | \
		VHA_SYS_EVENT_TYPE(AXI_ERROR       ))
#define VHA_SYS_EVENTS_DEFAULT ( \
		VHA_SYS_EVENTS | \
		VHA_SYS_ERR_EVENTS)

#define VHA_WM_EVENTS ( \
		VHA_WM_EVENT_TYPE(RESPONSE_FIFO_READY))
#ifdef VHA_SCF
#define VHA_WM_SCF_ERR_EVENTS ( \
		VHA_WM_EVENT_TYPE(LOGIC_FAULT   ))
#else
#define VHA_WM_SCF_ERR_EVENTS (0)
#endif
#define VHA_WM_ERR_EVENTS ( \
		VHA_WM_SCF_ERR_EVENTS | \
		VHA_WM_EVENT_TYPE(WM_WL_WDT     ) | \
		VHA_WM_EVENT_TYPE(WM_WL_IDLE_WDT) | \
		VHA_WM_EVENT_TYPE(WM_SOCIF_WDT  ))
#define VHA_WM_EVENTS_DEFAULT ( \
		VHA_WM_EVENTS | \
		VHA_WM_ERR_EVENTS)

#define VHA_CORE_EVENTS ( \
		VHA_CORE_EVENT_TYPE(CNN_COMPLETE     ))
#ifdef VHA_SCF
#define VHA_CORE_SCF_ERR_EVENTS ( \
		VHA_CORE_EVENT_TYPE(RAM_CORRECTION  ) | \
		VHA_CORE_EVENT_TYPE(RAM_DETECTION   ) | \
		VHA_CORE_EVENT_TYPE(LOGIC_ERROR     ))
#else
#define VHA_CORE_SCF_ERR_EVENTS (0)
#endif
#define VHA_CORE_ERR_EVENTS ( \
		VHA_CORE_SCF_ERR_EVENTS | \
		VHA_CORE_EVENT_TYPE(CNN_ERROR        ) | \
		VHA_CORE_EVENT_TYPE(CORE_SYNC_ERROR ) | \
		VHA_CORE_EVENT_TYPE(CORE_WDT     ) | \
		VHA_CORE_EVENT_TYPE(CORE_MEM_WDT    ))
#define VHA_CORE_EVENTS_DEFAULT ( \
		VHA_CORE_EVENTS | \
		VHA_CORE_ERR_EVENTS)

#define VHA_IC_EVENTS (0)
#ifdef VHA_SCF
#define VHA_IC_SCF_ERR_EVENTS ( \
		VHA_IC_EVENT_TYPE(LOGIC_ERROR     ))
#else
#define VHA_IC_SCF_ERR_EVENTS (0)
#endif
#define VHA_IC_ERR_EVENTS ( \
		VHA_IC_SCF_ERR_EVENTS | \
		VHA_IC_EVENT_TYPE(LOCKSTEP_ERROR) | \
		VHA_IC_EVENT_TYPE(SOCIF_READ_MISMATCH) | \
		VHA_IC_EVENT_TYPE(SOCIF_READ_UNRESPONSIVE))
#define VHA_IC_EVENTS_DEFAULT ( \
		VHA_IC_EVENTS | \
		VHA_IC_ERR_EVENTS)

/* Clock macro definitions */
#define VHA_CLOCKS_MULTI_ALL 0xff
#define VHA_SPREAD_MASK(m) \
		(((m * 0x0101010101010101ULL & 0x8040201008040201ULL) * \
				0x0102040810204081ULL >> 49) & 0x5555)
#define VHA_SET_CLOCKS(mask, mode) \
		(VHA_SPREAD_MASK(mask) * VHA_CR_SYS_CLK_CTRL0_MODE_##mode)

/* As REGBANK is a NOTOFF register, define its OFF to be AUTO by default. */
#define VHA_CR_SYS_CLK_CTRL0_REGBANK_OFF \
		VHA_CR_SYS_CLK_CTRL0_REGBANK_AUTO

#define VHA_SYS_CLOCK_MODE(name, mode) \
		VHA_CR_SYS_CLK_CTRL0_##name##_##mode

#define VHA_SYS_CLOCK_MODE_MULTI(name, mode, mask) \
		(VHA_SET_CLOCKS(mask, mode) << VHA_CR_SYS_CLK_CTRL0_##name##0_SHIFT)

#define VHA_SYS_CLOCK_MODE_MULTI_ALL(name, mode) \
		(VHA_SET_CLOCKS(VHA_CLOCKS_MULTI_ALL, mode) << \
				VHA_CR_SYS_CLK_CTRL0_##name##0_SHIFT)

#define VHA_SYS_CLOCKS_DEFAULT(mode) ( (\
			VHA_SYS_CLOCK_MODE(REGBANK,        mode) | \
			VHA_SYS_CLOCK_MODE(SOCM,           mode) | \
			VHA_SYS_CLOCK_MODE(LSYNC,          mode) | \
			VHA_SYS_CLOCK_MODE(SLC,            mode) | \
			VHA_SYS_CLOCK_MODE(AXI,            mode) | \
			VHA_SYS_CLOCK_MODE(INTERCONNECT,   mode) | \
			VHA_SYS_CLOCK_MODE_MULTI_ALL(WM,   mode) | \
			VHA_SYS_CLOCK_MODE_MULTI_ALL(NOC,  mode) | \
			VHA_SYS_CLOCK_MODE_MULTI_ALL(CORE, mode)   \
			) & VHA_CR_SYS_CLK_CTRL0_MASKFULL)

#define VHA_SYS_CLOCKS_RESET(mode) ( (\
			VHA_SYS_CLOCK_MODE(REGBANK,        mode) | \
			VHA_SYS_CLOCK_MODE(SOCM,           mode) | \
			VHA_SYS_CLOCK_MODE(LSYNC,          mode) | \
			VHA_SYS_CLOCK_MODE(SLC,            mode) | \
			VHA_SYS_CLOCK_MODE(AXI,            mode) | \
			VHA_SYS_CLOCK_MODE(INTERCONNECT,   mode) | \
			VHA_SYS_CLOCK_MODE_MULTI_ALL(WM,   mode) | \
			VHA_SYS_CLOCK_MODE_MULTI_ALL(NOC,  mode)   \
			) & VHA_CR_SYS_CLK_CTRL0_MASKFULL)

#define VHA_SYS_CLOCKS_CORE_FULL_MASK ( \
			~(VHA_CR_SYS_CLK_CTRL0_CORE7_CLRMSK) | \
			~(VHA_CR_SYS_CLK_CTRL0_CORE6_CLRMSK) | \
			~(VHA_CR_SYS_CLK_CTRL0_CORE5_CLRMSK) | \
			~(VHA_CR_SYS_CLK_CTRL0_CORE4_CLRMSK) | \
			~(VHA_CR_SYS_CLK_CTRL0_CORE3_CLRMSK) | \
			~(VHA_CR_SYS_CLK_CTRL0_CORE2_CLRMSK) | \
			~(VHA_CR_SYS_CLK_CTRL0_CORE1_CLRMSK) | \
			~(VHA_CR_SYS_CLK_CTRL0_CORE0_CLRMSK))

#define VHA_MAIN_CLOCK_MODE(name, mode) \
		VHA_CR_CLK_CTRL0_##name##_##mode \

#define VHA_MAIN_CLOCKS_DEFAULT(mode) ( (\
			VHA_MAIN_CLOCK_MODE(CNN_CORE_XBAR, mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_MMM,       mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_EWO,       mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_PACK,      mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_OIN,       mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_POOL,      mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_SB,        mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_XBAR,      mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_NORM,      mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_ACT,       mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_ACCUM,     mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CNV,       mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CBUF,      mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_IBUF,      mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_CMD,       mode) | \
			VHA_MAIN_CLOCK_MODE(CNN,           mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_TRS_A,     mode) | \
			VHA_MAIN_CLOCK_MODE(CNN_TRS_B,     mode) | \
			VHA_MAIN_CLOCK_MODE(MEMBUS_RESET,  mode) | \
			VHA_MAIN_CLOCK_MODE(BWM,           mode) | \
			VHA_MAIN_CLOCK_MODE(LOCM,          mode) | \
			VHA_MAIN_CLOCK_MODE(NOC,           mode) | \
			VHA_MAIN_CLOCK_MODE(ARB,           mode) | \
			VHA_MAIN_CLOCK_MODE(BIF,           mode) \
			) & VHA_CR_CLK_CTRL0_MASKFULL)

/* Response status macro definitions. */
#define VHA_WM_RESPONSE_STATUS(name) \
		VHA_CR_BITMASK(WM_RESPONSE_FIFO_WL_STATUS, name)

#define VHA_WM_RESPONSE_SUCCESS ( \
		VHA_WM_RESPONSE_STATUS(SUCCESS))

#define VHA_WM_RESPONSE_ERROR_CODE(name) \
		(VHA_CR_WM_RESPONSE_FIFO_WL_STATUS_WL_ERROR_CODE_##name)

#define VHA_WM_RESPONSE_GET_ERROR_CODE(s) \
		VHA_CR_GETBITS(WM_RESPONSE_FIFO_WL_STATUS, ERROR_CODE, s)

#define VHA_WM_RESPONSE_GET_FAILED_CORE_IDX(s) \
		VHA_CR_GETBITS(WM_RESPONSE_FIFO_WL_STATUS, FAILED_CORE_IDX, s)

/* Core status macro definitions. */
#define VHA_CORE_STATUS(name) \
		VHA_CR_BITMASK(CORE_EVENT_HOST_STATUS, name)

/* IC status macro definitions. */
#define VHA_IC_STATUS(name) \
		VHA_CR_BITMASK(INTERCONNECT_EVENT_HOST_STATUS, name)

/* Confirmation writes error indication. */
/* There is a fake error bit set in the VHA_CR_WM_RESPONSE_FIFO_WL_STATUS reg
 * to indicate confirmation writes error detected with software. */
#define VHA_REG_CONF_ERROR_SHIFT   (63)
#define VHA_REG_CONF_ERROR_CLRMSK  (0x7fffffffffffffffULL)
#define VHA_REG_CONF_ERROR_EN      (0x8000000000000000ULL)
#define VHA_REG_SET_CONF_ERROR(r) \
	(r |= VHA_REG_CONF_ERROR_EN)
#define VHA_REG_CLR_CONF_ERROR(r) \
	(r &= VHA_REG_CONF_ERROR_CLRMSK)
#define VHA_REG_GET_CONF_ERROR(r) \
	((r & VHA_REG_CONF_ERROR_EN) >> VHA_REG_CONF_ERROR_SHIFT)

/* Parity error indication. */
/* As not all regs with PARITY bit have LOGIC_ERROR bit to identify parity
 * error, there's a fake parity bit set in these regs to indicate parity
 * errors detected with software. */
#define VHA_REG_PARITY_ERROR_SHIFT   (62)
#define VHA_REG_PARITY_ERROR_CLRMSK  (0xBfffffffffffffffULL)
#define VHA_REG_PARITY_ERROR_EN      (0x4000000000000000ULL)
#define VHA_REG_SET_PARITY_ERROR(r) \
	(r |= VHA_REG_PARITY_ERROR_EN)
#define VHA_REG_CLR_PARITY_ERROR(r) \
	(r &= VHA_REG_PARITY_ERROR_CLRMSK)
#define VHA_REG_GET_PARITY_ERROR(r) \
	((r & VHA_REG_PARITY_ERROR_EN) >> VHA_REG_PARITY_ERROR_SHIFT)

/* Workload id mismatch indication. */
/* There is a fake error bit set in the VHA_CR_WM_RESPONSE_FIFO_WL_STATUS reg
 * to indicate workload id mismatch error detected with software. */
#define VHA_REG_WL_ID_MISMATCH_ERROR_SHIFT   (61)
#define VHA_REG_WL_ID_MISMATCH_ERROR_CLRMSK  (0xDfffffffffffffffULL)
#define VHA_REG_WL_ID_MISMATCH_ERROR_EN      (0x2000000000000000ULL)
#define VHA_REG_SET_WL_ID_MISMATCH_ERROR(r) \
	(r |= VHA_REG_WL_ID_MISMATCH_ERROR_EN)
#define VHA_REG_CLR_WL_ID_MISMATCH_ERROR(r) \
	(r &= VHA_REG_WL_ID_MISMATCH_ERROR_CLRMSK)
#define VHA_REG_GET_WL_ID_MISMATCH_ERROR(r) \
	((r & VHA_REG_WL_ID_MISMATCH_ERROR_EN) >> VHA_REG_WL_ID_MISMATCH_ERROR_SHIFT)

/* CRC mismatch indication. */
/* There is a fake error bit set in the VHA_CR_WM_RESPONSE_FIFO_WL_STATUS reg
 * to indicate workload id mismatch error detected with software. */
#define VHA_REG_COMBINED_CRC_ERROR_SHIFT   (60)
#define VHA_REG_COMBINED_CRC_ERROR_CLRMSK  (0xEFFFFFFFFFFFFFFFULL)
#define VHA_REG_COMBINED_CRC_ERROR_EN      (0x1000000000000000ULL)
#define VHA_REG_SET_COMBINED_CRC_ERROR(r) \
	(r |= VHA_REG_COMBINED_CRC_ERROR_EN)
#define VHA_REG_CLR_COMBINED_CRC_ERROR(r) \
	(r &= VHA_REG_COMBINED_CRC_ERROR_CLRMSK)
#define VHA_REG_GET_COMBINED_CRC_ERROR(r) \
	((r & VHA_REG_COMBINED_CRC_ERROR_EN) >> VHA_REG_COMBINED_CRC_ERROR_SHIFT)

/* General core error indication. */
/* There is a fake error bit set in the VHA_CR_WM_EVENT_STATUS reg
 * to indicate that core error was detected for one of the assigned cores. */
#define VHA_REG_WM_CORE_ERROR_SHIFT   (24)
#define VHA_REG_WM_CORE_ERROR_CLRMSK  (0xfffffffffeffffffULL)
#define VHA_REG_WM_CORE_ERROR_EN      (0x0000000001000000ULL)
#define VHA_REG_SET_WM_CORE_ERROR(r) \
	(r |= VHA_REG_WM_CORE_ERROR_EN)
#define VHA_REG_CLR_WM_CORE_ERROR(r) \
	(r &= VHA_REG_WM_CORE_ERROR_CLRMSK)
#define VHA_REG_GET_WM_CORE_ERROR(r) \
	((r & VHA_REG_WM_CORE_ERROR_EN) >> VHA_REG_WM_CORE_ERROR_SHIFT)

/* General interconnect error indication. */
/* There is a fake error bit set in the VHA_CR_WM_EVENT_STATUS reg
 * to indicate that interconnect error was detected for one of the assigned ones. */
#define VHA_REG_WM_IC_ERROR_SHIFT   (25)
#define VHA_REG_WM_IC_ERROR_CLRMSK  (0xfffffffffdffffffULL)
#define VHA_REG_WM_IC_ERROR_EN      (0x0000000002000000ULL)
#define VHA_REG_SET_WM_IC_ERROR(r) \
	(r |= VHA_REG_WM_IC_ERROR_EN)
#define VHA_REG_CLR_WM_IC_ERROR(r) \
	(r &= VHA_REG_WM_IC_ERROR_CLRMSK)
#define VHA_REG_GET_WM_IC_ERROR(r) \
	((r & VHA_REG_WM_IC_ERROR_EN) >> VHA_REG_WM_IC_ERROR_SHIFT)


#endif /* VHA_REGS_H */

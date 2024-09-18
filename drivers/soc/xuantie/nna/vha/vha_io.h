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

#ifndef VHA_IO_H
#define VHA_IO_H

#ifndef OSID
#define _OSID_ 0
#else
#define _OSID_ OSID
#endif

#define _CONCAT(x, y, z) x ## y ## z
#define OSID_TOKEN(group, osid, reg) _CONCAT(group, osid, reg)

#define VHA_CR_OS(reg_name) \
	OSID_TOKEN(VHA_CR_OS, _OSID_, _ ## reg_name)

/* ignore bottom 4 bits of CONFIG_ID: they identify different build variants */
#define VHA_CR_CORE_ID_BVNC_CLRMSK (0xfffffffffffffff0ULL)
/* value missing from vha_cr.h: value obtained from email from SS */
#define VHA_CR_CNN_DEBUG_STATUS_CNN_DEBUG_OFFSET_ALIGNSHIFT 32

#define IMG_UINT64_C(v) v##ULL

#define VHA_DEAD_HW (0xdead1000dead1ULL)

#define ADDR_CAST __force void *

/* IO access macros */
#define IOREAD64(b, o) vha_plat_read64((ADDR_CAST)b + (o))
#define IOWRITE64(b, o, v) vha_plat_write64((ADDR_CAST)b + (o), v)

#define IOREAD64_REGIO(r)     vha_plat_read64((ADDR_CAST)vha->reg_base + (r))
#define IOREAD64_CR_REGIO(r)     vha_plat_read64((ADDR_CAST)vha->reg_base + (VHA_CR_##r))
#define IOWRITE64_REGIO(v, r) vha_plat_write64((ADDR_CAST)vha->reg_base + (r), v)
#define IOWRITE64_CR_REGIO(v, r) vha_plat_write64((ADDR_CAST)vha->reg_base + (VHA_CR_##r), v)

/* write value to register and log into pdump file using specified reg namespace */
#define IOWRITE64_PDUMP_REGIO(v, o, r, s) do {				\
		uint64_t _val_ = v;					\
		vha_plat_write64((ADDR_CAST)vha->reg_base + o + r, _val_);	\
		img_pdump_printf("WRW64 :%s:%#x %#llx\n",	\
				 s, (r), _val_);		\
	} while (0)

/* write value to register and log into pdump file using default regspace */
#define IOWRITE64_PDUMP(v, r) 	\
		IOWRITE64_PDUMP_REGIO(v, 0, r, "REG")
#define IOWRITE64_CR_PDUMP(v, r) 	\
		IOWRITE64_PDUMP_REGIO(v, 0, VHA_CR_##r, "REG")

/* read value from register and log into pdump file using specified reg namespace */
#define IOREAD64_PDUMP_REGIO(o, r, s) ({	\
		uint64_t _val_;		\
		do {		\
			_val_ = vha_plat_read64((ADDR_CAST)vha->reg_base + o + r);	\
				img_pdump_printf("RDW64 :%s:%#x\n",	s, (r));	\
		} while (0);	\
		_val_;	\
	})

/* read value from register and log into pdump file using default regspace */
#define IOREAD64_PDUMP(r) 	\
		IOREAD64_PDUMP_REGIO(0, r, "REG")
#define IOREAD64_CR_PDUMP(r) 	\
		IOREAD64_PDUMP_REGIO(0, VHA_CR_##r, "REG")

#ifdef CONFIG_FUNCTION_ERROR_INJECTION
int __IOPOLL64_RET(int ret);
#else
#define __IOPOLL64_RET(ret) ret
#endif

/* poll c-times for the exact register value(v) masked with m,
 * using d-cycles delay between polls and log into pdump file
 * using specified reg namespace */
#ifdef CONFIG_VHA_DUMMY
#define IOPOLL64_PDUMP_REGIO(v, c, d, m, o, r, s) \
	({   img_pdump_printf("POL64 :%s:%#x %#llx %#llx 0 %d %d\n", \
					s, (r), v & m, m, c, d); \
			__IOPOLL64_RET(0); \
	})
/* Question: shall we generate pdump script to calculate parity in runtime? */
#define IOPOLL64_PDUMP_REGIO_PARITY(v, c, d, m, o, r, s) \
	IOPOLL64_PDUMP_REGIO(v, c, d, m, o, r, s)

#else /* CONFIG_VHA_DUMMY */

#define IOPOLL64_PDUMP_REGIO(v, c, d, m, o, r, s) \
	({ int _ret_ = -EIO;						\
		do {													\
			uint64_t _req_ = v & m;			\
			uint64_t _val_ = ~_req_;		\
			int _count_ = (c < 1 ? 1 : c);	\
			while (--_count_ >= 0) {		\
				_val_ = vha_plat_read64(	\
						(ADDR_CAST)vha->reg_base + ((o + r))) & m;	\
				if (_val_ == _req_) {			\
					_ret_ = 0;							\
					break;									\
				}													\
				if (vha->freq_khz > 0) {	\
					ndelay(d*1000000/vha->freq_khz); \
				} else										\
					udelay(100);						\
			}							\
			WARN_ON(_val_ != _req_ && !vha->hw_props.dummy_dev);	\
				img_pdump_printf("POL64 :%s:%#x %#llx %#llx 0 %d %d\n", \
					s, (r), _req_, m, c, d);			\
		} while (0); \
		__IOPOLL64_RET(_ret_); \
	})

#ifdef VHA_SCF

#define _PARITY_CHECKS 4

#define _PARITY_CHECK_VAL_IMP(val) \
	if((_parity_ok = !img_mem_calc_parity(val))) { \
		_parity_count=_PARITY_CHECKS; \
	} else { \
		if(--_parity_count==0) { \
			_ret_ = -EIO; \
			break; \
		} \
	}

#ifdef VHA_EVENT_INJECT
#define _PARITY_CHECK_VAL(val, reg) \
	if((vha->injection.parity_poll_err_reg == reg) && __EVENT_INJECT()) { \
		_parity_ok = false;	\
	} else { \
		_PARITY_CHECK_VAL_IMP(val) \
		WARN_ON(!_parity_ok); \
	}
#else
#define _PARITY_CHECK_VAL(val, reg) _PARITY_CHECK_VAL_IMP(val)
#endif
/* poll c-times for the exact register value(v) masked with m,
 * using d-cycles delay between polls and log into pdump file
 * using specified reg namespace
 * return error if parity calculated from 4 consecutive reads
 * is wrong
 * */
#define IOPOLL64_PDUMP_REGIO_PARITY(v, c, d, m, o, r, s) \
	({ int _ret_ = -EIO;              \
		if (vha->hw_props.supported.parity && !vha->parity_disable) { \
			do {                          \
				bool _parity_ok = false;    \
				int _parity_count=_PARITY_CHECKS; \
				uint64_t _req_ = (v) & (m); \
				uint64_t _val_ = ~_req_;    \
				int _count_ = (c) < _PARITY_CHECKS ? _PARITY_CHECKS : (c); \
				while (_count_-- > 0) {    \
					_val_ = vha_plat_read64(  \
							(ADDR_CAST)vha->reg_base + ((o + r))); \
					_PARITY_CHECK_VAL(_val_, r); \
					_val_ &= m;               \
					if (_parity_ok && _val_ == _req_) { \
						_ret_ = 0;              \
						break;                  \
					}                         \
					if (vha->freq_khz > 0) {  \
						ndelay(d*1000000/vha->freq_khz); \
					} else                    \
						udelay(100);            \
				}                           \
				WARN_ON(_val_ != _req_);    \
				img_pdump_printf("POL64 :%s:%#x %#llx %#llx 0 %d %d\n", \
						s, (r), _req_, m, c, d);      \
			} while (0); \
		} else { \
			_ret_ = IOPOLL64_PDUMP_REGIO(v, c, d, m, o, r, s); \
		} \
		__IOPOLL64_RET(_ret_); \
	})

#else /* VHA_SCF */
#define IOPOLL64_PDUMP_REGIO_PARITY(v, c, d, m, o, r, s) \
	IOPOLL64_PDUMP_REGIO(v, c, d, m, o, r, s)
#endif /* VHA_SCF */
#endif /* CONFIG_VHA_DUMMY */

/* poll c-times for the exact register value(v) masked with m,
 * using d-cycles delay between polls and log into pdump file
 * using specified reg namespace */
#define IOPOLL64_PDUMP(v, c, d, m, r)   \
	IOPOLL64_PDUMP_REGIO(v, c, d, m, 0, r, "REG")
#define IOPOLL64_CR_PDUMP(v, c, d, m, r)  \
	IOPOLL64_PDUMP_REGIO(v, c, d, m, 0, VHA_CR_##r, "REG")

#define IOPOLL64_PDUMP_PARITY(v, c, d, m, r)   \
	IOPOLL64_PDUMP_REGIO_PARITY(v, c, d, m, 0, r, "REG")
#define IOPOLL64_CR_PDUMP_PARITY(v, c, d, m, r)  \
	IOPOLL64_PDUMP_REGIO_PARITY(v, c, d, m, 0, VHA_CR_##r, "REG")

/* write phys address of buffer to register, and log into pdump */
#define IOWRITE_PDUMP_PHYS(buf, offset, reg) do {			\
		uint64_t __maybe_unused _addr_ = vha_buf_addr(session, buf);	\
		vha_plat_write64(	\
				(ADDR_CAST)session->vha->reg_base + reg,\
				_addr_ + offset);	\
		img_pdump_printf(					\
			"WRW64 :REG:%#x "_PMEM_":BLOCK_%d:%#x -- '%s%s'\n",	\
			reg, buf->id, offset, buf->name,		\
				buf->pcache.valid ? "_cached" : "");	\
	} while (0)

/* write virt address of buffer to register, and log into pdump */
#define IOWRITE_PDUMP_VIRT(buf, offset, reg) \
		IOWRITE64_PDUMP(buf->devvirt + offset, reg)

/* write address of buffer to register and log into pdump file */
#define IOWRITE_PDUMP_BUFADDR(session, buf, offset, reg) do {		\
		if (session->vha->mmu_mode)				\
			IOWRITE_PDUMP_VIRT(buf, offset, reg);		\
		else if (buf->attr & IMG_MEM_ATTR_OCM) {		\
				IOWRITE_PDUMP_VIRT(buf, offset, reg);	\
		} else {					\
			IOWRITE_PDUMP_PHYS(buf, offset, reg);	\
		}						\
	} while (0)


/* write phys address of buffer to register, and log into pdump */
#define SET_PHYS(buf, offset, addr) do {			\
		uint64_t _addr_ = vha_buf_addr(session, buf);	\
		*addr = _addr_ + offset;	\
	} while (0)

/* write virt address of buffer to register, and log into pdump */
#define SET_VIRT(buf, offset, addr) do {			\
		*addr = buf->devvirt + offset;					\
	} while (0)

#define SET_BUFADDR(session, buf, offset, addr) do {		\
		if (session->vha->mmu_mode)								\
			SET_VIRT(buf, offset, addr);					\
		else if (buf->attr & IMG_MEM_ATTR_OCM) {				\
			SET_VIRT(buf, offset, addr);				\
		} else {											\
			SET_PHYS(buf, offset, addr);				\
		}													\
	} while (0)


/* extract bitfield from a register value */
static inline
uint64_t _get_bits(uint64_t val, uint32_t shift, uint64_t mask)
{
	return (val & mask) >> shift;
}
/* set bitfield in a register value */
static inline
uint64_t _set_bits(uint64_t val, uint32_t shift, uint64_t mask)
{
	uint64_t v = val << shift;

	return v & mask;
}

/* utility macros for manipulating fields within registers */
/* apply bitmask */
#define VHA_CR_BITMASK(reg, field)			\
	(~VHA_CR_##reg##_##field##_CLRMSK)
/* get field from register */
#define VHA_CR_GETBITS(reg, field, val)			\
	_get_bits(val,					\
		 VHA_CR_##reg##_##field##_SHIFT,	\
		 VHA_CR_BITMASK(reg, field))
/* get value of a field in a register, taking alignment into account */
#define VHA_CR_ALIGN_GETBITS(reg, field, val)		\
	(VHA_CR_GETBITS(reg, field, val)		\
	 << VHA_CR_##reg##_##field##_ALIGNSHIFT)

/* apply bitmask - OS dependent */
#define VHA_CR_BITMASK_OS(reg, field)			\
	~OSID_TOKEN(VHA_CR_OS, _OSID_, _ ## reg ## _ ## field ## _CLRMSK)
/* get field from register - OS dependent */
#define VHA_CR_GETBITS_OS(reg, field, val)			\
	_get_bits(val,					\
		 OSID_TOKEN(VHA_CR_OS, _OSID_, _ ## reg ## _ ## field ## _SHIFT), \
		 VHA_CR_BITMASK_OS(reg, field))
/* get value of a field in a register, taking alignment into account - OS */
#define VHA_CR_ALIGN_GETBITS_OS(reg, field, val)		\
	(VHA_CR_GETBITS_OS(reg, field, val)		\
	 << OSID_TOKEN(VHA_CR_OS, _OSID_, _ ## reg ## _ ## field ## _ALIGNSHIFT)) \

/* max value of a field */
#define VHA_CR_MAX(reg, field)				\
	VHA_CR_GETBITS(reg, field, ~0ULL)
/* max value of an field taking alignment into account */
#define VHA_CR_ALIGN_MAX(reg, field)			\
	(VHA_CR_MAX(reg, field)				\
	 << VHA_CR_##reg##_##field##_SHIFT)

/* max value of a field - OS dependent */
#define VHA_CR_MAX_OS(reg, field)				\
	VHA_CR_GETBITS_OS(reg, field, ~0ULL)
/* max value of an field taking alignment into account - OS dependent */
#define VHA_CR_ALIGN_MAX_OS(reg, field)			\
	(VHA_CR_MAX_OS(reg, field)				\
	 << OSID_TOKEN(VHA_CR_OS, _OSID_, _ ## reg ## _ ## field ## _SHIFT)) \

/* set value of a field within a register */
#define VHA_CR_SETBITS(reg, field, val)			\
	_set_bits(val,					\
		 VHA_CR_##reg##_##field##_SHIFT,	\
		 VHA_CR_BITMASK(reg, field))
/* set value of a field within a register reducing value by alignment */
#define VHA_CR_ALIGN_SETBITS(reg, field, val)		\
	VHA_CR_SETBITS(					\
		reg, field, (val)			\
	 >> VHA_CR_##reg##_##field##_ALIGNSHIFT)

/* set value of a field within a register - OS dependent */
#define VHA_CR_SETBITS_OS(reg, field, val)			\
	_set_bits(val,					\
		 OSID_TOKEN(VHA_CR_OS, _OSID_, _ ## reg ## _ ## field ## _SHIFT), \
		 VHA_CR_BITMASK_OS(reg, field))
/* set value of a field within a register reducing value by alignment - OS */
#define VHA_CR_ALIGN_SETBITS_OS(reg, field, val)		\
	VHA_CR_SETBITS_OS(			\
		reg, field, (val)		\
	 >> OSID_TOKEN(VHA_CR_OS, _OSID_, _ ## reg ## _ ## field ## _ALIGNSHIFT)) \

/* clear bits of a field within a register */
#define VHA_CR_CLEARBITS(val, reg, field)			\
	(val &= ~VHA_CR_BITMASK(reg, field))
/* clear bits of a field within a register - OS dependent */
#define VHA_CR_CLEARBITS_OS(val, reg, field)			\
	(val &= ~VHA_CR_BITMASK_OS(reg, field))

#endif /* VHA_IO_H */

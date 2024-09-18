/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2021 VERISILICON
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2021 VERISILICON
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/

#ifndef _HANTRODEC_H_
#define _HANTRODEC_H_
#ifdef __FREERTOS__
#include "basetype.h"
#include "dev_common_freertos.h"
#elif defined(__linux__)
#include <linux/ioctl.h>
#include <linux/types.h>
#include "hantrommu.h"
#else //For other os
//TODO...
#endif
#include "hantrovcmd.h"

enum CoreType {
  /* Decoder */
  HW_VC8000D = 0,
  HW_VC8000DJ,
  HW_BIGOCEAN,
  HW_VCMD,
  HW_MMU,  //if set HW_MMU_WR, then HW_MMU means HW_MMU_RD
  HW_MMU_WR,
  HW_DEC400,
  HW_L2CACHE,
  HW_SHAPER,
  /* Encoder*/
  /* Auxiliary IPs */
  HW_NOC,
  HW_AXIFE,
  HW_APBFILTER,
  HW_CORE_MAX     /* max number of cores supported */
};

struct core_desc {
  __u32 id; /* id of the subsystem */
  __u32 type; /* type of core to be written */
  __u32 *regs; /* pointer to user registers */
  __u32 size; /* size of register space */
  __u32 reg_id; /* id of reigster to be read/written */
};

struct regsize_desc {
  __u32 slice; /* id of the slice */
  __u32 id; /* id of the subsystem */
  __u32 type; /* type of core to be written */
  __u32 size; /* iosize of the core */
};

struct core_param {
  __u32 slice; /* id of the slice */
  __u32 id; /* id of the subsystem */
  __u32 type; /* type of core to be written */
  __u32 size; /* iosize of the core */
  __u32 asic_id; /* asic id of the core */
};


struct subsys_desc {
  __u32 subsys_num;   /* total subsystems count */
  __u32 subsys_vcmd_num;  /* subsystems with vcmd */
};

struct axife_cfg {
  __u8 axi_rd_chn_num;
  __u8 axi_wr_chn_num;
  __u8 axi_rd_burst_length;
  __u8 axi_wr_burst_length;
  __u8 fe_mode;
  __u32 id;
};

struct apbfilter_cfg {
  __u32 nbr_mask_regs;
  __u32 mask_reg_offset;
  __u32 page_sel_addr;
  __u8  num_mode;
  __u8  mask_bits_per_reg;
  __u32 id; /* id of the subsystem */
  __u32 type; /* type of core to be written */
  __u32 has_apbfilter;
};
/* Use 'k' as magic number */
#define HANTRODEC_IOC_MAGIC  'k'

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": G and S atomically
 * H means "sHift": T and Q atomically
 */

#define HANTRODEC_PP_INSTANCE       _IO(HANTRODEC_IOC_MAGIC, 1)
#define HANTRODEC_HW_PERFORMANCE    _IO(HANTRODEC_IOC_MAGIC, 2)
#define HANTRODEC_IOCGHWOFFSET      _IOR(HANTRODEC_IOC_MAGIC,  3, unsigned long *)
#define HANTRODEC_IOCGHWIOSIZE      _IOR(HANTRODEC_IOC_MAGIC,  4, struct regsize_desc *)

#define HANTRODEC_IOC_CLI           _IO(HANTRODEC_IOC_MAGIC,  5)
#define HANTRODEC_IOC_STI           _IO(HANTRODEC_IOC_MAGIC,  6)
#define HANTRODEC_IOC_MC_OFFSETS    _IOR(HANTRODEC_IOC_MAGIC, 7, unsigned long *)
#define HANTRODEC_IOC_MC_CORES      _IOR(HANTRODEC_IOC_MAGIC, 8, unsigned int *)


#define HANTRODEC_IOCS_DEC_PUSH_REG  _IOW(HANTRODEC_IOC_MAGIC, 9, struct core_desc *)
#define HANTRODEC_IOCS_PP_PUSH_REG   _IOW(HANTRODEC_IOC_MAGIC, 10, struct core_desc *)

#define HANTRODEC_IOCH_DEC_RESERVE   _IO(HANTRODEC_IOC_MAGIC, 11)
#define HANTRODEC_IOCT_DEC_RELEASE   _IO(HANTRODEC_IOC_MAGIC, 12)
#define HANTRODEC_IOCQ_PP_RESERVE    _IO(HANTRODEC_IOC_MAGIC, 13)
#define HANTRODEC_IOCT_PP_RELEASE    _IO(HANTRODEC_IOC_MAGIC, 14)

#define HANTRODEC_IOCX_DEC_WAIT      _IOWR(HANTRODEC_IOC_MAGIC, 15, struct core_desc *)
#define HANTRODEC_IOCX_PP_WAIT       _IOWR(HANTRODEC_IOC_MAGIC, 16, struct core_desc *)

#define HANTRODEC_IOCS_DEC_PULL_REG  _IOWR(HANTRODEC_IOC_MAGIC, 17, struct core_desc *)
#define HANTRODEC_IOCS_PP_PULL_REG   _IOWR(HANTRODEC_IOC_MAGIC, 18, struct core_desc *)

#define HANTRODEC_IOCG_CORE_WAIT     _IOR(HANTRODEC_IOC_MAGIC, 19, int *)

#define HANTRODEC_IOX_ASIC_ID        _IOWR(HANTRODEC_IOC_MAGIC, 20, struct core_param *)

#define HANTRODEC_IOCG_CORE_ID       _IOR(HANTRODEC_IOC_MAGIC, 21, unsigned long)

#define HANTRODEC_IOCS_DEC_WRITE_REG  _IOW(HANTRODEC_IOC_MAGIC, 22, struct core_desc *)

#define HANTRODEC_IOCS_DEC_READ_REG   _IOWR(HANTRODEC_IOC_MAGIC, 23, struct core_desc *)

#define HANTRODEC_IOX_ASIC_BUILD_ID   _IOWR(HANTRODEC_IOC_MAGIC, 24, __u32 *)

#define HANTRODEC_IOX_SUBSYS _IOWR(HANTRODEC_IOC_MAGIC, 25, struct subsys_desc *)

#define HANTRODEC_IOCX_POLL _IO(HANTRODEC_IOC_MAGIC, 26)

#define HANTRODEC_DEBUG_STATUS       _IO(HANTRODEC_IOC_MAGIC, 29)

#define HANTRODEC_IOCS_DEC_WRITE_APBFILTER_REG  _IOW(HANTRODEC_IOC_MAGIC, 30, struct core_desc *)

#define HANTRODEC_IOC_APBFILTER_CONFIG     _IOR(HANTRODEC_IOC_MAGIC,  31, struct apbfilter_cfg *)

#define HANTRODEC_IOC_AXIFE_CONFIG   _IOR(HANTRODEC_IOC_MAGIC,  32, struct axife_cfg *)

#define HANTRODEC_IOC_MAXNR 32

#endif /* !_HANTRODEC_H_ */

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

#ifndef _VC8000_VCMD_DRIVER_H_
#define _VC8000_VCMD_DRIVER_H_
#ifdef __FREERTOS__
#include "basetype.h"
#include "dev_common_freertos.h"
#elif defined(__linux__)
#include <linux/ioctl.h>    /* needed for the _IOW etc stuff used later */
#endif

#ifdef __FREERTOS__
//addr_t has been defined in basetype.h //Now the FreeRTOS mem need to support 64bit env
#elif defined(__linux__)
#undef  addr_t
#define addr_t ADDR_T_VCMD
typedef size_t addr_t;
#endif

/* Use 'v' as magic number for vcmd */
#define HANTRO_VCMD_IOC_MAGIC  'v'
/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": G and S atomically
 * H means "sHift": T and Q atomically
 */

#define HANTRO_VCMD_IOCH_GET_CMDBUF_PARAMETER           _IOWR(HANTRO_VCMD_IOC_MAGIC, 20,struct cmdbuf_mem_parameter *)
#define HANTRO_VCMD_IOCH_GET_CMDBUF_POOL_SIZE           _IOWR(HANTRO_VCMD_IOC_MAGIC, 21,unsigned long)
#define HANTRO_VCMD_IOCH_SET_CMDBUF_POOL_BASE           _IOWR(HANTRO_VCMD_IOC_MAGIC, 22,unsigned long)

#define HANTRO_VCMD_IOCH_GET_VCMD_PARAMETER             _IOWR(HANTRO_VCMD_IOC_MAGIC, 24, struct config_parameter *)

#define HANTRO_VCMD_IOCH_RESERVE_CMDBUF                 _IOWR(HANTRO_VCMD_IOC_MAGIC, 25,struct exchange_parameter *)
#define HANTRO_VCMD_IOCH_LINK_RUN_CMDBUF                _IOR(HANTRO_VCMD_IOC_MAGIC, 26,u16 *)
#define HANTRO_VCMD_IOCH_WAIT_CMDBUF                    _IOR(HANTRO_VCMD_IOC_MAGIC, 27,u16 *)
#define HANTRO_VCMD_IOCH_RELEASE_CMDBUF                 _IOR(HANTRO_VCMD_IOC_MAGIC, 28,u16 *)

#define HANTRO_VCMD_IOCH_POLLING_CMDBUF                 _IOR(HANTRO_VCMD_IOC_MAGIC, 40,u16 *)

#define HANTRO_VCMD_IOC_MAXNR 50

/*priority support*/

#define MAX_CMDBUF_PRIORITY_TYPE            2       //0:normal priority,1:high priority

#define CMDBUF_PRIORITY_NORMAL            0
#define CMDBUF_PRIORITY_HIGH              1

#if 0
#define OPCODE_WREG               (0x01<<27)
#define OPCODE_END                (0x02<<27)
#define OPCODE_NOP                (0x03<<27)
#define OPCODE_RREG               (0x16<<27)
#define OPCODE_INT                (0x18<<27)
#define OPCODE_JMP                (0x19<<27)
#define OPCODE_STALL              (0x09<<27)
#define OPCODE_CLRINT             (0x1a<<27)
#define OPCODE_JMP_RDY0           (0x19<<27)
#define OPCODE_JMP_RDY1           ((0x19<<27)|(1<<26))
#define JMP_IE_1                  (1<<25)
#define JMP_RDY_1                 (1<<26)

#define CLRINT_OPTYPE_READ_WRITE_1_CLEAR         0
#define CLRINT_OPTYPE_READ_WRITE_0_CLEAR         1
#define CLRINT_OPTYPE_READ_CLEAR                 2

#define VC8000E_FRAME_RDY_INT_MASK                        0x0001
#define VC8000E_CUTREE_RDY_INT_MASK                       0x0002
#define VC8000E_DEC400_INT_MASK                           0x0004
#define VC8000E_L2CACHE_INT_MASK                          0x0008
#define VC8000E_MMU_INT_MASK                              0x0010

#define VC8000D_FRAME_RDY_INT_MASK                        0x0100
#define VC8000D_DEC400_INT_MASK                           0x0400
#define VC8000D_L2CACHE_INT_MASK                          0x0800
#define VC8000D_MMU_INT_MASK                              0x1000
#endif

#define HW_ID_1_0_C                0x43421001
#define HW_ID_1_1_2                0x43421102

#define ANY_CMDBUF_ID 0xFFFF

/*module_type support*/

enum vcmd_module_type{
  VCMD_TYPE_ENCODER = 0,
  VCMD_TYPE_CUTREE,
  VCMD_TYPE_DECODER,
  VCMD_TYPE_JPEG_ENCODER,
  VCMD_TYPE_JPEG_DECODER,
  MAX_VCMD_TYPE
};

struct cmdbuf_mem_parameter
{
  u32 *virt_cmdbuf_addr;
  addr_t phy_cmdbuf_addr;             //cmdbuf pool base physical address
  u32 mmu_phy_cmdbuf_addr;             //cmdbuf pool base mmu mapping address
  u32 cmdbuf_total_size;              //cmdbuf pool total size in bytes.
  u16 cmdbuf_unit_size;               //one cmdbuf size in bytes. all cmdbuf have same size.
  u32 *virt_status_cmdbuf_addr;
  addr_t phy_status_cmdbuf_addr;      //status cmdbuf pool base physical address
  u32 mmu_phy_status_cmdbuf_addr;      //status cmdbuf pool base mmu mapping address
  u32 status_cmdbuf_total_size;       //status cmdbuf pool total size in bytes.
  u16 status_cmdbuf_unit_size;        //one status cmdbuf size in bytes. all status cmdbuf have same size.
  addr_t base_ddr_addr;               //for pcie interface, hw can only access phy_cmdbuf_addr-pcie_base_ddr_addr.
                                      //for other interface, this value should be 0?
};

struct config_parameter
{
  u16 module_type;                    //input vc8000e=0,cutree=1,vc8000d=2,jpege=3, jpegd=4
  u16 vcmd_core_num;                  //output, how many vcmd cores are there with corresponding module_type.
  u16 submodule_main_addr;            //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_dec400_addr;          //output ,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_L2Cache_addr;         //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_MMU_addr;             //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_MMUWrite_addr;        //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_axife_addr;           //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 config_status_cmdbuf_id;        // output , this status comdbuf save the all register values read in driver init.//used for analyse configuration in cwl.
  u32 vcmd_hw_version_id;
};

/*need to consider how many memory should be allocated for status.*/
struct exchange_parameter
{
  u32 executing_time;                 //input ;executing_time=encoded_image_size*(rdoLevel+1)*(rdoq+1);
  u16 module_type;                    //input input vc8000e=0,IM=1,vc8000d=2, jpege=3, jpegd=4
  u16 cmdbuf_size;                    //input, reserve is not used; link and run is input.
  u16 priority;                       //input,normal=0, high/live=1
  u16 cmdbuf_id;                      //output, it is unique in driver.
  u16 core_id;                        //just used for polling.
};

struct vcmd_profile {
    int dev_loading_percent;
    int dev_loading_max_percent;

    int last_hw_proc_us;
    int avg_hw_proc_us;
    int proced_count;
    int cur_submit_vcmd_id;
    int cur_complete_vcmd_id;
    int vcmd_num_share_irq;

    //error statistics
    u32 vcmd_abort_cnt;
    u32 vcmd_buserr_cnt;
    u32 vcmd_timeout_cnt;
    u32 vcmd_cmderr_cnt;
};
extern struct vcmd_profile vdec_vcmd_profile;
#endif /* !_VC8000_VCMD_DRIVER_H_ */

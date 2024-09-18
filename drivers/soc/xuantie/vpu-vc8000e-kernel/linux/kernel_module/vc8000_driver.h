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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __FREERTOS__
/* needed for the _IOW etc stuff used later */
#include "base_type.h"
#include "osal.h"
#include "dev_common_freertos.h"
#elif defined(__linux__)
#include <linux/ioctl.h>    /* needed for the _IOW etc stuff used later */
#else //For other os
//TODO...
#endif
#ifdef HANTROMMU_SUPPORT
#include "hantrommu.h"
#endif

#ifdef HANTROAXIFE_SUPPORT
#include "vc8000_axife.h"
#endif

#ifdef __FREERTOS__
//ptr_t has been defined in base_type.h //Now the FreeRTOS mem need to support 64bit env
#elif defined(__linux__)
#undef  ptr_t
#define ptr_t  PTR_T_KERNEL
typedef size_t ptr_t;
#endif
/*
 * Macros to help debugging
 */

#undef PDEBUG   /* undef it, just in case */
#ifdef HANTRO_DRIVER_DEBUG
#  ifdef __KERNEL__
    /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_INFO "vc8000: " fmt, ## args)
#  else
    /* This one for user space */
#    define PDEBUG(fmt, args...) printf(__FILE__ ":%d: " fmt, __LINE__ , ## args)
#  endif
#else
#  define PDEBUG(fmt, args...)  pr_debug("vc8000: " fmt, ## args)
#endif

#define ENC_HW_ID1                  0x48320100
#define ENC_HW_ID2                  0x80006000
#define CORE_INFO_MODE_OFFSET       31
#define CORE_INFO_AMOUNT_OFFSET     28

/* Use 'k' as magic number */
#define HANTRO_IOC_MAGIC  'k'

/*
 * S means "Set" through a ptr,
 * T means "Tell" directly with the argument value
 * G means "Get": reply by setting through a pointer
 * Q means "Query": response is on the return value
 * X means "eXchange": G and S atomically
 * H means "sHift": T and Q atomically
 */

#define HANTRO_IOCG_HWOFFSET                                _IOR(HANTRO_IOC_MAGIC,  3, unsigned long *)
#define HANTRO_IOCG_HWIOSIZE                                _IOR(HANTRO_IOC_MAGIC,  4, unsigned int *)
#define HANTRO_IOC_CLI                                      _IO(HANTRO_IOC_MAGIC,  5)
#define HANTRO_IOC_STI                                      _IO(HANTRO_IOC_MAGIC,  6)
#define HANTRO_IOCX_VIRT2BUS                                _IOWR(HANTRO_IOC_MAGIC,  7, unsigned long *)
#define HANTRO_IOCH_ARDRESET                                _IO(HANTRO_IOC_MAGIC, 8)   /* debugging tool */
#define HANTRO_IOCG_SRAMOFFSET                              _IOR(HANTRO_IOC_MAGIC,  9, unsigned long *)
#define HANTRO_IOCG_SRAMEIOSIZE                             _IOR(HANTRO_IOC_MAGIC,  10, unsigned int *)
#define HANTRO_IOCH_ENC_RESERVE                             _IOR(HANTRO_IOC_MAGIC, 11,unsigned int *)
#define HANTRO_IOCH_ENC_RELEASE                             _IOR(HANTRO_IOC_MAGIC, 12,unsigned int *)
#define HANTRO_IOCG_CORE_NUM                                _IOR(HANTRO_IOC_MAGIC, 13,unsigned int *)
#define HANTRO_IOCG_CORE_INFO                               _IOR(HANTRO_IOC_MAGIC, 14,SUBSYS_CORE_INFO *)
#define HANTRO_IOCG_CORE_WAIT                               _IOR(HANTRO_IOC_MAGIC, 15, unsigned int *)
#define HANTRO_IOCG_ANYCORE_WAIT                            _IOR(HANTRO_IOC_MAGIC, 16, CORE_WAIT_OUT *)

#define HANTRO_IOCH_GET_CMDBUF_PARAMETER                    _IOWR(HANTRO_IOC_MAGIC, 25,struct cmdbuf_mem_parameter *)
#define HANTRO_IOCH_GET_CMDBUF_POOL_SIZE                    _IOWR(HANTRO_IOC_MAGIC, 26,unsigned long)
#define HANTRO_IOCH_SET_CMDBUF_POOL_BASE                    _IOWR(HANTRO_IOC_MAGIC, 27,unsigned long)
#define HANTRO_IOCH_GET_VCMD_PARAMETER                      _IOWR(HANTRO_IOC_MAGIC, 28, struct config_parameter *)
#define HANTRO_IOCH_RESERVE_CMDBUF                          _IOWR(HANTRO_IOC_MAGIC, 29,struct exchange_parameter *)
#define HANTRO_IOCH_LINK_RUN_CMDBUF                         _IOR(HANTRO_IOC_MAGIC, 30,u16 *)
#define HANTRO_IOCH_WAIT_CMDBUF                             _IOR(HANTRO_IOC_MAGIC, 31,u16 *)
#define HANTRO_IOCH_RELEASE_CMDBUF                          _IOR(HANTRO_IOC_MAGIC, 32,u16 *)
#define HANTRO_IOCH_POLLING_CMDBUF                          _IOR(HANTRO_IOC_MAGIC, 33,u16 *)

#define HANTRO_IOCH_GET_VCMD_ENABLE                         _IOWR(HANTRO_IOC_MAGIC, 50,unsigned long)

#define GET_ENCODER_IDX(type_info)  (CORE_VC8000E);
#define CORETYPE(core)     (1 << core)
#define HANTRO_IOC_MAXNR 60

/*priority support*/

#define MAX_CMDBUF_PRIORITY_TYPE            2       //0:normal priority,1:high priority

#define CMDBUF_PRIORITY_NORMAL            0
#define CMDBUF_PRIORITY_HIGH              1

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
#define CUTREE_MMU_INT_MASK                               0x0020


#define VC8000D_FRAME_RDY_INT_MASK                        0x0100
#define VC8000D_DEC400_INT_MASK                           0x0400
#define VC8000D_L2CACHE_INT_MASK                          0x0800
#define VC8000D_MMU_INT_MASK                              0x1000


#define VC8000D_DEC400_INT_MASK_1_1_1                      0x0200
#define VC8000D_L2CACHE_INT_MASK_1_1_1                     0x0400
#define VC8000D_MMU_INT_MASK_1_1_1                         0x0800




#define HW_ID_1_0_C                0x43421001
#define HW_ID_1_1_2                0x43421102


#define ASIC_STATUS_SEGMENT_READY       0x1000
#define ASIC_STATUS_FUSE_ERROR          0x200
#define ASIC_STATUS_SLICE_READY         0x100
#define ASIC_STATUS_LINE_BUFFER_DONE    0x080  /* low latency */
#define ASIC_STATUS_HW_TIMEOUT          0x040
#define ASIC_STATUS_BUFF_FULL           0x020
#define ASIC_STATUS_HW_RESET            0x010
#define ASIC_STATUS_ERROR               0x008
#define ASIC_STATUS_FRAME_READY         0x004
#define ASIC_IRQ_LINE                   0x001
#define ASIC_STATUS_ALL       (ASIC_STATUS_SEGMENT_READY |\
                               ASIC_STATUS_FUSE_ERROR |\
                               ASIC_STATUS_SLICE_READY |\
                               ASIC_STATUS_LINE_BUFFER_DONE |\
                               ASIC_STATUS_HW_TIMEOUT |\
                               ASIC_STATUS_BUFF_FULL |\
                               ASIC_STATUS_HW_RESET |\
                               ASIC_STATUS_ERROR |\
                               ASIC_STATUS_FRAME_READY)

enum
{
  CORE_VC8000E = 0,
  CORE_VC8000EJ = 1,
  CORE_CUTREE = 2,
  CORE_DEC400 = 3,
  CORE_MMU = 4,
  CORE_L2CACHE = 5,
  CORE_AXIFE = 6,
  CORE_APBFT = 7,
  CORE_MMU_1 = 8,
  CORE_AXIFE_1 = 9,
  CORE_MAX
};

//#define CORE_MAX  (CORE_MMU)

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
  ptr_t phy_cmdbuf_addr;             //cmdbuf pool base physical address
  u32 mmu_phy_cmdbuf_addr;             //cmdbuf pool base mmu mapping address
  u32 cmdbuf_total_size;              //cmdbuf pool total size in bytes.
  u16 cmdbuf_unit_size;               //one cmdbuf size in bytes. all cmdbuf have same size.
  u32 *virt_status_cmdbuf_addr;
  ptr_t phy_status_cmdbuf_addr;      //status cmdbuf pool base physical address
  u32 mmu_phy_status_cmdbuf_addr;      //status cmdbuf pool base mmu mapping address
  u32 status_cmdbuf_total_size;       //status cmdbuf pool total size in bytes.
  u16 status_cmdbuf_unit_size;        //one status cmdbuf size in bytes. all status cmdbuf have same size.
  ptr_t base_ddr_addr;               //for pcie interface, hw can only access phy_cmdbuf_addr-pcie_base_ddr_addr.
                                      //for other interface, this value should be 0?
};

struct config_parameter
{
  u16 module_type;                    //input vc8000e=0,cutree=1,vc8000d=2，jpege=3, jpegd=4
  u16 vcmd_core_num;                  //output, how many vcmd cores are there with corresponding module_type.
  u16 submodule_main_addr;            //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_dec400_addr;          //output ,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_L2Cache_addr;         //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_MMU_addr[2];          //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 submodule_axife_addr[2];        //output,if submodule addr == 0xffff, this submodule does not exist.
  u16 config_status_cmdbuf_id;        // output , this status comdbuf save the all register values read in driver init.//used for analyse configuration in cwl.
  u32 vcmd_hw_version_id;
};

/*need to consider how many memory should be allocated for status.*/
struct exchange_parameter
{
  u32 executing_time;                 //input ;executing_time=encoded_image_size*(rdoLevel+1)*(rdoq+1);
  u16 module_type;                    //input input vc8000e=0,IM=1,vc8000d=2，jpege=3, jpegd=4
  u16 cmdbuf_size;                    //input, reserve is not used; link and run is input.
  u16 priority;                       //input,normal=0, high/live=1
  u16 cmdbuf_id;                      //output ,it is unique in driver. 
  u16 core_id;                        //just used for polling.
};

typedef struct CoreWaitOut
{
  u32 job_id[4];
  u32 irq_status[4];
  u32 irq_num;
} CORE_WAIT_OUT;

typedef struct
{
  u32 subsys_idx;
  u32 core_type;
  unsigned long offset;
  u32 reg_size;
  int irq;
}CORE_CONFIG;

typedef struct
{
  unsigned long base_addr;
  u32 iosize;
  u32 resouce_shared; //indicate the core share resources with other cores or not.If 1, means cores can not work at the same time.
}SUBSYS_CONFIG;

typedef struct
{
  u32 type_info; //indicate which IP is contained in this subsystem and each uses one bit of this variable
  unsigned long offset[CORE_MAX];
  unsigned long regSize[CORE_MAX];
  int irq[CORE_MAX];
}SUBSYS_CORE_INFO;

typedef struct
{
  SUBSYS_CONFIG cfg;
  SUBSYS_CORE_INFO core_info;
}SUBSYS_DATA;

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
extern struct vcmd_profile venc_vcmd_profile;
#ifdef __cplusplus
}
#endif

#endif /* !_VC8000_VCMD_DRIVER_H_ */

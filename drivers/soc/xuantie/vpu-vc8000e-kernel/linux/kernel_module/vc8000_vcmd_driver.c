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

#include <linux/kernel.h>
#include <linux/module.h>
/* needed for __init,__exit directives */
#include <linux/init.h>
/* needed for remap_page_range
    SetPageReserved
    ClearPageReserved
*/
#include <linux/mm.h>
/* obviously, for kmalloc */
#include <linux/slab.h>
/* for struct file_operations, register_chrdev() */
#include <linux/fs.h>
/* standard error codes */
#include <linux/errno.h>

#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>


#include <linux/moduleparam.h>
/* request_irq(), free_irq() */
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <linux/semaphore.h>
#include <linux/spinlock.h>
/* needed for virt_to_phys() */
#include <asm/io.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/ioport.h>

#include <asm/irq.h>

#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>

//vpu devfreq
#include <linux/devfreq.h>
#include <linux/of_device.h>
#include <linux/pm_opp.h>

#include "vc8000_devfreq.h"

/* our own stuff */
#include <linux/platform_device.h>
#include <linux/of.h>
#include "vcmdswhwregisters.h"
#include "bidirect_list.h"
#include "vc8000_driver.h"
#undef linux
#define CREATE_TRACE_POINTS
#include "venc_trace_point.h"

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
#include <soc/xuantie/th1520_system_monitor.h>
#endif

/*------------------------------------------------------------------------
*****************************VCMD CONFIGURATION BY CUSTOMER********************************
-------------------------------------------------------------------------*/
//video encoder vcmd configuration

#define VCMD_ENC_IO_ADDR_0                      0x90000   /*customer specify according to own platform*/
#define VCMD_ENC_IO_SIZE_0                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_ENC_INT_PIN_0                      -1
#define VCMD_ENC_MODULE_TYPE_0                  0
#define VCMD_ENC_MODULE_MAIN_ADDR_0             0x1000    /*customer specify according to own platform*/
#define VCMD_ENC_MODULE_DEC400_ADDR_0           0X2000 //0X6000    /*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_L2CACHE_ADDR_0          0XFFFF
#define VCMD_ENC_MODULE_MMU0_ADDR_0             0X4000 //0X2000
#define VCMD_ENC_MODULE_MMU1_ADDR_0             0XFFFF //0X4000
#define VCMD_ENC_MODULE_AXIFE0_ADDR_0           0XFFFF //0X3000
#define VCMD_ENC_MODULE_AXIFE1_ADDR_0           0XFFFF //0X5000

#define VCMD_ENC_IO_ADDR_1                      0x91000   /*customer specify according to own platform*/
#define VCMD_ENC_IO_SIZE_1                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_ENC_INT_PIN_1                      -1
#define VCMD_ENC_MODULE_TYPE_1                  0
#define VCMD_ENC_MODULE_MAIN_ADDR_1             0x0000    /*customer specify according to own platform*/
#define VCMD_ENC_MODULE_DEC400_ADDR_1           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_L2CACHE_ADDR_1          0XFFFF
#define VCMD_ENC_MODULE_MMU_ADDR_1              0XFFFF

#define VCMD_ENC_IO_ADDR_2                      0x92000   /*customer specify according to own platform*/
#define VCMD_ENC_IO_SIZE_2                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_ENC_INT_PIN_2                      -1
#define VCMD_ENC_MODULE_TYPE_2                  0
#define VCMD_ENC_MODULE_MAIN_ADDR_2             0x0000    /*customer specify according to own platform*/
#define VCMD_ENC_MODULE_DEC400_ADDR_2           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_L2CACHE_ADDR_2          0XFFFF
#define VCMD_ENC_MODULE_MMU_ADDR_2              0XFFFF

#define VCMD_ENC_IO_ADDR_3                      0x93000   /*customer specify according to own platform*/
#define VCMD_ENC_IO_SIZE_3                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_ENC_INT_PIN_3                      -1
#define VCMD_ENC_MODULE_TYPE_3                  0
#define VCMD_ENC_MODULE_MAIN_ADDR_3             0x0000    /*customer specify according to own platform*/
#define VCMD_ENC_MODULE_DEC400_ADDR_3           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_ENC_MODULE_L2CACHE_ADDR_3          0XFFFF
#define VCMD_ENC_MODULE_MMU_ADDR_3              0XFFFF

//video encoder cutree/IM  vcmd configuration


#define VCMD_IM_IO_ADDR_0                      0x94000 //0xA0000    /*customer specify according to own platform*/
#define VCMD_IM_IO_SIZE_0                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_IM_INT_PIN_0                      -1
#define VCMD_IM_MODULE_TYPE_0                  1
#define VCMD_IM_MODULE_MAIN_ADDR_0             0x1000     /*customer specify according to own platform*/
#define VCMD_IM_MODULE_DEC400_ADDR_0           0XFFFF     /*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_L2CACHE_ADDR_0          0XFFFF
#define VCMD_IM_MODULE_MMU0_ADDR_0             0XFFFF //0X2000
#define VCMD_IM_MODULE_MMU1_ADDR_0             0XFFFF
#define VCMD_IM_MODULE_AXIFE0_ADDR_0           0XFFFF //0X3000
#define VCMD_IM_MODULE_AXIFE1_ADDR_0           0XFFFF //0XFFFF

#define VCMD_IM_IO_ADDR_1                      0xa1000    /*customer specify according to own platform*/
#define VCMD_IM_IO_SIZE_1                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_IM_INT_PIN_1                      -1
#define VCMD_IM_MODULE_TYPE_1                  1
#define VCMD_IM_MODULE_MAIN_ADDR_1             0x0000     /*customer specify according to own platform*/
#define VCMD_IM_MODULE_DEC400_ADDR_1           0XFFFF     /*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_L2CACHE_ADDR_1          0XFFFF
#define VCMD_IM_MODULE_MMU_ADDR_1              0XFFFF

#define VCMD_IM_IO_ADDR_2                      0xa2000    /*customer specify according to own platform*/
#define VCMD_IM_IO_SIZE_2                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_IM_INT_PIN_2                      -1
#define VCMD_IM_MODULE_TYPE_2                  1
#define VCMD_IM_MODULE_MAIN_ADDR_2             0x0000     /*customer specify according to own platform*/
#define VCMD_IM_MODULE_DEC400_ADDR_2           0XFFFF     /*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_L2CACHE_ADDR_2          0XFFFF
#define VCMD_IM_MODULE_MMU_ADDR_2              0XFFFF

#define VCMD_IM_IO_ADDR_3                      0xa3000    /*customer specify according to own platform*/
#define VCMD_IM_IO_SIZE_3                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_IM_INT_PIN_3                      -1
#define VCMD_IM_MODULE_TYPE_3                  1
#define VCMD_IM_MODULE_MAIN_ADDR_3             0x0000     /*customer specify according to own platform*/
#define VCMD_IM_MODULE_DEC400_ADDR_3           0XFFFF     /*0xffff means no such kind of submodule*/
#define VCMD_IM_MODULE_L2CACHE_ADDR_3          0XFFFF
#define VCMD_IM_MODULE_MMU_ADDR_3              0XFFFF

//video decoder vcmd configuration


#define VCMD_DEC_IO_ADDR_0                      0xb0000   /*customer specify according to own platform*/
#define VCMD_DEC_IO_SIZE_0                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_DEC_INT_PIN_0                      -1
#define VCMD_DEC_MODULE_TYPE_0                  2
#define VCMD_DEC_MODULE_MAIN_ADDR_0             0x0000    /*customer specify according to own platform*/
#define VCMD_DEC_MODULE_DEC400_ADDR_0           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_L2CACHE_ADDR_0          0XFFFF
#define VCMD_DEC_MODULE_MMU_ADDR_0              0XFFFF


#define VCMD_DEC_IO_ADDR_1                      0xb1000   /*customer specify according to own platform*/
#define VCMD_DEC_IO_SIZE_1                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_DEC_INT_PIN_1                      -1
#define VCMD_DEC_MODULE_TYPE_1                  2
#define VCMD_DEC_MODULE_MAIN_ADDR_1             0x0000    /*customer specify according to own platform*/
#define VCMD_DEC_MODULE_DEC400_ADDR_1           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_L2CACHE_ADDR_1          0XFFFF
#define VCMD_DEC_MODULE_MMU_ADDR_1              0XFFFF

#define VCMD_DEC_IO_ADDR_2                      0xb2000   /*customer specify according to own platform*/
#define VCMD_DEC_IO_SIZE_2                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_DEC_INT_PIN_2                      -1
#define VCMD_DEC_MODULE_TYPE_2                  2
#define VCMD_DEC_MODULE_MAIN_ADDR_2             0x0000    /*customer specify according to own platform*/
#define VCMD_DEC_MODULE_DEC400_ADDR_2           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_L2CACHE_ADDR_2          0XFFFF
#define VCMD_DEC_MODULE_MMU_ADDR_2              0XFFFF

#define VCMD_DEC_IO_ADDR_3                      0xb3000   /*customer specify according to own platform*/
#define VCMD_DEC_IO_SIZE_3                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_DEC_INT_PIN_3                      -1
#define VCMD_DEC_MODULE_TYPE_3                  2
#define VCMD_DEC_MODULE_MAIN_ADDR_3             0x0000    /*customer specify according to own platform*/
#define VCMD_DEC_MODULE_DEC400_ADDR_3           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_DEC_MODULE_L2CACHE_ADDR_3          0XFFFF
#define VCMD_DEC_MODULE_MMU_ADDR_3              0XFFFF

//JPEG encoder vcmd configuration

#define VCMD_JPEGE_IO_ADDR_0                      0x90000   /*customer specify according to own platform*/
#define VCMD_JPEGE_IO_SIZE_0                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGE_INT_PIN_0                      -1
#define VCMD_JPEGE_MODULE_TYPE_0                  3
#define VCMD_JPEGE_MODULE_MAIN_ADDR_0             0x1000    /*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_0           0XFFFF //0X4000    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_0          0XFFFF
#define VCMD_JPEGE_MODULE_MMU0_ADDR_0             0XFFFF //0X2000
#define VCMD_JPEGE_MODULE_MMU1_ADDR_0             0XFFFF
#define VCMD_JPEGE_MODULE_AXIFE0_ADDR_0           0XFFFF //0X3000
#define VCMD_JPEGE_MODULE_AXIFE1_ADDR_0           0XFFFF

#define VCMD_JPEGE_IO_ADDR_1                      0xC1000   /*customer specify according to own platform*/
#define VCMD_JPEGE_IO_SIZE_1                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGE_INT_PIN_1                      -1
#define VCMD_JPEGE_MODULE_TYPE_1                  3
#define VCMD_JPEGE_MODULE_MAIN_ADDR_1             0x0000    /*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_1           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_1          0XFFFF
#define VCMD_JPEGE_MODULE_MMU_ADDR_1              0XFFFF

#define VCMD_JPEGE_IO_ADDR_2                      0xC2000   /*customer specify according to own platform*/
#define VCMD_JPEGE_IO_SIZE_2                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGE_INT_PIN_2                      -1
#define VCMD_JPEGE_MODULE_TYPE_2                  3
#define VCMD_JPEGE_MODULE_MAIN_ADDR_2             0x0000    /*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_2           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_2          0XFFFF
#define VCMD_JPEGE_MODULE_MMU_ADDR_2              0XFFFF

#define VCMD_JPEGE_IO_ADDR_3                      0xC3000   /*customer specify according to own platform*/
#define VCMD_JPEGE_IO_SIZE_3                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGE_INT_PIN_3                      -1
#define VCMD_JPEGE_MODULE_TYPE_3                  3
#define VCMD_JPEGE_MODULE_MAIN_ADDR_3             0x0000    /*customer specify according to own platform*/
#define VCMD_JPEGE_MODULE_DEC400_ADDR_3           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGE_MODULE_L2CACHE_ADDR_3          0XFFFF
#define VCMD_JPEGE_MODULE_MMU_ADDR_3              0XFFFF


//JPEG decoder vcmd configuration

#define VCMD_JPEGD_IO_ADDR_0                      0xD0000   /*customer specify according to own platform*/
#define VCMD_JPEGD_IO_SIZE_0                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGD_INT_PIN_0                      -1
#define VCMD_JPEGD_MODULE_TYPE_0                  4
#define VCMD_JPEGD_MODULE_MAIN_ADDR_0             0x0000    /*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_DEC400_ADDR_0           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_0          0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_0              0XFFFF

#define VCMD_JPEGD_IO_ADDR_1                      0xD1000   /*customer specify according to own platform*/
#define VCMD_JPEGD_IO_SIZE_1                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGD_INT_PIN_1                      -1
#define VCMD_JPEGD_MODULE_TYPE_1                  4
#define VCMD_JPEGD_MODULE_MAIN_ADDR_1             0x0000    /*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_DEC400_ADDR_1           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_1          0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_1              0XFFFF

#define VCMD_JPEGD_IO_ADDR_2                      0xD2000   /*customer specify according to own platform*/
#define VCMD_JPEGD_IO_SIZE_2                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGD_INT_PIN_2                      -1
#define VCMD_JPEGD_MODULE_TYPE_2                  4
#define VCMD_JPEGD_MODULE_MAIN_ADDR_2             0x0000    /*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_DEC400_ADDR_2           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_2          0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_2              0XFFFF

#define VCMD_JPEGD_IO_ADDR_3                      0xD3000   /*customer specify according to own platform*/
#define VCMD_JPEGD_IO_SIZE_3                      (ASIC_VCMD_SWREG_AMOUNT * 4)    /* bytes */
#define VCMD_JPEGD_INT_PIN_3                      -1
#define VCMD_JPEGD_MODULE_TYPE_3                  4
#define VCMD_JPEGD_MODULE_MAIN_ADDR_3             0x0000    /*customer specify according to own platform*/
#define VCMD_JPEGD_MODULE_DEC400_ADDR_3           0XFFFF    /*0xffff means no such kind of submodule*/
#define VCMD_JPEGD_MODULE_L2CACHE_ADDR_3          0XFFFF
#define VCMD_JPEGD_MODULE_MMU_ADDR_3              0XFFFF

struct vcmd_config
{
  unsigned long vcmd_base_addr;
  u32 vcmd_iosize;
  int vcmd_irq;
  u32 sub_module_type;  /*input vc8000e=0,IM=1,vc8000d=2,jpege=3, jpegd=4*/
  u16 submodule_main_addr; // in byte
  u16 submodule_dec400_addr;//if submodule addr == 0xffff, this submodule does not exist.// in byte
  u16 submodule_L2Cache_addr; // in byte
  u16 submodule_MMU_addr[2]; // in byte
  u16 submodule_axife_addr[2]; // in byte
};

#define NETINT
//#define MAGVII
//#define OYB_VCEJ
//#define OYB_VCE

/*for all vcmds, the core info should be listed here for subsequent use*/
static struct vcmd_config vcmd_core_array[]= {
#if defined(NETINT) || defined(OYB_VCE)
    //encoder configuration
    {VCMD_ENC_IO_ADDR_0,
    VCMD_ENC_IO_SIZE_0,
    VCMD_ENC_INT_PIN_0,
    VCMD_ENC_MODULE_TYPE_0,
    VCMD_ENC_MODULE_MAIN_ADDR_0,
    VCMD_ENC_MODULE_DEC400_ADDR_0,
    VCMD_ENC_MODULE_L2CACHE_ADDR_0,
    {VCMD_ENC_MODULE_MMU0_ADDR_0,
     VCMD_ENC_MODULE_MMU1_ADDR_0},
    {VCMD_ENC_MODULE_AXIFE0_ADDR_0,
     VCMD_ENC_MODULE_AXIFE1_ADDR_0}},
#endif
#if 0
    {VCMD_ENC_IO_ADDR_1,
    VCMD_ENC_IO_SIZE_1,
    VCMD_ENC_INT_PIN_1,
    VCMD_ENC_MODULE_TYPE_1,
    VCMD_ENC_MODULE_MAIN_ADDR_1,
    VCMD_ENC_MODULE_DEC400_ADDR_1,
    VCMD_ENC_MODULE_L2CACHE_ADDR_1,
    VCMD_ENC_MODULE_MMU_ADDR_1},

    {VCMD_ENC_IO_ADDR_2,
    VCMD_ENC_IO_SIZE_2,
    VCMD_ENC_INT_PIN_2,
    VCMD_ENC_MODULE_TYPE_2,
    VCMD_ENC_MODULE_MAIN_ADDR_2,
    VCMD_ENC_MODULE_DEC400_ADDR_2,
    VCMD_ENC_MODULE_L2CACHE_ADDR_2,
    VCMD_ENC_MODULE_MMU_ADDR_2},

    {VCMD_ENC_IO_ADDR_3,
    VCMD_ENC_IO_SIZE_3,
    VCMD_ENC_INT_PIN_3,
    VCMD_ENC_MODULE_TYPE_3,
    VCMD_ENC_MODULE_MAIN_ADDR_3,
    VCMD_ENC_MODULE_DEC400_ADDR_3,
    VCMD_ENC_MODULE_L2CACHE_ADDR_3,
    VCMD_ENC_MODULE_MMU_ADDR_3},
#endif
    //cutree/IM configuration
#if defined(NETINT) || defined(OYB_VCE)
    {VCMD_IM_IO_ADDR_0,
    VCMD_IM_IO_SIZE_0,
    VCMD_IM_INT_PIN_0,
    VCMD_IM_MODULE_TYPE_0,
    VCMD_IM_MODULE_MAIN_ADDR_0,
    VCMD_IM_MODULE_DEC400_ADDR_0,
    VCMD_IM_MODULE_L2CACHE_ADDR_0,
    {VCMD_IM_MODULE_MMU0_ADDR_0,
     VCMD_IM_MODULE_MMU1_ADDR_0},
    {VCMD_IM_MODULE_AXIFE0_ADDR_0,
     VCMD_IM_MODULE_AXIFE1_ADDR_0}},
#endif
#if 0
    {VCMD_IM_IO_ADDR_1,
    VCMD_IM_IO_SIZE_1,
    VCMD_IM_INT_PIN_1,
    VCMD_IM_MODULE_TYPE_1,
    VCMD_IM_MODULE_MAIN_ADDR_1,
    VCMD_IM_MODULE_DEC400_ADDR_1,
    VCMD_IM_MODULE_L2CACHE_ADDR_1,
    VCMD_IM_MODULE_MMU_ADDR_1},

    {VCMD_IM_IO_ADDR_2,
    VCMD_IM_IO_SIZE_2,
    VCMD_IM_INT_PIN_2,
    VCMD_IM_MODULE_TYPE_2,
    VCMD_IM_MODULE_MAIN_ADDR_2,
    VCMD_IM_MODULE_DEC400_ADDR_2,
    VCMD_IM_MODULE_L2CACHE_ADDR_2,
    VCMD_IM_MODULE_MMU_ADDR_2},

    {VCMD_IM_IO_ADDR_3,
    VCMD_IM_IO_SIZE_3,
    VCMD_IM_INT_PIN_3,
    VCMD_IM_MODULE_TYPE_3,
    VCMD_IM_MODULE_MAIN_ADDR_3,
    VCMD_IM_MODULE_DEC400_ADDR_3,
    VCMD_IM_MODULE_L2CACHE_ADDR_3,
    VCMD_IM_MODULE_MMU_ADDR_3},

    //decoder configuration
    {VCMD_DEC_IO_ADDR_0,
    VCMD_DEC_IO_SIZE_0,
    VCMD_DEC_INT_PIN_0,
    VCMD_DEC_MODULE_TYPE_0,
    VCMD_DEC_MODULE_MAIN_ADDR_0,
    VCMD_DEC_MODULE_DEC400_ADDR_0,
    VCMD_DEC_MODULE_L2CACHE_ADDR_0,
    VCMD_DEC_MODULE_MMU_ADDR_0},

    {VCMD_DEC_IO_ADDR_1,
    VCMD_DEC_IO_SIZE_1,
    VCMD_DEC_INT_PIN_1,
    VCMD_DEC_MODULE_TYPE_1,
    VCMD_DEC_MODULE_MAIN_ADDR_1,
    VCMD_DEC_MODULE_DEC400_ADDR_1,
    VCMD_DEC_MODULE_L2CACHE_ADDR_1,
    VCMD_DEC_MODULE_MMU_ADDR_1},

    {VCMD_DEC_IO_ADDR_2,
    VCMD_DEC_IO_SIZE_2,
    VCMD_DEC_INT_PIN_2,
    VCMD_DEC_MODULE_TYPE_2,
    VCMD_DEC_MODULE_MAIN_ADDR_2,
    VCMD_DEC_MODULE_DEC400_ADDR_2,
    VCMD_DEC_MODULE_L2CACHE_ADDR_2,
    VCMD_DEC_MODULE_MMU_ADDR_2},

    {VCMD_DEC_IO_ADDR_3,
    VCMD_DEC_IO_SIZE_3,
    VCMD_DEC_INT_PIN_3,
    VCMD_DEC_MODULE_TYPE_3,
    VCMD_DEC_MODULE_MAIN_ADDR_3,
    VCMD_DEC_MODULE_DEC400_ADDR_3,
    VCMD_DEC_MODULE_L2CACHE_ADDR_3,
    VCMD_DEC_MODULE_MMU_ADDR_3},
#endif
#if defined(MAGVII) || defined(OYB_VCEJ)
    //JPEG encoder configuration
    {VCMD_JPEGE_IO_ADDR_0,
    VCMD_JPEGE_IO_SIZE_0,
    VCMD_JPEGE_INT_PIN_0,
    VCMD_JPEGE_MODULE_TYPE_0,
    VCMD_JPEGE_MODULE_MAIN_ADDR_0,
    VCMD_JPEGE_MODULE_DEC400_ADDR_0,
    VCMD_JPEGE_MODULE_L2CACHE_ADDR_0,
    {VCMD_JPEGE_MODULE_MMU0_ADDR_0,
     VCMD_JPEGE_MODULE_MMU1_ADDR_0},
    {VCMD_JPEGE_MODULE_AXIFE0_ADDR_0,
     VCMD_JPEGE_MODULE_AXIFE1_ADDR_0}},
#endif
#if 0
    {VCMD_JPEGE_IO_ADDR_1,
    VCMD_JPEGE_IO_SIZE_1,
    VCMD_JPEGE_INT_PIN_1,
    VCMD_JPEGE_MODULE_TYPE_1,
    VCMD_JPEGE_MODULE_MAIN_ADDR_1,
    VCMD_JPEGE_MODULE_DEC400_ADDR_1,
    VCMD_JPEGE_MODULE_L2CACHE_ADDR_1,
    VCMD_JPEGE_MODULE_MMU_ADDR_1},

    {VCMD_JPEGE_IO_ADDR_2,
    VCMD_JPEGE_IO_SIZE_2,
    VCMD_JPEGE_INT_PIN_2,
    VCMD_JPEGE_MODULE_TYPE_2,
    VCMD_JPEGE_MODULE_MAIN_ADDR_2,
    VCMD_JPEGE_MODULE_DEC400_ADDR_2,
    VCMD_JPEGE_MODULE_L2CACHE_ADDR_2,
    VCMD_JPEGE_MODULE_MMU_ADDR_2},

    {VCMD_JPEGE_IO_ADDR_3,
    VCMD_JPEGE_IO_SIZE_3,
    VCMD_JPEGE_INT_PIN_3,
    VCMD_JPEGE_MODULE_TYPE_3,
    VCMD_JPEGE_MODULE_MAIN_ADDR_3,
    VCMD_JPEGE_MODULE_DEC400_ADDR_3,
    VCMD_JPEGE_MODULE_L2CACHE_ADDR_3,
    VCMD_JPEGE_MODULE_MMU_ADDR_3},
    //JPEG decoder configuration
    {VCMD_JPEGD_IO_ADDR_0,
    VCMD_JPEGD_IO_SIZE_0,
    VCMD_JPEGD_INT_PIN_0,
    VCMD_JPEGD_MODULE_TYPE_0,
    VCMD_JPEGD_MODULE_MAIN_ADDR_0,
    VCMD_JPEGD_MODULE_DEC400_ADDR_0,
    VCMD_JPEGD_MODULE_L2CACHE_ADDR_0,
    VCMD_JPEGD_MODULE_MMU_ADDR_0},

    {VCMD_JPEGD_IO_ADDR_1,
    VCMD_JPEGD_IO_SIZE_1,
    VCMD_JPEGD_INT_PIN_1,
    VCMD_JPEGD_MODULE_TYPE_1,
    VCMD_JPEGD_MODULE_MAIN_ADDR_1,
    VCMD_JPEGD_MODULE_DEC400_ADDR_1,
    VCMD_JPEGD_MODULE_L2CACHE_ADDR_1,
    VCMD_JPEGD_MODULE_MMU_ADDR_1},

    {VCMD_JPEGD_IO_ADDR_2,
    VCMD_JPEGD_IO_SIZE_2,
    VCMD_JPEGD_INT_PIN_2,
    VCMD_JPEGD_MODULE_TYPE_2,
    VCMD_JPEGD_MODULE_MAIN_ADDR_2,
    VCMD_JPEGD_MODULE_DEC400_ADDR_2,
    VCMD_JPEGD_MODULE_L2CACHE_ADDR_2,
    VCMD_JPEGD_MODULE_MMU_ADDR_2},

    {VCMD_JPEGD_IO_ADDR_3,
    VCMD_JPEGD_IO_SIZE_3,
    VCMD_JPEGD_INT_PIN_3,
    VCMD_JPEGD_MODULE_TYPE_3,
    VCMD_JPEGD_MODULE_MAIN_ADDR_3,
    VCMD_JPEGD_MODULE_DEC400_ADDR_3,
    VCMD_JPEGD_MODULE_L2CACHE_ADDR_3,
    VCMD_JPEGD_MODULE_MMU_ADDR_3},
#endif
};
/*these size need to be modified according to hw config.*/
#define VCMD_ENCODER_REGISTER_SIZE              (479 * 4)
#define VCMD_DECODER_REGISTER_SIZE              (512 * 4)
#define VCMD_IM_REGISTER_SIZE                   (479 * 4)
#define VCMD_JPEG_ENCODER_REGISTER_SIZE         (479 * 4)
#define VCMD_JPEG_DECODER_REGISTER_SIZE         (512 * 4)

#define MAX_VCMD_NUMBER     (MAX_VCMD_TYPE*MAX_SAME_MODULE_TYPE_CORE_NUMBER)              //


#define HW_WORK_STATE_PEND            3

#define MAX_CMDBUF_INT_NUMBER         1
#define INT_MIN_SUM_OF_IMAGE_SIZE    (4096*2160*MAX_SAME_MODULE_TYPE_CORE_NUMBER*MAX_CMDBUF_INT_NUMBER)
#define MAX_PROCESS_CORE_NUMBER         4*8
#define PROCESS_MAX_SUM_OF_IMAGE_SIZE     (4096*2160*MAX_SAME_MODULE_TYPE_CORE_NUMBER*MAX_PROCESS_CORE_NUMBER)


#define MAX_SAME_MODULE_TYPE_CORE_NUMBER            4

#define VC8000E_MAX_CONFIG_LEN          32

#define VC8000E_PM_TIMEOUT              100 /* ms */

static size_t base_ddr_addr = 0;              /*pcie address need to substract this value then can be put to register*/

#ifdef HANTROAXIFE_SUPPORT
#define AXIFE_SIZE (64*4)
volatile u8* axife_hwregs[MAX_VCMD_NUMBER][2];
#endif

#ifdef HANTROMMU_SUPPORT
#define MMU_SIZE   (228*4)
extern unsigned int mmu_enable;
extern unsigned long gBaseDDRHw;
static volatile u8* mmu_hwregs[MAX_VCMD_NUMBER][2];
#else
static unsigned int mmu_enable = 0;
#endif

/********variables declaration related with race condition**********/

#define CMDBUF_MAX_SIZE       (512*4*4)


#define CMDBUF_POOL_TOTAL_SIZE     (2*1024*1024)        //approximately=128x(320x240)=128x2k=128x8kbyte=1Mbytes
#define TOTAL_DISCRETE_CMDBUF_NUM (CMDBUF_POOL_TOTAL_SIZE/CMDBUF_MAX_SIZE)
#define CMDBUF_VCMD_REGISTER_TOTAL_SIZE        9*1024*1024-CMDBUF_POOL_TOTAL_SIZE*2
#define VCMD_REGISTER_SIZE       (128*4)

#ifndef DYNAMIC_MALLOC_VCMDNODE
static struct cmdbuf_obj *g_cmdbuf_obj_pool;
static struct bi_list_node *g_cmdbuf_node_pool;
#endif

int debug_pr_devfreq_info = 0;

struct noncache_mem
{
  u32 *virtualAddress;
  dma_addr_t  busAddress;
  unsigned int mmu_bus_address;  /* buffer physical address in MMU*/
  u32 size;
  u16 cmdbuf_id;
};

struct process_manager_obj
{
    struct file *filp;
    u32 total_exe_time;
    u32 pm_count;
    spinlock_t spinlock;
    wait_queue_head_t wait_queue;
} ;

struct cmdbuf_obj
{
  u32 module_type;               //current CMDBUF type: input vc8000e=0,IM=1,vc8000d=2,jpege=3, jpegd=4
  u32 priority;                  //current CMDBUFpriority: normal=0, high=1
  u32 executing_time;           //current CMDBUFexecuting_time=encoded_image_size*(rdoLevel+1)*(rdoq+1);
  u32 cmdbuf_size;              //current CMDBUF size
  u32 *cmdbuf_virtualAddress;   //current CMDBUF start virtual address.
  size_t cmdbuf_busAddress;      //current CMDBUF start physical address.
  unsigned int mmu_cmdbuf_busAddress;      //current CMDBUF start mmu mapping address.
  u32 *status_virtualAddress;   //current status CMDBUF start virtual address.
  size_t status_busAddress;      //current status CMDBUF start physical address.
  unsigned int mmu_status_busAddress;      //current status CMDBUF start mmu mapping address.
  u32 status_size;              //current status CMDBUF size
  u32 executing_status;          //current CMDBUF executing status.
  struct file *filp;            //file pointer in the same process.
  u16 core_id;                  //which vcmd core is used.
  u16 cmdbuf_id;                 //used to manage CMDBUF in driver.It is a handle to identify cmdbuf.also is an interrupt vector.position in pool,same as status position.
  u8 cmdbuf_data_loaded;        //0 means sw has not copied data into this CMDBUF; 1 means sw has copied data into this CMDBUF
  u8 cmdbuf_data_linked;        //0 :not linked, 1:linked.into a vcmd core list.
  volatile u8 cmdbuf_run_done;           //if 0,waiting for CMDBUF finish; if 1, op code in CMDBUF has finished one by one. HANTRO_VCMD_IOCH_WAIT_CMDBUF will check this variable.
  u8 cmdbuf_need_remove;        // if 0, not need to remove CMDBUF; 1 CMDBUF can be removed if it is not the last CMDBUF;
  u8 has_end_cmdbuf;            //if 1, the last opcode is end opCode.
  u8 no_normal_int_cmdbuf;      //if 1, JMP will not send normal interrupt.
  struct process_manager_obj* process_manager_obj;
};

struct hantrovcmd_dev
{
    struct vcmd_config vcmd_core_cfg; //config of each core,such as base addr, irq,etc
    u32 core_id; //vcmd core id for driver and sw internal use
    u32 sw_cmdbuf_rdy_num;
    spinlock_t* spinlock;
    wait_queue_head_t * wait_queue;
    wait_queue_head_t * wait_abort_queue;
    bi_list list_manager;
    volatile u8 *hwregs;/* IO mem base */
    u32 reg_mirror[ASIC_VCMD_SWREG_AMOUNT];
    u32 duration_without_int;              //number of cmdbufs without interrupt.
    volatile u8 working_state;
    u32 total_exe_time;
    u16 status_cmdbuf_id;//used for analyse configuration in cwl.
    u32 hw_version_id; /*megvii 0x43421001, later 0x43421102*/
    u32 *vcmd_reg_mem_virtualAddress;//start virtual address of vcmd registers memory of  CMDBUF.
    size_t vcmd_reg_mem_busAddress;  //start physical address of vcmd registers memory of  CMDBUF.
    unsigned int mmu_vcmd_reg_mem_busAddress;  //start mmu mapping address of vcmd registers memory of CMDBUF.
    u32  vcmd_reg_mem_size;              // size of vcmd registers memory of CMDBUF.
    struct platform_device *pdev;
    struct clk *cclk;
    struct clk *aclk;
    struct clk *pclk;
    char config_buf[VC8000E_MAX_CONFIG_LEN];
    int has_power_domains;
    bi_list_node* last_linked_cmdbuf_node;
    bi_list_node* suspend_running_cmdbuf_node;
    bool suspend_entered;
    struct encoder_devfreq devfreq;
} ;

/*
 * Ioctl definitions
 */
#define VCMD_HW_ID                  0x4342


static struct noncache_mem vcmd_buf_mem_pool;
static struct noncache_mem vcmd_status_buf_mem_pool;
static struct noncache_mem vcmd_registers_mem_pool;

static u16 cmdbuf_used[TOTAL_DISCRETE_CMDBUF_NUM];
static u16 cmdbuf_used_pos;
static u16 cmdbuf_used_residual;

static struct hantrovcmd_dev* vcmd_manager[MAX_VCMD_TYPE][MAX_VCMD_NUMBER];
static bi_list_node* global_cmdbuf_node[TOTAL_DISCRETE_CMDBUF_NUM];

static bi_list global_process_manager;

static u16 vcmd_position[MAX_VCMD_TYPE];
static int vcmd_type_core_num[MAX_VCMD_TYPE];

#define EXECUTING_CMDBUF_ID_ADDR       26
#define VCMD_EXE_CMDBUF_COUNT           3

#define WORKING_STATE_IDLE          0
#define WORKING_STATE_WORKING       1
#define CMDBUF_EXE_STATUS_OK        0
#define CMDBUF_EXE_STATUS_CMDERR        1
#define CMDBUF_EXE_STATUS_BUSERR        2


struct semaphore vcmd_reserve_cmdbuf_sem[MAX_VCMD_TYPE]; //for reserve

//#define VCMD_DEBUG_INTERNAL

/***************************TYPE AND FUNCTION DECLARATION****************/

/* here's all the must remember stuff */


static int vcmd_reserve_IO(void);
static void vcmd_release_IO(void);
static void vcmd_reset_asic(struct hantrovcmd_dev * dev);
static void vcmd_reset_current_asic(struct hantrovcmd_dev * dev);
static int allocate_cmdbuf(struct noncache_mem*  new_cmdbuf_addr,struct noncache_mem*  new_status_cmdbuf_addr);
static void vcmd_link_cmdbuf(struct hantrovcmd_dev *dev,bi_list_node* last_linked_cmdbuf_node);
static void vcmd_start(struct hantrovcmd_dev *dev,bi_list_node* first_linked_cmdbuf_node);
static void create_kernel_process_manager(void);
static void vcmd_reset(void);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id, struct pt_regs *regs);
#else
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id);
#endif

#ifdef VCMD_DEBUG_INTERNAL
static void printk_vcmd_register_debug(const void *hwregs, char* info);
#endif

/*********************local variable declaration*****************/
static unsigned long vcmd_sram_base = 0;
static unsigned int vcmd_sram_size = 0;
/* and this is our MAJOR; use 0 for dynamic allocation (recommended)*/
static int hantrovcmd_major = 0;
static int hantrovcmd_minor = 0; /* dynamic allocation */
static struct cdev hantrovcmd_cdev;
static dev_t hantrovcmd_devt;
static struct class *hantrovcmd_class;
static int total_vcmd_core_num = 0;
/* dynamic allocation*/
static struct hantrovcmd_dev* hantrovcmd_data = NULL;
static struct dentry *root_debugfs_dir = NULL;

static int software_triger_abort=0;

//#define IRQ_SIMULATION

#ifdef IRQ_SIMULATION
struct timer_manager
{
    u32 core_id; //vcmd core id for driver and sw internal use
    u32 timer_id;
    struct timer_list *timer;
} ;

static struct timer_list timer[10000];
struct timer_manager timer_reserve[10000];
#if 0
static struct timer_list timer0;
static struct timer_list timer1;
#endif
#endif


//hw_queue can be used for reserve cmdbuf memory
DECLARE_WAIT_QUEUE_HEAD(vcmd_cmdbuf_memory_wait);

DEFINE_SPINLOCK(vcmd_cmdbuf_alloc_lock);
DEFINE_SPINLOCK(vcmd_process_manager_lock);


static spinlock_t owner_lock_vcmd[MAX_VCMD_NUMBER];

static wait_queue_head_t wait_queue_vcmd[MAX_VCMD_NUMBER];

static wait_queue_head_t abort_queue_vcmd[MAX_VCMD_NUMBER];

#if 0
/*allocate non-cacheable DMA memory*/
#define DRIVER_NAME_HANTRO_NON_CACH_MEM "non_cach_memory"

static struct platform_device *noncachable_mem_dev = NULL;


static const struct platform_device_info hantro_platform_info = {
    .name = DRIVER_NAME_HANTRO_NON_CACH_MEM,
    .id = -1,
    .dma_mask = DMA_BIT_MASK(32),
};


static int hantro_noncachable_mem_probe(struct platform_device *pdev)
{
  struct device *dev = &pdev->dev;
  vcmd_buf_mem_pool.virtualAddress = dma_alloc_coherent(dev,CMDBUF_POOL_TOTAL_SIZE,&vcmd_buf_mem_pool.busAddress, GFP_KERNEL | GFP_DMA);
  vcmd_buf_mem_pool.size = CMDBUF_POOL_TOTAL_SIZE;
  vcmd_status_buf_mem_pool.virtualAddress = dma_alloc_coherent(dev,CMDBUF_POOL_TOTAL_SIZE,&vcmd_status_buf_mem_pool.busAddress, GFP_KERNEL | GFP_DMA);
  vcmd_status_buf_mem_pool.size = CMDBUF_POOL_TOTAL_SIZE;
  return 0;
}

static int hantro_noncachable_mem_remove(struct platform_device *pdev)
{
  struct device *dev = &pdev->dev;

  dma_free_coherent(dev,vcmd_buf_mem_pool.size,vcmd_buf_mem_pool.virtualAddress,vcmd_buf_mem_pool.busAddress);
  dma_free_coherent(dev,vcmd_status_buf_mem_pool.size,vcmd_status_buf_mem_pool.virtualAddress,vcmd_status_buf_mem_pool.busAddress);

  return 0;
}


static const struct platform_device_id hantro_noncachable_mem_platform_ids[]={
  {
    .name = DRIVER_NAME_HANTRO_NON_CACH_MEM,
  },
  {/* sentinel */},
};


static const struct of_device_id  hantro_of_match[]={
  {
    .compatible  =  "thead,light-vc8000e",
  },
  {/* sentinel */},
};

static struct platform_driver hantro_noncachable_mem_platform_driver = {
  .probe  = hantro_noncachable_mem_probe,
  .remove  = hantro_noncachable_mem_remove,
  .driver  ={
    .name = DRIVER_NAME_HANTRO_NON_CACH_MEM,
    .owner  = THIS_MODULE,
    .of_match_table = hantro_of_match,
  },
  .id_table = hantro_noncachable_mem_platform_ids,
};

static void init_vcmd_non_cachable_memory_allocate(void)
{

  /*create device: This will create a {struct platform_device}, It has a member dev, which is a {struct device} */
  noncachable_mem_dev = platform_device_register_full(&hantro_platform_info);

  /*when this function is called, the .probe callback is invoked.*/
  platform_driver_register(&hantro_noncachable_mem_platform_driver);


}

static void release_vcmd_non_cachable_memory(void)
{

  /* when this fucntion is called, .remove callback will be invoked. use it to clean up all resources allocated in .probe.*/
  platform_driver_unregister(&hantro_noncachable_mem_platform_driver);

  /*destroy the device*/
  platform_device_unregister(noncachable_mem_dev);
}
#endif
/**********************************************************************************************************\
*cmdbuf object management
\***********************************************************************************************************/
static struct cmdbuf_obj* create_cmdbuf_obj(void)
{
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  cmdbuf_obj=vmalloc(sizeof(struct cmdbuf_obj));
  if(cmdbuf_obj==NULL)
  {
    PDEBUG ("%s\n","vmalloc for cmdbuf_obj fail!");
    return cmdbuf_obj;
  }
  memset(cmdbuf_obj,0,sizeof(struct cmdbuf_obj));
  return cmdbuf_obj;
}

static void free_cmdbuf_obj(struct cmdbuf_obj* cmdbuf_obj)
{
#ifdef DYNAMIC_MALLOC_VCMDNODE
  if(cmdbuf_obj==NULL)
  {
    PDEBUG ("%s\n","remove_cmdbuf_obj NULL");
    return;
  }
  //free current cmdbuf_obj
  vfree(cmdbuf_obj);
  return;
#endif
}

static struct cmdbuf_obj *create_vcmd_cmdbuf_obj(u16 cmdbuf_id)
{
  struct cmdbuf_obj *cmdbuf_obj = NULL;

#ifdef DYNAMIC_MALLOC_VCMDNODE
  cmdbuf_obj = create_cmdbuf_obj();
#else
  cmdbuf_obj = g_cmdbuf_obj_pool + cmdbuf_id;
  if (cmdbuf_obj)
    memset(cmdbuf_obj, 0, sizeof(struct cmdbuf_obj));
#endif
  return cmdbuf_obj;
}

static bi_list_node *bi_list_create_vcmd_node(u16 cmdbuf_id)
{
  bi_list_node *node = NULL;

#ifdef DYNAMIC_MALLOC_VCMDNODE
  node = bi_list_create_node();
#else
  node = g_cmdbuf_node_pool + cmdbuf_id;
  if (node)
    memset(node, 0, sizeof(bi_list_node));
#endif
  return node;
}

static void free_cmdbuf_mem(u16 cmdbuf_id )
{
    unsigned long flags;

    spin_lock_irqsave(&vcmd_cmdbuf_alloc_lock, flags);
    cmdbuf_used[cmdbuf_id]=0;
    cmdbuf_used_residual +=1;
    PDEBUG(" ## real free cmdbuf[%d]\n",cmdbuf_id);
    spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
    wake_up_all(&vcmd_cmdbuf_memory_wait);
}

static void free_cmdbuf_not_linked_by_flip(struct file *filp)
{
    unsigned long flags;
    int i;
    struct cmdbuf_obj* cmdbuf_obj;
    bi_list_node* new_cmdbuf_node;
    bool freed_flag = false;
    spin_lock_irqsave(&vcmd_cmdbuf_alloc_lock, flags);

    for(i = 0; i < TOTAL_DISCRETE_CMDBUF_NUM; i++)
    {
      if(cmdbuf_used[i] && global_cmdbuf_node[i] != NULL)
      {
          new_cmdbuf_node = global_cmdbuf_node[i];
          if(new_cmdbuf_node == (void*)0x55555555)
          {
              continue;
          }
          cmdbuf_obj = (struct cmdbuf_obj* )new_cmdbuf_node->data;
          if(cmdbuf_obj->filp == filp && !cmdbuf_obj->cmdbuf_data_linked && !cmdbuf_obj->cmdbuf_run_done)
          {
              cmdbuf_used[i]=0;
              cmdbuf_used_residual +=1;
              global_cmdbuf_node[i] = NULL;
              freed_flag = true;
              PDEBUG(" ## Find left node not freed,real free cmdbuf[%d],remain %d\n",i,cmdbuf_used_residual);
          }
      }

      if(cmdbuf_used_residual >= (TOTAL_DISCRETE_CMDBUF_NUM-2))
        break;

    }
    spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
    if(freed_flag)
      wake_up_all(&vcmd_cmdbuf_memory_wait);
}

static bi_list_node* create_cmdbuf_node(void)
{
  bi_list_node* current_node=NULL;
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  struct noncache_mem  new_cmdbuf_addr;
  struct noncache_mem  new_status_cmdbuf_addr;

  if(!wait_event_interruptible_timeout(vcmd_cmdbuf_memory_wait, allocate_cmdbuf(&new_cmdbuf_addr,&new_status_cmdbuf_addr),
      msecs_to_jiffies(500)) )
  {
      pr_err("vc8000e: wait allocate_cmdbuf timeout\n");
      return NULL;
  }

  cmdbuf_obj = create_vcmd_cmdbuf_obj(new_cmdbuf_addr.cmdbuf_id);
  if(cmdbuf_obj==NULL)
  {
    PDEBUG ("%s\n","create_vcmd_cmdbuf_obj fail!");
    free_cmdbuf_mem(new_cmdbuf_addr.cmdbuf_id);
    return NULL;
  }
  cmdbuf_obj->cmdbuf_busAddress = new_cmdbuf_addr.busAddress;
  cmdbuf_obj->mmu_cmdbuf_busAddress = new_cmdbuf_addr.mmu_bus_address;
  cmdbuf_obj->cmdbuf_virtualAddress = new_cmdbuf_addr.virtualAddress;
  cmdbuf_obj->cmdbuf_size = new_cmdbuf_addr.size;
  cmdbuf_obj->cmdbuf_id = new_cmdbuf_addr.cmdbuf_id;
  cmdbuf_obj->status_busAddress = new_status_cmdbuf_addr.busAddress;
  cmdbuf_obj->mmu_status_busAddress = new_status_cmdbuf_addr.mmu_bus_address;
  cmdbuf_obj->status_virtualAddress = new_status_cmdbuf_addr.virtualAddress;
  cmdbuf_obj->status_size = new_status_cmdbuf_addr.size;
  current_node=bi_list_create_vcmd_node(cmdbuf_obj->cmdbuf_id);
  if(current_node==NULL)
  {
    PDEBUG ("%s\n","bi_list_create_vcmd_node fail!");
    free_cmdbuf_mem(new_cmdbuf_addr.cmdbuf_id);
    free_cmdbuf_obj(cmdbuf_obj);
    return NULL;
  }
  current_node->data = (void*)cmdbuf_obj;
  current_node->next = NULL;
  current_node->previous = NULL;
  return current_node;
}
static void free_cmdbuf_node(bi_list_node* cmdbuf_node)
{
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  if(cmdbuf_node==NULL)
  {
    PDEBUG ("%s\n","remove_cmdbuf_node NULL");
    return;
  }
  cmdbuf_obj = (struct cmdbuf_obj*)cmdbuf_node->data;
  //free cmdbuf mem in pool
  free_cmdbuf_mem(cmdbuf_obj->cmdbuf_id);

  //free struct cmdbuf_obj
  free_cmdbuf_obj(cmdbuf_obj);
#ifdef DYNAMIC_MALLOC_VCMDNODE
  //free current cmdbuf_node entity.
  bi_list_free_node(cmdbuf_node);
#endif
  return;
}

//just remove, not free the node.
static bi_list_node* remove_cmdbuf_node_from_list(bi_list* list,bi_list_node* cmdbuf_node)
{
  if(cmdbuf_node==NULL)
  {
    PDEBUG ("%s\n","remove_cmdbuf_node_from_list  NULL");
    return NULL;
  }
  if(cmdbuf_node->next)
  {
    bi_list_remove_node(list,cmdbuf_node);
    return cmdbuf_node;
  }
  else
  {
    //the last one, should not be removed.
    return NULL;
  }
}
//calculate executing_time of each vcmd
static u32 calculate_executing_time_after_node(bi_list_node* exe_cmdbuf_node)
{
  u32 time_run_all=0;
  struct cmdbuf_obj* cmdbuf_obj_temp=NULL;
  while(1)
  {
    if(exe_cmdbuf_node==NULL)
      break;
    cmdbuf_obj_temp=(struct cmdbuf_obj* )exe_cmdbuf_node->data;
    time_run_all += cmdbuf_obj_temp->executing_time;
    exe_cmdbuf_node = exe_cmdbuf_node->next;
  }
  return time_run_all;
}
static u32 calculate_executing_time_after_node_high_priority(bi_list_node* exe_cmdbuf_node)
{
  u32 time_run_all=0;
  struct cmdbuf_obj* cmdbuf_obj_temp=NULL;
  if(exe_cmdbuf_node==NULL)
    return time_run_all;
  cmdbuf_obj_temp=(struct cmdbuf_obj* )exe_cmdbuf_node->data;
  time_run_all += cmdbuf_obj_temp->executing_time;
  exe_cmdbuf_node = exe_cmdbuf_node->next;
  while(1)
  {
    if(exe_cmdbuf_node==NULL)
      break;
    cmdbuf_obj_temp=(struct cmdbuf_obj* )exe_cmdbuf_node->data;
    if(cmdbuf_obj_temp->priority==CMDBUF_PRIORITY_NORMAL)
      break;
    time_run_all += cmdbuf_obj_temp->executing_time;
    exe_cmdbuf_node = exe_cmdbuf_node->next;
  }
  return time_run_all;
}
#ifdef DEBUG_CMDBUF_ALLOC
int cmdbuf_node_unexpected[TOTAL_DISCRETE_CMDBUF_NUM];
#endif
/**********************************************************************************************************\
*cmdbuf pool management
\***********************************************************************************************************/
static int allocate_cmdbuf(struct noncache_mem*  new_cmdbuf_addr,struct noncache_mem*  new_status_cmdbuf_addr)
{
  unsigned long flags;
  u16  cmdbuf_used_pos_start = cmdbuf_used_pos;
  spin_lock_irqsave(&vcmd_cmdbuf_alloc_lock, flags);
  if(cmdbuf_used_residual==0)
  {
    spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
    pr_err("%s:vc8000e:no empty cmdbuf\n",__func__);
    //no empty cmdbuf
    return 0;
  }
  //there is one cmdbuf at least
  while(1)
  {
    if(cmdbuf_used[cmdbuf_used_pos]==0&&(global_cmdbuf_node[cmdbuf_used_pos]==NULL ))
    {
      cmdbuf_used[cmdbuf_used_pos]=1;
      cmdbuf_used_residual -=1;
      new_cmdbuf_addr->virtualAddress=vcmd_buf_mem_pool.virtualAddress + cmdbuf_used_pos*CMDBUF_MAX_SIZE/4;
      new_cmdbuf_addr->busAddress=vcmd_buf_mem_pool.busAddress + cmdbuf_used_pos*CMDBUF_MAX_SIZE;
      new_cmdbuf_addr->mmu_bus_address=vcmd_buf_mem_pool.mmu_bus_address + cmdbuf_used_pos*CMDBUF_MAX_SIZE;
      new_cmdbuf_addr->size=CMDBUF_MAX_SIZE;
      new_cmdbuf_addr->cmdbuf_id = cmdbuf_used_pos;
      new_status_cmdbuf_addr->virtualAddress=vcmd_status_buf_mem_pool.virtualAddress + cmdbuf_used_pos*CMDBUF_MAX_SIZE/4;
      new_status_cmdbuf_addr->busAddress=vcmd_status_buf_mem_pool.busAddress + cmdbuf_used_pos*CMDBUF_MAX_SIZE;
      new_status_cmdbuf_addr->mmu_bus_address=vcmd_status_buf_mem_pool.mmu_bus_address + cmdbuf_used_pos*CMDBUF_MAX_SIZE;
      new_status_cmdbuf_addr->size=CMDBUF_MAX_SIZE;
      new_status_cmdbuf_addr->cmdbuf_id = cmdbuf_used_pos;
      global_cmdbuf_node[cmdbuf_used_pos]=(void*)0x55555555; //temp set it,for another thread not hit  cmdbuf_used[x] set but global_cmdbuf_node[x] is null
      cmdbuf_used_pos++;
      if(cmdbuf_used_pos>=TOTAL_DISCRETE_CMDBUF_NUM)
        cmdbuf_used_pos=0;
      spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
      return 1;
    }
    else
    {
    #ifdef DEBUG_CMDBUF_ALLOC
      if(cmdbuf_used[cmdbuf_used_pos]==0 && (global_cmdbuf_node[cmdbuf_used_pos]!=NULL ) && (cmdbuf_node_unexpected[cmdbuf_used_pos] == 0x55))
      {
        pr_warn("vc8000e:unexpected cmdbuf_used[%d] is 0,but global_cmdbuf_node[%d] != NULL\n",
          cmdbuf_used_pos,cmdbuf_used_pos);
        cmdbuf_node_unexpected[cmdbuf_used_pos] = 0x55;
      }
      else if(cmdbuf_used[cmdbuf_used_pos]!=0 && (global_cmdbuf_node[cmdbuf_used_pos] == NULL ) && (cmdbuf_node_unexpected[cmdbuf_used_pos] == 0x55))
      {
        pr_warn("vc8000e:unexpected cmdbuf_used[%d] != 0,but global_cmdbuf_node[%d] is NULL\n",
          cmdbuf_used_pos,cmdbuf_used_pos);
        cmdbuf_node_unexpected[cmdbuf_used_pos] = 0x55;
      }
    #endif
      cmdbuf_used_pos++;
      if(cmdbuf_used_pos>=TOTAL_DISCRETE_CMDBUF_NUM)
        cmdbuf_used_pos=0;

      if(cmdbuf_used_pos_start == cmdbuf_used_pos)  //searched all,not find one,should return;
        break;
    }
  }
  spin_unlock_irqrestore(&vcmd_cmdbuf_alloc_lock, flags);
  pr_err("%s:vc8000e: no cmdbuf found,cmdbuf_used_residual %d\n",__func__,cmdbuf_used_residual);
  return 0;
}

static bi_list_node* get_cmdbuf_node_in_list_by_addr(size_t  cmdbuf_addr,bi_list* list)
{
  bi_list_node* new_cmdbuf_node=NULL;
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  new_cmdbuf_node=list->head;
  while(1)
  {
    if(new_cmdbuf_node==NULL)
      return NULL;
    cmdbuf_obj=(struct cmdbuf_obj*)new_cmdbuf_node->data;
    if(((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr) <=cmdbuf_addr)&&(((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr+cmdbuf_obj->cmdbuf_size) >cmdbuf_addr)) )
    {
      return new_cmdbuf_node;
    }
    new_cmdbuf_node=new_cmdbuf_node->next;
  }
  return NULL;
}

static int wait_abort_rdy(struct hantrovcmd_dev*dev)
{
  return dev->working_state == WORKING_STATE_IDLE;
}
static int select_vcmd(bi_list_node* new_cmdbuf_node)
{
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  bi_list_node* curr_cmdbuf_node=NULL;
  bi_list* list=NULL;
  struct hantrovcmd_dev*dev=NULL;
  struct hantrovcmd_dev*smallest_dev=NULL;
  u32 executing_time=0xffff;
  int counter=0;
  unsigned long flags=0;
  u32 hw_rdy_cmdbuf_num=0;
  size_t exe_cmdbuf_addr=0;
  struct cmdbuf_obj* cmdbuf_obj_temp=NULL;
  u32 cmdbuf_id=0;
  cmdbuf_obj=(struct cmdbuf_obj*)new_cmdbuf_node->data;
  //there is an empty vcmd to be used
  while(1)
  {
    dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
    list=&dev->list_manager;
    spin_lock_irqsave(dev->spinlock, flags);
    if( list->tail==NULL)
    {
      bi_list_insert_node_tail(list,new_cmdbuf_node);
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      cmdbuf_obj->core_id = dev->core_id;
      return 0;
    }
    else
    {
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      counter++;
    }
    if(counter>=vcmd_type_core_num[cmdbuf_obj->module_type])
      break;
  }
  //there is a vcmd which tail node -> cmdbuf_run_done == 1. It means this vcmd has nothing to do, so we select it.
  counter =0;
  while(1)
  {
    dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
    list=&dev->list_manager;
    spin_lock_irqsave(dev->spinlock, flags);
    curr_cmdbuf_node = list->tail;
    if(curr_cmdbuf_node == NULL)
    {
      bi_list_insert_node_tail(list,new_cmdbuf_node);
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      cmdbuf_obj->core_id = dev->core_id;
      return 0;
    }
    cmdbuf_obj_temp =(struct cmdbuf_obj*) curr_cmdbuf_node->data;
    if(cmdbuf_obj_temp->cmdbuf_run_done ==1)
    {
      bi_list_insert_node_tail(list,new_cmdbuf_node);
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      cmdbuf_obj->core_id = dev->core_id;
      return 0;
    }
    else
    {
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      counter++;
    }
    if(counter>=vcmd_type_core_num[cmdbuf_obj->module_type])
      break;
  }

  //another case, tail = executing node, and vcmd=pend state (finish but not generate interrupt)
  counter =0;
  while(1)
  {
    dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
    list=&dev->list_manager;
    //read executing cmdbuf address
    if(dev->hw_version_id <= HW_ID_1_0_C )
      hw_rdy_cmdbuf_num = vcmd_get_register_value((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_EXE_CMDBUF_COUNT);
    else
    {
      hw_rdy_cmdbuf_num = *(dev->vcmd_reg_mem_virtualAddress+VCMD_EXE_CMDBUF_COUNT);
      if(hw_rdy_cmdbuf_num!=dev->sw_cmdbuf_rdy_num)
        hw_rdy_cmdbuf_num += 1;
    }
    spin_lock_irqsave(dev->spinlock, flags);
    curr_cmdbuf_node = list->tail;
    if(curr_cmdbuf_node == NULL)
    {
      bi_list_insert_node_tail(list,new_cmdbuf_node);
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      cmdbuf_obj->core_id = dev->core_id;
      return 0;
    }

    if((dev->sw_cmdbuf_rdy_num ==hw_rdy_cmdbuf_num))
    {
      bi_list_insert_node_tail(list,new_cmdbuf_node);
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      cmdbuf_obj->core_id = dev->core_id;
      return 0;
    }
    else
    {
      spin_unlock_irqrestore(dev->spinlock, flags);
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      counter++;
    }
    if(counter>=vcmd_type_core_num[cmdbuf_obj->module_type])
      break;
  }



  //there is no idle vcmd,if low priority,calculate exe time, select the least one.
  // or if high priority, calculate the exe time, select the least one and abort it.
  if(cmdbuf_obj->priority==CMDBUF_PRIORITY_NORMAL)
  {

    counter =0;
    //calculate total execute time of all devices
    while(1)
    {
      dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
      //read executing cmdbuf address
      if(dev->hw_version_id <= HW_ID_1_0_C )
      {
        exe_cmdbuf_addr = VCMDGetAddrRegisterValue((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR);
        list=&dev->list_manager;
        spin_lock_irqsave(dev->spinlock, flags);
        //get the executing cmdbuf node.
        curr_cmdbuf_node=get_cmdbuf_node_in_list_by_addr(exe_cmdbuf_addr,list);

        //calculate total execute time of this device
        dev->total_exe_time=calculate_executing_time_after_node(curr_cmdbuf_node);
        spin_unlock_irqrestore(dev->spinlock, flags);
      }
      else
      {
        //cmdbuf_id = vcmd_get_register_value((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID);
        cmdbuf_id = *(dev->vcmd_reg_mem_virtualAddress+EXECUTING_CMDBUF_ID_ADDR+1);
        spin_lock_irqsave(dev->spinlock, flags);
        if(cmdbuf_id>=TOTAL_DISCRETE_CMDBUF_NUM||cmdbuf_id == 0)
        {
          pr_err("cmdbuf_id greater than the ceiling !!\n");
          spin_unlock_irqrestore(dev->spinlock, flags);
          return -1;
        }
        //get the executing cmdbuf node.
        curr_cmdbuf_node=global_cmdbuf_node[cmdbuf_id];
        if(curr_cmdbuf_node==NULL)
        {
          list=&dev->list_manager;
          curr_cmdbuf_node = list->head;
          while(1)
          {
            if(curr_cmdbuf_node == NULL)
              break;
            cmdbuf_obj_temp =(struct cmdbuf_obj*) curr_cmdbuf_node->data;
            if(cmdbuf_obj_temp->cmdbuf_data_linked&&cmdbuf_obj_temp->cmdbuf_run_done==0)
              break;
            curr_cmdbuf_node = curr_cmdbuf_node->next;
          }

        }

        //calculate total execute time of this device
        dev->total_exe_time=calculate_executing_time_after_node(curr_cmdbuf_node);
        spin_unlock_irqrestore(dev->spinlock, flags);

      }
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      counter++;
      if(counter>=vcmd_type_core_num[cmdbuf_obj->module_type])
        break;
    }
    //find the device with the least total_exe_time.
    counter =0;
    executing_time=0xffffffff;
    while(1)
    {
      dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
      if(dev->total_exe_time <= executing_time)
      {
        executing_time = dev->total_exe_time;
        smallest_dev = dev;
      }
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      counter++;
      if(counter>=vcmd_type_core_num[cmdbuf_obj->module_type])
        break;
    }
    //insert list
    list = &smallest_dev->list_manager;
    spin_lock_irqsave(smallest_dev->spinlock, flags);
    bi_list_insert_node_tail(list,new_cmdbuf_node);
    spin_unlock_irqrestore(smallest_dev->spinlock, flags);
    cmdbuf_obj->core_id = smallest_dev->core_id;
    return 0;
  }
  else
  {
    //CMDBUF_PRIORITY_HIGH
    counter =0;
    //calculate total execute time of all devices
    while(1)
    {
      dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
      if(dev->hw_version_id <= HW_ID_1_0_C )
      {
        //read executing cmdbuf address
        exe_cmdbuf_addr = VCMDGetAddrRegisterValue((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR);
        list=&dev->list_manager;
        spin_lock_irqsave(dev->spinlock, flags);
        //get the executing cmdbuf node.
        curr_cmdbuf_node=get_cmdbuf_node_in_list_by_addr(exe_cmdbuf_addr,list);

        //calculate total execute time of this device
        dev->total_exe_time=calculate_executing_time_after_node_high_priority(curr_cmdbuf_node);
        spin_unlock_irqrestore(dev->spinlock, flags);
      }
      else
      {
        //cmdbuf_id = vcmd_get_register_value((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID);
        cmdbuf_id = *(dev->vcmd_reg_mem_virtualAddress+EXECUTING_CMDBUF_ID_ADDR);
        spin_lock_irqsave(dev->spinlock, flags);
        if(cmdbuf_id>=TOTAL_DISCRETE_CMDBUF_NUM||cmdbuf_id == 0)
        {
          pr_err("cmdbuf_id greater than the ceiling !!\n");
          spin_unlock_irqrestore(dev->spinlock, flags);
          return -1;
        }
        //get the executing cmdbuf node.
        curr_cmdbuf_node=global_cmdbuf_node[cmdbuf_id];
        if(curr_cmdbuf_node==NULL)
        {
          list=&dev->list_manager;
          curr_cmdbuf_node = list->head;
          while(1)
          {
            if(curr_cmdbuf_node == NULL)
              break;
            cmdbuf_obj_temp =(struct cmdbuf_obj*) curr_cmdbuf_node->data;
            if(cmdbuf_obj_temp->cmdbuf_data_linked&&cmdbuf_obj_temp->cmdbuf_run_done==0)
              break;
            curr_cmdbuf_node = curr_cmdbuf_node->next;
          }
        }

        //calculate total execute time of this device
        dev->total_exe_time=calculate_executing_time_after_node(curr_cmdbuf_node);
        spin_unlock_irqrestore(dev->spinlock, flags);
      }
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      counter++;
      if(counter>=vcmd_type_core_num[cmdbuf_obj->module_type])
        break;
    }
    //find the smallest device.
    counter =0;
    executing_time=0xffffffff;
    while(1)
    {
      dev = vcmd_manager[cmdbuf_obj->module_type][vcmd_position[cmdbuf_obj->module_type]];
      if(dev->total_exe_time <= executing_time)
      {
        executing_time = dev->total_exe_time;
        smallest_dev = dev;
      }
      vcmd_position[cmdbuf_obj->module_type]++;
      if(vcmd_position[cmdbuf_obj->module_type]>=vcmd_type_core_num[cmdbuf_obj->module_type])
        vcmd_position[cmdbuf_obj->module_type]=0;
      counter++;
      if(counter>=vcmd_type_core_num[cmdbuf_obj->module_type])
        break;
    }
    //abort the vcmd and wait
    vcmd_write_register_value((const void *)smallest_dev->hwregs,smallest_dev->reg_mirror,HWIF_VCMD_START_TRIGGER,0);
    software_triger_abort = 1;
    if(wait_event_interruptible(*smallest_dev->wait_abort_queue, wait_abort_rdy(smallest_dev)) )
    {
      software_triger_abort = 0;
      return -ERESTARTSYS;
    }
    software_triger_abort = 0;
    //need to select inserting position again because hw maybe have run to the next node.
    //CMDBUF_PRIORITY_HIGH
    spin_lock_irqsave(smallest_dev->spinlock, flags);
    curr_cmdbuf_node = smallest_dev->list_manager.head;
    while(1)
    {
     //if list is empty or tail,insert to tail
     if(curr_cmdbuf_node == NULL)
      break;
     cmdbuf_obj_temp= (struct cmdbuf_obj*)curr_cmdbuf_node->data;
     //if find the first node which priority is normal, insert node prior to  the node
     if((cmdbuf_obj_temp->priority==CMDBUF_PRIORITY_NORMAL) && (cmdbuf_obj_temp->cmdbuf_run_done==0))
      break;
     curr_cmdbuf_node = curr_cmdbuf_node->next;
    }
    bi_list_insert_node_before(list,curr_cmdbuf_node,new_cmdbuf_node);
    cmdbuf_obj->core_id = smallest_dev->core_id;
    spin_unlock_irqrestore(smallest_dev->spinlock, flags);

    return 0;
  }
  return 0;
}
static int wait_process_resource_rdy(struct process_manager_obj* process_manager_obj )
{
  return process_manager_obj->total_exe_time<=PROCESS_MAX_SUM_OF_IMAGE_SIZE;
}

static long reserve_cmdbuf(struct file *filp,struct exchange_parameter* input_para)
{
  bi_list_node* new_cmdbuf_node=NULL;
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  bi_list_node* process_manager_node=NULL;
  struct process_manager_obj* process_manager_obj=NULL;
  unsigned long flags;
  input_para->cmdbuf_id = 0;
  if(input_para->cmdbuf_size>CMDBUF_MAX_SIZE)
  {
    return -1;
  }
  spin_lock_irqsave(&vcmd_process_manager_lock, flags);
  process_manager_node = global_process_manager.head;
  while(1)
  {
    if(process_manager_node == NULL)
    {
      //should not happen
      pr_err("hantrovcmd: ERROR process_manager_node !!\n");
      spin_unlock_irqrestore(&vcmd_process_manager_lock, flags);
      return -1;
    }
    process_manager_obj = (struct process_manager_obj*)process_manager_node->data;
    if (filp==process_manager_obj->filp)
    {
      break;
    }
    process_manager_node = process_manager_node->next;
  }
  spin_unlock_irqrestore(&vcmd_process_manager_lock, flags);
  spin_lock_irqsave(&process_manager_obj->spinlock, flags);
  process_manager_obj->total_exe_time += input_para->executing_time;
  spin_unlock_irqrestore(&process_manager_obj->spinlock, flags);
  if(!wait_event_interruptible_timeout(process_manager_obj->wait_queue, wait_process_resource_rdy(process_manager_obj),
       msecs_to_jiffies(500) ))
  {
      pr_err("vc8000e: wait_process_resource_rdy timeout! total_exe_time %lld\n",process_manager_obj->total_exe_time);
      return -1;
  }


  new_cmdbuf_node=create_cmdbuf_node();
  if(new_cmdbuf_node==NULL)
    return -1;

  cmdbuf_obj = (struct cmdbuf_obj* )new_cmdbuf_node->data;
  cmdbuf_obj->module_type = input_para->module_type;
  cmdbuf_obj->priority = input_para->priority;
  cmdbuf_obj->executing_time = input_para->executing_time;
  cmdbuf_obj->cmdbuf_size = CMDBUF_MAX_SIZE;
  input_para->cmdbuf_size =CMDBUF_MAX_SIZE;
  cmdbuf_obj->filp = filp;
  cmdbuf_obj->process_manager_obj =process_manager_obj;

  input_para->cmdbuf_id=cmdbuf_obj->cmdbuf_id;
  global_cmdbuf_node[input_para->cmdbuf_id] = new_cmdbuf_node;

  return 0;
}

static long release_cmdbuf(struct file *filp,u16 cmdbuf_id)
{
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  bi_list_node* last_cmdbuf_node=NULL;
  bi_list_node* new_cmdbuf_node=NULL;
  bi_list* list=NULL;
  u32 module_type;


  unsigned long flags;
  struct hantrovcmd_dev* dev=NULL;
  /*get cmdbuf object according to cmdbuf_id*/
  new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
  if(new_cmdbuf_node==NULL)
  {
    //should not happen
    pr_err("hantrovcmd: ERROR cmdbuf_id !!\n");
    return -1;
  }
  cmdbuf_obj=(struct cmdbuf_obj*)new_cmdbuf_node->data;
  if(cmdbuf_obj->filp!=filp)
  {
    //should not happen
    pr_err("hantrovcmd: ERROR cmdbuf_id !!\n");
    return -1;
  }
  module_type = cmdbuf_obj->module_type;
  //TODO
  if (down_interruptible(&vcmd_reserve_cmdbuf_sem[module_type]))
      return -ERESTARTSYS;
  dev = &hantrovcmd_data[cmdbuf_obj->core_id];

  //spin_lock_irqsave(dev->spinlock, flags);
  list=&dev->list_manager;
  cmdbuf_obj->cmdbuf_need_remove=1;
  last_cmdbuf_node = new_cmdbuf_node->previous;
  while(1)
  {
    //remove current node
    cmdbuf_obj=(struct cmdbuf_obj*)new_cmdbuf_node->data;
    if(cmdbuf_obj->cmdbuf_need_remove==1)
    {
      new_cmdbuf_node=remove_cmdbuf_node_from_list(list,new_cmdbuf_node);
      if(new_cmdbuf_node)
      {
        //free node
        global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
        if(cmdbuf_obj->process_manager_obj)
        {
          spin_lock_irqsave(&cmdbuf_obj->process_manager_obj->spinlock, flags);
          cmdbuf_obj->process_manager_obj->total_exe_time -= cmdbuf_obj->executing_time;
          spin_unlock_irqrestore(&cmdbuf_obj->process_manager_obj->spinlock, flags);
          wake_up_all(&cmdbuf_obj->process_manager_obj->wait_queue);
        }
        free_cmdbuf_node(new_cmdbuf_node);

      }
    }
    if(last_cmdbuf_node==NULL)
      break;
    new_cmdbuf_node=last_cmdbuf_node;
    last_cmdbuf_node=new_cmdbuf_node->previous;
  }
  //spin_unlock_irqrestore(dev->spinlock, flags);
  up(&vcmd_reserve_cmdbuf_sem[module_type]);
  return 0;
}
static long release_cmdbuf_node(bi_list* list,bi_list_node*cmdbuf_node)
{
  bi_list_node* new_cmdbuf_node=NULL;
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  /*get cmdbuf object according to cmdbuf_id*/
  new_cmdbuf_node=cmdbuf_node;
  if(new_cmdbuf_node==NULL)
    return -1;
  //remove node from list
  new_cmdbuf_node=remove_cmdbuf_node_from_list(list,new_cmdbuf_node);
  if(new_cmdbuf_node)
  {
    //free node
    cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
    global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
    free_cmdbuf_node(new_cmdbuf_node);
    return 0;
  }
  return 1;
}

static long release_cmdbuf_node_cleanup(bi_list* list)
{
  bi_list_node* new_cmdbuf_node=NULL;
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  while(1)
  {
    new_cmdbuf_node=list->head;
    if(new_cmdbuf_node==NULL)
      return 0;
    //remove node from list
    bi_list_remove_node(list,new_cmdbuf_node);
    //free node
    cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
    global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
    free_cmdbuf_node(new_cmdbuf_node);
  }
  return 0;
}



static bi_list_node* find_last_linked_cmdbuf(bi_list_node* current_node)
{
  bi_list_node* new_cmdbuf_node=current_node;
  bi_list_node* last_cmdbuf_node;
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  if(current_node==NULL)
    return NULL;
  last_cmdbuf_node = new_cmdbuf_node;
  new_cmdbuf_node = new_cmdbuf_node->previous;
  while(1)
  {
    if(new_cmdbuf_node==NULL)
      return last_cmdbuf_node;
    cmdbuf_obj=(struct cmdbuf_obj*)new_cmdbuf_node->data;
    if(cmdbuf_obj->cmdbuf_data_linked)
    {
      return new_cmdbuf_node;
    }
    last_cmdbuf_node = new_cmdbuf_node;
    new_cmdbuf_node = new_cmdbuf_node->previous;
  }
  return NULL;
}
static long link_and_run_cmdbuf(struct file *filp,struct exchange_parameter* input_para)
{
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  bi_list_node* new_cmdbuf_node=NULL;
  bi_list_node* last_cmdbuf_node;
  u32* jmp_addr=NULL;
  u32 opCode;
  u32 tempOpcode;
  u32 record_last_cmdbuf_rdy_num;
  struct hantrovcmd_dev* dev=NULL;
  unsigned long flags;
  int return_value;
  u16 cmdbuf_id=input_para->cmdbuf_id;


  new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
  if(new_cmdbuf_node==NULL)
  {
    //should not happen
    pr_err("hantrovcmd: ERROR cmdbuf_id !!\n");
    return -1;
  }
  cmdbuf_obj=(struct cmdbuf_obj*)new_cmdbuf_node->data;
  if(cmdbuf_obj->filp!=filp)
  {
    //should not happen
    pr_err("hantrovcmd: ERROR cmdbuf_id !!\n");
    return -1;
  }
  cmdbuf_obj->cmdbuf_data_loaded=1;
  cmdbuf_obj->cmdbuf_size=input_para->cmdbuf_size;
#ifdef VCMD_DEBUG_INTERNAL
  {
     u32 i;
     pr_info("vcmd link, current cmdbuf content\n");
     for(i=0;i<cmdbuf_obj->cmdbuf_size/4;i++)
     {
       pr_info("current cmdbuf data %d =0x%x\n",i,*(cmdbuf_obj->cmdbuf_virtualAddress+i));
     }
  }
#endif
  //test nop and end opcode, then assign value.
  cmdbuf_obj->has_end_cmdbuf=0; //0: has jmp opcode,1 has end code
  cmdbuf_obj->no_normal_int_cmdbuf=0; //0: interrupt when JMP,1 not interrupt when JMP
  jmp_addr = cmdbuf_obj->cmdbuf_virtualAddress + (cmdbuf_obj->cmdbuf_size/4);
  opCode=tempOpcode = *(jmp_addr-4);
  opCode >>=27;
  opCode <<=27;
  //we can't identify END opcode or JMP opcode, so we don't support END opcode in control sw and driver.
  if(opCode == OPCODE_JMP)
  {
    //jmp
    opCode=tempOpcode;
    opCode &=0x02000000;
    if(opCode == JMP_IE_1)
    {
      cmdbuf_obj->no_normal_int_cmdbuf=0;
    }
    else
    {
      cmdbuf_obj->no_normal_int_cmdbuf=1;
    }
  }
  else
  {
    //not support other opcode
    return -1;
  }

  if (down_interruptible(&vcmd_reserve_cmdbuf_sem[cmdbuf_obj->module_type]))
      return -ERESTARTSYS;

  venc_vcmd_profile.cur_submit_vcmd_id = input_para->cmdbuf_id;
  encoder_devfreq_record_busy( encoder_get_devfreq_priv_data() );

  return_value=select_vcmd(new_cmdbuf_node);
  if(return_value)
    return return_value;

  dev = &hantrovcmd_data[cmdbuf_obj->core_id];
  input_para->core_id = cmdbuf_obj->core_id;
  PDEBUG("filp=%px, VCMD Link    CMDBUF [%d] to core [%d]\n", (void *)filp, cmdbuf_id, input_para->core_id);
  //set ddr address for vcmd registers copy.
  if(dev->hw_version_id > HW_ID_1_0_C )
  {
    //read vcmd executing register into ddr memory.
    //now core id is got and output ddr address of vcmd register can be filled in.
    //each core has its own fixed output ddr address of vcmd registers.
    jmp_addr = cmdbuf_obj->cmdbuf_virtualAddress;
    if (mmu_enable) {
      *(jmp_addr + 2) = 0;
      *(jmp_addr+1) = (u32)((dev->mmu_vcmd_reg_mem_busAddress + (EXECUTING_CMDBUF_ID_ADDR+1)*4));
    } else {
      if(sizeof(size_t) == 8) {
        *(jmp_addr + 2) = (u32)((u64)(dev->vcmd_reg_mem_busAddress + (EXECUTING_CMDBUF_ID_ADDR+1)*4)>>32);
      } else {
        *(jmp_addr + 2) = 0;
      }
      *(jmp_addr+1) = (u32)((dev->vcmd_reg_mem_busAddress + (EXECUTING_CMDBUF_ID_ADDR+1)*4));
    }

    jmp_addr = cmdbuf_obj->cmdbuf_virtualAddress + (cmdbuf_obj->cmdbuf_size/4);
    //read vcmd all registers into ddr memory.
    //now core id is got and output ddr address of vcmd registers can be filled in.
    //each core has its own fixed output ddr address of vcmd registers.
    if (mmu_enable) {
      if(sizeof(size_t) == 8) {
        *(jmp_addr-6) = 0;
      }
      *(jmp_addr-7) = (u32)(dev->mmu_vcmd_reg_mem_busAddress);
    } else {
      if(sizeof(size_t) == 8) {
        *(jmp_addr-6) = (u32)((u64)dev->vcmd_reg_mem_busAddress>>32);
      } else {
        *(jmp_addr-6) = 0;
      }
      *(jmp_addr-7) = (u32)(dev->vcmd_reg_mem_busAddress);
    }
  }
  //start to link and/or run
  spin_lock_irqsave(dev->spinlock, flags);
  last_cmdbuf_node = find_last_linked_cmdbuf(new_cmdbuf_node);
  record_last_cmdbuf_rdy_num=dev->sw_cmdbuf_rdy_num;
  vcmd_link_cmdbuf(dev,last_cmdbuf_node);
  dev->last_linked_cmdbuf_node = new_cmdbuf_node;  //record for resume when pm work
  if(dev->working_state==WORKING_STATE_IDLE)
  {
    //run
    while (last_cmdbuf_node &&
           ((struct cmdbuf_obj*)last_cmdbuf_node->data)->cmdbuf_run_done)
      last_cmdbuf_node = last_cmdbuf_node->next;

    if (last_cmdbuf_node && last_cmdbuf_node->data) {
      PDEBUG("vcmd start for cmdbuf id %d, cmdbuf_run_done = %d\n",
            ((struct cmdbuf_obj*)last_cmdbuf_node->data)->cmdbuf_id,
            ((struct cmdbuf_obj*)last_cmdbuf_node->data)->cmdbuf_run_done);
    }
    vcmd_start(dev,last_cmdbuf_node);
  }
  else
  {
    //just update cmdbuf ready number
    if(record_last_cmdbuf_rdy_num!=dev->sw_cmdbuf_rdy_num)
      vcmd_write_register_value((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_RDY_CMDBUF_COUNT,dev->sw_cmdbuf_rdy_num);
  }
  spin_unlock_irqrestore(dev->spinlock, flags);

  up(&vcmd_reserve_cmdbuf_sem[cmdbuf_obj->module_type]);


  return 0;
}

/******************************************************************************/
static int check_cmdbuf_irq(struct hantrovcmd_dev* dev,struct cmdbuf_obj* cmdbuf_obj,u32 *irq_status_ret)
{

  int rdy = 0;
  unsigned long flags;
  spin_lock_irqsave(dev->spinlock, flags);
  if(cmdbuf_obj->cmdbuf_run_done)
  {
    rdy = 1;
    *irq_status_ret=cmdbuf_obj->executing_status;//need to decide how to assign this variable
  }
  spin_unlock_irqrestore(dev->spinlock, flags);
  return rdy;
}
#ifdef IRQ_SIMULATION
static void get_random_bytes(void *buf, int nbytes);
#if 0
void hantrovcmd_trigger_irq_0(struct timer_list* timer)
{
  PDEBUG("trigger core 0 irq\n");
  del_timer(timer);
  hantrovcmd_isr(0,(void *)&hantrovcmd_data[0]);
}
void hantrovcmd_trigger_irq_1(struct timer_list* timer)
{
  PDEBUG("trigger core 1 irq\n");
  del_timer(timer);
  hantrovcmd_isr(0,(void *)&hantrovcmd_data[1]);
}
#else
static void hantrovcmd_trigger_irq(struct timer_list *timer)
{

  u32 timer_id=0;
  u32 core_id=0;
  u32 i;
  for(i=0;i<10000;i++)
  {
     if(timer_reserve[i].timer==timer)
     {
       timer_id=timer_reserve[i].timer_id;
       core_id = timer_reserve[i].core_id;
       break;
     }
  }
  PDEBUG("trigger core 0 irq\n");
  hantrovcmd_isr(core_id,(void *)&hantrovcmd_data[core_id]);
  del_timer(timer);
  timer_reserve[timer_id].timer=NULL;
}

#endif
#endif

static unsigned int wait_cmdbuf_ready(struct file *filp,u16 cmdbuf_id,u32 *irq_status_ret)
{

  struct cmdbuf_obj* cmdbuf_obj=NULL;
  bi_list_node* new_cmdbuf_node=NULL;
  struct hantrovcmd_dev* dev=NULL;

  PDEBUG("wait_cmdbuf_ready\n");
  new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
  if(new_cmdbuf_node==NULL)
  {
    //should not happen
    pr_err("hantrovcmd: ERROR cmdbuf_id !!\n");
    return -1;
  }
  cmdbuf_obj=(struct cmdbuf_obj*)new_cmdbuf_node->data;
  if(cmdbuf_obj->filp!=filp)
  {
    //should not happen
    pr_err("hantrovcmd: ERROR cmdbuf_id !!\n");
    return -1;
  }
  dev = &hantrovcmd_data[cmdbuf_obj->core_id];
#ifdef IRQ_SIMULATION
  {
    u32 random_num;
    //get_random_bytes(&random_num, sizeof(u32));
    random_num = (u32)((u64)100*cmdbuf_obj->executing_time/(4096*2160)+50);
    PDEBUG("random_num=%d\n",random_num);
#if 0
    /*init a timer to trigger irq*/
    if (cmdbuf_obj->core_id==0)
    {
      //init_timer(&timer0);
      //timer0.function = hantrovcmd_trigger_irq_0;
      timer_setup(&timer0,hantrovcmd_trigger_irq_0,0);
      timer0.expires =  jiffies + random_num*HZ/10; //the expires time is 1s
      add_timer(&timer0);
    }

    if (cmdbuf_obj->core_id==1)
    {
      //init_timer(&timer1);
      //timer1.function = hantrovcmd_trigger_irq_1;
      timer_setup(&timer1,hantrovcmd_trigger_irq_1,0);
      timer1.expires =  jiffies + random_num*HZ/10; //the expires time is 1s
      add_timer(&timer1);
    }
#else
    {
        u32 i;
        struct timer_list *temp_timer=NULL;
        for(i=0;i<10000;i++)
        {
           if(timer_reserve[i].timer==NULL)
           {
             timer_reserve[i].timer_id=i;
             timer_reserve[i].core_id=cmdbuf_obj->core_id;
             temp_timer=timer_reserve[i].timer =&timer[i] ;
             break;
           }
        }
        //if (cmdbuf_obj->core_id==0)
        {
          //init_timer(&timer0);
          //timer0.function = hantrovcmd_trigger_irq_0;
          timer_setup(temp_timer,hantrovcmd_trigger_irq,0);
          temp_timer->expires =  jiffies + random_num*HZ/10; //the expires time is 1s
          add_timer(temp_timer);
        }
    }
#endif
  }
#endif
  /*In suspend, it got a signal from "Freezing user space processes" period,
  * wait_event_interruptible() will be interrupted,return to user space a IO error.
  * So, here changed to wait_event_timeout().
  */
  if(!wait_event_timeout(*dev->wait_queue,
            check_cmdbuf_irq(dev,cmdbuf_obj,irq_status_ret), msecs_to_jiffies(500) )
    )
  {
      pr_err("vc8000e:vcmd_wait_queue_0 timeout cmdbuf[%d]\n",cmdbuf_id);
      //abort the vcmd
      vcmd_write_register_value((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_START_TRIGGER,0);
      return -ETIME;
  }
  return 0;
}

bool hantrovcmd_devfreq_check_state(void)
{
    struct hantrovcmd_dev *dev = hantrovcmd_data;
    u32 core_id = 0;
    u32 state = 0;
    for (core_id = 0;core_id < total_vcmd_core_num; core_id++) {
      state = vcmd_get_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_WORK_STATE);
      if((state != 0) && (state != 3) )  //HW state pend or idle
          return false;
    }
    return true;
}

static long hantrovcmd_ioctl(struct file *filp,
                          unsigned int cmd, unsigned long arg)
{
    int err = 0;
    struct device *dev = &hantrovcmd_data->pdev->dev;
    struct process_manager_obj* process_manager_obj=NULL;

    PDEBUG("ioctl cmd 0x%08x\n", cmd);
    /*
     * extract the type and number bitfields, and don't encode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if(_IOC_TYPE(cmd) != HANTRO_IOC_MAGIC
#ifdef HANTROMMU_SUPPORT
      && _IOC_TYPE(cmd) != HANTRO_IOC_MMU
#endif
    )
        return -ENOTTY;
    if((_IOC_TYPE(cmd) == HANTRO_IOC_MAGIC &&
        _IOC_NR(cmd) > HANTRO_IOC_MAXNR)
#ifdef HANTROMMU_SUPPORT
       ||(_IOC_TYPE(cmd) == HANTRO_IOC_MMU &&
       _IOC_NR(cmd) > HANTRO_IOC_MMU_MAXNR)
#endif
        )
        return -ENOTTY;

    /*
     * the direction is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. `Type' is user-oriented, while
     * access_ok is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
    if(_IOC_DIR(cmd) & _IOC_READ)
#if KERNEL_VERSION(5,0,0) <= LINUX_VERSION_CODE
        err = !access_ok((void *) arg, _IOC_SIZE(cmd));
#else
        err = !access_ok(VERIFY_WRITE, (void *) arg, _IOC_SIZE(cmd));
#endif
    else if(_IOC_DIR(cmd) & _IOC_WRITE)
#if KERNEL_VERSION(5,0,0) <= LINUX_VERSION_CODE
        err = !access_ok((void *) arg, _IOC_SIZE(cmd));
#else
        err = !access_ok(VERIFY_READ, (void *) arg, _IOC_SIZE(cmd));
#endif
    if(err)
        return -EFAULT;

    process_manager_obj = (struct process_manager_obj*)filp->private_data;

    switch (cmd)
    {
    case HANTRO_IOCH_GET_VCMD_ENABLE:
      {
        __put_user(1, (unsigned long *) arg);
        break;
      }    
    case HANTRO_IOCH_GET_CMDBUF_PARAMETER:
      {
       struct cmdbuf_mem_parameter local_cmdbuf_mem_data;
       PDEBUG(" VCMD GET_CMDBUF_PARAMETER\n");
       local_cmdbuf_mem_data.cmdbuf_unit_size = CMDBUF_MAX_SIZE;
       local_cmdbuf_mem_data.status_cmdbuf_unit_size = CMDBUF_MAX_SIZE;
       local_cmdbuf_mem_data.cmdbuf_total_size = CMDBUF_POOL_TOTAL_SIZE;
       local_cmdbuf_mem_data.status_cmdbuf_total_size = CMDBUF_POOL_TOTAL_SIZE;
       local_cmdbuf_mem_data.phy_status_cmdbuf_addr = vcmd_status_buf_mem_pool.busAddress;
       local_cmdbuf_mem_data.phy_cmdbuf_addr = vcmd_buf_mem_pool.busAddress;
       if (mmu_enable) {
         local_cmdbuf_mem_data.mmu_phy_status_cmdbuf_addr = vcmd_status_buf_mem_pool.mmu_bus_address;
         local_cmdbuf_mem_data.mmu_phy_cmdbuf_addr = vcmd_buf_mem_pool.mmu_bus_address;
       } else {
         local_cmdbuf_mem_data.mmu_phy_status_cmdbuf_addr = 0;
         local_cmdbuf_mem_data.mmu_phy_cmdbuf_addr = 0;
       }
       local_cmdbuf_mem_data.base_ddr_addr = base_ddr_addr;
       copy_to_user((struct cmdbuf_mem_parameter*)arg,&local_cmdbuf_mem_data,sizeof(struct cmdbuf_mem_parameter));
       break;
      }
    case HANTRO_IOCH_GET_VCMD_PARAMETER:
      {
       struct config_parameter input_para;
       PDEBUG(" VCMD get vcmd config parameter \n");
       copy_from_user(&input_para,(struct config_parameter*)arg,sizeof(struct config_parameter));
       if(vcmd_type_core_num[input_para.module_type])
       {
         input_para.submodule_main_addr = vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_main_addr;
         input_para.submodule_dec400_addr = vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_dec400_addr;
         input_para.submodule_L2Cache_addr = vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_L2Cache_addr;
         input_para.submodule_MMU_addr[0] = vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_MMU_addr[0];
         input_para.submodule_MMU_addr[1] = vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_MMU_addr[1];
         input_para.submodule_axife_addr[0] = vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_axife_addr[0];
         input_para.submodule_axife_addr[1] = vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_axife_addr[1];
         input_para.config_status_cmdbuf_id = vcmd_manager[input_para.module_type][0]->status_cmdbuf_id;
         input_para.vcmd_hw_version_id = vcmd_manager[input_para.module_type][0]->hw_version_id;
         input_para.vcmd_core_num = vcmd_type_core_num[input_para.module_type];
       }
       else
       {
         input_para.submodule_main_addr = 0xffff;
         input_para.submodule_dec400_addr = 0xffff;
         input_para.submodule_L2Cache_addr = 0xffff;
         input_para.submodule_MMU_addr[0] = 0xffff;
         input_para.submodule_MMU_addr[1] = 0xffff;
         input_para.submodule_axife_addr[0] = 0xffff;
         input_para.submodule_axife_addr[1] = 0xffff;
         input_para.config_status_cmdbuf_id = 0;
         input_para.vcmd_core_num = 0;
         input_para.vcmd_hw_version_id =HW_ID_1_0_C;
       }
       copy_to_user((struct config_parameter*)arg,&input_para,sizeof(struct config_parameter));
       break;
      }
    case HANTRO_IOCH_RESERVE_CMDBUF:
      {
       int ret;
       struct exchange_parameter input_para;
       copy_from_user(&input_para,(struct exchange_parameter*)arg,sizeof(struct exchange_parameter));
       ret = reserve_cmdbuf(filp,&input_para);
       if (ret == 0)
        copy_to_user((struct exchange_parameter*)arg,&input_para,sizeof(struct exchange_parameter));
       PDEBUG("filp=%px, VCMD Reserve CMDBUF [%d] remain %d \n", (void *)filp, input_para.cmdbuf_id,cmdbuf_used_residual);
       return ret;
      }

    case HANTRO_IOCH_LINK_RUN_CMDBUF:
      {
        struct exchange_parameter input_para;
        long retVal;
        copy_from_user(&input_para,(struct exchange_parameter*)arg,sizeof(struct exchange_parameter));

        PDEBUG("filp=%px,VCMD link and run cmdbuf,[%d] \n",(void *)filp, input_para.cmdbuf_id);
        pm_runtime_resume_and_get(dev);
        if (process_manager_obj)
          process_manager_obj->pm_count++;
        retVal = link_and_run_cmdbuf(filp,&input_para);
        copy_to_user((struct exchange_parameter*)arg,&input_para,sizeof(struct exchange_parameter));
        return retVal;
        break;
      }

    case HANTRO_IOCH_WAIT_CMDBUF:
      {

        u16 cmdbuf_id;
        unsigned int tmp;
        u32 irq_status_ret=0;
        __get_user(cmdbuf_id, (u16*)arg);
        /*high 16 bits are core id, low 16 bits are cmdbuf_id*/

        PDEBUG("filp=%px,VCMD wait for CMDBUF finishing. %d,\n",filp,cmdbuf_id);

        //TODO
        tmp = wait_cmdbuf_ready(filp,cmdbuf_id,&irq_status_ret);
        PDEBUG("filp=%px,VCMD wait for CMDBUF finished. %d,\n",filp,cmdbuf_id);
        cmdbuf_id=(u16)irq_status_ret;
        if (tmp==0)
        {
          __put_user(cmdbuf_id, (u16 *)arg);
          return tmp;//return core_id
        }
        else
        {
          return -1;
        }

        break;
      }
    case HANTRO_IOCH_RELEASE_CMDBUF:
      {
        u16 cmdbuf_id;
        __get_user(cmdbuf_id, (u16*)arg);
        /*16 bits are cmdbuf_id*/

        pm_runtime_mark_last_busy(dev);
        pm_runtime_put_autosuspend(dev);
        if (process_manager_obj)
          process_manager_obj->pm_count--;
        release_cmdbuf(filp,cmdbuf_id);
        PDEBUG("filp=%px,VCMD release CMDBUF ,%d,remain %d \n",filp,cmdbuf_id,cmdbuf_used_residual);
        return 0;
        break;
      }
    case HANTRO_IOCH_POLLING_CMDBUF:
    {
        u16 core_id;
        __get_user(core_id, (u16*)arg);
        /*16 bits are cmdbuf_id*/
        if(core_id>=total_vcmd_core_num)
          return -1;
        hantrovcmd_isr(core_id,&hantrovcmd_data[core_id]);
        return 0;
        break;
      }
    default:
      {
#ifdef HANTROMMU_SUPPORT
        if(_IOC_TYPE(cmd) == HANTRO_IOC_MMU)
        {
          pm_runtime_resume_and_get(dev);
          long retval = MMUIoctl(cmd, filp, arg, mmu_hwregs);
          pm_runtime_mark_last_busy(dev);
          pm_runtime_put_autosuspend(dev);
          return retval;
        }
#endif
      }
    }
    return 0;
}
/**********************************************************************************************************\
*process manager object management
\***********************************************************************************************************/
static struct process_manager_obj* create_process_manager_obj(void)
{
  struct process_manager_obj* process_manager_obj=NULL;
  process_manager_obj=vmalloc(sizeof(struct process_manager_obj));
  if(process_manager_obj==NULL)
  {
    PDEBUG ("%s\n","vmalloc for process_manager_obj fail!");
    return process_manager_obj;
  }
  memset(process_manager_obj,0,sizeof(struct process_manager_obj));
  return process_manager_obj;
}

static void free_process_manager_obj( struct process_manager_obj* process_manager_obj)
{
  if(process_manager_obj==NULL)
  {
    PDEBUG ("%s\n","free_process_manager_obj NULL");
    return;
  }
  //free current cmdbuf_obj
  vfree(process_manager_obj);
  return;
}

static bi_list_node* create_process_manager_node(void)
{
  bi_list_node* current_node=NULL;
  struct process_manager_obj* process_manager_obj=NULL;

  process_manager_obj=create_process_manager_obj();
  if(process_manager_obj==NULL)
  {
    PDEBUG ("%s\n","create_process_manager_obj fail!");
    return NULL;
  }
  process_manager_obj->total_exe_time = 0;
  process_manager_obj->pm_count = 0;
  spin_lock_init(&process_manager_obj->spinlock);
  init_waitqueue_head(&process_manager_obj->wait_queue);
  current_node=bi_list_create_node();
  if(current_node==NULL)
  {
    PDEBUG ("%s\n","bi_list_create_node fail!");
    free_process_manager_obj(process_manager_obj);
    return NULL;
  }
  current_node->data = (void*)process_manager_obj;
  return current_node;
}
static void free_process_manager_node(bi_list_node* process_node)
{
  struct process_manager_obj* process_manager_obj=NULL;
  if(process_node==NULL)
  {
    PDEBUG ("%s\n","free_process_manager_node NULL");
    return;
  }
  process_manager_obj = (struct process_manager_obj*)process_node->data;
  //free struct process_manager_obj
  free_process_manager_obj (process_manager_obj);
  //free current process_manager_obj entity.
  bi_list_free_node(process_node);
  return;
}

static long release_process_node_cleanup(bi_list* list)
{
  bi_list_node* new_process_node=NULL;

  while(1)
  {
    new_process_node=list->head;
    if(new_process_node==NULL)
      break;
    //remove node from list
    bi_list_remove_node(list,new_process_node);
    //remove node from list
    free_process_manager_node(new_process_node);
  }
  return 0;
}

static void create_kernel_process_manager(void)
{
  bi_list_node* process_manager_node;
  struct process_manager_obj* process_manager_obj=NULL;
  process_manager_node = create_process_manager_node();
  process_manager_obj = (struct process_manager_obj*)process_manager_node->data;
  process_manager_obj->filp = NULL;
  bi_list_insert_node_tail(&global_process_manager,process_manager_node);
}

/* Update the last JMP cmd in cmdbuf_ojb in order to jump to next_cmdbuf_obj. */
static void cmdbuf_update_jmp_cmd(int hw_version_id,
                                        struct cmdbuf_obj *cmdbuf_obj,
                                        struct cmdbuf_obj *next_cmdbuf_obj,
                                        int jmp_IE_1) {
  u32 *jmp_addr;
  u32 operation_code;

  if(!cmdbuf_obj)
    return;

  if(cmdbuf_obj->has_end_cmdbuf==0)
  {
    //need to link, current cmdbuf link to next cmdbuf
    jmp_addr = cmdbuf_obj->cmdbuf_virtualAddress + (cmdbuf_obj->cmdbuf_size/4);
    if (!next_cmdbuf_obj) {
      operation_code = *(jmp_addr-4);
      operation_code >>=16;
      operation_code <<=16;
      *(jmp_addr-4)=(u32)(operation_code & ~JMP_RDY_1);
    } else {
      if(hw_version_id > HW_ID_1_0_C )
      {
        //set next cmdbuf id
        *(jmp_addr-1) = next_cmdbuf_obj->cmdbuf_id;
      }
      if (mmu_enable) {
        if(sizeof(size_t) == 8) {
          *(jmp_addr-2)=(u32)((u64)(next_cmdbuf_obj->mmu_cmdbuf_busAddress)>>32);
        } else {
          *(jmp_addr-2)=0;
        }
        *(jmp_addr-3)=(u32)(next_cmdbuf_obj->mmu_cmdbuf_busAddress);
      } else {
        if(sizeof(size_t) == 8) {
          *(jmp_addr-2)=(u32)((u64)(next_cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr)>>32);
        } else {
          *(jmp_addr-2)=0;
        }
        *(jmp_addr-3)=(u32)(next_cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr);
      }
      operation_code = *(jmp_addr-4);
      operation_code >>=16;
      operation_code <<=16;
      *(jmp_addr-4)=(u32)(operation_code |JMP_RDY_1|jmp_IE_1|((next_cmdbuf_obj->cmdbuf_size+7)/8));
    }

#ifdef VCMD_DEBUG_INTERNAL
    {
       u32 i;
       pr_info("vcmd link, last cmdbuf content\n");
       for(i=cmdbuf_obj->cmdbuf_size/4 -8;i<cmdbuf_obj->cmdbuf_size/4;i++)
       {
         pr_info("current linked cmdbuf data %d =0x%x\n",i,*(cmdbuf_obj->cmdbuf_virtualAddress+i));
       }
    }
#endif
  }
}


/* delink given cmd buffer (cmdbuf_node) and remove it from list.
   Also modify the last JMP of buf P to point to cmdbuf N.
   Used when a process is terminated but there are pending cmd bufs in vmcd list.
   E.g.,
   before:
   
     L->L->...->P->X->N->        ...        ->L
     ^             ^                          ^
     head          cmdbuf_node                 tail
     
   end:

     L->L->...->P->N->        ...        ->L
     ^                                     ^
     head                                  tail

   Return: pointer to N or NULL if N doesn't exist.
 */
void vcmd_delink_rm_cmdbuf(struct hantrovcmd_dev *dev, bi_list_node* cmdbuf_node)
{
  bi_list *list = &dev->list_manager;
  struct cmdbuf_obj* cmdbuf_obj = (struct cmdbuf_obj*)cmdbuf_node->data;
  bi_list_node* prev = cmdbuf_node->previous;
  bi_list_node* next = cmdbuf_node->next;

  PDEBUG("Delink and remove cmdbuf [%d] from vcmd list.\n", cmdbuf_obj->cmdbuf_id);
  if (prev) {
    PDEBUG("prev cmdbuf [%d].\n", ((struct cmdbuf_obj*)prev->data)->cmdbuf_id);
  } else {
    PDEBUG("NO prev cmdbuf.\n");
  }
  if (next) {
    PDEBUG("next cmdbuf [%d].\n", ((struct cmdbuf_obj*)next->data)->cmdbuf_id);
  } else {
    PDEBUG("NO next cmdbuf.\n");
  }

  bi_list_remove_node(list, cmdbuf_node);
  global_cmdbuf_node[cmdbuf_obj->cmdbuf_id] = NULL;
  free_cmdbuf_node(cmdbuf_node);

  cmdbuf_update_jmp_cmd(dev->hw_version_id, prev ? prev->data : NULL, next ? next->data : NULL,
                        dev->duration_without_int > INT_MIN_SUM_OF_IMAGE_SIZE);
}

static int hantrovcmd_open(struct inode *inode, struct file *filp)
{
    int result = 0;
    struct hantrovcmd_dev *dev = hantrovcmd_data;
    bi_list_node* process_manager_node;
    unsigned long flags;
    struct process_manager_obj* process_manager_obj=NULL;

    filp->private_data = NULL;
    process_manager_node = create_process_manager_node();
    if(process_manager_node== NULL)
      return -1;
    process_manager_obj = (struct process_manager_obj*)process_manager_node->data;
    process_manager_obj->filp = filp;
    spin_lock_irqsave(&vcmd_process_manager_lock, flags);
    bi_list_insert_node_tail(&global_process_manager,process_manager_node);
    spin_unlock_irqrestore(&vcmd_process_manager_lock, flags);
    filp->private_data = process_manager_node->data;

    PDEBUG("process node %px for filp opened %px\n", (void *)process_manager_node, (void *)filp);
    return result;
}


/* hantrovcmd_flush -- blocking wait until cmdbuf done for memory can be released
 * To fix memory free but hardware in-using issue, caused in kill process which
 * vidmem file ops release may be before this drivers file ops release or waiting
 * cmd done.
 * This flush ops sequence depends on  devices open sequence in user mode drivers.
 * So check user mode driver opened sequence as open this dev then dev/vidmem.
 */
static int hantrovcmd_flush(struct file *filp, fl_owner_t id)
{
    struct hantrovcmd_dev *dev = hantrovcmd_data;
    u32 core_id = 0;
    bi_list_node* new_cmdbuf_node=NULL;
    struct cmdbuf_obj* cmdbuf_obj_temp=NULL;
    unsigned long flags;

    PDEBUG("dev flushing for process %px\n", (void *)filp);
    down(&vcmd_reserve_cmdbuf_sem[dev->vcmd_core_cfg.sub_module_type]); //should be wait,do not use interruptible interface

    for (core_id = 0;core_id < total_vcmd_core_num; core_id++)
    {
      if((&dev[core_id])==NULL)
        continue;
      spin_lock_irqsave(dev[core_id].spinlock, flags);
      new_cmdbuf_node=dev[core_id].list_manager.head;
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj_temp=(struct cmdbuf_obj*)new_cmdbuf_node->data;
        if(cmdbuf_obj_temp->cmdbuf_id > 1)
        {
            PDEBUG("Process %px is flush: checking cmdbuf %d (done %d,linked %d wstat %d)  process %px. remain %d\n",
               filp, cmdbuf_obj_temp->cmdbuf_id, cmdbuf_obj_temp->cmdbuf_run_done,cmdbuf_obj_temp->cmdbuf_data_linked,
               dev[core_id].working_state,
               cmdbuf_obj_temp->filp,cmdbuf_used_residual);
        }
        if (dev[core_id].hwregs && (cmdbuf_obj_temp->filp == filp))
        {
          if(cmdbuf_obj_temp->cmdbuf_data_linked==1 &&
            !cmdbuf_obj_temp->cmdbuf_run_done && dev[core_id].working_state==WORKING_STATE_WORKING)
          {
            u32 irq_status_ret;
            spin_unlock_irqrestore(dev[core_id].spinlock, flags);

            PDEBUG("  start wait event of cmdbuf[%d]\n",cmdbuf_obj_temp->cmdbuf_id);
            if(wait_event_timeout(*dev[core_id].wait_queue,
                      check_cmdbuf_irq(dev,cmdbuf_obj_temp,&irq_status_ret),msecs_to_jiffies(1000) )
              )
            {
                spin_lock_irqsave(dev[core_id].spinlock, flags);
                PDEBUG("  ## wait got cmdbuf[%d] done %d \n",cmdbuf_obj_temp->cmdbuf_id,cmdbuf_obj_temp->cmdbuf_run_done);
            }
            else
            {
              spin_lock_irqsave(dev[core_id].spinlock, flags);
              PDEBUG("  ## wait timeout cmdbuf[%d]  %d \n",cmdbuf_obj_temp->cmdbuf_id,cmdbuf_obj_temp->cmdbuf_run_done);
            }

          }

        }
        new_cmdbuf_node = new_cmdbuf_node->next;
      }

      spin_unlock_irqrestore(dev[core_id].spinlock, flags);

    }
    up(&vcmd_reserve_cmdbuf_sem[dev->vcmd_core_cfg.sub_module_type]);
    return 0;
}

static int __hantrovcmd_release(struct inode *inode, struct file *filp)
{
    struct hantrovcmd_dev *dev = hantrovcmd_data;
    u32 core_id = 0;
    u32 release_cmdbuf_num=0;
    bi_list_node* new_cmdbuf_node=NULL;
    struct cmdbuf_obj* cmdbuf_obj_temp=NULL;
    bi_list_node* process_manager_node;
    struct process_manager_obj* process_manager_obj=NULL;
    int vcmd_aborted = 0;  // vcmd is aborted in this function
    struct cmdbuf_obj* restart_cmdbuf = NULL;

    unsigned long flags;
    long retVal=0;

    PDEBUG("dev closed for process %px\n", (void *)filp);
    down(&vcmd_reserve_cmdbuf_sem[dev->vcmd_core_cfg.sub_module_type]); //should be wait,do not use interruptible interface

    for (core_id = 0;core_id < total_vcmd_core_num; core_id++)
    {
      if((&dev[core_id])==NULL)
        continue;
      spin_lock_irqsave(dev[core_id].spinlock, flags);
      new_cmdbuf_node=dev[core_id].list_manager.head;
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj_temp=(struct cmdbuf_obj*)new_cmdbuf_node->data;
        if(cmdbuf_obj_temp->cmdbuf_id > 1)
        {
            PDEBUG("Process %px is releasing: checking cmdbuf %d (done %d,linked %d wstat %d)  process %px. remain %d\n",
               filp, cmdbuf_obj_temp->cmdbuf_id, cmdbuf_obj_temp->cmdbuf_run_done,cmdbuf_obj_temp->cmdbuf_data_linked,
               dev[core_id].working_state,
               cmdbuf_obj_temp->filp,cmdbuf_used_residual);
        }
        if (dev[core_id].hwregs && (cmdbuf_obj_temp->filp == filp))
        {
          if(cmdbuf_obj_temp->cmdbuf_run_done)
          {
            cmdbuf_obj_temp->cmdbuf_need_remove=1;
            retVal=release_cmdbuf_node(&dev[core_id].list_manager,new_cmdbuf_node);
            if(retVal==1)
              cmdbuf_obj_temp->process_manager_obj = NULL;
          }
          else if(cmdbuf_obj_temp->cmdbuf_data_linked==0)
          {
            cmdbuf_obj_temp->cmdbuf_data_linked = 1;
            cmdbuf_obj_temp->cmdbuf_run_done=1;
            cmdbuf_obj_temp->cmdbuf_need_remove=1;
            retVal=release_cmdbuf_node(&dev[core_id].list_manager,new_cmdbuf_node);
            if(retVal==1)
              cmdbuf_obj_temp->process_manager_obj = NULL;
          }
          else if(cmdbuf_obj_temp->cmdbuf_data_linked==1 && dev[core_id].working_state==WORKING_STATE_IDLE)
          {
            vcmd_delink_rm_cmdbuf(&dev[core_id], new_cmdbuf_node);
            if(restart_cmdbuf == cmdbuf_obj_temp)
              restart_cmdbuf = new_cmdbuf_node->next ? new_cmdbuf_node->next->data : NULL;
          }
          else if(cmdbuf_obj_temp->cmdbuf_data_linked==1 && dev[core_id].working_state==WORKING_STATE_WORKING)
          {
            bi_list_node* last_cmdbuf_node = NULL;
            bi_list_node* done_cmdbuf_node = NULL;
            int abort_cmdbuf_id;
            int loop_count = 0;
            u32 irq_status_ret;

            spin_unlock_irqrestore(dev[core_id].spinlock, flags);
            if(wait_event_timeout(*dev[core_id].wait_queue,
                      check_cmdbuf_irq(dev,cmdbuf_obj_temp,&irq_status_ret),msecs_to_jiffies(1000))
              )
            {
                spin_lock_irqsave(dev[core_id].spinlock, flags);
                PDEBUG("  ## wait got cmdbuf[%d] done %d \n",cmdbuf_obj_temp->cmdbuf_id,cmdbuf_obj_temp->cmdbuf_run_done);
                cmdbuf_obj_temp->cmdbuf_need_remove=1;
                retVal=release_cmdbuf_node(&dev[core_id].list_manager,new_cmdbuf_node);
                if(retVal==1)
                  cmdbuf_obj_temp->process_manager_obj = NULL;
            }
            else
            {
                spin_lock_irqsave(dev[core_id].spinlock, flags);
                //abort the vcmd and wait
                PDEBUG("Abort due to linked cmdbuf %d of current process.\n", cmdbuf_obj_temp->cmdbuf_id);
    #ifdef VCMD_DEBUG_INTERNAL
                printk_vcmd_register_debug((const void *)dev[core_id].hwregs, "Before trigger to 0");
    #endif
                // disable abort interrupt
                vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_START_TRIGGER,0);
                vcmd_aborted = 1;
                software_triger_abort = 1;
    #ifdef VCMD_DEBUG_INTERNAL
                printk_vcmd_register_debug((const void *)dev[core_id].hwregs,"After trigger to 0");
    #endif
                // wait vcmd core aborted and vcmd enters IDLE mode.
                while (vcmd_get_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_WORK_STATE)) {
                  loop_count++;
                  if (!(loop_count % 10)) {
                    u32 irq_status = vcmd_read_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET);
                    pr_err("hantrovcmd: expected idle state, but irq status = 0x%0x\n", irq_status);
                    pr_err("hantrovcmd: vcmd current status is %d\n", vcmd_get_register_value((const void *)dev[core_id].hwregs, dev[core_id].reg_mirror, HWIF_VCMD_WORK_STATE));
                  }
                  mdelay(10);  // wait 10ms
                  if (loop_count > 100) { // too long
                    pr_err("hantrovcmd: too long before vcmd core to IDLE state\n");
                    process_manager_obj = (struct process_manager_obj*)filp->private_data;
                    if (process_manager_obj)
                    {
                      while(process_manager_obj->pm_count > 0)
                      {
                        pm_runtime_mark_last_busy(&dev[0].pdev->dev);
                        pm_runtime_put_autosuspend(&dev[0].pdev->dev);
                        process_manager_obj->pm_count--;
                      }
                    }
                    spin_unlock_irqrestore(dev[core_id].spinlock, flags);
                    up(&vcmd_reserve_cmdbuf_sem[dev->vcmd_core_cfg.sub_module_type]);
                    return -ERESTARTSYS;
                  }
                }
                dev[core_id].working_state = WORKING_STATE_IDLE;
                // clear interrupt & restore abort_e
                if (vcmd_get_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_IRQ_ABORT)) {
                  PDEBUG("Abort interrupt triggered, now clear it to avoid abort int...\n");
                  vcmd_write_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET, 0x1<<4);
                  PDEBUG("Now irq status = 0x%0x.\n", vcmd_read_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET));
                }

                abort_cmdbuf_id = vcmd_get_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID);
                PDEBUG("Abort when executing cmd buf %d.\n", abort_cmdbuf_id);
                dev[core_id].sw_cmdbuf_rdy_num = 0;
                dev[core_id].duration_without_int = 0;
                vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_EXE_CMDBUF_COUNT,0);
                vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_RDY_CMDBUF_COUNT,0);

                /* Mark cmdbuf_run_done to 1 for all the cmd buf executed. */
                done_cmdbuf_node = dev[core_id].list_manager.head;
                while (done_cmdbuf_node) {
                  if (!((struct cmdbuf_obj*)done_cmdbuf_node->data)->cmdbuf_run_done) { 
                    ((struct cmdbuf_obj*)done_cmdbuf_node->data)->cmdbuf_run_done = 1;
                    ((struct cmdbuf_obj*)done_cmdbuf_node->data)->cmdbuf_data_linked = 0;
                    PDEBUG("Set cmdbuf [%d] cmdbuf_run_done to 1.\n", ((struct cmdbuf_obj*)done_cmdbuf_node->data)->cmdbuf_id);
                  }
                  if (((struct cmdbuf_obj*)done_cmdbuf_node->data)->cmdbuf_id == abort_cmdbuf_id)
                    break;
                  done_cmdbuf_node = done_cmdbuf_node->next;
                }
                if (cmdbuf_obj_temp->cmdbuf_run_done) {
                  /* current cmdbuf is in fact has been executed, but due to interrupt is not triggered, the status is not updated.
                    Just delink and remove it from the list. */
                  if (done_cmdbuf_node && done_cmdbuf_node->data) {
                    PDEBUG("done_cmdbuf_node is cmdbuf [%d].\n", ((struct cmdbuf_obj*)done_cmdbuf_node->data)->cmdbuf_id);
                  }
                  done_cmdbuf_node = done_cmdbuf_node->next;            
                  if (done_cmdbuf_node)
                    restart_cmdbuf = (struct cmdbuf_obj*)done_cmdbuf_node->data;
                  if (restart_cmdbuf) {
                    PDEBUG("Set restart cmdbuf [%d] via if.\n", restart_cmdbuf->cmdbuf_id);
                  }
                } else { 
                  last_cmdbuf_node = new_cmdbuf_node;
                  /* cmd buf num from aborted cmd buf to current cmdbuf_obj_temp */
                  if (cmdbuf_obj_temp->cmdbuf_id != abort_cmdbuf_id) {
                    last_cmdbuf_node = new_cmdbuf_node->previous;
                    
                    while (last_cmdbuf_node &&
                          ((struct cmdbuf_obj*)last_cmdbuf_node->data)->cmdbuf_id != abort_cmdbuf_id) {
                      restart_cmdbuf = (struct cmdbuf_obj*)last_cmdbuf_node->data;
                      last_cmdbuf_node = last_cmdbuf_node->previous;
                      dev[core_id].sw_cmdbuf_rdy_num++;
                      dev[core_id].duration_without_int += restart_cmdbuf->executing_time;
                      PDEBUG("Keep valid cmdbuf [%d] in the list.\n", restart_cmdbuf->cmdbuf_id);
                    }
                  }
                  if (restart_cmdbuf) {
                    PDEBUG("Set restart cmdbuf [%d] via else.\n", restart_cmdbuf->cmdbuf_id);
                  }
                }

                // remove first linked cmdbuf from list
                vcmd_delink_rm_cmdbuf(&dev[core_id], new_cmdbuf_node);
            }

          }
          software_triger_abort = 0;
          release_cmdbuf_num++;
          PDEBUG("vc8000e : release reserved cmdbuf,remain %d\n",cmdbuf_used_residual);
        }
        else if (vcmd_aborted && !cmdbuf_obj_temp->cmdbuf_run_done) {
          /* VCMD is aborted, need to re-calculate the duration_without_int */
          if (!restart_cmdbuf)
            restart_cmdbuf = cmdbuf_obj_temp; /* first cmdbuf to be restarted */
          dev[core_id].duration_without_int += cmdbuf_obj_temp->executing_time;
          dev[core_id].sw_cmdbuf_rdy_num++;
        }
        new_cmdbuf_node = new_cmdbuf_node->next;
      }
      if (restart_cmdbuf && restart_cmdbuf->core_id == core_id) {
        u32 irq_status1, irq_status2;
        PDEBUG("Restart from cmdbuf [%d] after aborting.\n", restart_cmdbuf->cmdbuf_id);

        irq_status1 = vcmd_read_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET);
        vcmd_write_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET, irq_status1);
        irq_status2 = vcmd_read_reg((const void *)dev[core_id].hwregs, VCMD_REGISTER_INT_STATUS_OFFSET);
        PDEBUG("Clear irq status from 0x%0x -> 0x%0x\n", irq_status1, irq_status2);
        if (mmu_enable) {
          vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror, HWIF_VCMD_EXECUTING_CMD_ADDR,
                                        (u32)(restart_cmdbuf->mmu_cmdbuf_busAddress));
          if(sizeof(size_t) == 8) {
            vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror, HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,(u32)((u64)(restart_cmdbuf->mmu_cmdbuf_busAddress)>>32));
          } else {
            vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror, HWIF_VCMD_EXECUTING_CMD_ADDR_MSB, 0);
          }
        } else {
          vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror, HWIF_VCMD_EXECUTING_CMD_ADDR,(u32)(restart_cmdbuf->cmdbuf_busAddress-base_ddr_addr));
          if(sizeof(size_t) == 8) {
            vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror, HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,(u32)((u64)(restart_cmdbuf->cmdbuf_busAddress-base_ddr_addr)>>32));
          } else {
            vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror, HWIF_VCMD_EXECUTING_CMD_ADDR_MSB, 0);
          }
        }
        vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_EXE_CMDBUF_COUNT,0);
        vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_EXE_CMDBUF_LENGTH,(u32)((restart_cmdbuf->cmdbuf_size+7)/8));
        vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID,restart_cmdbuf->cmdbuf_id);
        vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_RDY_CMDBUF_COUNT,dev->sw_cmdbuf_rdy_num);
#ifdef VCMD_DEBUG_INTERNAL
        printk_vcmd_register_debug((const void *)dev[core_id].hwregs, "before restart");
#endif
        vcmd_write_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_START_TRIGGER,1);

        PDEBUG("Restart from cmdbuf [%d] dev register sw_cmdbuf_rdy_num is %d \n", restart_cmdbuf->cmdbuf_id,
               vcmd_get_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_RDY_CMDBUF_COUNT));

        PDEBUG("Restart from cmdbuf [%d] after aborting: start trigger = %d.\n", restart_cmdbuf->cmdbuf_id,
               vcmd_get_register_value((const void *)dev[core_id].hwregs,dev[core_id].reg_mirror,HWIF_VCMD_START_TRIGGER));
        PDEBUG("dev state from %d -> WORKING.\n", dev[core_id].working_state);
        dev[core_id].working_state = WORKING_STATE_WORKING;
#ifdef VCMD_DEBUG_INTERNAL
        printk_vcmd_register_debug((const void *)dev[core_id].hwregs, "after restart");
#endif
      } else {
        PDEBUG("No more command buffer to be restarted!\n");
      }
      spin_unlock_irqrestore(dev[core_id].spinlock, flags);
      // VCMD aborted but not restarted, nedd to wake up  
      if (vcmd_aborted && !restart_cmdbuf)
        wake_up_all(dev[core_id].wait_queue);
    }
    if(release_cmdbuf_num)
      wake_up_all(&vcmd_cmdbuf_memory_wait);

    // free reserved but unlinkd node
    // Here is powerfull free all this process reserved/linked cmdbuf
    free_cmdbuf_not_linked_by_flip(filp);

    spin_lock_irqsave(&vcmd_process_manager_lock, flags);
    process_manager_node = global_process_manager.head;
    while(1)
    {
      if(process_manager_node == NULL)
        break;
      process_manager_obj = (struct process_manager_obj*)process_manager_node->data;
      if(process_manager_obj->filp == filp)
        break;
      process_manager_node = process_manager_node->next;
    }
    if (process_manager_obj)
    {
      while(process_manager_obj->pm_count > 0)
      {
        pm_runtime_mark_last_busy(&dev[0].pdev->dev);
        pm_runtime_put_autosuspend(&dev[0].pdev->dev);
        process_manager_obj->pm_count--;
      }
    }
    //remove node from list
    PDEBUG("process node %px for filp to be removed: %px remain %d\n", (void *)process_manager_node, (void *)process_manager_obj->filp,cmdbuf_used_residual);
    bi_list_remove_node(&global_process_manager,process_manager_node);
    spin_unlock_irqrestore(&vcmd_process_manager_lock, flags);
    free_process_manager_node(process_manager_node);
    up(&vcmd_reserve_cmdbuf_sem[dev->vcmd_core_cfg.sub_module_type]);
    return 0;
}

static int hantrovcmd_release(struct inode *inode, struct file *filp)
{
    int i;
    int ret = 0;
    ret = __hantrovcmd_release(inode,filp);
#ifdef HANTROMMU_SUPPORT
    for(i = 0; i < total_vcmd_core_num; i++)
    {
        if (mmu_hwregs[i][0] != NULL)
            MMURelease(filp,mmu_hwregs[i][0]);
    }
#endif
  return ret;
}

static bool hantroenc_cmdbuf_range(size_t addr, size_t size);

#if LINUX_VERSION_CODE > KERNEL_VERSION(6, 4, 0)
static pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
			      unsigned long size, pgprot_t vma_prot)
{
	if (!pfn_valid(pfn)) {
		return pgprot_noncached(vma_prot);
	} else if (file->f_flags & O_SYNC) {
		return pgprot_writecombine(vma_prot);
	}

	return vma_prot;
}
#endif
static int mmap_cmdbuf_mem(struct file *file, struct vm_area_struct *vma)
{
    size_t size = vma->vm_end - vma->vm_start;
    phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

    /* Does it even fit in phys_addr_t? */
    if (offset >> PAGE_SHIFT != vma->vm_pgoff)
        return -EINVAL;

    /* It's illegal to wrap around the end of the physical address space. */
    if (offset + (phys_addr_t)size - 1 < offset)
        return -EINVAL;


    vma->vm_page_prot = phys_mem_access_prot(file, vma->vm_pgoff,
                        size,
                        vma->vm_page_prot);

    /* Remap-pfn-range will mark the range VM_IO */
    if (remap_pfn_range(vma,
                        vma->vm_start,
                        vma->vm_pgoff,
                        size,
                        vma->vm_page_prot))
    {
        return -EAGAIN;
    }

    return 0;
}

static int mmap_mem(struct file *file, struct vm_area_struct *vma)
{
    size_t size = vma->vm_end - vma->vm_start;
    phys_addr_t offset = (phys_addr_t)vma->vm_pgoff << PAGE_SHIFT;

    if (hantroenc_cmdbuf_range(offset, size))
    {
        return mmap_cmdbuf_mem(file, vma);
    }
    else
    {
        return -EINVAL;
        /*TODO: need check if not need enc,in this condition*/
        //return allocator_mmap(file,vma);
    }
}

/* VFS methods */
static struct file_operations hantrovcmd_fops = {
    .owner= THIS_MODULE,
    .open = hantrovcmd_open,
    .flush = hantrovcmd_flush,
    .release = hantrovcmd_release,
    .unlocked_ioctl = hantrovcmd_ioctl,
    .mmap = mmap_mem,
    .fasync = NULL,
};

static u32 vcmd_release_AXIFE_IO(void)
{
#ifdef HANTROAXIFE_SUPPORT
  int i=0, j=0;

  for(i=0; i<total_vcmd_core_num; i++)
  {
    for(j=0; j<2; j++)
    {
      if(axife_hwregs[i][j] != NULL)
      {
        iounmap(axife_hwregs[i][j]);
        release_mem_region(vcmd_core_array[i].vcmd_base_addr
                          +vcmd_core_array[i].submodule_axife_addr[j], AXIFE_SIZE);
        axife_hwregs[i][j] = NULL;
      }
    }
  }
#endif
  return 0;
}
static u32 vcmd_release_MMU_IO(void)
{
#ifdef HANTROMMU_SUPPORT
  int i=0, j=0;

  for(i=0; i<total_vcmd_core_num; i++)
  {
    for(j=0; j<2; j++)
    {
      if(mmu_hwregs[i][j] != NULL)
      {
        iounmap(mmu_hwregs[i][j]);
        release_mem_region(vcmd_core_array[i].vcmd_base_addr
                          +vcmd_core_array[i].submodule_MMU_addr[j], MMU_SIZE);
        mmu_hwregs[i][j] = NULL;
      }
    }
  }
#endif
  return 0;
}

static u32 ConfigAXIFE(u32 mode)
{
#ifdef HANTROAXIFE_SUPPORT
  u32 i = 0;
  u8 sub_module_type = 0;
  for(i=0; i<total_vcmd_core_num; i++)
  {
    sub_module_type = vcmd_core_array[i].sub_module_type;
    pr_info("ConfigAXIFE: sub_module_type is %d\n", sub_module_type);
    if(vcmd_core_array[i].submodule_axife_addr[0] != 0xffff)
    {
      if(!request_mem_region(vcmd_core_array[i].vcmd_base_addr+vcmd_core_array[i].submodule_axife_addr[0], AXIFE_SIZE, "vc8000"))
      {
        pr_info("failed to request_mem_region for axife_hwregs[%d][0]\n", i);
        return -EBUSY;
      }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0))
      axife_hwregs[i][0] = (volatile u8*)ioremap_nocache(vcmd_core_array[i].vcmd_base_addr
                                                        +vcmd_core_array[i].submodule_axife_addr[0], AXIFE_SIZE);
#else
      axife_hwregs[i][0] = (volatile u8*)ioremap(vcmd_core_array[i].vcmd_base_addr
                                                        +vcmd_core_array[i].submodule_axife_addr[0], AXIFE_SIZE);
#endif
      if (axife_hwregs[i][0] != NULL)
      {
        PDEBUG("ConfigAXIFE: axife_hwregs[%d][0]=%px\n", i, axife_hwregs[i][0]);
        AXIFEEnable(axife_hwregs[i][0], mode);
      }
    }
    if(vcmd_core_array[i].submodule_axife_addr[1] != 0xffff)
    {
      if(!request_mem_region(vcmd_core_array[i].vcmd_base_addr+vcmd_core_array[i].submodule_axife_addr[1], AXIFE_SIZE, "vc8000"))
      {
        pr_info("failed to request_mem_region for axife_hwregs[%d][0]\n", i);
        return -EBUSY;
      }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0))
      axife_hwregs[i][1]  = (volatile u8*)ioremap_nocache(vcmd_core_array[i].vcmd_base_addr
                                                         +vcmd_core_array[i].submodule_axife_addr[1], AXIFE_SIZE);
#else
      axife_hwregs[i][1] = (volatile u8*)ioremap(vcmd_core_array[i].vcmd_base_addr
                                                         +vcmd_core_array[i].submodule_axife_addr[1], AXIFE_SIZE);
#endif
      if (axife_hwregs[i][1]  != NULL)
      {
        PDEBUG("ConfigAXIFE: axife_hwregs[%d][1]=%px\n", i, axife_hwregs[i][1]);
        AXIFEEnable(axife_hwregs[i][1] , mode);
      }
    }
  }
#endif
  return 0;
}
static u32 ConfigMMU(void)
{
#ifdef HANTROMMU_SUPPORT
  u32 i = 0;
  enum MMUStatus mmu_status = MMU_STATUS_FALSE;
  u8 sub_module_type = 0;

  /* MMU only initial once for the vcmd core no matter how many MMU we have */
  for(i = 0; i < total_vcmd_core_num; i++)
  {
    sub_module_type = vcmd_core_array[i].sub_module_type;
    pr_info("ConfigMMU, MMUInit: sub_module_type is %d\n", sub_module_type);
    if(vcmd_core_array[i].submodule_MMU_addr[0] != 0xffff)
    {
      if(!request_mem_region(vcmd_core_array[i].vcmd_base_addr+vcmd_core_array[i].submodule_MMU_addr[0], MMU_SIZE, "vc8000"))
      {
        pr_info("failed to request_mem_region for mmu_hwregs[%d][0]\n", i);
        return -EBUSY;
      }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0))
      mmu_hwregs[i][0] =  (volatile u8*)ioremap_nocache(vcmd_core_array[i].vcmd_base_addr
                                                       +vcmd_core_array[i].submodule_MMU_addr[0], MMU_SIZE);
#else
      mmu_hwregs[i][0] =  (volatile u8*)ioremap(vcmd_core_array[i].vcmd_base_addr
                                                       +vcmd_core_array[i].submodule_MMU_addr[0], MMU_SIZE);
#endif
      if (mmu_hwregs[i][0] != NULL)
      {
        mmu_status = MMUInit(mmu_hwregs[i][0]);
        if(mmu_status == MMU_STATUS_NOT_FOUND)
        {
          pr_info("MMU does not exist!\n");
          return -1;
        }
        else if(mmu_status != MMU_STATUS_OK)
        {
          return -2;
        }
        else
          pr_info("MMU detected!\n");
        PDEBUG("mmu_hwregs[%d][0]=%px\n", i, mmu_hwregs[i][0]);
      }
    }

    if(vcmd_core_array[i].submodule_MMU_addr[1] != 0xffff)
    {
      if(!request_mem_region(vcmd_core_array[i].vcmd_base_addr+vcmd_core_array[i].submodule_MMU_addr[1], MMU_SIZE, "vc8000"))
      {
        pr_info("failed to request_mem_region for mmu_hwregs[%d][0]\n", i);
        return -EBUSY;
      }
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0))
      mmu_hwregs[i][1] =  (volatile u8*)ioremap_nocache(vcmd_core_array[i].vcmd_base_addr
                                                       +vcmd_core_array[i].submodule_MMU_addr[1], MMU_SIZE);
#else
      mmu_hwregs[i][1] =  (volatile u8*)ioremap(vcmd_core_array[i].vcmd_base_addr
                                                       +vcmd_core_array[i].submodule_MMU_addr[1], MMU_SIZE);
#endif
      PDEBUG("mmu_hwregs[%d][1]=%px\n", i, mmu_hwregs[i][1]);
    }
  }
  mmu_status = MMUEnable(mmu_hwregs);
  PDEBUG("mmu_enable is %d\n", mmu_enable);
  if (mmu_status != MMU_STATUS_OK)
  {
    pr_info("MMUEnable: MMU enable failed\n");
    return -3;
  }
#endif
  return 0;
}
static u32 MMU_Kernel_map(void)
{
#ifdef HANTROMMU_SUPPORT
  struct kernel_addr_desc addr;

  if (mmu_enable) {
    addr.bus_address = vcmd_buf_mem_pool.busAddress - gBaseDDRHw;
    addr.size = vcmd_buf_mem_pool.size;
    if(MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK) {
      return -1;
    }
    vcmd_buf_mem_pool.mmu_bus_address = addr.mmu_bus_address;
    pr_info("MMU_Kernel_map: vcmd_buf_mem_pool.mmu_bus_address=0x%llx.\n",(long long unsigned int)vcmd_buf_mem_pool.mmu_bus_address);
  }
  if (mmu_enable) {
    addr.bus_address = vcmd_status_buf_mem_pool.busAddress - gBaseDDRHw;
    addr.size = vcmd_status_buf_mem_pool.size;
    if(MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK) {
      return -1;
    }
    vcmd_status_buf_mem_pool.mmu_bus_address = addr.mmu_bus_address;
    pr_info("MMU_Kernel_map: vcmd_status_buf_mem_pool.mmu_bus_address=0x%llx.\n",(long long unsigned int)vcmd_status_buf_mem_pool.mmu_bus_address);
  }
  if (mmu_enable) {
    addr.bus_address = vcmd_registers_mem_pool.busAddress - gBaseDDRHw;
    addr.size = vcmd_registers_mem_pool.size;
    if(MMUKernelMemNodeMap(&addr) != MMU_STATUS_OK) {
      return -1;
    }
    vcmd_registers_mem_pool.mmu_bus_address = addr.mmu_bus_address;
    pr_info("MMU_Kernel_map: vcmd_registers_mem_pool.mmu_bus_address=0x%llx.\n",(long long unsigned int)vcmd_registers_mem_pool.mmu_bus_address);
  }
#endif
  return 0;
}

static u32 vcmd_pool_release(struct platform_device *pdev)
{
  //iounmap((void *) vcmd_buf_mem_pool.virtualAddress);
  //release_mem_region(vcmd_buf_mem_pool.busAddress, vcmd_buf_mem_pool.size);
  //iounmap((void *) vcmd_status_buf_mem_pool.virtualAddress);
  //release_mem_region(vcmd_status_buf_mem_pool.busAddress, vcmd_status_buf_mem_pool.size);
  //iounmap((void *) vcmd_registers_mem_pool.virtualAddress);
  //release_mem_region(vcmd_registers_mem_pool.busAddress, vcmd_registers_mem_pool.size);
  dma_free_coherent(&pdev->dev, CMDBUF_POOL_TOTAL_SIZE*2 + CMDBUF_VCMD_REGISTER_TOTAL_SIZE,
        vcmd_buf_mem_pool.virtualAddress, vcmd_buf_mem_pool.busAddress);

  return 0;
}

static u32 MMU_Kernel_unmap(void)
{
#ifdef HANTROMMU_SUPPORT
  struct kernel_addr_desc addr;

  if (vcmd_buf_mem_pool.virtualAddress) {
      if (mmu_enable) {
        addr.bus_address = vcmd_buf_mem_pool.busAddress - gBaseDDRHw;
        addr.size = vcmd_buf_mem_pool.size;
        MMUKernelMemNodeUnmap(&addr);
      }
    }
    if (vcmd_status_buf_mem_pool.virtualAddress) {
      if (mmu_enable) {
        addr.bus_address = vcmd_status_buf_mem_pool.busAddress - gBaseDDRHw;
        addr.size = vcmd_status_buf_mem_pool.size;
        MMUKernelMemNodeUnmap(&addr);
      }
    }
    if (vcmd_registers_mem_pool.virtualAddress) {
      if (mmu_enable) {
        addr.bus_address = vcmd_registers_mem_pool.busAddress - gBaseDDRHw;
        addr.size = vcmd_registers_mem_pool.size;
        MMUKernelMemNodeUnmap(&addr);
      }
    }
#endif

  return 0;
}

static void vcmd_link_cmdbuf(struct hantrovcmd_dev *dev,bi_list_node* last_linked_cmdbuf_node)
{
  bi_list_node* new_cmdbuf_node=NULL;
  bi_list_node* next_cmdbuf_node=NULL;
  struct cmdbuf_obj* cmdbuf_obj=NULL;
  struct cmdbuf_obj* next_cmdbuf_obj=NULL;
  u32 * jmp_addr=NULL;
  u32 operation_code;
  new_cmdbuf_node = last_linked_cmdbuf_node;
  //for the first cmdbuf.
  if(new_cmdbuf_node!=NULL)
  {
    cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
    if((cmdbuf_obj->cmdbuf_data_linked==0) )
    {
      dev->sw_cmdbuf_rdy_num ++;
      cmdbuf_obj->cmdbuf_data_linked=1;
      dev->duration_without_int = 0;
      if(cmdbuf_obj->has_end_cmdbuf==0)
      {
        if(cmdbuf_obj->no_normal_int_cmdbuf==1)
        {
          dev->duration_without_int = cmdbuf_obj->executing_time;
          //maybe nop is modified, so write back.
          if(dev->duration_without_int>=INT_MIN_SUM_OF_IMAGE_SIZE)
          {
            jmp_addr = cmdbuf_obj->cmdbuf_virtualAddress + (cmdbuf_obj->cmdbuf_size/4);
            operation_code = *(jmp_addr-4);
            operation_code = JMP_IE_1|operation_code;
            *(jmp_addr-4) = operation_code;
            dev->duration_without_int = 0;
          }

        }
      }
    }
  }
  while(1)
  {
    if(new_cmdbuf_node==NULL)
      break;
    if(new_cmdbuf_node->next==NULL)
      break;
    next_cmdbuf_node = new_cmdbuf_node->next;
    cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
    next_cmdbuf_obj = (struct cmdbuf_obj*)next_cmdbuf_node->data;
    if(cmdbuf_obj->has_end_cmdbuf==0 && !next_cmdbuf_obj->cmdbuf_run_done)
    {
      //need to link, current cmdbuf link to next cmdbuf
      jmp_addr = cmdbuf_obj->cmdbuf_virtualAddress + (cmdbuf_obj->cmdbuf_size/4);
      if(dev->hw_version_id > HW_ID_1_0_C )
      {
        //set next cmdbuf id
        *(jmp_addr-1) = next_cmdbuf_obj->cmdbuf_id;
      }
      if (mmu_enable) {
        if(sizeof(size_t) == 8) {
          *(jmp_addr-2)=(u32)((u64)(next_cmdbuf_obj->mmu_cmdbuf_busAddress)>>32);
        } else {
          *(jmp_addr-2)=0;
        }
        *(jmp_addr-3)=(u32)(next_cmdbuf_obj->mmu_cmdbuf_busAddress);
        pr_debug("vcmd_link_cmdbuf: next_cmdbuf_obj->mmu_cmdbuf_busAddress=0x%08x\n", next_cmdbuf_obj->mmu_cmdbuf_busAddress);
      } else {
        if(sizeof(size_t) == 8) {
          *(jmp_addr-2)=(u32)((u64)(next_cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr)>>32);
        } else {
          *(jmp_addr-2)=0;
        }
        *(jmp_addr-3)=(u32)(next_cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr);
      }
      operation_code = *(jmp_addr-4);
      operation_code >>=16;
      operation_code <<=16;
      *(jmp_addr-4)=(u32)(operation_code |JMP_RDY_1|((next_cmdbuf_obj->cmdbuf_size+7)/8));
      next_cmdbuf_obj->cmdbuf_data_linked = 1;
      dev->sw_cmdbuf_rdy_num ++;
      //modify nop code of next cmdbuf
      if(next_cmdbuf_obj->has_end_cmdbuf==0)
      {
        if(next_cmdbuf_obj->no_normal_int_cmdbuf==1)
        {
          dev->duration_without_int +=next_cmdbuf_obj->executing_time;

          //maybe we see the modified nop before abort, so need to write back.
          if(dev->duration_without_int>=INT_MIN_SUM_OF_IMAGE_SIZE)
          {
            jmp_addr = next_cmdbuf_obj->cmdbuf_virtualAddress + (next_cmdbuf_obj->cmdbuf_size/4);
            operation_code = *(jmp_addr-4);
            operation_code = JMP_IE_1|operation_code;
            *(jmp_addr-4) = operation_code;
            dev->duration_without_int = 0;
          }
        }
      }
      else
      {
        dev->duration_without_int = 0;
      }
#ifdef VCMD_DEBUG_INTERNAL
      {
         u32 i;
         pr_info("vcmd link, last cmdbuf content\n");
         for(i=cmdbuf_obj->cmdbuf_size/4 -8;i<cmdbuf_obj->cmdbuf_size/4;i++)
         {
           pr_info("current linked cmdbuf data %d =0x%x\n",i,*(cmdbuf_obj->cmdbuf_virtualAddress+i));
         }
      }
#endif
    }
    new_cmdbuf_node = new_cmdbuf_node->next;
  }
  return;
}
/* delink all the cmd buffers from the cmdbuf in front of last_linked_cmdbuf_node
   to head of the list. All the cmd bufs marked as X will be delinked.
   E.g.,
   X->X->...->X->L->L->        ...        ->L
   ^             ^                          ^
   head          last_linked_cmdbuf_node    tail
 */

static void vcmd_delink_cmdbuf(struct hantrovcmd_dev *dev,bi_list_node* last_linked_cmdbuf_node)
{
  bi_list_node* new_cmdbuf_node=NULL;
  struct cmdbuf_obj* cmdbuf_obj=NULL;

  new_cmdbuf_node = last_linked_cmdbuf_node;
  while(1)
  {
    if(new_cmdbuf_node==NULL)
      break;
    cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
    if(cmdbuf_obj->cmdbuf_data_linked)
    {
      cmdbuf_obj->cmdbuf_data_linked = 0;
    }
    else
      break;
    new_cmdbuf_node = new_cmdbuf_node->next;
  }
  dev->sw_cmdbuf_rdy_num=0;
}

static void ConfigAIIXFE_MMU_BYVCMD(struct hantrovcmd_dev **device)
{
#ifdef HANTROVCMD_ENABLE_IP_SUPPORT
  u32 i = 0;
  u64 address = 0;
  u32 mirror_index, register_index, register_value;
  u32 write_command = 0;
  if(!device) return;
  struct hantrovcmd_dev *dev = *device;

  mirror_index = VCMD_REGISTER_INDEX_SW_INIT_CMD0;
  write_command = OPCODE_WREG|(1<<26)|(1<<16);
#ifdef HANTROAXIFE_SUPPORT
  //enable AXIFE by VCMD
  for(i=0; i<2; i++)
  {
    if(dev->vcmd_core_cfg.submodule_axife_addr[i] != 0xffff)
    {
      register_index = AXI_REG10_SW_FRONTEND_EN;
      register_value = 0x02;
      dev->reg_mirror[mirror_index++] = write_command|(dev->vcmd_core_cfg.submodule_axife_addr[i]+register_index);
      dev->reg_mirror[mirror_index++] = register_value;

      register_index =  AXI_REG11_SW_WORK_MODE;
      register_value = 0x00;
      dev->reg_mirror[mirror_index++] = write_command|(dev->vcmd_core_cfg.submodule_axife_addr[i]+register_index);
      dev->reg_mirror[mirror_index++] = register_value;
    }
  }
#endif
#ifdef HANTROMMU_SUPPORT
  //enable MMU by VCMD
  address = GetMMUAddress();
  pr_info("ConfigAIIXFE_MMU_BYVCMD: address = 0x%llx", address);
  for(i=0; i<2; i++)
  {
    if(dev->vcmd_core_cfg.submodule_MMU_addr[i] != 0xffff)
    {
      register_index = MMU_REG_ADDRESS;
      register_value = address;
      dev->reg_mirror[mirror_index++] = write_command|(dev->vcmd_core_cfg.submodule_MMU_addr[i]+register_index);
      dev->reg_mirror[mirror_index++] = register_value;

      register_index =  MMU_REG_PAGE_TABLE_ID;
      register_value = 0x10000;
      dev->reg_mirror[mirror_index++] = write_command|(dev->vcmd_core_cfg.submodule_MMU_addr[i]+register_index);
      dev->reg_mirror[mirror_index++] = register_value;

      register_index =  MMU_REG_PAGE_TABLE_ID;
      register_value = 0x00000;
      dev->reg_mirror[mirror_index++] = write_command|(dev->vcmd_core_cfg.submodule_MMU_addr[i]+register_index);
      dev->reg_mirror[mirror_index++] = register_value;

      register_index =  MMU_REG_CONTROL;
      register_value = 1;
      dev->reg_mirror[mirror_index++] = write_command|(dev->vcmd_core_cfg.submodule_MMU_addr[i]+register_index);
      dev->reg_mirror[mirror_index++] = register_value;
    }
  }
#endif
  //END command
  dev->reg_mirror[mirror_index++] = OPCODE_END;
  dev->reg_mirror[mirror_index] = 0x00;

  for(i=0; i<mirror_index-VCMD_REGISTER_INDEX_SW_INIT_CMD0; i++)
  {
    register_index = (i + VCMD_REGISTER_INDEX_SW_INIT_CMD0) * 4;
    vcmd_write_reg((const void *)dev->hwregs, register_index, dev->reg_mirror[i+VCMD_REGISTER_INDEX_SW_INIT_CMD0]);
  }
#endif
}
static void vcmd_start(struct hantrovcmd_dev *dev,bi_list_node* first_linked_cmdbuf_node)
{
  struct cmdbuf_obj* cmdbuf_obj = NULL;

  if(dev->working_state == WORKING_STATE_IDLE)
  {
    if((first_linked_cmdbuf_node!=NULL) && dev->sw_cmdbuf_rdy_num)
    {
      cmdbuf_obj = (struct cmdbuf_obj*)first_linked_cmdbuf_node->data;
#ifdef VCMD_DEBUG_INTERNAL
      printk_vcmd_register_debug((const void *)dev->hwregs, "vcmd_start enters");
#endif
      //0x40
#ifdef HANTROVCMD_ENABLE_IP_SUPPORT
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_INIT_MODE,1); //when start vcmd, first vcmd is init mode
#endif
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_AXI_CLK_GATE_DISABLE,0);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_MASTER_OUT_CLK_GATE_DISABLE,1);//this bit should be set 1 only when need to reset dec400
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_CORE_CLK_GATE_DISABLE,0);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_ABORT_MODE,0);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_RESET_CORE,0);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_RESET_ALL,0);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_START_TRIGGER,0);
      //0x48
      if(dev->hw_version_id <= HW_ID_1_0_C)
        vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_INTCMD_EN,0xffff);
      else
      {
        vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_JMPP_EN,1);
        vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_JMPD_EN,1);
      }
      
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_RESET_EN,1);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_ABORT_EN,1);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_CMDERR_EN,1);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_TIMEOUT_EN,1);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_BUSERR_EN,1);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_ENDCMD_EN,1);
      //0x4c
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_TIMEOUT_EN,1);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_TIMEOUT_CYCLES,0x1dcd6500);
      if (mmu_enable) {
        vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR,(u32)(cmdbuf_obj->mmu_cmdbuf_busAddress));
        pr_debug("cmdbuf_obj->mmu_cmdbuf_busAddress=0x%08x\n", (u32)cmdbuf_obj->mmu_cmdbuf_busAddress);
        if(sizeof(size_t) == 8) {
          vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,(u32)((u64)(cmdbuf_obj->mmu_cmdbuf_busAddress)>>32));
        } else {
          vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR_MSB, 0);
        }
      } else {
        vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR,(u32)(cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr));
        if(sizeof(size_t) == 8) {
          vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR_MSB,(u32)((u64)(cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr)>>32));
        } else {
          vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR_MSB, 0);
        }
      }
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_EXE_CMDBUF_LENGTH,(u32)((cmdbuf_obj->cmdbuf_size+7)/8));
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_RDY_CMDBUF_COUNT,dev->sw_cmdbuf_rdy_num);
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_MAX_BURST_LEN,0x10);
      if(dev->hw_version_id > HW_ID_1_0_C )
      {
        vcmd_write_register_value((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID,(u32)cmdbuf_obj->cmdbuf_id);
      }
      vcmd_write_reg((const void *)dev->hwregs,0x44,vcmd_read_reg((const void *)dev->hwregs,0x44));
      vcmd_write_reg((const void *)dev->hwregs,0x40,dev->reg_mirror[0x40/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x48,dev->reg_mirror[0x48/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x4c,dev->reg_mirror[0x4c/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x50,dev->reg_mirror[0x50/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x54,dev->reg_mirror[0x54/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x58,dev->reg_mirror[0x58/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x5c,dev->reg_mirror[0x5c/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x60,dev->reg_mirror[0x60/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x64,0xffffffff);//not interrupt cpu

      dev->working_state = WORKING_STATE_WORKING;

      ConfigAIIXFE_MMU_BYVCMD(&dev);

      //start
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_MASTER_OUT_CLK_GATE_DISABLE,1);//this bit should be set 1 only when need to reset dec400
      vcmd_set_register_mirror_value(dev->reg_mirror,HWIF_VCMD_START_TRIGGER,1);
      PDEBUG("To write vcmd register 16:0x%x\n",dev->reg_mirror[0x40/4]);
      vcmd_write_reg((const void *)dev->hwregs,0x40,dev->reg_mirror[0x40/4]);
#ifdef VCMD_DEBUG_INTERNAL
      printk_vcmd_register_debug(dev->hwregs, "vcmd_start exits ");
#endif
    }
  }

}

static void create_read_all_registers_cmdbuf(struct exchange_parameter* input_para)
{
    u32 register_range[]={VCMD_ENCODER_REGISTER_SIZE,
                    VCMD_IM_REGISTER_SIZE,
                    VCMD_DECODER_REGISTER_SIZE,
                    VCMD_JPEG_ENCODER_REGISTER_SIZE,
                    VCMD_JPEG_DECODER_REGISTER_SIZE};
    u32 counter_cmdbuf_size=0;
    u32 * set_base_addr=vcmd_buf_mem_pool.virtualAddress + input_para->cmdbuf_id*CMDBUF_MAX_SIZE/4;
    //u32 *status_base_virt_addr=vcmd_status_buf_mem_pool.virtualAddress + input_para->cmdbuf_id*CMDBUF_MAX_SIZE/4+(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_main_addr/2/4+0);
    ptr_t status_base_phy_addr=vcmd_status_buf_mem_pool.busAddress + input_para->cmdbuf_id*CMDBUF_MAX_SIZE+(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_main_addr/2+0);
    u32 map_status_base_phy_addr=vcmd_status_buf_mem_pool.mmu_bus_address + input_para->cmdbuf_id*CMDBUF_MAX_SIZE+(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_main_addr/2+0);
    u32 offset_inc=0;
    u32 offset_inc_dec400=0;
    if(vcmd_manager[input_para->module_type][0]->hw_version_id>HW_ID_1_0_C)
    {
      pr_info("vc8000_vcmd_driver:create cmdbuf data when hw_version_id = 0x%x\n",vcmd_manager[input_para->module_type][0]->hw_version_id);

      //read vcmd executing cmdbuf id registers to ddr for balancing core load.
      *(set_base_addr+0) = (OPCODE_RREG) |(1<<16) |(EXECUTING_CMDBUF_ID_ADDR*4);
      counter_cmdbuf_size += 4;
      *(set_base_addr+1) = (u32)0; //will be changed in link stage
      counter_cmdbuf_size += 4;
      *(set_base_addr+2) = (u32)0; //will be changed in link stage
      counter_cmdbuf_size += 4;
      //alignment
      *(set_base_addr+3) = 0;
      counter_cmdbuf_size += 4;

      //read main IP all registers
      *(set_base_addr+4) = (OPCODE_RREG) |((register_range[input_para->module_type]/4)<<16) |(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_main_addr+0);
      counter_cmdbuf_size += 4;
      if (mmu_enable) {
        *(set_base_addr+5) = map_status_base_phy_addr;
      } else {
        *(set_base_addr+5) = (u32)(status_base_phy_addr-base_ddr_addr);
      }
      counter_cmdbuf_size += 4;
      if (mmu_enable) {
        *(set_base_addr+6) = 0;
      } else {
        if(sizeof(size_t) == 8) {
          *(set_base_addr+6) = (u32)((u64)(status_base_phy_addr-base_ddr_addr)>>32);
        } else {
          *(set_base_addr+6) = 0;
        }
      }
      counter_cmdbuf_size += 4;
      //alignment
      *(set_base_addr+7) = 0;
      counter_cmdbuf_size += 4;
      
      if(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_L2Cache_addr != 0xffff)
      {
        //read L2 cache register
        offset_inc = 4;
        status_base_phy_addr=vcmd_status_buf_mem_pool.busAddress + input_para->cmdbuf_id*CMDBUF_MAX_SIZE+(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_L2Cache_addr/2+0);
        map_status_base_phy_addr=vcmd_status_buf_mem_pool.mmu_bus_address + input_para->cmdbuf_id*CMDBUF_MAX_SIZE+(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_L2Cache_addr/2+0);
        //read L2cache IP first register
        *(set_base_addr+8) = (OPCODE_RREG) |(1<<16) |(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_L2Cache_addr+0);
        counter_cmdbuf_size += 4;
        if (mmu_enable) {
          *(set_base_addr+9) = map_status_base_phy_addr;
        } else {
          *(set_base_addr+9) = (u32)(status_base_phy_addr-base_ddr_addr);
        }
        counter_cmdbuf_size += 4;
        if (mmu_enable) {
          *(set_base_addr+10) = 0;
        } else {
          if(sizeof(size_t) == 8) {
            *(set_base_addr+10) = (u32)((u64)(status_base_phy_addr-base_ddr_addr)>>32);
          } else {
            *(set_base_addr+10) = 0;
          }
				}
        counter_cmdbuf_size += 4;
        //alignment
        *(set_base_addr+11) = 0;
        counter_cmdbuf_size += 4;
      }
      if(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_dec400_addr != 0xffff)
      {
        //read dec400 register
        offset_inc_dec400 = 4;
        status_base_phy_addr=vcmd_status_buf_mem_pool.busAddress + input_para->cmdbuf_id*CMDBUF_MAX_SIZE+(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_dec400_addr/2+0);
        map_status_base_phy_addr=vcmd_status_buf_mem_pool.mmu_bus_address + input_para->cmdbuf_id*CMDBUF_MAX_SIZE+(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_dec400_addr/2+0);
        //read DEC400 IP first register
        *(set_base_addr+8+offset_inc) = (OPCODE_RREG) |(0x2b<<16) |(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_dec400_addr+0);
        counter_cmdbuf_size += 4;
        if (mmu_enable)
        {
          *(set_base_addr+9+offset_inc) = map_status_base_phy_addr;
        }
        else
        {
          *(set_base_addr+9+offset_inc) = (u32)(status_base_phy_addr-base_ddr_addr);
        }
        counter_cmdbuf_size += 4;
        if (mmu_enable)
        {
          *(set_base_addr+10+offset_inc) = 0;
        }
        else
        {
          if(sizeof(size_t) == 8)
          {
            *(set_base_addr+10+offset_inc) = (u32)((u64)(status_base_phy_addr-base_ddr_addr)>>32);
          }
          else
          {
            *(set_base_addr+10+offset_inc) = 0;
          }
        }
        counter_cmdbuf_size += 4;
        //alignment
        *(set_base_addr+11+offset_inc) = 0;
        counter_cmdbuf_size += 4;
      }
      
#if 0
      //INT code, interrupt immediately
      *(set_base_addr+4) = (OPCODE_INT) |0 |input_para->cmdbuf_id;
      counter_cmdbuf_size += 4;
      //alignment
      *(set_base_addr+5) = 0;
      counter_cmdbuf_size += 4;
#endif
      //read vcmd registers to ddr
      *(set_base_addr+8+offset_inc + offset_inc_dec400) = (OPCODE_RREG) |(27<<16) |(0);
      counter_cmdbuf_size += 4;
      *(set_base_addr+9+offset_inc + offset_inc_dec400) = (u32)0; //will be changed in link stage
      counter_cmdbuf_size += 4;
      *(set_base_addr+10+offset_inc + offset_inc_dec400) = (u32)0; //will be changed in link stage
      counter_cmdbuf_size += 4;
      //alignment
      *(set_base_addr+11+offset_inc + offset_inc_dec400) = 0;
      counter_cmdbuf_size += 4;
      //JMP RDY = 0
      *(set_base_addr +12+offset_inc + offset_inc_dec400)= (OPCODE_JMP_RDY0) |0 |JMP_IE_1|0;
      counter_cmdbuf_size += 4;
      *(set_base_addr +13+offset_inc + offset_inc_dec400) = 0;
      counter_cmdbuf_size += 4;
      *(set_base_addr +14+offset_inc + offset_inc_dec400) = 0;
      counter_cmdbuf_size += 4;
      *(set_base_addr +15+offset_inc + offset_inc_dec400) = input_para->cmdbuf_id;
      //don't add the last alignment DWORD in order to  identify END command or JMP command.
      //counter_cmdbuf_size += 4;
      input_para->cmdbuf_size=(16+offset_inc + offset_inc_dec400)*4;
    }
    else
    {
      pr_info("vc8000_vcmd_driver:create cmdbuf data when hw_version_id = 0x%x\n",vcmd_manager[input_para->module_type][0]->hw_version_id);
      //read all registers
      *(set_base_addr+0) = (OPCODE_RREG) |((register_range[input_para->module_type]/4)<<16) |(vcmd_manager[input_para->module_type][0]->vcmd_core_cfg.submodule_main_addr+0);
      counter_cmdbuf_size += 4;
      if (mmu_enable) {
        *(set_base_addr+1) = map_status_base_phy_addr;
      } else {
        *(set_base_addr+1) = (u32)(status_base_phy_addr-base_ddr_addr);
      }
      counter_cmdbuf_size += 4;
      if (mmu_enable) {
        *(set_base_addr+2) = 0;
      } else {
        if(sizeof(size_t) == 8) {
          *(set_base_addr+2) = (u32)((u64)(status_base_phy_addr-base_ddr_addr)>>32);
        } else {
          *(set_base_addr+2) = 0;
        }
      }
      counter_cmdbuf_size += 4;
      //alignment
      *(set_base_addr+3) = 0;
      counter_cmdbuf_size += 4;
#if 0
      //INT code, interrupt immediately
      *(set_base_addr+4) = (OPCODE_INT) |0 |input_para->cmdbuf_id;
      counter_cmdbuf_size += 4;
      //alignment
      *(set_base_addr+5) = 0;
      counter_cmdbuf_size += 4;
#endif
      //JMP RDY = 0
      *(set_base_addr +4)= (OPCODE_JMP_RDY0) |0 |JMP_IE_1|0;
      counter_cmdbuf_size += 4;
      *(set_base_addr +5) = 0;
      counter_cmdbuf_size += 4;
      *(set_base_addr +6) = 0;
      counter_cmdbuf_size += 4;
      *(set_base_addr +7) = input_para->cmdbuf_id;
      //don't add the last alignment DWORD in order to  identify END command or JMP command.
      //counter_cmdbuf_size += 4;
      input_para->cmdbuf_size=8*4;
    }

}
static void read_main_module_all_registers(u32 main_module_type)
{
  int ret;
  struct exchange_parameter input_para;
  u32 irq_status_ret=0;
  u32 *status_base_virt_addr;

  input_para.executing_time=0;
  input_para.priority=CMDBUF_PRIORITY_NORMAL;
  input_para.module_type = main_module_type;
  input_para.cmdbuf_size=0;
  ret = reserve_cmdbuf(NULL,&input_para);
  vcmd_manager[main_module_type][0]->status_cmdbuf_id = input_para.cmdbuf_id;
  create_read_all_registers_cmdbuf(&input_para);
  link_and_run_cmdbuf(NULL,&input_para);
  //msleep(1000);
  hantrovcmd_isr(input_para.core_id, &hantrovcmd_data[input_para.core_id]);
  wait_cmdbuf_ready(NULL,input_para.cmdbuf_id,&irq_status_ret);

  pr_info("%s:record_idle:busy_count = %d\n",__func__,encoder_get_devfreq_priv_data()->busy_count);
  status_base_virt_addr=vcmd_status_buf_mem_pool.virtualAddress + input_para.cmdbuf_id*CMDBUF_MAX_SIZE/4+(vcmd_manager[input_para.module_type][0]->vcmd_core_cfg.submodule_main_addr/2/4+0);
  pr_info("vc8000_vcmd_driver: main module register 0:0x%x\n",*status_base_virt_addr);
  pr_info("vc8000_vcmd_driver: main module register 80:0x%x\n",*(status_base_virt_addr+80));
  pr_info("vc8000_vcmd_driver: main module register 214:0x%x\n",*(status_base_virt_addr+214));
  pr_info("vc8000_vcmd_driver: main module register 226:0x%x\n", *(status_base_virt_addr+226));
  pr_info("vc8000_vcmd_driver: main module register 287:0x%x\n", *(status_base_virt_addr+287));
  //don't release cmdbuf because it can be used repeatedly
  //release_cmdbuf(input_para.cmdbuf_id);
}

/*------------------------------------------------------------------------------
 Function name   : vcmd_pcie_init
 Description     : Initialize PCI Hw access

 Return type     : int
 ------------------------------------------------------------------------------*/
static int vcmd_init(struct platform_device *pdev)
{
    struct pci_dev *g_vcmd_dev = NULL;    /* PCI device structure. */
    unsigned long g_vcmd_base_ddr_hw;     /* PCI base register address (memalloc) */

    vcmd_buf_mem_pool.virtualAddress = dma_alloc_coherent(&pdev->dev, CMDBUF_POOL_TOTAL_SIZE*2 + CMDBUF_VCMD_REGISTER_TOTAL_SIZE,
		                             &vcmd_buf_mem_pool.busAddress, GFP_KERNEL | GFP_DMA);

    pr_info("Base memory val 0x%llx\n", vcmd_buf_mem_pool.busAddress);

    vcmd_buf_mem_pool.size =CMDBUF_POOL_TOTAL_SIZE;
    pr_info("Init: vcmd_buf_mem_pool.busAddress=0x%llx.\n",(long long unsigned int)vcmd_buf_mem_pool.busAddress);

    if (vcmd_buf_mem_pool.virtualAddress == NULL ) {
      pr_info("Init: failed to ioremap.\n");
      return -1;
    }
    pr_info("Init: vcmd_buf_mem_pool.virtualAddress=0x%llx.\n",(long long unsigned int)vcmd_buf_mem_pool.virtualAddress);

    vcmd_status_buf_mem_pool.busAddress = vcmd_buf_mem_pool.busAddress+CMDBUF_POOL_TOTAL_SIZE;
    vcmd_status_buf_mem_pool.virtualAddress = (void *)vcmd_buf_mem_pool.virtualAddress+CMDBUF_POOL_TOTAL_SIZE;
    vcmd_status_buf_mem_pool.size =CMDBUF_POOL_TOTAL_SIZE;
    pr_info("Init: vcmd_status_buf_mem_pool.busAddress=0x%llx.\n",(long long unsigned int)vcmd_status_buf_mem_pool.busAddress);

    if (vcmd_status_buf_mem_pool.virtualAddress == NULL ) {
      pr_info("Init: failed to ioremap.\n");
      return -1;
    }
    pr_info("Init: vcmd_status_buf_mem_pool.virtualAddress=0x%llx.\n",(long long unsigned int)vcmd_status_buf_mem_pool.virtualAddress);

    vcmd_registers_mem_pool.busAddress = vcmd_buf_mem_pool.busAddress+CMDBUF_POOL_TOTAL_SIZE*2;
    vcmd_registers_mem_pool.virtualAddress = (void *)vcmd_buf_mem_pool.virtualAddress+CMDBUF_POOL_TOTAL_SIZE*2;
    vcmd_registers_mem_pool.size =CMDBUF_VCMD_REGISTER_TOTAL_SIZE;
    pr_info("Init: vcmd_registers_mem_pool.busAddress=0x%llx.\n",(long long unsigned int)vcmd_registers_mem_pool.busAddress);

    if (vcmd_registers_mem_pool.virtualAddress == NULL ) {
      pr_info("Init: failed to ioremap.\n");
      return -1;
    }
    pr_info("Init: vcmd_registers_mem_pool.virtualAddress=0x%llx.\n",(long long unsigned int)vcmd_registers_mem_pool.virtualAddress);

    return 0;

out_pci_disable_device:
    pci_disable_device(g_vcmd_dev);
out:
    return -1;
}

static ssize_t encoder_config_write(struct file *filp,
				const char __user *userbuf,
				size_t count, loff_t *ppos)
{
  struct hantrovcmd_dev *dev = hantrovcmd_data;
	unsigned long value;
	int ret;

	if (count > VC8000E_MAX_CONFIG_LEN)
		count = VC8000E_MAX_CONFIG_LEN;
	else if (count <= 2)
		return 0;

	ret = copy_from_user(dev->config_buf, userbuf, count);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	//pr_info("hantrodec config: %s\n", dev->config_buf);
	switch (dev->config_buf[0]) {
		case 'd':
			value = simple_strtoul(&(dev->config_buf[1]), NULL, 10);
			pm_runtime_set_autosuspend_delay(&dev->pdev->dev, value);
			pr_info("Set pm runtime auto suspend delay to %ldms\n", value);
			break;
		case 'p':
			if (strncmp(&(dev->config_buf[0]),"pfreq-en",8) == 0)
			{
				debug_pr_devfreq_info = 1;
			}
			else if (strncmp(&(dev->config_buf[0]),"pfreq-dis",9) == 0)
			{
				debug_pr_devfreq_info = 0;
			}
			pr_info("cmd %s set debug_pr_devfreq_info %ld \n",&(dev->config_buf[0]),debug_pr_devfreq_info);
			break;
    default:
			pr_warn("Unsupported config!\n");
	}

out:
	return ret < 0 ? ret : count;
}

static ssize_t encoder_config_read(struct file *filp,
				char __user *userbuf,
				size_t count, loff_t *ppos)
{
  struct hantrovcmd_dev *dev = hantrovcmd_data;
	memset(dev->config_buf, 0, VC8000E_MAX_CONFIG_LEN);
	return 0;
}

static const struct file_operations encoder_debug_ops = {
	.write	= encoder_config_write,
	.read	= encoder_config_read,
	.open	= simple_open,
	.llseek	= generic_file_llseek,
};

static int encoder_add_debugfs(struct platform_device *pdev)
{
	root_debugfs_dir = debugfs_create_dir("vc8000e",NULL);
	if (!root_debugfs_dir) {
		dev_err(&pdev->dev, "Failed to create vc8000e debugfs\n");
		return -EINVAL;
	}

  dev_info(&pdev->dev, "Create vc8000e debugfs.\n");

	debugfs_create_file("config", 0600, root_debugfs_dir,
		hantrovcmd_data, &encoder_debug_ops);
	return 0;
}


/*-----------------------------------------
 platform register
-----------------------------------------*/

static const struct of_device_id hantro_of_match[] = {
        { .compatible = "thead,light-vc8000e",  },
        { .compatible = "xuantie,th1520-vc8000e",  },
        { /* sentinel */  },
};

static int check_power_domain(void)
{
	struct device_node *dn = NULL;
	struct property *info = NULL;
	dn = of_find_node_by_name(NULL, "venc");
	if (dn != NULL)
		info = of_find_property(dn, "power-domains", NULL);
	pr_debug("%s, %d: power gating is %s\n", __func__, __LINE__, 
		(info == NULL) ? "disabled" : "enabled");
	return (info == NULL) ? 0 : 1;
}

static int encoder_runtime_suspend(struct device *dev)
{
	struct hantrovcmd_dev *encdev = hantrovcmd_data;

	pr_debug("%s, %d: Disable clock\n", __func__, __LINE__);

	clk_disable_unprepare(encdev->cclk);
	clk_disable_unprepare(encdev->aclk);
	clk_disable_unprepare(encdev->pclk);
	encoder_devfreq_set_rate(dev); //for the last set rate request,may not handled in cmd
	encoder_devfreq_suspend(encoder_get_devfreq_priv_data());
	return 0;
}

static int encoder_runtime_resume(struct device *dev)
{
	struct hantrovcmd_dev *encdev = hantrovcmd_data;
	int ret;

	ret = clk_prepare_enable(encdev->cclk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable core clock\n");
		return ret;
	}

	ret = clk_prepare_enable(encdev->aclk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable axi clock\n");
		clk_disable_unprepare(encdev->cclk);
		return ret;
	}

	ret = clk_prepare_enable(encdev->pclk);
	if (ret < 0) {
		dev_err(dev, "could not prepare or enable apb clock\n");
		clk_disable_unprepare(encdev->cclk);
		clk_disable_unprepare(encdev->aclk);
		return ret;
	}

	if (encdev->has_power_domains) {
#ifdef HANTROMMU_SUPPORT
		MMURestore(mmu_hwregs);
#endif
		vcmd_reset();
	}
	pr_debug("%s, %d: Enabled clock\n", __func__, __LINE__);
  encoder_devfreq_resume(encoder_get_devfreq_priv_data());

	return 0;
}

void encoder_vcmd_suspend_record(void)
{
    if (!hantrovcmd_data) {
      return;
    }
    int i;
    struct hantrovcmd_dev* dev = NULL;
    unsigned long flags;
    struct cmdbuf_obj* cmdbuf_obj;
    int timeout = 100;
    for(i=0;i<total_vcmd_core_num;i++) {
        dev = &hantrovcmd_data[i];
        if(!dev)
        {
          pr_info("%s: get core %d vcmd data null\n",__func__,i);
          continue;
        }

        //when suspend working state is WORKING,record to suspend_running_cmdbuf_node for resume use
        if (dev->working_state == WORKING_STATE_WORKING )
          dev->suspend_running_cmdbuf_node = dev->last_linked_cmdbuf_node;
        else
          dev->suspend_running_cmdbuf_node = NULL;

        dev->suspend_entered = true;
        if(dev->last_linked_cmdbuf_node)
        {
          cmdbuf_obj = (struct cmdbuf_obj*)(dev->last_linked_cmdbuf_node->data);
          while(timeout--){
            if(cmdbuf_obj->cmdbuf_run_done)
              break;
            udelay(1000);
          }
        }
        pr_info("%s:  core %d working state %s ,node %px \n",__func__,i,
          dev->working_state == WORKING_STATE_WORKING ? "working":"idle",
          dev->suspend_running_cmdbuf_node);
    }
}

int encoder_vcmd_resume_start(void)
{
    if (!hantrovcmd_data) {
      return 0;
    }
    int i;
    struct hantrovcmd_dev* dev = NULL;
    bi_list_node* last_cmdbuf_node;
    int ret;
    for(i=0;i<total_vcmd_core_num;i++) {
        dev = &hantrovcmd_data[i];
        if(!dev)
        {
          pr_info("%s: get core %d vcmd data null\n",__func__,i);
          continue;
        }
        //when suspend working state is WORKING, reset to IDLE as well,we will start new
        dev->working_state = WORKING_STATE_IDLE;
        last_cmdbuf_node = dev->suspend_running_cmdbuf_node;
        if(last_cmdbuf_node)
        {
          //run
          while (last_cmdbuf_node &&
                ((struct cmdbuf_obj*)last_cmdbuf_node->data)->cmdbuf_run_done)
            last_cmdbuf_node = last_cmdbuf_node->next;

          if (last_cmdbuf_node && last_cmdbuf_node->data) {
            pr_info("vcmd start for cmdbuf id %d, cmdbuf_run_done = %d\n",
                  ((struct cmdbuf_obj*)last_cmdbuf_node->data)->cmdbuf_id,
                  ((struct cmdbuf_obj*)last_cmdbuf_node->data)->cmdbuf_run_done);
            ret = pm_runtime_resume_and_get(&hantrovcmd_data[0].pdev->dev);
            if(ret < 0)
              return ret;
            vcmd_start(dev,last_cmdbuf_node);
          }

        }
        dev->suspend_entered = false;
        dev->suspend_running_cmdbuf_node = NULL;
    }
  return 0;
}

static int encoder_suspend(struct device *dev)
{
	pr_info("%s, %d: enter\n", __func__, __LINE__);
	encoder_vcmd_suspend_record();
	/*pm_runtime_force_suspend will check current clk state*/
	return pm_runtime_force_suspend(dev);

}

static int encoder_resume(struct device *dev)
{
	int ret;
	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;
	ret = encoder_vcmd_resume_start();
	pr_info("%s, %d: exit resume\n", __func__, __LINE__);

	return ret;
}

/******************************************************************************\
******************************* VPU Devfreq support START***********************
\******************************************************************************/

static void encoder_devfreq_update_utilization(struct encoder_devfreq *devfreq)
{
    ktime_t now, last;
    ktime_t busy;
    now = ktime_get();
    last = devfreq->time_last_update;

    if (devfreq->busy_count > 0) {
          busy = ktime_sub(now, last);
          devfreq->busy_time += busy;
          #ifndef CONFIG_PM_DEVFREQ
            devfreq->based_maxfreq_last_busy_t = busy;
          #else
          if(devfreq->max_freq)
              devfreq->based_maxfreq_last_busy_t = busy/(devfreq->max_freq/devfreq->cur_devfreq);
          else
              devfreq->based_maxfreq_last_busy_t = busy;
          #endif
          devfreq->based_maxfreq_busy_time +=  devfreq->based_maxfreq_last_busy_t;
          devfreq->busy_record_count++;
    }
    else
    {
      if(devfreq->busy_time > 0)  //if first time in not recorded busy time,ignore idle time.
          devfreq->idle_time += ktime_sub(now, last);
    }
    devfreq->time_last_update = now;
}

static void encoder_devfreq_reset(struct encoder_devfreq *devfreq)
{
    devfreq->busy_time = 0;
    devfreq->idle_time = 0;
    devfreq->time_last_update = ktime_get();
}

void encoder_devfreq_reset_profile_record(struct encoder_devfreq *devfreq)
{
    devfreq->based_maxfreq_busy_time = 0;
    devfreq->busy_record_count = 0;
}

void encoder_devfreq_record_busy(struct encoder_devfreq *devfreq)
{
    unsigned long irqflags;
    int busy_count;
    if (!devfreq)
      return;
    //when devfreq not enabled,need into record time also.

    encoder_dev_clk_lock();
    spin_lock_irqsave(&devfreq->lock, irqflags);
    busy_count = devfreq->busy_count;
    PDEBUG("record_busy:busy_count = %d\n",busy_count);
    if(devfreq->busy_count > 0)
    { 
        devfreq->busy_count++;
        spin_unlock_irqrestore(&devfreq->lock, irqflags);
        encoder_dev_clk_unlock();
        return;
    }

    encoder_devfreq_update_utilization(devfreq);

    devfreq->busy_count++;

    spin_unlock_irqrestore(&devfreq->lock, irqflags);

    if(!busy_count)
        encoder_devfreq_set_rate(&hantrovcmd_data->pdev->dev);

    encoder_dev_clk_unlock();
}

void encoder_devfreq_record_idle(struct encoder_devfreq *devfreq)
{
    unsigned long irqflags;

    if (!devfreq)
      return;

    spin_lock_irqsave(&devfreq->lock, irqflags);
    PDEBUG("record_idle:busy_count = %d\n",devfreq->busy_count);
    if(devfreq->busy_count > 1)
    { 
      devfreq->busy_count--;
      spin_unlock_irqrestore(&devfreq->lock, irqflags);
      return;
    }

    encoder_devfreq_update_utilization(devfreq);

    WARN_ON(--devfreq->busy_count < 0);

    spin_unlock_irqrestore(&devfreq->lock, irqflags);
#ifdef DEV_FREQ_DEBUG    
    unsigned long busy_time,idle_time;
    busy_time = ktime_to_us(devfreq->busy_time);
    idle_time = ktime_to_us(devfreq->idle_time);
    pr_info("busy %lu idle %lu %lu %%  \n",
        busy_time,idle_time,
        busy_time / ( (idle_time+busy_time) / 100) );
#endif
}
struct encoder_devfreq * encoder_get_devfreq_priv_data(void)
{
    return &hantrovcmd_data->devfreq;
}
/* only reset record time now */
int encoder_devfreq_resume(struct encoder_devfreq *devfreq)
{
    unsigned long irqflags;

    if (!devfreq->df)
        return 0;

    spin_lock_irqsave(&devfreq->lock, irqflags);
    devfreq->busy_count = 0;//need reset avoid up
    encoder_devfreq_reset(devfreq);

    spin_unlock_irqrestore(&devfreq->lock, irqflags);

    return devfreq_resume_device(devfreq->df);
}

int encoder_devfreq_suspend(struct encoder_devfreq *devfreq)
{
    if (!devfreq->df)
        return 0;
    wake_up_all(&devfreq->target_freq_wait_queue);
    return devfreq_suspend_device(devfreq->df);
}

void encoder_dev_clk_lock(void)
{
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
    if(!devfreq->df)
       return;
    mutex_lock(&devfreq->clk_mutex);
}

void encoder_dev_clk_unlock(void)
{
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
    if(!devfreq->df)
       return;
    mutex_unlock(&devfreq->clk_mutex);
}


/* set rate need clk disabled,so carefully calling this function
 * which will disabled clk
*/
int encoder_devfreq_set_rate(struct device * dev)
{
    int ret;
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
    struct hantrovcmd_dev *encdev = hantrovcmd_data;
    if (!devfreq->df)
        return 0;
    if(!devfreq->update_freq_flag)
        return 0;
    if(debug_pr_devfreq_info)
        pr_info("start set rate %ldMHz \n",devfreq->next_target_freq/1000/1000);
    if( !hantrovcmd_devfreq_check_state() ) {
        pr_info("devfreq check state not ok\n");
        return 0;
    }
    clk_disable_unprepare(encdev->cclk);
    ret = dev_pm_opp_set_rate(dev, devfreq->next_target_freq);
    if(ret) {
        pr_err("set rate %ld MHz failed \n",devfreq->next_target_freq/1000/1000);
    } else {
        devfreq->cur_devfreq = devfreq->next_target_freq;
    }
    devfreq->update_freq_flag = false;
    wake_up_all(&devfreq->target_freq_wait_queue);
    ret = clk_prepare_enable(encdev->cclk);

    if(debug_pr_devfreq_info)
        pr_info("finished set rate \n");
    if (ret < 0) {
        dev_err(dev, "could not prepare or enable core clock\n");
        return ret;
    }
    return 0;
}

static int encoder_devfreq_target(struct device * dev, unsigned long *freq, u32 flags)
{
    int ret;
    struct dev_pm_opp *opp;
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
    opp = devfreq_recommended_opp(dev, freq, flags);
    if (IS_ERR(opp)) {
        dev_info(dev, "Failed to find opp for %lu Hz\n", *freq);
        return PTR_ERR(opp);
    }
    dev_pm_opp_put(opp);
    
    devfreq->next_target_freq = *freq;
    if(*freq != devfreq->cur_devfreq)
    {
        devfreq->update_freq_flag = true;
        if( !wait_event_timeout( devfreq->target_freq_wait_queue, (!devfreq->update_freq_flag),
                msecs_to_jiffies(100) ) )
        {
            if(debug_pr_devfreq_info) //usually last req,but all vcmd done
                dev_info(dev,"devfreq target freq set : wait queue timeout\n");
        }
    }
     
    return 0;
}

static int encoder_devfreq_get_status(struct device *dev, struct devfreq_dev_status *stat)
{
    unsigned long irqflags;
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
    stat->current_frequency = devfreq->cur_devfreq;
    spin_lock_irqsave(&devfreq->lock, irqflags);

    encoder_devfreq_update_utilization(devfreq);

    stat->total_time = ktime_to_ns(ktime_add(devfreq->busy_time,
                devfreq->idle_time));
    stat->busy_time = ktime_to_ns(devfreq->busy_time);

    encoder_devfreq_reset(devfreq);

    spin_unlock_irqrestore(&devfreq->lock, irqflags);

    if(debug_pr_devfreq_info){
        dev_info(dev, "busy %lu total %lu %lu %% freq %lu MHz\n",
            stat->busy_time/1000, stat->total_time/1000,
            stat->busy_time / (stat->total_time / 100),
            stat->current_frequency / 1000 / 1000);
    }
    return 0;
}

static int encoder_devfreq_get_cur_freq( struct device *dev, unsigned long *freq)
{
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
    *freq = devfreq->cur_devfreq;
    return 0;
}
#ifdef CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND
struct devfreq_simple_ondemand_data encoder_gov_data;
#endif

static struct devfreq_dev_profile encoder_devfreq_gov_data =
{
    .polling_ms = 100,
    .target = encoder_devfreq_target,
    .get_dev_status = encoder_devfreq_get_status,
    .get_cur_freq = encoder_devfreq_get_cur_freq,
};

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
static struct monitor_dev_profile encoder_dev_monitor = {
	.type = MONITOR_TPYE_DEV,
};
#endif

void encoder_devfreq_fini(struct device *dev)
{
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
#ifdef CONFIG_TH1520_SYSTEM_MONITOR
    if(devfreq->mdev_info)
      th1520_system_monitor_unregister(devfreq->mdev_info);
#endif

    if (devfreq->df) {
        devm_devfreq_remove_device(dev, devfreq->df);
        devfreq->df = NULL;
    }

    if (devfreq->opp_of_table_added) {
        dev_pm_opp_of_remove_table(dev);
        devfreq->opp_of_table_added = false;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
    if (devfreq->clkname_opp_table) {
        dev_pm_opp_put_clkname(devfreq->clkname_opp_table);
        devfreq->clkname_opp_table = NULL;
    }
#else
    if(devfreq->token >= 0)
      dev_pm_opp_put_clkname(devfreq->token);
#endif
    mutex_destroy(&devfreq->clk_mutex);
}

int encoder_devfreq_init(struct device *dev)
{
    struct devfreq *df;
    struct clk *new_clk;
    struct opp_table *opp_table;
    int ret = 0;
    struct encoder_devfreq *devfreq = encoder_get_devfreq_priv_data();
    
    memset(devfreq,0,sizeof(struct encoder_devfreq));
    spin_lock_init(&devfreq->lock);
    init_waitqueue_head(&devfreq->target_freq_wait_queue);
    mutex_init(&devfreq->clk_mutex);

#ifdef CONFIG_PM_DEVFREQ
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
    opp_table = dev_pm_opp_set_clkname(dev,"cclk");
    if(IS_ERR(opp_table)) {
        pr_err("enc set cclk failed\n");
        ret = PTR_ERR(opp_table);
		    goto err_fini;
    }
    devfreq->clkname_opp_table = opp_table;
#else
    devfreq->token = dev_pm_opp_set_clkname(dev,"cclk");
    if (devfreq->token < 0) {
        pr_err("enc set cclk failed\n");
        ret = devfreq->token;
        goto err_fini;
    }
#endif

    ret = dev_pm_opp_of_add_table(dev);
    if(ret) {
        pr_info("enc opp table not found in dtb\n");
        goto err_fini;
    }
    devfreq->opp_of_table_added = true;

    new_clk = devm_clk_get(dev, "cclk");

    devfreq->cur_devfreq = clk_get_rate(new_clk);

    encoder_devfreq_gov_data.initial_freq = devfreq->cur_devfreq;

#ifdef CONFIG_DEVFREQ_GOV_SIMPLE_ONDEMAND
    encoder_gov_data.upthreshold = 80;
    encoder_gov_data.downdifferential = 10;

    df = devm_devfreq_add_device(dev,
                &encoder_devfreq_gov_data,
                DEVFREQ_GOV_SIMPLE_ONDEMAND,
                &encoder_gov_data);
    
    if(IS_ERR(df)) {
        pr_err("Error: init devfreq %lx %ld\n", (unsigned long)dev,(long)df);
        devfreq->df = NULL;
        ret = PTR_ERR(df);
        goto err_fini;
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
    unsigned long *freq_table = df->profile->freq_table;
    if (freq_table[0] < freq_table[df->profile->max_state - 1]) {
        devfreq->max_freq = freq_table[df->profile->max_state - 1];
    } else {
        devfreq->max_freq  = freq_table[0];
    }
#else
    unsigned long *freq_table = df->freq_table;
    if (freq_table[0] < freq_table[df->max_state - 1]) {
        devfreq->max_freq = freq_table[df->max_state - 1];
    } else {
        devfreq->max_freq  = freq_table[0];
    }
#endif
    pr_info("device max freq %ld\n",devfreq->max_freq);
    df->suspend_freq = 0; // not set freq when suspend,not suitable for async set rate
    devfreq->df = df;

#ifdef CONFIG_TH1520_SYSTEM_MONITOR
    encoder_dev_monitor.data = devfreq->df;
    devfreq->mdev_info = th1520_system_monitor_register(dev, &encoder_dev_monitor);
    if (IS_ERR(devfreq->mdev_info))
      devfreq->mdev_info = NULL;
#endif

#endif
#endif  
    return 0;

err_fini:
    encoder_devfreq_fini(dev);
    return ret;
}

/******************************************************************************\
******************************* VPU Devfreq support END ************************
\******************************************************************************/

void venc_vcmd_profile_update(struct work_struct *work);
static DECLARE_DELAYED_WORK(venc_cmd_profile_work,venc_vcmd_profile_update);
static ktime_t last_update;
static long update_period_ms = 0;

struct vcmd_profile venc_vcmd_profile;

void venc_vcmd_profile_update(struct work_struct *work)
{
    //update busy time
    ktime_t now,during;
    struct encoder_devfreq *devfreq;
    devfreq = encoder_get_devfreq_priv_data();
    now = ktime_get();
    during = ktime_sub(now,last_update);
    last_update = now;
    venc_vcmd_profile.dev_loading_percent = ktime_to_us(devfreq->based_maxfreq_busy_time) * 100/ktime_to_us(during);
    if(venc_vcmd_profile.dev_loading_percent > venc_vcmd_profile.dev_loading_max_percent)
      venc_vcmd_profile.dev_loading_max_percent = venc_vcmd_profile.dev_loading_percent;
    pr_debug("based_maxfreq_busy_time %lldms,during period %lld ms",ktime_to_us(devfreq->based_maxfreq_busy_time)/1000,ktime_to_ms(during));

    if(devfreq->busy_record_count > 0)
      venc_vcmd_profile.avg_hw_proc_us = ktime_to_us(devfreq->based_maxfreq_busy_time)/devfreq->busy_record_count;
    else
      venc_vcmd_profile.avg_hw_proc_us = 0;

    venc_vcmd_profile.last_hw_proc_us = ktime_to_us(devfreq->based_maxfreq_last_busy_t);
    venc_vcmd_profile.proced_count  = devfreq->busy_record_count;
    encoder_devfreq_reset_profile_record(devfreq);
    if(update_period_ms > 0)
      schedule_delayed_work(&venc_cmd_profile_work, msecs_to_jiffies(update_period_ms));
}

static ssize_t log_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    ssize_t len = 0;

    const char *module_version = "1.0.0";
    int dev_id = 0;


    len += scnprintf(buf + len, PAGE_SIZE - len,
        "[VENC] Version: %s \n"
        "----------------------------------------MODULE PARAM-----------------------------\n"
        "updatePeriod_ms\n"
        "       %d\n"
        "----------------------------------------MODULE STATUS------------------------------\n"
        "DevId         DevLoading_%%   DevLoadingMax_%%\n"
        " %d                 %d               %d\n"

        " avg_hw_proc_us     last_hw_proc_us    proced_count\n"
        " %d                       %d                   %d \n"
        "cur_submit_vcmd     cur_complete_vcmd   vcmd_num_share_irq\n"
        " %d                       %d                   %d \n"
        "----------------------------------------EXCEPTION INFO-----------------------------------------\n"
        "BusErr    Abort     Timeout    CmdErr\n"
        " %d         %d         %d        %d \n",
        module_version, update_period_ms,
        dev_id, venc_vcmd_profile.dev_loading_percent, venc_vcmd_profile.dev_loading_max_percent,

        venc_vcmd_profile.avg_hw_proc_us, venc_vcmd_profile.last_hw_proc_us,venc_vcmd_profile.proced_count,
        venc_vcmd_profile.cur_submit_vcmd_id,venc_vcmd_profile.cur_complete_vcmd_id,venc_vcmd_profile.vcmd_num_share_irq,
        venc_vcmd_profile.vcmd_buserr_cnt, venc_vcmd_profile.vcmd_abort_cnt, venc_vcmd_profile.vcmd_timeout_cnt, venc_vcmd_profile.vcmd_cmderr_cnt);


    return len;
}

static ssize_t log_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    /******************clear *********************/
    venc_vcmd_profile.vcmd_buserr_cnt = 0;
    venc_vcmd_profile.vcmd_abort_cnt  = 0;
    venc_vcmd_profile.vcmd_timeout_cnt = 0;
    venc_vcmd_profile.vcmd_cmderr_cnt = 0;

    venc_vcmd_profile.dev_loading_max_percent = 0;
    venc_vcmd_profile.last_hw_proc_us = 0;
    return count;
}

/******************updatePeriod ************************************/
static ssize_t updatePeriod_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sprintf(buf,"%u\n",update_period_ms);
}

static ssize_t updatePeriod_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    char *start = (char *)buf;
    long old_period = update_period_ms;
    update_period_ms = simple_strtoul(start, &start, 0);
    if(old_period == 0 && update_period_ms)
      schedule_delayed_work(&venc_cmd_profile_work,msecs_to_jiffies(update_period_ms));
    return count;
}
/******************define log *************************************/
static struct kobj_attribute log_attr = __ATTR(log, 0664, log_show, log_store);
/******************define updatePeriod_ms*************************************/
static struct kobj_attribute updatePeriod_attr = __ATTR(updatePeriod_ms, 0664, updatePeriod_show, updatePeriod_store);

static struct attribute *attrs[] = {
    &log_attr.attr,
    &updatePeriod_attr.attr,
    NULL,   // must be NULL
};

static struct attribute_group venc_dev_attr_group = {
    .name = "info", // dir name
    .attrs = attrs,
};

int __init hantroenc_vcmd_probe(struct platform_device *pdev)
{
  int i,k;
  int result;

  struct resource *mem;
  mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
  if(mem->start)
    vcmd_core_array[0].vcmd_base_addr = mem->start; 
  vcmd_core_array[0].vcmd_irq = platform_get_irq(pdev,0);
  pr_info("%s:get irq %d\n",__func__,vcmd_core_array[0].vcmd_irq);

  result = vcmd_init(pdev);
  if(result)
     goto err;
  
  total_vcmd_core_num = 1;
  for (i = 0; i< total_vcmd_core_num; i++)
  {
    pr_info("vcmd: module init - vcmdcore[%d] addr =0x%llx\n",i,
           (long long unsigned int)vcmd_core_array[i].vcmd_base_addr);
  }
  hantrovcmd_data = (struct hantrovcmd_dev *)vmalloc(sizeof(struct hantrovcmd_dev)*total_vcmd_core_num);
  if (hantrovcmd_data == NULL)
    goto err1;
  memset(hantrovcmd_data,0,sizeof(struct hantrovcmd_dev)*total_vcmd_core_num);
  for(k=0;k<MAX_VCMD_TYPE;k++)
  {
    vcmd_type_core_num[k]=0;
    vcmd_position[k]=0;
    for(i=0;i<MAX_VCMD_NUMBER;i++)
    {
      vcmd_manager[k][i]=NULL;
    }
  }

  hantrovcmd_data->pdev = pdev;

  encoder_add_debugfs(pdev);

  hantrovcmd_data->has_power_domains = check_power_domain();

  hantrovcmd_data->aclk = devm_clk_get(&pdev->dev, "aclk");
  if (IS_ERR(hantrovcmd_data->aclk)) {
      dev_err(&pdev->dev, "failed to get axi clock\n");
      goto err;
  }

  hantrovcmd_data->cclk = devm_clk_get(&pdev->dev, "cclk");
  if (IS_ERR(hantrovcmd_data->cclk)) {
      dev_err(&pdev->dev, "failed to get core clock\n");
      goto err;
  }

  hantrovcmd_data->pclk = devm_clk_get(&pdev->dev, "pclk");
  if (IS_ERR(hantrovcmd_data->pclk)) {
      dev_err(&pdev->dev, "failed to get apb clock\n");
      goto err;
  }

  pm_runtime_set_autosuspend_delay(&pdev->dev, VC8000E_PM_TIMEOUT);
  pm_runtime_use_autosuspend(&pdev->dev);
  pm_runtime_enable(&pdev->dev);
  if (!pm_runtime_enabled(&pdev->dev)) {
    if (encoder_runtime_resume(&pdev->dev))
    {
      pm_runtime_disable(&pdev->dev);
      pm_runtime_dont_use_autosuspend(&pdev->dev);
    }
  }
  pm_runtime_resume_and_get(&pdev->dev);

  init_bi_list(&global_process_manager);
  result = ConfigAXIFE(1); //1: normal, 2: bypass
  if(result < 0)
  {
    vcmd_release_AXIFE_IO();
    goto err1;
  }
  result = ConfigMMU();
  if(result < 0)
  {
    vcmd_release_MMU_IO();
    goto err1;
  }
  result = MMU_Kernel_map();
  if(result < 0)
    goto err;
  for(i=0;i<total_vcmd_core_num;i++)
  {
    hantrovcmd_data[i].vcmd_core_cfg = vcmd_core_array[i];
    hantrovcmd_data[i].hwregs = NULL;
    hantrovcmd_data[i].core_id = i;
    hantrovcmd_data[i].working_state= WORKING_STATE_IDLE;
    hantrovcmd_data[i].sw_cmdbuf_rdy_num =0;
    hantrovcmd_data[i].spinlock = &owner_lock_vcmd[i];
    spin_lock_init(&owner_lock_vcmd[i]);
    hantrovcmd_data[i].wait_queue = &wait_queue_vcmd[i];
    init_waitqueue_head(&wait_queue_vcmd[i]);
    hantrovcmd_data[i].wait_abort_queue=&abort_queue_vcmd[i];
    init_waitqueue_head(&abort_queue_vcmd[i]);
    init_bi_list(&hantrovcmd_data[i].list_manager);
    hantrovcmd_data[i].duration_without_int = 0;
    vcmd_manager[vcmd_core_array[i].sub_module_type][vcmd_type_core_num[vcmd_core_array[i].sub_module_type]]=&hantrovcmd_data[i];
    vcmd_type_core_num[vcmd_core_array[i].sub_module_type]++;
    hantrovcmd_data[i].vcmd_reg_mem_busAddress = vcmd_registers_mem_pool.busAddress + i*VCMD_REGISTER_SIZE-base_ddr_addr;
    //next todo: split out
    hantrovcmd_data[i].mmu_vcmd_reg_mem_busAddress = vcmd_registers_mem_pool.mmu_bus_address + i*VCMD_REGISTER_SIZE;
    hantrovcmd_data[i].vcmd_reg_mem_virtualAddress = vcmd_registers_mem_pool.virtualAddress + i*VCMD_REGISTER_SIZE/4;
    hantrovcmd_data[i].vcmd_reg_mem_size = VCMD_REGISTER_SIZE;
    memset(hantrovcmd_data[i].vcmd_reg_mem_virtualAddress,0,VCMD_REGISTER_SIZE);
  }

  if (hantrovcmd_major == 0)
  {
    result = alloc_chrdev_region(&hantrovcmd_devt, 0, 1, "vc8000");
    if (result != 0)
    {
      pr_err("%s: alloc_chrdev_region error\n", __func__);
      goto err1;
    }
    hantrovcmd_major = MAJOR(hantrovcmd_devt);
    hantrovcmd_minor = MINOR(hantrovcmd_devt);
  }
  else
  {
    hantrovcmd_devt = MKDEV(hantrovcmd_major, hantrovcmd_minor);
    result = register_chrdev_region(hantrovcmd_devt, 1, "vc8000");
    if (result)
    {
      pr_err("%s: register_chrdev_region error\n", __func__);
      goto err1;
    }
  }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
  hantrovcmd_class = class_create(THIS_MODULE, "vc8000");
#else
  hantrovcmd_class = class_create("vc8000");
#endif
  if (IS_ERR(hantrovcmd_class))
  {
    pr_err("%s, %d: class_create error!\n", __func__, __LINE__);
    goto err;
  }
  hantrovcmd_devt = MKDEV(hantrovcmd_major, hantrovcmd_minor);

  cdev_init(&hantrovcmd_cdev, &hantrovcmd_fops);
  result = cdev_add(&hantrovcmd_cdev, hantrovcmd_devt, 1);
  if ( result )
  {
    pr_err("%s, %d: cdev_add error!\n", __func__, __LINE__);
    goto err;
  }

  device_create(hantrovcmd_class, NULL, hantrovcmd_devt,
      NULL, "vc8000");

  result = vcmd_reserve_IO();
  if(result < 0)
  {
    goto err;
  }
  vcmd_reset_asic(hantrovcmd_data);

  /* get the IRQ line */
  for (i=0;i<total_vcmd_core_num;i++)
  {
    if (hantrovcmd_data[i].hwregs==NULL)
      continue;
    if(hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq!= -1)
    {
      result = request_irq(hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq, hantrovcmd_isr,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
                           SA_INTERRUPT | SA_SHIRQ,
#else
                           IRQF_SHARED,
#endif
                           "vc8000", (void *) &hantrovcmd_data[i]);
      if(result == -EINVAL)
      {
        pr_err("vc8000_vcmd_driver: Bad vcmd_irq number or handler. core_id=%d\n",i);
        vcmd_release_IO();
        goto err;
      }
      else if(result == -EBUSY)
      {
        pr_err("vc8000_vcmd_driver: IRQ <%d> busy, change your config. core_id=%d\n",
               hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq,i);
        vcmd_release_IO();
        goto err;
      }
    }
    else
    {
      pr_info("vc8000_vcmd_driver: IRQ not in use!\n");
    }
  }
  //cmdbuf pool allocation
  //init_vcmd_non_cachable_memory_allocate();
  //for cmdbuf management
  cmdbuf_used_pos=0;
  for(k=0;k<TOTAL_DISCRETE_CMDBUF_NUM;k++)
  {
    cmdbuf_used[k]=0;
    global_cmdbuf_node[k] = NULL;
  }
  //cmdbuf_used[0] not be used, because int vector must non-zero
  cmdbuf_used_residual=TOTAL_DISCRETE_CMDBUF_NUM;
  cmdbuf_used_pos=1;
  cmdbuf_used[0]=1;
  cmdbuf_used_residual -=1;
  pr_info(" CMDBUF[0] reserved,remain %d\n",cmdbuf_used_residual);

  pr_info("vc8000_vcmd_driver: module inserted. Major <%d>\n", hantrovcmd_major);

  create_kernel_process_manager();
  for(i=0;i<MAX_VCMD_TYPE;i++)
  {
    if(vcmd_type_core_num[i]==0)
      continue;
    sema_init(&vcmd_reserve_cmdbuf_sem[i], 1);
  }
#ifdef IRQ_SIMULATION
  for(i=0;i<10000;i++)
  {
    timer_reserve[i].timer=NULL;
  }
#endif
#ifndef DYNAMIC_MALLOC_VCMDNODE
    g_cmdbuf_obj_pool = vmalloc(sizeof(struct cmdbuf_obj) * TOTAL_DISCRETE_CMDBUF_NUM);
    g_cmdbuf_node_pool  = vmalloc(sizeof(bi_list_node) * TOTAL_DISCRETE_CMDBUF_NUM);
#endif
  /*read all registers for each type of module for analyzing configuration in cwl*/
  for(i=0;i<MAX_VCMD_TYPE;i++)
  {
    if(vcmd_type_core_num[i]==0)
      continue;
    PDEBUG("hantrovcmd_init: vcmd_core_type is %d\n", i);
    read_main_module_all_registers(i);
  }
  //devfreq
  pr_info("start venc devfreq init \n");
  if(encoder_devfreq_init(&pdev->dev) ) {
    pr_err("venc devfreq not enabled\n");
  } else {
    pr_info("venc devfreq init ok\n");
  }
  result = sysfs_create_group(&pdev->dev.kobj, &venc_dev_attr_group);
    if(result)
      pr_warn("venc create sysfs failed\n");

  pm_runtime_mark_last_busy(&pdev->dev);
  pm_runtime_put_autosuspend(&pdev->dev);

  return 0;
  err:
  if (root_debugfs_dir) {
    debugfs_remove_recursive(root_debugfs_dir);
    root_debugfs_dir = NULL;
  }
#ifdef HANTROMMU_SUPPORT
  MMU_Kernel_unmap();
  vcmd_pool_release(pdev);
#endif
  unregister_chrdev_region(hantrovcmd_devt, 1);
  pm_runtime_mark_last_busy(&pdev->dev);
  pm_runtime_put_autosuspend(&pdev->dev);
  err1:
    if (hantrovcmd_data != NULL)
      vfree(hantrovcmd_data);
    pr_info("vc8000_vcmd_driver: module not inserted\n");
    return result;

}

static int hantroenc_vcmd_remove(struct platform_device *pdev)
{
  int i=0;
  u32 result;

  if (root_debugfs_dir) {
    debugfs_remove_recursive(root_debugfs_dir);
    root_debugfs_dir = NULL;
  }
  cancel_delayed_work_sync(&venc_cmd_profile_work);
  pm_runtime_resume_and_get(&pdev->dev);

  for(i=0;i<total_vcmd_core_num;i++)
  {
    if (hantrovcmd_data[i].hwregs==NULL)
      continue;
    //disable interrupt at first
    vcmd_write_reg((const void *) hantrovcmd_data[i].hwregs,VCMD_REGISTER_INT_CTL_OFFSET,0x0000);
    //disable HW
    vcmd_write_reg((const void *) hantrovcmd_data[i].hwregs,VCMD_REGISTER_CONTROL_OFFSET,0x0000);
    //read status register
    result =vcmd_read_reg((const void *) hantrovcmd_data[i].hwregs,VCMD_REGISTER_INT_STATUS_OFFSET);
    //clean status register
    vcmd_write_reg((const void *) hantrovcmd_data[i].hwregs,VCMD_REGISTER_INT_STATUS_OFFSET,result);

    /* free the vcmd IRQ */
    if(hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq != -1)
    {
      free_irq(hantrovcmd_data[i].vcmd_core_cfg.vcmd_irq, (void *)&hantrovcmd_data[i]);
    }
    release_cmdbuf_node_cleanup(&hantrovcmd_data[i].list_manager);
  }

  release_process_node_cleanup(&global_process_manager);

#ifdef HANTROMMU_SUPPORT
  MMUCleanup(mmu_hwregs);
#endif

  vcmd_release_IO();

   //release_vcmd_non_cachable_memory();
  MMU_Kernel_unmap();
  vcmd_pool_release(pdev);
  encoder_devfreq_fini(&pdev->dev);
  
  pm_runtime_mark_last_busy(&pdev->dev);
  pm_runtime_put_autosuspend(&pdev->dev);
  pm_runtime_disable(&pdev->dev);
  if (!pm_runtime_status_suspended(&pdev->dev))
    encoder_runtime_suspend(&pdev->dev);
  vfree(hantrovcmd_data);

  cdev_del(&hantrovcmd_cdev);
  device_destroy(hantrovcmd_class, hantrovcmd_devt);
  unregister_chrdev_region(hantrovcmd_devt, 1);
  class_destroy(hantrovcmd_class);
  sysfs_remove_group(&pdev->dev.kobj,&venc_dev_attr_group);
#ifndef DYNAMIC_MALLOC_VCMDNODE
  if (g_cmdbuf_obj_pool) {
    vfree(g_cmdbuf_obj_pool);
    g_cmdbuf_obj_pool = NULL;
  }
  if (g_cmdbuf_node_pool) {
    vfree(g_cmdbuf_node_pool);
    g_cmdbuf_node_pool = NULL;
  }
#endif

  pr_info("vc8000_vcmd_driver: module removed\n");
  return 0;
}


static const struct dev_pm_ops encoder_runtime_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(encoder_suspend, encoder_resume)
	SET_RUNTIME_PM_OPS(encoder_runtime_suspend, encoder_runtime_resume, NULL)
};

static struct platform_driver hantroenc_vcmd_driver = {
	.probe = hantroenc_vcmd_probe,
	.remove = hantroenc_vcmd_remove,
	.driver = {
		.name = "encoder_hantroenc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(hantro_of_match),
		.pm = &encoder_runtime_pm_ops,
	}
};


int __init hantroenc_vcmd_init(void)
{
	int ret = 0;
	pr_debug("enter %s\n",__func__);
	ret = platform_driver_register(&hantroenc_vcmd_driver);
	if(ret)
	{
		pr_err("register platform driver failed!\n");
	}
	return ret;
}

void __exit hantroenc_vcmd_cleanup(void)
{
	pr_debug("enter %s\n",__func__);
	platform_driver_unregister(&hantroenc_vcmd_driver);
	return;
}

static int vcmd_reserve_IO(void)
{
    u32 hwid;
    int i;
    u32 found_hw = 0;

    pr_info("vcmd_reserve_IO: total_vcmd_core_num is %d\n", total_vcmd_core_num);
    for (i=0;i<total_vcmd_core_num;i++)
    {
      hantrovcmd_data[i].hwregs = NULL;

      if(!request_mem_region
         (hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr, hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize, "vc8000_vcmd_driver"))
      {
          pr_info("hantrovcmd: failed to reserve HW regs for vcmd %d\n", i);
          pr_info("hantrovcmd: vcmd_base_addr = 0x%08lx, iosize = %d\n",
                            hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
                            hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
          continue;
      }

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0))
      hantrovcmd_data[i].hwregs =
          (volatile u8 *) ioremap_nocache(hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
                                          hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
#else
      hantrovcmd_data[i].hwregs =
          (volatile u8 *) ioremap(hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr,
                                          hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
#endif

      if(hantrovcmd_data[i].hwregs == NULL)
      {
          pr_info("hantrovcmd: failed to ioremap HW regs\n");
          release_mem_region(hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr, hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
          continue;
      }

      /*read hwid and check validness and store it*/
      hwid = (u32)ioread32(( void *)hantrovcmd_data[i].hwregs);
      pr_info("vcmd_reserve_IO: hantrovcmd_data[%d].hwregs=0x%px\n",i, hantrovcmd_data[i].hwregs);
      pr_info("hwid=0x%08x\n", hwid);
      hantrovcmd_data[i].hw_version_id = hwid;

      /* check for vcmd HW ID */
      if( ((hwid >> 16) & 0xFFFF) != VCMD_HW_ID  )
      {
          pr_info("hantrovcmd: HW not found at 0x%llx\n",
                 (long long unsigned int)hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr);
          iounmap(( void *) hantrovcmd_data[i].hwregs);
          release_mem_region(hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr, hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
          hantrovcmd_data[i].hwregs = NULL;
          continue;
      }

      found_hw = 1;

      pr_info(
             "hantrovcmd: HW at base <0x%llx> with ID <0x%08x>\n",
             (long long unsigned int)hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr, hwid);

    }

    if (found_hw == 0)
    {
      pr_err("hantrovcmd: NO ANY HW found!!\n");
      return -1;
    }

    return 0;
}

static void vcmd_release_IO(void)
{
    u32 i;

    vcmd_release_AXIFE_IO();
    vcmd_release_MMU_IO();
    for (i=0;i<total_vcmd_core_num;i++)
    {
      if(hantrovcmd_data[i].hwregs)
      {
        iounmap((void *) hantrovcmd_data[i].hwregs);
        release_mem_region(hantrovcmd_data[i].vcmd_core_cfg.vcmd_base_addr, hantrovcmd_data[i].vcmd_core_cfg.vcmd_iosize);
        hantrovcmd_data[i].hwregs = NULL;
      }
    }
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18))
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t hantrovcmd_isr(int irq, void *dev_id)
#endif
{
    unsigned int handled = 0;
    struct hantrovcmd_dev *dev = (struct hantrovcmd_dev *) dev_id;
    u32 irq_status=0;
    unsigned long flags;
    bi_list_node* new_cmdbuf_node=NULL;
    bi_list_node* base_cmdbuf_node=NULL;
    struct cmdbuf_obj* cmdbuf_obj=NULL;
    size_t exe_cmdbuf_busAddress;
    u32 cmdbuf_processed_num=0;
    u32 cmdbuf_id=0;

    /*If core is not reserved by any user, but irq is received, just clean it*/
    spin_lock_irqsave(dev->spinlock, flags);
    if (dev->list_manager.head==NULL)
    {
      PDEBUG( "hantrovcmd_isr:received IRQ but core has nothing to do.\n");
      irq_status = vcmd_read_reg((const void *)dev->hwregs,VCMD_REGISTER_INT_STATUS_OFFSET);
      vcmd_write_reg((const void *)dev->hwregs,VCMD_REGISTER_INT_STATUS_OFFSET,irq_status);
      spin_unlock_irqrestore(dev->spinlock, flags);
      return IRQ_HANDLED;
    }

    PDEBUG( "hantrovcmd_isr: received IRQ!\n");
    irq_status = vcmd_read_reg((const void *)dev->hwregs,VCMD_REGISTER_INT_STATUS_OFFSET);
#ifdef VCMD_DEBUG_INTERNAL
    {
         u32 i, fordebug;
         for(i=0;i<ASIC_VCMD_SWREG_AMOUNT;i++)
         {
           fordebug=vcmd_read_reg ((const void *)dev->hwregs, i*4);
           pr_info("vcmd register %d:0x%x\n",i,fordebug);
         }
    }
#endif

    if(!irq_status)
    {
      //pr_info("hantrovcmd_isr error,irq_status :0x%x",irq_status);
      spin_unlock_irqrestore(dev->spinlock, flags);
      return IRQ_HANDLED;
    }

    PDEBUG( "irq_status of %d is:0x%x\n",dev->core_id,irq_status);
    vcmd_write_reg((const void *)dev->hwregs,VCMD_REGISTER_INT_STATUS_OFFSET,irq_status);
    dev->reg_mirror[VCMD_REGISTER_INT_STATUS_OFFSET/4] = irq_status;

    if((dev->hw_version_id > HW_ID_1_0_C )&&(irq_status&0x3f))
    {
      //if error,read from register directly.
      cmdbuf_id = vcmd_get_register_value((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_CMDBUF_EXECUTING_ID);
      venc_vcmd_profile.cur_complete_vcmd_id = cmdbuf_id;
      if(cmdbuf_id>=TOTAL_DISCRETE_CMDBUF_NUM)
      {
        pr_err("hantrovcmd_isr error cmdbuf_id greater than the ceiling !!\n");
        spin_unlock_irqrestore(dev->spinlock, flags);
        return IRQ_HANDLED;
      }
    }
    else if((dev->hw_version_id > HW_ID_1_0_C ))
    {
      //read cmdbuf id from ddr
#ifdef VCMD_DEBUG_INTERNAL
      {
         u32 i, fordebug;
         pr_info("ddr vcmd register phy_addr=0x%x\n",dev->vcmd_reg_mem_busAddress);
         pr_info("ddr vcmd register virt_addr=0x%x\n",dev->vcmd_reg_mem_virtualAddress);
         for(i=0;i<ASIC_VCMD_SWREG_AMOUNT;i++)
         {
           fordebug=*(dev->vcmd_reg_mem_virtualAddress+i);
           pr_info("ddr vcmd register %d:0x%x\n",i,fordebug);
         }
      }
#endif

      cmdbuf_id = *(dev->vcmd_reg_mem_virtualAddress+EXECUTING_CMDBUF_ID_ADDR);
      venc_vcmd_profile.cur_complete_vcmd_id = cmdbuf_id;
      pr_debug("hantrovcmd_isr: cmdbuf_id %d from virtual!!\n", cmdbuf_id);
      if(cmdbuf_id>=TOTAL_DISCRETE_CMDBUF_NUM)
      {
        pr_err("hantrovcmd_isr error cmdbuf_id greater than the ceiling !!\n");
        spin_unlock_irqrestore(dev->spinlock, flags);
        return IRQ_HANDLED;
      }
    }


    if(vcmd_get_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_RESET))
    {
      //reset error,all cmdbuf that is not  done will be run again.
      new_cmdbuf_node = dev->list_manager.head;
      dev->working_state = WORKING_STATE_IDLE;
      //find the first run_done=0
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        if((cmdbuf_obj->cmdbuf_run_done == 0))
          break;
        new_cmdbuf_node = new_cmdbuf_node->next;
      }
      base_cmdbuf_node = new_cmdbuf_node;
      vcmd_delink_cmdbuf(dev,base_cmdbuf_node);
      vcmd_link_cmdbuf(dev,base_cmdbuf_node);
      if(dev->sw_cmdbuf_rdy_num !=0)
      {
        //restart new command
        vcmd_start(dev,base_cmdbuf_node);
      }
      handled++;
      trace_venc_interrupt(0xffffffff,irq_status,0);
      spin_unlock_irqrestore(dev->spinlock, flags);
      return IRQ_HANDLED;
    }
    if(vcmd_get_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_ABORT))
    {
      //abort error,don't need to reset
      new_cmdbuf_node = dev->list_manager.head;
      dev->working_state = WORKING_STATE_IDLE;
      venc_vcmd_profile.vcmd_abort_cnt++;
      if(dev->hw_version_id > HW_ID_1_0_C )
      {
        new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
        if(new_cmdbuf_node==NULL)
        {
          pr_err("hantrovcmd_isr error cmdbuf_id %d line %d!!\n",cmdbuf_id,__LINE__);
          spin_unlock_irqrestore(dev->spinlock, flags);
          return IRQ_HANDLED;
        }
      }
      else
      {
        exe_cmdbuf_busAddress = VCMDGetAddrRegisterValue((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR);
        //find the cmdbuf that tigers ABORT
        while(1)
        {
          if(new_cmdbuf_node==NULL)
          {
            spin_unlock_irqrestore(dev->spinlock, flags);
            return IRQ_HANDLED;
          }
          cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
          if((((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr) <=exe_cmdbuf_busAddress)&&(((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr+cmdbuf_obj->cmdbuf_size) >exe_cmdbuf_busAddress)) ) &&(cmdbuf_obj->cmdbuf_run_done==0))
            break;
          new_cmdbuf_node = new_cmdbuf_node->next;
        }
      }
      base_cmdbuf_node = new_cmdbuf_node;
      // this cmdbuf and cmdbufs prior to itself, run_done = 1
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        if((cmdbuf_obj->cmdbuf_run_done==0))
        {
          cmdbuf_obj->cmdbuf_run_done=1;
          cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_OK;
          cmdbuf_processed_num++;
          encoder_devfreq_record_idle( encoder_get_devfreq_priv_data() );
        }
        else
          break;
        new_cmdbuf_node = new_cmdbuf_node->previous;
      }
      base_cmdbuf_node=base_cmdbuf_node->next;
      vcmd_delink_cmdbuf(dev,base_cmdbuf_node);
      if(software_triger_abort==0)
      {
        //for QCFE
        vcmd_link_cmdbuf(dev,base_cmdbuf_node);
        if(dev->sw_cmdbuf_rdy_num !=0)
        {
          //restart new command
          vcmd_start(dev,base_cmdbuf_node);
        }
      }
      trace_venc_interrupt(cmdbuf_id,irq_status,cmdbuf_processed_num);
      spin_unlock_irqrestore(dev->spinlock, flags);
      if(cmdbuf_processed_num)
        wake_up_all(dev->wait_queue);
      //to let high priority cmdbuf be inserted
      wake_up_all(dev->wait_abort_queue);
      handled++;
      return IRQ_HANDLED;
    }
    if(vcmd_get_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_BUSERR))
    {
      //bus error, don't need to reset where to record status?
      new_cmdbuf_node = dev->list_manager.head;
      dev->working_state = WORKING_STATE_IDLE;
      venc_vcmd_profile.vcmd_buserr_cnt++;
      if(dev->hw_version_id > HW_ID_1_0_C )
      {
        new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
        if(new_cmdbuf_node==NULL)
        {
          pr_err("hantrovcmd_isr error cmdbuf_id %d line %d!!\n",cmdbuf_id,__LINE__);
          spin_unlock_irqrestore(dev->spinlock, flags);
          return IRQ_HANDLED;
        }
      }
      else
      {
        exe_cmdbuf_busAddress = VCMDGetAddrRegisterValue((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR);
        //find the buserr cmdbuf
        while(1)
        {
          if(new_cmdbuf_node==NULL)
          {
            spin_unlock_irqrestore(dev->spinlock, flags);
            return IRQ_HANDLED;
          }
          cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
          if((((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr) <=exe_cmdbuf_busAddress)&&(((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr+cmdbuf_obj->cmdbuf_size) >exe_cmdbuf_busAddress)) ) &&(cmdbuf_obj->cmdbuf_run_done==0))
            break;
          new_cmdbuf_node = new_cmdbuf_node->next;
        }
      }
      base_cmdbuf_node = new_cmdbuf_node;
      // this cmdbuf and cmdbufs prior to itself, run_done = 1
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        if((cmdbuf_obj->cmdbuf_run_done==0))
        {
          cmdbuf_obj->cmdbuf_run_done=1;
          cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_OK;
          cmdbuf_processed_num++;
          encoder_devfreq_record_idle( encoder_get_devfreq_priv_data() );
        }
        else
          break;
        new_cmdbuf_node = new_cmdbuf_node->previous;
      }
      new_cmdbuf_node = base_cmdbuf_node;
      if(new_cmdbuf_node!=NULL)
      {
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_BUSERR;
      }
      base_cmdbuf_node=base_cmdbuf_node->next;
      vcmd_delink_cmdbuf(dev,base_cmdbuf_node);
      vcmd_link_cmdbuf(dev,base_cmdbuf_node);
      if(dev->sw_cmdbuf_rdy_num !=0)
      {
        //restart new command
        vcmd_start(dev,base_cmdbuf_node);
      }
      trace_venc_interrupt(cmdbuf_id,irq_status,cmdbuf_processed_num);
      spin_unlock_irqrestore(dev->spinlock, flags);
      if(cmdbuf_processed_num)
        wake_up_all(dev->wait_queue);
      handled++;
      return IRQ_HANDLED;
    }
    if(vcmd_get_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_TIMEOUT))
    {
      //time out,need to reset
      new_cmdbuf_node = dev->list_manager.head;
      venc_vcmd_profile.vcmd_timeout_cnt++;
      dev->working_state = WORKING_STATE_IDLE;
      if(dev->hw_version_id > HW_ID_1_0_C )
      {
        new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
        if(new_cmdbuf_node==NULL)
        {
          pr_err("hantrovcmd_isr error cmdbuf_id %d line %d!!\n",cmdbuf_id,__LINE__);
          spin_unlock_irqrestore(dev->spinlock, flags);
          return IRQ_HANDLED;
        }
      }
      else
      {
        exe_cmdbuf_busAddress = VCMDGetAddrRegisterValue((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR);
        //find the timeout cmdbuf
        while(1)
        {
          if(new_cmdbuf_node==NULL)
          {
            spin_unlock_irqrestore(dev->spinlock, flags);
            return IRQ_HANDLED;
          }
          cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
          if((((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr) <=exe_cmdbuf_busAddress)&&(((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr+cmdbuf_obj->cmdbuf_size) >exe_cmdbuf_busAddress)) ) &&(cmdbuf_obj->cmdbuf_run_done==0))
            break;
          new_cmdbuf_node = new_cmdbuf_node->next;
        }
      }
      base_cmdbuf_node = new_cmdbuf_node;
      new_cmdbuf_node = new_cmdbuf_node->previous;
      // this cmdbuf and cmdbufs prior to itself, run_done = 1
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        if((cmdbuf_obj->cmdbuf_run_done==0))
        {
          cmdbuf_obj->cmdbuf_run_done=1;
          cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_OK;
          cmdbuf_processed_num++;
          encoder_devfreq_record_idle( encoder_get_devfreq_priv_data() );
        }
        else
          break;
        new_cmdbuf_node = new_cmdbuf_node->previous;
      }
      vcmd_delink_cmdbuf(dev,base_cmdbuf_node);
      vcmd_link_cmdbuf(dev,base_cmdbuf_node);
      if(dev->sw_cmdbuf_rdy_num !=0)
      {
        //reset
        vcmd_reset_current_asic(dev);
        //restart new command
        vcmd_start(dev,base_cmdbuf_node);
      }
      trace_venc_interrupt(cmdbuf_id,irq_status,cmdbuf_processed_num);
      spin_unlock_irqrestore(dev->spinlock, flags);
      if(cmdbuf_processed_num)
        wake_up_all(dev->wait_queue);
      handled++;
      return IRQ_HANDLED;
    }
    if(vcmd_get_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_CMDERR))
    {
      //command error,don't need to reset
      new_cmdbuf_node = dev->list_manager.head;
      dev->working_state = WORKING_STATE_IDLE;
      venc_vcmd_profile.vcmd_cmderr_cnt++;
      if(dev->hw_version_id > HW_ID_1_0_C )
      {
        new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
        if(new_cmdbuf_node==NULL)
        {
          pr_err("hantrovcmd_isr error cmdbuf_id %d!!\n", cmdbuf_id);
          spin_unlock_irqrestore(dev->spinlock, flags);
          return IRQ_HANDLED;
        }
      }
      else
      {
        exe_cmdbuf_busAddress = VCMDGetAddrRegisterValue((const void *)dev->hwregs,dev->reg_mirror,HWIF_VCMD_EXECUTING_CMD_ADDR);
        //find the cmderror cmdbuf
        while(1)
        {
          if(new_cmdbuf_node==NULL)
          {
            spin_unlock_irqrestore(dev->spinlock, flags);
            return IRQ_HANDLED;
          }
          cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
          if((((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr) <=exe_cmdbuf_busAddress)&&(((cmdbuf_obj->cmdbuf_busAddress-base_ddr_addr+cmdbuf_obj->cmdbuf_size) >exe_cmdbuf_busAddress)) ) &&(cmdbuf_obj->cmdbuf_run_done==0))
            break;
          new_cmdbuf_node = new_cmdbuf_node->next;
        }
      }
      base_cmdbuf_node = new_cmdbuf_node;
      // this cmdbuf and cmdbufs prior to itself, run_done = 1
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        if((cmdbuf_obj->cmdbuf_run_done==0))
        {
          cmdbuf_obj->cmdbuf_run_done=1;
          cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_OK;
          cmdbuf_processed_num++;
          encoder_devfreq_record_idle( encoder_get_devfreq_priv_data() );
        }
        else
          break;
        new_cmdbuf_node = new_cmdbuf_node->previous;
      }
      new_cmdbuf_node = base_cmdbuf_node;
      if(new_cmdbuf_node!=NULL)
      {
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_CMDERR;//cmderr
      }
      base_cmdbuf_node=base_cmdbuf_node->next;
      vcmd_delink_cmdbuf(dev,base_cmdbuf_node);
      vcmd_link_cmdbuf(dev,base_cmdbuf_node);
      if(dev->sw_cmdbuf_rdy_num !=0)
      {
        //restart new command
        vcmd_start(dev,base_cmdbuf_node);
      }
      trace_venc_interrupt(cmdbuf_id,irq_status,cmdbuf_processed_num);
      spin_unlock_irqrestore(dev->spinlock, flags);
      if(cmdbuf_processed_num)
        wake_up_all(dev->wait_queue);
      handled++;
      return IRQ_HANDLED;
    }

    if(vcmd_get_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_ENDCMD))
    {
      //end command interrupt
      new_cmdbuf_node = dev->list_manager.head;
      dev->working_state = WORKING_STATE_IDLE;
      if(dev->hw_version_id > HW_ID_1_0_C )
      {
        new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
        if(new_cmdbuf_node==NULL)
        {
          pr_err("hantrovcmd_isr error cmdbuf_id %d line %d!!\n",cmdbuf_id,__LINE__);
          spin_unlock_irqrestore(dev->spinlock, flags);
          return IRQ_HANDLED;
        }
      }
      else
      {
        //find the end cmdbuf
        while(1)
        {
          if(new_cmdbuf_node==NULL)
          {
            spin_unlock_irqrestore(dev->spinlock, flags);
            return IRQ_HANDLED;
          }
          cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
          if((cmdbuf_obj->has_end_cmdbuf == 1)&&(cmdbuf_obj->cmdbuf_run_done==0))
            break;
          new_cmdbuf_node = new_cmdbuf_node->next;
        }
      }
      base_cmdbuf_node = new_cmdbuf_node;
      // this cmdbuf and cmdbufs prior to itself, run_done = 1
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        if((cmdbuf_obj->cmdbuf_run_done==0))
        {
          cmdbuf_obj->cmdbuf_run_done=1;
          cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_OK;
          cmdbuf_processed_num++;
          encoder_devfreq_record_idle( encoder_get_devfreq_priv_data() );
        }
        else
          break;
        new_cmdbuf_node = new_cmdbuf_node->previous;
      }
      base_cmdbuf_node=base_cmdbuf_node->next;
      vcmd_delink_cmdbuf(dev,base_cmdbuf_node);
      vcmd_link_cmdbuf(dev,base_cmdbuf_node);
      if(dev->sw_cmdbuf_rdy_num !=0)
      {
        //restart new command
        vcmd_start(dev,base_cmdbuf_node);
      }
      trace_venc_interrupt(cmdbuf_id,irq_status,cmdbuf_processed_num);
      spin_unlock_irqrestore(dev->spinlock, flags);
      if(cmdbuf_processed_num)
        wake_up_all(dev->wait_queue);
      handled++;
      return IRQ_HANDLED;
    }
    if(dev->hw_version_id <= HW_ID_1_0_C )
      cmdbuf_id = vcmd_get_register_mirror_value(dev->reg_mirror,HWIF_VCMD_IRQ_INTCMD);
    if(cmdbuf_id)
    {
      if(dev->hw_version_id <= HW_ID_1_0_C )
      {
        if(cmdbuf_id>=TOTAL_DISCRETE_CMDBUF_NUM)
        {
          pr_err("hantrovcmd_isr error cmdbuf_id greater than the ceiling !!\n");
          spin_unlock_irqrestore(dev->spinlock, flags);
          return IRQ_HANDLED;
        }
      }
      new_cmdbuf_node = global_cmdbuf_node[cmdbuf_id];
      if(new_cmdbuf_node==NULL)
      {
        pr_err("hantrovcmd_isr error cmdbuf_id %d line %d!!\n",cmdbuf_id,__LINE__);
        spin_unlock_irqrestore(dev->spinlock, flags);
        return IRQ_HANDLED;
      }
      // interrupt cmdbuf and cmdbufs prior to itself, run_done = 1
      while(1)
      {
        if(new_cmdbuf_node==NULL)
          break;
        cmdbuf_obj = (struct cmdbuf_obj*)new_cmdbuf_node->data;
        if((cmdbuf_obj->cmdbuf_run_done==0))
        {
          cmdbuf_obj->cmdbuf_run_done=1;
          cmdbuf_obj->executing_status = CMDBUF_EXE_STATUS_OK;
          cmdbuf_processed_num++;
          encoder_devfreq_record_idle( encoder_get_devfreq_priv_data() );
        }
        else
          break;
        new_cmdbuf_node = new_cmdbuf_node->previous;
      }
      handled++;
    }
    venc_vcmd_profile.vcmd_num_share_irq = cmdbuf_processed_num;
    trace_venc_interrupt(cmdbuf_id,irq_status,cmdbuf_processed_num);
    spin_unlock_irqrestore(dev->spinlock, flags);
    if(cmdbuf_processed_num)
      wake_up_all(dev->wait_queue);
    if(!handled)
    {
        PDEBUG("IRQ received, but not hantro's!\n");
    }
    return IRQ_HANDLED;
}

static void vcmd_reset_asic(struct hantrovcmd_dev * dev)
{
    int i,n;
    u32 result;
    for (n=0;n<total_vcmd_core_num;n++)
    {
      if(dev[n].hwregs!=NULL)
      {
        //disable interrupt at first
        vcmd_write_reg((const void *)dev[n].hwregs,VCMD_REGISTER_INT_CTL_OFFSET,0x0000);
        //reset all
        vcmd_write_reg((const void *)dev[n].hwregs,VCMD_REGISTER_CONTROL_OFFSET,0x0002);
        //read status register
        result =vcmd_read_reg((const void *)dev[n].hwregs,VCMD_REGISTER_INT_STATUS_OFFSET);
        //clean status register
        vcmd_write_reg((const void *)dev[n].hwregs,VCMD_REGISTER_INT_STATUS_OFFSET,result);
        for(i = VCMD_REGISTER_CONTROL_OFFSET; i < dev[n].vcmd_core_cfg.vcmd_iosize; i += 4)
        {
          //set all register 0
          vcmd_write_reg((const void *)dev[n].hwregs,i,0x0000);
        }
        //enable all interrupt
        vcmd_write_reg((const void *)dev[n].hwregs,VCMD_REGISTER_INT_CTL_OFFSET,0xffffffff);
        // gate all external interrupt
        vcmd_write_reg((const void *)dev[n].hwregs,VCMD_REGISTER_EXT_INT_GATE_OFFSET,0xffffffff);
      }
    }
}

static void vcmd_reset_current_asic(struct hantrovcmd_dev * dev)
{
  u32 result;

  if(dev->hwregs!=NULL)
  {
    //disable interrupt at first
    vcmd_write_reg((const void *)dev->hwregs,VCMD_REGISTER_INT_CTL_OFFSET,0x0000);
    //reset all
    vcmd_write_reg((const void *)dev->hwregs,VCMD_REGISTER_CONTROL_OFFSET,0x0002);
    //read status register
    result =vcmd_read_reg((const void *)dev->hwregs,VCMD_REGISTER_INT_STATUS_OFFSET);
    //clean status register
    vcmd_write_reg((const void *)dev->hwregs,VCMD_REGISTER_INT_STATUS_OFFSET,result);
  }

}
 
#ifdef VCMD_DEBUG_INTERNAL
static void printk_vcmd_register_debug(const void *hwregs, char * info)
{
  u32 i, fordebug;
  for(i=0;i<ASIC_VCMD_SWREG_AMOUNT;i++)
  {
      fordebug=vcmd_read_reg ((const void *)hwregs, i*4);
      pr_info("%s vcmd register %d:0x%x\n",info,i,fordebug);
  }
}
#endif

static void vcmd_reset(void)
{
  if (hantrovcmd_data) {
    int i;
    for(i=0;i<total_vcmd_core_num;i++) {
      hantrovcmd_data[i].working_state = WORKING_STATE_IDLE;
      hantrovcmd_data[i].sw_cmdbuf_rdy_num = 0;
    }
    vcmd_reset_asic(hantrovcmd_data);
  }
}

static bool hantroenc_cmdbuf_range(size_t addr, size_t size)
{
    bool bInRange;

    bInRange = (addr >= vcmd_buf_mem_pool.busAddress &&
                (addr - vcmd_buf_mem_pool.busAddress  + size) <= CMDBUF_POOL_TOTAL_SIZE) ||
               (addr >= vcmd_status_buf_mem_pool.busAddress &&
                (addr - vcmd_status_buf_mem_pool.busAddress  + size) <= CMDBUF_POOL_TOTAL_SIZE) ||
               (addr >= vcmd_registers_mem_pool.busAddress &&
                (addr - vcmd_status_buf_mem_pool.busAddress + size) <= CMDBUF_VCMD_REGISTER_TOTAL_SIZE);

    return bInRange;
}

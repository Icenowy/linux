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

#ifndef _SUBSYS_H_
#define _SUBSYS_H_

#ifdef __FREERTOS__
/* nothing */
#elif defined(__linux__)
#include <linux/fs.h>
#include <linux/platform_device.h>
#endif
#include "hantrodec.h"

#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void *)0)
#endif
#endif

/* Functions provided by all other subsystem IP - hantrodec_xxx.c */

/******************************************************************************/
/* subsys level */
/******************************************************************************/

#define MAX_SUBSYS_NUM  4   /* up to 4 subsystem (temporary) */
#define HXDEC_MAX_CORES MAX_SUBSYS_NUM    /* used in hantro_dec_xxx.c */

/* SubsysDesc & CoreDesc are used for configuration */
struct SubsysDesc {
  int slice_index;    /* slice this subsys belongs to */
  int index;   /* subsystem index */
  long base;
};

struct CoreDesc {
  int slice;
  int subsys;     /* subsys this core belongs to */
  enum CoreType core_type;
  int offset;     /* offset to subsystem base */
  int iosize;
  int irq;
  int has_apb;
};

/* internal config struct (translated from SubsysDesc & CoreDesc) */
struct subsys_config {
  unsigned long base_addr;
  int irq;
  u32 subsys_type;  /* identifier for each subsys vc8000e=0,IM=1,vc8000d=2,jpege=3,jpegd=4 */
  u32 submodule_offset[HW_CORE_MAX]; /* in bytes */
  u16 submodule_iosize[HW_CORE_MAX]; /* in bytes */
  volatile u8 *submodule_hwregs[HW_CORE_MAX]; /* virtual address */
  int has_apbfilter[HW_CORE_MAX];
};

void CheckSubsysCoreArray(struct subsys_config *subsys, int *vcmd);

/******************************************************************************/
/* VCMD */
/******************************************************************************/
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

/* Used in vcmd initialization in hantro_vcmd_xxx.c. */
/* May be unified in next step. */
struct vcmd_config {
  unsigned long vcmd_base_addr;
  u32 vcmd_iosize;
  int vcmd_irq;
  u32 sub_module_type;        /*input vc8000e=0,IM=1,vc8000d=2,jpege=3, jpegd=4*/
  u16 submodule_main_addr;    // in byte
  u16 submodule_dec400_addr;  //if submodule addr == 0xffff, this submodule does not exist.// in byte
  u16 submodule_L2Cache_addr; // in byte
  u16 submodule_MMU_addr;     // in byte
  u16 submodule_MMUWrite_addr;// in byte
  u16 submodule_axife_addr;   // in byte
};


#ifdef __FREERTOS__
/* nothing */
#elif defined(__linux__)
int hantrovcmd_open(struct inode *inode, struct file *filp);
int hantrovcmd_flush(struct file *filp, fl_owner_t id);
int hantrovcmd_release(struct inode *inode, struct file *filp);
long hantrovcmd_ioctl(struct file *filp,
                      unsigned int cmd, unsigned long arg);
int hantrovcmd_init(struct platform_device *pdev);
void hantrovcmd_cleanup(struct platform_device *pdev);
void hantrovcmd_reset(bool only_asic);
int hantrovcmd_resume_start(void);
void hantrovcmd_suspend_record(void);
bool hantro_cmdbuf_range(addr_t addr,size_t size);

/******************************************************************************/
/* MMU */
/******************************************************************************/

/* Init MMU, should be called in driver init function. */
enum MMUStatus MMUInit(volatile unsigned char *hwregs);
/* Clean up all data in MMU, should be called in driver cleanup function
   when rmmod driver*/
enum MMUStatus MMUCleanup(volatile unsigned char *hwregs[MAX_SUBSYS_NUM][2]);
/* The function should be called in driver realease function
   when driver exit unnormally */
enum MMUStatus MMURelease(void *filp, volatile unsigned char *hwregs);

enum MMUStatus MMUEnable(volatile unsigned char *hwregs[MAX_SUBSYS_NUM][2]);

/* Used in kernel to map buffer */
enum MMUStatus MMUKernelMemNodeMap(struct kernel_addr_desc *addr);
/* Used in kernel to unmap buffer */
enum MMUStatus MMUKernelMemNodeUnmap(struct kernel_addr_desc *addr);

long MMUIoctl(unsigned int cmd, void *filp, unsigned long arg,
              volatile unsigned char *hwregs[MAX_SUBSYS_NUM][2]);

void MMURestore(volatile unsigned char *hwregs[MAX_SUBSYS_NUM][2]);
bool MMU_CheckPowerStayOn(volatile unsigned char *hwregs[MAX_SUBSYS_NUM][2]);

int allocator_init(struct device *dev);
void allocator_remove(void);
int allocator_open(struct inode *inode, struct file *filp);
void allocator_release(struct inode *inode, struct file *filp);
int allocator_ioctl(void *filp, unsigned int cmd, unsigned long arg);
int allocator_mmap(struct file *filp, struct vm_area_struct *vma);

/******************************************************************************/
/* L2Cache */
/******************************************************************************/

/******************************************************************************/
/* DEC400 */
/******************************************************************************/


/******************************************************************************/
/* AXI FE */
/******************************************************************************/
#endif

#endif


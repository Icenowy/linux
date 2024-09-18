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

#ifndef _HANTROMMU_H_
#define _HANTROMMU_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __FREERTOS__
#elif defined(__linux__)
#include <linux/fs.h>
#endif

#define REGION_IN_START          0x0
#define REGION_IN_END            0x0
#define REGION_OUT_START         0x0
#define REGION_OUT_END           0x0
#define REGION_PRIVATE_START     0x0
#define REGION_PRIVATE_END       0x0

#define REGION_IN_MMU_START      0x1000
#define REGION_IN_MMU_END        0x40002000
#define REGION_OUT_MMU_START     0x40002000
#define REGION_OUT_MMU_END       0x40002000
#define REGION_PRIVATE_MMU_START 0x40002000
#define REGION_PRIVATE_MMU_END   0x40002000

#define MMU_REG_OFFSET              0
#define MMU_REG_HW_ID               (MMU_REG_OFFSET + 6*4)
#define MMU_REG_FLUSH               (MMU_REG_OFFSET + 97*4)
#define MMU_REG_PAGE_TABLE_ID       (MMU_REG_OFFSET + 107*4)
#define MMU_REG_CONTROL             (MMU_REG_OFFSET + 226*4)
#define MMU_REG_ADDRESS             (MMU_REG_OFFSET + 227*4)
#define MMU_REG_ADDRESS_MSB         (MMU_REG_OFFSET + 228*4)

#define MTLB_PCIE_START_ADDRESS  0x00100000
#define PAGE_PCIE_START_ADDRESS  0x00200000 /* page_table_entry start address */
#define STLB_PCIE_START_ADDRESS  0x00300000
#define PAGE_TABLE_ENTRY_SIZE    64

enum MMUStatus {
  MMU_STATUS_OK                          =    0,

  MMU_STATUS_FALSE                       =   -1,
  MMU_STATUS_INVALID_ARGUMENT            =   -2,
  MMU_STATUS_INVALID_OBJECT              =   -3,
  MMU_STATUS_OUT_OF_MEMORY               =   -4,
  MMU_STATUS_NOT_FOUND                   =   -19,
};

struct addr_desc {
  void                       *virtual_address;  /* buffer virtual address */
  unsigned int               bus_address;  /* buffer physical address */
  unsigned int               size;  /* physical size */
};

struct kernel_addr_desc {
  unsigned long long         bus_address;  /* buffer virtual address */
  unsigned int               mmu_bus_address;  /* buffer physical address in MMU*/
  unsigned int               size;  /* physical size */
};


#define HANTRO_IOC_MMU  'm'

#define HANTRO_IOCS_MMU_MEM_MAP    _IOWR(HANTRO_IOC_MMU, 1, struct addr_desc *)
#define HANTRO_IOCS_MMU_MEM_UNMAP  _IOWR(HANTRO_IOC_MMU, 2, struct addr_desc *)
#define HANTRO_IOCS_MMU_ENABLE     _IOWR(HANTRO_IOC_MMU, 3, unsigned int *)
#define HANTRO_IOCS_MMU_FLUSH      _IOWR(HANTRO_IOC_MMU, 4, unsigned int *)
#define HANTRO_IOC_MMU_MAXNR 4

#define MAX_SUBSYS_NUM  4   /* up to 4 subsystem (temporary) */
#define HXDEC_MAX_CORES MAX_SUBSYS_NUM    /* used in hantro_dec.c */

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

unsigned long long GetMMUAddress(void);
long MMUIoctl(unsigned int cmd, void *filp, unsigned long arg,
              volatile unsigned char *hwregs[MAX_SUBSYS_NUM][2]);

void MMURestore(volatile unsigned char *hwregs[MAX_SUBSYS_NUM][2]);

#ifdef __cplusplus
}
#endif

#endif
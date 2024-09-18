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

#ifndef _IO_TOOLS_
#define _IO_TOOLS_

#include "basetype.h"

u32 ioread32(volatile void* addr);
void iowrite32(u32 val,volatile void *addr);
u16 ioread16(volatile void* addr);
u8 ioread8(volatile void* addr);
void iowrite16(u16 val,volatile void *addr);
void iowrite8(u8 val,volatile void *addr);
u32 readl(volatile void* addr);
void writel(unsigned int v, volatile void *addr);
#define read_mreg32(addr) ioread32((void*)(addr))
#define write_mreg32(addr,val) iowrite32(val, (void*)(addr))
#define read_mreg16(addr) ioread16((void*)addr)
#define write_mreg16(addr,val) iowrite16(val, (void*)(addr))
#define read_mreg8(addr) ioread8((void*)addr)
#define write_mreg8(addr,val) iowrite8(val, (void*)(addr))

//Dec
int hantrodec_init(void);
int hantrodec_open(int *inode, int filp);
int hantrodec_release(int *inode, int filp);
long hantrodec_ioctl(int filp, unsigned int cmd, void *arg);

//VCMD interfaces funcs
int hantrovcmd_init(void);
int hantrovcmd_open(int *inode, int filp);
int hantrovcmd_release(int *inode, int filp);
long hantrovcmd_ioctl(int filp, unsigned int cmd, void *arg);
//typedef int (*IRQHandler)(i32 i, void* data);
typedef void (*IRQHandler)(void* data);

#endif //_IO_TOOLS_
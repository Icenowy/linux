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

#include "subsys.h"

/******************************************************************************/
/* subsystem configuration                                                    */
/******************************************************************************/

/* List of subsystems */
struct SubsysDesc subsys_array[] = {
  /* {slice_index, index, base} */
  {0, 0, 0xffecc00000},
//  {0, 1, 0x700000}
};

/* List of all HW cores. */
struct CoreDesc core_array[] = {
  /* {slice, subsys, core_type, offset, iosize, irq, has_apbfilter} */
#if 0
  {0, 0, HW_VC8000DJ, 0x600000, 0, 0},
  {0, 0, HW_VC8000D, 0x602000, 0, 0},
  {0, 0, HW_L2CACHE, 0x604000, 0, 0},
  {0, 0, HW_DEC400, 0x606000, 0, 0},
  {0, 0, HW_BIGOCEAN, 0x608000, 0, 0},
  {0, 0, HW_NOC,0x60a000, 0, 0},
  {0, 0, HW_AXIFE, 0x60c000, 0, 0}
#endif
  {0, 0, HW_VCMD, 0x0, 27*4, 12},
  {0, 0, HW_VC8000D, 0x1000, 1023*4, -1, 0},
  {0, 0, HW_L2CACHE, 0x2000, 231*4, -1, 0},
  {0, 0, HW_MMU, 0x3000, 228*4, -1, 0},
  //{0, 0, HW_MMU_WR, 0x4000, 228*4, -1, 0},
  //{0, 0, HW_AXIFE, 0x5000, 64*4, -1, 1},
  {0, 0, HW_DEC400, 0x6000, 1568*4, -1, 0},
  //{0, 1, HW_VCMD, 0x0, 27*4, -1, 0},
  //{0, 1, HW_VC8000D, 0x1000, 503*4, -1, 1},
  //{0, 1, HW_L2CACHE, 0x2000, 231*4, -1, 0},
  //{0, 1, HW_MMU, 0x3000, 228*4, -1, 0},
  //{0, 1, HW_MMU_WR, 0x4000, 228*4, -1, 0},
  //{0, 1, HW_AXIFE, 0x5000, 64*4, -1, 1},
  //{0, 1, HW_DEC400, 0x6000, 1568*4, -1, 0},
};

extern struct vcmd_config vcmd_core_array[MAX_SUBSYS_NUM];
extern int total_vcmd_core_num;
extern unsigned long multicorebase[];
extern int irq[];
extern unsigned int iosize[];
extern int reg_count[];

/*
   If VCMD is used, convert core_array to vcmd_core_array, which are used in
   hantor_vcmd.c.
   Otherwise, covnert core_array to multicore_base/irq/iosize, which are used in
   hantro_dec.c

   VCMD:
        - struct vcmd_config vcmd_core_array[MAX_SUBSYS_NUM]
        - total_vcmd_core_num

   NON-VCMD:
        - multicorebase[HXDEC_MAX_CORES]
        - irq[HXDEC_MAX_CORES]
        - iosize[HXDEC_MAX_CORES]
*/
void CheckSubsysCoreArray(struct subsys_config *subsys, int *vcmd) {
  int num = sizeof(subsys_array)/sizeof(subsys_array[0]);
  int i, j;

  memset(subsys, 0, sizeof(subsys[0])*MAX_SUBSYS_NUM);
  for (i = 0; i < num; i++) {
    subsys[i].base_addr = subsys_array[i].base;
    subsys[i].irq = -1;
    for (j = 0; j < HW_CORE_MAX; j++) {
      subsys[i].submodule_offset[j] = 0xffff;
      subsys[i].submodule_iosize[j] = 0;
      subsys[i].submodule_hwregs[j] = NULL;
    }
  }

  total_vcmd_core_num = 0;

  for (i = 0; i < sizeof(core_array)/sizeof(core_array[0]); i++) {
    if (!subsys[core_array[i].subsys].base_addr) {
      /* undefined subsystem */
      continue;
    }
    subsys[core_array[i].subsys].submodule_offset[core_array[i].core_type]
                                  = core_array[i].offset;
    subsys[core_array[i].subsys].submodule_iosize[core_array[i].core_type]
                                  = core_array[i].iosize;
    if (subsys[core_array[i].subsys].irq != -1 && core_array[i].irq != -1) {
      if (subsys[core_array[i].subsys].irq != core_array[i].irq) {
        printk(KERN_INFO "hantrodec: hw core type %d irq %d != subsystem irq %d\n",
                          core_array[i].core_type,
                          core_array[i].irq,
                          subsys[core_array[i].subsys].irq);
        printk(KERN_INFO "hantrodec: hw cores of a subsystem should have same irq\n");
      }
	} else if (core_array[i].irq != -1) {
      subsys[core_array[i].subsys].irq = core_array[i].irq;
    }
    subsys[core_array[i].subsys].has_apbfilter[core_array[i].core_type] = core_array[i].has_apb;
    /* vcmd found */
    if (core_array[i].core_type == HW_VCMD) {
      *vcmd = 1;
      total_vcmd_core_num++;
    }
  }

  printk(KERN_INFO "hantrodec: vcmd = %d\n", *vcmd);

  /* To plug into hantro_vcmd.c */
  if (*vcmd) {
    for (i = 0; i < total_vcmd_core_num; i++) {
      vcmd_core_array[i].vcmd_base_addr = subsys[i].base_addr;
      vcmd_core_array[i].vcmd_iosize = subsys[i].submodule_iosize[HW_VCMD];
      vcmd_core_array[i].vcmd_irq = subsys[i].irq;
      vcmd_core_array[i].sub_module_type = 2; /* TODO(min): to be fixed */
      vcmd_core_array[i].submodule_main_addr = subsys[i].submodule_offset[HW_VC8000D];
      vcmd_core_array[i].submodule_dec400_addr = subsys[i].submodule_offset[HW_DEC400];
      vcmd_core_array[i].submodule_L2Cache_addr = subsys[i].submodule_offset[HW_L2CACHE];
      vcmd_core_array[i].submodule_MMU_addr = subsys[i].submodule_offset[HW_MMU];
      vcmd_core_array[i].submodule_MMUWrite_addr = subsys[i].submodule_offset[HW_MMU_WR];
      vcmd_core_array[i].submodule_axife_addr = subsys[i].submodule_offset[HW_AXIFE];
    }
  }
  memset(multicorebase, 0, sizeof(multicorebase[0]) * HXDEC_MAX_CORES);
  for (i = 0; i < num; i++) {
    multicorebase[i] = subsys[i].base_addr + subsys[i].submodule_offset[HW_VC8000D];
    irq[i] = subsys[i].irq;
    iosize[i] = subsys[i].submodule_iosize[HW_VC8000D];
    printk(KERN_INFO "hantrodec: [%d] multicorebase 0x%08lx, iosize %d\n", i, multicorebase[i], iosize[i]);
  }
}

void dump_core_array(void)
{
	int i;
	for (i = 0; i < sizeof(core_array)/sizeof(core_array[0]); i++) {
		printk(KERN_INFO "lucz: dumping dump_core_array[%d]\n", i);
		printk(KERN_INFO "  slice=%d\n", core_array[i].slice);
		printk(KERN_INFO "  subsys=%d\n", core_array[i].subsys);
		printk(KERN_INFO "  core_type=%d\n", core_array[i].core_type);
		printk(KERN_INFO "  offset=%d\n", core_array[i].offset);
		printk(KERN_INFO "  iosize=%d\n", core_array[i].iosize);
		printk(KERN_INFO "  irq=%d\n", core_array[i].irq);
		printk(KERN_INFO "  has_apb=%d\n", core_array[i].has_apb);
	}
}


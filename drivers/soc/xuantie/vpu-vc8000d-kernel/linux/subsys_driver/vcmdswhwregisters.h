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

/*------------------------------------------------------------------------------

    Table of contents

    1. Include headers
    2. External compiler flags
    3. Module defines

------------------------------------------------------------------------------*/
#ifndef VCMD_SWHWREGISTERS_H
#define VCMD_SWHWREGISTERS_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/
#ifdef __FREERTOS__
#include "basetype.h"
#include "io_tools.h"
#elif defined(__linux__)
#ifndef MODEL_SIMULATION
#include <linux/ioctl.h>
#include <linux/kernel.h>
#include <linux/module.h>
#endif
#endif

#ifdef __FREERTOS__
//addr_t has been defined in basetype.h //Now the FreeRTOS mem need to support 64bit env
#elif defined(__linux__)
typedef size_t    addr_t;
#endif
typedef addr_t    ptr_t;

#undef PDEBUG   /* undef it, just in case */
#ifdef REGISTER_DEBUG
#  ifdef __KERNEL__
    /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_INFO "memalloc: " fmt, ## args)
#  else
    /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, ...)  /* not debugging: nothing */
#endif

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

#define ASIC_VCMD_SWREG_AMOUNT                  27
#define VCMD_REGISTER_CONTROL_OFFSET            0X40
#define VCMD_REGISTER_INT_STATUS_OFFSET         0X44
#define VCMD_REGISTER_INT_CTL_OFFSET            0X48
#define VCMD_REGISTER_EXT_INT_GATE_OFFSET       0X64

/* HW Register field names */
typedef enum
{
#include "vcmdregisterenum.h"
  VcmdRegisterAmount
} regVcmdName;

/* HW Register field descriptions */
typedef struct
{
  u32 name;               /* Register name and index  */
  int base;               /* Register base address  */
  u32 mask;               /* Bitmask for this field */
  int lsb;                /* LSB for this field [31..0] */
  int trace;              /* Enable/disable writing in swreg_params.trc */
  int rw;                 /* 1=Read-only 2=Write-only 3=Read-Write */
  char *description;      /* Field description */
} regVcmdField_s;

/* Flags for read-only, write-only and read-write */
#define RO 1
#define WO 2
#define RW 3

#define REGBASE(reg) (asicVcmdRegisterDesc[reg].base)

/* Description field only needed for system model build. */
#ifdef TEST_DATA
#define VCMDREG(name, base, mask, lsb, trace, rw, desc) \
        {name, base, mask, lsb, trace, rw, desc}
#else
#define VCMDREG(name, base, mask, lsb, trace, rw, desc) \
        {name, base, mask, lsb, trace, rw, ""}
#endif


/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/
extern const regVcmdField_s asicVcmdRegisterDesc[];

/*------------------------------------------------------------------------------

    EncAsicSetRegisterValue

    Set a value into a defined register field

------------------------------------------------------------------------------*/
static inline void vcmd_set_register_mirror_value(u32 *reg_mirror, regVcmdName name, u32 value)
{
  const regVcmdField_s *field;
  u32 regVal;

  field = &asicVcmdRegisterDesc[name];

#ifdef DEBUG_PRINT_REGS
  printf("vcmd_set_register_mirror_value 0x%2x  0x%08x  Value: %10d  %s\n",
         field->base, field->mask, value, field->description);
#endif

  /* Check that value fits in field */
  PDEBUG("field->name == name=%d\n",field->name == name);
  PDEBUG("((field->mask >> field->lsb) << field->lsb) == field->mask=%d\n",((field->mask >> field->lsb) << field->lsb) == field->mask);
  PDEBUG("(field->mask >> field->lsb) >= value=%d\n",(field->mask >> field->lsb) >= value);
  PDEBUG("field->base < ASIC_VCMD_SWREG_AMOUNT * 4=%d\n",field->base < ASIC_VCMD_SWREG_AMOUNT * 4);

  /* Clear previous value of field in register */
  regVal = reg_mirror[field->base / 4] & ~(field->mask);

  /* Put new value of field in register */
  reg_mirror[field->base / 4] = regVal | ((value << field->lsb) & field->mask);
}
static inline u32 vcmd_get_register_mirror_value(u32 *reg_mirror, regVcmdName name)
{
  const regVcmdField_s *field;
  u32 regVal;

  field = &asicVcmdRegisterDesc[name];


  /* Check that value fits in field */
  PDEBUG("field->name == name=%d\n",field->name == name);
  PDEBUG("((field->mask >> field->lsb) << field->lsb) == field->mask=%d\n",((field->mask >> field->lsb) << field->lsb) == field->mask);
  PDEBUG("field->base < ASIC_VCMD_SWREG_AMOUNT * 4=%d\n",field->base < ASIC_VCMD_SWREG_AMOUNT * 4);

  regVal = reg_mirror[field->base / 4];
  regVal = (regVal & field->mask) >> field->lsb;
  
#ifdef DEBUG_PRINT_REGS
  PDEBUG("vcmd_get_register_mirror_value 0x%2x  0x%08x  Value: %10d  %s\n",
         field->base, field->mask, regVal, field->description);
#endif
  return regVal;
}

u32 vcmd_read_reg(const void *hwregs, u32 offset);

void vcmd_write_reg(const void *hwregs, u32 offset, u32 val);


void vcmd_write_register_value(const void *hwregs,u32* reg_mirror,regVcmdName name, u32 value);
u32 vcmd_get_register_value(const void *hwregs, u32* reg_mirror,regVcmdName name);

#define vcmd_set_addr_register_value(reg_base, reg_mirror, name, value) do {\
    if(sizeof(addr_t) == 8) {\
      vcmd_write_register_value((reg_base), (reg_mirror),name, (u32)((addr_t)value));  \
      vcmd_write_register_value((reg_base), (reg_mirror),name##_MSB, (u32)(((addr_t)value) >> 32));\
    } else {\
      vcmd_write_register_value((reg_base),(reg_mirror), name, (u32)((addr_t)value));\
    }\
}while (0)

#define VCMDGetAddrRegisterValue(reg_base, reg_mirror,name)  \
    ((sizeof(addr_t) == 8) ? (\
     (((addr_t)vcmd_get_register_value((reg_base),(reg_mirror), name)) |  \
     (((addr_t)vcmd_get_register_value((reg_base), (reg_mirror),name##_MSB)) << 32))\
    ) : ((addr_t)vcmd_get_register_value((reg_base),(reg_mirror), (name))))

#endif /* VCMD_SWHWREGISTERS_H */

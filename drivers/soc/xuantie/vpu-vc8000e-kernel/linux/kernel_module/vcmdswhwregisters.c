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
/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include <asm/io.h>
#include "vcmdswhwregisters.h"

/* NOTE: Don't use ',' in descriptions, because it is used as separator in csv
 * parsing. */
const regVcmdField_s asicVcmdRegisterDesc[] =
{
#include "vcmdregistertable.h"
};

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* Define this to print debug info for every register write.
#define DEBUG_PRINT_REGS */

/*******************************************************************************
 Function name   : vcmd_read_reg
 Description     : Retrive the content of a hadware register
                    Note: The status register will be read after every MB
                    so it may be needed to buffer it's content if reading
                    the HW register is slow.
 Return type     : u32 
 Argument        : u32 offset
*******************************************************************************/
u32 vcmd_read_reg(const void *hwregs, u32 offset)
{
    u32 val;

    val =(u32) ioread32((void*)hwregs + offset);

    PDEBUG("vcmd_read_reg 0x%02x --> %08x\n", offset, val);

    return val;
}

/*******************************************************************************
 Function name   : vcmd_write_reg
 Description     : Set the content of a hadware register
 Return type     : void 
 Argument        : u32 offset
 Argument        : u32 val
*******************************************************************************/
void vcmd_write_reg(const void *hwregs, u32 offset, u32 val)
{
    iowrite32(val,(void*)hwregs + offset);

    PDEBUG("vcmd_write_reg 0x%02x with value %08x\n", offset, val);
}


/*------------------------------------------------------------------------------

    vcmd_write_register_value

    Write a value into a defined register field (write will happens actually).

------------------------------------------------------------------------------*/
void vcmd_write_register_value(const void *hwregs,u32* reg_mirror,regVcmdName name, u32 value)
{
    const regVcmdField_s *field;
    u32 regVal;

    field = &asicVcmdRegisterDesc[name];

#ifdef DEBUG_PRINT_REGS
    PDEBUG("vcmd_write_register_value 0x%2x  0x%08x  Value: %10d  %s\n",
            field->base, field->mask, value, field->description);
#endif

    /* Check that value fits in field */
    PDEBUG("field->name == name=%d\n",field->name == name);
    PDEBUG("((field->mask >> field->lsb) << field->lsb) == field->mask=%d\n",((field->mask >> field->lsb) << field->lsb) == field->mask);
    PDEBUG("(field->mask >> field->lsb) >= value=%d\n",(field->mask >> field->lsb) >= value);
    PDEBUG("field->base < ASIC_VCMD_SWREG_AMOUNT*4=%d\n",field->base < ASIC_VCMD_SWREG_AMOUNT*4);

    /* Clear previous value of field in register */
    regVal = reg_mirror[field->base/4] & ~(field->mask);

    /* Put new value of field in register */
    reg_mirror[field->base/4] = regVal | ((value << field->lsb) & field->mask);

    /* write it into HW registers */
    vcmd_write_reg(hwregs, field->base,reg_mirror[field->base/4]);
}

/*------------------------------------------------------------------------------

    vcmd_get_register_value

    Get an unsigned value from the ASIC registers

------------------------------------------------------------------------------*/
u32 vcmd_get_register_value(const void *hwregs, u32* reg_mirror,regVcmdName name)
{
  const regVcmdField_s *field;
  u32 value;

  field = &asicVcmdRegisterDesc[name];

  PDEBUG("field->base < ASIC_VCMD_SWREG_AMOUNT * 4=%d\n",field->base < ASIC_VCMD_SWREG_AMOUNT * 4);

  value = reg_mirror[field->base / 4] = vcmd_read_reg(hwregs, field->base);
  value = (value & field->mask) >> field->lsb;

  return value;
}



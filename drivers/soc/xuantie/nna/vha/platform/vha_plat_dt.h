/*!
 *****************************************************************************
 *
 * @File       vha_plat_dt.h
 * ---------------------------------------------------------------------------
 *
 * Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 ("GPL")in which case the provisions of
 * GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms
 * of GPL, and not to allow others to use your version of this file under the
 * terms of the MIT license, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by GPL
 * as set out in the file called "GPLHEADER" included in this distribution. If
 * you do not delete the provisions above, a recipient may use your version of
 * this file under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT_COPYING".
 *
 *****************************************************************************/


#ifndef VHA_PLAT_DT_H
#define VHA_PLAT_DT_H

#include <linux/platform_device.h>

/* OpenFirmware device tree id, for this driver */
#if defined(HW_AX2)

#define VHA_PLAT_DT_OF_ID "img,ax21xx-nna"
#define VHA_PLAT_DT_NAME  "ax21xx-nna"

#elif defined(HW_AX3)

#define VHA_PLAT_DT_OF_ID "img,ax3xxx-nna"
#define VHA_PLAT_DT_NAME  "ax3xxx-nna"

#else

#error "No HW layout defined"

#endif

extern const struct of_device_id vha_plat_dt_of_ids[];

void vha_plat_dt_get_heaps(struct heap_config **heap_configs, int *num_heaps);
int vha_plat_dt_hw_init(struct platform_device *pdev);
void vha_plat_dt_hw_destroy(struct platform_device *pdev);

int vha_plat_dt_hw_suspend(struct platform_device *pdev);
int vha_plat_dt_hw_resume(struct platform_device *pdev);

#endif /* VHA_PLAT_DT_H */

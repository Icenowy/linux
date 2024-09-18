/*!
 *****************************************************************************
 *
 * @File       vha_plat.h
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

#ifndef VHA_PLAT_H
#define VHA_PLAT_H

/* Core clock frequency: default 30MHz */
#define VHA_CORE_CLOCK_MHZ 30

/* Core watchdog cycles default value */
#if defined(HW_AX2)
#define VHA_CORE_WDT_CYCLES           0x7fffff
#elif defined(HW_AX3)
/* MMM can transfer any number of bytes at cost of higher cycles, setting it to ~100ms @800MHz */
#define VHA_CORE_WDT_CYCLES           0xfffffff
/* Memory watchdog is set ~1ms @800MHz which is very safe value to avoid any false interrupts */
#define VHA_CORE_MEM_WDT_CYCLES       0xffffffff
#endif

#ifdef CONFIG_HW_MULTICORE
/* System watchdog cycles default values */
#define VHA_SYS_MEM_WDT_CYCLES        0xffffffff
/* WM watchdog cycles default values */
#define VHA_WM_WDT_CYCLES             0xffffffff
#define VHA_WM_IDLE_WDT_CYCLES        0xfffff
#define VHA_WM_SOCIF_WDT_CYCLES       0xfffff
/* Core watchdog cycles default values */
/* MMM can transfer any number of bytes at cost of higher cycles, setting it to ~100ms @800MHz */
#define VHA_CORE_WDT_CYCLES           0xfffffff
/* Memory watchdog is set ~1ms @800MHz which is very safe value to avoid any false interrupts */
#define VHA_CORE_MEM_WDT_CYCLES       0xffffffff
#define VHA_CORE_SYNC_WDT_CYCLES      0xffff
#endif

/* Memory burst size */
#define VHA_CORE_MH_MAX_BURST_LENGTH 128
/* SLC cache policy type (0-use cache, 1-bypass cache) */
#define VHA_CORE_MH_SLC_CACHE_POLICY_TYPE 1
/* GPU pipe coherent type */
#define VHA_CORE_MH_GPU_PIPE_COHERENT_TYPE 1
/* Persistence priority 0-lowest,3-highest */
#define VHA_CORE_MH_PERSISTENCE_PRIO 0

/* Suspend delay in ms after which the
 * runtime suspend callback is called */
#define VHA_CORE_SUSPEND_DELAY 10

/* Default OCM start address */
#ifdef CONFIG_HW_MULTICORE
#define VHA_OCM_ADDR_START 0x1000
#else
#define VHA_OCM_ADDR_START (~0)
#endif

/* IO hooks */
uint64_t vha_plat_read64(void *addr);
void vha_plat_write64(void *addr, uint64_t val);

int vha_plat_init(void);
int vha_plat_deinit(void);

#endif /* VHA_PLAT_H */

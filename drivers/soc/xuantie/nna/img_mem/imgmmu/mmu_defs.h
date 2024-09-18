/*!
 *****************************************************************************
 *
 * @File        mmu_defs.h
 * @Description Internal MMU library header used to define MMU information at
 *           compilation time and have access to the error printing functions
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

#ifndef MMU_DEFS_H
#define MMU_DEFS_H

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
#include <stdarg.h>
#else
#include <linux/stdarg.h>
#endif
/**
 * @addtogroup IMGMMU_lib
 * @{
 */
/*-------------------------------------------------------------------------
 * Following elements are in the IMGMMU_int documentation module
 *------------------------------------------------------------------------*/

#ifndef IMGMMU_PHYS_SIZE
/** @brief MMU physical address size in bits */
#define IMGMMU_PHYS_SIZE 40
#endif

#ifndef IMGMMU_VIRT_SIZE
/** @brief MMU virtual address size in bits */
#define IMGMMU_VIRT_SIZE 40
#endif

/** @brief Page size in bytes used at PC & PD (always 4k),
 * PT may use variable page size */
#define IMGMMU_PAGE_SIZE 4096u

/** should be log2(IMGMMU_PAGE_SIZE)*3-6 */
#define IMGMMU_CAT_SHIFT 30

/** should be log2(IMGMMU_PAGE_SIZE)*2-3 */
#define IMGMMU_DIR_SHIFT 21

/** should be log2(IMGMMU_PAGE_SIZE) */
#define IMGMMU_PAGE_SHIFT 12

#if IMGMMU_VIRT_SIZE == 40
/**
 * @brief maximum number of directories that
 * can be stored in the catalogue entry
 */
#define IMGMMU_N_DIR (IMGMMU_PAGE_SIZE/4u)
/**
 * @brief maximum number of pagetables that
 * can be stored in the directory entry
 */
#define IMGMMU_N_TABLE (IMGMMU_PAGE_SIZE/8u)
/**
 * @brief maximum number of page mappings in the pagetable
 * for variable page size
 */
#define IMGMMU_N_PAGE (IMGMMU_PAGE_SIZE/ \
		((imgmmu_get_page_size()/IMGMMU_PAGE_SIZE)*8u))
#else
/* it is unlikely to change anyway */
#error "need an update for the new virtual address size"
#endif

/** @brief Memory flag used to mark a page mapping as invalid */
#define MMU_FLAG_VALID 0x1
#define MMU_FLAG_INVALID 0x0

/** @brief Other memory flags */
#define MMU_FLAG_READ_ONLY 0x2

/** @brief PTE entry cache bits mask */
#define MMU_PTE_AXCACHE_MASK 0x3C00000000000000UL

/** @brief PTE entry cache bits shift */
#define MMU_PTE_AXCACHE_SHIFT 58

/** @brief PTE entry parity bit shift */
#define MMU_PTE_PARITY_SHIFT 62

/*
 * internal printing functions
 */
__printf(4, 5)
void _mmu_log(int err, const char *function, uint32_t line, const char *format, ...);

#define mmu_log_err(...) _mmu_log(1, __func__, __LINE__, __VA_ARGS__)

#define mmu_log_dbg(...)
/*
 * #define mmu_log_dbg(...) _mmu_log(0, __func__, __LINE__, __VA_ARGS__)
 */

/**
 * @}
 */
/*-------------------------------------------------------------------------
 * End of the IMGMMU_int documentation module
 *------------------------------------------------------------------------*/

#endif /* MMU_DEFS_H */

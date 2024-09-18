/*!
 *****************************************************************************
 *
 * @File           heap.h
 * @Description    MMU Library: device virtual allocation (heap)
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

#ifndef IMGMMU_HEAP_H
#define IMGMMU_HEAP_H

/**
 * @defgroup IMGMMU_heap MMU Heap Interface
 * @brief The API for the device virtual address Heap - must be implemented
 * (see tal_heap.c for an example implementation)
 * @ingroup IMGMMU_lib
 * @{
 */
/*-----------------------------------------------------------------------------
 * Following elements are in the IMGMMU_heap documentation module
 *---------------------------------------------------------------------------*/

/** @brief An allocation on a heap. */
struct imgmmu_halloc {
    /** @brief Start of the allocation */
	uint64_t vaddr;
    /** @brief Size in bytes */
	size_t size;
};

/**
 * @brief A virtual address heap - not directly related to HW MMU directory
 * entry
 */
struct imgmmu_heap {
    /** @brief Start of device virtual address */
	uint64_t vaddr_start;
    /** @brief Allocation atom in bytes */
	size_t atom;
    /** @brief Total size of the heap in bytes */
	size_t size;
    /** Guard band indicator. */
	bool guard_band;
};

/**
 * @name Device virtual address allocation (heap management)
 * @{
 */

/**
 * @brief Create a Heap
 *
 * @param vaddr_start start of the heap - must be a multiple of atom
 * @param atom the minimum possible allocation on the heap in bytes
 * - usually related to the system page size
 * @param size total size of the heap in bytes
 * @param guard_band enables/disables creation of a gap
 *                   between virtual addresses.
 *                   NOTE: The gap has size of atom.
 * @param res must be non-NULL - used to give detail about error
 *
 * @return pointer to the new Heap object and res is 0
 * @return NULL and the value of res can be:
 * @li -ENOMEM if internal allocation failed
 */
struct imgmmu_heap *imgmmu_hcreate(uintptr_t vaddr_start,
		size_t atom, size_t size, bool guard_band, int *res);

/**
 * @brief Allocate from a heap
 *
 * @warning Heap do not relate to each other, therefore one must insure that
 * they should not overlap if they should not.
 *
 * @param heap must not be NULL
 * @param size allocation size in bytes
 * @param res must be non-NULL - used to give details about error
 *
 * @return pointer to the new halloc object and res is 0
 * @return NULL and the value of res can be:
 * @li -EINVAL if the give size is not a multiple of
 * heap->atom
 * @li -ENOMEM if the internal structure allocation failed
 * @li -EFAULT if the internal device memory allocator did not
 * find a suitable virtual address
 */
struct imgmmu_halloc *imgmmu_hallocate(struct imgmmu_heap *heap, size_t size,
		int *res);

/**
 * @brief Liberate an allocation
 *
 * @return 0
 */
int imgmmu_hfree(struct imgmmu_halloc *alloc);

/**
 * @brief Destroy a heap object
 *
 * @return 0
 * @return -EFAULT if the given heap still has attached
 * allocation
 */
int imgmmu_hdestroy(struct imgmmu_heap *heap);

/**
 * @}
 */
/*-----------------------------------------------------------------------------
 * End of the public functions
 *---------------------------------------------------------------------------*/

/**
 * @}
 */
/*-----------------------------------------------------------------------------
 * End of the IMGMMU_heap documentation module
 *---------------------------------------------------------------------------*/

#endif /* IMGMMU_HEAP_H */

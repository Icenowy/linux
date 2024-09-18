/*!
 *****************************************************************************
 *
 * @File         kernel_heap.c
 * @Description  MMU Library: device virtual allocation (heap) implementation
 *               using gen_alloc from the Linux kernel
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

#include <linux/module.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/genalloc.h>
#include <linux/slab.h>

#include "mmulib/heap.h"
/* access to MMU info and error printing function */
#include "mmu_defs.h"

/*#define DEBUG_POOL 1 */

/*
 * Internal heap object using genalloc
 */
struct gen_heap {
	struct gen_pool *pool;
	size_t nalloc;
	struct imgmmu_heap hinfo;
};

/*
 * The Heap allocation - contains an imgmmu_halloc
 * that is given to the caller
 */
struct gen_halloc {
	/*
	 * Associated heap
	 */
	struct gen_heap *heap;
	/*
	 * MMU lib allocation part
	 */
	struct imgmmu_halloc virt_mem;
};

/*
 *  be used for debugging
 */
static void pool_crawler(struct gen_pool *pool,
		struct gen_pool_chunk *chunk, void *data) __maybe_unused;

static void pool_crawler(struct gen_pool *pool,
		struct gen_pool_chunk *chunk, void *data)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
	unsigned long size = (chunk->end_addr - chunk->start_addr);
#else
	unsigned long size = (chunk->end_addr - chunk->start_addr + 1);
#endif
	pr_debug("pool 0x%p has chunk 0x%lx to 0x%lx (size = %lu B)\n",
			data, chunk->start_addr, chunk->end_addr, size);
}

struct imgmmu_heap *imgmmu_hcreate(uintptr_t vaddr_start,
		size_t atom, size_t size, bool guard_band, int *res)
{
	struct gen_heap *iheap = NULL;
	int min_order = 0; /* log2 of the alloc atom */
	size_t isize = atom;
	int ret;
	uintptr_t start = vaddr_start;

	WARN_ON(res == NULL);
	WARN_ON(size == 0);

	if (size%atom != 0 || (vaddr_start != 0 && vaddr_start%atom != 0)) {
		mmu_log_err("Wrong input params: %zu %zu %zu %zu %zu %zu\n",
			size, atom, size%atom,
			vaddr_start, atom, vaddr_start%atom);
		*res = -EINVAL;
		return NULL;
	}

	iheap = kzalloc(sizeof(struct gen_heap), GFP_KERNEL);
	if (iheap == NULL) {
		*res = -ENOMEM;
		return NULL;
	}

	iheap->nalloc = 0;

	/* compute log2 of the alloc atom */
	while (isize >>= 1)
		min_order++;

	/* ugly fix for trouble using gen_pool_alloc() when allocating a block
	 * gen_pool_alloc() returns 0 on error alought 0 can be a valid
	 * first virtual address
	 * therefore all addresses are offseted by the allocation atom
	 * to insure 0 is the actual error code
	 */
	if (vaddr_start == 0)
		start = vaddr_start+atom; /* otherwise it is vaddr_start */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0)
	isize = start + size;
#else
	isize = start + size - 1;
#endif
	WARN_ON(isize < start); /* too big! it did an overflow */

	mmu_log_dbg("create genalloc pool of order %u\n", min_order);
	/* -1: not using real inode */
	iheap->pool = gen_pool_create(min_order, -1);

	if (iheap->pool == NULL) {
		*res = -ENOMEM;
		mmu_log_err("Failure to create the genalloc pool\n");
		kfree(iheap);
		return NULL;
	}

	mmu_log_dbg("pool 0x%p order %u region from 0x%x for %zu bytes\n",
			iheap->pool, min_order, start, size);

	ret = gen_pool_add(iheap->pool, start, size, -1);
	if (ret != 0) {
		*res = -EFAULT;
		mmu_log_err("Failure to configure the new pool: %d\n", ret);
		gen_pool_destroy(iheap->pool);
		kfree(iheap);
		return NULL;
	}

#ifdef DEBUG_POOL
	gen_pool_for_each_chunk(iheap->pool, &pool_crawler, iheap->pool);
#endif

	iheap->hinfo.vaddr_start = vaddr_start;
	iheap->hinfo.atom = atom;
	iheap->hinfo.size = size;
	iheap->hinfo.guard_band = guard_band;

	*res = 0;
	return &(iheap->hinfo);
}

struct imgmmu_halloc *imgmmu_hallocate(struct imgmmu_heap *heap,
		size_t size, int *res)
{
	struct gen_heap *iheap = NULL;
	struct gen_halloc *ialloc = NULL;

	WARN_ON(res == NULL);
	WARN_ON(heap == NULL);
	iheap = container_of(heap, struct gen_heap, hinfo);

	if (size%heap->atom != 0 || size == 0) {
		mmu_log_err("invalid alloc size (0x%zx) for atom:%zu\n",
			size, heap->atom);
		*res = -EINVAL;
		return NULL;
	}

	ialloc = kzalloc(sizeof(struct gen_halloc), GFP_KERNEL);
	if (ialloc == NULL) {
		mmu_log_err("failed to allocate internal structure\n");
		*res = -ENOMEM;
		return NULL;
	}
	mmu_log_dbg("heap 0x%p alloc %u\n", iheap->pool, size);


	/* gen_pool_alloc returns 0 on error
	 * that is a problem when 1st valid address is 0
	 * check imgmmu_hcreate for explanations
	 */
	ialloc->virt_mem.vaddr = gen_pool_alloc(iheap->pool,
	/* Take one more atom to create a fake gap between
	 * virtual addresses, when needed */
				iheap->hinfo.guard_band ?
					size + iheap->hinfo.atom :
					size);

	if (ialloc->virt_mem.vaddr == 0) {
		mmu_log_err("failed to allocate from gen_pool_alloc\n");
		*res = -EFAULT;
		kfree(ialloc);
		return NULL;
	}

	mmu_log_dbg(KERN_INFO "heap 0x%p alloc 0x%p %u B atom %u B\n",
		iheap->pool, ialloc->virt_mem.vaddr, size, iheap->hinfo.atom);

	/* if base address is 0 we applied an offset */
	if (iheap->hinfo.vaddr_start == 0)
		ialloc->virt_mem.vaddr -= iheap->hinfo.atom;

	ialloc->virt_mem.size = size;
	ialloc->heap = iheap;

	iheap->nalloc++;

#ifdef DEBUG_POOL
	gen_pool_for_each_chunk(iheap->pool, &pool_crawler, iheap->pool);
#endif

	*res = 0;
	return &(ialloc->virt_mem);
}

int imgmmu_hfree(struct imgmmu_halloc *alloc)
{
	struct gen_halloc *ialloc = NULL;
	uintptr_t addr = 0;
	size_t size;

	WARN_ON(alloc == NULL);
	ialloc = container_of(alloc, struct gen_halloc, virt_mem);

	WARN_ON(ialloc->heap == NULL);
	WARN_ON(ialloc->heap->pool == NULL);
	WARN_ON(ialloc->heap->nalloc == 0);

	mmu_log_dbg("heap 0x%p free 0x%p %u B\n",
			ialloc->heap->pool, alloc->vaddr, alloc->size);

#ifdef DEBUG_POOL
	gen_pool_for_each_chunk(ialloc->heap->pool,
			&pool_crawler, ialloc->heap->pool);
#endif

	addr = alloc->vaddr;
	/* Include a fake gap */
	size = ialloc->heap->hinfo.guard_band ?
				alloc->size + ialloc->heap->hinfo.atom :
				alloc->size;
	/* see the explanation in imgmmu_hcreate to know why + atom */
	if (ialloc->heap->hinfo.vaddr_start == 0)
		addr += ialloc->heap->hinfo.atom;

	gen_pool_free(ialloc->heap->pool, addr, size);

	ialloc->heap->nalloc--;

	kfree(ialloc);
	return 0;
}

int imgmmu_hdestroy(struct imgmmu_heap *heap)
{
	struct gen_heap *iheap = NULL;

	WARN_ON(heap == NULL);
	iheap = container_of(heap, struct gen_heap, hinfo);

	if (iheap->nalloc > 0) {
		mmu_log_err("destroying a heap with non-freed allocation\n");
		return -EFAULT;
	}

	if (iheap->pool != NULL) {
		mmu_log_dbg("destroying genalloc pool 0x%p\n", iheap->pool);
		gen_pool_destroy(iheap->pool);
	}

	kfree(iheap);
	return 0;
}

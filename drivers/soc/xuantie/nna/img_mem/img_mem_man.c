/*!
 *****************************************************************************
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
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

#include <img_mem_man.h>
#include <vha_drv_common.h>
#include <mmu.h>
#include <heap.h>

#include "img_mem_man_priv.h"

/* Maximum number of processes */
#define MAX_PROC_CTX 1000

/* Minimum page size (4KB) bits. */
#define MIN_PAGE_SIZE_BITS 12

struct mem_man {
	struct idr heaps;
	struct idr mem_ctxs;
	struct mutex mutex;

	unsigned cache_usage;
};
/* define like this, so it is easier to convert to a function argument later */
static struct mem_man mem_man_data;

/* wrapper struct for imgmmu_page */
struct mmu_page {
	struct buffer *buffer;
	struct imgmmu_page page;
	unsigned char type;
	bool bypass_addr_trans;
	bool use_parity;
};

static bool trace_physical_pages;
module_param(trace_physical_pages, bool, 0444);
MODULE_PARM_DESC(trace_physical_pages,
		"Enables tracing of physical pages being mapped into MMU");
static bool cache_sync = true;
module_param(cache_sync, bool, 0444);
MODULE_PARM_DESC(cache_sync,
				"cache sync mode: 0-no sync; 1-force sync (even if hw provides coherency);");

/*
 * memory heaps
 */
static char *get_heap_name(enum img_mem_heap_type type)
{
	switch (type) {
	case IMG_MEM_HEAP_TYPE_UNIFIED:
		return "unified";
	case IMG_MEM_HEAP_TYPE_CARVEOUT:
		return "carveout";
	case IMG_MEM_HEAP_TYPE_ION:
		return "ion";
	case IMG_MEM_HEAP_TYPE_DMABUF:
		return "dmabuf";
	case IMG_MEM_HEAP_TYPE_COHERENT:
		return "coherent";
	case IMG_MEM_HEAP_TYPE_ANONYMOUS:
		return "anonymous";
	case IMG_MEM_HEAP_TYPE_OCM:
			return "ocm";
	default:
		WARN_ON(type);
		return "unknown";
	}
}

int img_mem_add_heap(const struct heap_config *heap_cfg, int *heap_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap;
	int (*init_fn)(const struct heap_config *heap_cfg, struct heap *heap);
	int ret;

	pr_debug("%s:%d\n", __func__, __LINE__);

	switch (heap_cfg->type) {
	case IMG_MEM_HEAP_TYPE_UNIFIED:
		init_fn = img_mem_unified_init;
		break;
	case IMG_MEM_HEAP_TYPE_COHERENT:
		init_fn = img_mem_coherent_init;
		break;
#ifdef CONFIG_DMA_SHARED_BUFFER
	case IMG_MEM_HEAP_TYPE_DMABUF:
		init_fn = img_mem_dmabuf_init;
		break;
#endif
#ifdef ION_SUPPORTED
#ifdef CONFIG_ION
	case IMG_MEM_HEAP_TYPE_ION:
		init_fn = img_mem_ion_init;
		break;
#endif
#endif
#ifdef CONFIG_GENERIC_ALLOCATOR
	case IMG_MEM_HEAP_TYPE_CARVEOUT:
		init_fn = img_mem_carveout_init;
		break;
#endif
	case IMG_MEM_HEAP_TYPE_ANONYMOUS:
		init_fn = img_mem_anonymous_init;
		break;
	case IMG_MEM_HEAP_TYPE_OCM:
		init_fn = img_mem_ocm_init;
		break;
	default:
		pr_err("%s: heap type %d unknown\n", __func__, heap_cfg->type);
		return -EINVAL;
	}

	heap = kmalloc(sizeof(struct heap), GFP_KERNEL);
	if (!heap)
		return -ENOMEM;

	ret = mutex_lock_interruptible(&mem_man->mutex);
	if (ret)
		goto lock_failed;

	ret = idr_alloc(&mem_man->heaps, heap, IMG_MEM_MAN_MIN_HEAP,
			IMG_MEM_MAN_MAX_HEAP, GFP_KERNEL);
	if (ret < 0) {
		pr_err("%s: idr_alloc failed\n", __func__);
		goto alloc_id_failed;
	}

	heap->id = ret;
	heap->type = heap_cfg->type;
	heap->options = heap_cfg->options;
	heap->to_dev_addr = heap_cfg->to_dev_addr;
	heap->to_host_addr = heap_cfg->to_host_addr;
	heap->priv = NULL;
	heap->cache_sync = true;
	heap->alt_cache_attr = heap_cfg->cache_attr;

	ret = init_fn(heap_cfg, heap);
	if (ret) {
		pr_err("%s: heap init failed\n", __func__);
		goto heap_init_failed;
	}

	*heap_id = heap->id;
	mutex_unlock(&mem_man->mutex);

	pr_debug("%s created heap %d type %d (%s)\n",
		__func__, *heap_id, heap_cfg->type, get_heap_name(heap->type));
	return 0;

heap_init_failed:
	idr_remove(&mem_man->heaps, heap->id);
alloc_id_failed:
	mutex_unlock(&mem_man->mutex);
lock_failed:
	kfree(heap);
	return ret;
}
EXPORT_SYMBOL(img_mem_add_heap);

static void _img_mem_del_heap(struct heap *heap)
{
	struct mem_man *mem_man = &mem_man_data;

	pr_debug("%s heap %d 0x%p\n", __func__, heap->id, heap);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (heap->ops->destroy)
		heap->ops->destroy(heap);

	idr_remove(&mem_man->heaps, heap->id);
}

void img_mem_del_heap(int heap_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap;

	pr_debug("%s:%d heap %d\n", __func__, __LINE__, heap_id);

	mutex_lock(&mem_man->mutex);

	heap = idr_find(&mem_man->heaps, heap_id);
	if (!heap) {
		pr_warn("%s heap %d not found!\n", __func__, heap_id);
		mutex_unlock(&mem_man->mutex);
		return;
	}

	_img_mem_del_heap(heap);

	mutex_unlock(&mem_man->mutex);

	kfree(heap);
}
EXPORT_SYMBOL(img_mem_del_heap);

int img_mem_get_heap_info(int heap_id, uint8_t *type, uint32_t *attrs)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap;

	pr_debug("%s:%d heap %d\n", __func__, __LINE__, heap_id);

	if (heap_id < IMG_MEM_MAN_MIN_HEAP || heap_id > IMG_MEM_MAN_MAX_HEAP) {
		pr_err("%s heap %d does not match internal constraints <%u - %u>!\n",
				__func__, heap_id, IMG_MEM_MAN_MIN_HEAP, IMG_MEM_MAN_MAX_HEAP);
		return -EINVAL;
	}
	mutex_lock(&mem_man->mutex);

	heap = idr_find(&mem_man->heaps, heap_id);
	if (!heap) {
		pr_debug("%s heap %d not found!\n", __func__, heap_id);
		mutex_unlock(&mem_man->mutex);
		return -ENOENT;
	}

	*type = heap->type;

	*attrs = 0;
	if (heap->ops->import)
		*attrs |= IMG_MEM_HEAP_ATTR_IMPORT;
	if (heap->ops->export)
		*attrs |= IMG_MEM_HEAP_ATTR_EXPORT;
	if (heap->ops->alloc && !heap->ops->import)
		*attrs |= IMG_MEM_HEAP_ATTR_INTERNAL;
	if (heap->type == IMG_MEM_HEAP_TYPE_OCM)
		*attrs = IMG_MEM_HEAP_ATTR_SEALED;

	/* User attributes */
	*attrs |= heap->options.ocm.hattr;

	mutex_unlock(&mem_man->mutex);

	return 0;
}
EXPORT_SYMBOL(img_mem_get_heap_info);

/*
 * related to process context (contains SYSMEM heap's functionality in general)
 */
int img_mem_create_proc_ctx(struct mem_ctx **new_ctx)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mem_ctx *ctx;
	int ret = 0;

	pr_debug("%s:%d\n", __func__, __LINE__);

	ctx = kzalloc(sizeof(struct mem_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	idr_init(&ctx->buffers);
	INIT_LIST_HEAD(&ctx->mmu_ctxs);

	mutex_lock(&mem_man->mutex);
	ret = idr_alloc(&mem_man->mem_ctxs, ctx, 0 , MAX_PROC_CTX,
			GFP_KERNEL);
	if (ret < 0) {
		mutex_unlock(&mem_man->mutex);
		pr_err("%s: idr_alloc failed\n", __func__);
		goto idr_alloc_failed;
	}
	/* Assign id to the newly created context. */
	ctx->id = ret;
	mutex_unlock(&mem_man->mutex);
	pr_debug("%s id:%d\n", __func__, ctx->id);

	*new_ctx = ctx;
	return 0;

idr_alloc_failed:
	kfree(ctx);
	return ret;
}
EXPORT_SYMBOL(img_mem_create_proc_ctx);

static void _img_mem_free(struct buffer *buffer);
static void _img_mmu_unmap(struct mmu_ctx_mapping *mapping);
static void _img_mmu_ctx_destroy(struct mmu_ctx *ctx);

static void _img_mem_destroy_proc_ctx(struct mem_ctx *ctx)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	int buf_id;

	pr_debug("%s:%d id:%d\n", __func__, __LINE__, ctx->id);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	/* free derelict mmu contexts */
	while (!list_empty(&ctx->mmu_ctxs)) {
		struct mmu_ctx *mc;

		mc = list_first_entry(&ctx->mmu_ctxs,
							struct mmu_ctx, mem_ctx_entry);
		pr_warn("%s: found derelict mmu context %p\n", __func__, mc);
		_img_mmu_ctx_destroy(mc);
		kfree(mc);
	}

	/* free derelict buffers */
	buf_id = IMG_MEM_MAN_MIN_BUFFER;
	buffer = idr_get_next(&ctx->buffers, &buf_id);
	while (buffer) {
		pr_warn("%s: found derelict buffer %d\n", __func__, buf_id);
		_img_mem_free(buffer);
		buf_id = IMG_MEM_MAN_MIN_BUFFER;
		buffer = idr_get_next(&ctx->buffers, &buf_id);
	}

	idr_destroy(&ctx->buffers);
	idr_remove(&mem_man->mem_ctxs, ctx->id);
}

void img_mem_destroy_proc_ctx(struct mem_ctx *ctx)
{
	struct mem_man *mem_man = &mem_man_data;

	pr_debug("%s:%d\n", __func__, __LINE__);

	mutex_lock(&mem_man->mutex);
	_img_mem_destroy_proc_ctx(ctx);
	mutex_unlock(&mem_man->mutex);

	kfree(ctx);
}
EXPORT_SYMBOL(img_mem_destroy_proc_ctx);

static int _img_mem_alloc(struct device *device, struct mem_ctx *ctx,
				struct heap *heap, size_t size,
				enum img_mem_attr attr, struct buffer **buffer_new)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	int ret;
	/* Allocations for MMU pages are still 4k so CPU page size is enough */
	size_t align = attr & IMG_MEM_ATTR_MMU ?
		imgmmu_get_cpu_page_size() : IMGMMU_GET_MAX_PAGE_SIZE();

	pr_debug("%s heap %p '%s' ctx %p size %zu\n", __func__,
			heap, get_heap_name(heap->type), ctx, size);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (size == 0) {
		pr_err("%s: buffer size is zero\n", __func__);
		return -EINVAL;
	}

	if (heap->ops == NULL || heap->ops->alloc == NULL) {
		pr_err("%s: no alloc function in heap %d!\n",
					__func__, heap->id);
		return -EINVAL;
	}

	buffer = kzalloc(sizeof(struct buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	ret = idr_alloc(&ctx->buffers, buffer,
			(IMG_MEM_MAN_MAX_BUFFER * ctx->id) +
				IMG_MEM_MAN_MIN_BUFFER,
			(IMG_MEM_MAN_MAX_BUFFER * ctx->id) +
				IMG_MEM_MAN_MAX_BUFFER,
			GFP_KERNEL);
	if (ret < 0) {
		pr_err("%s: idr_alloc failed\n", __func__);
		goto idr_alloc_failed;
	}

	buffer->id = ret;
	buffer->request_size = size;
	buffer->actual_size = ((size + align - 1) /
			align) * align;
	buffer->device = device;
	buffer->mem_ctx = ctx;
	buffer->heap = heap;
	INIT_LIST_HEAD(&buffer->mappings);
	buffer->kptr = NULL;
	buffer->priv = NULL;

	/* Check if heap has been registered using an alternative cache attributes */
	if (heap->alt_cache_attr &&
			(heap->alt_cache_attr != (attr & IMG_MEM_ATTR_CACHE_MASK))) {
		pr_debug("%s heap %d changing cache attributes from %x to %x\n",
			__func__, heap->id, attr & IMG_MEM_ATTR_CACHE_MASK,
			heap->alt_cache_attr);
		attr &= ~IMG_MEM_ATTR_CACHE_MASK;
		attr |= heap->alt_cache_attr;
	}

	ret = heap->ops->alloc(device, heap, buffer->actual_size, attr, buffer);
	if (ret) {
		pr_err("%s: heap %d alloc failed\n", __func__, heap->id);
		goto heap_alloc_failed;
	}

	if (heap->type != IMG_MEM_HEAP_TYPE_OCM) {
		__img_pdump_printf(device, "-- Allocating zeroed buffer id:%d size:%zu\n",
				buffer->id, buffer->actual_size);
		__img_pdump_printf(device, "CALLOC "_PMEM_":BLOCK_%d %#zx %#zx 0x0\n",
				buffer->id, buffer->actual_size, align);
	}

	ctx->mem_usage_curr += buffer->actual_size;
	if (ctx->mem_usage_curr > ctx->mem_usage_max)
		ctx->mem_usage_max = ctx->mem_usage_curr;

	*buffer_new = buffer;

	pr_debug("%s heap %p ctx %p created buffer %d (%p) actual_size %zu\n",
		__func__, heap, ctx, buffer->id, buffer, buffer->actual_size);
	return 0;

heap_alloc_failed:
	idr_remove(&ctx->buffers, buffer->id);
idr_alloc_failed:
	kfree(buffer);
	return ret;
}

int img_mem_alloc(struct device *device, struct mem_ctx *ctx, int heap_id,
			size_t size, enum img_mem_attr attr, int *buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap;
	struct buffer *buffer;
	int ret;

	pr_debug("%s heap %d ctx %p size %zu\n", __func__, heap_id, ctx, size);

	ret = mutex_lock_interruptible(&mem_man->mutex);
	if (ret)
		return ret;

	heap = idr_find(&mem_man->heaps, heap_id);
	if (!heap) {
		pr_err("%s: heap id %d not found\n", __func__, heap_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	ret = _img_mem_alloc(device, ctx, heap, size, attr, &buffer);
	if (ret) {
		mutex_unlock(&mem_man->mutex);
		return ret;
	}

	*buf_id = buffer->id;
	mutex_unlock(&mem_man->mutex);

	pr_debug("%s heap %d ctx %p created buffer %d (%p) size %zu\n",
		__func__, heap_id, ctx, *buf_id, buffer, size);
	return ret;
}
EXPORT_SYMBOL(img_mem_alloc);

static int _img_mem_import(struct device *device,
				struct mem_ctx *ctx, struct heap *heap,
				size_t size, enum img_mem_attr attr, uint64_t buf_fd,
				struct page **pages, struct buffer **buffer_new)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	int ret;
	size_t align = IMGMMU_GET_MAX_PAGE_SIZE();

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (size == 0) {
		pr_err("%s: buffer size is zero\n", __func__);
		return -EINVAL;
	}

	if (heap->ops == NULL || heap->ops->import == NULL) {
		pr_err("%s: no import function in heap %d!\n",
					__func__, heap->id);
		return -EINVAL;
	}

	buffer = kzalloc(sizeof(struct buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	ret = idr_alloc(&ctx->buffers, buffer,
			(IMG_MEM_MAN_MAX_BUFFER * ctx->id) +
				IMG_MEM_MAN_MIN_BUFFER,
			(IMG_MEM_MAN_MAX_BUFFER * ctx->id) +
				IMG_MEM_MAN_MAX_BUFFER,
			GFP_KERNEL);
	if (ret < 0) {
		pr_err("%s: idr_alloc failed\n", __func__);
		goto idr_alloc_failed;
	}

	buffer->id = ret;
	buffer->request_size = size;
	buffer->actual_size = ((size + align - 1) /
			align) * align;
	buffer->device = device;
	buffer->mem_ctx = ctx;
	buffer->heap = heap;
	INIT_LIST_HEAD(&buffer->mappings);
	buffer->kptr = NULL;
	buffer->priv = NULL;

	/* If MMU page size is bigger than CPU page size
	 * we need an extra check against requested size
	 * The aligned size comparing to requested size
	 * can't be bigger than CPU page!
	 * otherwise it can cause troubles when
	 * HW tries to access non existing pages */
	if (buffer->actual_size - buffer->request_size >
			imgmmu_get_cpu_page_size()) {
		pr_err("%s: original buffer size is not MMU page size aligned!\n",
				__func__);
		ret = -EINVAL;
		goto heap_import_failed;
	}

	/* Check if heap has been registered using an alternative cache attributes */
	if (heap->alt_cache_attr &&
			(heap->alt_cache_attr != (attr & IMG_MEM_ATTR_CACHE_MASK))) {
		pr_debug("%s heap %d changing cache attributes from %x to %x\n",
			__func__, heap->id, attr & IMG_MEM_ATTR_CACHE_MASK,
			heap->alt_cache_attr);
		attr &= ~IMG_MEM_ATTR_CACHE_MASK;
		attr |= heap->alt_cache_attr;
	}

	ret = heap->ops->import(device, heap, buffer->actual_size, attr,
				buf_fd, pages, buffer);
	if (ret) {
		pr_err("%s: heap %d import failed\n", __func__, heap->id);
		goto heap_import_failed;
	}

	__img_pdump_printf(device, "-- Allocating zeroed buffer id:%d size:%zu for imported data\n",
			buffer->id, buffer->actual_size);
	__img_pdump_printf(device, "CALLOC "_PMEM_":BLOCK_%d %#zx %#zx 0x0\n",
			buffer->id, buffer->actual_size, align);

	ctx->mem_usage_curr += buffer->actual_size;
	if (ctx->mem_usage_curr > ctx->mem_usage_max)
		ctx->mem_usage_max = ctx->mem_usage_curr;

	*buffer_new = buffer;
	return 0;

heap_import_failed:
	idr_remove(&ctx->buffers, buffer->id);
idr_alloc_failed:
	kfree(buffer);
	return ret;
}

static void _img_mem_put_pages(size_t size, struct page **pages)
{
	int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	int i;

	for (i = 0; i < num_pages; i++)
		if (pages[i])
			put_page(pages[i]);		
	kfree(pages);
}

static int _img_mem_get_user_pages(size_t size, uint64_t cpu_ptr,
				struct page **pages[])
{
	struct page **tmp_pages = NULL;
	int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	int ret;
	int cnt = 0;

	/* Check alignment */
	if (cpu_ptr & (PAGE_SIZE-1)) {
		pr_err("%s wrong alignment of %#llx address!\n",
				__func__, cpu_ptr);
		return -EFAULT;
	}

	tmp_pages = kmalloc_array(num_pages, sizeof(struct page *),
			GFP_KERNEL | __GFP_ZERO);
	if (!tmp_pages) {
		pr_err("%s failed to allocate memory for pages\n", __func__);
		return -ENOMEM;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0)
	down_read(&current->mm->mmap_sem);
#else
	mmap_read_lock(current->mm);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
	ret = get_user_pages(
			cpu_ptr, num_pages,
			0,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
			tmp_pages);
#else
			tmp_pages, NULL);
#endif
#else
	pr_err("%s get_user_pages not supported for this kernel version\n",
					__func__);
	ret = -1;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0)
	up_read(&current->mm->mmap_sem);
#else
	mmap_read_unlock(current->mm);
#endif
	if (ret != num_pages) {
		pr_err("%s failed to get_user_pages count:%d for %#llx address\n",
				__func__, num_pages, cpu_ptr);
		ret = -ENOMEM;
		goto out_get_user_pages;
	}

	*pages = tmp_pages;

	return 0;

out_get_user_pages:
	_img_mem_put_pages(size, tmp_pages);

	return ret;	
}

int img_mem_import(struct device *device, struct mem_ctx *ctx, int heap_id,
			size_t size, enum img_mem_attr attr, uint64_t buf_fd,
			uint64_t cpu_ptr, int *buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap;
	struct buffer *buffer;
	struct page **pages = NULL;
	int ret;

	pr_debug("%s heap %d ctx %p hnd %#llx\n", __func__, heap_id, ctx, buf_fd);

	if (cpu_ptr) {
		ret = _img_mem_get_user_pages(size, cpu_ptr, &pages);
		if (ret) {
			pr_err("%s:%d getting user pages failed\n", __func__, __LINE__);
			return ret;
		}
	}

	ret = mutex_lock_interruptible(&mem_man->mutex);
	if (ret) {
		pr_err("%s:%d lock interrupted: mem_man->mutex\n", __func__, __LINE__);

		goto lock_interrupted;
	}

	heap = idr_find(&mem_man->heaps, heap_id);
	if (!heap) {
		pr_err("%s: heap id %d not found\n", __func__, heap_id);
		ret = -EINVAL;
		goto idr_find_failed;
	}

	ret = _img_mem_import(device, ctx, heap, size, attr, buf_fd, pages, &buffer);
	if (ret)
		goto mem_import_failed;

	*buf_id = buffer->id;
	mutex_unlock(&mem_man->mutex);

	if (cpu_ptr)
		_img_mem_put_pages(size, pages);

	pr_debug("%s buf_fd %#llx heap %d (%s) buffer %d size %zu\n", __func__,
		buf_fd, heap_id, get_heap_name(heap->type), *buf_id, size);
	pr_debug("%s heap %d ctx %p created buffer %d (%p) size %zu\n",
		__func__, heap_id, ctx, *buf_id, buffer, size);

	return 0;

mem_import_failed:
idr_find_failed:
	mutex_unlock(&mem_man->mutex);
lock_interrupted:
	if (cpu_ptr)
		_img_mem_put_pages(size, pages);

	return ret;
}
EXPORT_SYMBOL(img_mem_import);

static int _img_mem_export(struct device *device,
				struct mem_ctx *ctx, struct heap *heap,
				size_t size, enum img_mem_attr attr,
				struct buffer *buffer, uint64_t *buf_hnd)
{
	struct mem_man *mem_man = &mem_man_data;
	int ret;

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (size > buffer->actual_size) {
		pr_err("%s: buffer size (%zu) bigger than actual size (%zu)\n",
				__func__, size, buffer->actual_size);
		return -EINVAL;
	}

	if (heap->ops == NULL || heap->ops->export == NULL) {
		pr_err("%s: no export function in heap %d!\n",
					__func__, heap->id);
		return -EINVAL;
	}

	ret = heap->ops->export(device, heap, buffer->actual_size, attr,
				buffer, buf_hnd);
	if (ret) {
		pr_err("%s: heap %d export failed\n", __func__, heap->id);
		return -EFAULT;
	}

	return ret;
}

int img_mem_export(struct device *device, struct mem_ctx *ctx, int buf_id,
			size_t size, enum img_mem_attr attr, uint64_t *buf_hnd)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap;
	struct buffer *buffer;
	int ret;

	pr_debug("%s ctx %p buffer id %d\n", __func__, ctx, buf_id);

	ret = mutex_lock_interruptible(&mem_man->mutex);
	if (ret)
		return ret;

	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	heap = buffer->heap;

	ret = _img_mem_export(device, ctx, heap, size, attr, buffer, buf_hnd);
	if (ret) {
		mutex_unlock(&mem_man->mutex);
		return ret;
	}

	mutex_unlock(&mem_man->mutex);

	pr_debug("%s buf_hnd %#llx heap %d (%s) buffer %d size %zu\n", __func__,
		*buf_hnd, heap->id, get_heap_name(heap->type), buf_id, size);
	pr_debug("%s heap %d ctx %p exported buffer %d (%p) size %zu\n",
		__func__, heap->id, ctx, buf_id, buffer, size);
	return ret;
}
EXPORT_SYMBOL(img_mem_export);

static void _img_mem_free(struct buffer *buffer)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap = buffer->heap;
	struct mem_ctx *ctx = buffer->mem_ctx;

	pr_debug("%s buffer 0x%p\n", __func__, buffer);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (heap->ops == NULL || heap->ops->free == NULL) {
		pr_err("%s: no free function in heap %d!\n",
				__func__, heap->id);
		return;
	}

	while (!list_empty(&buffer->mappings)) {
		struct mmu_ctx_mapping *map;

		map = list_first_entry(&buffer->mappings,
							struct mmu_ctx_mapping, buffer_entry);
		pr_debug("%s: found mapping for buffer %d (size %zu)\n",
			__func__, map->buffer->id, map->buffer->actual_size);
		_img_mmu_unmap(map);
		kfree(map);
	}

	heap->ops->free(heap, buffer);
	if (ctx->mem_usage_curr >= buffer->actual_size)
		ctx->mem_usage_curr -= buffer->actual_size;
	else
		WARN_ON(1);

	idr_remove(&ctx->buffers, buffer->id);

	if (heap->type != IMG_MEM_HEAP_TYPE_OCM) {
		__img_pdump_printf(buffer->device, "-- Freeing buffer id:%d  size:%zu\n",
				buffer->id, buffer->actual_size);
		__img_pdump_printf(buffer->device, "FREE "_PMEM_":BLOCK_%d\n", buffer->id);
	}
	kfree(buffer);
}

void img_mem_free(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);

	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return;
	}

	_img_mem_free(buffer);

	mutex_unlock(&mem_man->mutex);
}
EXPORT_SYMBOL(img_mem_free);

#ifdef KERNEL_DMA_FENCE_SUPPORT
/*
 * dma_fence ops
 */
static const char *_img_mem_sync_get_driver_name(struct dma_fence *f)
{
	return "buf_sync";
}
static const char *_img_mem_sync_get_timeline_name(struct dma_fence *f)
{
	return "buf_timeline";
}
static bool _img_mem_sync_enable_signaling(struct dma_fence *f)
{
	return true;
}
static void _img_mem_sync_release(struct dma_fence *fence)
{
	dma_fence_free(fence);
}
static struct dma_fence_ops dma_fence_ops = {
	.get_driver_name = _img_mem_sync_get_driver_name,
	.get_timeline_name = _img_mem_sync_get_timeline_name,
	.enable_signaling = _img_mem_sync_enable_signaling,
	.release = _img_mem_sync_release,
	.wait = dma_fence_default_wait
};

struct dma_fence * img_mem_add_fence(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);

	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return NULL;
	}

	if (buffer->fence) {
		pr_err("%s: fence for buffer id %d already allocated and not freed \n",
					__func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return NULL;
	}

	buffer->fence = kmalloc(sizeof(struct buffer_fence), GFP_KERNEL);
	if (!buffer->fence) {
		pr_err("%s: cannot allocate fence for buffer id %d\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return NULL;
	}

	spin_lock_init(&buffer->fence->lock);
	dma_fence_init(&buffer->fence->fence,
					&dma_fence_ops,
					&buffer->fence->lock,
					dma_fence_context_alloc(1),
					1);

	mutex_unlock(&mem_man->mutex);

	return &buffer->fence->fence;
}
EXPORT_SYMBOL(img_mem_add_fence);

void img_mem_remove_fence(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	struct dma_fence *fence = NULL;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);

	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return;
	}

	if (buffer->fence) {
		fence = &buffer->fence->fence;
		buffer->fence = NULL;
	}

	mutex_unlock(&mem_man->mutex);

	if (fence)
		dma_fence_signal(fence);
}
EXPORT_SYMBOL(img_mem_remove_fence);

int img_mem_signal_fence(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	struct dma_fence *fence = NULL;
	int ret = -1;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);

	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -1;
	}
	if (buffer->fence) {
		fence = &buffer->fence->fence;
		buffer->fence = NULL;
	}

	mutex_unlock(&mem_man->mutex);

	if (fence)
		ret = dma_fence_signal(fence);

	return ret;
}
EXPORT_SYMBOL(img_mem_signal_fence);
#endif

static void _img_mem_sync_device_to_cpu(struct buffer *buffer, bool force);

int img_mem_map_um(struct mem_ctx *ctx, int buf_id, struct vm_area_struct *vma)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	struct heap *heap;
	int ret;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}
	pr_debug("%s:%d buffer 0x%p\n", __func__, __LINE__, buffer);

	heap = buffer->heap;
	if (heap->ops == NULL || heap->ops->map_um == NULL) {
		pr_err("%s: no map_um in heap %d!\n", __func__, heap->id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	ret = heap->ops->map_um(heap, buffer, vma);
	/* Always invalidate the buffer when it is mapped into UM for reading */
	if (!ret && (vma->vm_flags & VM_READ) && !(vma->vm_flags & VM_WRITE))
		_img_mem_sync_device_to_cpu(buffer, false);

	mutex_unlock(&mem_man->mutex);

	return ret;
}
EXPORT_SYMBOL(img_mem_map_um);

int img_mem_unmap_um(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	struct heap *heap;
	int ret;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}
	pr_debug("%s:%d buffer 0x%p\n", __func__, __LINE__, buffer);

	heap = buffer->heap;
	if (heap->ops == NULL || heap->ops->unmap_um == NULL) {
		pr_err("%s: no map_um in heap %d!\n", __func__, heap->id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	ret = heap->ops->unmap_um(heap, buffer);

	mutex_unlock(&mem_man->mutex);

	return ret;
}
EXPORT_SYMBOL(img_mem_unmap_um);

static int _img_mem_map_km(struct buffer *buffer)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap = buffer->heap;

	pr_debug("%s:%d buffer 0x%p\n", __func__, __LINE__, buffer);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (heap->ops == NULL || heap->ops->map_km == NULL) {
		pr_err("%s: no map_km in heap %d!\n", __func__, heap->id);
		return -EINVAL;
	}

	return heap->ops->map_km(heap, buffer);
}

int img_mem_map_km(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	int ret;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	ret = _img_mem_map_km(buffer);

	mutex_unlock(&mem_man->mutex);

	return ret;
}
EXPORT_SYMBOL(img_mem_map_km);

static int _img_mem_unmap_km(struct buffer *buffer)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap = buffer->heap;

	pr_debug("%s:%d buffer 0x%p\n", __func__, __LINE__, buffer);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (heap->ops == NULL || heap->ops->unmap_km == NULL) {
		pr_err("%s: no unmap_km in heap %d!\n", __func__, heap->id);
		return -EINVAL;
	}

	return heap->ops->unmap_km(heap, buffer);
}

int img_mem_unmap_km(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	int ret;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	ret = _img_mem_unmap_km(buffer);

	mutex_unlock(&mem_man->mutex);

	return ret;
}
EXPORT_SYMBOL(img_mem_unmap_km);

uint64_t *img_mem_get_page_array(struct mem_ctx *mem_ctx, int buf_id)
{
	struct buffer *buffer;
	struct heap *heap;
	struct mem_man *mem_man = &mem_man_data;
	uint64_t *addrs = NULL;
	int ret;

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&mem_ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n",
				__func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return NULL;
	}

	heap = buffer->heap;
	if (heap && heap->ops && heap->ops->get_page_array) {
		ret = heap->ops->get_page_array(heap, buffer, &addrs);
		if (ret || addrs == NULL) {
			pr_err("%s: no page array for heap %d buffer %d\n",
						__func__, heap->id, buffer->id);
		}
	} else
		pr_err("%s: heap %d does not support page arrays\n",
					__func__, heap->id);
	mutex_unlock(&mem_man->mutex);
	return addrs;
}
EXPORT_SYMBOL(img_mem_get_page_array);

/* gets physical address of a single page at given offset */
uint64_t img_mem_get_single_page(struct mem_ctx *mem_ctx, int buf_id,
		unsigned int offset)
{
	struct buffer *buffer;
	struct heap *heap;
	struct mem_man *mem_man = &mem_man_data;
	int ret;
	uint64_t addr = 0;

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&mem_ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -1;
	}

	heap = buffer->heap;
	if (!heap) {
		pr_err("%s: buffer %d does not point any heap it belongs to!\n",
				__func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -1;
	}

	if (heap->ops && heap->ops->get_sg_table) {
		struct sg_table *sgt;
		struct scatterlist *sgl;
		int offs = offset;
		bool use_sg_dma = false;

		ret = heap->ops->get_sg_table(heap, buffer, &sgt, &use_sg_dma);
		if (ret) {
			pr_err("%s: heap %d buffer %d no sg_table!\n",
						__func__, heap->id, buffer->id);
			mutex_unlock(&mem_man->mutex);
			return -1;
		}
		sgl = sgt->sgl;
		while (sgl) {
			if (use_sg_dma)
				offs -= sg_dma_len(sgl);
			else
				offs -= sgl->length;

			if (offs <= 0)
				break;
			sgl = sg_next(sgl);
		}
		if (!sgl) {
			pr_err("%s: heap %d buffer %d wrong offset %d!\n",
					__func__, heap->id, buffer->id, offset);
			mutex_unlock(&mem_man->mutex);
			return -1;
		}

		if (use_sg_dma)
			addr = sg_dma_address(sgl);
		else
			addr = sg_phys(sgl);

	} else if (heap->ops && heap->ops->get_page_array) {
		uint64_t *addrs;
		int page_idx = offset / PAGE_SIZE;

		ret = heap->ops->get_page_array(heap, buffer, &addrs);
		if (ret) {
			pr_err("%s: heap %d buffer %d no page array!\n",
						__func__, heap->id, buffer->id);
			mutex_unlock(&mem_man->mutex);
			return -1;
		}

		if (offset > buffer->actual_size) {
			pr_err("%s: heap %d buffer %d wrong offset %d!\n",
						__func__, heap->id, buffer->id, offset);
			mutex_unlock(&mem_man->mutex);
			return -1;
		}
		addr = addrs[page_idx];
	}

	mutex_unlock(&mem_man->mutex);
	return addr;
}
EXPORT_SYMBOL(img_mem_get_single_page);

void *img_mem_get_kptr(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	void *kptr;

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return NULL;
	}
	kptr = buffer->kptr;
	mutex_unlock(&mem_man->mutex);
	return kptr;
}
EXPORT_SYMBOL(img_mem_get_kptr);

phys_addr_t img_mem_get_dev_addr(struct mem_ctx *mem_ctx, int buf_id,
		phys_addr_t addr)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	struct heap *heap;

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&mem_ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return addr;
	}
	heap = buffer->heap;
	if (heap->to_dev_addr)
		addr = heap->to_dev_addr(&heap->options, addr);

	mutex_unlock(&mem_man->mutex);
	return addr;
}
EXPORT_SYMBOL(img_mem_get_dev_addr);

int img_mmu_init_cache(struct mmu_ctx *mmu_ctx,	unsigned long cache_phys_start,
		uint32_t cache_size)
{
	struct mem_man *mem_man = &mem_man_data;
	struct pdump_descr* pdump = vha_pdump_dev_get_drvdata(mmu_ctx->device);

	mutex_lock(&mem_man->mutex);

	mmu_ctx->cache_phys_start = cache_phys_start;
	mmu_ctx->cache_size = cache_size;

	if (img_pdump_enabled(pdump) && cache_size && !mem_man->cache_usage) {
		__img_pdump_printf(mmu_ctx->device, "-- Allocating img mem cache buffer size:%u\n", cache_size);
		__img_pdump_printf(mmu_ctx->device, "CALLOC :OCM:BLOCK_CACHE %#x %#zx 0x0\n",
			cache_size, IMGMMU_GET_MAX_PAGE_SIZE());
	}

	mem_man->cache_usage++;

	mutex_unlock(&mem_man->mutex);

	return 0;
}
EXPORT_SYMBOL(img_mmu_init_cache);

int img_mmu_clear_cache(struct mmu_ctx *mmu_ctx)
{
	struct mem_man *mem_man = &mem_man_data;
	struct pdump_descr* pdump = vha_pdump_dev_get_drvdata(mmu_ctx->device);

	mutex_lock(&mem_man->mutex);

	if (mem_man->cache_usage)
		mem_man->cache_usage--;

	if (img_pdump_enabled(pdump) && mmu_ctx->cache_size && !mem_man->cache_usage) {
		__img_pdump_printf(mmu_ctx->device, "-- Freeing img mem cache buffer size:%u\n",
				mmu_ctx->cache_size);
		__img_pdump_printf(mmu_ctx->device, "FREE :OCM:BLOCK_CACHE\n");
	}

	mutex_unlock(&mem_man->mutex);

	return 0;
}
EXPORT_SYMBOL(img_mmu_clear_cache);

int img_mmu_move_pg_to_cache(struct mmu_ctx *mmu_ctx, struct mem_ctx *mem_ctx,
		int buf_id, uint64_t virt_addr, uint32_t page_size, uint32_t page_idx)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;
	struct mmu_ctx_mapping *mapping;
	int ret = -EINVAL;

	if (page_size != imgmmu_get_page_size()) {
		pr_err("%s: page sizes does not match!\n", __func__);
		return -EINVAL;
	}

	if (!mmu_ctx->mmu_cat) {
		pr_err("%s: trying to move pages with mmu disabled!\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&mem_ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	if (buffer->actual_size <= page_idx * imgmmu_get_page_size()) {
		pr_err("%s: trying to remap out of the buffer boundaries!\n", __func__);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	list_for_each_entry(mapping, &buffer->mappings, buffer_entry) {
		if (mapping->virt_addr == virt_addr) {
			if (mapping->cache_offset + imgmmu_get_page_size() <= mmu_ctx->cache_size) {
				__img_pdump_printf(buffer->device, "-- Move page to CACHE\n");
				ret = imgmmu_cat_override_phys_addr(mmu_ctx->mmu_cat,
					mapping->virt_addr + page_idx * imgmmu_get_page_size(),
					mmu_ctx->cache_phys_start + mapping->cache_offset);

				mapping->cache_offset += imgmmu_get_page_size();
			}
			break;
		}
	}

	mutex_unlock(&mem_man->mutex);

	return ret;
}
EXPORT_SYMBOL(img_mmu_move_pg_to_cache);

static void _img_mem_sync_cpu_to_device(struct buffer *buffer, bool force)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap = buffer->heap;

	if (!cache_sync) {
		pr_debug("%s:%d buffer %d size %zu cache synchronization disabled!\n",
			__func__, __LINE__, buffer->id, buffer->actual_size);
		return;
	}

	pr_debug("%s:%d buffer %d size %zu kptr %p cache(%d:%d)\n",
			__func__, __LINE__, buffer->id, buffer->actual_size,
			buffer->kptr, force, heap->cache_sync);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (heap->ops && heap->ops->sync_cpu_to_dev &&
			(force || heap->cache_sync))
		heap->ops->sync_cpu_to_dev(heap, buffer);

#ifdef CONFIG_ARM
	dmb();
#else
	/* Put memory barrier */
	mb();
#endif
}

int img_mem_sync_cpu_to_device(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	_img_mem_sync_cpu_to_device(buffer, false);

	mutex_unlock(&mem_man->mutex);
	return 0;
}
EXPORT_SYMBOL(img_mem_sync_cpu_to_device);

static void _img_mem_sync_device_to_cpu(struct buffer *buffer, bool force)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap = buffer->heap;

	if (!cache_sync) {
		pr_debug("%s:%d buffer %d size %zu cache synchronization disabled!\n",
			__func__, __LINE__, buffer->id, buffer->actual_size);
		return;
	}

	pr_debug("%s:%d buffer %d size %zu kptr %p cache(%d:%d)\n",
			__func__, __LINE__, buffer->id, buffer->actual_size,
			buffer->kptr, force, heap->cache_sync);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (heap->ops && heap->ops->sync_dev_to_cpu &&
			(force || heap->cache_sync))
		heap->ops->sync_dev_to_cpu(heap, buffer);
}

int img_mem_sync_device_to_cpu(struct mem_ctx *ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct buffer *buffer;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	_img_mem_sync_device_to_cpu(buffer, false);

	mutex_unlock(&mem_man->mutex);
	return 0;
}
EXPORT_SYMBOL(img_mem_sync_device_to_cpu);

int img_mem_get_usage(const struct mem_ctx *ctx, size_t *max, size_t *curr)
{
	struct mem_man *mem_man = &mem_man_data;

	mutex_lock(&mem_man->mutex);
	if (max)
		*max = ctx->mem_usage_max;
	if (curr)
		*curr = ctx->mem_usage_curr;
	mutex_unlock(&mem_man->mutex);

	return 0;
}
EXPORT_SYMBOL(img_mem_get_usage);

int img_mmu_get_usage(const struct mem_ctx *ctx, size_t *max, size_t *curr)
{
	struct mem_man *mem_man = &mem_man_data;

	mutex_lock(&mem_man->mutex);
	if (max)
		*max = ctx->mmu_usage_max;
	if (curr)
		*curr = ctx->mmu_usage_curr;
	mutex_unlock(&mem_man->mutex);

	return 0;
}
EXPORT_SYMBOL(img_mmu_get_usage);

static int  img_mmu_cache_get_offset(struct mem_ctx *mem_ctx,
	unsigned long addr, unsigned int *offset)
{
	struct mmu_ctx *mmu_ctx;

	list_for_each_entry(mmu_ctx, &mem_ctx->mmu_ctxs, mem_ctx_entry) {
		if (addr >= mmu_ctx->cache_phys_start &&
				addr < mmu_ctx->cache_phys_start + mmu_ctx->cache_size) {
			*offset = addr - mmu_ctx->cache_phys_start;
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * related to stream MMU context (contains IMGMMU functionality in general)
 */

static int imgmmu_find_buffer(struct mem_ctx *ctx, uint64_t addr,
						int *buffer_id, unsigned int *buffer_offset)
{
	struct heap *heap;
	struct buffer *buffer;
	int buf_id;
	unsigned int buf_offset;
	int ret;

	for (buf_id = *buffer_id;
			((buffer) = idr_get_next(&ctx->buffers, &buf_id)) != NULL; ++buf_id) {
		heap = buffer->heap;
		if (heap->ops && heap->ops->get_sg_table) {
			struct sg_table *sgt;
			struct scatterlist *sgl;
			bool use_sg_dma = false;

			ret = heap->ops->get_sg_table(heap, buffer, &sgt, &use_sg_dma);
			if (ret) {
				pr_err("%s: heap %d buffer %d no sg_table!\n",
							__func__, heap->id, buffer->id);
				return -EINVAL;
			}

			if (buffer->pcache.last_sgl) {
				sgl = buffer->pcache.last_sgl;
				buf_offset = buffer->pcache.last_offset;
			} else {
				sgl = sgt->sgl;
				buf_offset = 0;
			}

			while (sgl) {
				phys_addr_t phys = use_sg_dma ?
						sg_dma_address(sgl) : sg_phys(sgl);
				unsigned int len = use_sg_dma ?
						sg_dma_len(sgl) : sgl->length;
#if 0
					pr_err("%s: phys %llx len:%d addr:%llx\n",
							__func__, phys,  len, addr);
#endif
				if (phys == addr) {
#if 0
					pr_err("%s: match @addr:%llx buf:%d offs:%d len:%d\n",
						__func__, addr, buffer->id, buf_offset, len);
#endif
					*buffer_id = buffer->id;
					*buffer_offset = buf_offset;
					return 0;
				}
				buffer->pcache.last_offset = buf_offset += len;
				buffer->pcache.last_sgl = sgl = sg_next(sgl);
			}
		} else if (heap->ops && heap->ops->get_page_array) {
			uint64_t *addrs;
			int page_idx;

			ret = heap->ops->get_page_array(heap, buffer, &addrs);
			if (ret) {
				pr_err("%s: heap %d buffer %d no page_array!\n",
							__func__, heap->id, buffer->id);
				return -EINVAL;
			}

			if (buffer->pcache.last_sgl) {
				page_idx = buffer->pcache.last_idx;
				buf_offset = buffer->pcache.last_offset;
			} else {
				page_idx = 0;
				buf_offset = 0;
			}

			while (buf_offset < buffer->actual_size) {
				if (addrs[page_idx] == addr) {
					*buffer_id = buffer->id;
					*buffer_offset = buf_offset;
					return 0;
				}
				buffer->pcache.last_idx = page_idx++;
				buffer->pcache.last_offset = buf_offset += PAGE_SIZE;
			}
		} else {
			pr_err("%s: heap %d buffer %d no phys addrs found!\n",
						__func__, heap->id, buffer->id);
			return -EINVAL;
		}
	}

	return -EINVAL;
}

static struct imgmmu_page *_page_alloc(void *arg, unsigned char type)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mmu_ctx *mmu_ctx = arg;
	struct mmu_page *page;
	struct buffer *buffer;
	struct heap *heap;
	int ret;

	pr_debug("%s:%d arg %p\n", __func__, __LINE__, arg);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	page = kzalloc(sizeof(struct mmu_page), GFP_KERNEL);
	if (!page)
		return NULL;

	__img_pdump_printf(mmu_ctx->device, "-- Allocating MMU page for %s\n",
			type == IMGMMU_PTYPE_PC ? "PC" :
			type == IMGMMU_PTYPE_PD ? "PD" :
			type == IMGMMU_PTYPE_PT ? "PT" :
			"???");
	ret = _img_mem_alloc(mmu_ctx->device, mmu_ctx->mem_ctx, mmu_ctx->heap,
					imgmmu_get_cpu_page_size(), mmu_ctx->config.alloc_attr, &buffer);
	if (ret) {
		pr_err("%s: img_mem_alloc failed (%d)\n", __func__, ret);
		goto free_page;
	}

	ret = _img_mem_map_km(buffer);
	if (ret) {
		pr_err("%s: img_mem_map_km failed (%d)\n", __func__, ret);
		goto free_buffer;
	}

	WARN_ON(!type);
	page->type = type;
	page->buffer = buffer;
	page->page.cpu_addr = (uintptr_t)buffer->kptr;
	memset((void *)page->page.cpu_addr, 0, imgmmu_get_cpu_page_size());
	if (type == IMGMMU_PTYPE_PT)
		page->use_parity = mmu_ctx->config.use_pte_parity;

	heap = buffer->heap;
	if (heap->ops && heap->ops->get_sg_table) {
		struct sg_table *sgt;
		bool use_sg_dma = false;

		ret = heap->ops->get_sg_table(heap, buffer, &sgt, &use_sg_dma);
		if (ret) {
			pr_err("%s: heap %d buffer %d no sg_table!\n",
						__func__, heap->id, buffer->id);
			ret = -EINVAL;
			goto free_buffer;
		}

		if (use_sg_dma)
			page->page.phys_addr = sg_dma_address(sgt->sgl);
		else
			page->page.phys_addr = sg_phys(sgt->sgl);
	} else if (heap->ops && heap->ops->get_page_array) {
		uint64_t *addrs;

		ret = heap->ops->get_page_array(heap, buffer, &addrs);
		if (ret) {
			pr_err("%s: heap %d buffer %d no page array!\n",
						__func__, heap->id, buffer->id);
			ret = -EINVAL;
			goto free_buffer;
		}
		page->page.phys_addr = *addrs; /* we allocated a single page */
	} else {
		pr_err("%s: heap %d buffer %d no get_sg or get_page_array!\n",
					__func__, heap->id, buffer->id);
		ret = -EINVAL;
		goto free_buffer;
	}

	mmu_ctx->mem_ctx->mmu_usage_curr += buffer->actual_size;
	if (mmu_ctx->mem_ctx->mmu_usage_curr > mmu_ctx->mem_ctx->mmu_usage_max)
		mmu_ctx->mem_ctx->mmu_usage_max = mmu_ctx->mem_ctx->mmu_usage_curr;

	pr_debug("%s:%d virt addr %#lx type:%d\n", __func__, __LINE__,
		page->page.cpu_addr, type);
	pr_debug("%s:%d phys addr %#llx\n", __func__, __LINE__,
		page->page.phys_addr);
	return &page->page;

free_buffer:
	_img_mem_free(buffer);
free_page:
	kfree(page);
	return NULL;
}

static void _page_free(struct imgmmu_page *arg)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mmu_page *page;

	page = container_of(arg, struct mmu_page, page);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	pr_debug("%s:%d buffer %u\n", __func__, __LINE__, page->buffer->id);

	if (page->buffer->mem_ctx->mmu_usage_curr >= page->buffer->actual_size)
		page->buffer->mem_ctx->mmu_usage_curr -= page->buffer->actual_size;
	else
		WARN_ON(1);

	_img_mem_free(page->buffer);
	kfree(page);
}

static inline void __pdump_apply_parity(struct device* dev, uint64_t virt,
		const char *block, unsigned int offset)
{
	uint8_t bits;
	/* XOR 32 bit pair <paddr & vaadr> */
	__img_pdump_printf(dev,
			"WRW "_PMEM_":$1 %#llx -- Calculate parity bit\n"
			"WRW "_PMEM_":$2 %s:%#x\n"
			"SHR "_PMEM_":$2 "_PMEM_":$2 %d\n"
			"XOR "_PMEM_":$1 "_PMEM_":$1 "_PMEM_":$2\n",
			virt >> MIN_PAGE_SIZE_BITS, block, offset, MIN_PAGE_SIZE_BITS);
	for (bits = 16; bits >= 1; bits>>=1)
		/* XOR 'bits' pair of previous result */
		__img_pdump_printf(dev,
				"AND "_PMEM_":$2 "_PMEM_":$1 %#x\n"
				"SHR "_PMEM_":$1 "_PMEM_":$1 %d\n"
				"XOR "_PMEM_":$1 "_PMEM_":$1 "_PMEM_":$2\n",
				(1<<bits)-1, bits);
	/* Apply parity bit */
	__img_pdump_printf(dev,
			"SHL "_PMEM_":$1 "_PMEM_":$1 %d\n"
			"OR  "_PMEM_":$0 "_PMEM_":$0 "_PMEM_":$1 -- Apply parity\n",
			imgmmu_get_pte_parity_shift());
}

static void _page_write(struct imgmmu_page *page,
						unsigned int offset, uint64_t entry,
						unsigned int flags, void *priv)
{
	uint64_t *mem64 = (uint64_t *)page->cpu_addr;
	uint32_t *mem32 = (uint32_t *)mem64;
	struct mmu_page *mmu_page;
	struct heap *heap = NULL;
	struct buffer *buf = (struct buffer*)priv;
	struct pdump_descr* pdump;
	uint32_t entry_shift = 0;
	uint64_t cache_bits = 0;
	uint64_t address = entry & IMG_MMU_PHY_ADDR_MASK;
	uint64_t virt = page->virt_base;
	uint64_t paddr = entry;

	mmu_page = container_of(page, struct mmu_page, page);
	virt += ((1<<imgmmu_get_entry_shift(mmu_page->type))) * offset;

	if (mmu_page->type == IMGMMU_PTYPE_PC ||
		mmu_page->type == IMGMMU_PTYPE_PD)
		heap = mmu_page->buffer->heap;
	else {
		/* PT entries are pointing to buffer which may have been allocated
		 * using different heap than the one used for mmu allocations */
		if (buf)
			heap = buf->heap;
	}

	mmu_page->bypass_addr_trans = (flags & IMGMMU_BYPASS_ADDR_TRANS ? true : false);

	/* Mask MMU flags */
	flags &= IMG_MMU_ENTRY_FLAGS_MASK;

	/* skip translation when flags are zero, assuming address is invalid */
	/* or when page is being remapped to on-chip ram*/
	if (flags && heap && heap->to_dev_addr &&
			!mmu_page->bypass_addr_trans)
		paddr = heap->to_dev_addr(&heap->options, paddr);

	WARN(paddr & ~dma_get_mask(mmu_page->buffer->device),
			"%s: Physical address is out of dma mask, "\
			"Set proper dma mask to avoid cache problems, ", __func__);

	WARN(paddr & ~IMG_MMU_PHY_ADDR_MASK,
		"%s: Physical address exceeds hardware capabilities, "\
		"MMU mapping will be likely invalid", __func__);
	paddr &= IMG_MMU_PHY_ADDR_MASK;

	if (trace_physical_pages && flags) {
		if (mmu_page->type == IMGMMU_PTYPE_PC)
			pr_info("%s: sid:%d off %#03x paddr %#016llx flags %#x type:PC\n",
					__func__, buf ? buf->mem_ctx->id : -1, offset, paddr, flags);
		else
			pr_info("%s: sid:%d off %#03x paddr %#016llx flags %#x type:%s virt:%llx\n",
					__func__, buf ? buf->mem_ctx->id : -1, offset, paddr, flags,
					mmu_page->type == IMGMMU_PTYPE_PD ? "PD" :
					mmu_page->type == IMGMMU_PTYPE_PT ? "PT" :
					"???", virt);
	}

	if (mmu_page->type == IMGMMU_PTYPE_PC) {
		/* Offset of PD physical base address(12)-4(PCE flags) */
		entry_shift = 8;
		/* This is 32 bit entry */
		mem32[offset] = (paddr >> entry_shift) | flags;
	} else if (mmu_page->type == IMGMMU_PTYPE_PD) {
		/* Offset of PT physical base address(12)-12(PDE flags) */
		entry_shift = 0;
		/* This is 64 bit entry */
		mem64[offset] = paddr | flags;
	} else if (mmu_page->type == IMGMMU_PTYPE_PT) {
		/* Offset of page physical base address(12)-12(PTE flags) */
		entry_shift = 0;
		cache_bits = imgmmu_get_pte_cache_bits(entry);
		/* This is 64 bit entry */
		mem64[offset] = cache_bits | paddr | flags;
		if (flags && mmu_page->use_parity) {
			uint64_t par_pair = (virt >> MIN_PAGE_SIZE_BITS) |
					((paddr >> MIN_PAGE_SIZE_BITS) << (sizeof(uint32_t)*8));
			bool par_bit = img_mem_calc_parity(par_pair);
			if (par_bit)
				imgmmu_set_pte_parity(&mem64[offset]);

			if (trace_physical_pages)
				pr_info("%s: [%llx]: %s\n", __func__, mem64[offset],
						par_bit ? "odd parity" : "even parity");
		}
	}

	pdump = vha_pdump_dev_get_drvdata(mmu_page->buffer->device);
	if (img_pdump_enabled(pdump) && flags) {
		/* skip when flags are zero, assuming address is invalid */
		int buffer_id = 0;
		unsigned int buffer_offset = 0;
		int ret;

		if (mmu_page->bypass_addr_trans) {
			ret = img_mmu_cache_get_offset(mmu_page->buffer->mem_ctx, address,
				&buffer_offset);
			if (ret) {
				pr_info("PDUMP: Can't find in cache %#llx\n", address);
			} else {
				/* Cache addresses are only applicable for PT entries */
				WARN_ON(mmu_page->type != IMGMMU_PTYPE_PT);
				__img_pdump_printf(mmu_page->buffer->device,
						"WRW "_PMEM_":$0 :OCM:BLOCK_CACHE:%#x\n"
						"OR  "_PMEM_":$0 "_PMEM_":$0 %d\n",
						buffer_offset, flags);

				if (mmu_page->use_parity) {
					const char block[] = ":OCM:BLOCK_CACHE";
					__pdump_apply_parity(mmu_page->buffer->device, virt, block, buffer_offset);
				}

				if (cache_bits)
					__img_pdump_printf(mmu_page->buffer->device,
						"OR  "_PMEM_":$0 "_PMEM_":$0 %#llx\n",
						cache_bits);
				__img_pdump_printf(mmu_page->buffer->device,
					"WRW64 "_PMEM_":BLOCK_%d:%#zx "_PMEM_":$0 -- PTE\n",
					mmu_page->buffer->id, offset * sizeof(*mem64));
			}
		} else {
			if (mmu_page->type == IMGMMU_PTYPE_PT && buf)
				buffer_id = buf->id;

			ret = imgmmu_find_buffer(mmu_page->buffer->mem_ctx, address,
						&buffer_id, &buffer_offset);
			if (ret) {
				pr_info("PDUMP: Can't find %#llx\n", address);
			} else if (mmu_page->type == IMGMMU_PTYPE_PC) {
				__img_pdump_printf(mmu_page->buffer->device,
					"WRW "_PMEM_":$0 "_PMEM_":BLOCK_%d:%#x\n"
					"SHR "_PMEM_":$0 "_PMEM_":$0 %d\n"
					"OR  "_PMEM_":$0 "_PMEM_":$0 %d\n"
					"WRW "_PMEM_":BLOCK_%d:%#zx "_PMEM_":$0 -- PCE\n",
					buffer_id, buffer_offset,
					entry_shift, flags,
					mmu_page->buffer->id, offset * sizeof(*mem32));
			} else {
				if (mmu_page->type == IMGMMU_PTYPE_PD) {
					__img_pdump_printf(mmu_page->buffer->device,
						"WRW "_PMEM_":$0 "_PMEM_":BLOCK_%d:%#x\n"
						"OR  "_PMEM_":$0 "_PMEM_":$0 %d\n"
						"WRW64 "_PMEM_":BLOCK_%d:%#zx "_PMEM_":$0 -- PDE\n",
						buffer_id, buffer_offset, flags,
						mmu_page->buffer->id, offset * sizeof(*mem64));
				} else if (mmu_page->type == IMGMMU_PTYPE_PT) {
					char block[25];

					if (heap->type == IMG_MEM_HEAP_TYPE_OCM)
						snprintf(block, sizeof(block), ":OCM:BLOCK_CACHE");
					else
						snprintf(block, sizeof(block), ""_PMEM_":BLOCK_%d",
								buffer_id);

					__img_pdump_printf(mmu_page->buffer->device,
						"WRW "_PMEM_":$0 %s:%#x\n"
						"OR  "_PMEM_":$0 "_PMEM_":$0 %d\n",
						block, buffer_offset, flags);

					if (mmu_page->use_parity)
						__pdump_apply_parity(mmu_page->buffer->device, virt, block, buffer_offset);

					if (cache_bits)
						__img_pdump_printf(mmu_page->buffer->device,
							"OR  "_PMEM_":$0 "_PMEM_":$0 %#llx\n",
							cache_bits);
					__img_pdump_printf(mmu_page->buffer->device,
						"WRW64 "_PMEM_":BLOCK_%d:%#zx "_PMEM_":$0 -- PTE\n",
						mmu_page->buffer->id, offset * sizeof(*mem64));
				}
			}
		}
	}
}

static uint64_t _page_read(struct imgmmu_page *page,
					unsigned int offset, void *priv,
					unsigned int *flags)
{
	uint64_t *mem64 = (uint64_t *)page->cpu_addr;
	uint32_t *mem32 = (uint32_t *)mem64;
	struct mmu_page *mmu_page;
	struct heap *heap = NULL;
	uint32_t entry_shift = 0;
	uint64_t entry = 0;
	uint64_t paddr;
	uint64_t virt = page->virt_base;
	struct buffer *buf = (struct buffer*)priv;

	mmu_page = container_of(page, struct mmu_page, page);
	virt += ((1<<imgmmu_get_entry_shift(mmu_page->type))) * offset;

	if (mmu_page->type == IMGMMU_PTYPE_PC ||
		mmu_page->type == IMGMMU_PTYPE_PD)
		heap = mmu_page->buffer->heap;
	else {
		/* PT entries are pointing to buffer which may have been allocated
		 * using different heap than the one used for mmu allocations */
		if (buf)
			heap = buf->heap;
	}

	if (mmu_page->type == IMGMMU_PTYPE_PC) {
		/* Offset of PD physical base address(12)-4(PCE flags) */
		entry_shift = 8;
		/* This is 32 bit entry */
		entry = mem32[offset];
	} else if (mmu_page->type == IMGMMU_PTYPE_PD) {
		/* Offset of PT physical base address(12)-12(PDE flags) */
		entry_shift = 0;
		/* This is 64 bit entry */
		entry = mem64[offset];
	} else if (mmu_page->type == IMGMMU_PTYPE_PT) {
		/* Offset of page physical base address(12)-12(PTE flags) */
		entry_shift = 0;
		/* This is 64 bit entry */
		entry = mem64[offset];
	}

	*flags = entry & IMG_MMU_ENTRY_FLAGS_MASK;
	paddr = entry & ~IMG_MMU_ENTRY_FLAGS_MASK;

	if (mmu_page->type == IMGMMU_PTYPE_PT) {
		/* Mask parity and special cache bits */
		paddr &= ~(1ULL<<imgmmu_get_pte_parity_shift());
		paddr &= ~imgmmu_get_pte_cache_bits(entry);
	}

	paddr <<= entry_shift;

	/* Check if physical address set in PTE is within correct range */
	if (paddr & ~IMG_MMU_PHY_ADDR_MASK) {
		pr_err("%s: mmu page entry (%llx) corruption detected (phys)!\n",
				__func__, paddr);
		*flags = IMG_MMU_ENTRY_FLAGS_MASK;
		goto exit;
	}

	/* Check parity */
	if (*flags && mmu_page->type == IMGMMU_PTYPE_PT &&
			mmu_page->use_parity) {
		uint64_t par_pair = (virt >> MIN_PAGE_SIZE_BITS) |
				((paddr >> MIN_PAGE_SIZE_BITS) << (sizeof(uint32_t)*8));
		bool par_bit = img_mem_calc_parity(par_pair);

		if (trace_physical_pages)
			pr_info("%s: [%llx]: %s\n", __func__, entry,
					par_bit ? "odd parity" : "even parity");
		if ((entry >> imgmmu_get_pte_parity_shift()) != par_bit) {
			pr_err("%s: mmu page entry (%llx) corruption detected (parity)!\n",
					__func__, entry);
			*flags = IMG_MMU_ENTRY_FLAGS_MASK;
			goto exit;
		}
	}

	/* skip translation when flags are zero, assuming address is invalid */
	if (*flags && heap && heap->to_host_addr &&
			!mmu_page->bypass_addr_trans)
		paddr = heap->to_host_addr(&heap->options, paddr);

	/* Check if physical address matches dma mask */
	if (paddr & ~dma_get_mask(mmu_page->buffer->device)) {
		pr_err("%s: mmu page entry (%llx) physical address is out of dma mask!\n"
				"Set proper dma mask to avoid cache problems\n",
				__func__, paddr);
		*flags = IMG_MMU_ENTRY_FLAGS_MASK;
		goto exit;
	}

	/* Sanity check for MMU flags - different on each level */
	if (((mmu_page->type == IMGMMU_PTYPE_PC ||
		mmu_page->type == IMGMMU_PTYPE_PD) &&
			(*flags & ~IMG_MMU_PTE_FLAG_VALID)) ||
		(mmu_page->type == IMGMMU_PTYPE_PT &&
			(*flags & ~(IMG_MMU_PTE_FLAG_VALID|IMG_MMU_PTE_FLAG_READ_ONLY)))) {
		pr_err("%s: mmu page entry corruption detected (flags)!\n",
				__func__);
		*flags = IMG_MMU_ENTRY_FLAGS_MASK;
	}

exit:
	if (trace_physical_pages && *flags) {
		if (mmu_page->type == IMGMMU_PTYPE_PC)
			pr_info("%s: sid:%d off %#03x paddr %#016llx flags %#x type:PC\n",
					__func__, buf ? buf->mem_ctx->id : -1, offset, paddr, *flags);
		else
			pr_info("%s: sid:%d off %#03x paddr %#016llx flags %#x type:%s virt:%llx\n",
					__func__, buf ? buf->mem_ctx->id : -1, offset, paddr, *flags,
					mmu_page->type == IMGMMU_PTYPE_PD ? "PD" :
					mmu_page->type == IMGMMU_PTYPE_PT ? "PT" :
					"???", virt);
	}

	return paddr;
}

static void _update_page(struct imgmmu_page *arg)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mmu_page *page;

	if (trace_physical_pages)
		pr_debug("%s\n", __func__);

	page = container_of(arg, struct mmu_page, page);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	_img_mem_sync_cpu_to_device(page->buffer, true);
}

int img_mmu_ctx_create(struct device *device, const struct mmu_config *config,
					struct mem_ctx *mem_ctx, int heap_id,
					int (*callback_fn)(enum img_mmu_callback_type type,
						int buf_id, void *data),
					void *callback_data, struct mmu_ctx **mmu_ctx)
{
	struct mem_man *mem_man = &mem_man_data;

	static struct imgmmu_info info = {
		.page_alloc = _page_alloc,
		.page_free = _page_free,
		.page_write = _page_write,
		.page_read = _page_read,
		.page_update = _update_page,
	};
	struct mmu_ctx *ctx;
	int res;

	if (((config->addr_width < imgmmu_get_virt_size()) ||
		(config->addr_width < imgmmu_get_phys_size())) &&
			!config->bypass_hw) {
		pr_err("%s: invalid addr_width (%d)!\n",
					__func__, config->addr_width);
		return -EINVAL;
	}

	ctx = kzalloc(sizeof(struct mmu_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->device = device;
	ctx->mem_ctx = mem_ctx;
	memcpy(&ctx->config, config, sizeof(struct mmu_config));

	imgmmu_set_page_size(config->page_size);

	mutex_lock(&mem_man->mutex);

	ctx->heap = idr_find(&mem_man->heaps, heap_id);
	if (!ctx->heap) {
		pr_err("%s: invalid heap_id (%d)!\n", __func__, heap_id);
		mutex_unlock(&mem_man->mutex);
		kfree(ctx);
		return -EINVAL;
	}

	/* Apply offset when needed */
	if (ctx->heap->ops->set_offset) {
		if (ctx->heap->ops->set_offset(ctx->heap, config->bypass_offset)) {
			pr_err("%s: failed to set offset %zu heap_id (%d)!\n",
					__func__, config->bypass_offset, heap_id);
			mutex_unlock(&mem_man->mutex);
			kfree(ctx);
			return -EINVAL;
		}
		pr_debug("%s adding %lx offset bytes to heap %d type %d (%s)\n",
				__func__, config->bypass_offset, ctx->heap->id,
				ctx->heap->type, get_heap_name(ctx->heap->type));
	}

	info.ctx = ctx;
	/* If we are in bypass mode, do not populate hw structures */
	if (!config->bypass_hw) {
		ctx->mmu_cat = imgmmu_cat_create(&info, &res);
		if (res) {
			pr_err("%s: catalogue create failed (%d)!\n",
					__func__, res);
			mutex_unlock(&mem_man->mutex);
			kfree(ctx);
			return -EFAULT;
		}
	} else
		pr_debug("%s imgmmu_cat_create bypass!\n", __func__);

	list_add(&ctx->mem_ctx_entry, &mem_ctx->mmu_ctxs);
	INIT_LIST_HEAD(&ctx->mappings);

	ctx->callback_fn = callback_fn;
	ctx->callback_data = callback_data;

	ctx->id = mem_ctx->id;

	*mmu_ctx = ctx;

	mutex_unlock(&mem_man->mutex);

	return ctx->id;
}
EXPORT_SYMBOL(img_mmu_ctx_create);

static void _img_mmu_ctx_destroy(struct mmu_ctx *ctx)
{
	struct mem_man *mem_man = &mem_man_data;
	int res;

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	while (!list_empty(&ctx->mappings)) {
		struct mmu_ctx_mapping *map;

		map = list_first_entry(&ctx->mappings,
							struct mmu_ctx_mapping, mmu_ctx_entry);
		pr_debug("%s: found mapped buffer %d (size %zu)\n",
			__func__, map->buffer->id, map->buffer->request_size);
		_img_mmu_unmap(map);
		kfree(map);
	}

	if (!ctx->config.bypass_hw) {
		res = imgmmu_cat_destroy(ctx->mmu_cat);
		if (res)
			pr_err("imgmmu_cat_destroy failed (%d)!\n", res);
	} else
		pr_debug("%s imgmmu_cat_destroy bypass!\n", __func__);

	list_del(&ctx->mem_ctx_entry);
}

void img_mmu_ctx_destroy(struct mmu_ctx *ctx)
{
	struct mem_man *mem_man = &mem_man_data;

	mutex_lock(&mem_man->mutex);
	_img_mmu_ctx_destroy(ctx);
	mutex_unlock(&mem_man->mutex);

	kfree(ctx);
}
EXPORT_SYMBOL(img_mmu_ctx_destroy);

int img_mmu_map(struct mmu_ctx *mmu_ctx, struct mem_ctx *mem_ctx, int buf_id,
		uint64_t virt_addr, unsigned int map_flags)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mmu_ctx_mapping *mapping;
	struct imgmmu_halloc heap_alloc;
	struct buffer *buffer;
	struct heap *heap;
	int res = 0;
	int ret;

	pr_debug("%s buffer %d virt_addr %#llx\n",
			__func__, buf_id, virt_addr);

	mapping = kzalloc(sizeof(struct mmu_ctx_mapping), GFP_KERNEL);
	if (!mapping)
		return -ENOMEM;

	mutex_lock(&mem_man->mutex);
	buffer = idr_find(&mem_ctx->buffers, buf_id);
	if (!buffer) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		ret = -EINVAL;
		goto error;
	}
	pr_debug("%s buffer %d 0x%p size %zu virt_addr %#llx\n", __func__,
		buf_id, buffer, buffer->request_size, virt_addr);
	/* Store MMU mapping flags */
	buffer->map_flags = map_flags;

	heap_alloc.vaddr = virt_addr;
	heap_alloc.size = buffer->actual_size;

	mapping->mmu_ctx = mmu_ctx;
	mapping->buffer = buffer;
	mapping->virt_addr = virt_addr;

	if (!mmu_ctx->config.bypass_hw)
		__img_pdump_printf(buffer->device, "-- Mapping "_PMEM_":BLOCK_%d @ 0x%llx\n",
				buf_id, virt_addr);

	heap = buffer->heap;
	if (heap->ops && heap->ops->get_sg_table) {
		struct sg_table *sgt;
		bool use_sg_dma = false;

		ret = heap->ops->get_sg_table(heap, buffer, &sgt, &use_sg_dma);
		if (ret) {
			pr_err("%s: heap %d buffer %d no sg_table!\n",
						__func__, heap->id, buffer->id);
			goto error;
		}

		if (!mmu_ctx->config.bypass_hw)
			mapping->map = imgmmu_cat_map_sg(
						mmu_ctx->mmu_cat,
						sgt->sgl,
						use_sg_dma,
						&heap_alloc,
						map_flags, buffer,
						&res);
		else
			pr_debug("%s imgmmu_cat_map_sg bypass!\n", __func__);
	} else if (heap->ops && heap->ops->get_page_array) {
		uint64_t *addrs;

		ret = heap->ops->get_page_array(heap, buffer, &addrs);
		if (ret) {
			pr_err("%s: heap %d buffer %d no page array!\n",
						__func__, heap->id, buffer->id);
			goto error;
		}

		if (!mmu_ctx->config.bypass_hw)
			mapping->map = imgmmu_cat_map_arr(
						mmu_ctx->mmu_cat,
						addrs,
						&heap_alloc,
						map_flags, buffer,
						&res);
		else
			pr_debug("%s imgmmu_cat_map_arr bypass!\n", __func__);
	} else {
		pr_err("%s: heap %d buffer %d no get_sg or get_page_array!\n",
					__func__, heap->id, buffer->id);
		ret = -EINVAL;
		goto error;
	}
	if (res) {
		pr_err("imgmmu_cat_map failed (%d)!\n", res);
		ret = -EFAULT;
		goto error;
	}

	list_add(&mapping->mmu_ctx_entry, &mmu_ctx->mappings);
	list_add(&mapping->buffer_entry, &mapping->buffer->mappings);

	if (mmu_ctx->callback_fn && !mmu_ctx->config.bypass_hw) {
		ret = mmu_ctx->callback_fn(IMG_MMU_CALLBACK_MAP, buffer->id,
						mmu_ctx->callback_data);
		if (ret) {
			pr_err("%s: imgmmu map callback failed!\n", __func__);
		}
	}
	mutex_unlock(&mem_man->mutex);
	return ret;

error:
	mutex_unlock(&mem_man->mutex);
	kfree(mapping);
	return ret;
}
EXPORT_SYMBOL(img_mmu_map);

static void _img_mmu_unmap(struct mmu_ctx_mapping *mapping)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mmu_ctx *ctx = mapping->mmu_ctx;
	int res;

	pr_debug("%s:%d unmapping %p buffer %d\n",
		__func__, __LINE__, mapping, mapping->buffer->id);

	WARN_ON(!mutex_is_locked(&mem_man->mutex));

	if (!ctx->config.bypass_hw) {
		res = imgmmu_cat_unmap(mapping->map);
		if (res)
			pr_warn("imgmmu_cat_unmap failed (%d)!\n", res);
	} else
		pr_debug("%s imgmmu_cat_unmap bypass!\n", __func__);

	list_del(&mapping->mmu_ctx_entry);
	list_del(&mapping->buffer_entry);

	if (ctx->callback_fn && !ctx->config.bypass_hw)
		ctx->callback_fn(IMG_MMU_CALLBACK_UNMAP, mapping->buffer->id,
				ctx->callback_data);
}

int img_mmu_unmap(struct mmu_ctx *mmu_ctx, struct mem_ctx *mem_ctx, int buf_id)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mmu_ctx_mapping *mapping;
	struct list_head *lst;

	pr_debug("%s:%d buffer %d\n", __func__, __LINE__, buf_id);

	mutex_lock(&mem_man->mutex);

	mapping = NULL;
	list_for_each(lst, &mmu_ctx->mappings) {
		struct mmu_ctx_mapping *m;

		m = list_entry(lst, struct mmu_ctx_mapping, mmu_ctx_entry);
		if (m->buffer->id == buf_id) {
			mapping = m;
			break;
		}
	}

	if (!mapping) {
		pr_err("%s: buffer id %d not found\n", __func__, buf_id);
		mutex_unlock(&mem_man->mutex);
		return -EINVAL;
	}

	_img_mmu_unmap(mapping);

	mutex_unlock(&mem_man->mutex);
	kfree(mapping);
	return 0;
}
EXPORT_SYMBOL(img_mmu_unmap);

int img_mmu_get_conf(size_t *page_size, size_t *virt_size)
{
	if (page_size)
		*page_size = imgmmu_get_page_size();
	if (virt_size)
		*virt_size = imgmmu_get_virt_size();

	return 0;
}
EXPORT_SYMBOL(img_mmu_get_conf);

int img_mmu_get_pc(const struct mmu_ctx *ctx, unsigned int *pc_reg, int *bufid)
{
	struct mem_man *mem_man = &mem_man_data;
	struct imgmmu_page *page = NULL;
	phys_addr_t addr = 0ULL;

	mutex_lock(&mem_man->mutex);
	*pc_reg = 0;

	if (!ctx->config.bypass_hw) {
		struct mmu_page *mmu_page;

		page = imgmmu_cat_get_page(ctx->mmu_cat);
		if (!page) {
			mutex_unlock(&mem_man->mutex);
			return -EINVAL;
		}

		mmu_page = container_of(page, struct mmu_page, page);
		*bufid = mmu_page->buffer->id;

		addr = page->phys_addr;
		if (ctx->heap->to_dev_addr)
			addr = ctx->heap->to_dev_addr(
					&ctx->heap->options,
					addr);

		/* This is PFN of Page Catalogue phy address */
		*pc_reg = (unsigned int)(addr >>= IMG_MMU_PC_ADDR_SHIFT);

		pr_debug("%s: addr %#llx pc %#llx bufid %d\n", __func__,
				page->phys_addr, addr, *bufid);
	} else
		pr_debug("%s imgmmu_cat_get_page bypass!\n", __func__);

	mutex_unlock(&mem_man->mutex);
	return 0;
}
EXPORT_SYMBOL(img_mmu_get_pc);

phys_addr_t img_mmu_get_paddr(const struct mmu_ctx *ctx,
				uint64_t vaddr, uint8_t *flags)
{
	struct mem_man *mem_man = &mem_man_data;
	uint64_t entry = 0;
	phys_addr_t paddr = 0;

	*flags = 0;
	mutex_lock(&mem_man->mutex);

	entry = imgmmu_cat_get_pte(ctx->mmu_cat, vaddr);
	if (entry != ~0) {
		*flags = entry & IMG_MMU_ENTRY_FLAGS_MASK;
		paddr = entry & ~IMG_MMU_ENTRY_FLAGS_MASK;
	}

	mutex_unlock(&mem_man->mutex);

	return paddr;
}
EXPORT_SYMBOL(img_mmu_get_paddr);

/*
 * Wrapper functions for virtual address allocator
 */
int img_mmu_vaa_create(struct device *device,
		uint32_t base, size_t size, struct mmu_vaa **vaa)
{
	struct mem_man *mem_man = &mem_man_data;
	struct mmu_vaa *ctx;
	int ret = 0;

	if (!size)
		return -EINVAL;

	ctx = kzalloc(sizeof(struct mmu_vaa), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock(&mem_man->mutex);

	ctx->heap = imgmmu_hcreate(base, imgmmu_get_page_size(),
			size, true, &ret);
	if (ret) {
		pr_err("%s: imgmmu_hcreate failed (%d)!\n", __func__, ret);
		kfree(ctx);
		ret = -EFAULT;
		goto exit;
	}

	INIT_LIST_HEAD(&ctx->entries);
	ctx->device = device;
	*vaa = ctx;

exit:
	mutex_unlock(&mem_man->mutex);
	return ret;
}
EXPORT_SYMBOL(img_mmu_vaa_create);

int img_mmu_vaa_destroy(struct mmu_vaa *vaa)
{
	struct mem_man *mem_man = &mem_man_data;

	if (!vaa)
		return -EINVAL;

	mutex_lock(&mem_man->mutex);

	while (!list_empty(&vaa->entries)) {
		struct vaa_entry *entry;

		entry = list_first_entry(&vaa->entries,
			struct vaa_entry, mmu_vaa_entry);
		if (imgmmu_hfree(entry->alloc)) {
			pr_err("%s: imgmmu_hfree failed!\n",
				__func__);
			WARN_ON(1);
		}
		list_del(&entry->mmu_vaa_entry);
		kfree(entry);
	}

	if (imgmmu_hdestroy(vaa->heap)) {
		pr_err("%s: imgmmu_hdestroy failed!\n", __func__);
		/* If some attachments are still active */
		WARN_ON(1);
	}
	kfree(vaa);

	mutex_unlock(&mem_man->mutex);

	return 0;
}
EXPORT_SYMBOL(img_mmu_vaa_destroy);

int img_mmu_vaa_alloc(struct mmu_vaa *vaa, size_t size, uint32_t *addr)
{
	struct mem_man *mem_man = &mem_man_data;
	struct imgmmu_halloc *alloc;
	struct vaa_entry *entry;
	int ret = 0;

	if (!vaa || !addr || !size)
		return -EINVAL;

	entry = kzalloc(sizeof(struct vaa_entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	mutex_lock(&mem_man->mutex);

	alloc = imgmmu_hallocate(vaa->heap, size, &ret);
	if (!alloc || ret) {
		pr_err("%s: imgmmu_hallocate failed (%zu)!\n",
			__func__, size);
		kfree(entry);
		ret = -EFAULT;
		goto exit;
	}

	entry->alloc = alloc;
	list_add(&entry->mmu_vaa_entry, &vaa->entries);

	*addr = alloc->vaddr;

exit:
	mutex_unlock(&mem_man->mutex);
	return ret;
}
EXPORT_SYMBOL(img_mmu_vaa_alloc);

int img_mmu_vaa_free(struct mmu_vaa *vaa, uint32_t addr, size_t size)
{
	struct mem_man *mem_man = &mem_man_data;
	struct vaa_entry *entry;
	int ret = 0;

	if (!vaa || !size)
		return -EINVAL;

	mutex_lock(&mem_man->mutex);

	entry = list_first_entry(&vaa->entries,
		struct vaa_entry, mmu_vaa_entry);
	while (!entry) {
		if (entry->alloc->vaddr == addr &&
			entry->alloc->size == size)
			break;
		/* advance */
		entry = list_next_entry(entry, mmu_vaa_entry);
	}

	if (!entry) {
		pr_err("%s: allocation not found (0x%x:%zu)!\n",
				__func__, addr, size);
		ret = -EINVAL;
		goto exit;
	}
	if (imgmmu_hfree(entry->alloc)) {
		pr_err("%s: imgmmu_hfree failed (0x%x:%zu)!\n",
			__func__, addr, size);
		ret = -EFAULT;
		goto exit;
	}
	list_del(&entry->mmu_vaa_entry);
	kfree(entry);

exit:
	mutex_unlock(&mem_man->mutex);
	return ret;
}
EXPORT_SYMBOL(img_mmu_vaa_free);

// Parity look-up table for 8bits
static unsigned int _parity_lut_[256] =
			{0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0,
			0, 1, 1, 0, 1, 0, 0, 1,
			0, 1, 1, 0, 1, 0, 0, 1,
			1, 0, 0, 1, 0, 1, 1, 0};

bool img_mem_calc_parity(unsigned long long input)
{
	// Split by half as number is considered to be of 64 bits
	int bits;

	// Dividing the number into 8-bit chunks while performing X-OR
	for (bits = 32; bits >= 8; bits>>=1)
		input = input ^ (input >> bits);

	return _parity_lut_[input & 0xff] ? true : false;
}
EXPORT_SYMBOL(img_mem_calc_parity);

/*
 * Initialisation
 */
static int __init img_mem_init(void)
{
	struct mem_man *mem_man = &mem_man_data;

	pr_debug("%s:%d\n", __func__, __LINE__);

	idr_init(&mem_man->heaps);
	idr_init(&mem_man->mem_ctxs);
	mutex_init(&mem_man->mutex);
	mem_man->cache_usage = 0;

	return 0;
}

static void __exit img_mem_exit(void)
{
	struct mem_man *mem_man = &mem_man_data;
	struct heap *heap;
	struct mem_ctx *ctx;
	int heap_id;
	int ctx_id;

	pr_debug("%s:%d\n", __func__, __LINE__);

	/* keeps mutex checks (WARN_ON) happy, this will never actually wait */
	mutex_lock(&mem_man->mutex);

	ctx_id = 0;
	ctx = idr_get_next(&mem_man->mem_ctxs, &ctx_id);
	while (ctx) {
		pr_warn("%s derelict memory context %p!\n", __func__, ctx);
		_img_mem_destroy_proc_ctx(ctx);
		kfree(ctx);
		ctx_id = 0;
		ctx = idr_get_next(&mem_man->mem_ctxs, &ctx_id);
	}

	heap_id = IMG_MEM_MAN_MIN_HEAP;
	heap = idr_get_next(&mem_man->heaps, &heap_id);
	while (heap) {
		pr_warn("%s derelict heap %d!\n", __func__, heap_id);
		_img_mem_del_heap(heap);
		kfree(heap);
		heap_id = IMG_MEM_MAN_MIN_HEAP;
		heap = idr_get_next(&mem_man->heaps, &heap_id);
	}
	idr_destroy(&mem_man->heaps);
	idr_destroy(&mem_man->mem_ctxs);

	mutex_unlock(&mem_man->mutex);

	mutex_destroy(&mem_man->mutex);
}

module_init(img_mem_init);
module_exit(img_mem_exit);

MODULE_LICENSE("GPL");

/*
 * coding style for emacs
 *
 * Local variables:
 * indent-tabs-mode: t
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 */

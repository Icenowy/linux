/*!
 *****************************************************************************
 *
 * @File       img_mem_unified.c
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
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/version.h>
#ifdef CONFIG_X86
#include <asm/cacheflush.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
#include <linux/set_memory.h>
#endif
#endif

#include <img_mem_man.h>
#include "img_mem_man_priv.h"

static int trace_physical_pages;
static int trace_mmap_fault;

struct buffer_data {
	struct sg_table *sgt;
	enum img_mem_attr mattr;  /* memory attributes */
	enum dma_data_direction dma_dir;
	struct vm_area_struct *mapped_vma;
	/* exporter via dmabuf */
	struct dma_buf *dma_buf;
	bool exported;
};

static void set_page_cache(struct page *page,
		enum img_mem_attr attr)
{
#ifdef CONFIG_X86
	if (attr & IMG_MEM_ATTR_UNCACHED)
		set_memory_uc((unsigned long)page_address(page), 1);
	else if (attr & IMG_MEM_ATTR_WRITECOMBINE)
		set_memory_wc((unsigned long)page_address(page), 1);
	else if (attr & IMG_MEM_ATTR_CACHED)
		set_memory_wb((unsigned long)page_address(page), 1);
#endif
}

/*
 * dmabuf wrapper ops
 */
static struct sg_table *unified_map_dmabuf(struct dma_buf_attachment *attach,
		enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;
	struct buffer_data *buffer_data;
	struct sg_table *sgt;
	struct scatterlist *src, *dst;
	int ret, i;

	if (!buffer)
		return NULL;

	pr_debug("%s:%d client:%p buffer %d (0x%p)\n", __func__, __LINE__,
			attach->dev, buffer->id, buffer);
	buffer_data = buffer->priv;

	/* Copy sgt so that we make an independent mapping */
	sgt = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (sgt == NULL)
		return NULL;

	ret = sg_alloc_table(sgt, buffer_data->sgt->orig_nents, GFP_KERNEL);
	if (ret)
		goto err_free;

	src = buffer_data->sgt->sgl;
	dst = sgt->sgl;
	for (i = 0; i < buffer_data->sgt->orig_nents; ++i) {
		sg_set_page(dst, sg_page(src), src->length, src->offset);
		dst = sg_next(dst);
		src = sg_next(src);
	}

	ret = dma_map_sg(attach->dev, sgt->sgl, sgt->orig_nents, dir);
	if (ret <= 0) {
		pr_err("%s dma_map_sg failed!\n", __func__);
		goto err_free_sgt;
	}
	sgt->nents = ret;

	return sgt;

err_free_sgt:
	sg_free_table(sgt);
err_free:
	kfree(sgt);
	return NULL;
}

static void unified_unmap_dmabuf(struct dma_buf_attachment *attach,
					struct sg_table *sgt,
					enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;

	pr_debug("%s:%d client:%p buffer %d (0x%p)\n", __func__, __LINE__,
			attach->dev, buffer ? buffer->id : -1, buffer);

	dma_unmap_sg(attach->dev, sgt->sgl, sgt->orig_nents, dir);
	sg_free_table(sgt);
	kfree(sgt);
}

/* Called when when ref counter reaches zero! */
static void unified_release_dmabuf(struct dma_buf *buf)
{
	struct buffer *buffer = buf->priv;
	struct buffer_data *buffer_data;

	if (!buffer)
		return;

	buffer_data = buffer->priv;
	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
			buffer->id, buffer);
	if (!buffer_data)
		return;

	buffer_data->exported = false;
}

static void unified_dma_map(struct buffer *buffer);
static void unified_dma_unmap(struct buffer *buffer);

static int unified_begin_cpu_access_dmabuf(struct dma_buf *buf,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
					size_t start, size_t len,
#endif
					enum dma_data_direction direction)
{
	struct buffer *buffer = buf->priv;
	struct buffer_data *buffer_data;
	struct sg_table *sgt;

	if (!buffer) {
		/* Buffer may have been released, exit silently */
		return 0;
	}

	buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
			buffer->id, buffer);

	buffer_data->dma_dir = direction;
	unified_dma_map(buffer);

	sgt = buffer_data->sgt;
	dma_sync_sg_for_cpu(buffer->device, sgt->sgl, sgt->orig_nents,
						direction);

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static void unified_end_cpu_access_dmabuf(struct dma_buf *buf,
					size_t start, size_t len,
					enum dma_data_direction direction)
#else
static int unified_end_cpu_access_dmabuf(struct dma_buf *buf,
					enum dma_data_direction direction)
#endif
{
	struct buffer *buffer = buf->priv;
	struct buffer_data *buffer_data;
	struct sg_table *sgt;

	if (!buffer) {
		/* Buffer may have been released, exit silently */
		return 0;
	}

	buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
			buffer->id, buffer);

	sgt = buffer_data->sgt;
	dma_sync_sg_for_device(buffer->device, sgt->sgl, sgt->orig_nents,
					direction);

	unified_dma_unmap(buffer);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
	return 0;
#endif
	;
}

/* Called on file descriptor mmap */
static int unified_mmap_dmabuf(struct dma_buf *buf, struct vm_area_struct *vma)
{
	struct buffer *buffer = buf->priv;
	struct buffer_data *buffer_data;
	struct scatterlist *sgl;
	unsigned long addr;

	if (!buffer)
		return -EINVAL;

	buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);
	pr_debug("%s:%d vm_start %#lx vm_end %#lx size %#lx\n",
		__func__, __LINE__,
		vma->vm_start, vma->vm_end, vma->vm_end - vma->vm_start);

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	sgl = buffer_data->sgt->sgl;
	addr = vma->vm_start;
	while (sgl) {
		dma_addr_t phys = sg_phys(sgl);
		unsigned long pfn = phys >> PAGE_SHIFT;
		unsigned int len = sgl->length;
		int ret;

		if (vma->vm_end < (addr + len)) {
			unsigned long size = vma->vm_end - addr;
			pr_debug("%s:%d buffer %d (0x%p) truncating len=%#x to size=%#lx\n",
				__func__, __LINE__,
				buffer->id, buffer, len, size);
			WARN(round_up(size, PAGE_SIZE) != size,
				"VMA size %#lx not page aligned\n", size);
			len = size;
			if (!len) /* VM space is smaller than allocation */
				break;
		}

		ret = remap_pfn_range(vma, addr, pfn, len, vma->vm_page_prot);
		if (ret)
			return ret;

		addr += len;
		sgl = sg_next(sgl);
	}

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
static void *unified_kmap_dmabuf(struct dma_buf *buf, unsigned long page)
{
	pr_err("%s not supported\n", __func__);
	return NULL;
}
#endif

static int unified_map_km(struct heap *heap, struct buffer *buffer);
static int unified_unmap_km(struct heap *heap, struct buffer *buffer);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
static void *unified_vmap_dmabuf(struct dma_buf *buf)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
static int unified_vmap_dmabuf(struct dma_buf *buf, struct dma_buf_map *map)
#else
static int unified_vmap_dmabuf(struct dma_buf *buf, struct iosys_map *map)
#endif
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
		return NULL;

	heap = buffer->heap;

	if (unified_map_km(heap, buffer))
		return NULL;

	pr_debug("%s:%d buffer %d kptr 0x%p\n", __func__, __LINE__,
		buffer->id, buffer->kptr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
	return buffer->kptr;
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	dma_buf_map_set_vaddr(map, buffer->kptr);
	return (buffer->kptr == NULL) ? -ENOMEM : 0;
#else
	iosys_map_set_vaddr(map, buffer->kptr);
    return (buffer->kptr == NULL) ? -ENOMEM : 0;
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
static void unified_vunmap_dmabuf(struct dma_buf *buf, void *kptr)
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
		return;

	heap = buffer->heap;

	pr_debug("%s:%d buffer %d kptr 0x%p (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer->kptr, kptr);

	if (buffer->kptr == kptr)
		unified_unmap_km(heap, buffer);
}
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
static void unified_vunmap_dmabuf(struct dma_buf *buf, struct dma_buf_map *map)
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
		return;

	heap = buffer->heap;

	pr_debug("%s:%d buffer %d kptr 0x%p (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer->kptr, map->vaddr);

	if(buffer->kptr == map->vaddr)
		unified_unmap_km(heap, buffer);
	dma_buf_map_clear(map);
}
#else
static void unified_vunmap_dmabuf(struct dma_buf *buf, struct iosys_map *map)
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
		return;

	heap = buffer->heap;

	pr_debug("%s:%d buffer %d kptr 0x%p (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer->kptr, map->vaddr);

	if(buffer->kptr == map->vaddr)
		unified_unmap_km(heap, buffer);
	iosys_map_clear(map);
}
#endif

static const struct dma_buf_ops unified_dmabuf_ops = {
	.map_dma_buf = unified_map_dmabuf,
	.unmap_dma_buf = unified_unmap_dmabuf,
	.release = unified_release_dmabuf,
	.begin_cpu_access = unified_begin_cpu_access_dmabuf,
	.end_cpu_access = unified_end_cpu_access_dmabuf,
	.mmap = unified_mmap_dmabuf,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
	.kmap_atomic = unified_kmap_dmabuf,
	.kmap = unified_kmap_dmabuf,
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
	.map_atomic = unified_kmap_dmabuf,
	.map = unified_kmap_dmabuf,
#endif
#endif
	.vmap = unified_vmap_dmabuf,
	.vunmap = unified_vunmap_dmabuf,
};

static int unified_export(struct device *device, struct heap *heap,
						 size_t size, enum img_mem_attr attr,
						 struct buffer *buffer, uint64_t* buf_hnd)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct dma_buf *dma_buf;
	int ret, fd;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
#endif
	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	if (!buffer_data)
		/* Nothing to export ? */
		return -ENOMEM;

	if (buffer_data->exported) {
		pr_err("%s: already exported!\n", __func__);
		return -EBUSY;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)
	dma_buf = dma_buf_export(buffer_data, &unified_dmabuf_ops,
			size, O_RDWR);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
	dma_buf = dma_buf_export(buffer_data, &unified_dmabuf_ops,
			size, O_RDWR, NULL);
#else
	exp_info.ops = &unified_dmabuf_ops;
	exp_info.size = size;
	exp_info.flags = O_RDWR;
	exp_info.priv = buffer;
	exp_info.resv = NULL;
	dma_buf = dma_buf_export(&exp_info);
#endif
	if (IS_ERR(dma_buf)) {
		pr_err("%s:dma_buf_export failed\n", __func__);
		ret = PTR_ERR(dma_buf);
		return ret;
	}

	get_dma_buf(dma_buf);
	fd = dma_buf_fd(dma_buf, 0);
	if (fd < 0) {
		pr_err("%s: dma_buf_fd failed\n", __func__);
		dma_buf_put(dma_buf);
		return -EFAULT;
	}
	buffer_data->dma_buf = dma_buf;
	buffer_data->exported = true;
	*buf_hnd = (uint64_t)fd;

	return 0;
}

static int unified_alloc(struct device *device, struct heap *heap,
			size_t size, enum img_mem_attr attr,
			struct buffer *buffer)
{
	struct buffer_data *buffer_data;
	struct sg_table *sgt;
	struct scatterlist *sgl;
	struct page *page, *tmp_page;
	struct list_head pages_list;
	int pages = 0;
	int ret;
	int min_order = heap->options.unified.min_order;
	int max_order = heap->options.unified.max_order;

	if (min_order == 0)
		min_order = IMG_MIN_ALLOC_ORDER_DEFAULT;

	if (max_order == 0)
		max_order = IMG_MAX_ALLOC_ORDER_DEFAULT;

	pr_debug("%s:%d buffer %d (0x%p) size:%zu attr:%x\n", __func__, __LINE__,
		buffer->id, buffer, size, attr);

	/* Allocations for MMU pages are still 4k so CPU page size is enough */
	if (attr & IMG_MEM_ATTR_MMU)
		min_order = get_order(size);

	if (min_order > max_order) {
		pr_err("min_alloc_order > max_alloc_order !\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&pages_list);

	while((long)size > 0) {
		int order;

		page = NULL;
		/* Fit the buffer size starting from the biggest order.
			 When system already run out of chunks with specific order,
			 try with lowest available with min_order constraint */
		for (order = max_order; order >= min_order; order--) {
			int page_order;

			/* Try to allocate min_order size */
			if (size < (PAGE_SIZE << order) && (order > min_order))
				continue;

			page = alloc_pages(heap->options.unified.gfp_type |
					__GFP_COMP | __GFP_NOWARN, order);
			if (!page)
				continue;

			page_order = compound_order(page);
			if (trace_physical_pages)
				pr_info("%s:%d phys %#llx size %lu page_address %p order:%d\n",
					__func__, __LINE__,
					(unsigned long long)page_to_phys(page),
					PAGE_SIZE << page_order, page_address(page), page_order);

			/* The below code is just a sanity check
			 * that dma streaming api is going to work with this device */
			if (!(attr & IMG_MEM_ATTR_UNCACHED)) {
				/*
				 * dma_map_page() is probably going to fail if
				 * alloc flags are GFP_HIGHMEM, since it is not
				 * mapped to CPU. Hopefully, this will never happen
				 * because memory of this sort cannot be used
				 * for DMA anyway. To check if this is the case,
				 * build with debug, set trace_physical_pages=1
				 * and check if page_address printed above is NULL
				 */
				dma_addr_t dma_addr = dma_map_page(device,
						page, 0, PAGE_SIZE << page_order, DMA_BIDIRECTIONAL);
				if (dma_mapping_error(device, dma_addr)) {
					__free_page(page);
					pr_err("%s dma_map_page failed!\n", __func__);
					ret = -EIO;
					goto alloc_pages_failed;
				}
				dma_unmap_page(device, dma_addr,
						PAGE_SIZE, DMA_BIDIRECTIONAL);
			}
			/* Record the max order taking the info
			 * from the page we have just found */
			max_order = page_order;
			break;
		}

		if (!page) {
			pr_err("%s alloc_pages failed!\n", __func__);
			ret = -ENOMEM;
			goto alloc_pages_failed;
		}
		size -= PAGE_SIZE << max_order;

		/* Split pages back to order 0 ->
		 * this is required to properly map into UM */
		if (max_order) {
			struct page *end = page + (1 << max_order);

			split_page(page, max_order);
			while (page < end) {
				list_add_tail(&page->lru, &pages_list);
				pages++;
				/* There should not by any mapping attached to the page at this point,
				 * but clear it just for sanity.
				 * This is workaround for kernel 4.15 & "splited" pages. */
				page->mapping = NULL;
				page++;
			}
		} else {
			list_add_tail(&page->lru, &pages_list);
			pages++;
		}
	}

	sgt = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto alloc_pages_failed;
	}

	ret = sg_alloc_table(sgt, pages, GFP_KERNEL);
	if (ret)
		goto sg_alloc_table_failed;

	sgl = sgt->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages_list, lru) {
		sg_set_page(sgl, page, PAGE_SIZE, 0);
		set_page_cache(page, attr);
		sgl = sg_next(sgl);
		list_del(&page->lru);
	}

	pr_debug("%s:%d buffer %d orig_nents %d\n", __func__, __LINE__,
		buffer->id, sgt->orig_nents);

	buffer_data = kzalloc(sizeof(struct buffer_data), GFP_KERNEL);
	if (!buffer_data) {
		ret = -ENOMEM;
		goto alloc_buffer_data_failed;
	}

	buffer->priv = buffer_data;
	buffer_data->sgt = sgt;
	buffer_data->mattr = attr;
	buffer_data->dma_dir = DMA_NONE;
	buffer_data->mapped_vma = NULL;

	return 0;

alloc_buffer_data_failed:
	sg_free_table(sgt);
sg_alloc_table_failed:
	kfree(sgt);
alloc_pages_failed:
	list_for_each_entry_safe(page, tmp_page, &pages_list, lru) {
		set_page_cache(page, IMG_MEM_ATTR_CACHED);
		__free_page(page);
	}
	return ret;
}

static void unified_dma_map(struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;
	int ret = 0;

	if (buffer_data->dma_dir == DMA_NONE)
		buffer_data->dma_dir = DMA_BIDIRECTIONAL;

	ret = dma_map_sg(buffer->device, sgt->sgl, sgt->orig_nents,
			buffer_data->dma_dir);
	if (ret <= 0) {
		pr_err("%s dma_map_sg failed!\n", __func__);
		buffer_data->dma_dir = DMA_NONE;
		return;
	}
	pr_debug("%s:%d buffer %d orig_nents %d nents %d\n", __func__, __LINE__,
				buffer->id, sgt->orig_nents, ret);
	sgt->nents = ret;
}

static void unified_dma_unmap(struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;

	if (buffer_data->dma_dir == DMA_NONE)
		return;

	dma_unmap_sg(buffer->device, sgt->sgl,
			sgt->orig_nents, buffer_data->dma_dir);
	buffer_data->dma_dir = DMA_NONE;

	pr_debug("%s:%d buffer %d orig_nents %d\n", __func__, __LINE__,
				buffer->id, sgt->orig_nents);
}

static void unified_free(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;
	struct scatterlist *sgl;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	/* If user forgot to unmap, free dma mapping anyway */
	unified_dma_unmap(buffer);

	if (buffer_data->dma_buf) {
		dma_buf_put(buffer_data->dma_buf);
		buffer_data->dma_buf->priv = NULL;
	}

	if (buffer->kptr) {
		pr_debug("%s vunmap 0x%p\n", __func__, buffer->kptr);
		vunmap(buffer->kptr);
	}

	if (buffer_data->mapped_vma)
		buffer_data->mapped_vma->vm_private_data = NULL;

	sgl = sgt->sgl;
	while (sgl) {
		struct page *page = sg_page(sgl);
		if (page) {
			set_page_cache(page, IMG_MEM_ATTR_CACHED);
			__free_page(page);
		}
		sgl = sg_next(sgl);
	}
	sg_free_table(sgt);
	kfree(sgt);
	kfree(buffer_data);
}

static void unified_mmap_open(struct vm_area_struct *vma)
{
	struct buffer *buffer = vma->vm_private_data;
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;

	buffer_data->mapped_vma = vma;

	pr_debug("%s:%d buffer %d (0x%p) vma:%p\n",
			__func__, __LINE__, buffer->id, buffer, vma);
	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED)) {
		if (vma->vm_flags & VM_WRITE)
			buffer_data->dma_dir = DMA_TO_DEVICE;
		else
			buffer_data->dma_dir = DMA_FROM_DEVICE;

		unified_dma_map(buffer);

		/* User will read the buffer so invalidate D-cache */
		if (buffer_data->dma_dir == DMA_FROM_DEVICE)
			dma_sync_sg_for_cpu(buffer->device,
					sgt->sgl,
					sgt->orig_nents,
					DMA_FROM_DEVICE);
	}
}

static void unified_mmap_close(struct vm_area_struct *vma)
{
	struct buffer *buffer = vma->vm_private_data;
	struct buffer_data *buffer_data;
	struct sg_table *sgt;

	if (!buffer)
		return;

	buffer_data = buffer->priv;
	sgt = buffer_data->sgt;

	pr_debug("%s:%d buffer %d (0x%p) vma:%p\n",
			__func__, __LINE__, buffer->id, buffer, vma);
	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED)) {
		/* User may have written to the buffer so flush D-cache */
		if (buffer_data->dma_dir == DMA_TO_DEVICE) {
			dma_sync_sg_for_device(buffer->device,
					sgt->sgl,
					sgt->orig_nents,
					DMA_TO_DEVICE);
			dma_sync_sg_for_cpu(buffer->device,
					sgt->sgl,
					sgt->orig_nents,
					DMA_FROM_DEVICE);
		}

		unified_dma_unmap(buffer);
	}

	buffer_data->mapped_vma = NULL;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static vm_fault_t unified_mmap_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#else
static int unified_mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#endif
	struct buffer *buffer = vma->vm_private_data;
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;
	struct scatterlist *sgl;
	struct page *page = NULL;
	int err;
        unsigned long addr;

	if (trace_mmap_fault) {
		pr_debug("%s:%d buffer %d (0x%p) vma:%p\n",
				__func__, __LINE__, buffer->id, buffer, vma);
		pr_debug("%s:%d vm_start %#lx vm_end %#lx total size %ld\n",
			__func__, __LINE__,
			vma->vm_start, vma->vm_end,
			vma->vm_end - vma->vm_start);
	}

	sgl = sgt->sgl;
	addr = vma->vm_start;
	while (sgl && addr < vma->vm_end) {
		page = sg_page(sgl);
		if (!page) {
			pr_err("%s:%d no page!\n", __func__, __LINE__);
			return VM_FAULT_SIGBUS;
		}
		if (trace_mmap_fault)
			pr_info("%s:%d vmf addr %lx page_address:%p phys:%#llx\n",
				__func__, __LINE__, addr, page,
				(unsigned long long)page_to_phys(page));

		err = vm_insert_page(vma, addr, page);
		switch (err) {
		case 0:
		case -EAGAIN:
		case -ERESTARTSYS:
		case -EINTR:
		case -EBUSY:
			break; // passthrough
		case -ENOMEM:
			return VM_FAULT_OOM;
		default:
			return VM_FAULT_SIGBUS;
		}

		addr += sgl->length;
		sgl = sg_next(sgl);
	}

	return VM_FAULT_NOPAGE;
}

/* vma ops->fault handler is used to track user space mappings
 * (inspired by other gpu/drm drivers from the kernel source tree)
 * to properly call dma_sync_* ops when the mapping is destroyed
 * (when user calls unmap syscall).
 * vma flags are used to choose a correct dma mapping.
 * By default use DMA_BIDIRECTONAL mapping type (kernel space only).
 * The above facts allows us to do automatic cache flushing/invalidation.
 *
 * Examples:
 *  mmap() -> .open -> invalidate buffer cache
 *  .. read content from buffer
 *  unmap() -> .close -> do nothing
 *
 *  mmap() -> .open -> do nothing
 *  .. write content to buffer
 *  unmap() -> .close -> flush buffer cache
 */
static struct vm_operations_struct unified_mmap_vm_ops = {
	.open = unified_mmap_open,
	.close = unified_mmap_close,
	.fault = unified_mmap_fault,
};

static int unified_map_um(struct heap *heap, struct buffer *buffer,
				struct vm_area_struct *vma)
{
	struct buffer_data *buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);
	pr_debug("%s:%d vm_start %#lx vm_end %#lx size %ld\n",
		__func__, __LINE__,
		vma->vm_start, vma->vm_end, vma->vm_end - vma->vm_start);

	/* Throw a warning when attempting
	 * to do dma mapping when already exists */
	WARN_ON(buffer_data->dma_dir != DMA_NONE);

	/* CACHED by default */
	if (buffer_data->mattr & IMG_MEM_ATTR_WRITECOMBINE)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else if (buffer_data->mattr & IMG_MEM_ATTR_UNCACHED)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	vma->vm_ops = &unified_mmap_vm_ops;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;
#else
	vm_flags_clear(vma, VM_PFNMAP);
    vm_flags_set(vma, VM_MIXEDMAP);
#endif
	vma->vm_private_data = buffer;
	vma->vm_pgoff = 0;

	unified_mmap_open(vma);

	return 0;
}

static int unified_map_km(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;
	struct scatterlist *sgl = sgt->sgl;
	unsigned int num_pages = sg_nents(sgl);
	struct page **pages;
	pgprot_t prot;
	int i;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	if (buffer->kptr) {
		pr_warn("%s called for already mapped buffer %d\n",
			__func__, buffer->id);
		return 0;
	}

	/*
	 * Use vmalloc to avoid limit with kmalloc
	 * where max possible allocation is 4MB,
	 * therefore the limit for the buffer that can be mapped
	 * 4194304 = number of 4k pages x sizeof(struct page *)
	 * number of 4k pages = 524288 which represents ~2.1GB.
	 * */
	pages = vmalloc(num_pages * sizeof(struct page *));
	if (!pages) {
		pr_err("%s failed to allocate memory for pages\n", __func__);
		return -ENOMEM;
	}

	prot = PAGE_KERNEL;
	/* CACHED by default */
	if (buffer_data->mattr & IMG_MEM_ATTR_WRITECOMBINE)
		prot = pgprot_writecombine(prot);
	else if (buffer_data->mattr & IMG_MEM_ATTR_UNCACHED)
		prot = pgprot_noncached(prot);

	/* Make dma mapping before mapping into kernel */
	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED))
		unified_dma_map(buffer);

	i = 0;
	while (sgl) {
		pages[i++] = sg_page(sgl);
		sgl = sg_next(sgl);
	}

	buffer->kptr = vmap(pages, num_pages, VM_MAP, prot);
	vfree(pages);
	if (!buffer->kptr) {
		pr_err("%s vmap failed!\n", __func__);
		return -EFAULT;
	}

	pr_debug("%s:%d buffer %d vmap to 0x%p\n", __func__, __LINE__,
		buffer->id, buffer->kptr);

	return 0;
}

static int unified_unmap_km(struct heap *heap, struct buffer *buffer)
{
	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	if (!buffer->kptr) {
		pr_warn("%s called for already unmapped buffer %d\n",
			__func__, buffer->id);
		return -EFAULT;
	}

	unified_dma_unmap(buffer);

	pr_debug("%s vunmap 0x%p\n", __func__, buffer->kptr);
	vunmap(buffer->kptr);
	buffer->kptr = NULL;

	return 0;
}

static int unified_get_sg_table(struct heap *heap, struct buffer *buffer,
				struct sg_table **sg_table, bool *use_sg_dma)
{
	struct buffer_data *buffer_data = buffer->priv;
	if (!buffer_data)
		return -EINVAL;

	*sg_table = buffer_data->sgt;
	*use_sg_dma = false;
	return 0;
}

static void unified_sync_cpu_to_dev(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED) &&
			buffer_data->dma_dir != DMA_NONE) {
		dma_sync_sg_for_device(buffer->device,
				sgt->sgl,
				sgt->orig_nents,
				DMA_TO_DEVICE);
		dma_sync_sg_for_cpu(buffer->device,
				sgt->sgl,
				sgt->orig_nents,
				DMA_FROM_DEVICE);
	}
}

static void unified_sync_dev_to_cpu(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED) &&
			buffer_data->dma_dir != DMA_NONE)
		dma_sync_sg_for_cpu(buffer->device,
				sgt->sgl,
				sgt->orig_nents,
				DMA_FROM_DEVICE);
}

static void unified_heap_destroy(struct heap *heap)
{
	pr_debug("%s:%d\n", __func__, __LINE__);
}

static struct heap_ops unified_heap_ops = {
	.export = unified_export,
	.alloc = unified_alloc,
	.import = NULL,
	.free = unified_free,
	.map_um = unified_map_um,
	.unmap_um = NULL, /* we are using vma ops to detect unmap event */
	.map_km = unified_map_km,
	.unmap_km = unified_unmap_km,
	.get_sg_table = unified_get_sg_table,
	.get_page_array = NULL,
	.sync_cpu_to_dev = unified_sync_cpu_to_dev,
	.sync_dev_to_cpu = unified_sync_dev_to_cpu,
	.set_offset = NULL,
	.destroy = unified_heap_destroy,
};

int img_mem_unified_init(const struct heap_config *heap_cfg, struct heap *heap)
{
	pr_debug("%s:%d\n", __func__, __LINE__);

	heap->ops = &unified_heap_ops;

	return 0;
}

/*
 * coding style for emacs
 *
 * Local variables:
 * indent-tabs-mode: t
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 */

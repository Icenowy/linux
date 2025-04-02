/*!
 *****************************************************************************
 *
 * @File       img_mem_carveout.c
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
#include <linux/dma-buf.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include <linux/genalloc.h>
#include <linux/version.h>

#include <asm/cacheflush.h>

#include <img_mem_man.h>
#include "img_mem_man_priv.h"

/* Default allocation order */
#define POOL_ALLOC_ORDER_BASE PAGE_SHIFT

struct heap_data {
	struct gen_pool *pool;
};

struct buffer_data {
	unsigned long addr; /* addr returned by genalloc */
	uint64_t *addrs; /* array of physical addresses, upcast to 64-bit */
	enum img_mem_attr mattr;  /* memory attributes */
	struct vm_area_struct *mapped_vma; /* Needed for cache manipulation */
	/* exporter via dmabuf */
	struct sg_table *sgt;
	bool exported;
	struct dma_buf *dma_buf;
	dma_addr_t dma_base;
	unsigned int dma_size;
};

static int trace_physical_pages;
static int trace_mmap_fault;

/*
 * dmabuf wrapper ops
 */
static struct sg_table *carveout_map_dmabuf(struct dma_buf_attachment *attach,
		enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;
	struct buffer_data *buffer_data;

	if (!buffer)
		return NULL;

	pr_debug("%s\n", __func__);

	buffer_data = buffer->priv;
	sg_dma_address(buffer_data->sgt->sgl) = buffer_data->dma_base;
	sg_dma_len(buffer_data->sgt->sgl) = buffer_data->dma_size;

	return buffer_data->sgt;
}

static void carveout_unmap_dmabuf(struct dma_buf_attachment *attach,
					struct sg_table *sgt,
					enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;
	struct buffer_data *buffer_data;

	if (!buffer)
		return;

	pr_debug("%s\n", __func__);

	buffer_data = buffer->priv;
	sg_dma_address(buffer_data->sgt->sgl) = (~(dma_addr_t)0);
	sg_dma_len(buffer_data->sgt->sgl) = 0;
}

/* Called when when ref counter reaches zero! */
static void carveout_release_dmabuf(struct dma_buf *buf)
{
	struct buffer *buffer = buf->priv;
	struct buffer_data *buffer_data;

	if (!buffer)
		return;

	buffer_data = buffer->priv;
	pr_debug("%s %p\n", __func__, buffer_data);
	if (!buffer_data)
		return;

	buffer_data->exported = false;
}

/* Called on file descriptor mmap */
static int carveout_mmap_dmabuf(struct dma_buf *buf, struct vm_area_struct *vma)
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
static void *carveout_kmap_dmabuf(struct dma_buf *buf, unsigned long page)
{
	pr_err("%s not supported\n", __func__);
	return NULL;
}
#endif

static int carveout_heap_map_km(struct heap *heap, struct buffer *buffer);
static int carveout_heap_unmap_km(struct heap *heap, struct buffer *buffer);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
static void *carveout_vmap_dmabuf(struct dma_buf *buf)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
static int carveout_vmap_dmabuf(struct dma_buf *buf, struct dma_buf_map *map)
#else
static int carveout_vmap_dmabuf(struct dma_buf *buf, struct iosys_map *map)
#endif
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
		return NULL;
#else
		return 0;
#endif

	heap = buffer->heap;

	if (carveout_heap_map_km(heap, buffer))
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0)
		return NULL;
#else
		return 0;
#endif

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
static void carveout_vunmap_dmabuf(struct dma_buf *buf, void *kptr)
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
		return;

	heap = buffer->heap;

	pr_debug("%s:%d buffer %d kptr 0x%p (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer->kptr, kptr);

	if (buffer->kptr == kptr)
		carveout_heap_unmap_km(heap, buffer);
}
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
static void carveout_vunmap_dmabuf(struct dma_buf *buf, struct dma_buf_map *map)
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
		return;

	heap = buffer->heap;

	pr_debug("%s:%d buffer %d kptr 0x%p (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer->kptr, map->vaddr);

	if (buffer->kptr == map->vaddr)
		carveout_heap_unmap_km(heap, buffer);
	dma_buf_map_clear(map);
}
#else
static void carveout_vunmap_dmabuf(struct dma_buf *buf, struct iosys_map *map)
{
	struct buffer *buffer = buf->priv;
	struct heap *heap;

	if (!buffer)
		return;

	heap = buffer->heap;

	pr_debug("%s:%d buffer %d kptr 0x%p (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer->kptr, map->vaddr);

	if (buffer->kptr == map->vaddr)
		carveout_heap_unmap_km(heap, buffer);
	iosys_map_clear(map);
}
#endif

static const struct dma_buf_ops carveout_dmabuf_ops = {
	.map_dma_buf = carveout_map_dmabuf,
	.unmap_dma_buf = carveout_unmap_dmabuf,
	.release = carveout_release_dmabuf,
	.mmap = carveout_mmap_dmabuf,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
	.kmap_atomic = carveout_kmap_dmabuf,
	.kmap = carveout_kmap_dmabuf,
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
	.map_atomic = carveout_kmap_dmabuf,
	.map = carveout_kmap_dmabuf,
#endif
#endif
	.vmap = carveout_vmap_dmabuf,
	.vunmap = carveout_vunmap_dmabuf,
};

static int carveout_heap_export(struct device *device, struct heap *heap,
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

	if (!buffer_data->sgt) {
		/* Create for the very first time */
		buffer_data->sgt = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
		if (!buffer_data->sgt) {
			pr_err("%s: failed to allocate sg_table\n", __func__);
			return -ENOMEM;
		}

		ret = sg_alloc_table(buffer_data->sgt, 1, GFP_KERNEL);
		if (ret) {
			pr_err("%s: sg_alloc_table failed\n", __func__);
			goto free_sgt_mem;
		}
		sg_set_page(buffer_data->sgt->sgl,
				pfn_to_page(PFN_DOWN(buffer_data->addr+heap->options.carveout.offs)),
				PAGE_ALIGN(size), 0);
		/* Store dma info */
		if (heap->to_dev_addr)
			buffer_data->dma_base = heap->to_dev_addr(&heap->options,
					buffer_data->addr+heap->options.carveout.offs);
		else
			buffer_data->dma_base = buffer_data->addr+heap->options.carveout.offs;

		buffer_data->dma_size = PAGE_ALIGN(size);
		/* No mapping yet */
		sg_dma_address(buffer_data->sgt->sgl) = (~(dma_addr_t)0);
		sg_dma_len(buffer_data->sgt->sgl) = 0;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)
	dma_buf = dma_buf_export(buffer_data, &carveout_dmabuf_ops,
			size, O_RDWR);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
	dma_buf = dma_buf_export(buffer_data, &carveout_dmabuf_ops,
			size, O_RDWR, NULL);
#else
	exp_info.ops = &carveout_dmabuf_ops;
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

free_sgt_mem:
	kfree(buffer_data->sgt);
	buffer_data->sgt = NULL;

	return ret;
}

static int carveout_heap_alloc(struct device *device, struct heap *heap,
						 size_t size, enum img_mem_attr attr,
						 struct buffer *buffer)
{
	struct heap_data *heap_data = heap->priv;
	struct buffer_data *buffer_data;
	phys_addr_t phys_addr;
	size_t pages, page;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	buffer_data = kzalloc(sizeof(struct buffer_data), GFP_KERNEL);
	if (!buffer_data)
		return -ENOMEM;

	pages = size / PAGE_SIZE;
	buffer_data->addrs = kmalloc_array(pages, sizeof(uint64_t), GFP_KERNEL);
	if (!buffer_data->addrs) {
		kfree(buffer_data);
		return -ENOMEM;
	}

	buffer_data->mattr = attr;

	buffer_data->addr = gen_pool_alloc(heap_data->pool, size);
	if (!buffer_data->addr) {
		pr_err("%s gen_pool_alloc failed!\n", __func__);
		kfree(buffer_data->addrs);
		kfree(buffer_data);
		return -ENOMEM;
	}

	/* The below assigns buffer_data->addr-> 1:1 mapping */
	phys_addr = gen_pool_virt_to_phys(heap_data->pool,
			buffer_data->addr + heap->options.carveout.offs);

	page = 0;
	while (page < pages) {
		if (trace_physical_pages)
			pr_info("%s phys %llx\n",
				__func__, (unsigned long long)phys_addr);
		buffer_data->addrs[page++] = phys_addr;
		phys_addr += PAGE_SIZE;
	};

	buffer->priv = buffer_data;

	pr_debug("%s buffer %d phys %#llx size %zu attrs %x\n", __func__,
		buffer->id,
		(unsigned long long)buffer_data->addrs[0],
		size,
		attr);
	return 0;
}

static void carveout_heap_free(struct heap *heap, struct buffer *buffer)
{
	struct heap_data *heap_data = heap->priv;
	struct buffer_data *buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	/* If forgot to unmap */
	if (heap->options.carveout.put_kptr && buffer->kptr) {
		heap->options.carveout.put_kptr(buffer->kptr);
		buffer->kptr = NULL;
	}

	if (buffer_data->dma_buf) {
		dma_buf_put(buffer_data->dma_buf);
		buffer_data->dma_buf->priv = NULL;
	}

	if (buffer_data->sgt) {
		sg_free_table(buffer_data->sgt);
		kfree(buffer_data->sgt);
		buffer_data->sgt = NULL;
	}

	if (buffer_data->mapped_vma)
		buffer_data->mapped_vma->vm_private_data = NULL;

	gen_pool_free(heap_data->pool, buffer_data->addr, buffer->actual_size);
	kfree(buffer_data->addrs);
	kfree(buffer_data);
}

static void _mmap_open(struct vm_area_struct *vma)
{
	struct buffer *buffer = vma->vm_private_data;
	struct buffer_data *buffer_data = buffer->priv;

	buffer_data->mapped_vma = vma;

	pr_debug("%s:%d buffer %d (0x%p) vma:%p\n",
			__func__, __LINE__, buffer->id, buffer, vma);
}

static void _mmap_close(struct vm_area_struct *vma)
{
	struct buffer *buffer = vma->vm_private_data;
	struct buffer_data *buffer_data;

	if (!buffer)
		return;

	buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p) vma:%p\n",
			__func__, __LINE__, buffer->id, buffer, vma);
	buffer_data->mapped_vma = NULL;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static vm_fault_t _mmap_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#else
static int _mmap_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#endif
	struct buffer *buffer = vma->vm_private_data;
	struct buffer_data *buffer_data = buffer->priv;
	phys_addr_t phys_addr;
	pgoff_t offset;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
	unsigned long addr = vmf->address;
#else
	unsigned long addr = (unsigned long)vmf->virtual_address;
#endif

	if (trace_mmap_fault) {
		pr_debug("%s:%d buffer %d (0x%p) vma:%p\n",
				__func__, __LINE__, buffer->id, buffer, vma);
		pr_debug("%s:%d vm_start %#lx vm_end %#lx total size %ld\n",
			__func__, __LINE__,
			vma->vm_start, vma->vm_end,
			vma->vm_end - vma->vm_start);
	}

	offset = (addr - vma->vm_start) >> PAGE_SHIFT;

	if (offset > (buffer->actual_size / PAGE_SIZE)) {
		pr_err("%s:%d offs:%ld\n",
			__func__, __LINE__, offset);
		return VM_FAULT_SIGBUS;
	}

	phys_addr = buffer_data->addrs[0] + (offset * PAGE_SIZE);

	if (trace_mmap_fault)
		pr_info("%s:%d vmf pgoff %#lx vmf addr %lx offs :%ld phys:%#llx\n",
			__func__, __LINE__, vmf->pgoff, addr, offset, phys_addr);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	return vmf_insert_pfn(vma, addr, phys_addr >> PAGE_SHIFT);
#else
	{
		int err = vm_insert_pfn(vma, addr, phys_addr >> PAGE_SHIFT);
		switch (err) {
		case 0:
		case -EAGAIN:
		case -ERESTARTSYS:
		case -EINTR:
		case -EBUSY:
			return VM_FAULT_NOPAGE;
		case -ENOMEM:
			return VM_FAULT_OOM;
		}

		return VM_FAULT_SIGBUS;
	}
#endif
}

/* vma ops->fault handler is used to track user space mappings
 * (inspired by other gpu/drm drivers from the kernel source tree)
 * to properly call cache handling ops when the mapping is destroyed
 * (when user calls unmap syscall).
 * vma flags are used to choose a correct direction.
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
static struct vm_operations_struct carveout_mmap_vm_ops = {
	.open = _mmap_open,
	.close = _mmap_close,
	.fault = _mmap_fault,
};

static int carveout_heap_map_um(struct heap *heap, struct buffer *buffer,
						 struct vm_area_struct *vma)
{
	struct buffer_data *buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);
	pr_debug("%s:%d vm_start %#lx vm_end %#lx size %ld\n",
		__func__, __LINE__,
		vma->vm_start, vma->vm_end, vma->vm_end - vma->vm_start);

	/* CACHED by default */
	if (buffer_data->mattr & IMG_MEM_ATTR_UNCACHED)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	else if (buffer_data->mattr & IMG_MEM_ATTR_WRITECOMBINE)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	vma->vm_ops = &carveout_mmap_vm_ops;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
    vma->vm_flags |= VM_PFNMAP;
#else
    vm_flags_set(vma,VM_PFNMAP);
#endif
	vma->vm_private_data = buffer;
	vma->vm_pgoff = 0;

	_mmap_open(vma);

	return 0;
}

static int carveout_heap_map_km(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;

	if (buffer->kptr) {
		pr_warn("%s called for already mapped buffer %d\n",
			__func__, buffer->id);
		return 0;
	}

	if (heap->options.carveout.get_kptr)
		buffer->kptr = heap->options.carveout.get_kptr(
				buffer_data->addrs[0],
				buffer->actual_size,
				buffer_data->mattr);
	else if (heap->options.carveout.kptr)
		buffer->kptr = heap->options.carveout.kptr +
				(buffer_data->addrs[0] - heap->options.carveout.phys);
	else
		return -ENOMEM;

	if (!buffer->kptr)
		return -ENOMEM;

	pr_debug("%s:%d buffer %d (0x%p) kptr 0x%p size:%zu\n", __func__, __LINE__,
		buffer->id, buffer, buffer->kptr, buffer->actual_size);

	return 0;
}

static int carveout_heap_unmap_km(struct heap *heap, struct buffer *buffer)
{
	pr_debug("%s:%d buffer %d (0x%p) kptr 0x%p\n", __func__, __LINE__,
		buffer->id, buffer, buffer->kptr);

	if (!buffer->kptr) {
		pr_warn("%s called for unmapped buffer %d\n",
			__func__, buffer->id);
		return 0;
	}

	if (heap->options.carveout.put_kptr)
		heap->options.carveout.put_kptr(buffer->kptr);

	buffer->kptr = NULL;

	return 0;
}

static int carveout_heap_get_page_array(struct heap *heap,
					struct buffer *buffer,
					uint64_t **addrs)
{
	struct buffer_data *buffer_data = buffer->priv;

	*addrs = buffer_data->addrs;
	return 0;
}

static int carveout_set_offset(struct heap *heap, size_t offs)
{
	if (heap->options.carveout.offs > heap->options.carveout.size) {
		pr_err("%s offset exceeds size!\n", __func__);
		return -EINVAL;
	}

	heap->options.carveout.offs = offs;

	return 0;
}

static void carveout_cache_update(struct vm_area_struct *vma)
{
	if (!vma)
		return;

	pr_debug("%s vma start:%lx end:%lx\n",
		__func__, vma->vm_start, vma->vm_end);

#if !defined(CONFIG_ARM) && !defined(CONFIG_ARM64)
	/* This function is not exported for modules by ARM kernel */
	flush_cache_range(vma, vma->vm_start, vma->vm_end);
#else
	/* Tentative for the SFF, this function is exported by the kernel... */
	/* vivt_flush_cache_range(vma, vma->vm_start, vma->vm_end); */
#endif
}

static void carveout_sync_cpu_to_dev(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;

	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED))
		carveout_cache_update(buffer_data->mapped_vma);
}

static void carveout_sync_dev_to_cpu(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;

	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED))
		carveout_cache_update(buffer_data->mapped_vma);
}

static void carveout_heap_destroy(struct heap *heap)
{
	struct heap_data *heap_data = heap->priv;

	pr_debug("%s:%d\n", __func__, __LINE__);

	gen_pool_destroy(heap_data->pool);
	kfree(heap_data);
}

static struct heap_ops carveout_heap_ops = {
	.export = carveout_heap_export,
	.alloc = carveout_heap_alloc,
	.import = NULL,
	.free = carveout_heap_free,
	.map_um = carveout_heap_map_um,
	.unmap_um = NULL,
	.map_km = carveout_heap_map_km,
	.unmap_km = carveout_heap_unmap_km,
	.get_sg_table = NULL,
	.get_page_array = carveout_heap_get_page_array,
	.sync_cpu_to_dev = carveout_sync_cpu_to_dev,
	.sync_dev_to_cpu = carveout_sync_dev_to_cpu,
	.set_offset = carveout_set_offset,
	.destroy = carveout_heap_destroy,
};

int img_mem_carveout_init(const struct heap_config *config, struct heap *heap)
{
	struct heap_data *heap_data;
	unsigned long virt_start;
	int ret;
	int pool_order = POOL_ALLOC_ORDER_BASE +
			heap->options.carveout.pool_order;

	if (heap->options.carveout.offs > heap->options.carveout.size) {
		pr_err("%s offset exceeds size!\n", __func__);
		return -EINVAL;
	}

	pr_debug("%s phys base:%#llx kptr %p (offs:%llx) size:%zu order:%d\n", __func__,
		 (unsigned long long)heap->options.carveout.phys,
		 heap->options.carveout.kptr,
		 (unsigned long long)heap->options.carveout.offs,
		 heap->options.carveout.size,
		 pool_order);

	if (config->options.carveout.kptr &&
			(heap->options.carveout.put_kptr || heap->options.carveout.get_kptr)) {
		pr_err("%s can't use static & dynamic kernel mapping at the same time!\n",
				__func__);
		return -EINVAL;
	}

	if (!config->options.carveout.kptr &&
			!(heap->options.carveout.put_kptr && heap->options.carveout.get_kptr)) {
		pr_warn("%s no kernel mapping method available!\n",
				__func__);
		return -EINVAL;
	}

	if (heap->options.carveout.phys & ((1<<pool_order)-1)) {
		pr_err("%s phys addr (%#llx) is not aligned to allocation order!\n",
				__func__, (unsigned long long)heap->options.carveout.phys);
		return -EINVAL;
	}

	if (heap->options.carveout.size == 0) {
		pr_err("%s size cannot be zero!\n", __func__);
		return -EINVAL;
	}

	heap_data = kmalloc(sizeof(struct heap_data), GFP_KERNEL);
	if (!heap_data)
		return -ENOMEM;

	heap_data->pool = gen_pool_create(pool_order, -1);
	if (!heap_data->pool) {
		pr_err("%s gen_pool_create failed\n", __func__);
		ret = -ENOMEM;
		goto pool_create_failed;
	}

	/* Operating in no offset mode -> virtual == phys
	 * However when physical address == 0 (unlikely) we need to distinguish
	 * if address returned from gen_pool_alloc is an error or valid address,
	 * so add a const offset.
	 */
	virt_start = (unsigned long)heap->options.carveout.phys;
	if (!virt_start)
		virt_start = 1<<pool_order;

	ret = gen_pool_add_virt(heap_data->pool, virt_start,
			heap->options.carveout.phys,
			heap->options.carveout.size,
			-1);
	if (ret) {
		pr_err("%s gen_pool_add_virt failed\n", __func__);
		goto pool_add_failed;
	}

	heap->ops = &carveout_heap_ops;
	heap->priv = heap_data;

	return 0;

pool_add_failed:
	gen_pool_destroy(heap_data->pool);
pool_create_failed:
	kfree(heap_data);
	return ret;
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

/*!
 *****************************************************************************
 *
 * @File       img_mem_anonymous.c
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
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/version.h>

#include <img_mem_man.h>
#include "img_mem_man_priv.h"

static int trace_physical_pages;

struct buffer_data {
	struct sg_table *sgt;
	enum img_mem_attr mattr;  /* memory attributes */
};

static int anonymous_heap_import(struct device *device, struct heap *heap,
						size_t size, enum img_mem_attr attr, uint64_t buf_fd,
						struct page **pages, struct buffer *buffer)
{
	struct buffer_data *data;
	struct sg_table *sgt;
	struct scatterlist *sgl;
	int num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	int ret;
	int i;

	pr_debug("%s:%d buffer %d (0x%p) for PID:%d\n",
			__func__, __LINE__, buffer->id, buffer,
			task_pid_nr(current));

	sgt = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sgt) 
		return -ENOMEM;

	ret = sg_alloc_table(sgt, num_pages, GFP_KERNEL);
	if (ret) {
		pr_err("%s failed to allocate sgt with num_pages\n", __func__);
		goto alloc_sgt_pages_failed;
	}

	data = kmalloc(sizeof(struct buffer_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto alloc_priv_failed;
	}

	for_each_sg(sgt->sgl, sgl, sgt->nents, i) {
		struct page *page = pages[i];
		sg_set_page(sgl, page, PAGE_SIZE, 0);

		if (trace_physical_pages)
			pr_info("%s:%d phys %#llx length %d\n",
				 __func__, __LINE__,
				 (unsigned long long)sg_phys(sgl), sgl->length);

		/* Sanity check if physical address is
		 * accessible from the device PoV */
		if (~dma_get_mask(device) & sg_phys(sgl)) {
			pr_err("%s physical address is out of dma_mask,"
					" and probably won't be accessible by the core!\n",
					__func__);
			ret = -ERANGE;
			goto dma_mask_check_failed;
		}
	}

	pr_debug("%s:%d buffer %d orig_nents %d\n", __func__, __LINE__,
		 buffer->id, sgt->orig_nents);

	data->sgt = sgt;
	data->mattr = attr;
	buffer->priv = data;

	ret = dma_map_sg(buffer->device, sgt->sgl, sgt->orig_nents,
				DMA_BIDIRECTIONAL);
	if (ret <= 0) {
		pr_err("%s dma_map_sg failed!\n", __func__);
		goto dma_mask_check_failed;
	}

	/* Increase ref count for each page used */
	for (i = 0; i < num_pages; i++)
		if (pages[i])
			get_page(pages[i]);

	return 0;

dma_mask_check_failed:
	kfree(data);
alloc_priv_failed:
	sg_free_table(sgt);
alloc_sgt_pages_failed:
	kfree(sgt);

	return ret;
}

static void anonymous_heap_free(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *data = buffer->priv;
	struct sg_table *sgt = data->sgt;
	struct scatterlist *sgl;
	bool dirty = false;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		 buffer->id, buffer);

	dma_unmap_sg(buffer->device, sgt->sgl, sgt->orig_nents,
			DMA_BIDIRECTIONAL);

	if (buffer->kptr) {
		pr_debug("%s vunmap 0x%p\n", __func__, buffer->kptr);
		dirty = true;
		vunmap(buffer->kptr);
		buffer->kptr = NULL;
	}

	sgl = sgt->sgl;
	while (sgl) {
		struct page *page = sg_page(sgl);
		if (page) {
			if (dirty)
				set_page_dirty(page);
			put_page(page);
		}
		sgl = sg_next(sgl);
	}

	sg_free_table(sgt);
	kfree(sgt);
	kfree(data);
}

static int anonymous_heap_map_km(struct heap *heap, struct buffer *buffer)
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

	pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
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

	i = 0;
	while (sgl) {
		pages[i++] = sg_page(sgl);
		sgl = sg_next(sgl);
	}

	buffer->kptr = vmap(pages, num_pages, VM_MAP, prot);
	kfree(pages);
	if (!buffer->kptr) {
		pr_err("%s vmap failed!\n", __func__);
		return -EFAULT;
	}

	pr_debug("%s:%d buffer %d vmap to 0x%p\n", __func__, __LINE__,
		 buffer->id, buffer->kptr);

	return 0;
}

static int anonymous_heap_unmap_km(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *data = buffer->priv;
	struct sg_table *sgt = data->sgt;
	struct scatterlist *sgl;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		 buffer->id, buffer);

	if (!buffer->kptr) {
		pr_warn("%s called for unmapped buffer %d\n",
			__func__, buffer->id);
		return 0;
	}

	pr_debug("%s:%d buffer %d kunmap from 0x%p\n", __func__, __LINE__,
		 buffer->id, buffer->kptr);
	vunmap(buffer->kptr);
	buffer->kptr = NULL;

	sgl = sgt->sgl;
	while (sgl) {
		struct page *page = sg_page(sgl);
		if (page) {
			set_page_dirty(page);
		}
		sgl = sg_next(sgl);
	}

	return 0;
}

static int anonymous_get_sg_table(struct heap *heap, struct buffer *buffer,
						 struct sg_table **sg_table, bool *use_sg_dma)
{
	struct buffer_data *data = buffer->priv;

	*sg_table = data->sgt;
	*use_sg_dma = false;
	return 0;
}

static void anonymous_sync_cpu_to_dev(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;
	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);
	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED)) {
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

static void anonymous_sync_dev_to_cpu(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;
	struct sg_table *sgt = buffer_data->sgt;
	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	if (!(buffer_data->mattr & IMG_MEM_ATTR_UNCACHED)) {
		dma_sync_sg_for_cpu(buffer->device,
				sgt->sgl,
				sgt->orig_nents,
				DMA_FROM_DEVICE);
	}
}

static void anonymous_heap_destroy(struct heap *heap)
{
	pr_debug("%s:%d\n", __func__, __LINE__);
}

static struct heap_ops anonymous_heap_ops = {
	.alloc = NULL,
	.import = anonymous_heap_import,
	.free = anonymous_heap_free,
	.map_um = NULL,
	.unmap_um = NULL,
	.map_km = anonymous_heap_map_km,
	.unmap_km = anonymous_heap_unmap_km,
	.get_sg_table = anonymous_get_sg_table,
	.get_page_array = NULL,
	.sync_cpu_to_dev = anonymous_sync_cpu_to_dev,
	.sync_dev_to_cpu = anonymous_sync_dev_to_cpu,
	.set_offset = NULL,
	.destroy = anonymous_heap_destroy,
};

int img_mem_anonymous_init(const struct heap_config *heap_cfg, struct heap *heap)
{
	pr_debug("%s:%d\n", __func__, __LINE__);

	heap->ops = &anonymous_heap_ops;

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

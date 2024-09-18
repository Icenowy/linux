/*!
 *****************************************************************************
 *
 * @File       img_mem_ion.c
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

#include <linux/version.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <ion.h>

#include <img_mem_man.h>
#include "img_mem_man_priv.h"

static int trace_physical_pages;

struct buffer_data {
	struct ion_client *client;
	struct ion_handle *handle;
	struct sg_table *sgt;
};

static int ion_heap_import(struct device *device, struct heap *heap,
				size_t size, enum img_mem_attr attr, uint64_t buf_fd,
				struct page **pages, struct buffer *buffer)
{
	struct buffer_data *data;
	int ret;
	int ion_buf_fd = (int)buf_fd;

	pr_debug("%s:%d buffer %d (0x%p) ion_buf_fd %d\n", __func__, __LINE__,
		 buffer->id, buffer, ion_buf_fd);

	data = kmalloc(sizeof(struct buffer_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->client = heap->priv;

	data->handle = ion_import_dma_buf(data->client, ion_buf_fd);
	if (IS_ERR_OR_NULL(data->handle)) {
		pr_err("%s ion_import_dma_buf fd %d\n", __func__, ion_buf_fd);
		ret = -EINVAL;
		goto ion_import_dma_buf_failed;
	}
	pr_debug("%s:%d buffer %d ion_handle %p\n", __func__, __LINE__,
		 buffer->id, data->handle);

	data->sgt = ion_sg_table(data->client, data->handle);
	if (IS_ERR(data->sgt)) {
		pr_err("%s ion_sg_table fd %d\n", __func__, ion_buf_fd);
		ret = -EINVAL;
		goto ion_sg_table_failed;
	}

	if (trace_physical_pages) {
		struct scatterlist *sgl = data->sgt->sgl;

		while (sgl) {
			pr_info("%s:%d phys %#llx length %d\n",
				 __func__, __LINE__,
				 (unsigned long long)sg_phys(sgl), sgl->length);
			sgl = sg_next(sgl);
		}
	}

	buffer->priv = data;
	return 0;

ion_sg_table_failed:
	ion_free(data->client, data->handle);
ion_import_dma_buf_failed:
	kfree(data);
	return ret;
}

static void ion_heap_free(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		 buffer->id, buffer);

	if (buffer->kptr)
		ion_unmap_kernel(data->client, data->handle);

	ion_free(data->client, data->handle);
	kfree(data);
}

static int ion_heap_map_um(struct heap *heap, struct buffer *buffer,
			   struct vm_area_struct *vma)
{
	struct buffer_data *data = buffer->priv;
	struct scatterlist *sgl;
	unsigned long addr;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		 buffer->id, buffer);
	pr_debug("%s:%d vm_start %#lx vm_end %#lx size %ld\n",
		 __func__, __LINE__,
		 vma->vm_start, vma->vm_end, vma->vm_end - vma->vm_start);

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	sgl = data->sgt->sgl;
	addr = vma->vm_start;
	while (sgl) {
		dma_addr_t phys = sg_phys(sgl); /* sg_dma_address ? */
		unsigned long pfn = phys >> PAGE_SHIFT;
		unsigned int len = sgl->length;
		int ret;

		ret = remap_pfn_range(vma, addr, pfn, len, vma->vm_page_prot);
		if (ret)
			return ret; 

		addr += len;
		sgl = sg_next(sgl);
	}

	return 0;
}

static int ion_heap_map_km(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		 buffer->id, buffer);

	if (buffer->kptr) {
		pr_warn("%s called for already mapped buffer %d\n",
			__func__, buffer->id);
		return 0;
	}

	buffer->kptr = ion_map_kernel(data->client, data->handle);
	if (!buffer->kptr) {
		pr_err("%s ion_map_kernel failed!\n", __func__);
		return -EFAULT;
	}

	pr_debug("%s:%d buffer %d map to 0x%p\n", __func__, __LINE__,
		 buffer->id, buffer->kptr);
	return 0;
}

static int ion_heap_unmap_km(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		 buffer->id, buffer);

	if (!buffer->kptr) {
		pr_warn("%s called for unmapped buffer %d\n",
			__func__, buffer->id);
		return 0;
	}

	ion_unmap_kernel(data->client, data->handle);

	pr_debug("%s:%d buffer %d unmap from 0x%p\n", __func__, __LINE__,
		 buffer->id, buffer->kptr);
	buffer->kptr = NULL;

	return 0;
}

static int ion_heap_get_sg_table(struct heap *heap, struct buffer *buffer,
				 struct sg_table **sg_table, bool *use_sg_dma)
{
	struct buffer_data *data = buffer->priv;

	*sg_table = data->sgt;
	*use_sg_dma = false;
	return 0;
}

static void ion_heap_destroy(struct heap *heap)
{
	pr_debug("%s:%d\n", __func__, __LINE__);
}

static struct heap_ops ion_heap_ops = {
	.alloc = NULL,
	.import = ion_heap_import,
	.free = ion_heap_free,
	.map_um = ion_heap_map_um,
	.unmap_um = NULL,
	.map_km = ion_heap_map_km,
	.unmap_km = ion_heap_unmap_km,
	.get_sg_table = ion_heap_get_sg_table,
	.get_page_array = NULL,
	.sync_cpu_to_dev = NULL, 
	.sync_dev_to_cpu = NULL, 
	.set_offset = NULL,
	.destroy = ion_heap_destroy,
};

int img_mem_ion_init(const struct heap_config *heap_cfg, struct heap *heap)
{
	pr_debug("%s:%d\n", __func__, __LINE__);

	if (!heap_cfg->options.ion.client) {
		pr_err("%s no ion client defined\n", __func__);
		return -EINVAL;
	}

	heap->ops = &ion_heap_ops;
	heap->priv = heap_cfg->options.ion.client;

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

/*!
 *****************************************************************************
 *
 * @File       img_mem_ocm.c
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
#include <linux/device.h>
#include <linux/slab.h>

#include <img_mem_man.h>
#include "img_mem_man_priv.h"

static int trace_physical_pages;

struct buffer_data {
	uint64_t *addrs; /* array of physical addresses, upcast to 64-bit */
	enum img_mem_attr mattr;  /* memory attributes */
};

static int ocm_heap_alloc(struct device *device, struct heap *heap,
			size_t size, enum img_mem_attr attr,
			struct buffer *buffer)
{	
	struct buffer_data *buffer_data;
	phys_addr_t phys_addr;
	size_t pages, page;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);
	if (size > heap->options.ocm.size) {
		pr_err("%s requested size bigger than ocm size !\n",
				__func__);
		return -EINVAL;
	}

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

	phys_addr = heap->options.ocm.phys;

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

static void ocm_heap_free(struct heap *heap, struct buffer *buffer)
{
	struct buffer_data *buffer_data = buffer->priv;

	pr_debug("%s:%d buffer %d (0x%p)\n", __func__, __LINE__,
		buffer->id, buffer);

	kfree(buffer_data->addrs);
	kfree(buffer_data);
}

static int ocm_heap_get_page_array(struct heap *heap,
				struct buffer *buffer,
				uint64_t **addrs)
{
	struct buffer_data *buffer_data = buffer->priv;

	*addrs = buffer_data->addrs;
	return 0;
}

static void ocm_heap_destroy(struct heap *heap)
{
	pr_debug("%s:%d\n", __func__, __LINE__);
}

static struct heap_ops ocm_heap_ops = {
	.alloc = ocm_heap_alloc,
	.import = NULL,
	.free = ocm_heap_free,
	.map_um = NULL,
	.unmap_um = NULL,
	.map_km = NULL,
	.unmap_km = NULL,
	.get_sg_table = NULL,
	.get_page_array = ocm_heap_get_page_array,
	.sync_cpu_to_dev = NULL,
	.sync_dev_to_cpu = NULL,
	.set_offset = NULL,
	.destroy = ocm_heap_destroy,
};

int img_mem_ocm_init(const struct heap_config *heap_cfg, struct heap *heap)
{
	pr_debug("%s phys:%#llx size:%zu attrs:%#x\n", __func__,
		 (unsigned long long)heap->options.ocm.phys,
		 heap->options.ocm.size, heap->options.ocm.hattr);

	heap->ops = &ocm_heap_ops;
	heap->priv = NULL;

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

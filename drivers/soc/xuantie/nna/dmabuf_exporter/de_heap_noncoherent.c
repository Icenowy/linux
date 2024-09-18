/*
 * de_heap_noncoherent.c
 */

#include <linux/version.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_X86
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
#include <asm/cacheflush.h>
#else
#include <asm/set_memory.h>
#endif
#endif /* CONFIG_X86 */

#include "de_heap.h"

#define MEMORY_ALLOCATION_FLAGS (GFP_DMA32 | __GFP_ZERO)

enum mem_cache_type {
	MEM_TYPE_CACHED        = 1,
	MEM_TYPE_UNCACHED      = 2,
	MEM_TYPE_WRITECOMBINE  = 3,
};

static unsigned int cache_type = MEM_TYPE_WRITECOMBINE;
module_param(cache_type, uint, 0444);
MODULE_PARM_DESC(cache_type,
		"Memory cache type: 1-cached, 2-uncached, 3-writecombine");

struct buffer {
	size_t size;
	void *vaddr;
	struct sg_table *sg_table;
	enum dma_data_direction dma_dir;
	struct device* client;
	int fd; /* Just for tracking */
};

/*
 * dmabuf ops
 */
static void de_noncoherent_kunmap(struct dma_buf *buf, unsigned long page,
						 void *vaddr);

static void de_noncoherent_release(struct dma_buf *buf)
{
	struct buffer *buffer = buf->priv;
	struct scatterlist *sgl;

	pr_info("%s fd:%d\n", __func__, buffer->fd);

	if (unlikely(buffer->vaddr))
		de_noncoherent_kunmap(buf, 0, buffer->vaddr);

	sgl = buffer->sg_table->sgl;
	while (sgl) {
		struct page *page = sg_page(sgl);

		if (page) {
#ifdef CONFIG_X86
			set_memory_wb((unsigned long)page_address(page), 1);
#endif
			__free_page(page);
		}
		sgl = sg_next(sgl);
	}
	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);

	kfree(buffer);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
static void *de_noncoherent_kmap_atomic(struct dma_buf *buf, unsigned long page)
{
	pr_debug("%s\n", __func__);

	return NULL;
}
#endif

static struct sg_table *de_noncoherent_map_dma(struct dma_buf_attachment *attach,
							enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;
	struct scatterlist *sgl = buffer->sg_table->sgl;

	pr_info("%s\n", __func__);

	if (buffer->client) {
		pr_err("%s client already attached!\n", __func__);
		return NULL;
	}

	/* We are only checking if buffer is mapable */
	while (sgl) {
		struct page *page  = sg_page(sgl);
		dma_addr_t dma_addr;

		pr_debug("%s:%d phys %#llx length %d\n",
			__func__, __LINE__,
			(unsigned long long)sg_phys(sgl), sgl->length);

		if(!page)
			WARN_ONCE(1, "Page does not exist!");

		dma_addr = dma_map_page(attach->dev, page, 0, PAGE_SIZE,
					DMA_BIDIRECTIONAL);
		if (dma_mapping_error(attach->dev, dma_addr)) {
				pr_err("%s dma_map_page failed!\n", __func__);
				return NULL;;
		}
		dma_unmap_page(attach->dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
#ifdef CONFIG_X86
		{
			if (cache_type == MEM_TYPE_CACHED)
				set_memory_wb((unsigned long)page_address(page), 1);
			else if (cache_type == MEM_TYPE_WRITECOMBINE)
				set_memory_wc((unsigned long)page_address(page), 1);
			else if (cache_type == MEM_TYPE_UNCACHED)
				set_memory_uc((unsigned long)page_address(page), 1);
		}
#endif
		sgl = sg_next(sgl);
	}
	buffer->client = attach->dev;

	return buffer->sg_table;
}

static void de_noncoherent_unmap_dma(struct dma_buf_attachment *attach,
					struct sg_table *sgt,
					enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;

	pr_info("%s\n", __func__);

	buffer->client = NULL;
}

static int de_noncoherent_begin_cpu_access(struct dma_buf *dmabuf,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
					size_t start, size_t len,
#endif
					enum dma_data_direction direction)
{
	struct buffer *buffer = dmabuf->priv;
	struct sg_table *sgt = buffer->sg_table;
	int ret;

	pr_info("%s\n", __func__);

	if (!buffer->client) {
		pr_err("%s client is NULL\n", __func__);
		return -EFAULT;
	}

	if (buffer->dma_dir == DMA_NONE) {
		ret = dma_map_sg(buffer->client, sgt->sgl, sgt->orig_nents,
				direction);
		if (ret <= 0) {
			pr_err("%s dma_map_sg failed!\n", __func__);
			return -EFAULT;
		}
		sgt->nents = ret;
		buffer->dma_dir = direction;
	}

	if (buffer->dma_dir == DMA_FROM_DEVICE)
		dma_sync_sg_for_cpu(buffer->client, sgt->sgl, sgt->orig_nents,
						DMA_FROM_DEVICE);

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
static void de_noncoherent_end_cpu_access(struct dma_buf *dmabuf,
					size_t start, size_t len,
					enum dma_data_direction direction)
#else
static int de_noncoherent_end_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction)
#endif
{
	struct buffer *buffer = dmabuf->priv;
	struct sg_table *sgt = buffer->sg_table;

	pr_info("%s\n", __func__);

	if (!buffer->client) {
		pr_err("%s client is NULL\n", __func__);
		goto exit;
	}

	if (buffer->dma_dir == DMA_NONE)
		goto exit;

	if (buffer->dma_dir == DMA_TO_DEVICE)
		dma_sync_sg_for_cpu(buffer->client, sgt->sgl, sgt->orig_nents,
					DMA_TO_DEVICE);

	dma_unmap_sg(buffer->client, sgt->sgl,
			sgt->orig_nents, buffer->dma_dir);

	buffer->dma_dir = DMA_NONE;
exit:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
	return 0;
#endif
	;
}

static int de_noncoherent_mmap(struct dma_buf *dmabuf,
					struct vm_area_struct *vma)
{
	struct buffer *buffer = dmabuf->priv;
	struct scatterlist *sgl = buffer->sg_table->sgl;
	unsigned long addr;

	pr_debug("%s\n", __func__);

	/* pgprot_t cached by default */
	if (cache_type == MEM_TYPE_WRITECOMBINE)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else if (cache_type == MEM_TYPE_UNCACHED)
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

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

static void *de_noncoherent_kmap(struct dma_buf *dma_buf, unsigned long page)
{
	struct buffer *buffer = dma_buf->priv;
	struct scatterlist *sgl = buffer->sg_table->sgl;
	unsigned int num_pages = sg_nents(sgl);
	struct page **pages;
	pgprot_t prot;
	int i;

	pr_debug("%s\n", __func__);

	/* NOTE: Ignoring pages param, we have the info info sgt */
	if (buffer->vaddr)
		return buffer->vaddr;

	pages = kmalloc_array(num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		pr_err("%s failed to allocate memory for pages\n", __func__);
		return NULL;
	}

	prot = PAGE_KERNEL;
	/* CACHED by default */
	if (cache_type == MEM_TYPE_WRITECOMBINE)
		prot = pgprot_writecombine(prot);
	else if (cache_type == MEM_TYPE_UNCACHED)
		prot = pgprot_noncached(prot);

	i = 0;
	while (sgl) {
		pages[i++] = sg_page(sgl);
		sgl = sg_next(sgl);
	}

	buffer->vaddr = vmap(pages, num_pages, VM_MAP, prot);
	kfree(pages);

	return buffer->vaddr;
}

static void de_noncoherent_kunmap(struct dma_buf *buf, unsigned long page,
						 void *vaddr)
{
	struct buffer *buffer = buf->priv;

	pr_debug("%s\n", __func__);

	if (buffer->vaddr != vaddr || !buffer->vaddr) {
		pr_warn("%s called with wrong address %p != %p\n",
				__func__, vaddr, buffer->vaddr);
		return;
	}

	vunmap(buffer->vaddr);
	buffer->vaddr = NULL;
}

static void *de_noncoherent_vmap(struct dma_buf *buf)
{
	return de_noncoherent_kmap(buf, 0);
}

static void de_noncoherent_vunmap(struct dma_buf *buf, void *kptr)
{
	de_noncoherent_kunmap(buf, 0, kptr);
}

static const struct dma_buf_ops dmabuf_ops = {
	.attach = NULL, /* optional */
	.detach = NULL, /* optional */
	.map_dma_buf = de_noncoherent_map_dma,
	.unmap_dma_buf = de_noncoherent_unmap_dma,
	.release = de_noncoherent_release,
	.begin_cpu_access = de_noncoherent_begin_cpu_access,
	.end_cpu_access = de_noncoherent_end_cpu_access,
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
	.kmap_atomic = de_noncoherent_kmap_atomic,
	.kunmap_atomic = NULL, /* optional */
	.kmap = de_noncoherent_kmap,
	.kunmap = de_noncoherent_kunmap, /* optional */
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
	.map_atomic = de_noncoherent_kmap_atomic,
	.unmap_atomic = NULL, /* optional */
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
	.map = de_noncoherent_kmap,
	.unmap = de_noncoherent_kunmap, /* optional */
#endif
#endif
	.mmap = de_noncoherent_mmap,
	.vmap = de_noncoherent_vmap,
	.vunmap = de_noncoherent_vunmap,
};

int de_heap_buffer_create(size_t size, unsigned long align, void **private_data)
{
	struct buffer *buffer;
	struct dma_buf *dma_buf;
	struct scatterlist *sgl;
	int ret;
	int pages;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
#endif

	pr_info("%s:noncoherent size %zu\n", __func__, size);

	buffer = kzalloc(sizeof(struct buffer), GFP_KERNEL);
	if (!buffer) {
		pr_err("%s:noncoherent failed to allocate buffer\n", __func__);
		return -ENOMEM;
	}
	buffer->size = size;

	buffer->sg_table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer->sg_table) {
		pr_err("%s:noncoherent failed to allocate sg_table\n", __func__);
		ret = -ENOMEM;
		goto sg_table_malloc_failed;
	}

	pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	ret = sg_alloc_table(buffer->sg_table, pages, GFP_KERNEL);
	if (ret) {
		pr_err("%s:noncoherent sg_alloc_table failed\n", __func__);
		goto sg_alloc_table_failed;
	}

	sgl = buffer->sg_table->sgl;
	while (sgl) {
		struct page *page;

		page = alloc_page(MEMORY_ALLOCATION_FLAGS);
		if (!page) {
			pr_err("%s alloc_page failed!\n", __func__);
			ret = -ENOMEM;
			goto alloc_page_failed;
		}

		sg_set_page(sgl, page, PAGE_SIZE, 0);
		sgl = sg_next(sgl);
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)
	dma_buf = dma_buf_export(buffer, &dmabuf_ops, size, O_RDWR);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4,1,0)
	dma_buf = dma_buf_export(buffer, &dmabuf_ops, size, O_RDWR, NULL);
#else
	exp_info.ops = &dmabuf_ops;
	exp_info.size = size;
	exp_info.flags = O_RDWR;
	exp_info.priv = buffer;
	exp_info.resv = NULL;
	dma_buf = dma_buf_export(&exp_info);
#endif
	if (IS_ERR(dma_buf)) {
		pr_err("%s:noncoherent dma_buf_export failed\n", __func__);
		ret = PTR_ERR(dma_buf);
		goto dma_buf_export_failed;
	}

	buffer->dma_dir = DMA_NONE;
	dma_buf->priv = buffer;
	*private_data = dma_buf;

	pr_info("%s:noncoherent size %zu\n",
		__func__, size);
	return 0;

alloc_page_failed:
	sgl = buffer->sg_table->sgl;
	while (sgl) {
		struct page *page = sg_page(sgl);

		if (page) {
#ifdef CONFIG_X86
			set_memory_wb((unsigned long)page_address(page), 1);
#endif
			__free_page(page);
		}
		sgl = sg_next(sgl);
	}
dma_buf_export_failed:
	sg_free_table(buffer->sg_table);
sg_alloc_table_failed:
	kfree(buffer->sg_table);
sg_table_malloc_failed:
	kfree(buffer);
	return ret;
}

int de_heap_export_fd(void *private_data, unsigned long flags)
{
	struct dma_buf *dma_buf = private_data;
	struct buffer *buffer = dma_buf->priv;
	int ret;

	pr_debug("%s:noncoherent %p\n", __func__, dma_buf);

	get_dma_buf(dma_buf);
	buffer->fd = ret = dma_buf_fd(dma_buf, flags);
	if (ret < 0) {
		pr_err("%s:noncoherent dma_buf_fd failed\n", __func__);
		dma_buf_put(dma_buf);
		return ret;
	}

	pr_info("%s:noncoherent export fd %d\n",
		__func__, ret);
	return ret;
}

void de_heap_buffer_free(void *private_data)
{
	struct dma_buf *dma_buf = private_data;
	struct buffer *buffer = dma_buf->priv;

	pr_info("%s:noncoherent fd:%d\n", __func__, buffer->fd);

	dma_buf_put(dma_buf);
}

int de_heap_heap_init(void)
{
	pr_info("%s:noncoherent cache_type:%d\n", __func__, cache_type);
	return 0;
}

void de_heap_heap_deinit(void)
{
	pr_info("%s:noncoherent\n", __func__);
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

/*
 * de_heap_coherent.c
 */

#include <linux/version.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#ifdef CONFIG_X86
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
#include <asm/cacheflush.h>
#else
#include <asm/set_memory.h>
#endif
#endif /* CONFIG_X86 */

#include "de_heap.h"

#define MEMORY_ALLOCATION_FLAGS (GFP_HIGHUSER | __GFP_ZERO)

struct buffer {
	size_t size;
	void *vaddr;
	struct sg_table *sg_table;
	dma_addr_t handle;
};

/*
 * dmabuf ops
 */

static void de_coherent_release(struct dma_buf *buf)
{
	struct buffer *buffer = buf->priv;

	pr_info("%s phys address 0x%llx\n",
		__func__, (unsigned long long int)buffer->handle);

	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);

#ifdef CONFIG_X86
	set_memory_wb((unsigned long)buffer->vaddr,
					(buffer->size + PAGE_SIZE - 1) / PAGE_SIZE);
#endif
	dma_free_coherent(NULL, buffer->size, buffer->vaddr, buffer->handle);

	kfree(buffer);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
static void *de_coherent_kmap_atomic(struct dma_buf *buf, unsigned long page)
{
	pr_debug("%s\n", __func__);

	return NULL;
}
#endif

static struct sg_table *de_coherent_map_dma(struct dma_buf_attachment *attach,
							enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;

	pr_debug("%s\n", __func__);

	return buffer->sg_table;
}

static void de_coherent_unmap_dma(struct dma_buf_attachment *attach,
					struct sg_table *sgt,
					enum dma_data_direction dir)
{
	pr_debug("%s\n", __func__);
}

static int de_coherent_mmap(struct dma_buf *dmabuf,
					struct vm_area_struct *vma)
{
	struct buffer *buffer = dmabuf->priv;
	unsigned long user_count, count, pfn, off;

	/*
	 * we could use dma_mmap_coherent() here, but it hard-codes
	 * an uncached behaviour and the kernel complains on x86 for
	 * a double mapping with different semantics (write-combine and
	 * uncached). Instead, we re-implement here the mapping.
	 * code copied from dma_common_mmap()
	 */

	pr_debug("%s\n", __func__);

	user_count = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	count = PAGE_ALIGN(buffer->size) >> PAGE_SHIFT;
	pfn = page_to_pfn(virt_to_page(buffer->vaddr));
	off = vma->vm_pgoff;

	if (off >= count || user_count > (count - off))
		return ENXIO;

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start, pfn + off,
						 user_count << PAGE_SHIFT,
						 vma->vm_page_prot);
}

static void *de_coherent_kmap(struct dma_buf *dma_buf, unsigned long page)
{
	struct buffer *buffer = dma_buf->priv;

	pr_debug("%s\n", __func__);

	/* kernel memory mapping has been done at allocation time */
	return buffer->vaddr;
}

static void de_coherent_kunmap(struct dma_buf *buf, unsigned long page,
						 void *vaddr)
{
	pr_debug("%s\n", __func__);
}

static void *de_coherent_vmap(struct dma_buf *buf)
{
	return de_coherent_kmap(buf, 0);
}

static void de_coherent_vunmap(struct dma_buf *buf, void *kptr)
{
	de_coherent_kunmap(buf, 0, kptr);
}

static const struct dma_buf_ops dmabuf_ops = {
	.attach = NULL, /* optional */
	.detach = NULL, /* optional */
	.map_dma_buf = de_coherent_map_dma,
	.unmap_dma_buf = de_coherent_unmap_dma,
	.release = de_coherent_release,
	.begin_cpu_access = NULL, /* optional */
	.end_cpu_access = NULL, /* optional */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
	.kmap_atomic = de_coherent_kmap_atomic,
	.kunmap_atomic = NULL, /* optional */
	.kmap = de_coherent_kmap,
	.kunmap = de_coherent_kunmap, /* optional */
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
	.map_atomic = de_coherent_kmap_atomic,
	.unmap_atomic = NULL, /* optional */
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
	.map = de_coherent_kmap,
	.unmap = de_coherent_kunmap, /* optional */
#endif
#endif
	.mmap = de_coherent_mmap,
	.vmap = de_coherent_vmap,
	.vunmap = de_coherent_vunmap,
};

int de_heap_buffer_create(size_t size, unsigned long align, void **private_data)
{
	struct buffer *buffer;
	struct dma_buf *dma_buf;
	int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
#endif

	pr_info("%s:coherent size %zu\n", __func__, size);

	buffer = kzalloc(sizeof(struct buffer), GFP_KERNEL);
	if (!buffer) {
		pr_err("%s:coherent failed to allocate buffer\n", __func__);
		return -ENOMEM;
	}
	buffer->size = size;

	buffer->vaddr = dma_alloc_coherent(NULL, size, &buffer->handle,
						 MEMORY_ALLOCATION_FLAGS);
	if (!buffer->vaddr) {
		pr_err("%s:coherent dma_alloc_coherent failed for size %zu\n",
					 __func__, size);
		ret = -ENOMEM;
		goto dma_alloc_coherent_failed;
	}
#ifdef CONFIG_X86
	set_memory_wc((unsigned long)buffer->vaddr,
					(buffer->size + PAGE_SIZE - 1) / PAGE_SIZE);
#endif

	buffer->sg_table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer->sg_table) {
		pr_err("%s:coherent failed to allocate sg_table\n", __func__);
		ret = -ENOMEM;
		goto sg_table_malloc_failed;
	}

	ret = sg_alloc_table(buffer->sg_table, 1, GFP_KERNEL);
	if (ret) {
		pr_err("%s:coherent sg_alloc_table failed\n", __func__);
		goto sg_alloc_table_failed;
	}
	sg_set_page(buffer->sg_table->sgl, virt_to_page(buffer->vaddr),
				PAGE_ALIGN(size), 0);

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
		pr_err("%s:coherent dma_buf_export failed\n", __func__);
		ret = PTR_ERR(dma_buf);
		goto dma_buf_export_failed;
	}

	dma_buf->priv = buffer;
	*private_data = dma_buf;

	pr_info("%s:coherent phys address 0x%llx virtual addr %p size %zu\n",
		__func__, (unsigned long long int)buffer->handle,
		buffer->vaddr, size);
	return 0;

dma_buf_export_failed:
	sg_free_table(buffer->sg_table);
sg_alloc_table_failed:
	kfree(buffer->sg_table);
sg_table_malloc_failed:
#ifdef CONFIG_X86
	set_memory_wb((unsigned long)buffer->vaddr,
					(buffer->size + PAGE_SIZE - 1) / PAGE_SIZE);
#endif
	dma_free_coherent(NULL, size, buffer->vaddr, buffer->handle);
dma_alloc_coherent_failed:
	kfree(buffer);
	return ret;
}

int de_heap_export_fd(void *private_data, unsigned long flags)
{
	struct dma_buf *dma_buf = private_data;
	struct buffer *buffer = dma_buf->priv;
	int ret;

	pr_debug("%s:coherent %p\n", __func__, dma_buf);

	get_dma_buf(dma_buf);
	ret = dma_buf_fd(dma_buf, flags);
	if (ret < 0) {
		pr_err("%s:coherent dma_buf_fd failed\n", __func__);
		dma_buf_put(dma_buf);
		return ret;
	}

	pr_info("%s:coherent phys address 0x%llx export fd %d\n",
		__func__, (unsigned long long int)buffer->handle, ret);
	return ret;
}

void de_heap_buffer_free(void *private_data)
{
	struct dma_buf *dma_buf = private_data;
	struct buffer *buffer = dma_buf->priv;

	pr_info("%s:coherent phys address 0x%llx\n",
		__func__, (unsigned long long int)buffer->handle);

	dma_buf_put(dma_buf);
}

int de_heap_heap_init(void)
{
	pr_info("%s:coherent\n", __func__);
	return 0;
}

void de_heap_heap_deinit(void)
{
	pr_info("%s:coherent\n", __func__);
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

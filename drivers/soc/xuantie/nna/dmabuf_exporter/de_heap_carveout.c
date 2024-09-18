/*
 * de_heap_carveout.c
 */

#include <linux/version.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/genalloc.h>
#include <linux/pci.h>
#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "de_heap.h"

/*
 * module parameters
 */

static unsigned int use_pci = 0;
module_param(use_pci, uint, 0444);
MODULE_PARM_DESC(use_pci, "use PCI bar memory (default: false)");

static unsigned int cpu_map = 1;
module_param(cpu_map, uint, 0444);
MODULE_PARM_DESC(cpu_map, "map memory to CPU (default: true)");

/* mandatory carveout parameters (use_pci = 0) */

static unsigned long carveout_base = 0;
module_param(carveout_base, ulong, 0444);
MODULE_PARM_DESC(carveout_base, "physical base address. "
		"mandatory when use_pci is false");

static unsigned long carveout_size = 0;
module_param(carveout_size, ulong, 0444);
MODULE_PARM_DESC(carveout_size, "physical size in bytes. "
		"mandatory when use_pci is false");

/* mandatory pci parameters (use_pci = 1) */

static unsigned int pci_vendor = 0;
module_param(pci_vendor, uint, 0444);
MODULE_PARM_DESC(pci_vendor, "PCI vendor id. mandatory when use_pci is true");

static unsigned int pci_product = 0;
module_param(pci_product, uint, 0444);
MODULE_PARM_DESC(pci_product, "PCI product id. mandatory when use_pci is true");

static int pci_bar = -1;
module_param(pci_bar, int, 0444);
MODULE_PARM_DESC(pci_bar, "PCI bar index. mandatory when use_pci is true");

/* optional pci parameters (use_pci = 1) */

static unsigned long pci_size = 0;
module_param(pci_size, ulong, 0444);
MODULE_PARM_DESC(pci_size, "physical size in bytes. "
		"used when use_pci is true. "
		"when 0 (the default), use all memory in the PCI bar");

static unsigned long pci_offset = 0;
module_param(pci_offset, ulong, 0444);
MODULE_PARM_DESC(pci_offset, "offset from PCI bar start. "
		"used when use_pci is true. optional (default: 0)");

static bool use_sg_dma = true;
module_param(use_sg_dma, bool, 0444);
MODULE_PARM_DESC(use_sg_dma,
		"Sets sg_dma_address/len info");

/*
 * internal values
 */
static phys_addr_t pool_base;

/* 12 bits (4096 bytes) */
#define POOL_ALLOC_ORDER 12

static struct gen_pool *heap_pool;

static struct pci_dev *pci_device;

struct buffer {
	phys_addr_t phys;
	size_t size;
	struct sg_table *sg_table;
	struct dma_buf *dma_buf;
	dma_addr_t dma_base;
	unsigned int dma_size;
};

/*
 * dmabuf ops
 */

static struct sg_table *de_carveout_map_dma(struct dma_buf_attachment *attach,
							enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;

	pr_debug("%s\n", __func__);

	if (use_sg_dma) {
		sg_dma_address(buffer->sg_table->sgl) = buffer->dma_base;
		sg_dma_len(buffer->sg_table->sgl) = buffer->dma_size;
	}

	return buffer->sg_table;
}

static void de_carveout_unmap_dma(struct dma_buf_attachment *attach,
					struct sg_table *sgt,
					enum dma_data_direction dir)
{
	struct buffer *buffer = attach->dmabuf->priv;

	pr_debug("%s\n", __func__);
	if (use_sg_dma) {
		sg_dma_address(buffer->sg_table->sgl) = (~(dma_addr_t)0);
		sg_dma_len(buffer->sg_table->sgl) = 0;
	}
}

static void de_carveout_release(struct dma_buf *buf)
{
	struct buffer *buffer = buf->priv;

	pr_info("%s phys address 0x%llx size %zu\n",
		__func__, (unsigned long long)buffer->phys, buffer->size);

	sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
	gen_pool_free(heap_pool, buffer->phys, buffer->size);
	kfree(buffer);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
static void *de_carveout_kmap_atomic(struct dma_buf *buf, unsigned long page)
{
	pr_err("%s not supported\n", __func__);
	return NULL;
}
#endif

static int de_carveout_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct buffer *buffer = dmabuf->priv;

	pr_debug("%s\n", __func__);

	if (!cpu_map) {
		pr_err("%s not allowed (cpu_map is false)\n", __func__);
		return -EIO;
	}

	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start,
						 page_to_pfn(sg_page(buffer->sg_table->sgl)),
						 buffer->sg_table->sgl->length,
						 vma->vm_page_prot);
}

static void *de_carveout_kmap(struct dma_buf *dma_buf, unsigned long page)
{
	struct buffer *buffer = dma_buf->priv;
	void *ptr;

	if (!cpu_map) {
		pr_err("%s not allowed (cpu_map is false)\n", __func__);
		return NULL;
	}

	ptr = (void __force *)ioremap(buffer->phys, buffer->size);
	if (!ptr) {
		pr_err("%s:carveout ioremap failed\n", __func__);
		return NULL;
	}

	return ptr;
}

static void de_carveout_kunmap(struct dma_buf *buf, unsigned long page,
						 void *vaddr)
{
	pr_debug("%s\n", __func__);

	if (vaddr)
		iounmap((void __iomem __force *)vaddr);
}

static void *de_carveout_vmap(struct dma_buf *buf)
{
	return de_carveout_kmap(buf, 0);
}

static void de_carveout_vunmap(struct dma_buf *buf, void *kptr)
{
	de_carveout_kunmap(buf, 0, kptr);
}

static const struct dma_buf_ops dmabuf_ops = {
	.attach = NULL, /* optional */
	.detach = NULL, /* optional */
	.map_dma_buf = de_carveout_map_dma,
	.unmap_dma_buf = de_carveout_unmap_dma,
	.release = de_carveout_release,
	.begin_cpu_access = NULL, /* optional */
	.end_cpu_access = NULL, /* optional */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
	.kmap_atomic = de_carveout_kmap_atomic,
	.kunmap_atomic = NULL, /* optional */
	.kmap = de_carveout_kmap,
	.kunmap = de_carveout_kunmap, /* optional */
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,19,0)
	.map_atomic = de_carveout_kmap_atomic,
	.unmap_atomic = NULL, /* optional */
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
	.map = de_carveout_kmap,
	.unmap = de_carveout_kunmap, /* optional */
#endif
#endif
	.mmap = de_carveout_mmap,
	.vmap = de_carveout_vmap,
	.vunmap = de_carveout_vunmap,
};

int de_heap_buffer_create(size_t size, unsigned long align, void **private_data)
{
	struct buffer *buffer;
	struct dma_buf *dma_buf;
	int ret;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,1,0)
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
#endif

	pr_info("%s:carveout size %zu\n", __func__, size);

	buffer = kzalloc(sizeof(struct buffer), GFP_KERNEL);
	if (!buffer) {
		pr_err("%s:carveout failed to allocate buffer\n", __func__);
		return -ENOMEM;
	}

	buffer->phys = gen_pool_alloc(heap_pool, size);
	if (!buffer->phys) {
		pr_err("%s:carveout gen_pool_alloc failed for size %zu\n",
					 __func__, size);
		ret = -ENOMEM;
		goto free_buffer;
	}
	buffer->size = size;

	buffer->sg_table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer->sg_table) {
		pr_err("%s:carveout failed to allocate sg_table\n", __func__);
		ret = -ENOMEM;
		goto free_alloc;
	}

	ret = sg_alloc_table(buffer->sg_table, 1, GFP_KERNEL);
	if (ret) {
		pr_err("%s:carveout sg_alloc_table failed\n", __func__);
		goto free_sg_table_mem;
	}
	sg_set_page(buffer->sg_table->sgl, pfn_to_page(PFN_DOWN(buffer->phys)),
				PAGE_ALIGN(buffer->size), 0);

	/* Store dma info */
	buffer->dma_base = buffer->phys;
	if (use_pci) {
		buffer->dma_base -= pool_base;
		buffer->dma_base += pci_offset;
	}
	buffer->dma_size = PAGE_ALIGN(size);

	if (use_sg_dma) {
		/* No mapping yet */
		sg_dma_address(buffer->sg_table->sgl) = (~(dma_addr_t)0);
		sg_dma_len(buffer->sg_table->sgl) = 0;
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
		pr_err("%s:carveout dma_buf_export failed\n", __func__);
		ret = PTR_ERR(dma_buf);
		goto free_sg_table;
	}
	buffer->dma_buf = dma_buf;

	*private_data = buffer;

	pr_info("%s:carveout phys address 0x%llx size %zu\n",
		__func__, (unsigned long long)buffer->phys, buffer->size);
	return 0;

free_sg_table:
	sg_free_table(buffer->sg_table);
free_sg_table_mem:
	kfree(buffer->sg_table);
free_alloc:
	gen_pool_free(heap_pool, buffer->phys, buffer->size);
free_buffer:
	kfree(buffer);
	return ret;
}

void de_heap_buffer_free(void *private_data)
{
	struct buffer *buffer = private_data;

	pr_info("%s:carveout phys address 0x%llx size %zu\n",
		__func__, (unsigned long long)buffer->phys, buffer->size);

	dma_buf_put(buffer->dma_buf);
}

int de_heap_export_fd(void *private_data, unsigned long flags)
{
	struct buffer *buffer = private_data;
	struct dma_buf *dma_buf = buffer->dma_buf;
	int ret;

	pr_debug("%s:carveout %p\n", __func__, buffer);

	get_dma_buf(dma_buf);
	ret = dma_buf_fd(dma_buf, flags);
	if (ret < 0) {
		pr_err("%s:carveout dma_buf_fd failed\n", __func__);
		dma_buf_put(dma_buf);
		return ret;
	}

	pr_info("%s:carveout phys address 0x%llx export fd %d\n",
		__func__, (unsigned long long)buffer->phys, ret);
	return ret;
}

int de_heap_heap_init(void)
{
	size_t pool_size;
	int ret;

	pr_debug("%s:carveout\n", __func__);

	if (use_pci) {
		unsigned long bar_base, bar_len;

		if (pci_vendor == 0 || pci_product == 0 || pci_bar < 0) {
			pr_err("%s:carveout missing pci parameters\n",
						 __func__);
			return -EFAULT;
		}

		pci_device = pci_get_device(pci_vendor, pci_product, NULL);
		if (pci_device == NULL) {
			pr_err("%s:carveout PCI device not found\n", __func__);
			return -EFAULT;
		}

		bar_base = pci_resource_start(pci_device, pci_bar);
		if (bar_base == 0) {
			pr_err("%s:carveout PCI bar %d not found\n",
						 __func__, pci_bar);
			ret = -EFAULT;
			goto free_pci_device;
		}

		bar_len = pci_resource_len(pci_device, pci_bar);
		if (bar_len == 0) {
			pr_err("%s:carveout PCI bar %d has zero length\n",
						 __func__, pci_bar);
			ret =  -EFAULT;
			goto free_pci_device;
		}
		pr_info("%s:carveout PCI bar %d start %#lx length %ld\n",
			__func__, pci_bar, bar_base, bar_len);

		if (pci_size == 0)
			pci_size = bar_len;

		if (pci_offset + pci_size > bar_len) {
			pr_err("%s:carveout pci_offset and size exceeds bar\n",
						 __func__);
			ret =  -EFAULT;
			goto free_pci_device;
		}

		pool_base = bar_base + pci_offset;
		pool_size = pci_size;
	} else {
		pci_device = NULL;

		if (carveout_base == 0) {
			pr_err("%s:carveout carveout_base not defined\n",
						 __func__);
			return -EFAULT;
		}
		if (carveout_size == 0) {
			pr_err("%s:carveout carveout_size not defined\n",
						 __func__);
			return -EFAULT;
		}

		pool_base = carveout_base;
		pool_size = carveout_size;
	}

	heap_pool = gen_pool_create(POOL_ALLOC_ORDER, -1);
	if (!heap_pool) {
		pr_err("%s:carveout gen_pool_create failed\n", __func__);
		ret = -ENOMEM;
		goto free_pci_device;
	}

	ret = gen_pool_add(heap_pool, (unsigned long)pool_base, pool_size, -1);
	if (ret) {
		pr_err("%s:carveout gen_pool_add failed\n", __func__);
		goto free_pool;
	}

	pr_info("%s:carveout base %#llx size %zu\n", __func__,
		(unsigned long long)pool_base, pool_size);
	return 0;

free_pool:
	gen_pool_destroy(heap_pool);
free_pci_device:
	if (pci_device)
		pci_dev_put(pci_device);
	return ret;
}

void de_heap_heap_deinit(void)
{
	pr_info("%s:carveout\n", __func__);

	gen_pool_destroy(heap_pool);

	if (pci_device)
		pci_dev_put(pci_device);
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

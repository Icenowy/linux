/*
 * de_heap_ion.c
 */

#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#include "de_heap.h"
#include "de_heap_ion.h"

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)))
static struct ion_client *dmabuf_ion_client;
int de_heap_buffer_create(size_t size, unsigned long align, void **private_data)
{
	struct ion_handle *ion_handle;
	unsigned int heap_mask;
	unsigned int heap_flags;

	pr_info("%s:check %zu\n", __func__, size);
	pr_debug("%s:ion size %zu\n", __func__, size);

	heap_mask = de_heap_ion_get_heap_mask();
	pr_info("%s: heap mask = %x\n", __func__, heap_mask);
	heap_flags = de_heap_ion_get_heap_flags();
	pr_info("%s: heap flags = %x\n", __func__, heap_flags);
	pr_info("%s:ion_alloc dmabuf_ion_client = %lx, size = %i, , align = %i \n", __func__, (long unsigned int)dmabuf_ion_client, (int)size, (int)align);
	ion_handle = ion_alloc(dmabuf_ion_client, size, align,
			       heap_mask, heap_flags);
	if (IS_ERR_OR_NULL(ion_handle)) {
		pr_err("%s:ion ion_alloc failed, ion_handle = %li\n", __func__, PTR_ERR(ion_handle));
		if (IS_ERR(ion_handle)) {
			return PTR_ERR(ion_handle);
		} else {
			return -ENOMEM;
		}
	} else {
		pr_info("%s:ion handle %p size %zu\n", __func__, ion_handle, size);
	}

	*private_data = ion_handle;

	return 0;
}

int de_heap_export_fd(void *private_data, unsigned long flags)
{
	struct ion_handle *ion_handle = private_data;
	int ret;

	pr_debug("%s:ion\n", __func__);

	ret = ion_share_dma_buf_fd(dmabuf_ion_client, ion_handle);
	if (ret < 0) {
		pr_err("%s:ion ion_share_dma_buf_fd failed\n", __func__);
		return ret;
	}

	pr_info("%s:ion handle %p export fd %d\n", __func__, ion_handle, ret);
	return ret;
}

void de_heap_buffer_free(void *private_data)
{
	struct ion_handle *ion_handle = private_data;

	pr_info("%s:ion handle %p\n", __func__, ion_handle);

	ion_free(dmabuf_ion_client, ion_handle);
}

int de_heap_heap_init(void)
{
	pr_info("%s:ion\n", __func__);

	dmabuf_ion_client = de_heap_ion_create_ion_client();
	pr_err("%s:dmabuf_ion_client = %li \n", __func__, (long)dmabuf_ion_client);
	if (!dmabuf_ion_client) {
		pr_err("%s:ion failed to get an ion client %lx \n", __func__, (long)dmabuf_ion_client);
		return -EFAULT;
	}

	return 0;
}

void de_heap_heap_deinit(void)
{
	pr_info("%s:ion\n", __func__);

	de_heap_ion_destroy_ion_client(dmabuf_ion_client);
}


#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)))

int de_heap_heap_init(void)
{
	pr_info("%s:ion\n", __func__);

	return 0;
}

void de_heap_heap_deinit(void)
{
	pr_info("%s:ion\n", __func__);
}

int de_heap_export_fd(void *private_data, unsigned long flags)
{
	pr_info("%s:\n", __func__);
	return 0;
}
#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(5, 5, 0)))

static struct ion_client *dmabuf_ion_client;
int de_heap_buffer_create(size_t size, unsigned long align, void **private_data)
{
	struct ion_handle *ion_handle;
	unsigned int heap_mask;
	unsigned int heap_flags;

	pr_info("%s:check %zu\n", __func__, size);
	pr_debug("%s:ion size %zu\n", __func__, size);

	heap_mask = de_heap_ion_get_heap_mask();
	pr_info("%s: heap mask = %x\n", __func__, heap_mask);
	heap_flags = de_heap_ion_get_heap_flags();
	pr_info("%s: heap flags = %x\n", __func__, heap_flags);
	pr_info("%s:ion_alloc dmabuf_ion_client = %lx, size = %i, , align = %i \n", __func__, (long unsigned int)dmabuf_ion_client, (int)size, (int)align);
	ion_handle = ion_alloc(dmabuf_ion_client, size, align,
			       heap_mask, heap_flags);
	if (IS_ERR_OR_NULL(ion_handle)) {
		pr_err("%s:ion ion_alloc failed, ion_handle = %li\n", __func__, PTR_ERR(ion_handle));
		if (IS_ERR(ion_handle)) {
			return PTR_ERR(ion_handle);
		} else {
			return -ENOMEM;
		}
	} else {
		pr_info("%s:ion handle %p size %zu\n", __func__, ion_handle, size);
	}

	*private_data = ion_handle;

	return 0;
}

int de_heap_export_fd(void *private_data, unsigned long flags)
{
	struct ion_handle *ion_handle = private_data;
	int ret;

	pr_debug("%s:ion\n", __func__);

	ret = ion_share_dma_buf_fd(dmabuf_ion_client, ion_handle);
	if (ret < 0) {
		pr_err("%s:ion ion_share_dma_buf_fd failed\n", __func__);
		return ret;
	}

	pr_info("%s:ion handle %p export fd %d\n", __func__, ion_handle, ret);
	return ret;
}

void de_heap_buffer_free(void *private_data)
{
	struct ion_handle *ion_handle = private_data;

	pr_info("%s:ion handle %p\n", __func__, ion_handle);

	ion_free(dmabuf_ion_client, ion_handle);
}

int de_heap_heap_init(void)
{
	pr_info("%s:ion\n", __func__);

	dmabuf_ion_client = de_heap_ion_create_ion_client();
	pr_err("%s:dmabuf_ion_client = %li \n", __func__, (long)dmabuf_ion_client);
	if (!dmabuf_ion_client) {
		pr_err("%s:ion failed to get an ion client %lx \n", __func__, (long)dmabuf_ion_client);
		return -EFAULT;
	}

	return 0;
}

void de_heap_heap_deinit(void)
{
	pr_info("%s:ion\n", __func__);

	de_heap_ion_destroy_ion_client(dmabuf_ion_client);
}


#else
#error "kernel not supported"
#endif

/*
 * coding style for emacs
 *
 * Local variables:
 * indent-tabs-mode: t
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 */

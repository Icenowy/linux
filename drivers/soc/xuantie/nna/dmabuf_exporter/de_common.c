/*
 * de_common.h
 */

#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/miscdevice.h>

#include "uapi/dmabuf_exporter.h"

#include "de_heap.h"

/*
 * Because this file is used in all modules, the kernel build system does
 * not define KBUILD_MODNAME. This causes a build failure in kernels where
 * dynamic debug is enabled, in all instances of pr_debug().
 *
 * dynamic debug messages for this file will use this name
 */
#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "dmabuf_exporter"
#endif

static struct miscdevice dmabuf_miscdevice;

static int de_file_open(struct inode *inode, struct file *file)
{
	pr_debug("%s\n", __func__);

	file->private_data = NULL;

	return 0;
}

static int dmabuf_ioctl_create(struct file *file, unsigned long arg_size)
{
	void *private_data;
	size_t size;
	int ret;

	pr_debug("%s: private_data %p\n", __func__, file->private_data);

	size = (arg_size + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
	pr_debug("%s: requested size %lu PAGE_SIZE %lu actual size %zu\n",
		 __func__, arg_size, PAGE_SIZE, size);

	if (file->private_data) {
		pr_err("%s: buffer already created!\n", __func__);
		return -EBUSY;
	}

	ret = de_heap_buffer_create(size, PAGE_SIZE, &private_data);
	if (ret)
		return ret;

	file->private_data = private_data;
	return 0;
}

static int dmabuf_ioctl_export(struct file *file, unsigned long flags)
{
	pr_debug("%s: private_data %p\n", __func__, file->private_data);

	if (file->private_data)
		return de_heap_export_fd(file->private_data, flags);

	pr_err("%s: buffer has not been created!\n", __func__);
	return -ENODEV;
}

static int de_file_release(struct inode *inode, struct file *file)
{
	pr_debug("%s: private_data %p\n", __func__, file->private_data);

	if (file->private_data) {
		de_heap_buffer_free(file->private_data);
		file->private_data = NULL;
	}

	return 0;
}

static long de_file_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	pr_debug("%s: cmd %x arg %lx\n", __func__, cmd, arg);

	switch (cmd) {
	case DMABUF_IOCTL_CREATE:
#ifdef CONFIG_COMPAT
	case COMPAT_DMABUF_IOCTL_CREATE:
#endif
		return dmabuf_ioctl_create(file, arg);

	case DMABUF_IOCTL_EXPORT:
#ifdef CONFIG_COMPAT
	case COMPAT_DMABUF_IOCTL_EXPORT:
#endif
		return dmabuf_ioctl_export(file, arg);

	default:
		pr_err("%s: unknown cmd %x\n", __func__, cmd);
		return -ENOTTY;
	}
}

static const struct file_operations dmabuf_fops = {
	.owner = THIS_MODULE,
	.open = de_file_open,
	.release = de_file_release,
	.unlocked_ioctl = de_file_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = de_file_ioctl,
#endif
};

static int __init dmabuf_device_init(void)
{
	int ret;

	pr_info("%s\n", __func__);

	dmabuf_miscdevice.minor = 128;
	dmabuf_miscdevice.name = "dmabuf";
	dmabuf_miscdevice.fops = &dmabuf_fops;
	dmabuf_miscdevice.parent = NULL;
	dmabuf_miscdevice.mode = 0666;

	ret = misc_register(&dmabuf_miscdevice);
	if (ret < 0) {
		pr_err("%s: failed to register misc device %s\n",
		       __func__, dmabuf_miscdevice.name);
		return ret;
	}
	pr_info("%s: registered misc device %s\n",
		__func__, dmabuf_miscdevice.name);

	ret = de_heap_heap_init();
	if (ret < 0) {
		misc_deregister(&dmabuf_miscdevice);
		return ret;
	}

	return 0;
}

static void __exit dmabuf_device_deinit(void)
{
	pr_info("%s\n", __func__);

	de_heap_heap_deinit();

	misc_deregister(&dmabuf_miscdevice);
}

module_init(dmabuf_device_init);
module_exit(dmabuf_device_deinit);

MODULE_AUTHOR("GPL");
MODULE_DESCRIPTION("DMA-BUF test driver");
MODULE_LICENSE("GPL v2");

/*
 * coding style for emacs
 *
 * Local variables:
 * indent-tabs-mode: t
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 */

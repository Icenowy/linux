/*
 * de_heap_ion_example.c
 */

#include "de_heap_ion.h"
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/version.h>


#undef linux
#include IMG_KERNEL_ION_HEADER
#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)))
#include IMG_KERNEL_ION_PRIV_HEADER
#endif
#define linux 1

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)))
static struct ion_device *idev;
static struct ion_client *client=NULL;
static struct ion_heap **heaps;
#endif

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)))
static void *carveout_ptr;
static void *chunk_ptr;

static struct ion_platform_heap dummy_heaps[] = {
		{
			.id	= ION_HEAP_TYPE_SYSTEM,
			.type	= ION_HEAP_TYPE_SYSTEM,
			.name	= "system",
		},
		{
			.id	= ION_HEAP_TYPE_SYSTEM_CONTIG,
			.type	= ION_HEAP_TYPE_SYSTEM_CONTIG,
			.name	= "system contig",
		},
		{
			.id	= ION_HEAP_TYPE_CARVEOUT,
			.type	= ION_HEAP_TYPE_CARVEOUT,
			.name	= "carveout",
			.size	= SZ_4M,
		},
		{
			.id	= ION_HEAP_TYPE_CHUNK,
			.type	= ION_HEAP_TYPE_CHUNK,
			.name	= "chunk",
			.size	= SZ_4M,
			.align	= SZ_16K,
			.priv	= (void *)(SZ_16K),
		},
};

static struct ion_platform_data dummy_ion_pdata = {
	.nr = ARRAY_SIZE(dummy_heaps),
	.heaps = dummy_heaps,
};
#endif

unsigned int de_heap_ion_get_heap_mask(void)
{
	return 0x42;
}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)))
unsigned int de_heap_ion_get_heap_id_mask(void)
{
	return 1<<ION_HEAP_TYPE_SYSTEM;
}
#endif

unsigned int de_heap_ion_get_heap_flags(void)
{
	return 0;
}

#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)))
struct ion_client *de_heap_ion_create_ion_client(void)
{
	int i, err;
	pr_info("%s:\n", __func__);
	/*
	 * usually involves fetching an ion_device from the system
	 * and calling ion_client_create()
	 */
	pr_info("%s:check ion_device_create \n", __func__);
	idev = ion_device_create(NULL);
	if (IS_ERR(idev)) {
		pr_err("%s:ion ion_device_create failed %li \n", __func__, PTR_ERR(idev));
		return (struct ion_client *)idev;
	}

	pr_info("%s:check kcalloc \n", __func__);
	heaps = kcalloc(dummy_ion_pdata.nr, sizeof(struct ion_heap *),
			GFP_KERNEL);
	pr_info("%s:heaps = %lx \n", __func__, (long unsigned int)heaps);
	if (!heaps) {
		pr_err("%s:ion kcalloc heaps = %lx \n", __func__, (long unsigned int)heaps);
		ion_device_destroy(idev);
		return ERR_PTR(-ENOMEM);
	}

	/* Allocate a dummy carveout heap */
	carveout_ptr = alloc_pages_exact(dummy_heaps[ION_HEAP_TYPE_CARVEOUT].size,
				GFP_KERNEL);
	if (carveout_ptr) {
		dummy_heaps[ION_HEAP_TYPE_CARVEOUT].base =	virt_to_phys(carveout_ptr);
	}	else {
		pr_err("ion_dummy: Could not allocate carveout\n");
	}

	/* Allocate a dummy chunk heap */
	chunk_ptr = alloc_pages_exact(dummy_heaps[ION_HEAP_TYPE_CHUNK].size,
				GFP_KERNEL);
	if (chunk_ptr) {
		dummy_heaps[ION_HEAP_TYPE_CHUNK].base = virt_to_phys(chunk_ptr);
	} else {
		pr_err("ion_dummy: Could not allocate chunk\n");
	}

	for (i = 0; i < dummy_ion_pdata.nr; i++) {
		struct ion_platform_heap *heap_data = &dummy_ion_pdata.heaps[i];

		if (heap_data->type == ION_HEAP_TYPE_CARVEOUT && !heap_data->base) {
			pr_info("ion_dummy: ION_HEAP_TYPE_CARVEOUT skipped heap_data->base == %lx \n", heap_data->base);
			continue;
		}

		if (heap_data->type == ION_HEAP_TYPE_CHUNK && !heap_data->base) {
			pr_info("ion_dummy: ION_HEAP_TYPE_CHUNK skipped heap_data->base == %lx \n", heap_data->base);
			continue;
		}

		heaps[i] = ion_heap_create(heap_data);
		if (IS_ERR_OR_NULL(heaps[i])) {
			pr_info("ion_dummy: ion_heap_create failed, returned = %lx, for heap id = %d \n", (unsigned long)(heaps[i]), i);
			err = PTR_ERR(heaps[i]);
			goto err;
		}
		ion_device_add_heap(idev, heaps[i]);
	}

	pr_info("%s:ion ion_device_create success idev = %lx \n", __func__, (long)idev);
	client = ion_client_create(idev, "ion_client");
	if (IS_ERR_OR_NULL(client)) {
		pr_info("%s:ion ion_client_create failed idev = %lx client = %li\n", __func__, (long)idev, PTR_ERR(client));
		ion_device_destroy(idev);
		return (client);
	}

	return client;

err:
	for (i = 0; i < dummy_ion_pdata.nr; ++i)
		ion_heap_destroy(heaps[i]);
	kfree(heaps);

	if (carveout_ptr) {
		free_pages_exact(carveout_ptr,
				dummy_heaps[ION_HEAP_TYPE_CARVEOUT].size);
		carveout_ptr = NULL;
	}
	if (chunk_ptr) {
		free_pages_exact(chunk_ptr,
				dummy_heaps[ION_HEAP_TYPE_CHUNK].size);
		chunk_ptr = NULL;
	}
	ion_device_destroy(idev);
	return ERR_PTR(err);

	return client;
}

/*
 * returns error code or success (0)
 */
struct ion_client *de_heap_ion_destroy_ion_client(struct ion_client *dmabuf_ion_client)
{
	pr_info("%s:de_heap_ion_destroy_ion_client \n", __func__);
	if (IS_ERR(idev) || idev == NULL) {
		pr_err("%s:ion device not present idev = %li \n", __func__, (idev==NULL) ? (long)NULL : PTR_ERR(idev));
		return (struct ion_client *)(idev);
	}
	if (dmabuf_ion_client>0) {
		ion_client_destroy(dmabuf_ion_client);
	}
	ion_device_destroy(idev);
	idev = NULL;
	return NULL;
}

#elif ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)))
#else
#error "Linux kernel not supported"
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

/*
 * de_heap_ion.h
 *
 * platform specific interface for de_heap_ion
 *
 */

#ifndef DE_HEAP_ION_H
#define DE_HEAP_ION_H

#include <linux/version.h>

/*
 * gcc preprocessor defines "linux" as "1".
 * [ http://stackoverflow.com/questions/19210935 ]
 * IMG_KERNEL_ION_HEADER can be <linux/ion.h>, which expands to <1/ion.h>
 */
#undef linux
#include IMG_KERNEL_ION_HEADER

/*
 * fetch the ion heap number (argument to ion_alloc)
 */
unsigned int de_heap_ion_get_heap_mask(void);


#if ((LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)))
unsigned int de_heap_ion_get_heap_id_mask(void);
#endif

/*
 * fetch the ion flags (argument to ion_alloc)
 */
unsigned int de_heap_ion_get_heap_flags(void);

/*
 * fetch an ion client instance
 *
 * the implementation of this usually depends on ion_device (ion_client_create)
 * which is platform specific
 */
struct ion_client *de_heap_ion_create_ion_client(void);

struct ion_client *de_heap_ion_destroy_ion_client(struct ion_client *dmabuf_ion_client);

#endif /* DE_HEAP_ION_H */

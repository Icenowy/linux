/*
 * de_heap.h
 */

#ifndef DE_HEAP_H
#define DE_HEAP_H

#include <linux/types.h>

int de_heap_buffer_create(size_t size, unsigned long align,
			  void **private_data);
int de_heap_export_fd(void *private_data, unsigned long flags);
void de_heap_buffer_free(void *private_data);

int de_heap_heap_init(void);
void de_heap_heap_deinit(void);

#endif /* DE_HEAP_H */

/*
 * coding style for emacs
 *
 * Local variables:
 * indent-tabs-mode: t
 * tab-width: 8
 * c-basic-offset: 8
 * End:
 */

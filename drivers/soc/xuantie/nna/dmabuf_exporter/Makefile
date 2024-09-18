#
# Makefile for dmabuf exporter implementations
#

ifdef CONFIG_ION
obj-$(CONFIG_VHA) += dmabuf_exporter_ion.o
dmabuf_exporter_ion-objs := de_common.o de_heap_ion.o
dmabuf_exporter_ion-objs += de_heap_ion_example.o

# detect ION header in Linux Kernel tree
# srctree is needed here for kernels built with separate object dir (O=)
ifneq ($(wildcard $(srctree)/include/linux/ion.h),)
# some kernel trees have this non-standard path
ccflags-y += -DIMG_KERNEL_ION_HEADER="<linux/ion.h>"
else
# this is the default location
# the vanilla linux kernel does not export ion.h to include/linux
# adding -I to the entire directory would expose many internal header files
# so we use this somewhat ugly hack to use only this one with full path
# realpath is needed to expand full path, some kernel trees set srctree to .
ccflags-y += -DIMG_KERNEL_ION_HEADER="<$(realpath $(srctree))/drivers/staging/android/ion/ion.h>"
ccflags-y += -DIMG_KERNEL_ION_PRIV_HEADER="<$(realpath $(srctree))/drivers/staging/android/ion/ion_priv.h>"
endif
endif

ifdef CONFIG_DMA_SHARED_BUFFER
obj-$(CONFIG_VHA) += dmabuf_exporter_coherent.o
dmabuf_exporter_coherent-objs := de_common.o de_heap_coherent.o
obj-$(CONFIG_VHA) += dmabuf_exporter_noncoherent.o
dmabuf_exporter_noncoherent-objs := de_common.o de_heap_noncoherent.o
endif

ifdef CONFIG_GENERIC_ALLOCATOR
obj-$(CONFIG_VHA) += dmabuf_exporter_carveout.o
dmabuf_exporter_carveout-objs := de_common.o de_heap_carveout.o
endif

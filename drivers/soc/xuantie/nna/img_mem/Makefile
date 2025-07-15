img_mem-y                           := img_mem_man.o
img_mem-$(CONFIG_GENERIC_ALLOCATOR) += img_mem_carveout.o
# removed building ION, kernel 4.14 ABI changes not implemented yet
img_mem-$(CONFIG_ION)               += img_mem_ion.o
img_mem-$(CONFIG_DMA_SHARED_BUFFER) += img_mem_dmabuf.o
img_mem-y                           += img_mem_unified.o img_mem_coherent.o
img_mem-y                           += img_mem_anonymous.o
img_mem-y                           += img_mem_ocm.o
img_mem-y                           += img_pdump.o


ifeq ($(CONFIG_ION), y)
CFLAGS_img_mem_ion.o += -Idrivers/staging/android/ion
endif

# IMGMMU: These should be obsoleted
ccflags-y += -I$(srctree)/$(src)/imgmmu/mmulib
# IMGMMU: code
img_mem-y                           += imgmmu/imgmmu.o
img_mem-y                           += imgmmu/kernel_heap.o

obj-$(CONFIG_VHA) += img_mem.o

# Alias for backward compatibility
CONFIG_HW_AX3_MC        := $(CONFIG_HW_MULTICORE)
# Magna does not use multiple OSes approach
ifeq ($(CONFIG_HW_AX3), y)
  ifeq ($(CONFIG_HW_AX3_MC),)
      ccflags-y += -DOSID=$(CONFIG_TARGET_OSID)
  endif
endif

ccflags-y +=-DDEFAULT_SYMBOL_NAMESPACE=IMG_MEM

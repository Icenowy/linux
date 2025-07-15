# if building the kernel module in-tree, these config options
# should be put into a Kconfig file.
# if building out-of-tree, defining them here is as good as any.
# CONFIG_VHA_NEXEF_PLAT:       build the VHA 3NX-F platform driver
export CONFIG_VHA_NEXEF_PLAT := m

ifeq ($(CONFIG_NEXEF_NNPU_INCLUDE),)
  $(error CONFIG_NEXEF_NNPU_INCLUDE not set: You must provide the path to the NNPU tc_drv.h file in)
endif

ccflags-y += -I$(CONFIG_NEXEF_NNPU_INCLUDE)

ccflags-y += -I$(srctree)/$(src)/../include
ccflags-y += -I$(srctree)/$(src)/
ccflags-y += -Wall -g

# This should not be needed, but on some platform (especially old kernel)
# the module can't be build without it.
ccflags-y += -DRETPOLINE -Wfatal-errors

# NOT WORKING FOR NOW, DO NOT ENABLE
#ccflags-y += -DCONFIG_SET_FPGA_CLOCK=y

obj-$(CONFIG_VHA_NEXEF_PLAT) += nexef_plat.o

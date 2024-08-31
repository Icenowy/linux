# Copyright (C) Microarray
# MicroArray Fprint Driver Code
# MAKEFILE
# Date: 2017-3-15
# Version: v4.0.06 
# Author: guq
# Contact: guq@microarray.com.cn


subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include \
					-I$(srctree)/drivers/misc/mediatek/include/mt-plat

MTK_PLATFORM := $(subst ",,$(CONFIG_MTK_PLATFORM))
subdir-ccflags-y += -Werror
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat
subdir-ccflags-y += -I$(srctree)/drivers/spi/mediatek/$(MTK_PLATFORM)

#compatible for trustkernel
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/tkcore/include/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/tkcore/core

#compatible for microtrust
ccflags-y += -I$(srctree)/drivers/misc/mediatek/teei/V1.0/tz_vfs

obj-y	+= madev.o mtk-settings.o



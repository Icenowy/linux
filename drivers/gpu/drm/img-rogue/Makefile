img_basedir := $(srctree)/$(src)
include $(img_basedir)/config_kernel.mk

obj-$(CONFIG_DRM_POWERVR_ROGUE) += pvrsrvkm.o

ccflags-y += \
 -include config_kernel.h \
 -I$(img_basedir)/include/drm \
 -I$(img_basedir) \
 -I$(img_basedir)/include \
 -I$(img_basedir)/km \
 -D__linux__

include $(img_basedir)/pvrsrvkm.mk

obj-$(CONFIG_DRM_POWERVR_ROGUE)	+= drm_nulldisp.o

drm_nulldisp-y	+= drm_nulldisp_drv.o drm_nulldisp_netlink.o drm_netlink_gem.o drm_nulldisp_gem.o

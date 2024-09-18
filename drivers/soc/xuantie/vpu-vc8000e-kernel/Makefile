##
 # Copyright (C) 2020 Alibaba Group Holding Limited
##
ifneq ($(wildcard ../.param),)
  include ../.param
endif

#CONFIG_DEBUG_MODE=1
CONFIG_OUT_ENV=hwlinux

CONFIG_BUILD_DRV_EXTRA_PARAM:=""    	

DIR_TARGET_BASE=bsp/venc		
DIR_TARGET_KO  =bsp/venc/ko
LINUX_DIR ?= $(OUT)/obj/KERNEL_OBJ/

MODULE_NAME=VENC
BUILD_LOG_START="\033[47;30m>>> $(MODULE_NAME) $@ begin\033[0m"
BUILD_LOG_END  ="\033[47;30m<<< $(MODULE_NAME) $@ end\033[0m"

#
# Do a parallel build with multiple jobs, based on the number of CPUs online
# in this system: 'make -j8' on a 8-CPU system, etc.
#
# (To override it, run 'make JOBS=1' and similar.)
#

ifeq ($(JOBS),)
  JOBS := $(shell grep -c ^processor /proc/cpuinfo 2>/dev/null)
  ifeq ($(JOBS),)
    JOBS := 1
  endif
endif

all:    info driver install_local_output
.PHONY: info driver install_local_output install_addons install_prepare clean_driver clean_output clean

info:
	@echo $(BUILD_LOG_START)
	@echo "  ====== Build Info from repo project ======"
	@echo "    BUILDROOT_DIR="$(BUILDROOT_DIR)
	@echo "    CROSS_COMPILE="$(CROSS_COMPILE)
	@echo "    LINUX_DIR="$(LINUX_DIR)
	@echo "    ARCH="$(ARCH)
	@echo "    BOARD_NAME="$(BOARD_NAME)
	@echo "    KERNEL_ID="$(KERNELVERSION)
	@echo "    KERNEL_DIR="$(LINUX_DIR)
	@echo "    INSTALL_DIR_ROOTFS="$(INSTALL_DIR_ROOTFS)
	@echo "    INSTALL_DIR_SDK="$(INSTALL_DIR_SDK)
	@echo "  ====== Build configuration by settings ======"
#	@echo "    CONFIG_DEBUG_MODE="$(CONFIG_DEBUG_MODE)
	@echo "    CONFIG_OUT_ENV="$(CONFIG_OUT_ENV)
	@echo "    JOBS="$(JOBS)
	@echo $(BUILD_LOG_END)

driver:
	@echo $(BUILD_LOG_START)
	make -C linux/kernel_module KDIR=$(LINUX_DIR)  ARCH=$(ARCH)
	@echo $(BUILD_LOG_END)

clean_driver:
	@echo $(BUILD_LOG_START)
	make -C linux/kernel_module KDIR=$(LINUX_DIR) clean
	@echo $(BUILD_LOG_END)

install_prepare:
	mkdir -p ./output/rootfs/$(DIR_TARGET_KO)

install_addons: install_prepare
	@if [ -d addons/ko ]; then                                 \
	    cp -rf addons/ko/* ./output/rootfs/$(DIR_TARGET_KO); \
	fi

install_local_output: install_addons install_prepare driver
	@echo $(BUILD_LOG_START)
	find ./linux -name "*.ko" | xargs -i cp -f {} ./output/rootfs/$(DIR_TARGET_KO)
	cp -f ./linux/kernel_module/driver_load.sh ./output/rootfs/$(DIR_TARGET_KO)
	chmod +x ./output/rootfs/$(DIR_TARGET_KO)/*.sh
	echo "vc8000" > ./output/rootfs/$(DIR_TARGET_KO)/vc8000e.conf
	@if [ `command -v tree` != "" ]; then \
	    tree ./output/rootfs;             \
	fi
	@echo $(BUILD_LOG_END)

clean_output:
	@echo $(BUILD_LOG_START)
	rm -rf ./output
	@echo $(BUILD_LOG_END)

clean: clean_output clean_driver


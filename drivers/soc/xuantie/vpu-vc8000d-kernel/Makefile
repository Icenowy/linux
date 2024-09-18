##
 # Copyright (C) 2020 Alibaba Group Holding Limited
##

DIR_TARGET_KO  =bsp/vdec/ko

MODULE_NAME=VDEC
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

all:    driver install_local_output
.PHONY: driver install_local_output install_addons install_prepare clean_driver clean_output clean

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
	@echo "    INSTALL_DIR_SDK="$(INSTALL_DIR_SDK)
	@echo "  ====== Build configuration by settings ======"
	@echo "    JOBS="$(JOBS)
	@echo $(BUILD_LOG_END)

driver:
	@echo $(BUILD_LOG_START)
	make -C linux/subsys_driver KDIR=$(LINUX_DIR) CROSS=$(CROSS_COMPILE) ARCH=$(ARCH)
	make -C linux/memalloc KDIR=$(LINUX_DIR) CROSS=$(CROSS_COMPILE) ARCH=$(ARCH)
	@echo $(BUILD_LOG_END)

clean_driver:
	@echo $(BUILD_LOG_START)
	make -C linux/subsys_driver clean
	make -C linux/memalloc clean
	@echo $(BUILD_LOG_END)

install_prepare:
	mkdir -p ./output/rootfs/$(DIR_TARGET_KO)

install_addons: install_prepare
	@if [ -d addons/ko ]; then                                 \
	    cp -rf addons/ko/* ./output/rootfs/$(DIR_TARGET_KO); \
	fi

install_local_output: driver install_prepare install_addons
	@echo $(BUILD_LOG_START)
	find ./linux -name "*.ko" | xargs -i cp -f {} ./output/rootfs/$(DIR_TARGET_KO)
	cp -f ./linux/subsys_driver/driver_load.sh ./output/rootfs/$(DIR_TARGET_KO)
	cp -f ./linux/memalloc/memalloc_load.sh ./output/rootfs/$(DIR_TARGET_KO)
	chmod +x ./output/rootfs/$(DIR_TARGET_KO)/*.sh
	echo "hantrodec" > ./output/rootfs/$(DIR_TARGET_KO)/vc8000d.conf
	@if [ `command -v tree` != "" ]; then \
	    tree ./output/rootfs;             \
	fi
	@echo $(BUILD_LOG_END)

clean_output:
	@echo $(BUILD_LOG_START)
	rm -rf ./output
	@echo $(BUILD_LOG_END)

clean: clean_output clean_driver


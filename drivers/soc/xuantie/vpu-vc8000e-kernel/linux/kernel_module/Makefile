#############################################################################
#
#    The MIT License (MIT)
#
#    Copyright (c) 2014 - 2021 VERISILICON
#
#    Permission is hereby granted, free of charge, to any person obtaining a
#    copy of this software and associated documentation files (the "Software"),
#    to deal in the Software without restriction, including without limitation
#    the rights to use, copy, modify, merge, publish, distribute, sublicense,
#    and/or sell copies of the Software, and to permit persons to whom the
#    Software is furnished to do so, subject to the following conditions:
#
#    The above copyright notice and this permission notice shall be included in
#    all copies or substantial portions of the Software.
#
#    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#    DEALINGS IN THE SOFTWARE.
#
#############################################################################
#
#    The GPL License (GPL)
#
#    Copyright (C) 2014 - 2021 VERISILICON
#
#    This program is free software; you can redistribute it and/or
#    modify it under the terms of the GNU General Public License
#    as published by the Free Software Foundation; either version 2
#    of the License, or (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software Foundation,
#    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
#############################################################################
#
#    Note: This software is released under dual MIT and GPL licenses. A
#    recipient may use this file under the terms of either the MIT license or
#    GPL License. If you wish to use only one license not the other, you can
#    indicate your decision by deleting one of the above license notices in your
#    version of this file.
#
##############################################################################
ARM_CROSS_COMPILE ?= n

ifeq ($(ARM_CROSS_COMPILE),y)
export ARCH=arm64
export CROSS_COMPILE=/opt/kmb/gcc-arm-8.3-2019.03-x86_64-aarch64-linux-gnu/bin/aarch64-linux-gnu-
KDIR := /home/vsi/kmb-evm/kernel/mainline-tracking
endif

SUPPORT_MMU = y
SUPPORT_AXIFE = n
SUPPORT_VCMD_ENABLE_IP = n

ifeq ($(obj),)
obj = .
endif

#################################################
# configuration

MDIR		:= hantro

# drivers objects
# list-multi	:= hantro_mmu.o

# # what to build

vc8000-objs :=  vc8000_driver.o vc8000_vcmd_driver.o bidirect_list.o vcmdswhwregisters.o vc8000_normal_driver.o
obj-m         += vc8000.o

ifeq ($(strip $(SUPPORT_MMU)),y)
vc8000-objs += hantro_mmu.o
endif

ifeq ($(strip $(SUPPORT_AXIFE)),y)
vc8000-objs += vc8000_axife.o
endif

tardest		:= .

#################################################
# compile modules

ifneq ($(KERNELRELEASE),)
# recursive call from kernel build system
dummy		:= $(shell echo $(KERNELRELEASE) > $(obj)/.version)

ifeq ($(VERSION).$(PATCHLEVEL),2.6)
 export-objs	:=
 list-multi	:=
else
 multi-m	:= $(filter $(list-multi), $(obj-m))
 int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))
 export-objs	:= $(filter $(int-m) $(obj-m),$(export-objs))
endif

CC += -I$(obj)
EXTRA_CFLAGS += -g

#ifeq ($(CONFIG_ANDROID), y)
#ccflags-y += -DTRACE_INCLUDE_PATH=$(src)
#else
#ccflags-y += -DTRACE_INCLUDE_PATH=$(PWD)
#endif

# Print debugging messages from the device
#EXTRA_CFLAGS	+= -DHANTRO_DRIVER_DEBUG

#EXTRA_CFLAGS += -DHANTROMMU_DEBUG
#EXTRA_CFLAGS   += -DDYNAMIC_MALLOC_VCMDNODE

snapshot := $(wildcard $(obj)/.snapshot)
ifneq ($(snapshot),)
SNAPSHOT_CFLAGS	:= -DSNAPSHOT='$(shell cat $(snapshot))'
EXTRA_CFLAGS	+= $(SNAPSHOT_CFLAGS)
endif

ifeq ($(strip $(SUPPORT_MMU)),y)
EXTRA_CFLAGS   += -DHANTROMMU_SUPPORT
endif

ifeq ($(strip $(SUPPORT_AXIFE)),y)
EXTRA_CFLAGS   += -DHANTROAXIFE_SUPPORT
endif

ifeq ($(strip $(SUPPORT_VCMD_ENABLE_IP)),y)
EXTRA_CFLAGS   += -DHANTROVCMD_ENABLE_IP_SUPPORT
endif

-include $(TOPDIR)/Rules.make
else
# take version info from last module build if available
KERNELRELEASE	:= $(shell cat $(obj)/.version 2>/dev/null || uname -r)
endif
ifneq ($(ARM_CROSS_COMPILE),y)
KDIR_BASE := /afs/hantro.com/projects/Testing/Board_Version_Control

#KDIR := $(KDIR_BASE)/Realview_EB/SW/Linux/v0_0/linux-2.6.19-arm2
#KDIR := $(KDIR_BASE)/Realview_EB/SW/Linux/linux-2.6.21-arm1/v0_0/linux-2.6.21-arm1
#KDIR := $(KDIR_BASE)/Realview_PB/PB926EJS/SW/Linux/linux-2.6.24-arm2-spnlck/v0_1/linux-2.6.24-arm2-spnlck
#KDIR := $(KDIR_BASE)/Realview_PB/PB926EJS/SW/Linux/linux-2.6.28-arm1/v0_1/linux-2.6.28-arm1
#KDIR := $(KDIR_BASE)/SW_Common/ARM_realview_v6/2.6.28-arm1/v0_1-v6/linux-2.6.28-arm1

KVER := $(shell uname -r)
KDIR := /lib/modules/$(KVER)/build
endif

PWD		:= $(shell pwd)
DEST		:= /lib/modules/$(KERNELRELEASE)/$(MDIR)

# which files to install?
inst-m		:= $(wildcard *.ko)
ifeq ($(inst-m),)
  inst-m	:= $(obj-m)
endif


# locales seem to cause trouble sometimes.
LC_ALL = POSIX
export LC_ALL

default::
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install::
	strip --strip-debug $(inst-m)
	-su -c "mkdir -p $(DEST); cp -v $(inst-m) $(DEST); depmod -a"

clean::
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	-rm -f .version

#################################################
# build tarballs

thisdir	:= $(notdir $(PWD))
name	:= $(shell echo $(thisdir) | sed 's/-.*//')
ver	:= $(shell echo $(thisdir) | sed 's/.*-//')
date    := $(shell date +%Y%m%d)
tardest	?= .

snapdir := $(HOME)/snapshot
snap	?= $(name)

release: clean
	rm -f .snapshot
	(cd ..; tar cvzf $(tardest)/$(name)-$(ver).tar.gz $(thisdir))

snapshot snap tarball: clean
	echo $(date) > .snapshot
	(cd ..; tar czf $(snapdir)/$(snap)-$(date).tar.gz $(thisdir))
	$(MAKE) -C $(snapdir)

#################################################
# other stuff

%.asm: %.o
	objdump -S $< > $@


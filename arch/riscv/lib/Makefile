# SPDX-License-Identifier: GPL-2.0-only
lib-y			+= delay.o

ifeq ($(CONFIG_XUANTIE_ISA),y)
lib-y			+= memcpy_xt.o
else
lib-y			+= memcpy.o
endif
lib-y			+= memset.o
lib-y			+= memmove.o
lib-y			+= strcmp.o
lib-y			+= strlen.o
lib-y			+= strncmp.o
lib-$(CONFIG_MMU)	+= uaccess.o
lib-$(CONFIG_64BIT)	+= tishift.o
lib-$(CONFIG_RISCV_ISA_ZICBOZ)	+= clear_page.o

obj-$(CONFIG_FUNCTION_ERROR_INJECTION) += error-inject.o

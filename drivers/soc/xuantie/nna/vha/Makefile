# Alias for backward compatibility
CONFIG_VHA_APOLLO       := $(CONFIG_VHA_FPGA)
CONFIG_HW_AX3_MC        := $(CONFIG_HW_MULTICORE)

obj-$(CONFIG_VHA)       += vha.o
CONFIG_VHA_INFO         := $(CONFIG_VHA)
obj-$(CONFIG_VHA_INFO)  += vha_info.o

# Common files
vha-y                   := vha_api.o vha_common.o
vha-y                   += vha_devfreq.o
vha-y                   += vha_dbg.o vha_pdump.o

ifeq ($(CONFIG_HW_AX3_MC), y) 
subdir-ccflags-y        += -I$(srctree)/$(src)/multi
vha-y                   += multi/vha_dev.o multi/vha_wm.o multi/vha_mmu.o multi/vha_mt19937.o
else
subdir-ccflags-y        += -I$(srctree)/$(src)/single
vha-y                   += single/vha_dev.o single/vha_cnn.o single/vha_mmu.o
vha-$(CONFIG_HW_AX2)    += single/vha_dev_ax2.o
vha-$(CONFIG_HW_AX3)    += single/vha_dev_ax3.o
endif

PLAT := platform
vha-$(CONFIG_VHA_PCI)          += $(PLAT)/vha_plat_pci.o
vha-$(CONFIG_VHA_DUMMY)        += $(PLAT)/vha_plat_dummy.o
vha-$(CONFIG_VHA_DUMMY_HW_SIM) += $(PLAT)/vha_plat_dummy.o
vha-$(CONFIG_VHA_APOLLO)       += $(PLAT)/vha_plat_apollo.o
vha-$(CONFIG_VHA_ODIN)         += $(PLAT)/vha_plat_odin.o
vha-$(CONFIG_VHA_EMU)          += $(PLAT)/vha_plat_emu.o
vha-$(CONFIG_VHA_FROST)        += $(PLAT)/vha_plat_frost.o
vha-$(CONFIG_VHA_ORION)        += $(PLAT)/vha_plat_orion.o
vha-$(CONFIG_VHA_NEXEF)        += $(PLAT)/vha_plat_nexef.o
vha-$(CONFIG_VHA_DT_EXAMPLE)   += $(PLAT)/vha_plat_dt_example.o $(PLAT)/vha_plat_dt.o

vha-$(CONFIG_VHA_XUANTIE_TH1520_FPGA_C910)   += $(PLAT)/vha_plat_xuantie.o $(PLAT)/vha_plat_xuantie_th1520_fpga_c910.o
vha-$(CONFIG_VHA_XUANTIE_TH1520)   += $(PLAT)/vha_plat_xuantie.o $(PLAT)/vha_plat_xuantie_th1520.o

# System configuration
# For AX2 set Mirage by default
CONFIG_VHA_SYS_MIRAGE := $(CONFIG_HW_AX2)
# For 3NX set to Aura by default. 
CONFIG_VHA_SYS_AURA := $(CONFIG_HW_AX3)
# For 4NX(3NX-MC) set to Magna by default.
CONFIG_VHA_SYS_MAGNA := $(CONFIG_HW_AX3_MC)

ccflags-$(CONFIG_VHA_SYS_MIRAGE) += -DCFG_SYS_MIRAGE
ccflags-$(CONFIG_VHA_SYS_AURA)   += -DCFG_SYS_AURA
ccflags-$(CONFIG_VHA_SYS_VAGUS)  += -DCFG_SYS_VAGUS
ccflags-$(CONFIG_VHA_SYS_MAGNA)  += -DCFG_SYS_MAGNA

ccflags-y        += -I$(srctree)/$(src)/$(PLAT)/
subdir-ccflags-y += -I$(srctree)/$(src)

ifdef CONFIG_NEXEF_NNPU_INCLUDE
    subdir-ccflags-y += -I${CONFIG_NEXEF_NNPU_INCLUDE}
endif

ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=VHA_CORE

ccflags-$(CONFIG_BUS_MASTERING) += -DFPGA_BUS_MASTERING

ccflags-$(CONFIG_VHA_DUMMY)        += -DCONFIG_VHA_DUMMY
ccflags-$(CONFIG_VHA_DUMMY_HW_SIM) += -DCONFIG_VHA_DUMMY -DCONFIG_VHA_DUMMY_SIMULATE_HW_PROCESSING_TIME
ccflags-$(CONFIG_VHA_APOLLO)       += -DCONFIG_VHA_APOLLO
ccflags-$(CONFIG_VHA_ORION)        += -DCONFIG_VHA_ORION
ccflags-$(CONFIG_VHA_ODIN)         += -DCONFIG_VHA_ODIN
ccflags-$(CONFIG_VHA_NEXEF)        += -DCONFIG_VHA_NEXEF

ccflags-$(CONFIG_ION)    += -Idrivers/staging/android/ion

ccflags-$(CONFIG_HW_AX2) += -DHW_AX2

# Mirrored page tables enabled by default if not specified
CONFIG_VHA_MMU_MIRRORED_CTX ?= y
ccflags-$(CONFIG_VHA_MMU_MIRRORED_CTX) += -DVHA_MMU_MIRRORED_CTX_SUPPORT

ccflags-$(CONFIG_HW_AX3) += -DHW_AX3

# Default cores
CONFIG_VHA_NCORES ?= 6
ccflags-$(CONFIG_HW_AX3_MC) += -DCONFIG_HW_MULTICORE -DCONFIG_VHA_NCORES=$(CONFIG_VHA_NCORES)

# Enhanced APM enabled by default if not specified
CONFIG_VHA_ENHANCED_APM ?= y
ccflags-$(CONFIG_VHA_ENHANCED_APM) += -DVHA_ENHANCED_APM

# Default OSID = 0
CONFIG_TARGET_OSID ?= 0
# Magna does not use multiple OSes approach
ifeq ($(CONFIG_HW_AX3), y)
  ifeq ($(CONFIG_HW_AX3_MC),)
      ccflags-y += -DOSID=$(CONFIG_TARGET_OSID)
  endif
endif

# Enable Safety critical features by default
ifeq ($(CONFIG_HW_AX3_MC)$(CONFIG_VHA_SYS_VAGUS), y)
    CONFIG_VHA_SCF ?= y
endif

ifeq ($(CONFIG_HW_AX3_MC)$(CONFIG_VHA_SCF), yy)
    vha-y                 += multi/vha_sc_dbg.o
endif

ccflags-$(CONFIG_VHA_SCF) += -DVHA_SCF

# enable support for *_EVENT_INJECT registers only for MC 
# and if kernel FUNCTION_ERROR_INJECTION is enabled
ifeq ($(CONFIG_FUNCTION_ERROR_INJECTION), y)
  ifeq ($(CONFIG_HW_AX3), y)
    ccflags-y += -DVHA_EVENT_INJECT
  endif
endif

ifeq ($(CONFIG_FUNCTONAL_TEST_CONTROL), y)
    ccflags-y += -DVHA_FUNCT_CTRL
endif

ifeq ($(CONFIG_FORCE_IOREG_DEBUG), y)
    ccflags-y += -DVHA_FORCE_IO_DEBUG
endif

# Magna does not use multiple OSes approach
ifeq ($(CONFIG_HW_AX3), y)
  ifeq ($(CONFIG_HW_AX3_MC),y)
    CONFIG_VHA_LO_PRI_SUBSEGS = n
  else
    CONFIG_VHA_LO_PRI_SUBSEGS ?= n
  endif
else
    CONFIG_VHA_LO_PRI_SUBSEGS = n
endif
ccflags-$(CONFIG_VHA_LO_PRI_SUBSEGS) += -DVHA_USE_LO_PRI_SUB_SEGMENTS
ifeq ($(CONFIG_VHA_LO_PRI_SUBSEGS), y)
    obj-$(CONFIG_VHA) += vha_monitor.o
endif


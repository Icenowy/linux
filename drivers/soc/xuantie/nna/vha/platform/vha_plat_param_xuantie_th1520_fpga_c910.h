/*!
 *****************************************************************************
 *
 * @File       vha_plat_param_xuantie_th1520_fpga_c910.h
 * ---------------------------------------------------------------------------
 *
 * Copyright (C) 2020 Alibaba Group Holding Limited
 *
 *****************************************************************************/

#ifndef VHA_PLAT_PARAM_XUANTIE_TH1520_FPGA_C910_H
#define VHA_PLAT_PARAM_XUANTIE_TH1520_FPGA_C910_H

/* Core clock frequency: default 30MHz */
#define VHA_CORE_CLOCK_MHZ 1000

/* Core watchdog cycles default value */
/* MMM can transfer any number of bytes at cost of higher cycles, setting it to ~100ms @800MHz */
#define VHA_CORE_WDT_CYCLES (0x4ffffff*VHA_CORE_CLOCK_MHZ/800)
/* Memory watchdog is set ~1ms @800MHz which is very safe value to avoid any false interrupts */
#define VHA_MEM_WDT_CYCLES (0xfffff*VHA_CORE_CLOCK_MHZ/800)

/* Memory burst size */
#define VHA_CORE_MH_MAX_BURST_LENGTH 128
/* SLC cache policy type (0-use cache, 1-bypass cache) */
#define VHA_CORE_MH_SLC_CACHE_POLICY_TYPE 1
/* GPU pipe coherent type */
#define VHA_CORE_MH_GPU_PIPE_COHERENT_TYPE 1
/* Persistence priority 0-lowest,3-highest */
#define VHA_CORE_MH_PERSISTENCE_PRIO 0

/* Suspend delay in ms after which the
 * runtime suspend callback is called */
#define VHA_CORE_SUSPEND_DELAY 10

#endif /* VHA_PLAT_PARAM_XUANTIE_TH1520_FPGA_C910_H */

/*************************************************************************/ /*!
@File
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <linux/clk.h>
#include <linux/interrupt.h>
#include "interrupt_support.h"
#include "pvrsrv_device.h"
#include "syscommon.h"
#include "vz_vmm_pvz.h"
#include "allocmem.h"
#include "sysinfo.h"
#include "sysconfig.h"
#include "physheap.h"
#include "pvr_debug.h"
#if defined(SUPPORT_ION)
#include "ion_support.h"
#endif
#if defined(__linux__)
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#endif
#include "rgx_bvnc_defs_km.h"
#include "xuantie_sys.h"
/*
 * In systems that support trusted device address protection, there are three
 * physical heaps from which pages should be allocated:
 * - one heap for normal allocations
 * - one heap for allocations holding META code memory
 * - one heap for allocations holding secured DRM data
 */

#define PHYS_HEAP_IDX_GENERAL     0
#define PHYS_HEAP_IDX_FW          1

#if defined(SUPPORT_TRUSTED_DEVICE)
#define PHYS_HEAP_IDX_TDFWMEM     2
#define PHYS_HEAP_IDX_TDSECUREBUF 3
#elif defined(SUPPORT_DEDICATED_FW_MEMORY)
#define PHYS_HEAP_IDX_FW_MEMORY   2
#endif

#define UMA_HEAP_USAGE_FLAG PHYS_HEAP_USAGE_GPU_LOCAL

#define UMA_DEFAULT_HEAP PVRSRV_PHYS_HEAP_GPU_LOCAL

#if defined(SUPPORT_LINUX_DVFS)
static struct clk *xuantie_gpu_core_clk = NULL;

static void SetFrequency(IMG_UINT32 ui32Frequency)
{
	if (!xuantie_gpu_core_clk)
	{
		PVR_DPF((PVR_DBG_ERROR, "xuantie_gpu_core_clk is NULL"));
		return;
	}
	clk_set_rate(xuantie_gpu_core_clk, ui32Frequency);
}

static void SetVoltage(IMG_UINT32 ui32Voltage)
{
	
}
#endif

/*
	CPU to Device physical address translation
*/
static
void UMAPhysHeapCpuPAddrToDevPAddr(IMG_HANDLE hPrivData,
								   IMG_UINT32 ui32NumOfAddr,
								   IMG_DEV_PHYADDR *psDevPAddr,
								   IMG_CPU_PHYADDR *psCpuPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psDevPAddr[0].uiAddr = psCpuPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psDevPAddr[ui32Idx].uiAddr = psCpuPAddr[ui32Idx].uiAddr;
		}
	}
}

/*
	Device to CPU physical address translation
*/
static
void UMAPhysHeapDevPAddrToCpuPAddr(IMG_HANDLE hPrivData,
								   IMG_UINT32 ui32NumOfAddr,
								   IMG_CPU_PHYADDR *psCpuPAddr,
								   IMG_DEV_PHYADDR *psDevPAddr)
{
	PVR_UNREFERENCED_PARAMETER(hPrivData);

	/* Optimise common case */
	psCpuPAddr[0].uiAddr = psDevPAddr[0].uiAddr;
	if (ui32NumOfAddr > 1)
	{
		IMG_UINT32 ui32Idx;
		for (ui32Idx = 1; ui32Idx < ui32NumOfAddr; ++ui32Idx)
		{
			psCpuPAddr[ui32Idx].uiAddr = psDevPAddr[ui32Idx].uiAddr;
		}
	}
}

static PHYS_HEAP_FUNCTIONS gsPhysHeapFuncs =
{
	/* pfnCpuPAddrToDevPAddr */
	.pfnCpuPAddrToDevPAddr = UMAPhysHeapCpuPAddrToDevPAddr,
	/* pfnDevPAddrToCpuPAddr */
	.pfnDevPAddrToCpuPAddr = UMAPhysHeapDevPAddrToCpuPAddr,
};

static PVRSRV_ERROR PhysHeapsCreate(PHYS_HEAP_CONFIG **ppasPhysHeapsOut,
									IMG_UINT32 *puiPhysHeapCountOut)
{
	/*
	 * This function is called during device initialisation, which on Linux,
	 * means it won't be called concurrently. As such, there's no need to
	 * protect it with a lock or use an atomic variable.
	 */
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	IMG_UINT32 uiHeapCount = 2;

#if defined(SUPPORT_TRUSTED_DEVICE)
	uiHeapCount += 2;
#elif defined(SUPPORT_DEDICATED_FW_MEMORY)
	uiHeapCount += 1;
#endif

	pasPhysHeaps = OSAllocZMem(sizeof(*pasPhysHeaps) * uiHeapCount);
	if (!pasPhysHeaps)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].pszPDumpMemspaceName = "SYSMEM";
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[PHYS_HEAP_IDX_GENERAL].ui32UsageFlags = UMA_HEAP_USAGE_FLAG;

	pasPhysHeaps[PHYS_HEAP_IDX_FW].pszPDumpMemspaceName = "SYSMEM_FW";
	pasPhysHeaps[PHYS_HEAP_IDX_FW].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[PHYS_HEAP_IDX_FW].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[PHYS_HEAP_IDX_FW].ui32UsageFlags = PHYS_HEAP_USAGE_FW_MAIN;

#if defined(SUPPORT_TRUSTED_DEVICE)
	pasPhysHeaps[PHYS_HEAP_IDX_TDFWMEM].pszPDumpMemspaceName = "TDFWMEM";
	pasPhysHeaps[PHYS_HEAP_IDX_TDFWMEM].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[PHYS_HEAP_IDX_TDFWMEM].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[PHYS_HEAP_IDX_TDFWMEM].ui32UsageFlags =
		PHYS_HEAP_USAGE_FW_CODE | PHYS_HEAP_USAGE_FW_PRIV_DATA;

	pasPhysHeaps[PHYS_HEAP_IDX_TDSECUREBUF].pszPDumpMemspaceName = "TDSECBUFMEM";
	pasPhysHeaps[PHYS_HEAP_IDX_TDSECUREBUF].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[PHYS_HEAP_IDX_TDSECUREBUF].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[PHYS_HEAP_IDX_TDSECUREBUF].ui32UsageFlags =
		PHYS_HEAP_USAGE_GPU_SECURE;

#elif defined(SUPPORT_DEDICATED_FW_MEMORY)
	pasPhysHeaps[PHYS_HEAP_IDX_FW_MEMORY].pszPDumpMemspaceName = "DEDICATEDFWMEM";
	pasPhysHeaps[PHYS_HEAP_IDX_FW_MEMORY].eType = PHYS_HEAP_TYPE_UMA;
	pasPhysHeaps[PHYS_HEAP_IDX_FW_MEMORY].psMemFuncs = &gsPhysHeapFuncs;
	pasPhysHeaps[PHYS_HEAP_IDX_FW_MEMORY].ui32UsageFlags =
		PHYS_HEAP_USAGE_FW_CODE | PHYS_HEAP_USAGE_FW_PRIV_DATA;
#endif

	*ppasPhysHeapsOut = pasPhysHeaps;
	*puiPhysHeapCountOut = uiHeapCount;

	return PVRSRV_OK;
}

static void PhysHeapsDestroy(PHYS_HEAP_CONFIG *pasPhysHeaps)
{
	OSFreeMem(pasPhysHeaps);
}

static PVRSRV_ERROR SysDevPrePowerState(
       IMG_HANDLE hSysData,
       PVRSRV_SYS_POWER_STATE eNewPowerState,
       PVRSRV_SYS_POWER_STATE eCurrentPowerState,
       PVRSRV_POWER_FLAGS ePwrFlags)
{
   struct gpu_plat_if *mfg = hSysData;

   xuantie_debug("SysDevPrePowerState (%d->%d), bPwrFlags = 0x%08x\n",
             eCurrentPowerState, eNewPowerState, ePwrFlags);

   mutex_lock(&mfg->set_power_state);

   if ((PVRSRV_SYS_POWER_STATE_OFF == eNewPowerState) &&
       (PVRSRV_SYS_POWER_STATE_ON == eCurrentPowerState))
       xuantie_mfg_disable(mfg);

   mutex_unlock(&mfg->set_power_state);
   return PVRSRV_OK;
}

static PVRSRV_ERROR SysDevPostPowerState(
       IMG_HANDLE hSysData,
       PVRSRV_SYS_POWER_STATE eNewPowerState,
       PVRSRV_SYS_POWER_STATE eCurrentPowerState,
       PVRSRV_POWER_FLAGS ePwrFlags)
{
   struct gpu_plat_if *mfg = hSysData;
   PVRSRV_ERROR ret;

   xuantie_debug("SysDevPostPowerState (%d->%d)\n",
             eCurrentPowerState, eNewPowerState);

   mutex_lock(&mfg->set_power_state);

   if ((PVRSRV_SYS_POWER_STATE_ON == eNewPowerState) &&
       (PVRSRV_SYS_POWER_STATE_OFF == eCurrentPowerState)) {
       if (xuantie_mfg_enable(mfg)) {
           ret = PVRSRV_ERROR_DEVICE_POWER_CHANGE_FAILURE;
           goto done;
       }
   }

   ret = PVRSRV_OK;
done:
   mutex_unlock(&mfg->set_power_state);

   return ret;
}


static void SysDevFeatureDepInit(PVRSRV_DEVICE_CONFIG *psDevConfig, IMG_UINT64 ui64Features)
{
#if defined(SUPPORT_AXI_ACE_TEST)
		if ( ui64Features & RGX_FEATURE_AXI_ACELITE_BIT_MASK)
		{
			psDevConfig->eCacheSnoopingMode		= PVRSRV_DEVICE_SNOOP_CPU_ONLY;
		}else
#endif
		{
			psDevConfig->eCacheSnoopingMode		= PVRSRV_DEVICE_SNOOP_NONE;
		}
}

PVRSRV_ERROR SysDevInit(void *pvOSDevice, PVRSRV_DEVICE_CONFIG **ppsDevConfig)
{
	PVRSRV_DEVICE_CONFIG *psDevConfig;
	RGX_DATA *psRGXData;
	RGX_TIMING_INFORMATION *psRGXTimingInfo;
	PHYS_HEAP_CONFIG *pasPhysHeaps;
	IMG_UINT32 uiPhysHeapCount;
	PVRSRV_ERROR eError;
    struct gpu_plat_if *mfg;

#if defined(__linux__)
    int iIrq;
    struct resource *psDevMemRes = NULL;
    struct platform_device *psDev;

    psDev = to_platform_device((struct device *)pvOSDevice);
#endif

#if defined(__linux__)
	dma_set_mask(pvOSDevice, DMA_BIT_MASK(40));
#endif

	xuantie_sysfs_init(pvOSDevice);

   mfg = dt_hw_init(pvOSDevice);
   if (IS_ERR(mfg)) {
       if (PTR_ERR(mfg) == -EPROBE_DEFER)
           return PVRSRV_ERROR_PROBE_DEFER;
       else
           return PVRSRV_ERROR_INIT_FAILURE;
   }

	psDevConfig = OSAllocZMem(sizeof(*psDevConfig) +
							  sizeof(*psRGXData) +
							  sizeof(*psRGXTimingInfo));
	if (!psDevConfig)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psRGXData = (RGX_DATA *)((IMG_CHAR *)psDevConfig + sizeof(*psDevConfig));
	psRGXTimingInfo = (RGX_TIMING_INFORMATION *)((IMG_CHAR *)psRGXData + sizeof(*psRGXData));

	eError = PhysHeapsCreate(&pasPhysHeaps, &uiPhysHeapCount);
	if (eError)
	{
		goto ErrorFreeDevConfig;
	}

	/* Setup RGX specific timing data */
	psRGXTimingInfo->ui32CoreClockSpeed        = RGX_XUANTIE_CORE_CLOCK_SPEED;
	psRGXTimingInfo->bEnableActivePM           = IMG_TRUE;
	psRGXTimingInfo->bEnableRDPowIsland        = IMG_FALSE;
	psRGXTimingInfo->ui32ActivePMLatencyms     = SYS_RGX_ACTIVE_POWER_LATENCY_MS;

	/* Set up the RGX data */
	psRGXData->psRGXTimingInfo = psRGXTimingInfo;

	/* Setup the device config */
	psDevConfig->pvOSDevice				= pvOSDevice;
	psDevConfig->pszName                = "xuantie";
	psDevConfig->pszVersion             = NULL;
	psDevConfig->pfnSysDevFeatureDepInit = SysDevFeatureDepInit;

    /* Device setup information */
#if defined(__linux__)
    psDevMemRes = platform_get_resource(psDev, IORESOURCE_MEM, 0);
    if (psDevMemRes)
    {
        psDevConfig->sRegsCpuPBase.uiAddr = psDevMemRes->start;
        psDevConfig->ui32RegsSize         = (unsigned int)(psDevMemRes->end - psDevMemRes->start);
    }
    iIrq = platform_get_irq(psDev, 0);
    if (iIrq >= 0)
    {
        psDevConfig->ui32IRQ = (IMG_UINT32) iIrq;
    }
#else
    /* Device setup information */
    psDevConfig->sRegsCpuPBase.uiAddr   = 0xFFEF400000;
    psDevConfig->ui32RegsSize           = 0x100000;
    psDevConfig->ui32IRQ                = 102;
#endif

    PVR_LOG(("*****enter sys dev init %llx %d\r\n", psDevConfig->sRegsCpuPBase.uiAddr, psDevConfig->ui32IRQ));
	psDevConfig->pasPhysHeaps			= pasPhysHeaps;
	psDevConfig->ui32PhysHeapCount		= uiPhysHeapCount;
    psDevConfig->eDefaultHeap = UMA_DEFAULT_HEAP;

	/* No power management on no HW system */
    psDevConfig->pfnPrePowerState       = SysDevPrePowerState;
    psDevConfig->pfnPostPowerState      = SysDevPostPowerState;

	psDevConfig->bHasFBCDCVersion31      = IMG_FALSE;

	/* No clock frequency either */
	psDevConfig->pfnClockFreqGet        = NULL;

	psDevConfig->hDevData               = psRGXData;
    psDevConfig->hSysData               = mfg;

#if defined(SUPPORT_LINUX_DVFS)
	xuantie_gpu_core_clk = mfg->gpu_cclk;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pasOPPTable = NULL;
	psDevConfig->sDVFS.sDVFSDeviceCfg.bIdleReq = IMG_TRUE;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetFrequency = SetFrequency;
	psDevConfig->sDVFS.sDVFSDeviceCfg.pfnSetVoltage = SetVoltage;
	psDevConfig->sDVFS.sDVFSDeviceCfg.ui32PollMs = 50;
	psDevConfig->sDVFS.sDVFSGovernorCfg.ui32UpThreshold = 50;
	psDevConfig->sDVFS.sDVFSGovernorCfg.ui32DownDifferential = 10;
#endif

	/* Setup other system specific stuff */
#if defined(SUPPORT_ION)
	IonInit(NULL);
#endif

	/* Set psDevConfig->pfnSysDevErrorNotify callback */
	psDevConfig->pfnSysDevErrorNotify = SysRGXErrorNotify;

	*ppsDevConfig = psDevConfig;

	return PVRSRV_OK;

ErrorFreeDevConfig:
	OSFreeMem(psDevConfig);
	dt_hw_uninit(mfg);
	return eError;
}

void SysDevDeInit(PVRSRV_DEVICE_CONFIG *psDevConfig)
{
	struct gpu_plat_if *mfg = (struct gpu_plat_if*)psDevConfig->hSysData;

#if defined(SUPPORT_ION)
	IonDeinit();
#endif

	PhysHeapsDestroy(psDevConfig->pasPhysHeaps);
	xuantie_sysfs_uninit(psDevConfig->pvOSDevice);
	OSFreeMem(psDevConfig);
	dt_hw_uninit(mfg);
}

PVRSRV_ERROR SysInstallDeviceLISR(IMG_HANDLE hSysData,
								  IMG_UINT32 ui32IRQ,
								  const IMG_CHAR *pszName,
								  PFN_LISR pfnLISR,
								  void *pvData,
								  IMG_HANDLE *phLISRData)
{
    PVR_LOG(("*****enter sysintstall LISR\r\n"));
    PVR_UNREFERENCED_PARAMETER(hSysData);
    return OSInstallSystemLISR(phLISRData, ui32IRQ, pszName, pfnLISR, pvData,
                                SYS_IRQ_FLAG_TRIGGER_DEFAULT);
}

PVRSRV_ERROR SysUninstallDeviceLISR(IMG_HANDLE hLISRData)
{
    return OSUninstallSystemLISR(hLISRData);
}

PVRSRV_ERROR SysDebugInfo(PVRSRV_DEVICE_CONFIG *psDevConfig,
				DUMPDEBUG_PRINTF_FUNC *pfnDumpDebugPrintf,
				void *pvDumpDebugFile)
{
	PVR_UNREFERENCED_PARAMETER(psDevConfig);
	PVR_UNREFERENCED_PARAMETER(pfnDumpDebugPrintf);
	PVR_UNREFERENCED_PARAMETER(pvDumpDebugFile);
	return PVRSRV_OK;
}

/******************************************************************************
 End of file (sysconfig.c)
******************************************************************************/

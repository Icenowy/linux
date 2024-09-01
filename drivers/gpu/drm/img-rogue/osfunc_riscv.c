/*************************************************************************/ /*!
@File
@Title          RISC-V specific OS functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Processor specific OS functions
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

#include <linux/version.h>
#include <linux/dma-mapping.h>
//#include <linux/uaccess.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include "img_defs.h"
#include "osfunc.h"
#include "pvr_debug.h"
#include "cache_ops.h"

#if 0
#define __enable_user_access()							\
	__asm__ __volatile__ ("csrs sstatus, %0" : : "r" (SR_SUM) : "memory")
#define __disable_user_access()							\
	__asm__ __volatile__ ("csrc sstatus, %0" : : "r" (SR_SUM) : "memory")
#endif

		//asm volatile (".long 0x0275000b"); /* dcache.civa a0 */
		//asm volatile (".long 0x0255000b"); /* dcache.cva a0 */
#define sync_is()	asm volatile (".long 0x01b0000b")
static void riscv_dma_wbinv_range(unsigned long start, unsigned long end)
{
//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,168))
//#endif
	register unsigned long i asm("a0") = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile (".long 0x02b5000b"); /* dcache.civa a0 */

	sync_is();

//#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,4,168))
//#endif
}

static void riscv_dma_wb_range(unsigned long start, unsigned long end)
{
	register unsigned long i asm("a0") = start & ~(L1_CACHE_BYTES - 1);

	for (; i < end; i += L1_CACHE_BYTES)
		asm volatile (".long 0x0295000b"); /* dcache.cva a0 */

	sync_is();
}

static inline void begin_user_mode_access(void)
{
}

static inline void end_user_mode_access(void)
{
}

static inline void FlushRange(void *pvRangeAddrStart,
							  void *pvRangeAddrEnd,
							  PVRSRV_CACHE_OP eCacheOp)
{
//	IMG_UINT32 ui32CacheLineSize = OSCPUCacheAttributeSize(OS_CPU_CACHE_ATTRIBUTE_LINE_SIZE);
	IMG_BYTE *pbStart = pvRangeAddrStart;
	IMG_BYTE *pbEnd = pvRangeAddrEnd;
//	IMG_BYTE *pbBase;

	//PVR_DPF((PVR_DBG_WARNING, "%s:&&&&&&%d %lx, %lx %x", __func__, __LINE__, (unsigned long)pbStart, (unsigned long)pbEnd, (uint32_t)eCacheOp));
    __enable_user_access();
	switch (eCacheOp)
	{
		case PVRSRV_CACHE_OP_CLEAN:
            riscv_dma_wb_range((unsigned long)pbStart, (unsigned long)pbEnd);
			break;

		case PVRSRV_CACHE_OP_INVALIDATE:
		case PVRSRV_CACHE_OP_FLUSH:
//	        PVR_DPF((PVR_DBG_WARNING, "%s:&&&&&&%d %lx, %lx %x", __func__, __LINE__, (unsigned long)pbStart, (unsigned long)pbEnd, (uint32_t)eCacheOp));
            riscv_dma_wbinv_range((unsigned long)pbStart, (unsigned long)pbEnd);
	        //PVR_DPF((PVR_DBG_WARNING, "%s:&&&&&&%d %lx, %lx %x", __func__, __LINE__, (unsigned long)pbStart, (unsigned long)pbEnd, (uint32_t)eCacheOp));
			break;

		default:
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Cache maintenance operation type %d is invalid",
					__func__, eCacheOp));
			break;
	}

    __disable_user_access();
	/*
	  On arm64, the TRM states in D5.8.1 (data and unified caches) that if cache
	  maintenance is performed on a memory location using a VA, the effect of
	  that cache maintenance is visible to all VA aliases of the physical memory
	  location. So here it's quicker to issue the machine cache maintenance
	  instruction directly without going via the Linux kernel DMA framework as
	  this is sufficient to maintain the CPU d-caches on arm64.
	 */

//	begin_user_mode_access();
//
//	pbEnd = (IMG_BYTE *) PVR_ALIGN((uintptr_t)pbEnd, (uintptr_t)ui32CacheLineSize);
//	for (pbBase = pbStart; pbBase < pbEnd; pbBase += ui32CacheLineSize)
//	{
//		switch (eCacheOp)
//		{
//			case PVRSRV_CACHE_OP_CLEAN:
//                asm volatile ("dcache.cva %0" : : "r"(pbBase));
//				break;
//
//			case PVRSRV_CACHE_OP_INVALIDATE:
//				asm volatile ("dcache.iva %0" :: "r" (pbBase));
//				break;
//
//			case PVRSRV_CACHE_OP_FLUSH:
//				asm volatile ("dcache.civa %0" :: "r" (pbBase));
//				break;
//
//			default:
//				PVR_DPF((PVR_DBG_ERROR,
//						"%s: Cache maintenance operation type %d is invalid",
//						__func__, eCacheOp));
//				break;
//		}
//	}
//
//	end_user_mode_access();
}
void OSCPUCacheFlushRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
							void *pvVirtStart,
							void *pvVirtEnd,
							IMG_CPU_PHYADDR sCPUPhysStart,
							IMG_CPU_PHYADDR sCPUPhysEnd)
{
	struct device *dev;

#if 0
	if (pvVirtStart)
	{
	    PVR_DPF((PVR_DBG_WARNING, "%s:********&&&&&&%d %lx %lx", __func__, __LINE__, (unsigned long)sCPUPhysStart.uiAddr, (unsigned long)sCPUPhysEnd.uiAddr));
		FlushRange(pvVirtStart, pvVirtEnd, PVRSRV_CACHE_OP_FLUSH);
		return;
	}
#else
	    //PVR_DPF((PVR_DBG_WARNING, "%s:********&&&&&&%d", __func__, __LINE__));
		FlushRange((void *)(sCPUPhysStart.uiAddr), (void *)(sCPUPhysEnd.uiAddr), PVRSRV_CACHE_OP_FLUSH);
        return;
#endif

	dev = psDevNode->psDevConfig->pvOSDevice;

	if (dev)
	{
	    PVR_DPF((PVR_DBG_WARNING, "%s:********&&&&&&%d", __func__, __LINE__));
		dma_sync_single_for_device(dev, sCPUPhysStart.uiAddr,
								   sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr,
								   DMA_TO_DEVICE);
		dma_sync_single_for_cpu(dev, sCPUPhysStart.uiAddr,
								sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr,
								DMA_FROM_DEVICE);
	}
	else
	{
		/*
		 * Allocations done prior to obtaining device pointer may
		 * affect in cache operations being scheduled.
		 *
		 * Ignore operations with null device pointer.
		 * This prevents crashes on newer kernels that don't return dummy ops
		 * when null pointer is passed to get_dma_ops.
		 *
		 */

		/* Don't spam on nohw */
#if !defined(NO_HARDWARE)
		PVR_DPF((PVR_DBG_WARNING, "Cache operation cannot be completed!"));
#endif
	}

	/*
	 * RISC-V cache maintenance mechanism is not part of the core spec.
	 * This leaves the actual mechanism of action to an implementer.
	 * Here we let the system layer decide how maintenance is done.
	 */
//	if (psDevNode->psDevConfig->pfnHostCacheMaintenance)
//	{
//		psDevNode->psDevConfig->pfnHostCacheMaintenance(
//				psDevNode->psDevConfig->hSysData,
//				PVRSRV_CACHE_OP_FLUSH,
//				pvVirtStart,
//				pvVirtEnd,
//				sCPUPhysStart,
//				sCPUPhysEnd);
//
//	}
//#if !defined(NO_HARDWARE)
//	else
//	{
//		PVR_DPF((PVR_DBG_WARNING,
//		         "%s: System doesn't implement cache maintenance. Skipping!",
//		         __func__));
//	}
//#endif
}

void OSCPUCacheCleanRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
							void *pvVirtStart,
							void *pvVirtEnd,
							IMG_CPU_PHYADDR sCPUPhysStart,
							IMG_CPU_PHYADDR sCPUPhysEnd)
{
	struct device *dev;

#if 0
	if (pvVirtStart)
	{
	    PVR_DPF((PVR_DBG_WARNING, "%s:****&&&&&&%d", __func__, __LINE__));
		FlushRange(pvVirtStart, pvVirtEnd, PVRSRV_CACHE_OP_CLEAN);
		return;
	}
#else
		FlushRange((void *)(sCPUPhysStart.uiAddr), (void *)(sCPUPhysEnd.uiAddr), PVRSRV_CACHE_OP_CLEAN);
        return;
#endif


	dev = psDevNode->psDevConfig->pvOSDevice;

	if (dev)
	{
	    PVR_DPF((PVR_DBG_WARNING, "%s:******&&&&&&%d", __func__, __LINE__));
		dma_sync_single_for_device(dev, sCPUPhysStart.uiAddr,
								   sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr,
								   DMA_TO_DEVICE);
	}
	else
	{
		/*
		 * Allocations done prior to obtaining device pointer may
		 * affect in cache operations being scheduled.
		 *
		 * Ignore operations with null device pointer.
		 * This prevents crashes on newer kernels that don't return dummy ops
		 * when null pointer is passed to get_dma_ops.
		 *
		 */


		/* Don't spam on nohw */
#if !defined(NO_HARDWARE)
		PVR_DPF((PVR_DBG_WARNING, "Cache operation cannot be completed!"));
#endif
	}

	/*
	 * RISC-V cache maintenance mechanism is not part of the core spec.
	 * This leaves the actual mechanism of action to an implementer.
	 * Here we let the system layer decide how maintenance is done.
	 */
//	if (psDevNode->psDevConfig->pfnHostCacheMaintenance)
//	{
//		psDevNode->psDevConfig->pfnHostCacheMaintenance(
//				psDevNode->psDevConfig->hSysData,
//				PVRSRV_CACHE_OP_CLEAN,
//				pvVirtStart,
//				pvVirtEnd,
//				sCPUPhysStart,
//				sCPUPhysEnd);
//
//	}
//#if !defined(NO_HARDWARE)
//	else
//	{
//		PVR_DPF((PVR_DBG_WARNING,
//		         "%s: System doesn't implement cache maintenance. Skipping!",
//		         __func__));
//	}
//#endif
}

void OSCPUCacheInvalidateRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
								 void *pvVirtStart,
								 void *pvVirtEnd,
								 IMG_CPU_PHYADDR sCPUPhysStart,
								 IMG_CPU_PHYADDR sCPUPhysEnd)
{
	struct device *dev;

#if 0
	if (pvVirtStart)
	{
	    PVR_DPF((PVR_DBG_WARNING, "%s:**&&&&&&%d", __func__, __LINE__));
		FlushRange(pvVirtStart, pvVirtEnd, PVRSRV_CACHE_OP_INVALIDATE);
		return;
	}
#else
		FlushRange((void *)(sCPUPhysStart.uiAddr), (void *)(sCPUPhysEnd.uiAddr), PVRSRV_CACHE_OP_INVALIDATE);
        return;
#endif


	dev = psDevNode->psDevConfig->pvOSDevice;

	if (dev)
	{
	    PVR_DPF((PVR_DBG_WARNING, "%s:***&&&&&&%d", __func__, __LINE__));
		dma_sync_single_for_cpu(dev, sCPUPhysStart.uiAddr,
								sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr,
								DMA_FROM_DEVICE);
	}
	else
	{
		/*
		 * Allocations done prior to obtaining device pointer may
		 * affect in cache operations being scheduled.
		 *
		 * Ignore operations with null device pointer.
		 * This prevents crashes on newer kernels that don't return dummy ops
		 * when null pointer is passed to get_dma_ops.
		 *
		 */

		/* Don't spam on nohw */
#if !defined(NO_HARDWARE)
		PVR_DPF((PVR_DBG_WARNING, "Cache operation cannot be completed!"));
#endif
	}
	/*
	 * RISC-V cache maintenance mechanism is not part of the core spec.
	 * This leaves the actual mechanism of action to an implementer.
	 * Here we let the system layer decide how maintenance is done.
	 */
//	if (psDevNode->psDevConfig->pfnHostCacheMaintenance)
//	{
//		psDevNode->psDevConfig->pfnHostCacheMaintenance(
//				psDevNode->psDevConfig->hSysData,
//				PVRSRV_CACHE_OP_INVALIDATE,
//				pvVirtStart,
//				pvVirtEnd,
//				sCPUPhysStart,
//				sCPUPhysEnd);
//
//	}
//#if !defined(NO_HARDWARE)
//	else
//	{
//		PVR_DPF((PVR_DBG_WARNING,
//		         "%s: System doesn't implement cache maintenance. Skipping!",
//		         __func__));
//	}
//#endif
}

OS_CACHE_OP_ADDR_TYPE OSCPUCacheOpAddressType(void)
{
	/*
	 * Need to obtain psDevNode here and do the following:
	 *
	 * OS_CACHE_OP_ADDR_TYPE eOpAddrType =
	 *	psDevNode->psDevConfig->bHasPhysicalCacheMaintenance ?
	 *		OS_CACHE_OP_ADDR_TYPE_PHYSICAL : OS_CACHE_OP_ADDR_TYPE_VIRTUAL;
	 *
	 * Return BOTH for now on.
	 *
	 */
	//return OS_CACHE_OP_ADDR_TYPE_BOTH;
	return OS_CACHE_OP_ADDR_TYPE_PHYSICAL;
}

void OSUserModeAccessToPerfCountersEn(void)
{
#if !defined(NO_HARDWARE)
	PVR_DPF((PVR_DBG_WARNING, "%s: Not implemented!", __func__));
//	PVR_ASSERT(0);
#endif
}

IMG_BOOL OSIsWriteCombineUnalignedSafe(void)
{
#if !defined(NO_HARDWARE)
	//PVR_DPF((PVR_DBG_WARNING,
	//         "%s: Not implemented (assuming false)!",
	//         __func__));
	//PVR_ASSERT(0);
	//return IMG_FALSE;
	return IMG_TRUE;
#else
	return IMG_TRUE;
#endif
}

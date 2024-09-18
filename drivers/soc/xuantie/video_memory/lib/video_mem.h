/*
 * Copyright (c) 2021-2022 Alibaba Group. All rights reserved.
 * License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#ifndef _VIDEO_MEM_H_
#define _VIDEO_MEM_H_

#ifdef __cplusplus
extern "C" {
#endif

/* No special needs. */
#define VMEM_FLAG_NONE                  0x00000000
/* Physical contiguous. */
#define VMEM_FLAG_CONTIGUOUS            0x00000001
/* Physical non contiguous. */
#define VMEM_FLAG_NON_CONTIGUOUS        0x00000002
/* Need 32bit address. */
#define VMEM_FLAG_4GB_ADDR              0x00000004
/* CMA priority */
#define VMEM_FLAG_CMA                   0x00000008
/* Use VI reserved memory */
#define VMEM_FLAG_VI                    0x00000010
/* Buffer enable cache */
#define VMEM_FLAG_ENABLE_CACHE          0x00000020

/* Alloc rsvmem pool region id should be 0~15 */
#define MAX_REGION_ID 15

#define SET_ALLOC_FLAG_REGION(flag, region_id) (flag & 0x00ffffff) | (region_id << 24)
#define GET_ALLOC_FLAG_REGION(flag)            (flag >> 24)


typedef enum _VmemStatus
{
    VMEM_STATUS_OK = 0,
    VMEM_STATUS_ERROR = -1,     /* general error */
    VMEM_STATUS_NO_MEMORY = -2, /* not enough memory to allocate buffer */
} VmemStatus;

typedef struct _VmemParams
{
    int size;
    int flags;
    unsigned long phy_address;
    void *vir_address;
    int fd;
} VmemParams;

typedef enum{
    VEME_CACHE_DIR_TO_DEV = 0,
    VEME_CACHE_DIR_FROM_DEV,
    VEME_CACHE_DIR_INVALID,
}VmemCacheDir;

VmemStatus VMEM_create(void **vmem);
VmemStatus VMEM_allocate(void *vmem, VmemParams *params);
VmemStatus VMEM_mmap(void *vmem, VmemParams *params);
VmemStatus VMEM_free(void *vmem, VmemParams *params);
VmemStatus VMEM_destroy(void *vmem);

VmemStatus VMEM_export(void *vmem, VmemParams *params);
VmemStatus VMEM_import(void *vmem, VmemParams *params);
VmemStatus VMEM_release(void *vmem, VmemParams *params);

VmemStatus VMEM_flush_cache(void *vmem, VmemParams *params,VmemCacheDir dir);

#ifdef __cplusplus
}
#endif

#endif /* !_VIDEO_MEM_H_ */

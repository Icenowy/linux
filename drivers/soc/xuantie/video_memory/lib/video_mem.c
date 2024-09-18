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

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include "video_memory.h"
#include "video_mem.h"

#define VMEM_PRINT(level, ...) \
    { \
        if (log_level >= VMEM_LOG_##level) \
        { \
            printf("VMEM[%d] %s: ", pid, #level); \
            printf(__VA_ARGS__); \
        } \
    }

#define VMEM_LOGE(...) VMEM_PRINT(ERROR, __VA_ARGS__)
#define VMEM_LOGW(...) VMEM_PRINT(WARNING, __VA_ARGS__)
#define VMEM_LOGI(...) VMEM_PRINT(INFO, __VA_ARGS__)
#define VMEM_LOGD(...) VMEM_PRINT(DEBUG, __VA_ARGS__)
#define VMEM_LOGT(...) VMEM_PRINT(TRACE, __VA_ARGS__)

typedef enum _VmemLogLevel
{
    VMEM_LOG_QUIET = 0,
    VMEM_LOG_ERROR,
    VMEM_LOG_WARNING,
    VMEM_LOG_INFO,
    VMEM_LOG_DEBUG,
    VMEM_LOG_TRACE,
    VMEM_LOG_MAX
} VmemLogLevel;

typedef struct _VmemContext
{
    int fd_alloc;

} VmemContext;

int log_level = VMEM_LOG_ERROR;
int pid = 0;

static int getLogLevel();

VmemStatus
VMEM_create(void **vmem)
{
    VmemContext *ctx = NULL;

    log_level = getLogLevel();
    pid = getpid();

    if (vmem == NULL)
        return VMEM_STATUS_ERROR;

    ctx = (VmemContext *)malloc(sizeof(*ctx));
    if (ctx == NULL)
        return VMEM_STATUS_NO_MEMORY;
    *vmem = (void *)ctx;

    ctx->fd_alloc = open("/dev/vidmem", O_RDWR);
    if (ctx->fd_alloc == -1)
    {
        VMEM_LOGE("Failed to open /dev/vidmem\n");
        return VMEM_STATUS_ERROR;
    }

    return VMEM_STATUS_OK;
}

VmemStatus
VMEM_allocate(void *vmem, VmemParams *params)
{
    VmemContext *ctx = NULL;
    VidmemParams p;

    if (vmem == NULL || params == NULL)
        return VMEM_STATUS_ERROR;

    ctx = (VmemContext *)vmem;
    params->phy_address = 0;
    params->vir_address = NULL;
    params->fd = -1;

    memset(&p, 0, sizeof(p));
    p.size = params->size;
    p.flags = params->flags;
    ioctl(ctx->fd_alloc, MEMORY_IOC_ALLOCATE, &p);
    if (p.bus_address == 0)
    {
        VMEM_LOGE("Failed to allocate memory\n");
        return VMEM_STATUS_NO_MEMORY;
    }

    params->phy_address = p.bus_address;
    VMEM_LOGI("Allocated %d bytes, phy addr 0x%lx\n",
            params->size, params->phy_address);

    return VMEM_STATUS_OK;
}

VmemStatus
VMEM_mmap(void *vmem, VmemParams *params)
{
    VmemContext *ctx = NULL;
    void *vir_addr;

    if (vmem == NULL || params == NULL)
        return VMEM_STATUS_ERROR;

    if (params->vir_address != NULL)
        return VMEM_STATUS_OK;

    ctx = (VmemContext *)vmem;
    int fd = params->fd > 0 ? params->fd : ctx->fd_alloc;
    unsigned int offset = params->fd > 0 ? 0 : params->phy_address;
    vir_addr = mmap(0, params->size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fd, offset);
    if (vir_addr == MAP_FAILED)
    {
        VMEM_LOGE("Failed to mmap physical address: 0x%lx, using fd %d\n",
                params->phy_address, fd);
        return VMEM_STATUS_ERROR;
    }

    params->vir_address = vir_addr;
    VMEM_LOGI("Mapped phy addr 0x%lx to vir addr %p, size %d\n",
            params->phy_address, params->vir_address, params->size);

    return VMEM_STATUS_OK;
}

VmemStatus
VMEM_free(void *vmem, VmemParams *params)
{
    VmemContext *ctx = NULL;
    VidmemParams p;

    if (vmem == NULL || params == NULL)
        return VMEM_STATUS_ERROR;

    ctx = (VmemContext *)vmem;

    VMEM_LOGI("Free virt addr %p, phy addr 0x%lx, size %d\n",
            params->vir_address, params->phy_address, params->size);
    if (params->vir_address != MAP_FAILED && params->vir_address != NULL)
        munmap(params->vir_address, params->size);
    params->vir_address = NULL;

    if (params->phy_address != 0)
    {
        memset(&p, 0, sizeof(p));
        p.bus_address = params->phy_address;
        ioctl(ctx->fd_alloc, MEMORY_IOC_FREE, &p);
        params->phy_address = 0;
    }

    return VMEM_STATUS_OK;
}

VmemStatus
VMEM_destroy(void *vmem)
{
    if (vmem != NULL)
    {
        VmemContext *ctx = (VmemContext *)vmem;
        if (ctx->fd_alloc != -1)
            close(ctx->fd_alloc);

        free(vmem);
    }

    return VMEM_STATUS_OK;
}

VmemStatus
VMEM_export(void *vmem, VmemParams *params)
{
    VmemContext *ctx = NULL;
    VidmemParams p;

    if (vmem == NULL || params == NULL)
        return VMEM_STATUS_ERROR;
    ctx = (VmemContext *)vmem;

    memset(&p, 0, sizeof(p));
    p.bus_address = params->phy_address;
    p.flags = O_RDWR;
    ioctl(ctx->fd_alloc, MEMORY_IOC_DMABUF_EXPORT, &p);
    if (p.fd < 0) {
        VMEM_LOGE("Failed to export memory\n");
        return VMEM_STATUS_ERROR;
    }

    params->fd = p.fd;
    VMEM_LOGI("Exported phy addr 0x%lx to fd %d, size %d\n",
            params->phy_address, params->fd, params->size);

    return VMEM_STATUS_OK;
}

VmemStatus
VMEM_import(void *vmem, VmemParams *params)
{
    VmemContext *ctx = NULL;
    VidmemParams p;

    if (vmem == NULL || params == NULL)
        return VMEM_STATUS_ERROR;
    ctx = (VmemContext *)vmem;

    memset(&p, 0, sizeof(p));
    p.fd = params->fd;
    ioctl(ctx->fd_alloc, MEMORY_IOC_DMABUF_IMPORT, &p);
    if (p.bus_address == 0 || p.size == 0) {
        VMEM_LOGE("Failed to import memory\n");
        return VMEM_STATUS_ERROR;
    }

    params->phy_address = p.bus_address;
    params->size = p.size;
    VMEM_LOGI("Imported fd %d to phy addr 0x%lx, size %d\n",
            params->fd, params->phy_address, params->size);

    return VMEM_STATUS_OK;
}

VmemStatus
VMEM_release(void *vmem, VmemParams *params)
{
    VmemContext *ctx = NULL;

    if (vmem == NULL || params == NULL)
        return VMEM_STATUS_ERROR;
    ctx = (VmemContext *)vmem;

    VMEM_LOGI("Released imported phy addr 0x%lx, fd %d, size %d\n",
            params->phy_address, params->fd, params->size);
    if (params->vir_address != MAP_FAILED && params->vir_address != NULL)
        munmap(params->vir_address, params->size);
    params->vir_address = NULL;

    if (params->phy_address != 0) {
        VidmemParams p;
        memset(&p, 0, sizeof(p));
        p.bus_address = params->phy_address;
        ioctl(ctx->fd_alloc, MEMORY_IOC_DMABUF_RELEASE, &p);
        params->phy_address = 0;
    }

    return VMEM_STATUS_OK;
}


VmemStatus
VMEM_flush_cache(void *vmem, VmemParams *params,VmemCacheDir dir)
{
    VmemContext *ctx = NULL;
    VidmemParams p;
    int ret;
    if (vmem == NULL || params == NULL)
        return VMEM_STATUS_ERROR;

    ctx = (VmemContext *)vmem;

    VMEM_LOGI("Flush phy addr 0x%lx, size %d,dir:%d\n",
                      params->phy_address, params->size,dir);

    if (params->phy_address != 0)
    {
        memset(&p, 0, sizeof(p));
        p.bus_address = params->phy_address;
        if(dir == VEME_CACHE_DIR_TO_DEV)
        {
            ret = ioctl(ctx->fd_alloc, MEMORY_IOC_DMABUF_FLUSH_CACHE, &p);
        }else if(dir == VEME_CACHE_DIR_FROM_DEV)
        {
            ret = ioctl(ctx->fd_alloc, MEMORY_IOC_DMABUF_INVALID_CACHE, &p);
        }
        else
        {
            return VMEM_STATUS_ERROR;
        }

        if(ret)
        {
            VMEM_LOGE("fail ret %d\n",ret);
            return VMEM_STATUS_ERROR;
        }
        return VMEM_STATUS_OK;
    }


    return VMEM_STATUS_ERROR;
}

static int getLogLevel()
{
    char *env = getenv("VMEM_LOG_LEVEL");
    if (env == NULL)
        return VMEM_LOG_ERROR;
    else
    {
        int level = atoi(env);
        if (level >= VMEM_LOG_MAX || level < VMEM_LOG_QUIET)
            return VMEM_LOG_ERROR;
        else
            return level;
    }
}

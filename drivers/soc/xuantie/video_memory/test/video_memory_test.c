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
#include "video_mem.h"

typedef enum MemType
{
    MEM_TYPE_CONTIGUOUS,
    MEM_TYPE_NONCONTIGUOUS,
    MEM_TYPE_CMA,
    MEM_TYPE_VI,
    MEM_TYPE_MAX
} MemType;

int alloc_flags[MEM_TYPE_MAX] =
{
    VMEM_FLAG_CONTIGUOUS | VMEM_FLAG_4GB_ADDR,
    VMEM_FLAG_NON_CONTIGUOUS,
    VMEM_FLAG_CMA,
    VMEM_FLAG_VI
};

int alloc_num[MEM_TYPE_MAX] = {3, 3, 3, 3};

void printUsage(char *name)
{
    printf(" \
        Usage: %s [buf_size|-h]\n\
            buf_size:   buffer size to be allocated, in unit of 4K pages\n\
            -h:         print this message\n",
            name);
}

int main(int argc, char **argv)
{
    int fd_alloc = -1;
    int size = 1920*1080*3/2;
    int pgsize = getpagesize();
    void *vmem = NULL;
    VmemParams *vmparams[MEM_TYPE_MAX] = {0};
    int err = 0;

    if (argc > 1)
    {
        if (strcmp(argv[1], "-h") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        else
            size = atoi(argv[1]) * pgsize;
    }

    if (size <= 0)
    {
        printf("ERROR: invalid size: %d\n", size);
        printUsage(argv[0]);
        return -1;
    }

    do
    {
        if (VMEM_create(&vmem) != VMEM_STATUS_OK)
            break;

        VmemParams imp_params;
        for (int type = 0; type < MEM_TYPE_MAX; type++)
        {
            vmparams[type] = malloc(sizeof(VmemParams)*alloc_num[type]);
            if (vmparams[type] == NULL)
            {
                printf("ERROR: Failed to allocate VmemParams\n");
                err = 1;
                break;
            }

            for (int i = 0; i < alloc_num[type]; i++)
            {
                VmemParams *params = &vmparams[type][i];
                memset(params, 0, sizeof(*params));
                params->size = size;
                params->flags = alloc_flags[type];

                int vi_rsvmem_pool_region_id = i;   // only for type == MEM_TYPE_VI
                if (type == MEM_TYPE_VI)
                {
                    params->flags = SET_ALLOC_FLAG_REGION(params->flags, vi_rsvmem_pool_region_id);
                }

                if (VMEM_allocate(vmem, params) != VMEM_STATUS_OK)
                {
                    if (type == MEM_TYPE_VI)
                        printf("ERROR: Failed to allocate memory type %d, region_id=%d\n",
                               type, vi_rsvmem_pool_region_id);
                    else
                        printf("ERROR: Failed to allocate memory type %d\n", type);
                    break;
                }
                if (VMEM_mmap(vmem, params) != VMEM_STATUS_OK)
                {
                    printf("ERROR: Failed to mmap busAddress: 0x%lx\n",
                            params->phy_address);
                    err = 1;
                    break;
                }
                if (VMEM_export(vmem, params) != VMEM_STATUS_OK)
                {
                    printf("ERROR: Failed to export buffer: 0x%lx\n",
                            params->phy_address);
                    err = 1;
                    break;
                }

                printf("Allocated buffer %d of type %d at paddr 0x%lx vaddr %p size %d fd %d\n",
                    i, type, params->phy_address, params->vir_address, size, params->fd);

                memset(&imp_params, 0, sizeof(imp_params));
                imp_params.fd = params->fd;
                if (VMEM_import(vmem, &imp_params) != VMEM_STATUS_OK)
                {
                    printf("ERROR: Failed to import fd %d\n", params->fd);
                    err = 1;
                    break;
                }

                printf("Imported fd %d: paddr 0x%lx vaddr %p size %d\n",
                    params->fd, params->phy_address, params->vir_address, size);
                VMEM_release(vmem, &imp_params);
            }

            if (err)
                break;
        }
    } while (0);

    for (int type = 0; type < MEM_TYPE_MAX; type++)
    {
        for (int i = 0; i < alloc_num[type]; i++)
        {
            VmemParams *params = &vmparams[type][i];
            VMEM_free(vmem, params);
            memset(params, 0, sizeof(*params));
        }

        if (vmparams[type])
            free(vmparams[type]);
    }

    VMEM_destroy(vmem);

    return 0;
}


/*
 * Copyright (C) 2021 - 2022  Alibaba Group. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2021 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2021 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/mman.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/genalloc.h>
#include <linux/version.h>
#include "video_memory.h"
#include "rsvmem_pool.h"


//#define VIDMEM_DMA_MAP
#define DISCRETE_PAGES 0
//#define VIDMEM_DEBUG


#define IS_ERROR(status)         (status > 0)

/*******************************************************************************
**
**  ONERROR
**
**      Jump to the error handler in case there is an error.
**
**  ASSUMPTIONS:
**
**      'status' variable of int type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _ONERROR(prefix, func) \
    do \
    { \
        status = func; \
        if (IS_ERROR(status)) \
        { \
            goto OnError; \
        } \
    } \
    while (false)

#define ONERROR(func)           _ONERROR(, func)

/*******************************************************************************
**
**  ERR_BREAK
**
**      Executes a break statement on error.
**
**  ASSUMPTIONS:
**
**      'status' variable of int type must be defined.
**
**  ARGUMENTS:
**
**      func    Function to evaluate.
*/
#define _ERR_BREAK(prefix, func){ \
    status = func; \
    if (IS_ERROR(status)) \
    { \
        break; \
    } \
    do { } while (false); \
    }

#define ERR_BREAK(func)         _ERR_BREAK(, func)

/*******************************************************************************
**
**  VERIFY_ARGUMENT
**
**      Assert if an argument does not apply to the specified expression.  If
**      the argument evaluates to false, EINVAL will be
**      returned from the current function.  In retail mode this macro does
**      nothing.
**
**  ARGUMENTS:
**
**      arg     Argument to evaluate.
*/
#define _VERIFY_ARGUMENT(prefix, arg) \
    do \
    { \
        if (!(arg)) \
        { \
            return EINVAL; \
        } \
    } \
    while (false)
#define VERIFY_ARGUMENT(arg)    _VERIFY_ARGUMENT(, arg)


#define VM_FLAGS (VM_IO | VM_DONTCOPY | VM_DONTEXPAND | VM_DONTDUMP)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,8,0)
#define current_mm_mmap_sem current->mm->mmap_lock
#else
#define current_mm_mmap_sem current->mm->mmap_sem
#endif

#define GetPageCount(size, offset)     ((((size) + ((offset) & ~PAGE_MASK)) + PAGE_SIZE - 1) >> PAGE_SHIFT)

#ifndef VIDMEM_DEBUG
#define DEBUG_PRINT(...) \
  do {                     \
        pr_debug(__VA_ARGS__);\
  } while (0)
#else
#undef DEBUG_PRINT
#define DEBUG_PRINT(...) pr_info(__VA_ARGS__)
#endif


struct mem_block
{
    int contiguous;
    size_t size;
    size_t numPages;
    struct dma_buf *dmabuf;
    struct dma_buf_attachment * attachment;
    struct sg_table           * sgt;
    unsigned long             * pagearray;
    struct vm_area_struct     * vma;
    bool is_cma;
    bool is_vi_mem;
    bool cache_en;
    void *va;
    struct file *filp;
    union
    {
        /* Pointer to a array of pages. */
        struct
        {
            struct page *contiguousPages;
            dma_addr_t dma_addr;
            int rsvmem_pool_region_id;
            int exact;
        };

        struct
        {
            /* Pointer to a array of pointers to page. */
            struct page **nonContiguousPages;

            struct page **Pages1M;
            int numPages1M;
            int *isExact;
        };
    };
};

struct mem_node
{
    struct mem_block memBlk;
    unsigned long busAddr;
    int isImported;
    int isExported;
    struct list_head link;
};

struct file_node
{
    struct list_head memList;
    struct file *filp;
    struct list_head link;
};

static struct list_head fileList;
/* golbal list for exported mem_node */
static struct list_head export_list;
static DEFINE_SPINLOCK(export_mem_lock);

static int vidalloc_major = 0;
static int vidalloc_minor = 0;
static struct device *gdev = NULL;
static struct cdev vidalloc_cdev;
static dev_t vidalloc_devt;
static struct class *vidalloc_class;

static DEFINE_SPINLOCK(mem_lock);
#if 1
static int
getPhysical(
    IN struct mem_block *MemBlk,
    IN unsigned int Offset,
    OUT unsigned long * Physical
    );
void free_memblk_pages(struct mem_block *memBlk);

static struct file_node * find_and_delete_file_node(struct file *filp)
{
    struct file_node *node;
    struct file_node *temp;

    spin_lock(&mem_lock);
    list_for_each_entry_safe(node, temp, &fileList, link)
    {
        if (node->filp == filp)
        {
            list_del(&node->link);
            spin_unlock(&mem_lock);
            return node;
        }
    }
    spin_unlock(&mem_lock);

    return NULL;
}

static struct file_node * get_file_node(struct file *filp)
{
    struct file_node *node;

    spin_lock(&mem_lock);
    list_for_each_entry(node, &fileList, link)
    {
        if (node->filp == filp)
        {
            spin_unlock(&mem_lock);
            return node;
        }
    }
    spin_unlock(&mem_lock);

    return NULL;
}

static struct mem_node * get_export_mem_node(struct file *filp, unsigned long bus_address)
{
    struct mem_node *node;
    spin_lock(&export_mem_lock);
    list_for_each_entry(node, &export_list, link)
    {
        if (node->busAddr == bus_address)
        {
            DEBUG_PRINT("[vidmem] Found export mem node %px, %d pages\n", node);
            spin_unlock(&export_mem_lock);
            return node;
        }
    }
    spin_unlock(&export_mem_lock);
    return NULL;
}

static struct mem_node * get_mem_node(struct file *filp, unsigned long bus_address, int imported)
{
    struct file_node *fnode;
    struct mem_node *node;

    fnode = get_file_node(filp);
    if (NULL == fnode)
    {
        return NULL;
    }

    spin_lock(&mem_lock);
    list_for_each_entry(node, &fnode->memList, link)
    {
        if (node->busAddr == bus_address && node->isImported == imported)
        {
            spin_unlock(&mem_lock);
            return node;
        }
    }
    spin_unlock(&mem_lock);


    return NULL;
}

static int
AllocateMemory(
    IN size_t Bytes,
    OUT void * * Memory
    )
{
    void * memory = NULL;
    int status = 0;

    /* Verify the arguments. */
    VERIFY_ARGUMENT(Bytes > 0);
    VERIFY_ARGUMENT(Memory != NULL);

    if (Bytes > PAGE_SIZE)
    {
        memory = (void *) vmalloc(Bytes);
    }
    else
    {
        memory = (void *) kmalloc(Bytes, GFP_KERNEL | __GFP_NOWARN);
    }

    if (memory == NULL)
    {
        /* Out of memory. */
        ONERROR(ENOMEM);
    }

    /* Return pointer to the memory allocation. */
    *Memory = memory;

OnError:
    /* Return the status. */
    return status;
}

int
static FreeMemory(
    IN void * Memory
    )
{
    /* Verify the arguments. */
    VERIFY_ARGUMENT(Memory != NULL);

    /* Free the memory from the OS pool. */
    if (is_vmalloc_addr(Memory))
    {
        vfree(Memory);
    }
    else
    {
        kfree(Memory);
    }

    /* Success. */
    return 0;
}


static int
GetSGT(
    IN struct mem_block *MemBlk,
    IN size_t Offset,
    IN size_t Bytes,
    OUT void * *SGT
    )
{
    struct page ** pages = NULL;
    struct page ** tmpPages = NULL;
    struct sg_table *sgt = NULL;
    struct mem_block *memBlk = MemBlk;

    int status = 0;
    size_t offset = Offset & ~PAGE_MASK; /* Offset to the first page */
    size_t skipPages = Offset >> PAGE_SHIFT;     /* skipped pages */
    size_t numPages = (PAGE_ALIGN(Offset + Bytes) >> PAGE_SHIFT) - skipPages;
    size_t i;

    if (memBlk->contiguous)
    {
        DEBUG_PRINT("[vidmem] Contiguous memory, %d pages\n", numPages);
        ONERROR(AllocateMemory(sizeof(struct page*) * numPages, (void * *)&tmpPages));
        pages = tmpPages;

        for (i = 0; i < numPages; ++i)
        {
            pages[i] = nth_page(memBlk->contiguousPages, i + skipPages);
        }
    }
    else
    {
        DEBUG_PRINT("[vidmem] Non-contiguous memory, %d pages\n", numPages);
        pages = &memBlk->nonContiguousPages[skipPages];
    }

    ONERROR(AllocateMemory(sizeof(struct sg_table) * numPages, (void * *)&sgt));

    if (sg_alloc_table_from_pages(sgt, pages, numPages, offset, Bytes, GFP_KERNEL) < 0)
    {
        ONERROR(EPERM);
    }

    *SGT = (void *)sgt;

OnError:
    if (tmpPages)
    {
        FreeMemory(tmpPages);
    }

    if (IS_ERROR(status) && sgt)
    {
        FreeMemory(sgt);
    }

    return status;
}


static void invalid_data_cache(IN struct file *filp, IN unsigned long bus_address )
{
    struct mem_block *memBlk = NULL;
    struct mem_node *mnode = NULL;
    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        mnode = get_export_mem_node(filp, bus_address);
        if (NULL == mnode)
            return;
    }

    memBlk = &mnode->memBlk;
    dma_addr_t dma_handle = memBlk->dma_addr;
    size_t size = memBlk->size;
    dma_sync_single_for_cpu(gdev,dma_handle ,memBlk->size,DMA_FROM_DEVICE);

}

static void flush_data_cache(IN struct file *filp, IN unsigned long bus_address )
{
    struct mem_block *memBlk = NULL;
    struct mem_node *mnode = NULL;

    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        mnode = get_export_mem_node(filp, bus_address);
        if (NULL == mnode)
            return;
    }

    memBlk = &mnode->memBlk;
    dma_addr_t dma_handle = memBlk->dma_addr;
    size_t size = memBlk->size;
    dma_sync_single_for_device(gdev,dma_handle ,memBlk->size,DMA_TO_DEVICE);

}

static int
Mmap(
    IN struct mem_block *MemBlk,
    IN size_t skipPages,
    IN size_t numPages,
    IN struct vm_area_struct *vma
    )
{
    struct mem_block *memBlk = MemBlk;
    int status = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
    vma->vm_flags |= VM_FLAGS;
#else
    vm_flags_set(vma,VM_FLAGS);
#endif
    /* Make this mapping write combined. */
    if (!memBlk->cache_en) {
        vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
    }
    DEBUG_PRINT("start remap vm_page_prot:0x%llx,skipPages %d,numPages %d,memBlk->contiguous %d\n",vma->vm_page_prot,skipPages,numPages,memBlk->contiguous);

    /* Now map all the vmalloc pages to this user address. */
    if (memBlk->contiguous)
    {
        if(!numPages)
        {
            pr_err("numPages is zero,addr %llx pfn 0x%x\n",vma->vm_start,page_to_pfn(memBlk->contiguousPages) + skipPages);
            return -1;
        }
        /* map kernel memory to user space.. */
        #if 0
        if (memBlk->is_cma == true) {
            return dma_mmap_coherent(gdev, vma, memBlk->va,
					   memBlk->dma_addr, vma->vm_end - vma->vm_start);
        } else
        #endif
        {
            if (remap_pfn_range(vma,
                                vma->vm_start,
                                page_to_pfn(memBlk->contiguousPages) + skipPages,
                                numPages << PAGE_SHIFT,
                                vma->vm_page_prot) < 0)
            {
                ONERROR(ENOMEM);
            }
        }
    }
    else
    {
        size_t i;
        unsigned long start = vma->vm_start;

        for (i = 0; i < numPages; ++i)
        {
            unsigned long pfn = page_to_pfn(memBlk->nonContiguousPages[i + skipPages]);

            if (remap_pfn_range(vma,
                                start,
                                pfn,
                                PAGE_SIZE,
                                vma->vm_page_prot) < 0)
            {
                ONERROR(ENOMEM);
            }

            start += PAGE_SIZE;
        }
    }

OnError:
    return status;
}

static int
Attach(
    INOUT struct mem_block *MemBlk
    )
{
    int status;
    struct mem_block *memBlk = MemBlk;

    struct dma_buf *dmabuf = memBlk->dmabuf;
    struct sg_table *sgt = NULL;
    struct dma_buf_attachment *attachment = NULL;
    int npages = 0;
    unsigned long *pagearray = NULL;
    int i, j, k = 0;
    struct scatterlist *s;
    unsigned int size = 0;

    if (!dmabuf)
    {
        ONERROR(EFAULT);
    }

    attachment = dma_buf_attach(dmabuf, gdev);

    if (!attachment)
    {
        ONERROR(EFAULT);
    }

    sgt = dma_buf_map_attachment(attachment, DMA_BIDIRECTIONAL);

    if (!sgt)
    {
        ONERROR(EFAULT);
    }

    /* Prepare page array. */
    /* Get number of pages. */
    for_each_sg(sgt->sgl, s, sgt->orig_nents, i)
    {
        npages += (sg_dma_len(s) + PAGE_SIZE - 1) / PAGE_SIZE;
    }

    /* Allocate page array. */
    ONERROR(AllocateMemory(npages * sizeof(*pagearray), (void * *)&pagearray));

    /* Fill page array. */
    for_each_sg(sgt->sgl, s, sgt->orig_nents, i)
    {
        for (j = 0; j < (sg_dma_len(s) + PAGE_SIZE - 1) / PAGE_SIZE; j++)
        {
#ifdef VIDMEM_DMA_MAP
            pagearray[k++] = sg_dma_address(s) + j * PAGE_SIZE;
#else
            pagearray[k++] = page_to_phys(nth_page(sg_page(s), j));
#endif
        }
        size += sg_dma_len(s);
    }

    memBlk->pagearray = pagearray;
    memBlk->attachment = attachment;
    memBlk->sgt = sgt;
    memBlk->numPages = npages;
    memBlk->size = size;
    memBlk->contiguous = (sgt->nents == 1) ? true : false;

    return 0;

OnError:
    if (pagearray)
    {
        FreeMemory(pagearray);
        pagearray = NULL;
    }

    if (sgt)
    {
        dma_buf_unmap_attachment(attachment, sgt, DMA_BIDIRECTIONAL);
    }

    return status;
}

static struct sg_table *_dmabuf_map(struct dma_buf_attachment *attachment,
                                    enum dma_data_direction direction)
{
    struct sg_table *sgt = NULL;
    struct dma_buf *dmabuf = attachment->dmabuf;
    struct mem_block *memBlk = dmabuf->priv;
    int status = 0;

    DEBUG_PRINT("[vidmem] %s\n", __func__);

    do
    {
        ERR_BREAK(GetSGT(memBlk, 0, memBlk->size, (void **)&sgt));

        if (dma_map_sg(attachment->dev, sgt->sgl, sgt->nents, direction) == 0)
        {
            sg_free_table(sgt);
            kfree(sgt);
            sgt = NULL;
            ERR_BREAK(EPERM);
        }
    }
    while (false);

#ifdef VIDMEM_DEBUG
    {
        DEBUG_PRINT("[vidmem] sgt: nents = %u, sgl: page_link = %#lx, offset = %#x, length = %#x, dma_address = %llx\n",
            sgt->nents, sgt->sgl->page_link, sgt->sgl->offset, sgt->sgl->length, sgt->sgl->dma_address);
        int i = 0, j = 0;
        struct scatterlist *s;
        for_each_sg(sgt->sgl, s, sgt->orig_nents, i)
        {
            unsigned long phys = page_to_phys(nth_page(sg_page(s), 0));
            DEBUG_PRINT("[vidmem] %d, %d: 0x%x, %d pages\n",
                i, j, phys, ((sg_dma_len(s) + PAGE_SIZE - 1) / PAGE_SIZE));
        }
    }
#endif

    return sgt;
}

static void _dmabuf_unmap(struct dma_buf_attachment *attachment,
                          struct sg_table *sgt,
                          enum dma_data_direction direction)
{
    DEBUG_PRINT("[vidmem] %s\n", __func__);

    dma_unmap_sg(attachment->dev, sgt->sgl, sgt->nents, direction);

    sg_free_table(sgt);
    FreeMemory(sgt);
}

static int _dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
    struct mem_block *memBlk = dmabuf->priv;
    size_t skipPages = vma->vm_pgoff;
    size_t numPages = PAGE_ALIGN(vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    unsigned long physical = 0;
    int status = 0;

    getPhysical(memBlk, 0, &physical);
    DEBUG_PRINT("[vidmem] %s, %d: Mmap 0x%lx with %ld pages\n", __func__, __LINE__, physical, numPages);

    ONERROR(Mmap(memBlk, skipPages, numPages, vma));

OnError:
    return IS_ERROR(status) ? -EINVAL : 0;
}

static void _dmabuf_release(struct dma_buf *dmabuf)
{
    struct mem_block *memBlk = dmabuf->priv;
    unsigned long physical;
    //struct mem_node *mnode = (mem_node *)memBlk;
    struct mem_node *mnode = container_of(memBlk,struct mem_node,memBlk);
    if (!memBlk)
        return;
    if(!memBlk->filp)
    {
        DEBUG_PRINT("[vidmem] %s, %d: memBlk filp null\n", __func__, __LINE__);
        return;
    }
    DEBUG_PRINT("  %s: remove node\n",__func__);
    //remove node from export gloabl list,
    //Remove it before free mem pages to avoid pages freed but node not deleted due to thread competition
    spin_lock(&export_mem_lock);
    list_del(&mnode->link);
    spin_unlock(&export_mem_lock);

    getPhysical(memBlk, 0, &physical);
    DEBUG_PRINT("[vidmem] %s, %d: free physical %llx,mnode %px\n", __func__, __LINE__,physical,mnode);

    free_memblk_pages(memBlk);

    DEBUG_PRINT("  %s: FreeMemory of node\n",__func__);
    FreeMemory(mnode);
    dmabuf->priv = NULL;
}

static void *_dmabuf_kmap(struct dma_buf *dmabuf, unsigned long offset)
{
    char *kvaddr = NULL;

    return (void *)kvaddr;
}

static void _dmabuf_kunmap(struct dma_buf *dmabuf, unsigned long offset, void *ptr)
{
}

static struct dma_buf_ops _dmabuf_ops =
{
    .map_dma_buf = _dmabuf_map,
    .unmap_dma_buf = _dmabuf_unmap,
    .mmap = _dmabuf_mmap,
    .release = _dmabuf_release,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,6,0)
    .map = _dmabuf_kmap,
    .unmap = _dmabuf_kunmap,
#endif
};


static int
DMABUF_Export(
    IN struct file *filp,
    IN unsigned long bus_address,
    IN signed int Flags,
    OUT signed int *FD
    )
{
    int status = 0;
    struct dma_buf *dmabuf = NULL;
    struct mem_block *memBlk = NULL;
    struct mem_node *mnode = NULL;

    DEBUG_PRINT("[vidmem] Export buffer 0x%lx\n", bus_address);
    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        pr_err("[vidmem] Cannot find mem_node with bus address 0x%lx\n", bus_address);
        ONERROR(EINVAL);
    }

    memBlk = &mnode->memBlk;
    memBlk->filp = filp;
    mnode->isExported = 1;

    dmabuf = memBlk->dmabuf;
    if (dmabuf == NULL)
    {
        size_t bytes = memBlk->size;

        {
            DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
            exp_info.ops = &_dmabuf_ops;
            exp_info.size = bytes;
            exp_info.flags = Flags;
            exp_info.priv = memBlk;
            dmabuf = dma_buf_export(&exp_info);
        }

        if (dmabuf == NULL)
        {
            ONERROR(EFAULT);
        }

        memBlk->dmabuf = dmabuf;
    }

    if (FD)
    {
        int fd = dma_buf_fd(dmabuf, Flags);

        if (fd < 0)
        {
            ONERROR(EIO);
        }

        *FD = fd;
        DEBUG_PRINT("  [vidmem] Export  as fd %d,mnode %px memBlk %px\n", fd,mnode,&mnode->memBlk);
    }

    /*exported buffer remove from list*/
    spin_lock(&mem_lock);
    list_del(&mnode->link);
    spin_unlock(&mem_lock);

    /* add to export gloabl list*/
    spin_lock(&export_mem_lock);
    list_add_tail(&mnode->link,&export_list);
    spin_unlock(&export_mem_lock);
OnError:
    return status;
}

static int
DMABUF_Import(
    IN struct file *filp,
    IN signed int FD,
    OUT unsigned long *bus_address,
    OUT unsigned int *size
    )
{
    int status;

    struct mem_block *memBlk = NULL;
    struct file_node *fnode = NULL;
    struct mem_node *mnode = NULL;

    DEBUG_PRINT("[vidmem] enter %s: fd = 0x%x\n",__func__, FD);
    mnode = kzalloc(sizeof(struct mem_node), GFP_KERNEL | __GFP_NORETRY);

    if (!mnode)
    {
        ONERROR(ENOMEM);
    }

    fnode = get_file_node(filp);

    if (NULL == fnode)
    {
        ONERROR(EINVAL);
    }

    memBlk = &mnode->memBlk;
    memBlk->filp = filp;
    /* Import dma buf handle. */
    memBlk->dmabuf = dma_buf_get(FD);
    if (!memBlk->dmabuf)
    {
        ONERROR(EFAULT);
    }

    ONERROR(Attach(memBlk));

    *bus_address = memBlk->pagearray[0];
    *size = memBlk->size;
    DEBUG_PRINT("[vidmem] Imported FD %d at 0x%lx in size of %ld\n", FD, memBlk->pagearray[0], memBlk->size);

    mnode->busAddr = memBlk->pagearray[0];
    mnode->isImported = 1;
    spin_lock(&mem_lock);
    list_add_tail(&mnode->link, &fnode->memList);
    spin_unlock(&mem_lock);

    return 0;

OnError:
    if (mnode)
    {
        kfree(mnode);
    }


    return status;
}

void
DMABUF_Release(
    IN struct file *filp,
    IN unsigned long bus_address
    )
{
    struct mem_block *memBlk = NULL;
    struct mem_node *mnode = NULL;
    DEBUG_PRINT("[vidmem] enter %s: bus_address 0x%lx\n",__func__, bus_address);
    mnode = get_mem_node(filp, bus_address, 1);
    if (NULL == mnode)
    {
        return;
    }

    memBlk = &mnode->memBlk;

    dma_buf_unmap_attachment(memBlk->attachment, memBlk->sgt, DMA_BIDIRECTIONAL);

    dma_buf_detach(memBlk->dmabuf, memBlk->attachment);

    dma_buf_put(memBlk->dmabuf);

    FreeMemory(memBlk->pagearray);

    DEBUG_PRINT("[vidmem] release bus address at 0x%lx in size of %ld\n", bus_address, memBlk->size);
    spin_lock(&mem_lock);
    list_del(&mnode->link);
    spin_unlock(&mem_lock);
    FreeMemory(mnode);
}

/***************************************************************************\
************************ GFP Allocator **********************************
\***************************************************************************/
static void
NonContiguousFree(
    IN struct page ** Pages,
    IN size_t NumPages
    )
{
    size_t i;

    for (i = 0; i < NumPages; i++)
    {
        __free_page(Pages[i]);
    }

    FreeMemory(Pages);
}

static int
NonContiguousAlloc(
    IN struct mem_block *MemBlk,
    IN size_t NumPages,
    IN unsigned int Gfp
    )
{
    struct page ** pages;
    struct page *p;
    size_t i, size;

    if (NumPages > totalram_pages())
    {
        return ENOMEM;
    }

    size = NumPages * sizeof(struct page *);
    if (AllocateMemory(size, (void * *)&pages))
        return ENOMEM;

    for (i = 0; i < NumPages; i++)
    {
        p = alloc_page(Gfp);

        if (!p)
        {
            pr_err("Failed to allocate non-contiguous memory\n");
            NonContiguousFree(pages, i);
            return ENOMEM;
        }

#if DISCRETE_PAGES
        if (i != 0)
        {
            if (page_to_pfn(pages[i-1]) == page_to_pfn(p)-1)
            {
                /* Replaced page. */
                struct page *l = p;

                /* Allocate a page which is not contiguous to previous one. */
                p = alloc_page(Gfp);

                /* Give replaced page back. */
                __free_page(l);

                if (!p)
                {
                    NonContiguousFree(pages, i);
                    return ENOMEM;
                }
            }
        }
#endif

        pages[i] = p;
    }

    MemBlk->nonContiguousPages = pages;

    return 0;
}

static int
getPhysical(
    IN struct mem_block *MemBlk,
    IN unsigned int Offset,
    OUT unsigned long * Physical
    )
{
    struct mem_block *memBlk = MemBlk;
    unsigned int offsetInPage = Offset & ~PAGE_MASK;
    unsigned int index = Offset / PAGE_SIZE;

    if (memBlk->contiguous)
    {
        *Physical = page_to_phys(nth_page(memBlk->contiguousPages, index));
    }
    else
    {
        *Physical = page_to_phys(memBlk->nonContiguousPages[index]);
    }

    *Physical += offsetInPage;

    return 0;
}

int
GFP_Alloc(
    IN struct file *filp,
    IN unsigned int size,
    IN unsigned int Flags,
    OUT unsigned long *bus_address
    )
{
    int status;
    size_t i = 0;
    unsigned int gfp = GFP_KERNEL | GFP_DMA | __GFP_NOWARN;
    int contiguous = Flags & (ALLOC_FLAG_CONTIGUOUS | ALLOC_FLAG_CMA | ALLOC_FLAG_VI);
    size_t numPages = GetPageCount(size, 0);
    unsigned long physical = 0;

    struct mem_block *memBlk = NULL;
    struct file_node *fnode = NULL;
    struct mem_node *mnode = NULL;

    if ((Flags & ALLOC_FLAG_CMA) && (Flags & ALLOC_FLAG_VI))
    {
        ONERROR(EINVAL);
    }

    mnode = kzalloc(sizeof(struct mem_node), GFP_KERNEL | __GFP_NORETRY);

    if (!mnode)
    {
        ONERROR(ENOMEM);
    }

    fnode = get_file_node(filp);

    if (NULL == fnode)
    {
        ONERROR(EINVAL);
    }

    memBlk = &mnode->memBlk;

    if (Flags & ALLOC_FLAG_4GB_ADDR)
    {
        /* remove __GFP_HIGHMEM bit, add __GFP_DMA32 bit */
        gfp &= ~__GFP_HIGHMEM;
        gfp |= __GFP_DMA32;
    }


    if (Flags & ALLOC_FLAG_ENABLE_CACHE) {
        memBlk->cache_en = true;
    } else {
        memBlk->cache_en = false;
    }

    memBlk->contiguous = contiguous;
    memBlk->numPages = numPages;
    memBlk->size = size;
    memBlk->is_cma = false;
    memBlk->is_vi_mem = false;

    if (contiguous)
    {
        size_t bytes = numPages << PAGE_SHIFT;

        void *addr = NULL;

        if (Flags & ALLOC_FLAG_VI) {
            int region_id = GET_ALLOC_FLAG_REGION(Flags);
            memBlk->dma_addr = rsvmem_pool_alloc(region_id, bytes);
            if (!memBlk->dma_addr)
            {
                ONERROR(ENOMEM);
            }
            memBlk->rsvmem_pool_region_id = region_id;
            memBlk->contiguousPages = (phys_addr_t)memBlk->dma_addr ? phys_to_page((phys_addr_t)memBlk->dma_addr) : NULL;
            memBlk->is_vi_mem = true;
            physical = memBlk->dma_addr;
            goto OnDone;
        }
        else if (Flags & ALLOC_FLAG_CMA) {
            memBlk->va = dma_alloc_coherent(gdev,
                            bytes, &memBlk->dma_addr,
                            GFP_KERNEL | __GFP_NOWARN);
            memBlk->contiguousPages = (phys_addr_t)memBlk->dma_addr ? phys_to_page((phys_addr_t)memBlk->dma_addr) : NULL;
            if (!memBlk->va) {
                return -ENOMEM;
            }
            memBlk->is_cma = true;
            //pr_debug("got cma vir %p phy 0x%x contiguousPages %p\n", memBlk->va, memBlk->dma_addr, memBlk->contiguousPages);
            physical = memBlk->dma_addr;
            goto OnDone;
        }

        addr = alloc_pages_exact(bytes, (gfp & ~__GFP_HIGHMEM) | __GFP_NORETRY);

        memBlk->contiguousPages = addr ? virt_to_page(addr) : NULL;

        if (memBlk->contiguousPages)
        {
            memBlk->exact = true;
        }

        if (memBlk->contiguousPages == NULL)
        {
            int order = get_order(bytes);

            if (order >= MAX_ORDER)
            {
                pr_err("Too big buffer size requested. (order %d >= max %d)\n",
                    order, MAX_ORDER);
                status = ENOMEM;
                goto OnError;
            }

            memBlk->contiguousPages = alloc_pages(gfp, order);
        }

        if (memBlk->contiguousPages == NULL)
        {
            pr_debug("Failed to allocate contiguous memory\n");
            ONERROR(ENOMEM);
        }

#ifdef VIDMEM_DMA_MAP
        memBlk->dma_addr = dma_map_page(gdev,
                memBlk->contiguousPages, 0, numPages * PAGE_SIZE,
                DMA_BIDIRECTIONAL);

        if (dma_mapping_error(gdev, memBlk->dma_addr))
        {
            if (memBlk->exact)
            {
                free_pages_exact(page_address(memBlk->contiguousPages), bytes);
            }
            else
            {
                __free_pages(memBlk->contiguousPages, get_order(bytes));
            }

            ONERROR(ENOMEM);
        }
#endif
    }
    else // non-contiguous pages
    {
        ONERROR(NonContiguousAlloc(memBlk, numPages, gfp));

        memBlk->sgt = kzalloc(sizeof(struct sg_table), GFP_KERNEL | __GFP_NORETRY);
        if (!memBlk->sgt)
        {
            ONERROR(ENOMEM);
        }

        status = sg_alloc_table_from_pages(memBlk->sgt,
                    memBlk->nonContiguousPages, numPages, 0,
                    numPages << PAGE_SHIFT, GFP_KERNEL);
        memBlk->sgt->orig_nents = memBlk->sgt->nents;
        if (status < 0)
        {
            NonContiguousFree(memBlk->nonContiguousPages, numPages);
            ONERROR(ENOMEM);
        }

#ifdef VIDMEM_DMA_MAP
        status = dma_map_sg(gdev, memBlk->sgt->sgl, memBlk->sgt->nents, DMA_BIDIRECTIONAL);
        if (status != memBlk->sgt->nents)
        {
            NonContiguousFree(memBlk->nonContiguousPages, numPages);
            sg_free_table(memBlk->sgt);
            ONERROR(ENOMEM);
        }
#endif
    }

    for (i = 0; i < numPages; i++)
    {
        struct page *page;

        if (contiguous)
        {
            page = nth_page(memBlk->contiguousPages, i);
        }
        else
        {
            page = memBlk->nonContiguousPages[i];
        }

        SetPageReserved(page);
    }

    getPhysical(memBlk, 0, &physical);

OnDone:
    *bus_address = physical;
    mnode->busAddr = physical;
    mnode->isImported = 0;
    spin_lock(&mem_lock);
    list_add_tail(&mnode->link, &fnode->memList);
    DEBUG_PRINT("[vidmem] filp %px add mnode %px to list %px\n",filp,mnode,&fnode->memList);
    spin_unlock(&mem_lock);

    DEBUG_PRINT("[vidmem] Allocated %d bytes (%ld pages) mnode %px at physical address 0x%lx with %d sg table entries\n",
        size, numPages, mnode, physical, contiguous ? 1 : memBlk->sgt->nents);

    return 0;

OnError:
    if (memBlk->sgt)
    {
        kfree(memBlk->sgt);
    }

    if (mnode)
    {
        kfree(mnode);
    }

    return status;
}

void free_memblk_pages(struct mem_block *memBlk)
{
    size_t i;
    struct page * page;
    DEBUG_PRINT("   ##[vidmem] Free %ld pages, contiguous %d\n", memBlk->numPages, memBlk->contiguous);

    if (memBlk->contiguous)
    {
#ifdef VIDMEM_DMA_MAP
        dma_unmap_page(gdev, memBlk->dma_addr,
                memBlk->numPages << PAGE_SHIFT, DMA_FROM_DEVICE);
#endif
    }
    else
    {
#ifdef VIDMEM_DMA_MAP
        dma_unmap_sg(gdev, memBlk->sgt->sgl, memBlk->sgt->nents,
                DMA_FROM_DEVICE);
#endif

        sg_free_table(memBlk->sgt);

        if (memBlk->sgt)
        {
            kfree(memBlk->sgt);
        }
    }

    if (memBlk->is_cma == false && memBlk->is_vi_mem == false) {
        for (i = 0; i < memBlk->numPages; i++)
        {
            if (memBlk->contiguous) {
                page = nth_page(memBlk->contiguousPages, i);
            } else {
	        page = memBlk->nonContiguousPages[i];
	    }

            ClearPageReserved(page);
        }
    }

    if (memBlk->contiguous)
    {
        size_t bytes = memBlk->numPages << PAGE_SHIFT;
        if (memBlk->is_vi_mem == true)
        {
            rsvmem_pool_free(memBlk->rsvmem_pool_region_id, bytes, memBlk->dma_addr);
        }
        else if (memBlk->is_cma == true)
        {
            dma_free_coherent(gdev, bytes,
                    memBlk->va, memBlk->dma_addr);
        }
        else if (memBlk->exact == true)
        {
            free_pages_exact(page_address(memBlk->contiguousPages), memBlk->numPages * PAGE_SIZE);
        }
        else
        {
            __free_pages(memBlk->contiguousPages, get_order(memBlk->numPages * PAGE_SIZE));
        }
    }
    else
    {
        NonContiguousFree(memBlk->nonContiguousPages, memBlk->numPages);
    }
}

void
GFP_Free(
    IN struct file *filp,
    IN unsigned long bus_address
    )
{
    struct mem_block *memBlk = NULL;
    struct mem_node *mnode = NULL;

    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        return;
    }
    //Remove it before free mem pages to avoid pages freed but node not deleted due to thread competition
    spin_lock(&mem_lock);
    DEBUG_PRINT("[vidmem] Free physical address 0x%lx  del mnode %px filp %px\n", mnode->busAddr, mnode,filp);
    list_del(&mnode->link);
    spin_unlock(&mem_lock);

    memBlk = &mnode->memBlk;
    DEBUG_PRINT("[vidmem] Free %ld pages from physical address 0x%lx\n", memBlk->numPages, mnode->busAddr);

    free_memblk_pages(memBlk);


    FreeMemory(mnode);
}

static int
GFP_MapUser(
    IN struct file *filp,
    IN struct vm_area_struct *vma
    )
{
    struct mem_block *memBlk = NULL;
    struct mem_node *mnode = NULL;
    unsigned long bus_address = vma->vm_pgoff * PAGE_SIZE;
    int status = 0;
    bool in_export_list = 0;
    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        mnode = get_export_mem_node(filp, bus_address);
        if (NULL == mnode)
            return EINVAL;
        in_export_list = 1;
    }

    memBlk = &mnode->memBlk;
    DEBUG_PRINT("[vidmem] mnode %px ,Map %ld pages from physical address 0x%lx,in_export_list %d\n", mnode,memBlk->numPages, mnode->busAddr,in_export_list);
    if (Mmap(memBlk, 0, memBlk->numPages, vma))
    {
        return ENOMEM;
    }

    memBlk->vma = vma;


    return status;
}

int vidalloc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    return GFP_MapUser(filp, vma);
}

static long vidalloc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    VidmemParams params;
    int ret = 0;

    if (_IOC_TYPE(cmd) != MEMORY_IOC_MAGIC) return EINVAL;
    if (_IOC_NR(cmd) > MEMORY_IOC_MAXNR) return EINVAL;

    if (_IOC_DIR(cmd) & _IOC_READ)
        ret = !access_ok((void*)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        ret = !access_ok((void*)arg, _IOC_SIZE(cmd));
    if (ret) return EINVAL;

    switch (cmd)
    {
    case MEMORY_IOC_ALLOCATE:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(VidmemParams));
        if (!ret)
        {
            ret = GFP_Alloc(filp, params.size, params.flags, &params.bus_address);
            params.translation_offset = 0;
            if (!ret)
                ret = copy_to_user((VidmemParams *)arg, &params, sizeof(VidmemParams));
        }
        break;
    }
    case MEMORY_IOC_FREE:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(VidmemParams));
        if (!ret)
        {
            GFP_Free(filp, params.bus_address);
            ret = copy_to_user((VidmemParams *)arg, &params, sizeof(VidmemParams));
        }
        break;
    }
    case MEMORY_IOC_DMABUF_EXPORT:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(VidmemParams));
        if (!ret)
        {
            ret = DMABUF_Export(filp, params.bus_address, params.flags, &params.fd);
            if (!ret)
                ret = copy_to_user((VidmemParams *)arg, &params, sizeof(VidmemParams));
        }
        break;
    }
    case MEMORY_IOC_DMABUF_IMPORT:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(VidmemParams));
        if (!ret)
        {
            ret = DMABUF_Import(filp, params.fd, &params.bus_address, &params.size);
            params.translation_offset = 0;
            if (!ret)
                ret = copy_to_user((VidmemParams *)arg, &params, sizeof(VidmemParams));
        }
        break;
    }
    case MEMORY_IOC_DMABUF_RELEASE:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(VidmemParams));
        if (!ret)
        {
            DMABUF_Release(filp, params.bus_address);
            ret = copy_to_user((VidmemParams *)arg, &params, sizeof(VidmemParams));
        }
        break;
    }

    case MEMORY_IOC_DMABUF_FLUSH_CACHE:
        ret = copy_from_user(&params, (void*)arg, sizeof(VidmemParams));
        if (!ret)
        {
            flush_data_cache(filp, params.bus_address);
        }
        break;
    case MEMORY_IOC_DMABUF_INVALID_CACHE:
        ret = copy_from_user(&params, (void*)arg, sizeof(VidmemParams));
        if (!ret)
        {
            invalid_data_cache(filp, params.bus_address);
        }
        break;
    default:
        ret = EINVAL;
    }

    return ret;
}

static int vidalloc_open(struct inode *inode, struct file *filp)
{
    int ret = 0;
    struct file_node *fnode = NULL;

    if (AllocateMemory(sizeof(struct file_node), (void * *)&fnode))
        return ENOMEM;

    fnode->filp = filp;
    INIT_LIST_HEAD(&fnode->memList);
    spin_lock(&mem_lock);
    list_add_tail(&fnode->link, &fileList);
    spin_unlock(&mem_lock);
    pr_debug(" == %s:fnode %px filp %px\n",__func__,fnode,filp);
    return ret;
}

static int vidalloc_release(struct inode *inode, struct file *filp)
{
    struct file_node *fnode = get_file_node(filp);
    struct mem_node *node;
    struct mem_node *temp;

    if (NULL == fnode)
        return EINVAL;
    pr_debug(" -- %s:fnode %px filp %px\n",__func__,fnode,filp);
    list_for_each_entry_safe(node, temp, &fnode->memList, link)
    {
        // this is not expected, memory leak detected!
        pr_debug("vidmem: Found unfreed memory at 0x%lx, isImported = %d isExported = %d\n", node->busAddr, node->isImported,node->isExported);
        if (node->isExported) //let dmabuf release ops free
        {
            continue;
        }
        if (node->isImported)
            DMABUF_Release(filp, node->busAddr);
        else
            GFP_Free(filp, node->busAddr);
    }


    spin_lock(&mem_lock);
    list_del(&fnode->link);
    spin_unlock(&mem_lock);
    FreeMemory(fnode);

    return 0;
}


static struct file_operations vidalloc_fops = {
    .owner= THIS_MODULE,
    .open = vidalloc_open,
    .release = vidalloc_release,
    .unlocked_ioctl = vidalloc_ioctl,
    .mmap = vidalloc_mmap,
    .fasync = NULL,
};
#endif
int vidalloc_probe(struct platform_device *pdev)
{
    int result = 0;

    pr_info("enter %s,ver:1.2A\n",__func__);
#if 1
    gdev = &pdev->dev;
    INIT_LIST_HEAD(&fileList);
    INIT_LIST_HEAD(&export_list);

    result = rsvmem_pool_create(&pdev->dev);
    if (result && result != -ENODEV)
    {
        pr_err("%s: Failed to create reserved memory pool\n", __func__);
        goto err1;
    }

    if (vidalloc_major == 0)
    {
        result = alloc_chrdev_region(&vidalloc_devt, 0, 1, "vidmem");
        if (result != 0)
        {
            pr_err("%s: alloc_chrdev_region error\n", __func__);
            goto err1;
        }
        vidalloc_major = MAJOR(vidalloc_devt);
        vidalloc_minor = MINOR(vidalloc_devt);
    }
    else
    {
        vidalloc_devt = MKDEV(vidalloc_major, vidalloc_minor);
        result = register_chrdev_region(vidalloc_devt, 1, "vidmem");
        if (result)
        {
            pr_err("%s: register_chrdev_region error\n", __func__);
            goto err1;
        }
    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    vidalloc_class = class_create(THIS_MODULE, "vidmem");
#else
    vidalloc_class = class_create("vidmem");
#endif
    if (IS_ERR(vidalloc_class))
    {
        pr_err("%s, %d: class_create error!\n", __func__, __LINE__);
        goto err;
    }
	vidalloc_devt = MKDEV(vidalloc_major, vidalloc_minor);

	cdev_init(&vidalloc_cdev, &vidalloc_fops);
	result = cdev_add(&vidalloc_cdev, vidalloc_devt, 1);
	if ( result )
	{
		pr_err("%s, %d: cdev_add error!\n", __func__, __LINE__);
		goto err;
	}

	device_create(vidalloc_class, NULL, vidalloc_devt,
			NULL, "vidmem");

    return 0;
err:
    unregister_chrdev_region(vidalloc_devt, 1);
err1:
    pr_err("vidmem: module not inserted\n");
#endif
    return result;
}

static int vidalloc_remove(struct platform_device *pdev)
{
    DEBUG_PRINT("enter %s\n",__func__);
    rsvmem_pool_destroy();
	cdev_del(&vidalloc_cdev);
	device_destroy(vidalloc_class, vidalloc_devt);
    unregister_chrdev_region(vidalloc_devt, 1);
    class_destroy(vidalloc_class);

    return 0;
}

static const struct of_device_id thead_of_match[] = {
        { .compatible = "thead,light-vidmem",  },
        { .compatible = "xuantie,th1520-vidmem",  },
        { /* sentinel */  },
};

static struct platform_driver vidalloc_driver = {
    .probe = vidalloc_probe,
    .remove = vidalloc_remove,
    .driver = {
        .name = "vidmem",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(thead_of_match),
    }
};


int __init vidalloc_init(void)
{
    int ret = 0;
    DEBUG_PRINT("enter %s\n",__func__);

    ret = platform_driver_register(&vidalloc_driver);
    if (ret)
    {
        pr_err("register platform driver failed!\n");
    }
    else
    {
        pr_info("vidmem: module inserted. Major <%d>\n", vidalloc_major);
    }

    return ret;
}

void __exit vidalloc_cleanup(void)
{
    DEBUG_PRINT("enter %s\n",__func__);
    platform_driver_unregister(&vidalloc_driver);
    pr_info("vidmem: module removed.\n");
}


module_init(vidalloc_init);
module_exit(vidalloc_cleanup);
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("T-HEAD");
MODULE_DESCRIPTION("Video Memory Allocator");

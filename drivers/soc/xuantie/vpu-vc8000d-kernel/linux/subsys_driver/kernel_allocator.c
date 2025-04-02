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


#include <linux/pagemap.h>
#include <linux/seq_file.h>
#include <linux/mman.h>
#include <linux/errno.h>
#include <asm/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/mm_types.h>
#include <linux/version.h>
#include <linux/module.h>
#include "kernel_allocator.h"


#define DISCRETE_PAGES 0
//#define ALLOCATOR_DEBUG

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

#ifndef ALLOCATOR_DEBUG
#define DEBUG_PRINT(...) \
  do {                     \
  } while (0)
#else
#undef DEBUG_PRINT
#define DEBUG_PRINT(...) printk(__VA_ARGS__)
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

    union
    {
        /* Pointer to a array of pages. */
        struct
        {
            struct page *contiguousPages;
            dma_addr_t dma_addr;
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
    struct list_head link;
};

struct file_node
{
    struct list_head memList;
    struct file *filp;
    struct list_head link;
};

static struct list_head fileList;

static struct device *gdev = NULL;

static DEFINE_SPINLOCK(mem_lock);

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
        ONERROR(AllocateMemory(sizeof(struct page*) * numPages, (void * *)&tmpPages));
        pages = tmpPages;

        for (i = 0; i < numPages; ++i)
        {
            pages[i] = nth_page(memBlk->contiguousPages, i + skipPages);
        }
    }
    else
    {
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
    /* Make this mapping non-cached. */
    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

    /* Now map all the vmalloc pages to this user address. */
    if (memBlk->contiguous)
    {
        /* map kernel memory to user space.. */
        if (remap_pfn_range(vma,
                            vma->vm_start,
                            page_to_pfn(memBlk->contiguousPages) + skipPages,
                            numPages << PAGE_SHIFT,
                            vma->vm_page_prot) < 0)
        {
            ONERROR(ENOMEM);
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

    get_dma_buf(dmabuf);
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
            pagearray[k++] = sg_dma_address(s) + j * PAGE_SIZE;
        }
        size = sg_dma_len(s);
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

    printk("%s\n", __func__);

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

    return sgt;
}

static void _dmabuf_unmap(struct dma_buf_attachment *attachment,
                          struct sg_table *sgt,
                          enum dma_data_direction direction)
{
    dma_unmap_sg(attachment->dev, sgt->sgl, sgt->nents, direction);

    sg_free_table(sgt);
    kfree(sgt);
}

static int _dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
    struct mem_block *memBlk = dmabuf->priv;
    size_t skipPages = vma->vm_pgoff;
    size_t numPages = PAGE_ALIGN(vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
    int status = 0;

    printk("%s, %d: 0x%llx\n", __func__, __LINE__, page_to_phys(nth_page(memBlk->contiguousPages, 0)));

    ONERROR(Mmap(memBlk, skipPages, numPages, vma));

OnError:
    return IS_ERROR(status) ? -EINVAL : 0;
}

static void _dmabuf_release(struct dma_buf *dmabuf)
{
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

    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        printk("Allocator: Cannot find mem_node with bus address 0x%lx\n", bus_address);
        ONERROR(EINVAL);
    }

    memBlk = &mnode->memBlk;

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
    }

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

    /* Import dma buf handle. */
    memBlk->dmabuf = dma_buf_get(FD);
    if (!memBlk->dmabuf)
    {
        ONERROR(EFAULT);
    }

    ONERROR(Attach(memBlk));

    *bus_address = memBlk->pagearray[0];
    *size = memBlk->size;

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

    spin_lock(&mem_lock);
    list_del(&mnode->link);
    spin_unlock(&mem_lock);
    kfree(mnode);
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
    unsigned int gfp = GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN;
    int contiguous = Flags & ALLOC_FLAG_CONTIGUOUS;
    size_t numPages = GetPageCount(size, 0);
    unsigned long physical = 0;

    struct mem_block *memBlk = NULL;
    struct file_node *fnode = NULL;
    struct mem_node *mnode = NULL;

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

    if (contiguous)
    {
        size_t bytes = numPages << PAGE_SHIFT;

        void *addr = NULL;

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
                status = ENOMEM;
                goto OnError;
            }

            memBlk->contiguousPages = alloc_pages(gfp, order);
        }

        if (memBlk->contiguousPages == NULL)
        {
            ONERROR(ENOMEM);
        }

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
    }
    else // non-contiguous pages
    {
        ONERROR(NonContiguousAlloc(memBlk, numPages, gfp));

        // this piece of code is for sanity check
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

        status = dma_map_sg(gdev, memBlk->sgt->sgl, memBlk->sgt->nents, DMA_BIDIRECTIONAL);
        if (status != memBlk->sgt->nents)
        {
            NonContiguousFree(memBlk->nonContiguousPages, numPages);
            sg_free_table(memBlk->sgt);
            ONERROR(ENOMEM);
        }
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

    memBlk->contiguous = contiguous;
    memBlk->numPages = numPages;
    memBlk->size = size;

    getPhysical(memBlk, 0, &physical);
    *bus_address = physical;
    mnode->busAddr = physical;
    mnode->isImported = 0;

    list_add_tail(&mnode->link, &fnode->memList);

    printk("Allocated %d bytes (%ld pages) at physical address 0x%lx with %d sg table entries\n", 
        size, numPages, physical, contiguous ? 1 : memBlk->sgt->nents);

    return 0;

OnError:
    if (mnode)
    {
        kfree(mnode);
    }

    if (memBlk->sgt)
    {
        kfree(memBlk->sgt);
    }

    return status;
}


void
GFP_Free(
    IN struct file *filp,
    IN unsigned long bus_address
    )
{
    size_t i;
    struct page * page;
    struct mem_block *memBlk = NULL;
    struct mem_node *mnode = NULL;

    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        return;
    }

    memBlk = &mnode->memBlk;

    printk("Free %ld pages from physical address 0x%lx\n", memBlk->numPages, mnode->busAddr);

    if (memBlk->contiguous)
    {
        dma_unmap_page(gdev, memBlk->dma_addr,
                memBlk->numPages << PAGE_SHIFT, DMA_FROM_DEVICE);
    }
    else
    {
        dma_unmap_sg(gdev, memBlk->sgt->sgl, memBlk->sgt->nents,
                DMA_FROM_DEVICE);

        sg_free_table(memBlk->sgt);

        if (memBlk->sgt)
        {
            kfree(memBlk->sgt);
        }
    }

    for (i = 0; i < memBlk->numPages; i++)
    {
        if (memBlk->contiguous)
        {
            page = nth_page(memBlk->contiguousPages, i);

            ClearPageReserved(page);
        }
    }

    if (memBlk->contiguous)
    {
        if (memBlk->exact == true)
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

    list_del(&mnode->link);
    kfree(mnode);
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

    mnode = get_mem_node(filp, bus_address, 0);
    if (NULL == mnode)
    {
        return EINVAL;
    }

    memBlk = &mnode->memBlk;

    if (Mmap(memBlk, 0, memBlk->numPages, vma))
    {
        return ENOMEM;
    }
    
    memBlk->vma = vma;

    DEBUG_PRINT("Map %ld pages from physical address 0x%lx\n", memBlk->numPages, mnode->busAddr);

    return status;
}

int allocator_mmap(struct file *filp, struct vm_area_struct *vma)
{
    return GFP_MapUser(filp, vma);
}

int allocator_ioctl(void *filp, unsigned int cmd, unsigned long arg)
{
    MemallocParams params;
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
        ret = copy_from_user(&params, (void*)arg, sizeof(MemallocParams));
        if (!ret)
        {
            ret = GFP_Alloc(filp, params.size, params.flags, &params.bus_address);
            params.translation_offset = 0;
            if (!ret)
                ret = copy_to_user((MemallocParams *)arg, &params, sizeof(MemallocParams));
        }
        break;
    }
    case MEMORY_IOC_FREE:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(MemallocParams));
        if (!ret)
        {
            GFP_Free(filp, params.bus_address);
            ret = copy_to_user((MemallocParams *)arg, &params, sizeof(MemallocParams));
        }
        break;
    }
    case MEMORY_IOC_DMABUF_EXPORT:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(MemallocParams));
        if (!ret)
        {
            ret = DMABUF_Export(filp, params.bus_address, params.flags, &params.fd);
            if (!ret)
                ret = copy_to_user((MemallocParams *)arg, &params, sizeof(MemallocParams));
        }
        break;
    }
    case MEMORY_IOC_DMABUF_IMPORT:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(MemallocParams));
        if (!ret)
        {
            ret = DMABUF_Import(filp, params.fd, &params.bus_address, &params.size);
            params.translation_offset = 0;
            if (!ret)
                ret = copy_to_user((MemallocParams *)arg, &params, sizeof(MemallocParams));
        }
        break;
    }
    case MEMORY_IOC_DMABUF_RELEASE:
    {
        ret = copy_from_user(&params, (void*)arg, sizeof(MemallocParams));
        if (!ret)
        {
            DMABUF_Release(filp, params.bus_address);
            ret = copy_to_user((MemallocParams *)arg, &params, sizeof(MemallocParams));
        }
        break;
    }
    default:
        ret = EINVAL;
    }

    return ret;
}


int allocator_init(struct device *dev)
{
    int ret = 0;

    gdev = dev;
    INIT_LIST_HEAD(&fileList);

    return ret;
}

void allocator_remove(void)
{
}
#define ALLOCTOR_IN_THIS_KO 0
int allocator_open(struct inode *inode, struct file *filp)
{
    int ret = 0;
    /*we are not using this alloctor,vidmem is using
     MUST be match with allocator_release(),otherwise caused
     memleak of mem in this function allocted.
    */
#if ALLOCTOR_IN_THIS_KO
    struct file_node *fnode = NULL;
    
    if (AllocateMemory(sizeof(struct file_node), (void * *)&fnode))
        return ENOMEM;

    fnode->filp = filp; 
    INIT_LIST_HEAD(&fnode->memList);
    spin_lock(&mem_lock);
    list_add_tail(&fnode->link, &fileList);
    spin_unlock(&mem_lock);
#endif
    return ret;
}

void allocator_release(struct inode *inode, struct file *filp)
{
#if ALLOCTOR_IN_THIS_KO
    struct file_node *fnode = find_and_delete_file_node(filp);
    struct mem_node *node;
    struct mem_node *temp;

    if (NULL == fnode)
        return;

    list_for_each_entry_safe(node, temp, &fnode->memList, link)
    {
        // this is not expected, memory leak detected!
        printk("Allocator: Found unfreed memory at 0x%lx\n", node->busAddr);
        if (node->isImported)
            DMABUF_Release(filp, node->busAddr);
        else
            GFP_Free(filp, node->busAddr);
        list_del(&node->link);
        FreeMemory(node);
    }
    
    list_del(&fnode->link);
    FreeMemory(fnode);
#endif
}
MODULE_IMPORT_NS(DMA_BUF);

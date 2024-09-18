/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2021 VERISILICON
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
*    Copyright (C) 2014 - 2021 VERISILICON
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

#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "memalloc.h"

#ifndef HLINA_START_ADDRESS
#define HLINA_START_ADDRESS 0x1e0000000
#endif

#ifndef HLINA_SIZE
#define HLINA_SIZE 512
#endif

#ifndef HLINA_TRANSL_OFFSET
#define HLINA_TRANSL_OFFSET 0x0
#endif

/* the size of chunk in MEMALLOC_DYNAMIC */
#define CHUNK_SIZE (PAGE_SIZE * 4)

/* memory size in MBs for MEMALLOC_DYNAMIC */
unsigned long alloc_size = HLINA_SIZE;
unsigned long alloc_base = HLINA_START_ADDRESS;

/* user space SW will substract HLINA_TRANSL_OFFSET from the bus address
 * and decoder HW will use the result as the address translated base
 * address. The SW needs the original host memory bus address for memory
 * mapping to virtual address. */
unsigned long addr_transl = HLINA_TRANSL_OFFSET;

static int memalloc_major = 0; /* dynamic */

/* module_param(name, type, perm) */
module_param(alloc_size, ulong, 0);
module_param(alloc_base, ulong, 0);
module_param(addr_transl, ulong, 0);

static DEFINE_SPINLOCK(mem_lock);

typedef struct hlinc {
  u64 bus_address;
  u32 chunks_reserved;
  const struct file *filp; /* Client that allocated this chunk */
} hlina_chunk;

static hlina_chunk *hlina_chunks = NULL;
static size_t chunks = 0;

static int AllocMemory(unsigned long *busaddr, unsigned int size,
                       const struct file *filp);
static int FreeMemory(unsigned long busaddr, const struct file *filp);
static void ResetMems(void);

static long memalloc_ioctl(struct file *filp, unsigned int cmd,
                           unsigned long arg) {
  int ret = 0;
  MemallocParams memparams;
  unsigned long busaddr;

  PDEBUG("ioctl cmd 0x%08x\n", cmd);

  /*
   * extract the type and number bitfields, and don't decode
   * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
   */
  if (_IOC_TYPE(cmd) != MEMALLOC_IOC_MAGIC) return -ENOTTY;
  if (_IOC_NR(cmd) > MEMALLOC_IOC_MAXNR) return -ENOTTY;

  if (_IOC_DIR(cmd) & _IOC_READ)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
    ret = !access_ok(arg, _IOC_SIZE(cmd));
#else
    ret = !access_ok(VERIFY_WRITE, arg, _IOC_SIZE(cmd));
#endif
  else if (_IOC_DIR(cmd) & _IOC_WRITE)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
    ret = !access_ok(arg, _IOC_SIZE(cmd));
#else
    ret = !access_ok(VERIFY_READ, arg, _IOC_SIZE(cmd));
#endif
  if (ret) return -EFAULT;

  switch (cmd) {
  case MEMALLOC_IOCHARDRESET:
    PDEBUG("HARDRESET\n");
    ResetMems();
    break;
  case MEMALLOC_IOCXGETBUFFER:
    PDEBUG("GETBUFFER");

    ret = copy_from_user(&memparams, (MemallocParams *)arg,
                         sizeof(MemallocParams));
    if (ret) break;

    ret = AllocMemory(&memparams.bus_address, memparams.size, filp);

    memparams.translation_offset = addr_transl;

    ret |= copy_to_user((MemallocParams *)arg, &memparams,
                        sizeof(MemallocParams));

    break;
  case MEMALLOC_IOCSFREEBUFFER:
    PDEBUG("FREEBUFFER\n");

    __get_user(busaddr, (unsigned long *)arg);
    ret = FreeMemory(busaddr, filp);
    break;
  }

  return ret ? -EFAULT : 0;
}

static int memalloc_open(struct inode *inode, struct file *filp) {

  PDEBUG("dev opened\n");
  return 0;
}

static int memalloc_release(struct inode *inode, struct file *filp) {
  int i = 0;

  spin_lock(&mem_lock);

  for (i = 0; i < chunks; i++) {
    if (hlina_chunks[i].filp == filp) {
      printk(KERN_WARNING "memalloc: Found unfreed memory at release time!\n");

      hlina_chunks[i].filp = NULL;
      hlina_chunks[i].chunks_reserved = 0;
    }
  }

  spin_unlock(&mem_lock);
  PDEBUG("dev closed\n");
  return 0;
}

void __exit memalloc_cleanup(void) {
  if (hlina_chunks != NULL) vfree(hlina_chunks);

  unregister_chrdev(memalloc_major, "memalloc");

  PDEBUG("module removed\n");
  return;
}

/* VFS methods */
static struct file_operations memalloc_fops = {
  .owner = THIS_MODULE,
  .open = memalloc_open,
  .release = memalloc_release,
  .unlocked_ioctl = memalloc_ioctl
};

int __init memalloc_init(void) {
  int result;

  PDEBUG("module init\n");
  printk("memalloc: Linear Memory Allocator\n");
  printk("memalloc: Linear memory base = 0x%llx\n", alloc_base);

  chunks = (alloc_size * 1024 * 1024) / CHUNK_SIZE;

  printk(KERN_INFO
         "memalloc: Total size %ld MB; %d chunks"
         " of size %lu\n",
         alloc_size, (int)chunks, CHUNK_SIZE);

  hlina_chunks = (hlina_chunk *)vmalloc(chunks * sizeof(hlina_chunk));
  if (hlina_chunks == NULL) {
    printk(KERN_ERR "memalloc: cannot allocate hlina_chunks\n");
    result = -ENOMEM;
    goto err;
  }

  result = register_chrdev(memalloc_major, "memalloc", &memalloc_fops);
  if (result < 0) {
    PDEBUG("memalloc: unable to get major %d\n", memalloc_major);
    goto err;
  } else if (result != 0) {/* this is for dynamic major */
    memalloc_major = result;
  }

  ResetMems();

  return 0;

err:
  if (hlina_chunks != NULL) vfree(hlina_chunks);

  return result;
}

/* Cycle through the buffers we have, give the first free one */
static int AllocMemory(unsigned long *busaddr, unsigned int size,
                       const struct file *filp) {

  int i = 0;
  int j = 0;
  unsigned int skip_chunks = 0;

  /* calculate how many chunks we need; round up to chunk boundary */
  unsigned int alloc_chunks = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;

  *busaddr = 0;

  spin_lock(&mem_lock);

  /* run through the chunk table */
  for (i = 0; i < chunks;) {
    skip_chunks = 0;
    /* if this chunk is available */
    if (!hlina_chunks[i].chunks_reserved) {
      /* check that there is enough memory left */
      if (i + alloc_chunks > chunks) break;

      /* check that there is enough consecutive chunks available */
      for (j = i; j < i + alloc_chunks; j++) {
        if (hlina_chunks[j].chunks_reserved) {
          skip_chunks = 1;
          /* skip the used chunks */
          i = j + hlina_chunks[j].chunks_reserved;
          break;
        }
      }

      /* if enough free memory found */
      if (!skip_chunks) {
        *busaddr = hlina_chunks[i].bus_address;
        hlina_chunks[i].filp = filp;
        hlina_chunks[i].chunks_reserved = alloc_chunks;
        break;
      }
    } else {
      /* skip the used chunks */
      i += hlina_chunks[i].chunks_reserved;
    }
  }

  spin_unlock(&mem_lock);

  if (*busaddr == 0) {
    printk("memalloc: Allocation FAILED: size = %ld\n", size);
    return -EFAULT;
  } else {
    PDEBUG("MEMALLOC OK: size: %d, reserved: %ld\n", size,
           alloc_chunks * CHUNK_SIZE);
  }

  return 0;
}

/* Free a buffer based on bus address */
static int FreeMemory(unsigned long busaddr, const struct file *filp) {
  int i = 0;

  spin_lock(&mem_lock);

  for (i = 0; i < chunks; i++) {
    /* user space SW has stored the translated bus address, add addr_transl to
     * translate back to our address space */
    if (hlina_chunks[i].bus_address == busaddr + addr_transl) {
      if (hlina_chunks[i].filp == filp) {
        hlina_chunks[i].filp = NULL;
        hlina_chunks[i].chunks_reserved = 0;
      } else {
        printk(KERN_WARNING "memalloc: Owner mismatch while freeing memory!\n");
      }
      break;
    }
  }

  spin_unlock(&mem_lock);

  return 0;
}

/* Reset "used" status */
static void ResetMems(void) {
  int i = 0;
  unsigned long ba = alloc_base;

  spin_lock(&mem_lock);

  for (i = 0; i < chunks; i++) {
    hlina_chunks[i].bus_address = ba;
    hlina_chunks[i].filp = NULL;
    hlina_chunks[i].chunks_reserved = 0;

    ba += CHUNK_SIZE;
  }

  spin_unlock(&mem_lock);
}

module_init(memalloc_init);
module_exit(memalloc_cleanup);

/* module description */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Verisilicon");
MODULE_DESCRIPTION("Linear RAM allocation");

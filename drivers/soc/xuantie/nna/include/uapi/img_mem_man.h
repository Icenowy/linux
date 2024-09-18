/*!
 *****************************************************************************
 *
 * @File       img_mem_man.h
 * ---------------------------------------------------------------------------
 *
 * Copyright (c) Imagination Technologies Ltd.
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 ("GPL")in which case the provisions of
 * GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms
 * of GPL, and not to allow others to use your version of this file under the
 * terms of the MIT license, indicate your decision by deleting the provisions
 * above and replace them with the notice and other provisions required by GPL
 * as set out in the file called "GPLHEADER" included in this distribution. If
 * you do not delete the provisions above, a recipient may use your version of
 * this file under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT_COPYING".
 *
 *****************************************************************************/

#ifndef IMG_MEM_MAN_UAPI_H
#define IMG_MEM_MAN_UAPI_H

/* memory attributes */
enum img_mem_attr {
	IMG_MEM_ATTR_CACHED        = 0x00000001,
	IMG_MEM_ATTR_UNCACHED      = 0x00000002,
	IMG_MEM_ATTR_WRITECOMBINE  = 0x00000004,

	/* Special */
	IMG_MEM_ATTR_SECURE        = 0x00000010,
	IMG_MEM_ATTR_NOMAP         = 0x00000020,
	IMG_MEM_ATTR_NOSYNC        = 0x00000040,

	/* Internal */
	IMG_MEM_ATTR_MMU           = 0x10000000,
	IMG_MEM_ATTR_OCM           = 0x20000000,
};

/* Cache attributes mask */
#define IMG_MEM_ATTR_CACHE_MASK 0xf

/* Supported heap types */
enum img_mem_heap_type {
	IMG_MEM_HEAP_TYPE_UNKNOWN = 0,
	IMG_MEM_HEAP_TYPE_UNIFIED,
	IMG_MEM_HEAP_TYPE_CARVEOUT,
	IMG_MEM_HEAP_TYPE_ION,
	IMG_MEM_HEAP_TYPE_DMABUF,
	IMG_MEM_HEAP_TYPE_COHERENT,
	IMG_MEM_HEAP_TYPE_ANONYMOUS,
	IMG_MEM_HEAP_TYPE_OCM,
};

/* Heap attributes */
enum img_mem_heap_attrs {
	IMG_MEM_HEAP_ATTR_INTERNAL  = 0x01,
	IMG_MEM_HEAP_ATTR_IMPORT    = 0x02,
	IMG_MEM_HEAP_ATTR_EXPORT    = 0x04,
	IMG_MEM_HEAP_ATTR_SEALED    = 0x08,

	/* User attributes */
	IMG_MEM_HEAP_ATTR_LOCAL     = 0x10,
	IMG_MEM_HEAP_ATTR_SHARED    = 0x20,
};

/* heaps ids */
#define IMG_MEM_MAN_HEAP_ID_INVALID 0
#define IMG_MEM_MAN_MIN_HEAP 1
#define IMG_MEM_MAN_MAX_HEAP 16

/* buffer ids (per memory context) */
#define IMG_MEM_MAN_BUF_ID_INVALID 0
#define IMG_MEM_MAN_MIN_BUFFER 1
#define IMG_MEM_MAN_MAX_BUFFER 2000

/* Definition of VA guard gap between allocations */
#define IMG_MEM_VA_GUARD_GAP 0x1000

/* Virtual memory space for buffers allocated
 * in the kernel - OCM & device debug buffers */
#define IMG_MEM_VA_HEAP1_BASE 0x8000000ULL
#define IMG_MEM_VA_HEAP1_SIZE 0x40000000ULL

/* Definition of VA guard gap between heaps - 2MB (size of MMU PD) */
#define IMG_MEM_HEAP_GUARD_GAP 0x200000

/* Virtual memory space for buffers allocated in the user space */
#define IMG_MEM_VA_HEAP2_BASE ( \
		IMG_MEM_VA_HEAP1_BASE + IMG_MEM_VA_HEAP1_SIZE + IMG_MEM_HEAP_GUARD_GAP)
#define IMG_MEM_VA_HEAP2_SIZE 0x3C0000000ULL

#endif /* IMG_MEM_MAN_UAPI_H */

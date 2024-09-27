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

#ifndef __VIDEO_MEMORY_H_
#define __VIDEO_MEMORY_H_


#define IN
#define OUT
#define INOUT
#define OPTIONAL

/* No special needs. */
#define ALLOC_FLAG_NONE                  0x00000000
/* Physical contiguous. */
#define ALLOC_FLAG_CONTIGUOUS            0x00000001
/* Physical non contiguous. */
#define ALLOC_FLAG_NON_CONTIGUOUS        0x00000002
/* Need 32bit address. */
#define ALLOC_FLAG_4GB_ADDR              0x00000004
/* CMA priority */
#define ALLOC_FLAG_CMA                   0x00000008
/* Use VI reserved memory */
#define ALLOC_FLAG_VI                    0x00000010
/* Buffer enable cache */
#define ALLOC_FLAG_ENABLE_CACHE          0x00000020

/* Alloc rsvmem pool region id should be 0~15 */
#define SET_ALLOC_FLAG_REGION(flag, region_id) (flag & 0x00ffffff) | (region_id << 24)
#define GET_ALLOC_FLAG_REGION(flag)            (flag >> 24)

#define MEMORY_IOC_MAGIC  'a'

#define MEMORY_IOC_ALLOCATE         _IOWR(MEMORY_IOC_MAGIC, 1, VidmemParams *)
#define MEMORY_IOC_FREE             _IOWR(MEMORY_IOC_MAGIC, 2, VidmemParams *)
#define MEMORY_IOC_DMABUF_EXPORT    _IOWR(MEMORY_IOC_MAGIC, 3, VidmemParams *)
#define MEMORY_IOC_DMABUF_IMPORT    _IOWR(MEMORY_IOC_MAGIC, 4, VidmemParams *)
#define MEMORY_IOC_DMABUF_RELEASE   _IOWR(MEMORY_IOC_MAGIC, 5, VidmemParams *)
#define MEMORY_IOC_DMABUF_FLUSH_CACHE   _IOWR(MEMORY_IOC_MAGIC, 6, VidmemParams *)
#define MEMORY_IOC_DMABUF_INVALID_CACHE   _IOWR(MEMORY_IOC_MAGIC, 7, VidmemParams *)
#define MEMORY_IOC_MAXNR 7

typedef struct {
  unsigned long bus_address;
  unsigned int size;
  unsigned long translation_offset;
  int fd;
  int flags;
} VidmemParams;




#endif /* __VIDEO_MEMORY_H_ */

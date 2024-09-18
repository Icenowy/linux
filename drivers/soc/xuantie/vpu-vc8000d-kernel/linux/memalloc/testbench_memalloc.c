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

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <unistd.h>
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <assert.h>

#include "basetype.h"
#include "memalloc.h"

int main(void) {

  i32 memdev_fd = -1;
  i32 memdev_fd_2 = -1;
  i32 memdev_map = -1;
  i32 i = 0, u = 0;
  u32 *virtual1 = MAP_FAILED;
  u32 *virtual2 = MAP_FAILED;

  u32 pgsize = getpagesize();

  const char *memdev = "/dev/memalloc";
  const char *memmap = "/dev/mem";

  u32 bus_address1 = 0, bus_address2 = 0;
  u32 size = 4096;

  memdev_fd = open(memdev, O_RDWR);
  if (memdev_fd == -1) {
    printf("Failed to open dev: %s\n", memdev);
    goto end1;
  }

  memdev_fd_2 = open(memdev, O_RDWR);
  if (memdev_fd_2 == -1) {
    printf("Failed to open dev: %s\n", memdev);
    goto end1;
  }

  memdev_map = open(memmap, O_RDWR);
  if (memdev_map == -1) {
    printf("Failed to open dev: %s\n", memmap);
    goto end1;
  }

  size = (size + pgsize) & (~(pgsize - 1));

  printf("Hard Reset\n", size);
  /*ioctl(memdev_fd, MEMALLOC_IOCHARDRESET, 0);*/

  for (i = 0; i < 100; i++) {

    ioctl(memdev_fd, MEMALLOC_IOCXGETBUFFER, &bus_address1);
    printf("bus_address1 0x%08x\n", bus_address1);

    ioctl(memdev_fd_2, MEMALLOC_IOCXGETBUFFER, &bus_address2);
    printf("bus_address2 0x%08x\n", bus_address2);

    /* test write stuff in the mem are*/

    if (bus_address1) {

      virtual1 = (u32 *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                             memdev_map, bus_address1);

      printf("Virtual1 %08x\n", virtual1);
    }
    if (virtual1 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        *virtual1 = i + 1;
      }
    } else {
      printf("map failed\n");
    }

    /* test write stuff in the mem are*/

    if (bus_address2) {

      virtual2 = (u32 *)mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                             memdev_map, bus_address2);

      printf("Virtual2 0x%08x\n", virtual2);
    }
    if (virtual2 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        *virtual2 = i + 2;
      }
    } else {
      printf("map failed\n");
    }

    if (virtual1 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        if (*virtual1 != i + 1) {
          printf("MISMATCH1!\n");
          break;
        }
      }
    }

    if (virtual2 != MAP_FAILED) {
      for (u = 0; u < size / 4; u++) {
        if (*virtual2 != i + 2) {
          printf("MISMATCH2!\n");
          break;
        }
      }
    }

    if ((i % 30)) {
      printf("release %d ", size);
      ioctl(memdev_fd, MEMALLOC_IOCSFREEBUFFER, &bus_address1);
      printf("address:\t\t0x%08x\n", bus_address1);
    }

    printf("release %d ", size);
    /*ioctl(memdev_fd_2, MEMALLOC_IOCSFREEBUFFER, &bus_address2);
    printf("address:\t\t0x%08x\n", bus_address2);
    virtual1 = MAP_FAILED;
    virtual2 = MAP_FAILED;*/

    if (!(i % 30)) {
      close(memdev_fd);
      memdev_fd = open(memdev, O_RDWR);
      if (memdev_fd == -1) {
        printf("Failed to open dev: %s\n", memdev);
        goto end1;
      }
    }
    close(memdev_fd_2);
    memdev_fd_2 = open(memdev, O_RDWR);
    if (memdev_fd_2 == -1) {
      printf("Failed to open dev: %s\n", memdev);
      goto end1;
    }
  }

end1:

  close(memdev_fd);
  close(memdev_fd_2);
  close(memdev_map);

  return 0;
}

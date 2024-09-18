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
#include "osal.h"
#include "user_freertos.h"
#include "io_tools.h"
#include "memalloc_freertos.h"

static pthread_mutex_t vcmd_dev_open_count_mutex = PTHREAD_MUTEX_INITIALIZER;
static u32 vcmd_dev_open_count = 0;
extern int vcmd; //it is defined in hantro_dec_freertos.c

/**
 * Unify driver for vcmd, the related vcmd interface besides init/open/release/
 * ioctl will be called in the hantrdec_init/open/release/ioctl when vcmd is used.
 * All drivers's probe should be added and be called by main() explicitly
 */
int Platform_init() {
  //Firstly, initialize the mem for IO device
  if (memalloc_init() != 0) {
    PDEBUG("memalloc_init error\n");
    assert(0);
  }

  //vcmd will modify if exist vcmd device in hantrodec_init
  if (hantrodec_init() != 0) {
    PDEBUG("hantroenc_init error\n");
    assert(0);
  }

  return 0;
}
int freertos_open(const char* dev_name, int flag) {
  //do a hash operator to gain the only dev fd
  //int fd = hash(name);
  int fd = 0; //the default is error id
  if(!strcmp(dev_name, DEC_MODULE_PATH)) { 
    fd = DEC_FD << 28;
    if(vcmd) {
      //u32, MSB 4bit for device type, LSB 28bit for reference count
      pthread_mutex_lock(&vcmd_dev_open_count_mutex);
      vcmd_dev_open_count++;
      fd += vcmd_dev_open_count;
      pthread_mutex_unlock(&vcmd_dev_open_count_mutex);
    }    
    hantrodec_open(NULL, fd);
  }  
  else if(!strcmp(dev_name, MEMALLOC_MODULE_PATH)) {
    fd = MEM_FD << 28;
    memalloc_open(NULL, fd);
  }
  return fd;
}

void freertos_close(int fd) {
  //u32, MSB 4bit for device type
  u32 fd_tmp = fd;
  fd_tmp = fd_tmp >> 28;

  switch(fd_tmp) {
    case DEC_FD: {
      hantrodec_release(NULL, fd);
      break;
    }
    case MEM_FD: {
      //so if low in special paltform, could comment it when test, but in normal, it will be opened
      //memalloc_release(NULL, fd);
      break;
    }
    case 0: //for /dev/mem, it it not used, so shouldn't go to default
    	break;
    default: {
      PDEBUG("freertos_release fd 0x%x error\n", fd);
      break;
    }
  }
}

long freertos_ioctl(int fd, unsigned int cmd, void *arg) {
  long ret = -1;
  //u32, MSB 4bit for device type
  u32 fd_tmp = fd;
  fd_tmp = fd_tmp >> 28;
  switch(fd_tmp) {
    case DEC_FD: {
      ret = hantrodec_ioctl(fd, cmd, arg);
      break;
    }
    case MEM_FD: {
      ret = memalloc_ioctl(fd, cmd, arg);
      break;
    }
    default: {
      PDEBUG("freertos_ioctl fd 0x%x error\n", fd);
      break;
    }
  }
  
  return ret;
}


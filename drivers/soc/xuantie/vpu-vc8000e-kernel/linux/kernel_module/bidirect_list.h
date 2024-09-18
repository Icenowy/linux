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

#ifndef _BIDIRECT_LIST_H_
#define _BIDIRECT_LIST_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __FREERTOS__
#include "dev_common_freertos.h"    /* needed for the _IOW etc stuff used later */
#elif defined(__linux__)
#include <linux/ioctl.h>    /* needed for the _IOW etc stuff used later */
#else //For other os
//TODO...
#endif

/*
 * Macros to help debugging
 */

#undef PDEBUG   /* undef it, just in case */
#ifdef BIDIRECTION_LIST_DEBUG
#  ifdef __KERNEL__
    /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_INFO "hmp4e: " fmt, ## args)
#  else
    /* This one for user space */
#    define PDEBUG(fmt, args...) printf(__FILE__ ":%d: " fmt, __LINE__ , ## args)
#  endif
#else
#  define PDEBUG(fmt, args...)  /* not debugging: nothing */
#endif

/***********************************************************************************************************************************************\
* <Typedefs>
\**********************************************************************************************************************************************/
typedef struct bi_list_node{
  void* data;
  struct bi_list_node* next; 
  struct bi_list_node* previous;
}bi_list_node;
typedef struct bi_list{
  bi_list_node* head; 
  bi_list_node* tail; 
}bi_list;

void init_bi_list(bi_list* list);

bi_list_node* bi_list_create_node(void);

void bi_list_free_node(bi_list_node* node);

void bi_list_insert_node_tail(bi_list* list,bi_list_node* current_node);

void bi_list_insert_node_before(bi_list* list,bi_list_node* base_node,bi_list_node* new_node);

void bi_list_remove_node(bi_list* list,bi_list_node* current_node);

#ifdef __cplusplus
}
#endif

#endif /* !_BIDIRECT_LIST_H_ */

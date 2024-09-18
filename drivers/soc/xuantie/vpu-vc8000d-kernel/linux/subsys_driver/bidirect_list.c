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
#ifdef __FREERTOS__
#include <string.h>
#include "osal.h"
#elif defined(__linux__)
#include <linux/kernel.h>
#include <linux/module.h>
/* needed for __init,__exit directives */
#include <linux/init.h>
/* needed for remap_page_range
    SetPageReserved
    ClearPageReserved
*/
#include <linux/mm.h>
/* obviously, for kmalloc */
#include <linux/slab.h>
/* for struct file_operations, register_chrdev() */
#include <linux/fs.h>
/* standard error codes */
#include <linux/errno.h>

#include <linux/moduleparam.h>
/* request_irq(), free_irq() */
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <linux/semaphore.h>
#include <linux/spinlock.h>
/* needed for virt_to_phys() */
#include <asm/io.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>

#include <asm/irq.h>

#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#else //For other os
//TODO...
#endif
#include "bidirect_list.h"

void init_bi_list(bi_list* list)
{
  list->head = NULL;
  list->tail = NULL; 
}

bi_list_node* bi_list_create_node(void)
{
  bi_list_node* node=NULL;
  node=(bi_list_node*)vmalloc(sizeof(bi_list_node));
  if(node==NULL)
  {
    PDEBUG ("%s\n","vmalloc for node fail!");
    return node;
  }
  memset(node,0,sizeof(bi_list_node)); 
  return node;
}
void bi_list_free_node(bi_list_node* node)
{
  //free current node
  vfree(node);
  return;
}

void bi_list_insert_node_tail(bi_list* list,bi_list_node* current_node)
{
  if(current_node==NULL)
  {
    PDEBUG ("%s\n","insert node tail  NULL");
    return;
  }
  if(list->tail)
  {
   current_node->previous=list->tail;
   list->tail->next=current_node;
   list->tail=current_node;
   list->tail->next=NULL;
  }
  else
  {
   list->head=current_node;
   list->tail=current_node;
   current_node->next=NULL;
   current_node->previous=NULL;
  }
  return;
}

void bi_list_insert_node_before(bi_list* list,bi_list_node* base_node,bi_list_node* new_node)
{
  bi_list_node* temp_node_previous=NULL;
  if(new_node==NULL)
  {
    PDEBUG ("%s\n","insert node before new node NULL");
    return;
  }
  if(base_node)
  {
   if(base_node->previous)
   {
    //at middle position
    temp_node_previous = base_node->previous;
    temp_node_previous->next=new_node;
    new_node->next = base_node;
    base_node->previous = new_node;
    new_node->previous=temp_node_previous;
   }
   else
   {
    //at head
    base_node->previous = new_node;
    new_node->next = base_node;
    list->head=new_node;
    new_node->previous = NULL;
   }
  }
  else
  {
   //at tail
   bi_list_insert_node_tail(list,new_node);
  }
  return;
}


void bi_list_remove_node(bi_list* list,bi_list_node* current_node)
{
  bi_list_node* temp_node_previous=NULL;
  bi_list_node* temp_node_next=NULL;
  if(current_node==NULL)
  {
    PDEBUG ("%s\n","remove node NULL");
    return;
  }
  temp_node_next=current_node->next;
  temp_node_previous=current_node->previous;
  
  if(temp_node_next==NULL && temp_node_previous==NULL )
  {
    //there is only one node.
    list->head=NULL;
    list->tail=NULL;
  }
  else if(temp_node_next==NULL)
  {
    //at tail
    list->tail=temp_node_previous;
    temp_node_previous->next=NULL;
  }
  else if( temp_node_previous==NULL)
  {
    //at head
    list->head=temp_node_next;
    temp_node_next->previous=NULL;
  }
  else
  {
   //at middle position
   temp_node_previous->next=temp_node_next;
   temp_node_next->previous=temp_node_previous;
  }
  return;
}

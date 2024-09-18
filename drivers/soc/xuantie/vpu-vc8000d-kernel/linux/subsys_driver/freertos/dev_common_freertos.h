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
#ifndef _DEV_COMMON_FREERTOS_H_
#define _DEV_COMMON_FREERTOS_H_

/**********Driver for FreeRTOS based on the method of Linux's calling**********/
#define __u8 u8
#define __i8 i8
#define __u32 u32
#define __i32 i32

#define sema_init(s,v)               sem_init(s,0,v)

//PCIE
#define pci_get_device(a,b,c)        (void *)0
#define pci_enable_device(a)         0
#define pci_disable_device(a)        0
#define pci_resource_start(a,b)      0
#define pci_resource_len(a,b)        0
/*--------------------------------*/
/* IO */
//#define O_RDONLY (1)
//#define O_WRONLY (1 << 1)
//#define O_RDWR (O_RDONLY | O_WRONLY)
//#define O_SYNC (1 << 2)
#undef MAP_FAILED
#define MAP_FAILED NULL
#undef NULL
#define NULL  ((void *)0)
#define getpagesize() (1)
#define ioremap_nocache(addr,size)           (addr)
#define iounmap(addr)
#define mmap(va, size, access, flag, fd, pa) DirectMemoryMap(pa, size)
#define munmap(pRegs, size)
#define vmalloc(a)                   pvPortMalloc(a) //malloc(a);
#define vfree(a)                     vPortFree(a) //free(a);
#define probe(a)                     Platform_init(a)
#define open(name,flag)              freertos_open(name,flag)
#define ioctl(fd,cmd,arg)            freertos_ioctl(fd,cmd,arg)
#define close(fd)                    freertos_close(fd)
#define KERN_INFO
#define KERN_ERR
#define KERN_WARNING
#define KERN_DEBUG 
#define printk                       PDEBUG
#define access_ok(a,b,c)             (1)
#define __init
#define __exit
#define msleep(s)                    osal_usleep(s*1000)

/*
 * Let any architecture override either of the following before
 * including this file.
 */
#ifndef _IOC_SIZEBITS
# define _IOC_SIZEBITS  14
#endif

#ifndef _IOC_DIRBITS
# define _IOC_DIRBITS   2
#endif
#define _IOC_NRBITS     8
#define _IOC_TYPEBITS   8

#define _IOC_NRMASK     ((1 << _IOC_NRBITS)-1)
#define _IOC_TYPEMASK   ((1 << _IOC_TYPEBITS)-1)
#define _IOC_SIZEMASK   ((1 << _IOC_SIZEBITS)-1)
#define _IOC_DIRMASK    ((1 << _IOC_DIRBITS)-1)

#define _IOC_NRSHIFT    0
#define _IOC_TYPESHIFT  (_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_SIZESHIFT  (_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_DIRSHIFT   (_IOC_SIZESHIFT+_IOC_SIZEBITS)

	/*
	 * Direction bits, which any architecture can choose to override
	 * before including this file.
	 */
#ifndef _IOC_NONE
# define _IOC_NONE      0U
#endif

#ifndef _IOC_WRITE
# define _IOC_WRITE     1U
#endif

#ifndef _IOC_READ
# define _IOC_READ      2U
#endif

#define _IOC(dir,type,nr,size) \
			(((dir)  << _IOC_DIRSHIFT) | \
			 ((type) << _IOC_TYPESHIFT) | \
			 ((nr)	 << _IOC_NRSHIFT) | \
			 ((size) << _IOC_SIZESHIFT))

#define _IOC_TYPECHECK(t) (sizeof(t))

/* used to create numbers */
#define _IO(type,nr)            _IOC(_IOC_NONE,(type),(nr),0)
#define _IOR(type,nr,size)      _IOC(_IOC_READ,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IOW(type,nr,size)      _IOC(_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IOWR(type,nr,size)     _IOC(_IOC_READ|_IOC_WRITE,(type),(nr),(_IOC_TYPECHECK(size)))
#define _IOR_BAD(type,nr,size)  _IOC(_IOC_READ,(type),(nr),sizeof(size))
#define _IOW_BAD(type,nr,size)  _IOC(_IOC_WRITE,(type),(nr),sizeof(size))

/* used to decode ioctl numbers.. */
#define _IOC_DIR(nr)            (((nr) >> _IOC_DIRSHIFT) & _IOC_DIRMASK)
#define _IOC_TYPE(nr)           (((nr) >> _IOC_TYPESHIFT) & _IOC_TYPEMASK)
#define _IOC_NR(nr)             (((nr) >> _IOC_NRSHIFT) & _IOC_NRMASK)
#define _IOC_SIZE(nr)           (((nr) >> _IOC_SIZESHIFT) & _IOC_SIZEMASK)

/* ...and for the drivers/sound files... */
#define IOC_IN                  (_IOC_WRITE << _IOC_DIRSHIFT)
#define IOC_OUT                 (_IOC_READ << _IOC_DIRSHIFT)
#define IOC_INOUT               ((_IOC_WRITE|_IOC_READ) << _IOC_DIRSHIFT)
#define IOCSIZE_MASK            (_IOC_SIZEMASK << _IOC_SIZESHIFT)
#define IOCSIZE_SHIFT           (_IOC_SIZESHIFT)

#define DECLARE_WAIT_QUEUE_HEAD(a)

/* Kernel/User space */
#define copy_from_user(des,src,size)       (memcpy(des,src,size),0)
#define copy_to_user(des,src,size)         (memcpy(des,src,size),0)
#define raw_copy_from_user(des,src,size)   (memcpy(des,src,size),0)
#define raw_copy_to_user(des,src,size)     (memcpy(des,src,size),0)
#define __put_user(val,user)               (*(user) = (val))
#define __get_user(val,user)               ((val) = *user)

/* Interrupt */
/*********************request_irq, disable_irq, enable_irq need to be provided by customer*********************/
#define request_irq(i,isr,flag,name,data)  RegisterIRQ(i, isr, flag, name, data)
#define disable_irq(i)                     IntDisableIRQ(i)
#define enable_irq(i)                      IntEnableIRQ(i)
#define free_irq(i,data)
#define irqreturn_t                        void
#define down_interruptible(a)              pthread_mutex_lock(a)
#define up(a)                              pthread_mutex_unlock(a)
#define wait_event_interruptible(a,b)      sem_wait(&a)
#define wake_up_interruptible_all(a)       sem_post(a)
#define IRQ_RETVAL(a)                      //(a)
#define IRQ_HANDLED                        //1
#define IRQ_NONE                           //0
#define register_chrdev(m,name,op)         (0)
#define unregister_chrdev(m,name)
#define IRQF_SHARED                        1
#define IRQF_DISABLED                      0x00000020
#define request_mem_region(addr,size,name) (1)
#define release_mem_region(addr,size)
#define kill_fasync(queue,sig,flag)
//Timer
//typedef TimerHandle_t struct timer_list; //For FreeRTOS

#define ERR_OS_FAIL                        (0xffff)
#define ERESTARTSYS                        ERR_OS_FAIL
#ifdef EFAULT
#undef EFAULT
#endif
#define EFAULT                             ERR_OS_FAIL
#ifdef ENOTTY
#undef ENOTTY
#endif
#define ENOTTY                             ERR_OS_FAIL
#ifdef EINVAL
#undef EINVAL
#endif
#define EINVAL                             ERR_OS_FAIL
#ifdef EBUSY
#undef EBUSY
#endif
#define EBUSY                              ERR_OS_FAIL

/* kernel sync objects */
/**********the atomic operations need to be provided by customer**********/
//#define atomic_t                           __attribute__((section("cpu_dram"), aligned(4))) xmp_atomic_int_t//i32
//#define ATOMIC_INIT(a)                     XMP_ATOMIC_INT_INITIALIZER(a)//a
//#define atomic_inc(a)                      xmp_atomic_int_increment(a,1)//((*(a))++)
//#define atomic_read(a)                     xmp_atomic_int_value(a)//(a)
#define atomic_t                           i32
#define ATOMIC_INIT(a)                     a
#define atomic_inc(a)                      ((*(a))++)
#define atomic_read(a)                     (a)
typedef int sig_atomic_t;
//typedef int sigset_t;
#define sigemptyset(set)
#define sigaddset(set, sig)
#define sigsuspend(set)
#define sigwait(set, signo)

#define spinlock_t                         pthread_mutex_t
#define spin_lock_init(a)                  (*a = PTHREAD_MUTEX_INITIALIZER)
#define isr_spin_lock_irqsave(a,b)
#define isr_spin_unlock_irqrestore(a,b)
#define spin_lock_irqsave(a,b)        {pthread_mutex_lock(a); \
                                       b = ioread32((void *)SYS_REG_INT_EN) & g_vc8000_int_enable_mask; \
                                       *((volatile uint32_t *)SYS_REG_INT_EN) &= ~(b);}
#define spin_unlock_irqrestore(a,b)   {*((volatile uint32_t *)SYS_REG_INT_EN) = \
                                           ioread32((void *)SYS_REG_INT_EN) | (b); \
                                       pthread_mutex_unlock(a); }
#define spin_lock(a)                       pthread_mutex_lock(a)
#define spin_unlock(a)                     pthread_mutex_unlock(a)
#define getpid()                           (int)pthread_self()

#endif /* _DEV_COMMON_FREERTOS_H_ */

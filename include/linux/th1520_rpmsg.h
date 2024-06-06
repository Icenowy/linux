/*
 * Copyright (C) 2023 Alibaba Group Holding Limited.
 */

/*
 * The code contained herein is licensed under the GNU Lesser General
 * Public License.  You may obtain a copy of the GNU Lesser General
 * Public License Version 2.1 or later at the following locations:
 *
 * http://www.opensource.org/licenses/lgpl-license.html
 * http://www.gnu.org/copyleft/lgpl.html
 */

/*
 * @file linux/th1520_rpmsg.h
 *
 * @brief Global header file for imx RPMSG
 *
 * @ingroup RPMSG
 */
#ifndef __LINUX_TH1520_RPMSG_H__
#define __LINUX_TH1520_RPMSG_H__

#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_ring.h>
#include <linux/proc_fs.h>

/* Category define */
#define TH1520_RMPSG_LIFECYCLE	1
#define TH1520_RPMSG_PMIC		2
#define TH1520_RPMSG_AUDIO		3
#define TH1520_RPMSG_KEY		4
#define TH1520_RPMSG_GPIO		5
#define TH1520_RPMSG_RTC		6
#define TH1520_RPMSG_SENSOR	7
/* rpmsg version */
#define TH1520_RMPSG_MAJOR		1
#define TH1520_RMPSG_MINOR		0

enum th1520_rpmsg_variants {
        TH1520A,
        TH1520B,
        TH1520_RPMSG,
};

struct th1520_virdev {
        struct virtio_device vdev;
        unsigned int vring[2];
        struct virtqueue *vq[2];
        int base_vq_id;
        int num_of_vqs;
        struct notifier_block nb;
};

struct th1520_rpmsg_vproc {
        char *rproc_name;
        struct mutex lock;
        struct clk *mu_clk;
        enum th1520_rpmsg_variants variant;
        int vdev_nums;
        int first_notify;
#define MAX_VDEV_NUMS   8
        struct th1520_virdev ivdev[MAX_VDEV_NUMS];
        void __iomem *mu_base;
        struct delayed_work rpmsg_work;
        struct blocking_notifier_head notifier;
#define MAX_NUM 10      /* enlarge it if overflow happen */
        u32 m4_message[MAX_NUM];
        u32 in_idx;
        u32 out_idx;
        u32 core_id;
        spinlock_t mu_lock;
#ifdef CONFIG_PM_SLEEP
        struct semaphore pm_sem;
        int sleep_flag;
#endif
        /*for log proc*/
	phys_addr_t log_phy;
	size_t log_size;
	void __iomem *log_mem;
	void *log_ctrl;
        struct proc_dir_entry *proc_dir;
};

struct th1520_rpmsg_head {
	u8 cate;
	u8 major;
	u8 minor;
	u8 type;
	u8 cmd;
	u8 reserved[5];
} __attribute__ ((packed));

#endif /* __LINUX_TH1520_RPMSG_H__*/


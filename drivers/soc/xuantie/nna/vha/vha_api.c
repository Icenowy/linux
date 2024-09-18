/*
 *****************************************************************************
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


#include <linux/slab.h>
#include <asm/current.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/poll.h>

#include <uapi/img_mem_man.h>
#include <uapi/version.h>
#include <img_mem_man.h>
#include "vha_common.h"
#include "vha_plat.h"

static uint32_t default_mem_heap = IMG_MEM_MAN_HEAP_ID_INVALID;
module_param(default_mem_heap, uint, 0444);
MODULE_PARM_DESC(default_mem_heap,
		"default heap to use when allocating device memory, \
		when 'invalid' -> user requested id will be used.");

#define VHA_IRQ_FENCE() \
	do { \
		spin_lock_irq(&vha->irq_lock); \
		spin_unlock_irq(&vha->irq_lock); \
	} while(0)

static ssize_t vha_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct vha_session *session = file->private_data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	struct vha_rsp *rsp;
	int ret;

	dev_dbg(miscdev->this_device, "%s: PID: %d, vha: %p, link: %p\n",
			__func__, task_pid_nr(current), vha, session);

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	while (list_empty(&session->rsps)) {
		mutex_unlock(&vha->lock);

		if (file->f_flags & O_NONBLOCK) {
			dev_err(miscdev->this_device,
				"%s: returning, no block!\n", __func__);
			return -EAGAIN;
		}
		dev_dbg(miscdev->this_device, "%s: going to sleep\n", __func__);
		if (wait_event_interruptible(session->wq,
						!list_empty(&session->rsps))) {
			dev_err(miscdev->this_device, "%s: signal\n", __func__);
			return -ERESTARTSYS;
		}

		dev_dbg(miscdev->this_device, "%s: woken up\n", __func__);

		ret = mutex_lock_interruptible(&vha->lock);
		if (ret)
			return -ERESTARTSYS;
	}

	if (list_empty(&session->rsps)) {
		ret = 0;
		dev_err(miscdev->this_device, "%s: empty rsps!\n", __func__);
		goto out_unlock;
	}

	rsp = list_first_entry(&session->rsps, struct vha_rsp, list);
	if (rsp->size > count) {
		dev_warn(miscdev->this_device,
			"WARNING: unexpected read buffer size (%zd/%zd). "
			"Probably user space and kernel space are out of step\n",
			count, rsp->size);
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = copy_to_user(buf, &rsp->user_rsp, rsp->size);
	if (ret) {
		ret = -EFAULT;
		dev_err(miscdev->this_device, "%s: copy to user failed!\n", __func__);
		goto out_unlock;
	}

	list_del(&rsp->list);

	vha->stats.cnn_responsed++;
	session->responsed++;

	mutex_unlock(&vha->lock);
	ret = rsp->size;

#if 0
	print_hex_dump_debug("VHA RSP: ", DUMP_PREFIX_NONE,
				4, 4, (uint32_t *)&rsp->user_rsp,
				ALIGN(rsp->size, 4), false);
#endif

	kfree(rsp);

	return ret;

out_unlock:
	mutex_unlock(&vha->lock);
	return ret;
}

static ssize_t vha_read_wrapper(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	ssize_t ret = 0;
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;

#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_READ)
		current->make_it_fail = true;
	else
		current->make_it_fail = false;
#endif
	ret = vha_read(file, buf, count, ppos);

#ifdef CONFIG_FAULT_INJECTION
	if ((vha->fault_inject & VHA_FI_READ) &&
			!(vha->fault_inject & VHA_FI_UM))
		current->make_it_fail = false;
#endif

	/* Avoid leaving ioctl with interrupts disabled. */
	VHA_IRQ_FENCE();

	return ret;
}

static unsigned int vha_poll(struct file *file, poll_table *wait)
{
	unsigned long req_events = poll_requested_events(wait);
	struct vha_session *session = file->private_data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	unsigned int mask = 0;
	int ret;

	dev_dbg(miscdev->this_device, "%s: PID: %d, vha: %p, link: %p\n",
			__func__, task_pid_nr(current), vha, session);

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return POLLERR;

	if (req_events & (POLLIN | POLLRDNORM)) {
		/* Register for event */
		poll_wait(file, &session->wq, wait);
		if (session->oom)
			mask = POLLERR;
		if (!list_empty(&session->rsps))
			mask = POLLIN | POLLRDNORM;
		/* if no response item available just return 0 */
	}

	vha->stats.cnn_polled++;
	session->polled++;

	mutex_unlock(&vha->lock);
	return mask;
}

static unsigned int vha_poll_wrapper(struct file *file, poll_table *wait)
{
	unsigned int ret = 0;
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;

#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_READ)
		current->make_it_fail = true;
	else
		current->make_it_fail = false;
#endif
	ret = vha_poll(file, wait);

#ifdef CONFIG_FAULT_INJECTION
	if ((vha->fault_inject & VHA_FI_READ) &&
			!(vha->fault_inject & VHA_FI_UM))
		current->make_it_fail = false;
#endif

	/* Avoid leaving ioctl with interrupts disabled. */
	VHA_IRQ_FENCE();

	return ret;
}

/* read a message from user, and queue it up to be sent to hw */
static ssize_t vha_write(struct file *file, const char __user *buf,
		size_t size, loff_t *offset)
{
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret;
	struct vha_cmd *cmd;

	dev_dbg(miscdev->this_device,
		"%s: PID: %d, vha: %p, session: %p, size: %zu\n",
		__func__, task_pid_nr(current), vha, session, size);

	if (size < sizeof(struct vha_user_cmd)) {
		dev_err(miscdev->this_device, "%s: msg too small\n", __func__);
		return -EINVAL;
	}

	cmd = kzalloc(sizeof(*cmd) - sizeof(cmd->user_cmd) + size, GFP_KERNEL);
	if (!cmd) {
		dev_err(miscdev->this_device, "%s: kzalloc failed!\n", __func__);
		return -ENOMEM;
	}

	cmd->size = size;
	cmd->session = session;
#ifdef VHA_SCF
	init_completion(&cmd->conf_done);
#endif
	ret = copy_from_user(&cmd->user_cmd, buf, size);
	if (ret) {
		dev_err(miscdev->this_device, "%s: copy failed!\n", __func__);
		ret = -EFAULT;
		goto out_free_item;
	}

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret) {
		dev_err(miscdev->this_device, "%s: mutex lock failed <0x%x>!\n",
										__func__, ret);
		goto out_free_item;
	}

	vha->stats.cnn_add_cmds++;
	session->kicks++;
	ret = vha_add_cmd(session, cmd);
	mutex_unlock(&vha->lock);
	if (ret)
		goto out_free_item;

	return size;

out_free_item:
	kfree(cmd);
	return ret;
}

static ssize_t vha_write_wrapper(struct file *file, const char __user *buf,
		size_t size, loff_t *offset)
{
	ssize_t ret = 0;
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;

#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_WRITE)
		current->make_it_fail = true;
	else
		current->make_it_fail = false;
#endif
	ret = vha_write(file, buf, size, offset);

#ifdef CONFIG_FAULT_INJECTION
	if ((vha->fault_inject & VHA_FI_WRITE) &&
			!(vha->fault_inject & VHA_FI_UM))
		current->make_it_fail = false;
#endif

	/* Avoid leaving ioctl with interrupts disabled. */
	VHA_IRQ_FENCE();

	return ret;
}

static int vha_open(struct inode *inode, struct file *file)
{
	struct miscdevice  *miscdev = (struct miscdevice *)file->private_data;
	struct vha_dev     *vha = container_of(miscdev, struct vha_dev, miscdev);
	struct vha_session *session;
	int ret;
	uint8_t pri;

	dev_dbg(miscdev->this_device, "%s: PID: %d, vha: %p\n",
		__func__, task_pid_nr(current), vha);

	session = devm_kzalloc(miscdev->this_device, sizeof(struct vha_session),
		GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	session->vha = vha;

	/* memory context for all buffers used by this session */
	ret = img_mem_create_proc_ctx(&session->mem_ctx);
	if (ret) {
		dev_err(miscdev->this_device, "%s: failed to create context!\n",
			__func__);
		devm_kfree(miscdev->this_device, session);
		return ret;
	}

	for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++)
		INIT_LIST_HEAD(&session->cmds[pri]);
	INIT_LIST_HEAD(&session->rsps);
	INIT_LIST_HEAD(&session->bufs);
	init_waitqueue_head(&session->wq);

	file->private_data = session;

	ret = vha_add_session(session);
	if (ret) {
		img_mem_destroy_proc_ctx(session->mem_ctx);
		devm_kfree(miscdev->this_device, session);
		file->private_data = NULL;
	}

	return ret;
}

static int vha_open_wrapper(struct inode *inode, struct file *file)
{
	int ret = 0;
	struct miscdevice  *miscdev = (struct miscdevice *)file->private_data;
	struct vha_dev *vha = container_of(miscdev, struct vha_dev, miscdev);

#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_OPEN)
		current->make_it_fail = true;
	else
		current->make_it_fail = false;
#endif
	ret = vha_open(inode, file);

#ifdef CONFIG_FAULT_INJECTION
	if ((vha->fault_inject & VHA_FI_OPEN) &&
			!(vha->fault_inject & VHA_FI_UM))
		current->make_it_fail = false;
#endif

	/* Avoid leaving ioctl with interrupts disabled. */
	VHA_IRQ_FENCE();

	return ret;
}

static int vha_release(struct inode *inode, struct file *file)
{
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev     *vha     = session->vha;
	struct miscdevice  *miscdev = &vha->miscdev;

	dev_dbg(miscdev->this_device, "%s: PID: %d, vha: %p, session: %p\n",
		__func__, task_pid_nr(current), vha, session);

	vha_rm_session(session);
	img_mem_destroy_proc_ctx(session->mem_ctx);

	devm_kfree(miscdev->this_device, session);
	file->private_data = NULL;

	/* Avoid leaving ioctl with interrupts disabled. */
	VHA_IRQ_FENCE();

	return 0;
}

static long vha_ioctl_get_hw_props(struct vha_session *session,
					void __user *buf)
{
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;

	dev_dbg(miscdev->this_device, "%s: session %p\n", __func__, session);

	if (copy_to_user(buf, &vha->hw_props,
			sizeof(struct vha_hw_props))) {
		dev_err(miscdev->this_device, "%s: copy to user failed!\n",
			__func__);
		return -EFAULT;
	}

	return 0;
}

static long vha_ioctl_query_heaps(struct vha_session *session, void __user *buf)
{
	struct vha_heaps_data data;
	struct vha_dev* vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret;
	int i = 0;
	struct list_head* pos;

	dev_dbg(miscdev->this_device, "%s: session %u\n",
			__func__, session->id);

	memset(&data, 0, sizeof(data));

	list_for_each(pos, &vha->heaps) {
		struct vha_heap* heap = list_entry(pos, struct vha_heap, list);
		uint8_t type;
		uint32_t attrs;
		struct vha_heap_data *info;

		ret = img_mem_get_heap_info(heap->id, &type, &attrs);
		BUG_ON(ret != 0);
		info = &data.heaps[i++];
		info->id = heap->id;
		info->type = type;
		info->attributes = attrs;
	}
	if (copy_to_user(buf, &data, sizeof(data)))
		return -EFAULT;

	return 0;
}

static long vha_ioctl_alloc(struct vha_session *session, void __user *buf)
{
	struct vha_alloc_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret;

	if (copy_from_user(&data, buf, sizeof(data)))
		return -EFAULT;

	dev_dbg(miscdev->this_device, "%s: session %u, size %llu, heap_id %u\n",
			__func__, session->id, data.size, data.heap_id);

	if (default_mem_heap != IMG_MEM_MAN_HEAP_ID_INVALID)
		data.heap_id = default_mem_heap;

	if (list_empty(&session->bufs))
		img_pdump_printf("-- ALLOC_BEGIN\n");

	ret = img_mem_alloc(session->vha->dev,
				session->mem_ctx, data.heap_id,
				(size_t)data.size, data.attributes, &data.buf_id);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret) {
		img_mem_free(session->mem_ctx, data.buf_id);
		return ret;
	}

	ret = vha_add_buf(session, data.buf_id, (size_t)data.size,
			data.name, data.attributes);
	if (ret)
		goto out_free;

	if (copy_to_user(buf, &data, sizeof(struct vha_alloc_data)))
		goto out_rm_buf;

	mutex_unlock(&vha->lock);

	return 0;

out_rm_buf:
	vha_rm_buf(session, data.buf_id);
out_free:
	img_mem_free(session->mem_ctx, data.buf_id);

	mutex_unlock(&vha->lock);

	return -EFAULT;
}

static long vha_ioctl_import(struct vha_session *session, void __user *buf)
{
	struct vha_import_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret;

	if (copy_from_user(&data, buf, sizeof(data)))
		return -EFAULT;

	dev_dbg(miscdev->this_device, "%s: session %u, buf_fd 0x%016llx, size %llu, heap_id %u\n",
			__func__, session->id, data.buf_fd, data.size, data.heap_id);

	ret = img_mem_import(session->vha->dev, session->mem_ctx, data.heap_id,
					(size_t)data.size, data.attributes, data.buf_fd,
					data.cpu_ptr, &data.buf_id);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret) {
		img_mem_free(session->mem_ctx, data.buf_id);
		return ret;
	}

	ret = vha_add_buf(session, data.buf_id, (size_t)data.size,
				data.name, data.attributes);
	if (ret)
		goto out_free;

	if (copy_to_user(buf, &data, sizeof(struct vha_import_data)))
		goto out_rm_buf;

	mutex_unlock(&vha->lock);

	return 0;

out_rm_buf:
	vha_rm_buf(session, data.buf_id);
out_free:
	img_mem_free(session->mem_ctx, data.buf_id);

	mutex_unlock(&vha->lock);

	return -EFAULT;
}

static long vha_ioctl_export(struct vha_session *session, void __user *buf)
{
	struct vha_export_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret;

	if (copy_from_user(&data, buf, sizeof(data)))
		return -EFAULT;

	dev_dbg(miscdev->this_device, "%s: session %u, buf_id %u, size %llu\n",
			__func__, session->id, data.buf_id, data.size);

	ret = img_mem_export(session->vha->dev, session->mem_ctx, data.buf_id,
					(size_t)data.size, data.attributes, &data.buf_hnd);
	if (ret)
		return ret;

	if (copy_to_user(buf, &data, sizeof(struct vha_export_data)))
		return -EFAULT;

	return 0;
}

static long vha_ioctl_free(struct vha_session *session, void __user *buf)
{
	struct vha_free_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret;

	if (copy_from_user(&data, buf, sizeof(data))) {
		dev_err(miscdev->this_device,
			"%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}

	dev_dbg(miscdev->this_device, "%s: session %u, buf_id %u\n",
			__func__, session->id, data.buf_id);

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;
	if (!session->freeing) {
		session->freeing = true;
		img_pdump_printf("-- FREE_BEGIN\n");
	}
	vha_rm_buf(session, data.buf_id);

	img_mem_free(session->mem_ctx, data.buf_id);
	mutex_unlock(&vha->lock);

	return 0;
}

static long vha_ioctl_map_to_onchip(struct vha_session *session, void __user *buf)
{
	struct vha_map_to_onchip_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret = 0;

	if (copy_from_user(&data, buf, sizeof(data))) {
		dev_err(miscdev->this_device,
			"%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}

	dev_dbg(miscdev->this_device, "%s: session %u, virt_addr 0x%016llx, buf_id %u\n",
			__func__, session->id, data.virt_addr, data.buf_id);

	ret = vha_map_to_onchip(session, data.buf_id, data.virt_addr, data.page_size,
		data.num_pages, data.page_idxs, &data.map_id);

	if (copy_to_user(buf, &data, sizeof(data))) {
		dev_err(miscdev->this_device, "%s: copy to user failed!\n",
			__func__);
		return -EFAULT;
	}

	return ret;
}

static long vha_ioctl_map(struct vha_session *session, void __user *buf)
{
	struct vha_map_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;

	if (copy_from_user(&data, buf, sizeof(data))) {
		dev_err(miscdev->this_device,
			"%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}

	dev_dbg(miscdev->this_device, "%s: session %u, virt_addr 0x%016llx, buf_id %u, flags 0x%08x\n",
			__func__, session->id, data.virt_addr, data.buf_id, data.flags);

	return vha_map_buffer(session, data.buf_id,
				data.virt_addr, data.flags);
}

static long vha_ioctl_unmap(struct vha_session *session, void __user *buf)
{
	struct vha_unmap_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;

	if (copy_from_user(&data, buf, sizeof(data))) {
		dev_err(miscdev->this_device,
			"%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}

	if (!session->freeing) {
		session->freeing = true;
		img_pdump_printf("-- FREE_BEGIN\n");
	}

	dev_dbg(miscdev->this_device, "%s: session %u, buf_id %u\n",
			__func__, session->id, data.buf_id);

	return vha_unmap_buffer(session, data.buf_id);
}

static long vha_ioctl_buf_status(struct vha_session *session, void __user *buf)
{
	struct vha_buf_status_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret;

	if (copy_from_user(&data, buf, sizeof(data))) {
		dev_err(miscdev->this_device,
			"%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}

	dev_dbg(miscdev->this_device, "%s: session %u, buf_id %u, status %u, in_sync_fd %d, out_sync_sig %d \n",
			__func__, session->id, data.buf_id, data.status, data.in_sync_fd, data.out_sync_sig);

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	ret = vha_set_buf_status(session, data.buf_id, data.status,
			data.in_sync_fd, data.out_sync_sig);
	mutex_unlock(&vha->lock);

	return ret;
}

static long vha_ioctl_sync(struct vha_session *session, void __user *buf)
{
	struct vha_sync_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int ret = -EINVAL;

	if (copy_from_user(&data, buf, sizeof(data))) {
		dev_err(miscdev->this_device, "%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}

#ifdef KERNEL_DMA_FENCE_SUPPORT
	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	switch (data.op) {
		case VHA_SYNC_OP_CREATE_OUT:
			dev_dbg(miscdev->this_device, "%s: session %u, VHA_SYNC_OP_CREATE_OUT buf_id_count: %u\n",
					__func__, session->id, data.create_data.buf_id_count);
			if (data.create_data.buf_id_count > VHA_SYNC_MAX_BUF_IDS) {
				dev_err(miscdev->this_device, "%s: too many buf_ids provided\n",
								__func__);
				ret = -EINVAL;
			} else
				ret = vha_create_output_sync(session, data.create_data.buf_id_count,
																		data.create_data.buf_ids);
			break;
		case VHA_SYNC_OP_MERGE_IN:
			dev_dbg(miscdev->this_device, "%s: session %u, VHA_SYNC_OP_MERGE_IN in_sync_fd_count: %u\n",
					__func__, session->id, data.merge_data.in_sync_fd_count);
			if (data.merge_data.in_sync_fd_count > VHA_SYNC_MAX_IN_SYNC_FDS) {
				dev_err(miscdev->this_device, "%s: too many in_sync_fds provided\n",
								__func__);
				ret = -EINVAL;
			} else
				ret = vha_merge_input_syncs(session, data.merge_data.in_sync_fd_count,
																		data.merge_data.in_sync_fds);
			break;
		case VHA_SYNC_OP_RELEASE:
			dev_dbg(miscdev->this_device, "%s: session %u, VHA_SYNC_OP_RELEASE buf_id_count: %u\n",
					__func__, session->id, data.release_data.buf_id_count);
			if (data.release_data.buf_id_count > VHA_SYNC_MAX_BUF_IDS) {
				dev_err(miscdev->this_device, "%s: too many buf_ids provided\n",
								__func__);
				ret = -EINVAL;
			} else
				ret = vha_release_syncs(session, data.release_data.buf_id_count,
																data.release_data.buf_ids);
			break;
		default:
			break;
	}
	mutex_unlock(&vha->lock);

	if (ret < 0)
		data.sync_fd = VHA_SYNC_NONE;
	else {
		data.sync_fd = ret;
		ret = 0;
	}
#else
	data.sync_fd = VHA_SYNC_NONE;
	ret = -ENOSYS;
	dev_warn(miscdev->this_device, "%s: dma_fences not supported!\n", __func__);
#endif

	if (copy_to_user(buf, &data, sizeof(data))) {
		dev_err(miscdev->this_device, "%s: copy to user failed!\n", __func__);
		return -EFAULT;
	}

	return ret;
}

static long vha_ioctl_cancel(struct vha_session *session, void __user *buf)
{
	struct vha_cancel_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;

	if (copy_from_user(&data, buf, sizeof(data))) {
		dev_err(miscdev->this_device,
			"%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}

	dev_dbg(miscdev->this_device, "%s: session %u, cmd_id 0x%08x, cmd_id_mask 0x%08x\n",
			__func__, session->id, data.cmd_id, data.cmd_id_mask);

	return vha_rm_cmds(session, data.cmd_id, data.cmd_id_mask, data.respond);
}

static long vha_ioctl_version(struct vha_session *session,
					void __user *buf)
{
	struct vha_version_data data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;

	memset(&data, 0, sizeof(struct vha_version_data));
	memcpy(data.digest, KERNEL_INTERFACE_DIGEST, sizeof(data.digest)-1);

	dev_dbg(miscdev->this_device, "%s: session %p: interface digest:%s\n", __func__,
			session, data.digest);

	if (copy_to_user(buf, &data,
			sizeof(struct vha_version_data))) {
		dev_err(miscdev->this_device, "%s: copy to user failed!\n",
			__func__);
		return -EFAULT;
	}

	return 0;
}

static long vha_ioctl(struct file *file, unsigned int code, unsigned long value)
{
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;

	dev_dbg(miscdev->this_device, "%s: code: 0x%x, value: 0x%lx\n",
		__func__, code, value);
	switch (code) {
	case VHA_IOC_HW_PROPS:
		return vha_ioctl_get_hw_props(session, (void __user *)value);
	case VHA_IOC_QUERY_HEAPS:
		return vha_ioctl_query_heaps(session, (void __user *)value);
	case VHA_IOC_ALLOC:
		return vha_ioctl_alloc(session, (void __user *)value);
	case VHA_IOC_IMPORT:
		return vha_ioctl_import(session, (void __user *)value);
	case VHA_IOC_EXPORT:
		return vha_ioctl_export(session, (void __user *)value);
	case VHA_IOC_FREE:
		return vha_ioctl_free(session, (void __user *)value);
	case VHA_IOC_VHA_MAP_TO_ONCHIP:
		return vha_ioctl_map_to_onchip(session, (void __user *)value);
	case VHA_IOC_VHA_MAP:
		return vha_ioctl_map(session, (void __user *)value);
	case VHA_IOC_VHA_UNMAP:
		return vha_ioctl_unmap(session, (void __user *)value);
	case VHA_IOC_BUF_STATUS:
		return vha_ioctl_buf_status(session, (void __user *)value);
	case VHA_IOC_SYNC:
		return vha_ioctl_sync(session, (void __user *)value);
	case VHA_IOC_CANCEL:
		return vha_ioctl_cancel(session, (void __user *)value);
	case VHA_IOC_VERSION:
		return vha_ioctl_version(session, (void __user *)value);
	default:
		dev_err(miscdev->this_device, "%s: code %#x unknown\n",
			__func__, code);
		return -EINVAL;
	}
}

static long vha_ioctl_wrapper(struct file *file, unsigned int code, unsigned long value)
{
	long ret = 0;
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;

#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_IOCTL)
		current->make_it_fail = true;
	else
		current->make_it_fail = false;
#endif
	ret = vha_ioctl(file, code, value);

#ifdef CONFIG_FAULT_INJECTION
	if ((vha->fault_inject & VHA_FI_IOCTL) &&
			!(vha->fault_inject & VHA_FI_UM))
		current->make_it_fail = false;
#endif

	/* Avoid leaving ioctl with interrupts disabled. */
	VHA_IRQ_FENCE();

	return ret;
}

static int vha_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;
	struct miscdevice *miscdev = &vha->miscdev;
	int buf_id = vma->vm_pgoff;

	dev_dbg(miscdev->this_device,
		"%s: PID: %d start %#lx end %#lx\n",
		__func__, task_pid_nr(current),
		vma->vm_start, vma->vm_end);

	dev_dbg(miscdev->this_device, "%s: PID: %d buf_id %d\n",
		__func__, task_pid_nr(current), buf_id);
	return img_mem_map_um(session->mem_ctx, buf_id, vma);
}

static int vha_mmap_wrapper(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0;
	struct vha_session *session = (struct vha_session *)file->private_data;
	struct vha_dev *vha = session->vha;

#ifdef CONFIG_FAULT_INJECTION
	if (vha->fault_inject & VHA_FI_MMAP)
		current->make_it_fail = true;
	else
		current->make_it_fail = false;
#endif
	ret = vha_mmap(file, vma);

#ifdef CONFIG_FAULT_INJECTION
	if ((vha->fault_inject & VHA_FI_MMAP) &&
			!(vha->fault_inject & VHA_FI_UM))
		current->make_it_fail = false;
#endif

	/* Avoid leaving ioctl with interrupts disabled. */
	VHA_IRQ_FENCE();

	return ret;
}

static const struct file_operations vha_fops = {
	.owner          = THIS_MODULE,
	.read           = vha_read_wrapper,
	.poll           = vha_poll_wrapper,
	.write          = vha_write_wrapper,
	.open           = vha_open_wrapper,
	.mmap           = vha_mmap_wrapper,
	.unlocked_ioctl = vha_ioctl_wrapper,
	.compat_ioctl   = vha_ioctl_wrapper,
	.release        = vha_release,
};

#define VHA_MAX_NODE_NAME 16

int vha_api_add_dev(struct device *dev, struct vha_dev *vha, unsigned int id)
{
	int ret;
	char *dev_name = NULL;

	if (!dev || !vha) {
		pr_err("%s: invalid params!\n", __func__);
		return -EINVAL;
	}
	dev_name = devm_kzalloc(dev, VHA_MAX_NODE_NAME, GFP_KERNEL);
	if (!dev_name)
		return -ENOMEM;

	snprintf(dev_name, VHA_MAX_NODE_NAME, "vha%d", id);

	dev_dbg(dev, "%s: trying to register misc dev %s...\n",
		__func__, dev_name);

	vha->miscdev.minor = MISC_DYNAMIC_MINOR;
	vha->miscdev.fops = &vha_fops;
	vha->miscdev.name = dev_name;
	vha->id = id;

	ret = misc_register(&vha->miscdev);
	if (ret) {
		dev_err(dev, "%s: failed to register VHA misc device\n",
			__func__);
		goto out_register;
	}

	dev_dbg(dev, "%s: misc dev registered successfully\n", __func__);

	return 0;

out_register:
	devm_kfree(dev, dev_name);

	return ret;
}

int vha_api_rm_dev(struct device *dev, struct vha_dev *vha)
{
	int ret = 0;

	if (!dev || !vha) {
		pr_err("%s: invalid params!\n", __func__);
		return -EINVAL;
	}

	dev_dbg(dev, "%s: trying to deregister VHA misc device\n", __func__);

	/* note: since linux v4.3, misc_deregister does not return errors */
	misc_deregister(&vha->miscdev);

	devm_kfree(dev, (void *)vha->miscdev.name);

	dev_dbg(dev, "%s: VHA misc dev deregistered: %d\n", __func__, ret);

	return ret;
}

static int __init vha_api_init(void)
{
	int ret;

	pr_debug("loading VHA module.\n");

	ret = vha_early_init();
	if (ret)
		pr_err("failed initialize VHA driver\n");
	else {
		ret = vha_plat_init();
		if (ret)
			pr_err("failed initialize VHA driver\n");
	}
	return ret;
}

static void __exit vha_api_exit(void)
{
	int ret;

	pr_debug("unloading VHA module.\n");

	ret = vha_plat_deinit();
	if (ret)
		pr_err("failed to deinitialise VHA driver\n");
}

module_init(vha_api_init);
module_exit(vha_api_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Imagination");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,4,0)
MODULE_IMPORT_NS(IMG_MEM);
#endif

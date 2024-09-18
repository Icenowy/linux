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
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include <uapi/vha.h>
#include "vha_common.h"
#include "vha_plat.h"
#include "vha_io.h"

int debug_log_enable = 0;
module_param(debug_log_enable, int, 0644);
MODULE_PARM_DESC(
	debug_log_enable,
	"flag to on/off debug log when dynamic debug is dissable.0:disable, other :enable");

#ifdef CONFIG_DEBUG_FS

#define VHA_DBG_CONBINED_CRC_BUF_SIZE 0x1000
#define VHA_DBG_CRC_BUF_SIZE 0x2000

static uint32_t cnn_crc_size_kB;
static uint32_t cnn_dbg_size_kB;
static bool cnn_dbg_pdump_enable = true;
module_param(cnn_crc_size_kB, uint, 0444);
module_param(cnn_dbg_size_kB, uint, 0444);
module_param(cnn_dbg_pdump_enable, bool, 0444);
MODULE_PARM_DESC(cnn_crc_size_kB, "size of hw CRC buffer");
MODULE_PARM_DESC(cnn_dbg_size_kB, "size of hw DEBUG buffer");
MODULE_PARM_DESC(cnn_dbg_pdump_enable,
	"DEBUG buffer is captured into pdump file");
static uint32_t cnn_crc_mode;
static uint32_t cnn_dbg_modes[2];
module_param(cnn_crc_mode, uint, 0444);
module_param_array(cnn_dbg_modes, uint, NULL, 0444);
MODULE_PARM_DESC(cnn_crc_mode,
	"CRC CONTROL: mode for CNN_CRC_ENABLE: 0=disable 1=stream 2=layer 3=pass");
MODULE_PARM_DESC(cnn_dbg_modes,
	"DEBUG CONTROL: modes for PERF and BAND_ENABLE: 0=disable 1=stream 2=layer 3=pass");

#ifdef HW_AX3
static uint32_t cnn_crc_mask = 0;
module_param(cnn_crc_mask, uint, 0444);
MODULE_PARM_DESC(cnn_crc_mask,
	"CRC MASK: 0=no mask 1=debug silicon 2=safety critical 3=reserved");
#endif

static uint32_t cnn_pdump_flush_dbg = 1;
module_param(cnn_pdump_flush_dbg, uint, 0444);
MODULE_PARM_DESC(cnn_pdump_flush_dbg,
	"PDUMP: flushing debug buffs: 0:session,1:stream(default)");

static unsigned long vaa_offset = 0;
module_param(vaa_offset, ulong, 0444);
MODULE_PARM_DESC(vaa_offset,
	"Page aligned offset in virtual address allocator space for kernel buffers."
	" NOTE: given offset decreases the size of vaa heap, accordingly");

struct vha_dbgfs_ctx {
	struct dentry    *debugfs_dir;
#if defined VHA_EVENT_INJECT
	struct dentry    *event_inject_dir;
#endif
#if defined VHA_FUNCT_CTRL
	struct dentry    *funct_ctrl_dir;
#endif
	struct vha_regset regset;
	uint64_t          rtm_ctrl;
	uint64_t          ioreg_addr;
};

/* MMU PTE dump info */
struct vha_ptedump {
	struct vha_session *session;

	/* Actual address */
	uint64_t vaddr;
	/* Selected mmu sw context to be dumped */
	unsigned cur_cid;

	/* Configuration info */
	size_t page_size;
	size_t virt_size;
};

static void *vha_mmu_ptedump_start(struct seq_file *seq, loff_t *pos)
{
	struct vha_ptedump *ctx = seq->private;
	struct vha_session *session;

	if (!ctx)
		return NULL;

	session = ctx->session;
	if (!session)
		return NULL;

	/* Get mmu configuration info - the same one the tables were built with */
	img_mmu_get_conf(&ctx->page_size, &ctx->virt_size);
	ctx->vaddr = *pos * ctx->page_size;

	if (*pos == 0) {
		uint64_t pc_addr = img_mem_get_single_page(session->mem_ctx,
				session->mmu_ctxs[ctx->cur_cid-1].pc_bufid, 0);

		seq_printf(seq, "  Session hw_ctxid:%x -> PC addr:%#llx\n",
				session->mmu_ctxs[ctx->cur_cid-1].hw_id, pc_addr);
		seq_printf(seq, "    [ virtaddr ] [ physaddr ] [flags]\n");
	}

	return ctx;
}

static void *vha_mmu_ptedump_next(struct seq_file *seq, void *priv, loff_t *pos)
{
	struct vha_ptedump *ctx = priv;
	struct vha_session *session;

	if (!ctx)
		return NULL;

	session = ctx->session;

	(*pos)++;
	ctx->vaddr = *pos * ctx->page_size;
	if (ctx->vaddr <= (1ULL<<ctx->virt_size)-ctx->page_size)
		return ctx;

	if (ctx->cur_cid < ARRAY_SIZE(session->mmu_ctxs)) {
		/* Switch to next context & reset position */
		ctx->cur_cid++;
		*pos = 0;
		vha_mmu_ptedump_start(seq, pos);

		return ctx;
	}

	return NULL;
}

static int vha_mmu_ptedump_show(struct seq_file *seq, void *priv)
{
	struct vha_ptedump *ctx = priv;
	struct vha_session *session;
	phys_addr_t paddr;
	uint8_t flags;
	int ret = 0;

	if (!ctx)
		return -EINVAL;

	session = ctx->session;
	if (!session)
		return -EINVAL;

	if (ctx->vaddr > (1ULL<<ctx->virt_size)-ctx->page_size)
		return SEQ_SKIP;

	paddr = img_mmu_get_paddr(session->mmu_ctxs[ctx->cur_cid-1].ctx,
				ctx->vaddr, &flags);
	if (flags) {
		struct vha_buffer *buf = vha_find_bufvaddr(session, ctx->vaddr);
		seq_printf(seq, "    0x%010llx 0x%010llx 0x%04x (%s)\n",
					ctx->vaddr, paddr, flags, buf ? buf->name : "???");
	} else if (!(ctx->vaddr % 0x40000000)) {
		/*  Give some time to others, to avoid soft lockup warnings
		 *  Call yield() for every GB boundary of virtual address space */
		yield();
	}

	return ret;
}

static void vha_mmu_ptedump_stop(struct seq_file *seq, void *priv)
{
	/* Nothing to do */
}

static const struct seq_operations vha_mmu_ptedump_sops = {
	.start = vha_mmu_ptedump_start,
	.next  = vha_mmu_ptedump_next,
	.show  = vha_mmu_ptedump_show,
	.stop  = vha_mmu_ptedump_stop
};

static int vha_mmu_ptedump_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = seq_open(file, &vha_mmu_ptedump_sops);
	if (!ret) {
		struct vha_session *session;
		struct seq_file *seq;
		struct vha_ptedump *ctx;

		seq = file->private_data;

		session = inode->i_private;
		if (!session)
			return -EINVAL;

		ctx = kzalloc(sizeof(struct vha_ptedump), GFP_KERNEL);
		if (!ctx)
			return -ENOMEM;

		ctx->session = session;
		seq->private = ctx;
		ctx->cur_cid = 1;

		ret = mutex_lock_interruptible(&session->vha->lock);
	}

	return ret;
}

static int vha_mmu_ptedump_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct vha_session *session;
	struct vha_ptedump *ctx;

	if (!seq)
		return -EINVAL;

	ctx = seq->private;
	session = ctx->session;
	if (session)
		mutex_unlock(&session->vha->lock);

	kfree(ctx);

	return seq_release(inode, file);
}

static const struct file_operations vha_mmu_ptedump_fops = {
	.owner = THIS_MODULE,
	.open = vha_mmu_ptedump_open,
	.llseek  = seq_lseek,
	.release = vha_mmu_ptedump_release,
	.read    = seq_read,
};

static void *vha_buffer_dump_start(struct seq_file *seq, loff_t *pos)
{
		struct vha_session *session = seq->private;
		int ret;

		if (session == NULL) {
				pr_warn("Invalid VHA session pointer...\n");
				return  NULL;
		}

		if (list_empty(&session->bufs))
				return NULL;


		ret = mutex_lock_interruptible(&session->vha->lock);

		if (ret) {
				pr_warn("Error while trying to get vha lock (%d)...\n", ret);
				return  NULL;
		}


		seq_printf(seq, "Allocated buffers:\n");
		seq_printf(seq, "ID    Name       Size       Atributes  Status     Kptr              DevVirt     Inval?  Flush?\n");
		/*               6005  012345678  123456789  CUWSNM     SW filled            (null)  0x40002001  n       Y\n" */

		/* Then first buffer from it */
		return seq_list_start(&session->bufs, *pos);
}

static void *vha_buffer_dump_next(struct seq_file *seq, void *priv, loff_t *pos)
{
		struct vha_session *session = seq->private;
		return seq_list_next(priv, &session->bufs, pos);
}

static void vha_buffer_dump_stop(struct seq_file *seq, void *priv)
{
		struct vha_session *session = seq->private;
		mutex_unlock(&session->vha->lock);

		seq_printf(seq, "Attributes: Cached;Uncached;Writecombine;Secure;Nomap;Mmu\n");
}

static const char *BufferStatus[] = {
				"Unfilled ",
				"SW filled",
				"HW filled"
};


static int vha_buffer_dump_show(struct seq_file *seq, void *priv)
{
		const struct vha_buffer *buf = list_entry(priv, struct vha_buffer, list);

		/* ID    Name       Size       Atributes  Status     Kptr              DevVirt     Inval?  Flush? */
		seq_printf(seq, "%04u  %9s  %9ld  %c%c%c%c%c%c     %s  %p  0x%08llX  %c       %c\n",
							 buf->id,
							 buf->name,
							 buf->size,
							 (buf->attr & IMG_MEM_ATTR_CACHED)?'C':'.',
							 (buf->attr & IMG_MEM_ATTR_UNCACHED)?'U':'.',
							 (buf->attr & IMG_MEM_ATTR_WRITECOMBINE)?'W':'.',
							 (buf->attr & IMG_MEM_ATTR_SECURE)?'S':'.',
							 (buf->attr & IMG_MEM_ATTR_NOMAP)?'N':'.',
							 (buf->attr & IMG_MEM_ATTR_MMU)?'M':'.',
							 BufferStatus[buf->status],
							 buf->kptr,
							 buf->devvirt,
							 (buf->inval)?'Y':'n',
							 (buf->flush)?'Y':'n'
							);
		return 0;
}

static const struct seq_operations vha_buffer_dump_sops = {
				.start = vha_buffer_dump_start,
				.next  = vha_buffer_dump_next,
				.show  = vha_buffer_dump_show,
				.stop  = vha_buffer_dump_stop
};

static int vha_buffer_dump_open(struct inode *inode, struct file *file)
{
		struct seq_file *s;
		int err;

		err = seq_open(file, &vha_buffer_dump_sops);
		if (err)
				return err;

		s = file->private_data;

		/* i_private containt a pointer to the vha_session structure */
		s->private = inode->i_private;

		return 0;
}

static const struct file_operations vha_buffer_dump_fops = {
				.owner = THIS_MODULE,
				.open = vha_buffer_dump_open,
				.read    = seq_read,
				.llseek  = seq_lseek,
				.release = seq_release,
};

static ssize_t vha_session_mem_max_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct vha_session *session = file->private_data;
	char mem_usage[25] = { 0 };
	size_t mem_val = 0;
	size_t size;

	img_mem_get_usage(session->mem_ctx, &mem_val, NULL);
	size = snprintf(mem_usage, sizeof(mem_usage), "%ld\n", mem_val);

	return simple_read_from_buffer(buf, count, ppos, mem_usage, size);
}

static const struct file_operations vha_session_mem_max_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_session_mem_max_read,
};

static ssize_t vha_session_mem_curr_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct vha_session *session = file->private_data;
	char mem_usage[25] = { 0 };
	size_t mem_val = 0;
	size_t size;

	img_mem_get_usage(session->mem_ctx, NULL, &mem_val);
	size = snprintf(mem_usage, sizeof(mem_usage), "%ld\n", mem_val);

	return simple_read_from_buffer(buf, count, ppos, mem_usage, size);
}

static const struct file_operations vha_session_mem_curr_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_session_mem_curr_read,
};

static ssize_t vha_session_mmu_max_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct vha_session *session = file->private_data;
	char mem_usage[25] = { 0 };
	size_t mem_val = 0;
	size_t size;

	img_mmu_get_usage(session->mem_ctx, &mem_val, NULL);
	size = snprintf(mem_usage, sizeof(mem_usage), "%ld\n", mem_val);

	return simple_read_from_buffer(buf, count, ppos, mem_usage, size);
}

static const struct file_operations vha_session_mmu_max_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_session_mmu_max_read,
};

static ssize_t vha_session_mmu_curr_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct vha_session *session = file->private_data;
	char mem_usage[25] = { 0 };
	size_t mem_val = 0;
	size_t size;

	img_mmu_get_usage(session->mem_ctx, NULL, &mem_val);
	size = snprintf(mem_usage, sizeof(mem_usage), "%ld\n", mem_val);

	return simple_read_from_buffer(buf, count, ppos, mem_usage, size);
}

static const struct file_operations vha_session_mmu_curr_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_session_mmu_curr_read,
};

struct dbgfs_buf_info {
	struct vha_session *session;
	struct vha_buffer  *buf;
	struct dentry      *dbgfs;	/* file in debugfs */

};

/* debugfs read a buffer */
static ssize_t dbgfs_buf_read(struct file *file, char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct dbgfs_buf_info *info = file->private_data;
	struct vha_buffer *buf = info->buf;
	struct vha_session *session = info->session;
	int ret;

	ret = mutex_lock_interruptible(&session->vha->lock);
	if (!ret) {
		if (buf->attr & IMG_MEM_ATTR_NOMAP) {
			ret = -ENOMEM;
			dev_err(session->vha->dev, "can't read non mappable buff %x\n (%d)",
				buf->id, ret);
			goto exit;
		}

		ret = img_mem_map_km(session->mem_ctx, buf->id);
		if (ret) {
			dev_err(session->vha->dev, "failed to map buff %x to km: %d\n",
				buf->id, ret);
			ret = -ENOMEM;
			goto exit;
		}
		buf->kptr = img_mem_get_kptr(session->mem_ctx, buf->id);

		ret = simple_read_from_buffer(user_buf, count, ppos,
							 buf->kptr, buf->size);
		if (ret < 0)
			dev_err(session->vha->dev, "failed to read buff %x to km: %d\n",
				buf->id, ret);

		if (img_mem_unmap_km(session->mem_ctx, buf->id))
			dev_err(session->vha->dev,
				"%s: failed to unmap buff %x from km: %d\n",
				__func__, buf->id, ret);
exit:
		buf->kptr = NULL;
		mutex_unlock(&session->vha->lock);
	}

	return ret;
}

static const struct file_operations dbgfs_buf_fops = {
	.read = dbgfs_buf_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static void dbg_add_buf(struct vha_session *session, struct vha_buffer *buf)
{
	if (buf->name[0] && session->dbgfs) {
		char name[13] = { 0 };

		struct dbgfs_buf_info *info = kzalloc(sizeof(struct dbgfs_buf_info),
				GFP_KERNEL);

		if (!info) {
			dev_err(session->vha->dev, "%s: alloc info failed!\n", __func__);
			return;
		}

		snprintf(name, sizeof(name)-1, "%s.bin", buf->name);
		info->buf = buf;
		info->session = session;
		info->dbgfs = debugfs_create_file(name,
						 S_IRUGO, session->dbgfs,
						 info, &dbgfs_buf_fops);
		if (!info->dbgfs)
			dev_warn(session->vha->dev,
					 "%s: failed to create debugfs entry for '%s'!\n",
					 __func__, name);
		buf->dbgfs_priv = (void*)info;
	}
}

static void dbg_rm_buf(struct vha_session *session, uint32_t buf_id)
{
	struct vha_buffer *buf = vha_find_bufid(session, buf_id);
	struct dbgfs_buf_info *info;

	info = (struct dbgfs_buf_info *)buf->dbgfs_priv;
	if (info) {
		/* remove debugfs directory. NULL is safe */
		debugfs_remove(info->dbgfs);
		kfree(info);
	}
	buf->dbgfs_priv = NULL;
}

/*
 * create buffers for CRC and DEBUG (PERF and BAND).
 * Configure the hardware to use them.
 * Buffers are mapped into device mmu on demand(when map=true)
 */
int vha_dbg_alloc_hwbuf(struct vha_session *session, size_t size,
					struct vha_buffer **buffer,
					const char *name, bool map)
{
	struct vha_dev *vha = session->vha;
	struct vha_buffer *buf;
	int buf_id, ret;
	uint32_t vaddr = 0;
	size_t page_size;

	if (list_empty(&session->bufs))
		img_pdump_printf("-- ALLOC_BEGIN\n");

	img_mmu_get_conf(&page_size, NULL);

	size = 	ALIGN(size, page_size);

	ret = img_mem_alloc(vha->dev,
			session->mem_ctx,
			vha->int_heap_id,
			size,
			IMG_MEM_ATTR_WRITECOMBINE,
			&buf_id);
	if (ret)
		return ret;

	ret = vha_add_buf(session, buf_id, size,
			name, IMG_MEM_ATTR_WRITECOMBINE);
	if (ret) {
		dev_err(vha->dev, "%s: add failed!\n", __func__);
		goto out_add_failed;
	}

	buf = vha_find_bufid(session, buf_id);
	if (buf == NULL)
		goto out_no_buf;

	if (vha->mmu_mode) {
		ret = img_mmu_vaa_alloc(session->vaa_ctx,
				buf->size, &vaddr);
		if (ret) {
			dev_err(vha->dev, "%s: vaa alloc failed!\n", __func__);
			goto out_vaa_failed;
		}

		if (map) {
			ret = img_mmu_map(
					session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].ctx,
					session->mem_ctx, buf_id,
					vaddr, 0);
			if (ret) {
				dev_err(vha->dev,
						"%s: map failed!\n",
						__func__);
				goto out_map_failed;
			}

			buf->devvirt = vaddr;
			dev_dbg(vha->dev,
					"%s: mapped buf %s (%u) to %#llx:%zu\n",
				__func__,
				buf->name, buf_id,
				buf->devvirt, buf->size);
		}
	}

	*buffer = buf;

	return 0;

out_map_failed:
	img_mmu_vaa_free(session->vaa_ctx, buf->devvirt, buf->size);
out_vaa_failed:
out_no_buf:
	vha_rm_buf(session, buf->id);
out_add_failed:
	img_mem_free(session->mem_ctx, buf_id);
	return -EFAULT;
}

/* create CNN_CRC and CNN_DEBUG capture into buffers */
int vha_dbg_create_hwbufs(struct vha_session *session)
{
	struct vha_dev *vha = session->vha;
	struct vha_dbgfs_ctx *ctx =
			(struct vha_dbgfs_ctx *)vha->dbgfs_ctx;
	int ret;

	if (vha->cnn_combined_crc_enable) {
		session->cnn_dbg.cnn_crc_size_kB = cnn_crc_size_kB ? cnn_crc_size_kB :
					VHA_DBG_CRC_BUF_SIZE;
		session->cnn_dbg.cnn_crc_mode = 1; /* stream mode */
#ifdef HW_AX3
		session->cnn_dbg.cnn_crc_mask = 2; /* safety mode */
#endif
	} else {
		session->cnn_dbg.cnn_crc_mode = cnn_crc_mode;
		session->cnn_dbg.cnn_crc_size_kB = cnn_crc_size_kB;
#ifdef HW_AX3
		session->cnn_dbg.cnn_crc_mask = cnn_crc_mask;
#endif
	}

	memcpy(session->cnn_dbg.cnn_dbg_modes, cnn_dbg_modes, sizeof(cnn_dbg_modes));
	session->cnn_dbg.cnn_dbg_size_kB = cnn_dbg_size_kB;
	session->cnn_dbg.cnn_dbg_flush = cnn_pdump_flush_dbg;
	session->cnn_dbg.cnn_dbg_pdump_enable = cnn_dbg_pdump_enable;

	if (vha->mmu_mode &&
		((session->cnn_dbg.cnn_crc_mode > 0 && session->cnn_dbg.cnn_crc_size_kB > 0) ||
		 (session->cnn_dbg.cnn_crc_size_kB > 0) || vha->cnn_combined_crc_enable)) {
		if (vaa_offset & (PAGE_SIZE-1)) {
			dev_err(vha->dev, "%s: given vaa offset is not page aligned!\n",
				__func__);
			return -EINVAL;
		}

		ret = img_mmu_vaa_create(vha->dev,
				IMG_MEM_VA_HEAP1_BASE + vaa_offset,
				IMG_MEM_VA_HEAP1_SIZE - vaa_offset,
				&session->vaa_ctx);
		if (ret) {
			dev_err(vha->dev, "%s: failed to allocate vaa heap\n",
				__func__);
			return ret;
		}
	}

	/* Create debugfs dir and populate entries */
	if (ctx->debugfs_dir) {
		char name[15] = { 0 };

		snprintf(name, sizeof(name)-1, "%s%d",
				"session",
				session->mmu_ctxs[VHA_MMU_REQ_MODEL_CTXID].id);

		session->dbgfs =
			debugfs_create_dir(name, ctx->debugfs_dir);

		if (session->dbgfs) {
			if (!debugfs_create_file("pte_dump", S_IRUGO, session->dbgfs,
						session, &vha_mmu_ptedump_fops))
				dev_warn(vha->dev,
					"%s: failed to create pte_dump!\n",
					__func__);
			if (!debugfs_create_file("mem_usage_max", S_IRUGO, session->dbgfs,
						session, &vha_session_mem_max_fops))
				dev_warn(vha->dev,
					"%s: failed to create mem_usage_max!\n",
					__func__);
			if (!debugfs_create_file("mem_usage_curr", S_IRUGO, session->dbgfs,
						session, &vha_session_mem_curr_fops))
				dev_warn(vha->dev,
					"%s: failed to create mem_usage_curr!\n",
					__func__);
			if (!debugfs_create_file("mmu_usage_max", S_IRUGO, session->dbgfs,
						session, &vha_session_mmu_max_fops))
				dev_warn(vha->dev,
					"%s: failed to create mmu_usage_max!\n",
					__func__);
			if (!debugfs_create_file("mmu_usage_curr", S_IRUGO, session->dbgfs,
						session, &vha_session_mmu_curr_fops))
				dev_warn(vha->dev,
					"%s: failed to create mmu_usage_curr!\n",
					__func__);

			if (!debugfs_create_file("buffer_dump", S_IRUGO, session->dbgfs,
								session, &vha_buffer_dump_fops))
					dev_warn(vha->dev,
							"%s: failed to create buffer_dump!\n",
							__func__);

			debugfs_create_u64("sess_kicks", S_IRUGO, session->dbgfs, &session->kicks);
			debugfs_create_u64("sess_polled", S_IRUGO, session->dbgfs, &session->polled);
			debugfs_create_u64("sess_responsed", S_IRUGO, session->dbgfs, &session->responsed);
		}
	}

	if (session->cnn_dbg.cnn_crc_mode > 0 && session->cnn_dbg.cnn_crc_size_kB > 0) {
		struct vha_buffer *buf;
		size_t size = session->cnn_dbg.cnn_crc_size_kB * 1024;
		int id;

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			char name[10] = { 0 };
			snprintf(name, sizeof(name)-1, "CRC_%u", id);
			ret = vha_dbg_alloc_hwbuf(session, size, &buf, name, true);
			if (ret) {
				dev_err(vha->dev, "%s: failed to allocate buffer for CNN_CRC\n",
						__func__);
				goto out_disable;
			}
			session->cnn_dbg.cnn_crc_buf[id] = buf;
			dbg_add_buf(session, buf);
		}
	}

	if (cnn_dbg_size_kB > 0) {
		struct vha_buffer *buf;
		size_t size = cnn_dbg_size_kB * 1024;
		int id;

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			char name[10] = { 0 };
			snprintf(name, sizeof(name)-1, "DBG_%u", id);
			ret = vha_dbg_alloc_hwbuf(session, size, &buf, name, true);
			if (ret) {
				dev_err(vha->dev, "%s: failed to allocate buffer for CNN_DEBUG\n",
						__func__);
				goto out_disable;
			}
			session->cnn_dbg.cnn_dbg_buf[id] = buf;
			dbg_add_buf(session, buf);
		}
	}

	if (vha->cnn_combined_crc_enable) {
		struct vha_buffer *buf;
		ret = vha_dbg_alloc_hwbuf(session, VHA_DBG_CONBINED_CRC_BUF_SIZE, &buf,
				"CRC_Cmb", true);
		if (ret) {
			dev_err(vha->dev, "%s: failed to allocate buffer for CRC_Cmb\n",
					__func__);
			goto out_disable;
		}
		session->cnn_dbg.cnn_combined_crc = buf;
		dbg_add_buf(session, buf);
		if (buf->kptr == NULL) {
			ret = img_mem_map_km(session->mem_ctx, buf->id);
			if (ret) {
				dev_err(session->vha->dev,
					"%s: failed to map buff %x to km: %d\n",
					__func__, buf->id, ret);
				return ret;
			}
			buf->kptr = img_mem_get_kptr(session->mem_ctx, buf->id);
		}
	}

	return 0;

out_disable:
	vha_dbg_destroy_hwbufs(session);
	return ret;
}

void vha_dbg_hwbuf_cleanup(struct vha_session *session,
		struct vha_buffer *buf)
{
	struct vha_dev *vha = session->vha;
	if (buf == NULL)
		return;

	if (vha->mmu_mode) {
		img_mmu_vaa_free(session->vaa_ctx, buf->devvirt, buf->size);
		img_mmu_unmap(session->mmu_ctxs[VHA_MMU_REQ_IO_CTXID].ctx,
				session->mem_ctx, buf->id);
	}
	dbg_rm_buf(session, buf->id);
	vha_rm_buf(session, buf->id);
	img_mem_free(session->mem_ctx, buf->id);
}

/* free the CRC and DEBUG buffers */
void vha_dbg_destroy_hwbufs(struct vha_session *session)
{
	struct vha_dev *vha = session->vha;

	if (session->cnn_dbg.cnn_combined_crc) {
		vha_dbg_hwbuf_cleanup(session, session->cnn_dbg.cnn_combined_crc);
	}

	if (session->cnn_dbg.cnn_crc_buf[0]) {
		int id;

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			struct vha_buffer *buf = session->cnn_dbg.cnn_crc_buf[id];
			vha_dbg_hwbuf_cleanup(session, buf);
		}
	}
	if (session->cnn_dbg.cnn_dbg_buf[0]) {
		int id;

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			struct vha_buffer *buf = session->cnn_dbg.cnn_dbg_buf[id];
			vha_dbg_hwbuf_cleanup(session, buf);
		}
	}

	if (vha->mmu_mode && session->vaa_ctx)
		img_mmu_vaa_destroy(session->vaa_ctx);

	/* remove debugfs directory. NULL is safe */
	debugfs_remove_recursive(session->dbgfs);
}

static int _show_vha_regset(struct seq_file *s, void *data)
{
	struct vha_regset *regset = s->private;
	struct vha_dev *vha = regset->vha;
	const struct vha_reg *reg = regset->regs;
	char str[150];
	int i;
	int ret;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

#ifndef VHA_FORCE_IO_DEBUG
	if (vha->state == VHA_STATE_OFF) {
		dev_err(vha->dev, "%s: can't access disabled device!!\n", __func__);
		mutex_unlock(&vha->lock);
		return -EIO;
	}
#endif

	for (i = 0; i < regset->nregs; i++, reg++) {
		uint64_t val;

		if (reg->name == NULL)
			break;

		val = IOREAD64(vha->reg_base, reg->offset);
		sprintf(str, "%s(0x%04x) = 0x%016llx",
				reg->name, reg->offset, val);
		if (val & ~reg->mask)
			strcat(str, " Bogus register value detected !!!");
		strcat(str, "\n");
		seq_puts(s, str);
	}

	mutex_unlock(&vha->lock);
	return 0;
}

static int _open_vha_regset(struct inode *inode, struct file *file)
{
	return single_open(file, _show_vha_regset, inode->i_private);
}

static const struct file_operations vha_regset_fops = {
	.open = _open_vha_regset,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* List of predefined registers to be shown in debugfs */
extern const struct vha_reg vha_regs[];

static ssize_t vha_cnn_utilization_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	char utilization[20] = { 0 };
	size_t bytes = 0;

	if (count < strlen(utilization))
		return -EINVAL;

	if (*ppos)
		return 0;

	snprintf(utilization, (int)sizeof(utilization)-1, "%d.%d[%%]\n",
			vha->stats.cnn_utilization / 10,
			vha->stats.cnn_utilization % 10);

	if (copy_to_user(buf, utilization,
			strlen(utilization))) {
		dev_err(vha->dev, "%s: cnn_utilization read: copy to user failed\n",
				__func__);
		return -EFAULT;
	}

	bytes = strlen(utilization);
	*ppos = bytes;

	return bytes;
}

static const struct file_operations vha_cnn_utilization_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_cnn_utilization_read,
};

static ssize_t vha_cnn_last_cycles_read(struct file *file, char __user *buf, size_t len,
					 loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	char cycles[16];
	size_t size;

#if defined(HW_AX2)
	/* For Mirage cnn_last_cycles holds a valid value only
	 * when we set WDT per segment */
#define WDT_CTRL_MASK (3)
#define	WDT_CTRL_KICK_PASS (1)
	if ((vha->wdt_mode & WDT_CTRL_MASK) ==
				WDT_CTRL_KICK_PASS)
		size = snprintf(cycles, sizeof(cycles), "n/a\n");
	else
#elif defined(HW_AX3) && !defined(CONFIG_HW_MULTICORE)
	/* For Aura cnn_last_cycles holds a valid value only
	 * when debug mode is turned on to collect performance data per segment
	 * VHA_CR_CNN_DEBUG_CTRL_STREAM */
#define DEBUG_CTRL_STREAM (1)
	if (cnn_dbg_modes[0] != DEBUG_CTRL_STREAM)
		size = snprintf(cycles, sizeof(cycles), "n/a\n");
	else
#endif
		size = snprintf(cycles, sizeof(cycles), "%lld\n",
				vha->stats.cnn_last_cycles);

	return simple_read_from_buffer(buf, len, ppos, cycles, size);
}

static const struct file_operations vha_cnn_last_cycles_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_cnn_last_cycles_read,
};

static ssize_t vha_bvnc_read(struct file *file, char __user *buf, size_t len,
					 loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	char bvnc[4*6];

	size_t size = snprintf(bvnc, sizeof(bvnc), "%llu.%llu.%llu.%llu\n",
					core_id_quad(vha->hw_props.core_id));

	return simple_read_from_buffer(buf, len, ppos, bvnc, size);
}

static const struct file_operations vha_bvnc_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_bvnc_read,
};

static ssize_t vha_pri_q_counters_read(struct file *file, char __user *buf, size_t len,
					 loff_t *ppos)
{
#define MAX_ENTRY_LEN 20

	struct vha_dev *vha = file->private_data;
	int ret;
	char pri_q_counters[VHA_MAX_PRIORITIES * MAX_ENTRY_LEN + 1] = "";
	char pri_q_counter[MAX_ENTRY_LEN] = "";
	char *str = pri_q_counters;

	ret = mutex_lock_interruptible(&vha->lock);
	if (!ret) {
		size_t size = 0;
		uint8_t pri;

		for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++) {
			size += snprintf(pri_q_counter, MAX_ENTRY_LEN,
							"pri %u: %u\n", pri, vha->pri_q_counters[pri]);
			strncat(str, pri_q_counter, MAX_ENTRY_LEN);
		}

		mutex_unlock(&vha->lock);

		return simple_read_from_buffer(buf, len, ppos, pri_q_counters, size);
	}

	return ret;

#undef MAX_ENTRY_LEN
}

static const struct file_operations vha_pri_q_counters_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_pri_q_counters_read,
};

/* Real Time Monitor facilities.
 * It allows to peek hw internals. Please refer to TRM */
static ssize_t vha_rtm_read(struct file *file, char __user *buf, size_t len,
					 loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	struct vha_dbgfs_ctx *ctx =
			(struct vha_dbgfs_ctx *)vha->dbgfs_ctx;
	int ret;
	char rtm[23];
	uint64_t rtm_data;

	ret = mutex_lock_interruptible(&vha->lock);
	if (!ret) {
		size_t size;

#ifndef VHA_FORCE_IO_DEBUG
		if (vha->state == VHA_STATE_OFF) {
			dev_err(vha->dev, "%s: can't access disabled device!!\n", __func__);
			mutex_unlock(&vha->lock);
			return -EIO;
		}
#endif

		rtm_data = vha_dbg_rtm_read(vha, ctx->rtm_ctrl);
		size = snprintf(rtm, sizeof(rtm), "%#.8llx %#.8llx\n",
			ctx->rtm_ctrl, rtm_data);

		mutex_unlock(&vha->lock);

		return simple_read_from_buffer(buf, len, ppos, rtm, size);
	}

	return ret;
}

static const struct file_operations vha_rtm_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_rtm_read,
};


/* Generic IO access facilities.
 * It allows read/write any register in the address space */
static ssize_t vha_ioreg_read(struct file *file, char __user *buf, size_t len,
					 loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	struct vha_dbgfs_ctx *ctx =
			(struct vha_dbgfs_ctx *)vha->dbgfs_ctx;
	char data[32];
	uint64_t io_data;
	size_t size;
	int ret;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	if (ctx->ioreg_addr >= vha->reg_size) {
		dev_err(vha->dev,
				"%s: read attempt beyond reg space (%#llx >= %#llx)!\n",
				__func__, ctx->ioreg_addr, vha->reg_size);
		mutex_unlock(&vha->lock);
		return -EINVAL;
	}

#ifndef VHA_FORCE_IO_DEBUG
	if (vha->state == VHA_STATE_OFF) {
		dev_err(vha->dev, "%s: can't access disabled device!!\n", __func__);
		mutex_unlock(&vha->lock);
		return -EIO;
	}
#endif

	/* Read the data */
	io_data = IOREAD64(vha->reg_base, ctx->ioreg_addr);
	mutex_unlock(&vha->lock);

	size = snprintf(data, sizeof(data), "%#.8llx::%#.16llx\n",
			ctx->ioreg_addr, io_data);

	return simple_read_from_buffer(buf, len, ppos, data, size);
}

static ssize_t vha_ioreg_write(struct file *file, const char __user *buf,
		size_t len, loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	struct vha_dbgfs_ctx *ctx =
			(struct vha_dbgfs_ctx *)vha->dbgfs_ctx;
	uint64_t io_data;
	int ret = kstrtou64_from_user(buf, len, 16, &io_data);
	if (ret)
		return ret;

	ret = mutex_lock_interruptible(&vha->lock);
	if (ret)
		return ret;

	if (ctx->ioreg_addr >= vha->reg_size) {
		dev_err(vha->dev,
				"%s: write attempt beyond reg space (%#llx >= %#llx)!\n",
				__func__, ctx->ioreg_addr, vha->reg_size);
		mutex_unlock(&vha->lock);
		return -EINVAL;
	}

#ifndef VHA_FORCE_IO_DEBUG
	if (vha->state == VHA_STATE_OFF) {
		dev_err(vha->dev, "%s: can't access disabled device!!\n", __func__);
		mutex_unlock(&vha->lock);
		return -EIO;
	}
#endif

	/* Write the data */
	IOWRITE64(vha->reg_base, ctx->ioreg_addr, io_data);
	mutex_unlock(&vha->lock);

	return len;
}

static const struct file_operations vha_ioreg_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_ioreg_read,
	.write = vha_ioreg_write,
};

static ssize_t vha_stats_reset_write(struct file *file, const char __user *buf,
		 size_t count, loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;

	memset(&vha->stats, 0, sizeof(struct vha_stats));

	return count;
}

static const struct file_operations vha_stats_reset_fops = {
	.owner = THIS_MODULE,
	.write = vha_stats_reset_write,
	.open = simple_open,
};

#ifdef CONFIG_HW_MULTICORE
/* Per core scheduling stats. */
static ssize_t vha_cnn_kicks_per_core_read(struct file *file, char __user *buf,
		size_t len, loff_t *ppos)
{
#define MAX_CORE_REPORT_LEN 76
#define MAX_REPORT_LEN ((2 * VHA_NUM_CORES + 1) * MAX_CORE_REPORT_LEN)
#define MAX_STAT_NUM 5

	struct vha_dev *vha = file->private_data;
	int ret;
	char* kicks_per = kmalloc(MAX_REPORT_LEN, GFP_KERNEL);

	if (kicks_per == NULL) {
		dev_err(vha->dev,
				"%s: failed to allocate memory for stats!\n", __func__);
		return -ENOMEM;
	}

	ret = mutex_lock_interruptible(&vha->lock);
	if (!ret) {
		char report_line[MAX_CORE_REPORT_LEN];
		char core_report_fmt[MAX_CORE_REPORT_LEN] = "core%u: %10u";
		char wm_report_fmt[MAX_CORE_REPORT_LEN]   = "WM%u:   %10u";
		size_t size = 0;
		uint8_t id;
		uint8_t stat_id;
		char* include_queued  = "";
		char* include_cancels = "";
		char* include_aborts  = "";
		uint32_t stats[MAX_STAT_NUM] = {0};
		ssize_t read_ret;

		/* Init stats message. */
		kicks_per[0] = 0;

		/* Check if queued WLs need to be included. */
		switch (vha->low_latency) {
		case VHA_LL_SW_KICK:
			include_queued = "      queued";
			break;
		case VHA_LL_SELF_KICK:
			include_queued = "  selfkicked";
			break;
		default:
			break;
		}
		if (strlen(include_queued) > 0) {
			strncat(core_report_fmt, "  %10u", MAX_CORE_REPORT_LEN);
			strncat(wm_report_fmt, "  %10u", MAX_CORE_REPORT_LEN);
		}

		/* Check if any cancels were recorded. */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			if (vha->stats.wm_stats[id].kicks_cancelled > 0) {
				include_cancels = "   cancelled";
				strncat(core_report_fmt, "  %10u", MAX_CORE_REPORT_LEN);
				strncat(wm_report_fmt, "  %10u", MAX_CORE_REPORT_LEN);
				break;
			}
		/* Check if any aborts were recorded. */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++)
			if (vha->stats.wm_stats[id].kicks_aborted > 0) {
				include_aborts = "     aborted";
				strncat(core_report_fmt, "  %10u", MAX_CORE_REPORT_LEN);
				strncat(wm_report_fmt, "  %10u", MAX_CORE_REPORT_LEN);
				break;
			}
		/* Add completed WLs. */
		strncat(core_report_fmt, "  %10u\n", MAX_CORE_REPORT_LEN);
		strncat(wm_report_fmt, "  %10u\n", MAX_CORE_REPORT_LEN);

		/* Create report header. */
		size += snprintf(kicks_per, MAX_REPORT_LEN,
						"            total%s%s%s   completed\n",
						include_queued, include_cancels, include_aborts);

		/* Create core report. */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			stat_id = 0;
			stats[stat_id] = vha->stats.core_stats[id].kicks;
			stat_id++;
			if (strlen(include_queued) > 0) {
				stats[stat_id] = vha->stats.core_stats[id].kicks_queued;
				stat_id++;
			}
			if (strlen(include_cancels) > 0) {
				stats[stat_id] = vha->stats.core_stats[id].kicks_cancelled;
				stat_id++;
			}
			if (strlen(include_aborts) > 0) {
				stats[stat_id] = vha->stats.core_stats[id].kicks_aborted;
				stat_id++;
			}
			stats[stat_id] = vha->stats.core_stats[id].kicks_completed;
			size += snprintf(report_line, MAX_CORE_REPORT_LEN,
							core_report_fmt, id,
							stats[0], stats[1], stats[2], stats[3], stats[4]);
			strncat(kicks_per, report_line, MAX_REPORT_LEN);
		}
		/* Create WM report. */
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			stat_id = 0;
			stats[stat_id] = vha->stats.wm_stats[id].kicks;
			stat_id++;
			if (strlen(include_queued) > 0) {
				stats[stat_id] = vha->stats.wm_stats[id].kicks_queued;
				stat_id++;
			}
			if (strlen(include_cancels) > 0) {
				stats[stat_id] = vha->stats.wm_stats[id].kicks_cancelled;
				stat_id++;
			}
			if (strlen(include_aborts) > 0) {
				stats[stat_id] = vha->stats.wm_stats[id].kicks_aborted;
				stat_id++;
			}
			stats[stat_id] = vha->stats.wm_stats[id].kicks_completed;
			size += snprintf(report_line, MAX_CORE_REPORT_LEN,
							wm_report_fmt, id,
							stats[0], stats[1], stats[2], stats[3], stats[4]);
			strncat(kicks_per, report_line, MAX_REPORT_LEN);
		}

		mutex_unlock(&vha->lock);

		read_ret = simple_read_from_buffer(buf, len, ppos, kicks_per, size);
		kfree(kicks_per);

		return read_ret;
	}

#undef MAX_CORE_REPORT_LEN
#undef MAX_REPORT_LEN
#undef MAX_STAT_NUM

	return ret;
}

static const struct file_operations vha_cnn_kicks_per_core_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_cnn_kicks_per_core_read,
};

/* Per core utilization stats. */
static ssize_t vha_cnn_utilization_per_core_read(struct file *file,
		char __user *buf, size_t len, loff_t *ppos)
{
#define MAX_CORE_REPORT_LEN 24
#define MAX_REPORT_LEN ((2 * VHA_NUM_CORES) * MAX_CORE_REPORT_LEN)

	struct vha_dev *vha = file->private_data;
	int ret;
	char utilization_per[MAX_REPORT_LEN] = "";

	ret = mutex_lock_interruptible(&vha->lock);
	if (!ret) {
		char core_report_line[MAX_CORE_REPORT_LEN];
		size_t size = 0;
		uint8_t id;

		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			size += snprintf(core_report_line, MAX_CORE_REPORT_LEN,
							"core%u: %d.%d[%%]\n",
							id,
							vha->stats.core_stats[id].utilization / 10,
							vha->stats.core_stats[id].utilization % 10);
			strcat(utilization_per, core_report_line);
		}
		for (id = 0; id < vha->hw_props.num_cnn_core_devs; id++) {
			size += snprintf(core_report_line, MAX_CORE_REPORT_LEN,
							"WM%u:   %d.%d[%%]\n",
							id,
							vha->stats.wm_stats[id].utilization / 10,
							vha->stats.wm_stats[id].utilization % 10);
			strcat(utilization_per, core_report_line);
		}

		mutex_unlock(&vha->lock);

		return simple_read_from_buffer(buf, len, ppos, utilization_per, size);
	}

#undef MAX_CORE_REPORT_LEN
#undef MAX_REPORT_LEN

	return ret;
}

static const struct file_operations vha_cnn_utilization_per_core_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_cnn_utilization_per_core_read,
};

/* Last processed WL stats. */
static ssize_t vha_wl_last_stats_read(struct file *file,
		char __user *buf, size_t len, loff_t *ppos)
{
#define MAX_WL_REPORT_LEN (16 * 30 + 11)

	struct vha_dev *vha = file->private_data;
	int ret;
	char wl_stats_txt[MAX_WL_REPORT_LEN] = "";

	ret = mutex_lock_interruptible(&vha->lock);
	if (!ret) {
		size_t size = 0;

		size = snprintf(wl_stats_txt, MAX_WL_REPORT_LEN,
						"cycles:           %llu\n"
						"LOCM rd trans:    %u\n"
						"LOCM wr trans:    %u\n"
						"LOCM mwr trans:   %u\n"
						"SOCM rd trans:    %u\n"
						"SOCM wr trans:    %u\n"
						"SOCM mwr trans:   %u\n"
						"DDR rd trans:     %u\n"
						"DDR wr trans:     %u\n"
						"DDR mwr trans:    %u\n"
						"LOCM read words:  %u\n"
						"LOCM write words: %u\n"
						"SOCM read words:  %u\n"
						"SOCM write words: %u\n"
						"DDR read words:   %u\n"
						"DDR write words:  %u\n",
						vha->stats.cnn_last_cycles,
						vha->stats.last_mem_stats.locm_rd_transactions,
						vha->stats.last_mem_stats.locm_wr_transactions,
						vha->stats.last_mem_stats.locm_mwr_transactions,
						vha->stats.last_mem_stats.socm_rd_transactions,
						vha->stats.last_mem_stats.socm_wr_transactions,
						vha->stats.last_mem_stats.socm_mwr_transactions,
						vha->stats.last_mem_stats.ddr_rd_transactions,
						vha->stats.last_mem_stats.ddr_wr_transactions,
						vha->stats.last_mem_stats.ddr_mwr_transactions,
						vha->stats.last_mem_stats.locm_rd_words,
						vha->stats.last_mem_stats.locm_wr_words,
						vha->stats.last_mem_stats.socm_rd_words,
						vha->stats.last_mem_stats.socm_wr_words,
						vha->stats.last_mem_stats.ddr_rd_words,
						vha->stats.last_mem_stats.ddr_wr_words);

		mutex_unlock(&vha->lock);

		return simple_read_from_buffer(buf, len, ppos, wl_stats_txt, size);
	}

#undef MAX_WL_REPORT_LEN

	return ret;
}

static const struct file_operations vha_wl_last_stats_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_wl_last_stats_read,
};

/* Scheduling stats. */
static ssize_t vha_scheduling_stats_read(struct file *file,
		char __user *buf, size_t len, loff_t *ppos)
{
#define MAX_PRI_SCHED_REPORT_LEN 40
#define MAX_SCHED_REPORT_LEN ((VHA_MAX_PRIORITIES + 1) * 40)

	struct vha_dev *vha = file->private_data;
	int ret;
	char sched_stats_txt[MAX_SCHED_REPORT_LEN] = "";
	char sched_pri_txt[MAX_PRI_SCHED_REPORT_LEN] = "";
	uint8_t pri;

	ret = mutex_lock_interruptible(&vha->lock);
	if (!ret) {
		size_t size = 0;

		size = snprintf(sched_stats_txt, MAX_SCHED_REPORT_LEN,
						"mean time from submit to kick [ns]:\n");
		for (pri = 0; pri < VHA_MAX_PRIORITIES; pri++) {
			size += snprintf(sched_pri_txt, MAX_PRI_SCHED_REPORT_LEN,
							"priority %u: %10llu\n",
							pri, vha->stats.sched_stats.mt_submit_to_kick_ns[pri]);
			strcat(sched_stats_txt, sched_pri_txt);
		}

		mutex_unlock(&vha->lock);

		return simple_read_from_buffer(buf, len, ppos, sched_stats_txt, size);
	}

#undef MAX_PRI_SCHED_REPORT_LEN
#undef MAX_SCHED_REPORT_LEN

	return ret;
}

static const struct file_operations vha_scheduling_stats_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_scheduling_stats_read,
};

static ssize_t vha_sched_read(struct file *file, char __user *buf,
		size_t len, loff_t *ppos)
{
#define MAX_ENTRY_LEN 8

	struct vha_dev *vha = file->private_data;
	char entries[VHA_MC_SCHED_SEQ_LEN_MAX * MAX_ENTRY_LEN] = { 0 };
	char entry[MAX_ENTRY_LEN] = { 0 };
	char *str = entries;
	size_t size = 0;
	int i;

	for (i = 0; i < vha->scheduling_sequence_len; i++) {
		size += snprintf(entry, MAX_ENTRY_LEN, "0x%04x%c",
				vha->scheduling_sequence[i],
				i == vha->scheduling_sequence_len -1 ? '\n' : ',');
		str = strncat(str, entry, MAX_ENTRY_LEN);
	}

#undef MAX_ENTRY_LEN

	return simple_read_from_buffer(buf, len, ppos, entries, size);
}

static ssize_t vha_sched_write(struct file *file, const char __user *buf,
		size_t len, loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	char *str, *str_aux, *s;
	int ret, i = 0;

	str = kzalloc(len, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	ret = copy_from_user(str, buf, len);
	if (ret)
		goto exit;
	str_aux = str;

	/* Expected format 0x0003, 0x010c, 0x000A ...
	 * Zero value stops parsing */
	while((s = strsep(&str_aux,",")) != NULL) {
		uint16_t entry;

		ret = kstrtou16(s, 16, &entry);
		if (ret)
			goto exit;

		if (entry == 0)
			break;

		if (i < VHA_MC_SCHED_SEQ_LEN_MAX)
			vha->scheduling_sequence[i++] = entry;
		else {
			ret = -EINVAL;
			goto exit;
		}
	}

	if (!vha_dev_dbg_params_check(vha)) {
		ret = -EINVAL;
		goto exit;
	}

	vha->scheduling_sequence_len = i;
	vha->scheduling_counter = 0;
	kfree(str);
	return len;

exit:
	vha->scheduling_sequence_len = 0;
	kfree(str);
	return ret;
}

static const struct file_operations vha_sched_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = vha_sched_read,
	.write = vha_sched_write,
};

static ssize_t vha_stalling_read(struct file *file, char __user *buf,
		size_t len, loff_t *ppos)
{
#define MAX_STALLING_DATA_TXT_LEN 20

	struct vha_dev *vha = file->private_data;
	char stalling_str[MAX_STALLING_DATA_TXT_LEN] = { 0 };
	size_t size = 0;

	size = snprintf(stalling_str, MAX_STALLING_DATA_TXT_LEN, "0x%04x,0x%08x\n",
					vha->stalling_sysbus_host_stall_ratio,
					vha->stalling_membus_sys_stall_ratio);

#undef MAX_STALLING_DATA_TXT_LEN

	return simple_read_from_buffer(buf, len, ppos, stalling_str, size);
}

static ssize_t vha_stalling_write(struct file *file, const char __user *buf,
		size_t len, loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	char *stalling_str, *stalling_str_aux, *stalling_token;
	uint32_t stalling_data = 0;
	int ret;

	stalling_str = kzalloc(len, GFP_KERNEL);
	if (!stalling_str)
		return -ENOMEM;

	ret = copy_from_user(stalling_str, buf, len);
	if (ret)
		goto exit;
	stalling_str_aux = stalling_str;

	/* Expected format 0x0000,0x00000000 */
	if ((stalling_token = strsep(&stalling_str_aux,",")) != NULL) {
		ret = kstrtou32(stalling_token, 16, &stalling_data);
		if (ret)
			goto exit;

		vha->stalling_sysbus_host_stall_ratio = stalling_data;
	}
	if ((stalling_token = strsep(&stalling_str_aux,",")) != NULL) {
		ret = kstrtou32(stalling_token, 16, &stalling_data);
		if (ret)
			goto exit;

		vha->stalling_membus_sys_stall_ratio = stalling_data;
	}

	kfree(stalling_str);
	return len;

exit:
	vha->stalling_sysbus_host_stall_ratio = 0;
	vha->stalling_membus_sys_stall_ratio = 0;
	kfree(stalling_str);
	return ret;
}

static const struct file_operations vha_stalling_fops = {
	.owner = THIS_MODULE,
	.open  = simple_open,
	.read  = vha_stalling_read,
	.write = vha_stalling_write,
};

#endif

void vha_dbg_init(struct vha_dev *vha)
{
	struct vha_dbgfs_ctx *ctx = devm_kzalloc(vha->dev,
			sizeof(struct vha_dbgfs_ctx), GFP_KERNEL);
	if (!ctx) {
		dev_err(vha->dev,
				"%s: Out of memory when creating debugfs context!\n",
				__func__);
		return;
	}

	/* Create userspace node */
	ctx->debugfs_dir = debugfs_create_dir(vha->miscdev.name, NULL);
	if (!ctx->debugfs_dir) {
		dev_warn(vha->dev,
				"%s: Probably debugfs not enabled in this kernel!\n",
				__func__);
		return;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
#define VHA_DBGFS_CREATE_(_type_, _name_, _vha_dev_member_, flags, dir) \
	{ \
			struct dentry *dentry; \
			debugfs_create_##_type_(_name_, \
				(flags), ctx->dir, \
				&vha->_vha_dev_member_); \
			dentry = debugfs_lookup(_name_, ctx->dir); \
			if (!dentry) { \
				dev_warn(vha->dev, \
					"%s: failed to create %s dbg file!\n", \
					__func__, _name_); \
			} else { \
				dput(dentry); \
			} \
	}
#else
#define VHA_DBGFS_CREATE_(_type_, _name_, _vha_dev_member_, flags, dir) \
	{ \
			if (!debugfs_create_##_type_(_name_, \
				(flags), ctx->dir, \
				&vha->_vha_dev_member_)) { \
				dev_warn(vha->dev, \
					"%s: failed to create %s dbg file!\n", \
					__func__, _name_); \
			} \
	}
#endif

#define VHA_DBGFS_CREATE_RO(_type_, _name_, _vha_dev_member_, dir) \
  VHA_DBGFS_CREATE_(_type_, _name_, _vha_dev_member_, S_IRUGO, dir)

#define VHA_DBGFS_CREATE_RW(_type_, _name_, _vha_dev_member_, dir) \
  VHA_DBGFS_CREATE_(_type_, _name_, _vha_dev_member_, S_IWUSR|S_IRUGO, dir)


#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
#define CTX_DBGFS_CREATE_RW(_type_, _name_, _ctx_dev_member_, dir) \
	{ \
			struct dentry *dentry; \
			debugfs_create_##_type_(_name_, \
				S_IWUSR|S_IRUGO, ctx->dir, \
				&ctx->_ctx_dev_member_); \
			dentry = debugfs_lookup(_name_, ctx->dir); \
			if (!dentry) { \
				dev_warn(vha->dev, \
					"%s: failed to create %s dbg file!\n", \
					__func__, _name_); \
			} else { \
				dput(dentry); \
			} \
	}
#else
#define CTX_DBGFS_CREATE_RW(_type_, _name_, _ctx_dev_member_, dir) \
	{ \
			if (!debugfs_create_##_type_(_name_, \
				S_IWUSR|S_IRUGO, ctx->dir, \
				&ctx->_ctx_dev_member_)) { \
				dev_warn(vha->dev, \
					"%s: failed to create %s dbg file!\n", \
					__func__, _name_); \
			} \
	}
#endif
#define VHA_DBGFS_CREATE_FILE(_perm_, _name_, _fops_) \
	{ \
			if (!debugfs_create_file(_name_, \
				_perm_, ctx->debugfs_dir, vha, \
				&vha_##_fops_##_fops)) { \
				dev_warn(vha->dev, \
					"%s: failed to create %s dbg file!\n", \
					__func__, _name_); \
			} \
	}

#define VHA_DBGFS_CREATE_FILE_IN_DIR(_perm_, _name_, _fops_, dir) \
	{ \
			if (!debugfs_create_file(_name_, \
				_perm_, ctx->dir, vha, \
				&vha_##_fops_##_fops)) { \
				dev_warn(vha->dev, \
					"%s: failed to create %s dbg file!\n", \
					__func__, _name_); \
			} \
	}
#define CTX_DBGFS_CREATE_FILE(_perm_, _name_, _fops_) \
	{ \
			if (!debugfs_create_file(_name_, \
				_perm_, ctx->debugfs_dir, &ctx->_fops_, \
				&vha_##_fops_##_fops)) { \
				dev_warn(vha->dev, \
					"%s: failed to create %s dbg file!\n", \
					__func__, _name_); \
			} \
	}

	/* and some registers for debug */
	if (vha->reg_base) {
		ctx->regset.regs = vha_regs;
		ctx->regset.nregs = vha->reg_size / sizeof(uint64_t);
		ctx->regset.vha = vha;
		CTX_DBGFS_CREATE_FILE(S_IRUGO, "regdump", regset);
	}

	VHA_DBGFS_CREATE_RO(u32, "core_freq_khz", freq_khz, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u32, "core_state", state, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "core_uptime_ms", stats.uptime_ms, debugfs_dir);
#ifndef CONFIG_HW_MULTICORE
	VHA_DBGFS_CREATE_RO(u64, "core_last_proc_us", stats.last_proc_us, debugfs_dir);
#endif
	VHA_DBGFS_CREATE_RO(u64, "cnn_add_cmds", stats.cnn_add_cmds, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_kicks", stats.cnn_kicks, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_polled", stats.cnn_polled, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_responsed", stats.cnn_responsed, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_kicks_queued", stats.cnn_kicks_queued, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_kicks_completed", stats.cnn_kicks_completed, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_kicks_cancelled", stats.cnn_kicks_cancelled, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_kicks_aborted", stats.cnn_kicks_aborted, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_total_proc_us", stats.cnn_total_proc_us, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_last_proc_us", stats.cnn_last_proc_us, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_avg_proc_us", stats.cnn_avg_proc_us, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_last_est_proc_us", stats.cnn_last_est_proc_us, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "cnn_avg_est_proc_us", stats.cnn_avg_est_proc_us, debugfs_dir);
#ifdef CONFIG_HW_MULTICORE
	VHA_DBGFS_CREATE_RO(u8,  "num_cores", hw_props.num_cnn_core_devs, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u32, "socm_bytes", hw_props.socm_size_bytes, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u32, "socm_core_bytes", hw_props.socm_core_size_bytes, debugfs_dir);
#endif
	VHA_DBGFS_CREATE_RO(u32, "locm_bytes", hw_props.locm_size_bytes, debugfs_dir);

	VHA_DBGFS_CREATE_RO(u32, "mem_usage_last", stats.mem_usage_last, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u32, "mmu_usage_last", stats.mmu_usage_last, debugfs_dir);
	VHA_DBGFS_CREATE_RO(u64, "total_failures", stats.total_failures, debugfs_dir);

	if (vha->hw_props.supported.rtm) {
		CTX_DBGFS_CREATE_RW(u64, "rtm_ctrl", rtm_ctrl, debugfs_dir);
		VHA_DBGFS_CREATE_FILE(S_IRUGO, "rtm_data", rtm);
	}

	CTX_DBGFS_CREATE_RW(u64, "ioreg_addr", ioreg_addr, debugfs_dir);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "ioreg_data", ioreg);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "cnn_utilization", cnn_utilization);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "cnn_last_cycles", cnn_last_cycles);
	VHA_DBGFS_CREATE_FILE(S_IWUSR, "stats_reset", stats_reset);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "BVNC", bvnc);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "pri_q_counters", pri_q_counters);
#ifdef CONFIG_HW_MULTICORE
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "cnn_kicks_per_core", cnn_kicks_per_core);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "cnn_utilization_per_core", cnn_utilization_per_core);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "wl_last_stats", wl_last_stats);
	VHA_DBGFS_CREATE_FILE(S_IRUGO, "scheduling_stats", scheduling_stats);
#endif

#ifdef VHA_FUNCT_CTRL
	ctx->funct_ctrl_dir = debugfs_create_dir("FUNCT_CTRL", ctx->debugfs_dir);
	if (ctx->funct_ctrl_dir) {
		VHA_DBGFS_CREATE_RW(u32, "pm_delay", pm_delay, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(u8, "mmu_mode", mmu_mode, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(u8, "mmu_ctx_default", mmu_ctx_default, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(u32, "mmu_page_size", mmu_page_size, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(bool, "mmu_base_pf_test", mmu_base_pf_test, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(u32, "mmu_no_map_count", mmu_no_map_count, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(u8, "low_latency", low_latency, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(u32, "suspend_interval_msec", suspend_interval_msec, funct_ctrl_dir);
		VHA_DBGFS_CREATE_RW(u8, "fault_inject", fault_inject, funct_ctrl_dir);
#ifdef CONFIG_HW_MULTICORE
		VHA_DBGFS_CREATE_FILE_IN_DIR(S_IRUGO, "scheduling_sequence", sched, funct_ctrl_dir);
		VHA_DBGFS_CREATE_FILE_IN_DIR(S_IRUGO, "stalling", stalling, funct_ctrl_dir);
#endif
	}
#endif

#ifdef VHA_EVENT_INJECT
	ctx->event_inject_dir = debugfs_create_dir("EVENT_INJECT", ctx->debugfs_dir);
	if (ctx->event_inject_dir) {
#ifdef CONFIG_HW_MULTICORE
		VHA_DBGFS_CREATE_RW(u64, "VHA_CR_CORE_EVENT", injection.vha_cr_core_event, event_inject_dir);
		VHA_DBGFS_CREATE_RW(u64, "VHA_CR_SYS_EVENT", injection.vha_cr_sys_event, event_inject_dir);
		VHA_DBGFS_CREATE_RW(u64, "VHA_CR_INTERCONNECT_EVENT", injection.vha_cr_interconnect_event, event_inject_dir);
		VHA_DBGFS_CREATE_RW(u64, "VHA_CR_WM_EVENT", injection.vha_cr_wm_event, event_inject_dir);
		VHA_DBGFS_CREATE_RW(u64, "CONF_ERR", injection.conf_err, event_inject_dir);
		VHA_DBGFS_CREATE_RW(u64, "PARITY_POLL_ERR", injection.parity_poll_err_reg, event_inject_dir);
#else
		VHA_DBGFS_CREATE_RW(u64, "VHA_CR_EVENT", injection.vha_cr_event, event_inject_dir);
#endif
	}
#endif /* VHA_EVENT_INJECT */

#undef CTX_DBGFS_CREATE_FILE
#undef VHA_DBGFS_CREATE_FILE
#undef VHA_DBGFS_CREATE_FILE_IN_DIR
#undef CTX_DBGFS_CREATE
#undef VHA_DBGFS_CREATE_RO
#undef VHA_DBGFS_CREATE_RW

#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
	vha_sc_dbg_init(vha, ctx->debugfs_dir);
#endif /* VHA_SCF */

	vha->dbgfs_ctx = (void *)ctx;
}

void vha_dbg_deinit(struct vha_dev *vha)
{
	struct vha_dbgfs_ctx *ctx =
			(struct vha_dbgfs_ctx *)vha->dbgfs_ctx;
#if defined(VHA_SCF) && defined(CONFIG_HW_MULTICORE)
	vha_sc_dbg_deinit(vha);
#endif /* VHA_SCF */
	/* ctx->debugfs_dir==NULL is safe */
	debugfs_remove_recursive(ctx->debugfs_dir);
}

struct dentry* vha_dbg_get_sysfs(struct vha_dev *vha)
{
	struct vha_dbgfs_ctx *ctx =
			(struct vha_dbgfs_ctx *)vha->dbgfs_ctx;

	return ctx->debugfs_dir;
}

#else // CONFIG_DEBUG_FS
void vha_dbg_init(struct vha_dev *vha) {}
void vha_dbg_deinit(struct vha_dev *vha) {}
struct dentry* vha_dbg_get_sysfs(struct vha_dev *vha) { return NULL; }
int vha_dbg_create_hwbufs(struct vha_session *session) { return 0; }
void vha_dbg_destroy_hwbufs(struct vha_session *session) {}
int vha_dbg_alloc_hwbuf(struct vha_session *session, size_t size,
                struct vha_buffer **buffer, const char *name, bool map) { return 0; }
void vha_dbg_hwbuf_cleanup(struct vha_session *session,
                struct vha_buffer *buf) {}

#endif // CONFIG_DEBUG_FS

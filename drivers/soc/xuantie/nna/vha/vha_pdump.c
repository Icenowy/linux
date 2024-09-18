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
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <uapi/vha.h>
#include "vha_common.h"

#ifdef CONFIG_DEBUG_FS

static uint32_t pdump_sizes_kB[PDUMP_MAX];
module_param_array(pdump_sizes_kB, int, NULL, 0444);
MODULE_PARM_DESC(pdump_sizes_kB, "sizes of buffers reserved for pdump TXT,PRM,RES,DBG,CRC,CMB_CRC");

static bool no_pdump_cache = true;
module_param(no_pdump_cache, bool, 0444);
MODULE_PARM_DESC(no_pdump_cache, "if Y, then pdump cache feature is disabled");

static uint32_t pdump_chunk_size_kB = 1024;
module_param(pdump_chunk_size_kB, uint, 0444);
MODULE_PARM_DESC(pdump_chunk_size_kB, "maximum size of pdump chunk to be stored at once");

static const char *pdump_filenames[PDUMP_MAX] = {
	"pdump.txt", "pdump.prm", "pdump.res", "pdump.dbg", "pdump.crc", "pdump.crc_cmb",
};

static bool pdump_file_enabled[PDUMP_MAX] = {
	true, true, true, true, true, true,
};

/* read one of the pdump buffers */
static ssize_t pdump_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct pdump_buf *pbuf = file->private_data;

	if (pbuf->len == pbuf->size)
		memcpy(pbuf->ptr+pbuf->size-8, "\n<oflo!>", 8);
	return simple_read_from_buffer(user_buf, count, ppos,
							 pbuf->ptr, pbuf->len);
}

/*
 * write anything to the "reset" file, and
 * it will clear out the contents of all the pdump buffers
 */
static ssize_t reset_write(struct file *file, const char __user *buf,
					 size_t count, loff_t *ppos)
{
	int i;
	struct pdump_descr *pbuf = file->private_data;

	for (i = 0; i < PDUMP_MAX; i++) {
		if (pbuf->pbufs[i].ptr)
			pbuf->pbufs[i].len = 0;
	}

	return count;
}
static const struct file_operations pdump_fops = {
	.read = pdump_read,
	.open = simple_open,
	.llseek = default_llseek,
};
static const struct file_operations reset_fops = {
	.write = reset_write,
	.open = simple_open,
};

static void config_pdump_files(struct vha_dev *vha) {
	if (!vha->cnn_combined_crc_enable)
		pdump_file_enabled[PDUMP_CRC_CMB] = false;
}

int vha_pdump_init(struct vha_dev *vha, struct pdump_descr* pdump)
{
	int i;

	/* no purpose to pdumping if no TXT */
	if (pdump_sizes_kB[PDUMP_TXT] == 0)
		return -EINVAL;

	if (!vha->hw_props.dummy_dev) {
		dev_err(vha->dev, "%s: PDUMPing not supported with real hardware!\n",
				__func__);
		return -EPERM;
	}

	config_pdump_files(vha);

	/* and map the buffers into debugfs */
	for (i = 0; i < PDUMP_MAX; i++) {
		struct pdump_buf *pbuf = NULL;

		if (!pdump_file_enabled[i])
			continue;

		if (pdump_sizes_kB[i]) {
			if (pdump_sizes_kB[i] == UINT_MAX) {
				pbuf = img_pdump_create(pdump, i, 0);
				if (pbuf)
					pbuf->drop_data = true;
			} else {
				pbuf = img_pdump_create(pdump, i, pdump_sizes_kB[i]*1024);
				if (pbuf)
					pbuf->drop_data = false;
			}
		}

		if (pdump->pbufs[PDUMP_TXT].ptr == NULL)
			return -ENOMEM;

		if (pbuf && pbuf->ptr) {
			if (!debugfs_create_file(pdump_filenames[i],
							0444, vha_dbg_get_sysfs(vha),
							pbuf, &pdump_fops))
				dev_warn(vha->dev,
						"%s: failed to create sysfs entry for pdump buf!\n",
						__func__);
			else
				dev_info(vha->dev, "%s: sysfs file %s created, size:%ukB\n",
						 __func__,
						 pdump_filenames[i], pdump_sizes_kB[i]);
		}
	}

	/* create a write-only file, for resetting all the pdump buffers
	 * note: world write permission: it is pretty safe, because these
	 * are just debug files.
	 */
	if (!debugfs_create_file("pdump_reset", 0222,
			vha_dbg_get_sysfs(vha), pdump, &reset_fops))
			dev_warn(vha->dev,
					"%s: failed to create sysfs entry for pdump reset!\n",
					__func__);
	return 0;
}

void vha_pdump_deinit(struct pdump_descr* pdump)
{
	img_pdump_destroy(pdump);
}

static void *get_buf_kptr(struct vha_session *session,
				struct vha_buffer *buf)
{
	int ret;

	if (buf->kptr == NULL) {
		ret = img_mem_map_km(session->mem_ctx, buf->id);
		if (ret) {
			dev_err(session->vha->dev,
				"%s: failed to map buff %x to km: %d\n",
				__func__, buf->id, ret);
			return NULL;
		}
		buf->kptr = img_mem_get_kptr(session->mem_ctx, buf->id);
	}
	return buf->kptr;
}

static void put_buf_kptr(struct vha_session *session,
				struct vha_buffer *buf)
{
	int ret;

	WARN_ON(!buf->kptr);
	if (buf->kptr != NULL) {
		ret = img_mem_unmap_km(session->mem_ctx, buf->id);
		if (ret)
			dev_err(session->vha->dev,
				"%s: failed to unmap buff %x from km: %d\n",
				__func__, buf->id, ret);
		buf->kptr = NULL;
	}
}

/* create pdump commands for LOAD Buffer */
void vha_pdump_ldb_buf(struct vha_session *session, uint32_t pdump_num,
					 struct vha_buffer *buf, uint32_t offset, uint32_t size, bool cache)
{
	struct pdump_descr* pdump = vha_pdump_dev_get_drvdata(session->vha->dev);
	struct vha_dev* vha = session->vha;

	if (pdump_num >= PDUMP_MAX ||
		 pdump->pbufs[PDUMP_TXT].ptr == NULL)
		return;

	if (buf->attr & IMG_MEM_ATTR_NOMAP)
		return;

	/* map buffer into km, if necessary */
	if (get_buf_kptr(session, buf) == NULL)
		return;

	if (no_pdump_cache)
		cache = false;

	if (!buf->pcache.valid ||
			(buf->pcache.valid &&
				(buf->pcache.offset != offset ||
				 buf->pcache.size != size))) {

		buf->pcache.valid = cache;
		buf->pcache.size = size;
		buf->pcache.offset = offset;

		img_pdump_printf("LDB "_PMEM_":BLOCK_%d:%#x %#x %#zx %s -- %s\n",
				 buf->id, offset, size,
				 pdump->pbufs[pdump_num].len,
				 pdump_filenames[pdump_num],
				 buf->name);
		{
			void *ptr = buf->kptr + offset;
			int max_chunk = pdump_chunk_size_kB * 1024;
			while (size) {
				int chunk_size = size > max_chunk ?
					max_chunk : size;
				pr_debug("vha_pdump_ldb_buf chunk %d!\n",
						chunk_size);
				if (img_pdump_write(pdump, pdump_num,
								ptr, chunk_size) < 0) {
					img_pdump_printf("COM \"ERROR:pdump oflo, writing %#xB from %s to %s!\"\n",
						size, buf->name,
						pdump_filenames[pdump_num]);
					break;
				}
				ptr += chunk_size;
				size -= chunk_size;
				schedule();
			}
		}
	} else {
			img_pdump_printf("-- LDB_CACHED %s\n", buf->name);
	}
	put_buf_kptr(session, buf);
}

/* create pdump commands for SAVE Buffer */
void vha_pdump_sab_buf(struct vha_session *session, uint32_t pdump_num,
					 struct vha_buffer *buf, uint32_t offset, uint32_t size)
{
	struct pdump_descr* pdump = vha_pdump_dev_get_drvdata(session->vha->dev);
	struct vha_dev* vha = session->vha;
	struct pdump_buf* pbuf;

	if (pdump_num >= PDUMP_MAX ||
		 pdump->pbufs[PDUMP_TXT].ptr == NULL)
		return;

	pbuf = &pdump->pbufs[pdump_num];

	if (buf->attr & IMG_MEM_ATTR_NOMAP)
		return;

	if (get_buf_kptr(session, buf) == NULL)
		return;

	img_pdump_printf("SAB "_PMEM_":BLOCK_%d:%#x %#x %#zx %s -- %s\n",
			 buf->id, offset, size,
			 pbuf->len, pdump_filenames[pdump_num],
			 buf->name);

	if (pbuf->drop_data) {
		pbuf->len += size;
	} else {
		void *ptr = buf->kptr + offset;
		int max_chunk = pdump_chunk_size_kB * 1024;

		/* Invalidate buffer cache just for sanity */
		img_mem_sync_device_to_cpu(session->mem_ctx, buf->id);

		/* write the binary data to the pdump blob file */
		while (size) {
			int chunk_size = size > max_chunk ? max_chunk : size;
			pr_debug("vha_pdump_sab_buf chunk %d!\n", chunk_size);
			if (img_pdump_write(pdump, pdump_num, ptr, chunk_size) < 0) {
				img_pdump_printf("COM \"ERROR:pdump oflo, writing %#xB from %s to %s!\"\n",
					size, buf->name,
					pdump_filenames[pdump_num]);
				break;
			}
			ptr += chunk_size;
			size -= chunk_size;
			schedule();
		}
	}
	put_buf_kptr(session, buf);
}

#else // CONFIG_DEBUG_FS
int vha_pdump_init(struct vha_dev *vha, struct pdump_descr* pdump) { return 0; }
void vha_pdump_deinit(struct pdump_descr* pdump) {}
void vha_pdump_ldb_buf(struct vha_session *session, uint32_t pdump_num,
		struct vha_buffer *buffer, uint32_t offset, uint32_t len, bool cache) {}
void vha_pdump_sab_buf(struct vha_session *session, uint32_t pdump_num,
		struct vha_buffer *buffer, uint32_t offset, uint32_t len) {}
#endif // CONFIG_DEBUG_FS

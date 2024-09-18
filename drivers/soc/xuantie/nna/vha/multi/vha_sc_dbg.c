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

#define VHA_MAX_NUM_SEGMENTS 10

static bool generate_crcs_enable = true;
module_param(generate_crcs_enable, bool, 0444);
MODULE_PARM_DESC(generate_crcs_enable,
	"Enable generating safety CRCs");

struct vha_sc_dbgfs_ctx {
	struct dentry    *sc_debugfs_dir;

	struct dentry    *crcs_dir;
	struct dentry    *crcs_sub_dirs[VHA_MAX_NUM_SEGMENTS];
	uint32_t 		 num_cores_used[VHA_MAX_NUM_SEGMENTS];
	uint32_t         latest_crcs[VHA_MAX_NUM_SEGMENTS][VHA_NUM_CORES];
	uint8_t          segment_crc_idx_to_use;

	uint8_t          num_segments;
};

static ssize_t vha_bin_crcs_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	uint32_t bin_crcs[VHA_MAX_NUM_SEGMENTS * VHA_MAX_CORES] = { 0 };
	struct vha_sc_dbgfs_ctx *ctx = (struct vha_sc_dbgfs_ctx *)vha->sc_dbgfs_ctx;
	size_t bytes = 0;
	uint32_t offset = 0;
	int i, j;

	if (*ppos)
		return 0;

	for (i = 0; i < ctx->num_segments; i++) {
		for (j = 0; j < ctx->num_cores_used[i]; j++) {
			bin_crcs[offset] = ctx->latest_crcs[i][j];
			offset++;
		}
	}

	if (copy_to_user(buf, bin_crcs, offset * sizeof(bin_crcs[0]))) {
		dev_err(vha->dev, "%s: bin_crcs read: copy to user failed\n",
				__func__);
		return -EFAULT;
	}

	if (count < offset)
		return -EINVAL;	

	bytes = offset * sizeof(uint32_t);
	*ppos = bytes;

	return bytes;
}

static const struct file_operations vha_crcs_bin_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = vha_bin_crcs_read,
};


static ssize_t vha_crcs_reset_write(struct file *file, const char __user *buf,
					 size_t count, loff_t *ppos)
{
	struct vha_dev *vha = file->private_data;
	struct vha_sc_dbgfs_ctx *ctx = (struct vha_sc_dbgfs_ctx *)vha->sc_dbgfs_ctx;

	if (ctx->crcs_dir) {
		int i = 0;

		for (i = 0; i < VHA_MAX_NUM_SEGMENTS; i++)
			if (ctx->crcs_sub_dirs[i]) {
				debugfs_remove_recursive(ctx->crcs_sub_dirs[i]);
				ctx->crcs_sub_dirs[i] = NULL;
			}

		ctx->segment_crc_idx_to_use = 0;

		memset(ctx->latest_crcs, 0, sizeof(ctx->latest_crcs));
		memset(ctx->num_cores_used, 0, sizeof(ctx->num_cores_used));
	}

	return count;
}

static const struct file_operations vha_crcs_reset_fops = {
	.write = vha_crcs_reset_write,
	.open = simple_open,
};

void vha_update_crcs(struct vha_dev *vha, uint32_t crcs[VHA_NUM_CORES], int n) {
	struct vha_sc_dbgfs_ctx *ctx = (struct vha_sc_dbgfs_ctx *)vha->sc_dbgfs_ctx;

	if (ctx->crcs_dir && ctx->num_segments) {
		uint8_t i;
		uint8_t crc_idx = ctx->segment_crc_idx_to_use;
		char core_txt[7] = "core_x";

		if (crc_idx >= VHA_MAX_NUM_SEGMENTS) {
			dev_warn_once(vha->dev, "%s: unable to update crcs, too many segments\n", __func__);			
			return;
		}

		if (ctx->crcs_sub_dirs[crc_idx] == NULL) {
			char dir_txt[10] = "segment_x";			
			snprintf(dir_txt, sizeof(dir_txt), "segment_%d", crc_idx);
			ctx->crcs_sub_dirs[crc_idx] = debugfs_create_dir(dir_txt, ctx->crcs_dir);
			for (i = 0; i < n; i++) {
				snprintf(core_txt, sizeof(core_txt), "core_%d", i);
				debugfs_create_x32(core_txt, S_IRUGO, ctx->crcs_sub_dirs[crc_idx],
						&ctx->latest_crcs[crc_idx][i]);
			}			
		}

		ctx->num_cores_used[crc_idx] = n;

		for (i = 0; i < n; i++)
			ctx->latest_crcs[crc_idx][i] = crcs[i];

		ctx->segment_crc_idx_to_use++;

		if (ctx->segment_crc_idx_to_use >= ctx->num_segments)
			ctx->segment_crc_idx_to_use = 0;
	}
}

void vha_sc_dbg_init(struct vha_dev *vha, struct dentry *debugfs_dir)
{
	struct vha_sc_dbgfs_ctx *ctx = devm_kzalloc(vha->dev,
			sizeof(struct vha_sc_dbgfs_ctx), GFP_KERNEL);
	if (!ctx) {
		dev_err(vha->dev,
				"%s: Out of memory when creating debugfs context!\n",
				__func__);
		return;
	}

	/* Create userspace node */
	if (!debugfs_dir) {
		dev_warn(vha->dev,
				"%s: Probably debugfs not enabled in this kernel!\n",
				__func__);
		return;
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

	ctx->sc_debugfs_dir = debugfs_create_dir("sf_gen", debugfs_dir);
	CTX_DBGFS_CREATE_RW(u8, "num_segments", num_segments, sc_debugfs_dir);

	if (generate_crcs_enable) {
		ctx->crcs_dir = debugfs_create_dir("CRCs", ctx->sc_debugfs_dir);
		if (ctx->crcs_dir) {
			VHA_DBGFS_CREATE_FILE_IN_DIR(S_IWUSR, "crcs_reset", crcs_reset, crcs_dir);
			VHA_DBGFS_CREATE_FILE_IN_DIR(S_IRUGO, "crcs_bin", crcs_bin, crcs_dir);
		}
	}

#undef VHA_DBGFS_CREATE_FILE_IN_DIR	
#undef CTX_DBGFS_CREATE_RW

	vha->sc_dbgfs_ctx = (void *)ctx;
}

void vha_sc_dbg_deinit(struct vha_dev *vha)
{
	struct vha_sc_dbgfs_ctx *ctx =
			(struct vha_sc_dbgfs_ctx *)vha->sc_dbgfs_ctx;
	debugfs_remove_recursive(ctx->sc_debugfs_dir);
}



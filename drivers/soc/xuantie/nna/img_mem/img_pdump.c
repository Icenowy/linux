/*!
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

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <img_mem_man.h>
#include <vha_drv_common.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 15, 0)
#include <stdarg.h>
#else
#include <linux/stdarg.h>
#endif

/*
 * create a pdump buffer
 * Pdump buffers are currently identified by hard coded number:
 * from PDUMP_TXT up to PDUMP_MAX.
 * Buffer is allocated using vmalloc, because it might be several MBytes.
 *
 * If size==0, the buffer can be used for SAB, but not for LDB or TXT.
 * (in other words, no memory will be allocated,
 * but it will still have a 'length')
 */
struct pdump_buf *img_pdump_create(struct pdump_descr* pdump, uint32_t pdump_num, size_t size)
{
	struct pdump_buf *pbuf = &pdump->pbufs[pdump_num];

	if (pdump_num >= PDUMP_MAX) {
		pr_err("%s: invalid pdump number:%d\n", __func__, pdump_num);
		return NULL;
	}
	if (pbuf->ptr != NULL) {
		pr_err("%s: pdump %d already created\n", __func__, pdump_num);
		return NULL;
	}

	pbuf->size = size;
	pbuf->len = 0;
	if (size == 0)
		return pbuf;

	pbuf->ptr  = vmalloc(size);
	pr_debug("%s %d buffer %p size:%zu!\n", __func__,
			pdump_num, pbuf->ptr, size);
	if (pbuf->ptr == NULL) {
		pr_err("%s: failed to create pdump %d\n", __func__, pdump_num);
		return NULL;
	}
	return pbuf;
}
EXPORT_SYMBOL(img_pdump_create);

/*
 * append binary data to one of the pdump buffers
 */
int img_pdump_write(struct pdump_descr* pdump, uint32_t pdump_num, const void *ptr, size_t size)
{
	struct pdump_buf *pbuf = &pdump->pbufs[pdump_num];
	int ret = 0;

	if (pdump_num >= PDUMP_MAX || ptr == NULL || pbuf->ptr == NULL)
		return -EINVAL;

	mutex_lock(&pdump->lock);
	if (pbuf->len + size > pbuf->size)
		size = pbuf->size - pbuf->len;

	if (!size) {
		pr_err("%s: no space left in the pdump %d buffer!\n",
				__func__, pdump_num);
		ret = -ENOSPC;
		goto unlock;
	}
	pr_debug("%s %d buffer len:%zu size:%zu!\n", __func__,
			pdump_num, size, pbuf->len);
	memcpy(pbuf->ptr + pbuf->len, ptr, size);
	pbuf->len += size;
	pr_debug("%s end!\n", __func__);

unlock:
	mutex_unlock(&pdump->lock);

	return ret;
}
EXPORT_SYMBOL(img_pdump_write);

/*
 * append a string to the TXT pdump buffer.
 * returns the number of bytes printed or error.
 */
__printf(2, 3)
int __img_pdump_printf(struct device* dev, const char *fmt, ...)
{
	struct pdump_descr* pdump = vha_pdump_dev_get_drvdata(dev);
	struct pdump_buf *pbuf;
	va_list ap;

	BUG_ON(pdump==NULL);
	pbuf = &pdump->pbufs[PDUMP_TXT];
	if (pbuf->ptr == NULL)
		return -EINVAL;

	mutex_lock(&pdump->lock);
	va_start(ap, fmt);
	if (pbuf->len < pbuf->size) {
#if defined(OSID)
		/* Prepend OSID to pdump comments */
		if (fmt[0] == '-' && fmt[1] == '-')
			pbuf->len += sprintf(pbuf->ptr + pbuf->len,
								 "-- (OS%d) ", OSID);
#endif
		pbuf->len += vsnprintf(pbuf->ptr + pbuf->len,
							 pbuf->size - pbuf->len,
							 fmt, ap);
	}
	/*
	 * vsnprintf returns the number of bytes that WOULD have been printed
	 */
	pbuf->len = min(pbuf->size, pbuf->len);
	va_end(ap);
	mutex_unlock(&pdump->lock);

	return pbuf->len;
}
EXPORT_SYMBOL(__img_pdump_printf);


void img_pdump_destroy(struct pdump_descr* pdump)
{
	int i;

	for (i = 0; i < PDUMP_MAX; i++) {
		void *ptr = pdump->pbufs[i].ptr;

		pdump->pbufs[i].ptr = NULL;
		pr_debug("%s %d buffer %p!\n", __func__, i, ptr);
		vfree(ptr);
	}
}
EXPORT_SYMBOL(img_pdump_destroy);

/*
 * PDUMP generation is disabled until a PDUMP TXT buffer has been created
 */
bool img_pdump_enabled(struct pdump_descr* pdump)
{
	return pdump && pdump->pbufs[PDUMP_TXT].ptr != NULL;
}
EXPORT_SYMBOL(img_pdump_enabled);


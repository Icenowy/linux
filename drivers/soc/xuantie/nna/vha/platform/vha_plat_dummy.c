/*****************************************************************************
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
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>

#include "vha_common.h"
#include "vha_plat.h"
#include "uapi/version.h"
#include "vha_regs.h"

#if defined(CFG_SYS_MAGNA)
#include "hwdefs/vha_cr_magna.h"
#endif

#define DEVICE_NAME "vha"

static unsigned short min_alloc_order;
module_param(min_alloc_order, ushort, 0444);
MODULE_PARM_DESC(min_alloc_order,
		"Minimum allocation order, depends on PAGE_SIZE, \
		for CPU PAGE_SIZE=4kB, 0-4kB, 1-8kB, 2-16kB, 3-32kB, 4-64kB");
static unsigned short max_alloc_order = 10; /* 4MB for PAGE_SIZE=4kB */
module_param(max_alloc_order, ushort, 0444);
MODULE_PARM_DESC(max_alloc_order,
		"Maximum allocation order, depends on PAGE_SIZE, \
		for CPU PAGE_SIZE=4kB, 0-4kB, 1-8kB, 2-16kB, 3-32kB, 4-64kB");

static unsigned char num_clusters = 1;
module_param(num_clusters, byte, 0444);
MODULE_PARM_DESC(num_clusters,
		"Number of dummy clusters. Each cluster will be instantiated "
		"as separate /dev/vhaN node. Max number supported is 255.");

static struct heap_config dummy_heap_configs[] = {
	/* the first config is default */
	{
		.type = IMG_MEM_HEAP_TYPE_UNIFIED,
		.options.unified = {
			.gfp_type = GFP_KERNEL | __GFP_ZERO,
		},
		.to_dev_addr = NULL,
	},
	{
		.type = IMG_MEM_HEAP_TYPE_ANONYMOUS,
	},
#ifdef CONFIG_DMA_SHARED_BUFFER
	{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		.to_dev_addr = NULL,
	},
#endif
};
static size_t num_dummy_heaps =
	sizeof(dummy_heap_configs) / sizeof(*dummy_heap_configs);

static const size_t nna_regs_size =
#ifdef _REG_NNSYS_SIZE
	_REG_NNSYS_SIZE +
#endif
	_REG_SIZE;
static bool use_dummy_regs = false;

/* IO hooks - do nothing */
uint64_t vha_plat_read64(void *addr)
{
	if (use_dummy_regs)
		return *((uint64_t*)addr);

	return 0ULL;
}

void vha_plat_write64(void *addr, uint64_t val)
{
	if (use_dummy_regs)
		*((uint64_t*)addr) = val;
}

#if defined(CFG_SYS_MAGNA)
static void vha_plat_magna_write_defaults(uint8_t* nna_regs) {
	if (!use_dummy_regs)
		return;
	*((uint64_t*)(nna_regs + VHA_CR_CORE_ASSIGNMENT)) =
			VHA_CR_CORE_ASSIGNMENT_CORE_7_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_6_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_5_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_4_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_3_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_2_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_1_WM_MAPPING_UNALLOCATED |
			VHA_CR_CORE_ASSIGNMENT_CORE_0_WM_MAPPING_UNALLOCATED;

	*((uint64_t*)(nna_regs + VHA_CR_SOCM_BUF_ASSIGNMENT)) =
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_7_WM_MAPPING_UNALLOCATED |
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_6_WM_MAPPING_UNALLOCATED |
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_5_WM_MAPPING_UNALLOCATED |
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_4_WM_MAPPING_UNALLOCATED |
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_3_WM_MAPPING_UNALLOCATED |
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_2_WM_MAPPING_UNALLOCATED |
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_1_WM_MAPPING_UNALLOCATED |
			VHA_CR_SOCM_BUF_ASSIGNMENT_SOCM_BUF_0_WM_MAPPING_UNALLOCATED;
}
#endif

#ifdef CONFIG_PM
static int vha_plat_suspend(struct device *dev)
{
	return vha_suspend_dev(dev);
}

static int vha_plat_resume(struct device *dev)
{
	return vha_resume_dev(dev);
}

static int vha_plat_runtime_idle(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static int vha_plat_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static int vha_plat_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}
#endif

static int vha_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void* nna_regs=NULL;
	int ret = 0;

	/* dma_mask is required in order for dma_ops mapping to work */
	dev_info(dev, "%s dma_get_mask : %#llx\n", __func__, dma_get_mask(dev));
	if (dev->dma_mask) {
		dev_info(dev, "%s dev->dma_mask : %p : %#llx\n",
			 __func__, dev->dma_mask, *dev->dma_mask);
	} else {
		dev_info(dev, "%s mask unset, setting coherent\n", __func__);
		dev->dma_mask = &dev->coherent_dma_mask;
	}

	/* Give 128GB fake dma address range,
	 * so that dma_map_page/map_sg does not throw any error,
	 * when dealing with high mem address alocations */
	ret = dma_set_mask(dev, DMA_BIT_MASK(37));
	if (ret) {
		dev_err(dev, "%s failed to set dma mask\n", __func__);
		goto out_add_dev;
	}
	dev_info(dev, "%s dma_set_mask %#llx\n",
			__func__, dma_get_mask(dev));

	if (dev->platform_data)
		nna_regs = *(uint8_t**)dev->platform_data;

	ret = vha_add_dev(dev, NULL, 0,
			NULL /* plat data */,
			nna_regs /* reg base */,
			nna_regs_size /* reg size*/);
	if (ret) {
		dev_err(dev, "vha_add_dev failed\n");
		goto out_add_dev;
	}
out_add_dev:
	return ret;
}

static int vha_plat_remove(struct platform_device *pdev)
{
	int ret = 0;

	dev_info(&pdev->dev, "%s\n", __func__);

	vha_rm_dev(&pdev->dev);

	return ret;
}

static struct dev_pm_ops vha_pm_plat_ops = {
	SET_RUNTIME_PM_OPS(vha_plat_runtime_suspend,
			vha_plat_runtime_resume, vha_plat_runtime_idle)
	SET_SYSTEM_SLEEP_PM_OPS(vha_plat_suspend, vha_plat_resume)
};

static ssize_t info_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "VHA dummy driver version : " VERSION_STRING "\n");
}

static DRIVER_ATTR_RO(info);
static struct attribute *drv_attrs[] = {
	&driver_attr_info.attr,
	NULL
};

ATTRIBUTE_GROUPS(drv);

static struct platform_driver vha_driver = {
	.driver = {
			 .owner = THIS_MODULE,
			 .name = "vha",
			 .groups = drv_groups,
			 .pm = &vha_pm_plat_ops,
			 },
	.probe = vha_plat_probe,
	.remove = vha_plat_remove,
};

static struct platform_device **pd;

int __exit vha_plat_deinit(void)
{
	int ret;
	int cluster;
	uint8_t* nna_regs;

	platform_driver_unregister(&vha_driver);
	for (cluster=0; cluster<num_clusters; ++cluster) {
		BUG_ON(pd[cluster]==NULL);
		nna_regs = *(uint8_t**)pd[cluster]->dev.platform_data;
		vfree(nna_regs);
		platform_device_unregister(pd[cluster]);
	}
	use_dummy_regs = false;
	kfree(pd);
	ret = vha_deinit();
	if (ret)
		pr_err("VHA driver deinit failed\n");

	return 0;
}

int __init vha_plat_init(void)
{
	int ret;
	int cluster;

	if (min_alloc_order > max_alloc_order) {
		pr_err("Can't set min_alloc_order > max_alloc_order !\n");
		return -EINVAL;
	}

	WARN_ON(dummy_heap_configs[0].type != IMG_MEM_HEAP_TYPE_UNIFIED);
	dummy_heap_configs[0].options.unified.min_order = min_alloc_order;
	dummy_heap_configs[0].options.unified.max_order = max_alloc_order;

	ret = vha_init_plat_heaps(dummy_heap_configs, num_dummy_heaps);
	if (ret) {
		pr_err("failed to initialize global heaps\n");
		return -ENOMEM;
	}
	if (num_clusters == 0) {
		pr_notice("Overriding num_clusters parameter to 1\n");
		num_clusters=1;
	}
	pr_notice("%s instantiating %d dummy clusters\n",
				__func__, num_clusters);

	pd = kmalloc(num_clusters*sizeof(*pd), GFP_KERNEL);
	if (pd == NULL) {
		pr_err("failed to allocate memory!\n");
		return -ENOMEM;
	}
	memset(pd, 0, num_clusters*sizeof(*pd));
	for (cluster=0; cluster<num_clusters; ++cluster) {
		void* nna_regs=NULL;

		pr_notice("%s Instantiating dummy vha%d cluster\n", __func__, cluster);
#ifdef _REG_NNA_SIZE
		nna_regs = vmalloc(nna_regs_size);
		if (nna_regs == NULL)
			pr_warn("Failed allocating dummy NNA reg space. Will not use it...\n");
		else {
			pr_notice("Successfully allocated dummy NNA reg space.\n");
			memset(nna_regs, 0, nna_regs_size);
			use_dummy_regs = true;
#if defined(CFG_SYS_MAGNA)
			vha_plat_magna_write_defaults(nna_regs);
#endif
		}
#endif

		/* after this call a copy of pointer to nna_regs is stored in struct device.platform_data
		 * internal data is managed by platform device */
		pd[cluster] = platform_device_register_data(NULL, "vha", cluster, &nna_regs, sizeof(&nna_regs));
		ret = IS_ERR(pd[cluster]);
		if (ret) {
			pr_err("failed to register platform device!\n");
			goto _err;
		}
	}
	ret = platform_driver_register(&vha_driver);
	if (ret) {
		pr_err("failed to register platform driver!\n");
		goto _err;
	}
	return ret;
_err:
	for (cluster=0; cluster<num_clusters; ++cluster) {
		uint8_t* nna_regs = *(uint8_t**)pd[cluster]->dev.platform_data;
		vfree(nna_regs);
		platform_device_unregister(pd[cluster]);
	}
	kfree(pd);
	return ret;
}

/*!
 *****************************************************************************
 *
 * @File       vha_plat_xuantie_th1520_fpga_c910.c
 * ---------------------------------------------------------------------------
 *
 * Copyright (C) 2020 Alibaba Group Holding Limited
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/pm_runtime.h>
#include <img_mem_man.h>
#include "vha_plat.h"
#include "vha_plat_dt.h"

const struct of_device_id vha_plat_dt_of_ids[] = {
	{ .compatible = "img,ax3386-nna" },
	//{ .compatible = VHA_PLAT_DT_OF_ID },
	{ }
};

static struct heap_config example_heap_configs[] = {
	{
		.type = IMG_MEM_HEAP_TYPE_UNIFIED,
		.options.unified = {
			.gfp_type = GFP_KERNEL | __GFP_ZERO,
		},
		.to_dev_addr = NULL,
	},
	{
		.type = IMG_MEM_HEAP_TYPE_DMABUF,
		.to_dev_addr = NULL,
	},
	{
		.type = IMG_MEM_HEAP_TYPE_ANONYMOUS,
		.to_dev_addr = NULL,
	},
};

struct npu_plat_if {
        struct clk *npu_pclk;
        struct clk *npu_aclk;
};
static struct npu_plat_if *g_npi;

/*
 * IO hooks.
 * NOTE: customer may want to use spinlock to avoid
 * problems with multi threaded IO access
 */
uint64_t vha_plat_read64(void *addr)
{
	return readq((volatile void __iomem *)addr);
}

void vha_plat_write64(void *addr, uint64_t val)
{
	writeq(val, (volatile void __iomem *)addr);
}

int vha_plat_dt_hw_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;
	uint64_t dma_mask;

	dev_dbg(dev, "%s dma_get_mask : %#llx\n", __func__, dma_get_mask(dev));
	if (dev->dma_mask) {
		dev_info(dev, "%s dev->dma_mask : %p : %#llx\n",
			 __func__, dev->dma_mask, *dev->dma_mask);
	} else {
		dev_info(dev, "%s mask unset, setting coherent\n", __func__);
		dev->dma_mask = &dev->coherent_dma_mask;
	}

	/* Try alternative dma_mask setting from device tree */
	if (!of_property_read_u64(pdev->dev.of_node, "dma-mask",
				(uint64_t *)&dma_mask)) {
		dev_info(dev, "%s forcing custom mask from DT : %#llx\n",
				__func__, dma_mask);
	} else {
		/* If alternative mask not defined in
		 * DT -> "dma-mask" property, use the default one (32bit) */
		dma_mask = dma_get_mask(dev);
	}
	ret = dma_set_mask(dev, dma_mask);
	if (ret) {
		dev_err(dev, "%s failed to set dma mask\n", __func__);
		return ret;
	}

	/* get clock domain, voltage regulator, set clock rate, etc */
	g_npi = devm_kzalloc(&pdev->dev, sizeof(*g_npi), GFP_KERNEL);
	if (!g_npi)
		return -ENOMEM;

	g_npi->npu_pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(g_npi->npu_pclk)) {
		dev_warn(&pdev->dev, "failed to get npu pclk");
		g_npi->npu_pclk == NULL;
	}

	g_npi->npu_aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(g_npi->npu_aclk)) {
		dev_warn(&pdev->dev, "failed to get npu aclk");
		g_npi->npu_aclk == NULL;
	}

	return 0;
}

/* return platform global heaps */
void vha_plat_dt_get_heaps(struct heap_config **heap_configs, int *num_heaps)
{
	*heap_configs = example_heap_configs;
	*num_heaps = sizeof(example_heap_configs)/sizeof(struct heap_config);
}

static int vha_plat_dt_clk_prepare_enable(struct npu_plat_if *npi)
{
	int ret;

	if (npi->npu_pclk) {
		ret = clk_prepare_enable(npi->npu_pclk);
		if (ret)
			return ret;
	}

	if (npi->npu_aclk) {
		ret = clk_prepare_enable(npi->npu_aclk);
		if (ret) {
			clk_disable_unprepare(npi->npu_pclk);
			return ret;
		}
	}

	return 0;
}

static void vha_plat_dt_clk_disable_unprepare(struct npu_plat_if *npi)
{
	if (npi->npu_aclk)
		clk_disable_unprepare(npi->npu_aclk);
	if (npi->npu_pclk)
		clk_disable_unprepare(npi->npu_pclk);
}

void vha_plat_dt_hw_destroy(struct platform_device *pdev)
{
	/* Put any vendor related code:
	 * put clock domain, voltage regulator, etc */
}

int vha_plat_dt_hw_suspend(struct platform_device *pdev)
{
	vha_plat_dt_clk_disable_unprepare(g_npi);

	return 0;
}

int vha_plat_dt_hw_resume(struct platform_device *pdev)
{
	int ret;

	ret = vha_plat_dt_clk_prepare_enable(g_npi);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable npu clock(%d)\n", ret);
		return ret;
	}

	return 0;
}

//MODULE_DEVICE_TABLE(of, vha_plat_dt_of_ids);

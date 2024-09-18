// SPDX-License-Identifier: GPL-2.0+
/*
 * Code for Fenrir's Loki.
 */
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include "loki.h"

static inline unsigned int loki_readreg32(struct loki_drvdata *pdata, unsigned long offset)
{
    void __iomem *reg = (void __iomem *)pdata->regbase + offset;
    return ioread32(reg);
}

static inline void loki_writereg32(struct loki_drvdata *pdata, unsigned long offset, int val)
{
    void __iomem *reg = (void __iomem *)pdata->regbase + offset;
    iowrite32(val, reg);
}

static int loki_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *of_node = pdev->dev.of_node;
    int ret = 0;
    struct loki_drvdata *priv_data;
    uint32_t memif_cache, memif_prot;

    dev_dbg(dev, "Probe...");

    priv_data = devm_kzalloc(dev, sizeof(struct loki_drvdata), GFP_KERNEL);
    if (!priv_data) {
        pr_err("Memory allocation error, aborting.\n");
        ret = -ENOMEM;
        goto exit;
    }

    priv_data->regbase = of_iomap(of_node, 0);
    if (!priv_data->regbase) {
        dev_err(dev, "Unable to map local interrupt registers\n");
        ret = -ENXIO;
        goto exit;
    }

    priv_data->writereg32 = loki_writereg32;
    priv_data->readreg32 = loki_readreg32;


    /* Reset the DUT */
    priv_data->writereg32(priv_data, REG_LOKI_EXTERNAL_RESET, 0);
    udelay(10);
    priv_data->writereg32(priv_data, REG_LOKI_EXTERNAL_RESET, 1);

    platform_set_drvdata(pdev, priv_data);

    /* Get optional data from the Device Tree */
    if (!of_property_read_u64(pdev->dev.of_node, "memif-cache",
                              (uint64_t *)&memif_cache)) {
        dev_info(dev, "Setting memif_cache to %X from the DT\n", memif_cache);
    }
    priv_data->writereg32(priv_data, REG_LOKI_MEMIF_CACHE_SET, memif_cache);

    if (!of_property_read_u64(pdev->dev.of_node, "memif-prot",
                              (uint64_t *)&memif_prot)) {
        dev_info(dev, "Setting memif_prot to %X from the DT\n", memif_prot);
    }
    priv_data->writereg32(priv_data, REG_LOKI_MEMIF_PROT_SET, memif_prot);

    loki_intc_probe(pdev);

exit:
    return ret;
}

static int loki_remove(struct platform_device *pdev)
{
    struct loki_drvdata *pdata = platform_get_drvdata(pdev);

    loki_intc_remove(pdev);

    return 0;
}

static const struct of_device_id loki_dt_ids[] = {
        { .compatible = "img,loki", },
        {},
};
MODULE_DEVICE_TABLE(of, loki_dt_ids);

static struct platform_driver loki_device_driver = {
        .probe		= loki_probe,
        .remove		= loki_remove,
        .driver		= {
                .name	= DEVICE_NAME,
                .of_match_table	= of_match_ptr(loki_dt_ids),
        }
};
module_platform_driver(loki_device_driver);

MODULE_AUTHOR("Imagination Technologies");
MODULE_DESCRIPTION("Fenrir Loki driver");
MODULE_LICENSE("GPL v2");
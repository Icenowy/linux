// SPDX-License-Identifier: GPL-2.0+
/*
 * Code for Fenrir's Loki interrupt controller.
 */
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

#include "loki.h"


static void loki_mask_irq(struct irq_data *d)
{
    struct loki_drvdata *pdata = irq_data_get_irq_chip_data(d);

    u32 reg = pdata->readreg32(pdata, REG_LOKI_INTERRUPT_ENABLE);

    pdata->writereg32(pdata, REG_LOKI_INTERRUPT_ENABLE, reg & ~(LOKI_INTERRUPT_DUT0));
}

static void loki_unmask_irq(struct irq_data *d)
{
    struct loki_drvdata *pdata = irq_data_get_irq_chip_data(d);

    u32 reg = pdata->readreg32(pdata, REG_LOKI_INTERRUPT_ENABLE);
    pdata->writereg32(pdata, REG_LOKI_INTERRUPT_ENABLE, reg | LOKI_INTERRUPT_DUT0);
}

static struct irq_chip fenrir_loki = {
        .name		= "loki-intc",
        .irq_mask	= loki_mask_irq,
        .irq_unmask	= loki_unmask_irq,
};

static int loki_intc_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
    struct irq_chip *chip = &fenrir_loki;

    irq_domain_set_info(d, irq, hw, chip, d->host_data,
                        handle_level_irq, NULL, NULL);
    irq_set_status_flags(irq, IRQ_LEVEL);

    return 0;
}

static irqreturn_t loki_isrcb(int irq, void *dev_id)
{
    struct platform_device *pdev = (struct platform_device *)dev_id;
    struct loki_drvdata *pdata;
    u32 reg, mask, timeout;

    if (!pdev) {
        pr_err("LOKI: pdev not set!?\n");
        return IRQ_NONE;
    }

    pdata = platform_get_drvdata(pdev);

    if (!pdata) {
        pr_err("LOKI: pdata not set!?\n");
        return IRQ_NONE;
    }

    reg = pdata->readreg32(pdata, REG_LOKI_INTERRUPT_STATUS);
    mask = pdata->readreg32(pdata, REG_LOKI_INTERRUPT_ENABLE);
    timeout = pdata->readreg32(pdata, REG_LOKI_INTERRUPT_TIMEOUT_CLR);

    dev_dbg(&pdev->dev, "Got an interrupt. %X - %X - %X\n", reg, mask, timeout);

    /* Check the timeout register just in case */
    if (timeout != 0) {
        dev_warn(&pdev->dev, "Interrupt timeout fired. Will need to be reset\n");
    }

    if (reg & mask) {
        if (reg & LOKI_INTERRUPT_TESTINT) {
            dev_warn(&pdev->dev, "Test interrupt fired! Was it on purpose?\n");
            /* Disable the interrupt */
            pdata->writereg32(pdata, REG_LOKI_INTERRUPT_TEST, 0);
        }
        else if (reg & LOKI_INTERRUPT_DUT0) {
            int logical_irq_num;
            /* trigger registered IRQ, if any */
            logical_irq_num = irq_find_mapping(pdata->intc.domain, 0);
            generic_handle_irq(logical_irq_num);
        }

        /* Clear interrupts */
        pdata->writereg32(pdata, REG_LOKI_INTERRUPT_CLR, reg);

        return IRQ_HANDLED;
    }

    return IRQ_NONE;
}

static const struct irq_domain_ops loki_intc_ops = {
        .xlate = irq_domain_xlate_onecell,
        .map = loki_intc_map,
};

int loki_intc_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *of_node = pdev->dev.of_node;
    int ret = 0;
    struct loki_drvdata *priv_data = platform_get_drvdata(pdev);;

    dev_dbg(dev, "Going to register loki's intc...");

    /* Setup the interrupt controller */
    priv_data->writereg32(priv_data, REG_LOKI_INTERRUPT_ENABLE, LOKI_INTERRUPT_BASE);
    priv_data->writereg32(priv_data, REG_LOKI_INTERRUPT_CLR, LOKI_INTERRUPT_BASE | LOKI_INTERRUPT_DUT0);
    priv_data->writereg32(priv_data, REG_LOKI_INTERRUPT_TIMEOUT_CLR, 0x2);

    priv_data->intc.irq_num = irq_of_parse_and_map(of_node, 0);
    if (priv_data->intc.irq_num == 0) {
        dev_err(dev, "Could not map IRQ\n");
        ret = -ENXIO;
        goto exit;
    }

    ret = devm_request_irq(dev, priv_data->intc.irq_num, &loki_isrcb, IRQF_SHARED, DEVICE_NAME, pdev);
    if (ret) {
        dev_err(dev, "Failed to request irq\n");
        ret = -ENXIO;
        goto exit;
    }

    priv_data->intc.domain = irq_domain_add_linear(of_node, 1, &loki_intc_ops, priv_data);
    if (!priv_data->intc.domain) {
        dev_err(dev, "Unable to create IRQ domain\n");
        ret = -ENXIO;
        goto exit;
    }

exit:
    return ret;
}

int loki_intc_remove(struct platform_device *pdev)
{
    struct loki_drvdata *pdata = platform_get_drvdata(pdev);

    irq_dispose_mapping(irq_find_mapping(pdata->intc.domain, 0));
    irq_domain_remove(pdata->intc.domain);
    return 0;
}
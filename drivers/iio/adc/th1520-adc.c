/* SPDX-License-Identifier: GPL-2.0 */
/*
 * THEAD TH1520 ADC driver
 *
 * Copyright (C) 2021-2024 Alibaba Group Holding Limited.
 * Fugang Duan <duanfugang.dfg@linux.alibaba.com>
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/regulator/consumer.h>
#include <linux/of_platform.h>
#include <linux/err.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>

#include "th1520-adc.h"

static inline void th1520_adc_cfg_init(struct th1520_adc *info)
{
	struct th1520_adc_feature *adc_feature = &info->adc_feature;

	/* set default Configuration for ADC controller */
	adc_feature->selres_sel = TH1520_ADC_SELRES_12BIT;
	adc_feature->input_mode = TH1520_ADC_SINGLE_ENDED_INPUTS;
	adc_feature->vol_ref = TH1520_ADC_VOL_VREF;
	adc_feature->offset_mode = TH1520_ADC_OFFSET_DIS;
	adc_feature->conv_mode = TH1520_ADC_MODE_SINGLE;
	adc_feature->clk_sel = TH1520_ADC_FCLK_TYP_1M;

	adc_feature->int_actual = TH1520_ADC_ACTUAL_ALL;
	adc_feature->int_detal = TH1520_ADC_DETAL_ALL;

	info->ch0_offmeas = 0;
	info->ch1_offmeas = 0;
}

static void th1520_adc_reg_set(struct th1520_adc *info)
{
	u32 phy_cfg = 0;
	u32 op_ctrl = 0;
	struct th1520_adc_feature *adc_feature = &info->adc_feature;

	/* phy_cfg */
	switch (adc_feature->selres_sel) {
	case TH1520_ADC_SELRES_6BIT:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_6BIT;
		break;
	case TH1520_ADC_SELRES_8BIT:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_8BIT;
		break;
	case TH1520_ADC_SELRES_10BIT:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_10BIT;
		break;
	case TH1520_ADC_SELRES_12BIT:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELRES_12BIT;
		break;
	default:
		break;
	}

	switch (adc_feature->input_mode) {
	case TH1520_ADC_SINGLE_ENDED_INPUTS:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELDIFF_SINGLE_ENDED_INPUTS;
		break;
	case TH1520_ADC_DIFFERENTIAL_INPUTS:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELDIFF_DIFFERENTIAL_INPUTS;
		break;
	default:
		break;
	}

	switch (adc_feature->vol_ref) {
	case TH1520_ADC_VOL_VREF:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELBG_EXTERNAL |
			   TH1520_ADC_PHY_CFG_SELREF_EXT;
		break;
	case TH1520_ADC_VOL_INTE:
		phy_cfg |= TH1520_ADC_PHY_CFG_SELBG_INTERNAL |
			   TH1520_ADC_PHY_CFG_SELREF_INTERNAL;
		break;
	default:
		break;
	}

	/* op_ctrl */
	switch (adc_feature->conv_mode) {
	case TH1520_ADC_MODE_SINGLE:
		op_ctrl |= TH1520_ADC_OP_CTRL_MODE_SINGLE;
		break;
	case TH1520_ADC_MODE_CONTINOUS:
		op_ctrl |= TH1520_ADC_OP_CTRL_MODE_CONTINOUS;
		break;
	default:
		break;
	}

	writel(phy_cfg, info->regs + TH1520_ADC_PHY_CFG);
	writel(op_ctrl, info->regs + TH1520_ADC_OP_CTRL);

	writel(TH1520_ADC_PHY_ENCTR, info->regs + TH1520_ADC_PHY_TEST);

	/* disable the irq */
	writel(0xff, info->regs + TH1520_ADC_INT_CTRL1);
	writel(0xff, info->regs + TH1520_ADC_INT_CTRL2);

	if (adc_feature->conv_mode == TH1520_ADC_MODE_CONTINOUS)
		writel(TH1520_ADC_PHY_CTRL_ENADC_EN,
		       info->regs + TH1520_ADC_PHY_CTRL);
}

static void th1520_adc_fclk_set(struct th1520_adc *info)
{
	int fclk_ctrl = 0;
	int start_time = 0;
	int sample_time = 0;
	struct th1520_adc_feature *adc_feature = &info->adc_feature;

	switch (adc_feature->clk_sel) {
	case TH1520_ADC_FCLK_TYP_1M:
		fclk_ctrl = TH1520_ADC_FCLK_CTRL_TYP_1M;
		start_time = TH1520_ADC_START_TIME_TYP_1M;
		if (adc_feature->selres_sel == TH1520_ADC_SELRES_6BIT)
			sample_time = TH1520_ADC_SAMPLE_TIME_TYP_6BIT;
		else if (adc_feature->selres_sel == TH1520_ADC_SELRES_8BIT)
			sample_time = TH1520_ADC_SAMPLE_TIME_TYP_8BIT;
		else if (adc_feature->selres_sel == TH1520_ADC_SELRES_10BIT)
			sample_time = TH1520_ADC_SAMPLE_TIME_TYP_10BIT;
		else if (adc_feature->selres_sel == TH1520_ADC_SELRES_12BIT)
			sample_time = TH1520_ADC_SAMPLE_TIME_TYP_12BIT;
		else {
			pr_err("[%s,%d]invalid selres select\n",
			       __func__, __LINE__);
			return;
		}
		break;
	default:
		break;
	}
	writel(fclk_ctrl, info->regs + TH1520_ADC_FCLK_CTRL);
	writel(start_time, info->regs + TH1520_ADC_START_TIME);
	writel(sample_time, info->regs + TH1520_ADC_SAMPLE_TIME);
}

static void th1520_adc_hw_init(struct th1520_adc *info)
{
	th1520_adc_reg_set(info);
	th1520_adc_fclk_set(info);
}

static const struct iio_chan_spec th1520_adc_iio_channels[] = {
	TH1520_ADC_CHAN(0, IIO_VOLTAGE),
	TH1520_ADC_CHAN(1, IIO_VOLTAGE),
	TH1520_ADC_CHAN(2, IIO_VOLTAGE),
	TH1520_ADC_CHAN(3, IIO_VOLTAGE),
	TH1520_ADC_CHAN(4, IIO_VOLTAGE),
	TH1520_ADC_CHAN(5, IIO_VOLTAGE),
	TH1520_ADC_CHAN(6, IIO_VOLTAGE),
	TH1520_ADC_CHAN(7, IIO_VOLTAGE),
	/* sentinel */
};

static irqreturn_t th1520_adc_isr(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct th1520_adc *info = iio_priv(indio_dev);
	/* TBD */
	complete(&info->completion);
	return IRQ_HANDLED;
}

static int th1520_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int tmp;
	long ret;
	struct th1520_adc *info = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&info->mlock);

		if (info->adc_feature.conv_mode == TH1520_ADC_MODE_SINGLE) {
			uint ievent;
			uint vld_flag;
			uint phy_ctrl;
			uint real_chan;
			uint op_ctrl = 0;
			uint single_retry = TH1520_ADC_FIFO_DATA_SIZE;

			op_ctrl = readl(info->regs + TH1520_ADC_OP_CTRL);
			op_ctrl &= ~TH1520_ADC_OP_CTRL_CH_EN_ALL;
			op_ctrl |= (BIT(chan->channel +	TH1520_ADC_OP_CTRL_CH_EN_0) &
					TH1520_ADC_OP_CTRL_CH_EN_ALL);
			writel(op_ctrl, info->regs + TH1520_ADC_OP_CTRL);

			writel(TH1520_ADC_PHY_CTRL_ENADC_EN,
			       info->regs + TH1520_ADC_PHY_CTRL);

			vld_flag = TH1520_ADC_SAMPLE_DATA_CH0_VLD;

			while (single_retry--) {
				writel(TH1520_ADC_OP_SINGLE_START_EN,
				       info->regs + TH1520_ADC_OP_SINGLE_START);
				/* wait the sampling result */
				ret = readl_poll_timeout(info->regs +
							 TH1520_ADC_SAMPLE_DATA,
							 ievent,
							 ievent & vld_flag, 100,
							 TH1520_ADC_TIMEOUT);
				if (ret)
					pr_err("wait the sampling timeout\n");

				real_chan =
				(ievent & TH1520_ADC_SAMPLE_DATA_CH0_NUMBER) >>
				TH1520_ADC_SAMPLE_DATA_CH0_NUMBER_OFF;
				if (real_chan == chan->channel)
					break;
			}

			phy_ctrl = readl(info->regs + TH1520_ADC_PHY_CTRL);
			phy_ctrl &= ~TH1520_ADC_PHY_CTRL_ENADC_EN;
			writel(phy_ctrl, info->regs + TH1520_ADC_PHY_CTRL);

			/* read the sampling data */
			*val = (ievent & TH1520_ADC_SAMPLE_DATA_CH0) >>
			       TH1520_ADC_SAMPLE_DATA_CH0_OFF;
		} else {
			uint ievent;
			uint vld_flag;
			uint op_single;
			uint op_ctrl = 0;

			op_ctrl = readl(info->regs + TH1520_ADC_OP_CTRL);
			op_ctrl &= ~TH1520_ADC_OP_CTRL_CH_EN_ALL;
			op_ctrl |= (BIT(chan->channel + TH1520_ADC_OP_CTRL_CH_EN_0) &
				   TH1520_ADC_OP_CTRL_CH_EN_ALL);
			writel(op_ctrl, info->regs + TH1520_ADC_OP_CTRL);

			op_single = readl(info->regs +
					  TH1520_ADC_OP_SINGLE_START);
			op_single &= ~TH1520_ADC_OP_SINGLE_START_EN;
			writel(op_single,
			       info->regs + TH1520_ADC_OP_SINGLE_START);

			vld_flag = TH1520_ADC_SAMPLE_DATA_CH0_VLD |
				   TH1520_ADC_SAMPLE_DATA_CH1_VLD;

			/* wait the sampling result */
			ret  = readl_poll_timeout(info->regs +
							TH1520_ADC_SAMPLE_DATA,
						  ievent, ievent & vld_flag, 10,
						  TH1520_ADC_TIMEOUT);
			if (ret)
				pr_err("wait the sampling timeout\n");

			/* read the sampling data */
			tmp = readl(info->regs + TH1520_ADC_SAMPLE_DATA);
			if (tmp & TH1520_ADC_SAMPLE_DATA_CH0_VLD)
				*val = (tmp & TH1520_ADC_SAMPLE_DATA_CH0) >>
				       TH1520_ADC_SAMPLE_DATA_CH0_OFF;
			else
				*val = (tmp & TH1520_ADC_SAMPLE_DATA_CH1) >>
				       TH1520_ADC_SAMPLE_DATA_CH1_OFF;
		}

		mutex_unlock(&info->mlock);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = info->vref_uv / 1000;
		*val2 = info->adc_feature.selres_sel;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = info->current_clk;
		*val2 = 0;
		return IIO_VAL_INT;

	default:
		break;
	}

	return -EINVAL;
}

static void th1520_adc_reset(struct th1520_adc *info)
{
	u32 tmp = readl(info->regs + TH1520_ADC_PHY_CTRL);

	tmp |= TH1520_ADC_PHY_CTRL_RST_EN;
	writel(tmp, info->regs + TH1520_ADC_PHY_CTRL);

	usleep_range(10, 15);

	tmp &= ~TH1520_ADC_PHY_CTRL_RST_EN;
	writel(tmp, info->regs + TH1520_ADC_PHY_CTRL);
}

static void th1520_adc_set_clk(struct th1520_adc *info, int val)
{
	u32 count;
	u32 apb_clk;
	int fclk_ctrl;

	apb_clk = clk_get_rate(info->clk);
	count = DIV_ROUND_UP(apb_clk, val);
	info->current_clk = apb_clk / count;

	fclk_ctrl = readl(info->regs + TH1520_ADC_FCLK_CTRL);
	fclk_ctrl &= ~TH1520_ADC_FCLK_CTRL_FCLLK_DIV;
	fclk_ctrl |= count;
	writel(fclk_ctrl, info->regs + TH1520_ADC_FCLK_CTRL);
}

static int th1520_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct th1520_adc *info = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;

	mutex_lock(&info->mlock);
	th1520_adc_set_clk(info, val);
	mutex_unlock(&info->mlock);

	return 0;
}

static const struct iio_info th1520_adc_iio_info = {
	.read_raw = &th1520_read_raw,
	.write_raw = &th1520_write_raw,
};

static const struct of_device_id th1520_adc_match[] = {
	{ .compatible = "thead,th1520-adc", },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, th1520_adc_match);

static ssize_t th1520_adc_res_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	size_t bufpos = 0, count = 5;
	struct platform_device *pdev = to_platform_device(dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct th1520_adc *info = iio_priv(indio_dev);

	snprintf(buf + bufpos, count - bufpos, "%.*x: ", 4,
		 info->adc_feature.selres_sel);
	bufpos += 4;
	buf[bufpos++] = '\n';

	return bufpos;
}

static ssize_t th1520_adc_res_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	unsigned long res;
	char *start = (char *)buf;
	struct platform_device *pdev = to_platform_device(dev);
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct th1520_adc *info = iio_priv(indio_dev);

	if (kstrtoul(start, 0, &res))
		return -EINVAL;

	switch (res) {
	case TH1520_ADC_SELRES_6BIT:
	case TH1520_ADC_SELRES_8BIT:
	case TH1520_ADC_SELRES_10BIT:
	case TH1520_ADC_SELRES_12BIT:
		info->adc_feature.selres_sel = res;
		th1520_adc_reset(info);
		th1520_adc_hw_init(info);
		break;
	default:
		dev_err(dev, "not support res\n");
		return -EINVAL;
	}

	return size;
}

static DEVICE_ATTR_RW(th1520_adc_res);

static int th1520_adc_probe(struct platform_device *pdev)
{
	int irq;
	int ret;
	struct resource *mem;
	struct th1520_adc *info;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					  sizeof(struct th1520_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "Failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	info->dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(info->dev, irq, th1520_adc_isr, 0,
			       dev_name(&pdev->dev), indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n", irq);
		return ret;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	info->clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock, err = %ld\n",
			PTR_ERR(info->clk));
		return PTR_ERR(info->clk);
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref))
		return PTR_ERR(info->vref);

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	info->vref_uv = regulator_get_voltage(info->vref);

	platform_set_drvdata(pdev, indio_dev);

	mutex_init(&info->mlock);
	init_completion(&info->completion);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &th1520_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = th1520_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(th1520_adc_iio_channels);

	ret = clk_prepare_enable(info->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Could not prepare or enable the clock.\n");
		goto error_adc_clk_enable;
	}

	th1520_adc_cfg_init(info);
	th1520_adc_reset(info);
	th1520_adc_hw_init(info);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto error_iio_device_register;
	}

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_th1520_adc_res.attr);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create adc debug sysfs.\n");
		goto error_iio_device_register;
	}

	dev_info(&pdev->dev, "THEAD TH1520 adc registered.\n");
	return 0;

error_iio_device_register:
	clk_disable_unprepare(info->clk);
error_adc_clk_enable:
	regulator_disable(info->vref);

	return ret;
}

static int th1520_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct th1520_adc *info = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	regulator_disable(info->vref);
	clk_disable_unprepare(info->clk);

	return 0;
}

static int __maybe_unused th1520_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct th1520_adc *info = iio_priv(indio_dev);
	int phy_ctrl;

	phy_ctrl = readl(info->regs + TH1520_ADC_PHY_CTRL);
	phy_ctrl &= ~TH1520_ADC_PHY_CTRL_ENADC_EN;
	writel(phy_ctrl, info->regs + TH1520_ADC_PHY_CTRL);

	clk_disable_unprepare(info->clk);
	regulator_disable(info->vref);

	return 0;
}

static int __maybe_unused th1520_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct th1520_adc *info = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	ret = clk_prepare_enable(info->clk);
	if (ret)
		goto disable_reg;

	th1520_adc_reset(info);
	th1520_adc_set_clk(info, info->current_clk);
	th1520_adc_hw_init(info);

	return 0;

disable_reg:
	regulator_disable(info->vref);
	return ret;
}

static SIMPLE_DEV_PM_OPS(th1520_adc_pm_ops,
			 th1520_adc_suspend, th1520_adc_resume);

static struct platform_driver th1520_adc_driver = {
	.probe          = th1520_adc_probe,
	.remove         = th1520_adc_remove,
	.driver         = {
		.name   = DRIVER_NAME,
		.of_match_table = th1520_adc_match,
		.pm     = &th1520_adc_pm_ops,
	},
};
module_platform_driver(th1520_adc_driver);

MODULE_AUTHOR("fugang.duan <duanfugang.dfg@linux.alibaba.com>");
MODULE_DESCRIPTION("Thead TH1520 ADC driver");
MODULE_LICENSE("GPL");

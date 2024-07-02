/* SPDX-License-Identifier: GPL-2.0 */
/*
 * XuanTie TH1520 ADC driver
 *
 * Copyright (C) 2021-2024 Alibaba Group Holding Limited.
 * Fugang Duan <duanfugang.dfg@linux.alibaba.com>
 *
 */

#include <linux/bitops.h>

/* This will be the driver name the kernel reports */
#define DRIVER_NAME "xuantie-th1520-adc"

/* ADC registers */
#define TH1520_ADC_PHY_CFG			0x00
#define TH1520_ADC_PHY_CTRL			0x04
#define TH1520_ADC_PHY_TEST			0x08
#define TH1520_ADC_OP_CTRL			0x0C
#define TH1520_ADC_OP_SINGLE_START		0x10
#define TH1520_ADC_FCLK_CTRL			0x14
#define TH1520_ADC_START_TIME			0x18
#define TH1520_ADC_SAMPLE_TIME			0x1C
#define TH1520_ADC_SAMPLE_DATA			0x20
#define TH1520_ADC_INT_CTRL1			0x50
#define TH1520_ADC_INT_CTRL2			0x54
#define TH1520_ADC_INT_STATUS			0x58
#define TH1520_ADC_INT_ACTUAL_VALUE_CH0		0x60
#define TH1520_ADC_INT_ACTUAL_VALUE_CH1		0x64
#define TH1520_ADC_INT_DELTA_VALUE_CH0		0x90
#define TH1520_ADC_INT_DELTA_VALUE_CH1		0x94

/* Configuration register field define */
#define TH1520_ADC_PHY_CFG_SELRES_6BIT			(0x0)
#define TH1520_ADC_PHY_CFG_SELRES_8BIT			(0x1)
#define TH1520_ADC_PHY_CFG_SELRES_10BIT			(0x2)
#define TH1520_ADC_PHY_CFG_SELRES_12BIT			(0x3)
#define TH1520_ADC_PHY_CFG_SELDIFF_SINGLE_ENDED_INPUTS	(0x0 << 4)
#define TH1520_ADC_PHY_CFG_SELDIFF_DIFFERENTIAL_INPUTS	(0x1 << 4)
#define TH1520_ADC_PHY_CFG_SELBG_INTERNAL		(0x1 << 8)
#define TH1520_ADC_PHY_CFG_SELBG_EXTERNAL		(0x0 << 8)
#define TH1520_ADC_PHY_CFG_SELREF_INTERNAL		(0x1 << 12)
#define TH1520_ADC_PHY_CFG_SELREF_EXT			(0x0 << 12)

/* PHY CTRL register field define */
#define TH1520_ADC_PHY_CTRL_ENOFFSET_EN			(0x1 << 12)
#define TH1520_ADC_PHY_CTRL_ENOFFMEAS_EN		(0x1 << 8)
#define TH1520_ADC_PHY_CTRL_RST_EN			(0x1 << 4)
#define TH1520_ADC_PHY_CTRL_ENADC_EN			(0x1 << 0)

/* ADC OP ctrl field define  */
#define TH1520_ADC_OP_CTRL_CH_EN_ALL			GENMASK(19, 12)
#define TH1520_ADC_OP_CTRL_CH_EN_0			(12)
#define TH1520_ADC_OP_CTRL_MODE_SINGLE			(0x1 << 0)
#define TH1520_ADC_OP_CTRL_MODE_CONTINOUS		(0x0 << 0)

/* ADC OP single start */
#define TH1520_ADC_OP_SINGLE_START_EN			BIT(0)

/* ADC fclk ctrl */
#define TH1520_ADC_FCLK_CTRL_FCLLK_DIV			GENMASK(6, 0)
#define TH1520_ADC_FCLK_CTRL_TYP_1M			(0x10004)
#define TH1520_ADC_START_TIME_TYP_1M			(0x160)
#define TH1520_ADC_SAMPLE_TIME_TYP_1M			(0x10)
#define TH1520_ADC_SAMPLE_TIME_TYP_6BIT			(8)
#define TH1520_ADC_SAMPLE_TIME_TYP_8BIT			(10)
#define TH1520_ADC_SAMPLE_TIME_TYP_10BIT		(12)
#define TH1520_ADC_SAMPLE_TIME_TYP_12BIT		(14)

/* ADC sample data */
#define TH1520_ADC_SAMPLE_DATA_CH1			GENMASK(27, 16)
#define TH1520_ADC_SAMPLE_DATA_CH1_OFF			(16)
#define TH1520_ADC_SAMPLE_DATA_CH1_VLD			BIT(31)
#define TH1520_ADC_SAMPLE_DATA_CH1_NUMBER		GENMASK(30, 28)
#define TH1520_ADC_SAMPLE_DATA_CH1_NUMBER_OFF		(28)
#define TH1520_ADC_SAMPLE_DATA_CH0			GENMASK(11, 0)
#define TH1520_ADC_SAMPLE_DATA_CH0_VLD			BIT(15)
#define TH1520_ADC_SAMPLE_DATA_CH0_OFF			(0)
#define TH1520_ADC_SAMPLE_DATA_CH0_NUMBER		GENMASK(14, 12)
#define TH1520_ADC_SAMPLE_DATA_CH0_NUMBER_OFF		(12)

/* ADC INT Ctrl */
#define TH1520_ADC_INT_CTRL1_CH1_INT_MODE		BIT(1)
#define TH1520_ADC_INT_CTRL1_CH0_INT_MODE		BIT(0)
#define TH1520_ADC_INT_CTRL2_CH1_INT_MASK		BIT(1)
#define TH1520_ADC_INT_CTRL2_CH0_INT_MASK		BIT(0)
#define TH1520_ADC_INT_STS_CH1_INT_STS			BIT(1)
#define TH1520_ADC_INT_STS_CH0_INT_STS			BIT(0)

#define TH1520_ADC_ACTUAL_VALUE_CH0_HVAL		GENMASK(27, 16)
#define TH1520_ADC_ACTUAL_VALUE_CH0_HVAL_OFF		(16)
#define TH1520_ADC_ACTUAL_VALUE_CH0_LVAL		GENMASK(11, 0)
#define TH1520_ADC_ACTUAL_VALUE_CH0_LVAL_OFF		(0)
#define TH1520_ADC_ACTUAL_VALUE_CH1_HVAL		GENMASK(27, 16)
#define TH1520_ADC_ACTUAL_VALUE_CH1_HVAL_OFF		(16)
#define TH1520_ADC_ACTUAL_VALUE_CH1_LVAL		GENMASK(11, 0)
#define TH1520_ADC_ACTUAL_VALUE_CH1_LVAL_OFF		(0)

#define TH1520_ADC_DLT_VALUE_CH0_HVAL			GENMASK(27, 16)
#define TH1520_ADC_DLT_VALUE_CH0_HVAL_OFF		(16)
#define TH1520_ADC_DLT_VALUE_CH0_LVAL			GENMASK(11, 0)
#define TH1520_ADC_DLT_VALUE_CH0_LVAL_OFF		(0)
#define TH1520_ADC_DLT_VALUE_CH1_HVAL			GENMASK(27, 16)
#define TH1520_ADC_DLT_VALUE_CH1_HVAL_OFF		(16)
#define TH1520_ADC_DLT_VALUE_CH1_LVAL			GENMASK(11, 0)
#define TH1520_ADC_DLT_VALUE_CH1_LVAL_OFF		(0)

#define TH1520_ADC_FIFO_DATA_SIZE			32
#define TH1520_ADC_PHY_ENCTR				0x8e0
#define TH1520_ADC_TIMEOUT				500000

#define TH1520_ADC_CHAN(_idx, _chan_type) {			\
	.type = (_chan_type),					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

enum vol_ref {
	TH1520_ADC_VOL_VREF,
	TH1520_ADC_VOL_INTE,
};

enum input_mode_sel {
	TH1520_ADC_SINGLE_ENDED_INPUTS,
	TH1520_ADC_DIFFERENTIAL_INPUTS,
};

enum selres_sel {
	TH1520_ADC_SELRES_6BIT = 6,
	TH1520_ADC_SELRES_8BIT = 8,
	TH1520_ADC_SELRES_10BIT = 10,
	TH1520_ADC_SELRES_12BIT = 12,
};

enum offset_mode_sel {
	TH1520_ADC_OFFSET_DIS = 0,
	TH1520_ADC_OFFSET_EN,
};

enum conversion_mode_sel {
	TH1520_ADC_MODE_SINGLE,
	TH1520_ADC_MODE_CONTINOUS,
};

enum clk_sel {
	TH1520_ADC_FCLK_TYP_1M,
};

enum int_actual_mask {
	TH1520_ADC_ACTUAL_CH0,
	TH1520_ADC_ACTUAL_CH1,
	TH1520_ADC_ACTUAL_ALL,

};

enum int_delta_mask {
	TH1520_ADC_DETAL_CH0,
	TH1520_ADC_DETAL_CH1,
	TH1520_ADC_DETAL_ALL,
};

struct th1520_adc_feature {
	enum selres_sel			selres_sel;
	enum input_mode_sel		input_mode;
	enum vol_ref			vol_ref;
	enum offset_mode_sel		offset_mode;
	enum conversion_mode_sel	conv_mode;
	enum clk_sel			clk_sel;
	enum int_actual_mask		int_actual;
	enum int_delta_mask		int_detal;
};

struct th1520_adc {
	struct device			*dev;
	void __iomem			*regs;
	struct clk			*clk;

	u32				vref_uv;
	u32				value;
	struct regulator		*vref;
	struct th1520_adc_feature	adc_feature;
	u32				current_clk;
	u32				ch0_offmeas;
	u32				ch1_offmeas;

	struct completion		completion;
	/* lock to protect against multiple access to the device */
	struct mutex			mlock;
};

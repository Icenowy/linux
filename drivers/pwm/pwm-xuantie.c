// SPDX-License-Identifier: GPL-2.0
/*
 * XuanTie PWM driver
 *
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (C) 2023 Jisheng Zhang <jszhang@kernel.org>
 *
 * Limitations:
 * - The XUANTIE_PWM_CTRL_START bit is only effective when 0 -> 1, which is used
 *   to start the channel, 1 -> 0 doesn't change anything. so 0 % duty cycle
 *   is used to "disable" the channel.
 * - The XUANTIE_PWM_CTRL_START bit is automatically cleared once PWM channel is
 *   started.
 * - The XUANTIE_PWM_CFG_UPDATE atomically updates and only updates period and duty.
 * - To update period and duty, XUANTIE_PWM_CFG_UPDATE needs to go through 0 -> 1
 *   step, I.E if XUANTIE_PWM_CFG_UPDATE is already 1, it's necessary to clear it
 *   to 0 beforehand.
 * - Polarity can only be changed if never started before.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define XUANTIE_PWM_MAX_NUM		6
#define XUANTIE_PWM_MAX_PERIOD		GENMASK(31, 0)
#define XUANTIE_PWM_MAX_DUTY		GENMASK(31, 0)

#define XUANTIE_PWM_CHN_BASE(n)		((n) * 0x20)
#define XUANTIE_PWM_CTRL(n)		(XUANTIE_PWM_CHN_BASE(n) + 0x00)
#define  XUANTIE_PWM_CTRL_START		BIT(0)
#define  XUANTIE_PWM_CTRL_SOFT_RST		BIT(1)
#define  XUANTIE_PWM_CTRL_CFG_UPDATE	BIT(2)
#define  XUANTIE_PWM_CTRL_INTEN		BIT(3)
#define  XUANTIE_PWM_CTRL_MODE		GENMASK(5, 4)
#define  XUANTIE_PWM_CTRL_MODE_CONTINUOUS	FIELD_PREP(XUANTIE_PWM_CTRL_MODE, 2)
#define  XUANTIE_PWM_CTRL_EVTRIG		GENMASK(7, 6)
#define  XUANTIE_PWM_CTRL_FPOUT		BIT(8)
#define  XUANTIE_PWM_CTRL_INFACTOUT	BIT(9)
#define XUANTIE_PWM_RPT(n)		(XUANTIE_PWM_CHN_BASE(n) + 0x04)
#define XUANTIE_PWM_PER(n)		(XUANTIE_PWM_CHN_BASE(n) + 0x08)
#define XUANTIE_PWM_FP(n)			(XUANTIE_PWM_CHN_BASE(n) + 0x0c)
#define XUANTIE_PWM_STATUS(n)		(XUANTIE_PWM_CHN_BASE(n) + 0x10)
#define  XUANTIE_PWM_STATUS_CYCLE		GENMASK(7, 0)

struct xuantie_pwm_chip {
	struct pwm_chip chip;
	void __iomem *mmio_base;
	struct clk *clk;
	u8 channel_ever_started;
};

static inline struct xuantie_pwm_chip *xuantie_pwm_from_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct xuantie_pwm_chip, chip);
}

static int xuantie_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct xuantie_pwm_chip *priv = xuantie_pwm_from_chip(chip);
	u32 val = XUANTIE_PWM_CTRL_INFACTOUT | XUANTIE_PWM_CTRL_FPOUT | XUANTIE_PWM_CTRL_MODE_CONTINUOUS;
	u64 period_cycle, duty_cycle, rate;
	int ret;

	/* if ever started, can't change the polarity */
	if ((priv->channel_ever_started & (1 << pwm->hwpwm)) &&
	    state->polarity != pwm->state.polarity)
		return -EINVAL;

	if (!state->enabled) {
		if (pwm->state.enabled) {
			val = readl(priv->mmio_base + XUANTIE_PWM_CTRL(pwm->hwpwm));
			val &= ~XUANTIE_PWM_CTRL_CFG_UPDATE;
			writel(val, priv->mmio_base + XUANTIE_PWM_CTRL(pwm->hwpwm));

			writel(0, priv->mmio_base + XUANTIE_PWM_FP(pwm->hwpwm));

			val |= XUANTIE_PWM_CTRL_CFG_UPDATE;
			writel(val, priv->mmio_base + XUANTIE_PWM_CTRL(pwm->hwpwm));
			pm_runtime_put_sync(chip->dev);
		}
		return 0;
	}

	if (!pwm->state.enabled) {
		ret = pm_runtime_resume_and_get(chip->dev);
		if (ret < 0)
			return ret;
	}

	if (state->polarity == PWM_POLARITY_INVERSED)
		val &= ~XUANTIE_PWM_CTRL_FPOUT;

	writel(val, priv->mmio_base + XUANTIE_PWM_CTRL(pwm->hwpwm));

	rate = clk_get_rate(priv->clk);
	/*
	 * The following calculations might overflow if clk is bigger
	 * than 1 GHz. In practise it's 24MHz, so this limitation
	 * is only theoretic.
	 */
	if (rate > NSEC_PER_SEC)
		return -EINVAL;

	period_cycle = mul_u64_u64_div_u64(rate, state->period, NSEC_PER_SEC);
	if (period_cycle > XUANTIE_PWM_MAX_PERIOD)
		period_cycle = XUANTIE_PWM_MAX_PERIOD;
	/*
	 * With limitation above we have period_cycle <= XUANTIE_PWM_MAX_PERIOD,
	 * so this cannot overflow.
	 */
	writel(period_cycle, priv->mmio_base + XUANTIE_PWM_PER(pwm->hwpwm));

	duty_cycle = mul_u64_u64_div_u64(rate, state->duty_cycle, NSEC_PER_SEC);
	if (duty_cycle > XUANTIE_PWM_MAX_DUTY)
		duty_cycle = XUANTIE_PWM_MAX_DUTY;
	/*
	 * With limitation above we have duty_cycle <= XUANTIE_PWM_MAX_DUTY,
	 * so this cannot overflow.
	 */
	writel(duty_cycle, priv->mmio_base + XUANTIE_PWM_FP(pwm->hwpwm));

	val |= XUANTIE_PWM_CTRL_CFG_UPDATE;
	writel(val, priv->mmio_base + XUANTIE_PWM_CTRL(pwm->hwpwm));

	if (!pwm->state.enabled) {
		val |= XUANTIE_PWM_CTRL_START;
		writel(val, priv->mmio_base + XUANTIE_PWM_CTRL(pwm->hwpwm));
		priv->channel_ever_started |= 1 << pwm->hwpwm;
	}

	return 0;
}

static int xuantie_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct xuantie_pwm_chip *priv = xuantie_pwm_from_chip(chip);
	u64 rate = clk_get_rate(priv->clk);
	u32 val;
	int ret;

	ret = pm_runtime_resume_and_get(chip->dev);
	if (ret < 0)
		return ret;

	val = readl(priv->mmio_base + XUANTIE_PWM_CTRL(pwm->hwpwm));
	if (val & XUANTIE_PWM_CTRL_FPOUT)
		state->polarity = PWM_POLARITY_NORMAL;
	else
		state->polarity = PWM_POLARITY_INVERSED;

	val = readl(priv->mmio_base + XUANTIE_PWM_PER(pwm->hwpwm));
	/*
	 * val 32 bits, multiply NSEC_PER_SEC, won't overflow.
	 */
	state->period = DIV64_U64_ROUND_UP((u64)val * NSEC_PER_SEC, rate);

	val = readl(priv->mmio_base + XUANTIE_PWM_FP(pwm->hwpwm));
	state->enabled = !!val;
	/*
	 * val 32 bits, multiply NSEC_PER_SEC, won't overflow.
	 */
	state->duty_cycle = DIV64_U64_ROUND_UP((u64)val * NSEC_PER_SEC, rate);

	pm_runtime_put_sync(chip->dev);

	return 0;
}

static const struct pwm_ops xuantie_pwm_ops = {
	.apply = xuantie_pwm_apply,
	.get_state = xuantie_pwm_get_state,
};

static int __maybe_unused xuantie_pwm_runtime_suspend(struct device *dev)
{
	struct xuantie_pwm_chip *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused xuantie_pwm_runtime_resume(struct device *dev)
{
	struct xuantie_pwm_chip *priv = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		dev_err(dev, "failed to enable pwm clock(%pe)\n", ERR_PTR(ret));

	return ret;
}

static int xuantie_pwm_probe(struct platform_device *pdev)
{
	struct xuantie_pwm_chip *priv;
	int ret, i;
	u32 val;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->mmio_base))
		return PTR_ERR(priv->mmio_base);

	priv->clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->chip.ops = &xuantie_pwm_ops;
	priv->chip.dev = &pdev->dev;
	priv->chip.npwm = XUANTIE_PWM_MAX_NUM;

	/* check whether PWM is ever started or not */
	for (i = 0; i < priv->chip.npwm; i++) {
		val = readl(priv->mmio_base + XUANTIE_PWM_FP(i));
		if (val)
			priv->channel_ever_started |= 1 << i;
	}

	ret = devm_pwmchip_add(&pdev->dev, &priv->chip);
	if (ret)
		return ret;

	devm_pm_runtime_enable(&pdev->dev);

	return 0;
}

static const struct of_device_id xuantie_pwm_dt_ids[] = {
	{.compatible = "xuantie,th1520-pwm",},
	{/* sentinel */}
};
MODULE_DEVICE_TABLE(of, xuantie_pwm_dt_ids);

static const struct dev_pm_ops xuantie_pwm_pm_ops = {
	SET_RUNTIME_PM_OPS(xuantie_pwm_runtime_suspend, xuantie_pwm_runtime_resume, NULL)
};

static struct platform_driver xuantie_pwm_driver = {
	.driver = {
		.name = "xuantie-pwm",
		.of_match_table = xuantie_pwm_dt_ids,
		.pm = &xuantie_pwm_pm_ops,
	},
	.probe = xuantie_pwm_probe,
};
module_platform_driver(xuantie_pwm_driver);

MODULE_AUTHOR("Wei Liu <lw312886@linux.alibaba.com>");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("XuanTie pwm driver");
MODULE_LICENSE("GPL");

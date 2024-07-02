/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "clk.h"

#define TH1520_PLL_CFG0		0x0
#define TH1520_PLL_CFG1		0x04
#define TH1520_PLL_CFG2		0x8
#define TH1520_POSTDIV2_SHIFT	24
#define TH1520_POSTDIV2_MASK	GENMASK(26, 24)
#define TH1520_POSTDIV1_SHIFT	20
#define TH1520_POSTDIV1_MASK	GENMASK(22, 20)
#define TH1520_FBDIV_SHIFT	8
#define TH1520_FBDIV_MASK	GENMASK(19, 8)
#define TH1520_REFDIV_SHIFT	0
#define TH1520_REFDIV_MASK	GENMASK(5, 0)
#define TH1520_BYPASS_MASK	BIT(30)
#define TH1520_RST_MASK		BIT(29)
#define TH1520_DSMPD_MASK	BIT(24)
#define TH1520_DACPD_MASK	BIT(25)
#define TH1520_FRAC_MASK		GENMASK(23, 0)
#define TH1520_FRAC_SHIFT	0
#define TH1520_FRAC_DIV		BIT(24)

#define LOCK_TIMEOUT_US		10000

#define div_mask(d)	((1 << (d->width)) - 1)

DEFINE_SPINLOCK(xuantie_th1520_clk_lock);

enum th1520_pll_mode {
        PLL_MODE_FRAC,
        PLL_MODE_INT,
};

struct clk_th1520pll {
	struct clk_hw			hw;
	void __iomem			*base;
	enum th1520_pll_clktype		clk_type;
	enum th1520_pll_outtype		out_type;
	enum th1520_pll_mode		pll_mode;
	const struct th1520_pll_rate_table *rate_table;
	int rate_count;

	u32 cfg0_reg_off;
	u32 pll_sts_off;
	int pll_lock_bit;

	/* Th1520 MPW Aon/ddr pll define bypass:rst bits as: 31:30
	 * but AP pll define bypass:rst bits as: 30:29
	 *
	 * Th1520 Fullmask align these register field define, all pll
	 * define bypss:rst bits as: 30:29
	 */
	int pll_rst_bit;
	int pll_bypass_bit;
};

struct clk_th1520div {
	struct clk_divider divider;
	enum th1520_div_type div_type;
	u16 min_div;
	u16 max_div;
	u8 sync_en;
	const struct clk_ops *ops;
};

struct clk_th1520gate {
	struct clk_gate gate;
	unsigned int *share_count;
	const struct clk_ops *ops;
};

#define to_clk_th1520pll(_hw) container_of(_hw, struct clk_th1520pll, hw)

void xuantie_unregister_clocks(struct clk *clks[], unsigned int count)
{
        unsigned int i;

        for (i = 0; i < count; i++)
                clk_unregister(clks[i]);
}

static void clk_th1520_pll_cfg_init(struct clk_th1520pll *pll)
{
	switch (pll->clk_type) {
	case TH1520_AUDIO_PLL:
		pll->cfg0_reg_off = 0x0;
		pll->pll_sts_off = 0x90;
		pll->pll_lock_bit = BIT(0);
		pll->pll_bypass_bit = BIT(31);
		pll->pll_rst_bit = BIT(30);
		pll->pll_mode = PLL_MODE_FRAC;
		break;
	case TH1520_SYS_PLL:
		pll->cfg0_reg_off = 0x10;
		pll->pll_sts_off = 0x90;
		pll->pll_lock_bit = BIT(1);
		pll->pll_bypass_bit = BIT(31);
		pll->pll_rst_bit = BIT(30);
		pll->pll_mode = PLL_MODE_FRAC;
		break;
	case TH1520_CPU_PLL0:
		pll->cfg0_reg_off = 0x0;
		pll->pll_sts_off = 0x80;
		pll->pll_lock_bit = BIT(1);
		pll->pll_bypass_bit = BIT(30);
		pll->pll_rst_bit = BIT(29);
		pll->pll_mode = PLL_MODE_INT;
		break;
	case TH1520_CPU_PLL1:
		pll->cfg0_reg_off = 0x10;
		pll->pll_sts_off = 0x80;
		pll->pll_lock_bit = BIT(4);
		pll->pll_bypass_bit = BIT(30);
		pll->pll_rst_bit = BIT(29);
		pll->pll_mode = PLL_MODE_INT;
		break;
	case TH1520_GMAC_PLL:
		pll->cfg0_reg_off = 0x20;
		pll->pll_sts_off = 0x80;
		pll->pll_lock_bit = BIT(3);
		pll->pll_bypass_bit = BIT(30);
		pll->pll_rst_bit = BIT(29);
		pll->pll_mode = PLL_MODE_INT;
		break;
	case TH1520_VIDEO_PLL:
		pll->cfg0_reg_off = 0x30;
		pll->pll_sts_off = 0x80;
		pll->pll_lock_bit = BIT(7);
		pll->pll_bypass_bit = BIT(30);
		pll->pll_rst_bit = BIT(29);
		pll->pll_mode = PLL_MODE_INT;
		break;
	case TH1520_DDR_PLL:
		pll->cfg0_reg_off = 0x8;
		pll->pll_sts_off = 0x18;
		pll->pll_lock_bit = BIT(0);
		pll->pll_bypass_bit = BIT(31);
		pll->pll_rst_bit = BIT(30);
		pll->pll_mode = PLL_MODE_INT;
		break;
	case TH1520_DPU0_PLL:
		pll->cfg0_reg_off = 0x40;
		pll->pll_sts_off = 0x80;
		pll->pll_lock_bit = BIT(8);
		pll->pll_bypass_bit = BIT(30);
		pll->pll_rst_bit = BIT(29);
		pll->pll_mode = PLL_MODE_INT;
		break;
	case TH1520_DPU1_PLL:
		pll->cfg0_reg_off = 0x50;
		pll->pll_sts_off = 0x80;
		pll->pll_lock_bit = BIT(9);
		pll->pll_bypass_bit = BIT(30);
		pll->pll_rst_bit = BIT(29);
		pll->pll_mode = PLL_MODE_INT;
		break;
	default:
		pr_err("%s: Unknown pll type\n", __func__);
	};
}

static int clk_th1520_pll_wait_lock(struct clk_th1520pll *pll)
{
	u32 val;

	return readl_poll_timeout(pll->base + pll->pll_sts_off, val,
				  val & pll->pll_lock_bit, 0,
				  LOCK_TIMEOUT_US);
}

static int clk_th1520_pll_prepare(struct clk_hw *hw)
{
	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	void __iomem *cfg1_off;
	u32 val;
	int ret;

	cfg1_off = pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1;
	val = readl_relaxed(cfg1_off);
	if (!(val & pll->pll_rst_bit))
		return 0;

	/* Enable RST */
	val |= pll->pll_rst_bit;
	writel_relaxed(val, cfg1_off);

	udelay(3);

	/* Disable RST */
	val &= ~pll->pll_rst_bit;
	writel_relaxed(val, cfg1_off);

	ret = clk_th1520_pll_wait_lock(pll);
	if (ret)
		return ret;

	return 0;
}

static int clk_th1520_pll_is_prepared(struct clk_hw *hw)
{
	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1);

	return (val & pll->pll_rst_bit) ? 0 : 1;
}

static void clk_th1520_pll_unprepare(struct clk_hw *hw)
{
	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1);
	val |= pll->pll_rst_bit;
	writel_relaxed(val, pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1);
}

static unsigned long clk_th1520_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
#ifndef CONFIG_TH1520_CLK_EMU
	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	u32 refdiv, fbdiv, postdiv1, postdiv2, frac;
	u32 pll_cfg0, pll_cfg1;
	u64 fvco = 0;

	pll_cfg0 = readl_relaxed(pll->base + pll->cfg0_reg_off);
	pll_cfg1 = readl_relaxed(pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1);
	refdiv = (pll_cfg0 & TH1520_REFDIV_MASK) >> TH1520_REFDIV_SHIFT;
	fbdiv = (pll_cfg0 & TH1520_FBDIV_MASK) >> TH1520_FBDIV_SHIFT;
	postdiv1 = (pll_cfg0 & TH1520_POSTDIV1_MASK) >> TH1520_POSTDIV1_SHIFT;
	postdiv2 = (pll_cfg0 & TH1520_POSTDIV2_MASK) >> TH1520_POSTDIV2_SHIFT;
	frac = (pll_cfg1 & TH1520_FRAC_MASK) >> TH1520_FRAC_SHIFT;

	/* rate calculation:
	 * INT mode: FOUTVCO = FREE * FBDIV / REFDIV
	 * FRAC mode:FOUTVCO = (FREE * FBDIV + FREE * FRAC/BIT(24)) / REFDIV
	 */
	if (pll->pll_mode == PLL_MODE_FRAC)
		fvco = (parent_rate * frac) / TH1520_FRAC_DIV;

	fvco += (parent_rate * fbdiv);
	do_div(fvco, refdiv);

	if (pll->out_type == TH1520_PLL_DIV)
		do_div(fvco, postdiv1 * postdiv2);

	return fvco;
#else

	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	const struct th1520_pll_rate_table *rate_table = pll->rate_table;

	/* return minimum supported value */
	if (pll->out_type == TH1520_PLL_DIV)
		return rate_table[0].rate;

	return rate_table[0].vco_rate;
#endif
}

static const struct th1520_pll_rate_table *th1520_get_pll_div_settings(
		struct clk_th1520pll *pll, unsigned long rate)
{
	const struct th1520_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++)
		if (rate == rate_table[i].rate)
			return &rate_table[i];

	return NULL;
}

static const struct th1520_pll_rate_table *th1520_get_pll_vco_settings(
		struct clk_th1520pll *pll, unsigned long rate)
{
	const struct th1520_pll_rate_table *rate_table = pll->rate_table;
	int i;

	for (i = 0; i < pll->rate_count; i++)
		if (rate == rate_table[i].vco_rate)
			return &rate_table[i];

	return NULL;
}

static inline bool clk_th1520_pll_change(struct clk_th1520pll *pll,
					const struct th1520_pll_rate_table *rate)
{
	u32 refdiv_old, fbdiv_old, postdiv1_old, postdiv2_old, frac_old;
	u32 cfg0, cfg1;
	bool pll_changed;

	cfg0 = readl_relaxed(pll->base + pll->cfg0_reg_off);
	cfg1 = readl_relaxed(pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1);

	refdiv_old = (cfg0 & TH1520_REFDIV_MASK) >> TH1520_REFDIV_SHIFT;
	fbdiv_old = (cfg0 & TH1520_FBDIV_MASK) >> TH1520_FBDIV_SHIFT;
	postdiv1_old = (cfg0 & TH1520_POSTDIV1_MASK) >> TH1520_POSTDIV1_SHIFT;
	postdiv2_old = (cfg0 & TH1520_POSTDIV2_MASK) >> TH1520_POSTDIV2_SHIFT;
	frac_old = (cfg1 & TH1520_FRAC_MASK) >> TH1520_FRAC_SHIFT;

	pll_changed = rate->refdiv != refdiv_old || rate->fbdiv != fbdiv_old ||
		      rate->postdiv1 != postdiv1_old || rate->postdiv2 != postdiv2_old;
	if (pll->pll_mode == PLL_MODE_FRAC)
		pll_changed |= (rate->frac != frac_old);

	return pll_changed;
}

static int clk_th1520_pll_set_rate(struct clk_hw *hw, unsigned long drate,
				 unsigned long prate)
{
	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	const struct th1520_pll_rate_table *rate;
	void __iomem *cfg1_off;
	u32 tmp, div_val;
	int ret;

	if (pll->out_type == TH1520_PLL_VCO) {
		rate = th1520_get_pll_vco_settings(pll, drate);
		if (!rate) {
			pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
				drate, clk_hw_get_name(hw));
			return -EINVAL;
		}
	} else {
		rate = th1520_get_pll_div_settings(pll, drate);
		if (!rate) {
			pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
				drate, clk_hw_get_name(hw));
			return -EINVAL;
		}
	}

	if (!clk_th1520_pll_change(pll, rate))
		return 0;

	/* Enable RST */
	cfg1_off = pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1;
	tmp = readl_relaxed(cfg1_off);
	tmp |= pll->pll_rst_bit;
	writel_relaxed(tmp, cfg1_off);

	div_val = (rate->refdiv << TH1520_REFDIV_SHIFT) |
		  (rate->fbdiv << TH1520_FBDIV_SHIFT) |
		  (rate->postdiv1 << TH1520_POSTDIV1_SHIFT) |
		  (rate->postdiv2 << TH1520_POSTDIV2_SHIFT);
	writel_relaxed(div_val, pll->base + pll->cfg0_reg_off);

	if (pll->pll_mode == PLL_MODE_FRAC) {
		tmp &= ~(TH1520_FRAC_MASK << TH1520_FRAC_SHIFT);
		tmp |= rate->frac;
		writel_relaxed(tmp, cfg1_off);
	}

	udelay(3);

	/* Disable RST */
	tmp &= ~pll->pll_rst_bit;
	writel_relaxed(tmp, cfg1_off);

	/* Wait Lock, ~20us cost */
	ret = clk_th1520_pll_wait_lock(pll);
	if (ret)
		return ret;

	/* HW requires 30us for pll stable */
	udelay(30);

	return 0;
}

static long clk_th1520_pllvco_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	const struct th1520_pll_rate_table *rate_table = pll->rate_table;
	unsigned long best = 0, now = 0;
	unsigned int i, best_i = 0;

	for (i = 0; i < pll->rate_count; i++) {
		now = rate_table[i].vco_rate;

		if (rate == now) {
			return rate_table[i].vco_rate;
		} else if (abs(now - rate) < abs(best - rate)) {
			best = now;
			best_i = i;
		}
	}

	/* return minimum supported value */
	return rate_table[best_i].vco_rate;
}

static long clk_th1520_plldiv_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *prate)
{
	struct clk_th1520pll *pll = to_clk_th1520pll(hw);
	const struct th1520_pll_rate_table *rate_table = pll->rate_table;
	unsigned long best = 0, now = 0;
	unsigned int i, best_i = 0;

	for (i = 0; i < pll->rate_count; i++) {
		now = rate_table[i].rate;

		if (rate == now) {
			return rate_table[i].rate;
		} else if (abs(now - rate) < abs(best - rate)) {
			best = now;
			best_i = i;
		}
	}

	/* return minimum supported value */
	return rate_table[best_i].rate;
}

static const struct clk_ops clk_th1520_pll_def_ops = {
	.recalc_rate	= clk_th1520_pll_recalc_rate,
};

static const struct clk_ops clk_th1520_pllvco_ops = {
	.prepare	= clk_th1520_pll_prepare,
	.unprepare	= clk_th1520_pll_unprepare,
	.is_prepared	= clk_th1520_pll_is_prepared,
	.recalc_rate	= clk_th1520_pll_recalc_rate,
	.round_rate	= clk_th1520_pllvco_round_rate,
	.set_rate	= clk_th1520_pll_set_rate,
};

static const struct clk_ops clk_th1520_plldiv_ops = {
	.prepare	= clk_th1520_pll_prepare,
	.unprepare	= clk_th1520_pll_unprepare,
	.is_prepared	= clk_th1520_pll_is_prepared,
	.recalc_rate	= clk_th1520_pll_recalc_rate,
	.round_rate	= clk_th1520_plldiv_round_rate,
	.set_rate	= clk_th1520_pll_set_rate,
};

struct clk *xuantie_th1520_pll(const char *name, const char *parent_name,
			    void __iomem *base,
			    const struct th1520_pll_clk *pll_clk)
{
	struct clk_th1520pll *pll;
	struct clk *clk;
	struct clk_init_data init;
	u32 val;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = pll_clk->flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	switch (pll_clk->out_type) {
	case TH1520_PLL_VCO:
		if (pll_clk->rate_table)
			init.ops = &clk_th1520_pllvco_ops;
		break;
	case TH1520_PLL_DIV:
		if (pll_clk->rate_table)
			init.ops = &clk_th1520_plldiv_ops;
		break;
	default:
		pr_err("%s: Unknown pll out type for pll clk %s\n",
		       __func__, name);
	};

	if (!pll_clk->rate_table)
		init.ops = &clk_th1520_pll_def_ops;

	pll->base = base;
	pll->hw.init = &init;
	pll->out_type = pll_clk->out_type;
	pll->clk_type = pll_clk->clk_type;
	pll->rate_table = pll_clk->rate_table;
	pll->rate_count = pll_clk->rate_count;

	clk_th1520_pll_cfg_init(pll);

	val = readl_relaxed(pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1);
	val &= ~pll->pll_bypass_bit;
	val |= TH1520_DACPD_MASK;
	val |= TH1520_DSMPD_MASK;
	if (pll->pll_mode == PLL_MODE_FRAC) {
		val &= ~TH1520_DSMPD_MASK;
		val &= ~TH1520_DACPD_MASK;
	}
	writel_relaxed(val, pll->base + pll->cfg0_reg_off + TH1520_PLL_CFG1);

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register pll %s %lu\n",
			__func__, name, PTR_ERR(clk));
		kfree(pll);
	}

	return clk;
}

static inline struct clk_th1520div *to_clk_th1520div(struct clk_hw *hw)
{
	struct clk_divider *divider = to_clk_divider(hw);

	return container_of(divider, struct clk_th1520div, divider);
}

static unsigned long clk_th1520div_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_th1520div *th1520_div = to_clk_th1520div(hw);

	return th1520_div->ops->recalc_rate(&th1520_div->divider.hw, parent_rate);
}

static long clk_th1520div_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *prate)
{
	struct clk_th1520div *th1520_div = to_clk_th1520div(hw);

	return th1520_div->ops->round_rate(&th1520_div->divider.hw, rate, prate);
}

static int clk_th1520div_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct clk_th1520div *th1520_div = to_clk_th1520div(hw);
	struct clk_divider *div = to_clk_divider(hw);
	unsigned int divider, value;
	unsigned long flags = 0;
	u32 val;

	/** 
	 * The clk-divider will calculate the node frequency by rounding up 
	 * based on the parent frequency and the target divider.
	 * This calculation is to restore accurate frequency divider. 
	*/
	divider = DIV64_U64_ROUND_CLOSEST(parent_rate, rate);

	/* DIV is zero based divider, but CDE is not */
	if (th1520_div->div_type == MUX_TYPE_DIV)
		value = divider;
	else
		value = divider - 1;

	/* handle the div valid range */
	if (value > th1520_div->max_div)
		value = th1520_div->max_div;
	if (value < th1520_div->min_div)
		value = th1520_div->min_div;

	spin_lock_irqsave(div->lock, flags);

	val = readl(div->reg);
	val &= ~BIT(th1520_div->sync_en);
	writel(val, div->reg);

	udelay(1);

	val &= ~(div_mask(div) << div->shift);
	val |= value << div->shift;
	writel(val, div->reg);

	udelay(1);

	val |= BIT(th1520_div->sync_en);
	writel(val, div->reg);

	spin_unlock_irqrestore(div->lock, flags);

	return 0;
}

static const struct clk_ops clk_th1520div_ops = {
	.recalc_rate = clk_th1520div_recalc_rate,
	.round_rate = clk_th1520div_round_rate,
	.set_rate = clk_th1520div_set_rate,
};

static struct clk *xuantie_clk_th1520_divider_internal(const char *name, const char *parent,
				       void __iomem *reg, u8 shift, u8 width,
				       u8 sync, enum th1520_div_type div_type,
				       u16 min, u16 max, bool closest)
{
	struct clk_th1520div *th1520_div;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	th1520_div = kzalloc(sizeof(*th1520_div), GFP_KERNEL);
	if (!th1520_div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_th1520div_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = parent ? &parent : NULL;
	init.num_parents = parent ? 1 : 0;

	th1520_div->divider.reg = reg;
	th1520_div->divider.shift = shift;
	th1520_div->divider.width = width;
	th1520_div->divider.lock = &xuantie_th1520_clk_lock;
	th1520_div->divider.hw.init = &init;
	th1520_div->ops = &clk_divider_ops;
	th1520_div->sync_en = sync;
	th1520_div->div_type = div_type;
	if (th1520_div->div_type == MUX_TYPE_DIV)
		th1520_div->divider.flags = CLK_DIVIDER_ONE_BASED;

	if (closest)
		th1520_div->divider.flags |= CLK_DIVIDER_ROUND_CLOSEST;

	th1520_div->min_div = min > ((1 << width) - 1) ?
			     ((1 << width) - 1) : min;
	th1520_div->max_div = max > ((1 << width) - 1) ?
			     ((1 << width) - 1) : max;

	hw = &th1520_div->divider.hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(th1520_div);
		return ERR_PTR(ret);
	}

	return hw->clk;
}

struct clk *xuantie_clk_th1520_divider(const char *name, const char *parent,
				       void __iomem *reg, u8 shift, u8 width,
				       u8 sync, enum th1520_div_type div_type,
				       u16 min, u16 max)
{
	return xuantie_clk_th1520_divider_internal(name, parent, reg, shift, width,
											sync, div_type, min, max, false);
}

struct clk *xuantie_clk_th1520_divider_closest(const char *name, const char *parent,
				       void __iomem *reg, u8 shift, u8 width,
				       u8 sync, enum th1520_div_type div_type,
				       u16 min, u16 max)
{
	return xuantie_clk_th1520_divider_internal(name, parent, reg, shift, width,
											sync, div_type, min, max, true);
}

static inline struct clk_th1520gate *to_clk_th1520gate(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);

	return container_of(gate, struct clk_th1520gate, gate);
}

static int clk_th1520_gate_share_is_enabled(struct clk_hw *hw)
{
	struct clk_th1520gate *th1520_gate = to_clk_th1520gate(hw);

	return th1520_gate->ops->is_enabled(hw);
}

static int clk_th1520_gate_share_enable(struct clk_hw *hw)
{
	struct clk_th1520gate *th1520_gate = to_clk_th1520gate(hw);

	if (th1520_gate->share_count && (*th1520_gate->share_count)++ > 0) {
		pr_debug("[%s,%d]share_count = %d\n", __func__, __LINE__, (*th1520_gate->share_count));
		return 0;
	}

	pr_debug("[%s,%d]share_count = %d\n", __func__, __LINE__, (*th1520_gate->share_count));

	return th1520_gate->ops->enable(hw);
}

static void clk_th1520_gate_share_disable(struct clk_hw *hw)
{
	struct clk_th1520gate *th1520_gate = to_clk_th1520gate(hw);

	if (th1520_gate->share_count) {
		if (WARN_ON(*th1520_gate->share_count == 0))
			return;
		else if (--(*th1520_gate->share_count) > 0) {
			pr_debug("[%s,%d]share_count = %d\n", __func__, __LINE__, (*th1520_gate->share_count));
			return;
		}
	}

	pr_debug("[%s,%d]share_count = %d\n", __func__, __LINE__, (*th1520_gate->share_count));

	th1520_gate->ops->disable(hw);
}

static void clk_th1520_gate_share_disable_unused(struct clk_hw *hw)
{
	struct clk_th1520gate *th1520_gate = to_clk_th1520gate(hw);

	if (!th1520_gate->share_count || *th1520_gate->share_count == 0)
		return th1520_gate->ops->disable(hw);
}

static const struct clk_ops clk_th1520gate_share_ops = {
	.enable = clk_th1520_gate_share_enable,
	.disable = clk_th1520_gate_share_disable,
	.disable_unused = clk_th1520_gate_share_disable_unused,
	.is_enabled = clk_th1520_gate_share_is_enabled,
};

struct clk *xuantie_clk_th1520_register_gate_shared(const char *name, const char *parent,
						 unsigned long flags, void __iomem *reg,
						 u8 shift, spinlock_t *lock,
						 unsigned int *share_count)
{
	struct clk_th1520gate *th1520_gate;
	struct clk_hw *hw;
	struct clk_init_data init;
	int ret;

	th1520_gate = kzalloc(sizeof(*th1520_gate), GFP_KERNEL);
	if (!th1520_gate)
		return ERR_PTR(-ENOMEM);

	th1520_gate->gate.reg = reg;
	th1520_gate->gate.bit_idx = shift;
	th1520_gate->gate.flags = 0;
	th1520_gate->gate.lock = lock;
	th1520_gate->gate.hw.init = &init;
	th1520_gate->ops = &clk_gate_ops;
	th1520_gate->share_count = share_count;

	init.name = name;
	init.ops = &clk_th1520gate_share_ops;
	init.flags = flags;
	init.parent_names = parent ? &parent : NULL;
	init.num_parents = parent ? 1 : 0;

	hw = &th1520_gate->gate.hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(th1520_gate);
		return ERR_PTR(ret);
	}

	return hw->clk;
}

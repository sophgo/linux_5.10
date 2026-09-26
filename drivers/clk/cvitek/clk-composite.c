// SPDX-License-Identifier: GPL-2.0+
//
// Sophgo/CVITEK composite clock helpers (syscon/regmap based)
//

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "clk-common.h"
#include "clk-composite.h"

#define to_cv_clk(_hw) container_of(_hw, struct cv_composite_clock, hw)

#define div_mask(width) ((1U << (width)) - 1)

static inline unsigned long cv_diff_ulong(unsigned long a, unsigned long b)
{
	return (a > b) ? (a - b) : (b - a);
}

static int cv_clk_is_bypassed(struct cv_composite_clock *clk_hw)
{
	unsigned int val;
	int ret;

	if (clk_hw->mux[0].reg == REG_UNEXIST)
		return 0;

	ret = regmap_read(clk_hw->syscon, clk_hw->mux[0].reg, &val);
	if (ret)
		return 0;
	val >>= clk_hw->mux[0].shift;
	val &= 0x1;

	return val;
}

static int cv_clk_get_clk_sel(struct cv_composite_clock *clk_hw)
{
	unsigned int val;
	int ret;

	if (clk_hw->mux[1].reg == REG_UNEXIST)
		return 0;

	ret = regmap_read(clk_hw->syscon, clk_hw->mux[1].reg, &val);
	if (ret)
		return 0;
	val >>= clk_hw->mux[1].shift;
	val &= 0x1;
	val ^= 0x1; /* invert value */

	return val;
}

static int cv_clk_get_src_sel(struct cv_composite_clock *clk_hw, u8 clk_sel)
{
	unsigned int val;
	u32 reg;
	int ret;

	if (clk_hw->mux[2].reg == REG_UNEXIST)
		return 0;

	reg = clk_hw->mux[2].reg + clk_sel * 0x4;
	ret = regmap_read(clk_hw->syscon, reg, &val);
	if (ret)
		return 0;
	val >>= clk_hw->mux[2].shift;
	val &= 0x3;

	return val;
}

unsigned long cv_clk_div_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned int clk_sel = cv_clk_get_clk_sel(clk_hw);
	unsigned int val;
	u32 reg;

	if (clk_hw->div[clk_sel].reg == REG_UNEXIST)
		return parent_rate;

	if ((clk_hw->mux[0].reg != REG_UNEXIST) && cv_clk_is_bypassed(clk_hw))
		return parent_rate;

	reg = clk_hw->div[clk_sel].reg;
	regmap_read(clk_hw->syscon, reg, &val);

	if ((clk_hw->div[clk_sel].initval > 0) && !(val & BIT(3))) {
		val = clk_hw->div[clk_sel].initval;
	} else {
		val >>= clk_hw->div[clk_sel].shift;
		val &= div_mask(clk_hw->div[clk_sel].width);
	}

	return divider_recalc_rate(hw, parent_rate, val, NULL,
				  clk_hw->div[clk_sel].flags,
				  clk_hw->div[clk_sel].width);
}

long cv_clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long *prate)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned int clk_sel = cv_clk_get_clk_sel(clk_hw);

	if ((clk_hw->mux[0].reg != REG_UNEXIST) && cv_clk_is_bypassed(clk_hw))
		return *prate;

	if (clk_hw->div[0].reg == REG_UNEXIST)
		return *prate;

	return divider_round_rate(hw, rate, prate, NULL,
				  clk_hw->div[clk_sel].width,
				  clk_hw->div[clk_sel].flags);
}

static long cv_clk_div_calc_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *prate)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);

	if (clk_hw->div[0].reg != REG_UNEXIST)
		return divider_round_rate(hw, rate, prate, NULL,
					  clk_hw->div[0].width,
					  clk_hw->div[0].flags);

	return DIV_ROUND_UP_ULL((u64)*prate, 1);
}

int cv_clk_div_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct clk_hw *current_parent;
	unsigned long parent_rate;
	unsigned long best_delta;
	unsigned long best_rate;
	u32 parent_count;
	long rate;
	u32 which;

	parent_count = clk_hw_get_num_parents(hw);

	if ((parent_count < 2) ||
	    (clk_hw_get_flags(hw) & CLK_SET_RATE_NO_REPARENT)) {
		rate = cv_clk_div_round_rate(hw, req->rate,
					     &req->best_parent_rate);
		if (rate < 0)
			return rate;

		req->rate = rate;
		return 0;
	}

	current_parent = clk_hw_get_parent(hw);
	parent_rate = clk_hw_get_rate(current_parent);
	best_rate = cv_clk_div_calc_round_rate(hw, req->rate, &parent_rate);
	best_delta = cv_diff_ulong(best_rate, req->rate);

	for (which = 0; which < parent_count; which++) {
		struct clk_hw *parent = clk_hw_get_parent_by_index(hw, which);
		unsigned long delta;
		unsigned long other_rate;

		if (!parent || parent == current_parent)
			continue;

		parent_rate = clk_hw_get_rate(parent);
		other_rate = cv_clk_div_calc_round_rate(hw, req->rate,
							&parent_rate);
		delta = cv_diff_ulong(other_rate, req->rate);
		if (delta < best_delta) {
			best_delta = delta;
			best_rate = other_rate;
			req->best_parent_hw = parent;
			req->best_parent_rate = parent_rate;
		}

		if (best_delta == 0)
			break;
	}

	req->rate = best_rate;
	return 0;
}

int cv_clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned int clk_sel = cv_clk_get_clk_sel(clk_hw);
	unsigned long flags = 0;
	int value;
	unsigned int val;
	u32 reg;
	u32 mask;

	if (clk_hw->div[0].reg == REG_UNEXIST) {
		if (rate != parent_rate)
			pr_warn("w/o div: %lu to %lu\n", parent_rate, rate);
		return 0;
	}

	value = divider_get_val(rate, parent_rate, NULL,
				clk_hw->div[clk_sel].width,
				clk_hw->div[clk_sel].flags);
	if (value < 0)
		return value;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);

	/*
	 * - temporarily toggle clk_sel (or set bypass) while updating div.
	 */
	if (clk_hw->mux[1].reg != REG_UNEXIST) {
		reg = clk_hw->mux[1].reg;
		regmap_read(clk_hw->syscon, reg, &val);
		val ^= BIT(clk_hw->mux[1].shift);
		regmap_write(clk_hw->syscon, reg, val);
	} else if (clk_hw->mux[0].reg != REG_UNEXIST) {
		reg = clk_hw->mux[0].reg;
		regmap_read(clk_hw->syscon, reg, &val);
		val |= BIT(clk_hw->mux[0].shift);
		regmap_write(clk_hw->syscon, reg, val);
	}

	reg = clk_hw->div[clk_sel].reg;
	mask = div_mask(clk_hw->div[clk_sel].width) << clk_hw->div[clk_sel].shift;
	regmap_read(clk_hw->syscon, reg, &val);
	val &= ~mask;
	val |= (u32)value << clk_hw->div[clk_sel].shift;
	if (!(clk_hw->div[clk_sel].initval < 0))
		val |= BIT(3);
	regmap_write(clk_hw->syscon, reg, val);

	if (clk_hw->mux[1].reg != REG_UNEXIST) {
		reg = clk_hw->mux[1].reg;
		regmap_read(clk_hw->syscon, reg, &val);
		val ^= BIT(clk_hw->mux[1].shift);
		regmap_write(clk_hw->syscon, reg, val);
	} else if (clk_hw->mux[0].reg != REG_UNEXIST) {
		reg = clk_hw->mux[0].reg;
		regmap_read(clk_hw->syscon, reg, &val);
		val &= ~BIT(clk_hw->mux[0].shift);
		regmap_write(clk_hw->syscon, reg, val);
	}

	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);

	return 0;
}

static void cv_clk_gate_endisable(struct clk_hw *hw, int enable)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned long flags;
	u32 reg = clk_hw->gate.reg;
	u32 mask = BIT(clk_hw->gate.shift);
	u32 val = enable ? mask : 0;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);

	regmap_update_bits(clk_hw->syscon, reg, mask, val);

	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);
}

int cv_clk_gate_enable(struct clk_hw *hw)
{
	cv_clk_gate_endisable(hw, 1);
	return 0;
}

void cv_clk_gate_disable(struct clk_hw *hw)
{
	cv_clk_gate_endisable(hw, 0);
}

int cv_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned int val;
	u32 mask = BIT(clk_hw->gate.shift);

	regmap_read(clk_hw->syscon, clk_hw->gate.reg, &val);
	val &= mask;

	if (clk_hw_get_flags(hw) & CLK_IGNORE_UNUSED)
		return __clk_get_enable_count(hw->clk) ? (val ? 1 : 0) : 0;

	return val ? 1 : 0;
}

u8 cv_clk_mux_get_parent(struct clk_hw *hw)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	u8 clk_sel = cv_clk_get_clk_sel(clk_hw);
	u8 src_sel = cv_clk_get_src_sel(clk_hw, clk_sel);
	u8 parent_idx = 0;

	/*
	 * | 0     |   1   |   2   |
	 * +-------+-------+-------+
	 * | XTAL  | DIV0  | DIV1  |
	 * | DIV0  | DIV1  |       |
	 */
	if (clk_hw->mux[0].reg != REG_UNEXIST) {
		if (cv_clk_is_bypassed(clk_hw))
			parent_idx = 0;
		else if (clk_hw->mux[1].reg != REG_UNEXIST)
			parent_idx = clk_sel ? (src_sel + 2) : (src_sel + 1);
		else
			parent_idx = src_sel + 1;
	} else if (clk_hw->mux[1].reg != REG_UNEXIST) {
		parent_idx = clk_sel;
	} else {
		parent_idx = src_sel;
	}

	return parent_idx;
}

int cv_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned long flags;
	unsigned int reg;
	unsigned int offset = 0;
	unsigned int val;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);

	if (clk_hw->mux[0].reg != REG_UNEXIST) {
		if (index == 0) {
			regmap_update_bits(clk_hw->syscon,
					   clk_hw->mux[0].reg,
					   BIT(clk_hw->mux[0].shift),
					   BIT(clk_hw->mux[0].shift));
			goto unlock_release;
		}
		index--;
	}

	if (index < 1) {
		offset = 0;
	} else {
		index -= 1;
		offset = 4;
	}

	/* set bypass */
	if (clk_hw->mux[0].reg != REG_UNEXIST)
		regmap_update_bits(clk_hw->syscon, clk_hw->mux[0].reg,
				   BIT(clk_hw->mux[0].shift),
				   BIT(clk_hw->mux[0].shift));

	/* set div_src_sel */
	if (clk_hw->mux[2].reg != REG_UNEXIST) {
		reg = clk_hw->mux[2].reg + offset;
		regmap_read(clk_hw->syscon, reg, &val);
		val &= ~(0x3 << clk_hw->mux[2].shift);
		val |= (index & 0x3) << clk_hw->mux[2].shift;
		regmap_write(clk_hw->syscon, reg, val);
	}

	/* set clk_sel */
	if (clk_hw->mux[1].reg != REG_UNEXIST) {
		regmap_read(clk_hw->syscon, clk_hw->mux[1].reg, &val);
		if (offset)
			val &= ~BIT(clk_hw->mux[1].shift);
		else
			val |= BIT(clk_hw->mux[1].shift);
		regmap_write(clk_hw->syscon, clk_hw->mux[1].reg, val);
	}

	/* clear bypass */
	if (clk_hw->mux[0].reg != REG_UNEXIST)
		regmap_update_bits(clk_hw->syscon, clk_hw->mux[0].reg,
				   BIT(clk_hw->mux[0].shift), 0);

unlock_release:
	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);

	return 0;
}

int cv_clk_set_rate_and_parent(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate, u8 index)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned long flags;
	unsigned int reg, offset;
	unsigned int val = 0;
	unsigned int tmp;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);

	/* set bypass */
	if (clk_hw->mux[0].reg != REG_UNEXIST) {
		regmap_update_bits(clk_hw->syscon, clk_hw->mux[0].reg,
					BIT(clk_hw->mux[0].shift),
					BIT(clk_hw->mux[0].shift));
		if (index == 0)
			goto unlock_release;

		index--;
	}

	if (index < 1) {
		offset = 0;
	} else {
		index -= 1;
		offset = 4;
	}

	if (clk_hw->div[0].reg != REG_UNEXIST)
		val = divider_get_val(rate, parent_rate, NULL,
				      clk_hw->div[0].width,
				      clk_hw->div[0].flags);

	/* set div + src_sel */
	if (clk_hw->mux[2].reg != REG_UNEXIST) {
		reg = clk_hw->mux[2].reg + offset;
		regmap_read(clk_hw->syscon, reg, &tmp);
		tmp &= ~(div_mask(clk_hw->div[0].width) << clk_hw->div[0].shift);
		tmp |= (u32)val << clk_hw->div[0].shift;
		tmp &= ~(0x3 << clk_hw->mux[2].shift);
		tmp |= (index & 0x3) << clk_hw->mux[2].shift;
		tmp |= BIT(3);
		regmap_write(clk_hw->syscon, reg, tmp);
	}

	/* set clk_sel */
	if (clk_hw->mux[1].reg != REG_UNEXIST) {
		regmap_read(clk_hw->syscon, clk_hw->mux[1].reg, &tmp);
		if (offset)
			tmp &= ~BIT(clk_hw->mux[1].shift);
		else
			tmp |= BIT(clk_hw->mux[1].shift);
		regmap_write(clk_hw->syscon, clk_hw->mux[1].reg, tmp);
	}

	/* clear bypass */
	if (clk_hw->mux[0].reg != REG_UNEXIST)
		regmap_update_bits(clk_hw->syscon, clk_hw->mux[0].reg,
				   BIT(clk_hw->mux[0].shift), 0);

unlock_release:
	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);

	return 0;
}

struct clk_hw *cv_register_composite_clk(struct cv_composite_clock *cclk,
					 struct regmap *syscon,
					 spinlock_t *lock)
{
	struct clk_hw *hw;
	int err;

	cclk->gate.flags = 0;
	cclk->div[0].flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
			     CLK_DIVIDER_ROUND_CLOSEST;
	cclk->div[1].flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
			     CLK_DIVIDER_ROUND_CLOSEST;
	cclk->mux[0].flags = 0;
	cclk->mux[1].flags = 0;
	cclk->mux[2].flags = 0;
	cclk->syscon = syscon;
	cclk->lock = lock;


	hw = &cclk->hw;
	err = clk_hw_register(NULL, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}

int cv_register_composite_clks(struct cv_composite_clock *clks, int num_clks,
	struct cv_clock_data *data)
{
	struct clk_hw *hw;
	unsigned int i;

	for (i = 0; i < num_clks; i++) {
		struct cv_composite_clock *cclk = &clks[i];

		hw = cv_register_composite_clk(cclk, data->syscon, data->lock);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, cclk->name);
			goto err_clk;
		}

		data->hw_data.hws[cclk->id] = hw;
		clk_hw_register_clkdev(hw, cclk->name, NULL);
	}

	return 0;

err_clk:
	while (i--)
		clk_hw_unregister(data->hw_data.hws[clks[i].id]);

	return PTR_ERR(hw);
}

const struct clk_ops cv_clk_ops = {
	.enable = cv_clk_gate_enable,
	.disable = cv_clk_gate_disable,
	.is_enabled = cv_clk_gate_is_enabled,
	.recalc_rate = cv_clk_div_recalc_rate,
	.round_rate = cv_clk_div_round_rate,
	.determine_rate = cv_clk_div_determine_rate,
	.set_rate = cv_clk_div_set_rate,
	.get_parent = cv_clk_mux_get_parent,
	.set_parent = cv_clk_mux_set_parent,
	.set_rate_and_parent = cv_clk_set_rate_and_parent,
};

unsigned long cv_pll_post_clk_recalc_rate(struct clk_hw *hw,
				     unsigned long parent_rate)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned int div, postdiv1, postdiv2;

	if ((clk_hw->mux[0].reg != REG_UNEXIST) && cv_clk_is_bypassed(clk_hw))
		return parent_rate;

	regmap_read(clk_hw->syscon, clk_hw->div[0].reg, &postdiv1);
	regmap_read(clk_hw->syscon, clk_hw->div[1].reg, &postdiv2);

	postdiv1 >>= clk_hw->div[0].shift;
	postdiv1 &= div_mask(clk_hw->div[0].width);

	postdiv2 >>= clk_hw->div[1].shift;
	postdiv2 &= div_mask(clk_hw->div[1].width);

	div = (postdiv1 + 1) * (postdiv2 + 1);

	return divider_recalc_rate(hw, parent_rate, div, NULL,
				  clk_hw->div[0].flags,
				  clk_hw->div[0].width);
}

static long cv_pll_post_clk_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	unsigned long best_rate = 0;
	unsigned long best_diff = ULONG_MAX;
	unsigned long parent_rate = *prate;
	unsigned int postdiv1, postdiv2;
	unsigned long calc_rate;
	unsigned long diff;

	for (postdiv1 = 1; postdiv1 <= 8; postdiv1++) {
		for (postdiv2 = 1; postdiv2 <= 8; postdiv2++) {
			calc_rate = parent_rate  / postdiv1 / postdiv2;
			diff = cv_diff_ulong(calc_rate, rate);
			if (diff < best_diff) {
				best_diff = diff;
				best_rate = calc_rate;
			}
			if (best_diff == 0)
				break;
		}
		if (best_diff == 0)
			break;
	}

	return best_rate;
}

int cv_pll_post_clk_set_rate(struct clk_hw *hw, unsigned long rate,
			     unsigned long parent_rate)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned long flags;
	unsigned long best_diff = ULONG_MAX;
	int postdiv1, postdiv2;
	int bestdiv1 = 1, bestdiv2 = 1;
	unsigned long calc_rate, diff;
	unsigned int val;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);

	if (rate == parent_rate) {
		regmap_update_bits(clk_hw->syscon,
				   clk_hw->mux[0].reg,
				   BIT(clk_hw->mux[0].shift),
				   BIT(clk_hw->mux[0].shift));
		goto unlock_release;
	}

	for (postdiv1 = 1; postdiv1 <= 8; postdiv1++) {
		for (postdiv2 = 1; postdiv2 <= 8; postdiv2++) {
			calc_rate = parent_rate / postdiv2 / postdiv1;
			diff = cv_diff_ulong(calc_rate, rate);

			if (diff < best_diff) {
				best_diff = diff;
				bestdiv1 = postdiv1;
				bestdiv2 = postdiv2;
			}

			if (best_diff == 0)
				break;
		}
		if (best_diff == 0)
			break;
	}


	regmap_read(clk_hw->syscon, clk_hw->div[0].reg, &val);
	val &= ~((div_mask(clk_hw->div[0].width) << clk_hw->div[0].shift));
	val |= (bestdiv1 - 1) << clk_hw->div[0].shift;
	regmap_write(clk_hw->syscon, clk_hw->div[0].reg, val);

	regmap_read(clk_hw->syscon, clk_hw->div[1].reg, &val);
	val &= ~((div_mask(clk_hw->div[1].width) << clk_hw->div[1].shift));
	val |= (bestdiv2 - 1) << clk_hw->div[1].shift;
	regmap_write(clk_hw->syscon, clk_hw->div[1].reg, val);

unlock_release:
	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);

	return 0;
}

u8 cv_pll_post_clk_get_parent(struct clk_hw *hw)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	u8 parent_idx = 0;

	/*
	 * | 0     |   1   |   2   |
	 * +-------+-------+-------+
	 * | XTAL  | DIV0  |       |
	 */
	if (clk_hw->mux[0].reg != REG_UNEXIST) {
		if (cv_clk_is_bypassed(clk_hw))
			parent_idx = 0;
		else
			parent_idx = 1;
	}

	return parent_idx;
}

int cv_pll_post_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv_composite_clock *clk_hw = to_cv_clk(hw);
	unsigned long flags;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);

	if (index == 0)
		regmap_update_bits(clk_hw->syscon, clk_hw->mux[0].reg,
				   BIT(clk_hw->mux[0].shift),
				   BIT(clk_hw->mux[0].shift));
	else
		regmap_update_bits(clk_hw->syscon, clk_hw->mux[0].reg,
				   BIT(clk_hw->mux[0].shift), 0);

	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);

	return 0;
}

const struct clk_ops cv_pll_post_clk_ops  = {
	.enable = cv_clk_gate_enable,
	.disable = cv_clk_gate_disable,
	.is_enabled = cv_clk_gate_is_enabled,

	.recalc_rate = cv_pll_post_clk_recalc_rate,
	.round_rate = cv_pll_post_clk_round_rate,

	.determine_rate = cv_clk_div_determine_rate,

	.set_rate = cv_pll_post_clk_set_rate,
	.get_parent = cv_pll_post_clk_get_parent,
	.set_parent = cv_pll_post_clk_set_parent,
};

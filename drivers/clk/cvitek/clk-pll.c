// SPDX-License-Identifier: GPL-2.0+
//
// Sophgo/CVITEK pll helpers (syscon/regmap based)
//

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/gcd.h>

#include "clk-common.h"
#include "clk-pll.h"

static inline unsigned long cv_diff_ulong(unsigned long a, unsigned long b)
{
	return (a > b) ? (a - b) : (b - a);
}

#define to_cv_pll(_hw) container_of(_hw, struct cv_pll, hw)

static struct clk_hw *cv_clk_register_pll(struct cv_pll *pll,
					  struct regmap *syscon)
{
	struct clk_hw *hw;
	int err;

	pll->syscon = syscon;

	hw = &pll->hw;

	err = clk_hw_register(NULL, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}

static void cv_clk_unregister_pll(struct clk_hw *hw)
{
	clk_hw_unregister(hw);
}

int cv_clk_register_plls(struct cv_pll *plls, int num_plls,
			 struct cv_clock_data *data)
{
	struct clk_hw *hw;
	int i;
	struct regmap *syscon = data->syscon;

	for (i = 0; i < num_plls; i++) {
		struct cv_pll *pll = &plls[i];

		pll->lock = data->lock;

		hw = cv_clk_register_pll(pll, syscon);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, pll->pll.name);
			goto err_clk;
		}

		data->hw_data.hws[plls[i].pll.id] = hw;

		clk_hw_register_clkdev(hw, pll->pll.name, NULL);
	}

	return 0;

err_clk:
	while (i--)
		cv_clk_unregister_pll(data->hw_data.hws[plls[i].pll.id]);

	return PTR_ERR(hw);
}

static unsigned long cv_intn_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned int val, fbdiv, refdiv, postdiv1, postdiv2;
	unsigned int ssc_set = 0;
	unsigned long rate;
	u64 prate = parent_rate;
	int ret;

	if (pll->pll.reg_ssc != REG_UNEXIST) {
		ret = regmap_read(pll->syscon, pll->pll.reg_ssc, &val);
		if (ret) {
			pr_err("%s: failed to read pll ssc register\n", __func__);
			return 0;
		}
		if (!(val & 0x10)) { //ssc enabled
			regmap_read(pll->syscon, pll->pll.reg_ssc + 4, &ssc_set);
			if (ssc_set) {
				prate <<= 26;
				do_div(prate, ssc_set);
			}
		}
	}

	ret = regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	if (ret) {
		pr_err("%s: failed to read pll register\n", __func__);
		return 0;
	}

	if (val & 0x1)
		return prate; // bypass mode

	fbdiv = (val >> 4) & 0xFFF; //range: 16-320
	refdiv = (val >> 16) & 0x3F;
	postdiv1 = (val >> 24) & 0x7;
	postdiv2 = (val >> 28) & 0x7;

	if (refdiv == 0)
		refdiv = 1;

	rate = (prate / refdiv) * fbdiv / postdiv1 / postdiv2;

	return rate;
}

static const struct {
	unsigned long rate;
	u32 csr;
	u32 ssc_set;
} ssc_pll_rate_lut[] = {
	{.rate =  417792000, .csr = 0x14c1010e, .ssc_set = 0x4c97d7d8},
	{.rate =  786432000, .csr = 0x12c1010e, .ssc_set = 0x51615555},
};

static long cv_intn_pll_round_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long *prate)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long best_rate = 0;
	unsigned long best_diff = ULONG_MAX;
	unsigned long parent_rate = *prate;
	unsigned int i, refdiv, fbdiv, postdiv1, postdiv2;
	unsigned long calc_rate;
	unsigned long diff;

	if (pll->pll.reg_ssc != REG_UNEXIST) {
		// check if the requested rate matches any entry in the LUT
		for (i = 0; i < ARRAY_SIZE(ssc_pll_rate_lut); i++) {
			calc_rate = ssc_pll_rate_lut[i].rate;
			diff = cv_diff_ulong(calc_rate, rate);
			if (diff < best_diff) {
				best_diff = diff;
				best_rate = calc_rate;
			}
		}
	}

	for (refdiv = 1; refdiv <= 63; refdiv++) {
		for (fbdiv = 16; fbdiv <= 320; fbdiv++) {
			for (postdiv1 = 1; postdiv1 <= 7; postdiv1++) {
				for (postdiv2 = 1; postdiv2 <= 7; postdiv2++) {
					calc_rate = (parent_rate / refdiv) * fbdiv / postdiv1 / postdiv2;
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
			if (best_diff == 0)
				break;
		}
		if (best_diff == 0)
			break;
	}

	return best_rate;
}

u8 cv_intn_pll_get_parent(struct clk_hw *hw)
{
	struct cv_pll *pll = to_cv_pll(hw);
	u8 parent_idx = 0;

	if (pll->pll.reg_ssc != REG_UNEXIST) {
		unsigned int val;
		int ret;

		ret = regmap_read(pll->syscon, pll->pll.reg_ssc, &val);
		if (ret) {
			pr_err("%s: failed to read pll ssc register\n", __func__);
			return 0;
		}
		if (!(val & BIT(4))) { /* syn enabled */
			parent_idx = 1 + ((val >> 16) & 0x3);
		}
	}
	return parent_idx;
}

int cv_intn_pll_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long flags;
	unsigned int val;

	if (pll->pll.reg_ssc == REG_UNEXIST)
		return 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	regmap_read(pll->syscon, pll->pll.reg_ssc, &val);

	if (index == 0) {
		/* set bypass mode: disable syn */
		val |= BIT(4);
	} else {
		/* enable syn */
		val &= ~BIT(4);
		val &= ~(0x7 << 16);
		val |= ((index - 1) & 0x3) << 16; /* set syn source */
	}
	regmap_write(pll->syscon, pll->pll.reg_ssc, val);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}

static int cv_intn_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long flags;
	unsigned long best_rate = 0;
	unsigned long best_diff = ULONG_MAX;
	unsigned int best_refdiv = 1, best_fbdiv = 1;
	unsigned int best_postdiv1 = 1, best_postdiv2 = 1;
	unsigned int refdiv, fbdiv, postdiv1, postdiv2;
	unsigned long calc_rate, diff;
	unsigned int val;
	int i;
	int ret = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	if (rate == parent_rate) {
		/* Set bypass mode when requested rate equals parent rate */
		regmap_update_bits(pll->syscon, pll->pll.reg_csr, BIT(0), BIT(0));
		goto unlock_release;
	}

	if (pll->pll.reg_ssc != REG_UNEXIST) {
		/* Check if the requested rate matches any entry in the LUT */
		for (i = 0; i < ARRAY_SIZE(ssc_pll_rate_lut); i++) {
			if (ssc_pll_rate_lut[i].rate == rate) {
				regmap_write(pll->syscon, pll->pll.reg_csr, ssc_pll_rate_lut[i].csr);
				regmap_write(pll->syscon, pll->pll.reg_ssc + 4, ssc_pll_rate_lut[i].ssc_set);
				regmap_update_bits(pll->syscon, pll->pll.reg_ssc, 0x1011, 0x1001);
				goto unlock_release;
			}
		}
	}

	for (refdiv = 1; refdiv <= 63; refdiv++) {
		for (fbdiv = 16; fbdiv <= 320; fbdiv++) {
			for (postdiv1 = 1; postdiv1 <= 7; postdiv1++) {
				for (postdiv2 = 1; postdiv2 <= 7; postdiv2++) {
					calc_rate = (parent_rate / refdiv) * fbdiv / postdiv1 / postdiv2;
					diff = cv_diff_ulong(calc_rate, rate);

					if (diff < best_diff) {
						best_diff = diff;
						best_rate = calc_rate;
						best_refdiv = refdiv;
						best_fbdiv = fbdiv;
						best_postdiv1 = postdiv1;
						best_postdiv2 = postdiv2;
					}

					if (best_diff == 0)
						break;
				}
				if (best_diff == 0)
					break;
			}
			if (best_diff == 0)
				break;
		}
		if (best_diff == 0)
			break;
	}

	regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	val &= ~((0xFFF << 4) | (0x3F << 16) | (0x7 << 24) | (0x7 << 28));
	val |= (best_fbdiv << 4) | (best_refdiv << 16) |
	       (best_postdiv1 << 24) | (best_postdiv2 << 28);
	regmap_write(pll->syscon, pll->pll.reg_csr, val);

unlock_release:
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

const struct clk_ops cv_intn_pll_ops = {
	.recalc_rate = cv_intn_pll_recalc_rate,
	.round_rate = cv_intn_pll_round_rate,
	.set_rate = cv_intn_pll_set_rate,

	.get_parent = cv_intn_pll_get_parent,
	.set_parent = cv_intn_pll_set_parent,
};

static long cv_frac_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *prate)
{
	unsigned long best_rate = 0;
	unsigned long best_diff = ULONG_MAX;
	unsigned long parent_rate = *prate;
	unsigned int refdiv, fbdiv;
	unsigned int fbdiv_h, fbdiv_l;
	unsigned long calc_rate;
	unsigned long diff;

	for (refdiv = 1; refdiv < 64; refdiv++) {
		/* round up */
		fbdiv_l = (32 * refdiv + parent_rate - 1) / parent_rate;
		/* round down */
		fbdiv_h = (4095 * refdiv) / parent_rate;

		if (fbdiv_l < 32)
			fbdiv_l = 32;

		if (fbdiv_h > 4095)
			fbdiv_h = 4095;

		for (fbdiv = fbdiv_l; fbdiv <= fbdiv_h; fbdiv++) {
			calc_rate = (parent_rate / refdiv) * fbdiv;
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

static int cv_frac_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long flags;
	unsigned long best_diff = ULONG_MAX;
	unsigned int best_refdiv = 1, best_fbdiv = 1;
	unsigned int refdiv, fbdiv;
	unsigned int fbdiv_h, fbdiv_l;
	unsigned long calc_rate, diff;
	unsigned int val;
	int ret;

	ret = regmap_read(pll->syscon, pll->pll.reg_csr + 0x10, &val);
	if (ret) {
		pr_err("%s: failed to read pll register\n", __func__);
		return ret;
	}

	if (val & BIT(20))
		rate *= 4; /* vcodivsel: fvco divided by 4 */

	for (refdiv = 1; refdiv < 64; refdiv++) {
		/* round up */
		fbdiv_l = (32 * refdiv + parent_rate - 1) / parent_rate;
		/* round down */
		fbdiv_h = (4095 * refdiv) / parent_rate;

		if (fbdiv_l < 32)
			fbdiv_l = 32;

		if (fbdiv_h > 4095)
			fbdiv_h = 4095;

		for (fbdiv = fbdiv_l; fbdiv <= fbdiv_h; fbdiv++) {
			calc_rate = (parent_rate / refdiv) * fbdiv;
			diff = cv_diff_ulong(calc_rate, rate);

			if (diff < best_diff) {
				best_diff = diff;
				best_refdiv = refdiv;
				best_fbdiv = fbdiv;
			}

			if (best_diff == 0)
				break;
		}
		if (best_diff == 0)
			break;
	}

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	ret = regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	if (ret) {
		pr_err("%s: failed to read pll register\n", __func__);
		goto out_unlock;
	}

	val &= ~((0xFFF << 4) | (0x3F << 16));
	val |= (best_fbdiv << 4) | (best_refdiv << 16);

	ret = regmap_write(pll->syscon, pll->pll.reg_csr, val);
	if (ret)
		pr_err("%s: failed to write pll register\n", __func__);

out_unlock:
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static unsigned long cv_frac_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned int val, fbdiv, refdiv;
	unsigned long rate;
	int ret;

	ret = regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	if (ret) {
		pr_err("%s: failed to read pll register\n", __func__);
		return 0;
	}

	fbdiv = (val >> 0) & 0xFFF; /* range: 32-4095 */
	refdiv = (val >> 18) & 0x3F;

	if (refdiv == 0)
		refdiv = 1;

	ret = regmap_read(pll->syscon, pll->pll.reg_csr + 0x10, &val);
	if (ret) {
		pr_err("%s: failed to read pll register\n", __func__);
		return 0;
	}

	if (val & BIT(20))
		refdiv *= 4; /* vcodivsel: fvco divided by 4 */

	rate = (parent_rate / refdiv) * fbdiv;

	return rate;
}

const struct clk_ops cv_frac_pll_ops = {
	.recalc_rate = cv_frac_pll_recalc_rate,
	.round_rate = cv_frac_pll_round_rate,
	.set_rate = cv_frac_pll_set_rate,
};

static unsigned long cv_div_pll_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned int val, m, n;
	unsigned long rate;

	regmap_read(pll->syscon, pll->pll.reg_csr + 4, &m);
	regmap_read(pll->syscon, pll->pll.reg_csr + 8, &n);

	m &= 0x3FFFFF;
	n &= 0x3FFFFF;

	if (!m)
		m = 24;
	if (!n)
		n = 1;

	rate = (parent_rate * n) / m / 2;

	return rate;
}

static long cv_div_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	unsigned long parent_rate = *prate;
	unsigned long numerator = 2 * rate;
	unsigned long common_divisor = gcd(numerator, parent_rate);
	unsigned int m = parent_rate / common_divisor;
	unsigned int n = numerator / common_divisor;

	return (parent_rate * n / m / 2);
}

static int cv_div_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long flags;
	unsigned long numerator = 2 * rate;
	unsigned long common_divisor = gcd(numerator, parent_rate);
	unsigned int best_m = parent_rate / common_divisor;
	unsigned int best_n = numerator / common_divisor;
	unsigned int val;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	val |= BIT(1); /* reg_a24k_frac_div_en */
	regmap_write(pll->syscon, pll->pll.reg_csr, val);

	regmap_write(pll->syscon, pll->pll.reg_csr + 4, best_m);
	regmap_write(pll->syscon, pll->pll.reg_csr + 8, best_n);

	val |= (BIT(2) | BIT(1)); /* reg_a24k_frac_div_en/up */
	regmap_write(pll->syscon, pll->pll.reg_csr, val);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}

static int cv_div_pll_enable(struct clk_hw *hw)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long flags;
	unsigned int val;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	val |= BIT(0) | BIT(3); /* enable */
	regmap_write(pll->syscon, pll->pll.reg_csr, val);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}
static void cv_div_pll_disable(struct clk_hw *hw)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long flags;
	unsigned int val;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	val &= ~(BIT(0) | BIT(3)); /* disable */
	regmap_write(pll->syscon, pll->pll.reg_csr, val);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static int cv_div_pll_is_enabled(struct clk_hw *hw)
{
	struct cv_pll *pll = to_cv_pll(hw);
	unsigned long flags;
	unsigned int val;
	int ret;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	ret = regmap_read(pll->syscon, pll->pll.reg_csr, &val);
	if (ret) {
		pr_err("%s: failed to read pll register\n", __func__);
		goto out_unlock;
	}

	ret = (val & (BIT(0) | BIT(3))) ? 1 : 0;

	if (clk_hw_get_flags(hw) & CLK_IGNORE_UNUSED)
		ret = __clk_get_enable_count(hw->clk) ? ret : 0;

out_unlock:
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

const struct clk_ops cv_div_pll_ops = {
	.enable = cv_div_pll_enable,
	.disable = cv_div_pll_disable,
	.is_enabled = cv_div_pll_is_enabled,

	.recalc_rate = cv_div_pll_recalc_rate,
	.round_rate = cv_div_pll_round_rate,
	.set_rate = cv_div_pll_set_rate,
};

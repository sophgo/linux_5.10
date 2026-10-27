/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sophgo/CVITEK syscon-based pll clocks.
 */

#ifndef __CV_PLL_CLOCK_H__
#define __CV_PLL_CLOCK_H__

#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct cv_pll_hw {
	unsigned int	id;
	const char	*name;
	u32		reg_csr;
	u32		reg_ssc;
	unsigned long	flags;
};

struct cv_pll {
	struct cv_pll_hw pll;
	struct regmap *syscon;
	spinlock_t *lock;
	struct clk_hw hw;
};

#define CV_INTN_PLL(_id, _name, _parent, _reg_csr, _reg_ssc, _flags) {		\
		.pll.id = _id,						\
		.pll.name = _name,					\
		.pll.reg_csr = _reg_csr,				\
		.pll.reg_ssc = _reg_ssc,				\
		.hw.init = CLK_HW_INIT_PARENTS(_name, _parent,		\
					       &cv_intn_pll_ops,	\
					       _flags | CV_CLK_FLAGS_ALL),	\
	}

#define CV_FRAC_PLL(_id, _name, _parent, _reg_csr, _flags) {		\
		.pll.id = _id,						\
		.pll.name = _name,					\
		.pll.reg_csr = _reg_csr,				\
		.hw.init = CLK_HW_INIT_PARENTS(_name, _parent,		\
					       &cv_frac_pll_ops,	\
					       _flags | CV_CLK_FLAGS_ALL),	\
	}

#define CV_DIV_PLL(_id, _name, _parent, _reg_csr, _flags) {		\
		.pll.id = _id,						\
		.pll.name = _name,					\
		.pll.reg_csr = _reg_csr,				\
		.hw.init = CLK_HW_INIT_PARENTS(_name, _parent,		\
					       &cv_div_pll_ops,	\
					       _flags | CV_CLK_FLAGS_ALL),	\
	}

extern const struct clk_ops cv_intn_pll_ops;
extern const struct clk_ops cv_frac_pll_ops;
extern const struct clk_ops cv_div_pll_ops;

int cv_clk_register_plls(struct cv_pll *clks, int num_plls,
			 struct cv_clock_data *data);

#endif /* __CV_PLL_CLOCK_H__ */

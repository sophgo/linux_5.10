/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Sophgo/CVITEK syscon-based composite clocks.
 */

#ifndef __CV_COMPOSITE_CLOCK_H__
#define __CV_COMPOSITE_CLOCK_H__

#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct cv_gate {
	u32 reg;
	s8 shift;
	unsigned long flags;
};

struct cv_div {
	u32 reg;
	s8 shift;
	s8 width;
	s16 initval;
	unsigned long flags;
};

struct cv_mux {
	u32 reg;
	s8 shift;
	s8 width;
	unsigned long flags;
};

/*
 * mux[0] - bypass
 * mux[1] - clk_sel
 * mux[2] - clk_src (DIV_IN0_SRC_MUX), optional
 *
 * div[0] - div_in0
 * div[1] - div_in1
 */
struct cv_composite_clock {
	unsigned int id;
	const char *name;
	struct clk_hw hw;

	struct regmap *syscon;
	spinlock_t *lock;

	struct cv_gate gate;
	struct cv_div div[2];
	struct cv_mux mux[3];
};

#define CV_COMPOSITE_CLK(_id, _name, _parents, _gate_reg, _gate_shift,		\
	_div_0_reg, _div_0_shift, _div_0_width, _div_0_initval,			\
	_div_1_reg, _div_1_shift, _div_1_width, _div_1_initval,			\
	_mux_0_reg, _mux_0_shift,			\
	_mux_1_reg, _mux_1_shift,			\
	_mux_2_reg, _flags) {		\
.id = _id,						\
.name = _name,						\
.gate.reg = _gate_reg,					\
.gate.shift = _gate_shift,				\
.div[0].reg = _div_0_reg,				\
.div[0].shift = _div_0_shift,				\
.div[0].width = _div_0_width,				\
.div[0].initval = _div_0_initval,			\
.div[1].reg = _div_1_reg,				\
.div[1].shift = _div_1_shift,				\
.div[1].width = _div_1_width,				\
.div[1].initval = _div_1_initval,			\
.mux[0].reg = _mux_0_reg,				\
.mux[0].shift = _mux_0_shift,				\
.mux[0].width = 1,					\
.mux[1].reg = _mux_1_reg,				\
.mux[1].shift = _mux_1_shift,				\
.mux[1].width = 1,					\
.mux[2].reg = _mux_2_reg,				\
.mux[2].shift = 8,				\
.mux[2].width = 2,					\
.hw.init = CLK_HW_INIT_PARENTS(				\
		_name, _parents,			\
		&cv_clk_ops,				\
		_flags | CV_CLK_FLAGS_ALL),		\
}

#define CV_PLL_POST_CLK(_id, _name, _parents, _gate_reg, _gate_shift,		\
	_div_0_reg, _div_0_shift, _div_0_width, _div_0_initval,			\
	_div_1_reg, _div_1_shift, _div_1_width, _div_1_initval,			\
	_mux_0_reg, _mux_0_shift,			\
	_mux_1_reg, _mux_1_shift,			\
	_mux_2_reg, _flags) {		\
.id = _id,						\
.name = _name,						\
.gate.reg = _gate_reg,					\
.gate.shift = _gate_shift,				\
.div[0].reg = _div_0_reg,				\
.div[0].shift = _div_0_shift,				\
.div[0].width = _div_0_width,				\
.div[0].initval = _div_0_initval,			\
.div[1].reg = _div_1_reg,				\
.div[1].shift = _div_1_shift,				\
.div[1].width = _div_1_width,				\
.div[1].initval = _div_1_initval,			\
.mux[0].reg = _mux_0_reg,				\
.mux[0].shift = _mux_0_shift,				\
.mux[0].width = 1,					\
.mux[1].reg = _mux_1_reg,				\
.mux[1].shift = _mux_1_shift,				\
.mux[1].width = 1,					\
.mux[2].reg = _mux_2_reg,				\
.mux[2].shift = 8,				\
.mux[2].width = 2,					\
.hw.init = CLK_HW_INIT_PARENTS(				\
		_name, _parents,			\
		&cv_pll_post_clk_ops,				\
		_flags | CV_CLK_FLAGS_ALL),		\
}

extern const struct clk_ops cv_clk_ops;
extern const struct clk_ops cv_pll_post_clk_ops;

struct clk_hw *cv_register_composite_clk(struct cv_composite_clock *cclk,
					 struct regmap *syscon,
					 spinlock_t *lock);

int cv_register_composite_clks(struct cv_composite_clock *clks, int num_clks,
			       struct cv_clock_data *data);

#endif /* __CV_COMPOSITE_CLOCK_H__ */

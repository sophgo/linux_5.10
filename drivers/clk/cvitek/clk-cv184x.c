/*
 * Copyright (c) 2021 CVITEK
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/clock/cv184x-clock.h>
#include "clk-cv184x.h"

#define CV184X_CLK_FLAGS_ALL	(CLK_GET_RATE_NOCACHE)
// #define CV184X_CLK_FLAGS_ALL	(CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL)
// #define CV184X_CLK_FLAGS_ALL	(CLK_GET_RATE_NOCACHE | CLK_IGNORE_UNUSED)
#define CV184X_CLK_FLAGS_MUX	(CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT)

#define to_cv184x_pll_clk(_hw) container_of(_hw, struct cv184x_pll_hw_clock, hw)
#define to_cv184x_clk(_hw) container_of(_hw, struct cv184x_hw_clock, hw)

#define div_mask(width) ((1 << (width)) - 1)

static DEFINE_SPINLOCK(cv184x_clk_lock);

struct cv184x_clock_data {
	void __iomem *base;
	spinlock_t *lock;
#ifdef CONFIG_PM_SLEEP
	uint32_t clken_saved_regs[REG_CLK_EN_NUM];
	uint32_t clksel_saved_regs[REG_CLK_SEL_NUM];
	uint32_t clkbyp_saved_regs[REG_CLK_BYP_NUM];
	uint32_t clkdiv_saved_regs[REG_CLK_DIV_NUM];
	uint32_t g2_clkdiv_saved_regs[REG_CLK_G2_DIV_NUM];
	uint32_t pll_g2_csr_saved_regs[REG_PLL_G2_CSR_NUM];
	uint32_t a0pll_ssc_syn_set_saved_reg;
	uint32_t disppll_ssc_syn_set_saved_reg;
	uint32_t cam0pll_ssc_syn_set_saved_reg;
	uint32_t cam1pll_ssc_syn_set_saved_reg;
	uint32_t pll_g6_csr_saved_regs[REG_PLL_G6_CSR_NUM];
#endif /* CONFIG_PM_SLEEP */
	struct clk_hw_onecell_data hw_data;
};

struct cv184x_gate {
	u32		reg;
	s8		shift;
	unsigned long	flags;
};

struct cv184x_div {
	u32		reg;
	s8		shift;
	s8		width;
	s16		initval;
	unsigned long	flags;
};

struct cv184x_mux {
	u32		reg;
	s8		shift;
	s8		width;
	unsigned long	flags;
};

struct cv184x_hw_clock {
	unsigned int id;
	const char *name;
	struct clk_hw hw;
	void __iomem *base;
	spinlock_t *lock;

	struct cv184x_gate gate;
	struct cv184x_div div[2]; /* 0: DIV_IN0, 1: DIV_IN1 */
	struct cv184x_mux mux[3]; /* 0: bypass, 1: CLK_SEL, 2: CLK_SRC(DIV_IN0_SRC_MUX) */
};

struct cv184x_pll_clock {
	unsigned int	id;
	const char	*name;
	u32		reg_csr;
	u32		reg_ssc;
	s16		post_div_sel; /* -1: postdiv*/
	unsigned long	flags;
};

struct cv184x_pll_hw_clock {
	struct cv184x_pll_clock pll;
	void __iomem *base;
	spinlock_t *lock;
	struct clk_hw hw;
};

static const struct clk_ops cv184x_g6_pll_ops;
static const struct clk_ops cv184x_g2_pll_ops;
static const struct clk_ops cv184x_g2d_pll_ops;
static const struct clk_ops cv184x_clk_ops;

static struct cv184x_clock_data *clk_data;

static unsigned long cvi_clk_flags;

#define CV184X_CLK(_id, _name, _parents, _gate_reg, _gate_shift,		\
			_div_0_reg, _div_0_shift,			\
			_div_0_width, _div_0_initval,			\
			_div_1_reg, _div_1_shift,			\
			_div_1_width, _div_1_initval,			\
			_mux_0_reg, _mux_0_shift,			\
			_mux_1_reg, _mux_1_shift,			\
			_mux_2_reg, _mux_2_shift, _flags) {		\
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
		.mux[2].shift = _mux_2_shift,				\
		.mux[2].width = 2,					\
		.hw.init = CLK_HW_INIT_PARENTS(				\
				_name, _parents,			\
				&cv184x_clk_ops,				\
				_flags | CV184X_CLK_FLAGS_ALL),		\
	}


#define CLK_G6_PLL(_id, _name, _parent, _reg_csr, _flags) {		\
		.pll.id = _id,						\
		.pll.name = _name,					\
		.pll.reg_csr = _reg_csr,				\
		.pll.reg_ssc = 0,					\
		.pll.post_div_sel = -1,					\
		.hw.init = CLK_HW_INIT_PARENTS(_name, _parent,		\
					       &cv184x_g6_pll_ops,	\
					       _flags |			\
					       CV184X_CLK_FLAGS_ALL),	\
	}

#define CLK_G2_PLL(_id, _name, _parent, _reg_csr, _reg_ssc, _flags) {	\
		.pll.id = _id,						\
		.pll.name = _name,					\
		.pll.reg_csr = _reg_csr,				\
		.pll.reg_ssc = _reg_ssc,				\
		.pll.post_div_sel = -1,					\
		.hw.init = CLK_HW_INIT_PARENTS(_name, _parent,		\
					       &cv184x_g2_pll_ops,	\
					       _flags |			\
					       CV184X_CLK_FLAGS_ALL),	\
	}

#define CLK_G2D_PLL(_id, _name, _parent, _reg_csr, _reg_ssc,		\
			_post_div_sel, _flags) {			\
		.pll.id = _id,						\
		.pll.name = _name,					\
		.pll.reg_csr = _reg_csr,				\
		.pll.reg_ssc = _reg_ssc,				\
		.pll.post_div_sel = _post_div_sel,			\
		.hw.init = CLK_HW_INIT_PARENTS(_name, _parent,		\
					       &cv184x_g2d_pll_ops,	\
					       _flags |			\
					       CV184X_CLK_FLAGS_ALL),	\
	}

const char *const cv184x_pll_parent[] = {"osc"};
const char *const cv184x_frac_pll_parent[] = {"clk_mipimpll"};

/*
 * All PLL clocks are marked as CRITICAL, hence they are very crucial
 * for the functioning of the SoC
 */
static struct cv184x_pll_hw_clock cv184x_pll_clks[] = {
	CLK_G6_PLL(CV184X_CLK_MPLL, "clk_mpll", cv184x_pll_parent, REG_MPLL_CSR, 0),
	CLK_G6_PLL(CV184X_CLK_TPLL, "clk_tpll", cv184x_pll_parent, REG_TPLL_CSR, 0),
	CLK_G6_PLL(CV184X_CLK_FPLL, "clk_fpll", cv184x_pll_parent, REG_FPLL_CSR, 0),
	CLK_G6_PLL(CV184X_CLK_APPLL, "clk_appll", cv184x_pll_parent, REG_APPLL_CSR, 0),
	CLK_G6_PLL(CV184X_CLK_RVPLL, "clk_rvpll", cv184x_pll_parent, REG_RVPLL_CSR, 0),
	CLK_G2_PLL(CV184X_CLK_MIPIMPLL, "clk_mipimpll", cv184x_pll_parent, REG_MIPIMPLL_CSR, 0, 0),
	CLK_G2_PLL(CV184X_CLK_A0PLL, "clk_a0pll", cv184x_frac_pll_parent, REG_APLL0_CSR, REG_APLL_SSC_SYN_CTRL, 0),
	CLK_G2_PLL(CV184X_CLK_DISPPLL, "clk_disppll", cv184x_frac_pll_parent, REG_DISPPLL_CSR,
			REG_DISPPLL_SSC_SYN_CTRL, 0),
	CLK_G2_PLL(CV184X_CLK_CAM0PLL, "clk_cam0pll", cv184x_frac_pll_parent, REG_CAM0PLL_CSR, REG_CAM0PLL_SSC_SYN_CTRL,
		CLK_IGNORE_UNUSED),
	CLK_G2_PLL(CV184X_CLK_CAM1PLL, "clk_cam1pll", cv184x_frac_pll_parent, REG_CAM1PLL_CSR,
			REG_CAM1PLL_SSC_SYN_CTRL, 0),
	CLK_G2D_PLL(CV184X_CLK_MIPIMPLL_D3, "clk_mipimpll_d3", cv184x_pll_parent, REG_MIPIMPLL_CSR,
		0, 3, CLK_IGNORE_UNUSED),
	// CLK_G2D_PLL(CV184X_CLK_CAM0PLL_D2, "clk_cam0pll_d2", cv184x_frac_pll_parent, REG_CAM0PLL_CSR,
	// 	REG_CAM0PLL_SSC_SYN_CTRL, 2, CLK_IGNORE_UNUSED),
	// CLK_G2D_PLL(CV184X_CLK_CAM0PLL_D3, "clk_cam0pll_d3", cv184x_frac_pll_parent, REG_CAM0PLL_CSR,
	// 	REG_CAM0PLL_SSC_SYN_CTRL, 3, CLK_IGNORE_UNUSED),
};

#ifdef CONFIG_CVI_DUAL_OS_CLK
/*
* If it is a dual system, configure this clk as CLK-IGNORE-UNUSED.
* Even without a user, keep the clock on to prevent critical clocks 
* from being accidentally turned off.
*/
#define CVI_CLK_FLAG_FOR_OS (CLK_IGNORE_UNUSED)
#else
#define CVI_CLK_FLAG_FOR_OS (CLK_SET_RATE_GATE)
#endif

/*
 * Clocks marked as CRITICAL are needed for the proper functioning
 * of the SoC.
 */
static struct cv184x_hw_clock cv184x_clks[] = {
	CV184X_CLK(CV184X_CLK_FAB_100M, "clk_fab_100M",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_0, 0,
		REG_DIV_TOP_CLK_FAB_100M, 16, 16, 10,	//100MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 0,
		0, -1,
		REG_DIV_TOP_CLK_FAB_100M, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_HSPERI, "clk_hsperi",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 1,
		REG_DIV_TOP_CLK_HSPERI, 16, 16, 4,		//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 1,
		0, -1,
		REG_DIV_TOP_CLK_HSPERI, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_RTC_SYS, "clk_rtc_sys",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 2,
		REG_DIV_RTC_CLK_RTC_SYS, 16, 16, 4,		//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 2,
		0, -1,
		REG_DIV_RTC_CLK_RTC_SYS, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_FAB_500M, "clk_fab_500M",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_0, 3,
		REG_DIV_TOP_CLK_FAB_500M, 16, 16, 2,		//500MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 3,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_1M, "clk_1M",
		((const char *[]) {"osc"}),
		REG_CLK_EN_0, 4,
		REG_DIV_TOP_CLK_1M, 16, 16, 250,			//100KHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 4,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CPU, "clk_cpu",
		((const char *[]) {"osc", "clk_appll", "clk_fpll", "clk_mpll", "clk_mipimpll"}),
		REG_CLK_EN_0, 5,
		REG_DIV_AP_CPU_CLK_0, 16, 16, 1,			//1000MHz
		REG_DIV_AP_CPU_CLK_1, 16, 16, 2,			//1000MHz
		REG_CLK_BYP_0, 5,
		REG_CLK_SEL_0, 0,
		REG_DIV_AP_CPU_CLK_0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_RV1, "clk_rv1",
		((const char *[]) {"osc", "clk_rvpll", "clk_mpll", "clk_cam1pll", "clk_mipimpll"}),
		REG_CLK_EN_0, 6,
		REG_DIV_AP_CLK_RV1_0, 16, 16, 2,			//600MHz
		REG_DIV_AP_CLK_RV1_1, 16, 16, 3,			//600MHz
		REG_CLK_BYP_0, 6,
		REG_CLK_SEL_0, 1,
		REG_DIV_AP_CLK_RV1_0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_BUS, "clk_bus",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 7,
		REG_DIV_AP_BUS_CLK, 16, 16, 2,			//600MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 7,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_GIC, "clk_gic",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 8,
		REG_DIV_AP_GIC_CLK, 16, 16, 4,			//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 8,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_DBG, "clk_dbg",
		((const char *[]) {"osc"}),
		REG_CLK_EN_0, 9,
		0, -1, 0, 0,							//25MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 9,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SC, "clk_sc",
		((const char *[]) {"osc"}),
		REG_CLK_EN_0, 10,
		0, -1, 0, 0,							//25MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 10,
		0, -1,
		0, -1,
		CLK_IGNORE_UNUSED),
	CV184X_CLK(CV184X_CLK_TPU_SYS, "clk_tpu_sys",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 11,
		REG_DIV_TPU_CLK_TPU_SYS, 16, 16, 4,		//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 11,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_TPU_GDMA, "clk_tpu_gdma",
		((const char *[]) {"osc", "clk_tpll", "clk_fpll", "clk_cam0pll", "clk_mipimpll"}),
		REG_CLK_EN_0, 12,
		REG_DIV_TPU_CLK_GDMA_0, 16, 16, 3,		//500MHz
		REG_DIV_TPU_CLK_GDMA_1, 16, 16, 2,		//750MHz
		REG_CLK_BYP_0, 12,
		REG_CLK_SEL_0, 2,
		REG_DIV_TPU_CLK_GDMA_0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_TPU, "clk_tpu",
		((const char *[]) {"osc", "clk_tpll", "clk_fpll", "clk_mpll", "clk_mipimpll"}),
		REG_CLK_EN_0, 13,
		REG_DIV_TPU_CLK_TPU_0, 16, 16, 3,		//500MHz
		REG_DIV_TPU_CLK_TPU_1, 16, 16, 2,		//650MHz
		REG_CLK_BYP_0, 13,
		REG_CLK_SEL_0, 3,
		REG_DIV_TPU_CLK_TPU_0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VIDEO_AXI, "clk_video_axi",
		((const char *[]) {"osc", "clk_fpll", "", "clk_mpll", ""}),
		REG_CLK_EN_0, 14,
		REG_DIV_VC_CLK_VIDEO_AXI_0, 16, 16, 2,		//500MHz
		REG_DIV_VC_CLK_VIDEO_AXI_1, 16, 16, 2,		//600MHz
		REG_CLK_BYP_0, 14,
		REG_CLK_SEL_0, 4,
		REG_DIV_VC_CLK_VIDEO_AXI_0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VC_SRC0, "clk_vc_src0",
		((const char *[]) {"osc", "clk_mpll", "", "clk_cam0pll", ""}),
		REG_CLK_EN_0, 15,
		REG_DIV_VC_CLK_VC_SRC0_0, 16, 16, 2,		//600MHz
		REG_DIV_VC_CLK_VC_SRC0_1, 16, 16, 2,		//650MHz
		REG_CLK_BYP_0, 15,
		REG_CLK_SEL_0, 5,
		REG_DIV_VC_CLK_VC_SRC0_0, 8,
		CVI_CLK_FLAG_FOR_OS),
	CV184X_CLK(CV184X_CLK_VC_SRC1, "clk_vc_src1",
		((const char *[]) {"osc", "clk_cam1pll", "", "clk_fpll", ""}),
		REG_CLK_EN_0, 16,
		REG_DIV_VC_CLK_VC_SRC1_0, 16, 16, 4,		//400MHz
		REG_DIV_VC_CLK_VC_SRC1_1, 16, 16, 2,		//500MHz
		REG_CLK_BYP_0, 16,
		REG_CLK_SEL_0, 6,
		REG_DIV_VC_CLK_VC_SRC1_0, 8,
		CVI_CLK_FLAG_FOR_OS),
	CV184X_CLK(CV184X_CLK_X2P,  "clk_x2p",
		((const char *[]) {"clk_fab_100M"}),
		REG_CLK_EN_0, 17,
		0, -1, 0, 0,							//100MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 17,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_RAW_AXI, "clk_raw_axi",
		((const char *[]) {"osc", "clk_cam1pll"}),
		REG_CLK_EN_0, 18,
		REG_DIV_VIVO_CLK_RAW_AXI, 16, 16, 4,		//400MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 18,
		0, -1,
		REG_DIV_VIVO_CLK_RAW_AXI, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SRC_VIP_SYS_0, "clk_vip_sys_0",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 19,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_0, 16, 16, 8,		//150MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 19,
		0, -1,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SRC_VIP_SYS_1, "clk_vip_sys_1",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 20,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_1, 16, 16, 4,		//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 20,
		0, -1,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_1, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SRC_VIP_SYS_2, "clk_vip_sys_2",
		((const char *[]) {"osc", "clk_cam1pll"}),
		REG_CLK_EN_0, 21,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_2, 16, 16, 4,		//400MHz
		0, -1, 0, 0,
		REG_CLK_BYP_2, 21,
		0, -1,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_2, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SRC_VIP_SYS_3, "clk_vip_sys_3",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 22,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_3, 16, 16, 2,		//600MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 22,
		0, -1,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_3, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SRC_VIP_SYS_4, "clk_vip_sys_4",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 23,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_4, 16, 16, 6,		//200MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 23,
		0, -1,
		REG_DIV_VIVO_CLK_SRC_VIP_SYS_4, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SYS_DISP, "clk_sys_disp",
		((const char *[]) {"osc", "clk_disppll"}),
		REG_CLK_EN_0, 24,
		REG_DIV_VIVO_CLK_SYS_DISP, 16, 16, 8,			//148MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 24,
		0, -1,
		REG_DIV_VIVO_CLK_SYS_DISP, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CYC_SCAN_100M, "clk_scan_100M",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_0, 25,
		REG_DIV_VIVO_CLK_CYC_SCAN_100M, 16, 16, 51,		//19MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 25,
		0, -1,
		REG_DIV_VIVO_CLK_CYC_SCAN_100M, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CYC_DSI_ESC, "clk_cyc_dsi_esc",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_0, 26,
		REG_DIV_VIVO_CLK_CYC_DSI_ESC, 16, 16, 51,		//19MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 26,
		0, -1,
		REG_DIV_VIVO_CLK_CYC_DSI_ESC, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CYC_SCAN_300M, "clk_cyc_scan_300M",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 27,
		REG_DIV_VIVO_CLK_CYC_SCAN_300M, 16, 16, 4,		//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 27,
		0, -1,
		REG_DIV_VIVO_CLK_CYC_SCAN_300M, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CYC_DSI_SYN, "clk_cyc_dsi_syn",
		((const char *[]) {"osc", "clk_mipimpll"}),
		REG_CLK_EN_0, 28,
		REG_DIV_VIVO_CLK_CYC_DSI_SYN, 16, 16, 1,			//900MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 28,
		0, -1,
		REG_DIV_VIVO_CLK_CYC_DSI_SYN, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VIVO_MIPIMPLL, "clk_vivo_mipimpll",
		((const char *[]) {"osc", "clk_mipimpll"}),
		REG_CLK_EN_0, 29,
		REG_DIV_VIVO_CLK_MIPIMPLL, 16, 16, 1,			//900MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 29,
		0, -1,
		REG_DIV_VIVO_CLK_MIPIMPLL, 8,
		CLK_IS_CRITICAL),
		#if 0
	CV184X_CLK(CV184X_CLK_RTC_XTAL_25M, "clk_rtc_25M",
		((const char *[]) {"osc"}),
		0, -1,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
		#endif
	CV184X_CLK(CV184X_CLK_RTC_SPI_NOR, "clk_rtc_spi_nor",
		((const char *[]) {"osc", "clk_mpll", "clk_fpll", "clk_mipimpll"}),
		REG_CLK_EN_0, 30,
		REG_DIV_RTC_CLK_SPI_NOR, 16, 16, 4,				//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 30,
		0, -1,
		REG_DIV_RTC_CLK_SPI_NOR, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_USB20_BUS_EARLY, "clk_usb20_bus_early",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_0, 31,
		REG_DIV_HSPERI_USB20_BUS_EARLY, 16, 16, 4,		//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_0, 31,
		0, -1,
		REG_DIV_HSPERI_USB20_BUS_EARLY, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_USB20_SUSPEND, "clk_usb20_suspend",
		((const char *[]) {"osc", "osc"}),
		REG_CLK_EN_1, 0,
		REG_DIV_HSPERI_USB20_SUSPEND, 16, 16, 125,		//200KHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 0,
		0, -1,
		REG_DIV_HSPERI_USB20_SUSPEND, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_USB20_REF, "clk_usb20_ref",
		((const char *[]) {"osc", "clk_mpll"}),
		REG_CLK_EN_1, 1,
		REG_DIV_HSPERI_USB20_REF, 16, 16, 50,			//24MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 1,
		0, -1,
		REG_DIV_HSPERI_USB20_REF, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_USB20_CORECLKIN, "clk_usb20_coreclkin",
		((const char *[]) {"osc"}),
		REG_CLK_EN_1, 2,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 2,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SD0, "clk_sd0",
		((const char *[]) {"osc", "clk_cam1pll"}),
		REG_CLK_EN_1, 3,
		REG_DIV_HSPERI_CLK_SD0, 16, 16, 4,			//400MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 3,
		0, -1,
		REG_DIV_HSPERI_CLK_SD0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_100K_SD0, "clk_100k_sd0",
		((const char *[]) {"clk_1M"}),
		REG_CLK_EN_1, 4,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 4,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SD1, "clk_sd1",
		((const char *[]) {"osc", "clk_cam1pll"}),
		REG_CLK_EN_1, 5,
		REG_DIV_HSPERI_CLK_SD1, 16, 16, 4,			//400MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 5,
		0, -1,
		REG_DIV_HSPERI_CLK_SD1, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_100K_SD1, "clk_100k_sd1",
		((const char *[]) {"clk_1M"}),
		REG_CLK_EN_1, 6,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 6,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_EMMC_CARD, "clk_emmc_card",
		((const char *[]) {"osc", "clk_cam1pll"}),
		REG_CLK_EN_1, 7,
		REG_DIV_HSPERI_EMMC_CARD_CLK, 16, 16, 4,		//400MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 7,
		0, -1,
		REG_DIV_HSPERI_EMMC_CARD_CLK, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_EMMC_100K, "clk_emmc_100K",
		((const char *[]) {"clk_1M"}),
		REG_CLK_EN_1, 8,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_ETHER0_ETH_PLL, "clk_eth_pll",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_1, 9,
		REG_DIV_HSPERI_ETHER0_CLK_ETH_PLL, 16, 16, 2,		//500MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 9,
		0, -1,
		REG_DIV_HSPERI_ETHER0_CLK_ETH_PLL, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SPI_NOR, "clk_spi_nor",
		((const char *[]) {"osc", "clk_mpll", "clk_fpll", "clk_mipimpll"}),
		REG_CLK_EN_1, 10,
		REG_DIV_HSPERI_CLK_SPI_NOR, 16, 16, 4,			//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 10,
		0, -1,
		REG_DIV_HSPERI_CLK_SPI_NOR, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SPI_NAND, "clk_spi_nand",
		((const char *[]) {"osc", "clk_mpll", "clk_fpll", "clk_mipimpll"}),
		REG_CLK_EN_1, 11,
		REG_DIV_HSPERI_CLK_SPI_NAND, 16, 16, 4,			//300MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 11,
		0, -1,
		REG_DIV_HSPERI_CLK_SPI_NAND, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AUDSRC, "clk_audsrc",
		((const char *[]) {"osc", "clk_a0pll", "clk_a24k"}),
		REG_CLK_EN_1, 12,
		REG_DIV_HSPERI_CLK_AUDSRC, 16, 16, 17,			//24MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 12,
		0, -1,
		REG_DIV_HSPERI_CLK_AUDSRC, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AUD0, "clk_aud0",
		((const char *[]) {"osc", "clk_a0pll", "clk_a24k"}),
		REG_CLK_EN_1, 13,
		REG_DIV_HSPERI_CLK_AUD0, 16, 16, 17,				//24MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 13,
		0, -1,
		REG_DIV_HSPERI_CLK_AUD0, 8,
		CLK_IS_CRITICAL),

	CV184X_CLK(CV184X_CLK_AUD1, "clk_aud1",
		((const char *[]) {"osc", "clk_a0pll", "clk_a24k"}),
		REG_CLK_EN_1, 14,
		REG_DIV_HSPERI_CLK_AUD1, 16, 16, 17,				//24MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 14,
		0, -1,
		REG_DIV_HSPERI_CLK_AUD1, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AUD2, "clk_aud2",
		((const char *[]) {"osc", "clk_a0pll", "clk_a24k"}),
		REG_CLK_EN_1, 15,
		REG_DIV_HSPERI_CLK_AUD2, 16, 16, 17,				//24MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 15,
		0, -1,
		REG_DIV_HSPERI_CLK_AUD2, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AUD3, "clk_aud3",
		((const char *[]) {"osc", "clk_a0pll", "clk_a24k"}),
		REG_CLK_EN_1, 16,
		REG_DIV_HSPERI_CLK_AUD3, 16, 16, 17,				//24MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 16,
		0, -1,
		REG_DIV_HSPERI_CLK_AUD3, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SPI, "clk_spi",
		((const char *[]) {"osc", "clk_mpll", "clk_cam1pll"}),
		REG_CLK_EN_1, 17,
		REG_DIV_HSPERI_CLK_SPI, 16, 16, 6,				//200MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 17,
		0, -1,
		REG_DIV_HSPERI_CLK_SPI, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_I2C, "clk_i2c",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_1, 18,
		REG_DIV_HSPERI_CLK_I2C, 16, 16, 10,				//100MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 18,
		0, -1,
		REG_DIV_HSPERI_CLK_I2C, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_UART0, "clk_uart0",
		((const char *[]) {"osc", "clk_mpll", "clk_cam1pll"}),
		REG_CLK_EN_1, 19,
		REG_DIV_HSPERI_CLK_UART0, 16, 16, 6,				//200MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 19,
		0, -1,
		REG_DIV_HSPERI_CLK_UART0, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_UART1, "clk_uart1",
		((const char *[]) {"osc", "clk_mpll", "clk_cam1pll"}),
		REG_CLK_EN_1, 20,
		REG_DIV_HSPERI_CLK_UART1, 16, 16, 6,				//200MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 20,
		0, -1,
		REG_DIV_HSPERI_CLK_UART1, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_UART2, "clk_uart2",
		((const char *[]) {"osc", "clk_mpll", "clk_cam1pll"}),
		REG_CLK_EN_1, 21,
		REG_DIV_HSPERI_CLK_UART2, 16, 16, 6,				//200MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 21,
		0, -1,
		REG_DIV_HSPERI_CLK_UART2, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_UART3, "clk_uart3",
		((const char *[]) {"osc", "clk_mpll", "clk_cam1pll"}),
		REG_CLK_EN_1, 22,
		REG_DIV_HSPERI_CLK_UART3, 16, 16, 6,				//200MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 22,
		0, -1,
		REG_DIV_HSPERI_CLK_UART3, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_UART4, "clk_uart4",
		((const char *[]) {"osc", "clk_mpll", "clk_cam1pll"}),
		REG_CLK_EN_1, 23,
		REG_DIV_HSPERI_CLK_UART4, 16, 16, 6,				//200MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 23,
		0, -1,
		REG_DIV_HSPERI_CLK_UART4, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_WDT_PCLK, "clk_wdt_pclk",
		((const char *[]) {"osc"}),
		REG_CLK_EN_1, 24,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 24,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_GPIO_DBCLK, "clk_gpio_dbclk",
		((const char *[]) {"clk_1M"}),
		REG_CLK_EN_1, 25,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 25,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_WGN_XCLK, "clk_wgn_xclk",
		((const char *[]) {"osc"}),
		REG_CLK_EN_1, 26,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 26,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_KEYSCAN_XCLK, "clk_keyscan_xclk",
		((const char *[]) {"osc"}),
		REG_CLK_EN_1, 27,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 27,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_EFUSE_PCLK, "clk_efuse_pclk",
		((const char *[]) {"clk_fab_100M"}),
		REG_CLK_EN_1, 28,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 28,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_EFUSE, "clk_efuse_clk",
		((const char *[]) {"osc"}),
		REG_CLK_EN_1, 29,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_1, 29,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_PWM, "clk_pwm",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_1, 30,
		REG_DIV_PERI_PWM_CLK, 16, 16, 4,					//250MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 30,
		0, -1,
		REG_DIV_PERI_PWM_CLK, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_XTAL_MISC, "clk_xtal_misc",
		((const char *[]) {"osc", "clk_fpll"}),
		REG_CLK_EN_1, 31,
		REG_DIV_PERI_CLK_XTAL_MISC, 16, 16, 40,			//25MHz
		0, -1, 0, 0,
		REG_CLK_BYP_1, 31,
		0, -1,
		REG_DIV_PERI_CLK_XTAL_MISC, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_TEMPSEN, "clk_tempsen",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 0,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_2, 0,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SARADC, "clk_saradc",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 1,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_2, 1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_FAB6_100M_FREE, "clk_fab6_100M_free",
		((const char *[]) {"clk_fab_100M"}),
		REG_CLK_EN_2, 2,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_2, 2,
		0, -1,
		0, -1,
		CLK_IGNORE_UNUSED),
	CV184X_CLK(CV184X_CLK_DBGSYS, "clk_dbgsys",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 3,
		0, -1, 0, 0,
		0, -1, 0, 0,
		REG_CLK_BYP_2, 3,
		0, -1,
		0, -1,
		CLK_IGNORE_UNUSED),
	// ----- just clk gate -----
	// ---- vivo ----
	CV184X_CLK(CV184X_CLK_DISP_VIP, "clk_disp_vip",
		((const char *[]) {"clk_sys_disp"}),
		REG_CLK_EN_2, 4,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CSI_MAC0_VIP, "clk_csi_mac0_vip",
		((const char *[]) {"clk_raw_axi"}),
		REG_CLK_EN_2, 5,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CSI_MAC1_VIP, "clk_csi_mac1_vip",
		((const char *[]) {"clk_raw_axi"}),
		REG_CLK_EN_2, 6,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CSI_MAC2_VIP, "clk_csi_mac2_vip",
		((const char *[]) {"clk_raw_axi"}),
		REG_CLK_EN_2, 7,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CSI_BE_VIP, "clk_csi_be_vip",
		((const char *[]) {"clk_raw_axi"}),
		REG_CLK_EN_2, 8,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_ISP_TOP_VIP, "clk_isp_top_vip",
		((const char *[]) {"clk_raw_axi"}),
		REG_CLK_EN_2, 9,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_RAW_VIP, "clk_raw_vip",
		((const char *[]) {"clk_raw_axi"}),
		REG_CLK_EN_2, 10,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VPSS0_VIP, "clk_vpss0_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 11,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VPSS1_VIP, "clk_vpss1_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 12,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VPSS2_VIP, "clk_vpss2_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 13,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VPSS3_VIP, "clk_vpss3_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 14,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_LDC_VIP, "clk_ldc_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 15,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CVI_CLK_FLAG_FOR_OS),
	CV184X_CLK(CV184X_CLK_CAM0_VIP, "clk_cam0_vip",
		((const char *[]) {"clk_cam0pll", "clk_disppll", "clk_mpll", "clk_mipipll_d3"}),
		REG_CLK_EN_2, 16,
		REG_CLK_CAM0_SRC_DIV, 16, 6, -1,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		REG_CLK_CAM0_SRC_DIV, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CAM1_VIP, "clk_cam1_vip",
		((const char *[]) {"clk_cam0pll", "clk_disppll", "clk_mpll", "clk_mipipll_d3"}),
		REG_CLK_EN_2, 17,
		REG_CLK_CAM1_SRC_DIV, 16, 6, -1,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		REG_CLK_CAM1_SRC_DIV, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CAM2_VIP, "clk_cam2_vip",
		((const char *[]) {"clk_cam0pll", "clk_disppll", "clk_mpll", "clk_mipipll_d3"}),
		REG_CLK_EN_2, 18,
		REG_CLK_CAM2_SRC_DIV, 16, 6, -1,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		REG_CLK_CAM2_SRC_DIV, 8,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_PAD_VI0_CLK0_VIP, "clk_pad_vi0_clk0_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 19,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_PAD_VI0_CLK1_VIP, "clk_pad_vi0_clk1_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 20,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_PAD_VI1_CLK_VIP, "clk_pad_vi1_clk_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 21,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_PAD_VI2_CLK_VIP, "clk_pad_vi2_clk_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 22,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_LVDS0_VIP, "clk_lvds0_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 23,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_LVDS1_VIP, "clk_lvds1_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 24,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_DSI_MAC_VIP, "clk_dsi_mac_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 25,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CSI0_RX_VIP, "clk_csi0_rx_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 26,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CSI1_RX_VIP, "clk_csi1_rx_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 27,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_CSI2_RX_VIP, "clk_csi2_rx_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 28,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VO_MAC_VIP, "clk_vo_mac_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 29,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_2DE_VIP, "clk_2de_vip",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 30,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	// ----vc----
	CV184X_CLK(CV184X_CLK_APB_VCSYS, "clk_apb_vcsys",
		((const char *[]) {"osc"}),
		REG_CLK_EN_2, 31,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_APB_VE, "clk_apb_ve",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 0,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_APB_JPEG, "clk_apb_jpeg",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 1,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_VE, "clk_ve",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 2,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_JPEG, "clk_jpeg",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 3,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_APB_AUDSRC, "clk_apb_audsrc",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 4,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AHB_ROM, "clk_ahb_rom",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 5,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AXI4_EMMC, "clk_axi4_emmc",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 6,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AXI4_SD0, "clk_axi4_sd0",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 7,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AXI4_SD1, "clk_axi4_sd1",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 8,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SPI_NAND_GATE, "clk_spi_nand_gate",
		((const char *[]) {"clk_spi_nand"}),
		REG_CLK_EN_3, 9,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AXI4_ETH0, "clk_axi4_eth0",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 10,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AXI4_ETH1, "clk_axi4_eth1",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 11,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AHB_SF, "clk_ahb_sf",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 12,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_AHB_SF1, "clk_ahb_sf1",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 13,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SDMA0_AXI, "clk_sdma0_axi",
		((const char *[]) {"clk_hsperi"}),
		REG_CLK_EN_3, 14,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SDMA1_AXI, "clk_sdma1_axi",
		((const char *[]) {"clk_hsperi"}),
		REG_CLK_EN_3, 14,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
		#if 0
	CV184X_CLK(CV184X_CLK_SDMA_AUD0, "clk_sdma_aud0",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 15,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SDMA_AUD1, "clk_sdma_aud1",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 16,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SDMA_AUD2, "clk_sdma_aud2",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 17,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_SDMA_AUD3, "clk_sdma_aud3",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 18,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
		#endif
	CV184X_CLK(CV184X_CLK_APB_SPI0, "clk_apb_spi0",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 19,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_APB_SPI1, "clk_apb_spi1",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 20,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_APB_SPI2, "clk_apb_spi2",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 21,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_APB_SPI3, "clk_apb_spi3",
		((const char *[]) {"osc"}),
		REG_CLK_EN_3, 22,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CLK_IS_CRITICAL),
	CV184X_CLK(CV184X_CLK_OENC, "clk_oenc",
		((const char *[]) {"clk_raw_axi"}),
		REG_CLK_EN_4, 0,
		0, -1, 0, 0,
		0, -1, 0, 0,
		0, -1,
		0, -1,
		0, -1,
		CVI_CLK_FLAG_FOR_OS),
};

static int __init cvi_clk_flags_setup(char *arg)
{
	int ret;
	unsigned long flags;

	ret = kstrtol(arg, 0, &flags);
	if (ret)
		return ret;

	cvi_clk_flags = flags;
	pr_info("cvi_clk_flags = 0x%lX\n", cvi_clk_flags);

	return 1;
}
__setup("cvi_clk_flags=", cvi_clk_flags_setup);

static unsigned long cv184x_pll_rate_calc(u32 regval, s16 post_div_sel, unsigned long parent_rate)
{
	u64 numerator;
	u32 predivsel, postdivsel, divsel;
	u32 denominator;

	predivsel = regval & 0x7f;
	postdivsel = post_div_sel < 0 ? (regval >> 8) & 0x7f : (u32)post_div_sel;
	divsel = (regval >> 17) & 0x7f;

	numerator = parent_rate * divsel;
	denominator = predivsel * postdivsel;
	do_div(numerator, denominator);

	return (unsigned long)numerator;
}

static unsigned long cv184x_g6_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct cv184x_pll_hw_clock *pll_hw = to_cv184x_pll_clk(hw);
	unsigned long rate;
	u32 regval;

	regval = readl(pll_hw->base + pll_hw->pll.reg_csr);
	rate = cv184x_pll_rate_calc(regval, pll_hw->pll.post_div_sel, parent_rate);

	return rate;
}

static long cv184x_g6_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	return rate;
}

static int cv184x_g6_pll_calc_csr(unsigned long parent_rate, unsigned long rate, u32 *csr)
{
	u64 numerator;
	u32 denominator;
	u32 divsel;		/* [23:17] DIV_SEL */
	u32 postdivsel = 1;	/* [14:8] POST_DIV_SEL */
	u32 ictrl = 7;		/* [26:24] ICTRL */
	u32 selmode = 1;	/* [16:15] SEL_MODE */
	u32 predivsel = 1;	/* [6:0] PRE_DIV_SEL */
	u32 vco_clks[] = {900, 1000, 1100, 1200, 1300, 1400, 1500, 1600};
	int i;

	for (i = 0; i < ARRAY_SIZE(vco_clks); i++) {
		if ((vco_clks[i] * 1000000) % rate == 0) {
			postdivsel = vco_clks[i] * 1000000 / rate;
			rate = vco_clks[i] * 1000000;
			pr_debug("rate=%ld, postdivsel=%d\n", rate, postdivsel);
			break;
		}
	}

	numerator = rate;
	denominator = parent_rate;

	do_div(numerator, denominator);

	divsel = (u32)numerator & 0x7f;
	*csr = (divsel << 17) | (postdivsel << 8) | (ictrl << 24) | (selmode << 15) | predivsel;

	pr_debug("csr=0x%08x\n", *csr);

	return 0;
}

static int cv184x_g6_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct cv184x_pll_hw_clock *pll_hw = to_cv184x_pll_clk(hw);
	unsigned long flags = 0;
	int ret;
	u32 reg_g6_pll_status;
	u32 regval_csr;
	u32 regval_g6_pll_status;
	u32 g6_pll_update_status = 0;
	ktime_t timeout;

	reg_g6_pll_status = (pll_hw->pll.reg_csr & ~PLL_STATUS_MASK) + PLL_STATUS_OFFSET;

	if (pll_hw->lock)
		spin_lock_irqsave(pll_hw->lock, flags);
	else
		__acquire(pll_hw->lock);

	/* calculate csr register */
	ret = cv184x_g6_pll_calc_csr(parent_rate, rate, &regval_csr);
	if (ret < 0)
		return ret;

	/* csr register */
	writel(regval_csr, pll_hw->base + pll_hw->pll.reg_csr);

	if (pll_hw->pll.reg_csr == REG_MPLL_CSR)
		g6_pll_update_status = BIT(0);
	else if (pll_hw->pll.reg_csr == REG_TPLL_CSR)
		g6_pll_update_status = BIT(1);
	else if (pll_hw->pll.reg_csr == REG_FPLL_CSR)
		g6_pll_update_status = BIT(2);

	/* wait for pll setting updated */
	timeout = ktime_add_ms(ktime_get(), CV184X_PLL_LOCK_TIMEOUT_MS);
	while (1) {
		regval_g6_pll_status = readl(pll_hw->base + reg_g6_pll_status);
		if ((regval_g6_pll_status & g6_pll_update_status) == 0)
			break;

		if (ktime_after(ktime_get(), timeout)) {
			pr_err("timeout waiting for pll update, g6_pll_status = 0x%08x\n",
			       regval_g6_pll_status);
			break;
		}
		cpu_relax();
	}

	if (pll_hw->lock)
		spin_unlock_irqrestore(pll_hw->lock, flags);
	else
		__release(pll_hw->lock);

	return 0;
}

static const struct clk_ops cv184x_g6_pll_ops = {
	.recalc_rate = cv184x_g6_pll_recalc_rate,
	.round_rate = cv184x_g6_pll_round_rate,
	.set_rate = cv184x_g6_pll_set_rate,
};

static unsigned long cv184x_g2_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct cv184x_pll_hw_clock *pll_hw = to_cv184x_pll_clk(hw);
	u32 reg_ssc_set;
	u32 reg_g2_ssc_ctrl;
	u32 regval_csr;
	u32 regval_ssc_set;
	u32 regval_g2_ssc_ctrl;
	u64 numerator;
	u32 denominator;
	unsigned long clk_ref;
	unsigned long rate;

	regval_csr = readl(pll_hw->base + pll_hw->pll.reg_csr);

	/* pll without synthesizer */
	if (pll_hw->pll.reg_ssc == 0) {
		clk_ref = parent_rate;
		goto rate_calc;
	}

	/* calculate synthesizer freq */
	reg_ssc_set = (pll_hw->pll.reg_ssc & ~SSC_SYN_SET_MASK) + SSC_SYN_SET_OFFSET;
	reg_g2_ssc_ctrl = (pll_hw->pll.reg_ssc & ~G2_SSC_CTRL_MASK) + G2_SSC_CTRL_OFFSET;

	regval_ssc_set = readl(pll_hw->base + reg_ssc_set);
	regval_g2_ssc_ctrl = readl(pll_hw->base + reg_g2_ssc_ctrl);

	/* bit0 sel_syn_clk */
	numerator = (regval_g2_ssc_ctrl & 0x1) ? parent_rate : (parent_rate >> 1);

	numerator <<= 26;
	denominator = regval_ssc_set;
	if (denominator)
		do_div(numerator, denominator);
	else
		pr_err("G2 pll ssc_set is zero, reg_ssc(0x%x), reg_ssc_set(0x%x) reg_g2_ssc_ctrl(0x%x)\n",
				pll_hw->pll.reg_ssc, reg_ssc_set, reg_g2_ssc_ctrl);

	clk_ref = numerator;

rate_calc:
	rate = cv184x_pll_rate_calc(regval_csr, pll_hw->pll.post_div_sel, clk_ref);

	return rate;
}

static const struct {
	unsigned long rate;
	u32 csr;
	u32 ssc_set;
} g2_pll_rate_lut[] = {
	// {.rate = 48000000, .csr = 0x00129201, .ssc_set = 629145600},
	// {.rate = 406425600, .csr = 0x010E9201, .ssc_set = 594430839},
	// {.rate = 417792000, .csr = 0x01109201, .ssc_set = 642509804},
	// {.rate = 768000000, .csr = 0x00108101, .ssc_set = 419430400},
	// {.rate = 832000000, .csr = 0x00108101, .ssc_set = 387166523},
	// {.rate = 1032000000, .csr = 0x00148101, .ssc_set = 390167814},
	// {.rate = 1050000000, .csr = 0x00168101, .ssc_set = 421827145},
	// {.rate = 1056000000, .csr = 0x00208100, .ssc_set = 412977625},
	// {.rate = 1125000000, .csr = 0x00168101, .ssc_set = 393705325},
	// {.rate = 1188000000, .csr = 0x00188101, .ssc_set = 610080582}, //postdiv=1
	{.rate = 1188000000, .csr = 0x00308201, .ssc_set = 610080582}, //postdiv=2
};

static int cv184x_g2_pll_get_setting_from_lut(unsigned long rate, u32 *csr,
					      u32 *ssc_set)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g2_pll_rate_lut); i++) {
		if (rate == g2_pll_rate_lut[i].rate) {
			*csr = g2_pll_rate_lut[i].csr;
			*ssc_set = g2_pll_rate_lut[i].ssc_set;
			return 0;
		}
	}

	*csr = 0;
	*ssc_set = 0;
	return -ENOENT;
}

static long cv184x_g2_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	return rate;
}

static int cv184x_g2_pll_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct cv184x_pll_hw_clock *pll_hw = to_cv184x_pll_clk(hw);
	unsigned long flags = 0;
	int ret;
	u32 reg_ssc_set;
	u32 reg_ssc_ctrl;
	u32 reg_g2_pll_status;
	u32 regval_csr;
	u32 regval_ssc_set;
	u32 regval_ssc_ctrl;
	u32 regval_g2_pll_status;
	u32 g2_pll_update_status = 0;
	ktime_t timeout;

	/* pll without synthesizer */
	if (pll_hw->pll.reg_ssc == 0)
		return -ENOENT;

	ret = cv184x_g2_pll_get_setting_from_lut(rate, &regval_csr,
						 &regval_ssc_set);
	if (ret < 0)
		return ret;

	reg_ssc_set = (pll_hw->pll.reg_ssc & ~SSC_SYN_SET_MASK) + SSC_SYN_SET_OFFSET;
	reg_ssc_ctrl = pll_hw->pll.reg_ssc;
	reg_g2_pll_status = (pll_hw->pll.reg_csr & ~PLL_STATUS_MASK) + PLL_STATUS_OFFSET;

	if (pll_hw->lock)
		spin_lock_irqsave(pll_hw->lock, flags);
	else
		__acquire(pll_hw->lock);

	/* set synthersizer */
	writel(regval_ssc_set, pll_hw->base + reg_ssc_set);

	/* bit 0 toggle */
	regval_ssc_ctrl = readl(pll_hw->base + reg_ssc_ctrl);
	regval_ssc_ctrl ^= 0x00000001;
	writel(regval_ssc_ctrl, pll_hw->base + reg_ssc_ctrl);

	/* csr register */
	writel(regval_csr, pll_hw->base + pll_hw->pll.reg_csr);

	if (pll_hw->pll.reg_csr == REG_MIPIMPLL_CSR)
		g2_pll_update_status = BIT(0);
	else if (pll_hw->pll.reg_csr == REG_APLL0_CSR)
		g2_pll_update_status = BIT(1);
	else if (pll_hw->pll.reg_csr == REG_DISPPLL_CSR)
		g2_pll_update_status = BIT(2);
	else if (pll_hw->pll.reg_csr == REG_CAM0PLL_CSR)
		g2_pll_update_status = BIT(3);
	else if (pll_hw->pll.reg_csr == REG_CAM1PLL_CSR)
		g2_pll_update_status = BIT(4);

	/* wait for pll setting updated */
	timeout = ktime_add_ms(ktime_get(), CV184X_PLL_LOCK_TIMEOUT_MS);
	while (1) {
		regval_g2_pll_status = readl(pll_hw->base + reg_g2_pll_status);
		if ((regval_g2_pll_status & g2_pll_update_status) == 0)
			break;

		if (ktime_after(ktime_get(), timeout)) {
			pr_err("timeout waiting for pll update, g2_pll_status = 0x%08x\n",
			       regval_g2_pll_status);
			break;
		}
		cpu_relax();
	}

	if (pll_hw->lock)
		spin_unlock_irqrestore(pll_hw->lock, flags);
	else
		__release(pll_hw->lock);

	return 0;
}

static const struct clk_ops cv184x_g2_pll_ops = {
	.recalc_rate = cv184x_g2_pll_recalc_rate,
	.round_rate = cv184x_g2_pll_round_rate,
	.set_rate = cv184x_g2_pll_set_rate,
};

static const struct clk_ops cv184x_g2d_pll_ops = {
	.recalc_rate = cv184x_g2_pll_recalc_rate,
};

static struct clk_hw *cv184x_clk_register_pll(struct cv184x_pll_hw_clock *pll_clk,
					    void __iomem *sys_base)
{
	struct clk_hw *hw;
	struct clk_init_data init;
	int err;

	pll_clk->lock = &cv184x_clk_lock;
	pll_clk->base = sys_base;

	if (cvi_clk_flags) {
		/* copy clk_init_data for modification */
		memcpy(&init, pll_clk->hw.init, sizeof(init));

		init.flags |= cvi_clk_flags;
		pll_clk->hw.init = &init;
	}

	hw = &pll_clk->hw;

	err = clk_hw_register(NULL, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}

static void cv184x_clk_unregister_pll(struct clk_hw *hw)
{
	struct cv184x_pll_hw_clock *pll_hw = to_cv184x_pll_clk(hw);

	clk_hw_unregister(hw);
	kfree(pll_hw);
}

static int cv184x_clk_register_plls(struct cv184x_pll_hw_clock *clks,
				    int num_clks,
				    struct cv184x_clock_data *data)
{
	struct clk_hw *hw;
	void __iomem *pll_base = data->base;
	int i;

	for (i = 0; i < num_clks; i++) {
		struct cv184x_pll_hw_clock *cv184x_clk = &clks[i];

		hw = cv184x_clk_register_pll(cv184x_clk, pll_base);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, cv184x_clk->pll.name);
			goto err_clk;
		}

		data->hw_data.hws[clks[i].pll.id] = hw;

		clk_hw_register_clkdev(hw, cv184x_clk->pll.name, NULL);
	}

	return 0;

err_clk:
	while (i--)
		cv184x_clk_unregister_pll(data->hw_data.hws[clks[i].pll.id]);

	return PTR_ERR(hw);
}

static int cv184x_clk_is_bypassed(struct cv184x_hw_clock *clk_hw)
{
	u32 val;
	void __iomem *reg_addr = clk_hw->base + clk_hw->mux[0].reg;

	if (clk_hw->mux[0].shift >= 0) {
		val = readl(reg_addr) >> clk_hw->mux[0].shift;
		val &= 0x1; //width
	} else {
		val = 0;
	}

	return val;
}

static int cv184x_clk_get_clk_sel(struct cv184x_hw_clock *clk_hw)
{
	u32 val;
	void __iomem *reg_addr = clk_hw->base + clk_hw->mux[1].reg;

	if (clk_hw->mux[1].shift >= 0) {
		val = readl(reg_addr) >> clk_hw->mux[1].shift;
		val &= 0x1; //width
		val ^= 0x1; //invert value
	} else {
		val = 0;
	}

	return val;
}

static int cv184x_clk_get_src_sel(struct cv184x_hw_clock *clk_hw)
{
	u32 val;
	void __iomem *reg_addr = clk_hw->base + clk_hw->mux[2].reg;

	if (clk_hw->mux[2].shift >= 0) {
		val = readl(reg_addr) >> clk_hw->mux[2].shift;
		val &= 0x3; //width
	} else {
		val = 0;
	}

	return val;
}

static unsigned long cv184x_clk_div_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);
	unsigned int clk_sel = cv184x_clk_get_clk_sel(clk_hw);
	void __iomem *reg_addr = clk_hw->base + clk_hw->div[clk_sel].reg;
	unsigned int val;
	unsigned long rate;

	if ((clk_hw->mux[0].shift >= 0) && cv184x_clk_is_bypassed(clk_hw))
		return parent_rate;

	if ((clk_hw->div[clk_sel].initval > 0) && !(readl(reg_addr) & BIT(3))) {
		val = clk_hw->div[clk_sel].initval;
	} else {
		val = readl(reg_addr) >> clk_hw->div[clk_sel].shift;
		val &= div_mask(clk_hw->div[clk_sel].width);
	}
	rate = divider_recalc_rate(hw, parent_rate, val, NULL,
				   clk_hw->div[clk_sel].flags,
				   clk_hw->div[clk_sel].width);

	return rate;
}

static long cv184x_clk_div_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *prate)
{
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);
	unsigned int clk_sel = cv184x_clk_get_clk_sel(clk_hw);

	if ((clk_hw->mux[0].shift >= 0) && cv184x_clk_is_bypassed(clk_hw))
		return DIV_ROUND_UP_ULL((u64)*prate, 1);

	return divider_round_rate(hw, rate, prate, NULL,
				  clk_hw->div[clk_sel].width, clk_hw->div[clk_sel].flags);
}

static long cv184x_clk_div_calc_round_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long *prate)
{
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);

	if (clk_hw->div[0].shift > 0)
		return divider_round_rate(hw, rate, prate, NULL,
					clk_hw->div[0].width, clk_hw->div[0].flags);
	else
		return DIV_ROUND_UP_ULL((u64)*prate, 1);
}

static int cv184x_clk_div_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct clk_hw *current_parent;
	unsigned long parent_rate;
	unsigned long best_delta;
	unsigned long best_rate;
	u32 parent_count;
	long rate;
	u32 which;

	pr_debug("%s()_%d: req->rate=%ld\n", __func__, __LINE__, req->rate);

	parent_count = clk_hw_get_num_parents(hw);
	pr_debug("%s()_%d: parent_count=%d\n", __func__, __LINE__, parent_count);

	if ((parent_count < 2) || (clk_hw_get_flags(hw) & CLK_SET_RATE_NO_REPARENT)) {
		rate = cv184x_clk_div_round_rate(hw, req->rate, &req->best_parent_rate);
		if (rate < 0)
			return rate;

		req->rate = rate;
		return 0;
	}

	/* Unless we can do better, stick with current parent */
	current_parent = clk_hw_get_parent(hw);
	parent_rate = clk_hw_get_rate(current_parent);
	best_rate = cv184x_clk_div_calc_round_rate(hw, req->rate, &parent_rate);
	best_delta = abs(best_rate - req->rate);

	pr_debug("%s()_%d: parent_rate=%ld, best_rate=%ld, best_delta=%ld\n",
		 __func__, __LINE__, parent_rate, best_rate, best_delta);

	/* Check whether any other parent clock can produce a better result */
	for (which = 0; which < parent_count; which++) {
		struct clk_hw *parent = clk_hw_get_parent_by_index(hw, which);
		unsigned long delta;
		unsigned long other_rate;

		pr_debug("%s()_%d: idx=%d, parent_rate=%ld, best_rate=%ld, best_delta=%ld\n",
			 __func__, __LINE__, which, parent_rate, best_rate, best_delta);

		if (!parent)
			continue;

		if (parent == current_parent)
			continue;

		/* Not support CLK_SET_RATE_PARENT */
		parent_rate = clk_hw_get_rate(parent);
		other_rate = cv184x_clk_div_calc_round_rate(hw, req->rate, &parent_rate);
		delta = abs(other_rate - req->rate);
		pr_debug("%s()_%d: parent_rate=%ld, other_rate=%ld, delta=%ld\n",
			 __func__, __LINE__, parent_rate, other_rate, delta);
		if (delta < best_delta) {
			best_delta = delta;
			best_rate = other_rate;
			req->best_parent_hw = parent;
			req->best_parent_rate = parent_rate;
			pr_debug("%s()_%d: parent_rate=%ld, best_rate=%ld, best_delta=%ld\n",
				 __func__, __LINE__, parent_rate, best_rate, best_delta);
		}
	}

	req->rate = best_rate;

	return 0;
}

static int cv184x_clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);
	unsigned int clk_sel = cv184x_clk_get_clk_sel(clk_hw);
	void __iomem *reg_addr = clk_hw->base + clk_hw->div[clk_sel].reg;
	unsigned long flags = 0;
	int value;
	u32 val;

	pr_debug("%s()_%d:%s, rate=%ld, parent_rate=%ld\n", __func__, __LINE__, clk_hw->name, rate, parent_rate);

	if (clk_hw->div[clk_sel].shift < 0)
		pr_err("Error: %s: div[%d].shift = %d\n", __func__, clk_sel, clk_hw->div[clk_sel].shift);

	value = divider_get_val(rate, parent_rate, NULL,
				clk_hw->div[clk_sel].width,
				clk_hw->div[clk_sel].flags);
	if (value < 0)
		return value;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);
	else
		__acquire(clk_hw->lock);

	val = readl(reg_addr);
	val &= ~(div_mask(clk_hw->div[clk_sel].width) << clk_hw->div[clk_sel].shift);
	val |= (u32)value << clk_hw->div[clk_sel].shift;
	if (!(clk_hw->div[clk_sel].initval < 0))
		val |= BIT(3);
	writel(val, reg_addr);

	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);
	else
		__release(clk_hw->lock);

	return 0;
}

static void cv184x_clk_gate_endisable(struct clk_hw *hw, int enable)
{
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);
	void __iomem *reg_addr = clk_hw->base + clk_hw->gate.reg;
	unsigned long flags = 0;
	u32 reg;

	if (clk_hw->gate.shift < 0)
		pr_err("Error: %s: gate.shift = %d\n", __func__, clk_hw->gate.shift);

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);
	else
		__acquire(clk_hw->lock);

	reg = readl(reg_addr);

	if (enable)
		reg |= BIT(clk_hw->gate.shift);
	else
		reg &= ~BIT(clk_hw->gate.shift);

	writel(reg, reg_addr);

	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);
	else
		__release(clk_hw->lock);
}

static int cv184x_clk_gate_enable(struct clk_hw *hw)
{
	cv184x_clk_gate_endisable(hw, 1);

	return 0;
}

static void cv184x_clk_gate_disable(struct clk_hw *hw)
{
	cv184x_clk_gate_endisable(hw, 0);
}

static int cv184x_clk_gate_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);
	void __iomem *reg_addr = clk_hw->base + clk_hw->gate.reg;

	if (clk_hw->gate.shift < 0)
		pr_err("Error: %s: gate.shift = %d\n", __func__, clk_hw->gate.shift);

	reg = readl(reg_addr);

	reg &= BIT(clk_hw->gate.shift);

	if (clk_hw_get_flags(hw) & CLK_IGNORE_UNUSED)
		return __clk_get_enable_count(hw->clk) ? (reg ? 1 : 0) : 0;
	else
		return reg ? 1 : 0;
}

static u8 cv184x_clk_mux_get_parent(struct clk_hw *hw)
{
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);
	u8 clk_sel = cv184x_clk_get_clk_sel(clk_hw);
	u8 src_sel = cv184x_clk_get_src_sel(clk_hw);
	u8 parent_idx = 0;

	/*
	 * | 0     | 1     | 2     | 3     | 4     |
	 * +-------+-------+-------+-------+-------+
	 * | XTAL  | src0_0 | src0_1 | src1_0 | src1_1 |
	 * | XTAL  | src0_0 | src0_1 | src1_0 | src1_1 |
	 * | src_0 | src_1  | src_2  | src_3  |        |
	 * +-------+-------+-------+-------+-------+
	 */

	if (clk_hw->mux[0].shift >= 0) {
		// clk with bypass reg
		if (cv184x_clk_is_bypassed(clk_hw)) {
			parent_idx = 0;
		} else {
			if (clk_hw->mux[1].shift >= 0) {
				// clk with clk_sel reg
				if (clk_sel) {
					parent_idx = 3 + src_sel;
				} else {
					parent_idx = src_sel + 1;
				}
			} else {
				// clk without clk_sel reg
				parent_idx = src_sel + 1;
			}
		}
	} else {
		// clk without bypass reg
		if (clk_hw->mux[1].shift >= 0) {
			// clk with clk_sel reg
			if (clk_sel) {
				parent_idx = 0;
			} else {
				parent_idx = src_sel + 1;
			}
		} else {
			//clk without clk_sel reg
			parent_idx = src_sel;
		}
	}

	return parent_idx;
}

static int cv184x_clk_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv184x_hw_clock *clk_hw = to_cv184x_clk(hw);
	unsigned long flags = 0;
	void __iomem *reg_addr;
	unsigned int reg;

	if (clk_hw->lock)
		spin_lock_irqsave(clk_hw->lock, flags);
	else
		__acquire(clk_hw->lock);

	/*
	 * | 0     | 1     | 2     | 3     | 4     | 5     |
	 * +-------+-------+-------+-------+-------+-------+
	 * | XTAL  | DIV_1 | src_0 | src_1 | src_2 | src_3 |
	 * | XTAL  | src_0 | src_1 | src_2 | src_3 |       |
	 * | DIV_1 | src_0 | src_1 | src_2 | src_3 |       |
	 * | src_0 | src_1 | src_2 | src_3 |       |       |
	 * +-------+-------+-------+-------+-------+-------+
	 */

	if (index == 0) {
		if (clk_hw->mux[0].shift >= 0) {
			// set bypass
			reg_addr = clk_hw->base + clk_hw->mux[0].reg;
			reg = readl(reg_addr);
			reg |= 1 << clk_hw->mux[0].shift;
			writel(reg, reg_addr);
			goto unlock_release;
		} else if (clk_hw->mux[1].shift >= 0) {
			// set clk_sel to DIV_1
			reg_addr = clk_hw->base + clk_hw->mux[1].reg;
			reg = readl(reg_addr);
			reg &= ~(1 << clk_hw->mux[1].shift);
			writel(reg, reg_addr);
			goto unlock_release;
		}
	} else if (index == 1) {
		if (clk_hw->mux[0].shift >= 0) {
			// clear bypass
			reg_addr = clk_hw->base + clk_hw->mux[0].reg;
			reg = readl(reg_addr);
			reg &= ~(0x1 << clk_hw->mux[0].shift);
			writel(reg, reg_addr);

			if (clk_hw->mux[1].shift >= 0) {
				// set clk_sel to DIV_1
				reg_addr = clk_hw->base + clk_hw->mux[1].reg;
				reg = readl(reg_addr);
				reg &= ~(1 << clk_hw->mux[1].shift);
				writel(reg, reg_addr);
				goto unlock_release;
			} else {
				index--;
			}
		} else if (clk_hw->mux[1].shift >= 0) {
			// set clk_sel to DIV_0
			reg_addr = clk_hw->base + clk_hw->mux[1].reg;
			reg = readl(reg_addr);
			reg |= 1 << clk_hw->mux[1].shift;
			writel(reg, reg_addr);
			index--;
		}
	} else {
		if (clk_hw->mux[0].shift >= 0) {
			// clear bypass
			reg_addr = clk_hw->base + clk_hw->mux[0].reg;
			reg = readl(reg_addr);
			reg &= ~(0x1 << clk_hw->mux[0].shift);
			writel(reg, reg_addr);
			index--;
		}

		if (clk_hw->mux[1].shift >= 0) {
			// set clk_sel to DIV_0
			reg_addr = clk_hw->base + clk_hw->mux[1].reg;
			reg = readl(reg_addr);
			reg |= 1 << clk_hw->mux[1].shift;
			writel(reg, reg_addr);
			index--;
		}
	}

	if (index < 0) {
		pr_err("index is negative(%d)\n", index);
		goto unlock_release;
	}

	// set src_sel reg
	reg_addr = clk_hw->base + clk_hw->mux[2].reg;
	reg = readl(reg_addr);
	reg &= ~(0x3 << clk_hw->mux[2].shift); // clear bits
	reg |= (index & 0x3) << clk_hw->mux[2].shift; //set bits
	writel(reg, reg_addr);

unlock_release:
	if (clk_hw->lock)
		spin_unlock_irqrestore(clk_hw->lock, flags);
	else
		__release(clk_hw->lock);

	return 0;
}

static const struct clk_ops cv184x_clk_ops = {
	// gate
	.enable = cv184x_clk_gate_enable,
	.disable = cv184x_clk_gate_disable,
	.is_enabled = cv184x_clk_gate_is_enabled,

	// div
	.recalc_rate = cv184x_clk_div_recalc_rate,
	.round_rate = cv184x_clk_div_round_rate,
	.determine_rate = cv184x_clk_div_determine_rate,
	.set_rate = cv184x_clk_div_set_rate,

	//mux
	.get_parent = cv184x_clk_mux_get_parent,
	.set_parent = cv184x_clk_mux_set_parent,
};

static struct clk_hw *cv184x_register_clk(struct cv184x_hw_clock *cv184x_clk,
					  void __iomem *sys_base)
{
	struct clk_hw *hw;
	struct clk_init_data init;
	int err;

	cv184x_clk->gate.flags = 0;
	cv184x_clk->div[0].flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
				 CLK_DIVIDER_ROUND_CLOSEST;
	cv184x_clk->div[1].flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO |
				 CLK_DIVIDER_ROUND_CLOSEST;
	cv184x_clk->mux[0].flags = 0; /* clk byp */
	cv184x_clk->mux[1].flags = 0; /* clk sel */
	cv184x_clk->mux[2].flags = 0; /* div_0 src_sel */
	cv184x_clk->base = sys_base;
	cv184x_clk->lock = &cv184x_clk_lock;

	if (cvi_clk_flags) {
		/* copy clk_init_data for modification */
		memcpy(&init, cv184x_clk->hw.init, sizeof(init));

		init.flags |= cvi_clk_flags;
		cv184x_clk->hw.init = &init;
	}

	hw = &cv184x_clk->hw;
	err = clk_hw_register(NULL, hw);
	if (err) {
		return ERR_PTR(err);
	}

	return hw;
}

static void cv184x_unregister_clk(struct clk_hw *hw)
{
	struct cv184x_hw_clock *cv184x_clk = to_cv184x_clk(hw);

	clk_hw_unregister(hw);
	kfree(cv184x_clk);
}
static int cv184x_register_clks(struct cv184x_hw_clock *clks,
				int num_clks,
				struct cv184x_clock_data *data)
{
	struct clk_hw *hw;
	void __iomem *sys_base = data->base;
	unsigned int i;

	for (i = 0; i < num_clks; i++) {
		struct cv184x_hw_clock *cv184x_clk = &clks[i];

		hw = cv184x_register_clk(cv184x_clk, sys_base);

		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, cv184x_clk->name);
			goto err_clk;
		}
		data->hw_data.hws[clks[i].id] = hw;
		clk_hw_register_clkdev(hw, cv184x_clk->name, NULL);
	}

	return 0;

err_clk:
	while (i--)
		cv184x_unregister_clk(data->hw_data.hws[clks[i].id]);

	return PTR_ERR(hw);
}

static const struct of_device_id cvi_clk_match_ids_tables[] = {
	{
		.compatible = "cvitek,cv184x-clk",
	},
	{}
};

#ifdef CONFIG_PM_SLEEP
static int cv184x_clk_suspend(void)
{
	memcpy_fromio(clk_data->clken_saved_regs,
		      clk_data->base + REG_CLK_EN_START,
		      REG_CLK_EN_NUM * 4);

	memcpy_fromio(clk_data->clksel_saved_regs,
		      clk_data->base + REG_CLK_SEL_START,
		      REG_CLK_SEL_NUM * 4);

	memcpy_fromio(clk_data->clkbyp_saved_regs,
		      clk_data->base + REG_CLK_BYP_START,
		      REG_CLK_BYP_NUM * 4);

	memcpy_fromio(clk_data->clkdiv_saved_regs,
		      clk_data->base + REG_CLK_DIV_START,
		      REG_CLK_DIV_NUM * 4);

	memcpy_fromio(clk_data->g2_clkdiv_saved_regs,
		      clk_data->base + REG_CLK_G2_DIV_START,
		      REG_CLK_G2_DIV_NUM * 4);

	memcpy_fromio(clk_data->pll_g2_csr_saved_regs,
		      clk_data->base + REG_PLL_G2_CSR_START,
		      REG_PLL_G2_CSR_NUM * 4);

	memcpy_fromio(clk_data->pll_g6_csr_saved_regs,
		      clk_data->base + REG_PLL_G6_CSR_START,
		      REG_PLL_G6_CSR_NUM * 4);

	clk_data->a0pll_ssc_syn_set_saved_reg =
		readl(clk_data->base + REG_APLL_SSC_SYN_SET);

	clk_data->disppll_ssc_syn_set_saved_reg =
		readl(clk_data->base + REG_DISPPLL_SSC_SYN_SET);

	clk_data->cam0pll_ssc_syn_set_saved_reg =
		readl(clk_data->base + REG_CAM0PLL_SSC_SYN_SET);

	clk_data->cam1pll_ssc_syn_set_saved_reg =
		readl(clk_data->base + REG_CAM1PLL_SSC_SYN_SET);

	return 0;
}

static void cv184x_clk_resume(void)
{
	uint32_t regval;

	/* switch clock to xtal */
	writel(0xffffffff, clk_data->base + REG_CLK_BYP_0);
	writel(0xffffffff, clk_data->base + REG_CLK_BYP_1);

	clk_data->clken_saved_regs[0] |= 0x10;
	memcpy_toio(clk_data->base + REG_CLK_EN_START,
		    clk_data->clken_saved_regs,
		    REG_CLK_EN_NUM * 4);

	memcpy_toio(clk_data->base + REG_CLK_SEL_START,
		    clk_data->clksel_saved_regs,
		    REG_CLK_SEL_NUM * 4);

	memcpy_toio(clk_data->base + REG_CLK_DIV_START,
		    clk_data->clkdiv_saved_regs,
		    REG_CLK_DIV_NUM * 4);

	memcpy_toio(clk_data->base + REG_CLK_G2_DIV_START,
		    clk_data->g2_clkdiv_saved_regs,
		    REG_CLK_G2_DIV_NUM * 4);

	// memcpy_toio(clk_data->base + REG_PLL_G6_CSR_START,
	// 	    clk_data->pll_g6_csr_saved_regs,
	// 	    REG_PLL_G6_CSR_NUM * 4);

	/* wait for pll setting updated */
	// while (readl(clk_data->base + REG_PLL_G6_STATUS) & 0x7) {
	// }

	/* A0PLL */
	if (clk_data->a0pll_ssc_syn_set_saved_reg !=
	    readl(clk_data->base + REG_APLL_SSC_SYN_SET)) {
		pr_debug("%s: update A0PLL\n", __func__);
		writel(clk_data->a0pll_ssc_syn_set_saved_reg,
		       clk_data->base + REG_APLL_SSC_SYN_SET);

		/* toggle software update */
		regval = readl(clk_data->base + REG_APLL_SSC_SYN_CTRL);
		regval ^= 1;
		writel(regval, clk_data->base + REG_APLL_SSC_SYN_CTRL);
	}

	/* DISPPLL */
	if (clk_data->disppll_ssc_syn_set_saved_reg !=
	    readl(clk_data->base + REG_DISPPLL_SSC_SYN_SET)) {
		pr_debug("%s: update DISPPLL\n", __func__);
		writel(clk_data->disppll_ssc_syn_set_saved_reg,
		       clk_data->base + REG_DISPPLL_SSC_SYN_SET);

		/* toggle software update */
		regval = readl(clk_data->base + REG_DISPPLL_SSC_SYN_CTRL);
		regval ^= 1;
		writel(regval, clk_data->base + REG_DISPPLL_SSC_SYN_CTRL);
	}

	/* CAM0PLL */
	if (clk_data->cam0pll_ssc_syn_set_saved_reg !=
	    readl(clk_data->base + REG_CAM0PLL_SSC_SYN_SET)) {
		pr_debug("%s: update CAM0PLL\n", __func__);
		writel(clk_data->cam0pll_ssc_syn_set_saved_reg,
		       clk_data->base + REG_CAM0PLL_SSC_SYN_SET);

		/* toggle software update */
		regval = readl(clk_data->base + REG_CAM0PLL_SSC_SYN_CTRL);
		regval ^= 1;
		writel(regval, clk_data->base + REG_CAM0PLL_SSC_SYN_CTRL);
	}

	/* CAM1PLL */
	if (clk_data->cam1pll_ssc_syn_set_saved_reg !=
	    readl(clk_data->base + REG_CAM1PLL_SSC_SYN_SET)) {
		pr_debug("%s: update CAM1PLL\n", __func__);
		writel(clk_data->cam1pll_ssc_syn_set_saved_reg,
		       clk_data->base + REG_CAM1PLL_SSC_SYN_SET);

		/* toggle software update */
		regval = readl(clk_data->base + REG_CAM1PLL_SSC_SYN_CTRL);
		regval ^= 1;
		writel(regval, clk_data->base + REG_CAM1PLL_SSC_SYN_CTRL);
	}

	memcpy_toio(clk_data->base + REG_PLL_G2_CSR_START,
		    clk_data->pll_g2_csr_saved_regs,
		    REG_PLL_G2_CSR_NUM * 4);

	/* wait for pll setting updated */
	while (readl(clk_data->base + REG_PLL_G2_STATUS) & 0x1F) {
	}

	memcpy_toio(clk_data->base + REG_CLK_BYP_START,
		    clk_data->clkbyp_saved_regs,
		    REG_CLK_BYP_NUM * 4);
}

static struct syscore_ops cv184x_clk_syscore_ops = {
	.suspend = cv184x_clk_suspend,
	.resume = cv184x_clk_resume,
};
#endif /* CONFIG_PM_SLEEP */

static void __init cvi_clk_init(struct device_node *node)
{
	int num_clks;
	int i;
	int ret = 0;
	int of_num_clks;
	struct clk *clk;

	of_num_clks = of_clk_get_parent_count(node);
	for (i = 0; i < of_num_clks; i++) {
		clk = of_clk_get(node, i);
		clk_register_clkdev(clk, __clk_get_name(clk), NULL);
		clk_put(clk);
	}

	num_clks = ARRAY_SIZE(cv184x_pll_clks) +
		   ARRAY_SIZE(cv184x_clks);

	clk_data = kzalloc(sizeof(struct cv184x_clock_data) +
			   sizeof(struct clk_hw) * num_clks,
			   GFP_KERNEL);
	if (!clk_data) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < num_clks; i++)
		clk_data->hw_data.hws[i] = ERR_PTR(-ENOENT);

	clk_data->hw_data.num = num_clks;

	clk_data->lock = &cv184x_clk_lock;

	clk_data->base = of_iomap(node, 0);
	if (!clk_data->base) {
		pr_err("Failed to map address range for cvitek,cv184x-clk node\n");
		return;
	}

	cv184x_clk_register_plls(cv184x_pll_clks,
			       ARRAY_SIZE(cv184x_pll_clks),
			       clk_data);

	cv184x_register_clks(cv184x_clks,
			   ARRAY_SIZE(cv184x_clks),
			   clk_data);


	/* register clk-provider */
	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, &clk_data->hw_data);
	if (ret)
		pr_err("Unable to add hw clk provider\n");

	/* force enable clocks */
	// clk_prepare_enable(clk_data->hw_data.hws[CV184X_CLK_DSI_MAC_VIP]->clk);
	// clk_prepare_enable(clk_data->hw_data.hws[CV184X_CLK_DISP_VIP]->clk);
	// clk_prepare_enable(clk_data->hw_data.hws[CV184X_CLK_BT_VIP]->clk);
	// clk_prepare_enable(clk_data->hw_data.hws[CV184X_CLK_SC_TOP_VIP]->clk);

#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&cv184x_clk_syscore_ops);
#endif

	if (!ret)
		return;

out:
	pr_err("%s failed error number %d\n", __func__, ret);
}
CLK_OF_DECLARE(cvi_clk, "cvitek,cv184x-clk", cvi_clk_init);

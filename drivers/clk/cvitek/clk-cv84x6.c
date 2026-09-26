// SPDX-License-Identifier: GPL-2.0+

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/syscore_ops.h>

#include <dt-bindings/clock/cv84x6-clock.h>

#include "clk-common.h"
#include "clk-composite.h"
#include "clk-pll.h"

#define REG_PLL_STATUS	0x001c

#define REG_FPLL0_CTRL	0x0200
#define REG_FPLL1_CTRL	0x0204
#define REG_FPLL2_CTRL	0x0208
#define REG_FPLL3_CTRL	0x020c
#define REG_FPLL4_CTRL	0x0210
#define REG_FPLL5_CTRL	0x0214
#define REG_FPLL6_CTRL	0x0218
#define REG_FPLL7_CTRL	0x021c
#define REG_MPLL0_CTRL	0x0220
#define REG_MPLL1_CTRL	0x0224
#define REG_MPLL2_CTRL	0x0228
#define REG_MPLL3_CTRL	0x022c
#define REG_MPLL4_CTRL	0x0230
#define REG_FPLL8_CTRL	0x0234
#define REG_FPLL9_CTRL	0x0238
#define REG_MPLL5_CTRL	0x023c
#define REG_MPLL6_CTRL	0x0240
#define REG_MPLL7_CTRL	0x0244
#define REG_DPLL0_CTRL_0	0x0540
#define REG_DPLL0_CTRL_1	0x0544
#define REG_DPLL0_CTRL_2	0x0548
#define REG_DPLL0_CTRL_3	0x054c
#define REG_DPLL0_CTRL_4	0x0550
#define REG_DPLL1_CTRL_0	0x0560
#define REG_DPLL1_CTRL_1	0x0564
#define REG_DPLL1_CTRL_2	0x0568
#define REG_DPLL1_CTRL_3	0x056c
#define REG_DPLL1_CTRL_4	0x0570
#define REG_UPLL0_CTRL_0	0x0580
#define REG_UPLL0_CTRL_1	0x0584
#define REG_UPLL0_CTRL_2	0x0588
#define REG_UPLL0_CTRL_3	0x058c
#define REG_UPLL0_CTRL_4	0x0590
#define REG_UPLL1_CTRL_0	0x05A0
#define REG_UPLL1_CTRL_1	0x05A4
#define REG_UPLL1_CTRL_2	0x05A8
#define REG_UPLL1_CTRL_3	0x05ac
#define REG_UPLL1_CTRL_4	0x05B0
#define REG_HPLL0_CTRL_0	0x05C0
#define REG_HPLL0_CTRL_1	0x05C4
#define REG_HPLL0_CTRL_2	0x05C8
#define REG_HPLL0_CTRL_3	0x05cc
#define REG_HPLL0_CTRL_4	0x05D0

#define REG_A24K_CTRL_0	0x0280
#define REG_A24K_CTRL_1	0x0284
#define REG_A24K_CTRL_2	0x0288

#define REG_MPLL4_SSC_0	0x0290
#define REG_MPLL4_SSC_1	0x0294
#define REG_MPLL4_SSC_2	0x0298
#define REG_MPLL4_SSC_3	0x029c
#define REG_FPLL5_SSC_0	0x02A0
#define REG_FPLL5_SSC_1	0x02A4
#define REG_FPLL5_SSC_2	0x02A8
#define REG_FPLL5_SSC_3	0x02ac
#define REG_FPLL6_SSC_0	0x02B0
#define REG_FPLL6_SSC_1	0x02B4
#define REG_FPLL6_SSC_2	0x02B8
#define REG_FPLL6_SSC_3	0x02bc
#define REG_AP_CLK_EN		0x0354
#define REG_HS_CLK_EN_0		0x0380
#define REG_HS_CLK_EN_1		0x0384
#define REG_PERI_CLK_EN		0x0390
#define REG_VO_CLK_EN		0x03f4

#define REG_CLK_EN_0		0x2000
#define REG_CLK_EN_1		0x2004
#define REG_CLK_EN_2		0x2008
#define REG_CLK_EN_3		0x200c
#define REG_CLK_EN_4		0x2010

#define REG_CLK_SEL_0		0x2020
#define REG_CLK_SEL_1		0x2024

#define REG_DIV_CLK_TOP_FAB_1P2G	0x2040
#define REG_DIV_CLK_TOP_FAB_500M	0x2044
#define REG_DIV_CLK_TOP_FAB_300M	0x2048
#define REG_DIV_CLK_TOP_FAB_200M	0x204c
#define REG_DIV_CLK_TOP_FAB_100M	0x2050
#define REG_DIV_CLK_TOP_50M	0x2054
#define REG_DIV_CLK_TOP_25M	0x2058
#define REG_DIV_CLK_SPACC	0x205c
#define REG_DIV_CLK_PKA	0x2060
#define REG_DIV_CLK_AP_CA55_0	0x2064
#define REG_DIV_CLK_AP_CA55_1	0x2068
#define REG_DIV_CLK_AP_C906	0x206c
#define REG_DIV_CLK_AP_SCLK_0	0x2070
#define REG_DIV_CLK_AP_SCLK_1	0x2074
#define REG_DIV_CLK_AP_PCLK	0x2078
#define REG_DIV_CLK_AP_GIC	0x207c
#define REG_DIV_CLK_AP_PERIPH	0x2080
#define REG_DIV_CLK_AP_DBG	0x2084
#define REG_DIV_CLK_AP_SC	0x2088
#define REG_DIV_CLK_TPU_SYS_0	0x208c
#define REG_DIV_CLK_TPU_SYS_1	0x2090
#define REG_DIV_CLK_TPU_MCU	0x2094
#define REG_DIV_CLK_TPU_IP	0x2098
#define REG_DIV_CLK_TPU_SC	0x209c
#define REG_DIV_CLK_DDR0_0	0x20a0
#define REG_DIV_CLK_DDR0_1	0x20a4
#define REG_DIV_CLK_DDR1_0	0x20a8
#define REG_DIV_CLK_DDR1_1	0x20ac
#define REG_DIV_CLK_DDR2_0	0x20b0
#define REG_DIV_CLK_DDR2_1	0x20b4
#define REG_DIV_CLK_DDR3_0	0x20b8
#define REG_DIV_CLK_DDR3_1	0x20bc
#define REG_DIV_CLK_DDR0_PLL_BYPASS_0	0x20c0
#define REG_DIV_CLK_DDR0_PLL_BYPASS_1	0x20c4
#define REG_DIV_CLK_DDR1_PLL_BYPASS_0	0x20c8
#define REG_DIV_CLK_DDR1_PLL_BYPASS_1	0x20cc
#define REG_DIV_CLK_DDR2_PLL_BYPASS_0	0x20d0
#define REG_DIV_CLK_DDR2_PLL_BYPASS_1	0x20d4
#define REG_DIV_CLK_DDR3_PLL_BYPASS_0	0x20d8
#define REG_DIV_CLK_DDR3_PLL_BYPASS_1	0x20dc
#define REG_DIV_CLK_PCIE_DFT_PHY	0x20e0
#define REG_DIV_CLK_PCIE_SATA0_OOB	0x20e4
#define REG_DIV_CLK_PCIE_SATA1_OOB	0x20e8
#define REG_DIV_CLK_PCIE_SATA0_PMA	0x20ec
#define REG_DIV_CLK_PCIE_SATA1_PMA	0x20f0
#define REG_DIV_CLK_VO_DISP_0	0x20f4
#define REG_DIV_CLK_VO_DISP_1	0x20f8
#define REG_DIV_CLK_VO_SYS0	0x20fc
#define REG_DIV_CLK_VO_SYS1	0x2100
#define REG_DIV_CLK_VO_SYS2	0x2104
#define REG_DIV_CLK_VO_SYS3	0x2108
#define REG_DIV_CLK_VO_AXI	0x210c
#define REG_DIV_CLK_HDMI_SFR	0x2110
#define REG_DIV_CLK_HDMI_ICP	0x2114
#define REG_DIV_CLK_VD0_SRC0_0	0x2118
#define REG_DIV_CLK_VD0_SRC0_1	0x211c
#define REG_DIV_CLK_VD0_SRC1_0	0x2120
#define REG_DIV_CLK_VD0_SRC1_1	0x2124
#define REG_DIV_CLK_VD0_SRC2_0	0x2128
#define REG_DIV_CLK_VD0_SRC2_1	0x212c
#define REG_DIV_CLK_VD0_AXI0_0	0x2130
#define REG_DIV_CLK_VD0_AXI0_1	0x2134
#define REG_DIV_CLK_VD1_SRC0_0	0x2138
#define REG_DIV_CLK_VD1_SRC0_1	0x213c
#define REG_DIV_CLK_VD1_SRC1_0	0x2140
#define REG_DIV_CLK_VD1_SRC1_1	0x2144
#define REG_DIV_CLK_VD1_SRC2_0	0x2148
#define REG_DIV_CLK_VD1_SRC2_1	0x214c
#define REG_DIV_CLK_VD1_AXI0_0	0x2150
#define REG_DIV_CLK_VD1_AXI0_1	0x2154
#define REG_DIV_CLK_VD2_SRC0_0	0x2158
#define REG_DIV_CLK_VD2_SRC0_1	0x215c
#define REG_DIV_CLK_VD2_SRC1_0	0x2160
#define REG_DIV_CLK_VD2_SRC1_1	0x2164
#define REG_DIV_CLK_VD2_SRC2_0	0x2168
#define REG_DIV_CLK_VD2_SRC2_1	0x216c
#define REG_DIV_CLK_VD2_AXI0_0	0x2170
#define REG_DIV_CLK_VD2_AXI0_1	0x2174
#define REG_DIV_CLK_VD3_SRC0_0	0x2178
#define REG_DIV_CLK_VD3_SRC0_1	0x217c
#define REG_DIV_CLK_VD3_SRC1_0	0x2180
#define REG_DIV_CLK_VD3_SRC1_1	0x2184
#define REG_DIV_CLK_VD3_SRC2_0	0x2188
#define REG_DIV_CLK_VD3_SRC2_1	0x218c
#define REG_DIV_CLK_VD3_AXI0_0	0x2190
#define REG_DIV_CLK_VD3_AXI0_1	0x2194
#define REG_DIV_CLK_VE_SRC0_0	0x2198
#define REG_DIV_CLK_VE_SRC0_1	0x219c
#define REG_DIV_CLK_VE_SRC1_0	0x21a0
#define REG_DIV_CLK_VE_SRC1_1	0x21a4
#define REG_DIV_CLK_VE_SRC2_0	0x21a8
#define REG_DIV_CLK_VE_SRC2_1	0x21ac
#define REG_DIV_CLK_VE_AXI_0	0x21b0
#define REG_DIV_CLK_VE_AXI_1	0x21b4
#define REG_DIV_CLK_VE_SC	0x21b8
#define REG_DIV_CLK_EMMC	0x21bc
#define REG_DIV_CLK_EMMC_100K	0x21c0
#define REG_DIV_CLK_SD0	0x21c4
#define REG_DIV_CLK_SD0_100K	0x21c8
#define REG_DIV_CLK_SD1	0x21cc
#define REG_DIV_CLK_SD1_100K	0x21d0
#define REG_DIV_CLK_ETH0_TX	0x21d4
#define REG_DIV_CLK_ETH0_PTP_REF	0x21d8
#define REG_DIV_CLK_ETH0_REFCLKO	0x21dc
#define REG_DIV_CLK_ETH1_TX	0x21e0
#define REG_DIV_CLK_ETH1_PTP_REF	0x21e4
#define REG_DIV_CLK_ETH1_REFCLKO	0x21e8
#define REG_DIV_CLK_SPI_NAND	0x21ec
#define REG_DIV_CLK_DW_SPI	0x21f0
#define REG_DIV_CLK_UART0	0x21f4
#define REG_DIV_CLK_UART1	0x21f8
#define REG_DIV_CLK_UART2	0x21fc
#define REG_DIV_CLK_UART3	0x2200
#define REG_DIV_CLK_UART4	0x2204
#define REG_DIV_CLK_UART5	0x2208
#define REG_DIV_CLK_UART6	0x220c
#define REG_DIV_CLK_UART7	0x2210
#define REG_DIV_CLK_I2C	0x2214
#define REG_DIV_CLK_CAN	0x2218
#define REG_DIV_CLK_AUD0_0	0x221c
#define REG_DIV_CLK_AUD0_1	0x2220
#define REG_DIV_CLK_AUD1_0	0x2224
#define REG_DIV_CLK_AUD1_1	0x2228
#define REG_DIV_CLK_AUD2_0	0x222c
#define REG_DIV_CLK_AUD2_1	0x2230
#define REG_DIV_CLK_AUD3_0	0x2234
#define REG_DIV_CLK_AUD3_1	0x2238
#define REG_DIV_CLK_AUD4_0	0x223c
#define REG_DIV_CLK_AUD4_1	0x2240
#define REG_DIV_CLK_AUD5_0	0x2244
#define REG_DIV_CLK_AUD5_1	0x2248
#define REG_DIV_CLK_AUD6_0	0x224c
#define REG_DIV_CLK_AUD6_1	0x2250
#define REG_DIV_CLK_AUDSRC_0	0x2254
#define REG_DIV_CLK_AUDSRC_1	0x2258
#define REG_DIV_CLK_AUD_HDMI	0x225c
#define REG_DIV_CLK_PWM	0x2260
#define REG_DIV_CLK_OTP	0x2264
#define REG_DIV_CLK_WDT	0x2268
#define REG_DIV_CLK_GPIO_DB	0x226c
#define REG_DIV_CLK_USB_SUSPEND	0x2270
#define REG_DIV_CLK_USB0_REF_100M	0x2274
#define REG_DIV_CLK_USB1_REF_100M	0x2278
#define REG_DIV_CLK_USB0_CLKCORE	0x227c
#define REG_DIV_CLK_USB1_CLKCORE	0x2280
#define REG_DIV_CLK_USB_REF	0x2284
#define REG_DIV_CLK_USB_MBIST_100M	0x2288
#define REG_DIV_CLK_USB_DFT_800M	0x228c
#define REG_DIV_CLK_USB_DFT_600M	0x2290
#define REG_DIV_CLK_USB_DFT_500M	0x2294
#define REG_DIV_CLK_USB_DFT_450M	0x2298
#define REG_DIV_CLK_HDMI_REF_100M	0x229c


static struct cv_clock_data *cv84x6_clk_data;

static const char * const cv84x6_pll_parent[] = { "osc" };
static const char * const cv84x6_ssc_parent[] = {
	"osc",
	"clk_fpll0",
	"clk_fpll1",
	"clk_fpll2",
};

static struct cv_pll cv84x6_plls[] = {
	CV_INTN_PLL(CV84X6_CLK_FPLL0, "clk_fpll0", cv84x6_pll_parent,
		    REG_FPLL0_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL1, "clk_fpll1", cv84x6_pll_parent,
		    REG_FPLL1_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL2, "clk_fpll2", cv84x6_pll_parent,
		    REG_FPLL2_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL3, "clk_fpll3", cv84x6_pll_parent,
		    REG_FPLL3_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL4, "clk_fpll4", cv84x6_pll_parent,
		    REG_FPLL4_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL5, "clk_fpll5", cv84x6_ssc_parent,
		    REG_FPLL5_CTRL, REG_FPLL5_SSC_0, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL6, "clk_fpll6", cv84x6_ssc_parent,
		    REG_FPLL6_CTRL, REG_FPLL6_SSC_0, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL7, "clk_fpll7", cv84x6_pll_parent,
		    REG_FPLL7_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL8, "clk_fpll8", cv84x6_pll_parent,
		    REG_FPLL8_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_FPLL9, "clk_fpll9", cv84x6_pll_parent,
		    REG_FPLL9_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL0, "clk_mpll0", cv84x6_pll_parent,
		    REG_MPLL0_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL1, "clk_mpll1", cv84x6_pll_parent,
		    REG_MPLL1_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL2, "clk_mpll2", cv84x6_pll_parent,
		    REG_MPLL2_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL3, "clk_mpll3", cv84x6_pll_parent,
		    REG_MPLL3_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL4, "clk_mpll4", cv84x6_pll_parent,
		    REG_MPLL4_CTRL, REG_MPLL4_SSC_0, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL5, "clk_mpll5", cv84x6_pll_parent,
		    REG_MPLL5_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL6, "clk_mpll6", cv84x6_pll_parent,
		    REG_MPLL6_CTRL, REG_UNEXIST, 0),
	CV_INTN_PLL(CV84X6_CLK_MPLL7, "clk_mpll7", cv84x6_pll_parent,
		    REG_MPLL7_CTRL, REG_UNEXIST, 0),
	CV_FRAC_PLL(CV84X6_CLK_DPLL0, "clk_dpll0", cv84x6_pll_parent,
		    REG_DPLL0_CTRL_0, 0),
	CV_FRAC_PLL(CV84X6_CLK_DPLL1, "clk_dpll1", cv84x6_pll_parent,
		    REG_DPLL1_CTRL_0, 0),
	CV_FRAC_PLL(CV84X6_CLK_UPLL0, "clk_upll0", cv84x6_pll_parent,
		    REG_UPLL0_CTRL_0, 0),
	CV_FRAC_PLL(CV84X6_CLK_UPLL1, "clk_upll1", cv84x6_pll_parent,
		    REG_UPLL1_CTRL_0, 0),
	CV_FRAC_PLL(CV84X6_CLK_HPLL0, "clk_hpll", cv84x6_pll_parent,
		    REG_HPLL0_CTRL_0, 0),
	CV_DIV_PLL(CV84X6_CLK_A24KPLL, "clk_a24k_pll",
		   ((const char *[]) { "clk_fpll1" }), REG_A24K_CTRL_0, 0),
};

static struct cv_composite_clock cv84x6_clks[] = {
	CV_COMPOSITE_CLK(CV84X6_CLK_TOP_FAB_1P2G, "clk_top_fab_1p2g",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_0, 0,
			REG_DIV_CLK_TOP_FAB_1P2G, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TOP_FAB_500M, "clk_top_fab_500m",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_0, 1,
			REG_DIV_CLK_TOP_FAB_500M, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TOP_FAB_300M, "clk_top_fab_300m",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_0, 2,
			REG_DIV_CLK_TOP_FAB_300M, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TOP_FAB_200M, "clk_top_fab_200m",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_0, 3,
			REG_DIV_CLK_TOP_FAB_200M, 16, 8, 6,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TOP_FAB_100M, "clk_top_fab_100m",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_0, 4,
			REG_DIV_CLK_TOP_FAB_100M, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_TOP_50M, "clk_top_50m",
	//		((const char *[]) { "clk_fpll1" }),
	//		REG_CLK_EN_0, 5,
	//		REG_DIV_CLK_TOP_50M, 16, 8, 48,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TOP_25M, "clk_top_25m",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_0, 6,
			REG_DIV_CLK_TOP_25M, 16, 8, 48,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SPACC, "clk_spacc",
			((const char *[]) { "clk_fpll9" }),
			REG_CLK_EN_0, 7,
			REG_DIV_CLK_SPACC, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PKA, "clk_pka",
			((const char *[]) { "clk_fpll8" }),
			REG_CLK_EN_0, 8,
			REG_DIV_CLK_PKA, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SE_FAB_500M, "clk_se_fab_500m",
			((const char *[]) { "clk_top_fab_500m" }),
			REG_CLK_EN_0, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SE_FAB_1P2G, "clk_se_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_0, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_RTC_SYS, "clk_rtc_sys",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_CLK_EN_0, 11,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_CA55, "clk_ap_ca55",
			((const char *[]) { "clk_mpll0", "clk_mpll1" }),
			REG_CLK_EN_0, 12,
			REG_DIV_CLK_AP_CA55_0, 16, 8, 1,
			REG_DIV_CLK_AP_CA55_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_C906, "clk_ap_c906",
			((const char *[]) { "clk_fpll8" }),
			REG_CLK_EN_0, 13,
			REG_DIV_CLK_AP_C906, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_SCLK, "clk_ap_sclk",
			((const char *[]) { "clk_mpll7", "clk_mpll1" }),
			REG_CLK_EN_0, 14,
			REG_DIV_CLK_AP_SCLK_0, 16, 8, 1,
			REG_DIV_CLK_AP_SCLK_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 1,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_PCLK, "clk_ap_pclk",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_0, 15,
			REG_DIV_CLK_AP_PCLK, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_GIC, "clk_ap_gic",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_0, 16,
			REG_DIV_CLK_AP_GIC, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_PERIPH, "clk_ap_periph",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_0, 17,
			REG_DIV_CLK_AP_PERIPH, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_DBG, "clk_ap_dbg",
			((const char *[]) { "clk_fpll3" }),
			REG_CLK_EN_0, 18,
			REG_DIV_CLK_AP_DBG, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_SC, "clk_ap_sc",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_0, 19,
			REG_DIV_CLK_AP_SC, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_FAB_500M, "clk_ap_fab_500m",
			((const char *[]) { "clk_top_fab_500m" }),
			REG_CLK_EN_0, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AP_FAB_1P2G, "clk_ap_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_0, 21,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TPU_SYS, "clk_tpu_sys",
			((const char *[]) { "clk_mpll1", "clk_mpll0" }),
			REG_CLK_EN_0, 22,
			REG_DIV_CLK_TPU_SYS_0, 16, 8, 1,
			REG_DIV_CLK_TPU_SYS_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 2,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TPU_MCU, "clk_tpu_mcu",
			((const char *[]) { "clk_fpll8" }),
			REG_CLK_EN_0, 23,
			REG_DIV_CLK_TPU_MCU, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TPU_IP, "clk_tpu_ip",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_0, 24,
			REG_DIV_CLK_TPU_IP, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TPU_SC, "clk_tpu_sc",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_0, 25,
			REG_DIV_CLK_TPU_SC, 16, 8, 24,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TPU_FAB_500M, "clk_tpu_fab_500m",
			((const char *[]) { "clk_top_fab_500m" }),
			REG_CLK_EN_0, 26,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TPU_FAB_1P2G, "clk_tpu_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_0, 27,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR0, "clk_ddr0",
			((const char *[]) { "clk_dpll0_0", "clk_mpll3" }),
			REG_CLK_EN_0, 28,
			REG_DIV_CLK_DDR0_0, 16, 8, 1,
			REG_DIV_CLK_DDR0_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 3,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR1, "clk_ddr1",
			((const char *[]) { "clk_dpll0_0", "clk_mpll3" }),
			REG_CLK_EN_0, 29,
			REG_DIV_CLK_DDR1_0, 16, 8, 1,
			REG_DIV_CLK_DDR1_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 4,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR2, "clk_ddr2",
			((const char *[]) { "clk_dpll1_0", "clk_mpll3" }),
			REG_CLK_EN_0, 30,
			REG_DIV_CLK_DDR2_0, 16, 8, 1,
			REG_DIV_CLK_DDR2_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 5,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR3, "clk_ddr3",
			((const char *[]) { "clk_dpll1_0", "clk_mpll3" }),
			REG_CLK_EN_0, 31,
			REG_DIV_CLK_DDR3_0, 16, 8, 1,
			REG_DIV_CLK_DDR3_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 6,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR0_PLL_BYPASS, "clk_ddr0_pllbyp",
			((const char *[]) { "clk_fpll7", "clk_mpll3" }),
			REG_CLK_EN_1, 0,
			REG_DIV_CLK_DDR0_PLL_BYPASS_0, 16, 8, 1,
			REG_DIV_CLK_DDR0_PLL_BYPASS_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 7,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR1_PLL_BYPASS, "clk_ddr1_pllbyp",
			((const char *[]) { "clk_fpll7", "clk_mpll3" }),
			REG_CLK_EN_1, 1,
			REG_DIV_CLK_DDR1_PLL_BYPASS_0, 16, 8, 1,
			REG_DIV_CLK_DDR1_PLL_BYPASS_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 8,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR2_PLL_BYPASS, "clk_ddr2_pllbyp",
			((const char *[]) { "clk_fpll7", "clk_mpll3" }),
			REG_CLK_EN_1, 2,
			REG_DIV_CLK_DDR2_PLL_BYPASS_0, 16, 8, 1,
			REG_DIV_CLK_DDR2_PLL_BYPASS_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 9,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR3_PLL_BYPASS, "clk_ddr3_pllbyp",
			((const char *[]) { "clk_fpll7", "clk_mpll3" }),
			REG_CLK_EN_1, 3,
			REG_DIV_CLK_DDR3_PLL_BYPASS_0, 16, 8, 1,
			REG_DIV_CLK_DDR3_PLL_BYPASS_1, 16, 8, 1,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 10,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR0_FAB_1P2G, "clk_ddr0_fab_1",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_1, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR1_FAB_1P2G, "clk_ddr1_fab_1",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_1, 5,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR2_FAB_1P2G, "clk_ddr2_fab_1",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_1, 6,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR3_FAB_1P2G, "clk_ddr3_fab_1",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_1, 7,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR0_FAB_200M, "clk_ddr0_fab_0",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_1, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR1_FAB_200M, "clk_ddr1_fab_0",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_1, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR2_FAB_200M, "clk_ddr2_fab_0",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_1, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DDR3_FAB_200M, "clk_ddr3_fab_0",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_1, 11,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_DFT_PHY, "clk_pcie_dft_phy",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_1, 12,
			REG_DIV_CLK_PCIE_DFT_PHY, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_SATA0_OOB, "clk_sata0_oob",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 13,
			REG_DIV_CLK_PCIE_SATA0_OOB, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_SATA1_OOB, "clk_sata1_oob",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 14,
			REG_DIV_CLK_PCIE_SATA1_OOB, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_SATA0_PMA, "clk_sata0_pma",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 15,
			REG_DIV_CLK_PCIE_SATA0_PMA, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_SATA1_PMA, "clk_sata1_pma",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 16,
			REG_DIV_CLK_PCIE_SATA1_PMA, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_FAB_1P2G, "clk_ss_fab_2",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_1, 17,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_FAB_500M, "clk_ss_fab_1",
			((const char *[]) { "clk_top_fab_500m" }),
			REG_CLK_EN_1, 18,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PCIE_FAB_200M, "clk_ss_fab_0",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_1, 19,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_DISP, "clk_vo_disp",
			((const char *[]) { "clk_mpll4", "clk_mpll3" }),
			REG_CLK_EN_1, 20,
			REG_DIV_CLK_VO_DISP_0, 16, 8, 2,
			REG_DIV_CLK_VO_DISP_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 11,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_SYS0, "clk_vo_sys0",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 21,
			REG_DIV_CLK_VO_SYS0, 16, 8, 30,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_SYS1, "clk_vo_sys1",
			((const char *[]) { "clk_fpll4" }),
			REG_CLK_EN_1, 22,
			REG_DIV_CLK_VO_SYS1, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_SYS2, "clk_vo_sys2",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 23,
			REG_DIV_CLK_VO_SYS2, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_SYS3, "clk_vo_sys3",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_1, 24,
			REG_DIV_CLK_VO_SYS3, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_AXI, "clk_vo_axi",
			((const char *[]) { "clk_fpll4" }),
			REG_CLK_EN_1, 25,
			REG_DIV_CLK_VO_AXI, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_HDMI_SFR, "clk_hdmi_sfr",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 26,
			REG_DIV_CLK_HDMI_SFR, 16, 8, 48,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_HDMI_ICP, "clk_hdmi_icp",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_1, 27,
			REG_DIV_CLK_HDMI_ICP, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_X2P, "clk_vo_x2p",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_1, 28,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VO_FAB_1P2G, "clk_vo_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_1, 29,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD0_SRC0, "clk_vd0_src0",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_1, 30,
			REG_DIV_CLK_VD0_SRC0_0, 16, 8, 2,
			REG_DIV_CLK_VD0_SRC0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 12,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD0_SRC1, "clk_vd0_src1",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_1, 31,
			REG_DIV_CLK_VD0_SRC1_0, 16, 8, 2,
			REG_DIV_CLK_VD0_SRC1_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 13,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD0_SRC2, "clk_vd0_src2",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_2, 0,
			REG_DIV_CLK_VD0_SRC2_0, 16, 8, 2,
			REG_DIV_CLK_VD0_SRC2_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 14,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD0_AXI0, "clk_vd0_axi0",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_2, 1,
			REG_DIV_CLK_VD0_AXI0_0, 16, 8, 2,
			REG_DIV_CLK_VD0_AXI0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 15,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD0_FAB_200M, "clk_vd0_fab_200m",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_2, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD0_FAB_1P2G, "clk_vd0_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_2, 3,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD1_SRC0, "clk_vd1_src0",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_2, 4,
			REG_DIV_CLK_VD1_SRC0_0, 16, 8, 2,
			REG_DIV_CLK_VD1_SRC0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 16,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD1_SRC1, "clk_vd1_src1",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_2, 5,
			REG_DIV_CLK_VD1_SRC1_0, 16, 8, 2,
			REG_DIV_CLK_VD1_SRC1_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 17,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD1_SRC2, "clk_vd1_src2",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_2, 6,
			REG_DIV_CLK_VD1_SRC2_0, 16, 8, 2,
			REG_DIV_CLK_VD1_SRC2_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 18,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD1_AXI0, "clk_vd1_axi0",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_2, 7,
			REG_DIV_CLK_VD1_AXI0_0, 16, 8, 2,
			REG_DIV_CLK_VD1_AXI0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 19,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD1_FAB_200M, "clk_vd1_fab_200m",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_2, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD1_FAB_1P2G, "clk_vd1_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_2, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD2_SRC0, "clk_vd2_src0",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_2, 10,
			REG_DIV_CLK_VD2_SRC0_0, 16, 8, 2,
			REG_DIV_CLK_VD2_SRC0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 20,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD2_SRC1, "clk_vd2_src1",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_2, 11,
			REG_DIV_CLK_VD2_SRC1_0, 16, 8, 2,
			REG_DIV_CLK_VD2_SRC1_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 21,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD2_SRC2, "clk_vd2_src2",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_2, 12,
			REG_DIV_CLK_VD2_SRC2_0, 16, 8, 2,
			REG_DIV_CLK_VD2_SRC2_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 22,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD2_AXI0, "clk_vd2_axi0",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_2, 13,
			REG_DIV_CLK_VD2_AXI0_0, 16, 8, 2,
			REG_DIV_CLK_VD2_AXI0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 23,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD2_FAB_200M, "clk_vd2_fab_200m",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_2, 14,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD2_FAB_1P2G, "clk_vd2_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_2, 15,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD3_SRC0, "clk_vd3_src0",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_2, 16,
			REG_DIV_CLK_VD3_SRC0_0, 16, 8, 2,
			REG_DIV_CLK_VD3_SRC0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 24,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD3_SRC1, "clk_vd3_src1",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_2, 17,
			REG_DIV_CLK_VD3_SRC1_0, 16, 8, 2,
			REG_DIV_CLK_VD3_SRC1_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 25,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD3_SRC2, "clk_vd3_src2",
			((const char *[]) { "clk_mpll2", "clk_mpll3" }),
			REG_CLK_EN_2, 18,
			REG_DIV_CLK_VD3_SRC2_0, 16, 8, 2,
			REG_DIV_CLK_VD3_SRC2_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 26,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD3_AXI0, "clk_vd3_axi0",
			((const char *[]) { "clk_mpll3", "clk_mpll2" }),
			REG_CLK_EN_2, 19,
			REG_DIV_CLK_VD3_AXI0_0, 16, 8, 2,
			REG_DIV_CLK_VD3_AXI0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 27,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD3_FAB_200M, "clk_vd3_fab_200m",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_2, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VD3_FAB_1P2G, "clk_vd3_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_2, 21,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VE_SRC0, "clk_ve_src0",
			((const char *[]) { "clk_mpll5", "clk_mpll6" }),
			REG_CLK_EN_2, 22,
			REG_DIV_CLK_VE_SRC0_0, 16, 8, 2,
			REG_DIV_CLK_VE_SRC0_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 28,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VE_SRC1, "clk_ve_src1",
			((const char *[]) { "clk_mpll6", "clk_mpll5" }),
			REG_CLK_EN_2, 23,
			REG_DIV_CLK_VE_SRC1_0, 16, 8, 2,
			REG_DIV_CLK_VE_SRC1_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 29,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VE_SRC2, "clk_ve_src2",
			((const char *[]) { "clk_mpll5", "clk_mpll6" }),
			REG_CLK_EN_2, 24,
			REG_DIV_CLK_VE_SRC2_0, 16, 8, 2,
			REG_DIV_CLK_VE_SRC2_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 30,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VE_AXI, "clk_ve_axi",
			((const char *[]) { "clk_mpll6", "clk_mpll5" }),
			REG_CLK_EN_2, 25,
			REG_DIV_CLK_VE_AXI_0, 16, 8, 2,
			REG_DIV_CLK_VE_AXI_1, 16, 8, 2,
			REG_UNEXIST, 0,
			REG_CLK_SEL_0, 31,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VE_SC, "clk_ve_sc",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_2, 26,
			REG_DIV_CLK_VE_SC, 16, 8, 48,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VE_FAB_200M, "clk_ve_fab_200m",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_2, 27,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_VE_FAB_1P2G, "clk_ve_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_2, 28,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_HSPERI_SYS, "clk_hsperi_sys",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_CLK_EN_2, 29,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_EMMC, "clk_emmc",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_2, 30,
			REG_DIV_CLK_EMMC, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_EMMC_100K, "clk_emmc_100k",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_2, 31,
			REG_DIV_CLK_EMMC_100K, 16, 8, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SD0, "clk_sd0",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_3, 0,
			REG_DIV_CLK_SD0, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SD0_100K, "clk_sd0_100k",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_3, 1,
			REG_DIV_CLK_SD0_100K, 16, 8, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SD1, "clk_sd1",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_3, 2,
			REG_DIV_CLK_SD1, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SD1_100K, "clk_sd1_100k",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_3, 3,
			REG_DIV_CLK_SD1_100K, 16, 8, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_ETH0_TX, "clk_eth0_tx",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 4,
			REG_DIV_CLK_ETH0_TX, 16, 16, 16,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_ETH0_PTP_REF, "clk_eth0_ptp_ref",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 5,
			REG_DIV_CLK_ETH0_PTP_REF, 16, 8, 40,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_ETH0_REFCLKO, "clk_eth0_refclko",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 6,
			REG_DIV_CLK_ETH0_REFCLKO, 16, 8, 80,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_ETH1_TX, "clk_eth1_tx",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 7,
			REG_DIV_CLK_ETH1_TX, 16, 16, 16,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_ETH1_PTP_REF, "clk_eth1_ptp_ref",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 8,
			REG_DIV_CLK_ETH1_PTP_REF, 16, 8, 40,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_ETH1_REFCLKO, "clk_eth1_refclko",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 9,
			REG_DIV_CLK_ETH1_REFCLKO, 16, 8, 80,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SPI_NAND, "clk_spi_nand",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 10,
			REG_DIV_CLK_SPI_NAND, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DW_SPI, "clk_dw_spi",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 11,
			REG_DIV_CLK_DW_SPI, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART0, "clk_uart0",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 12,
			REG_DIV_CLK_UART0, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART1, "clk_uart1",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 13,
			REG_DIV_CLK_UART1, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART2, "clk_uart2",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 14,
			REG_DIV_CLK_UART2, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART3, "clk_uart3",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 15,
			REG_DIV_CLK_UART3, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART4, "clk_uart4",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 16,
			REG_DIV_CLK_UART4, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART5, "clk_uart5",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 17,
			REG_DIV_CLK_UART5, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART6, "clk_uart6",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 18,
			REG_DIV_CLK_UART6, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_UART7, "clk_uart7",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 19,
			REG_DIV_CLK_UART7, 16, 8, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C, "clk_i2c",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 20,
			REG_DIV_CLK_I2C, 16, 8, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_CAN, "clk_can",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_3, 21,
			REG_DIV_CLK_CAN, 16, 8, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD0, "clk_aud0",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 22,
			REG_DIV_CLK_AUD0_0, 16, 8, 17,
			REG_DIV_CLK_AUD0_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD1, "clk_aud1",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 23,
			REG_DIV_CLK_AUD1_0, 16, 8, 17,
			REG_DIV_CLK_AUD1_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 1,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD2, "clk_aud2",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 24,
			REG_DIV_CLK_AUD2_0, 16, 8, 17,
			REG_DIV_CLK_AUD2_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 2,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD3, "clk_aud3",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 25,
			REG_DIV_CLK_AUD3_0, 16, 8, 17,
			REG_DIV_CLK_AUD3_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 3,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD4, "clk_aud4",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 26,
			REG_DIV_CLK_AUD4_0, 16, 8, 17,
			REG_DIV_CLK_AUD4_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 4,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD5, "clk_aud5",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 27,
			REG_DIV_CLK_AUD5_0, 16, 8, 17,
			REG_DIV_CLK_AUD5_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 5,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD6, "clk_aud6",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 28,
			REG_DIV_CLK_AUD6_0, 16, 8, 17,
			REG_DIV_CLK_AUD6_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 6,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUDSRC, "clk_audsrc",
			((const char *[]) { "clk_fpll5", "clk_a24k_pll" }),
			REG_CLK_EN_3, 29,
			REG_DIV_CLK_AUDSRC_0, 16, 8, 17,
			REG_DIV_CLK_AUDSRC_1, 16, 8, 17,
			REG_UNEXIST, 0,
			REG_CLK_SEL_1, 7,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AUD_HDMI, "clk_aud_hdmi",
			((const char *[]) { "clk_fpll6" }),
			REG_CLK_EN_3, 30,
			REG_DIV_CLK_AUD_HDMI, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_PERI_SYS, "clk_top_fab_200m",
	//		((const char *[]) { "clk_top_fab_200m" }),
	//		REG_CLK_EN_3, 31,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_PWM, "clk_pwm",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_4, 0,
			REG_DIV_CLK_PWM, 16, 8, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_OTP, "clk_otp",
	//		((const char *[]) { "clk_fpll0" }),
	//		REG_CLK_EN_4, 1,
	//		REG_DIV_CLK_OTP, 16, 8, 80,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_WDT, "clk_wdt",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_4, 2,
			REG_DIV_CLK_WDT, 16, 16, 80,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_XTAL_MISC, "clk_xtal_misc",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_4, 3,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SARADC, "clk_saradc",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_4, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TEMPSEN, "clk_tempsen",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_4, 5,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_GPIO_DB, "clk_gpio_db",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_4, 6,
			REG_DIV_CLK_GPIO_DB, 16, 8, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_FAB_1P2G, "clk_usb_fab_1p2g",
			((const char *[]) { "clk_top_fab_1p2g" }),
			REG_CLK_EN_4, 7,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_FAB_500M, "clk_usb_fab_500m",
			((const char *[]) { "clk_top_fab_500m" }),
			REG_CLK_EN_4, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_FAB_200M, "clk_usb_fab_200m",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_CLK_EN_4, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_FAB_100M, "clk_usb_fab_100m",
			((const char *[]) { "clk_top_fab_100m" }),
			REG_CLK_EN_4, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_SUSPEND, "clk_usb_suspend",
			((const char *[]) { "clk_top_25m" }),
			REG_CLK_EN_4, 11,
			REG_DIV_CLK_USB_SUSPEND, 16, 8, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB0_REF_100M, "clk_usb0_ref",
			((const char *[]) { "clk_upll1_0" }),
			REG_CLK_EN_4, 12,
			REG_DIV_CLK_USB0_REF_100M, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB1_REF_100M, "clk_usb1_ref",
			((const char *[]) { "clk_upll1_1" }),
			REG_CLK_EN_4, 13,
			REG_DIV_CLK_USB1_REF_100M, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB0_CLKCORE, "clk_usb0_core",
			((const char *[]) { "clk_upll0_0" }),
			REG_CLK_EN_4, 14,
			REG_DIV_CLK_USB0_CLKCORE, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB1_CLKCORE, "clk_usb1_core",
			((const char *[]) { "clk_upll0_1" }),
			REG_CLK_EN_4, 15,
			REG_DIV_CLK_USB1_CLKCORE, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_REF, "clk_usb_ref",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_4, 16,
			REG_DIV_CLK_USB_REF, 16, 8, 50,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_MBIST_100M, "clk_usb_mbist",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_4, 17,
			REG_DIV_CLK_USB_MBIST_100M, 16, 8, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_DFT_800M, "clk_usb_dft_3",
			((const char *[]) { "clk_fpll2" }),
			REG_CLK_EN_4, 18,
			REG_DIV_CLK_USB_DFT_800M, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_DFT_600M, "clk_usb_dft_2",
			((const char *[]) { "clk_fpll1" }),
			REG_CLK_EN_4, 19,
			REG_DIV_CLK_USB_DFT_600M, 16, 8, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_DFT_500M, "clk_usb_dft_1",
			((const char *[]) { "clk_fpll0" }),
			REG_CLK_EN_4, 20,
			REG_DIV_CLK_USB_DFT_500M, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_USB_DFT_450M, "clk_usb_dft_0",
			((const char *[]) { "clk_fpll9" }),
			REG_CLK_EN_4, 21,
			REG_DIV_CLK_USB_DFT_450M, 16, 8, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_HDMI_REF_100M, "clk_hdmi_ref",
			((const char *[]) { "clk_hpll_0" }),
			REG_CLK_EN_4, 22,
			REG_DIV_CLK_HDMI_REF_100M, 16, 8, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CA55_GICCDISABLE, "ca55_giccdisable",
	//		((const char *[]) { "none"}),
	//		REG_AP_CLK_EN, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE0_CLK_GATE, "clk_core0",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE1_CLK_GATE, "clk_core1",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE2_CLK_GATE, "clk_core2",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 3,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE3_CLK_GATE, "clk_core3",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE4_CLK_GATE, "clk_core4",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 5,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE5_CLK_GATE, "clk_core5",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 6,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE6_CLK_GATE, "clk_core6",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 7,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CORE7_CLK_GATE, "clk_core7",
			((const char *[]) { "clk_ap_ca55" }),
			REG_AP_CLK_EN, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_DSU_SCLK_GATE_SOFT, "dsu_sclk_gate_soft",
	//		((const char *[]) { "none"}),
	//		REG_AP_CLK_EN, 9,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_DSU_DBG_PCLK_GATE_SOFT, "dsu_dbg_pclk_gate_soft",
	//		((const char *[]) { "none"}),
	//		REG_AP_CLK_EN, 10,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_DSU_ATCLK_GATE_SOFT, "dsu_atclk_gate_soft",
	//		((const char *[]) { "none"}),
	//		REG_AP_CLK_EN, 11,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_DSU_GIC_GICCLK_GATE_SOFT, "dsu_gic_gicclk_gate_soft",
	//		((const char *[]) { "none"}),
	//		REG_AP_CLK_EN, 12,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AHB_SF, "clk_ahb_sf",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_AUDSRC, "clk_apb_audsrc",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_CAN0, "clk_apb_can0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_CAN1, "clk_apb_can1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 3,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_HDMI, "clk_apb_hdmi",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C0, "clk_apb_i2c0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 5,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C1, "clk_apb_i2c1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 6,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C2, "clk_apb_i2c2",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 7,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C3, "clk_apb_i2c3",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C4, "clk_apb_i2c4",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C5, "clk_apb_i2c5",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C6, "clk_apb_i2c6",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 11,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C7, "clk_apb_i2c7",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C8, "clk_apb_i2c8",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 13,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2C9, "clk_apb_i2c9",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 14,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S0, "clk_apb_i2s0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 15,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S1, "clk_apb_i2s1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 16,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S2, "clk_apb_i2s2",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 17,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S3, "clk_apb_i2s3",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 18,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S4, "clk_apb_i2s4",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 19,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S5, "clk_apb_i2s5",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S6, "clk_apb_i2s6",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 21,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_I2S_GLOBAL, "clk_apb_i2s_g",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 22,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_SPI0, "clk_apb_spi0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 23,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_SPI1, "clk_apb_spi1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 24,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_SPI2, "clk_apb_spi2",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 25,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_SPI3, "clk_apb_spi3",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 26,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART0, "clk_apb_uart0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 27,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART1, "clk_apb_uart1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 28,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART2, "clk_apb_uart2",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 29,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART3, "clk_apb_uart3",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 30,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART4, "clk_apb_uart4",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_0, 31,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART5, "clk_apb_uart5",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART6, "clk_apb_uart6",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_UART7, "clk_apb_uart7",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI4_EMMC, "clk_axi4_emmc",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 3,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI4_ETH0, "clk_axi4_eth0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI4_ETH1, "clk_axi4_eth1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 5,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI4_SD0, "clk_axi4_sd0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 6,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI4_SD1, "clk_axi4_sd1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 7,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI4_SD2, "clk_axi4_sd2",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI_SDMA0, "clk_axi_sdma0",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_AXI_SDMA1, "clk_axi_sdma1",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_CAN0, "clk_can0",
			((const char *[]) { "clk_can" }),
			REG_HS_CLK_EN_1, 11,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_CAN1, "clk_can1",
			((const char *[]) { "clk_can" }),
			REG_HS_CLK_EN_1, 12,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_DBG_I2C, "clk_dbg_i2c",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 13,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C0, "clk_i2c0",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 14,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C1, "clk_i2c1",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 15,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C2, "clk_i2c2",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 16,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C3, "clk_i2c3",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 17,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C4, "clk_i2c4",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 18,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C5, "clk_i2c5",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 19,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C6, "clk_i2c6",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C7, "clk_i2c7",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 21,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C8, "clk_i2c8",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 22,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_I2C9, "clk_i2c9",
			((const char *[]) { "clk_i2c" }),
			REG_HS_CLK_EN_1, 23,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SPI0, "clk_spi0",
			((const char *[]) { "clk_dw_spi" }),
			REG_HS_CLK_EN_1, 24,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SPI1, "clk_spi1",
			((const char *[]) { "clk_dw_spi" }),
			REG_HS_CLK_EN_1, 25,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SPI2, "clk_spi2",
			((const char *[]) { "clk_dw_spi" }),
			REG_HS_CLK_EN_1, 26,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_SPI3, "clk_spi3",
			((const char *[]) { "clk_dw_spi" }),
			REG_HS_CLK_EN_1, 27,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_SPI_NAND, "clk_apb_spi_nand",
			((const char *[]) { "clk_top_fab_300m" }),
			REG_HS_CLK_EN_1, 28,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_TIMER, "clk_apb_timer",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_PERI_CLK_EN, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER0, "clk_timer0",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 24,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER1, "clk_timer1",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 25,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER2, "clk_timer2",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 3,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 26,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER3, "clk_timer3",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 27,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER4, "clk_timer4",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 5,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 28,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER5, "clk_timer5",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 6,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 29,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER6, "clk_timer6",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 7,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 30,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_TIMER7, "clk_timer7",
			((const char *[]) { "clk_rtc_32k", "clk_xtal_misc" }),
			REG_PERI_CLK_EN, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 31,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_WDT, "clk_apb_wdt",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_PERI_CLK_EN, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_GPIO, "clk_apb_gpio",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_PERI_CLK_EN, 10,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_CLK_APB_MAILBOX, "clk_apb_mailbox",
			((const char *[]) { "clk_top_fab_200m" }),
			REG_PERI_CLK_EN, 11,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_OTP, "clk_otp",
	//		((const char *[]) { "clk_otp" }),
	//		REG_PERI_CLK_EN, 12,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_APB_OTP, "clk_apb_otp",
	//		((const char *[]) { "clk_top_fab_200m" }),
	//		REG_PERI_CLK_EN, 13,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_APB_OTP_C, "clk_apb_otp_c",
	//		((const char *[]) { "none"}),
	//		REG_PERI_CLK_EN, 14,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_SARADC, "clk_saradc",
	//		((const char *[]) { "clk_saradc" }),
	//		REG_PERI_CLK_EN, 15,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	//CV_COMPOSITE_CLK(CV84X6_CLK_TEMPSEN, "clk_tempsen",
	//		((const char *[]) { "clk_tempsen" }),
	//		REG_PERI_CLK_EN, 16,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0, 0, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0,
	//		REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_WDT_RST_SYS_0, "clk_wdt_sys_0",
			((const char *[]) { "clk_rtc_32k", "clk_wdt" }),
			REG_PERI_CLK_EN, 20,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 17,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_WDT_RST_SYS_1, "clk_wdt_sys_1",
			((const char *[]) { "clk_rtc_32k", "clk_wdt" }),
			REG_PERI_CLK_EN, 21,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 18,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_WDT_RST_SYS_2, "clk_wdt_sys_2",
			((const char *[]) { "clk_rtc_32k", "clk_wdt" }),
			REG_PERI_CLK_EN, 22,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_PERI_CLK_EN, 19,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_AXI, "vo_clk_axi",
			((const char *[]) { "clk_vo_axi" }),
			REG_VO_CLK_EN, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_LDC0, "clk_vo_ldc0",
			((const char *[]) { "clk_vo_sys1" }),
			REG_VO_CLK_EN, 1,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_2DE0, "clk_vo_2de0",
			((const char *[]) { "clk_vo_sys1" }),
			REG_VO_CLK_EN, 2,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_OENC0, "clk_vo_oenc0",
			((const char *[]) { "clk_vo_sys1" }),
			REG_VO_CLK_EN, 3,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_VPSS0, "clk_vo_vpss0",
			((const char *[]) { "clk_vo_sys1" }),
			REG_VO_CLK_EN, 4,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_DISP0, "clk_vo_disp0",
			((const char *[]) { "clk_vo_disp" }),
			REG_VO_CLK_EN, 5,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_HDMI_MAC, "clk_vo_hdmi_mac",
			((const char *[]) { "clk_hdmi_sfr" }),
			REG_VO_CLK_EN, 6,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_IGA0, "clk_vo_iga0",
			((const char *[]) { "clk_vo_sys3" }),
			REG_VO_CLK_EN, 7,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_IGA1, "clk_vo_iga1",
			((const char *[]) { "clk_vo_sys3" }),
			REG_VO_CLK_EN, 8,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_COMPOSITE_CLK(CV84X6_VO_CLK_IGA2, "clk_vo_iga2",
			((const char *[]) { "clk_vo_sys3" }),
			REG_VO_CLK_EN, 9,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0, 0, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_PLL_POST_CLK(CV84X6_CLK_DPLL0_0, "clk_dpll0_0",
			((const char *[]) { "osc", "clk_dpll0" }),
			REG_DPLL0_CTRL_0, 24,
			REG_DPLL0_CTRL_0, 12, 3, 1,
			REG_DPLL0_CTRL_0, 15, 3, 1,
			REG_DPLL0_CTRL_2, 24,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_PLL_POST_CLK(CV84X6_CLK_DPLL1_0, "clk_dpll1_0",
			((const char *[]) { "osc", "clk_dpll1" }),
			REG_DPLL1_CTRL_0, 24,
			REG_DPLL1_CTRL_0, 12, 3, 1,
			REG_DPLL1_CTRL_0, 15, 3, 1,
			REG_DPLL1_CTRL_2, 24,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_PLL_POST_CLK(CV84X6_CLK_UPLL0_0, "clk_upll0_0",
			((const char *[]) { "osc", "clk_upll0" }),
			REG_UPLL0_CTRL_0, 24,
			REG_UPLL0_CTRL_0, 12, 3, 1,
			REG_UPLL0_CTRL_0, 15, 3, 1,
			REG_UPLL0_CTRL_2, 24,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_PLL_POST_CLK(CV84X6_CLK_UPLL0_1, "clk_upll0_1",
			((const char *[]) { "osc", "clk_upll0" }),
			REG_UPLL0_CTRL_0, 25,
			REG_UPLL0_CTRL_1, 2, 3, 1,
			REG_UPLL0_CTRL_1, 5, 3, 1,
			REG_UPLL0_CTRL_2, 25,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_PLL_POST_CLK(CV84X6_CLK_UPLL1_0, "clk_upll1_0",
			((const char *[]) { "osc", "clk_upll1" }),
			REG_UPLL1_CTRL_0, 24,
			REG_UPLL1_CTRL_0, 12, 3, 1,
			REG_UPLL1_CTRL_0, 15, 3, 1,
			REG_UPLL1_CTRL_2, 24,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_PLL_POST_CLK(CV84X6_CLK_UPLL1_1, "clk_upll1_1",
			((const char *[]) { "osc", "clk_upll1" }),
			REG_UPLL1_CTRL_0, 25,
			REG_UPLL1_CTRL_1, 2, 3, 1,
			REG_UPLL1_CTRL_1, 5, 3, 1,
			REG_UPLL1_CTRL_2, 25,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

	CV_PLL_POST_CLK(CV84X6_CLK_HPLL0_0, "clk_hpll_0",
			((const char *[]) { "osc", "clk_hpll" }),
			REG_HPLL0_CTRL_0, 24,
			REG_HPLL0_CTRL_0, 12, 3, 1,
			REG_HPLL0_CTRL_0, 15, 3, 1,
			REG_HPLL0_CTRL_2, 24,
			REG_UNEXIST, 0,
			REG_UNEXIST, 0),

};

static void __init cv84x6_clk_init(struct device_node *node)
{
	int ret;

	cv84x6_clk_data = cvi_clk_init(node, CV84X6_CLK_MAX);
	if (IS_ERR(cv84x6_clk_data)) {
		pr_err("%s: failed to init clock data (%ld)\n",
		       __func__, PTR_ERR(cv84x6_clk_data));
		return;
	}

	ret = cv_clk_register_plls(cv84x6_plls, ARRAY_SIZE(cv84x6_plls),
				   cv84x6_clk_data);
	if (ret)
		pr_err("%s: failed to register plls (%d)\n", __func__, ret);

	ret = cv_register_composite_clks(cv84x6_clks, ARRAY_SIZE(cv84x6_clks),
					 cv84x6_clk_data);
	if (ret)
		pr_err("%s: failed to register composite clocks (%d)\n",
		       __func__, ret);

	cvi_clk_add_hw_provider(node, cv84x6_clk_data);

#ifdef CONFIG_PM_SLEEP
	/* TODO: implement suspend/resume saving if needed */
#endif
}
CLK_OF_DECLARE(cvi_clk, "cvitek,cv84x6-clk", cv84x6_clk_init);

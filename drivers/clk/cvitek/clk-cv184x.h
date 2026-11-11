/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2020. All rights reserved.
 *
 * File Name: cv184x-resets.h
 * Description: CV184X Clock Driver
 */

/* top_pll_g2 */
#define REG_PLL_G2_CTRL			0x800
#define REG_PLL_G2_STATUS		0x804
#define REG_MIPIMPLL_CSR		0x808
#define REG_APLL0_CSR			0x80C
#define REG_DISPPLL_CSR			0x810
#define REG_CAM0PLL_CSR			0x814
#define REG_CAM1PLL_CSR			0x818
#define REG_PLL_G2_SSC_SYN_CTRL		0x840
#define REG_APLL_SSC_SYN_CTRL		0x850
#define REG_APLL_SSC_SYN_SET		0x854
#define REG_APLL_SSC_SYN_SPAN		0x858
#define REG_APLL_SSC_SYN_STEP		0x85C
#define REG_DISPPLL_SSC_SYN_CTRL	0x860
#define REG_DISPPLL_SSC_SYN_SET		0x864
#define REG_DISPPLL_SSC_SYN_SPAN	0x868
#define REG_DISPPLL_SSC_SYN_STEP	0x86C
#define REG_CAM0PLL_SSC_SYN_CTRL	0x870
#define REG_CAM0PLL_SSC_SYN_SET		0x874
#define REG_CAM0PLL_SSC_SYN_SPAN	0x878
#define REG_CAM0PLL_SSC_SYN_STEP	0x87C
#define REG_CAM1PLL_SSC_SYN_CTRL	0x880
#define REG_CAM1PLL_SSC_SYN_SET		0x884
#define REG_CAM1PLL_SSC_SYN_SPAN	0x888
#define REG_CAM1PLL_SSC_SYN_STEP	0x88C
#define REG_APLL_FRAC_DIV_CTRL		0x890
#define REG_APLL_FRAC_DIV_M		0x894
#define REG_APLL_FRAC_DIV_N		0x898
#define REG_MIPIMPLL_CLK_CSR		0x8A0
#define REG_A0PLL_CLK_CSR		0x8A4
#define REG_DISPPLL_CLK_CSR		0x8A8
#define REG_CAM0PLL_CLK_CSR		0x8AC
#define REG_CAM1PLL_CLK_CSR		0x8B0
#define REG_CLK_CAM0_SRC_DIV		0x8C0
#define REG_CLK_CAM1_SRC_DIV		0x8C4
#define REG_CLK_CAM2_SRC_DIV		0x8C8

/* top_pll_g6 */
#define REG_PLL_G6_CTRL			0x900
#define REG_PLL_G6_STATUS		0x904
#define REG_MPLL_CSR			0x908
#define REG_TPLL_CSR			0x90C
#define REG_FPLL_CSR			0x910
#define REG_APPLL_CSR			0x914
#define REG_RVPLL_CSR			0x918
#define REG_PLL_G6_SSC_SYN_CTRL		0x940
#define REG_DPLL_SSC_SYN_CTRL		0x950
#define REG_DPLL_SSC_SYN_SET		0x954
#define REG_DPLL_SSC_SYN_SPAN		0x958
#define REG_DPLL_SSC_SYN_STEP		0x95C
#define REG_MPLL_SSC_SYN_CTRL		0x960
#define REG_MPLL_SSC_SYN_SET		0x964
#define REG_MPLL_SSC_SYN_SPAN		0x968
#define REG_MPLL_SSC_SYN_STEP		0x96C
#define REG_TPLL_SSC_SYN_CTRL		0x970
#define REG_TPLL_SSC_SYN_SET		0x974
#define REG_TPLL_SSC_SYN_SPAN		0x978
#define REG_TPLL_SSC_SYN_STEP		0x97C
#define REG_APPLL_SSC_SYN_CTRL		0x970
#define REG_APPLL_SSC_SYN_SET		0x974
#define REG_APPLL_SSC_SYN_SPAN		0x978
#define REG_APPLL_SSC_SYN_STEP		0x97C
#define REG_RVPLL_SSC_SYN_CTRL		0x970
#define REG_RVPLL_SSC_SYN_SET		0x974
#define REG_RVPLL_SSC_SYN_SPAN		0x978
#define REG_RVPLL_SSC_SYN_STEP		0x97C

/* clkgen reg */
// --- enable reg ---
#define REG_CLK_EN_0			0x0E8
#define REG_CLK_EN_1			0x0EC
#define REG_CLK_EN_2			0x0F0
#define REG_CLK_EN_3			0x0F4
#define REG_CLK_EN_4			0x0F8
#define REG_CLK_EN_5			0x0FC
// --- sel reg ---
#define REG_CLK_SEL_0			0x100
// --- bypass reg ---
#define REG_CLK_BYP_0			0x104
#define REG_CLK_BYP_1			0x108
#define REG_CLK_BYP_2			0x10C
// --- rtc system enable reg ---
#define REG_RTC_SYS_BASE_ADDR   0x05025000
#define REG_CLK_EN_6_FOR_RTC	0x034
// --- rtc system bypass reg ---
#define REG_CLK_BYP_3_FOR_RTC   0x030   //bit[0]: clk_fab , 0: clk_fab_pre, 1: xtal (default)
// --- rtc system clk mux ---
#define REG_CLK_MUX_FOR_RTC     0x01C

/* Clock Divider Register Definitions (Base Address: 0x0) */
// --- TOP ---
#define REG_DIV_TOP_CLK_FAB_100M              0x0   // h0
#define REG_DIV_TOP_CLK_HSPERI                0x4   // h4
#define REG_DIV_TOP_CLK_FAB_500M              0xC   // hc
#define REG_DIV_TOP_CLK_1M                    0x10  // h10

// --- AP ---
#define REG_DIV_AP_CPU_CLK_0              0x14  // h14
#define REG_DIV_AP_CPU_CLK_1              0x18  // h18
#define REG_DIV_AP_CLK_RV1_0              0x1C  // h1c
#define REG_DIV_AP_CLK_RV1_1              0x20  // h20
#define REG_DIV_AP_BUS_CLK                0x24  // h24
#define REG_DIV_AP_GIC_CLK                0x28  // h28

// --- TPU ---
#define REG_DIV_TPU_CLK_TPU_SYS           0x2C  // h2c
#define REG_DIV_TPU_CLK_GDMA_0            0x30  // h30
#define REG_DIV_TPU_CLK_GDMA_1            0x34  // h34
#define REG_DIV_TPU_CLK_TPU_0             0x38  // h38
#define REG_DIV_TPU_CLK_TPU_1             0x3C  // h3c

// --- Video Control ---
#define REG_DIV_VC_CLK_VIDEO_AXI_0        0x40  // h40
#define REG_DIV_VC_CLK_VIDEO_AXI_1        0x44  // h44
#define REG_DIV_VC_CLK_VC_SRC0_0          0x48  // h48
#define REG_DIV_VC_CLK_VC_SRC0_1          0x4C  // h4c
#define REG_DIV_VC_CLK_VC_SRC1_0          0x50  // h50
#define REG_DIV_VC_CLK_VC_SRC1_1          0x54  // h54

// --- VIVO ---
#define REG_DIV_VIVO_CLK_RAW_AXI          0x58  // h58
#define REG_DIV_VIVO_CLK_SRC_VIP_SYS_0    0x5C  // h5c
#define REG_DIV_VIVO_CLK_SRC_VIP_SYS_1    0x60  // h60
#define REG_DIV_VIVO_CLK_SRC_VIP_SYS_2    0x64  // h64
#define REG_DIV_VIVO_CLK_SRC_VIP_SYS_3    0x68  // h68
#define REG_DIV_VIVO_CLK_SRC_VIP_SYS_4    0x6C  // h6c
#define REG_DIV_VIVO_CLK_SYS_DISP         0x70  // h70
#define REG_DIV_VIVO_CLK_CYC_SCAN_100M    0x74  // h74
#define REG_DIV_VIVO_CLK_CYC_DSI_ESC      0x78  // h78
#define REG_DIV_VIVO_CLK_CYC_SCAN_300M    0x7C  // h7c
#define REG_DIV_VIVO_CLK_CYC_DSI_SYN      0x80  // h80
#define REG_DIV_VIVO_CLK_MIPIMPLL         0x84  // h84

// --- HSPERI ---
#define REG_DIV_HSPERI_CLK_SD0            0x98  // h98
#define REG_DIV_HSPERI_CLK_SD1            0x9C  // h9c
#define REG_DIV_HSPERI_EMMC_CARD_CLK      0xA0  // ha0
#define REG_DIV_HSPERI_ETHER0_CLK_ETH_PLL 0xA4  // ha4
#define REG_DIV_HSPERI_CLK_SPI_NAND       0xAC  // hac
#define REG_DIV_HSPERI_CLK_AUDSRC         0xB0  // hb0
#define REG_DIV_HSPERI_CLK_AUD0           0xB4  // hb4
#define REG_DIV_HSPERI_CLK_AUD1           0xB8  // hb8
#define REG_DIV_HSPERI_CLK_AUD2           0xBC  // hbc
#define REG_DIV_HSPERI_CLK_AUD3           0xC0  // hc0
#define REG_DIV_HSPERI_CLK_SPI            0xC4  // hc4
#define REG_DIV_HSPERI_CLK_I2C            0xC8  // hc8
#define REG_DIV_HSPERI_CLK_UART0          0xCC  // hcc
#define REG_DIV_HSPERI_CLK_UART1          0xD0  // hd0
#define REG_DIV_HSPERI_CLK_UART2          0xD4  // hd4
#define REG_DIV_HSPERI_CLK_UART3          0xD8  // hd8
#define REG_DIV_HSPERI_CLK_UART4          0xDC  // hdc

#define REG_DIV_HSPERI_USB20_BUS_EARLY    0x8C  // h8c (原div_hsperi_usb20_bus_clk_early_reg)
#define REG_DIV_HSPERI_USB20_SUSPEND      0x90  // h90 (原div_hsperi_usb20_suspend_clk_reg)
#define REG_DIV_HSPERI_USB20_REF          0x94  // h94 (原div_hsperi_usb20_ref_clk_reg)
#define REG_DIV_HSPERI_CLK_SPI_NOR        0xA8  // ha8 (原div_hsperi_clk_spi_nor_reg)

// --- RTC ---
#define REG_DIV_RTC_CLK_RTC_SYS           0x8   // h8 (原div_rtc_clk_rtc_sys_reg)
#define REG_DIV_RTC_CLK_SPI_NOR           0x88  // h88 (原div_rtc_clk_spi_nor_reg)

// --- PERI ---
#define REG_DIV_PERI_PWM_CLK              0xE0  // he0
#define REG_DIV_PERI_CLK_XTAL_MISC        0xE4  // he4


#define REG_PLL_G2_CSR_NUM                  (REG_CAM1PLL_CSR / 4 - REG_MIPIMPLL_CSR / 4 + 1)
#define REG_PLL_G2_CSR_START                REG_MIPIMPLL_CSR

#define REG_PLL_G6_CSR_NUM                  (REG_RVPLL_CSR / 4 - REG_MPLL_CSR / 4 + 1)
#define REG_PLL_G6_CSR_START                REG_MPLL_CSR

#define REG_CLK_EN_NUM                      (REG_CLK_EN_5 / 4 - REG_CLK_EN_0 / 4 + 1)
#define REG_CLK_EN_START                    REG_CLK_EN_0

#define REG_CLK_SEL_NUM                     (REG_CLK_SEL_0 / 4  - REG_CLK_SEL_0 / 4 + 1)
#define REG_CLK_SEL_START                   REG_CLK_SEL_0

#define REG_CLK_BYP_NUM                     (REG_CLK_BYP_2 / 4 - REG_CLK_BYP_0 / 4 + 1)
#define REG_CLK_BYP_START                   REG_CLK_BYP_0

#define REG_CLK_DIV_NUM                     (REG_DIV_PERI_CLK_XTAL_MISC / 4 - REG_DIV_TOP_CLK_FAB_100M / 4 + 1)
#define REG_CLK_DIV_START                   REG_DIV_TOP_CLK_FAB_100M

#define REG_CLK_G2_DIV_NUM                  (REG_CLK_CAM2_SRC_DIV / 4 - REG_CLK_CAM0_SRC_DIV / 4 + 1)
#define REG_CLK_G2_DIV_START                REG_CLK_CAM0_SRC_DIV

#define CV184X_PLL_LOCK_TIMEOUT_MS	200

/* PLL status register offset */
#define PLL_STATUS_MASK			0xFF
#define PLL_STATUS_OFFSET		0x04

/* G2 Synthesizer register offset */
#define G2_SSC_CTRL_MASK		0xFF
#define G2_SSC_CTRL_OFFSET		0x40
#define SSC_SYN_SET_MASK		0x0F
#define SSC_SYN_SET_OFFSET		0x04


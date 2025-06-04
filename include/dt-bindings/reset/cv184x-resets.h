/*
 * Copyright (C) Cvitek Co., Ltd. 2019-2020. All rights reserved.
 *
 * File Name: cv184x-resets.h
 * Description: CV184x SoC Hardware Reset Bit Definitions.
 *              This file provides symbolic constants for all peripheral reset control
 *              bits in the CV184x System-on-Chip. Each macro represents a specific
 *              hardware module's reset bit position in the reset control registers.
 *
 * Notes:
 *  - Prefix Legend:
 *    RST_    : Active-low reset signals
 *    RSTN_   : Active-high reset signals
 *    AUTO_CLR_RST_ : Auto-clearing reset signals
 *    SOFT_RST_     : Software-triggered resets
 *  - Values are contiguous and reflect bit positions in reset control registers
 *  - Do not modify numbering sequence (hardware dependency)
 */

#ifndef __DT_BINDINGS_RST_CV184X_H__
#define __DT_BINDINGS_RST_CV184X_H__

// ====================== sw_rst_reset_x_* ======================
#define RST_TEMP0                    0      //reserved
#define RST_TEMP1                    1      //reserved
#define RST_TEMP2                    2      //reserved
#define RST_TEMP3                    3      //reserved
#define RST_TEMP4                    4      //reserved
#define RST_TEMP5                    5      //reserved
#define RST_VIVO_SYS                 6
#define RST_VC_SYS                   7
#define RST_VE                       8
#define RST_JPEG                     9
#define RST_DDR                      10
#define RST_AUDDAC                   11
#define RST_AUDADC                   12
#define RST_AUDSRC                   13
#define RST_SPI_NAND                 14
#define RST_SE                       15
#define RST_USB                      16
#define RST_USB1                     17
#define RST_ETH0                     18
#define RST_ETH1                     19
#define RST_NAND                     20
#define RST_SDMA0                    21
#define RST_SDMA1                    22
#define RST_I2S0                     23
#define RST_I2S1                     24
#define RST_I2S2                     25
#define RST_I2S3                     26
#define RST_UART0                    27
#define RST_UART1                    28
#define RST_UART2                    29
#define RST_UART3                    30
#define RST_UART4                    31
#define RST_ETHPHY                   32
#define RST_EMMC                     33
#define RST_SD0                      34
#define RST_SD1                      35
#define RST_SF                       36
#define RST_SF1                      37
#define RST_AHBROM                   38
#define RST_I2C0                     39
#define RST_I2C1                     40
#define RST_I2C2                     41
#define RST_I2C3                     42
#define RST_I2C4                     43
#define RST_SPI0                     44
#define RST_SPI1                     45
#define RST_SPI2                     46
#define RST_SPI3                     47
#define RST_TIMER0                   48
#define RST_TIMER1                   49
#define RST_TIMER2                   50
#define RST_TIMER3                   51
#define RST_TIMER4                   52
#define RST_TIMER5                   53
#define RST_TIMER6                   54
#define RST_TIMER7                   55
#define RST_AUDDAC_APB               56
#define RST_AUDADC_APB               57
#define RST_TIMER                    58
#define RST_SARADC                   59
#define RST_TEMPSEN                  60
#define RST_WGN0                     61
#define RST_WGN1                     62
#define RST_WGN2                     63
#define RST_KEYSCAN                  64
#define RST_WDT0                     65
#define RST_WDT1                     66
#define RST_WDT2                     67
#define RST_GPIO0                    68
#define RST_GPIO1                    69
#define RST_GPIO2                    70
#define RST_GPIO3                    71
#define RST_PWM0                     72
#define RST_PWM1                     73
#define RST_PWM2                     74
#define RST_PWM3                     75
#define RST_PWM4                     76
#define RST_PWM5                     77
#define RST_PWM6                     78
#define RST_PWM7                     79
#define RST_EFUSEC                   80

// ====================== sw_rst_resetn_x_* ======================
#define RSTN_TPU_SYS                 81
#define RSTN_TPU                     82
#define RSTN_GDMA                    83

// ====================== sw_rst_reset_x_* ======================
#define RST_EPHY                     84
#define RST_SYSTEM                   85
#define RST_DUMMY0                   86
#define RST_DUMMY1                   87
#define RST_DUMMY2                   88
#define RST_DUMMY3                   89
#define RST_DUMMY4                   90
#define RST_DUMMY5                   91
#define RST_DUMMY6                   92
#define RST_DUMMY7                   93
#define RST_DUMMY8                   94


// ====================== reg_auto_clear_reset_x_* ======================
#define AUTO_CLR_RST_CPUCORE0        95
#define AUTO_CLR_RST_CPUCORE1        96     //NOT USED
#define AUTO_CLR_RST_CPUCORE2        97     //NOT USED
#define AUTO_CLR_RST_CPUCORE3        98     //NOT USED
#define AUTO_CLR_RST_CA53            99
#define AUTO_CLR_RST_C906_0          100    //NOT USED
#define AUTO_CLR_RST_C906_1          101

// ====================== reg_soft_reset_x_* ======================
#define SOFT_RST_CPUCORE0            102
#define SOFT_RST_CPUCORE1            103    //NOT USED
#define SOFT_RST_CPUCORE2            104    //NOT USED
#define SOFT_RST_CPUCORE3            105    //NOT USED
#define SOFT_RST_CA53                106
#define SOFT_RST_C906_0              107    //NOT USED
#define SOFT_RST_C906_1              108

// ========== Top Level Clocks ===========
#define CLK_RST_FAB_100M          0   // reg_top_clk_fab_100m_en
#define CLK_RST_HSPERI            1   // reg_top_clk_hsperi_en
#define CLK_RST_RTC_SYS           2   // reg_rtc_clk_rtc_sys_en
#define CLK_RST_FAB_500M          3   // reg_top_clk_fab_500m_en
#define CLK_RST_1M                4   // reg_top_clk_1m_en

// ========== AP Subsystem Clocks ===========
#define CLK_RST_AP_CPU            5   // reg_ap_cpu_clk_en
#define CLK_RST_RV1               6   // reg_ap_clk_rv1_en
#define CLK_RST_AP_BUS            7   // reg_ap_bus_clk_en
#define CLK_RST_AP_GIC            8   // reg_ap_gic_clk_en
#define CLK_RST_AP_DBG            9   // reg_ap_dbg_clk_en
#define CLK_RST_AP_SC             10  // reg_ap_sc_clk_en

// ========== TPU Clock Domain ===========
#define CLK_RST_TPU_SYS           11  // reg_tpu_clk_tpu_sys_en
#define CLK_RST_GDMA              12  // reg_tpu_clk_gdma_en
#define CLK_RST_TPU               13  // reg_tpu_clk_tpu_en

// ========== Video Codec Clocks ===========
#define CLK_RST_VIDEO_AXI         14  // reg_vc_clk_video_axi_en
#define CLK_RST_VC_SRC0           15  // reg_vc_clk_vc_src0_en
#define CLK_RST_VC_SRC1           16  // reg_vc_clk_vc_src1_en

// ========== Display Subsystem ===========
#define CLK_RST_X2P               17  // reg_vivo_clk_x2p_en
#define CLK_RST_RAW_AXI           18  // reg_vivo_clk_raw_axi_en
#define CLK_RST_SRC_VIP_SYS_0     19  // reg_vivo_clk_src_vip_sys_0_en
#define CLK_RST_SRC_VIP_SYS_1     20  // reg_vivo_clk_src_vip_sys_1_en
#define CLK_RST_SRC_VIP_SYS_2     21  // reg_vivo_clk_src_vip_sys_2_en
#define CLK_RST_SRC_VIP_SYS_3     22  // reg_vivo_clk_src_vip_sys_3_en
#define CLK_RST_SRC_VIP_SYS_4     23  // reg_vivo_clk_src_vip_sys_4_en
#define CLK_RST_SYS_DISP          24  // reg_vivo_clk_sys_disp_en
#define CLK_RST_CYC_SCAN_100M     25  // reg_vivo_clk_cyc_scan_100m_en
#define CLK_RST_CYC_DSI_ESC       26  // reg_vivo_clk_cyc_dsi_esc_en
#define CLK_RST_CYC_SCAN_300M     27  // reg_vivo_clk_cyc_scan_300m_en
#define CLK_RST_CYC_DSI_SYN       28  // reg_vivo_clk_cyc_dsi_syn_en
#define CLK_RST_MIPIPLL           29  // reg_vivo_clk_mipimpll_en

// ========== Peripheral Clocks ===========
#define CLK_RST_SPI_NOR           30  // reg_rtc_clk_spi_nor_en
#define CLK_RST_USB20_BUS_EARLY   31  // reg_hsperi_usb20_bus_clk_early_en
#define CLK_RST_USB20_SUSPEND     32  // reg_hsperi_usb20_suspend_clk_en
#define CLK_RST_USB20_REF         33  // reg_hsperi_usb20_ref_clk_en
#define CLK_RST_USB20_CORECLKIN   34  // reg_hsperi_usb20_coreclkin_en
#define CLK_RST_SD0               35  // reg_hsperi_clk_sd0_en
#define CLK_RST_100K_SD0          36  // reg_hsperi_clk_100k_sd0_en
#define CLK_RST_SD1               37  // reg_hsperi_clk_sd1_en
#define CLK_RST_100K_SD1          38  // reg_hsperi_clk_100k_sd1_en
#define CLK_RST_EMMC_CARD         39  // reg_hsperi_emmc_card_clk_en
#define CLK_RST_EMMC_100K         40  // reg_hsperi_emmc_clk100k_en
#define CLK_RST_ETHER0_PLL        41  // reg_hsperi_ether0_clk_eth_pll_en
#define CLK_RST_SPI_NOR_HS        42  // reg_hsperi_clk_spi_nor_en
#define CLK_RST_SPI_NAND          43  // reg_hsperi_clk_spi_nand_en
#define CLK_RST_AUDSRC            44  // reg_hsperi_clk_audsrc_en
#define CLK_RST_AUD0              45  // reg_hsperi_clk_aud0_en
#define CLK_RST_AUD1              46  // reg_hsperi_clk_aud1_en
#define CLK_RST_AUD2              47  // reg_hsperi_clk_aud2_en
#define CLK_RST_AUD3              48  // reg_hsperi_clk_aud3_en
#define CLK_RST_SPI               49  // reg_hsperi_clk_spi_en
#define CLK_RST_I2C               50  // reg_hsperi_clk_i2c_en
#define CLK_RST_UART0             51  // reg_hsperi_clk_uart0_en
#define CLK_RST_UART1             52  // reg_hsperi_clk_uart1_en
#define CLK_RST_UART2             53  // reg_hsperi_clk_uart2_en
#define CLK_RST_UART3             54  // reg_hsperi_clk_uart3_en
#define CLK_RST_UART4             55  // reg_hsperi_clk_uart4_en

#define CLK_RST_WDT_PCLK          56  // reg_peri_wdt_pclk_en
#define CLK_RST_GPIO_DBCLK        57  // reg_peri_gpio_dbclk_en
#define CLK_RST_WGN_XCLK          58  // reg_peri_wgn_xclk_en
#define CLK_RST_KEYSCAN_XCLK      59  // reg_peri_keyscan_xclk_en
#define CLK_RST_EFUSE_PCLK        60  // reg_peri_efuse_pclk_en
#define CLK_RST_EFUSE             61  // reg_peri_efuse_clk_en
#define CLK_RST_PWM               62  // reg_peri_pwm_clk_en
#define CLK_RST_XTAL_MISC         63  // reg_peri_clk_xtal_misc_en
#define CLK_RST_TEMPSEN           64  // reg_peri_clk_tempsen_en
#define CLK_RST_SARADC            65  // reg_peri_clk_saradc_en
#define CLK_RST_FAB6_100M_FREE    66  // reg_peri_fab6_100m_clk_free_en

// ========== Debug Clocks ===========
#define CLK_RST_DEBUG_DBGSYS      67  // reg_debug_dbgsys_clk_en

// ========== VIP Subsystem ===========
#define CLK_RST_DISP_VIP          68  // reg_clk_disp_vip_en
#define CLK_RST_CSI_MAC0_VIP      69  // reg_clk_csi_mac0_vip_en
#define CLK_RST_CSI_MAC1_VIP      70  // reg_clk_csi_mac1_vip_en
#define CLK_RST_CSI_MAC2_VIP      71  // reg_clk_csi_mac2_vip_en
#define CLK_RST_CSI_BE_VIP        72  // reg_clk_csi_be_vip_en
#define CLK_RST_ISP_TOP_VIP       73  // reg_clk_isp_top_vip_en
#define CLK_RST_RAW_VIP           74  // reg_clk_raw_vip_en
#define CLK_RST_VPSS0_VIP         75  // reg_clk_vpss0_vip_en
#define CLK_RST_VPSS1_VIP         76  // reg_clk_vpss1_vip_en
#define CLK_RST_VPSS2_VIP         77  // reg_clk_vpss2_vip_en
#define CLK_RST_VPSS3_VIP         78  // reg_clk_vpss3_vip_en
#define CLK_RST_LDC_VIP           79  // reg_clk_ldc_vip_en
#define CLK_RST_CAM0_VIP          80  // reg_clk_cam0_vip_en
#define CLK_RST_CAM1_VIP          81  // reg_clk_cam1_vip_en
#define CLK_RST_CAM2_VIP          82  // reg_clk_cam2_vip_en
#define CLK_RST_PAD_VI0_CLK0      83  // reg_pad_vi0_clk0_vip_en
#define CLK_RST_PAD_VI0_CLK1      84  // reg_pad_vi0_clk1_vip_en
#define CLK_RST_PAD_VI1_CLK       85  // reg_pad_vi1_clk_vip_en
#define CLK_RST_PAD_VI2_CLK       86  // reg_pad_vi2_clk_vip_en
#define CLK_RST_LVDS0_VIP         87  // reg_clk_lvds0_vip_en
#define CLK_RST_LVDS1_VIP         88  // reg_clk_lvds1_vip_en
#define CLK_RST_DSI_MAC_VIP       89  // reg_clk_dsi_mac_vip_en
#define CLK_RST_CSI0_RX_VIP       90  // reg_clk_csi0_rx_vip_en
#define CLK_RST_CSI1_RX_VIP       91  // reg_clk_csi1_rx_vip_en
#define CLK_RST_CSI2_RX_VIP       92  // reg_clk_csi2_rx_vip_en
#define CLK_RST_VO_MAC_VIP        93  // reg_clk_vo_mac_vip_en
#define CLK_RST_2DE_VIP           94  // reg_clk_2de_vip_en

// ==========  Bus Clocks ===========
#define CLK_RST_APB_VCSYS         95  // reg_clk_apb_en_vcsys
#define CLK_RST_APB_VE            96  // reg_clk_apb_en_ve
#define CLK_RST_APB_JPEG          97  // reg_clk_apb_en_jpeg
#define CLK_RST_EN_VE             98  // reg_clk_en_ve
#define CLK_RST_EN_JPEG           99  // reg_clk_en_jpeg
#define CLK_RST_APB_AUDSRC        100 // reg_clk_apb_audsrc_en
#define CLK_RST_AHB_ROM           101 // reg_clk_ahb_rom_en
#define CLK_RST_AXI4_EMMC         102 // reg_clk_axi4_emmc_en
#define CLK_RST_AXI4_SD0          103 // reg_clk_axi4_sd0_en
#define CLK_RST_AXI4_SD1          104 // reg_clk_axi4_sd1_en
#define CLK_RST_SPI_NAND_AXI      105 // reg_clk_spi_nand_en
#define CLK_RST_AXI4_ETH0         106 // reg_clk_axi4_eth0_en
#define CLK_RST_AXI4_ETH1         107 // reg_clk_axi4_eth1_en
#define CLK_RST_AHB_SF            108 // reg_clk_ahb_sf_en
#define CLK_RST_AHB_SF1           109 // reg_clk_ahb_sf1_en

// ==========  DMA Clocks ===========
#define CLK_RST_SDMA0_AXI         110 // reg_clk_sdma0_axi_en
#define CLK_RST_SDMA1_AXI         111 // reg_clk_sdma1_axi_en
#define CLK_RST_SDMA_AUD0         112 // reg_clk_sdma_aud0_en
#define CLK_RST_SDMA_AUD1         113 // reg_clk_sdma_aud1_en
#define CLK_RST_SDMA_AUD2         114 // reg_clk_sdma_aud2_en
#define CLK_RST_SDMA_AUD3         115 // reg_clk_sdma_aud3_en

// ==========  Other Clocks ===========
#define CLK_RST_APB_SPI0          116 // reg_clk_apb_spi0_en
#define CLK_RST_APB_SPI1          117 // reg_clk_apb_spi1_en
#define CLK_RST_APB_SPI2          118 // reg_clk_apb_spi2_en
#define CLK_RST_APB_SPI3          119 // reg_clk_apb_spi3_en
#define CLK_RST_UART0_CORE        120 // reg_clk_uart0_en
#define CLK_RST_APB_UART0         121 // reg_clk_apb_uart0_en
#define CLK_RST_UART1_CORE        122 // reg_clk_uart1_en
#define CLK_RST_APB_UART1         123 // reg_clk_apb_uart1_en
#define CLK_RST_UART2_CORE        124 // reg_clk_uart2_en
#define CLK_RST_APB_UART2         125 // reg_clk_apb_uart2_en
#define CLK_RST_UART3_CORE        126 // reg_clk_uart3_en
#define CLK_RST_APB_UART3         127 // reg_clk_apb_uart3_en
#define CLK_RST_UART4_CORE        128 // reg_clk_uart4_en
#define CLK_RST_APB_UART4         129 // reg_clk_apb_uart4_en
#define CLK_RST_APB_I2S0          130 // reg_clk_apb_i2s0_en
#define CLK_RST_APB_I2S1          131 // reg_clk_apb_i2s1_en
#define CLK_RST_APB_I2S2          132 // reg_clk_apb_i2s2_en
#define CLK_RST_APB_I2S3          133 // reg_clk_apb_i2s3_en
#define CLK_RST_AXI4_USB          134 // reg_clk_axi4_usb_en
#define CLK_RST_APB_USB           135 // reg_clk_apb_usb_en
#define CLK_RST_I2C_CORE          136 // reg_clk_i2c_en
#define CLK_RST_APB_I2C0          137 // reg_clk_apb_i2c0_en
#define CLK_RST_APB_I2C1          138 // reg_clk_apb_i2c1_en
#define CLK_RST_APB_I2C2          139 // reg_clk_apb_i2c2_en
#define CLK_RST_APB_I2C3          140 // reg_clk_apb_i2c3_en
#define CLK_RST_APB_I2C4          141 // reg_clk_apb_i2c4_en
#define CLK_RST_WGN               142 // reg_clk_wgn_en
#define CLK_RST_WGN0              143 // reg_clk_wgn0_en
#define CLK_RST_WGN1              144 // reg_clk_wgn1_en
#define CLK_RST_WGN2              145 // reg_clk_wgn2_en
#define CLK_RST_KEYSCAN           146 // reg_clk_keyscan_en
#define CLK_RST_APB_WDT           147 // reg_clk_apb_wdt_en
#define CLK_RST_TIMER0            148 // reg_clk_timer0_en
#define CLK_RST_TIMER1            149 // reg_clk_timer1_en
#define CLK_RST_TIMER2            150 // reg_clk_timer2_en
#define CLK_RST_TIMER3            151 // reg_clk_timer3_en
#define CLK_RST_TIMER4            152 // reg_clk_timer4_en
#define CLK_RST_TIMER5            153 // reg_clk_timer5_en
#define CLK_RST_TIMER6            154 // reg_clk_timer6_en
#define CLK_RST_TIMER7            155 // reg_clk_timer7_en
#define CLK_RST_TEMPSEN_CORE      156 // reg_clk_tempsen_en
#define CLK_RST_SARADC_CORE       157 // reg_clk_saradc_en
#define CLK_RST_PM                158 // reg_clk_pm_en
#define CLK_RST_APB_GPIO          159 // reg_clk_apb_gpio_en

#endif /* _DT_BINDINGS_RST_CV1835_H_ */

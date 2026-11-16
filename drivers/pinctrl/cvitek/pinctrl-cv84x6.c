// SPDX-License-Identifier: GPL-2.0
/*
 * Cvitek CV84x6 pinctrl driver
 *
 * Full Linux pinctrl subsystem driver implementing:
 *   - pinctrl_ops  (pin/group enumeration, DT node parsing)
 *   - pinmux_ops   (function mux selection; per-group pin_mux[] parallel to pins)
 *   - pinconf_ops  (bias, drive-strength, schmitt, open-drain)
 *   - PM suspend/resume register save/restore
 *
 * Based on the cv84x6 cvi_pinmux userspace tool (ramdisk/tools/cvi_pinmux/cv84x6/).
 * Pin data auto-derived from CV1684X6-Pinlist.
 *
 * Copyright (C) Cvitek Co., Ltd. 2024. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/types.h>

#include "../pinctrl-utils.h"
#include "pinctrl-cv84x6.h"

/* ======================================================================
 * Pin data table  (139 pins, derived from func.h pinlist_map[])
 *
 * PIN(id, name, grp, idx, { {func_name, mux_val}, ... })
 * ====================================================================== */

#define F(n, v)  { .name = (n), .mux_val = (v) }
#define FNULL    { .name = NULL, .mux_val = 0 }

static const struct cv84x6_pin_data cv84x6_pin_data[] = {
	/* ---- G1 pins (6) ---- */
	{  "PCIE0_RESET_X",     G1, 0, 3, { F("PCIE0_RESET_X",0), F("DEBUG_8",2), F("GPIO16",3) } },
	{  "PCIE1_RESET_X",     G1, 1, 3, { F("PCIE1_RESET_X",0), F("DEBUG_9",2), F("GPIO17",3) } },
	{  "PCIE2_RESET_X",     G1, 2, 3, { F("PCIE2_RESET_X",0), F("DEBUG_10",2), F("GPIO18",3) } },
	{  "PCIE3_RESET_X",     G1, 3, 3, { F("PCIE3_RESET_X",0), F("DEBUG_11",2), F("GPIO19",3) } },
	{  "PCIE_CLKREQ_IN_X",  G1, 4, 3, { F("PCIE_CLKREQ_IN_X",0), F("DEBUG_12",2), F("GPIO20",3) } },
	{  "PCIE_WAKEUP_X",     G1, 5, 3, { F("PCIE_WAKEUP_X",0), F("DEBUG_13",2), F("GPIO21",3) } },

	/* ---- G2 pins (10) ---- */
	{  "UART0_TX",   G2,  0, 2, { F("UART0_TX",0), F("GPIO79",3) } },
	{  "UART0_RX",   G2,  1, 2, { F("UART0_RX",0), F("GPIO80",3) } },
	{  "UART0_CTS",  G2,  2, 2, { F("UART0_CTS",0), F("GPIO58",3) } },
	{  "UART0_RTS",  G2,  3, 2, { F("UART0_RTS",0), F("GPIO71",3) } },
	{  "UART1_TX",   G2,  4, 4, { F("UART1_TX",0), F("C906B1_JTAG_TDO",1), F("PWM6",2), F("GPIO81",3) } },
	{  "UART1_RX",   G2,  5, 4, { F("UART1_RX",0), F("C906B1_JTAG_TCK",1), F("PWM7",2), F("GPIO72",3) } },
	{  "UART1_CTS",  G2,  6, 4, { F("UART1_CTS",0), F("C906B1_JTAG_TDI",1), F("UART3_TX",2), F("GPIO73",3) } },
	{  "UART1_RTS",  G2,  7, 4, { F("UART1_RTS",0), F("C906B1_JTAG_TMS",1), F("UART3_RX",2), F("GPIO74",3) } },
	{  "UART2_TX",   G2,  8, 3, { F("UART2_TX",0), F("C906B1_JTAG_TRST_X",1), F("GPIO75",3) } },
	{  "UART2_RX",   G2,  9, 3, { F("UART2_RX",0), F("C906B1_JTAG_SRST_X",1), F("GPIO82",3) } },

	/* ---- G3 pins (61) ---- */
	{  "USB2_VBUS_EN0",  G3,  0, 3, { F("USB2_VBUS_EN0",0), F("PWM20",1), F("GPIO67",3) } },
	{  "USB2_VBUS_EN1",  G3,  1, 3, { F("USB2_VBUS_EN1",0), F("PWM21",1), F("GPIO68",3) } },
	{  "USB3_VBUS_EN0",  G3,  2, 2, { F("USB3_VBUS_EN0",0), F("GPIO69",3) } },
	{  "USB3_VBUS_EN1",  G3,  3, 2, { F("USB3_VBUS_EN1",0), F("GPIO70",3) } },
	{  "SATA_DEVSLP0",   G3,  4, 3, { F("SATA_DEVSLP0",0), F("DEBUG_0",2), F("GPIO57",3) } },
	{  "SATA_DEVSLP1",   G3,  5, 3, { F("SATA_DEVSLP1",0), F("DEBUG_1",2), F("GPIO9",3) } },
	{  "SATA_DEVSLP2",   G3,  6, 3, { F("SATA_DEVSLP2",0), F("DEBUG_2",2), F("GPIO10",3) } },
	{  "SATA_DEVSLP3",   G3,  7, 3, { F("SATA_DEVSLP3",0), F("DEBUG_3",2), F("GPIO11",3) } },
	{  "SATA_LED0",      G3,  8, 3, { F("SATA_LED0",0), F("DEBUG_4",2), F("GPIO12",3) } },
	{  "SATA_LED1",      G3,  9, 3, { F("SATA_LED1",0), F("DEBUG_5",2), F("GPIO13",3) } },
	{  "SATA_LED2",      G3, 10, 3, { F("SATA_LED2",0), F("DEBUG_6",2), F("GPIO14",3) } },
	{  "SATA_LED3",      G3, 11, 3, { F("SATA_LED3",0), F("DEBUG_7",2), F("GPIO15",3) } },
	{  "GPIO0",          G3, 12, 3, { F("A55_JTAG_TDO",0), F("IIC4_SDA",1), F("GPIO0",3) } },
	{  "GPIO1",          G3, 13, 3, { F("A55_JTAG_TCK",0), F("IIC4_SCL",1), F("GPIO1",3) } },
	{  "GPIO2",          G3, 14, 3, { F("A55_JTAG_TDI",0), F("UART4_TX",2), F("GPIO2",3) } },
	{  "GPIO3",          G3, 15, 3, { F("A55_JTAG_TMS",0), F("UART4_RX",2), F("GPIO3",3) } },
	{  "GPIO4",          G3, 16, 2, { F("A55_JTAG_TRST_X",0), F("GPIO4",3) } },
	{  "GPIO5",          G3, 17, 2, { F("A55_JTAG_SRST_X",0), F("GPIO5",3) } },
	{  "GPIO6",          G3, 18, 3, { F("GPIO6",0), F("DBG_IIC_SDA",1), F("UART6_TX",2) } },
	{  "GPIO7",          G3, 19, 3, { F("GPIO7",0), F("DBG_IIC_SCL",1), F("UART6_RX",2) } },
	{  "GPIO8",          G3, 20, 2, { F("GPIO8",0), F("CLK_DBG_OUT",1) } },
	{  "SD1_PWR_EN",     G3, 21, 4, { F("SD1_PWR_EN",0), F("UART5_TX",1), F("CAN1_TX",2), F("GPIO98",3) } },
	{  "SD1_CD_X",       G3, 22, 4, { F("SD1_CD_X",0), F("UART5_RX",1), F("CAN1_RX",2), F("GPIO99",3) } },
	{  "SD0_PWR_EN",     G3, 23, 3, { F("SD0_PWR_EN",0), F("UART7_TX",1), F("GPIO100",3) } },
	{  "SD0_CD_X",       G3, 24, 3, { F("SD0_CD_X",0), F("UART7_RX",1), F("GPIO101",3) } },
	{  "HDMI_DDC_SDA",   G3, 25, 4, { F("HDMI_DDC_SDA",0), F("IIC5_SDA",1), F("DEBUG_26",2), F("GPIO93",3) } },
	{  "HDMI_DDC_SCL",   G3, 26, 4, { F("HDMI_DDC_SCL",0), F("IIC5_SCL",1), F("DEBUG_27",2), F("GPIO94",3) } },
	{  "HDMI_RXSENSE",   G3, 27, 2, { F("HDMI_RXSENSE",0), F("GPIO104",3) } },
	{  "HDMI_HPD",       G3, 28, 2, { F("HDMI_HPD",0), F("GPIO76",3) } },
	{  "PWM0",           G3, 29, 3, { F("PWM0",0), F("IIC7_SDA",2), F("GPIO85",3) } },
	{  "PWM1",           G3, 30, 3, { F("PWM1",0), F("IIC7_SCL",2), F("GPIO84",3) } },
	{  "PWM2",           G3, 31, 4, { F("PWM2",0), F("CAN0_TX",1), F("IIC8_SDA",2), F("GPIO83",3) } },
	{  "PWM3",           G3, 32, 4, { F("PWM3",0), F("CAN0_RX",1), F("IIC8_SCL",2), F("GPIO78",3) } },
	{  "FANTACH0",       G3, 33, 3, { F("FANTACH0",0), F("PWM4",2), F("GPIO77",3) } },
	{  "FANTACH1",       G3, 34, 3, { F("FANTACH1",0), F("PWM5",2), F("GPIO103",3) } },
	{  "RGMII0_REFCLKO", G3, 35, 2, { F("RGMII0_REFCLKO",0), F("GPIO34",3) } },
	{  "RGMII0_IRQ",     G3, 36, 2, { F("RGMII0_IRQ",0), F("GPIO35",3) } },
	{  "RGMII0_MDC",     G3, 37, 4, { F("RGMII0_MDC",0), F("IIC9_SCL",1), F("DEBUG_17",2), F("GPIO36",3) } },
	{  "RGMII0_MDIO",    G3, 38, 4, { F("RGMII0_MDIO",0), F("IIC9_SDA",1), F("DEBUG_18",2), F("GPIO37",3) } },
	{  "RGMII1_REFCLKO", G3, 39, 3, { F("RGMII1_REFCLKO",0), F("DEBUG_28",2), F("GPIO50",3) } },
	{  "RGMII1_IRQ",     G3, 40, 3, { F("RGMII1_IRQ",0), F("DEBUG_29",2), F("GPIO51",3) } },
	{  "RGMII1_MDC",     G3, 41, 4, { F("RGMII1_MDC",0), F("IIC6_SCL",1), F("DEBUG_30",2), F("GPIO52",3) } },
	{  "RGMII1_MDIO",    G3, 42, 4, { F("RGMII1_MDIO",0), F("IIC6_SDA",1), F("DEBUG_31",2), F("GPIO53",3) } },
	{  "IIC0_SDA",       G3, 43, 2, { F("IIC0_SDA",0), F("GPIO59",3) } },
	{  "IIC0_SCL",       G3, 44, 2, { F("IIC0_SCL",0), F("GPIO60",3) } },
	{  "IIC1_SDA",       G3, 45, 3, { F("IIC1_SDA",0), F("C906B0_JTAG_TDO",1), F("GPIO61",3) } },
	{  "IIC1_SCL",       G3, 46, 4, { F("IIC1_SCL",0), F("C906B0_JTAG_TDI",1), F("I2S2_MCLK",2), F("GPIO62",3) } },
	{  "IIC2_SDA",       G3, 47, 4, { F("IIC2_SDA",0), F("C906B0_JTAG_TCK",1), F("I2S2_SCLK",2), F("GPIO63",3) } },
	{  "IIC2_SCL",       G3, 48, 4, { F("IIC2_SCL",0), F("C906B0_JTAG_TMS",1), F("I2S2_WSI",2), F("GPIO64",3) } },
	{  "IIC3_SDA",       G3, 49, 4, { F("IIC3_SDA",0), F("C906B0_JTAG_TRST_X",1), F("I2S2_SDI",2), F("GPIO65",3) } },
	{  "IIC3_SCL",       G3, 50, 4, { F("IIC3_SCL",0), F("C906B0_JTAG_SRST_X",1), F("I2S2_SDO",2), F("GPIO66",3) } },
	{  "ADC1",           G3, 51, 2, { F("ADC1",0), F("GPIO54",3) } },
	{  "ADC2",           G3, 52, 2, { F("ADC2",0), F("GPIO55",3) } },
	{  "ADC3",           G3, 53, 2, { F("ADC3",0), F("GPIO56",3) } },
	{  "RSTN",           G3, 54, 1, { F("RSTN",0) } },
	{  "OTP_EFUSE_SEL",  G3, 55, 1, { F("OTP_EFUSE_SEL",0) } },
	{  "DUMMY0",         G3, 56, 1, { F("DUMMY0",0) } },
	{  "DUMMY1",         G3, 57, 1, { F("DUMMY1",0) } },
	{  "DUMMY2",         G3, 58, 1, { F("DUMMY2",0) } },
	{  "DUMMY3",         G3, 59, 1, { F("DUMMY3",0) } },
	{  "DUMMY4",         G3, 60, 1, { F("DUMMY4",0) } },

	/* ---- G4 pins (35) ---- */
	{  "RGMII0_TXD0",   G4,  0, 3, { F("RGMII0_TXD0",0), F("SPI1_SCK",2), F("GPIO22",3) } },
	{  "RGMII0_TXD1",   G4,  1, 3, { F("RGMII0_TXD1",0), F("SPI1_CS0_X",2), F("GPIO23",3) } },
	{  "RGMII0_TXD2",   G4,  2, 3, { F("RGMII0_TXD2",0), F("SPI1_CS1_X",2), F("GPIO24",3) } },
	{  "RGMII0_TXD3",   G4,  3, 3, { F("RGMII0_TXD3",0), F("SPI2_SDO",1), F("GPIO25",3) } },
	{  "RGMII0_TXCTRL", G4,  4, 4, { F("RGMII0_TXCTRL",0), F("SPI2_SDI",1), F("DEBUG_14",2), F("GPIO26",3) } },
	{  "RGMII0_RXD0",   G4,  5, 3, { F("RGMII0_RXD0",0), F("SPI1_SDO",2), F("GPIO27",3) } },
	{  "RGMII0_RXD1",   G4,  6, 3, { F("RGMII0_RXD1",0), F("SPI1_SDI",2), F("GPIO28",3) } },
	{  "RGMII0_RXD2",   G4,  7, 3, { F("RGMII0_RXD2",0), F("SPI2_SCK",2), F("GPIO29",3) } },
	{  "RGMII0_RXD3",   G4,  8, 3, { F("RGMII0_RXD3",0), F("SPI2_CS0_X",2), F("GPIO30",3) } },
	{  "RGMII0_RXCTRL", G4,  9, 3, { F("RGMII0_RXCTRL",0), F("SPI2_CS1_X",2), F("GPIO31",3) } },
	{  "RGMII0_TXC",    G4, 10, 3, { F("RGMII0_TXC",0), F("DEBUG_15",2), F("GPIO32",3) } },
	{  "RGMII0_RXC",    G4, 11, 3, { F("RGMII0_RXC",0), F("DEBUG_16",2), F("GPIO33",3) } },
	{  "RGMII1_TXD0",   G4, 12, 3, { F("RGMII1_TXD0",0), F("SPI3_SCK",2), F("GPIO38",3) } },
	{  "RGMII1_TXD1",   G4, 13, 3, { F("RGMII1_TXD1",0), F("SPI3_CS0_X",2), F("GPIO39",3) } },
	{  "RGMII1_TXD2",   G4, 14, 3, { F("RGMII1_TXD2",0), F("SPI3_CS1_X",2), F("GPIO40",3) } },
	{  "RGMII1_TXD3",   G4, 15, 3, { F("RGMII1_TXD3",0), F("DEBUG_19",2), F("GPIO41",3) } },
	{  "RGMII1_TXCTRL", G4, 16, 3, { F("RGMII1_TXCTRL",0), F("DEBUG_20",2), F("GPIO42",3) } },
	{  "RGMII1_RXD0",   G4, 17, 3, { F("RGMII1_RXD0",0), F("SPI3_SDO",2), F("GPIO43",3) } },
	{  "RGMII1_RXD1",   G4, 18, 3, { F("RGMII1_RXD1",0), F("SPI3_SDI",2), F("GPIO44",3) } },
	{  "RGMII1_RXD2",   G4, 19, 3, { F("RGMII1_RXD2",0), F("DEBUG_21",2), F("GPIO45",3) } },
	{  "RGMII1_RXD3",   G4, 20, 3, { F("RGMII1_RXD3",0), F("DEBUG_22",2), F("GPIO46",3) } },
	{  "RGMII1_RXCTRL", G4, 21, 3, { F("RGMII1_RXCTRL",0), F("DEBUG_23",2), F("GPIO47",3) } },
	{  "RGMII1_TXC",    G4, 22, 3, { F("RGMII1_TXC",0), F("DEBUG_24",2), F("GPIO48",3) } },
	{  "RGMII1_RXC",    G4, 23, 3, { F("RGMII1_RXC",0), F("DEBUG_25",2), F("GPIO49",3) } },
	{  "SPI0_CS0_X",    G4, 24, 3, { F("SPI0_CS0_X",0), F("I2S1_SCLK",1), F("GPIO102",3) } },
	{  "SPI0_CS1_X",    G4, 25, 3, { F("SPI0_CS1_X",0), F("I2S1_WSI",1), F("GPIO97",3) } },
	{  "SPI0_SDI",      G4, 26, 3, { F("SPI0_SDI",0), F("I2S1_SDI",1), F("GPIO96",3) } },
	{  "SPI0_SDO",      G4, 27, 3, { F("SPI0_SDO",0), F("I2S1_SDO",1), F("GPIO95",3) } },
	{  "SPI0_SCK",      G4, 28, 3, { F("SPI0_SCK",0), F("I2S1_MCLK",1), F("GPIO92",3) } },
	{  "I2S0_SCLK",     G4, 29, 4, { F("I2S0_SCLK",0), F("SPINOR_CS_X",1), F("SPINAND_CS_X",2), F("GPIO91",3) } },
	{  "I2S0_WSI",      G4, 30, 4, { F("I2S0_WSI",0), F("SPINOR_SDI",1), F("SPINAND_SDI",2), F("GPIO90",3) } },
	{  "I2S0_SDI0",     G4, 31, 4, { F("I2S0_SDI0",0), F("SPINOR_SDO",1), F("SPINAND_SDO",2), F("GPIO89",3) } },
	{  "I2S0_SDI1",     G4, 32, 4, { F("I2S0_SDI1",0), F("SPINOR_WP_X",1), F("SPINAND_WP_X",2), F("GPIO88",3) } },
	{  "I2S0_SDO",      G4, 33, 4, { F("I2S0_SDO",0), F("SPINOR_HOLD_X",1), F("SPINAND_HOLD_X",2), F("GPIO87",3) } },
	{  "I2S0_MCLK",     G4, 34, 4, { F("I2S0_MCLK",0), F("SPINOR_SCK",1), F("SPINAND_SCK",2), F("GPIO86",3) } },

	/* ---- G7 pins (2) ---- */
	{  "PWR_UART_TX",   G7,  0, 2, { F("PWR_UART_TX",0), F("PWR_GPIO16",3) } },
	{  "PWR_UART_RX",   G7,  1, 2, { F("PWR_UART_RX",0), F("PWR_GPIO17",3) } },

	/* ---- G8 pins (25) ---- */
	{  "PWR_RSTN",        G8,  0, 1, { F("PWR_RSTN",0) } },
	{  "PWR_ON",          G8,  1, 2, { F("PWR_ON",0), F("PWR_GPIO8",3) } },
	{  "PWR_BUTTON1",     G8,  2, 2, { F("PWR_BUTTON1",0), F("PWR_GPIO9",3) } },
	{  "PWR_WAKEUP0",     G8,  3, 2, { F("PWR_WAKEUP0",0), F("PWR_GPIO10",3) } },
	{  "PWR_SEQ1",        G8,  4, 2, { F("PWR_SEQ1",0), F("PWR_GPIO11",3) } },
	{  "PWR_SEQ2",        G8,  5, 2, { F("PWR_SEQ2",0), F("PWR_GPIO12",3) } },
	{  "PWR_SEQ3",        G8,  6, 2, { F("PWR_SEQ3",0), F("PWR_GPIO13",3) } },
	{  "PWR_IIC_SDA",     G8,  7, 2, { F("PWR_IIC_SDA",0), F("PWR_GPIO14",3) } },
	{  "PWR_IIC_SCL",     G8,  8, 2, { F("PWR_IIC_SCL",0), F("PWR_GPIO15",3) } },
	{  "PWR_GPIO0",       G8,  9, 4, { F("PWR_SPINOR_HOLD_X",0), F("USB_BOOT",1), F("PWR_GPIO0",2), F("PWR_GPIO18",3) } },
	{  "PWR_GPIO1",       G8, 10, 4, { F("PWR_SPINOR_WP_X",0), F("PWR_SAR1",1), F("PWR_GPIO1",2), F("PWR_GPIO19",3) } },
	{  "PWR_GPIO2",       G8, 11, 4, { F("PWR_SPINOR_CS_X",0), F("PWR_SAR2",1), F("PWR_GPIO2",2), F("PWR_GPIO20",3) } },
	{  "PWR_GPIO3",       G8, 12, 3, { F("PWR_SPINOR_SDI",0), F("PWR_GPIO3",2), F("PWR_GPIO21",3) } },
	{  "PWR_GPIO4",       G8, 13, 3, { F("PWR_SPINOR_SDO",0), F("PWR_GPIO4",2), F("PWR_GPIO22",3) } },
	{  "PWR_GPIO5",       G8, 14, 3, { F("PWR_SPINOR_SCK",0), F("PWR_GPIO5",2), F("PWR_GPIO23",3) } },
	{  "PWR_GPIO6",       G8, 15, 2, { F("PWR_GPIO6",0), F("PWR_GPIO24",3) } },
	{  "PWR_AVS0_CLK",    G8, 16, 3, { F("PWR_AVS0_CLK",0), F("PWR_PMBUS1_ALERT",1), F("PWR_GPIO25",3) } },
	{  "PWR_AVS0_SDI",    G8, 17, 3, { F("PWR_AVS0_SDI",0), F("PWR_PMBUS1_SCL",1), F("PWR_GPIO26",3) } },
	{  "PWR_AVS0_SDO",    G8, 18, 3, { F("PWR_AVS0_SDO",0), F("PWR_PMBUS1_SDA",1), F("PWR_GPIO27",3) } },
	{  "HW_BISR_BYP",     G8, 19, 1, { F("HW_BISR_BYP",0) } },
	{  "PWR_PMBUS_ALERT", G8, 20, 2, { F("PWR_PMBUS0_ALERT",0), F("PWR_GPIO7",3) } },
	{  "PWR_PMBUS_SCL",   G8, 21, 1, { F("PWR_PMBUS0_SCL",0) } },
	{  "PWR_PMBUS_SDA",   G8, 22, 1, { F("PWR_PMBUS0_SDA",0) } },
	{  "CLK_25M_IN",      G8, 23, 1, { F("CLK_25M_IN",0) } },
	{  "PTEST",           G8, 24, 1, { F("PTEST",0) } },
};

#undef F
#undef FNULL

static struct pinctrl_pin_desc cv84x6_pins[CV84X6_TOTAL_PINS];

/* ======================================================================
 * Function groups
 *
 * Each "function" (e.g. uart0, i2c0, spi0) has one or more pin groups.
 * A pin group is the set of pins that must be configured together
 * for that function.
 * ====================================================================== */

/* Helper: single-pin group for GPIO function (every pin can be GPIO) */

/* UART groups */
static const unsigned int uart0_pins[] = { G2_PIN_BASE + 0, G2_PIN_BASE + 1 };
static const unsigned int uart0_flow_pins[] = { G2_PIN_BASE + 0, G2_PIN_BASE + 1,
						G2_PIN_BASE + 2, G2_PIN_BASE + 3 };
static const unsigned int uart1_pins[] = { G2_PIN_BASE + 4, G2_PIN_BASE + 5 };
static const unsigned int uart1_flow_pins[] = { G2_PIN_BASE + 4, G2_PIN_BASE + 5,
						G2_PIN_BASE + 6, G2_PIN_BASE + 7 };
static const unsigned int uart2_pins[] = { G2_PIN_BASE + 8, G2_PIN_BASE + 9 };
static const unsigned int uart3_pins[] = { G2_PIN_BASE + 6, G2_PIN_BASE + 7 };
static const unsigned int uart4_pins[] = { G3_PIN_BASE + 14, G3_PIN_BASE + 15 };
static const unsigned int uart5_pins[] = { G3_PIN_BASE + 21, G3_PIN_BASE + 22 };
static const unsigned int uart6_pins[] = { G3_PIN_BASE + 18, G3_PIN_BASE + 19 };
static const unsigned int uart7_pins[] = { G3_PIN_BASE + 23, G3_PIN_BASE + 24 };

/* I2C groups */
static const unsigned int i2c0_pins[] = { G3_PIN_BASE + 43, G3_PIN_BASE + 44 };
static const unsigned int i2c1_pins[] = { G3_PIN_BASE + 45, G3_PIN_BASE + 46 };
static const unsigned int i2c2_pins[] = { G3_PIN_BASE + 47, G3_PIN_BASE + 48 };
static const unsigned int i2c3_pins[] = { G3_PIN_BASE + 49, G3_PIN_BASE + 50 };
static const unsigned int i2c4_pins[] = { G3_PIN_BASE + 12, G3_PIN_BASE + 13 };
static const unsigned int i2c5_pins[] = { G3_PIN_BASE + 25, G3_PIN_BASE + 26 };
static const unsigned int i2c6_pins[] = { G3_PIN_BASE + 41, G3_PIN_BASE + 42 };
static const unsigned int i2c7_pins[] = { G3_PIN_BASE + 29, G3_PIN_BASE + 30 };
static const unsigned int i2c8_pins[] = { G3_PIN_BASE + 31, G3_PIN_BASE + 32 };
static const unsigned int i2c9_pins[] = { G3_PIN_BASE + 37, G3_PIN_BASE + 38 };

/* SPI groups */
static const unsigned int spi0_pins[] = { G4_PIN_BASE + 24, G4_PIN_BASE + 25,
					  G4_PIN_BASE + 26, G4_PIN_BASE + 27,
					  G4_PIN_BASE + 28 };
static const unsigned int spi1_pins[] = { G4_PIN_BASE + 0, G4_PIN_BASE + 1,
					  G4_PIN_BASE + 2, G4_PIN_BASE + 5,
					  G4_PIN_BASE + 6 };
static const unsigned int spi2_pins[] = { G4_PIN_BASE + 3, G4_PIN_BASE + 4,
					  G4_PIN_BASE + 7, G4_PIN_BASE + 8,
					  G4_PIN_BASE + 9 };
static const unsigned int spi3_pins[] = { G4_PIN_BASE + 12, G4_PIN_BASE + 13,
					  G4_PIN_BASE + 14, G4_PIN_BASE + 17,
					  G4_PIN_BASE + 18 };

/* RGMII groups (data + control pins) */
static const unsigned int rgmii0_pins[] = {
	G4_PIN_BASE + 0, G4_PIN_BASE + 1, G4_PIN_BASE + 2, G4_PIN_BASE + 3,
	G4_PIN_BASE + 4, G4_PIN_BASE + 5, G4_PIN_BASE + 6, G4_PIN_BASE + 7,
	G4_PIN_BASE + 8, G4_PIN_BASE + 9, G4_PIN_BASE + 10, G4_PIN_BASE + 11,
	G3_PIN_BASE + 35, G3_PIN_BASE + 36, G3_PIN_BASE + 37, G3_PIN_BASE + 38,
};
static const unsigned int rgmii1_pins[] = {
	G4_PIN_BASE + 12, G4_PIN_BASE + 13, G4_PIN_BASE + 14, G4_PIN_BASE + 15,
	G4_PIN_BASE + 16, G4_PIN_BASE + 17, G4_PIN_BASE + 18, G4_PIN_BASE + 19,
	G4_PIN_BASE + 20, G4_PIN_BASE + 21, G4_PIN_BASE + 22, G4_PIN_BASE + 23,
	G3_PIN_BASE + 39, G3_PIN_BASE + 40, G3_PIN_BASE + 41, G3_PIN_BASE + 42,
};

/* I2S groups */
static const unsigned int i2s0_pins[] = { G4_PIN_BASE + 29, G4_PIN_BASE + 30,
					  G4_PIN_BASE + 31, G4_PIN_BASE + 32,
					  G4_PIN_BASE + 33, G4_PIN_BASE + 34 };
static const unsigned int i2s1_pins[] = { G4_PIN_BASE + 24, G4_PIN_BASE + 25,
					  G4_PIN_BASE + 26, G4_PIN_BASE + 27,
					  G4_PIN_BASE + 28 };
static const unsigned int i2s2_pins[] = { G3_PIN_BASE + 46, G3_PIN_BASE + 47,
					  G3_PIN_BASE + 48, G3_PIN_BASE + 49,
					  G3_PIN_BASE + 50 };

/* SPINOR / SPINAND groups */
static const unsigned int spinor_pins[] = { G4_PIN_BASE + 29, G4_PIN_BASE + 30,
					    G4_PIN_BASE + 31, G4_PIN_BASE + 32,
					    G4_PIN_BASE + 33, G4_PIN_BASE + 34 };
static const unsigned int spinand_pins[] = { G4_PIN_BASE + 29, G4_PIN_BASE + 30,
					     G4_PIN_BASE + 31, G4_PIN_BASE + 32,
					     G4_PIN_BASE + 33, G4_PIN_BASE + 34 };

/* CAN groups */
static const unsigned int can0_pins[] = { G3_PIN_BASE + 31, G3_PIN_BASE + 32 };
static const unsigned int can1_pins[] = { G3_PIN_BASE + 21, G3_PIN_BASE + 22 };

/* PWM groups (each PWM is a single pin) */
static const unsigned int pwm0_pin[] = { G3_PIN_BASE + 29 };
static const unsigned int pwm1_pin[] = { G3_PIN_BASE + 30 };
static const unsigned int pwm2_pin[] = { G3_PIN_BASE + 31 };
static const unsigned int pwm3_pin[] = { G3_PIN_BASE + 32 };
static const unsigned int pwm4_pin[] = { G3_PIN_BASE + 33 };
static const unsigned int pwm5_pin[] = { G3_PIN_BASE + 34 };
static const unsigned int pwm6_pin[] = { G2_PIN_BASE + 4 };
static const unsigned int pwm7_pin[] = { G2_PIN_BASE + 5 };
static const unsigned int pwm20_pin[] = { G3_PIN_BASE + 0 };
static const unsigned int pwm21_pin[] = { G3_PIN_BASE + 1 };

/* SE13 LED pins: SYS red/green, alarm, SSD red/green — all GPIO (mux=3) */
static const unsigned int led_pins[] = {
	G3_PIN_BASE + 30,  /* PWM1 / GPIO84   SYS red  */
	G3_PIN_BASE + 34,  /* FANTACH1 / GPIO103  SYS green */
	G4_PIN_BASE + 32,  /* I2S0_SDI1 / GPIO88  alarm */
	G3_PIN_BASE + 2,   /* USB3_VBUS_EN0 / GPIO69  SSD red */
	G3_PIN_BASE + 3,   /* USB3_VBUS_EN1 / GPIO70  SSD green */
};

/* PCIE groups */
static const unsigned int pcie_pins[] = { G1_PIN_BASE + 0, G1_PIN_BASE + 1,
					  G1_PIN_BASE + 2, G1_PIN_BASE + 3,
					  G1_PIN_BASE + 4, G1_PIN_BASE + 5 };

/* HDMI groups */
static const unsigned int hdmi_ddc_pins[] = { G3_PIN_BASE + 25, G3_PIN_BASE + 26 };
static const unsigned int hdmi_pins[] = { G3_PIN_BASE + 25, G3_PIN_BASE + 26,
					  G3_PIN_BASE + 27, G3_PIN_BASE + 28 };

/* SD groups */
static const unsigned int sd0_pins[] = { G3_PIN_BASE + 23, G3_PIN_BASE + 24 };
static const unsigned int sd1_pins[] = { G3_PIN_BASE + 21, G3_PIN_BASE + 22 };

/* PWR domain groups */
static const unsigned int pwr_uart_pins[] = { G7_PIN_BASE + 0, G7_PIN_BASE + 1 };
static const unsigned int pwr_iic_pins[] = { G8_PIN_BASE + 7, G8_PIN_BASE + 8 };
static const unsigned int pwr_spinor_pins[] = { G8_PIN_BASE + 9, G8_PIN_BASE + 10,
						G8_PIN_BASE + 11, G8_PIN_BASE + 12,
						G8_PIN_BASE + 13, G8_PIN_BASE + 14 };

/* FANTACH groups */
static const unsigned int fantach0_pin[] = { G3_PIN_BASE + 33 };
static const unsigned int fantach1_pin[] = { G3_PIN_BASE + 34 };

/* ADC groups */
static const unsigned int adc_pins[] = { G3_PIN_BASE + 51, G3_PIN_BASE + 52, G3_PIN_BASE + 53 };

/* USB groups */
static const unsigned int usb2_vbus_pins[] = { G3_PIN_BASE + 0, G3_PIN_BASE + 1 };
static const unsigned int usb3_vbus_pins[] = { G3_PIN_BASE + 2, G3_PIN_BASE + 3 };

/* SATA devslp groups */
static const unsigned int sata_devslp_pins[] = { G3_PIN_BASE + 4, G3_PIN_BASE + 5,
						 G3_PIN_BASE + 6, G3_PIN_BASE + 7 };
static const unsigned int sata_led_pins[] = { G3_PIN_BASE + 8, G3_PIN_BASE + 9,
					      G3_PIN_BASE + 10, G3_PIN_BASE + 11 };

/* PMBus groups */
static const unsigned int pwr_avs0_pins[] = { G8_PIN_BASE + 16, G8_PIN_BASE + 17,
					      G8_PIN_BASE + 18 };
static const unsigned int pwr_pmbus0_pins[] = { G8_PIN_BASE + 20, G8_PIN_BASE + 21,
						G8_PIN_BASE + 22 };
static const unsigned int pwr_pmbus1_pins[] = { G8_PIN_BASE + 16, G8_PIN_BASE + 17,
						G8_PIN_BASE + 18 };

/* DBG IIC */
static const unsigned int dbg_iic_pins[] = { G3_PIN_BASE + 18, G3_PIN_BASE + 19 };

/* JTAG groups */
static const unsigned int a55_jtag_pins[] = { G3_PIN_BASE + 12, G3_PIN_BASE + 13,
					      G3_PIN_BASE + 14, G3_PIN_BASE + 15,
					      G3_PIN_BASE + 16, G3_PIN_BASE + 17 };
static const unsigned int c906b0_jtag_pins[] = { G3_PIN_BASE + 45, G3_PIN_BASE + 46,
						 G3_PIN_BASE + 47, G3_PIN_BASE + 48,
						 G3_PIN_BASE + 49, G3_PIN_BASE + 50 };
static const unsigned int c906b1_jtag_pins[] = { G2_PIN_BASE + 4, G2_PIN_BASE + 5,
						 G2_PIN_BASE + 6, G2_PIN_BASE + 7,
						 G2_PIN_BASE + 8, G2_PIN_BASE + 9 };

/*
 * Per-pin mux field (see CV84X6_PIN_MUX_*) for each entry in the matching
 * *_pins[] array when this pin group is enabled for its function.
 * Source: cv84x6_pin_data[] alternate-function table / TRM.
 */
static const u8 uart0_grp_mux[] = { 0, 0 };
static const u8 uart0_flow_grp_mux[] = { 0, 0, 0, 0 };
static const u8 uart1_grp_mux[] = { 0, 0 };
static const u8 uart1_flow_grp_mux[] = { 0, 0, 0, 0 };
static const u8 uart2_grp_mux[] = { 0, 0 };
static const u8 uart3_grp_mux[] = { 2, 2 };
static const u8 uart4_grp_mux[] = { 2, 2 };
static const u8 uart5_grp_mux[] = { 1, 1 };
static const u8 uart6_grp_mux[] = { 2, 2 };
static const u8 uart7_grp_mux[] = { 1, 1 };
static const u8 i2c0_grp_mux[] = { 0, 0 };
static const u8 i2c1_grp_mux[] = { 0, 0 };
static const u8 i2c2_grp_mux[] = { 0, 0 };
static const u8 i2c3_grp_mux[] = { 0, 0 };
static const u8 i2c4_grp_mux[] = { 1, 1 };
static const u8 i2c5_grp_mux[] = { 1, 1 };
static const u8 i2c6_grp_mux[] = { 1, 1 };
static const u8 i2c7_grp_mux[] = { 2, 2 };
static const u8 i2c8_grp_mux[] = { 2, 2 };
static const u8 i2c9_grp_mux[] = { 1, 1 };
static const u8 spi0_grp_mux[] = { 0, 0, 0, 0, 0 };
static const u8 spi1_grp_mux[] = { 2, 2, 2, 2, 2 };
static const u8 spi2_grp_mux[] = { 1, 1, 2, 2, 2 };
static const u8 spi3_grp_mux[] = { 2, 2, 2, 2, 2 };
static const u8 rgmii0_grp_mux[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const u8 rgmii1_grp_mux[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const u8 i2s0_grp_mux[] = { 0, 0, 0, 0, 0, 0 };
static const u8 i2s1_grp_mux[] = { 1, 1, 1, 1, 1 };
static const u8 i2s2_grp_mux[] = { 2, 2, 2, 2, 2 };
static const u8 spinor_grp_mux[] = { 1, 1, 1, 1, 1, 1 };
static const u8 spinand_grp_mux[] = { 2, 2, 2, 2, 2, 2 };
static const u8 can0_grp_mux[] = { 1, 1 };
static const u8 can1_grp_mux[] = { 2, 2 };
static const u8 pwm0_grp_mux[] = { 0 };
static const u8 pwm1_grp_mux[] = { 0 };
static const u8 pwm2_grp_mux[] = { 0 };
static const u8 pwm3_grp_mux[] = { 0 };
static const u8 pwm4_grp_mux[] = { 2 };
static const u8 pwm5_grp_mux[] = { 2 };
static const u8 pwm6_grp_mux[] = { 2 };
static const u8 pwm7_grp_mux[] = { 2 };
static const u8 pwm20_grp_mux[] = { 1 };
static const u8 pwm21_grp_mux[] = { 1 };
static const u8 pcie_grp_mux[] = { 0, 0, 0, 0, 0, 0 };
static const u8 hdmi_ddc_grp_mux[] = { 0, 0 };
static const u8 hdmi_grp_mux[] = { 0, 0, 0, 0 };
static const u8 sd0_grp_mux[] = { 0, 0 };
static const u8 sd1_grp_mux[] = { 0, 0 };
static const u8 pwr_uart_grp_mux[] = { 0, 0 };
static const u8 pwr_iic_grp_mux[] = { 0, 0 };
static const u8 pwr_spinor_grp_mux[] = { 0, 0, 0, 0, 0, 0 };
static const u8 fantach0_grp_mux[] = { 0 };
static const u8 fantach1_grp_mux[] = { 0 };
static const u8 adc_grp_mux[] = { 0, 0, 0 };
static const u8 usb2_vbus_grp_mux[] = { 0, 0 };
static const u8 usb3_vbus_grp_mux[] = { 0, 0 };
static const u8 sata_devslp_grp_mux[] = { 0, 0, 0, 0 };
static const u8 sata_led_grp_mux[] = { 0, 0, 0, 0 };
static const u8 pwr_avs0_grp_mux[] = { 0, 0, 0 };
static const u8 pwr_pmbus0_grp_mux[] = { 0, 0, 0 };
static const u8 pwr_pmbus1_grp_mux[] = { 1, 1, 1 };
static const u8 dbg_iic_grp_mux[] = { 1, 1 };
static const u8 a55_jtag_grp_mux[] = { 0, 0, 0, 0, 0, 0 };
static const u8 c906b0_jtag_grp_mux[] = { 1, 1, 1, 1, 1, 1 };
static const u8 c906b1_jtag_grp_mux[] = { 1, 1, 1, 1, 1, 1 };
static const u8 led_grp_mux[] = { 3, 3, 3, 3, 3 };

#define GRP(_name, _pins, _mux) \
	{ .name = #_name, .pins = (_pins), .pin_mux = (_mux), .npins = ARRAY_SIZE(_pins) }

struct cv84x6_pin_group {
	const char *name;
	const unsigned int *pins;
	const u8 *pin_mux;
	unsigned int npins;
};

static const struct cv84x6_pin_group cv84x6_groups[] = {
	GRP(uart0_grp, uart0_pins, uart0_grp_mux),
	GRP(uart0_flow_grp, uart0_flow_pins, uart0_flow_grp_mux),
	GRP(uart1_grp, uart1_pins, uart1_grp_mux),
	GRP(uart1_flow_grp, uart1_flow_pins, uart1_flow_grp_mux),
	GRP(uart2_grp, uart2_pins, uart2_grp_mux),
	GRP(uart3_grp, uart3_pins, uart3_grp_mux),
	GRP(uart4_grp, uart4_pins, uart4_grp_mux),
	GRP(uart5_grp, uart5_pins, uart5_grp_mux),
	GRP(uart6_grp, uart6_pins, uart6_grp_mux),
	GRP(uart7_grp, uart7_pins, uart7_grp_mux),
	GRP(i2c0_grp, i2c0_pins, i2c0_grp_mux),
	GRP(i2c1_grp, i2c1_pins, i2c1_grp_mux),
	GRP(i2c2_grp, i2c2_pins, i2c2_grp_mux),
	GRP(i2c3_grp, i2c3_pins, i2c3_grp_mux),
	GRP(i2c4_grp, i2c4_pins, i2c4_grp_mux),
	GRP(i2c5_grp, i2c5_pins, i2c5_grp_mux),
	GRP(i2c6_grp, i2c6_pins, i2c6_grp_mux),
	GRP(i2c7_grp, i2c7_pins, i2c7_grp_mux),
	GRP(i2c8_grp, i2c8_pins, i2c8_grp_mux),
	GRP(i2c9_grp, i2c9_pins, i2c9_grp_mux),
	GRP(spi0_grp, spi0_pins, spi0_grp_mux),
	GRP(spi1_grp, spi1_pins, spi1_grp_mux),
	GRP(spi2_grp, spi2_pins, spi2_grp_mux),
	GRP(spi3_grp, spi3_pins, spi3_grp_mux),
	GRP(rgmii0_grp, rgmii0_pins, rgmii0_grp_mux),
	GRP(rgmii1_grp, rgmii1_pins, rgmii1_grp_mux),
	GRP(i2s0_grp, i2s0_pins, i2s0_grp_mux),
	GRP(i2s1_grp, i2s1_pins, i2s1_grp_mux),
	GRP(i2s2_grp, i2s2_pins, i2s2_grp_mux),
	GRP(spinor_grp, spinor_pins, spinor_grp_mux),
	GRP(spinand_grp, spinand_pins, spinand_grp_mux),
	GRP(can0_grp, can0_pins, can0_grp_mux),
	GRP(can1_grp, can1_pins, can1_grp_mux),
	GRP(pwm0_grp, pwm0_pin, pwm0_grp_mux),
	GRP(pwm1_grp, pwm1_pin, pwm1_grp_mux),
	GRP(pwm2_grp, pwm2_pin, pwm2_grp_mux),
	GRP(pwm3_grp, pwm3_pin, pwm3_grp_mux),
	GRP(pwm4_grp, pwm4_pin, pwm4_grp_mux),
	GRP(pwm5_grp, pwm5_pin, pwm5_grp_mux),
	GRP(pwm6_grp, pwm6_pin, pwm6_grp_mux),
	GRP(pwm7_grp, pwm7_pin, pwm7_grp_mux),
	GRP(pwm20_grp, pwm20_pin, pwm20_grp_mux),
	GRP(pwm21_grp, pwm21_pin, pwm21_grp_mux),
	GRP(pcie_grp, pcie_pins, pcie_grp_mux),
	GRP(hdmi_ddc_grp, hdmi_ddc_pins, hdmi_ddc_grp_mux),
	GRP(hdmi_grp, hdmi_pins, hdmi_grp_mux),
	GRP(sd0_grp, sd0_pins, sd0_grp_mux),
	GRP(sd1_grp, sd1_pins, sd1_grp_mux),
	GRP(pwr_uart_grp, pwr_uart_pins, pwr_uart_grp_mux),
	GRP(pwr_iic_grp, pwr_iic_pins, pwr_iic_grp_mux),
	GRP(pwr_spinor_grp, pwr_spinor_pins, pwr_spinor_grp_mux),
	GRP(fantach0_grp, fantach0_pin, fantach0_grp_mux),
	GRP(fantach1_grp, fantach1_pin, fantach1_grp_mux),
	GRP(adc_grp, adc_pins, adc_grp_mux),
	GRP(usb2_vbus_grp, usb2_vbus_pins, usb2_vbus_grp_mux),
	GRP(usb3_vbus_grp, usb3_vbus_pins, usb3_vbus_grp_mux),
	GRP(sata_devslp_grp, sata_devslp_pins, sata_devslp_grp_mux),
	GRP(sata_led_grp, sata_led_pins, sata_led_grp_mux),
	GRP(pwr_avs0_grp, pwr_avs0_pins, pwr_avs0_grp_mux),
	GRP(pwr_pmbus0_grp, pwr_pmbus0_pins, pwr_pmbus0_grp_mux),
	GRP(pwr_pmbus1_grp, pwr_pmbus1_pins, pwr_pmbus1_grp_mux),
	GRP(dbg_iic_grp, dbg_iic_pins, dbg_iic_grp_mux),
	GRP(a55_jtag_grp, a55_jtag_pins, a55_jtag_grp_mux),
	GRP(c906b0_jtag_grp, c906b0_jtag_pins, c906b0_jtag_grp_mux),
	GRP(c906b1_jtag_grp, c906b1_jtag_pins, c906b1_jtag_grp_mux),
	GRP(led_grp, led_pins, led_grp_mux),
};

/* ======================================================================
 * Function definitions
 * ====================================================================== */

#define FUNC_GROUPS(_fname, ...) \
	static const char * const _fname##_func_groups[] = { __VA_ARGS__ }

FUNC_GROUPS(uart0, "uart0_grp", "uart0_flow_grp");
FUNC_GROUPS(uart1, "uart1_grp", "uart1_flow_grp");
FUNC_GROUPS(uart2, "uart2_grp");
FUNC_GROUPS(uart3, "uart3_grp");
FUNC_GROUPS(uart4, "uart4_grp");
FUNC_GROUPS(uart5, "uart5_grp");
FUNC_GROUPS(uart6, "uart6_grp");
FUNC_GROUPS(uart7, "uart7_grp");
FUNC_GROUPS(i2c0, "i2c0_grp");
FUNC_GROUPS(i2c1, "i2c1_grp");
FUNC_GROUPS(i2c2, "i2c2_grp");
FUNC_GROUPS(i2c3, "i2c3_grp");
FUNC_GROUPS(i2c4, "i2c4_grp");
FUNC_GROUPS(i2c5, "i2c5_grp");
FUNC_GROUPS(i2c6, "i2c6_grp");
FUNC_GROUPS(i2c7, "i2c7_grp");
FUNC_GROUPS(i2c8, "i2c8_grp");
FUNC_GROUPS(i2c9, "i2c9_grp");
FUNC_GROUPS(spi0, "spi0_grp");
FUNC_GROUPS(spi1, "spi1_grp");
FUNC_GROUPS(spi2, "spi2_grp");
FUNC_GROUPS(spi3, "spi3_grp");
FUNC_GROUPS(rgmii0, "rgmii0_grp");
FUNC_GROUPS(rgmii1, "rgmii1_grp");
FUNC_GROUPS(i2s0, "i2s0_grp");
FUNC_GROUPS(i2s1, "i2s1_grp");
FUNC_GROUPS(i2s2, "i2s2_grp");
FUNC_GROUPS(spinor, "spinor_grp");
FUNC_GROUPS(spinand, "spinand_grp");
FUNC_GROUPS(can0, "can0_grp");
FUNC_GROUPS(can1, "can1_grp");
FUNC_GROUPS(pwm0, "pwm0_grp");
FUNC_GROUPS(pwm1, "pwm1_grp");
FUNC_GROUPS(pwm2, "pwm2_grp");
FUNC_GROUPS(pwm3, "pwm3_grp");
FUNC_GROUPS(pwm4, "pwm4_grp");
FUNC_GROUPS(pwm5, "pwm5_grp");
FUNC_GROUPS(pwm6, "pwm6_grp");
FUNC_GROUPS(pwm7, "pwm7_grp");
FUNC_GROUPS(pwm20, "pwm20_grp");
FUNC_GROUPS(pwm21, "pwm21_grp");
FUNC_GROUPS(pcie, "pcie_grp");
FUNC_GROUPS(hdmi, "hdmi_grp", "hdmi_ddc_grp");
FUNC_GROUPS(sd0, "sd0_grp");
FUNC_GROUPS(sd1, "sd1_grp");
FUNC_GROUPS(pwr_uart, "pwr_uart_grp");
FUNC_GROUPS(pwr_iic, "pwr_iic_grp");
FUNC_GROUPS(pwr_spinor, "pwr_spinor_grp");
FUNC_GROUPS(fantach0, "fantach0_grp");
FUNC_GROUPS(fantach1, "fantach1_grp");
FUNC_GROUPS(adc, "adc_grp");
FUNC_GROUPS(usb2_vbus, "usb2_vbus_grp");
FUNC_GROUPS(usb3_vbus, "usb3_vbus_grp");
FUNC_GROUPS(sata_devslp, "sata_devslp_grp");
FUNC_GROUPS(sata_led, "sata_led_grp");
FUNC_GROUPS(pwr_avs0, "pwr_avs0_grp");
FUNC_GROUPS(pwr_pmbus0, "pwr_pmbus0_grp");
FUNC_GROUPS(pwr_pmbus1, "pwr_pmbus1_grp");
FUNC_GROUPS(dbg_iic, "dbg_iic_grp");
FUNC_GROUPS(a55_jtag, "a55_jtag_grp");
FUNC_GROUPS(c906b0_jtag, "c906b0_jtag_grp");
FUNC_GROUPS(c906b1_jtag, "c906b1_jtag_grp");
FUNC_GROUPS(led_gpio, "led_grp");

struct cv84x6_pmx_func {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

#define PMX_FUNC(_name) \
	{ .name = #_name, .groups = _name##_func_groups, \
	  .ngroups = ARRAY_SIZE(_name##_func_groups) }

static const struct cv84x6_pmx_func cv84x6_functions[] = {
	PMX_FUNC(uart0),
	PMX_FUNC(uart1),
	PMX_FUNC(uart2),
	PMX_FUNC(uart3),
	PMX_FUNC(uart4),
	PMX_FUNC(uart5),
	PMX_FUNC(uart6),
	PMX_FUNC(uart7),
	PMX_FUNC(i2c0),
	PMX_FUNC(i2c1),
	PMX_FUNC(i2c2),
	PMX_FUNC(i2c3),
	PMX_FUNC(i2c4),
	PMX_FUNC(i2c5),
	PMX_FUNC(i2c6),
	PMX_FUNC(i2c7),
	PMX_FUNC(i2c8),
	PMX_FUNC(i2c9),
	PMX_FUNC(spi0),
	PMX_FUNC(spi1),
	PMX_FUNC(spi2),
	PMX_FUNC(spi3),
	PMX_FUNC(rgmii0),
	PMX_FUNC(rgmii1),
	PMX_FUNC(i2s0),
	PMX_FUNC(i2s1),
	PMX_FUNC(i2s2),
	PMX_FUNC(spinor),
	PMX_FUNC(spinand),
	PMX_FUNC(can0),
	PMX_FUNC(can1),
	PMX_FUNC(pwm0),
	PMX_FUNC(pwm1),
	PMX_FUNC(pwm2),
	PMX_FUNC(pwm3),
	PMX_FUNC(pwm4),
	PMX_FUNC(pwm5),
	PMX_FUNC(pwm6),
	PMX_FUNC(pwm7),
	PMX_FUNC(pwm20),
	PMX_FUNC(pwm21),
	PMX_FUNC(pcie),
	PMX_FUNC(hdmi),
	PMX_FUNC(sd0),
	PMX_FUNC(sd1),
	PMX_FUNC(pwr_uart),
	PMX_FUNC(pwr_iic),
	PMX_FUNC(pwr_spinor),
	PMX_FUNC(fantach0),
	PMX_FUNC(fantach1),
	PMX_FUNC(adc),
	PMX_FUNC(usb2_vbus),
	PMX_FUNC(usb3_vbus),
	PMX_FUNC(sata_devslp),
	PMX_FUNC(sata_led),
	PMX_FUNC(pwr_avs0),
	PMX_FUNC(pwr_pmbus0),
	PMX_FUNC(pwr_pmbus1),
	PMX_FUNC(dbg_iic),
	PMX_FUNC(a55_jtag),
	PMX_FUNC(c906b0_jtag),
	PMX_FUNC(c906b1_jtag),
	PMX_FUNC(led_gpio),
};

/* ======================================================================
 * Register access helpers
 * ====================================================================== */

static void __iomem *cv84x6_pin_reg(struct cv84x6_pinctrl *pctrl,
				     unsigned int pin)
{
	const struct cv84x6_pin_data *pd = &cv84x6_pin_data[pin];
	int res_idx = cv84x6_group_to_res(pd->group_id);

	if (res_idx < 0 || !pctrl->groups[res_idx].base)
		return NULL;
	return pctrl->groups[res_idx].base + pd->group_index * 4;
}

static u32 cv84x6_pin_read(struct cv84x6_pinctrl *pctrl, unsigned int pin)
{
	void __iomem *reg = cv84x6_pin_reg(pctrl, pin);

	return reg ? ioread32(reg) : 0;
}

static void cv84x6_pin_write(struct cv84x6_pinctrl *pctrl, unsigned int pin,
			     u32 val)
{
	void __iomem *reg = cv84x6_pin_reg(pctrl, pin);

	if (reg)
		iowrite32(val, reg);
}

/* ======================================================================
 * pinctrl_ops
 * ====================================================================== */

static int cv84x6_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(cv84x6_groups);
}

static const char *cv84x6_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned int group)
{
	return cv84x6_groups[group].name;
}

static int cv84x6_get_group_pins(struct pinctrl_dev *pctldev,
				 unsigned int group,
				 const unsigned int **pins,
				 unsigned int *npins)
{
	*pins = cv84x6_groups[group].pins;
	*npins = cv84x6_groups[group].npins;
	return 0;
}

static int cv84x6_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **map,
				 unsigned int *num_maps)
{
	return pinconf_generic_dt_node_to_map_all(pctldev, np, map, num_maps);
}

static void cv84x6_dt_free_map(struct pinctrl_dev *pctldev,
			       struct pinctrl_map *map,
			       unsigned int num_maps)
{
	pinctrl_utils_free_map(pctldev, map, num_maps);
}

static const struct pinctrl_ops cv84x6_pinctrl_ops = {
	.get_groups_count = cv84x6_get_groups_count,
	.get_group_name = cv84x6_get_group_name,
	.get_group_pins = cv84x6_get_group_pins,
	.dt_node_to_map = cv84x6_dt_node_to_map,
	.dt_free_map = cv84x6_dt_free_map,
};

/* ======================================================================
 * pinmux_ops
 * ====================================================================== */

static int cv84x6_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(cv84x6_functions);
}

static const char *cv84x6_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned int func)
{
	return cv84x6_functions[func].name;
}

static int cv84x6_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned int func,
				      const char * const **groups,
				      unsigned int * const ngroups)
{
	*groups = cv84x6_functions[func].groups;
	*ngroups = cv84x6_functions[func].ngroups;
	return 0;
}

/*
 * Exact name match in cv84x6_pin_data[] (used by gpio_request_enable only).
 */
static int cv84x6_find_mux_val(unsigned int pin, const char *func_name)
{
	const struct cv84x6_pin_data *pd = &cv84x6_pin_data[pin];
	int i;

	for (i = 0; i < pd->nfuncs; i++) {
		if (!pd->funcs[i].name)
			break;
		if (strcasecmp(pd->funcs[i].name, func_name) == 0)
			return pd->funcs[i].mux_val;
	}
	return -EINVAL;
}

static int cv84x6_set_mux(struct pinctrl_dev *pctldev,
			   unsigned int func_selector,
			   unsigned int group_selector)
{
	struct cv84x6_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct cv84x6_pin_group *grp = &cv84x6_groups[group_selector];
	const struct cv84x6_pmx_func *fn = &cv84x6_functions[func_selector];
	unsigned int i;

	dev_info(pctrl->dev,
		 "cv84x6 set_mux: func=%s group=%s (%u pins)\n",
		 fn->name, grp->name, grp->npins);

	for (i = 0; i < grp->npins; i++) {
		unsigned int pin = grp->pins[i];
		const struct cv84x6_pin_data *pd = &cv84x6_pin_data[pin];
		u8 mux_val = grp->pin_mux[i];
		u32 val;

		val = cv84x6_pin_read(pctrl, pin);
		val &= ~CV84X6_PIN_MUX_MASK;
		val |= (mux_val << CV84X6_PIN_MUX_SHIFT);
		cv84x6_pin_write(pctrl, pin, val);

		if (i < 8 || grp->npins <= 8)
			dev_info(pctrl->dev,
				 "cv84x6 set_mux: pin %s id=%u mux_val=%u reg_after=0x%x\n",
				 pd->name, pin, mux_val,
				 cv84x6_pin_read(pctrl, pin));
	}
	if (grp->npins > 8)
		dev_info(pctrl->dev,
			 "cv84x6 set_mux: ... %u more pins (mux applied, log truncated)\n",
			 grp->npins - 8);

	return 0;
}

static int cv84x6_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int pin)
{
	struct cv84x6_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int mux_val;
	u32 val;

	/* Find the GPIO function mux value for this pin */
	mux_val = cv84x6_find_mux_val(pin, cv84x6_pin_data[pin].funcs[0].name);

	/* Look for a GPIO function in the pin's function list */
	{
		const struct cv84x6_pin_data *pd = &cv84x6_pin_data[pin];
		int i;

		for (i = 0; i < pd->nfuncs; i++) {
			if (!pd->funcs[i].name)
				break;
			if (strncasecmp(pd->funcs[i].name, "GPIO", 4) == 0 ||
			    strncasecmp(pd->funcs[i].name, "PWR_GPIO", 8) == 0) {
				mux_val = pd->funcs[i].mux_val;
				break;
			}
		}
	}

	val = cv84x6_pin_read(pctrl, pin);
	val &= ~CV84X6_PIN_MUX_MASK;
	val |= (mux_val << CV84X6_PIN_MUX_SHIFT);
	cv84x6_pin_write(pctrl, pin, val);

	return 0;
}

static const struct pinmux_ops cv84x6_pinmux_ops = {
	.get_functions_count = cv84x6_get_functions_count,
	.get_function_name = cv84x6_get_function_name,
	.get_function_groups = cv84x6_get_function_groups,
	.set_mux = cv84x6_set_mux,
	.gpio_request_enable = cv84x6_gpio_request_enable,
	.strict = true,
};

/* ======================================================================
 * pinconf_ops
 * ====================================================================== */

static int cv84x6_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin,
			      unsigned long *config)
{
	struct cv84x6_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	u32 val = cv84x6_pin_read(pctrl, pin);
	u32 arg = 0;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		if (val & CV84X6_PIN_PU_EN_MASK)
			arg = 1;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (val & CV84X6_PIN_PD_EN_MASK)
			arg = 1;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		if (!(val & CV84X6_PIN_PU_EN_MASK) &&
		    !(val & CV84X6_PIN_PD_EN_MASK))
			arg = 1;
		else
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = (val & CV84X6_PIN_DRI_MASK) >> CV84X6_PIN_DRI_SHIFT;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		arg = !!(val & CV84X6_PIN_SCT_MASK);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (val & CV84X6_PIN_OEX_MASK)
			arg = 1;
		else
			return -EINVAL;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int cv84x6_pinconf_set(struct pinctrl_dev *pctldev,
			      unsigned int pin,
			      unsigned long *configs,
			      unsigned int nconfigs)
{
	struct cv84x6_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int i;
	u32 val;

	val = cv84x6_pin_read(pctrl, pin);

	for (i = 0; i < nconfigs; i++) {
		unsigned int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			val &= ~(CV84X6_PIN_PU_EN_MASK | CV84X6_PIN_PD_EN_MASK);
			val |= CV84X6_PIN_PU_EN_MASK;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			val &= ~(CV84X6_PIN_PU_EN_MASK | CV84X6_PIN_PD_EN_MASK);
			val |= CV84X6_PIN_PD_EN_MASK;
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			val &= ~(CV84X6_PIN_PU_EN_MASK | CV84X6_PIN_PD_EN_MASK);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (arg > 15)
				return -EINVAL;
			val &= ~CV84X6_PIN_DRI_MASK;
			val |= (arg << CV84X6_PIN_DRI_SHIFT);
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			val &= ~CV84X6_PIN_SCT_MASK;
			if (arg)
				val |= CV84X6_PIN_SCT_MASK;
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			val &= ~CV84X6_PIN_OEX_MASK;
			if (arg)
				val |= CV84X6_PIN_OEX_MASK;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	cv84x6_pin_write(pctrl, pin, val);
	return 0;
}

static int cv84x6_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int group,
				    unsigned long *configs,
				    unsigned int nconfigs)
{
	const struct cv84x6_pin_group *grp = &cv84x6_groups[group];
	unsigned int i;
	int ret;

	for (i = 0; i < grp->npins; i++) {
		ret = cv84x6_pinconf_set(pctldev, grp->pins[i],
					 configs, nconfigs);
		if (ret)
			return ret;
	}
	return 0;
}

static const struct pinconf_ops cv84x6_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = cv84x6_pinconf_get,
	.pin_config_set = cv84x6_pinconf_set,
	.pin_config_group_set = cv84x6_pinconf_group_set,
};

/* ======================================================================
 * Platform driver: probe, remove, PM
 * ====================================================================== */

static const struct {
	u8 group_id;
	u32 phys_base;
} cv84x6_group_info[CV84X6_NUM_GROUPS] = {
	{ G1, G1_PINMUX_REG_REG_BASE },
	{ G2, G2_PINMUX_REG_REG_BASE },
	{ G3, G3_PINMUX_REG_REG_BASE },
	{ G4, G4_PINMUX_REG_REG_BASE },
	{ G7, G7_PINMUX_REG_REG_BASE },
	{ G8, G8_PINMUX_REG_REG_BASE },
};

static int cv84x6_pinctrl_probe(struct platform_device *pdev)
{
	struct cv84x6_pinctrl *pctrl;
	struct pinctrl_desc *desc;
	struct resource *res;
	int i, ret;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = &pdev->dev;

	/* Map all 6 group register regions */
	for (i = 0; i < CV84X6_NUM_GROUPS; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(&pdev->dev, "missing reg resource %d\n", i);
			return -EINVAL;
		}

		pctrl->groups[i].size = resource_size(res);
		pctrl->groups[i].saved_regs = devm_kzalloc(&pdev->dev,
							    pctrl->groups[i].size,
							    GFP_KERNEL);
		if (!pctrl->groups[i].saved_regs)
			return -ENOMEM;

		pctrl->groups[i].base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(pctrl->groups[i].base)) {
			dev_err(&pdev->dev, "ioremap failed for group %d\n", i);
			return PTR_ERR(pctrl->groups[i].base);
		}

		dev_dbg(&pdev->dev, "group %d: base=%p size=0x%zx\n",
			i, pctrl->groups[i].base, pctrl->groups[i].size);
	}

	/* Build pinctrl_pin_desc array from pin data */
	for (i = 0; i < CV84X6_TOTAL_PINS; i++) {
		cv84x6_pins[i].number = i;
		cv84x6_pins[i].name = cv84x6_pin_data[i].name;
		cv84x6_pins[i].drv_data = (void *)&cv84x6_pin_data[i];
	}

	desc = devm_kzalloc(&pdev->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->name = "cv84x6-pinctrl";
	desc->pins = cv84x6_pins;
	desc->npins = CV84X6_TOTAL_PINS;
	desc->pctlops = &cv84x6_pinctrl_ops;
	desc->pmxops = &cv84x6_pinmux_ops;
	desc->confops = &cv84x6_pinconf_ops;
	desc->owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(&pdev->dev, desc, pctrl,
					     &pctrl->pctldev);
	if (ret) {
		dev_err(&pdev->dev, "pinctrl register failed: %d\n", ret);
		return ret;
	}

	ret = pinctrl_enable(pctrl->pctldev);
	if (ret) {
		dev_err(&pdev->dev, "pinctrl enable failed: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pctrl);
	dev_info(&pdev->dev, "cv84x6 pinctrl registered (%d pins)\n",
		 CV84X6_TOTAL_PINS);

	return 0;
}

static int cv84x6_pinctrl_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s()\n", __func__);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cv84x6_pinctrl_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cv84x6_pinctrl *pctrl = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < CV84X6_NUM_GROUPS; i++)
		memcpy_fromio(pctrl->groups[i].saved_regs,
			      pctrl->groups[i].base,
			      pctrl->groups[i].size);

	dev_dbg(dev, "%s()\n", __func__);
	return 0;
}

static int cv84x6_pinctrl_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cv84x6_pinctrl *pctrl = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < CV84X6_NUM_GROUPS; i++)
		memcpy_toio(pctrl->groups[i].base,
			    pctrl->groups[i].saved_regs,
			    pctrl->groups[i].size);

	dev_dbg(dev, "%s()\n", __func__);
	return 0;
}

static const struct dev_pm_ops cv84x6_pinctrl_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(cv84x6_pinctrl_suspend,
				      cv84x6_pinctrl_resume)
};
#endif

static const struct of_device_id cv84x6_pinctrl_of_match[] = {
	{ .compatible = "cvitek,pinctrl-cv84x6" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cv84x6_pinctrl_of_match);

static struct platform_driver cv84x6_pinctrl_driver = {
	.probe = cv84x6_pinctrl_probe,
	.remove = cv84x6_pinctrl_remove,
	.driver = {
		.name = "cv84x6-pinctrl",
		.of_match_table = cv84x6_pinctrl_of_match,
#ifdef CONFIG_PM_SLEEP
		.pm = &cv84x6_pinctrl_pm_ops,
#endif
	},
};

module_platform_driver(cv84x6_pinctrl_driver);

MODULE_DESCRIPTION("Cvitek CV84x6 pinctrl driver");
MODULE_AUTHOR("Cvitek");
MODULE_LICENSE("GPL v2");

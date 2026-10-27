/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cv84x6 pinmux register definitions for Linux kernel
 *
 * Adapted from fsbl/include/drivers/cvitek/84x6/pinmux/cv84x6_pinmux.h
 */
#ifndef __CV84X6_PINMUX_H__
#define __CV84X6_PINMUX_H__

#include "./cv84x6_reg_G1_PINMUX_REG.h"
#include "./cv84x6_reg_G2_PINMUX_REG.h"
#include "./cv84x6_reg_G3_PINMUX_REG.h"
#include "./cv84x6_reg_G4_PINMUX_REG.h"
#include "./cv84x6_reg_G7_PINMUX_REG.h"
#include "./cv84x6_reg_G8_PINMUX_REG.h"
#include "./cv84x6_reg_PINMUX_VALUE.h"

/* Physical base addresses for each pinmux group */
#define G1_PINMUX_REG_REG_BASE	0x28104100
#define G2_PINMUX_REG_REG_BASE	0x28104200
#define G3_PINMUX_REG_REG_BASE	0x28104300
#define G4_PINMUX_REG_REG_BASE	0x28104e00
#define G7_PINMUX_REG_REG_BASE	0x05027000
#define G8_PINMUX_REG_REG_BASE	0x05027800

#define PIN_PULL_DOWN		0x0
#define PIN_PULL_UP		0x1
#define PINMUX_INVALID_SEL	0xffff

typedef enum {
	G1 = 1,
	G2 = 2,
	G3 = 3,
	G4 = 4,
	G7 = 7,
	G8 = 8,
} group_pin_t;

/* Number of pins in each group */
#define G1_PIN_COUNT	6
#define G2_PIN_COUNT	10
#define G3_PIN_COUNT	61
#define G4_PIN_COUNT	35
#define G7_PIN_COUNT	2
#define G8_PIN_COUNT	25
#define CV84X6_TOTAL_PINS (G1_PIN_COUNT + G2_PIN_COUNT + G3_PIN_COUNT + \
			   G4_PIN_COUNT + G7_PIN_COUNT + G8_PIN_COUNT)

/* Global pin ID offsets per group */
#define G1_PIN_BASE	0
#define G2_PIN_BASE	(G1_PIN_BASE + G1_PIN_COUNT)
#define G3_PIN_BASE	(G2_PIN_BASE + G2_PIN_COUNT)
#define G4_PIN_BASE	(G3_PIN_BASE + G3_PIN_COUNT)
#define G7_PIN_BASE	(G4_PIN_BASE + G4_PIN_COUNT)
#define G8_PIN_BASE	(G7_PIN_BASE + G7_PIN_COUNT)

/* Per-pin register bit field definitions */
#define CV84X6_PIN_MUX_SHIFT	4
#define CV84X6_PIN_MUX_MASK	0xf0
#define CV84X6_PIN_PU_EN_SHIFT	2
#define CV84X6_PIN_PU_EN_MASK	0x04
#define CV84X6_PIN_PD_EN_SHIFT	3
#define CV84X6_PIN_PD_EN_MASK	0x08
#define CV84X6_PIN_DRI_SHIFT	8
#define CV84X6_PIN_DRI_MASK	0x0f00
#define CV84X6_PIN_SCT_SHIFT	12
#define CV84X6_PIN_SCT_MASK	0x1000
#define CV84X6_PIN_OEX_SHIFT	13
#define CV84X6_PIN_OEX_MASK	0x2000

/* Group register region size */
#define CV84X6_GROUP_REG_SIZE	0x100

#endif /* __CV84X6_PINMUX_H__ */

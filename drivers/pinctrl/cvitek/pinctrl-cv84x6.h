/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Cvitek cv84x6 pinctrl driver header
 *
 * Copyright (C) Cvitek Co., Ltd. 2024. All rights reserved.
 */
#ifndef __PINCTRL_CV84X6_H__
#define __PINCTRL_CV84X6_H__

#include "./cv84x6/cv84x6_pinmux.h"

/* Maximum number of functions per pin (from pinlist_map definition) */
#define CV84X6_MAX_FUNCS_PER_PIN	8

/*
 * Per-pin hardware description.
 * Encodes the mux function table for each physical pin.
 */
struct cv84x6_pin_mux_func {
	const char *name;
	u8 mux_val;
};

struct cv84x6_pin_data {
	const char *name;
	u8 group_id;		/* G1=1, G2=2, G3=3, G4=4, G7=7, G8=8 */
	u8 group_index;		/* index within the group (reg offset = index * 4) */
	u8 nfuncs;
	struct cv84x6_pin_mux_func funcs[CV84X6_MAX_FUNCS_PER_PIN];
};

/* Resource indices for platform_get_resource() matching DTS reg order */
#define CV84X6_RES_G1	0
#define CV84X6_RES_G2	1
#define CV84X6_RES_G3	2
#define CV84X6_RES_G4	3
#define CV84X6_RES_G7	4
#define CV84X6_RES_G8	5
#define CV84X6_NUM_GROUPS	6

struct cv84x6_group_regs {
	void __iomem *base;
	size_t size;
	u32 *saved_regs;
};

struct cv84x6_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;
	struct cv84x6_group_regs groups[CV84X6_NUM_GROUPS];
};

/*
 * Map group_id (enum value) to resource index.
 * G1=1->0, G2=2->1, G3=3->2, G4=4->3, G7=7->4, G8=8->5
 */
static inline int cv84x6_group_to_res(u8 group_id)
{
	switch (group_id) {
	case G1: return CV84X6_RES_G1;
	case G2: return CV84X6_RES_G2;
	case G3: return CV84X6_RES_G3;
	case G4: return CV84X6_RES_G4;
	case G7: return CV84X6_RES_G7;
	case G8: return CV84X6_RES_G8;
	default: return -1;
	}
}

#endif /* __PINCTRL_CV84X6_H__ */

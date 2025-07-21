/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Rockchip Inc.
 */

#ifndef _CVI_SDMMC_OPS_H_
#define _CVI_SDMMC_OPS_H_

int cvi_emmc_transfer(u8 *buffer, unsigned int addr, unsigned int datasz, int write);

#endif

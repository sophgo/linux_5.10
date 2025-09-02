/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Created on Thu Mar 07 2024
 *
 * Copyright (c) 2024 Sophgo
 */

#ifndef	__PLAT_CV184X_H__
#define	__PLAT_CV184X_H__
#include "cvi_saradc.h"
#include "pinctrl-cv184x.h"

#define	SARADC_CHAN_NUM	 15

#define SARADC_REGS_SIZE (0x40 + 0x4)
#define SARADC_REGS_NUM (SARADC_REGS_SIZE / 4)

struct cvi_saradc_device {
	struct device *dev;
	struct reset_control *rst_saradc;
	struct iio_chan_spec iio_channels[SARADC_CHAN_NUM];
	struct clk *clk_saradc;
	void __iomem *saradc_vaddr;
	void __iomem *top_saradc0_base_addr;
	void __iomem *top_saradc1_base_addr;
	void __iomem *top_saradc2_base_addr;
	void __iomem *rtcsys_saradc0_base_addr;
	void __iomem *rtcsys_saradc1_base_addr;
	int	saradc_irq;
	spinlock_t close_lock;
	bool enable[SARADC_CHAN_NUM];
	void *private_data;
	int	channel_index;
	u32 top_saradc0_saved_regs[SARADC_REGS_NUM];
	u32 top_saradc1_saved_regs[SARADC_REGS_NUM];
	u32 top_saradc2_saved_regs[SARADC_REGS_NUM];
	u32 rtcsys_saradc0_saved_regs[SARADC_REGS_NUM];
	u32 rtcsys_saradc1_saved_regs[SARADC_REGS_NUM];
	bool filter_enable;
};

#endif /* __PLAT_CV184X_H__ */

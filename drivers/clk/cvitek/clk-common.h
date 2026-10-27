/* SPDX-License-Identifier: GPL-2.0+ */

#define REG_UNEXIST 0xFF

/* #define CV_CLK_FLAGS_ALL	(CLK_GET_RATE_NOCACHE) */
/* #define CV_CLK_FLAGS_ALL	(CLK_GET_RATE_NOCACHE | CLK_IS_CRITICAL) */
#define CV_CLK_FLAGS_ALL	(CLK_GET_RATE_NOCACHE | CLK_IGNORE_UNUSED)

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

struct cv_clock_data {
	struct regmap *syscon;
	spinlock_t *lock;
#ifdef CONFIG_PM_SLEEP
#endif /* CONFIG_PM_SLEEP */
	struct clk_hw_onecell_data hw_data;
};

struct cv_clock_data *cvi_clk_init(struct device_node *node, unsigned int num_hws);

int cvi_clk_add_hw_provider(struct device_node *node,
			    struct cv_clock_data *clk_data);

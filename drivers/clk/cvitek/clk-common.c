// SPDX-License-Identifier: GPL-2.0+

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_clk.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>

#include "clk-common.h"

static DEFINE_SPINLOCK(cv_clk_lock);

static void cvi_clk_register_parent_clkdevs(struct device_node *node)
{
	int i, of_num_clks;
	struct clk *clk;

	of_num_clks = of_clk_get_parent_count(node);
	for (i = 0; i < of_num_clks; i++) {
		clk = of_clk_get(node, i);
		if (IS_ERR(clk))
			continue;

		clk_register_clkdev(clk, __clk_get_name(clk), NULL);
		clk_put(clk);
	}
}

struct cv_clock_data *cvi_clk_init(struct device_node *node, unsigned int num_hws)
{
	struct cv_clock_data *clk_data;
	unsigned int i;

	if (!node || !num_hws)
		return ERR_PTR(-EINVAL);

	cvi_clk_register_parent_clkdevs(node);

	clk_data = kzalloc(struct_size(clk_data, hw_data.hws, num_hws),
			   GFP_KERNEL);
	if (!clk_data)
		return ERR_PTR(-ENOMEM);

	clk_data->syscon = syscon_regmap_lookup_by_phandle(node, "syscon");
	if (IS_ERR(clk_data->syscon)) {
		void __iomem *base = of_iomap(node, 0);

		/*
		 * The composite/pll helpers in this directory are syscon/regmap
		 * based. If the clock node is not a syscon, we can't safely
		 * proceed here without knowing the matching syscon node.
		 */
		if (base)
			iounmap(base);

		kfree(clk_data);
		return ERR_PTR(-ENODEV);
	}

	clk_data->lock = &cv_clk_lock;
	clk_data->hw_data.num = num_hws;

	for (i = 0; i < num_hws; i++)
		clk_data->hw_data.hws[i] = ERR_PTR(-ENOENT);

	return clk_data;
}

int cvi_clk_add_hw_provider(struct device_node *node,
			    struct cv_clock_data *clk_data)
{
	int ret;

	if (!node || IS_ERR_OR_NULL(clk_data))
		return -EINVAL;

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get,
				     &clk_data->hw_data);
	if (ret)
		pr_err("%s: unable to add hw clk provider (%d)\n",
		       __func__, ret);

	return ret;
}

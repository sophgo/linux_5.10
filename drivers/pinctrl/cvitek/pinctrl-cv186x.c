#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include "pinctrl-cv186x.h"

typedef struct {
	void __iomem *base;
	size_t regs_size;
	uint32_t *saved_regs;
} pinctrl_regs;

struct cvitek_pinctrl {
	struct device *dev;
	pinctrl_regs regs[12];
};

static int cvi_pinctrl_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct cvitek_pinctrl *pinctrl;
	int i;

	pinctrl = devm_kzalloc(&pdev->dev, sizeof(*pinctrl), GFP_KERNEL);
	if (!pinctrl)
		return -ENOMEM;

	// alloc group ip mux regs
	for (i = 0; i < 12; ++i) {
	    res = platform_get_resource(pdev, IORESOURCE_MEM, i);
	    if (!res) {
	        dev_err(&pdev->dev, "Failed to get pinctrl io resource %d\n", i);
	        return -EINVAL;
	    }

	    pinctrl->regs[i].regs_size = resource_size(res);
	    pinctrl->regs[i].saved_regs = devm_kzalloc(&pdev->dev, pinctrl->regs[i].regs_size, GFP_KERNEL);
	    if (!pinctrl->regs[i].saved_regs) {
			dev_err(&pdev->dev, "Failed to devm_kzalloc for saved_regs %d\n", i);
			return -ENOMEM;
		}

	    pinctrl->regs[i].base = devm_ioremap_resource(&pdev->dev, res);
	    if (IS_ERR(pinctrl->regs[i].base)) {
			 dev_err(&pdev->dev, "Failed to devm_ioremap_resource for regs %d\n", i);
			 return PTR_ERR(pinctrl->regs[i].base);
		}

		dev_info(&pdev->dev, "Reg %d: base=%p, size=0x%x, saved_regs=%p\n",
         i, pinctrl->regs[i].base, pinctrl->regs[i].regs_size, pinctrl->regs[i].saved_regs);
	}

	platform_set_drvdata(pdev, pinctrl);

	return 0;
}

static int cvi_pinctrl_remove(struct platform_device *pdev)
{

	dev_info(&pdev->dev, "%s()\n", __func__);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int cvitek_pinctrl_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cvitek_pinctrl *pinctrl = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < 12; ++i)
		memcpy_fromio(pinctrl->regs[i].saved_regs, pinctrl->regs[i].base,
				pinctrl->regs[i].regs_size);
	dev_info(dev, "%s()\n", __func__);
	return 0;
}

static int cvitek_pinctrl_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cvitek_pinctrl *pinctrl = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < 12; ++i)
		memcpy_fromio(pinctrl->regs[i].base, pinctrl->regs[i].saved_regs,
				pinctrl->regs[i].regs_size);
	dev_info(dev, "%s()\n", __func__);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id cvi_pinctrl_of_match[] = {
	{ .compatible = "cvitek,pinctrl-cv186x" },
	{},
};

static const struct dev_pm_ops cvitek_pinctrl_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(cvitek_pinctrl_suspend,
				cvitek_pinctrl_resume)
};

static struct platform_driver cvi_pinctrl_driver = {
	.probe = cvi_pinctrl_probe,
	.remove	= cvi_pinctrl_remove,
	.driver = {
		.name = "cvitek,pinctrl-cv186x",
		.of_match_table = cvi_pinctrl_of_match,
#ifdef CONFIG_PM_SLEEP
		.pm	= &cvitek_pinctrl_pm_ops,
#endif
	},
};

module_platform_driver(cvi_pinctrl_driver);

MODULE_DESCRIPTION("Cvitek pinctrl");
MODULE_AUTHOR("Cvitek");
MODULE_LICENSE("GPL v2");

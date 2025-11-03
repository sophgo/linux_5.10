#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/proc_fs.h>
#include "plat_i2s_subsys.h"

struct i2s_subsys_obj *dev;
void __iomem *subsys_reg;

typedef struct _audio_power_ctrl_ {
	struct clk *aud_clk[6];
	struct clk *aud_src;
} aud_power_ctrl;

static const char *const aud_clk_name[8] = {
		"clk_aud0", "clk_aud1", "clk_aud2", "clk_aud3"
};

static aud_power_ctrl aud_pwm_ctrl = {0};

int aud_register_clk(u32 freq)
{
	int ret;
	int i;
	struct clk *clk_a0pll;

	for (i = 0; i < 4; i++) {
		aud_pwm_ctrl.aud_clk[i] = devm_clk_get(dev->dev, aud_clk_name[i]);
		if (IS_ERR(aud_pwm_ctrl.aud_clk[i])) {
			ret = PTR_ERR(aud_pwm_ctrl.aud_clk[i]);
			dev_err(dev->dev, "failed to retrieve aud %s clock",  aud_clk_name[i]);
			return ret;
		}
	}

	clk_a0pll = devm_clk_get(dev->dev, "clk_a0pll");
	if (IS_ERR(clk_a0pll)) {
		dev_err(dev->dev, "Get clk_a0pll failed\n");
		return -1;
	}

	switch (freq) {
	case FREQ_16384_MHZ:
		dev_info(dev->dev, "Set clk_aud0~3 to 16384000\n");
		clk_set_rate(aud_pwm_ctrl.aud_clk[0], 16384000);
		clk_set_rate(aud_pwm_ctrl.aud_clk[1], 16384000);
		clk_set_rate(aud_pwm_ctrl.aud_clk[2], 16384000);
		clk_set_rate(aud_pwm_ctrl.aud_clk[3], 16384000);
		break;
	case FREQ_22579_MHZ:
		dev_info(dev->dev, "Set clk_aud0~3  to 22579200\n");
		clk_set_rate(aud_pwm_ctrl.aud_clk[0], 22579200);
		clk_set_rate(aud_pwm_ctrl.aud_clk[1], 22579200);
		clk_set_rate(aud_pwm_ctrl.aud_clk[2], 22579200);
		clk_set_rate(aud_pwm_ctrl.aud_clk[3], 22579200);
		break;
	case FREQ_24576_MHZ:
		clk_set_rate(aud_pwm_ctrl.aud_clk[0], 24576000);
		clk_set_rate(aud_pwm_ctrl.aud_clk[1], 24576000);
		clk_set_rate(aud_pwm_ctrl.aud_clk[2], 24576000);
		clk_set_rate(aud_pwm_ctrl.aud_clk[3], 24576000);
		dev_info(dev->dev, "Set clk_aud0~3 to 24576000\n");
		break;
	default:
		clk_set_rate(aud_pwm_ctrl.aud_clk[0], freq);
		clk_set_rate(aud_pwm_ctrl.aud_clk[1], freq);
		clk_set_rate(aud_pwm_ctrl.aud_clk[2], freq);
		clk_set_rate(aud_pwm_ctrl.aud_clk[3], freq);
		dev_info(dev->dev, "Unrecognised freq :%d\n", freq);
		break;
	}

	return 0;
}

int subsys_set_mclk(struct clk *i2s_clk, u32 freq)
{
	dev_info(dev->dev, "%s,%d:set clk_name:%s, clk:%d\n", __func__, __LINE__, __clk_get_name(i2s_clk), freq);

	switch (freq) {
	case FREQ_16384_MHZ:
		dev_info(dev->dev, "Set clk_sdma_aud0~5 to 16384000\n");
		clk_set_rate(i2s_clk, 16384000);
		break;
	case FREQ_22579_MHZ:
		struct clk *clk_a24k = devm_clk_get(dev->dev, "a24k");

		if (IS_ERR(clk_a24k)) {
			dev_err(dev->dev, "Get clk_a24k failed\n");
			return -1;
		}
		clk_set_rate(clk_a24k, 112896000);
		dev_info(dev->dev, "Set clk_sdma_aud0~5 to 22579200\n");
		clk_set_rate(i2s_clk, 22579200);
		break;
	case FREQ_24576_MHZ:
		dev_info(dev->dev, "Set clk_sdma_aud0~5 to 24576000\n");
		clk_set_rate(i2s_clk, 24576000);
		break;
	default:
		dev_info(dev->dev, "Unrecognised freq\n");
		break;
	}
	return 0;
}

u32 subsys_get_mclk(struct clk *i2s_clk)
{
	return clk_get_rate(i2s_clk);
}

void aud_clk_enable(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!__clk_is_enabled(aud_pwm_ctrl.aud_clk[i]))
			clk_prepare_enable(aud_pwm_ctrl.aud_clk[i]);
	}

}

void aud_clk_disable(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (__clk_is_enabled(aud_pwm_ctrl.aud_clk[i]))
			clk_disable_unprepare(aud_pwm_ctrl.aud_clk[i]);
	}

}

void audio_clk_debug(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	dev_info(dev, "aud_pwm_ctrl.aud_clk[0] = %d\n", clk_get_rate(aud_pwm_ctrl.aud_clk[0]));
	dev_info(dev, "aud_pwm_ctrl.aud_clk[1] = %d\n", clk_get_rate(aud_pwm_ctrl.aud_clk[1]));
	dev_info(dev, "aud_pwm_ctrl.aud_clk[2] = %d\n", clk_get_rate(aud_pwm_ctrl.aud_clk[2]));
	dev_info(dev, "aud_pwm_ctrl.aud_clk[3] = %d\n", clk_get_rate(aud_pwm_ctrl.aud_clk[3]));

	void __iomem *pll_base = ioremap(0x03002800, 0x100);
	void __iomem *clk_base = ioremap(0x03002000, 0x1000);
	u32 val = readl(pll_base);

	dev_info(dev, "PLL:\n");
	dev_info(dev, "PLL_g2_ctrl:0x%08x\n", val);

	val = readl(pll_base + 0x0c);
	dev_info(dev, "PLL_A0PLL_CSR:0x%08x\n", val);
	val = readl(pll_base + 0x50);
	dev_info(dev, "PLL_A0PLL_SSC_SYN_CTRL:0x%08x\n", val);
	val = readl(pll_base + 0x54);
	dev_info(dev, "PLL_A0PLL_SSC_SYN_SET:0x%08x\n", val);
	val = readl(pll_base + 0x58);
	dev_info(dev, "PLL_A0PLL_SSC_SYN_SPAN:0x%08x\n", val);
	val = readl(pll_base + 0x5C);
	dev_info(dev, "PLL_A0PLL_SSC_SYN_STEP:0x%08x\n", val);

	dev_info(dev, "CLK_DIV:\n");

	val = readl(clk_base + 0xb4);
	dev_info(dev, "CLK_AUD0_DIV:0x%08x\n", val);
	val = readl(clk_base + 0xb8);
	dev_info(dev, "CLK_AUD1_DIV:0x%08x\n", val);
	val = readl(clk_base + 0xbc);
	dev_info(dev, "CLK_AUD2_DIV:0x%08x\n", val);
	val = readl(clk_base + 0xc0);
	dev_info(dev, "CLK_AUD3_DIV:0x%08x\n", val);
	val = readl(clk_base + 0xb0);
	dev_info(dev, "CLK_AUDSRC_DIV:0x%08x\n", val);
	dev_info(dev, "----------------------------------:\n");

	iounmap(pll_base);
	iounmap(clk_base);
}

static int i2s_subsys_probe(struct platform_device *pdev)
{
	struct resource *res;
	u32 audio_clk;
	struct proc_dir_entry *proc_audio;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->subsys_base = devm_ioremap_resource(&pdev->dev, res);
	dev_info(&pdev->dev, "I2S get i2s_subsys_base=0x%p\n", dev->subsys_base);
	if (IS_ERR(dev->subsys_base))
		return PTR_ERR(dev->subsys_base);
	dev->dev = &pdev->dev;

	subsys_reg = dev->subsys_base;

	device_property_read_u32(&pdev->dev, "default-freq", &audio_clk);
	pr_info("get audio clk=%d\n", audio_clk);

	if (aud_register_clk(audio_clk)) {
		dev_err(dev->dev, "aud clock init failed\n");
	}

	aud_clk_enable();
	proc_audio = proc_mkdir("audio_debug", NULL);
	if (!proc_audio)
		dev_err(&pdev->dev, "create audio_debug fail\n");

#if defined(CONFIG_SND_SOC_CVITEK_CONCURRENT_I2S)
	writel(0x7114, dev->subsys_base + SCLK_IN_SEL);
	writel(0x7114, dev->subsys_base + FS_IN_SEL);
	writel(0x7554, dev->subsys_base + SDI_IN_SEL);
	writel(0x7664, dev->subsys_base + SDO_OUT_SEL);
	writel(0x0000, dev->subsys_base + MULTI_SYNC);
	writel(0x0000, dev->subsys_base + BCLK_OEN_SEL);

#elif defined(CONFIG_SND_SOC_CVITEK_PDM)
	writel(0x7614, dev->subsys_base + SCLK_IN_SEL);
	writel(0x7214, dev->subsys_base + FS_IN_SEL);
	writel(0x7614, dev->subsys_base + SDI_IN_SEL);
	writel(0x7604, dev->subsys_base + SDO_OUT_SEL);
	writel(0x0000, dev->subsys_base + MULTI_SYNC);
	writel(0x0202, dev->subsys_base + BCLK_OEN_SEL);

	writel(0x0002, dev->subsys_base + AUDIO_PDM_CTRL);
#else
	/* normal operation, use I2S1 as TX and RX */
	writel(0x7654, dev->subsys_base + SCLK_IN_SEL);
	writel(0x7654, dev->subsys_base + FS_IN_SEL);
	writel(0x7654, dev->subsys_base + SDI_IN_SEL);
	writel(0x7654, dev->subsys_base + SDO_OUT_SEL);
	writel(0x0000, dev->subsys_base + MULTI_SYNC);
	writel(0x0000, dev->subsys_base + BCLK_OEN_SEL);
#endif//CONFIG_SND_SOC_CV1835_CONCURRENT_I2S
	return 0;
}

static const struct of_device_id i2s_subsys_id_match[] = {
	{
		.compatible = "cvitek,i2s_tdm_subsys",
	},
	{},
};

#ifdef CONFIG_PM_SLEEP
static int i2s_subsys_suspend_late(struct device *t_dev)
{
	if (!dev->reg_ctx) {
		dev->reg_ctx = devm_kzalloc(dev->dev, sizeof(struct subsys_reg_context), GFP_KERNEL);
		if (!dev->reg_ctx)
			return -ENOMEM;
	}

	dev->reg_ctx->sclk_in_sel = readl(dev->subsys_base + SCLK_IN_SEL);
	dev->reg_ctx->fs_in_sel = readl(dev->subsys_base + FS_IN_SEL);
	dev->reg_ctx->sdi_in_sel = readl(dev->subsys_base + SDI_IN_SEL);
	dev->reg_ctx->sdo_out_sel = readl(dev->subsys_base + SDO_OUT_SEL);
	dev->reg_ctx->multi_sync = readl(dev->subsys_base + MULTI_SYNC);
	dev->reg_ctx->bclk_oen_sel = readl(dev->subsys_base + BCLK_OEN_SEL);
	dev->reg_ctx->pdm_ctrl = readl(dev->subsys_base + AUDIO_PDM_CTRL);
	return 0;
}

static int i2s_subsys_resume_early(struct device *t_dev)
{
	writel(dev->reg_ctx->sclk_in_sel, dev->subsys_base + SCLK_IN_SEL);
	writel(dev->reg_ctx->fs_in_sel, dev->subsys_base + FS_IN_SEL);
	writel(dev->reg_ctx->sdi_in_sel, dev->subsys_base + SDI_IN_SEL);
	writel(dev->reg_ctx->sdo_out_sel, dev->subsys_base + SDO_OUT_SEL);
	writel(dev->reg_ctx->multi_sync, dev->subsys_base + MULTI_SYNC);
	writel(dev->reg_ctx->bclk_oen_sel, dev->subsys_base + BCLK_OEN_SEL);
	writel(dev->reg_ctx->pdm_ctrl, dev->subsys_base + AUDIO_PDM_CTRL);

	return 0;
}
#else
#define i2s_subsys_suspend_late	NULL
#define i2s_subsys_resume_early	NULL
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops i2s_subsys_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(i2s_subsys_suspend_late, i2s_subsys_resume_early)
};

static struct platform_driver i2s_subsys_driver = {
	.driver = {
		.name = "i2s-subsys",
		.owner = THIS_MODULE,
		.pm = &i2s_subsys_pm_ops,
		.of_match_table = of_match_ptr(i2s_subsys_id_match),
	},
	.probe = i2s_subsys_probe,
};

static int __init i2s_subsys_init(void)
{
	return platform_driver_register(&i2s_subsys_driver);
}

arch_initcall(i2s_subsys_init);

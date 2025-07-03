// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include "cv1835_i2s_subsys.h"

struct cvi_i2s_subsys_dev *dev;
void __iomem *master_reg;

u32 i2s_subsys_query_master(void)
{
	return dev->master_id;
}

bool i2s_master_is_on(void)
{
	return readl(master_reg + I2S_ENABLE_REG);
}

void i2s_set_master_clk(u32 clk_ctrl1)
{
	if (i2s_master_is_on()) {
		if (clk_ctrl1 != readl(master_reg + I2S_CLK_CTRL1_REG))
			dev_err(dev->dev, "Master I2S is already enabled, cannot set with different bclk\n");
		return;
	}

	writel(clk_ctrl1, master_reg + I2S_CLK_CTRL1_REG);
}

void i2s_set_master_frame_setting(u32 frame_format)
{
	if (i2s_master_is_on()) {
		if (frame_format != readl(master_reg + I2S_FRAME_SETTING_REG))
			dev_err(dev->dev, "Master I2S is already enabled, cannot set with different format\n");
		return;
	}

	writel(frame_format, master_reg + I2S_FRAME_SETTING_REG);
}

void i2s_master_clk_switch_on(bool on)
{
	u32 clk_ctrl0 = readl(master_reg + I2S_CLK_CTRL0_REG);

	if (i2s_master_is_on())
		return;

	if (on) {
		writel(clk_ctrl0 | 0x140, master_reg + I2S_CLK_CTRL0_REG);
		writel(0x1, master_reg + I2S_LCRK_MASTER_REG);
	} else {
		writel(0x0, master_reg + I2S_LCRK_MASTER_REG);
		writel(clk_ctrl0 & 0x0bf, master_reg + I2S_CLK_CTRL0_REG);
	}

}

void cv1835_set_mclk(char *clk_name, u32 freq)
{
	struct clk *clk_a0pll;
	struct clk *clk_sdma_audx;
	void __iomem *a24k_reg = ioremap(0x3002890, 12);
	void __iomem *clk_sdma = ioremap(0x3002098, 14);
	u8 src_id;
#ifdef CONFIG_ARCH_CV183X_ASIC
	void __iomem *gp_reg3 = ioremap(0x0300008c, 4);
	u32 chip_id = readl(gp_reg3);
#endif

	dev_info(dev->dev, "%s,%d:set clk_name:%s, clk:%d\n", __func__, __LINE__, clk_name, freq);
	clk_a0pll = devm_clk_get(dev->dev, "clk_a0pll");
	if (IS_ERR(clk_a0pll)) {
		dev_err(dev->dev, "Get clk_a0pll failed\n");
		return;
	}

	clk_sdma_audx = devm_clk_get(dev->dev, clk_name);
	if (IS_ERR(clk_sdma_audx)) {
		dev_err(dev->dev, "Get %s failed\n", clk_name);
		return;
	}

	if (kstrtou8(clk_name + strlen(clk_name) - 1, 10, &src_id)) {
		dev_err(dev->dev, "please check input clk name:%s\n", clk_name);
	}

	writel(0x36EE8, a24k_reg + 0x4);
	writel(0x1B8F8, a24k_reg + 0x8);
	writel(0x3F, a24k_reg);

	switch (freq) {
	case CVI_16384_MHZ:
#ifdef CONFIG_ARCH_CV183X_ASIC
		if (chip_id != 0x1838) {
			dev_info(dev->dev, "Set clk_a0pll to 406425600\n");
			clk_set_rate(clk_a0pll, 406425600);
		}
#endif
		clk_set_rate(clk_sdma_audx, 16384000);
		writel(readl(clk_sdma + src_id * 4) & ~(1 << 8), clk_sdma + src_id * 4);
		break;
	case CVI_22579_MHZ:
#ifdef CONFIG_ARCH_CV183X_ASIC
		if (chip_id != 0x1838) {
			dev_info(dev->dev, "Set clk_a0pll to 406425600\n");
			clk_set_rate(clk_a0pll, 406425600);
		}
#endif
		writel(readl(clk_sdma + src_id * 4) | 1 << 8, clk_sdma + src_id * 4);
		//clk_set_rate(clk_sdma_audx, 22579200);
		clk_set_rate(clk_sdma_audx, 45158400);
		break;
	case CVI_24576_MHZ:
#ifdef CONFIG_ARCH_CV183X_ASIC
		if (chip_id != 0x1838) {
			dev_info(dev->dev, "Set clk_a0pll to 417792000\n");
			clk_set_rate(clk_a0pll, 417792000);
		}
#endif
		writel(readl(clk_sdma + src_id * 4) & ~(1 << 8), clk_sdma + src_id * 4);
		clk_set_rate(clk_sdma_audx, 24576000);
		break;
	default:
		dev_info(dev->dev, "Unrecognised freq\n");
		break;
	}
	iounmap(a24k_reg);
	iounmap(clk_sdma);
#ifdef CONFIG_ARCH_CV183X_ASIC
	iounmap(gp_reg3);
#endif
}

u32 cv1835_get_mclk(char *clk_name)
{
	struct clk *clk_sdma_audx;

	clk_sdma_audx = devm_clk_get(dev->dev, clk_name);
	if (IS_ERR(clk_sdma_audx)) {
		dev_err(dev->dev, "Get %s failed\n", clk_name);
		return -1;
	}
	return clk_get_rate(clk_sdma_audx);
}

static int i2s_subsys_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct clk *i2sclk;
	const char *clk_id;
	u32 audio_clk;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->subsys_base = devm_ioremap_resource(&pdev->dev, res);
	dev_dbg(&pdev->dev, "I2S get i2s_subsys_base=0x%p\n", dev->subsys_base);
	if (IS_ERR(dev->subsys_base))
		return PTR_ERR(dev->subsys_base);
	dev->dev = &pdev->dev;

#if defined(CONFIG_SND_SOC_CV1835_CONCURRENT_I2S)
	device_property_read_u32(&pdev->dev, "master_base", &dev->master_base);
	dev_dbg(dev->dev, "master base is 0x%x\n", dev->master_base);

	switch (dev->master_base) {
	case 0x4110000:
		dev->master_id = 1;
		break;
	case 0x4120000:
		dev->master_id = 2;
		break;
	default:
		dev_err(dev->dev, "master base is not correct!! plz set I2S1 or I2S2\n");
	}
#endif

	master_reg = ioremap(dev->master_base, 0x100);
	if (!master_reg)
		dev_err(dev->dev, "%s FAILED!!\n", __func__);

	clk_id = "i2sclk";
	i2sclk = devm_clk_get(&pdev->dev, clk_id);
	if (IS_ERR(i2sclk))
		return PTR_ERR(i2sclk);

	audio_clk = clk_get_rate(i2sclk);
	pr_info("get audio clk=%d\n", audio_clk);
	cv1835_set_mclk("clk_sdma_aud0", audio_clk);
	cv1835_set_mclk("clk_sdma_aud1", audio_clk);
	cv1835_set_mclk("clk_sdma_aud2", audio_clk);
	cv1835_set_mclk("clk_sdma_aud3", audio_clk);
#if (!defined(CONFIG_SND_SOC_CV1835_CONCURRENT_I2S) && !defined(CONFIG_SND_SOC_CV1835PDM))
	/* normal operation, use I2S1 as TX and RX */
	writel(0x7654, dev->subsys_base + SCLK_IN_SEL);
	writel(0x7654, dev->subsys_base + FS_IN_SEL);
	writel(0x7654, dev->subsys_base + SDI_IN_SEL);
	writel(0x7654, dev->subsys_base + SDO_OUT_SEL);
	writel(0x0000, dev->subsys_base + MULTI_SYNC);
	writel(0x0000, dev->subsys_base + BCLK_OEN_SEL);

#elif defined(CONFIG_SND_SOC_CV1835_CONCURRENT_I2S)
	writel(0x7114, dev->subsys_base + SCLK_IN_SEL);
	writel(0x7114, dev->subsys_base + FS_IN_SEL);
	writel(0x7554, dev->subsys_base + SDI_IN_SEL);
	writel(0x7664, dev->subsys_base + SDO_OUT_SEL);
	writel(0x0000, dev->subsys_base + MULTI_SYNC);
	writel(0x0000, dev->subsys_base + BCLK_OEN_SEL);

#elif defined(CONFIG_SND_SOC_CV1835PDM)
	writel(0x7614, dev->subsys_base + SCLK_IN_SEL);
	writel(0x7214, dev->subsys_base + FS_IN_SEL);
	writel(0x7614, dev->subsys_base + SDI_IN_SEL);
	writel(0x7604, dev->subsys_base + SDO_OUT_SEL);
	writel(0x0000, dev->subsys_base + MULTI_SYNC);
#if defined(CONFIG_ARCH_CV182X)
	writel(0x0404, dev->subsys_base + BCLK_OEN_SEL);
#else
	writel(0x0202, dev->subsys_base + BCLK_OEN_SEL);
#endif
	writel(0x0002, dev->subsys_base + AUDIO_PDM_CTRL);

#endif

	return 0;
}

#define CV182X_DAC_RESET	0xF7FFFFFF
#define CV182X_ADC_RESET	0xDFFFFFFF
#define CV182XA_DAC_RESET	0xF7FFFFFF
#define CV182XA_ADC_RESET	0xDFFFFFFF

/* while cv182x codecs transfer CIC between 64 and 128, need to reset codec first */
void cv182x_reset_dac(void)
{
	void __iomem *reset_reg = ioremap(0x03003008, 4);

	writel((readl(reset_reg) & CV182X_DAC_RESET), reset_reg);
	writel((readl(reset_reg) | ~CV182X_DAC_RESET), reset_reg);
	iounmap(reset_reg);
}

void cv182x_reset_adc(void)
{
	void __iomem *reset_reg = ioremap(0x03003008, 4);

	writel((readl(reset_reg) & CV182X_ADC_RESET), reset_reg);
	writel((readl(reset_reg) | ~CV182X_ADC_RESET), reset_reg);
	iounmap(reset_reg);
}

void cv182xa_reset_dac(void)
{
	void __iomem *reset_reg = ioremap(0x03003008, 4);

	writel((readl(reset_reg) & CV182XA_DAC_RESET), reset_reg);
	writel((readl(reset_reg) | ~CV182XA_DAC_RESET), reset_reg);
	iounmap(reset_reg);
}

void cv182xa_reset_adc(void)
{
	void __iomem *reset_reg = ioremap(0x03003008, 4);

	writel((readl(reset_reg) & CV182XA_ADC_RESET), reset_reg);
	writel((readl(reset_reg) | ~CV182XA_ADC_RESET), reset_reg);
	iounmap(reset_reg);
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
		.name = "cvitek-i2s-subsys",
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

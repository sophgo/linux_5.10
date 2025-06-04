#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/proc_fs.h>
#include <sound/soc.h>
#include <sound/control.h>
#include "codec_ioctl.h"
#include "codec_common.h"
#include "plat_i2s.h"
#include "plat_i2s_subsys.h"

static int adc_vol_list[49] = {
//	0x2000000, /* -3dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=1, Reg_gstep=0x0 */
	0x1,       /* 0dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x1 */
	0x2,       /* 1dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x2 */
	0x4,       /* 2dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x4 */
	0x8,       /* 3dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x8 */
	0x10,      /* 4dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x10 */
	0x20,      /* 5dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x20 */
	0x40,      /* 6dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x40 */
	0x80,      /* 7dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x80 */
	0x100,     /* 8dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x100 */
	0x200,     /* 9dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x200 */
	0x400,     /* 10dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x400 */
	0x800,     /* 11dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x800 */
	0x1000,    /* 12dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x1000 */
	0x2000,    /* 13dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x2000 */
	0x4000,    /* 14dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x4000 */
	0x8000,    /* 15dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x8000 */
	0x10000,    /* 16dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x10000 */
	0x20000,    /* 17dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x20000 */
	0x40000,    /* 18dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x40000 */
	0x80000,    /* 19dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x80000 */
	0x100000,    /* 20dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x100000 */
	0x200000,    /* 21dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x200000 */
	0x400000,    /* 22dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x400000 */
	0x800000,    /* 23dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x800000 */
	0x1000000,    /* 24dB: Reg_gain_rxadc=0, Reg_g6db=0, reg_g3db_rxpga=0, Reg_gstep=0x1000000 */

	0x4080000, /* 25dB: Reg_gain_rxadc=0, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x80000 */
	0x4100000, /* 26dB: Reg_gain_rxadc=0, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x100000 */
	0x4200000, /* 27dB: Reg_gain_rxadc=0, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x200000 */
	0x4400000, /* 28dB: Reg_gain_rxadc=0, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x400000 */
	0x4800000, /* 29dB: Reg_gain_rxadc=0, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x800000 */
	0x5000000, /* 30dB: Reg_gain_rxadc=0, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x1000000 */

	0xC080000, /* 31dB: Reg_gain_rxadc=1, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x80000 */
	0xC100000, /* 32dB: Reg_gain_rxadc=1, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x100000 */
	0xC200000, /* 33dB: Reg_gain_rxadc=1, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x200000 */
	0xC400000, /* 34dB: Reg_gain_rxadc=1, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x400000 */
	0xC800000, /* 35dB: Reg_gain_rxadc=1, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x800000 */
	0xD000000, /* 36dB: Reg_gain_rxadc=1, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x1000000 */

	0x14080000, /* 37dB: Reg_gain_rxadc=2, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x80000 */
	0x14100000, /* 38dB: Reg_gain_rxadc=2, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x100000 */
	0x14200000, /* 39dB: Reg_gain_rxadc=2, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x200000 */
	0x14400000, /* 40dB: Reg_gain_rxadc=2, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x400000 */
	0x14800000, /* 41dB: Reg_gain_rxadc=2, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x800000 */
	0x15000000, /* 42dB: Reg_gain_rxadc=2, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x1000000 */

	0x1C080000, /* 43dB: Reg_gain_rxadc=3, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x80000 */
	0x1C100000, /* 44dB: Reg_gain_rxadc=3, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x100000 */
	0x1C200000, /* 45dB: Reg_gain_rxadc=3, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x200000 */
	0x1C400000, /* 46dB: Reg_gain_rxadc=3, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x400000 */
	0x1C800000, /* 47dB: Reg_gain_rxadc=3, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x800000 */
	0x1D000000, /* 48dB: Reg_gain_rxadc=3, Reg_g6db=1, reg_g3db_rxpga=0, Reg_gstep=0x1000000 */
};

static inline void adc_write_reg(void __iomem *io_base, int reg, u32 val)
{
	writel(val, io_base + reg);
}

static inline u32 adc_read_reg(void  __iomem *io_base, int reg)
{
	return readl(io_base + reg);
}

//fmt low 16bit, 4bit as a group, master/slave|INV MASK|CLOKE GATE|FORMAT
static int adc_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct adc_obj *adc = snd_soc_dai_get_drvdata(dai);

	if (!adc->dev)
		dev_err(adc->dev, "dev is NULL\n");

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		dev_dbg(adc->dev, "Set ADC to MASTER mode\n");
		adc->config.role = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		dev_err(adc->dev, "Cannot set DAC to SLAVE mode, only support MASTER mode\n");
		adc->config.role = 0;
		break;
	default:
		dev_err(adc->dev, "Cannot support this role mode\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_IF:
		dev_dbg(adc->dev, "set codec to NB_IF\n");
		break;
	case SND_SOC_DAIFMT_IB_NF:
		dev_dbg(adc->dev, "set codec to IB_NF\n");
		break;
	case SND_SOC_DAIFMT_IB_IF:
		dev_dbg(adc->dev, "set codec to IB_IF\n");
		break;
	case SND_SOC_DAIFMT_NB_NF:
		dev_dbg(adc->dev, "set codec to NB_NF\n");
		break;
	default:
		dev_err(adc->dev, "Cannot support this format\n");
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dev_dbg(adc->dev, "set codec to I2S mode\n");
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dev_dbg(adc->dev, "set codec to LEFT-JUSTIFY mode\n");
		break;
	default:
		dev_err(adc->dev, "Cannot support this mode\n");
		break;
	}
	return 0;
}

static int adc_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct adc_obj *adc = snd_soc_dai_get_drvdata(dai);
	u32 ctrl1 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1) & ~AUDIO_PHY_REG_RXADC_CIC_OPT_MASK;
	u32 ana3 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA3) & ~AUDIO_PHY_REG_CTUNE_RXADC_MASK;
	void __iomem *dac = ioremap(adc->dac_addr, 0x30);
	u32 ana0 = adc_read_reg(dac, AUDIO_PHY_TXDAC_ANA0) & ~AUDIO_PHY_REG_ADDI_TXDAC_MASK;
	u32 clk = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CLK) &
			~(AUDIO_RXADC_SCK_DIV_MASK | AUDIO_RXADC_DLYEN_MASK);

	adc->config.rate = params_rate(params);
	if (adc->config.rate >= 8000 && adc->config.rate <= 48000) {
		dev_info(adc->dev, "%s, set rate to %d\n", __func__, adc->config.rate);
		//adc_set_mclk(adc, adc->config.rate);
		switch (adc->config.rate) {
		case 8000:
			ctrl1 |= RXADC_CIC_DS_512;
			ana3 |= RXADC_CTUNE_MCLK_16384;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(32) | RXADC_DLYEN(0x21); /* 16384 / 8 / 32 / 2 */
			break;
		case 11025:
			ctrl1 |= RXADC_CIC_DS_256;
			ana3 |= RXADC_CTUNE_MCLK_11298;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(16) | RXADC_DLYEN(0x17); /* 112896 / 11.025 / 32 / 2 */
			break;
		case 16000:
			ctrl1 |= RXADC_CIC_DS_256;
			ana3 |= RXADC_CTUNE_MCLK_16384;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(16) | RXADC_DLYEN(0x21); /* 16384 / 16 / 32 / 2 */
			break;
		case 22050:
			ctrl1 |= RXADC_CIC_DS_128;
			ana3 |= RXADC_CTUNE_MCLK_11298;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(8) | RXADC_DLYEN(0x17); /* 112896 / 22.05 / 32 / 2 */
			break;
		case 32000:
			ctrl1 |= RXADC_CIC_DS_128;
			ana3 |= RXADC_CTUNE_MCLK_16384;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(8) | RXADC_DLYEN(0x21); /* 16384 / 32 / 32 / 2 */
			break;
		case 44100:
			ctrl1 &= RXADC_CIC_DS_64;
			ana3 |= RXADC_CTUNE_MCLK_11298;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(4) | RXADC_DLYEN(0x17); /* 112896 / 44.1 / 32 / 2 */
			break;
		case 48000:
			ctrl1 &= RXADC_CIC_DS_64;
			ana3 |= RXADC_CTUNE_MCLK_12288;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(4) | RXADC_DLYEN(0x19); /* 16384 / 16 / 32 / 2 */
			break;
		default:
			ctrl1 |= RXADC_CIC_DS_256;
			ana3 |= RXADC_CTUNE_MCLK_16384;
			ana0 &= ADDI_TXDAC_GAIN_RATIO_1;
			clk |= RXADC_SCK_DIV(16) | RXADC_DLYEN(0x21); /* 16384 / 16 / 32 / 2 */
			dev_dbg(adc->dev, "%s, unsupported sample rate. Set with default 16KHz\n", __func__);
			break;
		}

		adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1, ctrl1);
		adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA3, ana3);
		adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CLK, clk);
		adc_write_reg(dac, AUDIO_PHY_TXDAC_ANA0, ana0);
		iounmap(dac);

		adc->config.ch_num = params_channels(params);
		adc->config.bit_depth = snd_pcm_format_width(params_format(params));
	} else {
		dev_err(adc->dev, "%s, unsupported sample rate\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int adc_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct adc_obj *adc = snd_soc_dai_get_drvdata(dai);

	//adc_clk_on(adc);
	return 0;
}

static void adc_on(struct adc_obj *adc)
{
	u32 val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);

	if ((val & AUDIO_PHY_REG_RXADC_EN_ON) | (val & AUDIO_PHY_REG_I2S_TX_EN_ON))
		dev_info(adc->dev, "ADC or I2S TX already switched ON!!, val=0x%08x\n", val);

	//val |= AUDIO_PHY_REG_RXADC_EN_ON | AUDIO_PHY_REG_I2S_TX_EN_ON;
	val |= AUDIO_PHY_REG_RXADC_EN_ON | AUDIO_PHY_REG_I2S_TX_EN_ON | AUDIO_PHY_REG_RXADC_DCB_FIX_ON;
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, val);
}

static void adc_off(struct adc_obj *adc)
{
	u32 val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);

	//val &= AUDIO_PHY_REG_RXADC_EN_OFF & AUDIO_PHY_REG_I2S_TX_EN_OFF;
	val &= AUDIO_PHY_REG_RXADC_EN_OFF & AUDIO_PHY_REG_I2S_TX_EN_OFF & AUDIO_PHY_REG_RXADC_DCB_FIX_OFF;
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, val);
}

static void adc_reset(struct adc_obj *adc)
{
	void __iomem *reset_reg = ioremap(adc->reset_info[0], 4);

	writel(readl(reset_reg) & (~(1 << adc->reset_info[1])), reset_reg);
	writel(readl(reset_reg) | (1 << adc->reset_info[1]), reset_reg);
	iounmap(reset_reg);
}

static void adc_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct adc_obj *adc = snd_soc_dai_get_drvdata(dai);

	dev_dbg(adc->dev, "%s\n", __func__);
	adc_off(adc);
	adc_reset(adc);
//	adc_clk_off(adc);
}

static int adc_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct adc_obj *adc = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	dev_dbg(adc->dev, "%s, cmd=%d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		//adc_on(adc);//move to prepare function to meet adc on(clock out) before i2s reset
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		//adc_off(adc);//move to shutdown function as adc on move to prepare
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int adc_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct adc_obj *adc = snd_soc_dai_get_drvdata(dai);
	u32 val;

	//need to rewrite the register if called adc_reset
	val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, val);

	val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7, val);
	val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8, val);

	adc_on(adc);
#ifdef CONFIG_CVI_ADC_OV_MOD
	// chang overflow mode to bypass
	val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1);
	val &= ~AUDIO_PHY_REG_RXADC_DCB_OPT_MASK;
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1, val);
#endif
	return 0;
}

static const struct snd_soc_dai_ops adc_dai_ops = {
	.hw_params	= adc_hw_params,
	.set_fmt	= adc_set_dai_fmt,
	.startup	= adc_startup,
	.shutdown	= adc_shutdown,
	.trigger	= adc_trigger,
	.prepare	= adc_prepare,
};

static struct snd_soc_dai_driver adc_dai_driver = {

	.name		= "codec_adc",
	.capture	= {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops		= &adc_dai_ops,
};

static const struct snd_kcontrol_new adc0_controls[] = {

	SOC_DOUBLE("ADC0 Power", AUDIO_PHY_RXADC_CTRL0, 0, 1, 1, 0),
	//SOC_DOUBLE("ADC0 Capture Volume", AUDIO_PHY_RXADC_ANA0, 0, 16, 24, 0),
	SOC_DOUBLE_R("ADC0 Capture Volume", AUDIO_PHY_RXADC_ANA7, AUDIO_PHY_RXADC_ANA8, 0, 48, 0),

	SOC_DOUBLE("ADC0 Capture Mute", AUDIO_PHY_RXADC_ANA2, 0, 1, 1, 0),
};

static const struct snd_kcontrol_new adc1_controls[] = {

	SOC_DOUBLE("ADC1 Power", AUDIO_PHY_RXADC_CTRL0, 0, 1, 1, 0),
	//SOC_DOUBLE("ADC1 Capture Volume", AUDIO_PHY_RXADC_ANA0, 0, 16, 24, 0),
	SOC_DOUBLE_R("ADC1 Capture Volume", AUDIO_PHY_RXADC_ANA7, AUDIO_PHY_RXADC_ANA8, 0, 48, 0),

	SOC_DOUBLE("ADC1 Capture Mute", AUDIO_PHY_RXADC_ANA2, 0, 1, 1, 0),
};

static unsigned int adc_reg_read(struct snd_soc_component *codec, unsigned int reg)
{
	int ret, lidx;
	struct adc_obj *adc = dev_get_drvdata(codec->dev);

	ret = adc_read_reg(adc->base_addr, reg);
	if (reg == AUDIO_PHY_RXADC_ANA7 || reg == AUDIO_PHY_RXADC_ANA8) {
		for (lidx = 0; lidx < 49; lidx++)
			if (ret == adc_vol_list[lidx])
				break;
		dev_dbg(adc->dev, "ADC get Vol, reg:%d,ret:%#x, idx=%d.\n", reg, ret, lidx);
	}
	dev_dbg(adc->dev, "%s reg:%d,ret:%#x.\n", __func__, reg, ret);
	return ret;
}

static int adc_reg_write(struct snd_soc_component *codec, unsigned int reg, unsigned int value)
{
	struct adc_obj *adc = dev_get_drvdata(codec->dev);
	u32 temp;

	if (reg == AUDIO_PHY_RXADC_ANA7 || reg == AUDIO_PHY_RXADC_ANA8) {
		temp = value;
		if (temp > 49)
			temp = 49;
		value = adc_vol_list[temp];
		dev_dbg(adc->dev, "Set ADC Vol, get input val=%d, output val=0x%x\n", value, temp);
	}

	adc_write_reg(adc->base_addr, reg, value);
	dev_dbg(adc->dev, "%s reg:%d,value:%#x.\n", __func__, reg, value);

	return 0;
}

//wrapper as a miscdev
static struct adc_obj *file_adc_dev(struct file *file)
{
	return container_of(file->private_data, struct adc_obj, miscdev);
}

static int adc_open(struct inode *inode, struct file *file)
{
	struct adc_obj *adc = file_adc_dev(file);

	if (mutex_lock_interruptible(&adc->mutex))
		return -EINTR;
	mutex_unlock(&adc->mutex);
	pr_debug("%s\n", __func__);
	return 0;
}

static int adc_close(struct inode *inode, struct file *file)
{
	struct adc_obj *adc = file_adc_dev(file);

	if (mutex_lock_interruptible(&adc->mutex))
		return -EINTR;
	mutex_unlock(&adc->mutex);
	pr_debug("%s\n", __func__);
	return 0;
}

static long adc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int __user *argp = (unsigned int __user *)arg;
	struct adc_obj *adc = file_adc_dev(file);
	struct cvi_vol_ctrl vol;
	u32 val, val2;
	u32 temp;
	u32 left_vol, right_vol;

	if (argp) {
		if (!copy_from_user(&val, argp, sizeof(val))) {
			if (mutex_lock_interruptible(&adc->mutex)) {
				pr_debug("adc: signal arrives while waiting for lock\n");
				return -EINTR;
			}
		} else
			return -EFAULT;
	}
	pr_debug("%s, received cmd=%u, val=%d\n", __func__, cmd, val);

	switch (cmd) {
	case ACODEC_SOFT_RESET_CTRL:
		adc_reset(adc);
		break;

	case ACODEC_SET_INPUT_VOL:
		pr_debug("adc: ACODEC_SET_INPUT_VOL\n");
		if (val < 0 || val > 24)
			pr_err("Only support range 0 [0dB] ~ 24 [48dB]\n");
		else if (val == 0) {
			/* set mute */
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2)
				| AUDIO_PHY_REG_MUTEL_ON
				| AUDIO_PHY_REG_MUTER_ON;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
			/* set left vol */
			left_vol = adc_vol_list[val];
			/* set right vol */
			right_vol = adc_vol_list[val];
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7, left_vol);
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8, right_vol);
		} else {
			/* check current vol */
			val2 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7);
			for (temp = 0; temp < 49; temp++) {
				if (val2 == adc_vol_list[temp])
					break;
			}
			if (temp == 0) {
				/* unmute */
				temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2)
					& AUDIO_PHY_REG_MUTEL_OFF
					& AUDIO_PHY_REG_MUTEL_OFF;
				adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
			}
			/* set left vol */
			left_vol = adc_vol_list[val];
			/* set right vol */
			right_vol = adc_vol_list[val];
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7, left_vol);
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8, right_vol);
		}
		break;

	case ACODEC_GET_INPUT_VOL:
		pr_debug("adc: ACODEC_GET_INPUT_VOL\n");
		/* read left vol */
		val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7);
		for (temp = 0; temp < 49; temp++) {
			if (val == adc_vol_list[temp])
				break;
		}
		if (temp == 49)
			pr_info("adc: cannot find, out of range\n");

		if (copy_to_user(argp, &temp, sizeof(temp)))
			pr_err("adc, failed to return input vol\n");
		break;

	case ACODEC_SET_I2S1_FS:
		pr_info("adc: ACODEC_SET_I2S1_FS is not support\n");
		break;

	case ACODEC_SET_MIXER_MIC:
		pr_info("ACODEC_SET_MIXER_MIC is not support\n");
		break;
	case ACODEC_SET_GAIN_MICL:
		pr_debug("adc: ACODEC_SET_GAIN_MICL\n");
		if (val < 0 || val > 51)
			pr_err("Only support range 0 [-3dB] ~ 51 [48dB]\n");
		else {
			/* set left vol */
			left_vol = adc_vol_list[val];
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7, left_vol);
		}
		break;
	case ACODEC_SET_GAIN_MICR:
		pr_debug("adc: ACODEC_SET_GAIN_MICR\n");
		if (val < 0 || val > 51)
			pr_err("Only support range 0 [-3dB] ~ 51 [48dB]\n");
		else {
			/* set right vol */
			right_vol = adc_vol_list[val];
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8, right_vol);
		}
		break;

	case ACODEC_SET_ADCL_VOL:
		if (copy_from_user(&vol, argp, sizeof(vol))) {
			if (mutex_is_locked(&adc->mutex))
				mutex_unlock(&adc->mutex);

			return -EFAULT;
		}

		pr_info("adc: ACODEC_SET_ADCL_VOL to %d, mute=%d\n", vol.vol_ctrl, vol.vol_ctrl_mute);

		if (vol.vol_ctrl_mute == 1) {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) | AUDIO_PHY_REG_MUTEL_ON;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
		} else if ((vol.vol_ctrl < 0) | (vol.vol_ctrl > 51))
			pr_err("adc-L: Only support range 0 [-3dB] ~ 51 [48dB]\n");
		else {
			/* set left vol */
			left_vol = adc_vol_list[vol.vol_ctrl];
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7, left_vol);
			/* unmute */
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTEL_OFF;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
		}
		break;

	case ACODEC_SET_ADCR_VOL:
		if (copy_from_user(&vol, argp, sizeof(vol))) {
			if (mutex_is_locked(&adc->mutex))
				mutex_unlock(&adc->mutex);
			return -EFAULT;
		}

		pr_debug("adc: ACODEC_SET_ADCR_VOL to %d, mute=%d\n", vol.vol_ctrl, vol.vol_ctrl_mute);

		if (vol.vol_ctrl_mute == 1) {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) | AUDIO_PHY_REG_MUTER_ON;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
		} else if ((vol.vol_ctrl < 0) | (vol.vol_ctrl > 51))
			pr_err("adc-R: Only support range 0 [-3dB] ~ 51 [48dB]\n");
		else {
			/* set right vol */
			right_vol = adc_vol_list[vol.vol_ctrl];
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8, right_vol);
			/* unmute */
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTER_OFF;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
		}
		break;
	case ACODEC_SET_MICL_MUTE:
		pr_debug("adc: ACODEC_SET_MICL_MUTE\n");
		if (val == 0)
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTEL_OFF;
		else
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) | AUDIO_PHY_REG_MUTEL_ON;

		adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
		break;
	case ACODEC_SET_MICR_MUTE:
		pr_debug("adc: ACODEC_SET_MICR_MUTE\n");
		if (val == 0)
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTER_OFF;
		else
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) | AUDIO_PHY_REG_MUTER_ON;

		adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, temp);
		break;

	case ACODEC_GET_GAIN_MICL:
		pr_debug("adc: ACODEC_GET_GAIN_MICL\n");
		/* read left vol */
		val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7);

		for (temp = 0; temp < 49; temp++) {
			if (val == adc_vol_list[temp])
				break;
		}

		if (copy_to_user(argp, &temp, sizeof(temp)))
			pr_err("failed to return MICL gain\n");
		break;
	case ACODEC_GET_GAIN_MICR:
		pr_debug("adc: ACODEC_GET_GAIN_MICR\n");
		/* read right vol */
		val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8);

		for (temp = 0; temp < 49; temp++) {
			if (val == adc_vol_list[temp])
				break;
		}
		if (copy_to_user(argp, &temp, sizeof(temp)))
			pr_err("failed to return MICR gain\n");
		break;

	case ACODEC_GET_ADCL_VOL:
		pr_debug("adc: ACODEC_GET_ADCL_VOL\n");

		/* read left vol */
		val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7);
		for (temp = 0; temp < 49; temp++) {
			if (val == adc_vol_list[temp])
				break;
		}
		vol.vol_ctrl = temp;
		vol.vol_ctrl_mute = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTEL_RXPGA_MASK;

		if (copy_to_user(argp, &vol, sizeof(vol)))
			pr_err("failed to return ADCL vol\n");

		break;
	case ACODEC_GET_ADCR_VOL:
		pr_debug("adc: ACODEC_GET_ADCR_VOL\n");

		/* read right vol */
		val = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8);
		for (temp = 0; temp < 49; temp++) {
			if (val == adc_vol_list[temp])
				break;
		}
		vol.vol_ctrl = temp;
		vol.vol_ctrl_mute = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTER_RXPGA_MASK;

		if (copy_to_user(argp, &vol, sizeof(vol)))
			pr_err("failed to return ADCR vol\n");

		break;

	case ACODEC_SET_PD_ADCL:
		pr_debug("adc: ACODEC_SET_PD_ADCL, val=%d\n", val);
		if (val == 0) {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp |= AUDIO_PHY_REG_RXADC_EN_ON | AUDIO_PHY_REG_I2S_TX_EN_ON;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		} else {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp &= AUDIO_PHY_REG_RXADC_EN_OFF & AUDIO_PHY_REG_I2S_TX_EN_OFF;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		}
		break;
	case ACODEC_SET_PD_ADCR:
		pr_debug("adc: ACODEC_SET_PD_ADCR, val=%d\n", val);
		if (val == 0) {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp |= AUDIO_PHY_REG_RXADC_EN_ON | AUDIO_PHY_REG_I2S_TX_EN_ON;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		} else {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp &= AUDIO_PHY_REG_RXADC_EN_OFF & AUDIO_PHY_REG_I2S_TX_EN_OFF;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		}
		break;

	case ACODEC_SET_PD_LINEINL:
		pr_debug("adc: ACODEC_SET_PD_LINEINL, val=%d\n", val);
		if (val == 0) {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp |= AUDIO_PHY_REG_RXADC_EN_ON | AUDIO_PHY_REG_I2S_TX_EN_ON;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		} else {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp &= AUDIO_PHY_REG_RXADC_EN_OFF & AUDIO_PHY_REG_I2S_TX_EN_OFF;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		}
		break;
	case ACODEC_SET_PD_LINEINR:
		pr_debug("adc: ACODEC_SET_PD_LINEINR, val=%d\n", val);
		if (val == 0) {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp |= AUDIO_PHY_REG_RXADC_EN_ON | AUDIO_PHY_REG_I2S_TX_EN_ON;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		} else {
			temp = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
			temp &= AUDIO_PHY_REG_RXADC_EN_OFF & AUDIO_PHY_REG_I2S_TX_EN_OFF;
			adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, temp);
		}
		break;
	case ACODEC_SET_ADC_HP_FILTER:
		pr_info("adc: ACODEC_SET_ADC_HP_FILTER is not support\n");
		break;
	default:
		pr_info("%s, received unsupport cmd=%u\n", __func__, cmd);
		break;
	}

	if (mutex_is_locked(&adc->mutex))
		mutex_unlock(&adc->mutex);

	return 0;
}

static const struct file_operations adc_fops = {
	.owner = THIS_MODULE,
	.open = adc_open,
	.release = adc_close,
	.unlocked_ioctl = adc_ioctl,
	.compat_ioctl = adc_ioctl,
};

static int adc_misc_register(struct adc_obj *adc)
{
	struct miscdevice *miscdev = &adc->miscdev;
	int ret;
	char name[60] = {0};

	sprintf(name, "misc_adc_%d", adc->id);
	miscdev->minor = MISC_DYNAMIC_MINOR;
	miscdev->name = name;
	miscdev->fops = &adc_fops;
	miscdev->parent = NULL;

	ret = misc_register(miscdev);
	if (ret) {
		pr_err("adc: failed to register misc device.\n");
		return ret;
	}
	return 0;
}

static int adc_proc_show(struct seq_file *m, void *v)
{
	void __iomem *clk_pll_en;
	u32 audio_freq;
	u32 val1, val2, val3;
	u32 temp1, temp2;
	struct adc_obj *adc = m->private;

	clk_pll_en = ioremap(adc->sdma_clk_en, 0x10);
	audio_freq = subsys_get_mclk(adc->id);

	seq_printf(m, "\n----------------- ADC[%d] INFO ----------------\n", adc->id);
	seq_puts(m, "\n------------- CVI AI ATTRIBUTE -------------\n");
	seq_puts(m, "AiDev    Workmode    SampleRate    BitWidth\n");
	val1 = adc->config.role;
	val2 = adc->config.rate;
	val3 = adc->config.bit_depth;
	seq_printf(m, "  %d       %s        %6d        %2d\n", adc->id, val1 == 0 ? "slave" : "master", val2, val3);
	seq_puts(m, "\n");
	seq_puts(m, "-------------  CVI AI STATUS   -------------\n");

	seq_printf(m, "connect i2s is I2S%x\n", adc->bind_i2s);
	seq_puts(m, "\n");
	val1 = (readl(clk_pll_en) & 0x00000002) >> 1;
	seq_printf(m, "SDMA clk is %s freq = %d\n", val1 == 1 ? "on" : "off", audio_freq);
	seq_puts(m, "\n");

	val1 = (readl(adc->base_addr + AUDIO_PHY_RXADC_CTRL0) &
		(AUDIO_PHY_REG_RXADC_EN_MASK | AUDIO_PHY_REG_I2S_TX_EN_MASK));
	seq_printf(m, "ADC is %s (%d)\n", val1 == 3 ? "on" : "off", val1);
	seq_puts(m, "\n");

	val1 = (readl(adc->base_addr + AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTEL_RXPGA_MASK);
	val2 = (readl(adc->base_addr + AUDIO_PHY_RXADC_ANA2) & AUDIO_PHY_REG_MUTER_RXPGA_MASK) >> 1;
	seq_puts(m, "L-Mute   R-Mute\n");
	seq_printf(m, "  %s       %s\n", val1 == 1 ? "yes" : "no", val2 == 1 ? "yes" : "no");
	seq_puts(m, "\n");

	val1 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7);
	val2 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8);

	for (temp1 = 0; temp1 < 49; temp1++) {
		if (val1 == adc_vol_list[temp1])
			break;
	}
	for (temp2 = 0; temp2 < 49; temp2++) {
		if (val2 == adc_vol_list[temp2])
			break;
	}

	seq_puts(m, "L-Vol           R-Vol\n");
	seq_printf(m, "  %d              %d\n", temp1, temp2);
	seq_puts(m, "\n");
	iounmap(clk_pll_en);

	return 0;
}

static const struct snd_soc_component_driver adc0_component_driver = {
	.controls = adc0_controls,
	.num_controls = ARRAY_SIZE(adc0_controls),
	.read = adc_reg_read,
	.write = adc_reg_write,
};

static const struct snd_soc_component_driver adc1_component_driver = {
	.controls = adc1_controls,
	.num_controls = ARRAY_SIZE(adc1_controls),
	.read = adc_reg_read,
	.write = adc_reg_write,
};

static int adc_probe(struct platform_device *pdev)
{
	struct adc_obj *adc;
	struct resource *res;
	u32 ctrl1;
	int ret;
	const struct snd_soc_component_driver *adc_component_driver;
	struct proc_dir_entry *proc_adc;
	char proc_name[60];

	dev_info(&pdev->dev, "%s\n", __func__);

	adc = devm_kzalloc(&pdev->dev, sizeof(*adc), GFP_KERNEL);
	if (!adc)
		return -ENOMEM;

	// adc_component_driver = devm_kzalloc(&pdev->dev, sizeof(*adc_component_driver), GFP_KERNEL);
	// if (!adc_component_driver)
	//return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	adc->base_addr = devm_ioremap_resource(&pdev->dev, res);
	dev_info(&pdev->dev, "probe get adc_base=0x%p\n", adc->base_addr);
	if (IS_ERR(adc->base_addr))
		return PTR_ERR(adc->base_addr);

	of_property_read_u32(pdev->dev.of_node, "dac_addr", &adc->dac_addr);
	dev_info(&pdev->dev, "dac_addr=0x%x\n", adc->dac_addr);

	of_property_read_u32(pdev->dev.of_node, "dev_id", &adc->id);
	dev_info(&pdev->dev, "dev_id=0x%x\n", adc->id);

	of_property_read_u32(pdev->dev.of_node, "bind_i2s", &adc->bind_i2s);
	dev_info(&pdev->dev, "bind_i2s=0x%x\n", adc->bind_i2s);

	// of_property_read_u32(pdev->dev.of_node, "clk_source", &adc->mclk_source);
	// dev_info(&pdev->dev, "mclk_source=0x%x\n", adc->mclk_source);

	of_property_read_u32(pdev->dev.of_node, "sdma_clk_en", &adc->sdma_clk_en);
	dev_info(&pdev->dev, "sdma_clk_en=0x%x\n", adc->sdma_clk_en);

	of_property_read_u32_array(pdev->dev.of_node, "reset_cntl", adc->reset_info, 2);
	dev_info(&pdev->dev, "reset_control,addr:%x,offset:%d\n", adc->reset_info[0], adc->reset_info[1]);
	mutex_init(&adc->mutex);

	adc->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, adc);
	ret = adc_misc_register(adc);
	if (ret < 0) {
		pr_err("adc: register device error\n");
		return ret;
	}
	/* set default input vol gain to maxmum 48dB, vol range is 0~48 */
	ctrl1 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1, ctrl1 | AUDIO_ADC_IGR_INIT_EN);

	sprintf(proc_name, "audio_debug/adc%d_info", adc->id);
	proc_adc = proc_create_single_data(proc_name, 0444, NULL, adc_proc_show, adc);
	if (!proc_adc)
		dev_err(&pdev->dev, "create proc node %s fail\n", proc_name);

	if (adc->id == 0) {
		adc_component_driver = &adc0_component_driver;
	} else if (adc->id == 1) {
		adc_component_driver = &adc1_component_driver;
	} else {
		dev_err(&pdev->dev, "Invalid ADC ID: %d\n", adc->id);
		return -EINVAL;
	}
	return devm_snd_soc_register_component(&pdev->dev, adc_component_driver,
					    &adc_dai_driver, 1);
}

static int adc_remove(struct platform_device *pdev)
{
	// struct adc_obj *adc = dev_get_drvdata(&pdev->dev);

	dev_dbg(&pdev->dev, "%s\n", __func__);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id adc_of_match[] = {
	{ .compatible = "cvitek,cvitek_adc", },
	{},
};

MODULE_DEVICE_TABLE(of, adc_of_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int adc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct adc_obj *adc = platform_get_drvdata(pdev);

	if (!adc->reg_ctx) {
		adc->reg_ctx = devm_kzalloc(adc->dev, sizeof(struct adc_context), GFP_KERNEL);
		if (!adc->reg_ctx)
			return -ENOMEM;
	}

	adc->reg_ctx->ctl0 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0);
	adc->reg_ctx->ctl1 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1);
	adc->reg_ctx->status = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_STATUS);
	adc->reg_ctx->ana7 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7);
	adc->reg_ctx->ana8 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8);
	adc->reg_ctx->ana2 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2);
	adc->reg_ctx->ana3 = adc_read_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA3);

	return 0;
}

static int adc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct adc_obj *adc = platform_get_drvdata(pdev);

	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL0, adc->reg_ctx->ctl0);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_CTRL1, adc->reg_ctx->ctl1);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_STATUS, adc->reg_ctx->status);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA7, adc->reg_ctx->ana7);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA8, adc->reg_ctx->ana8);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA2, adc->reg_ctx->ana2);
	adc_write_reg(adc->base_addr, AUDIO_PHY_RXADC_ANA3, adc->reg_ctx->ana3);

	return 0;
}

static SIMPLE_DEV_PM_OPS(adc_pm_ops, adc_suspend, adc_resume);
#endif

static struct platform_driver adc_platform_driver = {
	.probe		= adc_probe,
	.remove		= adc_remove,
	.driver		= {
		.name	= "codec_adc",
		.of_match_table = of_match_ptr(adc_of_match),
#ifdef CONFIG_PM_SLEEP
		.pm	= &adc_pm_ops,
#endif
	},
};
module_platform_driver(adc_platform_driver);

MODULE_DESCRIPTION("ASoC codec V1 ADC driver");
MODULE_AUTHOR("Xiaodong <xiaodong.fan@sophgo.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:codec_adc");
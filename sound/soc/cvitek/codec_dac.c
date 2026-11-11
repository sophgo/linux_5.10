#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/mutex.h>
#include <linux/miscdevice.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/control.h>
#include "codec_ioctl.h"
#include "codec_common.h"
#include "plat_i2s_subsys.h"
#include <linux/gpio.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#include <linux/clk.h>

static void mute_amp(struct dac_obj *dac, bool enable)
{
	if (enable) {
		if (dac->amp_gpio_l != -EINVAL) {//SPK_EN XGPIOA15 495
			gpio_set_value(dac->amp_gpio_l, 0);
		}
		if (dac->amp_gpio_r != -EINVAL) {//AUX0 XGPIOA30 510
			gpio_set_value(dac->amp_gpio_r, 0);
		}

	} else {
		if (dac->amp_gpio_l != -EINVAL) {
			gpio_set_value(dac->amp_gpio_l, 1);
		}
		if (dac->amp_gpio_r != -EINVAL) {
			gpio_set_value(dac->amp_gpio_r, 1);
		}
	}
}

static inline void dac_write_reg(void __iomem *io_base, int reg, u32 val)
{
	writel(val, io_base + reg);
}

static inline u32 dac_read_reg(void __iomem *io_base, int reg)
{
	return readl(io_base + reg);
}

void dac_debug(struct dac_obj *dac)
{
	u32 ctl0, ctl1, afe0, afe1, ana0, ana1, ana2;

	ctl0 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);
	ctl1 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL1);
	afe0 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE0);
	afe1 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1);
	ana0 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA0);
	ana1 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA1);
	ana2 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
	ana2 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA3);

	dev_info(dac->dev,
		 "dac ctrl0=0x%08x, ctrl1=0x%08x, afe0=0x%08x, afe1=0x%08x, ana0=0x%08x, ana1=0x%08x, ana2=0x%08x\n",
		 ctl0, ctl1, afe0, afe1, ana0, ana1, ana2);
}

static int dac_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct dac_obj *dac = snd_soc_dai_get_drvdata(dai);

	if (!dac->dev)
		dev_err(dac->dev, "dev is NULL\n");

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		dev_err(dac->dev, "Cannot set DAC to MASTER mode\n");
		dac->config.role = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		dev_dbg(dac->dev, "Set DAC to SLAVE mode\n");
		dac->config.role = 0;
		break;
	default:
		dev_err(dac->dev, "Cannot support this role mode\n");
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		dev_dbg(dac->dev, "set codec to NB_NF\n");
		break;
	case SND_SOC_DAIFMT_IB_NF:
		dev_dbg(dac->dev, "set codec to IB_NF\n");
		break;
	case SND_SOC_DAIFMT_IB_IF:
		dev_dbg(dac->dev, "set codec to IB_IF\n");
		break;
	case SND_SOC_DAIFMT_NB_IF:
		dev_dbg(dac->dev, "set codec to NB_IF\n");
		break;
	default:
		dev_err(dac->dev, "Cannot support this format\n");
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dev_dbg(dac->dev, "set codec to I2S mode\n");
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dev_dbg(dac->dev, "set codec to LEFT-JUSTIFY mode\n");
		break;
	default:
		dev_err(dac->dev, "Cannot support this mode\n");
		break;
	}
	return 0;
}

static int dac_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params,
			 struct snd_soc_dai *dai)
{
	struct dac_obj *dac = snd_soc_dai_get_drvdata(dai);
	u32 ctrl1 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL1) & ~AUDIO_PHY_REG_TXDAC_CIC_OPT_MASK;
	u32 tick = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE0) & ~AUDIO_PHY_REG_TXDAC_INIT_DLY_CNT_MASK;
	u32 ana2 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);

	dac->config.ch_num = params_channels(params);

	switch (dac->config.ch_num) {
	case 1:
		//ana2 |= AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_ON; /* turn R-channel off */
		ana2 &= AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_OFF; /* turn R-channel on */
		dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, ana2);
		break;
	default:
		ana2 &= AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_OFF; /* turn R-channel on */
		dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, ana2);
		break;
	}

	dac->config.rate = params_rate(params);
	if (dac->config.rate >= 8000 && dac->config.rate <= 48000) {
		dev_dbg(dac->dev, "%s, set rate to %d\n", __func__, dac->config.rate);

		switch (dac->config.rate) {
		case 8000:
			ctrl1 |= TXDAC_CIC_DS_512;
			tick |= 0x21;
			break;
		case 11025:
			ctrl1 |= TXDAC_CIC_DS_256;
			tick |= 0x17;
			break;
		case 16000:
			ctrl1 |= TXDAC_CIC_DS_256;
			tick |= 0x21;
			break;
		case 22050:
			ctrl1 |= TXDAC_CIC_DS_128;
			tick |= 0x17;
			break;
		case 32000:
			ctrl1 |= TXDAC_CIC_DS_128;
			tick |= 0x21;
			break;
		case 44100:
			ctrl1 &= TXDAC_CIC_DS_64;
			tick |= 0x17;
			break;
		case 48000:
			ctrl1 &= TXDAC_CIC_DS_64;
			tick |= 0x19;
			break;
		default:
			ctrl1 |= TXDAC_CIC_DS_256;
			tick |= 0x21;
			dev_dbg(dac->dev, "%s, set sample rate with default 16KHz\n", __func__);
			break;
		}
	} else {
		dev_err(dac->dev, "%s, unsupported sample rate\n", __func__);
		return -EINVAL;
	}
	dev_dbg(dac->dev, "%s, ctrl1=0x%x\n", __func__, ctrl1);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL1, ctrl1);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE0, tick);
	dac->config.bit_depth = snd_pcm_format_width(params_format(params));

	return 0;

}

static int dac_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	return 0;
}

static void dac_on(struct dac_obj *dac)
{
	u32 val = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);

	dev_dbg(dac->dev, "dac on, before ctrl0_reg val=0x%08x\n", val);

	val |= AUDIO_PHY_REG_TXDAC_EN_ON | AUDIO_PHY_REG_I2S_RX_EN_ON;

	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, val);

	dev_dbg(dac->dev, "dac on, after ctrl0_reg val=0x%08x\n",
		dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0));
	//dac_debug(dac);
}

static void dac_off(struct dac_obj *dac)
{
	u32 val = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);

	val &= AUDIO_PHY_REG_TXDAC_EN_OFF & AUDIO_PHY_REG_I2S_RX_EN_OFF;
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, val);
}

static void dac_reset(struct dac_obj *dac)
{
	void __iomem *reset_reg = ioremap(dac->reset_info[0], 4);

	writel(readl(reset_reg) & (~(1 << dac->reset_info[1])), reset_reg);
	writel(readl(reset_reg) | (1 << dac->reset_info[1]), reset_reg);
	iounmap(reset_reg);
}

static void dac_shutdown(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai)
{
	struct dac_obj *dac = snd_soc_dai_get_drvdata(dai);

	dev_dbg(dac->dev, "%s\n", __func__);
	mute_amp(dac, true);
	dac_reset(dac);
}

static int dac_trigger(struct snd_pcm_substream *substream,
		       int cmd, struct snd_soc_dai *dai)
{
	struct dac_obj *dac = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	dev_dbg(dac->dev, "%s, cmd=%d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_pcm_stream_unlock_irq(substream);
		dac_on(dac);
		mute_amp(dac, false);
		snd_pcm_stream_lock_irq(substream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_pcm_stream_unlock_irq(substream);
		mute_amp(dac, true);
		dac_off(dac);
		snd_pcm_stream_lock_irq(substream);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int dac_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct dac_obj *dac = snd_soc_dai_get_drvdata(dai);
	u32 val;

	//need to rewrite the register if called dac_reset
	val = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1, val);
	val = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, val);
	val = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, val);

	return 0;
}

static const struct snd_soc_dai_ops dac_dai_ops = {
	.hw_params	= dac_hw_params,
	.set_fmt	= dac_set_dai_fmt,
	.startup	= dac_startup,
	.shutdown	= dac_shutdown,
	.trigger	= dac_trigger,
	.prepare	= dac_prepare,
};

static struct snd_soc_dai_driver dac_dai_driver = {

	.name		= "codec_dac",
	.playback	= {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops		= &dac_dai_ops,

};

static const struct snd_kcontrol_new dac0_controls[] = {

	SOC_DOUBLE("DAC0 Playback Power Up/Down", AUDIO_PHY_TXDAC_CTRL0, 1, 0, 1, 1),
	SOC_DOUBLE("DAC0 Playback Volume", AUDIO_PHY_TXDAC_AFE1, 0, 16, 32, 1),
	SOC_DOUBLE("DAC0 Playback MUTE", AUDIO_PHY_TXDAC_ANA2, 16, 17, 1, 0),

};

static const struct snd_kcontrol_new dac1_controls[] = {

	SOC_DOUBLE("DAC1 Playback Power Up/Down", AUDIO_PHY_TXDAC_CTRL0, 1, 0, 1, 1),
	SOC_DOUBLE("DAC1 Playback Volume", AUDIO_PHY_TXDAC_AFE1, 0, 16, 32, 1),
	SOC_DOUBLE("DAC1 Playback MUTE", AUDIO_PHY_TXDAC_ANA2, 16, 17, 1, 0),

};

static unsigned int dac_reg_read(struct snd_soc_component *codec, unsigned int reg)
{
	struct dac_obj *dac = dev_get_drvdata(codec->dev);
	int ret;
	u32 temp_lval = 0;
	u32 temp_rval = 0;

	ret = dac_read_reg(dac->base_addr, reg);

	if (reg == AUDIO_PHY_TXDAC_AFE1) {
		temp_lval = ((ret & 0x000001ff) + 1) / 16;
		temp_rval = (((ret >> 16) & 0x000001ff) + 1) / 16;
		dev_info(dac->dev, "Get DAC Vol reg:%d,ret:0x%x temp_lval=%d.\n", reg, ret, temp_lval);
		ret = (temp_rval << 16) | temp_lval;
	}
	dev_info(dac->dev, "%s reg:%d,ret:%#x.\n", __func__, reg, ret);

	return ret;
}

static int dac_reg_write(struct snd_soc_component *codec, unsigned int reg, unsigned int value)
{
	struct dac_obj *dac = dev_get_drvdata(codec->dev);
	u32 temp_lval;
	u32 temp_rval;

	if (reg == AUDIO_PHY_TXDAC_AFE1 && value) {
		temp_lval = value & 0xffff;
		temp_rval = (value >> 16) & 0xffff;
		if (temp_lval > 32)
			temp_lval = 32;
		if (temp_rval > 32)
			temp_rval = 32;
		value = DAC_VOL_L(temp_lval) | DAC_VOL_R(temp_rval);
	}

	dac_write_reg(dac->base_addr, reg, value);
	dev_info(dac->dev, "%s reg:%d,value:%#x.\n", __func__, reg, value);

	return 0;
}

static struct dac_obj *file_dac_dev(struct file *file)
{
	return container_of(file->private_data, struct dac_obj, miscdev);
}

static int dac_open(struct inode *inode, struct file *file)
{
	struct dac_obj *dac = file_dac_dev(file);

	if (mutex_lock_interruptible(&dac->mutex))
		return -EINTR;
	mutex_unlock(&dac->mutex);
	pr_debug("%s\n", __func__);
	return 0;
}

static int dac_close(struct inode *inode, struct file *file)
{
	struct dac_obj *dac = file_dac_dev(file);

	if (mutex_lock_interruptible(&dac->mutex))
		return -EINTR;
	mutex_unlock(&dac->mutex);
	pr_debug("%s\n", __func__);
	return 0;
}

static long dac_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int __user *argp = (unsigned int __user *)arg;
	struct dac_obj *dac = file_dac_dev(file);
	struct cvi_vol_ctrl vol;
	u32 val;
	u32 temp;

	if (argp) {
		if (!copy_from_user(&val, argp, sizeof(val))) {
			if (mutex_lock_interruptible(&dac->mutex)) {
				pr_debug("cvitekadac: signal arrives while waiting for lock\n");
				return -EINTR;
			}
		} else
			return -EFAULT;
	}

	switch (cmd) {
	case ACODEC_SOFT_RESET_CTRL:
		dac_reset(dac);
		break;

	case ACODEC_SET_OUTPUT_VOL:
		pr_debug("dac: ACODEC_SET_OUTPUT_VOL with val=%d\n", val);

		if (val < 0 || val > 32)
			pr_err("Only support range 0 [mute] ~ 32 [maximum]\n");
		else {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1)
					& ~(AUDIO_PHY_REG_TXDAC_GAIN_UB_0_MASK | AUDIO_PHY_REG_TXDAC_GAIN_UB_1_MASK);
			temp |= DAC_VOL_L(val) | DAC_VOL_R(val);
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1, temp);
		}
		break;

	case ACODEC_GET_OUTPUT_VOL:
		pr_debug("dac: ACODEC_GET_OUTPUT_VOL\n");
		temp = ((dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1)
				& AUDIO_PHY_REG_TXDAC_GAIN_UB_0_MASK) + 1) / DAC_VOL_STEP;
		pr_debug("dac: return val=%d\n", temp);
		if (copy_to_user(argp, &temp, sizeof(temp)))
			pr_err("dac, failed to return output vol\n");
		break;

	case ACODEC_SET_I2S1_FS:
		pr_debug("dac: ACODEC_SET_I2S1_FS is not support\n");
		break;

	case ACODEC_SET_DACL_VOL:
		pr_debug("dac: ACODEC_SET_DACL_VOL\n");
		if (copy_from_user(&vol, argp, sizeof(vol))) {
			if (mutex_is_locked(&dac->mutex))
				mutex_unlock(&dac->mutex);

			return -EFAULT;
		}
		if (vol.vol_ctrl_mute == 1) {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
			temp |= AUDIO_PHY_REG_DA_DEML_TXDAC_OW_EN_ON;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, temp);
		} else if ((vol.vol_ctrl < 0) | (vol.vol_ctrl > 32))
			pr_err("dac-L: Only support range 0 [mute] ~ 32 [maximum]\n");
		else {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1) & ~AUDIO_PHY_REG_TXDAC_GAIN_UB_0_MASK;
			temp |= DAC_VOL_L(vol.vol_ctrl);
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1, temp);

			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
			temp &= AUDIO_PHY_REG_DA_DEML_TXDAC_OW_EN_OFF;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, temp);
		}
		break;

	case ACODEC_SET_DACR_VOL:
		pr_debug("dac: ACODEC_SET_DACR_VOL\n");
		if (copy_from_user(&vol, argp, sizeof(vol)))
			return -EFAULT;

		if (vol.vol_ctrl_mute == 1) {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
			temp |= AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_ON;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, temp);
		} else if ((vol.vol_ctrl < 0) | (vol.vol_ctrl > 32))
			pr_err("dac-L: Only support range 0 [mute] ~ 32 [maximum]\n");
		else {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1) & ~AUDIO_PHY_REG_TXDAC_GAIN_UB_1_MASK;
			temp |= DAC_VOL_R(vol.vol_ctrl);
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1, temp);

			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
			temp &= AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_OFF;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, temp);
		}
		break;

	case ACODEC_SET_DACL_MUTE:
		pr_debug("dac: ACODEC_SET_DACL_MUTE, val=%d\n", val);
		temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
		if (val == 0)
			temp &= AUDIO_PHY_REG_DA_DEML_TXDAC_OW_EN_OFF;
		else
			temp |= AUDIO_PHY_REG_DA_DEML_TXDAC_OW_EN_ON;

		dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, temp);
		break;
	case ACODEC_SET_DACR_MUTE:
		pr_debug("dac: ACODEC_SET_DACR_MUTE, val=%d\n", val);
		temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
		if (val == 0)
			temp &= AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_OFF;
		else
			temp |= AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_ON;
		dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, temp);
		break;

	case ACODEC_GET_DACL_VOL:
		pr_debug("dac: ACODEC_GET_DACL_VOL\n");
		temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
		if (temp & AUDIO_PHY_REG_DA_DEML_TXDAC_OW_EN_MASK) {
			vol.vol_ctrl = 0;
			vol.vol_ctrl_mute = 1;
		} else {
			temp = ((dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1)
			& AUDIO_PHY_REG_TXDAC_GAIN_UB_0_MASK) + 1) / DAC_VOL_STEP;
			vol.vol_ctrl = temp;
			vol.vol_ctrl_mute = 0;
		}
		if (copy_to_user(argp, &vol, sizeof(vol)))
			pr_err("failed to return DACL vol\n");
		break;
	case ACODEC_GET_DACR_VOL:
		temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);
		pr_debug("dac: ACODEC_GET_DACR_VOL, txdac_ana2=0x%x\n", temp);
		if (temp & AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_MASK) {
			vol.vol_ctrl = 0;
			vol.vol_ctrl_mute = 1;
		} else {
			temp = (((dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1)
			& AUDIO_PHY_REG_TXDAC_GAIN_UB_1_MASK) >> 16) + 1) / DAC_VOL_STEP;
			vol.vol_ctrl = temp;
			vol.vol_ctrl_mute = 0;
		}
		if (copy_to_user(argp, &vol, sizeof(vol)))
			pr_err("failed to return DACR vol\n");
		break;

	case ACODEC_SET_PD_DACL:
		pr_debug("dac: ACODEC_SET_PD_DACL, val=%d\n", val);
		if (val == 0) {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);
			temp &= AUDIO_PHY_REG_TXDAC_EN_ON | AUDIO_PHY_REG_I2S_RX_EN_ON;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, temp);
		} else {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);
			temp &= AUDIO_PHY_REG_TXDAC_EN_OFF & AUDIO_PHY_REG_I2S_RX_EN_OFF;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, temp);
		}
		break;
	case ACODEC_SET_PD_DACR:
		pr_debug("dac: ACODEC_SET_PD_DACR, val=%d\n", val);
		if (val == 0) {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);
			temp &= AUDIO_PHY_REG_TXDAC_EN_ON | AUDIO_PHY_REG_I2S_RX_EN_ON;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, temp);
		} else {
			temp = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);
			temp &= AUDIO_PHY_REG_TXDAC_EN_OFF & AUDIO_PHY_REG_I2S_RX_EN_OFF;
			dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, temp);
		}
		break;
	case ACODEC_SET_DAC_DE_EMPHASIS:
		pr_info("dac: ACODEC_SET_DAC_DE_EMPHASIS is not support\n");
		break;
	default:
		pr_info("%s, received unsupported cmd=%u\n", __func__, cmd);
		break;
	}

	if (mutex_is_locked(&dac->mutex))
		mutex_unlock(&dac->mutex);

	return 0;
}

static const struct file_operations dac_fops = {
	.owner = THIS_MODULE,
	.open = dac_open,
	.release = dac_close,
	.unlocked_ioctl = dac_ioctl,
	.compat_ioctl = dac_ioctl,
};

static int dac_misc_register(struct dac_obj *dac)
{
	struct miscdevice *miscdev = &dac->miscdev;
	int ret;
	char name[60] = {0};

	sprintf(name, "misc_dac_%d", dac->id);
	miscdev->minor = MISC_DYNAMIC_MINOR;
	miscdev->name = name;
	miscdev->fops = &dac_fops;
	miscdev->parent = NULL;

	ret = misc_register(miscdev);
	if (ret) {
		pr_err("dac: failed to register misc device.\n");
		return ret;
	}
	return 0;
}

static int dac_proc_show(struct seq_file *m, void *v)
{
	void __iomem *clk_pll_en;
	u32 audio_freq;
	u32 val1, val2, val3;
	struct dac_obj *dac = m->private;

	clk_pll_en = ioremap(dac->sdma_clk_en, 0x4);
	audio_freq = clk_get_rate(dac->clk);

	seq_printf(m, "\n----------------- DAC[%d] INFO ----------------\n", dac->id);
	seq_puts(m, "\n------------- CVI AO ATTRIBUTE -------------\n");
	seq_puts(m, "AoDev    Workmode    SampleRate    BitWidth\n");
	val1 = dac->config.role;
	val2 = dac->config.rate;
	val3 = dac->config.bit_depth;
	seq_printf(m, "  %d       %s        %6d        %2d\n", dac->id, val1 == 0 ? "slave" : "master", val2, val3);
	seq_puts(m, "\n");
	seq_puts(m, "-------------  CVI AO STATUS   -------------\n");
	seq_printf(m, "dac connet i2s is I2S%x\n", dac->bind_i2s);
	seq_puts(m, "\n");

	val1 = (readl(clk_pll_en) & 0x00080000) >> 19;
	seq_printf(m, "SDMA clk is %s,freq = %d\n", val1 == 1 ? "on" : "off", audio_freq);

	val1 = (readl(dac->base_addr + AUDIO_PHY_TXDAC_CTRL0)
		& (AUDIO_PHY_REG_TXDAC_EN_MASK | AUDIO_PHY_REG_I2S_RX_EN_MASK));
	seq_printf(m, "DAC is %s (%d)\n", val1 == 3 ? "on" : "off", val1);

	val2 = (readl(dac->base_addr + AUDIO_PHY_TXDAC_ANA2) & AUDIO_PHY_REG_DA_DEML_TXDAC_OW_EN_MASK) >> 16;
	val3 = (readl(dac->base_addr + AUDIO_PHY_TXDAC_ANA2) & AUDIO_PHY_REG_DA_DEMR_TXDAC_OW_EN_MASK) >> 17;
	seq_puts(m, "L-Mute   R-Mute\n");
	seq_printf(m, "  %s       %s\n", val2 == 1 ? "yes" : "no", val3 == 1 ? "yes" : "no");
	seq_puts(m, "\n");

	val2 = ((readl(dac->base_addr + AUDIO_PHY_TXDAC_AFE1) & AUDIO_PHY_REG_TXDAC_GAIN_UB_0_MASK) + 1)
					/ DAC_VOL_STEP;
	val3 = (((readl(dac->base_addr + AUDIO_PHY_TXDAC_AFE1) & AUDIO_PHY_REG_TXDAC_GAIN_UB_1_MASK) >> 16) + 1)
					/ DAC_VOL_STEP;
	seq_puts(m, "L-Vol           R-Vol\n");
	seq_printf(m, "  %d             %d\n", val2, val3);
	seq_puts(m, "\n");

	iounmap(clk_pll_en);

	return 0;
}

static const struct snd_soc_component_driver dac0_component_driver = {
	.controls = dac0_controls,
	.num_controls = ARRAY_SIZE(dac0_controls),
	.read = dac_reg_read,
	.write = dac_reg_write,
};

static const struct snd_soc_component_driver dac1_component_driver = {
	.controls = dac1_controls,
	.num_controls = ARRAY_SIZE(dac1_controls),
	.read = dac_reg_read,
	.write = dac_reg_write,
};

static int dac_probe(struct platform_device *pdev)
{
	struct dac_obj *dac;
	struct resource *res;
	int ret;
	enum of_gpio_flags flags;
	const struct snd_soc_component_driver *dac_component_driver;
	struct proc_dir_entry *proc_dac;
	char proc_name[60];

	dev_info(&pdev->dev, "%s\n", __func__);

	dac = devm_kzalloc(&pdev->dev, sizeof(*dac), GFP_KERNEL);
	if (!dac)
		return -ENOMEM;

	// dac_component_driver = devm_kzalloc(&pdev->dev, sizeof(*dac_component_driver), GFP_KERNEL);
	// if (!dac_component_driver)
	//return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dac->base_addr = devm_ioremap_resource(&pdev->dev, res);
	dev_info(&pdev->dev, "%s get dac_base=0x%p\n", __func__, dac->base_addr);
	if (IS_ERR(dac->base_addr))
		return PTR_ERR(dac->base_addr);
	of_property_read_u32(pdev->dev.of_node, "dev_id", &dac->id);
	dev_info(&pdev->dev, "dev_id=0x%x\n", dac->id);
	of_property_read_u32(pdev->dev.of_node, "bind_i2s", &dac->bind_i2s);
	dev_info(&pdev->dev, "bind_i2s=0x%x\n", dac->bind_i2s);

	of_property_read_u32(pdev->dev.of_node, "sdma_clk_en", &dac->sdma_clk_en);
	dev_info(&pdev->dev, "sdma_clk_en=0x%x\n", dac->sdma_clk_en);
	of_property_read_u32_array(pdev->dev.of_node, "reset_cntl", dac->reset_info, 2);
	dev_info(&pdev->dev, "reset_control,addr:%x,offset:%d\n", dac->reset_info[0], dac->reset_info[1]);

	dac->amp_gpio_l = of_get_named_gpio_flags(pdev->dev.of_node, "mute-gpio-l", 0, &flags);
	dac->amp_gpio_r = of_get_named_gpio_flags(pdev->dev.of_node, "mute-gpio-r", 0, &flags);
	dac->clk = of_clk_get(pdev->dev.of_node, 0);

	dac->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, dac);
	ret = dac_misc_register(dac);
	if (ret < 0) {
		pr_err("dac: register device error\n");
		return ret;
	}

	if (!gpio_is_valid(dac->amp_gpio_l)) {
		pr_err("%s gpio_is_valid mute_pin_l\n", __func__);
		dac->amp_gpio_l =  -EINVAL;
	} else {
		gpio_request(dac->amp_gpio_l, "mute_pin_l");
		gpio_direction_output(dac->amp_gpio_l, 1);
		gpio_set_value(dac->amp_gpio_l, 0);
	}

	if (!gpio_is_valid(dac->amp_gpio_r)) {
		pr_err("%s gpio_is_valid mute_pin_r\n", __func__);
		dac->amp_gpio_r =  -EINVAL;
	} else {
		gpio_request(dac->amp_gpio_r, "mute_pin_r");
		gpio_direction_output(dac->amp_gpio_r, 1);
		gpio_set_value(dac->amp_gpio_r, 0);
	}

	sprintf(proc_name, "audio_debug/dac%d_info", dac->id);
	proc_dac = proc_create_single_data(proc_name, 0444, NULL, dac_proc_show, dac);
	if (!proc_dac)
		dev_err(&pdev->dev, "create proc node %s fail\n", proc_name);

	if (dac->id == 0) {
		dac_component_driver = &dac0_component_driver;
	} else if (dac->id == 1) {
		dac_component_driver = &dac1_component_driver;
	} else {
		dev_err(&pdev->dev, "Invalid DAC ID: %d\n", dac->id);
		return -EINVAL;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, dac_component_driver, &dac_dai_driver, 1);
	if (ret) {
		dev_err(&pdev->dev, "devm_snd_soc_register_component failed (%d)\n", ret);
		return ret;
	}
	return 0;
}

static int dac_remove(struct platform_device *pdev)
{
	struct dac_obj *dac = dev_get_drvdata(&pdev->dev);

	mute_amp(dac, true);
	dev_dbg(&pdev->dev, "%s\n", __func__);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dac_of_match[] = {
	{ .compatible = "cvitek,cvitek_dac", },
	{},
};

MODULE_DEVICE_TABLE(of, dac_of_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int dac_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dac_obj *dac = platform_get_drvdata(pdev);

	mute_amp(dac, true);
	if (!dac->reg_ctx) {
		dac->reg_ctx = devm_kzalloc(dac->dev, sizeof(struct dac_context), GFP_KERNEL);
		if (!dac->reg_ctx)
			return -ENOMEM;
	}

	dac->reg_ctx->ctl0 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0);
	dac->reg_ctx->ctl1 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL1);
	dac->reg_ctx->afe0 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE0);
	dac->reg_ctx->afe1 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1);
	dac->reg_ctx->ana0 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA0);
	dac->reg_ctx->ana1 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA1);
	dac->reg_ctx->ana2 = dac_read_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2);

	return 0;
}

static int dac_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dac_obj *dac = platform_get_drvdata(pdev);

	mute_amp(dac, false);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL0, dac->reg_ctx->ctl0);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_CTRL1, dac->reg_ctx->ctl1);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE0, dac->reg_ctx->afe0);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_AFE1, dac->reg_ctx->afe1);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA0, dac->reg_ctx->ana0);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA1, dac->reg_ctx->ana1);
	dac_write_reg(dac->base_addr, AUDIO_PHY_TXDAC_ANA2, dac->reg_ctx->ana2);

	return 0;
}

static SIMPLE_DEV_PM_OPS(dac_pm_ops, dac_suspend,
			 dac_resume);
#endif

static struct platform_driver dac_platform_driver = {
	.probe		= dac_probe,
	.remove		= dac_remove,
	.driver		= {
		.name	= "codec_dac",
		.of_match_table = of_match_ptr(dac_of_match),
#ifdef CONFIG_PM_SLEEP
		.pm	= &dac_pm_ops,
#endif
	},
};
module_platform_driver(dac_platform_driver);

MODULE_DESCRIPTION("ASoC codec V1 DAC driver");
MODULE_AUTHOR("Xiaodong <xiaodong.fan@sophgo.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:codec_dac");

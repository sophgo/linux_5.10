#include <linux/clk.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include "plat_i2s.h"
#include "plat_i2s_subsys.h"
#include <linux/version.h>

static inline void i2s_write_reg(void __iomem *io_base, int reg, u32 val)
{
	writel(val, io_base + reg);
}

static inline u32 i2s_read_reg(void __iomem *io_base, int reg)
{
	return readl(io_base + reg);
}

static inline void i2s_clear_irqs(struct i2s_obj *i2s, u32 stream)
{
	u32 irq = i2s_read_reg(i2s->i2s_base, I2S_INT);

	/* I2S_INT is write 1 clear */

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		i2s_write_reg(i2s->i2s_base, I2S_INT,
			      irq & (I2S_INT_TXDA | I2S_INT_TXFO | I2S_INT_TXFU
				     | I2S_INT_TXDA_RAW | I2S_INT_TXFO_RAW | I2S_INT_TXFU_RAW));
	else
		i2s_write_reg(i2s->i2s_base, I2S_INT,
			      irq & (I2S_INT_RXDA | I2S_INT_RXFO | I2S_INT_RXFU
				     | I2S_INT_RXDA_RAW | I2S_INT_RXFO_RAW | I2S_INT_RXFU_RAW));
}

static inline void i2s_disable_irqs(struct i2s_obj *i2s, u32 stream)
{
	u32 irq = i2s_read_reg(i2s->i2s_base, I2S_INT_EN);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		i2s_write_reg(i2s->i2s_base, I2S_INT_EN, irq & ~(I2S_INT_TXDA | I2S_INT_TXFO | I2S_INT_TXFU));
	else
		i2s_write_reg(i2s->i2s_base, I2S_INT_EN, irq & ~(I2S_INT_RXDA | I2S_INT_RXFO | I2S_INT_RXFU));
}

static inline void i2s_enable_irqs(struct i2s_obj *i2s, u32 stream)
{
	u32 irq = i2s_read_reg(i2s->i2s_base, I2S_INT_EN);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		i2s_write_reg(i2s->i2s_base, I2S_INT_EN, irq | I2S_INT_TXFO | I2S_INT_TXFU);
	else
		i2s_write_reg(i2s->i2s_base, I2S_INT_EN, irq | I2S_INT_RXFO | I2S_INT_RXFU);

}

static void i2s_fifo_reset(struct i2s_obj *i2s, u32 stream)
{
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		i2s_write_reg(i2s->i2s_base, FIFO_RESET, TX_FIFO_RESET_PULL_UP);
		i2s_write_reg(i2s->i2s_base, FIFO_RESET, TX_FIFO_RESET_PULL_DOWN);
	} else { /* reset RX*/
		i2s_write_reg(i2s->i2s_base, FIFO_RESET, RX_FIFO_RESET_PULL_UP);
		i2s_write_reg(i2s->i2s_base, FIFO_RESET, RX_FIFO_RESET_PULL_DOWN);
	}
}

static void i2s_debug(struct i2s_obj *i2s)
{
	pr_err("[i2s_reg]\n");

	pr_err("BLK_MODE_SETTING:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x0));
	pr_err("FRAME_SETTING:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x4));
	pr_err("SLOT_SETTING1:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x8));
	pr_err("SLOT_SETTING2:0x%x\n", i2s_read_reg(i2s->i2s_base, 0xc));

	pr_err("DATA_FORMAT:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x10));
	pr_err("BLK_CFG:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x14));
	pr_err("I2S_ENABL:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x18));
	pr_err("I2S_RESET:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x1c));

	pr_err("I2S_INT_EN:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x20));
	pr_err("I2S_INT:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x24));
	pr_err("FIFO_THRESHOLD:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x28));
	pr_err("I2S_LRCK_MASTER:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x2c));

	pr_err("FIFO_RESET:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x30));
	pr_err("RX_STATUS:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x40));
	pr_err("TX_STATUS:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x48));
	pr_err("FIFO_STATUS:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x4c));

	pr_err("I2S_CLK_CTRL0:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x60));
	pr_err("I2S_CLK_CTRL1:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x64));
	pr_err("I2S_PCM_SYNTH:0x%x\n", i2s_read_reg(i2s->i2s_base, 0x68));
}

#define I2S_RETRY_COUNT 30000
static void i2s_reset(struct i2s_obj *i2s, u32 stream)
{
	u32 retry = 0;

	dev_dbg(i2s->dev, "blk_mode=0x%08x, clk_ctrl=0x%08x, i2s_enable=0x%08x\n",
		i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING),
		i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0),
		i2s_read_reg(i2s->i2s_base, I2S_ENABLE));
	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		i2s_write_reg(i2s->i2s_base, I2S_RESET, I2S_RESET_TX_PULL_UP);

		while (!((i2s_read_reg(i2s->i2s_base, TX_STATUS) & RESET_TX_SCLK) >> 23)) {
			if ((retry++) > I2S_RETRY_COUNT)
				break;
		}

		if (retry > I2S_RETRY_COUNT)
			dev_err(i2s->dev, "WARNING!!! I2S TX RESET failed\n");

		i2s_write_reg(i2s->i2s_base, I2S_RESET, I2S_RESET_TX_PULL_DOWN);

	} else { /* reset RX*/
		i2s_write_reg(i2s->i2s_base, I2S_RESET, I2S_RESET_RX_PULL_UP);

		while (!((i2s_read_reg(i2s->i2s_base, RX_STATUS) & RESET_RX_SCLK) >> 23)) {
			if ((retry++) > I2S_RETRY_COUNT)
				break;
		}

		if (retry > I2S_RETRY_COUNT)
			dev_err(i2s->dev, "WARNING!!! I2S RX RESET failed\n");

		i2s_write_reg(i2s->i2s_base, I2S_RESET, I2S_RESET_RX_PULL_DOWN);
	}
}

static irqreturn_t i2s_irq_handler(int irq, void *dev_id)
{
	struct i2s_obj *i2s = dev_id;
	u32 val = 0;

	val = i2s_read_reg(i2s->i2s_base, I2S_INT);

	if (i2s->active >= 1) { /* If I2S is really active */
		if (val & (I2S_INT_RXFO | I2S_INT_RXFU)) {
			dev_dbg(i2s->dev, "WARNING!!! I2S RX FIFO exception occur int_status=0x%x\n", val);
			i2s_clear_irqs(i2s, SNDRV_PCM_STREAM_CAPTURE);
			i2s_write_reg(i2s->i2s_base, I2S_ENABLE, I2S_OFF);
			i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0,
				      (i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0) | AUD_ENABLE));
			i2s_fifo_reset(i2s, SNDRV_PCM_STREAM_CAPTURE);
			i2s_reset(i2s, SNDRV_PCM_STREAM_CAPTURE);
			i2s_write_reg(i2s->i2s_base, I2S_ENABLE, I2S_ON);
		} else if (val & (I2S_INT_TXFO | I2S_INT_TXFU)) {
			dev_dbg(i2s->dev, "WARNING!!! I2S TX FIFO exception occur int_status=0x%x\n", val);
			i2s_clear_irqs(i2s, SNDRV_PCM_STREAM_PLAYBACK);
			i2s_write_reg(i2s->i2s_base, I2S_ENABLE, I2S_OFF);
			i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0,
				      (i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0) | AUD_ENABLE));
			i2s_fifo_reset(i2s, SNDRV_PCM_STREAM_PLAYBACK);
			i2s_reset(i2s, SNDRV_PCM_STREAM_PLAYBACK);
			i2s_write_reg(i2s->i2s_base, I2S_ENABLE, I2S_ON);
		}
	}

	return IRQ_HANDLED;
}

static void i2s_start(struct i2s_obj *i2s,
		      struct snd_pcm_substream *substream)
{
	u32 i2s_enable = i2s_read_reg(i2s->i2s_base, I2S_ENABLE);
	u32 clk_ctrl = i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0);
	u32 blk_mode_setting = i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING);

	if (!strcmp(substream->pcm->card->shortname, "adc_card")) {
		if ((clk_ctrl & AUD_ENABLE) != AUD_ENABLE)
			i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0, clk_ctrl | AUD_ENABLE);
	} else {
		if (((blk_mode_setting & ROLE_MASK) == MASTER_MODE) && ((clk_ctrl & AUD_ENABLE) != AUD_ENABLE))
			i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0, clk_ctrl | AUD_ENABLE);
	}

	i2s_fifo_reset(i2s, substream->stream);
	i2s_reset(i2s, substream->stream);
	i2s_clear_irqs(i2s, substream->stream);
	i2s_enable_irqs(i2s, substream->stream);

	if (i2s_enable == I2S_OFF) {
		i2s_write_reg(i2s->i2s_base, I2S_ENABLE, I2S_ON);
		udelay(9);
	} else
		dev_err(i2s->dev, "WARNING!!!  I2S SHOULD NOT be in ON state\n");

	dev_dbg(i2s->dev,
		"blk_mode=0x%08x, clk_ctrl=0x%08x, int_en=0x%08x, frame_setting=0x%08x, slot_setting=0x%08x, data_format=0x%08x\n",
		i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING),
		i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0),
		i2s_read_reg(i2s->i2s_base, I2S_INT_EN),
		i2s_read_reg(i2s->i2s_base, FRAME_SETTING),
		i2s_read_reg(i2s->i2s_base, SLOT_SETTING1),
		i2s_read_reg(i2s->i2s_base, DATA_FORMAT));
	//i2s_debug(i2s);
}

static void i2s_stop(struct i2s_obj *i2s,
		     struct snd_pcm_substream *substream)
{
	u32 i2s_enable = i2s_read_reg(i2s->i2s_base, I2S_ENABLE);
	/* Don't to disalbe AUD_ENABLE due to external codec still need MCLK to do configuration */
	/* Don't to disalbe AUD_ENABLE due to external codec still need MCLK to do configuration */

	i2s_disable_irqs(i2s, substream->stream);
	if (!i2s->mclk_out) {
		u32 blk_mode_setting = i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING);
		u32 clk_ctrl = i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0);

		if (((blk_mode_setting & ROLE_MASK) == MASTER_MODE) && (clk_ctrl & AUD_ENABLE)) {
			dev_dbg(i2s->dev, "Disable aud_en\n");
			i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0, clk_ctrl & ~(AUD_ENABLE));
		}
	}

	if (i2s_enable == I2S_ON) {
		i2s_write_reg(i2s->i2s_base, I2S_ENABLE, I2S_OFF);
		i2s_fifo_reset(i2s, substream->stream);
	} else
		dev_err(i2s->dev, "WARNING!!! I2S SHOULD NOT be in OFF state\n");
}

static int i2s_suspend(struct snd_soc_dai *dai)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(dai);

	if (i2s->capability & CVI_I2S_MASTER)
		clk_disable(i2s->clk);
	return 0;
}

static int i2s_resume(struct snd_soc_dai *dai)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(dai);

	if (i2s->capability & CVI_I2S_MASTER)
		clk_enable(i2s->clk);
	return 0;
}

static int i2s_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(cpu_dai);

	dev_dbg(cpu_dai->dev, "%s start *cpu_dai = %p name = %s\n", __func__, cpu_dai, cpu_dai->name);
	cpu_dai->playback_dma_data = &i2s->play_dma_data;
	cpu_dai->capture_dma_data = &i2s->capture_dma_data;

	if (!cpu_dai->playback_dma_data) {
		dev_err(cpu_dai->dev, "%s playback_dma_data == NULL\n", __func__);
		return -EINVAL;
	}

	if (!cpu_dai->capture_dma_data) {
		dev_err(cpu_dai->dev, "%s capture_dma_data == NULL\n", __func__);
		return -EINVAL;
	}

	dev_dbg(cpu_dai->dev, "%s get dma data success!\n", __func__);

	return 0;
}

static int i2s_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *cpu_dai)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	union i2s_snd_dma_data *dma_data = NULL;

	dev_dbg(i2s->dev, "%s start *cpu_dai = %p name = %s\n", __func__, cpu_dai, cpu_dai->name);
	if (!(i2s->capability & CVI_I2S_RECORD) &&
	    substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		dev_dbg(i2s->dev, "%s return -EINVAL;\n", __func__);
		return -EINVAL;
	}

	if (!(i2s->capability & CVI_I2S_PLAY) &&
	    substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dev_dbg(i2s->dev, "%s return -EINVAL; 2\n", __func__);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &i2s->play_dma_data;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dma_data = &i2s->capture_dma_data;
	if (!dma_data) {
		dev_dbg(i2s->dev, "%s dma_data == NULL\n", __func__);
	}

	snd_soc_dai_set_dma_data(cpu_dai, substream, (void *)dma_data);
	dev_dbg(i2s->dev, "%s end cpu_dai->playback_dma_data = %p\n",
		__func__, cpu_dai->playback_dma_data);
	i2s->tx_substream = substream;
	dev_dbg(i2s->dev, "%s i2s_dev:%d,substream:%p\n", __func__, i2s->dev_id, i2s->tx_substream);
	return 0;
}

static void i2s_work_mode_config(struct i2s_obj *i2s, int stream)
{
	u32 blk_mode_setting = i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING) & ~(DMA_MODE_MASK);

	/* Configure to USE HW DMA*/
	i2s_write_reg(i2s->i2s_base, BLK_MODE_SETTING, blk_mode_setting | HW_DMA_MODE);

	/* Configure FIFO thresholds */
	i2s_write_reg(i2s->i2s_base, FIFO_THRESHOLD,
		      RX_FIFO_THRESHOLD(7) | TX_FIFO_THRESHOLD(7) | TX_FIFO_HIGH_THRESHOLD(31));
}

static int i2s_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(dai);
	struct i2s_data_fmt_config *config = &i2s->config;
	u32 frame_setting = i2s_read_reg(i2s->i2s_base, FRAME_SETTING) & ~(FRAME_LENGTH_MASK | FS_ACT_LENGTH_MASK);
	u32 slot_setting1 = i2s_read_reg(i2s->i2s_base, SLOT_SETTING1) & ~(SLOT_SIZE_MASK | DATA_SIZE_MASK);
	u32 clk_ctrl1 = 0;
	u32 data_format = i2s_read_reg(i2s->i2s_base, DATA_FORMAT) & ~(WORD_LENGTH_MASK | SKIP_TX_INACT_SLOT_MASK);
	u32 audio_clk = 0;
	u32 mclk_div = 0;
	u32 bclk_div = 0;

	config->ch_num = params_channels(params);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_U16_LE:
		config->data_size = 16;
		i2s->wss = WSS_16_CLKCYCLE;
		switch (i2s->mode) {
		case SND_SOC_DAIFMT_I2S:
		case SND_SOC_DAIFMT_RIGHT_J:
		case SND_SOC_DAIFMT_LEFT_J:
		case SND_SOC_DAIFMT_PDM:
			if (!strcmp(substream->pcm->card->shortname, "cvi_dac")) {
				frame_setting |= FRAME_LENGTH(64) | FS_ACT_LENGTH(32);
				i2s->wss = WSS_32_CLKCYCLE;
			} else
				frame_setting |= FRAME_LENGTH(32) | FS_ACT_LENGTH(16);

			break;
		case SND_SOC_DAIFMT_DSP_A:
		case SND_SOC_DAIFMT_DSP_B:
			frame_setting |= FRAME_LENGTH(config->data_size * config->ch_num) | FS_ACT_LENGTH(1);
			break;
		}
		//i2s_write_reg(dev->i2s_base, SLOT_SETTING1, slot_setting1 | SLOT_SIZE(16) | DATA_SIZE(16));
		if (!strcmp(substream->pcm->card->shortname, "cvi_dac"))
			slot_setting1 |= SLOT_SIZE(32) | DATA_SIZE(16);
		else
			slot_setting1 |= SLOT_SIZE(16) | DATA_SIZE(16);
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_U24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_U24_3LE:
		if (!strcmp(substream->pcm->card->shortname, "cv182x_adc") ||
		    !strcmp(substream->pcm->card->shortname, "cv182x_dac") ||
		    !strcmp(substream->pcm->card->shortname, "cv182xa_adc") ||
		    !strcmp(substream->pcm->card->shortname, "cv182xa_dac") ||
		    !strcmp(substream->pcm->card->shortname, "adc_card") ||
		    !strcmp(substream->pcm->card->shortname, "dac_card")) {
			dev_err(i2s->dev, "24 bit resolution is not supported\n");
			return -EINVAL;
		}

		config->data_size = 24;
		i2s->wss = WSS_32_CLKCYCLE;
		switch (i2s->mode) {
		case SND_SOC_DAIFMT_I2S:
		case SND_SOC_DAIFMT_RIGHT_J:
		case SND_SOC_DAIFMT_LEFT_J:
		case SND_SOC_DAIFMT_PDM:
			frame_setting |= FRAME_LENGTH(64) | FS_ACT_LENGTH(32);
			break;
		case SND_SOC_DAIFMT_DSP_A:
		case SND_SOC_DAIFMT_DSP_B:
			frame_setting |= FRAME_LENGTH(config->data_size * config->ch_num) | FS_ACT_LENGTH(1);
			break;
		}
		//i2s_write_reg(dev->i2s_base, SLOT_SETTING1, slot_setting1 | SLOT_SIZE(32) | DATA_SIZE(24));
		slot_setting1 |= SLOT_SIZE(32) | DATA_SIZE(24);
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
	case SNDRV_PCM_FORMAT_U32_LE:
		if (!strcmp(substream->pcm->card->shortname, "cv182x_adc") ||
		    !strcmp(substream->pcm->card->shortname, "cv182x_dac") ||
		    !strcmp(substream->pcm->card->shortname, "cv182xa_adc") ||
		    !strcmp(substream->pcm->card->shortname, "cv182xa_dac") ||
		    !strcmp(substream->pcm->card->shortname, "adc_card") ||
		    !strcmp(substream->pcm->card->shortname, "dac_card")) {
			dev_err(i2s->dev, "32 bit resolution is not supported\n");
			return -EINVAL;
		}
		config->data_size = 32;
		i2s->wss = WSS_32_CLKCYCLE;
		switch (i2s->mode) {
		case SND_SOC_DAIFMT_I2S:
		case SND_SOC_DAIFMT_RIGHT_J:
		case SND_SOC_DAIFMT_LEFT_J:
		case SND_SOC_DAIFMT_PDM:
			frame_setting |= FRAME_LENGTH(64) | FS_ACT_LENGTH(32);
			break;
		case SND_SOC_DAIFMT_DSP_A:
		case SND_SOC_DAIFMT_DSP_B:
			frame_setting |= FRAME_LENGTH(config->data_size * config->ch_num) | FS_ACT_LENGTH(1);
			break;
		}
		//i2s_write_reg(dev->i2s_base, SLOT_SETTING1, slot_setting1 | SLOT_SIZE(32) | DATA_SIZE(32));
		slot_setting1 |= SLOT_SIZE(32) | DATA_SIZE(32);
		break;

	default:
		dev_err(i2s->dev, "I2S unsupported PCM fmt\n");
		return -EINVAL;
	}
	i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
	i2s_write_reg(i2s->i2s_base, FRAME_SETTING, frame_setting);

	slot_setting1 &= ~SLOT_NUM_MASK;
	dev_dbg(i2s->dev, " set slot number=%d\n",	config->ch_num);
	switch (config->ch_num) {
	case EIGHT_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(8);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0xff); /* enable slot 0-7 for TDM */
		break;
	case SIX_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(6);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x3f); /* enable slot 0-5 for TDM */
		break;
	case FOUR_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(4);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x0f); /* enable slot 0-3 for TDM */
		break;
	case TWO_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(2);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x03); /* enable slot 0-1 for TDM */
		break;
	case ONE_CHANNEL_SUPPORT:
		if (!strcmp(substream->pcm->card->shortname, "adc_card")) {
			slot_setting1 |= SLOT_NUM(2);
			data_format |= SKIP_RX_INACT_SLOT;
		} else if (!strcmp(substream->pcm->card->shortname, "dac_card")) {
			slot_setting1 |= SLOT_NUM(2);
			data_format |= SKIP_TX_INACT_SLOT;
		} else
			slot_setting1 |= SLOT_NUM(1);

		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x01); /* enable slot 0-3 for TDM */
		break;
	default:
		dev_err(i2s->dev, "channel not supported\n");
		return -EINVAL;
	}

	i2s_work_mode_config(i2s, substream->stream); /* Config use HW DMA and FIFO threshold here */

	config->sample_rate = params_rate(params);
	//audio_clk = clk_get_rate(dev->clk);
	/* set audio_clk depends on audio format */
	switch (config->sample_rate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		audio_clk = FREQ_22579_MHZ;
		break;
	case 8000:
	case 16000:
	case 32000:
		if (!strcmp(substream->pcm->card->shortname, "adc_card") ||
		    !strcmp(substream->pcm->card->shortname, "dac_card"))
			audio_clk = FREQ_16384_MHZ;
		else
			audio_clk = FREQ_24576_MHZ;
		break;
	case 12000:
	case 24000:
	case 48000:
	case 96000:
	case 192000:
		audio_clk = FREQ_24576_MHZ;
		break;
	default:
		dev_err(i2s->dev, "Warning!!! this sample rate is not supported\n");
		return -1;
	}
	//audio_clk_debug(i2s->dev);
	subsys_set_mclk(0, audio_clk);
	//audio_clk_debug(i2s->dev);
	if (!strcmp(substream->pcm->card->shortname, "cvi_adc")) {
		/* cv183x internal adc codec need dynamic MCLK frequency input */
		int div = 1;

		switch (config->sample_rate) {
		case 8000:
		case 11025:
		case 12000:
			div = audio_clk / (1024 * config->sample_rate);
			clk_ctrl1 |= MCLK_DIV(div);
			break;
		case 16000:
		case 22050:
		case 24000:
			div = audio_clk / (512 * config->sample_rate);
			clk_ctrl1 |= MCLK_DIV(div);
			break;
		case 32000:
		case 44100:
		case 48000:
			div = audio_clk / (256 * config->sample_rate);
			clk_ctrl1 |= MCLK_DIV(div);
			break;
		}
	} else if (!strcmp(substream->pcm->card->shortname, "cv182x_adc") ||
			!strcmp(substream->pcm->card->shortname, "cv182x_dac")) {
		/* cv182x internal adc codec need dynamic MCLK frequency input */

		dev_info(i2s->dev, "%s set MCLK\n", __func__);
		switch (config->sample_rate) {
		case 8000:
			clk_ctrl1 |= MCLK_DIV(6);
			mclk_div = 6;
			break;
		case 11025:
			clk_ctrl1 |= MCLK_DIV(4);
			mclk_div = 4;
			break;
		case 16000:
		case 32000:
			clk_ctrl1 |= MCLK_DIV(3);
			mclk_div = 3;
			break;
		case 22050:
		case 44100:
		case 48000:
			clk_ctrl1 |= MCLK_DIV(2);
			mclk_div = 2;
			break;
		default:
			dev_err(i2s->dev, "%s doesn't support this sample rate\n", __func__);
			break;
		}
	} else if (!strcmp(substream->pcm->card->shortname, "adc_card") ||
			!strcmp(substream->pcm->card->shortname, "dac_card")) {
		/* cv182xa internal adc codec need dynamic MCLK frequency input */

		switch (config->sample_rate) {
		case 8000:
		case 16000:
		case 32000:
			/* apll is 16.384Mhz, no need to divide */
			clk_ctrl1 |= MCLK_DIV(1);
			mclk_div = 1;
			break;
		case 11025:
		case 22050:
		case 44100:
		case 48000:
			clk_ctrl1 |= MCLK_DIV(2);
			mclk_div = 2;
			break;
		default:
			dev_err(i2s->dev, "%s doesn't support this sample rate\n", __func__);
			break;
		}
	} else {
		if (audio_clk == FREQ_24576_MHZ || audio_clk == FREQ_22579_MHZ) {
			clk_ctrl1 |= MCLK_DIV(2);
			mclk_div = 2;
		} else
			dev_err(i2s->dev, "Get unexpected audio system clk=%d\n", audio_clk);
	}
	/* Configure I2S word length,  bclk_div and sync_div here*/
	switch (i2s->wss) {
	case (WSS_32_CLKCYCLE):
#if defined(CONFIG_ARCH_CV183X_ASIC)
		bclk_div = (audio_clk / 1000) / (WSS_32_CLKCYCLE * (config->sample_rate / 1000));
#else
		bclk_div = (audio_clk / 1000) / (WSS_32_CLKCYCLE * (config->sample_rate / 1000) * mclk_div);
#endif
		if (!strcmp(substream->pcm->card->shortname, "cvi_dac")) {
			switch (config->data_size) {
			case 32:
			case 24:
				data_format |= WORD_LEN_32;
				break;
			case 16:
				data_format |= WORD_LEN_16;
				break;
			}
		} else
			data_format |= WORD_LEN_32;
		break;
	case (WSS_24_CLKCYCLE):
#if defined(CONFIG_ARCH_CV183X_ASIC)
		bclk_div = (audio_clk / 1000) / (WSS_32_CLKCYCLE * (config->sample_rate / 1000));
#else
		bclk_div = (audio_clk / 1000) / (WSS_32_CLKCYCLE * (config->sample_rate / 1000) * mclk_div);
#endif
		data_format |= WORD_LEN_32;
		break;
	case (WSS_16_CLKCYCLE):
#if defined(CONFIG_ARCH_CV183X_ASIC)
			bclk_div = (audio_clk / 1000) / (WSS_16_CLKCYCLE * (config->sample_rate / 1000));
#else
			bclk_div = (audio_clk / 1000) / (WSS_16_CLKCYCLE * (config->sample_rate / 1000) * mclk_div);
#endif
		data_format |= WORD_LEN_16;
		break;
	default:
		dev_err(i2s->dev, "resolution not supported\n");
	}

	/* Configure word length */
	i2s_write_reg(i2s->i2s_base, DATA_FORMAT, data_format);

	clk_ctrl1 |= BCLK_DIV(bclk_div);
	dev_dbg(i2s->dev, "Set clock ctrl1=0x%08x\n", clk_ctrl1);
	i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL1, clk_ctrl1);

	dev_dbg(i2s->dev, "frame_setting=0x%08x, slot_setting1=0x%08x, clk_ctrl1=0x%08x, data_format=0x%08x\n",
		i2s_read_reg(i2s->i2s_base, FRAME_SETTING),
		i2s_read_reg(i2s->i2s_base, SLOT_SETTING1),
		i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL1),
		i2s_read_reg(i2s->i2s_base, DATA_FORMAT));

	return 0;
}

static void i2s_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	pr_err("%s\n", __func__);
}

static int i2s_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(dai);
	u32 blk_mode_setting = (i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING) & ~(TXRX_MODE_MASK));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		blk_mode_setting |= TX_MODE;
	else
		blk_mode_setting |= RX_MODE;

	i2s_write_reg(i2s->i2s_base, BLK_MODE_SETTING, blk_mode_setting);

	return 0;
}

static int i2s_trigger(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_pcm_stream_unlock_irq(substream);
		i2s->active++;
		i2s_resume(dai);
		i2s_start(i2s, substream);
		snd_pcm_stream_lock_irq(substream);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
//		snd_pcm_stream_unlock_irq(substream);
		i2s->active--;
		i2s_debug(i2s);
		i2s_stop(i2s, substream);
		i2s_suspend(dai);
//		snd_pcm_stream_lock_irq(substream);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int i2s_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	u32 blk_mode_setting = i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING) & ~(SAMPLE_EDGE_MASK | ROLE_MASK);
	u32 frame_setting = i2s_read_reg(i2s->i2s_base, FRAME_SETTING) &
			    ~(FS_POLARITY_MASK | FS_OFFSET_MASK | FS_IDEF_MASK | FS_ACT_LENGTH_MASK);
	u32 slot_setting1 = i2s_read_reg(i2s->i2s_base, SLOT_SETTING1);/* & ~(SLOT_NUM_MASK);*/
	int ret = 0;

	dev_dbg(i2s->dev, "%s, fmt=0x%08x\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* Set codec to Master mode, so I2S IP need to be Slave mode */
		blk_mode_setting |= SLAVE_MODE;
		i2s->role = SLAVE_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* Set codec to Slave mode, so I2S IP need to be Master mode */
		blk_mode_setting |= MASTER_MODE;
		i2s->role = MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		ret = -EINVAL;
		break;
	default:
		dev_dbg(i2s->dev, "%s : Invalid master/slave format\n", __func__);
		ret = -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		i2s->sample_edge = FMT_NB_NF;
		blk_mode_setting |= RX_SAMPLE_EDGE_N | TX_SAMPLE_EDGE_P | FS_SAMPLE_EDGE_P;
		frame_setting |= FS_ACT_LOW;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		i2s->sample_edge = FMT_NB_IF;
		blk_mode_setting |= RX_SAMPLE_EDGE_N | TX_SAMPLE_EDGE_P | FS_SAMPLE_EDGE_P;
		frame_setting |= FS_ACT_HIGH;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		i2s->sample_edge = FMT_IB_NF;
		blk_mode_setting |= RX_SAMPLE_EDGE_P | TX_SAMPLE_EDGE_N | FS_SAMPLE_EDGE_N;
		frame_setting |= FS_ACT_LOW;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		i2s->sample_edge = FMT_IB_IF;
		blk_mode_setting |= RX_SAMPLE_EDGE_P | TX_SAMPLE_EDGE_N | FS_SAMPLE_EDGE_N;
		frame_setting |= FS_ACT_HIGH;
		break;
	default:
		dev_dbg(i2s->dev, "%s : Invalid frame format\n", __func__);
		ret = -EINVAL;
		break;
	}

	i2s_write_reg(i2s->i2s_base, BLK_MODE_SETTING, blk_mode_setting);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_PDM:
		i2s->mode = SND_SOC_DAIFMT_I2S;
		frame_setting |= FS_OFFSET_1_BIT | FS_IDEF_FRAME_SYNC
				 | FS_ACT_LENGTH(((frame_setting & FRAME_LENGTH_MASK) + 1) / 2);
		i2s_write_reg(i2s->i2s_base, FRAME_SETTING, frame_setting);
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		i2s->mode = SND_SOC_DAIFMT_RIGHT_J;
		frame_setting |= NO_FS_OFFSET | FS_IDEF_FRAME_SYNC
				 | FS_ACT_LENGTH(((frame_setting & FRAME_LENGTH_MASK) + 1) / 2);
		i2s_write_reg(i2s->i2s_base, FRAME_SETTING, frame_setting);

		slot_setting1 &= ~(FB_OFFSET_MASK);
		slot_setting1 |= FB_OFFSET((((frame_setting & FS_ACT_LENGTH_MASK) >> 16)
					    - ((slot_setting1 & DATA_SIZE_MASK) >> 16)));
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		i2s->mode = SND_SOC_DAIFMT_LEFT_J;
		frame_setting |= NO_FS_OFFSET | FS_IDEF_FRAME_SYNC
				 | FS_ACT_LENGTH(((frame_setting & FRAME_LENGTH_MASK) + 1) / 2);
		i2s_write_reg(i2s->i2s_base, FRAME_SETTING, frame_setting);
		break;

	case SND_SOC_DAIFMT_DSP_A:
		i2s->mode = SND_SOC_DAIFMT_DSP_A;
		frame_setting |= FS_OFFSET_1_BIT | FS_IDEF_FRAME_SYNC | FS_ACT_LENGTH(1);
		i2s_write_reg(i2s->i2s_base, FRAME_SETTING, frame_setting);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		i2s->mode = SND_SOC_DAIFMT_DSP_B;
		frame_setting |= NO_FS_OFFSET | FS_IDEF_FRAME_SYNC | FS_ACT_LENGTH(1);
		i2s_write_reg(i2s->i2s_base, FRAME_SETTING, frame_setting);
		break;
	default:
		dev_dbg(i2s->dev, "%s : Invalid I2S mode\n", __func__);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int i2s_set_tdm_slot(struct snd_soc_dai *cpu_dai, unsigned int tx_mask,
			    unsigned int rx_mask, int slots, int width)
{
	struct i2s_obj *i2s = snd_soc_dai_get_drvdata(cpu_dai);
	u32 slot_setting1 = i2s_read_reg(i2s->i2s_base, SLOT_SETTING1) & ~(SLOT_NUM_MASK);

	/* Mode other than PDM/TDM mode*/
	if (slots == 0) {
		/* The other settings dont matter in I2S mode */
		return 0;
	}

	/* We have 16 channels anything outside that is not supported */
	if ((tx_mask & ~0xffff) != 0 || (rx_mask & ~0xffff) != 0)
		return -EINVAL;

	switch (width) {
	case 16:
		break;
	case 32:
		break;
	default:
		return -EINVAL;
	}

	switch (slots) {
	case EIGHT_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(8);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x00ff); /* enable slot 0-7 for TDM */
		break;
	case SIX_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(6);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x003f); /* enable slot 0-5 for TDM */
		break;
	case FOUR_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(4);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x000f); /* enable slot 0-3 for TDM */
		break;
	case TWO_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(2);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x0003); /* enable slot 0-1 for TDM */
		break;
	case ONE_CHANNEL_SUPPORT:
		slot_setting1 |= SLOT_NUM(2);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING1, slot_setting1);
		i2s_write_reg(i2s->i2s_base, SLOT_SETTING2, 0x0001); /* enable slot 0-3 for TDM */
		break;
	default:
		dev_err(i2s->dev, "slot number not supported\n");
		return -EINVAL;
	}

	return 0;
}

static struct snd_soc_dai_ops i2s_dai_ops = {
	.startup	= i2s_startup,
	.shutdown	= i2s_shutdown,
	.hw_params	= i2s_hw_params,
	.prepare	= i2s_prepare,
	.trigger	= i2s_trigger,
	.set_fmt	= i2s_set_fmt,
	.set_tdm_slot = i2s_set_tdm_slot,
};

static const struct snd_soc_component_driver i2s_component_driver = {
	.name		= "i2s_component",
};

static int i2s_configure_dai(struct i2s_obj *i2s,
			     struct snd_soc_dai_driver *i2s_dai,
			     unsigned int rates)
{

	struct device_node *np = i2s->dev->of_node;
	const char *capability;

	if (of_property_read_string(np, "capability", &capability) < 0)
		return -EINVAL;

	if ((!strcmp(capability, "tx")) || (!strcmp(capability, "txrx"))) {
		dev_dbg(i2s->dev, "CV: playback support\n");
		i2s_dai->playback.channels_min = 1;
		i2s_dai->playback.channels_max = 8;
		i2s_dai->playback.formats = SNDRV_PCM_FMTBIT_S32_LE
						| SNDRV_PCM_FMTBIT_S24_LE
						| SNDRV_PCM_FORMAT_S24_3LE
						| SNDRV_PCM_FMTBIT_U24_LE
						| SNDRV_PCM_FORMAT_U24_3LE
						| SNDRV_PCM_FMTBIT_S16_LE;
		i2s_dai->playback.rates = rates;
	} else {
		/* this device doesn't have playback capability */
		dev_dbg(i2s->dev, "CV: playback not support\n");
		i2s_dai->playback.channels_min = 0;
		i2s_dai->playback.channels_max = 0;
	}

	if ((!strcmp(capability, "rx")) || (!strcmp(capability, "txrx"))) {
		dev_dbg(i2s->dev, "CV: capature support\n");
		i2s_dai->capture.channels_min = 1;
		i2s_dai->capture.channels_max = 8;
		i2s_dai->capture.formats = SNDRV_PCM_FMTBIT_S32_LE
					       | SNDRV_PCM_FMTBIT_S24_LE
					       | SNDRV_PCM_FMTBIT_S16_LE;
		i2s_dai->capture.rates = rates;
	} else {
		/* this device doesn't have capature capability */
		dev_dbg(i2s->dev, "CV: capature not support\n");
		i2s_dai->capture.channels_min = 0;
		i2s_dai->capture.channels_max = 0;
	}

	dev_dbg(i2s->dev, "CV: i2s master/slave mode supported\n");
	i2s->capability |= CVI_I2S_MASTER | CVI_I2S_SLAVE;

	i2s->fifo_th = I2STDM_FIFO_DEPTH / 2;
	return 0;
}

static int i2s_configure_dai_by_dt(struct i2s_obj *i2s,
				   struct snd_soc_dai_driver *i2s_dai,
				   struct resource *res)
{
	int ret;
	struct device_node *np = i2s->dev->of_node;

	dev_dbg(i2s->dev, "%s start\n", __func__);
	ret = i2s_configure_dai(i2s, i2s_dai, SNDRV_PCM_RATE_8000_192000);
	if (ret < 0)
		return ret;

	/* Set TX parameters */
	if (of_property_match_string(np, "dma-names", "tx") >= 0) {
		dev_dbg(i2s->dev, "%s dma-names  tx\n", __func__);
		i2s->capability |= CVI_I2S_PLAY;
		i2s->play_dma_data.dt.addr = res->start + TX_WR_PORT_CH0;
		i2s->play_dma_data.dt.addr_width = 4;
		i2s->play_dma_data.dt.fifo_size = I2STDM_FIFO_DEPTH * I2STDM_FIFO_WIDTH;
		i2s->play_dma_data.dt.maxburst = 8;
	}

	/* Set RX parameters */
	if (of_property_match_string(np, "dma-names", "rx") >= 0) {
		dev_dbg(i2s->dev, "%s dma-names  rx\n", __func__);
		i2s->capability |= CVI_I2S_RECORD;
		i2s->capture_dma_data.dt.addr = res->start + RX_RD_PORT_CH0;
		i2s->capture_dma_data.dt.addr_width = 4;
		i2s->capture_dma_data.dt.fifo_size = I2STDM_FIFO_DEPTH * I2STDM_FIFO_WIDTH;
		i2s->capture_dma_data.dt.maxburst = 8;
	}

	return 0;
}

static int i2s_proc_show(struct seq_file *m, void *v)
{
	struct i2s_obj *i2s = m->private;

	if (i2s_read_reg(i2s->i2s_base, I2S_ENABLE))
		seq_printf(m, "\ni2s%d is enabled\n", i2s->dev_id);
	else
		seq_printf(m, "\ni2s%d is disabled\n", i2s->dev_id);

	seq_printf(m, "\n===== Dump I2S%d register status =====\n", i2s->dev_id);

	seq_printf(m, "\nblk_mode=0x%08x,          clk_ctrl=0x%08x,          int_en=0x%08x\n",
		   i2s_read_reg(i2s->i2s_base, BLK_MODE_SETTING),
		   i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0),
		   i2s_read_reg(i2s->i2s_base, I2S_INT_EN));

	seq_printf(m, "\nframe_setting=0x%08x,     slot_setting=0x%08x,      data_format=0x%08x\n",
		   i2s_read_reg(i2s->i2s_base, FRAME_SETTING),
		   i2s_read_reg(i2s->i2s_base, SLOT_SETTING1),
		   i2s_read_reg(i2s->i2s_base, DATA_FORMAT));

	seq_printf(m, "\ni2s_int=0x%08x,           rx_status=0x%08x,         tx_status=0x%08x\n",
		   i2s_read_reg(i2s->i2s_base, I2S_INT),
		   i2s_read_reg(i2s->i2s_base, RX_STATUS),
		   i2s_read_reg(i2s->i2s_base, TX_STATUS));

	seq_printf(m, "\ndma_req=0x%08x,           dma_ack=0x%08x\n",
		   i2s_read_reg(i2s->i2s_base, DMA_REQ_COUNT),
		   i2s_read_reg(i2s->i2s_base, DMA_ACK_COUNT));

	seq_printf(m, "\nclk_ctrl0=0x%08x,         clk_ctrl1=0x%08x\n",
		   i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0),
		   i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL1));

	return 0;
}

static int i2s_probe(struct platform_device *pdev)
{
	struct i2s_obj *i2s;
	struct resource *res;
	int ret, irq;
	struct snd_soc_dai_driver *i2s_dai;
	const char *clk_id;
	unsigned int val;
	struct proc_dir_entry *proc_i2s;
	char *i2s_dev_name;
	const char *mclk_out;

	dev_info(&pdev->dev, "%s 4\n", __func__);

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	i2s_dai = devm_kzalloc(&pdev->dev, sizeof(*i2s_dai), GFP_KERNEL);
	if (!i2s_dai)
		return -ENOMEM;

	i2s_dai->ops = &i2s_dai_ops;
	i2s_dai->name = "platform_i2s_dai";

	//for kernel version witch is less than 5.10.4
	//cvi_i2s_dai->suspend = cvi_i2s_suspend;
	//cvi_i2s_dai->resume = cvi_i2s_resume;

	i2s_dai->probe = i2s_dai_probe;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2s->i2s_base = devm_ioremap_resource(&pdev->dev, res);
	dev_dbg(&pdev->dev, "I2S get i2s_base=0x%p\n", i2s->i2s_base);
	if (IS_ERR(i2s->i2s_base))
		return PTR_ERR(i2s->i2s_base);

	i2s->dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq >= 0) {
		dev_dbg(&pdev->dev, "I2S get IRQ=0x%x\n", irq);
		ret = devm_request_irq(&pdev->dev, irq, i2s_irq_handler, 0,
				       pdev->name, i2s);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request irq\n");
			return ret;
		}
	}
	device_property_read_u32(&pdev->dev, "dev-id", &i2s->dev_id);
	dev_info(&pdev->dev, "dev_id=0x%x\n", i2s->dev_id);
	clk_id = "i2sclk";
	ret = i2s_configure_dai_by_dt(i2s, i2s_dai, res);
	i2s->clk = devm_clk_get(&pdev->dev, clk_id);
	if (ret < 0)
		return ret;

	if (i2s->capability & CVI_I2S_MASTER) {
		ret = clk_prepare_enable(i2s->clk);
		if (ret < 0) {
			dev_err(&pdev->dev, "I2S clock prepare failed\n");
			return ret;
		}
	}

	device_property_read_string(&pdev->dev, "mclk_out", &mclk_out);

	if (!strcmp(mclk_out, "true"))
		i2s->mclk_out = true;
	else
		i2s->mclk_out = false;

	val = i2s_read_reg(i2s->i2s_base, I2S_CLK_CTRL0);
	val &= ~(AUD_CLK_SOURCE_MASK);
	val &= ~(BCLK_OUT_FORCE_EN); /* blck_out output after transmission start */
#if defined(CONFIG_SND_SOC_CVITEK_USE_MCLK_IN)
	i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0, val | AUD_CLK_FROM_MCLK_IN);
#else
	if (i2s->mclk_out)
		i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0, val | AUD_CLK_FROM_PLL | MCLK_OUT_EN | AUD_ENABLE);
	/* Turn aud_en on due to external codec might need MCLK to do register initialization */
	else
		i2s_write_reg(i2s->i2s_base, I2S_CLK_CTRL0, val | AUD_CLK_FROM_PLL);
#endif

	dev_set_drvdata(&pdev->dev, i2s);
	ret = devm_snd_soc_register_component(&pdev->dev, &i2s_component_driver,
					      i2s_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "not able to register dai\n");
		goto err_clk_disable;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret == -EPROBE_DEFER) {
		dev_err(&pdev->dev, "failed to register PCM, deferring probe\n");
		return ret;
	}

	i2s_dev_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "audio_debug/i2s%d", i2s->dev_id);
	proc_i2s = proc_create_single_data(i2s_dev_name, 0444, NULL, i2s_proc_show, i2s);
	if (!proc_i2s)
		dev_err(&pdev->dev, "create proc node %s fail\n", i2s_dev_name);
	devm_kfree(&pdev->dev, i2s_dev_name);

	pm_runtime_enable(&pdev->dev);

	return 0;

err_clk_disable:
	if (i2s->capability & CVI_I2S_MASTER)
		clk_disable_unprepare(i2s->clk);
	return ret;
}

static int i2s_remove(struct platform_device *pdev)
{
	struct i2s_obj *i2s = dev_get_drvdata(&pdev->dev);

	if (i2s->capability & CVI_I2S_MASTER)
		clk_disable_unprepare(i2s->clk);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id i2s_of_match[] = {
	{ .compatible = "cvitek,cvitek-i2s", },
	{},
};

MODULE_DEVICE_TABLE(of, i2s_of_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int i2s_pm_suspend(struct device *dev)
{
	struct i2s_obj *i2s_dev = dev_get_drvdata(dev);

	if (!i2s_dev->reg_ctx) {
		i2s_dev->reg_ctx = devm_kzalloc(i2s_dev->dev, sizeof(struct i2s_reg_context), GFP_KERNEL);
		if (!i2s_dev->reg_ctx)
			return -ENOMEM;
	}

	i2s_dev->real_status = i2s_read_reg(i2s_dev->i2s_base, I2S_ENABLE);
	dev_dbg(i2s_dev->dev, "suspend check devID:%d,really_state:%d,stream:%p\n",
		i2s_dev->dev_id, i2s_dev->real_status, i2s_dev->tx_substream);
	if (i2s_dev->real_status && i2s_dev->tx_substream) {
		// stop dma
		snd_dmaengine_pcm_trigger(i2s_dev->tx_substream, SNDRV_PCM_TRIGGER_STOP);
		//stop i2s
		i2s_stop(i2s_dev, i2s_dev->tx_substream);
	}

	i2s_dev->reg_ctx->blk_setting = i2s_read_reg(i2s_dev->i2s_base, BLK_MODE_SETTING);
	i2s_dev->reg_ctx->frame_setting = i2s_read_reg(i2s_dev->i2s_base, FRAME_SETTING);
	i2s_dev->reg_ctx->slot_setting1 = i2s_read_reg(i2s_dev->i2s_base, SLOT_SETTING1);
	i2s_dev->reg_ctx->slot_setting2 = i2s_read_reg(i2s_dev->i2s_base, SLOT_SETTING2);
	i2s_dev->reg_ctx->data_format = i2s_read_reg(i2s_dev->i2s_base, DATA_FORMAT);
	i2s_dev->reg_ctx->blk_cfg = i2s_read_reg(i2s_dev->i2s_base, BLK_CFG);
	i2s_dev->reg_ctx->i2s_en = i2s_read_reg(i2s_dev->i2s_base, I2S_ENABLE);
	i2s_dev->reg_ctx->i2s_int_en = i2s_read_reg(i2s_dev->i2s_base, I2S_INT_EN);
	i2s_dev->reg_ctx->fifo_threshold = i2s_read_reg(i2s_dev->i2s_base, FIFO_THRESHOLD);
	i2s_dev->reg_ctx->i2s_lrck_master = i2s_read_reg(i2s_dev->i2s_base, I2S_LRCK_MASTER);
	i2s_dev->reg_ctx->i2s_clk_ctl0 = i2s_read_reg(i2s_dev->i2s_base, I2S_CLK_CTRL0);
	i2s_dev->reg_ctx->i2c_clk_ctl1 = i2s_read_reg(i2s_dev->i2s_base, I2S_CLK_CTRL1);
	i2s_dev->reg_ctx->i2s_pcm_synth = i2s_read_reg(i2s_dev->i2s_base, I2S_PCM_SYNTH);

	if (i2s_dev->capability & CVI_I2S_MASTER)
		clk_disable(i2s_dev->clk);

	return 0;
}

static int i2s_pm_resume(struct device *dev)
{
	struct i2s_obj *i2s_dev = dev_get_drvdata(dev);

	if (i2s_dev->capability & CVI_I2S_MASTER)
		clk_enable(i2s_dev->clk);

	i2s_write_reg(i2s_dev->i2s_base, BLK_MODE_SETTING, i2s_dev->reg_ctx->blk_setting);
	i2s_write_reg(i2s_dev->i2s_base, FRAME_SETTING, i2s_dev->reg_ctx->frame_setting);
	i2s_write_reg(i2s_dev->i2s_base, SLOT_SETTING1, i2s_dev->reg_ctx->slot_setting1);
	i2s_write_reg(i2s_dev->i2s_base, SLOT_SETTING2, i2s_dev->reg_ctx->slot_setting2);
	i2s_write_reg(i2s_dev->i2s_base, DATA_FORMAT, i2s_dev->reg_ctx->data_format);
	i2s_write_reg(i2s_dev->i2s_base, BLK_CFG, i2s_dev->reg_ctx->blk_cfg);
	i2s_write_reg(i2s_dev->i2s_base, I2S_ENABLE, i2s_dev->reg_ctx->i2s_en);
	i2s_write_reg(i2s_dev->i2s_base, I2S_INT_EN, i2s_dev->reg_ctx->i2s_int_en);
	i2s_write_reg(i2s_dev->i2s_base, FIFO_THRESHOLD, i2s_dev->reg_ctx->fifo_threshold);
	i2s_write_reg(i2s_dev->i2s_base, I2S_LRCK_MASTER, i2s_dev->reg_ctx->i2s_lrck_master);
	i2s_write_reg(i2s_dev->i2s_base, I2S_CLK_CTRL0, i2s_dev->reg_ctx->i2s_clk_ctl0);
	i2s_write_reg(i2s_dev->i2s_base, I2S_CLK_CTRL1, i2s_dev->reg_ctx->i2c_clk_ctl1);
	i2s_write_reg(i2s_dev->i2s_base, I2S_PCM_SYNTH, i2s_dev->reg_ctx->i2s_pcm_synth);

	if (i2s_dev->real_status && i2s_dev->tx_substream) {
		// start dma
		snd_dmaengine_pcm_trigger(i2s_dev->tx_substream, SNDRV_PCM_TRIGGER_START);
		//start i2s
		i2s_start(i2s_dev, i2s_dev->tx_substream);
	}
	return 0;
}

#else
#define i2s_pm_suspend	NULL
#define i2s_pm_resume	NULL
#endif

#ifdef CONFIG_PM
static int i2s_runtime_suspend(struct device *dev)
{
	struct i2s_obj *i2s = dev_get_drvdata(dev);

	if (i2s->capability & CVI_I2S_MASTER)
		clk_disable(i2s->clk);
	return 0;
}

static int i2s_runtime_resume(struct device *dev)
{
	struct i2s_obj *i2s = dev_get_drvdata(dev);

	if (i2s->capability & CVI_I2S_MASTER)
		clk_enable(i2s->clk);
	return 0;
}

static const struct dev_pm_ops pm_ops = {
	SET_RUNTIME_PM_OPS(i2s_runtime_suspend, i2s_runtime_resume, NULL)
#ifdef CONFIG_PM_SLEEP
	SET_SYSTEM_SLEEP_PM_OPS(i2s_pm_suspend, i2s_pm_resume)
#endif
};
#endif

static struct platform_driver i2s_driver = {
	.probe		= i2s_probe,
	.remove		= i2s_remove,
	.driver		= {
		.name	= "i2s_driver",
		.of_match_table = of_match_ptr(i2s_of_match),
#ifdef CONFIG_PM
		.pm = &pm_ops,
#endif
	},
};

module_platform_driver(i2s_driver);

MODULE_DESCRIPTION("ASoC I2S driver");
MODULE_AUTHOR("Xiaodong <xiaodong.fan@sophgo.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2s_driver");
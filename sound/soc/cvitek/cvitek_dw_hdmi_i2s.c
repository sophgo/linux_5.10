#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <sound/cv1835_i2s.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "local.h"
#include "cvitek_dw_hdmi_i2s.h"
#include "cv1835_i2s_subsys.h"

#define I2S_CHANNEL_MAX                4  /* The max channel 8 / 2 */

/*
 * Hardware LLI period length of the cvitek sysDMA, mirrored from
 * BM_DMA_PERIOD_LEN in drivers/dma/cvitek/cvitek-dma.h. The DMA cyclic prep
 * splits transfers into blocks of this size; aligning the ALSA buffer/period
 * byte sizes to it keeps the descriptor count sane (see the constraint setup
 * in cvi_dwi2s_hdmi_startup). Keep this in sync with the DMA driver.
 */
#define CVI_HDMI_DMA_PERIOD_ALIGN      64

// PLD NEED
#define CONFIG_USE_AUDIO_PLL

struct proc_dir_entry *proc_hdmi_dwi2s_dir;
u32 dwi2s_mode_txrx;
u32 dei2s_mode_masterslave;

static void __iomem *i2s_hdmi_clk_base;

#ifdef CONFIG_PM_SLEEP
static struct cvi_hdmi_dwi2s_reg_context *hdmi_pm_ctx;
#endif

static inline void i2s_write_reg(void __iomem *io_base, int reg, u32 val)
{
	iowrite32(val, io_base + reg);
}

static inline u32 i2s_read_reg(void __iomem *io_base, int reg)
{
	return ioread32(io_base + reg);
}

static void __maybe_unused dwi2s_hdmi_debug(void __iomem *i2s_base)
{
	int i = 0;

	pr_debug("DWI2S_HDMI_DEBUG:\n");
	pr_debug("DWI2S_EN:0x%x", i2s_read_reg(i2s_base, DWI2S_HDMI_EN));
	pr_debug("DWI2S_RX_BLOCK_EN:0x%x", i2s_read_reg(i2s_base, DWI2S_HDMI_RX_BLOCK_EN));
	pr_debug("DWI2S_TX_BLOCK_EN:0x%x", i2s_read_reg(i2s_base, DWI2S_HDMI_TX_BLOCK_EN));
	pr_debug("----\n");
	pr_debug("DWI2S_CLK_EN:0x%x", i2s_read_reg(i2s_base, DWI2S_HDMI_CLK_EN));
	pr_debug("DWI2S_CLK_CFG:0x%x", i2s_read_reg(i2s_base, DWI2S_HDMI_CLK_CFG));
	pr_debug("DWI2S_RX_FIFO_RESET:0x%x", i2s_read_reg(i2s_base, DWI2S_HDMI_RX_FIFO_RESET));
	pr_debug("DWI2S_TX_FIFO_RESET:0x%x", i2s_read_reg(i2s_base, DWI2S_HDMI_TX_FIFO_RESET));
	pr_debug("----\n");

	for(i = 0; i < I2S_CHANNEL_MAX; i++) {
		pr_debug("%s:===== channel %d INFO =======\n", __func__, i);
		pr_debug("DWI2S_RX(%d)_EN:0x%x", i, i2s_read_reg(i2s_base, DWI2S_HDMI_RERX(i)));
		pr_debug("DWI2S_TX(%d)_EN:0x%x", i, i2s_read_reg(i2s_base, DWI2S_HDMI_TERX(i)));
		pr_debug("DWI2S_RX(%d)_CFG_WLEN:0x%x", i, i2s_read_reg(i2s_base, DWI2S_HDMI_RCRX(i)));
		pr_debug("DWI2S_TX(%d)_CFG_WLEN:0x%x", i, i2s_read_reg(i2s_base, DWI2S_HDMI_TCRX(i)));
		pr_debug("DWI2S_IRQ(%d)_STATUS:0x%x", i, i2s_read_reg(i2s_base, DWI2S_HDMI_ISRX(i)));
		pr_debug("DWI2S_IRQ(%d)_MASK:0x%x", i, i2s_read_reg(i2s_base, DWI2S_HDMI_IMRX(i)));
		pr_debug("==================================\n");
	}
}

void dwi2s_hdmi_ctrl(struct cvi_i2s_dev *dev, u32 stream, unsigned int on)
{
	u32 dmacr = i2s_read_reg(dev->i2s_base, DWI2S_HDMI_DMACR);
	int channels = dev->config.chan_nr; // 2 4 6 8
	int ch_pairs = channels / 2; // each channel have L + R
	int i;

	pr_debug("%s: ------ enter dwi2s_hdmi_ctrl ------\n", __func__);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dmacr = dmacr & ~DMAEN_TXBLOCK_MASK;
		if (on) {
			dmacr |= DMAEN_TXBLOCK;
			i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TX_BLOCK_EN, I2S_TX_ON);

			for (i = 0; i < I2S_CHANNEL_MAX; i++)
				i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TERX(i), (i < ch_pairs) ? I2S_TX_ON : I2S_TX_OFF);

			pr_debug("[%s][%d]tx enable\n", __func__, __LINE__);
		} else {
			dmacr |= DMADIS_TXBLOCK;
			i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TX_BLOCK_EN, I2S_TX_OFF);
			for (i = 0; i < I2S_CHANNEL_MAX; i++)
				i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TERX(i), I2S_TX_OFF);

			pr_debug("[%s][%d]tx disable\n", __func__, __LINE__);
		}

	} else {
		dmacr = dmacr & ~DMAEN_RXBLOCK_MASK;
		if (on) {
			dmacr |= DMAEN_RXBLOCK;
			i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RX_BLOCK_EN, I2S_RX_ON);

			for (i = 0; i < I2S_CHANNEL_MAX; i++)
				i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RERX(i), (i < ch_pairs) ? I2S_RX_ON : I2S_RX_OFF);

			pr_debug("[%s][%d]rx enable\n", __func__, __LINE__);
		} else {
			dmacr |= DMADIS_RXBLOCK;
			i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RX_BLOCK_EN, I2S_RX_OFF);
			for (i = 0; i < I2S_CHANNEL_MAX; i++)
				i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RERX(i), I2S_RX_OFF);
			pr_debug("[%s][%d]rx disable\n", __func__, __LINE__);
		}

	}
	i2s_write_reg(dev->i2s_base, DWI2S_HDMI_DMACR, dmacr);
	dev_dbg(dev->dev, "[%s]i2s_base:%px, dmacr:0x%x\n", __func__, dev->i2s_base, i2s_read_reg(dev->i2s_base, DWI2S_HDMI_DMACR));
}

void dwi2s_hdmi_switch(struct cvi_i2s_dev *dev, unsigned int on)
{
	pr_debug("[%s]rrbase:%px, DWI2S_HDMI_EN:0x%x\n", __func__, dev->i2s_base, i2s_read_reg(dev->i2s_base, DWI2S_HDMI_EN));

	if (on)
		 i2s_write_reg(dev->i2s_base, DWI2S_HDMI_EN, I2S_ON);
	 else
		 i2s_write_reg(dev->i2s_base, DWI2S_HDMI_EN, I2S_OFF);

	pr_debug("[%s]DWI2S_HDMI_EN:0x%x\n", __func__, i2s_read_reg(dev->i2s_base, DWI2S_HDMI_EN));
}

// update TX/RX FIFO
void dwi2s_hdmi_fifo_reset(struct cvi_i2s_dev *dev, u32 stream)
{
	/* First:
		dwi2s_ctrl(dev, SNDRV_PCM_STREAM_PLAYBACK, I2S_TX_OFF);
		dwi2s_ctrl(dev, SNDRV_PCM_STREAM_CAPTURE, I2S_RX_OFF);
		dwi2s_switch(dev, I2S_OFF);
	*/
	int i;
	pr_debug("[%s] enter, stream=%d\n", __func__, stream);

	if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TX_FIFO_RESET, DWI2S_FIFO_FLUSH_VAL);
		for (i = 0; i < I2S_CHANNEL_MAX; i++)
			i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TFFX(i), DWI2S_FIFO_FLUSH_VAL);

		pr_debug("[%s][%d]tx flush\n", __func__, __LINE__);

	} else {
		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RX_FIFO_RESET, DWI2S_FIFO_FLUSH_VAL);
		for (i = 0; i < I2S_CHANNEL_MAX; i++)
			i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RFFX(i), DWI2S_FIFO_FLUSH_VAL);

		pr_debug("[%s][%d]rx flush\n", __func__, __LINE__);
	}
}

static inline void dwi2s_hdmi_disable_irqs(struct cvi_i2s_dev *dev, u32 stream)
{
	int i;
	u32 irq;
	pr_debug("[%s] enter, stream=%d\n", __func__, stream);

	for (i = 0; i < I2S_CHANNEL_MAX; i++) {
		irq = i2s_read_reg(dev->i2s_base, DWI2S_HDMI_IMRX(i));

		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			irq |= (DWI2S_INT_TXFE | DWI2S_INT_TXFO);
		} else {
			irq |= (DWI2S_INT_RXDA | DWI2S_INT_RXFO);
		}

		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_IMRX(i), irq);

		dev_dbg(dev->dev, "[%s]irq(%d)mask:0x%x \n", __func__, i,
			i2s_read_reg(dev->i2s_base, DWI2S_HDMI_IMRX(i)));
	}
}

static inline void dwi2s_hdmi_enable_irqs(struct cvi_i2s_dev *dev, u32 stream)
{
	int i;
	u32 irq;

	for (i = 0; i < dev->config.chan_nr / 2; i++) {
		irq = i2s_read_reg(dev->i2s_base, DWI2S_HDMI_IMRX(i));

		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			irq &= ~(DWI2S_INT_TXFE | DWI2S_INT_TXFO);
		else
			irq &= ~(DWI2S_INT_RXDA | DWI2S_INT_RXFO);

		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_IMRX(i), irq);
	}
}

static inline void dwi2s_hdmi_clear_irqs(struct cvi_i2s_dev *dev, u32 stream)
{
	int i;
	pr_debug("[%s] enter, stream=%d\n", __func__, stream);

	for (i = 0; i < I2S_CHANNEL_MAX; i++) {
		if (stream == SNDRV_PCM_STREAM_PLAYBACK) {
			i2s_read_reg(dev->i2s_base, DWI2S_HDMI_TORX(i));
		} else {
			i2s_read_reg(dev->i2s_base, DWI2S_HDMI_RORX(i));
		}
	}
}

static irqreturn_t dwi2s_hdmi_irq_handler(int irq, void *dev_id)
{
	struct cvi_i2s_dev *dev = dev_id;
	u32 isr;
	int i;
	bool rx_overflow = false, tx_overflow = false;

	for (i = 0; i < I2S_CHANNEL_MAX; i++) {
		isr = i2s_read_reg(dev->i2s_base, DWI2S_HDMI_ISRX(i));
		if (isr & DWI2S_INT_RXFO)
			rx_overflow = true;
		if (isr & DWI2S_INT_TXFO)
			tx_overflow = true;
	}

	if (dev->active >= 1) {
		if (rx_overflow) {
			dev_warn(dev->dev, "WARNING!!! HDMI I2S RX FIFO overflow\n");
			dwi2s_hdmi_ctrl(dev, SNDRV_PCM_STREAM_CAPTURE, I2S_RX_OFF);
			dwi2s_hdmi_switch(dev, I2S_OFF);
			dwi2s_hdmi_fifo_reset(dev, SNDRV_PCM_STREAM_CAPTURE);
			dwi2s_hdmi_ctrl(dev, SNDRV_PCM_STREAM_CAPTURE, I2S_RX_ON);
			dwi2s_hdmi_switch(dev, I2S_ON);
		} else if (tx_overflow) {
			dev_warn(dev->dev, "WARNING!!! HDMI I2S TX FIFO overflow\n");
			dwi2s_hdmi_ctrl(dev, SNDRV_PCM_STREAM_PLAYBACK, I2S_TX_OFF);
			dwi2s_hdmi_switch(dev, I2S_OFF);
			dwi2s_hdmi_fifo_reset(dev, SNDRV_PCM_STREAM_PLAYBACK);
			dwi2s_hdmi_ctrl(dev, SNDRV_PCM_STREAM_PLAYBACK, I2S_TX_ON);
			dwi2s_hdmi_switch(dev, I2S_ON);
		}
	}

	dwi2s_hdmi_clear_irqs(dev, SNDRV_PCM_STREAM_PLAYBACK);
	dwi2s_hdmi_clear_irqs(dev, SNDRV_PCM_STREAM_CAPTURE);

	return IRQ_HANDLED;
}

static void dwi2s_hdmi_start(struct cvi_i2s_dev *dev,
				struct snd_pcm_substream *substream)
{
	pr_debug("[%s] enter, stream=%d\n", __func__, substream->stream);
	dwi2s_hdmi_clear_irqs(dev, substream->stream);
	dwi2s_hdmi_disable_irqs(dev, substream->stream);

	dwi2s_hdmi_fifo_reset(dev, substream->stream);

	dwi2s_hdmi_ctrl(dev, SNDRV_PCM_STREAM_PLAYBACK, false);
	dwi2s_hdmi_ctrl(dev, SNDRV_PCM_STREAM_CAPTURE, false);

	dwi2s_hdmi_ctrl(dev, substream->stream, true);

	dwi2s_hdmi_switch(dev, true);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		pr_debug("[HDMI-I2S] TX data sample: L=0x%08x R=0x%08x\n",
			i2s_read_reg(dev->i2s_base, DWI2S_HDMI_LRBR_THR(0)),
			i2s_read_reg(dev->i2s_base, DWI2S_HDMI_RRBR_THR(0)));
	}
}

static void dwi2s_hdmi_stop(struct cvi_i2s_dev *dev,
			 struct snd_pcm_substream *substream)
{
	pr_debug("[%s] enter, stream=%d\n", __func__, substream->stream);
	dwi2s_hdmi_clear_irqs(dev, substream->stream);
	dwi2s_hdmi_disable_irqs(dev, substream->stream);

	dwi2s_hdmi_ctrl(dev, substream->stream, false);
	dwi2s_hdmi_switch(dev, false);
	dwi2s_hdmi_fifo_reset(dev, substream->stream);

	dev_dbg(dev->dev,"test[%s]", __func__);
}

#ifdef CONFIG_PM
static int cvi_dwi2s_hdmi_runtime_suspend(struct device *dev)
{
	struct cvi_i2s_dev *cvi_dev = dev_get_drvdata(dev);
	pr_debug("[%s] enter\n", __func__);

	if (cvi_dev->capability & CVI_I2S_MASTER)
		clk_disable(cvi_dev->clk);
	return 0;
}

static int cvi_dwi2s_hdmi_runtime_resume(struct device *dev)
{
	struct cvi_i2s_dev *cvi_dev = dev_get_drvdata(dev);
	pr_debug("[%s] enter\n", __func__);

	if (cvi_dev->capability & CVI_I2S_MASTER)
		clk_enable(cvi_dev->clk);
	return 0;
}
#endif


static int cvi_dwi2s_hdmi_suspend(struct snd_soc_dai *dai)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	pr_debug("[%s] enter\n", __func__);

	if (dev->capability & CVI_I2S_MASTER)
		clk_disable(dev->clk);
	return 0;
}

static int cvi_dwi2s_hdmi_resume(struct snd_soc_dai *dai)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	pr_debug("[%s] enter\n", __func__);

	if (dev->capability & CVI_I2S_MASTER)
		clk_enable(dev->clk);

	return 0;
}

/*
 * hw_params rule: disable mmap ONLY for multichannel (>= 6 ch) playback.
 *
 * The FC<->LFE slot swap that fixes the HDMI IEC channel order is done by
 * the .process callback (hdmi_i2s_process), which is only invoked on the
 * copy_user (read/write) data path. Under mmap the application writes the
 * DMA buffer directly and .process is never called, so the swap would not
 * happen for 5.1/7.1 streams.
 *
 * We cannot decide this in .startup because the channel count is not known
 * until hw_params. Instead we register a constraint rule that makes the
 * ACCESS parameter depend on CHANNELS: when channels >= 6 we strip every
 * mmap access type from the mask, leaving only the RW (copy_user) variants,
 * which forces the swap path to run. For <= 4 channels (including plain
 * stereo) the mask is left untouched so mmap remains available and there is
 * zero behavioural change.
 */
static int cvi_dwi2s_hdmi_rule_mc_no_mmap(struct snd_pcm_hw_params *params,
					  struct snd_pcm_hw_rule *rule)
{
	struct snd_interval *ch = hw_param_interval(params,
							SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *access = hw_param_mask(params,
						SNDRV_PCM_HW_PARAM_ACCESS);
	struct snd_mask target;

	/*
	 * Only constrain when the stream is unambiguously multichannel, i.e.
	 * the minimum possible channel count is already >= 6. While the
	 * channel interval is still wide (e.g. 2..8 before refinement) we must
	 * not touch the access mask, otherwise we would wrongly forbid mmap
	 * for a stream that may end up being stereo.
	 */
	if (ch->min < 6)
		return 0;

	/* Keep only the read/write (non-mmap) interleaved/noninterleaved
	 * access types. */
	snd_mask_none(&target);
	snd_mask_set(&target, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
	snd_mask_set(&target, SNDRV_PCM_ACCESS_RW_NONINTERLEAVED);

	return snd_mask_refine(access, &target);
}

/*
 * Dump the ASoC binding info of the sound card this HDMI I2S DAI belongs to:
 * card name, DAI link, and every bound component / DAI (CPU DAI, codec,
 * platform). Uses dev_dbg so it stays silent unless dynamic debug is enabled:
 *   echo 'file cvitek_dw_hdmi_i2s.c +p' > /sys/kernel/debug/dynamic_debug/control
 */
static void cvi_dwi2s_hdmi_dump_card(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *cpu_dai)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card;
	struct snd_soc_dai *dai;
	int i;

	if (!rtd || !rtd->card)
		return;
	card = rtd->card;

	dev_dbg(cpu_dai->dev, "==== HDMI I2S sound card binding ====\n");
	dev_dbg(cpu_dai->dev, "card: name=%s longname=%s\n",
		card->name, card->long_name);
	dev_dbg(cpu_dai->dev, "dai_link: %s, stream=%s, num_components=%d\n",
		rtd->dai_link->name,
		substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "playback" : "capture",
		rtd->num_components);

	/* CPU DAI */
	for_each_rtd_cpu_dais(rtd, i, dai)
		dev_dbg(cpu_dai->dev, "  cpu  dai[%d]: %s (component=%s)\n",
			i, dai->name, dai->component ? dai->component->name : "?");

	/* Codec DAI */
	for_each_rtd_codec_dais(rtd, i, dai)
		dev_dbg(cpu_dai->dev, "  codec dai[%d]: %s (component=%s)\n",
			i, dai->name, dai->component ? dai->component->name : "?");

	/* All components (cpu/codec/platform) */
	for (i = 0; i < rtd->num_components; i++) {
		struct snd_soc_component *comp = rtd->components[i];

		dev_dbg(cpu_dai->dev, "  component[%d]: name=%s driver=%s\n",
			i, comp->name,
			comp->driver ? comp->driver->name : "?");
	}
	dev_dbg(cpu_dai->dev, "=====================================\n");
}

static int cvi_dwi2s_hdmi_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *cpu_dai)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);
	struct snd_pcm_runtime *runtime = substream->runtime;
	union cvi_i2s_snd_dma_data *dma_data = NULL;
	int ret;

	/* Dump the sound card binding info (only prints with dynamic debug on) */
	cvi_dwi2s_hdmi_dump_card(substream, cpu_dai);

	/*
	 * Align the DMA buffer and period sizes to 64 bytes
	 * (CVI_HDMI_DMA_PERIOD_ALIGN, = BM_DMA_PERIOD_LEN in the DMA driver)
	 * to avoid a descriptor explosion in the cvitek sysDMA that surfaces as
	 * -ENOMEM (-12) at trigger on some sample rates.
	 *
	 * The cvitek sysDMA cyclic prep (drivers/dma/cvitek/cvitek-dma.c,
	 * dwc_prep_dma_cyclic) computes the hardware LLI period length as:
	 *
	 *	 tmp_len	= 1 << (__ffs(buf_len) - 1);   // lowest set bit of buf_len
	 *	 period_len = min(period_len, CVI_HDMI_DMA_PERIOD_ALIGN);   // 64
	 *	 if (tmp_len < period_len) period_len = tmp_len;
	 *	 periods	= buf_len / period_len;		 // one descriptor each
	 *
	 * When ALSA negotiates a buffer size that is not a multiple of 64, the
	 * lowest set bit of buf_len is small (e.g. buf_len=176408 -> __ffs gives
	 * 8), so period_len collapses to that tiny value and 'periods' explodes
	 * (~22000 descriptors for 88.2kHz). Allocating that many descriptors
	 * from the GFP_ATOMIC dma_pool fails, dmaengine_prep_dma_cyclic()
	 * returns NULL, and the trigger reports -ENOMEM.
	 *
	 * Forcing both the buffer and period byte sizes to be multiples of 64
	 * keeps period_len at the full 64 bytes, so 'periods' becomes
	 * buf_len/64 (~2000 for the same case) - a ~10x reduction that the pool
	 * can satisfy. This does NOT cap the buffer size, so large-buffer rates
	 * that previously worked (e.g. 192kHz) keep working.
	 */
	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 CVI_HDMI_DMA_PERIOD_ALIGN);
	if (ret < 0) {
		dev_err(dev->dev, "%s: failed to align buffer bytes: %d\n",
			__func__, ret);
		return ret;
	}
	ret = snd_pcm_hw_constraint_step(runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					 CVI_HDMI_DMA_PERIOD_ALIGN);
	if (ret < 0) {
		dev_err(dev->dev, "%s: failed to align period bytes: %d\n",
			__func__, ret);
		return ret;
	}

	/*
	 * Register the "multichannel -> no mmap" rule for the playback stream.
	 * It makes ACCESS depend on CHANNELS so that only 5.1/7.1 streams are
	 * forced onto the copy_user path (where the FC/LFE swap happens).
	 */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_pcm_hw_rule_add(runtime, 0,
					  SNDRV_PCM_HW_PARAM_ACCESS,
					  cvi_dwi2s_hdmi_rule_mc_no_mmap, NULL,
					  SNDRV_PCM_HW_PARAM_CHANNELS, -1);
		if (ret < 0) {
			dev_err(dev->dev,
				"%s: failed to add mc-no-mmap rule: %d\n",
				__func__, ret);
			return ret;
		}
	}

	dev_dbg(dev->dev, "%s start *cpu_dai = %p name = %s\n", __func__, cpu_dai, cpu_dai->name);
	if (!(dev->capability & CVI_I2S_RECORD) &&
		(substream->stream == SNDRV_PCM_STREAM_CAPTURE)) {
		dev_err(dev->dev, "%s return -EINVAL;\n", __func__);
		return -EINVAL;
	}

	if (!(dev->capability & CVI_I2S_PLAY) &&
		(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)) {
		dev_err(dev->dev, "%s return -EINVAL; 2\n", __func__);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &dev->play_dma_data;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dma_data = &dev->capture_dma_data;
	if (dma_data == NULL) {
		dev_err(dev->dev, "%s dma_data == NULL\n", __func__);
	}

	dev_dbg(dev->dev, "%s start *dma_data = %px\n", __func__, dma_data);
	snd_soc_dai_set_dma_data(cpu_dai, substream, (void *)dma_data);
	dev_dbg(dev->dev, "%s end cpu_dai->playback_dma_data = %p\n",
		__func__, cpu_dai->playback_dma_data);

	return 0;
}

static void cvi_dwi2s_hdmi_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	pr_debug("%s not start *dai = %px, *dai->playback_dma_data = %px\n", __func__, dai, dai->playback_dma_data);
	/*
	 * Do NOT clear the DAI dma_data here.
	 *
	 * The dma_data points at dev->play_dma_data / dev->capture_dma_data,
	 * which are embedded union members of struct cvi_i2s_dev (not
	 * dynamically allocated). Their lifetime equals the device's, so there
	 * is nothing to free and startup() re-binds them on every open. Setting
	 * it to NULL here would only risk a NULL dereference if the pointer is
	 * touched after shutdown, so we intentionally leave it bound.
	 */
	//snd_soc_dai_set_dma_data(dai, substream, NULL);
}

/**
 * pld need
 */

void i2s_dw_set_sample_rate(struct cvi_i2s_dev *dev)
{
	unsigned int clk_ctrl = 0;
	unsigned int clk_div = 0;
	unsigned int div_multiplier = 1; /* if use audio PLL (25 or 24.576Mhz), div_multiplier should be 2 */
	unsigned int clk0 = 0;
	pr_debug("[%s] enter, sample_rate=%d\n", __func__, dev->config.sample_rate);
#if defined(CONFIG_USE_AUDIO_PLL)
	clk_ctrl = MCLK_DIV(2); /* audio PLL is 25 or 24.576 Mhz, need to div with 2*/
	div_multiplier = 1;
#else
	clk_ctrl = MCLK_DIV(1); /* mclk_in is 12.288 Mhz, no need to div*/
#endif

	switch (dev->config.sample_rate) {
		case 48000:
			clk_ctrl |= BCLK_DIV(1 * div_multiplier);
			clk_div = 128; /* 48kHz * 128 = 6.144mHz; clk_div = 786.432 / 6.144 = 128 */
			break;
		case 96000:
			clk_ctrl |= BCLK_DIV(1 * div_multiplier);
			clk_div = 64; /* 96kHz * 128 = 12.288mHz; clk_div = 786.432 / 12.288 = 64 */
			break;
		case 192000:
			clk_ctrl |= BCLK_DIV(1 * div_multiplier);
			clk_div = 32; /* 192kHz * 128 = 24.576mHz; clk_div = 786.432 / 24.576 = 32 */
			break;
		// A1pll 180.6336M
		case 44100:
			clk_ctrl |= BCLK_DIV(1 * div_multiplier);
			clk_div = 139; /* 44.1kHz * 128 = 5.6448mHz; clk_div = 1806.336 / 5.6448 = 139 */
			break;
		case 88200:
			clk_ctrl |= BCLK_DIV(1 * div_multiplier);
			clk_div = 69; /* 88.2kHz * 128 = 11.2896mHz; clk_div = 1806.336 / 11.2896 = 69 */
			break;
		}

	// get audio clk
#if defined(CONFIG_ARCH_CV84X6)
	i2s_write_reg(i2s_hdmi_clk_base, DIV_HSP_CLK_HDMI_AUD, (clk_div << 16) | 0x9);

	pr_debug("%s: DIV_HSP_CLK_HDMI_AUD_REG = 0x%08x \n", __func__,
			i2s_read_reg(i2s_hdmi_clk_base, DIV_HSP_CLK_HDMI_AUD));

	clk0 = AUD_CLK_FROM_PLL | DW_AUD_ENABLE;

	dwi2s_hdmi_set_mclk(DWI2S_MODE_REG_VAL_X2, clk0, clk_ctrl);
#endif
}

/*
 * FPLL6 (top_misc, phys base 0x28100000) fractional PLL programming for the
 * 44.1kHz audio family.
 *
 * clk_aud_hdmi = clk_fpll6 / N, N kept at the CCF default (4). To feed the
 * I2S divider with 180.6336MHz (= 44100 * 4096) we drive FPLL6 to
 * 722.5344MHz (= 180.6336M * 4).
 *
 * Clock path (see spec 4.1.4 + drivers/clk/cvitek/clk-pll.c):
 *   2000MHz -> FPLL6 SSC fractional divider (syn_set) -> clk_ssc(=FREF)
 *		   -> integer PLL (FBDIV/REFDIV/POSTDIV1/POSTDIV2) -> FPLL6_out
 *
 *   clk_ssc  = 2000MHz * 2^26 / syn_set
 *   FPLL6_out = clk_ssc * FBDIV / REFDIV / POSTDIV1 / POSTDIV2
 *
 * Chosen: syn_set=0x79CB2369 (FREF=65.685MHz),
 *		 FBDIV=22, REFDIV=1, POSTDIV1=1, POSTDIV2=2  (VCO=1445MHz)
 *		 => 722.534400MHz (0 ppm) -> /4 -> 180.6336MHz.
 *
 * The 48kHz family keeps the FPLL6 power-on default (786.432MHz -> /4 ->
 * 196.608MHz) and never calls this path.
 */
#define TOP_MISC_REG_BASE		0x28100000
#define TOP_MISC_PLL_STATUS_OFF		0x01C
#define TOP_MISC_FPLL6_CTRL_OFF		0x218
#define TOP_MISC_FPLL6_SSC_0_OFF	0x2B0
#define TOP_MISC_FPLL6_SSC_1_OFF	0x2B4
#define FPLL6_LOCK_BIT			BIT(6)	/* pll_status[6] = FPLL6 lock */

/* fpll6_ctrl: bypass=0, pllen=1, foutvcoen=1, foutpostdiven=1,
 * FBDIV=22, REFDIV=1, clk_en=1, clken_mux_sel=1, POSTDIV1=1, POSTDIV2=2 */
#define FPLL6_CTRL_722M		0x21C1016Eu
#define FPLL6_SSC_SET_722M	0x79CB2369u

/* Power-on default: FBDIV=16, REFDIV=1, POSTDIV1=2, POSTDIV2=1, syn_set
 * 0x51615555 (clk_ssc=98.304MHz) => VCO=1572.864MHz -> /2 -> 786.432MHz
 * -> /4 -> 196.608MHz (48kHz family). */
#define FPLL6_CTRL_786M		0x12C1010Eu
#define FPLL6_SSC_SET_786M	0x51615555u

/*
 * fpll6_reconfig - Retune FPLL6 to a new frequency.
 * @ctrl_on:  fpll6_ctrl value with clk_en=1 (final state)
 * @syn_set:  SSC fractional divider ratio (0x2B4)
 * @tag:	  log string
 *
 * Spreading is always disabled here (en_ssc/ssc_mode/bypass cleared) so the
 * fractional divider produces a clean 0ppm ratio.
 */
static void fpll6_reconfig(u32 ctrl_on, u32 syn_set, const char *tag)
{
	void __iomem *top_misc;
	u32 reg_val;
	u32 timeout;
	bool locked = false;

	top_misc = ioremap(TOP_MISC_REG_BASE, 0x1000);
	if (!top_misc) {
		pr_err("[FPLL6] ioremap top_misc failed\n");
		return;
	}

	/* --- Step 1: cut PLL->clkgen path (clk_en=0) during reconfig --- */
	iowrite32(ctrl_on & ~BIT(22), top_misc + TOP_MISC_FPLL6_CTRL_OFF);

	/* --- Step 2: program the SSC fractional divider (spreading OFF) --- */
	/* ssc_0: clear en_ssc(bit1), ssc_mode(bit3:2), bypass(bit4) so the
	 * fractional divider is active but performs no spread modulation. */
	reg_val = ioread32(top_misc + TOP_MISC_FPLL6_SSC_0_OFF);
	reg_val &= ~((1u << 1) | (0x3u << 2) | (1u << 4));
	iowrite32(reg_val, top_misc + TOP_MISC_FPLL6_SSC_0_OFF);
	/* set fractional ratio */
	iowrite32(syn_set, top_misc + TOP_MISC_FPLL6_SSC_1_OFF);
	/* pulse sw_up(bit0) to latch the new syn_set (w1t) */
	iowrite32(reg_val | (1u << 0), top_misc + TOP_MISC_FPLL6_SSC_0_OFF);
	udelay(1);

	/* --- Step 3: re-enable integer PLL and wait for relock --- */
	iowrite32(ctrl_on, top_misc + TOP_MISC_FPLL6_CTRL_OFF);
	for (timeout = 100000; timeout > 0; timeout--) {
		if (ioread32(top_misc + TOP_MISC_PLL_STATUS_OFF) &
			FPLL6_LOCK_BIT) {
			locked = true;
			break;
		}
	}
	if (!locked)
		pr_err("[FPLL6] WARN: %s lock timeout, pll_status=0x%08x\n",
			   tag, ioread32(top_misc + TOP_MISC_PLL_STATUS_OFF));
	else
		pr_debug("[FPLL6] locked %s, ctrl=0x%08x\n", tag,
			ioread32(top_misc + TOP_MISC_FPLL6_CTRL_OFF));

	iounmap(top_misc);
}

/*
 * fpll6_set_722m - Move FPLL6 to 722.5344MHz for the 44.1kHz audio family.
 * Only called for 44.1/88.2kHz.
 */
static void fpll6_set_722m(void)
{
	fpll6_reconfig(FPLL6_CTRL_722M, FPLL6_SSC_SET_722M, "722.5344MHz");
}

/*
 * fpll6_set_786m - Restore FPLL6 to its power-on default 786.432MHz for the
 * 48kHz audio family (48/96/192kHz -> aud_hdmi 196.608MHz).
 */
static void fpll6_set_786m(void)
{
	fpll6_reconfig(FPLL6_CTRL_786M, FPLL6_SSC_SET_786M, "786.432MHz");
}

/**
 * i2s_dw_set_sample_rate_ccf - Set HDMI audio sample rate using CCF
 * @dev: I2S device
 *
 * This function uses clk_set_rate() to configure the audio clock
 * instead of direct register writes. Target frequency = sample_rate * 128
 */
void i2s_dw_set_sample_rate_ccf(struct cvi_i2s_dev *dev)
{
	unsigned int clk_ctrl = 0;
	unsigned int clk_div = 0;
	unsigned int div_multiplier = 1;
	unsigned int slot_bits;
	unsigned int bclk_div;
	/*
	 * clk_aud_hdmi = clk_fpll6 / 4.
	 *   48kHz family (48/96/192k): FPLL6 default 786.432M -> 196.608MHz.
	 *   44.1kHz family (44.1/88.2k): reprogram FPLL6 to 722.5344M
	 *								-> 180.6336MHz.
	 * 180.6336M = 44100 * 4096 divides both 44.1k*64 and 88.2k*64 exactly.
	 */
	unsigned long input_rate = 196608000;
	unsigned long target_rate;

	pr_debug("[%s] enter, sample_rate=%d (direct div mode)\n", __func__, dev->config.sample_rate);
	clk_ctrl = MCLK_DIV(2);
	div_multiplier = 1;

	/*
	 * The 44.1kHz family is not an integer divide of 196.608MHz. Only for
	 * these rates move FPLL6 to 722.5344MHz so aud_hdmi becomes 180.6336MHz.
	 * The 48kHz family restores FPLL6 to its power-on default 786.432MHz
	 * (aud_hdmi 196.608MHz) in case it was left at 722.5344MHz by a prior
	 * 44.1kHz stream.
	 */
	if (dev->config.sample_rate % 11025 == 0) {
		fpll6_set_722m();
		input_rate = 180633600;
	} else {
		fpll6_set_786m();
		input_rate = 196608000;
	}

	/* HDMI audio requires sample_rate * 64 */
	target_rate = (unsigned long)dev->config.sample_rate * 64;

	/* Calculate divider based on the selected input clock */
	clk_div = input_rate / target_rate;
	dev_dbg(dev->dev, "Input rate %lu, target %lu, divider %u\n",
		input_rate, target_rate, clk_div);

	slot_bits = (dev->config.data_size <= 16) ? 16 : 32;
	if (dev->config.chan_nr == 0)
		dev->config.chan_nr = 2;

	bclk_div = 128 / (slot_bits * 2);
	if (bclk_div == 0)
		bclk_div = 1;

#if defined(CONFIG_ARCH_CV84X6)
	/* Write calculated divider in upper 16 bits, control bits 0x9 in lower bits */
	i2s_write_reg(i2s_hdmi_clk_base, DIV_HSP_CLK_HDMI_AUD, (clk_div << 16) | 0x9);
	dev_dbg(dev->dev, "DIV_HSP_CLK_HDMI_AUD = 0x%08x (div=%u)\n",
		i2s_read_reg(i2s_hdmi_clk_base, DIV_HSP_CLK_HDMI_AUD), clk_div);

	clk_ctrl |= BCLK_DIV(bclk_div * div_multiplier);
	dwi2s_hdmi_set_mclk(DWI2S_MODE_REG_VAL_X2, AUD_CLK_FROM_PLL | DW_AUD_ENABLE, clk_ctrl);
#endif
}

static int cvi_dwi2s_hdmi_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct i2s_clk_config_data *config = &dev->config;
	u32 clk_cfg_ws = 0;
	u32 wlen = 0;
	int i;
	pr_debug("[%s] enter\n", __func__);

	config->chan_nr = params_channels(params);
	dev_dbg(dev->dev, "[%s] channels=%d\n", __func__, config->chan_nr);

	switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
		case SNDRV_PCM_FORMAT_U16_LE:
			config->data_size = 16;
			dev->wss = WSS_16_CLKCYCLE;
			clk_cfg_ws |= WSS_SET(0x0);
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
		case SNDRV_PCM_FORMAT_U24_LE:
		case SNDRV_PCM_FORMAT_S24_3LE:
		case SNDRV_PCM_FORMAT_U24_3LE:
			config->data_size = 24;
			dev->wss = WSS_32_CLKCYCLE;
			clk_cfg_ws |= WSS_SET(0x2);
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
		case SNDRV_PCM_FORMAT_U32_LE:
			config->data_size = 32;
			dev->wss = WSS_32_CLKCYCLE;
			clk_cfg_ws |= WSS_SET(0x2);
			break;
		default:
			dev_err(dev->dev, "CVI-hdmi-i2s: unsupported PCM fmt\n");
			return -EINVAL;
	}

	clk_cfg_ws |= SCLKG_SET(0x0);
	i2s_write_reg(dev->i2s_base, DWI2S_HDMI_CLK_EN, I2S_OFF);
	i2s_write_reg(dev->i2s_base, DWI2S_HDMI_CLK_CFG, clk_cfg_ws);
	i2s_write_reg(dev->i2s_base, DWI2S_HDMI_CLK_EN, I2S_ON);


	switch (config->data_size) {
	case 16:
		wlen = DW_16_RESOLUTION;
		break;
	case 24:
		/*
		 * WLEN=24 with WSS=32, matching the validated bmtest config.
		 * The 24-bit data (S24_3LE 3-byte packed, or S24_LE 4-byte
		 * container) is fed to the TX FIFO unmodified via a 4-byte DMA
		 * bus width. The serializer takes 24 bits per 32-cycle slot;
		 * the remaining 8 clock cycles are padding.
		 */
		wlen = DW_24_RESOLUTION;
		break;
	case 32:
		wlen = DW_32_RESOLUTION;
		break;
	default:
		wlen = DW_32_RESOLUTION;
		break;
	}

	for (i = 0; i < I2S_CHANNEL_MAX; i++) {
		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RCRX(i), wlen);
		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TCRX(i), wlen);
		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_RFCRX(i), DWI2S_DEFAULT_FIFO_LEVEL);
		i2s_write_reg(dev->i2s_base, DWI2S_HDMI_TFCRX(i), DWI2S_DEFAULT_FIFO_LEVEL);
	}

	config->sample_rate = params_rate(params);

	/* Use CCF version for clock rate setting */
	i2s_dw_set_sample_rate_ccf(dev);

	dev_dbg(dev->dev, "[hdmi_i2s] clken=0x%08x, clk_cfg=0x%08x\n",
		i2s_read_reg(dev->i2s_base, DWI2S_HDMI_CLK_EN),
		i2s_read_reg(dev->i2s_base, DWI2S_HDMI_CLK_CFG));

	return 0;
}


static int cvi_dwi2s_hdmi_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *runtime = substream->runtime;

	pr_debug("[%s] enter, stream=%d\n", __func__, substream->stream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		dwi2s_mode_txrx |= TX_MODE;
		/* Ensure DMA buffer is sufficiently filled before DMA starts,
		 * otherwise the .process copy mode leaves unwritten regions as
		 * zeros which get transmitted on HDMI during startup. */
		runtime->start_threshold = runtime->buffer_size;

		/* Clear the whole DMA buffer so that any region not yet written
		 * by the application at stream start is transmitted as clean
		 * silence instead of stale/garbage data. This turns the HDMI TX
		 * startup gap into silence rather than glitches. */
		if (runtime->dma_area)
			memset(runtime->dma_area, 0, runtime->dma_bytes);
	} else {
		dwi2s_mode_txrx |= RX_MODE;
	}

	dev_dbg(dev->dev, "[%s]dwi2s_mode_txrx:%d\n", __func__, dwi2s_mode_txrx);
	return 0;
}

static int cvi_dwi2s_hdmi_trigger(struct snd_pcm_substream *substream,
			   int cmd, struct snd_soc_dai *dai)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	dev_dbg(dev->dev, "[%s]cmd:%d\n", __func__, cmd);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_pcm_stream_unlock_irq(substream);
		dev->active++;
		cvi_dwi2s_hdmi_resume(dai);
		dwi2s_hdmi_start(dev, substream);
#if 0
		dwi2s_debug(dev->i2s_base);
		codec_dump_regs(substream);
#endif
		dev_dbg(dev->dev, "[%s]0start_cmd:%d\n", __func__, cmd);
		snd_pcm_stream_lock_irq(substream);
		dev_dbg(dev->dev, "[%s]1start_cmd:%d\n", __func__, cmd);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_pcm_stream_unlock_irq(substream);
		dev->active--;
		dwi2s_hdmi_stop(dev, substream);
		cvi_dwi2s_hdmi_suspend(dai);
		dev_dbg(dev->dev, "[%s]end_cmd:%d\n", __func__, cmd);
		snd_pcm_stream_lock_irq(substream);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int cvi_dwi2s_hdmi_set_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);
	int ret = 0;

	dev_dbg(dev->dev, "%s, fmt=0x%08x\n", __func__, fmt);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* Set codec to Master mode, so I2S IP need to be Slave mode */
		dei2s_mode_masterslave |= SLAVE_MODE;
		dev->role = SLAVE_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* Set codec to Slave mode, so I2S IP need to be Master mode */
		dei2s_mode_masterslave |= MASTER_MODE;
		dev->role = MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		ret = -EINVAL;
		break;
	default:
		dev_err(dev->dev, "cvitek : Invalid master/slave format\n");
		ret = -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		dev->sample_edge = FMT_NB_NF;
		break;
	case SND_SOC_DAIFMT_NB_IF:
	case SND_SOC_DAIFMT_IB_NF:
	case SND_SOC_DAIFMT_IB_IF:
		dev->sample_edge = FMT_NB_NF;
		dev_err(dev->dev, "cvitek : only support FMT_NB_NF\n");
		break;
	default:
		dev_err(dev->dev, "cvitek : Invalid frame format\n");
		ret = -EINVAL;
		break;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_PDM:
		dev->mode = SND_SOC_DAIFMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		dev->mode = SND_SOC_DAIFMT_I2S;
		dev_err(dev->dev, "cvitek : only support daifmt_i2s\n");
		break;
	default:
		dev_err(dev->dev, "cvitek : Invalid I2S mode\n");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int cvi_dwi2s_hdmi_set_tdm_slot(struct snd_soc_dai *cpu_dai, unsigned int tx_mask,
				unsigned int rx_mask, int slots, int width)
{
	pr_debug("[%s]\n", __func__);
	return 0;
}

static int cvi_hdmi_configure_dai(struct cvi_i2s_dev *dev,
				 struct snd_soc_dai_driver *cvi_i2s_dai,
				 unsigned int rates)
{
	struct device_node *np = dev->dev->of_node;
	const char *capability;
	pr_debug("[%s] enter\n", __func__);

	if (of_property_read_string(np, "capability", &capability) < 0)
		return -EINVAL;


	if ((!strcmp(capability, "tx")) || (!strcmp(capability, "txrx"))) {
		dev_dbg(dev->dev, "CV: playback support\n");
		cvi_i2s_dai->playback.channels_min = 1;//4
		cvi_i2s_dai->playback.channels_max = 8;//4
		/*
		 * Advertise S24_3LE / U24_3LE (3-byte packed) in addition to
		 * the 4-byte-container formats. This mirrors the validated
		 * bmtest configuration: 3-byte packed data is DMA'd with a
		 * 4-byte bus width straight to the I2S TX FIFO, with WSS=32 and
		 * WLEN=24, and no data manipulation. The serializer takes the
		 * low 24 bits of each 32-bit FIFO word for transmission.
		 */
		cvi_i2s_dai->playback.formats = SNDRV_PCM_FMTBIT_S32_LE
						| SNDRV_PCM_FMTBIT_S24_LE
						| SNDRV_PCM_FMTBIT_U24_LE
						| SNDRV_PCM_FMTBIT_S24_3LE
						| SNDRV_PCM_FMTBIT_U24_3LE
						| SNDRV_PCM_FMTBIT_S16_LE;
		cvi_i2s_dai->playback.rates = rates;
	} else {
		/* this device doesn't have playback capability */
		dev_dbg(dev->dev, "CV: playback not support\n");
		cvi_i2s_dai->playback.channels_min = 0;
		cvi_i2s_dai->playback.channels_max = 0;
	}

	if ((!strcmp(capability, "rx")) || (!strcmp(capability, "txrx"))) {
		dev_dbg(dev->dev, "CV: capature support\n");
		cvi_i2s_dai->capture.channels_min = 1;//4
		cvi_i2s_dai->capture.channels_max = 8;//4
		cvi_i2s_dai->capture.formats = SNDRV_PCM_FMTBIT_S32_LE
						   | SNDRV_PCM_FMTBIT_S24_LE
						   | SNDRV_PCM_FMTBIT_S16_LE;
		cvi_i2s_dai->capture.rates = rates;
	} else {
		/* this device doesn't have capature capability */
		dev_dbg(dev->dev, "CV: capature not support\n");
		cvi_i2s_dai->capture.channels_min = 0;
		cvi_i2s_dai->capture.channels_max = 0;
	}

	dev_dbg(dev->dev, "CV: i2s master/slave mode supported\n");
	dev->capability |= CVI_I2S_MASTER | CVI_I2S_SLAVE;

	dev->fifo_th = I2STDM_FIFO_DEPTH / 2;
	return 0;
}

static int cvi_hdmi_configure_dai_by_dt(struct cvi_i2s_dev *dev,
				   struct snd_soc_dai_driver *cvi_i2s_dai,
				   struct resource *res)
{
	int ret;
	struct device_node *np = dev->dev->of_node;

	dev_dbg(dev->dev, "%s start\n", __func__);
	ret = cvi_hdmi_configure_dai(dev, cvi_i2s_dai, SNDRV_PCM_RATE_8000_192000);
	if (ret < 0)
		return ret;

	/* Set TX parameters */
	if (of_property_match_string(np, "dma-names", "tx") >= 0) {
		dev_dbg(dev->dev, "%s dma-names  tx\n", __func__);
		dev->capability |= CVI_I2S_PLAY;
		dev->play_dma_data.dt.addr = res->start + DWI2S_HDMI_TXDMA;
		dev->play_dma_data.dt.addr_width = 4;
		dev->play_dma_data.dt.fifo_size = I2STDM_FIFO_DEPTH * I2STDM_FIFO_WIDTH;//fifo_level
		dev->play_dma_data.dt.maxburst = 8;
		/*
		 * Set FLAG_PACK so the generic dmaengine PCM layer does NOT
		 * mask formats by physical width. Without this the layer drops
		 * every format whose physical width has no matching DMA bus
		 * width; S24_3LE (24-bit / 3-byte physical) would be filtered
		 * out because the cvitek sysDMA has no 3-byte bus width. We DMA
		 * the 3-byte packed stream with a 4-byte bus width (as bmtest
		 * does), so the DAI itself declares the supported formats and
		 * the automatic width-based filtering must be bypassed.
		 */
		dev->play_dma_data.dt.flags = SND_DMAENGINE_PCM_DAI_FLAG_PACK;
	}

	/* Set RX parameters */
	if (of_property_match_string(np, "dma-names", "rx") >= 0) {
		dev_dbg(dev->dev, "%s dma-names  rx\n", __func__);
		dev->capability |= CVI_I2S_RECORD;
		dev->capture_dma_data.dt.addr = res->start + DWI2S_HDMI_RXDMA;
		dev->capture_dma_data.dt.addr_width = 4;
		dev->capture_dma_data.dt.fifo_size = I2STDM_FIFO_DEPTH * I2STDM_FIFO_WIDTH;
		dev->capture_dma_data.dt.maxburst = 8;
	}

	return 0;

}

static int cvi_dw_i2s_hdmi_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct cvi_i2s_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);

	dev_dbg(cpu_dai->dev, "%s start *cpu_dai = %p name = %s\n", __func__, cpu_dai, cpu_dai->name);
	cpu_dai->playback_dma_data = &dev->play_dma_data;
	cpu_dai->capture_dma_data = &dev->capture_dma_data;

	if (cpu_dai->playback_dma_data == NULL) {
		dev_err(cpu_dai->dev, "%s playback_dma_data == NULL\n", __func__);
	}

	if (cpu_dai->capture_dma_data == NULL) {
		dev_err(cpu_dai->dev, "%s capture_dma_data == NULL\n", __func__);
	}

	dev_dbg(cpu_dai->dev, "%s end cpu_dai->playback_dma_data = %p\n", __func__, cpu_dai->playback_dma_data);
	dev_dbg(cpu_dai->dev, "%s end cpu_dai->capture_dma_data = %p\n", __func__, cpu_dai->capture_dma_data);
	return 0;
}

static int hdmi_i2s_proc_show(struct seq_file *m, void *v)
{
	struct cvi_i2s_dev *dev = m->private;
	int i;

	if (i2s_read_reg(dev->i2s_base, DWI2S_HDMI_EN))
		seq_printf(m, "\nhdmi_i2s%d is enabled\n", dev->dev_id);
	else
		seq_printf(m, "\nhdmi_i2s%d is disabled\n", dev->dev_id);

	seq_printf(m, "\n===== Dump HDMI I2S%d register status =====\n", dev->dev_id);

	seq_printf(m, "\nrx_block_en=0x%08x, tx_block_en=0x%08x\n",
		   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_RX_BLOCK_EN),
		   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_TX_BLOCK_EN));

	for (i = 0; i < I2S_CHANNEL_MAX; i++) {
		seq_printf(m, "\n--- channel %d ---\n", i);
		seq_printf(m, "rx_en=0x%08x, tx_en=0x%08x\n",
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_RERX(i)),
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_TERX(i)));
		seq_printf(m, "rx_cfg_wlen=0x%08x, tx_cfg_wlen=0x%08x\n",
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_RCRX(i)),
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_TCRX(i)));
		seq_printf(m, "rx_fifo_cfg=0x%08x, tx_fifo_cfg=0x%08x\n",
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_RFCRX(i)),
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_TFCRX(i)));
		seq_printf(m, "irq_mask=0x%08x, irq_status=0x%08x\n",
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_IMRX(i)),
			   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_ISRX(i)));
	}

	seq_printf(m, "\nclk_en=0x%08x, clk_cfg=0x%08x\n",
		   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_CLK_EN),
		   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_CLK_CFG));

	seq_printf(m, "dmacr=0x%08x\n",
		   i2s_read_reg(dev->i2s_base, DWI2S_HDMI_DMACR));

	return 0;
}

static int seq_i2s_open(struct inode *inode, struct file *file)
{
	return single_open(file, hdmi_i2s_proc_show, PDE_DATA(inode));
}

static const struct proc_ops hdmi_i2s_proc_ops = {
	.proc_read	= seq_read,
	.proc_open	= seq_i2s_open,
	.proc_release	= single_release,
};

static const struct snd_soc_component_driver cvi_dw_hdmi_i2s_component = {
	.name		= "cvitek-dw-hdmi-i2s",
};

static struct snd_soc_dai_ops cvi_dw_hdmi_i2s_dai_ops = {
	.startup	= cvi_dwi2s_hdmi_startup,
	.shutdown	= cvi_dwi2s_hdmi_shutdown,
	.hw_params	= cvi_dwi2s_hdmi_hw_params,
	.prepare	= cvi_dwi2s_hdmi_prepare,
	.trigger	= cvi_dwi2s_hdmi_trigger,
	.set_fmt	= cvi_dwi2s_hdmi_set_fmt,
	.set_tdm_slot = cvi_dwi2s_hdmi_set_tdm_slot,
};

static int hdmi_i2s_process(struct snd_pcm_substream *substream,
				int channel, unsigned long hwoff,
				void *buf, unsigned long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int frame_bytes = runtime->frame_bits / 8;
	int sample_bytes = runtime->sample_bits / 8;
	int channels = runtime->channels;
	char *dma_area = runtime->dma_area + hwoff;
	int frames = bytes / frame_bytes;
	int i;
	char tmp[4];

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	/*
	 * Guard against non-interleaved access (hazard #3).
	 *
	 * This .process callback assumes an interleaved frame layout: it
	 * reaches into a whole frame and swaps the FC/LFE samples that live
	 * at slot 2 and slot 3. In the ALSA copy path the interleaved case
	 * always calls us with channel == 0 and 'bytes' spanning all channels
	 * (see interleaved_copy() in sound/core/pcm_lib.c). The
	 * non-interleaved case instead calls us once per channel with a
	 * per-channel 'channel' index and single-channel 'bytes', which would
	 * make the frame-relative pointer arithmetic below index out of the
	 * intended data and corrupt the buffer.
	 *
	 * This dmaengine PCM only advertises SNDRV_PCM_INFO_INTERLEAVED, so we
	 * never expect a non-zero channel here; bail out defensively if it
	 * ever happens rather than scribbling over the buffer.
	 */
	if (channel != 0)
		return 0;

	/*
	 * No per-sample data manipulation for 24-bit formats. This matches
	 * the validated bmtest path: S24_3LE (3-byte packed) or S24_LE
	 * (4-byte container) data is sent to the I2S TX FIFO unmodified via
	 * a 4-byte DMA bus width, with WSS=32 and WLEN=24. The only fixup
	 * done here is the 6/8-channel slot swap below.
	 *
	 * Idempotency w.r.t. re-processing (hazard #1):
	 *
	 * This swap is in-place and NOT self-inverse-safe if applied twice to
	 * the same bytes. It relies on being invoked from the copy_user path
	 * (dmaengine_copy_user), which performs copy_from_user() into the DMA
	 * buffer immediately before calling us. Therefore every invocation
	 * operates on freshly copied, still-unswapped source data - even when
	 * the application rewinds and rewrites the same ring region, that
	 * region is overwritten with pristine user data first and then swapped
	 * exactly once. There is thus no cumulative double-swap on the RW
	 * path. (Under mmap this callback is not invoked at all; that trade-off
	 * is documented and accepted for this driver.)
	 */
	if (channels >= 6) {
		for (i = 0; i < frames; i++) {
			char *frame = dma_area + i * frame_bytes;
			char *ch2 = frame + 2 * sample_bytes;
			char *ch3 = frame + 3 * sample_bytes;

			memcpy(tmp, ch2, sample_bytes);
			memcpy(ch2, ch3, sample_bytes);
			memcpy(ch3, tmp, sample_bytes);
		}
	}

	return 0;
}

static int hdmi_i2s_prepare_slave_config(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
					 struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_dmaengine_dai_dma_data *dma_data;
	int ret;

	if (rtd->num_cpus > 1) {
		dev_err(rtd->dev, "%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	dma_data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);

	ret = snd_hwparams_to_dma_slave_config(substream, params, slave_config);
	if (ret)
		return ret;

	/*
	 * Force 4-byte DMA bus width for 24-bit formats. The cvitek sysDMA does
	 * not support a 3-byte bus width; treating the transfer as a 4-byte
	 * granular byte stream keeps the data intact while satisfying the DMA
	 * engine's supported widths.
	 */
	if (params_physical_width(params) == 24) {
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			slave_config->dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		else
			slave_config->src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	}

	snd_dmaengine_pcm_set_config_from_dai_data(substream, dma_data, slave_config);

	return 0;
}

static const struct snd_dmaengine_pcm_config hdmi_i2s_pcm_config = {
	.prepare_slave_config = hdmi_i2s_prepare_slave_config,
	.process = hdmi_i2s_process,
};

static int cvi_dw_hdmi_i2s_probe(struct platform_device *pdev)
{
	const struct i2s_platform_data *pdata = pdev->dev.platform_data;
	struct cvi_i2s_dev *dev;
	struct snd_soc_dai_driver *hdmi_i2s_dai;
	struct resource *res;
	struct resource *res_i2s;
	const char *clk_id;
	int ret, irq;
	char *i2s_dev_name;
	struct proc_dir_entry *proc_i2s;
	const char *mclk_out;

	pr_debug("%s: ======= enter cvi_dw_hdmi_i2s_probe =====\n", __func__);

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	hdmi_i2s_dai = devm_kzalloc(&pdev->dev, sizeof(*hdmi_i2s_dai), GFP_KERNEL);
	if (!hdmi_i2s_dai)
		return -ENOMEM;

	//for kernel version witch is less than 5.10.4
	//cvi_i2s_dai->suspend = cvi_dwi2s_suspend;
	//cvi_i2s_dai->resume = cvi_dwi2s_resume;
	hdmi_i2s_dai->ops = &cvi_dw_hdmi_i2s_dai_ops;
	hdmi_i2s_dai->name = "cvi_dw_i2s_probe";
	hdmi_i2s_dai->probe = cvi_dw_i2s_hdmi_dai_probe;


	res_i2s = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev->i2s_base = devm_ioremap_resource(&pdev->dev, res_i2s);
	if (IS_ERR(dev->i2s_base))
		return PTR_ERR(dev->i2s_base);
	dev_dbg(&pdev->dev, "I2S i2s_base phys=0x%llx virt=%px val=0x%x\n",
		 (u64)res_i2s->start, dev->i2s_base, readl(dev->i2s_base));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	i2s_hdmi_clk_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2s_hdmi_clk_base))
		return PTR_ERR(i2s_hdmi_clk_base);
	dev_dbg(&pdev->dev, "I2S aud_hdmi_clk_base phys=0x%llx virt=%px val=0x%x\n",
		 (u64)res->start, i2s_hdmi_clk_base, readl(i2s_hdmi_clk_base));
	dev->dev = &pdev->dev;

	irq = platform_get_irq(pdev, 0);
	if (irq >= 0) {
		dev_dbg(&pdev->dev, "I2S get IRQ=0x%x\n", irq);
		ret = devm_request_irq(&pdev->dev, irq, dwi2s_hdmi_irq_handler, 0,
					   pdev->name, dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to request irq\n");
			return ret;
		}
	}

	if (pdata) {
		dev->capability = pdata->cap;
		clk_id = NULL;
		dev->quirks = pdata->quirks;
	} else {
		clk_id = "hdmi_aud_clk";
		ret = cvi_hdmi_configure_dai_by_dt(dev, hdmi_i2s_dai, res_i2s);
		device_property_read_u32(&pdev->dev, "dev-id",
					 &dev->dev_id);
		dev->clk = devm_clk_get(&pdev->dev, clk_id);
		if (ret < 0)
			return ret;

		if (!IS_ERR(dev->clk)) {
			struct clk *parent = clk_get_parent(dev->clk);
			if (parent)
				dev_dbg(&pdev->dev, "hdmi_aud_clk rate = %ld, parent rate = %ld\n",
					 clk_get_rate(dev->clk), clk_get_rate(parent));
		}
	}

	if (dev->capability & CVI_I2S_MASTER) {
		if (pdata) {
			dev->i2s_clk_cfg = pdata->i2s_clk_cfg;
			if (!dev->i2s_clk_cfg) {
				dev_err(&pdev->dev, "no clock configure method\n");
				return -ENODEV;
			}
		}
		dev->clk = devm_clk_get(&pdev->dev, clk_id);

		if (IS_ERR(dev->clk))
			return PTR_ERR(dev->clk);

		ret = clk_prepare_enable(dev->clk);
		if (ret < 0) {
			dev_err(&pdev->dev, "I2S clock prepare failed\n");
			return ret;
		}
	}
	ret = device_property_read_string(&pdev->dev, "mclk_out", &mclk_out);

	if (!ret && !strcmp(mclk_out, "true"))
		dev->mclk_out = true;
	else
		dev->mclk_out = false;

//i2s subsys set I2S_CLK_CTRL0

 	dev_set_drvdata(&pdev->dev, dev);
	ret = devm_snd_soc_register_component(&pdev->dev, &cvi_dw_hdmi_i2s_component,
						  hdmi_i2s_dai, 1);
	if (ret != 0) {
		dev_err(&pdev->dev, "not able to register dai\n");
		goto dwerr_clk_disable;
	}

	if (!pdata) {
		/*
		 * Register with our custom config so that .prepare_slave_config
		 * (force 4-byte DMA bus width for 24-bit) and .process (6/8-ch
		 * slot swap) are used.
		 */
		ret = devm_snd_dmaengine_pcm_register(&pdev->dev,
							  &hdmi_i2s_pcm_config, 0);

		if (ret == -EPROBE_DEFER) {
			dev_err(&pdev->dev,
				"failed to register PCM, deferring probe\n");
			return ret;
		} else if (ret) {
			dev_err(&pdev->dev,
				"Could not register DMA PCM: %d\n"
				"falling back to PIO mode\n", ret);
			ret = cvi_pcm_register(pdev);
			if (ret) {
				dev_err(&pdev->dev,
					"Could not register PIO PCM: %d\n",
					ret);
				goto dwerr_clk_disable;
			}
		}
	}

	if (!proc_hdmi_dwi2s_dir) {
		proc_hdmi_dwi2s_dir = proc_mkdir("auddwi2s_hdmi_debug", NULL);
		if (!proc_hdmi_dwi2s_dir)
			dev_err(&pdev->dev, "Error creating auddwi2s_hdmi_debug proc folder entry\n");
	}


	if (proc_hdmi_dwi2s_dir) {
		i2s_dev_name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "i2s%d", dev->dev_id);
		proc_i2s = proc_create_data(i2s_dev_name, 0664, proc_hdmi_dwi2s_dir, &hdmi_i2s_proc_ops, dev);
		if (!proc_i2s)
			dev_err(&pdev->dev, "Create i2s%d status proc failed!\n", dev->dev_id);
		devm_kfree(&pdev->dev, i2s_dev_name);
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
dwerr_clk_disable:
	if (dev->capability & CVI_I2S_MASTER)
		clk_disable_unprepare(dev->clk);
	return -1;
};

static int cvi_dw_hdmi_i2s_remove(struct platform_device *pdev)
{
	struct cvi_i2s_dev *dev = dev_get_drvdata(&pdev->dev);
	pr_debug("[%s] enter\n", __func__);

	if (proc_hdmi_dwi2s_dir) {
		proc_remove(proc_hdmi_dwi2s_dir);
		proc_hdmi_dwi2s_dir = NULL;
	}

	if (dev->capability & CVI_I2S_MASTER)
		clk_disable_unprepare(dev->clk);

	pm_runtime_disable(&pdev->dev);
	return 0;
};

#ifdef CONFIG_OF
static const struct of_device_id cvi_dwi2s_hdmi_of_match[] = {
	{ .compatible = "cvitek,cvitek-dwi2s-hdmi", },
	{},
};

MODULE_DEVICE_TABLE(of, cvi_dwi2s_hdmi_of_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int cvi_dwi2s_hdmi_pm_suspend(struct device *dev)
{
	struct cvi_i2s_dev *i2s_dev = dev_get_drvdata(dev);
	int i;
	pr_debug("[%s] enter\n", __func__);

	if (!hdmi_pm_ctx) {
		hdmi_pm_ctx = devm_kzalloc(i2s_dev->dev, sizeof(*hdmi_pm_ctx), GFP_KERNEL);
		if (!hdmi_pm_ctx)
			return -ENOMEM;
	}

	hdmi_pm_ctx->ier = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_EN);
	hdmi_pm_ctx->irer = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_RX_BLOCK_EN);
	hdmi_pm_ctx->iter = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_TX_BLOCK_EN);
	hdmi_pm_ctx->cer = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_CLK_EN);
	hdmi_pm_ctx->ccr = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_CLK_CFG);

	for (i = 0; i < HDMI_I2S_CHANNEL_MAX; i++) {
		hdmi_pm_ctx->rer[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_RERX(i));
		hdmi_pm_ctx->ter[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_TERX(i));
		hdmi_pm_ctx->rcr[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_RCRX(i));
		hdmi_pm_ctx->tcr[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_TCRX(i));
		hdmi_pm_ctx->rfcr[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_RFCRX(i));
		hdmi_pm_ctx->tfcr[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_TFCRX(i));
		hdmi_pm_ctx->rff[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_RFFX(i));
		hdmi_pm_ctx->tff[i] = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_TFFX(i));
	}

	hdmi_pm_ctx->dmacr = i2s_read_reg(i2s_dev->i2s_base, DWI2S_HDMI_DMACR);

	if (i2s_dev->capability & CVI_I2S_MASTER)
		clk_disable(i2s_dev->clk);

	return 0;
}

static int cvi_dwi2s_hdmi_pm_resume(struct device *dev)
{
	struct cvi_i2s_dev *i2s_dev = dev_get_drvdata(dev);
	int i;
	pr_debug("[%s] enter\n", __func__);

	if (i2s_dev->capability & CVI_I2S_MASTER)
		clk_enable(i2s_dev->clk);

	if (!hdmi_pm_ctx)
		return 0;

	i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_EN, hdmi_pm_ctx->ier);
	i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_RX_BLOCK_EN, hdmi_pm_ctx->irer);
	i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_TX_BLOCK_EN, hdmi_pm_ctx->iter);
	i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_CLK_EN, hdmi_pm_ctx->cer);
	i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_CLK_CFG, hdmi_pm_ctx->ccr);

	for (i = 0; i < HDMI_I2S_CHANNEL_MAX; i++) {
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_RERX(i), hdmi_pm_ctx->rer[i]);
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_TERX(i), hdmi_pm_ctx->ter[i]);
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_RCRX(i), hdmi_pm_ctx->rcr[i]);
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_TCRX(i), hdmi_pm_ctx->tcr[i]);
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_RFCRX(i), hdmi_pm_ctx->rfcr[i]);
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_TFCRX(i), hdmi_pm_ctx->tfcr[i]);
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_RFFX(i), hdmi_pm_ctx->rff[i]);
		i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_TFFX(i), hdmi_pm_ctx->tff[i]);
	}

	i2s_write_reg(i2s_dev->i2s_base, DWI2S_HDMI_DMACR, hdmi_pm_ctx->dmacr);

	return 0;
}

#else
#define cvi_dwi2s_hdmi_pm_suspend	NULL
#define cvi_dwi2s_hdmi_pm_resume	NULL
#endif

static const struct dev_pm_ops cvi_dwi2s_hdmi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(cvi_dwi2s_hdmi_pm_suspend, cvi_dwi2s_hdmi_pm_resume)
	SET_RUNTIME_PM_OPS(cvi_dwi2s_hdmi_runtime_suspend, cvi_dwi2s_hdmi_runtime_resume, NULL)
};

static struct platform_driver cvi_dw_hdmi_i2s_driver = {
	.probe	= cvi_dw_hdmi_i2s_probe,
	.remove	= cvi_dw_hdmi_i2s_remove,
	.driver	= {
		.name = "cvitek-dw-hdmi-i2s",
		.pm = &cvi_dwi2s_hdmi_pm_ops,
		.of_match_table = cvi_dwi2s_hdmi_of_match,
	},
};
module_platform_driver(cvi_dw_hdmi_i2s_driver);

MODULE_DESCRIPTION("CVITEK CV84X6 HDMI DW-I2S audio driver");
MODULE_LICENSE("GPL");
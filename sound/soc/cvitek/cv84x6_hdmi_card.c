// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Machine driver for CV84X6 HDMI I2S audio output
 *
 * Data path: DDR -> DMA -> I2S TX FIFO -> I2S BUS -> HDMI TX -> TMDS -> Monitor
 */

#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

static struct snd_soc_dai_link_component cv84x6_hdmi_codec[] = {
	{ /* filled from DT in probe via cvi,codec phandle */ },
};

static struct snd_soc_dai_link_component cv84x6_hdmi_cpu[] = {
	{ /* filled from DT in probe */ },
};

static struct snd_soc_dai_link_component cv84x6_hdmi_platform[] = {
	{ /* filled from DT in probe */ },
};

static struct snd_soc_dai_link cv84x6_hdmi_dai = {
	.name = "cv84x6-hdmi-i2s",
	.stream_name = "HDMI Audio",
	.codecs = cv84x6_hdmi_codec,
	.num_codecs = ARRAY_SIZE(cv84x6_hdmi_codec),
	.cpus = cv84x6_hdmi_cpu,
	.num_cpus = ARRAY_SIZE(cv84x6_hdmi_cpu),
	.platforms = cv84x6_hdmi_platform,
	.num_platforms = ARRAY_SIZE(cv84x6_hdmi_platform),
	.dai_fmt = SND_SOC_DAIFMT_I2S
		| SND_SOC_DAIFMT_NB_NF
		| SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card cv84x6_hdmi_card = {
	.owner = THIS_MODULE,
	.long_name = "CV84X6 HDMI I2S Audio",
	.dai_link = &cv84x6_hdmi_dai,
	.num_links = 1,
};

static const struct of_device_id cv84x6_hdmi_audio_match[] = {
	{ .compatible = "cvitek,cv84x6-hdmi-audio", },
	{ }
};
MODULE_DEVICE_TABLE(of, cv84x6_hdmi_audio_match);

static int cv84x6_hdmi_audio_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &cv84x6_hdmi_card;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *codec_np;
	int ret;

	if (!np)
		return -ENODEV;

	of_property_read_string(np, "cvi,card_name", &card->name);
	of_property_read_string(np, "cvi,cpu_dai_name",
				&card->dai_link[0].cpus->dai_name);
	of_property_read_string(np, "cvi,platform_name",
				&card->dai_link[0].platforms->name);

	/*
	 * Resolve the HDMI audio codec via a phandle (cvi,codec) pointing at the
	 * HDMI TX node. The dw-hdmi I2S audio glue registers a codec DAI named
	 * "i2s-hifi" there, and ASoC will bind it by of_node instead of a
	 * hard-coded "hdmi-audio-codec.N.auto" platform id (which is unstable
	 * since the N auto-increment depends on probe ordering).
	 */
	codec_np = of_parse_phandle(np, "cvi,codec", 0);
	if (codec_np) {
		struct snd_soc_dai_link_component *c = &cv84x6_hdmi_codec[0];

		c->of_node = codec_np;
		c->dai_name = "i2s-hifi";
		/* Leave .name NULL so ASoC matches by of_node */
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed: %d\n", ret);
	return ret;
}

static struct platform_driver cv84x6_hdmi_audio_driver = {
	.driver = {
		.name = "cv84x6-hdmi-audio",
		.pm = &snd_soc_pm_ops,
		.of_match_table = cv84x6_hdmi_audio_match,
	},
	.probe = cv84x6_hdmi_audio_probe,
};

module_platform_driver(cv84x6_hdmi_audio_driver);

MODULE_DESCRIPTION("CV84X6 HDMI I2S audio machine driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cv84x6-hdmi-audio");
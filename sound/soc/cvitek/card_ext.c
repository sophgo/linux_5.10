#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

static int ext_card_hw_params(struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	return 0;
}

static int ext_card_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static const struct snd_soc_ops ext_card_ops = {
	.hw_params = ext_card_hw_params,
};

static struct snd_soc_dai_link_component ext_card_cpus[] = {
	{
		.name = "4110000.i2s",
		.dai_name = "4110000.i2s",
	},
	{
		.name = "4120000.i2s",
		.dai_name = "4120000.i2s",
	},
};

static struct snd_soc_dai_link_component ext_card_codecs[] = {
	{
		.name = "dummy_codec",
		.dai_name = "dummy_codec",
	},
	{
		.name = "dummy_codec",
		.dai_name = "dummy_codec",
	},
};

static struct snd_soc_dai_link_component ext_card_platform[] = {
	{
		.name = "4110000.i2s",
		.dai_name = "4110000.i2s",
	},
	{
		.name = "4120000.i2s",
		.dai_name = "4120000.i2s",
	},
};

static struct snd_soc_dai_link ext_card_dai_link[] = {
	{
		.name = "ext_record",
		.stream_name = "record_stream",
		.cpus = &ext_card_cpus[0],
		.num_cpus = 1,
		.codecs = &ext_card_codecs[0],
		.num_codecs = 1,
		.platforms = &ext_card_platform[0],
		.num_platforms = 1,
		.ops = &ext_card_ops,
		.init = ext_card_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S
		| SND_SOC_DAIFMT_IB_NF
		| SND_SOC_DAIFMT_CBS_CFS,
		.capture_only = 1,
	},
	{
		.name = "ext_playback",
		.stream_name = "play_stream",
		.cpus = &ext_card_cpus[1],
		.num_cpus = 1,
		.codecs = &ext_card_codecs[1],
		.num_codecs = 1,
		.platforms = &ext_card_platform[1],
		.num_platforms = 1,
		.ops = &ext_card_ops,
		.init = ext_card_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S
		| SND_SOC_DAIFMT_IB_NF
		| SND_SOC_DAIFMT_CBS_CFS,
		.playback_only = 1,
	},
};

static struct snd_soc_card ext_card = {
	.owner = THIS_MODULE,
	.dai_link = ext_card_dai_link,
	.num_links = ARRAY_SIZE(ext_card_dai_link),
};

static const struct of_device_id ext_dac_match_ids[] = {
	{
		.compatible = "cvitek,cvitek-ext",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ext_dac_match_ids);

static int ext_card_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct device_node *np = pdev->dev.of_node, *dai_node;
	const char *mode, *fmt, *role, *type;
	u8 idx = 0;
	u32	slot_no;

	dev_err(&pdev->dev, "%s dev name=%s\n",  __func__, dev_name(&pdev->dev));
	card = &ext_card;

	if (np) {
		of_property_read_string(np, "card_name", &card->name);
		card->dev = &pdev->dev;
		for_each_child_of_node(np, dai_node) {
			of_property_read_string(dai_node, "dai_name", &card->dai_link[idx].name);
			of_property_read_string(dai_node, "stream_name", &card->dai_link[idx].stream_name);
			of_property_read_string(dai_node, "cpu_dai_name", &ext_card_cpus[idx].name);
			of_property_read_string(dai_node, "codec_dai_name", &ext_card_codecs[idx].name);
			of_property_read_string(dai_node, "platform_name", &ext_card_platform[idx].name);
			of_property_read_string(dai_node, "cpu_dai_name", &ext_card_cpus[idx].dai_name);
			of_property_read_string(dai_node, "codec_dai_name", &ext_card_codecs[idx].dai_name);
			of_property_read_string(dai_node, "platform_name", &ext_card_platform[idx].dai_name);
			of_property_read_string(dai_node, "stream_type", &type);
			of_property_read_string(dai_node, "mode", &mode);
			of_property_read_string(dai_node, "fmt", &fmt);
			of_property_read_string(dai_node, "role", &role);
			of_property_read_u32(dai_node, "slot_no", &slot_no);

			if (!strcmp(type, "capture"))
				card->dai_link[idx].capture_only = 1;
			else if (!strcmp(type, "playback"))
				card->dai_link[idx].playback_only = 1;
			else
				card->dai_link[idx].playback_only = 1;

			if (!strcmp(mode, "I2S"))
				card->dai_link[idx].dai_fmt = SND_SOC_DAIFMT_I2S;
			else if (!strcmp(mode, "LEFT_J"))
				card->dai_link[idx].dai_fmt = SND_SOC_DAIFMT_LEFT_J;
			else if (!strcmp(mode, "RIGHT_J"))
				card->dai_link[idx].dai_fmt = SND_SOC_DAIFMT_RIGHT_J;
			else if (!strcmp(mode, "DSP_A"))
				card->dai_link[idx].dai_fmt = SND_SOC_DAIFMT_DSP_A; /* PCM and TDM belong to it */
			else if (!strcmp(mode, "DSP_B"))
				card->dai_link[idx].dai_fmt = SND_SOC_DAIFMT_DSP_B; /* PCM and TDM belong to it */
			else if (!strcmp(mode, "PDM"))
				card->dai_link[idx].dai_fmt = SND_SOC_DAIFMT_PDM;
			else
				dev_err(&pdev->dev, "%s, not support this mode\n", __func__);

			if ((!strcmp(mode, "I2S")) || (!strcmp(mode, "LEFT_J")) || (!strcmp(mode, "RIGHT_J"))) {
				if (!strcmp(fmt, "IBNF"))
					card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_IB_NF;
				else if (!strcmp(fmt, "IBIF"))
					card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_IB_IF;
				else if (!strcmp(fmt, "NBNF"))
					card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_NB_NF;
				else if (!strcmp(fmt, "NBIF"))
					card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_NB_IF;
				else
					dev_err(&pdev->dev, "%s, not support this sample format\n", __func__);
			} else
				card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_NB_IF;
			/* DSP_A, DSP_B and PDM(TDM) use NB_IF format */

			if (!strcmp(role, "master"))
				card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;
			else if (!strcmp(role, "slave"))
				card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_CBM_CFM;
			else
				dev_err(&pdev->dev, "%s, not support this role\n", __func__);

			if (!strcmp(mode, "PDM"))
				card->dai_link[idx].dai_fmt |= SND_SOC_DAIFMT_PDM;
			//	tdm_slot_no2 = slot_no; /* tdm_slot_no is only valid when mode is TDM/PDM */
			else if (slot_no != 2)
				dev_err(&pdev->dev, "Wrong solt number setting in %s mode\n", mode);

			dev_info(&pdev->dev, "%s, set DAI fmt to 0x%08x\n", __func__, card->dai_link[idx].dai_fmt);
			idx++;
		}
		card->num_links = idx;
		platform_set_drvdata(pdev, card);

		return devm_snd_soc_register_card(&pdev->dev, card);
	}
	return 0;

}

#ifdef CONFIG_PM
int ext_card_suspend(struct device *dev)
{
	return 0;
}

int ext_card_resume(struct device *dev)
{
	return 0;
}

int ext_card_poweroff(struct device *dev)
{
	return 0;
}

#else
#define ext_card_suspend	NULL
#define ext_card_resume		NULL
#define ext_card_poweroff	NULL
#endif

const struct dev_pm_ops ext_card_pm_ops = {
	.suspend = ext_card_suspend,
	.resume = ext_card_resume,
	.freeze = ext_card_suspend,
	.thaw = ext_card_resume,
	.poweroff = ext_card_poweroff,
	.restore = ext_card_resume,
};

static struct platform_driver ext_card_driver = {
	.driver = {
		.name = "ext_card_driver",
		.pm = &ext_card_pm_ops,
		.of_match_table = ext_dac_match_ids,
	},
	.probe = ext_card_probe,
};

module_platform_driver(ext_card_driver);

MODULE_DESCRIPTION("ASoC ext card driver");
MODULE_AUTHOR("Xiaodong <xiaodong.fan@sophgo.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform: ext_card");

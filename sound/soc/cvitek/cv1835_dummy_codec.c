// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Audio Dummy driver on CVITEK CV1835
 *
 * Copyright 2021 CVITEK
 *
 * Author: RuiLong
 *
 */
/*
 * Dummy sound card (machine driver, built on both arches).
 *
 * Built on both arches (selected by CONFIG_SND_SOC_CV183x_DUMMY_CARD).
 * Registers a dummy sound card that links I2S controllers to the dummy
 * codec DAIs exposed by the arch-specific codec file below; used for
 * I2S self-loopback testing without a real codec chip.
 *
 * Arch-specific codec files:
 *   - cv84x6_dummy_codec.c : X2 (CV84X6), 4 codec instances (.0/.1/.2/.3)
 *   - dummy_codec.c         : A2 (CV186X), 1 codec instance (no suffix)
 *
 * CV1835_DUMMY_MAX_LINKS is a compile-time upper bound only; the link
 * arrays are allocated by the actual num_links from DTS, so a single
 * shared value of 3 is harmless on A2 (which uses <=2 links).
 */

#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/of_address.h>

u16 tdm_slot_no2 = 2;

#define CV1835_DUMMY_MAX_LINKS 3

/*
 * Configure audio_sys mux registers for IIS loopback routing.
 * DTS property cvi,loopback-mux = <&audio_sys addr1 mask1 val1 addr2 mask2 val2 ...>;
 * phandle points to the audio_sys node (with 'reg'), followed by (addr, mask, val) triples.
 * Each triple does read-modify-write: new = (cur & ~mask) | (val & mask).
 * No property => skip (compatible with A2 / non-loopback boards).
 */
static int cv1835_dummy_config_loopback_mux(struct platform_device *pdev,
					    struct device_node *np)
{
	struct device_node *sys_np;
	void __iomem *base;
	struct resource res;
	u32 args[24];
	int n, i, ret;

	n = of_property_read_variable_u32_array(np, "cvi,loopback-mux",
						args, 2, ARRAY_SIZE(args));
	if (n < 0)
		return 0; /* property absent => nothing to do */
	/* format: <phandle addr mask val ...> = 1 + 3N, (n-1) must be divisible by 3 */
	if (n < 4 || ((n - 1) % 3) != 0) {
		dev_err(&pdev->dev,
			"%s: cvi,loopback-mux must be <phandle addr mask val ...>, got %d u32\n",
			__func__, n);
		return -EINVAL;
	}

	/* args[0] is phandle to audio_sys node */
	sys_np = of_find_node_by_phandle(args[0]);
	if (!sys_np) {
		dev_err(&pdev->dev, "%s: audio_sys phandle 0x%x not found\n",
			__func__, args[0]);
		return -EINVAL;
	}

	ret = of_address_to_resource(sys_np, 0, &res);
	of_node_put(sys_np);
	if (ret) {
		dev_err(&pdev->dev, "%s: cannot get audio_sys reg\n", __func__);
		return ret;
	}

	base = devm_ioremap(&pdev->dev, res.start, resource_size(&res));
	if (!base) {
		dev_err(&pdev->dev, "%s: ioremap audio_sys failed\n", __func__);
		return -ENOMEM;
	}

	dev_info(&pdev->dev, "%s: audio_sys @%pa, %d (addr,mask,val) triples\n",
		 __func__, &res.start, (n - 1) / 3);

	for (i = 1; i + 2 < n; i += 3) {
		u32 cur = readl(base + args[i]);
		u32 mask = args[i + 1];
		u32 val = args[i + 2];
		u32 new_val = (cur & ~mask) | (val & mask);

		writel(new_val, base + args[i]);
		dev_info(&pdev->dev, "  audio_sys+0x%02x: (0x%08x & ~0x%08x) | (0x%08x & 0x%08x) = 0x%08x\n",
			 args[i], cur, mask, val, mask, new_val);
	}

	return 0;
}

static int cv1835_dummy_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params)
{
	return 0;
}

static struct snd_soc_ops cv1835_dummy_ops = {
	.hw_params = cv1835_dummy_hw_params,
};

static const struct of_device_id cvi_dummy_match_ids[] = {
	{
		.compatible = "cvitek,cv1835-dummy-card",
	},
};
MODULE_DEVICE_TABLE(of, cvi_dummy_match_ids);



static int cv1835_dummy_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node, *dai_np;
	struct snd_soc_card *card;
	struct snd_soc_dai_link *links;
	struct snd_soc_dai_link_component *cpus, *codecs, *platforms;
	const char *mode = NULL;
	const char *fmt = NULL;
	const char *role = NULL;
	u32 slot_no = 2;
	int num_links, idx = 0;

	dev_info(&pdev->dev, "%s dev name=%s\n",  __func__, dev_name(&pdev->dev));

	if (!np)
		return 0;

	num_links = of_get_child_count(np);
	if (num_links == 0 || num_links > CV1835_DUMMY_MAX_LINKS) {
		dev_err(&pdev->dev, "%s: invalid dai link count %d (1..%d)\n",
			 __func__, num_links, CV1835_DUMMY_MAX_LINKS);
		return -EINVAL;
	}

	card = devm_kzalloc(&pdev->dev, sizeof(*card), GFP_KERNEL);
	links = devm_kzalloc(&pdev->dev, sizeof(*links) * num_links, GFP_KERNEL);
	cpus = devm_kzalloc(&pdev->dev, sizeof(*cpus) * num_links, GFP_KERNEL);
	codecs = devm_kzalloc(&pdev->dev, sizeof(*codecs) * num_links, GFP_KERNEL);
	platforms = devm_kzalloc(&pdev->dev, sizeof(*platforms) * num_links, GFP_KERNEL);
	if (!card || !links || !cpus || !codecs || !platforms)
		return -ENOMEM;

	card->owner = THIS_MODULE;
	card->dev = &pdev->dev;
	card->dai_link = links;
	card->num_links = num_links;

	/* Configure audio_sys loopback mux (no-op if property absent) */
	cv1835_dummy_config_loopback_mux(pdev, np);

	of_property_read_string(np, "cvi,card_name", &card->name);
	of_property_read_u32(np, "cvi,slot_no", &slot_no);
	of_property_read_string(np, "cvi,mode", &mode);
	of_property_read_string(np, "cvi,fmt", &fmt);

	for_each_child_of_node(np, dai_np) {
		struct snd_soc_dai_link *link = &links[idx];

		link->cpus = &cpus[idx];
		link->num_cpus = 1;
		link->codecs = &codecs[idx];
		link->num_codecs = 1;
		link->platforms = &platforms[idx];
		link->num_platforms = 1;
		link->ops = &cv1835_dummy_ops;

		of_property_read_string(dai_np, "cvi,dai_name", &link->name);
		of_property_read_string(dai_np, "cvi,stream_name", &link->stream_name);
		/* CPU/platform 用 device name (如 "29110000.i2s") 匹配 platform device;
		 * dai_name 不填，让 ASoC 通过 device 的单 DAI fallback 自动匹配 cvi_i2s_probe */
		of_property_read_string(dai_np, "cvi,cpu_dai_name", &cpus[idx].name);
		of_property_read_string(dai_np, "cvi,platform_name", &platforms[idx].name);
		/* dummy_codec 是 platform device, dai name="dummy_codec-aif" 必须严格匹配 */
		of_property_read_string(dai_np, "cvi,codec_name", &codecs[idx].name);
		of_property_read_string(dai_np, "cvi,codec_dai_name", &codecs[idx].dai_name);

		/* 5.10 snd_soc_dai_link 有 playback_only/capture_only 字段，控制 PCM 单向构造 */
		if (of_property_read_bool(dai_np, "cvi,playback_only"))
			link->playback_only = 1;
		if (of_property_read_bool(dai_np, "cvi,capture_only"))
			link->capture_only = 1;

		of_property_read_string(dai_np, "cvi,role", &role);

		if (mode) {
			if (!strcmp(mode, "I2S"))
				link->dai_fmt = SND_SOC_DAIFMT_I2S;
			else if (!strcmp(mode, "LEFT_J"))
				link->dai_fmt = SND_SOC_DAIFMT_LEFT_J;
			else if (!strcmp(mode, "RIGHT_J"))
				link->dai_fmt = SND_SOC_DAIFMT_RIGHT_J;
			else if (!strcmp(mode, "DSP_A"))
				link->dai_fmt = SND_SOC_DAIFMT_DSP_A;
			else if (!strcmp(mode, "DSP_B"))
				link->dai_fmt = SND_SOC_DAIFMT_DSP_B;
			else if (!strcmp(mode, "PDM"))
				link->dai_fmt = SND_SOC_DAIFMT_PDM;
			else
				dev_err(&pdev->dev, "%s, not support this mode\n", __func__);

			if ((!strcmp(mode, "I2S")) || (!strcmp(mode, "LEFT_J")) || (!strcmp(mode, "RIGHT_J"))) {
				if (fmt) {
					if (!strcmp(fmt, "IBNF"))
						link->dai_fmt |= SND_SOC_DAIFMT_IB_NF;
					else if (!strcmp(fmt, "IBIF"))
						link->dai_fmt |= SND_SOC_DAIFMT_IB_IF;
					else if (!strcmp(fmt, "NBNF"))
						link->dai_fmt |= SND_SOC_DAIFMT_NB_NF;
					else if (!strcmp(fmt, "NBIF"))
						link->dai_fmt |= SND_SOC_DAIFMT_NB_IF;
					else
						dev_err(&pdev->dev, "%s, not support this sample format\n", __func__);
				}
			} else {
				link->dai_fmt |= SND_SOC_DAIFMT_NB_IF;
			}
		}

		if (role) {
			if (!strcmp(role, "master"))
				link->dai_fmt |= SND_SOC_DAIFMT_CBS_CFS;
			else if (!strcmp(role, "slave"))
				link->dai_fmt |= SND_SOC_DAIFMT_CBM_CFM;
			else
				dev_err(&pdev->dev, "%s, not support this role\n", __func__);
		}

		if (mode && !strcmp(mode, "PDM"))
			tdm_slot_no2 = slot_no;
		else if (slot_no != 2)
			dev_err(&pdev->dev, "Wrong solt number setting in %s mode\n", mode);

		dev_info(&pdev->dev, "%s, link[%d] %s fmt=0x%08x cpu=%s codec=%s/%s platform=%s\n",
			 __func__, idx, link->name, link->dai_fmt,
			 cpus[idx].name, codecs[idx].name, codecs[idx].dai_name,
			 platforms[idx].name);
		idx++;
	}

	platform_set_drvdata(pdev, card);
	return devm_snd_soc_register_card(&pdev->dev, card);
}

static struct platform_driver cv1835_dummy_driver = {
	.driver = {
		.name = "cv1835-dummy",
		.pm = &snd_soc_pm_ops,
		.of_match_table = cvi_dummy_match_ids,
	},
	.probe = cv1835_dummy_probe,
};

module_platform_driver(cv1835_dummy_driver);

MODULE_AUTHOR("RuiLong.Chen <ruilong.chen@cvitek.com>");
MODULE_DESCRIPTION("ALSA SoC cv1835 dummy driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:cv1835-dummy");


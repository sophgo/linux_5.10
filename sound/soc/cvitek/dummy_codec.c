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
 * A2 (CV186X) dummy codec.
 *
 * Built only on CV186X (obj-$(CONFIG_ARCH_CV186X)). Registers 1 codec
 * instance with DAI name "dummy_codec-aif" (no suffix), matching the A2
 * DTS sound node's codec_name reference. A2 normally uses simple-audio-card
 * for its codec path; this dummy codec is only compiled when the A2 defconfig
 * enables DUMMY_CARD for DW IIS self-loopback testing.
 *
 * Consumed by the shared dummy card (shared_dummy_codec.c) via DTS
 * compatible "cvitek,cv1835-dummy-card" + cvi,codec_name="dummy_codec".
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>

#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>

static bool initFlag;
struct work_struct work;
struct workqueue_struct *initWork;

static void initWorkFuc(struct work_struct *work);

static const struct snd_soc_component_driver dummy_component_driver = {

};


static int dummy_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	return 0;
}

static const struct snd_soc_dai_ops dummy_dai_ops = {
	.hw_params = dummy_hw_params,
};

#define DUMMY_FORMATS (SNDRV_PCM_FMTBIT_S16_LE \
						|	SNDRV_PCM_FMTBIT_S24_LE \
						|	SNDRV_PCM_FMTBIT_S32_LE)

#define DUMMY_CODEC_MAX_INSTANCES 2

static struct snd_soc_dai_driver dummy_dai_drivers[DUMMY_CODEC_MAX_INSTANCES] = {
	[0] = {
		.name = "dummy_codec-aif.0",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = DUMMY_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = DUMMY_FORMATS,
		},
		.ops = &dummy_dai_ops,
		.symmetric_rates = 1,
	},
	[1] = {
		.name = "dummy_codec-aif.1",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = DUMMY_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = DUMMY_FORMATS,
		},
		.ops = &dummy_dai_ops,
		.symmetric_rates = 1,
	},
};

static int dummy_codec_probe(struct platform_device *pdev)
{
	int id = pdev->id;

	/* of_match (DTS "cvitek,dummy-codec" node): pdev->id == -1, use instance 0.
	 * simple-audio-card's codec=<&dummy_codec> phandle resolves via this
	 * component's of_node so the card can bind. Static platform_device
	 * (name="dummy_codec", id=0/1) still works for cv1835-dummy-card machine
	 * driver via codec_name name matching. */
	if (id < 0)
		id = 0;
	if (id >= DUMMY_CODEC_MAX_INSTANCES)
		return -ENODEV;

	return devm_snd_soc_register_component(&pdev->dev, &dummy_component_driver,
					       &dummy_dai_drivers[id], 1);
}


static int dummy_codec_remove(struct platform_device *dev)
{
	return 0;
}

static void dummy_codec_release(struct device *dev)
{
}



static const struct of_device_id dummy_codec_of_match[] = {
	{ .compatible = "cvitek,dummy-codec", },
	{},
};
MODULE_DEVICE_TABLE(of, dummy_codec_of_match);

static struct platform_driver cvitek_dummy_codec_driver = {
	.driver = {
		.name = "dummy_codec",
		.of_match_table = dummy_codec_of_match,
	},
	.probe = dummy_codec_probe,
	.remove = dummy_codec_remove,
};

static struct platform_device cvitek_dummy_codec_devs[DUMMY_CODEC_MAX_INSTANCES] = {
	[0] = {
		.name = "dummy_codec",
		.id   = 0,
		.dev  = { .release = dummy_codec_release, },
	},
	[1] = {
		.name = "dummy_codec",
		.id   = 1,
		.dev  = { .release = dummy_codec_release, },
	},
};

static void initWorkFuc(struct work_struct *work)
{
	int i;

	pr_info("dummy_codec %s\n", __func__);
	if (!initFlag) {
		for (i = 0; i < DUMMY_CODEC_MAX_INSTANCES; i++)
			platform_device_register(&cvitek_dummy_codec_devs[i]);
		platform_driver_register(&cvitek_dummy_codec_driver);
		initFlag = true;
	}
}

// register dummy coudec after internal codec register.
static int dummy_codec_init(void)
{
	initFlag = false;
	initWork = create_workqueue("initWork");
	INIT_WORK(&work, initWorkFuc);
	queue_work(initWork, &work);
	return 0;
}

static void dummy_codec_exit(void)
{
	int i;

	destroy_workqueue(initWork);
	if (initFlag) {
		for (i = 0; i < DUMMY_CODEC_MAX_INSTANCES; i++)
			platform_device_unregister(&cvitek_dummy_codec_devs[i]);
		platform_driver_unregister(&cvitek_dummy_codec_driver);
	}
	initFlag = false;
}


module_init(dummy_codec_init);
module_exit(dummy_codec_exit);

MODULE_DESCRIPTION("ASoC Dummy CODEC driver");
MODULE_AUTHOR("RuiLong.Chen <ruilong.chen@cvitek.com>");
MODULE_LICENSE("GPL v2");


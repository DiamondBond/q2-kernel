/*
 * ALSA SoC ES7148 DAC driver
 *
 * Author:      Everest Semiconductor
 * Copyright:   (C) 2018 Everest Semiconductor Co Ltd.
 *
 * Based on sound/soc/codecs/es7243l.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "es7148.h"
static int es7148_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct es7148_priv *es7148 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s: freq = %d\n", __func__, freq);
	es7148->sysclk = freq;
	return 0;
}

static int es7148_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;

	dev_dbg(component->dev, "%s: format = 0x%x\n", __func__, fmt);

	/* ES7148 only supports I2S format in slave mode */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFM:	/* Slave mode */
		break;
	default:
		dev_err(component->dev, "Invalid master/slave configuration\n");
		return -EINVAL;
	}

	/* Interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* ES7148 supports I2S format */
		break;
	default:
		dev_err(component->dev, "Unsupported interface format\n");
		return -EINVAL;
	}

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:	/* Normal */
		break;
	case SND_SOC_DAIFMT_IB_IF:	/* Inverted */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int es7148_hw_params(struct snd_pcm_substream *substream,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;

	dev_dbg(component->dev, "%s: rate = %d, width = %d\n", __func__,
		params_rate(params), params_width(params));

	return 0;
}

static int es7148_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;

	dev_dbg(component->dev, "%s: mute = %d\n", __func__, mute);
	
	/* ES7148 doesn't have software mute control */
	/* Muting is typically handled at the stream level */
	return 0;
}

static int es7148_startup(struct snd_pcm_substream *substream,
			 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es7148_priv *es7148 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s\n", __func__);
	es7148->enabled = true;
	return 0;
}

static void es7148_shutdown(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es7148_priv *es7148 = snd_soc_component_get_drvdata(component);

	dev_dbg(component->dev, "%s\n", __func__);
	es7148->enabled = false;
}

static const struct snd_soc_dapm_widget es7148_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("AOUTL"),
	SND_SOC_DAPM_OUTPUT("AOUTR"),
};

static const struct snd_soc_dapm_route es7148_dapm_routes[] = {
	{"AOUTL", NULL, "DAC"},
	{"AOUTR", NULL, "DAC"},
};

static const struct snd_soc_dai_ops es7148_dai_ops = {
	.startup = es7148_startup,
	.shutdown = es7148_shutdown,
	.set_sysclk = es7148_set_dai_sysclk,
	.set_fmt = es7148_set_dai_fmt,
	.hw_params = es7148_hw_params,
	.mute_stream = es7148_mute,
};

static struct snd_soc_dai_driver es7148_dai = {
	.name = "es7148-hifi",
	.id       = 0,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = ES7148_FORMATS,
	},
	.ops = &es7148_dai_ops,
	.symmetric_rates = 1,
};

static int es7148_probe(struct snd_soc_component *component)
{
	struct es7148_priv *es7148 = snd_soc_component_get_drvdata(component);

	dev_info(component->dev, "ES7148 DAC probed successfully\n");
	es7148->component = component;
	es7148->enabled = false;

	/* ES7148 requires no specific initialization */
	/* It automatically starts working when clocks are provided */
	return 0;
}

static void es7148_remove(struct snd_soc_component *component)
{
	dev_info(component->dev, "ES7148 DAC removed\n");
}

static const struct snd_soc_component_driver soc_component_dev_es7148 = {
	.probe = es7148_probe,
	.remove = es7148_remove,
	.dapm_widgets = es7148_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(es7148_dapm_widgets),
	.dapm_routes = es7148_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(es7148_dapm_routes),
	.idle_bias_on = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
	.non_legacy_dai_naming = 1,
};

static int es7148_platform_probe(struct platform_device *pdev)
{
	struct es7148_priv *es7148;
	int ret;

	es7148 = devm_kzalloc(&pdev->dev, sizeof(*es7148), GFP_KERNEL);
	if (!es7148)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, es7148);

	ret = devm_snd_soc_register_component(&pdev->dev,
					     &soc_component_dev_es7148,
					     &es7148_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register component: %d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "ES7148 platform driver probed successfully\n");
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id es7148_of_match[] = {
	{ .compatible = "Everest-semi,es7148", },
	{ }
};
MODULE_DEVICE_TABLE(of, es7148_of_match);
#endif

static struct platform_driver es7148_codec_driver = {
	.driver = {
		.name = "es7148-codec",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(es7148_of_match),
#endif
	},
	.probe = es7148_platform_probe,
};

module_platform_driver(es7148_codec_driver);

MODULE_DESCRIPTION("ASoC ES7148 DAC driver");
MODULE_AUTHOR("Everest Semiconductor");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:es7148-codec");


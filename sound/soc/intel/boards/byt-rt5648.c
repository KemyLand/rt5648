/*
 * Intel Baytrail SST RT5648 machine driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define DEBUG 1
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../../codecs/rt5648.h"

#include "../common/sst-dsp.h"

static const struct snd_soc_dapm_widget byt_rt5648_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Internal Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route byt_rt5648_audio_map[] = {
	{"Headset Mic", NULL, "MICBIAS1"},
	{"IN2P", NULL, "Headset Mic"},
	{"Headphone", NULL, "HPOL"},
	{"Headphone", NULL, "HPOR"},
	{"Speaker", NULL, "SPOLP"},
	{"Speaker", NULL, "SPOLN"},
	{"Speaker", NULL, "SPORP"},
	{"Speaker", NULL, "SPORN"},
};

static const struct snd_soc_dapm_route byt_rt5648_intmic_dmic1_map[] = {
	{"DMIC1", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5648_intmic_dmic2_map[] = {
	{"DMIC2", NULL, "Internal Mic"},
};

static const struct snd_soc_dapm_route byt_rt5648_intmic_in1_map[] = {
	{"Internal Mic", NULL, "MICBIAS1"},
	{"IN1P", NULL, "Internal Mic"},
};

enum {
	BYT_RT5648_DMIC1_MAP,
	BYT_RT5648_DMIC2_MAP,
	BYT_RT5648_IN1_MAP,
};

#define BYT_RT5648_MAP(quirk)	((quirk) & 0xff)
#define BYT_RT5648_DMIC_EN	BIT(16)

static unsigned long byt_rt5648_quirk = BYT_RT5648_DMIC1_MAP |
					BYT_RT5648_DMIC_EN;

static const struct snd_kcontrol_new byt_rt5648_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Internal Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static int byt_rt5648_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, RT5648_SCLK_S_PLL1,
				     params_rate(params) * 256,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set codec clock %d\n", ret);
		return ret;
	}
	ret = snd_soc_dai_set_pll(codec_dai, 0, RT5648_PLL1_S_BCLK1,
				  params_rate(params) * 64,
				  params_rate(params) * 256);
	if (ret < 0) {
		dev_err(codec_dai->dev, "can't set codec pll: %d\n", ret);
		return ret;
	}
	return 0;
}

static int byt_rt5648_quirk_cb(const struct dmi_system_id *id)
{
	byt_rt5648_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id byt_rt5648_quirk_table[] = {
	{
		.callback = byt_rt5648_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "T100TA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X205TA"),
		},
		.driver_data = (unsigned long *)BYT_RT5648_IN1_MAP,
	},
	{
		.callback = byt_rt5648_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "DellInc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Venue 8 Pro 5830"),
		},
		.driver_data = (unsigned long *)(BYT_RT5648_DMIC2_MAP |
						 BYT_RT5648_DMIC_EN),
	},
	{}
};

static int byt_rt5648_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_codec *codec = runtime->codec;
	struct snd_soc_card *card = runtime->card;
	const struct snd_soc_dapm_route *custom_map;
	int num_routes;

	card->dapm.idle_bias_off = true;

	ret = snd_soc_add_card_controls(card, byt_rt5648_controls,
					ARRAY_SIZE(byt_rt5648_controls));
	if (ret) {
		dev_err(card->dev, "unable to add card controls\n");
		return ret;
	}

	dmi_check_system(byt_rt5648_quirk_table);
	switch (BYT_RT5648_MAP(byt_rt5648_quirk)) {
	case BYT_RT5648_IN1_MAP:
		custom_map = byt_rt5648_intmic_in1_map;
		num_routes = ARRAY_SIZE(byt_rt5648_intmic_in1_map);
		break;
	case BYT_RT5648_DMIC2_MAP:
		custom_map = byt_rt5648_intmic_dmic2_map;
		num_routes = ARRAY_SIZE(byt_rt5648_intmic_dmic2_map);
		break;
	default:
		custom_map = byt_rt5648_intmic_dmic1_map;
		num_routes = ARRAY_SIZE(byt_rt5648_intmic_dmic1_map);
	}

	ret = snd_soc_dapm_add_routes(&card->dapm, custom_map, num_routes);
	if (ret)
		return ret;

    /*TODO: Fix whatever crap this was
	if (byt_rt5648_quirk & BYT_RT5648_DMIC_EN) {
		ret = rt5648_dmic_enable(codec, 0, 0);
		if (ret)
			return ret;
	}*/

	snd_soc_dapm_ignore_suspend(&card->dapm, "Headphone");
	snd_soc_dapm_ignore_suspend(&card->dapm, "Speaker");

	return ret;
}

static struct snd_soc_ops byt_rt5648_ops = {
	.hw_params = byt_rt5648_hw_params,
};

static struct snd_soc_dai_link byt_rt5648_dais[] = {
	{
		.name = "Baytrail Audio",
		.stream_name = "Audio",
		.cpu_dai_name = "baytrail-pcm-audio",
		.codec_dai_name = "rt5648-aif1",
		.codec_name = "i2c-10EC5648:00",
		.platform_name = "baytrail-pcm-audio",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.init = byt_rt5648_init,
		.ops = &byt_rt5648_ops,
	},
};

static struct snd_soc_card byt_rt5648_card = {
	.name = "8086F028:00", // was "byt-rt5648"
	.owner = THIS_MODULE,
	.dai_link = byt_rt5648_dais,
	.num_links = ARRAY_SIZE(byt_rt5648_dais),
	.dapm_widgets = byt_rt5648_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_rt5648_widgets),
	.dapm_routes = byt_rt5648_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_rt5648_audio_map),
	.fully_routed = true,
};

static void dump_device(struct device *dev) {
	pr_debug(
		"dump_device: *dev = {\n"
		"\tstruct device_private *p = %p\n"
		"\tconst char *init_name = \"%s\"\n}\n",
		dev->p, dev->init_name);

	if(dev->parent != NULL) {
		pr_debug("dump_device(): Beginning recursive dump of dev->parent\n");
		dump_device(dev->parent);
	}
}

static int byt_rt5648_probe(struct platform_device *pdev)
{
	pr_debug("byt_rt5648_probe() called: pdev->name=%s\n", pdev->name);
	pr_debug(
		"byt_rt5648_probe(): *pdev = {\n"
		"\tconst char *name = \"%s\"\n"
		"\tint id = %d\n"
		"\tbool id_auto = %d\n"
		"\tu32 num_resources = %u\n}\n",
		pdev->name, pdev->id, pdev->id_auto, pdev->num_resources);
	pr_debug("byt_rt5648_probe(): Beginning recursive dump of pdev->dev\n");
	dump_device(&pdev->dev);
	struct snd_soc_card *card = &byt_rt5648_card;

	card->dev = &pdev->dev;
	pr_debug("byt_rt5648_probe(): calling devm_snd_soc_register_card(\"%s\", \"%s\")\n", pdev->dev.init_name, card->name);
	int ret = devm_snd_soc_register_card(&pdev->dev, card);
	pr_debug("byt_rt5648_probe() returned: %d\n", ret);
	return ret;
}


#ifdef CONFIG_ACPI
static const struct acpi_device_id byt_rt5648_acpi_id[] = {
	{ "80860F28" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, byt_rt5648_acpi_id);
#endif

static struct platform_driver byt_rt5648_audio = {
	.probe = byt_rt5648_probe,
	.driver = {
		.name = "80860F28:00", // was "byt-rt5648"
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(byt_rt5648_audio)

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail Machine driver");
MODULE_AUTHOR("Omair Md Abdullah, Jarkko Nikula");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:byt-rt5648");

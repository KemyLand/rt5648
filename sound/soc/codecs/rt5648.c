/*
 * rt5648.c  --  RT5648 ALSA SoC audio codec driver
 *
 * Copyright 2012 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define DEBUG 1
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <linux/mod_devicetable.h>
#include <linux/printk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/intel-mid.h>
#include <linux/gpio.h>

#include "rt_codec_ioctl.h"
#include "rt5648_ioctl.h"
#include "rt5648.h"


/* #define USE_INT_CLK */
#define JD1_FUNC
/* #define ALC_DRC_FUNC */

#define RT5648_REG_RW 1 /* for debug */

#define USE_ASRC

#define VERSION "0.0.3 alsa 1.0.25"

// ASUS_BSP : for ATD audio_codec_status
static int ret_codec_status = 0;
static ssize_t codec_show(struct device *dev, struct device_attribute *attr,
		char *buf);

static DEVICE_ATTR(audio_codec_status, S_IWUSR | S_IRUGO, codec_show, NULL);

static ssize_t codec_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	if(ret_codec_status == 1)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

// ASUS_BSP : for ATD headset_status
static int ret_headset_status = 0;
static ssize_t headset_show(struct device *dev, struct device_attribute *attr,
		char *buf);

static DEVICE_ATTR(headset_status, S_IWUSR | S_IRUGO, headset_show, NULL);

static int is_recording;

static ssize_t headset_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	if(ret_headset_status != 0)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");;
}

// ASUS_BSP : end
struct rt5648_init_reg {
	u8 reg;
	u16 val;
};

static struct rt5648_init_reg init_list[] = {
	//{ RT5648_DIG_MISC	, 0x0121 },
	{ RT5648_ADDA_CLK1	, 0x0000 },
	{ RT5648_PRIV_INDEX	, 0x003d },
	{ RT5648_PRIV_DATA	, 0x3600 },
	/* playback */
	{ RT5648_DAC_CTRL	, 0x0011 },
#ifdef CONFIG_TF103CG
	{ RT5648_STO_DAC_MIXER	, 0x2626 },/* Dig inf 1 -> Sto DAC mixer -> DACL */
#else
	{ RT5648_STO_DAC_MIXER	, 0x0606 },/* Dig inf 1 -> Sto DAC mixer -> DACL */
#endif
	{ RT5648_MONO_DAC_MIXER	, 0x4444 },
	{ RT5648_OUT_L1_MIXER	, 0x01fe },/* DACL1 -> OUTMIXL */
	{ RT5648_OUT_R1_MIXER	, 0x01fe },/* DACR1 -> OUTMIXR */
//	{ RT5648_LOUT_MIXER	, 0xc000 },
	{ RT5648_LOUT1		, 0x8888 },
#if 0 /* HP direct path */
	{ RT5648_HPO_MIXER	, 0x2000 },/* DAC1 -> HPOLMIX */
#else /* HP via mixer path */
	{ RT5648_HPOMIXL_CTRL	, 0x001e },/* DAC1 -> HPOVOL */
	{ RT5648_HPOMIXR_CTRL	, 0x001e },/* DAC1 -> HPOVOL */
	{ RT5648_HPO_MIXER	, 0x4000 },/* HPOVOL -> HPOLMIX */
#endif
	{ RT5648_HP_VOL		, 0x8888 },/* OUTMIX -> HPVOL */
#if 0 /* SPK direct path */
	{ RT5648_SPO_MIXER	, 0x7803 },/* DAC1 -> SPO */
#else /* SPK via mixer path */
	{ RT5648_SPK_L_MIXER	, 0x003a },/* DAC1/2 -> SPKVOL */
	{ RT5648_SPK_R_MIXER	, 0x003a },/* DAC1/2 -> SPKVOL */
#ifdef CONFIG_TF103CG
	{ RT5648_SPO_MIXER	, 0xc806 },/* SPKVOL -> SPO */ /* unmute SPKVOLR -> SPOLMIX for single speaker */
#else
	{ RT5648_SPO_MIXER	, 0xc806 },/* SPKVOL -> SPO */ /* unmute SPKVOLR -> SPOLMIX for single speaker */
#endif
#endif
	{ RT5648_SPK_VOL	, 0x8888 },
	/* record */
#ifdef CONFIG_TF103CG
	{ RT5648_IN1_IN2	, 0x0240 },/* IN2 boost 30db and differential mode */
#else
	{ RT5648_IN1_IN2	, 0x0340 },/* IN2 boost 30db and differential mode */
#endif
	{ RT5648_REC_L2_MIXER	, 0x007d },/* Mic1 -> RECMIXL */
	{ RT5648_REC_R2_MIXER	, 0x007d },/* Mic1 -> RECMIXR */
#if 0 /* DMIC1 */
	{ RT5648_STO1_ADC_MIXER	, 0x5840 },
	{ RT5648_MONO_ADC_MIXER	, 0x5858 },
#endif
#if 0 /* DMIC2 */
	{ RT5648_STO1_ADC_MIXER	, 0x5940 },
	{ RT5648_MONO_ADC_MIXER	, 0x5858 },
#endif
#if 1 /* AMIC */
	{ RT5648_STO1_ADC_MIXER	, 0x3020 },/* ADC -> Sto ADC mixer */
	{ RT5648_MONO_ADC_MIXER	, 0x3838 },
#endif
	{ RT5648_DMIC_CTRL1	, 0x1c05 }, /* fix IN2 setting */
	/* { RT5648_STO1_ADC_DIG_VOL, 0xafaf }, */ /* Mute STO1 ADC for depop, Digital Input Gain */
	{ RT5648_STO1_ADC_DIG_VOL, 0xd7d7 },/* Mute STO1 ADC for depop, Digital Input Gain */
	{ RT5648_GPIO_CTRL1	, 0xc080 },
	{ RT5648_GPIO_CTRL2	, 0x0004 },
#ifdef JD1_FUNC
	{ RT5648_IRQ_CTRL2	, 0x0200 },
	{ RT5648_JD_CTRL3	, 0x00c8 },
	{ RT5648_GEN_CTRL3	, 0x0100 }, /* set [10:9] to 01b for MCLK protection, realtek recommand */
	{ RT5648_MICBIAS	, 0x0008 },
	{ RT5648_GEN_CTRL2	, 0x4050 },
#ifdef CONFIG_TF103CG
	{ RT5648_CJ_CTRL1	, 0x2021 },	/* Combo Jack Disable */
#else
	{ RT5648_CJ_CTRL1	, 0x0021 },	/* Combo Jack Disable */
#endif
	{ RT5648_CJ_CTRL2	, 0x08a7 },	/* Combo Jack Disable */
	{ RT5648_CJ_CTRL3	, 0x4000 },	/* Combo Jack Disable */
#endif
#ifdef CONFIG_TF103CG
    { RT5648_SPO_CLSD_RATIO	, 0x0004 },
#else
    { RT5648_SPO_CLSD_RATIO	, 0x0002 },
#endif
	{ RT5648_ASRC_3		, 0x0022 },
	{ RT5648_ASRC_8		, 0x0100 },
/* for TFCG-119 headset noise*/
	{RT5648_CHARGE_PUMP , 0x0e06 },
/* for TFCG-119 headset noise*/
};
#define RT5648_INIT_REG_LEN ARRAY_SIZE(init_list)

#ifdef ALC_DRC_FUNC
static struct rt5648_init_reg alc_drc_list[] = {
	{ RT5648_ALC_DRC_CTRL1	, 0x0000 },
	{ RT5648_ALC_DRC_CTRL2	, 0x0000 },
	{ RT5648_ALC_CTRL_2	, 0x0000 },
	{ RT5648_ALC_CTRL_3	, 0x0000 },
	{ RT5648_ALC_CTRL_4	, 0x0000 },
	{ RT5648_ALC_CTRL_1	, 0x0000 },
};
#define RT5648_ALC_DRC_REG_LEN ARRAY_SIZE(alc_drc_list)
#endif

static int rt5648_reg_init(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5648_INIT_REG_LEN; i++)
		snd_soc_write(codec, init_list[i].reg, init_list[i].val);
#ifdef ALC_DRC_FUNC
	for (i = 0; i < RT5648_ALC_DRC_REG_LEN; i++)
		snd_soc_write(codec, alc_drc_list[i].reg, alc_drc_list[i].val);
#endif

	return 0;
}

static int rt5648_index_sync(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5648_INIT_REG_LEN; i++)
		if (RT5648_PRIV_INDEX == init_list[i].reg ||
			RT5648_PRIV_DATA == init_list[i].reg)
			snd_soc_write(codec, init_list[i].reg,
					init_list[i].val);
	return 0;
}

static const u16 rt5648_reg[RT5648_VENDOR_ID2 + 1] = {
	[RT5648_HP_VOL] = 0xc8c8,
	[RT5648_SPK_VOL] = 0xc8c8,
	[RT5648_LOUT1] = 0xc8c8,
	[RT5648_CJ_CTRL1] = 0x0002,
	[RT5648_CJ_CTRL2] = 0x0827,
	[RT5648_CJ_CTRL3] = 0xe000,
	[RT5648_INL1_INR1_VOL] = 0x0808,
	[RT5648_SPK_FUNC_LIM] = 0x3333,
	[RT5648_ADJ_HPF_CTRL] = 0x4b00,
	[RT5648_SIDETONE_CTRL] = 0x018b,
	[RT5648_DAC1_DIG_VOL] = 0xafaf,
	[RT5648_DAC2_DIG_VOL] = 0xafaf,
	[RT5648_DAC_CTRL] = 0x0001,
	[RT5648_STO1_ADC_DIG_VOL] = 0x2f2f,
	[RT5648_MONO_ADC_DIG_VOL] = 0x2f2f,
	[RT5648_STO1_ADC_MIXER] = 0x7060,
	[RT5648_MONO_ADC_MIXER] = 0x7070,
	[RT5648_AD_DA_MIXER] = 0x8080,
	[RT5648_STO_DAC_MIXER] = 0x5656,
	[RT5648_MONO_DAC_MIXER] = 0x5454,
	[RT5648_DIG_MIXER] = 0xaaa0,
	[RT5648_DIG_INF1_DATA] = 0x1002,
	[RT5648_PDM_OUT_CTRL] = 0x5000,
	[RT5648_REC_L2_MIXER] = 0x007f,
	[RT5648_REC_R2_MIXER] = 0x007f,
	[RT5648_HPOMIXL_CTRL] = 0x001f,
	[RT5648_HPOMIXR_CTRL] = 0x001f,
	[RT5648_HPO_MIXER] = 0x6000,
	[RT5648_SPK_L_MIXER] = 0x003e,
	[RT5648_SPK_R_MIXER] = 0x003e,
	[RT5648_SPO_MIXER] = 0xf807,
	[RT5648_SPO_CLSD_RATIO] = 0x0004,
	[RT5648_OUT_L1_MIXER] = 0x01ff,
 	[RT5648_OUT_R1_MIXER] = 0x01ff,
	[RT5648_LOUT_MIXER] = 0xf000,
	[RT5648_HAPTIC_CTRL1] = 0x0111,
	[RT5648_HAPTIC_CTRL2] = 0x0064,
	[RT5648_HAPTIC_CTRL3] = 0xef0e,
	[RT5648_HAPTIC_CTRL4] = 0xf0f0,
	[RT5648_HAPTIC_CTRL5] = 0xef0e,
	[RT5648_HAPTIC_CTRL6] = 0xf0f0,
	[RT5648_HAPTIC_CTRL7] = 0xef0e,
	[RT5648_HAPTIC_CTRL8] = 0xf0f0,
	[RT5648_HAPTIC_CTRL9] = 0xf000,
	[RT5648_PWR_DIG1] = 0x0300,
	[RT5648_PWR_ANLG1] = 0x00c2,
	[RT5648_I2S1_SDP] = 0x8000,
	[RT5648_I2S2_SDP] = 0x8000,
	[RT5648_I2S3_SDP] = 0x8000,
	[RT5648_ADDA_CLK1] = 0x1110,
	[RT5648_ADDA_CLK2] = 0x3e00,
	[RT5648_DMIC_CTRL1] = 0x2409,
	[RT5648_DMIC_CTRL2] = 0x000a,
	[RT5648_TDM_CTRL_3] = 0x0123,
	[RT5648_ASRC_3] = 0x0000,
	[RT5648_DEPOP_M1] = 0x0004,
	[RT5648_DEPOP_M2] = 0x1100,
	[RT5648_DEPOP_M3] = 0x0646,
	[RT5648_CHARGE_PUMP] = 0x0c06,
	[RT5648_MICBIAS] = 0x3000,
	[RT5648_A_JD_CTRL1] = 0x0200,
	[RT5648_VAD_CTRL1] = 0x2184,
	[RT5648_VAD_CTRL2] = 0x010a,
	[RT5648_VAD_CTRL3] = 0x0aea,
	[RT5648_VAD_CTRL4] = 0x000c,
	[RT5648_VAD_CTRL5] = 0x0400,
	[RT5648_CLSD_OUT_CTRL] = 0xa0a8,
	[RT5648_CLSD_OUT_CTRL1] = 0x0059,
	[RT5648_CLSD_OUT_CTRL2] = 0x0001,
	[RT5648_ADC_EQ_CTRL1] = 0x6000,
	[RT5648_EQ_CTRL1] = 0x6000,
	[RT5648_ALC_DRC_CTRL2] = 0x001f,
	[RT5648_ALC_CTRL_1] = 0x020c,
	[RT5648_ALC_CTRL_2] = 0x1f00,
	[RT5648_ALC_CTRL_4] = 0x4000,
	[RT5648_INT_IRQ_ST] = 0x0180,
	[RT5648_GPIO_CTRL4] = 0x2000,
	[RT5648_BASE_BACK] = 0x1813,
	[RT5648_MP3_PLUS1] = 0x0690,
	[RT5648_MP3_PLUS2] = 0x1c17,
	[RT5648_ADJ_HPF1] = 0xb320,
	[RT5648_HP_CALIB_AMP_DET] = 0x0400,
	[RT5648_SV_ZCD1] = 0x0809,
	[RT5648_IL_CMD] = 0x0003,
	[RT5648_IL_CMD2] = 0x0049,
	[RT5648_IL_CMD3] = 0x001b,
	[RT5648_DRC1_HL_CTRL1] = 0x8000,
	[RT5648_DRC1_HL_CTRL2] = 0x0200,
	[RT5648_DRC2_HL_CTRL1] = 0x8000,
	[RT5648_DRC2_HL_CTRL2] = 0x0200,
	[RT5648_MUTI_DRC_CTRL1] = 0x0f20,
	[RT5648_ADC_MONO_HP_CTRL1] = 0xb300,
	[RT5648_DRC2_CTRL1] = 0x001f,
	[RT5648_DRC2_CTRL2] = 0x020c,
	[RT5648_DRC2_CTRL3] = 0x1f00,
	[RT5648_DRC2_CTRL5] = 0x4000,
	[RT5648_DIG_MISC] = 0x2060,
};

static int rt5648_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, RT5648_RESET, 0);
}

/**
 * rt5648_index_write - Write private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 * @value: Private register Data.
 *
 * Modify private register for advanced setting. It can be written through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns 0 for success or negative error code.
 */
static int rt5648_index_write(struct snd_soc_codec *codec,
		unsigned int reg, unsigned int value)
{
	int ret;

	ret = snd_soc_write(codec, RT5648_PRIV_INDEX, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		goto err;
	}
	ret = snd_soc_write(codec, RT5648_PRIV_DATA, value);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private value: %d\n", ret);
		goto err;
	}
	return 0;

err:
	return ret;
}

/**
 * rt5648_index_read - Read private register.
 * @codec: SoC audio codec device.
 * @reg: Private register index.
 *
 * Read advanced setting from private register. It can be read through
 * private index (0x6a) and data (0x6c) register.
 *
 * Returns private register value or negative error code.
 */
static unsigned int rt5648_index_read(
	struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;

	ret = snd_soc_write(codec, RT5648_PRIV_INDEX, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		return ret;
	}
	return snd_soc_read(codec, RT5648_PRIV_DATA);
}

/**
 * rt5648_index_update_bits - update private register bits
 * @codec: audio codec
 * @reg: Private register index.
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value.
 *
 * Returns 1 for change, 0 for no change, or negative error code.
 */
static int rt5648_index_update_bits(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	unsigned int old, new;
	int change, ret;

	ret = rt5648_index_read(codec, reg);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to read private reg: %d\n", ret);
		goto err;
	}

	old = ret;
	new = (old & ~mask) | (value & mask);
	change = old != new;
	if (change) {
		ret = rt5648_index_write(codec, reg, new);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to write private reg: %d\n", ret);
			goto err;
		}
	}
	return change;

err:
	return ret;
}

void dc_calibrate(struct snd_soc_codec *codec)
{
	unsigned int sclk_src;

	sclk_src = snd_soc_read(codec, RT5648_GLB_CLK) &
		RT5648_SCLK_SRC_MASK;

	snd_soc_update_bits(codec, RT5648_PWR_ANLG2,
		RT5648_PWR_MB1, RT5648_PWR_MB1);
	snd_soc_update_bits(codec, RT5648_DEPOP_M2,
                RT5648_DEPOP_MASK, RT5648_DEPOP_MAN);
        snd_soc_update_bits(codec, RT5648_DEPOP_M1,
                RT5648_HP_CP_MASK | RT5648_HP_SG_MASK | RT5648_HP_CB_MASK,
                RT5648_HP_CP_PU | RT5648_HP_SG_DIS | RT5648_HP_CB_PU);

	snd_soc_update_bits(codec, RT5648_GLB_CLK,
		RT5648_SCLK_SRC_MASK, 0x2 << RT5648_SCLK_SRC_SFT);
        rt5648_index_write(codec, RT5648_HP_DCC_INT1, 0x9f01);
	snd_soc_update_bits(codec, RT5648_PWR_ANLG2,
		RT5648_PWR_MB1, 0);
	snd_soc_update_bits(codec, RT5648_GLB_CLK,
		RT5648_SCLK_SRC_MASK, sclk_src);
}

/**
 * rt5648_headset_detect - Detect headset.
 * @codec: SoC audio codec device.
 * @jack_insert: Jack insert or not.
 *
 * Detect whether is headset or not when jack inserted.
 *
 * Returns detect status.
 */

int rt5648_headset_detect(struct snd_soc_codec *codec, int jack_insert)
{
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	int reg63, reg64;

	if(jack_insert) {
		reg63 = snd_soc_read(codec, RT5648_PWR_ANLG1);
		reg64 = snd_soc_read(codec, RT5648_PWR_ANLG2);
		snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
			RT5648_PWR_MB | RT5648_PWR_BG | RT5648_LDO_SEL_MASK,
			RT5648_PWR_MB | RT5648_PWR_BG | 0x2);
		snd_soc_update_bits(codec, RT5648_PWR_ANLG2,
			RT5648_PWR_MB1, RT5648_PWR_MB1);
		snd_soc_update_bits(codec, RT5648_MICBIAS,
			RT5648_MIC1_OVCD_MASK, RT5648_MIC1_OVCD_EN);
		msleep(200);
		if (snd_soc_read(codec, RT5648_IRQ_CTRL3) & 0x300) {
			rt5648->jack_type = SND_JACK_HEADPHONE;
			snd_soc_write(codec, RT5648_PWR_ANLG1, reg63);
			snd_soc_write(codec, RT5648_PWR_ANLG2, reg64);
		} else {
			rt5648->jack_type = SND_JACK_HEADSET;
			snd_soc_update_bits(codec, RT5648_IRQ_CTRL3,
				RT5648_IRQ_MB1_OC_MASK, RT5648_IRQ_MB1_OC_NOR);
		}
	} else {
		snd_soc_update_bits(codec, RT5648_MICBIAS,
			RT5648_MIC1_OVCD_MASK, RT5648_MIC1_OVCD_DIS);
		snd_soc_update_bits(codec, RT5648_IRQ_CTRL3,
			RT5648_IRQ_MB1_OC_MASK, RT5648_IRQ_MB1_OC_BP);
		rt5648->jack_type = 0;
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			snd_soc_write(codec, RT5648_PWR_ANLG1, 0x0000);
			snd_soc_write(codec, RT5648_PWR_ANLG2, 0x0004);
		}
	}

	ret_headset_status = rt5648->jack_type;

	return rt5648->jack_type;
}
EXPORT_SYMBOL(rt5648_headset_detect);

int rt5648_check_interrupt_event(struct snd_soc_codec *codec)
{
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	int event = RT5648_UN_EVENT;
	int val;

	val = snd_soc_read(codec, RT5648_INT_IRQ_ST) & 0x1000;
	if (!rt5648->jd_status) {
		if (!val) {  /* Jack Insert */
			rt5648->jd_status = true;
			rt5648->bp_status = false;
			pr_debug("%s-RT5648_J_IN_EVENT\n", __func__);
			return RT5648_J_IN_EVENT;
		}
	} else { /* handle jack remove/button press events only when jack inserted */
		if (val) { /* Jack remove */
			rt5648->bp_status = false;
			rt5648->jd_status = false;
			pr_debug("%s-RT5648_J_OUT_EVENT\n", __func__);
			return RT5648_J_OUT_EVENT;
		}
		if (rt5648->jack_type == SND_JACK_HEADSET) {
			val = snd_soc_read(codec, RT5648_IRQ_CTRL3) & 0x300;
			if (rt5648->bp_status) {
				if (!val) {
					event = RT5648_BR_EVENT;
					rt5648->bp_status = false;
					pr_debug("%s-RT5648_BR_EVENT\n", __func__);
				}
			} else {
				if (val) {
					event = RT5648_BP_EVENT;
					rt5648->bp_status = true;
					pr_debug("%s-RT5648_BP_EVENT\n", __func__);
				}
			}
		}
	}
	pr_debug("%s-EVENT detected:%d\n", __func__, event);
	return event;
}
EXPORT_SYMBOL(rt5648_check_interrupt_event);

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -65625, 375, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -17625, 375, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static unsigned int bst_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0),
};

/* IN1/IN2 Input Type */
static const char *rt5648_input_mode[] = {
	"Single ended", "Differential"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_in1_mode_enum, RT5648_IN1_IN2,
	RT5648_IN_SFT1, rt5648_input_mode);

static const SOC_ENUM_SINGLE_DECL(
	rt5648_in2_mode_enum, RT5648_IN3,
	RT5648_IN_SFT2, rt5648_input_mode);

/* Interface data select */
static const char *rt5648_data_select[] = {
	"Normal", "Swap", "left copy to right", "right copy to left"
};

static const SOC_ENUM_SINGLE_DECL(rt5648_if2_dac_enum, RT5648_DIG_INF1_DATA,
				RT5648_IF2_DAC_SEL_SFT, rt5648_data_select);

static const SOC_ENUM_SINGLE_DECL(rt5648_if2_adc_enum, RT5648_DIG_INF1_DATA,
				RT5648_IF2_ADC_SEL_SFT, rt5648_data_select);

static const SOC_ENUM_SINGLE_DECL(rt5648_if3_dac_enum, RT5648_DIG_INF1_DATA,
				RT5648_IF3_DAC_SEL_SFT, rt5648_data_select);

static const SOC_ENUM_SINGLE_DECL(rt5648_if3_adc_enum, RT5648_DIG_INF1_DATA,
				RT5648_IF3_ADC_SEL_SFT, rt5648_data_select);

static const char *rt5648_tdm_data_swap_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static const SOC_ENUM_SINGLE_DECL(rt5648_tdm_adc_slot0_1_enum,
				RT5648_TDM_CTRL_1, 6,
				rt5648_tdm_data_swap_select);

static const SOC_ENUM_SINGLE_DECL(rt5648_tdm_adc_slot2_3_enum,
				RT5648_TDM_CTRL_1, 4,
				rt5648_tdm_data_swap_select);

static const SOC_ENUM_SINGLE_DECL(rt5648_tdm_adc_slot4_5_enum,
				RT5648_TDM_CTRL_1, 2,
				rt5648_tdm_data_swap_select);

static const SOC_ENUM_SINGLE_DECL(rt5648_tdm_adc_slot6_7_enum,
				RT5648_TDM_CTRL_1, 0,
				rt5648_tdm_data_swap_select);

/* SPO speaker gain ctrl */
static const char *rt5648_spo_gain_ratio[] = {"-6dB", "-4.5dB", "-3dB", "-1.5dB",
	"0dB", "0.83dB", "1.58dB", "2.22dB"};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_spo_gain_ratio_enum, RT5648_SPO_CLSD_RATIO,
	RT5648_SPO_CLSD_RATIO_SFT, rt5648_spo_gain_ratio);

#ifdef RT5648_REG_RW
#define REGVAL_MAX 0xffff
static unsigned int regctl_addr;
static int rt5648_regctl_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = REGVAL_MAX;
	return 0;
}

static int rt5648_regctl_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = regctl_addr;
	ucontrol->value.integer.value[1] = snd_soc_read(codec, regctl_addr);
	return 0;
}

static int rt5648_regctl_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	regctl_addr = ucontrol->value.integer.value[0];
	if(ucontrol->value.integer.value[1] <= REGVAL_MAX)
		snd_soc_write(codec, regctl_addr, ucontrol->value.integer.value[1]);
	return 0;
}
#endif

static const struct snd_kcontrol_new rt5648_snd_controls[] = {
	/* Speaker Output Volume */
	SOC_DOUBLE("Speaker Playback Switch", RT5648_SPK_VOL,
		RT5648_L_MUTE_SFT, RT5648_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("Speaker Playback Volume", RT5648_SPK_VOL,
		RT5648_L_VOL_SFT, RT5648_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* Headphone Output Volume */
	SOC_DOUBLE("HP Playback Switch", RT5648_HP_VOL,
		RT5648_L_MUTE_SFT, RT5648_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("HP Playback Volume", RT5648_HP_VOL,
		RT5648_L_VOL_SFT, RT5648_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* OUTPUT Control */
	SOC_DOUBLE("OUT Playback Switch", RT5648_LOUT1,
		RT5648_L_MUTE_SFT, RT5648_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("OUT Channel Switch", RT5648_LOUT1,
		RT5648_VOL_L_SFT, RT5648_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5648_LOUT1,
		RT5648_L_VOL_SFT, RT5648_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* DAC Digital Volume */
	SOC_DOUBLE("DAC2 Playback Switch", RT5648_DAC_CTRL,
		RT5648_M_DAC_L2_VOL_SFT, RT5648_M_DAC_R2_VOL_SFT, 1, 1),
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5648_DAC1_DIG_VOL,
			RT5648_L_VOL_SFT, RT5648_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("Mono DAC Playback Volume", RT5648_DAC2_DIG_VOL,
			RT5648_L_VOL_SFT, RT5648_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	/* IN1/IN2 Control */
	SOC_ENUM("IN1 Mode Control",  rt5648_in1_mode_enum),
	SOC_SINGLE_TLV("IN1 Boost", RT5648_IN1_IN2,
		RT5648_BST_SFT1, 8, 0, bst_tlv),
	SOC_ENUM("IN2 Mode Control", rt5648_in2_mode_enum),
	SOC_SINGLE_TLV("IN2 Boost", RT5648_IN3,
		RT5648_BST_SFT2, 8, 0, bst_tlv),
	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT5648_INL1_INR1_VOL,
			RT5648_INL_VOL_SFT, RT5648_INR_VOL_SFT,
			31, 1, in_vol_tlv),
	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT5648_STO1_ADC_DIG_VOL,
		RT5648_L_MUTE_SFT, RT5648_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT5648_STO1_ADC_DIG_VOL,
			RT5648_L_VOL_SFT, RT5648_R_VOL_SFT,
			127, 0, adc_vol_tlv),

	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT5648_MONO_ADC_DIG_VOL,
			RT5648_L_VOL_SFT, RT5648_R_VOL_SFT,
			127, 0, adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Gain", RT5648_ADC_BST_VOL1,
			RT5648_STO1_ADC_L_BST_SFT, RT5648_STO1_ADC_R_BST_SFT,
			3, 0, adc_bst_tlv),

	SOC_DOUBLE_TLV("STO2 ADC Boost Gain", RT5648_ADC_BST_VOL1,
			RT5648_STO2_ADC_L_BST_SFT, RT5648_STO2_ADC_R_BST_SFT,
			3, 0, adc_bst_tlv),

	/* TDM */
	SOC_ENUM("TDM Adc Slot0 1 Data", rt5648_tdm_adc_slot0_1_enum),
	SOC_ENUM("TDM Adc Slot2 3 Data", rt5648_tdm_adc_slot2_3_enum),
	SOC_ENUM("TDM Adc Slot4 5 Data", rt5648_tdm_adc_slot4_5_enum),
	SOC_ENUM("TDM Adc Slot6 7 Data", rt5648_tdm_adc_slot6_7_enum),
	SOC_SINGLE("TDM IF1_DAC1_L Sel", RT5648_TDM_CTRL_3, 12, 7, 0),
	SOC_SINGLE("TDM IF1_DAC1_R Sel", RT5648_TDM_CTRL_3, 8, 7, 0),
	SOC_SINGLE("TDM IF1_DAC2_L Sel", RT5648_TDM_CTRL_3, 4, 7, 0),
	SOC_SINGLE("TDM IF1_DAC2_R Sel", RT5648_TDM_CTRL_3, 0, 7, 0),
	
	SOC_ENUM("SPOMIX GAIN CTRL", rt5648_spo_gain_ratio_enum),

	#ifdef RT5648_REG_RW
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Register Control",
		.info = rt5648_regctl_info,
		.get = rt5648_regctl_get,
		.put = rt5648_regctl_put,
	},
      #endif
};

/**
 * set_dmic_clk - Set parameter of dmic.
 *
 * @w: DAPM widget.
 * @kcontrol: The kcontrol of this widget.
 * @event: Event id.
 *
 * Choose dmic clock between 1MHz and 3MHz.
 * It is better for clock to approximate 3MHz.
 */
static int set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	int div[] = {2, 3, 4, 6, 8, 12};
	int idx = -EINVAL, i;
	int rate, red, bound, temp;

	rate = rt5648->lrck[rt5648->aif_pu] << 8;
	/* red = 3000000 * 12; */
	red = 2000000 * 12;
	for (i = 0; i < ARRAY_SIZE(div); i++) {
		bound = div[i] * 2000000;
		if (rate > bound)
			continue;
		temp = bound - rate;
		if (temp < red) {
			red = temp;
			idx = i;
		}
	}
#ifdef USE_ASRC
	idx = 5;
#endif
	if (idx < 0)
		dev_err(codec->dev, "Failed to set DMIC clock\n");
	else
		snd_soc_update_bits(codec, RT5648_DMIC_CTRL1, RT5648_DMIC_CLK_MASK,
					idx << RT5648_DMIC_CLK_SFT);
	return idx;
}

static int check_sysclk1_source(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int val;

	val = snd_soc_read(snd_soc_dapm_to_codec(source->dapm), RT5648_GLB_CLK);
	val &= RT5648_SCLK_SRC_MASK;
	if (val == RT5648_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5648_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5648_STO1_ADC_MIXER,
			RT5648_M_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5648_STO1_ADC_MIXER,
			RT5648_M_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5648_STO1_ADC_MIXER,
			RT5648_M_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5648_STO1_ADC_MIXER,
			RT5648_M_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_mono_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5648_MONO_ADC_MIXER,
			RT5648_M_MONO_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5648_MONO_ADC_MIXER,
			RT5648_M_MONO_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_mono_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5648_MONO_ADC_MIXER,
			RT5648_M_MONO_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5648_MONO_ADC_MIXER,
			RT5648_M_MONO_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5648_AD_DA_MIXER,
			RT5648_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5648_AD_DA_MIXER,
			RT5648_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5648_AD_DA_MIXER,
			RT5648_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5648_AD_DA_MIXER,
			RT5648_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5648_STO_DAC_MIXER,
			RT5648_M_DAC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5648_STO_DAC_MIXER,
			RT5648_M_DAC_L2_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_STO_DAC_MIXER,
			RT5648_M_DAC_R1_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_STO_DAC_MIXER,
			RT5648_M_DAC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5648_STO_DAC_MIXER,
			RT5648_M_DAC_R2_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5648_STO_DAC_MIXER,
			RT5648_M_DAC_L1_STO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_mono_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5648_MONO_DAC_MIXER,
			RT5648_M_DAC_L1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5648_MONO_DAC_MIXER,
			RT5648_M_DAC_L2_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5648_MONO_DAC_MIXER,
			RT5648_M_DAC_R2_MONO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_mono_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_MONO_DAC_MIXER,
			RT5648_M_DAC_R1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5648_MONO_DAC_MIXER,
			RT5648_M_DAC_R2_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5648_MONO_DAC_MIXER,
			RT5648_M_DAC_L2_MONO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_dig_l_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix L Switch", RT5648_DIG_MIXER,
			RT5648_M_STO_L_DAC_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5648_DIG_MIXER,
			RT5648_M_DAC_L2_DAC_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5648_DIG_MIXER,
			RT5648_M_DAC_R2_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_dig_r_mix[] = {
	SOC_DAPM_SINGLE("Sto DAC Mix R Switch", RT5648_DIG_MIXER,
			RT5648_M_STO_R_DAC_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5648_DIG_MIXER,
			RT5648_M_DAC_R2_DAC_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5648_DIG_MIXER,
			RT5648_M_DAC_L2_DAC_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5648_rec_l_mix[] = {
	SOC_DAPM_SINGLE("HPOL Switch", RT5648_REC_L2_MIXER,
			RT5648_M_HP_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5648_REC_L2_MIXER,
			RT5648_M_IN_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5648_REC_L2_MIXER,
			RT5648_M_BST2_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5648_REC_L2_MIXER,
			RT5648_M_BST1_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXL Switch", RT5648_REC_L2_MIXER,
			RT5648_M_OM_L_RM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_rec_r_mix[] = {
	SOC_DAPM_SINGLE("HPOR Switch", RT5648_REC_R2_MIXER,
			RT5648_M_HP_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5648_REC_R2_MIXER,
			RT5648_M_IN_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5648_REC_R2_MIXER,
			RT5648_M_BST2_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5648_REC_R2_MIXER,
			RT5648_M_BST1_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXR Switch", RT5648_REC_R2_MIXER,
			RT5648_M_OM_R_RM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_spk_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5648_SPK_L_MIXER,
			RT5648_M_DAC_L1_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5648_SPK_L_MIXER,
			RT5648_M_DAC_L2_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5648_SPK_L_MIXER,
			RT5648_M_IN_L_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5648_SPK_L_MIXER,
			RT5648_M_BST1_L_SM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_spk_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_SPK_R_MIXER,
			RT5648_M_DAC_R1_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5648_SPK_R_MIXER,
			RT5648_M_DAC_R2_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5648_SPK_R_MIXER,
			RT5648_M_IN_R_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5648_SPK_R_MIXER,
			RT5648_M_BST2_R_SM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_out_l_mix[] = {
	SOC_DAPM_SINGLE("BST1 Switch", RT5648_OUT_L1_MIXER,
			RT5648_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5648_OUT_L1_MIXER,
			RT5648_M_IN_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5648_OUT_L1_MIXER,
			RT5648_M_DAC_L2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5648_OUT_L1_MIXER,
			RT5648_M_DAC_L1_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_out_r_mix[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5648_OUT_R1_MIXER,
			RT5648_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5648_OUT_R1_MIXER,
			RT5648_M_IN_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5648_OUT_R1_MIXER,
			RT5648_M_DAC_R2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_OUT_R1_MIXER,
			RT5648_M_DAC_R1_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_spo_l_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_SPO_MIXER,
			RT5648_M_DAC_R1_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5648_SPO_MIXER,
			RT5648_M_DAC_L1_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL R Switch", RT5648_SPO_MIXER,
			RT5648_M_SV_R_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL L Switch", RT5648_SPO_MIXER,
			RT5648_M_SV_L_SPM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_spo_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_SPO_MIXER,
			RT5648_M_DAC_R1_SPM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL R Switch", RT5648_SPO_MIXER,
			RT5648_M_SV_R_SPM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_hpo_mix[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", RT5648_HPO_MIXER,
			RT5648_M_DAC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPVOL Switch", RT5648_HPO_MIXER,
			RT5648_M_HPVOL_HM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_hpvoll_mix[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", RT5648_HPOMIXL_CTRL,
			RT5648_M_DAC1_HV_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 Switch", RT5648_HPOMIXL_CTRL,
			RT5648_M_DAC2_HV_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5648_HPOMIXL_CTRL,
			RT5648_M_IN_HV_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5648_HPOMIXL_CTRL,
			RT5648_M_BST1_HV_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_hpvolr_mix[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", RT5648_HPOMIXR_CTRL,
			RT5648_M_DAC1_HV_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC2 Switch", RT5648_HPOMIXR_CTRL,
			RT5648_M_DAC2_HV_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5648_HPOMIXR_CTRL,
			RT5648_M_IN_HV_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5648_HPOMIXR_CTRL,
			RT5648_M_BST2_HV_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5648_lout_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5648_LOUT_MIXER,
			RT5648_M_DAC_L1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5648_LOUT_MIXER,
			RT5648_M_DAC_R1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIX L Switch", RT5648_LOUT_MIXER,
			RT5648_M_OV_L_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIX R Switch", RT5648_LOUT_MIXER,
			RT5648_M_OV_R_LM_SFT, 1, 1),
};

/*DAC1 L/R source*/ /* MX-29 [9:8] [11:10] */
static const char *rt5648_dac1_src[] = {
	"IF1 DAC", "IF2 DAC", "IF3 DAC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_dac1l_enum, RT5648_AD_DA_MIXER,
	RT5648_DAC1_L_SEL_SFT, rt5648_dac1_src);

static const struct snd_kcontrol_new rt5648_dac1l_mux =
	SOC_DAPM_ENUM("DAC1 L source", rt5648_dac1l_enum);

static const SOC_ENUM_SINGLE_DECL(
	rt5648_dac1r_enum, RT5648_AD_DA_MIXER,
	RT5648_DAC1_R_SEL_SFT, rt5648_dac1_src);

static const struct snd_kcontrol_new rt5648_dac1r_mux =
	SOC_DAPM_ENUM("DAC1 R source", rt5648_dac1r_enum);

/*DAC2 L/R source*/ /* MX-1B [6:4] [2:0] */
static const char *rt5648_dac12_src[] = {
	"IF1 DAC", "IF2 DAC", "IF3 DAC", "Mono ADC", "VAD_ADC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_dac2l_enum, RT5648_DAC_CTRL,
	RT5648_DAC2_L_SEL_SFT, rt5648_dac12_src);

static const struct snd_kcontrol_new rt5648_dac_l2_mux =
	SOC_DAPM_ENUM("DAC2 L source", rt5648_dac2l_enum);

static const char *rt5648_dacr2_src[] = {
	"IF1 DAC", "IF2 DAC", "IF3 DAC", "Mono ADC", "Haptic"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_dac2r_enum, RT5648_DAC_CTRL,
	RT5648_DAC2_R_SEL_SFT, rt5648_dacr2_src);

static const struct snd_kcontrol_new rt5648_dac_r2_mux =
	SOC_DAPM_ENUM("DAC2 R source", rt5648_dac2r_enum);


/* INL/R source */
static const char *rt5648_inl_src[] = {
	"IN2P", "MonoP"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_inl_enum, RT5648_INL1_INR1_VOL,
	RT5648_INL_SEL_SFT, rt5648_inl_src);

static const struct snd_kcontrol_new rt5648_inl_mux =
	SOC_DAPM_ENUM("INL source", rt5648_inl_enum);

static const char *rt5648_inr_src[] = {
	"IN2N", "MonoN"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_inr_enum, RT5648_INL1_INR1_VOL,
	RT5648_INR_SEL_SFT, rt5648_inr_src);

static const struct snd_kcontrol_new rt5648_inr_mux =
	SOC_DAPM_ENUM("INR source", rt5648_inr_enum);

/* Stereo1 ADC source */
/* MX-27 [12] */
static const char *rt5648_stereo_adc1_src[] = {
	"DAC MIX", "ADC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_stereo1_adc1_enum, RT5648_STO1_ADC_MIXER,
	RT5648_ADC_1_SRC_SFT, rt5648_stereo_adc1_src);

static const struct snd_kcontrol_new rt5648_sto_adc_l1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC L1 source", rt5648_stereo1_adc1_enum);

static const struct snd_kcontrol_new rt5648_sto_adc_r1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC R1 source", rt5648_stereo1_adc1_enum);

/* MX-27 [11] */
static const char *rt5648_stereo_adc2_src[] = {
	"DAC MIX", "DMIC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_stereo1_adc2_enum, RT5648_STO1_ADC_MIXER,
	RT5648_ADC_2_SRC_SFT, rt5648_stereo_adc2_src);

static const struct snd_kcontrol_new rt5648_sto_adc_l2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC L2 source", rt5648_stereo1_adc2_enum);

static const struct snd_kcontrol_new rt5648_sto_adc_r2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC R2 source", rt5648_stereo1_adc2_enum);

/* MX-27 [8] */
static const char *rt5648_stereo_dmic_src[] = {
	"DMIC1", "DMIC2"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_stereo1_dmic_enum, RT5648_STO1_ADC_MIXER,
	RT5648_DMIC_SRC_SFT, rt5648_stereo_dmic_src);

static const struct snd_kcontrol_new rt5648_sto1_dmic_mux =
	SOC_DAPM_ENUM("Stereo1 DMIC source", rt5648_stereo1_dmic_enum);

/* Mono ADC source */
/* MX-28 [12] */
static const char *rt5648_mono_adc_l1_src[] = {
	"Mono DAC MIXL", "ADC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_mono_adc_l1_enum, RT5648_MONO_ADC_MIXER,
	RT5648_MONO_ADC_L1_SRC_SFT, rt5648_mono_adc_l1_src);

static const struct snd_kcontrol_new rt5648_mono_adc_l1_mux =
	SOC_DAPM_ENUM("Mono ADC1 left source", rt5648_mono_adc_l1_enum);
/* MX-28 [11] */
static const char *rt5648_mono_adc_l2_src[] = {
	"Mono DAC MIXL", "DMIC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_mono_adc_l2_enum, RT5648_MONO_ADC_MIXER,
	RT5648_MONO_ADC_L2_SRC_SFT, rt5648_mono_adc_l2_src);

static const struct snd_kcontrol_new rt5648_mono_adc_l2_mux =
	SOC_DAPM_ENUM("Mono ADC2 left source", rt5648_mono_adc_l2_enum);

/* MX-28 [8] */
static const char *rt5648_mono_dmic_src[] = {
	"DMIC1", "DMIC2"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_mono_dmic_l_enum, RT5648_MONO_ADC_MIXER,
	RT5648_MONO_DMIC_L_SRC_SFT, rt5648_mono_dmic_src);

static const struct snd_kcontrol_new rt5648_mono_dmic_l_mux =
	SOC_DAPM_ENUM("Mono DMIC left source", rt5648_mono_dmic_l_enum);
/* MX-28 [1:0] */
static const SOC_ENUM_SINGLE_DECL(
	rt5648_mono_dmic_r_enum, RT5648_MONO_ADC_MIXER,
	RT5648_MONO_DMIC_R_SRC_SFT, rt5648_mono_dmic_src);

static const struct snd_kcontrol_new rt5648_mono_dmic_r_mux =
	SOC_DAPM_ENUM("Mono DMIC Right source", rt5648_mono_dmic_r_enum);
/* MX-28 [4] */
static const char *rt5648_mono_adc_r1_src[] = {
	"Mono DAC MIXR", "ADC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_mono_adc_r1_enum, RT5648_MONO_ADC_MIXER,
	RT5648_MONO_ADC_R1_SRC_SFT, rt5648_mono_adc_r1_src);

static const struct snd_kcontrol_new rt5648_mono_adc_r1_mux =
	SOC_DAPM_ENUM("Mono ADC1 right source", rt5648_mono_adc_r1_enum);
/* MX-28 [3] */
static const char *rt5648_mono_adc_r2_src[] = {
	"Mono DAC MIXR", "DMIC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_mono_adc_r2_enum, RT5648_MONO_ADC_MIXER,
	RT5648_MONO_ADC_R2_SRC_SFT, rt5648_mono_adc_r2_src);

static const struct snd_kcontrol_new rt5648_mono_adc_r2_mux =
	SOC_DAPM_ENUM("Mono ADC2 right source", rt5648_mono_adc_r2_enum);

/* MX-77 [9:8] */
static const char *rt5648_if1_adc_in_src[] = {
	"IF_ADC1", "IF_ADC2", "VAD_ADC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_if1_adc_in_enum, RT5648_TDM_CTRL_1,
	RT5648_IF1_ADC_IN_SFT, rt5648_if1_adc_in_src);

static const struct snd_kcontrol_new rt5648_if1_adc_in_mux =
	SOC_DAPM_ENUM("IF1 ADC IN source", rt5648_if1_adc_in_enum);

/* MX-2F [13:12] */
static const char *rt5648_if2_adc_in_src[] = {
	"IF_ADC1", "IF_ADC2", "VAD_ADC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_if2_adc_in_enum, RT5648_DIG_INF1_DATA,
	RT5648_IF2_ADC_IN_SFT, rt5648_if2_adc_in_src);

static const struct snd_kcontrol_new rt5648_if2_adc_in_mux =
	SOC_DAPM_ENUM("IF2 ADC IN source", rt5648_if2_adc_in_enum);

/* MX-2F [1:0] */
static const char *rt5648_if3_adc_in_src[] = {
	"IF_ADC1", "IF_ADC2", "VAD_ADC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_if3_adc_in_enum, RT5648_DIG_INF1_DATA,
	RT5648_IF3_ADC_IN_SFT, rt5648_if3_adc_in_src);

static const struct snd_kcontrol_new rt5648_if3_adc_in_mux =
	SOC_DAPM_ENUM("IF3 ADC IN source", rt5648_if3_adc_in_enum);

/* MX-31 [15] [13] [11] [9] */
static const char *rt5648_pdm_src[] = {
	"Mono DAC", "Stereo DAC"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_pdm1_l_enum, RT5648_PDM_OUT_CTRL,
	RT5648_PDM1_L_SFT, rt5648_pdm_src);

static const struct snd_kcontrol_new rt5648_pdm1_l_mux =
	SOC_DAPM_ENUM("PDM1 L source", rt5648_pdm1_l_enum);

static const SOC_ENUM_SINGLE_DECL(
	rt5648_pdm1_r_enum, RT5648_PDM_OUT_CTRL,
	RT5648_PDM1_R_SFT, rt5648_pdm_src);

static const struct snd_kcontrol_new rt5648_pdm1_r_mux =
	SOC_DAPM_ENUM("PDM1 R source", rt5648_pdm1_r_enum);

/* MX-9D [9:8] */
static const char *rt5648_vad_adc_src[] = {
	"Sto1 ADC L", "Mono ADC L", "Mono ADC R"
};

static const SOC_ENUM_SINGLE_DECL(
	rt5648_vad_adc_enum, RT5648_VAD_CTRL4,
	RT5648_VAD_SEL_SFT, rt5648_vad_adc_src);

static const struct snd_kcontrol_new rt5648_vad_adc_mux =
	SOC_DAPM_ENUM("VAD ADC source", rt5648_vad_adc_enum);

static const struct snd_kcontrol_new spk_l_vol_control =
	SOC_DAPM_SINGLE("Switch", RT5648_SPK_VOL,
		RT5648_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new spk_r_vol_control =
	SOC_DAPM_SINGLE("Switch", RT5648_SPK_VOL,
		RT5648_VOL_R_SFT, 1, 1);

static const struct snd_kcontrol_new hp_l_vol_control =
	SOC_DAPM_SINGLE("Switch", RT5648_HP_VOL,
		RT5648_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new hp_r_vol_control =
	SOC_DAPM_SINGLE("Switch", RT5648_HP_VOL,
		RT5648_VOL_R_SFT, 1, 1);

static int rt5648_adc_clk_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt5648_index_update_bits(codec,
			RT5648_CHOP_DAC_ADC, 0x1000, 0x1000);
		break;

	case SND_SOC_DAPM_POST_PMD:
		rt5648_index_update_bits(codec,
			RT5648_CHOP_DAC_ADC, 0x1000, 0x0000);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_sto1_adcl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_STO1_ADC_DIG_VOL,
			RT5648_L_MUTE, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_STO1_ADC_DIG_VOL,
			RT5648_L_MUTE,
			RT5648_L_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_sto1_adcr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_STO1_ADC_DIG_VOL,
			RT5648_R_MUTE, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_STO1_ADC_DIG_VOL,
			RT5648_R_MUTE,
			RT5648_R_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_mono_adcl_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_MONO_ADC_DIG_VOL,
			RT5648_L_MUTE, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_MONO_ADC_DIG_VOL,
			RT5648_L_MUTE,
			RT5648_L_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_mono_adcr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_MONO_ADC_DIG_VOL,
			RT5648_R_MUTE, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_MONO_ADC_DIG_VOL,
			RT5648_R_MUTE,
			RT5648_R_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static void hp_amp_power(struct snd_soc_codec *codec, int on)
{
	static int hp_amp_power_count;

	if(on) {
		if(hp_amp_power_count <= 0) {
			/* depop parameters */
			snd_soc_update_bits(codec, RT5648_DEPOP_M2,
				RT5648_DEPOP_MASK, RT5648_DEPOP_MAN);
			snd_soc_write(codec, RT5648_DEPOP_M1, 0x000d);
			rt5648_index_write(codec, RT5648_HP_DCC_INT1, 0x9f01);
			mdelay(150);
			/* headphone amp power on */
			snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
				RT5648_PWR_FV1 | RT5648_PWR_FV2 , 0);
			snd_soc_update_bits(codec, RT5648_PWR_VOL,
				RT5648_PWR_HV_L | RT5648_PWR_HV_R,
				RT5648_PWR_HV_L | RT5648_PWR_HV_R);
			snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
				RT5648_PWR_HP_L | RT5648_PWR_HP_R | RT5648_PWR_HA,
				RT5648_PWR_HP_L | RT5648_PWR_HP_R | RT5648_PWR_HA);
			msleep(5);
			snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
				RT5648_PWR_FV1 | RT5648_PWR_FV2,
				RT5648_PWR_FV1 | RT5648_PWR_FV2);
			/* for TFCG-119 headset noise*/
//			snd_soc_update_bits(codec, RT5648_HP_CALIB_AMP_DET,
//				RT5648_HPD_PS_MASK, RT5648_HPD_PS_EN);
			/* for TFCG-119 headset noise*/
			snd_soc_update_bits(codec, RT5648_DEPOP_M1,
				RT5648_HP_CO_MASK | RT5648_HP_SG_MASK,
				RT5648_HP_CO_EN | RT5648_HP_SG_EN);

			rt5648_index_write(codec, 0x14, 0x1aaa);
			rt5648_index_write(codec, 0x24, 0x0430);
		}
		hp_amp_power_count++;
	} else {
		hp_amp_power_count--;
		if(hp_amp_power_count <= 0) {
			snd_soc_update_bits(codec, RT5648_DEPOP_M1,
				RT5648_HP_SG_MASK | RT5648_HP_L_SMT_MASK |
				RT5648_HP_R_SMT_MASK, RT5648_HP_SG_DIS |
				RT5648_HP_L_SMT_DIS | RT5648_HP_R_SMT_DIS);
			/* headphone amp power down */
			/*
			snd_soc_update_bits(codec, RT5648_DEPOP_M1,
				RT5648_SMT_TRIG_MASK | RT5648_HP_CD_PD_MASK |
				RT5648_HP_CO_MASK | RT5648_HP_CP_MASK |
				RT5648_HP_SG_MASK | RT5648_HP_CB_MASK,
				RT5648_SMT_TRIG_DIS | RT5648_HP_CD_PD_EN |
				RT5648_HP_CO_DIS | RT5648_HP_CP_PD |
				RT5648_HP_SG_EN | RT5648_HP_CB_PD);
			*/
			snd_soc_write(codec, RT5648_DEPOP_M1, 0x0000);
			snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
				RT5648_PWR_HP_L | RT5648_PWR_HP_R | RT5648_PWR_HA,
				0);
		}
	}
}

static void rt5648_pmu_depop(struct snd_soc_codec *codec)
{
	//hp_amp_power(codec, 1);
	/* headphone unmute sequence */
	snd_soc_update_bits(codec, RT5648_DEPOP_M3,
		RT5648_CP_FQ1_MASK | RT5648_CP_FQ2_MASK | RT5648_CP_FQ3_MASK,
		(RT5648_CP_FQ_192_KHZ << RT5648_CP_FQ1_SFT) |
		(RT5648_CP_FQ_12_KHZ << RT5648_CP_FQ2_SFT) |
		(RT5648_CP_FQ_192_KHZ << RT5648_CP_FQ3_SFT));
	rt5648_index_write(codec, RT5648_MAMP_INT_REG2, 0xfc00);
	snd_soc_update_bits(codec, RT5648_DEPOP_M1,
		RT5648_SMT_TRIG_MASK, RT5648_SMT_TRIG_EN);
	snd_soc_update_bits(codec, RT5648_DEPOP_M1,
		RT5648_RSTN_MASK, RT5648_RSTN_EN);
	snd_soc_update_bits(codec, RT5648_DEPOP_M1,
		RT5648_RSTN_MASK | RT5648_HP_L_SMT_MASK | RT5648_HP_R_SMT_MASK,
		RT5648_RSTN_DIS | RT5648_HP_L_SMT_EN | RT5648_HP_R_SMT_EN);
	snd_soc_update_bits(codec, RT5648_HP_VOL,
		RT5648_L_MUTE | RT5648_R_MUTE, 0);
	msleep(40);
	snd_soc_update_bits(codec, RT5648_DEPOP_M1,
		RT5648_HP_SG_MASK | RT5648_HP_L_SMT_MASK |
		RT5648_HP_R_SMT_MASK, RT5648_HP_SG_DIS |
		RT5648_HP_L_SMT_DIS | RT5648_HP_R_SMT_DIS);

}

static void rt5648_pmd_depop(struct snd_soc_codec *codec)
{
	/* headphone mute sequence */
	snd_soc_update_bits(codec, RT5648_DEPOP_M3,
		RT5648_CP_FQ1_MASK | RT5648_CP_FQ2_MASK | RT5648_CP_FQ3_MASK,
		(RT5648_CP_FQ_96_KHZ << RT5648_CP_FQ1_SFT) |
		(RT5648_CP_FQ_12_KHZ << RT5648_CP_FQ2_SFT) |
		(RT5648_CP_FQ_96_KHZ << RT5648_CP_FQ3_SFT));
	rt5648_index_write(codec, RT5648_MAMP_INT_REG2, 0xfc00);
	snd_soc_update_bits(codec, RT5648_DEPOP_M1,
		RT5648_HP_SG_MASK, RT5648_HP_SG_EN);
	snd_soc_update_bits(codec, RT5648_DEPOP_M1,
		RT5648_RSTP_MASK, RT5648_RSTP_EN);
	snd_soc_update_bits(codec, RT5648_DEPOP_M1,
		RT5648_RSTP_MASK | RT5648_HP_L_SMT_MASK |
		RT5648_HP_R_SMT_MASK, RT5648_RSTP_DIS |
		RT5648_HP_L_SMT_EN | RT5648_HP_R_SMT_EN);

	snd_soc_update_bits(codec, RT5648_HP_VOL,
		RT5648_L_MUTE | RT5648_R_MUTE, RT5648_L_MUTE | RT5648_R_MUTE);
	msleep(30);

	//hp_amp_power(codec, 0);
}

static int rt5648_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt5648_pmu_depop(codec);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		rt5648_pmd_depop(codec);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_spk_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt5648_index_write(codec, 0x1c, 0xfd20);
		rt5648_index_write(codec, 0x20, 0x611f);
		rt5648_index_write(codec, 0x21, 0x4040);
		rt5648_index_write(codec, 0x23, 0x0004);

		snd_soc_update_bits(codec, RT5648_PWR_DIG1,
			RT5648_PWR_CLS_D | RT5648_PWR_CLS_D_R | RT5648_PWR_CLS_D_L,
			RT5648_PWR_CLS_D | RT5648_PWR_CLS_D_R | RT5648_PWR_CLS_D_L);
		/*snd_soc_update_bits(codec, RT5648_SPK_VOL,
			RT5648_L_MUTE | RT5648_R_MUTE, 0);*/
			/* set [10:9] to 01b for MCLK protection, realtek recommand */
		snd_soc_update_bits(codec, RT5648_GEN_CTRL3, (0x3 << 9), (0x01 << 9));

		rt5648_update_eqmode(codec, EQ_CH_DACL, rt5648->eq_mode);
		rt5648_update_eqmode(codec, EQ_CH_DACR, rt5648->eq_mode);
	break;

	case SND_SOC_DAPM_PRE_PMD:
		/*snd_soc_update_bits(codec, RT5648_SPK_VOL,
			RT5648_L_MUTE | RT5648_R_MUTE,
			RT5648_L_MUTE | RT5648_R_MUTE);*/
		snd_soc_update_bits(codec, RT5648_PWR_DIG1,
			RT5648_PWR_CLS_D | RT5648_PWR_CLS_D_R | RT5648_PWR_CLS_D_L, 0);	
		snd_soc_update_bits(codec, RT5648_GEN_CTRL3, (0x3 << 9), 0);
		rt5648_update_eqmode(codec, EQ_CH_DACL, NORMAL);
		rt5648_update_eqmode(codec, EQ_CH_DACR, NORMAL);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_spkvoll_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_SPK_VOL,
			RT5648_L_MUTE, RT5648_L_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_spkvolr_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_SPK_VOL,
			RT5648_R_MUTE, RT5648_R_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_lout_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
//		hp_amp_power(codec,1);
		snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
			RT5648_PWR_LM, RT5648_PWR_LM);
		snd_soc_update_bits(codec, RT5648_LOUT1,
			RT5648_L_MUTE | RT5648_R_MUTE, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_LOUT1,
			RT5648_L_MUTE | RT5648_R_MUTE,
			RT5648_L_MUTE | RT5648_R_MUTE);
		snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
			RT5648_PWR_LM, 0);
//		hp_amp_power(codec,0);
		break;

	default:
		return 0;
	}

	return 0;
}

/*resolve headset pop sound*/
static int rt5648_hp_power_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		hp_amp_power(codec, 1);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		hp_amp_power(codec, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_bst1_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	//int gpio_3v;
	//gpio_3v = get_gpio_by_name("P_+3VSO_SYNC_5");//work around for power when play music in idle mode
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* tf103cg FOR MIC agc function*/
		snd_soc_write(codec, RT5648_ALC_DRC_CTRL2, 0x0023);
		snd_soc_write(codec, RT5648_ALC_CTRL_1, 0xC206);
		snd_soc_write(codec, RT5648_ALC_CTRL_2, 0x63E1);
		snd_soc_write(codec, RT5648_ALC_CTRL_3, 0x0011);
		snd_soc_write(codec, RT5648_ALC_CTRL_4, 0x2263);
		snd_soc_write(codec, RT5648_ADJ_HPF1, 0xA220);
		snd_soc_write(codec, RT5648_ADJ_HPF2, 0x0101);
		/* tf103cg FOR MIC agc function*/
		//gpio_direction_output(gpio_3v,1);//work around for power when play music in idle mode
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_bst2_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	//int gpio_3v;
	//gpio_3v = get_gpio_by_name("P_+3VSO_SYNC_5");//work around for power when play music in idle mode
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_PWR_ANLG2,
			RT5648_PWR_BST2_P, RT5648_PWR_BST2_P);
		/* tf103cg FOR MIC agc function*/
		snd_soc_write(codec, RT5648_ALC_DRC_CTRL2,0x00BF);
		snd_soc_write(codec, RT5648_ALC_CTRL_1, 0xC207);
		snd_soc_write(codec, RT5648_ALC_CTRL_2, 0x7FE1);
		snd_soc_write(codec, RT5648_ALC_CTRL_3, 0x0013);
		snd_soc_write(codec, RT5648_ALC_CTRL_4, 0x6324);
		snd_soc_write(codec, RT5648_ADJ_HPF1, 0xA220);
		snd_soc_write(codec, RT5648_ADJ_HPF2, 0x0303);
		/* tf103cg FOR MIC agc function*/
		//gpio_direction_output(gpio_3v,1);//work around for power when play music in idle mode
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_PWR_ANLG2,
			RT5648_PWR_BST2_P, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_pdm1_l_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_PDM_OUT_CTRL,
			RT5648_M_PDM1_L, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_PDM_OUT_CTRL,
			RT5648_M_PDM1_L, RT5648_M_PDM1_L);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_pdm1_r_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_PDM_OUT_CTRL,
			RT5648_M_PDM1_R, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_PDM_OUT_CTRL,
			RT5648_M_PDM1_R, RT5648_M_PDM1_R);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_dac_l_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		//rt5648_update_eqmode(codec, EQ_CH_DACL, rt5648->eq_mode);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		rt5648_update_eqmode(codec, EQ_CH_DACL, NORMAL);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_dac_r_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		//rt5648_update_eqmode(codec, EQ_CH_DACR, rt5648->eq_mode);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		rt5648_update_eqmode(codec, EQ_CH_DACR, NORMAL);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_hpvol_l_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_PWR_MIXER,
			RT5648_PWR_HM_L, RT5648_PWR_HM_L);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_PWR_MIXER,
			RT5648_PWR_HM_L, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_hpvol_r_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5648_PWR_MIXER,
			RT5648_PWR_HM_R, RT5648_PWR_HM_R);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5648_PWR_MIXER,
			RT5648_PWR_HM_R, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5648_post_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
#ifdef USE_ASRC
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
//		snd_soc_write(codec, RT5648_ASRC_1, 0xffff);
//		snd_soc_write(codec, RT5648_ASRC_2, 0x1221);
		break;
	default:
		return 0;
	}
#endif
	return 0;
}

static int rt5648_pre_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
#ifdef USE_ASRC
//		snd_soc_write(codec, RT5648_ASRC_1, 0x0);
//		snd_soc_write(codec, RT5648_ASRC_2, 0x0);
#endif
		break;
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_update_bits(codec, RT5648_PWR_ANLG1, RT5648_LDO_SEL_MASK, 0x2);
		break;
	default:
		return 0;
	}
	return 0;
}

static int rt5648_asrc_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_write(codec, RT5648_ASRC_1, 0x0);
		snd_soc_write(codec, RT5648_ASRC_2, 0x0);
		break;
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_write(codec, RT5648_ASRC_1, 0xffff);
		snd_soc_write(codec, RT5648_ASRC_2, 0x1221);
		break;
	default:
		return 0;
	}
	return 0;
}

static int rt5648_record_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	//int gpio_3v;
	//gpio_3v = get_gpio_by_name("P_+3VSO_SYNC_5");//work around for power when play music in idle mode
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		is_recording = 1;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		is_recording = 0;
		snd_soc_write(codec, RT5648_ADJ_HPF1, 0xB320);
		snd_soc_write(codec, RT5648_ADJ_HPF2, 0x0000);
		snd_soc_update_bits(codec, RT5648_ALC_CTRL_1, RT5648_DRC_AGC_MASK, RT5648_DRC_AGC_DIS);
		//gpio_direction_output(gpio_3v,0);//work around for power when play music in idle mode
		break;
	default:
		return 0;
	}
	return 0;
}

static const struct snd_soc_dapm_widget rt5648_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY_S("ASRC", 1, SND_SOC_NOPM,
		0, 0, rt5648_asrc_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("LDO2", RT5648_PWR_MIXER,
		RT5648_PWR_LDO2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", RT5648_PWR_ANLG2,
		RT5648_PWR_PLL_BIT, 0, NULL, 0),
#if 0
	SND_SOC_DAPM_SUPPLY("JD Power", RT5648_PWR_ANLG2,
		RT5648_PWR_JD1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Det Power", RT5648_PWR_VOL,
		RT5648_PWR_MIC_DET_BIT, 0, NULL, 0),
#else
	SND_SOC_DAPM_SUPPLY("JD Power", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Det Power", SND_SOC_NOPM,
		0, 0, NULL, 0),
#endif
	/* tf103cg FOR MIC agc function*/
	SND_SOC_DAPM_SUPPLY("Recording DRC", SND_SOC_NOPM,
		0, 0, rt5648_record_event, SND_SOC_DAPM_POST_PMU |
		SND_SOC_DAPM_PRE_PMD),
	/* tf103cg FOR MIC agc function*/
	/* Input Side */
	/* micbias */
	SND_SOC_DAPM_MICBIAS("micbias1", RT5648_PWR_ANLG2,
			RT5648_PWR_MB1_BIT, 0),
	SND_SOC_DAPM_MICBIAS("micbias2", RT5648_PWR_ANLG2,
			RT5648_PWR_MB2_BIT, 0),
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),
	SND_SOC_DAPM_INPUT("DMIC L2"),
	SND_SOC_DAPM_INPUT("DMIC R2"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),

	SND_SOC_DAPM_INPUT("Haptic Generator"),

	SND_SOC_DAPM_PGA("DMIC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC1 Power", RT5648_DMIC_CTRL1,
		RT5648_DMIC_1_EN_SFT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC2 Power", RT5648_DMIC_CTRL1,
		RT5648_DMIC_2_EN_SFT, 0, NULL, 0),
	/* Boost */
	SND_SOC_DAPM_PGA_E("BST1", RT5648_PWR_ANLG2,
		RT5648_PWR_BST1_BIT, 0, NULL, 0, rt5648_bst1_event,
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("BST2", RT5648_PWR_ANLG2,
		RT5648_PWR_BST2_BIT, 0, NULL, 0, rt5648_bst2_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	/* Input Volume */
	SND_SOC_DAPM_PGA("INL VOL", RT5648_PWR_VOL,
		RT5648_PWR_IN_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR VOL", RT5648_PWR_VOL,
		RT5648_PWR_IN_R_BIT, 0, NULL, 0),
	/* IN Mux */
	SND_SOC_DAPM_MUX("INL Mux", SND_SOC_NOPM, 0, 0, &rt5648_inl_mux),
	SND_SOC_DAPM_MUX("INR Mux", SND_SOC_NOPM, 0, 0, &rt5648_inr_mux),
	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL", RT5648_PWR_MIXER, RT5648_PWR_RM_L_BIT,
			0, rt5648_rec_l_mix, ARRAY_SIZE(rt5648_rec_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIXR", RT5648_PWR_MIXER, RT5648_PWR_RM_R_BIT,
			0, rt5648_rec_r_mix, ARRAY_SIZE(rt5648_rec_r_mix)),
	/* ADCs */
	SND_SOC_DAPM_ADC("ADC L", NULL, SND_SOC_NOPM,
		0, 0),
	SND_SOC_DAPM_ADC("ADC R", NULL, SND_SOC_NOPM,
		0, 0),

	SND_SOC_DAPM_SUPPLY("ADC L power",RT5648_PWR_DIG1,
			RT5648_PWR_ADC_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC R power",RT5648_PWR_DIG1,
			RT5648_PWR_ADC_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC clock",SND_SOC_NOPM,
			0, 0, rt5648_adc_clk_event,
			SND_SOC_DAPM_POST_PMD |
			SND_SOC_DAPM_POST_PMU),
	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 DMIC Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_sto1_dmic_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_sto_adc_l2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_sto_adc_r2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_sto_adc_l1_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_sto_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono DMIC L Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_mono_dmic_l_mux),
	SND_SOC_DAPM_MUX("Mono DMIC R Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_mono_dmic_r_mux),
	SND_SOC_DAPM_MUX("Mono ADC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_mono_adc_l2_mux),
	SND_SOC_DAPM_MUX("Mono ADC L1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_mono_adc_l1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_mono_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_mono_adc_r2_mux),
	/* ADC Mixer */

	SND_SOC_DAPM_SUPPLY("adc stereo1 filter", RT5648_PWR_DIG2,
		RT5648_PWR_ADC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("adc stereo2 filter", RT5648_PWR_DIG2,
		RT5648_PWR_ADC_S2F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("Sto1 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5648_sto1_adc_l_mix, ARRAY_SIZE(rt5648_sto1_adc_l_mix),
		rt5648_sto1_adcl_event,	SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER_E("Sto1 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5648_sto1_adc_r_mix, ARRAY_SIZE(rt5648_sto1_adc_r_mix),
		rt5648_sto1_adcr_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("adc mono left filter", RT5648_PWR_DIG2,
		RT5648_PWR_ADC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("Mono ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5648_mono_adc_l_mix, ARRAY_SIZE(rt5648_mono_adc_l_mix),
		rt5648_mono_adcl_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("adc mono right filter", RT5648_PWR_DIG2,
		RT5648_PWR_ADC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_E("Mono ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5648_mono_adc_r_mix, ARRAY_SIZE(rt5648_mono_adc_r_mix),
		rt5648_mono_adcr_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),

	/* ADC PGA */
	SND_SOC_DAPM_PGA("Stereo1 ADC MIXL", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Stereo1 ADC MIXR", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Sto2 ADC LR MIX", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("VAD_ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF_ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF_ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC3", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1_ADC4", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* IF2 3 4 Mux */
	SND_SOC_DAPM_MUX("IF1 ADC Mux", SND_SOC_NOPM,
		0, 0, &rt5648_if1_adc_in_mux),
	SND_SOC_DAPM_MUX("IF2 ADC Mux", SND_SOC_NOPM,
		0, 0, &rt5648_if2_adc_in_mux),
	SND_SOC_DAPM_MUX("IF3 ADC Mux", SND_SOC_NOPM,
		0, 0, &rt5648_if3_adc_in_mux),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5648_PWR_DIG1,
		RT5648_PWR_I2S1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2", RT5648_PWR_DIG1,
		RT5648_PWR_I2S2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("VAD ADC Mux", SND_SOC_NOPM,
		0, 0, &rt5648_vad_adc_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Audio DSP */
	SND_SOC_DAPM_PGA("Audio DSP", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER_E("DAC1 MIXL", SND_SOC_NOPM, 0, 0,
		rt5648_dac_l_mix, ARRAY_SIZE(rt5648_dac_l_mix),
		rt5648_dac_l_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER_E("DAC1 MIXR", SND_SOC_NOPM, 0, 0,
		rt5648_dac_r_mix, ARRAY_SIZE(rt5648_dac_r_mix),
		rt5648_dac_r_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA("DAC MIX", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DAC2 channel Mux */
	SND_SOC_DAPM_MUX("DAC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_dac_r2_mux),
	SND_SOC_DAPM_PGA("DAC L2 Volume", RT5648_PWR_DIG1,
			RT5648_PWR_DAC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC R2 Volume", RT5648_PWR_DIG1,
			RT5648_PWR_DAC_R2_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DAC1 L Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_dac1l_mux),
	SND_SOC_DAPM_MUX("DAC1 R Mux", SND_SOC_NOPM, 0, 0,
				&rt5648_dac1r_mux),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("dac stereo1 filter", RT5648_PWR_DIG2,
		RT5648_PWR_DAC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono left filter", RT5648_PWR_DIG2,
		RT5648_PWR_DAC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("dac mono right filter", RT5648_PWR_DIG2,
		RT5648_PWR_DAC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5648_sto_dac_l_mix, ARRAY_SIZE(rt5648_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5648_sto_dac_r_mix, ARRAY_SIZE(rt5648_sto_dac_r_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5648_mono_dac_l_mix, ARRAY_SIZE(rt5648_mono_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5648_mono_dac_r_mix, ARRAY_SIZE(rt5648_mono_dac_r_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5648_dig_l_mix, ARRAY_SIZE(rt5648_dig_l_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5648_dig_r_mix, ARRAY_SIZE(rt5648_dig_r_mix)),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, RT5648_PWR_DIG1,
			RT5648_PWR_DAC_L1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC L2", NULL, RT5648_PWR_DIG1,
			RT5648_PWR_DAC_L2_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, RT5648_PWR_DIG1,
			RT5648_PWR_DAC_R1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R2", NULL, RT5648_PWR_DIG1,
			RT5648_PWR_DAC_R2_BIT, 0),
	/* OUT Mixer */
	SND_SOC_DAPM_MIXER("SPK MIXL", RT5648_PWR_MIXER, RT5648_PWR_SM_L_BIT,
		0, rt5648_spk_l_mix, ARRAY_SIZE(rt5648_spk_l_mix)),
	SND_SOC_DAPM_MIXER("SPK MIXR", RT5648_PWR_MIXER, RT5648_PWR_SM_R_BIT,
		0, rt5648_spk_r_mix, ARRAY_SIZE(rt5648_spk_r_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXL", RT5648_PWR_MIXER, RT5648_PWR_OM_L_BIT,
		0, rt5648_out_l_mix, ARRAY_SIZE(rt5648_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5648_PWR_MIXER, RT5648_PWR_OM_R_BIT,
		0, rt5648_out_r_mix, ARRAY_SIZE(rt5648_out_r_mix)),
	/* Ouput Volume */
	SND_SOC_DAPM_OUT_DRV("SPOL Mute", RT5648_SPK_VOL,
		RT5648_L_MUTE_SFT, 1, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("SPOR Mute", RT5648_SPK_VOL,
		RT5648_R_MUTE_SFT, 1, NULL, 0),
	SND_SOC_DAPM_SWITCH_E("SPKVOL L", RT5648_PWR_VOL,
		RT5648_PWR_SV_L_BIT, 0, &spk_l_vol_control, 
		rt5648_spkvoll_event, SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SWITCH_E("SPKVOL R", RT5648_PWR_VOL,
		RT5648_PWR_SV_R_BIT, 0,	&spk_r_vol_control, 
		rt5648_spkvolr_event, SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MIXER_E("HPOVOL MIXL", RT5648_PWR_VOL, RT5648_PWR_HV_L_BIT,
		0, rt5648_hpvoll_mix, ARRAY_SIZE(rt5648_hpvoll_mix),
		rt5648_hpvol_l_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MIXER_E("HPOVOL MIXR", RT5648_PWR_VOL, RT5648_PWR_HV_R_BIT,
		0, rt5648_hpvolr_mix, ARRAY_SIZE(rt5648_hpvolr_mix),
		rt5648_hpvol_r_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA("DAC 1", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC 2", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_SWITCH("HPOVOL L", SND_SOC_NOPM,
		0, 0, &hp_l_vol_control),
	SND_SOC_DAPM_SWITCH("HPOVOL R", SND_SOC_NOPM,
		0, 0, &hp_r_vol_control),


	/* HPO/LOUT/Mono Mixer */
	SND_SOC_DAPM_MIXER("SPOL MIX", SND_SOC_NOPM, 0,
		0, rt5648_spo_l_mix, ARRAY_SIZE(rt5648_spo_l_mix)),
	SND_SOC_DAPM_MIXER("SPOR MIX", SND_SOC_NOPM, 0,
		0, rt5648_spo_r_mix, ARRAY_SIZE(rt5648_spo_r_mix)),
	SND_SOC_DAPM_MIXER("HPO MIX", SND_SOC_NOPM, 0, 0,
		rt5648_hpo_mix, ARRAY_SIZE(rt5648_hpo_mix)),
	SND_SOC_DAPM_MIXER("LOUT MIX", SND_SOC_NOPM, 0, 0,
		rt5648_lout_mix, ARRAY_SIZE(rt5648_lout_mix)),

	SND_SOC_DAPM_SUPPLY("HP amp Power", SND_SOC_NOPM,
		0, 0, rt5648_hp_power_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("HP amp", 1, SND_SOC_NOPM,
		0, 0, rt5648_hp_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("LOUT amp", 1, SND_SOC_NOPM,
		0, 0, rt5648_lout_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("SPK amp", 2, SND_SOC_NOPM,
		0, 0, rt5648_spk_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* PDM */
	SND_SOC_DAPM_SUPPLY("PDM1 Power", RT5648_PWR_DIG2,
		RT5648_PWR_PDM1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MUX_E("PDM1 L Mux", SND_SOC_NOPM,
		0, 0, &rt5648_pdm1_l_mux, rt5648_pdm1_l_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("PDM1 R Mux", SND_SOC_NOPM,
		0, 0, &rt5648_pdm1_r_mux, rt5648_pdm1_r_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
	SND_SOC_DAPM_OUTPUT("PDM1L"),
	SND_SOC_DAPM_OUTPUT("PDM1R"),
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),

	SND_SOC_DAPM_POST("DAPM_POST", rt5648_post_event),
	SND_SOC_DAPM_PRE("DAPM_PRE", rt5648_pre_event),
};

static const struct snd_soc_dapm_route rt5648_dapm_routes[] = {
	{ "IN1P", NULL, "LDO2" },
	{ "IN2P", NULL, "LDO2" },

	{ "DMIC1", NULL, "DMIC L1" },
	{ "DMIC1", NULL, "DMIC R1" },
	{ "DMIC2", NULL, "DMIC L2" },
	{ "DMIC2", NULL, "DMIC R2" },

	{ "BST1", NULL, "IN1P" },
	{ "BST1", NULL, "IN1N" },
	{ "BST1", NULL, "JD Power" },
	{ "BST1", NULL, "Mic Det Power" },
	{ "BST2", NULL, "IN2P" },
	{ "BST2", NULL, "IN2N" },

	/* tf103cg FOR MIC agc function*/
	{ "BST1", NULL, "Recording DRC" },
	{ "BST2", NULL, "Recording DRC" },
	/* tf103cg FOR MIC agc function*/

	{ "INL VOL", NULL, "IN2P" },
	{ "INR VOL", NULL, "IN2N" },

	{ "RECMIXL", "HPOL Switch", "HPOL" },
	{ "RECMIXL", "INL Switch", "INL VOL" },
	{ "RECMIXL", "BST2 Switch", "BST2" },
	{ "RECMIXL", "BST1 Switch", "BST1" },
	{ "RECMIXL", "OUT MIXL Switch", "OUT MIXL" },

	{ "RECMIXR", "HPOR Switch", "HPOR" },
	{ "RECMIXR", "INR Switch", "INR VOL" },
	{ "RECMIXR", "BST2 Switch", "BST2" },
	{ "RECMIXR", "BST1 Switch", "BST1" },
	{ "RECMIXR", "OUT MIXR Switch", "OUT MIXR" },

	{ "ADC L", NULL, "RECMIXL" },
	{ "ADC L", NULL, "ADC L power" },
	{ "ADC L", NULL, "ADC clock" },
	{ "ADC R", NULL, "RECMIXR" },
	{ "ADC R", NULL, "ADC R power" },
	{ "ADC R", NULL, "ADC clock" },

	{"DMIC L1", NULL, "DMIC CLK"},
	{"DMIC L1", NULL, "DMIC1 Power"},
	{"DMIC R1", NULL, "DMIC CLK"},
	{"DMIC R1", NULL, "DMIC1 Power"},
	{"DMIC L2", NULL, "DMIC CLK"},
	{"DMIC L2", NULL, "DMIC2 Power"},
	{"DMIC R2", NULL, "DMIC CLK"},
	{"DMIC R2", NULL, "DMIC2 Power"},

	{ "Stereo1 DMIC Mux", "DMIC1", "DMIC1" },
	{ "Stereo1 DMIC Mux", "DMIC2", "DMIC2" },

	{ "Mono DMIC L Mux", "DMIC1", "DMIC L1" },
	{ "Mono DMIC L Mux", "DMIC2", "DMIC L2" },

	{ "Mono DMIC R Mux", "DMIC1", "DMIC R1" },
	{ "Mono DMIC R Mux", "DMIC2", "DMIC R2" },

	{ "Stereo1 ADC L2 Mux", "DMIC", "Stereo1 DMIC Mux" },
	{ "Stereo1 ADC L2 Mux", "DAC MIX", "DAC MIXL" },
	{ "Stereo1 ADC L1 Mux", "ADC", "ADC L" },
	{ "Stereo1 ADC L1 Mux", "DAC MIX", "DAC MIXL" },

	{ "Stereo1 ADC R1 Mux", "ADC", "ADC R" },
	{ "Stereo1 ADC R1 Mux", "DAC MIX", "DAC MIXR" },
	{ "Stereo1 ADC R2 Mux", "DMIC", "Stereo1 DMIC Mux" },
	{ "Stereo1 ADC R2 Mux", "DAC MIX", "DAC MIXR" },

	{ "Mono ADC L2 Mux", "DMIC", "Mono DMIC L Mux" },
	{ "Mono ADC L2 Mux", "Mono DAC MIXL", "Mono DAC MIXL" },
	{ "Mono ADC L1 Mux", "Mono DAC MIXL", "Mono DAC MIXL" },
	{ "Mono ADC L1 Mux", "ADC", "ADC L" },

	{ "Mono ADC R1 Mux", "Mono DAC MIXR", "Mono DAC MIXR" },
	{ "Mono ADC R1 Mux", "ADC", "ADC R" },
	{ "Mono ADC R2 Mux", "DMIC", "Mono DMIC R Mux" },
	{ "Mono ADC R2 Mux", "Mono DAC MIXR", "Mono DAC MIXR" },

	{ "Sto1 ADC MIXL", "ADC1 Switch", "Stereo1 ADC L1 Mux" },
	{ "Sto1 ADC MIXL", "ADC2 Switch", "Stereo1 ADC L2 Mux" },
	{ "Sto1 ADC MIXR", "ADC1 Switch", "Stereo1 ADC R1 Mux" },
	{ "Sto1 ADC MIXR", "ADC2 Switch", "Stereo1 ADC R2 Mux" },

	{ "Stereo1 ADC MIXL", NULL, "Sto1 ADC MIXL" },
	{ "Stereo1 ADC MIXL", NULL, "adc stereo1 filter" },
	{ "adc stereo1 filter", NULL, "PLL1", check_sysclk1_source },

	{ "Stereo1 ADC MIXR", NULL, "Sto1 ADC MIXR" },
	{ "Stereo1 ADC MIXR", NULL, "adc stereo1 filter" },
	{ "adc stereo1 filter", NULL, "PLL1", check_sysclk1_source },

	{ "Mono ADC MIXL", "ADC1 Switch", "Mono ADC L1 Mux" },
	{ "Mono ADC MIXL", "ADC2 Switch", "Mono ADC L2 Mux" },
	{ "Mono ADC MIXL", NULL, "adc mono left filter" },
	{ "adc mono left filter", NULL, "PLL1", check_sysclk1_source },

	{ "Mono ADC MIXR", "ADC1 Switch", "Mono ADC R1 Mux" },
	{ "Mono ADC MIXR", "ADC2 Switch", "Mono ADC R2 Mux" },
	{ "Mono ADC MIXR", NULL, "adc mono right filter" },
	{ "adc mono right filter", NULL, "PLL1", check_sysclk1_source },

	{ "VAD ADC Mux", "Sto1 ADC L", "Stereo1 ADC MIXL" },
	{ "VAD ADC Mux", "Mono ADC L", "Mono ADC MIXL" },
	{ "VAD ADC Mux", "Mono ADC R", "Mono ADC MIXR" },

	{ "IF_ADC1", NULL, "Stereo1 ADC MIXL" },
	{ "IF_ADC1", NULL, "Stereo1 ADC MIXR" },
	{ "IF_ADC2", NULL, "Mono ADC MIXL" },
	{ "IF_ADC2", NULL, "Mono ADC MIXR" },
	{ "VAD_ADC", NULL, "VAD ADC Mux" },

	{ "IF1 ADC Mux", "IF_ADC1", "IF_ADC1" },
	{ "IF1 ADC Mux", "IF_ADC2", "IF_ADC2" },
	{ "IF1 ADC Mux", "VAD_ADC", "VAD_ADC" },

	{ "IF2 ADC Mux", "IF_ADC1", "IF_ADC1" },
	{ "IF2 ADC Mux", "IF_ADC2", "IF_ADC2" },
	{ "IF2 ADC Mux", "VAD_ADC", "VAD_ADC" },

	{ "IF1 ADC", NULL, "I2S1" },
	{ "IF1 ADC", NULL, "IF1 ADC Mux" },
	{ "IF2 ADC", NULL, "I2S2" },
	{ "IF2 ADC", NULL, "IF2 ADC Mux" },

	{ "I2S1", NULL, "ASRC" },
	{ "I2S2", NULL, "ASRC" },
	
	{ "AIF1TX", NULL, "IF1 ADC" },
	{ "AIF2TX", NULL, "IF2 ADC" },

	{ "IF1 DAC1", NULL, "AIF1RX" },
	{ "IF1 DAC2", NULL, "AIF1RX" },

	{ "IF2 DAC", NULL, "AIF2RX" },
	
	{ "IF1 DAC1", NULL, "I2S1" },
	{ "IF1 DAC2", NULL, "I2S1" },
	{ "IF2 DAC", NULL, "I2S2" },

	{ "IF1 DAC2 L", NULL, "IF1 DAC2" },
	{ "IF1 DAC2 R", NULL, "IF1 DAC2" },
	{ "IF1 DAC1 L", NULL, "IF1 DAC1" },
	{ "IF1 DAC1 R", NULL, "IF1 DAC1" },
	{ "IF2 DAC L", NULL, "IF2 DAC" },
	{ "IF2 DAC R", NULL, "IF2 DAC" },

	{ "DAC1 L Mux", "IF1 DAC", "IF1 DAC1 L" },
	{ "DAC1 L Mux", "IF2 DAC", "IF2 DAC L" },

	{ "DAC1 R Mux", "IF1 DAC", "IF1 DAC1 R" },
	{ "DAC1 R Mux", "IF2 DAC", "IF2 DAC R" },

	{ "DAC1 MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL" },
	{ "DAC1 MIXL", "DAC1 Switch", "DAC1 L Mux" },
	{ "DAC1 MIXL", NULL, "dac stereo1 filter" },
	{ "DAC1 MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR" },
	{ "DAC1 MIXR", "DAC1 Switch", "DAC1 R Mux" },
	{ "DAC1 MIXR", NULL, "dac stereo1 filter" },

	{ "DAC MIX", NULL, "DAC1 MIXL" },
	{ "DAC MIX", NULL, "DAC1 MIXR" },

	{ "Audio DSP", NULL, "DAC1 MIXL" },
	{ "Audio DSP", NULL, "DAC1 MIXR" },

	{ "DAC L2 Mux", "IF1 DAC", "IF1 DAC2 L" },
	{ "DAC L2 Mux", "IF2 DAC", "IF2 DAC L" },
	{ "DAC L2 Mux", "Mono ADC", "Mono ADC MIXL" },
	{ "DAC L2 Mux", "VAD_ADC", "VAD_ADC" },
	{ "DAC L2 Volume", NULL, "DAC L2 Mux" },
	{ "DAC L2 Volume", NULL, "dac mono left filter" },

	{ "DAC R2 Mux", "IF1 DAC", "IF1 DAC2 R" },
	{ "DAC R2 Mux", "IF2 DAC", "IF2 DAC R" },
	{ "DAC R2 Mux", "Mono ADC", "Mono ADC MIXR" },
	{ "DAC R2 Mux", "Haptic", "Haptic Generator" },
	{ "DAC R2 Volume", NULL, "DAC R2 Mux" },
	{ "DAC R2 Volume", NULL, "dac mono right filter" },

	{ "Stereo DAC MIXL", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXL", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXL", "DAC L2 Switch", "DAC L2 Volume" },
	{ "Stereo DAC MIXL", NULL, "dac stereo1 filter" },
	{ "Stereo DAC MIXR", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXR", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXR", "DAC R2 Switch", "DAC R2 Volume" },
	{ "Stereo DAC MIXR", NULL, "dac stereo1 filter" },

	{ "Mono DAC MIXL", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Mono DAC MIXL", "DAC L2 Switch", "DAC L2 Volume" },
	{ "Mono DAC MIXL", "DAC R2 Switch", "DAC R2 Volume" },
	{ "Mono DAC MIXL", NULL, "dac mono left filter" },
	{ "Mono DAC MIXR", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Mono DAC MIXR", "DAC R2 Switch", "DAC R2 Volume" },
	{ "Mono DAC MIXR", "DAC L2 Switch", "DAC L2 Volume" },
	{ "Mono DAC MIXR", NULL, "dac mono right filter" },

	{ "DAC MIXL", "Sto DAC Mix L Switch", "Stereo DAC MIXL" },
	{ "DAC MIXL", "DAC L2 Switch", "DAC L2 Volume" },
	{ "DAC MIXL", "DAC R2 Switch", "DAC R2 Volume" },
	{ "DAC MIXR", "Sto DAC Mix R Switch", "Stereo DAC MIXR" },
	{ "DAC MIXR", "DAC R2 Switch", "DAC R2 Volume" },
	{ "DAC MIXR", "DAC L2 Switch", "DAC L2 Volume" },

	{ "DAC L1", NULL, "Stereo DAC MIXL" },
	{ "DAC L1", NULL, "PLL1", check_sysclk1_source },
	{ "DAC R1", NULL, "Stereo DAC MIXR" },
	{ "DAC R1", NULL, "PLL1", check_sysclk1_source },
	{ "DAC L2", NULL, "Mono DAC MIXL" },
	{ "DAC L2", NULL, "PLL1", check_sysclk1_source },
	{ "DAC R2", NULL, "Mono DAC MIXR" },
	{ "DAC R2", NULL, "PLL1", check_sysclk1_source },

	{ "SPK MIXL", "BST1 Switch", "BST1" },
	{ "SPK MIXL", "INL Switch", "INL VOL" },
	{ "SPK MIXL", "DAC L1 Switch", "DAC L1" },
	{ "SPK MIXL", "DAC L2 Switch", "DAC L2" },
	{ "SPK MIXR", "BST2 Switch", "BST2" },
	{ "SPK MIXR", "INR Switch", "INR VOL" },
	{ "SPK MIXR", "DAC R1 Switch", "DAC R1" },
	{ "SPK MIXR", "DAC R2 Switch", "DAC R2" },

	{ "OUT MIXL", "BST1 Switch", "BST1" },
	{ "OUT MIXL", "INL Switch", "INL VOL" },
	{ "OUT MIXL", "DAC L2 Switch", "DAC L2" },
	{ "OUT MIXL", "DAC L1 Switch", "DAC L1" },

	{ "OUT MIXR", "BST2 Switch", "BST2" },
	{ "OUT MIXR", "INR Switch", "INR VOL" },
	{ "OUT MIXR", "DAC R2 Switch", "DAC R2" },
	{ "OUT MIXR", "DAC R1 Switch", "DAC R1" },

	{ "HPOVOL MIXL", "DAC1 Switch", "DAC L1" },
	{ "HPOVOL MIXL", "DAC2 Switch", "DAC L2" },
	{ "HPOVOL MIXL", "INL Switch", "INL VOL" },
	{ "HPOVOL MIXL", "BST1 Switch", "BST1" },
	{ "HPOVOL MIXR", "DAC1 Switch", "DAC R1" },
	{ "HPOVOL MIXR", "DAC2 Switch", "DAC R2" },
	{ "HPOVOL MIXR", "INR Switch", "INR VOL" },
	{ "HPOVOL MIXR", "BST2 Switch", "BST2" },

	{ "DAC 2", NULL, "DAC L2" },
	{ "DAC 2", NULL, "DAC R2" },
	{ "DAC 1", NULL, "DAC L1" },
	{ "DAC 1", NULL, "DAC R1" },
	{ "HPOVOL L", "Switch", "HPOVOL MIXL" },
	{ "HPOVOL R", "Switch", "HPOVOL MIXR" },
	{ "HPOVOL", NULL, "HPOVOL L" },
	{ "HPOVOL", NULL, "HPOVOL R" },
	{ "HPO MIX", "DAC1 Switch", "DAC 1" },
	{ "HPO MIX", "HPVOL Switch", "HPOVOL" },

	{ "SPKVOL L", "Switch", "SPK MIXL" },
	{ "SPKVOL R", "Switch", "SPK MIXR" },

	{ "SPOL MIX", "DAC R1 Switch", "DAC R1" },
	{ "SPOL MIX", "DAC L1 Switch", "DAC L1" },
	{ "SPOL MIX", "SPKVOL R Switch", "SPKVOL R" },
	{ "SPOL MIX", "SPKVOL L Switch", "SPKVOL L" },
	{ "SPOR MIX", "DAC R1 Switch", "DAC R1" },
	{ "SPOR MIX", "SPKVOL R Switch", "SPKVOL R" },

	{ "LOUT MIX", "DAC L1 Switch", "DAC L1" },
	{ "LOUT MIX", "DAC R1 Switch", "DAC R1" },
	{ "LOUT MIX", "OUTMIX L Switch", "OUT MIXL" },
	{ "LOUT MIX", "OUTMIX R Switch", "OUT MIXR" },

	{ "PDM1 L Mux", "Stereo DAC", "Stereo DAC MIXL" },
	{ "PDM1 L Mux", "Mono DAC", "Mono DAC MIXL" },
	{ "PDM1 L Mux", NULL, "PDM1 Power" },
	{ "PDM1 R Mux", "Stereo DAC", "Stereo DAC MIXR" },
	{ "PDM1 R Mux", "Mono DAC", "Mono DAC MIXR" },
	{ "PDM1 R Mux", NULL, "PDM1 Power" },

	{ "HP amp", NULL, "HPO MIX" },
	{ "HP amp", NULL, "JD Power" },
	{ "HP amp", NULL, "Mic Det Power" },
	{ "HP amp", NULL, "HP amp Power" },
	//{ "HP amp", NULL, "LDO2" },
	{ "HPOL", NULL, "HP amp" },
	{ "HPOR", NULL, "HP amp" },

	{ "LOUT amp", NULL, "LOUT MIX" },
	{ "LOUT amp", NULL, "HP amp Power" },
	{ "LOUTL", NULL, "LOUT amp" },
	{ "LOUTR", NULL, "LOUT amp" },

	{ "PDM1L", NULL, "PDM1 L Mux" },
	{ "PDM1R", NULL, "PDM1 R Mux" },

	{ "SPOL Mute", NULL, "SPOL MIX" },
	{ "SPOR Mute", NULL, "SPOR MIX" },
	{ "SPK amp", NULL, "SPOL Mute" },
	{ "SPK amp", NULL, "SPOR Mute" },
	{ "SPOL", NULL, "SPK amp" },
	{ "SPOR", NULL, "SPK amp" },
};

static int get_clk_info(int sclk, int rate)
{
	int i, pd[] = {1, 2, 3, 4, 6, 8, 12, 16};

#ifdef USE_ASRC
	return 0;
#endif
	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
}

static int rt5648_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

  if (RT5648_AIF2 == dai->id)
    snd_soc_update_bits(codec, RT5648_GEN_CTRL3, 0x2, 0);

	rt5648->lrck[dai->id] = params_rate(params);
	pre_div = get_clk_info(rt5648->sysclk, rt5648->lrck[dai->id]);
	if (pre_div < 0) {
		dev_err(codec->dev, "Unsupported clock setting\n");
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(codec->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}
	bclk_ms = frame_size > 32 ? 1 : 0;
	rt5648->bclk[dai->id] = rt5648->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5648->bclk[dai->id], rt5648->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val_len |= RT5648_I2S_DL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val_len |= RT5648_I2S_DL_24;
		break;
	case SNDRV_PCM_FORMAT_S8:
		val_len |= RT5648_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}
	switch (dai->id) {
	case RT5648_AIF1:
 		mask_clk = RT5648_I2S_BCLK_MS1_MASK | RT5648_I2S_PD1_MASK;
		val_clk = bclk_ms << RT5648_I2S_BCLK_MS1_SFT |
			pre_div << RT5648_I2S_PD1_SFT;
		snd_soc_update_bits(codec, RT5648_I2S1_SDP,
			RT5648_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT5648_ADDA_CLK1, mask_clk, val_clk);
		break;
	case  RT5648_AIF2:
		mask_clk = RT5648_I2S_BCLK_MS2_MASK | RT5648_I2S_PD2_MASK;
		val_clk = bclk_ms << RT5648_I2S_BCLK_MS2_SFT |
			pre_div << RT5648_I2S_PD2_SFT;
		snd_soc_update_bits(codec, RT5648_I2S2_SDP,
			RT5648_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT5648_ADDA_CLK1, mask_clk, val_clk);
		break;
	default:
		dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	return 0;
}

static int rt5648_hw_free(struct snd_pcm_substream *substream,
  struct snd_soc_dai *dai)
{
  struct snd_soc_pcm_runtime *rtd = substream->private_data;
  struct snd_soc_codec *codec = rtd->codec;

  if (RT5648_AIF2 == dai->id)
    snd_soc_update_bits(codec, RT5648_GEN_CTRL3, 0x2, 0x2);

  return 0;
}

static int rt5648_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);

	rt5648->aif_pu = dai->id;
	return 0;
}

static int rt5648_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5648->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5648_I2S_MS_S;
		rt5648->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5648_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5648_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5648_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5648_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}
	switch (dai->id) {
	case RT5648_AIF1:
		snd_soc_update_bits(codec, RT5648_I2S1_SDP,
			RT5648_I2S_MS_MASK | RT5648_I2S_BP_MASK |
			RT5648_I2S_DF_MASK, reg_val);
		break;
	case  RT5648_AIF2:
		snd_soc_update_bits(codec, RT5648_I2S1_SDP,
			RT5648_I2S_MS_MASK | RT5648_I2S_BP_MASK |
			RT5648_I2S_DF_MASK, reg_val);
		break;
	default:
		dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt5648_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	if (freq == rt5648->sysclk && clk_id == rt5648->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5648_SCLK_S_MCLK:
		reg_val |= RT5648_SCLK_SRC_MCLK;
		break;
	case RT5648_SCLK_S_PLL1:
		reg_val |= RT5648_SCLK_SRC_PLL1;
		break;
	case RT5648_SCLK_S_RCCLK:
		reg_val |= RT5648_SCLK_SRC_RCCLK;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT5648_GLB_CLK,
		RT5648_SCLK_SRC_MASK, reg_val);
	rt5648->sysclk = freq;
	rt5648->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

/**
 * rt5648_pll_calc - Calcualte PLL M/N/K code.
 * @freq_in: external clock provided to codec.
 * @freq_out: target clock which codec works on.
 * @pll_code: Pointer to structure with M, N, K and bypass flag.
 *
 * Calcualte M/N/K code to configure PLL for codec. And K is assigned to 2
 * which make calculation more efficiently.
 *
 * Returns 0 for success or negative error code.
 */
static int rt5648_pll_calc(const unsigned int freq_in,
	const unsigned int freq_out, struct rt5648_pll_code *pll_code)
{
	int max_n = RT5648_PLL_N_MAX, max_m = RT5648_PLL_M_MAX;
	int k, n, m, red, n_t, m_t, pll_out, in_t, out_t;
	int red_t = abs(freq_out - freq_in);
	bool bypass = false;

	if (RT5648_PLL_INP_MAX < freq_in || RT5648_PLL_INP_MIN > freq_in)
		return -EINVAL;

	k = 100000000 / freq_out - 2;
	if (k > RT5648_PLL_K_MAX)
		k = RT5648_PLL_K_MAX;
	for (n_t = 0; n_t <= max_n; n_t++) {
		in_t = freq_in / (k + 2);
		pll_out = freq_out / (n_t + 2);
		if (in_t < 0)
			continue;
		if (in_t == pll_out) {
			bypass = true;
			n = n_t;
			goto code_find;
		}
		red = abs(in_t - pll_out);
		if (red < red_t) {
			bypass = true;
			n = n_t;
			m = m_t;
			if (red == 0)
				goto code_find;
			red_t = red;
		}
		for (m_t = 0; m_t <= max_m; m_t++) {
			out_t = in_t / (m_t + 2);
			red = abs(out_t - pll_out);
			if (red < red_t) {
				bypass = false;
				n = n_t;
				m = m_t;
				if (red == 0)
					goto code_find;
				red_t = red;
			}
		}
	}
	pr_debug("Only get approximation about PLL\n");

code_find:

	pll_code->m_bp = bypass;
	pll_code->m_code = m;
	pll_code->n_code = n;
	pll_code->k_code = k;
	return 0;
}

static int rt5648_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
	struct rt5648_pll_code pll_code;
	int ret;

	if (source == rt5648->pll_src && freq_in == rt5648->pll_in &&
	    freq_out == rt5648->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt5648->pll_in = 0;
		rt5648->pll_out = 0;
		snd_soc_update_bits(codec, RT5648_GLB_CLK,
			RT5648_SCLK_SRC_MASK, RT5648_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5648_PLL1_S_MCLK:
		snd_soc_update_bits(codec, RT5648_GLB_CLK,
			RT5648_PLL1_SRC_MASK, RT5648_PLL1_SRC_MCLK);
		break;
	case RT5648_PLL1_S_BCLK1:
	case RT5648_PLL1_S_BCLK2:
		switch (dai->id) {
		case RT5648_AIF1:
			snd_soc_update_bits(codec, RT5648_GLB_CLK,
				RT5648_PLL1_SRC_MASK, RT5648_PLL1_SRC_BCLK1);
			break;
		case  RT5648_AIF2:
			snd_soc_update_bits(codec, RT5648_GLB_CLK,
				RT5648_PLL1_SRC_MASK, RT5648_PLL1_SRC_BCLK2);
			break;
		default:
			dev_err(codec->dev, "Invalid dai->id: %d\n", dai->id);
			return -EINVAL;
		}
		break;
	default:
		dev_err(codec->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rt5648_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(codec->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_write(codec, RT5648_PLL_CTRL1,
		pll_code.n_code << RT5648_PLL_N_SFT | pll_code.k_code);
	snd_soc_write(codec, RT5648_PLL_CTRL2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5648_PLL_M_SFT |
		pll_code.m_bp << RT5648_PLL_M_BP_SFT);

	rt5648->pll_in = freq_in;
	rt5648->pll_out = freq_out;
	rt5648->pll_src = source;

	return 0;
}


static int rt5648_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			unsigned int rx_mask, int slots, int slot_width)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int val = 0;

	if (rx_mask || tx_mask)
		val |= (1 << 14);

	switch (slots) {
	case 4:
		val |= (1 << 12);
		break;
	case 6:
		val |= (2 << 12);
		break;
	case 8:
		val |= (3 << 12);
		break;
	case 2:
	default:
		break;
	}

	switch (slot_width) {
	case 20:
		val |= (1 << 10);
		break;
	case 24:
		val |= (2 << 10);
		break;
	case 32:
		val |= (3 << 10);
		break;
	case 16:
	default:
		break;
	}

	snd_soc_update_bits(codec, RT5648_TDM_CTRL_1, 0x7c00, val);

	return 0;
}

/**
 * rt5648_index_show - Dump private registers.
 * @dev: codec device.
 * @attr: device attribute.
 * @buf: buffer for display.
 *
 * To show non-zero values of all private registers.
 *
 * Returns buffer length.
 */
static ssize_t rt5648_index_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt5648_priv *rt5648 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt5648->codec;
	unsigned int val;
	int cnt = 0, i;

	cnt += sprintf(buf, "RT5648 index register\n");
	for (i = 0; i < 0xff; i++) {
		if (cnt + RT5648_REG_DISP_LEN >= PAGE_SIZE)
			break;
		val = rt5648_index_read(codec, i);
		if (!val)
			continue;
		cnt += snprintf(buf + cnt, RT5648_REG_DISP_LEN,
				"%02x: %04x\n", i, val);
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	return cnt;
}

static ssize_t rt5648_index_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt5648_priv *rt5648 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt5648->codec;
	unsigned int val=0,addr=0;
	int i;

	for (i = 0; i < count; i++) {
		if (*(buf + i) <= '9' && *(buf + i) >= '0')
			addr = (addr << 4) | (*(buf + i)-'0');
		else if (*(buf + i) <= 'f' && *(buf + i) >= 'a')
			addr = (addr << 4) | ((*(buf + i) - 'a') + 0xa);
		else if (*(buf + i) <= 'F' && *(buf + i) >= 'A')
			addr = (addr << 4) | ((*(buf + i)-'A') + 0xa);
		else
			break;
	}

	for (i = i + 1 ; i < count; i++) {
		if (*(buf + i) <= '9' && *(buf + i) >= '0')
			val = (val << 4) | (*(buf + i) - '0');
		else if (*(buf + i) <= 'f' && *(buf + i) >= 'a')
			val = (val << 4) | ((*(buf + i) - 'a') + 0xa);
		else if (*(buf + i) <= 'F' && *(buf + i) >= 'A')
			val = (val << 4) | ((*(buf + i) - 'A') + 0xa);
		else
			break;
	}
	pr_debug("addr=0x%x val=0x%x\n",addr,val);
	if (addr > RT5648_VENDOR_ID2 || val > 0xffff || val < 0)
		return count;

	if (i == count)
		pr_info("0x%02x = 0x%04x\n",addr,rt5648_index_read(codec, addr));
	else
		rt5648_index_write(codec, addr, val);


	return count;
}
static DEVICE_ATTR(index_reg, 0664, rt5648_index_show, rt5648_index_store);

static ssize_t rt5648_codec_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt5648_priv *rt5648 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt5648->codec;
	unsigned int val;
	int cnt = 0, i;

	for (i = 0; i <= RT5648_VENDOR_ID2; i++) {
		if (cnt + RT5648_REG_DISP_LEN >= PAGE_SIZE)
			break;
		val = snd_soc_read(codec, i);
		if (!val)
			continue;
		cnt += snprintf(buf + cnt, RT5648_REG_DISP_LEN,
				"#rng%02x  #rv%04x  #rd0\n", i, val);
	}

	if (cnt >= PAGE_SIZE)
		cnt = PAGE_SIZE - 1;

	return cnt;
}

static ssize_t rt5648_codec_store(struct device *dev,struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct rt5648_priv *rt5648 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt5648->codec;
	unsigned int val=0,addr=0;
	int i;

//	pr_debug("register \"%s\" count=%d\n",buf,count);
	for (i = 0; i < count; i++) {
		if (*(buf + i) <= '9' && *(buf + i) >= '0')
			addr = (addr << 4) | (*(buf + i) - '0');
		else if (*(buf + i) <= 'f' && *(buf + i) >= 'a')
			addr = (addr << 4) | ((*(buf + i)-'a') + 0xa);
		else if (*(buf + i) <= 'F' && *(buf + i) >= 'A')
			addr = (addr << 4) | ((*(buf + i)-'A') + 0xa);
		else
			break;
	}

	for (i = i + 1 ; i < count; i++) {
		if (*(buf + i) <= '9' && *(buf + i) >= '0')
			val = (val << 4) | (*(buf + i)-'0');
		else if (*(buf + i) <= 'f' && *(buf + i) >= 'a')
			val = (val << 4) | ((*(buf + i)-'a') + 0xa);
		else if (*(buf + i) <= 'F' && *(buf + i) >= 'A')
			val = (val << 4) | ((*(buf + i)-'A') + 0xa);
		else
			break;
	}
//	pr_debug("addr=0x%x val=0x%x\n",addr,val);
	if (addr > RT5648_VENDOR_ID2 || val > 0xffff || val < 0)
		return count;

	if (i == count)
		pr_info("0x%02x = 0x%04x\n", addr, snd_soc_read(codec, addr));
	else
		snd_soc_write(codec, addr, val);


	return count;
}

static DEVICE_ATTR(codec_reg, 0664, rt5648_codec_show, rt5648_codec_store);

static int rt5648_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		break;

	case SND_SOC_BIAS_STANDBY:
		if (SND_SOC_BIAS_OFF == snd_soc_codec_get_bias_level(codec)) {
			snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
				RT5648_PWR_VREF1 | RT5648_PWR_MB |
				RT5648_PWR_BG | RT5648_PWR_VREF2,
				RT5648_PWR_VREF1 | RT5648_PWR_MB |
				RT5648_PWR_BG | RT5648_PWR_VREF2);
			msleep(10);
			snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
				RT5648_PWR_FV1 | RT5648_PWR_FV2,
				RT5648_PWR_FV1 | RT5648_PWR_FV2);
			snd_soc_update_bits(codec, RT5648_DIG_MISC,
				RT5648_DIG_GATE_CTRL, RT5648_DIG_GATE_CTRL);
			snd_soc_cache_sync(codec);
			rt5648_index_sync(codec);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, RT5648_DEPOP_M2, 0x1100);
		snd_soc_update_bits(codec, RT5648_DIG_MISC,
				RT5648_DIG_GATE_CTRL, 0);
		snd_soc_write(codec, RT5648_PWR_DIG1, 0x0000);
		snd_soc_write(codec, RT5648_PWR_DIG2, 0x0000);
		snd_soc_write(codec, RT5648_PWR_VOL, 0x0000);
		snd_soc_write(codec, RT5648_PWR_MIXER, 0x0002);
		if (rt5648->jack_type == SND_JACK_HEADSET) {
			snd_soc_write(codec, RT5648_PWR_ANLG1, 0x2802);
			snd_soc_write(codec, RT5648_PWR_ANLG2, 0x0804);
		} else {
			snd_soc_write(codec, RT5648_PWR_ANLG1, 0x0000);
			snd_soc_write(codec, RT5648_PWR_ANLG2, 0x0000);
		}
		break;

	default:
		break;
	}
	snd_soc_codec_get_dapm(codec)->bias_level = level;

	return 0;
}

static int rt5648_probe(struct snd_soc_codec *codec)
{
	struct rt5648_priv *rt5648 = snd_soc_codec_get_drvdata(codec);
#ifdef RTK_IOCTL
#if defined(CONFIG_SND_HWDEP) || defined(CONFIG_SND_HWDEP_MODULE)
	struct rt_codec_ops *ioctl_ops = rt_codec_get_ioctl_ops();
#endif
#endif
	int ret;

	pr_info("Codec driver version %s\n", VERSION);

	snd_soc_codec_get_dapm(codec)->idle_bias_off = 1;

	rt5648_reset(codec);
	snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
		RT5648_PWR_VREF1 | RT5648_PWR_MB |
		RT5648_PWR_BG | RT5648_PWR_VREF2,
		RT5648_PWR_VREF1 | RT5648_PWR_MB |
		RT5648_PWR_BG | RT5648_PWR_VREF2);
	msleep(10);
	snd_soc_update_bits(codec, RT5648_PWR_ANLG1,
		RT5648_PWR_FV1 | RT5648_PWR_FV2,
		RT5648_PWR_FV1 | RT5648_PWR_FV2);

	snd_soc_update_bits(codec, RT5648_DIG_MISC,
				RT5648_DIG_GATE_CTRL, RT5648_DIG_GATE_CTRL);
	rt5648_reg_init(codec);

	snd_soc_update_bits(codec, RT5648_PWR_ANLG1, RT5648_LDO_SEL_MASK, 0x0);

  snd_soc_update_bits(codec, RT5648_GEN_CTRL3, 0x2, 0x2);

	/* dc_calibrate(codec); */
	snd_soc_codec_get_dapm(codec)->bias_level = SND_SOC_BIAS_STANDBY;
	rt5648->codec = codec;

	snd_soc_add_codec_controls(codec, rt5648_snd_controls,
			ARRAY_SIZE(rt5648_snd_controls));
	snd_soc_dapm_new_controls(snd_soc_codec_get_dapm(codec), rt5648_dapm_widgets,
			ARRAY_SIZE(rt5648_dapm_widgets));
	snd_soc_dapm_add_routes(snd_soc_codec_get_dapm(codec), rt5648_dapm_routes,
			ARRAY_SIZE(rt5648_dapm_routes));

#ifdef RTK_IOCTL
#if defined(CONFIG_SND_HWDEP) || defined(CONFIG_SND_HWDEP_MODULE)
	ioctl_ops->index_write = rt5648_index_write;
	ioctl_ops->index_read = rt5648_index_read;
	ioctl_ops->index_update_bits = rt5648_index_update_bits;
	ioctl_ops->ioctl_common = rt5648_ioctl_common;
	realtek_ce_init_hwdep(codec);
#endif
#endif
	/* Oder 140117 start */
	rt5648->eq_mode = SPK;
	/* Oder 140117 end */
	
	ret = device_create_file(codec->dev, &dev_attr_index_reg);
	if (ret != 0) {
		dev_err(codec->dev,
			"Failed to create index_reg sysfs files: %d\n", ret);
		return ret;
	}

	ret = device_create_file(codec->dev, &dev_attr_codec_reg);
	if (ret != 0) {
		dev_err(codec->dev,
			"Failed to create codex_reg sysfs files: %d\n", ret);
		return ret;
	}

	/* tf103cg FOR MIC agc function*/
	is_recording = 0;
	/* tf103cg FOR MIC agc function*/

	// ASUS_BSP : for ATD audio_codec_status
	ret = device_create_file(codec->dev, &dev_attr_audio_codec_status);
        if (ret < 0)
                pr_err("%s(): Failed to create audio_codec_status: %d\n", __func__, ret);
	ret_codec_status = 1;
	ret = device_create_file(codec->dev, &dev_attr_headset_status);
	if (ret < 0)
		pr_err("%s(): Failed to create headset_status: %d\n", __func__, ret);
	// ASUS_BSP

	return 0;
}

static int rt5648_remove(struct snd_soc_codec *codec)
{
	rt5648_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

#ifdef CONFIG_PM
static int rt5648_suspend(struct snd_soc_codec *codec)
{
	rt5648_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int rt5648_resume(struct snd_soc_codec *codec)
{
	rt5648_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define rt5648_suspend NULL
#define rt5648_resume NULL
#endif

#define RT5648_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define RT5648_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

struct snd_soc_dai_ops rt5648_aif_dai_ops = {
	.hw_params = rt5648_hw_params,
	.hw_free = rt5648_hw_free,
	.prepare = rt5648_prepare,
	.set_fmt = rt5648_set_dai_fmt,
	.set_sysclk = rt5648_set_dai_sysclk,
	.set_tdm_slot = rt5648_set_tdm_slot,
	.set_pll = rt5648_set_dai_pll,
};

struct snd_soc_dai_driver rt5648_dai[] = {
	{
		.name = "rt5648-aif1",
		.id = RT5648_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5648_STEREO_RATES,
			.formats = RT5648_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5648_STEREO_RATES,
			.formats = RT5648_FORMATS,
		},
		.ops = &rt5648_aif_dai_ops,
	},
	{
		.name = "rt5648-aif2",
		.id = RT5648_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5648_STEREO_RATES,
			.formats = RT5648_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5648_STEREO_RATES,
			.formats = RT5648_FORMATS,
		},
		.ops = &rt5648_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5648 = {
	.probe = rt5648_probe,
	.remove = rt5648_remove,
	.suspend = rt5648_suspend,
	.resume = rt5648_resume,
	.set_bias_level = rt5648_set_bias_level,
	.idle_bias_off = true,
	.controls = rt5648_snd_controls,
	.num_controls = ARRAY_SIZE(rt5648_snd_controls),
	.dapm_widgets = rt5648_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5648_dapm_widgets),
	.dapm_routes = rt5648_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5648_dapm_routes),
	/*.reg_cache_size = RT5648_VENDOR_ID2 + 1,
	 *.reg_word_size = sizeof(u16),
	 *.reg_cache_default = rt5648_reg,
	 *.volatile_register = rt5648_volatile_register,
	 *.readable_register = rt5648_readable_register,
	 *.reg_cache_step = 1,*/
};

static const struct i2c_device_id rt5648_i2c_id[] = {
	{ "10EC5648" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5648_i2c_id);

static int rt5648_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	pr_debug("rt5648_i2c_probe() called: i2c->name=%s id->name=%s\n", i2c->name, id->name);
	struct rt5648_priv *rt5648;
	int ret;

	rt5648 = kzalloc(sizeof(struct rt5648_priv), GFP_KERNEL);
	if (NULL == rt5648)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5648);

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5648,
			rt5648_dai, ARRAY_SIZE(rt5648_dai));
	pr_debug("snd_soc_register_codec returned %d\n", ret);
	if (ret < 0)
		kfree(rt5648);

	pr_debug("rt5648_i2c_probe() returned: %d\n", ret);
	return ret;
}

static int rt5648_i2c_remove(struct i2c_client *i2c)
{
	pr_debug("rt5648_i2c_remove() called: i2c->name=%s\n", i2c->name);
	snd_soc_unregister_codec(&i2c->dev);
	kfree(i2c_get_clientdata(i2c));
	pr_debug("rt5648_i2c_remove() returned: 0\n");
	return 0;
}

void rt5648_i2c_shutdown(struct i2c_client *client)
{
	pr_debug("rt5648_i2c_shutdown() called: client->name=%s\n", client->name);
	struct rt5648_priv *rt5648 = i2c_get_clientdata(client);
	struct snd_soc_codec *codec = rt5648->codec;

	pr_debug("codec=%p", codec);
	if (codec != NULL)
		rt5648_set_bias_level(codec, SND_SOC_BIAS_OFF);

	pr_debug("rt5648_i2c_shutdown() returned\n");
}

struct i2c_driver rt5648_i2c_driver = {
	.driver = {
		.name = "rt5648",
		.owner = THIS_MODULE,
	},
	.probe = rt5648_i2c_probe,
	.remove = rt5648_i2c_remove,
	.shutdown = rt5648_i2c_shutdown,
	.id_table = rt5648_i2c_id,
};

static int __init rt5648_modinit(void)
{
	pr_debug("Initializing RT5648 driver\n");
	return i2c_add_driver(&rt5648_i2c_driver);
	pr_debug("Initialized RT5648 driver\n");
}
module_init(rt5648_modinit);

static void __exit rt5648_modexit(void)
{
	pr_debug("Finalizing RT5648 driver\n");
	i2c_del_driver(&rt5648_i2c_driver);
	pr_debug("Finalized RT5648 driver\n");
}
module_exit(rt5648_modexit);

MODULE_DESCRIPTION("ASoC RT5648 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL");

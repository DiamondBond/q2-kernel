/*
 * es8374.c  --  ES8374 ALSA SoC Audio Codec
 *
 * Copyright (C) 2016 Everest Semiconductor Co., Ltd
 *
 * Authors:  XianqingZheng(xqzheng@ambarella.com)
 *
 *
 * Based on es8374.c by David Yang(yangxiaohua@everest-semi.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/stddef.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "es8374.h"

#define es8374_dmic
/*
 * es8374 register cache
 */
static struct reg_default  es8374_reg_defaults[] = {
	{   0x00, 0x03 },
	{   0x01, 0x03 },
	{   0x02, 0x00 },
	{   0x03, 0x20 },
	{   0x04, 0x00 },
	{   0x05, 0x11 },
	{   0x06, 0x01 },
	{   0x07, 0x00 },
	{   0x08, 0x20 },
	{   0x09, 0x80 },
	{   0x0a, 0x4A },
	{   0x0b, 0x00 },
	{   0x0c, 0x00 },
	{   0x0d, 0x00 },
	{   0x0e, 0x00 },
	{   0x0f, 0x00 },

	{   0x10, 0x00 },
	{   0x11, 0x00 },
	{   0x12, 0x40 },
	{   0x13, 0x40 },
	{   0x14, 0x9C },
	{   0x15, 0xBE },
	{   0x16, 0x00 },
	{   0x17, 0xA0 },
	{   0x18, 0xFC },
	{   0x19, 0x00 },
	{   0x1a, 0x18 },
	{   0x1b, 0x00 },
	{   0x1c, 0x10 },
	{   0x1d, 0x10 },
	{   0x1e, 0x00 },
	{   0x1f, 0x08 },

	{   0x20, 0x08 },
	{   0x21, 0xD4 },
	{   0x22, 0x00 },
	{   0x23, 0x00 },
	{   0x24, 0x18 },
	{   0x25, 0xC0 },
	{   0x26, 0x1C },
	{   0x27, 0x00 },
	{   0x28, 0xB0 },
	{   0x29, 0x32 },
	{   0x2a, 0x03 },
	{   0x2b, 0x00 },
	{   0x2c, 0x0D },
	{   0x2d, 0x06 },
	{   0x2e, 0x1F },
	{   0x2f, 0xF7 },

	{   0x30, 0xFD },
	{   0x31, 0xFF },
	{   0x32, 0x1F },
	{   0x33, 0xF7 },
	{   0x34, 0xFD },
	{   0x35, 0xFF },
	{   0x36, 0x04 },
	{   0x37, 0x01 },
	{   0x38, 0xC0 },
	{   0x39, 0x00 },
	{   0x3a, 0x02 },
	{   0x3b, 0x17 },
	{   0x3c, 0xFD },
	{   0x3d, 0xFF },
	{   0x3e, 0x07 },
	{   0x3f, 0xFD },

	{   0x40, 0xFF },
	{   0x41, 0x00 },
	{   0x42, 0xFF },
	{   0x43, 0xBB },
	{   0x44, 0xFF },
	{   0x45, 0x00 },
	{   0x46, 0x00 },
	{   0x47, 0x00 },
	{   0x48, 0x00 },
	{   0x49, 0x00 },
	{   0x4a, 0x00 },
	{   0x4b, 0x00 },
	{   0x4c, 0x00 },
	{   0x4d, 0x00 },
	{   0x4e, 0x00 },
	{   0x4f, 0x00 },

	{   0x50, 0x00 },
	{   0x51, 0x00 },
	{   0x52, 0x00 },
	{   0x53, 0x00 },
	{   0x54, 0x00 },
	{   0x55, 0x00 },
	{   0x56, 0x00 },
	{   0x57, 0x00 },
	{   0x58, 0x00 },
	{   0x59, 0x00 },
	{   0x5a, 0x00 },
	{   0x5b, 0x00 },
	{   0x5c, 0x00 },
	{   0x5d, 0x00 },
	{   0x5e, 0x00 },
	{   0x5f, 0x00 },

	{   0x60, 0x00 },
	{   0x61, 0x00 },
	{   0x62, 0x00 },
	{   0x63, 0x00 },
	{   0x64, 0x00 },
	{   0x65, 0x00 },
	{   0x66, 0x00 },
	{   0x67, 0x00 },
	{   0x68, 0x00 },
	{   0x69, 0x00 },
	{   0x6a, 0x00 },
	{   0x6b, 0x00 },
	{   0x6c, 0x00 },
	{   0x6d, 0x00 },
	{   0x6e, 0x00 },
	{   0x6f, 0x00 },

};

/* codec private data */
struct	es8374_private {
	struct snd_soc_component *component;
	struct regmap *regmap;
	struct clk *mclk;
	u32 clk_id;
	uint sysclk;
	/* platform dependent DVDD voltage configuration */
	u8  dvdd_pwr_vol;
	u8  pll_div;
  	bool dmic_enable;
};

struct es8374_private *es8374_data;
struct snd_soc_component *es8374_codec;

static bool es8374_volatile_register(struct device *dev,
			unsigned int reg)
{
	if ((reg  < 0x80)) {
		if ((reg != 0x19) && (reg != 0x23))
			return true;
		else
			return false;
	} else {
			return false;
	}
}

static bool es8374_readable_register(struct device *dev,
			unsigned int reg)
{
	if ((reg  < 0x80)) {
		if ((reg != 0x19) && (reg != 0x23))
			return true;
		else
			return false;
	} else {
			return false;
	}
}
static bool es8374_writable_register(struct device *dev,
			unsigned int reg)
{
	if ((reg  < 0x80)) {
		if ((reg != 0x19) && (reg != 0x23))
			return true;
		else
			return false;
	} else {
			return false;
	}
}

/*
*	Define ADC and DAC Volume
*/
static const DECLARE_TLV_DB_SCALE(vdac_tlv, 0, 50, 0);
static const DECLARE_TLV_DB_SCALE(adc_rec_tlv, 0, 50, 0);
/*
*	Define D2SE MIC BOOST GAIN
*/
static const DECLARE_TLV_DB_SCALE(mic_boost_tlv, 0, 1500, 0);
/*
*	Define LINE PGA GAIN
*/
static const DECLARE_TLV_DB_SCALE(linin_pga_tlv, -300, 300, 0);
/*
*	Define dmic boost gain
*/
static const DECLARE_TLV_DB_SCALE(dmic_6db_scaleup_tlv, 0, 600, 0);
/*
 *	Define ALC noise gate type
*/
static const char * const ng_type_txt[] = {"Constant PGA Gain",
	"Mute ADC Output"};
static const struct soc_enum ng_type =
SOC_ENUM_SINGLE(ES8374_ALC_NGTH_REG2B, 6, 2, ng_type_txt);
/*
*	Define MONO output gain
*/
static const DECLARE_TLV_DB_SCALE(mono_out_gain_tlv, 0, 150, 0);
/*
 *	Define dac auto mute type
*/
static const char * const dac_auto_mute_type_txt[] = {
	"AUTO MUTE DISABLE",
	"MONO OUTPUT MUTE",
	"SPEAKER MUTE",
	"MONO OUT & SPEAKER MUTE"
	};
static const struct soc_enum dac_auto_mute_type =
SOC_ENUM_SINGLE(ES8374_DAC_CONTROL_REG37, 4, 4, dac_auto_mute_type_txt);
/*
 *	Definition dac dsm mute type
*/
static const char * const dac_dsm_mute_type_txt[] = {
	"DAC DSM UNMUTE",
	"DAC DSM MUTE",
	};
static const struct soc_enum dac_dsm_mute_type =
SOC_ENUM_SINGLE(ES8374_DAC_CONTROL_REG37, 0, 2, dac_dsm_mute_type_txt);
/*
 * es8374 Controls
 */
static const struct snd_kcontrol_new es8374_snd_controls[] = {
	/*
	* controls for capture path
	*/
	SOC_SINGLE_TLV("D2SE MIC BOOST GAIN",
					ES8374_AIN_PWR_SRC_REG21, 2, 1, 0, mic_boost_tlv),
	SOC_SINGLE_TLV("LIN PGA GAIN",//write only
					ES8374_AIN_PGA_REG22, 0, 15, 0, linin_pga_tlv),
	SOC_SINGLE_TLV("LIN PGA GAIN READ ONLY",
					ES8374_AIN_PGA_REG22, 4, 15, 0, linin_pga_tlv),
	SOC_SINGLE_TLV("DMIC 6DB SCALE UP GAIN",
					ES8374_ADC_CONTROL_REG24, 7, 1, 0, dmic_6db_scaleup_tlv),
	SOC_SINGLE("ADC Double FS Mode", ES8374_ADC_CONTROL_REG24, 6, 1, 0),
	SOC_SINGLE("ADC Soft Ramp", ES8374_ADC_CONTROL_REG24, 4, 1, 0),
	SOC_SINGLE("ADC MUTE", ES8374_ADC_CONTROL_REG24, 5, 1, 0),
	SOC_SINGLE("ADC INVERTED", ES8374_ADC_CONTROL_REG24, 2, 1, 0),
	SOC_SINGLE("ADC HPF COEFFICIENT", ES8374_ADC_HPF_REG2C, 0, 31, 0),
	SOC_SINGLE_TLV("ADC Capture Volume",
				ES8374_ADC_VOLUME_REG25, 0, 192, 1, adc_rec_tlv),
	SOC_SINGLE("ALC Capture Target Volume", ES8374_ALC_LVL_HLD_REG28, 4, 15, 0),
	SOC_SINGLE("ALC Capture Max PGA", ES8374_ALC_EN_MAX_GAIN_REG26, 0, 31, 0),
	SOC_SINGLE("ALC Capture Min PGA", ES8374_ALC_MIN_GAIN_REG27, 0, 31, 0),
//	SOC_ENUM("ALC Capture Function", alc_func),
	SOC_SINGLE("ALC Capture Hold Time", ES8374_ALC_LVL_HLD_REG28, 0, 15, 0),
	SOC_SINGLE("ALC Capture Decay Time", ES8374_ALC_DCY_ATK_REG29, 4, 15, 0),
	SOC_SINGLE("ALC Capture Attack Time", ES8374_ALC_DCY_ATK_REG29, 0, 15, 0),
	SOC_SINGLE("ALC Capture NG Threshold", ES8374_ALC_NGTH_REG2B, 0, 31, 0),
	SOC_ENUM("ALC Capture NG Type", ng_type),
	SOC_SINGLE("ALC Capture NG Switch", ES8374_ALC_NGTH_REG2B, 5, 1, 0),

	/*
	* controls for playback path
	*/
	SOC_SINGLE("DAC Double FS Mode", ES8374_DAC_CONTROL_REG37, 7, 1, 0),
	SOC_SINGLE("DAC Soft Ramp Rate", ES8374_DAC_CONTROL_REG36, 2, 7, 0),
	SOC_SINGLE("DAC MUTE", ES8374_DAC_CONTROL_REG36, 5, 1, 0),
	SOC_SINGLE("DAC OFFSET", ES8374_DAC_OFFSET_REG39, 0, 255, 0),
	SOC_ENUM("DAC AUTO MUTE TYPE", dac_auto_mute_type),
	SOC_ENUM("DAC DSM MUTE TYPE", dac_dsm_mute_type),

	SOC_SINGLE_TLV("DAC Playback Volume",
					ES8374_DAC_VOLUME_REG38, 0, 192, 1, vdac_tlv),
	SOC_SINGLE_TLV("MONO OUT GAIN",
					ES8374_MONO_GAIN_REG1B, 0, 15, 0, mono_out_gain_tlv),
	SOC_SINGLE_TLV("SPEAKER MIXER GAIN",
					ES8374_SPK_MIX_GAIN_REG1D, 0, 15, 0, mono_out_gain_tlv),
	SOC_SINGLE_TLV("SPEAKER OUTPUT Volume",
					ES8374_SPK_OUT_GAIN_REG1E, 0, 7, 0, mono_out_gain_tlv),
};

/*
 * DAPM Controls
 */
/*
* alc on/off
*/
static const char * const es8374_alc_enable_txt[] = {
	"ALC OFF",
	"ADC ALC ON",
	"DAC ALC ON",
	"DAC ALC on",
};
static const unsigned int es8374_alc_enable_values[] = {
	0, 1, 2, 3};
static const struct soc_enum es8374_alc_enable_enum =
SOC_VALUE_ENUM_SINGLE(ES8374_ALC_EN_MAX_GAIN_REG26, 6, 3,
			ARRAY_SIZE(es8374_alc_enable_txt),
			es8374_alc_enable_txt,
			es8374_alc_enable_values);
static const struct snd_kcontrol_new es8374_alc_enable_controls =
	SOC_DAPM_ENUM("Route", es8374_alc_enable_enum);
/*
* adc line in select
*/
static const char * const es8374_adc_input_src_txt[] = {
	"NO IN ",
	"LIN1-RIN1",
	"LIN2-RIN2",
	"LIN1-RIN1&LIN2-RIN2",
};
static const unsigned int es8374_adc_input_src_values[] = {
	0, 1, 2, 3};
static const struct soc_enum es8374_adc_input_src_enum =
SOC_VALUE_ENUM_SINGLE(ES8374_AIN_PWR_SRC_REG21, 4, 3,
			ARRAY_SIZE(es8374_adc_input_src_txt),
			es8374_adc_input_src_txt,
			es8374_adc_input_src_values);
static const struct snd_kcontrol_new es8374_adc_input_src_controls =
	SOC_DAPM_ENUM("Route", es8374_adc_input_src_enum);

/*
 * ANALOG IN MUX
 */
static const char * const es8374_analog_input_mux_txt[] = {
	"LIN1",
	"LIN2",
	"DIFF OUT1",
	"DIFF OUT2",
	"PGA OUT1",
	"PGA OUT2"
};
static const unsigned int es8374_analog_input_mux_values[] = {
	0, 1, 2, 3, 4, 5};
static const struct soc_enum es8374_analog_input_mux_enum =
SOC_VALUE_ENUM_SINGLE(ES8374_MONO_MIX_REG1A, 0, 5,
			ARRAY_SIZE(es8374_analog_input_mux_txt),
			es8374_analog_input_mux_txt,
			es8374_analog_input_mux_values);
static const struct snd_kcontrol_new es8374_analog_input_mux_controls =
SOC_DAPM_ENUM("Route", es8374_analog_input_mux_enum);
/*
 * MONO OUTPUT MIXER
 */
static const struct snd_kcontrol_new es8374_mono_out_mixer_controls[] = {
	SOC_DAPM_SINGLE("LIN TO MONO OUT Switch", ES8374_MONO_MIX_REG1A, 6, 1, 0),
	SOC_DAPM_SINGLE("DAC TO MONO OUT Switch", ES8374_MONO_MIX_REG1A, 7, 1, 0),
};
/*
 * SPEAKER OUTPUT MIXER
 */
static const struct snd_kcontrol_new es8374_speaker_mixer_controls[] = {
	SOC_DAPM_SINGLE("LIN TO SPEAKER OUT Switch", ES8374_SPK_MIX_REG1C, 6, 1, 0),
	SOC_DAPM_SINGLE("DAC TO SPEAKER OUT Switch", ES8374_SPK_MIX_REG1C, 7, 1, 0),
};
/*
 * digital microphone soure
 */
static const char * const es8374_dmic_mux_txt[] = {
		"DMIC DISABLE1",
		"DMIC DISABLE2",
		"DMIC AT HIGH LEVEL",
		"DMIC AT LOW LEVEL",
};
static const unsigned int es8374_dmic_mux_values[] = {
		0, 1, 2, 3};
static const struct soc_enum es8374_dmic_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8374_ADC_CONTROL_REG24, 0, 3,
				ARRAY_SIZE(es8374_dmic_mux_txt),
				es8374_dmic_mux_txt,
				es8374_dmic_mux_values);
static const struct snd_kcontrol_new es8374_dmic_mux_controls =
	SOC_DAPM_ENUM("Route", es8374_dmic_mux_enum);
/*
 * ADC sdp  soure
 */
static const char * const es8374_adc_sdp_mux_txt[] = {
		"FROM ADC OUT",
		"FROM EQUALIZER",
};
static const unsigned int es8374_adc_sdp_mux_values[] = {
		0, 1};
static const struct soc_enum es8374_adc_sdp_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8374_EQ_SRC_REG2D, 7, 1,
				ARRAY_SIZE(es8374_adc_sdp_mux_txt),
				es8374_adc_sdp_mux_txt,
				es8374_adc_sdp_mux_values);
static const struct snd_kcontrol_new es8374_adc_sdp_mux_controls =
	SOC_DAPM_ENUM("Route", es8374_adc_sdp_mux_enum);

/*
 * DAC dsm  soure
 */
static const char * const es8374_dac_dsm_mux_txt[] = {
		"FROM SDP IN",
		"FROM EQUALIZER",
};
static const unsigned int  es8374_dac_dsm_mux_values[] = {
		0, 1};
static const struct soc_enum  es8374_dac_dsm_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8374_EQ_SRC_REG2D, 6, 1,
				ARRAY_SIZE(es8374_dac_dsm_mux_txt),
				es8374_dac_dsm_mux_txt,
				es8374_dac_dsm_mux_values);
static const struct snd_kcontrol_new  es8374_dac_dsm_mux_controls =
	SOC_DAPM_ENUM("Route", es8374_dac_dsm_mux_enum);
/*
 * equalizer data  soure
 */
static const char * const es8374_equalizer_src_mux_txt[] = {
		"FROM ADC OUT",
		"FROM SDP IN",
};
static const unsigned int  es8374_equalizer_src_mux_values[] = {
		0, 1};
static const struct soc_enum  es8374_equalizer_src_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8374_EQ_SRC_REG2D, 5, 1,
				ARRAY_SIZE(es8374_equalizer_src_mux_txt),
				es8374_equalizer_src_mux_txt,
				es8374_equalizer_src_mux_values);
static const struct snd_kcontrol_new  es8374_equalizer_src_mux_controls =
	SOC_DAPM_ENUM("Route", es8374_equalizer_src_mux_enum);
/*
 * DAC data  soure
 */
static const char * const es8374_dac_data_mux_txt[] = {
		"SELECT SDP LEFT DATA",
		"SELECT SDP RIGHT DATA",
};
static const unsigned int  es8374_dac_data_mux_values[] = {
		0, 1};
static const struct soc_enum  es8374_dac_data_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8374_DAC_CONTROL_REG36, 6, 1,
				ARRAY_SIZE(es8374_dac_data_mux_txt),
				es8374_dac_data_mux_txt,
				es8374_dac_data_mux_values);
static const struct snd_kcontrol_new  es8374_dac_data_mux_controls =
	SOC_DAPM_ENUM("Route", es8374_dac_data_mux_enum);

static const struct snd_soc_dapm_widget es8374_dapm_widgets[] = {
	/* Input Lines */
		SND_SOC_DAPM_INPUT("DMIC"),
		SND_SOC_DAPM_INPUT("MIC1"),
		SND_SOC_DAPM_INPUT("MIC2"),
		SND_SOC_DAPM_INPUT("LIN1"),
		SND_SOC_DAPM_INPUT("LIN2"),

	/*
	*  Capture path
	*/
		SND_SOC_DAPM_MICBIAS("micbias", ES8374_ANA_REF_REG14,
				4, 1),

		/* Input MUX */
		SND_SOC_DAPM_MUX("DIFFERENTIAL MUX", SND_SOC_NOPM, 0, 0,
				&es8374_adc_input_src_controls),

		SND_SOC_DAPM_PGA("DIFFERENTIAL PGA", SND_SOC_NOPM,
				0, 0, NULL, 0),

		SND_SOC_DAPM_PGA("LINE PGA", SND_SOC_NOPM,
				0, 0, NULL, 0),

		/* ADCs */
		SND_SOC_DAPM_ADC("MONO ADC", NULL, SND_SOC_NOPM, 0, 0),

		/* Dmic MUX */
		SND_SOC_DAPM_MUX("DMIC MUX", SND_SOC_NOPM, 0, 0,
				&es8374_dmic_mux_controls),

		/* Dmic MUX */
		SND_SOC_DAPM_MUX("ALC MUX", SND_SOC_NOPM, 0, 0,
			&es8374_alc_enable_controls),


		/* sdp MUX */
		SND_SOC_DAPM_MUX("SDP OUT MUX", SND_SOC_NOPM, 0, 0,
			&es8374_adc_sdp_mux_controls),

		/* Digital Interface */
	SND_SOC_DAPM_AIF_OUT("I2S OUT", "I2S1 Capture", 1,
				SND_SOC_NOPM, 0, 0),

		/*
		*	Render path
		*/
		SND_SOC_DAPM_AIF_IN("I2S IN", "I2S1 Playback", 0,
				SND_SOC_NOPM, 0, 0),

		/*	DACs SDP DATA SRC MUX */
		SND_SOC_DAPM_MUX("DAC SDP SRC MUX", SND_SOC_NOPM, 0, 0,
				&es8374_dac_data_mux_controls),

		/*	DACs  DATA SRC MUX */
		SND_SOC_DAPM_MUX("DAC SRC MUX", SND_SOC_NOPM, 0, 0,
			&es8374_dac_dsm_mux_controls),

		SND_SOC_DAPM_DAC("MONO DAC", NULL, SND_SOC_NOPM, 0, 0),


		/* hpmux for hp mixer */
		SND_SOC_DAPM_MUX("ANALOG INPUT MUX", SND_SOC_NOPM, 0, 0,
				&es8374_analog_input_mux_controls),

		/* Output mixer  */
		SND_SOC_DAPM_MIXER("MONO MIXER", SND_SOC_NOPM,
			0, 0, &es8374_mono_out_mixer_controls[0],
			ARRAY_SIZE(es8374_mono_out_mixer_controls)),
		SND_SOC_DAPM_MIXER("SPEAKER MIXER", SND_SOC_NOPM,
			0, 0, &es8374_speaker_mixer_controls[0],
			ARRAY_SIZE(es8374_speaker_mixer_controls)),

		/*
		*	Equalizer path
		*/
		SND_SOC_DAPM_MUX("EQUALIZER MUX", SND_SOC_NOPM, 0, 0,
				&es8374_equalizer_src_mux_controls),


		/* Output Lines */
		SND_SOC_DAPM_OUTPUT("MOUT"),
		SND_SOC_DAPM_OUTPUT("SPKOUT"),

};


static const struct snd_soc_dapm_route es8374_dapm_routes[] = {
	/*
	 * record route map
	 */

	{"DIFFERENTIAL MUX", "LIN1-RIN1", "MIC1"},
	{"DIFFERENTIAL MUX", "LIN2-RIN2", "MIC2"},

	{"DIFFERENTIAL PGA", NULL, "DIFFERENTIAL MUX"},

	{"LINE PGA", NULL, "DIFFERENTIAL PGA"},

	{"MONO ADC", NULL, "LINE PGA"},

	{"DMIC MUX", "DMIC DISABLE1", "MONO ADC"},
	{"DMIC MUX", "DMIC DISABLE2", "MONO ADC"},
	{"DMIC MUX", "DMIC AT HIGH LEVEL", "DMIC"},
	{"DMIC MUX", "DMIC AT LOW LEVEL", "DMIC"},


	{"ALC MUX", "ALC OFF", "DMIC MUX"},
	{"ALC MUX", "DAC ALC ON", "DMIC MUX"},

	/*
	* Equalizer path
	*/
	{"EQUALIZER MUX", "FROM ADC OUT", "ALC MUX"},
	{"EQUALIZER MUX", "FROM SDP IN", "I2S IN"},

	{"SDP OUT MUX", "FROM ADC OUT", "ALC MUX"},
	{"SDP OUT MUX", "FROM EQUALIZER", "EQUALIZER MUX"},

	{"I2S OUT", NULL, "SDP OUT MUX"},
	/*
	 * playback route map
	 */
	{"DAC SDP SRC MUX", "SELECT SDP LEFT DATA", "I2S IN"},
	{"DAC SDP SRC MUX", "SELECT SDP RIGHT DATA", "I2S IN"},

	{"DAC SRC MUX", "FROM SDP IN", "DAC SDP SRC MUX"},
	{"DAC SRC MUX", "FROM EQUALIZER", "EQUALIZER MUX"},

	{"MONO DAC", NULL, "DAC SRC MUX"},

	{"ANALOG INPUT MUX", "LIN1", "LIN1"},
	{"ANALOG INPUT MUX", "LIN2", "LIN2"},
	{"ANALOG INPUT MUX", "DIFF OUT1", "DIFFERENTIAL MUX"},
	{"ANALOG INPUT MUX", "DIFF OUT2", "DIFFERENTIAL PGA"},
	{"ANALOG INPUT MUX", "PGA OUT1", "LINE PGA"},
	{"ANALOG INPUT MUX", "PGA OUT2", "LINE PGA"},

	{"MONO MIXER", "LIN TO MONO OUT Switch", "ANALOG INPUT MUX"},
	{"MONO MIXER", "DAC TO MONO OUT Switch", "MONO DAC"},

	{"SPEAKER MIXER", "LIN TO SPEAKER OUT Switch", "ANALOG INPUT MUX"},
	{"SPEAKER MIXER", "DAC TO SPEAKER OUT Switch", "MONO DAC"},

	{"MOUT", NULL, "MONO MIXER"},
	{"SPKOUT", NULL, "SPEAKER MIXER"},

};



struct _coeff_div {
	u32 mclk;       /* mclk frequency */
	u32 rate;       /* sample rate */
	u8 div;	 /* adcclk and dacclk divider */
	u8 fsmode;	 /* adcclk and dacclk divider */
	u8 divdouble;	 /* adcclk and dacclk divider */
	u8 lrck_h;      /* adclrck divider and daclrck divider */
	u8 lrck_l;
	u8 sr;	  /* sclk divider */
	u8 osr;	 /* adc osr */
};


/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {

//	MCLK	, LRCK,DIV,FSMODE,divDOUBLE,LRCK-H,LRCK-L,BCLK,OSR
	/* 8k */
	{12288000, 8000, 6, 0, 0, 0x06, 0x00, 2, 32},
	{12000000, 8000, 15, 0, 1, 0x05, 0xDC, 2, 25},
	{11289600, 8000, 6, 0, 0, 0x05, 0x83, 20, 29},
	{18432000, 8000, 9, 0, 0, 0x09, 0x00, 27, 32},
	{16934400, 8000, 8, 0, 0, 0x08, 0x44, 25, 33},
	{12000000, 8000, 7, 0, 0, 0x05, 0xdc, 21, 25},
	{19200000, 8000, 12, 0, 0, 0x09, 0x60, 27, 25},

	/* 11.025k */
	{11289600, 11025, 4, 0, 0, 0x04, 0x00, 2, 32},
	{12000000, 11025, 4, 0, 0, 0x04, 0x40, 2, 34},
	{16934400, 11025, 6, 0, 0, 0x06, 0x00, 21, 32},


	/* 12k */
	{12000000, 12000, 5, 0, 0, 0x03, 0xE8, 2, 25},
	{12288000, 12000, 4, 0, 0, 0x04, 0x00, 2, 32},

	/* 16k */
	{12288000, 16000, 3, 0, 0, 0x03, 0x00, 2, 32},
	{18432000, 16000, 5, 0, 0, 0x04, 0x80, 18, 25},
	{12000000, 16000, 15, 1, 1, 0x02, 0xEE, 2, 25},
	{19200000, 16000, 6, 0, 0, 0x04, 0xb0, 18, 25},

	/* 22.05k */
	{11289600, 22050, 2, 0, 0, 0x02, 0x00, 2, 32},
	{16934400, 22050, 3, 0, 0, 0x03, 0x00, 12, 32},
	{12000000, 22050, 2, 0, 0, 0x02, 0x20, 2, 34},

	/* 24k */
	{12000000, 24000, 5, 1, 0, 0x02, 0x00, 2, 25},
	{12288000, 24000, 2, 0, 0, 0x02, 0x00, 2, 32},

	/* 32k */
	{12288000, 32000, 3, 0, 1, 0x01, 0x80, 2, 32},
	{18432000, 32000, 2, 0, 0, 0x02, 0x40, 9, 32},
	{12000000, 32000, 3, 1, 0, 0x01, 0x77, 2, 31},
	{19200000, 32000, 3, 0, 0, 0x02, 0x58, 10, 25},

	/* 44.1k */
	{11289600, 44100, 1, 0, 0, 0x01, 0x00, 2, 32},
	{16934400, 44100, 1, 0, 0, 0x01, 0x80, 6, 32},
	{12000000, 44100, 1, 0, 0, 0x01, 0x10, 2, 34},

	/* 48k */
	{12288000, 48000, 1, 0, 0, 0x01, 0x00, 2, 32},
	{18432000, 48000, 1, 0, 0, 0x01, 0x80, 6, 32},
	{12000000, 48000, 5, 1, 1, 0x00, 0xFA, 2, 25},
	{19200000, 48000, 2, 0, 0, 0x01, 0x90, 6, 25},

	/* 64k */
	{12288000, 64000, 3, 1, 1, 0x00, 0xC0, 2, 32},

	/* 88.2k */
	{11289600, 88200, 1, 1, 0, 0x00, 0x80, 2, 32},
	{16934400, 88200, 1, 0, 0, 0x00, 0xc0, 3, 48},
	{12000000, 88200, 1, 1, 0, 0x00, 0x88, 2, 34},

	/* 96k */
	{12288000, 96000, 1, 1, 0, 0x00, 0x80, 2, 32},
	{18432000, 96000, 1, 0, 0, 0x00, 0xc0, 3, 48},
	{12000000, 96000, 1, 1, 0, 0x00, 0x7D, 2, 31},
	{19200000, 96000, 1, 0, 0, 0x00, 0xc8, 3, 25},
};
static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}

	//return 28;
	return -EINVAL;
}


/*
 * if PLL not be used, use internal clk1 for mclk,otherwise, use internal clk2 for PLL source.
 */
static int es8374_set_dai_sysclk(struct snd_soc_dai *dai,
			int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	//struct es8374_priv *es8374 = snd_soc_component_get_drvdata(codec);
	if (clk_id == ES8374_CLKID_MCLK) {
		snd_soc_component_write(component, 0x01, 0x7F);	//IC clk on
		snd_soc_component_write(component, 0x05, 0x11);	//clk div set
		snd_soc_component_write(component, 0x09, 0x01);	//pll set:reset on ,set start
		snd_soc_component_write(component, 0x0C, 0x22);	//pll set:k
		snd_soc_component_write(component, 0x0D, 0x2E);	//pll set:k
		snd_soc_component_write(component, 0x0E, 0xC6);	//pll set:k
	}
	return 0;
}

static int es8374_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u8 iface = 0;
	u8 adciface = 0;
	u8 daciface = 0;

	printk("Enter into %s()\n", __func__);
	iface    = snd_soc_component_read(component, ES8374_MS_BCKDIV_REG0F);
	adciface = snd_soc_component_read(component, ES8374_ADC_FMT_REG10);
	daciface = snd_soc_component_read(component, ES8374_DAC_FMT_REG11);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:    /* MASTER MODE */
		printk("ES8374 in Master mode\n");
		iface |= 0x80;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:    /* SLAVE MODE */
		printk("ES8374 in Slave mode\n");
		iface &= 0x7F;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		printk("ES8374 in I2S Format\n");
		adciface &= 0xFC;
		daciface &= 0xFC;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		return -EINVAL;
	case SND_SOC_DAIFMT_LEFT_J:
		printk("ES8374 in LJ Format\n");
		adciface &= 0xFC;
		daciface &= 0xFC;
		adciface |= 0x01;
		daciface |= 0x01;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		printk("ES8374 in DSP-A Format\n");
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x03;
		daciface |= 0x03;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		printk("ES8374 in DSP-B Format\n");
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x23;
		daciface |= 0x23;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	if (((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S) ||
		((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_LEFT_J)) {
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			iface    &= 0xDF;
			adciface &= 0xDF;
			daciface &= 0xDF;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			iface    |= 0x20;
			adciface |= 0x20;
			daciface |= 0x20;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			iface    |= 0x20;
			adciface &= 0xDF;
			daciface &= 0xDF;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			iface    &= 0xDF;
			adciface |= 0x20;
			daciface |= 0x20;
			break;
		default:
			return -EINVAL;
		}
	}
	snd_soc_component_write(component, ES8374_MS_BCKDIV_REG0F, iface);
	snd_soc_component_write(component, ES8374_ADC_FMT_REG10, adciface);
	snd_soc_component_write(component, ES8374_DAC_FMT_REG11, daciface);
	return 0;
}
static int es8374_pcm_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	return 0;
}

static int es8374_pcm_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8374_private *es8374 = snd_soc_component_get_drvdata(component);
	u16 iface;

	printk("Enter into %s()\n", __func__);
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		iface = snd_soc_component_read(component, ES8374_DAC_FMT_REG11) & 0xE3;
		/* bit size */
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			iface |= 0x0c;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			iface |= 0x04;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			iface |= 0x10;
			break;
		}
		/* set iface & srate */
		snd_soc_component_write(component, ES8374_DAC_FMT_REG11, iface);
	} else {
		iface = snd_soc_component_read(component, ES8374_ADC_FMT_REG10) & 0xE3;
		/* bit size */
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			iface |= 0x0c;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			iface |= 0x04;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			iface |= 0x10;
			break;
		}
		/* set iface */
		snd_soc_component_write(component, ES8374_ADC_FMT_REG10, iface);
	}

	if(es8374->dmic_enable){
		printk("dmic is enabled\n");
		snd_soc_component_write(component, ES8374_GPIO_INSERT_REG6D, 0x5F);  //set gpio1 to DMIC CLK
	} else {
		printk("dmic is not enabled\n");
		snd_soc_component_write(component, ES8374_GPIO_INSERT_REG6D, 0x1F);  //set gpio1 to GM SHORT
	}

/*
*	please add the hardware clock divider setting to get the correct LRCK, SCLK frequency
*/
	return 0;
}

static int es8374_set_bias_level(struct snd_soc_component *component,
			enum snd_soc_bias_level level)
{
//	u8 value;
//	struct es8374_private *es8374 = snd_soc_component_get_drvdata(codec);
	//dev_dbg(codec->dev, "es8374_set_bias_level.= 0x%x.......\n", codec->dapm.bias_level);
	dev_dbg(component->dev, "level = %d\n", level);
	printk("Enter into %s, level = %d\n", __func__, level);
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_write(component, ES8374_CLK_MANAGEMENT_REG01, 0x7f);
		snd_soc_component_write(component, ES8374_ANA_REF_REG14, 0x8a);
		snd_soc_component_write(component, ES8374_ANA_PWR_CTL_REG15, 0x40);
		snd_soc_component_write(component, ES8374_AIN_PWR_SRC_REG21, 0x14);
		snd_soc_component_write(component, ES8374_MONO_MIX_REG1A, 0xa0);
		snd_soc_component_write(component, ES8374_SPK_MIX_REG1C, 0x90);
		snd_soc_component_write(component, ES8374_SPK_MIX_GAIN_REG1D, 0x02);
		snd_soc_component_write(component, ES8374_SPK_OUT_GAIN_REG1E, 0xa0);
		snd_soc_component_write(component, ES8374_DAC_CONTROL_REG36, 0x00);
		snd_soc_component_write(component, ES8374_DAC_CONTROL_REG37, 0x00);
		snd_soc_component_write(component, ES8374_DAC_VOLUME_REG38, 0x00);
		snd_soc_component_write(component, ES8374_ADC_VOLUME_REG25, 0x00);
		break;
	
	case SND_SOC_BIAS_STANDBY:
		snd_soc_component_write(component, ES8374_DAC_VOLUME_REG38, 0xc0);
		//snd_soc_component_write(codec, ES8374_ADC_VOLUME_REG25, 0xc0);
		snd_soc_component_write(component, ES8374_DAC_CONTROL_REG36, 0x20);
		snd_soc_component_write(component, ES8374_DAC_CONTROL_REG37, 0x21);
		snd_soc_component_write(component, ES8374_MONO_MIX_REG1A, 0x08);
		snd_soc_component_write(component, ES8374_SPK_MIX_REG1C, 0x10);
		snd_soc_component_write(component, ES8374_SPK_MIX_GAIN_REG1D, 0x10);
		snd_soc_component_write(component, ES8374_SPK_OUT_GAIN_REG1E, 0x40);
		break;

	case SND_SOC_BIAS_OFF:

		snd_soc_component_write(component, ES8374_ALC_LVL_HLD_REG28, 0x1c);
		snd_soc_component_update_bits(component, ES8374_AIN_PWR_SRC_REG21, 0xc0, 0x00);
		snd_soc_component_write(component, ES8374_ANA_PWR_CTL_REG15, 0xbf);
		snd_soc_component_write(component, ES8374_ANA_REF_REG14, 0x14);
		snd_soc_component_write(component, ES8374_CLK_MANAGEMENT_REG01, 0x03);
		break;
	}

	//codec->dapm.bias_level = level;
	return 0;
}

#define es8374_RATES SNDRV_PCM_RATE_8000_96000

#define es8374_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
		SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops es8374_ops = {
	.startup = es8374_pcm_startup,
	.hw_params = es8374_pcm_hw_params,
	.set_fmt = es8374_set_dai_fmt,
	.set_sysclk = es8374_set_dai_sysclk,
};

static struct snd_soc_dai_driver es8374_dai[] = {
	{	.name = "ES8374 HiFi",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = es8374_RATES,
			.formats = es8374_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = es8374_RATES,
			.formats = es8374_FORMATS,
		},
		.ops = &es8374_ops,
		.symmetric_rates = 1,
	},
};

static int es8374_suspend(struct snd_soc_component *component)
{
	printk("=======enter func %s,line %d\n",__func__,__LINE__);
	snd_soc_component_write(component, ES8374_DAC_VOLUME_REG38, 0xc0);
	snd_soc_component_write(component, ES8374_ADC_VOLUME_REG25, 0xc0);
	snd_soc_component_write(component, ES8374_DAC_CONTROL_REG36, 0x20);
	snd_soc_component_write(component, ES8374_DAC_CONTROL_REG37, 0x21);
	snd_soc_component_write(component, ES8374_MONO_MIX_REG1A, 0x08);
	snd_soc_component_write(component, ES8374_SPK_OUT_GAIN_REG1E, 0x20);
	mdelay(5);
	snd_soc_component_write(component, ES8374_SPK_MIX_REG1C, 0x10);
	snd_soc_component_write(component, ES8374_SPK_MIX_GAIN_REG1D, 0x10);
	snd_soc_component_write(component, ES8374_SPK_OUT_GAIN_REG1E, 0x40);
	snd_soc_component_update_bits(component, ES8374_AIN_PWR_SRC_REG21, 0xc0, 0x00);
	snd_soc_component_write(component, ES8374_ANA_PWR_CTL_REG15, 0xbf);
	snd_soc_component_write(component, ES8374_ANA_REF_REG14, 0x14);
	snd_soc_component_write(component, ES8374_CLK_MANAGEMENT_REG01, 0x03);
	return 0;
}

static int es8374_resume(struct snd_soc_component *component)
{
	printk("=======enter func %s,line %d\n",__func__,__LINE__);

		snd_soc_component_write(component, ES8374_CLK_MANAGEMENT_REG01, 0x7f);
		snd_soc_component_write(component, ES8374_ANA_REF_REG14, 0x8a);
		snd_soc_component_write(component, ES8374_ANA_PWR_CTL_REG15, 0x40);
		snd_soc_component_update_bits(component, ES8374_AIN_PWR_SRC_REG21, 0xc0, 0xc0);
		snd_soc_component_write(component, ES8374_MONO_MIX_REG1A, 0xa0);
		snd_soc_component_write(component, ES8374_SPK_MIX_REG1C, 0x90);
		snd_soc_component_write(component, ES8374_SPK_MIX_GAIN_REG1D, 0x02);
		snd_soc_component_write(component, ES8374_SPK_OUT_GAIN_REG1E, 0xa0);
		snd_soc_component_write(component, ES8374_DAC_CONTROL_REG36, 0x00);
		snd_soc_component_write(component, ES8374_DAC_CONTROL_REG37, 0x00);
		snd_soc_component_write(component, ES8374_DAC_VOLUME_REG38, 0x00);
		snd_soc_component_write(component, ES8374_ADC_VOLUME_REG25, 0x00);
	return 0;
}

static int es8374_probe(struct snd_soc_component *component)
{
	printk("=======enter func %s,line %d\n",__func__,__LINE__);
	int ret = 0;
	struct es8374_private *es8374 = es8374_data;

	es8374_codec = component;
	es8374->component = component;
	snd_soc_component_set_drvdata(component, es8374);
#if 0
	es8374->mclk = devm_clk_get(component->dev, "mclk");
	if (IS_ERR(es8374->mclk)) {
		dev_err(component->dev, "%s,unable to get mclk\n", __func__);
		return PTR_ERR(es8374->mclk);
	}
	if (!es8374->mclk)
		dev_err(component->dev, "%s, assuming static mclk\n", __func__);

	ret = clk_prepare_enable(es8374->mclk);
	if (ret) {
		dev_err(component->dev, "%s, unable to enable mclk\n", __func__);
		return ret;
	}
#endif
	printk("Enter into %s-------\n", __func__);
	snd_soc_component_write(component,0x00,0x3F);	//IC Rst start
	msleep(1);  					//DELAY_MS
	snd_soc_component_write(component,0x00,0x03);	//IC Rst stop
	snd_soc_component_write(component,0x01,0x7F);	//IC clk on
	snd_soc_component_write(component,0x05,0x11);	//clk div set
	snd_soc_component_write(component,0x6F,0xA0);	//pll set:mode enable
	snd_soc_component_write(component,0x72,0x41);	//pll set:mode set
	snd_soc_component_write(component,0x09,0x01);	//pll set:reset on ,set start
	snd_soc_component_write(component,0x0C,0x22);	//pll set:k
	snd_soc_component_write(component,0x0D,0x2E);	//pll set:k
	snd_soc_component_write(component,0x0E,0xC6);	//pll set:k
	snd_soc_component_write(component,0x0A,0x3A);	//pll set:
	snd_soc_component_write(component,0x0B,0x07);	//pll set:n
	snd_soc_component_write(component,0x09,0x41);	//pll set:reset off ,set stop
	snd_soc_component_write(component,0x10,0x0f);	//ADC I2S-16BIT
	snd_soc_component_write(component,0x11,0x0f);	//DAC I2S-16BIT
	snd_soc_component_write(component,0x24,0x08);	//adc set
	snd_soc_component_write(component,0x36,0x00);	//dac set
	snd_soc_component_write(component,0x12,0x30);	//timming set
	snd_soc_component_write(component,0x13,0x20);	//timming set
	snd_soc_component_write(component, 0x18, 0xff);	//timming set
	snd_soc_component_write(component,0x21,0x50);	//adc set:	SEL LIN1 CH+PGAGAIN=0DB
	snd_soc_component_write(component,0x22,0x55);	//adc set:	PGA GAIN=0DB
	snd_soc_component_write(component,0x21,0x14);	//adc set:	SEL LIN1 CH+PGAGAIN=0DB
	snd_soc_component_write(component,0x00,0x80);	// IC START
	msleep(50);  					//DELAY_MS
	snd_soc_component_write(component,0x14,0x8A);	// IC START
	snd_soc_component_write(component,0x15,0x40);	// IC START
	snd_soc_component_write(component,0x1A,0xA0);	// monoout set
	snd_soc_component_write(component,0x1B,0x19);	// monoout set
	snd_soc_component_write(component,0x1C,0x90);	// spk set
	snd_soc_component_write(component,0x1D,0x02);	// spk set
	snd_soc_component_write(component,0x1F,0x00);	// spk set
	snd_soc_component_write(component,0x1E,0xA0);	// spk on
	snd_soc_component_write(component,0x28,0x00);	// alc set
	snd_soc_component_write(component,0x25,0x00);	// ADCVOLUME  on
	snd_soc_component_write(component,0x38,0x00);	// DACVOLUMEL on
	snd_soc_component_write(component,0x37,0x00);	// dac set
	snd_soc_component_write(component,0x6D,0x60);	//SEL:GPIO1=DMIC CLK OUT+SEL:GPIO2=PLL CLK OUT
	msleep(2000);
	return ret;
}

static void es8374_remove(struct snd_soc_component *component)
{
	printk("=======enter func %s,line %d\n",__func__,__LINE__);
	es8374_set_bias_level(component, SND_SOC_BIAS_OFF);
}

static struct snd_soc_component_driver soc_codec_dev_es8374 = {
	.probe =	es8374_probe,
	.remove =	es8374_remove,
	.suspend =	es8374_suspend,
	.resume =	es8374_resume,
	.set_bias_level = es8374_set_bias_level,

	.controls = es8374_snd_controls,
	.num_controls = ARRAY_SIZE(es8374_snd_controls),
	.dapm_widgets = es8374_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(es8374_dapm_widgets),
	.dapm_routes = es8374_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(es8374_dapm_routes),
};

static struct regmap_config es8374_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = ES8374_MAX_REGISTER,
	.reg_defaults = es8374_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(es8374_reg_defaults),
	.volatile_reg = es8374_volatile_register,
	.writeable_reg = es8374_writable_register,
	.readable_reg  = es8374_readable_register,
	.cache_type = REGCACHE_RBTREE,
};



static void es8374_i2c_shutdown(struct i2c_client *i2c)
{
	printk("=======enter func %s,line %d\n",__func__,__LINE__);
	struct snd_soc_component *component;
	struct es8374_private *es8374;

	es8374 = i2c_get_clientdata(i2c);
	component = es8374->component;

	snd_soc_component_write(component, ES8374_DAC_VOLUME_REG38, 0xc0);
	snd_soc_component_write(component, ES8374_ADC_VOLUME_REG25, 0xc0);
	snd_soc_component_write(component, ES8374_DAC_CONTROL_REG36, 0x20);
	snd_soc_component_write(component, ES8374_DAC_CONTROL_REG37, 0x21);
	snd_soc_component_write(component, ES8374_MONO_MIX_REG1A, 0x08);
	snd_soc_component_write(component, ES8374_SPK_MIX_REG1C, 0x10);
	snd_soc_component_write(component, ES8374_SPK_MIX_GAIN_REG1D, 0x10);
	snd_soc_component_write(component, ES8374_SPK_OUT_GAIN_REG1E, 0x40);
	snd_soc_component_update_bits(component, ES8374_AIN_PWR_SRC_REG21, 0xc0, 0x00);
	snd_soc_component_write(component, ES8374_ANA_PWR_CTL_REG15, 0xbf);
	snd_soc_component_write(component, ES8374_ANA_REF_REG14, 0x14);
	snd_soc_component_write(component, ES8374_CLK_MANAGEMENT_REG01, 0x03);

}
static u32 cur_reg;

static ssize_t es8374_show(struct device *dev, struct device_attribute *attr, char *_buf)
{
	printk("=======enter func %s,line %d\n",__func__,__LINE__);
	int ret;
	ret = sprintf(_buf, "%s(): get 0x%04x=0x%04x\n", __FUNCTION__, cur_reg,
	snd_soc_component_read(es8374_codec, cur_reg));

	return ret;
}

static ssize_t es8374_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	printk("=======enter func %s,line %d\n",__func__,__LINE__);
	int val = 0, flag = 0;
	u8 i = 0, reg, num, value_w, value_r;

	//struct es7210_priv *es7210 = dev_get_drvdata(dev);
	val = simple_strtol(buf, NULL, 16);
	flag = (val >> 16) & 0xFF;

	if (flag) {
		reg = (val >> 8) & 0xFF;
		value_w = val & 0xFF;
		printk("\nWrite: start REG:0x%02x,val:0x%02x,count:0x%02x\n", reg, value_w, flag);
		while (flag--) {
			snd_soc_component_write(es8374_codec, reg, value_w);
			printk("Write 0x%02x to REG:0x%02x\n", value_w, reg);
			reg++;
		}
	} else {
		reg = (val >> 8) & 0xFF;
		num = val & 0xff;
		printk("\nRead: start REG:0x%02x,count:0x%02x\n", reg, num);
		do {
			value_r = 0;
			value_r = snd_soc_component_read(es8374_codec, reg);
			printk("REG[0x%02x]: 0x%02x;  \n", reg, value_r);
			reg++;
			i++;
		} while (i < num);
	}

	return count;
}

static DEVICE_ATTR(es8374, 0664, es8374_show, es8374_store);

static struct attribute *es8374_debug_attrs[] = {
	&dev_attr_es8374.attr,
	NULL,
};

static struct attribute_group es8374_debug_attr_group = {
	.name   = "es8374_debug",
	.attrs  = es8374_debug_attrs,
};

static int es8374_i2c_probe(struct i2c_client *i2c_client,
		const struct i2c_device_id *id)
{
	struct es8374_private *es8374;

	int ret = -1;
	printk("================================Enter into %s\n", __func__);
	es8374 = kzalloc(sizeof(*es8374), GFP_KERNEL);
	if (es8374 == NULL)
		return -ENOMEM;

#ifdef es8374_dmic
	es8374->dmic_enable = 1;     /* if internal mic is dmic */
#else
	es8374->dmic_enable = 0;     /* if internal mic is amic */
#endif

	i2c_set_clientdata(i2c_client, es8374);
	es8374->regmap = devm_regmap_init_i2c(i2c_client, &es8374_regmap);
	if (IS_ERR(es8374->regmap)) {
		ret = PTR_ERR(es8374->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	es8374_data = es8374;

	ret =  snd_soc_register_component(&i2c_client->dev, &soc_codec_dev_es8374,
			&es8374_dai[0], ARRAY_SIZE(es8374_dai));
	if (ret < 0) {
		kfree(es8374);
		return ret;
	}
	ret = sysfs_create_group(&i2c_client->dev.kobj, &es8374_debug_attr_group);
	if (ret)
		pr_err("failed to create attr group\n");

	printk("Exit %s\n", __func__);
	return ret;
}

static  int es8374_i2c_remove(struct i2c_client *client)
{
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id es8374_i2c_id[] = {
	{"es8374", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8374_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id es8374_if_dt_ids[] = {
	{ .compatible = "everest,es8374", },
	{ }
};
#endif
MODULE_DEVICE_TABLE(of, es8374_if_dt_ids);

static struct i2c_driver es8374_i2c_driver = {
	.driver = {
		.name	= "es8374",
		.of_match_table = of_match_ptr(es8374_if_dt_ids),
	},
	.shutdown	= es8374_i2c_shutdown,
	.probe	= es8374_i2c_probe,
	.remove	= es8374_i2c_remove,
	.id_table	= es8374_i2c_id,
};

module_i2c_driver(es8374_i2c_driver);
static void __exit es8374_exit(void)
{
	i2c_del_driver(&es8374_i2c_driver);
}
module_exit(es8374_exit);

MODULE_DESCRIPTION("ASoC es8374 driver");
MODULE_AUTHOR("XianqingZheng <xqzheng@ambarella.com>");
MODULE_LICENSE("GPL");


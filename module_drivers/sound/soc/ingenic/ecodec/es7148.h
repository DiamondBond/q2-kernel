/*
 * ES7148 DAC driver
 *
 * Author:      Everest Semiconductor
 * Copyright:   (C) 2018 Everest Semiconductor Co Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ES7148_H_
#define _ES7148_H_

#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

/* ES7148 register definitions */
#define ES7148_CLK_CONTROL		0x00
#define ES7148_SYSTEM_CONTROL		0x01
#define ES7148_DAC_CONTROL		0x02

/* Supported sample rates */
#define ES7148_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 | \
		     SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
		     SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
		     SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000)

/* Supported formats */
#define ES7148_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
		       SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

struct es7148_priv {
	struct snd_soc_component *component;
	unsigned int sysclk;
	bool enabled;
};

#endif /* _ES7148_H_ */



/*
 * rt5648_ioctl.h  --  RT5648 ALSA SoC audio driver IO control
 *
 * Copyright 2012 Realtek Microelectronics
 * Author: Bard <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RT5648_IOCTL_H__
#define __RT5648_IOCTL_H__

#include <sound/hwdep.h>
#include <linux/ioctl.h>

enum {
        PhoneCall_Default,
        RingTone_Coming,
        PhoneCall_Connected,
};
enum {
	Record_Default,
	Record_Headset_mic,
	Record_Front_mic,
};
enum {
	NORMAL=0,
	CLUB,
	SPK,
	HP,
	MODE_NUM,
};

enum {
	EQ_CH_DACL = 0,
	EQ_CH_DACR,
	EQ_CH_ADC,
	EQ_CH_NUM,
};



#define EQ_REG_NUM 56
typedef struct  hweq_s {
 	unsigned int reg[EQ_REG_NUM];
	unsigned int value[EQ_REG_NUM];
	unsigned int ctrl;
} hweq_t;

int rt5648_ioctl_common(struct snd_hwdep *hw, struct file *file,
			unsigned int cmd, unsigned long arg);
int rt5648_update_eqmode(
	struct snd_soc_codec *codec, int channel, int mode);

#endif /* __RT5648_IOCTL_H__ */


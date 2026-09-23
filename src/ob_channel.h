/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — channel/frequency math.
 *
 * Rules recovered from the blob (RE Stage 6) and verified at instruction level:
 *   2.4 GHz: f = 2407 + 5*ch, ch14 = 2484
 *   5   GHz: f = 5000 + 5*ch
 */
#ifndef _OB_CHANNEL_H_
#define _OB_CHANNEL_H_

#include <linux/types.h>

#define OB_BAND_2GHZ	0
#define OB_BAND_5GHZ	1

/* Returns center frequency in MHz, or -1 if the channel is invalid. */
int ob_channel_to_freq(u16 ch, u8 band);

/* Returns channel number, or -1 if the frequency is invalid for the band. */
int ob_freq_to_channel(u16 mhz, u8 band);

#endif /* _OB_CHANNEL_H_ */

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

/*
 * 2.4 GHz channel list (provenance: RE Stage 6, the channel<->frequency rules
 * were verified at instruction level; channels 1..14 are the standard 2.4 GHz
 * set the formula is defined over).
 *
 * NOTE: the exact per-chip 5 GHz channel set has NOT been recovered yet. Do not
 * advertise 5 GHz until it is (see docs/milestones.md, M2 blocker).
 */
#define OB_N_2GHZ_CHANNELS	14
extern const u16 ob_2ghz_channels[OB_N_2GHZ_CHANNELS];

/* Returns center frequency in MHz, or -1 if the channel is invalid. */
int ob_channel_to_freq(u16 ch, u8 band);

/* Returns channel number, or -1 if the frequency is invalid for the band. */
int ob_freq_to_channel(u16 mhz, u8 band);

#endif /* _OB_CHANNEL_H_ */

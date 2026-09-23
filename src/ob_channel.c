// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — channel/frequency math (RE Stage 6).
 *
 * Pure functions, unit-tested by KUnit (tests/kunit) and on the host
 * (tests/host). No hardware access.
 */
#include <linux/types.h>
#include "ob_channel.h"

const u16 ob_2ghz_channels[OB_N_2GHZ_CHANNELS] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
};

int ob_channel_to_freq(u16 ch, u8 band)
{
	if (band == OB_BAND_2GHZ) {
		if (ch == 14)
			return 2484;
		if (ch >= 1 && ch <= 13)
			return 2407 + 5 * ch;
		return -1;
	}
	if (band == OB_BAND_5GHZ) {
		if (ch < 15)
			return -1;
		return 5000 + 5 * ch;
	}
	return -1;
}

int ob_freq_to_channel(u16 mhz, u8 band)
{
	if (band == OB_BAND_2GHZ) {
		if (mhz == 2484)
			return 14;
		if (mhz >= 2412 && mhz <= 2472 && (mhz - 2407) % 5 == 0)
			return (mhz - 2407) / 5;
		return -1;
	}
	if (band == OB_BAND_5GHZ) {
		if (mhz >= 5000 + 5 * 15 && mhz <= 5000 + 5 * 200 && (mhz - 5000) % 5 == 0)
			return (mhz - 5000) / 5;
		return -1;
	}
	return -1;
}

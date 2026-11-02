// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — channel/frequency math (RE Stage 6).
 *
 * Pure functions, unit-tested by KUnit (tests/kunit) and on the host
 * (tests/host). No hardware access.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include "ob_channel.h"

const u16 ob_2ghz_channels[OB_N_2GHZ_CHANNELS] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
};

int ob_test_chanspec_fixture(u16 chip_id, u8 chip_rev, u8 d11_corerev,
			     u8 phy_type, u8 phy_revision, u16 radio_id,
			     u8 radio_revision, u16 *encoded)
{
	if (!encoded || chip_id != 0x4352 || chip_rev != 3 ||
	    d11_corerev != 42 || phy_type != 0x0b || phy_revision != 1 ||
	    radio_id != 0x2069 || radio_revision != 4)
		return -EINVAL;

	*encoded = OB_TEST_CHANSPEC_BCM4352_CH1_20MHZ;
	return 0;
}

int ob_acphy_encode_2g_20mhz(u16 channel, u16 center_freq, u16 *encoded)
{
	int expected_freq;

	if (!encoded)
		return -EINVAL;
	expected_freq = ob_channel_to_freq(channel, OB_BAND_2GHZ);
	if (expected_freq < 0 || center_freq != expected_freq)
		return -EINVAL;

	*encoded = (u16)(0x1000u | channel);
	return 0;
}

int ob_acphy_bw_transition_needed(u16 encoded, u8 current_valid,
				  u16 cached, u8 cached_valid, u8 *needed)
{
	if (!needed || !current_valid || !cached_valid)
		return -EINVAL;

	*needed = (cached != (encoded & 0x3800));
	return 0;
}

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

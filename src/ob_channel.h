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

/* Vendor-primary, test-only BCM4352 AC-PHY fixture.  This is never
 * initialized into ordinary production channel state. */
#define OB_TEST_CHANSPEC_BCM4352_CH1_20MHZ	0x1001

int ob_test_chanspec_fixture(u16 chip_id, u8 chip_rev, u8 d11_corerev,
			     u8 phy_type, u8 phy_revision, u16 radio_id,
			     u8 radio_revision, u16 *encoded);

/* Encode only the admitted 2.4-GHz 20-MHz initial-up chanspec. */
int ob_acphy_encode_2g_20mhz(u16 channel, u16 center_freq, u16 *encoded);

/* Pure fail-closed semantic decision; no hardware access. */
int ob_acphy_bw_transition_needed(u16 encoded, u8 current_valid,
				  u16 cached, u8 cached_valid, u8 *needed);

/* Returns center frequency in MHz, or -1 if the channel is invalid. */
int ob_channel_to_freq(u16 ch, u8 band);

/* Returns channel number, or -1 if the frequency is invalid for the band. */
int ob_freq_to_channel(u16 mhz, u8 band);

#endif /* _OB_CHANNEL_H_ */

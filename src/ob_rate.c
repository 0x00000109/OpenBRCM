// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — data-rate math (RE Stage 6).
 *
 * Coefficient triplets (b0, b1, b2) are the blob's own table; the formula and
 * the subcarrier counts are 802.11n/ac. Pure, unit-tested.
 */
#include <linux/types.h>
#include "ob_rate.h"

static const u8 ob_mcs_coeff[10][3] = {
	{1, 1, 2}, {2, 1, 2}, {2, 3, 4}, {4, 1, 2}, {4, 3, 4},
	{6, 2, 3}, {6, 3, 4}, {6, 5, 6}, {8, 3, 4}, {8, 5, 6},
};

/*
 * 2.4 GHz legacy rates. Values are the blob's own rate constants (ob_rate.h);
 * the bitrate presented to mac80211 is derived as (rate500 & 0x7f) * 5.
 */
const struct ob_rate ob_2ghz_rates[OB_N_2GHZ_RATES] = {
	{ OB_RATE_1M,	true  },	/* 1   Mbps */
	{ OB_RATE_2M,	true  },	/* 2   Mbps */
	{ OB_RATE_5_5M,	true  },	/* 5.5 Mbps */
	{ OB_RATE_11M,	true  },	/* 11  Mbps */
	{ OB_RATE_6M,	true  },	/* 6   Mbps */
	{ OB_RATE_9M,	false },	/* 9   Mbps */
	{ OB_RATE_12M,	true  },	/* 12  Mbps */
	{ OB_RATE_18M,	false },	/* 18  Mbps */
	{ OB_RATE_24M,	true  },	/* 24  Mbps */
	{ OB_RATE_36M,	false },	/* 36  Mbps */
	{ OB_RATE_48M,	false },	/* 48  Mbps */
	{ OB_RATE_54M,	false },	/* 54  Mbps */
};

static u16 ob_nsd(u8 bw_mhz)
{
	switch (bw_mhz) {
	case 20:  return 52;
	case 40:  return 108;
	case 80:  return 234;
	case 160: return 468;
	default:  return 0;
	}
}

u32 ob_mcs_to_rate_kbps(u8 mcs, u8 bw_mhz, u8 nss, bool sgi)
{
	u16 nsd = ob_nsd(bw_mhz);
	u32 base;

	if (mcs > 9 || nss == 0 || nsd == 0)
		return 0;

	base = (u32)ob_mcs_coeff[mcs][0] * ob_mcs_coeff[mcs][1] *
	       nss * nsd / ob_mcs_coeff[mcs][2];

	if (sgi)
		return (base * 2500 + 4) / 9;
	return base * 250;
}

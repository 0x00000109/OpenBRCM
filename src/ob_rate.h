/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — data-rate math.
 *
 * The blob implements 802.11n/ac data rates with the formula recovered in
 * RE Stage 6 (wlc_rate_mcs2rate):
 *   base  = Nsd * Nss * b0 * b1 / b2
 *   GI800 = base * 250                     [kbps]
 *   GI400 = (base * 2500 + 4) / 9          [kbps]
 * Nsd = 52/108/234/468 for 20/40/80/160 MHz.
 */
#ifndef _OB_RATE_H_
#define _OB_RATE_H_

#include <linux/types.h>

/* Legacy 802.11 rates in 500 kbps units (0x80 marks an 11b/CCK rate). */
#define OB_RATE_1M	0x02
#define OB_RATE_2M	0x04
#define OB_RATE_5_5M	0x0b
#define OB_RATE_11M	0x16
#define OB_RATE_6M	0x0c
#define OB_RATE_9M	0x12
#define OB_RATE_12M	0x18
#define OB_RATE_18M	0x24
#define OB_RATE_24M	0x30
#define OB_RATE_36M	0x48
#define OB_RATE_48M	0x60
#define OB_RATE_54M	0x6c

/* Returns the MCS data rate in kbps, or 0 for an out-of-range request. */
u32 ob_mcs_to_rate_kbps(u8 mcs, u8 bw_mhz, u8 nss, bool sgi);

/* Legacy 2.4 GHz rate set (provenance: RE Stage 6 rate tables). */
struct ob_rate {
	u16 rate500;	/* value in 500 kbps units (low 7 bits) */
	bool basic;	/* mandatory/basic rate */
};

#define OB_N_2GHZ_RATES	12
extern const struct ob_rate ob_2ghz_rates[OB_N_2GHZ_RATES];

#endif /* _OB_RATE_H_ */

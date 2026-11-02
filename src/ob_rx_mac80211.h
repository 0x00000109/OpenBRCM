/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — offline RX translation boundary for mac80211 (Phase 35).
 *
 * This layer does not decode D11 descriptors and does not touch hardware. It
 * accepts only an already validated frame envelope (ob_rx_sw_validate()) plus
 * an explicit evidence word saying which receive facts the hardware path has
 * actually established. A fact that is not in @proven is never inferred.
 *
 * Why the gate is closed
 * ----------------------
 * The pinned vendor code proves the rev42 RX prefix (40 bytes), the
 * little-endian RxFrameSize word, and RxChan as the receive-channel source
 * (prefix +0x16: low byte = channel, bits 15:14 = 5 GHz). It does not establish
 * signal dBm or decryption status. This layer still requires explicit caller
 * evidence for every fact it accepts, and it never synthesizes a frequency.
 * mac80211 expects a frame with the FCS already removed unless
 * IEEE80211_HW_RX_INCLUDES_FCS is set, and it cannot represent an unknown
 * legacy rate. Delivery therefore requires:
 *
 *   - OB_RX_MAC80211_FACT_FCS_PROVEN and fcs_present == 0
 *     (a proven-stripped frame; this driver does not set RX_INCLUDES_FCS),
 *   - OB_RX_MAC80211_FACT_CHANNEL_PROVEN with a channel value (1..14 or
 *     32..196, matching ob_rx_chan_decode()),
 *   - OB_RX_MAC80211_FACT_RATE_PROVEN with a rate value. A legacy rate is an
 *     index into the band's rate table; an HT rate is an MCS index 0..31 and
 *     carries its proven SGI bit separately. The current BCM4352 profile
 *     advertises HT20 only, so an HT40 report is refused until .start accepts
 *     a 40 MHz channel.
 *
 * This boundary sets no fact automatically. Live delivery remains disabled
 * and no production caller exists; the kernel-side wrapper returns
 * -EOPNOTSUPP before consuming an skb.
 */

#ifndef _OB_RX_MAC80211_H_
#define _OB_RX_MAC80211_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ob_rx_mac80211_u8;
typedef u16 ob_rx_mac80211_u16;
typedef u32 ob_rx_mac80211_u32;
typedef s32 ob_rx_mac80211_s32;
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t ob_rx_mac80211_u8;
typedef uint16_t ob_rx_mac80211_u16;
typedef uint32_t ob_rx_mac80211_u32;
typedef int32_t ob_rx_mac80211_s32;
#endif

#include "ob_rx_sw.h"

/* Facts independently established for this receive source. */
#define OB_RX_MAC80211_FACT_FCS_PROVEN		(1u << 0)
#define OB_RX_MAC80211_FACT_CHANNEL_PROVEN	(1u << 1)
#define OB_RX_MAC80211_FACT_RATE_PROVEN		(1u << 2)
#define OB_RX_MAC80211_FACT_SIGNAL_PROVEN	(1u << 3)
#define OB_RX_MAC80211_FACT_DECRYPT_PROVEN	(1u << 4)
#define OB_RX_MAC80211_FACT_KNOWN_MASK		(OB_RX_MAC80211_FACT_FCS_PROVEN | \
						 OB_RX_MAC80211_FACT_CHANNEL_PROVEN | \
						 OB_RX_MAC80211_FACT_RATE_PROVEN | \
						 OB_RX_MAC80211_FACT_SIGNAL_PROVEN | \
						 OB_RX_MAC80211_FACT_DECRYPT_PROVEN)

struct ob_rx_mac80211_evidence {
	ob_rx_mac80211_u32 proven;
	/* Meaningful only with FACT_FCS_PROVEN. 1 means the FCS is still present. */
	ob_rx_mac80211_u8 fcs_present;
	/* Meaningful only with FACT_DECRYPT_PROVEN. */
	ob_rx_mac80211_u8 decrypted;
};

#define OB_RX_MAC80211_REPORT_SIGNAL_VALID	(1u << 0)
#define OB_RX_MAC80211_REPORT_DECRYPTED		(1u << 1)

/*
 * Capability policy, not a hardware fact. ob_mac80211.c advertises
 * IEEE80211_HT_CAP_SGI_20 only and .start accepts NL80211_CHAN_WIDTH_20, so a
 * received 40 MHz frame cannot be represented truthfully yet.
 */
#define OB_RX_MAC80211_HT40_SUPPORTED		0u

/* Neutral form of the fields that may cross into ieee80211_rx_status. */
struct ob_rx_mac80211_report {
	ob_rx_mac80211_u32 flags;
	ob_rx_mac80211_u16 channel;
	ob_rx_mac80211_u16 rate_index;
	ob_rx_mac80211_u8 rate_is_ht;
	ob_rx_mac80211_u8 bw40;
	ob_rx_mac80211_u8 sgi;
	ob_rx_mac80211_s32 signal_dbm;
};

/*
 * Translate one validated envelope. The output is zeroed on every failure.
 * -ENODATA means a required fact is missing; -EOPNOTSUPP means this driver
 * cannot represent the proven state (for example an FCS still in the frame).
 */
int ob_rx_mac80211_translate(const struct ob_rx_sw_status *status,
			     const struct ob_rx_mac80211_evidence *evidence,
			     struct ob_rx_mac80211_report *report);

/* Sink operations use opaque skb pointers so this file stays mac80211-free. */
struct ob_rx_mac80211_ops {
	/* Ownership of @skb transfers to the sink on a successful call. */
	void (*deliver)(void *ctx, void *skb,
			const struct ob_rx_mac80211_report *report);
	/* Consume @skb exactly once when delivery is refused. */
	void (*free)(void *ctx, void *skb);
};

/*
 * Return value from ob_rx_mac80211_handoff() when the skb was consumed through
 * ops->free but was not delivered. This is a positive value, distinct from all
 * negative errno values.
 */
#define OB_RX_MAC80211_HANDOFF_CONSUMED	1

/*
 * Validate and hand off exactly once.
 *
 * Return/ownership contract:
 *   < 0                              no callback ran; caller still owns @skb
 *   0                                ops->deliver ran once; ownership moved
 *   OB_RX_MAC80211_HANDOFF_CONSUMED   ops->free ran once; ownership gone
 *
 * A NULL skb or a NULL/incomplete ops table is a programming error: -EINVAL
 * and the caller still owns the skb. A translation failure consumes the skb
 * through ops->free and returns the positive consumed value, so a negative
 * result always means "the caller owns the skb".
 *
 * Translation failure details stay in ob_rx_mac80211_translate(); callers that
 * need the exact errno must call it before deciding whether to hand off. The
 * ownership return value never carries a translation errno.
 */
int ob_rx_mac80211_handoff(void *skb, const struct ob_rx_sw_status *status,
			   const struct ob_rx_mac80211_evidence *evidence,
			   const struct ob_rx_mac80211_ops *ops, void *ctx);

/* Compile-time policy: live mac80211 RX delivery is not admitted. */
bool ob_rx_mac80211_live_delivery_enabled(void);

#endif /* _OB_RX_MAC80211_H_ */

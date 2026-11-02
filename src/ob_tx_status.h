/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — offline TX-status translation (Phase 35).
 *
 * The only instruction-proven status facts in the pinned vendor object are:
 *   - a status record is present when bit 0 of the sampled word is set;
 *   - bits 31:16 become the vendor status word, whose low three bits select a
 *     vendor queue slot;
 *   - the host frame ID is a 16-bit value derived from a wrapping counter.
 *
 * The mapping from the three-bit vendor slot selector to an OpenBRCM packet
 * ticket, the on-air ACK bit, retry count, rate and signal are NOT established.
 * This layer therefore never invents them:
 *
 *   - without FRAME_ID evidence it cannot associate a record with a packet;
 *   - a matching frame ID produces "delivered, unconfirmed" and is not
 *     reportable to mac80211;
 *   - only an explicit ACK fact makes a report eligible for
 *     ieee80211_tx_status();
 *   - retry/rate/signal values require their own evidence bit.
 *
 * Live TX-status reporting is compile-time disabled and no hardware TX path
 * exists. This object performs no MMIO and owns no skb.
 */

#ifndef _OB_TX_STATUS_H_
#define _OB_TX_STATUS_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ob_tx_status_u8;
typedef u16 ob_tx_status_u16;
typedef u32 ob_tx_status_u32;
typedef s8 ob_tx_status_s8;
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t ob_tx_status_u8;
typedef uint16_t ob_tx_status_u16;
typedef uint32_t ob_tx_status_u32;
typedef int8_t ob_tx_status_s8;
#endif

/* Facts independently established for the status source. */
#define OB_TX_STATUS_EV_FRAME_ID	(1u << 0) /* record <-> packet association */
#define OB_TX_STATUS_EV_ACK		(1u << 1) /* ack field is decoded and valid */
#define OB_TX_STATUS_EV_RETRIES		(1u << 2) /* retry counter is valid */
#define OB_TX_STATUS_EV_RATE		(1u << 3) /* rate index is valid */
#define OB_TX_STATUS_EV_SIGNAL		(1u << 4) /* signal value is valid */
#define OB_TX_STATUS_EV_KNOWN_MASK	(OB_TX_STATUS_EV_FRAME_ID | \
					 OB_TX_STATUS_EV_ACK | \
					 OB_TX_STATUS_EV_RETRIES | \
					 OB_TX_STATUS_EV_RATE | \
					 OB_TX_STATUS_EV_SIGNAL)

struct ob_tx_status_evidence {
	ob_tx_status_u32 proven;
	/* Meaningful only with EV_ACK. */
	ob_tx_status_u8 ack;
};

struct ob_tx_status_input {
	ob_tx_status_u8 record_present;
	ob_tx_status_u16 expected_frame_id;
	ob_tx_status_u16 reported_frame_id;
	ob_tx_status_u8 status_slot_selector;
	ob_tx_status_u8 retries;
	ob_tx_status_s8 signal_dbm;
	ob_tx_status_u16 rate_index;
};

enum ob_tx_status_disposition {
	OB_TX_STATUS_NONE = 0,
	/* Record belongs to another frame: drop without reporting success. */
	OB_TX_STATUS_DROP_FRAME_ID,
	/* Associated with the packet, but no ACK/retry/rate evidence exists. */
	OB_TX_STATUS_DELIVERED_UNCONFIRMED,
	OB_TX_STATUS_ACKED,
	OB_TX_STATUS_NOT_ACKED,
};

struct ob_tx_status_report {
	enum ob_tx_status_disposition disposition;
	/* True only when mac80211 has enough proven facts to be told anything. */
	ob_tx_status_u8 report_to_mac80211;
	ob_tx_status_u8 acked;
	ob_tx_status_u8 rate_known;
	ob_tx_status_u16 rate_index;
	ob_tx_status_u8 retries_known;
	ob_tx_status_u8 retries;
	ob_tx_status_u8 signal_known;
	ob_tx_status_s8 signal_dbm;
};

/* Number of legacy 2.4 GHz rates: index 0..11 is representable. */
#define OB_TX_STATUS_RATE_INDEX_LIMIT 12u

/*
 * Translate one status record. A missing record is -ENODATA and leaves the
 * caller owning the packet and mapping. A record that cannot be associated
 * (no FRAME_ID evidence) is also -ENODATA. A frame-ID mismatch is a handled
 * packet drop, not a queue failure.
 */
int ob_tx_status_translate(const struct ob_tx_status_input *input,
			   const struct ob_tx_status_evidence *evidence,
			   struct ob_tx_status_report *report);

/*
 * Exact skb ownership for the gated mac80211 handoff.
 *
 * Phase 36 removed the ambiguity where an errno-valued return meant both
 * "caller keeps the skb" and "the skb is gone". The contract is now a single
 * rule that a caller can apply without knowing the internal branches:
 *
 *   negative return  -> the skb was NOT transferred; the caller still owns it
 *   zero             -> the skb was CONSUMED and its status was reported
 *   positive return  -> the skb was CONSUMED and silently dropped
 *
 * There is no return value that both consumes and reports failure. A caller
 * that passes a valid skb and a valid hw pointer therefore always transfers
 * ownership exactly once; the only retained case is an invalid argument, and
 * that is always a negative return.
 */
enum ob_tx_skb_owner {
	/* Nothing to transfer (no skb was supplied). */
	OB_TX_STATUS_SKB_NONE = 0,
	/* Freed once, no ieee80211_tx_status() call. */
	OB_TX_STATUS_SKB_CONSUMED_DROP = 1,
	/* Handed to mac80211 once. */
	OB_TX_STATUS_SKB_CONSUMED_REPORT = 2,
};

/*
 * Sink that performs the single ownership effect. Both callbacks are called
 * at most once per ob_tx_status_handoff() and never for the same skb.
 */
struct ob_tx_status_skb_ops {
	/* Free @skb exactly once; no mac80211 status call. */
	void (*drop)(void *ctx, void *skb);
	/* Hand @skb to the stack exactly once, with @report attached. */
	void (*report)(void *ctx, void *skb,
		       const struct ob_tx_status_report *report);
};

/*
 * The single ownership entry point for a status record.
 *
 *   return < 0                       -> @skb was NOT transferred; caller owns it
 *   return 0                         -> @skb was handed to ops->report once
 *   return OB_TX_STATUS_CONSUMED_UNREPORTED
 *                                    -> @skb was freed through ops->drop once
 *
 * A negative return happens only for a NULL skb or an incomplete ops table, and
 * is checked before any ownership effect. For every other input exactly one of
 * drop/report runs exactly once, so there is no path that both consumes the
 * skb and reports failure, and no path that returns without consuming it.
 */
int ob_tx_status_handoff(void *skb,
			 const struct ob_tx_status_report *report,
			 bool reporting_enabled,
			 const struct ob_tx_status_skb_ops *ops, void *ctx);

/*
 * Pure ownership decision. It never frees or reports anything; callers must
 * use ob_tx_status_handoff() to transfer ownership.
 */
enum ob_tx_skb_owner ob_tx_status_skb_action(bool reporting_enabled,
					     const struct ob_tx_status_report *report);

/* Positive value returned for a consumed, unreported skb. */
#define OB_TX_STATUS_CONSUMED_UNREPORTED 1

/*
 * mac80211 status composition.
 *
 * ieee80211_tx_info keeps `flags` and `status` in separate members, so clearing
 * `status` does not clear a stale `flags` bit. A status bit inherited from an
 * earlier use of the same skb must never reach mac80211, and an unproven rate
 * must never be advertised as rate index 0 (which is a real 1 Mb/s rate).
 *
 * The composer is a pure function so the host suite can drive it with a
 * pre-set ACK bit and unproven rate; ob_mac80211.c only copies the result.
 *
 *   - every flag bit is preserved except OB_TX_STATUS_MAC80211_FLAG_ACK;
 *   - that bit is cleared first and set only for a reportable, proven ACK;
 *   - the rate is emitted as idx = -1, count = 0 unless a rate is proven,
 *     which is the "no rate" encoding rejected by ieee80211_rate_valid()
 *     (idx >= 0 && count > 0).
 */
#define OB_TX_STATUS_MAC80211_FLAG_ACK		(1u << 9) /* IEEE80211_TX_STAT_ACK */
#define OB_TX_STATUS_MAC80211_RATE_UNKNOWN	(-1)
/* count is a 5-bit field; retries + 1 attempts saturate here. */
#define OB_TX_STATUS_MAC80211_RATE_MAX_ATTEMPTS	31u

struct ob_tx_status_tx_view {
	/* prior flags with the ACK bit cleared, then set only if proven */
	ob_tx_status_u32 flags;
	/* -1 when the rate is not proven */
	ob_tx_status_s8 rate_idx;
	/* 0 when the rate is not proven */
	ob_tx_status_u8 rate_count;
};

void ob_tx_status_mac80211_view(const struct ob_tx_status_report *report,
				ob_tx_status_u32 prior_flags,
				struct ob_tx_status_tx_view *view);

/* Compile-time policy: ieee80211_tx_status reporting is not admitted. */
bool ob_tx_status_live_reporting_enabled(void);

#endif /* _OB_TX_STATUS_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RX_SW_H_
#define _OB_RX_SW_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ob_rx_sw_u8;
typedef u16 ob_rx_sw_u16;
typedef u32 ob_rx_sw_u32;
typedef s32 ob_rx_sw_s32;
typedef unsigned long ob_rx_sw_size;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t ob_rx_sw_u8;
typedef uint16_t ob_rx_sw_u16;
typedef uint32_t ob_rx_sw_u32;
typedef int32_t ob_rx_sw_s32;
typedef size_t ob_rx_sw_size;
#endif

/* Metadata provenance is an explicit caller assertion, never inferred here. */
enum ob_rx_sw_provenance {
	OB_RX_SW_PROVENANCE_UNKNOWN = 0,
	OB_RX_SW_PROVENANCE_VALIDATED = 1,
};

/*
 * Only these metadata fields may cross the validation boundary. PHY type is
 * deliberately absent: the length contract follows ieee80211_hdrlen() and
 * must not infer a PHY from an omitted RATE_HT bit. See ob_rx_sw.c.
 */
#define OB_RX_SW_META_CHANNEL	(1u << 0)
#define OB_RX_SW_META_RATE	(1u << 1)
#define OB_RX_SW_META_SIGNAL	(1u << 2)
#define OB_RX_SW_META_FCS_LEN	(1u << 3)
/* rate_index is an HT MCS index instead of a legacy band table index. */
#define OB_RX_SW_META_RATE_HT	(1u << 4)
#define OB_RX_SW_META_KNOWN_MASK	(OB_RX_SW_META_CHANNEL | \
				 OB_RX_SW_META_RATE | OB_RX_SW_META_SIGNAL | \
				 OB_RX_SW_META_FCS_LEN | OB_RX_SW_META_RATE_HT)

struct ob_rx_sw_metadata {
	enum ob_rx_sw_provenance provenance;
	ob_rx_sw_u32 valid_fields;
	ob_rx_sw_size declared_len;
	ob_rx_sw_u8 complete;
	/* Values are read only when their corresponding valid_fields bit is set. */
	ob_rx_sw_u16 channel;
	ob_rx_sw_u16 rate_index;
	ob_rx_sw_s32 signal_dbm;
	ob_rx_sw_u8 fcs_len;
	/* Meaningful only with OB_RX_SW_META_RATE | OB_RX_SW_META_RATE_HT. */
	ob_rx_sw_u8 rate_bw40;
	ob_rx_sw_u8 rate_sgi;
};

/* Sanitized metadata passed to the sink; unset fields are always zero. */
struct ob_rx_sw_status {
	ob_rx_sw_u32 valid_fields;
	ob_rx_sw_u16 channel;
	ob_rx_sw_u16 rate_index;
	ob_rx_sw_s32 signal_dbm;
	ob_rx_sw_u8 fcs_len;
	ob_rx_sw_u8 rate_is_ht;	/* set iff OB_RX_SW_META_RATE_HT is set */
	ob_rx_sw_u8 rate_bw40;
	ob_rx_sw_u8 rate_sgi;
};

/* The sink borrows frame and status only for the duration of this call. */
typedef int (*ob_rx_sw_sink_fn)(void *ctx, const ob_rx_sw_u8 *frame,
				ob_rx_sw_size frame_len,
				const struct ob_rx_sw_status *status);

/*
 * Validate a complete frame and normalize explicitly trusted metadata.
 * Returns 0 on success; negative errno on malformed, incomplete, or
 * provenance-invalid inputs. No D11 status decoding or hardware access occurs.
 */
int ob_rx_sw_validate(const ob_rx_sw_u8 *frame,
		      ob_rx_sw_size available_len,
		      const struct ob_rx_sw_metadata *metadata,
		      struct ob_rx_sw_status *status);

/* On valid input, invokes sink exactly once. Frame ownership stays with caller. */
int ob_rx_sw_deliver(const ob_rx_sw_u8 *frame,
		     ob_rx_sw_size available_len,
		     const struct ob_rx_sw_metadata *metadata,
		     ob_rx_sw_sink_fn sink, void *sink_ctx);

#endif /* _OB_RX_SW_H_ */

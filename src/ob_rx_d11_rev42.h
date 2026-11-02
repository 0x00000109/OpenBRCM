/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RX_D11_REV42_H_
#define _OB_RX_D11_REV42_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ob_rx_d11_u8;
typedef u16 ob_rx_d11_u16;
typedef u32 ob_rx_d11_u32;
typedef unsigned long ob_rx_d11_size;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t ob_rx_d11_u8;
typedef uint16_t ob_rx_d11_u16;
typedef uint32_t ob_rx_d11_u32;
typedef size_t ob_rx_d11_size;
#endif

enum ob_rx_d11_length_provenance {
	OB_RX_D11_LENGTH_UNKNOWN = 0,
	/* Caller has independently validated this exact payload length. */
	OB_RX_D11_LENGTH_VALIDATED = 1,
};

struct ob_rx_d11_rev42_input {
	ob_rx_d11_u32 d11_core_revision;
	enum ob_rx_d11_length_provenance length_provenance;
	ob_rx_d11_size payload_length;
	ob_rx_d11_u8 complete;
};

struct ob_rx_d11_rev42_view {
	const ob_rx_d11_u8 *payload;
	ob_rx_d11_size payload_length;
};

enum ob_rx_d11_rev42_layout {
	OB_RX_D11_REV42_SINGLE_BUFFER = 0,
	OB_RX_D11_REV42_NEEDS_CHAIN = 1,
};

struct ob_rx_d11_rev42_length {
	ob_rx_d11_u32 rx_frame_size;
	ob_rx_d11_u32 required_dma_bytes;
	enum ob_rx_d11_rev42_layout layout;
};

/* Raw fields and branch outcomes observed in pinned wlc_recv @0x4c13e.
 * These deliberately avoid source-correlated semantic names.
 */
struct ob_rx_d11_rev42_status {
	ob_rx_d11_u32 raw_word_4;
	ob_rx_d11_u16 raw_word_0x16;
	ob_rx_d11_u8 raw_byte_16;
	ob_rx_d11_u8 raw_byte_18;
	ob_rx_d11_u8 drop_word4_branch;
	ob_rx_d11_u8 skip_two_bytes;
	ob_rx_d11_u8 drop_byte16_branch;
	ob_rx_d11_u8 aggregate_branch;
	ob_rx_d11_u8 raw_word_0x16_top2_selector;
	ob_rx_d11_u8 raw_byte_0x16_eq_0e;
	/* wlc_recv conditionally pulls six more bytes on its continuing path.
	 * These describe the operation/formula, not a PHY-header interpretation
	 * or proof that every packet reaches that path.
	 */
	ob_rx_d11_u8 conditional_later_packet_pull_len;
	ob_rx_d11_u8 offset_after_conditional_pull_on_continuing_path;
};

struct ob_rx_d11_rev42_chain_plan {
	ob_rx_d11_size segment_count;
	ob_rx_d11_size first_segment_length;
	ob_rx_d11_size continuation_bytes;
};

struct ob_rx_d11_rev42_segment {
	const ob_rx_d11_u8 *data;
	ob_rx_d11_size length;
};

/* Exact pinned BCM4352/D11-rev42 header prefix skipped before the MPDU. */
#define OB_RX_D11_REV42_PREFIX_LEN 40u
#define OB_RX_D11_REV42_RXFRAMESIZE_LEN 2u
#define OB_RX_D11_REV42_MAX_SEGMENTS 33u /* ceil((40 + U16_MAX) / 2048) */

/*
 * Return a borrowed view after the rev42 D11 header.
 *
 * This helper does not decode the header, infer payload length, FCS state, PHY
 * status, channel, rate, signal, or decryption. The caller must provide an
 * independently validated payload length and completeness assertion. All
 * bytes described by that length are preserved unchanged.
 */
int ob_rx_d11_rev42_payload_view(const ob_rx_d11_u8 *dma_buffer,
				 ob_rx_d11_size available_length,
				 const struct ob_rx_d11_rev42_input *input,
				 struct ob_rx_d11_rev42_view *view);

/*
 * Decode the pinned vendor's little-endian RxFrameSize word at buffer+0.
 * `required_dma_bytes` is the proven 40-byte RX offset plus that value. A
 * multi-buffer requirement is reported explicitly; this helper does not
 * assemble fragments or define FCS semantics.
 */
int ob_rx_d11_rev42_decode_length(ob_rx_d11_u32 d11_core_revision,
				  const ob_rx_d11_u8 *header,
				  ob_rx_d11_size header_length,
				  ob_rx_d11_size buffer_capacity,
				  struct ob_rx_d11_rev42_length *length);

/* Decode only raw, instruction-proven wlc_recv tests at prefix offsets
 * +4/u16, +0x10/u8, +0x12/u8, and +0x16/u16/u8. No field is exported as
 * mac80211 metadata.
 */
int ob_rx_d11_rev42_decode_status(ob_rx_d11_u32 d11_core_revision,
				  const ob_rx_d11_u8 *prefix,
				  ob_rx_d11_size prefix_length,
				  struct ob_rx_d11_rev42_status *status);

/*
 * Derive the historical dma-proc chunk boundaries. The first chunk includes
 * the RX prefix; continuation chunks begin at their data offset zero. This
 * does not prove that a caller's descriptor stream supplies these chunks.
 */
int ob_rx_d11_rev42_plan_chain(ob_rx_d11_u32 d11_core_revision,
			       ob_rx_d11_size rx_frame_size,
			       ob_rx_d11_size rx_offset,
			       ob_rx_d11_size chunk_capacity,
			       struct ob_rx_d11_rev42_chain_plan *plan);

/*
 * Copy a complete, already-detached historical DMA packet chain into an
 * opaque MPDU buffer. This host/offline adapter does not claim that OpenBRCM's
 * live RX descriptors have been proven to produce this segment array.
 */
int ob_rx_d11_rev42_copy_chain(ob_rx_d11_u32 d11_core_revision,
			       const struct ob_rx_d11_rev42_segment *segments,
			       ob_rx_d11_size segment_count,
			       ob_rx_d11_size rx_offset,
			       ob_rx_d11_size chunk_capacity,
			       ob_rx_d11_u8 *output,
			       ob_rx_d11_size output_capacity,
			       ob_rx_d11_size *output_length);

#endif /* _OB_RX_D11_REV42_H_ */

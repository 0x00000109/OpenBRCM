// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phase 36G — RX header-length and conditional four-byte consistency.
 *
 * The expected values in this file are an independent oracle, not a second
 * copy of the production formula:
 *
 *   - Minimum MPDU lengths come from Linux ieee80211_hdrlen()
 *     (net/wireless/util.c): 24 base, +6 for Address4, +2 for QoS control,
 *     +4 for the Order-indicated field on management and QoS data.
 *   - The vendor's wlc_recv/wlc_recvdata arithmetic is recorded in
 *     audit/openbrcm_phase36f and differs only for malformed legacy-PHY
 *     frames with Order set, where the vendor's internal parser (not
 *     mac80211) omits the field. OpenBRCM must follow the mac80211 contract.
 *   - Pad, PHY-header and FCS sizes are the Phase 36B/36C proven constants.
 *
 * No decoder is reimplemented here: the pad/FCS vectors call
 * ob_rx_status_decode(), ob_rx_frame_bounds() and
 * ob_rx_d11_rev42_decode_length(); the header vectors call
 * ob_rx_sw_validate()/ob_rx_sw_deliver().
 *
 * Offline only: no hardware, no DMA, no module load, no live RX/TX.
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_rx_d11_rev42.h"
#include "../../src/ob_rx_status.h"
#include "../../src/ob_rx_sw.h"

static unsigned int failures;
static unsigned int checks_run;

#define CHECK(name, expr) do { \
	checks_run++; \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

struct sink_state {
	unsigned int calls;
};

static int sink(void *ctx, const ob_rx_sw_u8 *frame, ob_rx_sw_size len,
		const struct ob_rx_sw_status *status)
{
	struct sink_state *state = ctx;

	(void)frame;
	(void)len;
	(void)status;
	state->calls++;
	return 0;
}

static void init_meta(struct ob_rx_sw_metadata *meta, ob_rx_sw_size len,
		      ob_rx_sw_u32 extra_fields)
{
	memset(meta, 0, sizeof(*meta));
	meta->provenance = OB_RX_SW_PROVENANCE_VALIDATED;
	meta->valid_fields = OB_RX_SW_META_FCS_LEN | extra_fields;
	meta->declared_len = len;
	meta->complete = 1;
	meta->fcs_len = 0;
}

/*
 * Drive both production entry points for one header vector. A rejected
 * vector must not reach the sink: that is the Phase 36E ownership boundary
 * (caller still owns the skb) at the validation edge.
 */
static void check_header(const char *name, ob_rx_sw_u8 fc0, ob_rx_sw_u8 fc1,
			 ob_rx_sw_size len, ob_rx_sw_u32 extra_fields,
			 int expected)
{
	ob_rx_sw_u8 frame[64];
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status status;
	struct sink_state state = { 0 };
	int ret;

	memset(frame, 0, sizeof(frame));
	frame[0] = fc0;
	frame[1] = fc1;
	init_meta(&meta, len, extra_fields);
	ret = ob_rx_sw_validate(frame, len, &meta, &status);
	CHECK(name, ret == expected);
	CHECK("rejected vector clears the normalized status", expected == 0 ||
	      status.valid_fields == 0);

	ret = ob_rx_sw_deliver(frame, len, &meta, sink, &state);
	CHECK("deliver mirrors validate", ret == expected);
	CHECK("only an accepted vector reaches the sink",
	      state.calls == (expected == 0 ? 1u : 0u));
}

static void test_header_lengths(void)
{
	/* M1: management Order=0 is a bare 24-byte header. */
	check_header("M1 management Order=0 at 24", 0x80, 0x00, 24, 0, 0);
	check_header("M1 management Order=0 at 23", 0x80, 0x00, 23, 0,
		     -EMSGSIZE);

	/* M2: management Order=1 carries the four-byte field. */
	check_header("M2 management Order=1 at 27", 0x80, 0x80, 27, 0,
		     -EMSGSIZE);
	check_header("M2 management Order=1 at 28", 0x80, 0x80, 28, 0, 0);

	/*
	 * M3: legacy PHY is not passed to this validator. The vendor's internal
	 * parser would accept 24 here because its predicate includes HT PHY,
	 * but mac80211's ieee80211_hdrlen() returns 28 for the same frame and
	 * would read four body bytes as the field. The interface contract is
	 * therefore 28; 24 is a false-acceptance hazard, not a valid frame.
	 */
	check_header("M3 legacy management Order=1 at 24 (mac80211 needs 28)",
		     0x80, 0x80, 24, 0, -EMSGSIZE);
	check_header("M3 legacy management Order=1 at 28", 0x80, 0x80, 28, 0,
		     0);

	/* D1: QoS data Order=1 is 24 + 2 QoS + 4 field. */
	check_header("D1 QoS data Order=1 at 29", 0x88, 0x80, 29, 0,
		     -EMSGSIZE);
	check_header("D1 QoS data Order=1 at 30", 0x88, 0x80, 30, 0, 0);

	/*
	 * D2: legacy QoS data Order=1. A 26-byte frame is complete only for a
	 * parser that omits the field. mac80211 computes 30, so rejecting 26 is
	 * required at this boundary.
	 */
	check_header("D2 legacy QoS data Order=1 at 26", 0x88, 0x80, 26, 0,
		     -EMSGSIZE);
	check_header("D2 legacy QoS data Order=1 at 30", 0x88, 0x80, 30, 0,
		     0);

	/* D4: QoS data + Address4 + Order=1 is 24 + 6 + 2 + 4. */
	check_header("D4 QoS Address4 Order=1 at 35", 0x88, 0x83, 35, 0,
		     -EMSGSIZE);
	check_header("D4 QoS Address4 Order=1 at 36", 0x88, 0x83, 36, 0, 0);

	/* D3: non-QoS data ignores Order; no field is present. */
	check_header("D3 non-QoS data Order=1 at 23", 0x08, 0x80, 23, 0,
		     -EMSGSIZE);
	check_header("D3 non-QoS data Order=1 at 24", 0x08, 0x80, 24, 0, 0);

	/* C1: ACK/CTS are 10 bytes; the vendor control pull is dead code. */
	check_header("C1 ACK Order=1 at 10", 0xd4, 0x80, 10, 0, 0);
	check_header("C1 ACK Order=1 at 9", 0xd4, 0x80, 9, 0, -EMSGSIZE);
	check_header("C1 CTS Order=1 at 10", 0xc4, 0x80, 10, 0, 0);
	check_header("C1 RTS has no admitted control contract", 0xb4, 0x00,
		     16, 0, -EOPNOTSUPP);

	/* Extension frames are not part of this format contract. */
	check_header("extension frame refused", 0x0c, 0x00, 24, 0,
		     -EOPNOTSUPP);

	/* N1: no PHY evidence is invented, and none is required. */
	check_header("N1 management Order=1 at 27, no PHY", 0x80, 0x80, 27, 0,
		     -EMSGSIZE);
	check_header("N1 management Order=1 at 28, no PHY", 0x80, 0x80, 28, 0,
		     0);
	check_header("N1 management Order=1 at 28, legacy rate",
		     0x80, 0x80, 28, OB_RX_SW_META_RATE, 0);
	check_header("N1 management Order=1 at 28, HT rate",
		     0x80, 0x80, 28,
		     OB_RX_SW_META_RATE | OB_RX_SW_META_RATE_HT, 0);
}

static void put_le16(ob_rx_sw_u8 *p, unsigned int value)
{
	p[0] = (ob_rx_sw_u8)(value & 0xffu);
	p[1] = (ob_rx_sw_u8)((value >> 8) & 0xffu);
}

/*
 * Build the proven rev42 single-buffer layout:
 *
 *   40-byte prefix | pad (0 or 2) | 6-byte PHY header | MPDU | 4-byte FCS
 *
 * RxFrameSize covers everything after the prefix, including the FCS.
 */
static size_t build_dma(ob_rx_sw_u8 *dma, size_t capacity, unsigned int pad,
			const ob_rx_sw_u8 *mpdu, size_t mpdu_len,
			unsigned int status1)
{
	size_t frame_size, total, off;

	if (pad != 0u && pad != 2u)
		return 0;
	frame_size = (size_t)pad + OB_RX_PLCP_LEN + mpdu_len + OB_RX_FCS_LEN;
	total = OB_RX_D11_REV42_PREFIX_LEN + frame_size;
	if (total > capacity)
		return 0;

	memset(dma, 0, total);
	put_le16(dma + OB_RX_STATUS_OFF_FRAME_SIZE, (unsigned int)frame_size);
	put_le16(dma + OB_RX_STATUS_OFF_STATUS1, status1);
	off = OB_RX_D11_REV42_PREFIX_LEN + pad;
	/* A six-byte PHY header is all zero here; only its length matters. */
	memcpy(dma + off + OB_RX_PLCP_LEN, mpdu, mpdu_len);
	memset(dma + off + OB_RX_PLCP_LEN + mpdu_len, 0xde, OB_RX_FCS_LEN);
	return total;
}

static void test_pad_and_fcs(void)
{
	ob_rx_sw_u8 dma[128];
	ob_rx_sw_u8 mpdu[36];
	struct ob_rx_d11_rev42_length length;
	struct ob_rx_frame_bounds bounds;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status status;
	struct ob_rx_status decoded;
	size_t total, off;
	int ret;

	memset(mpdu, 0, sizeof(mpdu));
	mpdu[0] = 0x80;
	mpdu[1] = 0x80;	/* management Order=1 */

	/* 11: two-byte D11 pad is outside the MPDU window. */
	total = build_dma(dma, sizeof(dma), 2, mpdu, 28,
			  OB_RX_STATUS1_PAD_PRESENT);
	CHECK("pad vector built", total == OB_RX_D11_REV42_PREFIX_LEN + 40u);
	ret = ob_rx_d11_rev42_decode_length(42, dma, 2, total, &length);
	CHECK("pad vector length decode", ret == 0 &&
	      length.rx_frame_size == 40u && total == length.required_dma_bytes);
	ret = ob_rx_status_decode(dma, OB_RX_D11_REV42_PREFIX_LEN, &decoded);
	CHECK("pad vector status decode", ret == 0 && decoded.pad_present);
	ret = ob_rx_frame_bounds(&decoded, &bounds);
	CHECK("pad vector bounds", ret == 0 && bounds.pad_len == 2u &&
	      bounds.phy_header_len == OB_RX_PLCP_LEN &&
	      bounds.fcs_len == OB_RX_FCS_LEN && bounds.mpdu_len == 28u);
	off = OB_RX_D11_REV42_PREFIX_LEN + bounds.pad_len +
	      bounds.phy_header_len;
	init_meta(&meta, bounds.mpdu_len, 0);
	CHECK("pad vector envelope at exact 28", off + 28u == total - 4u &&
	      ob_rx_sw_validate(dma + off, bounds.mpdu_len, &meta, &status) == 0);

	/* 12: the FCS is counted exactly once. */
	mpdu[0] = 0x08;
	mpdu[1] = 0x00;	/* data, subtype 0 */
	total = build_dma(dma, sizeof(dma), 0, mpdu, 24, 0);
	CHECK("FCS vector built", total == OB_RX_D11_REV42_PREFIX_LEN + 34u);
	ret = ob_rx_d11_rev42_decode_length(42, dma, 2, total, &length);
	CHECK("FCS vector length decode", ret == 0 &&
	      length.rx_frame_size == 34u);
	ret = ob_rx_status_decode(dma, OB_RX_D11_REV42_PREFIX_LEN, &decoded);
	CHECK("FCS vector status decode", ret == 0 && !decoded.pad_present);
	ret = ob_rx_frame_bounds(&decoded, &bounds);
	CHECK("FCS vector bounds exclude the tail", ret == 0 &&
	      bounds.mpdu_len == 24u && bounds.fcs_len == 4u);
	off = OB_RX_D11_REV42_PREFIX_LEN + bounds.phy_header_len;
	init_meta(&meta, 24, 0);
	CHECK("stripped MPDU validates without an FCS allowance",
	      ob_rx_sw_validate(dma + off, 24, &meta, &status) == 0);
	init_meta(&meta, 28, 0);
	meta.fcs_len = 4;
	CHECK("same frame with its FCS validates with one four-byte allowance",
	      ob_rx_sw_validate(dma + off, 28, &meta, &status) == 0);
	init_meta(&meta, 24, 0);
	meta.fcs_len = 4;
	CHECK("FCS allowance is not double-counted",
	      ob_rx_sw_validate(dma + off, 24, &meta, &status) == -EMSGSIZE);
	init_meta(&meta, 23, 0);
	CHECK("one byte short of the data minimum is rejected",
	      ob_rx_sw_validate(dma + off, 23, &meta, &status) == -EMSGSIZE);
	CHECK("vendor gate accepts 34 and rejects 33 total bytes",
	      ob_rx_len_plausible(34, 0) && !ob_rx_len_plausible(33, 0));
	CHECK("vendor gate accepts 38 and rejects 37 with the field",
	      ob_rx_len_plausible(38, 1) && !ob_rx_len_plausible(37, 1));

	/* 13: a declared RxFrameSize that exceeds the DMA buffer is visible. */
	put_le16(dma + OB_RX_STATUS_OFF_FRAME_SIZE, 41u);
	ret = ob_rx_d11_rev42_decode_length(42, dma, 2, total, &length);
	CHECK("oversized RxFrameSize asks for a chain",
	      ret == 0 && length.rx_frame_size == 41u &&
	      length.layout == OB_RX_D11_REV42_NEEDS_CHAIN &&
	      length.required_dma_bytes == OB_RX_D11_REV42_PREFIX_LEN + 41u);
	put_le16(dma + OB_RX_STATUS_OFF_FRAME_SIZE, 34u);
	ret = ob_rx_d11_rev42_decode_length(42, dma, 2, total - 1u, &length);
	CHECK("truncated DMA buffer asks for a chain",
	      ret == 0 && length.layout == OB_RX_D11_REV42_NEEDS_CHAIN);
}

static void test_no_fabricated_metadata(void)
{
	ob_rx_sw_u8 frame[28];
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status status;

	memset(frame, 0, sizeof(frame));
	frame[0] = 0x80;
	frame[1] = 0x80;
	init_meta(&meta, sizeof(frame), 0);
	CHECK("unproven metadata vector validates",
	      ob_rx_sw_validate(frame, sizeof(frame), &meta, &status) == 0);
	CHECK("no channel is synthesized",
	      status.channel == 0 && !(status.valid_fields & OB_RX_SW_META_CHANNEL));
	CHECK("no rate is synthesized",
	      status.rate_index == 0 && status.rate_is_ht == 0 &&
	      !(status.valid_fields & OB_RX_SW_META_RATE));
	CHECK("only the trusted FCS fact crosses the boundary",
	      status.valid_fields == OB_RX_SW_META_FCS_LEN &&
	      status.fcs_len == 0);
}

int main(void)
{
	test_header_lengths();
	test_pad_and_fcs();
	test_no_fabricated_metadata();

	if (failures) {
		printf("rx length contract: %u failures\n", failures);
		return 1;
	}
	printf("rx length contract: %u checks, PASS (mac80211 header lengths, pad/FCS, no PHY inference)\n",
	       checks_run);
	return 0;
}

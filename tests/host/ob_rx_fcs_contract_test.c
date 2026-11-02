// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests for the Phase 36B FCS contract (BCM4352 / D11 rev42).
 *
 * The pinned vendor object proves that RxFrameSize counts the 4-byte 802.11
 * FCS (wlc_recvdata rejects counts <= 0x21, i.e. 6 PHY + 24 MAC + 4 FCS) and
 * that the host removes those four tail bytes before any frame is handed up
 * (osl_pktsetlen(osl_pktlen - 4) at 0x440aa, osl_pktpull(6) + osl_pktsetlen
 * (len - 0xa) at 0x4c733/0x4c746). These vectors pin the arithmetic on both
 * sides of that boundary. No hardware, no DMA, no mac80211 call.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_rx_d11_rev42.h"
#include "../../src/ob_rx_status.h"
#include "../../src/ob_rx_sw.h"

static unsigned int failures;

#define CHECK(name, expr) do { \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

static void put_le16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

/* 40-byte rev42 prefix with only the fields the decoder consumes. */
static void make_prefix(unsigned char *p, unsigned int frame_size,
			unsigned int status1)
{
	memset(p, 0, 40);
	put_le16(p + OB_RX_STATUS_OFF_FRAME_SIZE, frame_size);
	put_le16(p + OB_RX_STATUS_OFF_STATUS1, status1);
}

/* Bounds for one RxFrameSize / RxStatus1 pair; 0 on success. */
static int bounds_of(unsigned int frame_size, unsigned int status1,
		     struct ob_rx_frame_bounds *b)
{
	unsigned char prefix[40];
	struct ob_rx_status st;

	make_prefix(prefix, frame_size, status1);
	if (ob_rx_status_decode(prefix, sizeof(prefix), &st) != 0)
		return -EINVAL;
	return ob_rx_frame_bounds(&st, b);
}

/* A minimal data frame: FC = 0x0008 (type data, subtype 0), 24 bytes. */
static void make_mpdu(unsigned char *frame, unsigned int len)
{
	memset(frame, 0, len);
	if (len >= 2) {
		frame[0] = 0x08;
		frame[1] = 0x00;
	}
}

static void set_meta(struct ob_rx_sw_metadata *meta, unsigned int declared,
		     unsigned int fcs_len)
{
	memset(meta, 0, sizeof(*meta));
	meta->provenance = OB_RX_SW_PROVENANCE_VALIDATED;
	meta->valid_fields = OB_RX_SW_META_FCS_LEN;
	meta->declared_len = declared;
	meta->complete = 1;
	meta->fcs_len = (ob_rx_sw_u8)fcs_len;
}

/*
 * Vector 1: frame with FCS. RxFrameSize = 6 + 24 + 4 = 34 (no pad), so the
 * MPDU window is 24 bytes and the same buffer still carries the FCS when the
 * caller declares fcs_len = 4.
 */
static void test_fcs_present(void)
{
	struct ob_rx_frame_bounds b;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status st;
	unsigned char frame[28];

	make_mpdu(frame, sizeof(frame));
	CHECK("with FCS: bounds accept the smallest counted frame",
	      bounds_of(34, 0, &b) == 0);
	CHECK("with FCS: pad is zero", b.pad_len == 0);
	CHECK("with FCS: phy header is six bytes", b.phy_header_len == 6);
	CHECK("with FCS: four bytes are the FCS", b.fcs_len == 4);
	CHECK("with FCS: MPDU window is the bare header", b.mpdu_len == 24);

	set_meta(&meta, 24, 0);
	CHECK("with FCS: trimmed frame validates",
	      ob_rx_sw_validate(frame, 24, &meta, &st) == 0 &&
	      st.fcs_len == 0);

	/* The DMA buffer before the strip: MPDU + FCS, declared honestly. */
	set_meta(&meta, 28, 4);
	CHECK("with FCS: untrimmed buffer is recorded, not hidden",
	      ob_rx_sw_validate(frame, 28, &meta, &st) == 0 &&
	      st.fcs_len == 4);

	/* Padded variant: 2 pad bytes replace nothing in the arithmetic. */
	CHECK("with FCS and pad: window shrinks by the pad only",
	      bounds_of(36, OB_RX_STATUS1_PAD_PRESENT, &b) == 0 &&
	      b.pad_len == 2 && b.mpdu_len == 24);
}

/*
 * Vector 2: frame without FCS. A descriptor that counted only 6 + 24 bytes
 * cannot describe a 24-byte MPDU once the proven layout is applied: the window
 * loses four bytes and the envelope refuses it. The parser never guesses that
 * the FCS might be absent.
 */
static void test_fcs_absent(void)
{
	struct ob_rx_frame_bounds b;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status st;
	unsigned char frame[24];

	make_mpdu(frame, sizeof(frame));
	CHECK("without FCS: bounds still subtract four bytes",
	      bounds_of(30, 0, &b) == 0 && b.mpdu_len == 20);
	set_meta(&meta, 20, 0);
	CHECK("without FCS: shortened frame is rejected",
	      ob_rx_sw_validate(frame, 20, &meta, &st) == -EMSGSIZE);
	CHECK("without FCS: rejection publishes no status",
	      st.valid_fields == 0 && st.fcs_len == 0);

	/* Same for the pad case: 2 + 6 + 24 leaves 20 bytes of MPDU. */
	CHECK("without FCS and with pad: window is 20 bytes",
	      bounds_of(32, OB_RX_STATUS1_PAD_PRESENT, &b) == 0 &&
	      b.mpdu_len == 20);

	/* The length helper cannot accept a count that leaves no full header. */
	CHECK("without FCS: gate rejects a 30-byte count",
	      ob_rx_len_plausible(30, 0) == 0);
	CHECK("without FCS: gate accepts the 34-byte minimum",
	      ob_rx_len_plausible(34, 0) != 0);
}

/*
 * Vector 3: truncated frame. Counts below pad + 6 + 4 cannot describe any
 * MPDU and are refused with -ERANGE; a zero-length window is refused by the
 * envelope rather than delivered.
 */
static void test_truncated(void)
{
	struct ob_rx_frame_bounds b;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status st;
	unsigned char frame[24];

	make_mpdu(frame, sizeof(frame));

	CHECK("truncated: nine bytes cannot hold the layout",
	      bounds_of(9, 0, &b) == -ERANGE);
	CHECK("truncated: failure zeroes the bounds",
	      b.mpdu_len == 0 && b.fcs_len == 0);
	CHECK("truncated: ten bytes leave a zero-length window",
	      bounds_of(10, 0, &b) == 0 && b.mpdu_len == 0);
	set_meta(&meta, 0, 0);
	CHECK("truncated: zero-length frame is not delivered",
	      ob_rx_sw_validate(frame, 0, &meta, &st) == -EMSGSIZE);

	CHECK("truncated: pad on a ten-byte count is impossible",
	      bounds_of(11, OB_RX_STATUS1_PAD_PRESENT, &b) == -ERANGE);
	CHECK("truncated: pad leaves 21 bytes, below the header",
	      bounds_of(33, OB_RX_STATUS1_PAD_PRESENT, &b) == 0 &&
	      b.mpdu_len == 21);
	set_meta(&meta, 21, 0);
	CHECK("truncated: 21-byte MPDU is refused",
	      ob_rx_sw_validate(frame, 21, &meta, &st) == -EMSGSIZE);

	CHECK("truncated: gate rejects a count below 0xa",
	      ob_rx_len_plausible(9, 0) == 0);
	CHECK("truncated: gate requires four more bytes when the flag is set",
	      ob_rx_len_plausible(37, 1) == 0 &&
	      ob_rx_len_plausible(38, 1) != 0);
}

/*
 * Vector 4: contradictory descriptor metadata. Nothing is repaired by
 * guessing: a length that disagrees with the buffer, an FCS length that is not
 * four, or a payload that overruns the available bytes all fail closed.
 */
static void test_contradictory(void)
{
	struct ob_rx_frame_bounds b;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status st;
	struct ob_rx_d11_rev42_input in;
	struct ob_rx_d11_rev42_view view;
	struct ob_rx_d11_rev42_length length;
	unsigned char buffer[40 + 30];
	unsigned char frame[24];

	make_mpdu(frame, sizeof(frame));

	/* RxStatus1 claims pad bytes that the count cannot contain. */
	CHECK("contradictory: pad bit with an 8-byte count is refused",
	      bounds_of(8, OB_RX_STATUS1_PAD_PRESENT, &b) == -ERANGE);

	/* The caller's declared length disagrees with the buffer it passed. */
	set_meta(&meta, 24, 0);
	CHECK("contradictory: declared length must equal the buffer",
	      ob_rx_sw_validate(frame, 28, &meta, &st) == -EMSGSIZE);

	/* An FCS length other than the proven four is not a policy choice. */
	set_meta(&meta, 24, 2);
	CHECK("contradictory: two-byte FCS is refused",
	      ob_rx_sw_validate(frame, 24, &meta, &st) == -EINVAL);

	/* Missing the FCS provenance fact is refused, not defaulted. */
	set_meta(&meta, 24, 0);
	meta.valid_fields = 0;
	CHECK("contradictory: missing FCS provenance is refused",
	      ob_rx_sw_validate(frame, 24, &meta, &st) == -ENODATA);

	/* A descriptor payload that overruns the DMA buffer is refused. */
	memset(buffer, 0, sizeof(buffer));
	put_le16(buffer, 34);
	memset(&in, 0, sizeof(in));
	in.d11_core_revision = 42;
	in.length_provenance = OB_RX_D11_LENGTH_VALIDATED;
	in.payload_length = 34;
	in.complete = 1;
	CHECK("contradictory: payload overruns a 30-byte window",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &in,
					   &view) == -EMSGSIZE);
	CHECK("contradictory: refused view publishes no payload",
	      view.payload == NULL && view.payload_length == 0);

	/* The same prefix with an honest length is accepted. */
	in.payload_length = 30;
	CHECK("contradictory: honest length is accepted",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &in,
					   &view) == 0 &&
	      view.payload == buffer + OB_RX_D11_REV42_PREFIX_LEN &&
	      view.payload_length == 30);

	/* RxFrameSize drives the DMA requirement, FCS included. */
	CHECK("contradictory: RxFrameSize maps to 40 + count",
	      ob_rx_d11_rev42_decode_length(42, buffer, sizeof(buffer), 128,
					    &length) == 0 &&
	      length.rx_frame_size == 34 &&
	      length.required_dma_bytes == OB_RX_D11_REV42_PREFIX_LEN + 34 &&
	      length.layout == OB_RX_D11_REV42_SINGLE_BUFFER);
	CHECK("contradictory: one byte short needs a chain",
	      ob_rx_d11_rev42_decode_length(42, buffer, sizeof(buffer), 73,
					    &length) == 0 &&
	      length.layout == OB_RX_D11_REV42_NEEDS_CHAIN);
}

int main(void)
{
	test_fcs_present();
	test_fcs_absent();
	test_truncated();
	test_contradictory();
	if (failures) {
		printf("ob_rx_fcs_contract_test: %u failure(s)\n", failures);
		return 1;
	}
	printf("ob_rx_fcs_contract_test: all vectors pass\n");
	return 0;
}

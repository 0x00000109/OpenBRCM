// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phase 36E — integrated BCM4352 / D11 rev42 RX metadata path.
 *
 * One synthetic DMA buffer is decoded by the production functions in order:
 *
 *   ob_rx_d11_rev42_decode_length()
 *   ob_rx_status_decode()
 *   ob_rx_chan_decode()
 *   ob_rx_frame_bounds()
 *   ob_rx_rate_decode()
 *   ob_rx_rate_to_mac80211()
 *   ob_rx_sw_validate()
 *   ob_rx_mac80211_translate() / ob_rx_mac80211_handoff()
 *
 * No decoder is reimplemented here. The test only supplies the fake DMA
 * buffer, the fake skb and the fake mac80211 sink. Signal strength, decryption
 * and every other unproven fact stay absent.
 *
 * Offline only: no hardware, no DMA, no module load, no live RX/TX.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_rx_d11_rev42.h"
#include "../../src/ob_rx_mac80211.h"
#include "../../src/ob_rx_status.h"
#include "../../src/ob_rx_sw.h"

static unsigned int failures;

#define CHECK(name, expr) do { \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

#define PREFIX_LEN	OB_RX_D11_REV42_PREFIX_LEN
#define PLCP_LEN	OB_RX_PLCP_LEN
#define FCS_LEN		OB_RX_FCS_LEN

static void put_le16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xffu);
	p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void make_mpdu(unsigned char *frame, size_t len)
{
	memset(frame, 0, len);
	if (len >= 2) {
		frame[0] = 0x08;	/* data, subtype 0 */
		frame[1] = 0x00;
	}
}

/*
 * Build the exact single-buffer layout proven for rev42:
 *
 *   40-byte D11 prefix | pad (0 or 2) | 6-byte PHY header |
 *   24-byte MPDU | 4-byte FCS
 *
 * RxFrameSize covers everything after the prefix, including the FCS.
 */
static size_t build_dma(unsigned char *dma, size_t capacity,
			unsigned int chan_raw, unsigned int phy0,
			unsigned int status1, unsigned int status2,
			const unsigned char *plcp, size_t plcp_len,
			const unsigned char *mpdu, size_t mpdu_len,
			unsigned int pad)
{
	size_t frame_size, total, off;

	if (!dma || !plcp || !mpdu || plcp_len != PLCP_LEN ||
	    mpdu_len < 24u || (pad != 0u && pad != 2u))
		return 0;

	frame_size = (size_t)pad + PLCP_LEN + mpdu_len + FCS_LEN;
	total = PREFIX_LEN + frame_size;
	if (total > capacity)
		return 0;

	memset(dma, 0, total);
	put_le16(dma + OB_RX_STATUS_OFF_FRAME_SIZE, (unsigned int)frame_size);
	put_le16(dma + OB_RX_STATUS_OFF_PHY_0, phy0);
	put_le16(dma + OB_RX_STATUS_OFF_STATUS1, status1);
	put_le16(dma + OB_RX_STATUS_OFF_STATUS2, status2);
	put_le16(dma + OB_RX_STATUS_OFF_CHAN, chan_raw);

	off = PREFIX_LEN + pad;
	memcpy(dma + off, plcp, PLCP_LEN);
	memcpy(dma + off + PLCP_LEN, mpdu, mpdu_len);
	memset(dma + off + PLCP_LEN + mpdu_len, 0xde, FCS_LEN);
	return total;
}

struct flow {
	struct ob_rx_d11_rev42_length length;
	struct ob_rx_status status;
	struct ob_rx_chan chan;
	struct ob_rx_frame_bounds bounds;
	struct ob_rx_rate rate;
	struct ob_rx_rate_80211 r80211;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_sw_status envelope;
	struct ob_rx_mac80211_evidence evidence;
	struct ob_rx_mac80211_report report;
	size_t plcp_off;
	size_t mpdu_off;
	size_t mpdu_len;
};

/*
 * Run the complete production path. Metadata and evidence are populated only
 * by successful decoders; a missing or refused fact stops the flow.
 */
static int run_flow(const unsigned char *dma, size_t dma_len, struct flow *f)
{
	int ret;

	memset(f, 0, sizeof(*f));
	if (!dma || dma_len < PREFIX_LEN)
		return -EMSGSIZE;

	ret = ob_rx_d11_rev42_decode_length(42, dma, dma_len, dma_len,
					    &f->length);
	if (ret)
		return ret;
	if (f->length.layout != OB_RX_D11_REV42_SINGLE_BUFFER ||
	    f->length.required_dma_bytes > dma_len)
		return -EMSGSIZE;

	ret = ob_rx_status_decode(dma, PREFIX_LEN, &f->status);
	if (ret)
		return ret;
	if (f->length.rx_frame_size != f->status.rx_frame_size)
		return -EINVAL;

	/* The vendor drops these before delivery; do not decode past them. */
	if (f->status.fcs_error ||
	    (f->status.phy_rx_status_0 & OB_RX_PHY0_DROP_MASK))
		return -EBADMSG;

	ret = ob_rx_chan_decode(42, f->status.rx_chan, &f->chan);
	if (ret)
		return ret;

	ret = ob_rx_frame_bounds(&f->status, &f->bounds);
	if (ret)
		return ret;
	if (f->bounds.rx_frame_size != f->length.rx_frame_size)
		return -EINVAL;

	f->plcp_off = PREFIX_LEN + f->bounds.pad_len;
	if (f->plcp_off + f->bounds.phy_header_len +
	    f->bounds.mpdu_len + f->bounds.fcs_len > dma_len)
		return -EMSGSIZE;
	f->mpdu_off = f->plcp_off + f->bounds.phy_header_len;
	f->mpdu_len = f->bounds.mpdu_len;

	ret = ob_rx_rate_decode(&f->status, dma + f->plcp_off,
				f->bounds.phy_header_len, &f->rate);
	if (ret)
		return ret;
	ret = ob_rx_rate_to_mac80211(&f->rate, f->chan.band_5g, &f->r80211);
	if (ret)
		return ret;

	/* Only facts produced by the decoders above cross the envelope. */
	f->meta.provenance = OB_RX_SW_PROVENANCE_VALIDATED;
	f->meta.valid_fields = OB_RX_SW_META_FCS_LEN |
		OB_RX_SW_META_CHANNEL | OB_RX_SW_META_RATE;
	if (f->r80211.is_ht)
		f->meta.valid_fields |= OB_RX_SW_META_RATE_HT;
	f->meta.declared_len = f->mpdu_len;
	f->meta.complete = 1;
	f->meta.channel = f->chan.chan;
	f->meta.rate_index = f->r80211.rate_idx;
	f->meta.rate_bw40 = f->r80211.bw40;
	f->meta.rate_sgi = f->r80211.sgi;
	f->meta.fcs_len = 0;	/* the four tail bytes are outside mpdu_len */

	f->evidence.proven = OB_RX_MAC80211_FACT_FCS_PROVEN |
		OB_RX_MAC80211_FACT_CHANNEL_PROVEN |
		OB_RX_MAC80211_FACT_RATE_PROVEN;
	f->evidence.fcs_present = 0;

	ret = ob_rx_sw_validate(dma + f->mpdu_off, f->mpdu_len, &f->meta,
				&f->envelope);
	if (ret)
		return ret;
	ret = ob_rx_mac80211_translate(&f->envelope, &f->evidence, &f->report);
	if (ret)
		return ret;
	return 0;
}

static void test_valid_cck_channel1(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x0a, 0, 0, 0, 0, 0 };
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x1001, OB_RX_PHY0_TYPE_CCK,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("CCK channel 1 buffer built", len == PREFIX_LEN + 34u);

	CHECK("CCK channel 1 full flow",
	      run_flow(dma, len, &f) == 0);
	CHECK("CCK frame size covers PHY + MPDU + FCS",
	      f.length.rx_frame_size == 34u &&
	      f.length.required_dma_bytes == len);
	CHECK("CCK channel decoded",
	      f.chan.chan == 1 && f.chan.band_5g == 0);
	CHECK("CCK bounds exclude pad, PHY and FCS",
	      f.bounds.pad_len == 0 && f.bounds.phy_header_len == PLCP_LEN &&
	      f.bounds.fcs_len == FCS_LEN && f.bounds.mpdu_len == 24u);
	CHECK("CCK rate decoded from PLCP",
	      f.rate.kind == OB_RX_RATE_CCK &&
	      f.rate.legacy_rate_500kbps == 2u);
	CHECK("CCK maps to legacy index 0",
	      f.r80211.rate_idx == 0 && f.r80211.is_ht == 0);
	CHECK("CCK envelope strips the FCS",
	      f.envelope.fcs_len == 0 &&
	      f.envelope.valid_fields == (OB_RX_SW_META_FCS_LEN |
					 OB_RX_SW_META_CHANNEL |
					 OB_RX_SW_META_RATE));
	CHECK("CCK report is channel 1 / rate 0",
	      f.report.channel == 1 && f.report.rate_index == 0 &&
	      f.report.rate_is_ht == 0 && f.report.bw40 == 0 &&
	      f.report.sgi == 0);
	CHECK("CCK keeps signal unknown",
	      (f.report.flags & OB_RX_MAC80211_REPORT_SIGNAL_VALID) == 0 &&
	      f.report.signal_dbm == 0);
	CHECK("CCK MPDU starts at the proven offset",
	      f.mpdu_off == PREFIX_LEN + PLCP_LEN &&
	      f.mpdu_len == 24u && dma[f.mpdu_off] == 0x08);
}

static void test_valid_ofdm_channel6(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	/*
	 * Vendor table index 4 is 54 Mbps, not 24 Mbps. If the table were
	 * treated as ascending this vector would map to mac80211 index 6.
	 */
	unsigned char plcp[6] = { 0x04, 0, 0, 0, 0, 0 };
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x1006, OB_RX_PHY0_TYPE_OFDM,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("OFDM channel 6 buffer built", len == PREFIX_LEN + 34u);

	CHECK("OFDM channel 6 full flow",
	      run_flow(dma, len, &f) == 0);
	CHECK("OFDM channel decoded",
	      f.chan.chan == 6 && f.chan.band_5g == 0);
	CHECK("OFDM non-ascending table gives 54 Mbps",
	      f.rate.kind == OB_RX_RATE_OFDM &&
	      f.rate.legacy_rate_500kbps == 108u);
	CHECK("OFDM 54 Mbps maps to 2.4 GHz index 11",
	      f.r80211.rate_idx == 11 && f.r80211.is_ht == 0);
	CHECK("OFDM report is channel 6 / rate 11",
	      f.report.channel == 6 && f.report.rate_index == 11 &&
	      f.report.rate_is_ht == 0);
	CHECK("OFDM leaves signal absent",
	      (f.report.flags & OB_RX_MAC80211_REPORT_SIGNAL_VALID) == 0);
}

static void test_valid_ht20_channel11(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x07, 0, 0, 0, 0, 0 };
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x100b, OB_RX_PHY0_TYPE_HT,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("HT20 channel 11 buffer built", len == PREFIX_LEN + 34u);

	CHECK("HT20 channel 11 full flow",
	      run_flow(dma, len, &f) == 0);
	CHECK("HT20 channel decoded",
	      f.chan.chan == 11 && f.chan.band_5g == 0);
	CHECK("HT20 MCS7 decoded",
	      f.rate.kind == OB_RX_RATE_HT && f.rate.mcs == 7u &&
	      f.rate.bw40 == 0 && f.rate.sgi == 0);
	CHECK("HT20 uses the MCS index space",
	      f.r80211.rate_idx == 7 && f.r80211.is_ht == 1 &&
	      f.r80211.bw40 == 0);
	CHECK("HT20 envelope carries the HT marker",
	      (f.envelope.valid_fields & OB_RX_SW_META_RATE_HT) != 0 &&
	      f.envelope.rate_is_ht == 1);
	CHECK("HT20 report is MCS7, not legacy index 7",
	      f.report.channel == 11 && f.report.rate_index == 7 &&
	      f.report.rate_is_ht == 1 && f.report.bw40 == 0 &&
	      f.report.sgi == 0);
}

static void test_missing_fcs_evidence(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x0a, 0, 0, 0, 0, 0 };
	struct ob_rx_sw_metadata meta;
	struct ob_rx_mac80211_evidence ev;
	struct ob_rx_mac80211_report report;
	struct ob_rx_sw_status env;
	struct ob_rx_sw_status no_fcs;
	struct ob_rx_frame_bounds bounds;
	struct ob_rx_status status;
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x1001, OB_RX_PHY0_TYPE_CCK,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("FCS-evidence source flow", run_flow(dma, len, &f) == 0);

	meta = f.meta;
	meta.valid_fields &= ~OB_RX_SW_META_FCS_LEN;
	CHECK("envelope refuses missing FCS fact",
	      ob_rx_sw_validate(dma + f.mpdu_off, f.mpdu_len, &meta,
				&env) == -ENODATA);

	ev = f.evidence;
	ev.proven &= ~OB_RX_MAC80211_FACT_FCS_PROVEN;
	CHECK("adapter refuses missing FCS evidence",
	      ob_rx_mac80211_translate(&f.envelope, &ev, &report) ==
	      -ENODATA);
	CHECK("missing FCS evidence zeroes report",
	      report.channel == 0 && report.rate_index == 0);

	/* The value zero is only proven zero when its envelope fact is set. */
	no_fcs = f.envelope;
	no_fcs.valid_fields &= ~OB_RX_SW_META_FCS_LEN;
	CHECK("adapter refuses missing FCS envelope fact",
	      ob_rx_mac80211_translate(&no_fcs, &f.evidence, &report) ==
	      -ENODATA);

	status = f.status;
	status.verified &= ~OB_RX_FACT_FRAME_SIZE;
	CHECK("bounds refuse missing frame-size fact",
	      ob_rx_frame_bounds(&status, &bounds) == -EOPNOTSUPP);
	CHECK("bounds failure zeroes output", bounds.mpdu_len == 0);
}

static void test_truncated_buffer(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x0a, 0, 0, 0, 0, 0 };
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x1001, OB_RX_PHY0_TYPE_CCK,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("truncation source flow", run_flow(dma, len, &f) == 0);
	CHECK("one byte short refuses the DMA layout",
	      run_flow(dma, len - 1u, &f) == -EMSGSIZE);
	CHECK("truncated flow did not emit a report",
	      f.report.channel == 0 && f.report.rate_index == 0);
	CHECK("prefix-only buffer refused",
	      run_flow(dma, PREFIX_LEN - 1u, &f) == -EMSGSIZE);
}

static void test_missing_channel_evidence(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x0a, 0, 0, 0, 0, 0 };
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x0000, OB_RX_PHY0_TYPE_CCK,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("missing RxChan refuses the flow",
	      run_flow(dma, len, &f) == -ERANGE);
	CHECK("missing channel leaves no report",
	      f.report.channel == 0 && f.report.rate_index == 0);
}

static void test_contradictory_channel_band(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x0a, 0, 0, 0, 0, 0 };
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	/* 5 GHz band bits with a 2.4 GHz channel number. */
	len = build_dma(dma, sizeof(dma), 0xd006, OB_RX_PHY0_TYPE_CCK,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("contradictory channel/band refuses the flow",
	      run_flow(dma, len, &f) == -ERANGE);
	CHECK("contradictory channel leaves no report",
	      f.report.channel == 0);

	/* A 5 GHz number with no 5 GHz band selector is equally invalid. */
	len = build_dma(dma, sizeof(dma), 0x0024, OB_RX_PHY0_TYPE_OFDM,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("5 GHz number in 2.4 GHz form refused",
	      run_flow(dma, len, &f) == -ERANGE);
}

static void test_missing_phy_type_evidence(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x0a, 0, 0, 0, 0, 0 };
	struct ob_rx_status status;
	struct ob_rx_rate rate;
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x1001, OB_RX_PHY0_TYPE_CCK,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("PHY-evidence source flow", run_flow(dma, len, &f) == 0);

	status = f.status;
	status.verified &= ~OB_RX_FACT_PHY_TYPE;
	CHECK("rate decoder refuses missing PHY-type fact",
	      ob_rx_rate_decode(&status, dma + f.plcp_off,
				PLCP_LEN, &rate) == -EOPNOTSUPP);
	CHECK("missing PHY-type leaves rate unknown",
	      rate.kind == OB_RX_RATE_UNKNOWN &&
	      rate.legacy_rate_500kbps == 0 && rate.mcs == 0);
}

static void test_unsupported_vht(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0, 0, 0, 0, 0, 0 };
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x1006, OB_RX_PHY0_TYPE_VHT,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("VHT is refused rather than guessed",
	      run_flow(dma, len, &f) == -EOPNOTSUPP);
	CHECK("VHT refusal leaves no report",
	      f.report.channel == 0 && f.report.rate_index == 0);
}

static void test_unsupported_ht_mcs(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0xa0, 0, 0, 0, 0, 0 }; /* MCS 32 */
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x100b, OB_RX_PHY0_TYPE_HT,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("HT MCS 32 is outside the mac80211 index space",
	      run_flow(dma, len, &f) == -EOPNOTSUPP);
	CHECK("HT MCS 32 decodes but does not translate",
	      f.rate.kind == OB_RX_RATE_HT && f.rate.mcs == 32u &&
	      f.r80211.rate_idx == 0 && f.r80211.is_ht == 0);
}

static void test_ht40_policy(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x87, 0, 0, 0, 0, 0 }; /* MCS7, 40 MHz */
	struct flow f;
	size_t len;

	CHECK("HT40 reporting is not advertised",
	      OB_RX_MAC80211_HT40_SUPPORTED == 0u);
	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x100b, OB_RX_PHY0_TYPE_HT,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("HT40 is decoded but refused by the HT20-only adapter",
	      run_flow(dma, len, &f) == -EOPNOTSUPP);
	CHECK("HT40 reached the rate decoder",
	      f.rate.kind == OB_RX_RATE_HT && f.rate.mcs == 7u &&
	      f.rate.bw40 == 1);
	CHECK("HT40 mapper preserved the unsupported width",
	      f.r80211.rate_idx == 7 && f.r80211.is_ht == 1 &&
	      f.r80211.bw40 == 1);
	CHECK("HT40 refusal emitted no report",
	      f.report.channel == 0 && f.report.rate_index == 0);
	CHECK("live delivery remains disabled",
	      ob_rx_mac80211_live_delivery_enabled() == false);
}

struct fake_skb {
	unsigned int marker;
};

struct fake_sink {
	unsigned int delivered;
	unsigned int freed;
	unsigned int consumptions;
	unsigned int duplicate;
	void *last_skb;
	struct ob_rx_mac80211_report last_report;
};

/* Each integration call uses one skb; any second callback is a duplicate. */
static void note_consumption(struct fake_sink *s, void *skb)
{
	if (s->consumptions)
		s->duplicate = 1;
	s->consumptions++;
	s->last_skb = skb;
}

static void fake_deliver(void *ctx, void *skb,
			 const struct ob_rx_mac80211_report *report)
{
	struct fake_sink *s = ctx;

	note_consumption(s, skb);
	s->delivered++;
	s->last_report = *report;
}

static void fake_free(void *ctx, void *skb)
{
	struct fake_sink *s = ctx;

	note_consumption(s, skb);
	s->freed++;
}

static const struct ob_rx_mac80211_ops fake_ops = {
	.deliver = fake_deliver,
	.free = fake_free,
};

static void check_handoff_failure(const char *name,
				  const struct ob_rx_sw_status *status,
				  const struct ob_rx_mac80211_evidence *ev,
				  int want_translate)
{
	struct ob_rx_mac80211_report report;
	struct fake_skb skb = { .marker = 0x36e00001u };
	struct fake_sink sink;

	CHECK("translation code preserved",
	      ob_rx_mac80211_translate(status, ev, &report) == want_translate);
	memset(&sink, 0, sizeof(sink));
	CHECK(name, ob_rx_mac80211_handoff(&skb, status, ev, &fake_ops,
					   &sink) ==
	      OB_RX_MAC80211_HANDOFF_CONSUMED);
	CHECK("failed handoff consumes exactly once",
	      sink.freed == 1 && sink.delivered == 0 &&
	      sink.consumptions == 1 && sink.duplicate == 0);
	CHECK("failed handoff preserves skb identity",
	      sink.last_skb == &skb && skb.marker == 0x36e00001u);
}

static void test_handoff_exactly_once(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x07, 0, 0, 0, 0, 0 };
	struct ob_rx_mac80211_evidence ev;
	struct ob_rx_sw_status bad;
	struct fake_skb skb = { .marker = 0x36e00002u };
	struct fake_sink sink;
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x100b, OB_RX_PHY0_TYPE_HT,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("handoff source flow", run_flow(dma, len, &f) == 0);

	memset(&sink, 0, sizeof(sink));
	CHECK("successful handoff", ob_rx_mac80211_handoff(
		&skb, &f.envelope, &f.evidence, &fake_ops, &sink) == 0);
	CHECK("successful handoff delivers exactly once",
	      sink.delivered == 1 && sink.freed == 0 &&
	      sink.consumptions == 1 && sink.duplicate == 0);
	CHECK("successful handoff transfers the same skb",
	      sink.last_skb == &skb && skb.marker == 0x36e00002u);
	CHECK("successful handoff preserves the HT report",
	      sink.last_report.channel == 11 &&
	      sink.last_report.rate_index == 7 &&
	      sink.last_report.rate_is_ht == 1 &&
	      sink.last_report.bw40 == 0);

	ev = f.evidence;
	ev.proven &= ~OB_RX_MAC80211_FACT_FCS_PROVEN;
	check_handoff_failure("FCS-evidence failure", &f.envelope, &ev,
			      -ENODATA);

	ev = f.evidence;
	ev.proven &= ~OB_RX_MAC80211_FACT_CHANNEL_PROVEN;
	check_handoff_failure("channel-evidence failure", &f.envelope, &ev,
			      -ENODATA);

	ev = f.evidence;
	ev.proven &= ~OB_RX_MAC80211_FACT_RATE_PROVEN;
	check_handoff_failure("rate-evidence failure", &f.envelope, &ev,
			      -ENODATA);

	ev = f.evidence;
	ev.proven |= (1u << 7);
	check_handoff_failure("unknown evidence bit failure", &f.envelope,
			      &ev, -EINVAL);

	bad = f.envelope;
	bad.rate_index = 32;
	check_handoff_failure("unsupported HT MCS failure", &bad, &f.evidence,
			      -ERANGE);

	/* The adapter policy failure also consumes the skb exactly once. */
	plcp[0] = 0x87;
	len = build_dma(dma, sizeof(dma), 0x100b, OB_RX_PHY0_TYPE_HT,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("HT40 handoff source flow",
	      run_flow(dma, len, &f) == -EOPNOTSUPP);
	check_handoff_failure("HT40 policy failure", &f.envelope, &f.evidence,
			      -EOPNOTSUPP);

	/* Invalid ops are a programming error: caller still owns the skb. */
	memset(&sink, 0, sizeof(sink));
	CHECK("missing ops rejected",
	      ob_rx_mac80211_handoff(&skb, &f.envelope, &f.evidence, NULL,
				     &sink) == -EINVAL);
	CHECK("missing ops does not consume", sink.freed == 0 &&
	      sink.delivered == 0 && sink.consumptions == 0);
	CHECK("null skb rejected",
	      ob_rx_mac80211_handoff(NULL, &f.envelope, &f.evidence,
				     &fake_ops, &sink) == -EINVAL);
	CHECK("null skb does not consume", sink.freed == 0 &&
	      sink.delivered == 0 && sink.consumptions == 0);
}

static void test_envelope_failure_keeps_caller_ownership(void)
{
	unsigned char dma[128];
	unsigned char mpdu[24];
	unsigned char plcp[6] = { 0x0a, 0, 0, 0, 0, 0 };
	struct ob_rx_sw_status status;
	struct fake_skb skb = { .marker = 0x36e00003u };
	struct fake_sink sink;
	struct flow f;
	size_t len;

	make_mpdu(mpdu, sizeof(mpdu));
	len = build_dma(dma, sizeof(dma), 0x1001, OB_RX_PHY0_TYPE_CCK,
			0, 0, plcp, sizeof(plcp), mpdu, sizeof(mpdu), 0);
	CHECK("envelope-ownership source flow", run_flow(dma, len, &f) == 0);

	/* A truncated envelope fails before any handoff owns the skb. */
	memset(&sink, 0, sizeof(sink));
	CHECK("truncated envelope refused",
	      ob_rx_sw_validate(dma + f.mpdu_off, f.mpdu_len - 1u,
				&f.meta, &status) == -EMSGSIZE);
	CHECK("envelope failure does not consume the skb",
	      sink.freed == 0 && sink.delivered == 0 &&
	      skb.marker == 0x36e00003u);
}

int main(void)
{
	test_valid_cck_channel1();
	test_valid_ofdm_channel6();
	test_valid_ht20_channel11();
	test_missing_fcs_evidence();
	test_truncated_buffer();
	test_missing_channel_evidence();
	test_contradictory_channel_band();
	test_missing_phy_type_evidence();
	test_unsupported_vht();
	test_unsupported_ht_mcs();
	test_ht40_policy();
	test_handoff_exactly_once();
	test_envelope_failure_keeps_caller_ownership();

	if (failures) {
		printf("rx integration: %u failures\n", failures);
		return 1;
	}
	puts("rx integration: PASS (rev42 2.4 GHz CCK/OFDM/HT20; fake backends)");
	return 0;
}

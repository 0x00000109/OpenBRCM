// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests for the Phase 36 receive-status decoder. Synthetic vectors only:
 * no hardware, no DMA and no mac80211 call.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_rx_status.h"

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

static void put_le32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

/* Build a 40-byte rev42 prefix with the fields the decoder consumes. */
static void make_prefix(unsigned char *p, unsigned int frame_size,
			unsigned int phy0, unsigned int phy1,
			unsigned int status1, unsigned int status2,
			unsigned int tsf_low, unsigned int chan,
			unsigned long tsf_high)
{
	memset(p, 0, 40);
	put_le16(p + OB_RX_STATUS_OFF_FRAME_SIZE, frame_size);
	put_le16(p + OB_RX_STATUS_OFF_PHY_0, phy0);
	put_le16(p + OB_RX_STATUS_OFF_PHY_1, phy1);
	put_le16(p + OB_RX_STATUS_OFF_STATUS1, status1);
	put_le16(p + OB_RX_STATUS_OFF_STATUS2, status2);
	put_le16(p + OB_RX_STATUS_OFF_TSF_LOW, tsf_low);
	put_le16(p + OB_RX_STATUS_OFF_CHAN, chan);
	put_le32(p + OB_RX_STATUS_OFF_TSF_HIGH, tsf_high);
}

static void test_header_decode(void)
{
	unsigned char prefix[40];
	struct ob_rx_status st;

	make_prefix(prefix, 140, 0x0002 | OB_RX_PHY0_SHORT_PREAMBLE, 0x8040,
		    OB_RX_STATUS1_FCS_ERR | OB_RX_STATUS1_PAD_PRESENT |
			OB_RX_STATUS1_DEC_ATTEMPT | OB_RX_STATUS1_DEC_ERR,
		    0x0006, 0x1234, 0x8006, 0xdeadbeef);

	CHECK("decode accepts 40 bytes",
	      ob_rx_status_decode(prefix, sizeof(prefix), &st) == 0);
	CHECK("frame size little endian", st.rx_frame_size == 140);
	CHECK("phy type HT", st.phy_type == OB_RX_PHY0_TYPE_HT);
	CHECK("jssi low byte", st.jssi == 0x40);
	CHECK("sq high byte", st.sq == 0x80);
	CHECK("fcs error bit", st.fcs_error == 1);
	CHECK("pad present bit", st.pad_present == 1);
	CHECK("decrypt bits are corroborated only",
	      st.dec_attempted == 1 && st.dec_error == 1 &&
	      (st.verified & OB_RX_FACT_DECRYPT) == 0 &&
	      (st.corroborated & OB_RX_FACT_DECRYPT) != 0);
	CHECK("amsdu bit", st.amsdu == 0);
	CHECK("agg type", st.agg_type == 3);
	CHECK("tsf low", st.rx_tsf_low == 0x1234);
	CHECK("tsf high", st.rx_tsf_high == 0xdeadbeef);
	CHECK("raw channel", st.rx_chan == 0x8006);
	CHECK("band from top two bits", st.band_5g == 1);
	CHECK("fcs/pad/type verified bits",
	      (st.verified & (OB_RX_FACT_FRAME_SIZE | OB_RX_FACT_PHY_TYPE |
			      OB_RX_FACT_FCS_ERROR | OB_RX_FACT_PAD_PRESENT |
			      OB_RX_FACT_AMSDU | OB_RX_FACT_AGGTYPE |
			      OB_RX_FACT_TSF_LOW | OB_RX_FACT_CHAN_RAW |
			      OB_RX_FACT_BAND)) ==
	      (OB_RX_FACT_FRAME_SIZE | OB_RX_FACT_PHY_TYPE |
	       OB_RX_FACT_FCS_ERROR | OB_RX_FACT_PAD_PRESENT |
	       OB_RX_FACT_AMSDU | OB_RX_FACT_AGGTYPE |
	       OB_RX_FACT_TSF_LOW | OB_RX_FACT_CHAN_RAW | OB_RX_FACT_BAND));
	CHECK("tsf high corroborated",
	      (st.verified & OB_RX_FACT_TSF_HIGH) != 0);

	/* The classic 24-byte header must not invent the extended TSF word. */
	CHECK("24 bytes decode",
	      ob_rx_status_decode(prefix, OB_RX_STATUS_BASE_LEN, &st) == 0);
	CHECK("no tsf high below 0x1c",
	      st.rx_tsf_high == 0 &&
	      (st.verified & OB_RX_FACT_TSF_HIGH) == 0);
	CHECK("band still decoded from rxchan", st.band_5g == 1);
}

static void test_header_rejections(void)
{
	unsigned char prefix[40];
	struct ob_rx_status st;

	make_prefix(prefix, 100, 0, 0, 0, 0, 0, 0, 0);
	memset(&st, 0xa5, sizeof(st));
	CHECK("short prefix rejected",
	      ob_rx_status_decode(prefix, OB_RX_STATUS_BASE_LEN - 1, &st) ==
	      -EINVAL);
	CHECK("rejected prefix zeroes output", st.rx_frame_size == 0 &&
	      st.verified == 0 && st.observed_len == 0);
	CHECK("null prefix rejected",
	      ob_rx_status_decode(NULL, 40, &st) == -EINVAL);
	CHECK("null output rejected",
	      ob_rx_status_decode(prefix, 40, NULL) == -EINVAL);
}

static void test_frame_bounds(void)
{
	unsigned char prefix[40];
	struct ob_rx_status st;
	struct ob_rx_frame_bounds b;

	make_prefix(prefix, 1600, 0, 0, 0, 0, 0, 0, 0);
	ob_rx_status_decode(prefix, sizeof(prefix), &st);
	CHECK("fcs length is the pinned four bytes", OB_RX_FCS_LEN == 4u);
	CHECK("bounds without pad",
	      ob_rx_frame_bounds(&st, &b) == 0 && b.pad_len == 0 &&
	      b.phy_header_len == 6 && b.fcs_len == OB_RX_FCS_LEN &&
	      b.mpdu_len == 1590);

	make_prefix(prefix, 1600, 0, 0, OB_RX_STATUS1_PAD_PRESENT, 0, 0, 0, 0);
	ob_rx_status_decode(prefix, sizeof(prefix), &st);
	CHECK("bounds with pad",
	      ob_rx_frame_bounds(&st, &b) == 0 && b.pad_len == 2 &&
	      b.mpdu_len == 1588);

	make_prefix(prefix, 9, 0, 0, 0, 0, 0, 0, 0);
	ob_rx_status_decode(prefix, sizeof(prefix), &st);
	CHECK("runt frame cannot describe an MPDU",
	      ob_rx_frame_bounds(&st, &b) == -ERANGE);
	CHECK("range failure zeroes bounds", b.mpdu_len == 0 && b.fcs_len == 0);

	/* Remove the proven length fact: the helper must refuse, not guess. */
	st.verified = 0;
	CHECK("missing length provenance refused",
	      ob_rx_frame_bounds(&st, &b) == -EOPNOTSUPP);
	CHECK("null bounds rejected", ob_rx_frame_bounds(&st, NULL) == -EINVAL);
}

/*
 * RxChan decoding. The vendor's rev>0x27 path takes the low byte as the
 * channel and the top two bits as the band; rev<=0x27 hardware packs the
 * channel in bits 10:3 with 5 GHz at 0x800 and 40 MHz at 0x1000.
 */
static void test_channel_decode(void)
{
	unsigned char prefix[40];
	struct ob_rx_status st;
	struct ob_rx_chan ch;

	CHECK("normalized 2.4 GHz channel 6",
	      ob_rx_chan_decode(42, 0x1006, &ch) == 0 &&
	      ch.chan == 6 && ch.band_5g == 0 && ch.bw40_known == 0 &&
	      ch.raw == 0x1006);
	CHECK("normalized 2.4 GHz lower boundary",
	      ob_rx_chan_decode(42, 0x1001, &ch) == 0 &&
	      ch.chan == 1 && ch.band_5g == 0);
	CHECK("normalized 2.4 GHz upper boundary",
	      ob_rx_chan_decode(42, 0x100e, &ch) == 0 &&
	      ch.chan == 14 && ch.band_5g == 0);
	CHECK("normalized 5 GHz channel 36",
	      ob_rx_chan_decode(42, 0xd024, &ch) == 0 &&
	      ch.chan == 36 && ch.band_5g == 1);
	CHECK("normalized 5 GHz channel 164",
	      ob_rx_chan_decode(42, 0xd0a4, &ch) == 0 &&
	      ch.chan == 164 && ch.band_5g == 1 && ch.bw40_known == 0);
	CHECK("normalized 5 GHz lower boundary",
	      ob_rx_chan_decode(42, 0xd020, &ch) == 0 &&
	      ch.chan == 32 && ch.band_5g == 1);
	CHECK("normalized 5 GHz upper boundary",
	      ob_rx_chan_decode(42, 0xd0c4, &ch) == 0 &&
	      ch.chan == 196 && ch.band_5g == 1);

	CHECK("unnamed channel 0 refused",
	      ob_rx_chan_decode(42, 0x1000, &ch) == -ERANGE && ch.chan == 0);
	CHECK("channel 15 gap refused",
	      ob_rx_chan_decode(42, 0x100f, &ch) == -ERANGE && ch.chan == 0);
	CHECK("channel 17 does not exist",
	      ob_rx_chan_decode(42, 0x1011, &ch) == -ERANGE && ch.chan == 0);
	CHECK("channel 31 gap refused",
	      ob_rx_chan_decode(42, 0x101f, &ch) == -ERANGE);
	CHECK("5 GHz channel 197 is out of range",
	      ob_rx_chan_decode(42, 0xd0c5, &ch) == -ERANGE);
	CHECK("5 GHz bit with a 2.4 GHz number refused",
	      ob_rx_chan_decode(42, 0xd006, &ch) == -ERANGE && ch.chan == 0);
	CHECK("5 GHz bit with channel 14 refused",
	      ob_rx_chan_decode(42, 0xd00e, &ch) == -ERANGE);
	CHECK("2.4 GHz channel 36 refused",
	      ob_rx_chan_decode(42, 0x0024, &ch) == -ERANGE);

	/* The decoder must not turn an absent descriptor word into channel 6. */
	make_prefix(prefix, 100, 0, 0, 0, 0, 0, 0, 0);
	CHECK("missing raw channel decodes",
	      ob_rx_status_decode(prefix, sizeof(prefix), &st) == 0 &&
	      st.rx_chan == 0 && st.band_5g == 0);
	CHECK("missing raw channel refused",
	      ob_rx_chan_decode(42, st.rx_chan, &ch) == -ERANGE && ch.chan == 0);

	CHECK("legacy 2.4 GHz channel 6",
	      ob_rx_chan_decode(39, 0x0030, &ch) == 0 &&
	      ch.chan == 6 && ch.band_5g == 0 &&
	      ch.bw40_known == 1 && ch.bw40 == 0);
	CHECK("legacy 5 GHz channel 36",
	      ob_rx_chan_decode(39, 0x0920, &ch) == 0 &&
	      ch.chan == 36 && ch.band_5g == 1);
	CHECK("legacy 5 GHz upper boundary",
	      ob_rx_chan_decode(39, 0x0e20, &ch) == 0 &&
	      ch.chan == 196 && ch.band_5g == 1);
	CHECK("legacy 40 MHz flag decoded",
	      ob_rx_chan_decode(39, 0x1030, &ch) == 0 &&
	      ch.chan == 6 && ch.bw40 == 1);
	CHECK("legacy 5 GHz channel 0 refused",
	      ob_rx_chan_decode(39, 0x0800, &ch) == -ERANGE);
	CHECK("legacy 5 GHz bit with a 2.4 GHz number refused",
	      ob_rx_chan_decode(39, 0x0830, &ch) == -ERANGE);
	CHECK("legacy 2.4 GHz bit with a 5 GHz number refused",
	      ob_rx_chan_decode(39, 0x0120, &ch) == -ERANGE);
	CHECK("null channel output rejected",
	      ob_rx_chan_decode(42, 0x1006, NULL) == -EINVAL);
}

static void test_cck_rate(void)
{
	static const struct {
		unsigned int signal;
		unsigned int rate;
		unsigned int index_2g;
		const char *what;
	} v[] = {
		{0x0a, 2, 0, "cck 1M"},
		{0x14, 4, 1, "cck 2M"},
		{0x37, 11, 2, "cck 5.5M"},
		{0x6e, 22, 3, "cck 11M"},
	};
	unsigned char prefix[40];
	unsigned char plcp[6] = {0, 0, 0, 0, 0, 0};
	struct ob_rx_status st;
	struct ob_rx_rate rate;
	struct ob_rx_rate_80211 r80211;
	unsigned int i;

	make_prefix(prefix, 200, OB_RX_PHY0_TYPE_CCK, 0, 0, 0, 0, 0, 0);
	ob_rx_status_decode(prefix, sizeof(prefix), &st);
	for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		plcp[0] = (unsigned char)v[i].signal;
		CHECK(v[i].what,
		      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
		      rate.kind == OB_RX_RATE_CCK &&
		      rate.legacy_rate_500kbps == v[i].rate);
		CHECK("cck 2.4 GHz index",
		      ob_rx_rate_to_mac80211(&rate, 0, &r80211) == 0 &&
		      r80211.rate_idx == v[i].index_2g && r80211.is_ht == 0);
		CHECK("cck refused on 5 GHz",
		      ob_rx_rate_to_mac80211(&rate, 1, &r80211) ==
		      -EOPNOTSUPP);
	}

	plcp[0] = 0x11;	/* not a standard 802.11b signal byte */
	CHECK("non-standard cck refused",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) ==
	      -EOPNOTSUPP && rate.kind == OB_RX_RATE_UNKNOWN &&
	      rate.legacy_rate_500kbps == 0 && rate.mcs == 0);
	plcp[0] = 0xff;
	CHECK("cck 0xff is not a standard signal",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) ==
	      -EOPNOTSUPP);

	CHECK("cck missing PLCP refused",
	      ob_rx_rate_decode(&st, plcp, 0, &rate) == -EINVAL &&
	      rate.kind == OB_RX_RATE_UNKNOWN);
}

static void test_ofdm_rate(void)
{
	/*
	 * .rodata 0x2cdbe0, byte-for-byte. The table is indexed by
	 * plcp[0] & 7 and is deliberately not in ascending rate order.
	 */
	static const struct {
		unsigned int signal;
		unsigned int rate;
		unsigned int index_2g;
		unsigned int index_5g;
		const char *what;
	} v[] = {
		{0, 96, 10, 6, "ofdm index 0 is 48M"},
		{1, 48, 8, 4, "ofdm index 1 is 24M"},
		{2, 24, 6, 2, "ofdm index 2 is 12M"},
		{3, 12, 4, 0, "ofdm index 3 is 6M"},
		{4, 108, 11, 7, "ofdm index 4 is 54M"},
		{5, 72, 9, 5, "ofdm index 5 is 36M"},
		{6, 36, 7, 3, "ofdm index 6 is 18M"},
		{7, 18, 5, 1, "ofdm index 7 is 9M"},
	};
	unsigned char prefix[40];
	unsigned char plcp[6] = {0, 0, 0, 0, 0, 0};
	struct ob_rx_status st;
	struct ob_rx_rate rate;
	struct ob_rx_rate_80211 r80211;
	unsigned int i;

	make_prefix(prefix, 200, OB_RX_PHY0_TYPE_OFDM, 0, 0, 0, 0, 0, 0);
	ob_rx_status_decode(prefix, sizeof(prefix), &st);
	for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		plcp[0] = (unsigned char)v[i].signal;
		CHECK(v[i].what,
		      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
		      rate.kind == OB_RX_RATE_OFDM &&
		      rate.legacy_rate_500kbps == v[i].rate);
		CHECK("ofdm 2.4 GHz index",
		      ob_rx_rate_to_mac80211(&rate, 0, &r80211) == 0 &&
		      r80211.rate_idx == v[i].index_2g &&
		      r80211.is_ht == 0);
		CHECK("ofdm 5 GHz index",
		      ob_rx_rate_to_mac80211(&rate, 1, &r80211) == 0 &&
		      r80211.rate_idx == v[i].index_5g &&
		      r80211.is_ht == 0);
	}

	plcp[0] = 0x0d;	/* low three bits are 5: index 5 = 36M */
	CHECK("ofdm lookup uses only the low three bits",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
	      rate.legacy_rate_500kbps == 72);
	CHECK("missing OFDM PLCP refused",
	      ob_rx_rate_decode(&st, plcp, 0, &rate) == -EINVAL);
}

static void test_ht_rate(void)
{
	unsigned char prefix[40];
	unsigned char plcp[6] = {0x00, 0, 0, 0, 0, 0};
	struct ob_rx_status st;
	struct ob_rx_rate rate;
	struct ob_rx_rate_80211 r80211;

	make_prefix(prefix, 200, OB_RX_PHY0_TYPE_HT, 0, 0, 0, 0, 0, 0);
	ob_rx_status_decode(prefix, sizeof(prefix), &st);

	/* MCS 0, 20 MHz, long GI. */
	CHECK("ht mcs0 20mhz decoded",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
	      rate.kind == OB_RX_RATE_HT && rate.mcs == 0 &&
	      rate.bw40 == 0 && rate.sgi == 0);
	CHECK("ht mcs0 maps to index 0",
	      ob_rx_rate_to_mac80211(&rate, 0, &r80211) == 0 &&
	      r80211.rate_idx == 0 && r80211.is_ht == 1 &&
	      r80211.bw40 == 0 && r80211.sgi == 0);

	/* MCS 7, 40 MHz, short GI. */
	plcp[0] = 0x87;
	plcp[3] = 0x80;
	CHECK("ht mcs7 40mhz sgi decoded",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
	      rate.kind == OB_RX_RATE_HT && rate.mcs == 7 &&
	      rate.bw40 == 1 && rate.sgi == 1);
	CHECK("ht mcs7 maps to index 7",
	      ob_rx_rate_to_mac80211(&rate, 0, &r80211) == 0 &&
	      r80211.rate_idx == 7 && r80211.is_ht == 1 &&
	      r80211.bw40 == 1 && r80211.sgi == 1);

	/* MCS 31 is the highest index in mac80211's 802.11n table. */
	plcp[0] = 0x9f;
	plcp[3] = 0;
	CHECK("ht mcs31 accepted at the mac80211 boundary",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
	      rate.mcs == 31 &&
	      ob_rx_rate_to_mac80211(&rate, 0, &r80211) == 0 &&
	      r80211.rate_idx == 31);

	/* The vendor field is seven bits, but mac80211 has no MCS 32 slot. */
	plcp[0] = 0xa0;
	CHECK("ht mcs32 refused by mac80211 mapper",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
	      rate.mcs == 32 &&
	      ob_rx_rate_to_mac80211(&rate, 0, &r80211) == -EOPNOTSUPP);
	CHECK("refused ht mapper zeroes output",
	      r80211.rate_idx == 0 && r80211.is_ht == 0);

	plcp[0] = 0xff;
	CHECK("ht mcs127 decoded but not representable",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) == 0 &&
	      rate.mcs == 127 &&
	      ob_rx_rate_to_mac80211(&rate, 0, &r80211) == -EOPNOTSUPP);

	CHECK("ht needs four plcp bytes",
	      ob_rx_rate_decode(&st, plcp, 3, &rate) == -EINVAL &&
	      rate.kind == OB_RX_RATE_UNKNOWN);
}

static void test_rate_translation_rejections(void)
{
	struct ob_rx_rate rate;
	struct ob_rx_rate_80211 out;

	memset(&rate, 0, sizeof(rate));
	CHECK("unknown rate kind refused",
	      ob_rx_rate_to_mac80211(&rate, 0, &out) == -EOPNOTSUPP);

	rate.kind = OB_RX_RATE_CCK;
	rate.legacy_rate_500kbps = 0;
	CHECK("cck rate zero refused",
	      ob_rx_rate_to_mac80211(&rate, 0, &out) == -EOPNOTSUPP);
	rate.legacy_rate_500kbps = 13;	/* 6.5 Mbps is not a CCK rate */
	CHECK("non-CCK legacy value refused as CCK",
	      ob_rx_rate_to_mac80211(&rate, 0, &out) == -EOPNOTSUPP);

	rate.kind = OB_RX_RATE_OFDM;
	rate.legacy_rate_500kbps = 999;
	CHECK("unknown OFDM rate refused",
	      ob_rx_rate_to_mac80211(&rate, 0, &out) == -EOPNOTSUPP &&
	      out.rate_idx == 0);

	rate.kind = OB_RX_RATE_HT;
	rate.mcs = 32;
	CHECK("HT MCS 32 refused before indexing",
	      ob_rx_rate_to_mac80211(&rate, 0, &out) == -EOPNOTSUPP &&
	      out.rate_idx == 0 && out.is_ht == 0);

	CHECK("null mapper output rejected",
	      ob_rx_rate_to_mac80211(&rate, 0, NULL) == -EINVAL);
	CHECK("null mapper input rejected",
	      ob_rx_rate_to_mac80211(NULL, 0, &out) == -EINVAL);
}

static void test_vht_and_argument_checks(void)
{
	unsigned char prefix[40];
	unsigned char plcp[6] = {0, 0, 0, 0, 0, 0};
	unsigned char cck[6] = {0x0a, 0, 0, 0, 0, 0};
	struct ob_rx_status st;
	struct ob_rx_status no_fact;
	struct ob_rx_rate rate;

	make_prefix(prefix, 200, OB_RX_PHY0_TYPE_VHT, 0, 0, 0, 0, 0, 0);
	ob_rx_status_decode(prefix, sizeof(prefix), &st);
	CHECK("vht refused rather than guessed",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), &rate) ==
	      -EOPNOTSUPP && rate.kind == OB_RX_RATE_UNKNOWN);

	CHECK("null status rejected",
	      ob_rx_rate_decode(NULL, plcp, sizeof(plcp), &rate) == -EINVAL);
	CHECK("null plcp rejected",
	      ob_rx_rate_decode(&st, NULL, sizeof(plcp), &rate) == -EINVAL);
	CHECK("null rate output rejected",
	      ob_rx_rate_decode(&st, plcp, sizeof(plcp), NULL) == -EINVAL);
	CHECK("empty plcp rejected",
	      ob_rx_rate_decode(&st, plcp, 0, &rate) == -EINVAL);

	memset(&no_fact, 0, sizeof(no_fact));
	CHECK("missing PHY-type fact refused",
	      ob_rx_rate_decode(&no_fact, cck, sizeof(cck), &rate) ==
	      -EOPNOTSUPP && rate.kind == OB_RX_RATE_UNKNOWN);
}

/*
 * TSF reconstruction vectors. Every expected value is the result of the
 * pinned sequence at 0x2ea46..0x2ea62: merged = (hi & 0xffff0000) | lo, minus
 * 0x10000 when lo > (hi & 0xffff).
 */
static void test_tsf_merge(void)
{
	unsigned char prefix[40];
	struct ob_rx_status st;
	ob_rx_status_u64 tsf = 0;
	struct {
		unsigned int lo;
		unsigned long hi;
		ob_rx_status_u64 want;
		const char *what;
	} v[] = {
		{0x1234, 0x00071234ul, 0x00071234ull, "aligned halves"},
		{0xfff0, 0x00080000ul, 0x0007fff0ull, "borrow on wrap"},
		{0x0000, 0xffff0000ul, 0xffff0000ull, "no borrow at 0"},
		{0xffff, 0xffff0000ul, 0xfffeffffull, "borrow at top"},
		{0x0001, 0x00100000ul, 0x000f0001ull, "borrow, high advanced"},
		{0x8000, 0x00008000ul, 0x00008000ull, "equal halves"},
		{0x8001, 0x00018000ul, 0x00008001ull, "low ahead of high"},
		/*
		 * Inconsistent halves (high not advanced yet) underflow the same
		 * arithmetic: the vendor subtracts unconditionally and the 48-bit
		 * mask keeps the wrapped value rather than clamping it.
		 */
		{0x8001, 0x00008000ul, 0xffffffff8001ull, "underflow wraps 48-bit"},
	};
	unsigned int i;

	for (i = 0; i < sizeof(v) / sizeof(v[0]); i++)
		CHECK(v[i].what,
		      ob_rx_tsf_merge((ob_rx_status_u16)v[i].lo,
				      (ob_rx_status_u32)v[i].hi) == v[i].want);

	CHECK("merge result is 48-bit",
	      ob_rx_tsf_merge(0xffff, 0xfffffffful) <= 0xffffffffffffull);

	/* Through the decoder: both halves present in a 40-byte prefix. */
	make_prefix(prefix, 140, OB_RX_PHY0_TYPE_OFDM, 0, 0, 0, 0xfff0, 36,
		    0x00080000ul);
	CHECK("decode sets TSF48",
	      ob_rx_status_decode(prefix, sizeof(prefix), &st) == 0 &&
	      (st.verified & OB_RX_FACT_TSF48) && st.tsf48 == 0x0007fff0ull);
	CHECK("tsf48 accessor agrees",
	      ob_rx_status_tsf48(&st, &tsf) == 0 && tsf == 0x0007fff0ull);

	/* A 24-byte prefix has the low half only: no merge, no high fact. */
	CHECK("short prefix decodes",
	      ob_rx_status_decode(prefix, 24, &st) == 0);
	CHECK("short prefix has no TSF48",
	      !(st.verified & OB_RX_FACT_TSF48) && st.tsf48 == 0 &&
	      ob_rx_status_tsf48(&st, &tsf) == -EOPNOTSUPP);
	CHECK("null tsf rejected",
	      ob_rx_status_tsf48(&st, NULL) == -EINVAL);
	CHECK("null status rejected for tsf",
	      ob_rx_status_tsf48(NULL, &tsf) == -EINVAL);
}

/*
 * wlc_recv 0x4c482..0x4c4af: (pulled_len - 0xa) must be >= 0x18 + 4*flag.
 * The boundary is therefore 34 bytes (flag 0) and 38 bytes (flag 1).
 */
static void test_length_gate(void)
{
	CHECK("34 bytes, flag 0 accepted", ob_rx_len_plausible(34, 0) == 1);
	CHECK("33 bytes, flag 0 rejected", ob_rx_len_plausible(33, 0) == 0);
	CHECK("0 bytes rejected", ob_rx_len_plausible(0, 0) == 0);
	CHECK("9 bytes rejected before the compare",
	      ob_rx_len_plausible(9, 0) == 0);
	CHECK("10 bytes is still too short",
	      ob_rx_len_plausible(10, 0) == 0);
	CHECK("37 bytes, flag 1 rejected", ob_rx_len_plausible(37, 1) == 0);
	CHECK("38 bytes, flag 1 accepted", ob_rx_len_plausible(38, 1) == 1);
	CHECK("any nonzero flag counts as 1",
	      ob_rx_len_plausible(37, 0xff) == 0 &&
	      ob_rx_len_plausible(38, 0x80) == 1);
	CHECK("large length accepted",
	      ob_rx_len_plausible(0xffff, 1) == 1);
	CHECK("truncated 4-byte frame rejected",
	      ob_rx_len_plausible(4, 0) == 0);
}

int main(void)
{
	test_header_decode();
	test_header_rejections();
	test_frame_bounds();
	test_channel_decode();
	test_cck_rate();
	test_ofdm_rate();
	test_ht_rate();
	test_rate_translation_rejections();
	test_vht_and_argument_checks();
	test_tsf_merge();
	test_length_gate();

	if (failures) {
		printf("rx status: %u failures\n", failures);
		return 1;
	}
	puts("rx status: PASS (rev42 header, FCS bounds, rate decode; synthetic)");
	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/* Host tests for the offline RX translation boundary. No hardware. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_rx_mac80211.h"
#include "../../src/ob_rx_status.h"

static unsigned int failures;

#define CHECK(name, expr) do { \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

struct fake_skb {
	int marker;
};

#define SINK_SEEN_MAX 4u

struct sink {
	unsigned int delivered;
	unsigned int freed;
	unsigned int consumptions;
	unsigned int duplicate;
	unsigned int seen_count;
	void *seen[SINK_SEEN_MAX];
	void *last_skb;
	struct ob_rx_mac80211_report last_report;
};

/* Detect the same skb crossing either callback more than once. */
static void sink_note_consumption(struct sink *s, void *skb)
{
	unsigned int i;

	s->consumptions++;
	for (i = 0; i < s->seen_count; i++) {
		if (s->seen[i] == skb) {
			s->duplicate = 1;
			return;
		}
	}
	if (s->seen_count < SINK_SEEN_MAX)
		s->seen[s->seen_count++] = skb;
}

static void fake_deliver(void *ctx, void *skb,
			 const struct ob_rx_mac80211_report *report)
{
	struct sink *s = ctx;

	sink_note_consumption(s, skb);
	s->delivered++;
	s->last_skb = skb;
	s->last_report = *report;
}

static void fake_free(void *ctx, void *skb)
{
	struct sink *s = ctx;

	sink_note_consumption(s, skb);
	s->freed++;
	s->last_skb = skb;
}

static const struct ob_rx_mac80211_ops fake_ops = {
	.deliver = fake_deliver,
	.free = fake_free,
};

static const struct ob_rx_mac80211_ops deliver_only_ops = {
	.deliver = fake_deliver,
};

static const struct ob_rx_mac80211_ops free_only_ops = {
	.free = fake_free,
};

static void base_metadata(struct ob_rx_sw_metadata *meta)
{
	memset(meta, 0, sizeof(*meta));
	meta->provenance = OB_RX_SW_PROVENANCE_VALIDATED;
	meta->valid_fields = OB_RX_SW_META_CHANNEL | OB_RX_SW_META_RATE |
		OB_RX_SW_META_SIGNAL | OB_RX_SW_META_FCS_LEN;
	meta->complete = 1;
	meta->channel = 6;
	meta->rate_index = 2;
	meta->signal_dbm = -45;
	meta->fcs_len = 0;
}

static void base_evidence(struct ob_rx_mac80211_evidence *ev)
{
	memset(ev, 0, sizeof(*ev));
	ev->proven = OB_RX_MAC80211_FACT_FCS_PROVEN |
		OB_RX_MAC80211_FACT_CHANNEL_PROVEN |
		OB_RX_MAC80211_FACT_RATE_PROVEN |
		OB_RX_MAC80211_FACT_SIGNAL_PROVEN;
	ev->fcs_present = 0;
}

static int validate_beacon(struct ob_rx_sw_status *status, uint8_t *frame,
			   size_t len, struct ob_rx_sw_metadata *meta)
{
	memset(frame, 0, len);
	frame[0] = 0x80; /* management, beacon */
	meta->declared_len = len;
	return ob_rx_sw_validate(frame, len, meta, status);
}

static void test_translation_accepts_only_proven_facts(void)
{
	struct ob_rx_sw_status status;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_mac80211_evidence ev;
	struct ob_rx_mac80211_report report;
	uint8_t frame[32];	/* also holds the FCS-bearing 28-byte vector */

	base_metadata(&meta);
	base_evidence(&ev);
	CHECK("valid beacon envelope", validate_beacon(&status, frame, 24, &meta) == 0);
	CHECK("translate valid envelope",
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0);
	CHECK("channel preserved", report.channel == 6);
	CHECK("rate preserved", report.rate_index == 2);
	CHECK("signal flagged", (report.flags & OB_RX_MAC80211_REPORT_SIGNAL_VALID) &&
	      report.signal_dbm == -45);
	CHECK("no decrypted flag", !(report.flags & OB_RX_MAC80211_REPORT_DECRYPTED));

	ev.proven &= ~OB_RX_MAC80211_FACT_FCS_PROVEN;
	CHECK("missing FCS fact refused", ob_rx_mac80211_translate(&status, &ev,
						  &report) == -ENODATA);
	CHECK("failure zeroes report", report.channel == 0 && report.rate_index == 0);
	base_evidence(&ev);

	ev.fcs_present = 1;
	CHECK("present FCS refused", ob_rx_mac80211_translate(&status, &ev,
						 &report) == -EOPNOTSUPP);
	base_evidence(&ev);

	meta.fcs_len = 4;
	CHECK("validated FCS envelope accepted",
	      validate_beacon(&status, frame, 28, &meta) == 0);
	CHECK("FCS-bearing status refused", ob_rx_mac80211_translate(&status, &ev,
						      &report) == -EOPNOTSUPP);
	base_metadata(&meta);
	CHECK("revalidate stripped envelope",
	      validate_beacon(&status, frame, 24, &meta) == 0);
}

static void test_metadata_requires_evidence(void)
{
	struct ob_rx_sw_status status;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_mac80211_evidence ev;
	struct ob_rx_mac80211_report report;
	uint8_t frame[24];

	base_metadata(&meta);
	base_evidence(&ev);
	CHECK("channel test envelope", validate_beacon(&status, frame, 24, &meta) == 0);

	ev.proven &= ~OB_RX_MAC80211_FACT_CHANNEL_PROVEN;
	CHECK("channel without evidence refused", ob_rx_mac80211_translate(&status,
		&ev, &report) == -ENODATA);
	base_evidence(&ev);

	ev.proven &= ~OB_RX_MAC80211_FACT_RATE_PROVEN;
	CHECK("rate without evidence refused", ob_rx_mac80211_translate(&status,
		&ev, &report) == -ENODATA);
	base_evidence(&ev);

	ev.proven &= ~OB_RX_MAC80211_FACT_SIGNAL_PROVEN;
	CHECK("signal value without evidence refused", ob_rx_mac80211_translate(
		&status, &ev, &report) == -ENODATA);
	base_evidence(&ev);

	meta.valid_fields &= ~OB_RX_SW_META_SIGNAL;
	CHECK("signal-less envelope", validate_beacon(&status, frame, 24, &meta) == 0);
	CHECK("signal evidence without value refused", ob_rx_mac80211_translate(
		&status, &ev, &report) == -ENODATA);
	base_metadata(&meta);
	CHECK("restore signal envelope", validate_beacon(&status, frame, 24, &meta) == 0);

	ev.decrypted = 1;
	CHECK("decrypted without evidence refused", ob_rx_mac80211_translate(&status,
		&ev, &report) == -ENODATA);
	ev.proven |= OB_RX_MAC80211_FACT_DECRYPT_PROVEN;
	CHECK("decrypted with evidence accepted", ob_rx_mac80211_translate(&status,
		&ev, &report) == 0);
	CHECK("decrypted flag set", report.flags & OB_RX_MAC80211_REPORT_DECRYPTED);

	ev.proven |= (1u << 7);
	CHECK("unknown evidence bit refused", ob_rx_mac80211_translate(&status, &ev,
		&report) == -EINVAL);
}

static void test_rate_evidence(void)
{
	struct ob_rx_sw_status status;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_mac80211_evidence ev;
	struct ob_rx_mac80211_report report;
	uint8_t frame[24];

	base_metadata(&meta);
	base_evidence(&ev);
	meta.rate_index = 11;
	CHECK("highest legacy index envelope",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	CHECK("highest legacy index translated",
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.rate_index == 11 && report.rate_is_ht == 0 &&
	      report.bw40 == 0 && report.sgi == 0);

	meta.rate_index = 12;
	CHECK("legacy index 12 rejected by envelope",
	      validate_beacon(&status, frame, 24, &meta) == -ERANGE);

	/* 5 GHz exposes only the eight OFDM legacy entries, not the CCK ones. */
	base_metadata(&meta);
	meta.channel = 36;
	meta.rate_index = 7;
	CHECK("5 GHz legacy index envelope",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	CHECK("5 GHz legacy index 7 translated",
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.channel == 36 && report.rate_index == 7 &&
	      report.rate_is_ht == 0);
	meta.rate_index = 8;
	CHECK("5 GHz legacy index 8 envelope",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	CHECK("adapter rejects 5 GHz legacy index 8",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ERANGE &&
	      report.rate_index == 0 && report.rate_is_ht == 0);

	/* HT MCS 7, 20 MHz, short GI (the advertised HT20 profile). */
	base_metadata(&meta);
	meta.valid_fields |= OB_RX_SW_META_RATE_HT;
	meta.rate_index = 7;
	meta.rate_bw40 = 0;
	meta.rate_sgi = 1;
	CHECK("HT envelope accepted",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	CHECK("HT MCS translated with GI",
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.rate_index == 7 && report.rate_is_ht == 1 &&
	      report.bw40 == 0 && report.sgi == 1);

	/*
	 * A proven HT40 descriptor decodes, but the driver advertises and starts
	 * only 20 MHz. The adapter must refuse it rather than promise a width the
	 * PHY/channel path cannot configure.
	 */
	meta.rate_bw40 = 1;
	CHECK("HT40 envelope is structurally valid",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	CHECK("HT40 report refused by the HT20-only policy",
	      ob_rx_mac80211_translate(&status, &ev, &report) ==
	      -EOPNOTSUPP && report.rate_index == 0 &&
	      report.rate_is_ht == 0 && report.bw40 == 0);
	meta.rate_bw40 = 0;

	meta.rate_index = 32;
	CHECK("HT MCS 32 rejected by envelope",
	      validate_beacon(&status, frame, 24, &meta) == -ERANGE);

	/* Mutate a validated HT status to exercise adapter consistency. */
	meta.rate_index = 7;
	CHECK("HT envelope for adapter mutation",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	status.rate_index = 32;
	CHECK("adapter rejects HT MCS 32",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ERANGE &&
	      report.rate_index == 0 && report.rate_is_ht == 0);
	status.rate_index = 7;
	status.rate_is_ht = 0;
	CHECK("adapter rejects missing HT marker",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -EINVAL);
	status.rate_is_ht = 1;
	status.rate_bw40 = 2;
	CHECK("adapter rejects non-boolean width",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -EINVAL);

	/* A legacy status cannot smuggle HT-only flags into the report. */
	base_metadata(&meta);
	CHECK("legacy envelope for HT flag mutation",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	status.rate_is_ht = 1;
	CHECK("adapter rejects HT marker without HT metadata",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -EINVAL);
}

static void test_envelope_rejections(void)
{
	struct ob_rx_sw_status status;
	struct ob_rx_sw_metadata meta;
	uint8_t frame[64];

	base_metadata(&meta);
	memset(frame, 0, sizeof(frame));
	frame[0] = 0x80;
	meta.declared_len = 10;
	CHECK("short management frame refused",
	      ob_rx_sw_validate(frame, 10, &meta, &status) == -EMSGSIZE);

	frame[0] = 0x87; /* protocol version 3 */
	meta.declared_len = 24;
	CHECK("nonzero protocol version refused",
	      ob_rx_sw_validate(frame, 24, &meta, &status) == -EPROTONOSUPPORT);

	frame[0] = 0xb4; /* control, RTS: no fixed admitted form */
	meta.declared_len = 16;
	CHECK("unsupported control frame refused",
	      ob_rx_sw_validate(frame, 16, &meta, &status) == -EOPNOTSUPP);

	frame[0] = 0xd4; /* control, ACK */
	meta.declared_len = 10;
	CHECK("ACK envelope admitted", ob_rx_sw_validate(frame, 10, &meta,
						 &status) == 0);
	meta.declared_len = 9;
	CHECK("truncated ACK refused", ob_rx_sw_validate(frame, 9, &meta,
						  &status) == -EMSGSIZE);

	meta.declared_len = 24;
	CHECK("declared length mismatch refused",
	      ob_rx_sw_validate(frame, 25, &meta, &status) == -EMSGSIZE);
}

static void check_handoff_result(const char *name, int ret, int want,
				 struct sink *sink)
{
	CHECK(name, ret == want);
	if (want < 0) {
		CHECK("negative return leaves skb caller-owned",
		      sink->consumptions == 0 && sink->delivered == 0 &&
		      sink->freed == 0 && sink->duplicate == 0);
	} else if (want == 0) {
		CHECK("zero return delivered exactly once",
		      sink->delivered == 1 && sink->freed == 0 &&
		      sink->consumptions == 1 && sink->duplicate == 0);
	} else {
		CHECK("positive return consumed exactly once",
		      want == OB_RX_MAC80211_HANDOFF_CONSUMED &&
		      sink->freed == 1 && sink->delivered == 0 &&
		      sink->consumptions == 1 && sink->duplicate == 0);
	}
}

static void test_fake_sink_duplicate_detection(void)
{
	struct ob_rx_mac80211_report report;
	struct fake_skb skb = { .marker = 0x5a5a };
	struct sink sink;

	memset(&report, 0, sizeof(report));
	memset(&sink, 0, sizeof(sink));
	fake_deliver(&sink, &skb, &report);
	fake_free(&sink, &skb);
	CHECK("fake sink sees two callback consumptions",
	      sink.consumptions == 2);
	CHECK("fake sink detects duplicate skb identity",
	      sink.duplicate == 1);
}

static void test_handoff_consumes_exactly_once(void)
{
	struct ob_rx_mac80211_report report;
	struct ob_rx_sw_status status;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_mac80211_evidence ev;
	struct sink sink;
	struct fake_skb skb = { .marker = 1 };
	uint8_t frame[24];
	int ret;

	base_metadata(&meta);
	base_evidence(&ev);
	CHECK("consumed sentinel is positive",
	      OB_RX_MAC80211_HANDOFF_CONSUMED > 0);
	CHECK("handoff envelope", validate_beacon(&status, frame, 24, &meta) == 0);

	memset(&sink, 0, sizeof(sink));
	ret = ob_rx_mac80211_handoff(&skb, &status, &ev, &fake_ops, &sink);
	check_handoff_result("successful handoff", ret, 0, &sink);
	CHECK("same skb delivered", sink.last_skb == &skb && skb.marker == 1);
	CHECK("report retained", sink.last_report.channel == 6);

	ev.proven &= ~OB_RX_MAC80211_FACT_FCS_PROVEN;
	CHECK("translate preserves the exact translation errno",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ENODATA);
	memset(&sink, 0, sizeof(sink));
	ret = ob_rx_mac80211_handoff(&skb, &status, &ev, &fake_ops, &sink);
	check_handoff_result("refused handoff returns consumed", ret,
			     OB_RX_MAC80211_HANDOFF_CONSUMED, &sink);
	CHECK("refused frame identity preserved", sink.last_skb == &skb);

	memset(&sink, 0, sizeof(sink));
	ret = ob_rx_mac80211_handoff(&skb, &status, &ev, NULL, &sink);
	check_handoff_result("missing ops refused", ret, -EINVAL, &sink);

	memset(&sink, 0, sizeof(sink));
	ret = ob_rx_mac80211_handoff(&skb, &status, &ev, &deliver_only_ops,
				     &sink);
	check_handoff_result("missing free op refused", ret, -EINVAL, &sink);

	memset(&sink, 0, sizeof(sink));
	ret = ob_rx_mac80211_handoff(&skb, &status, &ev, &free_only_ops,
				     &sink);
	check_handoff_result("missing deliver op refused", ret, -EINVAL, &sink);

	memset(&sink, 0, sizeof(sink));
	ret = ob_rx_mac80211_handoff(NULL, &status, &ev, &fake_ops, &sink);
	check_handoff_result("null skb refused", ret, -EINVAL, &sink);

	CHECK("live delivery gate closed",
	      ob_rx_mac80211_live_delivery_enabled() == false);
}

static void test_two_skb_identities(void)
{
	struct ob_rx_sw_status status;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_mac80211_evidence ev;
	struct sink sink;
	struct fake_skb first = { .marker = 0xa1 };
	struct fake_skb second = { .marker = 0xb2 };
	uint8_t frame[24];

	base_metadata(&meta);
	base_evidence(&ev);
	CHECK("two-skb envelope",
	      validate_beacon(&status, frame, 24, &meta) == 0);

	memset(&sink, 0, sizeof(sink));
	CHECK("first skb delivered",
	      ob_rx_mac80211_handoff(&first, &status, &ev, &fake_ops,
				     &sink) == 0);
	ev.proven &= ~OB_RX_MAC80211_FACT_CHANNEL_PROVEN;
	CHECK("second skb consumed after translation failure",
	      ob_rx_mac80211_handoff(&second, &status, &ev, &fake_ops,
				     &sink) ==
	      OB_RX_MAC80211_HANDOFF_CONSUMED);
	CHECK("two identities consumed once each",
	      sink.consumptions == 2 && sink.duplicate == 0 &&
	      sink.delivered == 1 && sink.freed == 1);
	CHECK("first identity was delivered, not freed",
	      sink.seen_count == 2 && sink.seen[0] == &first &&
	      sink.seen[1] == &second);
}

static void test_channel_evidence_bands(void)
{
	struct ob_rx_sw_status status;
	struct ob_rx_sw_metadata meta;
	struct ob_rx_mac80211_evidence ev;
	struct ob_rx_mac80211_report report;
	uint8_t frame[24];

	base_metadata(&meta);
	base_evidence(&ev);
	CHECK("channel band envelope",
	      validate_beacon(&status, frame, 24, &meta) == 0);

	meta.channel = OB_RX_CHAN_MIN_2G;
	CHECK("2.4 GHz lower bound accepted",
	      validate_beacon(&status, frame, 24, &meta) == 0 &&
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.channel == OB_RX_CHAN_MIN_2G);
	meta.channel = OB_RX_CHAN_MAX_2G;
	CHECK("2.4 GHz upper bound accepted",
	      validate_beacon(&status, frame, 24, &meta) == 0 &&
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.channel == OB_RX_CHAN_MAX_2G);
	meta.channel = OB_RX_CHAN_MIN_5G;
	CHECK("5 GHz lower bound accepted",
	      validate_beacon(&status, frame, 24, &meta) == 0 &&
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.channel == OB_RX_CHAN_MIN_5G);
	meta.channel = 36;
	CHECK("5 GHz channel 36 accepted",
	      validate_beacon(&status, frame, 24, &meta) == 0 &&
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.channel == 36);
	meta.channel = OB_RX_CHAN_MAX_5G;
	CHECK("5 GHz upper bound accepted",
	      validate_beacon(&status, frame, 24, &meta) == 0 &&
	      ob_rx_mac80211_translate(&status, &ev, &report) == 0 &&
	      report.channel == OB_RX_CHAN_MAX_5G);

	/* Mutate a validated envelope to exercise the adapter's own bound. */
	meta.channel = 6;
	CHECK("channel bound envelope",
	      validate_beacon(&status, frame, 24, &meta) == 0);
	status.channel = 0;
	CHECK("missing channel 0 refused",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ERANGE &&
	      report.channel == 0);
	status.channel = 15;
	CHECK("channel 15 gap refused",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ERANGE);
	status.channel = 31;
	CHECK("channel 31 gap refused",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ERANGE);
	status.channel = 197;
	CHECK("5 GHz channel 197 refused",
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ERANGE);
	status.channel = 6;

	/* Evidence without a channel value is not a channel claim. */
	meta.channel = 36;
	meta.valid_fields &= ~OB_RX_SW_META_CHANNEL;
	CHECK("channel evidence without value refused",
	      validate_beacon(&status, frame, 24, &meta) == 0 &&
	      ob_rx_mac80211_translate(&status, &ev, &report) == -ENODATA);
}

int main(void)
{
	test_translation_accepts_only_proven_facts();
	test_metadata_requires_evidence();
	test_channel_evidence_bands();
	test_rate_evidence();
	test_envelope_rejections();
	test_fake_sink_duplicate_detection();
	test_handoff_consumes_exactly_once();
	test_two_skb_identities();

	if (failures) {
		printf("rx mac80211: %u failures\n", failures);
		return 1;
	}
	puts("rx mac80211: PASS (offline envelope only)");
	return 0;
}

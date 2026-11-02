// SPDX-License-Identifier: GPL-2.0-only
/* Host tests for the offline TX-status translation boundary. No hardware. */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_tx_status.h"

static unsigned int failures;

#define CHECK(name, expr) do { \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

static void base_input(struct ob_tx_status_input *in)
{
	memset(in, 0, sizeof(*in));
	in->record_present = 1;
	in->expected_frame_id = 0x58a1;
	in->reported_frame_id = 0x58a1;
	in->retries = 3;
	in->rate_index = 7;
	in->signal_dbm = -40;
}

static void base_evidence(struct ob_tx_status_evidence *ev)
{
	memset(ev, 0, sizeof(*ev));
	ev->proven = OB_TX_STATUS_EV_FRAME_ID;
	ev->ack = 0;
}

static void test_association_without_ack_is_not_success(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;

	base_input(&in);
	base_evidence(&ev);
	CHECK("associated record translated",
	      ob_tx_status_translate(&in, &ev, &report) == 0);
	CHECK("unconfirmed disposition",
	      report.disposition == OB_TX_STATUS_DELIVERED_UNCONFIRMED);
	CHECK("not reported to mac80211", report.report_to_mac80211 == 0);
	CHECK("never acked without evidence", report.acked == 0);
	CHECK("rate unknown", report.rate_known == 0 && report.rate_index == 0);
	CHECK("retries unknown", report.retries_known == 0);
	CHECK("signal unknown", report.signal_known == 0);
}

static void test_ack_requires_explicit_evidence(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;

	base_input(&in);
	base_evidence(&ev);
	ev.proven |= OB_TX_STATUS_EV_ACK;
	ev.ack = 1;
	CHECK("acked record translated",
	      ob_tx_status_translate(&in, &ev, &report) == 0);
	CHECK("ack disposition", report.disposition == OB_TX_STATUS_ACKED);
	CHECK("ack reportable", report.report_to_mac80211 == 1 && report.acked == 1);

	ev.ack = 0;
	CHECK("negative ack translated",
	      ob_tx_status_translate(&in, &ev, &report) == 0);
	CHECK("negative ack disposition",
	      report.disposition == OB_TX_STATUS_NOT_ACKED &&
	      report.report_to_mac80211 == 1 && report.acked == 0);
}

static void test_frame_id_mismatch_drops_without_success(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;

	base_input(&in);
	base_evidence(&ev);
	ev.proven |= OB_TX_STATUS_EV_ACK;
	ev.ack = 1;
	in.reported_frame_id = 0x58a2;
	CHECK("mismatch is a handled drop",
	      ob_tx_status_translate(&in, &ev, &report) == 0);
	CHECK("drop disposition", report.disposition == OB_TX_STATUS_DROP_FRAME_ID);
	CHECK("drop never reports success",
	      report.report_to_mac80211 == 0 && report.acked == 0);
	CHECK("drop ignores nominal ack evidence", report.rate_known == 0);

	/* A false frame ID must not borrow an ACK even when the evidence carries
	 * one: the mismatch branch returns before the ack branch.
	 */
	ev.ack = 0;
	CHECK("mismatch without ack also drops",
	      ob_tx_status_translate(&in, &ev, &report) == 0 &&
	      report.disposition == OB_TX_STATUS_DROP_FRAME_ID);
}

static void test_missing_record_and_association(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;

	base_input(&in);
	base_evidence(&ev);
	in.record_present = 0;
	CHECK("absent record is not a status",
	      ob_tx_status_translate(&in, &ev, &report) == -ENODATA);
	CHECK("absent record zeroes report",
	      report.disposition == OB_TX_STATUS_NONE &&
	      report.report_to_mac80211 == 0);

	base_input(&in);
	ev.proven = 0;
	CHECK("unassociated record refused",
	      ob_tx_status_translate(&in, &ev, &report) == -ENODATA);
	CHECK("unassociated record zeroes report", report.disposition == 0);
}

static void test_value_evidence_is_independent(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;

	base_input(&in);
	base_evidence(&ev);
	ev.proven |= OB_TX_STATUS_EV_RATE | OB_TX_STATUS_EV_RETRIES |
		OB_TX_STATUS_EV_SIGNAL;
	CHECK("value evidence translated",
	      ob_tx_status_translate(&in, &ev, &report) == 0);
	CHECK("rate preserved", report.rate_known == 1 && report.rate_index == 7);
	CHECK("retries preserved", report.retries_known == 1 && report.retries == 3);
	CHECK("signal preserved", report.signal_known == 1 &&
	      report.signal_dbm == -40);

	in.rate_index = 12;
	CHECK("out-of-range rate refused",
	      ob_tx_status_translate(&in, &ev, &report) == -ERANGE);
	CHECK("range failure zeroes report", report.rate_known == 0);

	in.rate_index = 7;
	ev.proven |= (1u << 6);
	CHECK("unknown evidence bit refused",
	      ob_tx_status_translate(&in, &ev, &report) == -EINVAL);

	CHECK("live reporting gate closed",
	      ob_tx_status_live_reporting_enabled() == false);
}

/*
 * One ownership outcome per return code: CALLER_OWNS is reserved for the
 * invalid-argument case, a closed gate and an unreportable record always
 * consume the skb, and an open+reportable record hands it to mac80211.
 */
static void test_skb_ownership_table(void)
{
	struct ob_tx_status_report report;

	memset(&report, 0, sizeof(report));
	report.report_to_mac80211 = 1;
	report.acked = 1;

	CHECK("closed gate consumes as drop",
	      ob_tx_status_skb_action(false, &report) ==
	      OB_TX_STATUS_SKB_CONSUMED_DROP);
	CHECK("open gate reports",
	      ob_tx_status_skb_action(true, &report) ==
	      OB_TX_STATUS_SKB_CONSUMED_REPORT);
	CHECK("unreportable record consumes as drop",
	      (report.report_to_mac80211 = 0,
	       ob_tx_status_skb_action(true, &report)) ==
	      OB_TX_STATUS_SKB_CONSUMED_DROP);
	CHECK("missing report consumes as drop",
	      ob_tx_status_skb_action(true, NULL) ==
	      OB_TX_STATUS_SKB_CONSUMED_DROP);
	/*
	 * The helper can no longer express "not consumed": a valid skb is always
	 * consumed here, and the two retained cases are rejected by the wrapper
	 * with a negative return before the helper is reached.
	 */
	CHECK("helper never retains a skb",
	      ob_tx_status_skb_action(true, &report) != OB_TX_STATUS_SKB_NONE &&
	      ob_tx_status_skb_action(false, NULL) != OB_TX_STATUS_SKB_NONE);
	CHECK("consumed-unreported code is positive",
	      OB_TX_STATUS_CONSUMED_UNREPORTED > 0);
}

/*
 * Ownership harness: the sinks count the effects, so a consumed skb shows up
 * as exactly one drop or one report and never both. Effects are also booked
 * per skb identity, which is what turns "two calls happened" into "each skb
 * was consumed exactly once": a second effect on one identity (a double free
 * or a report after a free) is a duplicate and is counted as such.
 */
#define SINK_MAX_SKBS 8

struct sink_skb {
	void *skb;
	unsigned int effects;
	unsigned int drops;
	unsigned int reports;
};

struct fake_sink {
	unsigned int drops;
	unsigned int reports;
	unsigned int effects;
	/* second-and-later effects on one identity, or an untracked identity */
	unsigned int duplicates;
	int last_acked;
	int last_report_to_mac80211;
	void *last_skb;
	struct sink_skb skbs[SINK_MAX_SKBS];
	unsigned int skb_count;
};

static void sink_book(struct fake_sink *sink, void *skb, int reported)
{
	struct sink_skb *entry = NULL;
	unsigned int i;

	for (i = 0; i < sink->skb_count; i++) {
		if (sink->skbs[i].skb == skb) {
			entry = &sink->skbs[i];
			break;
		}
	}
	if (!entry && sink->skb_count < SINK_MAX_SKBS) {
		entry = &sink->skbs[sink->skb_count++];
		entry->skb = skb;
	}
	if (!entry) {
		/* Cannot prove single use: never let this pass silently. */
		sink->duplicates++;
		return;
	}
	if (entry->effects)
		sink->duplicates++;
	entry->effects++;
	if (reported)
		entry->reports++;
	else
		entry->drops++;
}

static void fake_drop(void *ctx, void *skb)
{
	struct fake_sink *sink = ctx;

	sink->drops++;
	sink->effects++;
	sink->last_skb = skb;
	sink_book(sink, skb, 0);
}

static void fake_report(void *ctx, void *skb,
			const struct ob_tx_status_report *report)
{
	struct fake_sink *sink = ctx;

	sink->reports++;
	sink->effects++;
	sink->last_skb = skb;
	sink->last_acked = report->acked;
	sink->last_report_to_mac80211 = report->report_to_mac80211;
	sink_book(sink, skb, 1);
}

static const struct ob_tx_status_skb_ops fake_ops = {
	.drop = fake_drop,
	.report = fake_report,
};

static void sink_init(struct fake_sink *sink)
{
	memset(sink, 0, sizeof(*sink));
	sink->last_acked = -1;
}

static unsigned int sink_effects_for(const struct fake_sink *sink,
				     const void *skb)
{
	unsigned int i;

	for (i = 0; i < sink->skb_count; i++)
		if (sink->skbs[i].skb == skb)
			return sink->skbs[i].effects;
	return 0;
}

static unsigned int sink_drops_for(const struct fake_sink *sink,
				   const void *skb)
{
	unsigned int i;

	for (i = 0; i < sink->skb_count; i++)
		if (sink->skbs[i].skb == skb)
			return sink->skbs[i].drops;
	return 0;
}

static unsigned int sink_reports_for(const struct fake_sink *sink,
				     const void *skb)
{
	unsigned int i;

	for (i = 0; i < sink->skb_count; i++)
		if (sink->skbs[i].skb == skb)
			return sink->skbs[i].reports;
	return 0;
}

/* A rejected or closed-gate call consumes the skb exactly once, never twice. */
static void test_handoff_closed_gate_drops_once(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;
	struct fake_sink sink;
	int skb_marker;
	int ret;

	base_input(&in);
	base_evidence(&ev);
	CHECK("closed-gate record translated",
	      ob_tx_status_translate(&in, &ev, &report) == 0);

	sink_init(&sink);
	ret = ob_tx_status_handoff(&skb_marker, &report, false, &fake_ops,
				   &sink);
	CHECK("closed gate reports consumed-unreported",
	      ret == OB_TX_STATUS_CONSUMED_UNREPORTED && ret > 0);
	CHECK("closed gate drops once", sink.drops == 1 && sink.reports == 0);
	CHECK("closed gate has exactly one effect", sink.effects == 1);
	CHECK("closed gate forwards the caller's skb",
	      sink.last_skb == (void *)&skb_marker);
	CHECK("closed gate never acknowledges", sink.last_acked == -1);
	CHECK("closed gate consumes the packet once",
	      sink_effects_for(&sink, &skb_marker) == 1 &&
	      sink.duplicates == 0);
}

/*
 * A successful handoff transfers ownership exactly once, and two different
 * skbs are two independent transfers. Two distinct objects stand in for two
 * allocated skbs; the sink books effects per identity, so a double free of one
 * packet (or a report of an already-freed one) cannot hide behind a total
 * count that still equals the number of calls.
 */
static void test_handoff_open_gate_transfers_each_skb_once(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;
	struct fake_sink sink;
	int skb_a;
	int skb_b;
	int ret;

	base_input(&in);
	base_evidence(&ev);
	ev.proven |= OB_TX_STATUS_EV_ACK;
	ev.ack = 1;
	CHECK("acked record translated for handoff",
	      ob_tx_status_translate(&in, &ev, &report) == 0 &&
	      report.report_to_mac80211 == 1);
	CHECK("the two skb identities are distinct",
	      (void *)&skb_a != (void *)&skb_b);

	sink_init(&sink);
	memset(&skb_a, 0xa1, sizeof(skb_a));
	memset(&skb_b, 0xb2, sizeof(skb_b));

	ret = ob_tx_status_handoff(&skb_a, &report, true, &fake_ops, &sink);
	CHECK("open gate returns success", ret == 0);
	CHECK("open gate reports once and never frees",
	      sink.reports == 1 && sink.drops == 0);
	CHECK("open gate has exactly one effect", sink.effects == 1);
	CHECK("handoff preserves the report",
	      sink.last_acked == 1 && sink.last_report_to_mac80211 == 1);
	CHECK("first skb is forwarded to the sink",
	      sink.last_skb == (void *)&skb_a);

	ret = ob_tx_status_handoff(&skb_b, &report, true, &fake_ops, &sink);
	CHECK("second handoff returns success", ret == 0);
	CHECK("second skb is forwarded to the sink",
	      sink.last_skb == (void *)&skb_b);
	CHECK("two calls produce two reports and no drop",
	      sink.reports == 2 && sink.drops == 0 && sink.effects == 2);

	/* Per-identity accounting: exactly one consumption of each packet. */
	CHECK("two distinct identities were observed", sink.skb_count == 2);
	CHECK("first skb consumed exactly once",
	      sink_effects_for(&sink, &skb_a) == 1);
	CHECK("second skb consumed exactly once",
	      sink_effects_for(&sink, &skb_b) == 1);
	CHECK("first skb reported once, never dropped",
	      sink_reports_for(&sink, &skb_a) == 1 &&
	      sink_drops_for(&sink, &skb_a) == 0);
	CHECK("second skb reported once, never dropped",
	      sink_reports_for(&sink, &skb_b) == 1 &&
	      sink_drops_for(&sink, &skb_b) == 0);
	CHECK("no skb was consumed twice", sink.duplicates == 0);
}

/*
 * The detector itself must have teeth: a deliberate second effect on one
 * identity is reported as a duplicate instead of being counted as progress.
 */
static void test_sink_detects_duplicate_consumption(void)
{
	struct ob_tx_status_report report;
	struct fake_sink sink;
	int skb_marker;

	memset(&report, 0, sizeof(report));
	report.report_to_mac80211 = 1;
	sink_init(&sink);
	fake_report(&sink, &skb_marker, &report);
	CHECK("single effect is not a duplicate", sink.duplicates == 0);
	fake_report(&sink, &skb_marker, &report);
	CHECK("second effect on the same skb is a duplicate",
	      sink.duplicates == 1);
	CHECK("the double effect stays visible per identity",
	      sink_effects_for(&sink, &skb_marker) == 2);
}

/* Composition: the ACK bit comes only from a reportable, proven ACK. */
static void test_view_preserves_report_without_ack(void)
{
	struct ob_tx_status_report report;
	struct ob_tx_status_tx_view view;
	const ob_tx_status_u32 stale = OB_TX_STATUS_MAC80211_FLAG_ACK | (1u << 3);

	memset(&report, 0, sizeof(report));
	report.report_to_mac80211 = 1;
	report.acked = 0;

	ob_tx_status_mac80211_view(&report, stale, &view);
	CHECK("stale ACK bit is cleared",
	      (view.flags & OB_TX_STATUS_MAC80211_FLAG_ACK) == 0);
	CHECK("unrelated flag bits survive", view.flags == (1u << 3));
	CHECK("unproven rate is the -1/0 sentinel",
	      view.rate_idx == -1 && view.rate_count == 0);
	CHECK("sentinel fails the mac80211 rate predicate",
	      !(view.rate_idx >= 0 && view.rate_count > 0));

	/* Even with a pre-set ACK bit, a proven ACK is the only way back in. */
	report.acked = 1;
	ob_tx_status_mac80211_view(&report, stale, &view);
	CHECK("proven ACK sets the bit",
	      (view.flags & OB_TX_STATUS_MAC80211_FLAG_ACK) != 0);
	CHECK("other flags are still preserved",
	      view.flags == (stale | OB_TX_STATUS_MAC80211_FLAG_ACK));
	CHECK("an acked report without rate evidence stays rate-unknown",
	      view.rate_idx == -1 && view.rate_count == 0);
}

/* An unreportable record must not assert anything, ACK included. */
static void test_view_unreportable_record_asserts_nothing(void)
{
	struct ob_tx_status_report report;
	struct ob_tx_status_tx_view view;

	memset(&report, 0, sizeof(report));
	report.acked = 1;
	report.rate_known = 1;
	report.rate_index = 7;
	report.retries_known = 1;
	report.retries = 3;

	ob_tx_status_mac80211_view(&report, OB_TX_STATUS_MAC80211_FLAG_ACK,
				   &view);
	CHECK("unreportable record clears the ACK bit",
	      (view.flags & OB_TX_STATUS_MAC80211_FLAG_ACK) == 0);
	CHECK("unreportable record advertises no rate",
	      view.rate_idx == -1 && view.rate_count == 0);

	/* A missing report is the same "nothing proven" case. */
	ob_tx_status_mac80211_view(NULL, 0, &view);
	CHECK("missing report clears the ACK bit",
	      (view.flags & OB_TX_STATUS_MAC80211_FLAG_ACK) == 0);
	CHECK("missing report emits no rate",
	      view.rate_idx == -1 && view.rate_count == 0);
}

/* Rate composition: attempts are exact, bounded and never zero. */
static void test_view_rate_composition(void)
{
	struct ob_tx_status_report report;
	struct ob_tx_status_tx_view view;

	memset(&report, 0, sizeof(report));
	report.report_to_mac80211 = 1;
	report.rate_known = 1;
	report.rate_index = 7;

	ob_tx_status_mac80211_view(&report, 0, &view);
	CHECK("known rate keeps its index", view.rate_idx == 7);
	CHECK("unknown retries mean one attempt", view.rate_count == 1);

	report.retries_known = 1;
	report.retries = 3;
	ob_tx_status_mac80211_view(&report, 0, &view);
	CHECK("attempts are retries + 1", view.rate_count == 4);

	report.retries = 255;
	ob_tx_status_mac80211_view(&report, 0, &view);
	CHECK("attempt counter saturates instead of wrapping",
	      view.rate_count == OB_TX_STATUS_MAC80211_RATE_MAX_ATTEMPTS &&
	      view.rate_count != 0);

	/* An out-of-range index never becomes rate 0. */
	report.retries = 0;
	report.rate_index = OB_TX_STATUS_RATE_INDEX_LIMIT;
	ob_tx_status_mac80211_view(&report, 0, &view);
	CHECK("out-of-range rate falls back to the sentinel",
	      view.rate_idx == -1 && view.rate_count == 0);
}

/* Argument failures leave ownership with the caller: no sink call at all. */
static void test_handoff_rejects_without_consuming(void)
{
	struct ob_tx_status_report report;
	struct ob_tx_status_skb_ops partial = { .drop = fake_drop };
	struct fake_sink sink;
	int skb_marker;

	memset(&report, 0, sizeof(report));
	report.report_to_mac80211 = 1;
	report.acked = 1;

	sink_init(&sink);
	CHECK("NULL skb refused",
	      ob_tx_status_handoff(NULL, &report, true, &fake_ops, &sink) ==
	      -EINVAL);
	CHECK("NULL skb is not consumed", sink.effects == 0);

	CHECK("NULL ops refused",
	      ob_tx_status_handoff(&skb_marker, &report, true, NULL, &sink) ==
	      -EINVAL);
	CHECK("incomplete ops refused",
	      ob_tx_status_handoff(&skb_marker, &report, true, &partial, &sink) ==
	      -EINVAL);
	CHECK("refused calls leave the skb with the caller", sink.effects == 0);
}

/* Every accepted call yields exactly one effect: no leak, no double free. */
static void test_handoff_accounting_table(void)
{
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;
	struct fake_sink sink;
	int skbs[3];
	unsigned int calls = 0;
	int ret;

	/* Matched, no ACK evidence, gate open: unreportable -> drop. */
	base_input(&in);
	base_evidence(&ev);
	CHECK("table record translated",
	      ob_tx_status_translate(&in, &ev, &report) == 0);
	sink_init(&sink);
	ret = ob_tx_status_handoff(&skbs[0], &report, true, &fake_ops, &sink);
	calls++;
	CHECK("unreportable record consumed as drop",
	      ret == OB_TX_STATUS_CONSUMED_UNREPORTED && sink.drops == 1);
	CHECK("unreportable record has no successful ack", sink.last_acked == -1);

	/* Missing report: still consumed exactly once, on its own packet. */
	ret = ob_tx_status_handoff(&skbs[1], NULL, true, &fake_ops, &sink);
	calls++;
	CHECK("missing report consumed as drop",
	      ret == OB_TX_STATUS_CONSUMED_UNREPORTED);

	/* Negative ack evidence: reported, but never as a success. */
	base_evidence(&ev);
	ev.proven |= OB_TX_STATUS_EV_ACK;
	ev.ack = 0;
	CHECK("negative-ack record translated",
	      ob_tx_status_translate(&in, &ev, &report) == 0);
	ret = ob_tx_status_handoff(&skbs[2], &report, true, &fake_ops, &sink);
	calls++;
	CHECK("negative ack is reported", ret == 0 && sink.reports == 1);
	CHECK("negative ack never sets the ack flag", sink.last_acked == 0);

	CHECK("every accepted call produced exactly one effect",
	      sink.effects == calls &&
	      sink.drops + sink.reports == sink.effects);
	CHECK("every packet was consumed exactly once",
	      sink.skb_count == calls && sink.duplicates == 0 &&
	      sink_effects_for(&sink, &skbs[0]) == 1 &&
	      sink_effects_for(&sink, &skbs[1]) == 1 &&
	      sink_effects_for(&sink, &skbs[2]) == 1);
}

int main(void)
{
	test_skb_ownership_table();
	test_handoff_closed_gate_drops_once();
	test_handoff_open_gate_transfers_each_skb_once();
	test_handoff_rejects_without_consuming();
	test_handoff_accounting_table();
	test_sink_detects_duplicate_consumption();
	test_association_without_ack_is_not_success();
	test_ack_requires_explicit_evidence();
	test_frame_id_mismatch_drops_without_success();
	test_missing_record_and_association();
	test_value_evidence_is_independent();
	test_view_preserves_report_without_ack();
	test_view_unreportable_record_asserts_nothing();
	test_view_rate_composition();

	if (failures) {
		printf("tx status: %u failures\n", failures);
		return 1;
	}
	puts("tx status: PASS (evidence-gated, no ACK fabrication)");
	return 0;
}

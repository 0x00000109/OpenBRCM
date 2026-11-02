// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests for the Phase 36 rev42 TX-status record decoder. Synthetic
 * register values only: no MMIO, no hardware and no mac80211 call.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_tx_status.h"
#include "../../src/ob_tx_status_decode.h"

static unsigned int failures;

#define CHECK(name, expr) do { \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

static void base_regs(struct ob_tx_status_regs *regs, unsigned int status)
{
	memset(regs, 0, sizeof(*regs));
	regs->frmtxstatus = OB_TX_DEC_FRMTXSTATUS_VALID |
		((0x58a1u & 0xffffu) << OB_TX_DEC_FID_SHIFT) | status;
	regs->frmtxstatus2 = 0x002a0042u;	/* seq 0x42, PTX 0x2a */
	regs->aux0 = 0x000c000bu;	/* 11 + 12 attempts */
	regs->aux1 = 0x000e000du;	/* 13 + 14 attempts */
	regs->have_aux = 1;
}

static void test_record_fields(void)
{
	struct ob_tx_status_regs regs;
	struct ob_tx_status_decoded d;

	base_regs(&regs, OB_TX_DEC_INTERMEDIATE | OB_TX_DEC_PM_INDICATED |
		  (5u << OB_TX_DEC_SUPPRESS_SHIFT) |
		  (2u << OB_TX_DEC_ACK_FIELD_SHIFT));

	CHECK("valid record decodes",
	      ob_tx_status_decode_rev42(&regs, &d) == 0 && d.valid == 1);
	CHECK("frame id from bits 31:16", d.frame_id == 0x58a1);
	CHECK("slot selector is fid & 7", d.slot == 1);
	CHECK("status word is bits 15:0 including valid",
	      d.status_word == (OB_TX_DEC_FRMTXSTATUS_VALID |
				OB_TX_DEC_INTERMEDIATE |
				OB_TX_DEC_PM_INDICATED |
				(5u << OB_TX_DEC_SUPPRESS_SHIFT) |
				(2u << OB_TX_DEC_ACK_FIELD_SHIFT)));
	CHECK("intermediate flag", d.intermediate == 1);
	CHECK("pm flag", d.pm_indicated == 1);
	CHECK("suppress code", d.suppress_code == 5);
	CHECK("ack from the composite predicate", d.acked == 1);
	CHECK("sequence", d.sequence == 0x42);
	CHECK("ptx", d.ptx == 0x2a);
	/* 11 + 12 + 13 + 14 */
	CHECK("attempts summed from four counters",
	      d.attempts_known == 1 && d.attempts == 50);
	CHECK("frame id and ack proven",
	      (d.proven & (OB_TX_STATUS_EV_FRAME_ID | OB_TX_STATUS_EV_ACK)) ==
	      (OB_TX_STATUS_EV_FRAME_ID | OB_TX_STATUS_EV_ACK));
	CHECK("retry evidence present with aux", d.proven &
	      OB_TX_STATUS_EV_RETRIES);
}

static void test_ack_predicate(void)
{
	struct ob_tx_status_regs regs;
	struct ob_tx_status_decoded d;

	/* field value 1 is not an acknowledgement */
	base_regs(&regs, (1u << OB_TX_DEC_ACK_FIELD_SHIFT));
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("field value 1 is not acked", d.acked == 0);

	/* field value 0 is not an acknowledgement */
	base_regs(&regs, 0);
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("field value 0 is not acked", d.acked == 0);

	/* top bit alone is an acknowledgement */
	base_regs(&regs, OB_TX_DEC_ACK_TOP_BIT);
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("top bit alone is acked", d.acked == 1);

	/* field value 2 is the lower boundary */
	base_regs(&regs, (2u << OB_TX_DEC_ACK_FIELD_SHIFT));
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("field value 2 is acked", d.acked == 1);
}

static void test_invalid_and_missing_aux(void)
{
	struct ob_tx_status_regs regs;
	struct ob_tx_status_decoded d;
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;

	base_regs(&regs, 0);
	regs.frmtxstatus = 0;	/* validity bit clear */
	CHECK("invalid record decodes without error",
	      ob_tx_status_decode_rev42(&regs, &d) == 0 && d.valid == 0);
	CHECK("invalid record establishes nothing", d.proven == 0);
	CHECK("invalid record cannot feed the translator",
	      ob_tx_status_input_from_decoded(&d, 0x58a1, &in, &ev) ==
	      -ENODATA);

	base_regs(&regs, (2u << OB_TX_DEC_ACK_FIELD_SHIFT));
	regs.have_aux = 0;
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("attempts unknown without aux",
	      d.attempts_known == 0 && (d.proven & OB_TX_STATUS_EV_RETRIES) == 0);
	CHECK("bridge without retries",
	      ob_tx_status_input_from_decoded(&d, 0x58a1, &in, &ev) == 0 &&
	      (ev.proven & OB_TX_STATUS_EV_RETRIES) == 0 &&
	      in.retries == 0);

	CHECK("null regs rejected", ob_tx_status_decode_rev42(NULL, &d) ==
	      -EINVAL);
	CHECK("null output rejected", ob_tx_status_decode_rev42(&regs, NULL) ==
	      -EINVAL);
	CHECK("null bridge output rejected",
	      ob_tx_status_input_from_decoded(&d, 0, NULL, &ev) == -EINVAL);
}

static void test_translator_bridge(void)
{
	struct ob_tx_status_regs regs;
	struct ob_tx_status_decoded d;
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;
	struct ob_tx_status_report report;

	/* ACK: record 0x58a1, matching expected ID */
	base_regs(&regs, (2u << OB_TX_DEC_ACK_FIELD_SHIFT));
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("bridge builds input",
	      ob_tx_status_input_from_decoded(&d, 0x58a1, &in, &ev) == 0);
	CHECK("bridge sets ack evidence",
	      (ev.proven & OB_TX_STATUS_EV_ACK) != 0 && ev.ack == 1);
	CHECK("bridge reports retries", in.retries == 49);
	CHECK("true ack translates",
	      ob_tx_status_translate(&in, &ev, &report) == 0 &&
	      report.disposition == OB_TX_STATUS_ACKED &&
	      report.report_to_mac80211 == 1 && report.acked == 1);

	/* Frame ID mismatch dominates a nominal ACK. */
	CHECK("mismatch still bridges",
	      ob_tx_status_input_from_decoded(&d, 0x58a2, &in, &ev) == 0);
	CHECK("mismatch drops",
	      ob_tx_status_translate(&in, &ev, &report) == 0 &&
	      report.disposition == OB_TX_STATUS_DROP_FRAME_ID &&
	      report.report_to_mac80211 == 0 && report.acked == 0);

	/* No ACK: reportable only as NOT_ACKED. */
	base_regs(&regs, 0);
	ob_tx_status_decode_rev42(&regs, &d);
	ob_tx_status_input_from_decoded(&d, 0x58a1, &in, &ev);
	CHECK("no-ack record translates",
	      ob_tx_status_translate(&in, &ev, &report) == 0 &&
	      report.disposition == OB_TX_STATUS_NOT_ACKED &&
	      report.report_to_mac80211 == 1 && report.acked == 0);
}

static void test_retry_range(void)
{
	struct ob_tx_status_regs regs;
	struct ob_tx_status_decoded d;
	struct ob_tx_status_input in;
	struct ob_tx_status_evidence ev;

	/* 1020 attempts -> 1019 retries does not fit the u8 report field. */
	base_regs(&regs, (2u << OB_TX_DEC_ACK_FIELD_SHIFT));
	regs.aux0 = 0xffffffffu;
	regs.aux1 = 0xffffffffu;
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("large attempt sum decoded", d.attempts == 1020);
	CHECK("bridge drops unrepresentable retries",
	      ob_tx_status_input_from_decoded(&d, 0x58a1, &in, &ev) == 0 &&
	      (ev.proven & OB_TX_STATUS_EV_RETRIES) == 0 && in.retries == 0);

	/* A zero attempt count cannot produce retries either. */
	regs.aux0 = 0;
	regs.aux1 = 0;
	ob_tx_status_decode_rev42(&regs, &d);
	CHECK("zero attempts cleared",
	      ob_tx_status_input_from_decoded(&d, 0x58a1, &in, &ev) == 0 &&
	      (ev.proven & OB_TX_STATUS_EV_RETRIES) == 0);
}

int main(void)
{
	test_record_fields();
	test_ack_predicate();
	test_invalid_and_missing_aux();
	test_translator_bridge();
	test_retry_range();

	if (failures) {
		printf("tx status decode: %u failures\n", failures);
		return 1;
	}
	puts("tx status decode: PASS (rev42 record; composite ACK; synthetic)");
	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests for the recovered BCM4352 ACPHY "phycal_tempdelta" data-flow
 * contract (Phase 36R).  The expected values come from the vendor instruction
 * anchors recorded in PHYCAL_TEMPDELTA_CONTRACT.md, not from this helper:
 *
 *   configured = (u8)strtoul(vars["phycal_tempdelta"], base 0)
 *   absent key -> 0 (getintvar default); unparsable text -> 0 (strtoul)
 *   effective  = configured > 64 ? 40 : configured   (ACPHY SROM11 path)
 *   0          -> the consumer samples no temperature at all
 */
#include <assert.h>
#include <stdio.h>

#include "ob_acphy_cal_contract.h"

static struct ob_acphy_cal_tempdelta_input
vars_input(bool present, bool parsed, u64 raw)
{
	return (struct ob_acphy_cal_tempdelta_input) {
		.vars_available = true,
		.var_present = present,
		.text_parsed = parsed,
		.raw = raw,
	};
}

/* Minimal admitted contract input; only the periodic threshold bit varies. */
static struct ob_acphy_cal_contract_input contract_input(bool tempdelta_known)
{
	return (struct ob_acphy_cal_contract_input) {
		.target_identity_valid = true,
		.radio_complete = true,
		.chanspec_valid = true,
		.chanspec = OB_ACPHY_CAL_CONTROLLED_CHANSPEC,
		.periodic_tempdelta_known = tempdelta_known,
		.pll_selector_known = true,
		.pll_selector_from_identity = true,
		.pll_selector = OB_ACPHY_CAL_RADIO_SELECTOR_CLASS,
		.channel_row_known = true,
		.channel_key = OB_ACPHY_CAL_CONTROLLED_CHANNEL_KEY,
		.channel_row_index = OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW,
		.channel_row_offset = OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW_OFFSET,
	};
}

int main(void)
{
	struct ob_acphy_cal_tempdelta_input in;
	struct ob_acphy_cal_tempdelta_result out, again, other;
	struct ob_acphy_cal_contract_input cin;
	struct ob_acphy_cal_contract_result cal;

	/* R01 - valid input with established provenance. */
	in = vars_input(true, true, 10);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.known);
	assert(out.source == OB_ACPHY_CAL_TEMPDELTA_VAR);
	assert(out.configured == 10);
	assert(out.effective == 10);
	assert(!out.clamped);

	/* R02 - missing source input is unread, never a fabricated value. */
	in = (struct ob_acphy_cal_tempdelta_input) { .vars_available = false };
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(!out.known);
	assert(out.source == OB_ACPHY_CAL_TEMPDELTA_UNREAD);
	assert(out.configured == 0);
	assert(out.effective == 0);

	/* R02b - readable blob without the key -> vendor default 0. */
	in = vars_input(false, false, 0);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.known);
	assert(out.source == OB_ACPHY_CAL_TEMPDELTA_DEFAULT);
	assert(out.configured == 0);
	assert(out.effective == 0);

	/* R03 - present but unparsable text: strtoul() zero, not the default. */
	in = vars_input(true, false, 0);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.known);
	assert(out.source == OB_ACPHY_CAL_TEMPDELTA_VAR);
	assert(out.configured == 0);
	assert(out.effective == 0);

	/* R04 - zero is a known value and disables the temperature gate. */
	in = vars_input(true, true, 0);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.known);
	assert(out.configured == 0);
	assert(out.effective == 0);
	assert(!out.clamped);

	/* R05 - negative text wraps through strtoul() and then the byte store. */
	in = vars_input(true, true, (u64)-1);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.configured == 0xff);
	assert(out.clamped);
	assert(out.effective == OB_ACPHY_CAL_TEMPDELTA_ACPHY_FALLBACK);

	/* R06 - lower boundary. */
	in = vars_input(true, true, 1);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.configured == 1);
	assert(out.effective == 1);
	assert(!out.clamped);

	/* R07 - highest value that is still not clamped. */
	in = vars_input(true, true, OB_ACPHY_CAL_TEMPDELTA_CLAMP_LIMIT);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.configured == 64);
	assert(out.effective == 64);
	assert(!out.clamped);

	/* R08 - above range: byte truncation happens before the clamp. */
	in = vars_input(true, true, 65);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.configured == 65);
	assert(out.clamped);
	assert(out.effective == 40);

	in = vars_input(true, true, 300);	/* 300 & 0xff == 44 */
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.configured == 44);
	assert(!out.clamped);
	assert(out.effective == 44);

	in = vars_input(true, true, 0x140);	/* 320 & 0xff == 64 */
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.configured == 64);
	assert(!out.clamped);
	assert(out.effective == 64);

	/*
	 * R09/R10/R11 are NOT_APPLICABLE: the recovered vendor path contains no
	 * ACPHY-revision, radio-revision or board-configuration input.  Target
	 * identity is already enforced by ob_acphy_cal_contract_admit() and by
	 * ob_acphy_calibration_inputs_probe().
	 */

	/* R12 - repeated invocation is stable and keeps no hidden state. */
	in = vars_input(true, true, 12);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &again) == 0);
	assert(out.known == again.known);
	assert(out.configured == again.configured);
	assert(out.effective == again.effective);
	assert(out.clamped == again.clamped);
	assert(out.source == again.source);

	/* R13 - two independent devices keep independent values. */
	in = vars_input(true, true, 10);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	in = vars_input(true, true, 20);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &other) == 0);
	assert(out.effective == 10);
	assert(other.effective == 20);

	/* R14 - an unreadable blob must not claim a source or a value. */
	in = (struct ob_acphy_cal_tempdelta_input) { .vars_available = false };
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, &out) == 0);
	assert(out.source == OB_ACPHY_CAL_TEMPDELTA_UNREAD);
	assert(!out.known && out.configured == 0 && out.effective == 0);
	assert(!out.clamped);

	/*
	 * R15 - the periodic threshold is not an initial calibration input.  An
	 * unread value is reported as unknown and is NOT turned into zero and NOT
	 * treated as a missing initial-calibration operation (Phase 36S).
	 */
	cin = contract_input(false);
	assert(ob_acphy_cal_contract_admit(&cin, &cal) == 0);
	assert(cal.first_blocker == OB_ACPHY_CAL_BLOCK_NONE);
	assert(cal.unknown_mask == 0);
	assert(!cal.periodic_tempdelta_known);
	assert(!cal.calibration_started);

	/* A known zero is likewise just a value, not a blocker. */
	cin = contract_input(true);
	assert(ob_acphy_cal_contract_admit(&cin, &cal) == 0);
	assert(cal.unknown_mask == 0);
	assert(cal.periodic_tempdelta_known);

	/* NULL handling is explicit, not undefined. */
	assert(ob_acphy_cal_tempdelta_acphy_resolve(NULL, &out) == -22);
	assert(ob_acphy_cal_tempdelta_acphy_resolve(&in, NULL) == -22);

	puts("ob_acphy_cal_tempdelta_test: PASS");
	return 0;
}

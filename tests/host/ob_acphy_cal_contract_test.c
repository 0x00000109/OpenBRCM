// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>

#include "ob_acphy_cal_contract.h"

static struct ob_acphy_cal_contract_input valid_input(void)
{
	return (struct ob_acphy_cal_contract_input) {
		.target_identity_valid = true,
		.radio_complete = true,
		.chanspec_valid = true,
		.chanspec = 0x1001,
		.periodic_tempdelta_known = true,
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
	struct ob_acphy_cal_contract_input in = valid_input();
	struct ob_acphy_cal_contract_result result;

	/*
	 * Phase 36S: the periodic phycal_tempdelta threshold is not an initial
	 * calibration input.  An unread value must be reported and must not block
	 * admission, and it must stay distinguishable from zero.
	 */
	in.periodic_tempdelta_known = false;
	assert(ob_acphy_cal_contract_admit(&in, &result) == 0);
	assert(result.first_blocker == OB_ACPHY_CAL_BLOCK_NONE);
	assert(result.unknown_mask == 0);
	assert(!result.periodic_tempdelta_known);
	assert(!result.calibration_started);
	assert(!result.pll_started);
	assert(!result.channel_started);

	in = valid_input();
	in.chanspec = 0x1002;
	assert(ob_acphy_cal_contract_admit(&in, &result) == -22);
	assert(result.first_blocker == OB_ACPHY_CAL_BLOCK_CHANSPEC);
	assert(result.unknown_mask == 0);

	in = valid_input();
	in.pll_selector = 1;
	assert(ob_acphy_cal_contract_admit(&in, &result) == -22);
	assert(result.first_blocker == OB_ACPHY_CAL_BLOCK_PLL_SELECTOR);
	assert(result.unknown_mask == 0);

	in = valid_input();
	in.channel_row_offset = 0x1c5c;
	assert(ob_acphy_cal_contract_admit(&in, &result) == -22);
	assert(result.first_blocker == OB_ACPHY_CAL_BLOCK_CHANNEL_ROW);
	assert(result.unknown_mask == 0);

	/* Unknown initial inputs still block in their own fixed order. */
	in = valid_input();
	in.pll_selector_known = false;
	in.periodic_tempdelta_known = false;
	assert(ob_acphy_cal_contract_admit(&in, &result) == -95);
	assert(result.first_blocker == OB_ACPHY_CAL_BLOCK_PLL_SELECTOR);
	assert(result.unknown_mask == OB_ACPHY_CAL_UNKNOWN_PLL_SELECTOR);

	in = valid_input();
	in.channel_row_known = false;
	assert(ob_acphy_cal_contract_admit(&in, &result) == -95);
	assert(result.first_blocker == OB_ACPHY_CAL_BLOCK_CHANNEL_ROW);
	assert(result.unknown_mask == OB_ACPHY_CAL_UNKNOWN_CHANNEL_ROW);

	in = valid_input();
	assert(ob_acphy_cal_contract_admit(&in, &result) == 0);
	assert(result.first_blocker == OB_ACPHY_CAL_BLOCK_NONE);
	assert(result.unknown_mask == 0);
	assert(result.periodic_tempdelta_known);
	assert(!result.calibration_started);
	assert(!result.pll_started);
	assert(!result.channel_started);

	puts("ob_acphy_cal_contract_test: PASS");
	return 0;
}

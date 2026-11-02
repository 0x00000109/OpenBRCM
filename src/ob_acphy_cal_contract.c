// SPDX-License-Identifier: GPL-2.0-only

#include "ob_acphy_cal_contract.h"

int ob_acphy_cal_tempdelta_acphy_resolve(
	const struct ob_acphy_cal_tempdelta_input *in,
	struct ob_acphy_cal_tempdelta_result *out)
{
	u8 configured;

	if (!in || !out)
		return -22;
	*out = (struct ob_acphy_cal_tempdelta_result) {
		.source = OB_ACPHY_CAL_TEMPDELTA_UNREAD,
	};
	/* Not observable is a state, not an error and not a zero value. */
	if (!in->vars_available)
		return 0;
	if (!in->var_present) {
		/* getintvar() returns its default when the key is absent. */
		out->known = true;
		out->source = OB_ACPHY_CAL_TEMPDELTA_DEFAULT;
		return 0;
	}
	/* A present key without digits is strtoul()'s zero, not the default. */
	configured = in->text_parsed ? (u8)in->raw : 0;
	out->known = true;
	out->source = OB_ACPHY_CAL_TEMPDELTA_VAR;
	out->configured = configured;
	if (configured > OB_ACPHY_CAL_TEMPDELTA_CLAMP_LIMIT) {
		out->clamped = true;
		out->effective = OB_ACPHY_CAL_TEMPDELTA_ACPHY_FALLBACK;
	} else {
		out->effective = configured;
	}
	return 0;
}

int ob_acphy_cal_contract_admit(const struct ob_acphy_cal_contract_input *in,
				 struct ob_acphy_cal_contract_result *result)
{
	u8 unknown = 0;

	if (!in || !result)
		return -22;
	*result = (struct ob_acphy_cal_contract_result) {
		.first_blocker = OB_ACPHY_CAL_BLOCK_NONE,
	};
	/*
	 * Periodic policy is observable but never gates the initial transaction:
	 * the vendor reason-6 arm reaches wlc_phy_cals_acphy() whether or not the
	 * threshold is known, and the calibration body never reads it.
	 */
	result->periodic_tempdelta_known = in->periodic_tempdelta_known;
	if (!in->target_identity_valid) {
		result->first_blocker = OB_ACPHY_CAL_BLOCK_TARGET;
		return -22;
	}
	if (!in->radio_complete) {
		result->first_blocker = OB_ACPHY_CAL_BLOCK_RADIO_COMPLETE;
		return -22;
	}
	if (!in->chanspec_valid || in->chanspec != OB_ACPHY_CAL_CONTROLLED_CHANSPEC) {
		result->first_blocker = OB_ACPHY_CAL_BLOCK_CHANSPEC;
		return -22;
	}
	/*
	 * The selector-zero fact is usable only when it was decoded during the
	 * completed attach identity sequence.  A zero in ordinary storage may be
	 * an uninitialized default, and selector values one or two would enter a
	 * different vendor PLL sequence.
	 */
	if (in->pll_selector_known &&
	    (!in->pll_selector_from_identity ||
	     in->pll_selector != OB_ACPHY_CAL_RADIO_SELECTOR_CLASS)) {
		result->first_blocker = OB_ACPHY_CAL_BLOCK_PLL_SELECTOR;
		return -22;
	}
	/*
	 * This is not a general channel lookup.  It admits only the pinned
	 * BCM2069 rev4 fixture: chanspec 0x1001, key 1, row 63, stride-derived
	 * row offset 0x1c8c.
	 */
	if (in->channel_row_known &&
	    (in->channel_key != OB_ACPHY_CAL_CONTROLLED_CHANNEL_KEY ||
	     in->channel_row_index != OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW ||
	     in->channel_row_offset != OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW_OFFSET)) {
		result->first_blocker = OB_ACPHY_CAL_BLOCK_CHANNEL_ROW;
		return -22;
	}
	if (!in->pll_selector_known)
		unknown |= OB_ACPHY_CAL_UNKNOWN_PLL_SELECTOR;
	if (!in->channel_row_known)
		unknown |= OB_ACPHY_CAL_UNKNOWN_CHANNEL_ROW;
	result->unknown_mask = unknown;
	if (unknown) {
		if (unknown & OB_ACPHY_CAL_UNKNOWN_PLL_SELECTOR)
			result->first_blocker = OB_ACPHY_CAL_BLOCK_PLL_SELECTOR;
		else
			result->first_blocker = OB_ACPHY_CAL_BLOCK_CHANNEL_ROW;
		return -95;
	}
	return 0;
}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Host-only policy checks for acphy_pre_radio_test_only.  The test mode is a
 * mutually exclusive, no-DMA/no-initvals isolated prefix and must leave the
 * device bound for the bounded postcondition record.
 */
#include <stdio.h>

#include "../../src/ob_ucode.h"

static int failures;

static void check(const char *what, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s: got %d expected %d\n", what, got, expected);
		failures++;
	}
}

static enum ob_isolated_mode select19_with_old_flag(unsigned int flag)
{
	bool m[18] = { false };

	if (flag < 18)
		m[flag] = true;
	return ob_isolated_mode_select19(
		m[0], m[1], m[2], m[3], m[4], m[5],
		m[6], m[7], m[8], m[9], m[10], m[11],
		m[12], m[13], m[14], m[15], m[16], m[17], true);
}

static enum ob_isolated_mode select19_only_old_flag(unsigned int flag)
{
	bool m[18] = { false };

	if (flag < 18)
		m[flag] = true;
	return ob_isolated_mode_select19(
		m[0], m[1], m[2], m[3], m[4], m[5],
		m[6], m[7], m[8], m[9], m[10], m[11],
		m[12], m[13], m[14], m[15], m[16], m[17], false);
}

static enum ob_isolated_mode select20_with_flags(const bool old[19],
						 bool postbmac)
{
	return ob_isolated_mode_select20(
		old[0], old[1], old[2], old[3], old[4], old[5], old[6],
		old[7], old[8], old[9], old[10], old[11], old[12], old[13],
		old[14], old[15], old[16], old[17], old[18], postbmac);
}

int main(void)
{
	enum ob_isolated_mode mode;
	unsigned int old_flag;
	bool new_flags[19] = { false };

	mode = ob_isolated_mode_select15(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		true);
	check("acphy pre-radio selected", mode,
	      OB_ISOLATED_ACPHY_PRE_RADIO_TEST);
	check("acphy pre-radio skips teardown",
	      ob_isolated_mode_skips_teardown(mode), true);
	check("acphy pre-radio does not use DMA", ob_isolated_mode_uses_dma(mode),
	      false);
	check("acphy pre-radio does not apply initvals",
	      ob_isolated_mode_applies_initvals(mode), false);

	mode = ob_isolated_mode_select16(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, true);
	check("class0 checkpoint selected", mode,
	      OB_ISOLATED_ACPHY_CLASS0_0413_TEST);
	check("class0 checkpoint preserves state",
	      ob_isolated_mode_preserves_state(mode), true);
	check("class0 checkpoint is not old pre-radio",
	      mode == OB_ISOLATED_ACPHY_PRE_RADIO_TEST, false);

	mode = ob_isolated_mode_select15(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false);
	check("normal still selected without isolated flags", mode,
	      OB_ISOLATED_NONE);

	mode = ob_isolated_mode_select15(false, true, false, false, false,
		false, false, false, false, false, false, false, false, false,
		true);
	check("acphy pre-radio conflicts with ucode", mode,
	      OB_ISOLATED_CONFLICT);

	mode = ob_isolated_mode_select16(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		true, true);
	check("class0 checkpoint conflicts with old pre-radio", mode,
	      OB_ISOLATED_CONFLICT);

	mode = ob_isolated_mode_select17(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, true);
	check("post-0413 phase1 mode selected", mode,
	      OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST);
	check("post-0413 mode preserves state",
	      ob_isolated_mode_preserves_state(mode), true);
	mode = ob_isolated_mode_select18(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false, true);
	check("post-phase1 phase2 mode selected", mode,
	      OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST);
	check("post-phase1 phase2 mode preserves state",
	      ob_isolated_mode_preserves_state(mode), true);
	mode = ob_isolated_mode_select18(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, true, true);
	check("phase1 and phase2 modes conflict", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select18(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, true, false, true);
	check("phase0 and phase2 modes conflict", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select18(false, true, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false, true);
	check("phase2 conflicts with unrelated isolated mode", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select17(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, true, true);
	check("phase0 and phase1 modes conflict", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select17(false, true, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, true);
	check("post-0413 conflicts with unrelated isolated mode", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select17(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, true, false);
	check("legacy phase0 selection preserved through select17", mode,
	      OB_ISOLATED_ACPHY_CLASS0_0413_TEST);
	mode = ob_isolated_mode_select17(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false);
	check("select17 preserves normal path", mode, OB_ISOLATED_NONE);

	mode = ob_isolated_mode_select19(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false, false, true);
	check("postphase2 return mode selected", mode,
	      OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST);
	check("postphase2 return preserves state",
	      ob_isolated_mode_preserves_state(mode), true);
	mode = ob_isolated_mode_select19(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false, true, false);
	check("old phase2 selection unchanged via select19", mode,
	      OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST);
	mode = ob_isolated_mode_select19(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false, false, false);
	check("select19 keeps normal path", mode, OB_ISOLATED_NONE);
	mode = ob_isolated_mode_select19(false, true, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false, false, true);
	check("postphase2 conflicts with unrelated isolated mode", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select19(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, true, false, false, true);
	check("postphase2 conflicts with phase0 mode", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select19(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, true, false, true);
	check("postphase2 conflicts with phase1 mode", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select19(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, false, true, true);
	check("postphase2 conflicts with phase2 mode", mode,
	      OB_ISOLATED_CONFLICT);
	mode = ob_isolated_mode_select19(false, false, false, false, false,
		false, false, false, false, false, false, false, false, false,
		false, false, true, false, false);
	check("old phase1 selection unchanged via select19", mode,
	      OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST);
	for (old_flag = 0; old_flag < 18; old_flag++)
		check("postphase2 conflicts with every prior isolated mode",
		      select19_with_old_flag(old_flag), OB_ISOLATED_CONFLICT);

	mode = select20_with_flags(new_flags, true);
	check("postbmac mode selected alone", mode,
	      OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST);
	check("postbmac mode preserves state",
	      ob_isolated_mode_preserves_state(mode), true);
	check("all modes unset remains normal",
	      select20_with_flags(new_flags, false), OB_ISOLATED_NONE);
	for (old_flag = 0; old_flag < 19; old_flag++) {
		enum ob_isolated_mode old_mode;

		new_flags[old_flag] = true;
		old_mode = old_flag == 18 ?
			OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST :
			select19_only_old_flag(old_flag);
		check("postbmac conflicts with every existing isolated mode",
		      select20_with_flags(new_flags, true),
		      OB_ISOLATED_CONFLICT);
		check("existing selection remains unchanged when new mode unset",
		      select20_with_flags(new_flags, false), old_mode);
		new_flags[old_flag] = false;
	}

	if (failures) {
		printf("ob_acphy_preradio_mode_test: %d failures\n", failures);
		return 1;
	}
	puts("ob_acphy_preradio_mode_test: PASS");
	return 0;
}

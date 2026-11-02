// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phase 36S first-up gate contract.
 *
 * The normal start path stops at ob_acphy_init_production_gate(), whose final
 * decision is a literal -EOPNOTSUPP.  The decisive step just before that
 * return is the shared policy exercised here:
 *
 *   ob_acphy_lpf_target_from_hw() identity  (mirrored as a target literal)
 *   -> ob_phy_acphy_lpf_input_status()      (real, src/ob_phy_lpf.c)
 *   -> ob_phy_init_block()                  (real, src/ob_phy.h)
 *
 * These are the production functions, not copies of them.  The literal
 * -EOPNOTSUPP return and the absence of MMIO before it are verified as
 * STATIC_SOURCE evidence in the Phase 36S package, not by this program.
 */
#include <assert.h>
#include <stdio.h>

#include "../../src/ob_phy.h"
#include "../../src/ob_phy_lpf.h"

static struct ob_phy_lpf_target supported_target(void)
{
	/* Exactly the BCM4352/rev42/ACPHY rev1/2069 rev4/class-0 identity. */
	return (struct ob_phy_lpf_target) {
		.identity_valid = true,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.d11_core_id = 0x812,
		.d11_revision = 42,
		.phy_type = 0x0b,
		.phy_revision = 1,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_revision_class = 0,
	};
}

int main(void)
{
	struct ob_phy_lpf_target target = supported_target();
	/* OB_PHY_INIT_NOT_STARTED is 0, so zero initialisation is the real
	 * not-started state; it also keeps the analysis warning-clean. */
	struct ob_phy_init_lifecycle a = { 0 }, b = { 0 }, c = { 0 };
	enum ob_phy_lpf_input_status status;

	/* G01 (S01/S02): the gate's blocker is admissible in the state that
	 * ob_phy_init_begin() publishes, and it leaves the lifecycle blocked. */
	assert(ob_phy_init_begin(&a) == 0);
	assert(a.phase == OB_PHY_INIT_PRE_RADIO_IN_PROGRESS);
	assert(ob_phy_acphy_lpf_input_status(&target, true, 0) ==
	       OB_PHY_LPF_INPUTS_SUPPORTED);
	assert(ob_phy_init_block(&a, OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD) == 0);
	assert(a.phase == OB_PHY_INIT_BLOCKED);
	assert(a.blocker == OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD);
	assert(a.error == -95);

	/* G02 (S14): a blocked stage cannot advance, so the gate cannot be
	 * mistaken for a successful pre-radio or radio stage. */
	assert(ob_phy_init_advance(&a, OB_PHY_INIT_PRE_RADIO_READY) == -95);
	assert(ob_phy_init_advance(&a, OB_PHY_INIT_RADIO_COMPLETE) == -95);
	assert(a.phase == OB_PHY_INIT_BLOCKED);
	assert(!a.radio_prefix_complete && !a.analog_tx_lpf_complete &&
	       !a.radio_sequence_complete);

	/* G03 (S01): start's failure path cannot overwrite the recorded block.
	 * ob_phy_init_fail() refuses a BLOCKED state, so the gate's exact blocker
	 * survives to the caller and is never rewritten as a generic failure. */
	assert(ob_phy_init_fail(&a, -95) == -16);
	assert(a.phase == OB_PHY_INIT_BLOCKED);
	assert(a.blocker == OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD);
	assert(a.error == -95);

	/* G04 (S03): invalid board/SPROM provenance is rejected before the
	 * supported status is claimed.  boardflags3_valid is the rev11-validated
	 * provenance token used by the production gate. */
	status = ob_phy_acphy_lpf_input_status(&target, false, 0);
	assert(status == OB_PHY_LPF_INPUT_BOARDFLAGS3);
	assert(status != OB_PHY_LPF_INPUTS_SUPPORTED);
	/* The nested/348 board bit selects a different vendor arm. */
	status = ob_phy_acphy_lpf_input_status(&target, true,
					       OB_PHY_LPF_NESTED_348_BOARD_BIT);
	assert(status == OB_PHY_LPF_INPUT_BOARDFLAGS3_BRANCH);

	/* G05 (S03): a valid LPF target with the wrong identity is rejected. */
	target = supported_target();
	target.identity_valid = false;
	assert(ob_phy_acphy_lpf_input_status(&target, true, 0) ==
	       OB_PHY_LPF_INPUT_TARGET);
	target = supported_target();
	target.d11_revision = 41;
	assert(ob_phy_acphy_lpf_input_status(&target, true, 0) ==
	       OB_PHY_LPF_INPUT_TARGET);
	target = supported_target();
	target.phy_revision = 2;
	assert(ob_phy_acphy_lpf_input_status(&target, true, 0) ==
	       OB_PHY_LPF_INPUT_PI164_BRANCH);

	/* G06 (S11): a phase label never proves its transaction ran.  The
	 * calibration and channel completion tokens have no setter anywhere in
	 * the driver, so CALIBRATION_COMPLETE/CHANNEL_PROGRAMMED are unreachable
	 * even from a correct predecessor. */
	assert(ob_phy_init_begin(&b) == 0);
	assert(ob_phy_init_advance(&b, OB_PHY_INIT_PRE_RADIO_READY) == 0);
	assert(ob_phy_init_begin_radio(&b) == 0);
	assert(ob_phy_init_advance(&b, OB_PHY_INIT_RADIO_COMPLETE) == -95);
	assert(b.phase == OB_PHY_INIT_RADIO_IN_PROGRESS);
	b.radio_prefix_complete = true;
	b.analog_tx_lpf_complete = true;
	b.radio_sequence_complete = true;
	assert(ob_phy_init_advance(&b, OB_PHY_INIT_RADIO_COMPLETE) == 0);
	assert(ob_phy_init_advance(&b, OB_PHY_INIT_CALIBRATION_IN_PROGRESS) == 0);
	assert(ob_phy_init_advance(&b, OB_PHY_INIT_CALIBRATION_COMPLETE) == -95);
	assert(b.phase == OB_PHY_INIT_CALIBRATION_IN_PROGRESS);
	assert(!b.calibration_complete);
	assert(!b.channel_programmed);
	assert(!ob_phy_init_is_operational(&b));

	/* G07 (S13): lifecycle state is per device; nothing is shared. */
	assert(ob_phy_init_begin(&a) == -16);
	assert(ob_phy_init_begin(&b) == -16);
	assert(ob_phy_init_begin(&c) == 0);
	assert(a.phase == OB_PHY_INIT_BLOCKED);
	assert(b.phase == OB_PHY_INIT_CALIBRATION_IN_PROGRESS);
	assert(c.phase == OB_PHY_INIT_PRE_RADIO_IN_PROGRESS);
	assert(!ob_phy_init_is_operational(&b));
	assert(!ob_phy_init_is_operational(&c));

	puts("ob_acphy_first_up_gate_test: PASS");
	return 0;
}

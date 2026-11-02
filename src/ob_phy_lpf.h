/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_LPF_H_
#define _OB_PHY_LPF_H_

#include "ob_phy.h"

/* Runtime identity needed by the selected BCM4352/rev42 sub_a04c2 arm. */
struct ob_phy_lpf_target {
	bool identity_valid;
	u16 chip_id;
	u8 chip_revision;
	u16 d11_core_id;
	u8 d11_revision;
	u8 phy_type;
	u8 phy_revision; /* PI+0x164 branch value; not the D11 revision */
	u16 radio_id;
	u8 radio_revision;
	u8 radio_revision_class;
};

enum ob_phy_lpf_input_status {
	OB_PHY_LPF_INPUTS_SUPPORTED = 0,
	OB_PHY_LPF_INPUT_TARGET,
	OB_PHY_LPF_INPUT_PI164_BRANCH,
	OB_PHY_LPF_INPUT_BOARDFLAGS3,
	OB_PHY_LPF_INPUT_BOARDFLAGS3_BRANCH,
};

#define OB_PHY_LPF_NESTED_348_BOARD_BIT	0x00000080u

/* Pure prerequisite check, safe to use before the radio prefix accesses HW. */
bool ob_phy_acphy_lpf_inputs_supported(
	const struct ob_phy_lpf_target *target,
	bool boardflags3_valid, u32 boardflags3);
enum ob_phy_lpf_input_status ob_phy_acphy_lpf_input_status(
	const struct ob_phy_lpf_target *target,
	bool boardflags3_valid, u32 boardflags3);

/*
 * Callback-only implementation of the BCM4352 rev1/rev0x0a sub_a04c2
 * sequence. The test entry remains available for isolated trace tests. The
 * lifecycle entry additionally requires exact target identity, valid board
 * data selecting nested+0x348 == 0, and successful radio-prefix completion.
 * The table arm also assumes the accepted nonzero PI+0x185 path; production
 * admission must establish that runtime input before lifting the start gate.
 * Other LPF arms are not implemented. saved_019e_out is published only after
 * the final PHY[0x1645] callback succeeds. Access errors are fail-stop: already
 * issued operations are not rolled back and no later callback is issued.
 */
int ob_phy_acphy_lpf_complete_test_only(
	const struct ob_phy_access_ops *ops, void *ctx, u16 chip_id,
	u8 phy_revision, u16 *saved_019e_out);

int ob_phy_acphy_lpf_stage_run(
	const struct ob_phy_access_ops *ops, void *ctx,
	const struct ob_phy_lpf_target *target,
	bool boardflags3_valid, u32 boardflags3,
	struct ob_phy_init_lifecycle *lifecycle,
	u16 *saved_019e_out);

#endif /* _OB_PHY_LPF_H_ */

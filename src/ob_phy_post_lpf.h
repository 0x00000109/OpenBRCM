/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_POST_LPF_H_
#define _OB_PHY_POST_LPF_H_

#include "ob_phy.h"

/*
 * Offline-only callback runner for the accepted selected sub_a7089 fixture.
 * It deliberately has no OpenBRCM hardware backend and no normal-probe caller.
 * The selected route is conditional on a caller-supplied synthetic key-1,
 * row-63 fixture; this does not establish the target's historical chanspec.
 */
enum ob_phy_post_lpf_domain {
	OB_PHY_POST_LPF_PHY,
	OB_PHY_POST_LPF_RADIO,
	OB_PHY_POST_LPF_CONTROL,
};

struct ob_phy_post_lpf_ops {
	int (*read16)(void *ctx, u8 logical_event, u32 source_site,
		      enum ob_phy_post_lpf_domain domain, u16 reg, u16 *value);
	int (*write16)(void *ctx, u8 logical_event, u32 source_site,
		       enum ob_phy_post_lpf_domain domain, u16 reg, u16 value);
	int (*delay_us)(void *ctx, u8 logical_event, u32 source_site, u16 usec);
	int (*phyclk_fgc)(void *ctx, u8 logical_event, u32 source_site,
			  bool enabled);
};

struct ob_phy_post_lpf_fixture {
	bool explicitly_synthetic;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u16 requested_chanspec;
	u8 lookup_key;
	u8 row_index;
	bool lookup_matched;
	u8 pi_16e_class;
	u8 pi_164_phy_revision;
	u8 pi_168_radio_class;
	u8 nested_912;
};

struct ob_phy_post_lpf_entry_state {
	u8 nested_32c;
	u8 nested_32e;
	u8 nested_912;
};

enum ob_phy_post_lpf_result_kind {
	OB_PHY_POST_LPF_RESULT_INVALID = 0,
	OB_PHY_POST_LPF_SELECTED_FIXTURE_COMPLETE = 1,
};

struct ob_phy_post_lpf_result {
	enum ob_phy_post_lpf_result_kind kind;
	u32 logical_event_count;
	u32 callback_count;
	bool hardware_ready;
};

int ob_phy_acphy_a7089_key1_row63_test_only(
	const struct ob_phy_post_lpf_ops *ops, void *ctx,
	const struct ob_phy_post_lpf_fixture *fixture,
	const struct ob_phy_post_lpf_entry_state *entry_state,
	struct ob_phy_post_lpf_result *result);

struct ob_phy_post_lpf_composed_result {
	u16 lpf_saved_019e;
	struct ob_phy_post_lpf_result selected_block;
};

/* Synthetic composition: complete LPF, the two proven caller stores, then
 * the explicitly supplied selected key-1/row-63 sub_a7089 fixture. */
int ob_phy_acphy_post_lpf_compose_fixture_test_only(
	const struct ob_phy_post_lpf_ops *ops, void *ctx,
	const struct ob_phy_post_lpf_fixture *fixture,
	struct ob_phy_post_lpf_composed_result *result);

/* Resume the accepted rev1/key-1 post-LPF path through RADIO_COMPLETE. */
int ob_acphy_post_lpf_radio_complete(struct ob_hw *hw, bool channel_probe);

#endif /* _OB_PHY_POST_LPF_H_ */

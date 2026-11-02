/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_POST_FARROW_CONTINUATION_H_
#define _OB_PHY_POST_FARROW_CONTINUATION_H_

#include "ob_phy_post_lpf.h"

#define OB_PHY_POST_FARROW_BYTES 9u

enum ob_phy_post_farrow_config_arm {
	OB_PHY_POST_FARROW_CONFIG_UNKNOWN = 0,
	OB_PHY_POST_FARROW_CONFIG_ZERO,
	OB_PHY_POST_FARROW_CONFIG_NONZERO,
};

struct ob_phy_post_farrow_software_input {
	bool explicitly_synthetic;
	bool entry_after_farrow_join;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u32 phy_flags_19c;
	u16 initial_chanspec;
	bool pi_cache_240_present;
	u8 pi_cache_240;
	bool nested_341_present;
	u8 nested_341;
	bool nested_912_present;
	u8 nested_912;
	bool nested_8b0_present;
	u32 nested_8b0;
	bool nested_914_present;
	s16 nested_914;
	bool nested_916_present;
	s16 nested_916;
	u8 nested_65f[OB_PHY_POST_FARROW_BYTES];
	u8 nested_8b4[OB_PHY_POST_FARROW_BYTES];
	bool cache_record_result_supplied;
	bool cache_record_result_present;
	bool cache_record_payload_present;
	u8 cache_record_payload[OB_PHY_POST_FARROW_BYTES];
};

struct ob_phy_post_farrow_state {
	bool valid;
	bool explicitly_synthetic;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u32 phy_flags_19c;
	u16 initial_chanspec;
	u8 pi_cache_240;
	u8 nested_341;
	u8 nested_912;
	u32 nested_8b0;
	u8 nested_668[OB_PHY_POST_FARROW_BYTES];
	bool nested_8a8_has_record;
	bool post_909fd_complete;
};

enum ob_phy_post_farrow_software_kind {
	OB_PHY_POST_FARROW_SW_INVALID = 0,
	OB_PHY_POST_FARROW_SW_COMPLETE,
};

struct ob_phy_post_farrow_software_result {
	enum ob_phy_post_farrow_software_kind kind;
	bool cache_helper_called;
	bool caller_joined;
	bool sub_909fd_called;
	bool copy_selected;
	enum ob_phy_post_farrow_config_arm configuration_arm;
	u32 common_rejoin_site;
	u32 helper_return_site;
	u16 hardware_callbacks;
	bool hardware_ready;
};

/* Models only explicit synthetic state. It executes the software cache and
 * nested+0x8b0 operations; the cache record is a typed byte object, never an
 * integer reinterpreted as a pointer. */
int ob_phy_post_farrow_continue_software_test_only(
	const struct ob_phy_post_farrow_software_input *input,
	struct ob_phy_post_farrow_state *state,
	struct ob_phy_post_farrow_software_result *result);

struct ob_phy_post_farrow_hw_ops {
	int (*read16)(void *ctx, u8 event, u32 source_site, u16 reg,
		      u16 *value);
	int (*write16)(void *ctx, u8 event, u32 source_site, u16 reg,
		       u16 value);
};

enum ob_phy_desense_prefix_kind {
	OB_PHY_DESENSE_PREFIX_INVALID = 0,
	OB_PHY_DESENSE_PREFIX_COMPLETE,
	OB_PHY_DESENSE_PREFIX_FAILED,
};

struct ob_phy_desense_prefix_result {
	enum ob_phy_desense_prefix_kind kind;
	u8 logical_events;
	u8 callback_records;
	u32 terminal_boundary;
	u16 saved_019e;
	u16 saved_016c;
	bool desense_helper_complete;
	bool hardware_ready;
};

/* Executes exactly the four directly recovered sub_9af05 operations through
 * the boundary before its selected child call at 0x9b123. No live backend or
 * normal-probe caller exists. */
int ob_phy_desense_9af05_prefix_test_only(
	const struct ob_phy_post_farrow_hw_ops *ops, void *ctx,
	const struct ob_phy_post_farrow_state *state,
	struct ob_phy_desense_prefix_result *result);

#endif /* _OB_PHY_POST_FARROW_CONTINUATION_H_ */

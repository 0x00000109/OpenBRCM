/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_9A121_H_
#define _OB_PHY_9A121_H_

#include "ob_phy.h"

enum ob_phy_9a121_policy {
	OB_PHY_9A121_HISTORICAL_STRICT = 0,
	OB_PHY_9A121_SYNTHETIC_PARAMETERIZED,
};

enum ob_phy_9a121_status {
	OB_9A121_INVALID = 0,
	OB_9A121_SYNTHETIC_COMPLETE,
	OB_9A121_HOST_SUPPORTED_PREFIX,
	OB_9A121_FAILED_BACKEND_OPERATION,
};

struct ob_phy_9a121_input {
	bool explicitly_synthetic;
	enum ob_phy_9a121_policy policy;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u8 chain_count;
	bool chanspec_present;
	u16 chanspec;
	bool nested_910_present;
	u8 nested_910;
	bool nested_911_present;
	u8 nested_911;
	bool nested_340_present;
	u8 nested_340;
	bool nested_341_present;
	u8 nested_341;
	bool nested_66e_present;
	u8 nested_66e;
	bool nested_66f_present;
	u8 nested_66f;
	bool core_mask_present;
	u8 core_mask;
	bool nested_64a_64f_present;
	u8 nested_64a_64f[6];
	bool nested_coefficients_present;
	/* Read-only synthetic slice rooted at nested+0x450. Extent reaches
	 * through nested+0x571 for two cores and the bounded candidate range. */
	u8 nested_coefficients[0x122];
};

struct ob_phy_9a121_ops {
	int (*phy_read16)(void *ctx, u32 source_site, u16 reg, u16 *value);
	int (*phy_write16)(void *ctx, u32 source_site, u16 reg, u16 value);
	int (*radio_read16)(void *ctx, u32 source_site, u16 reg, u16 *value);
	int (*radio_write16)(void *ctx, u32 source_site, u16 reg, u16 value);
	int (*table_write)(void *ctx, u32 source_site, u8 table_id,
			   u8 offset, u8 count, u8 width_bits,
			   const u8 *payload, u8 payload_length);
};

struct ob_phy_9a121_result {
	enum ob_phy_9a121_status status;
	u8 callback_count;
	u8 logical_event_count;
	u32 terminal_boundary;
	u32 first_unsupported_site;
	u32 first_unimplemented_operation;
	u32 failed_site;
	u8 selected_core;
	u8 child_returns[5];
	u8 sub99f29_generated[6];
	bool nested_state_present;
	u8 nested_650_655[6];
	u8 nested_65c;
	bool branch_flag;
	u8 sub_90493_returns[2];
	u16 sub_90493_samples[2];
	u16 caller_phy_samples[2];
	s8 next_sub_9175a_arg;
	s8 next_sub_91b6e_arg;
	u8 sub_9175a_category[2];
	u8 sub_91b6e_category[2];
	u8 sub_91b6e_band[2];
	u16 sub_9175a_samples[2][3];
	u16 sub_91b6e_samples[2];
	bool hardware_ready;
	bool phy_ready;
	bool channel_ready;
};

/* Isolated callback-only host model. No normal-probe path calls this runner. */
int ob_phy_9a121_test_only(const struct ob_phy_9a121_ops *ops, void *ctx,
			   const struct ob_phy_9a121_input *input,
			   struct ob_phy_9a121_result *result);

/* Narrow callback-only entry for the caller's selector-0 sub_99f29 at
 * sub_9a539 @0x9a7a5. Caller supplies the already-reached six-byte shared
 * state; this does not execute or imply the rest of sub_9a121. */
int ob_phy_9a121_sub99f29_selector0_test_only(
	const struct ob_phy_9a121_ops *ops, void *ctx,
	const struct ob_phy_9a121_input *input,
	const u8 seed[6], u8 core, u8 delta, u8 format,
	struct ob_phy_9a121_result *result);

/* Exact software-only sub_8f086 selector, exposed for another isolated
 * caller model that reaches the same historical helper with different state. */
int ob_phy_9a121_sub8f086_test_only(
	const u8 nested_coefficients[0x122], const u8 candidate_counts[6],
	const u8 initial[6], u8 core, u8 delta, u8 format,
	u8 output[6], u8 *helper_return);

#endif

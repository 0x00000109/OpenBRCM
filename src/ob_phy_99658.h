/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_99658_H_
#define _OB_PHY_99658_H_

#include "ob_phy.h"

#define OB_PHY_99658_MAX_TRANSFER_BYTES 7u
#define OB_PHY_99658_MAX_SOURCE_BYTES 7u
#define OB_PHY_99658_CHAINS 2u
#define OB_PHY_99658_SCRATCH_COPIES 4u

enum ob_phy_99658_policy {
	OB_PHY_99658_HISTORICAL_STRICT = 0,
	OB_PHY_99658_SYNTHETIC_PARAMETERIZED = 1,
};

enum ob_phy_99658_source_id {
	OB_PHY_99658_SRC_INVALID = 0,
	OB_PHY_99658_ARG1_CHANSPEC_LOW = 1,
	OB_PHY_99658_ARG1_BAND_1000,
	OB_PHY_99658_ARG1_BAND_1800,
	OB_PHY_99658_ARG1_OTHER_BAND,
	OB_PHY_99658_ARG2_LOW_66E_ZERO,
	OB_PHY_99658_ARG2_LOW_66E_NONZERO,
	OB_PHY_99658_ARG2_HIGH_BAND,
	OB_PHY_99658_998CC_STATIC_ROW,
};

enum ob_phy_99658_result_kind {
	OB_PHY_99658_RESULT_INVALID = 0,
	OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT,
	OB_PHY_99658_HOST_SIMULATION_COMPLETE,
	OB_PHY_99658_FAILED_BACKEND_OPERATION,
};

/* source_bytes are supplied by the caller so the source tree does not embed
 * historical vendor tables. The historical TABLE API consumes only the
 * initialized source-length prefix; no synthetic stack-tail bytes exist. */
struct ob_phy_99658_input {
	bool explicitly_synthetic;
	enum ob_phy_99658_policy policy;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u8 chain_count;
	u8 argument;
	bool chanspec_present;
	u16 chanspec;
	bool table_offset_present;
	u8 table_offset;
	bool transfer_count_present;
	u8 transfer_count;
	bool nested_66a_present;
	u8 nested_66a;
	bool nested_66b_present;
	u8 nested_66b;
	bool nested_66e_present;
	u8 nested_66e;
	bool nested_340_present;
	u8 nested_340;
	bool nested_3e0_present;
	u8 nested_3e0;
	enum ob_phy_99658_source_id source_id;
	const u8 *source_bytes;
	u8 source_length;
};

/* sub_998cc is the caller operation between the two sub_99658 calls (and
 * again after the second). Its selected transfers also consume exactly the
 * initialized 6- or 7-byte prefix. */
struct ob_phy_998cc_input {
	bool explicitly_synthetic;
	enum ob_phy_99658_policy policy;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u8 chain_count;
	u8 argument;
	bool table_offset_present;
	u8 table_offset;
	bool nested_66c_present;
	u8 nested_66c;
	bool nested_66d_present;
	u8 nested_66d;
	enum ob_phy_99658_source_id source_id;
	const u8 *source_bytes;
	u8 source_length;
};

struct ob_phy_99658_ops {
	/* One callback models one historical wlc_phy_table_write_acphy API call.
	 * The byte span is the exact caller memory consumed by width/count. */
	int (*table_write)(void *ctx, u32 source_site, u8 invocation_argument,
			   u8 chain, u8 table_id, u8 offset,
			   u8 element_count, u8 element_width_bits,
			   const u8 *payload, u8 payload_length);
};

struct ob_phy_99658_scratch_copy {
	u8 chain;
	u16 destination_offset;
	u8 length;
	u8 bytes[OB_PHY_99658_MAX_SOURCE_BYTES];
};

struct ob_phy_99658_result {
	enum ob_phy_99658_result_kind kind;
	u8 callback_count;
	u8 table_api_write_count;
	u32 terminal_boundary;
	bool copied_state_present;
	u16 copied_state_offset;
	u8 copied_state_value;
	u8 scratch_copy_count;
	struct ob_phy_99658_scratch_copy
		scratch_copies[OB_PHY_99658_SCRATCH_COPIES];
	bool historical_exact;
	bool hardware_ready;
};

struct ob_phy_9af05_table_stage_input {
	const struct ob_phy_99658_input *arg1;
	const struct ob_phy_998cc_input *between;
	const struct ob_phy_99658_input *arg2;
	const struct ob_phy_998cc_input *after;
};

struct ob_phy_9af05_table_stage_result {
	enum ob_phy_99658_result_kind kind;
	u8 sub_99658_writes;
	u8 sub_998cc_writes;
	u8 callback_count;
	u32 terminal_boundary;
	bool historical_exact;
	bool hardware_ready;
};

int ob_phy_acphy_99658_test_only(
	const struct ob_phy_99658_ops *ops, void *ctx,
	const struct ob_phy_99658_input *input,
	struct ob_phy_99658_result *result);

int ob_phy_acphy_998cc_test_only(
	const struct ob_phy_99658_ops *ops, void *ctx,
	const struct ob_phy_998cc_input *input,
	struct ob_phy_99658_result *result);

/* Synthetic-only exact caller order through the return from sub_998cc(PI,2):
 * sub_99658(1), sub_998cc(1), sub_99658(2), sub_998cc(2). */
int ob_phy_9af05_table_stage_test_only(
	const struct ob_phy_99658_ops *ops, void *ctx,
	const struct ob_phy_9af05_table_stage_input *input,
	struct ob_phy_9af05_table_stage_result *result);

#endif /* _OB_PHY_99658_H_ */

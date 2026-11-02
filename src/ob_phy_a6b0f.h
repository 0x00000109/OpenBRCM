/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_A6B0F_H_
#define _OB_PHY_A6B0F_H_

#include "ob_phy_a0be4.h"

enum ob_phy_a6b0f_operation {
	OB_PHY_A6B0F_PHY_READ,
	OB_PHY_A6B0F_PHY_RMW,
	OB_PHY_A6B0F_TABLE_READ,
	OB_PHY_A6B0F_TABLE_WRITE,
};

enum ob_phy_a6b0f_expression {
	OB_PHY_A6B0F_EXPR_LITERAL,
	OB_PHY_A6B0F_EXPR_PHY_SET_BIT,
	OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT,
	OB_PHY_A6B0F_EXPR_TABLE_RUNTIME_READ,
	OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK,
	OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW,
	OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH,
	OB_PHY_A6B0F_EXPR_CHILD_SNAPSHOT,
	OB_PHY_A6B0F_EXPR_CHILD_RESTORE_BIT,
};

/* One source-vector logical event. A nonzero batch_count marks the first
 * element of an accepted multi-element historical table API call. */
struct ob_phy_a6b0f_event {
	u16 local_sequence;
	u16 source_sequence;
	enum ob_phy_a6b0f_operation operation;
	u32 source_site;
	u16 selector;
	u16 offset;
	u8 width_bits;
	u16 mask;
	u16 value;
	enum ob_phy_a6b0f_expression expression;
	u16 read_source_local;
	u8 snapshot_group;
	u8 chain;
	u8 slot;
	u8 batch_count;
};

struct ob_phy_a6b0f_ops {
	struct ob_phy_a0be4_ops prefix;
	int (*table_read)(void *ctx, u8 local_event, u32 source_site,
			  u8 table_id, u16 offset, u16 element_count,
			  u8 element_width_bits, u16 *values);
	int (*table_write)(void *ctx, u8 local_event, u32 source_site,
			   u8 table_id, u16 offset, u16 element_count,
			   u8 element_width_bits, const u16 *values);
};

/* All input values here are explicit synthetic fixture inputs. The runner is
 * callback-only and has no normal-probe caller or real hardware backend. */
struct ob_phy_a6b0f_fixture {
	struct ob_phy_a0be4_fixture initial_prefix;
	bool a0be4_fixture_completed;
	u8 nested_34c;
	u8 nested_342;
	bool boardflags2_supplied;
	u32 boardflags2;
};

enum ob_phy_a6b0f_result_kind {
	OB_PHY_A6B0F_RESULT_INVALID = 0,
	OB_PHY_A6B0F_SELECTED_FIXTURE_COMPLETE = 1,
};

struct ob_phy_a6b0f_result {
	enum ob_phy_a6b0f_result_kind kind;
	u32 logical_event_count;
	u32 callback_count;
	bool boardflags2_mask_set;
	bool hardware_ready;
};

int ob_phy_acphy_a6b0f_selected_fixture_test_only(
	const struct ob_phy_a6b0f_ops *ops, void *ctx,
	const struct ob_phy_a6b0f_fixture *fixture,
	struct ob_phy_a6b0f_result *result);

struct ob_phy_a6b0f_composed_result {
	struct ob_phy_a0be4_composed_result through_a0be4;
	struct ob_phy_a6b0f_result a6b0f;
	u32 logical_event_count;
	u32 callback_count;
	u32 terminal_return_site;
	bool hardware_ready;
};

/* Synthetic LPF -> caller stores -> initial A7089 -> A0BE4 -> A6B0F. */
int ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
	const struct ob_phy_a6b0f_ops *ops, void *ctx,
	const struct ob_phy_a6b0f_fixture *fixture,
	struct ob_phy_a6b0f_composed_result *result);

#endif /* _OB_PHY_A6B0F_H_ */

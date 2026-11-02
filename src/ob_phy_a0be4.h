/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_A0BE4_H_
#define _OB_PHY_A0BE4_H_

#include "ob_phy_post_lpf.h"

#define OB_PHY_A0BE4_LOGICAL_EVENT_COUNT 40u

enum ob_phy_a0be4_operation {
	OB_PHY_A0BE4_PHY_OR,
	OB_PHY_A0BE4_PHY_WRITE,
	OB_PHY_A0BE4_PHY_RMW_MASKED_MERGE,
	OB_PHY_A0BE4_AI_CORE_CFLAGS,
	OB_PHY_A0BE4_PHY_READ,
	OB_PHY_A0BE4_PHY_WRITE_DERIVED,
	OB_PHY_A0BE4_DELAY_US,
	OB_PHY_A0BE4_PHYCLK_FGC,
};

enum ob_phy_a0be4_expression {
	OB_PHY_A0BE4_EXPR_LITERAL,
	OB_PHY_A0BE4_EXPR_SNAPSHOT_OR,
	OB_PHY_A0BE4_EXPR_SNAPSHOT_AND,
};

struct ob_phy_a0be4_event {
	u8 logical_event;
	enum ob_phy_a0be4_operation operation;
	u32 source_site;
	u16 register_offset;
	u8 width_bits;
	u32 mask;
	u32 value;
	enum ob_phy_a0be4_expression expression;
	u8 expression_source_event;
	u8 chain;
	u16 duration_us;
};

/*
 * This callback-only runner admits only the explicit key-1 / row-63,
 * chanspec-0x1001 synthetic invocation. The PI chanspec and nested selector
 * have not been proven for target initialization. There is no live backend
 * and no normal-probe caller.
 */
struct ob_phy_a0be4_fixture {
	bool selected_a7089_fixture_completed;
	u8 si_core_type; /* accepted target alternatives are AI-compatible 1 or 3 */
	struct ob_phy_post_lpf_fixture phy;
	struct ob_phy_post_lpf_entry_state entry;
};

enum ob_phy_a0be4_result_kind {
	OB_PHY_A0BE4_RESULT_INVALID = 0,
	OB_PHY_A0BE4_SELECTED_FIXTURE_COMPLETE = 1,
};

struct ob_phy_a0be4_result {
	enum ob_phy_a0be4_result_kind kind;
	u32 logical_event_count;
	u32 callback_count;
	bool hardware_ready;
};

struct ob_phy_a0be4_ops {
	struct ob_phy_post_lpf_ops phy;
	int (*read_ai_core32)(void *ctx, u8 logical_event, u32 source_site,
			      u16 register_offset, u32 *value);
	int (*write_ai_core32)(void *ctx, u8 logical_event, u32 source_site,
			       u16 register_offset, u32 value);
};

/*
 * Executes the accepted selected helper event vector in software-defined
 * order. Callback errors are returned unchanged; no callback follows a
 * failed callback. Result fields are published only after event 40 succeeds.
 */
int ob_phy_acphy_a0be4_selected_fixture_test_only(
	const struct ob_phy_a0be4_ops *ops, void *ctx,
	const struct ob_phy_a0be4_fixture *fixture,
	struct ob_phy_a0be4_result *result);

struct ob_phy_a0be4_composed_result {
	struct ob_phy_post_lpf_composed_result through_a7089;
	struct ob_phy_a0be4_result a0be4;
	u32 callback_count;
	u32 terminal_return_site;
	bool hardware_ready;
};

/* LPF + two caller stores + the selected 88-event a7089 fixture + a0be4. */
int ob_phy_acphy_initial_fixture_through_a0be4_test_only(
	const struct ob_phy_a0be4_ops *ops, void *ctx,
	const struct ob_phy_a0be4_fixture *fixture,
	struct ob_phy_a0be4_composed_result *result);

#endif /* _OB_PHY_A0BE4_H_ */

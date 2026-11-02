/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_96203_H_
#define _OB_PHY_96203_H_

#include "ob_phy_post_lpf.h"

enum ob_phy_96203_status {
	OB_96203_INVALID = 0,
	OB_96203_SKIPPED_BY_CALLER,
	OB_96203_SYNTHETIC_COMPLETE,
	OB_96203_FAILED_BACKEND_OPERATION,
	OB_96203_LIVE_COMPLETE,
};

struct ob_phy_96203_input {
	bool explicitly_synthetic;
	u32 chip_id;
	u8 chip_revision;
	/* Explicit synthetic profile constraint, not a hardware observation. */
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	bool acphychipid_present;
	u32 acphychipid;
	bool loop_bound_present;
	u8 loop_bound; /* PI+0x168; this target profile supports 0..2. */
	bool second_loop_gate_present;
	u8 second_loop_gate; /* PI+0x16c, unsigned byte. */
	bool caller_gate_present;
	u8 reset_marker;
	u8 nested_32c;
	bool pi_164_present;
	u32 pi_164;
};

struct ob_phy_96203_result {
	enum ob_phy_96203_status status;
	u32 logical_event_count;
	u32 callback_count;
	u32 terminal_boundary;
	u32 failed_site;
	u32 first_unsupported_operation;
	u8 outer_cores_completed;
	u8 inner_cores_completed;
	u8 poll_success_mask;
	u8 poll_exhausted_mask;
	u8 poll_success_attempt[2]; /* 1..10; zero means exhausted/not run. */
	bool caller_selected;
	bool clock_fgc_enabled;
	bool hardware_ready;
	bool phy_ready;
	bool channel_ready;
};

int ob_phy_96203_test_only(const struct ob_phy_post_lpf_ops *ops, void *ctx,
			   const struct ob_phy_96203_input *input,
			   struct ob_phy_96203_result *result);

/* Test-only caller seam: resetCCA -> delay_us(1) -> sub_96203. */
int ob_phy_96203_caller_test_only(const struct ob_phy_post_lpf_ops *ops,
				  void *ctx,
				  const struct ob_phy_96203_input *input,
				  struct ob_phy_96203_result *result);

/* Reuse the exact resetCCA helper body from a later bounded caller join.
 * prior_logical_events continues the callback event index for a persistent
 * test backend; it does not add those prior events to this result. */
int ob_phy_96203_resetcca_test_only(const struct ob_phy_post_lpf_ops *ops,
				    void *ctx,
				    const struct ob_phy_96203_input *input,
				    u32 prior_logical_events,
				    struct ob_phy_96203_result *result);

/*
 * Live entry: the same helper body, with the identity taken from the attach
 * state instead of a synthetic profile. Nothing here is a fixture claim, so
 * every field the helper reads must be owned when the call is made.
 *
 *   loop_bound         chain count; 2 on this target (the vendor touches the
 *                      0x07xx and 0x09xx banks and never 0x0bxx)
 *   acphychipid        selects the radio map in ob_96203_set_map()
 *   second_loop_gate   PI+0x16c; has no owned source in the live driver, so
 *                      the caller must pass it explicitly and record that it
 *                      is a hypothesis. With gate_owned == false the call is
 *                      refused BEFORE any MMIO.
 *
 * Fail-stop: a backend error is returned unresolved, already-issued
 * operations are not rolled back, and no later site is issued.
 */
struct ob_phy_96203_live_input {
	bool identity_valid;
	u32 acphychipid;
	bool acphychipid_owned;
	u8 loop_bound;
	bool second_loop_gate_owned;
	u8 second_loop_gate;
};

int ob_phy_96203_live_run(const struct ob_phy_post_lpf_ops *ops, void *ctx,
			  const struct ob_phy_96203_live_input *input,
			  struct ob_phy_96203_result *result);

#endif

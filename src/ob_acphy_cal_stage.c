// SPDX-License-Identifier: GPL-2.0-only

/*
 * M1-3 — read-only calibration pre-state witness.
 *
 * The module is a pure operation set: no MMIO of its own, no kernel state, no
 * writes.  The hardware binding lives in ob_phy.c
 * (ob_acphy_calibration_stage_witness), and the ops struct it fills has no
 * write callback.
 *
 * The readiness loop is not reimplemented here: ob_acphy_cal_poll380_run()
 * already owns the vendor's finite sub_abc76 poll (2001 reads / 2000 delays of
 * 10 us, counter 0x4e29 -> 9 in steps of 10) and its fail-closed timeout.  This
 * module adapts the stage ops to that operation set so there is exactly one
 * implementation of the poll in the tree.
 */

#include "ob_acphy_cal_stage.h"

struct ob_acphy_cal_stage_bridge {
	const struct ob_acphy_cal_stage_ops *ops;
	void *ctx;
};

static int ob_acphy_cal_stage_bridge_read(void *ctx, u8 event, u32 source_site,
					  u16 reg, u16 *value)
{
	struct ob_acphy_cal_stage_bridge *bridge = ctx;

	return bridge->ops->phy_read16(bridge->ctx, event, source_site, reg,
				       value);
}

static int ob_acphy_cal_stage_bridge_delay(void *ctx, u8 event, u32 site,
					   u16 usec)
{
	struct ob_acphy_cal_stage_bridge *bridge = ctx;

	return bridge->ops->delay_us(bridge->ctx, event, site, usec);
}

static bool ob_acphy_cal_stage_bridge_lost(void *ctx)
{
	struct ob_acphy_cal_stage_bridge *bridge = ctx;

	return bridge->ops->device_lost &&
	       bridge->ops->device_lost(bridge->ctx);
}

static bool ob_acphy_cal_stage_lost(const struct ob_acphy_cal_stage_ops *ops,
				    void *ctx)
{
	return ops->device_lost && ops->device_lost(ctx);
}

static int ob_acphy_cal_stage_sample(const struct ob_acphy_cal_stage_ops *ops,
				     void *ctx,
				     struct ob_acphy_cal_stage_result *result,
				     u32 site, u16 reg, u16 *value)
{
	u8 event = (u8)(result->readiness_reads + 1u);
	int ret;

	result->last_site = site;
	ret = ops->phy_read16(ctx, event, site, reg, value);
	if (ret) {
		result->status = ob_acphy_cal_stage_lost(ops, ctx) ?
			OB_ACPHY_CAL_STAGE_FAILED_DEVICE_LOST :
			OB_ACPHY_CAL_STAGE_FAILED_CALLBACK;
		result->failure_ret = ret;
		return ret;
	}
	if (ob_acphy_cal_stage_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_STAGE_FAILED_DEVICE_LOST;
		result->failure_ret = -5;
		return -5;
	}
	return 0;
}

int ob_acphy_cal_stage_witness_run(const struct ob_acphy_cal_stage_ops *ops,
				   void *ctx,
				   struct ob_acphy_cal_stage_result *result)
{
	struct ob_acphy_cal_stage_bridge bridge;
	struct ob_acphy_cal_ops poll_ops;
	struct ob_acphy_cal_poll_result poll;
	int ret;

	if (!ops || !result || !ops->phy_read16 || !ops->delay_us)
		return -22;
	*result = (struct ob_acphy_cal_stage_result) {
		.status = OB_ACPHY_CAL_STAGE_INVALID,
	};

	ret = ob_acphy_cal_stage_sample(ops, ctx, result,
					OB_ACPHY_CAL_STAGE_SITE_READ_0401,
					OB_ACPHY_CAL_STAGE_REG_0401,
					&result->phy_0401);
	if (ret)
		return 0;
	ret = ob_acphy_cal_stage_sample(ops, ctx, result,
					OB_ACPHY_CAL_STAGE_SITE_READ_0400,
					OB_ACPHY_CAL_STAGE_REG_0400,
					&result->phy_0400);
	if (ret)
		return 0;
	ret = ob_acphy_cal_stage_sample(ops, ctx, result,
					OB_ACPHY_CAL_STAGE_SITE_READ_0070,
					OB_ACPHY_CAL_STAGE_REG_0070,
					&result->phy_0070);
	if (ret)
		return 0;

	/*
	 * Reuse the recovered poll contract.  It reports its own status; the
	 * stage maps it without weakening the timeout: EXHAUSTED is a result,
	 * not a pass.
	 */
	bridge.ops = ops;
	bridge.ctx = ctx;
	poll_ops = (struct ob_acphy_cal_ops) {
		.phy_read16 = ob_acphy_cal_stage_bridge_read,
		.delay_us = ob_acphy_cal_stage_bridge_delay,
		.device_lost = ob_acphy_cal_stage_bridge_lost,
	};
	ret = ob_acphy_cal_poll380_run(&poll_ops, &bridge, &poll);
	result->readiness_last = poll.last_value;
	result->readiness_reads = poll.reads;
	result->readiness_delays = poll.delays;
	result->last_site = OB_ACPHY_CAL_VENDOR_POLL_READ;

	if (poll.status == OB_ACPHY_CAL_POLL_CLEARED) {
		/* reads == 1 means the bits were already clear at entry. */
		result->status = poll.reads == 1 ?
			OB_ACPHY_CAL_STAGE_WITNESS_OK :
			OB_ACPHY_CAL_STAGE_READY_AFTER_POLL;
	} else if (poll.status == OB_ACPHY_CAL_POLL_EXHAUSTED) {
		result->status = OB_ACPHY_CAL_STAGE_READY_EXHAUSTED;
		result->readiness_exhausted = true;
		result->failure_ret = ret;
	} else if (poll.status == OB_ACPHY_CAL_POLL_DEVICE_LOST) {
		result->status = OB_ACPHY_CAL_STAGE_FAILED_DEVICE_LOST;
		result->failure_ret = ret;
	} else {
		result->status = OB_ACPHY_CAL_STAGE_FAILED_CALLBACK;
		result->failure_ret = ret;
	}

	/*
	 * Invariants of this stage.  They are set after every branch so no
	 * future edit can publish an owned context or a calibration operation
	 * by accident.
	 */
	result->entry_context_owned = false;
	result->calibration_issued = false;
	return 0;
}

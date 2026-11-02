// SPDX-License-Identifier: GPL-2.0-only

/*
 * M1-6 — vendor 0x0070 contract: save -> disable -> restore, inside the
 * MAC-suspend / phyreg bracket.
 *
 * The stage re-reads the M1-3 pre-state first and refuses with zero writes
 * unless the tuple and the PHY[0x0380] readiness result still match the
 * confirmed baseline.  It then reads OpenBRCM's shadow of the vendor
 * pi+0xfa0 enable byte and refuses before any side effect when the restore
 * would need the enable arm (saved != 0), because that arm consumes the
 * per-chain estimate table (phy+0x45a, phy+0x168) which this driver does not
 * own.
 *
 * With saved == 0 the vendor shape is: MAC suspend, phyreg enter, disable arm,
 * work, restore (= the same disable arm), phyreg exit, MAC resume.  The two
 * writes are value-identity by construction: the arm re-reads 0x0070 and must
 * see exactly the admitted word before the RMW.
 *
 * Every early exit inside the bracket is unwound: the stage never leaves the
 * MAC suspended or the phyreg window entered because a later step failed.
 *
 * Write accounting: every write callback is issued through the ops struct and
 * every write is recorded before the callback runs, so a backend that fails
 * after the store still counts as an attempted write.
 */

#include "ob_acphy_cal_write_stage.h"

struct ob_acphy_cal_write_stage_bridge {
	const struct ob_acphy_cal_write_stage_ops *ops;
	void *ctx;
	u8 *seq;
};

static int ob_acphy_cal_write_bridge_read(void *ctx, u8 event, u32 source_site,
					  u16 reg, u16 *value)
{
	struct ob_acphy_cal_write_stage_bridge *bridge = ctx;

	return bridge->ops->phy_read16(bridge->ctx, event, source_site, reg,
				       value);
}

static int ob_acphy_cal_write_bridge_delay(void *ctx, u8 event, u32 site,
					   u16 usec)
{
	struct ob_acphy_cal_write_stage_bridge *bridge = ctx;

	return bridge->ops->delay_us(bridge->ctx, event, site, usec);
}

static bool ob_acphy_cal_write_bridge_lost(void *ctx)
{
	struct ob_acphy_cal_write_stage_bridge *bridge = ctx;

	return bridge->ops->device_lost &&
	       bridge->ops->device_lost(bridge->ctx);
}

static bool ob_acphy_cal_write_lost(
	const struct ob_acphy_cal_write_stage_ops *ops, void *ctx)
{
	return ops->device_lost && ops->device_lost(ctx);
}

/* Classify a callback error as loss when the latch is set. */
static void ob_acphy_cal_write_fail(
	const struct ob_acphy_cal_write_stage_ops *ops, void *ctx,
	struct ob_acphy_cal_write_stage_result *result, int ret,
	enum ob_acphy_cal_write_stage_status fallback)
{
	result->status = ob_acphy_cal_write_lost(ops, ctx) ?
		OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST : fallback;
	result->failure_site = result->last_site;
	result->failure_ret = ret;
}

static int ob_acphy_cal_write_sample(
	const struct ob_acphy_cal_write_stage_ops *ops, void *ctx,
	struct ob_acphy_cal_write_stage_result *result, u8 *seq, u32 site,
	u16 reg, u16 *value)
{
	int ret;

	result->last_site = site;
	ret = ops->phy_read16(ctx, (u8)(*seq + 1u), site, reg, value);
	(*seq)++;
	if (ret) {
		ob_acphy_cal_write_fail(ops, ctx, result, ret,
					OB_ACPHY_CAL_WRITE_FAILED_CALLBACK);
		return ret;
	}
	if (ob_acphy_cal_write_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return -5;
	}
	return 0;
}

static int ob_acphy_cal_write_apply(
	const struct ob_acphy_cal_write_stage_ops *ops, void *ctx,
	struct ob_acphy_cal_write_stage_result *result, u8 *seq, u32 site,
	u16 value)
{
	int ret;

	result->last_site = site;
	/* Count the attempt before the callback: a failing backend may have
	 * stored the word already. */
	result->writes_issued++;
	ret = ops->phy_write16(ctx, (u8)(*seq + 1u), site,
			       OB_ACPHY_CAL_WRITE_REG_0070, value);
	(*seq)++;
	if (ret) {
		ob_acphy_cal_write_fail(ops, ctx, result, ret,
					OB_ACPHY_CAL_WRITE_FAILED_CALLBACK);
		return ret;
	}
	if (ob_acphy_cal_write_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return -5;
	}
	return 0;
}

/*
 * One bracket call.  Sets the matching progress flag only after the call
 * succeeded and the device is still alive; a failure is reported as
 * BRACKET_FAILED (or DEVICE_LOST) and left to the caller to unwind.
 */
static int ob_acphy_cal_write_bracket(
	const struct ob_acphy_cal_write_stage_ops *ops, void *ctx,
	struct ob_acphy_cal_write_stage_result *result, u8 *seq, u32 site,
	int (*cb)(void *ctx, u8 event, u32 source_site), bool *flag)
{
	int ret;

	result->last_site = site;
	ret = cb(ctx, (u8)(*seq + 1u), site);
	(*seq)++;
	if (ret) {
		ob_acphy_cal_write_fail(ops, ctx, result, ret,
					OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
		return ret;
	}
	if (ob_acphy_cal_write_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return -5;
	}
	if (flag)
		*flag = true;
	return 0;
}

/*
 * Close whatever the bracket opened, in vendor order (phyreg exit, then MAC
 * resume).  Best effort: the primary status is preserved unless a close call
 * itself fails, and a lost device always wins.
 */
static void ob_acphy_cal_write_close(
	const struct ob_acphy_cal_write_stage_ops *ops, void *ctx,
	struct ob_acphy_cal_write_stage_result *result, u8 *seq,
	bool retry_exit)
{
	int ret;

	if (retry_exit && result->phyreg_entered) {
		result->last_site = OB_ACPHY_CAL_WRITE_SITE_PHYREG_EXIT;
		ret = ops->phyreg_exit(ctx, (u8)(*seq + 1u),
				       OB_ACPHY_CAL_WRITE_SITE_PHYREG_EXIT);
		(*seq)++;
		if (!ret && !ob_acphy_cal_write_lost(ops, ctx)) {
			result->phyreg_entered = false;
		} else if (result->status !=
			   OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST) {
			ob_acphy_cal_write_fail(
				ops, ctx, result, ret,
				OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
		}
	}
	if (result->mac_suspended) {
		result->last_site = OB_ACPHY_CAL_WRITE_SITE_MAC_RESUME;
		ret = ops->mac_resume(ctx, (u8)(*seq + 1u),
				      OB_ACPHY_CAL_WRITE_SITE_MAC_RESUME);
		(*seq)++;
		if (!ret && !ob_acphy_cal_write_lost(ops, ctx)) {
			result->mac_suspended = false;
		} else if (result->status !=
			   OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST) {
			ob_acphy_cal_write_fail(
				ops, ctx, result, ret,
				OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
		}
	}
}

static bool ob_acphy_cal_write_prestate_ok(
	const struct ob_acphy_cal_write_stage_result *result)
{
	/* The confirmed baseline had the readiness bits already clear on the
	 * first sample (one read, no delay).  A longer poll is a different
	 * state and is refused rather than written through. */
	return !result->pre_ready_exhausted &&
	       result->pre_ready_reads == 1u &&
	       result->pre_ready_delays == 0u &&
	       result->pre_ready_last == OB_ACPHY_CAL_WRITE_EXPECT_READY &&
	       result->pre_0401 == OB_ACPHY_CAL_WRITE_EXPECT_0401 &&
	       result->pre_0400 == OB_ACPHY_CAL_WRITE_EXPECT_0400 &&
	       result->pre_0070 == OB_ACPHY_CAL_WRITE_EXPECT_0070;
}

int ob_acphy_cal_write_stage_run(
	const struct ob_acphy_cal_write_stage_ops *ops, void *ctx,
	struct ob_acphy_cal_write_stage_result *result)
{
	struct ob_acphy_cal_write_stage_bridge bridge;
	struct ob_acphy_cal_ops poll_ops;
	struct ob_acphy_cal_poll_result poll;
	u8 seq = 0;
	u8 saved = 0;
	u16 value;
	u16 written;
	u16 readback;
	unsigned int arm;
	int ret;

	if (!ops || !result || !ops->phy_read16 || !ops->phy_write16 ||
	    !ops->delay_us)
		return -22;
	*result = (struct ob_acphy_cal_write_stage_result) {
		.status = OB_ACPHY_CAL_WRITE_INVALID,
	};

	/* 1. Fresh pre-state: the same three words and the same vendor poll as
	 * M1-3, sampled before any write is considered. */
	ret = ob_acphy_cal_write_sample(ops, ctx, result, &seq,
					OB_ACPHY_CAL_STAGE_SITE_READ_0401,
					OB_ACPHY_CAL_STAGE_REG_0401,
					&result->pre_0401);
	if (ret)
		return 0;
	ret = ob_acphy_cal_write_sample(ops, ctx, result, &seq,
					OB_ACPHY_CAL_STAGE_SITE_READ_0400,
					OB_ACPHY_CAL_STAGE_REG_0400,
					&result->pre_0400);
	if (ret)
		return 0;
	ret = ob_acphy_cal_write_sample(ops, ctx, result, &seq,
					OB_ACPHY_CAL_STAGE_SITE_READ_0070,
					OB_ACPHY_CAL_STAGE_REG_0070,
					&result->pre_0070);
	if (ret)
		return 0;

	bridge.ops = ops;
	bridge.ctx = ctx;
	bridge.seq = &seq;
	poll_ops = (struct ob_acphy_cal_ops) {
		.phy_read16 = ob_acphy_cal_write_bridge_read,
		.delay_us = ob_acphy_cal_write_bridge_delay,
		.device_lost = ob_acphy_cal_write_bridge_lost,
	};
	ret = ob_acphy_cal_poll380_run(&poll_ops, &bridge, &poll);
	result->pre_ready_last = poll.last_value;
	result->pre_ready_reads = poll.reads;
	result->pre_ready_delays = poll.delays;
	result->last_site = OB_ACPHY_CAL_VENDOR_POLL_READ;
	seq = (u8)(3u + poll.reads + poll.delays);
	if (poll.status == OB_ACPHY_CAL_POLL_EXHAUSTED) {
		result->pre_ready_exhausted = true;
	} else if (poll.status == OB_ACPHY_CAL_POLL_DEVICE_LOST) {
		result->status = OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST;
		result->failure_ret = ret;
		return 0;
	} else if (poll.status != OB_ACPHY_CAL_POLL_CLEARED) {
		result->status = OB_ACPHY_CAL_WRITE_FAILED_CALLBACK;
		result->failure_ret = ret;
		return 0;
	}

	if (!ob_acphy_cal_write_prestate_ok(result)) {
		result->status = OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED;
		return 0;
	}

	/* 2. The vendor saves pi+0xfa0 before the bracket (0x0b1497).  The byte
	 * is OpenBRCM's shadow: this driver has never issued the enable arm. */
	if (!ops->enable_state_get) {
		result->status = OB_ACPHY_CAL_WRITE_BRACKET_UNSUPPORTED;
		return 0;
	}
	ret = ops->enable_state_get(ctx, &saved);
	if (ret) {
		ob_acphy_cal_write_fail(ops, ctx, result, ret,
					OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
		return 0;
	}
	result->enable_state_saved = saved;
	/*
	 * A nonzero saved byte selects the vendor enable arm for the restore,
	 * which re-applies the per-chain estimates from phy+0x45a -- a table
	 * this stage does not own.  Refuse before any side effect: disabling a
	 * radio we cannot re-enable is worse than not running.
	 */
	if (saved != 0) {
		result->status = OB_ACPHY_CAL_WRITE_RESTORE_UNSUPPORTED;
		return 0;
	}
	result->restore_supported = true;

	if (!ops->mac_suspend || !ops->mac_resume || !ops->phyreg_enter ||
	    !ops->phyreg_exit) {
		result->status = OB_ACPHY_CAL_WRITE_BRACKET_UNSUPPORTED;
		return 0;
	}

	/* 3. Bracket open, in vendor order.  The suspend reports whether this
	 * call actually took the MAC down.  An already-quiesced MAC (this
	 * bench's attach state: MACCONTROL 0x04020402, path=already_quiesced)
	 * leaves mac_suspended false, so the close below never resumes a pause
	 * this stage does not own. */
	{
		bool we_suspended = false;

		result->last_site = OB_ACPHY_CAL_WRITE_SITE_MAC_SUSPEND;
		ret = ops->mac_suspend(ctx, (u8)(seq + 1u),
				       OB_ACPHY_CAL_WRITE_SITE_MAC_SUSPEND,
				       &we_suspended);
		seq++;
		if (ret) {
			ob_acphy_cal_write_fail(
				ops, ctx, result, ret,
				OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
			return 0;
		}
		if (ob_acphy_cal_write_lost(ops, ctx)) {
			result->status = OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST;
			result->failure_site = result->last_site;
			result->failure_ret = -5;
			return 0;
		}
		result->mac_suspended = we_suspended;
	}
	ret = ob_acphy_cal_write_bracket(ops, ctx, result, &seq,
					 OB_ACPHY_CAL_WRITE_SITE_PHYREG_ENTER,
					 ops->phyreg_enter,
					 &result->phyreg_entered);
	if (ret) {
		ob_acphy_cal_write_close(ops, ctx, result, &seq, true);
		return 0;
	}

	/* 4. Vendor shape: arm 0 is the disable (0x0b14ac -> 0x092608), arm 1
	 * is the restore of the saved byte (0x0b184f).  With saved == 0 the
	 * restore is the same disable arm, so both writes are value-identity. */
	for (arm = 0; arm < OB_ACPHY_CAL_WRITE_ARM_COUNT; arm++) {
		ret = ob_acphy_cal_write_sample(
			ops, ctx, result, &seq,
			arm == 0 ? OB_ACPHY_CAL_WRITE_SITE_READ_0070 :
				   OB_ACPHY_CAL_WRITE_SITE_RESTORE,
			OB_ACPHY_CAL_WRITE_REG_0070, &value);
		if (ret)
			goto out_bracket;
		result->arm_read[arm] = value;
		result->arms_sampled++;
		result->arm_predicate_false[arm] =
			(value & OB_ACPHY_CAL_WRITE_MASK_0070) !=
			OB_ACPHY_CAL_WRITE_MASK_0070;
		if (!result->arm_predicate_false[arm]) {
			/* The refill branch needs phy+0x45a/phy+0x168
			 * ownership that this stage does not have. */
			result->status = arm == 0 ?
				OB_ACPHY_CAL_WRITE_PREDICATE_TRUE_REFUSED :
				OB_ACPHY_CAL_WRITE_SECOND_ARM_DIVERGED;
			goto out_bracket;
		}
		/*
		 * Write-time admission (2026-10-28 review, issues 2 and A1):
		 * each arm must re-read exactly the admitted word.  The
		 * predicate check above owns the full-mask case; this guard
		 * owns every other drift, including the partial mask words this
		 * silicon actually produces (0x6100, 0x6500).  Without it the
		 * RMW below would clear real device bits while the markers
		 * still said value-identity.
		 */
		if (value != OB_ACPHY_CAL_WRITE_EXPECT_0070) {
			result->status = arm == 0 ?
				OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED :
				OB_ACPHY_CAL_WRITE_SECOND_ARM_DIVERGED;
			goto out_bracket;
		}
		written = (u16)(value & ~OB_ACPHY_CAL_WRITE_MASK_0070);
		result->arm_value[arm] = written;
		ret = ob_acphy_cal_write_apply(
			ops, ctx, result, &seq,
			OB_ACPHY_CAL_WRITE_SITE_RMW_0070, written);
		if (ret)
			goto out_bracket;
		ret = ob_acphy_cal_write_sample(
			ops, ctx, result, &seq,
			OB_ACPHY_CAL_WRITE_SITE_RMW_0070,
			OB_ACPHY_CAL_WRITE_REG_0070, &readback);
		if (ret)
			goto out_bracket;
		result->arm_readback[arm] = readback;
		if (readback != written) {
			result->status = OB_ACPHY_CAL_WRITE_READBACK_MISMATCH;
			goto out_bracket;
		}
		if (arm == 0) {
			ret = ops->delay_us(ctx, (u8)(seq + 1u),
					    OB_ACPHY_CAL_WRITE_SITE_RMW_0070,
					    OB_ACPHY_CAL_WRITE_SETTLE_US);
			seq++;
			if (ret) {
				ob_acphy_cal_write_fail(
					ops, ctx, result, ret,
					OB_ACPHY_CAL_WRITE_FAILED_CALLBACK);
				goto out_bracket;
			}
			ret = ob_acphy_cal_write_sample(
				ops, ctx, result, &seq,
				OB_ACPHY_CAL_WRITE_SITE_RMW_0070,
				OB_ACPHY_CAL_WRITE_REG_0070, &value);
			if (ret)
				goto out_bracket;
			result->stable_0070 = value;
			if (value != written) {
				result->status =
					OB_ACPHY_CAL_WRITE_READBACK_MISMATCH;
				goto out_bracket;
			}
		}
	}

	/* 5. Bracket close, in vendor order (0x0b1857, 0x0b1864). */
	result->last_site = OB_ACPHY_CAL_WRITE_SITE_PHYREG_EXIT;
	ret = ops->phyreg_exit(ctx, (u8)(seq + 1u),
			       OB_ACPHY_CAL_WRITE_SITE_PHYREG_EXIT);
	seq++;
	if (ret) {
		ob_acphy_cal_write_fail(ops, ctx, result, ret,
					OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
		/* The exit may have left the window open; do not retry it, just
		 * release the MAC it still holds. */
		ob_acphy_cal_write_close(ops, ctx, result, &seq, false);
		return 0;
	}
	if (ob_acphy_cal_write_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		ob_acphy_cal_write_close(ops, ctx, result, &seq, true);
		return 0;
	}
	result->phyreg_entered = false;

	/* Resume only a MAC this stage suspended.  When the MAC was already
	 * quiesced at open (this bench's attach state) the vendor bracket is
	 * satisfied by the exit alone, and resuming would undo the attach
	 * path's deliberate pause. */
	if (result->mac_suspended) {
		result->last_site = OB_ACPHY_CAL_WRITE_SITE_MAC_RESUME;
		ret = ops->mac_resume(ctx, (u8)(seq + 1u),
				      OB_ACPHY_CAL_WRITE_SITE_MAC_RESUME);
		seq++;
		if (ret) {
			ob_acphy_cal_write_fail(ops, ctx, result, ret,
						OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
			return 0;
		}
		if (ob_acphy_cal_write_lost(ops, ctx)) {
			result->status = OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST;
			result->failure_site = result->last_site;
			result->failure_ret = -5;
			return 0;
		}
		result->mac_suspended = false;
	}
	result->bracket_completed = true;

	/* 6. Nothing around the arms may move. */
	ret = ob_acphy_cal_write_sample(ops, ctx, result, &seq,
					OB_ACPHY_CAL_STAGE_SITE_READ_0401,
					OB_ACPHY_CAL_STAGE_REG_0401,
					&result->post_0401);
	if (ret)
		return 0;
	ret = ob_acphy_cal_write_sample(ops, ctx, result, &seq,
					OB_ACPHY_CAL_STAGE_SITE_READ_0400,
					OB_ACPHY_CAL_STAGE_REG_0400,
					&result->post_0400);
	if (ret)
		return 0;
	ret = ob_acphy_cal_write_sample(ops, ctx, result, &seq,
					OB_ACPHY_CAL_WRITE_SITE_RMW_0070,
					OB_ACPHY_CAL_WRITE_REG_0070,
					&result->post_0070);
	if (ret)
		return 0;
	ret = ob_acphy_cal_write_sample(ops, ctx, result, &seq,
					OB_ACPHY_CAL_VENDOR_POLL_READ,
					OB_ACPHY_CAL_POLL_REG,
					&result->post_0380);
	if (ret)
		return 0;

	result->calibration_issued = false;
	result->entry_context_owned = false;

	if (result->post_0401 != result->pre_0401 ||
	    result->post_0400 != result->pre_0400 ||
	    result->post_0070 != result->pre_0070 ||
	    result->post_0380 != result->pre_ready_last) {
		result->status = OB_ACPHY_CAL_WRITE_POST_STATE_MOVED;
		return 0;
	}

	result->status = OB_ACPHY_CAL_WRITE_ARM_OK;
	return 0;

out_bracket:
	ob_acphy_cal_write_close(ops, ctx, result, &seq, true);
	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only

/*
 * W2 — vendor calibration-entry replay, one bracketed window.
 *
 * The stage owns the window shape, not the vendor bodies it binds:
 *   - the 43-operation setstate body is the already reconstructed
 *     wlc_phy_rxcore_setstate_acphy in src/ob_phy_a7089_txpwr.c, exported
 *     through a thin wrapper so there is exactly one implementation;
 *   - PHY[0x0380]'s bounded poll uses the vendor counter constants recovered
 *     for sub_abc76 (0x4e29 / step 10 / floor 9, mask 0xc000).
 *
 * Every early exit inside the bracket unwinds it: phyreg exit first, then the
 * MAC resume the shim owns.  No path resumes a pause the stage did not open.
 */

#include "ob_acphy_cal_stage.h"
#include "ob_acphy_cal_run.h"
#include "ob_acphy_cal_w2_replay.h"

struct ob_acphy_cal_w2_bridge {
	const struct ob_acphy_cal_w2_ops *ops;
	void *ctx;
	u8 *seq;
};

static bool ob_acphy_cal_w2_lost(const struct ob_acphy_cal_w2_ops *ops,
				 void *ctx)
{
	return ops->device_lost && ops->device_lost(ctx);
}

static void ob_acphy_cal_w2_fail(const struct ob_acphy_cal_w2_ops *ops,
				 void *ctx,
				 struct ob_acphy_cal_w2_result *result,
				 int ret,
				 enum ob_acphy_cal_w2_status fallback)
{
	result->status = ob_acphy_cal_w2_lost(ops, ctx) ?
		OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST : fallback;
	result->failure_site = result->last_site;
	result->failure_ret = ret;
}

static int ob_acphy_cal_w2_read(const struct ob_acphy_cal_w2_ops *ops,
				void *ctx,
				struct ob_acphy_cal_w2_result *result, u8 *seq,
				u32 site, u16 reg, u16 *value)
{
	int ret;

	result->last_site = site;
	ret = ops->phy_read16(ctx, (u8)(*seq + 1u), site, reg, value);
	(*seq)++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_FAILED_CALLBACK);
		return ret;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return -5;
	}
	return 0;
}

static int ob_acphy_cal_w2_write_0070(const struct ob_acphy_cal_w2_ops *ops,
				      void *ctx,
				      struct ob_acphy_cal_w2_result *result,
				      u8 *seq, u32 site, u16 value)
{
	int ret;

	result->last_site = site;
	result->writes_0070++;
	ret = ops->phy_write16(ctx, (u8)(*seq + 1u), site,
			       OB_ACPHY_CAL_W2_REG_0070, value);
	(*seq)++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		return ret;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return -5;
	}
	return 0;
}

static int ob_acphy_cal_w2_write_kick(const struct ob_acphy_cal_w2_ops *ops,
				      void *ctx,
				      struct ob_acphy_cal_w2_result *result,
				      u8 *seq, u16 value)
{
	int ret;

	result->last_site = OB_ACPHY_CAL_VENDOR_POLL_WRITE;
	result->writes_kick++;
	ret = ops->phy_write16(ctx, (u8)(*seq + 1u),
			       OB_ACPHY_CAL_VENDOR_POLL_WRITE,
			       OB_ACPHY_CAL_W2_REG_KICK, value);
	(*seq)++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		return ret;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return -5;
	}
	return 0;
}

/*
 * sub_abc76's poll @0x0ad442: exit when both 0xc000 bits are clear, loop
 * while they are set, delay 10 us per read from the vendor counter, and stop
 * at the finite bound.  Exhaustion is recorded, not hidden: the caller still
 * restores and closes the bracket before reporting it.
 */
static int ob_acphy_cal_w2_kick_poll(const struct ob_acphy_cal_w2_ops *ops,
				     void *ctx,
				     struct ob_acphy_cal_w2_result *result,
				     u8 *seq)
{
	u32 counter = OB_ACPHY_CAL_POLL_COUNTER_INIT;

	for (;;) {
		u16 sample;
		int ret;

		ret = ob_acphy_cal_w2_read(ops, ctx, result, seq,
					   OB_ACPHY_CAL_VENDOR_POLL_READ,
					   OB_ACPHY_CAL_W2_REG_KICK, &sample);
		if (ret)
			return ret;
		result->kick_reads++;
		if ((sample & OB_ACPHY_CAL_POLL_MASK) == 0)
			return 0;
		if (counter == OB_ACPHY_CAL_POLL_COUNTER_FLOOR) {
			result->kick_exhausted = true;
			return 0;
		}
		counter -= OB_ACPHY_CAL_POLL_COUNTER_STEP;
		result->last_site = OB_ACPHY_CAL_VENDOR_POLL_DELAY;
		ret = ops->delay_us(ctx, (u8)(*seq + 1u),
				    OB_ACPHY_CAL_VENDOR_POLL_DELAY,
				    OB_ACPHY_CAL_POLL_DELAY_US);
		(*seq)++;
		result->kick_delays++;
		if (ret) {
			ob_acphy_cal_w2_fail(ops, ctx, result, ret,
					     OB_ACPHY_CAL_W2_FAILED_CALLBACK);
			return ret;
		}
		if (ob_acphy_cal_w2_lost(ops, ctx)) {
			result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
			result->failure_site = result->last_site;
			result->failure_ret = -5;
			return -5;
		}
	}
}

/* Close what the stage opened, in vendor order: phyreg exit, then MAC. */
static void ob_acphy_cal_w2_close(const struct ob_acphy_cal_w2_ops *ops,
				  void *ctx,
				  struct ob_acphy_cal_w2_result *result,
				  u8 *seq, bool retry_exit)
{
	int ret;

	if (retry_exit && result->phyreg_entered) {
		result->last_site = OB_ACPHY_CAL_W2_SITE_BRACKET_EXIT;
		ret = ops->phyreg_exit(ctx, (u8)(*seq + 1u),
				       OB_ACPHY_CAL_W2_SITE_BRACKET_EXIT);
		(*seq)++;
		if (!ret && !ob_acphy_cal_w2_lost(ops, ctx)) {
			result->phyreg_entered = false;
		} else if (result->status !=
			   OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST) {
			ob_acphy_cal_w2_fail(ops, ctx, result, ret,
					     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		}
	}
	if (result->mac_suspended) {
		result->last_site = OB_ACPHY_CAL_W2_SITE_BRACKET_RESUME;
		ret = ops->mac_resume(ctx, (u8)(*seq + 1u),
				      OB_ACPHY_CAL_W2_SITE_BRACKET_RESUME);
		(*seq)++;
		if (!ret && !ob_acphy_cal_w2_lost(ops, ctx)) {
			result->mac_suspended = false;
		} else if (result->status !=
			   OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST) {
			ob_acphy_cal_w2_fail(ops, ctx, result, ret,
					     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		}
	}
}

/*
 * One value-identity arm: fresh read of the admitted word, RMW that clears the
 * 0xe000 mask, readback, and (disable arm only) the 1 ms settle plus a stable
 * sample.  Any read that is not the admitted word stops before the write.
 */
static int ob_acphy_cal_w2_arm(const struct ob_acphy_cal_w2_ops *ops,
			       void *ctx,
			       struct ob_acphy_cal_w2_result *result, u8 *seq,
			       unsigned int index, bool settle)
{
	u16 read;
	u16 written;
	u16 readback;
	int ret;

	ret = ob_acphy_cal_w2_read(ops, ctx, result, seq,
				   index == 0 ? OB_ACPHY_CAL_W2_SITE_READ_0070 :
						OB_ACPHY_CAL_W2_SITE_ARM_RESTORE,
				   OB_ACPHY_CAL_W2_REG_0070, &read);
	if (ret)
		return ret;
	result->arm_read[index] = read;
	if (read != OB_ACPHY_CAL_W2_POST_0070) {
		/* The boundary moved between setstate and this arm: refuse
		 * before the RMW rather than clear real device bits. */
		result->status = OB_ACPHY_CAL_W2_ARM_DIVERGED;
		return 0;
	}
	written = (u16)(read & ~OB_ACPHY_CAL_W2_MASK_0070);
	result->arm_value[index] = written;
	ret = ob_acphy_cal_w2_write_0070(ops, ctx, result, seq,
					 OB_ACPHY_CAL_W2_SITE_RMW_0070,
					 written);
	if (ret)
		return ret;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, seq,
				   OB_ACPHY_CAL_W2_SITE_RMW_0070,
				   OB_ACPHY_CAL_W2_REG_0070, &readback);
	if (ret)
		return ret;
	result->arm_readback[index] = readback;
	if (readback != written) {
		result->status = OB_ACPHY_CAL_W2_READBACK_MISMATCH;
		return 0;
	}
	if (!settle)
		return 0;
	ret = ops->delay_us(ctx, (u8)(*seq + 1u),
			    OB_ACPHY_CAL_W2_SITE_RMW_0070,
			    OB_ACPHY_CAL_W2_SETTLE_US);
	(*seq)++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_FAILED_CALLBACK);
		return ret;
	}
	ret = ob_acphy_cal_w2_read(ops, ctx, result, seq,
				   OB_ACPHY_CAL_W2_SITE_RMW_0070,
				   OB_ACPHY_CAL_W2_REG_0070, &readback);
	if (ret)
		return ret;
	result->stable_0070 = readback;
	if (readback != written) {
		result->status = OB_ACPHY_CAL_W2_READBACK_MISMATCH;
		return 0;
	}
	return 0;
}

int ob_acphy_cal_w2_replay_run(const struct ob_acphy_cal_w2_ops *ops,
			       void *ctx, bool kick_allowed, u16 kick_value,
			       struct ob_acphy_cal_w2_result *result)
{
	u8 seq = 0;
	bool transitioned = false;
	bool software_only = false;
	int ret;

	if (!ops || !result || !ops->phy_read16 || !ops->phy_write16 ||
	    !ops->delay_us || !ops->setstate)
		return -22;
	if (kick_allowed && kick_value != OB_ACPHY_CAL_W2_KICK_MIN &&
	    kick_value != OB_ACPHY_CAL_W2_KICK_MAX)
		return -22;

	*result = (struct ob_acphy_cal_w2_result) {
		.status = OB_ACPHY_CAL_W2_INVALID,
		.kick_allowed = kick_allowed,
		.kick_value = kick_value,
	};

	/* 1. Entry sample: the confirmed M1-3 tuple plus an idle 0x0380. */
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0401,
				   OB_ACPHY_CAL_STAGE_REG_0401,
				   &result->entry_0401);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0400,
				   OB_ACPHY_CAL_STAGE_REG_0400,
				   &result->entry_0400);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0070,
				   OB_ACPHY_CAL_W2_REG_0070,
				   &result->entry_0070);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_VENDOR_POLL_READ,
				   OB_ACPHY_CAL_W2_REG_KICK,
				   &result->entry_0380);
	if (ret)
		return 0;
	if (result->entry_0401 != OB_ACPHY_CAL_W2_ENTRY_0401 ||
	    result->entry_0400 != OB_ACPHY_CAL_W2_ENTRY_0400 ||
	    result->entry_0070 != OB_ACPHY_CAL_W2_ENTRY_0070 ||
	    (result->entry_0380 & OB_ACPHY_CAL_POLL_MASK) != 0) {
		result->status = OB_ACPHY_CAL_W2_ENTRY_REFUSED;
		return 0;
	}

	/* 2. The vendor's own setstate, with its own reference-counted MAC
	 * pair (0x97d13 / 0x97e1d).  A software-only arm means the vendor's
	 * shared+0x31 gate took the no-PHY path; the window cannot answer
	 * anything then. */
	result->last_site = OB_ACPHY_CAL_W2_SITE_SETSTATE_CALL;
	ret = ops->setstate(ctx, (u8)(seq + 1u),
			    OB_ACPHY_CAL_W2_SITE_SETSTATE_CALL,
			    OB_ACPHY_CAL_W2_STATE_2TX, &software_only);
	seq++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_SETSTATE_DIVERGED);
		return 0;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return 0;
	}
	result->setstate_called = true;
	result->setstate_software_only = software_only;
	if (software_only) {
		result->status = OB_ACPHY_CAL_W2_SOFTWARE_ONLY;
		return 0;
	}

	/* 3. Post-setstate boundary: exactly 0x7733, not its transients. */
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0401,
				   OB_ACPHY_CAL_STAGE_REG_0401,
				   &result->post_0401);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0400,
				   OB_ACPHY_CAL_STAGE_REG_0400,
				   &result->post_0400);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0070,
				   OB_ACPHY_CAL_W2_REG_0070,
				   &result->post_0070);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_VENDOR_POLL_READ,
				   OB_ACPHY_CAL_W2_REG_KICK,
				   &result->post_0380);
	if (ret)
		return 0;
	if (result->post_0401 != OB_ACPHY_CAL_W2_POST_0401 ||
	    result->post_0400 != OB_ACPHY_CAL_W2_POST_0400 ||
	    result->post_0070 != OB_ACPHY_CAL_W2_POST_0070 ||
	    (result->post_0380 & OB_ACPHY_CAL_POLL_MASK) != 0) {
		result->status = OB_ACPHY_CAL_W2_POST_REFUSED;
		return 0;
	}

	/* 4. Bracket open (vendor 0x0925f5 / 0x0925fd). */
	if (!ops->mac_suspend || !ops->mac_resume || !ops->phyreg_enter ||
	    !ops->phyreg_exit) {
		result->status = OB_ACPHY_CAL_W2_BRACKET_UNSUPPORTED;
		return 0;
	}
	result->last_site = OB_ACPHY_CAL_W2_SITE_BRACKET_SUSPEND;
	ret = ops->mac_suspend(ctx, (u8)(seq + 1u),
			       OB_ACPHY_CAL_W2_SITE_BRACKET_SUSPEND,
			       &transitioned);
	seq++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		return 0;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return 0;
	}
	result->mac_suspended = true;
	result->last_site = OB_ACPHY_CAL_W2_SITE_BRACKET_ENTER;
	ret = ops->phyreg_enter(ctx, (u8)(seq + 1u),
				OB_ACPHY_CAL_W2_SITE_BRACKET_ENTER);
	seq++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		ob_acphy_cal_w2_close(ops, ctx, result, &seq, false);
		return 0;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		ob_acphy_cal_w2_close(ops, ctx, result, &seq, false);
		return 0;
	}
	result->phyreg_entered = true;

	/* 5. Disable arm (0x090760): value-identity, 0xe000 cleared. */
	ret = ob_acphy_cal_w2_arm(ops, ctx, result, &seq, 0, true);
	if (ret || result->status != OB_ACPHY_CAL_W2_INVALID)
		goto out_bracket;

	/* 6. Kick and poll, with the bracket still open (0x0ad418/0x0ad442).
	 * The gate is checked against the live pre-read: a non-idle register is
	 * never written. */
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_VENDOR_POLL_READ,
				   OB_ACPHY_CAL_W2_REG_KICK,
				   &result->kick_pre);
	if (ret)
		goto out_bracket;
	if (!kick_allowed) {
		result->kick_skipped = true;
	} else if (result->kick_pre != 0x0000u) {
		result->kick_skipped = true;
	} else {
		ret = ob_acphy_cal_w2_write_kick(ops, ctx, result, &seq,
						 kick_value);
		if (ret)
			goto out_bracket;
		ret = ob_acphy_cal_w2_kick_poll(ops, ctx, result, &seq);
		/* Exhaustion is a bounded vendor outcome, not a reason to skip
		 * the restore: the arm and the close still run, and the status is
		 * reported after the bracket is balanced (R5-C refuted for this
		 * silicon).  Only a callback failure unwinds early. */
		if (ret)
			goto out_bracket;
	}

	/* 7. Restore arm (0x0b184f), same value-identity RMW. */
	ret = ob_acphy_cal_w2_arm(ops, ctx, result, &seq, 1, false);
	if (ret || result->status != OB_ACPHY_CAL_W2_INVALID)
		goto out_bracket;

	/* 8. Bracket close (0x0b1857 / 0x0b1864). */
	result->last_site = OB_ACPHY_CAL_W2_SITE_BRACKET_EXIT;
	ret = ops->phyreg_exit(ctx, (u8)(seq + 1u),
			       OB_ACPHY_CAL_W2_SITE_BRACKET_EXIT);
	seq++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		ob_acphy_cal_w2_close(ops, ctx, result, &seq, false);
		return 0;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		ob_acphy_cal_w2_close(ops, ctx, result, &seq, true);
		return 0;
	}
	result->phyreg_entered = false;
	result->last_site = OB_ACPHY_CAL_W2_SITE_BRACKET_RESUME;
	ret = ops->mac_resume(ctx, (u8)(seq + 1u),
			      OB_ACPHY_CAL_W2_SITE_BRACKET_RESUME);
	seq++;
	if (ret) {
		ob_acphy_cal_w2_fail(ops, ctx, result, ret,
				     OB_ACPHY_CAL_W2_BRACKET_FAILED);
		return 0;
	}
	if (ob_acphy_cal_w2_lost(ops, ctx)) {
		result->status = OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST;
		result->failure_site = result->last_site;
		result->failure_ret = -5;
		return 0;
	}
	result->mac_suspended = false;
	result->bracket_completed = true;

	/* 9. Identity after the bracket: the boundary words must not move.
	 * On the kicked path 0x0380 is exempt (it carries the operation being
	 * watched); on the skipped path it must still be the idle sample. */
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0401,
				   OB_ACPHY_CAL_STAGE_REG_0401,
				   &result->post_identity_0401);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0400,
				   OB_ACPHY_CAL_STAGE_REG_0400,
				   &result->post_identity_0400);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_STAGE_SITE_READ_0070,
				   OB_ACPHY_CAL_W2_REG_0070,
				   &result->post_identity_0070);
	if (ret)
		return 0;
	ret = ob_acphy_cal_w2_read(ops, ctx, result, &seq,
				   OB_ACPHY_CAL_VENDOR_POLL_READ,
				   OB_ACPHY_CAL_W2_REG_KICK,
				   &result->post_identity_0380);
	if (ret)
		return 0;
	result->calibration_issued = false;
	result->entry_context_owned = false;
	if (result->post_identity_0401 != OB_ACPHY_CAL_W2_POST_0401 ||
	    result->post_identity_0400 != OB_ACPHY_CAL_W2_POST_0400 ||
	    result->post_identity_0070 != OB_ACPHY_CAL_W2_POST_0070 ||
	    (result->writes_kick == 0 &&
	     result->post_identity_0380 != result->entry_0380)) {
		result->status = OB_ACPHY_CAL_W2_POST_STATE_MOVED;
		return 0;
	}

	if (result->kick_exhausted)
		result->status = OB_ACPHY_CAL_W2_KICK_EXHAUSTED;
	else if (result->kick_skipped)
		result->status = OB_ACPHY_CAL_W2_KICK_SKIPPED;
	else
		result->status = OB_ACPHY_CAL_W2_FULL;
	return 0;

out_bracket:
	ob_acphy_cal_w2_close(ops, ctx, result, &seq, true);
	return 0;
}

/* ------------------------------------------------------------------ *
 * The 43-operation setstate ledger (vendor 0x97ce5..0x97e2a).
 * ------------------------------------------------------------------ */

#define OB_ACPHY_CAL_W2_SS_SITE_SUSPEND		0x097d13u
#define OB_ACPHY_CAL_W2_SS_SITE_ENABLE		0x097e1du
#define OB_ACPHY_CAL_W2_SS_SITE_READ_0401	0x097d20u
#define OB_ACPHY_CAL_W2_SS_SITE_READ_0400	0x097d30u
#define OB_ACPHY_CAL_W2_SS_SITE_MOD_0160	0x097d50u
#define OB_ACPHY_CAL_W2_SS_SITE_MOD_0401_70	0x097d6eu
#define OB_ACPHY_CAL_W2_SS_SITE_MOD_0401_7000	0x097d85u
#define OB_ACPHY_CAL_W2_SS_SITE_MOD_0401_7	0x097d99u
#define OB_ACPHY_CAL_W2_SS_SITE_MOD_0400	0x097db0u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_READ_0400	0x097c1fu
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_READ_019E	0x097c2fu
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_019E_2 0x097c49u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_019E_1 0x097c60u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_0400	0x097c72u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_0402	0x097c83u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_POLL	0x097ca6u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_DELAY	0x097c99u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_RESTORE_0400 0x097cc3u
#define OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_RESTORE_019E 0x097cd4u
#define OB_ACPHY_CAL_W2_SS_SITE_RESTORE_LOW	0x097de4u
#define OB_ACPHY_CAL_W2_SS_SITE_RESTORE_HIGH	0x097dffu
#define OB_ACPHY_CAL_W2_SS_SITE_RESTORE_0400	0x097e10u

static int ob_acphy_cal_w2_ss_read(const struct ob_acphy_cal_w2_ops *ops,
				   void *ctx,
				   struct ob_acphy_cal_w2_setstate_result *res,
				   u32 site, u16 reg, u16 *value)
{
	int ret;

	res->last_site = site;
	ret = ops->phy_read16(ctx, (u8)(res->reads + res->writes + 1u),
			      site, reg, value);
	res->reads++;
	if (ret)
		return ret;
	if (ops->device_lost && ops->device_lost(ctx))
		return -5;
	return 0;
}

static int ob_acphy_cal_w2_ss_write(const struct ob_acphy_cal_w2_ops *ops,
				    void *ctx,
				    struct ob_acphy_cal_w2_setstate_result *res,
				    u32 site, u16 reg, u16 value)
{
	int ret;

	res->last_site = site;
	/* Count the attempt before the callback: a failing backend may have
	 * stored the word already. */
	res->writes++;
	ret = ops->phy_write16(ctx, (u8)(res->reads + res->writes + 1u),
			       site, reg, value);
	if (ret)
		return ret;
	if (ops->device_lost && ops->device_lost(ctx))
		return -5;
	return 0;
}

static int ob_acphy_cal_w2_ss_rmw(const struct ob_acphy_cal_w2_ops *ops,
				  void *ctx,
				  struct ob_acphy_cal_w2_setstate_result *res,
				  u32 site, u16 reg, u16 mask, u16 value)
{
	u16 old;
	int ret;

	ret = ob_acphy_cal_w2_ss_read(ops, ctx, res, site, reg, &old);
	if (ret)
		return ret;
	return ob_acphy_cal_w2_ss_write(ops, ctx, res, site, reg,
					(u16)((old & (u16)~mask) |
					      (value & mask)));
}

/* wlc_phy_force_rfseq_acphy @0x97b9c..0x97ce4: exits on CLEAR (0x97caf). */
static int ob_acphy_cal_w2_ss_rfseq(const struct ob_acphy_cal_w2_ops *ops,
				    void *ctx,
				    struct ob_acphy_cal_w2_setstate_result *res,
				    unsigned int index, u16 force)
{
	u32 counter = OB_ACPHY_CAL_POLL_COUNTER_INIT;
	u16 saved_0400;
	u16 saved_019e;
	int ret;

	ret = ob_acphy_cal_w2_ss_read(ops, ctx, res,
				      OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_READ_0400,
				      0x0400, &saved_0400);
	if (ret)
		return ret;
	ret = ob_acphy_cal_w2_ss_read(ops, ctx, res,
				      OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_READ_019E,
				      0x019e, &saved_019e);
	if (ret)
		return ret;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, res,
				     OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_019E_2,
				     0x019e, 0x0002, 0x0002);
	if (ret)
		return ret;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, res,
				     OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_019E_1,
				     0x019e, 0x0001, 0x0001);
	if (ret)
		return ret;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, res,
				     OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_0400,
				     0x0400, 0x0003, 0x0003);
	if (ret)
		return ret;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, res,
				     OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_MOD_0402,
				     0x0402, force, force);
	if (ret)
		return ret;
	for (;;) {
		u16 sample;

		ret = ob_acphy_cal_w2_ss_read(
			ops, ctx, res, OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_POLL,
			0x0403, &sample);
		if (ret)
			return ret;
		res->poll_reads[index]++;
		if (!(sample & force))
			break;
		if (counter == OB_ACPHY_CAL_POLL_COUNTER_FLOOR) {
			res->poll_exhausted[index] = true;
			break;
		}
		counter -= OB_ACPHY_CAL_POLL_COUNTER_STEP;
		res->last_site = OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_DELAY;
		ret = ops->delay_us(ctx,
				    (u8)(res->reads + res->writes + 1u),
				    OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_DELAY,
				    OB_ACPHY_CAL_POLL_DELAY_US);
		res->poll_delays[index]++;
		if (ret)
			return ret;
		if (ops->device_lost && ops->device_lost(ctx))
			return -5;
	}
	ret = ob_acphy_cal_w2_ss_write(
		ops, ctx, res, OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_RESTORE_0400,
		0x0400, saved_0400);
	if (ret)
		return ret;
	return ob_acphy_cal_w2_ss_write(
		ops, ctx, res, OB_ACPHY_CAL_W2_SS_SITE_RFSEQ_RESTORE_019E,
		0x019e, saved_019e);
}

int ob_acphy_cal_w2_setstate_run(const struct ob_acphy_cal_w2_ops *ops,
				 void *ctx, u8 state, bool phy_context_nonzero,
				 struct ob_acphy_cal_w2_setstate_result *result)
{
	struct ob_acphy_cal_w2_setstate_result res = { 0 };
	u16 saved_0401;
	u16 saved_0400;
	bool transitioned = false;
	int ret;

	if (!ops || !ops->phy_read16 || !ops->phy_write16 || !ops->delay_us ||
	    !result)
		return -22;
	res.called = true;
	/* @0x97cfa stores the argument to shared+0xa7 unconditionally, before
	 * the 0x97d05 gate; that store is software state this runner does not
	 * carry, so it is reported as the software arm only. */
	if (!phy_context_nonzero) {
		res.software_only = true;
		*result = res;
		return 0;
	}
	if (!ops->mac_suspend || !ops->mac_resume) {
		/* -EOPNOTSUPP (95), spelled locally so the host build does not
		 * depend on a kernel errno header. */
		*result = res;
		return -95;
	}
	res.last_site = OB_ACPHY_CAL_W2_SS_SITE_SUSPEND;
	ret = ops->mac_suspend(ctx, 1u, OB_ACPHY_CAL_W2_SS_SITE_SUSPEND,
			       &transitioned);
	if (ret) {
		*result = res;
		return ret;
	}
	ret = ob_acphy_cal_w2_ss_read(ops, ctx, &res,
				      OB_ACPHY_CAL_W2_SS_SITE_READ_0401,
				      0x0401, &saved_0401);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_read(ops, ctx, &res,
				      OB_ACPHY_CAL_W2_SS_SITE_READ_0400,
				      0x0400, &saved_0400);
	if (ret)
		goto out_mac;
	res.saved_0401 = saved_0401;
	res.saved_0400 = saved_0400;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, &res,
				     OB_ACPHY_CAL_W2_SS_SITE_MOD_0160,
				     0x0160, 0x0007, state);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, &res,
				     OB_ACPHY_CAL_W2_SS_SITE_MOD_0401_70,
				     0x0401, 0x0070,
				     (u16)(((u16)state << 4) & 0x0ff0));
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, &res,
				     OB_ACPHY_CAL_W2_SS_SITE_MOD_0401_7000,
				     0x0401, 0x7000, 0x7000);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, &res,
				     OB_ACPHY_CAL_W2_SS_SITE_MOD_0401_7,
				     0x0401, 0x0007, 0x0000);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, &res,
				     OB_ACPHY_CAL_W2_SS_SITE_MOD_0400,
				     0x0400, 0x0001, 0x0001);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rfseq(ops, ctx, &res, 0, 0x0002);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rfseq(ops, ctx, &res, 1, 0x0001);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, &res,
				     OB_ACPHY_CAL_W2_SS_SITE_RESTORE_LOW,
				     0x0401, 0x0007, OB_ACPHY_CAL_W2_PI_A6);
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_rmw(ops, ctx, &res,
				     OB_ACPHY_CAL_W2_SS_SITE_RESTORE_HIGH,
				     0x0401, 0x7000,
				     (u16)(saved_0401 & 0x7000));
	if (ret)
		goto out_mac;
	ret = ob_acphy_cal_w2_ss_write(ops, ctx, &res,
				       OB_ACPHY_CAL_W2_SS_SITE_RESTORE_0400,
				       0x0400, saved_0400);
	if (ret)
		goto out_mac;
	res.last_site = OB_ACPHY_CAL_W2_SS_SITE_ENABLE;
	ret = ops->mac_resume(ctx, 2u, OB_ACPHY_CAL_W2_SS_SITE_ENABLE);
	if (ret) {
		*result = res;
		return ret;
	}
	*result = res;
	return 0;

out_mac:
	/* The vendor's enable is part of the pair: close it even when a PHY
	 * operation failed, so the shim's depth stays balanced. */
	ops->mac_resume(ctx, (u8)(res.reads + res.writes + 2u),
			OB_ACPHY_CAL_W2_SS_SITE_ENABLE);
	*result = res;
	return ret;
}

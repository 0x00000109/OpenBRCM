// SPDX-License-Identifier: GPL-2.0-only

/*
 * First bounded BCM4352 initial-calibration transaction.
 *
 * Every operation below is taken from the pinned vendor object
 * (wlc_hybrid.o_shipped, SHA-256 352a6e34...) at the instruction anchors named
 * in ob_acphy_cal_run.h.  Nothing here is inferred from a function name, and
 * nothing past 0x0b14d7 is executed.
 *
 * Deliberate differences from the vendor, all fail-closed:
 *   - the vendor discards every helper return value; OpenBRCM stops at the
 *     first failure and issues no later operation;
 *   - the vendor falls through a timed-out readiness poll; OpenBRCM returns
 *     -ETIMEDOUT and blocks every successor;
 *   - the vendor has no device-loss predicate.
 * The finite 2000-delay poll bound itself is the vendor's own counter, not an
 * OpenBRCM invention.
 */

#ifdef __KERNEL__
#include <linux/kernel.h>
#endif
#include "ob_acphy_cal_run.h"

#define OB_CAL_EINVAL		(-22)
#define OB_CAL_EOPNOTSUPP	(-95)
#define OB_CAL_EIO		(-5)
#define OB_CAL_ETIMEDOUT	(-110)

/* Vendor sub_925cc call sites, used as the source site of each callback. */
#define OB_ACPHY_CAL_SITE_SHM		0x0925e9u
#define OB_ACPHY_CAL_SITE_SUSPEND	0x0925f6u
#define OB_ACPHY_CAL_SITE_PHYREG_ENTER	0x0925feu
#define OB_ACPHY_CAL_SITE_TXPWRCTRL	0x092608u

static bool ob_acphy_cal_stop(const struct ob_acphy_cal_ops *ops, void *ctx)
{
	return ops->device_lost && ops->device_lost(ctx);
}

/*
 * The vendor entry gate returns to the common epilogue when PI+0x19c carries
 * bit 0x10, without touching the RX-core state or the common bytes.
 */
static bool ob_acphy_cal_entry_refused(const struct ob_acphy_cal_input *in)
{
	return (in->pi_19c & OB_ACPHY_CAL_ENTRY_SKIP_BIT) != 0;
}

/*
 * Target admission reuses the existing LPF identity policy so the accepted
 * tuple (0x4352/rev3/0x812/rev42/0x0b/2069-rev4 class 0) has exactly one
 * owner.  The calibration block additionally requires the rev1 branch: the
 * rev0x0a branch selects a different vendor arm.
 */
static int ob_acphy_cal_target_status(const struct ob_phy_lpf_target *target,
				      enum ob_phy_lpf_input_status *status)
{
	if (!target)
		return OB_CAL_EINVAL;
	if (!target->identity_valid) {
		*status = OB_PHY_LPF_INPUT_TARGET;
		return OB_CAL_EOPNOTSUPP;
	}
	if (target->phy_revision != 1) {
		*status = OB_PHY_LPF_INPUT_PI164_BRANCH;
		return OB_CAL_EOPNOTSUPP;
	}
	/* boardflags3 is not a calibration input; only identity is consulted. */
	*status = ob_phy_acphy_lpf_input_status(target, true, 0);
	return *status == OB_PHY_LPF_INPUTS_SUPPORTED ? 0
						      : OB_CAL_EOPNOTSUPP;
}

/*
 * wlc_phy_cals_acphy entry: the vendor rearranges the common-state bytes
 * before calling the RX-core setter, which later restores PHY[0x0401] bits
 * 0..2 from shared+0xa6.  a4 and a5 are read but not overwritten.
 */
static void ob_acphy_cal_common_prepare(const struct ob_acphy_cal_input *in,
					struct ob_acphy_cal_result *result)
{
	result->common_a7_after = in->common_a5;
	result->common_a6_after = in->common_a4;
}

/*
 * sub_925cc(pi, 0x7148) @0x0925cc..0x09260f.  The vendor discards every
 * return value; OpenBRCM stops at the first failure instead of continuing
 * with a half-issued sequence.  That difference is documented, not hidden.
 */
static int ob_acphy_cal_sub925cc(const struct ob_acphy_cal_ops *ops, void *ctx,
				 struct ob_acphy_cal_result *result)
{
	u8 event;
	int ret;

	event = (u8)(result->logical_event_count + 1u);
	result->last_site = OB_ACPHY_CAL_SITE_SHM;
	ret = ops->bmac_write_shm(ctx, event, OB_ACPHY_CAL_SITE_SHM,
				  OB_ACPHY_CAL_SUB925CC_SHM_OFFSET,
				  OB_ACPHY_CAL_SUB925CC_SHM_VALUE);
	result->callback_count++;
	result->logical_event_count++;
	if (ret)
		return ret;
	if (ob_acphy_cal_stop(ops, ctx))
		return OB_CAL_EIO;

	event = (u8)(result->logical_event_count + 1u);
	result->last_site = OB_ACPHY_CAL_SITE_SUSPEND;
	ret = ops->suspend_mac_and_wait(ctx, event,
					OB_ACPHY_CAL_SITE_SUSPEND);
	result->callback_count++;
	result->logical_event_count++;
	if (ret)
		return ret;
	if (ob_acphy_cal_stop(ops, ctx))
		return OB_CAL_EIO;

	event = (u8)(result->logical_event_count + 1u);
	result->last_site = OB_ACPHY_CAL_SITE_PHYREG_ENTER;
	ret = ops->phyreg_enter(ctx, event, OB_ACPHY_CAL_SITE_PHYREG_ENTER);
	result->callback_count++;
	result->logical_event_count++;
	if (ret)
		return ret;
	if (ob_acphy_cal_stop(ops, ctx))
		return OB_CAL_EIO;

	event = (u8)(result->logical_event_count + 1u);
	result->last_site = OB_ACPHY_CAL_SITE_TXPWRCTRL;
	/* wlc_phy_txpwrctrl_enable_acphy(pi, 0): disable before calibration. */
	ret = ops->txpwrctrl_enable(ctx, event, OB_ACPHY_CAL_SITE_TXPWRCTRL,
				    0);
	result->callback_count++;
	result->logical_event_count++;
	if (ret)
		return ret;
	if (ob_acphy_cal_stop(ops, ctx))
		return OB_CAL_EIO;
	return 0;
}

int ob_acphy_cal_run(const struct ob_acphy_cal_ops *ops, void *ctx,
		     const struct ob_phy_lpf_target *target,
		     const struct ob_acphy_cal_input *in,
		     struct ob_acphy_cal_result *result)
{
	int ret;

	if (!ops || !target || !in || !result)
		return OB_CAL_EINVAL;
	*result = (struct ob_acphy_cal_result) {
		.status = OB_ACPHY_CAL_INVALID,
		.target_status = OB_PHY_LPF_INPUT_TARGET,
	};

	ret = ob_acphy_cal_target_status(target, &result->target_status);
	if (ret) {
		/* Every refusal must block a successor, including this one. */
		result->tail_blocked = true;
		return ret;
	}

	/* The RX-core transition depends on a completed radio predecessor. */
	if (!in->predecessor_ready) {
		result->tail_blocked = true;
		return OB_CAL_EINVAL;
	}
	if (!in->chanspec_valid || in->chanspec != 0x1001) {
		result->tail_blocked = true;
		return OB_CAL_EINVAL;
	}
	/* A latched earlier failure must not be retried implicitly. */
	if (in->prior_fatal) {
		result->status = OB_ACPHY_CAL_FAILED_DEVICE_LOST;
		result->tail_blocked = true;
		return OB_CAL_EIO;
	}

	if (ob_acphy_cal_entry_refused(in)) {
		/* Vendor: PI+0x19c bit 0x10 sends the call straight to the
		 * epilogue, with no RX-core, common-state or MMIO work. */
		result->status = OB_ACPHY_CAL_STOPPED_ENTRY_FLAG;
		result->tail_blocked = true;
		return OB_CAL_EOPNOTSUPP;
	}

	ob_acphy_cal_common_prepare(in, result);

	/*
	 * The RX-core state transition is owned by src/ob_phy_a7089_txpwr.c.
	 * Without that owner the vendor sequence cannot be reproduced, and no
	 * operation is issued rather than skipping the step silently.
	 */
	if (!ops->rxcore_setstate) {
		result->status = OB_ACPHY_CAL_STOPPED_NO_RXCORE_OWNER;
		result->tail_blocked = true;
		return OB_CAL_EOPNOTSUPP;
	}
	if (!ops->bmac_write_shm || !ops->suspend_mac_and_wait ||
	    !ops->phyreg_enter || !ops->txpwrctrl_enable) {
		result->status = OB_ACPHY_CAL_INVALID;
		result->tail_blocked = true;
		return OB_CAL_EINVAL;
	}

	{
		u8 event = (u8)(result->logical_event_count + 1u);
		/* The vendor passes the unchanged common byte a5. */
		u8 state = in->common_a5;

		result->last_site = OB_ACPHY_CAL_VENDOR_RXCORE_CALL;
		ret = ops->rxcore_setstate(ctx, event,
					   OB_ACPHY_CAL_VENDOR_RXCORE_CALL,
					   state);
		result->callback_count++;
		result->logical_event_count++;
		result->rxcore_setstate_called = true;
		result->rxcore_state = state;
		if (ret) {
			result->status = OB_ACPHY_CAL_FAILED_CALLBACK;
			result->tail_blocked = true;
			return ret;
		}
		if (ob_acphy_cal_stop(ops, ctx)) {
			result->status = OB_ACPHY_CAL_FAILED_DEVICE_LOST;
			result->tail_blocked = true;
			return OB_CAL_EIO;
		}
	}

	ret = ob_acphy_cal_sub925cc(ops, ctx, result);
	if (ret) {
		result->status = ret == OB_CAL_EIO
					 ? OB_ACPHY_CAL_FAILED_DEVICE_LOST
					 : OB_ACPHY_CAL_FAILED_CALLBACK;
		result->tail_blocked = true;
		return ret;
	}

	/*
	 * Cal-state snapshots: the vendor stores *(shared+0x34) into cal+0xc0
	 * and the live chanspec word into cal+0xba.  Both are software copies of
	 * caller state, so they are published in the result instead of being
	 * issued as MMIO.
	 */
	result->snapshot_cal_c0 = in->shared_34;
	result->snapshot_cal_ba = in->pi_17e;
	result->last_site = OB_ACPHY_CAL_VENDOR_SNAPSHOT_BA;

	/*
	 * PI+0xf84 is set to 1 by the selected reason-6 arm before the cal body
	 * runs, so the real initial path takes the transmit-tone call, which is
	 * not recovered.  Stop here rather than inventing it.
	 */
	result->tail_blocked = true;
	if (in->pi_f84) {
		result->status = OB_ACPHY_CAL_STOPPED_TX_TONE;
		return OB_CAL_EOPNOTSUPP;
	}
	result->status = OB_ACPHY_CAL_STOPPED_B1227_TAIL;
	return OB_CAL_EOPNOTSUPP;
}

int ob_acphy_cal_poll380_run(const struct ob_acphy_cal_ops *ops, void *ctx,
			     struct ob_acphy_cal_poll_result *result)
{
	u32 counter = OB_ACPHY_CAL_POLL_COUNTER_INIT;
	u16 value = 0;
	int ret;

	if (!ops || !result || !ops->phy_read16 || !ops->delay_us)
		return OB_CAL_EINVAL;
	*result = (struct ob_acphy_cal_poll_result) {
		.status = OB_ACPHY_CAL_POLL_INVALID,
		.counter_end = counter,
		.successor_blocked = true,
	};

	for (;;) {
		u8 event = (u8)(result->reads + result->delays + 1u);

		ret = ops->phy_read16(ctx, event,
				      OB_ACPHY_CAL_VENDOR_POLL_READ,
				      OB_ACPHY_CAL_POLL_REG, &value);
		result->reads++;
		if (ret) {
			result->status = OB_ACPHY_CAL_POLL_FAILED_CALLBACK;
			result->counter_end = counter;
			return ret;
		}
		result->last_value = value;
		/* Success: both status bits clear. */
		if ((value & OB_ACPHY_CAL_POLL_MASK) == 0) {
			result->status = OB_ACPHY_CAL_POLL_CLEARED;
			result->successor_blocked = false;
			result->counter_end = counter;
			return 0;
		}
		/* Vendor counter reaches 9 without clearing: fall through. */
		if (counter == OB_ACPHY_CAL_POLL_COUNTER_FLOOR) {
			result->status = OB_ACPHY_CAL_POLL_EXHAUSTED;
			result->counter_end = counter;
			return OB_CAL_ETIMEDOUT;
		}
		counter -= OB_ACPHY_CAL_POLL_COUNTER_STEP;
		ret = ops->delay_us(ctx,
				    (u8)(result->reads + result->delays + 1u),
				    OB_ACPHY_CAL_VENDOR_POLL_DELAY,
				    OB_ACPHY_CAL_POLL_DELAY_US);
		result->delays++;
		if (ret) {
			result->status = OB_ACPHY_CAL_POLL_FAILED_CALLBACK;
			result->counter_end = counter;
			return ret;
		}
		if (ob_acphy_cal_stop(ops, ctx)) {
			result->status = OB_ACPHY_CAL_POLL_DEVICE_LOST;
			result->counter_end = counter;
			return OB_CAL_EIO;
		}
	}
}

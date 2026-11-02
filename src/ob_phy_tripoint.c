// SPDX-License-Identifier: GPL-2.0-only
/*
 * Three-point read-only witness for the seven 0x07xx PHY registers and the
 * 0x0408 identity word. Recovered from the vendor mmiotrace: boot
 * 64536612..., t = 531.521658..531.521705 (the prologue read series, PHY
 * version 0xcb01 already read at 531.521625) and both 0x17xx blocks at
 * t = 531.533405 / 531.535567 reuse those seven values.
 *
 * Four points since 2026-10-28: PM1 (first legal read of the attach prefix,
 * after the 0x408 == 0x5 gate), then P0/P1/P2 as before. There is no write
 * callback in the interface, so "read-only" is enforced by the type rather
 * than by an assertion.
 */
#include <linux/errno.h>

#include "ob_phy_tripoint.h"

const u16 ob_phy_tripoint_regs[OB_PHY_TRIPOINT_REGS] = {
	0x0739, 0x073a, 0x0725, 0x0729, 0x0721, 0x0728, 0x0720, 0x0408,
};

const char *ob_phy_tripoint_name(enum ob_phy_tripoint_point point)
{
	switch (point) {
	case OB_PHY_TRIPOINT_PM1:
		return "PM1";
	case OB_PHY_TRIPOINT_P0:
		return "P0";
	case OB_PHY_TRIPOINT_P1:
		return "P1";
	case OB_PHY_TRIPOINT_P2:
		return "P2";
	default:
		return "INVALID";
	}
}

int ob_phy_tripoint_run(const struct ob_phy_tripoint_ops *ops, void *ctx,
			struct ob_phy_tripoint *state,
			enum ob_phy_tripoint_point point)
{
	unsigned int i;

	if (!ops || !ops->read16 || !ops->device_lost || !state)
		return -EINVAL;
	/*
	 * One sample per point, in order. Re-entry and out-of-order calls are
	 * configuration errors: refusing them here keeps a later point from
	 * being recorded against a state its predecessor never observed.
	 */
	if ((unsigned int)point >= OB_PHY_TRIPOINT_POINTS ||
	    state->observed != (unsigned int)point)
		return -EINVAL;
	if (ops->device_lost(ctx)) {
		state->last_error = -EIO;
		return -EIO;
	}

	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++) {
		u16 value;
		int ret;

		state->last_reg = ob_phy_tripoint_regs[i];
		ret = ops->read16(ctx, state->last_reg, &value);
		if (ret) {
			state->last_error = ret;
			return ret;
		}
		if (ops->device_lost(ctx)) {
			state->last_error = -EIO;
			return -EIO;
		}
		state->value[point][i] = value;
	}

	state->observed = (unsigned int)point + 1u;
	state->last_error = 0;
	return 0;
}

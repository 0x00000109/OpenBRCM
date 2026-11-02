/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_SUB92500_H_
#define _OB_PHY_SUB92500_H_

#include <linux/types.h>

struct ob_phy_sub92500_ops {
	int (*read_shm16)(void *ctx, u16 offset, u16 *value);
	int (*write_shm16)(void *ctx, u16 offset, u16 value);
};

/* `wlc_phy_attach_acphy` stores (u32)PI+0x164 <= 1 into nested+0x912. */
bool ob_phy_nested_912_for_revision(u8 pi_164);

/* Implements the proven nested+0x912 arm of sub_92500. */
int ob_phy_sub92500_run(const struct ob_phy_sub92500_ops *ops, void *ctx,
			bool nested_912, bool *activated);

#endif

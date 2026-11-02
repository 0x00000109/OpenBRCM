/* SPDX-License-Identifier: GPL-2.0-only */
#include "ob_phy_sub92500.h"

#define OB_PHY_SUB92500_SHM_OFFSET 0x0184u
#define OB_PHY_SUB92500_SENTINEL   0xdeadu

bool ob_phy_nested_912_for_revision(u8 pi_164)
{
	return pi_164 <= 1u;
}

int ob_phy_sub92500_run(const struct ob_phy_sub92500_ops *ops, void *ctx,
			bool nested_912, bool *activated)
{
	u16 value;
	int ret;

	if (!ops || !ops->read_shm16 || !ops->write_shm16 || !activated)
		return -22;
	*activated = false;
	if (!nested_912)
		return 0;

	ret = ops->read_shm16(ctx, OB_PHY_SUB92500_SHM_OFFSET, &value);
	if (ret)
		return ret;
	if (value != OB_PHY_SUB92500_SENTINEL)
		return 0;

	ret = ops->write_shm16(ctx, OB_PHY_SUB92500_SHM_OFFSET, 0);
	if (ret)
		return ret;
	*activated = true;
	return 0;
}

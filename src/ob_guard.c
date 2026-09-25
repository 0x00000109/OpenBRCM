// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — central device-loss access guard (kernel backend).
 *
 * All D11 / ChipCommon / AXI(backplane) register access in the driver goes
 * through these wrappers. When `hw->dev_lost` is latched:
 *   - reads do not touch MMIO and return the dead sentinel;
 *   - writes are suppressed;
 * so no D11/PHY/radio/DMA/core write can be issued after device loss.
 *
 * With `dev_lost == false` the wrappers are a direct pass-through to bcma; the
 * previously hardware-proven normal paths are byte-for-byte unchanged.
 *
 * The pure decision code lives in ob_guard.h and is exercised by
 * tests/host/ob_guard_test.c with a fake backend (no hardware).
 */
#include <linux/types.h>
#include <linux/bcma/bcma.h>
#include "ob_core.h"
#include "ob_guard.h"
#include "ob_d3a0.h"

/* ---- real bcma backends ------------------------------------------------- */

static u32 ob_bcma_read32(void *ctx, u16 off)
{
	return bcma_read32((struct bcma_device *)ctx, off);
}

static u16 ob_bcma_read16(void *ctx, u16 off)
{
	return bcma_read16((struct bcma_device *)ctx, off);
}

static void ob_bcma_write32(void *ctx, u16 off, u32 val)
{
	bcma_write32((struct bcma_device *)ctx, off, val);
}

static void ob_bcma_write16(void *ctx, u16 off, u16 val)
{
	bcma_write16((struct bcma_device *)ctx, off, val);
}

static u32 ob_bcma_aread32(void *ctx, u16 off)
{
	return bcma_aread32((struct bcma_device *)ctx, off);
}

static void ob_bcma_awrite32(void *ctx, u16 off, u32 val)
{
	bcma_awrite32((struct bcma_device *)ctx, off, val);
}

static const struct ob_guard_ops ob_guard_ops_bcma = {
	.read32 = ob_bcma_read32,
	.read16 = ob_bcma_read16,
	.write32 = ob_bcma_write32,
	.write16 = ob_bcma_write16,
};

static const struct ob_guard_ops ob_guard_ops_axi = {
	.read32 = ob_bcma_aread32,
	.read16 = ob_bcma_read16,
	.write32 = ob_bcma_awrite32,
	.write16 = ob_bcma_write16,
};

/* ---- D11 window --------------------------------------------------------- */

u32 ob_d11_read32(struct ob_hw *hw, u16 off)
{
	if (!hw)
		return OB_GUARD_DEAD32;
	return ob_guard_do_read32(hw->dev_lost, &hw->guard_stat,
				  &ob_guard_ops_bcma, hw->core, off);
}

u16 ob_d11_read16(struct ob_hw *hw, u16 off)
{
	if (!hw)
		return OB_GUARD_DEAD16;
	return ob_guard_do_read16(hw->dev_lost, &hw->guard_stat,
				  &ob_guard_ops_bcma, hw->core, off);
}

void ob_d11_write32(struct ob_hw *hw, u16 off, u32 val)
{
	if (!hw)
		return;
	ob_guard_do_write32(hw->dev_lost, &hw->guard_stat,
			    &ob_guard_ops_bcma, hw->core, off, val);
}

void ob_d11_write16(struct ob_hw *hw, u16 off, u16 val)
{
	if (!hw)
		return;
	ob_guard_do_write16(hw->dev_lost, &hw->guard_stat,
			    &ob_guard_ops_bcma, hw->core, off, val);
}

/*
 * Trusted direct 32-bit D11 read: an all-ones result latches device loss
 * (monotonic). Call this ONLY for registers whose all-ones value unambiguously
 * means an inaccessible window (MACCONTROL/MACINTMASK/OBJADDR readback/DMA
 * control+status); never for SHM/OBJDATA payloads.
 */
u32 ob_d11_read32_trusted(struct ob_hw *hw, const char *where, u16 off)
{
	u32 v = ob_d11_read32(hw, off);

	if (hw && !hw->dev_lost &&
	    ob_guard_next_latch(hw->dev_lost, true, v))
		ob_dev_lost_latch(hw, where);
	return v;
}

/* ---- AXI / backplane window (BCMA IOCTL etc.) --------------------------- */

u32 ob_axi_read32(struct ob_hw *hw, u16 off)
{
	if (!hw)
		return OB_GUARD_DEAD32;
	return ob_guard_do_read32(hw->dev_lost, &hw->guard_stat,
				  &ob_guard_ops_axi, hw->core, off);
}

void ob_axi_write32(struct ob_hw *hw, u16 off, u32 val)
{
	if (!hw)
		return;
	ob_guard_do_write32(hw->dev_lost, &hw->guard_stat,
			    &ob_guard_ops_axi, hw->core, off, val);
}

/* ---- ChipCommon window (PMU/SPROM/OTP/clocks) --------------------------- */

u32 ob_cc_read32(struct ob_hw *hw, u16 off)
{
	if (!hw)
		return OB_GUARD_DEAD32;
	return ob_guard_do_read32(hw->dev_lost, &hw->guard_stat,
				  &ob_guard_ops_bcma, hw->cc, off);
}

u16 ob_cc_read16(struct ob_hw *hw, u16 off)
{
	if (!hw)
		return OB_GUARD_DEAD16;
	return ob_guard_do_read16(hw->dev_lost, &hw->guard_stat,
				  &ob_guard_ops_bcma, hw->cc, off);
}

void ob_cc_write32(struct ob_hw *hw, u16 off, u32 val)
{
	if (!hw)
		return;
	ob_guard_do_write32(hw->dev_lost, &hw->guard_stat,
			    &ob_guard_ops_bcma, hw->cc, off, val);
}

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
#include <linux/printk.h>
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
	u32 val;

	if (!hw)
		return OB_GUARD_DEAD32;
	val = ob_guard_do_read32(hw->dev_lost, &hw->guard_stat,
				 &ob_guard_ops_bcma, hw->core, off);
	if (val == OB_GUARD_DEAD32)
		pr_debug("openbrcm: mmio d11 r32 off=0x%04x ALL_ONES lost=%u\n",
			 off, hw->dev_lost ? 1u : 0u);
	return val;
}

u16 ob_d11_read16(struct ob_hw *hw, u16 off)
{
	u16 val;

	if (!hw)
		return OB_GUARD_DEAD16;
	val = ob_guard_do_read16(hw->dev_lost, &hw->guard_stat,
				 &ob_guard_ops_bcma, hw->core, off);
	if (val == OB_GUARD_DEAD16)
		pr_debug("openbrcm: mmio d11 r16 off=0x%04x ALL_ONES lost=%u\n",
			 off, hw->dev_lost ? 1u : 0u);
	return val;
}

void ob_d11_write32(struct ob_hw *hw, u16 off, u32 val)
{
	if (!hw)
		return;
	if (hw->dev_lost)
		pr_debug("openbrcm: mmio d11 w32 blocked off=0x%04x\n", off);
	ob_guard_do_write32(hw->dev_lost, &hw->guard_stat,
			    &ob_guard_ops_bcma, hw->core, off, val);
}

void ob_d11_write16(struct ob_hw *hw, u16 off, u16 val)
{
	if (!hw)
		return;
	if (hw->dev_lost)
		pr_debug("openbrcm: mmio d11 w16 blocked off=0x%04x\n", off);
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
	u32 val;

	if (!hw)
		return OB_GUARD_DEAD32;
	val = ob_guard_do_read32(hw->dev_lost, &hw->guard_stat,
				 &ob_guard_ops_axi, hw->core, off);
	if (val == OB_GUARD_DEAD32)
		pr_debug("openbrcm: mmio axi r32 off=0x%04x ALL_ONES lost=%u\n",
			 off, hw->dev_lost ? 1u : 0u);
	return val;
}

void ob_axi_write32(struct ob_hw *hw, u16 off, u32 val)
{
	if (!hw)
		return;
	if (hw->dev_lost)
		pr_debug("openbrcm: mmio axi w32 blocked off=0x%04x\n", off);
	ob_guard_do_write32(hw->dev_lost, &hw->guard_stat,
			    &ob_guard_ops_axi, hw->core, off, val);
}

/*
 * L2k scope-witness reads (acphy_identity_wrapper_witness): raw bcma reads
 * that DELIBERATELY bypass the device-loss latch. The guard exists so that no
 * production path touches MMIO after device loss; this instrument needs the
 * opposite: once the island has latched dev_lost, only a real bus access can
 * say whether the function still completes MMIO. A read through the guard
 * would return 0xffffffff without issuing MMIO and would measure the driver,
 * not the silicon. Call these ONLY from the L2k witness sequence.
 */
u32 ob_d11_witness_read32(struct ob_hw *hw, u16 off)
{
	if (!hw || !hw->core)
		return OB_GUARD_DEAD32;
	return bcma_read32(hw->core, off);
}

u32 ob_axi_witness_read32(struct ob_hw *hw, u16 off)
{
	if (!hw || !hw->core)
		return OB_GUARD_DEAD32;
	return bcma_aread32(hw->core, off);
}

static int ob_axi_checked_read(void *ctx, u16 off, u32 *value)
{
	struct ob_hw *hw = ctx;

	if (!value)
		return -22;
	*value = ob_axi_read32(hw, off);
	return hw && hw->dev_lost ? -19 : 0;
}

static int ob_axi_checked_write(void *ctx, u16 off, u32 value)
{
	struct ob_hw *hw = ctx;

	ob_axi_write32(hw, off, value);
	return hw && hw->dev_lost ? -19 : 0;
}

static bool ob_axi_checked_lost(void *ctx)
{
	const struct ob_hw *hw = ctx;

	return !hw || hw->dev_lost;
}

int ob_axi_rmw32_checked(struct ob_hw *hw, u16 off, u32 mask, u32 value,
			 u32 *new_value)
{
	static const struct ob_checked_rmw32_io io = {
		.read32 = ob_axi_checked_read,
		.write32 = ob_axi_checked_write,
		.device_lost = ob_axi_checked_lost,
	};

	if (!hw || !hw->core || !hw->core->wrap ||
	    hw->d11_state < OB_D11_POST_UCODE_READY)
		return -11;
	return ob_checked_rmw32(&io, hw, off, mask, value, new_value);
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

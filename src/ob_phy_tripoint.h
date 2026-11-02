/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_TRIPOINT_H_
#define _OB_PHY_TRIPOINT_H_

#include <linux/types.h>

/*
 * Read-only point witness for the seven 0x07xx registers the vendor samples in
 * its PHY-attach prologue and reuses in both core0 -> core1 mirror blocks, plus
 * the 0x0408 wrapper-domain identity word.
 *
 * The historical name keeps "tripoint"; there are FOUR points since
 * 2026-10-28, and the order is the contract:
 *
 *   PM1 ob_radio_vendor_pre_id(), immediately after the 0x408 == 0x5 gate.
 *       This is the first moment the PHY window is legal to read in this boot:
 *       the attach prefix itself issues no PHY write, and the pre-gate wrapper
 *       state is the one documented to stall the 0x3d8..0x3fb island, so no
 *       read is attempted before the gate. PM1 therefore asks whether the
 *       tuple already exists before ANY PHY access of the attach path.
 *   P0  ob_acphy_attach_identity(), immediately before the 0x03e0 read, i.e.
 *       after the optional SHM round-trip.
 *   P1  after ob_d11_post_ucode_init() returns (POST_UCODE_READY).
 *   P2  ob_acphy_analog_tx_lpf_initialize(), immediately before
 *       ob_phy_acphy_lpf_stage_run() issues the LPF register block. The
 *       core-mirror block is NOT here: since the S1 relocation (2026-10-28)
 *       it runs in ob_radio_vendor_pre_id(), between PM1 and the identity
 *       read, where the vendor places it.
 *
 * Each point at most once, in that order. A point whose predecessor did not
 * complete is refused without issuing a callback.
 *
 * No sampled value is a liveness verdict. The vendor's own pre-mirror read of
 * 0x0721 returns 0xffff, and 0x0000 is a legitimate value for five of the
 * eight registers, so only a callback error or a latched device loss can fail
 * a point. This module never writes.
 */
#define OB_PHY_TRIPOINT_REGS	8
#define OB_PHY_TRIPOINT_POINTS	4

enum ob_phy_tripoint_point {
	OB_PHY_TRIPOINT_PM1 = 0,
	OB_PHY_TRIPOINT_P0,
	OB_PHY_TRIPOINT_P1,
	OB_PHY_TRIPOINT_P2,
};

struct ob_phy_tripoint_ops {
	int (*read16)(void *ctx, u16 reg, u16 *value);
	bool (*device_lost)(void *ctx);
};

struct ob_phy_tripoint {
	/* Completed points; equals the point index the next call must carry. */
	unsigned int observed;
	u16 value[OB_PHY_TRIPOINT_POINTS][OB_PHY_TRIPOINT_REGS];
	u16 last_reg;
	int last_error;
};

/* The vendor order from the mmiotrace; 0x0408 closes the series. */
extern const u16 ob_phy_tripoint_regs[OB_PHY_TRIPOINT_REGS];

const char *ob_phy_tripoint_name(enum ob_phy_tripoint_point point);

/*
 * PM1 and P0 both precede the identity publication (and therefore the
 * ob_phy_read16() admission predicate), so both must use the pre-ID read of the
 * same 0x3fc selector + 0x3fe data pair. Getting this wrong fails the point
 * with -EINVAL before a single MMIO is issued, which is what the first
 * phy_prefix_point01 attempt measured at PM1 (TRIPOINT_FAIL point=PM1
 * reg=0x0739 ret=-22). Pinned by TR11.
 */
static inline bool ob_phy_tripoint_uses_pre_id(
	enum ob_phy_tripoint_point point)
{
	return point == OB_PHY_TRIPOINT_PM1 || point == OB_PHY_TRIPOINT_P0;
}

int ob_phy_tripoint_run(const struct ob_phy_tripoint_ops *ops, void *ctx,
			struct ob_phy_tripoint *state,
			enum ob_phy_tripoint_point point);

#endif /* _OB_PHY_TRIPOINT_H_ */

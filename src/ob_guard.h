/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — central device-loss access guard.
 *
 * Enforces ONE invariant for the whole driver (and therefore for the exact
 * BCM4352 rev42 D3B -> wlc_phy_init -> radio/PHY/calibration path):
 *
 *     once the device is lost (monotonic latch),
 *     NO new D11 / PHY-indirect / radio / SHM-OBJ / DMA / core hardware
 *     access may be issued — reads return a dead sentinel without touching
 *     MMIO, writes are suppressed.
 *
 * This header contains the pure, host-testable decision layer plus the backend
 * abstraction. `src/ob_guard.c` supplies the real bcma backend; host tests
 * supply a fake backend and exercise the exact same code the kernel uses.
 *
 * Detection (Phase 4) stays limited to TRUSTED DIRECT 32-bit D11 reads that
 * read all-ones. Arbitrary SHM/OBJDATA payloads are never classified as device
 * loss (they may legitimately be 0xffffffff). The vendor predicate
 * `(MACCONTROL & 0x404) == 0x400` (wlc_hw_deviceremoved) remains available.
 *
 * No hardware is touched by the pure layer. No new magic predicates.
 */
#ifndef _OB_GUARD_H_
#define _OB_GUARD_H_

#include <linux/types.h>

/* Dead-device sentinels returned when an access is blocked after latch. */
#define OB_GUARD_DEAD32	0xffffffffu
#define OB_GUARD_DEAD16	0xffffu
#define OB_GUARD_DEAD8	0xffu

/* All-ones value that unambiguously means an inaccessible D11/BCMA window. */
#define OB_GUARD_ALL_ONES32	0xffffffffu

/*
 * Backend for one register window (D11, ChipCommon, AXI/backplane, ...).
 * `ctx` is the window base (a bcma core) in the kernel, or a fake in tests.
 */
struct ob_guard_ops {
	u32  (*read32)(void *ctx, u16 off);
	u16  (*read16)(void *ctx, u16 off);
	void (*write32)(void *ctx, u16 off, u32 val);
	void (*write16)(void *ctx, u16 off, u16 val);
};

/*
 * Per-device access accounting. Diagnostics only; the latch itself lives in
 * `struct ob_hw::dev_lost` (single source of truth, monotonic).
 */
struct ob_guard_stat {
	u32 reads_issued;
	u32 writes_issued;
	u32 reads_blocked;
	u32 writes_blocked;
};

/*
 * ---- pure decision layer (host-testable, no kernel API) ----
 *
 * `lost` is the monotonic latch. `st` may be NULL. The functions issue the
 * backend call only when the access is permitted; after latch they return the
 * dead sentinel (read) or do nothing (write) and bump the blocked counter.
 */

static inline bool ob_guard_read_blocked(bool lost)
{
	return lost;
}

static inline bool ob_guard_write_blocked(bool lost)
{
	return lost;
}

static inline u32 ob_guard_do_read32(bool lost, struct ob_guard_stat *st,
				     const struct ob_guard_ops *ops, void *ctx,
				     u16 off)
{
	if (lost) {
		if (st)
			st->reads_blocked++;
		return OB_GUARD_DEAD32;
	}
	if (st)
		st->reads_issued++;
	return ops->read32(ctx, off);
}

static inline u16 ob_guard_do_read16(bool lost, struct ob_guard_stat *st,
				     const struct ob_guard_ops *ops, void *ctx,
				     u16 off)
{
	if (lost) {
		if (st)
			st->reads_blocked++;
		return OB_GUARD_DEAD16;
	}
	if (st)
		st->reads_issued++;
	return ops->read16(ctx, off);
}

static inline void ob_guard_do_write32(bool lost, struct ob_guard_stat *st,
				       const struct ob_guard_ops *ops, void *ctx,
				       u16 off, u32 val)
{
	if (lost) {
		if (st)
			st->writes_blocked++;
		return;
	}
	if (st)
		st->writes_issued++;
	ops->write32(ctx, off, val);
}

static inline void ob_guard_do_write16(bool lost, struct ob_guard_stat *st,
				       const struct ob_guard_ops *ops, void *ctx,
				       u16 off, u16 val)
{
	if (lost) {
		if (st)
			st->writes_blocked++;
		return;
	}
	if (st)
		st->writes_issued++;
	ops->write16(ctx, off, val);
}

/*
 * Trusted all-ones observation. Returns the NEXT latch value (monotonic):
 * true stays true; a live latch flips to true only when the access is trusted
 * AND the value is all-ones. A non-trusted all-ones payload NEVER latches.
 */
static inline bool ob_guard_next_latch(bool lost, bool trusted, u32 val)
{
	if (lost)
		return true;
	return trusted && val == OB_GUARD_ALL_ONES32;
}

/* ---- kernel wrappers (implemented in ob_guard.c) ------------------------ */
/*
 * Every D11 / AXI / ChipCommon access in the driver MUST use one of these.
 * With dev_lost set they never touch MMIO: reads return the dead sentinel,
 * writes are dropped.
 */
struct ob_hw;

u32  ob_d11_read32(struct ob_hw *hw, u16 off);
u16  ob_d11_read16(struct ob_hw *hw, u16 off);
void ob_d11_write32(struct ob_hw *hw, u16 off, u32 val);
void ob_d11_write16(struct ob_hw *hw, u16 off, u16 val);
u32  ob_d11_read32_trusted(struct ob_hw *hw, const char *where, u16 off);
u32  ob_axi_read32(struct ob_hw *hw, u16 off);
void ob_axi_write32(struct ob_hw *hw, u16 off, u32 val);
u32  ob_cc_read32(struct ob_hw *hw, u16 off);
u16  ob_cc_read16(struct ob_hw *hw, u16 off);
void ob_cc_write32(struct ob_hw *hw, u16 off, u32 val);

#endif /* _OB_GUARD_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_ACPHY_CAL_WRITE_STAGE_H_
#define _OB_ACPHY_CAL_WRITE_STAGE_H_

#include "ob_acphy_cal_stage.h"

/*
 * M1-6 — vendor 0x0070 contract: save -> disable -> restore, inside the
 * MAC-suspend / phyreg bracket.
 *
 * M1-4 executed the disable arm of wlc_phy_txpwrctrl_enable_acphy(pi, 0)
 * twice and called the second one a "repeatability witness".  The 2026-10-28
 * issue-10 disassembly showed that shape is not the vendor's: wlc_phy_cals_acphy
 * (0x0b13b5) saves the txpwrctrl enable byte at 0x0b1497, disables inside
 * sub_925cc, does its work, and *restores* the saved byte at 0x0b184f.
 *
 * Recovered contract (all addresses are .text-relative in the pinned
 * wlc_hybrid.o_shipped, sha256 352a6e34...):
 *
 *   0x0b1497  r15b = pi+0xfa0                     software save, no MMIO
 *   0x0925cc  sub_925cc(pi, 0x7148):
 *               wlapi_bmac_write_shm(bmac, 0xb8, 0x7148)   NOT part of this stage
 *               wlapi_suspend_mac_and_wait                  bracket open
 *               wlc_phyreg_enter                            bracket open
 *               wlc_phy_txpwrctrl_enable_acphy(pi, 0)       disable arm
 *   0x0b184f  wlc_phy_txpwrctrl_enable_acphy(pi, r15b)   restore
 *   0x0b1857  wlc_phyreg_exit                            bracket close
 *   0x0b1864  wlapi_enable_mac                           bracket close
 *
 * Arm semantics (0x0906e6..0x0907b8):
 *   0x090704  pi+0xfa0 <- enable (only when enable <= 1) software store
 *   0x09070d  enable != 0 -> enable arm
 *   0x09071a  v = phy_reg_read(0x0070)
 *   0x09071f  (v & 0xe000) == 0xe000              refill predicate
 *   0x090751  phy_reg_mod(0x0070, 0xe000, 0)      disable arm
 *   0x090767  phy_reg_mod(0x0070, 0xe000, 0xe000) enable arm, then per-chain
 *             estimate refill from phy+0x45a + phy+0x168
 *
 * This stage owns the *disable* arm and the bracket.  It does not own the
 * per-chain estimate table, so a restore that would need the enable arm
 * (saved byte != 0) is refused BEFORE any write: disabling a radio we cannot
 * re-enable is worse than not running.
 *
 * What is OpenBRCM's, not the vendor's:
 *   - the saved enable byte is our own shadow of pi+0xfa0 (ob_hw field).  The
 *     vendor's byte is driver state we do not carry; the shadow is 0 because
 *     this driver has never issued the enable arm.  Markers say so.
 *   - wlc_phyreg_enter/exit are reimplemented in ob_lifecycle.c from
 *     0x0bafd1 / 0x0bafac / 0x0605ad / 0x063946, with three documented
 *     deviations: MACCONTROL is read fresh instead of the vendor's cached
 *     hw+0x168; the hw+0x174 branch is not modelled (our field is always 0);
 *     the wake poll bound is ours (the vendor derives it from hw+0x192) and a
 *     timeout is fail-stop, while the vendor proceeds.
 *   - sub_925cc's SHM write (0xb8 <- 0x7148) is deliberately NOT issued here.
 *
 * Admission (unchanged from M1-4): the pre-state tuple and the PHY[0x0380]
 * readiness result must equal the confirmed M1-3/M1-3B baseline, otherwise the
 * stage refuses with zero writes.  The write-time identity guard of the
 * 2026-10-28 review (issues 2 and A1) is kept: each arm must re-read exactly
 * OB_ACPHY_CAL_WRITE_EXPECT_0070.
 *
 * Both software invariants (no owned entry context, no calibration claim) are
 * forced false in every branch, exactly as in M1-3/M1-4.
 */

/* Vendor instruction anchors in wlc_hybrid.o_shipped (sha256 352a6e34...). */
#define OB_ACPHY_CAL_WRITE_SITE_PI_FA0		0x090704u
#define OB_ACPHY_CAL_WRITE_SITE_READ_0070	0x09071au
#define OB_ACPHY_CAL_WRITE_SITE_RMW_0070	0x090760u
/* The vendor's restore call site; its MMIO is the same RMW at 0x090760. */
#define OB_ACPHY_CAL_WRITE_SITE_RESTORE		0x0b184fu

/* Bracket instruction anchors. */
#define OB_ACPHY_CAL_WRITE_SITE_MAC_SUSPEND	0x0925f6u
#define OB_ACPHY_CAL_WRITE_SITE_PHYREG_ENTER	0x0925feu
#define OB_ACPHY_CAL_WRITE_SITE_PHYREG_EXIT	0x0b1858u
#define OB_ACPHY_CAL_WRITE_SITE_MAC_RESUME	0x0b1865u

#define OB_ACPHY_CAL_WRITE_REG_0070		0x0070u
#define OB_ACPHY_CAL_WRITE_MASK_0070		0xe000u

/* Confirmed M1-3/M1-3B pre-state; admission compares against these exactly. */
#define OB_ACPHY_CAL_WRITE_EXPECT_0401		0x7777u
#define OB_ACPHY_CAL_WRITE_EXPECT_0400		0x0000u
#define OB_ACPHY_CAL_WRITE_EXPECT_0070		0x0100u
#define OB_ACPHY_CAL_WRITE_EXPECT_READY		0x0000u

/* Vendor shape: one disable, one restore. */
#define OB_ACPHY_CAL_WRITE_ARM_COUNT		2u
/*
 * Pacing between the two arms, not a vendor constant: the vendor emits no
 * delay between the disable and the restore, so this value cannot be
 * vendor-matched.  M1-4 measured an effective 15.36 ms gap while asking for
 * 1000 us, so 1 ms sufficiency was never exercised on silicon.  Treat it as
 * pacing only; the first readback stays immediate.
 */
#define OB_ACPHY_CAL_WRITE_SETTLE_US		1000u

enum ob_acphy_cal_write_stage_status {
	OB_ACPHY_CAL_WRITE_INVALID = 0,
	/* Pre-state tuple or readiness differs from the confirmed baseline. */
	OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED,
	/* The refill predicate is true on the disable arm: its table is unowned. */
	OB_ACPHY_CAL_WRITE_PREDICATE_TRUE_REFUSED,
	/* A later arm read did not equal the admitted word (after one write). */
	OB_ACPHY_CAL_WRITE_SECOND_ARM_DIVERGED,
	/* A write landed but the readback is not what was written. */
	OB_ACPHY_CAL_WRITE_READBACK_MISMATCH,
	/* Both arms wrote; the surrounding pre-state words moved afterwards. */
	OB_ACPHY_CAL_WRITE_POST_STATE_MOVED,
	/* Disable and restore completed, value-identity, post-state unchanged. */
	OB_ACPHY_CAL_WRITE_ARM_OK,
	OB_ACPHY_CAL_WRITE_FAILED_CALLBACK,
	OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST,
	/* The saved enable byte is not 0: the restore would need the enable arm,
	 * whose per-chain estimate table (phy+0x45a) this stage does not own.
	 * Refused with zero writes and no bracket, before any side effect. */
	OB_ACPHY_CAL_WRITE_RESTORE_UNSUPPORTED,
	/* A bracket callback is missing: no write may be issued without it. */
	OB_ACPHY_CAL_WRITE_BRACKET_UNSUPPORTED,
	/* Suspend/enter/exit/resume failed; the stage unwound what it opened. */
	OB_ACPHY_CAL_WRITE_BRACKET_FAILED,
};

/*
 * The event byte is best-effort trace tagging, not a transaction key: the
 * readiness poll numbers its own events from 1, so tags collide across
 * phases, and the production binding discards both event and source_site.
 * A backend must not key behaviour on event.
 *
 * enable_state_get returns OpenBRCM's shadow of the vendor pi+0xfa0 byte, not
 * the vendor's value.  mac_suspend/resume and phyreg_enter/exit are the four
 * vendor bracket calls; all seven callbacks are required.
 *
 * mac_suspend reports through `we_suspended` whether THIS call put the MAC
 * down.  The vendor's wlapi_suspend_mac_and_wait tolerates an already-suspended
 * MAC, and this bench's attach path leaves the MAC quiesced (MACCONTROL
 * 0x04020402, path=already_quiesced), so a bracket may be entered without a
 * transition.  Only a MAC this stage suspended may be resumed: resuming the
 * attach path's deliberate pause would be a new side effect outside the
 * vendor contract.  mac_resume is therefore called only when
 * we_suspended was true, and it must be a no-op otherwise.
 */
struct ob_acphy_cal_write_stage_ops {
	int (*phy_read16)(void *ctx, u8 event, u32 source_site, u16 reg,
			  u16 *value);
	int (*phy_write16)(void *ctx, u8 event, u32 source_site, u16 reg,
			   u16 value);
	int (*delay_us)(void *ctx, u8 event, u32 source_site, u16 usec);
	bool (*device_lost)(void *ctx);
	int (*enable_state_get)(void *ctx, u8 *value);
	int (*mac_suspend)(void *ctx, u8 event, u32 source_site,
			   bool *we_suspended);
	int (*mac_resume)(void *ctx, u8 event, u32 source_site);
	int (*phyreg_enter)(void *ctx, u8 event, u32 source_site);
	int (*phyreg_exit)(void *ctx, u8 event, u32 source_site);
};

struct ob_acphy_cal_write_stage_result {
	enum ob_acphy_cal_write_stage_status status;
	/* Pre-state sample and the vendor readiness poll (same poll as M1-3). */
	u16 pre_0401;
	u16 pre_0400;
	u16 pre_0070;
	u16 pre_ready_last;
	u32 pre_ready_reads;
	u32 pre_ready_delays;
	bool pre_ready_exhausted;
	/* Saved enable byte (OpenBRCM shadow) and whether the restore was legal. */
	u8 enable_state_saved;
	bool restore_supported;
	/* Bracket progress: each flag only after the corresponding call succeeded.
	 * mac_suspended means "this stage suspended the MAC and must resume it";
	 * an already-quiesced MAC leaves it false and is never resumed. */
	bool mac_suspended;
	bool phyreg_entered;
	bool bracket_completed;
	/* Per-arm evidence: fresh read, written value, readback. */
	u16 arm_read[OB_ACPHY_CAL_WRITE_ARM_COUNT];
	u16 arm_value[OB_ACPHY_CAL_WRITE_ARM_COUNT];
	u16 arm_readback[OB_ACPHY_CAL_WRITE_ARM_COUNT];
	bool arm_predicate_false[OB_ACPHY_CAL_WRITE_ARM_COUNT];
	/* Number of arms whose fresh read was recorded. */
	u32 arms_sampled;
	/* PHY[0x0070] after the pacing delay that follows the disable arm. */
	u16 stable_0070;
	/* Post-state tuple: the surrounding words must not move. */
	u16 post_0401;
	u16 post_0400;
	u16 post_0070;
	u16 post_0380;
	/* Attempted PHY data writes; the contract permits at most ARM_COUNT. */
	u32 writes_issued;
	u32 last_site;
	/* Site that raised the failing callback (unwinding does not change it). */
	u32 failure_site;
	/* Callback return value when status is FAILED_CALLBACK, else 0. */
	int failure_ret;
	/* Always false: this arm is not calibration completion. */
	bool calibration_issued;
	bool entry_context_owned;
};

/*
 * Run the vendor 0x0070 contract.  Returns 0 whenever a defined status was
 * produced (including refusals and callback failures, both carried in
 * result->status) and -EINVAL only for a NULL/incomplete ops or result.
 * Issues at most OB_ACPHY_CAL_WRITE_ARM_COUNT PHY data writes, each equal to
 * the value just read, only after admission and only inside the bracket.
 */
int ob_acphy_cal_write_stage_run(const struct ob_acphy_cal_write_stage_ops *ops,
				 void *ctx,
				 struct ob_acphy_cal_write_stage_result *result);

#endif /* _OB_ACPHY_CAL_WRITE_STAGE_H_ */

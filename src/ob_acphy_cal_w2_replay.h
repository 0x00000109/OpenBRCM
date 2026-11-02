/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_ACPHY_CAL_W2_REPLAY_H_
#define _OB_ACPHY_CAL_W2_REPLAY_H_

#include "ob_acphy_cal_stage.h"

/*
 * W2 — the vendor calibration-entry replay, one bracketed window.
 *
 * Vendor order (pinned wlc_hybrid.o_shipped, sha256 352a6e34...):
 *
 *   0x0b1428  wlc_phy_rxcore_setstate_acphy(pi, state)   own MAC pair
 *             (0x97d13 suspend, 0x97e1d enable), 43 PHY operations
 *   0x0b14ac  sub_925cc: wlapi_suspend_mac_and_wait,
 *             wlc_phyreg_enter
 *   0x090760  wlc_phy_txpwrctrl_enable_acphy(pi, 0)      disable arm
 *   0x0ad418  phy_reg_write(0x0380, value)               kick
 *   0x0ad442  poll 0x0380 until 0xc000 clears
 *   0x0b184f  wlc_phy_txpwrctrl_enable_acphy(pi, saved)  restore arm
 *   0x0b1857  wlc_phyreg_exit
 *   0x0b1864  wlapi_enable_mac
 *
 * The bracket stays OPEN across the kick and its poll.  That ordering is the
 * one the M1-6 write stage cannot express: it restores before returning.
 *
 * Admission is stricter than M1-6 because the boundary moved: the entry tuple
 * is the confirmed 0x7777/0x0000/0x0100 with PHY[0x0380] idle, and the
 * post-setstate boundary admits exactly 0x7733.  0x7730 and 0x7737 are
 * transient values INSIDE the setstate sequence and are refused here.
 *
 * Two known defects are fixed in the modules this stage binds:
 *   W2-P1  ob_a7089_rfseq's PHY[0x0403] poll now exits on CLEAR, as the vendor
 *          does at 0x97caf (it used to exit on SET and burn the whole bound).
 *   W2-P2  the MAC pair is reference counted by the caller's shim; the stage
 *          always closes with one resume and the shim decides whether an
 *          outer owner's pause may be lifted.
 *
 * The kick value is a CANDIDATE (0x0300 or 0x0400) gated by the offline K1
 * proof.  A closed gate is not an error: the stage skips the write, keeps the
 * bracket order, and reports OB_ACPHY_CAL_W2_KICK_SKIPPED so the run records
 * R5-C as untested rather than refuted.
 */

/* Vendor instruction anchors. */
#define OB_ACPHY_CAL_W2_SITE_SETSTATE_CALL	0x0b1428u
#define OB_ACPHY_CAL_W2_SITE_BRACKET_OPEN	0x0b14acu
/* Same vendor sites the M1-6 stage uses for its own bracket and arm.  They are
 * repeated here so W2 does not depend on the M1-6 header. */
#define OB_ACPHY_CAL_W2_SITE_READ_0070		0x09071au
#define OB_ACPHY_CAL_W2_SITE_RMW_0070		0x090760u
#define OB_ACPHY_CAL_W2_SITE_BRACKET_SUSPEND	0x0925f6u
#define OB_ACPHY_CAL_W2_SITE_BRACKET_ENTER	0x0925feu
#define OB_ACPHY_CAL_W2_SITE_BRACKET_EXIT	0x0b1858u
#define OB_ACPHY_CAL_W2_SITE_BRACKET_RESUME	0x0b1865u
#define OB_ACPHY_CAL_W2_SITE_ARM_DISABLE	0x090760u
#define OB_ACPHY_CAL_W2_SITE_ARM_RESTORE	0x0b184fu

/* Vendor setstate inputs (derived from the pinned capture, never invented). */
#define OB_ACPHY_CAL_W2_STATE_2TX		0x03u
/* shared+0xa6 byte restored into PHY[0x0401] bits 0..2 at 0x97de4: the
 * captured boundary moves 0x7730 -> 0x7733, so the low three bits are 3. */
#define OB_ACPHY_CAL_W2_PI_A6			0x03u

/* Confirmed boundary tuples. */
#define OB_ACPHY_CAL_W2_ENTRY_0401		0x7777u
#define OB_ACPHY_CAL_W2_ENTRY_0400		0x0000u
#define OB_ACPHY_CAL_W2_ENTRY_0070		0x0100u
#define OB_ACPHY_CAL_W2_POST_0401		0x7733u
#define OB_ACPHY_CAL_W2_POST_0400		0x0000u
#define OB_ACPHY_CAL_W2_POST_0070		0x0100u

#define OB_ACPHY_CAL_W2_REG_0070		0x0070u
#define OB_ACPHY_CAL_W2_MASK_0070		0xe000u
#define OB_ACPHY_CAL_W2_REG_KICK		0x0380u

/* Candidate kick values.  Writing one requires K1 to be closed. */
#define OB_ACPHY_CAL_W2_KICK_MIN		0x0300u
#define OB_ACPHY_CAL_W2_KICK_MAX		0x0400u

/* Pacing between the disable and the restore arm, as in M1-6. */
#define OB_ACPHY_CAL_W2_SETTLE_US		1000u

enum ob_acphy_cal_w2_status {
	OB_ACPHY_CAL_W2_INVALID = 0,
	/* The entry tuple or the idle 0x0380 sample moved: zero writes. */
	OB_ACPHY_CAL_W2_ENTRY_REFUSED,
	/* The vendor took the shared+0x31 == 0 arm: no PHY access. */
	OB_ACPHY_CAL_W2_SOFTWARE_ONLY,
	/* The setstate callback failed or its arm is not the hardware arm. */
	OB_ACPHY_CAL_W2_SETSTATE_DIVERGED,
	/* The post-setstate boundary is not 0x7733/0x0000/0x0100/0x0000. */
	OB_ACPHY_CAL_W2_POST_REFUSED,
	/* An arm read is not the admitted word: no write is issued. */
	OB_ACPHY_CAL_W2_ARM_DIVERGED,
	/* The bracket was not available or a bracket call failed. */
	OB_ACPHY_CAL_W2_BRACKET_UNSUPPORTED,
	OB_ACPHY_CAL_W2_BRACKET_FAILED,
	/* A write landed but the readback is not what was written. */
	OB_ACPHY_CAL_W2_READBACK_MISMATCH,
	/* The surrounding words moved after the bracket closed. */
	OB_ACPHY_CAL_W2_POST_STATE_MOVED,
	/* The kick poll reached the vendor bound with 0xc000 still set. */
	OB_ACPHY_CAL_W2_KICK_EXHAUSTED,
	/* K1 closed the kick gate or 0x0380 was not idle: no write issued. */
	OB_ACPHY_CAL_W2_KICK_SKIPPED,
	OB_ACPHY_CAL_W2_FULL,
	OB_ACPHY_CAL_W2_FAILED_CALLBACK,
	OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST,
};

/*
 * A backend must not key behaviour on event.  mac_suspend reports to the
 * stage whether the shim made the 0->1 transition; mac_resume is always called
 * once per successful suspend and must leave an outer owner's pause alone.
 * setstate reports whether the vendor took the software-only arm.
 */
struct ob_acphy_cal_w2_ops {
	int (*phy_read16)(void *ctx, u8 event, u32 source_site, u16 reg,
			  u16 *value);
	int (*phy_write16)(void *ctx, u8 event, u32 source_site, u16 reg,
			   u16 value);
	int (*delay_us)(void *ctx, u8 event, u32 source_site, u16 usec);
	bool (*device_lost)(void *ctx);
	int (*mac_suspend)(void *ctx, u8 event, u32 source_site,
			   bool *transitioned);
	int (*mac_resume)(void *ctx, u8 event, u32 source_site);
	int (*phyreg_enter)(void *ctx, u8 event, u32 source_site);
	int (*phyreg_exit)(void *ctx, u8 event, u32 source_site);
	int (*setstate)(void *ctx, u8 event, u32 source_site, u8 state,
			bool *software_only);
};

struct ob_acphy_cal_w2_result {
	enum ob_acphy_cal_w2_status status;
	/* Entry sample. */
	u16 entry_0401;
	u16 entry_0400;
	u16 entry_0070;
	u16 entry_0380;
	/* Setstate phase. */
	bool setstate_called;
	bool setstate_software_only;
	/* Post-setstate boundary sample. */
	u16 post_0401;
	u16 post_0400;
	u16 post_0070;
	u16 post_0380;
	/* Bracket progress. */
	bool mac_suspended;
	bool phyreg_entered;
	bool bracket_completed;
	/* Arm evidence: read, written value, readback (disabled, restore). */
	u16 arm_read[2];
	u16 arm_value[2];
	u16 arm_readback[2];
	u16 stable_0070;
	/* Kick evidence. */
	bool kick_allowed;
	bool kick_skipped;
	bool kick_exhausted;
	u16 kick_pre;
	u16 kick_value;
	u32 kick_reads;
	u32 kick_delays;
	/* Post-bracket identity sample. */
	u16 post_identity_0401;
	u16 post_identity_0400;
	u16 post_identity_0070;
	u16 post_identity_0380;
	/* Write accounting: the contract permits 2 + 1. */
	u32 writes_0070;
	u32 writes_kick;
	u32 last_site;
	u32 failure_site;
	int failure_ret;
	/* Always false: this stage is not calibration completion. */
	bool calibration_issued;
	bool entry_context_owned;
};

/*
 * The 43-operation wlc_phy_rxcore_setstate_acphy ledger, kernel-side.
 *
 * The reference reconstruction of the same vendor body lives in
 * src/ob_phy_a7089_txpwr.c, which is a host model and is not linked into the
 * module.  This runner is the kernel binding of the identical ledger
 * (23 reads, 20 writes, both bounded RF-sequence children); the host tests
 * assert the same op sequence the a7089 tests assert against the vendor.
 *
 * phy_context_nonzero selects the vendor arm: zero performs the software-only
 * store (shared+0xa7) and no PHY or MAC operation.
 */
struct ob_acphy_cal_w2_setstate_result {
	bool called;
	bool software_only;
	u16 saved_0401;
	u16 saved_0400;
	u32 reads;
	u32 writes;
	u32 poll_reads[2];
	u32 poll_delays[2];
	bool poll_exhausted[2];
	u32 last_site;
};

int ob_acphy_cal_w2_setstate_run(const struct ob_acphy_cal_w2_ops *ops,
				 void *ctx, u8 state, bool phy_context_nonzero,
				 struct ob_acphy_cal_w2_setstate_result *result);

/*
 * Run the W2 replay.  Returns 0 whenever a defined status was produced
 * (including refusals, a skipped kick and callback failures, all carried in
 * result->status) and -EINVAL only for NULL/incomplete ops or an out-of-range
 * candidate kick value requested while the gate is open.
 *
 * Issues at most two PHY[0x0070] writes (both value-identity) and at most one
 * PHY[0x0380] write, only when kick_allowed is true and the pre-read is idle.
 * The bracket is always closed on every path that opened it.
 */
int ob_acphy_cal_w2_replay_run(const struct ob_acphy_cal_w2_ops *ops,
			       void *ctx, bool kick_allowed, u16 kick_value,
			       struct ob_acphy_cal_w2_result *result);

#endif /* _OB_ACPHY_CAL_W2_REPLAY_H_ */

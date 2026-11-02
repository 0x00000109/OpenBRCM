/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_ACPHY_CAL_RUN_H_
#define _OB_ACPHY_CAL_RUN_H_

#include "ob_phy_lpf.h"

/*
 * First bounded BCM4352 initial-calibration transaction (Phase 36T).
 *
 * Scope is exactly the recovered vendor region
 * wlc_phy_cals_acphy @0x0b13b5 through the sub_925cc call @0x0b14ac, the two
 * cal-state snapshots @0x0b14b1..0x0b14d0 and the PI+0xf84 transmit-tone gate
 * @0x0b14d7.  The sub_b1227 call @0x0b14f6 and everything after it is NOT
 * recovered and is never executed: the runner stops and reports it.
 *
 * The RX-core state transition (`wlc_phy_rxcore_setstate_acphy` @0x97ce5) is
 * already implemented and owned by src/ob_phy_a7089_txpwr.c.  This module
 * therefore requires a delegate callback instead of reimplementing it.  The
 * operation set implemented here is `sub_925cc` @0x925cc, which exists nowhere
 * else in the driver.
 *
 * The runner performs no calibration-completion claim, sets no lifecycle
 * token, and has no production caller; it is reachable only from the offline
 * host harness.  See audit/openbrcm_phase36t/ for the instruction evidence.
 */

/* Vendor instruction anchors in the pinned wlc_hybrid.o_shipped image. */
#define OB_ACPHY_CAL_VENDOR_CALS_ENTRY		0x0b13b5u
#define OB_ACPHY_CAL_VENDOR_ENTRY_GATE		0x0b13e2u /* test [pi+0x19c],0x10 */
#define OB_ACPHY_CAL_VENDOR_COMMON_PREP		0x0b13efu /* a7<-a5, a6<-a4  */
#define OB_ACPHY_CAL_VENDOR_RXCORE_CALL		0x0b1428u /* -> 0x97ce5       */
#define OB_ACPHY_CAL_VENDOR_CHANGED_GATE	0x0b145fu
#define OB_ACPHY_CAL_VENDOR_SUB925CC_CALL	0x0b14acu /* -> 0x925cc       */
#define OB_ACPHY_CAL_VENDOR_SNAPSHOT_C0		0x0b14bfu /* cal+0xc0         */
#define OB_ACPHY_CAL_VENDOR_SNAPSHOT_BA		0x0b14d0u /* cal+0xba         */
#define OB_ACPHY_CAL_VENDOR_TX_TONE_GATE	0x0b14d7u /* [pi+0xf84]       */
#define OB_ACPHY_CAL_VENDOR_TX_TONE_CALL	0x0b14e3u /* -> 0xaffa9       */
#define OB_ACPHY_CAL_VENDOR_B1227_CALL		0x0b14f6u /* -> 0xb1227       */
#define OB_ACPHY_CAL_VENDOR_ABC76_CALL0		0x0b150au /* -> 0xabc76       */
#define OB_ACPHY_CAL_VENDOR_SUB925CC_ENTRY	0x0925ccu
#define OB_ACPHY_CAL_VENDOR_POLL_WRITE		0x0ad418u
#define OB_ACPHY_CAL_VENDOR_POLL_READ		0x0ad442u
#define OB_ACPHY_CAL_VENDOR_POLL_COUNTER	0x0ad425u
#define OB_ACPHY_CAL_VENDOR_POLL_DELAY		0x0ad436u

/* sub_925cc(pi, 0x7148): wlapi_bmac_write_shm(hw, 0xb8, 0x7148). */
#define OB_ACPHY_CAL_SUB925CC_SHM_OFFSET	0x00b8u
#define OB_ACPHY_CAL_SUB925CC_SHM_VALUE		0x7148u

/* wlc_phy_cals_acphy entry gate: nonzero means the vendor returns at once. */
#define OB_ACPHY_CAL_ENTRY_SKIP_BIT		0x10u

/*
 * sub_abc76 readiness poll @0x0ad418..0x0ad45e (16-bit PHY register access).
 * The vendor writes PHY[0x380], then reads it repeatedly until both 0xc000
 * bits are clear, delaying 10 us between reads from a 0x4e29 counter that
 * decrements by 10 and stops at 9.  That is a finite vendor bound:
 * (0x4e29 - 9) / 10 = 2000 delays and 2001 reads.
 */
#define OB_ACPHY_CAL_POLL_REG			0x0380u
#define OB_ACPHY_CAL_POLL_MASK			0xc000u
#define OB_ACPHY_CAL_POLL_DELAY_US		10u
#define OB_ACPHY_CAL_POLL_COUNTER_INIT		0x4e29u
#define OB_ACPHY_CAL_POLL_COUNTER_STEP		10u
#define OB_ACPHY_CAL_POLL_COUNTER_FLOOR		9u
#define OB_ACPHY_CAL_POLL_MAX_DELAYS		2000u
#define OB_ACPHY_CAL_POLL_MAX_READS		2001u

struct ob_acphy_cal_ops {
	int (*bmac_write_shm)(void *ctx, u8 logical_event, u32 source_site,
			      u16 offset, u32 value);
	int (*suspend_mac_and_wait)(void *ctx, u8 logical_event,
				    u32 source_site);
	int (*phyreg_enter)(void *ctx, u8 logical_event, u32 source_site);
	int (*txpwrctrl_enable)(void *ctx, u8 logical_event, u32 source_site,
				u8 enable);
	int (*phy_read16)(void *ctx, u8 logical_event, u32 source_site,
			  u16 reg, u16 *value);
	int (*phy_write16)(void *ctx, u8 logical_event, u32 source_site,
			   u16 reg, u16 value);
	int (*delay_us)(void *ctx, u8 logical_event, u32 source_site,
			u16 usec);
	/*
	 * wlc_phy_rxcore_setstate_acphy delegate.  The implementation lives in
	 * the existing a7089 TX-power module and is deliberately not duplicated
	 * here; a NULL callback is an explicit blocker, never a silent skip.
	 */
	int (*rxcore_setstate)(void *ctx, u8 logical_event, u32 source_site,
			       u8 state);
	/* OpenBRCM addition: the vendor has no device-loss predicate. */
	bool (*device_lost)(void *ctx);
};

enum ob_acphy_cal_status {
	OB_ACPHY_CAL_INVALID = 0,
	/* Vendor entry gate: PI+0x19c bit 0x10 set, no operation executed. */
	OB_ACPHY_CAL_STOPPED_ENTRY_FLAG,
	/* No owner was supplied for the RX-core state transition. */
	OB_ACPHY_CAL_STOPPED_NO_RXCORE_OWNER,
	/* PI+0xf84 nonzero: the transmit-tone helper is not recovered. */
	OB_ACPHY_CAL_STOPPED_TX_TONE,
	/* sub_b1227 and the sub_abc76 entries are not recovered. */
	OB_ACPHY_CAL_STOPPED_B1227_TAIL,
	OB_ACPHY_CAL_FAILED_DEVICE_LOST,
	OB_ACPHY_CAL_FAILED_CALLBACK,
};

struct ob_acphy_cal_input {
	/* True only from a completed, validated predecessor stage. */
	bool predecessor_ready;
	bool chanspec_valid;
	u16 chanspec;
	/*
	 * Latch set by the caller after any earlier fatal calibration failure
	 * (including a poll timeout).  A set latch rejects the transaction
	 * before the first callback.
	 */
	bool prior_fatal;
	/* Vendor software state read at the cal entry. */
	u8 pi_19c;
	u16 pi_17e;
	/* Set to 1 by the selected reason-6 arm before the cal body runs. */
	bool pi_f84;
	/* Snapshot sources: *(shared+0x34) and the live chanspec word. */
	u32 shared_34;
	/* Common-state bytes rearranged by the vendor entry sequence. */
	u8 common_a4;
	u8 common_a5;
	u8 common_a6;
	u8 common_a7;
};

struct ob_acphy_cal_result {
	enum ob_acphy_cal_status status;
	/* Identity admission outcome, reused from the LPF target policy. */
	enum ob_phy_lpf_input_status target_status;
	/* Last vendor source site actually issued. */
	u32 last_site;
	u32 callback_count;
	u32 logical_event_count;
	bool rxcore_setstate_called;
	u8 rxcore_state;
	/* Vendor stores cal+0xc0 <- *(shared+0x34) and cal+0xba <- PI+0x17e. */
	u32 snapshot_cal_c0;
	u16 snapshot_cal_ba;
	/* Vendor entry rearrangement: shared+0xa7 <- a5, shared+0xa6 <- a4. */
	u8 common_a6_after;
	u8 common_a7_after;
	/* True whenever the transaction refused to run its unrecovered tail. */
	bool tail_blocked;
	/*
	 * Always false.  One sub-engine is not calibration completion, and no
	 * lifecycle token is set by this module.
	 */
	bool calibration_complete;
};

enum ob_acphy_cal_poll_status {
	OB_ACPHY_CAL_POLL_INVALID = 0,
	OB_ACPHY_CAL_POLL_CLEARED,
	OB_ACPHY_CAL_POLL_EXHAUSTED,
	OB_ACPHY_CAL_POLL_FAILED_CALLBACK,
	OB_ACPHY_CAL_POLL_DEVICE_LOST,
};

struct ob_acphy_cal_poll_result {
	enum ob_acphy_cal_poll_status status;
	u32 reads;
	u32 delays;
	u16 last_value;
	/* Vendor counter value at exit; 9 means the finite bound was reached. */
	u32 counter_end;
	/*
	 * True for every non-success outcome.  A caller that appends further
	 * calibration operations must consult it; on timeout the vendor falls
	 * through to later work, OpenBRCM must not.
	 */
	bool successor_blocked;
};

/*
 * Run the recovered block.  Never returns 0: every path either fails or stops
 * at an explicitly unrecovered vendor boundary, and reports which one.
 *   -EINVAL       invalid target/predecessor/chanspec
 *   -EOPNOTSUPP   unsupported target, entry gate taken, missing RX-core owner,
 *                 or an unrecovered tail
 *   -EIO          prior fatal latch or device loss
 *   callback error is propagated verbatim
 */
int ob_acphy_cal_run(const struct ob_acphy_cal_ops *ops, void *ctx,
		     const struct ob_phy_lpf_target *target,
		     const struct ob_acphy_cal_input *in,
		     struct ob_acphy_cal_result *result);

/*
 * Bounded PHY[0x380] readiness poll for the first sub_abc76 entry.
 * Returns 0 when both 0xc000 status bits are clear, -ETIMEDOUT when the finite
 * vendor bound is reached, -EIO on device loss, or the callback error.  It
 * issues reads and delays only; it never writes.
 */
int ob_acphy_cal_poll380_run(const struct ob_acphy_cal_ops *ops, void *ctx,
			     struct ob_acphy_cal_poll_result *result);

#endif /* _OB_ACPHY_CAL_RUN_H_ */

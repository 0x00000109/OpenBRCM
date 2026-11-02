/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_ACPHY_CAL_STAGE_H_
#define _OB_ACPHY_CAL_STAGE_H_

#include "ob_acphy_cal_run.h"

/*
 * M1-3 — first hardware stage after the admitted calibration-input contract.
 *
 * The recovered calibration entry block (ob_acphy_cal_run) cannot run on
 * silicon yet: it consumes vendor driver context bytes that OpenBRCM does not
 * own (pi+0x19c, shared+0xa4/a5/a6/a7, shared+0x31, shared+0x34).  Inventing
 * those values is forbidden, so this stage is READ-ONLY by construction:
 *
 *   1. sample the three PHY registers the entry block saves or mutates:
 *        PHY[0x0401]  rxcore_setstate saves and rewrites this word
 *        PHY[0x0400]  rxcore_setstate saves and rewrites this word
 *        PHY[0x0070]  txpwrctrl_enable(0) reads and rewrites this word
 *   2. run the recovered sub_abc76 readiness poll on PHY[0x0380]
 *      (ob_acphy_cal_poll380_run: 2001 reads / 2000 x 10 us vendor bound),
 *   3. stop.  No calibration write is issued, no lifecycle completion token is
 *      published, and no calibration claim is made.
 *
 * The ops struct has no write callback at all, so "read-only" is a property of
 * the interface, not a promise.
 *
 * Vendor anchors in the pinned wlc_hybrid.o_shipped (sha256 352a6e34...):
 *   0x097d20  wlc_phy_rxcore_setstate_acphy reads PHY[0x0401]
 *   0x097d30  wlc_phy_rxcore_setstate_acphy reads PHY[0x0400]
 *   0x09071a  wlc_phy_txpwrctrl_enable_acphy reads PHY[0x0070]
 *   0x0ad442  sub_abc76 readiness poll reads PHY[0x0380]
 *   0x0ad425  poll counter initial value 0x4e29, step 10, floor 9
 */
#define OB_ACPHY_CAL_STAGE_SITE_READ_0401	0x097d20u
#define OB_ACPHY_CAL_STAGE_SITE_READ_0400	0x097d30u
#define OB_ACPHY_CAL_STAGE_SITE_READ_0070	0x09071au

#define OB_ACPHY_CAL_STAGE_REG_0401		0x0401u
#define OB_ACPHY_CAL_STAGE_REG_0400		0x0400u
#define OB_ACPHY_CAL_STAGE_REG_0070		0x0070u

/*
 * The only calibration-context byte this stage can name with provenance:
 * the accepted 2-TX-chain configuration (both chains active) makes shared+0xa5
 * equal to 0x03.  The remaining bytes stay UNKNOWN; they are reported by the
 * caller so a reader cannot mistake the stage for an owned entry context.
 */
#define OB_ACPHY_CAL_STAGE_SHARED_A5_2CHAIN	0x03u

enum ob_acphy_cal_stage_status {
	OB_ACPHY_CAL_STAGE_INVALID = 0,
	/* Readiness bits were already clear on the first sample. */
	OB_ACPHY_CAL_STAGE_WITNESS_OK,
	/* Bits were set at entry and cleared within the vendor bound. */
	OB_ACPHY_CAL_STAGE_READY_AFTER_POLL,
	/* Vendor bound reached with the status bits still set. */
	OB_ACPHY_CAL_STAGE_READY_EXHAUSTED,
	OB_ACPHY_CAL_STAGE_FAILED_CALLBACK,
	OB_ACPHY_CAL_STAGE_FAILED_DEVICE_LOST,
};

struct ob_acphy_cal_stage_ops {
	int (*phy_read16)(void *ctx, u8 event, u32 source_site, u16 reg,
			  u16 *value);
	int (*delay_us)(void *ctx, u8 event, u32 source_site, u16 usec);
	bool (*device_lost)(void *ctx);
};

struct ob_acphy_cal_stage_result {
	enum ob_acphy_cal_stage_status status;
	u16 phy_0401;
	u16 phy_0400;
	u16 phy_0070;
	u16 readiness_last;
	u32 readiness_reads;
	u32 readiness_delays;
	u32 last_site;
	/* Callback return value when status is FAILED_CALLBACK, else 0. */
	int failure_ret;
	/* The finite vendor poll bound was reached. */
	bool readiness_exhausted;
	/*
	 * Always false: the calibration entry context is not owned and no
	 * calibration operation is issued by this stage.
	 */
	bool entry_context_owned;
	bool calibration_issued;
};

/*
 * Run the read-only calibration pre-state witness.  Returns 0 whenever a
 * defined status was produced (including exhaustion and callback failure, both
 * carried in result->status), and -EINVAL only for a NULL or incomplete ops /
 * result argument.  Never issues a write.
 */
int ob_acphy_cal_stage_witness_run(const struct ob_acphy_cal_stage_ops *ops,
				   void *ctx,
				   struct ob_acphy_cal_stage_result *result);

#endif /* _OB_ACPHY_CAL_STAGE_H_ */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * M1-6 — vendor 0x0070 contract: save -> disable -> restore inside the
 * MAC-suspend / phyreg bracket (S01..S21).
 *
 * The code under test is src/ob_acphy_cal_write_stage.c.  The backend is a
 * deterministic recorder: no MMIO is issued, and every result here is
 * HOST_EXECUTED against that recorder, not hardware evidence.
 *
 * These tests cover the admission predicate (exact M1-3 tuple plus readiness),
 * the vendor refill-predicate refusal, the value-identity write discipline,
 * readback and post-state checks, the fail-stop paths, and the two invariants
 * (no owned entry context, no calibration claim).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_acphy_cal_write_stage.h"

#define FAKE_ERR (-71)

struct fake {
	unsigned int calls;
	unsigned int writes;
	unsigned int reads_0070;
	unsigned int reads_0401;
	unsigned int poll_reads;
	u16 reg_0401;
	u16 reg_0400;
	u16 reg_0070;
	/* Busy 0x380 samples before the first clear one; -1 = forever. */
	int busy_reads;
	/* Apply swapped_0070 when 0x0070 is read for the Nth time; 0 disables. */
	unsigned int swap_0070_on_read;
	u16 swapped_0070;
	/* Replace the 0x0070 readback (read 3 and later); 0 disables. */
	unsigned int force_readback_on_read;
	u16 forced_readback;
	/* Replace the post-state 0x0401 read (read 2); 0 disables. */
	u16 post_0401;
	/* Fail the Nth write; 0 disables.  The store happens first. */
	unsigned int fail_write_on;
	/* device_lost becomes true once this many calls have run; 0 disables. */
	unsigned int lost_after;
	/* Saved enable byte (OpenBRCM shadow) and bracket failure injection. */
	u8 enable_state;
	int fail_enable_state;
	int fail_mac_suspend;
	/* What mac_suspend reports in we_suspended: false = MAC was already down. */
	bool suspend_opened;
	int fail_phyreg_enter;
	int fail_phyreg_exit;
	int fail_mac_resume;
	/* Bracket call order: 1 suspend, 2 enter, 3 exit, 4 resume. */
	unsigned int bracket_order[16];
	unsigned int bracket_count;
	u16 last_written;
	u32 sites[32];
	unsigned int site_count;
};

static void fake_record_site(struct fake *f, u32 site)
{
	if (f->site_count < 32)
		f->sites[f->site_count++] = site;
}

static int f_read16(void *ctx, u8 event, u32 source_site, u16 reg, u16 *value)
{
	struct fake *f = ctx;

	(void)event;
	f->calls++;
	fake_record_site(f, source_site);
	switch (reg) {
	case OB_ACPHY_CAL_STAGE_REG_0401:
		f->reads_0401++;
		*value = f->reads_0401 >= 2 && f->post_0401 ?
			 f->post_0401 : f->reg_0401;
		break;
	case OB_ACPHY_CAL_STAGE_REG_0400:
		*value = f->reg_0400;
		break;
	case OB_ACPHY_CAL_WRITE_REG_0070:
		f->reads_0070++;
		if (f->swap_0070_on_read &&
		    f->reads_0070 == f->swap_0070_on_read)
			f->reg_0070 = f->swapped_0070;
		if (f->force_readback_on_read &&
		    f->reads_0070 >= f->force_readback_on_read)
			*value = f->forced_readback;
		else
			*value = f->reg_0070;
		break;
	case OB_ACPHY_CAL_POLL_REG:
		f->poll_reads++;
		if (f->busy_reads < 0 ||
		    (int)f->poll_reads <= f->busy_reads)
			*value = 0x4000;
		else
			*value = 0x0000;
		break;
	default:
		*value = 0;
		break;
	}
	return 0;
}

static int f_write16(void *ctx, u8 event, u32 source_site, u16 reg, u16 value)
{
	struct fake *f = ctx;

	(void)event;
	f->calls++;
	fake_record_site(f, source_site);
	assert(reg == OB_ACPHY_CAL_WRITE_REG_0070);
	f->writes++;
	f->last_written = value;
	f->reg_0070 = value;
	if (f->fail_write_on && f->writes == f->fail_write_on)
		return FAKE_ERR;
	return 0;
}

static int f_delay(void *ctx, u8 event, u32 source_site, u16 usec)
{
	struct fake *f = ctx;

	(void)event;
	(void)source_site;
	(void)usec;
	f->calls++;
	return 0;
}

static bool f_lost(void *ctx)
{
	struct fake *f = ctx;

	return f->lost_after && f->calls >= f->lost_after;
}

static void f_bracket(struct fake *f, unsigned int tag)
{
	if (f->bracket_count < 16)
		f->bracket_order[f->bracket_count++] = tag;
}

static int f_enable_get(void *ctx, u8 *value)
{
	struct fake *f = ctx;

	if (f->fail_enable_state)
		return f->fail_enable_state;
	*value = f->enable_state;
	return 0;
}

static int f_mac_suspend(void *ctx, u8 event, u32 source_site,
			 bool *we_suspended)
{
	struct fake *f = ctx;

	(void)event;
	(void)source_site;
	f->calls++;
	f_bracket(f, 1);
	if (!f->fail_mac_suspend && we_suspended)
		*we_suspended = f->suspend_opened;
	return f->fail_mac_suspend;
}

static int f_phyreg_enter(void *ctx, u8 event, u32 source_site)
{
	struct fake *f = ctx;

	(void)event;
	(void)source_site;
	f->calls++;
	f_bracket(f, 2);
	return f->fail_phyreg_enter;
}

static int f_phyreg_exit(void *ctx, u8 event, u32 source_site)
{
	struct fake *f = ctx;

	(void)event;
	(void)source_site;
	f->calls++;
	f_bracket(f, 3);
	return f->fail_phyreg_exit;
}

static int f_mac_resume(void *ctx, u8 event, u32 source_site)
{
	struct fake *f = ctx;

	(void)event;
	(void)source_site;
	f->calls++;
	f_bracket(f, 4);
	return f->fail_mac_resume;
}

static struct ob_acphy_cal_write_stage_ops fake_ops(void)
{
	return (struct ob_acphy_cal_write_stage_ops) {
		.phy_read16 = f_read16,
		.phy_write16 = f_write16,
		.delay_us = f_delay,
		.device_lost = f_lost,
		.enable_state_get = f_enable_get,
		.mac_suspend = f_mac_suspend,
		.mac_resume = f_mac_resume,
		.phyreg_enter = f_phyreg_enter,
		.phyreg_exit = f_phyreg_exit,
	};
}

static struct fake fake_init(void)
{
	return (struct fake) {
		.reg_0401 = OB_ACPHY_CAL_WRITE_EXPECT_0401,
		.reg_0400 = OB_ACPHY_CAL_WRITE_EXPECT_0400,
		.reg_0070 = OB_ACPHY_CAL_WRITE_EXPECT_0070,
		.enable_state = 0,
		.suspend_opened = true,
	};
}

static void run_stage(struct fake *f,
		      struct ob_acphy_cal_write_stage_result *r)
{
	struct ob_acphy_cal_write_stage_ops ops = fake_ops();

	assert(ob_acphy_cal_write_stage_run(&ops, f, r) == 0);
	assert(r->entry_context_owned == false);
	assert(r->calibration_issued == false);
}

/* S01: incomplete arguments are rejected and issue no callback. */
static void test_invalid_args(void)
{
	struct ob_acphy_cal_write_stage_ops ops = fake_ops();
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	assert(ob_acphy_cal_write_stage_run(NULL, &f, &r) == -22);
	assert(ob_acphy_cal_write_stage_run(&ops, &f, NULL) == -22);
	ops.phy_read16 = NULL;
	assert(ob_acphy_cal_write_stage_run(&ops, &f, &r) == -22);
	ops = fake_ops();
	ops.phy_write16 = NULL;
	assert(ob_acphy_cal_write_stage_run(&ops, &f, &r) == -22);
	ops = fake_ops();
	ops.delay_us = NULL;
	assert(ob_acphy_cal_write_stage_run(&ops, &f, &r) == -22);
	assert(f.calls == 0);
	printf("S01 invalid-args PASS\n");
}

/* S02: baseline tuple, both arms, value-identity, post-state unchanged. */
static void test_baseline_arm(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();
	unsigned int i;

	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_ARM_OK);
	assert(r.pre_0401 == 0x7777);
	assert(r.pre_0400 == 0x0000);
	assert(r.pre_0070 == 0x0100);
	assert(r.pre_ready_last == 0x0000);
	assert(r.pre_ready_reads >= 1);
	assert(r.pre_ready_exhausted == false);
	assert(r.arms_sampled == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	assert(r.writes_issued == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	assert(f.writes == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	for (i = 0; i < OB_ACPHY_CAL_WRITE_ARM_COUNT; i++) {
		assert(r.arm_predicate_false[i] == true);
		assert(r.arm_read[i] == 0x0100);
		/* S15 mutation pin: the written word is the constant, not a
		 * value derived from a drifted read. */
		assert(r.arm_value[i] == OB_ACPHY_CAL_WRITE_EXPECT_0070);
		/* The admitted arm is value-identity by construction. */
		assert(r.arm_value[i] == r.arm_read[i]);
		assert(r.arm_readback[i] == r.arm_value[i]);
	}
	/* S16-S20 pins: the vendor save/restore shape and the bracket. */
	assert(r.enable_state_saved == 0);
	assert(r.restore_supported == true);
	assert(r.mac_suspended == false);
	assert(r.phyreg_entered == false);
	assert(r.bracket_completed == true);
	assert(f.bracket_count == 4);
	assert(f.bracket_order[0] == 1); /* suspend */
	assert(f.bracket_order[1] == 2); /* phyreg enter */
	assert(f.bracket_order[2] == 3); /* phyreg exit */
	assert(f.bracket_order[3] == 4); /* mac resume */
	assert(r.stable_0070 == 0x0100);
	assert(r.post_0401 == r.pre_0401);
	assert(r.post_0400 == r.pre_0400);
	assert(r.post_0070 == r.pre_0070);
	assert(r.post_0380 == r.pre_ready_last);
	assert(f.last_written == 0x0100);
	printf("S02 baseline-arm PASS\n");
}

/* S03: every field of the admission tuple is enforced, zero writes. */
static void test_prestate_diverged(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f;

	f = fake_init();
	f.reg_0401 = 0x7770;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED);
	assert(r.writes_issued == 0);
	assert(f.writes == 0);
	assert(r.arms_sampled == 0);

	f = fake_init();
	f.reg_0400 = 0x0001;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED);
	assert(f.writes == 0);

	f = fake_init();
	f.reg_0070 = 0x0300;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED);
	assert(f.writes == 0);

	/* Readiness already busy on the first sample is also a refusal. */
	f = fake_init();
	f.busy_reads = 2;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED);
	assert(r.pre_ready_reads == 3);
	assert(r.pre_ready_last == 0x0000);
	assert(f.writes == 0);
	printf("S03 prestate-diverged PASS\n");
}

/* S04: the finite vendor bound is reached -> refusal, not an extension. */
static void test_readiness_exhausted(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.busy_reads = -1;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED);
	assert(r.pre_ready_exhausted == true);
	assert(r.pre_ready_reads == OB_ACPHY_CAL_POLL_MAX_READS);
	assert(r.pre_ready_delays == OB_ACPHY_CAL_POLL_MAX_DELAYS);
	assert(f.writes == 0);
	assert(r.arms_sampled == 0);
	printf("S04 readiness-exhausted PASS\n");
}

/* S05: a true refill predicate on the first arm refuses before any write. */
static void test_predicate_true_refused(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	/* The pre-state read admits; the first fresh arm read does not. */
	f.swap_0070_on_read = 2;
	f.swapped_0070 = 0xe100;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_PREDICATE_TRUE_REFUSED);
	assert(r.arms_sampled == 1);
	assert(r.arm_predicate_false[0] == false);
	assert(r.writes_issued == 0);
	assert(f.writes == 0);
	printf("S05 predicate-true-refused PASS\n");
}

/* S06: the predicate turning true on the witness stops after one write. */
static void test_second_arm_diverged(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	/* 0x0070 read order: 1 pre, 2 arm1, 3 readback1, 4 stable, 5 arm2. */
	f.swap_0070_on_read = 5;
	f.swapped_0070 = 0xe100;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_SECOND_ARM_DIVERGED);
	assert(r.writes_issued == 1);
	assert(f.writes == 1);
	assert(r.arms_sampled == 2);
	assert(r.arm_predicate_false[0] == true);
	assert(r.arm_predicate_false[1] == false);
	printf("S06 second-arm-diverged PASS\n");
}

/* S07: a write that does not read back stops the stage. */
static void test_readback_mismatch(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.force_readback_on_read = 3; /* the first readback after the write */
	f.forced_readback = 0x0104;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_READBACK_MISMATCH);
	assert(r.writes_issued == 1);
	assert(f.writes == 1);
	assert(r.arm_readback[0] == 0x0104);
	assert(r.arm_readback[0] != r.arm_value[0]);
	printf("S07 readback-mismatch PASS\n");
}

/* S08: both arms wrote but a surrounding word moved. */
static void test_post_state_moved(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.post_0401 = 0x7776;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_POST_STATE_MOVED);
	assert(r.writes_issued == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	assert(r.post_0401 == 0x7776);
	printf("S08 post-state-moved PASS\n");
}

/* S09: a write callback error is fail-stop and counts the attempt. */
static void test_write_error(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.fail_write_on = 1;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_FAILED_CALLBACK);
	assert(r.failure_ret == FAKE_ERR);
	assert(r.writes_issued == 1);
	assert(f.writes == 1);
	/* Unwinding the bracket must not erase the site that failed. */
	assert(r.failure_site == OB_ACPHY_CAL_WRITE_SITE_RMW_0070);
	printf("S09 write-error PASS\n");
}

/* S10: device loss during the arm is classified as loss, never as callback. */
static void test_device_lost(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	/* Call order: 3 pre-state reads, poll read, suspend, enter, arm-1 read,
	 * write, readback -> the latch is visible at the readback. */
	f.lost_after = 9;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST);
	assert(r.writes_issued == 1);
	printf("S10 device-lost PASS\n");
}

/* S11: the vendor site order and the software store are not invented. */
static void test_site_order(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();
	unsigned int i;

	run_stage(&f, &r);
	assert(f.site_count >= 8);
	assert(f.sites[0] == OB_ACPHY_CAL_STAGE_SITE_READ_0401);
	assert(f.sites[1] == OB_ACPHY_CAL_STAGE_SITE_READ_0400);
	assert(f.sites[2] == OB_ACPHY_CAL_STAGE_SITE_READ_0070);
	assert(f.sites[3] == OB_ACPHY_CAL_VENDOR_POLL_READ);
	assert(f.sites[4] == OB_ACPHY_CAL_WRITE_SITE_READ_0070);
	assert(f.sites[5] == OB_ACPHY_CAL_WRITE_SITE_RMW_0070);
	for (i = 0; i < f.site_count; i++)
		assert(f.sites[i] != 0);
	printf("S11 site-order PASS\n");
}

/* S12: no path publishes more writes than the contract allows. */
static void test_write_budget(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	run_stage(&f, &r);
	assert(r.writes_issued <= OB_ACPHY_CAL_WRITE_ARM_COUNT);
	assert(f.writes == r.writes_issued);

	/* Refusal path: budget untouched. */
	f = fake_init();
	f.reg_0401 = 0;
	run_stage(&f, &r);
	assert(r.writes_issued == 0);
	printf("S12 write-budget PASS\n");
}

/*
 * S13: a partial-mask word on the first fresh arm read (0x6100 -- a value
 * this silicon really produces) passes the refill predicate but is not the
 * admitted word. Before the 2026-10-28 fix the stage wrote 0x0100 through it,
 * clearing bits 14/13, and reported ARM_OK. It must refuse with zero writes.
 */
static void test_partial_mask_first_arm(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	/* 0x0070 read order: 1 pre-state, 2 the first fresh arm read. */
	f.swap_0070_on_read = 2;
	f.swapped_0070 = 0x6100;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED);
	assert(r.arms_sampled == 1);
	assert(r.arm_read[0] == 0x6100);
	assert(r.arm_predicate_false[0] == true);
	assert(r.writes_issued == 0);
	assert(f.writes == 0);
	printf("S13 partial-mask-first-arm PASS\n");
}

/* S14: the same drift on the witness stops after exactly one write. */
static void test_partial_mask_second_arm(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.swap_0070_on_read = 5; /* pre, arm1, readback1, stable, arm2 */
	f.swapped_0070 = 0x6500;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_SECOND_ARM_DIVERGED);
	assert(r.arms_sampled == 2);
	assert(r.arm_read[0] == 0x0100);
	assert(r.arm_read[1] == 0x6500);
	assert(r.writes_issued == 1);
	assert(f.writes == 1);
	assert(f.last_written == 0x0100);
	printf("S14 partial-mask-second-arm PASS\n");
}

/* S16: a nonzero saved enable byte would need the enable arm (phy+0x45a).
 * The stage must refuse before any side effect, not disable a radio it cannot
 * re-enable. */
static void test_restore_unsupported(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.enable_state = 1;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_RESTORE_UNSUPPORTED);
	assert(r.enable_state_saved == 1);
	assert(r.restore_supported == false);
	assert(r.mac_suspended == false);
	assert(f.bracket_count == 0);
	assert(r.writes_issued == 0);
	assert(f.writes == 0);
	printf("S16 restore-unsupported PASS\n");
}

/* S17: a missing bracket callback is a refusal, never a silent skip. */
static void test_bracket_unsupported(void)
{
	struct ob_acphy_cal_write_stage_ops ops = fake_ops();
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();
	int ret;

	ops.phyreg_enter = NULL;
	ret = ob_acphy_cal_write_stage_run(&ops, &f, &r);
	assert(ret == 0);
	assert(r.status == OB_ACPHY_CAL_WRITE_BRACKET_UNSUPPORTED);
	assert(r.writes_issued == 0);
	assert(f.writes == 0);
	assert(f.bracket_count == 0);
	printf("S17 bracket-unsupported PASS\n");
}

/* S18: suspend failure stops before enter and before any write. */
static void test_mac_suspend_failed(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.fail_mac_suspend = FAKE_ERR;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
	assert(r.failure_ret == FAKE_ERR);
	assert(r.mac_suspended == false);
	assert(r.phyreg_entered == false);
	assert(r.bracket_completed == false);
	assert(f.bracket_count == 1);
	assert(f.bracket_order[0] == 1);
	assert(r.writes_issued == 0);
	assert(f.writes == 0);
	printf("S18 mac-suspend-failed PASS\n");
}

/* S19: phyreg enter failure resumes the MAC it just suspended. */
static void test_phyreg_enter_failed(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.fail_phyreg_enter = FAKE_ERR;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
	assert(r.mac_suspended == false);
	assert(r.phyreg_entered == false);
	assert(r.bracket_completed == false);
	assert(f.bracket_count == 3);
	assert(f.bracket_order[0] == 1);
	assert(f.bracket_order[1] == 2);
	assert(f.bracket_order[2] == 4); /* unwind: resume, no exit */
	assert(r.writes_issued == 0);
	assert(f.writes == 0);
	printf("S19 phyreg-enter-failed PASS\n");
}

/*
 * S21: the MAC was already quiesced at bracket open (this bench's attach state,
 * MACCONTROL 0x04020402).  The bracket still runs phyreg enter/exit, both arms
 * execute, and the close must NOT resume a pause the stage does not own.
 */
static void test_mac_already_suspended(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.suspend_opened = false;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_ARM_OK);
	assert(r.mac_suspended == false);
	assert(r.phyreg_entered == false);
	assert(r.bracket_completed == true);
	assert(f.bracket_count == 3);
	assert(f.bracket_order[0] == 1); /* suspend (no transition) */
	assert(f.bracket_order[1] == 2); /* phyreg enter */
	assert(f.bracket_order[2] == 3); /* phyreg exit, no resume */
	assert(r.writes_issued == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	assert(f.writes == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	printf("S21 mac-already-suspended PASS\n");
}

/* S20: exit failure after both writes is fail-stop and still resumes the MAC. */
static void test_phyreg_exit_failed(void)
{
	struct ob_acphy_cal_write_stage_result r;
	struct fake f = fake_init();

	f.fail_phyreg_exit = FAKE_ERR;
	run_stage(&f, &r);
	assert(r.status == OB_ACPHY_CAL_WRITE_BRACKET_FAILED);
	assert(r.failure_ret == FAKE_ERR);
	assert(r.writes_issued == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	assert(f.writes == OB_ACPHY_CAL_WRITE_ARM_COUNT);
	assert(r.phyreg_entered == true);
	assert(r.mac_suspended == false);
	assert(r.bracket_completed == false);
	assert(f.bracket_count == 4);
	assert(f.bracket_order[2] == 3);
	assert(f.bracket_order[3] == 4);
	printf("S20 phyreg-exit-failed PASS\n");
}

int main(void)
{
	test_invalid_args();
	test_baseline_arm();
	test_prestate_diverged();
	test_readiness_exhausted();
	test_predicate_true_refused();
	test_second_arm_diverged();
	test_readback_mismatch();
	test_post_state_moved();
	test_write_error();
	test_device_lost();
	test_site_order();
	test_write_budget();
	test_partial_mask_first_arm();
	test_partial_mask_second_arm();
	test_restore_unsupported();
	test_bracket_unsupported();
	test_mac_suspend_failed();
	test_phyreg_enter_failed();
	test_phyreg_exit_failed();
	test_mac_already_suspended();
	printf("ob_acphy_cal_write_stage_test: 21/21 PASS\n");
	return 0;
}

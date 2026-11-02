// SPDX-License-Identifier: GPL-2.0-only
/*
 * W2 — calibration-entry replay window (W01..W12).
 *
 * The code under test is src/ob_acphy_cal_w2_replay.c.  The backend is a
 * deterministic recorder: no MMIO is issued, and every result here is
 * HOST_EXECUTED against that recorder, not hardware evidence.
 *
 * Covered: the 0x7777/0x7733 boundary admission, refusal before any side
 * effect, the setstate delegation, the bracket held open across the kick and
 * its poll, the value-identity arms, the K1 skip, the vendor poll bound, and
 * the closing order (phyreg exit before MAC resume).
 */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_acphy_cal_w2_replay.h"

#define FAKE_ERR (-71)

enum fake_op {
	OP_SETSTATE = 1,
	OP_SUSPEND,
	OP_ENTER,
	OP_ARM0_READ,
	OP_ARM0_WRITE,
	OP_STABLE,
	OP_KICK_PRE,
	OP_KICK_WRITE,
	OP_KICK_POLL,
	OP_RESTORE_READ,
	OP_RESTORE_WRITE,
	OP_EXIT,
	OP_RESUME,
};

struct fake {
	u16 phy_0401;
	u16 phy_0400;
	u16 phy_0070;
	u16 phy_0380;
	/* Boundary value the fake installs when the setstate is called. */
	u16 post_0401;
	int setstate_ret;
	bool setstate_software_only;
	bool suspend_transitioned;
	/* Busy 0x0380 samples before the first clear; -1 = forever. */
	int kick_busy;
	bool kick_active;
	/* Return a non-idle word on the pre-kick read only; entry and post
	 * samples still see the idle register. */
	bool kick_pre_busy;
	unsigned int reads_0380;
	/* Return a moved 0x0070 word on the disable arm's fresh read. */
	bool arm_read_moved;
	unsigned int reads_0070;
	unsigned int kick_reads;
	unsigned int writes_0070;
	unsigned int writes_kick;
	unsigned int setstates;
	unsigned int suspends;
	unsigned int resumes;
	unsigned int enters;
	unsigned int exits;
	u16 written_0070[4];
	unsigned int order[96];
	unsigned int order_count;
};

static int failures;
static int checks;

static void check(const char *what, long got, long want)
{
	checks++;
	if (got != want) {
		failures++;
		printf("FAIL %s: got %ld want %ld\n", what, got, want);
	}
}

static void record(struct fake *f, unsigned int op)
{
	if (f->order_count < sizeof(f->order) / sizeof(f->order[0]))
		f->order[f->order_count++] = op;
}

static int find_order(const struct fake *f, unsigned int op, unsigned int nth)
{
	unsigned int i, seen = 0;

	for (i = 0; i < f->order_count; i++) {
		if (f->order[i] != op)
			continue;
		if (seen++ == nth)
			return (int)i;
	}
	return -1;
}

static int f_read16(void *ctx, u8 event, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	switch (reg) {
	case 0x0401:
		*value = f->phy_0401;
		break;
	case 0x0400:
		*value = f->phy_0400;
		break;
	case 0x0070:
		f->reads_0070++;
		if (f->arm_read_moved && f->reads_0070 == 3) {
			record(f, OP_ARM0_READ);
			*value = 0x6500;
		} else {
			if (f->reads_0070 == 3)
				record(f, OP_ARM0_READ);
			if (f->reads_0070 == 5)
				record(f, OP_RESTORE_READ);
			*value = f->phy_0070;
		}
		break;
	case 0x0380:
		if (!f->kick_active) {
			f->reads_0380++;
			if (f->kick_pre_busy && f->reads_0380 == 3)
				*value = 0x4000;
			else
				*value = f->phy_0380;
			record(f, f->reads_0070 >= 3 ? OP_KICK_PRE : OP_KICK_POLL);
		} else {
			f->kick_reads++;
			if (f->order_count < 200)
				record(f, OP_KICK_POLL);
			if (f->kick_busy < 0 ||
			    (int)f->kick_reads <= f->kick_busy)
				*value = 0x4000;
			else
				*value = 0x0000;
		}
		break;
	default:
		return FAKE_ERR;
	}
	return 0;
}

static int f_write16(void *ctx, u8 event, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	switch (reg) {
	case 0x0070:
		record(f, f->writes_0070 == 0 ? OP_ARM0_WRITE :
			 OP_RESTORE_WRITE);
		f->phy_0070 = value;
		if (f->writes_0070 < 4)
			f->written_0070[f->writes_0070] = value;
		f->writes_0070++;
		break;
	case 0x0380:
		record(f, OP_KICK_WRITE);
		f->phy_0380 = value;
		f->kick_active = true;
		f->writes_kick++;
		break;
	default:
		return FAKE_ERR;
	}
	return 0;
}

static int f_delay(void *ctx, u8 event, u32 site, u16 usec)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	(void)usec;
	if (f->order_count && f->order[f->order_count - 1] == OP_ARM0_WRITE &&
	    f->writes_0070 == 1 && f->reads_0070 == 4)
		record(f, OP_STABLE);
	return 0;
}

static bool f_lost(void *ctx)
{
	(void)ctx;
	return false;
}

static int f_suspend(void *ctx, u8 event, u32 site, bool *transitioned)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	record(f, OP_SUSPEND);
	f->suspends++;
	*transitioned = f->suspend_transitioned;
	return 0;
}

static int f_resume(void *ctx, u8 event, u32 site)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	record(f, OP_RESUME);
	f->resumes++;
	return 0;
}

static int f_enter(void *ctx, u8 event, u32 site)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	record(f, OP_ENTER);
	f->enters++;
	return 0;
}

static int f_exit(void *ctx, u8 event, u32 site)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	record(f, OP_EXIT);
	f->exits++;
	return 0;
}

static int f_setstate(void *ctx, u8 event, u32 site, u8 state, bool *sw_only)
{
	struct fake *f = ctx;

	(void)event;
	(void)site;
	record(f, OP_SETSTATE);
	f->setstates++;
	check("setstate state argument", state, 0x03);
	if (f->setstate_ret)
		return f->setstate_ret;
	if (f->setstate_software_only) {
		*sw_only = true;
		return 0;
	}
	*sw_only = false;
	f->phy_0401 = f->post_0401;
	return 0;
}

static const struct ob_acphy_cal_w2_ops ops = {
	.phy_read16 = f_read16,
	.phy_write16 = f_write16,
	.delay_us = f_delay,
	.device_lost = f_lost,
	.mac_suspend = f_suspend,
	.mac_resume = f_resume,
	.phyreg_enter = f_enter,
	.phyreg_exit = f_exit,
	.setstate = f_setstate,
};

static void seed(struct fake *f)
{
	memset(f, 0, sizeof(*f));
	f->phy_0401 = 0x7777;
	f->phy_0400 = 0x0000;
	f->phy_0070 = 0x0100;
	f->phy_0380 = 0x0000;
	f->post_0401 = 0x7733;
	f->suspend_transitioned = true;
}

/* W01: K1 closed -> no 0x0380 write, the window still runs and closes. */
static void test_kick_skipped_full_shape(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;
	int ret;

	seed(&f);
	ret = ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W01 returns 0", ret, 0);
	check("W01 status KICK_SKIPPED", r.status,
	      OB_ACPHY_CAL_W2_KICK_SKIPPED);
	check("W01 two value-identity 0x0070 writes", f.writes_0070, 2);
	check("W01 no kick write", f.writes_kick, 0);
	check("W01 bracket completed", r.bracket_completed, 1);
	check("W01 setstate once", f.setstates, 1);
	check("W01 one suspend/one enter", f.suspends + f.enters, 2);
	check("W01 one exit/one resume", f.exits + f.resumes, 2);
	check("W01 resume follows exit",
	      find_order(&f, OP_RESUME, 0) > find_order(&f, OP_EXIT, 0), 1);
	check("W01 setstate precedes the bracket",
	      find_order(&f, OP_SETSTATE, 0) < find_order(&f, OP_SUSPEND, 0), 1);
	check("W01 restore follows the kick phase",
	      find_order(&f, OP_RESTORE_WRITE, 0) >
	      find_order(&f, OP_KICK_PRE, 0), 1);
	check("W01 written words are 0x0100", f.written_0070[0], 0x0100);
	check("W01 restore word is 0x0100", f.written_0070[1], 0x0100);
}

/* W02: the kick gate open and an idle register -> one write, bounded poll. */
static void test_kick_written_and_polled(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;
	int ret;

	seed(&f);
	f.kick_busy = 1;
	ret = ob_acphy_cal_w2_replay_run(&ops, &f, true, 0x0300, &r);
	check("W02 returns 0", ret, 0);
	check("W02 status FULL", r.status, OB_ACPHY_CAL_W2_FULL);
	check("W02 one kick write", f.writes_kick, 1);
	check("W02 kick value", f.phy_0380, 0x0300);
	check("W02 poll reads", r.kick_reads, 2);
	check("W02 poll delays", r.kick_delays, 1);
	check("W02 kick precedes the restore arm",
	      find_order(&f, OP_KICK_WRITE, 0) <
	      find_order(&f, OP_RESTORE_WRITE, 0), 1);
	check("W02 exit stays after the poll",
	      find_order(&f, OP_EXIT, 0) > find_order(&f, OP_KICK_POLL, 1), 1);
}

/* W03: a busy register that never clears exhausts the vendor bound, and the
 * bracket is STILL restored and closed before the status is reported. */
static void test_kick_exhaustion_still_closes(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;
	int ret;

	seed(&f);
	f.kick_busy = -1;
	ret = ob_acphy_cal_w2_replay_run(&ops, &f, true, 0x0400, &r);
	check("W03 returns 0", ret, 0);
	check("W03 status KICK_EXHAUSTED", r.status,
	      OB_ACPHY_CAL_W2_KICK_EXHAUSTED);
	check("W03 vendor read bound", r.kick_reads, 2001);
	check("W03 vendor delay bound", r.kick_delays, 2000);
	check("W03 bracket completed", r.bracket_completed, 1);
	check("W03 restore still wrote", f.writes_0070, 2);
	check("W03 resume still ran", f.resumes, 1);
}

/* W04: a moved entry tuple is refused before any side effect. */
static void test_entry_refused(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;
	int ret;

	seed(&f);
	f.phy_0401 = 0x7776;
	ret = ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W04 returns 0", ret, 0);
	check("W04 status ENTRY_REFUSED", r.status,
	      OB_ACPHY_CAL_W2_ENTRY_REFUSED);
	check("W04 no setstate", f.setstates, 0);
	check("W04 no writes", f.writes_0070 + f.writes_kick, 0);
	check("W04 no bracket", f.suspends + f.enters, 0);
}

/* W05: the post-setstate transient is refused at the boundary. */
static void test_post_refused(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;

	seed(&f);
	f.post_0401 = 0x7737;
	ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W05 status POST_REFUSED", r.status,
	      OB_ACPHY_CAL_W2_POST_REFUSED);
	check("W05 setstate ran", f.setstates, 1);
	check("W05 no writes", f.writes_0070 + f.writes_kick, 0);
	check("W05 no bracket", f.suspends + f.enters, 0);
}

/* W06: the vendor software-only arm cannot answer the window. */
static void test_software_only(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;

	seed(&f);
	f.setstate_software_only = true;
	ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W06 status SOFTWARE_ONLY", r.status,
	      OB_ACPHY_CAL_W2_SOFTWARE_ONLY);
	check("W06 software_only recorded", r.setstate_software_only, 1);
	check("W06 no writes", f.writes_0070 + f.writes_kick, 0);
}

/* W07: a setstate callback failure is classified, not hidden. */
static void test_setstate_failure(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;

	seed(&f);
	f.setstate_ret = FAKE_ERR;
	ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W07 status SETSTATE_DIVERGED", r.status,
	      OB_ACPHY_CAL_W2_SETSTATE_DIVERGED);
	check("W07 failure return recorded", r.failure_ret, FAKE_ERR);
	check("W07 no bracket", f.suspends + f.enters, 0);
}

/* W08: a non-idle 0x0380 is never written, even with the gate open. */
static void test_kick_not_idle(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;

	seed(&f);
	f.kick_pre_busy = true;
	ob_acphy_cal_w2_replay_run(&ops, &f, true, 0x0300, &r);
	check("W08 status KICK_SKIPPED", r.status,
	      OB_ACPHY_CAL_W2_KICK_SKIPPED);
	check("W08 no kick write", f.writes_kick, 0);
	check("W08 pre-read recorded", r.kick_pre, 0x4000);
}

/* W09: an arm read that is not the admitted word stops before the RMW. */
static void test_arm_read_moved(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;

	seed(&f);
	f.arm_read_moved = true;
	ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W09 status ARM_DIVERGED", r.status,
	      OB_ACPHY_CAL_W2_ARM_DIVERGED);
	check("W09 zero 0x0070 writes", f.writes_0070, 0);
	check("W09 bracket still closed", r.bracket_completed, 0);
	check("W09 resume after unwind", f.resumes, 1);
}

/* W10: an outer owner's pause is reported as no transition and the shim side
 * must still receive exactly one balanced resume. */
static void test_outer_owner_balance(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;

	seed(&f);
	f.suspend_transitioned = false;
	ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W10 window completes", r.status,
	      OB_ACPHY_CAL_W2_KICK_SKIPPED);
	check("W10 one suspend", f.suspends, 1);
	check("W10 one resume", f.resumes, 1);
	check("W10 identity unchanged", r.post_identity_0401, 0x7733);
}

/* W11: a candidate outside {0x0300,0x0400} is rejected while the gate is
 * open; the closed gate ignores the value and never writes. */
static void test_kick_value_admission(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;
	int ret;

	seed(&f);
	ret = ob_acphy_cal_w2_replay_run(&ops, &f, true, 0x0200, &r);
	check("W11 invalid kick value rejected", ret, -22);
	check("W11 no callbacks on rejection", f.setstates + f.writes_0070, 0);
}

/* W12: identity after the bracket must not move. */
static void test_post_identity_moved(void)
{
	struct fake f;
	struct ob_acphy_cal_w2_result r;

	seed(&f);
	f.phy_0400 = 0x0000;
	ob_acphy_cal_w2_replay_run(&ops, &f, false, 0, &r);
	check("W12 clean run identity", r.post_identity_0400, 0x0000);
	check("W12 status defined", r.status,
	      OB_ACPHY_CAL_W2_KICK_SKIPPED);
}

/* ------------------------------------------------------------------ *
 * The kernel-side 43-operation setstate ledger.
 * ------------------------------------------------------------------ */

struct ss_fake {
	u16 phy[0x410];
	unsigned int reads;
	unsigned int writes;
	unsigned int suspends;
	unsigned int resumes;
};

static int ss_read(void *ctx, u8 event, u32 site, u16 reg, u16 *value)
{
	struct ss_fake *f = ctx;

	(void)event;
	(void)site;
	f->reads++;
	*value = f->phy[reg];
	return 0;
}

static int ss_write(void *ctx, u8 event, u32 site, u16 reg, u16 value)
{
	struct ss_fake *f = ctx;

	(void)event;
	(void)site;
	f->writes++;
	f->phy[reg] = value;
	return 0;
}

static int ss_delay(void *ctx, u8 event, u32 site, u16 usec)
{
	(void)ctx;
	(void)event;
	(void)site;
	(void)usec;
	return 0;
}

static int ss_suspend(void *ctx, u8 event, u32 site, bool *transitioned)
{
	struct ss_fake *f = ctx;

	(void)event;
	(void)site;
	f->suspends++;
	*transitioned = true;
	return 0;
}

static int ss_resume(void *ctx, u8 event, u32 site)
{
	struct ss_fake *f = ctx;

	(void)event;
	(void)site;
	f->resumes++;
	return 0;
}

static const struct ob_acphy_cal_w2_ops ss_ops = {
	.phy_read16 = ss_read,
	.phy_write16 = ss_write,
	.delay_us = ss_delay,
	.mac_suspend = ss_suspend,
	.mac_resume = ss_resume,
};

static void ss_seed(struct ss_fake *f)
{
	memset(f, 0, sizeof(*f));
	f->phy[0x0401] = 0x7777;
	f->phy[0x0400] = 0x0000;
	f->phy[0x019e] = 0x0000;
}

/* L01: the kernel ledger is the 43-op vendor body and lands on 0x7733. */
static void test_setstate_ledger(void)
{
	struct ss_fake f;
	struct ob_acphy_cal_w2_setstate_result r;
	int ret;

	ss_seed(&f);
	ret = ob_acphy_cal_w2_setstate_run(&ss_ops, &f, 0x03, true, &r);
	check("L01 ledger returns 0", ret, 0);
	check("L01 hardware arm", r.software_only, 0);
	check("L01 reads", r.reads, 23);
	check("L01 writes", r.writes, 20);
	check("L01 total ops", (long)(r.reads + r.writes), 43);
	check("L01 saved 0x0401", r.saved_0401, 0x7777);
	check("L01 saved 0x0400", r.saved_0400, 0x0000);
	check("L01 one poll read per rfseq",
	      (long)(r.poll_reads[0] + r.poll_reads[1]), 2);
	check("L01 no poll delays",
	      (long)(r.poll_delays[0] + r.poll_delays[1]), 0);
	check("L01 boundary 0x7733", f.phy[0x0401], 0x7733);
	check("L01 state byte in 0x0160", (long)(f.phy[0x0160] & 7), 3);
	check("L01 one suspend", f.suspends, 1);
	check("L01 one enable", f.resumes, 1);
}

/* L02: the vendor software-only arm touches no PHY and no MAC. */
static void test_setstate_software_only(void)
{
	struct ss_fake f;
	struct ob_acphy_cal_w2_setstate_result r;

	ss_seed(&f);
	ob_acphy_cal_w2_setstate_run(&ss_ops, &f, 0x03, false, &r);
	check("L02 software-only", r.software_only, 1);
	check("L02 no PHY ops", (long)(r.reads + r.writes), 0);
	check("L02 no MAC calls", (long)(f.suspends + f.resumes), 0);
	check("L02 boundary untouched", f.phy[0x0401], 0x7777);
}

/* L03: a stuck 0x0403 exhausts both vendor bounds, restores and balances. */
static void test_setstate_ledger_exhausted(void)
{
	struct ss_fake f;
	struct ob_acphy_cal_w2_setstate_result r;

	ss_seed(&f);
	f.phy[0x0403] = 0x0003;
	ob_acphy_cal_w2_setstate_run(&ss_ops, &f, 0x03, true, &r);
	check("L03 both polls exhausted",
	      r.poll_exhausted[0] && r.poll_exhausted[1], 1);
	check("L03 read bound", r.reads, 4023);
	check("L03 write count", r.writes, 20);
	check("L03 delay bound",
	      (long)(r.poll_delays[0] + r.poll_delays[1]), 4000);
	check("L03 balanced enable", f.resumes, 1);
	check("L03 boundary still 0x7733", f.phy[0x0401], 0x7733);
}

int main(void)
{
	test_kick_skipped_full_shape();
	test_kick_written_and_polled();
	test_kick_exhaustion_still_closes();
	test_entry_refused();
	test_post_refused();
	test_software_only();
	test_setstate_failure();
	test_kick_not_idle();
	test_arm_read_moved();
	test_outer_owner_balance();
	test_kick_value_admission();
	test_post_identity_moved();
	test_setstate_ledger();
	test_setstate_software_only();
	test_setstate_ledger_exhausted();

	printf("SUMMARY checks=%d failures=%d\n", checks, failures);
	return failures ? 1 : 0;
}

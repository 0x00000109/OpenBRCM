// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phase 36T — first bounded initial-calibration transaction (T01..T20).
 *
 * The code under test is the production module src/ob_acphy_cal_run.c.  The
 * backend below is a deterministic recorder: it issues no MMIO, and every
 * result here is HOST_EXECUTED against that recorder, not hardware evidence.
 * Expected traces are the recovered vendor ledger (see
 * audit/openbrcm_phase36t/INITIAL_CALIBRATION_OPERATION_LEDGER.json).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_acphy_cal_run.h"

enum op_kind {
	OP_SHM = 1,
	OP_SUSPEND,
	OP_PHYREG_ENTER,
	OP_TXPWRCTRL,
	OP_RXCORE,
	OP_READ16,
	OP_WRITE16,
	OP_DELAY,
};

struct trace_op {
	enum op_kind kind;
	u8 event;
	u32 site;
	u16 reg;
	u32 value;
};

#define TRACE_MAX 4200u

/* Injected backend error; deliberately distinct from -EIO (-5) so a real
 * device-loss classification cannot be confused with a callback failure. */
#define FAKE_ERR (-71)

struct fake {
	struct trace_op trace[TRACE_MAX];
	unsigned int n;
	/* 1-based index of the callback that returns -EIO; 0 disables. */
	unsigned int fail_at;
	/* Device loss becomes visible once this many callbacks have run. */
	unsigned int lost_at;
	/* Poll script: number of "busy" reads before the clear sample. */
	int busy_reads;
	u16 busy_value;
	u16 clear_value;
	unsigned int last_event;
};

static void trace_push(struct fake *f, enum op_kind kind, u8 event, u32 site,
		       u16 reg, u32 value)
{
	assert(f->n < TRACE_MAX);
	f->trace[f->n].kind = kind;
	f->trace[f->n].event = event;
	f->trace[f->n].site = site;
	f->trace[f->n].reg = reg;
	f->trace[f->n].value = value;
	f->n++;
}

static int fake_fail(struct fake *f)
{
	return (f->fail_at && f->n == f->fail_at) ? FAKE_ERR : 0;
}

static int f_shm(void *ctx, u8 ev, u32 site, u16 off, u32 value)
{
	struct fake *f = ctx;

	trace_push(f, OP_SHM, ev, site, off, value);
	return fake_fail(f);
}

static int f_suspend(void *ctx, u8 ev, u32 site)
{
	struct fake *f = ctx;

	trace_push(f, OP_SUSPEND, ev, site, 0, 0);
	return fake_fail(f);
}

static int f_phyreg_enter(void *ctx, u8 ev, u32 site)
{
	struct fake *f = ctx;

	trace_push(f, OP_PHYREG_ENTER, ev, site, 0, 0);
	return fake_fail(f);
}

static int f_txpwrctrl(void *ctx, u8 ev, u32 site, u8 enable)
{
	struct fake *f = ctx;

	trace_push(f, OP_TXPWRCTRL, ev, site, 0, enable);
	return fake_fail(f);
}

static int f_rxcore(void *ctx, u8 ev, u32 site, u8 state)
{
	struct fake *f = ctx;

	trace_push(f, OP_RXCORE, ev, site, 0, state);
	return fake_fail(f);
}

static int f_read16(void *ctx, u8 ev, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	unsigned int reads = 0;
	unsigned int i;
	int ret;

	trace_push(f, OP_READ16, ev, site, reg, reg);
	ret = fake_fail(f);
	if (ret)
		return ret;
	for (i = 0; i < f->n; i++)
		if (f->trace[i].kind == OP_READ16)
			reads++;
	/* Scripted PHY[0x380] samples: busy for busy_reads, then clear. */
	if (reg == OB_ACPHY_CAL_POLL_REG) {
		if (f->busy_reads < 0)
			*value = f->busy_value;
		else if ((int)reads <= f->busy_reads)
			*value = f->busy_value;
		else
			*value = f->clear_value;
	} else {
		*value = 0;
	}
	return 0;
}

static int f_write16(void *ctx, u8 ev, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;

	trace_push(f, OP_WRITE16, ev, site, reg, value);
	return fake_fail(f);
}

static int f_delay(void *ctx, u8 ev, u32 site, u16 usec)
{
	struct fake *f = ctx;

	trace_push(f, OP_DELAY, ev, site, 0, usec);
	return fake_fail(f);
}

static bool f_lost(void *ctx)
{
	struct fake *f = ctx;

	return f->lost_at && f->n >= f->lost_at;
}

static struct fake fake_init(void)
{
	struct fake f;

	memset(&f, 0, sizeof(f));
	/* Default script: both status bits clear on the first read. */
	f.busy_reads = 0;
	f.busy_value = 0xc000;
	f.clear_value = 0x0000;
	return f;
}

static const struct ob_acphy_cal_ops cal_ops = {
	.bmac_write_shm = f_shm,
	.suspend_mac_and_wait = f_suspend,
	.phyreg_enter = f_phyreg_enter,
	.txpwrctrl_enable = f_txpwrctrl,
	.phy_read16 = f_read16,
	.phy_write16 = f_write16,
	.delay_us = f_delay,
	.rxcore_setstate = f_rxcore,
	.device_lost = f_lost,
};

static struct ob_phy_lpf_target supported_target(void)
{
	return (struct ob_phy_lpf_target) {
		.identity_valid = true,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.d11_core_id = 0x812,
		.d11_revision = 42,
		.phy_type = 0x0b,
		.phy_revision = 1,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_revision_class = 0,
	};
}

static struct ob_acphy_cal_input supported_input(void)
{
	return (struct ob_acphy_cal_input) {
		.predecessor_ready = true,
		.chanspec_valid = true,
		.chanspec = 0x1001,
		.pi_19c = 0,
		.pi_17e = 0x1001,
		/* Set by the selected reason-6 arm before the cal body runs. */
		.pi_f84 = true,
		.shared_34 = 0x00010203u,
		.common_a4 = 0x11,
		.common_a5 = 0x07,
		.common_a6 = 0x22,
		.common_a7 = 0x33,
	};
}


/* Literal vendor anchors, deliberately not taken from the header macros, so a
 * wrong constant in src/ob_acphy_cal_run.h cannot silently agree with the
 * expectation. */
static void expect_poll_trace(const struct fake *f, unsigned int reads,
			      unsigned int delays)
{
	unsigned int i, r = 0, d = 0;

	for (i = 0; i < f->n; i++) {
		if (f->trace[i].kind == OP_READ16) {
			assert(f->trace[i].reg == 0x0380);
			assert(f->trace[i].site == 0x0ad442u);
			r++;
		} else {
			assert(f->trace[i].kind == OP_DELAY);
			assert(f->trace[i].site == 0x0ad436u);
			assert(f->trace[i].value == 10);
			d++;
		}
	}
	assert(r == reads);
	assert(d == delays);
}

/* Exact expected order of the recovered entry block. */
static void expect_entry_block(const struct fake *f)
{
	assert(f->n == 5);
	assert(f->trace[0].kind == OP_RXCORE);
	assert(f->trace[0].site == OB_ACPHY_CAL_VENDOR_RXCORE_CALL);
	assert(f->trace[0].value == 0x07);	/* common byte a5 */

	assert(f->trace[1].kind == OP_SHM);
	assert(f->trace[1].site == 0x0925e9u);
	assert(f->trace[1].reg == 0x00b8);
	assert(f->trace[1].value == 0x7148);

	assert(f->trace[2].kind == OP_SUSPEND);
	assert(f->trace[2].site == 0x0925f6u);

	assert(f->trace[3].kind == OP_PHYREG_ENTER);
	assert(f->trace[3].site == 0x0925feu);

	assert(f->trace[4].kind == OP_TXPWRCTRL);
	assert(f->trace[4].site == 0x092608u);
	assert(f->trace[4].value == 0);
}

int main(void)
{
	struct ob_phy_lpf_target target = supported_target();
	struct ob_acphy_cal_input in = supported_input();
	struct ob_acphy_cal_result res;
	struct ob_acphy_cal_poll_result poll;
	struct fake fa = fake_init(), fb = fake_init();
	int ret;

	/* T01 — supported BCM4352 target is accepted and runs the block. */
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -95);
	assert(res.status == OB_ACPHY_CAL_STOPPED_TX_TONE);
	assert(res.target_status == OB_PHY_LPF_INPUTS_SUPPORTED);
	assert(res.rxcore_setstate_called);
	assert(res.rxcore_state == 0x07);
	assert(res.callback_count == 5);
	assert(res.logical_event_count == 5);
	assert(res.tail_blocked);
	expect_entry_block(&fa);

	/* The snapshot sources are copied, not transformed. */
	assert(res.snapshot_cal_c0 == 0x00010203u);
	assert(res.snapshot_cal_ba == 0x1001);

	/* Vendor entry rearrangement: a7<-a5, a6<-a4. */
	assert(res.common_a7_after == 0x07);
	assert(res.common_a6_after == 0x11);

	/* pi_f84 clear selects the second recovery boundary, same operations. */
	fa = fake_init();
	in = supported_input();
	in.pi_f84 = false;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -95);
	assert(res.status == OB_ACPHY_CAL_STOPPED_B1227_TAIL);
	assert(res.callback_count == 5);
	expect_entry_block(&fa);
	in = supported_input();

	/* T02 — unsupported chip rejected before any callback. */
	fa = fake_init();
	target = supported_target();
	target.chip_id = 0x4360;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -95);
	assert(res.target_status == OB_PHY_LPF_INPUT_TARGET);
	assert(fa.n == 0);
	assert(res.callback_count == 0);

	/* T03 — unsupported PHY revision rejected before any callback. */
	target = supported_target();
	target.phy_revision = 2;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -95);
	assert(res.target_status == OB_PHY_LPF_INPUT_PI164_BRANCH);
	assert(fa.n == 0);

	/* T04 — unsupported radio revision rejected before any callback. */
	target = supported_target();
	target.radio_revision = 3;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -95);
	assert(res.target_status == OB_PHY_LPF_INPUT_TARGET);
	assert(fa.n == 0);
	target = supported_target();

	/* T05 — missing validated predecessor state is rejected. */
	in.predecessor_ready = false;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -22);
	assert(fa.n == 0);
	in = supported_input();

	/* A chanspec other than the controlled 0x1001 is not admitted. */
	in.chanspec = 0x1002;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -22);
	assert(fa.n == 0);
	in = supported_input();

	/* The vendor entry gate (PI+0x19c bit 0x10) means "do nothing". */
	in.pi_19c = OB_ACPHY_CAL_ENTRY_SKIP_BIT;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -95);
	assert(res.status == OB_ACPHY_CAL_STOPPED_ENTRY_FLAG);
	assert(fa.n == 0);
	in = supported_input();

	/* A missing RX-core owner is a blocker, never a silent skip. */
	{
		struct ob_acphy_cal_ops no_owner = cal_ops;

		no_owner.rxcore_setstate = NULL;
		ret = ob_acphy_cal_run(&no_owner, &fa, &target, &in, &res);
		assert(ret == -95);
		assert(res.status == OB_ACPHY_CAL_STOPPED_NO_RXCORE_OWNER);
		assert(fa.n == 0);
	}

	/* T06 — the normal trace equals the recovered vendor ledger. */
	fa = fake_init();
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -95);
	expect_entry_block(&fa);

	/* T07 — a failed first read stops the poll immediately. */
	fa = fake_init();
	fa.fail_at = 1;
	ret = ob_acphy_cal_poll380_run(&cal_ops, &fa, &poll);
	assert(ret == FAKE_ERR);
	assert(poll.status == OB_ACPHY_CAL_POLL_FAILED_CALLBACK);
	assert(poll.reads == 1);
	assert(poll.delays == 0);
	assert(poll.successor_blocked);
	assert(fa.n == 1);

	/* T08 — a failed first write stops the transaction. */
	fa = fake_init();
	fa.fail_at = 2;		/* the SHM write after the RX-core delegate */
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == FAKE_ERR);
	assert(res.status == OB_ACPHY_CAL_FAILED_CALLBACK);
	assert(res.callback_count == 2);
	assert(res.last_site == 0x0925e9u);
	assert(fa.n == 2);	/* nothing after the failure */
	assert(res.tail_blocked);
	assert(!res.calibration_complete);

	/* T09 — an intermediate operation failure stops the sequence. */
	fa = fake_init();
	fa.fail_at = 3;		/* suspend_mac_and_wait */
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == FAKE_ERR);
	assert(res.callback_count == 3);
	assert(res.last_site == 0x0925f6u);
	assert(fa.n == 3);
	assert(res.snapshot_cal_c0 == 0);	/* snapshots not published */

	/* T10 — the poll succeeds immediately: one read, no delay. */
	fa = fake_init();
	fa.busy_reads = 0;
	ret = ob_acphy_cal_poll380_run(&cal_ops, &fa, &poll);
	assert(ret == 0);
	assert(poll.status == OB_ACPHY_CAL_POLL_CLEARED);
	assert(poll.reads == 1);
	assert(poll.delays == 0);
	assert(!poll.successor_blocked);
	assert(!(poll.last_value & 0xc000));
	expect_poll_trace(&fa, 1, 0);

	/* T11 — the poll succeeds after several iterations. */
	fa = fake_init();
	fa.busy_reads = 4;
	ret = ob_acphy_cal_poll380_run(&cal_ops, &fa, &poll);
	assert(ret == 0);
	assert(poll.reads == 5);
	assert(poll.delays == 4);
	assert(!poll.successor_blocked);
	expect_poll_trace(&fa, 5, 4);

	/* T12 — the finite vendor bound is reached without success. */
	fa = fake_init();
	fa.busy_reads = -1;	/* never clears */
	ret = ob_acphy_cal_poll380_run(&cal_ops, &fa, &poll);
	assert(ret == -110);
	assert(poll.status == OB_ACPHY_CAL_POLL_EXHAUSTED);
	assert(poll.reads == OB_ACPHY_CAL_POLL_MAX_READS);
	assert(poll.delays == OB_ACPHY_CAL_POLL_MAX_DELAYS);
	assert(poll.counter_end == 9);
	assert(poll.successor_blocked);
	expect_poll_trace(&fa, 2001, 2000);

	/* T13 — device loss during polling stops the loop early. */
	fa = fake_init();
	fa.busy_reads = -1;
	fa.lost_at = 50;
	ret = ob_acphy_cal_poll380_run(&cal_ops, &fa, &poll);
	assert(ret == -5);
	assert(poll.status == OB_ACPHY_CAL_POLL_DEVICE_LOST);
	assert(poll.reads < 2001);
	assert(poll.successor_blocked);
	expect_poll_trace(&fa, poll.reads, poll.delays);

	/* T14 — no operation follows a failed poll.  The poll issues reads and
	 * delays only, and the documented successor latch then rejects the
	 * transaction before its first callback. */
	fa = fake_init();
	fa.busy_reads = -1;
	ret = ob_acphy_cal_poll380_run(&cal_ops, &fa, &poll);
	assert(ret == -110 && poll.successor_blocked);
	{
		unsigned int i;

		for (i = 0; i < fa.n; i++)
			assert(fa.trace[i].kind == OP_READ16 ||
			       fa.trace[i].kind == OP_DELAY);
		expect_poll_trace(&fa, 2001, 2000);
	}
	fa = fake_init();
	in.prior_fatal = true;
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == -5);
	assert(ret != 0);
	assert(res.callback_count == 0);
	assert(fa.n == 0);
	in = supported_input();

	/* T15 — no dependent write follows a failed trusted read: a failing
	 * delegate produces no later SHM write or power-control change. */
	fa = fake_init();
	fa.fail_at = 1;		/* the RX-core delegate */
	ret = ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res);
	assert(ret == FAKE_ERR);
	assert(res.callback_count == 1);
	assert(fa.n == 1);
	{
		unsigned int i;

		for (i = 0; i < fa.n; i++)
			assert(fa.trace[i].kind == OP_RXCORE);
	}

	/* T16 — re-entry after a fatal failure is rejected, twice. */
	fa = fake_init();
	in.prior_fatal = true;
	assert(ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res) == -5);
	assert(ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res) == -5);
	assert(fa.n == 0);
	in = supported_input();

	/* T17 — device A failure does not alter device B. */
	fa = fake_init();
	fb = fake_init();
	fa.fail_at = 2;
	assert(ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res) == FAKE_ERR);
	{
		struct ob_acphy_cal_result res_b;
		int ret_b = ob_acphy_cal_run(&cal_ops, &fb, &target, &in, &res_b);

		assert(ret_b == -95);
		assert(res_b.status == OB_ACPHY_CAL_STOPPED_TX_TONE);
		assert(res_b.callback_count == 5);
		expect_entry_block(&fb);
		assert(fa.n == 2);
	}

	/* T18 — no calibration-complete token is published, ever. */
	assert(!res.calibration_complete);
	{
		struct ob_acphy_cal_result r2;

		fa = fake_init();
		assert(ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &r2) == -95);
		assert(!r2.calibration_complete);
		assert(r2.tail_blocked);
	}

	/* T19 — no existing radio/PHY init stream is replayed: the whole
	 * transaction is five operations and touches no PHY register. */
	fa = fake_init();
	assert(ob_acphy_cal_run(&cal_ops, &fa, &target, &in, &res) == -95);
	{
		unsigned int i;

		assert(fa.n == 5);
		for (i = 0; i < fa.n; i++) {
			assert(fa.trace[i].kind != OP_WRITE16);
			assert(fa.trace[i].kind != OP_READ16);
		}
	}

	/* T20 — the production start path stays closed.  This module never
	 * publishes a lifecycle token and refuses its own tail, so no caller can
	 * mistake the entry block for operational readiness. */
	assert(res.status != OB_ACPHY_CAL_INVALID);
	assert(res.tail_blocked);
	assert(!res.calibration_complete);

	/* Invariant: every non-zero transaction return blocks a successor. */
	{
		struct ob_acphy_cal_input bad = supported_input();
		struct ob_phy_lpf_target badt = supported_target();
		struct ob_acphy_cal_result r3;
		int rc;

		bad.prior_fatal = true;
		fa = fake_init();
		rc = ob_acphy_cal_run(&cal_ops, &fa, &target, &bad, &r3);
		assert(rc != 0 && r3.tail_blocked);

		bad = supported_input();
		badt.radio_revision = 3;
		fa = fake_init();
		rc = ob_acphy_cal_run(&cal_ops, &fa, &badt, &bad, &r3);
		assert(rc != 0 && r3.tail_blocked);

		bad = supported_input();
		bad.predecessor_ready = false;
		fa = fake_init();
		rc = ob_acphy_cal_run(&cal_ops, &fa, &target, &bad, &r3);
		assert(rc != 0 && r3.tail_blocked);
	}

	/* NULL argument handling. */
	assert(ob_acphy_cal_run(NULL, &fa, &target, &in, &res) == -22);
	/* ctx is opaque to the runner; only the ops/target/input/result
	 * pointers are validated. */
	assert(ob_acphy_cal_run(&cal_ops, &fa, NULL, &in, &res) == -22);
	assert(ob_acphy_cal_run(&cal_ops, &fa, &target, NULL, &res) == -22);
	assert(ob_acphy_cal_poll380_run(NULL, &fa, &poll) == -22);

	puts("ob_acphy_cal_run_test: PASS");
	return 0;
}

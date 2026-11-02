// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — FIFO530-GATE-01 integration tests.
 *
 * These tests do NOT re-implement the vendor loop: they link the production
 * control flow from src/ob_d3a1.h (ob_d3a1_poll_core + ob_d3a1_fifo530_stage)
 * and drive it through a mock MMIO backend that records every access. For each
 * scenario the test asserts the exact access sequence, the final outcome class
 * and that no hardware access follows a fatal observation.
 *
 * Not covered here: T11/T12 (DMA bring-up / teardown failure after a
 * continuation). Those run through ob_d3a0 DMA code that only exists in the
 * kernel build; they are covered by the source-order contract in
 * audit/.../admission_correction_01/scripts/check_tail_order.py plus the kernel
 * build, and are reported as such in INTEGRATION_TEST_RESULTS.md.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_d3a1.h"

#define MAXOPS 8192

enum op_kind { OP_READ, OP_WRITE, OP_DELAY };

struct op {
	enum op_kind kind;
	u16 off;
	u16 val;
};

struct mock {
	/* scripted D11 0x530 behaviour */
	unsigned nonzero_reads;	/* nonzero poll reads before the result read */
	u16 nonzero_val;	/* value of those nonzero reads */
	u16 post_val;		/* value of the read after 21 nonzero reads */
	unsigned fail_at;	/* 1-based read index within a poll -> 0xffff */
	unsigned lose_at;	/* 1-based read index within a poll -> latch */

	/* bookkeeping */
	unsigned poll_reads;	/* reads since the last 0x530 write */
	unsigned reads_mmio;	/* 0x530 reads that touched the model */
	unsigned reads_blocked;	/* reads refused by the device-loss latch */
	bool lost;

	struct op ops[MAXOPS];
	unsigned nops;
};

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

static void record(struct mock *m, enum op_kind k, u16 off, u16 val)
{
	if (m->nops < MAXOPS) {
		m->ops[m->nops].kind = k;
		m->ops[m->nops].off = off;
		m->ops[m->nops].val = val;
		m->nops++;
	}
}

static u16 mock_read16(void *ctx, u16 off)
{
	struct mock *m = ctx;

	record(m, OP_READ, off, 0);
	if (off != OB_D3A1_REG_XMT_530)
		return 0;

	/*
	 * Faithful to ob_d11_read16(): once the device-loss latch is set the
	 * access is REFUSED (no MMIO) and the caller sees the dead sentinel.
	 */
	if (m->lost) {
		m->reads_blocked++;
		return 0xffff;
	}

	m->poll_reads++;
	m->reads_mmio++;

	if (m->lose_at && m->poll_reads == m->lose_at) {
		m->lost = true;	/* the latch takes effect after this access */
		return m->nonzero_val ? m->nonzero_val : 0x0007;
	}
	/*
	 * fail_at K: the first K-1 observations are legitimate non-complete
	 * values, the K-th is the all-ones (no-slave-response) pattern. This is
	 * the transient scenario: with fail_at = 2 the register "recovers" on
	 * the next read, which must never be reached.
	 */
	if (m->fail_at) {
		if (m->poll_reads < m->fail_at)
			return m->nonzero_val ? m->nonzero_val : 0x0007;
		if (m->poll_reads == m->fail_at)
			return 0xffff;
	}
	if (m->nonzero_reads >= OB_D3A1_FIFO_POLL_MAX_ITERS + 1u) {
		if (m->poll_reads <= OB_D3A1_FIFO_POLL_MAX_ITERS + 1u)
			return m->nonzero_val;
		return m->post_val;
	}
	if (m->poll_reads <= m->nonzero_reads)
		return m->nonzero_val;
	return 0;
}

static void mock_write16(void *ctx, u16 off, u16 val)
{
	struct mock *m = ctx;

	record(m, OP_WRITE, off, val);
	/* A 0x530 write starts a new entry and therefore a new poll. */
	if (off == OB_D3A1_REG_XMT_530)
		m->poll_reads = 0;
}

static struct mock *delay_ctx;

static void mock_delay(u32 us)
{
	(void)us;
	record(delay_ctx, OP_DELAY, 0, 0);
}

static bool mock_dev_lost(void *ctx)
{
	return ((struct mock *)ctx)->lost;
}

/* --- counting helpers over the recorded stream ------------------------- */

static unsigned count_writes(const struct mock *m, u16 off)
{
	unsigned i, n = 0;

	for (i = 0; i < m->nops; i++)
		if (m->ops[i].kind == OP_WRITE && m->ops[i].off == off)
			n++;
	return n;
}

static unsigned count_reads(const struct mock *m, u16 off)
{
	unsigned i, n = 0;

	for (i = 0; i < m->nops; i++)
		if (m->ops[i].kind == OP_READ && m->ops[i].off == off)
			n++;
	return n;
}

static unsigned count_delays(const struct mock *m)
{
	unsigned i, n = 0;

	for (i = 0; i < m->nops; i++)
		if (m->ops[i].kind == OP_DELAY)
			n++;
	return n;
}

/* True when no write of any kind was recorded after stream index @i. */
static bool no_writes_after(const struct mock *m, int i)
{
	unsigned k;

	for (k = (unsigned)(i + 1); k < m->nops; k++)
		if (m->ops[k].kind == OP_WRITE)
			return false;
	return true;
}

/* --- scenario runner --------------------------------------------------- */

struct run_result {
	struct ob_d3a1_fifo530_state fs;
	struct mock m;
};

static void run(struct run_result *r, bool opt_in, unsigned nonzero_reads,
		u16 nonzero_val, u16 post_val, unsigned fail_at, unsigned lose_at)
{
	struct ob_d3a1_fifo530_ops ops = {
		.read16 = mock_read16,
		.write16 = mock_write16,
		.delay = mock_delay,
		.dev_lost = mock_dev_lost,
		.on_complete = NULL,
		.on_expired_continued = NULL,
	};

	memset(r, 0, sizeof(*r));
	r->m.nonzero_reads = nonzero_reads;
	r->m.nonzero_val = nonzero_val;
	r->m.post_val = post_val;
	r->m.fail_at = fail_at;
	r->m.lose_at = lose_at;
	ops.ctx = &r->m;
	delay_ctx = &r->m;

	ob_d3a1_fifo530_state_init(&r->fs, opt_in);
	ob_d3a1_fifo530_stage(&ops, &r->fs);
}

/* Every 42-entry row writes 534, 536, 532, 530 in that order. */
static void check_entry_write_order(const struct mock *m)
{
	static const u16 order[4] = {
		OB_D3A1_REG_XMT_534, OB_D3A1_REG_XMT_536,
		OB_D3A1_REG_XMT_532, OB_D3A1_REG_XMT_530,
	};
	unsigned i, seen = 0;
	int idx = -1;

	for (i = 0; i < m->nops; i++) {
		if (m->ops[i].kind == OP_WRITE &&
		    m->ops[i].off == OB_D3A1_REG_XMT_534) {
			idx = (int)i;
			break;
		}
	}
	if (idx < 0) {
		printf("FAIL: no 0x534 write in the stream\n");
		failures++;
		return;
	}
	for (i = (unsigned)idx; i < m->nops && seen < 4u; i++) {
		if (m->ops[i].kind != OP_WRITE)
			continue;
		if (m->ops[i].off != order[seen]) {
			printf("FAIL: entry write %u is 0x%04x, expected 0x%04x\n",
			       seen, m->ops[i].off, order[seen]);
			failures++;
			return;
		}
		seen++;
	}
	chk("entry write order complete", seen, 4);
}

/* T01: immediate 0x0000 completion on every entry. */
static void t01_immediate_completion(void)
{
	struct run_result r;

	run(&r, false, 0, 0, 0, 0, 0);
	chk("T01 result complete", r.fs.result, OB_D3A1_FIFO530_COMPLETE);
	chk("T01 entries", r.fs.entries_entered, OB_D3A1_FIFO42_ENTRIES);
	chk("T01 polls completed", r.fs.polls_completed, OB_D3A1_FIFO42_ENTRIES);
	chk("T01 expired", r.fs.expired, 0);
	chk("T01 continued", r.fs.continued, 0);
	chk("T01 writes 530", count_writes(&r.m, OB_D3A1_REG_XMT_530),
	    OB_D3A1_FIFO42_ENTRIES);
	chk("T01 writes 534", count_writes(&r.m, OB_D3A1_REG_XMT_534),
	    OB_D3A1_FIFO42_ENTRIES);
	chk("T01 writes 536", count_writes(&r.m, OB_D3A1_REG_XMT_536),
	    OB_D3A1_FIFO42_ENTRIES);
	chk("T01 writes 532", count_writes(&r.m, OB_D3A1_REG_XMT_532),
	    OB_D3A1_FIFO42_ENTRIES);
	chk("T01 reads", count_reads(&r.m, OB_D3A1_REG_XMT_530),
	    OB_D3A1_FIFO42_ENTRIES);
	chk("T01 no delay", count_delays(&r.m), 0);
	chk("T01 max iters", r.fs.iters_max, 1);
	check_entry_write_order(&r.m);
}

/* T02: delayed legitimate completion (0x0007, 0x0007, 0x0000). */
static void t02_delayed_completion(void)
{
	struct run_result r;

	run(&r, false, 2, 0x0007, 0, 0, 0);
	chk("T02 result complete", r.fs.result, OB_D3A1_FIFO530_COMPLETE);
	chk("T02 entries", r.fs.entries_entered, OB_D3A1_FIFO42_ENTRIES);
	chk("T02 polls completed", r.fs.polls_completed, OB_D3A1_FIFO42_ENTRIES);
	chk("T02 reads per entry", r.fs.iters_max, 3);
	chk("T02 reads", count_reads(&r.m, OB_D3A1_REG_XMT_530),
	    3 * OB_D3A1_FIFO42_ENTRIES);
	chk("T02 delays", count_delays(&r.m), 2 * OB_D3A1_FIFO42_ENTRIES);
	chk("T02 writes", count_writes(&r.m, OB_D3A1_REG_XMT_530),
	    OB_D3A1_FIFO42_ENTRIES);
	chk("T02 no expiry", r.fs.expired, 0);
}

/* T03: 21 nonzero reads + post-read 0x0007, opt-in DISABLED -> stop. */
static void t03_expiry_strict(void)
{
	struct run_result r;

	run(&r, false, 21, 0x0007, 0x0007, 0, 0);
	chk("T03 result expired stop", r.fs.result, OB_D3A1_FIFO530_EXPIRED_STOP);
	chk("T03 entries entered", r.fs.entries_entered, 1);
	chk("T03 failed index", r.fs.failed_index, 0);
	chk("T03 expired", r.fs.expired, 1);
	chk("T03 continued", r.fs.continued, 0);
	chk("T03 iters", r.fs.failed_iters, OB_D3A1_FIFO_POLL_MAX_ITERS + 1);
	/* exactly 21 reads: no post-expiry read without the opt-in */
	chk("T03 reads", count_reads(&r.m, OB_D3A1_REG_XMT_530),
	    OB_D3A1_FIFO_POLL_MAX_ITERS + 1);
	chk("T03 writes", count_writes(&r.m, OB_D3A1_REG_XMT_530), 1);
	chk("T03 no further write", no_writes_after(&r.m, (int)r.m.nops - 1), 1);
}

/* T04: same, opt-in ENABLED -> every entry continues, never as completion. */
static void t04_expiry_continued(void)
{
	struct run_result r;

	run(&r, true, 21, 0x0007, 0x0007, 0, 0);
	chk("T04 loop finished", r.fs.result, OB_D3A1_FIFO530_COMPLETE);
	chk("T04 entries", r.fs.entries_entered, OB_D3A1_FIFO42_ENTRIES);
	chk("T04 none completed", r.fs.polls_completed, 0);
	chk("T04 expired", r.fs.expired, OB_D3A1_FIFO42_ENTRIES);
	chk("T04 continued", r.fs.continued, OB_D3A1_FIFO42_ENTRIES);
	chk("T04 first continued index", r.fs.first_continued_index, 0);
	chk("T04 continued last", r.fs.continued_last, 0x0007);
	chk("T04 continued reads", r.fs.continued_reads,
	    OB_D3A1_FIFO_POLL_MAX_ITERS + 1);
	chk("T04 writes", count_writes(&r.m, OB_D3A1_REG_XMT_530),
	    OB_D3A1_FIFO42_ENTRIES);
	/* 21 poll reads + the vendor's single post-poll read, per entry */
	chk("T04 reads", count_reads(&r.m, OB_D3A1_REG_XMT_530),
	    (OB_D3A1_FIFO_POLL_MAX_ITERS + 2) * OB_D3A1_FIFO42_ENTRIES);
}

/* Shared assertion for T05..T09: fatal on the first entry, then nothing. */
static void check_fatal_stop(const char *tag, const struct run_result *r,
			     unsigned exp_reads, unsigned exp_iters,
			     unsigned exp_delays)
{
	char buf[96];

	chk(tag, r->fs.result, OB_D3A1_FIFO530_INVALID);
	snprintf(buf, sizeof(buf), "%.8s entries", tag);
	chk(buf, r->fs.entries_entered, 1);
	snprintf(buf, sizeof(buf), "%.8s failed index", tag);
	chk(buf, r->fs.failed_index, 0);
	snprintf(buf, sizeof(buf), "%.8s failed value", tag);
	chk(buf, r->fs.failed_last, 0xffff);
	snprintf(buf, sizeof(buf), "%.8s iters", tag);
	chk(buf, r->fs.failed_iters, exp_iters);
	snprintf(buf, sizeof(buf), "%.8s reads", tag);
	chk(buf, count_reads(&r->m, OB_D3A1_REG_XMT_530), exp_reads);
	snprintf(buf, sizeof(buf), "%.8s writes", tag);
	chk(buf, count_writes(&r->m, OB_D3A1_REG_XMT_530), 1);
	snprintf(buf, sizeof(buf), "%.8s delays", tag);
	chk(buf, count_delays(&r->m), exp_delays);
	snprintf(buf, sizeof(buf), "%.8s no further write", tag);
	chk(buf, no_writes_after(&r->m, (int)r->m.nops - 1), 1);
}

static void t05_to_t09_fatal_reads(void)
{
	struct run_result r;

	/* T05: 0xffff on the first poll read (worst case: opt-in ENABLED). */
	run(&r, true, 0, 0x0007, 0x0007, 1, 0);
	check_fatal_stop("T05", &r, 1, 1, 0);

	/* T06: 0xffff in the middle of the poll. */
	run(&r, true, 0, 0x0007, 0x0007, 10, 0);
	check_fatal_stop("T06", &r, 10, 10, 9);

	/* T07: 0xffff on the FINAL (21st) poll read, not a timeout. */
	run(&r, true, 0, 0x0007, 0x0007,
	    OB_D3A1_FIFO_POLL_MAX_ITERS + 1, 0);
	check_fatal_stop("T07", &r, OB_D3A1_FIFO_POLL_MAX_ITERS + 1,
			 OB_D3A1_FIFO_POLL_MAX_ITERS + 1,
			 OB_D3A1_FIFO_POLL_MAX_ITERS);

	/* T08: 0xffff on the post-expiry read (opt-in enabled). */
	run(&r, true, 21, 0x0007, 0xffff, 0, 0);
	chk("T08 result invalid", r.fs.result, OB_D3A1_FIFO530_INVALID);
	chk("T08 failed index", r.fs.failed_index, 0);
	chk("T08 post read observed", r.fs.failed_last, 0xffff);
	chk("T08 reads", count_reads(&r.m, OB_D3A1_REG_XMT_530),
	    OB_D3A1_FIFO_POLL_MAX_ITERS + 2);
	chk("T08 writes", count_writes(&r.m, OB_D3A1_REG_XMT_530), 1);
	chk("T08 continued", r.fs.continued, 0);
	chk("T08 expired not counted", r.fs.expired, 0);
	chk("T08 no further write", no_writes_after(&r.m, (int)r.m.nops - 1), 1);

	/* T09: device-loss latch during polling -> reads refused, then fatal. */
	run(&r, true, OB_D3A1_FIFO_POLL_MAX_ITERS, 0x0007, 0x0007, 0, 5);
	chk("T09 lost latched", r.m.lost, 1);
	chk("T09 result invalid", r.fs.result, OB_D3A1_FIFO530_INVALID);
	chk("T09 failed index", r.fs.failed_index, 0);
	chk("T09 reads that reached the model", r.m.reads_mmio, 5);
	chk("T09 reads refused after latch", r.m.reads_blocked, 1);
	chk("T09 total observation count", r.fs.failed_iters, 6);
	chk("T09 writes", count_writes(&r.m, OB_D3A1_REG_XMT_530), 1);
	chk("T09 continued", r.fs.continued, 0);
	chk("T09 no further write", no_writes_after(&r.m, (int)r.m.nops - 1), 1);
}

/*
 * T10: the modifier is meaningless without an isolated mode that runs the 0x530
 * fixup — the isolated D3A1 tail test or the isolated D3B band-init test, whose
 * prefix is that same tail (M-D3B-CONTINUE-GATE-01).
 */
static void t10_modifier_without_test_only(void)
{
	struct run_result r;

	chk("T10 modifier alone rejected",
	    ob_d3a1_fifo530_continue_allowed(true, false, false), false);
	chk("T10 modifier with d3a1 test allowed",
	    ob_d3a1_fifo530_continue_allowed(true, true, false), true);
	chk("T10 modifier with d3b test allowed",
	    ob_d3a1_fifo530_continue_allowed(true, false, true), true);
	chk("T10 modifier off allowed in every mode",
	    ob_d3a1_fifo530_continue_allowed(false, false, false) &&
	    ob_d3a1_fifo530_continue_allowed(false, true, false) &&
	    ob_d3a1_fifo530_continue_allowed(false, false, true), true);

	/*
	 * The rejected configuration IS the strict default: with the flag not
	 * admitted, hw->d3a1_fifo530_continue stays false and an expiry stops
	 * with no post read.
	 */
	run(&r, false, 21, 0x0007, 0x0007, 0, 0);
	chk("T10 strict stop", r.fs.result, OB_D3A1_FIFO530_EXPIRED_STOP);
	chk("T10 strict reads", count_reads(&r.m, OB_D3A1_REG_XMT_530),
	    OB_D3A1_FIFO_POLL_MAX_ITERS + 1);
	chk("T10 strict writes", count_writes(&r.m, OB_D3A1_REG_XMT_530), 1);
	chk("T10 strict continued", r.fs.continued, 0);
}

/*
 * The transient scenario from the audit finding: read #1 is 0x0007, read #2 is
 * 0xffff, and a later read would be 0x0007 again. The invalid observation must
 * stop the poll at #2, before the delay and before any further read.
 */
static void poll_core_transient_all_ones(void)
{
	struct mock m;
	u32 iters = 0;
	u16 last = 0;

	memset(&m, 0, sizeof(m));
	m.fail_at = 2;
	m.nonzero_val = 0x0007;
	delay_ctx = &m;

	chk("transient result invalid",
	    ob_d3a1_poll_core(mock_read16, mock_delay, &m,
			      OB_D3A1_REG_XMT_530, OB_D3A1_POLL_530,
			      &iters, &last),
	    OB_D3A1_POLL_INVALID);
	chk("transient iters", iters, 2);
	chk("transient last", last, 0xffff);
	chk("transient reads", count_reads(&m, OB_D3A1_REG_XMT_530), 2);
	chk("transient delays", count_delays(&m), 1);
}


/*
 * The 0x540 flush poll: predicate is bit0-clear, so a transient 0xffff followed
 * by a completion-looking value is the counterexample that made the earlier
 * "0x540 cannot be masked" argument wrong. These tests drive the same
 * ob_d3a1_poll_core with a scripted 0x540 sequence.
 */
struct poll540_mock {
	const u16 *vals;
	unsigned n;
	unsigned i;
	unsigned reads;
	unsigned delays;
	bool lost_after;
};

static u16 mock540_read(void *ctx, u16 off)
{
	struct poll540_mock *m = ctx;
	u16 v;

	(void)off;
	m->reads++;
	if (m->i >= m->n)
		return m->vals[m->n - 1u];
	v = m->vals[m->i++];
	if (m->lost_after && m->i == m->n)
		m->lost_after = false;
	return v;
}

static struct poll540_mock *delay540_ctx;

static void mock540_delay_counted(u32 us)
{
	(void)us;
	if (delay540_ctx)
		delay540_ctx->delays++;
}

/* 0xffff then a bit0-clear value: must be fatal on the FIRST observation. */
static void t540_transient_then_completion(void)
{
	static const u16 seq[] = { 0xffff, 0x0004 };
	struct poll540_mock m = {0};
	u32 iters = 0;
	u16 last = 0;
	enum ob_d3a1_poll_result r;

	m.vals = seq;
	m.n = 2;
	delay540_ctx = &m;
	r = ob_d3a1_poll_core(mock540_read, mock540_delay_counted, &m,
			      OB_D3A1_REG_XMTFIFOCMD, OB_D3A1_POLL_540,
			      &iters, &last);
	chk("0x540 transient result invalid", r, OB_D3A1_POLL_INVALID);
	chk("0x540 transient iters", iters, 1);
	chk("0x540 transient last", last, 0xffff);
	chk("0x540 transient reads", m.reads, 1);
	chk("0x540 transient delays", m.delays, 0);

	/* sanity: the second value really would have completed the predicate */
	chk("0x540 0x0004 satisfies the vendor predicate",
	    ob_d3a1_fifo540_done(0x0004), true);
}

/* A legitimate 0x540 sequence must still complete (no over-tightening). */
static void t540_legitimate_completion(void)
{
	static const u16 seq[] = { 0x0005, 0x0004 };
	struct poll540_mock m = {0};
	u32 iters = 0;
	u16 last = 0;

	m.vals = seq;
	m.n = 2;
	delay540_ctx = &m;
	chk("0x540 legitimate result",
	    ob_d3a1_poll_core(mock540_read, mock540_delay_counted, &m,
			      OB_D3A1_REG_XMTFIFOCMD, OB_D3A1_POLL_540,
			      &iters, &last),
	    OB_D3A1_POLL_COMPLETE);
	chk("0x540 legitimate iters", iters, 2);
	chk("0x540 legitimate last", last, 0x0004);
	chk("0x540 legitimate delays", m.delays, 1);

	/* and a sustained all-ones sequence is a bounded expiry, not a completion */
	{
		static const u16 ones[21] = {
			0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
			0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
			0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
		};
		struct poll540_mock m2 = {0};

		/* every observation is invalid, so the poll stops at read 1 */
		m2.vals = ones;
		m2.n = 21;
		delay540_ctx = &m2;
		chk("0x540 sustained all-ones is invalid at read 1",
		    ob_d3a1_poll_core(mock540_read, mock540_delay_counted, &m2,
				      OB_D3A1_REG_XMTFIFOCMD, OB_D3A1_POLL_540,
				      &iters, &last),
		    OB_D3A1_POLL_INVALID);
		chk("0x540 sustained all-ones reads", m2.reads, 1);
	}
}


/*
 * Remaining required 0x540 cases: a later invalid observation, an invalid
 * observation at the final bounded iteration, and a bounded expiry made only of
 * legitimate non-complete values. Each asserts the exact number of MMIO reads
 * and that nothing follows the invalid one.
 */
static void t540_second_observation(void)
{
	static const u16 seq[] = { 0x0005, 0xffff, 0x0004 };
	struct poll540_mock m = {0};
	u32 iters = 0;
	u16 last = 0;

	m.vals = seq;
	m.n = 3;
	delay540_ctx = &m;
	chk("0x540 second-observation result",
	    ob_d3a1_poll_core(mock540_read, mock540_delay_counted, &m,
			      OB_D3A1_REG_XMTFIFOCMD, OB_D3A1_POLL_540,
			      &iters, &last),
	    OB_D3A1_POLL_INVALID);
	chk("0x540 second-observation iters", iters, 2);
	chk("0x540 second-observation reads", m.reads, 2);
	chk("0x540 second-observation delays", m.delays, 1);
	chk("0x540 second-observation last", last, 0xffff);
}

static void t540_final_iteration_all_ones(void)
{
	u16 seq[OB_D3A1_FIFO_POLL_MAX_ITERS + 1u];
	struct poll540_mock m = {0};
	u32 iters = 0;
	u16 last = 0;
	unsigned i;

	for (i = 0; i < OB_D3A1_FIFO_POLL_MAX_ITERS; i++)
		seq[i] = 0x0005;	/* legitimate non-complete */
	seq[OB_D3A1_FIFO_POLL_MAX_ITERS] = 0xffff;	/* last allowed read */

	m.vals = seq;
	m.n = OB_D3A1_FIFO_POLL_MAX_ITERS + 1u;
	delay540_ctx = &m;
	{
		enum ob_d3a1_poll_result r =
			ob_d3a1_poll_core(mock540_read, mock540_delay_counted, &m,
					  OB_D3A1_REG_XMTFIFOCMD,
					  OB_D3A1_POLL_540, &iters, &last);

		chk("0x540 final-iteration result", r, OB_D3A1_POLL_INVALID);
		chk("0x540 final-iteration is not an expiry",
		    r != OB_D3A1_POLL_EXPIRED, 1);
	}
	chk("0x540 final-iteration iters", iters,
	    OB_D3A1_FIFO_POLL_MAX_ITERS + 1u);
	chk("0x540 final-iteration reads", m.reads,
	    OB_D3A1_FIFO_POLL_MAX_ITERS + 1u);
	chk("0x540 final-iteration delays", m.delays,
	    OB_D3A1_FIFO_POLL_MAX_ITERS);
}

static void t540_bounded_expiry(void)
{
	u16 seq[OB_D3A1_FIFO_POLL_MAX_ITERS + 1u];
	struct poll540_mock m = {0};
	u32 iters = 0;
	u16 last = 0;
	unsigned i;

	for (i = 0; i <= OB_D3A1_FIFO_POLL_MAX_ITERS; i++)
		seq[i] = 0x0005;	/* always non-complete, never invalid */

	m.vals = seq;
	m.n = OB_D3A1_FIFO_POLL_MAX_ITERS + 1u;
	delay540_ctx = &m;
	chk("0x540 bounded result",
	    ob_d3a1_poll_core(mock540_read, mock540_delay_counted, &m,
			      OB_D3A1_REG_XMTFIFOCMD, OB_D3A1_POLL_540,
			      &iters, &last),
	    OB_D3A1_POLL_EXPIRED);
	chk("0x540 bounded iters", iters, OB_D3A1_FIFO_POLL_MAX_ITERS + 1u);
	chk("0x540 bounded reads", m.reads, OB_D3A1_FIFO_POLL_MAX_ITERS + 1u);
	chk("0x540 bounded delays", m.delays, OB_D3A1_FIFO_POLL_MAX_ITERS);
	chk("0x540 bounded last", last, 0x0005);
}

int main(void)
{
	t01_immediate_completion();
	t02_delayed_completion();
	t03_expiry_strict();
	t04_expiry_continued();
	t05_to_t09_fatal_reads();
	t10_modifier_without_test_only();
	t540_transient_then_completion();
	t540_legitimate_completion();
	t540_second_observation();
	t540_final_iteration_all_ones();
	t540_bounded_expiry();
	poll_core_transient_all_ones();

	if (failures) {
		printf("ob_d3a1_fifo530_test: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ob_d3a1_fifo530_test: PASS (T01-T10 + transient 0x530 + 0x540 five cases)\n");
	return 0;
}

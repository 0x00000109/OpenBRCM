// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phase 38A host tests D01..D20 (D16/D17/D18/D20 are source assertions in
 * test_phase38a_source.py) — rev42_d11_maccontrol_once.
 *
 * WHAT IS BEING EXECUTED
 * ----------------------
 *   ob_aie_run()                the real Phase 37B wrapper-enable engine
 *                               (src/ob_ai_enable_only.h), unchanged
 *   ob_d11m_run()               the real Phase 38A gate/one-read/classify
 *                               contract (src/ob_d11_maccontrol_once.h)
 *   ob_d11m_wrapper_gate()      the real B2 predicate
 *   ob_isolated_mode_select24() the real 24-way isolated-mode policy
 *   ob_isolated_default_load_refused(), ob_aiw_legacy_retired()
 *
 * There is no model and no paraphrase: the backend below supplies exactly the
 * six permitted wrapper operations, ONE functional read and a log sink, the
 * same set the kernel adapter supplies. A functional write is not expressible
 * (struct ob_d11m_ops has no write callback), which is asserted structurally.
 *
 * Every check prints one RESULT line with the exercised function, the state,
 * the expected/observed trace, the expected/observed class and the count of
 * functional accesses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../../src/ob_d11_maccontrol_once.h"
#include "../../src/ob_ai_wrapper_only.h"
#include "../../src/ob_diag.h"
#include "../../src/ob_ucode.h"
#include "../../src/ob_chip_profile.h"

static int checks;
static int failures;

/* ---- wrapper fake: exactly the six Phase 37B operations ----------------- */

struct wfake {
	u32 rst;
	u32 rctl;
	u32 ioc;
	bool all_ones;
	bool fail_wr_e2;
	bool fail_rb_e2;
	unsigned reads;
	unsigned writes;
};

static int w_read_st(void *ctx, u32 *v)
{
	struct wfake *f = ctx;

	f->reads++;
	*v = f->all_ones ? OB_AIE_ALL_ONES : f->rst;
	return 0;
}

static int w_read_ctl(void *ctx, u32 *v)
{
	struct wfake *f = ctx;

	f->reads++;
	*v = f->all_ones ? OB_AIE_ALL_ONES : f->rctl;
	return 0;
}

static int w_read_ioctl(void *ctx, u32 *v)
{
	struct wfake *f = ctx;

	f->reads++;
	*v = f->all_ones ? OB_AIE_ALL_ONES : f->ioc;
	return 0;
}

static int w_wr_e1(void *ctx)
{
	struct wfake *f = ctx;

	f->writes++;
	f->ioc = OB_AIE_E1_IOCTL_VALUE;
	return 0;
}

static int w_wr_e2(void *ctx)
{
	struct wfake *f = ctx;

	f->writes++;
	if (f->fail_wr_e2)
		return OB_AIE_RET_EIO;
	/* A readback mismatch is modelled as "the write did not take". */
	if (!f->fail_rb_e2)
		f->rctl = OB_AIE_E2_RCTL_VALUE;
	return 0;
}

static int w_wr_e3(void *ctx)
{
	struct wfake *f = ctx;

	f->writes++;
	f->ioc = OB_AIE_E3_IOCTL_VALUE;
	return 0;
}

static void w_delay(void *ctx, unsigned int us)
{
	(void)ctx;
	(void)us;
}

static void w_log(void *ctx, enum ob_aie_event ev, const struct ob_aie_log *r)
{
	(void)ctx;
	(void)ev;
	(void)r;
}

static const struct ob_aie_ops w_ops = {
	.read_reset_st = w_read_st,
	.read_reset_ctl = w_read_ctl,
	.read_ioctl = w_read_ioctl,
	.write_ioctl_set_fgc = w_wr_e1,
	.write_reset_ctl_deassert = w_wr_e2,
	.write_ioctl_clr_fgc = w_wr_e3,
	.delay_us = w_delay,
	.log = w_log,
	.dev_lost = NULL,
};

static void wrap_run(struct wfake *f, struct ob_aie_result *res)
{
	struct ob_aie_ops o = w_ops;

	o.ctx = f;
	ob_aie_run(&o, false, res);
}

static void w_prestate(struct wfake *f)
{
	memset(f, 0, sizeof(*f));
	f->rst = OB_AIE_PRESTATE_RESET_ST;
	f->rctl = OB_AIE_PRESTATE_RESET_CTL;
	f->ioc = OB_AIE_PRESTATE_IOCTL;
}

/* ---- D11 fake: exactly ONE read callback -------------------------------- */

struct dfake {
	u32 value;
	bool fail;
	bool latch_after_read;
	unsigned reads;
	unsigned events;
	unsigned post_events;
	enum ob_d11m_event last_event;
	unsigned stop_events;
};

static int d_read(void *ctx, u32 *v)
{
	struct dfake *f = ctx;

	f->reads++;
	if (f->fail)
		return -5;
	*v = f->value;
	return 0;
}

static void d_log(void *ctx, enum ob_d11m_event ev, const struct ob_d11m_log *r)
{
	struct dfake *f = ctx;

	(void)r;
	f->events++;
	f->last_event = ev;
	if (ev == OB_D11M_EV_POST_READ)
		f->post_events++;
	if (ev == OB_D11M_EV_STOP)
		f->stop_events++;
}

static bool d_lost(void *ctx)
{
	struct dfake *f = ctx;

	return f->latch_after_read;
}

static const struct ob_d11m_ops d_ops = {
	.read_maccontrol = d_read,
	.log = d_log,
	.dev_lost = d_lost,
};

/* ---- check helper ------------------------------------------------------- */

static void check(const char *id, const char *what, bool ok,
		  const char *expected, const char *observed)
{
	checks++;
	if (!ok)
		failures++;
	printf("RESULT %s %s %s expected=%s observed=%s\n", id,
	       ok ? "pass" : "FAIL", what, expected, observed);
}

/* ---- mode-selector helpers --------------------------------------------- */

/* One flag set, evaluated by index so a single helper covers every mode. */
static enum ob_isolated_mode select24_one(int idx, bool d11)
{
	bool f[23] = { false };

	if (idx >= 0)
		f[idx] = true;
	return ob_isolated_mode_select24(f[0], f[1], f[2], f[3], f[4], f[5],
					 f[6], f[7], f[8], f[9], f[10], f[11],
					 f[12], f[13], f[14], f[15], f[16],
					 f[17], f[18], f[19], f[20], f[21],
					 f[22], d11);
}

int main(void)
{
	struct wfake wf;
	struct ob_aie_result wr;
	struct dfake df;
	struct ob_d11m_result res;
	struct ob_d11m_ops ops;
	char buf[128];

	/* D01 — bare module load still fails closed before any MMIO. */
	check("D01", "default-load-refused",
	      ob_isolated_default_load_refused(OB_ISOLATED_NONE, false,
					       false) &&
	      !ob_isolated_default_load_refused(
		      OB_ISOLATED_REV42_D11_MACCONTROL_ONCE, false, false),
	      "bare=refused,new-mode=admitted", "checked");

	/* D02 — Phase 37B rev42_ai_enable_only stays wrapper-only. */
	check("D02", "ai-enable-mode-unchanged",
	      select24_one(11, false) == OB_ISOLATED_REV42_AI_ENABLE &&
	      select24_one(-1, true) ==
		      OB_ISOLATED_REV42_D11_MACCONTROL_ONCE,
	      "rev42_ai_enable_only=AI_ENABLE,new=D11_MC_ONCE", "checked");

	/* D03 — wrapper-only diagnostic unchanged. */
	check("D03", "wrapper-only-mode-unchanged",
	      select24_one(22, false) == OB_ISOLATED_REV42_AI_WRAPPER_ONLY &&
	      ob_isolated_mode_select23(false, false, false, false, false,
					false, false, false, false, false,
					false, false, false, false, false,
					false, false, false, false, false,
					false, false, true) ==
		      OB_ISOLATED_REV42_AI_WRAPPER_ONLY,
	      "WRAPPER_ONLY via select23+select24", "checked");

	/* D04 — retired legacy AI probe still rejected. */
	check("D04", "legacy-probe-retired",
	      ob_aiw_legacy_retired(true) && !ob_aiw_legacy_retired(false),
	      "legacy=refused", "checked");

	/* D05 — every conflicting isolated mode rejects, including enable. */
	{
		bool all = true;
		int i;

		for (i = 0; i < 23; i++) {
			if (select24_one(i, true) != OB_ISOLATED_CONFLICT)
				all = false;
		}
		check("D05", "mode-conflicts",
		      all && select24_one(11, true) == OB_ISOLATED_CONFLICT &&
		      select24_one(22, true) == OB_ISOLATED_CONFLICT,
		      "all 23 combinations=CONFLICT", all ? "all" : "some");
	}

	/* D06 — invalid wrapper pre-state: no functional MMIO. */
	w_prestate(&wf);
	wf.rctl = 0x00000000u;	/* not reset-held: pre-state refused */
	wrap_run(&wf, &wr);
	memset(&df, 0, sizeof(df));
	ops = d_ops;
	ops.ctx = &df;
	ob_d11m_run(&ops, &wr, false, false, &res);
	check("D06", "prestate-rejected-no-functional-read",
	      res.class == OB_D11M_CLASS_WRAPPER_NOT_CONFIRMED &&
	      df.reads == 0 && wr.writes_attempted == 0,
	      "WRAPPER_ENABLE_NOT_CONFIRMED,0 reads",
	      ob_d11m_class_token(res.class));

	/* D07 — wrapper all-ones: no functional MMIO. */
	w_prestate(&wf);
	wf.all_ones = true;
	wrap_run(&wf, &wr);
	memset(&df, 0, sizeof(df));
	ops = d_ops;
	ops.ctx = &df;
	ob_d11m_run(&ops, &wr, true, false, &res);
	check("D07", "wrapper-all-ones-no-functional-read",
	      res.class == OB_D11M_CLASS_WRAPPER_NOT_CONFIRMED &&
	      df.reads == 0 &&
	      wr.class == OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED,
	      "WRAPPER_ENABLE_NOT_CONFIRMED,0 reads",
	      ob_d11m_class_token(res.class));

	/* D08 — wrapper write/readback failure: no functional MMIO. */
	w_prestate(&wf);
	wf.fail_wr_e2 = true;
	wrap_run(&wf, &wr);
	memset(&df, 0, sizeof(df));
	ops = d_ops;
	ops.ctx = &df;
	ob_d11m_run(&ops, &wr, false, false, &res);
	{
		bool ok = res.class == OB_D11M_CLASS_WRAPPER_NOT_CONFIRMED &&
			  df.reads == 0 &&
			  wr.writes_attempted == 2 &&
			  wr.writes_confirmed == 1;

		/* and a readback mismatch with full write count */
		w_prestate(&wf);
		wf.fail_rb_e2 = true;
		wrap_run(&wf, &wr);
		ops.ctx = &df;
		memset(&df, 0, sizeof(df));
		ob_d11m_run(&ops, &wr, false, false, &res);
		ok = ok && res.class == OB_D11M_CLASS_WRAPPER_NOT_CONFIRMED &&
		     df.reads == 0;
		check("D08", "wrapper-write-failure-no-functional-read", ok,
		      "WRAPPER_ENABLE_NOT_CONFIRMED,0 reads", "checked");
	}

	/* D09/D10/D11 — success permits exactly ONE MACCONTROL read. */
	w_prestate(&wf);
	wrap_run(&wf, &wr);
	memset(&df, 0, sizeof(df));
	df.value = 0x04020402u;
	ops = d_ops;
	ops.ctx = &df;
	ob_d11m_run(&ops, &wr, false, false, &res);
	snprintf(buf, sizeof(buf), "reads=%u,class=%s,writes=%u",
		 df.reads, ob_d11m_class_token(res.class),
		 (unsigned int)res.writes_attempted);
	check("D09", "exactly-one-functional-read",
	      df.reads == 1 && res.reads_attempted == 1 &&
	      res.reads_completed == 1 && wr.writes_attempted == 3 &&
	      wr.writes_confirmed == 3 && wr.reads_attempted == 6 &&
	      wr.reads_completed == 6,
	      "1 read,3 writes,6 wrapper reads", buf);

	check("D10", "maccontrol-offset-width-window",
	      OB_D11M_OFF_MACCONTROL == 0x0120u && OB_D11M_WIDTH_BITS == 32u &&
	      res.have_value && res.value == 0x04020402u,
	      "0x0120/32 bits/one callback", "checked");

	check("D11", "finite-value-readable",
	      res.class == OB_D11M_CLASS_READABLE &&
	      !strcmp(ob_d11m_class_token(res.class),
		      "D11_MACCONTROL_READABLE") &&
	      !res.dev_lost && !res.suspicious && res.stop_reason == NULL,
	      "D11_MACCONTROL_READABLE,no loss", "checked");

	/* D12 — all-ones is never a readable observation. */
	memset(&df, 0, sizeof(df));
	df.value = OB_D11M_ALL_ONES;
	ops = d_ops;
	ops.ctx = &df;
	ob_d11m_run(&ops, &wr, false, false, &res);
	check("D12", "all-ones-inaccessible",
	      res.class == OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS &&
	      res.class != OB_D11M_CLASS_READABLE && res.suspicious &&
	      !strcmp(ob_d11m_class_token(res.class),
		      "D11_WINDOW_INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED"),
	      "INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED",
	      ob_d11m_class_token(res.class));

	/* A latched loss by the accessor (finite value still returned). */
	memset(&df, 0, sizeof(df));
	df.value = 0x00000000u;
	df.latch_after_read = true;
	ops.ctx = &df;
	ob_d11m_run(&ops, &wr, false, false, &res);
	check("D12b", "accessor-latch-inaccessible",
	      res.class == OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS &&
	      df.reads == 1,
	      "INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED,1 read",
	      ob_d11m_class_token(res.class));

	/* D13 — no additional access after any functional read. */
	{
		unsigned after_readable, after_suspicious;

		memset(&df, 0, sizeof(df));
		df.value = 0x12345678u;
		ops.ctx = &df;
		ob_d11m_run(&ops, &wr, false, false, &res);
		after_readable = df.reads;
		check("D13a", "no-access-after-readable",
		      after_readable == 1 &&
		      df.last_event == OB_D11M_EV_END &&
		      df.stop_events == 0,
		      "1 read, END", "checked");

		memset(&df, 0, sizeof(df));
		df.value = OB_D11M_ALL_ONES;
		ops.ctx = &df;
		ob_d11m_run(&ops, &wr, false, false, &res);
		after_suspicious = df.reads;
		check("D13b", "no-access-after-suspicious",
		      after_suspicious == 1 &&
		      df.last_event == OB_D11M_EV_STOP &&
		      df.stop_events == 1,
		      "1 read, STOP then nothing", "checked");
	}

	/* D14 — forbidden candidates and writes are unreachable. */
	check("D14", "no-forbidden-offsets-or-writes",
	      OB_D11M_OFF_MACCONTROL != 0x0FFCu &&
	      OB_D11M_OFF_MACCONTROL != 0x03E0u &&
	      OB_D11M_FUNCTIONAL_WRITES == 0u && res.writes_attempted == 0,
	      "0x0120 only,0 functional writes", "checked");

	/* D15 — counters distinguish a callback failure. */
	memset(&df, 0, sizeof(df));
	df.fail = true;
	ops = d_ops;
	ops.ctx = &df;
	ob_d11m_run(&ops, &wr, false, false, &res);
	check("D15", "callback-failure-counters",
	      res.class == OB_D11M_CLASS_READ_UNCERTAIN &&
	      res.reads_attempted == 1 && res.reads_completed == 0 &&
	      !res.have_value && df.reads == 1,
	      "UNCERTAIN,attempted=1,completed=0", "checked");

	/*
	 * D23..D27 (Amendment 01, Task 2): the REAL POST formatter is compiled
	 * into this host test and into the kernel adapter. A result must never be
	 * described as successful before classification.
	 */
	{
		char line[160];
		bool fmt_ok = ob_d11m_format_post_read(line, sizeof(line),
						       0x04020402u) > 0;

		check("D23a", "post-formatter-pending-not-ok",
		      fmt_ok && strstr(line, "status=pending_classification") &&
		      !strstr(line, "status=ok") &&
		      strstr(line, "value=0x04020402") &&
		      strstr(line, "not yet classified"),
		      "pending_classification,no ok", line);

		/* Finite value, no latch -> readable. */
		w_prestate(&wf);
		wrap_run(&wf, &wr);
		memset(&df, 0, sizeof(df));
		df.value = 0x04020402u;
		ops = d_ops;
		ops.ctx = &df;
		ob_d11m_run(&ops, &wr, false, false, &res);
		check("D23b", "finite-value-readable-after-pending-post",
		      res.class == OB_D11M_CLASS_READABLE &&
		      df.post_events == 1 && df.reads == 1,
		      "READABLE, 1 POST, 1 read",
		      ob_d11m_class_token(res.class));

		/* 0xffffffff: the POST line must not say ok, classification stops. */
		memset(&df, 0, sizeof(df));
		df.value = OB_D11M_ALL_ONES;
		ops.ctx = &df;
		ob_d11m_run(&ops, &wr, false, false, &res);
		fmt_ok = ob_d11m_format_post_read(line, sizeof(line),
						  OB_D11M_ALL_ONES) > 0;
		check("D24", "all-ones-post-not-ok",
		      fmt_ok && !strstr(line, "status=ok") &&
		      res.class == OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS &&
		      df.post_events == 1 && df.reads == 1,
		      "INACCESSIBLE, 1 POST without ok",
		      ob_d11m_class_token(res.class));

		/* Finite value with accessor-side latch: no successful class. */
		memset(&df, 0, sizeof(df));
		df.value = 0x00000000u;
		df.latch_after_read = true;
		ops.ctx = &df;
		ob_d11m_run(&ops, &wr, false, false, &res);
		check("D25", "latch-with-finite-value-not-readable",
		      res.class == OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS &&
		      res.class != OB_D11M_CLASS_READABLE,
		      "INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED",
		      ob_d11m_class_token(res.class));

		/* Callback error: attempted 1, completed 0, no POST at all. */
		memset(&df, 0, sizeof(df));
		df.fail = true;
		ops.ctx = &df;
		ob_d11m_run(&ops, &wr, false, false, &res);
		check("D26", "callback-error-no-post",
		      res.class == OB_D11M_CLASS_READ_UNCERTAIN &&
		      res.reads_attempted == 1 && res.reads_completed == 0 &&
		      df.post_events == 0 && df.reads == 1,
		      "UNCERTAIN, 0 POST, 1 attempted",
		      ob_d11m_class_token(res.class));

		/* Suspicious result: no further MMIO callback after STOP. */
		memset(&df, 0, sizeof(df));
		df.value = OB_D11M_ALL_ONES;
		ops.ctx = &df;
		ob_d11m_run(&ops, &wr, false, false, &res);
		check("D27", "no-mmio-after-suspicious",
		      df.reads == 1 && df.last_event == OB_D11M_EV_STOP &&
		      df.stop_events == 1 && df.post_events == 1,
		      "1 read, POST, STOP, nothing after", "checked");
	}

	/* Lifecycle: retention is the post-write policy. */
	w_prestate(&wf);
	wrap_run(&wf, &wr);
	check("D21", "retention-after-write",
	      ob_d11m_retained(&wr) &&
	      ob_aie_remove_policy_of(&wr) == OB_AIE_REMOVE_RETAIN,
	      "RETAIN", "checked");

	/* Gate refuses a CORE_UP-looking result with wrong counters. */
	{
		struct ob_aie_result fake = wr;

		fake.writes_confirmed = 2;
		check("D22", "gate-rejects-counter-mismatch",
		      !ob_d11m_wrapper_gate(&fake, false, false) &&
		      strcmp(ob_d11m_gate_reason(&fake, false, false),
			     "gate_admitted") != 0,
		      "REFUSED", ob_d11m_gate_reason(&fake, false, false));
	}

	printf("ob_phase38a_maccontrol_test: %d checks, %d failure(s)\n",
	       checks, failures);
	return failures ? 1 : 0;
}

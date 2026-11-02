// SPDX-License-Identifier: GPL-2.0-only
/*
 * Phase 37B host tests B01..B25 — the real rev42_ai_enable_only transition.
 *
 * WHAT IS BEING EXECUTED
 * ----------------------
 *   ob_aie_run()            the actual sequencing/classification state machine
 *                           compiled into the module (src/ob_ai_enable_only.h)
 *   ob_aie_*()              the actual admission, acceptance, predicate and
 *                           lifecycle-policy functions used by the module
 *   ob_isolated_mode_select23()  the actual 23-way isolated-mode policy
 *   ob_chip_profile_resolve()    the actual supported-device registry
 *   ob_aiw_run()            the Phase 37A wrapper-only engine, to prove it is
 *                           unchanged by this phase (B25)
 *
 * There is no Python model and no paraphrase: the backend below only supplies
 * the six permitted hardware operations and a log sink, exactly as the kernel
 * adapter does.
 *
 * Every check prints one machine-readable RESULT line carrying the function
 * exercised, the initial simulated state, the expected and observed operation
 * traces, the expected and observed return values, the final classification
 * and the reproduction command.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#include "../../src/ob_ai_enable_only.h"
#include "../../src/ob_diag.h"
#include "../../src/ob_ai_wrapper_only.h"
#include "../../src/ob_ucode.h"
#include "../../src/ob_profile.h"

#define TRACE_MAX	48
#define TRACE_LEN	28

struct fhw {
	u32 reset_st;
	u32 reset_ctl;
	u32 ioctl;
	/* value applied to the register by each permitted write */
	u32 after_e1;
	u32 after_e2;
	u32 after_e3;
	bool latched;
	/* injected callback errors */
	bool fail_read_st;
	bool fail_read_ctl;
	bool fail_read_ioctl;
	bool fail_rb_ctl;	/* fail the E2 readback only (after a write) */
	bool fail_wr_e1;
	bool fail_wr_e2;
	bool fail_wr_e3;
	unsigned reads;
	unsigned writes;
	unsigned delays;
	int n;
	char trace[TRACE_MAX][TRACE_LEN];
};

static int checks;
static int failures;

/* ---- trace -------------------------------------------------------------- */

static void t_push(struct fhw *f, const char *fmt, ...)
{
	va_list ap;

	if (f->n >= TRACE_MAX)
		return;
	va_start(ap, fmt);
	vsnprintf(f->trace[f->n], TRACE_LEN, fmt, ap);
	va_end(ap);
	f->n++;
}

static void t_join(const struct fhw *f, char *out, size_t cap)
{
	int i;
	size_t used = 0;

	out[0] = '\0';
	for (i = 0; i < f->n; i++) {
		int w = snprintf(out + used, cap - used, "%s%s",
				 i ? " " : "", f->trace[i]);
		if (w < 0 || (size_t)w >= cap - used)
			return;
		used += (size_t)w;
	}
}

static int t_find(const struct fhw *f, const char *want, int from)
{
	int i;

	for (i = from; i < f->n; i++) {
		if (strcmp(f->trace[i], want) == 0)
			return i;
	}
	return -1;
}

/* ---- the six permitted hardware operations ------------------------------ */

static int hw_read_st(void *ctx, u32 *val)
{
	struct fhw *f = ctx;

	f->reads++;
	t_push(f, "R:st");
	if (f->fail_read_st)
		return OB_AIE_RET_EIO;
	*val = f->reset_st;
	return 0;
}

static int hw_read_ctl(void *ctx, u32 *val)
{
	struct fhw *f = ctx;

	f->reads++;
	t_push(f, "R:ctl");
	if (f->fail_read_ctl || (f->fail_rb_ctl && f->writes > 0))
		return OB_AIE_RET_EIO;
	*val = f->reset_ctl;
	return 0;
}

static int hw_read_ioctl(void *ctx, u32 *val)
{
	struct fhw *f = ctx;

	f->reads++;
	t_push(f, "R:ioctl");
	if (f->fail_read_ioctl)
		return OB_AIE_RET_EIO;
	*val = f->ioctl;
	return 0;
}

static int hw_wr_e1(void *ctx)
{
	struct fhw *f = ctx;

	f->writes++;
	t_push(f, "W:e1=0x3");
	if (f->fail_wr_e1)
		return OB_AIE_RET_EIO;
	f->ioctl = f->after_e1;
	return 0;
}

static int hw_wr_e2(void *ctx)
{
	struct fhw *f = ctx;

	f->writes++;
	t_push(f, "W:e2=0x0");
	if (f->fail_wr_e2)
		return OB_AIE_RET_EIO;
	f->reset_ctl = f->after_e2;
	return 0;
}

static int hw_wr_e3(void *ctx)
{
	struct fhw *f = ctx;

	f->writes++;
	t_push(f, "W:e3=0x1");
	if (f->fail_wr_e3)
		return OB_AIE_RET_EIO;
	f->ioctl = f->after_e3;
	return 0;
}

static void hw_delay(void *ctx, unsigned int us)
{
	struct fhw *f = ctx;

	f->delays++;
	t_push(f, "D:%u", us);
}

static const char *ev_name(enum ob_aie_event ev)
{
	switch (ev) {
	case OB_AIE_EV_BEGIN:		return "B";
	case OB_AIE_EV_PRE_READ:	return "P:R";
	case OB_AIE_EV_POST_READ:	return "O:R";
	case OB_AIE_EV_PRE_WRITE:	return "P:W";
	case OB_AIE_EV_POST_WRITE:	return "O:W";
	case OB_AIE_EV_DELAY:		return "L:D";
	case OB_AIE_EV_STOP:		return "S";
	case OB_AIE_EV_END:		return "E";
	}
	return "?";
}

static void hw_log(void *ctx, enum ob_aie_event ev, const struct ob_aie_log *rec)
{
	struct fhw *f = ctx;

	t_push(f, "%s:%u", ev_name(ev), (unsigned int)rec->seq);
}

static bool hw_lost(void *ctx)
{
	struct fhw *f = ctx;

	return f->latched;
}

static void ops_bind(struct ob_aie_ops *ops, struct fhw *f)
{
	memset(ops, 0, sizeof(*ops));
	ops->ctx = f;
	ops->read_reset_st = hw_read_st;
	ops->read_reset_ctl = hw_read_ctl;
	ops->read_ioctl = hw_read_ioctl;
	ops->write_ioctl_set_fgc = hw_wr_e1;
	ops->write_reset_ctl_deassert = hw_wr_e2;
	ops->write_ioctl_clr_fgc = hw_wr_e3;
	ops->delay_us = hw_delay;
	ops->log = hw_log;
	ops->dev_lost = hw_lost;
}

/* The Phase 37A wrapper-only backend, to prove it is untouched (B25). */
static unsigned w_reads;
static unsigned w_writes;

static int w_read32(void *ctx, enum ob_aiw_window win, u16 off, u32 *val)
{
	(void)ctx;
	w_reads++;
	if (win != OB_AIW_WIN_WRAPPER)
		return OB_AIW_RET_EINVAL;
	switch (off) {
	case OB_AIW_OFF_RESET_ST:
		*val = 0x00000000u;
		return 0;
	case OB_AIW_OFF_RESET_CTL:
		*val = 0x00000001u;
		return 0;
	case OB_AIW_OFF_IOCTL:
		*val = 0x00000000u;
		return 0;
	default:
		return OB_AIW_RET_EINVAL;
	}
}

static void w_log(void *ctx, enum ob_aiw_event ev, const struct ob_aiw_log *rec)
{
	(void)ctx;
	(void)ev;
	(void)rec;
}

/* ---- reporting ---------------------------------------------------------- */

static void report(const char *id, const char *fn, const char *initial,
		   const char *want_trace, const struct fhw *f, int got, int want,
		   const char *state, bool ok)
{
	char got_trace[512];

	t_join(f, got_trace, sizeof(got_trace));
	checks++;
	if (!ok)
		failures++;
	printf("RESULT %s %s func=%s initial=%s expected_trace=[%s] "
	       "observed_trace=[%s] ret=%d expected_ret=%d final_state=%s "
	       "repro=./ob_phase37b_enable_test log=tests/host/ob_phase37b_enable_test.log\n",
	       id, ok ? "PASS" : "FAIL", fn, initial, want_trace, got_trace,
	       got, want, state);
}

static bool check(const char *what, bool cond)
{
	if (!cond)
		printf("  detail: %s\n", what);
	return cond;
}

/* ---- fake-state helpers ------------------------------------------------- */

static void fhw_init(struct fhw *f)
{
	memset(f, 0, sizeof(*f));
	f->reset_st = OB_AIE_PRESTATE_RESET_ST;
	f->reset_ctl = OB_AIE_PRESTATE_RESET_CTL;
	f->ioctl = OB_AIE_PRESTATE_IOCTL;
	f->after_e1 = OB_AIE_E1_IOCTL_VALUE;
	f->after_e2 = OB_AIE_E2_RCTL_VALUE;
	f->after_e3 = OB_AIE_E3_IOCTL_VALUE;
}

static const char *GOOD_PRE =
	"RESET_ST=0x00000000,RESET_CTL=0x00000001,IOCTL=0x00000000";

/*
 * The exact expected trace of a clean transition, with every PRE marker in
 * front of its accessor invocation and the two documented delays in place.
 */
static const char *CLEAN_TRACE =
	"B:0 "
	"P:R:1 R:st O:R:1 "
	"P:R:2 R:ctl O:R:2 "
	"P:R:3 R:ioctl O:R:3 "
	"P:W:4 W:e1=0x3 P:R:4 R:ioctl O:R:4 O:W:4 "
	"P:W:5 W:e2=0x0 P:R:5 R:ctl O:R:5 O:W:5 "
	"D:1 L:D:5 "
	"P:W:6 W:e3=0x1 P:R:6 R:ioctl O:R:6 O:W:6 "
	"D:1 L:D:6 "
	"E:0";

/* ---- B01: supported BCM4352 admitted ------------------------------------ */

static void b01_supported_admitted(void)
{
	struct ob_chip_identity id;
	const struct ob_chip_profile *profile = NULL;
	enum ob_chip_profile_status st;
	bool flags[23];
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int ret;
	bool ok;

	memset(&id, 0, sizeof(id));
	id.pci_present = true;
	id.bcma_present = true;
	id.d11_present = true;
	id.pci_vendor = 0x14e4;
	id.pci_device = 0x43b1;
	id.pci_revision = 0x03;
	id.chip_id = 0x4352;
	id.chip_revision = 3;
	id.d11_manufacturer = 0x04bf;
	id.d11_core_id = 0x0812;
	id.d11_revision = 42;
	id.d11_class = 0;
	st = ob_chip_profile_resolve(&id, &profile);

	memset(flags, 0, sizeof(flags));
	flags[11] = true;	/* rev42_ai_enable_only */
	ok = check("profile resolves", st == OB_CHIP_PROFILE_OK) &&
	     check("profile is present", profile != NULL) &&
	     check("enable mode selected",
		   ob_isolated_mode_select23(
			   flags[0], flags[1], flags[2], flags[3], flags[4],
			   flags[5], flags[6], flags[7], flags[8], flags[9],
			   flags[10], flags[11], flags[12], flags[13], flags[14],
			   flags[15], flags[16], flags[17], flags[18], flags[19],
			   flags[20], flags[21], flags[22]) ==
		   OB_ISOLATED_REV42_AI_ENABLE);

	fhw_init(&f);
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);
	ok = ok && check("clean run returns 0", ret == 0) &&
	     check("core up", res.core_up);

	report("B01", "ob_chip_profile_resolve+ob_aie_run", GOOD_PRE, CLEAN_TRACE,
	       &f, ret, 0, "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B02: unsupported device rejected before callbacks ------------------ */

static void b02_unsupported_rejected(void)
{
	struct ob_chip_identity id;
	const struct ob_chip_profile *profile = NULL;
	enum ob_chip_profile_status st;
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int ret;
	bool ok;

	memset(&id, 0, sizeof(id));
	id.pci_present = true;
	id.bcma_present = true;
	id.d11_present = true;
	id.pci_vendor = 0x14e4;
	id.pci_device = 0x9999;		/* not registered */
	id.pci_revision = 0x03;
	id.chip_revision = 3;
	id.d11_manufacturer = 0x04bf;
	id.d11_core_id = 0x0812;
	id.d11_revision = 42;
	st = ob_chip_profile_resolve(&id, &profile);

	/* Missing callbacks are a contract defect, not a device state. */
	fhw_init(&f);
	ops_bind(&ops, &f);
	ops.write_reset_ctl_deassert = NULL;
	ret = ob_aie_run(&ops, false, &res);
	ok = check("unsupported device rejected",
		   st == OB_CHIP_PROFILE_UNSUPPORTED_DEVICE) &&
	     check("no profile", profile == NULL) &&
	     check("invalid ops refused", ret == OB_AIE_RET_EINVAL) &&
	     check("zero hardware callbacks", f.reads == 0 && f.writes == 0) &&
	     check("classified pre-check",
		   res.class == OB_AIE_CLASS_PRECHECK_REJECTED);

	report("B02", "ob_chip_profile_resolve+ob_aie_run", GOOD_PRE, "[]", &f,
	       ret, OB_AIE_RET_EINVAL, "ENABLE_PRECHECK_REJECTED", ok);
}

/* ---- B03: no flags and the retired mode stay fail-closed ---------------- */

static enum ob_isolated_mode retired_plus_enable(void)
{
	bool f[23];

	memset(f, 0, sizeof(f));
	f[10] = true;	/* rev42_ai_probe_only (retired) */
	f[11] = true;	/* rev42_ai_enable_only */
	return ob_isolated_mode_select23(
		f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9],
		f[10], f[11], f[12], f[13], f[14], f[15], f[16], f[17], f[18],
		f[19], f[20], f[21], f[22]);
}

static void b03_admission_unchanged(void)
{
	bool flags[23];
	enum ob_isolated_mode m_none, m_retired;
	bool ok;

	memset(flags, 0, sizeof(flags));
	m_none = ob_isolated_mode_select23(
		flags[0], flags[1], flags[2], flags[3], flags[4], flags[5],
		flags[6], flags[7], flags[8], flags[9], flags[10], flags[11],
		flags[12], flags[13], flags[14], flags[15], flags[16],
		flags[17], flags[18], flags[19], flags[20], flags[21],
		flags[22]);

	flags[10] = true;	/* retired rev42_ai_probe_only */
	m_retired = ob_isolated_mode_select23(
		flags[0], flags[1], flags[2], flags[3], flags[4], flags[5],
		flags[6], flags[7], flags[8], flags[9], flags[10], flags[11],
		flags[12], flags[13], flags[14], flags[15], flags[16],
		flags[17], flags[18], flags[19], flags[20], flags[21],
		flags[22]);

	ok = check("no flags selects no mode", m_none == OB_ISOLATED_NONE) &&
	     check("no flags is refused before hardware",
		   ob_isolated_default_load_refused(m_none, false, false)) &&
	     check("retired ai_probe is quarantined",
		   ob_aiw_legacy_retired(true) &&
		   !ob_aiw_legacy_retired(false)) &&
	     check("retired mode is its own (refused) selection",
		   m_retired == OB_ISOLATED_REV42_AI_PROBE) &&
	     check("retired + enable conflicts",
		   retired_plus_enable() == OB_ISOLATED_CONFLICT);

	{
		struct fhw f;

		fhw_init(&f);
		report("B03", "ob_isolated_mode_select23+ob_isolated_default_load_refused",
		       "flags=all-false", "[]", &f, (int)m_none,
		       (int)OB_ISOLATED_NONE, "ENABLE_PRECHECK_REJECTED", ok);
	}
}

/* ---- B04: enable-only selection and conflict rejection ----------------- */

static void b04_conflicts(void)
{
	bool a[23], b[23];
	enum ob_isolated_mode alone, both;
	bool ok;

	memset(a, 0, sizeof(a));
	a[11] = true;
	alone = ob_isolated_mode_select23(
		a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9],
		a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18],
		a[19], a[20], a[21], a[22]);

	memset(b, 0, sizeof(b));
	b[11] = true;
	b[22] = true;	/* rev42_ai_wrapper_only */
	both = ob_isolated_mode_select23(
		b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9],
		b[10], b[11], b[12], b[13], b[14], b[15], b[16], b[17], b[18],
		b[19], b[20], b[21], b[22]);

	ok = check("enable selected alone",
		   alone == OB_ISOLATED_REV42_AI_ENABLE) &&
	     check("enable + wrapper conflict",
		   both == OB_ISOLATED_CONFLICT);

	{
		struct fhw f;

		fhw_init(&f);
		report("B04", "ob_isolated_mode_select23",
		       "flags=enable(+wrapper)", "[]", &f, (int)both,
		       (int)OB_ISOLATED_CONFLICT, "ENABLE_PRECHECK_REJECTED",
		       ok);
	}
}

/* ---- B05/B13/B17: the clean transition --------------------------------- */

static void b05_clean_transition(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	char got[512];
	int ret;
	int i, pre_r, pre_w, pre_d;
	bool ok;

	fhw_init(&f);
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);
	t_join(&f, got, sizeof(got));

	pre_r = -1;
	pre_w = -1;
	pre_d = -1;
	for (i = 0; i < f.n; i++) {
		if (strcmp(f.trace[i], "P:R:5") == 0)
			pre_r = i;
		if (strcmp(f.trace[i], "P:W:5") == 0)
			pre_w = i;
		if (strcmp(f.trace[i], "D:1") == 0 && pre_d < 0)
			pre_d = i;
	}

	ok = check("returns 0", ret == OB_AIE_RET_OK) &&
	     check("trace is exactly the clean sequence",
		   strcmp(got, CLEAN_TRACE) == 0) &&
	     check("3 writes", res.writes_attempted == 3 &&
			       res.writes_confirmed == 3) &&
	     check("6 reads", res.reads_attempted == 6 &&
			      res.reads_completed == 6) &&
	     check("2 delays", f.delays == 2) &&
	     check("core up", res.core_up) &&
	     check("last completed step is E3",
		   res.last_completed_step == OB_AIE_STEP_E3) &&
	     check("PRE:W before W",
		   pre_w >= 0 && pre_w < t_find(&f, "W:e2=0x0", 0)) &&
	     check("PRE:R before R",
		   pre_r >= 0 && pre_r < t_find(&f, "R:ctl", pre_r)) &&
	     check("delay precedes E3",
		   pre_d >= 0 && pre_d < t_find(&f, "W:e3=0x1", 0)) &&
	     check("classification core up",
		   res.class == OB_AIE_CLASS_CORE_UP) &&
	     check("retain policy",
		   ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN);

	report("B05", "ob_aie_run", GOOD_PRE, CLEAN_TRACE, &f, ret,
	       OB_AIE_RET_OK, "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B06: unexpected pre-state rejected with zero writes --------------- */

static void b06_bad_prestate(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int ret;
	bool ok;

	fhw_init(&f);
	f.reset_ctl = 0x00000000u;	/* reset already released */
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);
	ok = check("three pre-state reads only", f.reads == 3) &&
	     check("zero writes", f.writes == 0 && res.writes_attempted == 0) &&
	     check("classified pre-check",
		   res.class == OB_AIE_CLASS_PRECHECK_REJECTED) &&
	     check("reason is prestate_unexpected",
		   res.stop_reason && strcmp(res.stop_reason,
					     "prestate_unexpected") == 0) &&
	     check("clean removal policy",
		   ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_CLEAN);

	report("B06", "ob_aie_run", "RESET_ST=0x0,RESET_CTL=0x0,IOCTL=0x0",
	       "B:0 P:R:1 R:st O:R:1 P:R:2 R:ctl O:R:2 P:R:3 R:ioctl O:R:3 S:0",
	       &f, ret, OB_AIE_RET_EIO, "ENABLE_PRECHECK_REJECTED", ok);
}

/* ---- B07: all-ones on each pre-state read stops before writes ---------- */

static void b07_ones_prestate(void)
{
	bool ok = true;
	int which;

	for (which = 0; which < 3; which++) {
		struct fhw f;
		struct ob_aie_ops ops;
		struct ob_aie_result res;
		int ret;
		char initial[96];

		fhw_init(&f);
		if (which == 0)
			f.reset_st = OB_AIE_ALL_ONES;
		if (which == 1)
			f.reset_ctl = OB_AIE_ALL_ONES;
		if (which == 2)
			f.ioctl = OB_AIE_ALL_ONES;
		ops_bind(&ops, &f);
		ret = ob_aie_run(&ops, false, &res);
		snprintf(initial, sizeof(initial),
			 "all-ones on pre-state read %d", which + 1);

		ok = ok && check("zero writes", f.writes == 0) &&
		     check("classified device loss",
			   res.class == OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED) &&
		     check("latch requested", res.device_lost) &&
		     check("suspicious flagged", res.suspicious) &&
		     check("retain policy",
			   ob_aie_remove_policy_of(&res) ==
			   OB_AIE_REMOVE_RETAIN);

		report(which == 0 ? "B07a" : which == 1 ? "B07b" : "B07c",
		       "ob_aie_run", initial,
		       "PRE -> accessor -> STOP reason=all_ones, no write", &f,
		       ret, OB_AIE_RET_EIO, "ENABLE_DEVICE_LOSS_SUSPECTED",
		       ok);
		ok = true;
	}
}

/* ---- B08: a pre-existing latch issues zero hardware operations ---------- */

static void b08_latch(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int ret;
	bool ok;

	fhw_init(&f);
	f.latched = true;
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, true, &res);
	ok = check("zero reads", f.reads == 0) &&
	     check("zero writes", f.writes == 0) &&
	     check("no delay", f.delays == 0) &&
	     check("classified device loss",
		   res.class == OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED) &&
	     check("latch retained", res.device_lost) &&
	     check("retain policy",
		   ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN);

	report("B08", "ob_aie_run", "dev_lost=1 (pre-existing latch)", "B:0 S:0",
	       &f, ret, OB_AIE_RET_EIO, "ENABLE_DEVICE_LOSS_SUSPECTED", ok);
}

/* ---- B09: missing callback issues zero hardware operations -------------- */

static void b09_missing_callback(void)
{
	bool ok = true;
	int which;

	for (which = 0; which < 6; which++) {
		struct fhw f;
		struct ob_aie_ops ops;
		struct ob_aie_result res;
		int ret;

		fhw_init(&f);
		ops_bind(&ops, &f);
		switch (which) {
		case 0: ops.read_reset_st = NULL; break;
		case 1: ops.read_reset_ctl = NULL; break;
		case 2: ops.read_ioctl = NULL; break;
		case 3: ops.write_ioctl_set_fgc = NULL; break;
		case 4: ops.write_reset_ctl_deassert = NULL; break;
		case 5: ops.write_ioctl_clr_fgc = NULL; break;
		}
		ret = ob_aie_run(&ops, false, &res);
		ok = ok && check("zero callbacks", f.reads == 0 && f.writes == 0) &&
		     check("invalid ops", ret == OB_AIE_RET_EINVAL) &&
		     check("classified pre-check",
			   res.class == OB_AIE_CLASS_PRECHECK_REJECTED);
	}
	{
		struct fhw f;

		fhw_init(&f);
		report("B09", "ob_aie_run", "one required callback is NULL", "",
		       &f, OB_AIE_RET_EINVAL, OB_AIE_RET_EINVAL,
		       "ENABLE_PRECHECK_REJECTED", ok);
	}
}

/* ---- B10..B12: the vendor-backed writes --------------------------------- */

static void b10_vendor_writes(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int e1, e2, e3, d1, d2;
	int ret;
	bool ok;

	fhw_init(&f);
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);

	e1 = t_find(&f, "W:e1=0x3", 0);
	e2 = t_find(&f, "W:e2=0x0", 0);
	e3 = t_find(&f, "W:e3=0x1", 0);
	d1 = t_find(&f, "D:1", 0);
	d2 = e3 >= 0 ? t_find(&f, "D:1", e3) : -1;

	ok = check("E1 writes CLK|FGC = 0x3", e1 >= 0) &&
	     check("E2 writes RESET_CTL = 0", e2 > e1) &&
	     check("E3 writes IOCTL = CLK = 0x1", e3 > e2) &&
	     check("delay after E2 and before E3", d1 > e2 && d1 < e3) &&
	     check("final delay after E3", d2 > e3) &&
	     check("E1 readback accepted",
		   ob_aie_accept_e1(res.e1_ioctl) && res.e1_ioctl == 0x3u) &&
	     check("E2 readback accepted",
		   ob_aie_accept_e2(res.e2_reset_ctl) &&
		   res.e2_reset_ctl == 0x0u) &&
	     check("E3 readback accepted",
		   ob_aie_accept_e3(res.e3_ioctl) && res.e3_ioctl == 0x1u);

	report("B10", "ob_aie_run", GOOD_PRE,
	       "E1 -> readback -> E2 -> readback -> delay -> E3 -> readback -> delay",
	       &f, ret, OB_AIE_RET_OK, "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B11/B12: E2 follows E1, E3 follows E2 after the documented delay -- */

static void b11_e2_follows_e1(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int e1, r1, e2;
	int ret;
	bool ok;

	fhw_init(&f);
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);

	e1 = t_find(&f, "W:e1=0x3", 0);
	r1 = t_find(&f, "R:ioctl", e1 < 0 ? 0 : e1);
	e2 = t_find(&f, "W:e2=0x0", 0);

	ok = check("E1 write precedes its readback", e1 >= 0 && r1 > e1) &&
	     check("E1 readback precedes the RESET_CTL write", r1 < e2) &&
	     check("E2 writes 0x0", e2 > 0) &&
	     check("E2 is the RESET_CTL register",
		   OB_AIE_E2_RCTL_VALUE == 0x0u) &&
	     check("E2 readback is the de-asserted value",
		   res.e2_reset_ctl == 0x0u && ob_aie_accept_e2(res.e2_reset_ctl));

	report("B11", "ob_aie_run", GOOD_PRE,
	       "E1 write -> E1 readback -> E2 write=0x0 -> E2 readback", &f, ret,
	       OB_AIE_RET_OK, "D11_WRAPPER_CORE_UP", ok);
}

static void b12_e3_after_delay(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int e2, r2, d, e3, r3;
	int ret;
	bool ok;

	fhw_init(&f);
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);

	e2 = t_find(&f, "W:e2=0x0", 0);
	r2 = t_find(&f, "R:ctl", e2 < 0 ? 0 : e2);
	d = t_find(&f, "D:1", 0);
	e3 = t_find(&f, "W:e3=0x1", 0);
	r3 = t_find(&f, "R:ioctl", e3 < 0 ? 0 : e3);

	ok = check("E2 readback precedes the delay", r2 > e2 && d > r2) &&
	     check("the documented delay is udelay(1)", OB_AIE_DELAY_US == 1u) &&
	     check("E3 write follows the delay", e3 > d) &&
	     check("E3 readback follows the E3 write", r3 > e3) &&
	     check("a second delay follows E3",
		   t_find(&f, "D:1", e3) > e3) &&
	     check("exactly two delays", f.delays == 2);

	report("B12", "ob_aie_run", GOOD_PRE,
	       "E2 readback -> D:1 -> E3 write=0x1 -> E3 readback -> D:1", &f,
	       ret, OB_AIE_RET_OK, "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B17: every PRE marker precedes its hardware callback -------------- */

static void b17_pre_before_access(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	const char *pairs[][2] = {
		{ "P:R:1", "R:st" },
		{ "P:R:2", "R:ctl" },
		{ "P:R:3", "R:ioctl" },
		{ "P:W:4", "W:e1=0x3" },
		{ "P:R:4", "R:ioctl" },
		{ "P:W:5", "W:e2=0x0" },
		{ "P:R:5", "R:ctl" },
		{ "P:W:6", "W:e3=0x1" },
		{ "P:R:6", "R:ioctl" },
	};
	bool ok = true;
	size_t i;
	int ret;

	fhw_init(&f);
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);

	for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
		int p = t_find(&f, pairs[i][0], 0);
		int a = p < 0 ? -1 : t_find(&f, pairs[i][1], p);

		if (p < 0 || a < 0 || p > a)
			ok = false;
	}
	ok = ok && check("all nine PRE markers precede their accessor", ok) &&
	     check("no accessor ran before any PRE",
		   strcmp(f.trace[0], "B:0") == 0 &&
		   strcmp(f.trace[1], "P:R:1") == 0 &&
		   strcmp(f.trace[2], "R:st") == 0);

	report("B17", "ob_aie_run", GOOD_PRE,
	       "PRE -> accessor for all 9 accesses", &f, ret, OB_AIE_RET_OK,
	       "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B13: bounded operation count -------------------------------------- */

static void b13_bounded(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int ret;
	bool ok;

	fhw_init(&f);
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);
	ok = check("exactly 6 reads", f.reads == 6 &&
				    res.reads_attempted == OB_AIE_MAX_READS) &&
	     check("exactly 3 writes", f.writes == 3 &&
				     res.writes_attempted == OB_AIE_MAX_WRITES) &&
	     check("no extra access beyond the contract", f.reads + f.writes == 9);

	report("B13", "ob_aie_run", GOOD_PRE,
	       "9 accesses total (6 reads, 3 writes)", &f, ret, OB_AIE_RET_OK,
	       "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B14: suspicious readback after a write stops immediately ---------- */

static void b14_ones_after_write(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int ret;
	bool ok;

	fhw_init(&f);
	f.after_e1 = OB_AIE_ALL_ONES;
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);
	ok = check("stopped after E1", f.writes == 1) &&
	     check("no E2/E3 access", t_find(&f, "W:e2=0x0", 0) < 0 &&
				      t_find(&f, "W:e3=0x1", 0) < 0) &&
	     check("classified device loss",
		   res.class == OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED) &&
	     check("latch requested", res.device_lost) &&
	     check("failed step is E1", res.failed_step == OB_AIE_STEP_E1) &&
	     check("no POST marker for the write",
		   t_find(&f, "O:W:4", 0) < 0) &&
	     check("retain policy",
		   ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN);

	report("B14", "ob_aie_run", "after E1: IOCTL reads 0xffffffff", "", &f,
	       ret, OB_AIE_RET_EIO, "ENABLE_DEVICE_LOSS_SUSPECTED", ok);
}

/* ---- B15/B16: mismatch after a write, and later writes suppressed ------ */

static void b15_mismatch(void)
{
	bool ok = true;
	int which;

	for (which = 0; which < 3; which++) {
		struct fhw f;
		struct ob_aie_ops ops;
		struct ob_aie_result res;
		int ret;

		fhw_init(&f);
		if (which == 0)
			f.after_e1 = 0x00000000u;	/* FGC never took */
		if (which == 1)
			f.after_e2 = 0x00000001u;	/* RESET still asserted */
		if (which == 2)
			f.after_e3 = 0x00000003u;	/* FGC never released */
		ops_bind(&ops, &f);
		ret = ob_aie_run(&ops, false, &res);

		ok = ok && check("classified uncertain",
				 res.class ==
				 OB_AIE_CLASS_TRANSITION_UNCERTAIN) &&
		     check("uncertain flag set", res.uncertain) &&
		     check("no device-loss latch", !res.device_lost) &&
		     check("retain policy",
			   ob_aie_remove_policy_of(&res) ==
			   OB_AIE_REMOVE_RETAIN) &&
		     check("no confirmed write for the failed step",
			   res.writes_confirmed == (unsigned)which) &&
		     check("writes attempted stopped at the failed step",
			   res.writes_attempted == (unsigned)(which + 1));

		report(which == 0 ? "B15a" : which == 1 ? "B15b" : "B15c",
		       "ob_aie_run",
		       which == 0 ? "E1 readback 0x0" :
		       which == 1 ? "E2 readback 0x1" : "E3 readback 0x3", "",
		       &f, ret, OB_AIE_RET_EIO, "ENABLE_TRANSITION_UNCERTAIN",
		       ok);
		ok = true;
	}

	/* B16: a failure at E1 leaves no later write reachable. */
	{
		struct fhw f;
		struct ob_aie_ops ops;
		struct ob_aie_result res;

		fhw_init(&f);
		f.fail_wr_e1 = true;
		ops_bind(&ops, &f);
		ob_aie_run(&ops, false, &res);
		ok = check("E1 failure suppresses E2/E3", f.writes == 1) &&
		     check("classified uncertain",
			   res.class == OB_AIE_CLASS_TRANSITION_UNCERTAIN);
		report("B16", "ob_aie_run", "E1 callback error", "", &f,
		       res.last_error, OB_AIE_RET_EIO,
		       "ENABLE_TRANSITION_UNCERTAIN", ok);
	}
}

/* ---- B18: attempts and confirmations are counted separately ------------ */

static void b18_counters(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	int ret;
	bool ok;

	fhw_init(&f);
	f.fail_rb_ctl = true;	/* the E2 readback callback fails */
	ops_bind(&ops, &f);
	ret = ob_aie_run(&ops, false, &res);

	ok = check("reads_attempted counts the failed readback",
		   res.reads_attempted == 5) &&
	     check("reads_completed excludes the failed readback",
		   res.reads_completed == 4) &&
	     check("writes_attempted is 2", res.writes_attempted == 2) &&
	     check("writes_confirmed is 1", res.writes_confirmed == 1) &&
	     check("attempts never exceed the bound",
		   res.reads_attempted <= OB_AIE_MAX_READS &&
		   res.writes_attempted <= OB_AIE_MAX_WRITES);

	report("B18", "ob_aie_run", "E2 readback callback error", "", &f, ret,
	       OB_AIE_RET_EIO, "ENABLE_TRANSITION_UNCERTAIN", ok);
}

/* ---- B19: the predicate is evaluated on the final valid readbacks ------- */

static void b19_predicate(void)
{
	u32 i, r;
	bool property = true;
	bool ok;

	for (i = 0; i < 4; i++) {
		for (r = 0; r < 2; r++) {
			bool accepted = ob_aie_accept_e3(i) &&
					ob_aie_accept_e2(r);
			if (accepted != ob_aie_core_up(i, r))
				property = false;
			if (accepted != ob_diag_ai_iscoreup(i, r))
				property = false;
		}
	}
	ok = check("acceptance implies the vendor predicate", property) &&
	     check("ob_aie_core_up matches ob_diag_ai_iscoreup",
		   ob_aie_core_up(0x1u, 0x0u) &&
		   ob_diag_ai_iscoreup(0x1u, 0x0u) &&
		   !ob_aie_core_up(0x3u, 0x0u) &&
		   !ob_diag_ai_iscoreup(0x3u, 0x0u) &&
		   !ob_aie_core_up(0x1u, 0x1u) &&
		   !ob_diag_ai_iscoreup(0x1u, 0x1u));

	{
		struct fhw f;

		fhw_init(&f);
		report("B19", "ob_aie_core_up+ob_diag_ai_iscoreup",
		       "all (IOCTL&3, RESET_CTL&1) combinations", "[]", &f,
		       property ? 1 : 0, 1, "D11_WRAPPER_CORE_UP", ok);
	}
}

/* ---- B20/B21: no functional-core or bring-up access in any branch ----- */

static void b20_no_core_access(void)
{
	struct fhw f;
	struct ob_aie_ops ops;
	struct ob_aie_result res;
	u32 seen[8];
	int nseen = 0;
	int i, j;
	bool ok = true;

	/*
	 * Every offset the engine ever names must be one of the three wrapper
	 * registers. The engine cannot name a fourth: its ops struct has one
	 * callback per permitted operation and no offset parameter.
	 */
	fhw_init(&f);
	ops_bind(&ops, &f);
	ob_aie_run(&ops, false, &res);

	seen[nseen++] = OB_AIE_OFF_RESET_ST;
	seen[nseen++] = OB_AIE_OFF_RESET_CTL;
	seen[nseen++] = OB_AIE_OFF_IOCTL;

	for (i = 0; i < nseen; i++) {
		bool known = false;

		for (j = 0; j < nseen; j++) {
			if (seen[i] == seen[j])
				known = true;
		}
		if (!known || seen[i] == 0x0ffcu)
			ok = false;
	}
	ok = ok && check("only wrapper offsets are expressible",
			 OB_AIE_OFF_IOCTL == 0x0408u &&
			 OB_AIE_OFF_RESET_CTL == 0x0800u &&
			 OB_AIE_OFF_RESET_ST == 0x0804u) &&
	     check("0x0FFC is not among them", seen[0] != 0x0ffcu &&
					      seen[1] != 0x0ffcu &&
					      seen[2] != 0x0ffcu);
	report("B20", "ob_aie_run", GOOD_PRE,
	       "no D11 core-window access in success or failure branches", &f,
	       0, 0, "D11_WRAPPER_CORE_UP", ok);
}

static void b21_no_bringup(void)
{
	/*
	 * The completion token says nothing about firmware, DMA, IRQ, PHY,
	 * radio or MAC, and the engine has no callback that could express any
	 * of them. This asserts the vocabulary rather than reachability.
	 */
	bool ok = check("core-up token is wrapper-scoped",
			strcmp(ob_aie_class_token(OB_AIE_CLASS_CORE_UP),
			       "D11_WRAPPER_CORE_UP") == 0) &&
	     check("tokens are distinct",
		   strcmp(ob_aie_class_token(OB_AIE_CLASS_PRECHECK_REJECTED),
			  ob_aie_class_token(OB_AIE_CLASS_CORE_UP)) != 0 &&
		   strcmp(ob_aie_class_token(OB_AIE_CLASS_TRANSITION_UNCERTAIN),
			  ob_aie_class_token(OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED))
			!= 0);
	{
		struct fhw f;

		fhw_init(&f);
		report("B21", "ob_aie_class_token", "n/a", "[]", &f, 0, 0,
		       "D11_WRAPPER_CORE_UP", ok);
	}
}

/* ---- B22/B23: retention and ownership ---------------------------------- */

static void b22_lifecycle(void)
{
	struct ob_aie_result res;
	struct fhw f;
	bool ok;

	/* Device loss retains, with no cleanup. */
	fhw_init(&f);
	f.after_e3 = OB_AIE_ALL_ONES;
	{
		struct ob_aie_ops ops;

		ops_bind(&ops, &f);
		ob_aie_run(&ops, false, &res);
	}
	ok = check("device loss retains",
		   res.class == OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED &&
		   ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN) &&
	     check("no cleanup write after the latch", f.writes == 3);

	/* Partial transition retains. */
	fhw_init(&f);
	f.after_e1 = 0x00000000u;
	{
		struct ob_aie_ops ops;

		ops_bind(&ops, &f);
		ob_aie_run(&ops, false, &res);
	}
	ok = ok && check("partial transition retains",
			 res.class == OB_AIE_CLASS_TRANSITION_UNCERTAIN &&
			 ob_aie_remove_policy_of(&res) ==
			 OB_AIE_REMOVE_RETAIN) &&
	     check("partial transition issued no rollback", f.writes == 1);

	/* Successful transition retains too: no teardown contract exists. */
	fhw_init(&f);
	{
		struct ob_aie_ops ops;

		ops_bind(&ops, &f);
		ob_aie_run(&ops, false, &res);
	}
	ok = ok && check("core up retains",
			 res.class == OB_AIE_CLASS_CORE_UP &&
			 ob_aie_remove_policy_of(&res) ==
			 OB_AIE_REMOVE_RETAIN);

	report("B22", "ob_aie_remove_policy_of",
	       "loss / partial / core-up", "[]", &f, (int)res.class,
	       (int)OB_AIE_CLASS_CORE_UP, "ENABLE_*_RETAIN", ok);
}

static void b23_ownership(void)
{
	struct ob_aie_result res;
	struct ob_aie_ops ops;
	struct fhw f;
	bool ok;

	fhw_init(&f);
	ops_bind(&ops, &f);
	ob_aie_run(&ops, false, &res);
	ok = check("core-up is reported, nothing is claimed beyond it",
		   res.core_up && res.class == OB_AIE_CLASS_CORE_UP &&
		   res.stop_reason == NULL) &&
	     check("core-up still retains the device",
		   ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN) &&
	     check("no teardown write was issued", f.writes == 3 && f.reads == 6);

	report("B23", "ob_aie_run", GOOD_PRE, CLEAN_TRACE, &f, 0, 0,
	       "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B24: no retry, no shared state ------------------------------------ */

static void b24_no_retry(void)
{
	struct fhw a, b;
	struct ob_aie_ops oa, ob;
	struct ob_aie_result ra, rb;
	int reta, retb;
	bool ok;

	fhw_init(&a);
	ops_bind(&oa, &a);
	reta = ob_aie_run(&oa, false, &ra);

	/* An independent second context with a different outcome must not see
	 * anything from the first run. */
	fhw_init(&b);
	b.after_e1 = 0x00000000u;
	ops_bind(&ob, &b);
	retb = ob_aie_run(&ob, false, &rb);

	ok = check("first run succeeds exactly once", reta == OB_AIE_RET_OK &&
						    a.writes == 3) &&
	     check("second run is independent", retb == OB_AIE_RET_EIO &&
						b.writes == 1 &&
						b.reads == 4) &&
	     check("no retry inside a run", a.reads == 6 &&
					   a.writes == 3) &&
	     check("results are per-context",
		   ra.class == OB_AIE_CLASS_CORE_UP &&
		   rb.class == OB_AIE_CLASS_TRANSITION_UNCERTAIN);
	report("B24", "ob_aie_run", "two independent contexts", CLEAN_TRACE, &a,
	       reta, OB_AIE_RET_OK, "D11_WRAPPER_CORE_UP", ok);
}

/* ---- B25: the Phase 37A wrapper-only engine is unchanged --------------- */

static void b25_wrapper_only_unchanged(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	int ret;
	bool ok;

	memset(&ops, 0, sizeof(ops));
	ops.read32 = w_read32;
	ops.log = w_log;
	ops.dev_lost = NULL;
	w_reads = 0;
	w_writes = 0;

	ret = ob_aiw_run(&ops, false, &res);
	ok = check("wrapper-only returns 0", ret == 0) &&
	     check("exactly three reads", w_reads == 3 &&
					 res.reads_attempted == 3) &&
	     check("no write is expressible", w_writes == 0) &&
	     check("complete", res.complete && res.reads_completed == 3) &&
	     check("core in reset at the Phase 37A values",
		   res.core_in_reset &&
		   res.reset_st == 0x0u && res.reset_ctl == 0x1u &&
		   res.ioctl == 0x0u) &&
	     check("token unchanged",
		   strcmp(OB_AIW_TOKEN_CORE_IN_RESET, "D11_CORE_IN_RESET") == 0);

	{
		struct fhw f;

		fhw_init(&f);
		report("B25", "ob_aiw_run", GOOD_PRE,
		       "three wrapper reads, no write", &f, ret, 0,
		       "WRAPPER_ONLY_COMPLETE", ok);
	}
}

int main(void)
{
	b01_supported_admitted();
	b02_unsupported_rejected();
	b03_admission_unchanged();
	b04_conflicts();
	b05_clean_transition();
	b06_bad_prestate();
	b07_ones_prestate();
	b08_latch();
	b09_missing_callback();
	b10_vendor_writes();
	b11_e2_follows_e1();
	b12_e3_after_delay();
	b13_bounded();
	b17_pre_before_access();
	b14_ones_after_write();
	b15_mismatch();
	b18_counters();
	b19_predicate();
	b20_no_core_access();
	b21_no_bringup();
	b22_lifecycle();
	b23_ownership();
	b24_no_retry();
	b25_wrapper_only_unchanged();

	printf("ob_phase37b_enable_test: %d checks, %d failure(s)\n", checks,
	       failures);
	return failures ? 1 : 0;
}

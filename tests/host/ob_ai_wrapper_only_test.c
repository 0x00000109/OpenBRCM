// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests Z01..Z24 for the Phase 36Z AI-wrapper-only diagnostic.
 *
 * WHAT IS UNDER TEST
 * ------------------
 * Not a model and not a rewrite: the compiled src/ob_ai_wrapper_only.h that the
 * module itself calls. `ob_aiw_run()` is the exact sequencing and stop
 * implementation invoked by ob_ai_wrapper_only_test() in src/ob_diag.c, and
 * `ob_isolated_mode_select23()` / `ob_aiw_legacy_retired()` are the exact
 * admission decisions ob_probe() takes. The backend here is a deterministic
 * recorder; the kernel backend supplies the same three callbacks through BCMA.
 *
 * Every test prints one machine-readable RESULT line:
 *
 *   RESULT <id> <PASS|FAIL> func=<symbol> ret=<got> expected_ret=<want>
 *          reads=<n> trace=[<ordered events>] note=<what was asserted>
 *
 * The trace is the observed callback/log order, so "PRE before the read" and
 * "no fourth access" are asserted on what actually happened, not on intent.
 *
 * Structural absence (no write callback, no config-space callback, no
 * functional-core access) is enforced two ways: the ops struct is asserted to
 * contain exactly four pointers by _Static_assert below, and the textual call
 * graph is checked by tests/host/test_ai_wrapper_only_source.py (Z17-Z21).
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "../../src/ob_ai_wrapper_only.h"
#include "../../src/ob_ucode.h"
#include "../../src/ob_diag.h"
#include "../../src/ob_chip_profile.h"

/*
 * Compile-time containment: ctx + read32 + log + dev_lost, nothing else. A
 * mutation that adds a write, reset, DMA or config-space callback to the
 * contract fails to compile here before any test runs.
 */
_Static_assert(sizeof(struct ob_aiw_ops) == 4 * sizeof(void *),
	       "ob_aiw_ops must contain exactly ctx/read32/log/dev_lost");
_Static_assert(OB_AIW_MAX_READS == 3u, "the contract is three reads");

static int failures;
static int checks;

/* ---- deterministic backend --------------------------------------------- */

enum rkind { R_LOG = 0, R_READ };

struct rec {
	enum rkind kind;
	enum ob_aiw_event ev;
	u8 seq;
	u16 off;
	u32 val;
	enum ob_aiw_window win;
};

#define MAXREC 64

struct fake {
	const char *name;
	u32 val[3];		/* RESET_ST, RESET_CTL, IOCTL */
	int err[3];
	bool latch_mid;		/* latch after the first successful read */
	bool latched;
	int nread;
	int nbadwin;
	int nrec;
	struct rec rec[MAXREC];
};

static int off_index(u16 off)
{
	switch (off) {
	case OB_AIW_OFF_RESET_ST:
		return 0;
	case OB_AIW_OFF_RESET_CTL:
		return 1;
	case OB_AIW_OFF_IOCTL:
		return 2;
	default:
		return -1;
	}
}

static void push(struct fake *f, enum rkind kind, enum ob_aiw_event ev, u8 seq,
		 u16 off, u32 val, enum ob_aiw_window win)
{
	if (f->nrec < MAXREC) {
		f->rec[f->nrec].kind = kind;
		f->rec[f->nrec].ev = ev;
		f->rec[f->nrec].seq = seq;
		f->rec[f->nrec].off = off;
		f->rec[f->nrec].val = val;
		f->rec[f->nrec].win = win;
		f->nrec++;
	}
}

static int fake_read(void *ctx, enum ob_aiw_window win, u16 off, u32 *v)
{
	struct fake *f = ctx;
	int i = off_index(off);

	push(f, R_READ, 0, 0, off, 0, win);
	if (win != OB_AIW_WIN_WRAPPER) {
		f->nbadwin++;
		return OB_AIW_RET_EINVAL;
	}
	if (i < 0) {
		f->nbadwin++;
		return OB_AIW_RET_EINVAL;
	}
	f->nread++;
	if (f->err[i])
		return f->err[i];
	*v = f->val[i];
	if (f->latch_mid && f->nread == 1)
		f->latched = true;
	return 0;
}

static void fake_log(void *ctx, enum ob_aiw_event ev,
		     const struct ob_aiw_log *r)
{
	push(ctx, R_LOG, ev, r->seq, r->off, r->value, 0);
}

static bool fake_lost(void *ctx)
{
	return ((struct fake *)ctx)->latched;
}

static void fake_init(struct fake *f, const char *name, u32 rst, u32 rctl,
		      u32 ioc)
{
	memset(f, 0, sizeof(*f));
	f->name = name;
	f->val[0] = rst;
	f->val[1] = rctl;
	f->val[2] = ioc;
}

static struct ob_aiw_ops ops_for(struct fake *f)
{
	struct ob_aiw_ops ops;

	ops.ctx = f;
	ops.read32 = fake_read;
	ops.log = fake_log;
	ops.dev_lost = fake_lost;
	return ops;
}

static const char *ev_name(enum ob_aiw_event ev)
{
	switch (ev) {
	case OB_AIW_EV_BEGIN:
		return "BEGIN";
	case OB_AIW_EV_PRE:
		return "PRE";
	case OB_AIW_EV_POST:
		return "POST";
	case OB_AIW_EV_STOP:
		return "STOP";
	case OB_AIW_EV_END:
		return "END";
	}
	return "?";
}

static void trace_of(const struct fake *f, char *b, size_t n)
{
	size_t used = 0;
	int i;

	b[0] = '\0';
	for (i = 0; i < f->nrec && used + 48 < n; i++) {
		const struct rec *r = &f->rec[i];
		int w;

		if (r->kind == R_READ)
			w = snprintf(b + used, n - used, "READ(win=%d,0x%04x) ",
				     (int)r->win, r->off);
		else
			w = snprintf(b + used, n - used, "%s(seq=%u,off=0x%04x,val=0x%08x) ",
				     ev_name(r->ev), (unsigned int)r->seq,
				     r->off, r->val);
		if (w < 0)
			break;
		used += (size_t)w;
	}
}

static int count_reads(const struct fake *f, u16 off)
{
	int i, n = 0;

	for (i = 0; i < f->nrec; i++)
		if (f->rec[i].kind == R_READ && f->rec[i].off == off)
			n++;
	return n;
}

static int pos_read(const struct fake *f, u16 off)
{
	int i;

	for (i = 0; i < f->nrec; i++)
		if (f->rec[i].kind == R_READ && f->rec[i].off == off)
			return i;
	return -1;
}

static int pos_log(const struct fake *f, enum ob_aiw_event ev, int seq)
{
	int i;

	for (i = 0; i < f->nrec; i++)
		if (f->rec[i].kind == R_LOG && f->rec[i].ev == ev &&
		    (seq < 0 || (int)f->rec[i].seq == seq))
			return i;
	return -1;
}

static int count_log(const struct fake *f, enum ob_aiw_event ev)
{
	int i, n = 0;

	for (i = 0; i < f->nrec; i++)
		if (f->rec[i].kind == R_LOG && f->rec[i].ev == ev)
			n++;
	return n;
}

/* ---- result reporting --------------------------------------------------- */

static void report(const char *id, const char *fn, bool ok, int got, int want,
		   int reads, const struct fake *f, const char *note)
{
	char tr[2048];

	trace_of(f, tr, sizeof(tr));
	checks++;
	if (!ok)
		failures++;
	printf("RESULT %s %s func=%s ret=%d expected_ret=%d reads=%d trace=[%s] note=%s\n",
	       id, ok ? "PASS" : "FAIL", fn, got, want, reads, tr, note);
	if (!ok)
		fflush(stdout);
}

/* ---- selector helper: the 23 real flags in declaration order ------------ */

enum {
	F_FW, F_UCODE, F_INITVALS, F_DMA, F_D11TAIL, F_BSINIT, F_RADIOID,
	F_DIAG, F_ATTACH, F_SBRESET, F_AI_PROBE, F_AI_ENABLE, F_R2069,
	F_D11TIMING, F_PRERADIO, F_CLASS0_0413, F_POST0413, F_POSTPHASE1,
	F_POSTPHASE2, F_POSTBMAC, F_RADIO_ON, F_POSTLPF, F_AI_WRAPPER,
	F_COUNT
};

static enum ob_isolated_mode sel23(const bool *f)
{
	return ob_isolated_mode_select23(
		f[F_FW], f[F_UCODE], f[F_INITVALS], f[F_DMA], f[F_D11TAIL],
		f[F_BSINIT], f[F_RADIOID], f[F_DIAG], f[F_ATTACH], f[F_SBRESET],
		f[F_AI_PROBE], f[F_AI_ENABLE], f[F_R2069], f[F_D11TIMING],
		f[F_PRERADIO], f[F_CLASS0_0413], f[F_POST0413],
		f[F_POSTPHASE1], f[F_POSTPHASE2], f[F_POSTBMAC], f[F_RADIO_ON],
		f[F_POSTLPF], f[F_AI_WRAPPER]);
}

static void flags_clear(bool *f)
{
	memset(f, 0, F_COUNT * sizeof(bool));
}

/* ---- profile identity (the real admission function) --------------------- */

static struct ob_chip_identity bcm4352_identity(void)
{
	struct ob_chip_identity id = {
		.pci_present = true,
		.bcma_present = true,
		.d11_present = true,
		.pci_vendor = 0x14e4,
		.pci_device = 0x43b1,
		.pci_revision = 0x03,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.d11_manufacturer = 0x04bf,
		.d11_core_id = 0x0812,
		.d11_revision = 42,
		.d11_class = 0,
	};

	return id;
}

/* ---- Z01..Z24 ----------------------------------------------------------- */

static void z01_supported_identity(void)
{
	struct ob_chip_identity id = bcm4352_identity();
	const struct ob_chip_profile *profile = NULL;
	enum ob_chip_profile_status st;
	struct fake f;

	fake_init(&f, "z01", 0, 0, 0);
	st = ob_chip_profile_resolve(&id, &profile);
	report("Z01", "ob_chip_profile_resolve", st == OB_CHIP_PROFILE_OK, (int)st,
	       (int)OB_CHIP_PROFILE_OK, 0, &f,
	       "BCM4352 14e4:43b1 rev03 / D11 rev42 accepted by the real profile admission");
}

static void z02_unsupported_identity(void)
{
	struct ob_chip_identity id = bcm4352_identity();
	const struct ob_chip_profile *profile = NULL;
	enum ob_chip_profile_status st;
	struct fake f;

	fake_init(&f, "z02", 0, 0, 0);
	id.pci_device = 0x43b0;		/* not the supported adapter */
	st = ob_chip_profile_resolve(&id, &profile);
	report("Z02", "ob_chip_profile_resolve", st != OB_CHIP_PROFILE_OK, (int)st,
	       -1, 0, &f,
	       "different PCI id rejected by admission, before any callback could exist");
}

static void z03_legacy_rejected(void)
{
	struct fake f;
	bool retired = ob_aiw_legacy_retired(true);
	bool live = ob_aiw_legacy_retired(false);
	bool ok = retired && !live;

	fake_init(&f, "z03", 0, 0, 0);
	report("Z03", "ob_aiw_legacy_retired", ok, retired ? 1 : 0, 1, 0, &f,
	       "rev42_ai_probe_only=1 maps to unconditional refusal (-EOPNOTSUPP) before any register access");
}

static void z04_wrapper_selected(void)
{
	bool fl[F_COUNT];
	enum ob_isolated_mode m;
	struct fake f;

	flags_clear(fl);
	fl[F_AI_WRAPPER] = true;
	m = sel23(fl);
	fake_init(&f, "z04", 0, 0, 0);
	report("Z04", "ob_isolated_mode_select23",
	       m == OB_ISOLATED_REV42_AI_WRAPPER_ONLY, (int)m,
	       (int)OB_ISOLATED_REV42_AI_WRAPPER_ONLY, 0, &f,
	       "only rev42_ai_wrapper_only selected -> the wrapper-only mode, nothing else");
}

static void z05_both_options_conflict(void)
{
	bool fl[F_COUNT];
	enum ob_isolated_mode m, m2, m3;
	struct fake f;

	flags_clear(fl);
	fl[F_AI_WRAPPER] = true;
	fl[F_AI_PROBE] = true;
	m = sel23(fl);
	flags_clear(fl);
	fl[F_AI_WRAPPER] = true;
	fl[F_RADIO_ON] = true;
	m2 = sel23(fl);
	flags_clear(fl);
	fl[F_AI_WRAPPER] = true;
	fl[F_AI_ENABLE] = true;
	m3 = sel23(fl);
	fake_init(&f, "z05", 0, 0, 0);
	report("Z05", "ob_isolated_mode_select23",
	       m == OB_ISOLATED_CONFLICT && m2 == OB_ISOLATED_CONFLICT &&
	       m3 == OB_ISOLATED_CONFLICT,
	       (int)m, (int)OB_ISOLATED_CONFLICT, 0, &f,
	       "wrapper+legacy, wrapper+radio_on and wrapper+ai_enable all fail closed as CONFLICT");
}

static void z06_none_selects_nothing(void)
{
	bool fl[F_COUNT];
	enum ob_isolated_mode m;
	struct fake f;

	flags_clear(fl);
	m = sel23(fl);
	fake_init(&f, "z06", 0, 0, 0);
	report("Z06", "ob_isolated_mode_select23", m == OB_ISOLATED_NONE, (int)m,
	       (int)OB_ISOLATED_NONE, 0, &f,
	       "no isolated option -> NONE (the wrapper runner is not selected implicitly)");
}

static void z07_normal_sequence(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int r1, r2, r3;
	bool ok;

	fake_init(&f, "z07", 0x00000000u, 0x00000001u, 0x00000000u);
	ops = ops_for(&f);
	r1 = ob_aiw_run(&ops, false, &res);
	r2 = pos_read(&f, OB_AIW_OFF_RESET_ST);
	r3 = pos_read(&f, OB_AIW_OFF_RESET_CTL);
	ok = (r1 == 0) && (res.reads_attempted == 3) &&
	     (res.reads_completed == 3) && (f.nread == 3) &&
	     (r2 >= 0 && r2 < r3 && r3 < pos_read(&f, OB_AIW_OFF_IOCTL)) &&
	     res.complete && !res.suspicious && count_log(&f, OB_AIW_EV_END) == 1 &&
	     count_reads(&f, OB_AIW_OFF_RESET_ST) == 1 &&
	     count_reads(&f, OB_AIW_OFF_RESET_CTL) == 1 &&
	     count_reads(&f, OB_AIW_OFF_IOCTL) == 1;
	report("Z07", "ob_aiw_run", ok, r1, 0, f.nread, &f,
	       "exactly three reads in order 0x0804,0x0800,0x0408 and one END marker");
}

static void z08_core_in_reset(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;
	bool ok;

	/* The recorded Phase 36V wrapper values: core held in reset. */
	fake_init(&f, "z08", 0x00000000u, 0x00000001u, 0x00000000u);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	ok = (ret == 0) && res.complete && res.core_in_reset && !res.core_up &&
	     (f.nread == 3) && (f.nbadwin == 0) &&
	     strcmp(OB_AIW_TOKEN_CORE_IN_RESET, "D11_CORE_IN_RESET") == 0;
	report("Z08", "ob_aiw_run", ok, ret, 0, f.nread, &f,
	       "RESET_ST=0 RESET_CTL=1 IOCTL=0 -> D11_CORE_IN_RESET result, still no core-window read");
}

static void z09_core_up_still_wrapper_only(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;
	int i;
	bool all_wrapper = true;

	fake_init(&f, "z09", 0x00000000u, 0x00000000u, 0x00000001u);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	for (i = 0; i < f.nrec; i++)
		if (f.rec[i].kind == R_READ &&
		    f.rec[i].win != OB_AIW_WIN_WRAPPER)
			all_wrapper = false;
	report("Z09", "ob_aiw_run",
	       ret == 0 && res.complete && res.core_up && f.nread == 3 &&
	       all_wrapper && f.nbadwin == 0,
	       ret, 0, f.nread, &f,
	       "core-up predicate true does NOT authorise a functional-core read; still three wrapper reads");
}

static void z10_first_all_ones(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "z10", 0xffffffffu, 0x00000001u, 0x00000000u);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("Z10", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 1 &&
	       res.reads_attempted == 1 && res.reads_completed == 0 &&
	       res.suspicious && res.fatal && res.failed_seq == 1 &&
	       count_log(&f, OB_AIW_EV_POST) == 0 &&
	       !res.complete && count_log(&f, OB_AIW_EV_END) == 0 &&
	       count_reads(&f, OB_AIW_OFF_RESET_CTL) == 0 &&
	       count_reads(&f, OB_AIW_OFF_IOCTL) == 0,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "all-ones RESET_ST stops the sequence: no second/third access, no END");
}

static void z11_second_all_ones(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "z11", 0x00000000u, 0xffffffffu, 0x00000000u);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("Z11", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 2 && res.failed_seq == 2 &&
	       count_reads(&f, OB_AIW_OFF_IOCTL) == 0 &&
	       count_log(&f, OB_AIW_EV_END) == 0,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "all-ones RESET_CTL suppresses the third read");
}

static void z12_third_all_ones(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "z12", 0x00000000u, 0x00000001u, 0xffffffffu);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("Z12", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 3 && res.failed_seq == 3 &&
	       !res.complete && count_log(&f, OB_AIW_EV_END) == 0 &&
	       res.stop_reason && strcmp(res.stop_reason, "all_ones") == 0,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "all-ones IOCTL after three reads: STOP, no COMPLETE marker");
}

static void z13_latched_before_entry(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "z13", 0, 0, 0);
	f.latched = true;
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, true, &res);
	report("Z13", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 0 &&
	       res.reads_attempted == 0 && res.reads_completed == 0 &&
	       res.stop_reason &&
	       strcmp(res.stop_reason, "device_loss_latched") == 0 &&
	       count_log(&f, OB_AIW_EV_PRE) == 0,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "pre-latched device: zero hardware callbacks and no PRE announced");
}

static void z13b_latch_mid_sequence(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "z13b", 0, 0, 0);
	f.latch_mid = true;
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("Z13b", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 1 &&
	       count_reads(&f, OB_AIW_OFF_RESET_CTL) == 0,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "latch set between reads suppresses every later access");
}

static void z14_missing_prerequisites(void)
{
	struct fake f;
	struct ob_aiw_result res;
	int no_hw, no_core, no_bus, no_ops, no_read, run_null;
	struct ob_aiw_ops ops;

	fake_init(&f, "z14", 0, 0, 0);
	ops = ops_for(&f);
	no_hw = ob_aiw_entry_check(false, true, true, &ops);
	no_core = ob_aiw_entry_check(true, false, true, &ops);
	no_bus = ob_aiw_entry_check(true, true, false, &ops);
	no_ops = ob_aiw_entry_check(true, true, true, NULL);
	ops.read32 = NULL;
	no_read = ob_aiw_entry_check(true, true, true, &ops);
	run_null = ob_aiw_run(NULL, false, &res);
	report("Z14", "ob_aiw_entry_check",
	       no_hw == OB_AIW_RET_ENODEV && no_core == OB_AIW_RET_ENODEV &&
	       no_bus == OB_AIW_RET_ENODEV && no_ops == OB_AIW_RET_EINVAL &&
	       no_read == OB_AIW_RET_EINVAL && run_null == OB_AIW_RET_EINVAL &&
	       f.nread == 0,
	       no_hw, OB_AIW_RET_ENODEV, f.nread, &f,
	       "missing hw/core/bus/callback refused before any callback; NULL ops refused by run()");
}

static void z15_callback_error(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "z15", 0, 0, 0);
	f.err[1] = -110;		/* -ETIMEDOUT from the second access */
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("Z15", "ob_aiw_run",
	       ret == -110 && f.nread == 2 && res.failed_seq == 2 &&
	       res.reads_attempted == 2 && res.reads_completed == 1 &&
	       res.failed_off == OB_AIW_OFF_RESET_CTL &&
	       res.stop_reason &&
	       strcmp(res.stop_reason, "callback_error") == 0 &&
	       count_log(&f, OB_AIW_EV_POST) == 1 &&
	       count_reads(&f, OB_AIW_OFF_IOCTL) == 0 &&
	       count_log(&f, OB_AIW_EV_END) == 0 && !res.complete,
	       ret, -110, f.nread, &f,
	       "failed callback propagates its errno, suppresses the rest, logs STOP not COMPLETE");
}

static void z16_pre_precedes_read(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret, i;
	bool ok = true;

	fake_init(&f, "z16", 0x11, 0x22, 0x33);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	for (i = 1; i <= 3 && ok; i++) {
		int pre = pos_log(&f, OB_AIW_EV_PRE, i);
		int post = pos_log(&f, OB_AIW_EV_POST, i);
		int rd = -1;
		int j;

		for (j = 0; j < f.nrec; j++)
			if (f.rec[j].kind == R_READ && j > pre && rd < 0)
				rd = j;
		ok = (pre >= 0) && (post > pre) && (rd > pre) && (post > rd);
	}
	report("Z16", "ob_aiw_run", ok && ret == 0, ret, 0, f.nread, &f,
	       "each PRE is logged before its read and each POST after it");
}

static void z17_no_config_readback(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret, i;
	int hw_records = 0;

	fake_init(&f, "z17", 0x1, 0x2, 0x3);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	for (i = 0; i < f.nrec; i++)
		if (f.rec[i].kind == R_READ)
			hw_records++;
	/* The ops table has no config-space entry point at all (enforced by the
	 * _Static_assert above); the only hardware callbacks are the three
	 * accessor calls. */
	report("Z17", "ob_aiw_run", ret == 0 && hw_records == 3 && f.nread == 3,
	       ret, 0, f.nread, &f,
	       "the diagnostic body issues accessor calls only; no PCI config callback exists to call");
}

static void z18_no_functional_window_any_branch(void)
{
	struct fake cases[6];
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	int i;
	bool ok = true;
	int total_reads = 0;

	/* success, core-in-reset, all-ones at 1/2/3, callback error */
	fake_init(&cases[0], "ok", 0x0, 0x1, 0x0);
	fake_init(&cases[1], "up", 0x0, 0x0, 0x1);
	fake_init(&cases[2], "ones1", 0xffffffffu, 0x0, 0x0);
	fake_init(&cases[3], "ones2", 0x0, 0xffffffffu, 0x0);
	fake_init(&cases[4], "ones3", 0x0, 0x0, 0xffffffffu);
	fake_init(&cases[5], "err", 0x0, 0x0, 0x0);
	cases[5].err[0] = -5;

	for (i = 0; i < 6; i++) {
		int j;

		ops = ops_for(&cases[i]);
		ob_aiw_run(&ops, false, &res);
		for (j = 0; j < cases[i].nrec; j++)
			if (cases[i].rec[j].kind == R_READ) {
				total_reads++;
				if (cases[i].rec[j].win != OB_AIW_WIN_WRAPPER)
					ok = false;
				if (off_index(cases[i].rec[j].off) < 0)
					ok = false;
			}
	}
	report("Z18", "ob_aiw_run", ok, ok ? 1 : 0, 1, total_reads, &cases[5],
	       "every access on every branch (normal/reset/all-ones/error) addresses the wrapper window only");
}

static void z22_two_contexts(void)
{
	struct fake a, b;
	struct ob_aiw_ops oa, ob;
	struct ob_aiw_result ra, rb;
	int r1, r2, r3;
	bool ok;

	fake_init(&a, "a", 0x0, 0x1, 0x0);
	fake_init(&b, "b", 0x0, 0x0, 0x1);
	oa = ops_for(&a);
	ob = ops_for(&b);
	r1 = ob_aiw_run(&oa, false, &ra);
	r2 = ob_aiw_run(&ob, false, &rb);
	r3 = ob_aiw_run(&oa, false, &ra);
	ok = r1 == 0 && r2 == 0 && r3 == 0 && a.nread == 6 && b.nread == 3 &&
	     ra.core_in_reset && rb.core_up;
	report("Z22", "ob_aiw_run", ok, r1 + r2 + r3, 0, a.nread + b.nread, &b,
	       "two independent contexts hold independent results; no shared static state");
}

static void z23_no_retry(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "z23", 0xffffffffu, 0, 0);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("Z23", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && count_log(&f, OB_AIW_EV_BEGIN) == 1 &&
	       f.nread == 1,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "an automatic retry after a suspicious read would show a second BEGIN or READ");
}

static void z24_existing_modes_unchanged(void)
{
	bool fl[F_COUNT];
	enum ob_isolated_mode oldsel, newsel;
	struct fake f;
	bool ok = true;

	/* select22 behavior for a representative mode must be identical to
	 * select23 with the new flag cleared. */
	flags_clear(fl);
	fl[F_RADIO_ON] = true;
	oldsel = ob_isolated_mode_select22(fl[F_FW], fl[F_UCODE], fl[F_INITVALS],
					   fl[F_DMA], fl[F_D11TAIL], fl[F_BSINIT],
					   fl[F_RADIOID], fl[F_DIAG], fl[F_ATTACH],
					   fl[F_SBRESET], fl[F_AI_PROBE],
					   fl[F_AI_ENABLE], fl[F_R2069],
					   fl[F_D11TIMING], fl[F_PRERADIO],
					   fl[F_CLASS0_0413], fl[F_POST0413],
					   fl[F_POSTPHASE1], fl[F_POSTPHASE2],
					   fl[F_POSTBMAC], fl[F_RADIO_ON],
					   fl[F_POSTLPF]);
	newsel = sel23(fl);
	ok = ok && oldsel == newsel && oldsel == OB_ISOLATED_ACPHY_RADIO_ON_TEST;

	flags_clear(fl);
	fl[F_AI_ENABLE] = true;
	oldsel = ob_isolated_mode_select22(fl[F_FW], fl[F_UCODE], fl[F_INITVALS],
					   fl[F_DMA], fl[F_D11TAIL], fl[F_BSINIT],
					   fl[F_RADIOID], fl[F_DIAG], fl[F_ATTACH],
					   fl[F_SBRESET], fl[F_AI_PROBE],
					   fl[F_AI_ENABLE], fl[F_R2069],
					   fl[F_D11TIMING], fl[F_PRERADIO],
					   fl[F_CLASS0_0413], fl[F_POST0413],
					   fl[F_POSTPHASE1], fl[F_POSTPHASE2],
					   fl[F_POSTBMAC], fl[F_RADIO_ON],
					   fl[F_POSTLPF]);
	newsel = sel23(fl);
	ok = ok && oldsel == newsel && oldsel == OB_ISOLATED_REV42_AI_ENABLE;

	fake_init(&f, "z24", 0, 0, 0);
	report("Z24", "ob_isolated_mode_select22/select23", ok,
	       (int)newsel, (int)oldsel, 0, &f,
	       "adding the 23rd flag leaves the other 22 selections bit-identical");
}

static void z15b_core_up_predicate_matches_ob_diag(void)
{
	u32 ioc, rctl;
	bool ok = true;
	struct fake f;

	for (ioc = 0; ioc < 4; ioc++)
		for (rctl = 0; rctl < 2; rctl++)
			if (ob_aiw_core_up(ioc, rctl) !=
			    ob_diag_ai_iscoreup(ioc, rctl))
				ok = false;
	fake_init(&f, "z15b", 0, 0, 0);
	report("Z15b", "ob_aiw_core_up", ok, ok ? 1 : 0, 1, 0, &f,
	       "wrapper predicate identical to the pre-existing vendor ai_iscoreup helper");
}

/* ---- Phase 37A A05..A15 ------------------------------------------------ */

static void a05_normal_observation(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "a05", 0x00000000u, 0x00000001u, 0x00000000u);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("A05", "ob_aiw_run",
	       ret == 0 && res.complete && !res.suspicious &&
	       res.reads_attempted == 3 && res.reads_completed == 3 &&
	       f.nread == 3 && count_log(&f, OB_AIW_EV_END) == 1 &&
	       count_log(&f, OB_AIW_EV_POST) == 3,
	       ret, 0, f.nread, &f,
	       "normal observation: attempted=3 completed=3, three POSTs, one END");
}

static void a06_first_all_ones(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "a06", 0xffffffffu, 0x00000001u, 0x00000000u);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("A06", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 1 &&
	       res.reads_attempted == 1 && res.reads_completed == 0 &&
	       count_log(&f, OB_AIW_EV_POST) == 0 &&
	       count_log(&f, OB_AIW_EV_STOP) == 1 &&
	       count_reads(&f, OB_AIW_OFF_RESET_CTL) == 0 &&
	       count_reads(&f, OB_AIW_OFF_IOCTL) == 0 &&
	       count_log(&f, OB_AIW_EV_END) == 0,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "all-ones read 1 stops immediately: attempted=1 completed=0, no POST, no later read");
}

static void a07_second_all_ones(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "a07", 0x00000000u, 0xffffffffu, 0x00000000u);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("A07", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 2 &&
	       res.reads_attempted == 2 && res.reads_completed == 1 &&
	       count_reads(&f, OB_AIW_OFF_IOCTL) == 0 &&
	       count_log(&f, OB_AIW_EV_POST) == 1 &&
	       count_log(&f, OB_AIW_EV_END) == 0 &&
	       res.stop_reason && strcmp(res.stop_reason, "all_ones") == 0,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "all-ones read 2: attempted=2 completed=1 and the third read is suppressed");
}

static void a08_third_all_ones(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "a08", 0x00000000u, 0x00000001u, 0xffffffffu);
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("A08", "ob_aiw_run",
	       ret == OB_AIW_RET_EIO && f.nread == 3 &&
	       res.reads_attempted == 3 && res.reads_completed == 2 &&
	       res.suspicious && res.fatal && !res.complete &&
	       res.stop_reason && strcmp(res.stop_reason, "all_ones") == 0 &&
	       count_log(&f, OB_AIW_EV_END) == 0 &&
	       count_log(&f, OB_AIW_EV_POST) == 2,
	       ret, OB_AIW_RET_EIO, f.nread, &f,
	       "all-ones read 3: attempted=3 completed=2 and no successful completion");
}

static void a09_no_ok_post_for_suspicious(void)
{
	struct fake cases[3];
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	int i;
	bool ok = true;

	/* The all-ones register rotates through seq 1, 2 and 3. In every case
	 * the failing seq must have NO POST at all, and the number of POSTs must
	 * equal the number of preceding valid observations. */
	fake_init(&cases[0], "ones1", 0xffffffffu, 0x00000000u, 0x00000000u);
	fake_init(&cases[1], "ones2", 0x00000000u, 0xffffffffu, 0x00000000u);
	fake_init(&cases[2], "ones3", 0x00000000u, 0x00000000u, 0xffffffffu);

	for (i = 0; i < 3; i++) {
		int seq = i + 1;

		ops = ops_for(&cases[i]);
		ob_aiw_run(&ops, false, &res);
		ok = ok &&
		     pos_log(&cases[i], OB_AIW_EV_POST, seq) < 0 &&
		     count_log(&cases[i], OB_AIW_EV_POST) == i &&
		     count_log(&cases[i], OB_AIW_EV_STOP) == 1;
	}
	report("A09", "ob_aiw_run", ok, ok ? 1 : 0, 1, 0, &cases[2],
	       "no POST status=ok exists for any suspicious readback; only PRE then STOP all_ones");
}

static void a10_callback_failure_counts(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	struct fake f;
	int ret;

	fake_init(&f, "a10", 0, 0, 0);
	f.err[1] = -110; /* second access fails */
	ops = ops_for(&f);
	ret = ob_aiw_run(&ops, false, &res);
	report("A10", "ob_aiw_run",
	       ret == -110 && f.nread == 2 &&
	       res.reads_attempted == 2 && res.reads_completed == 1 &&
	       res.failed_seq == 2 &&
	       res.stop_reason &&
	       strcmp(res.stop_reason, "callback_error") == 0 &&
	       count_log(&f, OB_AIW_EV_STOP) == 1 &&
	       count_log(&f, OB_AIW_EV_END) == 0,
	       ret, -110, f.nread, &f,
	       "failed seq 2 records attempted=2 completed=1; it is not reported as an unattempted read");
}

static void a12_device_loss_retention(void)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result r1, r2;
	struct fake f1, f2;
	int x, y;
	bool suspicious_latches, entry_latch_fatal;

	fake_init(&f1, "a12a", 0xffffffffu, 0, 0);
	ops = ops_for(&f1);
	x = ob_aiw_run(&ops, false, &r1);
	suspicious_latches = (x == OB_AIW_RET_EIO) && r1.fatal &&
			     r1.suspicious && !r1.complete;

	fake_init(&f2, "a12b", 0, 0, 0);
	f2.latched = true;
	ops = ops_for(&f2);
	y = ob_aiw_run(&ops, true, &r2);
	entry_latch_fatal = (y == OB_AIW_RET_EIO) && r2.fatal &&
			    !r2.suspicious && f2.nread == 0;

	report("A12", "ob_aiw_run", suspicious_latches && entry_latch_fatal,
	       suspicious_latches && entry_latch_fatal ? 1 : 0, 1, f1.nread, &f1,
	       "a suspicious read and an already-latched device both set the fatal/retention result and issue no later access");
}

static void a15_phase36z_regressions(void)
{
	struct fake f;
	int prior = failures;

	fake_init(&f, "a15", 0, 0, 0);
	report("A15", "phase36z_suite", prior == 0, prior, 0, 0, &f,
	       "all Phase 36Z Z01..Z24 assertions in this binary passed before this check");
}

int main(void)
{
	printf("# Phase 36Z Z01..Z24 + Phase 37A A05..A15 AI-wrapper-only host tests (real driver logic)\n");
	z01_supported_identity();
	z02_unsupported_identity();
	z03_legacy_rejected();
	z04_wrapper_selected();
	z05_both_options_conflict();
	z06_none_selects_nothing();
	z07_normal_sequence();
	z08_core_in_reset();
	z09_core_up_still_wrapper_only();
	z10_first_all_ones();
	z11_second_all_ones();
	z12_third_all_ones();
	z13_latched_before_entry();
	z13b_latch_mid_sequence();
	z14_missing_prerequisites();
	z15_callback_error();
	z15b_core_up_predicate_matches_ob_diag();
	z16_pre_precedes_read();
	z17_no_config_readback();
	z18_no_functional_window_any_branch();
	z22_two_contexts();
	z23_no_retry();
	z24_existing_modes_unchanged();
	a05_normal_observation();
	a06_first_all_ones();
	a07_second_all_ones();
	a08_third_all_ones();
	a09_no_ok_post_for_suspicious();
	a10_callback_failure_counts();
	a12_device_loss_retention();
	a15_phase36z_regressions();
	printf("# checks=%d failures=%d\n", checks, failures);
	return failures ? 1 : 0;
}

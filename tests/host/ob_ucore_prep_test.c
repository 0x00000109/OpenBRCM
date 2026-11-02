// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — Amendment 01 host tests for the core-preparation contract.
 *
 * These execute the REAL preparation state machine compiled into the module
 * (src/ob_ucore_prep.h) through a fake wrapper window. No hardware, no kernel
 * API, no MMIO. Every check prints a machine-readable RESULT line.
 *
 * Coverage map (Phase 38C Amendment 01):
 *   C05  a suspicious FIRST wrapper read suppresses the second read
 *   C06  a suspicious SECOND wrapper read suppresses every later operation
 *   C07  an invalid CP2 cannot reach the FAST clock-mode transition
 *   C08  a failed core-enable is not followed by any diagnostic MMIO
 *   C09  CP0..CP3 failure paths classify and stop with exact read counts
 *  A2-s  the sequence keeps its proven order and call counts on success
 *  A2-t  an already-latched device issues zero callbacks
 *
 * The operations this contract does NOT count are the BCMA helpers' OWN
 * accesses (inside bcma_host_pci_up/bcma_core_enable/clockmode): they are
 * invisible here and are not claimed.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "../../src/ob_ucore_prep.h"

static int checks;
static int failures;

static const char *ev_name(enum ob_ucore_ev ev)
{
	switch (ev) {
	case OB_UCORE_EV_SNAPSHOT: return "SNAPSHOT";
	case OB_UCORE_EV_SUSPICIOUS: return "SUSPICIOUS";
	case OB_UCORE_EV_LIFECYCLE: return "LIFECYCLE";
	case OB_UCORE_EV_HOST_UP: return "HOST_UP";
	case OB_UCORE_EV_ENABLE_CALLED: return "ENABLE_CALLED";
	case OB_UCORE_EV_POSTCONDITION: return "POSTCONDITION";
	case OB_UCORE_EV_POSTCONDITION_FAILED: return "POSTCONDITION_FAILED";
	case OB_UCORE_EV_CLK1E0: return "CLK1E0";
	case OB_UCORE_EV_CLK1E0_UNAVAILABLE: return "CLK1E0_UNAVAILABLE";
	case OB_UCORE_EV_STATE_PUBLISHED: return "STATE_PUBLISHED";
	}
	return "?";
}

static void report(const char *id, const char *what, bool ok, long got,
		   long want)
{
	checks++;
	if (!ok)
		failures++;
	printf("RESULT %s %s %s got=%ld expected=%ld\n", id,
	       ok ? "PASS" : "FAIL", what, got, want);
}

static void chk(const char *id, const char *what, long got, long want)
{
	report(id, what, got == want, got, want);
}

static void chkstr(const char *id, const char *what, const char *got,
		   const char *want)
{
	bool ok = got && want && strcmp(got, want) == 0;

	checks++;
	if (!ok)
		failures++;
	printf("RESULT %s %s %s got=%s expected=%s\n", id,
	       ok ? "PASS" : "FAIL", what, got ? got : "(null)",
	       want ? want : "(null)");
}

/* ---- fake wrapper window ------------------------------------------------ */

struct pfake {
	/* per-point scripted values (indexed by point) */
	u32 ioctl[4];
	u32 reset[4];
	bool ioctl_ones[4];
	bool reset_ones[4];
	/* operations */
	int enable_ret;
	bool report_core_enabled;
	u32 clk1e0;
	bool clk1e0_latches;
	/* state */
	bool dev_lost;
	/* exact counters */
	int ioctl_reads;
	int reset_reads;
	int host_up;
	int enable;
	int clockmode;
	int functional;
	int latch;
	int touches;
	int touches_after_latch;
	/* log bookkeeping */
	int ev_snapshot;
	int ev_suspicious;
	int ev_lifecycle;
	int ev_host_up;
	int ev_enable_called;
	int ev_postcond;
	int ev_postcond_failed;
	int ev_clk1e0;
	int ev_unavailable;
	int ev_published;
	int lifecycle_idx_mask;
};

static void pf_init(struct pfake *f)
{
	memset(f, 0, sizeof(*f));
	/* Device as found: wrapper disabled, reset asserted at CP0/CP1. */
	f->ioctl[OB_UCORE_CP0] = 0x00000000u;
	f->reset[OB_UCORE_CP0] = 0x00000001u;
	f->ioctl[OB_UCORE_CP1] = 0x00000000u;
	f->reset[OB_UCORE_CP1] = 0x00000001u;
	/* After bcma_core_enable(): clocked, reset released. */
	f->ioctl[OB_UCORE_CP2] = 0x00000001u;
	f->reset[OB_UCORE_CP2] = 0x00000000u;
	f->ioctl[OB_UCORE_CP3] = 0x00000001u;
	f->reset[OB_UCORE_CP3] = 0x00000000u;
	f->report_core_enabled = true;
	f->clk1e0 = 0x00000002u;
	f->enable_ret = 0;
}

static void touch(struct pfake *f)
{
	f->touches++;
	if (f->dev_lost)
		f->touches_after_latch++;
}

static bool pf_dev_lost(void *ctx)
{
	return ((struct pfake *)ctx)->dev_lost;
}

static void pf_latch(void *ctx, const char *reason)
{
	struct pfake *f = ctx;

	(void)reason;
	f->latch++;
	f->dev_lost = true;
}

static int pf_index(struct pfake *f, int read)
{
	(void)f;
	return read;
}

static u32 pf_read_ioctl(void *ctx)
{
	struct pfake *f = ctx;
	int p = pf_index(f, f->ioctl_reads);

	touch(f);
	f->ioctl_reads++;
	if (f->ioctl_ones[p])
		return 0xffffffffu;
	return f->ioctl[p];
}

static u32 pf_read_reset(void *ctx)
{
	struct pfake *f = ctx;
	int p = pf_index(f, f->reset_reads);

	touch(f);
	f->reset_reads++;
	if (f->reset_ones[p])
		return 0xffffffffu;
	return f->reset[p];
}

static void pf_host_up(void *ctx)
{
	struct pfake *f = ctx;

	touch(f);
	f->host_up++;
}

static int pf_core_enable(void *ctx)
{
	struct pfake *f = ctx;

	touch(f);
	f->enable++;
	return f->enable_ret;
}

static void pf_clockmode(void *ctx)
{
	struct pfake *f = ctx;

	touch(f);
	f->clockmode++;
}

static u32 pf_read_clk1e0(void *ctx)
{
	struct pfake *f = ctx;

	touch(f);
	f->functional++;
	if (f->clk1e0_latches) {
		f->latch++;
		f->dev_lost = true;
	}
	return f->clk1e0;
}

static bool pf_core_is_enabled(void *ctx)
{
	return ((struct pfake *)ctx)->report_core_enabled;
}

static void pf_log(void *ctx, const struct ob_ucore_log *rec)
{
	struct pfake *f = ctx;

	switch (rec->ev) {
	case OB_UCORE_EV_SNAPSHOT: f->ev_snapshot++; break;
	case OB_UCORE_EV_SUSPICIOUS: f->ev_suspicious++; break;
	case OB_UCORE_EV_LIFECYCLE:
		f->ev_lifecycle++;
		f->lifecycle_idx_mask |= 1 << rec->idx;
		break;
	case OB_UCORE_EV_HOST_UP: f->ev_host_up++; break;
	case OB_UCORE_EV_ENABLE_CALLED: f->ev_enable_called++; break;
	case OB_UCORE_EV_POSTCONDITION: f->ev_postcond++; break;
	case OB_UCORE_EV_POSTCONDITION_FAILED: f->ev_postcond_failed++; break;
	case OB_UCORE_EV_CLK1E0: f->ev_clk1e0++; break;
	case OB_UCORE_EV_CLK1E0_UNAVAILABLE: f->ev_unavailable++; break;
	case OB_UCORE_EV_STATE_PUBLISHED: f->ev_published++; break;
	default:
		printf("RESULT LOG FAIL unknown event %d\n", (int)rec->ev);
		break;
	}
	(void)ev_name;
}

static void pf_ops(struct ob_ucore_ops *ops, struct pfake *f)
{
	memset(ops, 0, sizeof(*ops));
	ops->ctx = f;
	ops->dev_lost = pf_dev_lost;
	ops->latch = pf_latch;
	ops->read_ioctl = pf_read_ioctl;
	ops->read_reset = pf_read_reset;
	ops->host_up = pf_host_up;
	ops->core_enable = pf_core_enable;
	ops->clockmode_fast = pf_clockmode;
	ops->read_clk1e0 = pf_read_clk1e0;
	ops->core_is_enabled = pf_core_is_enabled;
	ops->log = pf_log;
}

static int prep_run(struct pfake *f, struct ob_ucore_result *out)
{
	struct ob_ucore_ops ops;

	pf_init(f);
	pf_ops(&ops, f);
	return ob_ucore_prepare(&ops, out);
}

/* ---- A2-s / success ordering ------------------------------------------- */

static void t_success(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret = prep_run(&f, &r);

	chk("A2s", "ret", ret, 0);
	chk("A2s", "core_up", r.core_up, 1);
	chk("A2s", "mutated", r.mutated, 1);
	chkstr("A2s", "reason", r.reason, "core_up");
	chk("A2s", "wrapper ioctl reads", f.ioctl_reads, 4);
	chk("A2s", "wrapper reset reads", f.reset_reads, 4);
	chk("A2s", "snapshots", r.snapshots, 4);
	chk("A2s", "host_up calls", f.host_up, 1);
	chk("A2s", "core enable calls", f.enable, 1);
	chk("A2s", "clockmode calls", f.clockmode, 1);
	chk("A2s", "functional reads", f.functional, 1);
	chk("A2s", "latch calls", f.latch, 0);
	chk("A2s", "snapshot events", f.ev_snapshot, 4);
	chk("A2s", "lifecycle events", f.ev_lifecycle, 7);
	chk("A2s", "lifecycle 0..6 all emitted", f.lifecycle_idx_mask, 0x7f);
	chk("A2s", "postcondition event", f.ev_postcond, 1);
	chk("A2s", "state published", f.ev_published, 1);
	chk("A2s", "CP2 observed enabled", r.state[OB_UCORE_CP2].enabled, 1);
	chk("A2s", "CP3 observed enabled", r.state[OB_UCORE_CP3].enabled, 1);
	chk("A2s", "CP0 reset was read", r.state[OB_UCORE_CP0].reset_read, 1);
	chk("A2s", "no access after latch", f.touches_after_latch, 0);
}

static void t_already_enabled(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.ioctl[OB_UCORE_CP1] = 0x00000001u;
	f.reset[OB_UCORE_CP1] = 0x00000000u;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("A2s", "already-enabled ret", ret, 0);
	chk("A2s", "core enable not called", f.enable, 0);
	chk("A2s", "enable-called event says not called",
	    f.ev_enable_called, 1);
	chk("A2s", "clockmode still called", f.clockmode, 1);
}

/* ---- C05: suspicious FIRST read suppresses the second ------------------- */

static void t_first_read_ones(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.ioctl_ones[OB_UCORE_CP0] = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}

	chk("C05", "CP0 ioctl all-ones ret", ret, OB_UCORE_RET_ENODEV);
	chk("C05", "CP0 ioctl reads", f.ioctl_reads, 1);
	chk("C05", "CP0 reset read suppressed", f.reset_reads, 0);
	chk("C05", "CP0 host up suppressed", f.host_up, 0);
	chk("C05", "CP0 enable suppressed", f.enable, 0);
	chk("C05", "CP0 clockmode suppressed", f.clockmode, 0);
	chk("C05", "CP0 functional suppressed", f.functional, 0);
	chk("C05", "latch set", f.latch, 1);
	chk("C05", "no access after latch", f.touches_after_latch, 0);
	chk("C05", "state records no reset read",
	    r.state[OB_UCORE_CP0].reset_read, 0);
	chk("C05", "state records the suspicious read",
	    r.state[OB_UCORE_CP0].ioctl_suspicious, 1);
	chkstr("C05", "reason", r.reason, "cp0_suspicious");
	chk("C05", "failed point", r.failed_point, OB_UCORE_CP0);
	chk("C05", "suspicious event", f.ev_suspicious, 1);
}

static void t_first_read_ones_cp1(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.ioctl_ones[OB_UCORE_CP1] = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C05", "CP1 ioctl all-ones ret", ret, OB_UCORE_RET_ENODEV);
	chk("C05", "CP1 ioctl reads (CP0+CP1)", f.ioctl_reads, 2);
	chk("C05", "CP1 reset read suppressed", f.reset_reads, 1);
	chk("C05", "CP1 enable suppressed", f.enable, 0);
	chk("C05", "CP1 clockmode suppressed", f.clockmode, 0);
	chkstr("C05", "CP1 reason", r.reason, "cp1_suspicious");
	chk("C05", "CP1 failed point", r.failed_point, OB_UCORE_CP1);
}

/* ---- C06: suspicious SECOND read suppresses everything later ------------ */

static void t_second_read_ones_cp2(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.reset_ones[OB_UCORE_CP2] = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C06", "CP2 reset all-ones ret", ret, OB_UCORE_RET_ENODEV);
	chk("C06", "CP2 ioctl reads", f.ioctl_reads, 3);
	chk("C06", "CP2 reset reads", f.reset_reads, 3);
	chk("C06", "core enable happened before CP2", f.enable, 1);
	chk("C06", "clockmode suppressed", f.clockmode, 0);
	chk("C06", "functional suppressed", f.functional, 0);
	chk("C06", "latch set", f.latch, 1);
	chk("C06", "no access after latch", f.touches_after_latch, 0);
	chk("C06", "state records the suspicious reset",
	    r.state[OB_UCORE_CP2].reset_suspicious, 1);
	chkstr("C06", "reason", r.reason, "cp2_suspicious");
}

static void t_second_read_ones_cp0(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.reset_ones[OB_UCORE_CP0] = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C06", "CP0 reset all-ones ret", ret, OB_UCORE_RET_ENODEV);
	chk("C06", "CP0 ioctl reads", f.ioctl_reads, 1);
	chk("C06", "CP0 reset reads", f.reset_reads, 1);
	chk("C06", "host up suppressed", f.host_up, 0);
	chk("C06", "no access after latch", f.touches_after_latch, 0);
}

/* ---- C07: invalid CP2 cannot reach the FAST clock transition ----------- */

static void t_cp2_invalid_no_clockmode(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.ioctl_ones[OB_UCORE_CP2] = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C07", "CP2 ioctl all-ones ret", ret, OB_UCORE_RET_ENODEV);
	chk("C07", "clockmode calls", f.clockmode, 0);
	chk("C07", "reached_clockmode flag", r.reached_clockmode, 0);
	chk("C07", "functional read suppressed", f.functional, 0);
	chk("C07", "latch set", f.latch, 1);
	chkstr("C07", "reason", r.reason, "cp2_suspicious");
}

/* ---- C08: a failed core-enable is not followed by diagnostic MMIO ------- */

static void t_enable_failure(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.enable_ret = -5;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C08", "ret is the enable error", ret, -5);
	chk("C08", "snapshots performed", r.snapshots, 2);
	chk("C08", "wrapper ioctl reads (CP0,CP1)", f.ioctl_reads, 2);
	chk("C08", "wrapper reset reads (CP0,CP1)", f.reset_reads, 2);
	chk("C08", "no CP2 observation", r.state[OB_UCORE_CP2].ioctl, 0);
	chk("C08", "clockmode suppressed", f.clockmode, 0);
	chk("C08", "functional read suppressed", f.functional, 0);
	chk("C08", "enable error counted", r.enable_errors, 1);
	chk("C08", "enable_failed flag", r.enable_failed, 1);
	chk("C08", "no latch (no suspicious read)", f.latch, 0);
	chkstr("C08", "reason", r.reason, "core_enable_failed");
	chk("C08", "mutated is still reported", r.mutated, 1);
}

/* ---- C09: CP3 and functional-read failure paths ------------------------ */

static void t_cp3_ones(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.ioctl_ones[OB_UCORE_CP3] = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C09", "CP3 ioctl all-ones ret", ret, OB_UCORE_RET_ENODEV);
	chk("C09", "clockmode happened before CP3", f.clockmode, 1);
	chk("C09", "functional suppressed", f.functional, 0);
	chk("C09", "wrapper ioctl reads", f.ioctl_reads, 4);
	chk("C09", "wrapper reset reads", f.reset_reads, 3);
	chkstr("C09", "reason", r.reason, "cp3_suspicious");
	chk("C09", "latch set", f.latch, 1);
	chk("C09", "no access after latch", f.touches_after_latch, 0);
}

static void t_clk1e0_ones(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.clk1e0 = 0xffffffffu;
	f.clk1e0_latches = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C09", "clk1e0 all-ones ret", ret, OB_UCORE_RET_ENODEV);
	chk("C09", "functional read attempted", f.functional, 1);
	chk("C09", "latch set by the accessor", f.latch, 1);
	chkstr("C09", "reason", r.reason, "clk1e0_unavailable");
	chk("C09", "core_up false", r.core_up, 0);
	chk("C09", "unavailable event", f.ev_unavailable, 1);
}

static void t_core_down_after_clockmode(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	/* CP3 sees a wrapper that is not enabled (FGC set, CLK clear). */
	f.ioctl[OB_UCORE_CP3] = 0x00000002u;
	f.reset[OB_UCORE_CP3] = 0x00000000u;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("C09", "core-down ret", ret, OB_UCORE_RET_ENODEV);
	chk("C09", "functional read suppressed", f.functional, 0);
	chk("C09", "postcondition-failed event", f.ev_postcond_failed, 1);
	chkstr("C09", "reason", r.reason, "core_not_enabled_after_clockmode");
	chk("C09", "no latch", f.latch, 0);
}

/* ---- A2-t: entry gates issue zero callbacks ---------------------------- */

static void t_entry_latched(void)
{
	struct pfake f;
	struct ob_ucore_result r;
	int ret;

	pf_init(&f);
	f.dev_lost = true;
	{
		struct ob_ucore_ops ops;

		pf_ops(&ops, &f);
		ret = ob_ucore_prepare(&ops, &r);
	}
	chk("A2t", "latched entry ret", ret, OB_UCORE_RET_EIO);
	chk("A2t", "no wrapper read", f.ioctl_reads + f.reset_reads, 0);
	chk("A2t", "no host up", f.host_up, 0);
	chk("A2t", "no enable", f.enable, 0);
	chk("A2t", "no clockmode", f.clockmode, 0);
	chk("A2t", "no functional read", f.functional, 0);
	chkstr("A2t", "reason", r.reason, "entry_device_loss_latched");
}

static void t_invalid_ops(void)
{
	struct ob_ucore_result r;
	int ret = ob_ucore_prepare(NULL, &r);

	chk("A2t", "null ops ret", ret, OB_UCORE_RET_EINVAL);
	chk("A2t", "null ops reason", (long)(r.reason != NULL), 1);
}

int main(void)
{
	t_success();
	t_already_enabled();
	t_first_read_ones();
	t_first_read_ones_cp1();
	t_second_read_ones_cp0();
	t_second_read_ones_cp2();
	t_cp2_invalid_no_clockmode();
	t_enable_failure();
	t_cp3_ones();
	t_clk1e0_ones();
	t_core_down_after_clockmode();
	t_entry_latched();
	t_invalid_ops();

	printf("openbrcm core-prep tests: %d checks, %d failures\n", checks,
	       failures);
	return failures ? 1 : 0;
}

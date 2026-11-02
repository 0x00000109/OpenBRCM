/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 wrapper-window core preparation, Phase 38C Amendment 01.
 *
 * WHAT THIS FILE IS
 * -----------------
 * The complete control flow of the pre-upload D11 core preparation (the historic
 * `ob_ucode_prepare()` CP0..CP3 sequence) as a pure header, so a host test
 * executes the SAME code the module runs. `src/ob_ucode.c` keeps the kernel
 * adapter: one callback per wrapper read / core operation / log line.
 *
 * The register sequence is NOT changed. It is the hardware-proven M2.5 order:
 *
 *   CP0 snapshot   read BCMA_IOCTL, read BCMA_RESET_CTL       (wrapper window)
 *   host up        bcma_host_pci_up(bus)
 *   CP1 snapshot   read IOCTL, read RESET_CTL
 *   core enable    bcma_core_enable(core, 0) only if not enabled
 *   CP2 snapshot   read IOCTL, read RESET_CTL
 *   FAST clock     bcma_core_set_clockmode(core, BCMA_CLKMODE_FAST)
 *   CP3 snapshot   read IOCTL, read RESET_CTL
 *   functional     read D11+0x01e0 through the latching accessor
 *
 * WHAT THIS FILE FIXES (Amendment 01 defects A2-a, A2-b, A2-c)
 * ------------------------------------------------------------
 *  A2-a  `ob_ucode_core_state_snapshot()` performed BOTH raw wrapper reads
 *        unconditionally, so an all-ones IOCTL was followed by another MMIO
 *        read. Fixed: the first read is classified BEFORE the second is issued;
 *        a suspicious first read leaves `reset_ctl` unread
 *        (`state.reset_read == false`) and returns -ENODEV.
 *  A2-b  A CP2 snapshot was taken but never classified, so an inaccessible
 *        wrapper window could still reach `bcma_core_set_clockmode(FAST)`.
 *        Fixed: CP2 is classified before the clock-mode transition.
 *  A2-c  A failed `bcma_core_enable()` was followed by a CP2 snapshot, i.e. two
 *        more MMIO reads after the failure. Fixed: the failure returns
 *        immediately and no diagnostic access follows it.
 *
 * WHAT IT DELIBERATELY DOES NOT DO
 * --------------------------------
 * No write to any D11 register, no PCI configuration space access of its own,
 * no DMA/IRQ/PHY/radio/mac80211. `bcma_host_pci_up()`, `bcma_core_enable()` and
 * `bcma_core_set_clockmode()` are BCMA helpers whose INTERNAL accesses are not
 * visible here and are therefore not counted or claimed by this contract; only
 * the calls OpenBRCM itself issues are counted (`host_up_calls`,
 * `enable_calls`, `clockmode_calls`).
 */
#ifndef _OB_UCORE_PREP_H_
#define _OB_UCORE_PREP_H_

#include <linux/types.h>
#include "ob_ucode.h"

/* The inaccessible-window sentinel used by the rest of the driver. */
#define OB_UCORE_ALL_ONES		0xffffffffu

/* Native errno values, so this header needs no errno.h in the kernel. */
#define OB_UCORE_RET_OK			(0)
#define OB_UCORE_RET_EIO		(-5)
#define OB_UCORE_RET_ENODEV		(-19)
#define OB_UCORE_RET_EINVAL		(-22)

enum ob_ucore_point {
	OB_UCORE_CP0 = 0,
	OB_UCORE_CP1,
	OB_UCORE_CP2,
	OB_UCORE_CP3,
	OB_UCORE_POINT_COUNT,
};

/* One wrapper observation. `reset_read` is false when the first read stopped
 * the sequence, which is how a caller proves the second read was suppressed. */
struct ob_ucore_state {
	u32 ioctl;
	u32 reset_ctl;
	bool reset_read;
	bool ioctl_suspicious;
	bool reset_suspicious;
	bool clock_set;
	bool fgc_set;
	bool reset_asserted;
	bool enabled;
};

enum ob_ucore_ev {
	OB_UCORE_EV_SNAPSHOT = 0,	/* a CPn observation completed or stopped */
	OB_UCORE_EV_SUSPICIOUS,		/* an all-ones wrapper read latched loss */
	OB_UCORE_EV_LIFECYCLE,		/* lifecycle index 0..6 */
	OB_UCORE_EV_HOST_UP,
	OB_UCORE_EV_ENABLE_CALLED,
	OB_UCORE_EV_POSTCONDITION,
	OB_UCORE_EV_POSTCONDITION_FAILED,
	OB_UCORE_EV_CLK1E0,
	OB_UCORE_EV_CLK1E0_UNAVAILABLE,
	OB_UCORE_EV_STATE_PUBLISHED,
};

struct ob_ucore_log {
	enum ob_ucore_ev ev;
	int idx;		/* lifecycle index, point, or postcondition enum */
	u32 value;		/* ioctl / clk1e0 / postcondition raw value */
	u32 value2;		/* reset_ctl */
	int err;
	bool flag;		/* enabled / called / reset-available / latched */
	const char *point;	/* "CP0".."CP3" */
	const char *reason;	/* static token */
};

/*
 * Backend. `read_ioctl`/`read_reset` are the two raw wrapper reads; they are
 * separate callbacks precisely so the contract can omit the second one after a
 * suspicious first result. `latch` sets the monotonic device-loss latch.
 * `read_clk1e0` is the latching functional read of D11+0x01e0.
 */
struct ob_ucore_ops {
	void *ctx;
	bool (*dev_lost)(void *ctx);
	void (*latch)(void *ctx, const char *reason);
	u32  (*read_ioctl)(void *ctx);
	u32  (*read_reset)(void *ctx);
	void (*host_up)(void *ctx);
	int  (*core_enable)(void *ctx);
	void (*clockmode_fast)(void *ctx);
	u32  (*read_clk1e0)(void *ctx);
	bool (*core_is_enabled)(void *ctx);
	void (*log)(void *ctx, const struct ob_ucore_log *rec);
};

struct ob_ucore_result {
	/* outcome */
	bool core_up;
	bool mutated;
	int rc;
	const char *reason;
	enum ob_ucore_point failed_point;
	bool has_failed_point;
	/* exact operation accounting (driver-initiated calls only) */
	u32 ioctl_reads;
	u32 reset_reads;
	u32 snapshots;
	u32 host_up_calls;
	u32 enable_calls;
	u32 enable_errors;
	u32 clockmode_calls;
	u32 functional_reads;
	u32 latch_calls;
	bool enable_failed;
	bool reached_clockmode;
	/* per-point observations */
	struct ob_ucore_state state[OB_UCORE_POINT_COUNT];
};

static inline const char *ob_ucore_point_name(enum ob_ucore_point p)
{
	switch (p) {
	case OB_UCORE_CP0:
		return "CP0";
	case OB_UCORE_CP1:
		return "CP1";
	case OB_UCORE_CP2:
		return "CP2";
	case OB_UCORE_CP3:
		return "CP3";
	default:
		return "CP?";
	}
}

static inline bool ob_ucore_ops_ok(const struct ob_ucore_ops *ops)
{
	return ops && ops->dev_lost && ops->latch && ops->read_ioctl &&
	       ops->read_reset && ops->host_up && ops->core_enable &&
	       ops->clockmode_fast && ops->read_clk1e0 &&
	       ops->core_is_enabled && ops->log;
}

static inline void ob_ucore_log_ev(const struct ob_ucore_ops *ops,
				   enum ob_ucore_ev ev, int idx, u32 value,
				   u32 value2, int err, bool flag,
				   const char *point, const char *reason)
{
	struct ob_ucore_log rec;

	rec.ev = ev;
	rec.idx = idx;
	rec.value = value;
	rec.value2 = value2;
	rec.err = err;
	rec.flag = flag;
	rec.point = point;
	rec.reason = reason;
	ops->log(ops->ctx, &rec);
}

static inline void ob_ucore_result_init(struct ob_ucore_result *out)
{
	int i;

	out->core_up = false;
	out->mutated = false;
	out->rc = OB_UCORE_RET_OK;
	out->reason = "not_evaluated";
	out->failed_point = OB_UCORE_CP0;
	out->has_failed_point = false;
	out->ioctl_reads = 0;
	out->reset_reads = 0;
	out->snapshots = 0;
	out->host_up_calls = 0;
	out->enable_calls = 0;
	out->enable_errors = 0;
	out->clockmode_calls = 0;
	out->functional_reads = 0;
	out->latch_calls = 0;
	out->enable_failed = false;
	out->reached_clockmode = false;
	for (i = 0; i < OB_UCORE_POINT_COUNT; i++) {
		out->state[i].ioctl = 0;
		out->state[i].reset_ctl = 0;
		out->state[i].reset_read = false;
		out->state[i].ioctl_suspicious = false;
		out->state[i].reset_suspicious = false;
		out->state[i].clock_set = false;
		out->state[i].fgc_set = false;
		out->state[i].reset_asserted = false;
		out->state[i].enabled = false;
	}
}

/*
 * One wrapper observation with the fail-stop rule A2-a:
 *   read IOCTL -> classify -> (only if finite) read RESET_CTL -> classify.
 * Returns 0 with a fully populated @st, or -ENODEV with the suspicious field
 * set and `reset_read == false` when the FIRST read was all-ones.
 */
static inline int ob_ucore_snapshot(const struct ob_ucore_ops *ops,
				    struct ob_ucore_result *out,
				    enum ob_ucore_point p,
				    struct ob_ucore_state *st)
{
	const char *point = ob_ucore_point_name(p);

	st->ioctl = 0;
	st->reset_ctl = 0;
	st->reset_read = false;
	st->ioctl_suspicious = false;
	st->reset_suspicious = false;
	st->clock_set = false;
	st->fgc_set = false;
	st->reset_asserted = false;
	st->enabled = false;
	out->snapshots++;

	st->ioctl = ops->read_ioctl(ops->ctx);
	out->ioctl_reads++;
	if (st->ioctl == OB_UCORE_ALL_ONES) {
		/* Classified before the second read: no further MMIO. */
		st->ioctl_suspicious = true;
		out->state[p] = *st;
		ob_ucore_log_ev(ops, OB_UCORE_EV_SNAPSHOT, (int)p, st->ioctl, 0,
				0, false, point, "ioctl_suspicious");
		return OB_UCORE_RET_ENODEV;
	}

	st->reset_ctl = ops->read_reset(ops->ctx);
	out->reset_reads++;
	st->reset_read = true;
	if (st->reset_ctl == OB_UCORE_ALL_ONES) {
		st->reset_suspicious = true;
		out->state[p] = *st;
		ob_ucore_log_ev(ops, OB_UCORE_EV_SNAPSHOT, (int)p, st->ioctl,
				st->reset_ctl, 0, true, point,
				"reset_suspicious");
		return OB_UCORE_RET_ENODEV;
	}

	st->clock_set = !!(st->ioctl & BCMA_IOCTL_CLK);
	st->fgc_set = !!(st->ioctl & BCMA_IOCTL_FGC);
	st->reset_asserted = !!(st->reset_ctl & BCMA_RESET_CTL_RESET);
	st->enabled = ob_ucode_core_enabled_predicate(st->ioctl, st->reset_ctl);

	out->state[p] = *st;
	ob_ucore_log_ev(ops, OB_UCORE_EV_SNAPSHOT, (int)p, st->ioctl,
			st->reset_ctl, 0, true, point, NULL);
	return OB_UCORE_RET_OK;
}

/* Every stop funnels through here so counters, latch and reason cannot drift. */
static inline int ob_ucore_stop(const struct ob_ucore_ops *ops,
				struct ob_ucore_result *out,
				enum ob_ucore_point p, const char *reason,
				int rc, bool latch)
{
	out->failed_point = p;
	out->has_failed_point = true;
	out->reason = reason;
	out->rc = rc;
	out->core_up = false;
	if (latch) {
		out->latch_calls++;
		ops->latch(ops->ctx, reason);
		ob_ucore_log_ev(ops, OB_UCORE_EV_SUSPICIOUS, (int)p,
				out->state[p].ioctl, out->state[p].reset_ctl, rc,
				out->state[p].reset_read,
				ob_ucore_point_name(p), reason);
	}
	return rc;
}

/*
 * The whole preparation. Straight-line: eight wrapper reads at most, one
 * conditional core-enable, one clock-mode transition, one functional read.
 * No retry, no rollback, no cleanup write, no diagnostic access after a stop.
 */
static inline int ob_ucore_prepare(const struct ob_ucore_ops *ops,
				   struct ob_ucore_result *out)
{
	struct ob_ucore_state st;
	enum ob_ucore_point p;
	u32 clk = 0;
	bool enabled;
	int ret;

	if (!out)
		return OB_UCORE_RET_EINVAL;
	ob_ucore_result_init(out);
	if (!ob_ucore_ops_ok(ops)) {
		out->reason = "invalid_ops";
		out->rc = OB_UCORE_RET_EINVAL;
		return OB_UCORE_RET_EINVAL;
	}

	/* An already-latched device issues zero callbacks. */
	if (ops->dev_lost(ops->ctx)) {
		out->reason = "entry_device_loss_latched";
		out->rc = OB_UCORE_RET_EIO;
		out->mutated = false;
		return out->rc;
	}

	/* ---- CP0: entry observation, before any mutation ------------------ */
	p = OB_UCORE_CP0;
	ret = ob_ucore_snapshot(ops, out, p, &st);
	if (ret)
		return ob_ucore_stop(ops, out, p, "cp0_suspicious", ret, true);
	ob_ucore_log_ev(ops, OB_UCORE_EV_LIFECYCLE, 0, 0, 0, 0,
			ops->core_is_enabled(ops->ctx), "CP0", NULL);

	/* ---- host/backplane up: the first mutation ------------------------ */
	ob_ucore_log_ev(ops, OB_UCORE_EV_HOST_UP, 0, 0, 0, 0, false, "CP0",
			NULL);
	out->mutated = true;
	out->host_up_calls++;
	ops->host_up(ops->ctx);

	/* ---- CP1 ---------------------------------------------------------- */
	p = OB_UCORE_CP1;
	ret = ob_ucore_snapshot(ops, out, p, &st);
	if (ret)
		return ob_ucore_stop(ops, out, p, "cp1_suspicious", ret, true);
	enabled = st.enabled;
	ob_ucore_log_ev(ops, OB_UCORE_EV_LIFECYCLE, 1, 0, 0, 0, enabled, "CP1",
			NULL);
	ob_ucore_log_ev(ops, OB_UCORE_EV_LIFECYCLE, 2, 0, 0, 0, enabled, "CP1",
			NULL);

	/* ---- conditional core enable (A2-c: no MMIO after a failure) ----- */
	if (!enabled) {
		out->mutated = true;
		out->enable_calls++;
		ret = ops->core_enable(ops->ctx);
		ob_ucore_log_ev(ops, OB_UCORE_EV_ENABLE_CALLED, 0, 0, 0, ret, true,
				"CP1", NULL);
		if (ret) {
			/* No CP2 snapshot: the failure stops the sequence here. */
			out->enable_failed = true;
			out->enable_errors++;
			return ob_ucore_stop(ops, out, p, "core_enable_failed", ret,
					     false);
		}
	} else {
		ob_ucore_log_ev(ops, OB_UCORE_EV_ENABLE_CALLED, 0, 0, 0, 0, false,
				"CP1", NULL);
	}

	/* ---- CP2: classified BEFORE the clock-mode transition (A2-b) ------ */
	p = OB_UCORE_CP2;
	ret = ob_ucore_snapshot(ops, out, p, &st);
	if (ret)
		return ob_ucore_stop(ops, out, p, "cp2_suspicious", ret, true);
	enabled = st.enabled;
	ob_ucore_log_ev(ops, OB_UCORE_EV_LIFECYCLE, 3, 0, 0, 0, enabled, "CP2",
			NULL);

	/* ---- FAST (HT) clock --------------------------------------------- */
	out->mutated = true;
	out->clockmode_calls++;
	out->reached_clockmode = true;
	ops->clockmode_fast(ops->ctx);

	/* ---- CP3 ---------------------------------------------------------- */
	p = OB_UCORE_CP3;
	ret = ob_ucore_snapshot(ops, out, p, &st);
	if (ret)
		return ob_ucore_stop(ops, out, p, "cp3_suspicious", ret, true);
	enabled = st.enabled;
	ob_ucore_log_ev(ops, OB_UCORE_EV_LIFECYCLE, 4, 0, 0, 0, enabled, "CP3",
			NULL);
	ob_ucore_log_ev(ops, OB_UCORE_EV_POSTCONDITION, (int)
			ob_ucode_core_postcondition_from_regs(st.ioctl,
							      st.reset_ctl),
			st.ioctl, st.reset_ctl, 0, enabled, "CP3", NULL);
	if (!enabled) {
		ob_ucore_log_ev(ops, OB_UCORE_EV_POSTCONDITION_FAILED, 0, 0, 0, 0,
				false, "CP3", NULL);
		return ob_ucore_stop(ops, out, p, "core_not_enabled_after_clockmode",
				     OB_UCORE_RET_ENODEV, false);
	}

	/* ---- functional prerequisite: D11+0x01e0 (latching read) ---------- */
	p = OB_UCORE_CP3;
	ob_ucore_log_ev(ops, OB_UCORE_EV_LIFECYCLE, 5, 0, 0, 0,
			ops->core_is_enabled(ops->ctx), "CP3", NULL);
	out->functional_reads++;
	clk = ops->read_clk1e0(ops->ctx);
	if (ops->dev_lost(ops->ctx)) {
		out->state[p].ioctl = 0;
		out->state[p].reset_ctl = 0;
		ob_ucore_log_ev(ops, OB_UCORE_EV_CLK1E0_UNAVAILABLE, 0, clk, 0, 0,
				true, "CP3", "clk1e0_latched");
		return ob_ucore_stop(ops, out, p, "clk1e0_unavailable",
				     OB_UCORE_RET_ENODEV, false);
	}
	enabled = ops->core_is_enabled(ops->ctx);
	ob_ucore_log_ev(ops, OB_UCORE_EV_LIFECYCLE, 6, clk, 0, 0, enabled, "CP3",
			NULL);
	ob_ucore_log_ev(ops, OB_UCORE_EV_CLK1E0, 0, clk, 0, 0, enabled, "CP3",
			NULL);
	if (!ob_ucode_bootstrap_can_continue(enabled, clk)) {
		ob_ucore_log_ev(ops, OB_UCORE_EV_CLK1E0_UNAVAILABLE, 0, clk, 0, 0,
				false, "CP3", "clk1e0_rejected");
		return ob_ucore_stop(ops, out, p, "clk1e0_rejected",
				     OB_UCORE_RET_ENODEV, false);
	}

	ob_ucore_log_ev(ops, OB_UCORE_EV_STATE_PUBLISHED, 0, 0, 0, 0, true,
			"CP3", NULL);
	out->core_up = true;
	out->rc = OB_UCORE_RET_OK;
	out->reason = "core_up";
	return OB_UCORE_RET_OK;
}

#endif /* _OB_UCORE_PREP_H_ */

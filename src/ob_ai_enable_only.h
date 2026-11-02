/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — rev42 AI-wrapper core-enable transition (Phase 37B).
 *
 * WHAT THIS FILE IS
 * -----------------
 * The complete decision and sequencing logic of `rev42_ai_enable_only`, as a
 * pure header so the host tests execute the SAME code the module runs. It is
 * the write-capable successor to ob_ai_wrapper_only.h (Phase 36Z/37A), which
 * remains unchanged and observation-only.
 *
 * VENDOR PROVENANCE — the only sequence this contract implements
 * ---------------------------------------------------------------
 * Transcribed from upstream Linux drivers/bcma/core.c:bcma_core_enable() with
 * `flags = 0`, and confirmed against the bcma.ko actually shipped for the
 * running kernel (7.0.0-38-generic): the compiled function performs exactly
 *
 *     bcma_core_disable(core, flags)                     ; early-returns
 *     bcma_awrite32(core, BCMA_IOCTL,     CLK|FGC|flags)  -> 0x0408 <= 0x3
 *     bcma_aread32(core, BCMA_IOCTL)
 *     bcma_awrite32(core, BCMA_RESET_CTL, 0)              -> 0x0800 <= 0x0
 *     bcma_aread32(core, BCMA_RESET_CTL)
 *     udelay(1)
 *     bcma_awrite32(core, BCMA_IOCTL,     CLK|flags)      -> 0x0408 <= 0x1
 *     bcma_aread32(core, BCMA_IOCTL)
 *     udelay(1)
 *
 * with BCMA_IOCTL_CLK = 0x1, BCMA_IOCTL_FGC = 0x2, BCMA_RESET_CTL_RESET = 0x1
 * (include/linux/bcma/bcma_regs.h).
 *
 * The disable step is deliberately NOT reproduced. bcma_core_disable() opens
 * with
 *
 *     if (bcma_aread32(core, BCMA_RESET_CTL) & BCMA_RESET_CTL_RESET)
 *             return;
 *
 * so for a core that is ALREADY held in reset it issues no write and its
 * bounded BCMA_RESET_ST poll never runs. The 2026-10-24 live observation put
 * this core in exactly that state (RESET_ST=0x0, RESET_CTL=0x1, IOCTL=0x0).
 * The pre-state gate below admits ONLY that state; every other state is
 * refused before the first write. The disable path is therefore never taken,
 * never improvised, and has no residual unaccounted access.
 *
 * THE HARDWARE SURFACE IS ENUMERATED, NOT PARAMETERISED
 * -----------------------------------------------------
 * `struct ob_aie_ops` has one callback per permitted operation, each with a
 * fixed register and a fixed value. There is no `read32(off)` / `write32(off)`
 * escape hatch, so a new register cannot be reached by passing a different
 * number: it would require editing this header. There is no D11 functional-core
 * callback at all — `ob_d11_*`, `bcma_read32`/`bcma_write32` on core->addr and
 * the 0x0FFC probe offset are not merely unused, they are not expressible.
 * This holds in the success branch as well as every failure branch.
 *
 * THE FIVE DEFECTS THIS CONTRACT CLOSES (Phase 37A D1..D5)
 * -------------------------------------------------------
 *  D1 logging order    — every access is PRE-marked before the callback runs;
 *                        the previous code read the register, then logged PRE.
 *  D2 suspicious reads — 0xffffffff in any of the three control/status
 *                        registers stops the transition BEFORE any write.
 *  D3 progression      — no step proceeds until its readback matched the
 *                        documented acceptance mask, and the mask is exactly
 *                        the vendor control bits (never unrelated bits).
 *  D4 functional core  — the 0x0FFC read is gone. This mode ends at the
 *                        wrapper boundary even when the core reports up.
 *  D5 success reporting— four mutually exclusive outcomes, each with its own
 *                        token; `COMPLETE` alone is no longer emitted for a
 *                        rejected, failed or half-completed transition.
 *
 * OUTCOMES (exhaustive)
 * ---------------------
 *   ENABLE_PRECHECK_REJECTED      no write was attempted. The pre-state was
 *                                 unexpected, a precondition was missing, or a
 *                                 pre-state read failed. Hardware may be
 *                                 considered unmodified.
 *   ENABLE_DEVICE_LOSS_SUSPECTED  an all-ones readback (or a latched loss)
 *                                 stopped the run. No cleanup write is issued.
 *   ENABLE_TRANSITION_UNCERTAIN   at least one write was attempted and the
 *                                 transition did not reach a validated
 *                                 core-up. The hardware may have changed state.
 *   D11_WRAPPER_CORE_UP           all three transitions and their documented
 *                                 readbacks validated, and the vendor
 *                                 predicate is true on the final readbacks.
 *
 * A readback mismatch is NOT reported as proven PCIe device loss, and a
 * returning accessor is NOT reported as a successful write: a write is only
 * "confirmed" once its readback matched the mask. There is no retry, no poll,
 * no rollback and no cleanup write anywhere in this file.
 *
 * WHAT D11_WRAPPER_CORE_UP DOES NOT MEAN
 * --------------------------------------
 * Not microcode, not initvals, not PHY/radio, not DMA, not IRQ, not MAC, not
 * Wi-Fi. It says the AI wrapper reports an enabled, clocked D11 core.
 *
 * LIFECYCLE
 * ---------
 * Any state reached after the first write attempt retains ownership: the
 * removal policy below is RETAIN for every class except a write-free
 * pre-check rejection. A retained device must not be removed and re-probed as
 * if nothing happened.
 */
#ifndef _OB_AI_ENABLE_ONLY_H_
#define _OB_AI_ENABLE_ONLY_H_

#include <linux/types.h>

/* BCMA wrapper-window offsets (core->wrap). Identical to Phase 37A. */
#define OB_AIE_OFF_IOCTL	0x0408u	/* BCMA_IOCTL */
#define OB_AIE_OFF_RESET_CTL	0x0800u	/* BCMA_RESET_CTL */
#define OB_AIE_OFF_RESET_ST	0x0804u	/* BCMA_RESET_ST (read-only) */

/* Upstream BCMA bit definitions. */
#define OB_AIE_IOCTL_CLK	0x00000001u	/* BCMA_IOCTL_CLK */
#define OB_AIE_IOCTL_FGC	0x00000002u	/* BCMA_IOCTL_FGC */
#define OB_AIE_RCTL_RESET	0x00000001u	/* BCMA_RESET_CTL_RESET */

#define OB_AIE_ALL_ONES		0xffffffffu

/*
 * Acceptance masks. ONLY these bits are compared on a readback: IOCTL bits
 * 2..13 (BCMA_IOCTL_CORE_BITS) and RESET_CTL bits 1..31 are core-specific and
 * are never assumed to read back as written.
 */
#define OB_AIE_IOCTL_CTRL_MASK	(OB_AIE_IOCTL_CLK | OB_AIE_IOCTL_FGC)
#define OB_AIE_RCTL_CTRL_MASK	OB_AIE_RCTL_RESET

/* E1 — IOCTL force-gate/clock. */
#define OB_AIE_E1_IOCTL_VALUE	(OB_AIE_IOCTL_CLK | OB_AIE_IOCTL_FGC)
#define OB_AIE_E1_EXPECT	(OB_AIE_IOCTL_CLK | OB_AIE_IOCTL_FGC)
/* E2 — release reset. bcma_core_enable() writes a plain zero. */
#define OB_AIE_E2_RCTL_VALUE	0x00000000u
#define OB_AIE_E2_EXPECT	0x00000000u
/* E3 — drop the force gate, keep the clock. */
#define OB_AIE_E3_IOCTL_VALUE	OB_AIE_IOCTL_CLK
#define OB_AIE_E3_EXPECT	OB_AIE_IOCTL_CLK

/* The documented delay: udelay(1) before and after E3. */
#define OB_AIE_DELAY_US		1u

/*
 * Pre-state admission profile: the exact state observed live on 2026-10-24
 * and used by Phase 37A. Held to the documented control bits, so the check is
 * "reset asserted, clock off, no reset in progress" rather than "this exact
 * 32-bit word".
 */
#define OB_AIE_PRESTATE_RESET_ST	0x00000000u
#define OB_AIE_PRESTATE_RESET_CTL	0x00000001u
#define OB_AIE_PRESTATE_IOCTL		0x00000000u

/* Bounded operation counts: 3 pre-state reads + 3 write/readback pairs. */
#define OB_AIE_MAX_READS	6u
#define OB_AIE_MAX_WRITES	3u

/*
 * Return codes. Numeric literals keep this header usable in the host harness,
 * which has no kernel errno headers; the values equal the kernel's.
 */
#define OB_AIE_RET_OK		0
#define OB_AIE_RET_EIO		(-5)	/* -EIO */
#define OB_AIE_RET_ENODEV	(-19)	/* -ENODEV */
#define OB_AIE_RET_EINVAL	(-22)	/* -EINVAL */

/* The ordered steps. ADMISSION issues no hardware operation. */
enum ob_aie_step {
	OB_AIE_STEP_ADMISSION = 0,	/* pre-state verdict, no access */
	OB_AIE_STEP_PRE_RESET_ST,	/* read RESET_ST  */
	OB_AIE_STEP_PRE_RESET_CTL,	/* read RESET_CTL */
	OB_AIE_STEP_PRE_IOCTL,		/* read IOCTL     */
	OB_AIE_STEP_E1,			/* IOCTL |= CLK|FGC, read back */
	OB_AIE_STEP_E2,			/* RESET_CTL = 0,    read back */
	OB_AIE_STEP_E3,			/* IOCTL = CLK,      read back */
	OB_AIE_STEP_COUNT
};

enum ob_aie_op {
	OB_AIE_OP_NONE = 0,
	OB_AIE_OP_READ,
	OB_AIE_OP_WRITE,
};

enum ob_aie_class {
	OB_AIE_CLASS_CORE_UP = 0,
	OB_AIE_CLASS_PRECHECK_REJECTED,
	OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED,
	OB_AIE_CLASS_TRANSITION_UNCERTAIN,
};

enum ob_aie_event {
	OB_AIE_EV_BEGIN = 0,
	OB_AIE_EV_PRE_READ,
	OB_AIE_EV_POST_READ,
	OB_AIE_EV_PRE_WRITE,
	OB_AIE_EV_POST_WRITE,
	OB_AIE_EV_DELAY,
	OB_AIE_EV_STOP,
	OB_AIE_EV_END,
};

/* Machine-readable log record; the kernel adapter owns the formatting. */
struct ob_aie_log {
	u8 seq;		/* step ordinal; 0 for BEGIN/END and DELAY */
	u16 off;	/* wrapper offset, 0 when not applicable */
	u32 value;	/* POST_* only: readback */
	u32 written;	/* POST_WRITE only: the value that was written */
	int error;	/* STOP only */
	const char *reason;	/* STOP only; static string */
};

/*
 * Backend. Exactly three reads and three writes are expressible, each with a
 * fixed register and fixed value. `ctx` is `struct ob_hw *` in the kernel and
 * a test context on the host.
 */
struct ob_aie_ops {
	void *ctx;
	int  (*read_reset_st)(void *ctx, u32 *val);
	int  (*read_reset_ctl)(void *ctx, u32 *val);
	int  (*read_ioctl)(void *ctx, u32 *val);
	int  (*write_ioctl_set_fgc)(void *ctx);
	int  (*write_reset_ctl_deassert)(void *ctx);
	int  (*write_ioctl_clr_fgc)(void *ctx);
	void (*delay_us)(void *ctx, unsigned int us);
	void (*log)(void *ctx, enum ob_aie_event ev, const struct ob_aie_log *rec);
	bool (*dev_lost)(void *ctx);	/* optional; checked before every access */
};

struct ob_aie_result {
	/* pre-state observations */
	u32 reset_st;
	u32 reset_ctl;
	u32 ioctl;
	bool have_reset_st;
	bool have_reset_ctl;
	bool have_ioctl;
	bool prestate_ok;
	/* transition readbacks */
	u32 e1_ioctl;
	u32 e2_reset_ctl;
	u32 e3_ioctl;
	bool have_e1;
	bool have_e2;
	bool have_e3;
	u32 final_ioctl;
	u32 final_reset_ctl;
	/* counters, deliberately separate */
	u8 reads_attempted;	/* PRE emitted and accessor invoked */
	u8 reads_completed;	/* non-suspicious value recorded */
	u8 writes_attempted;	/* PRE emitted and accessor invoked */
	u8 writes_confirmed;	/* readback matched the acceptance mask */
	/* progress */
	u8 last_completed_step;	/* enum ob_aie_step, 0 = none */
	u8 failed_step;		/* enum ob_aie_step, 0 = none */
	enum ob_aie_op failed_op;
	u16 failed_off;
	u32 last_value;		/* last value read, including a rejected one */
	int last_error;		/* 0 on CORE_UP */
	bool suspicious;	/* an all-ones read was observed */
	bool device_lost;	/* latch the device-loss state, retain */
	bool uncertain;		/* post-write state not established */
	bool core_up;		/* vendor predicate true on final readbacks */
	enum ob_aie_class class;
	const char *stop_reason;	/* static token, NULL on CORE_UP */
};

/*
 * Vendor ai_iscoreup predicate: (IOCTL & 0x3) == 0x1 && (RESET_CTL & 0x1) == 0.
 * MUST stay identical to ob_diag_ai_iscoreup(); the host test pins both.
 *
 * Note the deliberate redundancy: OB_AIE_E3_EXPECT / OB_AIE_E2_EXPECT are the
 * same bits, so a validated E3 already implies CORE_UP. The explicit
 * re-evaluation exists so a future mask edit cannot silently accept an
 * "enabled" core that does not satisfy the vendor predicate.
 */
static inline bool ob_aie_core_up(u32 ioc, u32 rctl)
{
	return ((ioc & OB_AIE_IOCTL_CTRL_MASK) == OB_AIE_E3_EXPECT) &&
	       ((rctl & OB_AIE_RCTL_CTRL_MASK) == OB_AIE_E2_EXPECT);
}

/*
 * Pre-state admission. Conservative by construction:
 *   RESET_ST  == 0   no reset transition is in progress;
 *   RESET_CTL.RESET  == 1   the core is held in reset (the only state this
 *                           contract covers, and the only state in which
 *                           bcma_core_disable() writes nothing);
 *   IOCTL CLK|FGC    == 0   the core is not currently clocked.
 * An all-ones readback is rejected earlier as a device-loss indication, so it
 * never reaches this predicate.
 */
static inline bool ob_aie_prestate_ok(u32 rst, u32 rctl, u32 ioc)
{
	return rst == OB_AIE_PRESTATE_RESET_ST &&
	       (rctl & OB_AIE_RCTL_CTRL_MASK) ==
			(OB_AIE_PRESTATE_RESET_CTL & OB_AIE_RCTL_CTRL_MASK) &&
	       (ioc & OB_AIE_IOCTL_CTRL_MASK) ==
			(OB_AIE_PRESTATE_IOCTL & OB_AIE_IOCTL_CTRL_MASK);
}

/*
 * bcma_core_disable()'s own early-return predicate. Kept as a named, tested
 * function because it is the reason this contract contains no disable writes
 * and no RESET_ST poll: it states the condition under which the vendor's
 * disable step performs no hardware operation at all.
 */
static inline bool ob_aie_disable_is_noop(u32 rctl_pre)
{
	return (rctl_pre & OB_AIE_RCTL_CTRL_MASK) != 0u;
}

/* Readback acceptance helpers: only the documented control bits are compared. */
static inline bool ob_aie_accept_e1(u32 v)
{
	return (v & OB_AIE_IOCTL_CTRL_MASK) == OB_AIE_E1_EXPECT;
}

static inline bool ob_aie_accept_e2(u32 v)
{
	return (v & OB_AIE_RCTL_CTRL_MASK) == OB_AIE_E2_EXPECT;
}

static inline bool ob_aie_accept_e3(u32 v)
{
	return (v & OB_AIE_IOCTL_CTRL_MASK) == OB_AIE_E3_EXPECT;
}

/*
 * Entry gate, executed before any hardware call. A missing callback is a
 * build/contract defect, not a device state, and is reported as such so it can
 * never be mistaken for a device-loss result.
 */
static inline bool ob_aie_ops_ok(const struct ob_aie_ops *ops)
{
	return ops && ops->read_reset_st && ops->read_reset_ctl &&
	       ops->read_ioctl && ops->write_ioctl_set_fgc &&
	       ops->write_reset_ctl_deassert && ops->write_ioctl_clr_fgc &&
	       ops->delay_us && ops->log;
}

static inline void ob_aie_result_init(struct ob_aie_result *res)
{
	res->reset_st = 0;
	res->reset_ctl = 0;
	res->ioctl = 0;
	res->have_reset_st = false;
	res->have_reset_ctl = false;
	res->have_ioctl = false;
	res->prestate_ok = false;
	res->e1_ioctl = 0;
	res->e2_reset_ctl = 0;
	res->e3_ioctl = 0;
	res->have_e1 = false;
	res->have_e2 = false;
	res->have_e3 = false;
	res->final_ioctl = 0;
	res->final_reset_ctl = 0;
	res->reads_attempted = 0;
	res->reads_completed = 0;
	res->writes_attempted = 0;
	res->writes_confirmed = 0;
	res->last_completed_step = 0;
	res->failed_step = 0;
	res->failed_op = OB_AIE_OP_NONE;
	res->failed_off = 0;
	res->last_value = 0;
	res->last_error = 0;
	res->suspicious = false;
	res->device_lost = false;
	res->uncertain = false;
	res->core_up = false;
	res->class = OB_AIE_CLASS_PRECHECK_REJECTED;
	res->stop_reason = NULL;
}

static inline void ob_aie_log_ev(const struct ob_aie_ops *ops,
				 enum ob_aie_event ev, u8 seq, u16 off,
				 u32 value, u32 written, int error,
				 const char *reason)
{
	struct ob_aie_log rec;

	rec.seq = seq;
	rec.off = off;
	rec.value = value;
	rec.written = written;
	rec.error = error;
	rec.reason = reason;
	ops->log(ops->ctx, ev, &rec);
}

/*
 * Terminal stop. Every stop path in this file goes through here, so the
 * classification, the counters and the STOP marker cannot drift apart.
 */
static inline int ob_aie_stop(const struct ob_aie_ops *ops,
			      struct ob_aie_result *res, enum ob_aie_step step,
			      enum ob_aie_op op, u16 off, const char *reason,
			      int error, enum ob_aie_class cls, bool suspicious)
{
	res->failed_step = (u8)step;
	res->failed_op = op;
	res->failed_off = off;
	res->suspicious = suspicious;
	res->class = cls;
	res->stop_reason = reason;
	res->device_lost = (cls == OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED);
	res->uncertain = (cls == OB_AIE_CLASS_TRANSITION_UNCERTAIN);
	res->core_up = false;
	res->last_error = error;
	ob_aie_log_ev(ops, OB_AIE_EV_STOP, (u8)step, off, 0, 0, error, reason);
	return error ? error : OB_AIE_RET_EIO;
}

/*
 * One pre-state observation. Returns 0 only when the accessor succeeded and
 * the value was not the all-ones sentinel. Never issues a write.
 */
static inline int ob_aie_pre_read(const struct ob_aie_ops *ops,
				  struct ob_aie_result *res,
				  enum ob_aie_step step, u16 off,
				  int (*rd)(void *ctx, u32 *val),
				  u32 *dst, bool *have)
{
	u32 val = 0;
	int ret;

	if (ops->dev_lost && ops->dev_lost(ops->ctx))
		return ob_aie_stop(ops, res, step, OB_AIE_OP_READ, off,
				   "device_loss_latched", OB_AIE_RET_EIO,
				   OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED, true);

	/* PRE precedes the accessor invocation (D1). */
	ob_aie_log_ev(ops, OB_AIE_EV_PRE_READ, (u8)step, off, 0, 0, 0, NULL);
	res->reads_attempted++;

	ret = rd(ops->ctx, &val);
	if (ret)
		return ob_aie_stop(ops, res, step, OB_AIE_OP_READ, off,
				   "callback_error", ret,
				   OB_AIE_CLASS_PRECHECK_REJECTED, false);

	res->last_value = val;
	/* Suspicious value is classified BEFORE any success marker (D2). */
	if (val == OB_AIE_ALL_ONES)
		return ob_aie_stop(ops, res, step, OB_AIE_OP_READ, off,
				   "all_ones", OB_AIE_RET_EIO,
				   OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED, true);

	*dst = val;
	*have = true;
	res->reads_completed++;
	ob_aie_log_ev(ops, OB_AIE_EV_POST_READ, (u8)step, off, val, 0, 0, NULL);
	return 0;
}

/*
 * One transition step: PRE -> write -> PRE -> read -> classify.
 *
 * The write is counted as ATTEMPTED before the callback and as CONFIRMED only
 * after the readback matched `mask`/`expect`. A step therefore cannot report
 * success on the strength of an accessor returning (D3, D5).
 */
static inline int ob_aie_transition(const struct ob_aie_ops *ops,
				    struct ob_aie_result *res,
				    enum ob_aie_step step, u16 off,
				    u32 wr_value, u32 mask, u32 expect,
				    int (*wr)(void *ctx),
				    int (*rd)(void *ctx, u32 *val),
				    u32 *dst, bool *have)
{
	u32 val = 0;
	int ret;

	if (ops->dev_lost && ops->dev_lost(ops->ctx))
		return ob_aie_stop(ops, res, step, OB_AIE_OP_WRITE, off,
				   "device_loss_latched", OB_AIE_RET_EIO,
				   OB_AIE_CLASS_TRANSITION_UNCERTAIN, true);

	ob_aie_log_ev(ops, OB_AIE_EV_PRE_WRITE, (u8)step, off, wr_value, 0, 0,
		      NULL);
	res->writes_attempted++;

	ret = wr(ops->ctx);
	if (ret)
		return ob_aie_stop(ops, res, step, OB_AIE_OP_WRITE, off,
				   "callback_error", ret,
				   OB_AIE_CLASS_TRANSITION_UNCERTAIN, false);

	/* The documented readback: exactly one, no extra read for reassurance. */
	ob_aie_log_ev(ops, OB_AIE_EV_PRE_READ, (u8)step, off, 0, 0, 0, NULL);
	res->reads_attempted++;

	ret = rd(ops->ctx, &val);
	if (ret)
		return ob_aie_stop(ops, res, step, OB_AIE_OP_WRITE, off,
				   "callback_error", ret,
				   OB_AIE_CLASS_TRANSITION_UNCERTAIN, false);

	res->last_value = val;
	if (val == OB_AIE_ALL_ONES)
		return ob_aie_stop(ops, res, step, OB_AIE_OP_WRITE, off,
				   "all_ones", OB_AIE_RET_EIO,
				   OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED, true);

	res->reads_completed++;
	ob_aie_log_ev(ops, OB_AIE_EV_POST_READ, (u8)step, off, val, 0, 0, NULL);

	if ((val & mask) != expect)
		return ob_aie_stop(ops, res, step, OB_AIE_OP_WRITE, off,
				   "readback_mismatch", OB_AIE_RET_EIO,
				   OB_AIE_CLASS_TRANSITION_UNCERTAIN, false);

	*dst = val;
	*have = true;
	res->writes_confirmed++;
	res->last_completed_step = (u8)step;
	ob_aie_log_ev(ops, OB_AIE_EV_POST_WRITE, (u8)step, off, val, wr_value,
		      0, NULL);
	return 0;
}

/*
 * The whole transition. Straight-line, bounded, no loop, no retry, no rollback
 * and no cleanup write on any path.
 */
static inline int ob_aie_run(const struct ob_aie_ops *ops, bool entry_dev_lost,
			     struct ob_aie_result *res)
{
	u32 rst = 0, rctl = 0, ioc = 0;
	u32 e1 = 0, e2 = 0, e3 = 0;
	bool h_rst = false, h_rctl = false, h_ioc = false;
	bool h_e1 = false, h_e2 = false, h_e3 = false;
	int ret;

	if (!res)
		return OB_AIE_RET_EINVAL;

	ob_aie_result_init(res);
	if (!ob_aie_ops_ok(ops)) {
		res->stop_reason = "invalid_ops";
		res->last_error = OB_AIE_RET_EINVAL;
		res->class = OB_AIE_CLASS_PRECHECK_REJECTED;
		return OB_AIE_RET_EINVAL;
	}

	ob_aie_log_ev(ops, OB_AIE_EV_BEGIN, 0, 0, 0, 0, 0, NULL);

	/* An already-latched device issues zero hardware callbacks. */
	if (entry_dev_lost) {
		res->failed_step = (u8)OB_AIE_STEP_ADMISSION;
		res->failed_op = OB_AIE_OP_NONE;
		res->class = OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED;
		res->stop_reason = "device_loss_latched";
		res->device_lost = true;
		res->last_error = OB_AIE_RET_EIO;
		ob_aie_log_ev(ops, OB_AIE_EV_STOP, 0, 0, 0, 0,
			      OB_AIE_RET_EIO, "device_loss_latched");
		return OB_AIE_RET_EIO;
	}

	/* --- pre-state: three observations, no write ---------------------- */
	ret = ob_aie_pre_read(ops, res, OB_AIE_STEP_PRE_RESET_ST,
			      OB_AIE_OFF_RESET_ST, ops->read_reset_st,
			      &rst, &h_rst);
	if (ret)
		return ret;
	ret = ob_aie_pre_read(ops, res, OB_AIE_STEP_PRE_RESET_CTL,
			      OB_AIE_OFF_RESET_CTL, ops->read_reset_ctl,
			      &rctl, &h_rctl);
	if (ret)
		return ret;
	ret = ob_aie_pre_read(ops, res, OB_AIE_STEP_PRE_IOCTL,
			      OB_AIE_OFF_IOCTL, ops->read_ioctl, &ioc, &h_ioc);
	if (ret)
		return ret;

	res->reset_st = rst;
	res->reset_ctl = rctl;
	res->ioctl = ioc;
	res->have_reset_st = h_rst;
	res->have_reset_ctl = h_rctl;
	res->have_ioctl = h_ioc;

	if (!ob_aie_prestate_ok(rst, rctl, ioc)) {
		res->prestate_ok = false;
		/* Zero writes have been issued at this point by construction. */
		return ob_aie_stop(ops, res, OB_AIE_STEP_ADMISSION,
				   OB_AIE_OP_NONE, 0, "prestate_unexpected",
				   OB_AIE_RET_EIO,
				   OB_AIE_CLASS_PRECHECK_REJECTED, false);
	}
	res->prestate_ok = true;

	/* --- E1: clock on, force-gated ------------------------------------ */
	ret = ob_aie_transition(ops, res, OB_AIE_STEP_E1, OB_AIE_OFF_IOCTL,
				OB_AIE_E1_IOCTL_VALUE,
				OB_AIE_IOCTL_CTRL_MASK, OB_AIE_E1_EXPECT,
				ops->write_ioctl_set_fgc, ops->read_ioctl,
				&e1, &h_e1);
	if (ret)
		return ret;

	/* --- E2: release reset -------------------------------------------- */
	ret = ob_aie_transition(ops, res, OB_AIE_STEP_E2, OB_AIE_OFF_RESET_CTL,
				OB_AIE_E2_RCTL_VALUE,
				OB_AIE_RCTL_CTRL_MASK, OB_AIE_E2_EXPECT,
				ops->write_reset_ctl_deassert,
				ops->read_reset_ctl, &e2, &h_e2);
	if (ret)
		return ret;

	/* The documented delay between E2 and E3. */
	ops->delay_us(ops->ctx, OB_AIE_DELAY_US);
	ob_aie_log_ev(ops, OB_AIE_EV_DELAY, (u8)OB_AIE_STEP_E2, 0,
		      OB_AIE_DELAY_US, 0, 0, NULL);

	/* --- E3: drop the force gate, keep the clock ---------------------- */
	ret = ob_aie_transition(ops, res, OB_AIE_STEP_E3, OB_AIE_OFF_IOCTL,
				OB_AIE_E3_IOCTL_VALUE,
				OB_AIE_IOCTL_CTRL_MASK, OB_AIE_E3_EXPECT,
				ops->write_ioctl_clr_fgc, ops->read_ioctl,
				&e3, &h_e3);
	if (ret)
		return ret;

	ops->delay_us(ops->ctx, OB_AIE_DELAY_US);
	ob_aie_log_ev(ops, OB_AIE_EV_DELAY, (u8)OB_AIE_STEP_E3, 0,
		      OB_AIE_DELAY_US, 0, 0, NULL);

	res->e1_ioctl = e1;
	res->e2_reset_ctl = e2;
	res->e3_ioctl = e3;
	res->have_e1 = h_e1;
	res->have_e2 = h_e2;
	res->have_e3 = h_e3;
	res->final_ioctl = e3;
	res->final_reset_ctl = e2;

	/* Vendor predicate, re-evaluated on the final VALID readbacks (D5). */
	if (!ob_aie_core_up(e3, e2))
		return ob_aie_stop(ops, res, OB_AIE_STEP_E3, OB_AIE_OP_WRITE,
				   OB_AIE_OFF_IOCTL, "core_predicate_false",
				   OB_AIE_RET_EIO,
				   OB_AIE_CLASS_TRANSITION_UNCERTAIN, false);

	res->core_up = true;
	res->class = OB_AIE_CLASS_CORE_UP;
	res->stop_reason = NULL;
	res->last_error = 0;

	/*
	 * The contract ends here in BOTH core states: D11_CORE_UP does not
	 * authorise a functional-core read (D4). Nothing below this line may
	 * touch hardware.
	 */
	ob_aie_log_ev(ops, OB_AIE_EV_END, 0, 0, 0, 0, 0, NULL);
	return OB_AIE_RET_OK;
}

/* Static tokens: the host tests pin these exact strings. */
#define OB_AIE_TOKEN_CORE_UP		"D11_WRAPPER_CORE_UP"
#define OB_AIE_TOKEN_PRECHECK		"ENABLE_PRECHECK_REJECTED"
#define OB_AIE_TOKEN_DEVICE_LOSS	"ENABLE_DEVICE_LOSS_SUSPECTED"
#define OB_AIE_TOKEN_UNCERTAIN		"ENABLE_TRANSITION_UNCERTAIN"

static inline const char *ob_aie_class_token(enum ob_aie_class c)
{
	switch (c) {
	case OB_AIE_CLASS_CORE_UP:
		return OB_AIE_TOKEN_CORE_UP;
	case OB_AIE_CLASS_PRECHECK_REJECTED:
		return OB_AIE_TOKEN_PRECHECK;
	case OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED:
		return OB_AIE_TOKEN_DEVICE_LOSS;
	case OB_AIE_CLASS_TRANSITION_UNCERTAIN:
		return OB_AIE_TOKEN_UNCERTAIN;
	}
	return OB_AIE_TOKEN_UNCERTAIN;
}

/* ------------------------------------------------------------------------
 * Lifecycle policy
 *
 * A transition that issued any write leaves hardware in a state this driver
 * does not own a teardown for: nothing here ever disables the core again, and
 * no cleanup write is issued after a failure. Ownership is therefore RETAINed
 * for every class except a provably write-free pre-check rejection.
 *
 * RETAIN means: the device stays bound, the module stays loaded, and the core
 * stays in whatever state the transition left it in. Removal and re-probe are
 * not recovery actions; rebooting is.
 *
 * RETAIN is this driver's policy, not a kernel guarantee. It keeps the device
 * bound and the module loaded, but it does not make the binding survive
 * `rmmod`: unloading releases the driver's record of the post-write state
 * while the hardware stays in it, with no owner and no teardown contract.
 * Module unload after any enable write is therefore forbidden, and a reboot is
 * the only documented recovery.
 * ------------------------------------------------------------------------ */
enum ob_aie_remove_policy {
	OB_AIE_REMOVE_CLEAN = 0,	/* no write was ever issued */
	OB_AIE_REMOVE_RETAIN = 1,	/* ownership retained, no teardown */
};

static inline enum ob_aie_remove_policy
ob_aie_remove_policy_of(const struct ob_aie_result *res)
{
	if (!res)
		return OB_AIE_REMOVE_RETAIN;
	if (res->writes_attempted > 0)
		return OB_AIE_REMOVE_RETAIN;
	if (res->device_lost || res->uncertain)
		return OB_AIE_REMOVE_RETAIN;
	return OB_AIE_REMOVE_CLEAN;
}

#endif /* _OB_AI_ENABLE_ONLY_H_ */

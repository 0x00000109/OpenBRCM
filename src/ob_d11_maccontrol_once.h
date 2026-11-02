/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — rev42_d11_maccontrol_once (Phase 38A).
 *
 * WHAT THIS FILE IS
 * -----------------
 * The complete decision, gate and single-access logic of
 * `rev42_d11_maccontrol_once`, as a pure header so the host tests execute the
 * SAME code the module runs. It composes the verified Phase 37B wrapper-enable
 * engine (ob_ai_enable_only.h) with exactly ONE D11 functional-core read.
 *
 * THE REGISTER
 * ------------
 *   D11 MACCONTROL, offset 0x0120, 32 bits, functional-core window. Provenance
 *   is recorded in REGISTER_PROVENANCE.md of the Phase 38A audit directory:
 *     - b43:      drivers/net/wireless/broadcom/b43/b43.h:41 —
 *                 `#define B43_MMIO_MACCTL 0x120` ("MAC control")
 *     - brcmsmac: drivers/net/wireless/broadcom/brcm80211/brcmsmac/d11.h:125 —
 *                 `u32 maccontrol;` with the offset comment "0x120" inside
 *                 struct d11regs, whose first field is register 0x000
 *   The access goes through bcma_read32(core, offset), i.e. the BCMA-owned
 *   core window selected from core->addr — NOT the AI wrapper window
 *   core->wrap. 0x0120 is not a wrapper offset: the wrapper window carries
 *   BCMA_IOCTL 0x0408, BCMA_RESET_CTL 0x0800 and BCMA_RESET_ST 0x0804, and the
 *   Phase 37B run observed exactly those three.
 *
 * THE SURFACE IS ENUMERATED, NOT PARAMETERISED
 * --------------------------------------------
 * `struct ob_d11m_ops` has exactly one hardware callback:
 * `read_maccontrol`. There is no write callback, no `read32(off)` escape
 * hatch, and no second read callback, so a second functional access or a
 * different offset is not expressible without editing this header. The
 * forbidden offsets (0x0FFC, 0x03E0), PHY/radio registers, SHM payload
 * registers and every write are unreachable by construction.
 *
 * WHY THIS IS SAFE TO CLASSIFY FROM ONE READ
 * ------------------------------------------
 * MACCONTROL is not read-to-clear: upstream b43/brcmsmac treat it as an
 * ordinary read/write control register (all uses are plain reads or
 * read-modify-write). No second access is needed or permitted to classify the
 * first result. An all-ones value is NOT an observation: it is the
 * inaccessible-window/device-loss indication used elsewhere in this driver
 * (OB_GUARD_DEAD32), and it is classified before any success marker.
 *
 * OUTCOMES (exhaustive)
 * ---------------------
 *   D11_ADMISSION_REJECTED
 *       A mode-selection or entry precondition failed. No hardware access.
 *   D11_WRAPPER_ENABLE_NOT_CONFIRMED
 *       The Phase 37B wrapper transition did not produce a verified
 *       D11_WRAPPER_CORE_UP result with the documented counters, or a
 *       device-loss/fatal state is already latched. NO functional read.
 *   D11_MACCONTROL_READABLE
 *       Exactly one 32-bit read completed with a finite value and no
 *       device-loss latch was raised. This demonstrates ONE readable
 *       functional register — nothing more (not firmware, PHY, radio, DMA,
 *       IRQ or MAC operation).
 *   D11_WINDOW_INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED
 *       The single read returned 0xffffffff or the trusted accessor latched
 *       device loss. Fail-stop: no further access of any kind.
 *   D11_FUNCTIONAL_READ_UNCERTAIN
 *       The callback failed or the observation cannot be classified. The read
 *       counts as ATTEMPTED and not as COMPLETED. Fail-stop.
 *
 * LIFECYCLE
 * ---------
 * Any state reached after the first wrapper write is retained: no teardown,
 * no disable/reset write, no re-probe. A retained device is left exactly as
 * the transition left it and a reboot is the only documented recovery.
 */
#ifndef _OB_D11_MACCONTROL_ONCE_H_
#define _OB_D11_MACCONTROL_ONCE_H_

#include <linux/types.h>
#include "ob_ai_enable_only.h"

/*
 * The POST line is shared between the kernel adapter and the host tests, so the
 * wording defect this phase is correcting cannot live in only one of them
 * (Amendment 01, Task 1/Task 2).
 *
 * POST is emitted from ob_d11m_run() BEFORE the value and the device-loss latch
 * are classified. It must therefore never say "ok": an all-ones value is still
 * on its way to D11_WINDOW_INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED. The final
 * classification is printed by the caller after ob_d11m_run() returns.
 */
#ifdef __KERNEL__
#include <linux/kernel.h>		/* snprintf */
#else
#include <stdio.h>			/* snprintf */
#endif

#define OB_D11M_FMT_POST_PENDING \
	"rev42-d11-mc: POST reg=D11_MACCONTROL status=pending_classification value=0x%08x (observation only; not yet classified)\n"

static inline int ob_d11m_format_post_read(char *buf, unsigned int cap, u32 value)
{
	if (!buf || cap == 0u)
		return -1;
	return snprintf(buf, cap, OB_D11M_FMT_POST_PENDING, value);
}

/* D11 functional-core MACCONTROL (b43 B43_MMIO_MACCTL / brcmsmac d11regs). */
#define OB_D11M_OFF_MACCONTROL		0x0120u
#define OB_D11M_WIDTH_BITS		32u
#define OB_D11M_ALL_ONES		0xffffffffu

/* Ceilings. One functional read; zero functional writes are expressible. */
#define OB_D11M_MAX_FUNCTIONAL_READS	1u
#define OB_D11M_FUNCTIONAL_WRITES	0u

/* The exact Phase 37B counters the B2 gate requires. */
#define OB_D11M_WRAPPER_READS_EXPECTED	OB_AIE_MAX_READS	/* 6 */
#define OB_D11M_WRAPPER_WRITES_EXPECTED	OB_AIE_MAX_WRITES	/* 3 */

/* Totals of the complete successful path (driver-initiated register access). */
#define OB_D11M_TOTAL_READS			7u	/* 6 wrapper + 1 D11 */
#define OB_D11M_TOTAL_WRITES		3u	/* 3 wrapper + 0 D11 */

enum ob_d11m_class {
	OB_D11M_CLASS_ADMISSION_REJECTED = 0,
	OB_D11M_CLASS_WRAPPER_NOT_CONFIRMED,
	OB_D11M_CLASS_READABLE,
	OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS,
	OB_D11M_CLASS_READ_UNCERTAIN,
};

enum ob_d11m_event {
	OB_D11M_EV_BEGIN = 0,
	OB_D11M_EV_GATE,
	OB_D11M_EV_PRE_READ,
	OB_D11M_EV_POST_READ,
	OB_D11M_EV_STOP,
	OB_D11M_EV_END,
};

struct ob_d11m_log {
	u16 off;		/* functional offset, 0 when not applicable */
	u32 value;		/* POST_READ only */
	u8 reads_attempted;
	u8 reads_completed;
	int error;		/* STOP only */
	bool dev_lost;
	bool gate_ok;		/* GATE only */
	const char *reason;	/* static token */
};

/*
 * Backend: ONE callback, fixed register, fixed width, read only. `ctx` is
 * `struct ob_hw *` in the kernel and a test context on the host.
 */
struct ob_d11m_ops {
	void *ctx;
	int  (*read_maccontrol)(void *ctx, u32 *val);
	void (*log)(void *ctx, enum ob_d11m_event ev,
		    const struct ob_d11m_log *rec);
	bool (*dev_lost)(void *ctx);	/* optional; evaluated AFTER the read */
};

struct ob_d11m_result {
	bool gate_evaluated;
	bool gate_ok;
	const char *gate_reason;	/* static token */
	u8 reads_attempted;
	u8 reads_completed;
	u8 writes_attempted;	/* always 0: no write callback exists */
	u32 value;
	bool have_value;
	bool suspicious;
	bool dev_lost;
	int last_error;
	enum ob_d11m_class class;
	const char *stop_reason;	/* static token, NULL on READABLE */
};

/* Static tokens: the host tests pin these exact strings. */
#define OB_D11M_TOKEN_READABLE		"D11_MACCONTROL_READABLE"
#define OB_D11M_TOKEN_INACCESSIBLE	\
	"D11_WINDOW_INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED"
#define OB_D11M_TOKEN_UNCERTAIN		"D11_FUNCTIONAL_READ_UNCERTAIN"
#define OB_D11M_TOKEN_WRAPPER_NOT_CONFIRMED	\
	"D11_WRAPPER_ENABLE_NOT_CONFIRMED"
#define OB_D11M_TOKEN_ADMISSION_REJECTED	"D11_ADMISSION_REJECTED"

static inline const char *ob_d11m_class_token(enum ob_d11m_class c)
{
	switch (c) {
	case OB_D11M_CLASS_READABLE:
		return OB_D11M_TOKEN_READABLE;
	case OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS:
		return OB_D11M_TOKEN_INACCESSIBLE;
	case OB_D11M_CLASS_READ_UNCERTAIN:
		return OB_D11M_TOKEN_UNCERTAIN;
	case OB_D11M_CLASS_WRAPPER_NOT_CONFIRMED:
		return OB_D11M_TOKEN_WRAPPER_NOT_CONFIRMED;
	case OB_D11M_CLASS_ADMISSION_REJECTED:
		return OB_D11M_TOKEN_ADMISSION_REJECTED;
	}
	return OB_D11M_TOKEN_UNCERTAIN;
}

/* Retained after any wrapper write attempt; never a cleanup decision. */
static inline bool ob_d11m_retained(const struct ob_aie_result *wrapper)
{
	return !wrapper || wrapper->writes_attempted > 0 ||
	       wrapper->device_lost || wrapper->uncertain;
}

/*
 * Stage B2 — functional-access gate.
 *
 * An `insmod` success or a bare `ob_ai_enable_test()` return value is NOT
 * sufficient: the Phase 37B post-write paths deliberately return success to
 * retain the bound device. This predicate therefore requires the ACTUAL
 * verified result, including every documented counter, and refuses when a
 * device-loss or fatal lifecycle state is already present.
 */
static inline bool
ob_d11m_wrapper_gate(const struct ob_aie_result *w, bool dev_lost, bool fatal)
{
	if (!w)
		return false;
	if (w->class != OB_AIE_CLASS_CORE_UP || !w->core_up)
		return false;
	if (w->writes_attempted != OB_D11M_WRAPPER_WRITES_EXPECTED ||
	    w->writes_confirmed != OB_D11M_WRAPPER_WRITES_EXPECTED)
		return false;
	if (w->reads_attempted != OB_D11M_WRAPPER_READS_EXPECTED ||
	    w->reads_completed != OB_D11M_WRAPPER_READS_EXPECTED)
		return false;
	if (dev_lost || fatal || w->device_lost || w->uncertain)
		return false;
	return true;
}

/* Why the gate refused; static token for the log and the host tests. */
static inline const char *
ob_d11m_gate_reason(const struct ob_aie_result *w, bool dev_lost, bool fatal)
{
	if (!w)
		return "wrapper_result_missing";
	if (dev_lost || w->device_lost)
		return "device_loss_latched";
	if (fatal)
		return "fatal_lifecycle_state";
	if (w->class != OB_AIE_CLASS_CORE_UP || !w->core_up)
		return "wrapper_class_not_core_up";
	if (w->writes_attempted != OB_D11M_WRAPPER_WRITES_EXPECTED ||
	    w->writes_confirmed != OB_D11M_WRAPPER_WRITES_EXPECTED)
		return "wrapper_write_counters_mismatch";
	if (w->reads_attempted != OB_D11M_WRAPPER_READS_EXPECTED ||
	    w->reads_completed != OB_D11M_WRAPPER_READS_EXPECTED)
		return "wrapper_read_counters_mismatch";
	return "gate_admitted";
}

static inline bool ob_d11m_ops_ok(const struct ob_d11m_ops *ops)
{
	return ops && ops->read_maccontrol && ops->log;
}

static inline void ob_d11m_result_init(struct ob_d11m_result *res)
{
	res->gate_evaluated = false;
	res->gate_ok = false;
	res->gate_reason = "not_evaluated";
	res->reads_attempted = 0;
	res->reads_completed = 0;
	res->writes_attempted = 0;
	res->value = 0;
	res->have_value = false;
	res->suspicious = false;
	res->dev_lost = false;
	res->last_error = 0;
	res->class = OB_D11M_CLASS_ADMISSION_REJECTED;
	res->stop_reason = "not_evaluated";
}

static inline void ob_d11m_log_ev(const struct ob_d11m_ops *ops,
				  enum ob_d11m_event ev,
				  const struct ob_d11m_result *res, u16 off,
				  u32 value, const char *reason)
{
	struct ob_d11m_log rec;

	rec.off = off;
	rec.value = value;
	rec.reads_attempted = res->reads_attempted;
	rec.reads_completed = res->reads_completed;
	rec.error = res->last_error;
	rec.dev_lost = res->dev_lost;
	rec.gate_ok = res->gate_ok;
	rec.reason = reason;
	ops->log(ops->ctx, ev, &rec);
}

/* Every stop path goes through here so class, counters and log cannot drift. */
static inline int ob_d11m_stop(const struct ob_d11m_ops *ops,
			       struct ob_d11m_result *res,
			       enum ob_d11m_class cls, const char *reason,
			       int error)
{
	res->class = cls;
	res->stop_reason = reason;
	res->last_error = error;
	ob_d11m_log_ev(ops, OB_D11M_EV_STOP, res, 0, 0, reason);
	return error ? error : OB_AIE_RET_EIO;
}

/*
 * Stage B3/B4 — gate, exactly one read, immediate classification, stop.
 *
 * There is no loop, no retry, no rollback, no second read and no cleanup
 * access anywhere below. `wrapper` is the ACTUAL Phase 37B result.
 */
static inline int ob_d11m_run(const struct ob_d11m_ops *ops,
			      const struct ob_aie_result *wrapper,
			      bool dev_lost, bool fatal,
			      struct ob_d11m_result *res)
{
	u32 val = 0;
	int ret;

	if (!res)
		return OB_AIE_RET_EINVAL;

	ob_d11m_result_init(res);

	/* A contract defect is not a device state and issues no access. */
	if (!ob_d11m_ops_ok(ops)) {
		res->class = OB_D11M_CLASS_ADMISSION_REJECTED;
		res->stop_reason = "invalid_ops";
		res->last_error = OB_AIE_RET_EINVAL;
		return OB_AIE_RET_EINVAL;
	}

	ob_d11m_log_ev(ops, OB_D11M_EV_BEGIN, res, 0, 0, NULL);

	/* --- B2: the functional-access gate ------------------------------- */
	res->gate_evaluated = true;
	res->gate_ok = ob_d11m_wrapper_gate(wrapper, dev_lost, fatal);
	res->gate_reason = ob_d11m_gate_reason(wrapper, dev_lost, fatal);
	ob_d11m_log_ev(ops, OB_D11M_EV_GATE, res, 0, 0, res->gate_reason);

	if (!res->gate_ok)
		return ob_d11m_stop(ops, res,
				    OB_D11M_CLASS_WRAPPER_NOT_CONFIRMED,
				    res->gate_reason, OB_AIE_RET_EIO);

	/* --- B3: exactly one functional read ------------------------------ */
	ob_d11m_log_ev(ops, OB_D11M_EV_PRE_READ, res,
		       OB_D11M_OFF_MACCONTROL, 0, "single_functional_read");
	res->reads_attempted = 1;

	ret = ops->read_maccontrol(ops->ctx, &val);
	if (ret) {
		res->last_error = ret;
		return ob_d11m_stop(ops, res, OB_D11M_CLASS_READ_UNCERTAIN,
				    "callback_error", ret);
	}

	res->value = val;
	res->have_value = true;
	res->reads_completed = 1;
	ob_d11m_log_ev(ops, OB_D11M_EV_POST_READ, res,
		       OB_D11M_OFF_MACCONTROL, val, NULL);

	/* An all-ones observation is a loss indication, never a success (D12). */
	if (val == OB_D11M_ALL_ONES) {
		res->suspicious = true;
		res->dev_lost = true;
		return ob_d11m_stop(ops, res,
				    OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS,
				    "all_ones_window_inaccessible",
				    OB_AIE_RET_EIO);
	}

	/* The trusted accessor latches device loss itself; honour it. */
	if (ops->dev_lost && ops->dev_lost(ops->ctx)) {
		res->suspicious = true;
		res->dev_lost = true;
		return ob_d11m_stop(ops, res,
				    OB_D11M_CLASS_WINDOW_INACCESSIBLE_OR_LOSS,
				    "device_loss_latched_by_accessor",
				    OB_AIE_RET_EIO);
	}

	res->class = OB_D11M_CLASS_READABLE;
	res->stop_reason = NULL;
	res->last_error = 0;
	ob_d11m_log_ev(ops, OB_D11M_EV_END, res, 0, 0, NULL);
	return OB_AIE_RET_OK;
}

#endif /* _OB_D11_MACCONTROL_ONCE_H_ */

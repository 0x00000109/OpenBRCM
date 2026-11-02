/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — rev42 AI-wrapper-only diagnostic (Phase 36Z).
 *
 * THE DEFECT THIS FILE EXISTS TO CLOSE
 * ------------------------------------
 * The retired `rev42_ai_probe_only` diagnostic read the D11 functional-core
 * window (core->addr + 0x0FFC) although the same run had just observed the
 * core held in reset (RESET_ST=0x0, RESET_CTL=0x1, IOCTL=0x0). In the single
 * live experiment that read preceded loss of PCI configuration-space access
 * for the endpoint and its upstream port. The electrical cause is UNKNOWN and
 * is not claimed here.
 *
 * CONTRACT (exact, bounded, no extension without a new phase)
 * -----------------------------------------------------------
 * The wrapper-only diagnostic may perform AT MOST THREE device-register
 * observations, all in the BCMA-selected AI wrapper window (core->wrap):
 *
 *   seq 1  RESET_ST   0x0804
 *   seq 2  RESET_CTL  0x0800
 *   seq 3  IOCTL      0x0408
 *
 * There is no fourth access, no repeat, no poll, and NO functional-core
 * window in this contract at all: `enum ob_aiw_window` has exactly one legal
 * member, and the backend refuses anything else. The 0x0FFC core read, PCI
 * config readback, BAR remapping, resets, enables, firmware, DMA, IRQ, PHY,
 * radio and mac80211 are not merely "not called" — they are not expressible
 * through `struct ob_aiw_ops`.
 *
 * BCMA SIDE EFFECT (not hidden)
 * -----------------------------
 * ob_axi_read32() -> bcma_aread32() -> bcma_host_pci_switch_core() writes the
 * PCI BAR0 window selector registers -- 0x80 (BCMA_PCI_BAR0_WIN) = core->addr,
 * and 0x70 (BCMA_PCIE2_BAR0_WIN2) or 0xAC (BCMA_PCI_BAR0_WIN2) = core->wrap,
 * selected by bus->host_is_pcie2 -- whenever BCMA's mapped_core differs from
 * this core. So the wrapper-only mode is NOT physically write-free: it issues
 * two config-space writes per window switch, through BCMA, before the first
 * read. That ownership is deliberately not bypassed. See BCMA_CALLGRAPH.json.
 * (Amendment 01 corrected this paragraph: the second selector is 0x70 or 0xAC,
 * never 0x74.)
 *
 * STOP RULES
 * ----------
 *   - device-loss latch already set at entry        -> no callback at all
 *   - latch set between reads                       -> no further read
 *   - a read callback returns an error              -> stop, propagate it
 *   - a read returns 0xffffffff for these three
 *     known control/status registers                -> suspicious, stop, latch
 *
 * 0xffffffff is treated as a suspicious device-loss indication ONLY for these
 * three registers. It is never generalised to payload registers, and no other
 * value is classified as invalid: a plausible value is an observation, not a
 * proof that the link is stable.
 *
 * LOGGING ORDER (the retired mode logged PRE after the access)
 * ------------------------------------------------------------
 * Every access produces PRE -> (callback) -> POST when the value is a valid
 * observation, and PRE -> STOP when the callback failed OR the value is the
 * all-ones sentinel. A suspicious readback is never represented as
 * `POST status=ok`: it is classified before any POST marker, so the only
 * markers for it are PRE and STOP reason=all_ones. `END ...
 * WRAPPER_ONLY_COMPLETE` is emitted only after all three observations returned
 * non-suspicious values. The token deliberately says nothing about D11,
 * firmware, PHY, DMA, channels or Wi-Fi.
 *
 * ATTEMPTS vs COMPLETED OBSERVATIONS
 * ----------------------------------
 * `reads_attempted` counts the PRE-marked accessor invocations (a callback
 * error or an all-ones value still counts as an attempt). `reads_completed`
 * counts only accessor calls that returned a non-suspicious value and were
 * recorded. A failure on seq 2 therefore reports attempted=2, completed<=1,
 * never attempted=1.
 *
 * This header is pure: no kernel API beyond fixed-width types, so the host
 * tests execute the SAME decision and sequencing code the module runs.
 */
#ifndef _OB_AI_WRAPPER_ONLY_H_
#define _OB_AI_WRAPPER_ONLY_H_

#include <linux/types.h>

/* AI-wrapper window offsets (offsets inside a BCMA-selected window). */
#define OB_AIW_OFF_IOCTL	0x0408u	/* wrapper IOCTL */
#define OB_AIW_OFF_RESET_CTL	0x0800u	/* wrapper RESET_CTL */
#define OB_AIW_OFF_RESET_ST	0x0804u	/* wrapper RESET_ST */

#define OB_AIW_MAX_READS	3u
#define OB_AIW_ALL_ONES		0xffffffffu

/*
 * Return codes. Numeric literals (not -EIO & friends) keep this header
 * compilable in the host test harness, which has no kernel errno headers.
 * Values equal the Linux errno values used by the kernel side.
 */
#define OB_AIW_RET_OK		0
#define OB_AIW_RET_EIO		(-5)	/* -EIO */
#define OB_AIW_RET_ENODEV	(-19)	/* -ENODEV */
#define OB_AIW_RET_EINVAL	(-22)	/* -EINVAL */
#define OB_AIW_RET_EOPNOTSUPP	(-95)	/* -EOPNOTSUPP */

/*
 * The ONLY windows this contract can address. `OB_AIW_WIN_MAX` exists so a
 * backend can reject an out-of-contract value; it is never a valid argument.
 */
enum ob_aiw_window {
	OB_AIW_WIN_WRAPPER = 0,
	OB_AIW_WIN_MAX,
};

enum ob_aiw_event {
	OB_AIW_EV_BEGIN = 0,
	OB_AIW_EV_PRE,
	OB_AIW_EV_POST,
	OB_AIW_EV_STOP,
	OB_AIW_EV_END,
};

/* Machine-readable log record; the backend owns the formatting. */
struct ob_aiw_log {
	u8 seq;			/* 1..3, 0 for BEGIN/END */
	u16 off;		/* wrapper offset, 0 when not applicable */
	u32 value;		/* POST only */
	int error;		/* STOP only */
	const char *reason;	/* STOP only; static string */
};

/*
 * Backend. `ctx` is `struct ob_hw *` in the kernel and a test context on the
 * host. There is no write callback and no config-space callback: neither can
 * be issued by this module's diagnostic logic.
 */
struct ob_aiw_ops {
	void *ctx;
	int  (*read32)(void *ctx, enum ob_aiw_window win, u16 off, u32 *val);
	void (*log)(void *ctx, enum ob_aiw_event ev, const struct ob_aiw_log *rec);
	bool (*dev_lost)(void *ctx);	/* optional; checked before every read */
};

struct ob_aiw_result {
	u32 ioctl;
	u32 reset_ctl;
	u32 reset_st;
	bool have_ioctl;
	bool have_reset_ctl;
	bool have_reset_st;
	bool core_up;		/* vendor predicate; meaningful only when complete */
	bool core_in_reset;	/* complete sequence, predicate false */
	bool suspicious;	/* an all-ones read was observed */
	bool fatal;		/* latch the device-loss state, retain the device */
	bool complete;		/* all three observations returned */
	u8 reads_attempted;	/* PRE emitted and accessor invoked */
	u8 reads_completed;	/* non-suspicious value recorded */
	u8 failed_seq;		/* 1..3, 0 when none */
	u16 failed_off;
	int last_error;		/* callback error, 0 when none */
	const char *stop_reason;	/* static token, NULL on success */
};

/*
 * Vendor ai_iscoreup predicate: (IOCTL & 0x3) == 0x1 && (RESET_CTL & 0x1) == 0.
 * MUST stay identical to ob_diag_ai_iscoreup() in ob_diag.h; the host test
 * pins both over every combination of the two low bits.
 */
static inline bool ob_aiw_core_up(u32 ioc, u32 rctl)
{
	return ((ioc & 0x3u) == 0x1u) && ((rctl & 0x1u) == 0x0u);
}

/*
 * Legacy-mode quarantine predicate. The retired `rev42_ai_probe_only` value is
 * rejected unconditionally — including when it is combined with the new
 * option — and the rejection happens before any allocation, MMIO or BCMA
 * access. Pure so the driver decision itself is host-tested.
 */
static inline bool ob_aiw_legacy_retired(bool rev42_ai_probe_only)
{
	return rev42_ai_probe_only;
}

/*
 * Entry gate, executed by the kernel adapter before any BCMA/MMIO call.
 *
 * The wrapper accessor needs the BCMA core (the wrapper window lives in the
 * LDPC/PCIe2 wrapper of that core) and the bus that owns it; without either,
 * the observation is not expressible and must not issue a callback. A missing
 * callback is a build/contract bug, not a device state, and is reported
 * separately so it can never be mistaken for a device-loss result.
 *
 * Pure, so Z13/Z14 execute this exact gate rather than a paraphrase of it.
 */
static inline int ob_aiw_entry_check(bool have_hw, bool have_core,
				    bool have_bus,
				    const struct ob_aiw_ops *ops)
{
	if (!have_hw || !have_core || !have_bus)
		return OB_AIW_RET_ENODEV;
	if (!ops || !ops->read32 || !ops->log)
		return OB_AIW_RET_EINVAL;
	return OB_AIW_RET_OK;
}

static inline void ob_aiw_result_init(struct ob_aiw_result *res)
{
	res->ioctl = 0;
	res->reset_ctl = 0;
	res->reset_st = 0;
	res->have_ioctl = false;
	res->have_reset_ctl = false;
	res->have_reset_st = false;
	res->core_up = false;
	res->core_in_reset = false;
	res->suspicious = false;
	res->fatal = false;
	res->complete = false;
	res->reads_attempted = 0;
	res->reads_completed = 0;
	res->failed_seq = 0;
	res->failed_off = 0;
	res->last_error = 0;
	res->stop_reason = NULL;
}

static inline void ob_aiw_log_ev(const struct ob_aiw_ops *ops,
				 enum ob_aiw_event ev, u8 seq, u16 off,
				 u32 value, int error, const char *reason)
{
	struct ob_aiw_log rec;

	rec.seq = seq;
	rec.off = off;
	rec.value = value;
	rec.error = error;
	rec.reason = reason;
	ops->log(ops->ctx, ev, &rec);
}

static inline int ob_aiw_stop(const struct ob_aiw_ops *ops,
			      struct ob_aiw_result *res, u8 seq, u16 off,
			      const char *reason, int error, bool fatal)
{
	res->stop_reason = reason;
	res->failed_seq = seq;
	res->failed_off = off;
	res->last_error = error;
	res->fatal = fatal;
	ob_aiw_log_ev(ops, OB_AIW_EV_STOP, seq, off, 0, error, reason);
	return error ? error : OB_AIW_RET_EIO;
}

/*
 * One bounded observation. Returns 0 only when the value was read, was not the
 * all-ones sentinel, and was recorded. Otherwise the sequence is over.
 */
static inline int ob_aiw_read_one(const struct ob_aiw_ops *ops,
				  struct ob_aiw_result *res, u8 seq, u16 off,
				  u32 *dst, bool *have)
{
	u32 val = 0;
	int ret;

	/* Latch gate: checked BEFORE the PRE marker, so a suppressed access is
	 * never announced as an attempt. */
	if (ops->dev_lost && ops->dev_lost(ops->ctx))
		return ob_aiw_stop(ops, res, seq, off, "device_loss_latched",
				   OB_AIW_RET_EIO, true);

	/* PRE precedes the access (the retired mode logged it afterwards). */
	ob_aiw_log_ev(ops, OB_AIW_EV_PRE, seq, off, 0, 0, NULL);
	/* The accessor is about to be invoked, whether or not it succeeds. */
	res->reads_attempted++;

	ret = ops->read32(ops->ctx, OB_AIW_WIN_WRAPPER, off, &val);
	if (ret) {
		res->failed_seq = seq;
		res->failed_off = off;
		return ob_aiw_stop(ops, res, seq, off, "callback_error", ret,
				   false);
	}

	/*
	 * Classify a suspicious readback BEFORE any POST marker. It is not a
	 * successful observation: it is not recorded in dst/have, it does not
	 * count as completed, and it is never printed as `POST status=ok`. The
	 * only markers for this branch are PRE -> STOP reason=all_ones.
	 */
	if (val == OB_AIW_ALL_ONES) {
		res->suspicious = true;
		return ob_aiw_stop(ops, res, seq, off, "all_ones", OB_AIW_RET_EIO,
				   true);
	}

	*dst = val;
	*have = true;
	res->reads_completed++;

	/* POST follows a completed, non-suspicious observation. */
	ob_aiw_log_ev(ops, OB_AIW_EV_POST, seq, off, val, 0, NULL);

	return 0;
}

/*
 * The whole wrapper-only diagnostic. No bus, no core: the backend supplied by
 * the caller already binds the BCMA wrapper accessor. Everything below is
 * straight-line and bounded by OB_AIW_MAX_READS.
 */
static inline int ob_aiw_run(const struct ob_aiw_ops *ops, bool entry_dev_lost,
			     struct ob_aiw_result *res)
{
	u32 ioctl = 0, rctl = 0, rst = 0;
	bool have_ioc = false, have_rctl = false, have_rst = false;
	int ret;

	if (!res)
		return OB_AIW_RET_EINVAL;
	ob_aiw_result_init(res);
	if (!ops || !ops->read32 || !ops->log) {
		res->stop_reason = "invalid_ops";
		res->last_error = OB_AIW_RET_EINVAL;
		return OB_AIW_RET_EINVAL;
	}

	ob_aiw_log_ev(ops, OB_AIW_EV_BEGIN, 0, 0, 0, 0, NULL);

	/* Entry gate: an already-latched device issues zero hardware callbacks. */
	if (entry_dev_lost)
		return ob_aiw_stop(ops, res, 0, 0, "device_loss_latched",
				   OB_AIW_RET_EIO, true);

	ret = ob_aiw_read_one(ops, res, 1, OB_AIW_OFF_RESET_ST, &rst, &have_rst);
	if (ret)
		return ret;
	ret = ob_aiw_read_one(ops, res, 2, OB_AIW_OFF_RESET_CTL, &rctl,
			      &have_rctl);
	if (ret)
		return ret;
	ret = ob_aiw_read_one(ops, res, 3, OB_AIW_OFF_IOCTL, &ioctl, &have_ioc);
	if (ret)
		return ret;

	res->reset_st = rst;
	res->reset_ctl = rctl;
	res->ioctl = ioctl;
	res->have_reset_st = have_rst;
	res->have_reset_ctl = have_rctl;
	res->have_ioctl = have_ioc;
	res->core_up = ob_aiw_core_up(ioctl, rctl);
	res->core_in_reset = !res->core_up;
	res->complete = (res->reads_completed == OB_AIW_MAX_READS);

	/* Core-up does NOT authorise a functional-core read. The contract ends
	 * at the wrapper boundary in both cases. */
	ob_aiw_log_ev(ops, OB_AIW_EV_END, 0, 0, 0, 0, NULL);
	return OB_AIW_RET_OK;
}

/*
 * The one static summary token the kernel side prints. Kept in the header so
 * the host tests assert the exact strings the module emits.
 */
#define OB_AIW_TOKEN_COMPLETE		"WRAPPER_ONLY_COMPLETE"
#define OB_AIW_TOKEN_CORE_IN_RESET	"D11_CORE_IN_RESET"
#define OB_AIW_TOKEN_CORE_UP		"D11_CORE_UP_WRAPPER_ONLY"
#define OB_AIW_TOKEN_STOP		"STOP"

#endif /* _OB_AI_WRAPPER_ONLY_H_ */

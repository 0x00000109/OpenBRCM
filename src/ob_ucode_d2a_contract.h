/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 rev42 ucode upload + PSM start (M3.4D2A), Phase 38C contract.
 *
 * WHAT THIS FILE IS
 * -----------------
 * The complete sequencing and failure logic of the existing D2A path, as a
 * pure header, so the host tests execute the SAME state machine the module
 * runs. `src/ob_ucode.c` is a thin kernel adapter over `ob_d2a_run()`.
 *
 * The recovered register sequence is NOT changed. It is the vendor order the
 * project already implemented, preserved word for word:
 *
 *   prep (caller)                                          -- wrapper window
 *   read  MACCONTROL 0x0120                                -- functional
 *   write MACCONTROL = (old & ~0xffffffff) | 0x04000404
 *   read  MACCONTROL (readback, validated)
 *   write OBJADDR    = 0x03000000 | 0x00000000  (auto-inc, UCM)
 *   read  OBJADDR    (vendor barrier readback)
 *   write OBJDATA    0x0164, 10850 times (raw LE32 words)
 *   write MACINTSTATUS 0x0128 = 0xffffffff       (clear stale state)
 *   write MACCONTROL = (old & ~0xffffffff) | 0x04020402
 *   read  MACCONTROL (readback, validated)
 *   poll  MACINTSTATUS 0x0128 for MI_MACSSPNDD (10us step, bounded)
 *
 * The SHM M_FIFOSIZE0..3 diagnostic is NOT part of this sequence. It is the
 * ucode_test_only tail: `ob_d2a_shm_diag()` below, called ONLY by
 * `ob_ucode_test()` after `ob_d2a_run()` returned 0. Amendment 01 defect A1:
 * the previous revision ran those four observations HERE and again in
 * `ob_ucode_test()`, so ucode_test_only issued eight SHM observations and
 * initvals_test_only issued four before its common-initvals block.
 *
 * WHAT IT FIXES (defects D1..D5 of the Phase 38C brief)
 * ---------------------------------------------------
 *  D1  MACINTSTATUS == 0xffffffff satisfies MI_MACSSPNDD. Fixed: every
 *      MACINTSTATUS observation is classified as SUSPICIOUS BEFORE the
 *      suspended-bit test, so all-ones can never be reported as PSM success.
 *  D2  The upload loop counted issued writes and called them written. Fixed:
 *      `writes_issued` and `writes_confirmed` are separate counters, and
 *      `writes_confirmed` stays 0 because this path has no readback of the
 *      ucode RAM. No log line or field claims a confirmed upload.
 *  D3  No device-loss check during the 10850-word sequence. Fixed: the
 *      monotonic latch is evaluated before every write and every 1024 words;
 *      after a latch no further accessor is invoked, ever.
 *  D4  Non-latching prerequisite reads allowed writes after suspicious
 *      values. Fixed: every control/status read is `read32_trusted`; an
 *      all-ones value stops the run before the next write.
 *  D5  A failure after hardware mutations returned a probe error, releasing
 *      the device. Fixed: the result carries `mutated` / `retained`, and the
 *      caller keeps the device bound with no teardown (RETAIN contract).
 *
 *  Counting (2026-10-25, after the first live run)
 *  ----------------------------------------------
 *  The live log printed `poke=` from `writes_issued` a second time, never
 *  printed the control-write total, and summed fixed control reads and PSM
 *  poll reads into one `reads_trusted`. Three consequences: a phantom field
 *  that looked like an independent measurement, a budget (10 858 writes) that
 *  could only be checked in aggregate, and a read count that could not be
 *  attributed. Now `writes_issued_total` is printed as `writes_total`,
 *  `reads_poll` counts the poll subset, and the fixed cost is derivable as
 *  `reads_trusted - reads_poll`. No counter's meaning changed.
 *
 * Amendment 01 defects fixed here
 * -------------------------------
 *  A1  The four SHM FIFO-size observations lived in the shared path, so the
 *      ucode_test_only caller (which repeated them) performed eight and the
 *      initvals caller performed four before common initvals. Fixed: the
 *      shared path performs none; `ob_d2a_shm_diag()` performs exactly four
 *      and is reachable only from the ucode_test_only tail.
 *  A4  A failing SHM observation was followed by the remaining FIFO reads and
 *      the diagnostic could still be reported as a measurement. Fixed: the
 *      diagnostic stops at the failing observation, reports how many were
 *      completed (`shm_observations`), and sets `shm_read` only after all
 *      four succeeded.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 * No PHY, radio, channel, calibration, DMA, IRQ, RX/TX, firmware download
 * beyond the D11 ucode stream, mac80211, EN_MAC, or initvals. It has no
 * `read32(off)`/`write32(off)` escape hatch: the ops struct exposes exactly
 * the registers this sequence touches, so a different offset is not
 * expressible without editing this header.
 *
 * `ucode_test_only` stops after the SHM diagnostic. Nothing below authorises
 * a later stage.
 *
 * Amendment 01 (A1): the shared path ends at `OB_D2A_STAGE_DONE` with the PSM
 * suspended and performs ZERO SHM observations. `ob_d2a_shm_diag()` is the
 * explicit, fail-stop ucode_test_only tail: exactly four observations, and it
 * stops at the first one that fails (A4).
 */
#ifndef _OB_UCODE_D2A_CONTRACT_H_
#define _OB_UCODE_D2A_CONTRACT_H_

#include <linux/types.h>
#include "ob_ucode.h"
#include "ob_fw.h"

/* Numeric literals keep this header buildable in the host harness. */
#define OB_D2A_RET_OK		(0)
#define OB_D2A_RET_EIO		(-5)	/* -EIO */
#define OB_D2A_RET_ENODEV	(-19)	/* -ENODEV */
#define OB_D2A_RET_EINVAL	(-22)	/* -EINVAL */
#define OB_D2A_RET_ETIMEDOUT	(-110)	/* -ETIMEDOUT */

/* Defensive ceiling on the poll, independent of the vendor counter. */
#define OB_D2A_POLL_MAX_ITERATIONS \
	(OB_UCODE_POLL_TIMEOUT / OB_UCODE_POLL_STEP)	/* 100000 */

/* Device-loss feedback cadence inside the upload loop. No MMIO involved. */
#define OB_D2A_UPLOAD_LOSS_CHECK_MASK	0x3ffu

/* The exact vendor word count this contract accepts (43400 / 4). */
#define OB_D2A_EXPECTED_WORDS		(OB_FW_UCODE42_SIZE / 4u)	/* 10850 */

/* ---- stages (the failing stage is reported, never guessed) ---- */
enum ob_d2a_stage {
	OB_D2A_STAGE_ADMISSION = 0,
	OB_D2A_STAGE_FIRMWARE,
	OB_D2A_STAGE_CORE_PREP,
	OB_D2A_STAGE_MACCONTROL_UPLOAD,
	OB_D2A_STAGE_OBJADDR,
	OB_D2A_STAGE_UPLOAD,
	OB_D2A_STAGE_PSM_START,
	OB_D2A_STAGE_PSM_POLL,
	OB_D2A_STAGE_SHM_DIAG,
	OB_D2A_STAGE_DONE,
};

enum ob_d2a_class {
	OB_D2A_CLASS_ADMISSION_REJECTED = 0,
	OB_D2A_CLASS_FIRMWARE_UNAVAILABLE,
	OB_D2A_CLASS_CORE_NOT_READY,
	OB_D2A_CLASS_PREREQUISITE_REJECTED,
	OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
	OB_D2A_CLASS_UPLOAD_INCOMPLETE,
	OB_D2A_CLASS_PSM_START_REJECTED,
	OB_D2A_CLASS_PSM_TIMEOUT,
	OB_D2A_CLASS_UNCERTAIN,
	OB_D2A_CLASS_PSM_SUSPENDED,
};

enum ob_d2a_event {
	OB_D2A_EV_BEGIN = 0,
	OB_D2A_EV_FIRMWARE,
	OB_D2A_EV_CORE_PREP,
	OB_D2A_EV_PRE_READ,
	OB_D2A_EV_POST_READ,
	OB_D2A_EV_PRE_WRITE,
	OB_D2A_EV_POST_WRITE,
	OB_D2A_EV_UPLOAD_PROGRESS,
	OB_D2A_EV_DELAY,
	OB_D2A_EV_STOP,
	OB_D2A_EV_END,
};

struct ob_d2a_log {
	enum ob_d2a_stage stage;
	u16 off;
	u32 value;		/* POST_READ: observed; PRE_WRITE: to be written */
	u32 counter;		/* progress/upload counters */
	int error;		/* STOP only */
	const char *reason;	/* static token */
};

/*
 * Backend. One callback per operation, each bound to a fixed register and a
 * fixed width. `write32` returns non-zero when the access was suppressed (the
 * guard observed a device-loss latch); that is how "issued" is distinguished
 * from "called".
 *
 * `read32_trusted` is the latching accessor: an all-ones result sets the
 * monotonic device-loss latch in the backend. `read16` is payload-only and
 * never latches.
 *
 * `core_prepare` performs the caller's wrapper-window preparation. It reports
 * whether the core is confirmed up AND whether it mutated hardware, because a
 * failed preparation may still have issued writes that have no teardown.
 */
struct ob_d2a_ops {
	void *ctx;
	int  (*fw_request)(void *ctx, const u8 **data, size_t *size,
			   const char **name);
	int  (*core_prepare)(void *ctx, bool *core_up, bool *mutated);
	int  (*read32_trusted)(void *ctx, u16 off, u32 *val);
	int  (*read16)(void *ctx, u16 off, u16 *val);
	int  (*write32)(void *ctx, u16 off, u32 val);
	void (*delay_us)(void *ctx, unsigned int us);
	void (*log)(void *ctx, enum ob_d2a_event ev,
		    const struct ob_d2a_log *rec);
	bool (*dev_lost)(void *ctx);
};

struct ob_d2a_result {
	/* firmware */
	const char *image_name;
	size_t image_size;
	bool fw_validated;
	/* core preparation */
	bool core_prepared;
	bool core_up;
	/* upload */
	u32 words;		/* expected words */
	u32 writes_issued;	/* OBJDATA writes the backend actually issued */
	u32 writes_confirmed;	/* always 0: no ucode-RAM readback exists */
	u32 upload_completed_words;	/* writes_issued at loop exit */
	/* PSM */
	bool psm_started;
	bool psm_suspended;
	u32 psm_iterations;
	u32 psm_status;
	/* diagnostic (ucode_test_only tail; never the shared path) */
	bool shm_read;		/* true only after all four observations succeeded */
	u32 shm_observations;	/* 0..4 completed FIFO-size observations */
	u8 shm_failed_index;	/* index of the first failing observation */
	bool shm_failed;
	u16 shm_fifosize[4];
	/* accounting */
	u32 reads_trusted;	/* every checked read: fixed control + PSM poll */
	u32 reads_poll;		/* subset of reads_trusted issued by the PSM poll loop */
	u32 reads_payload;
	u32 writes_attempted;	/* write callback invoked (may be suppressed) */
	u32 writes_issued_total;/* every register write, incl. control ones */
	/* verdict */
	enum ob_d2a_stage stage;	/* last stage entered */
	enum ob_d2a_stage failed_stage;
	enum ob_d2a_class class;
	const char *reason;		/* static token, NULL on success */
	int error;
	bool suspicious;	/* an all-ones control/status value was seen */
	bool device_lost;	/* the latch is (or became) set */
	bool uncertain;		/* post-write state not established */
	bool mutated;		/* any hardware mutation was issued */
	bool retained;		/* ownership retained: no teardown exists */
};

/* ---- static tokens (host tests pin these strings) ---- */
#define OB_D2A_TOKEN_PSM_SUSPENDED	"PSM_SUSPENDED"
#define OB_D2A_TOKEN_PSM_TIMEOUT	"PSM_POLL_TIMEOUT"
#define OB_D2A_TOKEN_PSM_START_REJECTED	"PSM_START_REJECTED"
#define OB_D2A_TOKEN_UPLOAD_INCOMPLETE	"UPLOAD_INCOMPLETE"
#define OB_D2A_TOKEN_DEVICE_LOSS	"D2A_DEVICE_LOSS_SUSPECTED"
#define OB_D2A_TOKEN_PREREQ		"D2A_PREREQUISITE_REJECTED"
#define OB_D2A_TOKEN_CORE_NOT_READY	"D2A_CORE_NOT_READY"
#define OB_D2A_TOKEN_FW			"D2A_FIRMWARE_UNAVAILABLE"
#define OB_D2A_TOKEN_ADMISSION		"D2A_ADMISSION_REJECTED"
#define OB_D2A_TOKEN_UNCERTAIN		"D2A_UNCERTAIN"

static inline const char *ob_d2a_class_token(enum ob_d2a_class c)
{
	switch (c) {
	case OB_D2A_CLASS_PSM_SUSPENDED:
		return OB_D2A_TOKEN_PSM_SUSPENDED;
	case OB_D2A_CLASS_PSM_TIMEOUT:
		return OB_D2A_TOKEN_PSM_TIMEOUT;
	case OB_D2A_CLASS_PSM_START_REJECTED:
		return OB_D2A_TOKEN_PSM_START_REJECTED;
	case OB_D2A_CLASS_UPLOAD_INCOMPLETE:
		return OB_D2A_TOKEN_UPLOAD_INCOMPLETE;
	case OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED:
		return OB_D2A_TOKEN_DEVICE_LOSS;
	case OB_D2A_CLASS_PREREQUISITE_REJECTED:
		return OB_D2A_TOKEN_PREREQ;
	case OB_D2A_CLASS_CORE_NOT_READY:
		return OB_D2A_TOKEN_CORE_NOT_READY;
	case OB_D2A_CLASS_FIRMWARE_UNAVAILABLE:
		return OB_D2A_TOKEN_FW;
	case OB_D2A_CLASS_ADMISSION_REJECTED:
		return OB_D2A_TOKEN_ADMISSION;
	case OB_D2A_CLASS_UNCERTAIN:
		return OB_D2A_TOKEN_UNCERTAIN;
	}
	return OB_D2A_TOKEN_UNCERTAIN;
}

static inline const char *ob_d2a_stage_name(enum ob_d2a_stage s)
{
	switch (s) {
	case OB_D2A_STAGE_ADMISSION:
		return "admission";
	case OB_D2A_STAGE_FIRMWARE:
		return "firmware";
	case OB_D2A_STAGE_CORE_PREP:
		return "core-prep";
	case OB_D2A_STAGE_MACCONTROL_UPLOAD:
		return "maccontrol-upload";
	case OB_D2A_STAGE_OBJADDR:
		return "objaddr";
	case OB_D2A_STAGE_UPLOAD:
		return "upload";
	case OB_D2A_STAGE_PSM_START:
		return "psm-start";
	case OB_D2A_STAGE_PSM_POLL:
		return "psm-poll";
	case OB_D2A_STAGE_SHM_DIAG:
		return "shm-diag";
	case OB_D2A_STAGE_DONE:
		return "done";
	}
	return "unknown";
}

/* ---- pure predicates (the five defects, each pinned by a host test) ---- */

/* D1: all-ones is an inaccessible window, never a suspended MAC. */
static inline bool ob_d2a_status_suspicious(u32 status)
{
	return status == 0xffffffffu; /* OB_GUARD_DEAD32 sentinel */
}

/* D1: success requires the suspended bit AND a non-suspicious value. */
static inline bool ob_d2a_psm_success(u32 status)
{
	return !ob_d2a_status_suspicious(status) &&
	       ob_ucode_mac_suspended(status);
}

/* Recovery states the vendor sets before each write. */
static inline bool ob_d2a_macctl_upload_ok(u32 v)
{
	return (v & OB_UCODE_UPLOAD_EXPECTED) == OB_UCODE_UPLOAD_EXPECTED &&
	       (v & OB_UCODE_UPLOAD_FORBIDDEN) == 0;
}

static inline bool ob_d2a_macctl_psm_ok(u32 v)
{
	return (v & OB_UCODE_PSM_EXPECTED) == OB_UCODE_PSM_EXPECTED &&
	       (v & OB_UCODE_PSM_FORBIDDEN) == 0;
}

/* Masked update, mirroring wlc_bmac_mctrl(dev, mask, val). */
static inline u32 ob_d2a_mctrl_next(u32 old, u32 mask, u32 val)
{
	return (old & ~mask) | val;
}

/* D2: the upload is complete only when every word was actually ISSUED. */
static inline bool ob_d2a_upload_complete(u32 issued, u32 expected)
{
	return expected == OB_D2A_EXPECTED_WORDS && issued == expected;
}

/* D2: this path can never confirm an upload (no ucode-RAM readback). */
static inline bool ob_d2a_upload_confirmable(void)
{
	return false;
}

/* D5: any mutation, or a latched loss, means ownership is retained. */
static inline bool ob_d2a_retained(bool mutated, bool device_lost)
{
	return mutated || device_lost;
}

/* The callable set is exact; a missing callback is a contract defect. */
static inline bool ob_d2a_ops_ok(const struct ob_d2a_ops *ops)
{
	return ops && ops->fw_request && ops->core_prepare &&
	       ops->read32_trusted && ops->read16 && ops->write32 &&
	       ops->delay_us && ops->log;
}

static inline bool ob_d2a_image_valid(const u8 *data, size_t size)
{
	if (!data)
		return false;
	if (!ob_fw_ucode_size_ok(size) || (size % 4u) != 0u)
		return false;
	return ob_fw_hash64(data, size) == OB_FW_UCODE42_FNV;
}

static inline void ob_d2a_result_init(struct ob_d2a_result *r)
{
	r->image_name = NULL;
	r->image_size = 0;
	r->fw_validated = false;
	r->core_prepared = false;
	r->core_up = false;
	r->words = 0;
	r->writes_issued = 0;
	r->writes_confirmed = 0;
	r->upload_completed_words = 0;
	r->psm_started = false;
	r->psm_suspended = false;
	r->psm_iterations = 0;
	r->psm_status = 0;
	r->shm_read = false;
	r->shm_observations = 0;
	r->shm_failed_index = 0;
	r->shm_failed = false;
	r->shm_fifosize[0] = 0;
	r->shm_fifosize[1] = 0;
	r->shm_fifosize[2] = 0;
	r->shm_fifosize[3] = 0;
	r->reads_trusted = 0;
	r->reads_poll = 0;
	r->reads_payload = 0;
	r->writes_attempted = 0;
	r->writes_issued_total = 0;
	r->stage = OB_D2A_STAGE_ADMISSION;
	r->failed_stage = OB_D2A_STAGE_ADMISSION;
	r->class = OB_D2A_CLASS_ADMISSION_REJECTED;
	r->reason = "not_evaluated";
	r->error = 0;
	r->suspicious = false;
	r->device_lost = false;
	r->uncertain = false;
	r->mutated = false;
	r->retained = false;
}

static inline void ob_d2a_log_ev(const struct ob_d2a_ops *ops,
				 enum ob_d2a_event ev,
				 const struct ob_d2a_result *r,
				 enum ob_d2a_stage stage, u16 off,
				 u32 value, u32 counter, int error,
				 const char *reason)
{
	struct ob_d2a_log rec;

	(void)r; /* the counters live in @r; they are formatted by the adapter */
	rec.stage = stage;
	rec.off = off;
	rec.value = value;
	rec.counter = counter;
	rec.error = error;
	rec.reason = reason;
	ops->log(ops->ctx, ev, &rec);
}

/* Every stop path funnels through here so class/counters/log cannot drift. */
static inline int ob_d2a_stop(const struct ob_d2a_ops *ops,
			      struct ob_d2a_result *r, enum ob_d2a_stage stage,
			      enum ob_d2a_class cls, const char *reason,
			      int error)
{
	r->failed_stage = stage;
	r->class = cls;
	r->reason = reason;
	r->error = error;
	r->uncertain = (cls == OB_D2A_CLASS_UNCERTAIN);
	r->device_lost = (cls == OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED) ||
			 (ops->dev_lost && ops->dev_lost(ops->ctx));
	r->retained = ob_d2a_retained(r->mutated, r->device_lost);
	ob_d2a_log_ev(ops, OB_D2A_EV_STOP, r, stage, 0, 0, 0, error, reason);
	return error ? error : OB_D2A_RET_EIO;
}

/* D4: every control/status read is trusted and classified before use. */
static inline int ob_d2a_read_checked(const struct ob_d2a_ops *ops,
				      struct ob_d2a_result *r,
				      enum ob_d2a_stage stage, u16 off,
				      u32 *val)
{
	u32 v = 0;
	int ret;

	if (ops->dev_lost && ops->dev_lost(ops->ctx))
		return ob_d2a_stop(ops, r, stage,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "device_loss_latched", OB_D2A_RET_EIO);

	ob_d2a_log_ev(ops, OB_D2A_EV_PRE_READ, r, stage, off, 0, 0, 0, NULL);
	r->reads_trusted++;

	ret = ops->read32_trusted(ops->ctx, off, &v);
	if (ret)
		return ob_d2a_stop(ops, r, stage, OB_D2A_CLASS_UNCERTAIN,
				   "read_callback_error", ret);

	ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, stage, off, v, 0, 0, NULL);

	/* A suspicious value is classified before any success marker. */
	if (ob_d2a_status_suspicious(v)) {
		r->suspicious = true;
		return ob_d2a_stop(ops, r, stage,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "all_ones_window_inaccessible",
				   OB_D2A_RET_EIO);
	}
	/* The trusted accessor may latch on this same value. */
	if (ops->dev_lost && ops->dev_lost(ops->ctx))
		return ob_d2a_stop(ops, r, stage,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "device_loss_latched_by_accessor",
				   OB_D2A_RET_EIO);

	*val = v;
	return 0;
}

/* A write is counted as ATTEMPTED before the call and as ISSUED only if the
 * backend actually executed it. No readback means no confirmation. */
static inline int ob_d2a_write_checked(const struct ob_d2a_ops *ops,
				       struct ob_d2a_result *r,
				       enum ob_d2a_stage stage, u16 off,
				       u32 val)
{
	int ret;

	if (ops->dev_lost && ops->dev_lost(ops->ctx))
		return ob_d2a_stop(ops, r, stage,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "device_loss_latched_no_write", OB_D2A_RET_EIO);

	ob_d2a_log_ev(ops, OB_D2A_EV_PRE_WRITE, r, stage, off, val, 0, 0, NULL);
	r->writes_attempted++;
	r->mutated = true;

	ret = ops->write32(ops->ctx, off, val);
	if (ret)
		return ob_d2a_stop(ops, r, stage,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "write_suppressed_device_lost", ret);

	r->writes_issued_total++;
	ob_d2a_log_ev(ops, OB_D2A_EV_POST_WRITE, r, stage, off, val, 0, 0,
		      NULL);
	return 0;
}

/*
 * The whole path. Straight-line between two loops (the bounded upload and the
 * bounded poll); no retry, no rollback, no cleanup write on any path.
 */
static inline int ob_d2a_run(const struct ob_d2a_ops *ops, bool entry_dev_lost,
			     struct ob_d2a_result *r)
{
	const u8 *data = NULL;
	size_t size = 0;
	const char *name = NULL;
	u32 v = 0, i, status;
	u32 remaining;
	bool core_up = false, core_mutated = false;
	int ret;

	if (!r)
		return OB_D2A_RET_EINVAL;

	ob_d2a_result_init(r);

	if (!ob_d2a_ops_ok(ops)) {
		r->reason = "invalid_ops";
		r->error = OB_D2A_RET_EINVAL;
		r->class = OB_D2A_CLASS_ADMISSION_REJECTED;
		return OB_D2A_RET_EINVAL;
	}

	ob_d2a_log_ev(ops, OB_D2A_EV_BEGIN, r, OB_D2A_STAGE_ADMISSION, 0, 0, 0,
		      0, NULL);

	/* An already-latched device issues zero callbacks. */
	if (entry_dev_lost || (ops->dev_lost && ops->dev_lost(ops->ctx))) {
		r->device_lost = true;
		r->retained = true;
		return ob_d2a_stop(ops, r, OB_D2A_STAGE_ADMISSION,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "device_loss_latched", OB_D2A_RET_EIO);
	}

	/* ---- 1. firmware first: a failure here touches no hardware ---- */
	r->stage = OB_D2A_STAGE_FIRMWARE;
	ret = ops->fw_request(ops->ctx, &data, &size, &name);
	if (ret) {
		ob_d2a_log_ev(ops, OB_D2A_EV_FIRMWARE, r, r->stage, 0, 0, 0,
			      ret, "request_failed");
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_FIRMWARE_UNAVAILABLE,
				   "firmware_request_failed", ret);
	}

	/* Size + FNV re-validation: an unexpected image is never uploaded. */
	if (!ob_d2a_image_valid(data, size)) {
		ob_d2a_log_ev(ops, OB_D2A_EV_FIRMWARE, r, r->stage, 0, 0,
			      (u32)size, 0, "image_mismatch");
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_FIRMWARE_UNAVAILABLE,
				   "image_size_or_hash_mismatch",
				   OB_D2A_RET_EINVAL);
	}

	r->image_name = name;
	r->image_size = size;
	r->fw_validated = true;
	r->words = ob_ucode_words_from_size(size);
	ob_d2a_log_ev(ops, OB_D2A_EV_FIRMWARE, r, r->stage, 0, 0, r->words, 0,
		      "image_validated");

	/* ---- 2. core preparation (wrapper window) ---------------------- */
	r->stage = OB_D2A_STAGE_CORE_PREP;
	ret = ops->core_prepare(ops->ctx, &core_up, &core_mutated);
	r->core_prepared = (ret == 0);
	r->core_up = core_up;
	if (core_mutated) {
		r->mutated = true;
		r->retained = ob_d2a_retained(true, r->device_lost);
	}
	ob_d2a_log_ev(ops, OB_D2A_EV_CORE_PREP, r, r->stage, 0, core_up ? 1 : 0,
		      0, ret, core_up ? "core_up" : "core_not_up");

	/* D4/T04: no functional access before a confirmed core-up. */
	if (ret || !core_up)
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_CORE_NOT_READY,
				   ret ? "core_prepare_failed" :
					 "core_not_confirmed",
				   ret ? ret : OB_D2A_RET_ENODEV);

	/* ---- 3. pre-upload MACCONTROL (masked RMW, validated) --------- */
	r->stage = OB_D2A_STAGE_MACCONTROL_UPLOAD;
	ret = ob_d2a_read_checked(ops, r, r->stage, OB_UCODE_REG_MACCONTROL,
				  &v);
	if (ret)
		return ret;

	ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
		      OB_UCODE_REG_MACCONTROL, v, 0, 0, "maccontrol_before");

	ret = ob_d2a_write_checked(ops, r, r->stage, OB_UCODE_REG_MACCONTROL,
				   ob_d2a_mctrl_next(v, OB_UCODE_MACCONTROL_MASK,
						     OB_UCODE_MACCONTROL_UPLOAD));
	if (ret)
		return ret;

	ret = ob_d2a_read_checked(ops, r, r->stage, OB_UCODE_REG_MACCONTROL,
				  &v);
	if (ret)
		return ret;

	if (!ob_d2a_macctl_upload_ok(v)) {
		ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
			      OB_UCODE_REG_MACCONTROL, v, 0, 0,
			      "maccontrol_upload_invalid");
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_PREREQUISITE_REJECTED,
				   "maccontrol_upload_state_invalid",
				   OB_D2A_RET_EIO);
	}
	/* The readback is what proves the write; log it separately. */
	ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
		      OB_UCODE_REG_MACCONTROL, v, 0, 0,
		      "maccontrol_upload_validated");

	/* ---- 4. OBJADDR auto-increment selector ----------------------- */
	r->stage = OB_D2A_STAGE_OBJADDR;
	ret = ob_d2a_write_checked(ops, r, r->stage, OB_UCODE_REG_OBJADDR,
				   OB_UCODE_OBJADDR_AUTO_INC |
				   OB_UCODE_OBJADDR_UCM_SEL);
	if (ret)
		return ret;

	/* Vendor barrier readback of the selector. */
	ret = ob_d2a_read_checked(ops, r, r->stage, OB_UCODE_REG_OBJADDR, &v);
	if (ret)
		return ret;

	if ((v & OB_UCODE_OBJADDR_AUTO_INC) != OB_UCODE_OBJADDR_AUTO_INC) {
		ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
			      OB_UCODE_REG_OBJADDR, v, 0, 0,
			      "objaddr_selector_invalid");
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_PREREQUISITE_REJECTED,
				   "objaddr_selector_not_auto_inc",
				   OB_D2A_RET_EIO);
	}

	/* ---- 5. the 10850-word upload, device loss checked throughout -- */
	r->stage = OB_D2A_STAGE_UPLOAD;
	for (i = 0; i < r->words; i++) {
		u32 word = ob_fw_le32(data + (size_t)i * 4);

		/* D3: the latch is consulted before every access, no MMIO. */
		if (ops->dev_lost && ops->dev_lost(ops->ctx)) {
			r->upload_completed_words = r->writes_issued;
			return ob_d2a_stop(ops, r, r->stage,
					   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
					   "device_loss_during_upload",
					   OB_D2A_RET_EIO);
		}

		ret = ob_d2a_write_checked(ops, r, r->stage,
					   OB_UCODE_REG_OBJDATA, word);
		if (ret) {
			r->upload_completed_words = r->writes_issued;
			return ret;
		}

		r->writes_issued++;
		r->upload_completed_words = r->writes_issued;

		if ((i & OB_D2A_UPLOAD_LOSS_CHECK_MASK) ==
		    OB_D2A_UPLOAD_LOSS_CHECK_MASK) {
			ob_d2a_log_ev(ops, OB_D2A_EV_UPLOAD_PROGRESS, r,
				      r->stage, 0, 0, r->writes_issued, 0,
				      "progress");
			if (ops->dev_lost && ops->dev_lost(ops->ctx))
				return ob_d2a_stop(ops, r, r->stage,
						   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
						   "device_loss_during_upload",
						   OB_D2A_RET_EIO);
		}
	}

	/* D2: issued, never "confirmed"; no short upload reaches PSM. */
	ob_d2a_log_ev(ops, OB_D2A_EV_UPLOAD_PROGRESS, r, r->stage, 0, 0,
		      r->writes_issued, 0, "upload_complete");

	if (!ob_d2a_upload_complete(r->writes_issued, r->words)) {
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_UPLOAD_INCOMPLETE,
				   "upload_write_count_mismatch",
				   OB_D2A_RET_EIO);
	}
	if (ops->dev_lost && ops->dev_lost(ops->ctx))
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "device_loss_after_upload", OB_D2A_RET_EIO);

	/* ---- 6. clear stale interrupt state, then start the PSM -------- */
	r->stage = OB_D2A_STAGE_PSM_START;
	ret = ob_d2a_write_checked(ops, r, r->stage,
				   OB_UCODE_REG_MACINTSTATUS, 0xffffffffu);
	if (ret)
		return ret;

	ret = ob_d2a_read_checked(ops, r, r->stage, OB_UCODE_REG_MACCONTROL,
				  &v);
	if (ret)
		return ret;

	ret = ob_d2a_write_checked(ops, r, r->stage, OB_UCODE_REG_MACCONTROL,
				   ob_d2a_mctrl_next(v, OB_UCODE_MACCONTROL_MASK,
						     OB_UCODE_MACCONTROL_PSM));
	if (ret)
		return ret;

	ret = ob_d2a_read_checked(ops, r, r->stage, OB_UCODE_REG_MACCONTROL,
				  &v);
	if (ret)
		return ret;

	if (!ob_d2a_macctl_psm_ok(v)) {
		ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
			      OB_UCODE_REG_MACCONTROL, v, 0, 0,
			      "maccontrol_psm_invalid");
		return ob_d2a_stop(ops, r, r->stage,
				   OB_D2A_CLASS_PSM_START_REJECTED,
				   "maccontrol_psm_state_invalid",
				   OB_D2A_RET_EIO);
	}
	r->psm_started = true;
	/* The readback is what proves the write; log it separately. */
	ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
		      OB_UCODE_REG_MACCONTROL, v, 0, 0,
		      "maccontrol_psm_validated");

	/* ---- 7. bounded poll: suspicious BEFORE suspended (D1) -------- */
	r->stage = OB_D2A_STAGE_PSM_POLL;
	remaining = OB_UCODE_POLL_TIMEOUT;
	for (;;) {
		if (ops->dev_lost && ops->dev_lost(ops->ctx))
			return ob_d2a_stop(ops, r, r->stage,
					   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
					   "device_loss_during_poll",
					   OB_D2A_RET_EIO);

		ret = ob_d2a_read_checked(ops, r, r->stage,
					  OB_UCODE_REG_MACINTSTATUS, &status);
		if (ret)
			return ret;
		/* Counted separately so the fixed-read cost stays derivable: the
		 * trusted total minus this subset is the fixed control/status cost. */
		r->reads_poll++;

		r->psm_status = status;

		/* All-ones contains MI_MACSSPNDD: never report success. */
		if (ob_d2a_status_suspicious(status))
			return ob_d2a_stop(ops, r, r->stage,
					   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
					   "all_ones_macintstatus",
					   OB_D2A_RET_EIO);

		if (ob_d2a_psm_success(status))
			break;

		if (ob_ucode_poll_expired(remaining, OB_UCODE_POLL_STEP) ||
		    r->psm_iterations >= OB_D2A_POLL_MAX_ITERATIONS) {
			ob_d2a_log_ev(ops, OB_D2A_EV_STOP, r, r->stage, 0,
				      status, r->psm_iterations,
				      OB_D2A_RET_ETIMEDOUT, "poll_timeout");
			return ob_d2a_stop(ops, r, r->stage,
					   OB_D2A_CLASS_PSM_TIMEOUT,
					   "psm_poll_timeout",
					   OB_D2A_RET_ETIMEDOUT);
		}

		ops->delay_us(ops->ctx, OB_UCODE_POLL_DELAY_US);
		remaining -= OB_UCODE_POLL_STEP;
		r->psm_iterations++;
	}
	r->psm_suspended = true;

	/* ---- 8. success (no SHM diagnostic on the shared path) -------- */
	r->stage = OB_D2A_STAGE_DONE;
	r->class = OB_D2A_CLASS_PSM_SUSPENDED;
	r->reason = NULL;
	r->error = 0;
	r->retained = ob_d2a_retained(r->mutated, r->device_lost);
	ob_d2a_log_ev(ops, OB_D2A_EV_END, r, r->stage, 0, r->psm_status,
		      r->writes_issued, 0, NULL);
	return OB_D2A_RET_OK;
}

/*
 * ucode_test_only tail (Amendment 01 A1/A4): the four read-only SHM FIFO-size
 * observations, performed EXACTLY ONCE and only after `ob_d2a_run()` returned 0
 * with a classified PSM suspend. The initvals caller never calls this, so
 * common initvals sees the same ordering it had before.
 *
 * Fail-stop (A4): the first observation that cannot be completed stops the
 * diagnostic at that operation. No later FIFO register is touched, the completed
 * count is what `shm_observations` reports, and `shm_read` stays false, so an
 * accessor error can never be hidden by a later successful read.
 *
 * The 16-bit OBJDATA payload is legitimate payload: an all-ones half-word is a
 * measurement here, not the 32-bit inaccessible-window sentinel, and it is never
 * classified as device loss.
 */
static inline int ob_d2a_shm_diag(const struct ob_d2a_ops *ops,
				  struct ob_d2a_result *r, u16 fifo[4])
{
	static const u16 offs[4] = {
		OB_UCODE_SHM_FIFOSIZE0, OB_UCODE_SHM_FIFOSIZE1,
		OB_UCODE_SHM_FIFOSIZE2, OB_UCODE_SHM_FIFOSIZE3,
	};
	u32 v = 0;
	u32 k;
	int ret;

	if (!r || !fifo)
		return OB_D2A_RET_EINVAL;
	if (!ob_d2a_ops_ok(ops)) {
		r->reason = "invalid_ops";
		r->error = OB_D2A_RET_EINVAL;
		return OB_D2A_RET_EINVAL;
	}

	r->shm_read = false;
	r->shm_observations = 0;
	r->shm_failed = false;
	r->shm_failed_index = 0;
	fifo[0] = 0;
	fifo[1] = 0;
	fifo[2] = 0;
	fifo[3] = 0;

	/* Only a classified PSM suspend authorises the diagnostic. */
	if (!r->psm_suspended) {
		r->reason = "shm_diag_without_psm";
		r->error = OB_D2A_RET_EINVAL;
		return OB_D2A_RET_EINVAL;
	}
	if (r->device_lost || (ops->dev_lost && ops->dev_lost(ops->ctx)))
		return ob_d2a_stop(ops, r, OB_D2A_STAGE_SHM_DIAG,
				   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
				   "device_loss_before_shm_diag", OB_D2A_RET_EIO);

	r->stage = OB_D2A_STAGE_SHM_DIAG;
	for (k = 0; k < 4; k++) {
		u16 half = 0;

		/* Select the SHM window, then read the barrier. */
		ret = ob_d2a_write_checked(ops, r, r->stage, OB_UCODE_REG_OBJADDR,
					   OB_UCODE_OBJADDR_SHM_SEL |
					   ((u32)offs[k] >> 2));
		if (ret) {
			r->shm_failed = true;
			r->shm_failed_index = (u8)k;
			return ret;
		}
		ret = ob_d2a_read_checked(ops, r, r->stage, OB_UCODE_REG_OBJADDR,
					  &v);
		if (ret) {
			r->shm_failed = true;
			r->shm_failed_index = (u8)k;
			return ret;
		}
		if (ops->dev_lost && ops->dev_lost(ops->ctx)) {
			r->shm_failed = true;
			r->shm_failed_index = (u8)k;
			return ob_d2a_stop(ops, r, r->stage,
					   OB_D2A_CLASS_DEVICE_LOSS_SUSPECTED,
					   "device_loss_during_shm_diag",
					   OB_D2A_RET_EIO);
		}

		/* Payload half-word: all-ones is legitimate here. */
		ob_d2a_log_ev(ops, OB_D2A_EV_PRE_READ, r, r->stage,
			      OB_UCODE_REG_OBJDATA, 0, 0, 0, NULL);
		r->reads_payload++;
		if (ops->read16(ops->ctx,
				OB_UCODE_REG_OBJDATA + (u16)(offs[k] & 0x2),
				&half)) {
			/* Stop here: never a pass condition, never followed. */
			r->shm_failed = true;
			r->shm_failed_index = (u8)k;
			ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
				      OB_UCODE_REG_OBJDATA, 0, k,
				      OB_D2A_RET_EIO, "shm_diag_unavailable");
			return ob_d2a_stop(ops, r, r->stage,
					   OB_D2A_CLASS_UNCERTAIN,
					   "shm_diag_read_failed",
					   OB_D2A_RET_EIO);
		}
		fifo[k] = half;
		r->shm_fifosize[k] = half;
		r->shm_observations++;
		ob_d2a_log_ev(ops, OB_D2A_EV_POST_READ, r, r->stage,
			      OB_UCODE_REG_OBJDATA, half, k, 0, NULL);
	}

	/* Defensive: only a complete diagnostic may report itself complete. */
	if (r->shm_observations != 4) {
		r->shm_failed = true;
		return ob_d2a_stop(ops, r, r->stage, OB_D2A_CLASS_UNCERTAIN,
				   "shm_diag_incomplete", OB_D2A_RET_EIO);
	}
	r->shm_read = true;
	return OB_D2A_RET_OK;
}

#endif /* _OB_UCODE_D2A_CONTRACT_H_ */

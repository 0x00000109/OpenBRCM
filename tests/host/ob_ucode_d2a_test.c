// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — Phase 38C host tests for the D2A ucode/PSM state machine.
 *
 * These execute the REAL contract compiled into the module
 * (src/ob_ucode_d2a_contract.h) through a fake backend. No hardware, no kernel
 * API, no MMIO. Every check prints a machine-readable RESULT line; the process
 * exits non-zero on the first aggregate failure count.
 *
 * Coverage map (Phase 38C brief):
 *   T01 correct isolated-mode admission
 *   T02 default load remains fail-closed
 *   T03 conflicting modes reject
 *   T04 core-down rejects functional access
 *   T05 device loss prevents subsequent access
 *   T06 all-ones MACCONTROL prerequisite rejects
 *   T07 all-ones MACINTSTATUS never means PSM success
 *   T08 firmware request failure performs no hardware operation
 *   T09 firmware size/hash mismatch performs no upload
 *   T10 upload interruption cannot start PSM
 *   T11 issued writes are distinguished from confirmed state
 *   T12 bounded polling times out correctly
 *   T13 valid synthetic PSM completion is classified correctly
 *   T14 hardware-mutating failure retains the device safely
 *
 * T15/T16 (no normal teardown / no PHY-radio-DMA-IRQ-mac80211 path) are
 * source-level invariants and live in tests/host/test_d2a_source_contract.py.
 * T17/T18 are regression: the pre-existing suites are run unchanged
 * (ob_phase37a_admission_test, ob_phase37b_enable_test, ob_diag_test,
 * ob_ucode_test, ob_fw_test, ob_initvals_test).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../../src/ob_ucode_d2a_contract.h"

static int failures;
static int checks;

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

/* ---- firmware fixture --------------------------------------------------- */

static u8 *g_img;
static size_t g_img_size;

/*
 * Load the exact rev42 image the driver requests. The repository artifact is
 * tried first so the suite does not depend on the host's /lib/firmware;
 * the installed legacy name is the documented fallback the module itself
 * uses. A missing image is a FAILURE here, not a skip: the D2A contract is
 * defined on this exact 43400-byte image.
 */
static bool load_image(void)
{
	static const char *const paths[] = {
		"../../artifacts/firmware/bcm43xx-ucode.fw",
		"/lib/firmware/brcm/bcm43xx-ucode.fw",
		"/lib/firmware/brcm/bcm4352-d11ucode42.bin",
	};
	unsigned int i;

	for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		FILE *f = fopen(paths[i], "rb");
		long n;

		if (!f)
			continue;
		g_img = malloc(OB_FW_UCODE42_SIZE);
		if (!g_img) {
			fclose(f);
			return false;
		}
		n = (long)fread(g_img, 1, OB_FW_UCODE42_SIZE, f);
		fclose(f);
		if (n != (long)OB_FW_UCODE42_SIZE) {
			free(g_img);
			g_img = NULL;
			continue;
		}
		g_img_size = (size_t)n;
		printf("INFO image=%s size=%zu fnv=0x%016llx\n", paths[i],
		       g_img_size, (unsigned long long)ob_fw_hash64(g_img,
								g_img_size));
		return ob_fw_hash64(g_img, g_img_size) == OB_FW_UCODE42_FNV;
	}
	return false;
}

/* ---- fake backend ------------------------------------------------------- */

#define FAKE_TRACE_MAX 16

struct fake_trace {
	u16 off;
	u32 val;
	char op;	/* 'R' trusted read, 'r' payload read, 'W' write */
};

struct fake {
	/* image */
	int fw_ret;
	bool fw_corrupt;
	bool fw_truncate;
	bool fw_called;
	/* core prep */
	bool core_up;
	int core_ret;
	bool core_mutated;
	bool core_called;
	/* registers */
	u32 maccontrol;
	u32 macintstatus_fixed;
	bool use_script;
	u32 script[8];
	int script_len;
	int script_pos;
	u32 objaddr;
	u16 shm[4];
	bool dev_lost;
	/* injection */
	int fail_objdata_write_at;	/* 1-based; -1 = never */
	int lose_after_words;		/* -1 = never */
	bool lose_when_upload_done;	/* latch after the last accepted word */
	int fail_read_at;		/* 1-based MACCONTROL read; -1 */
	int fail_shm_read_at;		/* 1-based payload read; -1 */
	int bad_readback_at;		/* 1-based readback index; -1 */
	u32 bad_readback_val;
	/* accounting */
	int hw_reads;
	int hw_writes;
	int objdata_writes;
	int maccontrol_reads;
	int macintstatus_reads;
	int delays;
	int ops_after_lost;
	int writes_after_latched;
	int shm_reads;
	bool psm_seen;
	u8 *scratch;		/* corrupted-image copy for T09 */
	/* ordering trace: first and last accesses */
	struct fake_trace first[FAKE_TRACE_MAX];
	int first_n;
	struct fake_trace last[FAKE_TRACE_MAX];
	int last_n;
};

static void trace_add(struct fake *f, char op, u16 off, u32 val)
{
	struct fake_trace t;

	t.op = op;
	t.off = off;
	t.val = val;
	if (f->first_n < FAKE_TRACE_MAX)
		f->first[f->first_n++] = t;
	if (f->last_n < FAKE_TRACE_MAX) {
		f->last[f->last_n++] = t;
	} else {
		memmove(&f->last[0], &f->last[1],
			sizeof(f->last[0]) * (FAKE_TRACE_MAX - 1));
		f->last[FAKE_TRACE_MAX - 1] = t;
	}
}

static u32 fake_poll_value(struct fake *f)
{
	if (!f->use_script)
		return f->macintstatus_fixed;
	if (f->script_pos >= f->script_len)
		return f->script[f->script_len - 1];
	return f->script[f->script_pos++];
}

static int fake_read32(void *ctx, u16 off, u32 *val)
{
	struct fake *f = ctx;

	if (f->dev_lost) {
		f->ops_after_lost++;
		*val = 0xffffffffu;
		return 0;
	}

	f->hw_reads++;
	if (f->fail_read_at > 0 && f->hw_reads == f->fail_read_at)
		return -5;

	switch (off) {
	case OB_UCODE_REG_MACCONTROL:
		f->maccontrol_reads++;
		if (f->bad_readback_at > 0 &&
		    f->maccontrol_reads == f->bad_readback_at)
			*val = f->bad_readback_val;
		else
			*val = f->maccontrol;
		break;
	case OB_UCODE_REG_MACINTSTATUS:
		f->macintstatus_reads++;
		*val = fake_poll_value(f);
		break;
	case OB_UCODE_REG_OBJADDR:
		*val = f->objaddr;
		break;
	default:
		*val = 0xffffffffu;
		break;
	}

	trace_add(f, 'R', off, *val);

	/* The real trusted accessor latches on all-ones. */
	if (*val == 0xffffffffu)
		f->dev_lost = true;
	return 0;
}

static int fake_read16(void *ctx, u16 off, u16 *val)
{
	struct fake *f = ctx;
	unsigned int idx;

	if (f->dev_lost) {
		f->ops_after_lost++;
		return -5;
	}
	if (off < OB_UCODE_REG_OBJDATA)
		return -5;
	idx = (unsigned int)((off - OB_UCODE_REG_OBJDATA) >> 1);
	if (idx > 1)
		return -5;
	/* Amendment 01 C10..C12: one payload observation can be made to fail. */
	if (f->fail_shm_read_at > 0 &&
	    f->shm_reads == f->fail_shm_read_at - 1) {
		f->shm_reads++;
		return -5;
	}
	/* Both halves of the FIFO word come from the same fixture entry. */
	*val = f->shm[f->shm_reads & 0x3];
	f->shm_reads++;
	trace_add(f, 'r', off, *val);
	return 0;
}

static int fake_write32(void *ctx, u16 off, u32 val)
{
	struct fake *f = ctx;

	if (f->dev_lost) {
		f->ops_after_lost++;
		return -5;
	}

	f->hw_writes++;
	if (f->psm_seen && off == OB_UCODE_REG_OBJDATA)
		f->writes_after_latched++;

	switch (off) {
	case OB_UCODE_REG_MACCONTROL:
		f->maccontrol = val;
		if ((val & OB_UCODE_PSM_EXPECTED) == OB_UCODE_PSM_EXPECTED)
			f->psm_seen = true;
		break;
	case OB_UCODE_REG_MACINTSTATUS:
		/* write-to-clear */
		break;
	case OB_UCODE_REG_OBJADDR:
		f->objaddr = val;
		break;
	case OB_UCODE_REG_OBJDATA:
		f->objdata_writes++;
		if (f->fail_objdata_write_at > 0 &&
		    f->objdata_writes == f->fail_objdata_write_at) {
			f->dev_lost = true;
			return -5;
		}
		if (f->lose_after_words > 0 &&
		    f->objdata_writes == f->lose_after_words)
			f->dev_lost = true;
		/* The latch can also arrive with the last accepted word: that is
		 * the only state the post-upload barrier can ever observe. */
		if (f->lose_when_upload_done &&
		    f->objdata_writes == (int)OB_D2A_EXPECTED_WORDS)
			f->dev_lost = true;
		break;
	default:
		break;
	}

	trace_add(f, 'W', off, val);
	return 0;
}

static int fake_fw(void *ctx, const u8 **data, size_t *size,
		   const char **name)
{
	struct fake *f = ctx;

	f->fw_called = true;
	if (f->fw_ret)
		return f->fw_ret;
	if (!g_img)
		return -2;
	if (f->fw_corrupt) {
		/* Corrupt a private copy: the fixture itself stays intact. */
		if (!f->scratch) {
			f->scratch = malloc(g_img_size);
			if (!f->scratch)
				return -12;
			memcpy(f->scratch, g_img, g_img_size);
			f->scratch[100] ^= 0xff;
		}
		*data = f->scratch;
		*size = g_img_size;
		*name = "test/corrupt.bin";
		return 0;
	}
	if (f->fw_truncate) {
		*data = g_img;
		*size = g_img_size - 4;
		*name = "test/truncated.bin";
		return 0;
	}
	*data = g_img;
	*size = g_img_size;
	*name = "brcm/bcm43xx-ucode.fw";
	return 0;
}

static int fake_core(void *ctx, bool *core_up, bool *mutated)
{
	struct fake *f = ctx;

	f->core_called = true;
	if (mutated)
		*mutated = f->core_mutated;
	if (core_up)
		*core_up = f->core_up;
	return f->core_ret;
}

static void fake_delay(void *ctx, unsigned int us)
{
	struct fake *f = ctx;

	(void)us;
	f->delays++;
}

static bool fake_lost(void *ctx)
{
	return ((struct fake *)ctx)->dev_lost;
}

static void fake_log(void *ctx, enum ob_d2a_event ev,
		     const struct ob_d2a_log *rec)
{
	(void)ctx;
	(void)ev;
	(void)rec;
}

static void fake_init(struct fake *f)
{
	memset(f, 0, sizeof(*f));
	f->maccontrol = 0x04000404u;	/* settled upload state from a prior run */
	f->macintstatus_fixed = 0;
	f->use_script = true;
	f->script[0] = 0x00000008u;
	f->script[1] = 0x00000008u;
	f->script[2] = OB_D11_MI_MACSSPNDD;
	f->script_len = 3;
	f->shm[0] = 0x1234;
	f->shm[1] = 0x2345;
	f->shm[2] = 0x3456;
	f->shm[3] = 0x4567;
	f->core_up = true;
	f->core_mutated = true;
	f->fail_objdata_write_at = -1;
	f->lose_after_words = -1;
	f->fail_read_at = -1;
	f->bad_readback_at = -1;
	f->fail_shm_read_at = -1;
}

static void make_ops(struct ob_d2a_ops *ops, struct fake *f)
{
	ops->ctx = f;
	ops->fw_request = fake_fw;
	ops->core_prepare = fake_core;
	ops->read32_trusted = fake_read32;
	ops->read16 = fake_read16;
	ops->write32 = fake_write32;
	ops->delay_us = fake_delay;
	ops->log = fake_log;
	ops->dev_lost = fake_lost;
}

/* A single run with a fresh fake, standard success path unless overridden. */
static int run_once(void (*setup)(struct fake *), struct ob_d2a_result *res,
		    struct fake *f)
{
	struct ob_d2a_ops ops;

	fake_init(f);
	if (setup)
		setup(f);
	make_ops(&ops, f);
	return ob_d2a_run(&ops, f->dev_lost, res);
}

/* ---- T01: correct isolated-mode admission ------------------------------- */
static void t01(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(NULL, &r, &f);

	chk("T01", "admission ret", ret, 0);
	chkstr("T01", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_PSM_SUSPENDED);
	chk("T01", "psm_suspended", r.psm_suspended, 1);
	chk("T01", "psm_iterations", r.psm_iterations, 2);
	chk("T01", "fw_validated", r.fw_validated, 1);
	chk("T01", "core_up", r.core_up, 1);
	chk("T01", "words", r.words, OB_D2A_EXPECTED_WORDS);
	chk("T01", "writes_issued", r.writes_issued, OB_D2A_EXPECTED_WORDS);
	chk("T01", "backend objdata writes", f.objdata_writes,
	    OB_D2A_EXPECTED_WORDS);
	/* Amendment 01 (A1): the shared path performs NO SHM observation. */
	chk("T01", "shm_read", r.shm_read, 0);
	chk("T01", "shm observations", r.shm_observations, 0);
	chk("T01", "shm0 untouched", r.shm_fifosize[0], 0);
	chk("T01", "backend payload reads", f.shm_reads, 0);
	chk("T01", "run stage is DONE", r.stage, OB_D2A_STAGE_DONE);
	chk("T01", "retained", r.retained, 1);
	chk("T01", "mutated", r.mutated, 1);
	chk("T01", "device_lost", r.device_lost, 0);
	chk("T01", "ops after latch", f.ops_after_lost, 0);
	chk("T01", "writes after PSM", f.writes_after_latched, 0);
	/* Final MACCONTROL is the PSM value. */
	chk("T01", "maccontrol final", f.maccontrol, OB_UCODE_MACCONTROL_PSM);

	/* Exact ordering of the first accesses: read, write, read, ... */
	chk("T01", "first op", f.first[0].op, 'R');
	chk("T01", "first off", f.first[0].off, OB_UCODE_REG_MACCONTROL);
	chk("T01", "2nd op", f.first[1].op, 'W');
	chk("T01", "2nd off", f.first[1].off, OB_UCODE_REG_MACCONTROL);
	chk("T01", "2nd val", f.first[1].val, OB_UCODE_MACCONTROL_UPLOAD);
	chk("T01", "3rd op", f.first[2].op, 'R');
	chk("T01", "4th op", f.first[3].op, 'W');
	chk("T01", "4th off", f.first[3].off, OB_UCODE_REG_OBJADDR);
	chk("T01", "4th val", f.first[3].val, OB_UCODE_OBJADDR_AUTO_INC);
	chk("T01", "5th op", f.first[4].op, 'R');
	chk("T01", "6th off", f.first[5].off, OB_UCODE_REG_OBJDATA);
}

/* ---- T02: default load remains fail-closed ------------------------------ */
static void t02(void)
{
	chk("T02", "default load refused",
	    ob_isolated_default_load_refused(OB_ISOLATED_NONE, false, false), 1);
	chk("T02", "ucode mode admitted",
	    ob_isolated_default_load_refused(OB_ISOLATED_UCODE_TEST, false,
					     false), 0);
	chk("T02", "sprom evidence still admitted",
	    ob_isolated_default_load_refused(OB_ISOLATED_NONE, true, false), 0);
	chk("T02", "sflags diag still admitted",
	    ob_isolated_default_load_refused(OB_ISOLATED_NONE, false, true), 0);

	/* No flag at all selects no mode. */
	chk("T02", "no flags selects none",
	    ob_isolated_mode_select(false, false, false, false),
	    OB_ISOLATED_NONE);
}

/* ---- T03: conflicting modes reject -------------------------------------- */
static void t03(void)
{
	chk("T03", "ucode+initvals conflict",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(false, true, true, false)), 1);
	chk("T03", "ucode+dma conflict",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(false, true, false, true)), 1);
	chk("T03", "ucode+fw conflict",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(true, true, false, false)), 1);
	chk("T03", "ucode alone is not a conflict",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(false, true, false, false)), 0);
}

/* ---- T04: core-down rejects functional access --------------------------- */
static void core_down(struct fake *f)
{
	f->core_up = false;
}

static void t04(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(core_down, &r, &f);

	chk("T04", "ret non-zero", ret != 0, 1);
	chkstr("T04", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_CORE_NOT_READY);
	chk("T04", "no register reads", f.hw_reads, 0);
	chk("T04", "no register writes", f.hw_writes, 0);
	chk("T04", "psm not started", r.psm_started, 0);
	chk("T04", "core prep was called", f.core_called, 1);
	/* The core prep itself mutated: retained, not released. */
	chk("T04", "retained after prep mutation", r.retained, 1);
}

/* ---- T05: device loss prevents subsequent access ------------------------ */
static void lose_midway(struct fake *f)
{
	f->lose_after_words = 100;
}

static void t05(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(lose_midway, &r, &f);

	chk("T05", "ret non-zero", ret != 0, 1);
	chkstr("T05", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_DEVICE_LOSS);
	chk("T05", "device_lost", r.device_lost, 1);
	chk("T05", "no access after latch", f.ops_after_lost, 0);
	chk("T05", "upload stopped at latch", f.objdata_writes, 100);
	chk("T05", "psm not started", r.psm_started, 0);
	chk("T05", "retained", r.retained, 1);
	chk("T05", "issued equals observed writes", r.writes_issued, 100);
	/* The stop must name the operation that observed the latch, and no
	 * write may be attempted past it (Amendment 01, C14/C15 counters). */
	chk("T05", "reason names the in-upload latch",
	    (long)(r.reason && !strcmp(r.reason, "device_loss_during_upload")), 1);
	chk("T05", "completed words at the latch", r.upload_completed_words, 100);
	chk("T05", "writes attempted: maccontrol+objaddr+100 words",
	    r.writes_attempted, 102);
}

/* ---- T05b: the latch can arrive with the last accepted word --------------
 * With no words left, the in-loop barrier never runs again, so this is the
 * ONLY case the post-upload barrier can observe: the PSM must not be started
 * and the reason must name that barrier, not the next write's pre-check.
 */
static void lose_at_upload_end(struct fake *f)
{
	f->lose_when_upload_done = true;
}

static void t05b(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(lose_at_upload_end, &r, &f);

	chk("T05b", "ret non-zero", ret != 0, 1);
	chkstr("T05b", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_DEVICE_LOSS);
	chk("T05b", "all words were issued", r.writes_issued, 10850);
	chk("T05b", "observed objdata writes", f.objdata_writes, 10850);
	chk("T05b", "reason names the post-upload barrier",
	    (long)(r.reason && !strcmp(r.reason, "device_loss_after_upload")), 1);
	chk("T05b", "psm not started", r.psm_started, 0);
	chk("T05b", "no interrupt-state write", f.hw_writes, 10852);
	chk("T05b", "no access after latch", f.ops_after_lost, 0);
	chk("T05b", "retained", r.retained, 1);
}

/* ---- T06: all-ones MACCONTROL prerequisite rejects ---------------------- */
static void ones_maccontrol(struct fake *f)
{
	f->maccontrol = 0xffffffffu;
}

static void t06(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(ones_maccontrol, &r, &f);

	chk("T06", "ret non-zero", ret != 0, 1);
	chkstr("T06", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_DEVICE_LOSS);
	chk("T06", "suspicious recorded", r.suspicious, 1);
	chk("T06", "no writes at all", f.hw_writes, 0);
	chk("T06", "no upload", r.writes_issued, 0);
}

/* ---- T07: all-ones MACINTSTATUS never means PSM success ----------------- */
static void ones_macintstatus(struct fake *f)
{
	f->use_script = false;
	f->macintstatus_fixed = 0xffffffffu;
}

static void t07(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(ones_macintstatus, &r, &f);

	/* The bit test alone WOULD accept it: prove the defect is real. */
	chk("T07", "bit test alone passes", ob_ucode_mac_suspended(0xffffffffu),
	    1);
	chk("T07", "contract rejects all-ones", ob_d2a_psm_success(0xffffffffu),
	    0);
	chk("T07", "ret non-zero", ret != 0, 1);
	chkstr("T07", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_DEVICE_LOSS);
	chk("T07", "psm_suspended false", r.psm_suspended, 0);
	chk("T07", "no access after latch", f.ops_after_lost, 0);
}

/* ---- T08: firmware request failure performs no hardware operation ------- */
static void no_firmware(struct fake *f)
{
	f->fw_ret = -2; /* -ENOENT */
}

static void t08(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(no_firmware, &r, &f);

	chk("T08", "ret non-zero", ret != 0, 1);
	chkstr("T08", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_FW);
	chk("T08", "core prep not called", f.core_called, 0);
	chk("T08", "no register reads", f.hw_reads, 0);
	chk("T08", "no register writes", f.hw_writes, 0);
	chk("T08", "not retained", r.retained, 0);
}

/* ---- T09: firmware size/hash mismatch performs no upload ---------------- */
static void corrupt_image(struct fake *f)
{
	f->fw_corrupt = true;
}

static void short_image(struct fake *f)
{
	f->fw_truncate = true;
}

static void t09(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret;

	ret = run_once(corrupt_image, &r, &f);
	chk("T09", "corrupt hash ret", ret != 0, 1);
	chkstr("T09", "corrupt hash class", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_FW);
	chk("T09", "corrupt hash no reads", f.hw_reads, 0);
	chk("T09", "corrupt hash no writes", f.hw_writes, 0);
	chk("T09", "corrupt hash not retained", r.retained, 0);
	free(f.scratch);
	f.scratch = NULL;

	ret = run_once(short_image, &r, &f);
	chk("T09", "short size ret", ret != 0, 1);
	chkstr("T09", "short size class", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_FW);
	chk("T09", "short size no writes", f.hw_writes, 0);

	/* The image itself is pinned, not merely "parseable". */
	chk("T09", "image valid predicate", ob_d2a_image_valid(g_img, g_img_size),
	    1);
	chk("T09", "predicate rejects short",
	    ob_d2a_image_valid(g_img, g_img_size - 4), 0);
}

/* ---- T10: upload interruption cannot start PSM -------------------------- */
static void interrupted_upload(struct fake *f)
{
	f->fail_objdata_write_at = 5000;
}

static void t10(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(interrupted_upload, &r, &f);

	chk("T10", "ret non-zero", ret != 0, 1);
	chk("T10", "psm not started", r.psm_started, 0);
	chk("T10", "no MACCONTROL PSM write", f.psm_seen, 0);
	chk("T10", "upload stopped", f.objdata_writes, 5000);
	chk("T10", "issued 4999", r.writes_issued, 4999);
	chk("T10", "no access after latch", f.ops_after_lost, 0);

	/* The completeness predicate itself, independent of the path. */
	chk("T10", "predicate short", ob_d2a_upload_complete(10849, 10850), 0);
	chk("T10", "predicate exact", ob_d2a_upload_complete(10850, 10850), 1);
	chk("T10", "predicate over", ob_d2a_upload_complete(10851, 10850), 0);
}

/* ---- T11: issued writes are distinguished from confirmed state ---------- */
static void t11(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(NULL, &r, &f);

	chk("T11", "ret", ret, 0);
	chk("T11", "issued", r.writes_issued, OB_D2A_EXPECTED_WORDS);
	chk("T11", "confirmed is zero", r.writes_confirmed, 0);
	chk("T11", "not confirmable", ob_d2a_upload_confirmable(), 0);
	chk("T11", "issued == backend writes", r.writes_issued,
	    f.objdata_writes);
	/* A suppressed write is not counted as issued (T10 already showed it). */
	chk("T11", "control writes are counted separately",
	    r.writes_issued_total > r.writes_issued, 1);
	/* Counting fix (2026-10-25): reads are split, not pooled. */
	chk("T11", "one poll read per iteration plus the success read",
	    r.reads_poll, r.psm_iterations + 1);
	chk("T11", "poll reads are a subset of trusted reads",
	    r.reads_poll <= r.reads_trusted, 1);
	chk("T11", "fixed control reads remain derivable",
	    r.reads_trusted - r.reads_poll > 0, 1);
}

/* ---- T12: bounded polling times out correctly --------------------------- */
static void never_suspends(struct fake *f)
{
	f->use_script = false;
	f->macintstatus_fixed = 0x00000008u;
}

static void t12(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(never_suspends, &r, &f);

	chk("T12", "ret non-zero", ret != 0, 1);
	chkstr("T12", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_PSM_TIMEOUT);
	chk("T12", "iterations bounded", r.psm_iterations,
	    OB_D2A_POLL_MAX_ITERATIONS);
	chk("T12", "delays bounded", f.delays, OB_D2A_POLL_MAX_ITERATIONS);
	chk("T12", "reads bounded", f.macintstatus_reads,
	    OB_D2A_POLL_MAX_ITERATIONS + 1);
	chk("T12", "no access after stop", f.ops_after_lost, 0);
	chk("T12", "retained", r.retained, 1);
}

/* ---- T13: valid synthetic PSM completion is classified correctly -------- */
static void fast_suspend(struct fake *f)
{
	f->use_script = true;
	f->script[0] = OB_D11_MI_MACSSPNDD;
	f->script_len = 1;
}

static void t13(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret = run_once(fast_suspend, &r, &f);

	chk("T13", "ret", ret, 0);
	chkstr("T13", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_PSM_SUSPENDED);
	chk("T13", "iterations", r.psm_iterations, 0);
	chk("T13", "status", r.psm_status, OB_D11_MI_MACSSPNDD);
	chk("T13", "psm_started", r.psm_started, 1);
	chk("T13", "psm_suspended", r.psm_suspended, 1);
	chk("T13", "confirmed still zero", r.writes_confirmed, 0);
}

/* ---- T14: hardware-mutating failure retains the device safely ----------- */
static void bad_upload_readback(struct fake *f)
{
	f->bad_readback_at = 2;	/* the post-write MACCONTROL readback */
	f->bad_readback_val = 0x00000000u;
}

static void bad_psm_readback(struct fake *f)
{
	/* Fourth MACCONTROL read: the readback of the PSM start write. */
	f->bad_readback_at = 4;
	f->bad_readback_val = 0x00000000u;
}

static void t14(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret;

	ret = run_once(bad_upload_readback, &r, &f);
	chk("T14", "ret non-zero", ret != 0, 1);
	chkstr("T14", "classification", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_PREREQ);
	chk("T14", "mutated", r.mutated, 1);
	chk("T14", "retained", r.retained, 1);
	chk("T14", "no PSM", r.psm_started, 0);
	chk("T14", "no upload", f.objdata_writes, 0);

	/* A PSM start that cannot be validated is rejected, never polled. */
	ret = run_once(bad_psm_readback, &r, &f);
	chk("T14", "psm readback ret non-zero", ret != 0, 1);
	chkstr("T14", "psm readback class", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_PSM_START_REJECTED);
	chk("T14", "psm readback not suspended", r.psm_suspended, 0);
	chk("T14", "psm readback not started", r.psm_started, 0);
	chk("T14", "psm readback retained", r.retained, 1);
	chk("T14", "psm write was issued", f.psm_seen, 1);

	/* A failure before any mutation is NOT retained. */
	ret = run_once(no_firmware, &r, &f);
	chk("T14", "fw failure not retained", r.retained, 0);
	chk("T14", "fw failure not mutated", r.mutated, 0);

	/* Retention predicate itself. */
	chk("T14", "retain on mutation", ob_d2a_retained(true, false), 1);
	chk("T14", "retain on loss", ob_d2a_retained(false, true), 1);
	chk("T14", "clean when untouched", ob_d2a_retained(false, false), 0);
}

/* ---- admission/contract-defect checks ----------------------------------- */
static void t00(void)
{
	struct ob_d2a_ops ops;
	struct ob_d2a_result r;
	struct fake f;
	int ret;

	fake_init(&f);
	ops.ctx = &f;
	memset(&ops, 0, sizeof(ops));
	ret = ob_d2a_run(&ops, false, &r);
	chk("T00", "invalid ops rejected", ret, OB_D2A_RET_EINVAL);
	chk("T00", "invalid ops touch nothing", f.hw_reads + f.hw_writes, 0);

	/* An already-latched device issues zero callbacks. */
	fake_init(&f);
	f.dev_lost = true;
	make_ops(&ops, &f);
	ret = ob_d2a_run(&ops, true, &r);
	chk("T00", "latched entry rejected", ret != 0, 1);
	chkstr("T00", "latched entry class", ob_d2a_class_token(r.class),
	       OB_D2A_TOKEN_DEVICE_LOSS);
	chk("T00", "latched entry no access", f.hw_reads + f.hw_writes, 0);
	chk("T00", "latched entry retained", r.retained, 1);
}


/* ====================================================================== *
 * Amendment 01 — SHM diagnostic ownership and fail-stop
 * ====================================================================== */

/* C01/C02/C04: the shared path has none; one explicit call has exactly four. */
static void a01_tail(void)
{
	struct ob_d2a_result r;
	struct ob_d2a_ops ops;
	struct fake f;
	u16 fifo[4] = { 0, 0, 0, 0 };
	int ret = run_once(NULL, &r, &f);
	int writes0, reads0;

	chk("C01", "shared run ret", ret, 0);
	chk("C01", "shared run ends DONE", r.stage, OB_D2A_STAGE_DONE);
	chk("C01", "shared run shm_read", r.shm_read, 0);
	chk("C01", "shared run observations", r.shm_observations, 0);
	chk("C01", "shared run payload reads", f.shm_reads, 0);
	chk("C01", "shared run class", ob_d2a_class_token(r.class) != NULL, 1);

	writes0 = f.hw_writes;
	reads0 = f.hw_reads;
	make_ops(&ops, &f);
	ret = ob_d2a_shm_diag(&ops, &r, fifo);

	chk("C02", "diag ret", ret, 0);
	chk("C02", "observations", r.shm_observations, 4);
	chk("C02", "shm_read", r.shm_read, 1);
	chk("C02", "fifo0", fifo[0], 0x1234);
	chk("C02", "fifo1", fifo[1], 0x2345);
	chk("C02", "fifo2", fifo[2], 0x3456);
	chk("C02", "fifo3", fifo[3], 0x4567);
	chk("C02", "payload reads", f.shm_reads, 4);
	chk("C02", "one OBJADDR write per observation", f.hw_writes - writes0, 4);
	chk("C02", "one barrier read per observation", f.hw_reads - reads0, 4);
	chk("C02", "class kept after a clean diagnostic",
	    (long)(r.class == OB_D2A_CLASS_PSM_SUSPENDED), 1);

	/* C04: run + one diagnostic == exactly four, never eight. */
	chk("C04", "total payload reads", f.shm_reads, 4);
	chk("C04", "no duplicate diagnostic is reachable", f.shm_reads < 8, 1);
}

/* The diagnostic refuses to run without a classified PSM suspend. */
static void a01_diag_gate(void)
{
	struct ob_d2a_result r;
	struct ob_d2a_ops ops;
	struct fake f;
	u16 fifo[4] = { 0, 0, 0, 0 };
	int ret;

	fake_init(&f);
	ob_d2a_result_init(&r);
	make_ops(&ops, &f);
	ret = ob_d2a_shm_diag(&ops, &r, fifo);
	chk("C02", "diag without PSM refused", ret, OB_D2A_RET_EINVAL);
	chk("C02", "diag without PSM touches nothing",
	    f.hw_reads + f.hw_writes, 0);
	chk("C02", "diag without PSM sets the reason",
	    (long)(r.reason && !strcmp(r.reason, "shm_diag_without_psm")), 1);
}

/* C10/C11/C12: a failing observation stops the diagnostic at that point. */
static void a01_shm_failstop(long fail_at, const char *id)
{
	struct ob_d2a_result r;
	struct ob_d2a_ops ops;
	struct fake f;
	u16 fifo[4] = { 0, 0, 0, 0 };
	int ret = run_once(NULL, &r, &f);
	int writes0;

	chk(id, "shared run ret", ret, 0);
	f.fail_shm_read_at = (int)fail_at;
	writes0 = f.hw_writes;
	make_ops(&ops, &f);
	ret = ob_d2a_shm_diag(&ops, &r, fifo);

	chk(id, "diag ret non-zero", ret != 0, 1);
	chk(id, "completed observations", r.shm_observations, fail_at - 1);
	chk(id, "shm_read stays false", r.shm_read, 0);
	chk(id, "payload attempts", f.shm_reads, fail_at);
	chk(id, "one OBJADDR write per attempted observation",
	    f.hw_writes - writes0, fail_at);
	chk(id, "failed index", r.shm_failed_index, fail_at - 1);
	if (fail_at < 4)
		chk(id, "later FIFO register untouched", fifo[fail_at], 0);
	else
		chk(id, "no observation after the fourth",
		    r.shm_observations, 3);
	chk(id, "reason is the accessor failure",
	    (long)(r.reason && !strcmp(r.reason, "shm_diag_read_failed")), 1);
	chk(id, "no access after the stop", f.ops_after_lost, 0);
}

/* C13..C16: regression of the properties the shared path must keep. */
static void a01_regression(void)
{
	struct ob_d2a_result r;
	struct fake f;
	int ret;

	/* C13: an all-ones MACINTSTATUS can never be PSM success. */
	{
		ret = run_once(ones_macintstatus, &r, &f);
		chk("C13", "all-ones status ret non-zero", ret != 0, 1);
		chk("C13", "all-ones status not success", r.psm_suspended, 0);
		chkstr("C13", "all-ones status class",
		       ob_d2a_class_token(r.class), OB_D2A_TOKEN_DEVICE_LOSS);
	}

	/* C14: the successful path still issues exactly 10850 words. */
	ret = run_once(NULL, &r, &f);
	chk("C14", "ret", ret, 0);
	chk("C14", "issued words", r.writes_issued, OB_D2A_EXPECTED_WORDS);
	chk("C14", "backend writes", f.objdata_writes, OB_D2A_EXPECTED_WORDS);
	chk("C14", "confirmed words", r.writes_confirmed, 0);

	/* C15: a partial upload never reaches PSM. */
	ret = run_once(interrupted_upload, &r, &f);
	chk("C15", "partial upload ret non-zero", ret != 0, 1);
	chk("C15", "partial upload not suspended", r.psm_suspended, 0);
	chk("C15", "partial upload stage",
	    r.failed_stage == OB_D2A_STAGE_UPLOAD, 1);

	/* C16: a mutating failure retains the device. */
	ret = run_once(bad_upload_readback, &r, &f);
	chk("C16", "mutating failure ret non-zero", ret != 0, 1);
	chk("C16", "mutating failure retained", r.retained, 1);
	chk("C16", "mutating failure mutated", r.mutated, 1);
}

int main(void)
{
	if (!load_image()) {
		printf("FAIL: cannot load the rev42 ucode image (43400 bytes, "
		       "FNV 0x%016llx)\n",
		       (unsigned long long)OB_FW_UCODE42_FNV);
		return 1;
	}
	chk("IMG", "image size", (long)g_img_size, OB_FW_UCODE42_SIZE);
	chk("IMG", "image words", (long)ob_ucode_words_from_size(g_img_size),
	    OB_D2A_EXPECTED_WORDS);

	t00();
	t01();
	t02();
	t03();
	t04();
	t05();
	t05b();
	t06();
	t07();
	t08();
	t09();
	t10();
	t11();
	t12();
	t13();
	t14();

	a01_tail();
	a01_diag_gate();
	a01_shm_failstop(1, "C10");
	a01_shm_failstop(2, "C11");
	a01_shm_failstop(3, "C12");
	a01_shm_failstop(4, "C12");
	a01_regression();

	free(g_img);

	printf("openbrcm d2a tests: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}

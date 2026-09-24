// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the isolated common-initvals mode (M3.4D2B).
 *
 * Pure plan/count/postcondition/mode-selection helpers only. No hardware, no
 * MMIO. The real vendor-derived table is checked when installed.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/ob_initvals.h"
#include "../../src/ob_ucode.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

/* Build one vendor IV record: offset, width, value (little-endian). */
static void put_iv(u8 *p, u16 off, u16 width, u32 val)
{
	p[0] = off & 0xff;
	p[1] = off >> 8;
	p[2] = width & 0xff;
	p[3] = width >> 8;
	p[4] = val & 0xff;
	p[5] = (val >> 8) & 0xff;
	p[6] = (val >> 16) & 0xff;
	p[7] = (val >> 24) & 0xff;
}

/* A valid small plan: w16, w32, w16, terminator -> 3 writes (2 w16, 1 w32). */
static void test_plan_valid(void)
{
	u8 t[4 * 8];
	struct ob_initvals_plan plan;

	put_iv(t + 0, 0x686, 2, 0x09d0);
	put_iv(t + 8, 0x160, 4, 0x03010005);
	put_iv(t + 16, 0x124, 2, 0x0004);
	put_iv(t + 24, 0xffff, 0, 0);

	chk("valid plan", ob_initvals_plan_from_table(t, sizeof(t), &plan), 0);
	chk("plan records", (long)plan.records, 3);
	chk("plan w16", (long)plan.w16, 2);
	chk("plan w32", (long)plan.w32, 1);
	/* Terminator excluded: records (3) != record slots (4). */
	chk("plan terminator excluded", (long)plan.records, 3);
}

/* Malformed/truncated/missing terminator must be rejected before any write. */
static void test_plan_bad(void)
{
	u8 t[4 * 8];
	struct ob_initvals_plan plan;

	put_iv(t + 0, 0x160, 4, 1);
	chk("plan truncated", ob_initvals_plan_from_table(t, 7, &plan), -EINVAL);
	chk("plan zero", ob_initvals_plan_from_table(t, 0, &plan), -EINVAL);

	put_iv(t + 0, 0x160, 4, 1);
	put_iv(t + 8, 0x164, 4, 1);
	chk("plan no terminator",
	    ob_initvals_plan_from_table(t, 16, &plan), -ENODATA);

	put_iv(t + 0, 0x160, 1, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	chk("plan bad width",
	    ob_initvals_plan_from_table(t, 16, &plan), -EINVAL);

	put_iv(t + 0, 0x160, 4, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	put_iv(t + 16, 0x164, 4, 1);
	chk("plan data after terminator",
	    ob_initvals_plan_from_table(t, 24, &plan), -EINVAL);
}

/* Exact M3.4D2B write-count invariant. */
static void test_counts(void)
{
	chk("counts ok", ob_initvals_counts_ok(610, 113, 497), 1);
	chk("counts short", ob_initvals_counts_ok(609, 113, 497), 0);
	chk("counts w16 wrong", ob_initvals_counts_ok(610, 114, 496), 0);
	chk("counts w32 wrong", ob_initvals_counts_ok(610, 112, 498), 0);
	chk("counts zero", ob_initvals_counts_ok(0, 0, 0), 0);

	chk("shape ok", (long)ob_initvals_plan_ok(&(struct ob_initvals_plan){
			    610, 113, 497 }), 1);
	chk("shape bad", (long)ob_initvals_plan_ok(&(struct ob_initvals_plan){
			    610, 113, 496 }), 0);
}

/* Postcondition validation: all expected -> OK; any deviation -> fail. */
static void test_post(void)
{
	struct ob_initvals_post p = {
		.fifosize0 = OB_INITVALS_FIFOSIZE0_EXPECTED,
		.fifosize1 = OB_INITVALS_FIFOSIZE1_EXPECTED,
		.fifosize2 = OB_INITVALS_FIFOSIZE2_EXPECTED,
		.fifosize3 = OB_INITVALS_FIFOSIZE3_EXPECTED,
		.macintmask = OB_INITVALS_MACINTMASK_EXPECTED,
		.maccontrol = OB_INITVALS_MACCONTROL_EXPECTED,
		.shm14 = OB_INITVALS_SHM14_EXPECTED,
	};

	chk("post ok", ob_initvals_post_ok(&p), 1);

	p.fifosize0 = 0x0000;
	chk("post fifo0 mismatch", ob_initvals_post_ok(&p), 0);
	p.fifosize0 = OB_INITVALS_FIFOSIZE0_EXPECTED;

	p.macintmask = 0x00000001;
	chk("post macintmask mismatch", ob_initvals_post_ok(&p), 0);
	p.macintmask = OB_INITVALS_MACINTMASK_EXPECTED;

	p.maccontrol = 0x04020403;
	chk("post maccontrol mismatch", ob_initvals_post_ok(&p), 0);
	p.maccontrol = OB_INITVALS_MACCONTROL_EXPECTED;

	p.shm14 = 0x00000000;
	chk("post shm14 mismatch", ob_initvals_post_ok(&p), 0);

	chk("post null", ob_initvals_post_ok(NULL), 0);
}

/*
 * Path selection and teardown. The initvals mode must be selected only when
 * requested, and every isolated mode must skip teardown; the D2A regression is
 * that only the initvals mode applies the common table.
 */
static void test_path_and_teardown(void)
{
	enum ob_isolated_mode mode;

	mode = ob_isolated_mode_select(false, false, true, false);
	chk("select initvals mode", mode, OB_ISOLATED_INITVALS_TEST);
	chk("initvals applies table", ob_isolated_mode_applies_initvals(mode), 1);
	chk("initvals skips teardown", ob_isolated_mode_skips_teardown(mode), 1);
	chk("initvals no dma", ob_isolated_mode_uses_dma(mode), 0);

	mode = ob_isolated_mode_select(false, true, false, false);
	chk("select ucode mode", mode, OB_ISOLATED_UCODE_TEST);
	chk("ucode does not apply table",
	    ob_isolated_mode_applies_initvals(mode), 0);
	chk("ucode skips teardown", ob_isolated_mode_skips_teardown(mode), 1);

	/*
	 * dma_test_only owns the DMA lifecycle and is not teardown-skipping. It
	 * MUST apply the common table first (its entry state is the D2B exit).
	 */
	mode = ob_isolated_mode_select(false, false, false, true);
	chk("select dma mode", mode, OB_ISOLATED_DMA_TEST);
	chk("dma uses dma", ob_isolated_mode_uses_dma(mode), 1);
	chk("dma applies common table",
	    ob_isolated_mode_applies_initvals(mode), 1);
	chk("dma does not skip teardown",
	    ob_isolated_mode_skips_teardown(mode), 0);

	mode = ob_isolated_mode_select(false, false, false, false);
	chk("select normal", mode, OB_ISOLATED_NONE);
	chk("normal needs teardown", ob_isolated_mode_skips_teardown(mode), 0);

	/* D2A regression: short upload blocks PSM start (no initvals reached). */
	chk("short upload no psm", ob_ucode_writes_ok(10849, 10850), 0);
}

/*
 * Failure before PSM start and PSM timeout are pure state transitions: prove
 * the D2A poll terminates exactly at the bound, which is what makes a failure
 * deterministic and non-retrying.
 */
static void test_psm_failure_paths(void)
{
	u32 max = ob_ucode_poll_max_iterations(OB_UCODE_POLL_TIMEOUT,
					       OB_UCODE_POLL_STEP);
	u32 remaining = OB_UCODE_POLL_TIMEOUT;
	u32 iters = 0, status = 0;

	for (;;) {
		if (ob_ucode_mac_suspended(status))
			break;
		if (ob_ucode_poll_expired(remaining, OB_UCODE_POLL_STEP))
			break;
		remaining -= OB_UCODE_POLL_STEP;
		iters++;
	}
	chk("psm timeout bounded", iters, max);
	chk("psm timeout detected",
	    ob_ucode_mac_suspended(status), 0);
}

/* Optional: validate the real installed table if present. */
static void test_real_file(void)
{
	const char *path = "/lib/firmware/brcm/bcm4352-d11ac1initvals42.bin";
	struct ob_initvals_plan plan;
	u8 *buf;
	long n;
	FILE *f;

	f = fopen(path, "rb");
	if (!f) {
		printf("skip: %s not installed\n", path);
		return;
	}
	buf = malloc(OB_FW_AC1INITVALS42_SIZE);
	if (!buf) {
		fclose(f);
		return;
	}
	n = fread(buf, 1, OB_FW_AC1INITVALS42_SIZE, f);
	fclose(f);
	chk("real plan size", n, (long)OB_FW_AC1INITVALS42_SIZE);
	chk("real plan parse",
	    ob_initvals_plan_from_table(buf, OB_FW_AC1INITVALS42_SIZE, &plan),
	    0);
	chk("real plan records", (long)plan.records, (long)OB_INITVALS_RECORDS);
	chk("real plan w16", (long)plan.w16, (long)OB_INITVALS_W16);
	chk("real plan w32", (long)plan.w32, (long)OB_INITVALS_W32);
	chk("real plan ok", ob_initvals_plan_ok(&plan), 1);
	free(buf);
}

int main(void)
{
	test_plan_valid();
	test_plan_bad();
	test_counts();
	test_post();
	test_path_and_teardown();
	test_psm_failure_paths();
	test_real_file();

	if (failures) {
		printf("openbrcm initvals tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm initvals tests: PASS\n");
	return 0;
}

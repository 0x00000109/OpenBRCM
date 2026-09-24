// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the isolated ucode-upload mode (M3.4D2A).
 *
 * Pure policy/arithmetic/poll/teardown helpers only. No hardware.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_ucode.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

/* Mode policy: at most one isolated mode, any pair is a conflict. */
static void test_mode_conflict(void)
{
	chk("mode normal", ob_isolated_mode_count(false, false, false, false), 0);
	chk("mode validate only",
	    ob_isolated_mode_count(true, false, false, false), 1);
	chk("mode ucode only",
	    ob_isolated_mode_count(false, true, false, false), 1);
	chk("mode initvals only",
	    ob_isolated_mode_count(false, false, true, false), 1);
	chk("mode dma only",
	    ob_isolated_mode_count(false, false, false, true), 1);

	chk("select none",
	    ob_isolated_mode_select(false, false, false, false),
	    OB_ISOLATED_NONE);
	chk("select validate",
	    ob_isolated_mode_select(true, false, false, false),
	    OB_ISOLATED_FW_VALIDATE);
	chk("select ucode",
	    ob_isolated_mode_select(false, true, false, false),
	    OB_ISOLATED_UCODE_TEST);
	chk("select initvals",
	    ob_isolated_mode_select(false, false, true, false),
	    OB_ISOLATED_INITVALS_TEST);
	chk("select dma",
	    ob_isolated_mode_select(false, false, false, true),
	    OB_ISOLATED_DMA_TEST);

	/* Every multiple-mode combination must be a conflict. */
	chk("conflict fw+ucode",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(true, true, false, false)), 1);
	chk("conflict fw+initvals",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(true, false, true, false)), 1);
	chk("conflict ucode+initvals",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(false, true, true, false)), 1);
	chk("conflict fw+dma",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(true, false, false, true)), 1);
	chk("conflict ucode+dma",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(false, true, false, true)), 1);
	chk("conflict initvals+dma",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(false, false, true, true)), 1);
	chk("conflict all four",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(true, true, true, true)), 1);
	chk("count all four",
	    ob_isolated_mode_count(true, true, true, true), 4);

	/* Only dma_test_only enters the DMA lifecycle; the others never do. */
	chk("dma mode uses dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_DMA_TEST), 1);
	chk("initvals mode no dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_INITVALS_TEST), 0);
	chk("ucode mode no dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_UCODE_TEST), 0);
	chk("fw mode no dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_FW_VALIDATE), 0);
	chk("none mode no dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_NONE), 0);

	/* dma mode must NOT be treated as teardown-skipping. */
	chk("dma mode not teardown-skip",
	    ob_isolated_mode_skips_teardown(OB_ISOLATED_DMA_TEST), 0);
	chk("ucode mode teardown-skip",
	    ob_isolated_mode_skips_teardown(OB_ISOLATED_UCODE_TEST), 1);

	/* Only the initvals mode applies the common table (D2A regression). */
	chk("ucode mode no initvals",
	    ob_isolated_mode_applies_initvals(OB_ISOLATED_UCODE_TEST), 0);
	chk("fw mode no initvals",
	    ob_isolated_mode_applies_initvals(OB_ISOLATED_FW_VALIDATE), 0);
	chk("initvals mode applies",
	    ob_isolated_mode_applies_initvals(OB_ISOLATED_INITVALS_TEST), 1);
	chk("none mode no initvals",
	    ob_isolated_mode_applies_initvals(OB_ISOLATED_NONE), 0);
}

/* Exact recovered register/bit vocabulary. */
static void test_constants(void)
{
	chk("maccontrol upload", OB_UCODE_MACCONTROL_UPLOAD, 0x04000404L);
	chk("maccontrol psm", OB_UCODE_MACCONTROL_PSM, 0x04020402L);
	chk("objaddr autoinc", OB_UCODE_OBJADDR_AUTO_INC, 0x03000000L);
	chk("objaddr shm sel", OB_UCODE_OBJADDR_SHM_SEL, 0x00010000L);
	chk("poll timeout", OB_UCODE_POLL_TIMEOUT, 0x000f4249L);
	chk("poll delay", OB_UCODE_POLL_DELAY_US, 10);
	chk("poll step", OB_UCODE_POLL_STEP, 10);

	/* Upload state sets IHR_EN|PSM_JMP0|WAKE and nothing forbidden. */
	chk("upload expected set",
	    (OB_UCODE_MACCONTROL_UPLOAD & OB_UCODE_UPLOAD_EXPECTED) ==
	    OB_UCODE_UPLOAD_EXPECTED, 1);
	chk("upload forbidden clear",
	    (OB_UCODE_MACCONTROL_UPLOAD & OB_UCODE_UPLOAD_FORBIDDEN) == 0, 1);
	/* PSM state sets IHR_EN|INFRA|PSM_RUN|WAKE, EN_MAC clear. */
	chk("psm expected set",
	    (OB_UCODE_MACCONTROL_PSM & OB_UCODE_PSM_EXPECTED) ==
	    OB_UCODE_PSM_EXPECTED, 1);
	chk("psm en_mac clear",
	    (OB_UCODE_MACCONTROL_PSM & OB_UCODE_PSM_FORBIDDEN) == 0, 1);
}

/* Write-count arithmetic: 43400 bytes == 10850 words, exactly once each. */
static void test_write_count(void)
{
	chk("words 43400", ob_ucode_words_from_size(43400), 10850);
	chk("words 0", ob_ucode_words_from_size(0), 0);
	chk("writes ok", ob_ucode_writes_ok(10850, 10850), 1);
	chk("writes short", ob_ucode_writes_ok(10849, 10850), 0);
	chk("writes long", ob_ucode_writes_ok(10851, 10850), 0);
}

/* Bounded poll helpers and the exact timeout path. */
static void test_poll_timeout(void)
{
	u32 max = ob_ucode_poll_max_iterations(OB_UCODE_POLL_TIMEOUT,
					       OB_UCODE_POLL_STEP);
	u32 remaining = OB_UCODE_POLL_TIMEOUT;
	u32 iters = 0, status = 0;

	chk("poll max", max, 100000);
	chk("poll expired 9", ob_ucode_poll_expired(9, 10), 1);
	chk("poll expired 10", ob_ucode_poll_expired(10, 10), 0);
	chk("poll expired 0", ob_ucode_poll_expired(0, 10), 1);
	chk("poll zero step", ob_ucode_poll_max_iterations(100, 0), 0);

	/* status never reports MI_MACSSPNDD -> must terminate at exactly max. */
	for (;;) {
		if (ob_ucode_mac_suspended(status))
			break;
		if (ob_ucode_poll_expired(remaining, OB_UCODE_POLL_STEP))
			break;
		remaining -= OB_UCODE_POLL_STEP;
		iters++;
	}
	chk("timeout iterations", iters, max);
	chk("timeout total us", (long)(iters * OB_UCODE_POLL_DELAY_US), 1000000);
}

static void test_poll_success(void)
{
	u32 remaining = OB_UCODE_POLL_TIMEOUT;
	u32 iters = 0, status = 0;

	for (;;) {
		status = (iters == 3) ? OB_D11_MI_MACSSPNDD : 0x00000008u;
		if (ob_ucode_mac_suspended(status))
			break;
		if (ob_ucode_poll_expired(remaining, OB_UCODE_POLL_STEP))
			break;
		remaining -= OB_UCODE_POLL_STEP;
		iters++;
	}
	chk("success detected", ob_ucode_mac_suspended(status), 1);
	chk("success iterations", iters, 3);

	chk("suspended bit set", ob_ucode_mac_suspended(0x1), 1);
	chk("suspended bit clear", ob_ucode_mac_suspended(0xfffffffe), 0);
}

/* Teardown state flags: isolated mode plans zero teardown steps. */
static void test_teardown(void)
{
	struct ob_ucode_teardown t = ob_ucode_teardown_plan(true);

	chk("iso mac80211", t.mac80211, 0);
	chk("iso rx", t.rx, 0);
	chk("iso irq", t.irq, 0);
	chk("iso dma", t.dma, 0);

	t = ob_ucode_teardown_plan(false);
	chk("normal mac80211", t.mac80211, 1);
	chk("normal rx", t.rx, 1);
	chk("normal irq", t.irq, 1);
	chk("normal dma", t.dma, 1);
}

int main(void)
{
	test_mode_conflict();
	test_constants();
	test_write_count();
	test_poll_timeout();
	test_poll_success();
	test_teardown();

	if (failures) {
		printf("openbrcm ucode tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm ucode tests: PASS\n");
	return 0;
}

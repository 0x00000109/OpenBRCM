// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host tests for the additive D11 tail stage plan (M1).
 *
 * No kernel API, no MMIO, no module load.  These tests pin the corrected
 * stop-point semantics from Phase 27:
 *
 *   DISABLED    no actions
 *   AFTER_FIXUP FIFO fixup only
 *   AFTER_DMA   fixup + T1 + DMA          (the Phase 26 defect dropped DMA)
 *   COMPLETE    fixup + T1 + DMA + T2 + validate
 *
 * and reject any stage value outside that set.
 */
#include <stdio.h>
#include "../../src/ob_d11_tail_plan.h"

static int failures;
static int checks;

static void chk(const char *what, long got, long exp)
{
	checks++;
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

static void test_default_policy(void)
{
	/* COMPLETE must never be the default. */
	chk("default is DISABLED",
	    (long)OB_D11_TAIL_STOP_DEFAULT, (long)OB_D11_TAIL_STOP_DISABLED);
	chk("default differs from COMPLETE",
	    OB_D11_TAIL_STOP_DEFAULT != OB_D11_TAIL_STOP_COMPLETE, 1);
	chk("default differs from AFTER_DMA",
	    OB_D11_TAIL_STOP_DEFAULT != OB_D11_TAIL_STOP_AFTER_DMA, 1);
}

static void test_stage_values(void)
{
	chk("DISABLED value", OB_D11_TAIL_STOP_DISABLED, 0);
	chk("AFTER_FIXUP value", OB_D11_TAIL_STOP_AFTER_FIXUP, 1);
	chk("AFTER_DMA value", OB_D11_TAIL_STOP_AFTER_DMA, 2);
	chk("COMPLETE value", OB_D11_TAIL_STOP_COMPLETE, 3);

	/* distinct names, no aliasing */
	chk("values distinct",
	    (OB_D11_TAIL_STOP_DISABLED != OB_D11_TAIL_STOP_AFTER_FIXUP) &&
	    (OB_D11_TAIL_STOP_AFTER_FIXUP != OB_D11_TAIL_STOP_AFTER_DMA) &&
	    (OB_D11_TAIL_STOP_AFTER_DMA != OB_D11_TAIL_STOP_COMPLETE), 1);
}

static void test_disabled_is_empty(void)
{
	enum ob_d11_tail_stop s = OB_D11_TAIL_STOP_DISABLED;

	chk("DISABLED valid", ob_d11_tail_stop_valid(s), 1);
	chk("DISABLED mask", ob_d11_tail_action_mask(s), 0);
	chk("DISABLED fixup", ob_d11_tail_does_fixup(s), 0);
	chk("DISABLED t1", ob_d11_tail_does_t1(s), 0);
	chk("DISABLED dma", ob_d11_tail_does_dma(s), 0);
	chk("DISABLED t2", ob_d11_tail_does_t2(s), 0);
	chk("DISABLED validate", ob_d11_tail_does_validate(s), 0);
}

static void test_after_fixup(void)
{
	enum ob_d11_tail_stop s = OB_D11_TAIL_STOP_AFTER_FIXUP;

	chk("AFTER_FIXUP valid", ob_d11_tail_stop_valid(s), 1);
	chk("AFTER_FIXUP mask", ob_d11_tail_action_mask(s),
	    OB_D11_TAIL_ACT_FIXUP);
	chk("AFTER_FIXUP fixup", ob_d11_tail_does_fixup(s), 1);
	chk("AFTER_FIXUP no t1", ob_d11_tail_does_t1(s), 0);
	chk("AFTER_FIXUP no dma", ob_d11_tail_does_dma(s), 0);
	chk("AFTER_FIXUP no t2", ob_d11_tail_does_t2(s), 0);
	chk("AFTER_FIXUP no validate", ob_d11_tail_does_validate(s), 0);
}

/*
 * Regression for the Phase 26 defect: `if (stop > AFTER_DMA)` skipped DMA for
 * stop == AFTER_DMA while T1 had already run.  The correct AFTER_DMA stage has
 * fixup + T1 + DMA; it must not equal the fixup+T1-only mask.
 */
static void test_after_dma_regression(void)
{
	enum ob_d11_tail_stop s = OB_D11_TAIL_STOP_AFTER_DMA;
	u32 mask = ob_d11_tail_action_mask(s);
	u32 phase26_bug = OB_D11_TAIL_ACT_FIXUP | OB_D11_TAIL_ACT_T1;

	chk("AFTER_DMA valid", ob_d11_tail_stop_valid(s), 1);
	chk("AFTER_DMA has fixup", ob_d11_tail_does_fixup(s), 1);
	chk("AFTER_DMA has t1", ob_d11_tail_does_t1(s), 1);
	chk("AFTER_DMA has dma", ob_d11_tail_does_dma(s), 1);
	chk("AFTER_DMA no t2", ob_d11_tail_does_t2(s), 0);
	chk("AFTER_DMA no validate", ob_d11_tail_does_validate(s), 0);
	chk("AFTER_DMA mask exact", mask,
	    OB_D11_TAIL_ACT_FIXUP | OB_D11_TAIL_ACT_T1 | OB_D11_TAIL_ACT_DMA);
	chk("AFTER_DMA is not the Phase 26 bug", mask == phase26_bug, 0);
	chk("AFTER_DMA has a DMA action", (mask & OB_D11_TAIL_ACT_DMA) != 0, 1);
}

static void test_complete(void)
{
	enum ob_d11_tail_stop s = OB_D11_TAIL_STOP_COMPLETE;

	chk("COMPLETE valid", ob_d11_tail_stop_valid(s), 1);
	chk("COMPLETE mask", ob_d11_tail_action_mask(s), OB_D11_TAIL_ACT_ALL);
	chk("COMPLETE fixup", ob_d11_tail_does_fixup(s), 1);
	chk("COMPLETE t1", ob_d11_tail_does_t1(s), 1);
	chk("COMPLETE dma", ob_d11_tail_does_dma(s), 1);
	chk("COMPLETE t2", ob_d11_tail_does_t2(s), 1);
	chk("COMPLETE validate", ob_d11_tail_does_validate(s), 1);
}

static void test_invalid_rejected(void)
{
	enum ob_d11_tail_stop bad[] = {
		(enum ob_d11_tail_stop)-1,
		(enum ob_d11_tail_stop)4,
		(enum ob_d11_tail_stop)99,
	};
	unsigned int i;

	for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		chk("invalid not valid", ob_d11_tail_stop_valid(bad[i]), 0);
		chk("invalid mask", ob_d11_tail_action_mask(bad[i]), 0);
		chk("invalid no fixup", ob_d11_tail_does_fixup(bad[i]), 0);
		chk("invalid no dma", ob_d11_tail_does_dma(bad[i]), 0);
		chk("invalid no validate",
		    ob_d11_tail_does_validate(bad[i]), 0);
	}
}

static void test_prefix_invariant(void)
{
	enum ob_d11_tail_stop all[] = {
		OB_D11_TAIL_STOP_DISABLED,
		OB_D11_TAIL_STOP_AFTER_FIXUP,
		OB_D11_TAIL_STOP_AFTER_DMA,
		OB_D11_TAIL_STOP_COMPLETE,
	};
	unsigned int i, j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			int expect = i <= j;

			chk("prefix invariant",
			    ob_d11_tail_is_prefix(all[i], all[j]), expect);
		}
	}

	/* invalid endpoints never form a prefix */
	chk("prefix invalid lower",
	    ob_d11_tail_is_prefix((enum ob_d11_tail_stop)9,
				  OB_D11_TAIL_STOP_COMPLETE), 0);
	chk("prefix invalid upper",
	    ob_d11_tail_is_prefix(OB_D11_TAIL_STOP_DISABLED,
				  (enum ob_d11_tail_stop)9), 0);
}

/*
 * Software-only admission guard.  These cases execute the exact predicate
 * ob_d11_tail_run() calls before its first register read.  They do not prove
 * hardware readiness (the live D2B re-check remains mandatory).
 */
static void test_entry_guard(void)
{
	const struct ob_d11_tail_entry_guard clean = { 0 };
	struct ob_d11_tail_entry_guard bad;

	/* S1: DISABLED performs no access, so every latched flag is irrelevant. */
	bad = clean;
	bad.dev_lost = true;
	bad.dma_fatal = true;
	bad.fifo_retry_forbidden = true;
	bad.reboot_required = true;
	bad.tail_started = true;
	bad.dma_live = true;
	chk("S1 disabled ignores all latches",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_DISABLED, &bad),
	    OB_D11_TAIL_ENTRY_OK);
	chk("S1 disabled accepts NULL guard",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_DISABLED, NULL),
	    OB_D11_TAIL_ENTRY_OK);

	/* S2: invalid stop is refused before any state is inspected. */
	chk("S2 invalid stop clean state",
	    ob_d11_tail_entry_check((enum ob_d11_tail_stop)4, &clean),
	    OB_D11_TAIL_ENTRY_INVALID_STOP);
	chk("S2 invalid stop wins over lost device",
	    ob_d11_tail_entry_check((enum ob_d11_tail_stop)-1, &bad),
	    OB_D11_TAIL_ENTRY_INVALID_STOP);
	chk("S2 NULL guard for COMPLETE refused",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, NULL),
	    OB_D11_TAIL_ENTRY_INVALID_STATE);

	/* S4/S8/S12/S14/S15: each latch denies the valid stop. */
	bad = clean;
	bad.dev_lost = true;
	chk("S15 device lost",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &bad),
	    OB_D11_TAIL_ENTRY_DEVICE_LOST);
	bad = clean;
	bad.dma_fatal = true;
	chk("S8 DMA fatal",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &bad),
	    OB_D11_TAIL_ENTRY_DMA_FATAL);
	bad = clean;
	bad.fifo_retry_forbidden = true;
	chk("S4 FIFO latch",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_AFTER_FIXUP, &bad),
	    OB_D11_TAIL_ENTRY_FIFO_LATCHED);
	bad = clean;
	bad.reboot_required = true;
	chk("S9/S10 reboot-required stage failure",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &bad),
	    OB_D11_TAIL_ENTRY_REBOOT_REQUIRED);
	bad = clean;
	bad.tail_started = true;
	chk("S14 staged re-entry not proven",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &bad),
	    OB_D11_TAIL_ENTRY_ALREADY_STARTED);
	bad = clean;
	bad.dma_live = true;
	chk("S12/S14 live DMA duplicate",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_AFTER_DMA, &bad),
	    OB_D11_TAIL_ENTRY_DMA_LIVE);

	/* Precedence is fixed: loss outranks fatal, FIFO and live DMA. */
	bad = clean;
	bad.dev_lost = true;
	bad.dma_fatal = true;
	bad.fifo_retry_forbidden = true;
	bad.reboot_required = true;
	bad.tail_started = true;
	bad.dma_live = true;
	chk("precedence device lost",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &bad),
	    OB_D11_TAIL_ENTRY_DEVICE_LOST);

	/* S11/S13: clean state admits the non-empty stop points. */
	chk("S11 AFTER_FIXUP clean",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_AFTER_FIXUP, &clean),
	    OB_D11_TAIL_ENTRY_OK);
	chk("S12 AFTER_DMA clean",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_AFTER_DMA, &clean),
	    OB_D11_TAIL_ENTRY_OK);
	chk("S13 COMPLETE clean",
	    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &clean),
	    OB_D11_TAIL_ENTRY_OK);

	/* S16: the helper is stateless and per-device guards do not bleed. */
	{
		struct ob_d11_tail_entry_guard a = { 0 };
		struct ob_d11_tail_entry_guard b = { 0 };

		a.dev_lost = true;
		chk("S16 device A denied",
		    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &a),
		    OB_D11_TAIL_ENTRY_DEVICE_LOST);
		chk("S16 device B admitted",
		    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &b),
		    OB_D11_TAIL_ENTRY_OK);
		chk("S16 device A still denied",
		    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_COMPLETE, &a),
		    OB_D11_TAIL_ENTRY_DEVICE_LOST);
	}
}

int main(void)
{
	test_default_policy();
	test_stage_values();
	test_disabled_is_empty();
	test_after_fixup();
	test_after_dma_regression();
	test_complete();
	test_invalid_rejected();
	test_prefix_invariant();
	test_entry_guard();

	if (failures) {
		printf("ob_d11_tail_state_test: %d/%d checks FAILED\n",
		       failures, checks);
		return 1;
	}
	printf("ob_d11_tail_state_test: %d checks PASS\n", checks);
	return 0;
}

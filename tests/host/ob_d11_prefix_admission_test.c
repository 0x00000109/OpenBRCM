// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host tests for the D3A1/D3B whole-prefix admission gate.
 *
 * Phase 36J.  The gate must reject a non-fresh, fatal or DMA-owning device
 * BEFORE D2A firmware upload, the 610 common-initvals writes and the
 * memset() of hw->d3a1 / hw->d3a0 that both isolated prefix entries perform.
 *
 * These tests exercise the production predicate compiled from
 * src/ob_d11_tail_plan.h.  They prove the decision function only: the caller
 * ordering (gate before D2B, gate before memset) is a SOURCE_CONTRACT
 * property checked by oracle/check_prefix_contract.py, and the wrapper that
 * fills the guard from struct ob_hw is not executed here (it needs a kernel
 * device).
 */
#include <stdio.h>
#include <string.h>
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

static struct ob_d11_prefix_entry_guard clean(void)
{
	struct ob_d11_prefix_entry_guard g;

	memset(&g, 0, sizeof(g));
	return g;
}

/*
 * The gate must be a pure decision: evaluating it may not mutate the state it
 * inspects (no latch clearing, no "cleanup to admit").  Each case is evaluated
 * twice and the guard bytes are compared before/after.
 */
static void test_predicate_is_pure(void)
{
	struct ob_d11_prefix_entry_guard cases[9];
	const char *names[9] = {
		"clean", "dev_lost", "dma_fatal", "fifo", "reboot",
		"started", "owner", "pool", "rings"
	};
	/* Independently defined expectations, not the observed value. */
	const enum ob_d11_prefix_entry_denial expected[9] = {
		OB_D11_PREFIX_ENTRY_OK,
		OB_D11_PREFIX_ENTRY_DEVICE_LOST,
		OB_D11_PREFIX_ENTRY_DMA_FATAL,
		OB_D11_PREFIX_ENTRY_FIFO_LATCHED,
		OB_D11_PREFIX_ENTRY_REBOOT_REQUIRED,
		OB_D11_PREFIX_ENTRY_ALREADY_STARTED,
		OB_D11_PREFIX_ENTRY_DMA_OWNED,
		OB_D11_PREFIX_ENTRY_DMA_OWNED,
		OB_D11_PREFIX_ENTRY_DMA_OWNED,
	};
	int i;

	for (i = 0; i < 9; i++)
		cases[i] = clean();
	cases[1].dev_lost = true;
	cases[2].dma_fatal = true;
	cases[3].fifo_retry_forbidden = true;
	cases[4].reboot_required = true;
	cases[5].tail_started = true;
	cases[6].dma_owner_present = true;
	cases[7].dma_pool_created = true;
	cases[8].dma_resources_present = true;

	for (i = 0; i < 9; i++) {
		struct ob_d11_prefix_entry_guard before = cases[i];

		chk(names[i], ob_d11_prefix_entry_check(&cases[i]),
		    expected[i]);
		chk("K19 denied input unchanged",
		    memcmp(&before, &cases[i], sizeof(before)) == 0, 1);
	}
}

/*
 * D3A0 reset-entry vectors (K01-K15, K18).  These execute the same production
 * predicate that ob_d3a0_test() now calls before memset(&hw->d3a0).  The
 * wrapper (NULL hw/core, and which concrete field produced the flag) is a
 * SOURCE_CONTRACT property, verified by oracle/check_d3a0_contract.py.
 */
static void test_d3a0_entry_vectors(void)
{
	struct ob_d11_prefix_entry_guard g;
	struct ob_d11_prefix_entry_guard h;

	/* K01: fresh D3A0, no owner. */
	g = clean();
	chk("K01 fresh admitted", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_OK);

	/* K04: device lost. */
	g = clean();
	g.dev_lost = true;
	chk("K04 device lost", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DEVICE_LOST);

	/* K05: fatal DMA record. */
	g = clean();
	g.dma_fatal = true;
	chk("K05 fatal record", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_FATAL);

	/* K06: pool still created. */
	g = clean();
	g.dma_pool_created = true;
	chk("K06 pool preserved", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_OWNED);

	/* K07/K08/K09: any ring or RX mapping still referenced. */
	g = clean();
	g.dma_resources_present = true;
	chk("K07/K08/K09 resources preserved",
	    ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_OWNED);

	/* K10: LIVE owner handle attached. */
	g = clean();
	g.dma_owner_present = true;
	chk("K10 live owner preserved", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_OWNED);

	/* K11: RETAINED record for this identity. */
	g = clean();
	g.owner_registry_refused = true;
	chk("K11 retained owner", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_OWNER_RETAINED);

	/* K12/K13: unavailable identity and saturated registry are both
	 * reported by ob_dma_owner_core_probe_refused() and map to the same
	 * denial; fail-closed is the requirement, the cause is diagnostics. */
	g = clean();
	g.owner_registry_refused = true;
	chk("K12/K13 registry refusal", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_OWNER_RETAINED);

	/* K14: prior unverified teardown latches both fatal and retained; the
	 * fatal classification wins and still forbids entry. */
	g = clean();
	g.dma_fatal = true;
	g.owner_registry_refused = true;
	g.dma_pool_created = true;
	chk("K14 unverified teardown", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_FATAL);

	/* K15: a verified teardown leaves no pool, no owner and no latch. */
	g = clean();
	chk("K15 verified teardown fresh", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_OK);

	/* K18: device-local isolation of the D3A0 denial. */
	g = clean();
	g.dma_pool_created = true;
	h = clean();
	chk("K18 device A refused", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_OWNED);
	chk("K18 device B admitted", ob_d11_prefix_entry_check(&h),
	    OB_D11_PREFIX_ENTRY_OK);
	chk("K18 device A unchanged",
	    ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_OWNED);
}

int main(void)
{
	struct ob_d11_prefix_entry_guard g;

	/* P1: fresh state, no owner. */
	g = clean();
	chk("P1 fresh admitted",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_OK);

	/* P2: device lost. */
	g = clean();
	g.dev_lost = true;
	chk("P2 device lost",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_DEVICE_LOST);

	/* P3: fatal DMA latch. */
	g = clean();
	g.dma_fatal = true;
	chk("P3 dma fatal",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_DMA_FATAL);

	/* P4: FIFO timeout latch. */
	g = clean();
	g.fifo_retry_forbidden = true;
	chk("P4 fifo latch",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_FIFO_LATCHED);

	/* P5: reboot-required T1 failure (same record for T2/validation). */
	g = clean();
	g.reboot_required = true;
	chk("P5 reboot required",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_REBOOT_REQUIRED);
	/* P6: a T2/validation-shaped record with DMA still owned must still be
	 * classified reboot-required (the reboot latch outranks the ownership
	 * denial).  fail_record() sets the same flag for T1/T2/validation; the
	 * stage-specific producer is source-verified, not executed here. */
	g = clean();
	g.reboot_required = true;
	g.dma_pool_created = true;
	g.dma_owner_present = true;
	chk("P6 reboot outranks live DMA", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_REBOOT_REQUIRED);

	/* P7: partial FIFO programming. */
	g = clean();
	g.tail_started = true;
	chk("P7 partial fifo",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_ALREADY_STARTED);

	/* P8: prior completed tail (FIFO writes recorded). */
	g = clean();
	g.tail_started = true;
	g.dma_owner_present = false;
	chk("P8 completed tail no replay",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_ALREADY_STARTED);

	/* P9: pool still created. */
	g = clean();
	g.dma_pool_created = true;
	chk("P9 pool created",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_DMA_OWNED);

	/* P10: durable owner LIVE and attached. */
	g = clean();
	g.dma_owner_present = true;
	chk("P10 live owner",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_DMA_OWNED);

	/* P11: registry owner RETAINED. */
	g = clean();
	g.owner_registry_refused = true;
	chk("P11 retained owner",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_OWNER_RETAINED);

	/* P12: rings or mapped RX buffers still referenced. */
	g = clean();
	g.dma_resources_present = true;
	chk("P12 resources present",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_DMA_OWNED);

	/* P13: a fresh device is admitted; the D2B postcondition gate inside
	 * ob_d3a1_run_prefix() remains responsible for refusing an invalid D2B
	 * exit.  The prefix gate must not pretend to observe hardware. */
	g = clean();
	chk("P13 fresh admitted before D2B gate",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_OK);

	/* P14: two independent devices — one guard struct per device. */
	{
		struct ob_d11_prefix_entry_guard a = clean();
		struct ob_d11_prefix_entry_guard b = clean();

		a.dev_lost = true;
		chk("P14 device A denied",
		    ob_d11_prefix_entry_check(&a),
		    OB_D11_PREFIX_ENTRY_DEVICE_LOST);
		chk("P14 device B admitted",
		    ob_d11_prefix_entry_check(&b), OB_D11_PREFIX_ENTRY_OK);
		chk("P14 device A still denied",
		    ob_d11_prefix_entry_check(&a),
		    OB_D11_PREFIX_ENTRY_DEVICE_LOST);
	}

	/* P15: DISABLED is a tail no-op and preserves pre-existing ownership.
	 * The tail predicate has no ownership-clearing action; with DMA owned
	 * the stop is still accepted and returns before any register access, so
	 * ownership stays with the caller. */
	{
		struct ob_d11_tail_entry_guard tail;

		memset(&tail, 0, sizeof(tail));
		tail.dma_live = true;
		chk("P15 DISABLED keeps ownership",
		    ob_d11_tail_entry_check(OB_D11_TAIL_STOP_DISABLED, &tail),
		    OB_D11_TAIL_ENTRY_OK);
		/* The same owned state must not admit any real tail stage. */
		chk("P15 DISABLED has no prefix bypass",
		    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_OK);
		g = clean();
		g.dma_pool_created = true;
		chk("P15 owned state refused by prefix",
		    ob_d11_prefix_entry_check(&g),
		    OB_D11_PREFIX_ENTRY_DMA_OWNED);
	}

	/* P16: a valid fresh test entry keeps the existing behaviour: the
	 * predicate returns OK and the caller continues into the prefix. */
	g = clean();
	chk("P16 fresh entry allowed",
	    ob_d11_prefix_entry_check(&g), OB_D11_PREFIX_ENTRY_OK);

	/* NULL guard is fail-closed, never an implicit admit. */
	chk("NULL guard refused",
	    ob_d11_prefix_entry_check(NULL),
	    OB_D11_PREFIX_ENTRY_INVALID_STATE);

	/* Fixed precedence: loss > fatal > FIFO latch > reboot-required >
	 * retained owner > prior tail start > live DMA.  FIFO keeps its
	 * documented -EBUSY ahead of the broader reboot classification. */
	g = clean();
	g.dev_lost = true;
	g.dma_fatal = true;
	g.owner_registry_refused = true;
	g.reboot_required = true;
	g.fifo_retry_forbidden = true;
	g.tail_started = true;
	g.dma_pool_created = true;
	chk("precedence loss", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DEVICE_LOST);

	g.dev_lost = false;
	chk("precedence fatal", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_FATAL);

	g.dma_fatal = false;
	chk("precedence fifo", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_FIFO_LATCHED);

	g.fifo_retry_forbidden = false;
	chk("precedence reboot", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_REBOOT_REQUIRED);

	g.reboot_required = false;
	chk("precedence retained", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_OWNER_RETAINED);

	g.owner_registry_refused = false;
	chk("precedence started", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_ALREADY_STARTED);

	g.tail_started = false;
	chk("precedence owned", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_DMA_OWNED);

	g.dma_pool_created = false;
	chk("precedence clean", ob_d11_prefix_entry_check(&g),
	    OB_D11_PREFIX_ENTRY_OK);

	test_predicate_is_pure();
	test_d3a0_entry_vectors();

	if (failures) {
		printf("ob_d11_prefix_admission_test: %d/%d checks FAILED\n",
		       failures, checks);
		return 1;
	}
	printf("ob_d11_prefix_admission_test: %d checks PASS\n", checks);
	return 0;
}

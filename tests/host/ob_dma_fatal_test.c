// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the per-device DMA fatal registry (Phase 34).
 *
 * These exercise the real src/ob_dma_fatal.c (not a model) outside the kernel
 * with the tests/host shim. They cover the admission invariants added in
 * Phase 34:
 *
 *   - a fatal record refuses its own device (and the same PCI slot) only;
 *   - an unrelated PCI function stays admitted (cross-device contamination is
 *     the negative case this file exists for);
 *   - repeated callbacks are idempotent and pin the module once;
 *   - a saturated registry fails closed for every identity;
 *   - concurrent latches for two devices never merge records.
 *
 * Passing this file is sequential/threaded host evidence only. It does not
 * prove hardware DMA-domain isolation, kernel locking or IOMMU behaviour.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_dma_fatal.h"
#include "../../src/ob_dma_owner.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

static void ok(const char *what, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

static struct ob_dma_owner_identity id_at(u8 bus, u8 devfn, u16 chip, u8 rev)
{
	struct ob_dma_owner_identity id = {
		.pci_domain = 0,
		.pci_bus = bus,
		.pci_devfn = devfn,
		.chip_id = chip,
		.chip_rev = rev,
		.d11_rev = 42,
		.pci_present = true,
	};

	return id;
}

static struct ob_dma_owner_identity id_a(void)
{
	return id_at(3, 0, 0x4352, 3);
}

static struct ob_dma_owner_identity id_b(void)
{
	return id_at(3, 8, 0x4352, 3);
}

static struct ob_dma_owner_identity id_slot_replaced(void)
{
	return id_at(3, 0, 0x4352, 4);	/* same PCI slot, different silicon */
}

static struct ob_dma_fatal_evidence ev(int marker)
{
	struct ob_dma_fatal_evidence e;

	memset(&e, 0, sizeof(e));
	e.rx_ring = 0x1000000u + (u32)marker;
	e.tx_ring[0] = 0x2000000u + (u32)marker;
	e.rx_mapped = 64;
	e.tx_channels = OB_DMA_FATAL_TX_CHANNELS;
	return e;
}

static void test_device_scope(void)
{
	struct ob_dma_owner_identity a = id_a(), b = id_b();
	struct ob_dma_fatal_evidence e = ev(1);

	ob_dma_fatal_reset_for_test();
	chk("latch A", ob_dma_fatal_latch(&a, &e, "test:a", NULL), 0);
	chk("count", ob_dma_fatal_count(), 1);
	chk("pins", ob_dma_fatal_pin_count(), 1);
	ok("A refused", ob_dma_fatal_refused(&a));
	ok("A latched", ob_dma_fatal_is_latched(&a));
	ok("B admitted", !ob_dma_fatal_refused(&b));
	ok("B not latched", !ob_dma_fatal_is_latched(&b));
	ok("module pinned", ob_dma_fatal_module_pinned());
}

static void test_slot_replacement(void)
{
	struct ob_dma_owner_identity a = id_a(), r = id_slot_replaced();
	struct ob_dma_fatal_evidence e = ev(2), e2 = ev(9);

	ob_dma_fatal_reset_for_test();
	chk("latch A", ob_dma_fatal_latch(&a, &e, "test:a", NULL), 0);
	ok("replacement refused", ob_dma_fatal_refused(&r));
	ok("replacement matches", ob_dma_fatal_is_latched(&r));
	/* A second latch for the replacement must not overwrite A's evidence. */
	chk("latch replacement", ob_dma_fatal_latch(&r, &e2, "test:r", NULL), 0);
	chk("still one record", ob_dma_fatal_count(), 1);
	{
		struct ob_dma_fatal_snapshot s[2];

		chk("snapshot", ob_dma_fatal_snapshot(s, 2), 1);
		chk("first evidence kept", (long)s[0].ev.rx_ring, 0x1000002L);
		chk("first where kept", strcmp(s[0].where, "test:a"), 0);
		chk("first id kept", s[0].id.chip_rev, 3);
	}
}

static void test_unrelated_bus_admitted(void)
{
	struct ob_dma_owner_identity a = id_a(), other = id_at(4, 0, 0x4352, 3);
	struct ob_dma_fatal_evidence e = ev(3), e2 = ev(4);

	ob_dma_fatal_reset_for_test();
	chk("latch A", ob_dma_fatal_latch(&a, &e, "test:a", NULL), 0);
	ok("other bus admitted", !ob_dma_fatal_refused(&other));
	chk("latch other", ob_dma_fatal_latch(&other, &e2, "test:o", NULL), 0);
	chk("two records", ob_dma_fatal_count(), 2);
	chk("one pin", ob_dma_fatal_pin_count(), 1);
}

static void test_repeated_callbacks(void)
{
	struct ob_dma_owner_identity a = id_a(), b = id_b();
	struct ob_dma_fatal_evidence e = ev(5);
	int i;

	ob_dma_fatal_reset_for_test();
	for (i = 0; i < 100; i++) {
		chk("repeated latch", ob_dma_fatal_latch(&a, &e, "test:a", NULL), 0);
		chk("repeated latch B", ob_dma_fatal_latch(&b, &e, "test:b", NULL), 0);
	}
	chk("one record each", ob_dma_fatal_count(), 2);
	chk("one pin", ob_dma_fatal_pin_count(), 1);
	{
		struct ob_dma_fatal_snapshot s[2];

		chk("snapshot", ob_dma_fatal_snapshot(s, 2), 2);
		chk("A calls", s[0].latch_calls, 100);
		chk("B calls", s[1].latch_calls, 100);
		ok("A evidence kept", s[0].ev.rx_ring == 0x1000005u);
		ok("B evidence kept", s[1].ev.rx_ring == 0x1000005u);
	}
}

static void test_invalid_identity(void)
{
	struct ob_dma_owner_identity bad = { 0 };
	struct ob_dma_fatal_evidence e = ev(1);

	ob_dma_fatal_reset_for_test();
	chk("latch invalid", ob_dma_fatal_latch(&bad, &e, "test:bad", NULL),
	    -EINVAL);
	ok("invalid refused", ob_dma_fatal_refused(&bad));
	ok("NULL refused", ob_dma_fatal_refused(NULL));
	chk("invalid not stored", ob_dma_fatal_count(), 0);
	chk("invalid no pin", ob_dma_fatal_pin_count(), 0);
}

static void test_saturation_fails_closed(void)
{
	struct ob_dma_owner_identity ids[OB_DMA_FATAL_CAP + 1];
	struct ob_dma_owner_identity extra = id_at(9, 0, 0x4352, 3);
	struct ob_dma_fatal_evidence e = ev(1), e2 = ev(99);
	unsigned int i;

	ob_dma_fatal_reset_for_test();
	for (i = 0; i < OB_DMA_FATAL_CAP; i++) {
		ids[i] = id_at(3, (u8)(i * 8), 0x4352, 3);
		chk("fill", ob_dma_fatal_latch(&ids[i], &e, "fill", NULL), 0);
	}
	chk("cap", ob_dma_fatal_count(), OB_DMA_FATAL_CAP);
	chk("one pin", ob_dma_fatal_pin_count(), 1);
	/* Full: an unrecorded identity cannot be proven unrelated. */
	ok("extra refused", ob_dma_fatal_refused(&extra));
	ok("extra not latched", !ob_dma_fatal_is_latched(&extra));
	chk("extra latch refused",
	    ob_dma_fatal_latch(&extra, &e2, "full", NULL), -ENOSPC);
	ok("known record still refused", ob_dma_fatal_refused(&ids[0]));
	chk("still cap", ob_dma_fatal_count(), OB_DMA_FATAL_CAP);
}

struct stress_arg {
	struct ob_dma_owner_identity id;
	int iterations;
	int errors;
};

static void *stress_latch(void *arg)
{
	struct stress_arg *s = arg;
	struct ob_dma_fatal_evidence e = ev(s->id.pci_devfn);
	int i;

	for (i = 0; i < s->iterations; i++)
		if (ob_dma_fatal_latch(&s->id, &e, "stress", NULL) != 0)
			s->errors++;
	return NULL;
}

static void test_concurrent_failures(void)
{
	struct stress_arg a = { id_a(), 500, 0 };
	struct stress_arg b = { id_b(), 500, 0 };
	pthread_t t1, t2;

	ob_dma_fatal_reset_for_test();
	pthread_create(&t1, NULL, stress_latch, &a);
	pthread_create(&t2, NULL, stress_latch, &b);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);

	ok("no concurrency errors", a.errors == 0 && b.errors == 0);
	chk("two records", ob_dma_fatal_count(), 2);
	chk("one pin", ob_dma_fatal_pin_count(), 1);
	ok("A refused", ob_dma_fatal_refused(&a.id));
	ok("B refused", ob_dma_fatal_refused(&b.id));
	{
		struct ob_dma_fatal_snapshot s[2];

		chk("snapshot", ob_dma_fatal_snapshot(s, 2), 2);
		chk("A calls", s[0].latch_calls, 500);
		chk("B calls", s[1].latch_calls, 500);
		ok("distinct identities", s[0].id.pci_devfn != s[1].id.pci_devfn);
	}
}

int main(void)
{
	test_device_scope();
	test_slot_replacement();
	test_unrelated_bus_admitted();
	test_repeated_callbacks();
	test_invalid_identity();
	test_saturation_fails_closed();
	test_concurrent_failures();

	if (failures) {
		printf("ob_dma_fatal_test: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ob_dma_fatal_test: PASS\n");
	return 0;
}

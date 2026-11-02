// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the durable DMA resource owner (Phase 33).
 *
 * These exercise the real src/ob_dma_owner.c (not a model) outside the kernel
 * with the tests/host shim. They cover the lifetime invariants I1..I12 and the
 * required negative cases: missing ownership reference, premature free,
 * wrong-device free and duplicate module pins.
 *
 * Passing this file is sequential/threaded host evidence only. It does not
 * prove kernel locking, DMA/IOMMU coherence or hardware quiescence.
 */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

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

static struct ob_dma_owner_identity id_a(void)
{
	struct ob_dma_owner_identity id = {
		.pci_domain = 0, .pci_bus = 3, .pci_devfn = 0,
		.chip_id = 0x4352, .chip_rev = 3, .d11_rev = 42,
		.pci_present = true,
	};

	return id;
}

static struct ob_dma_owner_identity id_b(void)
{
	struct ob_dma_owner_identity id = id_a();

	id.pci_devfn = 8;	/* same bus, different slot */
	return id;
}

static struct ob_dma_owner_identity id_slot_replaced(void)
{
	struct ob_dma_owner_identity id = id_a();

	id.chip_rev = 4;	/* same PCI slot, different silicon */
	return id;
}

static void test_clear_lifecycle(void)
{
	struct ob_dma_owner_identity id = id_a();
	struct ob_dma_owner *o = NULL;

	ob_dma_owner_registry_reset_for_test();
	chk("create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	ok("owner returned", o != NULL);
	chk("registry count", ob_dma_owner_registry_count(), 1);
	chk("state live", ob_dma_owner_state(o), OB_DMA_OWNER_LIVE);

	chk("add pool", ob_dma_owner_add(o, OB_DMA_OWNER_CLASS_POOL, 1), 0);
	chk("add desc", ob_dma_owner_add(o, OB_DMA_OWNER_CLASS_RING_DESC, 2), 0);
	chk("live desc", ob_dma_owner_live(o, OB_DMA_OWNER_CLASS_RING_DESC), 2);
	ok("has resources", ob_dma_owner_has_resources(o));

	/* Premature free: no quiescence evidence -> nothing may be accounted
	 * released, and the census must be unchanged.
	 */
	chk("clear unverified", ob_dma_owner_clear_resources(o, OB_DMA_OWNER_FAMILY_M32, false), -EPERM);
	chk("census survives", ob_dma_owner_live(o, OB_DMA_OWNER_CLASS_POOL), 1);
	chk("release unverified", ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_M32, false), -EPERM);
	chk("registry after refusal", ob_dma_owner_registry_count(), 1);

	/* Verified, but resources are still accounted: refuse. */
	chk("clear verified", ob_dma_owner_clear_resources(o, OB_DMA_OWNER_FAMILY_M32, true), 0);
	chk("census cleared", ob_dma_owner_live(o, OB_DMA_OWNER_CLASS_POOL), 0);
	chk("release", ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_M32, true), 0);
	chk("state released", ob_dma_owner_state(o), OB_DMA_OWNER_RELEASED);
	chk("registry empty", ob_dma_owner_registry_count(), 0);
	chk("double release refused", ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_M32, true), -EINVAL);
	ob_dma_owner_put(o);
}

static void test_release_with_resources_refused(void)
{
	struct ob_dma_owner_identity id = id_a();
	struct ob_dma_owner *o = NULL;

	ob_dma_owner_registry_reset_for_test();
	chk("create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	chk("add", ob_dma_owner_add(o, OB_DMA_OWNER_CLASS_RX_BUFFER, 64), 0);
	/* Skipping clear_resources must be refused: the last reference must not
	 * disappear while memory is still accounted live.
	 */
	chk("release with census", ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_M32, true), -EBUSY);
	chk("state live", ob_dma_owner_state(o), OB_DMA_OWNER_LIVE);
	chk("registry kept", ob_dma_owner_registry_count(), 1);
	chk("clear", ob_dma_owner_clear_resources(o, OB_DMA_OWNER_FAMILY_M32, true), 0);
	chk("release", ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_M32, true), 0);
	ob_dma_owner_put(o);
}

static void test_retained_is_immortal(void)
{
	struct ob_dma_owner_identity id = id_a();
	struct ob_dma_owner *o = NULL;
	const void *handles[2] = { (const void *)0x3000, (const void *)0x4000 };

	ob_dma_owner_registry_reset_for_test();
	chk("create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	chk("add", ob_dma_owner_add(o, OB_DMA_OWNER_CLASS_POOL, 1), 0);
	chk("adopt", ob_dma_owner_adopt(o, OB_DMA_OWNER_CLASS_POOL, handles, 2), 0);
	chk("retain", ob_dma_owner_retain(o,
					  OB_DMA_OWNER_REASON_QUIESCE_UNCONFIRMED,
					  "test", true), 0);
	ok("is retained", ob_dma_owner_is_retained(o));

	/* I9: no release while quiescence is unknown. */
	chk("release retained", ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_M32, true), -EBUSY);
	chk("clear retained", ob_dma_owner_clear_resources(o, OB_DMA_OWNER_FAMILY_M32, true), -EBUSY);
	chk("retained count", ob_dma_owner_registry_retained_count(), 1);

	/* I7/I8: repeated fatal callbacks are idempotent and never re-pin. */
	chk("retain again", ob_dma_owner_retain(o,
						OB_DMA_OWNER_REASON_DEVICE_LOST,
						"test2", true), 0);
	chk("retain third", ob_dma_owner_retain(o,
						OB_DMA_OWNER_REASON_FATAL_TEARDOWN,
						"test3", true), 0);
	chk("pin once", (long)ob_dma_owner_pin_count(), 1);
	{
		struct ob_dma_owner_snapshot s[2];

		chk("snapshot entries",
		    ob_dma_owner_registry_snapshot(s, 2), 1);
		chk("retain calls", s[0].retain_calls, 3);
		chk("first reason kept", s[0].reason,
		    OB_DMA_OWNER_REASON_QUIESCE_UNCONFIRMED);
		chk("adopted handles", s[0].handles[OB_DMA_OWNER_CLASS_POOL], 2);
		ok("module pinned recorded", s[0].module_pinned);
	}

	/* Missing ownership reference: adopting the same class twice would mean
	 * two accountable owners for one allocation.
	 */
	chk("duplicate adopt",
	    ob_dma_owner_adopt(o, OB_DMA_OWNER_CLASS_POOL, handles, 2), -EEXIST);

	/* Dropping the caller reference must NOT free a retained owner: the
	 * registry reference keeps it reachable (this is the Phase 32 gap).
	 */
	ob_dma_owner_put(o);
	chk("retained survives put", ob_dma_owner_registry_retained_count(), 1);
}

static void test_wrong_device_release(void)
{
	struct ob_dma_owner_identity a = id_a();
	struct ob_dma_owner_identity b = id_b();
	struct ob_dma_owner *o = NULL;

	ob_dma_owner_registry_reset_for_test();
	chk("create A", ob_dma_owner_create(&a, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	chk("clear", ob_dma_owner_clear_resources(o, OB_DMA_OWNER_FAMILY_M32, true), 0);
	/* I2: device B must not release device A's owner. */
	chk("wrong device release",
	    ob_dma_owner_release(o, &b, OB_DMA_OWNER_FAMILY_M32, true), -EPERM);
	chk("state still live", ob_dma_owner_state(o), OB_DMA_OWNER_LIVE);
	chk("correct device release", ob_dma_owner_release(o, &a, OB_DMA_OWNER_FAMILY_M32, true), 0);
	ob_dma_owner_put(o);
}

static void test_retained_refuses_reprobe(void)
{
	struct ob_dma_owner_identity a = id_a();
	struct ob_dma_owner_identity replaced = id_slot_replaced();
	struct ob_dma_owner *o = NULL, *o2 = NULL;

	ob_dma_owner_registry_reset_for_test();
	chk("create", ob_dma_owner_create(&a, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	chk("retain", ob_dma_owner_retain(o,
					  OB_DMA_OWNER_REASON_REMOVE_RETAINED,
					  "remove", true), 0);
	ok("probe refused same identity", ob_dma_owner_probe_refused(&a));
	ok("probe refused replacement in same slot",
	   ob_dma_owner_probe_refused(&replaced));
	chk("create refused", ob_dma_owner_create(&a, OB_DMA_OWNER_FAMILY_M32, &o2), -EBUSY);
	ok("no second owner", o2 == NULL);
	chk("still one retained", ob_dma_owner_registry_retained_count(), 1);
	ob_dma_owner_put(o);
}

static void test_unkeyed_refused(void)
{
	struct ob_dma_owner_identity id = id_a();
	struct ob_dma_owner *o = NULL;

	ob_dma_owner_registry_reset_for_test();
	id.pci_present = false;
	chk("unkeyed create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &o), -ENODEV);
	ok("unkeyed probe refused", ob_dma_owner_probe_refused(&id));
	id = id_a();
	id.d11_rev = 0;
	ok("missing d11 refused", ob_dma_owner_probe_refused(&id));
}

static void test_registry_exhaustion(void)
{
	struct ob_dma_owner *owners[OB_DMA_OWNER_REGISTRY_CAP];
	struct ob_dma_owner_identity id;
	struct ob_dma_owner *extra = NULL;
	unsigned int i;

	ob_dma_owner_registry_reset_for_test();
	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++) {
		id = id_a();
		id.pci_devfn = (u8)(i * 8);
		owners[i] = NULL;
		chk("create until full", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &owners[i]), 0);
	}
	chk("registry full", ob_dma_owner_registry_count(),
	    OB_DMA_OWNER_REGISTRY_CAP);
	id = id_a();
	id.pci_devfn = 0xf8;
	chk("saturated create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &extra), -ENOSPC);
	ok("registry poisoned", ob_dma_owner_registry_poisoned());
	ok("probe refused when poisoned", ob_dma_owner_probe_refused(&id));
	chk("second saturated create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &extra), -EBUSY);

	for (i = 0; i < OB_DMA_OWNER_REGISTRY_CAP; i++) {
		chk("clear", ob_dma_owner_clear_resources(owners[i], OB_DMA_OWNER_FAMILY_M32, true), 0);
		chk("release", ob_dma_owner_release(owners[i], NULL, OB_DMA_OWNER_FAMILY_M32, true), 0);
		ob_dma_owner_put(owners[i]);
	}
	ok("poison is monotonic", ob_dma_owner_registry_poisoned());
}

static void test_suppressed_pin(void)
{
	struct ob_dma_owner_identity id = id_a();
	struct ob_dma_owner *o = NULL;

	ob_dma_owner_registry_reset_for_test();
	chk("create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	/* The caller reports that another subsystem already pinned the module
	 * for the same retention event.
	 */
	chk("retain without pin",
	    ob_dma_owner_retain(o, OB_DMA_OWNER_REASON_DEVICE_LOST,
				"d3a0-latched", false), 0);
	chk("no extra pin", (long)ob_dma_owner_pin_count(), 0);
}

/*
 * F3 negative regression: an owner belongs to exactly one allocation family.
 * A D3A0 teardown (clear + release) on an M3.2 owner must be refused and must
 * leave the M3.2 census, state and registry slot untouched.
 */
static void test_family_isolation(void)
{
	struct ob_dma_owner_identity id = id_a();
	struct ob_dma_owner *o = NULL;
	struct ob_dma_owner_snapshot s[2];

	ob_dma_owner_registry_reset_for_test();
	chk("create with no family",
	    ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_NONE, &o), -EINVAL);
	chk("create m32",
	    ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	chk("owner family", ob_dma_owner_family(o), OB_DMA_OWNER_FAMILY_M32);
	chk("add pool", ob_dma_owner_add(o, OB_DMA_OWNER_CLASS_POOL, 1), 0);
	chk("add desc", ob_dma_owner_add(o, OB_DMA_OWNER_CLASS_RING_DESC, 2), 0);
	chk("add slot", ob_dma_owner_add(o, OB_DMA_OWNER_CLASS_RING_SLOT, 2), 0);

	/* A D3A0 teardown must not erase another family's accounting. */
	chk("d3a0 clear refused",
	    ob_dma_owner_clear_resources(o, OB_DMA_OWNER_FAMILY_D3A0, true),
	    -EPERM);
	chk("d3a0 release refused",
	    ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_D3A0, true), -EPERM);
	chk("m32 census kept",
	    ob_dma_owner_live(o, OB_DMA_OWNER_CLASS_RING_DESC), 2);
	chk("m32 owner still live", ob_dma_owner_state(o), OB_DMA_OWNER_LIVE);
	chk("registry kept", ob_dma_owner_registry_count(), 1);
	chk("snapshot entries", ob_dma_owner_registry_snapshot(s, 2), 1);
	chk("snapshot family", s[0].family, OB_DMA_OWNER_FAMILY_M32);

	/* The owning family still finishes normally. */
	chk("m32 clear",
	    ob_dma_owner_clear_resources(o, OB_DMA_OWNER_FAMILY_M32, true), 0);
	chk("m32 release",
	    ob_dma_owner_release(o, &id, OB_DMA_OWNER_FAMILY_M32, true), 0);
	chk("registry empty", ob_dma_owner_registry_count(), 0);
	ob_dma_owner_put(o);
}

/* ---- concurrency ------------------------------------------------------- */

struct stress_arg {
	struct ob_dma_owner *owner;
	int iterations;
	int errors;
};

static void *stress_retain(void *arg)
{
	struct stress_arg *a = arg;
	int i;

	for (i = 0; i < a->iterations; i++) {
		if (ob_dma_owner_retain(a->owner,
					OB_DMA_OWNER_REASON_FATAL_TEARDOWN,
					"stress", true))
			a->errors++;
		if (ob_dma_owner_is_retained(a->owner) != true)
			a->errors++;
	}
	return NULL;
}

static void *stress_release(void *arg)
{
	struct stress_arg *a = arg;
	int i;

	for (i = 0; i < a->iterations; i++) {
		/* Every attempt must be refused: the owner is retained. */
		if (ob_dma_owner_release(a->owner, NULL, OB_DMA_OWNER_FAMILY_M32, true) != -EBUSY)
			a->errors++;
	}
	return NULL;
}

static void test_concurrent_retain(void)
{
	struct ob_dma_owner_identity id = id_a();
	struct ob_dma_owner *o = NULL;
	struct stress_arg r = { NULL, 500, 0 }, rel = { NULL, 500, 0 };
	pthread_t t1, t2, t3, t4;

	ob_dma_owner_registry_reset_for_test();
	chk("create", ob_dma_owner_create(&id, OB_DMA_OWNER_FAMILY_M32, &o), 0);
	r.owner = o;
	rel.owner = o;
	pthread_create(&t1, NULL, stress_retain, &r);
	pthread_create(&t2, NULL, stress_retain, &r);
	pthread_create(&t3, NULL, stress_release, &rel);
	pthread_create(&t4, NULL, stress_release, &rel);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	pthread_join(t3, NULL);
	pthread_join(t4, NULL);

	ok("no concurrency errors", r.errors == 0 && rel.errors == 0);
	chk("pin still once", (long)ob_dma_owner_pin_count(), 1);
	ok("retained", ob_dma_owner_is_retained(o));
	{
		struct ob_dma_owner_snapshot s[2];

		/* 2 threads x 500 iterations, all idempotent. */
		chk("snapshot", ob_dma_owner_registry_snapshot(s, 2), 1);
		chk("retain calls recorded", s[0].retain_calls, 1000);
	}
}

int main(void)
{
	test_clear_lifecycle();
	test_release_with_resources_refused();
	test_retained_is_immortal();
	test_wrong_device_release();
	test_retained_refuses_reprobe();
	test_unkeyed_refused();
	test_registry_exhaustion();
	test_suppressed_pin();
	test_family_isolation();
	test_concurrent_retain();

	if (failures) {
		printf("ob_dma_owner_test: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ob_dma_owner_test: PASS\n");
	return 0;
}

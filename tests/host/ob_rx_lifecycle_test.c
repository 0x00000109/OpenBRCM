/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include "../../src/ob_rx_lifecycle.h"

static int failures;
#define CHECK(label, expr) do { \
	if (!(expr)) { printf("FAIL: %s\n", label); failures++; } \
} while (0)

int main(void)
{
	struct ob_rx_dma_ownership s = { 0 };

	CHECK("RX activation stays disabled without durable quarantine ownership",
	      !ob_rx_dma_activation_supported());
	CHECK("never initialized may free", ob_rx_dma_may_free(&s));
	CHECK("never initialized may initialize", ob_rx_dma_may_initialize(&s));
	ob_rx_dma_mark_enabled(&s);
	CHECK("enabled may own", s.may_own && !ob_rx_dma_may_free(&s));
	ob_rx_dma_quarantine(&s);
	CHECK("timeout quarantines", s.may_own && s.quarantined &&
	      !ob_rx_dma_may_free(&s));
	ob_rx_dma_mark_enabled(&s);
	CHECK("a later enable marker cannot clear quarantine",
	      s.may_own && s.quarantined && !ob_rx_dma_may_free(&s));
	/* Repeated cleanup checks are idempotent and cannot clear quarantine. */
	CHECK("repeated cleanup remains blocked", !ob_rx_dma_may_free(&s));
	CHECK("quarantined memory forbids reinit", !ob_rx_dma_may_initialize(&s));
	ob_rx_dma_mark_disabled_confirmed(&s);
	CHECK("confirmed disable permits free", !s.may_own && !s.quarantined &&
	      ob_rx_dma_may_free(&s));
	ob_rx_dma_mark_enabled(&s);
	CHECK("second enable restores ownership", !ob_rx_dma_may_free(&s));
	ob_rx_dma_mark_disabled_confirmed(&s);
	CHECK("confirmed second disable permits free", ob_rx_dma_may_free(&s));
	CHECK("confirmed second disable permits init",
	      ob_rx_dma_may_initialize(&s));
	ob_rx_dma_mark_removing(&s);
	CHECK("remove is sticky against reinit",
	      !ob_rx_dma_may_initialize(&s));
	CHECK("remove does not imply DMA ownership", ob_rx_dma_may_free(&s));

	if (failures) {
		printf("ob_rx_lifecycle: %d failures\n", failures);
		return 1;
	}
	puts("ob_rx_lifecycle: PASS");
	return 0;
}

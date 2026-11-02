/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>

#include "../../src/ob_d11_tx_frameid.h"

static unsigned int failures;

static void check(const char *name, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %d expected %d\n", name, got, expected);
		failures++;
	}
}

int main(void)
{
	struct ob_d11_tx_frameid_result result;
	int ret;

	/* Literal outputs calculated from the two instruction-derived masks. */
	ret = ob_d11_tx_frameid_acphy(0x0b, 0x800b, 0x1234, &result);
	check("normal selector result", ret, 0);
	check("normal frame-id golden", result.frame_id, 0xe80b);
	check("normal counter kind", result.counter_kind,
	      OB_D11_TX_FRAMEID_NORMAL_COUNTER);
	check("normal counter increments", result.next_counter, 0x1235);

	ret = ob_d11_tx_frameid_acphy(0x0b, 0x801c, 0x03ff, &result);
	check("selector-4 result", ret, 0);
	check("selector-4 frame-id golden", result.frame_id, 0xfffc);
	check("selector-4 counter kind", result.counter_kind,
	      OB_D11_TX_FRAMEID_SELECTOR4_COUNTER);
	check("selector-4 counter increments", result.next_counter, 0x0400);

	ret = ob_d11_tx_frameid_acphy(0x0a, 0x801c, 0x03ff, &result);
	check("unsupported PHY rejected", ret, -EOPNOTSUPP);
	check("unsupported result cleared", result.frame_id, 0);
	check("null output rejected",
	      ob_d11_tx_frameid_acphy(0x0b, 0, 0, NULL), -EINVAL);
	if (failures) {
		printf("ob_d11_tx_frameid tests: %u failures\n", failures);
		return 1;
	}
	puts("ob_d11_tx_frameid tests: PASS (instruction-derived; synthetic inputs)");
	return 0;
}

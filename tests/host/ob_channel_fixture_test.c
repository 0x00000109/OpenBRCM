/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include "../../src/ob_channel.h"

static int failures;

static void chk(const char *what, int got, int exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %d expected %d\n", what, got, exp);
		failures++;
	}
}

int main(void)
{
	u16 encoded = 0;
	u8 needed = 0;

	chk("fixture identity", ob_test_chanspec_fixture(0x4352, 3, 42,
		0x0b, 1, 0x2069, 4, &encoded), 0);
	chk("fixture value", encoded, OB_TEST_CHANSPEC_BCM4352_CH1_20MHZ);
	chk("wrong identity rejected", ob_test_chanspec_fixture(0x4352, 2, 42,
		0x0b, 1, 0x2069, 4, &encoded), -22);
	chk("invalid current", ob_acphy_bw_transition_needed(0x1001, false,
		0x1000, true, &needed), -22);
	chk("invalid cache", ob_acphy_bw_transition_needed(0x1001, true,
		0x1000, false, &needed), -22);
	chk("equal mask", ob_acphy_bw_transition_needed(0x1001, true,
		0x1000, true, &needed), 0);
	chk("equal mask result", needed, 0);
	chk("unrelated bits ignored", ob_acphy_bw_transition_needed(0x1003,
		true, 0x1000, true, &needed), 0);
	chk("unrelated bits result", needed, 0);
	chk("different mask", ob_acphy_bw_transition_needed(0x1801, true,
		0x1000, true, &needed), 0);
	chk("different mask result", needed, 1);

	if (failures) {
		printf("openbrcm channel fixture tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm channel fixture tests: PASS\n");
	return 0;
}

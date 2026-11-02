/* SPDX-License-Identifier: GPL-2.0-only */
/* Host tests for mac80211's accepted receive-filter flag contract. */
#include <stdio.h>

#include "../../src/ob_mac80211.h"

static unsigned int failures;

static void check(const char *name, unsigned int got, unsigned int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %#x expected %#x\n", name, got, expected);
		failures++;
	}
}

int main(void)
{
	const unsigned int all_flags = 0xffffffffu;
	const unsigned int partial = 0x00000015u;

	/* Production currently passes supported=0 because RX is absent. */
	check("no RX means no accepted filter flags",
	      ob_mac80211_filter_flags_supported(all_flags, 0), 0);
	/* Generic mask semantics remain correct if support is added later. */
	check("supported subset retained",
	      ob_mac80211_filter_flags_supported(all_flags, 0x0000000au),
	      0x0000000au);
	check("unrequested supported flags stay clear",
	      ob_mac80211_filter_flags_supported(partial, 0x0000000cu), 0x4u);
	check("empty request stays empty",
	      ob_mac80211_filter_flags_supported(0, all_flags), 0);

	if (failures) {
		printf("openbrcm mac80211 lifecycle tests: %u FAILURES\n", failures);
		return 1;
	}
	puts("openbrcm mac80211 lifecycle tests: PASS");
	return 0;
}

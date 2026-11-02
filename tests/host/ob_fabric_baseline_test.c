// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define u32 uint32_t
#include "../../src/ob_fabric_baseline.h"

static void expect_class(const char *name,
			 const struct ob_fixed_window_snapshot *snapshot,
			 enum ob_fixed_window_baseline expected)
{
	if (ob_fixed_window_classify(snapshot) != expected) {
		fprintf(stderr, "%s: unexpected fixed-window classification\n", name);
		exit(EXIT_FAILURE);
	}
}

int main(void)
{
	const struct ob_fixed_window_snapshot readable = {
		.cc_cap = 0x58680001,
		.cc_otps = 0x00009400,
		.cc_otpl = 0x00115200,
		.cc_srom_ctl = 0x00000023,
		.cc_clkctlst = 0x01030040,
		.pmu_cap = 0x10a22b11,
		.pmu_ctl = 0x01770381,
	};
	const struct ob_fixed_window_snapshot all_ones = {
		.cc_cap = 0xffffffff,
		.cc_otps = 0xffffffff,
		.cc_otpl = 0xffffffff,
		.cc_srom_ctl = 0xffffffff,
		.cc_clkctlst = 0xffffffff,
		.pmu_cap = 0xffffffff,
		.pmu_ctl = 0xffffffff,
	};
	struct ob_fixed_window_snapshot mixed = readable;

	expect_class("readable snapshot", &readable, OB_FIXED_WINDOW_READABLE);
	expect_class("all-ones snapshot", &all_ones, OB_FIXED_WINDOW_ALL_ONES);
	mixed.pmu_ctl = 0xffffffff;
	expect_class("mixed snapshot", &mixed,
			 OB_FIXED_WINDOW_INDETERMINATE);
	expect_class("null snapshot", NULL, OB_FIXED_WINDOW_INDETERMINATE);
	return 0;
}

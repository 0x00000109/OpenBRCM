// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define u16 uint16_t
#include "../../src/ob_sprom_policy.h"

static void check(const char *name, bool got, bool expected)
{
	if (got != expected) {
		fprintf(stderr, "%s: got %d expected %d\n", name, got, expected);
		exit(EXIT_FAILURE);
	}
}

int main(void)
{
	check("bounded radio path skips direct reread",
	      ob_si_sprom_read_required(true), false);
	check("normal path skips direct validation",
	      ob_si_sprom_read_required(false), false);
	check("valid first word admitted",
	      ob_si_sprom_first_word_accessible(0x3801u), true);
	check("all-ones first word fails closed",
	      ob_si_sprom_first_word_accessible(0xffffu), false);
	return 0;
}

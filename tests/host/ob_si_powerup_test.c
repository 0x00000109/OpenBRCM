// SPDX-License-Identifier: GPL-2.0-only
/* Host contract tests for the BCMA enable postcondition gate. */
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>

#include "../../src/ob_si_postcondition.h"

static int failures;

static void check_int(const char *name, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %d expected %d\n", name, got, expected);
		failures++;
	}
}

int main(void)
{
	check_int("already enabled permits FAST",
		ob_si_core_enable_postcondition(0, true), 0);
	check_int("BCMA API error propagates unchanged",
		ob_si_core_enable_postcondition(-EAGAIN, false), -EAGAIN);
	check_int("successful enable permits FAST",
		ob_si_core_enable_postcondition(0, true), 0);
	check_int("false enabled postcondition fails closed",
		ob_si_core_enable_postcondition(0, false), -EIO);
	check_int("invalid postcondition fails closed",
		ob_si_core_enable_postcondition(0, false), -EIO);
	check_int("attach reset accepts valid wrapper postcondition",
		ob_attach_core_reset_postcondition(0, true, true), 0);
	check_int("attach reset propagates BCMA error",
		ob_attach_core_reset_postcondition(-EAGAIN, false, false), -EAGAIN);
	check_int("attach reset rejects invalid wrapper observation",
		ob_attach_core_reset_postcondition(0, false, true), -EIO);
	check_int("attach reset rejects false enabled predicate",
		ob_attach_core_reset_postcondition(0, true, false), -EIO);
	check_int("SI context admits matching per-device profile",
		ob_si_core_context_precondition(true, true, true, true, true,
						true, false), 0);
	check_int("SI context rejects missing device before access",
		ob_si_core_context_precondition(false, true, true, true, true,
						true, false), -EINVAL);
	check_int("SI context rejects missing profile before access",
		ob_si_core_context_precondition(true, true, true, false, true,
						false, false), -EINVAL);
	check_int("SI context rejects detached BCMA core",
		ob_si_core_context_precondition(true, true, true, true, false,
						true, false), -ENODEV);
	check_int("SI context rejects contradictory D11/profile identity",
		ob_si_core_context_precondition(true, true, true, true, true,
						false, false), -ENODEV);
	check_int("SI context stops after device loss",
		ob_si_core_context_precondition(true, true, true, true, true,
						true, true), -EIO);
	if (failures)
		return 1;
	puts("ob_si_powerup_test: PASS");
	return 0;
}

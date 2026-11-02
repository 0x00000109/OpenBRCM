/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>

#include "../../src/ob_ucode.h"

static int expect(enum ob_ucode_core_postcondition actual,
			  enum ob_ucode_core_postcondition expected)
{
	return actual == expected ? 0 : 1;
}

int main(void)
{
	if (expect(ob_ucode_core_postcondition_from_regs(0, 0),
		   OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID))
		return 1;
	if (expect(ob_ucode_core_postcondition_from_regs(BCMA_IOCTL_CLK,
							BCMA_RESET_CTL_RESET),
		   OB_UCODE_CORE_POSTCONDITION_RESET_ASSERTED))
		return 1;
	if (expect(ob_ucode_core_postcondition_from_regs(0,
							BCMA_RESET_CTL_RESET),
		   OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID_RESET_ASSERTED))
		return 1;
	if (expect(ob_ucode_core_postcondition_from_regs(BCMA_IOCTL_CLK, 0),
		   OB_UCODE_CORE_POSTCONDITION_ENABLED))
		return 1;
	if (!ob_ucode_core_enabled_predicate(BCMA_IOCTL_CLK, 0))
		return 1;
	if (ob_ucode_core_enabled_predicate(0, BCMA_RESET_CTL_RESET))
		return 1;
	puts("ob_ucode_core_state_test: PASS");
	return 0;
}

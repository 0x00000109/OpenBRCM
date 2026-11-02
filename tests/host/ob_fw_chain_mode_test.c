// SPDX-License-Identifier: GPL-2.0-only
/* Host tests for the W2 fw_chain_test_only mode plumbing. */
#include <assert.h>
#include <stdio.h>

#include "../../src/ob_ucode.h"

static enum ob_isolated_mode select_chain(bool chain)
{
	return ob_isolated_mode_select26(false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 chain);
}

int main(void)
{
	enum ob_isolated_mode mode;

	assert(select_chain(true) == OB_ISOLATED_FW_CHAIN_TEST);
	assert(select_chain(false) == OB_ISOLATED_NONE);
	assert((unsigned int)OB_ISOLATED_FW_CHAIN_TEST >
	       (unsigned int)OB_ISOLATED_ACPHY_IDENTITY_TIMING);

	/* The new mode conflicts with each existing mode family. */
	assert(ob_isolated_mode_select26(true, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 true) == OB_ISOLATED_CONFLICT);
	assert(ob_isolated_mode_select26(false, true, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 true) == OB_ISOLATED_CONFLICT);
	assert(ob_isolated_mode_select26(false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, true,
					 true) == OB_ISOLATED_CONFLICT);

	/* select25 is unchanged for an existing caller. */
	assert(ob_isolated_mode_select25(false, true, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false,
					 false) == OB_ISOLATED_UCODE_TEST);

	mode = OB_ISOLATED_FW_CHAIN_TEST;
	assert(!ob_isolated_mode_conflict(mode));
	assert(ob_isolated_mode_applies_initvals(mode));
	assert(ob_isolated_mode_skips_teardown(mode));
	assert(!ob_isolated_mode_uses_dma(mode));
	assert(!ob_isolated_mode_preserves_state(mode));

	/* The previous enumerators keep their values; the new value is appended. */
	assert((unsigned int)OB_ISOLATED_NONE == 0u);
	assert((unsigned int)OB_ISOLATED_FW_VALIDATE == 1u);
	assert((unsigned int)OB_ISOLATED_UCODE_TEST == 2u);
	assert((unsigned int)OB_ISOLATED_INITVALS_TEST == 3u);
	assert((unsigned int)OB_ISOLATED_DMA_TEST == 4u);
	assert((unsigned int)OB_ISOLATED_D3A1_TEST == 5u);
	assert((unsigned int)OB_ISOLATED_D3B_TEST == 6u);
	assert(ob_isolated_default_load_refused(OB_ISOLATED_NONE, false,
						false));
	assert(!ob_isolated_default_load_refused(mode, false, false));

	printf("ob_fw_chain_mode_test: PASS\n");
	return 0;
}

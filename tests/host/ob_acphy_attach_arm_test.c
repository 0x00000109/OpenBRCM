// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test for the vendor core-clock-word arm of wlc_bmac_phy_reset.
 *
 * Recovered from the pinned object (SHA-256 352a6e34...4743):
 *
 *   wlc_bmac_phy_reset 0x65da6  cmp $0xb,%ax      ; phy_type == AC-PHY
 *                      0x65daa  jne 0x65dc0       ; mutually exclusive arms
 *                      0x65db6  AC mask 0x0ce
 *                      0x65dbb  AC value bw | 0x0e
 *                      0x65dca  other mask 0x0cc
 *                      0x65dcf  other value bw | 0x0c
 *                      0x65dd3  single shared si_core_cflags call
 *
 *   wlc_phy_clk_bwbits 0xb1913: gate {4,6,7,8,0xa,0xb};
 *                      pi+0x182 0x1800 -> 0x80, 0x2000 -> 0xc0,
 *                                0x1000 -> 0x40, else 0.
 *
 * These cases discriminate the corrected behaviour from the previous source,
 * which wrote the constant 0x0e (and then 0x0c) for every PHY type.
 */
#include <stdio.h>
#include "ob_radio.h"

static int failures;

static void check(const char *name, unsigned long got, unsigned long expected)
{
	if (got != expected) {
		printf("FAIL %s: got 0x%lx expected 0x%lx\n", name, got, expected);
		failures++;
	}
}

static void check_bwbits(void)
{
	check("bw ac 0x1000", ob_acphy_clk_bwbits(0x0b, 0x1000), 0x40);
	check("bw ac 0x1800", ob_acphy_clk_bwbits(0x0b, 0x1800), 0x80);
	check("bw ac 0x2000", ob_acphy_clk_bwbits(0x0b, 0x2000), 0xc0);
	check("bw ac other", ob_acphy_clk_bwbits(0x0b, 0x0000), 0x00);
	check("bw ac 0x3800", ob_acphy_clk_bwbits(0x0b, 0x3800), 0x00);
	/* every member of the type gate */
	check("bw 4", ob_acphy_clk_bwbits(0x04, 0x1000), 0x40);
	check("bw 6", ob_acphy_clk_bwbits(0x06, 0x1000), 0x40);
	check("bw 7", ob_acphy_clk_bwbits(0x07, 0x1800), 0x80);
	check("bw 8", ob_acphy_clk_bwbits(0x08, 0x2000), 0xc0);
	check("bw 0xa", ob_acphy_clk_bwbits(0x0a, 0x2000), 0xc0);
	/* types outside the gate never yield bandwidth bits */
	check("bw type 1", ob_acphy_clk_bwbits(0x01, 0x1000), 0x00);
	check("bw type 0", ob_acphy_clk_bwbits(0x00, 0x1000), 0x00);
	check("bw type 9", ob_acphy_clk_bwbits(0x09, 0x1800), 0x00);
	check("bw type 0xc", ob_acphy_clk_bwbits(0x0c, 0x2000), 0x00);
}

static void check_arm(void)
{
	u16 mask = 0xffff, value = 0xffff;

	/* The target: BCM4352 AC-PHY at the attach-time bandwidth state. */
	ob_acphy_reset_arm(0x0b, OB_ACPHY_BW_STATE_ATTACH, &mask, &value);
	check("ac mask", mask, 0xce);
	check("ac value", value, 0x4e);
	check("ac value is not the old constant", value == 0x0e, 0);

	ob_acphy_reset_arm(0x0b, 0x1800, &mask, &value);
	check("ac 40mhz mask", mask, 0xce);
	check("ac 40mhz value", value, 0x8e);

	ob_acphy_reset_arm(0x0b, 0x2000, &mask, &value);
	check("ac 80mhz mask", mask, 0xce);
	check("ac 80mhz value", value, 0xce);

	/* bw is computed for every type, so a gated type keeps its bits. */
	ob_acphy_reset_arm(0x06, 0x1000, &mask, &value);
	check("type 6 mask", mask, 0xcc);
	check("type 6 value", value, 0x4c);

	/* Types outside the bandwidth gate contribute no bandwidth bits. */
	ob_acphy_reset_arm(0x01, 0x1000, &mask, &value);
	check("type 1 mask", mask, 0xcc);
	check("type 1 value", value, 0x0c);

	/* An AC arm can never produce the non-AC mask/value pair. */
	for (unsigned int bw = 0; bw <= 0x2000u; bw += 0x200u) {
		ob_acphy_reset_arm(0x0b, (u16)bw, &mask, &value);
		check("ac mask invariant", mask, 0xce);
		if ((value & 0x00ceu) != value) {
			printf("FAIL ac value escape bw=0x%x value=0x%x\n", bw,
			       value);
			failures++;
		}
	}
}

int main(void)
{
	check_bwbits();
	check_arm();
	if (failures)
		return 1;
	puts("ob_acphy_attach_arm_test: PASS");
	return 0;
}

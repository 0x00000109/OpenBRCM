// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the D11 interrupt decision helpers (M3.3).
 *
 * No request_irq here; these only exercise the pure masking/decision logic in
 * src/ob_irq.h.
 */
#include <stdio.h>
#include "../../src/ob_irq.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

int main(void)
{
	u32 unknown_bit = 1u << 24;	/* not a named MI_* bit */

	/* owned bits must be a subset of known bits */
	chk("owned subset of known",
	    (long)(OB_D11_IRQ_OWNED_MASK & ~OB_D11_IRQ_KNOWN_MASK), 0);
	chk("owned non-zero", (long)(OB_D11_IRQ_OWNED_MASK != 0), 1);
	chk("known excludes bit24", (long)(OB_D11_IRQ_KNOWN_MASK & unknown_bit), 0);

	/* status validity: 0xffffffff is not a real status */
	chk("invalid ffffffff", (long)ob_d11_irq_status_valid(0xffffffffu), 0);
	chk("valid zero", (long)ob_d11_irq_status_valid(0), 1);
	chk("valid dmain", (long)ob_d11_irq_status_valid(OB_D11_MI_DMAINT), 1);

	/* pending == raw & owned */
	chk("pending dmain",
	    (long)ob_d11_irq_pending(OB_D11_MI_DMAINT, OB_D11_IRQ_OWNED_MASK),
	    (long)OB_D11_MI_DMAINT);
	chk("pending unowned",
	    (long)ob_d11_irq_pending(OB_D11_MI_TBTT, OB_D11_IRQ_OWNED_MASK), 0);

	/* has_work: valid AND owned bit pending */
	chk("work dmain",
	    (long)ob_d11_irq_has_work(OB_D11_MI_DMAINT, OB_D11_IRQ_OWNED_MASK), 1);
	chk("no work unowned",
	    (long)ob_d11_irq_has_work(OB_D11_MI_TBTT, OB_D11_IRQ_OWNED_MASK), 0);
	chk("no work zero",
	    (long)ob_d11_irq_has_work(0, OB_D11_IRQ_OWNED_MASK), 0);
	chk("no work invalid",
	    (long)ob_d11_irq_has_work(0xffffffffu, OB_D11_IRQ_OWNED_MASK), 0);

	/* ack generation never includes an unowned or unknown bit */
	chk("ack owned only",
	    (long)ob_d11_irq_ack_bits(OB_D11_MI_DMAINT | OB_D11_MI_TBTT,
				      OB_D11_IRQ_OWNED_MASK),
	    (long)OB_D11_MI_DMAINT);
	chk("ack excludes unknown",
	    (long)ob_d11_irq_ack_bits(OB_D11_MI_DMAINT | unknown_bit,
				      OB_D11_IRQ_OWNED_MASK),
	    (long)OB_D11_MI_DMAINT);
	chk("ack none", (long)ob_d11_irq_ack_bits(OB_D11_MI_TBTT,
						  OB_D11_IRQ_OWNED_MASK), 0);

	/* unexpected = known & ~owned (unknown bits are not reported) */
	chk("unexpected tbtt",
	    (long)ob_d11_irq_unexpected(OB_D11_MI_DMAINT | OB_D11_MI_TBTT,
					OB_D11_IRQ_OWNED_MASK,
					OB_D11_IRQ_KNOWN_MASK),
	    (long)OB_D11_MI_TBTT);
	chk("unexpected excludes unknown",
	    (long)ob_d11_irq_unexpected(unknown_bit, OB_D11_IRQ_OWNED_MASK,
					OB_D11_IRQ_KNOWN_MASK), 0);
	chk("unexpected excludes owned",
	    (long)ob_d11_irq_unexpected(OB_D11_MI_DMAINT, OB_D11_IRQ_OWNED_MASK,
					OB_D11_IRQ_KNOWN_MASK), 0);

	/* mask_clear disables owned bits, preserves unrelated ones */
	chk("mask_clear",
	    (long)ob_d11_irq_mask_clear(OB_D11_MI_DMAINT | OB_D11_MI_TBTT |
					OB_D11_MI_TFS,
					OB_D11_IRQ_OWNED_MASK),
	    (long)(OB_D11_MI_TBTT | OB_D11_MI_TFS));
	chk("mask_clear noop",
	    (long)ob_d11_irq_mask_clear(OB_D11_MI_TBTT, OB_D11_IRQ_OWNED_MASK),
	    (long)OB_D11_MI_TBTT);

	if (failures) {
		printf("openbrcm irq tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm irq tests: PASS\n");
	return 0;
}

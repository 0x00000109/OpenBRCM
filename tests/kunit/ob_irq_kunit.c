// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the D11 interrupt decision helpers (M3.3).
 * Mirrors tests/host/ob_irq_test.c.
 */
#include <kunit/test.h>
#include "ob_irq.h"

static void ob_irq_helpers_test(struct kunit *test)
{
	u32 unknown_bit = 1u << 24;	/* not a named MI_* bit */

	KUNIT_EXPECT_EQ(test, OB_D11_IRQ_OWNED_MASK & ~OB_D11_IRQ_KNOWN_MASK, 0u);
	KUNIT_EXPECT_NE(test, OB_D11_IRQ_OWNED_MASK, 0u);
	KUNIT_EXPECT_EQ(test, OB_D11_IRQ_KNOWN_MASK & unknown_bit, 0u);

	KUNIT_EXPECT_FALSE(test, ob_d11_irq_status_valid(0xffffffffu));
	KUNIT_EXPECT_TRUE(test, ob_d11_irq_status_valid(0));
	KUNIT_EXPECT_TRUE(test,
			  ob_d11_irq_status_valid(OB_D11_MI_DMAINT));

	KUNIT_EXPECT_EQ(test,
			ob_d11_irq_pending(OB_D11_MI_DMAINT,
					   OB_D11_IRQ_OWNED_MASK),
			OB_D11_MI_DMAINT);
	KUNIT_EXPECT_EQ(test,
			ob_d11_irq_pending(OB_D11_MI_TBTT,
					   OB_D11_IRQ_OWNED_MASK), 0u);

	KUNIT_EXPECT_TRUE(test,
			  ob_d11_irq_has_work(OB_D11_MI_DMAINT,
					      OB_D11_IRQ_OWNED_MASK));
	KUNIT_EXPECT_FALSE(test,
			   ob_d11_irq_has_work(0xffffffffu,
					       OB_D11_IRQ_OWNED_MASK));

	KUNIT_EXPECT_EQ(test,
			ob_d11_irq_ack_bits(OB_D11_MI_DMAINT | OB_D11_MI_TBTT,
					    OB_D11_IRQ_OWNED_MASK),
			OB_D11_MI_DMAINT);
	KUNIT_EXPECT_EQ(test,
			ob_d11_irq_ack_bits(OB_D11_MI_DMAINT | unknown_bit,
					    OB_D11_IRQ_OWNED_MASK),
			OB_D11_MI_DMAINT);

	KUNIT_EXPECT_EQ(test,
			ob_d11_irq_unexpected(OB_D11_MI_DMAINT | OB_D11_MI_TBTT,
					      OB_D11_IRQ_OWNED_MASK,
					      OB_D11_IRQ_KNOWN_MASK),
			OB_D11_MI_TBTT);
	KUNIT_EXPECT_EQ(test,
			ob_d11_irq_unexpected(unknown_bit,
					      OB_D11_IRQ_OWNED_MASK,
					      OB_D11_IRQ_KNOWN_MASK), 0u);

	KUNIT_EXPECT_EQ(test,
			ob_d11_irq_mask_clear(OB_D11_MI_DMAINT | OB_D11_MI_TBTT,
					      OB_D11_IRQ_OWNED_MASK),
			OB_D11_MI_TBTT);
}

static struct kunit_case ob_irq_cases[] = {
	KUNIT_CASE(ob_irq_helpers_test),
	{}
};

static struct kunit_suite ob_irq_suite = {
	.name = "openbrcm_irq",
	.test_cases = ob_irq_cases,
};

kunit_test_suite(ob_irq_suite);
MODULE_LICENSE("GPL");

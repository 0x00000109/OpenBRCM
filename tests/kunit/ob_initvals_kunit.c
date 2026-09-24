// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the isolated common-initvals mode (M3.4D2B).
 * Mirrors tests/host/ob_initvals_test.c (pure plan/count/postcondition/mode
 * helpers). No hardware, no MMIO.
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_initvals.h"

/* Build one vendor IV record: offset, width, value (little-endian). */
static void put_iv(u8 *p, u16 off, u16 width, u32 val)
{
	p[0] = off & 0xff;
	p[1] = off >> 8;
	p[2] = width & 0xff;
	p[3] = width >> 8;
	p[4] = val & 0xff;
	p[5] = (val >> 8) & 0xff;
	p[6] = (val >> 16) & 0xff;
	p[7] = (val >> 24) & 0xff;
}

static void ob_initvals_plan_test(struct kunit *test)
{
	u8 t[4 * 8];
	struct ob_initvals_plan plan;

	put_iv(t + 0, 0x686, 2, 0x09d0);
	put_iv(t + 8, 0x160, 4, 0x03010005);
	put_iv(t + 16, 0x124, 2, 0x0004);
	put_iv(t + 24, 0xffff, 0, 0);

	KUNIT_EXPECT_EQ(test, ob_initvals_plan_from_table(t, sizeof(t), &plan), 0);
	KUNIT_EXPECT_EQ(test, plan.records, 3u);	/* terminator excluded */
	KUNIT_EXPECT_EQ(test, plan.w16, 2u);
	KUNIT_EXPECT_EQ(test, plan.w32, 1u);

	put_iv(t + 0, 0x160, 4, 1);
	KUNIT_EXPECT_EQ(test, ob_initvals_plan_from_table(t, 7, &plan), -EINVAL);
	put_iv(t + 8, 0x164, 4, 1);
	KUNIT_EXPECT_EQ(test, ob_initvals_plan_from_table(t, 16, &plan),
			-ENODATA);
	put_iv(t + 0, 0x160, 1, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	KUNIT_EXPECT_EQ(test, ob_initvals_plan_from_table(t, 16, &plan),
			-EINVAL);
}

static void ob_initvals_counts_test(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, ob_initvals_counts_ok(610, 113, 497));
	KUNIT_EXPECT_FALSE(test, ob_initvals_counts_ok(609, 113, 497));
	KUNIT_EXPECT_FALSE(test, ob_initvals_counts_ok(610, 114, 496));
	KUNIT_EXPECT_FALSE(test, ob_initvals_counts_ok(0, 0, 0));
}

static void ob_initvals_post_test(struct kunit *test)
{
	struct ob_initvals_post p = {
		.fifosize0 = OB_INITVALS_FIFOSIZE0_EXPECTED,
		.fifosize1 = OB_INITVALS_FIFOSIZE1_EXPECTED,
		.fifosize2 = OB_INITVALS_FIFOSIZE2_EXPECTED,
		.fifosize3 = OB_INITVALS_FIFOSIZE3_EXPECTED,
		.macintmask = OB_INITVALS_MACINTMASK_EXPECTED,
		.maccontrol = OB_INITVALS_MACCONTROL_EXPECTED,
		.shm14 = OB_INITVALS_SHM14_EXPECTED,
	};

	KUNIT_EXPECT_TRUE(test, ob_initvals_post_ok(&p));
	p.fifosize0 = 0;
	KUNIT_EXPECT_FALSE(test, ob_initvals_post_ok(&p));
	p.fifosize0 = OB_INITVALS_FIFOSIZE0_EXPECTED;
	p.macintmask = 1;
	KUNIT_EXPECT_FALSE(test, ob_initvals_post_ok(&p));
	p.macintmask = OB_INITVALS_MACINTMASK_EXPECTED;
	p.maccontrol = 0x04020403;
	KUNIT_EXPECT_FALSE(test, ob_initvals_post_ok(&p));
	p.maccontrol = OB_INITVALS_MACCONTROL_EXPECTED;
	p.shm14 = 0;
	KUNIT_EXPECT_FALSE(test, ob_initvals_post_ok(&p));
}

static struct kunit_case ob_initvals_cases[] = {
	KUNIT_CASE(ob_initvals_plan_test),
	KUNIT_CASE(ob_initvals_counts_test),
	KUNIT_CASE(ob_initvals_post_test),
	{}
};

static struct kunit_suite ob_initvals_suite = {
	.name = "openbrcm_initvals",
	.test_cases = ob_initvals_cases,
};

kunit_test_suite(ob_initvals_suite);
MODULE_LICENSE("GPL");

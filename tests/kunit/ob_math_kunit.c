// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit suite for the pure math (RE Stage 6).
 *
 * Wire this into a KUnit-enabled kernel build; it exercises the same
 * invariants as tests/host/ob_math_test.c.
 */
#include <kunit/test.h>
#include "ob_channel.h"
#include "ob_rate.h"

static void ob_channels_test(struct kunit *test)
{
	int ch;

	for (ch = 1; ch <= 13; ch++)
		KUNIT_EXPECT_EQ(test, ob_channel_to_freq(ch, OB_BAND_2GHZ), 2407 + 5 * ch);
	KUNIT_EXPECT_EQ(test, ob_channel_to_freq(14, OB_BAND_2GHZ), 2484);
	KUNIT_EXPECT_EQ(test, ob_channel_to_freq(200, OB_BAND_2GHZ), -1);
	KUNIT_EXPECT_EQ(test, ob_channel_to_freq(36, OB_BAND_5GHZ), 5180);

	for (ch = 1; ch <= 14; ch++)
		KUNIT_EXPECT_EQ(test,
			ob_freq_to_channel(ob_channel_to_freq(ch, OB_BAND_2GHZ), OB_BAND_2GHZ), ch);
}

static void ob_mcs_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(0, 20, 1, false), 6500);
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(7, 20, 1, false), 65000);
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(0, 40, 1, false), 13500);
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(0, 80, 1, false), 29250);
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(9, 80, 1, false), 390000);
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(9, 80, 2, false), 780000);
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(20, 20, 1, false), 0);
}

static struct kunit_case ob_math_cases[] = {
	KUNIT_CASE(ob_channels_test),
	KUNIT_CASE(ob_mcs_test),
	{}
};

static struct kunit_suite ob_math_suite = {
	.name = "openbrcm_math",
	.test_cases = ob_math_cases,
};

kunit_test_suite(ob_math_suite);
MODULE_LICENSE("GPL");

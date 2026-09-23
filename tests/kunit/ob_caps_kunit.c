// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the advertised 2.4 GHz capabilities.
 * Mirrors tests/host/ob_caps_test.c.
 */
#include <kunit/test.h>
#include "ob_channel.h"
#include "ob_rate.h"

static void ob_caps_channels_test(struct kunit *test)
{
	int i;

	KUNIT_EXPECT_EQ(test, OB_N_2GHZ_CHANNELS, 14);
	for (i = 0; i < OB_N_2GHZ_CHANNELS; i++) {
		KUNIT_EXPECT_EQ(test, ob_2ghz_channels[i], i + 1);
		KUNIT_EXPECT_EQ(test,
			ob_channel_to_freq(ob_2ghz_channels[i], OB_BAND_2GHZ),
			i == 13 ? 2484 : 2407 + 5 * (i + 1));
	}
}

static void ob_caps_rates_test(struct kunit *test)
{
	static const int exp[OB_N_2GHZ_RATES] = {
		10, 20, 55, 110, 60, 90, 120, 180, 240, 360, 480, 540
	};
	int i;

	KUNIT_EXPECT_EQ(test, OB_N_2GHZ_RATES, 12);
	for (i = 0; i < OB_N_2GHZ_RATES; i++)
		KUNIT_EXPECT_EQ(test,
			(ob_2ghz_rates[i].rate500 & 0x7f) * 5, exp[i]);
}

static void ob_caps_ht_test(struct kunit *test)
{
	/* advertised highest rate (144) derived from our MCS formula */
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(7, 20, 2, true) / 1000, 144);
	KUNIT_EXPECT_EQ(test, ob_mcs_to_rate_kbps(0, 20, 2, false), 13000);
}

static struct kunit_case ob_caps_cases[] = {
	KUNIT_CASE(ob_caps_channels_test),
	KUNIT_CASE(ob_caps_rates_test),
	KUNIT_CASE(ob_caps_ht_test),
	{}
};

static struct kunit_suite ob_caps_suite = {
	.name = "openbrcm_caps",
	.test_cases = ob_caps_cases,
};

kunit_test_suite(ob_caps_suite);
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the isolated ucode-upload mode (M3.4D2A).
 * Mirrors tests/host/ob_ucode_test.c (pure policy/arithmetic/poll helpers).
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_ucode.h"

static void ob_ucode_mode_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			ob_isolated_mode_count(false, false, false, false), 0u);
	KUNIT_EXPECT_EQ(test,
			ob_isolated_mode_select(false, false, false, false),
			OB_ISOLATED_NONE);
	KUNIT_EXPECT_EQ(test,
			ob_isolated_mode_select(true, false, false, false),
			OB_ISOLATED_FW_VALIDATE);
	KUNIT_EXPECT_EQ(test,
			ob_isolated_mode_select(false, true, false, false),
			OB_ISOLATED_UCODE_TEST);
	KUNIT_EXPECT_EQ(test,
			ob_isolated_mode_select(false, false, true, false),
			OB_ISOLATED_INITVALS_TEST);
	KUNIT_EXPECT_EQ(test,
			ob_isolated_mode_select(false, false, false, true),
			OB_ISOLATED_DMA_TEST);
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
			ob_isolated_mode_select(true, true, false, false)));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
			ob_isolated_mode_select(true, false, true, false)));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
			ob_isolated_mode_select(false, true, true, false)));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
			ob_isolated_mode_select(true, false, false, true)));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
			ob_isolated_mode_select(false, true, false, true)));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
			ob_isolated_mode_select(false, false, true, true)));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
			ob_isolated_mode_select(true, true, true, true)));
	/*
	 * D2A regression: ucode_test_only never applies the common table. The
	 * D2B initvals mode and the D3A0 DMA mode both do (D3A0 entry state is
	 * the D2B exit).
	 */
	KUNIT_EXPECT_FALSE(test, ob_isolated_mode_applies_initvals(
			OB_ISOLATED_UCODE_TEST));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_applies_initvals(
			OB_ISOLATED_INITVALS_TEST));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_applies_initvals(
			OB_ISOLATED_DMA_TEST));
	/* Only dma_test_only enters the DMA lifecycle. */
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_uses_dma(
			OB_ISOLATED_DMA_TEST));
	KUNIT_EXPECT_FALSE(test, ob_isolated_mode_uses_dma(
			OB_ISOLATED_INITVALS_TEST));
	KUNIT_EXPECT_FALSE(test, ob_isolated_mode_skips_teardown(
			OB_ISOLATED_DMA_TEST));
}

static void ob_ucode_constants_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, OB_UCODE_MACCONTROL_UPLOAD, 0x04000404u);
	KUNIT_EXPECT_EQ(test, OB_UCODE_MACCONTROL_PSM, 0x04020402u);
	KUNIT_EXPECT_EQ(test, OB_UCODE_OBJADDR_AUTO_INC, 0x03000000u);
	KUNIT_EXPECT_EQ(test, OB_UCODE_OBJADDR_SHM_SEL, 0x00010000u);
	KUNIT_EXPECT_EQ(test, OB_UCODE_POLL_TIMEOUT, 0x000f4249u);
	KUNIT_EXPECT_EQ(test, OB_UCODE_POLL_DELAY_US, 10u);
	KUNIT_EXPECT_EQ(test, OB_UCODE_POLL_STEP, 10u);

	KUNIT_EXPECT_EQ(test,
			OB_UCODE_MACCONTROL_UPLOAD & OB_UCODE_UPLOAD_EXPECTED,
			OB_UCODE_UPLOAD_EXPECTED);
	KUNIT_EXPECT_EQ(test,
			OB_UCODE_MACCONTROL_UPLOAD & OB_UCODE_UPLOAD_FORBIDDEN,
			0u);
	KUNIT_EXPECT_EQ(test,
			OB_UCODE_MACCONTROL_PSM & OB_UCODE_PSM_EXPECTED,
			OB_UCODE_PSM_EXPECTED);
	KUNIT_EXPECT_EQ(test,
			OB_UCODE_MACCONTROL_PSM & OB_UCODE_PSM_FORBIDDEN, 0u);
}

static void ob_ucode_write_count_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_ucode_words_from_size(43400), 10850u);
	KUNIT_EXPECT_EQ(test, ob_ucode_words_from_size(0), 0u);
	KUNIT_EXPECT_TRUE(test, ob_ucode_writes_ok(10850, 10850));
	KUNIT_EXPECT_FALSE(test, ob_ucode_writes_ok(10849, 10850));
	KUNIT_EXPECT_FALSE(test, ob_ucode_writes_ok(10851, 10850));
}

static void ob_ucode_poll_test(struct kunit *test)
{
	u32 max = ob_ucode_poll_max_iterations(OB_UCODE_POLL_TIMEOUT,
					       OB_UCODE_POLL_STEP);
	u32 remaining = OB_UCODE_POLL_TIMEOUT;
	u32 iters = 0, status = 0;
	u32 remaining_ok = OB_UCODE_POLL_TIMEOUT;
	u32 iters_ok = 0, status_ok = 0;

	KUNIT_EXPECT_EQ(test, max, 100000u);
	KUNIT_EXPECT_TRUE(test, ob_ucode_poll_expired(9, 10));
	KUNIT_EXPECT_FALSE(test, ob_ucode_poll_expired(10, 10));
	KUNIT_EXPECT_TRUE(test, ob_ucode_poll_expired(0, 10));
	KUNIT_EXPECT_EQ(test, ob_ucode_poll_max_iterations(100, 0), 0u);

	for (;;) {
		if (ob_ucode_mac_suspended(status))
			break;
		if (ob_ucode_poll_expired(remaining, OB_UCODE_POLL_STEP))
			break;
		remaining -= OB_UCODE_POLL_STEP;
		iters++;
	}
	KUNIT_EXPECT_EQ(test, iters, max);
	KUNIT_EXPECT_EQ(test, iters * OB_UCODE_POLL_DELAY_US, 1000000u);

	for (;;) {
		status_ok = (iters_ok == 3) ? OB_D11_MI_MACSSPNDD : 0x8u;
		if (ob_ucode_mac_suspended(status_ok))
			break;
		if (ob_ucode_poll_expired(remaining_ok, OB_UCODE_POLL_STEP))
			break;
		remaining_ok -= OB_UCODE_POLL_STEP;
		iters_ok++;
	}
	KUNIT_EXPECT_TRUE(test, ob_ucode_mac_suspended(status_ok));
	KUNIT_EXPECT_EQ(test, iters_ok, 3u);
}

static void ob_ucode_teardown_test(struct kunit *test)
{
	struct ob_ucode_teardown t = ob_ucode_teardown_plan(true);

	KUNIT_EXPECT_FALSE(test, t.mac80211);
	KUNIT_EXPECT_FALSE(test, t.rx);
	KUNIT_EXPECT_FALSE(test, t.irq);
	KUNIT_EXPECT_FALSE(test, t.dma);

	t = ob_ucode_teardown_plan(false);
	KUNIT_EXPECT_TRUE(test, t.mac80211);
	KUNIT_EXPECT_TRUE(test, t.rx);
	KUNIT_EXPECT_TRUE(test, t.irq);
	KUNIT_EXPECT_TRUE(test, t.dma);
}

static struct kunit_case ob_ucode_cases[] = {
	KUNIT_CASE(ob_ucode_mode_test),
	KUNIT_CASE(ob_ucode_constants_test),
	KUNIT_CASE(ob_ucode_write_count_test),
	KUNIT_CASE(ob_ucode_poll_test),
	KUNIT_CASE(ob_ucode_teardown_test),
	{}
};

static struct kunit_suite ob_ucode_suite = {
	.name = "openbrcm_ucode",
	.test_cases = ob_ucode_cases,
};

kunit_test_suite(ob_ucode_suite);
MODULE_LICENSE("GPL");

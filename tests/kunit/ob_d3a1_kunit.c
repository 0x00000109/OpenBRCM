// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the isolated D3A1 vendor post-common / pre-PHY
 * D11 tail test (M3.4D3A1). Mirrors tests/host/ob_d3a1_test.c (pure helpers
 * only). NOTE: this environment does not wire KUnit into the build, so this
 * mirror is not executed here (see docs/m34d3a1_vendor_tail_test.md).
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_d3a1.h"
#include "ob_ucode.h"

static void ob_d3a1_mode_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
		ob_d3a1_mode_select(false, false, false, false, true),
		OB_ISOLATED_D3A1_TEST);
	KUNIT_EXPECT_EQ(test,
		ob_d3a1_mode_count(false, false, false, false, true), 1u);
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_conflict(
		ob_d3a1_mode_select(false, false, false, true, true)));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_uses_dma(
		OB_ISOLATED_D3A1_TEST));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_applies_initvals(
		OB_ISOLATED_D3A1_TEST));
	KUNIT_EXPECT_FALSE(test, ob_isolated_mode_skips_teardown(
		OB_ISOLATED_D3A1_TEST));
}

static void ob_d3a1_order_test(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, ob_d3a1_order_ok());
	KUNIT_EXPECT_LT(test, ob_d3a1_stage_rank(OB_D3A1_STAGE_T1),
			ob_d3a1_stage_rank(OB_D3A1_STAGE_DMA));
	KUNIT_EXPECT_LT(test, ob_d3a1_stage_rank(OB_D3A1_STAGE_DMA),
			ob_d3a1_stage_rank(OB_D3A1_STAGE_T2));
	KUNIT_EXPECT_EQ(test,
		ob_d3a1_stage_rank(OB_D3A1_STAGE_STOP_BEFORE_SUB6656C),
		OB_D3A1_STAGE__COUNT - 1);
}

static void ob_d3a1_fifo_model_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo_table1(0), 7u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo_table1(6), 5u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo_flush_val(0x200), 0x100);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo7_rqpri(7, 0x100), 0x2a);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo7_rqpri(0, 0x100), 0x100 - 0x2a);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo7_54e(0), 0x1216);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo7_54e(7), 0x262a);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo42_x532(0), 3u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo42_x532(2), 3u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo42_x532(40), 2u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo42_x532(41), 1u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_fifo42_x530(0), 0x8007);
	KUNIT_EXPECT_EQ(test, OB_D3A1_FIFO7_WRITES, 42u);
	KUNIT_EXPECT_EQ(test, OB_D3A1_FIFO42_WRITES, 168u);
	KUNIT_EXPECT_EQ(test, OB_D3A1_FIFO_TOTAL_WRITES, 212u);
}

static void ob_d3a1_mac_test(struct kunit *test)
{
	const u8 mac[6] = {0x2c, 0xfd, 0xa1, 0x61, 0x40, 0x25};

	KUNIT_EXPECT_EQ(test, ob_d3a1_mac_word(mac, 0), 0x2cfd);
	KUNIT_EXPECT_EQ(test, ob_d3a1_mac_word(mac, 1), 0xa161);
	KUNIT_EXPECT_EQ(test, ob_d3a1_mac_word(mac, 2), 0x4025);
	KUNIT_EXPECT_EQ(test, OB_D3A1_SHM_MAC_0, 0x78cu);
}

static void ob_d3a1_btc_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_d3a1_btc_base(0), 0u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_btc_base(0x10), 0x20u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_btc_key_action(NULL),
			OB_D3A1_BTC_SKIP);
	KUNIT_EXPECT_EQ(test, ob_d3a1_btc_key_action("1"),
			OB_D3A1_BTC_WRITE);
	KUNIT_EXPECT_TRUE(test, ob_d3a1_chip_uses_macfreq(0x4352));
}

static void ob_d3a1_scr_and_clock_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, OB_D3A1_SCR_RATE_24, 0x24u);
	KUNIT_EXPECT_EQ(test, OB_D3A1_SCR_RATE_24_FIRST_INIT_SKIPPED, 1);
	KUNIT_EXPECT_EQ(test, ob_d3a1_tsf_frac(3), 0x80000000u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_tsf_frac(1500), 0xa0000000u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_tsf_frac(400000), 0x00999999u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_tsf_frac(1), OB_D3A1_DIV_NO_WRITE);
	KUNIT_EXPECT_EQ(test, OB_D3A1_REG_TSF_FRAC_L, 0x62eu);
}

static void ob_d3a1_muladd_div_test(struct kunit *test)
{
	u32 hi, lo;

	ob_d3a1_muladd(&hi, &lo, 3, 5, 7);
	KUNIT_EXPECT_EQ(test, hi, 0u);
	KUNIT_EXPECT_EQ(test, lo, 22u);
	ob_d3a1_muladd(&hi, &lo, 0x10000, 0x10000, 0);
	KUNIT_EXPECT_EQ(test, hi, 1u);
	KUNIT_EXPECT_EQ(test, lo, 0u);

	KUNIT_EXPECT_EQ(test, ob_d3a1_u64_divide(0x3a9, 0x80000000u, 3),
			0x80000000u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_u64_divide(0x3a9, 0x80000000u, 2),
			0xbffffc57u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_u64_divide(0x3a9, 0x80000000u, 1500),
			0xa0000000u);
	KUNIT_EXPECT_EQ(test,
			ob_d3a1_u64_divide(0x3a9, 0x80000000u, 400000),
			0x00999999u);
	KUNIT_EXPECT_EQ(test,
			ob_d3a1_u64_divide(0x3a9, 0x80000000u, 1),
			OB_D3A1_DIV_NO_WRITE);
}

static void ob_d3a1_bb_vcofreq_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_d3a1_bb_vcofreq_from_pll(0x3f80u, 0),
			0x03072580u);
	KUNIT_EXPECT_EQ(test,
			ob_d3a1_bb_vcofreq_from_pll(0x3f90u, 0x1234u),
			0x030725efu);
	KUNIT_EXPECT_EQ(test, ob_d3a1_bb_vcofreq_from_pll(0x70u, 0), 0u);
	KUNIT_EXPECT_EQ(test, ob_d3a1_bb_vcofreq_from_pll(0x540000u, 0),
			0u);
	KUNIT_EXPECT_TRUE(test, ob_d3a1_poll_continue(false, 0xd1u));
	KUNIT_EXPECT_FALSE(test, ob_d3a1_poll_continue(false, 9));

	/* Exact predicates: 0x530 whole-word zero, 0x540 bit0 clear. */
	KUNIT_EXPECT_TRUE(test, ob_d3a1_fifo530_done(0x0000));
	KUNIT_EXPECT_FALSE(test, ob_d3a1_fifo530_done(0x8007));
	KUNIT_EXPECT_FALSE(test, ob_d3a1_fifo530_done(0x0007));
	KUNIT_EXPECT_TRUE(test, ob_d3a1_fifo540_done(0x0004));
	KUNIT_EXPECT_FALSE(test, ob_d3a1_fifo540_done(0x0001));
	KUNIT_EXPECT_TRUE(test, ob_d3a1_poll_done(OB_D3A1_POLL_540, 0x0007));
	KUNIT_EXPECT_FALSE(test, ob_d3a1_poll_done(OB_D3A1_POLL_530, 0x0007));
}

static void ob_d3a1_lifecycle_test(struct kunit *test)
{
	struct ob_d3a0_lifecycle lc;

	memset(&lc, 0, sizeof(lc));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_can_free(&lc));
	lc.tx[0] = OB_D3A0_PROGRAMMED;
	lc.rx = OB_D3A0_PROGRAMMED;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_can_free(&lc));
	lc.engines_stopped = true;
	lc.free_allowed = true;
	KUNIT_EXPECT_TRUE(test, ob_d3a0_can_free(&lc));
	lc.fatal = true;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_can_free(&lc));
}

static struct kunit_case ob_d3a1_test_cases[] = {
	KUNIT_CASE(ob_d3a1_mode_test),
	KUNIT_CASE(ob_d3a1_order_test),
	KUNIT_CASE(ob_d3a1_fifo_model_test),
	KUNIT_CASE(ob_d3a1_mac_test),
	KUNIT_CASE(ob_d3a1_btc_test),
	KUNIT_CASE(ob_d3a1_scr_and_clock_test),
	KUNIT_CASE(ob_d3a1_muladd_div_test),
	KUNIT_CASE(ob_d3a1_bb_vcofreq_test),
	KUNIT_CASE(ob_d3a1_lifecycle_test),
	{}
};

static struct kunit_suite ob_d3a1_test_suite = {
	.name = "openbrcm_d3a1",
	.test_cases = ob_d3a1_test_cases,
};
kunit_test_suite(ob_d3a1_test_suite);

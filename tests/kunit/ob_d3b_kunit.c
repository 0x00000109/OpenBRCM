// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the isolated D3B band-init +
 * d11ac1bsinitvals42 test (M3.4D3B). Mirrors tests/host/ob_d3b_test.c
 * (pure helpers only). NOTE: this environment does not wire KUnit into the
 * build, so this mirror is not executed here (see docs/m34d3b_band_init_test.md
 * §9).
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_d3b.h"
#include "ob_d3a0.h"
#include "ob_ucode.h"

static void ob_d3b_board_decode_test(struct kunit *test)
{
	u16 sp[OB_SPROM11_WORDS];
	struct ob_sprom_board b;

	memset(sp, 0, sizeof(sp));
	sp[OB_SPROM11_BOARDTYPE_WORD] = 0x85ba;
	sp[OB_SPROM11_BOARDFLAGS_LO_WORD] = 0x1000;
	sp[OB_SPROM11_BOARDFLAGS_HI_WORD] = 0x1000;
	sp[OB_SPROM11_ANT_WORD] = 0x0707;
	sp[OB_SPROM11_TXRXC_WORD] = 0x0077;

	KUNIT_EXPECT_TRUE(test,
			  ob_d3b_decode_rev11_board(sp, OB_SPROM11_WORDS, &b));
	KUNIT_EXPECT_EQ(test, b.boardtype, 0x85ba);
	KUNIT_EXPECT_EQ(test, b.boardflags, 0x10001000u);
	KUNIT_EXPECT_EQ(test, b.aa2g, 7);
	KUNIT_EXPECT_EQ(test, b.aa5g, 7);
	KUNIT_EXPECT_EQ(test, b.antswitch, 0);
	KUNIT_EXPECT_TRUE(test, b.antswitch_present);

	/* skip-all-ones absent rule */
	sp[OB_SPROM11_TXRXC_WORD] = 0xff00;
	KUNIT_EXPECT_TRUE(test,
			  ob_d3b_decode_rev11_board(sp, OB_SPROM11_WORDS, &b));
	KUNIT_EXPECT_FALSE(test, b.antswitch_present);
	KUNIT_EXPECT_EQ(test, b.antswitch, 0);
}

static void ob_d3b_antsel_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_d3b_antsel_type(0x85ba, 0x10001000u, 0, 7, 7),
			0);
	KUNIT_EXPECT_EQ(test, ob_d3b_antsel_type(0x04, 0x0, 0, 7, 0), 2);
	KUNIT_EXPECT_EQ(test, ob_d3b_antsel_type(0x04, 0x8, 0, 6, 0), 1);
	KUNIT_EXPECT_EQ(test, ob_d3b_antsel_type(0x10, 0x8, 0, 0, 0), 1);
	KUNIT_EXPECT_EQ(test, ob_d3b_antsel_type(0x00, 0x8, 9, 0, 0), 1);
	KUNIT_EXPECT_EQ(test, ob_d3b_antsel_type(0x05, 0x0, 4, 0, 0), 3);
	KUNIT_EXPECT_EQ(test, ob_d3b_antsel_type(0x05, 0x0, 8, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, ob_d3b_mhf3_from_antsel(1), 0x1);
	KUNIT_EXPECT_EQ(test, ob_d3b_mhf3_from_antsel(2), 0x3);
}

static void ob_d3b_mhf_test(struct kunit *test)
{
	u16 sp[OB_SPROM11_WORDS];
	struct ob_sprom_board b;
	struct ob_d3b_band band;
	u16 mhf[OB_D3B_MHF_COUNT];

	memset(sp, 0, sizeof(sp));
	sp[OB_SPROM11_BOARDTYPE_WORD] = 0x85ba;
	sp[OB_SPROM11_BOARDFLAGS_LO_WORD] = 0x1000;
	sp[OB_SPROM11_BOARDFLAGS_HI_WORD] = 0x1000;
	sp[OB_SPROM11_ANT_WORD] = 0x0707;
	sp[OB_SPROM11_TXRXC_WORD] = 0x0077;
	KUNIT_ASSERT_TRUE(test,
			  ob_d3b_decode_rev11_board(sp, OB_SPROM11_WORDS, &b));
	ob_d3b_band_from_board(&b, OB_D3B_BAND_PHYTYPE_AC, &band);
	ob_d3b_mhf_vector(&band, mhf);
	KUNIT_EXPECT_TRUE(test, ob_d3b_mhf_is_captured(mhf));
	KUNIT_EXPECT_EQ(test, mhf[0], 0x0100);
	KUNIT_EXPECT_EQ(test, mhf[4], 0x0080);
}

static void ob_d3b_shape_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, OB_D3B_BS_RECORDS, 73u);
	KUNIT_EXPECT_EQ(test, OB_D3B_BS_W16, 39u);
	KUNIT_EXPECT_EQ(test, OB_D3B_BS_W32, 34u);
	KUNIT_EXPECT_TRUE(test, ob_d3b_bs_counts_ok(73, 39, 34));
	KUNIT_EXPECT_FALSE(test, ob_d3b_bs_counts_ok(73, 34, 39));
	KUNIT_EXPECT_TRUE(test, ob_d3b_bs_offset_allowed(0x0160));
	KUNIT_EXPECT_TRUE(test, ob_d3b_bs_offset_allowed(0x0700));
	KUNIT_EXPECT_FALSE(test, ob_d3b_bs_offset_allowed(0x03fc));
	KUNIT_EXPECT_FALSE(test, ob_d3b_bs_offset_allowed(0x03fe));
	KUNIT_EXPECT_FALSE(test, ob_d3b_bs_offset_allowed(0x03d8));
	KUNIT_EXPECT_FALSE(test, ob_d3b_bs_offset_allowed(0x03da));
}

static void ob_d3b_mode_and_order_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
		ob_isolated_mode_select6(0, 0, 0, 0, 0, 1),
		OB_ISOLATED_D3B_TEST);
	KUNIT_EXPECT_EQ(test,
		ob_isolated_mode_select6(0, 0, 0, 0, 1, 1),
		OB_ISOLATED_CONFLICT);
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_uses_dma(
		OB_ISOLATED_D3B_TEST));
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_applies_initvals(
		OB_ISOLATED_D3B_TEST));
	KUNIT_EXPECT_FALSE(test, ob_isolated_mode_skips_teardown(
		OB_ISOLATED_D3B_TEST));
	KUNIT_EXPECT_TRUE(test, ob_d3b_dma_live_during_d3b());
	KUNIT_EXPECT_TRUE(test, ob_d3b_stage_rank_ok());
}

static void ob_d3b_post_test(struct kunit *test)
{
	struct ob_d3b_post p;

	memset(&p, 0, sizeof(p));
	p.mhf[0] = 0x0100;
	p.mhf[4] = 0x0080;
	p.shm_10 = OB_D3B_SHM_OVR_10_VAL;
	p.shm_1c = OB_D3B_SHM_OVR_1C_VAL;
	p.shm_94 = OB_D3B_SHM_OVR_94_VAL;
	p.maccontrol = OB_D3B_MACCONTROL_EXPECTED;
	p.macintmask = 0;
	p.records = 73;
	p.w16 = 39;
	p.w32 = 34;
	KUNIT_EXPECT_TRUE(test, ob_d3b_post_ok(&p));
	p.maccontrol |= OB_D3B_MCTL_EN_MAC;
	KUNIT_EXPECT_FALSE(test, ob_d3b_post_ok(&p));
}

static void ob_d3b_lifecycle_test(struct kunit *test)
{
	struct ob_d3a0_lifecycle lc;

	memset(&lc, 0, sizeof(lc));
	lc.rx = OB_D3A0_PROGRAMMED;
	lc.tx[0] = OB_D3A0_PROGRAMMED;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_can_free(&lc));
	lc.fatal = true;
	lc.engines_stopped = true;
	lc.free_allowed = true;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_can_free(&lc));
	lc.fatal = false;
	KUNIT_EXPECT_TRUE(test, ob_d3a0_can_free(&lc));
}

static struct kunit_case ob_d3b_test_cases[] = {
	KUNIT_CASE(ob_d3b_board_decode_test),
	KUNIT_CASE(ob_d3b_antsel_test),
	KUNIT_CASE(ob_d3b_mhf_test),
	KUNIT_CASE(ob_d3b_shape_test),
	KUNIT_CASE(ob_d3b_mode_and_order_test),
	KUNIT_CASE(ob_d3b_post_test),
	KUNIT_CASE(ob_d3b_lifecycle_test),
	{}
};

static struct kunit_suite ob_d3b_test_suite = {
	.name = "openbrcm_d3b",
	.test_cases = ob_d3b_test_cases,
};
kunit_test_suite(ob_d3b_test_suite);

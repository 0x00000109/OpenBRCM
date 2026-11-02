// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the recovered 2069 per-channel tuning rows.
 *
 * Uses the generated static tables and a fake radio/PHY backend. No MMIO,
 * no hardware, no device state.
 */
#include <kunit/test.h>
#include <linux/string.h>

#include "bcm43xx_chan_tuning.h"
#include "bcm43xx_chan_tuning_data.h"

struct bcm43xx_fake_io {
	u16 radio_reg[64];
	u16 radio_val[64];
	unsigned int radio_n;
	u16 phy_reg[8];
	u16 phy_val[8];
	unsigned int phy_n;
};

static void bcm43xx_fake_radio_write(void *ctx, u16 reg, u16 value)
{
	struct bcm43xx_fake_io *f = ctx;

	if (f->radio_n < ARRAY_SIZE(f->radio_reg)) {
		f->radio_reg[f->radio_n] = reg;
		f->radio_val[f->radio_n] = value;
	}
	f->radio_n++;
}

static void bcm43xx_fake_phy_write(void *ctx, u16 reg, u16 value)
{
	struct bcm43xx_fake_io *f = ctx;

	if (f->phy_n < ARRAY_SIZE(f->phy_reg)) {
		f->phy_reg[f->phy_n] = reg;
		f->phy_val[f->phy_n] = value;
	}
	f->phy_n++;
}

static void bcm43xx_lookup_test(struct kunit *test)
{
	static const struct {
		u16 chanspec;
		u16 freq;
		u16 channel;
	} cases[] = {
		{ 0x1001, 2412, 1 },	/* 2.4 GHz, HT20 */
		{ 0xd024, 5180, 36 },	/* 5 GHz, HT20 */
		{ 0xe024, 5180, 36 },	/* 5 GHz, VHT80 */
		{ 0xd895, 5745, 149 },	/* 5 GHz, VHT40 */
		{ 0xd0b8, 4920, 184 },	/* 4.9 GHz, HT20 */
	};
	struct bcm43xx_chan_tuning_env env = {
		.radio_class = 0,
		.radio_rev = 4,
		.refclk_hz = BCM43XX_REFCLK_40MHZ,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		const void *row74 = NULL, *row5e = NULL, *row60 = NULL;
		const void *row5e_mode2 = NULL;
		u32 code = 0;
		int ret;

		KUNIT_EXPECT_EQ(test, bcm43xx_chan_tuning_channel(cases[i].chanspec),
				cases[i].channel);
		ret = bcm43xx_get_chan_tuning_row(&env,
						  &bcm43xx_chan_tuning_default_tables,
						  cases[i].chanspec, cases[i].freq,
						  &code, &row74, &row5e, &row60,
						  &row5e_mode2);
		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, code, cases[i].freq);
		KUNIT_EXPECT_TRUE(test, row74 != NULL);
		KUNIT_EXPECT_TRUE(test, !row5e && !row60 && !row5e_mode2);
	}
}

static void bcm43xx_apply_test(struct kunit *test)
{
	struct bcm43xx_chan_tuning_env env = {
		.radio_class = 0,
		.radio_rev = 4,
		.refclk_hz = BCM43XX_REFCLK_40MHZ,
	};
	struct bcm43xx_fake_io fake = { 0 };
	struct bcm43xx_chan_tuning_io io = {
		.ctx = &fake,
		.write_radio_reg = bcm43xx_fake_radio_write,
		.phy_reg_write = bcm43xx_fake_phy_write,
		.chip_id = 0x4352,
	};
	const void *row74 = NULL, *row5e = NULL, *row60 = NULL;
	const void *row5e_mode2 = NULL;
	const u8 *raw;
	u32 code = 0;
	unsigned int i;
	int ret;

	ret = bcm43xx_get_chan_tuning_row(&env,
					  &bcm43xx_chan_tuning_default_tables,
					  0x1001, 2412, &code, &row74, &row5e,
					  &row60, &row5e_mode2);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_TRUE(test, row74 != NULL);

	ret = bcm43xx_apply_chan_tuning(BCM43XX_ROW_LEGACY, row74, &io);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, fake.radio_n, 50);
	KUNIT_EXPECT_EQ(test, fake.phy_n, 6);

	raw = row74;
	for (i = 0; i < BCM43XX_CHAN_TUNING_BW_COUNT; i++) {
		u16 expected;

		memcpy(&expected, raw + BCM43XX_CHAN_TUNING_STRIDE_LEGACY - 12 +
		       2 * i, sizeof(expected));
		KUNIT_EXPECT_EQ(test, fake.phy_reg[i], BCM43XX_PHY_BW1A + i);
		KUNIT_EXPECT_EQ(test, fake.phy_val[i], expected);
	}
}

static void bcm43xx_wide_band_apply_test(struct kunit *test)
{
	static const struct {
		u16 chanspec;
		u16 freq;
	} cases[] = {
		{ 0xe024, 5180 },	/* ch36 VHT80 */
		{ 0xd895, 5745 },	/* ch149 VHT40 */
		{ 0xd0b8, 4920 },	/* ch184 4.9 GHz */
	};
	struct bcm43xx_chan_tuning_env env = {
		.radio_class = 0,
		.radio_rev = 4,
		.refclk_hz = BCM43XX_REFCLK_40MHZ,
	};
	unsigned int c;

	for (c = 0; c < ARRAY_SIZE(cases); c++) {
		struct bcm43xx_fake_io fake = { 0 };
		struct bcm43xx_chan_tuning_io io = {
			.ctx = &fake,
			.write_radio_reg = bcm43xx_fake_radio_write,
			.phy_reg_write = bcm43xx_fake_phy_write,
			.chip_id = 0x4352,
		};
		const void *row74 = NULL, *row5e = NULL, *row60 = NULL;
		const void *row5e_mode2 = NULL;
		u32 code = 0;
		unsigned int i;
		int ret;

		ret = bcm43xx_get_chan_tuning_row(&env,
						  &bcm43xx_chan_tuning_default_tables,
						  cases[c].chanspec, cases[c].freq,
						  &code, &row74, &row5e, &row60,
						  &row5e_mode2);
		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, code, cases[c].freq);
		KUNIT_ASSERT_TRUE(test, row74 != NULL);

		ret = bcm43xx_apply_chan_tuning(BCM43XX_ROW_LEGACY, row74, &io);
		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, fake.radio_n, 50);
		KUNIT_EXPECT_EQ(test, fake.phy_n, 6);
		for (i = 0; i < BCM43XX_CHAN_TUNING_BW_COUNT; i++)
			KUNIT_EXPECT_EQ(test, fake.phy_reg[i],
					BCM43XX_PHY_BW1A + i);
	}
}

static void bcm43xx_refclk_test(struct kunit *test)
{
	struct bcm43xx_chan_tuning_env env = {
		.radio_class = 1,
		.radio_rev = 0x18,
		.refclk_hz = 20000000u,
	};
	const u8 *row20 = bcm43xx_chan_tuning_2069rev_18 +
			 14 * BCM43XX_CHAN_TUNING_STRIDE_2069;
	const u8 *row40 = bcm43xx_chan_tuning_2069rev_18_40 +
			 14 * BCM43XX_CHAN_TUNING_STRIDE_2069;
	const void *row74 = NULL, *row5e = NULL, *row60 = NULL;
	const void *row5e_mode2 = NULL;
	struct bcm43xx_fake_io fake = { 0 };
	struct bcm43xx_chan_tuning_io io = {
		.ctx = &fake,
		.write_radio_reg = bcm43xx_fake_radio_write,
		.phy_reg_write = bcm43xx_fake_phy_write,
		.chip_id = 0x4352,
	};
	u32 code = 0;
	int ret;

	/* ch36 VHT80: 20 MHz ALP clock selects the normal class-1 table. */
	ret = bcm43xx_get_chan_tuning_row(&env,
					  &bcm43xx_chan_tuning_default_tables,
					  0xe024, 5180, &code, &row74, &row5e,
					  &row60, &row5e_mode2);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, row5e == (const void *)row20);
	KUNIT_EXPECT_TRUE(test, !row74 && !row60 && !row5e_mode2);
	ret = bcm43xx_apply_chan_tuning(BCM43XX_ROW_2069, row5e, &io);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, fake.radio_n, 39);
	KUNIT_EXPECT_EQ(test, fake.phy_n, 6);

	/* The 40 MHz reference clock selects the _40 table for the same row. */
	memset(&fake, 0, sizeof(fake));
	env.refclk_hz = BCM43XX_REFCLK_40MHZ;
	ret = bcm43xx_get_chan_tuning_row(&env,
					  &bcm43xx_chan_tuning_default_tables,
					  0xe024, 5180, &code, &row74, &row5e,
					  &row60, &row5e_mode2);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, row5e == (const void *)row40);
	KUNIT_EXPECT_TRUE(test, row20 != row40);
	KUNIT_EXPECT_TRUE(test, !row74 && !row60 && !row5e_mode2);
	ret = bcm43xx_apply_chan_tuning(BCM43XX_ROW_2069, row5e, &io);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, fake.radio_n, 39);
	KUNIT_EXPECT_EQ(test, fake.phy_n, 6);
}

static void bcm43xx_reject_test(struct kunit *test)
{
	struct bcm43xx_chan_tuning_env env = {
		.radio_class = 0,
		.radio_rev = 4,
		.refclk_hz = BCM43XX_REFCLK_40MHZ,
	};
	const void *row74 = NULL, *row5e = NULL, *row60 = NULL;
	const void *row5e_mode2 = NULL;
	struct bcm43xx_fake_io fake = { 0 };
	struct bcm43xx_chan_tuning_io io = {
		.ctx = &fake,
		.write_radio_reg = bcm43xx_fake_radio_write,
		.phy_reg_write = bcm43xx_fake_phy_write,
		.chip_id = 0x4352,
	};
	u32 code = 0;
	int ret;

	/* Unknown channel: no row and no callback. */
	ret = bcm43xx_get_chan_tuning_row(&env,
					  &bcm43xx_chan_tuning_default_tables,
					  0x10fe, 0, &code, &row74, &row5e,
					  &row60, &row5e_mode2);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);
	KUNIT_EXPECT_TRUE(test, !row74 && !row5e && !row60 && !row5e_mode2);

	/* Class 0 revision 0x99 is outside the table index. */
	env.radio_rev = 0x99;
	ret = bcm43xx_get_chan_tuning_row(&env,
					  &bcm43xx_chan_tuning_default_tables,
					  0x1001, 2412, &code, &row74, &row5e,
					  &row60, &row5e_mode2);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	/* Class 1 revision 0x13 has no table in the generated defaults. */
	env.radio_rev = 0x13;
	env.radio_class = 1;
	ret = bcm43xx_get_chan_tuning_row(&env,
					  &bcm43xx_chan_tuning_default_tables,
					  0x1001, 2412, &code, &row74, &row5e,
					  &row60, &row5e_mode2);
	KUNIT_EXPECT_EQ(test, ret, -ENOENT);

	/* Invalid API arguments must not dereference. */
	KUNIT_EXPECT_EQ(test, bcm43xx_get_chan_tuning_row(NULL,
							  &bcm43xx_chan_tuning_default_tables,
							  0x1001, 0, &code, &row74,
							  &row5e, &row60, &row5e_mode2),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, bcm43xx_apply_chan_tuning(BCM43XX_ROW_LEGACY,
							NULL, &io), -EINVAL);
	KUNIT_EXPECT_EQ(test, bcm43xx_apply_chan_tuning(BCM43XX_ROW_LEGACY,
							row74, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, fake.radio_n, 0);
	KUNIT_EXPECT_EQ(test, fake.phy_n, 0);
}

static struct kunit_case bcm43xx_chan_tuning_cases[] = {
	KUNIT_CASE(bcm43xx_lookup_test),
	KUNIT_CASE(bcm43xx_apply_test),
	KUNIT_CASE(bcm43xx_wide_band_apply_test),
	KUNIT_CASE(bcm43xx_refclk_test),
	KUNIT_CASE(bcm43xx_reject_test),
	{}
};

static struct kunit_suite bcm43xx_chan_tuning_suite = {
	.name = "openbrcm_chan_tuning",
	.test_cases = bcm43xx_chan_tuning_cases,
};

kunit_test_suite(bcm43xx_chan_tuning_suite);
MODULE_LICENSE("GPL");

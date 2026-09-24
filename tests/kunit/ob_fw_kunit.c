// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for rev42 firmware validation (M3.4D1).
 * Mirrors tests/host/ob_fw_test.c (pure helpers only).
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_fw.h"

static void ob_fw_ucode_test(struct kunit *test)
{
	static const u8 w[12] = {
		0x4e, 0x10, 0x00, 0x03,
		0x60, 0xbc, 0x01, 0x00,
		0x25, 0x0e, 0xf0, 0x02,
	};
	u32 v;

	KUNIT_EXPECT_TRUE(test, ob_fw_ucode_size_ok(OB_FW_UCODE42_SIZE));
	KUNIT_EXPECT_FALSE(test, ob_fw_ucode_size_ok(OB_FW_UCODE42_SIZE + 1));
	KUNIT_EXPECT_FALSE(test, ob_fw_ucode_size_ok(0));
	KUNIT_EXPECT_EQ(test, ob_fw_ucode_words(OB_FW_UCODE42_SIZE), 10850u);
	KUNIT_EXPECT_EQ(test, ob_fw_ucode_words(12), 3u);

	ob_fw_ucode_word(w, 0, &v);
	KUNIT_EXPECT_EQ(test, v, 0x0300104eu);
	ob_fw_ucode_word(w, 1, &v);
	KUNIT_EXPECT_EQ(test, v, 0x0001bc60u);
	ob_fw_ucode_word(w, 2, &v);
	KUNIT_EXPECT_EQ(test, v, 0x02f00e25u);
}

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

static void ob_fw_iv_valid_test(struct kunit *test)
{
	u8 t[4 * 8];
	struct ob_fw_iv_stats st;
	struct ob_fw_iv iv;

	put_iv(t + 0, 0x160, 4, 0x03010005);
	put_iv(t + 8, 0x686, 2, 0x09d0);
	put_iv(t + 16, 0x124, 4, 0x4);
	put_iv(t + 24, 0xffff, 0, 0);

	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, sizeof(t), &st), 0);
	KUNIT_EXPECT_EQ(test, st.records, 3u);
	KUNIT_EXPECT_EQ(test, st.w16, 1u);
	KUNIT_EXPECT_EQ(test, st.w32, 2u);
	KUNIT_EXPECT_EQ(test, st.terminator_index, 3u);

	KUNIT_EXPECT_EQ(test, ob_fw_iv_at(t, sizeof(t), 0, &iv), 0);
	KUNIT_EXPECT_EQ(test, iv.offset, 0x160u);
	KUNIT_EXPECT_EQ(test, iv.width, 4u);
	KUNIT_EXPECT_EQ(test, iv.value, 0x03010005u);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_at(t, sizeof(t), 1, &iv), 0);
	KUNIT_EXPECT_EQ(test, iv.width, 2u);
	KUNIT_EXPECT_EQ(test, iv.value, 0x09d0u);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_at(t, sizeof(t), 4, &iv), -EINVAL);
}

static void ob_fw_iv_bad_test(struct kunit *test)
{
	u8 t[5 * 8];
	struct ob_fw_iv_stats st;

	put_iv(t, 0x160, 4, 1);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, 7, &st), -EINVAL);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, 0, &st), -EINVAL);

	put_iv(t + 0, 0x160, 4, 1);
	put_iv(t + 8, 0x164, 4, 1);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, 16, &st), -ENODATA);

	put_iv(t + 0, 0x160, 1, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, 16, &st), -EINVAL);

	put_iv(t + 0, 0x1000, 4, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, 16, &st), -EINVAL);

	put_iv(t + 0, 0x160, 4, 1);
	put_iv(t + 8, 0xffff, 0, 0);
	put_iv(t + 16, 0x164, 4, 1);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, 24, &st), -EINVAL);

	put_iv(t + 0, 0xffff, 0, 0);
	KUNIT_EXPECT_EQ(test, ob_fw_iv_parse(t, 8, &st), 0);
	KUNIT_EXPECT_EQ(test, st.records, 0u);
}

static void ob_fw_hash_test(struct kunit *test)
{
	static const u8 s[3] = { 'a', 'b', 'c' };

	KUNIT_EXPECT_EQ(test, ob_fw_hash64((const u8 *)"", 0),
			0xcbf29ce484222325ULL);
	KUNIT_EXPECT_EQ(test, ob_fw_hash64(s, 3), 0xe71fa2190541574bULL);
}

static struct kunit_case ob_fw_cases[] = {
	KUNIT_CASE(ob_fw_ucode_test),
	KUNIT_CASE(ob_fw_iv_valid_test),
	KUNIT_CASE(ob_fw_iv_bad_test),
	KUNIT_CASE(ob_fw_hash_test),
	{}
};

static struct kunit_suite ob_fw_suite = {
	.name = "openbrcm_fw",
	.test_cases = ob_fw_cases,
};

kunit_test_suite(ob_fw_suite);
MODULE_LICENSE("GPL");

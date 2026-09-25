// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit mirror of tests/host/ob_radio_test.c for the isolated
 * BCM2069 radio identity probe (D4-BLOCKER-PLL-BRANCH-HW-PROBE). Pure
 * sequence/decode layer only; no MMIO, no hardware.
 *
 * NOTE: the OpenBRCM tree does not run KUnit locally (the kernel test harness
 * needs a KUnit-enabled build); this mirror is provided for in-tree/KUnit CI.
 * The host test is the executed authority locally.
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_radio.h"
#include "ob_ucode.h"

struct ob_radio_fake {
	u32 sentinel[2];
	u16 reg[2];
	int read32_n;
	u32 reads32;
	u32 reads16;
	u32 writes;
	u16 w_off[4];
	u16 w_val[4];
};

static u32 ob_radio_fake_read32(void *ctx, u16 off)
{
	struct ob_radio_fake *f = ctx;
	int idx = f->read32_n++;

	KUNIT_ASSERT_EQ(NULL, off, OB_RADIO_REG_SENTINEL);
	f->reads32++;
	return f->sentinel[idx < 2 ? idx : 1];
}

static u16 ob_radio_fake_read16(void *ctx, u16 off)
{
	struct ob_radio_fake *f = ctx;
	int idx = f->reads16;

	f->reads16++;
	(void)off;
	return f->reg[idx == 0 ? 0 : 1];
}

static void ob_radio_fake_write16(void *ctx, u16 off, u16 val)
{
	struct ob_radio_fake *f = ctx;

	if (f->writes < 4) {
		f->w_off[f->writes] = off;
		f->w_val[f->writes] = val;
	}
	f->writes++;
}

static const struct ob_radio_io_ops ob_radio_fake_ops = {
	.read32_trusted = ob_radio_fake_read32,
	.read16 = ob_radio_fake_read16,
	.write16 = ob_radio_fake_write16,
};

static struct ob_radio_fake ob_radio_fake_init(u16 r0, u16 r1)
{
	struct ob_radio_fake f = {};

	f.sentinel[0] = 0x04020402u;
	f.sentinel[1] = 0x04020402u;
	f.reg[0] = r0;
	f.reg[1] = r1;
	return f;
}

static void ob_radio_mode_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_isolated_mode_select7(false, false, false,
						       false, false, false, true),
			OB_ISOLATED_RADIO_ID_PROBE);
	KUNIT_EXPECT_EQ(test, ob_isolated_mode_select7(false, false, false,
						       false, false, false,
						       false),
			OB_ISOLATED_NONE);
	KUNIT_EXPECT_EQ(test, ob_isolated_mode_select7(false, true, false,
						       false, false, false, true),
			OB_ISOLATED_CONFLICT);
	KUNIT_EXPECT_TRUE(test, ob_isolated_mode_skips_teardown(
				 OB_ISOLATED_RADIO_ID_PROBE));
	KUNIT_EXPECT_FALSE(test, ob_isolated_mode_uses_dma(
				  OB_ISOLATED_RADIO_ID_PROBE));
	KUNIT_EXPECT_FALSE(test, ob_isolated_mode_applies_initvals(
				  OB_ISOLATED_RADIO_ID_PROBE));
}

static void ob_radio_decode_test(struct kunit *test)
{
	struct ob_radio_observation o;

	o = ob_radio_decode(0x0010u, 0x2069u);
	KUNIT_EXPECT_EQ(test, o.revision_class, 1);
	KUNIT_EXPECT_EQ(test, o.radio_rev, 0x10);
	KUNIT_EXPECT_EQ(test, o.branch, OB_RADIO_BRANCH_A);

	o = ob_radio_decode(0x0020u, 0x2069u);
	KUNIT_EXPECT_EQ(test, o.branch, OB_RADIO_BRANCH_B);

	o = ob_radio_decode(0x0003u, 0x2069u);
	KUNIT_EXPECT_EQ(test, o.branch, OB_RADIO_BRANCH_SKIP);

	o = ob_radio_decode(0x0010u, 0x1234u);
	KUNIT_EXPECT_FALSE(test, o.id_accepted);
	KUNIT_EXPECT_EQ(test, o.branch, OB_RADIO_BRANCH_UNKNOWN);
}

static void ob_radio_sequence_test(struct kunit *test)
{
	struct ob_radio_fake f = ob_radio_fake_init(0x0010u, 0x2069u);
	bool lost = false;
	struct ob_radio_seq_result res;

	KUNIT_EXPECT_EQ(test, ob_radio_sequence(&ob_radio_fake_ops, &f, &lost,
						&res), 0);
	KUNIT_EXPECT_EQ(test, res.radio_writes, 2u);
	KUNIT_EXPECT_EQ(test, res.radio_reads, 2u);
	KUNIT_EXPECT_EQ(test, f.writes, 2u);
	KUNIT_EXPECT_EQ(test, f.w_off[0], OB_RADIO_REG_ADDR_LATCH);
	KUNIT_EXPECT_EQ(test, f.w_val[0], 0);
	KUNIT_EXPECT_EQ(test, f.w_off[1], OB_RADIO_REG_ADDR_LATCH);
	KUNIT_EXPECT_EQ(test, f.w_val[1], 1);
}

static void ob_radio_devlost_test(struct kunit *test)
{
	/* before: no MMIO at all */
	{
		struct ob_radio_fake f = ob_radio_fake_init(0x0010u, 0x2069u);
		bool lost = true;
		struct ob_radio_seq_result res;

		KUNIT_EXPECT_EQ(test, ob_radio_sequence(&ob_radio_fake_ops, &f,
							&lost, &res), -5);
		KUNIT_EXPECT_EQ(test, f.writes, 0u);
		KUNIT_EXPECT_EQ(test, f.reads16 + f.reads32, 0u);
	}
	/* pre sentinel all-ones: latch, zero writes */
	{
		struct ob_radio_fake f = ob_radio_fake_init(0x0010u, 0x2069u);
		bool lost = false;
		struct ob_radio_seq_result res;

		f.sentinel[0] = OB_GUARD_ALL_ONES32;
		ob_radio_sequence(&ob_radio_fake_ops, &f, &lost, &res);
		KUNIT_EXPECT_TRUE(test, lost);
		KUNIT_EXPECT_TRUE(test, res.stopped_dev_lost);
		KUNIT_EXPECT_EQ(test, f.writes, 0u);
		KUNIT_EXPECT_EQ(test, f.reads16, 0u);
	}
	/* post sentinel all-ones: observation invalidated */
	{
		struct ob_radio_fake f = ob_radio_fake_init(0x0020u, 0x2069u);
		bool lost = false;
		struct ob_radio_seq_result res;

		f.sentinel[1] = OB_GUARD_ALL_ONES32;
		KUNIT_EXPECT_EQ(test, ob_radio_sequence(&ob_radio_fake_ops, &f,
							&lost, &res), -5);
		KUNIT_EXPECT_TRUE(test, res.dev_lost_after);
		KUNIT_EXPECT_FALSE(test, res.valid);
		KUNIT_EXPECT_EQ(test, f.writes, 2u);
	}
}

static struct kunit_case ob_radio_test_cases[] = {
	KUNIT_CASE(ob_radio_mode_test),
	KUNIT_CASE(ob_radio_decode_test),
	KUNIT_CASE(ob_radio_sequence_test),
	KUNIT_CASE(ob_radio_devlost_test),
	{}
};

static struct kunit_suite ob_radio_test_suite = {
	.name = "openbrcm_radio",
	.test_cases = ob_radio_test_cases,
};
kunit_test_suite(ob_radio_test_suite);

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the central device-loss access guard
 * (D4-BLOCKER-DEV-LOST-PHY-PATH). Mirrors tests/host/ob_guard_test.c (pure
 * decision layer only; no MMIO, no hardware).
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_guard.h"

/* A fake backend so the guard decision layer can be exercised alone. */
struct ob_guard_fake {
	u32 r32;
	u16 r16;
	u32 reads;
	u32 writes;
	u32 last_off;
	u32 last_val;
};

static u32 ob_guard_fake_read32(void *ctx, u16 off)
{
	struct ob_guard_fake *f = ctx;
	f->reads++;
	f->last_off = off;
	return f->r32;
}
static u16 ob_guard_fake_read16(void *ctx, u16 off)
{
	struct ob_guard_fake *f = ctx;
	f->reads++;
	f->last_off = off;
	return f->r16;
}
static void ob_guard_fake_write32(void *ctx, u16 off, u32 v)
{
	struct ob_guard_fake *f = ctx;
	f->writes++;
	f->last_off = off;
	f->last_val = v;
}
static void ob_guard_fake_write16(void *ctx, u16 off, u16 v)
{
	struct ob_guard_fake *f = ctx;
	f->writes++;
	f->last_off = off;
	(void)v;
}

static const struct ob_guard_ops ob_guard_fake_ops = {
	.read32 = ob_guard_fake_read32,
	.read16 = ob_guard_fake_read16,
	.write32 = ob_guard_fake_write32,
	.write16 = ob_guard_fake_write16,
};

static void ob_guard_normal_test(struct kunit *test)
{
	struct ob_guard_fake f = { .r32 = 0x44020402u };
	struct ob_guard_stat st = {};
	u32 v;

	v = ob_guard_do_read32(false, &st, &ob_guard_fake_ops, &f, 0x120);
	KUNIT_EXPECT_EQ(test, v, 0x44020402u);
	KUNIT_EXPECT_EQ(test, st.reads_issued, 1u);
	KUNIT_EXPECT_EQ(test, f.reads, 1u);

	ob_guard_do_write32(false, &st, &ob_guard_fake_ops, &f, 0x120, 0x04020402u);
	KUNIT_EXPECT_EQ(test, st.writes_issued, 1u);
	KUNIT_EXPECT_EQ(test, f.writes, 1u);
	KUNIT_EXPECT_EQ(test, f.last_val, 0x04020402u);
}

static void ob_guard_latch_test(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test, ob_guard_next_latch(false, true, 1u));
	KUNIT_EXPECT_FALSE(test, ob_guard_next_latch(false, false, OB_GUARD_ALL_ONES32));
	KUNIT_EXPECT_TRUE(test, ob_guard_next_latch(false, true, OB_GUARD_ALL_ONES32));
	KUNIT_EXPECT_TRUE(test, ob_guard_next_latch(true, true, 0x44020402u));
	KUNIT_EXPECT_TRUE(test, ob_guard_next_latch(true, false, 0u));
	KUNIT_EXPECT_FALSE(test, ob_guard_next_latch(false, true, 0xfffffffeu));
}

static void ob_guard_blocked_test(struct kunit *test)
{
	struct ob_guard_fake f = {};
	struct ob_guard_stat st = {};
	u16 h;
	u32 v;

	/* writes are suppressed after latch (D11/PHY/radio/DMA/SHM all) */
	ob_guard_do_write32(true, &st, &ob_guard_fake_ops, &f, 0x120, 0xdeadbeefu);
	ob_guard_do_write16(true, &st, &ob_guard_fake_ops, &f, 0x7cc, 0xf4u);
	KUNIT_EXPECT_EQ(test, f.writes, 0u);
	KUNIT_EXPECT_EQ(test, st.writes_blocked, 2u);

	/* reads return the dead sentinel without MMIO */
	v = ob_guard_do_read32(true, &st, &ob_guard_fake_ops, &f, 0x120);
	h = ob_guard_do_read16(true, &st, &ob_guard_fake_ops, &f, 0x164);
	KUNIT_EXPECT_EQ(test, v, OB_GUARD_DEAD32);
	KUNIT_EXPECT_EQ(test, h, OB_GUARD_DEAD16);
	KUNIT_EXPECT_EQ(test, f.reads, 0u);
	KUNIT_EXPECT_EQ(test, ob_guard_read_blocked(true), true);
	KUNIT_EXPECT_EQ(test, ob_guard_write_blocked(true), true);
}

static struct kunit_case ob_guard_test_cases[] = {
	KUNIT_CASE(ob_guard_normal_test),
	KUNIT_CASE(ob_guard_latch_test),
	KUNIT_CASE(ob_guard_blocked_test),
	{}
};

static struct kunit_suite ob_guard_test_suite = {
	.name = "openbrcm_guard",
	.test_cases = ob_guard_test_cases,
};
kunit_test_suite(ob_guard_test_suite);

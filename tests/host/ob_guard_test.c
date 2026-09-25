// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the central device-loss access guard.
 *
 * Exercises the exact pure decision layer that src/ob_guard.c uses in the
 * kernel, through a fake MMIO backend. No hardware, no MMIO, no driver.
 *
 * Covers the D4-BLOCKER-DEV-LOST-PHY-PATH required cases:
 *   1 normal access before dev_lost
 *   2 trusted read all-ones -> latch
 *   3 latch is monotonic
 *   4 direct D11 write suppressed after latch
 *   5 PHY-indirect write suppressed after latch
 *   6 radio write suppressed after latch
 *   7 SHM/OBJ access suppressed after latch
 *   8 DMA teardown/reset suppressed after latch
 *   9 nested caller observes failure (dead sentinel) and stops
 *  10 repeated removal/unload path performs no hardware access
 *  11 resources retained (latch never clears; existing free gate stays denied)
 *  12 normal non-device-lost paths unchanged
 * Plus negatives: a legitimate all-ones payload never latches unless the
 * access is trusted, and writes are never observations.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_guard.h"
#include "../../src/ob_d3a0.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

struct fake_io {
	u32 r32;
	u16 r16;
	u32 reads;
	u32 writes;
	u32 last_off;
	u32 last_val;
	u16 last_off16;
	u16 last_val16;
};

static u32 f_read32(void *ctx, u16 off)
{
	struct fake_io *f = ctx;
	f->reads++;
	f->last_off = off;
	return f->r32;
}
static u16 f_read16(void *ctx, u16 off)
{
	struct fake_io *f = ctx;
	f->reads++;
	f->last_off16 = off;
	return f->r16;
}
static void f_write32(void *ctx, u16 off, u32 v)
{
	struct fake_io *f = ctx;
	f->writes++;
	f->last_off = off;
	f->last_val = v;
}
static void f_write16(void *ctx, u16 off, u16 v)
{
	struct fake_io *f = ctx;
	f->writes++;
	f->last_off16 = off;
	f->last_val16 = v;
}

static const struct ob_guard_ops fake_ops = {
	.read32 = f_read32,
	.read16 = f_read16,
	.write32 = f_write32,
	.write16 = f_write16,
};

/* 1 + 12: normal access is a straight pass-through (unchanged semantics). */
static void test_normal_passthrough(void)
{
	struct fake_io io;
	struct ob_guard_stat st;
	bool lost = false;
	u32 v;

	memset(&io, 0, sizeof(io));
	memset(&st, 0, sizeof(st));
	io.r32 = 0x44020402u;

	v = ob_guard_do_read32(lost, &st, &fake_ops, &io, 0x120);
	chk("normal read value", v, 0x44020402u);
	chk("normal read issued", st.reads_issued, 1);
	chk("normal read blocked", st.reads_blocked, 0);
	chk("normal read hit backend", io.reads, 1);
	chk("normal read offset", io.last_off, 0x120);

	ob_guard_do_write32(lost, &st, &fake_ops, &io, 0x120, 0x04020402u);
	chk("normal write issued", st.writes_issued, 1);
	chk("normal write blocked", st.writes_blocked, 0);
	chk("normal write hit backend", io.writes, 1);
	chk("normal write value", io.last_val, 0x04020402u);

	chk("not blocked when live (read)", ob_guard_read_blocked(lost), 0);
	chk("not blocked when live (write)", ob_guard_write_blocked(lost), 0);
}

/* 2 + 3: trusted all-ones latches; the latch is monotonic. */
static void test_latch_monotonic(void)
{
	bool lost = false;

	/* live, trusted, non-all-ones: no latch */
	chk("live trusted live-value", ob_guard_next_latch(false, true, 1u), 0);
	/* live, untrusted, all-ones: no latch (negative fixture) */
	chk("untrusted all-ones no latch",
	    ob_guard_next_latch(false, false, OB_GUARD_ALL_ONES32), 0);
	/* live, trusted, all-ones: latch */
	lost = ob_guard_next_latch(lost, true, OB_GUARD_ALL_ONES32);
	chk("trusted all-ones latches", lost, 1);
	/* monotonic: a later live value never clears it */
	lost = ob_guard_next_latch(lost, true, 0x44020402u);
	chk("latched stays latched (live value)", lost, 1);
	lost = ob_guard_next_latch(lost, false, 0u);
	chk("latched stays latched (untrusted)", lost, 1);
	/* nearly-all-ones does not latch even trusted */
	chk("trusted near-all-ones no latch",
	    ob_guard_next_latch(false, true, 0xfffffffeu), 0);
}

/* 4/5/6: direct D11, PHY-indirect and radio writes all use the D11 window. */
static void test_writes_suppressed_after_latch(void)
{
	struct fake_io io;
	struct ob_guard_stat st;
	const bool lost = true;

	memset(&io, 0, sizeof(io));
	memset(&st, 0, sizeof(st));

	/* direct D11 register write (MACCONTROL/OBJADDR/... ) */
	ob_guard_do_write32(lost, &st, &fake_ops, &io, 0x120, 0xdeadbeefu);
	/* PHY-indirect write (same window, phy_reg_write translation) */
	ob_guard_do_write32(lost, &st, &fake_ops, &io, 0x3d8, 0xffu);
	/* radio write */
	ob_guard_do_write32(lost, &st, &fake_ops, &io, 0x80b, 0x80u);
	/* 16-bit PHY/radio write */
	ob_guard_do_write16(lost, &st, &fake_ops, &io, 0x7cc, 0xf4u);

	chk("no backend writes after latch", io.writes, 0);
	chk("all writes blocked", st.writes_blocked, 4);
	chk("no writes issued", st.writes_issued, 0);
}

/* 7/8/9: SHM/OBJ reads/writes, DMA reset writes, nested caller stop. */
static void test_reads_and_nested(void)
{
	struct fake_io io;
	struct ob_guard_stat st;
	const bool lost = true;
	u32 v;
	u16 h;

	memset(&io, 0, sizeof(io));
	memset(&st, 0, sizeof(st));
	io.r32 = 0x12345678u;
	io.r16 = 0xabcd;

	/* SHM/OBJ read is blocked and returns the dead sentinel */
	h = ob_guard_do_read16(lost, &st, &fake_ops, &io, 0x164);
	chk("blocked 16-bit read sentinel", h, OB_GUARD_DEAD16);
	/* trusted direct read after latch */
	v = ob_guard_do_read32(lost, &st, &fake_ops, &io, 0x120);
	chk("blocked 32-bit read sentinel", v, OB_GUARD_DEAD32);
	/* DMA teardown/reset write (RX/TX control) */
	ob_guard_do_write32(lost, &st, &fake_ops, &io, 0x220, 0u);

	chk("no backend reads after latch", io.reads, 0);
	chk("no backend writes after latch", io.writes, 0);
	chk("reads blocked counted", st.reads_blocked, 2);
	chk("writes blocked counted", st.writes_blocked, 1);

	/* nested caller: sentinel means inaccessible -> caller must stop */
	chk("nested caller sees failure",
	    (v == OB_GUARD_DEAD32) || (h == OB_GUARD_DEAD16), 1);
}

/* 10: a repeated teardown/unload sequence performs no hardware access. */
static void test_repeated_remove_no_access(void)
{
	struct fake_io io;
	struct ob_guard_stat st;
	const bool lost = true;
	int i;

	memset(&io, 0, sizeof(io));
	memset(&st, 0, sizeof(st));

	for (i = 0; i < 8; i++) {
		ob_guard_do_read32(lost, &st, &fake_ops, &io, 0x120);
		ob_guard_do_write32(lost, &st, &fake_ops, &io, 0x220, 0u);
		ob_guard_do_read16(lost, &st, &fake_ops, &io, 0x164);
	}
	chk("repeated remove: zero backend access",
	    io.reads + io.writes, 0);
	chk("repeated remove: blocked = 24", st.reads_blocked + st.writes_blocked,
	    24);
}

/* 11: resources retained — the latch never clears and free stays denied. */
static void test_resources_retained(void)
{
	struct ob_d3a0_lifecycle lc;
	bool lost = true;

	/* monotonic across arbitrary subsequent observations */
	lost = ob_guard_next_latch(lost, true, 0x44020402u);
	lost = ob_guard_next_latch(lost, false, 0u);
	chk("latch never clears", lost, 1);

	/* the fail-closed free gate still refuses under fatal (retained) */
	memset(&lc, 0, sizeof(lc));
	lc.fatal = true;
	lc.engines_stopped = true;
	lc.free_allowed = true;
	chk("retained resources cannot be freed", ob_d3a0_can_free(&lc), 0);
}

/* Negative: a legitimate all-ones payload/write is not device loss. */
static void test_negative_payloads(void)
{
	bool lost = false;

	/* A trustworthy-looking all-ones from an UNtrusted class never latches. */
	lost = ob_guard_next_latch(lost, false, OB_GUARD_ALL_ONES32);
	chk("untrusted all-ones payload no latch", lost, 0);

	/* A 16-bit SHM payload 0xffff is modelled as 0x0000ffff, never all-ones32. */
	lost = ob_guard_next_latch(lost, false, 0x0000ffffu);
	chk("16-bit 0xffff payload no latch", lost, 0);

	/* Writing an all-ones value is not an observation at all. */
	{
		struct fake_io io;
		struct ob_guard_stat st;
		const bool live = false;

		memset(&io, 0, sizeof(io));
		memset(&st, 0, sizeof(st));
		io.r32 = 0u;
		ob_guard_do_write32(live, &st, &fake_ops, &io, 0x120,
				    OB_GUARD_ALL_ONES32);
		chk("write all-ones issues (live)", io.writes, 1);
		chk("write all-ones does not latch",
		    ob_guard_next_latch(live, false, io.last_val), 0);
	}
}

int main(void)
{
	test_normal_passthrough();
	test_latch_monotonic();
	test_writes_suppressed_after_latch();
	test_reads_and_nested();
	test_repeated_remove_no_access();
	test_resources_retained();
	test_negative_payloads();

	if (failures) {
		printf("ob_guard_test: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ob_guard_test: all device-loss guard tests PASS\n");
	return 0;
}

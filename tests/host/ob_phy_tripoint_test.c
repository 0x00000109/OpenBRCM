// SPDX-License-Identifier: GPL-2.0-only
/*
 * Point witness PM1/P0/P1/P2 — host tests (TR01..TR10).
 *
 * The code under test is src/ob_phy_tripoint.c. The backend is a deterministic
 * recorder: it issues no MMIO, so every result here is HOST_EXECUTED against
 * that recorder, not hardware evidence.
 *
 * The interface has no write callback, so "read-only" is enforced by the type.
 * These tests pin the vendor register order, the strict PM1 -> P0 -> P1 -> P2
 * sequencing, the fail-stop paths, the deliberate absence of any value
 * predicate (the vendor's own 0x0721 sample is 0xffff), and the PM1/P0
 * comparison the prefix window depends on.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy_tripoint.h"

#define FAKE_ERR (-71)

struct fake {
	u16 values[OB_PHY_TRIPOINT_REGS];
	u16 regs[64];
	unsigned int nregs;
	u16 fail_reg;		/* 0 = none */
	bool lost;
	unsigned int lost_after;	/* 0 = never */
};

static int f_read16(void *ctx, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	unsigned int i;

	if (f->nregs < 64)
		f->regs[f->nregs] = reg;
	f->nregs++;
	if (f->fail_reg && reg == f->fail_reg)
		return FAKE_ERR;
	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++) {
		if (ob_phy_tripoint_regs[i] == reg) {
			*value = f->values[i];
			break;
		}
	}
	if (i == OB_PHY_TRIPOINT_REGS)
		*value = 0;
	if (f->lost_after && f->nregs >= f->lost_after)
		f->lost = true;
	return 0;
}

static bool f_lost(void *ctx)
{
	struct fake *f = ctx;

	return f->lost;
}

static struct ob_phy_tripoint_ops fake_ops(void)
{
	return (struct ob_phy_tripoint_ops) {
		.read16 = f_read16,
		.device_lost = f_lost,
	};
}

static void fake_values(struct fake *f, u16 seed)
{
	unsigned int i;

	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++)
		f->values[i] = (u16)(seed + i);
}

static void expect_order(const struct fake *f, unsigned int from)
{
	unsigned int i;

	assert(f->nregs == from + OB_PHY_TRIPOINT_REGS);
	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++)
		assert(f->regs[from + i] == ob_phy_tripoint_regs[i]);
}

/* TR01: incomplete arguments are rejected without a callback. */
static void test_invalid_args(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };

	assert(ob_phy_tripoint_run(NULL, &f, &state, OB_PHY_TRIPOINT_PM1) == -22);
	assert(ob_phy_tripoint_run(&ops, &f, NULL, OB_PHY_TRIPOINT_PM1) == -22);
	ops.read16 = NULL;
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == -22);
	ops = fake_ops();
	ops.device_lost = NULL;
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == -22);
	assert(f.nregs == 0);
	printf("TR01 invalid-args PASS\n");
}

/* TR02: PM1 reads the eight registers in the vendor order and stores them. */
static void test_pm1_order_and_snapshot(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };
	unsigned int i;

	fake_values(&f, 0x100);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	expect_order(&f, 0);
	assert(state.observed == 1);
	assert(state.last_reg == 0x0408);
	assert(state.last_error == 0);
	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++)
		assert(state.value[OB_PHY_TRIPOINT_PM1][i] == f.values[i]);
	printf("TR02 PM1 order+snapshot PASS\n");
}

/* TR03: the sequence is strictly PM1, P0, P1, P2, each exactly once. */
static void test_sequence(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };

	/* P0 first is refused and issues nothing. */
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P0) == -22);
	assert(f.nregs == 0);
	assert(state.observed == 0);

	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	/* PM1 twice is refused: a point is sampled once. */
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == -22);
	/* P1 out of order is refused too. */
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P1) == -22);
	assert(f.nregs == OB_PHY_TRIPOINT_REGS);

	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P0) == 0);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P1) == 0);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P2) == 0);
	assert(state.observed == 4);
	assert(f.nregs == 4 * OB_PHY_TRIPOINT_REGS);
	/* P2 after completion is refused. */
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P2) == -22);

	/* An out-of-range point is a configuration error, not a sample. */
	f.nregs = 0;
	assert(ob_phy_tripoint_run(&ops, &f, &state,
				   (enum ob_phy_tripoint_point)9) == -22);
	assert(f.nregs == 0);
	printf("TR03 strict sequence PASS\n");
}

/* TR04: a callback error is fail-stop with the exact failing register. */
static void test_read_error(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };

	f.fail_reg = 0x0721;
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) ==
	       FAKE_ERR);
	assert(state.observed == 0);
	assert(state.last_reg == 0x0721);
	assert(state.last_error == FAKE_ERR);
	/* Four reads completed, the fifth was issued and failed; none after. */
	assert(f.nregs == 5);

	memset(&state, 0, sizeof(state));
	f = (struct fake) { 0 };
	f.fail_reg = 0x0408;
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) ==
	       FAKE_ERR);
	assert(f.nregs == OB_PHY_TRIPOINT_REGS);
	assert(state.observed == 0);
	printf("TR04 read error fail-stop PASS\n");
}

/* TR05: a loss latched during the point stops it before the next read. */
static void test_lost_mid_point(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };

	f.lost_after = 3;
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == -5);
	assert(state.observed == 0);
	assert(state.last_error == -5);
	assert(f.nregs == 3);
	printf("TR05 device lost mid-point PASS\n");
}

/* TR06: a loss already latched issues no read at all. */
static void test_lost_before_point(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };

	f.lost = true;
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == -5);
	assert(f.nregs == 0);
	assert(state.observed == 0);
	assert(state.last_error == -5);
	printf("TR06 pre-existing loss PASS\n");
}

/* TR07: no value is a verdict; the vendor's 0xffff and 0x0000 both pass. */
static void test_no_value_predicate(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };
	unsigned int i;

	/* Exactly the vendor's pre-mirror series from the mmiotrace. */
	f.values[0] = 0x0000;
	f.values[1] = 0x0000;
	f.values[2] = 0x1fff;
	f.values[3] = 0x0000;
	f.values[4] = 0xffff;
	f.values[5] = 0x0000;
	f.values[6] = 0x03ff;
	f.values[7] = 0x0d80;
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++)
		assert(state.value[OB_PHY_TRIPOINT_PM1][i] == f.values[i]);

	/* And the all-zero / all-ones extremes are equally not verdicts. */
	f = (struct fake) { 0 };
	memset(&state, 0, sizeof(state));
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	f = (struct fake) { 0 };
	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++)
		f.values[i] = 0xffff;
	memset(&state, 0, sizeof(state));
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	printf("TR07 no value predicate PASS\n");
}

/* TR08: each point keeps its own row; a later sample never rewrites an earlier. */
static void test_rows_do_not_alias(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };
	unsigned int i;

	fake_values(&f, 0x200);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	fake_values(&f, 0x400);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P0) == 0);
	fake_values(&f, 0x600);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P1) == 0);
	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++) {
		assert(state.value[0][i] == (u16)(0x200 + i));
		assert(state.value[1][i] == (u16)(0x400 + i));
		assert(state.value[2][i] == (u16)(0x600 + i));
	}
	printf("TR08 row isolation PASS\n");
}

/* TR09: the published table and point names are the recovered constants. */
static void test_constants(void)
{
	static const u16 expected[OB_PHY_TRIPOINT_REGS] = {
		0x0739, 0x073a, 0x0725, 0x0729, 0x0721, 0x0728, 0x0720, 0x0408,
	};
	unsigned int i;

	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++)
		assert(ob_phy_tripoint_regs[i] == expected[i]);
	assert(strcmp(ob_phy_tripoint_name(OB_PHY_TRIPOINT_PM1), "PM1") == 0);
	assert(strcmp(ob_phy_tripoint_name(OB_PHY_TRIPOINT_P0), "P0") == 0);
	assert(strcmp(ob_phy_tripoint_name(OB_PHY_TRIPOINT_P1), "P1") == 0);
	assert(strcmp(ob_phy_tripoint_name(OB_PHY_TRIPOINT_P2), "P2") == 0);
	assert(strcmp(ob_phy_tripoint_name((enum ob_phy_tripoint_point)11),
		       "INVALID") == 0);
	printf("TR09 constants PASS\n");
}

/*
 * TR10: the PM1 -> P0 pair. This is the comparison the prefix window reads:
 * one changed word must survive as two distinct rows, and an identical pair
 * must stay identical.
 */
static void test_pm1_vs_p0(void)
{
	struct ob_phy_tripoint_ops ops = fake_ops();
	struct ob_phy_tripoint state = { 0 };
	struct fake f = { 0 };

	fake_values(&f, 0x0);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	f.values[2] = 0x0600;	/* 0x725 differs at P0 */
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P0) == 0);
	assert(state.value[OB_PHY_TRIPOINT_PM1][2] == 0x0002);
	assert(state.value[OB_PHY_TRIPOINT_P0][2] == 0x0600);

	/* Identical pair stays identical. */
	memset(&state, 0, sizeof(state));
	f = (struct fake) { 0 };
	fake_values(&f, 0x0);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_PM1) == 0);
	assert(ob_phy_tripoint_run(&ops, &f, &state, OB_PHY_TRIPOINT_P0) == 0);
	assert(memcmp(state.value[0], state.value[1],
		      sizeof(state.value[0])) == 0);
	printf("TR10 PM1 vs P0 rows PASS\n");
}

/*
 * TR11: the read-path predicate. PM1 and P0 precede the identity publication,
 * so both must use the pre-ID read; P1/P2 use the admitted helper. The first
 * phy_prefix_point01 attempt failed here: PM1 was routed to the admitted path
 * and returned -EINVAL before any MMIO (`TRIPOINT_FAIL point=PM1 reg=0x0739
 * ret=-22`).
 */
static void test_pre_id_predicate(void)
{
	assert(ob_phy_tripoint_uses_pre_id(OB_PHY_TRIPOINT_PM1));
	assert(ob_phy_tripoint_uses_pre_id(OB_PHY_TRIPOINT_P0));
	assert(!ob_phy_tripoint_uses_pre_id(OB_PHY_TRIPOINT_P1));
	assert(!ob_phy_tripoint_uses_pre_id(OB_PHY_TRIPOINT_P2));
	printf("TR11 pre-ID read predicate PASS\n");
}

int main(void)
{
	test_invalid_args();
	test_pm1_order_and_snapshot();
	test_sequence();
	test_read_error();
	test_lost_mid_point();
	test_lost_before_point();
	test_no_value_predicate();
	test_rows_do_not_alias();
	test_constants();
	test_pm1_vs_p0();
	test_pre_id_predicate();
	printf("ob_phy_tripoint_test: 11/11 PASS\n");
	return 0;
}

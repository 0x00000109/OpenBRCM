// SPDX-License-Identifier: GPL-2.0-only
/*
 * S1: core0 -> core1 mirror block tests.
 *
 * The block is recovered from the vendor mmiotrace, not from the sub_a04c2
 * vector: seven reads of 0x07xx, then 0x173e = 0x1c00 and the seven mirror
 * writes, then a read-identity write of PHY[0x0408], then PHY[0x0417] = 0
 * and PHY[0x0416] = 1. The vendor executes the whole block twice, 11.7 ms
 * after its prologue read series and 80 ms before its LPF register block.
 *
 * Since the 2026-10-28 relocation the block is its own stage, run from
 * ob_radio_vendor_pre_id() (pre-identity), and the LPF stage no longer issues
 * it. These tests pin order, values, failure behaviour, the two passes, and
 * its absence from the LPF stage. They do not claim that the values are
 * healthy for any channel.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy.h"
#include "../../src/ob_phy_lpf.h"

#define MAX_EVENTS 128

struct event {
	char kind;
	u16 reg;
	u16 value;
};

struct fake {
	struct event events[MAX_EVENTS];
	unsigned int count;
	unsigned int total;
	int fail_read_reg;	/* -1 = none */
	int fail_write_reg;	/* -1 = none */
	u16 r739, r73a, r725, r729, r721, r728, r720, r408;
	u16 r019e;
};

static u16 fake_value(const struct fake *f, u16 reg)
{
	switch (reg) {
	case 0x0739: return f->r739;
	case 0x073a: return f->r73a;
	case 0x0725: return f->r725;
	case 0x0729: return f->r729;
	case 0x0721: return f->r721;
	case 0x0728: return f->r728;
	case 0x0720: return f->r720;
	case 0x0408: return f->r408;
	case 0x019e: return f->r019e;
	default: return 0;
	}
}

static void record(struct fake *f, char kind, u16 reg, u16 value)
{
	if (f->count < MAX_EVENTS)
		f->events[f->count++] = (struct event) {
			.kind = kind, .reg = reg, .value = value,
		};
	f->total++;
}

static int fake_read(void *ctx, u16 reg, u16 *value)
{
	struct fake *f = ctx;

	if ((int)reg == f->fail_read_reg) {
		record(f, 'r', reg, 0);
		return -5;
	}
	*value = fake_value(f, reg);
	record(f, 'R', reg, *value);
	return 0;
}

static int fake_write(void *ctx, u16 reg, u16 value)
{
	struct fake *f = ctx;

	if ((int)reg == f->fail_write_reg) {
		record(f, 'w', reg, value);
		return -5;
	}
	record(f, 'W', reg, value);
	return 0;
}

static void expect(const struct fake *f, unsigned int index, char kind,
		   u16 reg, u16 value)
{
	if (index >= f->count) {
		printf("missing event %u (expected %c 0x%04x = 0x%04x)\n",
		       index, kind, reg, value);
		assert(0);
	}
	if (f->events[index].kind != kind ||
	    f->events[index].reg != reg ||
	    f->events[index].value != value) {
		printf("event %u: got %c 0x%04x = 0x%04x, "
		       "expected %c 0x%04x = 0x%04x\n",
		       index, f->events[index].kind, f->events[index].reg,
		       f->events[index].value, kind, reg, value);
		assert(0);
	}
}

static void init_fake(struct fake *f)
{
	memset(f, 0, sizeof(*f));
	f->fail_read_reg = -1;
	f->fail_write_reg = -1;
	f->r739 = 0x0000;
	f->r73a = 0x0000;
	f->r725 = 0x1fff;
	f->r729 = 0x0000;
	f->r721 = 0xffff;
	f->r728 = 0x0000;
	f->r720 = 0x03ff;
	f->r408 = 0x0d80;
	f->r019e = 0xa540;
}

static struct ob_phy_lpf_target supported_target(void)
{
	return (struct ob_phy_lpf_target) {
		.identity_valid = true,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.d11_core_id = 0x812,
		.d11_revision = 42,
		.phy_type = 0x0b,
		.phy_revision = 1,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_revision_class = 0,
	};
}

static void test_mirror_sequence(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};

	init_fake(&f);
	assert(ob_phy_acphy_core_mirror_run(&ops, &f) == 0);
	assert(f.count == 19);
	assert(f.total == 19);

	expect(&f, 0, 'R', 0x0739, 0x0000);
	expect(&f, 1, 'R', 0x073a, 0x0000);
	expect(&f, 2, 'R', 0x0725, 0x1fff);
	expect(&f, 3, 'R', 0x0729, 0x0000);
	expect(&f, 4, 'R', 0x0721, 0xffff);
	expect(&f, 5, 'R', 0x0728, 0x0000);
	expect(&f, 6, 'R', 0x0720, 0x03ff);

	expect(&f, 7, 'W', 0x173e, 0x1c00);
	expect(&f, 8, 'W', 0x1739, 0x0000);
	expect(&f, 9, 'W', 0x173a, 0x0000);
	expect(&f, 10, 'W', 0x1725, 0x1fff);
	expect(&f, 11, 'W', 0x1729, 0x0000);
	expect(&f, 12, 'W', 0x1721, 0xffff);
	expect(&f, 13, 'W', 0x1728, 0x0000);
	expect(&f, 14, 'W', 0x1720, 0x03ff);

	expect(&f, 15, 'R', 0x0408, 0x0d80);
	expect(&f, 16, 'W', 0x0408, 0x0d80);
	expect(&f, 17, 'W', 0x0417, 0x0000);
	expect(&f, 18, 'W', 0x0416, 0x0001);
}

static void test_loopback_values_are_not_constants(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};

	init_fake(&f);
	f.r725 = 0x1234;
	f.r721 = 0xabcd;
	f.r720 = 0x0f0f;
	f.r408 = 0x0f00;
	assert(ob_phy_acphy_core_mirror_run(&ops, &f) == 0);
	expect(&f, 10, 'W', 0x1725, 0x1234);
	expect(&f, 12, 'W', 0x1721, 0xabcd);
	expect(&f, 14, 'W', 0x1720, 0x0f0f);
	expect(&f, 16, 'W', 0x0408, 0x0f00);
}

static void test_read_failure_is_fail_stop(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};

	init_fake(&f);
	f.fail_read_reg = 0x0725;
	assert(ob_phy_acphy_core_mirror_run(&ops, &f) == -5);
	assert(f.total == 3);	/* 0x739, 0x73a, failing 0x725 */
	assert(f.events[2].kind == 'r');
}

static void test_write_failure_is_fail_stop(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};

	init_fake(&f);
	f.fail_write_reg = 0x1721;
	assert(ob_phy_acphy_core_mirror_run(&ops, &f) == -5);
	/* Seven reads, then 0x173e, 0x1739, 0x173a, 0x1725, 0x1729, f0x1721. */
	assert(f.total == 13);
	assert(f.events[12].kind == 'w');
	expect(&f, 12, 'w', 0x1721, 0xffff);
}

static void test_0408_read_failure_stops_the_tail(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};

	init_fake(&f);
	f.fail_read_reg = 0x0408;
	assert(ob_phy_acphy_core_mirror_run(&ops, &f) == -5);
	/* 7 reads + 8 writes + the failing 0x0408 read; no 0x0417/0x0416. */
	assert(f.total == 16);
	assert(f.events[15].kind == 'r');
}

static void test_stage_runs_the_block_twice(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};
	unsigned int base;

	init_fake(&f);
	assert(ob_phy_acphy_core_mirror_stage_run(&ops, &f) == 0);
	assert(f.count == 38);
	assert(f.total == 38);

	/* First pass mirrors the prologue generation the vendor reads. */
	expect(&f, 0, 'R', 0x0739, 0x0000);
	expect(&f, 2, 'R', 0x0725, 0x1fff);
	expect(&f, 7, 'W', 0x173e, 0x1c00);
	expect(&f, 10, 'W', 0x1725, 0x1fff);
	expect(&f, 12, 'W', 0x1721, 0xffff);
	expect(&f, 14, 'W', 0x1720, 0x03ff);
	expect(&f, 15, 'R', 0x0408, 0x0d80);
	expect(&f, 16, 'W', 0x0408, 0x0d80);
	expect(&f, 18, 'W', 0x0416, 0x0001);

	/* Second pass, identical and immediately following. */
	base = 19;
	expect(&f, base + 0, 'R', 0x0739, 0x0000);
	expect(&f, base + 7, 'W', 0x173e, 0x1c00);
	expect(&f, base + 10, 'W', 0x1725, 0x1fff);
	expect(&f, base + 18, 'W', 0x0416, 0x0001);
}

static void test_stage_failure_stops_the_second_pass(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};

	init_fake(&f);
	f.fail_write_reg = 0x1721;
	assert(ob_phy_acphy_core_mirror_stage_run(&ops, &f) == -5);
	/* First pass stops at 0x1721; the second pass is never issued. */
	assert(f.total == 13);
}

static void test_lpf_stage_no_longer_runs_the_mirror(void)
{
	struct fake f;
	const struct ob_phy_access_ops ops = {
		.read16 = fake_read,
		.write16 = fake_write,
	};
	struct ob_phy_lpf_target target = supported_target();
	struct ob_phy_init_lifecycle lifecycle = { 0 };
	u16 saved_019e = 0;

	init_fake(&f);
	lifecycle.phase = OB_PHY_INIT_RADIO_IN_PROGRESS;
	lifecycle.radio_prefix_complete = true;

	assert(ob_phy_acphy_lpf_stage_run(&ops, &f, &target, true, 0,
					  &lifecycle, &saved_019e) == 0);
	assert(lifecycle.analog_tx_lpf_complete);
	/* 3736 pinned LPF events; the 2 x 19 mirror events are gone. */
	assert(f.total == 3736);
	/* The stage opens with the LPF prefix's first immediate write. */
	expect(&f, 0, 'W', 0x0410, 0x0077);
	expect(&f, 1, 'W', 0x173e, 0x0000);
	expect(&f, 10, 'W', 0x1728, 0x0080);
	expect(&f, 11, 'W', 0x1720, 0x0180);
}

int main(void)
{
	test_mirror_sequence();
	test_loopback_values_are_not_constants();
	test_read_failure_is_fail_stop();
	test_write_failure_is_fail_stop();
	test_0408_read_failure_stops_the_tail();
	test_stage_runs_the_block_twice();
	test_stage_failure_stops_the_second_pass();
	test_lpf_stage_no_longer_runs_the_mirror();
	printf("ob_acphy_core_mirror_test: PASS\n");
	return 0;
}

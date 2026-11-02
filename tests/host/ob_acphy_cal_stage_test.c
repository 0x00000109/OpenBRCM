// SPDX-License-Identifier: GPL-2.0-only
/*
 * M1-3 — calibration pre-state witness (S01..S09).
 *
 * The code under test is src/ob_acphy_cal_stage.c.  The backend is a
 * deterministic recorder: it issues no MMIO, and every result here is
 * HOST_EXECUTED against that recorder, not hardware evidence.
 *
 * The interface under test has no write callback, so "read-only" is enforced
 * by the type, not by an assertion.  These tests cover the mapping from the
 * recovered poll status to the stage status, the vendor-bound accounting, the
 * fail-stop paths, and the two invariants (no owned context, no calibration
 * claim).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_acphy_cal_stage.h"

#define FAKE_ERR (-71)

struct fake {
	unsigned int calls;
	unsigned int poll_reads;
	/* Number of busy 0x380 samples before the clear sample. -1 = forever. */
	int busy_reads;
	u16 busy_value;
	u16 clear_value;
	u16 reg_0401;
	u16 reg_0400;
	u16 reg_0070;
	/* Return FAKE_ERR when this register is read; 0 disables. */
	u16 fail_reg;
	/* device_lost becomes true once this many calls have run; 0 disables. */
	unsigned int lost_after;
	u32 sites[8];
	unsigned int site_count;
};

/* The vendor-bound poll can read 2001 times; keep the first eight sites. */
static void fake_record_site(struct fake *f, u32 site)
{
	if (f->site_count < 8)
		f->sites[f->site_count++] = site;
}

static int f_read16(void *ctx, u8 event, u32 source_site, u16 reg, u16 *value)
{
	struct fake *f = ctx;

	(void)event;
	f->calls++;
	fake_record_site(f, source_site);
	if (f->fail_reg && reg == f->fail_reg)
		return FAKE_ERR;
	switch (reg) {
	case OB_ACPHY_CAL_STAGE_REG_0401:
		*value = f->reg_0401;
		break;
	case OB_ACPHY_CAL_STAGE_REG_0400:
		*value = f->reg_0400;
		break;
	case OB_ACPHY_CAL_STAGE_REG_0070:
		*value = f->reg_0070;
		break;
	case OB_ACPHY_CAL_POLL_REG:
		f->poll_reads++;
		if (f->busy_reads < 0 || (int)f->poll_reads <= f->busy_reads)
			*value = f->busy_value;
		else
			*value = f->clear_value;
		break;
	default:
		*value = 0;
		break;
	}
	return 0;
}

static int f_delay(void *ctx, u8 event, u32 source_site, u16 usec)
{
	struct fake *f = ctx;

	(void)ctx;
	(void)event;
	(void)source_site;
	(void)usec;
	f->calls++;
	return 0;
}

static bool f_lost(void *ctx)
{
	struct fake *f = ctx;

	return f->lost_after && f->calls >= f->lost_after;
}

static struct ob_acphy_cal_stage_ops fake_ops(void)
{
	return (struct ob_acphy_cal_stage_ops) {
		.phy_read16 = f_read16,
		.delay_us = f_delay,
		.device_lost = f_lost,
	};
}

static struct fake fake_init(void)
{
	return (struct fake) {
		.reg_0401 = 0x1234,
		.reg_0400 = 0x0b22,
		.reg_0070 = 0x0000,
		.busy_value = 0x4000,
		.clear_value = 0x0000,
	};
}

static void assert_invariants(const struct ob_acphy_cal_stage_result *r)
{
	assert(r->entry_context_owned == false);
	assert(r->calibration_issued == false);
}

/* S01: incomplete arguments are rejected and issue no callback. */
static void test_invalid_args(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	assert(ob_acphy_cal_stage_witness_run(NULL, &f, &r) == -22);
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, NULL) == -22);
	ops.phy_read16 = NULL;
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == -22);
	ops = fake_ops();
	ops.delay_us = NULL;
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == -22);
	assert(f.calls == 0);
	printf("S01 invalid-args PASS\n");
}

/* S02: readiness already clear -> WITNESS_OK, one poll read, no delay. */
static void test_clean_entry(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(r.status == OB_ACPHY_CAL_STAGE_WITNESS_OK);
	assert(r.readiness_reads == 1);
	assert(r.readiness_delays == 0);
	assert(r.readiness_last == 0x0000);
	assert(r.readiness_exhausted == false);
	assert(r.phy_0401 == 0x1234);
	assert(r.phy_0400 == 0x0b22);
	assert(r.phy_0070 == 0x0000);
	assert_invariants(&r);
	printf("S02 clean-entry PASS\n");
}

/* S03: busy then clear -> READY_AFTER_POLL with exact read/delay counts. */
static void test_clears_after_three(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	f.busy_reads = 3;
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(r.status == OB_ACPHY_CAL_STAGE_READY_AFTER_POLL);
	assert(r.readiness_reads == 4);
	assert(r.readiness_delays == 3);
	assert(r.readiness_last == 0x0000);
	assert_invariants(&r);
	printf("S03 clears-after-three PASS\n");
}

/* S04: the finite vendor bound is reached, not extended. */
static void test_exhausted(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	f.busy_reads = -1;
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(r.status == OB_ACPHY_CAL_STAGE_READY_EXHAUSTED);
	assert(r.readiness_exhausted == true);
	assert(r.readiness_reads == OB_ACPHY_CAL_POLL_MAX_READS);
	assert(r.readiness_delays == OB_ACPHY_CAL_POLL_MAX_DELAYS);
	assert(r.readiness_last == 0x4000);
	assert_invariants(&r);
	printf("S04 exhausted PASS\n");
}

/* S05: a PHY read error stops the stage before the poll. */
static void test_read_error(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	f.fail_reg = OB_ACPHY_CAL_STAGE_REG_0400;
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(r.status == OB_ACPHY_CAL_STAGE_FAILED_CALLBACK);
	assert(r.failure_ret == FAKE_ERR);
	assert(r.readiness_reads == 0);
	assert(r.readiness_delays == 0);
	assert(r.last_site == OB_ACPHY_CAL_STAGE_SITE_READ_0400);
	assert_invariants(&r);
	printf("S05 read-error PASS\n");
}

/* S06: device loss is classified as such, never as a generic callback error. */
static void test_device_lost(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	f.lost_after = 2; /* visible after the second read */
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(r.status == OB_ACPHY_CAL_STAGE_FAILED_DEVICE_LOST);
	assert(r.readiness_reads == 0);
	assert_invariants(&r);
	printf("S06 device-lost PASS\n");
}

/* S07: the site sequence is the vendor's save/mutate order, then the poll. */
static void test_site_order(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(f.site_count == 4);
	assert(f.sites[0] == OB_ACPHY_CAL_STAGE_SITE_READ_0401);
	assert(f.sites[1] == OB_ACPHY_CAL_STAGE_SITE_READ_0400);
	assert(f.sites[2] == OB_ACPHY_CAL_STAGE_SITE_READ_0070);
	assert(f.sites[3] == OB_ACPHY_CAL_VENDOR_POLL_READ);
	assert(r.last_site == OB_ACPHY_CAL_VENDOR_POLL_READ);
	printf("S07 site-order PASS\n");
}

/* S08: only the poll's 0xc000 mask counts as busy (a 0x4000 sample is busy). */
static void test_mask(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f = fake_init();

	f.busy_reads = 0;
	f.clear_value = 0x00ff; /* outside the mask: treated as clear */
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(r.status == OB_ACPHY_CAL_STAGE_WITNESS_OK);

	f = fake_init();
	f.busy_reads = 1;
	f.clear_value = 0x8000; /* still inside the 0xc000 mask: never clears */
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert(r.status == OB_ACPHY_CAL_STAGE_READY_EXHAUSTED);
	assert(r.readiness_reads == OB_ACPHY_CAL_POLL_MAX_READS);
	assert(r.readiness_delays == OB_ACPHY_CAL_POLL_MAX_DELAYS);
	printf("S08 readiness-mask PASS\n");
}

/* S09: no result ever claims an owned entry context or a calibration issue. */
static void test_invariants_all_paths(void)
{
	struct ob_acphy_cal_stage_ops ops = fake_ops();
	struct ob_acphy_cal_stage_result r;
	struct fake f;

	f = fake_init();
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert_invariants(&r);

	f = fake_init();
	f.busy_reads = 2;
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert_invariants(&r);

	f = fake_init();
	f.busy_reads = -1;
	assert(ob_acphy_cal_stage_witness_run(&ops, &f, &r) == 0);
	assert_invariants(&r);
	printf("S09 invariants PASS\n");
}

int main(void)
{
	test_invalid_args();
	test_clean_entry();
	test_clears_after_three();
	test_exhausted();
	test_read_error();
	test_device_lost();
	test_site_order();
	test_mask();
	test_invariants_all_paths();
	printf("ob_acphy_cal_stage_test: 9/9 PASS\n");
	return 0;
}

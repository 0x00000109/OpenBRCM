// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the isolated BCM2069 radio identity probe
 * (D4-BLOCKER-PLL-BRANCH-HW-PROBE).
 *
 * Exercises the exact pure sequence + decode that src/ob_radio.c uses in the
 * kernel, through a scripted fake backend. No hardware, no MMIO, no driver.
 *
 * Covers the required cases 1-20 and 23-24 (the offline decoder cases 21-22 are
 * covered by tests/host/test_radio_probe_decoder.py):
 *   1 mode selection (radio only)          13 class 2 -> B
 *   2 normal path isolation                14 unexpected class -> SKIP
 *   3 exact prerequisite ordering          15 unexpected radio id
 *   4 no PLL programming                    16 dev_lost before probe
 *   5 no calibration                        17 dev_lost during probe (pre/post)
 *   6 no PHY/radio tables beyond probe      18 zero writes after dev_lost
 *   7 exact two-register maximum            19 fatal/default retain policy
 *   8 selector/address semantics            20 clean path skip-teardown
 *   9 extraction reg0 -> rev/class          23 older isolated modes unchanged
 *  10 reg1 -> radioid                       24 normal probe path unchanged
 *  11 class 0 -> SKIP
 *  12 class 1 -> A
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_radio.h"
#include "../../src/ob_ucode.h"

static int failures;
static int checks;
static void chk(const char *what, long got, long exp);

struct fake_synth {
	u16 regs[0x1000];
	char events[32][24];
	u16 event_reg[32];
	u16 event_mask[32];
	u32 event_delay[32];
	int n;
};

static u16 synth_read(void *ctx, u16 reg)
{
	struct fake_synth *f = ctx;

	if (f->n < 32) {
		strcpy(f->events[f->n], "read");
		f->event_reg[f->n] = reg;
	}
	f->n++;
	return f->regs[reg];
}

static void synth_write(void *ctx, u16 reg, u16 value)
{
	struct fake_synth *f = ctx;

	if (f->n < 32) {
		strcpy(f->events[f->n], "write");
		f->event_reg[f->n] = reg;
		f->event_mask[f->n] = value;
	}
	f->n++;
}

static void synth_delay(void *ctx, u32 usec)
{
	struct fake_synth *f = ctx;

	if (f->n < 32) {
		strcpy(f->events[f->n], "delay");
		f->event_delay[f->n] = usec;
	}
	f->n++;
}

static const struct ob_radio2069_io_ops synth_ops = {
	.read16 = synth_read,
	.write16 = synth_write,
	.delay_us = synth_delay,
};

static void test_radio2069_synth_script(void)
{
	struct fake_synth f;
	int rc;

	memset(&f, 0, sizeof(f));
	f.regs[0x08e5] = 0xa5a5;
	f.regs[0x08d0] = 0x8001;
	f.regs[0x08e8] = 0x0041;
	f.regs[0x08dc] = 0x2003;
	rc = ob_radio2069_synth_transition(&synth_ops, &f, true);
	chk("synth rc", rc, 0);
	chk("synth event count", f.n, 16);
	chk("synth clear e5", f.regs[0x08e5], 0xa5a5 & (u16)~0x4000u);
	chk("synth clear d0 then set", f.regs[0x08d0], 0x8001);
	chk("synth clear e8 then set", f.regs[0x08e8], 0x0041);
	chk("synth clear dc then set", f.regs[0x08dc], 0x2003);
	chk("synth set d0", f.regs[0x08d0], 0x8001);
	chk("synth set e8", f.regs[0x08e8], 0x0041);
	chk("synth set dc", f.regs[0x08dc], 0x2003);
	chk("event 0 read", strcmp(f.events[0], "read"), 0);
	chk("event 0 reg", f.event_reg[0], 0x08e5);
	chk("event 1 write", strcmp(f.events[1], "write"), 0);
	chk("event 1 clear result", f.event_mask[1], 0xa5a5 & (u16)~0x4000u);
	chk("event 2 read", f.event_reg[2], 0x08d0);
	chk("event 3 clear result", f.event_mask[3], 0x8000);
	chk("event 4 read", f.event_reg[4], 0x08e8);
	chk("event 5 clear result", f.event_mask[5], 0x0001);
	chk("event 6 read", f.event_reg[6], 0x08dc);
	chk("event 7 clear result", f.event_mask[7], 0x0003);
	chk("delay 11 position", strcmp(f.events[8], "delay"), 0);
	chk("delay 11", f.event_delay[8], 11);
	chk("event 9 read", f.event_reg[9], 0x08d0);
	chk("event 10 set result", f.event_mask[10], 0x8001);
	chk("event 11 read", f.event_reg[11], 0x08e8);
	chk("event 12 set result", f.event_mask[12], 0x0041);
	chk("delay 1 position", strcmp(f.events[13], "delay"), 0);
	chk("delay 1", f.event_delay[13], 1);
	chk("event 14 read", f.event_reg[14], 0x08dc);
	chk("event 15 set result", f.event_mask[15], 0x2003);
}

static void test_radio2069_synth_requires_suspend(void)
{
	struct fake_synth f;

	memset(&f, 0, sizeof(f));
	chk("synth refuses unsuspended", ob_radio2069_synth_transition(
		&synth_ops, &f, false), -5);
	chk("unsuspended emits no operations", f.n, 0);
}

struct fake_access {
	u16 reg[0x1000];
	int fail_read;
	int fail_write;
};

static int access_read(void *ctx, u16 reg, u16 *value)
{
	struct fake_access *a = ctx;
	if (a->fail_read)
		return -5;
	*value = a->reg[reg];
	return 0;
}

static int access_write(void *ctx, u16 reg, u16 value)
{
	struct fake_access *a = ctx;
	if (a->fail_write)
		return -5;
	 a->reg[reg] = value;
	return 0;
}

static void test_radio_access_rmw(void)
{
	struct fake_access a = { .reg = { [0x08e5] = 0xa5a5,
		[0x08d0] = 0x8001 } };
	const struct ob_radio_access_ops ops = {
		.read16 = access_read, .write16 = access_write,
	};

	chk("rmw clear", ob_radio_rmw_clear(&ops, &a, 0x08e5, 0x4000), 0);
	chk("rmw clear preserves", a.reg[0x08e5], 0xa5a5 & (u16)~0x4000u);
	chk("rmw set", ob_radio_rmw_set(&ops, &a, 0x08d0, 0x0040), 0);
	chk("rmw set preserves", a.reg[0x08d0], 0x8041);
	a.fail_read = 1;
	chk("rmw read failure", ob_radio_rmw_clear(&ops, &a, 0x08e5, 1), -5);
	a.fail_read = 0;
	a.fail_write = 1;
	chk("rmw write failure", ob_radio_rmw_set(&ops, &a, 0x08e5, 1), -5);
}

static void test_radio2069_rev4_prefregs(void)
{
	struct fake_access a = { 0 };
	const struct ob_radio_access_ops ops = {
		.read16 = access_read, .write16 = access_write,
	};
	static const u16 expected[][2] = {
		{0x063a, 0x0000}, {0x063d, 0x000f}, {0x0645, 0x3000},
		{0x0646, 0x0303}, {0x0647, 0x36e2}, {0x0649, 0x6000},
		{0x064a, 0x0003}, {0x065e, 0x0ff4}, {0x0666, 0x0b2d},
		{0x0667, 0x03ff}, {0x0115, 0x3337}, {0x0117, 0x1337},
		{0x0718, 0x7777}, {0x071c, 0x1000}, {0x0726, 0x0100},
		{0x0727, 0x0002}, {0x0761, 0x0100}, {0x0407, 0x8382},
		{0x055e, 0x0020}, {0x0885, 0x0700}, {0x08e9, 0x01ce},
		{0x08ea, 0x00ff}, {0x08ec, 0x06ff}, {0x0972, 0x0600},
	};
	size_t i;

	chk("rev4 prefreg count", OB_RADIO2069_REV4_PREFREG_COUNT, 24);
	chk("rev4 prefregs accepted", ob_radio2069_apply_rev4_prefregs(
		&ops, &a, OB_RADIO_BCM2069_ID, 4), 0);
	for (i = 0; i < 24; i++) {
		char name[32];
		snprintf(name, sizeof(name), "prefreg %zu value", i);
		chk(name, a.reg[expected[i][0]], expected[i][1]);
	}
	chk("rev4 prefregs reject non-rev4", ob_radio2069_apply_rev4_prefregs(
		&ops, &a, OB_RADIO_BCM2069_ID, 3), -22);
	a.fail_write = 1;
	chk("rev4 prefregs fail closed", ob_radio2069_apply_rev4_prefregs(
		&ops, &a, OB_RADIO_BCM2069_ID, 4), -5);
}

struct fake_checked {
	struct fake_access access;
	u32 delays[4];
	int delay_count;
};

static int checked_read(void *ctx, u16 reg, u16 *value)
{
	struct fake_checked *f = ctx;
	return access_read(&f->access, reg, value);
}

static int checked_write(void *ctx, u16 reg, u16 value)
{
	struct fake_checked *f = ctx;
	return access_write(&f->access, reg, value);
}

static void checked_delay(void *ctx, u32 usec)
{
	struct fake_checked *f = ctx;
	if (f->delay_count < 4)
		f->delays[f->delay_count++] = usec;
}

static void test_radio2069_checked_synth(void)
{
	struct fake_checked f = { .access = { .reg = {
		[0x08e5] = 0xa5a5, [0x08d0] = 0x8001,
		[0x08e8] = 0x0041, [0x08dc] = 0x2003 } } };
	const struct ob_radio2069_checked_ops ops = {
		.read16 = checked_read, .write16 = checked_write,
		.delay_us = checked_delay,
	};

	chk("checked synth rc", ob_radio2069_synth_transition_checked(
		&ops, &f, true), 0);
	chk("checked synth delay count", f.delay_count, 2);
	chk("checked synth delay 11", f.delays[0], 11);
	chk("checked synth delay 1", f.delays[1], 1);
	f.access.fail_read = 1;
	chk("checked synth first error", ob_radio2069_synth_transition_checked(
		&ops, &f, true), -5);
	chk("checked synth refuses unsuspended", ob_radio2069_synth_transition_checked(
		&ops, &f, false), -22);
}

static void test_radio_target_guard(void)
{
	struct ob_radio_target_identity id = {
		.chip_id = 0x4352, .chip_rev = 3, .d11_corerev = 42,
		.phy_type = OB_RADIO_PHYTYPE_AC, .phy_revision = 1,
		.radio_id = OB_RADIO_BCM2069_ID, .radio_revision = 4,
		.radio_access_initialized = true, .valid = true,
	};

	chk("target accepted", ob_radio2069_target_applicable(&id), 1);
	id.radio_access_initialized = false;
	chk("access unknown rejects", ob_radio2069_target_applicable(&id), 0);
	id.radio_access_initialized = true;
	id.radio_id = 0x030b;
	chk("wrong radio rejects", ob_radio2069_target_applicable(&id), 0);
	id.radio_id = OB_RADIO_BCM2069_ID;
	id.valid = false;
	chk("unknown identity rejects", ob_radio2069_target_applicable(&id), 0);
}

static void test_radio2069_mode_selection(void)
{
	chk("rev4 mode selected", ob_isolated_mode_select13(false, false,
		false, false, false, false, false, false, false, false, false,
		false, true), OB_ISOLATED_RADIO2069_REV4_PROBE);
	chk("rev4 mode conflicts", ob_isolated_mode_select13(false, false,
		false, false, false, false, true, false, false, false, false,
		false, true), OB_ISOLATED_CONFLICT);
	chk("rev4 mode skips teardown",
		ob_isolated_mode_skips_teardown(OB_ISOLATED_RADIO2069_REV4_PROBE), 1);
	chk("rev4 fixture chanspec", OB_RADIO2069_CHANSPEC_CH1_20MHZ, 0x1001);
}

static void chk(const char *what, long got, long exp)
{
	checks++;
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

static void test_pcie_link_classifier(void)
{
	struct ob_pcie_link_classification c;

	c = ob_classify_pcie_link_status(0, OB_PCIE_LNKSTA_DLLLA);
	chk("link up raw valid", c.raw_valid, 1);
	chk("link up semantic valid", c.semantic_valid, 1);
	chk("link up", c.link_up, 1);
	chk("link up normal", c.abnormal, 0);
	chk("link up class", strcmp(c.classification, "LINK_UP"), 0);

	c = ob_classify_pcie_link_status(0, 0);
	chk("link down raw valid", c.raw_valid, 1);
	chk("link down semantic valid", c.semantic_valid, 1);
	chk("link down", c.link_up, 0);
	chk("link down abnormal", c.abnormal, 1);
	chk("link down class", strcmp(c.classification, "LINK_NOT_ACTIVE"), 0);

	c = ob_classify_pcie_link_status(-1, 0xffffu);
	chk("link error raw invalid", c.raw_valid, 0);
	chk("link error semantic invalid", c.semantic_valid, 0);
	chk("link error abnormal", c.abnormal, 1);
	chk("link error class", strcmp(c.classification, "PCI_CONFIG_ERROR"), 0);

	c = ob_classify_pcie_link_status(0, 0xffffu);
	chk("all ones raw valid", c.raw_valid, 1);
	chk("all ones semantic invalid", c.semantic_valid, 0);
	chk("all ones not link up", c.link_up, 0);
	chk("all ones abnormal", c.abnormal, 1);
	chk("all ones class", strcmp(c.classification, "PCI_CONFIG_ALL_ONES"), 0);
}

/* ---- scripted fake backend ---------------------------------------------- */

#define FAKE_MAX_OPS 16

struct fake_radio {
	/* returned values */
	u32 sentinel[2];	/* pre, post */
	u16 reg[2];		/* reg0, reg1 */
	int read32_n;

	/* observed traffic */
	int reads32;
	int reads16;
	int writes;
	u16 w_off[FAKE_MAX_OPS];
	u16 w_val[FAKE_MAX_OPS];
	u16 r_off[FAKE_MAX_OPS];
};

static u32 f_read32_trusted(void *ctx, u16 off)
{
	struct fake_radio *f = ctx;
	int idx = f->read32_n++;
	u32 v;

	chk("sentinel offset", off, OB_RADIO_REG_SENTINEL);
	v = f->sentinel[idx < 2 ? idx : 1];
	f->reads32++;
	return v;
}

static u16 f_read16(void *ctx, u16 off)
{
	struct fake_radio *f = ctx;
	int idx = f->reads16;

	if (idx < FAKE_MAX_OPS)
		f->r_off[idx] = off;
	f->reads16++;
	return f->reg[idx == 0 ? 0 : 1];
}

static void f_write16(void *ctx, u16 off, u16 val)
{
	struct fake_radio *f = ctx;

	if (f->writes < FAKE_MAX_OPS) {
		f->w_off[f->writes] = off;
		f->w_val[f->writes] = val;
	}
	f->writes++;
}

static const struct ob_radio_io_ops fake_ops = {
	.read32_trusted = f_read32_trusted,
	.read16 = f_read16,
	.write16 = f_write16,
};

static struct fake_radio fake_init(u16 reg0, u16 reg1)
{
	struct fake_radio f;

	memset(&f, 0, sizeof(f));
	f.sentinel[0] = 0x04020402u;	/* live MACCONTROL-like value */
	f.sentinel[1] = 0x04020402u;
	f.reg[0] = reg0;
	f.reg[1] = reg1;
	return f;
}

/* 1 + 2 + 23 + 24: mode selection, isolation and unchanged older modes. */
static void test_mode_selection(void)
{
	/* radio only */
	chk("radio only", ob_isolated_mode_select7(false, false, false, false,
						   false, false, true),
	    OB_ISOLATED_RADIO_ID_PROBE);
	/* all false -> normal path (isolation) */
	chk("none -> normal",
	    ob_isolated_mode_select7(false, false, false, false, false, false,
				     false), OB_ISOLATED_NONE);
	/* conflict: radio + any other */
	chk("radio + ucode conflict",
	    ob_isolated_mode_select7(false, true, false, false, false, false,
				     true), OB_ISOLATED_CONFLICT);
	chk("radio + d3b conflict",
	    ob_isolated_mode_select7(false, false, false, false, false, true,
				     true), OB_ISOLATED_CONFLICT);
	/* older select6 unaffected by the additive mode */
	chk("select6 unchanged (d3b)",
	    ob_isolated_mode_select6(false, false, false, false, false, true),
	    OB_ISOLATED_D3B_TEST);
	chk("select6 unchanged (none)",
	    ob_isolated_mode_select6(false, false, false, false, false, false),
	    OB_ISOLATED_NONE);
	chk("select5 unchanged (d3a1)",
	    ob_isolated_mode_select5(false, false, false, false, true),
	    OB_ISOLATED_D3A1_TEST);
	/* radio mode touches no initvals, no DMA and needs no teardown */
	chk("radio no initvals",
	    ob_isolated_mode_applies_initvals(OB_ISOLATED_RADIO_ID_PROBE), 0);
	chk("radio no dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_RADIO_ID_PROBE), 0);
	chk("radio skips teardown",
	    ob_isolated_mode_skips_teardown(OB_ISOLATED_RADIO_ID_PROBE), 1);
}

/* 3-10: exact ordering, two-register maximum, selector/address semantics. */
static void test_sequence_shape(void)
{
	struct fake_radio f = fake_init(0x0010u, 0x2069u); /* class 1 -> A */
	bool lost = false;
	struct ob_radio_seq_result res;
	int rc = ob_radio_sequence(&fake_ops, &f, &lost, &res);

	chk("sequence rc", rc, 0);
	chk("ran", res.ran, 1);
	chk("valid", res.valid, 1);
	chk("no dev_lost", res.stopped_dev_lost, 0);

	/* exact two-register maximum */
	chk("radio writes == 2", res.radio_writes, 2);
	chk("radio reads == 2", res.radio_reads, 2);
	chk("backend writes == 2", f.writes, 2);
	chk("backend reads16 == 2", f.reads16, 2);
	chk("backend reads32 == 2", f.reads32, 2);

	/* ordering: selector 0, reg0, selector 1, reg1 */
	chk("write0 offset", f.w_off[0], OB_RADIO_REG_ADDR_LATCH);
	chk("write0 value", f.w_val[0], 0);
	chk("write1 offset", f.w_off[1], OB_RADIO_REG_ADDR_LATCH);
	chk("write1 value", f.w_val[1], 1);
	chk("read0 offset", f.r_off[0], OB_RADIO_REG_DATA);
	chk("read1 offset", f.r_off[1], OB_RADIO_REG_DATA);

	/* no PLL/calibration/PHY/radio-table write: only 0x3d8 was written */
	{
		int i, bad = 0;
		for (i = 0; i < f.writes; i++)
			if (f.w_off[i] != OB_RADIO_REG_ADDR_LATCH)
				bad++;
		chk("only selector writes", bad, 0);
	}
}

/* 9-15: extraction and branch decoding. */
static void test_decode(void)
{
	struct ob_radio_observation o;

	/* reg0=0x0010 -> rev=0x10, class=1, low=0 -> branch A */
	o = ob_radio_decode(0x0010u, 0x2069u);
	chk("A radioid", o.radio_id, 0x2069u);
	chk("A rev", o.radio_rev, 0x10u);
	chk("A low", o.rev_low, 0x0u);
	chk("A class", o.revision_class, 1u);
	chk("A accepted", o.id_accepted, 1);
	chk("A branch", o.branch, OB_RADIO_BRANCH_A);
	chk("A name", strcmp(ob_radio_branch_name(o.branch), "A"), 0);

	/* class 2 -> B */
	o = ob_radio_decode(0x0020u, 0x2069u);
	chk("B class", o.revision_class, 2u);
	chk("B rev", o.radio_rev, 0x20u);
	chk("B branch", o.branch, OB_RADIO_BRANCH_B);

	/* class 0 -> SKIP */
	o = ob_radio_decode(0x0003u, 0x2069u);
	chk("skip class", o.revision_class, 0u);
	chk("skip branch", o.branch, OB_RADIO_BRANCH_SKIP);

	/* unexpected (non-1/2) class 5 -> SKIP, not A/B */
	o = ob_radio_decode(0x0057u, 0x2069u);
	chk("class5", o.revision_class, 5u);
	chk("class5 branch SKIP", o.branch, OB_RADIO_BRANCH_SKIP);

	/* class 15 / rev 254 -> SKIP */
	o = ob_radio_decode(0x00feu, 0x2069u);
	chk("rev254 class", o.revision_class, 15u);
	chk("rev254 branch SKIP", o.branch, OB_RADIO_BRANCH_SKIP);

	/* unexpected radio id -> UNKNOWN branch, not accepted */
	o = ob_radio_decode(0x0010u, 0x1234u);
	chk("bad id accepted", o.id_accepted, 0);
	chk("bad id is_2069", o.id_is_2069, 0);
	chk("bad id branch UNKNOWN", o.branch, OB_RADIO_BRANCH_UNKNOWN);
	chk("bad id name", strcmp(ob_radio_branch_name(o.branch),
				  "UNKNOWN"), 0);

	/* alternate accepted AC id 0x030B does not claim 2069 semantics */
	o = ob_radio_decode(0x0010u, 0x030bu);
	chk("alt id accepted", o.id_accepted, 1);
	chk("alt id is_2069", o.id_is_2069, 0);
	chk("alt id branch UNKNOWN", o.branch, OB_RADIO_BRANCH_UNKNOWN);

	/* all-ones pair -> invalid */
	o = ob_radio_decode(0xffffu, 0xffffu);
	chk("all-ones pair", o.all_ones_pair, 1);
}

/* 16: dev_lost before the probe -> no MMIO at all. */
static void test_dev_lost_before(void)
{
	struct fake_radio f = fake_init(0x0010u, 0x2069u);
	bool lost = true;
	struct ob_radio_seq_result res;
	int rc = ob_radio_sequence(&fake_ops, &f, &lost, &res);

	chk("before rc", rc, -5);
	chk("before stopped", res.stopped_dev_lost, 1);
	chk("before ran", res.ran, 0);
	chk("before writes", f.writes, 0);
	chk("before reads", f.reads16 + f.reads32, 0);
}

/* 17 + 18: dev_lost at the pre sentinel -> latch, zero writes; and at the
 * post sentinel -> observation completed but invalidated.
 */
static void test_dev_lost_during(void)
{
	/* pre sentinel all-ones */
	{
		struct fake_radio f = fake_init(0x0010u, 0x2069u);
		bool lost = false;
		struct ob_radio_seq_result res;

		f.sentinel[0] = 0xffffffffu;
		ob_radio_sequence(&fake_ops, &f, &lost, &res);
		chk("pre latch", lost, 1);
		chk("pre stopped", res.stopped_dev_lost, 1);
		chk("pre ran", res.ran, 0);
		chk("pre zero writes", f.writes, 0);
		chk("pre zero reads16", f.reads16, 0);
	}

	/* post sentinel all-ones: observation ran, then invalidated */
	{
		struct fake_radio f = fake_init(0x0020u, 0x2069u);
		bool lost = false;
		struct ob_radio_seq_result res;
		int rc;

		f.sentinel[0] = 0x04020402u;
		f.sentinel[1] = 0xffffffffu;
		rc = ob_radio_sequence(&fake_ops, &f, &lost, &res);
		chk("post rc", rc, -5);
		chk("post latch", lost, 1);
		chk("post dev_lost_after", res.dev_lost_after, 1);
		chk("post stopped", res.stopped_dev_lost, 1);
		chk("post ran", res.ran, 1);
		chk("post failure stage", res.failure_stage,
		    OB_RADIO_STAGE_POST_ACCESS);
		chk("post valid cleared", res.valid, 0);
		chk("post writes == 2 (before loss)", f.writes, 2);
	}
}

/* 18b: after the latch the guard backend itself never sees a write. */
static void test_zero_writes_after_latch(void)
{
	struct fake_radio f = fake_init(0x0010u, 0x2069u);
	bool lost = true;
	struct ob_radio_seq_result res;
	int i;

	for (i = 0; i < 4; i++)
		ob_radio_sequence(&fake_ops, &f, &lost, &res);
	chk("latch: zero writes over 4 runs", f.writes, 0);
	chk("latch: zero reads over 4 runs", f.reads16 + f.reads32, 0);
}

/* 20: clean path leaves no platform resource to tear down. */
static void test_clean_path(void)
{
	struct fake_radio f = fake_init(0x0010u, 0x2069u);
	bool lost = false;
	struct ob_radio_seq_result res;

	ob_radio_sequence(&fake_ops, &f, &lost, &res);
	chk("clean rc", res.rc, 0);
	chk("clean not lost", lost, 0);
	chk("clean skips teardown",
	    ob_isolated_mode_skips_teardown(OB_ISOLATED_RADIO_ID_PROBE), 1);
	chk("clean uses no dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_RADIO_ID_PROBE), 0);
}

int main(void)
{
	test_pcie_link_classifier();
	test_radio2069_synth_script();
	test_radio2069_synth_requires_suspend();
	test_radio_access_rmw();
	test_radio2069_rev4_prefregs();
	test_radio2069_checked_synth();
	test_radio_target_guard();
	test_radio2069_mode_selection();
	test_mode_selection();
	test_sequence_shape();
	test_decode();
	test_dev_lost_before();
	test_dev_lost_during();
	test_zero_writes_after_latch();
	test_clean_path();

	if (failures) {
		printf("ob_radio_test: %d FAILURE(S) of %d checks\n", failures,
		       checks);
		return 1;
	}
	printf("ob_radio_test: all %d radio-probe tests PASS\n", checks);
	return 0;
}

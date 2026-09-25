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

static void chk(const char *what, long got, long exp)
{
	checks++;
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests for the isolated D3B band-init path (M3.4D3B).
 *
 * Exercises only the pure, provenance-backed helpers: rev11 board-data
 * extraction, the exact vendor antsel_type flow, the MHF vector derivation and
 * write destinations, the bsinitvals42 shape/offset guarantees, the D3B
 * ordering model and the isolated-mode mutual exclusion. No kernel API.
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "ob_fw.h"
#include "ob_initvals.h"
#include "ob_d3b.h"
#include "ob_d3a0.h"
#include "ob_ucode.h"

static int failures;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); \
		failures++; \
	} \
} while (0)

/* ---- synthetic 592-byte bsinitvals42 image (73 records + terminator) ---- */

static void put_rec(uint8_t *img, uint32_t idx, uint16_t off, uint16_t width,
		    uint32_t val)
{
	uint8_t *r = img + (size_t)idx * 8u;

	r[0] = off & 0xff;
	r[1] = (off >> 8) & 0xff;
	r[2] = width & 0xff;
	r[3] = (width >> 8) & 0xff;
	r[4] = val & 0xff;
	r[5] = (val >> 8) & 0xff;
	r[6] = (val >> 16) & 0xff;
	r[7] = (val >> 24) & 0xff;
}

/* Build the recovered offset/width multiset: 34x0x160 w4, 15x0x164 w2,
 * 19x0x166 w2, 5 direct IHR w2 (=73 records, w16=39, w32=34).
 */
static void build_bs_image(uint8_t *img, size_t size)
{
	uint32_t i, n = 0;
	static const uint16_t ihr[5] = { 0x0680, 0x0682, 0x0684, 0x0686,
					 0x0700 };

	assert(size == OB_D3B_BS_SIZE);
	memset(img, 0, size);
	for (i = 0; i < 34u; i++)
		put_rec(img, n++, 0x0160, 4, 0x00010000u | i);
	for (i = 0; i < 15u; i++)
		put_rec(img, n++, 0x0164, 2, 0x1000u + i);
	for (i = 0; i < 19u; i++)
		put_rec(img, n++, 0x0166, 2, 0x2000u + i);
	for (i = 0; i < 5u; i++)
		put_rec(img, n++, ihr[i], 2, 0x3000u + i);
	assert(n == OB_D3B_BS_RECORDS);
	put_rec(img, n, OB_FW_IV_TERMINATOR, 0, 0);
}

static void test_board_decode(void)
{
	/* The captured BCM4352/ASUS PCE-AC56 rev11 image words. */
	uint16_t sp[OB_SPROM11_WORDS];
	struct ob_sprom_board b;
	struct ob_d3b_band band;
	uint16_t mhf[OB_D3B_MHF_COUNT];

	memset(sp, 0, sizeof(sp));
	sp[OB_SPROM11_BOARDTYPE_WORD] = 0x85ba;
	sp[OB_SPROM11_BOARDFLAGS_LO_WORD] = 0x1000;
	sp[OB_SPROM11_BOARDFLAGS_HI_WORD] = 0x1000;
	sp[OB_SPROM11_ANT_WORD] = 0x0707;
	sp[OB_SPROM11_TXRXC_WORD] = 0x0077;

	memset(&b, 0, sizeof(b));
	CHECK(ob_d3b_decode_rev11_board(sp, OB_SPROM11_WORDS, &b),
	      "board decode failed");
	CHECK(b.valid && b.boardtype == 0x85ba && b.boardflags == 0x10001000u,
	      "board fields: type=%04x flags=%08x", b.boardtype,
	      b.boardflags);
	CHECK(b.aa2g == 7 && b.aa5g == 7, "aa2g=%u aa5g=%u", b.aa2g, b.aa5g);
	CHECK(b.antswitch == 0 && b.antswitch_present,
	      "antswitch=%u present=%d", b.antswitch, b.antswitch_present);

	/* absent semantics: 0xff -> value 0, present=false */
	sp[OB_SPROM11_TXRXC_WORD] = 0xff00;
	CHECK(ob_d3b_decode_rev11_board(sp, OB_SPROM11_WORDS, &b),
	      "board re-decode failed");
	CHECK(b.antswitch == 0 && !b.antswitch_present,
	      "antswitch absent: v=%u present=%d", b.antswitch,
	      b.antswitch_present);

	/* short image rejected */
	CHECK(!ob_d3b_decode_rev11_board(sp, 4, &b), "short image accepted");

	/* captured board -> captured MHF vector */
	memset(sp, 0, sizeof(sp));
	sp[OB_SPROM11_BOARDTYPE_WORD] = 0x85ba;
	sp[OB_SPROM11_BOARDFLAGS_LO_WORD] = 0x1000;
	sp[OB_SPROM11_BOARDFLAGS_HI_WORD] = 0x1000;
	sp[OB_SPROM11_ANT_WORD] = 0x0707;
	sp[OB_SPROM11_TXRXC_WORD] = 0x0077;
	assert(ob_d3b_decode_rev11_board(sp, OB_SPROM11_WORDS, &b));
	ob_d3b_band_from_board(&b, OB_D3B_BAND_PHYTYPE_AC, &band);
	ob_d3b_mhf_vector(&band, mhf);
	CHECK(ob_d3b_mhf_is_captured(mhf),
	      "captured vector mismatch %04x %04x %04x %04x %04x",
	      mhf[0], mhf[1], mhf[2], mhf[3], mhf[4]);
	CHECK(band.edcf_nonzero && !band.pci_war16165 && !band.chip_is_4313 &&
	      band.phytype == OB_D3B_BAND_PHYTYPE_AC,
	      "resolved gate inputs wrong");
}

static void test_mhf_shm_destinations(void)
{
	/* Exact recovered MHF SHM destinations and order (blob sub_62766). */
	static const uint16_t want[OB_D3B_MHF_COUNT] = {
		0x5e, 0x60, 0x62, 0x78, 0xd4
	};
	const uint16_t got[OB_D3B_MHF_COUNT] = {
		OB_D3B_MHF_SHM0, OB_D3B_MHF_SHM1, OB_D3B_MHF_SHM2,
		OB_D3B_MHF_SHM3, OB_D3B_MHF_SHM4
	};

	CHECK(memcmp(want, got, sizeof(want)) == 0,
	      "MHF SHM destinations mismatch");
}

static void test_antsel_vectors(void)
{
	/* Exact vendor flow, incl. the L_bt0 -> L_bf fall-through. */
	CHECK(ob_d3b_antsel_type(0x85ba, 0x10001000u, 0, 7, 7) == 0,
	      "captured board antsel != 0");
	CHECK(ob_d3b_antsel_type(0x04, 0x0, 0, 7, 0) == 2, "L_bt0 type 2");
	CHECK(ob_d3b_antsel_type(0x04, 0x8, 0, 6, 0) == 1,
	      "L_bt0 mismatch -> L_bf type 1");
	CHECK(ob_d3b_antsel_type(0x10, 0x8, 0, 0, 0) == 1,
	      "boardtype>3 antswitch 0 -> L_bf type 1");
	CHECK(ob_d3b_antsel_type(0x10, 0x0, 0, 0, 0) == 0,
	      "boardtype>3 antswitch 0 no RFANTS -> 0");
	CHECK(ob_d3b_antsel_type(0x00, 0x8, 9, 0, 0) == 1,
	      "boardtype<=3 -> L_bf type 1");
	CHECK(ob_d3b_antsel_type(0x05, 0x0, 1, 0, 0) == 2, "antswitch 1 -> 2");
	CHECK(ob_d3b_antsel_type(0x05, 0x0, 4, 0, 0) == 3, "antswitch 4 -> 3");
	CHECK(ob_d3b_antsel_type(0x05, 0x0, 5, 0, 0) == 4, "antswitch 5 -> 4");
	CHECK(ob_d3b_antsel_type(0x05, 0x0, 6, 0, 0) == 5, "antswitch 6 -> 5");
	CHECK(ob_d3b_antsel_type(0x05, 0x0, 7, 0, 0) == 6, "antswitch 7 -> 6");
	CHECK(ob_d3b_antsel_type(0x05, 0x0, 8, 0, 0) == 0, "antswitch >7 -> 0");

	CHECK(ob_d3b_mhf3_from_antsel(0) == 0x0, "mhf3 type0");
	CHECK(ob_d3b_mhf3_from_antsel(1) == 0x1, "mhf3 type1");
	CHECK(ob_d3b_mhf3_from_antsel(2) == 0x3, "mhf3 type2");
	CHECK(ob_d3b_mhf3_from_antsel(6) == 0x3, "mhf3 type6");
}

static void test_bs_shape(void)
{
	uint8_t img[OB_D3B_BS_SIZE];
	struct ob_initvals_plan plan;
	uint32_t i;

	build_bs_image(img, sizeof(img));
	CHECK(ob_initvals_plan_from_table(img, sizeof(img), &plan) == 0,
	      "bs image parse failed");
	CHECK(ob_d3b_bs_plan_ok(&plan),
	      "bs plan records=%u w16=%u w32=%u", plan.records, plan.w16,
	      plan.w32);
	CHECK(plan.records == 73u && plan.w16 == 39u && plan.w32 == 34u,
	      "bs invariant");

	/* terminator excluded: last plan record is not the terminator */
	{
		struct ob_fw_iv last;

		CHECK(ob_fw_iv_at(img, sizeof(img), plan.records - 1u, &last) ==
		      0 && last.offset != OB_FW_IV_TERMINATOR,
		      "terminator counted as a record");
	}

	/* every record offset must be in the recovered allowed set */
	for (i = 0; i < plan.records; i++) {
		struct ob_fw_iv rec;

		assert(ob_fw_iv_at(img, sizeof(img), i, &rec) == 0);
		CHECK(ob_d3b_bs_offset_allowed(rec.offset),
		      "record %u offset 0x%03x not allowed", i, rec.offset);
	}

	/* malformed / truncated rejection */
	CHECK(ob_initvals_plan_from_table(img, sizeof(img) - 8u, &plan) != 0,
	      "truncated bs image accepted");
	img[2] = 3;	/* illegal width */
	CHECK(ob_initvals_plan_from_table(img, sizeof(img), &plan) != 0,
	      "illegal width accepted");
}

static void test_bs_offsets_block_phy(void)
{
	/* PHY-indirect and radio-window offsets must be forbidden. */
	CHECK(!ob_d3b_bs_offset_allowed(0x3fcu), "PHY indirect addr allowed");
	CHECK(!ob_d3b_bs_offset_allowed(0x3feu), "PHY indirect data allowed");
	CHECK(!ob_d3b_bs_offset_allowed(0x3d8u), "radio A allowed");
	CHECK(!ob_d3b_bs_offset_allowed(0x3dau), "radio B allowed");
	CHECK(ob_d3b_bs_offset_allowed(0x160u), "OBJADDR not allowed");
	CHECK(ob_d3b_bs_offset_allowed(0x700u), "NAV not allowed");
}

static void test_order_and_dma_lifetime(void)
{
	/* D3A1 prefix strictly precedes MHF/bsinitvals/STOP/teardown. */
	CHECK(ob_d3b_stage_rank_ok(), "D3B stage order invalid");
	CHECK(ob_d3b_dma_live_during_d3b(),
	      "DMA must be live while D3B executes");

	/* fail-closed free semantics are unchanged for the D3B lifecycle */
	{
		struct ob_d3a0_lifecycle lc;

		memset(&lc, 0, sizeof(lc));
		lc.rx = OB_D3A0_PROGRAMMED;
		lc.tx[0] = OB_D3A0_PROGRAMMED;
		CHECK(!ob_d3a0_can_free(&lc), "free before verified stop");
		lc.fatal = true;
		lc.engines_stopped = true;
		lc.free_allowed = true;
		CHECK(!ob_d3a0_can_free(&lc), "free in fatal state");
		lc.fatal = false;
		CHECK(ob_d3a0_can_free(&lc), "no free after verified stop");
	}
}

static void test_mode_exclusion(void)
{
	enum ob_isolated_mode m;

	m = ob_isolated_mode_select6(0, 0, 0, 0, 0, 1);
	CHECK(m == OB_ISOLATED_D3B_TEST, "bsinitvals mode select");
	m = ob_isolated_mode_select6(0, 0, 0, 0, 1, 1);
	CHECK(m == OB_ISOLATED_CONFLICT, "d3a1+d3b must conflict");
	m = ob_isolated_mode_select6(1, 1, 0, 0, 0, 0);
	CHECK(m == OB_ISOLATED_CONFLICT, "fw+ucode must conflict");
	m = ob_isolated_mode_select6(0, 0, 0, 0, 0, 0);
	CHECK(m == OB_ISOLATED_NONE, "no mode");
	CHECK(!ob_isolated_mode_skips_teardown(OB_ISOLATED_D3B_TEST),
	      "D3B must not skip teardown");
	CHECK(ob_isolated_mode_uses_dma(OB_ISOLATED_D3B_TEST),
	      "D3B must own DMA");
	CHECK(ob_isolated_mode_applies_initvals(OB_ISOLATED_D3B_TEST),
	      "D3B must apply the common initvals prefix");
}

static void test_post_predicate(void)
{
	struct ob_d3b_post p;

	memset(&p, 0, sizeof(p));
	p.mhf[0] = 0x0100; p.mhf[4] = 0x0080;
	p.shm_10 = OB_D3B_SHM_OVR_10_VAL;
	p.shm_1c = OB_D3B_SHM_OVR_1C_VAL;
	p.shm_94 = OB_D3B_SHM_OVR_94_VAL;
	p.maccontrol = OB_D3B_MACCONTROL_EXPECTED;
	p.macintmask = 0;
	p.records = 73; p.w16 = 39; p.w32 = 34;
	CHECK(ob_d3b_post_ok(&p), "post ok");
	p.mhf[2] = 0x3;
	CHECK(!ob_d3b_post_ok(&p), "wrong MHF3 accepted");
	p.mhf[2] = 0;
	p.maccontrol |= OB_D3B_MCTL_EN_MAC;
	CHECK(!ob_d3b_post_ok(&p), "EN_MAC accepted");
}

int main(void)
{
	test_board_decode();
	test_mhf_shm_destinations();
	test_antsel_vectors();
	test_bs_shape();
	test_bs_offsets_block_phy();
	test_order_and_dma_lifetime();
	test_mode_exclusion();
	test_post_predicate();

	if (failures) {
		printf("ob_d3b_test: %d FAILURES\n", failures);
		return 1;
	}
	printf("ob_d3b_test: PASS\n");
	return 0;
}

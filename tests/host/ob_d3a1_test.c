// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the isolated D3A1 vendor post-common /
 * pre-PHY D11 tail test (M3.4D3A1). Pure helpers only: no hardware, no kernel
 * API.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_d3a1.h"
#include "../../src/ob_rx.h"
#include "../../src/ob_ucode.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

static void chk_u64(const char *what, u64 got, u64 exp)
{
	if (got != exp) {
		printf("FAIL: %s: got 0x%llx expected 0x%llx\n", what,
		       (unsigned long long)got, (unsigned long long)exp);
		failures++;
	}
}

static void test_mode_conflict(void)
{
	chk("d3a1 only",
	    ob_d3a1_mode_select(false, false, false, false, true),
	    OB_ISOLATED_D3A1_TEST);
	chk("d3a1 count", ob_d3a1_mode_count(false, false, false, false, true),
	    1);
	chk("none", ob_d3a1_mode_select(false, false, false, false, false),
	    OB_ISOLATED_NONE);

	/* every pairwise conflict with d3a1 must be rejected */
	chk("d3a1+fw conflict",
	    ob_isolated_mode_conflict(
		    ob_d3a1_mode_select(true, false, false, false, true)), 1);
	chk("d3a1+ucode conflict",
	    ob_isolated_mode_conflict(
		    ob_d3a1_mode_select(false, true, false, false, true)), 1);
	chk("d3a1+initvals conflict",
	    ob_isolated_mode_conflict(
		    ob_d3a1_mode_select(false, false, true, false, true)), 1);
	chk("d3a1+dma conflict",
	    ob_isolated_mode_conflict(
		    ob_d3a1_mode_select(false, false, false, true, true)), 1);
	chk("all five conflict",
	    ob_isolated_mode_conflict(
		    ob_d3a1_mode_select(true, true, true, true, true)), 1);

	chk("d3a1 uses dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_D3A1_TEST), 1);
	chk("d3a1 applies initvals",
	    ob_isolated_mode_applies_initvals(OB_ISOLATED_D3A1_TEST), 1);
	chk("d3a1 does not skip teardown",
	    ob_isolated_mode_skips_teardown(OB_ISOLATED_D3A1_TEST), 0);

	/* other modes unchanged */
	chk("dma uses dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_DMA_TEST), 1);
	chk("ucode not dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_UCODE_TEST), 0);
}

static void test_order_model(void)
{
	chk("d3a1 order", ob_d3a1_order_ok(), 1);
	chk("fifo first",
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_FIFO_FIXUP), 0);
	chk("t1 before dma",
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_T1) <
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_DMA), 1);
	chk("dma before t2",
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_DMA) <
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_T2), 1);
	chk("stop after macfreq",
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_SWITCH_MACFREQ) <
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_STOP_BEFORE_SUB6656C), 1);
	chk("stop is last",
	    ob_d3a1_stage_rank(OB_D3A1_STAGE_STOP_BEFORE_SUB6656C),
	    OB_D3A1_STAGE__COUNT - 1);
}

static void test_sub67efd_model(void)
{
	u16 v = 0x0100;

	chk("table1 len", OB_D3A1_FIFO_TABLE1_LEN, 7);
	chk("table1[0]", ob_d3a1_fifo_table1(0), 7);
	chk("table1[1]", ob_d3a1_fifo_table1(1), 0);
	chk("table1[6]", ob_d3a1_fifo_table1(6), 5);

	/* flush value = (machwcap >> 1) & 0xffc */
	chk("flush val", ob_d3a1_fifo_flush_val(0x200), 0x100);
	chk("flush val mask", ob_d3a1_fifo_flush_val(0x1fff), 0xffc);

	/* entry == 7 group */
	chk("7 rqpri", ob_d3a1_fifo7_rqpri(7, v), 0x2a);
	chk("7 txptr", ob_d3a1_fifo7_txptr(7), 0x2a);
	chk("7 def", ob_d3a1_fifo7_def(7), 0x2a);
	chk("7 54e", ob_d3a1_fifo7_54e(7), 0x262a);
	chk("7 prirdy", ob_d3a1_fifo7_prirdy(7), 0x17);

	/* rev42 group (entry != 7) */
	chk("rev42 rqpri", ob_d3a1_fifo7_rqpri(0, v), 0x100 - 0x2a);
	chk("rev42 txptr", ob_d3a1_fifo7_txptr(0), 0x20);
	chk("rev42 def", ob_d3a1_fifo7_def(0), 0x0b);
	chk("rev42 54e", ob_d3a1_fifo7_54e(0), 0x1216);
	chk("rev42 prirdy", ob_d3a1_fifo7_prirdy(3), 0x13);
	chk("tplate const", OB_D3A1_FIFO_VAL_TPLATE, 0x740c);

	/* exact write accounting: 42 from loop1, 168 from loop2 */
	chk("loop7 writes", OB_D3A1_FIFO7_WRITES,
	    OB_D3A1_FIFO_TABLE1_LEN * 6);
	chk("loop7 writes value", OB_D3A1_FIFO7_WRITES, 42);
	chk("loop42 entries", OB_D3A1_FIFO42_ENTRIES, 42);
	chk("loop42 writes", OB_D3A1_FIFO42_WRITES,
	    OB_D3A1_FIFO42_ENTRIES * 4);
	chk("loop42 writes value", OB_D3A1_FIFO42_WRITES, 168);
	chk("fixed writes", OB_D3A1_FIFO_FIXED_WRITES, 2);
	chk("total writes", OB_D3A1_FIFO_TOTAL_WRITES, 212);
	chk("poll bound", OB_D3A1_FIFO_POLL_BOUND, 0xd1);
	chk("poll max iters", OB_D3A1_FIFO_POLL_MAX_ITERS, 20);

	/* 42-entry per-index values */
	chk("x534[0]", ob_d3a1_fifo42_x534(0), 0);
	chk("x536[0]", ob_d3a1_fifo42_x536(0), 2);
	chk("x532[0] (+1)", ob_d3a1_fifo42_x532(0), 3);
	chk("x532[1] (no +1)", ob_d3a1_fifo42_x532(1), 3);
	chk("x530[0]", ob_d3a1_fifo42_x530(0), 0x8007);
	chk("x536 clamp", ob_d3a1_fifo42_x536(41), 0x29);
	chk("x530[41]", ob_d3a1_fifo42_x530(41), 0x8297);
}

static void test_t1_constants(void)
{
	u32 old = 0x04020402u;
	u32 now = (old & ~OB_D3A1_MACCONTROL_MASK) | OB_D3A1_MACCONTROL_VAL;

	chk_u64("maccontrol new", now, 0x44020402);
	chk_u64("maccontrol expected", OB_D3A1_MACCONTROL_EXPECTED,
		0x44020402);
	chk("mburst", OB_D3A1_SHM_MBURST_VAL, 8);
	chk("maxantcnt", OB_D3A1_SHM_MAXANTCNT_VAL, 0x0a);
	chk_u64("intrcvlazy", OB_D3A1_INTRCVLAZY, 0x01000000);
	chk_u64("tsf cfprep", OB_D3A1_TSF_CFPREP, 0x80000000);
	chk_u64("tsf cfpstart", OB_D3A1_TSF_CFPSTART, 0x02000000);
	chk_u64("mi_gp1", OB_D3A1_MI_GP1, 0x4000);
	chk_u64("i_ri", OB_D3A1_I_RI, 0x10000);
	chk("shm machwver", OB_D3A1_SHM_MACHWVER, 0x16);
	chk("shm machwcap l", OB_D3A1_SHM_MACHWCAP_L, 0xc0);
	chk("shm machwcap h", OB_D3A1_SHM_MACHWCAP_H, 0xc2);
	chk("scr srl", OB_D3A1_SCR_SRL, 0x18);
	chk("scr lrl", OB_D3A1_SCR_LRL, 0x1c);
	chk("shm sfbl", OB_D3A1_SHM_SFBL, 0x44);
	chk("shm lfbl", OB_D3A1_SHM_LFBL, 0x46);
	chk("objaddr shm sel", OB_D3A1_OBJADDR_SHM_SEL, 0x10000);
	chk("objaddr scr sel", OB_D3A1_OBJADDR_SCR_SEL, 0x20000);
	chk("rev42", OB_D3A1_PHYREV_REV42, 0x2a);
}

static void test_mac_to_shm(void)
{
	/* Proven ASUS PCE-AC56 MAC. */
	const u8 mac[6] = {0x2c, 0xfd, 0xa1, 0x61, 0x40, 0x25};

	chk("mac word0", ob_d3a1_mac_word(mac, 0), 0x2cfd);
	chk("mac word1", ob_d3a1_mac_word(mac, 1), 0xa161);
	chk("mac word2", ob_d3a1_mac_word(mac, 2), 0x4025);
	chk("mac words", OB_D3A1_MAC_WORDS, 3);
	chk("shm mac0", OB_D3A1_SHM_MAC_0, 0x78c);
	chk("shm mac1", OB_D3A1_SHM_MAC_1, 0x78e);
	chk("shm mac2", OB_D3A1_SHM_MAC_2, 0x790);
}

static void test_scr24_and_btc(void)
{
	chk("scr 0x24", OB_D3A1_SCR_RATE_24, 0x24);
	chk("scr 0x24 skipped on first init",
	    OB_D3A1_SCR_RATE_24_FIRST_INIT_SKIPPED, 1);

	/* btc_base gate */
	chk("btc_base 0", ob_d3a1_btc_base(0), 0);
	chk("btc_base 2x", ob_d3a1_btc_base(0x10), 0x20);
	chk("shm btc base", OB_D3A1_SHM_BTC_BASE, 0x92);
	chk("btc params max", OB_D3A1_BTC_PARAMS_MAX, 0x76);

	/* absent key -> SKIP (never zero-fill) */
	chk("absent btc param skip",
	    ob_d3a1_btc_key_action(NULL), OB_D3A1_BTC_SKIP);
	chk("absent btc flags skip",
	    ob_d3a1_btc_key_action(NULL), OB_D3A1_BTC_SKIP);
	chk("present btc param writes",
	    ob_d3a1_btc_key_action("1"), OB_D3A1_BTC_WRITE);

	/* fixed 0x4352 extras */
	chk("extra1", OB_D3A1_BTC_EXTRA_VAL1, 0x7530);
	chk("extra2", OB_D3A1_BTC_EXTRA_VAL2, 0x4e20);
	chk("extra3", OB_D3A1_BTC_EXTRA_VAL3, 0x7530);
	chk("extra4", OB_D3A1_BTC_EXTRA_VAL4, 0x0753);
	chk("chip 0x4352 uses macfreq",
	    ob_d3a1_chip_uses_macfreq(0x4352), 1);
	chk("chip 0x0000 no macfreq",
	    ob_d3a1_chip_uses_macfreq(0x0000), 0);
}

static void test_switch_macfreq_math(void)
{
	u32 frac = ob_d3a1_tsf_frac(400000);
	u32 zero = ob_d3a1_tsf_frac(0);

	chk("tsf frac 0 vco", zero, 0);
	chk("tsf frac nonzero", frac != 0, 1);
	chk("frac reconstruct",
	    ((u32)ob_d3a1_tsf_frac_hi(frac) << 16) |
	    (u32)ob_d3a1_tsf_frac_lo(frac), frac);
	chk("tsf frac regs", OB_D3A1_REG_TSF_FRAC_L, 0x62e);
	chk("tsf frac reg h", OB_D3A1_REG_TSF_FRAC_H, 0x630);
}

static void test_lifecycle_reuse(void)
{
	struct ob_d3a0_lifecycle lc;

	memset(&lc, 0, sizeof(lc));
	chk("empty cannot free", ob_d3a0_can_free(&lc), 0);

	lc.tx[0] = OB_D3A0_PROGRAMMED;
	lc.rx = OB_D3A0_PROGRAMMED;
	chk("active cannot free", ob_d3a0_can_free(&lc), 0);

	lc.engines_stopped = true;
	lc.free_allowed = true;
	chk("stopped can free", ob_d3a0_can_free(&lc), 1);

	lc.fatal = true;
	chk("fatal cannot free", ob_d3a0_can_free(&lc), 0);
}

int main(void)
{
	test_mode_conflict();
	test_order_model();
	test_sub67efd_model();
	test_t1_constants();
	test_mac_to_shm();
	test_scr24_and_btc();
	test_switch_macfreq_math();
	test_lifecycle_reuse();

	if (failures) {
		printf("ob_d3a1_test: %d FAILURES\n", failures);
		return 1;
	}
	printf("ob_d3a1_test: PASS\n");
	return 0;
}

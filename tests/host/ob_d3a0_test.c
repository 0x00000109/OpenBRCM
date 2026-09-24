// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the isolated vendor pre-PHY DMA bring-up
 * (M3.4D3A0). Pure helpers only: no hardware, no kernel API.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_d3a0.h"
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

static void test_tx_reg_map(void)
{
	chk("tx channels", OB_D3A0_TX_CHANNELS, 4);
	chk("tx0 base (AC_BK)", ob_d3a0_tx_base(0), 0x0200);
	chk("tx1 base (AC_BE)", ob_d3a0_tx_base(1), 0x0240);
	chk("tx2 base (AC_VI)", ob_d3a0_tx_base(2), 0x0280);
	chk("tx3 base (AC_VO/CTL)", ob_d3a0_tx_base(3), 0x02c0);
	chk("rx base", OB_D3A0_RX_BASE, 0x0220);
	chk("d64 control off", OB_D3A0_D64_CONTROL, 0x00);
	chk("d64 ptr off", OB_D3A0_D64_PTR, 0x04);
	chk("d64 addrlow off", OB_D3A0_D64_ADDRLOW, 0x08);
	chk("d64 addrhigh off", OB_D3A0_D64_ADDRHIGH, 0x0c);
	chk("d64 status0 off", OB_D3A0_D64_STATUS0, 0x10);
	chk("d64 status1 off", OB_D3A0_D64_STATUS1, 0x14);
}

static void test_tx_geometry(void)
{
	chk("ntxd", OB_D3A0_TX_NTXD, 512);
	chk("tx ring bytes", OB_D3A0_TX_RING_BYTES, 8192);
	chk("desc size", OB_DMA_DESC_SIZE, 16);
	chk("ring align", OB_D3A0_RING_ALIGN, 8192);
	chk("rxpost", OB_DMA_RX_POST_INIT, 64);
	chk("rxbufsize", OB_DMA_RX_BUFSZ, 2048);
	chk("rxoffset", OB_RX_HDR_LEN, 38);
	chk("rx control", OB_D3A0_RX_CONTROL, 0x0000084d);
	chk("rx ptr", OB_D3A0_RX_PTR, 0x400);
	chk("pcie h32", OB_DMA_PCIE_H32, 0x80000000);
	/* Ownership: TX publishes base+CONTROL only (zero payload mappings). */
	chk("tx payload mappings", OB_D3A0_TX_PAYLOAD_MAPPINGS, 0);
	chk("rx mappings", OB_D3A0_RX_MAPPINGS, 64);
	chk("rx mappings == post init", OB_D3A0_RX_MAPPINGS,
	    OB_DMA_RX_POST_INIT);
}

/* The D3A0 prefix may run only after the exact D2B exit state is proven. */
static void test_d2b_gate(void)
{
	struct ob_initvals_post p = {
		.fifosize0 = OB_INITVALS_FIFOSIZE0_EXPECTED,
		.fifosize1 = OB_INITVALS_FIFOSIZE1_EXPECTED,
		.fifosize2 = OB_INITVALS_FIFOSIZE2_EXPECTED,
		.fifosize3 = OB_INITVALS_FIFOSIZE3_EXPECTED,
		.macintmask = OB_INITVALS_MACINTMASK_EXPECTED,
		.maccontrol = OB_INITVALS_MACCONTROL_EXPECTED,
		.shm14 = OB_INITVALS_SHM14_EXPECTED,
	};

	chk("d2b gate ok", ob_d3a0_d2b_state_ok(&p), 1);
	chk("d2b exit maccontrol", OB_INITVALS_MACCONTROL_EXPECTED,
	    0x04020402);
	chk("d2b exit macintmask", OB_INITVALS_MACINTMASK_EXPECTED, 0);

	/* The D3A0 transition target is NOT the D2B exit and must be rejected. */
	p.maccontrol = 0x44020402u;
	chk("d2b gate rejects post-transition mctrl",
	    ob_d3a0_d2b_state_ok(&p), 0);
	p.maccontrol = OB_INITVALS_MACCONTROL_EXPECTED;

	p.fifosize0 = 0;
	chk("d2b gate rejects fifo0 mismatch", ob_d3a0_d2b_state_ok(&p), 0);
	p.fifosize0 = OB_INITVALS_FIFOSIZE0_EXPECTED;

	p.macintmask = 1;
	chk("d2b gate rejects macintmask", ob_d3a0_d2b_state_ok(&p), 0);
	p.macintmask = OB_INITVALS_MACINTMASK_EXPECTED;

	p.shm14 = 0;
	chk("d2b gate rejects shm14 mismatch", ob_d3a0_d2b_state_ok(&p), 0);

	chk("d2b gate rejects null", ob_d3a0_d2b_state_ok(NULL), 0);
}

static void test_tx_control(void)
{
	u32 old = 0x00000000;
	u32 now;

	/* empty engine: only XE and PD appear */
	now = ob_d3a0_tx_control(old);
	chk("tx control XE", (now & OB_D3A0_XC_XE) != 0, 1);
	chk("tx control PD", (now & OB_D3A0_XC_PD) != 0, 1);
	chk("tx control ok empty", ob_d3a0_tx_control_ok(old, now), 1);
	chk_u64("tx control value", now, 0x801);

	/* capability/unrelated bits must be preserved exactly */
	old = 0x000fc030u | 0x00000002u;	/* cap bits + SE */
	now = ob_d3a0_tx_control(old);
	chk("tx control preserved", ob_d3a0_tx_control_ok(old, now), 1);
	chk_u64("tx control keeps old bits",
		now & ~(u64)(OB_D3A0_XC_XE | OB_D3A0_XC_PD), old);

	/* a synthetic constant write that clears a cap bit must be rejected */
	chk("tx control reject lost cap",
	    ob_d3a0_tx_control_ok(0x00040000u, 0x801u), 0);
	chk("tx control reject missing XE",
	    ob_d3a0_tx_control_ok(0, OB_D3A0_XC_PD), 0);

	/* No per-FIFO fixed CONTROL constant: XE/PD only. */
	chk("tx xc se", OB_D3A0_XC_SE, 0x2);
}

static void test_addr_window(void)
{
	chk("in window 0", ob_dma_addr_in_window(0), 1);
	chk("in window 4G-1", ob_dma_addr_in_window(0xffffffffULL), 1);
	chk("outside 4G", ob_dma_addr_in_window(0x100000000ULL), 0);
	chk("high32 0", ob_dma_addr_high32(0xfe0e6000ULL), 0);
	chk("high32 1", ob_dma_addr_high32(0x1fe0e6000ULL), 1);
}

static void test_irq_constants(void)
{
	chk("intrcvlazy", OB_D3A0_INTRCVLAZY, 0x01000000);
	chk("I_RI", OB_D3A0_I_RI, 0x00010000);
	chk("MI_DMAINT", OB_D3A0_MI_DMAINT, 0x00008000);
	chk("I_RI != MI_DMAINT", OB_D3A0_I_RI != OB_D3A0_MI_DMAINT, 1);
	chk("no bit overlap", (OB_D3A0_I_RI & OB_D3A0_MI_DMAINT), 0);
	chk("host irq disabled at 0", ob_d3a0_host_irq_disabled(0), 1);
	chk("host irq not disabled nonzero", ob_d3a0_host_irq_disabled(1), 0);
	chk("irq source ok", ob_d3a0_irq_source_ok(OB_D3A0_I_RI), 1);
	chk("irq source missing", ob_d3a0_irq_source_ok(0), 0);
}

static void test_maccontrol(void)
{
	/* D2B exit 0x04020402 -> vendor transition 0x44020402 */
	u32 old = 0x04020402u;
	u32 now = (old & ~OB_D3A0_MACCONTROL_MASK) | OB_D3A0_MACCONTROL_VAL;

	chk_u64("maccontrol new", now, 0x44020402);
	chk("maccontrol ok", ob_d3a0_maccontrol_ok(now), 1);
	chk("maccontrol old not ok (no DISCARD_PMQ)",
	    ob_d3a0_maccontrol_ok(old), 0);
	chk("maccontrol en_mac reject",
	    ob_d3a0_maccontrol_ok(now | OB_D3A0_MCTL_EN_MAC), 0);
	chk("maccontrol shm_en reject",
	    ob_d3a0_maccontrol_ok(now | OB_D3A0_MCTL_SHM_EN), 0);
}

static void test_status_helpers(void)
{
	chk("disabled", ob_d3a0_rx_disabled(0x0), 1);
	chk("idle", ob_d3a0_rx_idle(0x2000e000u), 1);
	chk("active not idle", ob_d3a0_rx_idle(0x10000000u), 0);
	chk("settled disabled", ob_d3a0_tx_reset_settled(0x0), 1);
	chk("settled stopped", ob_d3a0_tx_reset_settled(0x30000000u), 1);
	chk("settled idle", ob_d3a0_tx_reset_settled(0x20000000u), 1);
	chk("settled active", ob_d3a0_tx_reset_settled(0x10000000u), 0);
	chk("reset timeout", OB_D3A0_RESET_TIMEOUT, 0x00002719);
	chk("reset max iters", ob_d3a0_reset_max_iters(),
	    0x00002719 / 10);
	chk("poll expired at 9", ob_d3a0_poll_expired(9), 1);
	chk("poll active at 10", ob_d3a0_poll_expired(10), 0);
}

static void test_lifecycle(void)
{
	struct ob_d3a0_lifecycle lc;

	memset(&lc, 0, sizeof(lc));
	/* nothing verified yet: no free permit even for an empty lifecycle */
	chk("empty not active", ob_d3a0_hw_active(&lc), 0);
	chk("empty cannot free (no permit)", ob_d3a0_can_free(&lc), 0);

	/* partially brought up: TX0/TX1 programmed, TX2 failed, TX3 untouched */
	lc.tx[0] = OB_D3A0_PROGRAMMED;
	lc.tx[1] = OB_D3A0_PROGRAMMED;
	lc.tx[2] = OB_D3A0_NONE;
	lc.tx[3] = OB_D3A0_ALLOCATED;
	chk("partial active", ob_d3a0_hw_active(&lc), 1);
	chk("partial cannot free", ob_d3a0_can_free(&lc), 0);

	/* A: every programmed engine verified stopped -> free allowed */
	lc.engines_stopped = true;
	lc.free_allowed = true;
	chk("A stopped not active", ob_d3a0_hw_active(&lc), 0);
	chk("A can free", ob_d3a0_can_free(&lc), 1);

	/* fatal overrides everything: never free, even if stopped/permitted */
	lc.fatal = true;
	chk("A+fatal cannot free", ob_d3a0_can_free(&lc), 0);
}

/*
 * Conservative fatal matrix (B/C/D). Core-reset containment is reported but
 * NEVER authorizes a free; only a normal per-channel verified stop does.
 */
static void test_fatal_scenarios(void)
{
	struct ob_d3a0_lifecycle lc;

	/* B: a TX reset failed, core disable verified -> fatal, retained */
	memset(&lc, 0, sizeof(lc));
	lc.tx[0] = OB_D3A0_PROGRAMMED;
	lc.tx[1] = OB_D3A0_PROGRAMMED;
	lc.rx = OB_D3A0_PROGRAMMED;
	lc.core_contained = true;
	lc.fatal = true;
	chk("B active", ob_d3a0_hw_active(&lc), 1);
	chk("B cannot free", ob_d3a0_can_free(&lc), 0);
	chk("B containment is not a free permit", lc.free_allowed, 0);

	/* C: RX reset failed, core disable verified -> fatal, retained */
	memset(&lc, 0, sizeof(lc));
	lc.rx = OB_D3A0_PROGRAMMED;
	lc.core_contained = true;
	lc.fatal = true;
	chk("C cannot free", ob_d3a0_can_free(&lc), 0);

	/* D: reset failed and core disable failed -> fatal, retained */
	memset(&lc, 0, sizeof(lc));
	lc.rx = OB_D3A0_PROGRAMMED;
	lc.tx[3] = OB_D3A0_PROGRAMMED;
	lc.fatal = true;
	chk("D active", ob_d3a0_hw_active(&lc), 1);
	chk("D cannot free", ob_d3a0_can_free(&lc), 0);

	/* a permit without engines_stopped must still be refused */
	memset(&lc, 0, sizeof(lc));
	lc.tx[0] = OB_D3A0_PROGRAMMED;
	lc.free_allowed = true;
	chk("permit without stop cannot free", ob_d3a0_can_free(&lc), 0);

	/* containment alone can never grant the permit */
	memset(&lc, 0, sizeof(lc));
	lc.tx[0] = OB_D3A0_PROGRAMMED;
	lc.core_contained = true;
	chk("containment alone cannot free", ob_d3a0_can_free(&lc), 0);
}

static void test_rx_descriptors(void)
{
	struct ob_dma_desc d;
	u8 mem[OB_DMA_RING_BYTES];
	struct ob_dma_ring r;

	memset(&r, 0, sizeof(r));
	r.desc_cpu = mem;
	r.desc_dma = 0xfe0e6000ULL;
	r.n = OB_DMA_RING_DESC_COUNT_RX;
	r.role = OB_DMA_RING_RX;

	/* ordinary posted buffer: ctrl1=0, len=2048, addrhigh=0x80000000 */
	ob_rx_desc_build(&d, 0x00000000fe0e7000ULL, false);
	chk("rx desc ctrl1", ob_dma_desc_ctrl1(&d), 0);
	chk("rx desc len", ob_dma_desc_len(&d), 2048);
	chk("rx desc addrhigh", ob_dma_desc_addrhigh(&d), OB_DMA_PCIE_H32);
	chk_u64("rx desc addrlow", ob_dma_desc_addrlow(&d), 0xfe0e7000u);

	/* structural EOT descriptor: no live buffer, EOT set */
	ob_rx_desc_build(&d, 0, true);
	chk("eot ctrl1", ob_dma_desc_ctrl1(&d), OB_DMA_CTRL1_EOT);
	chk("eot len", ob_dma_desc_len(&d), 0);
	chk("eot addrhigh", ob_dma_desc_addrhigh(&d), OB_DMA_PCIE_H32);
}

static void test_mode_inclusion(void)
{
	/* dma_test_only is an isolated mode and cannot combine with others. */
	chk("dma isolated",
	    ob_isolated_mode_select(false, false, false, true),
	    OB_ISOLATED_DMA_TEST);
	chk("dma+initvals conflict",
	    ob_isolated_mode_conflict(
		    ob_isolated_mode_select(false, false, true, true)), 1);
	chk("dma uses dma",
	    ob_isolated_mode_uses_dma(OB_ISOLATED_DMA_TEST), 1);
	chk("dma not teardown-skip",
	    ob_isolated_mode_skips_teardown(OB_ISOLATED_DMA_TEST), 0);
}

int main(void)
{
	test_tx_reg_map();
	test_tx_geometry();
	test_tx_control();
	test_d2b_gate();
	test_addr_window();
	test_irq_constants();
	test_maccontrol();
	test_status_helpers();
	test_lifecycle();
	test_fatal_scenarios();
	test_rx_descriptors();
	test_mode_inclusion();

	if (failures) {
		printf("ob_d3a0_test: %d FAILURES\n", failures);
		return 1;
	}
	printf("ob_d3a0_test: PASS\n");
	return 0;
}

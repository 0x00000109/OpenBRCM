// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the isolated vendor pre-PHY DMA bring-up
 * (M3.4D3A0). Mirrors tests/host/ob_d3a0_test.c (pure helpers only).
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_d3a0.h"
#include "ob_rx.h"

static void ob_d3a0_tx_reg_map_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, OB_D3A0_TX_CHANNELS, 4u);
	KUNIT_EXPECT_EQ(test, ob_d3a0_tx_base(0), 0x0200);
	KUNIT_EXPECT_EQ(test, ob_d3a0_tx_base(1), 0x0240);
	KUNIT_EXPECT_EQ(test, ob_d3a0_tx_base(2), 0x0280);
	KUNIT_EXPECT_EQ(test, ob_d3a0_tx_base(3), 0x02c0);
	KUNIT_EXPECT_EQ(test, OB_D3A0_RX_BASE, 0x0220);
	/* TX publishes base+CONTROL only: zero payload mappings. */
	KUNIT_EXPECT_EQ(test, OB_D3A0_TX_PAYLOAD_MAPPINGS, 0u);
	KUNIT_EXPECT_EQ(test, OB_D3A0_RX_MAPPINGS, OB_DMA_RX_POST_INIT);
}

static void ob_d3a0_d2b_gate_test(struct kunit *test)
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

	KUNIT_EXPECT_TRUE(test, ob_d3a0_d2b_state_ok(&p));
	/* the D3A0 transition target is not the D2B exit */
	p.maccontrol = 0x44020402u;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_d2b_state_ok(&p));
	p.maccontrol = OB_INITVALS_MACCONTROL_EXPECTED;
	p.fifosize0 = 0;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_d2b_state_ok(&p));
	p.fifosize0 = OB_INITVALS_FIFOSIZE0_EXPECTED;
	p.macintmask = 1;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_d2b_state_ok(&p));
	p.macintmask = OB_INITVALS_MACINTMASK_EXPECTED;
	p.shm14 = 0;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_d2b_state_ok(&p));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_d2b_state_ok(NULL));
}

static void ob_d3a0_geometry_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, OB_D3A0_TX_NTXD, 512u);
	KUNIT_EXPECT_EQ(test, OB_D3A0_TX_RING_BYTES, 8192u);
	KUNIT_EXPECT_EQ(test, OB_D3A0_RING_ALIGN, 8192u);
	KUNIT_EXPECT_EQ(test, OB_DMA_RX_POST_INIT, 64u);
	KUNIT_EXPECT_EQ(test, OB_DMA_RX_BUFSZ, 2048u);
	KUNIT_EXPECT_EQ(test, OB_D3A0_RX_CONTROL, 0x0000084du);
	KUNIT_EXPECT_EQ(test, OB_D3A0_RX_PTR, 0x400u);
	KUNIT_EXPECT_EQ(test, OB_DMA_PCIE_H32, 0x80000000u);
}

static void ob_d3a0_tx_control_test(struct kunit *test)
{
	u32 old = 0x000fc030u | OB_D3A0_XC_SE;
	u32 now = ob_d3a0_tx_control(old);

	KUNIT_EXPECT_TRUE(test, now & OB_D3A0_XC_XE);
	KUNIT_EXPECT_TRUE(test, now & OB_D3A0_XC_PD);
	KUNIT_EXPECT_TRUE(test, ob_d3a0_tx_control_ok(old, now));
	KUNIT_EXPECT_EQ(test, now & ~(OB_D3A0_XC_XE | OB_D3A0_XC_PD),
			old & ~(OB_D3A0_XC_XE | OB_D3A0_XC_PD));
	/* synthetic constant that loses a capability bit is rejected */
	KUNIT_EXPECT_FALSE(test, ob_d3a0_tx_control_ok(0x00040000u, 0x801u));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_tx_control_ok(0, OB_D3A0_XC_PD));
}

static void ob_d3a0_addr_window_test(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, ob_dma_addr_in_window(0));
	KUNIT_EXPECT_TRUE(test, ob_dma_addr_in_window(0xffffffffULL));
	KUNIT_EXPECT_FALSE(test, ob_dma_addr_in_window(0x100000000ULL));
}

static void ob_d3a0_irq_constants_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, OB_D3A0_INTRCVLAZY, 0x01000000u);
	KUNIT_EXPECT_EQ(test, OB_D3A0_I_RI, 0x00010000u);
	KUNIT_EXPECT_EQ(test, OB_D3A0_MI_DMAINT, 0x00008000u);
	KUNIT_EXPECT_EQ(test, OB_D3A0_I_RI & OB_D3A0_MI_DMAINT, 0u);
	KUNIT_EXPECT_TRUE(test, ob_d3a0_host_irq_disabled(0));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_host_irq_disabled(1));
	KUNIT_EXPECT_TRUE(test, ob_d3a0_irq_source_ok(OB_D3A0_I_RI));
}

static void ob_d3a0_maccontrol_test(struct kunit *test)
{
	u32 old = 0x04020402u;
	u32 now = (old & ~OB_D3A0_MACCONTROL_MASK) | OB_D3A0_MACCONTROL_VAL;

	KUNIT_EXPECT_EQ(test, now, 0x44020402u);
	KUNIT_EXPECT_TRUE(test, ob_d3a0_maccontrol_ok(now));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_maccontrol_ok(now | OB_D3A0_MCTL_EN_MAC));
}

static void ob_d3a0_status_test(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, ob_d3a0_rx_disabled(0));
	KUNIT_EXPECT_TRUE(test, ob_d3a0_rx_idle(0x2000e000u));
	KUNIT_EXPECT_TRUE(test, ob_d3a0_tx_reset_settled(0x30000000u));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_tx_reset_settled(0x10000000u));
	KUNIT_EXPECT_EQ(test, ob_d3a0_reset_max_iters(), 0x00002719u / 10u);
	KUNIT_EXPECT_TRUE(test, ob_d3a0_poll_expired(9));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_poll_expired(10));
}

static void ob_d3a0_lifecycle_test(struct kunit *test)
{
	struct ob_d3a0_lifecycle lc;

	memset(&lc, 0, sizeof(lc));
	KUNIT_EXPECT_TRUE(test, ob_d3a0_can_free(&lc));

	lc.tx[0] = OB_D3A0_PROGRAMMED;
	lc.tx[1] = OB_D3A0_PROGRAMMED;
	lc.tx[3] = OB_D3A0_ALLOCATED;
	KUNIT_EXPECT_TRUE(test, ob_d3a0_hw_active(&lc));
	KUNIT_EXPECT_FALSE(test, ob_d3a0_can_free(&lc));

	lc.quiesced = true;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_hw_active(&lc));
	KUNIT_EXPECT_TRUE(test, ob_d3a0_can_free(&lc));

	/* fatal overrides everything, even a quiesced flag */
	lc.fatal = true;
	lc.quiesced = true;
	KUNIT_EXPECT_FALSE(test, ob_d3a0_can_free(&lc));
}

static void ob_d3a0_rx_desc_test(struct kunit *test)
{
	struct ob_dma_desc d;

	ob_rx_desc_build(&d, 0x00000000fe0e7000ULL, false);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_ctrl1(&d), 0u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_len(&d), 2048u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addrhigh(&d), OB_DMA_PCIE_H32);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addrlow(&d), 0xfe0e7000u);

	ob_rx_desc_build(&d, 0, true);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_ctrl1(&d), OB_DMA_CTRL1_EOT);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addrhigh(&d), OB_DMA_PCIE_H32);
}

static struct kunit_case ob_d3a0_test_cases[] = {
	KUNIT_CASE(ob_d3a0_tx_reg_map_test),
	KUNIT_CASE(ob_d3a0_geometry_test),
	KUNIT_CASE(ob_d3a0_d2b_gate_test),
	KUNIT_CASE(ob_d3a0_tx_control_test),
	KUNIT_CASE(ob_d3a0_addr_window_test),
	KUNIT_CASE(ob_d3a0_irq_constants_test),
	KUNIT_CASE(ob_d3a0_maccontrol_test),
	KUNIT_CASE(ob_d3a0_status_test),
	KUNIT_CASE(ob_d3a0_lifecycle_test),
	KUNIT_CASE(ob_d3a0_rx_desc_test),
	{}
};

static struct kunit_suite ob_d3a0_test_suite = {
	.name = "openbrcm_d3a0",
	.test_cases = ob_d3a0_test_cases,
};
kunit_test_suite(ob_d3a0_test_suite);

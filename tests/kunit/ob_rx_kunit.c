// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the FIFO0 RX model (M3.4B).
 * Mirrors tests/host/ob_rx_test.c.
 */
#include <kunit/test.h>
#include "ob_rx.h"

static void ob_rx_index_test(struct kunit *test)
{
	const dma_addr_t ring = 0x00000000fe0e6000ULL;

	KUNIT_EXPECT_EQ(test, ob_rx_status_index((u32)ring, ring), 0u);
	KUNIT_EXPECT_EQ(test, ob_rx_status_index((u32)ring + 16, ring), 1u);
	KUNIT_EXPECT_EQ(test, ob_rx_status_index((u32)ring + 0x400, ring), 64u);
	KUNIT_EXPECT_EQ(test,
			ob_rx_status_index((u32)ring + 255 * 16, ring), 255u);
	KUNIT_EXPECT_EQ(test, ob_rx_status_index((u32)ring - 16, ring), 511u);
	KUNIT_EXPECT_EQ(test, ob_rx_status_index(0xffffffffu, ring), 511u);

	KUNIT_EXPECT_TRUE(test, ob_rx_index_ok(255, 256));
	KUNIT_EXPECT_FALSE(test, ob_rx_index_ok(256, 256));
	KUNIT_EXPECT_FALSE(test, ob_rx_index_ok(511, 256));
}

static void ob_rx_len_test(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test, ob_rx_frame_len_ok(OB_RX_MIN_FRAME));
	KUNIT_EXPECT_TRUE(test, ob_rx_frame_len_ok(OB_RX_MAX_FRAME));
	KUNIT_EXPECT_FALSE(test, ob_rx_frame_len_ok(0));
	KUNIT_EXPECT_FALSE(test, ob_rx_frame_len_ok(9));
	KUNIT_EXPECT_FALSE(test, ob_rx_frame_len_ok(OB_RX_MAX_FRAME + 1));
	KUNIT_EXPECT_FALSE(test, ob_rx_frame_len_ok(0xffff));
	KUNIT_EXPECT_EQ(test, OB_RX_MAX_FRAME, 2048 - 38);
}

static void ob_rx_desc_test(struct kunit *test)
{
	struct ob_dma_desc d;

	ob_rx_desc_build(&d, 0x00000000fe0e6000ULL, false);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_ctrl1(&d), 0u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_len(&d), 0x800u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addrlow(&d), 0xfe0e6000u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addrhigh(&d), OB_DMA_PCIE_H32);

	ob_rx_desc_build(&d, 0, true);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_ctrl1(&d), OB_DMA_CTRL1_EOT);
}

static void ob_rx_fc_test(struct kunit *test)
{
	u8 beacon[2] = { 0x80, 0x00 };
	u8 data[2] = { 0x08, 0x00 };
	u16 fc = ob_rx_fc(beacon);

	KUNIT_EXPECT_EQ(test, fc, 0x0080);
	KUNIT_EXPECT_EQ(test, ob_rx_fc_type(fc), 0);
	KUNIT_EXPECT_EQ(test, ob_rx_fc_subtype(fc), 8);

	fc = ob_rx_fc(data);
	KUNIT_EXPECT_EQ(test, ob_rx_fc_type(fc), 2);
	KUNIT_EXPECT_EQ(test, ob_rx_fc_subtype(fc), 0);
}

static void ob_rx_ownership_test(struct kunit *test)
{
	struct ob_dma_slot s;
	struct sk_buff *out_skb = NULL;
	dma_addr_t out_dma = 0;

	ob_dma_slot_init(&s);
	KUNIT_EXPECT_TRUE(test,
			  ob_dma_slot_claim(&s, (struct sk_buff *)0x1234,
					    0xfe0e6000));
	KUNIT_EXPECT_FALSE(test,
			   ob_dma_slot_claim(&s, (struct sk_buff *)0x1234,
					     0xfe0e6000));
	KUNIT_EXPECT_TRUE(test,
			  ob_dma_slot_release(&s, &out_skb, &out_dma));
	KUNIT_EXPECT_FALSE(test,
			   ob_dma_slot_release(&s, &out_skb, &out_dma));
}

static struct kunit_case ob_rx_cases[] = {
	KUNIT_CASE(ob_rx_index_test),
	KUNIT_CASE(ob_rx_len_test),
	KUNIT_CASE(ob_rx_desc_test),
	KUNIT_CASE(ob_rx_fc_test),
	KUNIT_CASE(ob_rx_ownership_test),
	{}
};

static struct kunit_suite ob_rx_suite = {
	.name = "openbrcm_rx",
	.test_cases = ob_rx_cases,
};

kunit_test_suite(ob_rx_suite);
MODULE_LICENSE("GPL");

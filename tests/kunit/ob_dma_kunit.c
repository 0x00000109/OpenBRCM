// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the DMA64 software model (M3.2/M3.4A).
 * Mirrors tests/host/ob_dma_test.c; RX and TX capacities are independent.
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_dma.h"

static void ob_dma_geometry_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, sizeof(struct ob_dma_desc), 16);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_DESC_COUNT_RX, 256);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_DESC_COUNT_TX, 512);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_ACTIVE_BYTES_RX, 4096);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_ACTIVE_BYTES_TX, 8192);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_BYTES, 8192);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_ALIGN, 8192);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_count(OB_DMA_RING_RX), 256);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_count(OB_DMA_RING_TX), 512);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_active_bytes(OB_DMA_RING_RX), 4096);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_active_bytes(OB_DMA_RING_TX), 8192);
	KUNIT_EXPECT_EQ(test, OB_DMA_RX_POST_INIT, 64);
	KUNIT_EXPECT_EQ(test, OB_DMA_RX_POST_INIT * 16, 0x400);
}

static void ob_dma_desc_test(struct kunit *test)
{
	struct ob_dma_desc d;

	ob_dma_desc_zero(&d);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_ctrl1(&d), 0u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_len(&d), 0u);

	ob_dma_desc_encode(&d, 0x00000000fe0e6000ULL, OB_DMA_PCIE_H32, 0,
			   OB_DMA_RX_BUFSZ);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_ctrl1(&d), 0u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_len(&d), OB_DMA_RX_BUFSZ);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addrlow(&d), 0xfe0e6000u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addrhigh(&d), OB_DMA_PCIE_H32);

	/* len is masked into OB_DMA_CTRL2_BC_MASK */
	ob_dma_desc_encode(&d, 0, 0, 0, 0x7ffff);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_len(&d), 0x7fff);
}

static void ob_dma_ring_math_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, ob_dma_next(OB_DMA_RING_DESC_COUNT_RX, 255), 0);
	KUNIT_EXPECT_EQ(test, ob_dma_next(OB_DMA_RING_DESC_COUNT_TX, 511), 0);
	KUNIT_EXPECT_EQ(test, ob_dma_prev(256, 0), 255);
	KUNIT_EXPECT_EQ(test, ob_dma_used(256, 0, 255), 1);
	KUNIT_EXPECT_EQ(test, ob_dma_avail(256, 0, 255), 254);
	KUNIT_EXPECT_TRUE(test, ob_dma_full(256, 255, 0));
	KUNIT_EXPECT_FALSE(test, ob_dma_index_valid(256, 256));

	KUNIT_EXPECT_EQ(test, ob_dma_desc_offset(255), 4080);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_offset(511), 8176);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_eot(256, 255), OB_DMA_CTRL1_EOT);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_eot(256, 254), 0u);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_eot(512, 511), OB_DMA_CTRL1_EOT);
}

static struct kunit_case ob_dma_cases[] = {
	KUNIT_CASE(ob_dma_geometry_test),
	KUNIT_CASE(ob_dma_desc_test),
	KUNIT_CASE(ob_dma_ring_math_test),
	{}
};

static struct kunit_suite ob_dma_suite = {
	.name = "openbrcm_dma",
	.test_cases = ob_dma_cases,
};

kunit_test_suite(ob_dma_suite);
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — KUnit tests for the DMA64 software model (M3.2).
 * Mirrors tests/host/ob_dma_test.c.
 */
#include <kunit/test.h>
#include <linux/string.h>
#include "ob_dma.h"

static void ob_dma_desc_test(struct kunit *test)
{
	struct ob_dma_desc d;

	KUNIT_EXPECT_EQ(test, sizeof(struct ob_dma_desc), 16);

	ob_dma_desc_zero(&d);
	KUNIT_EXPECT_EQ(test, d.ctrl1, 0);
	KUNIT_EXPECT_EQ(test, d.ctrl2, 0);
	KUNIT_EXPECT_EQ(test, d.addrlow, 0);
	KUNIT_EXPECT_EQ(test, d.addrhigh, 0);

	ob_dma_desc_encode(&d, 0x1122334455667788ULL,
			   OB_DMA_CTRL1_SOF | OB_DMA_CTRL1_EOT, 0x1234);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_ctrl1(&d), 0x90000000u);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_len(&d), 0x1234);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_addr(&d), 0x1122334455667788ULL);

	/* len is masked into OB_DMA_CTRL2_BC_MASK */
	ob_dma_desc_encode(&d, 0, 0, 0x7ffff);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_len(&d), 0x7fff);
}

static void ob_dma_ring_math_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_DESC_COUNT, 512);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_BYTES, 8192);
	KUNIT_EXPECT_EQ(test, OB_DMA_RING_ALIGN, 8192);

	KUNIT_EXPECT_EQ(test, ob_dma_next(512, 511), 0);
	KUNIT_EXPECT_EQ(test, ob_dma_next(512, 0), 1);
	KUNIT_EXPECT_EQ(test, ob_dma_prev(512, 0), 511);
	KUNIT_EXPECT_EQ(test, ob_dma_prev(512, 1), 0);

	KUNIT_EXPECT_FALSE(test, ob_dma_index_valid(512, 512));
	KUNIT_EXPECT_TRUE(test, ob_dma_index_valid(512, 511));

	KUNIT_EXPECT_TRUE(test, ob_dma_empty(512, 0, 0));
	KUNIT_EXPECT_FALSE(test, ob_dma_full(512, 0, 0));
	KUNIT_EXPECT_EQ(test, ob_dma_used(512, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, ob_dma_avail(512, 0, 0), 511);

	KUNIT_EXPECT_TRUE(test, ob_dma_full(512, 511, 0));
	KUNIT_EXPECT_EQ(test, ob_dma_used(512, 511, 0), 511);
	KUNIT_EXPECT_EQ(test, ob_dma_avail(512, 511, 0), 0);

	KUNIT_EXPECT_EQ(test, ob_dma_used(512, 0, 511), 1);
	KUNIT_EXPECT_EQ(test, ob_dma_avail(512, 0, 511), 510);
}

static void ob_dma_desc_addressing_test(struct kunit *test)
{
	struct ob_dma_ring r;
	u8 mem[OB_DMA_RING_BYTES];
	int i;

	KUNIT_EXPECT_EQ(test, ob_dma_desc_offset(0), 0);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_offset(1), 16);
	KUNIT_EXPECT_EQ(test, ob_dma_desc_offset(511), 8176);

	KUNIT_EXPECT_EQ(test, ob_dma_ring_eot(512, 511),
			OB_DMA_CTRL1_EOT);
	KUNIT_EXPECT_EQ(test, ob_dma_ring_eot(512, 510), 0);

	memset(&r, 0, sizeof(r));
	r.desc_cpu = mem;
	r.n = OB_DMA_RING_DESC_COUNT;

	for (i = 0; i < OB_DMA_RING_DESC_COUNT; i++)
		KUNIT_EXPECT_PTR_EQ(test, ob_dma_desc_at(&r, (u16)i),
				    (void *)(mem + i * OB_DMA_DESC_SIZE));
}

static struct kunit_case ob_dma_cases[] = {
	KUNIT_CASE(ob_dma_desc_test),
	KUNIT_CASE(ob_dma_ring_math_test),
	KUNIT_CASE(ob_dma_desc_addressing_test),
	{}
};

static struct kunit_suite ob_dma_suite = {
	.name = "openbrcm_dma",
	.test_cases = ob_dma_cases,
};

kunit_test_suite(ob_dma_suite);
MODULE_LICENSE("GPL");

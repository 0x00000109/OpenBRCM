// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the DMA64 software model (M3.2).
 *
 * These exercise the pure descriptor encoding and ring arithmetic in
 * src/ob_dma.h with no hardware and no kernel API.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_dma.h"

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

int main(void)
{
	struct ob_dma_desc d;
	u8 mem[OB_DMA_RING_BYTES];
	struct ob_dma_ring r;
	int i, j;

	/* ---- compile-time geometry ---- */
	chk("sizeof desc", (long)sizeof(struct ob_dma_desc), 16);
	chk("desc count", OB_DMA_RING_DESC_COUNT, 512);
	chk("ring bytes", OB_DMA_RING_BYTES, 8192);
	chk("ring align", OB_DMA_RING_ALIGN, 8192);

	/* ---- descriptor field encoding ---- */
	ob_dma_desc_zero(&d);
	chk("zero ctrl1", (long)d.ctrl1, 0);
	chk("zero ctrl2", (long)d.ctrl2, 0);
	chk("zero addrlow", (long)d.addrlow, 0);
	chk("zero addrhigh", (long)d.addrhigh, 0);

	ob_dma_desc_encode(&d, 0x1122334455667788ULL,
			   OB_DMA_CTRL1_SOF | OB_DMA_CTRL1_EOT, 0x1234);
	chk("enc ctrl1 raw", (long)d.ctrl1, (long)0x90000000u);
	chk("enc ctrl2 raw", (long)d.ctrl2, 0x1234);
	chk("enc addrlow raw", (long)d.addrlow, (long)0x55667788u);
	chk("enc addrhigh raw", (long)d.addrhigh, 0x11223344);
	chk("dec ctrl1", (long)ob_dma_desc_ctrl1(&d), (long)0x90000000u);
	chk("dec len", (long)ob_dma_desc_len(&d), 0x1234);
	chk_u64("dec addr", ob_dma_desc_addr(&d), 0x1122334455667788ULL);

	/* len must be masked into OB_DMA_CTRL2_BC_MASK. */
	ob_dma_desc_encode(&d, 0, 0, 0x7ffff);
	chk("len mask", (long)ob_dma_desc_len(&d), 0x7fff);

	/* low-only address. */
	ob_dma_desc_encode(&d, 0x00000000deadbeefULL, 0, 1);
	chk_u64("addr low only", ob_dma_desc_addr(&d), 0xdeadbeefULL);

	/* ---- slot ownership metadata ---- */
	{
		struct ob_dma_slot s;

		s.skb = (struct sk_buff *)0x1;
		s.dma = 0x2;
		s.mapped = true;
		ob_dma_slot_init(&s);
		chk("slot skb cleared", (long)(s.skb == NULL), 1);
		chk_u64("slot dma cleared", s.dma, 0);
		chk("slot mapped cleared", (long)s.mapped, 0);
	}

	/* ---- index helpers / wraparound ---- */
	chk("next(511)", ob_dma_next(512, 511), 0);
	chk("next(0)", ob_dma_next(512, 0), 1);
	chk("prev(0)", ob_dma_prev(512, 0), 511);
	chk("prev(1)", ob_dma_prev(512, 1), 0);
	chk("index_valid(0)", (long)ob_dma_index_valid(512, 0), 1);
	chk("index_valid(511)", (long)ob_dma_index_valid(512, 511), 1);
	chk("index_valid(512)", (long)ob_dma_index_valid(512, 512), 0);

	/* ---- empty / full / used / available transitions ---- */
	chk("empty", (long)ob_dma_empty(512, 0, 0), 1);
	chk("not full", (long)ob_dma_full(512, 0, 0), 0);
	chk("used empty", ob_dma_used(512, 0, 0), 0);
	chk("avail empty", ob_dma_avail(512, 0, 0), 511);

	chk("used 1", ob_dma_used(512, 1, 0), 1);
	chk("avail 1", ob_dma_avail(512, 1, 0), 510);

	chk("full at 511", (long)ob_dma_full(512, 511, 0), 1);
	chk("used full", ob_dma_used(512, 511, 0), 511);
	chk("avail full", ob_dma_avail(512, 511, 0), 0);

	/* producer/consumer wraparound: head wrapped to 0, tail near the end. */
	chk("used wrap", ob_dma_used(512, 0, 511), 1);
	chk("full wrap", (long)ob_dma_full(512, 0, 511), 0);
	chk("avail wrap", ob_dma_avail(512, 0, 511), 510);

	/* walk a full lap and check used counts monotonically. */
	for (i = 1; i < 512; i++) {
		if (ob_dma_used(512, (u16)i, 0) != (u16)i) {
			printf("FAIL: used walk at %d\n", i);
			failures++;
			break;
		}
	}

	/* ---- descriptor offset / index conversion ---- */
	chk("offset(0)", ob_dma_desc_offset(0), 0);
	chk("offset(1)", ob_dma_desc_offset(1), 16);
	chk("offset(511)", ob_dma_desc_offset(511), 8176);
	for (i = 0; i < OB_DMA_RING_DESC_COUNT; i++) {
		if (ob_dma_desc_offset((u16)i) + OB_DMA_DESC_SIZE >
		    OB_DMA_RING_BYTES) {
			printf("FAIL: descriptor %d overruns the ring\n", i);
			failures++;
			break;
		}
	}

	/* ---- descriptor addressing / no overlap ---- */
	memset(mem, 0, sizeof(mem));
	memset(&r, 0, sizeof(r));
	r.desc_cpu = mem;
	r.n = OB_DMA_RING_DESC_COUNT;

	for (i = 0; i < OB_DMA_RING_DESC_COUNT; i++) {
		void *p = ob_dma_desc_at(&r, (u16)i);

		if (p != (void *)(mem + i * OB_DMA_DESC_SIZE)) {
			printf("FAIL: desc_at(%d) address mismatch\n", i);
			failures++;
			break;
		}
	}
	for (i = 0; i < OB_DMA_RING_DESC_COUNT; i++) {
		for (j = i + 1; j < OB_DMA_RING_DESC_COUNT; j++) {
			void *pi = ob_dma_desc_at(&r, (u16)i);
			void *pj = ob_dma_desc_at(&r, (u16)j);

			if (pj != (u8 *)pi + (j - i) * OB_DMA_DESC_SIZE ||
			    pi == pj) {
				printf("FAIL: descriptor %d overlaps %d\n", i, j);
				failures++;
				break;
			}
		}
	}

	/* ---- EOT helper (encoding only; never applied to hardware here) ---- */
	chk("eot last", (long)ob_dma_ring_eot(512, 511),
	    (long)OB_DMA_CTRL1_EOT);
	chk("no eot mid", (long)ob_dma_ring_eot(512, 510), 0);
	chk("no eot first", (long)ob_dma_ring_eot(512, 0), 0);

	if (failures) {
		printf("openbrcm dma tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm dma tests: PASS\n");
	return 0;
}

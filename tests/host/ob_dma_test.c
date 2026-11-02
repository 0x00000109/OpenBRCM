// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the DMA64 software model (M3.2/M3.4A).
 *
 * These exercise the pure descriptor encoding and ring arithmetic in
 * src/ob_dma.h with no hardware and no kernel API. RX and TX ring capacities
 * are independent (RX 256, TX 512) per the M3.4A RX proof.
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

/* generic ring invariants for an arbitrary count */
static void ring_math(const char *tag, u16 n)
{
	u16 last = (u16)(n - 1);
	u16 i;

	chk(tag, ob_dma_next(n, last), 0);
	chk("next(0)", ob_dma_next(n, 0), 1);
	chk("prev(0)", ob_dma_prev(n, 0), last);
	chk("prev(1)", ob_dma_prev(n, 1), 0);
	chk("empty", (long)ob_dma_empty(n, 0, 0), 1);
	chk("not full", (long)ob_dma_full(n, 0, 0), 0);
	chk("used empty", ob_dma_used(n, 0, 0), 0);
	chk("avail empty", ob_dma_avail(n, 0, 0), (u16)(n - 1));
	chk("full", (long)ob_dma_full(n, last, 0), 1);
	chk("used full", ob_dma_used(n, last, 0), last);
	chk("avail full", ob_dma_avail(n, last, 0), 0);
	chk("used wrap", ob_dma_used(n, 0, last), 1);
	chk("index_valid(last)", (long)ob_dma_index_valid(n, last), 1);
	chk("index_valid(n)", (long)ob_dma_index_valid(n, n), 0);

	for (i = 1; i < n; i++) {
		if (ob_dma_used(n, i, 0) != i) {
			printf("FAIL: %s used walk at %u\n", tag, i);
			failures++;
			break;
		}
	}
}

int main(void)
{
	struct ob_dma_desc d;
	struct ob_dma_backend_input backend_input;
	u8 mem[OB_DMA_RING_BYTES];
	struct ob_dma_ring r;
	struct ob_dma_desc *desc;
	dma_addr_t sample = 0x00000000fe0e6000ULL;
	int i, j;

	/* ---- compile-time geometry (independent RX/TX capacities) ---- */
	chk("sizeof desc", (long)sizeof(struct ob_dma_desc), 16);
	chk("RX count", OB_DMA_RING_DESC_COUNT_RX, 256);
	chk("TX count", OB_DMA_RING_DESC_COUNT_TX, 512);
	chk("RX active bytes", OB_DMA_RING_ACTIVE_BYTES_RX, 4096);
	chk("TX active bytes", OB_DMA_RING_ACTIVE_BYTES_TX, 8192);
	chk("alloc bytes", OB_DMA_RING_BYTES, 8192);
	chk("ring align", OB_DMA_RING_ALIGN, 8192);
	chk("ring_count(RX)", ob_dma_ring_count(OB_DMA_RING_RX), 256);
	chk("ring_count(TX)", ob_dma_ring_count(OB_DMA_RING_TX), 512);
	chk("invalid role rejected", (long)ob_dma_ring_role_valid((enum ob_dma_ring_role)2), 0);
	chk("invalid role count", ob_dma_ring_count((enum ob_dma_ring_role)2), 0);
	chk("invalid role bytes", ob_dma_ring_active_bytes((enum ob_dma_ring_role)2), 0);
	chk("RX geometry", (long)ob_dma_ring_geometry_valid(OB_DMA_RING_RX, 256), 1);
	chk("TX geometry", (long)ob_dma_ring_geometry_valid(OB_DMA_RING_TX, 512), 1);
	chk("wrong RX geometry", (long)ob_dma_ring_geometry_valid(OB_DMA_RING_RX, 512), 0);
	chk("zero geometry", (long)ob_dma_ring_geometry_valid(OB_DMA_RING_TX, 0), 0);
	chk("active_bytes(RX)", ob_dma_ring_active_bytes(OB_DMA_RING_RX), 4096);
	chk("active_bytes(TX)", ob_dma_ring_active_bytes(OB_DMA_RING_TX), 8192);
	chk("RX post init", OB_DMA_RX_POST_INIT, 64);
	chk("RX initial PTR offset", OB_DMA_RX_POST_INIT * 16, 0x400);

	/* ---- 32-bit host DMA window helpers ---- */
	chk("SFLAGS bit12 selects DMA64", ob_dma64_backend_selected(0x1000), 1);
	chk("SFLAGS without bit12 does not select DMA64",
	    ob_dma64_backend_selected(0x0fffu), 0);
	chk("SFLAGS other bits do not select DMA64",
	    ob_dma64_backend_selected(0x80000000u), 0);
	chk("unavailable wrapper sample stays unclassified",
	    ob_sflags_snapshot_classify(false, 0x1000u),
	    OB_DMA_BACKEND_UNKNOWN);
	chk("valid wrapper sample reports DMA64 predicate",
	    ob_sflags_snapshot_classify(true, 0x1000u),
	    OB_DMA_BACKEND_DMA64PROC);
	chk("valid wrapper sample reports DMA32 predicate",
	    ob_sflags_snapshot_classify(true, 0x0fffu),
	    OB_DMA_BACKEND_DMA32PROC);

	/* Synthetic classifier cases: the backend result is valid only when the
	 * observed SFLAGS word is tied to the selected D11 rev42 core and known
	 * TX attach context. These are not target SFLAGS observations.
	 */
	memset(&backend_input, 0, sizeof(backend_input));
	backend_input.selected_core_id = OB_DMA_BACKEND_D11_CORE_ID;
	backend_input.selected_core_revision = OB_DMA_BACKEND_D11_REVISION;
	backend_input.selected_core_index = 2;
	backend_input.sflags_core_id = OB_DMA_BACKEND_D11_CORE_ID;
	backend_input.sflags_core_revision = OB_DMA_BACKEND_D11_REVISION;
	backend_input.sflags_core_index = 2;
	backend_input.attach_context = OB_DMA_ATTACH_TX_FIFO2;
	backend_input.sflags_from_selected_wrapper = true;
	backend_input.sflags = OB_SI_SFLAGS_DMA64;
	chk("complete matching attachment selects dma64proc",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_DMA64PROC);
	backend_input.attach_context = OB_DMA_ATTACH_TX_FIFO0;
	chk("FIFO0 uses same conditional classifier",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_DMA64PROC);
	backend_input.attach_context = OB_DMA_ATTACH_TX_FIFO1;
	chk("FIFO1 uses same conditional classifier",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_DMA64PROC);
	backend_input.attach_context = OB_DMA_ATTACH_TX_FIFO3;
	chk("FIFO3 uses same conditional classifier",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_DMA64PROC);
	backend_input.attach_context = OB_DMA_ATTACH_TX_FIFO2;
	backend_input.sflags = 0;
	chk("complete matching attachment selects dma32proc",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_DMA32PROC);
	backend_input.sflags_core_index++;
	chk("mismatched SFLAGS core index unresolved",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_UNKNOWN);
	backend_input.sflags_core_index--;
	backend_input.selected_core_id++;
	chk("non-D11 attach core unresolved",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_UNKNOWN);
	backend_input.selected_core_id = OB_DMA_BACKEND_D11_CORE_ID;
	backend_input.selected_core_revision++;
	chk("wrong D11 revision unresolved",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_UNKNOWN);
	backend_input.selected_core_revision = OB_DMA_BACKEND_D11_REVISION;
	backend_input.sflags_core_revision++;
	chk("mismatched SFLAGS core revision unresolved",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_UNKNOWN);
	backend_input.sflags_core_revision--;
	backend_input.attach_context = OB_DMA_ATTACH_UNKNOWN;
	chk("unknown attach context unresolved",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_UNKNOWN);
	backend_input.attach_context = OB_DMA_ATTACH_TX_FIFO0;
	backend_input.sflags_from_selected_wrapper = false;
	chk("unproven SFLAGS origin unresolved",
	    ob_dma_backend_classify(&backend_input), OB_DMA_BACKEND_UNKNOWN);
	chk("null backend input unresolved",
	    ob_dma_backend_classify(NULL), OB_DMA_BACKEND_UNKNOWN);
	chk("window zero", (long)ob_dma_addr_in_window(0), 1);
	chk("window low", (long)ob_dma_addr_in_window(0x00000000fe0e6000ULL), 1);
	chk("window 4G boundary",
	    (long)ob_dma_addr_in_window(0x00000000ffffffffULL), 1);
	chk("window above 4G",
	    (long)ob_dma_addr_in_window(0x0000000100000000ULL), 0);
	chk("high32 low", ob_dma_addr_high32(0x00000000fe0e6000ULL), 0);
	chk("high32 high",
	    (long)ob_dma_addr_high32(0x00000001fe0e6000ULL), 1);

	/* ---- descriptor field encoding (explicit high word) ---- */
	ob_dma_desc_zero(&d);
	chk("zero ctrl1", (long)d.ctrl1, 0);
	chk("zero ctrl2", (long)d.ctrl2, 0);
	chk("zero addrlow", (long)d.addrlow, 0);
	chk("zero addrhigh", (long)d.addrhigh, 0);

	/* RX descriptor 0 example: ctrl1=0, ctrl2=2048, addrlow=pa, h=0x80000000 */
	ob_dma_desc_encode(&d, sample, OB_DMA_PCIE_H32, 0,
			   OB_DMA_RX_BUFSZ);
	chk("rx0 ctrl1", (long)ob_dma_desc_ctrl1(&d), 0);
	chk("rx0 ctrl2/len", (long)ob_dma_desc_len(&d), OB_DMA_RX_BUFSZ);
	chk("rx0 addrlow", (long)ob_dma_desc_addrlow(&d), (long)0xfe0e6000u);
	chk("rx0 addrhigh", (long)ob_dma_desc_addrhigh(&d),
	    (long)OB_DMA_PCIE_H32);

	/* RX descriptor 255 example: the only one with EOT. */
	ob_dma_desc_encode(&d, sample, OB_DMA_PCIE_H32,
			   ob_dma_ring_eot(OB_DMA_RING_DESC_COUNT_RX,
					   OB_DMA_RING_DESC_COUNT_RX - 1),
			   OB_DMA_RX_BUFSZ);
	chk("rx255 ctrl1 (EOT)", (long)ob_dma_desc_ctrl1(&d),
	    (long)OB_DMA_CTRL1_EOT);
	chk("rx255 addrhigh", (long)ob_dma_desc_addrhigh(&d),
	    (long)OB_DMA_PCIE_H32);

	/* len must be masked into OB_DMA_CTRL2_BC_MASK. */
	ob_dma_desc_encode(&d, 0, 0, 0, 0x7ffff);
	chk("len mask", (long)ob_dma_desc_len(&d), 0x7fff);

	/* Pinned sub_dcf8 contract, independent of the generic DMA encoder:
	 * normal address mode: DW0=flags, DW1=len, DW2=base+address,
	 * DW3=dataoffsethigh.
	 */
	ob_dma64_tx_desc_encode(&d, 0x10000000u, 0x12345678u,
				0xa0000000u, 0x8123u, 0x80000000u,
				false);
	chk("sub_dcf8 normal DW0", ob_dma_desc_ctrl1(&d), 0xa0000000u);
	chk("sub_dcf8 normal DW1", ob_dma_desc_len(&d), 0x123u);
	chk("sub_dcf8 normal DW2", ob_dma_desc_addrlow(&d), 0x22345678u);
	chk("sub_dcf8 normal DW3", ob_dma_desc_addrhigh(&d), 0x80000000u);

	/* Split mode takes address[31:30] into DW1[17:16] and adds only
	 * address[29:0] to dataoffset. Literal expected values are derived
	 * from the pinned instruction sequence, not the encoder under test.
	 */
	ob_dma64_tx_desc_encode(&d, 0x00002000u, 0xc1234567u,
				0x60000000u, 0x234u, 0x80000000u,
				false);
	chk("sub_dcf8 split DW0", ob_dma_desc_ctrl1(&d), 0x60000000u);
	chk("sub_dcf8 split DW1", le32_to_cpu(d.ctrl2), 0x00030234u);
	chk("sub_dcf8 split DW2", ob_dma_desc_addrlow(&d), 0x01236567u);
	chk("sub_dcf8 split DW3", ob_dma_desc_addrhigh(&d), 0x80000000u);

	/* With zero dataoffset, the historical branch does not split the top
	 * address bits even when they are set.
	 */
	ob_dma64_tx_desc_encode(&d, 0, 0xc1234567u, 0, 0x10000u,
				0x80000000u, false);
	chk("sub_dcf8 zero base no split DW1", le32_to_cpu(d.ctrl2), 0);
	chk("sub_dcf8 zero base wraps length", ob_dma_desc_addrlow(&d),
	    0xc1234567u);

	/* The optional parity is the reduction parity of all four descriptor
	 * dwords and occupies bit 18 of DW1 only when the DI parity flag is set.
	 */
	ob_dma64_tx_desc_encode(&d, 0x10000000u, 0x12345678u,
				0xa0000000u, 0x8123u, 0x80000000u,
				true);
	chk("sub_dcf8 even parity vector DW1", le32_to_cpu(d.ctrl2), 0x00000123u);
	ob_dma64_tx_desc_encode(&d, 0, 0, 0x1u, 0, 0, true);
	chk("sub_dcf8 odd parity DW0", ob_dma_desc_ctrl1(&d), 1);
	chk("sub_dcf8 odd parity sets bit18", le32_to_cpu(d.ctrl2),
	    OB_DMA_CTRL2_PARITY);

	/* ---- slot ownership metadata ---- */
	{
		struct ob_dma_slot s;
		struct sk_buff *owned = NULL;
		struct sk_buff *released = NULL;
		dma_addr_t owned_dma = 0, released_dma = 0;

		ob_dma_slot_init(&s);
		chk("slot skb cleared", (long)(s.skb == NULL), 1);
		chk_u64("slot dma cleared", s.dma, 0);
		chk("slot mapped cleared", (long)s.mapped, 0);
		chk("first slot claim", (long)ob_dma_slot_claim(&s,
			(struct sk_buff *)0x1, 0x2), 1);
		chk("double claim rejected", (long)ob_dma_slot_claim(&s,
			(struct sk_buff *)0x3, 0x4), 0);
		chk("claim retained owner", (long)(s.skb == (struct sk_buff *)0x1), 1);
		chk("first release", (long)ob_dma_slot_release(&s, &released,
			&released_dma), 1);
		chk("release skb", (long)(released == (struct sk_buff *)0x1), 1);
		chk_u64("release dma", released_dma, 0x2);
		chk("double release rejected", (long)ob_dma_slot_release(&s,
			&owned, &owned_dma), 0);
		chk("double release leaves output", (long)(owned == NULL), 1);
	}

	/* ---- independent ring arithmetic ---- */
	ring_math("RX", OB_DMA_RING_DESC_COUNT_RX);
	ring_math("TX", OB_DMA_RING_DESC_COUNT_TX);

	/* explicit per-role wrap checks */
	chk("RX next(255)", ob_dma_next(OB_DMA_RING_DESC_COUNT_RX, 255), 0);
	chk("TX next(511)", ob_dma_next(OB_DMA_RING_DESC_COUNT_TX, 511), 0);
	chk("zero ring next rejected", ob_dma_next(0, 0), 0);
	chk("out-of-range next rejected", ob_dma_next(4, 4), 0);
	chk("zero ring prev rejected", ob_dma_prev(0, 0), 0);
	chk("out-of-range prev rejected", ob_dma_prev(4, 4), 0);
	chk("invalid indices rejected", (long)ob_dma_ring_indices_valid(8, 8, 0), 0);
	chk("zero ring rejected", (long)ob_dma_ring_indices_valid(0, 0, 0), 0);
	chk("invalid not empty", (long)ob_dma_empty(8, 8, 0), 0);
	chk("invalid not full", (long)ob_dma_full(8, 8, 0), 0);
	chk("invalid used is zero", ob_dma_used(0, 0, 0), 0);
	chk("invalid avail is zero", ob_dma_avail(8, 8, 0), 0);

	/* ---- descriptor offset / index conversion ---- */
	chk("offset(0)", ob_dma_desc_offset(0), 0);
	chk("offset(1)", ob_dma_desc_offset(1), 16);
	chk("RX offset max", ob_dma_desc_offset(255), 4080);
	chk("TX offset max", ob_dma_desc_offset(511), 8176);
	for (i = 0; i < OB_DMA_RING_DESC_COUNT_RX; i++) {
		if (ob_dma_desc_offset((u16)i) + OB_DMA_DESC_SIZE >
		    OB_DMA_RING_ACTIVE_BYTES_RX) {
			printf("FAIL: RX descriptor %d overruns\n", i);
			failures++;
			break;
		}
	}

	/* ---- descriptor addressing / no overlap (RX uses first 256) ---- */
	memset(mem, 0, sizeof(mem));
	memset(&r, 0, sizeof(r));
	r.desc_cpu = mem;
	r.n = OB_DMA_RING_DESC_COUNT_RX;
	r.role = OB_DMA_RING_RX;
	desc = ob_dma_desc_at(&r, 0);
	chk("desc_at first valid", (long)(desc == (struct ob_dma_desc *)mem), 1);
	chk("desc_at count rejected", (long)(ob_dma_desc_at(&r, r.n) == NULL), 1);
	r.role = OB_DMA_RING_TX;
	chk("desc_at role geometry mismatch rejected",
	    (long)(ob_dma_desc_at(&r, 0) == NULL), 1);
	r.role = OB_DMA_RING_RX;
	r.desc_cpu = NULL;
	chk("desc_at null base rejected", (long)(ob_dma_desc_at(&r, 0) == NULL), 1);
	r.desc_cpu = mem;

	for (i = 0; i < OB_DMA_RING_DESC_COUNT_RX; i++) {
		void *p = ob_dma_desc_at(&r, (u16)i);

		if (p != (void *)(mem + i * OB_DMA_DESC_SIZE)) {
			printf("FAIL: desc_at(%d) address mismatch\n", i);
			failures++;
			break;
		}
	}
	for (i = 0; i < OB_DMA_RING_DESC_COUNT_RX; i++) {
		for (j = i + 1; j < OB_DMA_RING_DESC_COUNT_RX; j++) {
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
	chk("RX eot last", (long)ob_dma_ring_eot(OB_DMA_RING_DESC_COUNT_RX, 255),
	    (long)OB_DMA_CTRL1_EOT);
	chk("RX no eot 254", (long)ob_dma_ring_eot(OB_DMA_RING_DESC_COUNT_RX, 254), 0);
	chk("TX eot last", (long)ob_dma_ring_eot(OB_DMA_RING_DESC_COUNT_TX, 511),
	    (long)OB_DMA_CTRL1_EOT);
	chk("TX no eot 510", (long)ob_dma_ring_eot(OB_DMA_RING_DESC_COUNT_TX, 510), 0);
	chk("no eot 0", (long)ob_dma_ring_eot(OB_DMA_RING_DESC_COUNT_TX, 0), 0);

	if (failures) {
		printf("openbrcm dma tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm dma tests: PASS\n");
	return 0;
}

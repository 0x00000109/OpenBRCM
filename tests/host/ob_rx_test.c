// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — host unit tests for the FIFO0 RX model (M3.4B).
 *
 * Pure helpers only: STATUS0 -> index conversion, length validation, RX
 * descriptor construction, frame_control decode and exactly-once ownership.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/ob_rx.h"

static int failures;

static void chk(const char *what, long got, long exp)
{
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

int main(void)
{
	const dma_addr_t ring = 0x00000000fe0e6000ULL;
	struct ob_dma_desc d;
	struct ob_dma_slot s;
	struct sk_buff *skb = (struct sk_buff *)0x1234;
	dma_addr_t dma = 0xfe0e6000;

	/* ---- STATUS0 -> descriptor index ---- */
	chk("idx base", ob_rx_status_index((u32)ring, ring), 0);
	chk("idx 1", ob_rx_status_index((u32)ring + 16, ring), 1);
	chk("idx ptr(64)", ob_rx_status_index((u32)ring + 0x400, ring), 64);
	chk("idx 255", ob_rx_status_index((u32)ring + 255 * 16, ring), 255);
	chk("idx wrap 511",
	    ob_rx_status_index((u32)ring - 16, ring), 511);
	chk("idx ffffffff",
	    ob_rx_status_index(0xffffffffu, ring), 511);

	chk("index_ok 0", (long)ob_rx_index_ok(0, 256), 1);
	chk("index_ok 255", (long)ob_rx_index_ok(255, 256), 1);
	chk("index_ok 256", (long)ob_rx_index_ok(256, 256), 0);
	chk("index_ok 511", (long)ob_rx_index_ok(511, 256), 0);

	/* ---- RxFrameSize bounds ---- */
	chk("len min ok", (long)ob_rx_frame_len_ok(OB_RX_MIN_FRAME), 1);
	chk("len max ok", (long)ob_rx_frame_len_ok(OB_RX_MAX_FRAME), 1);
	chk("len 0 bad", (long)ob_rx_frame_len_ok(0), 0);
	chk("len 9 bad", (long)ob_rx_frame_len_ok(9), 0);
	chk("len 2011 bad", (long)ob_rx_frame_len_ok(OB_RX_MAX_FRAME + 1), 0);
	chk("len 0xffff bad", (long)ob_rx_frame_len_ok(0xffff), 0);
	chk("max frame value", OB_RX_MAX_FRAME, 2048 - 38);

	/* ---- RX descriptor construction ---- */
	ob_rx_desc_build(&d, dma, false);
	chk("d0 ctrl1", (long)ob_dma_desc_ctrl1(&d), 0);
	chk("d0 ctrl2", (long)ob_dma_desc_len(&d), 0x800);
	chk("d0 addrlow", (long)ob_dma_desc_addrlow(&d), (long)0xfe0e6000u);
	chk("d0 addrhigh", (long)ob_dma_desc_addrhigh(&d),
	    (long)OB_DMA_PCIE_H32);

	ob_rx_desc_build(&d, 0, true);
	chk("d255 ctrl1 EOT", (long)ob_dma_desc_ctrl1(&d),
	    (long)OB_DMA_CTRL1_EOT);
	chk("d255 ctrl2", (long)ob_dma_desc_len(&d), 0);
	chk("d255 addrhigh", (long)ob_dma_desc_addrhigh(&d),
	    (long)OB_DMA_PCIE_H32);

	/* 256-descriptor wrap */
	chk("next(255)", ob_dma_next(OB_DMA_RING_DESC_COUNT_RX, 255), 0);

	/* ---- 802.11 frame_control decode ---- */
	{
		u8 beacon[2] = { 0x80, 0x00 };	/* mgmt, subtype 8 */
		u8 data[2] = { 0x08, 0x00 };	/* data, subtype 0 */
		u8 ctl[2] = { 0x94, 0x00 };	/* control, subtype 9 (ACK) */
		u16 fc;

		fc = ob_rx_fc(beacon);
		chk("beacon fc", fc, 0x0080);
		chk("beacon type", ob_rx_fc_type(fc), 0);
		chk("beacon subtype", ob_rx_fc_subtype(fc), 8);

		fc = ob_rx_fc(data);
		chk("data type", ob_rx_fc_type(fc), 2);
		chk("data subtype", ob_rx_fc_subtype(fc), 0);

		fc = ob_rx_fc(ctl);
		chk("ctl type", ob_rx_fc_type(fc), 1);
		chk("ctl subtype", ob_rx_fc_subtype(fc), 9);
	}

	/* ---- exactly-once ownership ---- */
	ob_dma_slot_init(&s);
	chk("claim 1", (long)ob_dma_slot_claim(&s, skb, dma), 1);
	chk("claim 2 rejected", (long)ob_dma_slot_claim(&s, skb, dma), 0);
	{
		struct sk_buff *out_skb = NULL;
		dma_addr_t out_dma = 0;

		chk("release 1",
		    (long)ob_dma_slot_release(&s, &out_skb, &out_dma), 1);
		chk("release skb", (long)(out_skb == skb), 1);
		chk("release dma",
		    (long)(out_dma == dma), 1);
		chk("release 2 rejected",
		    (long)ob_dma_slot_release(&s, &out_skb, &out_dma), 0);
	}

	if (failures) {
		printf("openbrcm rx tests: %d FAILURES\n", failures);
		return 1;
	}
	printf("openbrcm rx tests: PASS\n");
	return 0;
}

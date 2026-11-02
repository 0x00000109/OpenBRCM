/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_rx_d11_rev42.h"

static unsigned int failures;

static void check(const char *name, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %d expected %d\n", name, got, expected);
		failures++;
	}
}

static void check_true(const char *name, int value)
{
	if (!value) {
		printf("FAIL: %s\n", name);
		failures++;
	}
}

int main(void)
{
	ob_rx_d11_u8 buffer[48];
	struct ob_rx_d11_rev42_length length = { 0 };
	struct ob_rx_d11_rev42_status status = { 0 };
	struct ob_rx_d11_rev42_chain_plan plan = { 0 };
	ob_rx_d11_u8 first[64] = { 0 }, cont1[64], cont2[64], output[128];
	struct ob_rx_d11_rev42_segment segments[3];
	ob_rx_d11_size output_length = 99;
	struct ob_rx_d11_rev42_input input = {
		.d11_core_revision = 42,
		.length_provenance = OB_RX_D11_LENGTH_VALIDATED,
		.payload_length = 8,
		.complete = 1,
	};
	struct ob_rx_d11_rev42_view view = {
		.payload = (const ob_rx_d11_u8 *)1,
		.payload_length = 99,
	};
	unsigned int i;

	for (i = 0; i < sizeof(buffer); i++)
		buffer[i] = (ob_rx_d11_u8)i;
	buffer[0] = 0xd0;
	buffer[1] = 0x07; /* 2000 bytes; fits after the 40-byte prefix. */
	check("decode single-buffer RxFrameSize",
	      ob_rx_d11_rev42_decode_length(42, buffer, 2, 2048, &length), 0);
	check("RxFrameSize is little-endian u16", (int)length.rx_frame_size,
	      2000);
	check("required bytes include 40-byte prefix",
	      (int)length.required_dma_bytes, 2040);
	check("single-buffer layout identified",
	      length.layout, OB_RX_D11_REV42_SINGLE_BUFFER);
	buffer[0] = 0xd9;
	buffer[1] = 0x07; /* 2009 bytes exceeds the 2008-byte single-buffer room. */
	check("decode chained RxFrameSize",
	      ob_rx_d11_rev42_decode_length(42, buffer, 2, 2048, &length), 0);
	check("required bytes retain payload length",
	      (int)length.required_dma_bytes, 2049);
	check("multibuffer requirement explicit",
	      length.layout, OB_RX_D11_REV42_NEEDS_CHAIN);
	check("wrong revision rejected",
	      ob_rx_d11_rev42_decode_length(41, buffer, 2, 2048, &length),
	      -EOPNOTSUPP);
	check("truncated size word rejected",
	      ob_rx_d11_rev42_decode_length(42, buffer, 1, 2048, &length),
	      -EMSGSIZE);
	check("too-small configured buffer rejected",
	      ob_rx_d11_rev42_decode_length(42, buffer, 2, 39, &length),
	      -EMSGSIZE);
	check("null header rejected",
	      ob_rx_d11_rev42_decode_length(42, NULL, 2, 2048, &length),
	      -EINVAL);
	check("null length output rejected",
	      ob_rx_d11_rev42_decode_length(42, buffer, 2, 2048, NULL),
	      -EINVAL);
	check("single chunk plan",
	      ob_rx_d11_rev42_plan_chain(42, 2008, 40, 2048, &plan), 0);
	check("exact first chunk length", (int)plan.first_segment_length, 2048);
	check("single chunk count", (int)plan.segment_count, 1);
	check("single chunk continuation", (int)plan.continuation_bytes, 0);
	check("two chunk plan",
	      ob_rx_d11_rev42_plan_chain(42, 2009, 40, 2048, &plan), 0);
	check("two chunk count", (int)plan.segment_count, 2);
	check("two chunk first includes prefix", (int)plan.first_segment_length,
	      2048);
	check("two chunk continuation bytes", (int)plan.continuation_bytes, 1);
	check("three chunk plan",
	      ob_rx_d11_rev42_plan_chain(42, 4100, 40, 2048, &plan), 0);
	check("three chunk count", (int)plan.segment_count, 3);
	check("three chunk continuation bytes", (int)plan.continuation_bytes,
	      2092);
	check("unsupported chain revision",
	      ob_rx_d11_rev42_plan_chain(41, 1, 40, 2048, &plan),
	      -EOPNOTSUPP);
	check("zero chunk capacity rejected",
	      ob_rx_d11_rev42_plan_chain(42, 1, 40, 0, &plan), -EINVAL);
	check("overflow rejected",
	      ob_rx_d11_rev42_plan_chain(42, (ob_rx_d11_size)-1, 40, 2048,
					 &plan), -EINVAL);
	/* Exact historical chunk rule: first carries prefix, continuations start
	 * at byte zero, and output is not published for a missing piece. */
	first[0] = 30;
	first[1] = 0;
	for (i = 40; i < sizeof(first); i++)
		first[i] = (ob_rx_d11_u8)(i - 40);
	for (i = 0; i < sizeof(cont1); i++) {
		cont1[i] = (ob_rx_d11_u8)(24 + i);
		cont2[i] = (ob_rx_d11_u8)(88 + i);
	}
	segments[0].data = first;
	segments[0].length = sizeof(first);
	segments[1].data = cont1;
	segments[1].length = 6;
	check("two-segment payload copy",
	      ob_rx_d11_rev42_copy_chain(42, segments, 2, 40, 64, output,
					 sizeof(output), &output_length), 0);
	check("two-segment output length", (int)output_length, 30);
	check("first segment starts after prefix", output[0], 0);
	check("continuation starts at data zero", output[24], 24);
	segments[1].length = 5;
	output_length = 99;
	check("truncated continuation rejected",
	      ob_rx_d11_rev42_copy_chain(42, segments, 2, 40, 64, output,
					 sizeof(output), &output_length), -EMSGSIZE);
	check("truncated chain has no published length", (int)output_length, 0);
	first[0] = 100;
	segments[1].length = sizeof(cont1);
	segments[2].data = cont2;
	segments[2].length = 12;
	check("three-segment payload copy",
	      ob_rx_d11_rev42_copy_chain(42, segments, 3, 40, 64, output,
					 sizeof(output), &output_length), 0);
	check("three-segment output length", (int)output_length, 100);
	check("third segment begins at expected offset", output[88], 88);
	check("missing third segment rejected",
	      ob_rx_d11_rev42_copy_chain(42, segments, 2, 40, 64, output,
					 sizeof(output), &output_length), -EMSGSIZE);

	check("rev42 payload view accepted",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &input,
					   &view), 0);
	check_true("payload begins after 40-byte rev42 prefix",
		   view.payload == buffer + 40);
	check("payload length preserved", (int)view.payload_length, 8);
	check("first frame byte preserved", view.payload[0], 40);
	check("last frame byte preserved", view.payload[7], 47);
	check("header bytes are not modified", buffer[0], 0xd9);

	input.d11_core_revision = 41;
	check("other revision rejected",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &input,
					   &view), -EOPNOTSUPP);
	check_true("failed result clears view", !view.payload &&
		   view.payload_length == 0);
	input.d11_core_revision = 42;
	input.length_provenance = OB_RX_D11_LENGTH_UNKNOWN;
	check("unproven payload length rejected",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &input,
					   &view), -ENODATA);
	input.length_provenance = OB_RX_D11_LENGTH_VALIDATED;
	input.complete = 0;
	check("incomplete payload rejected",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &input,
					   &view), -EMSGSIZE);
	input.complete = 1;
	check("shorter than full header rejected",
	      ob_rx_d11_rev42_payload_view(buffer, 39, &input, &view),
	      -EMSGSIZE);
	input.payload_length = 9;
	check("payload past DMA buffer rejected",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &input,
					   &view), -EMSGSIZE);
	input.payload_length = 0;
	check("zero frame length rejected",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &input,
					   &view), -EMSGSIZE);
	check("null buffer rejected",
	      ob_rx_d11_rev42_payload_view(NULL, sizeof(buffer), &input, &view),
	      -EINVAL);
	check("null metadata rejected",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), NULL, &view),
	      -EINVAL);
	check("null output rejected",
	      ob_rx_d11_rev42_payload_view(buffer, sizeof(buffer), &input, NULL),
	      -EINVAL);

	/* Raw wlc_recv fields and branches; semantic field names are withheld. */
	memset(buffer, 0, sizeof(buffer));
	buffer[4] = 0x10;
	buffer[5] = 0x03;
	buffer[0x10] = 0x05;
	buffer[0x12] = 0x01;
	check("raw status decode",
	      ob_rx_d11_rev42_decode_status(42, buffer, sizeof(buffer),
					    &status), 0);
	check("raw status word4", status.raw_word_4, 0x0310);
	check("word4 drop branch", status.drop_word4_branch, 1);
	check("raw byte16", status.raw_byte_16, 5);
	check("two-byte skip", status.skip_two_bytes, 2);
	check("byte16 drop branch", status.drop_byte16_branch, 1);
	check("byte18 aggregate branch", status.aggregate_branch, 1);
	check("conditional later packet pull length",
	      status.conditional_later_packet_pull_len, 6);
	check("offset after conditional pull on continuing branch",
	      status.offset_after_conditional_pull_on_continuing_path, 48);
	check("truncated status rejected",
	      ob_rx_d11_rev42_decode_status(42, buffer, 0x12, &status),
	      -EMSGSIZE);
	check("status from wrong D11 revision rejected",
	      ob_rx_d11_rev42_decode_status(41, buffer, sizeof(buffer), &status),
	      -EOPNOTSUPP);
	/* The +0x16 field was observed in two distinct predicates. Keep it raw:
	 * these branch outcomes are not channel/band metadata.
	 */
	buffer[0x16] = 0x0e;
	buffer[0x17] = 0xc0;
	check("extended raw status decoded",
	      ob_rx_d11_rev42_decode_status(42, buffer, sizeof(buffer),
					    &status), 0);
	check("raw word +0x16", status.raw_word_0x16, 0xc00e);
	check("raw +0x16 selector bits", status.raw_word_0x16_top2_selector, 3);
	check("raw byte +0x16 equals 0x0e predicate",
	      status.raw_byte_0x16_eq_0e, 1);
	check("short prefix excludes word +0x16",
	      ob_rx_d11_rev42_decode_status(42, buffer, 0x17, &status),
	      -EMSGSIZE);

	if (failures) {
		printf("ob_rx_d11_rev42 tests: %u failures\n", failures);
		return 1;
	}
	puts("ob_rx_d11_rev42 tests: PASS");
	return 0;
}

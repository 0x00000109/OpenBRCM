/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_channel.h"
#include "../../src/ob_phy.h"
#include "../../src/ob_radio_2069_lookup.h"

static int failures;

static void check(const char *name, int got, int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %d expected %d\n", name, got, expected);
		failures++;
	}
}

static void test_key_lookup_vectors(void)
{
	static const u8 expected_keys[OB_RADIO2069_REV4_ROW_COUNT] = {
		184, 185, 187, 188, 189, 192, 196, 207, 208, 209, 210, 212, 216,
		34, 36, 38, 40, 42, 44, 46, 48, 52, 54, 56, 58, 60, 62, 64,
		100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124,
		126, 128, 130, 132, 134, 136, 138, 140, 142, 144, 145, 146, 147,
		148, 149, 151, 153, 155, 157, 159, 161, 165,
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	};
	struct ob_radio2069_lookup_result result;
	unsigned int i;
	int ret;

	for (i = 0; i < OB_RADIO2069_REV4_ROW_COUNT; i++) {
		ret = ob_radio2069_rev4_lookup_key(expected_keys[i], &result);
		check("table key lookup", ret, 0);
		check("table key identity", result.key, expected_keys[i]);
		check("table row ordinal", result.row_index, i);
		check("table row byte offset", result.row_offset,
		      i * OB_RADIO2069_REV4_ROW_STRIDE);
	}

	ret = ob_radio2069_rev4_lookup_key(1, &result);
	check("key 1 lookup", ret, 0);
	check("key 1 row", result.row_index, 63);
	/* The admitted RE.DB table bytes place key 1 at row 63 * 0x74. */
	check("key 1 row offset", result.row_offset, 0x1c8c);
	check("key 1 row word validity", result.row_word2_valid, 1);
	check("key 1 row word", result.row_word2, 0x096c);

	ret = ob_radio2069_rev4_lookup_key(184, &result);
	check("first key lookup", ret, 0);
	check("first key row", result.row_index, 0);
	check("first key offset", result.row_offset, 0);

	ret = ob_radio2069_rev4_lookup_key(14, &result);
	check("last 2g key lookup", ret, 0);
	check("last 2g key row", result.row_index, 76);
	check("unknown row word is not fabricated", result.row_word2_valid, 0);

	ret = ob_radio2069_rev4_lookup_key(200, &result);
	check("key 200 miss", ret, -2);
	check("key 200 miss not marked found", result.found, 0);
	ret = ob_radio2069_rev4_lookup_key(204, &result);
	check("key 204 miss", ret, -2);
	check("key 204 miss not marked found", result.found, 0);
	check("null result rejected", ob_radio2069_rev4_lookup_key(1, NULL), -22);
}

static void test_runtime_chanspec_lookup(void)
{
	struct ob_radio2069_lookup_result result;
	u16 chanspec = 0;
	unsigned int channel;
	int ret;

	for (channel = 1; channel <= 14; channel++) {
		ret = ob_acphy_encode_2g_20mhz((u16)channel,
			(u16)ob_channel_to_freq((u16)channel, OB_BAND_2GHZ),
			&chanspec);
		check("encode runtime 2g20 channel", ret, 0);
		ret = ob_radio2069_rev4_lookup_2g20(chanspec, &result);
		check("runtime chanspec lookup", ret, 0);
		check("runtime low-byte key", result.key, channel);
		check("runtime row order", result.row_index, 62 + channel);
	}

	check("reject zero chanspec",
	      ob_radio2069_rev4_lookup_2g20(0, &result), -95);
	check("reject unsupported width/band bits",
	      ob_radio2069_rev4_lookup_2g20(0x1801, &result), -95);
	check("reject channel zero",
	      ob_radio2069_rev4_lookup_2g20(0x1000, &result), -95);
	check("reject 5 GHz chanspec",
	      ob_radio2069_rev4_lookup_2g20(0xc024, &result), -95);
}

static void test_lookup_lifecycle_stop_boundary(void)
{
	struct ob_phy_init_lifecycle lifecycle = { 0 };

	check("lifecycle begin", ob_phy_init_begin(&lifecycle), 0);
	check("pre-radio ready", ob_phy_init_advance(&lifecycle,
	      OB_PHY_INIT_PRE_RADIO_READY), 0);
	check("radio stage begin", ob_phy_init_begin_radio(&lifecycle), 0);
	check("lookup cannot complete before LPF",
	      ob_phy_init_post_lpf_lookup_complete(&lifecycle, true, 1, 63,
						  0x1c8c), -95);
	lifecycle.radio_prefix_complete = true;
	lifecycle.analog_tx_lpf_complete = true;
	check("record selected row", ob_phy_init_post_lpf_lookup_complete(
	      &lifecycle, true, 1, 63, 0x1c8c), 0);
	check("lookup completion recorded", lifecycle.post_lpf_lookup_complete, 1);
	check("lookup key retained", lifecycle.post_lpf_lookup_key, 1);
	check("lookup row retained", lifecycle.post_lpf_lookup_row, 63);
	check("hit stops at row payload boundary",
	      ob_phy_init_block(&lifecycle,
		OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD), 0);
	check("row payload boundary is non-operational",
	      ob_phy_init_is_operational(&lifecycle), 0);
	check("row payload boundary cannot complete radio",
	      ob_phy_init_advance(&lifecycle, OB_PHY_INIT_RADIO_COMPLETE), -95);

	lifecycle = (struct ob_phy_init_lifecycle){ 0 };
	check("miss lifecycle begin", ob_phy_init_begin(&lifecycle), 0);
	check("miss pre-radio ready", ob_phy_init_advance(&lifecycle,
	      OB_PHY_INIT_PRE_RADIO_READY), 0);
	check("miss radio stage begin", ob_phy_init_begin_radio(&lifecycle), 0);
	lifecycle.radio_prefix_complete = true;
	lifecycle.analog_tx_lpf_complete = true;
	check("record lookup miss", ob_phy_init_post_lpf_lookup_complete(
	      &lifecycle, false, 200, 0xff, 0xffff), 0);
	check("miss cannot select row", lifecycle.post_lpf_lookup_found, 0);
	check("miss stops at unresolved exit semantics",
	      ob_phy_init_block(&lifecycle,
		OB_PHY_BLOCK_POST_LPF_LOOKUP_MISS), 0);
	check("miss cannot claim operational",
	      ob_phy_init_is_operational(&lifecycle), 0);
}

int main(void)
{
	test_key_lookup_vectors();
	test_runtime_chanspec_lookup();
	test_lookup_lifecycle_stop_boundary();
	if (failures) {
		printf("openbrcm radio 2069 lookup: %d FAILURES\n", failures);
		return 1;
	}
	puts("openbrcm radio 2069 lookup: PASS");
	return 0;
}

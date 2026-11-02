/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy_txpwr_index.h"

#define MAX_EVENTS 96u

enum fake_kind { PHY_R, PHY_W, TABLE_R, TABLE_W };

struct fake_event {
	enum fake_kind kind;
	u32 site;
	u16 reg;
	u8 table;
	s16 offset;
	u8 count;
	u8 width;
	u8 length;
	u16 value;
	u8 bytes[6];
};

struct fake_backend {
	struct fake_event events[MAX_EVENTS];
	u16 event_count;
	u16 fail_at;
	u16 phy_019e;
	u8 table_sample[6];
};

static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static struct fake_event *new_event(struct fake_backend *fake,
				    enum fake_kind kind, u32 site)
{
	struct fake_event *event;

	if (!fake || fake->event_count >= MAX_EVENTS)
		return NULL;
	event = &fake->events[fake->event_count++];
	event->kind = kind;
	event->site = site;
	return event;
}

static int fake_phy_read(void *ctx, u32 site, u16 reg, u16 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, PHY_R, site);

	if (!event || !value)
		return -EINVAL;
	event->reg = reg;
	if (fake->fail_at == fake->event_count)
		return -EIO;
	if (reg != 0x019e)
		return -EINVAL;
	*value = fake->phy_019e;
	event->value = *value;
	return 0;
}

static int fake_phy_write(void *ctx, u32 site, u16 reg, u16 value)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, PHY_W, site);

	if (!event)
		return -EINVAL;
	event->reg = reg;
	event->value = value;
	if (fake->fail_at == fake->event_count)
		return -EIO;
	if (reg != 0x019e)
		return -EINVAL;
	fake->phy_019e = value;
	return 0;
}

static int fake_table_read(void *ctx, u32 site, u8 table, s16 offset,
			   u8 count, u8 width, u8 *payload, u8 length)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, TABLE_R, site);

	if (!event || !payload || length > sizeof(event->bytes))
		return -EINVAL;
	event->table = table;
	event->offset = offset;
	event->count = count;
	event->width = width;
	event->length = length;
	if (fake->fail_at == fake->event_count)
		return -EIO;
	if (table != 0x20 || count != 1 || width != 48 || length != 6)
		return -EINVAL;
	memcpy(payload, fake->table_sample, 6);
	memcpy(event->bytes, payload, 6);
	return 0;
}

static int fake_table_write(void *ctx, u32 site, u8 table, u16 offset,
			    u8 count, u8 width, const u8 *payload,
			    u8 length)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, TABLE_W, site);

	if (!event || !payload || length > sizeof(event->bytes))
		return -EINVAL;
	event->table = table;
	event->offset = (s16)offset;
	event->count = count;
	event->width = width;
	event->length = length;
	memcpy(event->bytes, payload, length);
	if (fake->fail_at == fake->event_count)
		return -EIO;
	if (count != 1 || width != 16 || length != 2)
		return -EINVAL;
	return 0;
}

static const struct ob_phy_txpwr_index_ops fake_ops = {
	.phy_read16 = fake_phy_read,
	.phy_write16 = fake_phy_write,
	.table_read = fake_table_read,
	.table_write = fake_table_write,
};

static struct ob_phy_txpwr_index_input input_for(u8 mask, u8 bound,
						  s8 index, u8 *indices)
{
	struct ob_phy_txpwr_index_input input = {
		.explicitly_synthetic = true,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.phy_type = 0x0b,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_class = 0,
		.core_mask = mask,
		.core_bound = bound,
		.power_index = index,
		.nested_indices = indices,
		.nested_indices_length = indices ? bound : 0,
	};

	return input;
}

static void seed(struct fake_backend *fake)
{
	memset(fake, 0, sizeof(*fake));
	fake->phy_019e = 0xa5a4;
	fake->table_sample[0] = 0x10;
	fake->table_sample[1] = 0x21;
	fake->table_sample[2] = 0x32;
	fake->table_sample[3] = 0x43;
	fake->table_sample[4] = 0x54;
	fake->table_sample[5] = 0x65;
}

static void expect_event(const struct fake_backend *fake, u16 index,
			enum fake_kind kind, u32 site)
{
	if (index >= fake->event_count) {
		printf("FAIL: missing callback %u\n", index + 1);
		failures++;
		return;
	}
	check("event kind", fake->events[index].kind, kind);
	check("event site", fake->events[index].site, site);
}

static void test_single_core_exact_trace(void)
{
	u8 indices[1] = { 0x55 };
	struct ob_phy_txpwr_index_input input = input_for(1, 1, -7, indices);
	struct ob_phy_txpwr_index_result result;
	struct fake_backend fake;
	static const enum fake_kind kinds[21] = {
		PHY_R, PHY_R, PHY_W, PHY_R, PHY_R, PHY_W, TABLE_R,
		PHY_R, PHY_W, TABLE_W, TABLE_W, TABLE_W, PHY_R, PHY_R,
		PHY_W, TABLE_W, TABLE_W, PHY_R, PHY_W, PHY_R, PHY_W,
	};
	static const u32 sites[21] = {
		0x9ce1e, 0x9ce39, 0x9ce39, 0x986ac, 0x986c6, 0x986c6,
		0x986e6, 0x9873f, 0x9873f, 0x9cea3, 0x9cec7, 0x9ceeb,
		0x9c535, 0x9c54f, 0x9c54f, 0x9c573, 0x9c594, 0x9c5b1,
		0x9c5b1, 0x9cf3f, 0x9cf3f,
	};
	unsigned int i;

	seed(&fake);
	check("single-core return", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), 0);
	check("single-core status", result.status,
	      OB_PHY_TXPWR_INDEX_SYNTHETIC_COMPLETE);
	check("single-core logical events", result.logical_event_count, 16);
	check("single-core callback events", result.callback_count, 21);
	check("single-core selected cores", result.selected_core_count, 1);
	check("single-core normal return", result.terminal_boundary, 0x9cf52);
	check("no hardware ready", result.hardware_ready, 0);
	check("PHY save/restore", fake.phy_019e, 0xa5a4);
	check("software index handoff", indices[0], (u8)-7);
	for (i = 0; i < 21; i++)
		expect_event(&fake, (u16)i, kinds[i], sites[i]);
	check("TABLE_READ id", fake.events[6].table, 0x20);
	check("TABLE_READ signed offset", fake.events[6].offset, -7);
	check("TABLE_READ count", fake.events[6].count, 1);
	check("TABLE_READ width", fake.events[6].width, 48);
	check("TABLE_WRITE first id", fake.events[9].table, 7);
	check("TABLE_WRITE first offset", fake.events[9].offset, 0x100);
	check("TABLE_WRITE second offset", fake.events[10].offset, 0x103);
	check("TABLE_WRITE third offset", fake.events[11].offset, 0x106);
	check("packed word 0 low", fake.events[9].bytes[0], 0x21);
	check("packed word 0 high", fake.events[9].bytes[1], 0x32);
	check("packed word 1 low", fake.events[10].bytes[0], 0x43);
	check("packed word 1 high", fake.events[10].bytes[1], 0x54);
	check("packed word 2 low", fake.events[11].bytes[0], 0x65);
	check("packed word 2 high truncation", fake.events[11].bytes[1], 0);
	check("sub_9c4e4 low payload", fake.events[15].bytes[0], 0x10);
	check("sub_9c4e4 high payload", fake.events[15].bytes[1], 0);
	check("sub_9c4e4 first offset", fake.events[15].offset, 0x63);
	check("sub_9c4e4 second offset", fake.events[16].offset, 0x73);
}

static void test_two_core_persistent_state_and_offsets(void)
{
	u8 indices[2] = { 0x11, 0x22 };
	struct ob_phy_txpwr_index_input input = input_for(3, 2, 127, indices);
	struct ob_phy_txpwr_index_result result;
	struct fake_backend fake;

	seed(&fake);
	check("two-core return", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), 0);
	check("two-core logical events", result.logical_event_count, 29);
	check("two-core callback events", result.callback_count, 37);
	check("two-core selected", result.selected_core_count, 2);
	check("two-core final PHY state", fake.phy_019e, 0xa5a4);
	check("core0 index state", indices[0], 127);
	check("core1 index state", indices[1], 127);
	check("second TABLE_READ site", fake.events[22].site, 0x986e6);
	check("core1 table7 offset", fake.events[25].offset, 0x101);
	check("core1 table0c first offset", fake.events[31].offset, 0x67);
	check("core1 table0c second offset", fake.events[32].offset, 0x77);
}

static void test_four_core_bound_and_offset_arrays(void)
{
	u8 indices[4] = { 0x10, 0x20, 0x30, 0x40 };
	struct ob_phy_txpwr_index_input input = input_for(0x0f, 4, -128, indices);
	struct ob_phy_txpwr_index_result result;
	struct fake_backend fake;

	seed(&fake);
	check("four-core return", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), 0);
	check("four-core logical events", result.logical_event_count, 55);
	check("four-core callbacks", result.callback_count, 69);
	check("four-core selected", result.selected_core_count, 4);
	check("four-core final PHY state", fake.phy_019e, 0xa5a4);
	check("four-core software index", indices[3], (u8)-128);
	check("core3 TABLE7 first offset", fake.events[57].offset, 0x103);
	check("core3 TABLE7 last offset", fake.events[59].offset, 0x109);
	check("core3 TABLE0c first offset", fake.events[63].offset, 0x6f);
	check("core3 TABLE0c second offset", fake.events[64].offset, 0x7f);
}

static void test_core1_only_and_runtime_table_sample_variation(void)
{
	u8 indices[2] = { 0x11, 0x22 };
	struct ob_phy_txpwr_index_input input = input_for(2, 2, 5, indices);
	struct ob_phy_txpwr_index_result result;
	struct fake_backend fake;

	seed(&fake);
	fake.phy_019e = 0x7a06;
	fake.table_sample[0] = 0x91;
	fake.table_sample[1] = 0x82;
	fake.table_sample[2] = 0x73;
	fake.table_sample[3] = 0x64;
	fake.table_sample[4] = 0x55;
	fake.table_sample[5] = 0x46;
	check("core1-only return", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), 0);
	check("core1-only callback count", result.callback_count, 21);
	check("core1-only selected", result.selected_core_count, 1);
	check("core0 software state unchanged", indices[0], 0x11);
	check("core1 software state changed", indices[1], 5);
	check("core1 TABLE_READ signed offset", fake.events[6].offset, 5);
	check("core1 table7 first offset", fake.events[9].offset, 0x101);
	check("core1 table7 second offset", fake.events[10].offset, 0x104);
	check("core1 table7 third offset", fake.events[11].offset, 0x107);
	check("core1 table0c first offset", fake.events[15].offset, 0x67);
	check("core1 table0c second offset", fake.events[16].offset, 0x77);
	check("runtime sample changes first payload low",
	      fake.events[9].bytes[0], 0x82);
	check("runtime sample changes first payload high",
	      fake.events[9].bytes[1], 0x73);
	check("runtime sample changes sub9c4e4 payload",
	      fake.events[15].bytes[0], 0x91);
	check("saved set-bit state restored", fake.phy_019e, 0x7a06);
}

static void test_zero_core_still_saves_and_restores(void)
{
	struct ob_phy_txpwr_index_input input = input_for(0, 2, 0, NULL);
	struct ob_phy_txpwr_index_result result;
	struct fake_backend fake;

	seed(&fake);
	check("zero-core return", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), 0);
	check("zero-core logical events", result.logical_event_count, 3);
	check("zero-core callbacks", result.callback_count, 5);
	check("zero-core selected", result.selected_core_count, 0);
	check("zero-core no table access", fake.event_count, 5);
	check("zero-core restored", fake.phy_019e, 0xa5a4);
}

static void test_signed_index_boundaries(void)
{
	const s8 values[] = { (s8)-128, (s8)127 };
	unsigned int i;

	for (i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
		u8 indices[1] = { 0 };
		struct ob_phy_txpwr_index_input input =
			input_for(1, 1, values[i], indices);
		struct ob_phy_txpwr_index_result result;
		struct fake_backend fake;

		seed(&fake);
		check("boundary return", ob_phy_txpwr_by_index_test_only(
			&fake_ops, &fake, &input, &result), 0);
		check("signed TABLE offset", fake.events[6].offset, values[i]);
		check("signed byte writeback", indices[0], (u8)values[i]);
	}
}

static void test_fail_stop_at_every_four_core_callback(void)
{
	unsigned int fail_at;

	for (fail_at = 1; fail_at <= 69; fail_at++) {
		u8 indices[4] = { 0x12, 0x34, 0x56, 0x78 };
		struct ob_phy_txpwr_index_input input =
			input_for(0x0f, 4, -3, indices);
		struct ob_phy_txpwr_index_result result;
		struct fake_backend fake;
		int ret;

		seed(&fake);
		fake.fail_at = (u16)fail_at;
		ret = ob_phy_txpwr_by_index_test_only(&fake_ops, &fake,
							     &input, &result);
		check("injected error propagated", ret, -EIO);
		check("failed status", result.status,
		      OB_PHY_TXPWR_INDEX_FAILED_BACKEND);
		check("no callback after failure", fake.event_count, fail_at);
		check("reported callback count", result.callback_count, fail_at);
		check("failed source site",
		      result.failed_site, fake.events[fail_at - 1].site);
		check("failure never ready", result.hardware_ready, 0);
	}
}

static void test_invalid_profile_stops_before_callbacks(void)
{
	u8 indices[1] = { 0x22 };
	struct ob_phy_txpwr_index_input input = input_for(1, 1, 4, indices);
	struct ob_phy_txpwr_index_result result;
	struct fake_backend fake;

	seed(&fake);
	input.explicitly_synthetic = false;
	check("non-synthetic rejected", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), -EINVAL);
	check("invalid has no callback", fake.event_count, 0);
	input.explicitly_synthetic = true;
	input.chip_id = 0x4360;
	check("BCM4360 rejected", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), -EINVAL);
	check("BCM4360 no callback", fake.event_count, 0);
	input = input_for(1, 1, 4, NULL);
	check("missing mutable state rejected", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), -EINVAL);
	check("missing state no callback", fake.event_count, 0);
	input = input_for(1, 5, 4, indices);
	check("out-of-range loop rejected", ob_phy_txpwr_by_index_test_only(
		&fake_ops, &fake, &input, &result), -EINVAL);
	check("bad loop no callback", fake.event_count, 0);
}

static void print_trace(u8 mask, u8 bound, s8 index, u16 initial_phy,
			const u8 sample[6])
{
	u8 indices[OB_PHY_TXPWR_INDEX_MAX_CORES] = { 0x55, 0x56, 0x57, 0x58 };
	struct ob_phy_txpwr_index_input input = input_for(mask, bound, index, indices);
	struct ob_phy_txpwr_index_result result;
	struct fake_backend fake;
	unsigned int i;

	seed(&fake);
	fake.phy_019e = initial_phy;
	memcpy(fake.table_sample, sample, sizeof(fake.table_sample));
	if (ob_phy_txpwr_by_index_test_only(&fake_ops, &fake, &input, &result))
		return;
	puts("[");
	for (i = 0; i < fake.event_count; i++) {
		const struct fake_event *e = &fake.events[i];
		const char *kind = e->kind == PHY_R ? "PHY_READ" :
			e->kind == PHY_W ? "PHY_WRITE" :
			e->kind == TABLE_R ? "TABLE_READ" : "TABLE_WRITE";

		printf("{\"kind\":\"%s\",\"site\":%u,\"reg\":%u,"
		       "\"table\":%u,\"offset\":%d,\"count\":%u,"
		       "\"width\":%u,\"value\":%u,\"payload\":[",
		       kind, e->site, e->reg, e->table, e->offset, e->count,
		       e->width, e->value);
		for (unsigned int j = 0; j < e->length; j++)
			printf("%s%u", j ? "," : "", e->bytes[j]);
		printf("]}%s\n", i + 1 == fake.event_count ? "" : ",");
	}
	puts("]");
}

int main(int argc, char **argv)
{
	static const u8 sample[6] = { 0x10, 0x21, 0x32, 0x43, 0x54, 0x65 };

	if (argc == 2 && !strcmp(argv[1], "--trace")) {
		print_trace(1, 1, -7, 0xa5a4, sample);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-two-core")) {
		print_trace(3, 2, 127, 0xa5a4, sample);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-core1")) {
		static const u8 core1_sample[6] = {
			0x91, 0x82, 0x73, 0x64, 0x55, 0x46,
		};
		print_trace(2, 2, 5, 0x7a06, core1_sample);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "--trace-four-core")) {
		print_trace(0x0f, 4, -128, 0xa5a4, sample);
		return 0;
	}
	test_single_core_exact_trace();
	test_two_core_persistent_state_and_offsets();
	test_four_core_bound_and_offset_arrays();
	test_core1_only_and_runtime_table_sample_variation();
	test_zero_core_still_saves_and_restores();
	test_signed_index_boundaries();
	test_fail_stop_at_every_four_core_callback();
	test_invalid_profile_stops_before_callbacks();
	if (failures) {
		printf("ob_phy_txpwr_index_test: %u failure(s)\n", failures);
		return 1;
	}
	puts("ob_phy_txpwr_index_test: PASS");
	return 0;
}

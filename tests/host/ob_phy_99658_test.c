/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/ob_phy_99658.h"

#define MAX_RECORDS 16u

struct table_record {
	u32 site;
	u8 argument;
	u8 chain;
	u8 table;
	u8 offset;
	u8 count;
	u8 width;
	u8 length;
	u8 bytes[OB_PHY_99658_MAX_TRANSFER_BYTES];
};

struct fake {
	struct table_record records[MAX_RECORDS];
	u8 count;
	u8 fail_at;
};

static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static int fake_table_write(void *ctx, u32 site, u8 argument, u8 chain,
			    u8 table, u8 offset, u8 count, u8 width,
			    const u8 *payload, u8 length)
{
	struct fake *f = ctx;
	struct table_record *r;

	if (!f || !payload || f->count == MAX_RECORDS ||
	    length > OB_PHY_99658_MAX_TRANSFER_BYTES)
		return -EINVAL;
	r = &f->records[f->count++];
	r->site = site;
	r->argument = argument;
	r->chain = chain;
	r->table = table;
	r->offset = offset;
	r->count = count;
	r->width = width;
	r->length = length;
	memcpy(r->bytes, payload, length);
	return f->fail_at == f->count ? -EIO : 0;
}

static const struct ob_phy_99658_ops fake_ops = {
	.table_write = fake_table_write,
};

static struct ob_phy_99658_input input_for(u8 arg)
{
	struct ob_phy_99658_input in = {
		.explicitly_synthetic = true,
		.policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED,
		.chip_id = 0x4352, .chip_revision = 3, .phy_revision = 0x0a,
		.radio_id = 0x2069, .radio_revision = 4, .radio_class = 0,
		.chain_count = 2, .argument = arg,
		.chanspec_present = true, .chanspec = 0x1001,
		.table_offset_present = true,
		.table_offset = arg == 1 ? 8 : 16,
		.transfer_count_present = true,
		.transfer_count = arg == 1 ? 6 : 7,
		.nested_66a_present = true, .nested_66a = 2,
		.nested_66b_present = true, .nested_66b = 2,
		.nested_66e_present = true, .nested_66e = 0,
		.nested_340_present = true, .nested_340 = 1,
		.nested_3e0_present = true, .nested_3e0 = 7,
		.source_id = arg == 1 ? OB_PHY_99658_ARG1_CHANSPEC_LOW :
			OB_PHY_99658_ARG2_LOW_66E_ZERO,
	};
	return in;
}

static void test_argument1_payloads(void)
{
	static const u8 source[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
	static const u8 expected_a[6] = { 0x20, 0x20, 0x30, 0x40,
					  0x40, 0x40 };
	static const u8 expected_b[6] = { 1, 1, 2, 3, 3, 3 };
	struct ob_phy_99658_input in = input_for(1);
	struct ob_phy_99658_result result;
	struct fake f = { 0 };
	unsigned int i;

	in.source_bytes = source;
	in.source_length = sizeof(source);
	check("arg1 return", ob_phy_acphy_99658_test_only(&fake_ops, &f, &in,
							 &result), 0);
	check("arg1 result", result.kind, OB_PHY_99658_HOST_SIMULATION_COMPLETE);
	check("arg1 writes", result.table_api_write_count, 4);
	check("arg1 scratch length is actual transfer", result.scratch_copies[0].length, 6);
	check("arg1 copied state offset", result.copied_state_offset, 0x0658);
	check("arg1 copied state value", result.copied_state_value, 2);
	check("arg1 scratch copies", result.scratch_copy_count, 4);
	check("arg1 scratch copy 0 offset",
	      result.scratch_copies[0].destination_offset, 0x0474);
	check("arg1 scratch copy 1 offset",
	      result.scratch_copies[1].destination_offset, 0x04b0);
	check("arg1 scratch copy 2 offset",
	      result.scratch_copies[2].destination_offset, 0x04ec);
	check("arg1 scratch copy 3 offset",
	      result.scratch_copies[3].destination_offset, 0x0528);
	check("arg1 scratch copy length", result.scratch_copies[0].length, 6);
	if (memcmp(result.scratch_copies[0].bytes, expected_a, 6)) failures++;
	if (memcmp(result.scratch_copies[1].bytes, expected_b, 6)) failures++;
	for (i = 0; i < 4; i++) {
		check("arg1 site", f.records[i].site, i % 2 ? 0x998aa : 0x99875);
		check("arg1 width", f.records[i].width, 8);
		check("arg1 count", f.records[i].count, 6);
		check("arg1 offset", f.records[i].offset, 8);
		check("arg1 payload length", f.records[i].length, 6);
	}
	check("arg1 table chain0-A", f.records[0].table, 0x44);
	check("arg1 table chain0-B", f.records[1].table, 0x45);
	check("arg1 table chain1-A", f.records[2].table, 0x64);
	check("arg1 table chain1-B", f.records[3].table, 0x65);
	if (memcmp(f.records[0].bytes, expected_a, sizeof(expected_a))) failures++;
	if (memcmp(f.records[1].bytes, expected_b, sizeof(expected_b))) failures++;
	if (memcmp(f.records[2].bytes, expected_a, sizeof(expected_a))) failures++;
	if (memcmp(f.records[3].bytes, expected_b, sizeof(expected_b))) failures++;
	check("arg1 no historical exactness", result.historical_exact, 0);
	check("arg1 no hardware ready", result.hardware_ready, 0);
}

static void test_argument2_payloads(void)
{
	static const u8 source[7] = { 0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97 };
	static const u8 expected_a[7] = {
		0x42, 0x42, 0x53, 0x64, 0x64, 0x64, 0x64,
	};
	static const u8 expected_b[7] = { 1, 1, 2, 3, 3, 3, 3 };
	struct ob_phy_99658_input in = input_for(2);
	struct ob_phy_99658_result result;
	struct fake f = { 0 };
	unsigned int i;

	in.source_bytes = source;
	in.source_length = sizeof(source);
	check("arg2 return", ob_phy_acphy_99658_test_only(&fake_ops, &f, &in,
							 &result), 0);
	check("arg2 result", result.kind, OB_PHY_99658_HOST_SIMULATION_COMPLETE);
	check("arg2 writes", result.table_api_write_count, 4);
	check("arg2 scratch length is actual transfer", result.scratch_copies[0].length, 7);
	check("arg2 copied state offset", result.copied_state_offset, 0x0659);
	check("arg2 copied state value", result.copied_state_value, 2);
	check("arg2 scratch copies", result.scratch_copy_count, 4);
	check("arg2 scratch copy 0 offset",
	      result.scratch_copies[0].destination_offset, 0x047e);
	check("arg2 scratch copy 1 offset",
	      result.scratch_copies[1].destination_offset, 0x04ba);
	check("arg2 scratch copy 2 offset",
	      result.scratch_copies[2].destination_offset, 0x04f6);
	check("arg2 scratch copy 3 offset",
	      result.scratch_copies[3].destination_offset, 0x0532);
	check("arg2 scratch copy length", result.scratch_copies[0].length, 7);
	if (memcmp(result.scratch_copies[0].bytes, expected_a, 7)) failures++;
	if (memcmp(result.scratch_copies[1].bytes, expected_b, 7)) failures++;
	for (i = 0; i < 4; i++) {
		check("arg2 width", f.records[i].width, 8);
		check("arg2 count", f.records[i].count, 7);
		check("arg2 offset", f.records[i].offset, 16);
		check("arg2 payload length", f.records[i].length, 7);
	}
	if (memcmp(f.records[0].bytes, expected_a, sizeof(expected_a))) failures++;
	if (memcmp(f.records[1].bytes, expected_b, sizeof(expected_b))) failures++;
	if (memcmp(f.records[2].bytes, expected_a, sizeof(expected_a))) failures++;
	if (memcmp(f.records[3].bytes, expected_b, sizeof(expected_b))) failures++;

	/* The 0x3e0 threshold selects base 4 at >= 10; delta is base minus
	 * nested+0x66b (clamped at zero), and later entries repeat delta. */
	in.nested_3e0 = 10;
	memset(&f, 0, sizeof(f));
	check("arg2 threshold return", ob_phy_acphy_99658_test_only(&fake_ops,
								 &f, &in, &result), 0);
	check("arg2 threshold tail", f.records[0].bytes[5], 0x53);
	check("arg2 threshold index", f.records[1].bytes[6], 2);
}

static void test_strict_missing_and_failstop(void)
{
	static const u8 source[6] = { 0, 1, 2, 3, 4, 5 };
	struct ob_phy_99658_input in = input_for(1);
	struct ob_phy_99658_result result;
	struct fake f = { 0 };
	int ret;
	unsigned int fail_at;

	in.source_bytes = source;
	in.source_length = sizeof(source);
	in.policy = OB_PHY_99658_HISTORICAL_STRICT;
	ret = ob_phy_acphy_99658_test_only(&fake_ops, &f, &in, &result);
	check("strict unresolved input blocked", ret, -EAGAIN);
	check("strict status", result.kind, OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT);
	check("strict no callbacks", f.count, 0);

	in.policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED;
	in.source_id = OB_PHY_99658_ARG1_OTHER_BAND;
	check("wrong row identity rejected", ob_phy_acphy_99658_test_only(
			&fake_ops, &f, &in, &result), -EINVAL);
	check("wrong row no callbacks", f.count, 0);
	in.source_id = OB_PHY_99658_ARG1_CHANSPEC_LOW;
	in.nested_66b_present = false;
	in.nested_66e_present = false;
	check("arg1 does not require arg2-only selectors",
	      ob_phy_acphy_99658_test_only(&fake_ops, &f, &in, &result), 0);
	check("arg1 selector test callback count", f.count, 4);
	check("missing table callback rejected",
	      ob_phy_acphy_99658_test_only(NULL, &f, &in, &result), -EINVAL);
	check("missing callback issues no new access", f.count, 4);
	in.nested_66b_present = true;
	in.nested_66e_present = true;

	for (fail_at = 1; fail_at <= 4; fail_at++) {
		memset(&f, 0, sizeof(f));
		f.fail_at = fail_at;
		ret = ob_phy_acphy_99658_test_only(&fake_ops, &f, &in, &result);
		check("failure error unchanged", ret, -EIO);
		check("failure stops at callback", f.count, fail_at);
		check("failure preserves only preceding scratch copies",
		      result.scratch_copy_count, fail_at);
		check("failure result", result.kind, OB_PHY_99658_FAILED_BACKEND_OPERATION);
	}
}

static struct ob_phy_998cc_input input_998cc(u8 arg)
{
	struct ob_phy_998cc_input in = {
		.explicitly_synthetic = true,
		.policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED,
		.chip_id = 0x4352, .chip_revision = 3, .phy_revision = 0x0a,
		.radio_id = 0x2069, .radio_revision = 4, .radio_class = 0,
		.chain_count = 2, .argument = arg,
		.table_offset_present = true, .table_offset = arg == 1 ? 8 : 16,
		.nested_66c_present = true, .nested_66c = 3,
		.nested_66d_present = true, .nested_66d = 4,
		.source_id = OB_PHY_99658_998CC_STATIC_ROW,
	};
	return in;
}

static void test_interposed_998cc_payloads(void)
{
	static const u8 source[7] = { 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78 };
	static const u8 expected1[6] = {
		0x0b, 0x0c, 0x7f, 0x7f, 0x7f, 0x7f,
	};
	static const u8 expected2[7] = {
		0x12, 0x23, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
	};
	struct ob_phy_998cc_input in = input_998cc(1);
	struct ob_phy_99658_result result;
	struct fake f = { 0 };
	unsigned int i;

	check("998cc arg1 return", ob_phy_acphy_998cc_test_only(
			&fake_ops, &f, &in, &result), 0);
	check("998cc arg1 writes", result.table_api_write_count, 1);
	check("998cc arg1 table", f.records[0].table, 0x0b);
	check("998cc arg1 offset", f.records[0].offset, 8);
	check("998cc arg1 width", f.records[0].width, 8);
	check("998cc arg1 count", f.records[0].count, 6);
	check("998cc arg1 length", f.records[0].length, 6);
	if (memcmp(f.records[0].bytes, expected1, sizeof(expected1))) failures++;
	check("998cc arg1 copied field", result.copied_state_offset, 0x065a);
	check("998cc arg1 copied value", result.copied_state_value, 3);

	memset(&f, 0, sizeof(f));
	in = input_998cc(2);
	in.source_bytes = source;
	in.source_length = sizeof(source);
	check("998cc arg2 return", ob_phy_acphy_998cc_test_only(
			&fake_ops, &f, &in, &result), 0);
	check("998cc arg2 table", f.records[0].table, 0x0b);
	check("998cc arg2 offset", f.records[0].offset, 16);
	check("998cc arg2 width", f.records[0].width, 8);
	check("998cc arg2 count", f.records[0].count, 7);
	check("998cc arg2 length", f.records[0].length, 7);
	if (memcmp(f.records[0].bytes, expected2, sizeof(expected2))) failures++;
	check("998cc arg2 copied field", result.copied_state_offset, 0x065b);
	check("998cc arg2 copied value", result.copied_state_value, 4);

	for (i = 0; i < 2; i++) {
		memset(&f, 0, sizeof(f));
		f.fail_at = 1;
		in = input_998cc(i ? 2 : 1);
		if (i) {
			in.source_bytes = source;
			in.source_length = sizeof(source);
		}
		check("998cc error propagated", ob_phy_acphy_998cc_test_only(
				&fake_ops, &f, &in, &result), -EIO);
		check("998cc one failing callback", f.count, 1);
		check("998cc failed status", result.kind,
		      OB_PHY_99658_FAILED_BACKEND_OPERATION);
	}
}

static void test_interleaved_caller_stage(void)
{
	static const u8 src1[6] = { 9, 8, 7, 6, 5, 4 };
	static const u8 src2[7] = { 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87 };
	struct ob_phy_99658_input a1 = input_for(1), a2 = input_for(2);
	struct ob_phy_998cc_input b1 = input_998cc(1), b2 = input_998cc(2);
	struct ob_phy_9af05_table_stage_input in = {
		.arg1 = &a1, .between = &b1, .arg2 = &a2, .after = &b2,
	};
	struct ob_phy_9af05_table_stage_result result;
	struct fake f = { 0 };
	unsigned int fail_at;
	int ret;

	a1.source_bytes = src1;
	a1.source_length = sizeof(src1);
	a2.source_bytes = src2;
	a2.source_length = sizeof(src2);
	a2.source_id = OB_PHY_99658_ARG2_LOW_66E_ZERO;
	b2.source_bytes = src2;
	b2.source_length = sizeof(src2);
	check("caller stage complete", ob_phy_9af05_table_stage_test_only(
			&fake_ops, &f, &in, &result), 0);
	check("caller stage 99658 writes", result.sub_99658_writes, 8);
	check("caller stage 998cc writes", result.sub_998cc_writes, 2);
	check("caller stage callbacks", result.callback_count, 10);
	check("caller stage result", result.kind, OB_PHY_99658_HOST_SIMULATION_COMPLETE);
	check("caller stage stop", result.terminal_boundary, 0x9b14f);
	check("first helper table", f.records[0].table, 0x44);
	check("intervening helper table", f.records[4].table, 0x0b);
	check("second helper table", f.records[5].table, 0x44);
	check("final interposed table", f.records[9].table, 0x0b);

	for (fail_at = 1; fail_at <= 10; fail_at++) {
		memset(&f, 0, sizeof(f));
		f.fail_at = fail_at;
		ret = ob_phy_9af05_table_stage_test_only(&fake_ops, &f, &in, &result);
		check("stage failure propagated", ret, -EIO);
		check("stage stops exactly", f.count, fail_at);
		check("stage failure not complete", result.kind,
		      OB_PHY_99658_FAILED_BACKEND_OPERATION);
	}

	/* Preflight rejects a missing second-call payload before any first-call
	 * callback, avoiding a partially accepted synthetic composition. */
	memset(&f, 0, sizeof(f));
	a2.source_bytes = NULL;
	check("stage preflight rejects missing arg2 source", ob_phy_9af05_table_stage_test_only(
			&fake_ops, &f, &in, &result), -EINVAL);
	check("stage preflight no callback", f.count, 0);
}

static int parse_hex(const char *text, u8 *bytes, u8 expected)
{
	u8 i;

	if (!text || strlen(text) != (size_t)expected * 2)
		return -EINVAL;
	for (i = 0; i < expected; i++) {
		char item[3] = { text[i * 2], text[i * 2 + 1], '\0' };
		char *end;
		unsigned long value = strtoul(item, &end, 16);
		if (*end || value > 0xff)
			return -EINVAL;
		bytes[i] = (u8)value;
	}
	return 0;
}

static int dump_probe(int argc, char **argv)
{
	struct ob_phy_99658_input in = input_for(1);
	struct ob_phy_99658_result result;
	struct fake f = { 0 };
	u8 arg, length, row;
	unsigned int i, j;

	if (argc != 12)
		return 2;
	arg = (u8)strtoul(argv[2], NULL, 0);
	row = (u8)strtoul(argv[3], NULL, 0);
	length = arg == 1 ? 6 : 7;
	in = input_for(arg);
	in.source_id = (enum ob_phy_99658_source_id)row;
	in.chanspec = (u16)strtoul(argv[4], NULL, 0);
	in.nested_66a = (u8)strtoul(argv[5], NULL, 0);
	in.nested_66b = (u8)strtoul(argv[6], NULL, 0);
	in.nested_66e = (u8)strtoul(argv[7], NULL, 0);
	in.nested_340 = (u8)strtoul(argv[8], NULL, 0);
	in.nested_3e0 = (u8)strtoul(argv[9], NULL, 0);
	in.table_offset = (u8)strtoul(argv[10], NULL, 0);
	{
		static u8 source[7];
		if (parse_hex(argv[11], source, length))
			return 2;
		in.source_bytes = source;
		in.source_length = length;
	}
	if (ob_phy_acphy_99658_test_only(&fake_ops, &f, &in, &result))
		return 3;
	for (i = 0; i < f.count; i++) {
		const struct table_record *r = &f.records[i];
		printf("%08x,%u,%u,%02x,%u,%u,%u,%u,", r->site,
		       r->argument, r->chain, r->table, r->offset, r->count,
		       r->width, r->length);
		for (j = 0; j < r->length; j++)
			printf("%02x", r->bytes[j]);
		putchar('\n');
	}
	return f.count == 4 ? 0 : 4;
}

static void print_records(const struct fake *f)
{
	unsigned int i, j;

	for (i = 0; i < f->count; i++) {
		const struct table_record *r = &f->records[i];
		printf("%08x,%u,%u,%02x,%u,%u,%u,%u,", r->site,
		       r->argument, r->chain, r->table, r->offset, r->count,
		       r->width, r->length);
		for (j = 0; j < r->length; j++)
			printf("%02x", r->bytes[j]);
		putchar('\n');
	}
}

static int dump_stage(void)
{
	static const u8 source_a[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
	static const u8 source_b[7] = { 0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97 };
	struct ob_phy_99658_input arg1 = input_for(1), arg2 = input_for(2);
	struct ob_phy_998cc_input between = input_998cc(1), after = input_998cc(2);
	struct ob_phy_9af05_table_stage_input in = {
		.arg1 = &arg1, .between = &between,
		.arg2 = &arg2, .after = &after,
	};
	struct ob_phy_9af05_table_stage_result result;
	struct fake f = { 0 };

	arg1.source_bytes = source_a;
	arg1.source_length = sizeof(source_a);
	arg2.source_bytes = source_b;
	arg2.source_length = sizeof(source_b);
	arg2.source_id = OB_PHY_99658_ARG2_LOW_66E_ZERO;
	after.source_bytes = source_b;
	after.source_length = sizeof(source_b);
	if (ob_phy_9af05_table_stage_test_only(&fake_ops, &f, &in, &result))
		return 2;
	print_records(&f);
	return result.callback_count == 10 ? 0 : 3;
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--probe"))
		return dump_probe(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "--dump-stage"))
		return dump_stage();
	test_argument1_payloads();
	test_argument2_payloads();
	test_strict_missing_and_failstop();
	test_interposed_998cc_payloads();
	test_interleaved_caller_stage();
	if (failures) {
		printf("%u failures\n", failures);
		return 1;
	}
	puts("sub_99658 synthetic C tests: PASS");
	return 0;
}

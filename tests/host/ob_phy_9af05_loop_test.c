/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy_9af05_loop.h"
#include "../../src/ob_phy_99658.h"
#include "../../src/ob_phy_9a121.h"
#include "../../src/ob_phy_9a539.h"

enum record_kind { REC_PHY_READ, REC_PHY_WRITE, REC_TABLE_WRITE, REC_TABLE_READ,
		   REC_RADIO_READ, REC_RADIO_WRITE, REC_MAC_SUSPEND,
		   REC_ACI_UPDATE, REC_MAC_ENABLE };
struct record {
	enum record_kind kind;
	u32 site;
	u16 reg;
	u16 value;
	u16 destination;
	u8 chain, table, offset, count, width, length, bytes[10];
};
struct fake {
	struct record records[96];
	u8 count, fail_at;
	u16 phy_019e, phy_016c;
	u16 phy_06dc, phy_08dc;
	u16 radio_regs[0x800];
};
static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL %s got=%ld expected=%ld\n", name, got, expected);
		failures++;
	}
}

static int push(struct fake *f, struct record *r)
{
	if (f->count >= 96)
		return -ENOSPC;
	f->records[f->count++] = *r;
	return f->fail_at == f->count ? -EIO : 0;
}

static int phy_read(void *ctx, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct record r = { .kind = REC_PHY_READ, .site = site, .reg = reg };
	int ret = push(f, &r);
	if (!ret) {
		if (reg == 0x016c)
			*value = f->phy_016c;
		else if (reg == 0x019e)
			*value = f->phy_019e;
		else if (reg == 0x06dc)
			*value = f->phy_06dc;
		else if (reg == 0x08dc)
			*value = f->phy_08dc;
		else
			*value = 0;
	}
	return ret;
}

static int phy_write(void *ctx, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct record r = { .kind = REC_PHY_WRITE, .site = site,
		.reg = reg, .value = value };
	int ret = push(f, &r);
	if (!ret && reg == 0x019e)
		f->phy_019e = value;
	if (!ret && reg == 0x016c)
		f->phy_016c = value;
	if (!ret && reg == 0x06dc)
		f->phy_06dc = value;
	if (!ret && reg == 0x08dc)
		f->phy_08dc = value;
	return ret;
}

static int table_write(void *ctx, u32 site, u8 chain, u8 table, u8 offset,
		       u8 count, u8 width, const u8 *payload, u8 length)
{
	struct fake *f = ctx;
	struct record r = { .kind = REC_TABLE_WRITE, .site = site,
		.chain = chain, .table = table, .offset = offset,
		.count = count, .width = width, .length = length };
	if (!payload || length > sizeof(r.bytes))
		return -EINVAL;
	memcpy(r.bytes, payload, length);
	return push(f, &r);
}

static int table_read(void *ctx, u32 site, u8 chain, u8 table, u8 offset,
		      u8 count, u8 width, u16 destination, const u8 *payload,
		      u8 length)
{
	struct fake *f = ctx;
	struct record r = { .kind = REC_TABLE_READ, .site = site,
		.destination = destination, .chain = chain, .table = table,
		.offset = offset, .count = count, .width = width, .length = length };
	u8 i;
	if (!payload || length > sizeof(r.bytes))
		return -EINVAL;
	for (i = 0; i < length; i++)
		r.bytes[i] = payload[i];
	return push(f, &r);
}

static int a121_table_write(void *ctx, u32 site, u8 table, u8 offset,
			    u8 count, u8 width, const u8 *payload, u8 length)
{
	return table_write(ctx, site, (u8)(table - 0xf9u), table, offset,
			count, width, payload, length);
}

static int a121_radio_read(void *ctx, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct record r = { .kind = REC_RADIO_READ, .site = site, .reg = reg,
			    .value = f->radio_regs[reg] };
	int ret = push(f, &r);

	if (!ret)
		*value = r.value;
	return ret;
}

static int a121_radio_write(void *ctx, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct record r = { .kind = REC_RADIO_WRITE, .site = site, .reg = reg,
			    .value = value };
	int ret = push(f, &r);

	if (!ret)
		f->radio_regs[reg] = value;
	return ret;
}

static const struct ob_9af05_loop_ops ops = {
	.phy_read16 = phy_read, .phy_write16 = phy_write,
	.table_write = table_write, .table_read = table_read,
};

static const struct ob_phy_9a121_ops a121_ops = {
	.phy_read16 = phy_read, .phy_write16 = phy_write,
	.radio_read16 = a121_radio_read, .radio_write16 = a121_radio_write,
	.table_write = a121_table_write,
};

static int a539_suspend(void *ctx, u32 site)
{
	struct record r = { .kind = REC_MAC_SUSPEND, .site = site };
	return push(ctx, &r);
}
static int a539_enable(void *ctx, u32 site)
{
	struct record r = { .kind = REC_MAC_ENABLE, .site = site };
	return push(ctx, &r);
}
static int a539_aci(void *ctx, u32 site, u8 mode)
{
	struct record r = { .kind = REC_ACI_UPDATE, .site = site, .value = mode };
	return push(ctx, &r);
}
static const struct ob_phy_9a539_ops a539_ops = {
	.mac_suspend_and_wait = a539_suspend, .mac_enable = a539_enable,
	.aci_update = a539_aci, .phy_read16 = phy_read,
	.phy_write16 = phy_write,
};

static int stage_table_write(void *ctx, u32 site, u8 argument, u8 chain,
			     u8 table, u8 offset, u8 count, u8 width,
			     const u8 *payload, u8 length)
{
	struct fake *f = ctx;
	struct record r = { .kind = REC_TABLE_WRITE, .site = site,
		.chain = chain, .table = table, .offset = offset,
		.count = count, .width = width, .length = length };
	(void)argument;
	if (!payload || length > sizeof(r.bytes))
		return -EINVAL;
	memcpy(r.bytes, payload, length);
	return push(f, &r);
}

static const struct ob_phy_99658_ops stage_ops = {
	.table_write = stage_table_write,
};

static struct ob_9af05_loop_input input(void)
{
	struct ob_9af05_loop_input in = {
		.explicitly_synthetic = true,
		.chip_id = 0x4352, .chip_revision = 3, .phy_revision = 0x0a,
		.radio_id = 0x2069, .radio_revision = 4, .radio_class = 0,
		.chain_count = 2, .chanspec_present = true, .chanspec = 0x1001,
		.caller_arg_sil_present = true, .caller_arg_sil = 1,
		.caller_arg_dl_present = true, .caller_arg_dl = 1,
		.nested_912_present = true, .nested_912 = 0,
		.nested_66e_present = true, .nested_66e = 1,
		.nested_3e3_present = true, .nested_3e3 = 0x77,
		.nested_3e4_present = true, .nested_3e4 = 0x88,
		.nested_3e5_present = true, .nested_3e5 = 0x99,
		.nested_64a_present = true, .nested_64a = 2,
		.saved_phy_016c_present = true, .saved_phy_016c = 0x1234,
		.saved_phy_019e_present = true, .saved_phy_019e = 0x0002,
	};
	static const struct {
		u32 site; u8 table, offset, count;
	} meta[OB_9AF05_SAMPLE_COUNT] = {
		{ 0x9b63f, 0x65, 0x00, 1 }, { 0x9b665, 0x65, 0x20, 10 },
		{ 0x9b687, 0x64, 0x60, 8 }, { 0x9b6a9, 0x64, 0x70, 8 },
		{ 0x9b6cf, 0x65, 0x60, 8 }, { 0x9b6f5, 0x65, 0x70, 8 },
	};
	unsigned int i;
	for (i = 0; i < OB_9AF05_SAMPLE_COUNT; i++) {
		struct ob_9af05_table_sample *s = &in.samples[i];
		unsigned int j;
		s->present = true; s->source_site = meta[i].site;
		s->table_id = meta[i].table; s->offset = meta[i].offset;
		s->count = meta[i].count; s->width_bits = 8;
		s->length = meta[i].count;
		for (j = 0; j < s->length; j++)
			s->bytes[j] = (u8)(0x20 + i + j);
	}
	return in;
}

static void test_exact_trace_and_restore(void)
{
	struct ob_9af05_loop_input in = input();
	struct ob_9af05_loop_result result;
	struct fake f = { .phy_019e = 0xa5a5 };
	static const u32 read_sites[] = {
		0x9b63f, 0x9b665, 0x9b687, 0x9b6a9, 0x9b6cf, 0x9b6f5,
	};
	unsigned int i;
	check("run", ob_phy_9af05_chain1_test_only(&ops, &f, &in, &result), 0);
	check("callbacks", f.count, 14);
	check("logical events", result.logical_event_count, 13);
	check("complete status", result.status, OB_9AF05_LOOP_SYNTHETIC_COMPLETE);
	check("stop", result.terminal_boundary, 0x9b751);
	check("first callback table write", f.records[0].kind, REC_TABLE_WRITE);
	check("first site", f.records[0].site, 0x9b3dd);
	check("first table", f.records[0].table, 0x64);
	check("first count", f.records[0].count, 2);
	check("first offset", f.records[0].offset, 0);
	check("first width", f.records[0].width, 8);
	check("first payload word 0", f.records[0].bytes[0], 0x77);
	check("first payload word 1", f.records[0].bytes[1], 0x77);
	check("PHY 173b site", f.records[1].site, 0x9b477);
	check("PHY 173b", f.records[1].reg, 0x173b);
	check("PHY 173b value", f.records[1].value, 0x1c);
	check("PHY 1726 site", f.records[2].site, 0x9b4b7);
	check("PHY 1726", f.records[2].reg, 0x1726);
	check("PHY 1726 value", f.records[2].value, 0x0c);
	check("TABLE64 count10", f.records[3].count, 10);
	check("TABLE65 count10", f.records[4].count, 10);
	check("TABLE64 id", f.records[3].table, 0x64);
	check("TABLE64 offset", f.records[3].offset, 0x20);
	check("TABLE64 width", f.records[3].width, 8);
	check("TABLE65 id", f.records[4].table, 0x65);
	check("TABLE65 offset", f.records[4].offset, 0x20);
	check("TABLE65 width", f.records[4].width, 8);
	for (i = 0; i < 10; i++) {
		check("TABLE64 payload", f.records[3].bytes[i], 3);
		check("TABLE65 payload", f.records[4].bytes[i], 2);
	}
	for (i = 0; i < 6; i++) {
		check("read event site", f.records[5 + i].site, read_sites[i]);
		check("read event type", f.records[5 + i].kind, REC_TABLE_READ);
		check("read chain", f.records[5 + i].chain, 1);
		check("read width", f.records[5 + i].width, 8);
		check("read snapshot retained", f.records[5 + i].bytes[0], 0x20 + i);
	}
	check("restore 016c site", f.records[11].site, 0x9b719);
	check("restore 016c", f.records[11].reg, 0x016c);
	check("restore 016c value", f.records[11].value, 0x1234);
	check("RMW read site", f.records[12].site, 0x9b73b);
	check("RMW read 019e", f.records[12].reg, 0x019e);
	check("RMW write site", f.records[13].site, 0x9b73b);
	check("RMW write value preserves other bits", f.records[13].value, 0xa5a7);
	check("software copy offset", result.nested_copy_offset, 0x0461);
	check("software copy byte", result.nested_copy[2], 0x99);
	check("synthetic not exact", result.historical_exact, false);
	check("not ready", result.phy_ready || result.channel_ready ||
	      result.hardware_ready, false);
	in.saved_phy_019e = 0xa5a5;
	memset(&f, 0, sizeof(f)); f.phy_019e = 0xa5a5;
	check("clear saved bit1 scenario", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), 0);
	check("clear saved bit1 restores original live register",
	      f.records[13].value, 0xa5a5);
	in.saved_phy_019e = 0xa5a7;
	memset(&f, 0, sizeof(f)); f.phy_019e = 0xa5a5;
	check("set saved bit1 scenario", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), 0);
	check("set saved bit1 preserves unrelated live bits",
	      f.records[13].value, 0xa5a7);
}

static void test_preflight_and_fail_stop(void)
{
	static const u32 failure_sites[14] = {
		0x9b3dd, 0x9b477, 0x9b4b7, 0x9b4d9, 0x9b4fc,
		0x9b63f, 0x9b665, 0x9b687, 0x9b6a9, 0x9b6cf,
		0x9b6f5, 0x9b719, 0x9b73b, 0x9b73b,
	};
	struct ob_9af05_loop_input in = input();
	struct ob_9af05_loop_result result;
	struct fake f;
	unsigned int fail_at;
	int ret;
	for (fail_at = 1; fail_at <= 14; fail_at++) {
		memset(&f, 0, sizeof(f)); f.phy_019e = 0x8001; f.fail_at = fail_at;
		ret = ob_phy_9af05_chain1_test_only(&ops, &f, &in, &result);
		check("failure error unchanged", ret, -EIO);
		check("failure has no later callback", f.count, fail_at);
		check("failure status", result.status, OB_9AF05_LOOP_FAILED);
		check("failure exact site", result.failed_site, failure_sites[fail_at - 1]);
		check("failure no terminal", result.terminal_boundary, 0);
		check("failure no software state publication",
		      result.nested_copy_present || result.table_source_copy_present, false);
		check("failure no readiness", result.hardware_ready ||
		      result.phy_ready || result.channel_ready, false);
	}
	memset(&f, 0, sizeof(f));
	in.explicitly_synthetic = false;
	check("unlabeled scenario rejected", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), -EPERM);
	check("unlabeled scenario no access", f.count, 0);
	in = input();
	in.samples[2].present = false;
	check("missing read snapshot rejected", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), -EINVAL);
	check("preflight before access", f.count, 0);
	in = input(); in.nested_912 = 1;
	check("HIRSSI selected path blocked", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), -EOPNOTSUPP);
	check("unsupported branch no access", f.count, 0);
	in = input(); in.samples[1].offset++;
	check("wrong TABLE snapshot offset rejected", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), -EINVAL);
	check("bad snapshot no access", f.count, 0);
	in = input(); in.chanspec = 0x5001;
	check("high-band arm has exact unsupported boundary", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), -EOPNOTSUPP);
	check("high-band unsupported no access", f.count, 0);
	in = input();
	check("missing callback set rejected", ob_phy_9af05_chain1_test_only(
			NULL, &f, &in, &result), -EINVAL);
	check("missing callback no access", f.count, 0);
	in = input(); in.nested_66e = 0;
	memset(&f, 0, sizeof(f));
	check("zero branch", ob_phy_9af05_chain1_test_only(&ops, &f, &in,
								 &result), 0);
	check("zero branch payload value", f.records[1].value, 0x18);
	in.nested_66e = 2;
	memset(&f, 0, sizeof(f));
	check("nonzero above one branch", ob_phy_9af05_chain1_test_only(
			&ops, &f, &in, &result), 0);
	check("nonzero above one uses nonzero arm", f.records[1].value, 0x1c);
}

static void test_stage_to_outer_loop_composition(void)
{
	static const u8 arg1_source[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
	static const u8 arg2_source[7] = { 0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97 };
	struct ob_phy_99658_input a1 = {
		.explicitly_synthetic = true, .policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED,
		.chip_id = 0x4352, .chip_revision = 3, .phy_revision = 0x0a,
		.radio_id = 0x2069, .radio_revision = 4, .chain_count = 2,
		.argument = 1, .chanspec_present = true, .chanspec = 0x1001,
		.table_offset_present = true, .table_offset = 8,
		.transfer_count_present = true, .transfer_count = 6,
		.nested_66a_present = true, .nested_66a = 2,
		.source_id = OB_PHY_99658_ARG1_CHANSPEC_LOW,
		.source_bytes = arg1_source, .source_length = 6,
	};
	struct ob_phy_998cc_input b1 = {
		.explicitly_synthetic = true, .policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED,
		.chip_id = 0x4352, .chip_revision = 3, .phy_revision = 0x0a,
		.radio_id = 0x2069, .radio_revision = 4, .chain_count = 2,
		.argument = 1, .table_offset_present = true, .table_offset = 8,
		.nested_66c_present = true, .nested_66c = 3,
		.nested_66d_present = true, .nested_66d = 4,
	};
	struct ob_phy_99658_input a2 = a1;
	struct ob_phy_998cc_input b2 = b1;
	struct ob_phy_9af05_table_stage_input stage = {
		.arg1 = &a1, .between = &b1, .arg2 = &a2, .after = &b2,
	};
	struct ob_phy_9af05_table_stage_result stage_result;
	struct ob_9af05_loop_input next = input();
	struct ob_9af05_loop_result next_result;
	struct ob_phy_9a121_input a121 = { 0 };
	struct ob_phy_9a121_result a121_result;
	struct fake f = { .phy_019e = 0x1122, .phy_016c = 0xabcd };
	int ret;

	a2.argument = 2; a2.table_offset = 16; a2.transfer_count = 7;
	a2.nested_66b_present = true; a2.nested_66b = 2;
	a2.nested_66e_present = true; a2.nested_66e = 0;
	a2.nested_340_present = true; a2.nested_340 = 1;
	a2.nested_3e0_present = true; a2.nested_3e0 = 7;
	a2.source_id = OB_PHY_99658_ARG2_LOW_66E_ZERO;
	a2.source_bytes = arg2_source; a2.source_length = 7;
	b2.argument = 2; b2.table_offset = 16;
	b2.source_id = OB_PHY_99658_998CC_STATIC_ROW;
	b2.source_bytes = arg2_source; b2.source_length = 7;
	ret = ob_phy_9af05_table_stage_test_only(&stage_ops, &f, &stage,
						 &stage_result);
	check("ten-call TABLE stage", ret, 0);
	check("bounded TABLE stage count", f.count, 10);
	check("no accidental stage repeat at join", f.records[9].site, 0x99981);
	ret = ob_phy_9af05_chain1_test_only(&ops, &f, &next, &next_result);
	check("outer chain continuation", ret, 0);
	check("composed new callbacks", f.count, 24);
	check("join first event", f.records[10].site, 0x9b3dd);
	check("join first table", f.records[10].table, 0x64);
	check("join last RMW write", f.records[23].site, 0x9b73b);
	check("continuation terminal", next_result.terminal_boundary, 0x9b751);
	check("saved PHY016c restored", f.phy_016c, 0x1234);

	/* The same fake PHY register state and callback log continue across the
	 * caller return into the explicitly synthetic zero-core 9a121 scenario. */
	a121.explicitly_synthetic = true;
	a121.policy = OB_PHY_9A121_SYNTHETIC_PARAMETERIZED;
	a121.chip_id = 0x4352; a121.chip_revision = 3; a121.phy_revision = 0x0a;
	a121.radio_id = 0x2069; a121.radio_revision = 4; a121.radio_class = 0;
	a121.chain_count = 2;
	a121.chanspec_present = true; a121.chanspec = 0x1001;
	a121.nested_910_present = true; a121.nested_910 = 0;
	a121.nested_911_present = true; a121.nested_911 = 0;
	a121.nested_340_present = true; a121.nested_340 = 0x55;
	a121.nested_341_present = true; a121.nested_341 = 0x66;
	a121.nested_66e_present = true; a121.nested_66e = 0;
	a121.nested_66f_present = true; a121.nested_66f = 3;
	a121.core_mask_present = true; a121.core_mask = 0;
	ret = ob_phy_9a121_test_only(&a121_ops, &f, &a121, &a121_result);
	check("9a121 after one persistent 9af05 state", ret, 0);
	check("stage-loop-a121 combined callback count", f.count, 29);
	check("a121 starts after selected 9af05 return", f.records[24].site, 0x9a1af);
	check("combined terminal", a121_result.terminal_boundary, 0x9a538);
}

static void test_return_handoff_uses_persistent_phy_state(void)
{
	struct ob_9af05_loop_input in = input();
	struct ob_9af05_loop_result loop_result;
	struct ob_phy_9a121_result a121_result;
	struct ob_phy_9a121_input a121 = { 0 };
	struct fake f = { .phy_019e = 0xa5a5 };
	int ret;

	ret = ob_phy_9af05_chain1_test_only(&ops, &f, &in, &loop_result);
	check("handoff predecessor", ret, 0);
	check("handoff predecessor callbacks", f.count, 14);
	check("saved PHY019e bit restored", f.phy_019e & 2, 2);
	check("saved PHY016c input is restored", f.phy_016c, 0x1234);

	a121.explicitly_synthetic = true;
	a121.policy = OB_PHY_9A121_SYNTHETIC_PARAMETERIZED;
	a121.chip_id = 0x4352; a121.chip_revision = 3; a121.phy_revision = 0x0a;
	a121.radio_id = 0x2069; a121.radio_revision = 4; a121.radio_class = 0;
	a121.chain_count = 2;
	a121.chanspec_present = true; a121.chanspec = 0x1001;
	a121.nested_910_present = true; a121.nested_910 = 0;
	a121.nested_911_present = true; a121.nested_911 = 0;
	a121.nested_340_present = true; a121.nested_340 = 0x55;
	a121.nested_341_present = true; a121.nested_341 = 0x66;
	a121.nested_66e_present = true; a121.nested_66e = 0;
	a121.core_mask_present = true; a121.core_mask = 0;
	ret = ob_phy_9a121_test_only(&a121_ops, &f, &a121, &a121_result);
	check("handoff a121", ret, 0);
	check("a121 appends five callbacks", f.count, 19);
	check("a121 begins after sub_9af05 return", f.records[14].site, 0x9a1af);
	check("PHY state remains shared", f.phy_019e & 2, 2);
	check("a121 exact return", a121_result.terminal_boundary, 0x9a538);
}

static void test_persistent_handoff_completes_active_core_path(void)
{
	struct ob_9af05_loop_input in = input();
	struct ob_9af05_loop_result loop_result;
	struct ob_phy_9a121_result a121_result;
	struct ob_phy_9a121_input a121 = { 0 };
	struct ob_phy_9a539_input a539 = {
		.explicitly_synthetic = true,
		.policy = OB_9A539_SYNTHETIC_PARAMETERIZED,
		.chip_id = 0x4352, .chip_revision = 3, .phy_revision = 0x0a,
		.radio_id = 0x2069, .radio_revision = 4, .radio_class = 0,
		.chain_count = 2, .argument_present = true, .argument = 0,
		.chanspec_present = true, .chanspec = 0x1001,
	};
	struct ob_phy_9a539_result a539_result;
	struct fake f = { .phy_019e = 0xa5a5 };
	u8 i;
	int ret;

	ret = ob_phy_9af05_chain1_test_only(&ops, &f, &in, &loop_result);
	check("active predecessor", ret, 0);
	check("active predecessor callbacks", f.count, 14);
	a121.explicitly_synthetic = true;
	a121.policy = OB_PHY_9A121_SYNTHETIC_PARAMETERIZED;
	a121.chip_id = 0x4352; a121.chip_revision = 3; a121.phy_revision = 0x0a;
	a121.radio_id = 0x2069; a121.radio_revision = 4; a121.radio_class = 0;
	a121.chain_count = 2;
	a121.chanspec_present = true; a121.chanspec = 0x1001;
	a121.nested_910_present = true; a121.nested_911_present = true;
	a121.nested_340_present = true; a121.nested_340 = 0x55;
	a121.nested_341_present = true; a121.nested_341 = 0x66;
	a121.nested_66e_present = true; a121.nested_66e = 0;
	a121.nested_66f_present = true; a121.nested_66f = 3;
	a121.core_mask_present = true; a121.core_mask = 1;
	a121.nested_64a_64f_present = true;
	memset(a121.nested_64a_64f, 1, sizeof(a121.nested_64a_64f));
	a121.nested_coefficients_present = true;
	a121.nested_coefficients[0x0f] = 2;
	for (i = 0; i < 6; i++) {
		a121.nested_coefficients[0x1a + i * 0x0a] = i + 1;
		a121.nested_coefficients[0x56 + i * 0x0a] = i + 5;
	}
	ret = ob_phy_9a121_test_only(&a121_ops, &f, &a121, &a121_result);
	check("active helper completes", ret, 0);
	check("persistent active composition callbacks", f.count, 47);
	check("active core discarded read follows prior return", f.records[17].site,
	      0x9a304);
	check("first child PHY write follows discarded read", f.records[18].site,
	      0x99fef);
	check("child TABLE API follows pair writes", f.records[20].site, 0x9a04f);
	check("first sub_9175a event follows prefix", f.records[33].site, 0x91825);
	check("active helper reaches return", a121_result.terminal_boundary,
	      0x9a538);
	check("PHY019e saved bit restored after active path", f.phy_019e & 2, 2);
	ret = ob_phy_9a539_test_only(&a539_ops, &f, &a539, &a539_result);
	check("persistent 9af05-A121-9a539 composition", ret, 0);
	check("prior 47 callbacks plus 9a539 path", f.count, 77);
	check("9a539 reads persistent restored 019e", f.records[48].site, 0x9a59c);
	check("9a539 first helper effect after caller boundary", f.records[47].site,
	      0x9a58f);
	check("9a539 normal return", a539_result.terminal_boundary, 0x9a88a);
}

int main(void)
{
	test_exact_trace_and_restore();
	test_preflight_and_fail_stop();
	test_stage_to_outer_loop_composition();
	test_return_handoff_uses_persistent_phy_state();
	test_persistent_handoff_completes_active_core_path();
	if (failures) {
		printf("%u failures\n", failures);
		return 1;
	}
	puts("sub_9af05 chain-1 synthetic tests: PASS");
	return 0;
}

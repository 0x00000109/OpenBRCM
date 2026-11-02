/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ob_phy_9a121.h"

#define MAX_LOG 24
enum op_kind { OP_READ, OP_WRITE, OP_TABLE };
struct log_entry { enum op_kind kind; u32 site; u16 reg; u16 value; };
struct fake {
	struct log_entry log[MAX_LOG];
	u8 count;
	u8 fail_at;
	u16 phy019e;
};

static int push(struct fake *f, enum op_kind kind, u32 site, u16 reg,
		u16 value)
{
	struct log_entry *e;

	assert(f->count < MAX_LOG);
	e = &f->log[f->count++];
	e->kind = kind;
	e->site = site;
	e->reg = reg;
	e->value = value;
	return f->fail_at == f->count ? -77 : 0;
}

static int read16(void *ctx, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	int ret = push(f, OP_READ, site, reg, f->phy019e);

	if (!ret)
		*value = f->phy019e;
	return ret;
}

static int write16(void *ctx, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	int ret = push(f, OP_WRITE, site, reg, value);

	if (!ret && reg == 0x019e)
		f->phy019e = value;
	return ret;
}

static int table_write(void *ctx, u32 site, u8 table, u8 offset, u8 count,
		       u8 width, const u8 *payload, u8 length)
{
	struct fake *f = ctx;
	struct log_entry e = { .kind = OP_TABLE, .site = site,
			       .reg = (u16)(((u16)table << 8) | offset),
			       .value = (u16)(((u16)count << 8) | width) };
	int ret;

	(void)payload;
	(void)length;
	ret = push(f, e.kind, e.site, e.reg, e.value);
	return ret;
}

static const struct ob_phy_9a121_ops ops = {
	.phy_read16 = read16,
	.phy_write16 = write16,
	.table_write = table_write,
};

static struct ob_phy_9a121_input input(u8 core_mask)
{
	struct ob_phy_9a121_input in = { 0 };

	in.explicitly_synthetic = true;
	in.policy = OB_PHY_9A121_SYNTHETIC_PARAMETERIZED;
	in.chip_id = 0x4352;
	in.chip_revision = 3;
	in.phy_revision = 0x0a;
	in.radio_id = 0x2069;
	in.radio_revision = 4;
	in.radio_class = 0;
	in.chain_count = 2;
	in.chanspec_present = true;
	in.chanspec = 0x1001;
	in.nested_910_present = true;
	in.nested_910 = 0;
	in.nested_911_present = true;
	in.nested_911 = 0;
	in.nested_340_present = true;
	in.nested_340 = 0x55;
	in.nested_341_present = true;
	in.nested_341 = 0x66;
	in.nested_66e_present = true;
	in.nested_66e = 0;
	in.nested_66f_present = true;
	in.nested_66f = 3;
	in.core_mask_present = true;
	in.core_mask = core_mask;
	in.nested_64a_64f_present = true;
	memset(in.nested_64a_64f, 1, sizeof(in.nested_64a_64f));
	in.nested_coefficients_present = true;
	/* A bounded, explicit per-core candidate snapshot for active-path tests. */
	in.nested_coefficients[0x0f] = 2;
	in.nested_coefficients[0x12] = 3;
	in.nested_coefficients[0x1a] = 1;
	in.nested_coefficients[0x24] = 2;
	in.nested_coefficients[0x2e] = 3;
	in.nested_coefficients[0x38] = 4;
	in.nested_coefficients[0x42] = 5;
	in.nested_coefficients[0x4c] = 6;
	in.nested_coefficients[0x56] = 5;
	in.nested_coefficients[0x60] = 6;
	in.nested_coefficients[0x6a] = 7;
	in.nested_coefficients[0x74] = 8;
	in.nested_coefficients[0x7e] = 9;
	in.nested_coefficients[0x88] = 10;
	return in;
}

static void test_zero_active_mask_full_selected_return(void)
{
	struct fake f = { .phy019e = 0x1040 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = input(0);

	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == 0);
	assert(result.status == OB_9A121_SYNTHETIC_COMPLETE);
	assert(result.callback_count == 5);
	assert(result.logical_event_count == 3);
	assert(result.terminal_boundary == 0x9a538);
	assert(f.phy019e == 0x1040);
	assert(f.log[0].kind == OP_READ && f.log[0].site == 0x9a1af);
	assert(f.log[1].kind == OP_READ && f.log[1].site == 0x9a1ca);
	assert(f.log[2].kind == OP_WRITE && f.log[2].site == 0x9a1ca);
	assert(f.log[2].value == 0x1042);
	assert(f.log[3].kind == OP_READ && f.log[3].site == 0x9a525);
	assert(f.log[4].kind == OP_WRITE && f.log[4].site == 0x9a525);
	assert(f.log[4].value == 0x1040);
	assert(result.nested_650_655[0] == 0x2b);
	assert(result.nested_650_655[3] == 0x34);
	assert(result.nested_650_655[4] == 0x34);
	assert(result.nested_650_655[5] == 0x64);
	assert(result.nested_65c == 0);
	assert(!result.hardware_ready && !result.phy_ready && !result.channel_ready);
}

static void test_high_band_coefficients_do_not_alias_input(void)
{
	struct fake f = { .phy019e = 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = input(0);

	in.chanspec = 0xc001;
	in.nested_341 = 0x99;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == 0);
	assert(result.branch_flag == false);
	assert(result.nested_650_655[0] == 0x2f);
	assert(result.nested_650_655[1] == 0x2f);
	assert(result.nested_650_655[2] == 0x2f);
	assert(result.nested_650_655[3] == 0x34);
	assert(result.nested_650_655[4] == 0x34);
	assert(result.nested_650_655[5] == 0x64);
}

static void test_active_core_missing_radio_callbacks_rejected_before_access(void)
{
	struct fake f = { .phy019e = 0x0020 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = input(1);

	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == -EINVAL);
	assert(result.status == OB_9A121_INVALID);
	assert(result.callback_count == 0);
	assert(f.count == 0);
	assert(f.phy019e == 0x0020);
	assert(!result.hardware_ready && !result.phy_ready && !result.channel_ready);
}

static void test_first_configuration_predicate_uses_bit_zero(void)
{
	struct fake f = { .phy019e = 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = input(0);

	in.nested_910 = 0x02;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == 0);
	assert(!result.branch_flag);
	f.count = 0;
	f.phy019e = 0;
	in.nested_910 = 0x01;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == 0);
	assert(result.branch_flag);
}

static void test_every_complete_path_callback_fail_stops(void)
{
	u8 i;

	for (i = 1; i <= 5; i++) {
		struct fake f = { .fail_at = i, .phy019e = 0x0008 };
		struct ob_phy_9a121_result result;
		struct ob_phy_9a121_input in = input(0);

		assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == -77);
		assert(result.status == OB_9A121_FAILED_BACKEND_OPERATION);
		assert(result.callback_count == i);
		assert(result.terminal_boundary == 0);
		assert(!result.hardware_ready && !result.phy_ready && !result.channel_ready);
	}
}

static void test_invalid_or_strict_input_rejected_before_access(void)
{
	struct fake f = { .phy019e = 0x80 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = input(0);

	in.chanspec_present = false;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == -EINVAL);
	assert(f.count == 0);
	in = input(0);
	in.policy = OB_PHY_9A121_HISTORICAL_STRICT;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == -EAGAIN);
	assert(f.count == 0);
	in = input(4);
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == -EINVAL);
	assert(f.count == 0);
	in = input(0);
	in.chip_revision = 2;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == -ENODEV);
	assert(f.count == 0);
	assert(ob_phy_9a121_test_only(NULL, &f, &in, &result) == -EINVAL);
	assert(f.count == 0);
}

int main(void)
{
	test_zero_active_mask_full_selected_return();
	test_high_band_coefficients_do_not_alias_input();
	test_active_core_missing_radio_callbacks_rejected_before_access();
	test_first_configuration_predicate_uses_bit_zero();
	test_every_complete_path_callback_fail_stops();
	test_invalid_or_strict_input_rejected_before_access();
	puts("sub_9a121 bounded C fake-backend checks passed");
	return 0;
}

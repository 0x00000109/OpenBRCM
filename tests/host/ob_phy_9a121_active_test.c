/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ob_phy_9a121.h"

#define LOG_MAX 96u
enum kind { READ16, WRITE16, TABLE16, RADIO_READ16, RADIO_WRITE16 };
struct record {
	enum kind kind;
	u32 site;
	u16 reg, value;
	u8 table, offset, count, width, length;
	u8 payload[2];
};
struct fake {
	struct record log[LOG_MAX];
	u8 count, fail_at;
	u16 regs[0x800], radio_regs[0x800];
	u8 helper_read_count;
	u16 helper_samples[2], caller_samples[2];
	bool selected_samples_present;
	u8 selected_read_count;
	u16 selected_samples[4];
};

static int append(struct fake *f, const struct record *r)
{
	assert(f->count < LOG_MAX);
	f->log[f->count++] = *r;
	return f->fail_at == f->count ? -73 : 0;
}

static int read16(void *ctx, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct record r = { .kind = READ16, .site = site, .reg = reg };
	u16 sample = f->regs[reg];
	int ret;

	if (site == 0x90506 && f->helper_read_count < 2)
		sample = f->helper_samples[f->helper_read_count++];
	else if (site == 0x9a3b4)
		sample = f->caller_samples[0];
	else if (site == 0x9a42c)
		sample = f->caller_samples[1];
	else if (f->selected_samples_present &&
		 (site == 0x91825 || site == 0x9185b || site == 0x91879) &&
		 f->selected_read_count < 3)
		sample = f->selected_samples[f->selected_read_count++];
	else if (f->selected_samples_present && site == 0x91cb9 &&
		 f->selected_read_count < 4)
		sample = f->selected_samples[f->selected_read_count++];
	r.value = sample;
	ret = append(f, &r);
	if (!ret)
		*value = sample;
	return ret;
}

static int write16(void *ctx, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct record r = { .kind = WRITE16, .site = site, .reg = reg,
			    .value = value };
	int ret = append(f, &r);
	if (!ret)
		f->regs[reg] = value;
	return ret;
}

static int table_write(void *ctx, u32 site, u8 table, u8 offset,
		       u8 count, u8 width, const u8 *payload, u8 length)
{
	struct fake *f = ctx;
	struct record r = { .kind = TABLE16, .site = site, .table = table,
			    .offset = offset, .count = count,
			    .width = width, .length = length };
	int ret;
	assert(length <= sizeof(r.payload));
	memcpy(r.payload, payload, length);
	ret = append(f, &r);
	return ret;
}

static int radio_read16(void *ctx, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct record r = { .kind = RADIO_READ16, .site = site, .reg = reg,
			    .value = f->radio_regs[reg] };
	int ret = append(f, &r);
	if (!ret)
		*value = r.value;
	return ret;
}

static int radio_write16(void *ctx, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct record r = { .kind = RADIO_WRITE16, .site = site, .reg = reg,
			    .value = value };
	int ret = append(f, &r);
	if (!ret)
		f->radio_regs[reg] = value;
	return ret;
}

static const struct ob_phy_9a121_ops ops = {
	.phy_read16 = read16, .phy_write16 = write16,
	.radio_read16 = radio_read16, .radio_write16 = radio_write16,
	.table_write = table_write,
};

static struct ob_phy_9a121_input make_input(u8 mask)
{
	struct ob_phy_9a121_input in = { 0 };
	u8 core, i;
	in.explicitly_synthetic = true;
	in.policy = OB_PHY_9A121_SYNTHETIC_PARAMETERIZED;
	in.chip_id = 0x4352; in.chip_revision = 3; in.phy_revision = 0x0a;
	in.radio_id = 0x2069; in.radio_revision = 4; in.radio_class = 0;
	in.chain_count = 2;
	in.chanspec_present = true; in.chanspec = 0x1001;
	in.nested_910_present = true; in.nested_911_present = true;
	in.nested_340_present = true; in.nested_340 = 0x55;
	in.nested_341_present = true; in.nested_341 = 0x66;
	in.nested_66e_present = true; in.nested_66e = 0;
	in.nested_66f_present = true; in.nested_66f = 3;
	in.core_mask_present = true; in.core_mask = mask;
	in.nested_64a_64f_present = true;
	memset(in.nested_64a_64f, 1, sizeof(in.nested_64a_64f));
	in.nested_coefficients_present = true;
	for (core = 0; core < 2; core++) {
		u8 *s = &in.nested_coefficients[core * 0x78u];
		in.nested_coefficients[0x0f + core * 3u] = (u8)(core + 2u);
		for (i = 0; i < 6; i++) {
			s[0x1a + i * 0x0a] = (u8)(i + 1u);
			s[0x56 + i * 0x0a] = (u8)(i + 5u);
		}
	}
	return in;
}

static int test_s8(u8 x)
{
	return x < 0x80 ? x : (int)x - 0x100;
}

static u8 oracle_9175_category(const struct ob_phy_9a121_input *in,
			       s8 arg, u16 r0, u16 r1)
{
	const u8 *p = in->nested_coefficients;
	static const int limit[8] = {-40,-5,20,40,55,80,100,116};
	int m = arg + test_s8(p[0x1a + (r0 & 1)]) +
		test_s8(p[0x24 + ((r0 >> 1) & 7)]) +
		test_s8(p[0x2e + ((r0 >> 4) & 7)]) +
		test_s8(p[0x38 + ((r0 >> 7) & 15)]) +
		test_s8(p[0x42 + ((r1 >> 4) & 7)]);
	unsigned int i;
	m *= 10;
	if (m <= limit[0]) return 0;
	if (m > limit[7]) return 7;
	for (i = 0; i < 7; i++)
		if (m >= limit[i] && m <= limit[i + 1])
			return (u8)(m - limit[i] < limit[i + 1] - m ? i : i + 1);
	return 0;
}

static void test_zero_core_is_unchanged(void)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(0);
	f.regs[0x019e] = 0x1040;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == 0);
	assert(result.status == OB_9A121_SYNTHETIC_COMPLETE);
	assert(result.callback_count == 5);
	assert(result.terminal_boundary == 0x9a538);
	assert(f.regs[0x019e] == 0x1040);
}

static void test_one_core_complete_and_prefix_exact(void)
{
	static const u32 prefix[] = {
		0x9a1af,0x9a1ca,0x9a1ca,0x9a304,0x99fef,0x99fff,0x9a04f,
		0x9a079,0x9a10a,0x9a0a4,0x9a10a,0x9a0fa,0x9a10a,0x9a0cf,
		0x9a10a,0x90506,0x9a3b4,0x90506,0x9a42c,
	};
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	u8 i;
	f.regs[0x019e] = 0x1040;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(result.status == OB_9A121_SYNTHETIC_COMPLETE);
	assert(result.callback_count == 33 && result.logical_event_count == 27);
	assert(result.terminal_boundary == 0x9a538);
	for (i = 0; i < sizeof(prefix)/sizeof(prefix[0]); i++)
		assert(f.log[i].site == prefix[i]);
	assert(f.log[4].reg == 0x06dc && f.log[5].reg == 0x06dd);
	assert(f.log[6].table == 0xf9 && f.log[6].offset == 7);
	assert(f.log[6].count == 1 && f.log[6].width == 16);
	assert(f.log[19].site == 0x91825 && f.log[19].reg == 0x06dc);
	assert(f.log[20].site == 0x9185b && f.log[20].reg == 0x06dd);
	assert(f.log[21].site == 0x91879 && f.log[21].reg == 0x06dd);
	assert(f.log[22].site == 0x91a4a && f.log[23].site == 0x91a4a);
	assert(f.log[24].site == 0x91b57 && f.log[25].site == 0x91b57);
	assert(f.log[26].site == 0x91cb9);
	assert(f.log[27].site == 0x91db8 && f.log[28].site == 0x91db8);
	assert(f.log[29].site == 0x91e3f && f.log[30].site == 0x91e3f);
	assert(f.log[31].site == 0x9a525 && f.log[32].site == 0x9a525);
	assert(f.log[25].reg == 0x45 && f.log[25].value == 0x80);
	assert(f.log[30].reg == 0x2c && f.log[30].value == 0x60);
	assert(result.sub_9175a_category[0] == 0);
	assert(result.sub_91b6e_category[0] == 2 && result.sub_91b6e_band[0] == 0);
	assert(result.child_returns[0] == 19 && result.child_returns[1] == 19);
	assert(result.child_returns[2] == 19 && result.child_returns[3] == 19);
	assert(result.child_returns[4] == 19);
	assert(result.sub_90493_returns[0] == 0xd4);
	assert(result.sub_90493_returns[1] == 0xd4);
	assert(result.sub_90493_samples[0] == 0 && result.sub_90493_samples[1] == 0);
	assert(result.next_sub_9175a_arg == (s8)0xe2);
	assert(result.next_sub_91b6e_arg == (s8)0xe1);
	assert(f.regs[0x019e] == 0x1040);
	assert(!result.hardware_ready && !result.phy_ready && !result.channel_ready);
}

static void test_runtime_reads_drive_both_sub90493_invocations(void)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	in.nested_66e = 1;
	in.nested_66f = 30;
	in.nested_coefficients[0x10] = 5;
	f.helper_samples[0] = f.helper_samples[1] = 1;
	f.caller_samples[0] = f.caller_samples[1] = 1;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(result.sub_90493_samples[0] == 1);
	assert(result.sub_90493_samples[1] == 1);
	assert(result.caller_phy_samples[0] == 1);
	assert(result.caller_phy_samples[1] == 1);
	assert(f.log[15].value == 1 && f.log[16].value == 1);
	assert(f.log[17].value == 1 && f.log[18].value == 1);
	assert(result.sub_90493_returns[0] != 28);
}

static void test_per_output_candidate_count_changes_matching_byte(void)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	u8 *row = in.nested_coefficients;
	in.nested_64a_64f[4] = 2;
	row[0x1a + 4 * 0x0a + 1] = 0;
	row[0x56 + 4 * 0x0a + 1] = 12;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(f.log[5].value == 0x0ac4);
}

static void test_core_threshold_caps_high_candidate_at_output_four(void)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	u8 *row = in.nested_coefficients;
	in.nested_64a_64f[4] = 2;
	row[0x1a + 4 * 0x0a + 1] = 20;
	row[0x56 + 4 * 0x0a + 1] = 12;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(f.log[5].value == 0x0a94);
}

static void test_core_one_and_two_core_order(void)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(2);
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(result.callback_count == 33 && result.selected_core == 1);
	assert(f.log[3].reg == 0x08dc && f.log[19].reg == 0x08dc);
	assert(f.log[20].reg == 0x08dd && f.log[21].reg == 0x08dd);
	assert(f.log[25].reg == 0x0245 && f.log[30].reg == 0x022c);

	memset(&f, 0, sizeof(f));
	in = make_input(3);
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(result.status == OB_9A121_SYNTHETIC_COMPLETE);
	assert(result.callback_count == 61 && result.selected_core == 1);
	assert(f.log[31].site == 0x9a304 && f.log[31].reg == 0x08dc);
	assert(f.log[47].site == 0x91825 && f.log[47].reg == 0x08dc);
	assert(f.log[52].reg == 0x0245 && f.log[57].reg == 0x022c);
	assert(f.log[59].site == 0x9a525 && f.log[60].site == 0x9a525);
}

static void test_91b6e_uses_explicit_chanspec_radio_bank(void)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	in.chanspec = 0xc001;
	assert(ob_phy_9a121_test_only(&ops, &f, &in, &result) == 0);
	assert(result.status == OB_9A121_SYNTHETIC_COMPLETE);
	assert(f.log[f.count - 4].site == 0x91e3f);
	assert(f.log[f.count - 4].kind == RADIO_READ16);
	assert(f.log[f.count - 4].reg == 0x33);
	assert(f.log[f.count - 3].site == 0x91e3f);
	assert(f.log[f.count - 3].kind == RADIO_WRITE16);
	assert(f.log[f.count - 3].reg == 0x33);
}

static void test_9175a_signed_and_single_47e_consumption(void)
{
	struct fake f = { .selected_samples_present = true };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	u8 expected, candidate;
	in.nested_coefficients[0x1a] = 0xf0;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	expected = oracle_9175_category(&in, result.next_sub_9175a_arg, 0, 0);
	assert(result.sub_9175a_category[0] == expected);

	for (candidate = 0; candidate < 128; candidate++) {
		memset(&f, 0, sizeof(f));
		f.selected_samples_present = true;
		f.radio_regs[0x45] = 0xffff;
		in = make_input(1);
		in.nested_coefficients[0x2e] = candidate;
		assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
		expected = oracle_9175_category(&in, result.next_sub_9175a_arg,
						0, 0);
		assert(result.sub_9175a_category[0] == expected);
		if (expected == 2)
			break;
	}
	assert(candidate < 128);
	assert(f.log[25].value == 0xfdff);
}

static void test_runtime_samples_and_high_category_path(void)
{
	struct fake f = { .selected_samples_present = true };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	u8 fail;
	f.selected_samples[0] = 0;
	f.selected_samples[1] = 0;
	f.selected_samples[2] = 0;
	f.selected_samples[3] = 0;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(result.sub_9175a_samples[0][0] == 0);
	assert(result.sub_91b6e_samples[0] == 0);

	memset(&f, 0, sizeof(f));
	in = make_input(1);
	in.nested_coefficients[0x1a] = 100;
	f.radio_regs[0x45] = 0xffff;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == 0);
	assert(result.status == OB_9A121_SYNTHETIC_COMPLETE);
	assert(result.sub_9175a_category[0] == 7);
	assert(f.count == 37);
	assert(f.log[22].site == 0x9197d && f.log[23].site == 0x9197d);
	assert(f.log[24].site == 0x919b2 && f.log[25].site == 0x919b2);
	assert(f.log[26].site == 0x91a4a && f.log[27].site == 0x91a4a);
	assert(f.log[28].site == 0x91b57 && f.log[29].site == 0x91b57);
	assert(f.log[29].reg == 0x45 && f.log[29].value == 0xffbf);
	assert(f.log[35].site == 0x9a525 && f.log[36].site == 0x9a525);
	for (fail = 1; fail <= f.count; fail++) {
		struct fake failed = { .fail_at = fail };
		struct ob_phy_9a121_result failed_result;
		int ret = ob_phy_9a121_test_only(&ops, &failed, &in,
						 &failed_result);
		assert(ret == -73);
		assert(failed.count == fail);
		assert(failed_result.status == OB_9A121_FAILED_BACKEND_OPERATION);
		assert(failed_result.callback_count == fail);
		assert(failed_result.terminal_boundary == 0);
	}
}

static void test_fail_stop_each_single_core_callback(void)
{
	struct ob_phy_9a121_input in = make_input(1);
	u8 fail;
	for (fail = 1; fail <= 33; fail++) {
		struct fake f = { .fail_at = fail };
		struct ob_phy_9a121_result result;
		assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == -73);
		assert(f.count == fail && result.callback_count == fail);
		assert(result.status == OB_9A121_FAILED_BACKEND_OPERATION);
		assert(result.terminal_boundary == 0);
		assert(!result.hardware_ready && !result.phy_ready && !result.channel_ready);
	}
}

static void test_fail_stop_each_dual_core_callback(void)
{
	struct ob_phy_9a121_input in = make_input(3);
	u8 fail;
	for (fail = 1; fail <= 61; fail++) {
		struct fake f = { .fail_at = fail };
		struct ob_phy_9a121_result result;
		assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == -73);
		assert(f.count == fail && result.callback_count == fail);
		assert(result.status == OB_9A121_FAILED_BACKEND_OPERATION);
		assert(result.terminal_boundary == 0);
	}
}

static void test_input_and_identity_rejection(void)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	in.nested_coefficients_present = false;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == -EINVAL);
	assert(f.count == 0);
	in = make_input(1); in.core_mask = 0x04;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == -EINVAL);
	assert(f.count == 0);
	in = make_input(1); in.phy_revision = 9;
	assert(ob_phy_9a121_test_only(&ops,&f,&in,&result) == -ENODEV);
	assert(f.count == 0);
}

static void emit_mask_one_trace(bool high_category)
{
	struct fake f = { 0 };
	struct ob_phy_9a121_result result;
	struct ob_phy_9a121_input in = make_input(1);
	u8 i;
	if (high_category) {
		in.nested_coefficients[0x1a] = 100;
		f.radio_regs[0x45] = 0xffff;
	}
	int ret = ob_phy_9a121_test_only(&ops, &f, &in, &result);
	assert(ret == 0 && result.callback_count == f.count);
	for (i = 0; i < f.count; i++)
		printf("%08x %u %04x\n", f.log[i].site,
		       (unsigned int)f.log[i].kind, f.log[i].reg);
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--trace-mask1") == 0) {
		emit_mask_one_trace(false);
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--trace-high-mask1") == 0) {
		emit_mask_one_trace(true);
		return 0;
	}
	test_zero_core_is_unchanged();
	test_one_core_complete_and_prefix_exact();
	test_runtime_reads_drive_both_sub90493_invocations();
	test_per_output_candidate_count_changes_matching_byte();
	test_core_threshold_caps_high_candidate_at_output_four();
	test_core_one_and_two_core_order();
	test_91b6e_uses_explicit_chanspec_radio_bank();
	test_9175a_signed_and_single_47e_consumption();
	test_runtime_samples_and_high_category_path();
	test_fail_stop_each_single_core_callback();
	test_fail_stop_each_dual_core_callback();
	test_input_and_identity_rejection();
	puts("sub_9a121 active-core complete supported-scenario checks passed");
	return 0;
}

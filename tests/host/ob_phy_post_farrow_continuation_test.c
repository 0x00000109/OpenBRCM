/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy_post_farrow_continuation.h"

#define TRACE_MAX 16u

struct record {
	char kind;
	u8 event;
	u32 site;
	u16 reg;
	u16 value;
};

struct fake {
	struct record trace[TRACE_MAX];
	u8 count;
	u8 fail_at;
	u16 reg_019e;
	u16 reg_016c;
};

static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static void setup_input(struct ob_phy_post_farrow_software_input *in)
{
	unsigned int i;

	memset(in, 0, sizeof(*in));
	in->explicitly_synthetic = true;
	in->entry_after_farrow_join = true;
	in->chip_id = 0x4352;
	in->chip_revision = 3;
	in->phy_revision = 0x0a;
	in->radio_id = 0x2069;
	in->radio_revision = 4;
	in->phy_flags_19c = 0;
	in->initial_chanspec = 0x1001;
	in->pi_cache_240_present = true;
	in->pi_cache_240 = 0x55;
	in->nested_341_present = true;
	in->nested_341 = 1;
	in->nested_912_present = true;
	in->nested_912 = 0;
	in->nested_8b0_present = true;
	in->nested_8b0 = 0;
	in->nested_914_present = true;
	in->nested_914 = -1;
	in->nested_916_present = true;
	in->nested_916 = -1;
	for (i = 0; i < OB_PHY_POST_FARROW_BYTES; i++) {
		in->nested_65f[i] = (u8)(0x10 + i);
		in->nested_8b4[i] = (u8)(0x20 + i);
		in->cache_record_payload[i] = (u8)(0x30 + i);
	}
	in->cache_record_result_supplied = true;
	in->cache_record_result_present = true;
	in->cache_record_payload_present = true;
}

static int fake_append(struct fake *f, struct record *r)
{
	if (f->count == TRACE_MAX)
		return -ENOSPC;
	f->trace[f->count++] = *r;
	return f->fail_at == f->count ? -EIO : 0;
}

static int fake_read16(void *ctx, u8 event, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct record r = { .kind = 'R', .event = event, .site = site,
		.reg = reg };
	int ret;

	if (!value)
		return -EINVAL;
	if (reg == 0x019e)
		r.value = f->reg_019e;
	else if (reg == 0x016c)
		r.value = f->reg_016c;
	else
		return -EINVAL;
	ret = fake_append(f, &r);
	if (!ret)
		*value = r.value;
	return ret;
}

static int fake_write16(void *ctx, u8 event, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct record r = { .kind = 'W', .event = event, .site = site,
		.reg = reg, .value = value };
	int ret;

	ret = fake_append(f, &r);
	if (!ret && reg == 0x019e)
		f->reg_019e = value;
	else if (!ret && reg == 0x016c)
		f->reg_016c = value;
	return ret;
}

static const struct ob_phy_post_farrow_hw_ops fake_ops = {
	.read16 = fake_read16,
	.write16 = fake_write16,
};

static void test_zero_fork_and_cache_selected(void)
{
	struct ob_phy_post_farrow_software_input in;
	struct ob_phy_post_farrow_state state;
	struct ob_phy_post_farrow_software_result result;
	int ret;

	setup_input(&in);
	ret = ob_phy_post_farrow_continue_software_test_only(&in, &state,
									    &result);
	check("zero fork ret", ret, 0);
	check("cache refresh on flags zero", result.cache_helper_called, 1);
	check("software caller join", result.caller_joined, 1);
	check("sub_909fd unconditional", result.sub_909fd_called, 1);
	check("zero config arm", result.configuration_arm,
	      OB_PHY_POST_FARROW_CONFIG_ZERO);
	check("zero arm copy", result.copy_selected, 1);
	check("zero arm common rejoin", result.common_rejoin_site, 0x90b2e);
	check("software return", result.helper_return_site, 0x90b76);
	check("cache byte updated", state.pi_cache_240, 0x01);
	check("typed cache object selected", state.nested_8a8_has_record, 1);
	check("copied byte 0", state.nested_668[0], 0x30);
	check("copied byte 8", state.nested_668[8], 0x38);
	check("software callbacks", result.hardware_callbacks, 0);
	check("no ready state", result.hardware_ready, 0);
}

static void test_cache_skip_and_nonzero_copy_arm(void)
{
	struct ob_phy_post_farrow_software_input in;
	struct ob_phy_post_farrow_state state;
	struct ob_phy_post_farrow_software_result result;

	setup_input(&in);
	in.phy_flags_19c = 0x0206;
	in.pi_cache_240 = 0x22;
	in.nested_8b0 = 7;
	check("nonzero copy ret",
	      ob_phy_post_farrow_continue_software_test_only(&in, &state,
								     &result), 0);
	check("cache refresh skipped", result.cache_helper_called, 0);
	check("sub_909fd still called", result.sub_909fd_called, 1);
	check("nonzero config arm", result.configuration_arm,
	      OB_PHY_POST_FARROW_CONFIG_NONZERO);
	check("nonzero copy arm", result.copy_selected, 1);
	check("fallback bytes copied", state.nested_668[0], 0x10);
	check("caller rejoin retained", result.caller_joined, 1);
}

static void test_nonzero_merge_and_signed_post_join(void)
{
	struct ob_phy_post_farrow_software_input in;
	struct ob_phy_post_farrow_state state;
	struct ob_phy_post_farrow_software_result result;

	setup_input(&in);
	in.nested_8b0 = 0x1234;
	in.nested_914 = 0;
	in.nested_916 = -1;
	check("nonzero merge ret",
	      ob_phy_post_farrow_continue_software_test_only(&in, &state,
								     &result), 0);
	check("nonzero merge selected", result.copy_selected, 0);
	check("max merge byte 0", state.nested_668[0], 0x30);
	check("max merge byte 1", state.nested_668[1], 0x31);
	check("max merge byte 2", state.nested_668[2], 0x32);
	check("last byte OR", state.nested_668[8], 0x38);
	check("post join forces 66e", state.nested_668[6], 1);
	check("cache side effect still selected", result.cache_helper_called, 1);
	check("hardware callbacks absent", result.hardware_callbacks, 0);
}

static void test_nonzero_band_copy_and_input_rejection(void)
{
	struct ob_phy_post_farrow_software_input in;
	struct ob_phy_post_farrow_state state;
	struct ob_phy_post_farrow_software_result result;
	struct ob_phy_post_farrow_state untouched;

	setup_input(&in);
	in.nested_8b0 = 1;
	in.initial_chanspec = 0xd001;
	in.phy_flags_19c = 0;
	check("band copy ret",
	      ob_phy_post_farrow_continue_software_test_only(&in, &state,
								     &result), 0);
	check("band c000 copies", result.copy_selected, 1);

	memset(&state, 0xa5, sizeof(state));
	untouched = state;
	in.nested_8b0_present = false;
	check("missing external config rejected",
	      ob_phy_post_farrow_continue_software_test_only(&in, &state,
								     &result), -EINVAL);
	check("invalid result", result.kind, OB_PHY_POST_FARROW_SW_INVALID);
	check("no state publication", memcmp(&state, &untouched, sizeof(state)), 0);

	setup_input(&in);
	in.cache_record_result_supplied = false;
	in.cache_record_result_present = false;
	check("missing cache object rejected",
	      ob_phy_post_farrow_continue_software_test_only(&in, &state,
								     &result), -EINVAL);
}

static void test_explicit_null_cache_result_uses_nested_fallback(void)
{
	struct ob_phy_post_farrow_software_input in;
	struct ob_phy_post_farrow_state state;
	struct ob_phy_post_farrow_software_result result;

	setup_input(&in);
	in.cache_record_result_present = false;
	in.cache_record_payload_present = false;
	check("explicit null cache result modeled",
	      ob_phy_post_farrow_continue_software_test_only(&in, &state,
							     &result), 0);
	check("null record remains null", state.nested_8a8_has_record, 0);
	check("nested fallback selected", state.nested_668[0], 0x10);
	check("null cache is still refresh call", result.cache_helper_called, 1);
}

static void test_desense_prefix_trace_and_fail_stop(void)
{
	static const struct record expected[] = {
		{ 'R', 1, 0x9b0cd, 0x019e, 0xa540 },
		{ 'R', 2, 0x9b0eb, 0x019e, 0xa540 },
		{ 'W', 2, 0x9b0eb, 0x019e, 0xa542 },
		{ 'R', 3, 0x9b0f8, 0x016c, 0x0001 },
		{ 'R', 4, 0x9b116, 0x016c, 0x0001 },
		{ 'W', 4, 0x9b116, 0x016c, 0x0041 },
	};
	struct ob_phy_post_farrow_software_input in;
	struct ob_phy_post_farrow_state state;
	struct ob_phy_post_farrow_software_result sw_result;
	struct ob_phy_desense_prefix_result result;
	struct ob_phy_post_farrow_hw_ops missing_ops = { 0 };
	struct fake f;
	unsigned int i;

	setup_input(&in);
	check("software state ready for prefix",
	      ob_phy_post_farrow_continue_software_test_only(&in, &state,
								     &sw_result), 0);
	memset(&f, 0, sizeof(f));
	f.reg_019e = 0xa540;
	f.reg_016c = 0x0001;
	check("prefix ret", ob_phy_desense_9af05_prefix_test_only(
			&fake_ops, &f, &state, &result), 0);
	check("prefix status", result.kind, OB_PHY_DESENSE_PREFIX_COMPLETE);
	check("prefix logical events", result.logical_events, 4);
	check("prefix callbacks", result.callback_records, 6);
	check("terminal before selected child", result.terminal_boundary,
	      0x9b123);
	check("helper not complete", result.desense_helper_complete, 0);
	check("prefix not hardware ready", result.hardware_ready, 0);
	check("trace length", f.count, sizeof(expected) / sizeof(expected[0]));
	for (i = 0; i < f.count && i < sizeof(expected) / sizeof(expected[0]); i++) {
		check("trace kind", f.trace[i].kind, expected[i].kind);
		check("trace event", f.trace[i].event, expected[i].event);
		check("trace site", f.trace[i].site, expected[i].site);
		check("trace register", f.trace[i].reg, expected[i].reg);
		check("trace value", f.trace[i].value, expected[i].value);
	}
	check("saved 019e state", result.saved_019e, 0xa540);
	check("saved 016c state", result.saved_016c, 0x0001);

	for (i = 1; i <= 6; i++) {
		memset(&f, 0, sizeof(f));
		f.reg_019e = 0xa540;
		f.reg_016c = 1;
		f.fail_at = (u8)i;
		check("failure propagated", ob_phy_desense_9af05_prefix_test_only(
				&fake_ops, &f, &state, &result), -EIO);
		check("failed status", result.kind, OB_PHY_DESENSE_PREFIX_FAILED);
		check("no callback after failure", f.count, (long)i);
		check("failure not helper completion",
		      result.desense_helper_complete, 0);
	}
	memset(&f, 0, sizeof(f));
	f.reg_019e = 0xa540;
	f.reg_016c = 1;
	check("missing callbacks prevalidated",
	      ob_phy_desense_9af05_prefix_test_only(&missing_ops, &f, &state,
							     &result), -EINVAL);
	check("missing callback no accesses", f.count, 0);
}

int main(void)
{
	test_zero_fork_and_cache_selected();
	test_cache_skip_and_nonzero_copy_arm();
	test_nonzero_merge_and_signed_post_join();
	test_nonzero_band_copy_and_input_rejection();
	test_explicit_null_cache_result_uses_nested_fallback();
	test_desense_prefix_trace_and_fail_stop();
	if (failures) {
		printf("%u post-Farrow continuation checks failed\n", failures);
		return 1;
	}
	puts("post-Farrow software fork and desense-prefix checks passed");
	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests for the W2 firmware-chain contract.
 *
 * These tests execute the same ob_fw_chain_core() used by the kernel adapter.
 * They prove ordering, stop-on-first-failure, retention after mutation and the
 * profile/runtime identity predicate; they do not prove silicon behaviour.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_fw_chain_contract.h"
#include "../../src/ob_ucode.h"

struct fake {
	int seq[8];
	int n;
	int identity_calls;
	int dev_lost_calls;
	int identity_ret;
	int ucode_ret;
	int publish_ucode_ret;
	int common_ret;
	int publish_common_ret;
	bool dev_lost;
	bool ucode_mutated;
	enum ob_fw_chain_stage ucode_stage;
	enum ob_fw_chain_stage common_stage;
	bool runtime_seen;
};

static void record(struct fake *f, int code)
{
	assert(f->n < (int)(sizeof(f->seq) / sizeof(f->seq[0])));
	f->seq[f->n++] = code;
}

static int fake_identity(void *ctx, bool runtime_required)
{
	struct fake *f = ctx;

	f->identity_calls++;
	f->runtime_seen = runtime_required;
	record(f, 1);
	return f->identity_ret;
}

static int fake_ucode(void *ctx, struct ob_fw_chain_ucode_result *out)
{
	struct fake *f = ctx;

	record(f, 2);
	out->name = "ucode.bin";
	out->words = 10850;
	out->psm_polls = 42;
	out->mutated = f->ucode_mutated;
	out->ret = f->ucode_ret;
	out->stage = f->ucode_ret ? f->ucode_stage : OB_FW_CHAIN_DONE;
	return f->ucode_ret;
}

static int fake_publish_ucode(void *ctx)
{
	struct fake *f = ctx;

	record(f, 3);
	return f->publish_ucode_ret;
}

static int fake_common(void *ctx, struct ob_fw_chain_common_result *out)
{
	struct fake *f = ctx;

	record(f, 4);
	out->name = "common.bin";
	out->records = f->common_ret ? 3 : 610;
	out->ret = f->common_ret;
	out->stage = f->common_ret ? f->common_stage : OB_FW_CHAIN_DONE;
	return f->common_ret;
}

static int fake_publish_common(void *ctx)
{
	struct fake *f = ctx;

	record(f, 5);
	return f->publish_common_ret;
}

static bool fake_dev_lost(void *ctx)
{
	struct fake *f = ctx;

	f->dev_lost_calls++;
	return f->dev_lost;
}

static void init_fake(struct fake *f)
{
	memset(f, 0, sizeof(*f));
	f->ucode_stage = OB_FW_CHAIN_PSM_POLL;
	f->common_stage = OB_FW_CHAIN_COMMON_APPLY;
}

static void init_ops(struct ob_fw_chain_ops *ops, struct fake *f)
{
	memset(ops, 0, sizeof(*ops));
	ops->ctx = f;
	ops->identity = fake_identity;
	ops->ucode = fake_ucode;
	ops->publish_ucode = fake_publish_ucode;
	ops->common = fake_common;
	ops->publish_common = fake_publish_common;
	ops->dev_lost = fake_dev_lost;
}

static void test_success(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == 0);
	assert(r.stage == OB_FW_CHAIN_DONE);
	assert(!strcmp(r.stage_name, "done"));
	assert(r.ret == 0);
	assert(r.mutated);
	assert(!r.retained);
	assert(r.runtime_identity);
	assert(f.runtime_seen);
	assert(f.identity_calls == 1);
	assert(f.dev_lost_calls == 1);
	assert(f.n == 5);
	assert(f.seq[0] == 1 && f.seq[1] == 2 && f.seq[2] == 3 &&
	       f.seq[3] == 4 && f.seq[4] == 5);
	assert(r.words == 10850 && r.psm_polls == 42 && r.records == 610);
	assert(!strcmp(r.image_name, "common.bin"));
}

static void test_identity_refusal(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	f.identity_ret = -22;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -22);
	assert(r.stage == OB_FW_CHAIN_IDENTITY);
	assert(!r.mutated && !r.retained);
	assert(f.n == 1 && f.seq[0] == 1);
	assert(f.identity_calls == 1);
}

static void test_ucode_failure_without_mutation(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	f.ucode_ret = -5;
	f.ucode_mutated = false;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.stage == OB_FW_CHAIN_PSM_POLL);
	assert(!r.mutated && !r.retained);
	assert(f.n == 2 && f.seq[0] == 1 && f.seq[1] == 2);
	assert(!strcmp(r.image_name, "ucode.bin"));
}

static void test_ucode_failure_retains(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	f.ucode_ret = -110;
	f.ucode_mutated = true;
	f.ucode_stage = OB_FW_CHAIN_PSM_POLL;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -110);
	assert(r.stage == OB_FW_CHAIN_PSM_POLL);
	assert(r.mutated && r.retained);
	assert(f.n == 2); /* no publish_ucode, no common */
}

static void test_common_failure_retains(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	f.common_ret = -5;
	f.common_stage = OB_FW_CHAIN_POST_CHECK;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.stage == OB_FW_CHAIN_POST_CHECK);
	assert(r.mutated && r.retained);
	assert(f.n == 4);
	assert(f.seq[3] == 4); /* publish_common was not called */
}

static void test_lifecycle_failures_retain(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	f.publish_ucode_ret = -5;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.stage == OB_FW_CHAIN_LIFECYCLE);
	assert(r.mutated && r.retained);
	assert(f.n == 3);

	init_fake(&f);
	f.publish_common_ret = -5;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.stage == OB_FW_CHAIN_LIFECYCLE);
	assert(r.mutated && r.retained);
	assert(f.n == 5);
}

static void test_dev_lost_is_retained_without_callbacks(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	f.dev_lost = true;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.stage == OB_FW_CHAIN_IDENTITY);
	assert(r.retained && !r.mutated);
	assert(f.n == 0);
	assert(f.identity_calls == 0);
}

static void test_isolated_policy_skips_lifecycle(void)
{
	const struct ob_fw_chain_policy policy = { false, false };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == 0);
	assert(r.stage == OB_FW_CHAIN_DONE);
	assert(!r.runtime_identity);
	assert(!f.runtime_seen);
	assert(f.n == 3);
	assert(f.seq[0] == 1 && f.seq[1] == 2 && f.seq[2] == 4);
}

static void test_identity_predicate(void)
{
	struct ob_fw_chain_identity_view v;

	memset(&v, 0, sizeof(v));
	v.profile_present = true;
	v.profile_enabled = true;
	v.profile_phy_known = true;
	v.profile_radio_known = true;
	v.chip_id = v.profile_chip_id = 0x43a0;
	v.chip_rev = v.profile_chip_rev = 3;
	v.d11_manuf = v.profile_d11_manuf = 0x04bf;
	v.d11_core_id = v.profile_d11_core_id = 0x0812;
	v.d11_rev = v.profile_d11_rev = 42;
	v.d11_class = v.profile_d11_class = 0x0b;
	v.phy_type = v.profile_phy_type = 0x0b;
	v.phy_rev = v.profile_phy_rev = 0x2a;
	v.radio_id = v.profile_radio_id = 0x2069;
	v.radio_rev = v.profile_radio_rev = 4;
	v.radio_class = v.profile_radio_class = 4;
	assert(ob_fw_chain_identity_ok(&v, false));
	assert(!ob_fw_chain_identity_ok(&v, true)); /* runtime not published */
	v.runtime_valid = true;
	assert(ob_fw_chain_identity_ok(&v, true));
	v.radio_class++;
	assert(!ob_fw_chain_identity_ok(&v, true));
	assert(ob_fw_chain_identity_ok(&v, false));
	v.profile_radio_known = false;
	assert(!ob_fw_chain_identity_ok(&v, false));
}

static void test_select26(void)
{
	enum ob_isolated_mode mode;

	assert(ob_isolated_mode_select26(false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 true) == OB_ISOLATED_FW_CHAIN_TEST);
	assert(ob_isolated_mode_select26(false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false) == OB_ISOLATED_NONE);
	assert(ob_isolated_mode_select26(true, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 false, false, false, false, false,
					 true) == OB_ISOLATED_CONFLICT);
	mode = OB_ISOLATED_FW_CHAIN_TEST;
	assert(ob_isolated_mode_conflict(mode) == false);
	assert(ob_isolated_mode_applies_initvals(mode));
	assert(ob_isolated_mode_skips_teardown(mode));
	/* The appended value must not change any existing enumerator. */
	assert((unsigned int)OB_ISOLATED_NONE == 0u);
	assert((unsigned int)OB_ISOLATED_FW_VALIDATE == 1u);
	assert((unsigned int)OB_ISOLATED_FW_CHAIN_TEST >
	       (unsigned int)OB_ISOLATED_ACPHY_IDENTITY_TIMING);
}

int main(void)
{
	test_success();
	test_identity_refusal();
	test_ucode_failure_without_mutation();
	test_ucode_failure_retains();
	test_common_failure_retains();
	test_lifecycle_failures_retain();
	test_dev_lost_is_retained_without_callbacks();
	test_isolated_policy_skips_lifecycle();
	test_identity_predicate();
	test_select26();
	printf("ob_fw_chain_test: PASS\n");
	return 0;
}

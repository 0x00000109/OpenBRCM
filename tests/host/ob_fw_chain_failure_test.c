// SPDX-License-Identifier: GPL-2.0-only
/*
 * W3 host failure matrix for the W2 firmware-chain contract.
 *
 * This test executes the same ob_fw_chain_core() used by the kernel adapter.
 * It enumerates every failure stage and checks the exact result, stop-on-first-
 * failure ordering and the retain-after-mutation rule.  It is a host contract
 * test only: it says nothing about silicon behaviour.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_fw_chain_contract.h"

enum {
	SEQ_IDENTITY = 1,
	SEQ_UCODE = 2,
	SEQ_PUBLISH_UCODE = 3,
	SEQ_COMMON = 4,
	SEQ_PUBLISH_COMMON = 5,
};

struct fake {
	int seq[8];
	int n;
	int identity_calls;
	int dev_lost_calls;
	int identity_ret;
	int ucode_ret;
	int ucode_out_ret;
	int publish_ucode_ret;
	int common_ret;
	int common_out_ret;
	int publish_common_ret;
	bool dev_lost;
	bool ucode_mutated;
	bool runtime_seen;
	enum ob_fw_chain_stage ucode_stage;
	enum ob_fw_chain_stage common_stage;
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
	record(f, SEQ_IDENTITY);
	return f->identity_ret;
}

static int fake_ucode(void *ctx, struct ob_fw_chain_ucode_result *out)
{
	struct fake *f = ctx;

	record(f, SEQ_UCODE);
	out->name = "ucode.bin";
	out->words = 10850;
	out->psm_polls = 42;
	out->mutated = f->ucode_mutated;
	out->ret = f->ucode_out_ret;
	out->stage = f->ucode_ret ? f->ucode_stage : OB_FW_CHAIN_DONE;
	return f->ucode_ret;
}

static int fake_publish_ucode(void *ctx)
{
	struct fake *f = ctx;

	record(f, SEQ_PUBLISH_UCODE);
	return f->publish_ucode_ret;
}

static int fake_common(void *ctx, struct ob_fw_chain_common_result *out)
{
	struct fake *f = ctx;

	record(f, SEQ_COMMON);
	out->name = "common.bin";
	out->records = f->common_ret ? 7 : 610;
	out->ret = f->common_out_ret;
	out->stage = f->common_ret ? f->common_stage : OB_FW_CHAIN_DONE;
	return f->common_ret;
}

static int fake_publish_common(void *ctx)
{
	struct fake *f = ctx;

	record(f, SEQ_PUBLISH_COMMON);
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
	f->ucode_stage = OB_FW_CHAIN_UCODE_PREP;
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

static enum ob_fw_chain_stage mapped_ucode_stage(enum ob_fw_chain_stage s)
{
	if (s == OB_FW_CHAIN_DONE || s == OB_FW_CHAIN_IDENTITY)
		return OB_FW_CHAIN_UCODE_PREP;
	return s;
}

static enum ob_fw_chain_stage mapped_common_stage(enum ob_fw_chain_stage s)
{
	if (s == OB_FW_CHAIN_DONE || s == OB_FW_CHAIN_IDENTITY)
		return OB_FW_CHAIN_COMMON_APPLY;
	return s;
}

static void test_ucode_stage_matrix(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	int s;
	int mutated;

	for (s = OB_FW_CHAIN_IDENTITY; s <= OB_FW_CHAIN_DONE; s++) {
		for (mutated = 0; mutated <= 1; mutated++) {
			struct ob_fw_chain_result r;
			struct ob_fw_chain_ops ops;
			struct fake f;
			enum ob_fw_chain_stage expected;

			init_fake(&f);
			f.ucode_ret = -5;
			f.ucode_out_ret = -5;
			f.ucode_stage = (enum ob_fw_chain_stage)s;
			f.ucode_mutated = mutated != 0;
			init_ops(&ops, &f);

			assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
			expected = mapped_ucode_stage((enum ob_fw_chain_stage)s);
			assert(r.stage == expected);
			assert(!strcmp(r.stage_name,
				       ob_fw_chain_stage_name(expected)));
			assert(r.ret == -5);
			assert(r.mutated == (mutated != 0));
			assert(r.retained == (mutated != 0));
			assert(f.n == 2);
			assert(f.seq[0] == SEQ_IDENTITY);
			assert(f.seq[1] == SEQ_UCODE);
			assert(!strcmp(r.image_name, "ucode.bin"));
			assert(r.words == 10850);
			assert(r.psm_polls == 42);
		}
	}
}

static void test_ucode_return_precedence(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	/* An absent result ret must not erase the callback return. */
	init_fake(&f);
	f.ucode_ret = -5;
	f.ucode_out_ret = 0;
	f.ucode_stage = OB_FW_CHAIN_PSM_POLL;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.ret == -5);
	assert(r.stage == OB_FW_CHAIN_PSM_POLL);

	/* A concrete result ret wins over the callback return. */
	init_fake(&f);
	f.ucode_ret = -5;
	f.ucode_out_ret = -7;
	f.ucode_stage = OB_FW_CHAIN_PSM_POLL;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -7);
	assert(r.ret == -7);
	assert(r.stage == OB_FW_CHAIN_PSM_POLL);
}

static void test_common_stage_matrix(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	int s;

	for (s = OB_FW_CHAIN_IDENTITY; s <= OB_FW_CHAIN_DONE; s++) {
		struct ob_fw_chain_result r;
		struct ob_fw_chain_ops ops;
		struct fake f;
		enum ob_fw_chain_stage expected;

		init_fake(&f);
		f.common_ret = -5;
		f.common_out_ret = -5;
		f.common_stage = (enum ob_fw_chain_stage)s;
		init_ops(&ops, &f);

		assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
		expected = mapped_common_stage((enum ob_fw_chain_stage)s);
		assert(r.stage == expected);
		assert(!strcmp(r.stage_name, ob_fw_chain_stage_name(expected)));
		assert(r.ret == -5);
		assert(r.mutated);
		assert(r.retained);
		assert(f.n == 4);
		assert(f.seq[0] == SEQ_IDENTITY);
		assert(f.seq[1] == SEQ_UCODE);
		assert(f.seq[2] == SEQ_PUBLISH_UCODE);
		assert(f.seq[3] == SEQ_COMMON);
		assert(!strcmp(r.image_name, "common.bin"));
		assert(r.records == 7);
	}
}

static void test_common_return_precedence(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	f.common_ret = -5;
	f.common_out_ret = 0;
	f.common_stage = OB_FW_CHAIN_POST_CHECK;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.ret == -5);
	assert(r.stage == OB_FW_CHAIN_POST_CHECK);

	init_fake(&f);
	f.common_ret = -5;
	f.common_out_ret = -7;
	f.common_stage = OB_FW_CHAIN_POST_CHECK;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -7);
	assert(r.ret == -7);
	assert(r.stage == OB_FW_CHAIN_POST_CHECK);
}

static void test_identity_failure_matrix(void)
{
	static const int rets[] = { -22, -19, -5, -517, -12 };
	const struct ob_fw_chain_policy policy = { true, true };
	size_t i;

	for (i = 0; i < sizeof(rets) / sizeof(rets[0]); i++) {
		struct ob_fw_chain_result r;
		struct ob_fw_chain_ops ops;
		struct fake f;

		init_fake(&f);
		f.identity_ret = rets[i];
		init_ops(&ops, &f);
		assert(ob_fw_chain_core(&ops, &policy, &r) == rets[i]);
		assert(r.stage == OB_FW_CHAIN_IDENTITY);
		assert(!strcmp(r.stage_name, "identity"));
		assert(r.ret == rets[i]);
		assert(!r.mutated);
		assert(!r.retained);
		assert(f.n == 1);
		assert(f.seq[0] == SEQ_IDENTITY);
	}
}

static void test_lifecycle_failure_matrix(void)
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
	assert(!strcmp(r.stage_name, "lifecycle"));
	assert(r.mutated && r.retained);
	assert(f.n == 3);
	assert(f.seq[0] == SEQ_IDENTITY);
	assert(f.seq[1] == SEQ_UCODE);
	assert(f.seq[2] == SEQ_PUBLISH_UCODE);

	init_fake(&f);
	f.publish_common_ret = -5;
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(&ops, &policy, &r) == -5);
	assert(r.stage == OB_FW_CHAIN_LIFECYCLE);
	assert(r.mutated && r.retained);
	assert(r.records == 610);
	assert(f.n == 5);
	assert(f.seq[4] == SEQ_PUBLISH_COMMON);
}

static void test_predev_lost(void)
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
	assert(r.ret == -5);
	assert(!r.mutated);
	assert(r.retained);
	assert(f.n == 0);
	assert(f.identity_calls == 0);
	assert(f.dev_lost_calls == 1);
}

static void test_api_guards(void)
{
	const struct ob_fw_chain_policy policy = { true, true };
	struct ob_fw_chain_result r;
	struct ob_fw_chain_ops ops;
	struct fake f;

	init_fake(&f);
	init_ops(&ops, &f);
	assert(ob_fw_chain_core(NULL, &policy, &r) == -22);
	assert(ob_fw_chain_core(&ops, NULL, &r) == -22);
	assert(ob_fw_chain_core(&ops, &policy, NULL) == -22);

	init_ops(&ops, &f);
	ops.identity = NULL;
	assert(ob_fw_chain_core(&ops, &policy, &r) == -22);
	init_ops(&ops, &f);
	ops.ucode = NULL;
	assert(ob_fw_chain_core(&ops, &policy, &r) == -22);
	init_ops(&ops, &f);
	ops.common = NULL;
	assert(ob_fw_chain_core(&ops, &policy, &r) == -22);
	init_ops(&ops, &f);
	ops.dev_lost = NULL;
	assert(ob_fw_chain_core(&ops, &policy, &r) == -22);
	init_ops(&ops, &f);
	ops.publish_ucode = NULL;
	assert(ob_fw_chain_core(&ops, &policy, &r) == -22);
	init_ops(&ops, &f);
	ops.publish_common = NULL;
	assert(ob_fw_chain_core(&ops, &policy, &r) == -22);
	assert(f.n == 0);
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
	assert(r.words == 10850);
	assert(r.records == 610);
	assert(r.psm_polls == 42);
	assert(!strcmp(r.image_name, "common.bin"));
	assert(f.identity_calls == 1);
	assert(f.dev_lost_calls == 1);
	assert(f.n == 5);
	assert(f.seq[0] == SEQ_IDENTITY);
	assert(f.seq[1] == SEQ_UCODE);
	assert(f.seq[2] == SEQ_PUBLISH_UCODE);
	assert(f.seq[3] == SEQ_COMMON);
	assert(f.seq[4] == SEQ_PUBLISH_COMMON);
}

static void test_isolated_policy(void)
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
	assert(f.seq[0] == SEQ_IDENTITY);
	assert(f.seq[1] == SEQ_UCODE);
	assert(f.seq[2] == SEQ_COMMON);
}

int main(void)
{
	test_ucode_stage_matrix();
	test_ucode_return_precedence();
	test_common_stage_matrix();
	test_common_return_precedence();
	test_identity_failure_matrix();
	test_lifecycle_failure_matrix();
	test_predev_lost();
	test_api_guards();
	test_success();
	test_isolated_policy();
	printf("ob_fw_chain_failure_test: PASS (ucode stages=%d, common stages=%d)\n",
	       (int)OB_FW_CHAIN_DONE + 1, (int)OB_FW_CHAIN_DONE + 1);
	return 0;
}

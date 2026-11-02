// SPDX-License-Identifier: GPL-2.0-only
/* Host contract tests for the isolated BCM4352 class-0 radio-local checkpoint. */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_radio.h"
#include "../../src/ob_ucode.h"

static int failures;

static void chk(const char *what, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, expected);
		failures++;
	}
}

struct fake_0413 {
	u16 regs[0x1000];
	u16 reads[128];
	u16 writes[128];
	u32 delays[128];
	unsigned int n;
	unsigned int delay_n;
	unsigned int read_n;
	unsigned int write_n;
	unsigned int quiesce_n;
	unsigned int quiesce_polls;
	int read_fail;
	int write_fail;
	bool lost;
};

static int fake_quiesce(void *ctx, bool *transitioned, u32 *polls)
{
	struct fake_0413 *f = ctx;

	f->quiesce_n++;
	*transitioned = false;
	*polls = f->quiesce_polls;
	return f->lost ? -5 : 0;
}

static int fake_read16(void *ctx, u16 reg, u16 *value)
{
	struct fake_0413 *f = ctx;

	if (f->read_fail || f->lost)
		return -5;
	if (f->read_n < sizeof(f->reads) / sizeof(f->reads[0]))
		f->reads[f->read_n] = reg;
	f->read_n++;
	f->n++;
	*value = f->regs[reg];
	return 0;
}

static int fake_write16(void *ctx, u16 reg, u16 value)
{
	struct fake_0413 *f = ctx;

	if (f->write_fail || f->lost)
		return -5;
	if (f->write_n < sizeof(f->writes) / sizeof(f->writes[0])) {
		f->writes[f->write_n] = reg;
		f->regs[reg] = value;
	}
	f->write_n++;
	f->n++;
	return 0;
}

static void fake_delay(void *ctx, u32 usec)
{
	struct fake_0413 *f = ctx;

	if (f->delay_n < sizeof(f->delays) / sizeof(f->delays[0]))
		f->delays[f->delay_n] = usec;
	f->delay_n++;
	f->n++;
}

static bool fake_lost(void *ctx)
{
	return ((struct fake_0413 *)ctx)->lost;
}

static const struct ob_radio_local_0413_ops fake_ops = {
	.quiesce = fake_quiesce,
	.read16 = fake_read16,
	.write16 = fake_write16,
	.delay_us = fake_delay,
	.device_lost = fake_lost,
};

static void test_historical_rmw(void)
{
	struct fake_0413 f = { 0 };
	const struct ob_radio_access_ops access_ops = {
		.read16 = fake_read16,
		.write16 = fake_write16,
	};

	f.regs[0x8ea] = 0xffff;
	chk("historical mod rc", ob_radio_mod16_historical(
		&access_ops, &f, 0x8ea, 0x0080, 0x0080), 0);
	chk("historical mod value", f.regs[0x8ea], 0xffff);
	f.regs[0x8ed] = 0xffff;
	chk("value/mask differ", ob_radio_mod16_historical(
		&access_ops, &f, 0x8ed, 0x0600, 0x0400), 0);
	chk("value/mask composition", f.regs[0x8ed], 0xfdff);
	f.regs[0x410] = 0xffff;
	chk("mask zero clears value bits", ob_radio_mod16_historical(
		&access_ops, &f, 0x410, 0x0001, 0x0000), 0);
	chk("mask zero result", f.regs[0x410], 0xfffe);
}

static void test_exact_success_prefix(void)
{
	struct fake_0413 f = { 0 };
	struct ob_radio_local_0413_result result = { 0 };
	static const u16 expected_regs[] = {
		0x8ea, 0x8ed, 0x410, 0x410, 0x411, 0x412,
		0x410, 0x410, 0x411,
	};
	unsigned int i;

	f.regs[0x413] = 0x0010;
	chk("sequence success", ob_radio_local_0413_run(&fake_ops, &f,
		&result), 0);
	chk("sequence pass", result.success, true);
	chk("sequence attempts", result.attempts, 1);
	chk("quiesce first", f.quiesce_n, 1);
	chk("exact writes", f.write_n, 9);
	chk("first delay", f.delays[0], 1);
	chk("second delay", f.delays[1], 35);
	chk("poll delay", f.delays[2], 100);
	chk("poll read", f.reads[f.read_n - 1], 0x413);
	for (i = 0; i < sizeof(expected_regs) / sizeof(expected_regs[0]); i++)
		chk("write order", f.writes[i], expected_regs[i]);
	chk("no post-poll write", f.write_n, 9);
}

static void test_timeout_and_guard(void)
{
	struct fake_0413 f = { 0 };
	struct ob_radio_local_0413_result result = { 0 };

	f.regs[0x413] = 0;
	chk("timeout rc", ob_radio_local_0413_run(&fake_ops, &f, &result), -110);
	chk("timeout result", result.success, false);
	chk("timeout attempts", result.attempts, 100);
	chk("timeout no postpoll write", f.write_n, 9);
	memset(&f, 0, sizeof(f));
	f.read_fail = 1;
	chk("invalid read rc", ob_radio_local_0413_run(&fake_ops, &f, &result), -5);
	chk("invalid read no retry", f.read_n, 0);
	memset(&f, 0, sizeof(f));
	f.lost = true;
	chk("device loss before sequence", ob_radio_local_0413_run(&fake_ops,
		&f, &result), -5);
	chk("device loss no writes", f.write_n, 0);
}

static void test_target_gate(void)
{
	struct ob_radio_local_0413_target target = {
		.chip_id = 0x4352, .chip_rev = 3, .d11_corerev = 42,
		.phy_type = 0x0b, .phy_revision = 1, .radio_id = 0x2069,
		.radio_revision = 4, .radio_revision_class = 0,
		.chanspec = 0x1001, .chanspec_valid = true,
		.d11_identity_ready = true, .acphy_start_ready = true,
		.d11_core_enabled = true, .shm_00a0 = 0x1001,
		.wrapper_cflags = 0x40,
		.valid = true, .radio_access_initialized = true,
	};

	chk("pure target identity admitted before postcondition reads",
	    ob_radio_local_0413_identity_ok(&target), true);
	chk("target accepted", ob_radio_local_0413_target_ok(&target), true);
	target.shm_00a0 = 0;
	target.wrapper_cflags = 0;
	target.d11_core_enabled = false;
	chk("postcondition values do not affect pure identity gate",
	    ob_radio_local_0413_identity_ok(&target), true);
	chk("missing postconditions reject full target",
	    ob_radio_local_0413_target_ok(&target), false);
	target.shm_00a0 = 0x1001;
	target.wrapper_cflags = 0x40;
	target.d11_core_enabled = true;
	target.radio_revision_class = 1;
	chk("wrong class rejected before hardware reads",
	    ob_radio_local_0413_identity_ok(&target), false);
	chk("class one rejected", ob_radio_local_0413_target_ok(&target), false);
	target.radio_revision_class = 2;
	chk("class two rejected", ob_radio_local_0413_target_ok(&target), false);
	target.radio_revision_class = 0;
	target.shm_00a0 = 0x1002;
	chk("wrong chanspec postcondition rejected",
		ob_radio_local_0413_target_ok(&target), false);
}

int main(void)
{
	test_historical_rmw();
	test_exact_success_prefix();
	test_timeout_and_guard();
	test_target_gate();
	if (failures) {
		printf("ob_radio_local_0413_test: %d failures\n", failures);
		return 1;
	}
	puts("ob_radio_local_0413_test: PASS");
	return 0;
}

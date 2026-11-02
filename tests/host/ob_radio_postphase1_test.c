// SPDX-License-Identifier: GPL-2.0-only
/* Host-only exact-order and fail-stop tests for the phase-2 checkpoint. */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_radio_postphase1.h"

#define MAX_EVENTS 1024u
#define REG_COUNT 0x500u

enum event_kind { EV_READ, EV_WRITE, EV_DELAY, EV_MARK };
struct event {
	enum event_kind kind;
	u16 reg;
	u16 value;
	u32 delay;
	enum ob_radio_postphase1_marker marker;
	u16 sample;
	u16 low5;
	u32 attempts;
	u16 last_value;
	bool success;
};

struct fake_radio {
	u16 regs[REG_COUNT];
	struct event events[MAX_EVENTS];
	u32 event_count;
	u32 hardware_ops;
	u32 read_count;
	u32 delay_count;
	u32 reads_0413;
	u32 reads_0416;
	u32 success_at;
	u32 fail_at_op;
	u32 lose_after_op;
	bool lost;
	bool poll_all_ones;
};

static int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got=%ld expected=%ld\n", name, got, expected);
		failures++;
	}
}

static void event_add(struct event *events, u32 *count, struct event event)
{
	if (*count < MAX_EVENTS)
		events[(*count)++] = event;
	else
		failures++;
}

static bool fake_operation_fails(struct fake_radio *f)
{
	f->hardware_ops++;
	if (f->lose_after_op == f->hardware_ops)
		f->lost = true;
	return f->fail_at_op == f->hardware_ops;
}

static int fake_read(void *ctx, u16 reg, u16 *value)
{
	struct fake_radio *f = ctx;
	bool fail;

	event_add(f->events, &f->event_count,
		  (struct event){ .kind = EV_READ, .reg = reg });
	f->read_count++;
	if (reg == 0x0413u)
		f->reads_0413++;
	if (reg == 0x0416u)
		f->reads_0416++;
	fail = fake_operation_fails(f);
	if (fail)
		return -5;
	if (f->lost)
		return -5;
	if (reg == 0x0413u && f->poll_all_ones) {
		*value = 0xffffu;
		return 0;
	}
	if (reg == 0x0413u && f->success_at &&
	    f->reads_0413 >= f->success_at)
		f->regs[reg] = 0x0010u;
	*value = f->regs[reg];
	return 0;
}

static int fake_write(void *ctx, u16 reg, u16 value)
{
	struct fake_radio *f = ctx;
	bool fail;

	event_add(f->events, &f->event_count,
		  (struct event){ .kind = EV_WRITE, .reg = reg, .value = value });
	fail = fake_operation_fails(f);
	if (fail || f->lost)
		return -5;
	f->regs[reg] = value;
	return 0;
}

static void fake_delay(void *ctx, u32 usec)
{
	struct fake_radio *f = ctx;

	event_add(f->events, &f->event_count,
		  (struct event){ .kind = EV_DELAY, .delay = usec });
	f->delay_count++;
	if (fake_operation_fails(f))
		f->lost = true;
}

static bool fake_lost(void *ctx)
{
	return ((struct fake_radio *)ctx)->lost;
}

static void fake_marker(void *ctx, enum ob_radio_postphase1_marker marker,
			u16 sample, u16 low5, u32 attempts,
			u16 last_value, bool success)
{
	struct fake_radio *f = ctx;

	event_add(f->events, &f->event_count, (struct event){
		.kind = EV_MARK, .marker = marker, .sample = sample,
		.low5 = low5, .attempts = attempts,
		.last_value = last_value, .success = success,
	});
}

static const struct ob_radio_postphase1_ops fake_ops = {
	.read16 = fake_read,
	.write16 = fake_write,
	.delay_us = fake_delay,
	.device_lost = fake_lost,
	.marker = fake_marker,
};

static void init_fake(struct fake_radio *f, u16 sample, u32 success_at)
{
	u32 i;

	memset(f, 0, sizeof(*f));
	for (i = 0; i < REG_COUNT; i++)
		f->regs[i] = (u16)(0xa55au ^ (u16)(i * 37u));
	f->regs[0x0413] = 0;
	f->regs[0x0416] = sample;
	f->success_at = success_at;
}

static int run(struct fake_radio *f, bool phase1_success,
	       struct ob_radio_postphase1_result *result)
{
	return ob_radio_postphase1_run(&fake_ops, f, phase1_success, result);
}

static void expect_event(struct event *events, u32 *count,
			struct event event)
{
	event_add(events, count, event);
}

static void expect_read(struct event *events, u32 *count, u16 reg)
{
	expect_event(events, count,
		     (struct event){ .kind = EV_READ, .reg = reg });
}

static void expect_write(struct event *events, u32 *count, u16 reg,
			 u16 value)
{
	expect_event(events, count, (struct event){
		.kind = EV_WRITE, .reg = reg, .value = value,
	});
}

static void expect_delay(struct event *events, u32 *count, u32 usec)
{
	expect_event(events, count, (struct event){
		.kind = EV_DELAY, .delay = usec,
	});
}

static void expect_marker(struct event *events, u32 *count,
			  enum ob_radio_postphase1_marker marker,
			  u16 sample, u16 low5, u32 attempts,
			  u16 last_value, bool success)
{
	expect_event(events, count, (struct event){
		.kind = EV_MARK, .marker = marker, .sample = sample,
		.low5 = low5, .attempts = attempts,
		.last_value = last_value, .success = success,
	});
}

static void expect_rmw(struct event *events, u32 *count, u16 *model,
		       u16 reg, u16 value, u16 mask)
{
	u16 old = model[reg];
	u16 next = (u16)((old & (u16)~value) | (value & mask));

	expect_read(events, count, reg);
	expect_write(events, count, reg, next);
	model[reg] = next;
}

static void build_expected_trace(struct event *events, u32 *count,
				 u16 *model, u16 sample, u16 low5,
				 u32 poll_success_at)
{
	u32 i;
	const u16 phase1_chain_regs[] = { 0x0126, 0x0043, 0x0326, 0x0243 };
	const u16 phase2_chain_clear[] = { 0x011d, 0x031d };
	const u16 phase2_chain_set[] = { 0x0171, 0x0371 };

	expect_marker(events, count, OB_RADIO_POSTPHASE1_MARK_CP0_ENTRY,
		      0, 0, 0, 0, false);
	expect_rmw(events, count, model, 0x0411, 0x0001, 0x0000);
	expect_read(events, count, 0x0416);
	expect_marker(events, count, OB_RADIO_POSTPHASE1_MARK_CP1_0416,
		      sample, low5, 0, 0, false);
	for (i = 0; i < 4; i++)
		expect_rmw(events, count, model, phase1_chain_regs[i],
			   0x001f, low5);
	expect_marker(events, count,
		      OB_RADIO_POSTPHASE1_MARK_CP2_PHASE1_CHAIN_APPLY_DONE,
		      sample, low5, 0, 0, false);
	expect_rmw(events, count, model, 0x0410, 0x0001, 0x0000);
	expect_rmw(events, count, model, 0x0410, 0x1000, 0x0000);
	expect_rmw(events, count, model, 0x0410, 0x0018, 0x0008);
	expect_rmw(events, count, model, 0x0411, 0xff00, 0x4000);
	expect_write(events, count, 0x0412, 0x011a);
	model[0x0412] = 0x011a;
	expect_marker(events, count, OB_RADIO_POSTPHASE1_MARK_CP3_PHASE2_SETUP_DONE,
		      sample, low5, 0, 0, false);
	for (i = 0; i < 2; i++) {
		expect_rmw(events, count, model, phase2_chain_clear[i],
			   0x0004, 0x0000);
		expect_rmw(events, count, model, phase2_chain_set[i],
			   0x2000, 0x2000);
	}
	expect_marker(events, count,
		      OB_RADIO_POSTPHASE1_MARK_CP4_PHASE2_CHAIN_SETUP_DONE,
		      sample, low5, 0, 0, false);
	expect_rmw(events, count, model, 0x0410, 0x0001, 0x0000);
	expect_delay(events, count, 1);
	expect_rmw(events, count, model, 0x0410, 0x0001, 0x0001);
	expect_delay(events, count, 35);
	expect_rmw(events, count, model, 0x0411, 0x0001, 0x0001);
	expect_marker(events, count, OB_RADIO_POSTPHASE1_MARK_CP5_PRE_POLL,
		      sample, low5, 0, 0, false);
	for (i = 0; i < poll_success_at; i++) {
		expect_delay(events, count, 100);
		expect_read(events, count, 0x0413);
	}
	expect_marker(events, count, OB_RADIO_POSTPHASE1_MARK_POLL,
		      sample, low5, poll_success_at, 0x0010, true);
}

static bool event_equal(const struct event *a, const struct event *b)
{
	return a->kind == b->kind && a->reg == b->reg &&
	       a->value == b->value && a->delay == b->delay &&
	       a->marker == b->marker && a->sample == b->sample &&
	       a->low5 == b->low5 && a->attempts == b->attempts &&
	       a->last_value == b->last_value && a->success == b->success;
}

static void check_poll_delay_order(const struct fake_radio *f)
{
	u32 i, reads = 0;

	for (i = 0; i < f->event_count; i++) {
		if (f->events[i].kind != EV_READ ||
		    f->events[i].reg != 0x0413u)
			continue;
		check("100us delay precedes poll read", i > 0 &&
		      f->events[i - 1].kind == EV_DELAY &&
		      f->events[i - 1].delay == 100, true);
		reads++;
	}
	check("poll read count matches attempts", reads, f->reads_0413);
}

static void check_exact_success(u16 sample, u32 success_at)
{
	struct fake_radio f;
	struct ob_radio_postphase1_result result = { 0 };
	struct event expected[MAX_EVENTS] = { 0 };
	u16 model[REG_COUNT];
	u16 low5 = (u16)(sample & 0x001fu);
	u32 expected_count = 0, i;
	int ret;

	init_fake(&f, sample, success_at);
	memcpy(model, f.regs, sizeof(model));
	build_expected_trace(expected, &expected_count, model, sample, low5,
			     success_at);
	ret = run(&f, true, &result);
	check("success return", ret, 0);
	check("success flag", result.success, true);
	check("attempt count", result.attempts, success_at);
	check("last poll value", result.last_value, 0x0010);
	check("single 0416 read", f.reads_0416, 1);
	check("bounded poll reads", f.reads_0413, success_at);
	check_poll_delay_order(&f);
	check("exact full event count", f.event_count, expected_count);
	for (i = 0; i < f.event_count && i < expected_count; i++)
		if (!event_equal(&f.events[i], &expected[i])) {
			printf("FAIL: event %u differs (kind %u/%u reg 0x%04x/0x%04x)\n",
			       i, f.events[i].kind, expected[i].kind,
			       f.events[i].reg, expected[i].reg);
			failures++;
			break;
		}
	/* Poll marker is terminal: no cleanup, 0x0416, or later phase follows. */
	check("terminal event is poll marker",
	      f.events[f.event_count - 1].kind == EV_MARK &&
	      f.events[f.event_count - 1].marker ==
		OB_RADIO_POSTPHASE1_MARK_POLL, true);
}

static void test_dynamic_samples_and_poll_boundaries(void)
{
	check_exact_success(0x0000, 1);
	check_exact_success(0x0001, 7);
	check_exact_success(0x001f, 100);
	check_exact_success(0xa5c3, 7); /* upper bits ignored; low5 is 3 */
}

static void test_gate_and_timeout(void)
{
	struct fake_radio f;
	struct ob_radio_postphase1_result result = { 0 };

	init_fake(&f, 0x0007, 1);
	check("phase1 gate rejected", run(&f, false, &result), -22);
	check("phase1 gate no hardware ops", f.hardware_ops, 0);
	check("phase1 gate no reads", f.read_count, 0);

	init_fake(&f, 0x001f, 0);
	check("timeout return", run(&f, true, &result), -110);
	check("timeout attempts", result.attempts, 100);
	check("timeout reads", f.reads_0413, 100);
	check("timeout total delays", f.delay_count, 102);
	check_poll_delay_order(&f);
	check("timeout success false", result.success, false);
	check("timeout stage", result.failure_stage,
	      OB_RADIO_POSTPHASE1_STAGE_POLL_TIMEOUT);
	check("timeout has no later access", f.event_count > 0 &&
	      f.events[f.event_count - 1].kind == EV_MARK &&
	      f.events[f.event_count - 1].marker == OB_RADIO_POSTPHASE1_MARK_POLL,
	      true);
}

static void test_failure_at_each_hardware_operation(void)
{
	struct fake_radio baseline;
	struct ob_radio_postphase1_result result;
	u32 op, total;

	init_fake(&baseline, 0x0012, 100);
	check("baseline for fault injection", run(&baseline, true, &result), 0);
	total = baseline.hardware_ops;
	for (op = 1; op <= total; op++) {
		struct fake_radio f;
		int ret;

		init_fake(&f, 0x0012, 100);
		f.fail_at_op = op;
		ret = run(&f, true, &result);
		if (!ret) {
			printf("FAIL: injected hardware-op error %u succeeded\n", op);
			failures++;
		}
		check("failure stops at injected operation", f.hardware_ops, op);
		check("failure no 0416 duplication", f.reads_0416 <= 1, true);
		if (f.event_count && f.events[f.event_count - 1].kind == EV_MARK) {
			printf("FAIL: marker emitted after failed hardware operation %u\n", op);
			failures++;
		}
	}
	for (op = 1; op <= total; op++) {
		struct fake_radio f;
		int ret;

		init_fake(&f, 0x0012, 100);
		f.lose_after_op = op;
		ret = run(&f, true, &result);
		if (!ret) {
			printf("FAIL: injected device loss after operation %u succeeded\n",
			       op);
			failures++;
		}
		check("device loss stops at injected operation", f.hardware_ops, op);
		check("device loss prevents duplicate 0416", f.reads_0416 <= 1, true);
	}
}

static void test_all_ones_and_loss_stop(void)
{
	struct fake_radio f;
	struct ob_radio_postphase1_result result;

	init_fake(&f, 0xffff, 1);
	check("all-ones 0416 fails", run(&f, true, &result), -5);
	check("all-ones 0416 read once", f.reads_0416, 1);
	check("all-ones prevents chain writes", f.regs[0x0126],
	      (u16)(0xa55au ^ (u16)(0x0126u * 37u)));

	init_fake(&f, 0x0003, 0);
	f.poll_all_ones = true;
	check("all-ones third poll fails", run(&f, true, &result), -5);
	check("all-ones third poll stops at first read", f.reads_0413, 1);
	check("all-ones third poll attempts", result.attempts, 1);
	check("all-ones third poll stage", result.failure_stage,
	      OB_RADIO_POSTPHASE1_STAGE_POLL_READ);
	check("all-ones third poll has no later MMIO",
	      f.events[f.event_count - 1].kind == EV_READ &&
	      f.events[f.event_count - 1].reg == 0x0413u, true);

	init_fake(&f, 0x0003, 1);
	f.lose_after_op = 1; /* after the first cleanup read */
	check("loss after cleanup read fails", run(&f, true, &result), -5);
	check("loss stops before 0416", f.reads_0416, 0);
	check("loss stops at first operation", f.hardware_ops, 1);
}

int main(void)
{
	test_dynamic_samples_and_poll_boundaries();
	test_gate_and_timeout();
	test_failure_at_each_hardware_operation();
	test_all_ones_and_loss_stop();
	if (failures) {
		printf("ob_radio_postphase1_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("ob_radio_postphase1_test: PASS");
	return 0;
}

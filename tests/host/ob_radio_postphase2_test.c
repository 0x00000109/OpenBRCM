// SPDX-License-Identifier: GPL-2.0-only
/* Host-only exact-trace, dynamic-sample, and fail-stop tests. */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_radio_postphase2.h"

#define REG_COUNT 0x1000u
#define MAX_EVENTS 64u

enum event_kind { EV_READ, EV_WRITE, EV_MARK };
struct event {
	enum event_kind kind;
	u16 reg;
	u16 value;
	enum ob_radio_postphase2_marker marker;
	enum ob_radio_postphase2_stage stage;
	u16 sample;
	u8 field;
	int ret;
};

struct fake_radio {
	u16 regs[REG_COUNT];
	struct event events[MAX_EVENTS];
	u32 event_count;
	u32 callbacks;
	u32 reads;
	u32 fail_at;
	u32 lose_after;
	u32 loss_queries;
	u32 lose_on_query;
	u32 ones_at_read;
	bool lost;
};

static int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got=%ld expected=%ld\n", name, got, expected);
		failures++;
	}
}

static void add_event(struct fake_radio *f, struct event event)
{
	if (f->event_count < MAX_EVENTS)
		f->events[f->event_count++] = event;
	else
		failures++;
}

static bool callback_result(struct fake_radio *f)
{
	f->callbacks++;
	if (f->lose_after == f->callbacks)
		f->lost = true;
	return f->fail_at == f->callbacks;
}

static int fake_read16(void *ctx, u16 reg, u16 *value)
{
	struct fake_radio *f = ctx;
	bool fail;

	f->reads++;
	add_event(f, (struct event){ .kind = EV_READ, .reg = reg });
	fail = callback_result(f);
	if (fail || f->lost)
		return -5;
	*value = f->reads == f->ones_at_read ? 0xffffu : f->regs[reg];
	return 0;
}

static int fake_write16(void *ctx, u16 reg, u16 value)
{
	struct fake_radio *f = ctx;
	bool fail;

	add_event(f, (struct event){ .kind = EV_WRITE, .reg = reg,
				    .value = value });
	fail = callback_result(f);
	if (fail || f->lost)
		return -5;
	f->regs[reg] = value;
	return 0;
}

static bool fake_device_lost(void *ctx)
{
	struct fake_radio *f = ctx;

	f->loss_queries++;
	if (f->lose_on_query == f->loss_queries)
		f->lost = true;
	return f->lost;
}

static void fake_marker(void *ctx,
			enum ob_radio_postphase2_marker marker,
			u16 sample, u8 field,
			enum ob_radio_postphase2_stage stage, int ret)
{
	struct fake_radio *f = ctx;

	add_event(f, (struct event){ .kind = EV_MARK, .marker = marker,
				    .sample = sample, .field = field,
				    .stage = stage, .ret = ret });
}

static u32 marker_count(const struct fake_radio *f,
		       enum ob_radio_postphase2_marker marker)
{
	u32 i, count = 0;

	for (i = 0; i < f->event_count; i++)
		if (f->events[i].kind == EV_MARK && f->events[i].marker == marker)
			count++;
	return count;
}

static const struct ob_radio_postphase2_ops fake_ops = {
	.read16 = fake_read16,
	.write16 = fake_write16,
	.device_lost = fake_device_lost,
	.marker = fake_marker,
};

static void init_fake(struct fake_radio *f, u16 sample)
{
	memset(f, 0, sizeof(*f));
	f->regs[0x0411] = 0xa5a5;
	f->regs[0x0416] = sample;
	f->regs[0x0171] = 0xafff;
	f->regs[0x0371] = 0x6000;
	f->regs[0x0410] = 0x0003;
	f->regs[0x08ea] = 0x0180;
}

static void expect_read(struct event *e, u32 *n, u16 reg)
{
	e[(*n)++] = (struct event){ .kind = EV_READ, .reg = reg };
}

static void expect_write(struct event *e, u32 *n, u16 reg, u16 value)
{
	e[(*n)++] = (struct event){ .kind = EV_WRITE, .reg = reg,
				    .value = value };
}

static void expect_marker(struct event *e, u32 *n,
			  enum ob_radio_postphase2_marker marker,
			  u16 sample, u8 field,
			  enum ob_radio_postphase2_stage stage, int ret)
{
	e[(*n)++] = (struct event){ .kind = EV_MARK, .marker = marker,
				    .sample = sample, .field = field,
				    .stage = stage, .ret = ret };
}

static void build_expected(struct event *e, u32 *n, u16 sample)
{
	u8 field = (u8)((sample & 0x03e0u) >> 5);

	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_CP0_ENTRY, 0, 0,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_read(e, n, 0x0411);
	expect_write(e, n, 0x0411, 0xa5a4);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_CP1_0411_CLEANUP_DONE,
		      0, 0, OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_read(e, n, 0x0416);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_CP2_0416, sample, field,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_read(e, n, 0x0171);
	expect_write(e, n, 0x0171, 0x8fff);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_CP3_CHAIN0_DONE, 0, 0,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_read(e, n, 0x0371);
	expect_write(e, n, 0x0371, 0x4000);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_CP4_CHAIN1_DONE, 0, 0,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_read(e, n, 0x0410);
	expect_write(e, n, 0x0410, 0x0002);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_CP5_PHASE_END_DONE, 0, 0,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_read(e, n, 0x08ea);
	expect_write(e, n, 0x08ea, 0x0100);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_RETURN, 0, 0,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_PASS, 0, 0,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	expect_marker(e, n, OB_RADIO_POSTPHASE2_MARK_STOP, 0, 0,
		      OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
}

static void compare_events(const char *label, const struct event *got,
			   u32 got_count, const struct event *want,
			   u32 want_count)
{
	u32 i;
	check(label, got_count, want_count);
	if (got_count != want_count)
		return;
	for (i = 0; i < want_count; i++) {
		if (memcmp(&got[i], &want[i], sizeof(want[i]))) {
			printf("FAIL: %s event[%u] differs\n", label, i);
			failures++;
			return;
		}
	}
}

static void test_success_trace(void)
{
	struct fake_radio f;
	struct ob_radio_postphase2_result result;
	struct event expected[MAX_EVENTS] = { 0 };
	u32 expected_count = 0;
	int ret;

	init_fake(&f, 0x02a5);
	ret = ob_radio_postphase2_run(&fake_ops, &f, true, &result);
	build_expected(expected, &expected_count, 0x02a5);
	check("success ret", ret, 0);
	check("success result", result.success, true);
	check("success fresh sample", result.sample_0416, 0x02a5);
	check("success field", result.sample_field, 0x15);
	check("logical operations", result.logical_ops, 6);
	check("callback MMIO events", f.callbacks, 11);
	check("0416 exactly once", f.regs[0x0416] == 0x02a5, true);
	compare_events("exact suffix trace", f.events, f.event_count,
		       expected, expected_count);
}

static void test_dynamic_samples(void)
{
	static const struct { u16 sample; u8 field; } cases[] = {
		{ 0x0000, 0x00 }, { 0x0020, 0x01 },
		{ 0x03e0, 0x1f }, { 0x02a5, 0x15 },
		{ 0xe2a5, 0x15 }, { 0xffff, 0x1f },
	};
	struct fake_radio baseline;
	struct ob_radio_postphase2_result result;
	struct event baseline_events[MAX_EVENTS];
	u32 i;

	check("pure field 0000", ob_radio_postphase2_field(0x0000), 0x00);
	check("pure field 0020", ob_radio_postphase2_field(0x0020), 0x01);
	check("pure field 03e0", ob_radio_postphase2_field(0x03e0), 0x1f);
	check("pure field 02a5", ob_radio_postphase2_field(0x02a5), 0x15);
	check("pure field e2a5", ob_radio_postphase2_field(0xe2a5), 0x15);
	check("pure field ffff", ob_radio_postphase2_field(0xffff), 0x1f);

	init_fake(&baseline, cases[0].sample);
	check("baseline run", ob_radio_postphase2_run(&fake_ops, &baseline,
			true, &result), 0);
	memcpy(baseline_events, baseline.events,
	       baseline.event_count * sizeof(baseline.events[0]));
	for (i = 1; i < sizeof(cases) / sizeof(cases[0]) - 1; i++) {
		struct fake_radio f;
		u32 j;

		init_fake(&f, cases[i].sample);
		check("dynamic run", ob_radio_postphase2_run(&fake_ops, &f,
				true, &result), 0);
		check("dynamic extracted field", result.sample_field,
		      cases[i].field);
		check("dynamic op count", f.event_count, baseline.event_count);
		for (j = 0; j < f.event_count; j++) {
			struct event want = baseline_events[j];

			if (want.kind == EV_MARK &&
			    want.marker == OB_RADIO_POSTPHASE2_MARK_CP2_0416) {
				want.sample = cases[i].sample;
				want.field = cases[i].field;
			}
			if (memcmp(&f.events[j], &want, sizeof(want))) {
				printf("FAIL: sample changed non-local event at %u\n", j);
				failures++;
				break;
			}
		}
	}

	/* All-ones is valid only for the pure extraction helper, not live I/O. */
	{
		struct fake_radio f;
		u32 j;

		init_fake(&f, 0xffff);
		check("live all-ones rejected", ob_radio_postphase2_run(
				&fake_ops, &f, true, &result), -5);
		check("all-ones read fails at sample stage",
		      result.failure_stage,
		      OB_RADIO_POSTPHASE2_STAGE_READ_0416);
		check("all-ones not accepted", result.success, false);
		check("all-ones emits one FAIL", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_FAIL), 1);
		check("all-ones emits one STOP", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_STOP), 1);
		check("all-ones emits no CP2", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_CP2_0416), 0);
		check("all-ones emits no RETURN", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_RETURN), 0);
		check("all-ones emits no PASS", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_PASS), 0);
		for (j = 0; j < f.event_count; j++)
			if (f.events[j].kind == EV_READ && f.events[j].reg == 0x0416u)
				break;
		check("all-ones fresh sample read occurred", j < f.event_count, true);
		for (j++; j < f.event_count; j++)
			check("all-ones no later MMIO",
			      f.events[j].kind == EV_READ ||
			      f.events[j].kind == EV_WRITE, false);
	}
}

static void test_callback_failure_injection(void)
{
	u32 fail_at;

	for (fail_at = 1; fail_at <= 11; fail_at++) {
		struct fake_radio f;
		struct ob_radio_postphase2_result result;
		int ret;

		init_fake(&f, 0x0240);
		f.fail_at = fail_at;
		ret = ob_radio_postphase2_run(&fake_ops, &f, true, &result);
		check("injected callback failure", ret, -5);
		check("failure stage present", result.failure_stage !=
		      OB_RADIO_POSTPHASE2_STAGE_NONE, true);
		check("failure stops at callback", f.callbacks, fail_at);
		check("failure includes no return marker",
		      f.events[f.event_count - 1].marker ==
		      OB_RADIO_POSTPHASE2_MARK_RETURN, false);
		check("failure result not success", result.success, false);
		check("callback failure emits one FAIL", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_FAIL), 1);
		check("callback failure emits one STOP", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_STOP), 1);
		check("callback failure emits no PASS", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_PASS), 0);
		check("callback failure emits no RETURN", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_RETURN), 0);
		check("callback failure ends FAIL then STOP",
		      f.events[f.event_count - 2].marker ==
		      OB_RADIO_POSTPHASE2_MARK_FAIL &&
		      f.events[f.event_count - 1].marker ==
		      OB_RADIO_POSTPHASE2_MARK_STOP, true);
		{
			u32 i;

			for (i = 0; i < f.event_count; i++)
				if (f.events[i].kind == EV_MARK)
					check("failure emits no return marker",
					      f.events[i].marker ==
					      OB_RADIO_POSTPHASE2_MARK_RETURN, false);
		}
	}
}

static void test_all_ones_at_each_read(void)
{
	static const u32 callback_at_read[] = { 1, 3, 4, 6, 8, 10 };
	u32 read;

	for (read = 1; read <= 6; read++) {
		struct fake_radio f;
		struct ob_radio_postphase2_result result;
		u32 last_mmio = 0, j;
		int ret;

		init_fake(&f, 0x0040);
		f.ones_at_read = read;
		ret = ob_radio_postphase2_run(&fake_ops, &f, true, &result);
		check("all-ones read fails closed", ret, -5);
		check("all-ones stops on that read callback", f.callbacks,
		      callback_at_read[read - 1]);
		check("all-ones does not mark success", result.success, false);
		for (j = 0; j < f.event_count; j++)
			if (f.events[j].kind == EV_READ || f.events[j].kind == EV_WRITE)
				last_mmio = j;
		check("all-ones last MMIO is read", f.events[last_mmio].kind, EV_READ);
		check("all-ones failure does not reach RETURN",
		      result.failure_stage == OB_RADIO_POSTPHASE2_STAGE_NONE, false);
	}
}

static void test_device_loss_at_each_guard_boundary(void)
{
	u32 callback;

	/* Each callback has a preceding guard query at odd query numbers. */
	for (callback = 1; callback <= 11; callback++) {
		struct fake_radio f;
		struct ob_radio_postphase2_result result;
		u32 expected_callbacks = callback - 1;
		int ret;

		init_fake(&f, 0x0240);
		f.lose_on_query = callback * 2;
		ret = ob_radio_postphase2_run(&fake_ops, &f, true, &result);
		check("pre-callback device loss ret", ret, -5);
		check("pre-callback device loss blocks access",
		      f.callbacks, expected_callbacks);
		check("pre-callback loss stage", result.failure_stage,
		      OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST);
		check("pre-callback loss cannot succeed", result.success, false);
		check("pre-callback loss emits one FAIL", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_FAIL), 1);
		check("pre-callback loss emits one STOP", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_STOP), 1);
	}

	/* Each callback can latch loss immediately after the transport returns. */
	for (callback = 1; callback <= 11; callback++) {
		struct fake_radio f;
		struct ob_radio_postphase2_result result;
		int ret;

		init_fake(&f, 0x0240);
		f.lose_after = callback;
		ret = ob_radio_postphase2_run(&fake_ops, &f, true, &result);
		check("post-callback device loss ret", ret, -5);
		check("post-callback device loss stops immediately",
		      f.callbacks, callback);
		check("post-callback loss stage", result.failure_stage,
		      OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST);
		check("post-callback loss cannot succeed", result.success, false);
		check("post-callback loss emits one FAIL", marker_count(&f,
		      OB_RADIO_POSTPHASE2_MARK_FAIL), 1);
		check("post-callback loss emits one STOP", marker_count(&f,
	      OB_RADIO_POSTPHASE2_MARK_STOP), 1);
	}
}

static void test_entry_gate_and_loss_latch(void)
{
	struct fake_radio f;
	struct ob_radio_postphase2_result result;

	init_fake(&f, 0x0000);
	check("third-poll gate rejects", ob_radio_postphase2_run(
			&fake_ops, &f, false, &result), -22);
	check("false gate has no event", f.event_count, 0);
	check("false gate has no MMIO callback", f.callbacks, 0);
	check("false gate no entry marker", result.success, false);

	init_fake(&f, 0x0000);
	f.lost = true;
	check("pre-access loss rejected", ob_radio_postphase2_run(
			&fake_ops, &f, true, &result), -5);
	check("pre-access loss has no callback", f.callbacks, 0);
	check("pre-access loss omits entry marker", marker_count(&f,
	      OB_RADIO_POSTPHASE2_MARK_CP0_ENTRY), 0);
	check("pre-access loss emits only terminal pair", f.event_count, 2);
	check("pre-access loss fails closed", result.success, false);
	check("pre-access loss ends FAIL then STOP",
	      f.events[0].marker == OB_RADIO_POSTPHASE2_MARK_FAIL &&
	      f.events[1].marker == OB_RADIO_POSTPHASE2_MARK_STOP, true);

	init_fake(&f, 0x0000);
	f.lose_after = 1;
	check("post-read loss rejected", ob_radio_postphase2_run(
			&fake_ops, &f, true, &result), -5);
	check("post-read loss no write", f.callbacks, 1);
	check("post-read loss stops with terminal pair", f.event_count, 4);
	check("post-read loss failure stage", result.failure_stage,
	      OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST);
	check("post-read loss ends FAIL then STOP",
	      f.events[f.event_count - 2].marker ==
	      OB_RADIO_POSTPHASE2_MARK_FAIL &&
	      f.events[f.event_count - 1].marker ==
	      OB_RADIO_POSTPHASE2_MARK_STOP, true);
}

int main(void)
{
	test_success_trace();
	if (!failures)
		puts("POSTPHASE2_PURE_SEQUENCE_TEST: PASS");
	test_dynamic_samples();
	if (!failures)
		puts("POSTPHASE2_DYNAMIC_SAMPLE_TEST: PASS");
	test_callback_failure_injection();
	test_all_ones_at_each_read();
	test_device_loss_at_each_guard_boundary();
	if (!failures)
		puts("POSTPHASE2_FAILURE_INJECTION_TEST: PASS");
	test_entry_gate_and_loss_latch();
	if (!failures)
		puts("POSTPHASE2_ENTRY_GATE_TEST: PASS");
	if (failures) {
		printf("ob_radio_postphase2_test: %d failures\n", failures);
		return 1;
	}
	puts("ob_radio_postphase2_test: PASS");
	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/* Host-only operation-order and fail-stop tests for the phase-1 checkpoint. */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_radio_post0413.h"

#define MAX_EVENTS 512u

enum event_kind { EV_READ, EV_WRITE, EV_DELAY, EV_MARK };
struct event {
	enum event_kind kind;
	u16 reg;
	u16 value;
	u32 delay;
	enum ob_radio_post0413_marker marker;
};

struct fake_radio {
	u16 regs[0x1000];
	struct event events[MAX_EVENTS];
	u32 event_count;
	u32 reads_0413;
	u32 success_at;
	u32 read_count;
	u32 write_count;
	u32 delay_count;
	u32 write_0411_count;
	bool lost;
	u16 lose_on_read_reg;
	u16 fail_read_reg;
	int fail_read_rc;
	u16 fail_write_reg;
	int fail_write_rc;
};

static int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got=%ld expected=%ld\n", name, got, expected);
		failures++;
	}
}

static void event_add(struct fake_radio *f, struct event event)
{
	if (f->event_count < MAX_EVENTS)
		f->events[f->event_count++] = event;
	else
		failures++;
}

static int fake_read(void *ctx, u16 reg, u16 *value)
{
	struct fake_radio *f = ctx;

	event_add(f, (struct event){ .kind = EV_READ, .reg = reg });
	f->read_count++;
	if (reg == 0x0413u) {
		f->reads_0413++;
		if (f->success_at && f->reads_0413 >= f->success_at)
			f->regs[reg] = 0x0010u;
	}
	if (f->lost)
		return -5;
	if (reg == f->fail_read_reg && f->fail_read_rc)
		return f->fail_read_rc;
	*value = f->regs[reg];
	if (reg == f->lose_on_read_reg)
		f->lost = true;
	return 0;
}

static int fake_write(void *ctx, u16 reg, u16 value)
{
	struct fake_radio *f = ctx;

	event_add(f, (struct event){
		.kind = EV_WRITE, .reg = reg, .value = value,
	});
	f->write_count++;
	if (reg == 0x0411u)
		f->write_0411_count++;
	if (f->lost)
		return -5;
	if (reg == f->fail_write_reg && f->fail_write_rc)
		return f->fail_write_rc;
	f->regs[reg] = value;
	return 0;
}

static void fake_delay(void *ctx, u32 usec)
{
	struct fake_radio *f = ctx;

	event_add(f, (struct event){ .kind = EV_DELAY, .delay = usec });
	f->delay_count++;
}

static bool fake_lost(void *ctx)
{
	return ((struct fake_radio *)ctx)->lost;
}

static void fake_marker(void *ctx, enum ob_radio_post0413_marker marker,
			u16 r414, u16 r415, u8 derived)
{
	struct fake_radio *f = ctx;

	event_add(f, (struct event){
		.kind = EV_MARK, .marker = marker,
		.reg = r414, .value = r415, .delay = derived,
	});
}

static const struct ob_radio_post0413_ops fake_ops = {
	.read16 = fake_read,
	.write16 = fake_write,
	.delay_us = fake_delay,
	.device_lost = fake_lost,
	.marker = fake_marker,
};

static int run(struct fake_radio *f, bool phase0_ok,
	       struct ob_radio_post0413_result *result)
{
	return ob_radio_post0413_run(&fake_ops, f, phase0_ok, result);
}

static u32 event_index(const struct fake_radio *f, enum event_kind kind,
		       u16 reg, u32 start)
{
	u32 i;

	for (i = start; i < f->event_count; i++)
		if (f->events[i].kind == kind && f->events[i].reg == reg)
			return i;
	return MAX_EVENTS;
}

static void check_poll_delays_before_reads(const struct fake_radio *f,
					  u32 expected_reads)
{
	u32 i, poll_reads = 0;

	for (i = 0; i < f->event_count; i++) {
		if (f->events[i].kind == EV_READ && f->events[i].reg == 0x0413u) {
			check("poll delay immediately precedes read", i > 0 &&
			      f->events[i - 1].kind == EV_DELAY &&
			      f->events[i - 1].delay == 100u, true);
			poll_reads++;
		}
	}
	check("poll read count", poll_reads, expected_reads);
}

static u32 marker_index(const struct fake_radio *f,
			enum ob_radio_post0413_marker marker)
{
	u32 i;

	for (i = 0; i < f->event_count; i++)
		if (f->events[i].kind == EV_MARK &&
		    f->events[i].marker == marker)
			return i;
	return MAX_EVENTS;
}

static void init_fake(struct fake_radio *f)
{
	memset(f, 0, sizeof(*f));
	f->regs[0x0411] = 0xabcd;
	f->regs[0x0410] = 0x1fff;
	f->regs[0x0414] = 0xfff0;
	f->regs[0x0415] = 0x0010;
	f->success_at = 1;
}

static void test_exact_sequence_and_derived_byte(void)
{
	static const struct event expected_trace[] = {
		{ .kind = EV_MARK, .marker = OB_RADIO_POST0413_MARK_CP0_ENTRY },
		{ .kind = EV_READ, .reg = 0x0411 },
		{ .kind = EV_WRITE, .reg = 0x0411, .value = 0xabcc },
		{ .kind = EV_READ, .reg = 0x0414 },
		{ .kind = EV_READ, .reg = 0x0415 },
		{ .kind = EV_MARK, .marker = OB_RADIO_POST0413_MARK_CP1_SAMPLES,
		  .reg = 0xfff0, .value = 0x0010 },
		{ .kind = EV_READ, .reg = 0x0410 },
		{ .kind = EV_WRITE, .reg = 0x0410, .value = 0x1ffe },
		{ .kind = EV_READ, .reg = 0x0410 },
		{ .kind = EV_WRITE, .reg = 0x0410, .value = 0x0ffe },
		{ .kind = EV_READ, .reg = 0x0410 },
		{ .kind = EV_WRITE, .reg = 0x0410, .value = 0x0ff6 },
		{ .kind = EV_READ, .reg = 0x0411 },
		{ .kind = EV_WRITE, .reg = 0x0411, .value = 0x70cc },
		{ .kind = EV_WRITE, .reg = 0x0412, .value = 0x0101 },
		{ .kind = EV_MARK, .marker = OB_RADIO_POST0413_MARK_CP2_SETUP_DONE },
		{ .kind = EV_READ, .reg = 0x0410 },
		{ .kind = EV_WRITE, .reg = 0x0410, .value = 0x0ff6 },
		{ .kind = EV_DELAY, .delay = 1 },
		{ .kind = EV_READ, .reg = 0x0410 },
		{ .kind = EV_WRITE, .reg = 0x0410, .value = 0x0ff7 },
		{ .kind = EV_DELAY, .delay = 35 },
		{ .kind = EV_READ, .reg = 0x0411 },
		{ .kind = EV_WRITE, .reg = 0x0411, .value = 0x70cd },
		{ .kind = EV_MARK, .marker = OB_RADIO_POST0413_MARK_CP3_PRE_POLL },
		{ .kind = EV_DELAY, .delay = 100 },
		{ .kind = EV_READ, .reg = 0x0413 },
	};
	struct fake_radio f;
	struct ob_radio_post0413_result result = { 0 };
	u32 i, poll_index;
	u32 exact = ((u32)0x0010u - (u32)0xfff0u) * 0x000000c1u;
	u8 expected_derived = (u8)(exact >> 8);
	const u16 expected_writes[] = {
		0xabcc, /* 0411 post-phase0 bit-0 cleanup */
		0x1ffe, /* common phase advance */
		0x0ffe, /* phase-1 0410 clear bit 12 */
		0x0ff6, /* phase-1 0410 composite value/mask */
		0x70cc, /* phase-1 0411 setup */
		0x0101, /* direct 0412 write */
		0x0ff6, /* trigger 0410 clear */
		0x0ff7, /* trigger 0410 set */
		0x70cd, /* trigger 0411 set */
	};
	u32 wi = 0;

	init_fake(&f);
	check("sequence return", run(&f, true, &result), 0);
	check("exact operation/marker event count", f.event_count,
	      sizeof(expected_trace) / sizeof(expected_trace[0]));
	for (i = 0; i < f.event_count &&
	     i < sizeof(expected_trace) / sizeof(expected_trace[0]); i++) {
		check("trace event kind", f.events[i].kind,
		      expected_trace[i].kind);
		if (f.events[i].kind == EV_READ || f.events[i].kind == EV_WRITE)
			check("trace register", f.events[i].reg,
			      expected_trace[i].reg);
		if (f.events[i].kind == EV_WRITE)
			check("trace value", f.events[i].value,
			      expected_trace[i].value);
		if (f.events[i].kind == EV_DELAY)
			check("trace delay", f.events[i].delay,
			      expected_trace[i].delay);
		if (f.events[i].kind == EV_MARK)
			check("trace marker", f.events[i].marker,
			      expected_trace[i].marker);
	}
	check("CP1 derived marker truncation",
	      f.events[5].delay, expected_derived);
	check("pass flag", result.success, true);
	check("one poll attempt", result.attempts, 1);
	check("dynamic 0414", result.r414, 0xfff0);
	check("dynamic 0415", result.r415, 0x0010);
	check("u32 then byte truncation", result.derived, expected_derived);
	check("r414 then r415 order",
	      event_index(&f, EV_READ, 0x0414, 0) <
	      event_index(&f, EV_READ, 0x0415, 0), true);
	check("first marker CP0", f.events[0].kind, EV_MARK);
	check("first marker is CP0", f.events[0].marker,
	      OB_RADIO_POST0413_MARK_CP0_ENTRY);
	check("first operation read is 0411 cleanup",
	      f.events[1].kind, EV_READ);
	check("first operation register", f.events[1].reg, 0x0411);
	poll_index = event_index(&f, EV_READ, 0x0413, 0);
	check("CP1 after 0415 sample",
	      marker_index(&f, OB_RADIO_POST0413_MARK_CP1_SAMPLES) >
	      event_index(&f, EV_READ, 0x0415, 0), true);
	check("CP2 after 0412 write",
	      marker_index(&f, OB_RADIO_POST0413_MARK_CP2_SETUP_DONE) >
	      event_index(&f, EV_WRITE, 0x0412, 0), true);
	check("CP3 before poll",
	      marker_index(&f, OB_RADIO_POST0413_MARK_CP3_PRE_POLL) < poll_index,
	      true);
	check("no 0416 read", event_index(&f, EV_READ, 0x0416, 0), MAX_EVENTS);
	check("three 0411 writes only", f.write_0411_count, 3);
	check("exact total writes", f.write_count,
	      sizeof(expected_writes) / sizeof(expected_writes[0]));
	for (i = 0; i < f.event_count; i++) {
		if (f.events[i].kind != EV_WRITE)
			continue;
		if (wi < sizeof(expected_writes) / sizeof(expected_writes[0]))
			check("write order/value", f.events[i].value,
			      expected_writes[wi]);
		wi++;
	}
	check("no chain loop reg 0x11d", event_index(&f, EV_READ,
	      0x011d, 0), MAX_EVENTS);
	check("first success stops after poll read",
	      f.events[f.event_count - 1].reg, 0x0413);
	check_poll_delays_before_reads(&f, 1);
}

static void test_samples_are_observations_only(void)
{
	struct fake_radio a, b;
	struct ob_radio_post0413_result ra = { 0 }, rb = { 0 };
	u32 i, wa = 0;

	init_fake(&a);
	init_fake(&b);
	b.regs[0x0414] = 0x0012;
	b.regs[0x0415] = 0xeeee;
	check("sample A run", run(&a, true, &ra), 0);
	check("sample B run", run(&b, true, &rb), 0);
	check("different local derived byte", ra.derived != rb.derived, true);
	for (i = 0; i < a.event_count; i++) {
		if (a.events[i].kind == EV_WRITE) {
			while (wa < b.event_count && b.events[wa].kind != EV_WRITE)
				wa++;
			check("derived is not used for hardware mutation",
			      b.events[wa].value, a.events[i].value);
			wa++;
		}
	}
}

static void test_poll_boundaries(void)
{
	struct fake_radio f;
	struct ob_radio_post0413_result result = { 0 };

	init_fake(&f);
	f.success_at = 4;
	check("multi-attempt success", run(&f, true, &result), 0);
	check("multi-attempt count", result.attempts, 4);
	check_poll_delays_before_reads(&f, 4);

	init_fake(&f);
	f.success_at = 100;
	check("attempt-100 success", run(&f, true, &result), 0);
	check("attempt-100 count", result.attempts, 100);
	check_poll_delays_before_reads(&f, 100);

	init_fake(&f);
	f.success_at = 0;
	check("100-read timeout", run(&f, true, &result), -110);
	check("timeout stage", result.failure_stage,
	      OB_RADIO_POST0413_STAGE_POLL_TIMEOUT);
	check("timeout attempts", result.attempts, 100);
	check("timeout has no 101st read", f.reads_0413, 100);
	check_poll_delays_before_reads(&f, 100);
}

static void test_fail_closed_and_stop(void)
{
	struct fake_radio f;
	struct ob_radio_post0413_result result = { 0 };

	init_fake(&f);
	check("phase0 failure blocks continuation", run(&f, false, &result),
	      -22);
	check("no phase0-failed hardware access", f.event_count, 0);

	init_fake(&f);
	f.fail_read_reg = 0x0414;
	f.fail_read_rc = -5;
	check("0414 access failure", run(&f, true, &result), -5);
	check("0414 fail stage", result.failure_stage,
	      OB_RADIO_POST0413_STAGE_READ_414);
	check("only cleanup write before 0414 error", f.write_count, 1);
	check("0415 not reached", event_index(&f, EV_READ, 0x0415, 0),
	      MAX_EVENTS);

	init_fake(&f);
	f.lose_on_read_reg = 0x0414;
	check("loss during sample stops", run(&f, true, &result), -5);
	check("loss stage", result.failure_stage,
	      OB_RADIO_POST0413_STAGE_DEVICE_LOST);
	check("loss prevents 0415 and later writes", f.read_count, 2);
	check("loss only permits first cleanup write", f.write_count, 1);

	init_fake(&f);
	f.regs[0x0414] = 0xffff;
	check("all ones rejected", run(&f, true, &result), -5);
	check("only cleanup write before all-ones 0414", f.write_count, 1);
	check("all ones stops before 0415", event_index(&f, EV_READ,
	      0x0415, 0), MAX_EVENTS);

	init_fake(&f);
	f.fail_write_reg = 0x0411;
	f.fail_write_rc = -5;
	check("post-poll cleanup write failure", run(&f, true, &result), -5);
	check("cleanup failure has no later radio read", f.read_count, 1);

	init_fake(&f);
	f.lost = true;
	check("preexisting device loss", run(&f, true, &result), -5);
	check("device loss no MMIO", f.read_count + f.write_count, 0);

	init_fake(&f);
	f.fail_read_reg = 0x0413;
	f.fail_read_rc = -5;
	check("poll read error", run(&f, true, &result), -5);
	check("poll error one read", f.reads_0413, 1);
	check("poll error no 0411 cleanup", f.write_0411_count, 3);
	check("poll error no 0416", event_index(&f, EV_READ, 0x0416, 0),
	      MAX_EVENTS);

	init_fake(&f);
	f.success_at = 0;
	f.fail_read_reg = 0x0413;
	f.fail_read_rc = 0;
	f.regs[0x0413] = 0xffff;
	check("all-ones poll sample fails", run(&f, true, &result), -5);
	check("all-ones poll stage", result.failure_stage,
	      OB_RADIO_POST0413_STAGE_POLL_READ);
	check("all-ones is the only second poll read", f.reads_0413, 1);
}

int main(void)
{
	test_exact_sequence_and_derived_byte();
	test_samples_are_observations_only();
	test_poll_boundaries();
	test_fail_closed_and_stop();
	if (failures) {
		printf("ob_radio_post0413_test: %d failures\n", failures);
		return 1;
	}
	puts("ob_radio_post0413_test: PASS");
	return 0;
}

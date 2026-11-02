/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../src/ob_dpc_sw.h"

static int failures;
static void check(const char *name, int ok)
{
	if (!ok) {
		fprintf(stderr, "FAIL: %s\n", name);
		failures++;
	}
}

static const struct ob_dpc_sw_ops ob_dpc_sw_test_ops;

struct sink {
	struct ob_dpc_sw_state *state;
	struct ob_dpc_sw_event seen[64];
	u32 n;
	u32 stop_after;
	u32 lose_after;
	bool reenter;
	int nested_ret;
	int fail_ret;
};

static int process_event(void *ctx, const struct ob_dpc_sw_event *event)
{
	struct sink *sink = ctx;

	if (sink->n < 64)
		sink->seen[sink->n] = *event;
	sink->n++;
	if (sink->reenter) {
		u32 nested = 99;
		sink->nested_ret = ob_dpc_sw_dispatch(sink->state,
			&ob_dpc_sw_test_ops, sink, 1, &nested);
	}
	if (sink->stop_after && sink->n == sink->stop_after)
		ob_dpc_sw_quiesce(sink->state);
	if (sink->lose_after && sink->n == sink->lose_after)
		ob_dpc_sw_mark_dev_lost(sink->state);
	return sink->fail_ret;
}

static const struct ob_dpc_sw_ops ob_dpc_sw_test_ops = {
	.process = process_event,
};

static void enqueue(struct ob_dpc_sw_state *state, enum ob_dpc_sw_kind kind,
		    ob_dpc_sw_token_t token)
{
	struct ob_dpc_sw_event event = { .kind = kind, .token = token };
	check("enqueue", ob_dpc_sw_enqueue(state, &event) == 0);
}

static void test_duplicate_dispatch_and_bounded_batch(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state };
	u32 processed = 0;

	ob_dpc_sw_init(&state);
	enqueue(&state, OB_DPC_SW_TX_COMPLETE, 10);
	enqueue(&state, OB_DPC_SW_RX_FRAME, 11);
	enqueue(&state, OB_DPC_SW_TX_COMPLETE, 12);
	check("batch dispatch succeeds", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 2, &processed) == 0);
	check("batch is bounded", processed == 2 && sink.n == 2 &&
		state.count == 1);
	check("first event order", sink.seen[0].token == 10 &&
		sink.seen[1].token == 11);
	check("second dispatch consumes remainder", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 2, &processed) == 0 &&
		processed == 1 && sink.n == 3);
	check("duplicate idle dispatch does not replay", ob_dpc_sw_dispatch(
		&state, &ob_dpc_sw_test_ops, &sink, 2, &processed) == 0 &&
		processed == 0 && sink.n == 3);
}

static void test_maximum_batch_is_clamped(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state };
	u32 processed = 0, i;

	ob_dpc_sw_init(&state);
	for (i = 0; i < 20; i++)
		enqueue(&state, OB_DPC_SW_RX_FRAME, 100 + i);
	check("large dispatch budget clamps to maximum",
		ob_dpc_sw_dispatch(&state, &ob_dpc_sw_test_ops, &sink,
			0xffffffffu, &processed) == 0 &&
		processed == OB_DPC_SW_MAX_BATCH &&
		state.count == 20 - OB_DPC_SW_MAX_BATCH);
}

static void test_reentrant_dispatch_is_rejected(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state, .reenter = true };
	u32 processed = 0;

	ob_dpc_sw_init(&state);
	enqueue(&state, OB_DPC_SW_RX_FRAME, 20);
	check("outer dispatch succeeds", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 2, &processed) == 0);
	check("reentrant dispatch rejected", sink.nested_ret == -EBUSY);
	check("event processed exactly once", sink.n == 1 && processed == 1);
}

static void test_quiesce_preserves_reclaimable_pending_events(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state, .stop_after = 1 };
	struct ob_dpc_sw_event cancelled[OB_DPC_SW_CAPACITY];
	u32 processed = 0, cancelled_count = 0;

	ob_dpc_sw_init(&state);
	enqueue(&state, OB_DPC_SW_TX_COMPLETE, 30);
	enqueue(&state, OB_DPC_SW_RX_FRAME, 31);
	enqueue(&state, OB_DPC_SW_RX_FRAME, 32);
	check("dispatch lets callback quiesce", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 3, &processed) == 0);
	check("stop halts batch after current event", processed == 1 &&
		sink.n == 1 && state.count == 2);
	check("no callback after quiesce", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 3, &processed) == -ESHUTDOWN &&
		sink.n == 1);
	check("pending ownership can be reclaimed", ob_dpc_sw_cancel_pending(
		&state, cancelled, OB_DPC_SW_CAPACITY, &cancelled_count) == 0 &&
		cancelled_count == 2 && cancelled[0].token == 31 &&
		cancelled[1].token == 32 && state.count == 0);
}

static void test_device_loss_blocks_queue_and_dispatch(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state };
	struct ob_dpc_sw_event cancelled[OB_DPC_SW_CAPACITY];
	u32 processed = 9, cancelled_count = 0;

	ob_dpc_sw_init(&state);
	enqueue(&state, OB_DPC_SW_TX_COMPLETE, 40);
	check("device loss latches", ob_dpc_sw_mark_dev_lost(&state) == 0);
	check("enqueue after loss rejected", ob_dpc_sw_enqueue(&state,
		&(struct ob_dpc_sw_event){OB_DPC_SW_RX_FRAME, 41}) == -EIO);
	check("dispatch after loss rejected", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 4, &processed) == -EIO &&
		processed == 0 && sink.n == 0);
	check("lost event remains reclaimable", ob_dpc_sw_cancel_pending(
		&state, cancelled, OB_DPC_SW_CAPACITY, &cancelled_count) == 0 &&
		cancelled_count == 1 && cancelled[0].token == 40);
}

static void test_ring_wraparound_preserves_fifo(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state };
	u32 processed = 0, i;

	ob_dpc_sw_init(&state);
	for (i = 0; i < 24; i++)
		enqueue(&state, OB_DPC_SW_TX_COMPLETE, i);
	check("partial drain before wrap", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 20, &processed) == 0 &&
		processed == 16 && sink.n == 16);
	check("second bounded drain", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 4, &processed) == 0 &&
		processed == 4 && sink.n == 20);
	for (i = 24; i < 44; i++)
		enqueue(&state, OB_DPC_SW_RX_FRAME, i);
	check("ring wraps while retaining FIFO", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 20, &processed) == 0 &&
		processed == 16 && sink.n == 36);
	check("wrapped queue drains remainder", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 20, &processed) == 0 &&
		processed == 8 && sink.n == 44);
	for (i = 0; i < 44; i++)
		check("FIFO token order", sink.seen[i].token == i);
}

static void test_loss_during_callback_stops_batch(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state, .lose_after = 1 };
	u32 processed = 0;

	ob_dpc_sw_init(&state);
	enqueue(&state, OB_DPC_SW_TX_COMPLETE, 45);
	enqueue(&state, OB_DPC_SW_RX_FRAME, 46);
	check("loss during callback returns device error",
		ob_dpc_sw_dispatch(&state, &ob_dpc_sw_test_ops, &sink,
			OB_DPC_SW_MAX_BATCH, &processed) == -EIO);
	check("loss during callback prevents later processing",
		processed == 1 && sink.n == 1 && state.count == 1 &&
		state.dev_lost);
}

static void test_callback_error_failstops(void)
{
	struct ob_dpc_sw_state state;
	struct sink sink = { .state = &state, .fail_ret = -EIO };
	struct ob_dpc_sw_event cancelled[OB_DPC_SW_CAPACITY];
	u32 processed = 0, cancelled_count = 0;

	ob_dpc_sw_init(&state);
	enqueue(&state, OB_DPC_SW_RX_FRAME, 50);
	enqueue(&state, OB_DPC_SW_TX_COMPLETE, 51);
	check("callback error propagates", ob_dpc_sw_dispatch(&state,
		&ob_dpc_sw_test_ops, &sink, 8, &processed) == -EIO);
	check("callback error is latched", ob_dpc_sw_failed(&state) &&
		state.failed_token == 50);
	check("failed event not retried and later work not processed",
		processed == 1 && sink.n == 1 && state.count == 1);
	check("failure permits explicit ownership reclaim",
		ob_dpc_sw_cancel_pending(&state, cancelled,
			OB_DPC_SW_CAPACITY, &cancelled_count) == 0 &&
		cancelled_count == 1 && cancelled[0].token == 51);
}

int main(void)
{
	test_duplicate_dispatch_and_bounded_batch();
	test_maximum_batch_is_clamped();
	test_reentrant_dispatch_is_rejected();
	test_quiesce_preserves_reclaimable_pending_events();
	test_device_loss_blocks_queue_and_dispatch();
	test_ring_wraparound_preserves_fifo();
	test_loss_during_callback_stops_batch();
	test_callback_error_failstops();
	if (failures)
		return 1;
	puts("ob_dpc_sw_test: PASS");
	return 0;
}

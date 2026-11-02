/* SPDX-License-Identifier: GPL-2.0-only */
#include "ob_dpc_sw.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

static bool ob_dpc_sw_kind_valid(enum ob_dpc_sw_kind kind)
{
	return kind == OB_DPC_SW_TX_COMPLETE || kind == OB_DPC_SW_RX_FRAME;
}

static u32 ob_dpc_sw_advance(u32 index)
{
	index++;
	return index == OB_DPC_SW_CAPACITY ? 0 : index;
}

void ob_dpc_sw_init(struct ob_dpc_sw_state *state)
{
	if (!state)
		return;
	memset(state, 0, sizeof(*state));
	state->accepting = true;
}

int ob_dpc_sw_enqueue(struct ob_dpc_sw_state *state,
		      const struct ob_dpc_sw_event *event)
{
	if (!state || !event || !ob_dpc_sw_kind_valid(event->kind))
		return -EINVAL;
	if (state->dev_lost) {
		state->rejected++;
		return -EIO;
	}
	if (!state->accepting || state->failed) {
		state->rejected++;
		return -ESHUTDOWN;
	}
	if (state->count == OB_DPC_SW_CAPACITY) {
		state->rejected++;
		return -ENOSPC;
	}

	state->queue[state->head] = *event;
	state->head = ob_dpc_sw_advance(state->head);
	state->count++;
	state->enqueued++;
	return 0;
}

int ob_dpc_sw_dispatch(struct ob_dpc_sw_state *state,
		       const struct ob_dpc_sw_ops *ops, void *ctx,
		       u32 budget, u32 *processed)
{
	u32 limit, done = 0;
	int ret = 0;

	if (processed)
		*processed = 0;
	if (!state || !processed || !ops || !ops->process)
		return -EINVAL;
	if (state->dev_lost)
		return -EIO;
	if (state->failed)
		return state->failure_error ? state->failure_error : -EIO;
	if (!state->accepting)
		return -ESHUTDOWN;
	if (state->dispatching)
		return -EBUSY;

	limit = budget < OB_DPC_SW_MAX_BATCH ? budget : OB_DPC_SW_MAX_BATCH;
	state->dispatching = true;
	while (done < limit && state->count && state->accepting &&
	       !state->dev_lost && !state->failed) {
		struct ob_dpc_sw_event event = state->queue[state->tail];

		/* Remove before callback: a reentrant dispatch cannot claim it twice. */
		state->tail = ob_dpc_sw_advance(state->tail);
		state->count--;
		ret = ops->process(ctx, &event);
		done++;
		state->processed++;
		if (ret) {
			state->failed = true;
			state->accepting = false;
			state->failed_token = event.token;
			state->failure_error = ret;
			break;
		}
	}
	state->dispatching = false;
	*processed = done;
	if (state->dev_lost)
		return -EIO;
	if (state->failed)
		return state->failure_error ? state->failure_error : -EIO;
	return 0;
}

int ob_dpc_sw_quiesce(struct ob_dpc_sw_state *state)
{
	if (!state)
		return -EINVAL;
	state->accepting = false;
	return 0;
}

int ob_dpc_sw_mark_dev_lost(struct ob_dpc_sw_state *state)
{
	if (!state)
		return -EINVAL;
	state->dev_lost = true;
	state->accepting = false;
	return 0;
}

int ob_dpc_sw_cancel_pending(struct ob_dpc_sw_state *state,
			     struct ob_dpc_sw_event *out, u32 capacity,
			     u32 *cancelled)
{
	u32 count = 0;

	if (cancelled)
		*cancelled = 0;
	if (!state || !cancelled || (capacity && !out))
		return -EINVAL;
	if (state->accepting || state->dispatching)
		return -EBUSY;

	while (state->count && count < capacity) {
		out[count++] = state->queue[state->tail];
		state->tail = ob_dpc_sw_advance(state->tail);
		state->count--;
	}
	state->reclaimed += count;
	*cancelled = count;
	return 0;
}

bool ob_dpc_sw_failed(const struct ob_dpc_sw_state *state)
{
	return state && state->failed;
}

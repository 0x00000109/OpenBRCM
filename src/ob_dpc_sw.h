/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_DPC_SW_H_
#define _OB_DPC_SW_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef u64 ob_dpc_sw_token_t;
#else
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t ob_dpc_sw_token_t;
typedef uint32_t u32;
#endif

/* Opaque software event classes; these are not D11 interrupt/status bits. */
enum ob_dpc_sw_kind {
	OB_DPC_SW_TX_COMPLETE = 1,
	OB_DPC_SW_RX_FRAME = 2,
};

struct ob_dpc_sw_event {
	enum ob_dpc_sw_kind kind;
	ob_dpc_sw_token_t token;
};

#define OB_DPC_SW_CAPACITY	32u
#define OB_DPC_SW_MAX_BATCH	16u

/*
 * Per-device, software-only state. The embedding driver must serialize all
 * calls (normally via its IRQ/tasklet ownership discipline); this module does
 * not access MMIO, acknowledge interrupts, or provide a kernel lock.
 */
struct ob_dpc_sw_state {
	struct ob_dpc_sw_event queue[OB_DPC_SW_CAPACITY];
	u32 head;
	u32 tail;
	u32 count;
	bool accepting;
	bool dev_lost;
	bool failed;
	bool dispatching;
	ob_dpc_sw_token_t failed_token;
	int failure_error;
	u32 enqueued;
	u32 processed;
	u32 reclaimed;
	u32 rejected;
};

struct ob_dpc_sw_ops {
	/* A successful call takes ownership of this event exactly once. */
	int (*process)(void *ctx, const struct ob_dpc_sw_event *event);
};

void ob_dpc_sw_init(struct ob_dpc_sw_state *state);
int ob_dpc_sw_enqueue(struct ob_dpc_sw_state *state,
		      const struct ob_dpc_sw_event *event);
int ob_dpc_sw_dispatch(struct ob_dpc_sw_state *state,
		       const struct ob_dpc_sw_ops *ops, void *ctx,
		       u32 budget, u32 *processed);
int ob_dpc_sw_quiesce(struct ob_dpc_sw_state *state);
int ob_dpc_sw_mark_dev_lost(struct ob_dpc_sw_state *state);
/* After terminal stop, transfer queued event ownership back without processing. */
int ob_dpc_sw_cancel_pending(struct ob_dpc_sw_state *state,
			     struct ob_dpc_sw_event *out, u32 capacity,
			     u32 *cancelled);
bool ob_dpc_sw_failed(const struct ob_dpc_sw_state *state);

#endif /* _OB_DPC_SW_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_TX_PATH_H_
#define _OB_TX_PATH_H_

#include "ob_tx_sw.h"
#include "ob_dma.h"

#define OB_TX_PATH_QUEUE_COUNT 4u

struct ob_tx_path;

/* Callbacks are invoked in the caller's serialized TX context. map/submit
 * must not sleep. The physical ring slot is marked SUBMITTED before submit
 * runs so an eventual publication cannot precede software ownership. The
 * caller must serialize completion against enqueue until submit returns.
 * Submit success transfers packet and mapping ownership; submit failure must
 * guarantee that no hardware-visible publication or retention occurred.
 */
struct ob_tx_path_ops {
	int (*map)(void *ctx, unsigned int queue, void *packet,
		   ob_tx_sw_u32 length, ob_tx_sw_u64 *dma);
	void (*unmap)(void *ctx, unsigned int queue, ob_tx_sw_u64 dma,
		      ob_tx_sw_u32 length);
	int (*prepare)(void *ctx, unsigned int queue, void *packet,
		       ob_tx_sw_u64 dma, ob_tx_sw_u32 length,
		       const struct ob_tx_sw_ticket *ticket,
		       ob_tx_sw_u16 ring_slot);
	int (*submit)(void *ctx, unsigned int queue, void *packet,
		      ob_tx_sw_u64 dma, ob_tx_sw_u32 length,
		      const struct ob_tx_sw_ticket *ticket,
		      ob_tx_sw_u16 ring_slot);
	int (*cancel)(void *ctx, unsigned int queue,
		      const struct ob_tx_sw_ticket *ticket);
	void (*complete)(void *ctx, unsigned int queue, void *packet, int status);
	void (*queue_stop)(void *ctx, unsigned int queue);
	void (*queue_wake)(void *ctx, unsigned int queue);
};

struct ob_tx_path_queue {
	struct ob_tx_sw sw;
	struct ob_tx_path *owner;
	unsigned int index;
	bool backpressured;
	bool blocked_on_queue_capacity;
	bool blocked_on_ring_capacity;
	bool rollback_pending;
	ob_tx_sw_u16 rollback_ring_slot;
	u32 rollback_ring_generation;
};

/* Four independent software queues preserve mac80211's queue mapping. They
 * are logical queue indices only; no mapping to D11 hardware FIFOs is implied.
 */
struct ob_tx_path {
	struct ob_tx_path_queue queue[OB_TX_PATH_QUEUE_COUNT];
	struct ob_dma_ring *ring;
	const struct ob_tx_path_ops *ops;
	void *ctx;
	ob_tx_sw_u16 capacity;
	ob_tx_sw_u8 dma_addr_bits;
	ob_tx_sw_u32 max_length;
	bool initialized;
	bool running;
	bool faulted;
};

/* Derive the complete 802.11 header length from FC and reject unsupported or
 * truncated headers before any DMA mapping or ownership transfer.
 */
int ob_tx_frame_header_length(ob_tx_sw_u16 frame_control,
			      ob_tx_sw_u32 frame_length,
			      ob_tx_sw_u32 *header_length);

int ob_tx_path_init(struct ob_tx_path *path,
		    struct ob_tx_sw_slot *slots_per_queue,
		    ob_tx_sw_u16 queue_capacity,
		    ob_tx_sw_u8 dma_addr_bits,
		    ob_tx_sw_u32 max_length,
			    struct ob_dma_ring *ring,
		    const struct ob_tx_path_ops *ops, void *ctx);
int ob_tx_path_start(struct ob_tx_path *path);
int ob_tx_path_stop(struct ob_tx_path *path);
int ob_tx_path_enqueue(struct ob_tx_path *path, void *packet,
		       ob_tx_sw_u16 frame_control,
		       ob_tx_sw_u32 frame_length,
		       unsigned int queue,
		       struct ob_tx_sw_ticket *ticket);
int ob_tx_path_enqueue_identified(struct ob_tx_path *path, void *packet,
				  ob_tx_sw_u16 frame_control,
				  ob_tx_sw_u32 frame_length,
				  unsigned int queue,
				  ob_tx_sw_u16 frame_id,
				  struct ob_tx_sw_ticket *ticket);
int ob_tx_path_complete(struct ob_tx_path *path, unsigned int queue,
		const struct ob_tx_sw_ticket *ticket, int status);
int ob_tx_path_dma_reclaim_packet(struct ob_tx_path *path,
				  unsigned int queue, void *returned_packet,
				  struct ob_tx_sw_ticket *ticket);
int ob_tx_path_complete_reclaimed_frame_id(
	struct ob_tx_path *path, unsigned int queue,
	const struct ob_tx_sw_ticket *ticket,
	ob_tx_sw_u16 reported_frame_id);
ob_tx_sw_u16 ob_tx_path_pending(const struct ob_tx_path *path,
				unsigned int queue);

#endif /* _OB_TX_PATH_H_ */

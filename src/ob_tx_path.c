/* SPDX-License-Identifier: GPL-2.0-only */
#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "ob_tx_path.h"

#define OB_TX_FC_VERSION_MASK 0x0003u
#define OB_TX_FC_TYPE_MASK    0x000cu
#define OB_TX_FC_SUBTYPE_MASK 0x00f0u
#define OB_TX_FC_TO_DS        0x0100u
#define OB_TX_FC_FROM_DS      0x0200u
#define OB_TX_FC_ORDER        0x8000u

static void ob_tx_path_maybe_wake(struct ob_tx_path *path);

static struct ob_tx_sw_slot *
ob_tx_path_find_unmap_slot(struct ob_tx_path_queue *queue, ob_tx_sw_u64 dma)
{
	struct ob_tx_sw_slot *slot;

	/* Completion and reclaim are oldest-first within a logical queue. During
	 * submit failure the ticket remains at head in SUBMITTING state until the
	 * unmap callback returns. Do not join solely on DMA address: mappings can
	 * be reused by an IOMMU after their lifetime ends.
	 */
	if (queue->sw.slots[queue->sw.head].state == OB_TX_SW_SUBMITTING) {
		slot = &queue->sw.slots[queue->sw.head];
		if (slot->ring_bound && slot->dma == dma)
			return slot;
	}
	if (queue->sw.pending) {
		slot = &queue->sw.slots[queue->sw.tail];
		if (slot->ring_bound && slot->dma == dma)
			return slot;
	}
	return NULL;
}

static struct ob_dma_slot *
ob_tx_path_ring_slot(struct ob_tx_path_queue *queue,
		     const struct ob_tx_sw_slot *sw_slot)
{
	struct ob_tx_path *path = queue->owner;
	struct ob_dma_ring *ring = path->ring;
	struct ob_dma_slot *slot;

	if (!sw_slot->ring_bound || !ob_dma_tx_ring_usable(ring) ||
	    !ob_dma_index_valid(ring->n, sw_slot->ring_slot))
		return NULL;
	slot = &ring->slot[sw_slot->ring_slot];
	if (slot->tx_generation != sw_slot->ring_generation ||
	    slot->tx_sw_slot !=
		(ob_tx_sw_u16)(sw_slot - queue->sw.slots) ||
	    slot->tx_sw_generation != sw_slot->generation ||
	    slot->tx_queue != queue->index)
		return NULL;
	return slot;
}

static void ob_tx_path_backpressure(struct ob_tx_path *path,
				    unsigned int queue_index,
				    bool queue_full, bool ring_full)
{
	struct ob_tx_path_queue *queue;

	if (!path->running || queue_index >= OB_TX_PATH_QUEUE_COUNT)
		return;
	queue = &path->queue[queue_index];
	queue->blocked_on_queue_capacity |= queue_full;
	queue->blocked_on_ring_capacity |= ring_full;
	if (!queue->backpressured) {
		queue->backpressured = true;
		if (path->ops->queue_stop)
			path->ops->queue_stop(path->ctx, queue_index);
	}
}

static void ob_tx_path_maybe_wake(struct ob_tx_path *path)
{
	unsigned int i;

	if (!path->running || path->faulted || !ob_dma_tx_ring_usable(path->ring) ||
	    !ob_dma_avail(path->ring->n, path->ring->head, path->ring->tail))
		return;
	for (i = 0; i < OB_TX_PATH_QUEUE_COUNT; i++) {
		struct ob_tx_path_queue *queue = &path->queue[i];
		ob_tx_sw_u16 low = path->capacity / 2;

		ob_tx_sw_u16 pending = ob_tx_sw_pending(&queue->sw);

		if (!queue->backpressured ||
		    (queue->blocked_on_queue_capacity && pending > low) ||
		    (queue->blocked_on_ring_capacity &&
		     (!ob_dma_avail(path->ring->n, path->ring->head,
				    path->ring->tail) || pending >= path->capacity)))
			continue;
		queue->backpressured = false;
		queue->blocked_on_queue_capacity = false;
		queue->blocked_on_ring_capacity = false;
		if (path->ops->queue_wake)
			path->ops->queue_wake(path->ctx, i);
	}
}

static int ob_tx_path_map(void *ctx, void *packet, ob_tx_sw_u32 length,
			  ob_tx_sw_u64 *dma)
{
	struct ob_tx_path_queue *queue = ctx;
	struct ob_tx_path *path = queue->owner;

	return path->ops->map(path->ctx, queue->index, packet, length, dma);
}

static void ob_tx_path_unmap(void *ctx, ob_tx_sw_u64 dma,
			     ob_tx_sw_u32 length)
{
	struct ob_tx_path_queue *queue = ctx;
	struct ob_tx_path *path = queue->owner;
	struct ob_tx_sw_slot *sw_slot = ob_tx_path_find_unmap_slot(queue, dma);
	struct ob_dma_slot *ring_slot;

	if (sw_slot) {
		ring_slot = ob_tx_path_ring_slot(queue, sw_slot);
		if (ring_slot && ring_slot->mapped &&
		    (ring_slot->tx_state == OB_DMA_TX_SLOT_COMPLETED ||
		     ring_slot->tx_state == OB_DMA_TX_SLOT_RECLAIMED ||
		     ring_slot->tx_state == OB_DMA_TX_SLOT_CANCELED)) {
			(void)ob_dma_tx_slot_unmap(path->ring, sw_slot->ring_slot,
					   sw_slot->ring_generation);
			if (queue->rollback_pending &&
			    queue->rollback_ring_slot == sw_slot->ring_slot &&
			    queue->rollback_ring_generation ==
				sw_slot->ring_generation) {
				if (!ob_dma_tx_slot_release(path->ring,
							    sw_slot->ring_slot,
							    sw_slot->ring_generation))
					path->faulted = true;
				queue->rollback_pending = false;
			}
		}
	}

	path->ops->unmap(path->ctx, queue->index, dma, length);
}

static int ob_tx_path_submit(void *ctx, void *packet, ob_tx_sw_u64 dma,
			     ob_tx_sw_u32 length,
			     const struct ob_tx_sw_ticket *ticket)
{
	struct ob_tx_path_queue *queue = ctx;
	struct ob_tx_path *path = queue->owner;
	struct ob_tx_sw_slot *sw_slot;
	struct ob_dma_desc *desc;
	ob_tx_sw_u16 ring_index;
	u32 ring_generation;
	int ret;

	if (path->faulted || !ticket || ticket->slot >= queue->sw.capacity)
		return -EUCLEAN;
	sw_slot = &queue->sw.slots[ticket->slot];
	if (sw_slot->packet != packet || sw_slot->generation != ticket->generation ||
	    sw_slot->state != OB_TX_SW_SUBMITTING || sw_slot->ring_bound)
		return -EUCLEAN;
	ret = ob_dma_tx_ring_reserve(path->ring, (struct sk_buff *)packet,
				    (dma_addr_t)dma, queue->index,
				    ticket->slot, ticket->generation,
				    &ring_index, &ring_generation);
	if (ret)
		return ret;
	sw_slot->ring_slot = ring_index;
	sw_slot->ring_generation = ring_generation;
	sw_slot->ring_bound = true;
	desc = ob_dma_desc_at(path->ring, ring_index);
	if (!desc) {
		(void)ob_dma_tx_slot_transition(path->ring, ring_index,
						ring_generation,
						OB_DMA_TX_SLOT_RESERVED,
						OB_DMA_TX_SLOT_CANCELED);
		queue->rollback_pending = true;
		queue->rollback_ring_slot = ring_index;
		queue->rollback_ring_generation = ring_generation;
		return -EUCLEAN;
	}
	ob_dma_desc_zero(desc);
	ret = path->ops->prepare(path->ctx, queue->index, packet, dma, length,
				ticket, ring_index);
	if (ret) {
		(void)ob_dma_tx_slot_transition(path->ring, ring_index,
						ring_generation,
						OB_DMA_TX_SLOT_RESERVED,
						OB_DMA_TX_SLOT_CANCELED);
		queue->rollback_pending = true;
		queue->rollback_ring_slot = ring_index;
		queue->rollback_ring_generation = ring_generation;
		return ret;
	}
	if (!ob_dma_tx_slot_transition(path->ring, ring_index, ring_generation,
				       OB_DMA_TX_SLOT_RESERVED,
				       OB_DMA_TX_SLOT_PREPARED)) {
		path->faulted = true;
		return -EUCLEAN;
	}

	/* Establish software completion ownership before the backend can publish
	 * the producer pointer. The caller serializes enqueue against completion;
	 * a failed submit must still guarantee that nothing reached the device.
	 */
	if (!ob_dma_tx_slot_transition(path->ring, ring_index, ring_generation,
				       OB_DMA_TX_SLOT_PREPARED,
				       OB_DMA_TX_SLOT_SUBMITTED)) {
		path->faulted = true;
		return -EUCLEAN;
	}
	ret = path->ops->submit(path->ctx, queue->index, packet, dma, length,
				ticket, ring_index);
	if (ret) {
		(void)ob_dma_tx_slot_transition(path->ring, ring_index,
						ring_generation,
						OB_DMA_TX_SLOT_SUBMITTED,
						OB_DMA_TX_SLOT_CANCELED);
		queue->rollback_pending = true;
		queue->rollback_ring_slot = ring_index;
		queue->rollback_ring_generation = ring_generation;
		return ret;
	}
	return 0;
}

static int ob_tx_path_cancel(void *ctx,
			     const struct ob_tx_sw_ticket *ticket)
{
	struct ob_tx_path_queue *queue = ctx;
	struct ob_tx_path *path = queue->owner;
	struct ob_tx_sw_slot *sw_slot;
	struct ob_dma_slot *ring_slot;
	int ret;

	if (!path->ops->cancel)
		return -EOPNOTSUPP;
	if (!ticket || ticket->slot >= queue->sw.capacity)
		return -EINVAL;
	sw_slot = &queue->sw.slots[ticket->slot];
	ring_slot = ob_tx_path_ring_slot(queue, sw_slot);
	if (!ring_slot || ring_slot->tx_state != OB_DMA_TX_SLOT_SUBMITTED)
		return -EUCLEAN;
	ret = path->ops->cancel(path->ctx, queue->index, ticket);
	if (ret)
		return ret;
	if (!ob_dma_tx_slot_transition(path->ring, sw_slot->ring_slot,
				       sw_slot->ring_generation,
				       OB_DMA_TX_SLOT_SUBMITTED,
				       OB_DMA_TX_SLOT_CANCELED))
		return -EUCLEAN;
	return 0;
}

static void ob_tx_path_complete_packet(void *ctx, void *packet, int status)
{
	struct ob_tx_path_queue *queue = ctx;
	struct ob_tx_path *path = queue->owner;
	unsigned int i;
	bool released = false;

	for (i = 0; i < path->ring->n; i++) {
		struct ob_dma_slot *slot = &path->ring->slot[i];

		if (slot->skb != (struct sk_buff *)packet ||
		    slot->tx_queue != queue->index)
			continue;
		if (ob_dma_tx_slot_release(path->ring, i, slot->tx_generation))
			released = true;
		break;
	}
	if (!released) {
		path->faulted = true;
		return;
	}

	path->ops->complete(path->ctx, queue->index, packet, status);
	ob_tx_path_maybe_wake(path);
}

static const struct ob_tx_sw_ops ob_tx_path_sw_ops = {
	.map = ob_tx_path_map,
	.unmap = ob_tx_path_unmap,
	.submit = ob_tx_path_submit,
	.cancel = ob_tx_path_cancel,
	.complete = ob_tx_path_complete_packet,
};

int ob_tx_frame_header_length(ob_tx_sw_u16 fc, ob_tx_sw_u32 frame_length,
			      ob_tx_sw_u32 *header_length)
{
	ob_tx_sw_u32 length;
	ob_tx_sw_u16 type, subtype;

	if (!header_length || (fc & OB_TX_FC_VERSION_MASK))
		return -EINVAL;

	type = fc & OB_TX_FC_TYPE_MASK;
	subtype = fc & OB_TX_FC_SUBTYPE_MASK;
	switch (type) {
	case 0x0000u: /* management */
		length = 24;
		/* Order adds an HT Control field after the 24-byte header, as in
		 * Linux ieee80211_hdrlen(). A 24..27-byte Order frame is truncated.
		 */
		if (fc & OB_TX_FC_ORDER)
			length += 4;
		break;
	case 0x0004u: /* control */
		length = subtype == 0x00c0u || subtype == 0x00d0u ? 10 : 16;
		break;
	case 0x0008u: /* data */
		length = 24;
		if ((fc & (OB_TX_FC_TO_DS | OB_TX_FC_FROM_DS)) ==
		    (OB_TX_FC_TO_DS | OB_TX_FC_FROM_DS))
			length += 6;
		if (subtype & 0x0080u) {
			length += 2; /* QoS control */
			if (fc & OB_TX_FC_ORDER)
				length += 4; /* HT control */
		}
		break;
	default: /* reserved/extension formats have no admitted TX contract */
		return -EINVAL;
	}

	*header_length = length;
	return frame_length < length ? -EMSGSIZE : 0;
}

int ob_tx_path_init(struct ob_tx_path *path,
		    struct ob_tx_sw_slot *slots_per_queue,
		    ob_tx_sw_u16 queue_capacity,
		    ob_tx_sw_u8 dma_addr_bits,
		    ob_tx_sw_u32 max_length,
			    struct ob_dma_ring *ring,
		    const struct ob_tx_path_ops *ops, void *ctx)
{
	unsigned int i;
	int ret;

	if (!path || !slots_per_queue || !queue_capacity ||
	    !ob_dma_tx_ring_empty(ring) || !ops || !ops->map ||
	    !ops->unmap || !ops->prepare || !ops->submit || !ops->complete)
		return -EINVAL;
	if (!!ops->queue_stop != !!ops->queue_wake)
		return -EINVAL;

	memset(path, 0, sizeof(*path));
	path->ops = ops;
	path->ctx = ctx;
	path->ring = ring;
	path->capacity = queue_capacity;
	path->dma_addr_bits = dma_addr_bits;
	path->max_length = max_length;

	for (i = 0; i < OB_TX_PATH_QUEUE_COUNT; i++) {
		struct ob_tx_path_queue *queue = &path->queue[i];

		queue->owner = path;
		queue->index = i;
		ret = ob_tx_sw_init(&queue->sw,
				    slots_per_queue + i * queue_capacity,
				    queue_capacity, dma_addr_bits, max_length,
				    &ob_tx_path_sw_ops, queue);
		if (ret) {
			path->initialized = false;
			return ret;
		}
	}
	path->initialized = true;
	return 0;
}

int ob_tx_path_start(struct ob_tx_path *path)
{
	unsigned int i;
	int ret;

	if (!path || !path->initialized)
		return -EINVAL;
	for (i = 0; i < OB_TX_PATH_QUEUE_COUNT; i++) {
		ret = ob_tx_sw_start(&path->queue[i].sw);
		if (ret) {
			while (i)
				(void)ob_tx_sw_stop(&path->queue[--i].sw);
			return ret;
		}
	}
	path->running = true;
	ob_tx_path_maybe_wake(path);
	return 0;
}

int ob_tx_path_stop(struct ob_tx_path *path)
{
	unsigned int i;
	int first_error = 0;

	if (!path || !path->initialized)
		return -EINVAL;
	path->running = false;
	for (i = 0; i < OB_TX_PATH_QUEUE_COUNT; i++) {
		int ret = ob_tx_sw_stop(&path->queue[i].sw);

		if (ret && !first_error)
			first_error = ret;
	}
	return first_error;
}

static int ob_tx_path_enqueue_common(struct ob_tx_path *path, void *packet,
				     ob_tx_sw_u16 frame_control,
				     ob_tx_sw_u32 frame_length,
				     unsigned int queue,
				     bool frame_id_valid,
				     ob_tx_sw_u16 frame_id,
				     struct ob_tx_sw_ticket *ticket)
{
	ob_tx_sw_u32 header_length;
	int ret;

	if (!path || !path->initialized || !packet || !ticket ||
	    queue >= OB_TX_PATH_QUEUE_COUNT || frame_length > path->max_length)
		return -EINVAL;
	if (path->faulted)
		return -EUCLEAN;
	ret = ob_tx_frame_header_length(frame_control, frame_length,
					&header_length);
	if (ret)
		return ret;
	if (frame_id_valid)
		ret = ob_tx_sw_enqueue_identified(&path->queue[queue].sw, packet,
						 frame_length, frame_id, ticket);
	else
		ret = ob_tx_sw_enqueue(&path->queue[queue].sw, packet, frame_length,
				       ticket);
	if (ret == -ENOSPC) {
		ob_tx_sw_u16 pending = ob_tx_sw_pending(&path->queue[queue].sw);
		bool queue_full = pending >= path->capacity;
		bool ring_full = !ob_dma_avail(path->ring->n, path->ring->head,
					       path->ring->tail);

		if (queue_full || ring_full)
			ob_tx_path_backpressure(path, queue, queue_full, ring_full);
	} else if (!ret && ob_tx_sw_pending(&path->queue[queue].sw) >=
		   path->capacity)
		ob_tx_path_backpressure(path, queue, true, false);
	return ret;
}

int ob_tx_path_enqueue(struct ob_tx_path *path, void *packet,
		       ob_tx_sw_u16 frame_control,
		       ob_tx_sw_u32 frame_length, unsigned int queue,
		       struct ob_tx_sw_ticket *ticket)
{
	return ob_tx_path_enqueue_common(path, packet, frame_control,
					 frame_length, queue, false, 0, ticket);
}

int ob_tx_path_enqueue_identified(struct ob_tx_path *path, void *packet,
				  ob_tx_sw_u16 frame_control,
				  ob_tx_sw_u32 frame_length,
				  unsigned int queue,
				  ob_tx_sw_u16 frame_id,
				  struct ob_tx_sw_ticket *ticket)
{
	return ob_tx_path_enqueue_common(path, packet, frame_control,
					 frame_length, queue, true, frame_id,
					 ticket);
}

int ob_tx_path_complete(struct ob_tx_path *path, unsigned int queue,
		const struct ob_tx_sw_ticket *ticket, int status)
{
	struct ob_tx_sw_slot *sw_slot;
	struct ob_dma_slot *ring_slot;
	int ret;

	if (!path || !path->initialized || queue >= OB_TX_PATH_QUEUE_COUNT)
		return -EINVAL;
	if (!ticket || ticket->slot >= path->queue[queue].sw.capacity)
		return -EINVAL;
	sw_slot = &path->queue[queue].sw.slots[ticket->slot];
	ring_slot = ob_tx_path_ring_slot(&path->queue[queue], sw_slot);
	if (!ring_slot || ring_slot->tx_state != OB_DMA_TX_SLOT_SUBMITTED)
		return -ESTALE;
	if (!ob_dma_tx_slot_transition(path->ring, sw_slot->ring_slot,
				       sw_slot->ring_generation,
				       OB_DMA_TX_SLOT_SUBMITTED,
				       OB_DMA_TX_SLOT_COMPLETED))
		return -ESTALE;
	ret = ob_tx_sw_complete(&path->queue[queue].sw, ticket, status);
	if (ret)
		(void)ob_dma_tx_slot_transition(path->ring, sw_slot->ring_slot,
						sw_slot->ring_generation,
						OB_DMA_TX_SLOT_COMPLETED,
						OB_DMA_TX_SLOT_SUBMITTED);
	return ret;
}

int ob_tx_path_dma_reclaim_packet(struct ob_tx_path *path,
				  unsigned int queue, void *returned_packet,
				  struct ob_tx_sw_ticket *ticket)
{
	struct ob_tx_sw_ticket oldest;
	struct ob_tx_sw_slot *sw_slot;
	struct ob_dma_slot *ring_slot;
	int ret;

	if (!path || !path->initialized || queue >= OB_TX_PATH_QUEUE_COUNT)
		return -EINVAL;
	if (!ob_tx_sw_oldest_ticket(&path->queue[queue].sw, &oldest))
		return -ENOENT;
	sw_slot = &path->queue[queue].sw.slots[oldest.slot];
	ring_slot = ob_tx_path_ring_slot(&path->queue[queue], sw_slot);
	if (ring_slot && ring_slot->tx_state == OB_DMA_TX_SLOT_RECLAIMED)
		return -EALREADY;
	if (!ring_slot || ring_slot->tx_state != OB_DMA_TX_SLOT_SUBMITTED)
		return -ESTALE;
	if (!ob_dma_tx_slot_transition(path->ring, sw_slot->ring_slot,
				       sw_slot->ring_generation,
				       OB_DMA_TX_SLOT_SUBMITTED,
				       OB_DMA_TX_SLOT_COMPLETED))
		return -ESTALE;
	ret = ob_tx_sw_dma_reclaim_packet(&path->queue[queue].sw,
					 returned_packet, ticket);
	if (ret) {
		(void)ob_dma_tx_slot_transition(path->ring, sw_slot->ring_slot,
						sw_slot->ring_generation,
						OB_DMA_TX_SLOT_COMPLETED,
						OB_DMA_TX_SLOT_SUBMITTED);
		return ret;
	}
	if (!ob_dma_tx_slot_transition(path->ring, sw_slot->ring_slot,
				       sw_slot->ring_generation,
				       OB_DMA_TX_SLOT_COMPLETED,
				       OB_DMA_TX_SLOT_RECLAIMED))
		path->faulted = true;
	return path->faulted ? -EUCLEAN : 0;
}

int ob_tx_path_complete_reclaimed_frame_id(
	struct ob_tx_path *path, unsigned int queue,
	const struct ob_tx_sw_ticket *ticket,
	ob_tx_sw_u16 reported_frame_id)
{
	struct ob_tx_sw_slot *sw_slot;
	struct ob_dma_slot *ring_slot;
	int ret;

	if (!path || !path->initialized || queue >= OB_TX_PATH_QUEUE_COUNT)
		return -EINVAL;
	if (!path->queue[queue].sw.pending)
		return -ENOENT;
	if (!ticket || ticket->slot >= path->queue[queue].sw.capacity)
		return -EINVAL;
	sw_slot = &path->queue[queue].sw.slots[ticket->slot];
	ring_slot = ob_tx_path_ring_slot(&path->queue[queue], sw_slot);
	if (!ring_slot || ring_slot->tx_state != OB_DMA_TX_SLOT_RECLAIMED)
		return -ESTALE;
	ret = ob_tx_sw_complete_reclaimed_frame_id(&path->queue[queue].sw,
						   ticket, reported_frame_id);
	return ret;
}

ob_tx_sw_u16 ob_tx_path_pending(const struct ob_tx_path *path,
				unsigned int queue)
{
	if (!path || !path->initialized || queue >= OB_TX_PATH_QUEUE_COUNT)
		return 0;
	return ob_tx_sw_pending(&path->queue[queue].sw);
}

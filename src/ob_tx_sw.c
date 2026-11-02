// SPDX-License-Identifier: GPL-2.0-only
#include "ob_tx_sw.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

static ob_tx_sw_u16 ob_tx_sw_next(const struct ob_tx_sw *tx,
				 ob_tx_sw_u16 index)
{
	return index + 1 == tx->capacity ? 0 : (ob_tx_sw_u16)(index + 1);
}

static bool ob_tx_sw_packet_pending(const struct ob_tx_sw *tx,
				   const void *packet)
{
	ob_tx_sw_u16 index = tx->tail;
	ob_tx_sw_u16 i;

	for (i = 0; i < tx->pending; i++) {
		if (tx->slots[index].packet == packet)
			return true;
		index = ob_tx_sw_next(tx, index);
	}
	return false;
}

static bool ob_tx_sw_dma_fits(ob_tx_sw_u64 dma, ob_tx_sw_u8 bits)
{
	if (bits == 64)
		return true;
	return (dma >> bits) == 0;
}

static bool ob_tx_sw_valid(const struct ob_tx_sw *tx)
{
	return tx && tx->slots && tx->ops && tx->ops->map && tx->ops->unmap &&
		tx->ops->submit && tx->ops->complete && tx->capacity != 0 &&
		tx->head < tx->capacity && tx->tail < tx->capacity &&
			tx->pending <= tx->capacity && tx->dma_addr_bits != 0 &&
			tx->dma_addr_bits <= 64 && tx->max_length != 0;
}

static void ob_tx_sw_slot_clear(struct ob_tx_sw_slot *slot)
{
	slot->packet = NULL;
	slot->dma = 0;
	slot->length = 0;
	slot->ring_slot = 0;
	slot->ring_generation = 0;
	slot->ring_bound = false;
	slot->frame_id = 0;
	slot->frame_id_valid = false;
	slot->state = OB_TX_SW_FREE;
}

int ob_tx_sw_init(struct ob_tx_sw *tx, struct ob_tx_sw_slot *slots,
		  ob_tx_sw_u16 capacity, ob_tx_sw_u8 dma_addr_bits,
		  ob_tx_sw_u32 max_length, const struct ob_tx_sw_ops *ops,
		  void *ctx)
{
	ob_tx_sw_u16 i;

	if (!tx || !slots || !capacity || !ops || !ops->map || !ops->unmap ||
	    !ops->submit || !ops->complete || dma_addr_bits == 0 ||
	    dma_addr_bits > 64 || !max_length)
		return -EINVAL;

	tx->slots = slots;
	tx->ops = ops;
	tx->ctx = ctx;
	tx->capacity = capacity;
	tx->head = 0;
	tx->tail = 0;
	tx->pending = 0;
	tx->dma_addr_bits = dma_addr_bits;
	tx->max_length = max_length;
	tx->accepting = false;
	for (i = 0; i < capacity; i++) {
		slots[i].generation = 0;
		ob_tx_sw_slot_clear(&slots[i]);
	}
	return 0;
}

int ob_tx_sw_start(struct ob_tx_sw *tx)
{
	if (!ob_tx_sw_valid(tx))
		return -EINVAL;
	if (tx->pending)
		return -EBUSY;
	tx->accepting = true;
	return 0;
}

static int ob_tx_sw_enqueue_common(struct ob_tx_sw *tx, void *packet,
				  ob_tx_sw_u32 length, bool frame_id_valid,
				  ob_tx_sw_u16 frame_id,
				  struct ob_tx_sw_ticket *ticket);

int ob_tx_sw_enqueue(struct ob_tx_sw *tx, void *packet,
		     ob_tx_sw_u32 length, struct ob_tx_sw_ticket *ticket)
{
	return ob_tx_sw_enqueue_common(tx, packet, length, false, 0, ticket);
}

static int ob_tx_sw_enqueue_common(struct ob_tx_sw *tx, void *packet,
				   ob_tx_sw_u32 length, bool frame_id_valid,
				   ob_tx_sw_u16 frame_id,
				   struct ob_tx_sw_ticket *ticket)
{
	struct ob_tx_sw_slot *slot;
	struct ob_tx_sw_ticket candidate;
	ob_tx_sw_u64 dma;
	int ret;

	if (!ob_tx_sw_valid(tx) || !packet || !ticket || !length ||
	    length > tx->max_length)
		return -EINVAL;
	if (!tx->accepting)
		return -ESHUTDOWN;
	/* A returned packet pointer is used to join backend reclaim with this
	 * software queue. Do not permit two live entries to share that identity.
	 */
	if (ob_tx_sw_packet_pending(tx, packet))
		return -EALREADY;
	if (tx->pending == tx->capacity)
		return -ENOSPC;

	slot = &tx->slots[tx->head];
	if (slot->state != OB_TX_SW_FREE)
		return -EUCLEAN;
	if (slot->generation == (ob_tx_sw_u32)~(ob_tx_sw_u32)0)
		return -EOVERFLOW;

	dma = 0;
	ret = tx->ops->map(tx->ctx, packet, length, &dma);
	if (ret)
		return ret;
	if (!ob_tx_sw_dma_fits(dma, tx->dma_addr_bits)) {
		tx->ops->unmap(tx->ctx, dma, length);
		return -ERANGE;
	}

	candidate.slot = tx->head;
	candidate.generation = slot->generation + 1;
	slot->generation = candidate.generation;
	slot->packet = packet;
	slot->dma = dma;
	slot->length = length;
	slot->frame_id = frame_id;
	slot->frame_id_valid = frame_id_valid;
	slot->state = OB_TX_SW_SUBMITTING;

	ret = tx->ops->submit(tx->ctx, packet, dma, length, &candidate);
	if (ret) {
		tx->ops->unmap(tx->ctx, dma, length);
		ob_tx_sw_slot_clear(slot);
		return ret;
	}

	slot->state = OB_TX_SW_SUBMITTED;
	tx->pending++;
	tx->head = ob_tx_sw_next(tx, tx->head);
	*ticket = candidate;
	return 0;
}

int ob_tx_sw_enqueue_identified(struct ob_tx_sw *tx, void *packet,
				ob_tx_sw_u32 length,
				ob_tx_sw_u16 frame_id,
				struct ob_tx_sw_ticket *ticket)
{
	return ob_tx_sw_enqueue_common(tx, packet, length, true, frame_id, ticket);
}

bool ob_tx_sw_oldest_ticket(const struct ob_tx_sw *tx,
			    struct ob_tx_sw_ticket *ticket)
{
	const struct ob_tx_sw_slot *slot;

	if (!ob_tx_sw_valid(tx) || !ticket || !tx->pending)
		return false;
	slot = &tx->slots[tx->tail];
	if (slot->state != OB_TX_SW_SUBMITTED &&
	    slot->state != OB_TX_SW_DMA_RECLAIMED)
		return false;
	ticket->slot = tx->tail;
	ticket->generation = slot->generation;
	return true;
}

int ob_tx_sw_dma_reclaim(struct ob_tx_sw *tx,
			 const struct ob_tx_sw_ticket *ticket)
{
	struct ob_tx_sw_slot *slot;
	struct ob_tx_sw_ticket oldest;

	if (!ob_tx_sw_valid(tx) || !ticket)
		return -EINVAL;
	if (!ob_tx_sw_oldest_ticket(tx, &oldest))
		return -ENOENT;
	if (ticket->slot != oldest.slot ||
	    ticket->generation != oldest.generation)
		return -ESTALE;
	slot = &tx->slots[ticket->slot];
	if (slot->state == OB_TX_SW_DMA_RECLAIMED)
		return -EALREADY;
	if (slot->state != OB_TX_SW_SUBMITTED)
		return -EUCLEAN;

	/* DMA reclaim is distinct from packet/TX-status disposition. */
	tx->ops->unmap(tx->ctx, slot->dma, slot->length);
	slot->dma = 0;
	slot->state = OB_TX_SW_DMA_RECLAIMED;
	return 0;
}

int ob_tx_sw_complete(struct ob_tx_sw *tx,
		      const struct ob_tx_sw_ticket *ticket, int status)
{
	struct ob_tx_sw_slot *slot;
	struct ob_tx_sw_ticket oldest;
	void *packet;

	if (!ob_tx_sw_valid(tx) || !ticket)
		return -EINVAL;
	if (!ob_tx_sw_oldest_ticket(tx, &oldest))
		return -ENOENT;
	if (ticket->slot != oldest.slot ||
	    ticket->generation != oldest.generation)
		return -ESTALE;

	slot = &tx->slots[ticket->slot];
	packet = slot->packet;
	if (slot->state == OB_TX_SW_SUBMITTED)
		tx->ops->unmap(tx->ctx, slot->dma, slot->length);
	else if (slot->state != OB_TX_SW_DMA_RECLAIMED)
		return -EUCLEAN;
	ob_tx_sw_slot_clear(slot);
	tx->tail = ob_tx_sw_next(tx, tx->tail);
	tx->pending--;
	tx->ops->complete(tx->ctx, packet, status);
	return 0;
}

int ob_tx_sw_complete_frame_id(struct ob_tx_sw *tx,
			       const struct ob_tx_sw_ticket *ticket,
			       ob_tx_sw_u16 reported_frame_id)
{
	struct ob_tx_sw_slot *slot;
	int status;
	int ret;

	if (!ob_tx_sw_valid(tx) || !ticket)
		return -EINVAL;
	if (!tx->pending || tx->slots[tx->tail].state != OB_TX_SW_SUBMITTED)
		return -ENOENT;
	if (ticket->slot != tx->tail ||
	    ticket->generation != tx->slots[tx->tail].generation)
		return -ESTALE;
	slot = &tx->slots[ticket->slot];
	if (!slot->frame_id_valid)
		return -ENODATA;
	status = slot->frame_id == reported_frame_id ? 0 : -EBADMSG;
	ret = ob_tx_sw_complete(tx, ticket, status);
	if (ret)
		return ret;
	return status;
}

int ob_tx_sw_complete_reclaimed_frame_id(
	struct ob_tx_sw *tx, const struct ob_tx_sw_ticket *ticket,
	ob_tx_sw_u16 reported_frame_id)
{
	struct ob_tx_sw_slot *slot;
	int status;
	int ret;

	if (!ob_tx_sw_valid(tx) || !ticket)
		return -EINVAL;
	if (!tx->pending || tx->slots[tx->tail].state == OB_TX_SW_FREE)
		return -ENOENT;
	if (ticket->slot != tx->tail ||
	    ticket->generation != tx->slots[tx->tail].generation)
		return -ESTALE;
	slot = &tx->slots[ticket->slot];
	if (slot->state != OB_TX_SW_DMA_RECLAIMED)
		return -EINPROGRESS;
	if (!slot->frame_id_valid)
		return -ENODATA;
	status = slot->frame_id == reported_frame_id ? 0 : -EBADMSG;
	ret = ob_tx_sw_complete(tx, ticket, status);
	return ret ? ret : status;
}

int ob_tx_sw_dma_reclaim_packet(struct ob_tx_sw *tx, void *returned_packet,
				struct ob_tx_sw_ticket *ticket)
{
	struct ob_tx_sw_ticket oldest;
	int ret;

	if (!ob_tx_sw_valid(tx) || !returned_packet || !ticket)
		return -EINVAL;
	if (!tx->pending)
		return -ENOENT;
	if (tx->slots[tx->tail].packet != returned_packet)
		return -ESTALE;
	if (!ob_tx_sw_oldest_ticket(tx, &oldest))
		return -EUCLEAN;
	ret = ob_tx_sw_dma_reclaim(tx, &oldest);
	if (ret)
		return ret;
	*ticket = oldest;
	return 0;
}

int ob_tx_sw_stop(struct ob_tx_sw *tx)
{
	if (!ob_tx_sw_valid(tx))
		return -EINVAL;
	tx->accepting = false;
	while (tx->pending) {
		struct ob_tx_sw_ticket ticket;
		struct ob_tx_sw_slot *slot;
		void *packet;
	int ret;

		if (!ob_tx_sw_oldest_ticket(tx, &ticket))
			return -EUCLEAN;
		slot = &tx->slots[ticket.slot];
		if (slot->state == OB_TX_SW_SUBMITTED) {
			if (!tx->ops->cancel)
				return -EOPNOTSUPP;
			ret = tx->ops->cancel(tx->ctx, &ticket);
			if (ret)
				return ret;
		} else if (slot->state != OB_TX_SW_DMA_RECLAIMED) {
			return -EUCLEAN;
		}

		packet = slot->packet;
		if (slot->state == OB_TX_SW_SUBMITTED)
			tx->ops->unmap(tx->ctx, slot->dma, slot->length);
		ob_tx_sw_slot_clear(slot);
		tx->tail = ob_tx_sw_next(tx, tx->tail);
		tx->pending--;
		tx->ops->complete(tx->ctx, packet, -ECANCELED);
	}
	return 0;
}

ob_tx_sw_u16 ob_tx_sw_pending(const struct ob_tx_sw *tx)
{
	return ob_tx_sw_valid(tx) ? tx->pending : 0;
}

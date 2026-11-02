/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef OB_TX_SW_H
#define OB_TX_SW_H

/*
 * Hardware-independent single-queue TX ownership model.
 * This module does not define or write D11 descriptors/registers. The caller
 * serializes all operations; backend completion must be deferred until submit
 * has returned. A successful submit transfers packet ownership to this queue.
 */
#ifdef __KERNEL__
#include <linux/types.h>
typedef u8 ob_tx_sw_u8;
typedef u16 ob_tx_sw_u16;
typedef u32 ob_tx_sw_u32;
typedef u64 ob_tx_sw_u64;
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t ob_tx_sw_u8;
typedef uint16_t ob_tx_sw_u16;
typedef uint32_t ob_tx_sw_u32;
typedef uint64_t ob_tx_sw_u64;
#endif

struct ob_tx_sw_ticket {
	ob_tx_sw_u16 slot;
	ob_tx_sw_u32 generation;
};

enum ob_tx_sw_slot_state {
	OB_TX_SW_FREE = 0,
	OB_TX_SW_SUBMITTING,
	OB_TX_SW_SUBMITTED,
	/* Software model: mapping reclaimed; packet awaits TX-status handling.
	 * This state does not assert a separate asynchronous hardware event.
	 */
	OB_TX_SW_DMA_RECLAIMED,
};

struct ob_tx_sw_slot {
	void *packet;
	ob_tx_sw_u64 dma;
	ob_tx_sw_u32 length;
	ob_tx_sw_u32 generation;
	/* Optional binding to the shared physical software ring, owned by the
	 * higher-level path. UINT16_MAX is not used as a valid slot index.
	 */
	ob_tx_sw_u16 ring_slot;
	ob_tx_sw_u32 ring_generation;
	bool ring_bound;
	ob_tx_sw_u16 frame_id;
	bool frame_id_valid;
	enum ob_tx_sw_slot_state state;
};

/*
 * map(): on error, guarantees no mapping was acquired.
 * submit(): on error, guarantees the backend did not accept or retain packet
 *           or DMA ownership. It must not synchronously call completion.
 * cancel(): success means backend is quiescent for this ticket; failure means
 *           the slot and mapping remain live and owned by the queue.
 * complete(): called after DMA unmap/reclaim for a completed/cancelled
 *             accepted packet; the callback now owns packet disposal/status
 *             reporting. DMA reclaim may precede wireless TX-status handling.
 */
struct ob_tx_sw_ops {
	int (*map)(void *ctx, void *packet, ob_tx_sw_u32 length,
		   ob_tx_sw_u64 *dma);
	void (*unmap)(void *ctx, ob_tx_sw_u64 dma, ob_tx_sw_u32 length);
	int (*submit)(void *ctx, void *packet, ob_tx_sw_u64 dma,
		      ob_tx_sw_u32 length,
		      const struct ob_tx_sw_ticket *ticket);
	int (*cancel)(void *ctx, const struct ob_tx_sw_ticket *ticket);
	void (*complete)(void *ctx, void *packet, int status);
};

struct ob_tx_sw {
	struct ob_tx_sw_slot *slots;
	const struct ob_tx_sw_ops *ops;
	void *ctx;
	ob_tx_sw_u16 capacity;
	ob_tx_sw_u16 head;
	ob_tx_sw_u16 tail;
	ob_tx_sw_u16 pending;
	ob_tx_sw_u8 dma_addr_bits;
	ob_tx_sw_u32 max_length;
	bool accepting;
};

int ob_tx_sw_init(struct ob_tx_sw *tx, struct ob_tx_sw_slot *slots,
		  ob_tx_sw_u16 capacity, ob_tx_sw_u8 dma_addr_bits,
		  ob_tx_sw_u32 max_length, const struct ob_tx_sw_ops *ops,
		  void *ctx);
int ob_tx_sw_start(struct ob_tx_sw *tx);
int ob_tx_sw_enqueue(struct ob_tx_sw *tx, void *packet,
		     ob_tx_sw_u32 length, struct ob_tx_sw_ticket *ticket);
/* Store the historical 16-bit TX-header frame ID alongside the queue ticket.
 * The frame ID is reused and must never replace ticket identity.
 */
int ob_tx_sw_enqueue_identified(struct ob_tx_sw *tx, void *packet,
				ob_tx_sw_u32 length,
				ob_tx_sw_u16 frame_id,
				struct ob_tx_sw_ticket *ticket);
/* One-stage synthetic completion: unmap (if still mapped), then hand off. */
int ob_tx_sw_complete(struct ob_tx_sw *tx,
		      const struct ob_tx_sw_ticket *ticket, int status);
/* Complete only the supplied oldest generation ticket. A frame-ID mismatch
 * completes that packet with -EBADMSG (matching vendor drop behavior) and
 * returns -EBADMSG; it never searches another outstanding packet.
 */
int ob_tx_sw_complete_frame_id(struct ob_tx_sw *tx,
			       const struct ob_tx_sw_ticket *ticket,
			       ob_tx_sw_u16 reported_frame_id);
int ob_tx_sw_stop(struct ob_tx_sw *tx);
ob_tx_sw_u16 ob_tx_sw_pending(const struct ob_tx_sw *tx);
bool ob_tx_sw_oldest_ticket(const struct ob_tx_sw *tx,
			    struct ob_tx_sw_ticket *ticket);
/* Model DMA mapping reclamation separately from packet disposition. The
 * pinned vendor path calls queue reclaim while processing TX status; this
 * API does not assert an independent earlier hardware completion event.
 * Caller must bind this ticket to the exact packet returned by its backend.
 */
int ob_tx_sw_dma_reclaim(struct ob_tx_sw *tx,
			 const struct ob_tx_sw_ticket *ticket);
/* Complete a previously reclaimed packet after checking its wire frame ID. */
int ob_tx_sw_complete_reclaimed_frame_id(
	struct ob_tx_sw *tx, const struct ob_tx_sw_ticket *ticket,
	ob_tx_sw_u16 reported_frame_id);
/* Resolve a backend-returned packet against the oldest accepted software
 * packet, then reclaim its DMA mapping and return its generation ticket.
 * This models one serialized queue only; the caller must already have
 * selected the correct hardware queue. A packet identity mismatch is
 * fail-stop and leaves the mapping and packet ownership unchanged.
 */
int ob_tx_sw_dma_reclaim_packet(struct ob_tx_sw *tx, void *returned_packet,
				struct ob_tx_sw_ticket *ticket);

#endif /* OB_TX_SW_H */

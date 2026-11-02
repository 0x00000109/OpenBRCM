// SPDX-License-Identifier: GPL-2.0-only
/* End-to-end mac80211 software TX path; fake DMA/D11 backend only. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_dma.h"
#include "../../src/ob_tx_path.h"

static unsigned int failures;

#define CHECK(name, expr) do { \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

struct fake_backend {
	int map_error;
	int prepare_error;
	int submit_error;
	int cancel_error;
	bool same_dma;
	uint64_t next_dma;
	unsigned int maps;
	unsigned int unmaps;
	unsigned int submits;
	unsigned int cancels;
	unsigned int completions;
	unsigned int queue_stops;
	unsigned int queue_wakes;
	unsigned int prepare_saw_reserved;
	unsigned int submit_saw_submitted;
	unsigned int complete_saw_slot_released;
	unsigned int publications;
	unsigned int publication_saw_descriptor;
	unsigned int publication_saw_submitted;
	unsigned int last_queue;
	int last_completion;
	u32 last_register;
	u32 last_ptr;
	void *last_packet;
	struct ob_tx_sw_ticket last_ticket;
	uint16_t last_ring_slot;
	struct ob_dma_desc descriptor;
	struct ob_dma_ring *ring;
};

static void fake_write32(void *ctx, u32 reg, u32 value)
{
	struct fake_backend *f = ctx;

	f->publications++;
	f->last_register = reg;
	f->last_ptr = value;
	if (f->ring && f->ring->head < f->ring->n) {
		u16 posted = f->ring->head == 0 ? f->ring->n - 1 :
			(u16)(f->ring->head - 1);

		if (ob_dma_desc_ctrl1(ob_dma_desc_at(f->ring, posted)) != 0)
			f->publication_saw_descriptor++;
		if (f->ring->slot[posted].tx_state == OB_DMA_TX_SLOT_SUBMITTED)
			f->publication_saw_submitted++;
	}
}

static const struct ob_dma64_tx_publish_ops fake_publish_ops = {
	.write32 = fake_write32,
};

static int fake_map(void *ctx, unsigned int queue, void *packet,
		    uint32_t length, uint64_t *dma)
{
	struct fake_backend *f = ctx;
	(void)packet;
	(void)length;
	f->maps++;
	f->last_queue = queue;
	if (f->map_error)
		return f->map_error;
	*dma = f->next_dma;
	if (!f->same_dma)
		f->next_dma += 0x1000;
	return 0;
}

static void fake_unmap(void *ctx, unsigned int queue, uint64_t dma,
		       uint32_t length)
{
	struct fake_backend *f = ctx;
	(void)dma;
	(void)length;
	f->unmaps++;
	f->last_queue = queue;
}

static int fake_submit(void *ctx, unsigned int queue, void *packet,
		       uint64_t dma, uint32_t length,
		       const struct ob_tx_sw_ticket *ticket,
		       uint16_t ring_index)
{
	struct fake_backend *f = ctx;

	f->submits++;
	f->last_queue = queue;
	f->last_packet = packet;
	f->last_ticket = *ticket;
	f->last_ring_slot = ring_index;
	if (f->ring && f->ring->slot[ring_index].tx_state ==
		OB_DMA_TX_SLOT_SUBMITTED)
		f->submit_saw_submitted++;
	if (f->submit_error)
		return f->submit_error;
	(void)dma;
	(void)length;
	/* Explicit test FIFO. The logical mac80211 queue is not used as a FIFO
	 * selector. This invokes the common pointer publisher with a fake register.
	 */
	return ob_dma64_tx_publish_producer(f->ring, 3, f->ring->head,
					     &fake_publish_ops, f);
}

static int fake_prepare(void *ctx, unsigned int queue, void *packet,
			uint64_t dma, uint32_t length,
			const struct ob_tx_sw_ticket *ticket,
			uint16_t ring_index)
{
	struct fake_backend *f = ctx;
	struct ob_dma_desc *desc;
	if (f->ring && ring_index < f->ring->n &&
	    f->ring->slot[ring_index].tx_state == OB_DMA_TX_SLOT_RESERVED &&
	    f->ring->slot[ring_index].tx_queue == queue)
		f->prepare_saw_reserved++;
	(void)packet;
	(void)ticket;
	/* Exercise the existing instruction-derived encoder with explicit inputs.
	 * The vector checks software encoding only; no hardware FIFO is implied.
	 */
	desc = ob_dma_desc_at(f->ring, ring_index);
	if (!desc)
		return -EINVAL;
	if (ob_dma64_tx_desc_prepare_single(f->ring, ring_index,
					    (dma_addr_t)dma, length, 0,
					    OB_DMA_PCIE_H32, false))
		return -EINVAL;
	f->descriptor = *desc;
	return f->prepare_error;
}

static void fake_queue_stop(void *ctx, unsigned int queue)
{
	struct fake_backend *f = ctx;
	f->queue_stops++;
	f->last_queue = queue;
}

static void fake_queue_wake(void *ctx, unsigned int queue)
{
	struct fake_backend *f = ctx;
	f->queue_wakes++;
	f->last_queue = queue;
}

static int fake_cancel(void *ctx, unsigned int queue,
		       const struct ob_tx_sw_ticket *ticket)
{
	struct fake_backend *f = ctx;
	f->cancels++;
	f->last_queue = queue;
	f->last_ticket = *ticket;
	return f->cancel_error;
}

static void fake_complete(void *ctx, unsigned int queue, void *packet,
			  int status)
{
	struct fake_backend *f = ctx;
	(void)packet;
	f->completions++;
	f->last_queue = queue;
	f->last_completion = status;
	if (f->ring && f->last_ring_slot < f->ring->n &&
	    f->ring->slot[f->last_ring_slot].tx_state == OB_DMA_TX_SLOT_FREE)
		f->complete_saw_slot_released++;
}

static const struct ob_tx_path_ops fake_ops = {
	.map = fake_map,
	.unmap = fake_unmap,
	.prepare = fake_prepare,
	.submit = fake_submit,
	.cancel = fake_cancel,
	.complete = fake_complete,
	.queue_stop = fake_queue_stop,
	.queue_wake = fake_queue_wake,
};

static void init_fake_ring(struct ob_dma_ring *ring,
			   struct ob_dma_desc *descs,
			   struct ob_dma_slot *slots)
{
	unsigned int i;
	memset(ring, 0, sizeof(*ring));
	memset(descs, 0, sizeof(*descs) * OB_DMA_RING_DESC_COUNT_TX);
	memset(slots, 0, sizeof(*slots) * OB_DMA_RING_DESC_COUNT_TX);
	ring->desc_cpu = descs;
	ring->desc_dma = 0x00120000u;
	ring->n = OB_DMA_RING_DESC_COUNT_TX;
	ring->slot = slots;
	ring->role = OB_DMA_RING_TX;
	ring->allocated = true;
	for (i = 0; i < ring->n; i++)
		ob_dma_slot_init(&ring->slot[i]);
}

static void test_header_validation(void)
{
	uint32_t length = 0;

	CHECK("management Order=0 needs 24 bytes",
	      ob_tx_frame_header_length(0x0000, 24, &length) == 0 &&
	      length == 24);
	CHECK("management Order=0 at 23 bytes rejected",
	      ob_tx_frame_header_length(0x0000, 23, &length) == -EMSGSIZE &&
	      length == 24);
	CHECK("management Order=1 needs 28 bytes at 24",
	      ob_tx_frame_header_length(0x8000, 24, &length) == -EMSGSIZE &&
	      length == 28);
	CHECK("management Order=1 needs 28 bytes at 27",
	      ob_tx_frame_header_length(0x8000, 27, &length) == -EMSGSIZE &&
	      length == 28);
	CHECK("management Order=1 accepts 28",
	      ob_tx_frame_header_length(0x8000, 28, &length) == 0 &&
	      length == 28);
	CHECK("management Order=1 accepts 29",
	      ob_tx_frame_header_length(0x8000, 29, &length) == 0 &&
	      length == 28);
	CHECK("ordinary data header", ob_tx_frame_header_length(0x0008, 24,
			&length) == 0 && length == 24);
	CHECK("non-QoS Order data has no HT Control field",
	      ob_tx_frame_header_length(0x8008, 24, &length) == 0 &&
	      length == 24);
	CHECK("QoS Order data needs 30 bytes at 29",
	      ob_tx_frame_header_length(0x8088, 29, &length) == -EMSGSIZE &&
	      length == 30);
	CHECK("QoS Order data accepts 30",
	      ob_tx_frame_header_length(0x8088, 30, &length) == 0 &&
	      length == 30);
	CHECK("QoS Address4 Order data needs 36 bytes at 35",
	      ob_tx_frame_header_length(0x8388, 35, &length) == -EMSGSIZE &&
	      length == 36);
	CHECK("QoS Address4 Order data accepts 36",
	      ob_tx_frame_header_length(0x8388, 36, &length) == 0 &&
	      length == 36);
	CHECK("truncated ACK rejected", ob_tx_frame_header_length(0x00d4, 9,
			&length) == -EMSGSIZE && length == 10);
	CHECK("CTS control header", ob_tx_frame_header_length(0x00c4, 10,
			&length) == 0 && length == 10);
	CHECK("four-address QoS data header", ob_tx_frame_header_length(
		0x8388, 36, &length) == 0 && length == 36);
	CHECK("ACK control header", ob_tx_frame_header_length(0x00d4, 10,
			&length) == 0 && length == 10);
	CHECK("RTS control header", ob_tx_frame_header_length(0x00b4, 16,
			&length) == 0 && length == 16);
	CHECK("reserved protocol version rejected",
	      ob_tx_frame_header_length(0x0009, 24, &length) == -EINVAL);
	CHECK("reserved frame type rejected",
	      ob_tx_frame_header_length(0x000c, 24, &length) == -EINVAL);
	CHECK("truncated frame rejected",
	      ob_tx_frame_header_length(0x0008, 23, &length) == -EMSGSIZE);
	CHECK("null result rejected",
	      ob_tx_frame_header_length(0x0008, 24, NULL) == -EINVAL);
}

static void test_rejected_header_has_no_side_effects(void)
{
	struct ob_tx_sw_slot slots[OB_TX_PATH_QUEUE_COUNT * 2];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0x5000 };
	struct ob_tx_sw_ticket ticket = { 0x1111, 0x2222 };
	struct ob_tx_sw_ticket first, second_ticket, duplicate;
	uint8_t frame[28] = { 0x00, 0x80 };	/* management, Order=1 */
	uint8_t second[28] = { 0x00, 0x80 };

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("side-effect path init", ob_tx_path_init(&path, slots, 2, 32,
			4096, &ring, &fake_ops, &fake) == 0);
	CHECK("valid Management Order=1 does not bypass closed TX gate",
	      ob_tx_path_enqueue(&path, frame, 0x8000, 28, 0, &ticket) ==
	      -ESHUTDOWN && fake.maps == 0);
	CHECK("side-effect path start", ob_tx_path_start(&path) == 0);
	CHECK("oversize frame rejected before header mapping",
	      ob_tx_path_enqueue(&path, frame, 0x8000, 4097, 0, &ticket) ==
	      -EINVAL);
	CHECK("truncated Management Order=1 rejected",
	      ob_tx_path_enqueue(&path, frame, 0x8000, 24, 0, &ticket) ==
	      -EMSGSIZE);
	CHECK("rejected frame caused no map, submit or packet handoff",
	      fake.maps == 0 && fake.unmaps == 0 && fake.submits == 0 &&
	      fake.last_packet == NULL && ring.head == ring.tail);
	CHECK("rejected frame left queue pending count and ticket unchanged",
	      ob_tx_path_pending(&path, 0) == 0 && ticket.slot == 0x1111 &&
	      ticket.generation == 0x2222);
	CHECK("complete Management Order=1 admitted at 28",
	      ob_tx_path_enqueue(&path, frame, 0x8000, 28, 0, &first) == 0 &&
	      fake.maps == 1 && fake.submits == 1 &&
	      ob_tx_path_pending(&path, 0) == 1);
	CHECK("same packet identity cannot be enqueued twice",
	      ob_tx_path_enqueue(&path, frame, 0x8000, 28, 0, &duplicate) ==
	      -EALREADY && fake.maps == 1);
	CHECK("distinct packet identity admitted independently",
	      ob_tx_path_enqueue(&path, second, 0x8000, 28, 0, &second_ticket) ==
	      0 && fake.maps == 2 && ob_tx_path_pending(&path, 0) == 2);
	CHECK("completion consumes only the first packet identity",
	      ob_tx_path_complete(&path, 0, &first, 0) == 0 &&
	      fake.completions == 1 && fake.unmaps == 1 &&
	      ob_tx_path_pending(&path, 0) == 1);
	CHECK("duplicate consumption cannot consume the second packet",
	      ob_tx_path_complete(&path, 0, &first, 0) == -ESTALE &&
	      fake.completions == 1 && ob_tx_path_pending(&path, 0) == 1);
	CHECK("side-effect path stop disposes both packets once",
	      ob_tx_path_stop(&path) == 0 && fake.cancels == 1 &&
	      fake.completions == 2 && fake.unmaps == 2 &&
	      ob_tx_path_pending(&path, 0) == 0);
}

static void test_queue_submit_and_completion(void)
{
	struct ob_tx_sw_slot slots[OB_TX_PATH_QUEUE_COUNT];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0x12345678 };
	struct ob_tx_sw_ticket ticket, stale;
	uint8_t frame[24] = { 0x08, 0x00 };
	uint8_t second_frame[24] = { 0x08, 0x00 };

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("path init", ob_tx_path_init(&path, slots, 1, 32, 4096,
						   &ring, &fake_ops, &fake) == 0);
	CHECK("valid packet on closed production-like queue fails closed",
	      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), 2,
				 &ticket) == -ESHUTDOWN);
	CHECK("closed queue does not map or submit",
	      fake.maps == 0 && fake.submits == 0);
	CHECK("path start", ob_tx_path_start(&path) == 0);
	CHECK("enqueue selected logical AC queue with vendor frame ID",
	      ob_tx_path_enqueue_identified(&path, frame, 0x0008, sizeof(frame),
					    2, 0x58a1, &ticket) == 0);
	CHECK("logical queue mapping preserved", fake.last_queue == 2);
	CHECK("packet identity retained", fake.last_packet == frame);
	CHECK("one map and submit", fake.maps == 1 && fake.submits == 1);
	CHECK("slot transitions RESERVED then PREPARED before submit",
	      fake.prepare_saw_reserved == 1 && fake.submit_saw_submitted == 1);
	CHECK("descriptor control word", ob_dma_desc_ctrl1(&fake.descriptor) ==
	      (OB_DMA_CTRL1_SOF | OB_DMA_CTRL1_EOF | OB_DMA_CTRL1_IOC));
	CHECK("descriptor length", ob_dma_desc_len(&fake.descriptor) == 24);
	CHECK("descriptor data address", ob_dma_desc_addrlow(&fake.descriptor) ==
	      0x12345678u);
	CHECK("descriptor high word", ob_dma_desc_addrhigh(&fake.descriptor) ==
	      OB_DMA_PCIE_H32);
	CHECK("software TX ticket is bound to physical ring slot",
	      ring.slot[0].tx_state == OB_DMA_TX_SLOT_SUBMITTED &&
	      ring.slot[0].skb == (struct sk_buff *)frame && ring.slot[0].mapped &&
	      ring.slot[0].tx_queue == 2 && ring.slot[0].tx_sw_slot == ticket.slot &&
	      ring.slot[0].tx_sw_generation == ticket.generation);
	CHECK("descriptor resides in the reserved real TX ring slot",
	      ob_dma_desc_len(ob_dma_desc_at(&ring, 0)) == 24);
	CHECK("producer pointer published after descriptor preparation",
	      fake.publications == 1 && fake.publication_saw_descriptor == 1 &&
	      fake.last_register == 0x2c4 &&
	      fake.last_ptr == (u32)ring.desc_dma + OB_DMA_DESC_SIZE);
	CHECK("slot ownership is submitted before producer publication",
	      ring.slot[0].tx_state == OB_DMA_TX_SLOT_SUBMITTED &&
	      fake.publications == 1 && fake.publication_saw_submitted == 1);
	CHECK("queue stops once at capacity", fake.queue_stops == 1 &&
	      fake.last_queue == 2);
	CHECK("no early packet disposition", fake.completions == 0);
	CHECK("ring capacity enforced per AC",
	      ob_tx_path_enqueue(&path, second_frame, 0x0008, sizeof(second_frame), 2,
				 &stale) == -ENOSPC);
	CHECK("full ring did not map second packet", fake.maps == 1);
	CHECK("fake DMA reclaim matches the returned packet",
	      ob_tx_path_dma_reclaim_packet(&path, 2, frame, &stale) == 0);
	CHECK("DMA mapping reclaimed exactly once", fake.unmaps == 1);
	CHECK("reclaim alone leaves packet owned pending status",
	      fake.completions == 0 && ob_tx_path_pending(&path, 2) == 1);
	CHECK("duplicate reclaim rejected",
	      ob_tx_path_dma_reclaim_packet(&path, 2, frame, &ticket) == -EALREADY);
	CHECK("matching status disposes packet once",
	      ob_tx_path_complete_reclaimed_frame_id(&path, 2, &stale,
						     0x58a1) == 0);
	CHECK("synthetic DMA/status completion was not an ACK report",
	      fake.completions == 1 && fake.last_completion == 0);
	CHECK("duplicate completion rejected",
	      ob_tx_path_complete_reclaimed_frame_id(&path, 2, &stale,
						     0x58a1) == -ENOENT);
	CHECK("queue drains", ob_tx_path_pending(&path, 2) == 0);
	CHECK("completion reclaims the physical ring slot and wakes queue",
	      ring.slot[0].tx_state == OB_DMA_TX_SLOT_FREE &&
	      ring.tail == ring.head && fake.queue_wakes == 1 &&
	      fake.complete_saw_slot_released == 1);
	CHECK("stop empty path", ob_tx_path_stop(&path) == 0);
}

static void test_failures_and_teardown(void)
{
	struct ob_tx_sw_slot slots[2 * 4];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0x4000 };
	struct ob_tx_sw_ticket ticket;
	uint8_t frame[24] = { 0x08, 0x00 };

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("failure path init", ob_tx_path_init(&path, slots, 2, 32, 4096,
						  &ring, &fake_ops, &fake) == 0);
	CHECK("failure path start", ob_tx_path_start(&path) == 0);
	fake.map_error = -EIO;
	CHECK("map failure returned to caller",
	      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), 0,
				 &ticket) == -EIO);
	CHECK("map failure acquired no ownership",
	      fake.maps == 1 && fake.unmaps == 0 && fake.submits == 0 &&
	      ob_tx_path_pending(&path, 0) == 0 && ring.head == ring.tail);
	fake.map_error = 0;
	fake.prepare_error = -ERANGE;
	CHECK("descriptor preparation failure unwinds mapping",
	      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), 1,
				 &ticket) == -ERANGE);
	CHECK("descriptor preparation failure never submits or completes",
	      fake.maps == 2 && fake.unmaps == 1 && fake.submits == 0 &&
	      fake.completions == 0 && ob_tx_path_pending(&path, 1) == 0);
	fake.prepare_error = 0;
	fake.submit_error = -EIO;
	CHECK("submit failure unwinds map",
	      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), 1,
				 &ticket) == -EIO);
	CHECK("submit failure does not complete packet",
	      fake.maps == 3 && fake.unmaps == 2 && fake.completions == 0 &&
	      ob_tx_path_pending(&path, 1) == 0);
	CHECK("failed preparation and submit leave ring slots reusable",
	      ring.head == ring.tail &&
	      ring.slot[0].tx_state == OB_DMA_TX_SLOT_FREE);
	CHECK("failed preparation clears unpublished descriptor bytes",
	      ob_dma_desc_ctrl1(ob_dma_desc_at(&ring, 0)) == 0 &&
	      ob_dma_desc_len(ob_dma_desc_at(&ring, 0)) == 0);
	CHECK("failed submit did not publish producer pointer",
	      fake.publications == 0);
	fake.submit_error = 0;
	CHECK("accepted packet on independent AC",
	      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), 1,
				 &ticket) == 0);
	fake.cancel_error = -EBUSY;
	CHECK("teardown timeout retains mapping and skb",
	      ob_tx_path_stop(&path) == -EBUSY);
	CHECK("no release on failed cancellation",
	      fake.unmaps == 2 && fake.completions == 0 &&
	      ob_tx_path_pending(&path, 1) == 1);
	fake.cancel_error = 0;
	CHECK("confirmed fake quiescence permits teardown",
	      ob_tx_path_stop(&path) == 0);
	CHECK("teardown releases exactly once",
	      fake.unmaps == 3 && fake.completions == 1 &&
	      fake.last_completion == -ECANCELED &&
	      ob_tx_path_pending(&path, 1) == 0);
	CHECK("stopped path rejects later packet",
	      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), 1,
				 &ticket) == -ESHUTDOWN);
}

static void test_bad_input_and_queue_isolation(void)
{
	struct ob_tx_sw_slot slots[OB_TX_PATH_QUEUE_COUNT];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0x9000 };
	struct ob_tx_sw_ticket ticket;
	uint8_t frame[24] = { 0x08, 0x00 };
	unsigned int q;

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("isolation init", ob_tx_path_init(&path, slots, 1, 32, 4096,
						 &ring, &fake_ops, &fake) == 0);
	CHECK("isolation start", ob_tx_path_start(&path) == 0);
	CHECK("invalid queue rejected before DMA",
	      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), 4,
				 &ticket) == -EINVAL);
	CHECK("malformed frame rejected before DMA",
	      ob_tx_path_enqueue(&path, frame, 0x0009, sizeof(frame), 0,
				 &ticket) == -EINVAL);
	CHECK("bad input caused no mapping", fake.maps == 0);
	for (q = 0; q < OB_TX_PATH_QUEUE_COUNT; q++) {
		CHECK("each AC queue independently accepts one packet",
		      ob_tx_path_enqueue(&path, frame, 0x0008, sizeof(frame), q,
					 &ticket) == 0);
	}
	CHECK("all four software AC queues represented", fake.submits == 4);
	for (q = 0; q < OB_TX_PATH_QUEUE_COUNT; q++) {
		CHECK("queue-specific pending count", ob_tx_path_pending(&path, q) == 1);
	}
	CHECK("drain queue set", ob_tx_path_stop(&path) == 0);
	CHECK("all software-owned packets disposed once",
	      fake.completions == 4 && fake.unmaps == 4);
}

static void test_ring_wrap_and_shutdown_wake(void)
{
	struct ob_tx_sw_slot slots[OB_TX_PATH_QUEUE_COUNT * 2];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0x1000 };
	struct ob_tx_sw_ticket tickets[4];
	struct ob_tx_sw_ticket stale;
	uint8_t frame[4][24] = { { 0x08, 0 }, { 0x08, 0 },
				  { 0x08, 0 }, { 0x08, 0 } };
	unsigned int i;

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("wrap init", ob_tx_path_init(&path, slots, 2, 32, 4096,
						 &ring, &fake_ops, &fake) == 0);
	CHECK("wrap start", ob_tx_path_start(&path) == 0);
	for (i = 0; i < 4; i++)
		CHECK("four logical queues submit to shared ring",
		      ob_tx_path_enqueue(&path, frame[i], 8, sizeof(frame[i]), i,
					 &tickets[i]) == 0);
	CHECK("shared ring producer advanced", ring.head == 4 && ring.tail == 0);
	CHECK("cross-queue completion may arrive out of order",
	      ob_tx_path_complete(&path, 3, &tickets[3], -EIO) == 0);
	CHECK("consumer waits at the oldest outstanding ring slot",
	      ring.tail == 0 && ring.slot[3].tx_state == OB_DMA_TX_SLOT_FREE);
	for (i = 0; i < 3; i++)
		CHECK("completion frees oldest remaining reserved slots",
		      ob_tx_path_complete(&path, i, &tickets[i], -EIO) == 0);
	CHECK("ring consumer catches producer after drain",
	      ring.head == ring.tail && ring.tail == 4);
	stale = tickets[0];
	for (i = 0; i < 4; i++)
		CHECK("wrapped submissions accepted",
		      ob_tx_path_enqueue(&path, frame[i], 8, sizeof(frame[i]), i,
					 &tickets[i]) == 0);
	CHECK("ring wraps to first physical indices", ring.head == 8 &&
	      ring.slot[4].tx_state == OB_DMA_TX_SLOT_SUBMITTED &&
	      ring.slot[7].tx_state == OB_DMA_TX_SLOT_SUBMITTED);
	CHECK("old generation cannot complete reused software slot",
	      ob_tx_path_complete(&path, 0, &stale, 0) == -ESTALE);
	CHECK("shutdown stops queue wake callbacks", ob_tx_path_stop(&path) == 0);
	CHECK("no queue wake after shutdown", fake.queue_wakes == 0);
}

static void test_shared_ring_capacity_and_backpressure(void)
{
	struct ob_tx_sw_slot sw_slots[OB_TX_PATH_QUEUE_COUNT * 128];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0x100000 };
	struct ob_tx_sw_ticket tickets[OB_DMA_RING_DESC_COUNT_TX - 1];
	uintptr_t packet_ids[OB_DMA_RING_DESC_COUNT_TX];
	unsigned int i;

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("shared capacity init", ob_tx_path_init(&path, sw_slots, 128,
			32, 4096, &ring, &fake_ops, &fake) == 0);
	CHECK("shared capacity start", ob_tx_path_start(&path) == 0);
	for (i = 0; i < OB_DMA_RING_DESC_COUNT_TX - 1; i++) {
		packet_ids[i] = i + 1;
		CHECK("reserve every usable real ring slot",
		      ob_tx_path_enqueue(&path, &packet_ids[i], 8, 24, i % 4,
					 &tickets[i]) == 0);
	}
	CHECK("ring reaches one-slot-empty full boundary",
	      ring.head == OB_DMA_RING_DESC_COUNT_TX - 1 && ring.tail == 0 &&
	      ob_dma_full(ring.n, ring.head, ring.tail));
	packet_ids[OB_DMA_RING_DESC_COUNT_TX - 1] = OB_DMA_RING_DESC_COUNT_TX;
	CHECK("shared physical ring rejects overwrite",
	      ob_tx_path_enqueue(&path,
				 &packet_ids[OB_DMA_RING_DESC_COUNT_TX - 1],
				 8, 24, 3, &tickets[0]) == -ENOSPC);
	CHECK("full ring rollback unmaps only rejected packet",
	      fake.maps == OB_DMA_RING_DESC_COUNT_TX && fake.unmaps == 1 &&
	      fake.submits == OB_DMA_RING_DESC_COUNT_TX - 1 &&
	      ring.head == OB_DMA_RING_DESC_COUNT_TX - 1 && ring.tail == 0);
	CHECK("each logical queue stopped once at capacity pressure",
	      fake.queue_stops == 4);
	for (i = 0; i < OB_DMA_RING_DESC_COUNT_TX - 1; i++)
		CHECK("reclaim packet in each logical queue order",
		      ob_tx_path_complete(&path, i % 4, &tickets[i], -EIO) == 0);
	CHECK("physical consumer catches producer after completions",
	      ring.tail == ring.head && !ob_dma_full(ring.n, ring.head, ring.tail));
	CHECK("stopped queues wake after capacity returns",
	      fake.queue_wakes == 4);
	CHECK("all submitted mappings and packets reach one terminal state",
	      fake.unmaps == OB_DMA_RING_DESC_COUNT_TX &&
	      fake.completions == OB_DMA_RING_DESC_COUNT_TX - 1);
	CHECK("stop after drain succeeds", ob_tx_path_stop(&path) == 0);
}

static void test_shutdown_cancels_without_queue_wake(void)
{
	struct ob_tx_sw_slot slots[OB_TX_PATH_QUEUE_COUNT];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0x8000 };
	struct ob_tx_sw_ticket ticket;
	uint8_t frame[24] = { 0x08, 0 };

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("shutdown queue init", ob_tx_path_init(&path, slots, 1, 32, 4096,
			&ring, &fake_ops, &fake) == 0);
	CHECK("shutdown queue start", ob_tx_path_start(&path) == 0);
	CHECK("capacity causes one stop", ob_tx_path_enqueue(&path, frame, 8,
		24, 0, &ticket) == 0 && fake.queue_stops == 1);
	CHECK("shutdown cancels outstanding software submission",
	      ob_tx_path_stop(&path) == 0 && fake.cancels == 1 &&
	      fake.completions == 1 && fake.last_completion == -ECANCELED);
	CHECK("shutdown does not wake stopped mac80211 queue",
	      fake.queue_wakes == 0 && ring.head == ring.tail);
	CHECK("restart reconciles the stopped queue after software quiescence",
	      ob_tx_path_start(&path) == 0 && fake.queue_wakes == 1);
	CHECK("second stop remains idempotent", ob_tx_path_stop(&path) == 0);
}

static void test_failure_with_reused_dma_address(bool fail_submit)
{
	struct ob_tx_sw_slot slots[OB_TX_PATH_QUEUE_COUNT * 2];
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot ring_slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_tx_path path;
	struct fake_backend fake = { .next_dma = 0xa000, .same_dma = true };
	struct ob_tx_sw_ticket first, failed;
	uint8_t first_packet[24] = { 0x08, 0 };
	uint8_t failed_packet[24] = { 0x08, 0 };

	init_fake_ring(&ring, descs, ring_slots);
	fake.ring = &ring;
	CHECK("same DMA address test init", ob_tx_path_init(&path, slots, 2,
		32, 4096, &ring, &fake_ops, &fake) == 0);
	CHECK("same DMA address test start", ob_tx_path_start(&path) == 0);
	CHECK("first same-address mapping submits",
	      ob_tx_path_enqueue(&path, first_packet, 8, 24, 0, &first) == 0);
	if (fail_submit)
		fake.submit_error = -EIO;
	else
		fake.prepare_error = -EIO;
	CHECK("second same-address mapping failure rolls back",
	      ob_tx_path_enqueue(&path, failed_packet, 8, 24, 0, &failed) == -EIO);
	CHECK("rollback releases only failed physical slot",
	      ring.slot[0].tx_state == OB_DMA_TX_SLOT_SUBMITTED &&
	      ring.slot[0].mapped && ring.slot[0].skb ==
		(struct sk_buff *)first_packet &&
	      ring.slot[1].tx_state == OB_DMA_TX_SLOT_FREE &&
	      ring.slot[1].skb == NULL && !ring.slot[1].mapped &&
	      ob_tx_path_pending(&path, 0) == 1);
	CHECK("failed map lifetime unmapped once before completion",
	      fake.maps == 2 && fake.unmaps == 1 && fake.completions == 0);
	fake.prepare_error = 0;
	fake.submit_error = 0;
	CHECK("original same-address packet completes independently",
	      ob_tx_path_complete(&path, 0, &first, -EIO) == 0);
	CHECK("both mappings have one unmap and packet has one disposition",
	      fake.unmaps == 2 && fake.completions == 1 &&
	      ring.head == ring.tail);
	CHECK("same-address test stops cleanly", ob_tx_path_stop(&path) == 0);
}

static void test_failed_submit_with_reused_dma_address(void)
{
	test_failure_with_reused_dma_address(false);
	test_failure_with_reused_dma_address(true);
}

static void test_dma64_descriptor_and_publication_contract(void)
{
	struct ob_dma_ring ring;
	struct ob_dma_desc descs[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma_slot slots[OB_DMA_RING_DESC_COUNT_TX];
	struct ob_dma64_tx_publish_ops publish_ops = { .write32 = fake_write32 };
	struct fake_backend fake = { 0 };
	uint8_t packet_object;
	unsigned int i;
	u32 base = 0;

	init_fake_ring(&ring, descs, slots);
	ring.desc_dma = 0x00200000u;
	fake.ring = &ring;

	CHECK("rev42 FIFO0 base", ob_dma64_tx_fifo_base(0, &base) == 0 &&
	      base == 0x200);
	CHECK("rev42 FIFO1 base", ob_dma64_tx_fifo_base(1, &base) == 0 &&
	      base == 0x240);
	CHECK("rev42 FIFO2 base", ob_dma64_tx_fifo_base(2, &base) == 0 &&
	      base == 0x280);
	CHECK("rev42 FIFO3 base", ob_dma64_tx_fifo_base(3, &base) == 0 &&
	      base == 0x2c0);
	CHECK("invalid FIFO fails closed",
	      ob_dma64_tx_fifo_base(4, &base) == -EINVAL);

	/* Slot 511 is a legal physical slot after wrap when the consumer allows
	 * it. This golden vector is derived independently from the observed
	 * sub_fafd flags and sub_dcf8 word stores.
	 */
	for (i = 1; i < OB_DMA_RING_DESC_COUNT_TX - 1; i++) {
		slots[i].skb = (struct sk_buff *)&packet_object;
		slots[i].mapped = true;
		slots[i].tx_state = OB_DMA_TX_SLOT_SUBMITTED;
	}
	slots[OB_DMA_RING_DESC_COUNT_TX - 1].skb =
		(struct sk_buff *)&packet_object;
	slots[OB_DMA_RING_DESC_COUNT_TX - 1].dma = 0x12345678u;
	slots[OB_DMA_RING_DESC_COUNT_TX - 1].mapped = true;
	slots[OB_DMA_RING_DESC_COUNT_TX - 1].tx_state = OB_DMA_TX_SLOT_RESERVED;
	ring.tail = 1;
	ring.head = 0; /* producer wrapped after reserving slot 511 */
	CHECK("last-slot descriptor prepared",
	      ob_dma64_tx_desc_prepare_single(&ring,
			OB_DMA_RING_DESC_COUNT_TX - 1, 0x12345678u,
			0x7fffu, 0, OB_DMA_PCIE_H32, false) == 0);
	CHECK("golden ctrl1 includes SOF EOF IOC EOT",
	      le32_to_cpu(descs[OB_DMA_RING_DESC_COUNT_TX - 1].ctrl1) ==
	      0xf0000000u);
	CHECK("golden ctrl2 is bounded byte count",
	      le32_to_cpu(descs[OB_DMA_RING_DESC_COUNT_TX - 1].ctrl2) ==
	      0x00007fffu);
	CHECK("golden address words retain PCIe high selector",
	      le32_to_cpu(descs[OB_DMA_RING_DESC_COUNT_TX - 1].addrlow) ==
		0x12345678u &&
	      le32_to_cpu(descs[OB_DMA_RING_DESC_COUNT_TX - 1].addrhigh) ==
		0x80000000u);
	CHECK("wrapped producer pointer uses ring base, FIFO3 PTR",
	      ob_dma64_tx_publish_producer(&ring, 3, 0, &publish_ops,
					   &fake) == 0 &&
	      fake.publications == 1 && fake.last_register == 0x2c4 &&
	      fake.last_ptr == (u32)ring.desc_dma);
	CHECK("publisher refuses noncurrent producer",
	      ob_dma64_tx_publish_producer(&ring, 3, 1, &publish_ops,
					   &fake) == -EINVAL &&
	      fake.publications == 1);
	CHECK("descriptor rejects DMA addresses outside proven low word",
	      ob_dma64_tx_desc_prepare_single(&ring,
			OB_DMA_RING_DESC_COUNT_TX - 1,
			0x100000000ULL, 64, 0, OB_DMA_PCIE_H32,
			false) == -EINVAL);
	CHECK("descriptor rejects overlength without changing golden control",
	      ob_dma64_tx_desc_prepare_single(&ring,
			OB_DMA_RING_DESC_COUNT_TX - 1,
			0x12345678u, 0x8000u, 0, OB_DMA_PCIE_H32,
			false) == -EINVAL &&
	      le32_to_cpu(descs[OB_DMA_RING_DESC_COUNT_TX - 1].ctrl1) ==
		0xf0000000u);
}

int main(void)
{
	test_header_validation();
	test_rejected_header_has_no_side_effects();
	test_queue_submit_and_completion();
	test_failures_and_teardown();
	test_bad_input_and_queue_isolation();
	test_ring_wrap_and_shutdown_wake();
	test_shared_ring_capacity_and_backpressure();
	test_shutdown_cancels_without_queue_wake();
	test_failed_submit_with_reused_dma_address();
	test_dma64_descriptor_and_publication_contract();
	if (failures) {
		printf("OpenBRCM TX pipeline: %u failures\n", failures);
		return 1;
	}
	puts("OpenBRCM TX pipeline: PASS (software/fake backend only)");
	return 0;
}

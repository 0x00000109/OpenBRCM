// SPDX-License-Identifier: GPL-2.0-only
/* End-to-end software ownership test; fake D11/mac80211 backends only. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_dpc_sw.h"
#include "../../src/ob_rx_d11_rev42.h"
#include "../../src/ob_rx_sw.h"
#include "../../src/ob_tx_sw.h"

#define CHECK(name, expr) do { \
	if (!(expr)) { \
		printf("FAIL: %s\n", name); \
		failures++; \
	} \
} while (0)

static unsigned int failures;

struct fake_tx_backend {
	int map_error;
	int submit_error;
	int cancel_error;
	uint64_t next_dma;
	unsigned int maps;
	unsigned int unmaps;
	unsigned int submits;
	unsigned int cancels;
	unsigned int released;
	int last_status;
	void *last_packet;
	struct ob_tx_sw_ticket submitted_ticket;
};

static int fake_map(void *ctx, void *packet, uint32_t len, uint64_t *dma)
{
	struct fake_tx_backend *f = ctx;
	(void)packet;
	(void)len;
	f->maps++;
	if (f->map_error)
		return f->map_error;
	*dma = f->next_dma;
	return 0;
}

static void fake_unmap(void *ctx, uint64_t dma, uint32_t len)
{
	struct fake_tx_backend *f = ctx;
	(void)dma;
	(void)len;
	f->unmaps++;
}

static int fake_submit(void *ctx, void *packet, uint64_t dma, uint32_t len,
		       const struct ob_tx_sw_ticket *ticket)
{
	struct fake_tx_backend *f = ctx;
	(void)packet;
	(void)dma;
	(void)len;
	f->submits++;
	f->last_packet = packet;
	f->submitted_ticket = *ticket;
	return f->submit_error;
}

static int fake_cancel(void *ctx, const struct ob_tx_sw_ticket *ticket)
{
	struct fake_tx_backend *f = ctx;
	f->cancels++;
	f->submitted_ticket = *ticket;
	return f->cancel_error;
}

static void fake_release(void *ctx, void *packet, int status)
{
	struct fake_tx_backend *f = ctx;
	(void)packet;
	f->released++;
	f->last_status = status;
}

static const struct ob_tx_sw_ops tx_ops = {
	.map = fake_map,
	.unmap = fake_unmap,
	.submit = fake_submit,
	.cancel = fake_cancel,
	.complete = fake_release,
};

struct rx_item {
	const uint8_t *bytes;
	size_t length;
	const uint8_t *dma_buffer;
	size_t dma_available;
	struct ob_rx_sw_metadata meta;
};

struct sink_state {
	unsigned int calls;
	const uint8_t *frame;
	size_t length;
	struct ob_rx_sw_status status;
};

static int fake_sink(void *ctx, const uint8_t *frame, size_t len,
		     const struct ob_rx_sw_status *status)
{
	struct sink_state *sink = ctx;
	sink->calls++;
	sink->frame = frame;
	sink->length = len;
	sink->status = *status;
	return 0;
}

struct pipeline {
	struct ob_tx_sw *tx;
	struct sink_state *sink;
	uint16_t tx_status_frame_id;
	unsigned int tx_events;
	unsigned int rx_events;
};

static int fake_dpc_process(void *ctx, const struct ob_dpc_sw_event *event)
{
	struct pipeline *p = ctx;

	if (event->kind == OB_DPC_SW_TX_COMPLETE) {
		void *returned_packet = (void *)(uintptr_t)event->token;
		struct ob_tx_sw_ticket ticket;
		int ret;

		/* Fake backend return joins packet identity to the oldest ticket. */
		ret = ob_tx_sw_dma_reclaim_packet(p->tx, returned_packet, &ticket);
		if (ret)
			return ret;
		p->tx_events++;
		ret = ob_tx_sw_complete_reclaimed_frame_id(
			p->tx, &ticket, p->tx_status_frame_id);
		/* A frame-ID mismatch is a completed packet drop, not a DPC
		 * backend failure. The TX callback already received -EBADMSG.
		 */
		return ret == -EBADMSG ? 0 : ret;
	}
	if (event->kind == OB_DPC_SW_RX_FRAME) {
		const struct rx_item *item = (const struct rx_item *)(uintptr_t)event->token;
		struct ob_rx_d11_rev42_input input = {
			.d11_core_revision = 42,
			.length_provenance = OB_RX_D11_LENGTH_VALIDATED,
			.payload_length = item->length,
			.complete = 1,
		};
		struct ob_rx_d11_rev42_view view;

		p->rx_events++;
		if (ob_rx_d11_rev42_payload_view(item->dma_buffer,
						 item->dma_available, &input,
						 &view))
			return -EINVAL;
		if (view.payload != item->bytes ||
		    view.payload_length != item->length)
			return -EINVAL;
		return ob_rx_sw_deliver(view.payload, view.payload_length, &item->meta,
					fake_sink, p->sink);
	}
	return -EINVAL;
}

static void prepare_rx(struct rx_item *item, uint8_t dma_buffer[68])
{
	uint8_t *frame = dma_buffer + 40;

	memset(dma_buffer, 0, 40);
	memset(frame, 0, 28);
	frame[0] = 0x80; /* management beacon frame control */
	item->bytes = frame;
	item->length = 28;
	item->dma_buffer = dma_buffer;
	item->dma_available = 68;
	memset(&item->meta, 0, sizeof(item->meta));
	item->meta.provenance = OB_RX_SW_PROVENANCE_VALIDATED;
	item->meta.valid_fields = OB_RX_SW_META_CHANNEL | OB_RX_SW_META_RATE |
		OB_RX_SW_META_SIGNAL | OB_RX_SW_META_FCS_LEN;
	item->meta.declared_len = item->length;
	item->meta.complete = 1;
	item->meta.channel = 6;
	item->meta.rate_index = 2;
	item->meta.signal_dbm = -45;
	item->meta.fcs_len = 4;
}

static void test_joined_tx_dpc_rx_flow(void)
{
	struct ob_tx_sw_slot slots[4];
	struct ob_tx_sw tx;
	struct fake_tx_backend fake = { .next_dma = 0x1000 };
	struct ob_dpc_sw_state dpc;
	struct sink_state sink = { 0 };
	struct pipeline pipeline = {
		.tx = &tx,
		.sink = &sink,
		.tx_status_frame_id = 0x58a1,
	};
	const struct ob_dpc_sw_ops dpc_ops = { .process = fake_dpc_process };
	struct ob_tx_sw_ticket ticket;
	struct ob_dpc_sw_event event;
	struct rx_item rx;
	uint8_t tx_frame[24] = { 0x08, 0x00 };
	uint8_t rx_dma_buffer[68];
	uint32_t processed = 0;

	CHECK("TX init", ob_tx_sw_init(&tx, slots, 4, 32, 4096,
				       &tx_ops, &fake) == 0);
	CHECK("TX start", ob_tx_sw_start(&tx) == 0);
	ob_dpc_sw_init(&dpc);
	prepare_rx(&rx, rx_dma_buffer);
	CHECK("fake D11 identified TX enqueue",
	      ob_tx_sw_enqueue_identified(&tx, tx_frame, 24,
					  pipeline.tx_status_frame_id, &ticket) == 0);
	event.kind = OB_DPC_SW_TX_COMPLETE;
	event.token = (uint64_t)(uintptr_t)fake.last_packet;
	CHECK("fake backend retained submitted packet identity",
	      fake.last_packet == tx_frame);
	CHECK("queue returned-packet TX status event",
	      ob_dpc_sw_enqueue(&dpc, &event) == 0);
	event.kind = OB_DPC_SW_RX_FRAME;
	event.token = (uint64_t)(uintptr_t)&rx;
	CHECK("queue RX frame event", ob_dpc_sw_enqueue(&dpc, &event) == 0);
	CHECK("bounded DPC dispatch", ob_dpc_sw_dispatch(&dpc, &dpc_ops,
			&pipeline, 8, &processed) == 0);
	CHECK("two DPC events processed", processed == 2);
	CHECK("one TX completion", pipeline.tx_events == 1);
	CHECK("one RX sink delivery", pipeline.rx_events == 1 && sink.calls == 1);
	CHECK("TX mapping released once", fake.maps == 1 && fake.unmaps == 1);
	CHECK("TX packet released once", fake.released == 1 && fake.last_status == 0);
	CHECK("submit generation ticket remains attached to same packet",
	      ticket.slot == fake.submitted_ticket.slot &&
	      ticket.generation == fake.submitted_ticket.generation);
	CHECK("RX frame/status preserved", sink.frame == rx.bytes &&
		 sink.length == rx.length && sink.status.channel == 6 &&
		 sink.status.rate_index == 2 && sink.status.signal_dbm == -45 &&
		 sink.status.fcs_len == 4);
	CHECK("TX ring empty", ob_tx_sw_pending(&tx) == 0);
	CHECK("DPC queue empty", dpc.count == 0);
	CHECK("stop after drain", ob_tx_sw_stop(&tx) == 0);
	CHECK("DPC quiesce", ob_dpc_sw_quiesce(&dpc) == 0);
	CHECK("no leaked RX callback ownership", sink.calls == 1);
}

static void test_acquisition_and_transition_failures(void)
{
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw tx;
	struct fake_tx_backend fake = { .next_dma = 0x1000 };
	struct ob_dpc_sw_state dpc;
	struct ob_tx_sw_ticket ticket;
	int packet;

	CHECK("failure TX init", ob_tx_sw_init(&tx, slots, 2, 32, 4096,
					       &tx_ops, &fake) == 0);
	CHECK("failure TX start", ob_tx_sw_start(&tx) == 0);
	ob_dpc_sw_init(&dpc);
	fake.map_error = -EIO;
	CHECK("mapping failure leaves packet caller-owned",
	      ob_tx_sw_enqueue(&tx, &packet, 100, &ticket) == -EIO);
	CHECK("mapping failure has no DPC event", dpc.count == 0);
	fake.map_error = 0;
	fake.submit_error = -EIO;
	CHECK("submission failure unwinds mapping",
	      ob_tx_sw_enqueue(&tx, &packet, 100, &ticket) == -EIO);
	CHECK("submission failure no pending packet", ob_tx_sw_pending(&tx) == 0);
	CHECK("one map unwound", fake.maps == 2 && fake.unmaps == 1);
	fake.submit_error = 0;
	CHECK("accepted TX pending", ob_tx_sw_enqueue(&tx, &packet, 100, &ticket) == 0);
	fake.cancel_error = -EBUSY;
	CHECK("stop preserves backend-owned pending packet", ob_tx_sw_stop(&tx) == -EBUSY);
	CHECK("pending mapping remains owned", ob_tx_sw_pending(&tx) == 1 &&
	      fake.unmaps == 1 && fake.released == 0);
	fake.cancel_error = 0;
	CHECK("retry stop after backend quiescence", ob_tx_sw_stop(&tx) == 0);
	CHECK("pending packet cancelled exactly once", ob_tx_sw_pending(&tx) == 0 &&
	      fake.unmaps == 2 && fake.released == 1 &&
	      fake.last_status == -ECANCELED);
	CHECK("DPC quiesce stops dispatch", ob_dpc_sw_quiesce(&dpc) == 0);
}

static void test_wrong_backend_packet_fails_closed(void)
{
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw tx;
	struct fake_tx_backend fake = { .next_dma = 0x3000 };
	struct ob_dpc_sw_state dpc;
	struct sink_state sink = { 0 };
	struct pipeline pipeline = {
		.tx = &tx,
		.sink = &sink,
		.tx_status_frame_id = 0x6001,
	};
	const struct ob_dpc_sw_ops dpc_ops = { .process = fake_dpc_process };
	struct ob_tx_sw_ticket ticket;
	struct ob_dpc_sw_event event;
	uint32_t processed = 0, cancelled = 0;
	struct ob_dpc_sw_event cancelled_event;
	uint8_t packet[24] = { 0x08, 0x00 };
	uint8_t unrelated[24] = { 0x08, 0x00 };

	CHECK("wrong-return init", ob_tx_sw_init(&tx, slots, 2, 32, 4096,
						 &tx_ops, &fake) == 0);
	CHECK("wrong-return start", ob_tx_sw_start(&tx) == 0);
	ob_dpc_sw_init(&dpc);
	CHECK("wrong-return accepted TX",
	      ob_tx_sw_enqueue_identified(&tx, packet, sizeof(packet),
					  pipeline.tx_status_frame_id,
					  &ticket) == 0);
	CHECK("wrong-return packet differs from submitted packet",
	      fake.last_packet == packet && fake.last_packet != unrelated);
	event.kind = OB_DPC_SW_TX_COMPLETE;
	event.token = (uint64_t)(uintptr_t)unrelated;
	CHECK("enqueue mismatched backend return",
	      ob_dpc_sw_enqueue(&dpc, &event) == 0);
	event.kind = OB_DPC_SW_RX_FRAME;
	event.token = 0;
	CHECK("enqueue later work behind mismatch",
	      ob_dpc_sw_enqueue(&dpc, &event) == 0);
	CHECK("mismatched return fail-stops DPC",
	      ob_dpc_sw_dispatch(&dpc, &dpc_ops, &pipeline, 8, &processed) ==
	      -ESTALE);
	CHECK("only first event attempted", processed == 1 && dpc.count == 1);
	CHECK("DPC failure latched", ob_dpc_sw_failed(&dpc));
	CHECK("mismatch does not release live DMA mapping",
	      fake.unmaps == 0 && ob_tx_sw_pending(&tx) == 1);
	CHECK("mismatch does not dispose packet", fake.released == 0);
	CHECK("later RX callback was not run", pipeline.rx_events == 0 &&
	      sink.calls == 0);
	CHECK("quiesce failed DPC", ob_dpc_sw_quiesce(&dpc) == 0);
	CHECK("return queued event during quiesce",
	      ob_dpc_sw_cancel_pending(&dpc, &cancelled_event, 1, &cancelled) == 0 &&
	      cancelled == 1 && cancelled_event.kind == OB_DPC_SW_RX_FRAME);
	CHECK("cancel backend-owned TX safely", ob_tx_sw_stop(&tx) == 0);
	CHECK("cancellation releases mapping and packet exactly once",
	      fake.unmaps == 1 && fake.released == 1 &&
	      fake.last_status == -ECANCELED && ob_tx_sw_pending(&tx) == 0);
}

static void test_tx_status_mismatch_is_packet_error(void)
{
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw tx;
	struct fake_tx_backend fake = { .next_dma = 0x4000 };
	struct ob_dpc_sw_state dpc;
	struct sink_state sink = { 0 };
	struct pipeline pipeline = {
		.tx = &tx,
		.sink = &sink,
		.tx_status_frame_id = 0x7002,
	};
	const struct ob_dpc_sw_ops dpc_ops = { .process = fake_dpc_process };
	struct ob_tx_sw_ticket ticket;
	struct ob_dpc_sw_event event;
	uint8_t packet[24] = { 0x08, 0x00 };
	struct rx_item rx;
	uint8_t rx_dma_buffer[68];
	uint32_t processed = 0;

	prepare_rx(&rx, rx_dma_buffer);

	CHECK("mismatch queue init", ob_tx_sw_init(&tx, slots, 2, 32, 4096,
						    &tx_ops, &fake) == 0);
	CHECK("mismatch queue start", ob_tx_sw_start(&tx) == 0);
	ob_dpc_sw_init(&dpc);
	CHECK("mismatch enqueue identified packet",
	      ob_tx_sw_enqueue_identified(&tx, packet, sizeof(packet), 0x7001,
					  &ticket) == 0);
	event.kind = OB_DPC_SW_TX_COMPLETE;
	event.token = (uint64_t)(uintptr_t)fake.last_packet;
	CHECK("queue mismatched TX status", ob_dpc_sw_enqueue(&dpc, &event) == 0);
	event.kind = OB_DPC_SW_RX_FRAME;
	event.token = (uint64_t)(uintptr_t)&rx;
	CHECK("queue RX after TX status", ob_dpc_sw_enqueue(&dpc, &event) == 0);
	CHECK("frame-ID mismatch remains a handled DPC event",
	      ob_dpc_sw_dispatch(&dpc, &dpc_ops, &pipeline, 8, &processed) == 0);
	CHECK("TX mismatch disposed with error status",
	      fake.released == 1 && fake.last_status == -EBADMSG);
	CHECK("mismatch does not retain packet or mapping",
	      fake.unmaps == 1 && ob_tx_sw_pending(&tx) == 0);
	CHECK("subsequent RX event still delivered",
	      processed == 2 && pipeline.rx_events == 1 && sink.calls == 1);
	CHECK("DPC remains healthy after packet-level drop",
	      !ob_dpc_sw_failed(&dpc));
	CHECK("stop after mismatch and RX", ob_tx_sw_stop(&tx) == 0);
	CHECK("quiesce after mismatch and RX", ob_dpc_sw_quiesce(&dpc) == 0);
}

int main(void)
{
	test_joined_tx_dpc_rx_flow();
	test_acquisition_and_transition_failures();
	test_wrong_backend_packet_fails_closed();
	test_tx_status_mismatch_is_packet_error();
	if (failures) {
		printf("datapath integration: %u failures\n", failures);
		return 1;
	}
	puts("datapath integration: PASS (software fake backends only)");
	return 0;
}

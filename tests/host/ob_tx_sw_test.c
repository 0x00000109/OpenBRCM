// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../../src/ob_tx_sw.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define EXPECT(name, got, want) do { \
	long _g = (long)(got), _w = (long)(want); \
	if (_g != _w) { \
		printf("FAIL %s: got %ld expected %ld\n", name, _g, _w); \
		failures++; \
	} \
} while (0)

static int failures;

struct fake_backend {
	int map_result;
	int submit_result;
	int cancel_result;
	ob_tx_sw_u64 next_dma;
	unsigned int maps;
	unsigned int unmaps;
	unsigned int submits;
	unsigned int cancels;
	unsigned int completions;
	void *last_packet;
	int last_status;
	struct ob_tx_sw_ticket last_ticket;
};

static int fake_map(void *ctx, void *packet, ob_tx_sw_u32 length,
		    ob_tx_sw_u64 *dma)
{
	struct fake_backend *f = ctx;

	(void)packet;
	(void)length;
	f->maps++;
	if (f->map_result)
		return f->map_result;
	*dma = f->next_dma;
	return 0;
}

static void fake_unmap(void *ctx, ob_tx_sw_u64 dma, ob_tx_sw_u32 length)
{
	struct fake_backend *f = ctx;

	(void)dma;
	(void)length;
	f->unmaps++;
}

static int fake_submit(void *ctx, void *packet, ob_tx_sw_u64 dma,
		       ob_tx_sw_u32 length,
		       const struct ob_tx_sw_ticket *ticket)
{
	struct fake_backend *f = ctx;

	(void)dma;
	(void)length;
	f->submits++;
	f->last_packet = packet;
	f->last_ticket = *ticket;
	return f->submit_result;
}

static int fake_cancel(void *ctx, const struct ob_tx_sw_ticket *ticket)
{
	struct fake_backend *f = ctx;

	f->cancels++;
	f->last_ticket = *ticket;
	return f->cancel_result;
}

static void fake_complete(void *ctx, void *packet, int status)
{
	struct fake_backend *f = ctx;

	f->completions++;
	f->last_packet = packet;
	f->last_status = status;
}

static const struct ob_tx_sw_ops fake_ops = {
	.map = fake_map,
	.unmap = fake_unmap,
	.submit = fake_submit,
	.cancel = fake_cancel,
	.complete = fake_complete,
};

static int init_queue(struct ob_tx_sw *tx, struct ob_tx_sw_slot *slots,
		     ob_tx_sw_u16 capacity, struct fake_backend *fake)
{
	int ret = ob_tx_sw_init(tx, slots, capacity, 32, 4096, &fake_ops, fake);

	return ret ? ret : ob_tx_sw_start(tx);
}

static void test_validation_and_mapping_failures(void)
{
	struct ob_tx_sw tx;
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw_ticket ticket = { 77, 88 };
	struct fake_backend f = { .next_dma = 0x1000 };
	int packet;

	EXPECT("init valid", init_queue(&tx, slots, 2, &f), 0);
	EXPECT("zero payload", ob_tx_sw_enqueue(&tx, &packet, 0, &ticket), -EINVAL);
	EXPECT("oversize payload", ob_tx_sw_enqueue(&tx, &packet, 4097, &ticket), -EINVAL);
	EXPECT("map failure", (f.map_result = -EIO,
		ob_tx_sw_enqueue(&tx, &packet, 100, &ticket)), -EIO);
	EXPECT("map failure no unmap", f.unmaps, 0);
	EXPECT("map failure no submit", f.submits, 0);
	EXPECT("ticket unchanged on rejection", ticket.slot, 77);

	f.map_result = 0;
	f.next_dma = 0x100000000ULL;
	EXPECT("high DMA rejected", ob_tx_sw_enqueue(&tx, &packet, 100, &ticket),
	       -ERANGE);
	EXPECT("high DMA unmapped", f.unmaps, 1);
	EXPECT("high DMA not submitted", f.submits, 0);
	EXPECT("no pending after failures", ob_tx_sw_pending(&tx), 0);

	f.next_dma = 0x2000;
	f.submit_result = -EIO;
	EXPECT("submit failure", ob_tx_sw_enqueue(&tx, &packet, 100, &ticket), -EIO);
	EXPECT("submit failure unmaps", f.unmaps, 2);
	EXPECT("submit failure leaves slot free", slots[0].state, OB_TX_SW_FREE);
	EXPECT("submit failure no transfer", ob_tx_sw_pending(&tx), 0);

	EXPECT("stop after failures", ob_tx_sw_stop(&tx), 0);
	EXPECT("stopped rejects enqueue", ob_tx_sw_enqueue(&tx, &packet, 100,
		&ticket), -ESHUTDOWN);
	EXPECT("restart empty", ob_tx_sw_start(&tx), 0);
	f.submit_result = 0;
	EXPECT("restart enqueue", ob_tx_sw_enqueue(&tx, &packet, 100, &ticket), 0);
	EXPECT("finish restarted packet", ob_tx_sw_complete(&tx, &ticket, 0), 0);
}

static void test_wrap_and_exactly_once_completion(void)
{
	struct ob_tx_sw tx;
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw_ticket first, second, wrapped, stale;
	struct fake_backend f = { .next_dma = 0x1000 };
	int packets[3];
	unsigned int maps_before_full;

	EXPECT("init wrap queue", init_queue(&tx, slots, 2, &f), 0);
	EXPECT("enqueue first", ob_tx_sw_enqueue(&tx, &packets[0], 100, &first), 0);
	EXPECT("first slot", first.slot, 0);
	EXPECT("first generation", first.generation, 1);
	f.next_dma += 0x1000;
	EXPECT("enqueue second", ob_tx_sw_enqueue(&tx, &packets[1], 100, &second), 0);
	EXPECT("second slot", second.slot, 1);
	maps_before_full = f.maps;
	EXPECT("full queue", ob_tx_sw_enqueue(&tx, &packets[2], 100, &wrapped), -ENOSPC);
	EXPECT("full queue does not map", f.maps, maps_before_full);
	EXPECT("out of order completion rejected",
	       ob_tx_sw_complete(&tx, &second, 0), -ESTALE);
	EXPECT("out of order preserves pending", ob_tx_sw_pending(&tx), 2);

	EXPECT("complete first", ob_tx_sw_complete(&tx, &first, 0), 0);
	EXPECT("completion packet", (long)(f.last_packet == &packets[0]), 1);
	EXPECT("completion status", f.last_status, 0);
	EXPECT("duplicate completion rejected",
	       ob_tx_sw_complete(&tx, &first, 0), -ESTALE);
	EXPECT("duplicate completion no extra unmap", f.unmaps, 1);

	f.next_dma += 0x1000;
	EXPECT("enqueue wrapped slot", ob_tx_sw_enqueue(&tx, &packets[2], 100,
		&wrapped), 0);
	EXPECT("wrapped slot index", wrapped.slot, 0);
	EXPECT("wrapped generation changes", wrapped.generation, 2);
	stale = first;
	EXPECT("stale ticket cannot complete reused slot",
	       ob_tx_sw_complete(&tx, &stale, 0), -ESTALE);
	EXPECT("complete second", ob_tx_sw_complete(&tx, &second, 0), 0);
	EXPECT("complete wrapped", ob_tx_sw_complete(&tx, &wrapped, 0), 0);
	EXPECT("queue drained", ob_tx_sw_pending(&tx), 0);
	EXPECT("three accepted completions", f.completions, 3);
	EXPECT("three mappings released", f.unmaps, 3);
}

static void test_stop_quiescence_and_pending_ownership(void)
{
	struct ob_tx_sw tx;
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw_ticket a, b, probe;
	struct fake_backend f = { .next_dma = 0x3000 };
	int packets[3];
	unsigned int unmaps_before;
	unsigned int done_before;

	EXPECT("init stop queue", init_queue(&tx, slots, 2, &f), 0);
	EXPECT("stop enqueue a", ob_tx_sw_enqueue(&tx, &packets[0], 200, &a), 0);
	f.next_dma += 0x1000;
	EXPECT("stop enqueue b", ob_tx_sw_enqueue(&tx, &packets[1], 200, &b), 0);
	f.cancel_result = -EBUSY;
	unmaps_before = f.unmaps;
	done_before = f.completions;
	EXPECT("cancel failure surfaced", ob_tx_sw_stop(&tx), -EBUSY);
	EXPECT("queue stops accepting", ob_tx_sw_enqueue(&tx, &packets[2], 200,
		&probe), -ESHUTDOWN);
	EXPECT("cancel failure retains both", ob_tx_sw_pending(&tx), 2);
	EXPECT("cancel failure does not unmap", f.unmaps, unmaps_before);
	EXPECT("cancel failure does not complete", f.completions, done_before);
	EXPECT("failed cancel leaves submitted state", slots[a.slot].state,
	       OB_TX_SW_SUBMITTED);

	f.cancel_result = 0;
	EXPECT("retry stop after backend quiesces", ob_tx_sw_stop(&tx), 0);
	EXPECT("stop drains pending", ob_tx_sw_pending(&tx), 0);
	EXPECT("two cancellations", f.cancels, 3);
	EXPECT("two stop unmaps", f.unmaps, unmaps_before + 2);
	EXPECT("two stop completions", f.completions, done_before + 2);
	EXPECT("cancel status", f.last_status, -ECANCELED);
	EXPECT("stopped queue restart", ob_tx_sw_start(&tx), 0);
}

static void test_configuration_validation(void)
{
	struct ob_tx_sw tx;
	struct ob_tx_sw_slot slots[1];
	struct fake_backend f = { 0 };

	EXPECT("zero capacity invalid", ob_tx_sw_init(&tx, slots, 0, 32, 100,
		&fake_ops, &f), -EINVAL);
	EXPECT("zero address width invalid", ob_tx_sw_init(&tx, slots, 1, 0,
		100, &fake_ops, &f), -EINVAL);
	EXPECT("address width >64 invalid", ob_tx_sw_init(&tx, slots, 1, 65,
		100, &fake_ops, &f), -EINVAL);
	EXPECT("zero max length invalid", ob_tx_sw_init(&tx, slots, 1, 32, 0,
		&fake_ops, &f), -EINVAL);
}

static void test_frame_id_completion_is_ticket_scoped(void)
{
	struct ob_tx_sw tx;
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw_ticket a, b, stale;
	struct fake_backend f = { .next_dma = 0x5000 };
	int packets[2];

	EXPECT("init frame-id queue", init_queue(&tx, slots, 2, &f), 0);
	EXPECT("enqueue identified a",
	       ob_tx_sw_enqueue_identified(&tx, &packets[0], 100, 0x1234, &a), 0);
	f.next_dma += 0x1000;
	EXPECT("enqueue identified b with recycled wire id",
	       ob_tx_sw_enqueue_identified(&tx, &packets[1], 100, 0x1234, &b), 0);
	EXPECT("same live packet pointer cannot be enqueued twice",
	       ob_tx_sw_enqueue_identified(&tx, &packets[0], 100, 0x1234,
					   &stale), -EALREADY);
	EXPECT("reported id cannot skip oldest ticket",
	       ob_tx_sw_complete_frame_id(&tx, &b, 0x1234), -ESTALE);
	EXPECT("wrong oldest frame id consumes only oldest as error",
	       ob_tx_sw_complete_frame_id(&tx, &a, 0x1235), -EBADMSG);
	EXPECT("mismatch packet belongs to oldest", (long)(f.last_packet == &packets[0]), 1);
	EXPECT("mismatch reports failed TX status", f.last_status, -EBADMSG);
	EXPECT("one packet remains after mismatch", ob_tx_sw_pending(&tx), 1);
	EXPECT("reused wire id completes against next ticket",
	       ob_tx_sw_complete_frame_id(&tx, &b, 0x1234), 0);
	EXPECT("exactly two unmaps", f.unmaps, 2);
	EXPECT("exactly two completions", f.completions, 2);
	stale = a;
	EXPECT("duplicate old ticket remains rejected",
	       ob_tx_sw_complete_frame_id(&tx, &stale, 0x1234), -ENOENT);
	EXPECT("stop frame-id queue", ob_tx_sw_stop(&tx), 0);
	EXPECT("restart frame-id queue", ob_tx_sw_start(&tx), 0);
	f.next_dma += 0x1000;
	EXPECT("enqueue without frame id remains supported",
	       ob_tx_sw_enqueue(&tx, &packets[0], 100, &a), 0);
	EXPECT("unknown frame id cannot be matched",
	       ob_tx_sw_complete_frame_id(&tx, &a, 0x1234), -ENODATA);
	EXPECT("unknown-id packet remains pending", ob_tx_sw_pending(&tx), 1);
	EXPECT("ordinary ticket completion still works",
	       ob_tx_sw_complete(&tx, &a, 0), 0);
}

static void test_dma_reclaim_precedes_wireless_status(void)
{
	struct ob_tx_sw tx;
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw_ticket a, b, stale;
	struct fake_backend f = { .next_dma = 0x7000 };
	int packets[2];
	unsigned int completions;

	EXPECT("init staged completion queue", init_queue(&tx, slots, 2, &f), 0);
	EXPECT("enqueue staged packet a",
	       ob_tx_sw_enqueue_identified(&tx, &packets[0], 100, 0x2345, &a), 0);
	f.next_dma += 0x1000;
	EXPECT("enqueue staged packet b",
	       ob_tx_sw_enqueue_identified(&tx, &packets[1], 100, 0x2346, &b), 0);
	completions = f.completions;

	EXPECT("DMA reclaim accepts only returned oldest ticket",
	       ob_tx_sw_dma_reclaim(&tx, &b), -ESTALE);
	EXPECT("out-of-order reclaim leaves mappings live", f.unmaps, 0);
	EXPECT("reclaim a", ob_tx_sw_dma_reclaim(&tx, &a), 0);
	EXPECT("reclaim unmaps exactly once", f.unmaps, 1);
	EXPECT("DMA reclaim leaves packet owned pending status",
	       ob_tx_sw_pending(&tx), 2);
	EXPECT("DMA reclaim does not report wireless completion",
	       f.completions, completions);
	EXPECT("duplicate reclaim rejected", ob_tx_sw_dma_reclaim(&tx, &a),
	       -EALREADY);
	EXPECT("wrong frame ID drops reclaimed packet",
	       ob_tx_sw_complete_reclaimed_frame_id(&tx, &a, 0x2344), -EBADMSG);
	EXPECT("reclaimed packet completed exactly once",
	       f.completions, completions + 1);
	EXPECT("mismatch maps to reclaimed packet",
	       (long)(f.last_packet == &packets[0]), 1);
	EXPECT("no second unmap after status mismatch", f.unmaps, 1);
	EXPECT("status for not-yet-reclaimed packet rejected",
	       ob_tx_sw_complete_reclaimed_frame_id(&tx, &b, 0x2346),
	       -EINPROGRESS);
	EXPECT("reclaim b", ob_tx_sw_dma_reclaim(&tx, &b), 0);
	EXPECT("matching status completes b",
	       ob_tx_sw_complete_reclaimed_frame_id(&tx, &b, 0x2346), 0);
	EXPECT("two mappings reclaimed", f.unmaps, 2);
	EXPECT("two packets handed off", f.completions, completions + 2);
	stale = a;
	EXPECT("duplicate status ticket rejected",
	       ob_tx_sw_complete_reclaimed_frame_id(&tx, &stale, 0x2345),
	       -ENOENT);
}

static void test_backend_returned_packet_ownership(void)
{
	struct ob_tx_sw tx;
	struct ob_tx_sw_slot slots[2];
	struct ob_tx_sw_ticket a, b, reclaimed = { 77, 88 };
	struct fake_backend f = { .next_dma = 0x9000 };
	int packets[2];

	EXPECT("init packet-identity queue", init_queue(&tx, slots, 2, &f), 0);
	EXPECT("enqueue returned packet a",
	       ob_tx_sw_enqueue_identified(&tx, &packets[0], 100, 0x3456, &a), 0);
	f.next_dma += 0x1000;
	EXPECT("enqueue returned packet b",
	       ob_tx_sw_enqueue_identified(&tx, &packets[1], 100, 0x3456, &b), 0);

	EXPECT("wrong returned packet cannot reclaim oldest",
	       ob_tx_sw_dma_reclaim_packet(&tx, &packets[1], &reclaimed), -ESTALE);
	EXPECT("failed packet lookup leaves ticket untouched", reclaimed.slot, 77);
	EXPECT("wrong packet leaves both mappings live", f.unmaps, 0);
	EXPECT("oldest backend-returned packet reclaimed",
	       ob_tx_sw_dma_reclaim_packet(&tx, &packets[0], &reclaimed), 0);
	EXPECT("returned packet resolves the queue ticket", reclaimed.slot, a.slot);
	EXPECT("returned packet resolves its generation",
	       reclaimed.generation, a.generation);
	EXPECT("backend packet mapping released once", f.unmaps, 1);
	EXPECT("returned packet status matches and completes",
	       ob_tx_sw_complete_reclaimed_frame_id(&tx, &reclaimed, 0x3456), 0);
	EXPECT("completion returns the same packet", (long)(f.last_packet ==
	       &packets[0]), 1);
	EXPECT("first packet disposed once", f.completions, 1);
	EXPECT("completed packet cannot claim the next pending slot",
	       ob_tx_sw_complete_reclaimed_frame_id(&tx, &reclaimed, 0x3456),
	       -ESTALE);
	EXPECT("second packet still pending", ob_tx_sw_pending(&tx), 1);
	EXPECT("reclaim second returned packet",
	       ob_tx_sw_dma_reclaim_packet(&tx, &packets[1], &reclaimed), 0);
	EXPECT("reused frame ID matches second returned packet in order",
	       ob_tx_sw_complete_reclaimed_frame_id(&tx, &reclaimed, 0x3456), 0);
	EXPECT("second packet disposed once", f.completions, 2);
	EXPECT("second packet identity preserved", (long)(f.last_packet ==
	       &packets[1]), 1);
	EXPECT("reused frame-ID packet reports success", f.last_status, 0);
	EXPECT("both mappings released exactly once", f.unmaps, 2);
	EXPECT("queue drained", ob_tx_sw_pending(&tx), 0);
	EXPECT("stop after packet-identity test", ob_tx_sw_stop(&tx), 0);
}

int main(void)
{
	test_configuration_validation();
	test_validation_and_mapping_failures();
	test_wrap_and_exactly_once_completion();
	test_stop_quiescence_and_pending_ownership();
	test_frame_id_completion_is_ticket_scoped();
	test_dma_reclaim_precedes_wireless_status();
	test_backend_returned_packet_ownership();
	if (failures) {
		printf("openbrcm tx software tests: %d failures\n", failures);
		return 1;
	}
	printf("openbrcm tx software tests: PASS\n");
	return 0;
}

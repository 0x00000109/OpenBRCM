/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — per-device DMA failure evidence (Phase 34).
 *
 * Phase 33 introduced a durable, identity-keyed DMA owner and retained its
 * resources whenever quiescence could not be proven. The remaining admission
 * restriction was module-wide: one `static bool` in ob_d3a0.c refused every
 * later D3A0/D3A1/D3B entry in the loaded module, including a second,
 * unrelated adapter.
 *
 * This object replaces that boolean with a fixed-capacity registry keyed by
 * the same full admission identity the owner uses (PCI domain/bus/devfn plus
 * chip id/rev and D11 revision). A fatal DMA event therefore refuses:
 *
 *   - the exact device that failed, and
 *   - a replacement board in the same PCI slot (the address is reused and the
 *     previous device's quiescence was never proven),
 *
 * and nothing else. The registry stores evidence only; it never frees, maps,
 * resets or otherwise touches DMA memory or hardware. It is not a DMA domain
 * proof: two PCI functions may still share an IOMMU group or an upstream
 * link. See audit/openbrcm_phase34/DMA_ISOLATION_DESIGN.md.
 *
 * Fail-closed rules:
 *   - an unkeyable identity is refused;
 *   - a saturated registry cannot prove an unrecorded device unrelated, so
 *     every identity is refused until reboot;
 *   - the module reference is taken at most once per module lifetime, because
 *     it protects the text that holds every retained record.
 */

#ifndef _OB_DMA_FATAL_H_
#define _OB_DMA_FATAL_H_

#include <linux/types.h>

#include "ob_dma_owner.h"	/* struct ob_dma_owner_identity */

#define OB_DMA_FATAL_CAP		8u
#define OB_DMA_FATAL_TX_CHANNELS	4u
#define OB_DMA_FATAL_WHERE_LEN		40u

/* The retained-DMA addresses are diagnostic evidence only. They are never
 * dereferenced and never used to free, map or reset anything.
 */
struct ob_dma_fatal_evidence {
	u64	rx_ring;
	u64	tx_ring[OB_DMA_FATAL_TX_CHANNELS];
	u32	rx_mapped;
	u32	tx_channels;
};

struct ob_dma_fatal_snapshot {
	struct ob_dma_owner_identity	id;
	struct ob_dma_fatal_evidence	ev;
	char				where[OB_DMA_FATAL_WHERE_LEN];
	u32				latch_calls;
};

/*
 * Record a fatal DMA state for @id. Idempotent: a second call for the same
 * identity (or the same PCI slot) increments the per-record call counter and
 * keeps the first evidence. Returns 0 when the state is recorded (or already
 * was), -EINVAL for an unkeyable identity, -ENOSPC when the registry is full.
 *
 * The registry takes the single module reference itself, on the first call,
 * including when the registry is full (the retained memory must not outlive
 * the text even if the new evidence cannot be stored). @pin_now, when
 * non-NULL, is only an optional report that this call took it; callers must
 * not take a second reference.
 */
int ob_dma_fatal_latch(const struct ob_dma_owner_identity *id,
		       const struct ob_dma_fatal_evidence *ev,
		       const char *where, bool *pin_now);

/*
 * Admission predicate. True when this identity must not be admitted: invalid
 * key, exact/same-slot fatal record, or a saturated registry. Reads only this
 * registry; it performs no MMIO and takes no other lock.
 */
bool ob_dma_fatal_refused(const struct ob_dma_owner_identity *id);

/* True only when this exact identity (or its PCI slot) has a record. Unlike
 * ob_dma_fatal_refused() this is false for an unrelated identity when the
 * registry is saturated; it is used for the "latch already pinned" decision.
 */
bool ob_dma_fatal_is_latched(const struct ob_dma_owner_identity *id);

bool ob_dma_fatal_module_pinned(void);
u32  ob_dma_fatal_pin_count(void);
unsigned int ob_dma_fatal_count(void);
bool ob_dma_fatal_any(void);

/* Fills up to @max snapshots, returns the number written. */
unsigned int ob_dma_fatal_snapshot(struct ob_dma_fatal_snapshot *out,
				   unsigned int max);

/* Host/oracle only: clear every record. Never compiled into the module. */
#ifndef __KERNEL__
void ob_dma_fatal_reset_for_test(void);
#endif

#endif /* _OB_DMA_FATAL_H_ */

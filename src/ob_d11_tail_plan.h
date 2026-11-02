/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 post-common tail stage plan (M1).
 *
 * This header is host-testable and contains no kernel API.  It exists so the
 * vendor-ordered tail (sub_67efd -> T1 -> DMA -> T2 -> validate) has one
 * explicit, testable definition of what each stop point means and which
 * software state may enter it.  It performs no register access itself.
 *
 * The Phase 26 draft used `stop > AFTER_DMA` on an ordinal, which skipped DMA
 * while still running T1.  The predicates below are exhaustive switches over
 * the named stage; an out-of-range value is rejected and matches no action.
 *
 * Stage ownership:
 *   DISABLED    - no action, no MMIO
 *   AFTER_FIXUP - D2B-exit check + FIFO fixup only
 *   AFTER_DMA   - the above + T1 + DMA programming
 *   COMPLETE    - the above + T2 + postcondition validation
 *
 * COMPLETE is never a default.  The default policy is DISABLED.
 */
#ifndef _OB_D11_TAIL_PLAN_H_
#define _OB_D11_TAIL_PLAN_H_

#include <linux/types.h>

enum ob_d11_tail_stop {
	OB_D11_TAIL_STOP_DISABLED = 0,
	OB_D11_TAIL_STOP_AFTER_FIXUP,
	OB_D11_TAIL_STOP_AFTER_DMA,
	OB_D11_TAIL_STOP_COMPLETE,
};

#define OB_D11_TAIL_STOP_DEFAULT OB_D11_TAIL_STOP_DISABLED

/* Action bits; one per stage operation, in vendor order. */
#define OB_D11_TAIL_ACT_FIXUP		(1u << 0)
#define OB_D11_TAIL_ACT_T1		(1u << 1)
#define OB_D11_TAIL_ACT_DMA		(1u << 2)
#define OB_D11_TAIL_ACT_T2		(1u << 3)
#define OB_D11_TAIL_ACT_VALIDATE	(1u << 4)

#define OB_D11_TAIL_ACT_ALL \
	(OB_D11_TAIL_ACT_FIXUP | OB_D11_TAIL_ACT_T1 | OB_D11_TAIL_ACT_DMA | \
	 OB_D11_TAIL_ACT_T2 | OB_D11_TAIL_ACT_VALIDATE)

/*
 * True only for the four named stages.  Anything else (negative, > COMPLETE,
 * or a future value this header does not know) is invalid and must be
 * rejected by the caller before any device access.
 */
static inline bool ob_d11_tail_stop_valid(enum ob_d11_tail_stop stop)
{
	switch (stop) {
	case OB_D11_TAIL_STOP_DISABLED:
	case OB_D11_TAIL_STOP_AFTER_FIXUP:
	case OB_D11_TAIL_STOP_AFTER_DMA:
	case OB_D11_TAIL_STOP_COMPLETE:
		return true;
	}
	return false;
}

/*
 * Exhaustive stage -> action mapping.  Invalid stages map to 0 actions, which
 * is distinct from DISABLED only through ob_d11_tail_stop_valid(); callers
 * must check validity first and refuse, never treat 0 as "nothing to do".
 */
static inline u32 ob_d11_tail_action_mask(enum ob_d11_tail_stop stop)
{
	switch (stop) {
	case OB_D11_TAIL_STOP_DISABLED:
		return 0;
	case OB_D11_TAIL_STOP_AFTER_FIXUP:
		return OB_D11_TAIL_ACT_FIXUP;
	case OB_D11_TAIL_STOP_AFTER_DMA:
		return OB_D11_TAIL_ACT_FIXUP | OB_D11_TAIL_ACT_T1 |
		       OB_D11_TAIL_ACT_DMA;
	case OB_D11_TAIL_STOP_COMPLETE:
		return OB_D11_TAIL_ACT_ALL;
	}
	return 0;
}

static inline bool ob_d11_tail_does_fixup(enum ob_d11_tail_stop stop)
{
	return (ob_d11_tail_action_mask(stop) & OB_D11_TAIL_ACT_FIXUP) != 0;
}

static inline bool ob_d11_tail_does_t1(enum ob_d11_tail_stop stop)
{
	return (ob_d11_tail_action_mask(stop) & OB_D11_TAIL_ACT_T1) != 0;
}

static inline bool ob_d11_tail_does_dma(enum ob_d11_tail_stop stop)
{
	return (ob_d11_tail_action_mask(stop) & OB_D11_TAIL_ACT_DMA) != 0;
}

static inline bool ob_d11_tail_does_t2(enum ob_d11_tail_stop stop)
{
	return (ob_d11_tail_action_mask(stop) & OB_D11_TAIL_ACT_T2) != 0;
}

static inline bool ob_d11_tail_does_validate(enum ob_d11_tail_stop stop)
{
	return (ob_d11_tail_action_mask(stop) & OB_D11_TAIL_ACT_VALIDATE) != 0;
}

/*
 * The action prefix invariant: a stage's mask must be exactly the previous
 * stage's mask plus the next contiguous action group.  Used by the host test
 * and, later, by the lifecycle publication code.
 */
static inline bool ob_d11_tail_is_prefix(enum ob_d11_tail_stop lower,
					 enum ob_d11_tail_stop upper)
{
	/*
	 * The masks are nested by construction, so mask inclusion alone defines
	 * the order.  No comparison of enum ordinals is used.
	 */
	return ob_d11_tail_stop_valid(lower) &&
	       ob_d11_tail_stop_valid(upper) &&
	       (ob_d11_tail_action_mask(lower) &
		~ob_d11_tail_action_mask(upper)) == 0;
}

/*
 * ---- software admission guard ------------------------------------------
 *
 * These flags describe software/hardware state that is ALREADY known to the
 * caller.  The guard is deliberately not a readiness claim: a clean guard
 * only says that the tail is allowed to perform its own live D2B-exit
 * re-check.  It cannot prove MACCONTROL, FIFOSIZE, SHM14, DMA or PHY state.
 *
 *   dev_lost             hw->dev_lost is monotonic and blocks all MMIO.
 *   dma_fatal            this device has an unverified DMA quiesce; reboot
 *                        required.
 *   fifo_retry_forbidden a previous FIFO poll expired; the partially
 *                        programmed FIFO must never be retried.
 *   reboot_required      a previous stage was classified reboot-required
 *                        (T1/T2/validation, or a DMA failure whose teardown
 *                        was not verified).  The classification is only
 *                        honest if it is enforced across a second entry.
 *   tail_started         the FIFO fixup already issued its first writes.  No
 *                        staged re-entry (AFTER_FIXUP -> COMPLETE) is proven
 *                        safe: COMPLETE re-runs the fixup and T1.
 *   dma_live             D3A0 DMA resources are still owned (the pool has not
 *                        been released by a verified teardown), so an engine
 *                        may be programmed.  A second tail run must not
 *                        re-program it.
 */
struct ob_d11_tail_entry_guard {
	bool dev_lost;
	bool dma_fatal;
	bool fifo_retry_forbidden;
	bool reboot_required;
	bool tail_started;
	bool dma_live;
};

enum ob_d11_tail_entry_denial {
	OB_D11_TAIL_ENTRY_OK = 0,
	OB_D11_TAIL_ENTRY_INVALID_STOP,
	OB_D11_TAIL_ENTRY_INVALID_STATE,
	OB_D11_TAIL_ENTRY_DEVICE_LOST,
	OB_D11_TAIL_ENTRY_DMA_FATAL,
	OB_D11_TAIL_ENTRY_FIFO_LATCHED,
	OB_D11_TAIL_ENTRY_REBOOT_REQUIRED,
	OB_D11_TAIL_ENTRY_ALREADY_STARTED,
	OB_D11_TAIL_ENTRY_DMA_LIVE,
};

/*
 * Side-effect-free entry decision.  DISABLED is always OK because it performs
 * no device access; every other valid stop requires a non-NULL guard and all
 * six flags clear.  The order of denial checks is diagnostic only, except
 * that stop validity is decided before any state is inspected.
 */
static inline enum ob_d11_tail_entry_denial
ob_d11_tail_entry_check(enum ob_d11_tail_stop stop,
			const struct ob_d11_tail_entry_guard *guard)
{
	if (!ob_d11_tail_stop_valid(stop))
		return OB_D11_TAIL_ENTRY_INVALID_STOP;
	if (stop == OB_D11_TAIL_STOP_DISABLED)
		return OB_D11_TAIL_ENTRY_OK;
	if (!guard)
		return OB_D11_TAIL_ENTRY_INVALID_STATE;
	if (guard->dev_lost)
		return OB_D11_TAIL_ENTRY_DEVICE_LOST;
	if (guard->dma_fatal)
		return OB_D11_TAIL_ENTRY_DMA_FATAL;
	if (guard->fifo_retry_forbidden)
		return OB_D11_TAIL_ENTRY_FIFO_LATCHED;
	if (guard->reboot_required)
		return OB_D11_TAIL_ENTRY_REBOOT_REQUIRED;
	if (guard->tail_started)
		return OB_D11_TAIL_ENTRY_ALREADY_STARTED;
	if (guard->dma_live)
		return OB_D11_TAIL_ENTRY_DMA_LIVE;
	return OB_D11_TAIL_ENTRY_OK;
}

/*
 * ---- whole-prefix admission guard (D3A1/D3B, before D2A) ----------------
 *
 * The tail guard above only protects ob_d11_tail_run().  Both isolated prefix
 * entries (ob_d3a1_test(), ob_d3b_test()) call ob_d3a1_run_prefix(), which
 * starts with D2A firmware upload and the 610 common-initvals writes BEFORE
 * the tail guard is reached, and both erase hw->d3a0 with memset() before
 * that call.  This guard is the admission point for the whole prefix: a
 * non-fresh, fatal or DMA-owning state must never reach D2A, the common
 * initvals writes, or the D3A0 state reset.
 *
 * Like the tail guard it consumes only state that is already in memory and
 * reads no register.  A clean result is not a readiness claim; the live D2B
 * exit re-check inside the tail remains mandatory.
 *
 *   dma_owner_present    hw->dma_owner is non-NULL: a durable registry owner
 *                        is still attached to this device (LIVE or RETAINED).
 *   dma_pool_created     hw->d3a0.pool_created is true; the pool handle is
 *                        the only reference that can free it.
 *   dma_resources_present any D3A0 ring is allocated or RX buffers are still
 *                        mapped; erasing hw->d3a0 would lose those references.
 *   owner_registry_refused a RETAINED record for this identity/slot exists, or
 *                        the registry is poisoned/saturated.  Reboot required.
 */
struct ob_d11_prefix_entry_guard {
	bool dev_lost;
	bool dma_fatal;
	bool fifo_retry_forbidden;
	bool reboot_required;
	bool tail_started;
	bool dma_owner_present;
	bool dma_pool_created;
	bool dma_resources_present;
	bool owner_registry_refused;
};

enum ob_d11_prefix_entry_denial {
	OB_D11_PREFIX_ENTRY_OK = 0,
	OB_D11_PREFIX_ENTRY_INVALID_STATE,
	OB_D11_PREFIX_ENTRY_DEVICE_LOST,
	OB_D11_PREFIX_ENTRY_DMA_FATAL,
	OB_D11_PREFIX_ENTRY_OWNER_RETAINED,
	OB_D11_PREFIX_ENTRY_REBOOT_REQUIRED,
	OB_D11_PREFIX_ENTRY_FIFO_LATCHED,
	OB_D11_PREFIX_ENTRY_ALREADY_STARTED,
	OB_D11_PREFIX_ENTRY_DMA_OWNED,
};

/*
 * Side-effect-free prefix decision.  Every denial is fail-closed; the order is
 * diagnostic (loss, fatal, FIFO latch, reboot-required, retained owner, prior
 * tail start, live DMA).  FIFO precedes the reboot classification so a FIFO
 * expiry keeps returning the documented -EBUSY instead of -EIO.
 * There is no "disabled" bypass: DISABLED is a tail stop point, not a prefix
 * mode, so this predicate has no stop argument.
 */
static inline enum ob_d11_prefix_entry_denial
ob_d11_prefix_entry_check(const struct ob_d11_prefix_entry_guard *guard)
{
	if (!guard)
		return OB_D11_PREFIX_ENTRY_INVALID_STATE;
	if (guard->dev_lost)
		return OB_D11_PREFIX_ENTRY_DEVICE_LOST;
	if (guard->dma_fatal)
		return OB_D11_PREFIX_ENTRY_DMA_FATAL;
	/* A FIFO poll expiry keeps its documented -EBUSY before the broader
	 * reboot-required classification below (it is also reboot-required). */
	if (guard->fifo_retry_forbidden)
		return OB_D11_PREFIX_ENTRY_FIFO_LATCHED;
	if (guard->reboot_required)
		return OB_D11_PREFIX_ENTRY_REBOOT_REQUIRED;
	if (guard->owner_registry_refused)
		return OB_D11_PREFIX_ENTRY_OWNER_RETAINED;
	if (guard->tail_started)
		return OB_D11_PREFIX_ENTRY_ALREADY_STARTED;
	if (guard->dma_owner_present || guard->dma_pool_created ||
	    guard->dma_resources_present)
		return OB_D11_PREFIX_ENTRY_DMA_OWNED;
	return OB_D11_PREFIX_ENTRY_OK;
}

#endif /* _OB_D11_TAIL_PLAN_H_ */

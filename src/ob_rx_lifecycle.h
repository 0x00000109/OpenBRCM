/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RX_LIFECYCLE_H_
#define _OB_RX_LIFECYCLE_H_

#ifdef __KERNEL__
#include <linux/types.h>
typedef bool ob_rx_lifecycle_bool;
#else
#include <stdbool.h>
typedef bool ob_rx_lifecycle_bool;
#endif

/* Pure ownership state: a timeout never counts as confirmed quiescence. */
struct ob_rx_dma_ownership {
	ob_rx_lifecycle_bool may_own;
	ob_rx_lifecycle_bool quarantined;
	ob_rx_lifecycle_bool removing;
};

/*
 * A failed stop currently leaves DMA mappings and their owner embedded in
 * devm-managed per-core state.  BCMA remove releases that state, and the
 * project has not established a parent-device/IOMMU teardown guarantee that
 * would make the mappings safe to reclaim or abandon.  Keep RX activation
 * unavailable until a durable owner and reclaim contract are implemented.
 */
static inline ob_rx_lifecycle_bool ob_rx_dma_activation_supported(void)
{
	return false;
}

static inline void ob_rx_dma_mark_enabled(struct ob_rx_dma_ownership *state)
{
	state->may_own = true;
}

static inline void
ob_rx_dma_mark_disabled_confirmed(struct ob_rx_dma_ownership *state)
{
	state->may_own = false;
	state->quarantined = false;
}

static inline void ob_rx_dma_quarantine(struct ob_rx_dma_ownership *state)
{
	state->may_own = true;
	state->quarantined = true;
}

static inline void ob_rx_dma_mark_removing(struct ob_rx_dma_ownership *state)
{
	state->removing = true;
}

static inline ob_rx_lifecycle_bool
ob_rx_dma_may_free(const struct ob_rx_dma_ownership *state)
{
	return !state->may_own && !state->quarantined;
}

static inline ob_rx_lifecycle_bool
ob_rx_dma_may_initialize(const struct ob_rx_dma_ownership *state)
{
	return !state->removing && ob_rx_dma_may_free(state);
}

#endif /* _OB_RX_LIFECYCLE_H_ */

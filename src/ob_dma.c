// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — DMA64 ring allocation and teardown (M3.2, software model only).
 *
 * M3.2 deliberately does NOT touch the D11 DMA register blocks (FIFO0 RX
 * @0x220, FIFO3 TX @0x2c0), does not publish a ring base address to hardware,
 * does not enable an engine and does not register an IRQ. It only:
 *
 *   - validates the real device's 64-bit DMA capability through the DMA API;
 *   - allocates and validates two 8 KiB-aligned coherent descriptor rings
 *     (one RX, one TX/control) with completely separate bookkeeping;
 *   - unwinds every partial allocation on failure and on module unload.
 *
 * Provenance: docs/dma_architecture.md (M3.1).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ob_core.h"
#include "ob_dma.h"

static const char *ob_dma_role_name(enum ob_dma_ring_role role)
{
	return role == OB_DMA_RING_RX ? "RX" : "TX";
}

static void ob_dma_dump_ring(struct ob_hw *hw, const struct ob_dma_ring *ring)
{
	dev_info(hw->dev,
		 "dma: %s ring cpu=%px dma=%pad descriptors=%u bytes=%u dma_aligned_8k=%s cpu_desc_aligned=%s\n",
		 ob_dma_role_name(ring->role), ring->desc_cpu, &ring->desc_dma,
		 ring->n, OB_DMA_RING_BYTES,
		 IS_ALIGNED((unsigned long)ring->desc_dma, OB_DMA_RING_ALIGN) ?
			"yes" : "no",
		 IS_ALIGNED((unsigned long)ring->desc_cpu,
			    __alignof__(struct ob_dma_desc)) ? "yes" : "no");
}

/*
 * Release one ring in reverse order of construction. Safe to call on a
 * partially built ring and leaves it in the unallocated/stopped state.
 */
static void ob_dma_ring_free(struct ob_hw *hw, struct ob_dma_ring *ring)
{
	kfree(ring->slot);
	ring->slot = NULL;
	if (ring->alloc_cpu && hw->dma.pool) {
		dma_pool_free(hw->dma.pool, ring->alloc_cpu, ring->alloc_dma);
		ring->alloc_cpu = NULL;
	}
	ring->desc_cpu = NULL;
	ring->desc_dma = 0;
	ring->alloc_dma = 0;
	ring->n = 0;
	ring->head = 0;
	ring->tail = 0;
	ring->allocated = false;
}

static int ob_dma_ring_alloc(struct ob_hw *hw, struct ob_dma_ring *ring,
			     enum ob_dma_ring_role role)
{
	struct dma_pool *pool = hw->dma.pool;
	unsigned int i;

	memset(ring, 0, sizeof(*ring));
	ring->role = role;
	ring->n = OB_DMA_RING_DESC_COUNT;

	ring->alloc_cpu = dma_pool_alloc(pool, GFP_KERNEL, &ring->alloc_dma);
	if (!ring->alloc_cpu)
		return -ENOMEM;
	ring->desc_cpu = ring->alloc_cpu;
	ring->desc_dma = ring->alloc_dma;

	/*
	 * The device only ever receives @desc_dma, so the recovered 8 KiB
	 * constraint applies to the DMA address alone. The CPU virtual address
	 * is unrelated to the hardware and only needs the natural alignment of
	 * struct ob_dma_desc so the ring can be accessed safely from the CPU.
	 */
	if (!IS_ALIGNED((unsigned long)ring->desc_dma, OB_DMA_RING_ALIGN)) {
		dev_err(hw->dev,
			"dma: %s ring DMA address not %u-byte aligned (dma=%pad)\n",
			ob_dma_role_name(role), OB_DMA_RING_ALIGN,
			&ring->desc_dma);
		ob_dma_ring_free(hw, ring);
		return -EINVAL;
	}
	if (!IS_ALIGNED((unsigned long)ring->desc_cpu,
			__alignof__(struct ob_dma_desc))) {
		dev_err(hw->dev,
			"dma: %s ring CPU address not descriptor-aligned (cpu=%px need=%u)\n",
			ob_dma_role_name(role), ring->desc_cpu,
			(unsigned int)__alignof__(struct ob_dma_desc));
		ob_dma_ring_free(hw, ring);
		return -EINVAL;
	}

	/* Known software-safe initial state: every descriptor zeroed. */
	memset(ring->desc_cpu, 0, OB_DMA_RING_BYTES);

	/* Ownership metadata, all slots unused at this point. */
	ring->slot = kcalloc(ring->n, sizeof(*ring->slot), GFP_KERNEL);
	if (!ring->slot) {
		ob_dma_ring_free(hw, ring);
		return -ENOMEM;
	}
	for (i = 0; i < ring->n; i++)
		ob_dma_slot_init(&ring->slot[i]);

	ring->head = 0;
	ring->tail = 0;
	ring->allocated = true;
	return 0;
}

int ob_dma_init(struct ob_hw *hw)
{
	struct device *dev;
	int ret;

	memset(&hw->dma, 0, sizeof(hw->dma));

	dev = hw->core->dma_dev;
	if (!dev) {
		dev_err(hw->dev, "dma: D11 core has no DMA device\n");
		return -ENODEV;
	}

	/* Validate the real device capability, not the ChipCommon bits. */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(hw->dev,
			"dma: 64-bit coherent/streaming mask rejected: %d\n",
			ret);
		return ret;
	}
	hw->dma.mask64 = true;
	dev_info(hw->dev,
		 "dma: 64-bit coherent/streaming mask accepted (%s)\n",
		 dev_name(dev));

	/*
	 * A dma_pool with size == align == boundary == 8 KiB guarantees each
	 * block is carved on an 8 KiB boundary; this is stricter than a plain
	 * dma_alloc_coherent() of the ring size.
	 */
	hw->dma.pool = dma_pool_create("ob-dma-ring", dev, OB_DMA_RING_BYTES,
				       OB_DMA_RING_ALIGN, OB_DMA_RING_ALIGN);
	if (!hw->dma.pool) {
		dev_err(hw->dev,
			"dma: dma_pool_create(%u bytes, align %u) failed\n",
			OB_DMA_RING_BYTES, OB_DMA_RING_ALIGN);
		ob_dma_free(hw);
		return -ENOMEM;
	}

	ret = ob_dma_ring_alloc(hw, &hw->dma.rx, OB_DMA_RING_RX);
	if (ret) {
		dev_err(hw->dev, "dma: RX ring allocation failed: %d\n", ret);
		ob_dma_free(hw);
		return ret;
	}

	ret = ob_dma_ring_alloc(hw, &hw->dma.tx, OB_DMA_RING_TX);
	if (ret) {
		dev_err(hw->dev, "dma: TX ring allocation failed: %d\n", ret);
		ob_dma_free(hw);
		return ret;
	}

	ob_dma_dump_ring(hw, &hw->dma.rx);
	ob_dma_dump_ring(hw, &hw->dma.tx);
	return 0;
}

void ob_dma_free(struct ob_hw *hw)
{
	if (!hw)
		return;

	ob_dma_ring_free(hw, &hw->dma.tx);
	ob_dma_ring_free(hw, &hw->dma.rx);

	dma_pool_destroy(hw->dma.pool);
	hw->dma.pool = NULL;
	hw->dma.mask64 = false;
}

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
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ob_core.h"
#include "ob_dma.h"
#include "ob_dma_owner.h"
#include "ob_dma_fatal.h"

static const char *ob_dma_role_name(enum ob_dma_ring_role role)
{
	return role == OB_DMA_RING_RX ? "RX" : "TX";
}

static void ob_dma_dump_ring(struct ob_hw *hw, const struct ob_dma_ring *ring)
{
	dev_info(hw->dev,
		 "dma: %s ring cpu=%px dma=%pad high32=%08x window=32-bit descriptors=%u bytes=%u dma_aligned_8k=%s cpu_desc_aligned=%s\n",
		 ob_dma_role_name(ring->role), ring->desc_cpu, &ring->desc_dma,
		 ob_dma_addr_high32(ring->desc_dma), ring->n,
		 ob_dma_ring_active_bytes(ring->role),
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

	/* Reject invalid role/geometry before allocating or deriving any index. */
	if (!ring || !pool || !ob_dma_ring_role_valid(role))
		return -EINVAL;

	memset(ring, 0, sizeof(*ring));
	ring->role = role;
	ring->n = ob_dma_ring_count(role);
	if (!ob_dma_ring_geometry_valid(role, ring->n)) {
		memset(ring, 0, sizeof(*ring));
		return -EINVAL;
	}

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
	if (!ob_dma_addr_in_window(ring->desc_dma)) {
		dev_err(hw->dev,
			"dma: %s ring dma=%pad outside 32-bit window (high32=%08x)\n",
			ob_dma_role_name(role), &ring->desc_dma,
			ob_dma_addr_high32(ring->desc_dma));
		ob_dma_ring_free(hw, ring);
		return -ERANGE;
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

	/*
	 * The blob's descriptor/ring writer discards the DMA address high dword
	 * and writes addrhigh = dataoffsethigh = 0x80000000, so the device can
	 * only address a 32-bit host window. Constrain the DMA mask to 32 bits
	 * so every coherent/streaming address stays below 4 GiB (a 64-bit mask
	 * would allow allocations the device cannot represent).
	 */
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(hw->dev,
			"dma: 32-bit coherent/streaming mask rejected: %d\n",
			ret);
		return ret;
	}
	hw->dma.mask_ok = true;
	hw->dma.h32 = OB_DMA_PCIE_H32;
	dev_info(hw->dev,
		 "dma: 32-bit coherent/streaming mask accepted (%s), h32=%08x\n",
		 dev_name(dev), hw->dma.h32);
	dev_info(hw->dev,
		 "dma: window = 32-bit (DMA addresses must have high32=0)\n");

	/*
	 * The durable owner is created immediately before the first DMA
	 * allocation, after every precondition that can still refuse the probe.
	 * A failed probe never runs the driver's .remove, so creating the owner
	 * earlier would leak a registry slot on each dma_dev/mask failure. It
	 * still precedes the pool, so every allocation has an accountable
	 * holder from the start and survives `struct ob_hw` when quiescence
	 * cannot be proven.
	 */
	ret = ob_dma_owner_ensure(hw, OB_DMA_OWNER_FAMILY_M32);
	if (ret)
		return ret;

	/*
	 * A dma_pool with size == align == boundary == 8 KiB guarantees each
	 * block is carved on an 8 KiB boundary; this is stricter than a plain
	 * dma_alloc_coherent() of the ring size.
	 */
	hw->dma.pool = dma_pool_create("ob-dma-ring", dev, OB_DMA_RING_BYTES,
				       OB_DMA_RING_ALIGN, OB_DMA_RING_ALIGN);
	if (hw->dma.pool)
		(void)ob_dma_owner_add(hw->dma_owner,
				       OB_DMA_OWNER_CLASS_POOL, 1);
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
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_DESC, 1);
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_SLOT, 1);

	ret = ob_dma_ring_alloc(hw, &hw->dma.tx, OB_DMA_RING_TX);
	if (ret) {
		dev_err(hw->dev, "dma: TX ring allocation failed: %d\n", ret);
		ob_dma_free(hw);
		return ret;
	}
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_DESC, 1);
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_SLOT, 1);

	ob_dma_dump_ring(hw, &hw->dma.rx);
	ob_dma_dump_ring(hw, &hw->dma.tx);
	return 0;
}

void ob_dma_free(struct ob_hw *hw)
{
	if (!hw)
		return;
	/* RX descriptors share this pool; do not destroy it if the device may
	 * still own RX memory after an unconfirmed disable.
	 */
	if (!ob_rx_dma_may_free(&hw->rx.dma_ownership)) {
		/* Ownership transfer, not a free: the registry-held owner keeps
		 * the handles reachable after devres releases `struct ob_hw`, and
		 * refuses every later release. */
		ob_dma_owner_retain_m32(hw,
					OB_DMA_OWNER_REASON_QUIESCE_UNCONFIRMED,
					"ob_dma_free");
		dev_crit(hw->dev,
			 "dma: retaining rings/pool because RX quiescence is unconfirmed (owner retained, release refused)\n");
		return;
	}

	ob_dma_ring_free(hw, &hw->dma.tx);
	ob_dma_ring_free(hw, &hw->dma.rx);

	dma_pool_destroy(hw->dma.pool);
	hw->dma.pool = NULL;
	hw->dma.h32 = 0;
	hw->dma.mask_ok = false;

	/* Only now, with every class released and quiescence confirmed, may the
	 * owner be cleared and dropped. The family is required so this path can
	 * never clear a D3A0 owner.
	 */
	ob_dma_owner_finish(hw, OB_DMA_OWNER_FAMILY_M32, true);
}

/* ---- durable DMA resource owner glue (Phase 33) ------------------------ */

/*
 * Build the registry key from the already-admitted BCMA/PCI identity. Nothing
 * here reads config space or a device register: the values were resolved by
 * probe before any allocation.
 */
static const char *ob_dma_owner_family_label(enum ob_dma_owner_family family)
{
	switch (family) {
	case OB_DMA_OWNER_FAMILY_M32:
		return "M3.2";
	case OB_DMA_OWNER_FAMILY_D3A0:
		return "D3A0";
	case OB_DMA_OWNER_FAMILY_NONE:
		break;
	}
	return "NONE";
}

void ob_dma_owner_core_identity(struct bcma_device *core,
				struct ob_dma_owner_identity *id)
{
	struct pci_dev *pdev;

	memset(id, 0, sizeof(*id));
	if (!core || !core->bus || core->bus->hosttype != BCMA_HOSTTYPE_PCI)
		return;
	pdev = core->bus->host_pci;
	if (!pdev || !pdev->bus)
		return;
	id->pci_present = true;
	id->pci_domain = pci_domain_nr(pdev->bus);
	id->pci_bus = pdev->bus->number;
	id->pci_devfn = pdev->devfn;
	id->chip_id = core->bus->chipinfo.id;
	id->chip_rev = core->bus->chipinfo.rev;
	id->d11_rev = core->id.rev;
}

static void ob_dma_owner_fill_identity(struct ob_hw *hw,
				       struct ob_dma_owner_identity *id)
{
	if (!hw) {
		memset(id, 0, sizeof(*id));
		return;
	}
	ob_dma_owner_core_identity(hw->core, id);
}

bool ob_dma_fatal_core_refused(struct bcma_device *core)
{
	struct ob_dma_owner_identity id;

	ob_dma_owner_core_identity(core, &id);
	return ob_dma_fatal_refused(&id);
}

bool ob_dma_owner_active(const struct ob_hw *hw)
{
	return hw && hw->dma_owner;
}

/*
 * Production probe guard: true when this device identity (or a replacement in
 * the same PCI slot) has a retained DMA record, the registry is saturated, or
 * the identity cannot be keyed. Reads no device register: the identity comes
 * from BCMA enumeration and was already used for profile admission.
 */
bool ob_dma_owner_core_probe_refused(struct bcma_device *core)
{
	struct ob_dma_owner_identity id;

	ob_dma_owner_core_identity(core, &id);
	return ob_dma_owner_probe_refused(&id);
}

int ob_dma_owner_ensure(struct ob_hw *hw, enum ob_dma_owner_family family)
{
	struct ob_dma_owner_identity id;
	int ret;

	if (!hw)
		return -EINVAL;
	if (hw->dma_owner) {
		/* Strict M3.2/D3A0 mutual exclusion: one owner cannot host two
		 * allocation families, because finish() would release both. */
		if (ob_dma_owner_family(hw->dma_owner) != family) {
			dev_err(hw->dev,
				"dma-owner: %s allocation cannot share the existing %s owner\n",
				ob_dma_owner_family_label(family),
				ob_dma_owner_family_label(
					ob_dma_owner_family(hw->dma_owner)));
			return -EBUSY;
		}
		return 0;
	}
	ob_dma_owner_fill_identity(hw, &id);
	if (!id.pci_present) {
		/*
		 * Fail closed: an unkeyed device cannot be distinguished from a
		 * device with a retained allocation, so it gets no owner.
		 */
		dev_err(hw->dev,
			"dma-owner: no PCI identity; owner cannot be keyed\n");
		return -ENODEV;
	}
	ret = ob_dma_owner_create(&id, family, &hw->dma_owner);
	if (ret) {
		dev_err(hw->dev,
			"dma-owner: cannot create owner: %d (retained record or registry saturated)\n",
			ret);
		hw->dma_owner = NULL;
	}
	return ret;
}

void ob_dma_owner_finish(struct ob_hw *hw, enum ob_dma_owner_family family,
			 bool quiescence_verified)
{
	struct ob_dma_owner *owner;
	struct ob_dma_owner_identity id;
	int ret;

	if (!hw || !hw->dma_owner)
		return;
	owner = hw->dma_owner;
	if (ob_dma_owner_family(owner) != family) {
		/* Leave the handle in place: the other family's owner is still
		 * live and must stay reachable for its own teardown. */
		dev_crit(hw->dev,
			 "dma-owner: refusing to release the %s owner from the %s path\n",
			 ob_dma_owner_family_label(ob_dma_owner_family(owner)),
			 ob_dma_owner_family_label(family));
		return;
	}
	/* The caller keeps its own reference; the registry holds the other, or
	 * a retained record keeps it for the module lifetime. Clearing the
	 * handle makes a second finish() a no-op instead of a double release.
	 */
	hw->dma_owner = NULL;
	ob_dma_owner_fill_identity(hw, &id);

	ret = ob_dma_owner_clear_resources(owner, family, quiescence_verified);
	if (ret)
		dev_crit(hw->dev,
			 "dma-owner: resource census not cleared (%d); release stays refused\n",
			 ret);
	ret = ob_dma_owner_release(owner, &id, family, quiescence_verified);
	if (ret)
		dev_crit(hw->dev,
			 "dma-owner: owner not released (%d, state=%s); retained record keeps it reachable\n",
			 ret, ob_dma_owner_state_name(ob_dma_owner_state(owner)));
	ob_dma_owner_put(owner);
}

void ob_dma_owner_retain_m32(struct ob_hw *hw,
			     enum ob_dma_owner_retain_reason reason,
			     const char *where)
{
	const void *pool[1];
	const void *desc[4];
	const void *slot[2];
	int ret;

	if (!hw)
		return;
	if (ob_dma_owner_ensure(hw, OB_DMA_OWNER_FAMILY_M32)) {
		dev_crit(hw->dev,
			 "dma-owner: retained DMA has no accountable owner (registry refusal)\n");
		return;
	}

	pool[0] = hw->dma.pool;
	desc[0] = hw->dma.rx.alloc_cpu;
	desc[1] = (const void *)(uintptr_t)hw->dma.rx.alloc_dma;
	desc[2] = hw->dma.tx.alloc_cpu;
	desc[3] = (const void *)(uintptr_t)hw->dma.tx.alloc_dma;
	slot[0] = hw->dma.rx.slot;
	slot[1] = hw->dma.tx.slot;

	ret = ob_dma_owner_adopt(hw->dma_owner, OB_DMA_OWNER_CLASS_POOL,
				 pool, hw->dma.pool ? 1u : 0u);
	if (ret && ret != -EEXIST)
		dev_err(hw->dev, "dma-owner: pool adoption failed: %d\n", ret);
	ret = ob_dma_owner_adopt(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_DESC,
				 desc, 4);
	if (ret && ret != -EEXIST)
		dev_err(hw->dev,
			"dma-owner: descriptor-ring adoption failed: %d\n", ret);
	ret = ob_dma_owner_adopt(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_SLOT,
				 slot, 2);
	if (ret && ret != -EEXIST)
		dev_err(hw->dev, "dma-owner: slot adoption failed: %d\n", ret);

	/* A D3A0 fatal latch for this same device may already have pinned the
	 * module; do not add a second reference for one retention. The pin is
	 * module-wide by design (it protects the retained record's text).
	 */
	(void)ob_dma_owner_retain(hw->dma_owner, reason, where,
				  !ob_dma_fatal_module_pinned());
}

void ob_dma_owner_retain_d3a0(struct ob_hw *hw,
			      enum ob_dma_owner_retain_reason reason,
			      const char *where)
{
	const void *pool[1];
	const void *desc[OB_DMA_OWNER_MAX_HANDLES];
	const void *slot[OB_DMA_OWNER_MAX_HANDLES];
	unsigned int nd = 0, ns = 0, ch;
	int ret;

	if (!hw)
		return;
	if (ob_dma_owner_ensure(hw, OB_DMA_OWNER_FAMILY_D3A0)) {
		dev_crit(hw->dev,
			 "dma-owner: retained D3A0 DMA has no accountable owner (registry refusal)\n");
		return;
	}

	pool[0] = hw->d3a0.pool;
	desc[nd++] = hw->d3a0.rx.alloc_cpu;
	desc[nd++] = (const void *)(uintptr_t)hw->d3a0.rx.alloc_dma;
	slot[ns++] = hw->d3a0.rx.slot;
	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++) {
		desc[nd++] = hw->d3a0.tx[ch].alloc_cpu;
		desc[nd++] = (const void *)(uintptr_t)hw->d3a0.tx[ch].alloc_dma;
		slot[ns++] = hw->d3a0.tx[ch].slot;
	}

	ret = ob_dma_owner_adopt(hw->dma_owner, OB_DMA_OWNER_CLASS_POOL,
				 pool, hw->d3a0.pool ? 1u : 0u);
	if (ret && ret != -EEXIST)
		dev_err(hw->dev, "dma-owner: d3a0 pool adoption failed: %d\n",
			ret);
	ret = ob_dma_owner_adopt(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_DESC,
				 desc, nd);
	if (ret && ret != -EEXIST)
		dev_err(hw->dev,
			"dma-owner: d3a0 descriptor adoption failed: %d\n", ret);
	ret = ob_dma_owner_adopt(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_SLOT,
				 slot, ns);
	if (ret && ret != -EEXIST)
		dev_err(hw->dev, "dma-owner: d3a0 slot adoption failed: %d\n",
			ret);

	(void)ob_dma_owner_retain(hw->dma_owner, reason, where,
				  !ob_dma_fatal_module_pinned());
}

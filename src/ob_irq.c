// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — D11 interrupt plumbing (M3.3).
 *
 * Establishes a safe IRQ path for the BCM4352 / D11 rev42:
 *
 *   PCI/BCMA IRQ -> OpenBRCM handler -> service gate
 *                -> (when enabled) bounded status/ack path -> unregister
 *
 * Registration never opens the service gate. First-up may run synchronously
 * while RX resources/source routing already exist; until a later authorized
 * stage enables service, each handler returns before its first D11 access.
 *
 * Provenance: docs/dma_architecture.md (0x128/0x12C) and upstream `struct
 * d11regs` (C3). The PCI IRQ is the one bcma already recorded for the core
 * (`core->irq`); OpenBRCM does not allocate IRQ vectors of its own.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_irq.h"

static const char *ob_irq_mode(struct pci_dev *pdev)
{
	if (!pdev)
		return "unknown";
	if (pdev->msix_enabled)
		return "MSI-X";
	if (pdev->msi_enabled)
		return "MSI";
	return "INTx";
}

/*
 * Minimal hardirq handler: no sleeping APIs, no allocation, no loops. It reads
 * the top-level status, keeps only the bits OpenBRCM owns and acknowledges
 * exactly those (write-1-to-clear). Everything else is left untouched.
 */
static irqreturn_t ob_irq_handler(int irq, void *dev_id)
{
	struct ob_hw *hw = dev_id;
	u32 raw, ack;

	hw->irq.total++;

	if (!ob_d11_irq_service_allowed(READ_ONCE(hw->irq.active))) {
		hw->irq.none++;
		return IRQ_NONE;
	}

	raw = ob_d11_read32(hw, OB_D11_REG_MACINTSTATUS);
	hw->irq.last_status = raw;

	if (!ob_d11_irq_has_work(raw, hw->irq.owned_mask)) {
		hw->irq.none++;
		return IRQ_NONE;
	}

	if (ob_d11_irq_unexpected(raw, hw->irq.owned_mask,
				  OB_D11_IRQ_KNOWN_MASK))
		hw->irq.unexpected++;
	hw->irq.handled++;

	/*
	 * MI_DMAINT is the DMA-interrupt summary. Hand the FIFO0 RX handling to
	 * ob_rx (acks I_RI, masks the source and schedules bounded deferred
	 * work); only then acknowledge the owned MACINTSTATUS bit.
	 */
	if (raw & OB_D11_MI_DMAINT)
		ob_rx_irq(hw);

	ack = ob_d11_irq_ack_bits(raw, hw->irq.owned_mask);
	ob_d11_write32(hw, OB_D11_REG_MACINTSTATUS, ack);

	dev_info_ratelimited(hw->dev, "irq: handled status=%08x ack=%08x\n",
			     raw, ack);

	return IRQ_HANDLED;
}

int ob_irq_init(struct ob_hw *hw)
{
	struct bcma_bus *bus = hw->bus;
	struct pci_dev *pdev = bus->host_pci;
	unsigned int irq;
	u32 status, mask, newmask;
	int ret;

	memset(&hw->irq, 0, sizeof(hw->irq));
	hw->irq.irq = -1;
	hw->irq.shared = true;
	hw->irq.owned_mask = OB_D11_IRQ_OWNED_MASK;

	if (bus->hosttype != BCMA_HOSTTYPE_PCI || !pdev) {
		dev_err(hw->dev,
			"irq: bus is not PCI-hosted (hosttype=%d host_pci=%px)\n",
			bus->hosttype, pdev);
		return -ENODEV;
	}

	irq = hw->core->irq;
	dev_info(hw->dev,
		 "irq: pci irq=%u bcma irq=%u pci_dev->irq=%u mode=%s agree=%d\n",
		 pdev->irq, hw->core->irq, pdev->irq, ob_irq_mode(pdev),
		 (irq == pdev->irq));
	if (irq != pdev->irq)
		dev_warn(hw->dev,
			 "irq: core->irq (%u) != pci_dev->irq (%u)\n",
			 irq, pdev->irq);

	/*
	 * Read-only initial state, reported before anything is changed. A
	 * non-zero mask here is residual hardware state, not something
	 * OpenBRCM programmed.
	 */
	status = ob_d11_read32(hw, OB_D11_REG_MACINTSTATUS);
	mask = ob_d11_read32(hw, OB_D11_REG_MACINTMASK);
	hw->irq.initial_status = status;
	hw->irq.initial_mask = mask;
	dev_info(hw->dev,
		 "irq: initial macintstatus=%08x macintmask=%08x\n",
		 status, mask);
	dev_info(hw->dev, "irq: fifo rx intstatus=%08x intmask=%08x\n",
		 ob_d11_read32(hw,
			     OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX)),
		 ob_d11_read32(hw,
			     OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4));

	/*
	 * Mask only the sources OpenBRCM owns, preserving every unrelated mask
	 * bit (RMW). Writing the mask never enables a source.
	 */
	newmask = ob_d11_irq_mask_clear(mask, hw->irq.owned_mask);
	if (newmask != mask) {
		ob_d11_write32(hw, OB_D11_REG_MACINTMASK, newmask);
		(void)ob_d11_read32(hw, OB_D11_REG_MACINTMASK);
		dev_info(hw->dev,
			 "irq: macintmask %08x -> %08x (owned %08x masked)\n",
			 mask, newmask, hw->irq.owned_mask);
	} else {
		dev_info(hw->dev, "irq: owned mask %08x already clear\n",
			 hw->irq.owned_mask);
	}

	/* Registered is not synonymous with D11 service enabled. */
	hw->irq.active = false;
	ret = request_irq(irq, ob_irq_handler, IRQF_SHARED, OB_DRV_NAME, hw);
	if (ret) {
		dev_err(hw->dev, "irq: request_irq(%u) failed: %d\n", irq, ret);
		return ret;
	}
	hw->irq.irq = irq;
	hw->irq.installed = true;
	hw->irq.active = false;

	dev_info(hw->dev, "irq: requested irq=%u shared=1\n", irq);
	dev_info(hw->dev, "irq: handler installed, D11 sources remain masked\n");
	return 0;
}

void ob_irq_free(struct ob_hw *hw)
{
	if (!hw || !hw->irq.installed)
		return;

	/* Close the shared-IRQ service gate before touching teardown state. */
	WRITE_ONCE(hw->irq.active, false);

	/* Mask only while the D11 window remains accessible. */
	if (!hw->dev_lost) {
		ob_d11_write32(hw, OB_D11_REG_MACINTMASK,
				     ob_d11_irq_mask_clear(
					ob_d11_read32(hw, OB_D11_REG_MACINTMASK),
					hw->irq.owned_mask));
		(void)ob_d11_read32(hw, OB_D11_REG_MACINTMASK);
		dev_info(hw->dev, "irq: masked\n");
	}

	/* Drain any handler that observed active before the gate was closed. */
	synchronize_irq(hw->irq.irq);
	dev_info(hw->dev, "irq: synchronized\n");

	/* Unregister the handler after all in-flight accesses are drained. */
	free_irq(hw->irq.irq, hw);
	dev_info(hw->dev, "irq: freed\n");

	dev_info(hw->dev,
		 "irq: totals total=%u handled=%u none=%u unexpected=%u last=%08x\n",
		 hw->irq.total, hw->irq.handled, hw->irq.none,
		 hw->irq.unexpected, hw->irq.last_status);

	hw->irq.installed = false;
	hw->irq.irq = -1;
}

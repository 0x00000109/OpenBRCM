// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — core lifecycle and bcma glue.
 *
 * Clean-room, specification-driven (see docs/provenance.md).
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "ob_core.h"
#include "ob_si.h"
#include "ob_mac80211.h"

int ob_probe(struct bcma_device *core)
{
	struct ob_hw *hw;
	int ret;

	if (core->id.manuf != BCMA_MANUF_BCM ||
	    core->id.id != BCMA_CORE_80211)
		return -ENODEV;

	hw = devm_kzalloc(&core->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->core = core;
	hw->dev = &core->dev;
	hw->bus = core->bus;
	hw->chip_id = core->bus->chipinfo.id;
	hw->chip_rev = core->bus->chipinfo.rev;

	bcma_set_drvdata(core, hw);

	dev_info(hw->dev,
		 OB_DRV_NAME ": chip 0x%04x rev %u, d11 core rev %u\n",
		 hw->chip_id, hw->chip_rev, core->id.rev);

	ret = ob_si_probe(hw);
	if (ret)
		return ret;

	/*
	 * M3.2: allocate the DMA64 descriptor rings (software model only). The
	 * D11 DMA register blocks are left untouched and no engine is enabled.
	 */
	ret = ob_dma_init(hw);
	if (ret)
		return ret;

	/*
	 * M3.3: register the D11 interrupt path. No source is enabled; the
	 * handler is installed only to prove safe registration/teardown.
	 */
	ret = ob_irq_init(hw);
	if (ret) {
		ob_dma_free(hw);
		return ret;
	}

	/*
	 * M3.4B: map the RX buffers, program the FIFO0 ring, route the D11 IRQ
	 * and enable only I_RI + MI_DMAINT. No TX, no mac80211 RX yet.
	 */
	ret = ob_rx_init(hw);
	if (ret) {
		/* ob_rx_init already quiesced and freed its buffers. */
		ob_irq_free(hw);
		ob_dma_free(hw);
		return ret;
	}

	ret = ob_mac80211_register(hw);
	if (ret) {
		ob_rx_quiesce(hw);
		ob_irq_free(hw);
		ob_rx_free_buffers(hw);
		ob_dma_free(hw);
		return ret;
	}

	return 0;
}

void ob_remove(struct bcma_device *core)
{
	struct ob_hw *hw = bcma_get_drvdata(core);

	if (!hw)
		return;

	ob_mac80211_unregister(hw);
	/*
	 * Teardown order: mask RX sources, disable host routing, disable the RX
	 * engine with a bounded poll, synchronize_irq() so no in-flight hard IRQ
	 * can schedule new deferred work, tasklet_kill(), then free_irq, then
	 * unmap/free every still-mapped buffer exactly once, then free rings.
	 */
	ob_rx_quiesce(hw);
	ob_irq_free(hw);
	ob_rx_free_buffers(hw);
	ob_dma_free(hw);
	dev_info(hw->dev, OB_DRV_NAME ": removed\n");
	bcma_set_drvdata(core, NULL);
}

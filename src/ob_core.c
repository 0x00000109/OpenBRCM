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
#include "ob_ucode.h"
#include "ob_initvals.h"

/*
 * Explicit firmware-validation-only mode.
 *
 * When set, probe binds through BCMA, performs the read-only identity check
 * below, acquires/hashes/parses the three rev42 images and returns
 * successfully. It deliberately skips every later bring-up stage: no
 * OpenBRCM MMIO access at all (not even reads), no bcma_host_pci_up(),
 * bcma_core_enable()/reset, DMA/IRQ/RX setup, and no mac80211 registration.
 * ob_remove() recognises hw->validate_only and skips teardown of resources
 * that were never initialized.
 *
 * Read-only via sysfs; set at load time:  insmod openbrcm.ko fw_validate_only=1
 */
static bool fw_validate_only;
module_param(fw_validate_only, bool, 0444);
MODULE_PARM_DESC(fw_validate_only,
		 "validate rev42 firmware only; skip all hardware bring-up (default: 0)");

/*
 * Explicit D11 rev42 ucode-upload + PSM-start hardware test (M3.4D2A).
 *
 * When set, probe performs the minimum proven core preparation, uploads the
 * validated rev42 ucode, starts the PSM and waits (bounded) for MI_MACSSPNDD,
 * then STOPS. It never applies initvals and never reaches PHY/radio/channel,
 * RX/TX DMA, IRQ registration or mac80211. Mutually exclusive with
 * fw_validate_only (mode policy is explicit; conflicting modes fail probe).
 */
static bool ucode_test_only;
module_param(ucode_test_only, bool, 0444);
MODULE_PARM_DESC(ucode_test_only,
		 "D11 rev42 ucode upload + PSM start only; stops before initvals/PHY/DMA (default: 0)");

/*
 * Explicit isolated D11 rev42 common-initvals test (M3.4D2B).
 *
 * When set, probe runs the hardware-proven D2A core (ucode upload + PSM start),
 * applies exactly the 610 `d11ac1initvals42` common-initvals records, reads the
 * deterministic postconditions, then STOPS before bsinitvals/sub_6656c/PHY/
 * radio/channel/RX/TX DMA/IRQ/mac80211. Mutually exclusive with the other
 * isolated modes; any conflicting combination fails probe before hardware
 * access.
 */
static bool initvals_test_only;
module_param(initvals_test_only, bool, 0444);
MODULE_PARM_DESC(initvals_test_only,
		 "D11 rev42 common initvals + PSM only; stops before bsinitvals/PHY/DMA (default: 0)");

int ob_probe(struct bcma_device *core)
{
	struct ob_hw *hw;
	enum ob_isolated_mode mode;
	int ret;

	if (core->id.manuf != BCMA_MANUF_BCM ||
	    core->id.id != BCMA_CORE_80211)
		return -ENODEV;

	/* Explicit mode policy: at most one isolated mode may be selected. */
	mode = ob_isolated_mode_select(fw_validate_only, ucode_test_only,
				       initvals_test_only);
	if (ob_isolated_mode_conflict(mode)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": fw_validate_only/ucode_test_only/initvals_test_only are mutually exclusive\n");
		return -EINVAL;
	}

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

	/*
	 * fw_validate_only: BCMA has bound the device and the identity check
	 * above passed. Validate the rev42 images and return. ob_fw_probe() only
	 * calls request_firmware()/release_firmware() and the size-bounded
	 * parsers; it never touches an OpenBRCM register. No mac80211.
	 */
	if (mode == OB_ISOLATED_FW_VALIDATE) {
		hw->validate_only = true;
		ret = ob_fw_probe(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		dev_info(hw->dev,
			 "fw: validation-only complete; hardware bring-up skipped\n");
		return 0;
	}

	/*
	 * ucode_test_only: isolated D11 ucode upload + PSM start. Performs only
	 * the minimum core prep and the recovered upload/start sequence, then
	 * returns before any normal OpenBRCM bring-up (no ob_si_probe, no DMA,
	 * no IRQ, no RX, no mac80211).
	 */
	if (mode == OB_ISOLATED_UCODE_TEST) {
		hw->ucode_test_only = true;
		ret = ob_ucode_test(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * initvals_test_only: isolated D11 common-initvals test. Runs the proven
	 * D2A core, applies the 610 common-initvals records and verifies the
	 * deterministic postconditions, then returns before any normal OpenBRCM
	 * bring-up (no ob_si_probe, no bsinitvals/PHY/radio, no DMA, no IRQ, no
	 * RX, no mac80211).
	 */
	if (mode == OB_ISOLATED_INITVALS_TEST) {
		hw->initvals_test_only = true;
		ret = ob_initvals_test(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	ret = ob_si_probe(hw);
	if (ret)
		return ret;

	/*
	 * M3.4D1: acquire and validate the exact vendor rev42 firmware through
	 * request_firmware(). Acquisition + dry-run only — no hardware writes.
	 * Present-but-invalid firmware fails probe; absent firmware is reported
	 * but does not disturb the validated SPROM/MAC/DMA/IRQ paths.
	 */
	ret = ob_fw_probe(hw);
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

	/*
	 * fw_validate_only never initialized mac80211, DMA, IRQ or RX. Skip
	 * every teardown step so no uninitialized resource is touched.
	 */
	if (hw->validate_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (validation-only; nothing to tear down)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/*
	 * ucode_test_only never initialized mac80211, IRQ, DMA or RX; skip all
	 * teardown. No MAC/PHY register is restored because no recovery write is
	 * provenance-backed for this partial state (see docs/milestones.md).
	 */
	if (hw->ucode_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (ucode-test; no resource teardown, hardware left as-is)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/*
	 * initvals_test_only never initialized mac80211, IRQ, DMA or RX; skip all
	 * teardown. Like ucode_test_only, no MAC/PHY register is restored because
	 * no recovery write is provenance-backed for this partial state.
	 */
	if (hw->initvals_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (initvals-test; no resource teardown, hardware left as-is)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

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

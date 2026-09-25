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

/*
 * Explicit isolated D3A0 DMA lifecycle test (M3.4D3A0).
 *
 * D3A0 TYPE: ISOLATED DMA LIFECYCLE TEST (not a full vendor-prefix
 * reproduction). When set, probe runs the proven D2B prefix, the
 * provenance-pinned D11/clock/IRQ-source prerequisites, then the four TX DMA
 * channels and FIFO0 RX (64 buffers), validates deterministic postconditions,
 * executes the mandatory verified quiesce and frees the Linux DMA resources
 * only after every programmed engine had its own verified normal stop. It
 * never reaches band init/bsinitvals/PHY/radio/channel/mac80211 and never
 * enables EN_MAC, MACINTMASK, MI_DMAINT or the host IRQ route. Mutually
 * exclusive with the other isolated modes; any conflict fails probe before
 * hardware access.
 */
static bool dma_test_only;
module_param(dma_test_only, bool, 0444);
MODULE_PARM_DESC(dma_test_only,
		 "isolated D3A0 DMA lifecycle test (4 TX + FIFO0 RX) + verified quiesce; stops before band/PHY/mac80211 (default: 0)");

/*
 * Explicit isolated D3A1 vendor post-common / pre-PHY D11 tail test
 * (M3.4D3A1).
 *
 * BCM4352 rev42 vendor-ordered post-common/pre-PHY D11 tail test; includes the
 * proven DMA lifecycle in its vendor position (T1 -> DMA -> T2); stops before
 * sub_6656c/bsinitvals/PHY. When set, probe runs the proven D2A/D2B core, the
 * exact rev42 tail (sub_67efd equivalent, T1, DMA, T2, switch_macfreq), the
 * deterministic postconditions and the mandatory verified quiesce, then STOPS
 * before sub_6656c/wlc_phy_init/PHY/radio/channel/mac80211. It never enables
 * EN_MAC, MACINTMASK, MI_DMAINT or the host IRQ route. Mutually exclusive with
 * the other isolated modes; any conflict fails probe before hardware access.
 */
static bool d11_tail_test_only;
module_param(d11_tail_test_only, bool, 0444);
MODULE_PARM_DESC(d11_tail_test_only,
		 "isolated D3A1 vendor-ordered post-common/pre-PHY D11 tail test; includes proven DMA lifecycle; stops before bsinitvals/PHY (default: 0)");

/*
 * Explicit isolated read-only external-SPROM evidence capture (D3B MHF3).
 *
 * Reuses the existing external-SPROM diagnostic (ob_si_sprom_diag) to emit the
 * already-read, CRC-validated 234-word rev11 image. It performs NO hardware
 * writes at all: no power-up, no D11/core/clock write, no firmware upload, no
 * PSM start, no DMA, no IRQ, no PHY/radio/channel, no MAC enable and no
 * mac80211. Mutually exclusive with the other isolated modes.
 */
static bool sprom_evidence_only;
module_param(sprom_evidence_only, bool, 0444);
MODULE_PARM_DESC(sprom_evidence_only,
		 "read-only external-SPROM evidence capture (emits the 234-word rev11 image); no bring-up (default: 0)");

/*
 * Explicit isolated D3B band-init test (M3.4D3B).
 *
 * Runs the proven D2A/D2B core and the exact D3A1 vendor prefix with the D3A0
 * DMA engines left LIVE (vendor order T1 -> DMA -> T2), then the exact
 * sub_6656c MHF writes + d11ac1bsinitvals42 (73 records) and STOPS before
 * wlc_phy_init. It never enters PHY/radio/channel, never enables EN_MAC, never
 * routes the host IRQ and never registers mac80211. Mutually exclusive with the
 * other isolated modes; any conflict fails probe before hardware access.
 */
static bool bsinitvals_test_only;
module_param(bsinitvals_test_only, bool, 0444);
MODULE_PARM_DESC(bsinitvals_test_only,
		 "isolated D3B band-init + d11ac1bsinitvals42 test; DMA stays live through band init; stops before wlc_phy_init/PHY (default: 0)");

int ob_probe(struct bcma_device *core)
{
	struct ob_hw *hw;
	enum ob_isolated_mode mode;
	int ret;

	if (core->id.manuf != BCMA_MANUF_BCM ||
	    core->id.id != BCMA_CORE_80211)
		return -ENODEV;

	/* Explicit mode policy: at most one isolated mode may be selected. */
	mode = ob_isolated_mode_select6(fw_validate_only, ucode_test_only,
					initvals_test_only, dma_test_only,
					d11_tail_test_only,
					bsinitvals_test_only);
	if (ob_isolated_mode_conflict(mode)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": fw_validate_only/ucode_test_only/initvals_test_only/dma_test_only/d11_tail_test_only/bsinitvals_test_only are mutually exclusive\n");
		return -EINVAL;
	}
	if (sprom_evidence_only && mode != OB_ISOLATED_NONE) {
		dev_err(&core->dev,
			OB_DRV_NAME ": sprom_evidence_only conflicts with other modes\n");
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
	 * sprom_evidence_only: bind, then run ONLY the read-only external-SPROM
	 * diagnostic and return. Reuses ob_si_sprom_diag(); no power-up, no
	 * D11/clock write, no firmware/PSM/DMA/IRQ/PHY/MAC/mac80211.
	 */
	if (sprom_evidence_only) {
		hw->sprom_evidence_only = true;
		ret = ob_si_sprom_evidence(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

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

	/*
	 * dma_test_only: isolated D3A0 DMA lifecycle test. Runs the proven D2B
	 * prefix, the pinned D11/clock/IRQ-source prerequisites, the four TX +
	 * FIFO0 RX DMA lifecycle, validation, mandatory verified quiesce and safe
	 * free. It is not a full vendor-prefix reproduction (see
	 * docs/d3a0_dma_test_design.md §0) and never reaches band
	 * init/bsinitvals/PHY/radio/channel, never enables EN_MAC or the host IRQ
	 * route, and never registers mac80211. On success all DMA resources are
	 * already released; @remove is still consulted for the fail-closed fatal
	 * state.
	 */
	if (mode == OB_ISOLATED_DMA_TEST) {
		hw->dma_test_only = true;
		ret = ob_d3a0_test(hw);
		if (hw->d3a0.lc.fatal) {
			/*
			 * Fail-closed: the DMA engine could not be verified
			 * stopped. Keep probe SUCCESSFUL so the device stays
			 * bound and @hw (devres) is retained -- the fatal state
			 * and the retained DMA memory must not silently
			 * disappear through a failed probe/unbind. ob_d3a0_test()
			 * latched a module-wide re-entry block and pinned the
			 * module; only a reboot clears it.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": dma-test FATAL unverified quiesce; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			/* No live DMA resources (teardown ran or none created). */
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * d11_tail_test_only: isolated D3A1 vendor post-common / pre-PHY D11
	 * tail test. Runs the proven D2A/D2B core, the exact rev42 tail
	 * (sub_67efd, T1, DMA in vendor position, T2, switch_macfreq), the
	 * deterministic postconditions and the mandatory verified quiesce, then
	 * STOPS before sub_6656c/bsinitvals/PHY/radio/channel. It never
	 * registers mac80211 and never enables EN_MAC or the host IRQ route.
	 */
	if (mode == OB_ISOLATED_D3A1_TEST) {
		hw->d3a1_test_only = true;
		/*
		 * Minimal board-data preparation (ChipCommon pointer + validated
		 * external-SPROM MAC) BEFORE D2A/D2B. It must not run the
		 * normal ob_si_probe() path.
		 */
		ret = ob_si_prepare_board_data_for_d3a1(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_d3a1_test(hw);
		if (hw->d3a0.lc.fatal) {
			/* Fail-closed: keep the device bound and the DMA memory
			 * retained; ob_d3a1_test latched a module-wide re-entry
			 * block and pinned the module. Only a reboot clears it.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": d3a1-test FATAL unverified quiesce; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * bsinitvals_test_only: isolated D3B band-init test. Runs the proven
	 * D2A/D2B core and the exact D3A1 vendor prefix with the D3A0 DMA engines
	 * left LIVE (vendor order T1 -> DMA -> T2), then the exact sub_6656c MHF
	 * writes + d11ac1bsinitvals42 (73 records), the deterministic
	 * postconditions and the mandatory verified quiesce, then STOPS before
	 * wlc_phy_init/PHY/radio/channel. It never registers mac80211 and never
	 * enables EN_MAC or the host IRQ route.
	 */
	if (mode == OB_ISOLATED_D3B_TEST) {
		hw->bsinitvals_test_only = true;
		/*
		 * Minimal board-data preparation (ChipCommon pointer + validated
		 * external-SPROM MAC and rev11 board fields) BEFORE D2A/D2B. It
		 * must not run the normal ob_si_probe() path.
		 */
		ret = ob_si_prepare_board_data_for_d3b(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_d3b_test(hw);
		if (hw->d3a0.lc.fatal) {
			/* Fail-closed: keep the device bound and the DMA memory
			 * retained; ob_d3b_test latched a module-wide re-entry
			 * block and pinned the module. Only a reboot clears it.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": d3b-test FATAL unverified quiesce; device kept bound, reboot required\n");
			return 0;
		}
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
	 * sprom_evidence_only is read-only and initialized no platform
	 * resources; skip every teardown step.
	 */
	if (hw->sprom_evidence_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (sprom-evidence; nothing to tear down)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

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

	/*
	 * dma_test_only owns its own DMA lifecycle. ob_d3a0_remove() applies the
	 * fail-closed policy: it never frees DMA memory while hardware may still
	 * consume it, and enters a reboot-required fatal state otherwise.
	 */
	if (hw->dma_test_only) {
		ob_d3a0_remove(hw);
		return;
	}

	/*
	 * d3a1_test_only owns its own DMA lifecycle (shared with D3A0, in the
	 * vendor T1 -> DMA -> T2 position). ob_d3a1_remove() applies the same
	 * fail-closed policy.
	 */
	if (hw->d3a1_test_only) {
		ob_d3a1_remove(hw);
		return;
	}

	/*
	 * bsinitvals_test_only owns its own DMA lifecycle (shared with D3A0, in
	 * the vendor T1 -> DMA -> T2 -> D3B position). ob_d3b_remove() applies
	 * the same fail-closed policy.
	 */
	if (hw->bsinitvals_test_only) {
		ob_d3b_remove(hw);
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

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — BCM2069 radio identity observation (D4-BLOCKER-PLL-BRANCH-HW-PROBE).
 *
 * Smallest isolated probe that reads the radio revision from the D11 radio
 * indirect window and stops. It is deliberately NOT a normal bring-up path and
 * NOT a radio register dump: exactly two selector writes (radio reg 0 and 1)
 * and two 16-bit data reads, plus one trusted accessibility sentinel before and
 * after. It never issues a PLL sequence, calibration, channel, DMA, IRQ, PHY
 * table, TX/RX or mac80211 operation.
 *
 * Minimum prerequisite chain (see docs/m34d4/radio_probe_design.md Part A):
 * host/backplane up + D11 core enable + FAST clock + D11 MAC-PHY clock
 * (SICF_MPCLKE). The vendor reads radio reg 0/1 in wlc_phy_attach, reached
 * from wlc_bmac_attach BEFORE ucode/initvals/clkctl_init/DMA, so none of the
 * heavier stages (ucode, PSM, common initvals, D3A1, DMA, D3B MHF, bsinitvals,
 * PHY init) is required. wlc_bmac_core_phypll_ctl is a proven no-op for rev42.
 *
 * All register access goes through the central dev_lost guard. After the
 * monotonic latch no MMIO is issued and the module is pinned; reboot clears it.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/pci.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_phy.h"
#include "ob_radio.h"
#include "ob_radio_post0413.h"
#include "ob_radio_postbmac.h"
#include "ob_radio_postphase1.h"
#include "ob_radio_postphase2.h"
#include "ob_ucode.h"
#include "ob_d3a0.h"
#include "ob_si.h"
#include "ob_phy.h"

#define OB_RADIO_D11_MACCONTROL 0x0120u

#ifndef OB_DL_BUILD_ID
#define OB_DL_BUILD_ID "UNAVAILABLE"
#endif

#if defined(CONFIG_X86_64)
#define OB_DL_ARCHITECTURE "x86_64"
#elif defined(CONFIG_X86_32)
#define OB_DL_ARCHITECTURE "x86_32"
#elif defined(CONFIG_ARM64)
#define OB_DL_ARCHITECTURE "arm64"
#elif defined(CONFIG_ARM)
#define OB_DL_ARCHITECTURE "arm"
#else
#define OB_DL_ARCHITECTURE "UNAVAILABLE"
#endif

static const char *ob_radio_dl_text(const char *value)
{
	return value ? value : "UNAVAILABLE";
}

/* The transport is deliberately a stable key/value record, not free-form
 * prose.  The source core has no kernel dependency; this sink is the only
 * production-specific part of the emitter. */
static int ob_radio_dl_metadata(void *ctx,
				const struct ob_dl_metadata *metadata)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->dev)
		return -ENODEV;
	dev_emerg(hw->dev,
		  "openbrcm-dl:v1 kind=meta schema=%s candidate_id=%s source_commit=%s build_id=%s module_version=%s architecture=%s diagnostic_flags=%s observer_set=%s\n",
		  ob_radio_dl_text(metadata->schema),
		  ob_radio_dl_text(metadata->candidate_id),
		  ob_radio_dl_text(metadata->source_commit),
		  ob_radio_dl_text(metadata->build_id),
		  ob_radio_dl_text(metadata->module_version),
		  ob_radio_dl_text(metadata->architecture),
		  ob_radio_dl_text(metadata->diagnostic_flags),
		  ob_radio_dl_text(metadata->observer_set));
	return 0;
}

static int ob_radio_dl_observer(void *ctx,
				const struct ob_dl_observer_def *observer)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->dev)
		return -ENODEV;
	dev_emerg(hw->dev,
		  "openbrcm-dl:v1 kind=observer observer_id=%s layer=%s admission=%s side_effect=%s dependency=%s remains_after_loss=%d implemented=%d enabled=%d reason=%s\n",
		  ob_radio_dl_text(observer->observer_id),
		  ob_radio_dl_text(observer->layer),
		  ob_dl_admission_name(observer->admission),
		  ob_dl_side_effect_name(observer->side_effect),
		  ob_radio_dl_text(observer->dependency),
		  observer->remains_admissible_after_loss ? 1 : 0,
		  observer->implemented ? 1 : 0,
		  observer->enabled ? 1 : 0,
		  ob_radio_dl_text(observer->reason));
	return 0;
}

static int ob_radio_dl_record(void *ctx, const struct ob_dl_record *record)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->dev)
		return -ENODEV;
	dev_emerg(hw->dev,
		  "openbrcm-dl:v1 kind=record record_kind=%s seq=%u timestamp_ns=%llu phase=%s id=%s layer=%s observer_id=%s admission=%s side_effect=%s access=%s address_space=%s offset=0x%04x width=%u base_valid=%d base_offset=0x%04x write_valid=%d write_value=0x%llx raw_valid=%d raw_value=0x%llx abnormal=%d classification=%s trusted_source=%d device_lost_before=%d device_lost_after=%d first_abnormal=%d status=%s access_rc_valid=%d access_rc=%d provenance=%s reason=%s\n",
		  ob_dl_record_kind_name(record->kind), record->seq,
		  record->timestamp_ns, ob_dl_phase_name(record->phase),
		  ob_radio_dl_text(record->id),
		  ob_dl_layer_name(record->layer),
		  ob_radio_dl_text(record->observer_id),
		  ob_dl_admission_name(record->admission),
		  ob_dl_side_effect_name(record->side_effect),
		  ob_dl_access_name(record->access),
		  ob_radio_dl_text(record->address_space), record->offset,
		  record->width, record->base_valid ? 1 : 0, record->base_offset,
		  record->write_valid ? 1 : 0,
		  record->write_value, record->raw_valid ? 1 : 0,
		  record->raw_value, record->abnormal ? 1 : 0,
		  ob_radio_dl_text(record->classification),
		  record->trusted_source ? 1 : 0,
		  record->device_lost_before ? 1 : 0,
		  record->device_lost_after ? 1 : 0,
		  record->first_abnormal ? 1 : 0,
		  ob_dl_status_name(record->status),
		  record->access_rc_valid ? 1 : 0, record->access_rc,
		  ob_radio_dl_text(record->provenance),
		  ob_radio_dl_text(record->reason));
	return 0;
}

static const struct ob_dl_trace_sink ob_radio_dl_sink = {
	.metadata = ob_radio_dl_metadata,
	.observer = ob_radio_dl_observer,
	.record = ob_radio_dl_record,
};

static unsigned long long ob_radio_dl_now(void *ctx)
{
	(void)ctx;
	return (unsigned long long)ktime_get_mono_fast_ns();
}

static const struct ob_dl_trace_clock ob_radio_dl_clock = {
	.now_ns = ob_radio_dl_now,
};

/* ---- backend: central dev_lost-guarded D11 window ----------------------- */

static u32 ob_radio_io_read32_trusted(void *ctx, u16 off)
{
	struct ob_hw *hw = ctx;

	/* The trace hook must see the raw value before the trusted latch is
	 * changed.  The untraced path retains the existing accessor semantics. */
	if (ob_dl_trace_is_active(&hw->dl_trace))
		return ob_d11_read32(hw, off);
	return ob_d11_read32_trusted(hw, "radio-probe", off);
}

static u16 ob_radio_io_read16(void *ctx, u16 off)
{
	return ob_d11_read16((struct ob_hw *)ctx, off);
}

static void ob_radio_io_write16(void *ctx, u16 off, u16 val)
{
	ob_d11_write16((struct ob_hw *)ctx, off, val);
}

static const struct ob_radio_io_ops ob_radio_io = {
	.read32_trusted = ob_radio_io_read32_trusted,
	.read16 = ob_radio_io_read16,
	.write16 = ob_radio_io_write16,
};

static int ob_radio_dl_observe_ex(struct ob_hw *hw, enum ob_dl_phase phase,
					  const char *id, enum ob_dl_layer layer,
					  const char *observer_id,
					  enum ob_dl_side_effect side_effect,
					  enum ob_dl_access access,
					  const char *address_space, u16 off, u32 width,
					  bool base_valid, u16 base_offset,
					  bool write_valid, unsigned long long write_value,
					  bool raw_valid, unsigned long long raw_value,
					  bool abnormal, const char *classification,
					  bool trusted_source, bool trusted_loss,
					  enum ob_dl_status status,
					  bool access_rc_valid, int access_rc,
					  const char *provenance, const char *reason)
{
	struct ob_dl_observation observation;
	int ret;

	if (!ob_dl_trace_is_active(&hw->dl_trace))
		return 0;
	observation.kind = OB_DL_RECORD_EVENT;
	observation.phase = phase;
	observation.id = id;
	observation.layer = layer;
	observation.observer_id = observer_id;
	observation.admission = OB_DL_ADMISSION_ADMITTED;
	observation.side_effect = side_effect;
	observation.access = access;
	observation.address_space = address_space;
	observation.offset = off;
	observation.width = width;
	observation.base_valid = base_valid;
	observation.base_offset = base_offset;
	observation.write_valid = write_valid;
	observation.write_value = write_value;
	observation.raw_valid = raw_valid;
	observation.raw_value = raw_value;
	observation.abnormal = abnormal;
	observation.classification = classification;
	observation.trusted_source = trusted_source;
	observation.trusted_loss = trusted_loss;
	observation.status = status;
	observation.access_rc_valid = access_rc_valid;
	observation.access_rc = access_rc;
	observation.provenance = provenance;
	observation.reason = reason;
	ret = ob_dl_trace_record(&hw->dl_trace, &observation);
	/* The raw event is already in the sink. Only now may the trusted result
	 * change the central device-loss latch. */
	if (trusted_loss)
		ob_dev_lost_latch(hw, id);
	return ret;
}

static int ob_radio_dl_observe(struct ob_hw *hw, enum ob_dl_phase phase,
				       const char *id, enum ob_dl_layer layer,
				       const char *observer_id,
				       enum ob_dl_side_effect side_effect,
				       enum ob_dl_access access,
				       const char *address_space, u16 off, u32 width,
				       bool write_valid, unsigned long long write_value,
				       bool raw_valid, unsigned long long raw_value,
				       bool abnormal, const char *classification,
				       bool trusted_source, bool trusted_loss,
				       const char *provenance)
{
	return ob_radio_dl_observe_ex(hw, phase, id, layer, observer_id,
					     side_effect, access, address_space, off, width,
					     false, 0, write_valid, write_value, raw_valid,
					     raw_value, abnormal, classification, trusted_source,
					     trusted_loss, OB_DL_STATUS_OK, false, 0, provenance,
					     "EXISTING_ACCESS_WRAPPED_ONLY");
}

static int ob_radio_dl_unavailable(struct ob_hw *hw, enum ob_dl_phase phase,
					   const char *id, enum ob_dl_layer layer,
					   const char *observer_id,
					   const char *address_space, u16 off, u32 width,
					   const char *reason)
{
	struct ob_dl_observation observation = {
		.kind = OB_DL_RECORD_EVENT,
		.phase = phase,
		.id = id,
		.layer = layer,
		.observer_id = observer_id,
		.admission = OB_DL_ADMISSION_UNAVAILABLE,
		.side_effect = OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
		.access = OB_DL_ACCESS_NONE,
		.address_space = address_space,
		.offset = off,
		.width = width,
		.base_valid = false,
		.base_offset = 0,
		.write_valid = false,
		.write_value = 0,
		.raw_valid = false,
		.raw_value = 0,
		.abnormal = false,
		.classification = "UNAVAILABLE",
		.trusted_source = false,
		.trusted_loss = false,
		.status = OB_DL_STATUS_UNAVAILABLE,
		.access_rc_valid = false,
		.access_rc = 0,
		.provenance = "observer-admission",
		.reason = reason,
	};

	if (!ob_dl_trace_is_active(&hw->dl_trace))
		return 0;
	return ob_dl_trace_record(&hw->dl_trace, &observation);
}

static int ob_radio_dl_phase(void *ctx, enum ob_radio_stage stage)
{
	struct ob_hw *hw = ctx;
	enum ob_dl_phase phase;
	int ret;

	switch (stage) {
	case OB_RADIO_STAGE_PRE_ACCESS:
		phase = OB_DL_PHASE_PRE;
		break;
	case OB_RADIO_STAGE_SELECTOR0_WRITE:
		phase = OB_DL_PHASE_RADIO_SELECTOR_0;
		break;
	case OB_RADIO_STAGE_REG0_READ:
		phase = OB_DL_PHASE_RADIO_DATA_0;
		break;
	case OB_RADIO_STAGE_SELECTOR1_WRITE:
		phase = OB_DL_PHASE_RADIO_SELECTOR_1;
		break;
	case OB_RADIO_STAGE_REG1_READ:
		phase = OB_DL_PHASE_RADIO_DATA_1;
		break;
	case OB_RADIO_STAGE_MID:
		phase = OB_DL_PHASE_MID;
		break;
	case OB_RADIO_STAGE_POST_ACCESS:
		phase = OB_DL_PHASE_SENTINEL;
		break;
	default:
		return -EINVAL;
	}
	ret = ob_dl_trace_transition(&hw->dl_trace, phase);
	if (ret)
		return ret;
	if (phase == OB_DL_PHASE_MID)
		return ob_dl_trace_note(&hw->dl_trace, phase, "mid.observers",
					"observer-admission", "PCI_CONFIG_LINK_OBSERVERS_RUN_ONCE");
	return 0;
}

/*
 * MID host observers. These are deliberately independent of the BCMA/D11
 * window: they use only the already-owned pci_dev and one config read per
 * admitted field. The access return code is retained separately from the raw
 * output because PCI may fill an error response (0xffffffff/0xffff) on a
 * failed config transaction. No observer here retries, writes config space,
 * maps a BAR, or latches device loss.
 */
static int ob_radio_dl_mid(void *ctx)
{
	struct ob_hw *hw = ctx;
	struct pci_dev *pdev;
	u32 vendor_device = 0;
	u16 link_status = 0;
	int cap;
	int rc;
	int ret;
	bool abnormal;
	const char *classification;

	if (!hw || !ob_dl_trace_is_active(&hw->dl_trace))
		return 0;
	if (hw->dev_lost || ob_dl_trace_is_lost(&hw->dl_trace))
		return -EIO;
	pdev = hw->bus ? hw->bus->host_pci : NULL;
	if (!pdev) {
		ret = ob_radio_dl_unavailable(hw, OB_DL_PHASE_MID,
					      "mid.pci.vendor_device", OB_DL_LAYER_L0,
					      "l0.pci.config", "PCI_CFG", PCI_VENDOR_ID,
					      32, "NO_HOST_PCI_DEVICE");
		if (ret)
			return ret;
		return ob_radio_dl_unavailable(hw, OB_DL_PHASE_MID,
					       "mid.pcie.link_status", OB_DL_LAYER_L1,
					       "l1.pcie.link", "PCI_EXP_CAP",
					       PCI_EXP_LNKSTA, 16, "NO_HOST_PCI_DEVICE");
	}

	rc = pci_read_config_dword(pdev, PCI_VENDOR_ID, &vendor_device);
	abnormal = rc != 0 || vendor_device == 0xffffffffu;
	classification = rc ? "PCI_CONFIG_ERROR" :
		(vendor_device == 0xffffffffu ? "PCI_CONFIG_ALL_ONES" :
		 "PCI_CONFIG_OK");
	ret = ob_radio_dl_observe_ex(hw, OB_DL_PHASE_MID,
					     "mid.pci.vendor_device", OB_DL_LAYER_L0,
					     "l0.pci.config",
					     OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
					     OB_DL_ACCESS_READ, "PCI_CFG", PCI_VENDOR_ID, 32,
					     false, 0, false, 0, rc == 0, vendor_device, abnormal,
					     classification, false, false,
					     rc ? OB_DL_STATUS_ERROR : OB_DL_STATUS_OK, true, rc,
					     "pci_read_config_dword", "MID_PCI_CONFIG_OBSERVER");
	if (ret)
		return ret;

	/* pci_pcie_cap() returns the cached enumeration-time offset. It does not
	 * walk the capability list or perform another config transaction. */
	cap = pci_pcie_cap(pdev);
	if (!cap)
		return ob_radio_dl_unavailable(hw, OB_DL_PHASE_MID,
					       "mid.pcie.link_status", OB_DL_LAYER_L1,
					       "l1.pcie.link", "PCI_EXP_CAP",
					       PCI_EXP_LNKSTA, 16,
					       "NO_CACHED_PCIE_CAPABILITY");

	rc = pci_read_config_word(pdev, cap + PCI_EXP_LNKSTA, &link_status);
	{
		struct ob_pcie_link_classification link =
			ob_classify_pcie_link_status(rc, link_status);

		abnormal = link.abnormal;
		classification = link.classification;
	}
	return ob_radio_dl_observe_ex(hw, OB_DL_PHASE_MID,
					     "mid.pcie.link_status", OB_DL_LAYER_L1,
					     "l1.pcie.link",
					     OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
					     OB_DL_ACCESS_READ, "PCI_EXP_CAP", PCI_EXP_LNKSTA, 16,
					     true, (u16)cap, false, 0, rc == 0, link_status, abnormal,
					     classification, false, false,
					     rc ? OB_DL_STATUS_ERROR : OB_DL_STATUS_OK, true, rc,
					     "pci_read_config_word_cached_pcie_cap",
					     "MID_PCIE_LINK_OBSERVER");
}

static int ob_radio_dl_read32(void *ctx, enum ob_radio_stage stage,
				      u16 off, u32 value)
{
	struct ob_hw *hw = ctx;
	const char *id;
	const char *classification;
	enum ob_dl_phase phase;
	bool lost = value == OB_GUARD_ALL_ONES32;

	if (stage == OB_RADIO_STAGE_PRE_ACCESS) {
		id = "pre.d11.maccontrol_0120";
		phase = OB_DL_PHASE_PRE;
	} else if (stage == OB_RADIO_STAGE_POST_ACCESS) {
		id = "sentinel.d11.maccontrol_0120";
		phase = OB_DL_PHASE_SENTINEL;
	} else {
		return -EINVAL;
	}
	classification = lost ? "DEVICE_LOST" : "NONE";
	return ob_radio_dl_observe(hw, phase, id, OB_DL_LAYER_L4,
					  "l4.d11.functional",
					  OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
					  OB_DL_ACCESS_READ, "D11_CORE", off, 32,
					  false, 0, true, value, lost, classification,
					  true, lost, "ob_d11_read32_trusted");
}

static int ob_radio_dl_read16(void *ctx, enum ob_radio_stage stage,
				      u16 off, u16 value)
{
	struct ob_hw *hw = ctx;
	enum ob_dl_phase phase;
	const char *id;
	bool abnormal = value == OB_GUARD_DEAD16;

	if (stage == OB_RADIO_STAGE_REG0_READ) {
		phase = OB_DL_PHASE_RADIO_DATA_0;
		id = "radio.data.0";
	} else if (stage == OB_RADIO_STAGE_REG1_READ) {
		phase = OB_DL_PHASE_RADIO_DATA_1;
		id = "radio.data.1";
	} else {
		return -EINVAL;
	}
	return ob_radio_dl_observe(hw, phase, id, OB_DL_LAYER_L6,
					  "l6.radio.window",
					  OB_DL_SIDE_EFFECT_STATE_CHANGING,
					  OB_DL_ACCESS_READ, "RADIO_WINDOW", off, 16,
					  false, 0, true, value, abnormal,
					  abnormal ? "ABNORMAL_RAW" : "NONE", false, false,
					  "ob_d11_read16");
}

static int ob_radio_dl_write16(void *ctx, enum ob_radio_stage stage,
				       u16 off, u16 value)
{
	struct ob_hw *hw = ctx;
	enum ob_dl_phase phase;
	const char *id;

	if (stage == OB_RADIO_STAGE_SELECTOR0_WRITE) {
		phase = OB_DL_PHASE_RADIO_SELECTOR_0;
		id = "radio.selector.0";
	} else if (stage == OB_RADIO_STAGE_SELECTOR1_WRITE) {
		phase = OB_DL_PHASE_RADIO_SELECTOR_1;
		id = "radio.selector.1";
	} else {
		return -EINVAL;
	}
	return ob_radio_dl_observe(hw, phase, id, OB_DL_LAYER_L6,
					  "l6.radio.window",
					  OB_DL_SIDE_EFFECT_STATE_CHANGING,
					  OB_DL_ACCESS_WRITE, "RADIO_WINDOW", off, 16,
					  true, value, false, 0, false, "NONE", false, false,
					  "ob_d11_write16");
}

static int ob_radio_dl_stop(void *ctx, bool lost)
{
	struct ob_hw *hw = ctx;

	if (!ob_dl_trace_is_active(&hw->dl_trace))
		return 0;
	return ob_dl_trace_stop(&hw->dl_trace,
				lost ? "TRUSTED_SENTINEL_DEVICE_LOST" :
				       "TRACE_STOP_FAIL_CLOSED");
}

static int __maybe_unused ob_radio_dl_prepare_note(struct ob_hw *hw, const char *id,
					    const char *reason)
{
	if (!ob_dl_trace_is_active(&hw->dl_trace))
		return 0;
	return ob_dl_trace_note(&hw->dl_trace, OB_DL_PHASE_PRE, id,
					"existing-preparation", reason);
}

static int __maybe_unused ob_radio_dl_prepare_read32(struct ob_hw *hw, const char *id,
					      enum ob_dl_layer layer,
					      const char *observer_id,
					      const char *address_space, u16 off,
					      u32 value)
{
	bool abnormal = value == OB_GUARD_ALL_ONES32;

	return ob_radio_dl_observe(hw, OB_DL_PHASE_PRE, id, layer,
					  observer_id,
					  OB_DL_SIDE_EFFECT_READ_ONLY_STATE_SENSITIVE,
					  OB_DL_ACCESS_READ, address_space, off, 32,
					  false, 0, true, value, abnormal,
					  abnormal ? "ABNORMAL_RAW" : "NONE", false, false,
					  "existing-preparation-access");
}

static int __maybe_unused ob_radio_dl_prepare_write32(struct ob_hw *hw, const char *id,
					       const char *observer_id, u16 off,
					       u32 value)
{
	return ob_radio_dl_observe(hw, OB_DL_PHASE_PRE, id, OB_DL_LAYER_L3,
					  observer_id,
					  OB_DL_SIDE_EFFECT_STATE_CHANGING,
					  OB_DL_ACCESS_WRITE, "BCMA_WRAPPER", off, 32,
					  true, value, false, 0, false, "NONE", false, false,
					  "existing-preparation-access");
}

static const struct ob_radio_trace_hooks ob_radio_dl_hooks = {
	.phase = ob_radio_dl_phase,
	.mid = ob_radio_dl_mid,
	.read32 = ob_radio_dl_read32,
	.read16 = ob_radio_dl_read16,
	.write16 = ob_radio_dl_write16,
	.stop = ob_radio_dl_stop,
};

static const char *ob_radio_stage_name(enum ob_radio_stage stage)
{
	switch (stage) {
	case OB_RADIO_STAGE_PRE_ACCESS: return "pre_access";
	case OB_RADIO_STAGE_SELECTOR0_WRITE: return "selector0_write";
	case OB_RADIO_STAGE_REG0_READ: return "reg0_read";
	case OB_RADIO_STAGE_SELECTOR1_WRITE: return "selector1_write";
	case OB_RADIO_STAGE_REG1_READ: return "reg1_read";
	case OB_RADIO_STAGE_MID: return "mid";
	case OB_RADIO_STAGE_POST_ACCESS: return "post_access";
	default: return "none";
	}
}

/*
 * Minimum proven prerequisite (M2.5 order, shared by the isolated test modes)
 * plus the D11 MAC-PHY clock gate that the vendor corereset applies on the AC
 * path before the radio read. No PHY/radio/PLL register is written.
 */
static int ob_radio_vendor_rmw_wrapper(struct ob_hw *hw, u32 mask, u32 value)
{
	u32 old, now;

	if (!hw || hw->dev_lost)
		return -EIO;
	old = ob_axi_read32(hw, BCMA_IOCTL);
	if (hw->dev_lost || old == OB_GUARD_ALL_ONES32)
		return -EIO;
	now = (old & ~mask) | (value & mask);
	ob_axi_write32(hw, BCMA_IOCTL, now);
	if (hw->dev_lost)
		return -EIO;
	if (ob_axi_read32(hw, BCMA_IOCTL) != now)
		return -EIO;
	return 0;
}

/*
 * Pre-identity PHY access for the relocated S1 core-mirror block. Reads keep
 * the vendor's 16-bit selector + data-port pair; writes use the vendor's single
 * 32-bit latch form, so the trace shape and the MMIO count per pass match the
 * vendor's mirror block (boot 64536612..., t = 531.533405 and 531.535567).
 */
static int ob_radio_mirror_read16(void *ctx, u16 reg, u16 *value)
{
	return ob_phy_read16_pre_id(ctx, reg, value);
}

static int ob_radio_mirror_write16(void *ctx, u16 reg, u16 value)
{
	return ob_phy_write16_atomic_pre_id(ctx, reg, value);
}

static const struct ob_phy_access_ops ob_radio_mirror_ops = {
	.read16 = ob_radio_mirror_read16,
	.write16 = ob_radio_mirror_write16,
};

/* Exact BCM4352 rev3 / D11 rev42 first-attach prefix. */
static int ob_radio_vendor_pre_id(struct ob_hw *hw)
{
	u32 ioctl;
	u32 reset_ctl;
	bool wrapper_valid;
	bool enabled;
	int err;

	if (hw->dev_lost)
		return -EIO;
	/* Linux BCMA's per-core objects subsume vendor SI core selection. */
	hw->cc = hw->bus->drv_cc.core;
	if (!hw->cc || hw->cc->id.id != BCMA_CORE_CHIPCOMMON)
		return -ENODEV;
	if (hw->core->id.id != BCMA_CORE_80211 || hw->core->id.rev != 42)
		return -ENODEV;

	bcma_host_pci_up(hw->bus);
	/*
	 * Vendor-faithful D11 enable: the attach-prefix dance carries flags 0x4, so
	 * the wrapper IOCTL goes 0x7 -> 0x5 and the persistent bit 2 stays set. With
	 * flags=0 the same code path writes 0x3 -> 0x1, which is what every failed
	 * run measured before the 0x3d8..0x3fb island stall.
	 *
	 * Promoted from the P1-A witness on 2026-10-28 after the A-only window
	 * measured it sufficient on its own: wrapper 0x408 = 0x5 and D11 0x3e0 =
	 * 0xcb01 in <=4 us with no SHM probe
	 * (audit/.../acphy_p1_candidates_run01/exec-20261028T005446Z, verdict
	 * P1_A_ONLY_LIVE). The readback below is a hard gate, not a log line.
	 */
	err = bcma_core_enable(hw->core, 0x4u);
	if (err)
		return err;
	/* BCMA owns reset/enable writes; admit only a valid enabled postcondition. */
	ioctl = ob_axi_read32(hw, BCMA_IOCTL);
	reset_ctl = ob_axi_read32(hw, BCMA_RESET_CTL);
	wrapper_valid = !hw->dev_lost &&
		ioctl != OB_GUARD_ALL_ONES32 &&
		reset_ctl != OB_GUARD_ALL_ONES32;
	if (!wrapper_valid)
		ob_dev_lost_latch(hw, "attach reset postcondition");
	enabled = wrapper_valid &&
		((ioctl & (BCMA_IOCTL_CLK | BCMA_IOCTL_FGC)) ==
		 BCMA_IOCTL_CLK) &&
		!(reset_ctl & BCMA_RESET_CTL_RESET);
	err = ob_attach_core_reset_postcondition(0, wrapper_valid, enabled);
	if (err) {
		dev_info(hw->dev,
			 "OB_ATTACH_FAIL stage=RESET_POSTCONDITION ret=%d\n", err);
		return err;
	}
	dev_info(hw->dev,
		 "OB_ATTACH_CP1_RESET_READY ioctl=0x%08x reset_ctl=0x%08x enabled=1\n",
		 ioctl, reset_ctl);
	/*
	 * The persistent bit 0x4 is the vendor's postcondition and the measured
	 * discriminator between a live and a dead 0x3e0 read, so a readback other
	 * than 0x5 fails closed here, before any D11 functional access.
	 */
	dev_info(hw->dev,
		 "OB_ATTACH_CP1_IOCTL4 wrapper_0x408=0x%08x expect=0x00000005 verdict=%s\n",
		 ioctl, ioctl == 0x5u ? "ok" : "FAIL");
	if (ioctl != 0x5u) {
		dev_err(hw->dev,
			"wrapper_0x408=0x%08x does not carry the vendor's persistent bit 0x4 (expect 0x5); the 0x3e0 read is not issued\n",
			ioctl);
		dev_info(hw->dev, "OB_ATTACH_FAIL stage=WRAPPER_IOCTL_BIT4 ret=-5\n");
		return -EIO;
	}
	/*
	 * PM1 of the point witness: the first legal PHY read of this boot, taken
	 * after the wrapper gate and before every other attach access. The prefix
	 * issues no PHY or radio write, so PM1 answers whether the 0x07xx tuple
	 * already exists before any PHY access of the attach path; the only
	 * difference from P0 is the optional SHM round-trip.
	 */
	if (hw->phy_tripoint_probe) {
		err = ob_phy_tripoint_observe(hw, OB_PHY_TRIPOINT_PM1);
		if (err) {
			dev_info(hw->dev, "TRIPOINT_STOP point=PM1 ret=%d\n", err);
			return err;
		}
	}
	/*
	 * S1 relocated (2026-10-28): the vendor runs the core0 -> core1 mirror
	 * block 11.7 ms after the prologue read series and 80 ms before its LPF
	 * register block, with the whole 0x164 table stream in between
	 * (audit/.../phy_prefix_point01/ORDERING_DELTA.md). It belongs here, in the
	 * pre-identity prefix. Scheduled after post-ucode, as it was before, it
	 * mirrored the post-ucode generation of the tuple (0x0725 = 0x0600,
	 * 0x0408 = 0x0d82), and the LPF prefix's own 0x1725 = 0x0000 write then
	 * erased the 0x0600 the device had produced, leaving 0x0725 = 0x0400 where
	 * the vendor ends at 0x0600.
	 *
	 * Two passes, vendor order, fail-stop. No value predicate: this is a
	 * value-identity copy of the prologue tuple by construction.
	 */
	err = ob_phy_acphy_core_mirror_stage_run(&ob_radio_mirror_ops, hw);
	if (err) {
		dev_info(hw->dev,
			 "OB_ATTACH_FAIL stage=CORE_MIRROR ret=%d\n", err);
		return err;
	}
	dev_info(hw->dev, "ACPHY_CORE_MIRROR_DONE passes=2 alias=0x17xx\n");
	/*
	 * Everything below the BCMA-owned reset admit is PHY-type dependent and
	 * therefore stays behind the authoritative identity read in
	 * ob_acphy_attach_identity(): wlc_bmac_phy_reset's AC arm (0x65da6),
	 * the wlc_bmac_core_phy_clk pair (0x650d4/0x650f5/0x65142), the reset-path
	 * wlc_phy_anacore write (0x65e04) and the wlc_bmac_up_finish clock-off
	 * (0x66439). This prefix issues no D11 read and no PHY/radio register write.
	 */
	return 0;
}

/*
 * wlc_phy_attach 0xbed73..0xbed85: for every PHY type except 0x0a, the attach
 * path issues wlc_phy_anacore(pi, 1). The AC type takes the no-hook arm of
 * wlc_phy_anacore (0xbabcb..0xbabee): ONE 16-bit osl_writew of 0x0000 to the
 * D11 window at pi+0x148 + 0x3e6. This is a DIFFERENT event from the
 * wlc_bmac_phy_reset call at 0x65e04, which ob_radio_vendor_acphy_reset()
 * models separately. The pi+0x118 hook is NULL on this path
 * (FALSIFICATION_PROOF.md T2), so no indirect dispatch is possible.
 */
int ob_radio_vendor_acphy_attach_anacore(struct ob_hw *hw, u8 phy_type)
{
	if (!hw || hw->dev_lost)
		return -EIO;
	if (phy_type == 0x0au)	/* LCN target arm at 0xbed73 */
		return 0;
	ob_d11_write16(hw, 0x03e6u, 0x0000u);
	if (hw->dev_lost)
		return -EIO;
	return 0;
}

/*
 * Effective AC-PHY reset: wlc_bmac_corereset 0x65f59..0x66004 followed by the
 * wlc_bmac_phy_reset AC arm 0x659de..0x65e04. Reachable only with band->pi
 * published (0x6a140) and band+0x1c == 0x0b (0x6a19c), i.e. strictly AFTER the
 * attach-path PHYVERSION read, so it is issued only once the identity has been
 * read and published.
 */
int ob_radio_vendor_acphy_reset(struct ob_hw *hw, u8 phy_type)
{
	u32 clkctl;
	u16 cflags_mask;
	u16 cflags_value;
	int err;

	if (!hw || hw->dev_lost)
		return -EIO;
	if ((err = ob_radio_vendor_rmw_wrapper(hw, 0x300, 0x100)))
		return err;
	if ((err = ob_radio_vendor_rmw_wrapper(hw, 0x006, 0x000)))
		return err;
	if ((err = ob_radio_vendor_rmw_wrapper(hw, 0x004, 0x004)))
		return err;

	ob_d11_write32(hw, OB_RADIO_D11_MACCONTROL, 0x04000400u);
	if (hw->dev_lost)
		return -EIO;

	/*
	 * wlc_bmac_phy_reset 0x65da6..0x65dd3: ONE si_core_cflags word whose mask
	 * and value are selected by PHY type, AC value = bw | 0x0e. The 0xcc/0x0c
	 * pair is the mutually exclusive `jne` arm at 0x65dc0 and must NOT also be
	 * issued on the AC path.
	 */
	ob_acphy_reset_arm(phy_type, OB_ACPHY_BW_STATE_ATTACH, &cflags_mask,
			   &cflags_value);
	if ((err = ob_radio_vendor_rmw_wrapper(hw, cflags_mask, cflags_value)))
		return err;
	udelay(2);
	if ((err = ob_radio_vendor_rmw_wrapper(hw, 0x300, 0x100)))
		return err;
	if ((err = ob_radio_vendor_rmw_wrapper(hw, 0x00e, 0x000)))
		return err;
	udelay(1);
	if ((err = ob_radio_vendor_rmw_wrapper(hw, 0x004, 0x004)))
		return err;
	udelay(1);
	/*
	 * wlc_phy_anacore default arm 0xbabce..0xbabee: ONE direct 16-bit write of
	 * 0x0000 to the D11 register window at +0x3e6. It is NOT issued through
	 * the 0x3fc/0x3fe indirect PHY-register window.
	 */
	ob_d11_write16(hw, 0x03e6u, 0x0000u);
	if (hw->dev_lost)
		return -EIO;

	/*
	 * wlc_bmac_up_finish 0x66439 -> 0x64887(hw, 2): clock-off. PROVEN arm A.
	 * The arm selector at 0x648a1 tests cccaps bit 28 (CC_CAP_PMU); the target
	 * reads cccaps = 0x58680001 (bit 28 set), so the D11 +0x1e0 window is the
	 * taken arm and the ChipCommon CLKCTLST form is NOT used. One read plus one
	 * write clearing FORCEHT (0x2); the availability poll at 0x64940 is skipped
	 * because pmurev != 0 (0x64921). See FALSIFICATION_PROOF.md T1.
	 */
	clkctl = ob_d11_read32(hw, 0x01e0u);
	if (hw->dev_lost)
		return -EIO;
	ob_d11_write32(hw, 0x01e0u, clkctl & ~0x2u);
	if (hw->dev_lost)
		return -EIO;
	return 0;
}

int ob_radio_prepare_for_identity(struct ob_hw *hw)
{
	int ret;

	if (ob_dl_trace_is_active(&hw->dl_trace)) {
		ret = ob_dl_trace_transition(&hw->dl_trace, OB_DL_PHASE_PRE);
		if (ret)
			return ret;
	}
	ret = ob_radio_vendor_pre_id(hw);
	if (ret && ob_dl_trace_is_active(&hw->dl_trace))
		ob_dl_trace_stop(&hw->dl_trace, "PREPARATION_ERROR");
	return ret;
}

int ob_radio_read_identity(struct ob_hw *hw,
		struct ob_radio_observation *observation)
{
	struct ob_radio_seq_result result;
	int ret;

	if (!hw || !observation || hw->dev_lost)
		return -22;
	ret = ob_radio_sequence(&ob_radio_io, hw, &hw->dev_lost, &result);
	if (ret)
		return ret;
	if (!result.valid || !result.obs.id_is_2069)
		return -95;
	*observation = result.obs;
	return 0;
}

static int ob_radio_access_read16(void *ctx, u16 reg, u16 *value)
{
	return ob_radio_read16(ctx, reg, value);
}

static int ob_radio_access_write16(void *ctx, u16 reg, u16 value)
{
	return ob_radio_write16(ctx, reg, value);
}

int ob_radio_read16(struct ob_hw *hw, u16 reg, u16 *value)
{
	if (!hw || !value || hw->dev_lost || !hw->radio_access_initialized ||
		hw->d11_state != OB_D11_POST_UCODE_READY)
		return -22;
	ob_d11_write16(hw, OB_RADIO_REG_ADDR_LATCH, reg);
	if (hw->dev_lost)
		return -5;
	*value = ob_d11_read16(hw, OB_RADIO_REG_DATA);
	if (hw->dev_lost || *value == 0xffffu)
		return -5;
	return 0;
}

int ob_radio_write16(struct ob_hw *hw, u16 reg, u16 value)
{
	if (!hw || hw->dev_lost || !hw->radio_access_initialized ||
		hw->d11_state != OB_D11_POST_UCODE_READY)
		return -22;
	ob_d11_write16(hw, OB_RADIO_REG_ADDR_LATCH, reg);
	if (hw->dev_lost)
		return -5;
	ob_d11_write16(hw, OB_RADIO_REG_DATA, value);
	return hw->dev_lost ? -5 : 0;
}

static const char *ob_radio_local_0413_stage_name(
	enum ob_radio_local_0413_stage stage)
{
	switch (stage) {
	case OB_RADIO_LOCAL_0413_STAGE_MAC_QUIESCE:
		return "MAC_QUIESCE";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_8EA:
		return "RADIO_RMW_8EA";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_8ED:
		return "RADIO_RMW_8ED";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_A:
		return "RADIO_RMW_410_A";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_B:
		return "RADIO_RMW_410_B";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_411_SETUP:
		return "RADIO_RMW_411_SETUP";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_WRITE_412:
		return "RADIO_WRITE_412";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_CLEAR:
		return "RADIO_RMW_410_CLEAR";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_SET:
		return "RADIO_RMW_410_SET";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_411_SET:
		return "RADIO_RMW_411_SET";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_0413_READ:
		return "RADIO_0413_READ";
	case OB_RADIO_LOCAL_0413_STAGE_RADIO_0413_TIMEOUT:
		return "RADIO_0413_TIMEOUT";
	case OB_RADIO_LOCAL_0413_STAGE_DEVICE_LOST:
		return "DEVICE_LOST";
	default:
		return "UNKNOWN";
	}
}

static int ob_radio_local_0413_quiesce(void *ctx, bool *transitioned,
					       u32 *polls)
{
	struct ob_hw *hw = ctx;
	int ret;

	ret = ob_d11_quiesce_for_radio(hw);
	if (transitioned)
		*transitioned = hw && hw->radio_stage_owns_suspend;
	if (polls)
		*polls = hw ? hw->radio_quiesce_polls : 0;
	if (ret) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_MAC_QUIESCE result=FAILED polls=%u\n",
			 hw ? hw->radio_quiesce_polls : 0);
		return ret;
	}
	dev_info(hw->dev,
		 "ACPHY_CLASS0_0413_MAC_QUIESCE result=%s polls=%u\n",
		 hw->radio_stage_owns_suspend ? "TRANSITIONED" :
		 "ALREADY_QUIESCED", hw->radio_quiesce_polls);
	dev_info(hw->dev, "ACPHY_CLASS0_0413_PRE_MUTATION\n");
	return 0;
}

static int ob_radio_local_0413_read(void *ctx, u16 reg, u16 *value)
{
	return ob_radio_read16(ctx, reg, value);
}

static int ob_radio_local_0413_write(void *ctx, u16 reg, u16 value)
{
	return ob_radio_write16(ctx, reg, value);
}

static void ob_radio_local_0413_delay(void *ctx, u32 usec)
{
	(void)ctx;
	udelay(usec);
}

static bool ob_radio_local_0413_lost(void *ctx)
{
	struct ob_hw *hw = ctx;

	return !hw || hw->dev_lost;
}

int ob_radio_local_0413_test(struct ob_hw *hw)
{
	struct ob_radio_local_0413_target target;
	struct ob_radio_local_0413_ops ops = {
		.quiesce = ob_radio_local_0413_quiesce,
		.read16 = ob_radio_local_0413_read,
		.write16 = ob_radio_local_0413_write,
		.delay_us = ob_radio_local_0413_delay,
		.device_lost = ob_radio_local_0413_lost,
	};
	struct ob_radio_local_0413_result result;
	u32 cflags;
	u16 shm;
	int ret;

	if (!hw)
		return -ENODEV;
	dev_info(hw->dev, "ACPHY_CLASS0_0413_CP0_ENTRY\n");
	if (hw->dev_lost || !hw->core ||
		hw->d11_state != OB_D11_POST_UCODE_READY ||
		hw->acphy_start_state != OB_ACPHY_START_READY_FOR_RADIO_ON) {
		dev_info(hw->dev, "ACPHY_CLASS0_0413_FAIL stage=ENTRY_STATE ret=-22\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		return -22;
	}
	shm = ob_ucode_read_shm16(hw, 0x00a0u);
	cflags = ob_axi_read32(hw, 0x0408u);
	target = (struct ob_radio_local_0413_target){
		.chip_id = hw->chip_id,
		.chip_rev = hw->chip_rev,
		.d11_corerev = hw->core->id.rev,
		.phy_type = hw->phy_type,
		.phy_revision = hw->phy_revision,
		.radio_id = hw->radio_id,
		.radio_revision = hw->radio_revision,
		.radio_revision_class = hw->radio_revision_class,
		.chanspec = hw->chanspec,
		.shm_00a0 = shm,
		.wrapper_cflags = cflags,
		.chanspec_valid = hw->chanspec_valid,
		.d11_identity_ready =
			 hw->phy_radio_identity_valid,
		.acphy_start_ready =
			hw->acphy_start_state == OB_ACPHY_START_READY_FOR_RADIO_ON,
		.d11_core_enabled = bcma_core_is_enabled(hw->core),
		.radio_access_initialized = hw->radio_access_initialized,
		.valid = hw->phy_radio_identity_valid && !hw->dev_lost,
	};
	if (!target.valid || !target.radio_access_initialized ||
	    target.chip_id != 0x4352u || target.chip_rev != 3u ||
	    target.d11_corerev != 42u || target.phy_type != 0x0bu ||
	    target.phy_revision != 1u || target.radio_id != 0x2069u ||
	    target.radio_revision != 4u || target.radio_revision_class != 0u ||
	    !target.chanspec_valid || target.chanspec != 0x1001u) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=TARGET_GATE ret=-22\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		return -22;
	}
	if (!target.d11_identity_ready || !target.acphy_start_ready) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=ENTRY_STATE ret=-22\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		return -22;
	}
	if (!target.d11_core_enabled || target.shm_00a0 != 0x1001u ||
	    (target.wrapper_cflags & 0x000000c0u) != 0x00000040u) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=PRE_RADIO_POSTCONDITION ret=-5\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		return -5;
	}
	if (!ob_radio_local_0413_target_ok(&target)) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=TARGET_GATE ret=-22\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		return -22;
	}
	dev_info(hw->dev, "ACPHY_CLASS0_0413_TARGET_OK\n");
	ret = ob_radio_local_0413_run(&ops, hw, &result);
	dev_info(hw->dev,
		 "ACPHY_CLASS0_0413_POLL attempts=%u last_value=0x%04x success=%u\n",
		 result.attempts, result.last_value, result.success ? 1 : 0);
	if (ret) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=%s ret=%d\n",
			 ob_radio_local_0413_stage_name(result.failure_stage), ret);
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		return ret;
	}
	dev_info(hw->dev, "ACPHY_CLASS0_0413_PASS\n");
	dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
	return 0;
}

static void ob_radio_post0413_marker(void *ctx,
				     enum ob_radio_post0413_marker marker,
				     u16 r414, u16 r415, u8 derived)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->dev)
		return;
	switch (marker) {
	case OB_RADIO_POST0413_MARK_CP0_ENTRY:
		dev_info(hw->dev, "ACPHY_CLASS0_POST0413_CP0_ENTRY\n");
		break;
	case OB_RADIO_POST0413_MARK_CP1_SAMPLES:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POST0413_CP1_SAMPLES r414=0x%04x r415=0x%04x derived=0x%02x\n",
			 r414, r415, derived);
		break;
	case OB_RADIO_POST0413_MARK_CP2_SETUP_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POST0413_CP2_PHASE1_SETUP_DONE\n");
		break;
	case OB_RADIO_POST0413_MARK_CP3_PRE_POLL:
		dev_info(hw->dev, "ACPHY_CLASS0_POST0413_CP3_PRE_POLL\n");
		break;
	}
}

static const char *ob_radio_post0413_stage_name(
	enum ob_radio_post0413_stage stage)
{
	switch (stage) {
	case OB_RADIO_POST0413_STAGE_INVALID_ARGUMENT:
		return "INVALID_ARGUMENT";
	case OB_RADIO_POST0413_STAGE_PHASE0_GATE:
		return "PHASE0_GATE";
	case OB_RADIO_POST0413_STAGE_DEVICE_LOST:
		return "DEVICE_LOST";
	case OB_RADIO_POST0413_STAGE_RMW_411_CLEANUP:
		return "RMW_411_CLEANUP";
	case OB_RADIO_POST0413_STAGE_READ_414:
		return "READ_414";
	case OB_RADIO_POST0413_STAGE_READ_415:
		return "READ_415";
	case OB_RADIO_POST0413_STAGE_RMW_410_ADVANCE:
		return "RMW_410_ADVANCE";
	case OB_RADIO_POST0413_STAGE_RMW_410_PHASE1_A:
		return "RMW_410_PHASE1_A";
	case OB_RADIO_POST0413_STAGE_RMW_410_PHASE1_B:
		return "RMW_410_PHASE1_B";
	case OB_RADIO_POST0413_STAGE_RMW_411_PHASE1:
		return "RMW_411_PHASE1";
	case OB_RADIO_POST0413_STAGE_WRITE_412:
		return "WRITE_412";
	case OB_RADIO_POST0413_STAGE_RMW_410_TRIGGER_CLEAR:
		return "RMW_410_TRIGGER_CLEAR";
	case OB_RADIO_POST0413_STAGE_RMW_410_TRIGGER_SET:
		return "RMW_410_TRIGGER_SET";
	case OB_RADIO_POST0413_STAGE_RMW_411_TRIGGER:
		return "RMW_411_TRIGGER";
	case OB_RADIO_POST0413_STAGE_POLL_READ:
		return "POLL_READ";
	case OB_RADIO_POST0413_STAGE_POLL_TIMEOUT:
		return "POLL_TIMEOUT";
	default:
		return "UNKNOWN";
	}
}

static void ob_radio_post0413_stop_fail(struct ob_hw *hw,
				       const char *stage, int ret,
				       bool phase2_mode)
{
	dev_info(hw->dev,
		 "ACPHY_CLASS0_POST0413_FAIL stage=%s ret=%d\n", stage, ret);
	dev_info(hw->dev, "ACPHY_CLASS0_POST0413_STOP\n");
	if (phase2_mode) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_FAIL stage=%s ret=%d\n",
			 stage, ret);
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_STOP\n");
	}
}

static void ob_radio_postphase1_marker(
	void *ctx, enum ob_radio_postphase1_marker marker,
	u16 sample, u16 low5, u32 attempts, u16 last_value, bool success)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->dev)
		return;
	switch (marker) {
	case OB_RADIO_POSTPHASE1_MARK_CP0_ENTRY:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_CP0_ENTRY\n");
		break;
	case OB_RADIO_POSTPHASE1_MARK_CP1_0416:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_CP1_0416 sample=0x%04x low5=0x%04x\n",
			 sample, low5);
		break;
	case OB_RADIO_POSTPHASE1_MARK_CP2_PHASE1_CHAIN_APPLY_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_CP2_PHASE1_CHAIN_APPLY_DONE\n");
		break;
	case OB_RADIO_POSTPHASE1_MARK_CP3_PHASE2_SETUP_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_CP3_PHASE2_SETUP_DONE\n");
		break;
	case OB_RADIO_POSTPHASE1_MARK_CP4_PHASE2_CHAIN_SETUP_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_CP4_PHASE2_CHAIN_SETUP_DONE\n");
		break;
	case OB_RADIO_POSTPHASE1_MARK_CP5_PRE_POLL:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_CP5_PRE_POLL\n");
		break;
	case OB_RADIO_POSTPHASE1_MARK_POLL:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_POLL attempts=%u last_value=0x%04x success=%u\n",
			 attempts, last_value, success ? 1 : 0);
		break;
	}
}

static const char *ob_radio_postphase1_stage_name(
	enum ob_radio_postphase1_stage stage)
{
	switch (stage) {
	case OB_RADIO_POSTPHASE1_STAGE_INVALID_ARGUMENT:
		return "INVALID_ARGUMENT";
	case OB_RADIO_POSTPHASE1_STAGE_PHASE1_GATE:
		return "PHASE1_GATE";
	case OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST:
		return "DEVICE_LOST";
	case OB_RADIO_POSTPHASE1_STAGE_0411_CLEANUP:
		return "0411_CLEANUP";
	case OB_RADIO_POSTPHASE1_STAGE_READ_0416:
		return "READ_0416";
	case OB_RADIO_POSTPHASE1_STAGE_PHASE1_CHAIN_APPLY:
		return "PHASE1_CHAIN_APPLY";
	case OB_RADIO_POSTPHASE1_STAGE_PHASE_END_0410:
		return "PHASE_END_0410";
	case OB_RADIO_POSTPHASE1_STAGE_PHASE2_SETUP:
		return "PHASE2_SETUP";
	case OB_RADIO_POSTPHASE1_STAGE_PHASE2_CHAIN_SETUP:
		return "PHASE2_CHAIN_SETUP";
	case OB_RADIO_POSTPHASE1_STAGE_PHASE2_TRIGGER:
		return "PHASE2_TRIGGER";
	case OB_RADIO_POSTPHASE1_STAGE_POLL_DELAY:
		return "POLL_DELAY";
	case OB_RADIO_POSTPHASE1_STAGE_POLL_READ:
		return "POLL_READ";
	case OB_RADIO_POSTPHASE1_STAGE_POLL_TIMEOUT:
		return "POLL_TIMEOUT";
	default:
		return "UNKNOWN";
	}
}

static void ob_radio_postphase1_stop_fail(struct ob_hw *hw,
					 const char *stage, int ret)
{
	dev_info(hw->dev,
		 "ACPHY_CLASS0_POSTPHASE1_FAIL stage=%s ret=%d\n", stage, ret);
	dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_STOP\n");
}

static const char *ob_radio_postphase2_stage_name(
	enum ob_radio_postphase2_stage stage);
static const char *ob_radio_postbmac_stage_name(
	enum ob_radio_postbmac_stage stage);

struct ob_radio_postphase2_marker_context {
	struct ob_hw *hw;
	bool suppress_success_terminal;
};

static int ob_radio_postphase2_context_read16(void *ctx, u16 reg, u16 *value)
{
	struct ob_radio_postphase2_marker_context *marker_ctx = ctx;

	return ob_radio_local_0413_read(marker_ctx ? marker_ctx->hw : NULL,
					reg, value);
}

static int ob_radio_postphase2_context_write16(void *ctx, u16 reg, u16 value)
{
	struct ob_radio_postphase2_marker_context *marker_ctx = ctx;

	return ob_radio_local_0413_write(marker_ctx ? marker_ctx->hw : NULL,
					 reg, value);
}

static bool ob_radio_postphase2_context_lost(void *ctx)
{
	struct ob_radio_postphase2_marker_context *marker_ctx = ctx;

	return ob_radio_local_0413_lost(marker_ctx ? marker_ctx->hw : NULL);
}

static void ob_radio_postphase2_marker(
	void *ctx, enum ob_radio_postphase2_marker marker,
	u16 sample, u8 field, enum ob_radio_postphase2_stage stage, int ret)
{
	struct ob_radio_postphase2_marker_context *marker_ctx = ctx;
	struct ob_hw *hw = marker_ctx ? marker_ctx->hw : NULL;

	if (!hw || !hw->dev)
		return;
	if (marker_ctx->suppress_success_terminal &&
	    stage == OB_RADIO_POSTPHASE2_STAGE_NONE &&
	    (marker == OB_RADIO_POSTPHASE2_MARK_PASS ||
	     marker == OB_RADIO_POSTPHASE2_MARK_STOP))
		return;
	switch (marker) {
	case OB_RADIO_POSTPHASE2_MARK_CP0_ENTRY:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE2_CP0_ENTRY\n");
		break;
	case OB_RADIO_POSTPHASE2_MARK_CP1_0411_CLEANUP_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE2_CP1_0411_CLEANUP_DONE\n");
		break;
	case OB_RADIO_POSTPHASE2_MARK_CP2_0416:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE2_CP2_0416 sample=0x%04x field=0x%02x\n",
			 sample, field);
		break;
	case OB_RADIO_POSTPHASE2_MARK_CP3_CHAIN0_DONE:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE2_CP3_CHAIN0_DONE\n");
		break;
	case OB_RADIO_POSTPHASE2_MARK_CP4_CHAIN1_DONE:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE2_CP4_CHAIN1_DONE\n");
		break;
	case OB_RADIO_POSTPHASE2_MARK_CP5_PHASE_END_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE2_CP5_PHASE_END_DONE\n");
		break;
	case OB_RADIO_POSTPHASE2_MARK_RETURN:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE2_RETURN\n");
		break;
	case OB_RADIO_POSTPHASE2_MARK_PASS:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE2_PASS\n");
		break;
	case OB_RADIO_POSTPHASE2_MARK_FAIL:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE2_FAIL stage=%s ret=%d\n",
			 ob_radio_postphase2_stage_name(stage), ret);
		break;
	case OB_RADIO_POSTPHASE2_MARK_STOP:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE2_STOP\n");
		break;
	}
}

static const char *ob_radio_postbmac_stage_name(
	enum ob_radio_postbmac_stage stage)
{
	switch (stage) {
	case OB_RADIO_POSTBMAC_STAGE_ENTRY_GATE:
		return "ENTRY_GATE";
	case OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST:
		return "DEVICE_LOST";
	case OB_RADIO_POSTBMAC_STAGE_OP1_SELECTOR_01B0:
		return "OP1_SELECTOR_01B0";
	case OB_RADIO_POSTBMAC_STAGE_OP1_DATA_READ:
		return "OP1_DATA_READ";
	case OB_RADIO_POSTBMAC_STAGE_OP1_DATA_WRITE:
		return "OP1_DATA_WRITE";
	case OB_RADIO_POSTBMAC_STAGE_OP2_SELECTOR_0000:
		return "OP2_SELECTOR_0000";
	case OB_RADIO_POSTBMAC_STAGE_OP2_DATA_READ:
		return "OP2_DATA_READ";
	case OB_RADIO_POSTBMAC_STAGE_HISTORICAL_BRANCH:
		return "HISTORICAL_BRANCH";
	case OB_RADIO_POSTBMAC_STAGE_CHIPCOMMON_GATE:
		return "CHIPCOMMON_GATE";
	case OB_RADIO_POSTBMAC_STAGE_PMU_0658_READ:
		return "PMU_0658_READ";
	case OB_RADIO_POSTBMAC_STAGE_PMU_0658_WRITE:
		return "PMU_0658_WRITE";
	case OB_RADIO_POSTBMAC_STAGE_PMU_0658_READBACK:
		return "PMU_0658_READBACK";
	case OB_RADIO_POSTBMAC_STAGE_PMU_065C_READ:
		return "PMU_065C_READ";
	case OB_RADIO_POSTBMAC_STAGE_PMU_065C_WRITE:
		return "PMU_065C_WRITE";
	case OB_RADIO_POSTBMAC_STAGE_PMU_065C_READBACK:
		return "PMU_065C_READBACK";
	case OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READ:
		return "GPIO_006C_READ";
	case OB_RADIO_POSTBMAC_STAGE_GPIO_006C_WRITE:
		return "GPIO_006C_WRITE";
	case OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READBACK:
		return "GPIO_006C_READBACK";
	default:
		return "UNKNOWN";
	}
}

static bool ob_radio_postbmac_cc_ready(void *ctx)
{
	struct ob_hw *hw = ctx;

	return hw && hw->cc &&
		hw->cc->id.id == BCMA_CORE_CHIPCOMMON;
}

static bool ob_radio_postbmac_cc_device_lost(void *ctx)
{
	struct ob_hw *hw = ctx;

	return !hw || hw->dev_lost;
}

static u32 ob_radio_postbmac_cc_read32(void *ctx, u16 reg)
{
	return ob_cc_read32(ctx, reg);
}

static void ob_radio_postbmac_cc_write32(void *ctx, u16 reg, u32 value)
{
	ob_cc_write32(ctx, reg, value);
}

static const struct ob_radio_postbmac_cc_ops ob_radio_postbmac_cc_io = {
	.ready = ob_radio_postbmac_cc_ready,
	.device_lost = ob_radio_postbmac_cc_device_lost,
	.read32 = ob_radio_postbmac_cc_read32,
	.write32 = ob_radio_postbmac_cc_write32,
};

static void ob_radio_postbmac_marker(
	void *ctx, enum ob_radio_postbmac_marker marker,
	u16 sample, u8 low4, bool ja_gt1,
	enum ob_radio_postbmac_stage stage, int ret,
	u32 old_value, u32 write_value, u32 readback)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->dev)
		return;
	switch (marker) {
	case OB_RADIO_POSTBMAC_MARK_CP0_ENTRY:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTBMAC_CP0_ENTRY\n");
		break;
	case OB_RADIO_POSTBMAC_MARK_CP1_PHY1B0_RMW_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTBMAC_CP1_PHY1B0_RMW_DONE\n");
		break;
	case OB_RADIO_POSTBMAC_MARK_CP2_PHY0_SAMPLE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTBMAC_CP2_PHY0_SAMPLE sample=0x%04x low4=0x%x ja_gt1=%u\n",
			 sample, low4, ja_gt1 ? 1u : 0u);
		break;
	case OB_RADIO_POSTBMAC_MARK_CP3_BRANCH_GATE:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTBMAC_CP3_BRANCH_GATE\n");
		break;
	case OB_RADIO_POSTBMAC_MARK_CP4_PMU_0658_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTBMAC_CP4_PMU_0658 old=0x%08x write=0x%08x readback=0x%08x\n",
			 old_value, write_value, readback);
		break;
	case OB_RADIO_POSTBMAC_MARK_CP5_PMU_065C_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTBMAC_CP5_PMU_065C old=0x%08x write=0x%08x readback=0x%08x\n",
			 old_value, write_value, readback);
		break;
	case OB_RADIO_POSTBMAC_MARK_CP6_GPIO_006C_DONE:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTBMAC_CP6_GPIO_006C old=0x%08x write=0x%08x readback=0x%08x\n",
			 old_value, write_value, readback);
		break;
	case OB_RADIO_POSTBMAC_MARK_PASS:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTBMAC_PASS\n");
		break;
	case OB_RADIO_POSTBMAC_MARK_FAIL:
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTBMAC_FAIL stage=%s ret=%d\n",
			 ob_radio_postbmac_stage_name(stage), ret);
		break;
	case OB_RADIO_POSTBMAC_MARK_STOP:
		dev_info(hw->dev, "ACPHY_CLASS0_POSTBMAC_STOP\n");
		break;
	}
}

static const char *ob_radio_postphase2_stage_name(
	enum ob_radio_postphase2_stage stage)
{
	switch (stage) {
	case OB_RADIO_POSTPHASE2_STAGE_INVALID_ARGUMENT:
		return "INVALID_ARGUMENT";
	case OB_RADIO_POSTPHASE2_STAGE_PHASE2_GATE:
		return "PHASE2_GATE";
	case OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST:
		return "DEVICE_LOST";
	case OB_RADIO_POSTPHASE2_STAGE_CLEANUP_0411:
		return "CLEANUP_0411";
	case OB_RADIO_POSTPHASE2_STAGE_READ_0416:
		return "READ_0416";
	case OB_RADIO_POSTPHASE2_STAGE_CHAIN0_0171:
		return "CHAIN0_0171";
	case OB_RADIO_POSTPHASE2_STAGE_CHAIN1_0371:
		return "CHAIN1_0371";
	case OB_RADIO_POSTPHASE2_STAGE_PHASE_END_0410:
		return "PHASE_END_0410";
	case OB_RADIO_POSTPHASE2_STAGE_TAIL_08EA:
		return "TAIL_08EA";
	default:
		return "UNKNOWN";
	}
}

static int ob_radio_class0_post0413_run(struct ob_hw *hw,
				       bool run_phase2,
				       bool run_return_suffix,
				       bool run_postbmac)
{
	struct ob_radio_local_0413_target target;
	struct ob_radio_postphase2_marker_context suffix_marker_ctx;
	struct ob_radio_local_0413_ops phase0_ops = {
		.quiesce = ob_radio_local_0413_quiesce,
		.read16 = ob_radio_local_0413_read,
		.write16 = ob_radio_local_0413_write,
		.delay_us = ob_radio_local_0413_delay,
		.device_lost = ob_radio_local_0413_lost,
	};
	struct ob_radio_local_0413_result phase0 = { 0 };
	struct ob_radio_post0413_ops phase1_ops = {
		.read16 = ob_radio_local_0413_read,
		.write16 = ob_radio_local_0413_write,
		.delay_us = ob_radio_local_0413_delay,
		.device_lost = ob_radio_local_0413_lost,
		.marker = ob_radio_post0413_marker,
	};
	struct ob_radio_post0413_result phase1;
	struct ob_radio_postphase1_ops phase2_ops = {
		.read16 = ob_radio_local_0413_read,
		.write16 = ob_radio_local_0413_write,
		.delay_us = ob_radio_local_0413_delay,
		.device_lost = ob_radio_local_0413_lost,
		.marker = ob_radio_postphase1_marker,
	};
	struct ob_radio_postphase1_result phase2;
	struct ob_radio_postphase2_ops suffix_ops = {
		.read16 = ob_radio_postphase2_context_read16,
		.write16 = ob_radio_postphase2_context_write16,
		.device_lost = ob_radio_postphase2_context_lost,
		.marker = ob_radio_postphase2_marker,
	};
	struct ob_radio_postphase2_result suffix;
	struct ob_radio_postbmac_gate postbmac_gate;
	struct ob_radio_postbmac_ops postbmac_ops = {
		.window = NULL,
		.cc = &ob_radio_postbmac_cc_io,
		.marker = ob_radio_postbmac_marker,
	};
	struct ob_radio_postbmac_result postbmac_result;
	u32 cflags;
	u16 shm;
	int ret;

	if (!hw || !hw->dev || !hw->core)
		return -ENODEV;
	dev_info(hw->dev, "ACPHY_CLASS0_0413_CP0_ENTRY\n");
	if (hw->dev_lost) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=ENTRY_STATE ret=-22\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		ob_radio_post0413_stop_fail(hw, "ENTRY_STATE", -22,
					    run_phase2);
		return -22;
	}
	target = (struct ob_radio_local_0413_target){
		.chip_id = hw->chip_id,
		.chip_rev = hw->chip_rev,
		.d11_corerev = hw->core->id.rev,
		.phy_type = hw->phy_type,
		.phy_revision = hw->phy_revision,
		.radio_id = hw->radio_id,
		.radio_revision = hw->radio_revision,
		.radio_revision_class = hw->radio_revision_class,
		.chanspec = hw->chanspec,
		.chanspec_valid = hw->chanspec_valid,
		.d11_identity_ready = hw->phy_radio_identity_valid,
		.acphy_start_ready =
			hw->acphy_start_state == OB_ACPHY_START_READY_FOR_RADIO_ON,
		.radio_access_initialized = hw->radio_access_initialized,
		.valid = hw->phy_radio_identity_valid && !hw->dev_lost,
	};
	if (hw->core->id.id != BCMA_CORE_80211 ||
	    hw->d11_state != OB_D11_POST_UCODE_READY ||
	    hw->acphy_start_state != OB_ACPHY_START_READY_FOR_RADIO_ON ||
	    !ob_radio_local_0413_identity_ok(&target)) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=TARGET_GATE ret=-22\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		ob_radio_post0413_stop_fail(hw, "TARGET_GATE", -22,
					    run_phase2);
		return -22;
	}

	/* Hardware-backed postconditions are sampled only after the pure target
	 * identity gate has admitted this selected BCM4352/rev42 path. */
	shm = ob_ucode_read_shm16(hw, 0x00a0u);
	cflags = ob_axi_read32(hw, 0x0408u);
	target.shm_00a0 = shm;
	target.wrapper_cflags = cflags;
	target.d11_core_enabled = bcma_core_is_enabled(hw->core);
	if (!target.d11_core_enabled || target.shm_00a0 != 0x1001u ||
	    (target.wrapper_cflags & 0x000000c0u) != 0x00000040u) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=PRE_RADIO_POSTCONDITION ret=-5\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		ob_radio_post0413_stop_fail(hw, "PRE_RADIO_POSTCONDITION", -5,
					    run_phase2);
		return -5;
	}
	if (!ob_radio_local_0413_target_ok(&target)) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=TARGET_GATE ret=-22\n");
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		ob_radio_post0413_stop_fail(hw, "TARGET_GATE", -22,
					    run_phase2);
		return -22;
	}
	dev_info(hw->dev, "ACPHY_CLASS0_0413_TARGET_OK\n");
	ret = ob_radio_local_0413_run(&phase0_ops, hw, &phase0);
	dev_info(hw->dev,
		 "ACPHY_CLASS0_0413_POLL attempts=%u last_value=0x%04x success=%u\n",
		 phase0.attempts, phase0.last_value, phase0.success ? 1 : 0);
	if (ret) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_0413_FAIL stage=%s ret=%d\n",
			 ob_radio_local_0413_stage_name(phase0.failure_stage), ret);
		dev_info(hw->dev, "ACPHY_CLASS0_0413_STOP\n");
		ob_radio_post0413_stop_fail(hw, "PHASE0", ret, run_phase2);
		return ret;
	}
	dev_info(hw->dev, "ACPHY_CLASS0_0413_PASS\n");
	ret = ob_radio_post0413_run(&phase1_ops, hw, phase0.success, &phase1);
	if (phase1.poll_started)
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POST0413_POLL attempts=%u last_value=0x%04x success=%u\n",
			 phase1.attempts, phase1.last_value,
			 phase1.success ? 1 : 0);
	if (ret) {
		ob_radio_post0413_stop_fail(hw,
			ob_radio_post0413_stage_name(phase1.failure_stage), ret,
			run_phase2);
		return ret;
	}
	dev_info(hw->dev, "ACPHY_CLASS0_POST0413_PASS\n");
	if (!run_phase2) {
		dev_info(hw->dev, "ACPHY_CLASS0_POST0413_STOP\n");
		return 0;
	}

	ret = ob_radio_postphase1_run(&phase2_ops, hw, phase1.success, &phase2);
	if (phase2.poll_started && !phase2.poll_marker_emitted)
		ob_radio_postphase1_marker(hw, OB_RADIO_POSTPHASE1_MARK_POLL,
			phase2.sample_0416, phase2.sample_low5,
			phase2.attempts, phase2.last_value, false);
	if (ret) {
		ob_radio_postphase1_stop_fail(hw,
			ob_radio_postphase1_stage_name(phase2.failure_stage), ret);
		return ret;
	}
	dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_PASS\n");
	if (!run_return_suffix) {
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_STOP\n");
		return 0;
	}

	suffix_marker_ctx = (struct ob_radio_postphase2_marker_context){
		.hw = hw,
		.suppress_success_terminal = run_postbmac,
	};
	suffix_ops.marker = ob_radio_postphase2_marker;
	ret = ob_radio_postphase2_run(&suffix_ops, &suffix_marker_ctx,
				      phase2.success, &suffix);
	if (ret)
		return ret;
	if (!run_postbmac)
		return 0;

	postbmac_gate = (struct ob_radio_postbmac_gate){
		.mode_selected = hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
		.identity_valid = target.valid && target.d11_identity_ready,
		.chip_id = target.chip_id,
		.chip_rev = target.chip_rev,
		.d11_core_rev = target.d11_corerev,
		.phy_type = target.phy_type,
		.phy_revision = target.phy_revision,
		.radio_id = target.radio_id,
		.radio_revision = target.radio_revision,
		.radio_revision_class = target.radio_revision_class,
		.d11_post_ucode_ready = hw->d11_state == OB_D11_POST_UCODE_READY,
		.acphy_start_ready = hw->acphy_start_state == OB_ACPHY_START_READY_FOR_RADIO_ON,
		.radio_access_initialized = hw->radio_access_initialized,
		.d11_core_enabled = target.d11_core_enabled,
		.chanspec_valid = target.chanspec_valid,
		.chanspec = target.chanspec,
		.phase2_success = phase2.success,
		.postphase2_success = suffix.success,
		.otp8e2_observed = hw->postbmac_otp8e2_valid,
		.otp8e2_word = hw->postbmac_otp8e2_word,
		.otp8e2_value = hw->postbmac_otp8e2_value,
	};
	postbmac_ops.window = ob_phy_window_hw_ops();
	ret = ob_radio_postbmac_run(&postbmac_ops, hw, &postbmac_gate,
				   &postbmac_result);
	return ret;
}

int ob_radio_class0_post0413_test(struct ob_hw *hw)
{
	return ob_radio_class0_post0413_run(hw, false, false, false);
}

int ob_radio_class0_postphase1_test(struct ob_hw *hw)
{
	return ob_radio_class0_post0413_run(hw, true, false, false);
}

/* Shared production subsystem helper for the established pre-LPF radio
 * prefix: 0x0413 -> phase 1 -> phase 2 -> sub_9591e return. The caller must
 * establish the exact target/channel predicate before entering; the selected
 * analog TX LPF call follows this prefix in the caller. */
int ob_radio_class0_initialize_to_sub9591e(struct ob_hw *hw)
{
	return ob_radio_class0_post0413_run(hw, true, true, false);
}

int ob_radio_class0_postphase2_return_test(struct ob_hw *hw)
{
	return ob_radio_class0_initialize_to_sub9591e(hw);
}

int ob_radio_class0_postbmac_branch_gate_test(struct ob_hw *hw)
{
	return ob_radio_class0_post0413_run(hw, true, true, true);
}

static int ob_radio_checked_read16(void *ctx, u16 reg, u16 *value)
{
	return ob_radio_read16(ctx, reg, value);
}

static int ob_radio_checked_write16(void *ctx, u16 reg, u16 value)
{
	return ob_radio_write16(ctx, reg, value);
}

static void ob_radio_checked_delay(void *ctx, u32 usec)
{
	(void)ctx;
	udelay(usec);
}

static const struct ob_radio2069_checked_ops ob_radio2069_checked_io = {
	.read16 = ob_radio_checked_read16,
	.write16 = ob_radio_checked_write16,
	.delay_us = ob_radio_checked_delay,
};

int ob_radio2069_rev4_probe_test(struct ob_hw *hw)
{
	struct ob_radio_access_ops pref_ops = {
		.read16 = ob_radio_checked_read16,
		.write16 = ob_radio_checked_write16,
	};
	u32 sentinel;
	int ret;

	if (!hw || hw->dev_lost || !hw->phy_radio_identity_valid ||
	    !ob_radio2069_target_applicable(&(struct ob_radio_target_identity){
		.valid = hw->phy_radio_identity_valid,
		.radio_access_initialized = hw->radio_access_initialized,
		.chip_id = hw->chip_id, .chip_rev = hw->chip_rev,
		.d11_corerev = hw->core->id.rev, .phy_type = hw->phy_type,
		.phy_revision = hw->phy_revision, .radio_id = hw->radio_id,
		.radio_revision = hw->radio_revision }))
		return -22;

	hw->chanspec = OB_RADIO2069_CHANSPEC_CH1_20MHZ;
	dev_info(hw->dev, "radio2069-rev4: RADIO_REV4_PREFS_REACHED chanspec=0x%04x\n",
		 hw->chanspec);
	ret = ob_d11_quiesce_for_radio(hw);
	if (ret)
		return ret;
	ret = ob_radio2069_apply_rev4_prefregs(&pref_ops, hw,
		OB_RADIO_BCM2069_ID, 4u);
	if (ret)
		return ret;
	dev_info(hw->dev, "radio2069-rev4: RADIO_REV4_PREFS_COMPLETE count=%u\n",
		 (unsigned int)OB_RADIO2069_REV4_PREFREG_COUNT);
	dev_info(hw->dev, "radio2069-rev4: RADIO2069_SYNTH_BEGIN\n");
	ret = ob_radio2069_synth_transition_checked(&ob_radio2069_checked_io,
		 hw, hw->runtime_mac_suspended);
	if (ret)
		return ret;
	sentinel = ob_d11_read32_trusted(hw, "radio2069-rev4", OB_RADIO_REG_SENTINEL);
	if (hw->dev_lost || sentinel == OB_GUARD_ALL_ONES32)
		return -5;
	dev_info(hw->dev, "radio2069-rev4: RADIO2069_SYNTH_COMPLETE\n");
	dev_info(hw->dev, "radio2069-rev4: FINAL_TRUSTED_SENTINEL=0x%08x\n",
		 sentinel);
	if (hw->radio_stage_owns_suspend) {
		ret = ob_d11_enable_mac(hw);
		if (ret)
			return ret;
		dev_info(hw->dev,
			 "radio2069-rev4: RADIO_MAC_RESUME_COMPLETE\n");
	} else {
		dev_info(hw->dev,
			 "radio2069-rev4: RADIO_MAC_RESUME_NOT_REQUIRED entry_already_quiesced\n");
	}
	dev_info(hw->dev, "radio2069-rev4: BOUNDED_STOP\n");
	return 0;
}

int ob_radio_rmw_clear16(struct ob_hw *hw, u16 reg, u16 mask)
{
	const struct ob_radio_access_ops ops = {
		.read16 = ob_radio_access_read16,
		.write16 = ob_radio_access_write16,
	};
	return ob_radio_rmw_clear(&ops, hw, reg, mask);
}

int ob_radio_rmw_set16(struct ob_hw *hw, u16 reg, u16 mask)
{
	const struct ob_radio_access_ops ops = {
		.read16 = ob_radio_access_read16,
		.write16 = ob_radio_access_write16,
	};
	return ob_radio_rmw_set(&ops, hw, reg, mask);
}

int ob_radio_probe_test(struct ob_hw *hw)
{
	struct ob_radio_seq_result res;
	struct ob_radio_io_ops trace_io;
	struct ob_dl_metadata metadata = {
		.schema = OB_DL_TRACE_SCHEMA,
		.candidate_id = "openbrcm-device-loss-trace/v1-source-emitter",
		.source_commit = "UNAVAILABLE",
		.build_id = OB_DL_BUILD_ID,
		.module_version = "UNAVAILABLE",
		.architecture = OB_DL_ARCHITECTURE,
		.diagnostic_flags = "raw-first_no-retry_fail-closed",
		.observer_set = OB_DL_OBSERVER_SET_FINGERPRINT,
	};
	int ret, trace_ret;

	dev_info(hw->dev, "radio-probe: BEGIN\n");
	trace_ret = ob_dl_trace_begin(&hw->dl_trace, &ob_radio_dl_sink, hw,
					      &ob_radio_dl_clock, hw, &metadata);
	if (trace_ret)
		return trace_ret;

	ret = ob_radio_prepare_for_identity(hw);
	if (ret) {
		if (ob_dl_trace_is_active(&hw->dl_trace))
			ob_dl_trace_stop(&hw->dl_trace, "PREPARATION_ERROR");
		return ret;
	}

	trace_io = ob_radio_io;
	trace_io.trace = &ob_radio_dl_hooks;
	trace_io.trace_ctx = hw;
	ret = ob_radio_sequence(&trace_io, hw, &hw->dev_lost, &res);
	if (res.stopped_dev_lost) {
		dev_crit(hw->dev,
			 "radio-probe: FAIL stage=%s pre_access=0x%08x post_access=0x%08x radio_writes=%u radio_reads=%u reg0_raw=0x%04x reg1_raw=0x%04x; no further MMIO, reboot required\n",
			 ob_radio_stage_name(res.failure_stage), res.pre_access,
			 res.post_access, res.radio_writes, res.radio_reads,
			 res.reg0_raw, res.reg1_raw);
		return ret ? ret : -EIO;
	}
	if (ret) {
		if (ob_dl_trace_is_active(&hw->dl_trace))
			ob_dl_trace_stop(&hw->dl_trace, "TRACE_STOP_FAIL_CLOSED");
		return ret;
	}

	trace_ret = ob_dl_trace_transition(&hw->dl_trace, OB_DL_PHASE_POST);
	if (!trace_ret)
		trace_ret = ob_dl_trace_note(&hw->dl_trace, OB_DL_PHASE_POST,
					     "post.observers", "observer-admission",
					     "NO_POST_LOSS_HOST_OBSERVER_ADMITTED");
	if (!trace_ret)
		trace_ret = ob_dl_trace_stop(&hw->dl_trace, "TRACE_COMPLETE");
	if (trace_ret)
		return trace_ret;

	dev_info(hw->dev, "radio-probe: pre_access=0x%08x\n", res.pre_access);
	dev_info(hw->dev, "radio-probe: reg0_raw=0x%04x\n", res.reg0_raw);
	dev_info(hw->dev, "radio-probe: reg1_raw=0x%04x\n", res.reg1_raw);
	dev_info(hw->dev, "radio-probe: radioid=0x%04x\n", res.obs.radio_id);
	dev_info(hw->dev, "radio-probe: radiorev=0x%02x\n", res.obs.radio_rev);
	dev_info(hw->dev, "radio-probe: revision_class=%u\n",
		 res.obs.revision_class);
	dev_info(hw->dev, "radio-probe: id_accepted=%d id_is_2069=%d\n",
		 res.obs.id_accepted ? 1 : 0, res.obs.id_is_2069 ? 1 : 0);
	dev_info(hw->dev, "radio-probe: pll_branch=%s\n",
		 ob_radio_branch_name(res.obs.branch));
	dev_info(hw->dev, "radio-probe: post_access=0x%08x\n", res.post_access);

	if (!res.valid) {
		dev_err(hw->dev,
			"radio-probe: FAIL invalid observation (all-ones pair reg0=0x%04x reg1=0x%04x)\n",
			res.reg0_raw, res.reg1_raw);
		return -EIO;
	}
	if (!res.obs.id_accepted) {
		dev_err(hw->dev,
			"radio-probe: FAIL unexpected radioid=0x%04x (AC accepts 0x2069/0x030b)\n",
			res.obs.radio_id);
		return -EIO;
	}

	dev_info(hw->dev, "radio-probe: PASS\n");
	dev_info(hw->dev, "radio-probe: STOPPED BEFORE PLL/RADIO INIT\n");
	return 0;
}

void ob_radio_remove(struct ob_hw *hw)
{
	if (hw->d3a0.lc.fatal || hw->dev_lost) {
		dev_crit(hw->dev,
			 "radio-probe: removed in FATAL/dev-lost state; no teardown, reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	dev_info(hw->dev,
		 "radio-probe: removed (no resource teardown, hardware left as-is)\n");
	bcma_set_drvdata(hw->core, NULL);
}

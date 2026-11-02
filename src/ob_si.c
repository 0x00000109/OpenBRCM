// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — silicon backplane bring-up.
 *
 * The register map and the exact values/sequences are recovered from the blob
 * (RE Stage 4/5) and cross-checked against Linux bcma. This file is the only
 * place that touches PMU/PLL/clock/OTP/SPROM directly.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/bcma/bcma.h>
#include "ob_si.h"
#include "ob_otp8e2.h"
#include "ob_ucode.h"

/*
 * Opt-out switch for the NON-DESTRUCTIVE-TO-OTP diagnostic probe. It writes OTP
 * control registers but never programs OTP and never replaces the MAC. Disable
 * with `insmod openbrcm.ko otp_diag=0`.
 */
static bool otp_diag;
module_param(otp_diag, bool, 0444);
MODULE_PARM_DESC(otp_diag, "Run the OTP diagnostic probe at probe time (disabled: board uses external SPROM)");

/*
 * Read-only external-SPROM diagnostic. BC.M2.5e showed the BCM4352 reports
 * SROM_CONTROL.PRESENT=1 and the blob's srom_read() therefore takes the
 * external-SPROM path, never the OTP fallback. This probe reads the external
 * SPROM via the ChipCommon window exactly as bcma_sprom_read() does.
 */
static bool sprom_diag = true;
module_param(sprom_diag, bool, 0444);
MODULE_PARM_DESC(sprom_diag, "Run the read-only external SPROM diagnostic probe at probe time");
/* Alias so runbooks using the historical "srom_diag" spelling keep working. */
module_param_named(srom_diag, sprom_diag, bool, 0444);
MODULE_PARM_DESC(srom_diag, "Alias for sprom_diag");

/* CLKCTLST: HT clock available (matches BCMA_CLKCTLST_HAVEHT). */
#define OB_CLKCTLST_HAVEHT	0x00020000

/*
 * D11 host-interface registers used by the M3.4C read-only bring-up
 * diagnostic. Offsets are C3 (brcmsmac d11.h `struct d11regs`):
 *   maccontrol 0x120, maccommand 0x124, macintstatus 0x128, macintmask 0x12C,
 *   phydebug 0x158 (32-bit), psmdebug 0x154, tsf_timerlow/high 0x180/0x184.
 */
#define OB_D11_MACCONTROL	0x0120
#define OB_D11_MACCOMMAND	0x0124
#define OB_D11_MACINTSTATUS	0x0128
#define OB_D11_MACINTMASK	0x012c
#define OB_D11_PSMDEBUG		0x0154
#define OB_D11_PHYDEBUG		0x0158
#define OB_D11_TSF_LOW		0x0180
#define OB_D11_TSF_HIGH		0x0184
#define OB_SI_SFLAGS_OFFSET	0x0500

/* maccontrol bits (C3 d11.h MCTL_*). */
#define OB_MCTL_EN_MAC		0x00000001
#define OB_MCTL_PSM_RUN		0x00000002
#define OB_MCTL_IHR_EN		0x00000400
#define OB_MCTL_INFRA		0x00020000
#define OB_MCTL_PROMISC		0x01000000
#define OB_MCTL_WAKE		0x04000000

/*
 * The SI path uses BCMA's device accessor and never owns the BAR window.  Fail
 * before the first access if the per-device object graph was detached or the
 * immutable profile no longer describes the BCMA D11 object.
 */
static bool ob_si_profile_matches_context(const struct ob_hw *hw)
{
	const struct ob_chip_profile *profile;

	if (!hw || !hw->profile || !hw->core)
		return false;
	profile = hw->profile;
	return hw->chip_id == profile->chip_id &&
	       hw->chip_rev == profile->chip_revision &&
	       hw->core->id.manuf == profile->d11_manufacturer &&
	       hw->core->id.id == profile->d11_core_id &&
	       hw->core->id.rev == profile->d11_revision;
}

static int ob_si_validate_context(const struct ob_hw *hw)
{
	return ob_si_core_context_precondition(
		hw && hw->dev, hw && hw->bus, hw && hw->core,
		hw && hw->profile,
		hw && hw->bus && hw->core && hw->core->bus == hw->bus,
		ob_si_profile_matches_context(hw), hw && hw->dev_lost);
}

/*
 * One-shot, isolated SFLAGS snapshot for a future authorized observation.
 * BCMA's aread32 path addresses the selected core wrapper; it may update the
 * PCIe BAR window selector as part of the normal BCMA access operation. This
 * records the wrapper word and the historical bit-12 predicate only: this
 * diagnostic does not run dma_attach and therefore cannot claim an installed
 * DMA callback or an attach-time vendor value.
 */
int ob_si_sflags_evidence(struct ob_hw *hw)
{
	struct pci_dev *pdev;
	enum ob_dma_backend_choice predicate;
	u32 sflags;
	bool valid;
	int ret;

	if (!hw || !hw->sflags_diag_only)
		return -EPERM;
	ret = ob_si_validate_context(hw);
	if (ret)
		return ret;
	if (hw->bus->hosttype != BCMA_HOSTTYPE_PCI ||
	    !hw->bus->host_pci || !hw->core->wrap ||
	    hw->core->core_index >= hw->bus->nr_cores)
		return -ENODEV;
	pdev = hw->bus->host_pci;

	/* Same 32-bit wrapper access domain used by historical ai_core_sflags. */
	sflags = ob_axi_read32(hw, OB_SI_SFLAGS_OFFSET);
	valid = !hw->dev_lost && sflags != U32_MAX;
	predicate = ob_sflags_snapshot_classify(valid, sflags);
	dev_info(hw->dev,
		 "SFLAGS_DIAG pci=%s pci_id=%04x:%04x pci_rev=%02x chip=%04x/%u d11=%04x:%04x rev=%u class=%u core_index=%u core_unit=%u wrapper=0x%08x reg=+0x%04x width=32 value=0x%08x valid=%u bit12=%s historical_predicate=%s actual_dma_attach=NOT_RUN context=PRE_INIT_NO_FIFO_OR_RX\n",
		 pci_name(pdev), pdev->vendor, pdev->device, pdev->revision,
		 hw->chip_id, hw->chip_rev, hw->core->id.manuf,
		 hw->core->id.id, hw->core->id.rev, hw->core->id.class,
		 hw->core->core_index, hw->core->core_unit, hw->core->wrap,
		 OB_SI_SFLAGS_OFFSET, sflags, valid,
		 valid ? ((sflags & OB_SI_SFLAGS_DMA64) ? "1" : "0") : "UNKNOWN",
		 predicate == OB_DMA_BACKEND_DMA64PROC ? "DMA64_IF_USED" :
		 predicate == OB_DMA_BACKEND_DMA32PROC ? "DMA32_IF_USED" :
		 "UNKNOWN");

	/* An invalid/dead read remains unknown and fails the isolated observation;
	 * it never falls through to normal initialization.
	 */
	return valid ? 0 : -EIO;
}

/*
 * M3.4C read-only bring-up diagnostic. Logs the D11 host-interface state that
 * determines whether the MAC/ucode/PHY are running, so it can be compared with
 * the recovered vendor pre-RX state. No register is written.
 */
struct ob_d11_timing_sample {
	u16 offset;
	u8 width;
	u32 value;
	u64 start_ns;
	u64 end_ns;
};

static void ob_si_d11_diag(struct ob_hw *hw)
{
	struct ob_d11_timing_sample s[8] = {
		{ .offset = OB_D11_MACCONTROL, .width = 32 },
		{ .offset = OB_D11_MACCOMMAND, .width = 32 },
		{ .offset = OB_D11_MACINTSTATUS, .width = 32 },
		{ .offset = OB_D11_MACINTMASK, .width = 32 },
		{ .offset = OB_D11_PSMDEBUG, .width = 32 },
		{ .offset = OB_D11_PHYDEBUG, .width = 32 },
		{ .offset = OB_D11_TSF_LOW, .width = 32 },
		{ .offset = OB_D11_TSF_HIGH, .width = 32 },
	};
	u32 mc, cmd, ist, im, psm, phyd, tlo, thi;
	int i;

	s[0].start_ns = ktime_get_mono_fast_ns();
	mc = ob_d11_read32(hw, OB_D11_MACCONTROL);
	s[0].end_ns = ktime_get_mono_fast_ns(); s[0].value = mc;
	s[1].start_ns = ktime_get_mono_fast_ns();
	cmd = ob_d11_read32(hw, OB_D11_MACCOMMAND);
	s[1].end_ns = ktime_get_mono_fast_ns(); s[1].value = cmd;
	s[2].start_ns = ktime_get_mono_fast_ns();
	ist = ob_d11_read32(hw, OB_D11_MACINTSTATUS);
	s[2].end_ns = ktime_get_mono_fast_ns(); s[2].value = ist;
	s[3].start_ns = ktime_get_mono_fast_ns();
	im = ob_d11_read32(hw, OB_D11_MACINTMASK);
	s[3].end_ns = ktime_get_mono_fast_ns(); s[3].value = im;
	s[4].start_ns = ktime_get_mono_fast_ns();
	psm = ob_d11_read32(hw, OB_D11_PSMDEBUG);
	s[4].end_ns = ktime_get_mono_fast_ns(); s[4].value = psm;
	s[5].start_ns = ktime_get_mono_fast_ns();
	phyd = ob_d11_read32(hw, OB_D11_PHYDEBUG);
	s[5].end_ns = ktime_get_mono_fast_ns(); s[5].value = phyd;
	s[6].start_ns = ktime_get_mono_fast_ns();
	tlo = ob_d11_read32(hw, OB_D11_TSF_LOW);
	s[6].end_ns = ktime_get_mono_fast_ns(); s[6].value = tlo;
	s[7].start_ns = ktime_get_mono_fast_ns();
	thi = ob_d11_read32(hw, OB_D11_TSF_HIGH);
	s[7].end_ns = ktime_get_mono_fast_ns(); s[7].value = thi;

	dev_info(hw->dev,
		 "bringup: maccontrol=%08x maccommand=%08x macintstatus=%08x macintmask=%08x\n",
		 mc, cmd, ist, im);
	dev_info(hw->dev,
		 "bringup: psmdebug=%08x phydebug=%08x tsf=%08x%08x\n",
		 psm, phyd, thi, tlo);
	dev_info(hw->dev,
		 "bringup: en_mac=%d psm_run=%d ihr_en=%d infra=%d promisc=%d wake=%d\n",
		 !!(mc & OB_MCTL_EN_MAC), !!(mc & OB_MCTL_PSM_RUN),
		 !!(mc & OB_MCTL_IHR_EN), !!(mc & OB_MCTL_INFRA),
		 !!(mc & OB_MCTL_PROMISC), !!(mc & OB_MCTL_WAKE));
	/*
	 * Derived, not a separate register: the ucode PSM is running when
	 * PSM_RUN and IHR_EN are set, and the MAC is enabled by EN_MAC.
	 */
	dev_info(hw->dev,
		 "bringup: ucode_running=%d mac_enabled=%d (derived)\n",
		 !!((mc & OB_MCTL_PSM_RUN) && (mc & OB_MCTL_IHR_EN)),
		 !!(mc & OB_MCTL_EN_MAC));
	for (i = 0; i < ARRAY_SIZE(s); i++) {
		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%08x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 s[i].offset, s[i].width, s[i].value,
			 (unsigned long long)s[i].start_ns,
			 (unsigned long long)s[i].end_ns,
			 (unsigned long long)div_u64(s[i].end_ns - s[i].start_ns,
						     NSEC_PER_USEC));
	}
}

/* Read-only wrapper bracket; raw invalid state is a fail-closed error. */
int ob_si_d11_enable_bracket(struct ob_hw *hw, const char *stage)
{
	u64 ioctl_start_ns;
	u32 ioctl;
	u64 ioctl_end_ns;
	u32 reset_ctl = 0;
	bool ioctl_valid;
	bool reset_read = false;
	bool reset_valid = true;
	bool enabled = false;
	bool wrapper_valid;
	int ret;

	ret = ob_si_validate_context(hw);
	if (ret)
		return ret;

	ioctl_start_ns = ktime_get_mono_fast_ns();
	ioctl = ob_axi_read32(hw, BCMA_IOCTL);
	ioctl_end_ns = ktime_get_mono_fast_ns();
	ioctl_valid = ioctl != 0xffffffffu;

	if (ioctl_valid &&
	    (ioctl & (BCMA_IOCTL_CLK | BCMA_IOCTL_FGC)) == BCMA_IOCTL_CLK) {
		reset_ctl = ob_axi_read32(hw, BCMA_RESET_CTL);
		reset_read = true;
		reset_valid = reset_ctl != 0xffffffffu;
		enabled = reset_valid &&
			   !(reset_ctl & BCMA_RESET_CTL_RESET);
	}
	wrapper_valid = ioctl_valid && (!reset_read || reset_valid);

	dev_info(hw->dev,
		 "%s core=%px mapped=%px mapped_is_core=%d index=%u state=%u "
		 "ioctl=0x%08x ioctl_valid=%d reset_ctl=0x%08x "
		 "reset_read=%d reset_valid=%d enabled=%d wrapper_valid=%d "
		 "ioctl_start_ns=%llu ioctl_end_ns=%llu ioctl_duration_us=%llu\n",
		 stage, hw->core, hw->bus->mapped_core,
		 hw->bus->mapped_core == hw->core, hw->core->core_index,
		 hw->d11_state, ioctl, ioctl_valid, reset_ctl, reset_read,
		 reset_valid, enabled, wrapper_valid,
		 (unsigned long long)ioctl_start_ns,
		 (unsigned long long)ioctl_end_ns,
		 (unsigned long long)div_u64(ioctl_end_ns - ioctl_start_ns,
						     NSEC_PER_USEC));

	if (!wrapper_valid)
		return -EIO;
	return 0;
}

/* ChipCommon is a fixed-function core; access it through bcma's window. */
u32 ob_si_cc_read(struct ob_hw *hw, u16 off)
{
	return ob_cc_read32(hw, off);
}

void ob_si_cc_write(struct ob_hw *hw, u16 off, u32 val)
{
	ob_cc_write32(hw, off, val);
}

/* 16-bit accessor: identical width to bcma_sprom_read()'s bcma_read16(). */
static u16 ob_si_cc_read16(struct ob_hw *hw, u16 off)
{
	return ob_cc_read16(hw, off);
}

static u8 ob_sprom_crc(const u16 *sprom, size_t words);

/*
 * Read and validate the factory MAC from the external SPROM (base 0x800).
 *
 * Provenance: bcma_sprom_get() probes 220/230/234 words and validates the CRC8
 * plus revision (8..11) before trusting the image; the MAC byte offset then
 * depends on that revision: rev8 -> +0x8C, rev11 (BCM4352/BCM4360) -> +0x90.
 * Using 0x8C against a rev11 image reads the zero words just before the real
 * MAC — exactly the malformed 00:00:00:00:2c:fd bcma reports. The three words
 * are decoded big-endian (bcma: cpu_to_be16). Base is NOT OTPL-derived (0x840
 * is the on-chip OTP general-use region).
 */
int ob_si_read_mac(struct ob_hw *hw, u8 mac[6])
{
	static const u16 sizes[] = { OB_SPROM_WORDS_R4, OB_SPROM_WORDS_R10,
				     OB_SPROM_WORDS_R11 };
	u16 *sp;
	size_t words = 0;
	u8 rev = 0, crc = 0, ecrc = 0;
	u16 off, raw[3], first;
	int i, k;
	int ret;

	ret = ob_si_validate_context(hw);
	if (ret || !mac)
		return ret ? ret : -EINVAL;

	hw->mac_source = OB_MAC_SOURCE_UNKNOWN;
	hw->mac_valid = false;
	ob_d3b_clear_rev11_board(&hw->board);

	sp = kcalloc(OB_SPROM_WORDS_R11, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		words = sizes[i];
		/* Avoid hundreds of platform-timeout reads after the window has
		 * already returned the trusted inaccessible sentinel. */
		first = ob_si_cc_read16(hw, OB_CC_SPROM);
		if (!ob_si_sprom_first_word_accessible(first)) {
			dev_warn(hw->dev,
				 "SPROM window inaccessible at first word; aborting validation\n");
			kfree(sp);
			return -ENODATA;
		}
		sp[0] = first;
		for (k = 1; k < words; k++)
			sp[k] = ob_si_cc_read16(hw, OB_CC_SPROM + k * 2);
		crc = ob_sprom_crc(sp, words);
		rev = sp[words - 1] & 0xff;
		ecrc = sp[words - 1] >> 8;
		dev_info(hw->dev,
			 "SPROM rev=%u words=%zu crc stored=%02x calculated=%02x %s\n",
			 rev, words, ecrc, crc,
			 crc == ecrc ? "MATCH" : "MISMATCH");
		if (crc == ecrc && rev >= 8 && rev <= 11)
			break;
	}
	if (i == ARRAY_SIZE(sizes)) {
		dev_warn(hw->dev,
			 "SPROM validation failed (no valid CRC/revision)\n");
		kfree(sp);
		return -ENODATA;
	}

	/*
	 * Proven rev11 board fields (boardtype/boardflags/boardflags3/aa2g/aa5g/antswitch)
	 * decoded from the SAME already-read, CRC-validated image: no extra
	 * MMIO. Only rev11 carries the recovered field map; other revisions
	 * leave board.valid=false.
	 */
	ob_d3b_clear_rev11_board(&hw->board);
	if (rev == 11 &&
	    ob_d3b_decode_rev11_board(sp, words, &hw->board)) {
		dev_info(hw->dev,
			 "boarddata: boardtype=0x%04x boardflags=0x%08x boardflags3=0x%08x aa2g=%u aa5g=%u antswitch=%u(present=%d)\n",
			 hw->board.boardtype, hw->board.boardflags,
			 hw->board.boardflags3,
			 hw->board.aa2g, hw->board.aa5g, hw->board.antswitch,
			 hw->board.antswitch_present);
	}

	/* rev11 moved the first MAC to +0x90; rev8 uses +0x8C. */
	off = (rev == 11) ? OB_SPROM11_IL0MAC : OB_SPROM8_IL0MAC;
	raw[0] = sp[(off + 0) / 2];
	raw[1] = sp[(off + 2) / 2];
	raw[2] = sp[(off + 4) / 2];
	mac[0] = (raw[0] >> 8) & 0xff; mac[1] = raw[0] & 0xff;
	mac[2] = (raw[1] >> 8) & 0xff; mac[3] = raw[1] & 0xff;
	mac[4] = (raw[2] >> 8) & 0xff; mac[5] = raw[2] & 0xff;
	kfree(sp);

	dev_info(hw->dev, "IL0MAC offset=0x%x\n", off);
	dev_info(hw->dev, "raw=%04x %04x %04x\n", raw[0], raw[1], raw[2]);
	dev_info(hw->dev, "MAC=%pM valid=%d\n", mac, is_valid_ether_addr(mac));

	if (!is_valid_ether_addr(mac))
		return -EINVAL;
	hw->mac_source = rev == 11 ? OB_MAC_SOURCE_REV11_CORRECTED :
					OB_MAC_SOURCE_DIRECT_VALIDATED;
	hw->mac_valid = true;
	return 0;
}

/*
 * Consume only the SPROM state already owned and populated by BCMA bus setup.
 *
 * Linux 7.x exposes one il0mac field in the shared SPROM structure.  The
 * retained BCMA source-equivalent applies the rev8 extraction offset to the
 * BCM4352/rev11 image, so that field is not accepted for rev11.  The normal
 * child path therefore leaves the rev11 MAC explicitly unknown instead of
 * reopening the raw ChipCommon SPROM window.  Isolated D3 board-data paths
 * retain ob_si_read_mac(), where the rev11-corrected source is bounded.
 */
static int ob_si_consume_cached_sprom(struct ob_hw *hw)
{
	const struct ssb_sprom *sprom;
	u8 ext_mac[6];
	int ret;

	if (!hw || !hw->bus)
		return -ENODEV;

	sprom = &hw->bus->sprom;
	hw->mac_valid = false;
	hw->mac_source = OB_MAC_SOURCE_UNKNOWN;
	eth_zero_addr(hw->mac);
	ob_d3b_clear_rev11_board(&hw->board);

	dev_info(hw->dev,
		 "bcma board state: sprom_rev=%u board_rev=%u board_type=0x%04x\n",
		 sprom->revision, sprom->board_rev, sprom->board_type);

	/*
	 * Linux BCMA exposes a boardflags3 member, but its rev11 extractor does
	 * not populate it. Read the same external, CRC-validated 234-word image
	 * that the isolated board-data path already uses. The MAC is passed a
	 * scratch buffer and discarded so the normal path's MAC provenance does
	 * not change. The decoder publishes boardflags3_valid/source only after
	 * CRC and revision validation, so absent or corrupt data stays invalid.
	 */
	if (sprom->revision == 11) {
		ret = ob_si_read_mac(hw, ext_mac);
		if (!hw->board.valid)
			dev_warn(hw->dev,
				 "external rev11 board data unavailable (ret=%d); boardflags3 remains invalid\n",
				 ret);
		else
			dev_info(hw->dev,
				 "external rev11 board data: boardflags3=0x%08x source=VALIDATED_REV11\n",
				 hw->board.boardflags3);
		hw->mac_valid = false;
		hw->mac_source = OB_MAC_SOURCE_UNKNOWN;
		eth_zero_addr(hw->mac);
	}

	if (sprom->revision != 11 &&
	    sprom->revision >= 8 && sprom->revision <= 10 &&
	    is_valid_ether_addr(sprom->il0mac)) {
		ether_addr_copy(hw->mac, sprom->il0mac);
		hw->mac_valid = true;
		hw->mac_source = OB_MAC_SOURCE_BCMA_PARSED;
		dev_info(hw->dev, "BCMA-parsed permanent MAC %pM\n", hw->mac);
	} else if (sprom->revision == 11) {
		dev_warn(hw->dev,
			 "BCMA rev11 MAC field is not trusted for this target; MAC remains unknown\n");
	} else {
		dev_warn(hw->dev,
			 "no valid cached BCMA MAC; MAC remains unknown (rev=%u)\n",
			 sprom->revision);
	}

	return 0;
}

static const char *ob_fixed_window_baseline_name(
		 enum ob_fixed_window_baseline baseline)
{
	switch (baseline) {
	case OB_FIXED_WINDOW_READABLE:
		return "READABLE";
	case OB_FIXED_WINDOW_ALL_ONES:
		return "ALL_ONES";
	default:
		return "INDETERMINATE";
	}
}

enum ob_fixed_window_baseline ob_si_dump(struct ob_hw *hw)
{
	struct ob_fixed_window_snapshot snapshot = {
		.cc_cap = ob_si_cc_read(hw, OB_CC_CAP),
		.cc_otps = ob_si_cc_read(hw, OB_CC_OTPS),
		.cc_otpl = ob_si_cc_read(hw, OB_CC_OTPL),
		.cc_srom_ctl = ob_si_cc_read(hw, OB_CC_SROM_CONTROL),
		.cc_clkctlst = ob_si_cc_read(hw, OB_CC_CLKCTLST),
		.pmu_cap = ob_si_cc_read(hw, OB_CC_PMU_CAP),
		.pmu_ctl = ob_si_cc_read(hw, OB_CC_PMU_CTL),
	};
	enum ob_fixed_window_baseline baseline =
		ob_fixed_window_classify(&snapshot);

	dev_info(hw->dev, "cc: cap=%08x otps=%08x otpl=%08x srom_ctl=%08x\n",
		 snapshot.cc_cap, snapshot.cc_otps, snapshot.cc_otpl,
		 snapshot.cc_srom_ctl);
	dev_info(hw->dev, "pmu: cap=%08x ctl=%08x clkctlst=%08x\n",
		 snapshot.pmu_cap, snapshot.pmu_ctl, snapshot.cc_clkctlst);
	dev_info(hw->dev, "sprom present=%d otp present=%d otpsel=%d\n",
		 !!(snapshot.cc_srom_ctl & OB_SROM_PRESENT),
		 !!(snapshot.cc_srom_ctl & OB_SROM_OTP_PRESENT),
		 !!(snapshot.cc_srom_ctl & OB_SROM_OTPSEL));

	return baseline;
}

/*
 * Read-only diagnostics of the bcma host/core topology. All of these are plain
 * struct reads (no MMIO), used purely to prove the PCIe2 vs legacy-PCI topology
 * before any power operation. Safe to call at any time.
 */
static void ob_si_dump_bus(struct ob_hw *hw)
{
	struct bcma_bus *bus = hw->bus;
	struct bcma_device *d11 = hw->core;

	dev_info(hw->dev,
		 "bus: hosttype=%d host_is_pcie2=%d host_pci=%px\n",
		 bus->hosttype, bus->host_is_pcie2, bus->host_pci);
	dev_info(hw->dev,
		 "bus: drv_pci[0].core=%px drv_pci[1].core=%px drv_pcie2.core=%px\n",
		 bus->drv_pci[0].core, bus->drv_pci[1].core, bus->drv_pcie2.core);
	dev_info(hw->dev,
		 "d11: core_index=%u addr=0x%x wrap=0x%x rev=%u\n",
		 d11->core_index, d11->addr, d11->wrap, d11->id.rev);
}

/*
 * External SPROM base: the fixed ChipCommon window 0x800. It is NOT the
 * OTPL-derived 0x840 — 0x840 is the on-chip OTP general-use region offset
 * (0x800 + (OTPL.GURGN_OFFSET >> 3), OTPL=0x00115200 -> 0x200 bits -> 0x40 B).
 */
static u32 ob_si_sprom_base(struct ob_hw *hw)
{
	(void)hw;
	return OB_CC_SPROM;
}

/* Read-only snapshot of the external SPROM window (rev4 MAC location shown). */
static void ob_si_dump_sprom(struct ob_hw *hw, const char *tag)
{
	u32 base = ob_si_sprom_base(hw);

	dev_info(hw->dev,
		 "powerup[%s]: external SPROM@0x%x word0=%08x rev4mac=%08x %08x %08x\n",
		 tag, base,
		 ob_si_cc_read(hw, base),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET + 4),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET + 8));
}

/*
 * M2.5 — instrumented power-up for the BCM4352.
 *
 * Steps that are delegated to the in-kernel bcma bus driver (authoritative,
 * GPL-exported) are used directly: core enable/reset (bcma_core_enable is the
 * kernel implementation of the recovered ai_core_reset semantics) and HT clock
 * forcing (bcma_core_set_clockmode implements FORCEHT + HAVEHT polling with a
 * timeout). Steps whose values are not provenance-backed for 0x4352 are NOT
 * guessed; they are reported as UNKNOWN.
 */
int ob_si_powerup(struct ob_hw *hw)
{
	u32 clk;
	int err;

	err = ob_si_validate_context(hw);
	if (err) {
		if (hw && hw->dev)
			dev_err(hw->dev,
				"powerup: invalid SI/BCMA context, stopping before access (%d)\n",
				err);
		return err;
	}

	/* Device-loss guard: no host/core/clock bring-up after the latch. */

	dev_info(hw->dev, "powerup: chip 0x%04x rev %u\n",
		 hw->chip_id, hw->chip_rev);

	/* Stage: initial fixed-window state, before any OpenBRCM state change. */
	{
		enum ob_fixed_window_baseline baseline = ob_si_dump(hw);

		dev_info(hw->dev,
			 "FIXED_WINDOW_BASELINE=%s FABRIC_BASELINE_ACCEPTED=%s\n",
			 ob_fixed_window_baseline_name(baseline),
			 baseline == OB_FIXED_WINDOW_READABLE ? "YES" : "NO");
		if (baseline != OB_FIXED_WINDOW_READABLE) {
			dev_err(hw->dev,
				"FIXED_WINDOW_BASELINE_REJECTED class=%s\n",
				ob_fixed_window_baseline_name(baseline));
			return -EIO;
		}
	}
	dev_info(hw->dev,
		 "powerup: using BCMA-cached SPROM state; raw SPROM reread is not a normal prerequisite\n");
	dev_info(hw->dev, "powerup: d11 core enabled=%d\n",
		 bcma_core_is_enabled(hw->core));

	/* Stage 0: topology diagnostics (read-only) */
	ob_si_dump_bus(hw);

	/*
	 * Stage A: host PCIe bring-up.
	 *
	 * bcma_core_pci_power_save() is the legacy PCI-core power-save path and
	 * only guards bus->hosttype, NOT bus->host_is_pcie2. A modern BCM4352
	 * board has no legacy PCI core, so bus->drv_pci[0].core is NULL and that
	 * function dereferences NULL at pc->core->id.rev (fault address 0xc).
	 *
	 * Use the public host abstraction instead: bcma_host_pci_up() checks
	 * hosttype and dispatches on host_is_pcie2 to bcma_core_pcie2_up() — the
	 * correct path for this bus. The legacy call is removed entirely.
	 */
	dev_info(hw->dev, "powerup[A]: host up (host_is_pcie2=%d)\n",
		 hw->bus->host_is_pcie2);
	bcma_host_pci_up(hw->bus);
	dev_info(hw->dev,
		 "powerup[A]: host up done drv_pcie2.core=%px drv_pci[0].core=%px\n",
		 hw->bus->drv_pcie2.core, hw->bus->drv_pci[0].core);

	/* Stage B: D11 core enable/reset, separate from host bring-up. */
	dev_info(hw->dev, "powerup[B]: d11 enabled(before)=%d\n",
		 bcma_core_is_enabled(hw->core));
	if (!bcma_core_is_enabled(hw->core)) {
		err = bcma_core_enable(hw->core, 0);
		dev_info(hw->dev,
			 "powerup[B]: bcma_core_enable(d11) err=%d enabled(after)=%d\n",
			 err, bcma_core_is_enabled(hw->core));
		if (err)
			return err;
	}
	err = ob_si_core_enable_postcondition(0,
					      bcma_core_is_enabled(hw->core));
	if (err) {
		dev_err(hw->dev,
			"powerup[B]: BCMA enable postcondition failed; stopping before FAST clock\n");
		return err;
	}

	/* Stage C: force HT clock and wait for it (bounded inside bcma) */
	dev_info(hw->dev, "powerup[C]: set clockmode FAST\n");
	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);
	clk = ob_si_cc_read(hw, OB_CC_CLKCTLST);
	dev_info(hw->dev, "powerup[C]: clkctlst=0x%08x HAVEHT=%d\n",
		 clk, !!(clk & OB_CLKCTLST_HAVEHT));

	/*
	 * Board data source. BCMA has already acquired the SPROM during bus setup;
	 * normal probe consumes that cached state and does not reread CC+0x800.
	 * The rev11 cached il0mac representation is not trusted for this target,
	 * so MAC validity is deferred to the interface-registration boundary.
	 * On-chip OTP is reachable only through the opt-in otp_diag path (default
	 * off). si_pmu_otp_power is a no-op for 0x4352 — no register write is
	 * needed here.
	 */
	dev_info(hw->dev,
		 "powerup: board data from external SPROM (OTP not required; otp_diag opt-in)\n");
	return 0;
}

/* OTP geometry derived from the CC_CAP OTP-size selector. */
struct ob_otp_geometry {
	u32 rows;
	u32 cols;
	u32 words;
};

/*
 * OTP-size selector -> geometry for the legacy wrap-type-0 diagnostic arm.
 * The 0x3d6a switch writes rows=96/cols=64/384 words for selector 5 on that
 * arm. Do not apply it to the captured BCM4352 type-1 target route: the
 * target's alternate arm sets the bit-address divisor to 0x20, which is
 * supplied only by ob_si_otp8e2_probe() for its isolated word-0x10 read.
 */
static const struct ob_otp_geometry ob_otp_geom_tbl[] = {
	[1] = {  32, 64, 128 },
	[2] = {  64, 64, 256 },
	[5] = {  96, 64, 384 },	/* BCM4352: CC_CAP=0x58680001 -> selector 5 */
	[7] = {  16, 64,  64 },
};

static const struct ob_otp_geometry *ob_otp_geometry(struct ob_hw *hw,
						     u32 *selector)
{
	u32 cap = ob_si_cc_read(hw, OB_CC_CAP);
	u32 sel = (cap & OB_CC_CAP_OTPSIZE_MASK) >> OB_CC_CAP_OTPSIZE_SHIFT;

	if (selector)
		*selector = sel;
	if (sel >= ARRAY_SIZE(ob_otp_geom_tbl))
		return NULL;
	if (!ob_otp_geom_tbl[sel].rows || !ob_otp_geom_tbl[sel].cols)
		return NULL;
	return &ob_otp_geom_tbl[sel];
}

struct ob_otp_bit_dbg {
	u32 bitaddr, row, col, cmd, val, otpl;
	bool busy, readerr, value, timeout;
};

/*
 * Raw OTP bit read — faithful port of blob 0x38d3 (table A, 40nm OTP):
 *   OTPC=0; OTPC1=0; read OTPL; row = bitaddr / cols; col = bitaddr % cols;
 *   OTPP = START | (row << 8) | col (opcode READ=0); poll OTPP while START
 *   (bounded); READERR on bit 28; data on bit 29. No WAKEUP/STBY and no init
 *   opcode is issued by the blob for this wrapper. Writes only OTP control
 *   registers, never a programming-enable bit.
 */
static int ob_otp_read_bit(struct ob_hw *hw, u32 cols, u32 bitaddr,
			   u32 *bit, struct ob_otp_bit_dbg *dbg)
{
	u32 row, col, cmd, v, otpl;
	bool timeout = false, readerr = false;
	int j;

	if (!hw || hw->dev_lost)
		return -EIO;

	row = bitaddr / cols;
	col = bitaddr % cols;
	cmd = OB_OTP_CMD_START | ((row & 0xff) << 8) | (col & 0xff);

	ob_si_cc_write(hw, OB_CC_OTPC, 0);
	if (hw->dev_lost)
		return -EIO;
	ob_si_cc_write(hw, OB_CC_OTPC1, 0);
	if (hw->dev_lost)
		return -EIO;
	otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	if (hw->dev_lost)
		return -EIO;
	ob_si_cc_write(hw, OB_CC_OTPP, cmd);
	if (hw->dev_lost)
		return -EIO;

	v = ob_si_cc_read(hw, OB_CC_OTPP);
	if (hw->dev_lost)
		return -EIO;
	for (j = 0; (v & OB_OTP_CMD_START) != 0; j++) {
		if (j >= OB_OTP_BUSY_MAX) {
			timeout = true;
			break;
		}
		v = ob_si_cc_read(hw, OB_CC_OTPP);
		if (hw->dev_lost)
			return -EIO;
	}
	if (!timeout)
		readerr = (v & OB_OTP_READERR) != 0;

	if (dbg) {
		dbg->bitaddr = bitaddr;
		dbg->row = row;
		dbg->col = col;
		dbg->cmd = cmd;
		dbg->val = v;
		dbg->otpl = otpl;
		dbg->busy = (v & OB_OTP_CMD_START) != 0;
		dbg->readerr = readerr;
		dbg->timeout = timeout;
		dbg->value = (!timeout && !readerr) &&
			     ((v & OB_OTP_VALUE) != 0);
	}

	if (timeout)
		return -ETIMEDOUT;
	if (readerr)
		return -EIO;
	*bit = (v & OB_OTP_VALUE) ? 1 : 0;
	return 0;
}

static int ob_otp_read_word16(struct ob_hw *hw, u32 cols, u32 word, u16 *out)
{
	u32 acc = 0;
	int i, err;

	for (i = 0; i < 16; i++) {
		struct ob_otp_bit_dbg dbg = { 0 };
		u32 bit = 0;

		err = ob_otp_read_bit(hw, cols, word * 16 + i, &bit, &dbg);
		if (err) {
			dev_warn(hw->dev,
				 "otpdiag: bit FAIL word=%#x bit=%d off=%u row=%u col=%u cmd(OTPP@0x18)=%08x OTPL(0x1c)=%08x OTPP=%08x BUSY=%d READERR=%d VALUE=%d timeout=%d err=%d\n",
				 word, i, dbg.bitaddr, dbg.row, dbg.col, dbg.cmd,
				 dbg.otpl, dbg.val, dbg.busy, dbg.readerr,
				 dbg.value, dbg.timeout, err);
			return err;
		}
		acc |= bit << i;
	}
	*out = acc & 0xffff;
	return 0;
}

/*
 * Blob 0x3fa3..0x3fdd: OTPP = 0x84000000, i.e. START | opcode 4. Opcode 4 is
 * WORD_VERIFY_1 (NOT an init), and the blob only issues it on the geometry arm
 * where the OTPL wrap_type != 1. For the BCM4352 (40nm, wrap_type == 1) the
 * blob skips it entirely, so we do too.
 */
static int ob_otp_word_verify1(struct ob_hw *hw, u32 otpl)
{
	u32 wrap_type = (otpl & OB_OTPL_WRAP_TYPE_MASK) >> OB_OTPL_WRAP_TYPE_SHIFT;
	u32 cmd = OB_OTP_CMD_START | (OB_OTP_OP_WORD_VERIFY_1 << OB_OTP_OPCODE_SHIFT);
	u32 v;
	int j;

	if (wrap_type == OB_OTPL_WRAP_TYPE_40NM)
		return 0;	/* 40nm/BCM4352: not part of the read sequence */

	ob_si_cc_write(hw, OB_CC_OTPP, cmd);
	v = ob_si_cc_read(hw, OB_CC_OTPP);
	for (j = 0; (v & OB_OTP_CMD_START) != 0; j++) {
		if (j >= OB_OTP_BUSY_MAX) {
			dev_warn(hw->dev,
				 "otpdiag[verify1]: opcode 4 timeout OTPP=%08x\n", v);
			return -ETIMEDOUT;
		}
		v = ob_si_cc_read(hw, OB_CC_OTPP);
	}
	dev_info(hw->dev, "otpdiag[verify1]: opcode 4 WORD_VERIFY_1 done OTPP=%08x\n", v);
	return 0;
}

/*
 * NON-DESTRUCTIVE-TO-OTP diagnostic probe (M2.5c). It writes OTP *control*
 * registers (OTPC/OTP_CFG/OTPD) but never a programming bit and never programs
 * OTP. It does not replace the mac80211 hardware address; it only reports.
 */
int ob_si_otp_diag(struct ob_hw *hw)
{
	const struct ob_otp_geometry *geom;
	u32 otpc, otpc1, otpc1_save, otpc_save;
	u32 pmu_stat, resmin, selector = 0, cap;
	u32 otpl, wrap_type, wrap_rev, row_size, hwbase;
	u16 word0 = 0;
	bool was_powered;
	int i, err, ret = 0;

	dev_info(hw->dev, "otpdiag: ===== NON-DESTRUCTIVE-TO-OTP diagnostic probe =====\n");

	/* ---- A. before state ---- */
	ob_si_dump(hw);
	otpc = ob_si_cc_read(hw, OB_CC_OTPC);
	otpc1 = ob_si_cc_read(hw, OB_CC_OTPC1);
	otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	pmu_stat = ob_si_cc_read(hw, OB_CC_PMU_OTPSTAT);
	resmin = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
	wrap_type = (otpl & OB_OTPL_WRAP_TYPE_MASK) >> OB_OTPL_WRAP_TYPE_SHIFT;
	wrap_rev = (otpl & OB_OTPL_WRAP_REV_MASK) >> OB_OTPL_WRAP_REV_SHIFT;
	row_size = (otpl & OB_OTPL_ROW_SIZE_MASK) >> OB_OTPL_ROW_SIZE_SHIFT;
	hwbase = (otpl & OB_OTPL_GURGN_OFFSET_MASK) >> 4;
	dev_info(hw->dev,
		 "otpdiag[before]: otpc=%08x otpc1=%08x otpl=%08x pmu_stat=%08x minres=%08x\n",
		 otpc, otpc1, otpl, pmu_stat, resmin);
	dev_info(hw->dev,
		 "otpdiag[otpl]: wrap_type=%u wrap_rev=%u row_size=%u hw_off_bits=%u hwbase(words)=%u\n",
		 wrap_type, wrap_rev, row_size,
		 otpl & OB_OTPL_GURGN_OFFSET_MASK, hwbase);
	ob_si_dump_sprom(hw, "otp:before");

	otpc_save = otpc;
	otpc1_save = otpc1;
	was_powered = (pmu_stat & OB_OTP_PWR_BIT) != 0;

	/* ---- B. power OTP (BCM4352 C2 sequence), bounded ---- */
	if (!was_powered) {
		resmin = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
		ob_si_cc_write(hw, OB_CC_PMU_MINRES_MSK, resmin | OB_OTP_PWR_BIT);
		udelay(1000);
		for (i = 0; i < 2000; i++) {
			pmu_stat = ob_si_cc_read(hw, OB_CC_PMU_OTPSTAT);
			if (pmu_stat & OB_OTP_PWR_BIT)
				break;
			udelay(10);
		}
	}
	dev_info(hw->dev,
		 "otpdiag[power]: was_powered=%d now_powered=%d pmu_stat=%08x minres=%08x\n",
		 was_powered, !!(pmu_stat & OB_OTP_PWR_BIT), pmu_stat,
		 ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK));
	if (!(pmu_stat & OB_OTP_PWR_BIT)) {
		dev_warn(hw->dev, "otpdiag: OTP did not report powered; abort\n");
		ret = -ETIMEDOUT;
		goto restore;
	}

	/* ---- C. re-read traditional shadow (diagnostic only) ---- */
	ob_si_dump_sprom(hw, "otp:after-power");

	/*
	 * ---- D. geometry from the CC_CAP OTP-size selector ----
	 * ChipCommon+0x10 is BCMA_CC_OTPS (OTP status/flags), NOT a geometry
	 * divisor; the old readl(CC+0x10) derivation was wrong. The blob derives
	 * the geometry from the capability selector instead (otp_init 0x3c3f /
	 * 0x3dc7). No OTP word is read to discover the divisor (no circularity).
	 */
	cap = ob_si_cc_read(hw, OB_CC_CAP);
	geom = ob_otp_geometry(hw, &selector);
	if (!geom) {
		dev_warn(hw->dev,
			 "otpdiag[geom]: cap=%08x selector=%u has no recovered geometry; abort (no guessing)\n",
			 cap, selector);
		ret = -EOPNOTSUPP;
		goto restore;
	}
	dev_info(hw->dev,
		 "otpdiag[geom]: cap=%08x selector=%u rows=%u cols=%u words=%u divisor(cols)=%u\n",
		 cap, selector, geom->rows, geom->cols, geom->words, geom->cols);

	/* ---- D2. Blob opcode-4 WORD_VERIFY_1 (only when wrap_type != 40nm) ---- */
	err = ob_otp_word_verify1(hw, otpl);
	if (err)
		dev_warn(hw->dev,
			 "otpdiag: WORD_VERIFY_1 did not complete (%d); continuing\n",
			 err);

	/*
	 * ---- E. SINGLE-WORD diagnostic: absolute OTP word 0x46 only. ----
	 * srom_read(region 1) maps buffer word i -> absolute OTP word hwbase+i,
	 * so SPROM byte 0x4c (buffer word 0x26) is absolute word hwbase+0x26 =
	 * 0x20+0x26 = 0x46. Words 0x47/0x48 and the MAC decode are deferred.
	 */
	{
		u32 bitaddr = (hwbase + 0x26) * 16;
		u32 row0 = bitaddr / geom->cols;
		u32 col0 = bitaddr % geom->cols;
		u32 cmd0 = OB_OTP_CMD_START | ((row0 & 0xff) << 8) |
			   (col0 & 0xff);

		/* Requirement: log pre-read control state and the first command. */
		dev_info(hw->dev,
			 "otpdiag[pre-read]: otpc=%08x otpc1=%08x otpl=%08x otps=%08x otpp=%08x\n",
			 ob_si_cc_read(hw, OB_CC_OTPC),
			 ob_si_cc_read(hw, OB_CC_OTPC1),
			 ob_si_cc_read(hw, OB_CC_OTPL),
			 ob_si_cc_read(hw, OB_CC_OTPS),
			 ob_si_cc_read(hw, OB_CC_OTPP));
		dev_info(hw->dev,
			 "otpdiag[addr]: word=%#x bit=0 offset=%u row=%u col=%u cmd=%08x opcode=%u\n",
			 hwbase + 0x26, bitaddr, row0, col0, cmd0,
			 (cmd0 & OB_OTP_OPCODE_MASK) >> OB_OTP_OPCODE_SHIFT);
	}

	err = ob_otp_read_word16(hw, geom->cols, hwbase + 0x26, &word0);
	if (err) {
		dev_warn(hw->dev,
			 "otpdiag: absolute OTP word %#x (buffer %#x) read failed (%d); STOP\n",
			 hwbase + 0x26, 0x26, err);
		ret = err;
		goto restore;
	}

	/* Success: report the raw 16-bit value and STOP (no 0x47/0x48 yet). */
	dev_info(hw->dev, "otpdiag[word %#x]: raw=%04x (all 16 bits, no READERR)\n",
		 hwbase + 0x26, word0);
	ret = 0;
	goto restore;

restore:
	/* ---- I. symmetric power-down + control restore, only if we changed it ---- */
	if (!was_powered) {
		resmin = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
		ob_si_cc_write(hw, OB_CC_PMU_MINRES_MSK, resmin & ~OB_OTP_PWR_BIT);
	}
	ob_si_cc_write(hw, OB_CC_OTPC, otpc_save);
	ob_si_cc_write(hw, OB_CC_OTPC1, otpc1_save);
	dev_info(hw->dev, "otpdiag: ===== done (ret=%d) =====\n", ret);
	return ret;
}

struct ob_si_otp8e2_context {
	struct ob_hw *hw;
	u16 word;
	u8 value;
	bool jbe_le3;
	bool observed;
	int read_error;
};

static int ob_si_otp8e2_read_word(void *ctx, u16 word_index, u16 columns,
				  u16 *word)
{
	struct ob_si_otp8e2_context *observation = ctx;

	if (!observation || !observation->hw || !word ||
	    word_index != OB_OTP8E2_WORD_INDEX ||
	    columns != OB_OTP8E2_COLUMNS)
		return -EINVAL;
	if (observation->hw->dev_lost)
		return -EIO;

	return ob_otp_read_word16(observation->hw, columns, word_index, word);
}

static void ob_si_otp8e2_record(void *ctx, u16 word, u8 value, bool jbe_le3)
{
	struct ob_si_otp8e2_context *observation = ctx;

	observation->word = word;
	observation->value = value;
	observation->jbe_le3 = jbe_le3;
	observation->observed = true;
}

static void ob_si_otp8e2_record_failure(void *ctx, int ret)
{
	struct ob_si_otp8e2_context *observation = ctx;

	observation->read_error = ret;
}

static const struct ob_otp8e2_ops ob_si_otp8e2_ops = {
	.read_word16 = ob_si_otp8e2_read_word,
	.observation = ob_si_otp8e2_record,
	.failure = ob_si_otp8e2_record_failure,
};

/*
 * Isolated word-0x10 observation for the post-BMAC branch-gate mode. The
 * accepted historical target route is OTP selector 5 with OTPL wrap type 1;
 * that route's bit-address divisor is 0x20. Keep the shared selector-5
 * geometry (wrap type 0, 96x64) unchanged and pass the proven type-1 divisor
 * only for this exact test mode. The one observation is retained in hw so the
 * later isolated continuation can admit only the accepted selected word.
 */
static int ob_si_otp8e2_probe(struct ob_hw *hw)
{
	u32 cap, otpl, otpc_saved = 0, otpc1_saved = 0;
	u32 pmu_stat, minres_saved = 0, minres;
	u32 selector, wrap_type;
	bool controls_saved = false, minres_changed = false;
	bool was_powered = false;
	struct ob_si_otp8e2_context observation = { .hw = hw };
	const char *failure_stage = "TARGET";
	int i, ret = 0, restore_ret = 0;

	if (!hw)
		return -EINVAL;
	hw->postbmac_otp8e2_valid = false;
	if (!hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only ||
	    hw->chip_id != 0x4352 || hw->chip_rev != 3 || !hw->core ||
	    hw->core->id.id != BCMA_CORE_80211 || hw->core->id.rev != 42) {
		ret = -ENODEV;
		goto report_failure;
	}
	if (hw->dev_lost) {
		ret = -EIO;
		failure_stage = "DEVICE_LOST";
		goto report_failure;
	}

	cap = ob_si_cc_read(hw, OB_CC_CAP);
	if (hw->dev_lost) {
		ret = -EIO;
		failure_stage = "DEVICE_LOST";
		goto report_failure;
	}
	selector = (cap & OB_CC_CAP_OTPSIZE_MASK) >> OB_CC_CAP_OTPSIZE_SHIFT;
	if (selector != 5) {
		ret = -EOPNOTSUPP;
		failure_stage = "GEOMETRY";
		goto report_failure;
	}

	otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	if (hw->dev_lost) {
		ret = -EIO;
		failure_stage = "DEVICE_LOST";
		goto report_failure;
	}
	wrap_type = (otpl & OB_OTPL_WRAP_TYPE_MASK) >>
		    OB_OTPL_WRAP_TYPE_SHIFT;
	if (wrap_type != OB_OTPL_WRAP_TYPE_40NM) {
		ret = -EOPNOTSUPP;
		failure_stage = "GEOMETRY";
		goto report_failure;
	}

	otpc_saved = ob_si_cc_read(hw, OB_CC_OTPC);
	if (hw->dev_lost) {
		ret = -EIO;
		failure_stage = "DEVICE_LOST";
		goto report_failure;
	}
	otpc1_saved = ob_si_cc_read(hw, OB_CC_OTPC1);
	if (hw->dev_lost) {
		ret = -EIO;
		failure_stage = "DEVICE_LOST";
		goto report_failure;
	}
	controls_saved = true;
	pmu_stat = ob_si_cc_read(hw, OB_CC_PMU_OTPSTAT);
	if (hw->dev_lost) {
		ret = -EIO;
		failure_stage = "DEVICE_LOST";
		goto restore;
	}
	was_powered = (pmu_stat & OB_OTP_PWR_BIT) != 0;

	/* Reuse only the existing bounded BCM4352 OTP power-up protocol. */
	if (!was_powered) {
		failure_stage = "POWER";
		minres_saved = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
		if (hw->dev_lost) {
			ret = -EIO;
			failure_stage = "DEVICE_LOST";
			goto restore;
		}
		ob_si_cc_write(hw, OB_CC_PMU_MINRES_MSK,
			       minres_saved | OB_OTP_PWR_BIT);
		if (hw->dev_lost) {
			ret = -EIO;
			failure_stage = "DEVICE_LOST";
			goto restore;
		}
		minres_changed = true;
		udelay(1000);
		for (i = 0; i < 2000; i++) {
			pmu_stat = ob_si_cc_read(hw, OB_CC_PMU_OTPSTAT);
			if (hw->dev_lost) {
				ret = -EIO;
				failure_stage = "DEVICE_LOST";
				goto restore;
			}
			if (pmu_stat & OB_OTP_PWR_BIT)
				break;
			udelay(10);
		}
		if (!(pmu_stat & OB_OTP_PWR_BIT)) {
			ret = -ETIMEDOUT;
			goto restore;
		}
	}

	failure_stage = "READ_WORD";
	ret = ob_otp8e2_observe_word10(&ob_si_otp8e2_ops, &observation);
	if (ret) {
		if (observation.read_error)
			ret = observation.read_error;
		goto restore;
	}

restore:
	/* Never issue cleanup MMIO after the monotonic device-loss latch. */
	if (controls_saved && !hw->dev_lost) {
		if (minres_changed) {
			minres = ob_si_cc_read(hw, OB_CC_PMU_MINRES_MSK);
			if (hw->dev_lost) {
				restore_ret = -EIO;
			} else {
				minres = (minres & ~OB_OTP_PWR_BIT) |
					 (minres_saved & OB_OTP_PWR_BIT);
				ob_si_cc_write(hw, OB_CC_PMU_MINRES_MSK, minres);
				if (hw->dev_lost)
					restore_ret = -EIO;
			}
		}
		if (!hw->dev_lost) {
			ob_si_cc_write(hw, OB_CC_OTPC, otpc_saved);
			if (hw->dev_lost)
				restore_ret = -EIO;
		}
		if (!hw->dev_lost) {
			ob_si_cc_write(hw, OB_CC_OTPC1, otpc1_saved);
			if (hw->dev_lost)
				restore_ret = -EIO;
		}
	}
	if (restore_ret && !ret) {
		ret = restore_ret;
		failure_stage = hw->dev_lost ? "DEVICE_LOST" : "RESTORE";
	}
	if (hw->dev_lost && !ret) {
		ret = -EIO;
		failure_stage = "DEVICE_LOST";
	}
	if (!ret && !observation.observed) {
		ret = -EIO;
		failure_stage = "READ_WORD";
	}
	if (ret)
		goto report_failure;
	hw->postbmac_otp8e2_word = observation.word;
	hw->postbmac_otp8e2_value = observation.value;
	hw->postbmac_otp8e2_valid = true;

	dev_info(hw->dev,
		 "ACPHY_OTP8E2_OBS word=0x%04x value=0x%02x jbe_le3=%u\n",
		 observation.word, observation.value, observation.jbe_le3);
	return 0;

report_failure:
	dev_info(hw->dev, "ACPHY_OTP8E2_FAIL stage=%s ret=%d\n",
		 failure_stage, ret);
	return ret;
}

/*
 * BCMA's SPROM CRC8 (polynomial x^8 + x^7 + x^6 + x^4 + x^2 + 1). The table and
 * algorithm are reproduced from Linux drivers/bcma/sprom.c so the external
 * SPROM is validated exactly as the in-kernel bus driver validates it.
 */
static u8 ob_sprom_crc8(u8 crc, u8 data)
{
	static const u8 t[256] = {
	0x00, 0xF7, 0xB9, 0x4E, 0x25, 0xD2, 0x9C, 0x6B,
	0x4A, 0xBD, 0xF3, 0x04, 0x6F, 0x98, 0xD6, 0x21,
	0x94, 0x63, 0x2D, 0xDA, 0xB1, 0x46, 0x08, 0xFF,
	0xDE, 0x29, 0x67, 0x90, 0xFB, 0x0C, 0x42, 0xB5,
	0x7F, 0x88, 0xC6, 0x31, 0x5A, 0xAD, 0xE3, 0x14,
	0x35, 0xC2, 0x8C, 0x7B, 0x10, 0xE7, 0xA9, 0x5E,
	0xEB, 0x1C, 0x52, 0xA5, 0xCE, 0x39, 0x77, 0x80,
	0xA1, 0x56, 0x18, 0xEF, 0x84, 0x73, 0x3D, 0xCA,
	0xFE, 0x09, 0x47, 0xB0, 0xDB, 0x2C, 0x62, 0x95,
	0xB4, 0x43, 0x0D, 0xFA, 0x91, 0x66, 0x28, 0xDF,
	0x6A, 0x9D, 0xD3, 0x24, 0x4F, 0xB8, 0xF6, 0x01,
	0x20, 0xD7, 0x99, 0x6E, 0x05, 0xF2, 0xBC, 0x4B,
	0x81, 0x76, 0x38, 0xCF, 0xA4, 0x53, 0x1D, 0xEA,
	0xCB, 0x3C, 0x72, 0x85, 0xEE, 0x19, 0x57, 0xA0,
	0x15, 0xE2, 0xAC, 0x5B, 0x30, 0xC7, 0x89, 0x7E,
	0x5F, 0xA8, 0xE6, 0x11, 0x7A, 0x8D, 0xC3, 0x34,
	0xAB, 0x5C, 0x12, 0xE5, 0x8E, 0x79, 0x37, 0xC0,
	0xE1, 0x16, 0x58, 0xAF, 0xC4, 0x33, 0x7D, 0x8A,
	0x3F, 0xC8, 0x86, 0x71, 0x1A, 0xED, 0xA3, 0x54,
	0x75, 0x82, 0xCC, 0x3B, 0x50, 0xA7, 0xE9, 0x1E,
	0xD4, 0x23, 0x6D, 0x9A, 0xF1, 0x06, 0x48, 0xBF,
	0x9E, 0x69, 0x27, 0xD0, 0xBB, 0x4C, 0x02, 0xF5,
	0x40, 0xB7, 0xF9, 0x0E, 0x65, 0x92, 0xDC, 0x2B,
	0x0A, 0xFD, 0xB3, 0x44, 0x2F, 0xD8, 0x96, 0x61,
	0x55, 0xA2, 0xEC, 0x1B, 0x70, 0x87, 0xC9, 0x3E,
	0x1F, 0xE8, 0xA6, 0x51, 0x3A, 0xCD, 0x83, 0x74,
	0xC1, 0x36, 0x78, 0x8F, 0xE4, 0x13, 0x5D, 0xAA,
	0x8B, 0x7C, 0x32, 0xC5, 0xAE, 0x59, 0x17, 0xE0,
	0x2A, 0xDD, 0x93, 0x64, 0x0F, 0xF8, 0xB6, 0x41,
	0x60, 0x97, 0xD9, 0x2E, 0x45, 0xB2, 0xFC, 0x0B,
	0xBE, 0x49, 0x07, 0xF0, 0x9B, 0x6C, 0x22, 0xD5,
	0xF4, 0x03, 0x4D, 0xBA, 0xD1, 0x26, 0x68, 0x9F,
	};

	return t[crc ^ data];
}

static u8 ob_sprom_crc(const u16 *sprom, size_t words)
{
	u8 crc = 0xFF;
	int word;

	for (word = 0; word < words - 1; word++) {
		crc = ob_sprom_crc8(crc, sprom[word] & 0x00FF);
		crc = ob_sprom_crc8(crc, (sprom[word] & 0xFF00) >> 8);
	}
	crc = ob_sprom_crc8(crc, sprom[words - 1] & 0x00FF);
	return crc ^ 0xFF;
}

/* Log the SPROM data the in-kernel bcma bus driver already populated. */
static void ob_si_dump_bus_sprom(struct ob_hw *hw)
{
	struct bcma_bus *bus = hw->bus;

	dev_info(hw->dev,
		 "bcmasp: revision=%u board_rev=%u board_type=%u board_num=%u vendor=%u type=%u\n",
		 bus->sprom.revision, bus->sprom.board_rev, bus->sprom.board_type,
		 bus->sprom.board_num, bus->boardinfo.vendor,
		 bus->boardinfo.type);
	/* Shown for comparison only; NOT used for the hardware address. */
	dev_info(hw->dev, "bcmasp(bcma-parsed,not used): il0mac=%pM et0mac=%pM\n",
		 bus->sprom.il0mac, bus->sprom.et0mac);
}

static void ob_si_dump_sprom_window(struct ob_hw *hw, u16 start,
				    unsigned int bytes)
{
	unsigned int off;

	for (off = 0; off < bytes; off += 16) {
		char line[160];
		int n = 0, k;

		n += scnprintf(line + n, sizeof(line) - n, "spromdiag[0x%x]:",
			       OB_CC_SPROM + start + off);
		for (k = 0; k < 8; k++)
			n += scnprintf(line + n, sizeof(line) - n, " %04x",
				       ob_si_cc_read16(hw, OB_CC_SPROM + start +
						       off + k * 2));
		dev_info(hw->dev, "%s\n", line);
	}
}

/*
 * D3B MHF3 evidence emission: dump the external SPROM image that the caller
 * has ALREADY read and CRC-validated. This performs ZERO ChipCommon/SPROM
 * MMIO — it only formats the existing `sp[]` array. Raw evidence only; field
 * interpretation is done offline by scripts/sprom11_decode.py. It is reached
 * only from the sprom_diag-gated diagnostic, so `sprom_diag=0` emits nothing.
 */
#define OB_SPROM11_WORDS_PER_LINE	8

static void ob_si_emit_sprom11(struct ob_hw *hw, const u16 *sp, size_t words,
			       u8 rev, u8 ecrc, u8 crc)
{
	char line[OB_SPROM11_WORDS_PER_LINE * 6 + 32];
	unsigned int i, k;
	bool valid;

	/* Only the full rev10/11-sized image is part of this evidence. */
	if (words != OB_SPROM_WORDS_R11)
		return;
	valid = (rev == 11) && (crc == ecrc);

	dev_info(hw->dev,
		 "openbrcm: sprom11: BEGIN words=%zu revision=%u crc=%02x calc=%02x valid=%d\n",
		 words, rev, ecrc, crc, valid);
	for (i = 0; i < words; i += OB_SPROM11_WORDS_PER_LINE) {
		int n = scnprintf(line, sizeof(line),
				  "openbrcm: sprom11: %03u:", i);

		for (k = 0; k < OB_SPROM11_WORDS_PER_LINE && i + k < words; k++)
			n += scnprintf(line + n, sizeof(line) - n, " %04x",
				       sp[i + k]);
		dev_info(hw->dev, "%s\n", line);
	}
	dev_info(hw->dev, "openbrcm: sprom11: END words=%zu\n", words);
}

/*
 * Read-only external-SPROM diagnostic (M2.5f). Reads through the ChipCommon
 * window with 16-bit accesses exactly like bcma_sprom_read(); writes nothing.
 * Validates CRC + revision the same way bcma_sprom_valid() does.
 */
static int ob_si_sprom_diag(struct ob_hw *hw)
{
	u32 srom_ctl = ob_si_cc_read(hw, OB_CC_SROM_CONTROL);
	u32 otps = ob_si_cc_read(hw, OB_CC_OTPS);
	u32 cap = ob_si_cc_read(hw, OB_CC_CAP);
	u16 revw;
	bool valid = false;
	u16 *sp;
	int i;

	dev_info(hw->dev, "spromdiag: ===== read-only external SPROM probe =====\n");
	dev_info(hw->dev, "spromdiag[ctl]: cap=%08x srom_control=%08x otps=%08x\n",
		 cap, srom_ctl, otps);
	dev_info(hw->dev,
		 "spromdiag[ctl]: present=%d size=%u otpsel=%d otp_present=%d cap_sprom=%d\n",
		 !!(srom_ctl & OB_SROM_PRESENT),
		 (srom_ctl & OB_SROM_SIZE_MASK) >> OB_SROM_SIZE_SHIFT,
		 !!(srom_ctl & OB_SROM_OTPSEL),
		 !!(srom_ctl & OB_SROM_OTP_PRESENT),
		 !!(cap & OB_CC_CAP_SPROM));
	dev_info(hw->dev,
		 "spromdiag[otps]: GU_PROG_IND=%u GU_PROG_HW=%d protect=%u\n",
		 (otps & OB_OTPS_GU_PROG_IND) >> 8,
		 !!(otps & OB_OTPS_GU_PROG_HW), otps & 0x7);

	/* Window 0x800..0x8a0, 16-bit reads, exactly like bcma_sprom_read(). */
	ob_si_dump_sprom_window(hw, 0, 0xa0);

	/*
	 * The rev@0x7e word is the legacy 64-word-tail convention; the rev8/rev11
	 * revision actually lives in the last word of the probed size (reported in
	 * the bcma-style block below and by ob_si_read_mac()). MAC extraction is
	 * revision-aware in ob_si_read_mac() (rev8 +0x8C, rev11 +0x90), so it is
	 * not duplicated here.
	 */
	revw = ob_si_cc_read16(hw, OB_CC_SPROM + OB_SPROM8_REVISION);
	dev_info(hw->dev, "spromdiag[rev@0x%x]: raw=%04x rev=%u crc8=%02x\n",
		 OB_SPROM8_REVISION, revw, revw & 0xff, revw >> 8);

	/* BCMA-style full-size read + CRC/revision validation. */
	sp = kcalloc(OB_SPROM_WORDS_R11, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;

	for (i = 0; i < 3; i++) {
		size_t words = (i == 0) ? OB_SPROM_WORDS_R4 :
			       (i == 1) ? OB_SPROM_WORDS_R10 :
					  OB_SPROM_WORDS_R11;
		u8 crc, erev, ecrc;
		int k;

		for (k = 0; k < words; k++)
			sp[k] = ob_si_cc_read16(hw, OB_CC_SPROM + k * 2);
		crc = ob_sprom_crc(sp, words);
		erev = sp[words - 1] & 0xff;
		ecrc = sp[words - 1] >> 8;
		dev_info(hw->dev,
			 "spromdiag[bcma]: words=%zu rev=%u crc=%02x calc=%02x %s rev_ok=%d\n",
			 words, erev, ecrc, crc, crc == ecrc ? "MATCH" : "MISMATCH",
			 erev >= 8 && erev <= 11);
		if (crc == ecrc && erev >= 8 && erev <= 11)
			valid = true;
	}

	/*
	 * D3B MHF3 evidence: emit the ALREADY-read 234-word image from sp[]
	 * (zero additional MMIO). sp[] holds the last loop iteration, which is
	 * the full rev10/11-sized read.
	 */
	{
		u8 crc234 = ob_sprom_crc(sp, OB_SPROM_WORDS_R11);
		u8 rev234 = sp[OB_SPROM_WORDS_R11 - 1] & 0xff;
		u8 ecrc234 = sp[OB_SPROM_WORDS_R11 - 1] >> 8;

		ob_si_emit_sprom11(hw, sp, OB_SPROM_WORDS_R11, rev234,
				   ecrc234, crc234);
	}
	kfree(sp);

	dev_info(hw->dev,
		 "spromdiag: result external_sprom_valid=%d (bcma CRC+rev)\n",
		 valid);
	dev_info(hw->dev, "spromdiag: ===== done =====\n");
	return valid ? 0 : -ENODATA;
}

/*
 * Minimal board-data preparation for the isolated D3A1 tail test.
 *
 * The D3A1 path is selected BEFORE the normal ob_si_probe() path, so it must
 * obtain the two pieces of board data D3A1 needs without importing any of the
 * normal probe side effects:
 *
 *   - hw->cc = hw->bus->drv_cc.core, verified non-NULL (ChipCommon is the
 *     CC/PMU/PLL/SPROM window used by switch_macfreq);
 *   - the already hardware-proven READ-ONLY external-SPROM MAC path
 *     (ob_si_read_mac) into hw->mac/hw->mac_valid.
 *
 * It performs NO D11/PMU/PLL/core writes, no power-up, no diagnostics and no
 * DMA/IRQ/RX/mac80211 initialization. The only PMU PLL accesses in the D3A1
 * path remain inside the exact switch_macfreq implementation.
 */
int ob_si_prepare_board_data_for_d3a1(struct ob_hw *hw)
{
	int ret;

	hw->cc = hw->bus->drv_cc.core;
	if (!hw->cc) {
		dev_err(hw->dev, "d3a1-test: no ChipCommon core\n");
		return -ENODEV;
	}

	/*
	 * Read-only external-SPROM MAC (reuses the proven parser). Preserve the
	 * exact errno: a bad CRC, an invalid MAC or an allocation failure must
	 * fail board-data preparation BEFORE any D2A/D2B work, not be flattened
	 * into mac_valid=false with a success return.
	 */
	ret = ob_si_read_mac(hw, hw->mac);
	hw->mac_valid = (ret == 0);
	if (ret) {
		dev_err(hw->dev,
			"d3a1-test: board-data MAC read failed ret=%d (mac_valid=0)\n",
			ret);
		return ret;
	}

	dev_info(hw->dev,
		 "d3a1-test: board data prepared (cc=%p mac_valid=%d mac=%pM)\n",
		 hw->cc, hw->mac_valid, hw->mac);
	return 0;
}

/*
 * Minimal board-data preparation for the isolated D3B band-init test.
 *
 * Identical read-only preparation to the D3A1 path, but D3B additionally needs
 * the proven rev11 board fields (boardtype/boardflags/aa2g/aa5g/antswitch),
 * which ob_si_read_mac() now decodes from the same already-read image into
 * hw->board (no extra MMIO). A non-rev11/unvalidated image leaves
 * hw->board.valid=false and ob_d3b_test() refuses.
 */
int ob_si_prepare_board_data_for_d3b(struct ob_hw *hw)
{
	int ret = ob_si_prepare_board_data_for_d3a1(hw);

	if (ret)
		return ret;
	if (!hw->board.valid)
		dev_warn(hw->dev,
			 "d3b-test: rev11 board data not available (board.valid=0)\n");
	return 0;
}

/*
 * Endpoint-loss bisection L2 (acphy_identity_timing_only): exactly one 16-bit
 * read of the D11 PHY-version register, immediately after the eight timing
 * reads and in the same window selection, timed and logged in the same format
 * so it can be compared line-for-line with the L1 trace.
 *
 * Offset and width are the production ones: ob_acphy_attach_identity() reads
 * 0x03e0 with a 16-bit access and OB_DIAG_REG_PHY_VER names the same register.
 * A 32-bit read here would be a different transaction and would break the
 * comparison. No write precedes it in this mode; no second read follows it.
 *
 * This is deliberately not inside ob_si_d11_diag(): that function is the
 * already validated d11_transaction_timing_only engine, whose machine-checked
 * contract is "exactly eight reads and no PHY-version access" (see
 * tests/host/test_d11_transaction_timing.py and test_d11_phyversion_boundary.py).
 * Keeping the L2 read here leaves that contract literally true rather than
 * relaxing the tests that enforce it.
 */
static void ob_si_d11_identity_timing(struct ob_hw *hw)
{
	u64 id_start = ktime_get_mono_fast_ns();
	u16 phyver = ob_d11_read16(hw, 0x03e0u);
	u64 id_end = ktime_get_mono_fast_ns();

	dev_info(hw->dev,
		 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%08x start_ns=%llu end_ns=%llu duration_us=%llu\n",
		 0x03e0u, 16u, (u32)phyver,
		 (unsigned long long)id_start,
		 (unsigned long long)id_end,
		 (unsigned long long)div_u64(id_end - id_start, NSEC_PER_USEC));
	dev_info(hw->dev,
		 "ACPHY_IDENTITY_TIMING: phyver=0x%04x dev_lost=%d read_before_radio_prepare=1\n",
		 phyver, hw->dev_lost ? 1 : 0);
	if (phyver == 0xffffu)
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_TIMING: 0x3e0 returned all-ones WITHOUT ob_radio_prepare_for_identity(); the attach prefix is not a necessary condition for this result\n");
}

/*
 * Endpoint-loss bisection L2b (acphy_identity_3e2_first): one 16-bit read of
 * 0x03e2, the upper half of the same D11 dword as the PHY-version register at
 * 0x03e0, issued immediately before the L2 read.
 *
 * The point of the address is its indivisibility from 0x03e0 at every layer
 * below the device: same dword, same window, same accessor, same width, same
 * timing wrapper. If 0x03e2 returns quickly and 0x03e0 then stalls, the failure
 * is inside the device's 16-bit decode of one address; if both stall, it is
 * dword- or region-wide. 0x03e2 carries no vendor meaning and its value is
 * reported only as the contrast.
 *
 * Fail-stop: a read at or beyond OB_L2_3E2_STALL_US, or a device already
 * marked lost, ends the run before 0x03e0 is issued. 10 ms is five thousand
 * times a healthy 2 us read and twenty times below the observed 201.7 ms
 * signature, so it cannot be crossed by scheduling noise alone.
 */
#define OB_L2_3E2_STALL_US	10000ULL

static bool ob_si_d11_identity_3e2_probe(struct ob_hw *hw)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u16 val = ob_d11_read16(hw, 0x03e2u);
	u64 end_ns = ktime_get_mono_fast_ns();
	u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	bool stalled = dur_us >= OB_L2_3E2_STALL_US;

	dev_info(hw->dev,
		 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%08x start_ns=%llu end_ns=%llu duration_us=%llu\n",
		 0x03e2u, 16u, (u32)val,
		 (unsigned long long)start_ns, (unsigned long long)end_ns,
		 (unsigned long long)dur_us);
	dev_info(hw->dev,
		 "ACPHY_IDENTITY_3E2: value=0x%04x dev_lost=%d duration_us=%llu stalled=%d\n",
		 val, hw->dev_lost ? 1 : 0, (unsigned long long)dur_us,
		 stalled ? 1 : 0);

	return !hw->dev_lost && !stalled;
}

/*
 * Endpoint-loss bisection L2c (acphy_identity_span_probe): one 16-bit read of
 * 0x03e4 and one of 0x03e6 — the dword immediately above the PHY-version word —
 * issued before the 0x3e2/0x3e0 pair.
 *
 * L2 and L2b established that both halves of the 0x03e0 dword fail identically
 * (0xffff after ~201.7 ms), so the individual address is not the unit. This
 * probe asks whether the failure stops at the dword boundary or continues into
 * the next one. 0x03e6 is written by the vendor radio path
 * (ob_d11_write16(hw, 0x03e6u, 0)), so the dword is a functional register and
 * not a hole. Nothing here writes: both accesses are reads.
 *
 * The order is forced by the knowledge already in hand: 0x03e2 is known to
 * stall, so any probe placed after it would never run.
 *
 * Fail-stop at the first of the two addresses that stalls or reports device
 * loss, before the second one and before 0x3e2/0x3e0 are issued.
 */
static bool ob_si_d11_identity_span_probe(struct ob_hw *hw)
{
	static const u16 offs[2] = { 0x03e4u, 0x03e6u };
	int i;

	for (i = 0; i < ARRAY_SIZE(offs); i++) {
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 val = ob_d11_read16(hw, offs[i]);
		u64 end_ns = ktime_get_mono_fast_ns();
		u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		bool stalled = dur_us >= OB_L2_3E2_STALL_US;

		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%08x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 offs[i], 16u, (u32)val,
			 (unsigned long long)start_ns, (unsigned long long)end_ns,
			 (unsigned long long)dur_us);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_SPAN: offset=0x%04x value=0x%04x dev_lost=%d duration_us=%llu stalled=%d\n",
			 offs[i], val, hw->dev_lost ? 1 : 0,
			 (unsigned long long)dur_us, stalled ? 1 : 0);
		if (hw->dev_lost || stalled)
			return false;
	}

	return true;
}

/*
 * Endpoint-loss bisection L2d (acphy_identity_control_reprobe): re-read
 * D11+0x0120, MACCONTROL, as the FIRST access after the eight bring-up reads.
 *
 * Every PHY-block probe so far (0x3e0, 0x3e2, 0x3e4) was also the first access
 * after that block, so all of them confounded two explanations: "this address
 * or block is bad" and "the endpoint stopped answering altogether". This probe
 * uses an offset that returned 0x00000000 in 2 us microseconds earlier, which
 * is the only way to separate them:
 *
 *   stall here  -> the endpoint stopped responding; the address is irrelevant;
 *   fast here   -> the endpoint is alive and the 0x03e0 block is specifically bad.
 *
 * A 32-bit read through the same ob_d11_read32() used by the bring-up reads keeps
 * the comparison single-variable. Nothing here writes.
 */
/*
 * Endpoint-loss bisection L2e (acphy_identity_control16_reprobe): read the
 * SAME known-good offset as L2d, D11+0x0120, with a 16-bit access, as the FIRST
 * access after the eight-read block.
 *
 * L2d settled "the endpoint stopped answering": a 32-bit read of 0x0120
 * completed in 3 us, 6.9 us before 0x3e0 entered its 201.7 ms timeout. What it
 * could not settle is width. Every failing access was 16-bit and no successful
 * 16-bit read exists anywhere in the L2 family, so "the 0x03e0 block" and
 * "16-bit MMIO to this function" were still confounded.
 *
 * This probe holds the address fixed at a value proven to answer and changes
 * only the width. Fast here -> the 0x03e0 block is the determinant; stall here
 * -> the 16-bit access path is. A >10 ms stall is latched as device loss so the
 * retention path runs instead of any teardown.
 */
/*
 * Endpoint-loss bisection L2f (acphy_identity_upper_probe): read the 16-bit
 * word above the failing dword, D11+0x03e6, and only if that completes also
 * D11+0x03e8 — as the first accesses after the eight-read block.
 *
 * L2/L2b/L2c bounded the region from below: 0x03e0, 0x03e2 and 0x03e4 each
 * stalled ~201.7 ms. L2d excluded endpoint death and L2e excluded access
 * width, so what remains unmeasured is where the region ENDS: nothing above
 * 0x03e7 has ever been read. 0x03e6 is a functional register (the recovered
 * radio path writes it), which makes it the natural step across the edge; the
 * next word is read only if the first one completed, so a stall at 0x03e6 and
 * a stall at 0x03e8 give different upper bounds.
 */
/*
 * Endpoint-loss bisection L2g (acphy_identity_edge_probe): descending ladder
 * 0x03f0 -> 0x03ec -> 0x03e8 as the first accesses after the eight-read block.
 *
 * L2f showed 0x03e6 stalls ~201.7 ms, so the region reaches at least 0x3e7 and
 * nothing is known about 0x3e8..0x3ef. Ascending from 0x3e8 would cost a
 * timeout plus a reboot per word. Reading downward from a coarse jump instead
 * yields both edges in one insertion: the last address that completed and the
 * first that stalled bracket the boundary. The ladder stops at the first stall
 * and latches device loss, so no teardown follows a timeout.
 */
/*
 * Endpoint-loss bisection L2h (acphy_identity_wide_probe): jump to 0x0800
 * first, then descend 0x0600 -> 0x0500 -> 0x0480 -> 0x0440 -> 0x0420 ->
 * 0x0410 -> 0x0408, stopping at the first stall.
 *
 * L2g showed the region reaches at least 0x3f1, so descending one word at a
 * time costs a timeout plus a reboot per step. This ladder crosses the rest of
 * the D11 window in one insertion. If the first rung completes, the upper edge
 * is below 0x800 and the descent narrows it to an 8-byte gap; if 0x0800 stalls,
 * the region covers most of the window and the register-block framing is dead.
 */
/*
 * Endpoint-loss bisection L2i (acphy_identity_edge2_probe): two 16-bit reads,
 * 0x0400 then 0x03f8, fail-stop at the first stall.
 *
 * L2h bracketed the upper edge of the timeout band to 0x3f2..0x407: 0x3f0 times
 * out (L2g) and 0x0408 does not (L2h). 0x0400 halves that interval and 0x03f8
 * halves the lower half again, so one insertion lands the edge on an 8-byte
 * boundary. A stall latches device loss and stops the ladder.
 */
/*
 * Endpoint-loss bisection L2j (acphy_identity_lower_probe): probe 0x02b0 first,
 * then halve the bracket [0x0184 completes, 0x03e0 stalls] toward the first
 * stalling word, in one insertion.
 *
 * The upper edge is measured to 6 bytes by L2h/L2i; the lower edge is not
 * measured at all. Every probe that completes is cheap (2-5 us), so a halving
 * search converges in at most 12 reads. The search stops at the first stall and
 * reports the bracket it has: `lo` completed, `hi` stalled. When the bracket
 * collapses to two bytes the loop ends without needing a stall at all, because
 * 0x03e0 is already known to stall from L2 and its re-runs.
 */
static bool ob_si_d11_identity_lower_probe(struct ob_hw *hw)
{
	u32 lo = 0x0184u;	/* known to complete (32-bit reads, L2..L2e) */
	u32 hi = 0x03e0u;	/* known to stall (L2, L2d, L2e, L2h) */
	u32 off = 0x02b0u;	/* requested starting point */
	unsigned int step;

	for (step = 0; step < 12u; step++) {
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 val = ob_d11_read16(hw, (u16)off);
		u64 end_ns = ktime_get_mono_fast_ns();
		u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		bool stalled = dur_us >= OB_L2_3E2_STALL_US;

		if (stalled)
			hw->dev_lost = true;

		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%04x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 (u16)off, 16u, val,
			 (unsigned long long)start_ns, (unsigned long long)end_ns,
			 (unsigned long long)dur_us);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_LOWER: offset=0x%04x width=16 value=0x%04x dev_lost=%d duration_us=%llu stalled=%d bracket=[0x%04x,0x%04x]\n",
			 (u16)off, val, hw->dev_lost ? 1 : 0,
			 (unsigned long long)dur_us, stalled ? 1 : 0, lo, hi);

		if (stalled) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_LOWER: 0x%04x stalls; lower edge is between 0x%04x (completes) and 0x%04x (stalls) (fail-stop)\n",
				 (u16)off, lo, (u16)off);
			return false;
		}

		lo = off;
		if (hi - lo <= 2u) {
			dev_info(hw->dev,
				 "ACPHY_IDENTITY_LOWER: bracket collapsed without a stall; the first stalling word is 0x%04x\n",
				 (u16)hi);
			return true;
		}
		off = ((lo + hi) / 2u) & ~1u;
		if (off <= lo)
			off = lo + 2u;
	}

	dev_info(hw->dev,
		 "ACPHY_IDENTITY_LOWER: step budget exhausted; bracket=[0x%04x,0x%04x]\n",
		 lo, hi);
	return true;
}

static bool ob_si_d11_identity_edge2_probe(struct ob_hw *hw)
{
	static const u16 offs[2] = { 0x0400u, 0x03f8u };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(offs); i++) {
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 val = ob_d11_read16(hw, offs[i]);
		u64 end_ns = ktime_get_mono_fast_ns();
		u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		bool stalled = dur_us >= OB_L2_3E2_STALL_US;

		if (stalled)
			hw->dev_lost = true;

		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%04x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 offs[i], 16u, val,
			 (unsigned long long)start_ns, (unsigned long long)end_ns,
			 (unsigned long long)dur_us);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_EDGE2: offset=0x%04x width=16 value=0x%04x dev_lost=%d duration_us=%llu stalled=%d\n",
			 offs[i], val, hw->dev_lost ? 1 : 0,
			 (unsigned long long)dur_us, stalled ? 1 : 0);

		if (hw->dev_lost || stalled) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_EDGE2: 0x%04x did not complete; the boundary lies below this word and above the last completing one (fail-stop)\n",
				 offs[i]);
			return false;
		}
	}

	return true;
}

static bool ob_si_d11_identity_wide_probe(struct ob_hw *hw)
{
	static const u16 offs[8] = {
		0x0800u, 0x0600u, 0x0500u, 0x0480u,
		0x0440u, 0x0420u, 0x0410u, 0x0408u,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(offs); i++) {
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 val = ob_d11_read16(hw, offs[i]);
		u64 end_ns = ktime_get_mono_fast_ns();
		u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		bool stalled = dur_us >= OB_L2_3E2_STALL_US;

		if (stalled)
			hw->dev_lost = true;

		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%04x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 offs[i], 16u, val,
			 (unsigned long long)start_ns, (unsigned long long)end_ns,
			 (unsigned long long)dur_us);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_WIDE: offset=0x%04x width=16 value=0x%04x dev_lost=%d duration_us=%llu stalled=%d\n",
			 offs[i], val, hw->dev_lost ? 1 : 0,
			 (unsigned long long)dur_us, stalled ? 1 : 0);

		if (hw->dev_lost || stalled) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_WIDE: 0x%04x did not complete; the boundary lies below this word and above the last completing one (fail-stop)\n",
				 offs[i]);
			return false;
		}
	}

	return true;
}

static bool ob_si_d11_identity_edge_probe(struct ob_hw *hw)
{
	static const u16 offs[3] = { 0x03f0u, 0x03ecu, 0x03e8u };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(offs); i++) {
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 val = ob_d11_read16(hw, offs[i]);
		u64 end_ns = ktime_get_mono_fast_ns();
		u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		bool stalled = dur_us >= OB_L2_3E2_STALL_US;

		if (stalled)
			hw->dev_lost = true;

		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%04x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 offs[i], 16u, val,
			 (unsigned long long)start_ns, (unsigned long long)end_ns,
			 (unsigned long long)dur_us);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_EDGE: offset=0x%04x width=16 value=0x%04x dev_lost=%d duration_us=%llu stalled=%d\n",
			 offs[i], val, hw->dev_lost ? 1 : 0,
			 (unsigned long long)dur_us, stalled ? 1 : 0);

		if (hw->dev_lost || stalled) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_EDGE: 0x%04x did not complete; the boundary lies below this word and above the last completing one (fail-stop)\n",
				 offs[i]);
			return false;
		}
	}

	return true;
}

static bool ob_si_d11_identity_upper_probe(struct ob_hw *hw)
{
	static const u16 offs[2] = { 0x03e6u, 0x03e8u };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(offs); i++) {
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 val = ob_d11_read16(hw, offs[i]);
		u64 end_ns = ktime_get_mono_fast_ns();
		u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		bool stalled = dur_us >= OB_L2_3E2_STALL_US;

		if (stalled)
			hw->dev_lost = true;

		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%04x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 offs[i], 16u, val,
			 (unsigned long long)start_ns, (unsigned long long)end_ns,
			 (unsigned long long)dur_us);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_UPPER: offset=0x%04x width=16 value=0x%04x dev_lost=%d duration_us=%llu stalled=%d\n",
			 offs[i], val, hw->dev_lost ? 1 : 0,
			 (unsigned long long)dur_us, stalled ? 1 : 0);

		if (hw->dev_lost || stalled) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_UPPER: 0x%04x did not complete; the failing region extends at least through this word (fail-stop)\n",
				 offs[i]);
			return false;
		}
	}

	return true;
}

static bool ob_si_d11_identity_control16_reprobe(struct ob_hw *hw)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u16 val = ob_d11_read16(hw, 0x0120u);
	u64 end_ns = ktime_get_mono_fast_ns();
	u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	bool stalled = dur_us >= OB_L2_3E2_STALL_US;

	if (stalled)
		hw->dev_lost = true;

	dev_info(hw->dev,
		 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%04x start_ns=%llu end_ns=%llu duration_us=%llu\n",
		 0x0120u, 16u, val,
		 (unsigned long long)start_ns, (unsigned long long)end_ns,
		 (unsigned long long)dur_us);
	dev_info(hw->dev,
		 "ACPHY_IDENTITY_CONTROL16: offset=0x0120 width=16 value=0x%04x dev_lost=%d duration_us=%llu stalled=%d\n",
		 val, hw->dev_lost ? 1 : 0, (unsigned long long)dur_us,
		 stalled ? 1 : 0);

	return !hw->dev_lost && !stalled;
}

static bool ob_si_d11_identity_control_reprobe(struct ob_hw *hw)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u32 val = ob_d11_read32(hw, 0x0120u);
	u64 end_ns = ktime_get_mono_fast_ns();
	u64 dur_us = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	bool stalled = dur_us >= OB_L2_3E2_STALL_US;

	dev_info(hw->dev,
		 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%08x start_ns=%llu end_ns=%llu duration_us=%llu\n",
		 0x0120u, 32u, val,
		 (unsigned long long)start_ns, (unsigned long long)end_ns,
		 (unsigned long long)dur_us);
	dev_info(hw->dev,
		 "ACPHY_IDENTITY_CONTROL: offset=0x0120 value=0x%08x dev_lost=%d duration_us=%llu stalled=%d\n",
		 val, hw->dev_lost ? 1 : 0, (unsigned long long)dur_us,
		 stalled ? 1 : 0);

	return !hw->dev_lost && !stalled;
}

/*
 * Endpoint-loss bisection L2k (acphy_identity_wrapper_witness): scope witness.
 *
 * L2..L2j measured WHERE the ~201.7 ms timeout band is (bytes 0x3d8..0x3fb).
 * None of them measured its SCOPE: when the island times out, does the whole
 * PCIe function stop completing MMIO, or only that block? The 2026-10-26 D1P
 * run saw the wrapper register 0x0408 answer in 1 us before a D11 access and
 * return all-ones after it, but that was a different build, with no duration
 * field and no trace, and it was never replicated.
 *
 * The sequence (one insertion, fail-stop at the first anomalous stage):
 *
 *   S1  wrapper (core->wrap) 0x0408, 0x0804   baseline; the guarded accessor
 *                                             is safe here because dev_lost
 *                                             is still 0
 *   S2  D11 0x0120                            pre-island control, known-good
 *   S3  D11 0x03da (16-bit)                   island entry measured by L2j;
 *                                             a stall latches dev_lost
 *   S4  D11 0x0120 UNGUARDED                  the clean post-stall witness:
 *                                             no window switch, no config
 *                                             write
 *   S5  wrapper 0x0408, 0x0804, 0x0800        only if S4 was LIVE; forces
 *       UNGUARDED                             one window switch (bcma's two
 *                                             config dwords, not this
 *                                             driver's)
 *   S6  D11 0x0120 UNGUARDED                  only if S5 was LIVE; proves
 *                                             the switch back changed
 *                                             nothing
 *
 * Classes are logged per read: LIVE (<100 us and not all-ones), ALL_ONES_FAST
 * (fast but all-ones), GREY (100 us..10 ms - not an answer), STALL (>=10 ms,
 * the OB_L2_3E2_STALL_US threshold used by L2..L2j). NO_COMPLETION exists only
 * offline: it is the absence of the matching kprobe return, which the driver
 * cannot observe from a call that did not return.
 *
 * If a read exceeds 1 s the sequence stops issuing further reads (design
 * paragraph 9). One run: no retry, no rmmod after a stall.
 */
#define OB_L2K_LIVE_US		100ULL
#define OB_L2K_ABORT_US		1000000ULL

enum ob_l2k_class {
	OB_L2K_NOT_ISSUED = 0,
	OB_L2K_LIVE,
	OB_L2K_ALL_ONES_FAST,
	OB_L2K_GREY,
	OB_L2K_STALL,
};

enum ob_l2k_win {
	OB_L2K_WIN_D11 = 0,
	OB_L2K_WIN_WRAP,
};

static const char *ob_si_l2k_class_name(enum ob_l2k_class klass)
{
	switch (klass) {
	case OB_L2K_LIVE:
		return "LIVE";
	case OB_L2K_ALL_ONES_FAST:
		return "ALL_ONES_FAST";
	case OB_L2K_GREY:
		return "GREY";
	case OB_L2K_STALL:
		return "STALL";
	default:
		return "not_issued";
	}
}

static const char *ob_si_l2k_win_name(enum ob_l2k_win win)
{
	return win == OB_L2K_WIN_WRAP ? "wrap" : "d11";
}

static enum ob_l2k_class ob_si_l2k_classify(u64 dur_us, u32 val,
					    unsigned int width)
{
	u32 all_ones = (width == 16u) ? 0xffffu : 0xffffffffu;

	if (dur_us >= OB_L2_3E2_STALL_US)
		return OB_L2K_STALL;
	if (dur_us >= OB_L2K_LIVE_US)
		return OB_L2K_GREY;
	if (val == all_ones)
		return OB_L2K_ALL_ONES_FAST;
	return OB_L2K_LIVE;
}

/*
 * One 32-bit witness read. With guarded=false the device-loss guard is
 * bypassed on purpose: after the island the guard would return 0xffffffff
 * without issuing MMIO, and the run would measure the driver, not the device.
 * The unguarded reads live in the guard backend (ob_guard.c) as the two
 * ob_*_witness_read32() helpers, so raw bcma access stays in one file. The
 * kprobe-derived times are compared offline against start_ns/end_ns.
 */
static enum ob_l2k_class ob_si_l2k_read32(struct ob_hw *hw, const char *stage,
					  enum ob_l2k_win win, u16 off,
					  bool guarded, u32 *value,
					  u64 *dur_us)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u64 end_ns;
	u32 val;
	u64 dur;
	enum ob_l2k_class klass;

	if (win == OB_L2K_WIN_WRAP)
		val = guarded ? ob_axi_read32(hw, off)
			      : ob_axi_witness_read32(hw, off);
	else
		val = guarded ? ob_d11_read32(hw, off)
			      : ob_d11_witness_read32(hw, off);
	end_ns = ktime_get_mono_fast_ns();
	dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	klass = ob_si_l2k_classify(dur, val, 32u);

	dev_info(hw->dev,
		 "ACPHY_IDENTITY_WITNESS: stage=%s offset=0x%04x window=%s width=32 guarded=%u value=0x%08x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
		 stage, off, ob_si_l2k_win_name(win), guarded ? 1u : 0u, val,
		 hw->dev_lost ? 1u : 0u, (unsigned long long)start_ns,
		 (unsigned long long)end_ns, (unsigned long long)dur,
		 ob_si_l2k_class_name(klass));

	if (value)
		*value = val;
	if (dur_us)
		*dur_us = dur;
	return klass;
}

static void ob_si_d11_identity_wrapper_witness(struct ob_hw *hw)
{
	enum ob_l2k_class s1 = OB_L2K_NOT_ISSUED, s2 = OB_L2K_NOT_ISSUED;
	enum ob_l2k_class s3 = OB_L2K_NOT_ISSUED, s4 = OB_L2K_NOT_ISSUED;
	enum ob_l2k_class s5 = OB_L2K_NOT_ISSUED, s6 = OB_L2K_NOT_ISSUED;
	const char *verdict = "inconclusive";
	unsigned int window_changes = 0;
	bool island_reproduced = false;
	bool island_local = false;
	u64 dur_us = 0;
	u32 val = 0;

	/*
	 * S1: wrapper baseline. Guarded accessors are safe here: dev_lost is
	 * still 0, so they are a pass-through, and that keeps this stage equal
	 * to every other pre-island read in the driver.
	 */
	s1 = ob_si_l2k_read32(hw, "S1", OB_L2K_WIN_WRAP, BCMA_IOCTL, true,
			      &val, &dur_us);
	if (s1 == OB_L2K_LIVE)
		s1 = ob_si_l2k_read32(hw, "S1", OB_L2K_WIN_WRAP, BCMA_RESET_ST,
				      true, &val, &dur_us);

	/* S2: pre-island D11 control, same offset as the bring-up block. */
	s2 = ob_si_l2k_read32(hw, "S2", OB_L2K_WIN_D11, 0x0120u, true, &val,
			      &dur_us);

	/* One switch into wrap (S1) and one back to D11 (S2). */
	window_changes = 2;

	if (s1 != OB_L2K_LIVE || s2 != OB_L2K_LIVE) {
		hw->acphy_identity_witness_void = true;
		verdict = "void_no_baseline";
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_WITNESS: no baseline (S1=%s S2=%s); the island is NOT issued and this run is VOID\n",
			 ob_si_l2k_class_name(s1),
			 ob_si_l2k_class_name(s2));
		goto summary;
	}

	/* S3: island entry, the first stalling word measured by L2j. */
	{
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 v16 = ob_d11_read16(hw, 0x03dau);
		u64 end_ns = ktime_get_mono_fast_ns();
		u64 dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);

		s3 = ob_si_l2k_classify(dur, (u32)v16, 16u);
		if (s3 == OB_L2K_STALL)
			hw->dev_lost = true;	/* same latch as L2j */
		island_reproduced = (s3 == OB_L2K_STALL);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_WITNESS: stage=S3 offset=0x03da window=d11 width=16 guarded=1 value=0x%08x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
			 (u32)v16, hw->dev_lost ? 1u : 0u,
			 (unsigned long long)start_ns,
			 (unsigned long long)end_ns,
			 (unsigned long long)dur,
			 ob_si_l2k_class_name(s3));
		if (dur >= OB_L2K_ABORT_US) {
			verdict = "aborted_slow_read";
			hw->acphy_identity_witness_failed = true;
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_WITNESS: S3 exceeded 1 s; no further reads issued\n");
			goto summary;
		}
	}

	/*
	 * S4: the clean post-stall witness. Same window as S3, no window switch
	 * and no config write: if this read is LIVE while S5 later fails, the
	 * difference is the switch itself, not the D11 window.
	 */
	s4 = ob_si_l2k_read32(hw, "S4", OB_L2K_WIN_D11, 0x0120u, false, &val,
			      &dur_us);
	if (s4 != OB_L2K_LIVE) {
		hw->acphy_identity_witness_failed = true;
		verdict = (s4 == OB_L2K_STALL) ? "function_wide" :
			  "inconclusive_s4_not_live";
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_WITNESS: S4 was %s; the wrapper witness is NOT issued (fail-stop)\n",
			 ob_si_l2k_class_name(s4));
		goto summary;
	}

	if (!island_reproduced) {
		/*
		 * The island did not reproduce, so there is no post-stall branch
		 * for S5/S6 to refine; they are deliberately not issued.
		 */
		verdict = "not_applicable_no_reproduction";
		goto summary;
	}

	/* S5: wrapper witness; this is where bcma's two config dwords land. */
	s5 = ob_si_l2k_read32(hw, "S5", OB_L2K_WIN_WRAP, BCMA_IOCTL, false,
			      &val, &dur_us);
	window_changes++;
	if (s5 == OB_L2K_LIVE)
		s5 = ob_si_l2k_read32(hw, "S5", OB_L2K_WIN_WRAP, BCMA_RESET_ST,
				      false, &val, &dur_us);
	if (s5 == OB_L2K_LIVE)
		s5 = ob_si_l2k_read32(hw, "S5", OB_L2K_WIN_WRAP, BCMA_RESET_CTL,
				      false, &val, &dur_us);
	if (s5 != OB_L2K_LIVE) {
		hw->acphy_identity_witness_failed = true;
		verdict = "window_specific";
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_WITNESS: wrapper witness was %s; S6 is NOT issued (fail-stop)\n",
			 ob_si_l2k_class_name(s5));
		goto summary;
	}

	/* S6: control after switching back to the D11 window. */
	window_changes++;
	s6 = ob_si_l2k_read32(hw, "S6", OB_L2K_WIN_D11, 0x0120u, false, &val,
			      &dur_us);
	if (s6 == OB_L2K_LIVE) {
		verdict = "island_local";
		island_local = true;
	} else {
		verdict = "inconclusive_switch_confounded";
	}

summary:
	dev_info(hw->dev,
		 "ACPHY_IDENTITY_WITNESS_SUMMARY: island_reproduced=%u s1=%s s2=%s s3=%s s4=%s s5=%s s6=%s window_changes=%u writes_issued=0 verdict=%s\n",
		 island_reproduced ? 1u : 0u,
		 ob_si_l2k_class_name(s1), ob_si_l2k_class_name(s2),
		 ob_si_l2k_class_name(s3), ob_si_l2k_class_name(s4),
		 ob_si_l2k_class_name(s5), ob_si_l2k_class_name(s6),
		 window_changes, verdict);
	if (!island_local)
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_WITNESS: verdict=%s; apply the reading rules in DESIGN_L2K_WRAPPER_WITNESS.md before interpreting\n",
			 verdict);
}

/*
 * Endpoint-loss L2l (acphy_identity_pll_witness): PHY-PLL pre-state witness.
 *
 * L2..L2k established WHERE the ~201.7 ms band is (bytes 0x3d8..0x3fb) and that
 * the failure is function-wide once it fires, but no run observed the clock
 * state BEFORE the first stalling access. The rev-42 register map puts the
 * radio port and the PHY status file in that band and puts the PHY-PLL
 * request/available bits in clk_ctl_st 0x01e0 — below the band, so the same
 * window can read them:
 *
 *   P1  D11 0x01e0 32-bit  clk_ctl_st      guarded
 *   P2  D11 0x01e8 32-bit  d11_phypllctl   guarded
 *   P3  D11 0x03d8 16-bit  radioregaddr    guarded; first word of the band
 *   P4  D11 0x03da 16-bit  radioregdata    only if P3 completed; L2j's word
 *
 * P1/P2 must be LIVE, otherwise the run is VOID and the island is NOT entered
 * (no pre-state, no conclusion). A stall at P3/P4 latches dev_lost exactly like
 * L2j/L2k and stops the sequence, so this mode costs at most the legacy single
 * 0x3e0 read it replaces. No write is issued anywhere in the sequence.
 *
 * Bit values are the upstream Broadcom definitions (brcmsmac d11.h):
 * CCS_ERSRC_REQ_HT 0x00000010, CCS_ERSRC_REQ_D11PLL 0x00000100,
 * CCS_ERSRC_REQ_PHYPLL 0x00000200, CCS_ERSRC_AVAIL_HT 0x00020000,
 * CCS_ERSRC_AVAIL_D11PLL 0x01000000, CCS_ERSRC_AVAIL_PHYPLL 0x02000000.
 * On corerev >= 40 the PHY-PLL request/avail bits live in clk_ctl_st; 0x1e8 is
 * read as the legacy counterpart and reported verbatim.
 */
#define OB_L2L_ABORT_US		1000000ULL
#define OB_L2L_CCS_REQ_HT	0x00000010u
#define OB_L2L_CCS_REQ_D11PLL	0x00000100u
#define OB_L2L_CCS_REQ_PHYPLL	0x00000200u
#define OB_L2L_CCS_AVAIL_HT	0x00020000u
#define OB_L2L_CCS_AVAIL_D11PLL	0x01000000u
#define OB_L2L_CCS_AVAIL_PHYPLL	0x02000000u

static const char *ob_si_l2l_phypll_name(u32 ccs)
{
	if (ccs & OB_L2L_CCS_AVAIL_PHYPLL)
		return "avail";
	if (ccs & OB_L2L_CCS_REQ_PHYPLL)
		return "requested_no_avail";
	return "off";
}

static const char *ob_si_l2l_verdict(u32 ccs)
{
	if (ccs & OB_L2L_CCS_AVAIL_PHYPLL)
		return "phypll_avail_at_island";
	if (ccs & OB_L2L_CCS_REQ_PHYPLL)
		return "phypll_requested_not_available_at_island";
	return "phypll_off_at_island";
}

static enum ob_l2k_class ob_si_l2l_read16(struct ob_hw *hw, const char *stage,
					  u16 off, u16 *value, u64 *dur_us)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u16 val = ob_d11_read16(hw, off);
	u64 end_ns = ktime_get_mono_fast_ns();
	u64 dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	enum ob_l2k_class klass = ob_si_l2k_classify(dur, (u32)val, 16u);

	if (klass == OB_L2K_STALL)
		hw->dev_lost = true;	/* same latch as L2j/L2k */

	dev_info(hw->dev,
		 "ACPHY_IDENTITY_PLLWITNESS: stage=%s offset=0x%04x window=d11 width=16 guarded=1 value=0x%04x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
		 stage, off, val, hw->dev_lost ? 1u : 0u,
		 (unsigned long long)start_ns, (unsigned long long)end_ns,
		 (unsigned long long)dur, ob_si_l2k_class_name(klass));

	if (value)
		*value = val;
	if (dur_us)
		*dur_us = dur;
	return klass;
}

static void ob_si_d11_identity_pll_witness(struct ob_hw *hw)
{
	enum ob_l2k_class p1 = OB_L2K_NOT_ISSUED, p2 = OB_L2K_NOT_ISSUED;
	enum ob_l2k_class p3 = OB_L2K_NOT_ISSUED, p4 = OB_L2K_NOT_ISSUED;
	const char *verdict = "inconclusive";
	bool island_reproduced = false;
	u64 dur = 0;
	u32 ccs = 0, phypllctl = 0;
	u16 v16 = 0;

	/* P1/P2: the pre-state, both BELOW the measured band. */
	p1 = ob_si_l2k_read32(hw, "P1", OB_L2K_WIN_D11, 0x01e0u, true, &ccs,
			      &dur);
	p2 = ob_si_l2k_read32(hw, "P2", OB_L2K_WIN_D11, 0x01e8u, true,
			      &phypllctl, &dur);

	if (p1 != OB_L2K_LIVE || p2 != OB_L2K_LIVE) {
		hw->acphy_identity_witness_void = true;
		verdict = "void_no_baseline";
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_PLLWITNESS: no baseline (P1=%s P2=%s); the island is NOT issued and this run is VOID\n",
			 ob_si_l2k_class_name(p1),
			 ob_si_l2k_class_name(p2));
		goto summary;
	}

	dev_info(hw->dev,
		 "ACPHY_IDENTITY_PLLWITNESS: pre_state clk_ctl_st=0x%08x d11_phypllctl=0x%08x req_ht=%u avail_ht=%u req_d11pll=%u avail_d11pll=%u req_phypll=%u avail_phypll=%u phypll=%s\n",
		 ccs, phypllctl,
		 (ccs & OB_L2L_CCS_REQ_HT) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_AVAIL_HT) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_REQ_D11PLL) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_AVAIL_D11PLL) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_REQ_PHYPLL) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_AVAIL_PHYPLL) ? 1u : 0u,
		 ob_si_l2l_phypll_name(ccs));

	/* P3: first word of the band; P4 only if P3 still completes. */
	p3 = ob_si_l2l_read16(hw, "P3", 0x03d8u, &v16, &dur);
	if (dur >= OB_L2L_ABORT_US) {
		hw->acphy_identity_witness_failed = true;
		verdict = "aborted_slow_read";
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_PLLWITNESS: P3 exceeded 1 s; no further reads issued\n");
		goto summary;
	}
	if (p3 == OB_L2K_STALL)
		island_reproduced = true;
	else {
		p4 = ob_si_l2l_read16(hw, "P4", 0x03dau, &v16, &dur);
		if (dur >= OB_L2L_ABORT_US) {
			hw->acphy_identity_witness_failed = true;
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_PLLWITNESS: P4 exceeded 1 s; no further reads issued\n");
			goto summary;
		}
		if (p4 == OB_L2K_STALL)
			island_reproduced = true;
	}

	verdict = island_reproduced ? ob_si_l2l_verdict(ccs) :
		  "no_island_reproduction";

summary:
	dev_info(hw->dev,
		 "ACPHY_IDENTITY_PLLWITNESS_SUMMARY: clk_ctl_st=0x%08x d11_phypllctl=0x%08x phypll=%s req_phypll=%u avail_phypll=%u req_d11pll=%u avail_d11pll=%u p1=%s p2=%s p3_03d8=%s p4_03da=%s island_reproduced=%u writes_issued=0 verdict=%s\n",
		 ccs, phypllctl, ob_si_l2l_phypll_name(ccs),
		 (ccs & OB_L2L_CCS_REQ_PHYPLL) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_AVAIL_PHYPLL) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_REQ_D11PLL) ? 1u : 0u,
		 (ccs & OB_L2L_CCS_AVAIL_D11PLL) ? 1u : 0u,
		 ob_si_l2k_class_name(p1), ob_si_l2k_class_name(p2),
		 ob_si_l2k_class_name(p3), ob_si_l2k_class_name(p4),
		 island_reproduced ? 1u : 0u, verdict);
}

/*
 * T4 (acphy_identity_3e0_first): the vendor register read with no bring-up
 * block.
 *
 * Sequence (one insertion, fail-stop; the run stops here by design):
 *
 *   F1  D11 0x01e0 32-bit  clk_ctl_st    liveness bracket, guarded
 *   F2  D11 0x03e0 16-bit  PHY version   only if F1 was LIVE
 *
 * F2 is the first and only access to the 0x3xx window in the whole run: the
 * eight-read ob_si_d11_diag() block is not issued at all (ob_si_probe). That is
 * exactly the variable L2 (0x3e0 after eight 0x1xx reads), D1'/HIL (after
 * radio prepare + the cflags write) and exec03 (a control before it) still
 * carried: is any prior D11 traffic needed for the stall?
 *
 * F1 must be LIVE, otherwise the run is VOID and 0x3e0 is NOT issued - a
 * bracket that cannot be trusted must not be followed by the fatal read.
 * A stall at F2 latches dev_lost exactly like L2j/L2k and ends the sequence.
 * No write is issued anywhere in the sequence, and the F2 timing is logged in
 * the L2 format as well, so the two traces compare character for character.
 */
static void ob_si_d11_identity_3e0_first(struct ob_hw *hw)
{
	enum ob_l2k_class f1 = OB_L2K_NOT_ISSUED, f2 = OB_L2K_NOT_ISSUED;
	const char *verdict = "inconclusive";
	u64 dur = 0;
	u32 bracket = 0;
	u16 phyver = 0;

	/* F1: the liveness bracket, BELOW the measured band and known-good. */
	{
		u64 start_ns = ktime_get_mono_fast_ns();
		u32 val = ob_d11_read32(hw, 0x01e0u);
		u64 end_ns = ktime_get_mono_fast_ns();

		bracket = val;
		dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		f1 = ob_si_l2k_classify(dur, val, 32u);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_3E0FIRST: stage=F1 offset=0x%04x window=d11 width=32 guarded=1 value=0x%08x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
			 0x01e0u, val, hw->dev_lost ? 1u : 0u,
			 (unsigned long long)start_ns,
			 (unsigned long long)end_ns,
			 (unsigned long long)dur, ob_si_l2k_class_name(f1));
	}

	if (f1 != OB_L2K_LIVE) {
		hw->acphy_identity_witness_void = true;
		verdict = "void_no_liveness_bracket";
		dev_warn(hw->dev,
			 "ACPHY_IDENTITY_3E0FIRST: bracket not LIVE (F1=%s); 0x3e0 NOT issued and this run is VOID\n",
			 ob_si_l2k_class_name(f1));
		goto summary;
	}

	/*
	 * F2: the vendor read itself, 16-bit, the same access L2 issued after the
	 * block and D1' issued after the attach prefix. Its timing line is
	 * deliberately in the L2 format.
	 */
	{
		u64 start_ns = ktime_get_mono_fast_ns();
		u16 val = ob_d11_read16(hw, 0x03e0u);
		u64 end_ns = ktime_get_mono_fast_ns();

		phyver = val;
		dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);
		f2 = ob_si_l2k_classify(dur, (u32)val, 16u);
		if (f2 == OB_L2K_STALL)
			hw->dev_lost = true;	/* same latch as L2j/L2k */

		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%08x start_ns=%llu end_ns=%llu duration_us=%llu\n",
			 0x03e0u, 16u, (u32)val,
			 (unsigned long long)start_ns,
			 (unsigned long long)end_ns,
			 (unsigned long long)dur);
		dev_info(hw->dev,
			 "ACPHY_IDENTITY_3E0FIRST: stage=F2 offset=0x%04x window=d11 width=16 guarded=1 value=0x%04x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
			 0x03e0u, val, hw->dev_lost ? 1u : 0u,
			 (unsigned long long)start_ns,
			 (unsigned long long)end_ns,
			 (unsigned long long)dur, ob_si_l2k_class_name(f2));
	}

	switch (f2) {
	case OB_L2K_STALL:
		verdict = "vendor_read_stall_first_access";
		break;
	case OB_L2K_LIVE:
		verdict = "vendor_read_completed_first_access";
		break;
	case OB_L2K_ALL_ONES_FAST:
		verdict = "vendor_read_all_ones_fast";
		break;
	case OB_L2K_GREY:
		verdict = "vendor_read_grey";
		break;
	default:
		break;
	}

summary:
	dev_info(hw->dev,
		 "ACPHY_IDENTITY_3E0FIRST_SUMMARY: bracket_01e0=0x%08x f1=%s f2=%s value_03e0=0x%04x duration_us=%llu dev_lost=%u reads_0x3xx=1 writes_issued=0 verdict=%s\n",
		 bracket, ob_si_l2k_class_name(f1), ob_si_l2k_class_name(f2),
		 phyver, (unsigned long long)dur, hw->dev_lost ? 1u : 0u,
		 verdict);
}

/*
 * P1-B — the vendor's pre-read SHM RAM proof, mirrored.
 *
 * The vendor trace (`p0_attach_trace01/exec-20261028T001326Z`, vendor stream
 * `vendor_driver_telemetry_exec01`) shows 18 objmem accesses 0.4 ms BEFORE its
 * first 0x3e0 read and 17 670 after it: OBJADDR = 0x00010000 (SHM select,
 * offset 0), then the patterns 0xaa5555aa and 0x55aaaa55 through 0x164/0x166
 * with readback, then 0x007f0340, all readbacks matching. Our attach path never
 * touches 0x160/0x164/0x166 at all, so this is the second measured pre-read
 * divergence (the first is the missing wrapper-IOCTL bit 0x4).
 *
 * The probe issues only those three writes and their readbacks. Any mismatch,
 * any all-ones and any device-loss latch stops the caller before the 0x3e0
 * read: the run is a candidate test, not a place to spend the endpoint on an
 * unproven state.
 *
 * Register widths are those of the vendor trace: a 32-bit OBJADDR and 16-bit
 * OBJDATA halves. Values are fixed constants — nothing is derived from the
 * device, so a wrong readback is unambiguous.
 */
int ob_si_p1_shm_roundtrip(struct ob_hw *hw)
{
	static const u32 pattern[3] = { 0xaa5555aau, 0x55aaaa55u, 0x007f0340u };
	int i;

	if (!hw || hw->dev_lost)
		return -EIO;
	for (i = 0; i < 3; i++) {
		const u32 addr = OB_UCODE_OBJADDR_SHM_SEL; /* offset 0 */
		const u16 lo = (u16)(pattern[i] & 0xffffu);
		const u16 hi = (u16)(pattern[i] >> 16);
		u16 got_lo, got_hi;

		ob_d11_write32(hw, OB_UCODE_REG_OBJADDR, addr);
		if (hw->dev_lost)
			goto lost;
		ob_d11_write16(hw, OB_UCODE_REG_OBJDATA, lo);
		if (hw->dev_lost)
			goto lost;
		ob_d11_write16(hw, OB_UCODE_REG_OBJDATA + 0x2u, hi);
		if (hw->dev_lost)
			goto lost;
		got_lo = ob_d11_read16(hw, OB_UCODE_REG_OBJDATA);
		got_hi = ob_d11_read16(hw, OB_UCODE_REG_OBJDATA + 0x2u);
		if (hw->dev_lost)
			goto lost;
		dev_info(hw->dev,
			 "P1_WITNESS param=shm stage=%d addr=0x%08x wrote=0x%08x read=0x%08x verdict=%s\n",
			 i + 1, addr, pattern[i],
			 ((u32)got_hi << 16) | (u32)got_lo,
			 (got_lo == lo && got_hi == hi) ? "ok" : "mismatch");
		if (got_lo != lo || got_hi != hi) {
			dev_err(hw->dev,
				"P1_FAIL_STOP reason=shm_roundtrip stage=%d wrote=0x%08x read=0x%08x; the 0x3e0 read is not issued\n",
				 i + 1, pattern[i],
				 ((u32)got_hi << 16) | (u32)got_lo);
			return -EIO;
		}
	}
	return 0;

lost:
	dev_err(hw->dev,
		"P1_FAIL_STOP reason=shm_roundtrip stage=%d dev_lost=1; the 0x3e0 read is not issued\n",
		i + 1);
	return -EIO;
}

int ob_si_probe(struct ob_hw *hw)
{
	int sprom_ret;

	sprom_ret = ob_si_validate_context(hw);
	if (sprom_ret)
		return sprom_ret;

	/* ChipCommon core provides the CC/PMU/OTP/SPROM window. */
	hw->cc = hw->bus->drv_cc.core;
	if (!hw->cc) {
		dev_err(hw->dev, "no ChipCommon core\n");
		return -ENODEV;
	}

	sprom_ret = ob_si_powerup(hw);
	if (sprom_ret)
		return sprom_ret;
	hw->d11_state = OB_D11_CORE_READY;

	/*
	 * M3.4C: read-only D11 host-interface/MAC state (no writes).
	 *
	 * T4 (acphy_identity_3e0_first) removes this block on purpose: it is the
	 * last uncontrolled "prior D11 traffic" variable left in the minimal
	 * state (L2 read these eight words before 0x3e0, D1'/HIL ran radio
	 * prepare and the cflags write first, exec03 read a control first). The
	 * block reads only 0x1xx words and writes nothing, so skipping it changes
	 * no register state - the T4 run simply reaches the vendor read with no
	 * D11 access before it. The flag cannot be set outside the identity mode
	 * (refused in ob_core.c), so no other mode's sequence changes.
	 */
	if (!hw->acphy_identity_3e0_first)
		ob_si_d11_diag(hw);
	/*
	 * L2 reads 0x03e0 immediately after the eight bring-up reads, before any
	 * attach prefix exists. The call is adjacent on purpose: any access placed
	 * between the two stages would make the L1 comparison multi-variable.
	 *
	 * L2b (opt-in, default off) inserts exactly one 16-bit read of 0x03e2 —
	 * the other half of the same dword — between them, and only then, so the
	 * unmodified L2 contract and its host test remain literally true.
	 */
	if (hw->acphy_identity_timing_only) {
		/*
		 * T4 (acphy_identity_3e0_first) is the minimal variant: the bring-up
		 * block was skipped above, F1/F2 are the run's only D11 accesses, and
		 * the run ends at F2 by design (no legacy read, no later stage).
		 * Every modifier below keeps its own eight-read-block contract.
		 */
		if (hw->acphy_identity_3e0_first) {
			ob_si_d11_identity_3e0_first(hw);
			hw->acphy_identity_early_stop = true;
			if (hw->dev_lost)
				dev_warn(hw->dev,
					 "ACPHY_IDENTITY_3E0FIRST: device loss latched at F2; the run ends at the vendor read (fail-stop)\n");
			return 0;
		}
		/* Any probe here must be the first access after the block. */
		/*
		 * L2l (PHY-PLL pre-state witness) is exclusive with the other
		 * modifiers and replaces the legacy 0x3e0 read: it reads 0x1e0/0x1e8
		 * first, then its own P3/P4 are the island entry, and a second
		 * ~201.7 ms access would add nothing.
		 */
		if (hw->acphy_identity_pll_witness) {
			ob_si_d11_identity_pll_witness(hw);
			if (hw->acphy_identity_witness_failed ||
			    hw->acphy_identity_witness_void || hw->dev_lost) {
				dev_warn(hw->dev,
					 "ACPHY_IDENTITY_PLLWITNESS: %s; no later address issued (fail-stop)\n",
					 hw->acphy_identity_witness_void ?
					 "no baseline, run is VOID" :
					 "a stage was not LIVE");
				hw->acphy_identity_early_stop = true;
				return 0;
			}
		}
		/*
		 * L2k (scope witness) is exclusive with the other modifiers and
		 * replaces the legacy 0x3e0 read: its own S3 is the island entry,
		 * and a second ~201.7 ms access would add nothing.
		 */
		if (hw->acphy_identity_wrapper_witness) {
			ob_si_d11_identity_wrapper_witness(hw);
			if (hw->acphy_identity_witness_failed ||
			    hw->acphy_identity_witness_void) {
				dev_warn(hw->dev,
					 "ACPHY_IDENTITY_WITNESS: %s; no later address issued (fail-stop)\n",
					 hw->acphy_identity_witness_void ?
					 "no baseline, run is VOID" :
					 "a stage was not LIVE");
				hw->acphy_identity_early_stop = true;
				return 0;
			}
		}
		if (hw->acphy_identity_lower_probe &&
		    !ob_si_d11_identity_lower_probe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_LOWER: lower-edge bisection stopped at a stall; no later address issued (fail-stop)\n");
			hw->acphy_identity_lower_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_edge2_probe &&
		    !ob_si_d11_identity_edge2_probe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_EDGE2: fine edge probe failed first; no later address issued (fail-stop)\n");
			hw->acphy_identity_edge2_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_wide_probe &&
		    !ob_si_d11_identity_wide_probe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_WIDE: wide bracket failed; no later address issued (fail-stop)\n");
			hw->acphy_identity_wide_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_edge_probe &&
		    !ob_si_d11_identity_edge_probe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_EDGE: edge probe failed first; no later address issued (fail-stop)\n");
			hw->acphy_identity_edge_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_upper_probe &&
		    !ob_si_d11_identity_upper_probe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_UPPER: upper-bound probe failed first; 0x3e2 and 0x3e0 NOT issued (fail-stop)\n");
			hw->acphy_identity_upper_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_control16_reprobe &&
		    !ob_si_d11_identity_control16_reprobe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_CONTROL16: 16-bit control re-read failed; nothing else issued (fail-stop)\n");
			hw->acphy_identity_control16_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_control_reprobe &&
		    !ob_si_d11_identity_control_reprobe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_CONTROL: control re-read failed; nothing else issued (fail-stop)\n");
			hw->acphy_identity_control_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_span_probe &&
		    !ob_si_d11_identity_span_probe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_SPAN: span probe failed first; 0x3e2 and 0x3e0 NOT issued (fail-stop)\n");
			hw->acphy_identity_span_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		if (hw->acphy_identity_3e2_first &&
		    !ob_si_d11_identity_3e2_probe(hw)) {
			dev_warn(hw->dev,
				 "ACPHY_IDENTITY_3E2: 0x3e2 failed first; 0x3e0 NOT issued (fail-stop)\n");
			hw->acphy_identity_3e2_failed = true;
			hw->acphy_identity_early_stop = true;
			return 0;
		}
		/*
		 * L2k issued its own island entry and its own post-stall witnesses;
		 * the legacy single 0x3e0 read is deliberately not issued in that
		 * run (a second ~201.7 ms access would add nothing).
		 */
		if (!hw->acphy_identity_wrapper_witness &&
		    !hw->acphy_identity_pll_witness)
			ob_si_d11_identity_timing(hw);
	}

	/* Read-only: what bcma already parsed from the SPROM at bus scan time. */
	ob_si_dump_bus_sprom(hw);

	/* Normal probe consumes BCMA-owned state; it never rereads CC+0x800. */
	sprom_ret = ob_si_consume_cached_sprom(hw);
	if (sprom_ret)
		return sprom_ret;

	if (hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only) {
		sprom_ret = ob_si_otp8e2_probe(hw);
		if (sprom_ret)
			return sprom_ret;
	} else if (otp_diag) {
		ob_si_otp_diag(hw);
	}

	if (hw->radio2069_rev4_probe_only ||
	    hw->d11_transaction_timing_only) {
		sprom_ret = ob_si_d11_enable_bracket(hw,
			"D11_ENABLE_BRACKET_SI_PROBE_EXIT");
		if (sprom_ret)
			return sprom_ret;
	}

	return 0;
}

/*
 * Isolated READ-ONLY SPROM evidence capture (D3B MHF3, sprom_evidence_only=1).
 *
 * Sets the ChipCommon window and runs the existing external-SPROM diagnostic
 * (ob_si_sprom_diag), which emits the already-read, CRC-validated 234-word
 * rev11 image via ob_si_emit_sprom11(). It performs NO hardware writes: no
 * power-up, no D11/core/clock write, no firmware upload, no PSM start, no DMA,
 * no IRQ, no PHY/radio/channel, no MAC enable.
 */
int ob_si_sprom_evidence(struct ob_hw *hw)
{
	int ret = ob_si_validate_context(hw);

	if (ret)
		return ret;
	hw->cc = hw->bus->drv_cc.core;
	if (!hw->cc) {
		dev_err(hw->dev, "sprom-evidence: no ChipCommon core\n");
		return -ENODEV;
	}

	if (!sprom_diag) {
		dev_info(hw->dev,
			 "sprom-evidence: diagnostic disabled by sprom_diag=0\n");
		return -EOPNOTSUPP;
	}
	dev_info(hw->dev,
		 "sprom-evidence: read-only external-SPROM capture (no bring-up)\n");
	return ob_si_sprom_diag(hw);
}

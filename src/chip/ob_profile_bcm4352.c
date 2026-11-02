/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * BCM4352 rev03 / D11 rev42 — the one admitted device.
 *
 * Data only: no MMIO, no allocation, no logging.  Provenance:
 * docs/data/verification/stand-readonly-checks.md and
 * audit/openbrcm_integration/BCM4352_PROFILE.json.
 *
 * Firmware names/sizes/FNV mirror src/ob_fw.h (the validator); they are not a
 * second validation path.
 */
#include "../ob_profile.h"

static const struct ob_chip_profile ob_bcm4352_rev03 = {
	.name = "BCM4352-rev03-D11-rev42",
	.enabled = true,

	.pci_vendor = OB_PROFILE_PCI_VENDOR_BROADCOM,
	.pci_device = 0x43b1,
	.pci_revision = 0x03,

	.subsystem_known = true,
	.subsystem_vendor = 0x1043,
	.subsystem_device = 0x85ba,

	.board_known = true,
	.board_revision = 11,

	.chip_id = 0x4352,
	.chip_revision = 3,

	.d11_manufacturer = OB_PROFILE_BCMA_MANUF_BCM,
	.d11_core_id = OB_PROFILE_BCMA_CORE_80211,
	.d11_revision = 42,
	.d11_class = 0,

	.phy = {
		.known = true,
		.type = 0x0b,
		.revision = 1,
		.revision_class = 0,
	},

	.radio = {
		.known = true,
		.id = 0x2069,
		.revision = 4,
		.revision_class = 0,
	},

	.firmware = {
		.known = true,
		.ucode_name = "brcm/bcm4352-d11ucode42.bin",
		.ucode_size = 43400u,
		.ucode_fnv = 0x7d364f6207b3298bULL,
		.initvals_name = "brcm/bcm4352-d11ac1initvals42.bin",
		.initvals_size = 4888u,
		.initvals_fnv = 0xa7cfdfdbcc9d58f1ULL,
		.bsinitvals_name = "brcm/bcm4352-d11ac1bsinitvals42.bin",
		.bsinitvals_size = 592u,
		.bsinitvals_fnv = 0xfa86a2510d2e0e3eULL,
	},

	.revision_policy = {
		.known = true,
		/* ob_d3a1_chip_uses_macfreq() includes 0x4352. */
		.uses_macfreq = true,
		/* The current 4352 path applies no PCIe2 workaround. */
		.requires_pcie2_war = false,
	},

	.required_evidence = (const struct ob_profile_evidence *)0,
	.required_evidence_count = 0,
};

const struct ob_chip_profile *ob_profile_bcm4352_rev03(void)
{
	return &ob_bcm4352_rev03;
}

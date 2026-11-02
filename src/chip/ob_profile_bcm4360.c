/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * BCM4360 rev03 — EVIDENCE ONLY, binding disabled.
 *
 * Only the PCI ids/revision and this board SKU's subsystem identity were
 * observed on the stand.  Chip, D11, PHY, radio, SPROM and firmware identity
 * were never read on this device and must stay UNKNOWN.  No value is inferred
 * from the model number: in particular chip_id is NOT set to 0x4360 and the
 * D11 revision is NOT set to 0x2a.
 *
 * The resolver never returns this descriptor because `enabled == false`; its
 * only consumer is the evidence/audit path and the admission tests.
 *
 * Sources: audit/openbrcm_integration/BCM4360_REQUIREMENTS.json,
 * audit/openbrcm_phase27/DEVICE_PROFILE_ARCHITECTURE.md §5, and
 * docs/data/verification/stand-readonly-checks.md.
 */
#include "../ob_profile.h"

static const struct ob_profile_evidence ob_bcm4360_evidence[] = {
	{
		.id = "E1",
		.status = "PARTIAL",
		.detail = "PCI 14e4:43a0 rev 0x03 and subsystem 1043:85df observed on this SKU; other board SKUs UNKNOWN",
	},
	{
		.id = "E2",
		.status = "UNKNOWN",
		.detail = "chip id and chip revision not read on the 4360",
	},
	{
		.id = "E3",
		.status = "UNKNOWN",
		.detail = "D11 manufacturer/core/revision/class unknown; D11 rev 0x2a not established",
	},
	{
		.id = "E4",
		.status = "UNKNOWN",
		.detail = "PHY type/revision unknown; type 0x0b is a hypothesis, not an observation",
	},
	{
		.id = "E5",
		.status = "UNKNOWN",
		.detail = "radio id/revision/class unknown; 0x2069 rev4 not established",
	},
	{
		.id = "E6",
		.status = "UNKNOWN",
		.detail = "SPROM revision and field map unknown",
	},
	{
		.id = "E7",
		.status = "BLOCKED_BY_E3_E4",
		.detail = "firmware selector cannot run without chip/D11/PHY identity",
	},
	{
		.id = "E8",
		.status = "UNKNOWN",
		.detail = "per-chip D11 behaviour and defmacintmask arm unknown (vendor has >20 0x4360 arms)",
	},
};

static const struct ob_chip_profile ob_bcm4360_rev03 = {
	.name = "BCM4360-rev03-evidence-only",
	.enabled = false,

	.pci_vendor = OB_PROFILE_PCI_VENDOR_BROADCOM,
	.pci_device = 0x43a0,
	.pci_revision = 0x03,

	/* Observed for this board SKU only; E1 tracks the SKU caveat. */
	.subsystem_known = true,
	.subsystem_vendor = 0x1043,
	.subsystem_device = 0x85df,

	.board_known = false,
	.board_revision = OB_PROFILE_UNKNOWN_U8,

	.chip_id = OB_PROFILE_UNKNOWN_U16,
	.chip_revision = OB_PROFILE_UNKNOWN_U8,

	.d11_manufacturer = OB_PROFILE_UNKNOWN_U16,
	.d11_core_id = OB_PROFILE_UNKNOWN_U16,
	.d11_revision = OB_PROFILE_UNKNOWN_U8,
	.d11_class = OB_PROFILE_UNKNOWN_U8,

	.phy = {
		.known = false,
		.type = OB_PROFILE_UNKNOWN_U16,
		.revision = OB_PROFILE_UNKNOWN_U8,
		.revision_class = OB_PROFILE_UNKNOWN_U8,
	},

	.radio = {
		.known = false,
		.id = OB_PROFILE_UNKNOWN_U16,
		.revision = OB_PROFILE_UNKNOWN_U8,
		.revision_class = OB_PROFILE_UNKNOWN_U8,
	},

	.firmware = {
		.known = false,
		.ucode_name = (const char *)0,
		.ucode_size = 0,
		.ucode_fnv = 0,
		.initvals_name = (const char *)0,
		.initvals_size = 0,
		.initvals_fnv = 0,
		.bsinitvals_name = (const char *)0,
		.bsinitvals_size = 0,
		.bsinitvals_fnv = 0,
	},

	.revision_policy = {
		.known = false,
		.uses_macfreq = false,
		.requires_pcie2_war = false,
	},

	.required_evidence = ob_bcm4360_evidence,
	.required_evidence_count = 8,
};

const struct ob_chip_profile *ob_profile_bcm4360_evidence_only(void)
{
	return &ob_bcm4360_rev03;
}

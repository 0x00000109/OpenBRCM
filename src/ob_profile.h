/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PROFILE_H_
#define _OB_PROFILE_H_

#include <linux/types.h>

/*
 * Common device-profile interface (M6).
 *
 * One immutable descriptor per admitted hardware identity.  The resolver is
 * pure data lookup: it never reads PCI config space, BCMA registers or MMIO.
 * Pipelines stay shared; only data differs per device.
 *
 * Boundaries kept explicit below:
 *   - PCI identity and PCI board (subsystem) identity
 *   - Broadcom chip id/revision
 *   - BCMA D11 core identity
 *   - PHY and radio identity
 *   - firmware requirements
 *   - revision-specific behaviour
 *
 * UNKNOWN is a value, not a default: the *_known flags must be consulted
 * before the corresponding group is used.  A missing value is never replaced
 * by a convenient chip-id-derived guess.
 */

#define OB_PROFILE_UNKNOWN_U8	0xffu
#define OB_PROFILE_UNKNOWN_U16	0xffffu

#define OB_PROFILE_PCI_VENDOR_BROADCOM	0x14e4u
#define OB_PROFILE_BCMA_MANUF_BCM	0x04bfu
#define OB_PROFILE_BCMA_CORE_80211	0x0812u

/* Identity fields already discovered by PCI and BCMA enumeration. */
struct ob_chip_identity {
	bool pci_present;
	bool bcma_present;
	bool d11_present;
	u16 pci_vendor;
	u16 pci_device;
	u8 pci_revision;
	u16 chip_id;
	u8 chip_revision;
	u16 d11_manufacturer;
	u16 d11_core_id;
	u8 d11_revision;
	u8 d11_class;
};

struct ob_profile_phy {
	bool known;
	u16 type;
	u8 revision;
	u8 revision_class;
};

struct ob_profile_radio {
	bool known;
	u16 id;
	u8 revision;
	u8 revision_class;
};

/* Names and size/FNV pins mirror src/ob_fw.h, which remains the validator. */
struct ob_profile_firmware {
	bool known;
	const char *ucode_name;
	u32 ucode_size;
	u64 ucode_fnv;
	const char *initvals_name;
	u32 initvals_size;
	u64 initvals_fnv;
	const char *bsinitvals_name;
	u32 bsinitvals_size;
	u64 bsinitvals_fnv;
};

/* Revision-specific behaviour; `known == false` means the bits are UNKNOWN. */
struct ob_profile_revision_policy {
	bool known;
	bool uses_macfreq;
	bool requires_pcie2_war;
};

/* One named open requirement (E1-E8 for the evidence-only BCM4360). */
struct ob_profile_evidence {
	const char *id;
	const char *status;
	const char *detail;
};

struct ob_chip_profile {
	const char *name;
	/* Admission decision.  A disabled profile is never returned by resolve. */
	bool enabled;

	/* PCI identity. */
	u16 pci_vendor;
	u16 pci_device;
	u8 pci_revision;

	/* PCI board identity: evidence, never silicon identity. */
	bool subsystem_known;
	u16 subsystem_vendor;
	u16 subsystem_device;

	/* Board/SPROM revision; UNKNOWN unless board_known is true. */
	bool board_known;
	u8 board_revision;

	/* Broadcom silicon identity. */
	u16 chip_id;
	u8 chip_revision;

	/* BCMA D11 core identity. */
	u16 d11_manufacturer;
	u16 d11_core_id;
	u8 d11_revision;
	u8 d11_class;

	/* PHY / radio identity. */
	struct ob_profile_phy phy;
	struct ob_profile_radio radio;

	/* Firmware requirements. */
	struct ob_profile_firmware firmware;

	/* Revision-specific hardware behaviour. */
	struct ob_profile_revision_policy revision_policy;

	/* Open evidence required before a disabled profile may be admitted. */
	const struct ob_profile_evidence *required_evidence;
	u32 required_evidence_count;
};

enum ob_chip_profile_status {
	OB_CHIP_PROFILE_OK = 0,
	OB_CHIP_PROFILE_INVALID_ARGUMENT,
	OB_CHIP_PROFILE_MISSING_IDENTITY,
	OB_CHIP_PROFILE_UNSUPPORTED_DEVICE,
	OB_CHIP_PROFILE_UNSUPPORTED_CHIP_REVISION,
	OB_CHIP_PROFILE_UNSUPPORTED_D11_REVISION,
	OB_CHIP_PROFILE_IDENTITY_CONTRADICTION,
};

/*
 * Resolve an identity against the immutable registry.  Rejection statuses keep
 * their historical precedence: device -> chip revision -> contradiction ->
 * D11 revision.  A disabled profile is reported as UNSUPPORTED_DEVICE.
 */
enum ob_chip_profile_status ob_chip_profile_resolve(
	const struct ob_chip_identity *identity,
	const struct ob_chip_profile **profile_out);

/* Immutable descriptors, exposed for evidence tests and the audit artifacts. */
const struct ob_chip_profile *ob_profile_bcm4352_rev03(void);
const struct ob_chip_profile *ob_profile_bcm4360_evidence_only(void);

#endif /* _OB_PROFILE_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common device-profile resolver (M6).
 *
 * Pure: no PCI config read, no BCMA/MMIO access, no allocation.  Admission
 * precedence is the historical one and must stay byte-identical:
 *
 *   missing identity -> unsupported device -> unsupported chip revision ->
 *   identity contradiction -> unsupported D11 revision -> OK
 *
 * A descriptor with enabled == false is reported as UNSUPPORTED_DEVICE, so the
 * evidence-only BCM4360 record can never be returned to probe.
 */
#include "../ob_profile.h"

static const struct ob_chip_profile *profile_by_pci(u16 vendor, u16 device)
{
	const struct ob_chip_profile *registry[] = {
		ob_profile_bcm4352_rev03(),
		ob_profile_bcm4360_evidence_only(),
	};
	unsigned int i;

	for (i = 0; i < sizeof(registry) / sizeof(registry[0]); i++) {
		if (registry[i]->pci_vendor == vendor &&
		    registry[i]->pci_device == device)
			return registry[i];
	}
	return (const struct ob_chip_profile *)0;
}

enum ob_chip_profile_status ob_chip_profile_resolve(
	const struct ob_chip_identity *identity,
	const struct ob_chip_profile **profile_out)
{
	const struct ob_chip_profile *profile;

	if (!profile_out)
		return OB_CHIP_PROFILE_INVALID_ARGUMENT;
	*profile_out = (const struct ob_chip_profile *)0;
	if (!identity || !identity->pci_present || !identity->bcma_present ||
	    !identity->d11_present)
		return OB_CHIP_PROFILE_MISSING_IDENTITY;

	profile = profile_by_pci(identity->pci_vendor, identity->pci_device);
	if (!profile || !profile->enabled)
		return OB_CHIP_PROFILE_UNSUPPORTED_DEVICE;

	if (identity->pci_revision != profile->pci_revision ||
	    identity->chip_revision != profile->chip_revision)
		return OB_CHIP_PROFILE_UNSUPPORTED_CHIP_REVISION;

	if (identity->chip_id != profile->chip_id ||
	    identity->d11_manufacturer != profile->d11_manufacturer ||
	    identity->d11_core_id != profile->d11_core_id ||
	    identity->d11_class != profile->d11_class)
		return OB_CHIP_PROFILE_IDENTITY_CONTRADICTION;

	if (identity->d11_revision != profile->d11_revision)
		return OB_CHIP_PROFILE_UNSUPPORTED_D11_REVISION;

	*profile_out = profile;
	return OB_CHIP_PROFILE_OK;
}

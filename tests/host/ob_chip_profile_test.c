/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>

#include "../../src/ob_chip_profile.h"

static struct ob_chip_identity bcm4352_identity(void)
{
	struct ob_chip_identity id = {
		.pci_present = true,
		.bcma_present = true,
		.d11_present = true,
		.pci_vendor = 0x14e4,
		.pci_device = 0x43b1,
		.pci_revision = 0x03,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.d11_manufacturer = 0x04bf,
		.d11_core_id = 0x0812,
		.d11_revision = 42,
		.d11_class = 0,
	};

	return id;
}

static void expect_status(const char *name, struct ob_chip_identity id,
			  enum ob_chip_profile_status expected)
{
	const struct ob_chip_profile *profile = (const void *)1;
	enum ob_chip_profile_status status;

	status = ob_chip_profile_resolve(&id, &profile);
	if (status != expected) {
		printf("FAIL: %s status=%u expected=%u\n", name,
		       (unsigned int)status, (unsigned int)expected);
		assert(status == expected);
	}
	if (expected != OB_CHIP_PROFILE_OK)
		assert(profile == NULL);
}

int main(void)
{
	struct ob_chip_identity supported = bcm4352_identity();
	struct ob_chip_identity other = bcm4352_identity();
	const struct ob_chip_profile *first = NULL;
	const struct ob_chip_profile *second = NULL;
	enum ob_chip_profile_status status;

	status = ob_chip_profile_resolve(&supported, &first);
	assert(status == OB_CHIP_PROFILE_OK);
	assert(first != NULL);
	assert(first->chip_id == 0x4352);
	assert(first->chip_revision == 3);
	assert(first->d11_revision == 42);

	other.pci_device = 0x43a0;
	other.chip_id = 0x4360;
	expect_status("BCM4360 rejected despite same D11", other,
		      OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);

	other = supported;
	other.pci_device = 0xdead;
	expect_status("unknown PCI ID", other,
		      OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);

	other = supported;
	other.pci_revision = 0x04;
	expect_status("unsupported PCI revision", other,
		      OB_CHIP_PROFILE_UNSUPPORTED_CHIP_REVISION);

	other = supported;
	other.chip_revision = 2;
	expect_status("unsupported BCMA chip revision", other,
		      OB_CHIP_PROFILE_UNSUPPORTED_CHIP_REVISION);

	other = supported;
	other.d11_revision = 24;
	expect_status("unsupported D11 revision", other,
		      OB_CHIP_PROFILE_UNSUPPORTED_D11_REVISION);

	other = supported;
	other.pci_present = false;
	expect_status("missing PCI identity", other,
		      OB_CHIP_PROFILE_MISSING_IDENTITY);

	other = supported;
	other.chip_id = 0x4360;
	expect_status("contradictory PCI and BCMA identity", other,
		      OB_CHIP_PROFILE_IDENTITY_CONTRADICTION);

	other = supported;
	other.d11_class = 1;
	expect_status("contradictory D11 class", other,
		      OB_CHIP_PROFILE_IDENTITY_CONTRADICTION);

	/* Separate devices share only the immutable descriptor, never a mutable
	 * active-chip selector. A rejected second identity cannot alter the first. */
	other = supported;
	status = ob_chip_profile_resolve(&other, &second);
	assert(status == OB_CHIP_PROFILE_OK);
	assert(first == second);
	other.pci_device = 0x43a0;
	other.chip_id = 0x4360;
	status = ob_chip_profile_resolve(&other, &second);
	assert(status == OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);
	assert(second == NULL);
	assert(first->chip_id == 0x4352);

	assert(ob_chip_profile_resolve(NULL, &second) ==
	       OB_CHIP_PROFILE_MISSING_IDENTITY);
	assert(second == NULL);
	assert(ob_chip_profile_resolve(&supported, NULL) ==
	       OB_CHIP_PROFILE_INVALID_ARGUMENT);

	puts("ob_chip_profile_test: PASS");
	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — M7 admission-policy tests.
 *
 * Host-only, pure resolver: no kernel API, no MMIO, no allocation.  The BCMA
 * id table is checked separately by test_admission_policy.py; this test pins
 * the profile-side status a matching core receives.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../../src/ob_profile.h"

static int failures;
static int checks;

static void chk(const char *what, long got, long exp)
{
	checks++;
	if (got != exp) {
		printf("FAIL: %s: got %ld expected %ld\n", what, got, exp);
		failures++;
	}
}

static struct ob_chip_identity identity_4352(void)
{
	struct ob_chip_identity id;

	memset(&id, 0, sizeof(id));
	id.pci_present = true;
	id.bcma_present = true;
	id.d11_present = true;
	id.pci_vendor = 0x14e4;
	id.pci_device = 0x43b1;
	id.pci_revision = 0x03;
	id.chip_id = 0x4352;
	id.chip_revision = 3;
	id.d11_manufacturer = 0x04bf;
	id.d11_core_id = 0x0812;
	id.d11_revision = 42;
	id.d11_class = 0;
	return id;
}

static enum ob_chip_profile_status
status_of(struct ob_chip_identity id, const struct ob_chip_profile **out)
{
	const struct ob_chip_profile *profile = (const void *)1;
	enum ob_chip_profile_status status;

	status = ob_chip_profile_resolve(&id, &profile);
	if (out)
		*out = profile;
	return status;
}

static void test_exact_target(void)
{
	const struct ob_chip_profile *profile = NULL;
	struct ob_chip_identity id = identity_4352();

	chk("4352 status", status_of(id, &profile), OB_CHIP_PROFILE_OK);
	chk("4352 profile returned", profile != NULL, 1);
	chk("4352 enabled", profile ? profile->enabled : 0, 1);
	chk("4352 d11 rev", profile ? profile->d11_revision : -1, 42);
}

static void test_candidate_revisions(void)
{
	static const unsigned int revisions[] = { 17, 23, 24 };
	unsigned int i;

	for (i = 0; i < sizeof(revisions) / sizeof(revisions[0]); i++) {
		struct ob_chip_identity id = identity_4352();
		const struct ob_chip_profile *profile = (const void *)1;
		char label[48];

		id.d11_revision = (unsigned char)revisions[i];
		snprintf(label, sizeof(label), "d11 rev%u rejected", revisions[i]);
		chk(label, status_of(id, &profile),
		    OB_CHIP_PROFILE_UNSUPPORTED_D11_REVISION);
		chk("candidate leaves profile null", profile == NULL, 1);
	}
}

static void test_unknown_and_boundary_revisions(void)
{
	static const unsigned int revisions[] = { 0, 1, 16, 18, 25, 41, 43,
						  99, 255 };
	unsigned int i;

	for (i = 0; i < sizeof(revisions) / sizeof(revisions[0]); i++) {
		struct ob_chip_identity id = identity_4352();
		char label[56];

		id.d11_revision = (unsigned char)revisions[i];
		snprintf(label, sizeof(label), "unknown d11 rev%u", revisions[i]);
		chk(label, status_of(id, NULL),
		    OB_CHIP_PROFILE_UNSUPPORTED_D11_REVISION);
	}
}

static void test_pci_identity(void)
{
	struct ob_chip_identity id;

	id = identity_4352();
	id.pci_vendor = 0x1234;
	chk("wrong pci vendor", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);

	id = identity_4352();
	id.pci_device = 0x43a0;
	chk("wrong pci device (4360 id)", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);

	id = identity_4352();
	id.pci_revision = 0x02;
	chk("wrong pci revision", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_CHIP_REVISION);
}

static void test_chip_and_core_identity(void)
{
	struct ob_chip_identity id;

	id = identity_4352();
	id.chip_id = 0x4360;
	chk("contradictory chip id", status_of(id, NULL),
	    OB_CHIP_PROFILE_IDENTITY_CONTRADICTION);

	id = identity_4352();
	id.chip_revision = 2;
	chk("wrong chip revision", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_CHIP_REVISION);

	id = identity_4352();
	id.d11_manufacturer = 0x0000;
	chk("wrong d11 manufacturer", status_of(id, NULL),
	    OB_CHIP_PROFILE_IDENTITY_CONTRADICTION);

	id = identity_4352();
	id.d11_core_id = 0x0811;
	chk("wrong d11 core", status_of(id, NULL),
	    OB_CHIP_PROFILE_IDENTITY_CONTRADICTION);

	id = identity_4352();
	id.d11_class = 1;
	chk("wrong d11 class", status_of(id, NULL),
	    OB_CHIP_PROFILE_IDENTITY_CONTRADICTION);
}

static void test_bcm4360_stays_disabled(void)
{
	struct ob_chip_identity id;

	memset(&id, 0, sizeof(id));
	id.pci_present = true;
	id.bcma_present = true;
	id.d11_present = true;
	id.pci_vendor = 0x14e4;
	id.pci_device = 0x43a0;
	id.pci_revision = 0x03;
	id.chip_id = 0x4360;
	id.chip_revision = 3;
	id.d11_manufacturer = 0x04bf;
	id.d11_core_id = 0x0812;
	id.d11_revision = 42;
	id.d11_class = 0;
	chk("bcm4360 documented id", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);

	id.chip_id = 0xffff;
	id.d11_revision = 17;
	chk("bcm4360 guessed values", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);
}

static void test_missing_identity_and_arguments(void)
{
	struct ob_chip_identity id;
	const struct ob_chip_profile *profile = (const void *)1;

	chk("null identity", ob_chip_profile_resolve(NULL, &profile),
	    OB_CHIP_PROFILE_MISSING_IDENTITY);
	chk("null output", ob_chip_profile_resolve(&id, NULL),
	    OB_CHIP_PROFILE_INVALID_ARGUMENT);

	id = identity_4352();
	id.pci_present = false;
	chk("missing pci", status_of(id, NULL), OB_CHIP_PROFILE_MISSING_IDENTITY);

	id = identity_4352();
	id.bcma_present = false;
	chk("missing bcma", status_of(id, NULL),
	    OB_CHIP_PROFILE_MISSING_IDENTITY);

	id = identity_4352();
	id.d11_present = false;
	chk("missing d11", status_of(id, NULL),
	    OB_CHIP_PROFILE_MISSING_IDENTITY);
}

static void test_rejection_precedence(void)
{
	struct ob_chip_identity id;

	/* Device before chip revision, contradiction and D11 revision. */
	id = identity_4352();
	id.pci_device = 0x43a0;
	id.pci_revision = 0x02;
	id.chip_id = 0x4360;
	id.d11_class = 1;
	id.d11_revision = 17;
	chk("precedence device", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);

	/* Chip revision before contradiction and D11 revision. */
	id = identity_4352();
	id.pci_revision = 0x02;
	id.chip_id = 0x4360;
	id.d11_class = 1;
	id.d11_revision = 17;
	chk("precedence chip revision", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_CHIP_REVISION);

	/* Contradiction before D11 revision. */
	id = identity_4352();
	id.d11_core_id = 0x0811;
	id.d11_revision = 17;
	chk("precedence contradiction", status_of(id, NULL),
	    OB_CHIP_PROFILE_IDENTITY_CONTRADICTION);

	/* Revision last: valid identity, unsupported revision. */
	id = identity_4352();
	id.d11_revision = 24;
	chk("precedence d11 revision", status_of(id, NULL),
	    OB_CHIP_PROFILE_UNSUPPORTED_D11_REVISION);
}

int main(void)
{
	test_exact_target();
	test_candidate_revisions();
	test_unknown_and_boundary_revisions();
	test_pci_identity();
	test_chip_and_core_identity();
	test_bcm4360_stays_disabled();
	test_missing_identity_and_arguments();
	test_rejection_precedence();

	if (failures) {
		printf("ob_admission_policy_test: %d/%d checks FAILED\n",
		       failures, checks);
		return 1;
	}
	printf("ob_admission_policy_test: %d checks PASS\n", checks);
	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — M6 profile separation tests.
 *
 * Host-only, pure data: no kernel API, no MMIO.  These tests pin the two
 * independent profiles and prove that the evidence-only BCM4360 record cannot
 * be admitted even when every observed/guessed field matches.
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

static void chk_str(const char *what, const char *got, const char *exp)
{
	checks++;
	if (!got || !exp || strcmp(got, exp)) {
		printf("FAIL: %s: got %s expected %s\n", what,
		       got ? got : "(null)", exp ? exp : "(null)");
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

static void test_4352_descriptor(void)
{
	const struct ob_chip_profile *p = ob_profile_bcm4352_rev03();
	const struct ob_chip_profile *resolved = NULL;
	struct ob_chip_identity id = identity_4352();

	chk("4352 descriptor present", p != NULL, 1);
	if (!p)
		return;
	chk("4352 enabled", p->enabled, 1);
	chk_str("4352 name", p->name, "BCM4352-rev03-D11-rev42");
	chk("4352 pci vendor", p->pci_vendor, 0x14e4);
	chk("4352 pci device", p->pci_device, 0x43b1);
	chk("4352 pci revision", p->pci_revision, 0x03);
	chk("4352 board identity known", p->subsystem_known, 1);
	chk("4352 board vendor", p->subsystem_vendor, 0x1043);
	chk("4352 board device", p->subsystem_device, 0x85ba);
	chk("4352 board revision known", p->board_known, 1);
	chk("4352 board revision", p->board_revision, 11);
	chk("4352 chip id", p->chip_id, 0x4352);
	chk("4352 chip revision", p->chip_revision, 3);
	chk("4352 d11 manufacturer", p->d11_manufacturer, 0x04bf);
	chk("4352 d11 core", p->d11_core_id, 0x0812);
	chk("4352 d11 revision", p->d11_revision, 42);
	chk("4352 d11 class", p->d11_class, 0);

	chk("4352 phy known", p->phy.known, 1);
	chk("4352 phy type", p->phy.type, 0x0b);
	chk("4352 phy revision", p->phy.revision, 1);
	chk("4352 phy class", p->phy.revision_class, 0);
	chk("4352 radio known", p->radio.known, 1);
	chk("4352 radio id", p->radio.id, 0x2069);
	chk("4352 radio revision", p->radio.revision, 4);
	chk("4352 radio class", p->radio.revision_class, 0);

	chk("4352 firmware known", p->firmware.known, 1);
	chk_str("4352 ucode name", p->firmware.ucode_name,
		"brcm/bcm4352-d11ucode42.bin");
	chk("4352 ucode size", p->firmware.ucode_size, 43400);
	chk("4352 ucode fnv", (long)p->firmware.ucode_fnv,
	    0x7d364f6207b3298bL);
	chk("4352 initvals size", p->firmware.initvals_size, 4888);
	chk("4352 bsinitvals size", p->firmware.bsinitvals_size, 592);

	chk("4352 revision policy known", p->revision_policy.known, 1);
	chk("4352 uses macfreq", p->revision_policy.uses_macfreq, 1);
	chk("4352 no pcie2 war", p->revision_policy.requires_pcie2_war, 0);
	chk("4352 has no open evidence", p->required_evidence_count, 0);

	chk("4352 resolves", ob_chip_profile_resolve(&id, &resolved),
	    OB_CHIP_PROFILE_OK);
	chk("4352 resolver returns descriptor", resolved == p, 1);
}

static void test_4360_evidence_only(void)
{
	const struct ob_chip_profile *p = ob_profile_bcm4360_evidence_only();
	unsigned int i;

	chk("4360 descriptor present", p != NULL, 1);
	if (!p)
		return;
	chk("4360 disabled", p->enabled, 0);
	chk_str("4360 name", p->name, "BCM4360-rev03-evidence-only");
	chk("4360 pci vendor", p->pci_vendor, 0x14e4);
	chk("4360 pci device", p->pci_device, 0x43a0);
	chk("4360 pci revision", p->pci_revision, 0x03);
	chk("4360 board evidence", p->subsystem_known, 1);
	chk("4360 board vendor", p->subsystem_vendor, 0x1043);
	chk("4360 board device", p->subsystem_device, 0x85df);

	/* No model-number inference: chip, D11, PHY and radio stay UNKNOWN. */
	chk("4360 chip id unknown", p->chip_id, OB_PROFILE_UNKNOWN_U16);
	chk("4360 chip revision unknown", p->chip_revision,
	    OB_PROFILE_UNKNOWN_U8);
	chk("4360 d11 manufacturer unknown", p->d11_manufacturer,
	    OB_PROFILE_UNKNOWN_U16);
	chk("4360 d11 core unknown", p->d11_core_id,
	    OB_PROFILE_UNKNOWN_U16);
	chk("4360 d11 revision unknown", p->d11_revision,
	    OB_PROFILE_UNKNOWN_U8);
	chk("4360 d11 class unknown", p->d11_class,
	    OB_PROFILE_UNKNOWN_U8);
	chk("4360 board revision unknown", p->board_known, 0);
	chk("4360 board revision sentinel", p->board_revision,
	    OB_PROFILE_UNKNOWN_U8);

	chk("4360 phy unknown", p->phy.known, 0);
	chk("4360 phy type sentinel", p->phy.type,
	    OB_PROFILE_UNKNOWN_U16);
	chk("4360 phy revision sentinel", p->phy.revision,
	    OB_PROFILE_UNKNOWN_U8);
	chk("4360 radio unknown", p->radio.known, 0);
	chk("4360 radio id sentinel", p->radio.id,
	    OB_PROFILE_UNKNOWN_U16);
	chk("4360 radio revision sentinel", p->radio.revision,
	    OB_PROFILE_UNKNOWN_U8);

	chk("4360 firmware unknown", p->firmware.known, 0);
	chk("4360 ucode not selected", p->firmware.ucode_name == NULL, 1);
	chk("4360 initvals not selected", p->firmware.initvals_name == NULL, 1);
	chk("4360 bsinitvals not selected",
	    p->firmware.bsinitvals_name == NULL, 1);
	chk("4360 revision policy unknown", p->revision_policy.known, 0);

	chk("4360 evidence count", p->required_evidence_count, 8);
	for (i = 0; i < p->required_evidence_count && i < 8; i++) {
		const struct ob_profile_evidence *e =
			&p->required_evidence[i];
		char expected_id[4] = { 'E', (char)('1' + i), '\0', '\0' };

		chk_str("4360 evidence id", e->id, expected_id);
		chk("4360 evidence status present",
		    e->status && e->status[0] ? 1 : 0, 1);
		chk("4360 evidence detail present",
		    e->detail && e->detail[0] ? 1 : 0, 1);
	}
}

static void test_4360_never_admitted(void)
{
	const struct ob_chip_profile *out = (const void *)1;
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
	id.d11_revision = 0x2a;
	id.d11_class = 0;

	chk("4360 rejected", ob_chip_profile_resolve(&id, &out),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);
	chk("4360 output cleared", out == NULL, 1);

	/* Even the exact documented/observed PCI tuple stays rejected. */
	id.chip_id = OB_PROFILE_UNKNOWN_U16;
	id.chip_revision = OB_PROFILE_UNKNOWN_U8;
	id.d11_revision = OB_PROFILE_UNKNOWN_U8;
	id.d11_class = OB_PROFILE_UNKNOWN_U8;
	out = (const void *)1;
	chk("4360 unknown-chip rejected", ob_chip_profile_resolve(&id, &out),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);
	chk("4360 unknown-chip output cleared", out == NULL, 1);

	/* Unknown ids never resolve to a profile. */
	id.pci_vendor = 0xffff;
	id.pci_device = 0xffff;
	out = (const void *)1;
	chk("unknown device rejected", ob_chip_profile_resolve(&id, &out),
	    OB_CHIP_PROFILE_UNSUPPORTED_DEVICE);
	chk("unknown device output cleared", out == NULL, 1);
}

int main(void)
{
	test_4352_descriptor();
	test_4360_evidence_only();
	test_4360_never_admitted();

	if (failures) {
		printf("ob_profile_evidence_test: %d/%d checks FAILED\n",
		       failures, checks);
		return 1;
	}
	printf("ob_profile_evidence_test: %d checks PASS\n", checks);
	return 0;
}

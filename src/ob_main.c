// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — module entry point and bcma driver registration.
 *
 * OpenBRCM is a clean-room SoftMAC driver for Broadcom 43xx PCIe Wi-Fi
 * controllers, built on mac80211/cfg80211. It is derived solely from the
 * OpenBRCM reverse-engineering specification and publicly documented silicon
 * behaviour (see docs/provenance.md). It contains no code from the proprietary
 * blob or from other drivers.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bcma/bcma.h>
#include "ob_core.h"

/*
 * Recognized BCMA cores (D11 / 80211 core).
 *
 * Admission policy (M7): advertise only the D11 revision for which an enabled
 * experimental device profile exists.  Linux bcma_bus_match() compares
 * manufacturer, core id, revision and class for exact equality unless a
 * BCMA_ANY_* wildcard is used (drivers/bcma/main.c), so a D11 core with
 * revision 17, 23 or 24 is NOT_MATCHED: ob_probe() is never called and no
 * OpenBRCM allocation, register access or initialization stage can occur.
 * There is deliberately no probe-time error for those revisions because no
 * probe happens.
 *
 * Historical candidate revisions 17, 23 and 24 remain recorded below as future
 * compatibility candidates only.  They are intentionally not advertised or
 * bound because no enabled profile exists for them.  Adding one later requires
 * both an enabled profile and an explicit table entry.  Do not use
 * BCMA_ANY_REV: bind only to explicit revisions.
 *
 * BCMA_ANY_CLASS is retained deliberately.  A rev42 core with a non-zero class
 * still reaches ob_probe() and is rejected there with the typed
 * OB_CHIP_PROFILE_IDENTITY_CONTRADICTION status before any side effect; making
 * the class exact here would turn a typed probe rejection into NOT_MATCHED and
 * would broaden M7 beyond the revision policy.
 *
 * Provenance for the BCM4352 (D11 core revision 0x2A = 42) entry:
 *   Real hardware:  Broadcom BCM4352 [14e4:43b1] rev 03
 *                   ASUS PCE-AC56 subsystem [1043:85ba]
 *   Runtime device: /sys/bus/bcma/devices/bcma0:1
 *   Observed:       manuf = 0x4BF, id = 0x812, rev = 0x2A
 *   Exact modalias: bcma:m04BFid0812rev2Acl00
 *
 * The BCMA table is a candidate-core match, not chip support policy.
 * ob_probe() performs exact per-device PCI/BCMA/D11 profile admission before
 * dispatching any OpenBRCM initialization mode; currently that admits only
 * BCM4352 PCI rev03 / BCMA rev3 / D11 rev42 class0.
 *
 * Future compatibility candidates (NOT advertised, NOT bound, no profile):
 *   BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 17, BCMA_ANY_CLASS)
 *   BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 23, BCMA_ANY_CLASS)
 *   BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 24, BCMA_ANY_CLASS)
 */
static struct bcma_device_id ob_coreid_table[] = {
	/* BCM4352 (ASUS PCE-AC56): D11 core rev 0x2A */
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 42, BCMA_ANY_CLASS),
	{},
};
MODULE_DEVICE_TABLE(bcma, ob_coreid_table);

static struct bcma_driver ob_bcma_driver = {
	.name		= OB_DRV_NAME,
	.id_table	= ob_coreid_table,
	.probe		= ob_probe,
	.remove		= ob_remove,
};

module_bcma_driver(ob_bcma_driver);

MODULE_DESCRIPTION("OpenBRCM — clean-room Broadcom 43xx SoftMAC driver");
MODULE_AUTHOR("OpenBRCM contributors");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(OB_FW_UCODE42_NAME);
MODULE_FIRMWARE(OB_FW_UCODE42_NAME_LEGACY);
MODULE_FIRMWARE(OB_FW_AC1INITVALS42_NAME);
MODULE_FIRMWARE(OB_FW_AC1BSINITVALS42_NAME);

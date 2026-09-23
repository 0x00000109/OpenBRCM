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

static struct bcma_device_id ob_coreid_table[] = {
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 17, BCMA_ANY_CLASS),
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 23, BCMA_ANY_CLASS),
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_80211, 24, BCMA_ANY_CLASS),
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
MODULE_FIRMWARE("brcm/bcm43xx-0.fw");
MODULE_FIRMWARE("brcm/bcm43xx_hdr-0.fw");

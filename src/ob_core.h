/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — core state.
 *
 * Clean-room implementation derived from the OpenBRCM RE specification
 * (see docs/provenance.md). No code copied from the proprietary blob or from
 * upstream drivers.
 */
#ifndef _OB_CORE_H_
#define _OB_CORE_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/bcma/bcma.h>
#include "ob_dma.h"

#define OB_DRV_NAME	"openbrcm"

struct ieee80211_hw;

/**
 * struct ob_hw - per-device state
 * @core:	the D11 (80211) bcma core
 * @dev:	backing bcma device
 * @bus:	the silicon backplane bus
 * @chip_id:	chip id (e.g. 0x4352)
 * @chip_rev:	chip revision
 * @cc:		chipcommon core (register window for CC/PMU/SPROM)
 * @mac:	validated factory MAC from the external SPROM (rev8/rev11)
 * @mac_valid:	true when @mac passed CRC/revision validation and eth checks
 * @ieee:	mac80211 hw, once registered
 * @dma:	DMA64 descriptor rings and DMA capability (M3.2, software only)
 */
struct ob_hw {
	struct bcma_device	*core;
	struct device		*dev;
	struct bcma_bus		*bus;
	u16			chip_id;
	u8			chip_rev;
	struct bcma_device	*cc;
	u8			mac[6];
	bool			mac_valid;
	struct ieee80211_hw	*ieee;
	struct ob_dma		dma;
};

int ob_probe(struct bcma_device *core);
void ob_remove(struct bcma_device *core);

#endif /* _OB_CORE_H_ */

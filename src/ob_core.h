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
#include "ob_irq.h"
#include "ob_rx.h"
#include "ob_fw.h"
#include "ob_d3a0.h"
#include "ob_d3a1.h"

#define OB_DRV_NAME	"openbrcm"

struct ieee80211_hw;

/**
 * struct ob_hw - per-device state
 * @core:	the D11 (80211) bcma core
 * @dev:	backing bcma device
 * @bus:	the silicon backplane bus
 * @chip_id:	chip id (e.g. 0x4352)
 * @chip_rev:	chip revision
 * @validate_only: true when probe ran with fw_validate_only=1: the device is
 *		bound and the rev42 images were validated, but no OpenBRCM
 *		hardware bring-up happened and @remove must skip all teardown
 * @ucode_test_only: true when probe ran with ucode_test_only=1: the device is
 *		bound, minimum core prep + ucode upload + PSM start ran, and
 *		@remove must skip all RX/IRQ/DMA/mac80211 teardown
 * @initvals_test_only: true when probe ran with initvals_test_only=1: the
 *		device is bound, the proven D2A core ran, the 610 common
 *		initvals were applied and postconditions verified; @remove must
 *		skip all RX/IRQ/DMA/mac80211 teardown
 * @dma_test_only: true when probe ran with dma_test_only=1: the device is
 *		bound, the D2B prefix plus the pinned D11/clock/IRQ-source
 *		prerequisites ran and the isolated DMA lifecycle (4 TX + FIFO0
 *		RX) was brought up, validated, quiesced and freed; @remove
 *		handles the fail-closed DMA lifecycle via ob_d3a0_remove()
 * @d3a1_test_only: true when probe ran with d11_tail_test_only=1: the device
 *		is bound, the proven D2A/D2B core ran, the exact vendor rev42
 *		post-common tail (sub_67efd + T1 + DMA in vendor position + T2 +
 *		switch_macfreq) ran and STOPPED before sub_6656c/bsinitvals/PHY;
 *		@remove handles the fail-closed DMA lifecycle via ob_d3a1_remove()
 * @cc:		chipcommon core (register window for CC/PMU/SPROM)
 * @mac:	validated factory MAC from the external SPROM (rev8/rev11)
 * @mac_valid:	true when @mac passed CRC/revision validation and eth checks
 * @ieee:	mac80211 hw, once registered
 * @dma:	DMA64 descriptor rings and DMA capability (M3.2, software only)
 * @irq:	D11 interrupt registration and counters (M3.3)
 * @rx:		FIFO0 RX engine state (M3.4B)
 * @fw:		rev42 firmware validation state (M3.4D1, acquisition only)
 */
struct ob_hw {
	struct bcma_device	*core;
	struct device		*dev;
	struct bcma_bus		*bus;
	u16			chip_id;
	u8			chip_rev;
	bool			validate_only;
	bool			ucode_test_only;
	bool			initvals_test_only;
	bool			dma_test_only;
	bool			d3a1_test_only;
	struct bcma_device	*cc;
	u8			mac[6];
	bool			mac_valid;
	struct ieee80211_hw	*ieee;
	struct ob_dma		dma;
	struct ob_irq		irq;
	struct ob_rx		rx;
	struct ob_fw		fw;
	struct ob_d3a0		d3a0;
	struct ob_d3a1		d3a1;
};

int ob_probe(struct bcma_device *core);
void ob_remove(struct bcma_device *core);

#endif /* _OB_CORE_H_ */

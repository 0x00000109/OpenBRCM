// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — silicon backplane bring-up.
 *
 * The register map and the exact values/sequences are recovered from the blob
 * (RE Stage 4/5) and cross-checked against Linux bcma. This file is the only
 * place that touches PMU/PLL/clock/OTP/SPROM directly.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/bcma/bcma.h>
#include "ob_si.h"

/* CLKCTLST: HT clock available (matches BCMA_CLKCTLST_HAVEHT). */
#define OB_CLKCTLST_HAVEHT	0x00020000

/* Bounded polling: 50 * 10 ms = 500 ms. No infinite waits. */
#define OB_SHADOW_POLLS		50
#define OB_SHADOW_POLL_MS	10

/* ChipCommon is a fixed-function core; access it through bcma's window. */
u32 ob_si_cc_read(struct ob_hw *hw, u16 off)
{
	return bcma_read32(hw->cc, off);
}

void ob_si_cc_write(struct ob_hw *hw, u16 off, u32 val)
{
	bcma_write32(hw->cc, off, val);
}

/*
 * Read the factory MAC address from the SPROM shadow (the location is derived
 * from OTPL.GURGN_OFFSET). The shadow is populated by the chip power-up; if the
 * chip is not powered up yet this reads 0xff/0x00 and we report it as such.
 *
 * Verified layout (RE Stage 4): read 3 words at shadow + 0x4c; each word holds
 * two MAC octets in big-endian order within the word.
 */
int ob_si_read_mac(struct ob_hw *hw, u8 mac[6])
{
	u32 otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	u32 off = OB_CC_SPROM + ((otpl & 0xfff) >> 3);
	u32 w0, w1, w2;

	w0 = ob_si_cc_read(hw, off + OB_SPROM_MAC_OFFSET);
	w1 = ob_si_cc_read(hw, off + OB_SPROM_MAC_OFFSET + 4);
	w2 = ob_si_cc_read(hw, off + OB_SPROM_MAC_OFFSET + 8);

	mac[0] = (w0 >> 8) & 0xff;
	mac[1] =  w0       & 0xff;
	mac[2] = (w1 >> 8) & 0xff;
	mac[3] =  w1       & 0xff;
	mac[4] = (w2 >> 8) & 0xff;
	mac[5] =  w2       & 0xff;

	if (!is_valid_ether_addr(mac)) {
		dev_warn(hw->dev, "SPROM shadow unpopulated (chip not powered up?)\n");
		return -ENODATA;
	}
	return 0;
}

void ob_si_dump(struct ob_hw *hw)
{
	u32 cap = ob_si_cc_read(hw, OB_CC_CAP);
	u32 otps = ob_si_cc_read(hw, OB_CC_OTPS);
	u32 otpl = ob_si_cc_read(hw, OB_CC_OTPL);
	u32 srom_ctl = ob_si_cc_read(hw, OB_CC_SROM_CONTROL);
	u32 clkctl = ob_si_cc_read(hw, OB_CC_CLKCTLST);
	u32 pmu_cap = ob_si_cc_read(hw, OB_CC_PMU_CAP);
	u32 pmu_ctl = ob_si_cc_read(hw, OB_CC_PMU_CTL);

	dev_info(hw->dev, "cc: cap=%08x otps=%08x otpl=%08x srom_ctl=%08x\n",
		 cap, otps, otpl, srom_ctl);
	dev_info(hw->dev, "pmu: cap=%08x ctl=%08x clkctlst=%08x\n",
		 pmu_cap, pmu_ctl, clkctl);
	dev_info(hw->dev, "sprom present=%d otp present=%d otpsel=%d\n",
		 !!(srom_ctl & OB_SROM_PRESENT),
		 !!(srom_ctl & OB_SROM_OTP_PRESENT),
		 !!(srom_ctl & OB_SROM_OTPSEL));
}

/*
 * Read-only diagnostics of the bcma host/core topology. All of these are plain
 * struct reads (no MMIO), used purely to prove the PCIe2 vs legacy-PCI topology
 * before any power operation. Safe to call at any time.
 */
static void ob_si_dump_bus(struct ob_hw *hw)
{
	struct bcma_bus *bus = hw->bus;
	struct bcma_device *d11 = hw->core;

	dev_info(hw->dev,
		 "bus: hosttype=%d host_is_pcie2=%d host_pci=%px\n",
		 bus->hosttype, bus->host_is_pcie2, bus->host_pci);
	dev_info(hw->dev,
		 "bus: drv_pci[0].core=%px drv_pci[1].core=%px drv_pcie2.core=%px\n",
		 bus->drv_pci[0].core, bus->drv_pci[1].core, bus->drv_pcie2.core);
	dev_info(hw->dev,
		 "d11: core_index=%u addr=0x%x wrap=0x%x rev=%u\n",
		 d11->core_index, d11->addr, d11->wrap, d11->id.rev);
}

/* SPROM shadow base: 0x800 + (OTPL.GURGN_OFFSET >> 3). */
static u32 ob_si_sprom_base(struct ob_hw *hw)
{
	u32 otpl = ob_si_cc_read(hw, OB_CC_OTPL);

	return OB_CC_SPROM + ((otpl & 0xfff) >> 3);
}

static void ob_si_dump_shadow(struct ob_hw *hw, const char *tag)
{
	u32 base = ob_si_sprom_base(hw);

	dev_info(hw->dev,
		 "powerup[%s]: shadow@0x%x rev=%08x mac=%08x %08x %08x\n",
		 tag, base,
		 ob_si_cc_read(hw, base),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET + 4),
		 ob_si_cc_read(hw, base + OB_SPROM_MAC_OFFSET + 8));
}

/*
 * M2.5 — instrumented power-up for the BCM4352.
 *
 * Steps that are delegated to the in-kernel bcma bus driver (authoritative,
 * GPL-exported) are used directly: core enable/reset (bcma_core_enable is the
 * kernel implementation of the recovered ai_core_reset semantics) and HT clock
 * forcing (bcma_core_set_clockmode implements FORCEHT + HAVEHT polling with a
 * timeout). Steps whose values are not provenance-backed for 0x4352 are NOT
 * guessed; they are reported as UNKNOWN.
 */
int ob_si_powerup(struct ob_hw *hw)
{
	u32 clk;
	int i, err;

	dev_info(hw->dev, "powerup: chip 0x%04x rev %u\n",
		 hw->chip_id, hw->chip_rev);

	/* Stage: initial state */
	ob_si_dump(hw);
	ob_si_dump_shadow(hw, "before");
	dev_info(hw->dev, "powerup: d11 core enabled=%d\n",
		 bcma_core_is_enabled(hw->core));

	/* Stage 0: topology diagnostics (read-only) */
	ob_si_dump_bus(hw);

	/*
	 * Stage A: host PCIe bring-up.
	 *
	 * bcma_core_pci_power_save() is the legacy PCI-core power-save path and
	 * only guards bus->hosttype, NOT bus->host_is_pcie2. A modern BCM4352
	 * board has no legacy PCI core, so bus->drv_pci[0].core is NULL and that
	 * function dereferences NULL at pc->core->id.rev (fault address 0xc).
	 *
	 * Use the public host abstraction instead: bcma_host_pci_up() checks
	 * hosttype and dispatches on host_is_pcie2 to bcma_core_pcie2_up() — the
	 * correct path for this bus. The legacy call is removed entirely.
	 */
	dev_info(hw->dev, "powerup[A]: host up (host_is_pcie2=%d)\n",
		 hw->bus->host_is_pcie2);
	bcma_host_pci_up(hw->bus);
	dev_info(hw->dev,
		 "powerup[A]: host up done drv_pcie2.core=%px drv_pci[0].core=%px\n",
		 hw->bus->drv_pcie2.core, hw->bus->drv_pci[0].core);

	/* Stage B: D11 core enable/reset, separate from host bring-up */
	dev_info(hw->dev, "powerup[B]: d11 enabled(before)=%d\n",
		 bcma_core_is_enabled(hw->core));
	if (!bcma_core_is_enabled(hw->core)) {
		err = bcma_core_enable(hw->core, 0);
		dev_info(hw->dev,
			 "powerup[B]: bcma_core_enable(d11) err=%d enabled(after)=%d\n",
			 err, bcma_core_is_enabled(hw->core));
	}

	/* Stage C: force HT clock and wait for it (bounded inside bcma) */
	dev_info(hw->dev, "powerup[C]: set clockmode FAST\n");
	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);
	clk = ob_si_cc_read(hw, OB_CC_CLKCTLST);
	dev_info(hw->dev, "powerup[C]: clkctlst=0x%08x HAVEHT=%d\n",
		 clk, !!(clk & OB_CLKCTLST_HAVEHT));

	/*
	 * Stage: OTP power. On this chip si_pmu_otp_power is a no-op (OTP is
	 * always powered) — provenance: RE Stage 4/5. No register write needed.
	 */
	dev_info(hw->dev, "powerup: OTP power no-op for 0x4352 (provenance C2)\n");

	/*
	 * Stage: OTP -> SPROM-shadow activation.
	 *
	 * UNKNOWN: the exact ChipCommon OTP read FSM (otp_read_word variants) and
	 * the 0x4352-specific PLL branch were not fully recovered, and prior
	 * exhaustive probing found no single register/clock toggle that populates
	 * the shadow. Per project policy we do not guess register values here.
	 * We only wait (bounded) for the shadow to become valid and report.
	 */
	for (i = 0; i < OB_SHADOW_POLLS; i++) {
		u32 w0 = ob_si_cc_read(hw, ob_si_sprom_base(hw) + OB_SPROM_MAC_OFFSET);

		if (w0 != 0 && w0 != 0xffffffff)
			break;
		msleep(OB_SHADOW_POLL_MS);
	}

	ob_si_dump_shadow(hw, "after");
	dev_warn(hw->dev,
		 "powerup: UNKNOWN - BCM4352 OTP->SPROM activation not provenance-backed; no guessed writes performed (docs/milestones.md)\n");

	return 0;
}

int ob_si_probe(struct ob_hw *hw)
{
	u8 mac[6];

	/* ChipCommon core provides the CC/PMU/OTP/SPROM window. */
	hw->cc = hw->bus->drv_cc.core;
	if (!hw->cc) {
		dev_err(hw->dev, "no ChipCommon core\n");
		return -ENODEV;
	}

	ob_si_powerup(hw);

	if (!ob_si_read_mac(hw, mac))
		dev_info(hw->dev, "SROM MAC %pM\n", mac);
	else
		dev_warn(hw->dev, "MAC not available from SPROM shadow\n");

	return 0;
}

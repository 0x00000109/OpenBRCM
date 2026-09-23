// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — silicon backplane bring-up.
 *
 * The register map and the exact values/sequences are recovered from the blob
 * (RE Stage 4/5) and cross-checked against Linux bcma. This file is the only
 * place that touches PMU/PLL/clock/OTP/SPROM directly.
 */
#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include "ob_si.h"

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

int ob_si_probe(struct ob_hw *hw)
{
	u8 mac[6];

	/* ChipCommon core provides the CC/PMU/OTP/SPROM window. */
	hw->cc = hw->bus->drv_cc.core;
	if (!hw->cc) {
		dev_err(hw->dev, "no ChipCommon core\n");
		return -ENODEV;
	}

	ob_si_dump(hw);

	if (!ob_si_read_mac(hw, mac))
		dev_info(hw->dev, "SROM MAC %pM\n", mac);

	return 0;
}

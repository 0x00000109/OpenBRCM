// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — BCM2069 radio identity observation (D4-BLOCKER-PLL-BRANCH-HW-PROBE).
 *
 * Smallest isolated probe that reads the radio revision from the D11 radio
 * indirect window and stops. It is deliberately NOT a normal bring-up path and
 * NOT a radio register dump: exactly two selector writes (radio reg 0 and 1)
 * and two 16-bit data reads, plus one trusted accessibility sentinel before and
 * after. It never issues a PLL sequence, calibration, channel, DMA, IRQ, PHY
 * table, TX/RX or mac80211 operation.
 *
 * Minimum prerequisite chain (see docs/m34d4/radio_probe_design.md Part A):
 * host/backplane up + D11 core enable + FAST clock + D11 MAC-PHY clock
 * (SICF_MPCLKE). The vendor reads radio reg 0/1 in wlc_phy_attach, reached
 * from wlc_bmac_attach BEFORE ucode/initvals/clkctl_init/DMA, so none of the
 * heavier stages (ucode, PSM, common initvals, D3A1, DMA, D3B MHF, bsinitvals,
 * PHY init) is required. wlc_bmac_core_phypll_ctl is a proven no-op for rev42.
 *
 * All register access goes through the central dev_lost guard. After the
 * monotonic latch no MMIO is issued and the module is pinned; reboot clears it.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_radio.h"
#include "ob_ucode.h"
#include "ob_d3a0.h"

/* ---- backend: central dev_lost-guarded D11 window ----------------------- */

static u32 ob_radio_io_read32_trusted(void *ctx, u16 off)
{
	struct ob_hw *hw = ctx;

	return ob_d11_read32_trusted(hw, "radio-probe", off);
}

static u16 ob_radio_io_read16(void *ctx, u16 off)
{
	return ob_d11_read16((struct ob_hw *)ctx, off);
}

static void ob_radio_io_write16(void *ctx, u16 off, u16 val)
{
	ob_d11_write16((struct ob_hw *)ctx, off, val);
}

static const struct ob_radio_io_ops ob_radio_io = {
	.read32_trusted = ob_radio_io_read32_trusted,
	.read16 = ob_radio_io_read16,
	.write16 = ob_radio_io_write16,
};

/*
 * Minimum proven prerequisite (M2.5 order, shared by the isolated test modes)
 * plus the D11 MAC-PHY clock gate that the vendor corereset applies on the AC
 * path before the radio read. No PHY/radio/PLL register is written.
 */
static int ob_radio_prepare(struct ob_hw *hw)
{
	u32 clk, ioc;
	int err;

	if (hw->dev_lost)
		return -EIO;

	dev_info(hw->dev, "radio-probe: prep host up (host_is_pcie2=%d)\n",
		 hw->bus->host_is_pcie2 ? 1 : 0);
	bcma_host_pci_up(hw->bus);

	if (!bcma_core_is_enabled(hw->core)) {
		err = bcma_core_enable(hw->core, 0);
		if (err) {
			dev_err(hw->dev,
				"radio-probe: D11 core enable failed: %d\n", err);
			return err;
		}
	}
	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);

	clk = ob_d11_read32(hw, OB_UCODE_REG_CLKCTLST);
	dev_info(hw->dev,
		 "radio-probe: clkctlst=%08x HAVEHT=%d core_enabled=%d\n",
		 clk, !!(clk & OB_UCODE_CLKCTLST_HAVEHT),
		 bcma_core_is_enabled(hw->core) ? 1 : 0);

	ioc = ob_axi_read32(hw, BCMA_IOCTL);
	ob_axi_write32(hw, BCMA_IOCTL, ioc | OB_D3A0_IOCTL_MPCLKE);
	ioc = ob_axi_read32(hw, BCMA_IOCTL);
	dev_info(hw->dev, "radio-probe: ioctl=%08x mpclke=%d\n",
		 ioc, !!(ioc & OB_D3A0_IOCTL_MPCLKE) ? 1 : 0);
	return 0;
}

int ob_radio_probe_test(struct ob_hw *hw)
{
	struct ob_radio_seq_result res;
	int ret;

	dev_info(hw->dev, "radio-probe: BEGIN\n");

	ret = ob_radio_prepare(hw);
	if (ret)
		return ret;

	ret = ob_radio_sequence(&ob_radio_io, hw, &hw->dev_lost, &res);
	if (res.stopped_dev_lost) {
		dev_crit(hw->dev,
			 "radio-probe: FAIL dev_lost pre_access=0x%08x post_access=0x%08x; no further MMIO, reboot required\n",
			 res.pre_access, res.post_access);
		return ret ? ret : -EIO;
	}

	dev_info(hw->dev, "radio-probe: pre_access=0x%08x\n", res.pre_access);
	dev_info(hw->dev, "radio-probe: reg0_raw=0x%04x\n", res.reg0_raw);
	dev_info(hw->dev, "radio-probe: reg1_raw=0x%04x\n", res.reg1_raw);
	dev_info(hw->dev, "radio-probe: radioid=0x%04x\n", res.obs.radio_id);
	dev_info(hw->dev, "radio-probe: radiorev=0x%02x\n", res.obs.radio_rev);
	dev_info(hw->dev, "radio-probe: revision_class=%u\n",
		 res.obs.revision_class);
	dev_info(hw->dev, "radio-probe: id_accepted=%d id_is_2069=%d\n",
		 res.obs.id_accepted ? 1 : 0, res.obs.id_is_2069 ? 1 : 0);
	dev_info(hw->dev, "radio-probe: pll_branch=%s\n",
		 ob_radio_branch_name(res.obs.branch));
	dev_info(hw->dev, "radio-probe: post_access=0x%08x\n", res.post_access);

	if (!res.valid) {
		dev_err(hw->dev,
			"radio-probe: FAIL invalid observation (all-ones pair reg0=0x%04x reg1=0x%04x)\n",
			res.reg0_raw, res.reg1_raw);
		return -EIO;
	}
	if (!res.obs.id_accepted) {
		dev_err(hw->dev,
			"radio-probe: FAIL unexpected radioid=0x%04x (AC accepts 0x2069/0x030b)\n",
			res.obs.radio_id);
		return -EIO;
	}

	dev_info(hw->dev, "radio-probe: PASS\n");
	dev_info(hw->dev, "radio-probe: STOPPED BEFORE PLL/RADIO INIT\n");
	return 0;
}

void ob_radio_remove(struct ob_hw *hw)
{
	if (hw->d3a0.lc.fatal || hw->dev_lost) {
		dev_crit(hw->dev,
			 "radio-probe: removed in FATAL/dev-lost state; no teardown, reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	dev_info(hw->dev,
		 "radio-probe: removed (no resource teardown, hardware left as-is)\n");
	bcma_set_drvdata(hw->core, NULL);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — D11 rev42 ucode upload + PSM start only (M3.4D2A).
 *
 * Isolated hardware test mode (module param ucode_test_only=1). It performs the
 * minimum recovered sequence to make the D11 accept the rev42 ucode and start
 * the PSM, then STOPS before common initvals, PHY, radio, channel, DMA, IRQ and
 * mac80211. Nothing else in OpenBRCM is reachable from here.
 *
 * Sequence (exact vendor order, see ob_ucode.h for per-step provenance):
 *   1. minimal core prep: bcma_host_pci_up + D11 core enable/reset + FAST clock
 *   2. pre-upload MACCONTROL  = 0x04000404 (IHR_EN | PSM_JMP0 | WAKE), masked RMW
 *   3. ucode upload via OBJADDR=0x03000000 (auto-inc) + 10850 OBJDATA writes
 *   4. macintstatus = 0xffffffff, then PSM start MACCONTROL = 0x04020402
 *   5. bounded poll of macintstatus & MI_MACSSPNDD (10 us, <=100000 iters)
 *   6. read-only SHM FIFO-size diagnostic (no equality test)
 *
 * No initvals are applied, no EN_MAC, no request_irq, no DMA, no mac80211.
 * No cleanup register writes are performed on any failure path (see the
 * failure/unwind matrix in docs/milestones.md).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_fw.h"
#include "ob_ucode.h"

/*
 * Minimum proven D11 preparation for the ucode upload, in the proven M2.5
 * order (ob_si_powerup stages A/B/C): host/backplane up, D11 core enable/reset,
 * then FAST (HT) clock. The vendor reaches this state through
 * wlc_bmac_up_prep()/wlc_bmac_corereset() before wlc_bmac_init() uploads ucode.
 */
static int ob_ucode_prepare(struct ob_hw *hw)
{
	u32 clk;
	int err;

	dev_info(hw->dev, "ucode-test: prep host up (host_is_pcie2=%d)\n",
		 hw->bus->host_is_pcie2);
	bcma_host_pci_up(hw->bus);

	if (!bcma_core_is_enabled(hw->core)) {
		err = bcma_core_enable(hw->core, 0);
		if (err) {
			dev_err(hw->dev, "ucode-test: D11 core enable failed: %d\n",
				err);
			return err;
		}
	}

	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);
	clk = bcma_read32(hw->core, OB_UCODE_REG_CLKCTLST);
	dev_info(hw->dev,
		 "ucode-test: clkctlst=%08x HAVEHT=%d core_enabled=%d\n",
		 clk, !!(clk & OB_UCODE_CLKCTLST_HAVEHT),
		 bcma_core_is_enabled(hw->core));
	return 0;
}

/*
 * Vendor SHM read (wlc_bmac_read_shm @0x61910 -> helper 0x6188a): select the
 * SHM window in OBJADDR, read it back (barrier), then read the 16-bit half
 * selected by offset bit1 from OBJDATA. offset is a byte offset.
 */
static u16 ob_ucode_read_shm16(struct ob_hw *hw, u16 off)
{
	bcma_write32(hw->core, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	(void)bcma_read32(hw->core, OB_UCODE_REG_OBJADDR);
	return bcma_read16(hw->core, OB_UCODE_REG_OBJDATA + (off & 0x2));
}

/* Masked MACCONTROL update, mirroring wlc_bmac_mctrl(dev, mask, val). */
static u32 ob_ucode_mctrl_update(struct ob_hw *hw, u32 mask, u32 val)
{
	u32 old = bcma_read32(hw->core, OB_UCODE_REG_MACCONTROL);
	u32 new = (old & ~mask) | val;

	bcma_write32(hw->core, OB_UCODE_REG_MACCONTROL, new);
	return bcma_read32(hw->core, OB_UCODE_REG_MACCONTROL);
}

int ob_ucode_test(struct ob_hw *hw)
{
	const struct firmware *ucode = NULL;
	const char *name = NULL;
	const char *stage = "request";
	u32 words, i, written = 0, status = 0, remaining, iterations = 0;
	int ret = 0;

	dev_info(hw->dev, "ucode-test: BEGIN\n");

	/* Acquire + hash + size-validate the exact vendor rev42 ucode first. */
	ret = ob_fw_request_ucode(hw, &ucode, &name);
	if (ret) {
		dev_err(hw->dev, "ucode-test: ucode unavailable/invalid: %d\n",
			ret);
		goto out;
	}
	words = ob_ucode_words_from_size(ucode->size);
	dev_info(hw->dev, "ucode-test: image %s size=%zu words=%u\n",
		 name ? name : "?", ucode->size, words);

	/* 1. minimum core preparation. */
	stage = "core-prep";
	ret = ob_ucode_prepare(hw);
	if (ret)
		goto out;
	dev_info(hw->dev, "ucode-test: core prepared\n");

	/* 2. pre-upload MACCONTROL (masked update, mask = ~0). */
	stage = "maccontrol-upload";
	{
		u32 before = bcma_read32(hw->core, OB_UCODE_REG_MACCONTROL);
		u32 after;

		dev_info(hw->dev, "ucode: maccontrol before=%08x\n", before);
		after = ob_ucode_mctrl_update(hw, OB_UCODE_MACCONTROL_MASK,
					      OB_UCODE_MACCONTROL_UPLOAD);
		dev_info(hw->dev, "ucode: maccontrol upload-state=%08x\n",
			 after);
		if ((after & OB_UCODE_UPLOAD_EXPECTED) !=
		    OB_UCODE_UPLOAD_EXPECTED ||
		    (after & OB_UCODE_UPLOAD_FORBIDDEN)) {
			dev_err(hw->dev,
				"ucode-test: maccontrol upload state invalid\n");
			ret = -EIO;
			goto out;
		}
	}
	dev_info(hw->dev, "ucode-test: MAC upload state ready\n");

	/* 3. ucode upload (raw LE 32-bit words, one write each, auto-increment). */
	stage = "upload";
	bcma_write32(hw->core, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_AUTO_INC | OB_UCODE_OBJADDR_UCM_SEL);
	(void)bcma_read32(hw->core, OB_UCODE_REG_OBJADDR);	/* vendor readback */
	dev_info(hw->dev, "ucode-test: OBJADDR=30000000\n");
	dev_info(hw->dev, "ucode-test: upload start words=%u\n", words);
	for (i = 0; i < words; i++) {
		bcma_write32(hw->core, OB_UCODE_REG_OBJDATA,
			     ob_fw_le32(ucode->data + (size_t)i * 4));
		written++;
	}
	if (words >= 4) {
		dev_info(hw->dev,
			 "ucode-test: first %08x %08x %08x %08x\n",
			 ob_fw_le32(ucode->data), ob_fw_le32(ucode->data + 4),
			 ob_fw_le32(ucode->data + 8),
			 ob_fw_le32(ucode->data + 12));
		i = words - 4;
		dev_info(hw->dev,
			 "ucode-test: last  %08x %08x %08x %08x\n",
			 ob_fw_le32(ucode->data + (size_t)i * 4),
			 ob_fw_le32(ucode->data + ((size_t)i + 1) * 4),
			 ob_fw_le32(ucode->data + ((size_t)i + 2) * 4),
			 ob_fw_le32(ucode->data + ((size_t)i + 3) * 4));
	}
	dev_info(hw->dev, "ucode-test: upload complete writes=%u\n", written);

	/* 6. write-count invariant: never start PSM after a short upload. */
	if (!ob_ucode_writes_ok(written, words)) {
		dev_err(hw->dev,
			"ucode-test: write count %u != %u; PSM not started\n",
			written, words);
		ret = -EIO;
		goto out;
	}

	/* 4. clear stale interrupt state, then start PSM. */
	stage = "psm-start";
	bcma_write32(hw->core, OB_UCODE_REG_MACINTSTATUS, 0xffffffffu);
	{
		u32 after = ob_ucode_mctrl_update(hw, OB_UCODE_MACCONTROL_MASK,
						  OB_UCODE_MACCONTROL_PSM);

		dev_info(hw->dev, "ucode: PSM start maccontrol=%08x\n", after);
		if ((after & OB_UCODE_PSM_EXPECTED) != OB_UCODE_PSM_EXPECTED ||
		    (after & OB_UCODE_PSM_FORBIDDEN)) {
			dev_err(hw->dev,
				"ucode-test: PSM start maccontrol invalid\n");
			ret = -EIO;
			goto out;
		}
	}
	dev_info(hw->dev, "ucode-test: PSM start\n");

	/* 5. bounded poll for MI_MACSSPNDD (no IRQ, no handler). */
	stage = "psm-poll";
	dev_info(hw->dev,
		 "ucode-test: poll start delay=%uus step=%u max_iter=%u max_total_us=%u\n",
		 OB_UCODE_POLL_DELAY_US, OB_UCODE_POLL_STEP,
		 ob_ucode_poll_max_iterations(OB_UCODE_POLL_TIMEOUT,
					      OB_UCODE_POLL_STEP),
		 ob_ucode_poll_max_iterations(OB_UCODE_POLL_TIMEOUT,
					      OB_UCODE_POLL_STEP) *
		 OB_UCODE_POLL_DELAY_US);
	remaining = OB_UCODE_POLL_TIMEOUT;
	for (;;) {
		status = bcma_read32(hw->core, OB_UCODE_REG_MACINTSTATUS);
		if (ob_ucode_mac_suspended(status))
			break;
		if (ob_ucode_poll_expired(remaining, OB_UCODE_POLL_STEP)) {
			dev_err(hw->dev,
				"ucode-test: PSM poll TIMEOUT iterations=%u status=%08x\n",
				iterations, status);
			ret = -ETIMEDOUT;
			goto out;
		}
		udelay(OB_UCODE_POLL_DELAY_US);
		remaining -= OB_UCODE_POLL_STEP;
		iterations++;
	}
	dev_info(hw->dev, "ucode-test: PSM poll PASS iterations=%u status=%08x\n",
		 iterations, status);

	/*
	 * 7. read-only SHM FIFO-size diagnostic. The vendor reads these after the
	 * common-initvals applier (0x68f88 > 0x68b98); D2A logs them without an
	 * equality test and does not treat them as a pass/fail condition.
	 */
	stage = "shm";
	dev_info(hw->dev,
		 "ucode-test: SHM M_FIFOSIZE0..3=%04x %04x %04x %04x (read-only)\n",
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE0),
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE1),
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE2),
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE3));

	dev_info(hw->dev,
		 "ucode-test: PASS - stopped before initvals/PHY/radio/DMA\n");
	ret = 0;

out:
	if (ret)
		dev_err(hw->dev,
			"ucode-test: FAIL stage=%s ret=%d (no cleanup writes)\n",
			stage, ret);
	release_firmware(ucode);
	return ret;
}

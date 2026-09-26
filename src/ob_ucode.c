// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — D11 rev42 ucode upload + PSM start (M3.4D2A), shared by the
 * isolated ucode_test_only and initvals_test_only (M3.4D2B) paths.
 *
 * The recovered sequence (exact vendor order, see ob_ucode.h for per-step
 * provenance) is implemented once in ob_ucode_run_d2a():
 *   1. minimal core prep: bcma_host_pci_up + D11 core enable/reset + FAST clock
 *   2. pre-upload MACCONTROL  = 0x04000404 (IHR_EN | PSM_JMP0 | WAKE), masked RMW
 *   3. ucode upload via OBJADDR=0x03000000 (auto-inc) + 10850 OBJDATA writes
 *   4. macintstatus = 0xffffffff, then PSM start MACCONTROL = 0x04020402
 *   5. bounded poll of macintstatus & MI_MACSSPNDD (10 us, <=100000 iters)
 *
 * ucode_test_only (M3.4D2A) then reads the SHM FIFO-size diagnostic and STOPS.
 * initvals_test_only (M3.4D2B, ob_initvals.c) then applies the common table and
 * verifies the deterministic postconditions. Both share this file so the
 * proven D2A register sequence cannot silently diverge.
 *
 * Neither path applies bsinitvals, EN_MAC, request_irq, DMA or mac80211. No
 * cleanup register writes are performed on any failure path (see the
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

	/* Device-loss guard: no core/host bring-up after the latch. */
	if (hw->dev_lost)
		return -EIO;

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
	clk = ob_d11_read32(hw, OB_UCODE_REG_CLKCTLST);
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
u16 ob_ucode_read_shm16(struct ob_hw *hw, u16 off)
{
	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	(void)ob_d11_read32(hw, OB_UCODE_REG_OBJADDR);
	return ob_d11_read16(hw, OB_UCODE_REG_OBJDATA + (off & 0x2));
}

/*
 * Vendor SHM 16-bit write (wlc_bmac_write_shm equivalent): select the SHM
 * window in OBJADDR, barrier, then write the 16-bit half selected by offset
 * bit1 into OBJDATA. offset is a byte offset. Reused by the D3B MHF writes.
 */
void ob_ucode_write_shm16(struct ob_hw *hw, u16 off, u16 val)
{
	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	(void)ob_d11_read32(hw, OB_UCODE_REG_OBJADDR);
	ob_d11_write16(hw, OB_UCODE_REG_OBJDATA + (off & 0x2), val);
}

/* Masked MACCONTROL update, mirroring wlc_bmac_mctrl(dev, mask, val). */
static u32 ob_ucode_mctrl_update(struct ob_hw *hw, u32 mask, u32 val)
{
	u32 old = ob_d11_read32(hw, OB_UCODE_REG_MACCONTROL);
	u32 new = (old & ~mask) | val;

	ob_d11_write32(hw, OB_UCODE_REG_MACCONTROL, new);
	return ob_d11_read32(hw, OB_UCODE_REG_MACCONTROL);
}

/*
 * Hardware-proven D2A core, shared by ucode_test_only (M3.4D2A) and
 * initvals_test_only (M3.4D2B). It stops with the PSM paused after auto-init
 * and applies no initvals; @tag only selects the log prefix. The register
 * sequence is identical to the M3.4D2A hardware-proven implementation. No
 * cleanup writes on any failure path.
 */
int ob_ucode_run_d2a(struct ob_hw *hw, const char *tag, struct ob_ucode_run *run)
{
	const struct firmware *ucode = NULL;
	const char *name = NULL;
	const char *stage = "request";
	u32 words = 0, i, written = 0, status = 0, remaining, iterations = 0;
	int ret = 0;

	run->name = NULL;
	run->words = 0;
	run->written = 0;
	run->psm_iterations = 0;
	run->psm_status = 0;

	dev_info(hw->dev, "%s: BEGIN\n", tag);

	/* Acquire + hash + size-validate the exact vendor rev42 ucode first. */
	ret = ob_fw_request_ucode(hw, &ucode, &name);
	if (ret) {
		dev_err(hw->dev, "%s: ucode unavailable/invalid: %d\n", tag,
			ret);
		goto out;
	}
	words = ob_ucode_words_from_size(ucode->size);
	dev_info(hw->dev, "%s: image %s size=%zu words=%u\n",
		 tag, name ? name : "?", ucode->size, words);

	/* 1. minimum core preparation. */
	stage = "core-prep";
	ret = ob_ucode_prepare(hw);
	if (ret)
		goto out;
	dev_info(hw->dev, "%s: core prepared\n", tag);

	/* 2. pre-upload MACCONTROL (masked update, mask = ~0). */
	stage = "maccontrol-upload";
	{
		u32 before = ob_d11_read32(hw, OB_UCODE_REG_MACCONTROL);
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
				"%s: maccontrol upload state invalid\n", tag);
			ret = -EIO;
			goto out;
		}
	}
	dev_info(hw->dev, "%s: MAC upload state ready\n", tag);

	/* 3. ucode upload (raw LE 32-bit words, one write each, auto-increment). */
	stage = "upload";
	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_AUTO_INC | OB_UCODE_OBJADDR_UCM_SEL);
	(void)ob_d11_read32(hw, OB_UCODE_REG_OBJADDR);	/* vendor readback */
	dev_info(hw->dev, "%s: OBJADDR=30000000\n", tag);
	dev_info(hw->dev, "%s: upload start words=%u\n", tag, words);
	for (i = 0; i < words; i++) {
		ob_d11_write32(hw, OB_UCODE_REG_OBJDATA,
			     ob_fw_le32(ucode->data + (size_t)i * 4));
		written++;
	}
	if (words >= 4) {
		dev_info(hw->dev, "%s: first %08x %08x %08x %08x\n", tag,
			 ob_fw_le32(ucode->data), ob_fw_le32(ucode->data + 4),
			 ob_fw_le32(ucode->data + 8),
			 ob_fw_le32(ucode->data + 12));
		i = words - 4;
		dev_info(hw->dev, "%s: last  %08x %08x %08x %08x\n", tag,
			 ob_fw_le32(ucode->data + (size_t)i * 4),
			 ob_fw_le32(ucode->data + ((size_t)i + 1) * 4),
			 ob_fw_le32(ucode->data + ((size_t)i + 2) * 4),
			 ob_fw_le32(ucode->data + ((size_t)i + 3) * 4));
	}
	dev_info(hw->dev, "%s: upload complete writes=%u\n", tag, written);

	/* write-count invariant: never start PSM after a short upload. */
	if (!ob_ucode_writes_ok(written, words)) {
		dev_err(hw->dev,
			"%s: write count %u != %u; PSM not started\n",
			tag, written, words);
		ret = -EIO;
		goto out;
	}

	/* 4. clear stale interrupt state, then start PSM. */
	stage = "psm-start";
	ob_d11_write32(hw, OB_UCODE_REG_MACINTSTATUS, 0xffffffffu);
	{
		u32 after = ob_ucode_mctrl_update(hw, OB_UCODE_MACCONTROL_MASK,
						  OB_UCODE_MACCONTROL_PSM);

		dev_info(hw->dev, "ucode: PSM start maccontrol=%08x\n", after);
		if ((after & OB_UCODE_PSM_EXPECTED) != OB_UCODE_PSM_EXPECTED ||
		    (after & OB_UCODE_PSM_FORBIDDEN)) {
			dev_err(hw->dev,
				"%s: PSM start maccontrol invalid\n", tag);
			ret = -EIO;
			goto out;
		}
	}
	dev_info(hw->dev, "%s: PSM start\n", tag);

	/* 5. bounded poll for MI_MACSSPNDD (no IRQ, no handler). */
	stage = "psm-poll";
	dev_info(hw->dev,
		 "%s: poll start delay=%uus step=%u max_iter=%u max_total_us=%u\n",
		 tag, OB_UCODE_POLL_DELAY_US, OB_UCODE_POLL_STEP,
		 ob_ucode_poll_max_iterations(OB_UCODE_POLL_TIMEOUT,
					      OB_UCODE_POLL_STEP),
		 ob_ucode_poll_max_iterations(OB_UCODE_POLL_TIMEOUT,
					      OB_UCODE_POLL_STEP) *
		 OB_UCODE_POLL_DELAY_US);
	remaining = OB_UCODE_POLL_TIMEOUT;
	for (;;) {
		status = ob_d11_read32(hw, OB_UCODE_REG_MACINTSTATUS);
		if (ob_ucode_mac_suspended(status))
			break;
		if (ob_ucode_poll_expired(remaining, OB_UCODE_POLL_STEP)) {
			dev_err(hw->dev,
				"%s: PSM poll TIMEOUT iterations=%u status=%08x\n",
				tag, iterations, status);
			ret = -ETIMEDOUT;
			goto out;
		}
		udelay(OB_UCODE_POLL_DELAY_US);
		remaining -= OB_UCODE_POLL_STEP;
		iterations++;
	}
	dev_info(hw->dev, "%s: PSM poll PASS iterations=%u status=%08x\n",
		 tag, iterations, status);

	run->name = name;
	run->words = words;
	run->written = written;
	run->psm_iterations = iterations;
	run->psm_status = status;

out:
	if (ret)
		dev_err(hw->dev, "%s: FAIL stage=%s ret=%d (no cleanup writes)\n",
			tag, stage, ret);
	release_firmware(ucode);
	return ret;
}

int ob_ucode_test(struct ob_hw *hw)
{
	struct ob_ucode_run run;
	int ret = ob_ucode_run_d2a(hw, "ucode-test", &run);

	if (ret)
		return ret;

	/*
	 * Read-only SHM FIFO-size diagnostic. The vendor reads these after the
	 * common-initvals applier (0x68f88 > 0x68b98); D2A logs them without an
	 * equality test and does not treat them as a pass/fail condition.
	 */
	dev_info(hw->dev,
		 "ucode-test: SHM M_FIFOSIZE0..3=%04x %04x %04x %04x (read-only)\n",
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE0),
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE1),
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE2),
		 ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE3));

	dev_info(hw->dev,
		 "ucode-test: PASS - stopped before initvals/PHY/radio/DMA\n");
	return 0;
}

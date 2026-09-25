// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — isolated rev42 common-initvals test (M3.4D2B).
 *
 * Module param initvals_test_only=1. After the shared, hardware-proven D2A
 * core (prep -> ucode upload -> PSM start -> MI_MACSSPNDD poll; see
 * ob_ucode_run_d2a) this applies exactly the 610 data records of
 * `brcm/bcm4352-d11ac1initvals42.bin` in strict original order, then reads a
 * small set of provenance-backed postconditions and STOPS.
 *
 * It does NOT run bsinitvals, sub_6656c, wlc_phy_init, PHY/radio/channel,
 * calibration, RX/TX DMA, request_irq, host IRQ routing or mac80211. It never
 * requests an IRQ and never programs a DMA engine. No cleanup register writes
 * are performed on any path; the failure/residual-state matrix is documented in
 * docs/m34d2b_common_initvals.md and docs/milestones.md.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_fw.h"
#include "ob_ucode.h"
#include "ob_initvals.h"

/*
 * Apply one record exactly as the vendor applier does: width 2 -> 16-bit D11
 * write, width 4 -> 32-bit D11 write, target D11 base + offset. No coalescing,
 * no reordering, no byte swap, no re-interpretation.
 */
static void ob_initvals_write(struct ob_hw *hw, const struct ob_fw_iv *rec)
{
	if (rec->width == OB_FW_IV_WIDTH16)
		ob_d11_write16(hw, rec->offset, (u16)rec->value);
	else
		ob_d11_write32(hw, rec->offset, rec->value);
}

/*
 * Shared D2B core (single common-initvals implementation). Runs the proven D2A
 * sequence, applies exactly the 610 records and gates on the deterministic
 * postconditions. @tag is the log prefix ("initvals-test" / "dma-test").
 */
int ob_initvals_run_d2b(struct ob_hw *hw, const char *tag,
			struct ob_ucode_run *run,
			struct ob_initvals_post *post)
{
	const struct firmware *iv = NULL;
	struct ob_initvals_plan plan;
	struct ob_fw_iv rec;
	const char *stage = "iv-request";
	u32 total = 0, w16 = 0, w32 = 0, i;
	int ret;

	/*
	 * 1-5. Shared, hardware-proven D2A core. On success the PSM is paused
	 * after auto-init and MACCONTROL == 0x04020402 (EN_MAC=0, SHM_EN=0).
	 */
	ret = ob_ucode_run_d2a(hw, tag, run);
	if (ret)
		return ret;

	dev_info(hw->dev, "%s: D2A prep complete\n", tag);
	dev_info(hw->dev, "%s: ucode upload complete writes=%u\n", tag,
		 run->written);
	dev_info(hw->dev, "%s: PSM PASS iterations=%u status=%08x\n", tag,
		 run->psm_iterations, run->psm_status);

	/* 6. acquire + validate the common table (size + FNV-1a-64). */
	ret = ob_fw_request_initvals(hw, &iv);
	if (ret) {
		dev_err(hw->dev,
			"%s: common initvals unavailable/invalid: %d\n", tag,
			ret);
		goto out;
	}

	stage = "iv-plan";
	ret = ob_initvals_plan_from_table(iv->data, iv->size, &plan);
	if (ret) {
		dev_err(hw->dev, "%s: common initvals malformed: %d\n", tag,
			ret);
		goto out;
	}
	if (!ob_initvals_plan_ok(&plan)) {
		dev_err(hw->dev,
			"%s: common initvals shape records=%u w16=%u w32=%u (expected %u/%u/%u)\n",
			tag, plan.records, plan.w16, plan.w32,
			OB_INITVALS_RECORDS, OB_INITVALS_W16,
			OB_INITVALS_W32);
		ret = -EINVAL;
		goto out;
	}
	dev_info(hw->dev, "%s: common initvals begin records=%u\n", tag,
		 plan.records);

	/*
	 * 7. apply exactly records 0..609 in strict original order. The plan
	 * excludes the terminator; the defensive offset check guarantees it is
	 * never written even if a record decodes as 0xffff.
	 */
	stage = "iv-apply";
	for (i = 0; i < plan.records; i++) {
		if (ob_fw_iv_at(iv->data, iv->size, i, &rec)) {
			ret = -EINVAL;
			goto out;
		}
		if (rec.offset == OB_FW_IV_TERMINATOR ||
		    (rec.width != OB_FW_IV_WIDTH16 &&
		     rec.width != OB_FW_IV_WIDTH32)) {
			ret = -EINVAL;
			goto out;
		}
		ob_initvals_write(hw, &rec);
		if (rec.width == OB_FW_IV_WIDTH16)
			w16++;
		else
			w32++;
		total++;
	}
	if (!ob_initvals_counts_ok(total, w16, w32)) {
		dev_err(hw->dev,
			"%s: write count total=%u w16=%u w32=%u (expected %u/%u/%u)\n",
			tag, total, w16, w32, OB_INITVALS_RECORDS,
			OB_INITVALS_W16, OB_INITVALS_W32);
		ret = -EIO;
		goto out;
	}
	dev_info(hw->dev,
		 "%s: common initvals complete total=%u w16=%u w32=%u\n", tag,
		 total, w16, w32);

	/* 8. read-only deterministic postconditions. */
	stage = "post-read";
	post->fifosize0 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE0);
	post->fifosize1 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE1);
	post->fifosize2 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE2);
	post->fifosize3 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE3);
	post->macintmask = ob_d11_read32(hw, OB_D11_REG_MACINTMASK);
	post->maccontrol = ob_d11_read32(hw, OB_UCODE_REG_MACCONTROL);
	post->shm14 = (u32)ob_ucode_read_shm16(hw, OB_INITVALS_SHM14_LO) |
		      ((u32)ob_ucode_read_shm16(hw, OB_INITVALS_SHM14_HI)
		       << 16);

	dev_info(hw->dev, "%s: M_FIFOSIZE0=%04x\n", tag, post->fifosize0);
	dev_info(hw->dev, "%s: M_FIFOSIZE1=%04x\n", tag, post->fifosize1);
	dev_info(hw->dev, "%s: M_FIFOSIZE2=%04x\n", tag, post->fifosize2);
	dev_info(hw->dev, "%s: M_FIFOSIZE3=%04x\n", tag, post->fifosize3);
	dev_info(hw->dev, "%s: MACINTMASK=%08x\n", tag, post->macintmask);
	dev_info(hw->dev, "%s: MACCONTROL=%08x\n", tag, post->maccontrol);
	dev_info(hw->dev, "%s: SHM[0014]=%08x\n", tag, post->shm14);

	if (!ob_initvals_post_ok(post)) {
		dev_err(hw->dev,
			"%s: FIFO mismatch got/exp %04x/%04x %04x/%04x %04x/%04x %04x/%04x\n",
			tag, post->fifosize0, OB_INITVALS_FIFOSIZE0_EXPECTED,
			post->fifosize1, OB_INITVALS_FIFOSIZE1_EXPECTED,
			post->fifosize2, OB_INITVALS_FIFOSIZE2_EXPECTED,
			post->fifosize3, OB_INITVALS_FIFOSIZE3_EXPECTED);
		dev_err(hw->dev,
			"%s: reg mismatch got/exp MACINTMASK=%08x/%08x MACCONTROL=%08x/%08x SHM14=%08x/%08x\n",
			tag, post->macintmask, OB_INITVALS_MACINTMASK_EXPECTED,
			post->maccontrol, OB_INITVALS_MACCONTROL_EXPECTED,
			post->shm14, OB_INITVALS_SHM14_EXPECTED);
		ret = -EIO;
		goto out;
	}

	ret = 0;
out:
	if (ret)
		dev_err(hw->dev,
			"%s: D2B FAIL stage=%s ret=%d (no cleanup writes)\n",
			tag, stage, ret);
	release_firmware(iv);
	return ret;
}

int ob_initvals_test(struct ob_hw *hw)
{
	struct ob_ucode_run run;
	struct ob_initvals_post post;
	int ret;

	ret = ob_initvals_run_d2b(hw, "initvals-test", &run, &post);
	if (ret)
		return ret;

	dev_info(hw->dev,
		 "initvals-test: PASS - stopped before bsinitvals/PHY/radio/channel/DMA\n");
	return 0;
}

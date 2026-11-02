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
#include <linux/string.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_fw.h"
#include "ob_fw_chain.h"
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
/*
 * Apply exactly the 610 common-initvals records from an already-established
 * D2A exit state (PSM paused, MACCONTROL == 0x04020402). This is the second
 * half of the D2B core, factored out so the staged rev42 diagnostic can bracket
 * the ucode stage (CP1) and the common-initvals stage (CP2) separately. It
 * performs NO D2A work and releases its own firmware image.
 */
static int ob_initvals_apply_common_internal(struct ob_hw *hw,
					      const char *tag,
					      struct ob_initvals_post *post,
					      struct ob_initvals_run *iv_run)
{
	const struct firmware *iv = NULL;
	struct ob_initvals_plan plan;
	struct ob_fw_iv rec;
	const char *stage = "iv-request";
	u32 total = 0, w16 = 0, w32 = 0, i;
	int ret = 0;

	if (iv_run) {
		iv_run->stage = "iv-request";
		iv_run->image_name = OB_FW_AC1INITVALS42_NAME;
		iv_run->records = 0;
		iv_run->w16 = 0;
		iv_run->w32 = 0;
		iv_run->ret = 0;
	}

	/* 6. acquire + validate the common table (size + FNV-1a-64). */
	ret = ob_fw_request_initvals(hw, &iv);
	if (ret) {
		dev_err(hw->dev,
			"%s: common initvals unavailable/invalid: %d\n", tag,
			ret);
		goto out;
	}

	stage = "iv-plan";
	if (iv_run)
		iv_run->stage = stage;
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
	if (iv_run)
		iv_run->stage = stage;
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
	if (iv_run)
		iv_run->stage = stage;
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

	stage = "post-check";
	if (iv_run)
		iv_run->stage = stage;
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
	if (iv_run) {
		iv_run->records = total;
		iv_run->w16 = w16;
		iv_run->w32 = w32;
		iv_run->ret = ret;
	}
	if (ret)
		dev_err(hw->dev,
			"%s: D2B FAIL stage=%s ret=%d (no cleanup writes)\n",
			tag, stage, ret);
	release_firmware(iv);
	return ret;
}

int ob_initvals_apply_common(struct ob_hw *hw, const char *tag,
			     struct ob_initvals_post *post)
{
	return ob_initvals_apply_common_internal(hw, tag, post, NULL);
}

int ob_initvals_apply_common_ex(struct ob_hw *hw, const char *tag,
				struct ob_initvals_post *post,
				struct ob_initvals_run *run)
{
	return ob_initvals_apply_common_internal(hw, tag, post, run);
}

struct ob_d2b_chain_ctx {
	struct ob_hw *hw;
	const char *tag;
	struct ob_ucode_run *run;
	struct ob_initvals_post *post;
};

static enum ob_fw_chain_stage ob_d2b_d2a_stage(const char *token)
{
	if (!token)
		return OB_FW_CHAIN_UCODE_PREP;
	if (!strcmp(token, "admission") || !strcmp(token, "firmware"))
		return OB_FW_CHAIN_UCODE_REQUEST;
	if (!strcmp(token, "core-prep"))
		return OB_FW_CHAIN_UCODE_PREP;
	if (!strcmp(token, "maccontrol-upload") ||
	    !strcmp(token, "objaddr") || !strcmp(token, "upload"))
		return OB_FW_CHAIN_UCODE_UPLOAD;
	if (!strcmp(token, "psm-start"))
		return OB_FW_CHAIN_PSM_START;
	if (!strcmp(token, "psm-poll"))
		return OB_FW_CHAIN_PSM_POLL;
	if (!strcmp(token, "done"))
		return OB_FW_CHAIN_DONE;
	return OB_FW_CHAIN_UCODE_PREP;
}

static enum ob_fw_chain_stage ob_d2b_common_stage(const char *token)
{
	if (!token)
		return OB_FW_CHAIN_COMMON_APPLY;
	if (!strcmp(token, "iv-request") || !strcmp(token, "common-request"))
		return OB_FW_CHAIN_COMMON_REQUEST;
	if (!strcmp(token, "iv-plan") || !strcmp(token, "common-plan"))
		return OB_FW_CHAIN_COMMON_PLAN;
	if (!strcmp(token, "iv-apply") || !strcmp(token, "common-apply"))
		return OB_FW_CHAIN_COMMON_APPLY;
	if (!strcmp(token, "post-read"))
		return OB_FW_CHAIN_POST_READ;
	if (!strcmp(token, "post-check") || !strcmp(token, "done"))
		return OB_FW_CHAIN_POST_CHECK;
	return OB_FW_CHAIN_COMMON_APPLY;
}

static int ob_d2b_chain_identity(void *ctx, bool runtime_required)
{
	struct ob_d2b_chain_ctx *c = ctx;

	return ob_fw_chain_identity_gate(c->hw, runtime_required);
}

static int ob_d2b_chain_ucode(void *ctx, struct ob_fw_chain_ucode_result *out)
{
	struct ob_d2b_chain_ctx *c = ctx;
	int ret;

	ret = ob_ucode_run_d2a(c->hw, c->tag, c->run);
	out->name = c->run->name;
	out->words = c->run->words;
	out->psm_polls = c->run->psm_iterations;
	out->mutated = c->run->mutated;
	out->ret = ret;
	out->stage = ret ? ob_d2b_d2a_stage(c->run->stage) :
			   OB_FW_CHAIN_DONE;
	if (!ret) {
		const char *tag = c->tag ? c->tag : "d2b";

		dev_info(c->hw->dev, "%s: D2A prep complete\n", tag);
		dev_info(c->hw->dev,
			 "%s: ucode upload complete words=%u issued=%u confirmed=%u (no ucode-RAM readback exists)\n",
			 tag, c->run->words, c->run->issued, c->run->confirmed);
		dev_info(c->hw->dev,
			 "%s: PSM PASS iterations=%u status=%08x\n", tag,
			 c->run->psm_iterations, c->run->psm_status);
	}
	return ret;
}

static int ob_d2b_chain_common(void *ctx,
			       struct ob_fw_chain_common_result *out)
{
	struct ob_d2b_chain_ctx *c = ctx;
	struct ob_initvals_run iv;
	int ret;

	memset(&iv, 0, sizeof(iv));
	ret = ob_initvals_apply_common_ex(c->hw, c->tag, c->post, &iv);
	out->name = iv.image_name;
	out->records = iv.records;
	out->w16 = iv.w16;
	out->w32 = iv.w32;
	out->ret = ret;
	out->stage = ret ? ob_d2b_common_stage(iv.stage) :
			   OB_FW_CHAIN_DONE;
	return ret;
}

static bool ob_d2b_chain_dev_lost(void *ctx)
{
	struct ob_d2b_chain_ctx *c = ctx;

	return !c || !c->hw || c->hw->dev_lost;
}

/*
 * Shared D2B core.  The D2A -> common ordering is owned by the same pure chain
 * contract as the normal path; the isolated policy is profile-only (runtime
 * PHY/radio identity is not required for D2B/D3A0/D3B) and publishes no normal
 * lifecycle state.
 */
int ob_initvals_run_d2b(struct ob_hw *hw, const char *tag,
			struct ob_ucode_run *run,
			struct ob_initvals_post *post)
{
	const struct ob_fw_chain_policy policy = {
		.runtime_identity_required = false,
		.publish_lifecycle = false,
	};
	struct ob_d2b_chain_ctx c;
	struct ob_fw_chain_ops ops;
	struct ob_fw_chain_result result;
	int ret;

	if (!hw || !run || !post)
		return -EINVAL;
	memset(run, 0, sizeof(*run));
	memset(post, 0, sizeof(*post));
	c.hw = hw;
	c.tag = tag;
	c.run = run;
	c.post = post;
	memset(&ops, 0, sizeof(ops));
	ops.ctx = &c;
	ops.identity = ob_d2b_chain_identity;
	ops.ucode = ob_d2b_chain_ucode;
	ops.common = ob_d2b_chain_common;
	ops.dev_lost = ob_d2b_chain_dev_lost;

	ret = ob_fw_chain_core(&ops, &policy, &result);
	if (ret)
		return ret;
	return 0;
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

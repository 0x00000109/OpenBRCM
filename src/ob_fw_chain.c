// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — W2 firmware-chain kernel adapter.
 *
 * Single orchestration point for:
 *   identity gate -> D2A -> common initvals -> lifecycle -> POST_UCODE_READY
 *
 * The actual D2A and common-initvals work stays in their existing owners
 * (ob_ucode_run_d2a, ob_initvals_apply_common_ex).  This file only sequences
 * them, maps their stage tokens into one result and publishes retention.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "ob_core.h"
#include "ob_fw.h"
#include "ob_fw_chain.h"
#include "ob_initvals.h"
#include "ob_lifecycle.h"
#include "ob_profile.h"
#include "ob_ucode.h"

struct ob_fw_chain_ctx {
	struct ob_hw *hw;
	const char *tag;
};

static const char *ob_fw_chain_tag(const struct ob_fw_chain_ctx *c)
{
	return c && c->tag ? c->tag : "fw-chain";
}

static void ob_fw_chain_identity_fill(struct ob_hw *hw,
				      struct ob_fw_chain_identity_view *v)
{
	const struct ob_chip_profile *p;

	memset(v, 0, sizeof(*v));
	if (!hw)
		return;

	p = hw->profile;
	if (p) {
		v->profile_present = true;
		v->profile_enabled = p->enabled;
		v->profile_phy_known = p->phy.known;
		v->profile_radio_known = p->radio.known;
		v->profile_chip_id = p->chip_id;
		v->profile_chip_rev = p->chip_revision;
		v->profile_d11_manuf = p->d11_manufacturer;
		v->profile_d11_core_id = p->d11_core_id;
		v->profile_d11_rev = p->d11_revision;
		v->profile_d11_class = p->d11_class;
		v->profile_phy_type = p->phy.type;
		v->profile_phy_rev = p->phy.revision;
		v->profile_radio_id = p->radio.id;
		v->profile_radio_rev = p->radio.revision;
		v->profile_radio_class = p->radio.revision_class;
	}

	if (hw->core) {
		v->chip_id = hw->chip_id;
		v->chip_rev = hw->chip_rev;
		v->d11_manuf = hw->core->id.manuf;
		v->d11_core_id = hw->core->id.id;
		v->d11_rev = hw->core->id.rev;
		v->d11_class = hw->core->id.class;
	}
	v->runtime_valid = hw->phy_radio_identity_valid;
	v->phy_type = hw->phy_type;
	v->phy_rev = hw->phy_revision;
	v->radio_id = hw->radio_id;
	v->radio_rev = hw->radio_revision;
	v->radio_class = hw->radio_revision_class;
}

int ob_fw_chain_identity_gate(struct ob_hw *hw, bool runtime_required)
{
	struct ob_fw_chain_identity_view v;

	if (!hw)
		return -EINVAL;
	if (hw->dev_lost)
		return -EIO;

	ob_fw_chain_identity_fill(hw, &v);
	if (!ob_fw_chain_identity_ok(&v, runtime_required)) {
		dev_err(hw->dev,
			"fw-chain: identity REJECT scope=%s profile=%d enabled=%d phy_known=%d radio_known=%d runtime_valid=%d chip=%04x/%04x d11=%04x:%04x:%02x/%02x phy=%04x:%02x radio=%04x:%02x:%02x; no firmware request, no MMIO\n",
			runtime_required ? "runtime" : "profile",
			v.profile_present ? 1 : 0, v.profile_enabled ? 1 : 0,
			v.profile_phy_known ? 1 : 0,
			v.profile_radio_known ? 1 : 0,
			v.runtime_valid ? 1 : 0,
			v.chip_id, v.profile_chip_id,
			v.d11_manuf, v.d11_core_id, v.d11_rev, v.d11_class,
			v.phy_type, v.phy_rev, v.radio_id, v.radio_rev,
			v.radio_class);
		return -ENODEV;
	}

	dev_info(hw->dev,
		 "fw-chain: identity PASS scope=%s chip=%04x d11=%04x:%04x phy=%04x:%02x radio=%04x:%02x:%02x\n",
		 runtime_required ? "runtime" : "profile",
		 v.chip_id, v.d11_manuf, v.d11_core_id, v.phy_type,
		 v.phy_rev, v.radio_id, v.radio_rev, v.radio_class);
	return 0;
}

static enum ob_fw_chain_stage
ob_fw_chain_stage_from_d2a(const char *token)
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

static enum ob_fw_chain_stage
ob_fw_chain_stage_from_common(const char *token)
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

static int ob_fw_chain_op_identity(void *ctx, bool runtime_required)
{
	struct ob_fw_chain_ctx *c = ctx;

	return ob_fw_chain_identity_gate(c->hw, runtime_required);
}

static int ob_fw_chain_op_ucode(void *ctx,
				struct ob_fw_chain_ucode_result *out)
{
	struct ob_fw_chain_ctx *c = ctx;
	struct ob_ucode_run run;
	int ret;

	memset(&run, 0, sizeof(run));
	ret = ob_ucode_run_d2a(c->hw, ob_fw_chain_tag(c), &run);
	out->name = run.name;
	out->words = run.words;
	out->psm_polls = run.psm_iterations;
	out->mutated = run.mutated;
	out->ret = ret;
	out->stage = ret ? ob_fw_chain_stage_from_d2a(run.stage) :
			   OB_FW_CHAIN_DONE;
	return ret;
}

static int ob_fw_chain_op_common(void *ctx,
				 struct ob_fw_chain_common_result *out)
{
	struct ob_fw_chain_ctx *c = ctx;
	struct ob_initvals_run iv;
	struct ob_initvals_post post;
	int ret;

	memset(&iv, 0, sizeof(iv));
	memset(&post, 0, sizeof(post));
	ret = ob_initvals_apply_common_ex(c->hw, ob_fw_chain_tag(c), &post,
					  &iv);
	out->name = iv.image_name;
	out->records = iv.records;
	out->w16 = iv.w16;
	out->w32 = iv.w32;
	out->ret = ret;
	out->stage = ret ? ob_fw_chain_stage_from_common(iv.stage) :
			   OB_FW_CHAIN_DONE;
	return ret;
}

static int ob_fw_chain_op_publish_ucode(void *ctx)
{
	struct ob_fw_chain_ctx *c = ctx;
	enum ob_d11_lifecycle_state s;

	if (c->hw->dev_lost)
		return -EIO;
	/*
	 * All-or-nothing publication: validate the whole path before the first
	 * state write, so a failed stage cannot leave a half-published sequence.
	 */
	s = c->hw->d11_state;
	if (!ob_d11_lifecycle_step_allowed(s, OB_D11_UCODE_LOADED))
		return -EIO;
	s = OB_D11_UCODE_LOADED;
	if (!ob_d11_lifecycle_step_allowed(s, OB_D11_PSM_STARTED))
		return -EIO;
	s = OB_D11_PSM_STARTED;
	if (!ob_d11_lifecycle_step_allowed(s,
					   OB_D11_STARTUP_SUSPEND_CONFIRMED))
		return -EIO;
	c->hw->d11_state = OB_D11_STARTUP_SUSPEND_CONFIRMED;
	return 0;
}

static int ob_fw_chain_op_publish_common(void *ctx)
{
	struct ob_fw_chain_ctx *c = ctx;
	enum ob_d11_lifecycle_state s;

	if (c->hw->dev_lost)
		return -EIO;
	s = c->hw->d11_state;
	if (!ob_d11_lifecycle_step_allowed(s,
					   OB_D11_COMMON_INITVALS_APPLIED))
		return -EIO;
	s = OB_D11_COMMON_INITVALS_APPLIED;
	if (!ob_d11_lifecycle_step_allowed(s, OB_D11_POST_UCODE_READY))
		return -EIO;
	c->hw->d11_state = OB_D11_POST_UCODE_READY;
	return 0;
}

static bool ob_fw_chain_op_dev_lost(void *ctx)
{
	struct ob_fw_chain_ctx *c = ctx;

	return !c || !c->hw || c->hw->dev_lost;
}

int ob_fw_chain_run(struct ob_hw *hw, const char *tag,
		    const struct ob_fw_chain_policy *policy,
		    struct ob_fw_chain_result *result)
{
	struct ob_fw_chain_ctx c;
	struct ob_fw_chain_ops ops;
	int ret;

	if (!hw || !policy || !result)
		return -EINVAL;

	/*
	 * Never re-enter after a retained failure: a second upload is exactly the
	 * recovery this design forbids.  The latch is preserved, not cleared.
	 */
	if (hw->ucode_retain) {
		ob_fw_chain_result_reset(result);
		result->stage = OB_FW_CHAIN_IDENTITY;
		result->stage_name = ob_fw_chain_stage_name(result->stage);
		result->ret = -EBUSY;
		result->retained = true;
		dev_err(hw->dev,
			"fw-chain: refusing re-entry after retained failure stage=%s; no MMIO, no second upload\n",
			hw->ucode_class ? hw->ucode_class : "UNKNOWN");
		return -EBUSY;
	}

	ob_fw_chain_result_reset(result);
	c.hw = hw;
	c.tag = tag;
	memset(&ops, 0, sizeof(ops));
	ops.ctx = &c;
	ops.identity = ob_fw_chain_op_identity;
	ops.ucode = ob_fw_chain_op_ucode;
	ops.publish_ucode = ob_fw_chain_op_publish_ucode;
	ops.common = ob_fw_chain_op_common;
	ops.publish_common = ob_fw_chain_op_publish_common;
	ops.dev_lost = ob_fw_chain_op_dev_lost;

	ret = ob_fw_chain_core(&ops, policy, result);
	if (result->ret != ret)
		result->ret = ret;

	/*
	 * Publish the contract verdict.  A retained failure is terminal for this
	 * probe: no cleanup, no reset, no retry and no second upload.
	 */
	hw->ucode_retain = result->retained;
	if (result->mutated) {
		hw->ucode_mutated = true;
		hw->ucode_class = result->stage_name;
	}
	if (result->retained)
		dev_emerg(hw->dev,
			  "fw-chain: RETAINED stage=%s image=%s mutated=%d; device must stay bound, no teardown/reboot to recover\n",
			  result->stage_name,
			  result->image_name ? result->image_name : "none",
			  result->mutated ? 1 : 0);

	if (!ret)
		dev_info(hw->dev,
			 "fw-chain: PASS stage=%s image=%s words=%u records=%u psm_polls=%u runtime_identity=%d\n",
			 result->stage_name,
			 result->image_name ? result->image_name : "none",
			 result->words, result->records, result->psm_polls,
			 result->runtime_identity ? 1 : 0);
	else
		dev_err(hw->dev,
			"fw-chain: FAIL stage=%s image=%s words=%u records=%u psm_polls=%u ret=%d mutated=%d retained=%d (no cleanup writes)\n",
			result->stage_name,
			result->image_name ? result->image_name : "none",
			result->words, result->records, result->psm_polls,
			ret, result->mutated ? 1 : 0,
			result->retained ? 1 : 0);

	return ret;
}

int ob_fw_chain_test(struct ob_hw *hw)
{
	const struct ob_fw_chain_policy policy = {
		.runtime_identity_required = true,
		.publish_lifecycle = true,
	};
	struct ob_fw_chain_result result;
	int ret;

	if (!hw)
		return -EINVAL;
	if (hw->dev_lost)
		return -EIO;
	if (hw->d11_state != OB_D11_ATTACH_IDENTITY_READY)
		return -EINVAL;

	ret = ob_fw_chain_run(hw, "fw-chain-test", &policy, &result);
	if (!ret)
		dev_info(hw->dev,
			 "fw-chain-test: PASS at POST_UCODE_READY; stopped before PHY/radio/DMA/IRQ/mac80211\n");
	return ret;
}

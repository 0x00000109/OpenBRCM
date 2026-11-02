/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — W2 firmware-chain orchestration contract.
 *
 * This header is the single owner of the D2A -> common-initvals -> lifecycle
 * ordering.  It contains no MMIO, no firmware API and no kernel object.  The
 * kernel adapter (ob_fw_chain.c) supplies one callback for each stage; host
 * tests execute this exact state machine with fake callbacks.
 *
 * Provenance: docs/W2_FIRMWARE_CHAIN_DESIGN.md; vendor wlc_bmac_init
 * (0x6828a), FUN_001607b0 (0x607b0), FUN_00160744 (0x60744),
 * wlc_bmac_wowlucode_start (0x63828), FUN_00160f67 (0x60f67).
 */
#ifndef _OB_FW_CHAIN_CONTRACT_H_
#define _OB_FW_CHAIN_CONTRACT_H_

#include <linux/types.h>

enum ob_fw_chain_stage {
	OB_FW_CHAIN_IDENTITY = 0,
	OB_FW_CHAIN_UCODE_REQUEST,
	OB_FW_CHAIN_UCODE_PREP,
	OB_FW_CHAIN_UCODE_UPLOAD,
	OB_FW_CHAIN_PSM_START,
	OB_FW_CHAIN_PSM_POLL,
	OB_FW_CHAIN_COMMON_REQUEST,
	OB_FW_CHAIN_COMMON_PLAN,
	OB_FW_CHAIN_COMMON_APPLY,
	OB_FW_CHAIN_POST_READ,
	OB_FW_CHAIN_POST_CHECK,
	OB_FW_CHAIN_LIFECYCLE,
	OB_FW_CHAIN_DONE,
};

struct ob_fw_chain_result {
	enum ob_fw_chain_stage stage;
	const char *stage_name;	/* stable token; never a temporary buffer */
	const char *image_name;	/* last image touched, or NULL */
	u32 words;		/* ucode words; 10850 on the valid path */
	u32 records;		/* common records applied; 610 on success */
	u32 psm_polls;		/* D2A poll iterations */
	int ret;		/* 0 only at OB_FW_CHAIN_DONE */
	bool mutated;		/* any hardware mutation may have occurred */
	bool retained;		/* mutated && failure; keep the device bound */
	bool runtime_identity;	/* runtime PHY/radio gate was required */
};

struct ob_fw_chain_policy {
	bool runtime_identity_required;
	bool publish_lifecycle;
};

/*
 * Identity view.  The profile fields are required for every policy.  The
 * runtime fields are consulted only when runtime_identity_required is true.
 */
struct ob_fw_chain_identity_view {
	bool profile_present;
	bool profile_enabled;
	bool profile_phy_known;
	bool profile_radio_known;
	bool runtime_valid;

	u16 chip_id;
	u8 chip_rev;
	u16 d11_manuf;
	u16 d11_core_id;
	u8 d11_rev;
	u8 d11_class;

	u16 profile_chip_id;
	u8 profile_chip_rev;
	u16 profile_d11_manuf;
	u16 profile_d11_core_id;
	u8 profile_d11_rev;
	u8 profile_d11_class;

	u16 phy_type;
	u8 phy_rev;
	u16 radio_id;
	u8 radio_rev;
	u8 radio_class;

	u16 profile_phy_type;
	u8 profile_phy_rev;
	u16 profile_radio_id;
	u8 profile_radio_rev;
	u8 profile_radio_class;
};

static inline const char *ob_fw_chain_stage_name(enum ob_fw_chain_stage s)
{
	switch (s) {
	case OB_FW_CHAIN_IDENTITY:
		return "identity";
	case OB_FW_CHAIN_UCODE_REQUEST:
		return "ucode-request";
	case OB_FW_CHAIN_UCODE_PREP:
		return "ucode-prep";
	case OB_FW_CHAIN_UCODE_UPLOAD:
		return "ucode-upload";
	case OB_FW_CHAIN_PSM_START:
		return "psm-start";
	case OB_FW_CHAIN_PSM_POLL:
		return "psm-poll";
	case OB_FW_CHAIN_COMMON_REQUEST:
		return "common-request";
	case OB_FW_CHAIN_COMMON_PLAN:
		return "common-plan";
	case OB_FW_CHAIN_COMMON_APPLY:
		return "common-apply";
	case OB_FW_CHAIN_POST_READ:
		return "post-read";
	case OB_FW_CHAIN_POST_CHECK:
		return "post-check";
	case OB_FW_CHAIN_LIFECYCLE:
		return "lifecycle";
	case OB_FW_CHAIN_DONE:
		return "done";
	}
	return "unknown";
}

/*
 * Profile identity is mandatory.  Runtime identity is mandatory only when the
 * caller selected the normal/test policy.  An isolated D2B caller passes
 * runtime_required=false and must report that limitation in its result.
 */
static inline bool
ob_fw_chain_identity_ok(const struct ob_fw_chain_identity_view *v,
			bool runtime_required)
{
	if (!v || !v->profile_present || !v->profile_enabled ||
	    !v->profile_phy_known || !v->profile_radio_known)
		return false;
	if (v->chip_id != v->profile_chip_id ||
	    v->chip_rev != v->profile_chip_rev)
		return false;
	if (v->d11_manuf != v->profile_d11_manuf ||
	    v->d11_core_id != v->profile_d11_core_id ||
	    v->d11_rev != v->profile_d11_rev ||
	    v->d11_class != v->profile_d11_class)
		return false;
	if (!runtime_required)
		return true;
	if (!v->runtime_valid)
		return false;
	return v->phy_type == v->profile_phy_type &&
	       v->phy_rev == v->profile_phy_rev &&
	       v->radio_id == v->profile_radio_id &&
	       v->radio_rev == v->profile_radio_rev &&
	       v->radio_class == v->profile_radio_class;
}

struct ob_fw_chain_ucode_result {
	const char *name;
	u32 words;
	u32 psm_polls;
	bool mutated;
	enum ob_fw_chain_stage stage;	/* exact failing stage, or DONE */
	int ret;
};

struct ob_fw_chain_common_result {
	const char *name;
	u32 records;
	u32 w16;
	u32 w32;
	enum ob_fw_chain_stage stage;
	int ret;
};

/*
 * One callback per boundary.  The identity callback receives the policy bit so
 * the adapter can select profile-only vs profile+runtime without a second
 * policy copy.  publish_* are called only when policy.publish_lifecycle is
 * true; they advance one or more production lifecycle states and must be
 * all-or-nothing (the adapter publishes the D2A block and the common block).
 */
struct ob_fw_chain_ops {
	void *ctx;
	int (*identity)(void *ctx, bool runtime_required);
	int (*ucode)(void *ctx, struct ob_fw_chain_ucode_result *out);
	int (*publish_ucode)(void *ctx);
	int (*common)(void *ctx, struct ob_fw_chain_common_result *out);
	int (*publish_common)(void *ctx);
	bool (*dev_lost)(void *ctx);
};

static inline void ob_fw_chain_result_reset(struct ob_fw_chain_result *r)
{
	r->stage = OB_FW_CHAIN_IDENTITY;
	r->stage_name = ob_fw_chain_stage_name(r->stage);
	r->image_name = (const char *)0;
	r->words = 0;
	r->records = 0;
	r->psm_polls = 0;
	r->ret = 0;
	r->mutated = false;
	r->retained = false;
	r->runtime_identity = false;
}

static inline int
ob_fw_chain_fail(struct ob_fw_chain_result *r, enum ob_fw_chain_stage stage,
		 int ret, bool mutated)
{
	r->stage = stage;
	r->stage_name = ob_fw_chain_stage_name(stage);
	r->ret = ret;
	r->mutated = mutated;
	r->retained = mutated;
	return ret;
}

/*
 * Execute exactly one identity -> D2A -> common -> lifecycle chain.
 *
 * Failure rules:
 *   - identity failure: no ucode/common callback, never retained;
 *   - D2A failure: no common callback; retained iff the D2A result says a
 *     hardware mutation may have occurred;
 *   - common failure: retained (D2A already succeeded);
 *   - lifecycle failure: retained;
 *   - success: stage DONE, ret 0, retained false.
 */
static inline int
ob_fw_chain_core(const struct ob_fw_chain_ops *ops,
		 const struct ob_fw_chain_policy *policy,
		 struct ob_fw_chain_result *r)
{
	struct ob_fw_chain_ucode_result u;
	struct ob_fw_chain_common_result c;
	int ret;

	if (!ops || !policy || !r || !ops->identity || !ops->ucode ||
	    !ops->common || !ops->dev_lost)
		return -22;
	if (policy->publish_lifecycle &&
	    (!ops->publish_ucode || !ops->publish_common))
		return -22;

	ob_fw_chain_result_reset(r);
	r->runtime_identity = policy->runtime_identity_required;

	/*
	 * A pre-existing device-loss latch is not a fresh chain failure.  It is
	 * still retained: no MMIO may follow and the device must stay bound.
	 */
	if (ops->dev_lost(ops->ctx)) {
		r->stage = OB_FW_CHAIN_IDENTITY;
		r->stage_name = ob_fw_chain_stage_name(r->stage);
		r->ret = -5;
		r->mutated = false;
		r->retained = true;
		return -5;
	}

	ret = ops->identity(ops->ctx, policy->runtime_identity_required);
	if (ret) {
		r->stage = OB_FW_CHAIN_IDENTITY;
		r->stage_name = ob_fw_chain_stage_name(r->stage);
		r->ret = ret;
		r->mutated = false;
		r->retained = false;
		return ret;
	}

	u.name = (const char *)0;
	u.words = 0;
	u.psm_polls = 0;
	u.mutated = false;
	u.stage = OB_FW_CHAIN_UCODE_PREP;
	u.ret = 0;
	r->stage = OB_FW_CHAIN_UCODE_REQUEST;
	r->stage_name = ob_fw_chain_stage_name(r->stage);
	ret = ops->ucode(ops->ctx, &u);
	r->image_name = u.name;
	r->words = u.words;
	r->psm_polls = u.psm_polls;
	if (ret) {
		enum ob_fw_chain_stage stage = u.stage;

		if (stage == OB_FW_CHAIN_DONE || stage == OB_FW_CHAIN_IDENTITY)
			stage = OB_FW_CHAIN_UCODE_PREP;
		return ob_fw_chain_fail(r, stage,
					u.ret ? u.ret : ret, u.mutated);
	}

	/* A successful D2A has issued hardware mutations. */
	r->mutated = true;
	if (policy->publish_lifecycle) {
		r->stage = OB_FW_CHAIN_LIFECYCLE;
		r->stage_name = ob_fw_chain_stage_name(r->stage);
		ret = ops->publish_ucode(ops->ctx);
		if (ret)
			return ob_fw_chain_fail(r, OB_FW_CHAIN_LIFECYCLE, ret,
						 true);
	}

	c.name = (const char *)0;
	c.records = 0;
	c.w16 = 0;
	c.w32 = 0;
	c.stage = OB_FW_CHAIN_COMMON_REQUEST;
	c.ret = 0;
	r->stage = OB_FW_CHAIN_COMMON_REQUEST;
	r->stage_name = ob_fw_chain_stage_name(r->stage);
	ret = ops->common(ops->ctx, &c);
	if (c.name)
		r->image_name = c.name;
	r->records = c.records;
	if (ret) {
		enum ob_fw_chain_stage stage = c.stage;

		if (stage == OB_FW_CHAIN_DONE || stage == OB_FW_CHAIN_IDENTITY)
			stage = OB_FW_CHAIN_COMMON_APPLY;
		return ob_fw_chain_fail(r, stage,
					c.ret ? c.ret : ret, true);
	}

	if (policy->publish_lifecycle) {
		r->stage = OB_FW_CHAIN_LIFECYCLE;
		r->stage_name = ob_fw_chain_stage_name(r->stage);
		ret = ops->publish_common(ops->ctx);
		if (ret)
			return ob_fw_chain_fail(r, OB_FW_CHAIN_LIFECYCLE, ret,
						 true);
	}

	r->stage = OB_FW_CHAIN_DONE;
	r->stage_name = ob_fw_chain_stage_name(r->stage);
	r->ret = 0;
	r->retained = false;
	return 0;
}

#endif /* _OB_FW_CHAIN_CONTRACT_H_ */

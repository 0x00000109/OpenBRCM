/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RADIO_POSTBMAC_H_
#define _OB_RADIO_POSTBMAC_H_

#include <linux/errno.h>
#include <linux/types.h>
#include "ob_phy.h"

enum ob_radio_postbmac_marker {
	OB_RADIO_POSTBMAC_MARK_CP0_ENTRY,
	OB_RADIO_POSTBMAC_MARK_CP1_PHY1B0_RMW_DONE,
	OB_RADIO_POSTBMAC_MARK_CP2_PHY0_SAMPLE,
	OB_RADIO_POSTBMAC_MARK_CP3_BRANCH_GATE,
	OB_RADIO_POSTBMAC_MARK_CP4_PMU_0658_DONE,
	OB_RADIO_POSTBMAC_MARK_CP5_PMU_065C_DONE,
	OB_RADIO_POSTBMAC_MARK_CP6_GPIO_006C_DONE,
	OB_RADIO_POSTBMAC_MARK_PASS,
	OB_RADIO_POSTBMAC_MARK_FAIL,
	OB_RADIO_POSTBMAC_MARK_STOP,
};

enum ob_radio_postbmac_stage {
	OB_RADIO_POSTBMAC_STAGE_NONE,
	OB_RADIO_POSTBMAC_STAGE_ENTRY_GATE,
	OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST,
	OB_RADIO_POSTBMAC_STAGE_OP1_SELECTOR_01B0,
	OB_RADIO_POSTBMAC_STAGE_OP1_DATA_READ,
	OB_RADIO_POSTBMAC_STAGE_OP1_DATA_WRITE,
	OB_RADIO_POSTBMAC_STAGE_OP2_SELECTOR_0000,
	OB_RADIO_POSTBMAC_STAGE_OP2_DATA_READ,
	OB_RADIO_POSTBMAC_STAGE_HISTORICAL_BRANCH,
	OB_RADIO_POSTBMAC_STAGE_CHIPCOMMON_GATE,
	OB_RADIO_POSTBMAC_STAGE_PMU_0658_READ,
	OB_RADIO_POSTBMAC_STAGE_PMU_0658_WRITE,
	OB_RADIO_POSTBMAC_STAGE_PMU_0658_READBACK,
	OB_RADIO_POSTBMAC_STAGE_PMU_065C_READ,
	OB_RADIO_POSTBMAC_STAGE_PMU_065C_WRITE,
	OB_RADIO_POSTBMAC_STAGE_PMU_065C_READBACK,
	OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READ,
	OB_RADIO_POSTBMAC_STAGE_GPIO_006C_WRITE,
	OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READBACK,
};

struct ob_radio_postbmac_gate {
	bool mode_selected;
	bool identity_valid;
	u16 chip_id;
	u8 chip_rev;
	u8 d11_core_rev;
	u8 phy_type;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_revision_class;
	bool d11_post_ucode_ready;
	bool acphy_start_ready;
	bool radio_access_initialized;
	bool d11_core_enabled;
	bool chanspec_valid;
	u16 chanspec;
	bool phase2_success;
	bool postphase2_success;
	bool otp8e2_observed;
	u16 otp8e2_word;
	u8 otp8e2_value;
};

struct ob_radio_postbmac_cc_ops {
	bool (*ready)(void *ctx);
	bool (*device_lost)(void *ctx);
	u32 (*read32)(void *ctx, u16 reg);
	void (*write32)(void *ctx, u16 reg, u32 value);
};

struct ob_radio_postbmac_ops {
	const struct ob_phy_window_ops *window;
	const struct ob_radio_postbmac_cc_ops *cc;
	void (*marker)(void *ctx, enum ob_radio_postbmac_marker marker,
		       u16 sample, u8 low4, bool ja_gt1,
		       enum ob_radio_postbmac_stage stage, int ret,
		       u32 old_value, u32 write_value, u32 readback);
};

struct ob_radio_postbmac_result {
	enum ob_radio_postbmac_stage failure_stage;
	u16 sample;
	u8 low4;
	bool ja_gt1;
	u8 logical_ops;
	u8 window_callbacks;
	u8 cc_callbacks;
	bool historical_branch_executed;
	bool success;
	u32 pmu_0658_old;
	u32 pmu_0658_readback;
	u32 pmu_065c_old;
	u32 pmu_065c_write;
	u32 pmu_065c_readback;
	u32 gpio_006c_old;
	u32 gpio_006c_write;
	u32 gpio_006c_readback;
};

static inline bool ob_radio_postbmac_gate_valid(
	const struct ob_radio_postbmac_gate *gate)
{
	return gate && gate->mode_selected && gate->identity_valid &&
		gate->chip_id == 0x4352u && gate->chip_rev == 3u &&
		gate->d11_core_rev == 42u && gate->phy_type == 0x0bu &&
		gate->phy_revision == 1u && gate->radio_id == 0x2069u &&
		gate->radio_revision == 4u &&
		gate->radio_revision_class == 0u &&
		gate->d11_post_ucode_ready && gate->acphy_start_ready &&
		gate->radio_access_initialized && gate->d11_core_enabled &&
		gate->chanspec_valid && gate->chanspec == 0x1001u &&
		gate->phase2_success && gate->postphase2_success &&
		gate->otp8e2_observed && gate->otp8e2_word == 0x0600u &&
		gate->otp8e2_value == 0x06u;
}

static inline void ob_radio_postbmac_emit(
	const struct ob_radio_postbmac_ops *ops, void *ctx,
	enum ob_radio_postbmac_marker marker, u16 sample, u8 low4,
	bool ja_gt1, enum ob_radio_postbmac_stage stage, int ret,
	u32 old_value, u32 write_value, u32 readback)
{
	ops->marker(ctx, marker, sample, low4, ja_gt1, stage, ret,
		    old_value, write_value, readback);
}

static inline int ob_radio_postbmac_fail(
	const struct ob_radio_postbmac_ops *ops, void *ctx,
	struct ob_radio_postbmac_result *result,
	enum ob_radio_postbmac_stage stage, int ret)
{
	result->failure_stage = stage;
	ops->marker(ctx, OB_RADIO_POSTBMAC_MARK_FAIL, result->sample,
		    result->low4, result->ja_gt1, stage, ret, 0, 0, 0);
	ops->marker(ctx, OB_RADIO_POSTBMAC_MARK_STOP, result->sample,
		    result->low4, result->ja_gt1, stage, ret, 0, 0, 0);
	return ret;
}

static inline enum ob_radio_postbmac_stage ob_radio_postbmac_access_stage(
	bool op1, enum ob_phy_window_stage stage)
{
	if (stage == OB_PHY_WINDOW_STAGE_DEVICE_LOST)
		return OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST;
	if (op1) {
		if (stage == OB_PHY_WINDOW_STAGE_DATA_READ)
			return OB_RADIO_POSTBMAC_STAGE_OP1_DATA_READ;
		if (stage == OB_PHY_WINDOW_STAGE_DATA_WRITE)
			return OB_RADIO_POSTBMAC_STAGE_OP1_DATA_WRITE;
		return OB_RADIO_POSTBMAC_STAGE_OP1_SELECTOR_01B0;
	}
	if (stage == OB_PHY_WINDOW_STAGE_DATA_READ)
		return OB_RADIO_POSTBMAC_STAGE_OP2_DATA_READ;
	return OB_RADIO_POSTBMAC_STAGE_OP2_SELECTOR_0000;
}

static inline int ob_radio_postbmac_cc_read(
	const struct ob_radio_postbmac_ops *ops, void *ctx,
	struct ob_radio_postbmac_result *result, u16 reg,
	enum ob_radio_postbmac_stage stage, u32 *value)
{
	if (ops->cc->device_lost(ctx))
		return ob_radio_postbmac_fail(ops, ctx, result,
			OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST, -EIO);
	if (!ops->cc->ready(ctx))
		return ob_radio_postbmac_fail(ops, ctx, result,
			OB_RADIO_POSTBMAC_STAGE_CHIPCOMMON_GATE, -ENODEV);
	*value = ops->cc->read32(ctx, reg);
	result->cc_callbacks++;
	if (ops->cc->device_lost(ctx))
		return ob_radio_postbmac_fail(ops, ctx, result,
			OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST, -EIO);
	if (*value == 0xffffffffu)
		return ob_radio_postbmac_fail(ops, ctx, result, stage, -EIO);
	return 0;
}

static inline int ob_radio_postbmac_cc_write(
	const struct ob_radio_postbmac_ops *ops, void *ctx,
	struct ob_radio_postbmac_result *result, u16 reg, u32 value,
	enum ob_radio_postbmac_stage stage)
{
	if (ops->cc->device_lost(ctx))
		return ob_radio_postbmac_fail(ops, ctx, result,
			OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST, -EIO);
	if (!ops->cc->ready(ctx))
		return ob_radio_postbmac_fail(ops, ctx, result,
			OB_RADIO_POSTBMAC_STAGE_CHIPCOMMON_GATE, -ENODEV);
	ops->cc->write32(ctx, reg, value);
	result->cc_callbacks++;
	if (ops->cc->device_lost(ctx))
		return ob_radio_postbmac_fail(ops, ctx, result, stage, -EIO);
	return 0;
}

static inline int ob_radio_postbmac_cc_rmw(
	const struct ob_radio_postbmac_ops *ops, void *ctx,
	struct ob_radio_postbmac_result *result, u16 reg, u32 mask, u32 value,
	enum ob_radio_postbmac_stage read_stage,
	enum ob_radio_postbmac_stage write_stage,
	enum ob_radio_postbmac_stage readback_stage,
	enum ob_radio_postbmac_marker marker,
	u32 *old_value_out, u32 *write_value_out, u32 *readback_out)
{
	u32 old_value, write_value, readback;
	int ret;

	ret = ob_radio_postbmac_cc_read(ops, ctx, result, reg, read_stage,
					&old_value);
	if (ret)
		return ret;
	write_value = (old_value & ~mask) | value;
	ret = ob_radio_postbmac_cc_write(ops, ctx, result, reg, write_value,
					 write_stage);
	if (ret)
		return ret;
	ret = ob_radio_postbmac_cc_read(ops, ctx, result, reg,
					readback_stage, &readback);
	if (ret)
		return ret;
	result->logical_ops++;
	*old_value_out = old_value;
	*write_value_out = write_value;
	*readback_out = readback;
	ops->marker(ctx, marker, result->sample, result->low4,
		    result->ja_gt1, OB_RADIO_POSTBMAC_STAGE_NONE, 0,
		    old_value, write_value, readback);
	return 0;
}

static inline int ob_radio_postbmac_run(
	const struct ob_radio_postbmac_ops *ops, void *ctx,
	const struct ob_radio_postbmac_gate *gate,
	struct ob_radio_postbmac_result *result)
{
	enum ob_phy_window_stage window_stage = OB_PHY_WINDOW_STAGE_NONE;
	enum ob_radio_postbmac_stage failure_stage;
	u16 phy_new_value;
	u32 cc_write_value;
	int ret;

	if (!result)
		return -EINVAL;
	*result = (struct ob_radio_postbmac_result){ 0 };
	if (!ops || !ops->window || !ops->cc || !ops->cc->ready ||
	    !ops->cc->device_lost || !ops->cc->read32 ||
	    !ops->cc->write32 || !ops->marker)
		return -EINVAL;
	if (!ob_radio_postbmac_gate_valid(gate))
		return ob_radio_postbmac_fail(ops, ctx, result,
			OB_RADIO_POSTBMAC_STAGE_ENTRY_GATE, -EINVAL);
	ret = ob_phy_window_check_ready(ops->window, ctx, &window_stage);
	if (ret) {
		failure_stage = window_stage == OB_PHY_WINDOW_STAGE_DEVICE_LOST ?
			OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST :
			OB_RADIO_POSTBMAC_STAGE_ENTRY_GATE;
		return ob_radio_postbmac_fail(ops, ctx, result, failure_stage,
			ret);
	}

	ob_radio_postbmac_emit(ops, ctx, OB_RADIO_POSTBMAC_MARK_CP0_ENTRY,
			       0, 0, false, OB_RADIO_POSTBMAC_STAGE_NONE,
			       0, 0, 0, 0);
	ret = ob_phy_window_rmw16_once(ops->window, ctx, 0x01b0u,
				       0x8000u, 0x8000u, &phy_new_value,
				       &result->window_callbacks,
				       &window_stage);
	if (ret)
		return ob_radio_postbmac_fail(ops, ctx, result,
			ob_radio_postbmac_access_stage(true, window_stage), ret);
	result->logical_ops++;
	ob_radio_postbmac_emit(ops, ctx,
		OB_RADIO_POSTBMAC_MARK_CP1_PHY1B0_RMW_DONE, 0, 0, false,
		OB_RADIO_POSTBMAC_STAGE_NONE, 0, 0, 0, 0);

	ret = ob_phy_window_read16_once(ops->window, ctx, 0x0000u,
					&result->sample,
					&result->window_callbacks,
					&window_stage);
	if (ret)
		return ob_radio_postbmac_fail(ops, ctx, result,
			ob_radio_postbmac_access_stage(false, window_stage), ret);
	result->logical_ops++;
	result->low4 = (u8)(result->sample & 0x000fu);
	result->ja_gt1 = result->low4 > 1u;
	ob_radio_postbmac_emit(ops, ctx,
		OB_RADIO_POSTBMAC_MARK_CP2_PHY0_SAMPLE, result->sample,
		result->low4, result->ja_gt1,
		OB_RADIO_POSTBMAC_STAGE_NONE, 0, 0, 0, 0);
	ob_radio_postbmac_emit(ops, ctx,
		OB_RADIO_POSTBMAC_MARK_CP3_BRANCH_GATE, result->sample,
		result->low4, result->ja_gt1,
		OB_RADIO_POSTBMAC_STAGE_NONE, 0, 0, 0, 0);

	/* The accepted target takes ja @0xb01f0 and jne @0xb01fd as specified. */
	if (result->ja_gt1 || result->low4 != 1u)
		return ob_radio_postbmac_fail(ops, ctx, result,
			OB_RADIO_POSTBMAC_STAGE_HISTORICAL_BRANCH, -EAGAIN);
	result->historical_branch_executed = true;

	ret = ob_radio_postbmac_cc_rmw(ops, ctx, result, 0x0658u,
				       0xffffffffu, 0,
				       OB_RADIO_POSTBMAC_STAGE_PMU_0658_READ,
				       OB_RADIO_POSTBMAC_STAGE_PMU_0658_WRITE,
				       OB_RADIO_POSTBMAC_STAGE_PMU_0658_READBACK,
				       OB_RADIO_POSTBMAC_MARK_CP4_PMU_0658_DONE,
				       &result->pmu_0658_old, &cc_write_value,
				       &result->pmu_0658_readback);
	if (ret)
		return ret;

	ret = ob_radio_postbmac_cc_rmw(ops, ctx, result, 0x065cu,
				       0x01f00000u, 0x00300000u,
				       OB_RADIO_POSTBMAC_STAGE_PMU_065C_READ,
				       OB_RADIO_POSTBMAC_STAGE_PMU_065C_WRITE,
				       OB_RADIO_POSTBMAC_STAGE_PMU_065C_READBACK,
				       OB_RADIO_POSTBMAC_MARK_CP5_PMU_065C_DONE,
				       &result->pmu_065c_old,
				       &result->pmu_065c_write,
				       &result->pmu_065c_readback);
	if (ret)
		return ret;

	ret = ob_radio_postbmac_cc_rmw(ops, ctx, result, 0x006cu,
				       0x0000ffffu, 0,
				       OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READ,
				       OB_RADIO_POSTBMAC_STAGE_GPIO_006C_WRITE,
				       OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READBACK,
				       OB_RADIO_POSTBMAC_MARK_CP6_GPIO_006C_DONE,
				       &result->gpio_006c_old,
				       &result->gpio_006c_write,
				       &result->gpio_006c_readback);
	if (ret)
		return ret;

	result->success = true;
	ob_radio_postbmac_emit(ops, ctx, OB_RADIO_POSTBMAC_MARK_PASS,
			       result->sample, result->low4, result->ja_gt1,
			       OB_RADIO_POSTBMAC_STAGE_NONE, 0, 0, 0, 0);
	ob_radio_postbmac_emit(ops, ctx, OB_RADIO_POSTBMAC_MARK_STOP,
			       result->sample, result->low4, result->ja_gt1,
			       OB_RADIO_POSTBMAC_STAGE_NONE, 0, 0, 0, 0);
	return 0;
}

#endif /* _OB_RADIO_POSTBMAC_H_ */

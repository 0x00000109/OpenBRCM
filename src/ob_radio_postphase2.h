/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RADIO_POSTPHASE2_H_
#define _OB_RADIO_POSTPHASE2_H_

#include <linux/types.h>

enum ob_radio_postphase2_marker {
	OB_RADIO_POSTPHASE2_MARK_CP0_ENTRY,
	OB_RADIO_POSTPHASE2_MARK_CP1_0411_CLEANUP_DONE,
	OB_RADIO_POSTPHASE2_MARK_CP2_0416,
	OB_RADIO_POSTPHASE2_MARK_CP3_CHAIN0_DONE,
	OB_RADIO_POSTPHASE2_MARK_CP4_CHAIN1_DONE,
	OB_RADIO_POSTPHASE2_MARK_CP5_PHASE_END_DONE,
	OB_RADIO_POSTPHASE2_MARK_RETURN,
	OB_RADIO_POSTPHASE2_MARK_PASS,
	OB_RADIO_POSTPHASE2_MARK_FAIL,
	OB_RADIO_POSTPHASE2_MARK_STOP,
};

enum ob_radio_postphase2_stage {
	OB_RADIO_POSTPHASE2_STAGE_NONE,
	OB_RADIO_POSTPHASE2_STAGE_INVALID_ARGUMENT,
	OB_RADIO_POSTPHASE2_STAGE_PHASE2_GATE,
	OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST,
	OB_RADIO_POSTPHASE2_STAGE_CLEANUP_0411,
	OB_RADIO_POSTPHASE2_STAGE_READ_0416,
	OB_RADIO_POSTPHASE2_STAGE_CHAIN0_0171,
	OB_RADIO_POSTPHASE2_STAGE_CHAIN1_0371,
	OB_RADIO_POSTPHASE2_STAGE_PHASE_END_0410,
	OB_RADIO_POSTPHASE2_STAGE_TAIL_08EA,
};

struct ob_radio_postphase2_ops {
	int (*read16)(void *ctx, u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 reg, u16 value);
	bool (*device_lost)(void *ctx);
	void (*marker)(void *ctx, enum ob_radio_postphase2_marker marker,
		       u16 sample, u8 field,
		       enum ob_radio_postphase2_stage stage, int ret);
};

struct ob_radio_postphase2_result {
	enum ob_radio_postphase2_stage failure_stage;
	u16 sample_0416;
	u8 sample_field;
	u32 logical_ops;
	bool success;
};

static inline u8 ob_radio_postphase2_field(u16 sample)
{
	return (u8)((sample & 0x03e0u) >> 5);
}

static inline void ob_radio_postphase2_emit(
	const struct ob_radio_postphase2_ops *ops, void *ctx,
	enum ob_radio_postphase2_marker marker, u16 sample, u8 field)
{
	if (ops->marker)
		ops->marker(ctx, marker, sample, field,
			    OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
}

static inline void ob_radio_postphase2_emit_terminal(
	const struct ob_radio_postphase2_ops *ops, void *ctx,
	enum ob_radio_postphase2_marker marker,
	enum ob_radio_postphase2_stage stage, int ret)
{
	if (ops->marker)
		ops->marker(ctx, marker, 0, 0, stage, ret);
}

static inline int ob_radio_postphase2_fail(
	const struct ob_radio_postphase2_ops *ops, void *ctx, int ret,
	struct ob_radio_postphase2_result *result)
{
	if (result->failure_stage == OB_RADIO_POSTPHASE2_STAGE_NONE)
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST;
	ob_radio_postphase2_emit_terminal(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_FAIL, result->failure_stage, ret);
	ob_radio_postphase2_emit_terminal(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_STOP, result->failure_stage, ret);
	return ret;
}

static inline int ob_radio_postphase2_read(
	const struct ob_radio_postphase2_ops *ops, void *ctx, u16 reg,
	enum ob_radio_postphase2_stage stage, u16 *value,
	struct ob_radio_postphase2_result *result)
{
	int ret;

	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST;
		return -5;
	}
	ret = ops->read16(ctx, reg, value);
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST;
		return ret ? ret : -5;
	}
	if (ret) {
		result->failure_stage = stage;
		return ret;
	}
	if (*value == 0xffffu) {
		result->failure_stage = stage;
		return -5;
	}
	return 0;
}

static inline int ob_radio_postphase2_write(
	const struct ob_radio_postphase2_ops *ops, void *ctx, u16 reg,
	u16 value, enum ob_radio_postphase2_stage stage,
	struct ob_radio_postphase2_result *result)
{
	int ret;

	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST;
		return -5;
	}
	ret = ops->write16(ctx, reg, value);
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST;
		return ret ? ret : -5;
	}
	if (ret)
		result->failure_stage = stage;
	return ret;
}

/* Historical mod_radio_reg semantics: (old & ~value) | (value & mask). */
static inline int ob_radio_postphase2_mod(
	const struct ob_radio_postphase2_ops *ops, void *ctx, u16 reg,
	u16 value, u16 mask, enum ob_radio_postphase2_stage stage,
	struct ob_radio_postphase2_result *result)
{
	u16 old;
	int ret;

	ret = ob_radio_postphase2_read(ops, ctx, reg, stage, &old, result);
	if (ret)
		return ret;
	return ob_radio_postphase2_write(ops, ctx, reg,
		(u16)((old & (u16)~value) | (value & mask)), stage, result);
}

static inline int ob_radio_postphase2_run(
	const struct ob_radio_postphase2_ops *ops, void *ctx,
	bool third_poll_success, struct ob_radio_postphase2_result *result)
{
	int ret;

	if (!result)
		return -22;
	*result = (struct ob_radio_postphase2_result){ 0 };
	if (!ops || !ops->read16 || !ops->write16 || !ops->device_lost ||
	    !ops->marker) {
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_INVALID_ARGUMENT;
		return -22;
	}
	if (!third_poll_success) {
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_PHASE2_GATE;
		return -22;
	}
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE2_STAGE_DEVICE_LOST;
		return ob_radio_postphase2_fail(ops, ctx, -5, result);
	}

	ob_radio_postphase2_emit(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_CP0_ENTRY, 0, 0);

	result->logical_ops++;
	ret = ob_radio_postphase2_mod(ops, ctx, 0x0411u, 0x0001u, 0x0000u,
		OB_RADIO_POSTPHASE2_STAGE_CLEANUP_0411, result);
	if (ret)
		return ob_radio_postphase2_fail(ops, ctx, ret, result);
	ob_radio_postphase2_emit(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_CP1_0411_CLEANUP_DONE, 0, 0);

	result->logical_ops++;
	ret = ob_radio_postphase2_read(ops, ctx, 0x0416u,
		OB_RADIO_POSTPHASE2_STAGE_READ_0416,
		&result->sample_0416, result);
	if (ret)
		return ob_radio_postphase2_fail(ops, ctx, ret, result);
	result->sample_field = ob_radio_postphase2_field(result->sample_0416);
	ob_radio_postphase2_emit(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_CP2_0416,
		result->sample_0416, result->sample_field);

	result->logical_ops++;
	ret = ob_radio_postphase2_mod(ops, ctx, 0x0171u, 0x2000u, 0x0000u,
		OB_RADIO_POSTPHASE2_STAGE_CHAIN0_0171, result);
	if (ret)
		return ob_radio_postphase2_fail(ops, ctx, ret, result);
	ob_radio_postphase2_emit(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_CP3_CHAIN0_DONE, 0, 0);

	result->logical_ops++;
	ret = ob_radio_postphase2_mod(ops, ctx, 0x0371u, 0x2000u, 0x0000u,
		OB_RADIO_POSTPHASE2_STAGE_CHAIN1_0371, result);
	if (ret)
		return ob_radio_postphase2_fail(ops, ctx, ret, result);
	ob_radio_postphase2_emit(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_CP4_CHAIN1_DONE, 0, 0);

	result->logical_ops++;
	ret = ob_radio_postphase2_mod(ops, ctx, 0x0410u, 0x0001u, 0x0000u,
		OB_RADIO_POSTPHASE2_STAGE_PHASE_END_0410, result);
	if (ret)
		return ob_radio_postphase2_fail(ops, ctx, ret, result);
	ob_radio_postphase2_emit(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_CP5_PHASE_END_DONE, 0, 0);

	result->logical_ops++;
	ret = ob_radio_postphase2_mod(ops, ctx, 0x08eau, 0x0080u, 0x0000u,
		OB_RADIO_POSTPHASE2_STAGE_TAIL_08EA, result);
	if (ret)
		return ob_radio_postphase2_fail(ops, ctx, ret, result);
	ob_radio_postphase2_emit(ops, ctx, OB_RADIO_POSTPHASE2_MARK_RETURN, 0, 0);
	result->success = true;
	ob_radio_postphase2_emit_terminal(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_PASS,
		OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	ob_radio_postphase2_emit_terminal(ops, ctx,
		OB_RADIO_POSTPHASE2_MARK_STOP,
		OB_RADIO_POSTPHASE2_STAGE_NONE, 0);
	return 0;
}

#endif /* _OB_RADIO_POSTPHASE2_H_ */

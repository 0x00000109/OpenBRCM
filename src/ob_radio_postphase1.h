/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RADIO_POSTPHASE1_H_
#define _OB_RADIO_POSTPHASE1_H_

#include <linux/types.h>

#define OB_RADIO_POSTPHASE1_MAX_ATTEMPTS 100u
#define OB_RADIO_POSTPHASE1_POLL_DELAY_US 100u
#define OB_RADIO_POSTPHASE1_POLL_MASK 0x0010u

enum ob_radio_postphase1_marker {
	OB_RADIO_POSTPHASE1_MARK_CP0_ENTRY,
	OB_RADIO_POSTPHASE1_MARK_CP1_0416,
	OB_RADIO_POSTPHASE1_MARK_CP2_PHASE1_CHAIN_APPLY_DONE,
	OB_RADIO_POSTPHASE1_MARK_CP3_PHASE2_SETUP_DONE,
	OB_RADIO_POSTPHASE1_MARK_CP4_PHASE2_CHAIN_SETUP_DONE,
	OB_RADIO_POSTPHASE1_MARK_CP5_PRE_POLL,
	OB_RADIO_POSTPHASE1_MARK_POLL,
};

enum ob_radio_postphase1_stage {
	OB_RADIO_POSTPHASE1_STAGE_NONE,
	OB_RADIO_POSTPHASE1_STAGE_INVALID_ARGUMENT,
	OB_RADIO_POSTPHASE1_STAGE_PHASE1_GATE,
	OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST,
	OB_RADIO_POSTPHASE1_STAGE_0411_CLEANUP,
	OB_RADIO_POSTPHASE1_STAGE_READ_0416,
	OB_RADIO_POSTPHASE1_STAGE_PHASE1_CHAIN_APPLY,
	OB_RADIO_POSTPHASE1_STAGE_PHASE_END_0410,
	OB_RADIO_POSTPHASE1_STAGE_PHASE2_SETUP,
	OB_RADIO_POSTPHASE1_STAGE_PHASE2_CHAIN_SETUP,
	OB_RADIO_POSTPHASE1_STAGE_PHASE2_TRIGGER,
	OB_RADIO_POSTPHASE1_STAGE_POLL_DELAY,
	OB_RADIO_POSTPHASE1_STAGE_POLL_READ,
	OB_RADIO_POSTPHASE1_STAGE_POLL_TIMEOUT,
};

struct ob_radio_postphase1_ops {
	int (*read16)(void *ctx, u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 reg, u16 value);
	void (*delay_us)(void *ctx, u32 usec);
	bool (*device_lost)(void *ctx);
	void (*marker)(void *ctx, enum ob_radio_postphase1_marker marker,
		       u16 sample, u16 low5, u32 attempts,
		       u16 last_value, bool success);
};

struct ob_radio_postphase1_result {
	enum ob_radio_postphase1_stage failure_stage;
	u16 sample_0416;
	u16 sample_low5;
	u32 attempts;
	u16 last_value;
	bool poll_started;
	bool poll_marker_emitted;
	bool success;
};

static inline int ob_radio_postphase1_read(
	const struct ob_radio_postphase1_ops *ops, void *ctx, u16 reg,
	enum ob_radio_postphase1_stage stage, u16 *value,
	struct ob_radio_postphase1_result *result)
{
	int ret;

	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST;
		return -5;
	}
	ret = ops->read16(ctx, reg, value);
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST;
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

static inline int ob_radio_postphase1_write(
	const struct ob_radio_postphase1_ops *ops, void *ctx, u16 reg,
	u16 value, enum ob_radio_postphase1_stage stage,
	struct ob_radio_postphase1_result *result)
{
	int ret;

	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST;
		return -5;
	}
	ret = ops->write16(ctx, reg, value);
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST;
		return ret ? ret : -5;
	}
	if (ret)
		result->failure_stage = stage;
	return ret;
}

/* Historical mod_radio_reg formula: (old & ~value) | (value & mask). */
static inline int ob_radio_postphase1_mod(
	const struct ob_radio_postphase1_ops *ops, void *ctx, u16 reg,
	u16 value, u16 mask, enum ob_radio_postphase1_stage stage,
	struct ob_radio_postphase1_result *result)
{
	u16 old;
	int ret;

	ret = ob_radio_postphase1_read(ops, ctx, reg, stage, &old, result);
	if (ret)
		return ret;
	return ob_radio_postphase1_write(ops, ctx, reg,
		(u16)((old & (u16)~value) | (value & mask)), stage, result);
}

static inline int ob_radio_postphase1_delay(
	const struct ob_radio_postphase1_ops *ops, void *ctx, u32 usec,
	enum ob_radio_postphase1_stage stage,
	struct ob_radio_postphase1_result *result)
{
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST;
		return -5;
	}
	ops->delay_us(ctx, usec);
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST;
		return -5;
	}
	(void)stage;
	return 0;
}

static inline int ob_radio_postphase1_run(
	const struct ob_radio_postphase1_ops *ops, void *ctx,
	bool phase1_success, struct ob_radio_postphase1_result *result)
{
	static const u16 phase1_regs[] = { 0x0126u, 0x0043u };
	static const u16 phase2_clear_regs[] = { 0x011du, 0x031du };
	static const u16 phase2_set_regs[] = { 0x0171u, 0x0371u };
	u32 i;
	u16 value = 0;
	int ret;

	if (!result)
		return -22;
	*result = (struct ob_radio_postphase1_result){ 0 };
	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us ||
	    !ops->device_lost || !ops->marker) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_INVALID_ARGUMENT;
		return -22;
	}
	if (!phase1_success) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_PHASE1_GATE;
		return -22;
	}
	if (ops->device_lost(ctx)) {
		result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_DEVICE_LOST;
		return -5;
	}
	ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_CP0_ENTRY,
		    0, 0, 0, 0, false);

	/* Shared post-phase-1 cleanup must precede the one dynamic 0x0416 read. */
	ret = ob_radio_postphase1_mod(ops, ctx, 0x0411u, 0x0001u, 0x0000u,
		OB_RADIO_POSTPHASE1_STAGE_0411_CLEANUP, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_read(ops, ctx, 0x0416u,
		OB_RADIO_POSTPHASE1_STAGE_READ_0416,
		&result->sample_0416, result);
	if (ret)
		return ret;
	result->sample_low5 = (u16)(result->sample_0416 & 0x001fu);
	ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_CP1_0416,
		    result->sample_0416, result->sample_low5, 0, 0, false);

	for (i = 0; i < 2u; i++) {
		ret = ob_radio_postphase1_mod(ops, ctx,
			phase1_regs[0] | (u16)(i << 9), 0x001fu,
			result->sample_low5,
			OB_RADIO_POSTPHASE1_STAGE_PHASE1_CHAIN_APPLY, result);
		if (ret)
			return ret;
		ret = ob_radio_postphase1_mod(ops, ctx,
			phase1_regs[1] | (u16)(i << 9), 0x001fu,
			result->sample_low5,
			OB_RADIO_POSTPHASE1_STAGE_PHASE1_CHAIN_APPLY, result);
		if (ret)
			return ret;
	}
	ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_CP2_PHASE1_CHAIN_APPLY_DONE,
		    result->sample_0416, result->sample_low5, 0, 0, false);

	ret = ob_radio_postphase1_mod(ops, ctx, 0x0410u, 0x0001u, 0x0000u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE_END_0410, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_mod(ops, ctx, 0x0410u, 0x1000u, 0x0000u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_SETUP, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_mod(ops, ctx, 0x0410u, 0x0018u, 0x0008u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_SETUP, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_mod(ops, ctx, 0x0411u, 0xff00u, 0x4000u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_SETUP, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_write(ops, ctx, 0x0412u, 0x011au,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_SETUP, result);
	if (ret)
		return ret;
	ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_CP3_PHASE2_SETUP_DONE,
		    result->sample_0416, result->sample_low5, 0, 0, false);

	for (i = 0; i < 2u; i++) {
		ret = ob_radio_postphase1_mod(ops, ctx, phase2_clear_regs[i],
			0x0004u, 0x0000u,
			OB_RADIO_POSTPHASE1_STAGE_PHASE2_CHAIN_SETUP, result);
		if (ret)
			return ret;
		ret = ob_radio_postphase1_mod(ops, ctx, phase2_set_regs[i],
			0x2000u, 0x2000u,
			OB_RADIO_POSTPHASE1_STAGE_PHASE2_CHAIN_SETUP, result);
		if (ret)
			return ret;
	}
	ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_CP4_PHASE2_CHAIN_SETUP_DONE,
		    result->sample_0416, result->sample_low5, 0, 0, false);

	ret = ob_radio_postphase1_mod(ops, ctx, 0x0410u, 0x0001u, 0x0000u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_TRIGGER, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_delay(ops, ctx, 1u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_TRIGGER, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_mod(ops, ctx, 0x0410u, 0x0001u, 0x0001u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_TRIGGER, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_delay(ops, ctx, 35u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_TRIGGER, result);
	if (ret)
		return ret;
	ret = ob_radio_postphase1_mod(ops, ctx, 0x0411u, 0x0001u, 0x0001u,
		OB_RADIO_POSTPHASE1_STAGE_PHASE2_TRIGGER, result);
	if (ret)
		return ret;
	ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_CP5_PRE_POLL,
		    result->sample_0416, result->sample_low5, 0, 0, false);
	result->poll_started = true;

	for (i = 0; i < OB_RADIO_POSTPHASE1_MAX_ATTEMPTS; i++) {
		ret = ob_radio_postphase1_delay(ops, ctx,
			OB_RADIO_POSTPHASE1_POLL_DELAY_US,
			OB_RADIO_POSTPHASE1_STAGE_POLL_DELAY, result);
		if (ret)
			return ret;
		value = 0;
		ret = ob_radio_postphase1_read(ops, ctx, 0x0413u,
			OB_RADIO_POSTPHASE1_STAGE_POLL_READ, &value, result);
		result->attempts = i + 1u;
		result->last_value = value;
		if (ret)
			return ret;
		if (value & OB_RADIO_POSTPHASE1_POLL_MASK) {
			result->success = true;
			ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_POLL,
				    result->sample_0416, result->sample_low5,
				    result->attempts, result->last_value, true);
			result->poll_marker_emitted = true;
			return 0;
		}
	}
	result->failure_stage = OB_RADIO_POSTPHASE1_STAGE_POLL_TIMEOUT;
	ops->marker(ctx, OB_RADIO_POSTPHASE1_MARK_POLL,
		    result->sample_0416, result->sample_low5,
		    result->attempts, result->last_value, false);
	result->poll_marker_emitted = true;
	return -110;
}

#endif /* _OB_RADIO_POSTPHASE1_H_ */

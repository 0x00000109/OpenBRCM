/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_RADIO_POST0413_H_
#define _OB_RADIO_POST0413_H_

#include <linux/types.h>

#define OB_RADIO_POST0413_MAX_ATTEMPTS 100u
#define OB_RADIO_POST0413_POLL_DELAY_US 100u
#define OB_RADIO_POST0413_POLL_MASK 0x0010u

enum ob_radio_post0413_marker {
	OB_RADIO_POST0413_MARK_CP0_ENTRY,
	OB_RADIO_POST0413_MARK_CP1_SAMPLES,
	OB_RADIO_POST0413_MARK_CP2_SETUP_DONE,
	OB_RADIO_POST0413_MARK_CP3_PRE_POLL,
};

enum ob_radio_post0413_stage {
	OB_RADIO_POST0413_STAGE_NONE,
	OB_RADIO_POST0413_STAGE_INVALID_ARGUMENT,
	OB_RADIO_POST0413_STAGE_PHASE0_GATE,
	OB_RADIO_POST0413_STAGE_DEVICE_LOST,
	OB_RADIO_POST0413_STAGE_RMW_411_CLEANUP,
	OB_RADIO_POST0413_STAGE_READ_414,
	OB_RADIO_POST0413_STAGE_READ_415,
	OB_RADIO_POST0413_STAGE_RMW_410_ADVANCE,
	OB_RADIO_POST0413_STAGE_RMW_410_PHASE1_A,
	OB_RADIO_POST0413_STAGE_RMW_410_PHASE1_B,
	OB_RADIO_POST0413_STAGE_RMW_411_PHASE1,
	OB_RADIO_POST0413_STAGE_WRITE_412,
	OB_RADIO_POST0413_STAGE_RMW_410_TRIGGER_CLEAR,
	OB_RADIO_POST0413_STAGE_RMW_410_TRIGGER_SET,
	OB_RADIO_POST0413_STAGE_RMW_411_TRIGGER,
	OB_RADIO_POST0413_STAGE_POLL_READ,
	OB_RADIO_POST0413_STAGE_POLL_TIMEOUT,
};

struct ob_radio_post0413_ops {
	int (*read16)(void *ctx, u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 reg, u16 value);
	void (*delay_us)(void *ctx, u32 usec);
	bool (*device_lost)(void *ctx);
	void (*marker)(void *ctx, enum ob_radio_post0413_marker marker,
		       u16 r414, u16 r415, u8 derived);
};

struct ob_radio_post0413_result {
	enum ob_radio_post0413_stage failure_stage;
	u16 r414;
	u16 r415;
	u8 derived;
	u32 attempts;
	u16 last_value;
	bool poll_started;
	bool success;
};

static inline bool ob_radio_post0413_lost(
	const struct ob_radio_post0413_ops *ops, void *ctx)
{
	return ops->device_lost(ctx);
}

static inline int ob_radio_post0413_read(
	const struct ob_radio_post0413_ops *ops, void *ctx, u16 reg,
	enum ob_radio_post0413_stage stage, u16 *value,
	struct ob_radio_post0413_result *result)
{
	int ret;

	if (ob_radio_post0413_lost(ops, ctx)) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
		return -5;
	}
	ret = ops->read16(ctx, reg, value);
	if (ob_radio_post0413_lost(ops, ctx)) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
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

static inline int ob_radio_post0413_write(
	const struct ob_radio_post0413_ops *ops, void *ctx, u16 reg, u16 value,
	enum ob_radio_post0413_stage stage,
	struct ob_radio_post0413_result *result)
{
	int ret;

	if (ob_radio_post0413_lost(ops, ctx)) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
		return -5;
	}
	ret = ops->write16(ctx, reg, value);
	if (ob_radio_post0413_lost(ops, ctx)) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
		return ret ? ret : -5;
	}
	if (ret)
		result->failure_stage = stage;
	return ret;
}

/* These operations are historical mod_radio_reg calls, whose semantics are
 * composite value/mask RMW rather than conventional mask/value RMW. */
static inline int ob_radio_post0413_mod(
	const struct ob_radio_post0413_ops *ops, void *ctx, u16 reg,
	u16 value, u16 mask, enum ob_radio_post0413_stage stage,
	struct ob_radio_post0413_result *result)
{
	u16 old;
	int ret;

	ret = ob_radio_post0413_read(ops, ctx, reg, stage, &old, result);
	if (ret)
		return ret;
	return ob_radio_post0413_write(ops, ctx, reg,
		(u16)((old & (u16)~value) | (value & mask)), stage, result);
}

static inline int ob_radio_post0413_delay(
	const struct ob_radio_post0413_ops *ops, void *ctx, u32 usec,
	struct ob_radio_post0413_result *result)
{
	if (ob_radio_post0413_lost(ops, ctx)) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
		return -5;
	}
	ops->delay_us(ctx, usec);
	if (ob_radio_post0413_lost(ops, ctx)) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
		return -5;
	}
	return 0;
}

static inline int ob_radio_post0413_run(
	const struct ob_radio_post0413_ops *ops, void *ctx, bool phase0_success,
	struct ob_radio_post0413_result *result)
{
	u32 delta, scaled, i;
	u16 value = 0;
	int ret;

	if (!result)
		return -22;
	*result = (struct ob_radio_post0413_result){ 0 };
	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us ||
	    !ops->device_lost || !ops->marker) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_INVALID_ARGUMENT;
		return -22;
	}
	if (!phase0_success) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_PHASE0_GATE;
		return -22;
	}
	if (ob_radio_post0413_lost(ops, ctx)) {
		result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
		return -5;
	}
	ops->marker(ctx, OB_RADIO_POST0413_MARK_CP0_ENTRY, 0, 0, 0);

	/* Immediate post-phase0 historical cleanup; no operation may precede it. */
	ret = ob_radio_post0413_mod(ops, ctx, 0x0411u, 0x0001u, 0x0000u,
		OB_RADIO_POST0413_STAGE_RMW_411_CLEANUP, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_read(ops, ctx, 0x0414u,
		OB_RADIO_POST0413_STAGE_READ_414, &result->r414, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_read(ops, ctx, 0x0415u,
		OB_RADIO_POST0413_STAGE_READ_415, &result->r415, result);
	if (ret)
		return ret;
	delta = (u32)result->r415 - (u32)result->r414;
	scaled = delta * (u32)0x00c1u;
	result->derived = (u8)(scaled >> 8);
	ops->marker(ctx, OB_RADIO_POST0413_MARK_CP1_SAMPLES,
		    result->r414, result->r415, result->derived);

	ret = ob_radio_post0413_mod(ops, ctx, 0x0410u, 0x0001u, 0x0000u,
		OB_RADIO_POST0413_STAGE_RMW_410_ADVANCE, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_mod(ops, ctx, 0x0410u, 0x1000u, 0x0000u,
		OB_RADIO_POST0413_STAGE_RMW_410_PHASE1_A, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_mod(ops, ctx, 0x0410u, 0x0018u, 0x0010u,
		OB_RADIO_POST0413_STAGE_RMW_410_PHASE1_B, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_mod(ops, ctx, 0x0411u, 0xff00u, 0x7000u,
		OB_RADIO_POST0413_STAGE_RMW_411_PHASE1, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_write(ops, ctx, 0x0412u, 0x0101u,
		OB_RADIO_POST0413_STAGE_WRITE_412, result);
	if (ret)
		return ret;
	ops->marker(ctx, OB_RADIO_POST0413_MARK_CP2_SETUP_DONE,
		    result->r414, result->r415, result->derived);

	/* r12 is phase 1, not 2: the historical 0x11d loop is not entered. */
	ret = ob_radio_post0413_mod(ops, ctx, 0x0410u, 0x0001u, 0x0000u,
		OB_RADIO_POST0413_STAGE_RMW_410_TRIGGER_CLEAR, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_delay(ops, ctx, 1u, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_mod(ops, ctx, 0x0410u, 0x0001u, 0x0001u,
		OB_RADIO_POST0413_STAGE_RMW_410_TRIGGER_SET, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_delay(ops, ctx, 35u, result);
	if (ret)
		return ret;
	ret = ob_radio_post0413_mod(ops, ctx, 0x0411u, 0x0001u, 0x0001u,
		OB_RADIO_POST0413_STAGE_RMW_411_TRIGGER, result);
	if (ret)
		return ret;
	ops->marker(ctx, OB_RADIO_POST0413_MARK_CP3_PRE_POLL,
		    result->r414, result->r415, result->derived);
	result->poll_started = true;

	for (i = 0; i < OB_RADIO_POST0413_MAX_ATTEMPTS; i++) {
		ret = ob_radio_post0413_delay(ops, ctx,
			OB_RADIO_POST0413_POLL_DELAY_US, result);
		if (ret)
			return ret;
		value = 0;
		ret = ops->read16(ctx, 0x0413u, &value);
		result->attempts = i + 1u;
		result->last_value = value;
		if (ob_radio_post0413_lost(ops, ctx)) {
			result->failure_stage = OB_RADIO_POST0413_STAGE_DEVICE_LOST;
			return ret ? ret : -5;
		}
		if (ret) {
			result->failure_stage = OB_RADIO_POST0413_STAGE_POLL_READ;
			return ret;
		}
		if (value == 0xffffu) {
			result->failure_stage = OB_RADIO_POST0413_STAGE_POLL_READ;
			return -5;
		}
		if (value & OB_RADIO_POST0413_POLL_MASK) {
			result->success = true;
			return 0;
		}
	}
	result->failure_stage = OB_RADIO_POST0413_STAGE_POLL_TIMEOUT;
	return -110;
}

#endif /* _OB_RADIO_POST0413_H_ */

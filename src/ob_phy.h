/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_H_
#define _OB_PHY_H_

#include <linux/types.h>

#include "ob_phy_tripoint.h"

struct ob_hw;

/* Vendor phy_reg_read/mod indirect window. */
#define OB_PHY_REG_ADDR_LATCH 0x03fcu
#define OB_PHY_REG_DATA       0x03feu

struct ob_phy_access_ops {
	int (*read16)(void *ctx, u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 reg, u16 value);
};

/* Selected PHY-window seam: one selector may remain active across a read and
 * its paired data write. Access callbacks report only errors their backend
 * can actually observe; device loss is checked independently. */
enum ob_phy_window_stage {
	OB_PHY_WINDOW_STAGE_NONE,
	OB_PHY_WINDOW_STAGE_ENTRY,
	OB_PHY_WINDOW_STAGE_DEVICE_LOST,
	OB_PHY_WINDOW_STAGE_SELECTOR,
	OB_PHY_WINDOW_STAGE_DATA_READ,
	OB_PHY_WINDOW_STAGE_DATA_WRITE,
};

struct ob_phy_window_ops {
	bool (*ready)(void *ctx);
	bool (*device_lost)(void *ctx);
	int (*select16)(void *ctx, u16 selector);
	int (*read_data16)(void *ctx, u16 *value);
	int (*write_data16)(void *ctx, u16 value);
};

static inline int ob_phy_window_check_ready(
	const struct ob_phy_window_ops *ops, void *ctx,
	enum ob_phy_window_stage *stage)
{
	if (!ops || !ops->ready || !ops->device_lost || !ops->select16 ||
	    !ops->read_data16 || !ops->write_data16 || !stage) {
		if (stage)
			*stage = OB_PHY_WINDOW_STAGE_ENTRY;
		return -22;
	}
	if (ops->device_lost(ctx)) {
		*stage = OB_PHY_WINDOW_STAGE_DEVICE_LOST;
		return -5;
	}
	if (!ops->ready(ctx)) {
		*stage = OB_PHY_WINDOW_STAGE_ENTRY;
		return -11;
	}
	if (ops->device_lost(ctx)) {
		*stage = OB_PHY_WINDOW_STAGE_DEVICE_LOST;
		return -5;
	}
	*stage = OB_PHY_WINDOW_STAGE_NONE;
	return 0;
}

static inline int ob_phy_window_check_lost(
	const struct ob_phy_window_ops *ops, void *ctx,
	enum ob_phy_window_stage *stage)
{
	if (ops->device_lost(ctx)) {
		*stage = OB_PHY_WINDOW_STAGE_DEVICE_LOST;
		return -5;
	}
	return 0;
}

static inline int ob_phy_window_select(
	const struct ob_phy_window_ops *ops, void *ctx, u16 selector,
	u8 *access_count, enum ob_phy_window_stage *stage)
{
	int ret;

	if (ob_phy_window_check_lost(ops, ctx, stage))
		return -5;
	if (access_count)
		(*access_count)++;
	ret = ops->select16(ctx, selector);
	if (ob_phy_window_check_lost(ops, ctx, stage))
		return -5;
	if (ret) {
		*stage = OB_PHY_WINDOW_STAGE_SELECTOR;
		return ret;
	}
	return 0;
}

static inline int ob_phy_window_read_data(
	const struct ob_phy_window_ops *ops, void *ctx, u16 *value,
	u8 *access_count, enum ob_phy_window_stage *stage)
{
	int ret;

	if (ob_phy_window_check_lost(ops, ctx, stage))
		return -5;
	if (access_count)
		(*access_count)++;
	ret = ops->read_data16(ctx, value);
	if (ob_phy_window_check_lost(ops, ctx, stage))
		return -5;
	if (ret) {
		*stage = OB_PHY_WINDOW_STAGE_DATA_READ;
		return ret;
	}
	return 0;
}

static inline int ob_phy_window_write_data(
	const struct ob_phy_window_ops *ops, void *ctx, u16 value,
	u8 *access_count, enum ob_phy_window_stage *stage)
{
	int ret;

	if (ob_phy_window_check_lost(ops, ctx, stage))
		return -5;
	if (access_count)
		(*access_count)++;
	ret = ops->write_data16(ctx, value);
	if (ob_phy_window_check_lost(ops, ctx, stage))
		return -5;
	if (ret) {
		*stage = OB_PHY_WINDOW_STAGE_DATA_WRITE;
		return ret;
	}
	return 0;
}

static inline int ob_phy_window_rmw16_once(
	const struct ob_phy_window_ops *ops, void *ctx, u16 selector, u16 mask,
	u16 value, u16 *new_value, u8 *access_count,
	enum ob_phy_window_stage *failure_stage)
{
	u16 old, next;
	int ret;

	if (!new_value || !failure_stage)
		return -22;
	ret = ob_phy_window_check_ready(ops, ctx, failure_stage);
	if (ret)
		return ret;
	ret = ob_phy_window_select(ops, ctx, selector, access_count,
				   failure_stage);
	if (ret)
		return ret;
	ret = ob_phy_window_read_data(ops, ctx, &old, access_count,
				      failure_stage);
	if (ret)
		return ret;
	next = (u16)((old & (u16)~mask) | (value & mask));
	ret = ob_phy_window_check_lost(ops, ctx, failure_stage);
	if (ret)
		return ret;
	ret = ob_phy_window_write_data(ops, ctx, next, access_count,
				       failure_stage);
	if (ret)
		return ret;
	*new_value = next;
	*failure_stage = OB_PHY_WINDOW_STAGE_NONE;
	return 0;
}

static inline int ob_phy_window_read16_once(
	const struct ob_phy_window_ops *ops, void *ctx, u16 selector,
	u16 *value, u8 *access_count,
	enum ob_phy_window_stage *failure_stage)
{
	int ret;

	if (!value || !failure_stage)
		return -22;
	ret = ob_phy_window_check_ready(ops, ctx, failure_stage);
	if (ret)
		return ret;
	ret = ob_phy_window_select(ops, ctx, selector, access_count,
				   failure_stage);
	if (ret)
		return ret;
	ret = ob_phy_window_read_data(ops, ctx, value, access_count,
				      failure_stage);
	if (ret)
		return ret;
	*failure_stage = OB_PHY_WINDOW_STAGE_NONE;
	return 0;
}

enum ob_acphy_start_state {
	OB_ACPHY_START_NOT_STARTED,
	OB_ACPHY_START_IN_PROGRESS,
	OB_ACPHY_START_READY_FOR_RADIO_ON,
	OB_ACPHY_START_FAILED,
};

/* Driver-visible PHY lifecycle.  These states deliberately distinguish the
 * pre-radio prefix from radio/calibration/channel completion.  Reaching an
 * earlier state never implies that mac80211 can use the device. */
enum ob_phy_init_phase {
	OB_PHY_INIT_NOT_STARTED,
	OB_PHY_INIT_PRE_RADIO_IN_PROGRESS,
	OB_PHY_INIT_PRE_RADIO_READY,
	OB_PHY_INIT_RADIO_IN_PROGRESS,
	OB_PHY_INIT_RADIO_COMPLETE,
	OB_PHY_INIT_CALIBRATION_IN_PROGRESS,
	OB_PHY_INIT_CALIBRATION_COMPLETE,
	OB_PHY_INIT_CHANNEL_PROGRAMMING,
	OB_PHY_INIT_CHANNEL_PROGRAMMED,
	OB_PHY_INIT_OPERATIONAL,
	OB_PHY_INIT_BLOCKED,
	OB_PHY_INIT_FAILED,
};

enum ob_phy_init_blocker {
	OB_PHY_BLOCK_NONE,
	OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP,
	OB_PHY_BLOCK_LPF_TARGET,
	OB_PHY_BLOCK_LPF_PI164_BRANCH,
	OB_PHY_BLOCK_LPF_BOARDFLAGS3,
	OB_PHY_BLOCK_LPF_BOARDFLAGS3_BRANCH,
	OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD,
	OB_PHY_BLOCK_REV1_AFTER_SUB92500,
	OB_PHY_BLOCK_POST_LPF_LOOKUP_MISS,
	OB_PHY_BLOCK_CHANNEL_LOOKUP,
	OB_PHY_BLOCK_CALIBRATION_INPUTS,
};

struct ob_phy_init_lifecycle {
	enum ob_phy_init_phase phase;
	enum ob_phy_init_blocker blocker;
	int error;
	bool radio_prefix_complete;
	bool analog_tx_lpf_complete;
	bool radio_sequence_complete;
	bool calibration_complete;
	bool channel_programmed;
	bool post_lpf_lookup_complete;
	bool post_lpf_lookup_found;
	u8 post_lpf_lookup_key;
	u8 post_lpf_lookup_row;
	u16 post_lpf_lookup_row_offset;
};

static inline int ob_phy_init_begin(struct ob_phy_init_lifecycle *state)
{
	if (!state || state->phase != OB_PHY_INIT_NOT_STARTED)
		return -16;
	state->phase = OB_PHY_INIT_PRE_RADIO_IN_PROGRESS;
	state->blocker = OB_PHY_BLOCK_NONE;
	state->error = 0;
	state->radio_prefix_complete = false;
	state->analog_tx_lpf_complete = false;
	state->radio_sequence_complete = false;
	state->calibration_complete = false;
	state->channel_programmed = false;
	state->post_lpf_lookup_complete = false;
	state->post_lpf_lookup_found = false;
	state->post_lpf_lookup_key = 0;
	state->post_lpf_lookup_row = 0;
	state->post_lpf_lookup_row_offset = 0;
	return 0;
}

/* Record only the proven lookup result. A hit identifies a row but does not
 * authorize the later row-driven PHY/radio sequence. */
static inline int ob_phy_init_post_lpf_lookup_complete(
	struct ob_phy_init_lifecycle *state, bool found, u8 key,
	u8 row_index, u16 row_offset)
{
	if (!state || state->phase != OB_PHY_INIT_RADIO_IN_PROGRESS ||
	    !state->radio_prefix_complete || !state->analog_tx_lpf_complete ||
	    state->post_lpf_lookup_complete)
		return -95;
	if ((found && (row_index >= 77u ||
		       row_offset != (u16)(row_index * 0x74u))) ||
	    (!found && (row_index != 0xffu || row_offset != 0xffffu)))
		return -22;
	state->post_lpf_lookup_complete = true;
	state->post_lpf_lookup_found = found;
	state->post_lpf_lookup_key = key;
	state->post_lpf_lookup_row = row_index;
	state->post_lpf_lookup_row_offset = row_offset;
	return 0;
}

static inline int ob_phy_init_advance(struct ob_phy_init_lifecycle *state,
				      enum ob_phy_init_phase next)
{
	enum ob_phy_init_phase expected;

	if (!state)
		return -22;
	switch (state->phase) {
	case OB_PHY_INIT_PRE_RADIO_IN_PROGRESS:
		expected = OB_PHY_INIT_PRE_RADIO_READY;
		break;
	case OB_PHY_INIT_PRE_RADIO_READY:
		expected = OB_PHY_INIT_RADIO_IN_PROGRESS;
		break;
	case OB_PHY_INIT_RADIO_IN_PROGRESS:
		expected = OB_PHY_INIT_RADIO_COMPLETE;
		break;
	case OB_PHY_INIT_RADIO_COMPLETE:
		expected = OB_PHY_INIT_CALIBRATION_IN_PROGRESS;
		break;
	case OB_PHY_INIT_CALIBRATION_IN_PROGRESS:
		expected = OB_PHY_INIT_CALIBRATION_COMPLETE;
		break;
	case OB_PHY_INIT_CALIBRATION_COMPLETE:
		expected = OB_PHY_INIT_CHANNEL_PROGRAMMING;
		break;
	case OB_PHY_INIT_CHANNEL_PROGRAMMING:
		expected = OB_PHY_INIT_CHANNEL_PROGRAMMED;
		break;
	case OB_PHY_INIT_CHANNEL_PROGRAMMED:
		expected = OB_PHY_INIT_OPERATIONAL;
		break;
	default:
		return -95;
	}
	if (next != expected)
		return -22;
	/* A phase label is not evidence that its transaction ran. Completion
	 * transitions require a success token set only after that stage returns. */
	if ((next == OB_PHY_INIT_RADIO_COMPLETE &&
	     (!state->radio_prefix_complete ||
	      !state->analog_tx_lpf_complete ||
	      !state->radio_sequence_complete)) ||
	    (next == OB_PHY_INIT_CALIBRATION_COMPLETE &&
	     !state->calibration_complete) ||
	    (next == OB_PHY_INIT_CHANNEL_PROGRAMMED &&
	     !state->channel_programmed) ||
	    (next == OB_PHY_INIT_OPERATIONAL &&
	     (!state->radio_prefix_complete ||
	      !state->analog_tx_lpf_complete ||
	      !state->radio_sequence_complete ||
	      !state->calibration_complete ||
	      !state->channel_programmed)))
		return -95;
	state->phase = next;
	state->blocker = OB_PHY_BLOCK_NONE;
	state->error = 0;
	return 0;
}

static inline int ob_phy_init_block(struct ob_phy_init_lifecycle *state,
				    enum ob_phy_init_blocker blocker)
{
	if (!state)
		return -22;
	if (!((state->phase == OB_PHY_INIT_PRE_RADIO_IN_PROGRESS &&
	       (blocker == OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP ||
		blocker == OB_PHY_BLOCK_LPF_TARGET ||
		blocker == OB_PHY_BLOCK_LPF_PI164_BRANCH ||
		blocker == OB_PHY_BLOCK_LPF_BOARDFLAGS3 ||
		blocker == OB_PHY_BLOCK_LPF_BOARDFLAGS3_BRANCH ||
		blocker == OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD)) ||
	      (state->phase == OB_PHY_INIT_PRE_RADIO_READY &&
	       (blocker == OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP ||
		blocker == OB_PHY_BLOCK_LPF_TARGET ||
		blocker == OB_PHY_BLOCK_LPF_PI164_BRANCH ||
		blocker == OB_PHY_BLOCK_LPF_BOARDFLAGS3 ||
		blocker == OB_PHY_BLOCK_LPF_BOARDFLAGS3_BRANCH ||
		blocker == OB_PHY_BLOCK_CHANNEL_LOOKUP ||
		blocker == OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD)) ||
	      (state->phase == OB_PHY_INIT_RADIO_IN_PROGRESS &&
	       (blocker == OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP ||
		blocker == OB_PHY_BLOCK_CHANNEL_LOOKUP ||
		(blocker == OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD &&
		 state->post_lpf_lookup_complete &&
		 state->post_lpf_lookup_found) ||
		(blocker == OB_PHY_BLOCK_POST_LPF_LOOKUP_MISS &&
		 state->post_lpf_lookup_complete &&
		 !state->post_lpf_lookup_found) ||
		(blocker == OB_PHY_BLOCK_REV1_AFTER_SUB92500 &&
		 state->post_lpf_lookup_complete &&
		 state->post_lpf_lookup_found))) ||
	      (state->phase == OB_PHY_INIT_CALIBRATION_IN_PROGRESS &&
	       blocker == OB_PHY_BLOCK_CALIBRATION_INPUTS) ||
	      (state->phase == OB_PHY_INIT_CHANNEL_PROGRAMMING &&
	       blocker == OB_PHY_BLOCK_CHANNEL_LOOKUP)))
		return -22;
	state->phase = OB_PHY_INIT_BLOCKED;
	state->blocker = blocker;
	state->error = -95;
	return 0;
}

static inline int ob_phy_init_begin_radio(
	struct ob_phy_init_lifecycle *state)
{
	return ob_phy_init_advance(state, OB_PHY_INIT_RADIO_IN_PROGRESS);
}

/* The historical radio prefix through sub_9591e precedes the analog LPF
 * helper. Publish its token only after that bounded production helper returns. */
static inline int ob_phy_init_radio_prefix_complete(
	struct ob_phy_init_lifecycle *state)
{
	if (!state || state->phase != OB_PHY_INIT_RADIO_IN_PROGRESS ||
	    state->radio_prefix_complete)
		return -22;
	state->radio_prefix_complete = true;
	return 0;
}

/* Resume the radio stage after the REV1_AFTER_SUB92500 block, once the
 * post-LPF continuation has run. Tokens set before the block are preserved. */
static inline int ob_phy_init_resume_radio(struct ob_phy_init_lifecycle *state)
{
	if (!state || state->phase != OB_PHY_INIT_BLOCKED ||
	    state->blocker != OB_PHY_BLOCK_REV1_AFTER_SUB92500)
		return -22;
	state->phase = OB_PHY_INIT_RADIO_IN_PROGRESS;
	state->blocker = OB_PHY_BLOCK_NONE;
	state->error = 0;
	return 0;
}

/* Publish only after the bounded post-LPF radio sequence (sub_a7089 +
 * sub_a0be4) has returned. */
static inline int ob_phy_init_radio_sequence_complete(
	struct ob_phy_init_lifecycle *state)
{
	if (!state || state->phase != OB_PHY_INIT_RADIO_IN_PROGRESS ||
	    state->radio_sequence_complete)
		return -22;
	state->radio_sequence_complete = true;
	return 0;
}

static inline int ob_phy_init_fail(struct ob_phy_init_lifecycle *state,
				   int error)
{
	if (!state || !error || state->phase == OB_PHY_INIT_OPERATIONAL ||
	    state->phase == OB_PHY_INIT_BLOCKED ||
	    state->phase == OB_PHY_INIT_FAILED)
		return -16;
	state->phase = OB_PHY_INIT_FAILED;
	state->blocker = OB_PHY_BLOCK_NONE;
	state->error = error;
	return 0;
}

static inline bool ob_phy_init_is_operational(
	const struct ob_phy_init_lifecycle *state)
{
	return state && state->phase == OB_PHY_INIT_OPERATIONAL &&
		state->blocker == OB_PHY_BLOCK_NONE && state->error == 0;
}

static inline const char *ob_phy_init_phase_name(enum ob_phy_init_phase phase)
{
	switch (phase) {
	case OB_PHY_INIT_NOT_STARTED: return "NOT_STARTED";
	case OB_PHY_INIT_PRE_RADIO_IN_PROGRESS: return "PRE_RADIO_IN_PROGRESS";
	case OB_PHY_INIT_PRE_RADIO_READY: return "PRE_RADIO_READY";
	case OB_PHY_INIT_RADIO_IN_PROGRESS: return "RADIO_IN_PROGRESS";
	case OB_PHY_INIT_RADIO_COMPLETE: return "RADIO_COMPLETE";
	case OB_PHY_INIT_CALIBRATION_IN_PROGRESS:
		return "CALIBRATION_IN_PROGRESS";
	case OB_PHY_INIT_CALIBRATION_COMPLETE: return "CALIBRATION_COMPLETE";
	case OB_PHY_INIT_CHANNEL_PROGRAMMING: return "CHANNEL_PROGRAMMING";
	case OB_PHY_INIT_CHANNEL_PROGRAMMED: return "CHANNEL_PROGRAMMED";
	case OB_PHY_INIT_OPERATIONAL: return "OPERATIONAL";
	case OB_PHY_INIT_BLOCKED: return "BLOCKED";
	case OB_PHY_INIT_FAILED: return "FAILED";
	default: return "INVALID";
	}
}

static inline const char *ob_phy_init_blocker_name(
	enum ob_phy_init_blocker blocker)
{
	switch (blocker) {
	case OB_PHY_BLOCK_NONE: return "NONE";
	case OB_PHY_BLOCK_POST_LPF_CHANNEL_LOOKUP:
		return "POST_LPF_CHANNEL_LOOKUP";
	case OB_PHY_BLOCK_LPF_TARGET: return "LPF_TARGET";
	case OB_PHY_BLOCK_LPF_PI164_BRANCH: return "LPF_PI164_BRANCH";
	case OB_PHY_BLOCK_LPF_BOARDFLAGS3: return "LPF_BOARDFLAGS3";
	case OB_PHY_BLOCK_LPF_BOARDFLAGS3_BRANCH:
		return "LPF_BOARDFLAGS3_BRANCH";
	case OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD:
		return "POST_LPF_ROW_PAYLOAD";
	case OB_PHY_BLOCK_REV1_AFTER_SUB92500:
		return "REV1_AFTER_SUB92500";
	case OB_PHY_BLOCK_POST_LPF_LOOKUP_MISS:
		return "POST_LPF_LOOKUP_MISS";
	case OB_PHY_BLOCK_CHANNEL_LOOKUP: return "CHANNEL_LOOKUP";
	case OB_PHY_BLOCK_CALIBRATION_INPUTS: return "CALIBRATION_INPUTS";
	default: return "INVALID";
	}
}

struct ob_acphy_pre_radio_state {
	u16 bandwidth_bits;
	bool bandwidth_valid;
	u8 state_offset_465;
	u8 state_offset_f88;
};

/* Callback seam keeps the exact pre-radio transaction host-testable. */
struct ob_acphy_pre_radio_ops {
	int (*shm_write16)(void *ctx, u16 off, u16 value);
	int (*d11_read32_trusted)(void *ctx, u16 off, u32 *value);
	int (*d11_write16)(void *ctx, u16 off, u16 value);
	int (*axi_rmw32_checked)(void *ctx, u16 off, u32 mask,
				  u32 value, u32 *new_value);
	bool (*device_lost)(void *ctx);
};

static inline int ob_acphy_first_up_begin(enum ob_acphy_start_state *state)
{
	if (!state || *state != OB_ACPHY_START_NOT_STARTED)
		return -16;
	*state = OB_ACPHY_START_IN_PROGRESS;
	return 0;
}

static inline void ob_acphy_first_up_finish(enum ob_acphy_start_state *state,
					   bool success)
{
	if (state && *state == OB_ACPHY_START_IN_PROGRESS)
		*state = success ? OB_ACPHY_START_READY_FOR_RADIO_ON :
			OB_ACPHY_START_FAILED;
}

static inline int ob_acphy_first_up_fail(enum ob_acphy_start_state *state)
{
	if (!state || (*state != OB_ACPHY_START_IN_PROGRESS &&
		       *state != OB_ACPHY_START_READY_FOR_RADIO_ON))
		return -16;
	*state = OB_ACPHY_START_FAILED;
	return 0;
}

static inline u32 ob_acphy_bw_cflags(u16 bandwidth_bits)
{
	switch (bandwidth_bits) {
	case 0x1000:
		return 0x40;
	case 0x1800:
		return 0x80;
	case 0x2000:
		return 0xc0;
	default:
		return 0;
	}
}

static inline int ob_acphy_pre_radio_run(
	const struct ob_acphy_pre_radio_ops *ops, void *ctx, u16 chanspec,
	struct ob_acphy_pre_radio_state *state)
{
	u16 bandwidth_bits = chanspec & 0x3800u;
	u32 ignored_read, ignored_new_value;
	int ret;

	if (!ops || !ops->shm_write16 || !ops->d11_read32_trusted ||
	    !ops->d11_write16 || !ops->axi_rmw32_checked ||
	    !ops->device_lost || !state)
		return -22;
	if (ops->device_lost(ctx))
		return -19;

	ret = ops->shm_write16(ctx, 0x00a0, chanspec);
	if (ret)
		return ret;
	if (ops->device_lost(ctx))
		return -19;

	ret = ops->d11_read32_trusted(ctx, 0x0120, &ignored_read);
	if (ret)
		return ret;
	if (ops->device_lost(ctx))
		return -19;

	ret = ops->d11_write16(ctx, 0x03e6, 0x0000);
	if (ret)
		return ret;
	if (ops->device_lost(ctx))
		return -19;

	if (!state->bandwidth_valid ||
	    state->bandwidth_bits != bandwidth_bits) {
		ret = ops->axi_rmw32_checked(ctx, 0x0408, 0x000000c0u,
					     ob_acphy_bw_cflags(bandwidth_bits),
					     &ignored_new_value);
		if (ret)
			return ret;
		if (ops->device_lost(ctx))
			return -19;
		state->bandwidth_bits = bandwidth_bits;
		state->bandwidth_valid = true;
	}

	/* Offset-derived software fields; their semantic meanings remain unnamed. */
	state->state_offset_465 = 0;
	state->state_offset_f88 = 0;
	return 0;
}

static inline int ob_phy_rmw16(const struct ob_phy_access_ops *ops,
		void *ctx, u16 reg, u16 mask, u16 value)
{
	u16 old;
	int ret;

	if (!ops || !ops->read16 || !ops->write16)
		return -22;
	ret = ops->read16(ctx, reg, &old);
	if (ret)
		return ret;
	return ops->write16(ctx, reg,
		(u16)((old & (u16)~mask) | (value & mask)));
}

/*
 * S1: the vendor's core0 -> core1 mirror block. Recovered from the vendor
 * mmiotrace, NOT from the sub_a04c2 vector: boot 64536612..., the prologue read
 * series at t = 531.521658..531.521700, then two identical write blocks at
 * t = 531.533405..531.533424 and 531.535567..531.535587, and the sub_a04c2
 * fixture block at 531.613518..531.613550.
 *
 * The blocks are distinct and 80 ms apart: the mirror writes the values it
 * just read (0x1739/0x173a/0x1725/0x1729/0x1721/0x1728/0x1720) plus a constant
 * 0x173e = 0x1c00 with no source in the read series, while the LPF block
 * writes the immediate fixture values (0x1721 = 0x5000, 0x1720 = 0x0180,
 * 0x173e = 0x0000, ...). Adding this block therefore does not replace or
 * correct the LPF block; it fills a missing earlier step.
 *
 * Order and count are part of the contract: seven reads in the vendor order,
 * then 0x173e and the seven mirrors in the vendor write order, then a
 * read-identity write of PHY[0x0408], then PHY[0x0417] = 0x0000 and
 * PHY[0x0416] = 0x0001. Every failure is fail-stop: the first error is
 * returned and no later operation is issued.
 */
static inline int ob_phy_acphy_core_mirror_run(
	const struct ob_phy_access_ops *ops, void *ctx)
{
	u16 r739, r73a, r725, r729, r721, r728, r720, r408;
	int ret;

	if (!ops || !ops->read16 || !ops->write16)
		return -22;
	ret = ops->read16(ctx, 0x0739, &r739);
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x073a, &r73a);
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x0725, &r725);
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x0729, &r729);
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x0721, &r721);
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x0728, &r728);
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x0720, &r720);
	if (ret)
		return ret;

	ret = ops->write16(ctx, 0x173e, 0x1c00);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x1739, r739);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x173a, r73a);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x1725, r725);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x1729, r729);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x1721, r721);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x1728, r728);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x1720, r720);
	if (ret)
		return ret;

	ret = ops->read16(ctx, 0x0408, &r408);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x0408, r408);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x0417, 0x0000);
	if (ret)
		return ret;
	return ops->write16(ctx, 0x0416, 0x0001);
}

/*
 * Stage form: the block is issued TWICE, back to back, which is what the
 * vendor does (t = 531.533405..531.533424 and 531.535567..531.535587). The
 * second pass is not a retry; both passes are part of the recovered sequence
 * and either failure is fail-stop.
 *
 * Placement is part of the contract since 2026-10-28. In the vendor trace the
 * mirror runs 11.7 ms after the prologue read series and 80 ms BEFORE its LPF
 * register block, with the whole 0x164 table stream in between
 * (audit/.../phy_prefix_point01/ORDERING_DELTA.md). It therefore belongs to the
 * pre-identity prefix (ob_radio_vendor_pre_id), not to the LPF stage: run after
 * post-ucode it mirrors the post-ucode generation of the tuple (0x0725 = 0x0600
 * instead of 0x1fff, 0x0408 = 0x0d82 instead of 0x0d80), and the LPF prefix's
 * own 0x1725 = 0x0000 write then erases the 0x0600 the device produced there,
 * leaving 0x0400 where the vendor ends at 0x0600.
 */
static inline int ob_phy_acphy_core_mirror_stage_run(
	const struct ob_phy_access_ops *ops, void *ctx)
{
	unsigned int pass;
	int ret;

	if (!ops || !ops->read16 || !ops->write16)
		return -22;
	for (pass = 0; pass < 2; pass++) {
		ret = ob_phy_acphy_core_mirror_run(ops, ctx);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Callback-only clean-room runner for the BCM4352 PHY-rev-1/0x0a LPF
 * register prelude. The ordered writes and three runtime PHY samples are
 * pinned by bcm4352-next-post-pmu-gpio-block-v1.json. It stops after the
 * initial PHY[0x019e] bit-1 RMW, before the descriptor-backed table stream.
 */
static inline int ob_phy_acphy_lpf_register_prefix_run(
	const struct ob_phy_access_ops *ops, void *ctx, u16 chip_id,
	u8 phy_revision, u16 *saved_019e)
{
	static const struct {
		u16 reg;
		u16 value;
	} writes[] = {
		{ 0x0410, 0x0077 }, { 0x173e, 0x0000 },
		{ 0x1725, 0x0000 }, { 0x1722, 0x0000 },
		{ 0x1723, 0x0000 }, { 0x1724, 0x0000 },
		{ 0x1725, 0x0000 }, { 0x1726, 0x0000 },
		{ 0x1727, 0x0000 }, { 0x1750, 0x0000 },
		{ 0x1728, 0x0080 }, { 0x1720, 0x0180 },
		{ 0x1729, 0x0000 }, { 0x1721, 0x5000 },
	};
	u16 sample_173a, sample_1725, saved;
	unsigned int i;
	int ret;

	if (!ops || !ops->read16 || !ops->write16 || !saved_019e)
		return -22;
	if (chip_id != 0x4352 ||
	    (phy_revision != 1 && phy_revision != 0x0a))
		return -95;

	for (i = 0; i < sizeof(writes) / sizeof(writes[0]); i++) {
		ret = ops->write16(ctx, writes[i].reg, writes[i].value);
		if (ret)
			return ret;
	}

	ret = ops->read16(ctx, 0x173a, &sample_173a);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x173a, (u16)(sample_173a | 0x0100u));
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x1725, &sample_1725);
	if (ret)
		return ret;
	ret = ops->write16(ctx, 0x1725, (u16)(sample_1725 | 0x0400u));
	if (ret)
		return ret;
	ret = ops->read16(ctx, 0x019e, &saved);
	if (ret)
		return ret;
	ret = ob_phy_rmw16(ops, ctx, 0x019e, 0x0002, 0x0002);
	if (ret)
		return ret;
	*saved_019e = saved;
	return 0;
}

/* Backward-compatible test entry; no C implementation path dispatches here. */
static inline int ob_phy_acphy_lpf_register_prefix_test_only(
	const struct ob_phy_access_ops *ops, void *ctx, u16 chip_id,
	u8 phy_revision, u16 *saved_019e)
{
	return ob_phy_acphy_lpf_register_prefix_run(ops, ctx, chip_id,
							    phy_revision, saved_019e);
}

int ob_phy_read16(struct ob_hw *hw, u16 reg, u16 *value);
int ob_phy_write16(struct ob_hw *hw, u16 reg, u16 value);
/* Same latch + data-port pair as ob_phy_read16, without its post-identity
 * admission predicate: legal only where the attach prefix already armed the
 * PHY window (the 0x03e0 read site and later). Fail-stop on device loss. */
int ob_phy_read16_pre_id(struct ob_hw *hw, u16 reg, u16 *value);
int ob_phy_write16_pre_id(struct ob_hw *hw, u16 reg, u16 value);
/* Vendor atomic latch write (ONE 32-bit write at 0x3fc) without the
 * post-identity admission predicate. Same fail-stop; the shape the vendor uses
 * for the S1 core-mirror block. */
int ob_phy_write16_atomic_pre_id(struct ob_hw *hw, u16 reg, u16 value);
/* P0/P1/P2 read-only witness; a no-op unless hw->phy_tripoint_probe is set. */
int ob_phy_tripoint_observe(struct ob_hw *hw,
			    enum ob_phy_tripoint_point point);
/* Vendor AC-PHY table write (wlc_phy_write_table_ext @0xb6959). Register
 * protocol recovered from the vendor mmiotrace (P10/P10c) and the pinned
 * helper: PHY reg 0x0d <- table_id, reg 0x0e <- offset, reg 0x11 <- word0,
 * then words[1..n-1] to the data port 0x3fe. Width 0x30 consumes three u16
 * words per element. */
int ob_acphy_tables_write(struct ob_hw *hw);
int ob_acphy_init_run(struct ob_hw *hw);
int ob_acphy_set_channel(struct ob_hw *hw, u16 code);
int ob_phy_table_write_acphy(struct ob_hw *hw, u8 table_id, u16 offset,
			     u8 element_width_bits, const void *data,
			     u16 element_count);
int ob_phy_rmw16_hw(struct ob_hw *hw, u16 reg, u16 mask, u16 value);
const struct ob_phy_window_ops *ob_phy_window_hw_ops(void);
int ob_acphy_pre_radio_setup(struct ob_hw *hw);
int ob_acphy_init_production_gate(struct ob_hw *hw);
int ob_acphy_pre_radio_test(struct ob_hw *hw);
int ob_acphy_radio_initialize(struct ob_hw *hw);
int ob_acphy_radio_on_test(struct ob_hw *hw);
int ob_acphy_radio_postlpf_test(struct ob_hw *hw);
int ob_acphy_calibration_inputs_probe(struct ob_hw *hw);
/* M1-3: read-only pre-state witness on the admitted M1-2 contract. */
int ob_acphy_calibration_stage_witness(struct ob_hw *hw);
/* M1-4: first owned PHY write arm (vendor txpwrctrl disable) on the same
 * contract, admitted only when the M1-3 pre-state still matches exactly. */
int ob_acphy_calibration_write_stage(struct ob_hw *hw);
int ob_acphy_calibration_w2_replay(struct ob_hw *hw);
int ob_acphy_calibration_initialize(struct ob_hw *hw);
int ob_acphy_channel_initialize(struct ob_hw *hw);

#endif

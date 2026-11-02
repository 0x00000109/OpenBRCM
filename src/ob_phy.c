// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include "ob_core.h"
#include "ob_guard.h"
#include "ob_lifecycle.h"
#include "ob_phy.h"
#include "ob_phy_lpf.h"
#include "ob_phy_post_lpf.h"
#include "ob_si.h"
#include "ob_ucode.h"
#include "ob_channel.h"
#include "ob_radio.h"
#include "ob_radio_2069_lookup.h"
#include "ob_phy_sub92500.h"
#include "ob_acphy_tables.h"
#include "ob_acphy_init_ops.h"
#include "ob_acphy_channels.h"
#include "ob_acphy_cal_contract.h"
#include "ob_acphy_cal_stage.h"
#include "ob_acphy_cal_write_stage.h"
#include "ob_acphy_cal_w2_replay.h"

static bool ob_phy_access_ready(const struct ob_hw *hw)
{
	return hw && !hw->dev_lost && hw->d11_state ==
		OB_D11_POST_UCODE_READY && hw->phy_radio_identity_valid &&
		hw->radio_access_initialized;
}

int ob_phy_read16(struct ob_hw *hw, u16 reg, u16 *value)
{
	if (!value || !ob_phy_access_ready(hw))
		return -22;
	ob_d11_write16(hw, OB_PHY_REG_ADDR_LATCH, reg);
	if (hw->dev_lost)
		return -5;
	*value = ob_d11_read16(hw, OB_PHY_REG_DATA);
	if (hw->dev_lost)
		return -5;
	return 0;
}

int ob_phy_read16_pre_id(struct ob_hw *hw, u16 reg, u16 *value)
{
	if (!hw || !value || hw->dev_lost)
		return -22;
	ob_d11_write16(hw, OB_PHY_REG_ADDR_LATCH, reg);
	if (hw->dev_lost)
		return -5;
	*value = ob_d11_read16(hw, OB_PHY_REG_DATA);
	if (hw->dev_lost)
		return -5;
	return 0;
}

int ob_phy_write16(struct ob_hw *hw, u16 reg, u16 value)
{
	if (!ob_phy_access_ready(hw))
		return -22;
	/*
	 * Vendor phy_reg_write is ONE 32-bit MMIO write at the address latch:
	 * 0x3fc <- (value << 16) | reg. Because 0x3fc + 2 == 0x3fe, this single
	 * transaction latches the register AND writes the data atomically. Using
	 * two separate 16-bit writes (latch then data) can be split/interrupted and
	 * lands the data under the wrong last-latch context (verified as the likely
	 * AC-PHY bus-hang cause). Emit it as one 32-bit write.
	 */
	ob_d11_write32(hw, OB_PHY_REG_ADDR_LATCH,
		       ((u32)value << 16) | (u32)reg);
	return hw->dev_lost ? -5 : 0;
}

/* The vendor pre-ID prefix reaches the PHY window before identity state is
 * published, so it cannot use ob_phy_write16()'s post-identity admission
 * predicate. It still uses the same guarded D11 backend and fail-stop latch. */
int ob_phy_write16_pre_id(struct ob_hw *hw, u16 reg, u16 value)
{
	if (!hw || hw->dev_lost)
		return -22;
	ob_d11_write16(hw, OB_PHY_REG_ADDR_LATCH, reg);
	if (hw->dev_lost)
		return -5;
	ob_d11_write16(hw, OB_PHY_REG_DATA, value);
	return hw->dev_lost ? -5 : 0;
}

/*
 * Pre-identity form of the vendor's ATOMIC latch write: ONE 32-bit write to
 * 0x3fc carrying (value << 16) | reg. This is the shape the vendor emits for
 * its S1 core-mirror block (boot 64536612..., t = 531.533405: one 32-bit write
 * per mirrored word) and the shape ob_phy_write16() uses after identity is
 * published. The pair form above would double the MMIO count and change the
 * trace shape the mirror's contract is checked against. It skips only the
 * post-identity admission predicate, not the dev_lost fail-stop; it is legal
 * only after the attach prefix armed the PHY window (0x408 == 0x5).
 */
int ob_phy_write16_atomic_pre_id(struct ob_hw *hw, u16 reg, u16 value)
{
	if (!hw || hw->dev_lost)
		return -22;
	ob_d11_write32(hw, OB_PHY_REG_ADDR_LATCH,
		       ((u32)value << 16) | (u32)reg);
	return hw->dev_lost ? -5 : 0;
}

/* Vendor AC-PHY table write (wlc_phy_write_table_ext). Register protocol
 * recovered from the vendor mmiotrace (P10/P10c) and the pinned helper
 * 0xb6959..0xb6b14: reg 0x0d <- table_id, reg 0x0e <- offset, reg 0x11 <-
 * word0 (via phy_reg_write), then words[1..] streamed to the data port 0x3fe.
 * Width 0x30 consumes three u16 words per element. Same guarded D11 backend
 * and dev_lost fail-stop as ob_phy_write16. */



/* Channel switch: write the AC-PHY cal row for the given channel code (radio regs). */
int ob_acphy_set_channel(struct ob_hw *hw, u16 code)
{
	unsigned int i, j;

	for (i = 0; i < OB_ACPHY_CHAN_COUNT; i++) {
		const struct ob_acphy_chan *c = &ob_acphy_chans[i];

		if (c->code != code)
			continue;
		for (j = 0; j < c->n; j++) {
			if (ob_radio_write16(hw, ob_acphy_chan_regs[c->off + j],
					     ob_acphy_chan_vals[c->off + j]))
				return -5;
		}
		return 0;
	}
	return -22;
}

/* Data-driven semantic AC-PHY init (non-radio): tables (grouped) + individual PHY/D11
 * ops + delays, in vendor order. HARDWARE-VALIDATED to scale without bus hang. */
int ob_acphy_init_run(struct ob_hw *hw)
{
	unsigned int i;

	for (i = 0; i < OB_ACPHY_OP_COUNT; i++) {
		const struct ob_acphy_op *o = &ob_acphy_init_ops[i];
		int ret = 0;
		if (!hw || hw->dev_lost)
			return -EIO;

		switch (o->code) {
		case 9: {
			u16 per = o->c / 16;

			if (!per)
				break;
			ret = ob_phy_table_write_acphy(hw, (u8)o->a, (u16)o->b,
						       (u8)o->c,
						       &ob_acphy_init_words[o->d],
						       (u16)(o->e / per));
			break;
		}
		case 0: ret = ob_phy_write16(hw, o->a, (u16)o->b); break;
		case 1: { u16 v; ret = ob_phy_read16(hw, o->a, &v); break; }
		case 2: ob_d11_write16(hw, 0x3fe, (u16)o->b); ret = hw->dev_lost ? -5 : 0; break;
		case 3: (void)ob_d11_read16(hw, 0x3fe); break;
		case 4: udelay(o->b ? o->b : 1); break;
		case 5: ob_d11_write16(hw, o->a, (u16)o->b); ret = hw->dev_lost ? -5 : 0; break;
		case 6: (void)ob_d11_read16(hw, o->a); break;
		case 7: ob_d11_write32(hw, o->a, o->b); ret = hw->dev_lost ? -5 : 0; break;
		case 8: (void)ob_d11_read32(hw, o->a); break;
		default: ret = -22; break;
		}
		if (ret)
			return ret;
		if (hw->dev_lost)
			return -EIO;
	}
	return 0;
}

/* Data-driven AC-PHY table init: write every decoded vendor table run in order
 * via the semantic table-write primitive. HARDWARE-VALIDATED to scale (no bus hang
 * unlike raw replay). Returns 0 or a negative errno on the first failure. */
int ob_acphy_tables_write(struct ob_hw *hw)
{
	unsigned int i;

	for (i = 0; i < OB_ACPHY_TABLE_RUNS; i++) {
		const struct ob_acphy_tble *r = &ob_acphy_tbl_runs[i];
		u16 per = r->width / 16;
		u16 cnt = per ? (u16)(r->nwords / per) : 0;
		int ret;

		if (!cnt)
			continue;
		ret = ob_phy_table_write_acphy(hw, r->table, r->offset, r->width,
					       &ob_acphy_tbl_words[r->word_off], cnt);
		if (ret)
			return ret;
	}
	return 0;
}

int ob_phy_table_write_acphy(struct ob_hw *hw, u8 table_id, u16 offset,
			     u8 element_width_bits, const void *data,
			     u16 element_count)
{
	unsigned int i;

	if (!data || !element_count)
		return -22;
	if (!ob_phy_access_ready(hw))
		return -22;
	/* Vendor wlc_phy_write_table_ext (@0xb6959, FACT_BIN): select table_id at
	 * reg 0x0d and offset at reg 0x0e ONCE, then per element write by width:
	 *   0x08 -> reg 0x0f = byte
	 *   0x10 -> reg 0x0f = u16
	 *   0x20 -> reg 0x10 = hi16, reg 0x0f = lo16
	 *   0x30 -> reg 0x11 = word0, then data port 0x3fe = word1, word2
	 *   0x3c/0x40 -> 2 words/element (u32) via reg 0x11 then 0x3fe
	 * No polls/waits inside; the offset auto-increments in hardware. */
	if (ob_phy_write16(hw, 0x0d, table_id))
		return -5;
	if (ob_phy_write16(hw, 0x0e, offset))
		return -5;

	for (i = 0; i < element_count; i++) {
		switch (element_width_bits) {
		case 0x08: {
			const u8 *d = data;

			if (ob_phy_write16(hw, 0x0f, d[i]))
				return -5;
			break;
		}
		case 0x10: {
			const u16 *d = data;

			if (ob_phy_write16(hw, 0x0f, d[i]))
				return -5;
			break;
		}
		case 0x20:
		case 0x3c:
		case 0x40: {
			const u32 *d = data;
			u32 w = d[i];

			if (element_width_bits == 0x20) {
				if (ob_phy_write16(hw, 0x10, (u16)(w >> 16)))
					return -5;
				if (ob_phy_write16(hw, 0x0f, (u16)w))
					return -5;
			} else {
				if (ob_phy_write16(hw, 0x11, (u16)w))
					return -5;
				ob_d11_write16(hw, OB_PHY_REG_DATA, (u16)(w >> 16));
				if (hw->dev_lost)
					return -5;
			}
			break;
		}
		case 0x30: {
			const u16 *d = data;

			if (ob_phy_write16(hw, 0x11, d[i * 3 + 0]))
				return -5;
			ob_d11_write16(hw, OB_PHY_REG_DATA, d[i * 3 + 1]);
			if (hw->dev_lost)
				return -5;
			ob_d11_write16(hw, OB_PHY_REG_DATA, d[i * 3 + 2]);
			if (hw->dev_lost)
				return -5;
			break;
		}
		default:
			return -22;
		}
	}
	return 0;
}

static int ob_phy_access_read16(void *ctx, u16 reg, u16 *value)
{
	return ob_phy_read16(ctx, reg, value);
}

static int ob_phy_access_write16(void *ctx, u16 reg, u16 value)
{
	return ob_phy_write16(ctx, reg, value);
}

static int ob_acphy_lpf_read16(void *ctx, u16 reg, u16 *value)
{
	return ob_phy_read16(ctx, reg, value);
}

static int ob_acphy_lpf_write16(void *ctx, u16 reg, u16 value)
{
	return ob_phy_write16(ctx, reg, value);
}

static int ob_phy_tripoint_read_admitted(void *ctx, u16 reg, u16 *value)
{
	return ob_phy_read16(ctx, reg, value);
}

static int ob_phy_tripoint_read_pre_id(void *ctx, u16 reg, u16 *value)
{
	return ob_phy_read16_pre_id(ctx, reg, value);
}

static bool ob_phy_tripoint_lost(void *ctx)
{
	const struct ob_hw *hw = ctx;

	return !hw || hw->dev_lost;
}

int ob_phy_tripoint_observe(struct ob_hw *hw, enum ob_phy_tripoint_point point)
{
	static const struct ob_phy_tripoint_ops admitted = {
		.read16 = ob_phy_tripoint_read_admitted,
		.device_lost = ob_phy_tripoint_lost,
	};
	static const struct ob_phy_tripoint_ops pre_id = {
		.read16 = ob_phy_tripoint_read_pre_id,
		.device_lost = ob_phy_tripoint_lost,
	};
	const struct ob_phy_tripoint_ops *ops =
		ob_phy_tripoint_uses_pre_id(point) ? &pre_id : &admitted;
	unsigned int i;
	int ret;

	if (!hw || !hw->phy_tripoint_probe)
		return 0;
	/*
	 * P0 precedes the identity publication, so ob_phy_read16()'s admitted
	 * predicate is not true there yet; the pre-ID primitive puts the same
	 * selector/data pair on the wire. P1/P2 use the admitted helper.
	 */
	ret = ob_phy_tripoint_run(ops, hw, &hw->phy_tripoint, point);
	if (ret) {
		dev_info(hw->dev,
			 "TRIPOINT_FAIL point=%s reg=0x%04x ret=%d lost=%u\n",
			 ob_phy_tripoint_name(point), hw->phy_tripoint.last_reg,
			 ret, hw->dev_lost ? 1u : 0u);
		return ret;
	}
	for (i = 0; i < OB_PHY_TRIPOINT_REGS; i++)
		dev_info(hw->dev,
			 "TRIPOINT point=%s idx=%u reg=0x%04x val=0x%04x\n",
			 ob_phy_tripoint_name(point), i, ob_phy_tripoint_regs[i],
			 hw->phy_tripoint.value[point][i]);
	dev_info(hw->dev, "TRIPOINT_DONE point=%s count=%u observed=%u\n",
		 ob_phy_tripoint_name(point),
		 (unsigned int)OB_PHY_TRIPOINT_REGS,
		 hw->phy_tripoint.observed);
	return 0;
}

static struct ob_phy_lpf_target ob_acphy_lpf_target_from_hw(
	const struct ob_hw *hw)
{
	return (struct ob_phy_lpf_target){
		.identity_valid = hw && hw->phy_radio_identity_valid,
		.chip_id = hw ? hw->chip_id : 0,
		.chip_revision = hw ? hw->chip_rev : 0,
		.d11_core_id = hw && hw->core ? hw->core->id.id : 0,
		.d11_revision = hw && hw->core ? hw->core->id.rev : 0,
		.phy_type = hw ? hw->phy_type : 0,
		.phy_revision = hw ? hw->phy_revision : 0,
		.radio_id = hw ? hw->radio_id : 0,
		.radio_revision = hw ? hw->radio_revision : 0,
		.radio_revision_class = hw ? hw->radio_revision_class : 0,
	};
}

static enum ob_phy_init_blocker ob_acphy_lpf_blocker(
	enum ob_phy_lpf_input_status status)
{
	switch (status) {
	case OB_PHY_LPF_INPUT_TARGET:
		return OB_PHY_BLOCK_LPF_TARGET;
	case OB_PHY_LPF_INPUT_PI164_BRANCH:
		return OB_PHY_BLOCK_LPF_PI164_BRANCH;
	case OB_PHY_LPF_INPUT_BOARDFLAGS3:
		return OB_PHY_BLOCK_LPF_BOARDFLAGS3;
	case OB_PHY_LPF_INPUT_BOARDFLAGS3_BRANCH:
		return OB_PHY_BLOCK_LPF_BOARDFLAGS3_BRANCH;
	case OB_PHY_LPF_INPUTS_SUPPORTED:
	default:
		return OB_PHY_BLOCK_POST_LPF_ROW_PAYLOAD;
	}
}

static int ob_acphy_analog_tx_lpf_initialize(struct ob_hw *hw)
{
	static const struct ob_phy_access_ops ops = {
		.read16 = ob_acphy_lpf_read16,
		.write16 = ob_acphy_lpf_write16,
	};
	const struct ob_phy_lpf_target target = ob_acphy_lpf_target_from_hw(hw);
	bool boardflags3_valid = hw->board.boardflags3_valid &&
		hw->board.revision == 11;
	u16 saved_019e;
	int ret;

	/* P2: the vendor's LPF read point. Since the S1 relocation (2026-10-28)
	 * the core-mirror block runs in the pre-identity prefix, so this samples
	 * the state ob_phy_acphy_lpf_stage_run() is about to change, without any
	 * mirror write in between. */
	if (hw->phy_tripoint_probe) {
		ret = ob_phy_tripoint_observe(hw, OB_PHY_TRIPOINT_P2);
		if (ret)
			return ret;
	}

	return ob_phy_acphy_lpf_stage_run(&ops, hw, &target,
					  boardflags3_valid,
					  hw->board.boardflags3,
					  &hw->phy_init, &saved_019e);
}

int ob_phy_rmw16_hw(struct ob_hw *hw, u16 reg, u16 mask, u16 value)
{
	const struct ob_phy_access_ops ops = {
		.read16 = ob_phy_access_read16,
		.write16 = ob_phy_access_write16,
	};

	return ob_phy_rmw16(&ops, hw, reg, mask, value);
}

static bool ob_phy_window_hw_ready(void *ctx)
{
	return ob_phy_access_ready(ctx);
}

static bool ob_phy_window_hw_lost(void *ctx)
{
	const struct ob_hw *hw = ctx;

	return !hw || hw->dev_lost;
}

static int ob_phy_window_hw_select(void *ctx, u16 selector)
{
	struct ob_hw *hw = ctx;

	if (!hw)
		return -22;
	ob_d11_write16(hw, OB_PHY_REG_ADDR_LATCH, selector);
	return hw->dev_lost ? -5 : 0;
}

static int ob_phy_window_hw_read_data(void *ctx, u16 *value)
{
	struct ob_hw *hw = ctx;

	if (!hw || !value)
		return -22;
	*value = ob_d11_read16(hw, OB_PHY_REG_DATA);
	return hw->dev_lost ? -5 : 0;
}

static int ob_phy_window_hw_write_data(void *ctx, u16 value)
{
	struct ob_hw *hw = ctx;

	if (!hw)
		return -22;
	ob_d11_write16(hw, OB_PHY_REG_DATA, value);
	return hw->dev_lost ? -5 : 0;
}

const struct ob_phy_window_ops *ob_phy_window_hw_ops(void)
{
	static const struct ob_phy_window_ops ops = {
		.ready = ob_phy_window_hw_ready,
		.device_lost = ob_phy_window_hw_lost,
		.select16 = ob_phy_window_hw_select,
		.read_data16 = ob_phy_window_hw_read_data,
		.write_data16 = ob_phy_window_hw_write_data,
	};

	return &ops;
}

static int ob_acphy_shm_write16(void *ctx, u16 off, u16 value)
{
	return ob_ucode_write_shm16_checked(ctx, off, value);
}

static int ob_acphy_shm_read16_checked(void *ctx, u16 off, u16 *value)
{
	return ob_ucode_read_shm16_checked(ctx, off, value);
}

static int ob_acphy_shm_write16_checked(void *ctx, u16 off, u16 value)
{
	return ob_ucode_write_shm16_checked(ctx, off, value);
}

static int ob_acphy_d11_read32_trusted(void *ctx, u16 off, u32 *value)
{
	struct ob_hw *hw = ctx;

	if (!hw || !value)
		return -22;
	*value = ob_d11_read32_trusted(hw, "acphy_pre_radio_preread", off);
	return hw->dev_lost ? -19 : 0;
}

static int ob_acphy_d11_write16(void *ctx, u16 off, u16 value)
{
	struct ob_hw *hw = ctx;

	if (!hw)
		return -22;
	ob_d11_write16(hw, off, value);
	return hw->dev_lost ? -19 : 0;
}

static int ob_acphy_axi_rmw32_checked(void *ctx, u16 off, u32 mask,
				      u32 value, u32 *new_value)
{
	return ob_axi_rmw32_checked(ctx, off, mask, value, new_value);
}

static bool ob_acphy_device_lost(void *ctx)
{
	const struct ob_hw *hw = ctx;

	return !hw || hw->dev_lost;
}

int ob_acphy_pre_radio_setup(struct ob_hw *hw)
{
	static const struct ob_acphy_pre_radio_ops ops = {
		.shm_write16 = ob_acphy_shm_write16,
		.d11_read32_trusted = ob_acphy_d11_read32_trusted,
		.d11_write16 = ob_acphy_d11_write16,
		.axi_rmw32_checked = ob_acphy_axi_rmw32_checked,
		.device_lost = ob_acphy_device_lost,
	};
	u16 channel;
	int ret;

	if (!hw || !hw->core || !hw->bus || !hw->core->wrap ||
	    hw->dev_lost || !hw->chanspec_valid || hw->irq.active ||
	    hw->acphy_start_state != OB_ACPHY_START_IN_PROGRESS ||
	    hw->chip_id != 0x4352 || hw->chip_rev != 3 ||
	    hw->core->id.id != BCMA_CORE_80211 || hw->core->id.rev != 42 ||
	    hw->phy_type != 0x0b || !hw->phy_radio_identity_valid ||
	    !hw->radio_access_initialized ||
	    hw->d11_state != OB_D11_POST_UCODE_READY)
		return -22;

	channel = hw->chanspec & 0x00ffu;
	if (channel < 1 || channel > 14 || hw->chanspec != (0x1000u | channel))
		return -95;

	ret = ob_acphy_pre_radio_run(&ops, hw, hw->chanspec,
				     &hw->acphy_pre_radio);
	if (ret)
		dev_err(hw->dev, "acphy-pre-radio: transaction failed: %d\n", ret);
	return ret;
}

/* Normal mac80211 start must report the first missing LPF input when known,
 * then remain gated before the established prefix because row-driven
 * continuation/calibration/channel programming are still incomplete. */
int ob_acphy_init_production_gate(struct ob_hw *hw)
{
	struct ob_phy_lpf_target target;
	enum ob_phy_lpf_input_status input_status;
	bool boardflags3_valid;
	int ret;

	if (!hw || !hw->core || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_PRE_RADIO_IN_PROGRESS)
		return hw && hw->dev_lost ? -EIO : -EINVAL;
	target = ob_acphy_lpf_target_from_hw(hw);
	boardflags3_valid = hw->board.boardflags3_valid &&
		hw->board.boardflags3_source ==
			OB_BOARDFLAGS3_SOURCE_VALIDATED_REV11 &&
		hw->board.revision == 11;
	input_status = ob_phy_acphy_lpf_input_status(&target,
						     boardflags3_valid,
						     hw->board.boardflags3);
	if (hw->bringup_test_only) {
		/*
		 * Explicit incomplete hardware smoke test: retain the LPF evidence in
		 * the log but do not latch a lifecycle blocker, because the documented
		 * REV1_AFTER_SUB92500 stop is the intended endpoint. No calibration or
		 * channel transition is admitted by this bypass.
		 */
		dev_info(hw->dev,
			 "BRINGUP_TEST_GATE_BYPASS lpf_status=%d boardflags3_valid=%u boardflags3=0x%08x\n",
			 (int)input_status, boardflags3_valid ? 1u : 0u,
			 hw->board.boardflags3);
		return 0;
	}
	ret = ob_phy_init_block(&hw->phy_init,
				ob_acphy_lpf_blocker(input_status));
	if (ret)
		return ret;
	/* Remain fail-closed even if the LPF inputs become available: the
	 * row-driven continuation and calibration/channel transitions are not
	 * complete. */
	return -EOPNOTSUPP;
}

/*
 * Enter the shared PHY/radio initialization lifecycle after the validated
 * pre-radio transaction. The prefix runs through sub_9591e, analog_tx_lpf,
 * the rev4/class0 key lookup and sub_92500. The selected continuation is
 * owned by ob_acphy_post_lpf_radio_complete().
 */
static int ob_acphy_radio_prefix_initialize(struct ob_hw *hw)
{
	static const struct ob_phy_sub92500_ops sub92500_ops = {
		.read_shm16 = ob_acphy_shm_read16_checked,
		.write_shm16 = ob_acphy_shm_write16_checked,
	};
	struct ob_radio_target_identity target;
	struct ob_phy_lpf_target lpf_target;
	struct ob_radio2069_lookup_result prefix_lookup, lookup;
	enum ob_phy_lpf_input_status input_status;
	bool boardflags3_valid, hirssi_elnabypass_active;
	int ret;

	if (!hw || !hw->core || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_PRE_RADIO_READY ||
	    hw->acphy_start_state != OB_ACPHY_START_READY_FOR_RADIO_ON ||
	    hw->d11_state != OB_D11_POST_UCODE_READY || !hw->chanspec_valid)
		return hw && hw->dev_lost ? -EIO : -EINVAL;

	target = (struct ob_radio_target_identity){
		.chip_id = hw->chip_id,
		.chip_rev = hw->chip_rev,
		.d11_corerev = hw->core->id.rev,
		.phy_type = hw->phy_type,
		.phy_revision = hw->phy_revision,
		.radio_id = hw->radio_id,
		.radio_revision = hw->radio_revision,
		.radio_access_initialized = hw->radio_access_initialized,
		.valid = hw->phy_radio_identity_valid,
	};
	if (!ob_radio2069_target_applicable(&target))
		return -EOPNOTSUPP;

	/* Reject unsupported board/PHY inputs before the radio prefix can issue
	 * any hardware access. The normal probe validates rev11 boardflags3 from
	 * the external CRC-checked SPROM; absent/invalid data still fails closed. */
	lpf_target = ob_acphy_lpf_target_from_hw(hw);
	boardflags3_valid = hw->board.boardflags3_valid &&
		hw->board.boardflags3_source ==
			OB_BOARDFLAGS3_SOURCE_VALIDATED_REV11 &&
		hw->board.revision == 11;
	input_status = ob_phy_acphy_lpf_input_status(&lpf_target,
						     boardflags3_valid,
						     hw->board.boardflags3);
	if (input_status != OB_PHY_LPF_INPUTS_SUPPORTED) {
		ret = ob_phy_init_block(&hw->phy_init,
					ob_acphy_lpf_blocker(input_status));
		return ret ? ret : -EOPNOTSUPP;
	}
	/* Decode the caller's actual requested chanspec. The pre-LPF radio prefix
	 * is currently proven only for the key-1 row; no 0x1001 default is made. */
	if (hw->radio_revision_class != 0 ||
	    ob_radio2069_rev4_lookup_2g20(hw->chanspec, &prefix_lookup) ||
	    prefix_lookup.key != 1u) {
		ret = ob_phy_init_block(&hw->phy_init,
					OB_PHY_BLOCK_CHANNEL_LOOKUP);
		return ret ? ret : -EOPNOTSUPP;
	}

	ret = ob_phy_init_begin_radio(&hw->phy_init);
	if (ret) {
		dev_warn(hw->dev,
			 "PHY init could not enter radio stage; no radio/table accesses issued\n");
		return ret;
	}

	ret = ob_radio_class0_initialize_to_sub9591e(hw);
	if (ret)
		return ret;
	ret = ob_phy_init_radio_prefix_complete(&hw->phy_init);
	if (ret)
		return ret;

	ret = ob_acphy_analog_tx_lpf_initialize(hw);
	if (ret)
		return ret;

	ret = ob_radio2069_rev4_lookup_2g20(hw->chanspec, &lookup);
	if (ret == -2) {
		ret = ob_phy_init_post_lpf_lookup_complete(&hw->phy_init, false,
			(u8)hw->chanspec, 0xffu, 0xffffu);
		if (ret)
			return ret;
		ret = ob_phy_init_block(&hw->phy_init,
					OB_PHY_BLOCK_POST_LPF_LOOKUP_MISS);
		return ret ? ret : -EOPNOTSUPP;
	}
	if (ret)
		return ret;
	ret = ob_phy_init_post_lpf_lookup_complete(&hw->phy_init, true,
			lookup.key, lookup.row_index, lookup.row_offset);
	if (ret)
		return ret;
	dev_info(hw->dev,
		 "post-lpf lookup key=%u row=%u offset=0x%04x row_word2_valid=%u\n",
		 lookup.key, lookup.row_index, lookup.row_offset,
		 lookup.row_word2_valid ? 1 : 0);
	/* The selected rev1 attach predicate sets nested+0x912. The 4352 class-0
	 * path reaches sub_92500 after lookup and before the remaining row/caller
	 * sequence. Keep its sentinel exchange exact and checked. */
	ret = ob_phy_sub92500_run(&sub92500_ops, hw,
		ob_phy_nested_912_for_revision(hw->phy_revision),
		&hirssi_elnabypass_active);
	if (ret)
		return ret;
	dev_info(hw->dev, "sub_92500 activated=%u\n",
		 hirssi_elnabypass_active ? 1u : 0u);
	ret = ob_phy_init_block(&hw->phy_init,
				OB_PHY_BLOCK_REV1_AFTER_SUB92500);
	return ret ? ret : -EOPNOTSUPP;
}

int ob_acphy_radio_initialize(struct ob_hw *hw)
{
	int ret;

	if (!hw)
		return -EINVAL;
	if (hw->dev_lost)
		return -EIO;
	/* The shared continuation is admitted only for the observed rev1/key-1
	 * route. Reject other identities before the prefix touches hardware. */
	if (hw->phy_revision != 1 || !hw->chanspec_valid ||
	    hw->chanspec != 0x1001 || hw->acphy_a7089_golden_cap ||
	    hw->acphy_table_probe)
		return -EOPNOTSUPP;
	ret = ob_acphy_radio_prefix_initialize(hw);
	if (ret != -EOPNOTSUPP || hw->phy_init.phase != OB_PHY_INIT_BLOCKED ||
	    hw->phy_init.blocker != OB_PHY_BLOCK_REV1_AFTER_SUB92500)
		return ret;
	return ob_acphy_post_lpf_radio_complete(hw, false);
}

/*
 * Isolated radio-ON checkpoint. Runs after ob_acphy_pre_radio_test() has
 * published OB_ACPHY_START_READY_FOR_RADIO_ON, advances the PHY lifecycle to
 * PRE_RADIO_READY and runs only the class-0 radio prefix, analog TX LPF and
 * sub_92500, then blocks at
 * OB_PHY_BLOCK_REV1_AFTER_SUB92500. A block (-EOPNOTSUPP) is the expected STOP;
 * any other error or a dev_lost latch is fail-stop.
 */
int ob_acphy_radio_on_test(struct ob_hw *hw)
{
	int ret;

	if (!hw || !hw->dev)
		return -22;
	if (hw->dev_lost)
		return -EIO;

	/* pre_radio_test() does not touch the PHY-init lifecycle; establish the
	 * PRE_RADIO_READY state the radio entry requires. */
	if (hw->phy_init.phase == OB_PHY_INIT_NOT_STARTED) {
		ret = ob_phy_init_begin(&hw->phy_init);
		if (ret) {
			dev_info(hw->dev, "ACPHY_RADIO_ON_FAIL stage=begin ret=%d\n", ret);
			return ret;
		}
	}
	if (hw->phy_init.phase == OB_PHY_INIT_PRE_RADIO_IN_PROGRESS) {
		ret = ob_phy_init_advance(&hw->phy_init,
					  OB_PHY_INIT_PRE_RADIO_READY);
		if (ret) {
			dev_info(hw->dev, "ACPHY_RADIO_ON_FAIL stage=advance ret=%d\n",
				 ret);
			return ret;
		}
	}

	ret = ob_acphy_radio_prefix_initialize(hw);
	if (ret == -EOPNOTSUPP) {
		dev_info(hw->dev, "ACPHY_RADIO_ON_STOP blocked_at=%s\n",
			 ob_phy_init_blocker_name(hw->phy_init.blocker));
		return 0;
	}
	if (ret) {
		dev_info(hw->dev, "ACPHY_RADIO_ON_FAIL ret=%d blocker=%s\n", ret,
			 ob_phy_init_blocker_name(hw->phy_init.blocker));
		return ret;
	}
	dev_info(hw->dev, "ACPHY_RADIO_ON_PASS\n");
	return 0;
}

int ob_acphy_calibration_initialize(struct ob_hw *hw)
{
	int ret;

	if (!hw || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_RADIO_COMPLETE)
		return hw && hw->dev_lost ? -EIO : -EINVAL;
	ret = ob_phy_init_advance(&hw->phy_init,
				  OB_PHY_INIT_CALIBRATION_IN_PROGRESS);
	if (ret)
		return ret;
	ret = ob_phy_init_block(&hw->phy_init,
				OB_PHY_BLOCK_CALIBRATION_INPUTS);
	return ret ? ret : -EOPNOTSUPP;
}

/*
 * This is the non-mutating fallback for the calibration/channel/PLL task.
 * RADIO_COMPLETE is an established predecessor.  A completed attach identity
 * gives us the selected selector-0 class, and the controlled 0x1001 fixture
 * gives us its exact rev4 table row.  The vendor runtime getter for
 * phycal_tempdelta remains unresolved, so stop before the calibration
 * lifecycle can be entered.
 */
/*
 * Build the calibration-input contract from the live device state.  Pure:
 * no MMIO, no logging.  Shared by the M1-2 admission probe and the M1-3
 * pre-state witness so both judge the same tuple.
 */
static void ob_acphy_cal_contract_input_build(
	struct ob_hw *hw, struct ob_acphy_cal_contract_input *input,
	struct ob_acphy_cal_tempdelta_result *td_result)
{
	struct ob_acphy_cal_tempdelta_input td_input;
	bool target_identity_valid;
	bool selector_from_identity;
	bool controlled_row_known;

	/*
	 * The vendor value is the PHY variable "phycal_tempdelta" from the
	 * NVRAM name=value blob passed to the PHY attach.  No OpenBRCM input
	 * reads that blob yet, so the resolver reports "unread".  The byte is a
	 * periodic recalibration threshold: it is carried as UNKNOWN but never
	 * blocks the initial calibration admission (see the contract header).
	 */
	td_input = (struct ob_acphy_cal_tempdelta_input) { 0 };
	(void)ob_acphy_cal_tempdelta_acphy_resolve(&td_input, td_result);
	target_identity_valid = hw->core && hw->chip_id == 0x4352 &&
		hw->chip_rev == 3 && hw->core->id.rev == 42 &&
		hw->phy_type == 0x0b && hw->phy_revision == 1 &&
		hw->radio_id == 0x2069 && hw->radio_revision == 4 &&
		hw->phy_radio_identity_valid && hw->radio_access_initialized;
	/* hw->radio_revision_class was decoded from raw radio register 0 at attach. */
	selector_from_identity = target_identity_valid &&
		hw->radio_revision_class == OB_ACPHY_CAL_RADIO_SELECTOR_CLASS;
	controlled_row_known = selector_from_identity && hw->chanspec_valid &&
		hw->chanspec == OB_ACPHY_CAL_CONTROLLED_CHANSPEC;
	*input = (struct ob_acphy_cal_contract_input) {
		.target_identity_valid = target_identity_valid,
		.radio_complete = true,
		.chanspec_valid = hw->chanspec_valid,
		.chanspec = hw->chanspec,
		/* Periodic vendor-owned configuration; known only once readable. */
		.periodic_tempdelta_known = td_result->known,
		.pll_selector_known = selector_from_identity,
		.pll_selector_from_identity = selector_from_identity,
		.pll_selector = hw->radio_revision_class,
		.channel_row_known = controlled_row_known,
		.channel_key = OB_ACPHY_CAL_CONTROLLED_CHANNEL_KEY,
		.channel_row_index = OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW,
		.channel_row_offset = OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW_OFFSET,
	};
}

int ob_acphy_calibration_inputs_probe(struct ob_hw *hw)
{
	struct ob_acphy_cal_contract_input input;
	struct ob_acphy_cal_contract_result result;
	struct ob_acphy_cal_tempdelta_result td_result;
	int ret;

	if (!hw || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_RADIO_COMPLETE)
		return hw && hw->dev_lost ? -EIO : -EINVAL;
	ob_acphy_cal_contract_input_build(hw, &input, &td_result);

	ret = ob_acphy_cal_contract_admit(&input, &result);
	dev_info(hw->dev,
		 "ACPHY_CAL_INPUTS_BEGIN chanspec=0x%04x radio_revision_class=%u\n",
		 input.chanspec, hw->radio_revision_class);
	if (td_result.known)
		dev_info(hw->dev,
			 "ACPHY_CAL_INPUT phycal_tempdelta=%u configured=%u clamped=%u source=nvram_var scope=periodic\n",
			 td_result.effective, td_result.configured,
			 td_result.clamped ? 1u : 0u);
	else
		dev_info(hw->dev,
			 "ACPHY_CAL_INPUT phycal_tempdelta=UNKNOWN source=nvram_var:phycal_tempdelta state=UNREAD scope=periodic gate=initial_cal:none\n");
	if (input.pll_selector_known && input.pll_selector_from_identity)
		dev_info(hw->dev,
			 "ACPHY_CAL_INPUT pll_selector=%u source=attach_identity\n",
			 input.pll_selector);
	else
		dev_info(hw->dev,
			 "ACPHY_CAL_INPUT pll_selector=UNKNOWN source=attach_identity_unavailable\n");
	if (input.channel_row_known)
		dev_info(hw->dev,
			 "ACPHY_CAL_INPUT channel_row=%u key=%u offset=0x%04x source=controlled_fixture\n",
			 input.channel_row_index, input.channel_key,
			 input.channel_row_offset);
	else
		dev_info(hw->dev,
			 "ACPHY_CAL_INPUT channel_row=UNKNOWN selected_chanspec=0x%04x\n",
			 input.chanspec);
	if (ret == -95) {
		dev_info(hw->dev,
			 "ACPHY_CAL_CONTRACT_BLOCKED first=%u unknown_mask=0x%02x\n",
			 result.first_blocker, result.unknown_mask);
		dev_info(hw->dev, "ACPHY_CAL_CONTRACT_STOP\n");
		return 0;
	}
	if (ret)
		return ret;
	/* Inputs admitted says nothing about the operation ledger. */
	dev_info(hw->dev,
		 "ACPHY_CAL_CONTRACT_ADMITTED periodic_tempdelta_known=%u\n",
		 result.periodic_tempdelta_known ? 1u : 0u);
	dev_info(hw->dev, "ACPHY_CAL_CONTRACT_STOP\n");
	return 0;
}

/* M1-3 hardware binding: PHY indirect reads only; the ops struct has no write. */
static int ob_acphy_cal_stage_read16(void *ctx, u8 event, u32 source_site,
				     u16 reg, u16 *value)
{
	struct ob_hw *hw = ctx;

	(void)event;
	(void)source_site;
	return ob_phy_read16(hw, reg, value);
}

static int ob_acphy_cal_stage_delay(void *ctx, u8 event, u32 source_site,
				    u16 usec)
{
	(void)ctx;
	(void)event;
	(void)source_site;
	udelay(usec ? usec : 1);
	return 0;
}

static bool ob_acphy_cal_stage_lost(void *ctx)
{
	struct ob_hw *hw = ctx;

	return hw->dev_lost;
}

static const char *ob_acphy_cal_stage_status_name(
	enum ob_acphy_cal_stage_status status)
{
	switch (status) {
	case OB_ACPHY_CAL_STAGE_WITNESS_OK:
		return "WITNESS_OK";
	case OB_ACPHY_CAL_STAGE_READY_AFTER_POLL:
		return "READY_AFTER_POLL";
	case OB_ACPHY_CAL_STAGE_READY_EXHAUSTED:
		return "READY_EXHAUSTED";
	case OB_ACPHY_CAL_STAGE_FAILED_CALLBACK:
		return "FAILED_CALLBACK";
	case OB_ACPHY_CAL_STAGE_FAILED_DEVICE_LOST:
		return "FAILED_DEVICE_LOST";
	default:
		return "INVALID";
	}
}

/*
 * M1-3: read-only calibration pre-state witness, reachable only after the M1-2
 * contract admitted.  Samples PHY[0x0401]/[0x0400]/[0x0070] and the recovered
 * PHY[0x0380] readiness poll, then stops.  It does not advance the PHY-init
 * lifecycle (a witness is not "calibration in progress"), issues no
 * calibration write and publishes no completion token.  The calibration entry
 * block stays blocked because its vendor context bytes are still unowned.
 */
int ob_acphy_calibration_stage_witness(struct ob_hw *hw)
{
	struct ob_acphy_cal_contract_input input;
	struct ob_acphy_cal_contract_result contract;
	struct ob_acphy_cal_tempdelta_result td_result;
	struct ob_acphy_cal_stage_ops ops;
	struct ob_acphy_cal_stage_result stage;
	const char *name;
	int ret;

	if (!hw || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_RADIO_COMPLETE)
		return hw && hw->dev_lost ? -EIO : -EINVAL;

	ob_acphy_cal_contract_input_build(hw, &input, &td_result);
	ret = ob_acphy_cal_contract_admit(&input, &contract);
	if (ret) {
		dev_info(hw->dev,
			 "ACPHY_CAL_STAGE_BLOCKED contract_ret=%d first=%u unknown_mask=0x%02x\n",
			 ret, contract.first_blocker, contract.unknown_mask);
		dev_info(hw->dev,
			 "ACPHY_CAL_STAGE_STOP calibration_issued=0 entry_context_owned=0\n");
		return 0;
	}
	dev_info(hw->dev,
		 "ACPHY_CAL_STAGE_BEGIN contract=ADMITTED periodic_tempdelta_known=%u\n",
		 contract.periodic_tempdelta_known ? 1u : 0u);

	ops = (struct ob_acphy_cal_stage_ops) {
		.phy_read16 = ob_acphy_cal_stage_read16,
		.delay_us = ob_acphy_cal_stage_delay,
		.device_lost = ob_acphy_cal_stage_lost,
	};
	ret = ob_acphy_cal_stage_witness_run(&ops, hw, &stage);
	if (ret) {
		dev_info(hw->dev, "ACPHY_CAL_STAGE_FAIL status=INVALID ret=%d\n",
			 ret);
		return ret;
	}
	name = ob_acphy_cal_stage_status_name(stage.status);

	dev_info(hw->dev,
		 "ACPHY_CAL_STAGE_PRE_STATE phy_0401=0x%04x phy_0400=0x%04x phy_0070=0x%04x\n",
		 stage.phy_0401, stage.phy_0400, stage.phy_0070);
	dev_info(hw->dev,
		 "ACPHY_CAL_STAGE_READY last=0x%04x reads=%u delays=%u exhausted=%u\n",
		 stage.readiness_last, stage.readiness_reads,
		 stage.readiness_delays, stage.readiness_exhausted ? 1u : 0u);
	dev_info(hw->dev, "ACPHY_CAL_STAGE_RESULT status=%s\n", name);
	dev_info(hw->dev,
		 "ACPHY_CAL_STAGE_CONTEXT shared_a5=0x%02x source=accepted_chain_state pi_19c=UNKNOWN shared_a4=UNKNOWN shared_a6=UNKNOWN shared_a7=UNKNOWN shared_31=UNKNOWN shared_34=UNKNOWN reason6_f84=1 source=0xb588b\n",
		 OB_ACPHY_CAL_STAGE_SHARED_A5_2CHAIN);

	if (stage.status == OB_ACPHY_CAL_STAGE_FAILED_CALLBACK ||
	    stage.status == OB_ACPHY_CAL_STAGE_FAILED_DEVICE_LOST) {
		dev_info(hw->dev,
			 "ACPHY_CAL_STAGE_FAIL status=%s site=0x%05x ret=%d\n",
			 name, stage.last_site, stage.failure_ret);
		return stage.failure_ret ? stage.failure_ret : -EIO;
	}
	dev_info(hw->dev,
		 "ACPHY_CAL_STAGE_STOP calibration_issued=0 entry_context_owned=0\n");
	return 0;
}

/* M1-4 hardware binding: the one owned PHY write arm (PHY[0x0070] RMW). */
static int ob_acphy_cal_write_read16(void *ctx, u8 event, u32 source_site,
				     u16 reg, u16 *value)
{
	struct ob_hw *hw = ctx;

	(void)event;
	(void)source_site;
	return ob_phy_read16(hw, reg, value);
}

static int ob_acphy_cal_write_write16(void *ctx, u8 event, u32 source_site,
				      u16 reg, u16 value)
{
	struct ob_hw *hw = ctx;

	(void)event;
	(void)source_site;
	return ob_phy_write16(hw, reg, value);
}

static int ob_acphy_cal_write_delay(void *ctx, u8 event, u32 source_site,
				    u16 usec)
{
	(void)ctx;
	(void)event;
	(void)source_site;
	udelay(usec ? usec : 1);
	return 0;
}

static bool ob_acphy_cal_write_lost(void *ctx)
{
	struct ob_hw *hw = ctx;

	return hw->dev_lost;
}

/*
 * OpenBRCM's shadow of the vendor pi+0xfa0 enable byte.  The vendor saves the
 * byte at 0x0b1497 and restores it at 0x0b184f; this driver has never issued
 * the enable arm, so the shadow is 0.  It is not the vendor's value and the
 * markers say so.
 */
static int ob_acphy_cal_write_enable_state(void *ctx, u8 *value)
{
	struct ob_hw *hw = ctx;

	if (!hw || !value)
		return -EINVAL;
	*value = hw->txpwrctrl_enable_shadow;
	return 0;
}

/*
 * Bracket open, vendor-tolerant: wlapi_suspend_mac_and_wait accepts a MAC that
 * is already down.  This bench's attach path leaves exactly that state
 * (MACCONTROL 0x04020402, path=already_quiesced), where the strict runtime
 * helper ob_d11_suspend_mac_and_wait() would return -22 and the stage would
 * never reach the arm.  The classification is the vendor rule
 * (ob_d11_mac_already_suspended(): EN_MAC == 0, all-ones refused), NOT exact
 * equality with the startup word: the first M1-6 window read 0x84020402 — the
 * startup word with bit 31 set on the read, a bit this driver never wrote —
 * and an equality test returned -22 before phyreg_enter and before any write
 * (exec-20261028T222049Z, M16-B).  The exact word is logged so the difference
 * stays visible.  A MAC this call did not suspend is reported through
 * we_suspended=false and must not be resumed by the close.
 *
 * Documented deviation: the vendor's suspend reads MACCONTROL once inside its
 * own routine; this path may read it once more to classify the state.
 */
static int ob_acphy_cal_write_mac_suspend(void *ctx, u8 event, u32 site,
					  bool *we_suspended)
{
	struct ob_hw *hw = ctx;
	u32 mctrl;
	int ret;

	(void)event;
	(void)site;
	if (!hw || !we_suspended || hw->dev_lost)
		return -EIO;
	*we_suspended = false;
	if (hw->d11_state != OB_D11_POST_UCODE_READY)
		return -22;
	/*
	 * M1-6D: wlc_bmac_suspend_mac_and_wait @0x063a04 sets the suspend wake
	 * override (mask 4) on its 0->1 refcount even when EN_MAC is already
	 * clear, and wlc_bmac_enable_mac clears it again.  With the override
	 * active, wlc_phyreg_enter @0x0bafd1 is MMIO-free (the frozen trace
	 * shows no MACCONTROL write and no SHM 0x40 access at 0x0925fe).
	 * Setting it here, before either suspend path, reproduces that state.
	 */
	ret = ob_d11_suspend_wake_override_enter(hw);
	if (ret)
		return ret;
	/* Suspended earlier by the radio stage or a previous bracket: the
	 * bracket precondition holds, and the close must leave it suspended. */
	if (hw->runtime_mac_suspended)
		return 0;
	mctrl = ob_d11_read32(hw, OB_D11_REG_MACCONTROL);
	if (hw->dev_lost || mctrl == 0xffffffffu)
		return -EIO;
	if (ob_d11_mac_already_suspended(mctrl)) {
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_MAC_OPEN state=already_quiesced maccontrol=0x%08x startup_word=%u resume_on_close=0\n",
			 mctrl,
			 mctrl == OB_D11_STARTUP_QUIESCED_MACCONTROL ? 1u : 0u);
		return 0;
	}
	ret = ob_d11_suspend_mac_and_wait(hw);
	if (!ret)
		*we_suspended = true;
	return ret;
}

static int ob_acphy_cal_write_mac_resume(void *ctx, u8 event, u32 site)
{
	struct ob_hw *hw = ctx;

	(void)event;
	(void)site;
	return ob_d11_enable_mac(hw);
}

static int ob_acphy_cal_write_phyreg_enter(void *ctx, u8 event, u32 site)
{
	struct ob_hw *hw = ctx;

	(void)event;
	(void)site;
	return ob_d11_phyreg_enter(hw);
}

static int ob_acphy_cal_write_phyreg_exit(void *ctx, u8 event, u32 site)
{
	struct ob_hw *hw = ctx;

	(void)event;
	(void)site;
	return ob_d11_phyreg_exit(hw);
}

static const char *ob_acphy_cal_write_status_name(
	enum ob_acphy_cal_write_stage_status status)
{
	switch (status) {
	case OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED:
		return "PRESTATE_DIVERGED";
	case OB_ACPHY_CAL_WRITE_PREDICATE_TRUE_REFUSED:
		return "PREDICATE_TRUE_REFUSED";
	case OB_ACPHY_CAL_WRITE_SECOND_ARM_DIVERGED:
		return "SECOND_ARM_DIVERGED";
	case OB_ACPHY_CAL_WRITE_READBACK_MISMATCH:
		return "READBACK_MISMATCH";
	case OB_ACPHY_CAL_WRITE_POST_STATE_MOVED:
		return "POST_STATE_MOVED";
	case OB_ACPHY_CAL_WRITE_ARM_OK:
		return "ARM_OK";
	case OB_ACPHY_CAL_WRITE_FAILED_CALLBACK:
		return "FAILED_CALLBACK";
	case OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST:
		return "FAILED_DEVICE_LOST";
	case OB_ACPHY_CAL_WRITE_RESTORE_UNSUPPORTED:
		return "RESTORE_UNSUPPORTED";
	case OB_ACPHY_CAL_WRITE_BRACKET_UNSUPPORTED:
		return "BRACKET_UNSUPPORTED";
	case OB_ACPHY_CAL_WRITE_BRACKET_FAILED:
		return "BRACKET_FAILED";
	default:
		return "INVALID";
	}
}

/*
 * M1-6: vendor 0x0070 contract, reachable only after the M1-2 contract
 * admitted.  Re-samples the M1-3 pre-state, refuses unless it still equals the
 * confirmed tuple with readiness clear on the first sample, reads OpenBRCM's
 * shadow of pi+0xfa0, and then runs the vendor shape: MAC suspend, phyreg
 * enter, disable arm, work, restore (= the same disable arm while the saved
 * byte is 0), phyreg exit, MAC resume.  A nonzero saved byte is refused before
 * any side effect, because the vendor restore would take the enable arm and
 * its per-chain estimate table (phy+0x45a) is not owned.  With the confirmed
 * pre-state both writes are value-identity.  No calibration completion is
 * claimed.
 */
int ob_acphy_calibration_write_stage(struct ob_hw *hw)
{
	struct ob_acphy_cal_contract_input input;
	struct ob_acphy_cal_contract_result contract;
	struct ob_acphy_cal_tempdelta_result td_result;
	struct ob_acphy_cal_write_stage_ops ops;
	struct ob_acphy_cal_write_stage_result stage;
	const char *name;
	u32 k;
	int ret;

	if (!hw || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_RADIO_COMPLETE)
		return hw && hw->dev_lost ? -EIO : -EINVAL;

	ob_acphy_cal_contract_input_build(hw, &input, &td_result);
	ret = ob_acphy_cal_contract_admit(&input, &contract);
	if (ret) {
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_BLOCKED contract_ret=%d first=%u unknown_mask=0x%02x\n",
			 ret, contract.first_blocker, contract.unknown_mask);
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_STOP writes=0 calibration_issued=0 entry_context_owned=0\n");
		return 0;
	}
	dev_info(hw->dev,
		 "ACPHY_CAL_WRITE_BEGIN contract=ADMITTED periodic_tempdelta_known=%u\n",
		 contract.periodic_tempdelta_known ? 1u : 0u);

	ops = (struct ob_acphy_cal_write_stage_ops) {
		.phy_read16 = ob_acphy_cal_write_read16,
		.phy_write16 = ob_acphy_cal_write_write16,
		.delay_us = ob_acphy_cal_write_delay,
		.device_lost = ob_acphy_cal_write_lost,
		.enable_state_get = ob_acphy_cal_write_enable_state,
		.mac_suspend = ob_acphy_cal_write_mac_suspend,
		.mac_resume = ob_acphy_cal_write_mac_resume,
		.phyreg_enter = ob_acphy_cal_write_phyreg_enter,
		.phyreg_exit = ob_acphy_cal_write_phyreg_exit,
	};
	ret = ob_acphy_cal_write_stage_run(&ops, hw, &stage);
	if (ret) {
		dev_info(hw->dev, "ACPHY_CAL_WRITE_FAIL status=INVALID ret=%d\n",
			 ret);
		return ret;
	}
	name = ob_acphy_cal_write_status_name(stage.status);

	dev_info(hw->dev,
		 "ACPHY_CAL_WRITE_PRE_STATE phy_0401=0x%04x phy_0400=0x%04x phy_0070=0x%04x ready_last=0x%04x ready_reads=%u ready_delays=%u\n",
		 stage.pre_0401, stage.pre_0400, stage.pre_0070,
		 stage.pre_ready_last, stage.pre_ready_reads,
		 stage.pre_ready_delays);
	dev_info(hw->dev,
		 "ACPHY_CAL_WRITE_ENABLE_STATE value=%u source=ob_hw_shadow restore_supported=%u\n",
		 stage.enable_state_saved, stage.restore_supported ? 1u : 0u);
	dev_info(hw->dev,
		 "ACPHY_CAL_WRITE_BRACKET completed=%u mac_left_suspended=%u phyreg_left_entered=%u\n",
		 stage.bracket_completed ? 1u : 0u,
		 stage.mac_suspended ? 1u : 0u, stage.phyreg_entered ? 1u : 0u);
	for (k = 0; k < stage.arms_sampled; k++) {
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_ROLE index=%u role=%s gate=%s\n",
			 k + 1u, k == 0 ? "disable" : "restore",
			 "vendor_save_disable_restore");
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_ARM index=%u read=0x%04x predicate=%u value=0x%04x site=0x%05x pi_fa0=%u site_pi_fa0=0x%05x\n",
			 k + 1u, stage.arm_read[k],
			 stage.arm_predicate_false[k] ? 0u : 1u,
			 stage.arm_value[k],
			 k == 0 ? OB_ACPHY_CAL_WRITE_SITE_READ_0070 :
				  OB_ACPHY_CAL_WRITE_SITE_RESTORE,
			 stage.enable_state_saved,
			 OB_ACPHY_CAL_WRITE_SITE_PI_FA0);
		if (k >= stage.writes_issued)
			continue;
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_RMW index=%u wrote=0x%04x readback=0x%04x site=0x%05x\n",
			 k + 1u, stage.arm_value[k],
			 stage.arm_readback[k],
			 OB_ACPHY_CAL_WRITE_SITE_RMW_0070);
		/* The settle and the stable sample fall BETWEEN the two
		 * invocations, so the STABLE line is emitted here and not after
		 * the loop: the harness contract is the execution order, and a
		 * STABLE line printed after the second arm would misstate when
		 * the word was sampled. */
		if (k == 0 && stage.arm_readback[0] == stage.arm_value[0])
			dev_info(hw->dev,
				 "ACPHY_CAL_WRITE_STABLE index=1 delay_us=%u value=0x%04x\n",
				 OB_ACPHY_CAL_WRITE_SETTLE_US,
				 stage.stable_0070);
	}

	if (stage.status == OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED ||
	    stage.status == OB_ACPHY_CAL_WRITE_PREDICATE_TRUE_REFUSED ||
	    stage.status == OB_ACPHY_CAL_WRITE_SECOND_ARM_DIVERGED ||
	    stage.status == OB_ACPHY_CAL_WRITE_RESTORE_UNSUPPORTED ||
	    stage.status == OB_ACPHY_CAL_WRITE_BRACKET_UNSUPPORTED) {
		const char *reason;

		switch (stage.status) {
		case OB_ACPHY_CAL_WRITE_PRESTATE_DIVERGED:
			reason = "prestate_diverged";
			break;
		case OB_ACPHY_CAL_WRITE_PREDICATE_TRUE_REFUSED:
			reason = "predicate_true";
			break;
		case OB_ACPHY_CAL_WRITE_RESTORE_UNSUPPORTED:
			reason = "restore_unsupported_enable_state_nonzero";
			break;
		case OB_ACPHY_CAL_WRITE_BRACKET_UNSUPPORTED:
			reason = "bracket_callback_missing";
			break;
		default:
			reason = "second_arm_diverged";
			break;
		}
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_REFUSED reason=%s writes=%u\n", reason,
			 stage.writes_issued);
	}
	if (stage.status == OB_ACPHY_CAL_WRITE_ARM_OK ||
	    stage.status == OB_ACPHY_CAL_WRITE_POST_STATE_MOVED) {
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_POST_STATE phy_0401=0x%04x phy_0400=0x%04x phy_0070=0x%04x phy_0380=0x%04x\n",
			 stage.post_0401, stage.post_0400, stage.post_0070,
			 stage.post_0380);
	}
	dev_info(hw->dev,
		 "ACPHY_CAL_WRITE_RESULT status=%s writes=%u\n", name,
		 stage.writes_issued);
	if (stage.status == OB_ACPHY_CAL_WRITE_FAILED_CALLBACK ||
	    stage.status == OB_ACPHY_CAL_WRITE_FAILED_DEVICE_LOST ||
	    stage.status == OB_ACPHY_CAL_WRITE_BRACKET_FAILED) {
		dev_info(hw->dev,
			 "ACPHY_CAL_WRITE_FAIL status=%s site=0x%05x ret=%d\n",
			 name, stage.failure_site, stage.failure_ret);
		return stage.failure_ret ? stage.failure_ret : -EIO;
	}
	dev_info(hw->dev,
		 "ACPHY_CAL_WRITE_STOP writes=%u calibration_issued=0 entry_context_owned=0\n",
		 stage.writes_issued);
	return 0;
}

int ob_acphy_channel_initialize(struct ob_hw *hw)
{
	int ret;

	if (!hw || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_CALIBRATION_COMPLETE ||
	    !hw->chanspec_valid)
		return hw && hw->dev_lost ? -EIO : -EINVAL;
	ret = ob_phy_init_advance(&hw->phy_init,
				  OB_PHY_INIT_CHANNEL_PROGRAMMING);
	if (ret)
		return ret;
	ret = ob_phy_init_block(&hw->phy_init, OB_PHY_BLOCK_CHANNEL_LOOKUP);
	return ret ? ret : -EOPNOTSUPP;
}

static int ob_acphy_pre_radio_test_fail(struct ob_hw *hw, const char *stage,
					int ret)
{
	if (hw && hw->acphy_start_state == OB_ACPHY_START_IN_PROGRESS)
		ob_acphy_first_up_finish(&hw->acphy_start_state, false);
	dev_err(hw->dev, "ACPHY_PRERADIO_HW_FAIL stage=%s ret=%d\n", stage, ret);
	return ret;
}

int ob_acphy_pre_radio_test(struct ob_hw *hw)
{
	u16 chanspec;
	u16 shm;
	u32 cflags;
	int ret;

	if (!hw || !hw->dev)
		return -22;
	dev_info(hw->dev, "ACPHY_PRERADIO_HW_CP0_ENTRY\n");
	if (!hw->core || !hw->bus || !hw->core->wrap || hw->dev_lost ||
	    hw->chip_id != 0x4352 || hw->chip_rev != 3 ||
	    hw->core->id.id != BCMA_CORE_80211 || hw->core->id.rev != 42 ||
	    hw->phy_type != 0x0b || hw->phy_revision != 1 ||
	    hw->radio_id != 0x2069 || hw->radio_revision != 4 ||
	    !hw->phy_radio_identity_valid || !hw->radio_access_initialized ||
	    hw->d11_state != OB_D11_POST_UCODE_READY)
		return ob_acphy_pre_radio_test_fail(hw, "identity", -22);
	dev_info(hw->dev, "ACPHY_PRERADIO_HW_CP1_IDENTITY_OK\n");

	ret = ob_test_chanspec_fixture(hw->chip_id, hw->chip_rev, hw->core->id.rev,
				       hw->phy_type, hw->phy_revision, hw->radio_id,
				       hw->radio_revision, &chanspec);
	if (ret)
		return ob_acphy_pre_radio_test_fail(hw, "fixture", ret);
	hw->chanspec = chanspec;
	hw->chanspec_valid = true;
	dev_info(hw->dev, "ACPHY_PRERADIO_HW_CP2_CHANSPEC_1001\n");
	/*
	 * P51 runtime-identity harvest (read-only). These are the hardware-derived
	 * selectors the vendor branch logic consumes: pi+0x164 (phy_revision),
	 * pi+0x16e (radio_revision_class), pi+0x17e (chanspec). Logged for the KB;
	 * no behavior change.
	 */
	dev_info(hw->dev,
		 "RUNTIME_IDENTITY chip=0x%04x chip_rev=%u d11_rev=%u phy_type=0x%02x phy_revision=%u radio_id=0x%04x radio_revision=%u radio_revision_class=%u chanspec=0x%04x\n",
		 hw->chip_id, hw->chip_rev, hw->core->id.rev, hw->phy_type,
		 hw->phy_revision, hw->radio_id, hw->radio_revision,
		 hw->radio_revision_class, hw->chanspec);

	ret = ob_acphy_first_up_begin(&hw->acphy_start_state);
	if (ret)
		return ob_acphy_pre_radio_test_fail(hw, "first_up", ret);
	ret = ob_acphy_pre_radio_setup(hw);
	if (ret)
		return ob_acphy_pre_radio_test_fail(hw, "transaction", ret);
	dev_info(hw->dev, "ACPHY_PRERADIO_HW_CP3_TRANSACTION_DONE\n");

	if (hw->dev_lost || !hw->chanspec_valid || hw->chanspec != 0x1001 ||
	    hw->acphy_start_state != OB_ACPHY_START_IN_PROGRESS ||
	    !hw->acphy_pre_radio.bandwidth_valid ||
	    hw->acphy_pre_radio.bandwidth_bits != 0x1000 ||
	    hw->acphy_pre_radio.state_offset_465 != 0 ||
	    hw->acphy_pre_radio.state_offset_f88 != 0)
		return ob_acphy_pre_radio_test_fail(hw, "software_postcondition", -5);

	shm = ob_ucode_read_shm16(hw, 0x00a0);
	if (hw->dev_lost || shm != 0x1001)
		return ob_acphy_pre_radio_test_fail(hw, "shm_postcondition", -5);
	dev_info(hw->dev, "ACPHY_PRERADIO_HW_CP4_SHM_OK\n");

	cflags = ob_axi_read32(hw, 0x0408);
	if (hw->dev_lost || (cflags & 0x000000c0u) != 0x00000040u)
		return ob_acphy_pre_radio_test_fail(hw, "cflags_postcondition", -5);
	dev_info(hw->dev, "ACPHY_PRERADIO_HW_CP5_CFLAGS_OK\n");

	/*
	 * P55/LPF-input read-only probe (no PHY/radio writes, no lifecycle
	 * change). Reads the rev11 board data through the read-only SPROM path
	 * and reports whether the analog-TX-LPF arm is admissible on the target.
	 */
	{
		struct ob_phy_lpf_target t = {
			.identity_valid = hw->phy_radio_identity_valid,
			.chip_id = hw->chip_id,
			.chip_revision = hw->chip_rev,
			.d11_core_id = hw->core->id.id,
			.d11_revision = hw->core->id.rev,
			.phy_type = hw->phy_type,
			.phy_revision = hw->phy_revision,
			.radio_id = hw->radio_id,
			.radio_revision = hw->radio_revision,
			.radio_revision_class = hw->radio_revision_class,
		};
		int bret = ob_si_prepare_board_data_for_d3a1(hw);
		enum ob_phy_lpf_input_status st =
			ob_phy_acphy_lpf_input_status(&t, hw->board.valid,
						      hw->board.boardflags3);

		dev_info(hw->dev,
			 "LPF_INPUT status=%d board_prep=%d boardflags=0x%08x boardflags3=0x%08x board_valid=%d nested348_bit=%d\n",
			 (int)st, bret, hw->board.boardflags, hw->board.boardflags3,
			 hw->board.valid,
			 !!(hw->board.boardflags3 & OB_PHY_LPF_NESTED_348_BOARD_BIT));
	}

	/* Publish READY only after all pre-radio hardware postconditions pass. */
	ob_acphy_first_up_finish(&hw->acphy_start_state, true);
	dev_info(hw->dev, "ACPHY_PRERADIO_HW_PASS\n");
	return 0;
}

/* ---------------------------------------------------------------- *
 * W2: calibration-entry replay binding (one bracketed window).
 *
 * The stage owns the window; this binding supplies the callbacks, the
 * reference-counted MAC shim of W2-P2, and the kernel-side 43-operation
 * setstate ledger.  The host-only a7089 reconstruction of the same vendor body
 * stays the reference model and is not linked into the module.
 * ---------------------------------------------------------------- */

struct ob_acphy_cal_w2_ctx {
	struct ob_hw *hw;
	const struct ob_acphy_cal_w2_ops *ops;
	u32 mac_depth;
	bool mac_entered;
};

static int ob_acphy_cal_w2_read16(void *ctx, u8 event, u32 site, u16 reg,
				  u16 *value)
{
	struct ob_acphy_cal_w2_ctx *w = ctx;

	return ob_acphy_cal_write_read16(w->hw, event, site, reg, value);
}

static int ob_acphy_cal_w2_write16(void *ctx, u8 event, u32 site, u16 reg,
				   u16 value)
{
	struct ob_acphy_cal_w2_ctx *w = ctx;

	return ob_acphy_cal_write_write16(w->hw, event, site, reg, value);
}

static int ob_acphy_cal_w2_delay(void *ctx, u8 event, u32 site, u16 usec)
{
	struct ob_acphy_cal_w2_ctx *w = ctx;

	return ob_acphy_cal_write_delay(w->hw, event, site, usec);
}

static bool ob_acphy_cal_w2_lost(void *ctx)
{
	struct ob_acphy_cal_w2_ctx *w = ctx;

	return w && w->hw && ob_acphy_cal_write_lost(w->hw);
}

/* W2-P2: refcounted nesting.  A suspend on an already-quiesced MAC is an outer
 * owner's hold: depth increments without a transition and the enable below
 * never lifts that pause. */
static int ob_acphy_cal_w2_mac_hold(void *ctx, u8 event, u32 site,
				    bool *transitioned)
{
	struct ob_acphy_cal_w2_ctx *w = ctx;
	bool we_suspended = false;
	int ret;

	if (!w || !transitioned || !w->hw || w->hw->dev_lost)
		return -EIO;
	*transitioned = false;
	if (w->mac_depth) {
		w->mac_depth++;
		return 0;
	}
	ret = ob_acphy_cal_write_mac_suspend(w->hw, event, site,
					     &we_suspended);
	if (ret)
		return ret;
	w->mac_depth = 1;
	w->mac_entered = we_suspended;
	*transitioned = we_suspended;
	return 0;
}

static int ob_acphy_cal_w2_mac_suspend(void *ctx, u8 event, u32 site,
				       bool *transitioned)
{
	return ob_acphy_cal_w2_mac_hold(ctx, event, site, transitioned);
}

static int ob_acphy_cal_w2_mac_resume(void *ctx, u8 event, u32 site)
{
	struct ob_acphy_cal_w2_ctx *w = ctx;
	bool entered;

	if (!w || !w->hw || w->hw->dev_lost)
		return -EIO;
	if (!w->mac_depth)
		return -22;
	w->mac_depth--;
	if (w->mac_depth)
		return 0;
	entered = w->mac_entered;
	w->mac_entered = false;
	if (!entered)
		return 0;
	return ob_acphy_cal_write_mac_resume(w->hw, event, site);
}

static int ob_acphy_cal_w2_setstate(void *ctx, u8 event, u32 site, u8 state,
				    bool *software_only)
{
	struct ob_acphy_cal_w2_ctx *w = ctx;
	struct ob_acphy_cal_w2_setstate_result res;
	int ret;

	(void)event;
	(void)site;
	if (!w || !w->ops || !software_only)
		return -EIO;
	*software_only = false;
	ret = ob_acphy_cal_w2_setstate_run(w->ops, w, state, true, &res);
	if (!ret)
		*software_only = res.software_only;
	return ret;
}

/*
 * K1 (offline kick-value reachability) failed on 2026-10-30: [rbp-0x18c] is
 * written twice, at 0xac0fc and 0xad334, and 0xad334 dominates the 0xad412
 * load that feeds the 0x0380 write at 0xad420.  The 0xac0fc value is consumed
 * at 0xacbea, so the 0x0300/0x0400 candidate is not the reaching definition.
 * The window therefore does not write PHY[0x0380]; the run records
 * KICK_SKIPPED and R5-C stays untested, not refuted.
 */
static const bool ob_acphy_cal_w2_kick_allowed = false;
static const u16 ob_acphy_cal_w2_kick_value = 0x0000;

static const char *ob_acphy_cal_w2_status_name(enum ob_acphy_cal_w2_status s)
{
	switch (s) {
	case OB_ACPHY_CAL_W2_ENTRY_REFUSED:
		return "ENTRY_REFUSED";
	case OB_ACPHY_CAL_W2_SOFTWARE_ONLY:
		return "SOFTWARE_ONLY";
	case OB_ACPHY_CAL_W2_SETSTATE_DIVERGED:
		return "SETSTATE_DIVERGED";
	case OB_ACPHY_CAL_W2_POST_REFUSED:
		return "POST_REFUSED";
	case OB_ACPHY_CAL_W2_ARM_DIVERGED:
		return "ARM_DIVERGED";
	case OB_ACPHY_CAL_W2_BRACKET_UNSUPPORTED:
		return "BRACKET_UNSUPPORTED";
	case OB_ACPHY_CAL_W2_BRACKET_FAILED:
		return "BRACKET_FAILED";
	case OB_ACPHY_CAL_W2_READBACK_MISMATCH:
		return "READBACK_MISMATCH";
	case OB_ACPHY_CAL_W2_POST_STATE_MOVED:
		return "POST_STATE_MOVED";
	case OB_ACPHY_CAL_W2_KICK_EXHAUSTED:
		return "KICK_EXHAUSTED";
	case OB_ACPHY_CAL_W2_KICK_SKIPPED:
		return "KICK_SKIPPED";
	case OB_ACPHY_CAL_W2_FULL:
		return "FULL";
	case OB_ACPHY_CAL_W2_FAILED_CALLBACK:
		return "FAILED_CALLBACK";
	case OB_ACPHY_CAL_W2_FAILED_DEVICE_LOST:
		return "FAILED_DEVICE_LOST";
	default:
		return "INVALID";
	}
}

int ob_acphy_calibration_w2_replay(struct ob_hw *hw)
{
	struct ob_acphy_cal_contract_input input;
	struct ob_acphy_cal_contract_result contract;
	struct ob_acphy_cal_tempdelta_result td_result;
	struct ob_acphy_cal_w2_ops ops;
	struct ob_acphy_cal_w2_result stage;
	struct ob_acphy_cal_w2_ctx w2;
	const char *name;
	int ret;

	if (!hw || hw->dev_lost || hw->irq.active ||
	    hw->phy_init.phase != OB_PHY_INIT_RADIO_COMPLETE)
		return hw && hw->dev_lost ? -EIO : -EINVAL;

	ob_acphy_cal_contract_input_build(hw, &input, &td_result);
	ret = ob_acphy_cal_contract_admit(&input, &contract);
	if (ret) {
		dev_info(hw->dev,
			 "ACPHY_CAL_W2_BLOCKED contract_ret=%d first=%u unknown_mask=0x%02x\n",
			 ret, contract.first_blocker, contract.unknown_mask);
		dev_info(hw->dev,
			 "ACPHY_CAL_W2_STOP writes=0 calibration_issued=0 entry_context_owned=0\n");
		return 0;
	}
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_BEGIN contract=ADMITTED periodic_tempdelta_known=%u\n",
		 contract.periodic_tempdelta_known ? 1u : 0u);

	w2.hw = hw;
	w2.ops = NULL;
	w2.mac_depth = 0;
	w2.mac_entered = false;
	ops = (struct ob_acphy_cal_w2_ops) {
		.phy_read16 = ob_acphy_cal_w2_read16,
		.phy_write16 = ob_acphy_cal_w2_write16,
		.delay_us = ob_acphy_cal_w2_delay,
		.device_lost = ob_acphy_cal_w2_lost,
		.mac_suspend = ob_acphy_cal_w2_mac_suspend,
		.mac_resume = ob_acphy_cal_w2_mac_resume,
		.phyreg_enter = ob_acphy_cal_write_phyreg_enter,
		.phyreg_exit = ob_acphy_cal_write_phyreg_exit,
		.setstate = ob_acphy_cal_w2_setstate,
	};
	w2.ops = &ops;
	ret = ob_acphy_cal_w2_replay_run(&ops, &w2,
					 ob_acphy_cal_w2_kick_allowed,
					 ob_acphy_cal_w2_kick_value, &stage);
	if (ret) {
		dev_info(hw->dev, "ACPHY_CAL_W2_FAIL status=INVALID ret=%d\n",
			 ret);
		return ret;
	}
	name = ob_acphy_cal_w2_status_name(stage.status);

	dev_info(hw->dev,
		 "ACPHY_CAL_W2_PRE phy_0401=0x%04x phy_0400=0x%04x phy_0070=0x%04x phy_0380=0x%04x\n",
		 stage.entry_0401, stage.entry_0400, stage.entry_0070,
		 stage.entry_0380);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_SETSTATE called=%u software_only=%u\n",
		 stage.setstate_called ? 1u : 0u,
		 stage.setstate_software_only ? 1u : 0u);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_POST phy_0401=0x%04x phy_0400=0x%04x phy_0070=0x%04x phy_0380=0x%04x\n",
		 stage.post_0401, stage.post_0400, stage.post_0070,
		 stage.post_0380);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_BRACKET completed=%u mac_left_suspended=%u phyreg_left_entered=%u\n",
		 stage.bracket_completed ? 1u : 0u,
		 stage.mac_suspended ? 1u : 0u,
		 stage.phyreg_entered ? 1u : 0u);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_ARM index=1 read=0x%04x value=0x%04x readback=0x%04x stable=0x%04x site=0x%05x\n",
		 stage.arm_read[0], stage.arm_value[0], stage.arm_readback[0],
		 stage.stable_0070, OB_ACPHY_CAL_W2_SITE_ARM_DISABLE);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_KICK allowed=%u written=%u skipped=%u exhausted=%u pre=0x%04x value=0x%04x reads=%u delays=%u\n",
		 stage.kick_allowed ? 1u : 0u, stage.writes_kick ? 1u : 0u,
		 stage.kick_skipped ? 1u : 0u,
		 stage.kick_exhausted ? 1u : 0u, stage.kick_pre,
		 stage.kick_value, stage.kick_reads, stage.kick_delays);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_ARM index=2 read=0x%04x value=0x%04x readback=0x%04x site=0x%05x\n",
		 stage.arm_read[1], stage.arm_value[1], stage.arm_readback[1],
		 OB_ACPHY_CAL_W2_SITE_ARM_RESTORE);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_IDENTITY phy_0401=0x%04x phy_0400=0x%04x phy_0070=0x%04x phy_0380=0x%04x\n",
		 stage.post_identity_0401, stage.post_identity_0400,
		 stage.post_identity_0070, stage.post_identity_0380);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_RESULT status=%s writes_0070=%u writes_kick=%u site=0x%05x\n",
		 name, stage.writes_0070, stage.writes_kick, stage.last_site);
	dev_info(hw->dev,
		 "ACPHY_CAL_W2_STOP writes=%u calibration_issued=0 entry_context_owned=0\n",
		 stage.writes_0070 + stage.writes_kick);
	return 0;
}

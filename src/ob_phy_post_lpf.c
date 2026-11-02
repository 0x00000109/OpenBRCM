/* SPDX-License-Identifier: GPL-2.0-only */
#include "ob_phy_post_lpf.h"
#include "ob_phy_lpf.h"

#ifdef __KERNEL__
#include <linux/delay.h>
#include "ob_core.h"
#include "ob_guard.h"
#include "ob_radio.h"
#include "ob_phy_a7089_cal.h"
#include "ob_phy_a0be4.h"
#include "ob_phy_96203.h"
#endif

enum ob_a7089_event_kind {
	OB_A7089_READ,
	OB_A7089_WRITE,
	OB_A7089_WRITE_DERIVED,
	OB_A7089_RMW_PHY,
	OB_A7089_RMW_RADIO,
	OB_A7089_OR_PHY,
	OB_A7089_DELAY,
	OB_A7089_PHYCLK_FGC,
};

enum ob_a7089_expression_kind {
	OB_A7089_EXPR_LITERAL,
	OB_A7089_EXPR_AND,
	OB_A7089_EXPR_OR,
	OB_A7089_EXPR_AND_NOT_OR,
};

struct ob_a7089_event {
	u8 logical_event;
	enum ob_phy_post_lpf_domain domain;
	u32 source_site;
	enum ob_a7089_event_kind kind;
	u16 reg;
	u16 mask;
	u16 value;
	u16 duration_us;
	enum ob_a7089_expression_kind expression;
	u16 expression_a;
	u16 expression_b;
	u8 expression_source_event;
};

#include "ob_phy_post_lpf_vector.h"

static int ob_a7089_validate_fixture(
	const struct ob_phy_post_lpf_fixture *fixture)
{
	if (!fixture || !fixture->explicitly_synthetic)
		return -22;
	if (fixture->chip_id != 0x4352 || fixture->chip_revision != 3 ||
	    (fixture->phy_revision != 0x0a && fixture->phy_revision != 1) ||
	    fixture->radio_id != 0x2069 ||
	    fixture->radio_revision != 4 || fixture->radio_class != 0 ||
	    fixture->requested_chanspec != 0x1001 || fixture->lookup_key != 1 ||
	    fixture->row_index != 63 || fixture->pi_16e_class != 0 ||
	    fixture->pi_164_phy_revision != fixture->phy_revision ||
	    fixture->pi_168_radio_class != 2)
		return -95;
	return 0;
}

static int ob_a7089_eval_value(
	const struct ob_a7089_event *event, const u16 read_values[],
	const bool read_valid[], u16 *value)
{
	u16 source;

	if (!event || !value)
		return -22;
	if (event->expression == OB_A7089_EXPR_LITERAL) {
		*value = event->value;
		return 0;
	}
	if (!event->expression_source_event ||
	    event->expression_source_event >= event->logical_event ||
	    !read_valid[event->expression_source_event])
		return -22;
	source = read_values[event->expression_source_event];
	switch (event->expression) {
	case OB_A7089_EXPR_AND:
		*value = (u16)(source & event->expression_a);
		return 0;
	case OB_A7089_EXPR_OR:
		*value = (u16)(source | event->expression_b);
		return 0;
	case OB_A7089_EXPR_AND_NOT_OR:
		*value = (u16)((source & (u16)~event->expression_a) |
			       event->expression_b);
		return 0;
	case OB_A7089_EXPR_LITERAL:
	default:
		return -22;
	}
}

static int ob_a7089_emit_read(const struct ob_phy_post_lpf_ops *ops,
			      void *ctx, const struct ob_a7089_event *event,
			      u16 *value, u32 *callback_count)
{
	int ret;

	(*callback_count)++;
	ret = ops->read16(ctx, event->logical_event, event->source_site,
			  event->domain, event->reg, value);
	return ret;
}

static int ob_a7089_emit_write(const struct ob_phy_post_lpf_ops *ops,
			       void *ctx, const struct ob_a7089_event *event,
			       u16 value, u32 *callback_count)
{
	(*callback_count)++;
	return ops->write16(ctx, event->logical_event, event->source_site,
			    event->domain, event->reg, value);
}

int ob_phy_acphy_a7089_key1_row63_test_only(
	const struct ob_phy_post_lpf_ops *ops, void *ctx,
	const struct ob_phy_post_lpf_fixture *fixture,
	const struct ob_phy_post_lpf_entry_state *entry_state,
	struct ob_phy_post_lpf_result *result)
{
	u16 read_values[OB_A7089_EVENT_COUNT + 1] = { 0 };
	bool read_valid[OB_A7089_EVENT_COUNT + 1] = { false };
	u32 callback_count = 0;
	unsigned int i;
	int ret;

	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us ||
	    !ops->phyclk_fgc || !fixture || !entry_state || !result)
		return -22;
	ret = ob_a7089_validate_fixture(fixture);
	if (ret)
		return ret;
	if (!fixture->lookup_matched)
		return -2;
	/*
	 * The selected vector reads no [nested+0x912] inside its range (the
	 * 0xa71ea and 0xaa446 reads are outside 0xa7262..0xaa404), so 912 only
	 * has to be consistent with the revision-derived value
	 * (pi+0x164 <= 1), not necessarily 0.
	 */
	if (entry_state->nested_32c != 1 || entry_state->nested_32e != 0xff ||
	    entry_state->nested_912 != fixture->nested_912 ||
	    fixture->nested_912 != (fixture->pi_164_phy_revision <= 1 ? 1u : 0u))
		return -95;
	if (OB_A7089_EVENT_COUNT != 88)
		return -22;

	for (i = 0; i < OB_A7089_EVENT_COUNT; i++) {
		const struct ob_a7089_event *event = &ob_a7089_events[i];
		u16 old_value, value, new_value;

		if (event->logical_event != i + 1)
			return -22;
		switch (event->kind) {
		case OB_A7089_READ:
			ret = ob_a7089_emit_read(ops, ctx, event, &value,
						 &callback_count);
			if (ret)
				return ret;
			read_values[event->logical_event] = value;
			read_valid[event->logical_event] = true;
			break;
		case OB_A7089_WRITE:
			ret = ob_a7089_eval_value(event, read_values, read_valid,
						  &value);
			if (ret)
				return ret;
			ret = ob_a7089_emit_write(ops, ctx, event, value,
						  &callback_count);
			if (ret)
				return ret;
			break;
		case OB_A7089_WRITE_DERIVED:
			ret = ob_a7089_eval_value(event, read_values, read_valid,
						  &value);
			if (ret)
				return ret;
			ret = ob_a7089_emit_write(ops, ctx, event, value,
						  &callback_count);
			if (ret)
				return ret;
			break;
		case OB_A7089_RMW_PHY:
		case OB_A7089_RMW_RADIO:
			ret = ob_a7089_eval_value(event, read_values, read_valid,
						  &value);
			if (ret)
				return ret;
			ret = ob_a7089_emit_read(ops, ctx, event, &old_value,
						 &callback_count);
			if (ret)
				return ret;
			if (event->kind == OB_A7089_RMW_PHY) {
				new_value = (u16)((old_value & (u16)~event->mask) |
						  (value & event->mask));
			} else {
				/* Historical mod_radio_reg(value, mask) formula. */
				new_value = (u16)((old_value & (u16)~value) |
						  (value & event->mask));
			}
			ret = ob_a7089_emit_write(ops, ctx, event, new_value,
						  &callback_count);
			if (ret)
				return ret;
			break;
		case OB_A7089_OR_PHY:
			ret = ob_a7089_emit_read(ops, ctx, event, &old_value,
						 &callback_count);
			if (ret)
				return ret;
			ret = ob_a7089_emit_write(ops, ctx, event,
						  (u16)(old_value | event->value),
						  &callback_count);
			if (ret)
				return ret;
			break;
		case OB_A7089_DELAY:
			callback_count++;
			ret = ops->delay_us(ctx, event->logical_event,
					    event->source_site, event->duration_us);
			if (ret)
				return ret;
			break;
		case OB_A7089_PHYCLK_FGC:
			callback_count++;
			ret = ops->phyclk_fgc(ctx, event->logical_event,
					      event->source_site,
					      event->value != 0);
			if (ret)
				return ret;
			break;
		default:
			return -95;
		}
	}

	result->kind = OB_PHY_POST_LPF_SELECTED_FIXTURE_COMPLETE;
	result->logical_event_count = OB_A7089_EVENT_COUNT;
	result->callback_count = callback_count;
	result->hardware_ready = false;
	return 0;
}

struct ob_lpf_adapter {
	const struct ob_phy_post_lpf_ops *ops;
	void *ctx;
};

static int ob_lpf_adapter_read(void *opaque, u16 reg, u16 *value)
{
	struct ob_lpf_adapter *adapter = opaque;

	return adapter->ops->read16(adapter->ctx, 0, 0,
				    OB_PHY_POST_LPF_PHY, reg, value);
}

static int ob_lpf_adapter_write(void *opaque, u16 reg, u16 value)
{
	struct ob_lpf_adapter *adapter = opaque;

	return adapter->ops->write16(adapter->ctx, 0, 0,
				     OB_PHY_POST_LPF_PHY, reg, value);
}

int ob_phy_acphy_post_lpf_compose_fixture_test_only(
	const struct ob_phy_post_lpf_ops *ops, void *ctx,
	const struct ob_phy_post_lpf_fixture *fixture,
	struct ob_phy_post_lpf_composed_result *result)
{
	const struct ob_phy_access_ops lpf_ops = {
		.read16 = ob_lpf_adapter_read,
		.write16 = ob_lpf_adapter_write,
	};
	struct ob_lpf_adapter adapter = { .ops = ops, .ctx = ctx };
	struct ob_phy_post_lpf_entry_state entry_state;
	struct ob_phy_post_lpf_result block_result;
	struct ob_phy_post_lpf_composed_result composed;
	int ret;

	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us ||
	    !ops->phyclk_fgc || !fixture || !result)
		return -22;
	ret = ob_a7089_validate_fixture(fixture);
	if (ret)
		return ret;
	if (!fixture->lookup_matched)
		return -2;

	ret = ob_phy_acphy_lpf_complete_test_only(&lpf_ops, &adapter,
						  fixture->chip_id,
						  fixture->phy_revision,
						  &composed.lpf_saved_019e);
	if (ret)
		return ret;

	/* sub_b018f's accepted stores immediately after the LPF return. */
	entry_state.nested_32c = 1;
	entry_state.nested_32e = 0xff;
	entry_state.nested_912 = fixture->nested_912;
	ret = ob_phy_acphy_a7089_key1_row63_test_only(
		ops, ctx, fixture, &entry_state, &block_result);
	if (ret)
		return ret;
	composed.selected_block = block_result;
	*result = composed;
	return 0;
}

#ifdef __KERNEL__

/* ---- HW-backed post-LPF (sub_a7089) + sub_a0be4 continuation -------------- */

static int ob_postlpf_hw_read16(void *ctx, u8 ev, u32 site,
				enum ob_phy_post_lpf_domain domain, u16 reg,
				u16 *value)
{
	struct ob_hw *hw = ctx;
	(void)ev;
	(void)site;
	return domain == OB_PHY_POST_LPF_RADIO ?
		ob_radio_read16(hw, reg, value) : ob_phy_read16(hw, reg, value);
}

static int ob_postlpf_hw_write16(void *ctx, u8 ev, u32 site,
				 enum ob_phy_post_lpf_domain domain, u16 reg,
				 u16 value)
{
	struct ob_hw *hw = ctx;
	(void)ev;
	(void)site;
	return domain == OB_PHY_POST_LPF_RADIO ?
		ob_radio_write16(hw, reg, value) : ob_phy_write16(hw, reg, value);
}

static int ob_postlpf_hw_delay(void *ctx, u8 ev, u32 site, u16 usec)
{
	(void)ctx;
	(void)ev;
	(void)site;
	udelay(usec ? usec : 1);
	return 0;
}

static int ob_postlpf_hw_phyclk(void *ctx, u8 ev, u32 site, bool enabled)
{
	struct ob_hw *hw = ctx;
	u32 v;
	(void)ev;
	(void)site;
	/*
	 * Vendor wlc_bmac_phyclk_fgc: for phy_type in {4,7,0x0b} it performs
	 * si_core_cflags(mask=0x2, value = on ? 0x2 : 0), i.e. an RMW of core
	 * cflags bit 1 (FGC) at wrapper 0x408 - NOT bit 0 (CLK). Writing bit 0
	 * here would disable the D11 core clock and hang subsequent MMIO.
	 */
	if (hw->phy_type != 4 && hw->phy_type != 7 && hw->phy_type != 0x0b)
		return 0;
	v = ob_axi_read32(hw, 0x0408);
	v = (v & ~0x00000002u) | (enabled ? 0x00000002u : 0u);
	ob_axi_write32(hw, 0x0408, v);
	return 0;
}

static int ob_a0be4_hw_read_ai(void *ctx, u8 ev, u32 site, u16 off, u32 *value)
{
	(void)ev;
	(void)site;
	*value = ob_axi_read32(ctx, off);
	return 0;
}

static int ob_a0be4_hw_write_ai(void *ctx, u8 ev, u32 site, u16 off, u32 value)
{
	(void)ev;
	(void)site;
	ob_axi_write32(ctx, off, value);
	return 0;
}

static const struct ob_phy_a0be4_ops ob_postlpf_hw_ops = {
	.phy = {
		.read16 = ob_postlpf_hw_read16,
		.write16 = ob_postlpf_hw_write16,
		.delay_us = ob_postlpf_hw_delay,
		.phyclk_fgc = ob_postlpf_hw_phyclk,
	},
	.read_ai_core32 = ob_a0be4_hw_read_ai,
	.write_ai_core32 = ob_a0be4_hw_write_ai,
};

/*
 * Continue the production radio entry past OB_PHY_BLOCK_REV1_AFTER_SUB92500
 * with the pinned, oracle-validated post-LPF vector (sub_a7089) and the
 * sub_a0be4 vector, using hardware accessors. The entry state is the
 * instruction-proven one for rev 1: nested_32c=1, nested_32e=0xff,
 * nested_912=(pi_164<=1)=1. Fail-stop on any accessor error or dev_lost.
 */
static int ob_a7089cal_hw_write16(void *ctx, u8 ev, u16 reg, u16 value)
{
	(void)ev;
	return ob_radio_write16(ctx, reg, value);
}

static int ob_a7089cal_hw_read16(void *ctx, u8 ev, u16 reg, u16 *value)
{
	(void)ev;
	return ob_radio_read16(ctx, reg, value);
}

static int ob_a7089cal_hw_write16_phy(void *ctx, u8 ev, u16 reg, u16 value)
{
	(void)ev;
	return ob_phy_write16(ctx, reg, value);
}

static int ob_a7089cal_hw_read16_phy(void *ctx, u8 ev, u16 reg, u16 *value)
{
	(void)ev;
	return ob_phy_read16(ctx, reg, value);
}

static void ob_a7089cal_hw_delay(void *ctx, u16 usec)
{
	(void)ctx;
	udelay(usec ? usec : 1);
}

static void ob_a7089cal_hw_write_dp(void *ctx, u16 value)
{
	ob_d11_write16(ctx, 0x3fe, value);
}
static int ob_a7089cal_hw_read_dp(void *ctx, u16 *value)
{
	*value = ob_d11_read16(ctx, 0x3fe);
	return 0;
}

static void ob_a7089cal_hw_w16d11(void *ctx, u16 off, u16 v){ ob_d11_write16(ctx, off, v); }
static void ob_a7089cal_hw_w32d11(void *ctx, u16 off, u32 v){ ob_d11_write32(ctx, off, v); }
static u16 ob_a7089cal_hw_r16d11(void *ctx, u16 off){ return ob_d11_read16(ctx, off); }
static u32 ob_a7089cal_hw_r32d11(void *ctx, u16 off){ return ob_d11_read32(ctx, off); }

static const struct ob_a7089_cal_ops ob_a7089cal_hw_ops = {
	.write16 = ob_a7089cal_hw_write16,
	.read16 = ob_a7089cal_hw_read16,
	.write16_phy = ob_a7089cal_hw_write16_phy,
	.read16_phy = ob_a7089cal_hw_read16_phy,
	.delay_us = ob_a7089cal_hw_delay,
	.write_dp = ob_a7089cal_hw_write_dp,
	.read_dp = ob_a7089cal_hw_read_dp,
	.write16_d11 = ob_a7089cal_hw_w16d11,
	.write32_d11 = ob_a7089cal_hw_w32d11,
	.read16_d11 = ob_a7089cal_hw_r16d11,
	.read32_d11 = ob_a7089cal_hw_r32d11,
};

/*
 * W96: the vendor runs sub_96203 for both chains once per attach, 15.9 ms
 * after its LPF prefix. Measured on the recorded vendor stream: block at
 * 531.629464 (chain 0) and 531.629649 (chain 1), both with saved
 * 0x739/0x73a/0x725 = 0x0000/0x0180/0x0600 and the order
 * rmw739 -> rmw725 -> res725 -> res739 -> res73a. The golden table in
 * ob_phy_a7089_golden.h is radio-only and contains none of this block's
 * registers, so this call cannot duplicate the replay that follows.
 */
static int ob_96203_live_stage(struct ob_hw *hw)
{
	struct ob_phy_96203_live_input in;
	struct ob_phy_96203_result res;
	int ret;

	if (!hw->phy_radio_identity_valid) {
		dev_info(hw->dev,
			 "ACPHY_96203_LIVE_REFUSED reason=identity_not_published\n");
		return -EINVAL;
	}
	memset(&in, 0, sizeof(in));
	in.identity_valid = true;
	/* Measured, not read: the vendor touches 0x07xx and 0x09xx only. */
	in.loop_bound = 2;
	in.acphychipid = hw->chip_id;
	in.acphychipid_owned = true;
	in.second_loop_gate_owned = hw->acphy_96203_gate != 0xffffu;
	in.second_loop_gate = (u8)(hw->acphy_96203_gate & 0xffu);
	dev_info(hw->dev,
		 "ACPHY_96203_LIVE_BEGIN loop_bound=%u acphychipid=0x%04x gate=%u gate_owned=%u gate_source=%s\n",
		 in.loop_bound, in.acphychipid, in.second_loop_gate,
		 in.second_loop_gate_owned,
		 in.second_loop_gate_owned ? "operator_hypothesis" : "unowned");
	ret = ob_phy_96203_live_run(&ob_postlpf_hw_ops.phy, hw, &in, &res);
	if (ret) {
		dev_info(hw->dev, "ACPHY_96203_LIVE_%s ret=%d site=0x%x\n",
			 res.status == OB_96203_INVALID ? "REFUSED" : "FAIL",
			 ret, res.failed_site);
		return ret;
	}
	dev_info(hw->dev,
		 "ACPHY_96203_LIVE_DONE cores=%u inner=%u callbacks=%u boundary=0x%x\n",
		 res.outer_cores_completed, res.inner_cores_completed,
		 res.callback_count, res.terminal_boundary);
	return 0;
}

int ob_acphy_post_lpf_radio_complete(struct ob_hw *hw, bool channel_probe)
{
	struct ob_a7089_cal_result gres;
	int ret;

	if (!hw || hw->dev_lost)
		return hw ? -EIO : -EINVAL;
	if (hw->phy_revision != 1 || hw->chanspec != 0x1001 ||
	    hw->acphy_a7089_golden_cap ||
	    hw->phy_init.phase != OB_PHY_INIT_BLOCKED ||
	    hw->phy_init.blocker != OB_PHY_BLOCK_REV1_AFTER_SUB92500 ||
	    !hw->phy_init.radio_prefix_complete ||
	    !hw->phy_init.analog_tx_lpf_complete ||
	    !hw->phy_init.post_lpf_lookup_complete ||
	    !hw->phy_init.post_lpf_lookup_found ||
	    hw->phy_init.post_lpf_lookup_key != 1 ||
	    hw->phy_init.post_lpf_lookup_row != 63 ||
	    hw->phy_init.post_lpf_lookup_row_offset != 0x1c8c)
		return -EINVAL;
	if (hw->acphy_96203_live) {
		ret = ob_96203_live_stage(hw);
		if (ret)
			return ret;
	}

	/*
	 * Golden-radio mainline: replay the vendor's exact radio-init MMIO stream
	 * (cal0..end, diff-verified 100% vs the vendor `wl` oracle). This replaces
	 * the earlier partial a7089-vector/cal/postcal path, which diverged because
	 * it did not interleave cal rows with their inter-cal blocks. PHY replay in
	 * this path is refused (proven to hang the device); only radio ops run.
	 */
	ret = ob_acphy_a7089_golden_unit_run(&ob_a7089cal_hw_ops, hw, &gres,
					       hw->acphy_a7089_golden_cap);
	if (ret) {
		dev_info(hw->dev, "ACPHY_A7089_GOLDEN_FAIL ret=%d\n", ret);
		return ret;
	}
	if (hw->dev_lost)
		return -EIO;
	dev_info(hw->dev, "ACPHY_A7089_GOLDEN_DONE ops=%u\n", gres.callback_count);

	/* Semantic AC-PHY init (tables + individual PHY/D11 + MAC-handshake), hardware-validated. */
	ret = ob_acphy_init_run(hw);
	if (ret) {
		dev_info(hw->dev, "ACPHY_INIT_RUN FAIL ret=%d\n", ret);
		return ret;
	}
	dev_info(hw->dev, "ACPHY_INIT_DONE\n");
	if (channel_probe) {
		int cr = ob_acphy_set_channel(hw, 0x0e29);
		dev_info(hw->dev, "ACPHY_CHAN_SWITCH ret=%d\n", cr);
		if (cr)
			return cr;
	}

	ret = ob_phy_init_resume_radio(&hw->phy_init);
	if (ret) {
		dev_info(hw->dev, "ACPHY_POSTLPF_FAIL stage=resume ret=%d\n", ret);
		return ret;
	}
	ret = ob_phy_init_radio_sequence_complete(&hw->phy_init);
	if (ret) {
		dev_info(hw->dev, "ACPHY_POSTLPF_FAIL stage=seq ret=%d\n", ret);
		return ret;
	}
	ret = ob_phy_init_advance(&hw->phy_init, OB_PHY_INIT_RADIO_COMPLETE);
	if (ret) {
		dev_info(hw->dev, "ACPHY_POSTLPF_FAIL stage=advance ret=%d\n", ret);
		return ret;
	}
	dev_info(hw->dev, "ACPHY_RADIO_COMPLETE\n");
	return 0;
}

int ob_acphy_radio_postlpf_test(struct ob_hw *hw)
{
	int ret;

	if (!hw || hw->dev_lost)
		return hw ? -EIO : -EINVAL;
	if (hw->phy_revision != 1 || hw->chanspec != 0x1001)
		return -EINVAL;

	ret = ob_acphy_radio_on_test(hw); /* pre-radio + prefix + LPF + sub92500 */
	if (ret) {
		dev_info(hw->dev, "ACPHY_POSTLPF_FAIL stage=prefix ret=%d\n", ret);
		return ret;
	}
	dev_info(hw->dev, "ACPHY_POSTLPF prefix complete (blocked_at=%s)\n",
		 ob_phy_init_blocker_name(hw->phy_init.blocker));
	return ob_acphy_post_lpf_radio_complete(hw, hw->acphy_table_probe);
}

#endif /* __KERNEL__ */

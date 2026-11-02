/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_9a539.h"
#include "ob_phy_9a121.h"

static int call_result(struct ob_phy_9a539_result *r, u32 site, int error)
{
	r->callback_count++;
	if (error)
		r->failed_site = site;
	return error;
}

static int phy_read(const struct ob_phy_9a539_ops *o, void *ctx,
		    struct ob_phy_9a539_result *r, u32 site, u16 reg, u16 *v)
{
	return call_result(r, site, o->phy_read16(ctx, site, reg, v));
}
static int phy_write(const struct ob_phy_9a539_ops *o, void *ctx,
		     struct ob_phy_9a539_result *r, u32 site, u16 reg, u16 v)
{
	return call_result(r, site, o->phy_write16(ctx, site, reg, v));
}
static int phy_mod(const struct ob_phy_9a539_ops *o, void *ctx,
		   struct ob_phy_9a539_result *r, u32 site, u16 reg,
		   u16 mask, u16 value)
{
	u16 old;
	int ret = phy_read(o, ctx, r, site, reg, &old);
	if (ret)
		return ret;
	ret = phy_write(o, ctx, r, site, reg,
			(u16)((old & (u16)~mask) | (value & mask)));
	if (!ret)
		r->logical_event_count++;
	return ret;
}

static void mark_unsupported(struct ob_phy_9a539_result *r, u32 site)
{
	r->status = OB_9A539_UNSUPPORTED_HISTORICAL_OPERATION;
	r->first_unsupported_site = site;
	r->first_unimplemented_operation = site;
}

struct ob_9a539_child_context {
	const struct ob_phy_9a539_ops *ops;
	void *ctx;
	struct ob_phy_9a539_result *result;
	u8 *coefficient_matrix;
	bool apply_99658_copy;
};

static int child_phy_read(void *opaque, u32 site, u16 reg, u16 *value)
{
	struct ob_9a539_child_context *c = opaque;
	return call_result(c->result, site,
			   c->ops->phy_read16(c->ctx, site, reg, value));
}

static int child_phy_write(void *opaque, u32 site, u16 reg, u16 value)
{
	struct ob_9a539_child_context *c = opaque;
	return call_result(c->result, site,
			   c->ops->phy_write16(c->ctx, site, reg, value));
}

static int child_radio_read(void *opaque, u32 site, u16 reg, u16 *value)
{
	struct ob_9a539_child_context *c = opaque;
	if (!c->ops->radio_read16)
		return -EINVAL;
	return call_result(c->result, site,
			   c->ops->radio_read16(c->ctx, site, reg, value));
}

static int child_radio_write(void *opaque, u32 site, u16 reg, u16 value)
{
	struct ob_9a539_child_context *c = opaque;
	if (!c->ops->radio_write16)
		return -EINVAL;
	return call_result(c->result, site,
			   c->ops->radio_write16(c->ctx, site, reg, value));
}

static int child_table_write(void *opaque, u32 site, u8 argument,
			     u8 chain, u8 table_id, u8 offset,
			     u8 count, u8 width,
			     const u8 *payload, u8 payload_length)
{
	struct ob_9a539_child_context *c = opaque;
	u16 nested_offset, matrix_offset;
	u8 expected_table;
	unsigned i;

	if (!c->ops->table_write)
		return -EINVAL;
	if (c->apply_99658_copy) {
		if (!c->coefficient_matrix || (width != 8) ||
		    payload_length != count || count > 7 ||
		    (argument != 1 && argument != 2) || chain > 1)
			return -EINVAL;
		if (table_id == (chain ? 0x64 : 0x44)) {
			nested_offset = (u16)(0x046a + argument * 0x0au +
					      chain * 0x78u);
		} else if (table_id == (chain ? 0x65 : 0x45)) {
			nested_offset = (u16)(0x04a6 + argument * 0x0au +
					      chain * 0x78u);
		} else {
			return -EINVAL;
		}
		if (site == 0x99875)
			expected_table = chain ? 0x64 : 0x44;
		else if (site == 0x998aa)
			expected_table = chain ? 0x65 : 0x45;
		else
			return -EINVAL;
		if (table_id != expected_table)
			return -EINVAL;
		if (nested_offset < 0x0450)
			return -EINVAL;
		matrix_offset = (u16)(nested_offset - 0x0450);
		if ((unsigned)matrix_offset + payload_length > 0x122u)
			return -EINVAL;
		/* The historical memcpy is immediately before this TABLE call. */
		for (i = 0; i < payload_length; i++)
			c->coefficient_matrix[matrix_offset + i] = payload[i];
	}
	return call_result(c->result, site,
			   c->ops->table_write(c->ctx, site, argument, chain,
					       table_id, offset, count, width,
					       payload, payload_length));
}

static int child_99f29_table_write(void *opaque, u32 site, u8 table_id,
				   u8 offset, u8 count, u8 width,
				   const u8 *payload, u8 payload_length)
{
	u8 core;

	if (table_id < 0xf9 || table_id > 0xfa)
		return -EINVAL;
	core = (u8)(table_id - 0xf9);
	return child_table_write(opaque, site, 0, core, table_id, offset,
				 count, width, payload, payload_length);
}

static int run_998cc_arg1(const struct ob_phy_9a539_ops *ops, void *ctx,
			  struct ob_phy_9a539_result *r,
			  const struct ob_phy_9a539_input *in,
			  const struct ob_phy_post_farrow_state *inner)
{
	struct ob_phy_998cc_input child = { 0 };
	struct ob_phy_99658_ops child_ops = { .table_write = child_table_write };
	struct ob_phy_99658_result child_result;
	struct ob_9a539_child_context c = {
		.ops = ops, .ctx = ctx, .result = r,
	};
	int ret;

	child.explicitly_synthetic = true;
	child.policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED;
	child.chip_id = in->chip_id;
	child.chip_revision = in->chip_revision;
	child.phy_revision = in->phy_revision;
	child.radio_id = in->radio_id;
	child.radio_revision = in->radio_revision;
	child.radio_class = in->radio_class;
	child.chain_count = in->chain_count;
	child.argument = 1;
	child.table_offset_present = true;
	child.table_offset = 8;
	child.nested_66c_present = true;
	child.nested_66c = inner->nested_668[4];
	child.nested_66d_present = true;
	child.nested_66d = inner->nested_668[5];
	if (!ops->table_write)
		return -EINVAL;
	ret = ob_phy_acphy_998cc_test_only(&child_ops, &c, &child,
					    &child_result);
	if (ret)
		return ret;
	r->logical_event_count++;
	r->sub_998cc_arg1_called = true;
	r->nested_65a_after = child_result.copied_state_value;
	return 0;
}

static int run_99658_arg1(const struct ob_phy_9a539_ops *ops, void *ctx,
			  struct ob_phy_9a539_result *r,
			  const struct ob_phy_9a539_input *in,
			  const struct ob_phy_post_farrow_state *inner)
{
	struct ob_phy_99658_input child = { 0 };
	struct ob_phy_99658_ops child_ops = { .table_write = child_table_write };
	struct ob_phy_99658_result child_result;
	struct ob_9a539_child_context c = {
		.ops = ops, .ctx = ctx, .result = r,
		.coefficient_matrix = r->coefficient_matrix_after,
		.apply_99658_copy = true,
	};
	int ret;

	child.explicitly_synthetic = true;
	child.policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED;
	child.chip_id = in->chip_id;
	child.chip_revision = in->chip_revision;
	child.phy_revision = in->phy_revision;
	child.radio_id = in->radio_id;
	child.radio_revision = in->radio_revision;
	child.radio_class = in->radio_class;
	child.chain_count = in->chain_count;
	child.argument = 1;
	child.chanspec_present = true;
	child.chanspec = in->chanspec;
	child.table_offset_present = true;
	child.table_offset = 8;
	child.transfer_count_present = true;
	if (!in->nested_table_counts_present ||
	    in->nested_64b_table_count != 6 || !in->per_core_matrix_present)
		return -EINVAL;
	child.transfer_count = in->nested_64b_table_count;
	child.nested_66a_present = true;
	child.nested_66a = inner->nested_668[2];
	child.source_id = (in->chanspec & 0xc000u) ?
		OB_PHY_99658_ARG1_OTHER_BAND : OB_PHY_99658_ARG1_CHANSPEC_LOW;
	child.source_bytes = in->arg1_table_source;
	child.source_length = sizeof(in->arg1_table_source);
	ret = ob_phy_acphy_99658_test_only(&child_ops, &c, &child,
					    &child_result);
	if (ret)
		return ret;
	r->logical_event_count = (u8)(r->logical_event_count +
					 child_result.table_api_write_count);
	r->sub_99658_arg1_called = true;
	r->nested_658_after = child_result.copied_state_value;
	return 0;
}

static int run_99658_arg2(const struct ob_phy_9a539_ops *ops, void *ctx,
			  struct ob_phy_9a539_result *r,
			  const struct ob_phy_9a539_input *in,
			  const struct ob_phy_post_farrow_state *inner)
{
	struct ob_phy_99658_input child = { 0 };
	struct ob_phy_99658_ops child_ops = { .table_write = child_table_write };
	struct ob_phy_99658_result child_result;
	struct ob_9a539_child_context c = {
		.ops = ops, .ctx = ctx, .result = r,
		.coefficient_matrix = r->coefficient_matrix_after,
		.apply_99658_copy = true,
	};
	int ret;

	child.explicitly_synthetic = true;
	child.policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED;
	child.chip_id = in->chip_id;
	child.chip_revision = in->chip_revision;
	child.phy_revision = in->phy_revision;
	child.radio_id = in->radio_id;
	child.radio_revision = in->radio_revision;
	child.radio_class = in->radio_class;
	child.chain_count = in->chain_count;
	child.argument = 2;
	child.chanspec_present = true;
	child.chanspec = in->chanspec;
	child.table_offset_present = true;
	child.table_offset = 16;
	child.transfer_count_present = true;
	if (!in->nested_table_counts_present ||
	    in->nested_64c_table_count != 7 || !in->per_core_matrix_present)
		return -EINVAL;
	child.transfer_count = in->nested_64c_table_count;
	child.nested_66b_present = true;
	child.nested_66b = inner->nested_668[3];
	child.nested_66e_present = true;
	child.nested_66e = inner->nested_668[6];
	child.nested_340_present = true;
	child.nested_340 = in->nested_340;
	child.nested_3e0_present = true;
	child.nested_3e0 = in->nested_3e0;
	child.source_id = OB_PHY_99658_ARG2_HIGH_BAND;
	child.source_bytes = in->arg2_table_source;
	child.source_length = sizeof(in->arg2_table_source);
	ret = ob_phy_acphy_99658_test_only(&child_ops, &c, &child,
					    &child_result);
	if (ret)
		return ret;
	r->logical_event_count = (u8)(r->logical_event_count +
					     child_result.table_api_write_count);
	r->sub_99658_arg2_called = true;
	r->nested_659_after = child_result.copied_state_value;
	return 0;
}

static int run_998cc_arg2(const struct ob_phy_9a539_ops *ops, void *ctx,
			  struct ob_phy_9a539_result *r,
			  const struct ob_phy_9a539_input *in,
			  const struct ob_phy_post_farrow_state *inner)
{
	struct ob_phy_998cc_input child = { 0 };
	struct ob_phy_99658_ops child_ops = { .table_write = child_table_write };
	struct ob_phy_99658_result child_result;
	struct ob_9a539_child_context c = {
		.ops = ops, .ctx = ctx, .result = r,
	};
	int ret;

	child.explicitly_synthetic = true;
	child.policy = OB_PHY_99658_SYNTHETIC_PARAMETERIZED;
	child.chip_id = in->chip_id;
	child.chip_revision = in->chip_revision;
	child.phy_revision = in->phy_revision;
	child.radio_id = in->radio_id;
	child.radio_revision = in->radio_revision;
	child.radio_class = in->radio_class;
	child.chain_count = in->chain_count;
	child.argument = 2;
	child.table_offset_present = true;
	child.table_offset = 16;
	child.nested_66c_present = true;
	child.nested_66c = inner->nested_668[4];
	child.nested_66d_present = true;
	child.nested_66d = inner->nested_668[5];
	child.source_id = OB_PHY_99658_998CC_STATIC_ROW;
	child.source_bytes = in->arg2_998cc_source;
	child.source_length = sizeof(in->arg2_998cc_source);
	ret = ob_phy_acphy_998cc_test_only(&child_ops, &c, &child,
					    &child_result);
	if (ret)
		return ret;
	r->logical_event_count++;
	r->sub_998cc_arg2_called = true;
	r->nested_65b_after = child_result.copied_state_value;
	return 0;
}

static int run_9a121_internal(const struct ob_phy_9a539_ops *ops,
			       void *ctx,
			       struct ob_phy_9a539_result *r,
			       const struct ob_phy_9a539_input *in,
			       const struct ob_phy_post_farrow_state *inner)
{
	struct ob_phy_9a121_input child = { 0 };
	struct ob_phy_9a121_ops child_ops = {
		.phy_read16 = child_phy_read,
		.phy_write16 = child_phy_write,
		.radio_read16 = child_radio_read,
		.radio_write16 = child_radio_write,
		.table_write = child_99f29_table_write,
	};
	struct ob_phy_9a121_result child_result;
	struct ob_9a539_child_context c = {
		.ops = ops, .ctx = ctx, .result = r,
	};
	int ret;

	if (!in->a121_state_present || !in->active_core_mask_present)
		return -EINVAL;
	child.explicitly_synthetic = true;
	child.policy = OB_PHY_9A121_SYNTHETIC_PARAMETERIZED;
	child.chip_id = in->chip_id;
	child.chip_revision = in->chip_revision;
	child.phy_revision = in->phy_revision;
	child.radio_id = in->radio_id;
	child.radio_revision = in->radio_revision;
	child.radio_class = in->radio_class;
	child.chain_count = in->chain_count;
	child.chanspec_present = true;
	child.chanspec = in->chanspec;
	child.nested_910_present = true;
	child.nested_910 = in->nested_910;
	child.nested_911_present = true;
	child.nested_911 = in->nested_911;
	child.nested_340_present = true;
	child.nested_340 = in->nested_340;
	child.nested_341_present = true;
	child.nested_341 = inner->nested_341;
	child.nested_66e_present = true;
	child.nested_66e = inner->nested_668[6];
	child.core_mask_present = true;
	child.core_mask = in->active_core_mask;
	if (child.core_mask) {
		if (!r->coefficient_matrix_after_present ||
		    !in->per_core_limits_present || !ops->radio_read16 ||
		    !ops->radio_write16 || !ops->table_write)
			return -EINVAL;
		child.nested_coefficients_present = true;
		memcpy(child.nested_coefficients, r->coefficient_matrix_after,
		       sizeof(child.nested_coefficients));
		child.nested_64a_64f_present = true;
		memcpy(child.nested_64a_64f, in->per_core_limits,
		       sizeof(child.nested_64a_64f));
		child.nested_66f_present = true;
		child.nested_66f = inner->nested_668[7];
	}
	ret = ob_phy_9a121_test_only(&child_ops, &c, &child, &child_result);
	if (ret)
		return ret;
	r->logical_event_count = (u8)(r->logical_event_count +
					 child_result.logical_event_count);
	r->sub_9a121_called = true;
	return 0;
}

static int run_outer_99f29(const struct ob_phy_9a539_ops *ops, void *ctx,
			   struct ob_phy_9a539_result *r,
			   const struct ob_phy_9a539_input *in,
			   const struct ob_phy_post_farrow_state *inner,
			   u8 core, u8 delta)
{
	struct ob_phy_9a121_input child = { 0 };
	struct ob_phy_9a121_ops child_ops = {
		.phy_write16 = child_phy_write,
		.table_write = child_99f29_table_write,
	};
	struct ob_phy_9a121_result child_result;
	struct ob_9a539_child_context c = {
		.ops = ops, .ctx = ctx, .result = r,
	};
	static const u8 high_seed[6] = { 0x2f, 0x2f, 0x2f, 0x34, 0x34, 0x64 };
	int ret;

	child.explicitly_synthetic = true;
	child.policy = OB_PHY_9A121_SYNTHETIC_PARAMETERIZED;
	child.chip_id = in->chip_id;
	child.chip_revision = in->chip_revision;
	child.phy_revision = in->phy_revision;
	child.radio_id = in->radio_id;
	child.radio_revision = in->radio_revision;
	child.radio_class = in->radio_class;
	child.chain_count = in->chain_count;
	child.nested_coefficients_present = r->coefficient_matrix_after_present;
	memcpy(child.nested_coefficients, r->coefficient_matrix_after_present ?
		r->coefficient_matrix_after : in->per_core_matrix,
	       sizeof(child.nested_coefficients));
	child.nested_64a_64f_present = in->per_core_limits_present;
	memcpy(child.nested_64a_64f, in->per_core_limits,
	       sizeof(child.nested_64a_64f));
	ret = ob_phy_9a121_sub99f29_selector0_test_only(
		&child_ops, &c, &child, high_seed, core, delta,
		inner->nested_668[6], &child_result);
	if (ret)
		return ret;
	r->logical_event_count = (u8)(r->logical_event_count +
					     child_result.logical_event_count);
	r->per_core_return[core] = child_result.child_returns[0];
	r->per_core_selector0_argument[core] = delta;
	memcpy(r->per_core_generated[core], child_result.sub99f29_generated, 6);
	r->per_core_called_mask |= (u8)(1u << core);
	return 0;
}

/* Nonzero paths are explicit synthetic fixtures. Low-band static-row copies
 * are represented as caller-supplied, provenance-labeled byte snapshots. */
static int prepare_nonzero(const struct ob_phy_9a539_input *in,
			   struct ob_phy_9a539_result *r,
			   struct ob_phy_post_farrow_state *entry,
			   struct ob_phy_post_farrow_state *inner,
			   u8 *sub8f41b_arg, u8 *sub8f84d_arg,
			   bool *call_99658_arg1,
				   bool *call_99658_arg2,
				   bool *call_998cc_arg1,
				   bool *call_998cc_arg2)
{
	struct ob_phy_post_farrow_software_result sw;
	u8 cap, minus_one, count, delta, loop_index;
	int ret;

	if (!in->entry_software_state_present ||
	    !in->entry_software_bytes_present ||
	    !in->entry_software_flags_present ||
	    !in->inner_software_state_present ||
	    !in->inner_software_bytes_present ||
	    !in->inner_software_flags_present ||
	    !in->nested_8a8_state_present ||
	    !in->active_core_mask_present ||
	    !in->phy_mode_present || !in->nested_42_present ||
	    !in->nested_compare_state_present)
		return -EINVAL;
	if (in->entry_software_state.initial_chanspec != in->chanspec ||
	    in->inner_software_state.initial_chanspec != in->chanspec)
		return -EINVAL;
	ret = ob_phy_post_farrow_continue_software_test_only(
		&in->entry_software_state, entry, &sw);
	if (ret)
		return -EINVAL;
	ret = ob_phy_post_farrow_continue_software_test_only(
		&in->inner_software_state, inner, &sw);
	if (ret)
		return -EINVAL;
	r->entry_nested_670_present = true;
	r->entry_nested_670 = entry->nested_668[8];
	r->inner_nested_670_present = true;
	r->inner_nested_670 = inner->nested_668[8];
	r->sub_909fd_invocations = 2;
	if (!in->argument_present || in->argument == 0 ||
	    in->argument != r->entry_nested_670 ||
	    in->nested_8a8_present || entry->nested_8a8_has_record ||
	    inner->nested_8a8_has_record)
		return -EINVAL;
	if (in->active_core_mask & (u8)~0x03u)
		return -EINVAL;
	if (in->phy_mode != 0) {
		mark_unsupported(r, 0x8f435);
		return -EOPNOTSUPP;
	}
	*call_99658_arg1 = in->nested_65c != inner->nested_668[6] ||
		in->nested_658 != inner->nested_668[2];
	if (*call_99658_arg1 && !in->arg1_table_source_present) {
		mark_unsupported(r, 0x9a6e7);
		return -EOPNOTSUPP;
	}
	if (*call_99658_arg1 &&
	    (!in->nested_table_counts_present ||
	     in->nested_64b_table_count != 6 || !in->per_core_matrix_present)) {
		mark_unsupported(r, 0x99686);
		return -EOPNOTSUPP;
	}
	*call_99658_arg2 = in->nested_65c != inner->nested_668[6] ||
		in->nested_659 != inner->nested_668[3];
	if (*call_99658_arg2 &&
	    (!in->arg2_table_source_present || !in->nested_340_present ||
	     !in->nested_3e0_present)) {
		mark_unsupported(r, 0x9a71a);
		return -EOPNOTSUPP;
	}
	if (*call_99658_arg2 &&
	    (!in->nested_table_counts_present ||
	     in->nested_64c_table_count != 7 || !in->per_core_matrix_present)) {
		mark_unsupported(r, 0x99686);
		return -EOPNOTSUPP;
	}
	*call_998cc_arg1 = in->nested_65a != inner->nested_668[4];
	*call_998cc_arg2 = in->nested_65b != inner->nested_668[5];
	if (*call_998cc_arg2 && !in->arg2_998cc_source_present) {
		mark_unsupported(r, 0x9a75c);
		return -EOPNOTSUPP;
	}
	if ((*call_99658_arg1 || *call_99658_arg2 || *call_998cc_arg1 ||
	     *call_998cc_arg2) && !in->a121_state_present) {
		mark_unsupported(r, 0x9a76a);
		return -EOPNOTSUPP;
	}
	if (in->active_core_mask &&
	    (!in->per_core_matrix_present || !in->per_core_limits_present)) {
		mark_unsupported(r, 0x9a7a5);
		return -EOPNOTSUPP;
	}
	/* Both bands clamp cap and form r13=max(cap-1,0). High band uses
	 * min(r13,12) as count. Low band reads count from the copied .rodata
	 * row at 0x9a687; the three complete row snapshots must be explicit. */
	cap = inner->nested_668[0] > 0x30 ? 0x30 : inner->nested_668[0];
	minus_one = cap > 1 ? (u8)(cap - 1) : 0;
	if (in->chanspec & 0xc000u) {
		count = minus_one > 0x0c ? 0x0c : minus_one;
	} else {
		u8 nested_index = inner->nested_668[1] > 0x18 ?
			0x18 : inner->nested_668[1];
		u8 row_index = (u8)(((u16)nested_index + 1u) >> 1);

		if (!in->low_band_rows_present ||
		    in->low_band_row_source !=
			    OB_9A539_LOW_BAND_ROWS_EXPLICIT_SYNTHETIC_FIXTURE) {
			mark_unsupported(r, 0x9a687);
			return -EOPNOTSUPP;
		}
		if (row_index > 0x0c)
			row_index = 0x0c;
		/* The vendor copies .rodata+0x2cc700 to [rbp-0x50],
		 * which supplies the count at 0x9a687. .rodata+0x2cc730
		 * is [rbp-0x40], the byte consumed by PHY[0x0299]. */
		count = in->low_band_row_2cc700[row_index];
		if (count > 0x0c)
			count = 0x0c;
		r->low_band_row_selected = true;
		r->low_band_row_index = row_index;
		r->low_band_row_count = count;
		r->low_band_phy_0299_value = in->low_band_row_2cc730[row_index];
		r->low_band_phy_03c1_value =
			(u16)in->low_band_row_2cc710[row_index * 2u] |
			((u16)in->low_band_row_2cc710[row_index * 2u + 1u] << 8);
	}
	delta = minus_one > count ? (u8)(minus_one - count) : 0;
	if (delta > 0x1e)
		delta = 0x1e;
	loop_index = (u8)(((u16)delta * 0x58u) >> 5);
	loop_index = (u8)(loop_index + 0x36u);
	if (in->nested_42 > loop_index)
		loop_index = in->nested_42;
	*sub8f41b_arg = loop_index;
	/* sub_8f84d receives 0x36 + ((max(r13-0x15,0)*0x58)>>5). */
	delta = minus_one > 0x15 ? (u8)(minus_one - 0x15) : 0;
	loop_index = (u8)(((u16)delta * 0x58u) >> 5);
	*sub8f84d_arg = (u8)(loop_index + 0x36u);
	r->nested_656_after = cap;
	r->nested_657_after = inner->nested_668[1] > 0x18 ?
		0x18 : inner->nested_668[1];
	return 0;
}

static int run_sub8f41b_mode0(const struct ob_phy_9a539_ops *ops, void *ctx,
			      struct ob_phy_9a539_result *r, u8 value)
{
	static const u16 group0_regs[] = {
		0x0324, 0x0330, 0x0321, 0x032d,
		0x032a, 0x0336, 0x0327, 0x0333,
	};
	static const u32 group0_sites[] = {
		0x8f64c, 0x8f661, 0x8f676, 0x8f68b,
		0x8f6a0, 0x8f6b5, 0x8f6ca, 0x8f829,
	};
	static const u16 group1_regs[] = {
		0x0910, 0x0910, 0x0912, 0x0912,
		0x0911, 0x0911, 0x0913, 0x0913,
	};
	static const u16 group1_masks[] = {
		0xff00, 0x00ff, 0xff00, 0x00ff,
		0x00ff, 0xff00, 0x00ff, 0xff00,
	};
	static const u32 group1_sites[] = {
		0x8f6f1, 0x8f706, 0x8f71b, 0x8f730,
		0x8f745, 0x8f75a, 0x8f76f, 0x8f829,
	};
	static const u16 group2_regs[] = {
		0x0b10, 0x0b10, 0x0b12, 0x0b12,
		0x0b11, 0x0b11, 0x0b13, 0x0b13,
	};
	static const u16 group2_masks[] = {
		0xff00, 0x00ff, 0xff00, 0x00ff,
		0x00ff, 0xff00, 0x00ff, 0xff00,
	};
	static const u32 group2_sites[] = {
		0x8f796, 0x8f7ab, 0x8f7c0, 0x8f7d5,
		0x8f7ea, 0x8f7ff, 0x8f814, 0x8f829,
	};
	unsigned core, i;
	int ret;

	/* PI+0x164==0 takes the no-category special case: the first two
	 * register groups use value in the low-byte selectors; arg2/arg3 are zero,
	 * so the second/high-byte arrays and final core group clear their fields. */
	for (core = 0; core < 2; core++) {
		for (i = 0; i < 8; i++) {
			ret = phy_mod(ops, ctx, r, group0_sites[i],
				      group0_regs[i], 0x00ff, value);
			if (ret) return ret;
		}
		for (i = 0; i < 8; i++) {
			ret = phy_mod(ops, ctx, r, group1_sites[i],
				      group1_regs[i], group1_masks[i], 0);
			if (ret) return ret;
		}
		for (i = 0; i < 8; i++) {
			ret = phy_mod(ops, ctx, r, group2_sites[i],
				      group2_regs[i], group2_masks[i], 0);
			if (ret) return ret;
		}
	}
	return 0;
}

static int run_nonzero_supported(const struct ob_phy_9a539_ops *ops,
				 void *ctx,
				 const struct ob_phy_9a539_input *in,
				 struct ob_phy_9a539_result *r,
				 const struct ob_phy_post_farrow_state *inner,
				 u8 sub8f41b_arg, u8 sub8f84d_arg,
				 bool call_99658_arg1,
				 bool call_998cc_arg1,
				 bool call_99658_arg2,
				 bool call_998cc_arg2,
				 u16 saved)
{
	static const u16 sub_90b77_regs[] = {
		0x0304, 0x0307, 0x030a, 0x030d,
	};
	static const u32 sub_90b77_sites[] = {
		0x90ba0, 0x90bb0, 0x90bc0, 0x90bd0,
	};
	static const u16 sub_8f84d_regs[] = {
		0x0324, 0x0330, 0x0321, 0x032d,
		0x032a, 0x0336, 0x0327, 0x0333,
	};
	static const u32 sub_8f84d_sites[] = {
		0x8f86f, 0x8f884, 0x8f899, 0x8f8ae,
		0x8f8c3, 0x8f8d8, 0x8f8ed, 0x8f902,
	};
	u8 i, value, mode, cap, selector_count, selector_delta;
	int ret;

	value = inner->nested_668[0] > 0x30 ? 0x30 : inner->nested_668[0];
	cap = value;
	if (in->chanspec & 0xc000u) {
		selector_count = cap > 1 ? (u8)(cap - 1u) : 0;
		if (selector_count > 0x0c)
			selector_count = 0x0c;
	} else {
		selector_count = r->low_band_row_count;
	}
	selector_delta = (u8)(0x45u - selector_count);
	for (i = 0; i < sizeof(sub_90b77_regs) / sizeof(sub_90b77_regs[0]); i++) {
		ret = phy_write(ops, ctx, r, sub_90b77_sites[i],
				sub_90b77_regs[i], value ? 0x5f62 : 0x4e51);
		if (ret) return ret;
		r->logical_event_count++;
	}
	if (call_99658_arg1) {
		ret = run_99658_arg1(ops, ctx, r, in, inner);
		if (ret) return ret;
	}
	if (call_99658_arg2) {
		ret = run_99658_arg2(ops, ctx, r, in, inner);
		if (ret) return ret;
	}
	if (call_998cc_arg1) {
		ret = run_998cc_arg1(ops, ctx, r, in, inner);
		if (ret) return ret;
	}
	if (call_998cc_arg2) {
		ret = run_998cc_arg2(ops, ctx, r, in, inner);
		if (ret) return ret;
	}
	if (r->sub_99658_arg1_called || r->sub_99658_arg2_called ||
	    r->sub_998cc_arg1_called || r->sub_998cc_arg2_called) {
		ret = run_9a121_internal(ops, ctx, r, in, inner);
		if (ret) return ret;
	}
	{
		u8 core;
		for (core = 0; core < in->chain_count; core++) {
			if (!(in->active_core_mask & (1u << core)))
				continue;
			ret = run_outer_99f29(ops, ctx, r, in, inner, core,
					       selector_delta);
			if (ret) return ret;
		}
	}
	ret = run_sub8f41b_mode0(ops, ctx, r, sub8f41b_arg);
	if (ret) return ret;
	for (i = 0; i < sizeof(sub_8f84d_regs) / sizeof(sub_8f84d_regs[0]); i++) {
		ret = phy_mod(ops, ctx, r, sub_8f84d_sites[i],
			      sub_8f84d_regs[i], 0xff00,
			      (u16)sub8f84d_arg << 8);
		if (ret) return ret;
	}
	if (!(in->chanspec & 0xc000u)) {
		/* The original helper copied the relocation-backed row bytes to
		 * [rbp-0x40] and [rbp-0x70], then indexed both at this selected row. */
		ret = phy_write(ops, ctx, r, 0x9a82d, 0x0299,
				(u16)(0x4400u | r->low_band_phy_0299_value));
		if (ret) return ret;
		r->logical_event_count++;
		ret = phy_write(ops, ctx, r, 0x9a840, 0x03c1,
				r->low_band_phy_03c1_value);
		if (ret) return ret;
		r->logical_event_count++;
	}
	mode = !in->nested_8a8_present ? 0 :
		(in->nested_8a8_byte18 ? 1 : (in->nested_8a8_byte47 != 0));
	r->aci_mode = mode;
	ret = call_result(r, 0x9a848, ops->aci_update(ctx, 0x9a848, mode));
	if (ret) return ret;
	r->logical_event_count++;
	ret = phy_mod(ops, ctx, r, 0x9a86a, 0x019e, 0x0002,
		      (u16)((saved >> 1) & 1u) << 1);
	if (ret) return ret;
	ret = call_result(r, 0x9a877, ops->mac_enable(ctx, 0x9a877));
	if (ret) return ret;
	r->logical_event_count++;
	r->mac_suspended = false;
	r->terminal_boundary = 0x9a88a;
	r->status = OB_9A539_SYNTHETIC_COMPLETE;
	return 0;
}

int ob_phy_9a539_test_only(const struct ob_phy_9a539_ops *ops, void *ctx,
			   const struct ob_phy_9a539_input *in,
			   struct ob_phy_9a539_result *r)
{
	static const u16 sub_8f84d_regs[] = {
		0x0324, 0x0330, 0x0321, 0x032d,
		0x032a, 0x0336, 0x0327, 0x0333,
	};
	static const u32 sub_8f84d_sites[] = {
		0x8f86f, 0x8f884, 0x8f899, 0x8f8ae,
		0x8f8c3, 0x8f8d8, 0x8f8ed, 0x8f902,
	};
	static const u16 sub_90b77_regs[] = { 0x0304, 0x0307, 0x030a, 0x030d };
	static const u32 sub_90b77_sites[] = { 0x90ba0, 0x90bb0, 0x90bc0, 0x90bd0 };
	u16 saved;
	u8 mode, i, sub8f41b_arg = 0, sub8f84d_arg = 0;
	bool call_998cc_arg1 = false;
	bool call_99658_arg1 = false;
	bool call_99658_arg2 = false;
	bool call_998cc_arg2 = false;
	struct ob_phy_post_farrow_state entry_state = { 0 }, inner_state = { 0 };
	int ret;

	if (!r)
		return -EINVAL;
	memset(r, 0, sizeof(*r));
	r->status = OB_9A539_INVALID;
	if (!in || !in->argument_present || !in->chanspec_present)
		return -EINVAL;
	if (in->chip_id != 0x4352 || in->chip_revision != 3 ||
	    in->phy_revision != 0x0a || in->radio_id != 0x2069 ||
	    in->radio_revision != 4 || in->radio_class != 0 ||
	    in->chain_count != 2)
		return -ENODEV;
	if (in->policy == OB_9A539_HISTORICAL_STRICT) {
		r->status = OB_9A539_BLOCKED_HISTORICAL_INPUT;
		return -EOPNOTSUPP;
	}
	if (in->policy != OB_9A539_SYNTHETIC_PARAMETERIZED ||
	    !in->explicitly_synthetic)
		return -EINVAL;
	if (in->argument != 0) {
		ret = prepare_nonzero(in, r, &entry_state, &inner_state,
				      &sub8f41b_arg, &sub8f84d_arg,
				      &call_99658_arg1,
				      &call_99658_arg2,
				      &call_998cc_arg1,
				      &call_998cc_arg2);
		if (ret)
			return ret;
	}
	if (!ops || !ops->mac_suspend_and_wait || !ops->mac_enable ||
	    !ops->aci_update || !ops->phy_read16 || !ops->phy_write16)
		return -EINVAL;
	if ((call_99658_arg1 || call_99658_arg2 || call_998cc_arg1 ||
	      call_998cc_arg2 || (in->argument && in->active_core_mask)) &&
	    !ops->table_write)
		return -EINVAL;
	if (in->active_core_mask &&
	    (call_99658_arg1 || call_99658_arg2 || call_998cc_arg1 ||
	     call_998cc_arg2) && (!ops->radio_read16 || !ops->radio_write16))
		return -EINVAL;
	if (in->per_core_matrix_present) {
		r->coefficient_matrix_after_present = true;
		memcpy(r->coefficient_matrix_after, in->per_core_matrix,
		       sizeof(r->coefficient_matrix_after));
	}

	ret = call_result(r, 0x9a58f, ops->mac_suspend_and_wait(ctx, 0x9a58f));
	if (ret)
		goto failed;
	r->mac_suspended = true;
	r->logical_event_count++;
	ret = phy_read(ops, ctx, r, 0x9a59c, 0x019e, &saved);
	if (ret)
		goto failed;
	r->saved_phy_019e = saved;
	r->logical_event_count++;
	ret = phy_mod(ops, ctx, r, 0x9a5ba, 0x019e, 0x0002, 0x0002);
	if (ret)
		goto failed;
	if (in->argument != 0) {
		ret = run_nonzero_supported(ops, ctx, in, r, &inner_state,
					    sub8f41b_arg, sub8f84d_arg,
					    call_99658_arg1, call_998cc_arg1,
					    call_99658_arg2, call_998cc_arg2, saved);
		if (ret)
			goto failed;
		return 0;
	}

	/* arg==0 selects sub_8f84d(PI, 0x36), eight RMWs. */
	for (i = 0; i < sizeof(sub_8f84d_regs) / sizeof(sub_8f84d_regs[0]); i++) {
		ret = phy_mod(ops, ctx, r, sub_8f84d_sites[i],
			      sub_8f84d_regs[i], 0xff00, 0x3600);
		if (ret)
			goto failed;
	}
	/* arg==0 selects sub_90b77(PI, 0); the result is narrowed to u16. */
	for (i = 0; i < sizeof(sub_90b77_regs) / sizeof(sub_90b77_regs[0]); i++) {
		ret = phy_write(ops, ctx, r, sub_90b77_sites[i],
				sub_90b77_regs[i], 0x4e51);
		if (ret)
			goto failed;
		r->logical_event_count++;
	}
	if (!(in->chanspec & 0xc000u)) {
		ret = phy_write(ops, ctx, r, 0x9a5f7, 0x0299, 0x4477);
		if (ret)
			goto failed;
		r->logical_event_count++;
		ret = phy_write(ops, ctx, r, 0x9a840, 0x03c1, 0x0010);
		if (ret)
			goto failed;
		r->logical_event_count++;
	}
	mode = !in->nested_8a8_present ? 0 :
		(in->nested_8a8_byte18 ? 1 : (in->nested_8a8_byte47 != 0));
	r->aci_mode = mode;
	ret = call_result(r, 0x9a848, ops->aci_update(ctx, 0x9a848, mode));
	if (ret)
		goto failed;
	r->logical_event_count++;
	ret = phy_mod(ops, ctx, r, 0x9a86a, 0x019e, 0x0002,
		      (u16)((saved >> 1) & 1u) << 1);
	if (ret)
		goto failed;
	ret = call_result(r, 0x9a877, ops->mac_enable(ctx, 0x9a877));
	if (ret)
		goto failed;
	r->logical_event_count++;
	r->terminal_boundary = 0x9a88a;
	r->status = OB_9A539_SYNTHETIC_COMPLETE;
	r->mac_suspended = false;
	return 0;

failed:
	r->status = OB_9A539_FAILED_BACKEND_OPERATION;
	return ret;
}

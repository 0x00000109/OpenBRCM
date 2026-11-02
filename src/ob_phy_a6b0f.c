/* SPDX-License-Identifier: GPL-2.0-only */
#include <stddef.h>

#include "ob_phy_a6b0f.h"

#include "ob_phy_a6b0f_vector.h"

#define OB_PHY_A6B0F_MAX_BATCH 16u
#define OB_PHY_A6B0F_SNAPSHOT_SLOTS 213u

static int ob_a6b0f_run_child(const struct ob_phy_a6b0f_ops *, void *, u32 *);

static int ob_a6b0f_validate_fixture(const struct ob_phy_a6b0f_fixture *f,
				     bool require_a0be4)
{
	const struct ob_phy_a0be4_fixture *p;

	if (!f || !f->boardflags2_supplied)
		return -22;
	if (require_a0be4 && !f->a0be4_fixture_completed)
		return -22;
	p = &f->initial_prefix;
	if (!p->phy.explicitly_synthetic || p->phy.chip_id != 0x4352 ||
	    p->phy.chip_revision != 3 || p->phy.phy_revision != 0x0a ||
	    p->phy.radio_id != 0x2069 || p->phy.radio_revision != 4 ||
	    p->phy.radio_class != 0 || p->phy.requested_chanspec != 0x1001 ||
	    p->phy.lookup_key != 1 || p->phy.row_index != 63 ||
	    !p->phy.lookup_matched || p->phy.pi_16e_class != 0 ||
	    p->phy.pi_164_phy_revision != 0x0a || p->phy.pi_168_radio_class != 2 ||
	    p->phy.nested_912 != 0 || p->entry.nested_912 != 0 ||
	    p->entry.nested_32c != 1 || p->entry.nested_32e != 0xff ||
	    (p->si_core_type != 1 && p->si_core_type != 3) ||
	    f->nested_34c != 0 || f->nested_342 != 0)
		return -95;
	return 0;
}

static u8 ob_a6b0f_callback_event(u16 source_local, bool branch_selected)
{
	u16 adjusted = source_local;

	if (source_local > 2)
		adjusted += 3;
	if (source_local == OB_PHY_A6B0F_VECTOR_COUNT && branch_selected)
		adjusted++;
	return (u8)adjusted;
}

static u16 ob_a6b0f_width_mask(u8 width)
{
	if (width == 16)
		return 0xffff;
	return 0;
}

static int ob_a6b0f_call_read(const struct ob_phy_a6b0f_ops *ops, void *ctx,
			      u8 logical, u32 site, u16 reg, u16 *value,
			      u32 *callbacks)
{
	(*callbacks)++;
	return ops->prefix.phy.read16(ctx, logical, site, OB_PHY_POST_LPF_PHY,
				      reg, value);
}

static int ob_a6b0f_call_write(const struct ob_phy_a6b0f_ops *ops, void *ctx,
			       u8 logical, u32 site, u16 reg, u16 value,
			       u32 *callbacks)
{
	(*callbacks)++;
	return ops->prefix.phy.write16(ctx, logical, site, OB_PHY_POST_LPF_PHY,
				       reg, value);
}

static int ob_a6b0f_table_value(const struct ob_phy_a6b0f_event *event,
				const u16 *table_reads,
				const bool *table_read_valid, u16 *value)
{
	u16 old;

	if (!value || event->width_bits != 16)
		return -22;
	if (event->expression == OB_PHY_A6B0F_EXPR_LITERAL) {
		*value = event->value;
		return 0;
	}
	if (!event->read_source_local ||
	    event->read_source_local >= OB_PHY_A6B0F_SNAPSHOT_SLOTS)
		return -22;
	if (!table_read_valid[event->read_source_local])
		return -22;
	old = table_reads[event->read_source_local];
	switch (event->expression) {
	case OB_PHY_A6B0F_EXPR_TABLE_PAIR_LOW_MASK:
		*value = old & event->value;
		return 0;
	case OB_PHY_A6B0F_EXPR_TABLE_CAP_LOW:
		*value = (u16)((old & 0x0fe0) | 0x000c);
		return 0;
	case OB_PHY_A6B0F_EXPR_TABLE_CAP_HIGH:
		*value = (u16)((old & 0x003f) | (old & 0x0800) | 0x0300);
		return 0;
	default:
		return -95;
	}
}

static int ob_a6b0f_run_parent(const struct ob_phy_a6b0f_ops *ops, void *ctx,
			       bool branch_selected, u32 *callbacks)
{
	u16 phy_reads[OB_PHY_A6B0F_SNAPSHOT_SLOTS] = { 0 };
	u16 table_reads[OB_PHY_A6B0F_SNAPSHOT_SLOTS] = { 0 };
	bool phy_read_valid[OB_PHY_A6B0F_SNAPSHOT_SLOTS] = { false };
	bool table_read_valid[OB_PHY_A6B0F_SNAPSHOT_SLOTS] = { false };
	size_t i;
	for (i = 0; i < OB_PHY_A6B0F_VECTOR_COUNT; i++) {
		const struct ob_phy_a6b0f_event *event = &ob_phy_a6b0f_events[i];
		u8 logical = ob_a6b0f_callback_event(event->local_sequence,
						     branch_selected);
		u16 old, value;
		int ret;

		if (event->local_sequence != i + 1 ||
		    event->read_source_local >= OB_PHY_A6B0F_SNAPSHOT_SLOTS ||
		    !ob_a6b0f_width_mask(event->width_bits))
			return -22;
		if (i == OB_PHY_A6B0F_VECTOR_COUNT - 1 && branch_selected) {
			const u8 branch_event = OB_PHY_A6B0F_VECTOR_COUNT + 3u;
			const u16 payload = 0x0078;

			(*callbacks)++;
			ret = ops->table_write(ctx, branch_event, 0xa6f35, 7,
					       0x0080, 1, 16, &payload);
			if (ret)
				return ret;
		}
		switch (event->operation) {
		case OB_PHY_A6B0F_PHY_READ:
			ret = ob_a6b0f_call_read(ops, ctx, logical, event->source_site,
						 event->selector, &value, callbacks);
			if (ret)
				return ret;
			phy_reads[event->local_sequence] = value;
			phy_read_valid[event->local_sequence] = true;
			break;
		case OB_PHY_A6B0F_PHY_RMW:
			ret = ob_a6b0f_call_read(ops, ctx, logical, event->source_site,
						 event->selector, &old, callbacks);
			if (ret)
				return ret;
			if (event->expression == OB_PHY_A6B0F_EXPR_PHY_SET_BIT) {
				value = event->value;
			} else if (event->expression == OB_PHY_A6B0F_EXPR_PHY_RESTORE_BIT) {
				if (!event->read_source_local)
					return -22;
				if (!phy_read_valid[event->read_source_local])
					return -22;
				value = phy_reads[event->read_source_local] & event->mask;
			} else {
				return -95;
			}
			/* Accepted phy_reg_mod semantics: (old & ~mask) | (value & mask). */
			value = (u16)((old & (u16)~event->mask) |
				       (value & event->mask));
			ret = ob_a6b0f_call_write(ops, ctx, logical, event->source_site,
						  event->selector, value, callbacks);
			if (ret)
				return ret;
			break;
		case OB_PHY_A6B0F_TABLE_READ:
			if (!ops->table_read || event->batch_count)
				return -95;
			(*callbacks)++;
			ret = ops->table_read(ctx, logical, event->source_site,
					      (u8)event->selector, event->offset, 1,
					      event->width_bits, &value);
			if (ret)
				return ret;
			table_reads[event->local_sequence] = value;
			table_read_valid[event->local_sequence] = true;
			break;
		case OB_PHY_A6B0F_TABLE_WRITE: {
			u16 values[OB_PHY_A6B0F_MAX_BATCH];
			u16 count = event->batch_count ? event->batch_count : 1;
			unsigned int j;

			if (!ops->table_write || count > OB_PHY_A6B0F_MAX_BATCH ||
			    i + count > OB_PHY_A6B0F_VECTOR_COUNT)
				return -95;
			for (j = 0; j < count; j++) {
				const struct ob_phy_a6b0f_event *part =
					&ob_phy_a6b0f_events[i + j];

				if (part->operation != OB_PHY_A6B0F_TABLE_WRITE ||
				    part->selector != event->selector ||
				    part->offset != event->offset + j ||
				    part->width_bits != event->width_bits ||
				    (j && part->batch_count))
					return -22;
				ret = ob_a6b0f_table_value(part, table_reads,
							   table_read_valid, &values[j]);
				if (ret)
					return ret;
			}
			(*callbacks)++;
			ret = ops->table_write(ctx, logical, event->source_site,
					       (u8)event->selector, event->offset,
					       count, event->width_bits, values);
			if (ret)
				return ret;
			i += count - 1;
			break;
		}
		default:
			return -95;
		}
		if (i == 1) {
			ret = ob_a6b0f_run_child(ops, ctx, callbacks);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static int ob_a6b0f_run_child(const struct ob_phy_a6b0f_ops *ops, void *ctx,
			      u32 *callbacks)
{
	const struct ob_phy_a6b0f_event *read = &ob_phy_a6b0f_child_events[0];
	const struct ob_phy_a6b0f_event *set = &ob_phy_a6b0f_child_events[1];
	const struct ob_phy_a6b0f_event *restore = &ob_phy_a6b0f_child_events[2];
	u16 saved, old, value;
	int ret;

	ret = ob_a6b0f_call_read(ops, ctx, read->local_sequence,
				read->source_site, read->selector, &saved, callbacks);
	if (ret)
		return ret;
	ret = ob_a6b0f_call_read(ops, ctx, set->local_sequence, set->source_site,
				set->selector, &old, callbacks);
	if (ret)
		return ret;
	value = (u16)((old & (u16)~set->mask) | (set->value & set->mask));
	ret = ob_a6b0f_call_write(ops, ctx, set->local_sequence, set->source_site,
				  set->selector, value, callbacks);
	if (ret)
		return ret;
	ret = ob_a6b0f_call_read(ops, ctx, restore->local_sequence,
				restore->source_site, restore->selector, &old,
				callbacks);
	if (ret)
		return ret;
	value = (u16)((old & (u16)~restore->mask) | (saved & restore->mask));
	return ob_a6b0f_call_write(ops, ctx, restore->local_sequence,
				   restore->source_site, restore->selector, value,
				   callbacks);
}

int ob_phy_acphy_a6b0f_selected_fixture_test_only(
	const struct ob_phy_a6b0f_ops *ops, void *ctx,
	const struct ob_phy_a6b0f_fixture *fixture,
	struct ob_phy_a6b0f_result *result)
{
	struct ob_phy_a6b0f_result completed = { 0 };
	bool branch_selected;
	u32 callbacks = 0;
	int ret;

	if (!result)
		return -22;
	*result = (struct ob_phy_a6b0f_result) { 0 };
	if (!ops || !ops->prefix.phy.read16 || !ops->prefix.phy.write16 ||
	    !ops->prefix.phy.delay_us || !ops->prefix.phy.phyclk_fgc ||
	    !ops->prefix.read_ai_core32 || !ops->prefix.write_ai_core32 ||
	    !ops->table_read || !ops->table_write)
		return -22;
	ret = ob_a6b0f_validate_fixture(fixture, true);
	if (ret)
		return ret;
	if (OB_PHY_A6B0F_VECTOR_COUNT != 212 ||
	    OB_PHY_A6B0F_CHILD_VECTOR_COUNT != 3)
		return -22;
	branch_selected = (fixture->initial_prefix.phy.requested_chanspec & 0xc000) == 0 &&
		((fixture->boardflags2 & 0x00100000) != 0);

	ret = ob_a6b0f_run_parent(ops, ctx, branch_selected, &callbacks);
	if (ret)
		return ret;
	completed.kind = OB_PHY_A6B0F_SELECTED_FIXTURE_COMPLETE;
	completed.logical_event_count = 212u + 3u + (branch_selected ? 1u : 0u);
	completed.callback_count = callbacks;
	completed.boardflags2_mask_set = branch_selected;
	completed.hardware_ready = false;
	*result = completed;
	return 0;
}

int ob_phy_acphy_initial_fixture_through_a6b0f_test_only(
	const struct ob_phy_a6b0f_ops *ops, void *ctx,
	const struct ob_phy_a6b0f_fixture *fixture,
	struct ob_phy_a6b0f_composed_result *result)
{
	struct ob_phy_a6b0f_composed_result completed = { 0 };
	struct ob_phy_a6b0f_fixture selected;
	int ret;

	if (!result)
		return -22;
	*result = (struct ob_phy_a6b0f_composed_result) { 0 };
	if (!ops || !ops->prefix.phy.read16 || !ops->prefix.phy.write16 ||
	    !ops->prefix.phy.delay_us || !ops->prefix.phy.phyclk_fgc ||
	    !ops->prefix.read_ai_core32 || !ops->prefix.write_ai_core32 ||
	    !ops->table_read || !ops->table_write)
		return -22;
	ret = ob_a6b0f_validate_fixture(fixture, false);
	if (ret)
		return ret;
	ret = ob_phy_acphy_initial_fixture_through_a0be4_test_only(
		&ops->prefix, ctx, &fixture->initial_prefix,
		&completed.through_a0be4);
	if (ret)
		return ret;
	selected = *fixture;
	selected.a0be4_fixture_completed = true;
	ret = ob_phy_acphy_a6b0f_selected_fixture_test_only(
		ops, ctx, &selected, &completed.a6b0f);
	if (ret)
		return ret;
	completed.callback_count = completed.through_a0be4.callback_count +
				   completed.a6b0f.callback_count;
	completed.logical_event_count = 3734u +
		completed.through_a0be4.through_a7089.selected_block.logical_event_count +
		completed.through_a0be4.a0be4.logical_event_count +
		completed.a6b0f.logical_event_count;
	completed.terminal_return_site = 0xaa41f;
	completed.hardware_ready = false;
	*result = completed;
	return 0;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include "ob_phy_a0be4.h"

#include "ob_phy_a0be4_vector.h"

static int ob_a0be4_validate_fixture(const struct ob_phy_a0be4_fixture *fixture,
				     bool require_prior_completion)
{
	const struct ob_phy_post_lpf_fixture *phy;
	const struct ob_phy_post_lpf_entry_state *entry;

	if (!fixture)
		return -22;
	if (require_prior_completion &&
	    !fixture->selected_a7089_fixture_completed)
		return -22;
	phy = &fixture->phy;
	entry = &fixture->entry;
	if (!phy->explicitly_synthetic)
		return -22;
	/*
	 * PI+0x164 (the PHY revision) is the branch value. The recovered vector
	 * is the rev > 1 arm (built for 0x0a); the BCM4352 target is rev 1, which
	 * takes one extra write (see the loop below). Both are accepted; the
	 * producer pins pi+0x164 == PHYVERSION & 0x0f and pi+0x168 == 2.
	 */
	if (phy->chip_id != 0x4352 || phy->chip_revision != 3 ||
	    (phy->phy_revision != 0x0a && phy->phy_revision != 1) ||
	    phy->radio_id != 0x2069 ||
	    phy->radio_revision != 4 || phy->radio_class != 0 ||
	    phy->requested_chanspec != 0x1001 || phy->lookup_key != 1 ||
	    phy->row_index != 63 || phy->pi_16e_class != 0 ||
	    phy->pi_164_phy_revision != phy->phy_revision ||
	    phy->pi_168_radio_class != 2)
		return -95;
	if (!phy->lookup_matched)
		return -2;
	/* 0x912 is not read inside the vector; require only revision-consistency. */
	if (phy->nested_912 != entry->nested_912 ||
	    phy->nested_912 != (phy->pi_164_phy_revision <= 1 ? 1u : 0u) ||
	    entry->nested_32c != 1 || entry->nested_32e != 0xff)
		return -95;
	/* The admitted SI target states 1 and 3 both select ai_core_cflags. */
	if (fixture->si_core_type != 1 && fixture->si_core_type != 3)
		return -95;
	return 0;
}

static int ob_a0be4_read16(const struct ob_phy_a0be4_ops *ops, void *ctx,
			   const struct ob_phy_a0be4_event *event,
			   u16 *value, u32 *callbacks)
{
	(*callbacks)++;
	return ops->phy.read16(ctx, event->logical_event, event->source_site,
			       OB_PHY_POST_LPF_PHY,
			       event->register_offset, value);
}

static int ob_a0be4_write16(const struct ob_phy_a0be4_ops *ops, void *ctx,
			    const struct ob_phy_a0be4_event *event,
			    u16 value, u32 *callbacks)
{
	(*callbacks)++;
	return ops->phy.write16(ctx, event->logical_event, event->source_site,
				OB_PHY_POST_LPF_PHY,
				event->register_offset, value);
}

int ob_phy_acphy_a0be4_selected_fixture_test_only(
	const struct ob_phy_a0be4_ops *ops, void *ctx,
	const struct ob_phy_a0be4_fixture *fixture,
	struct ob_phy_a0be4_result *result)
{
	struct ob_phy_a0be4_result completed = { 0 };
	u16 resetcca_snapshot = 0;
	bool resetcca_snapshot_valid = false;
	u32 callbacks = 0;
	unsigned int i;
	int ret;

	if (!result)
		return -22;
	*result = (struct ob_phy_a0be4_result) { 0 };
	if (!ops || !ops->phy.read16 || !ops->phy.write16 ||
	    !ops->phy.delay_us || !ops->phy.phyclk_fgc ||
	    !ops->read_ai_core32 || !ops->write_ai_core32)
		return -22;
	ret = ob_a0be4_validate_fixture(fixture, true);
	if (ret)
		return ret;
	if (OB_PHY_A0BE4_VECTOR_COUNT != OB_PHY_A0BE4_LOGICAL_EVENT_COUNT)
		return -22;

	for (i = 0; i < OB_PHY_A0BE4_VECTOR_COUNT; i++) {
		const struct ob_phy_a0be4_event *event = &ob_phy_a0be4_events[i];
		u16 old16, value16;
		u32 old32, value32, readback32;

		if (event->logical_event != i + 1)
			return -22;
		switch (event->operation) {
		case OB_PHY_A0BE4_PHY_OR:
			if (event->width_bits != 16)
				return -22;
			ret = ob_a0be4_read16(ops, ctx, event, &old16, &callbacks);
			if (ret)
				return ret;
			ret = ob_a0be4_write16(ops, ctx, event,
						(u16)(old16 | event->value), &callbacks);
			if (ret)
				return ret;
			break;
		case OB_PHY_A0BE4_PHY_WRITE:
			if (event->width_bits != 16)
				return -22;
			ret = ob_a0be4_write16(ops, ctx, event,
						(u16)event->value, &callbacks);
			if (ret)
				return ret;
			break;
		case OB_PHY_A0BE4_PHY_RMW_MASKED_MERGE:
			if (event->width_bits != 16)
				return -22;
			ret = ob_a0be4_read16(ops, ctx, event, &old16, &callbacks);
			if (ret)
				return ret;
			value16 = (u16)((old16 & (u16)~event->mask) |
					((u16)event->value & (u16)event->mask));
			ret = ob_a0be4_write16(ops, ctx, event, value16, &callbacks);
			if (ret)
				return ret;
			break;
		case OB_PHY_A0BE4_AI_CORE_CFLAGS:
			if (event->width_bits != 32 || fixture->si_core_type == 0)
				return -95;
			callbacks++;
			ret = ops->read_ai_core32(ctx, event->logical_event,
					  event->source_site,
					  event->register_offset, &old32);
			if (ret)
				return ret;
			/* ai_core_cflags uses (old & ~mask) | value. */
			value32 = (old32 & ~event->mask) | event->value;
			if (event->mask || event->value) {
				callbacks++;
				ret = ops->write_ai_core32(ctx, event->logical_event,
						   event->source_site,
						   event->register_offset,
						   value32);
				if (ret)
					return ret;
			}
			/* Historical AI helper performs a readback; caller discards it. */
			callbacks++;
			ret = ops->read_ai_core32(ctx, event->logical_event,
					  event->source_site,
					  event->register_offset, &readback32);
			if (ret)
				return ret;
			(void)readback32;
			break;
		case OB_PHY_A0BE4_PHY_READ:
			if (event->width_bits != 16)
				return -22;
			ret = ob_a0be4_read16(ops, ctx, event, &resetcca_snapshot,
					      &callbacks);
			if (ret)
				return ret;
			resetcca_snapshot_valid = true;
			break;
		case OB_PHY_A0BE4_PHY_WRITE_DERIVED:
			if (!resetcca_snapshot_valid || event->width_bits != 16 ||
			    event->expression_source_event != 21)
				return -22;
			if (event->expression == OB_PHY_A0BE4_EXPR_SNAPSHOT_OR)
				value16 = (u16)(resetcca_snapshot | event->value);
			else if (event->expression == OB_PHY_A0BE4_EXPR_SNAPSHOT_AND)
				value16 = (u16)(resetcca_snapshot & event->value);
			else
				return -95;
			ret = ob_a0be4_write16(ops, ctx, event, value16, &callbacks);
			if (ret)
				return ret;
			break;
		case OB_PHY_A0BE4_DELAY_US:
			callbacks++;
			ret = ops->phy.delay_us(ctx, event->logical_event,
						event->source_site,
						event->duration_us);
			if (ret)
				return ret;
			break;
		case OB_PHY_A0BE4_PHYCLK_FGC:
			callbacks++;
			ret = ops->phy.phyclk_fgc(ctx, event->logical_event,
						  event->source_site,
						  event->value != 0);
			if (ret)
				return ret;
			break;
		default:
			return -95;
		}

		/*
		 * PI+0x164 <= 1 executes an extra write that the rev > 1 (0x0a)
		 * vector omits: phy_reg_write(0x0026, 0x0092) at site 0xa0ce5,
		 * immediately after the 0xa0cca write. Proven by the sub_a0be4
		 * disassembly (`cmp [rbx+164h],1 ; ja 0xa0cea` skips 0xa0ce5 when
		 * [0x164] > 1) and confirmed on hardware by the vendor wl trace.
		 */
		if (event->source_site == 0xa0ccau &&
		    fixture->phy.phy_revision <= 1) {
			struct ob_phy_a0be4_event extra = {
				.logical_event = event->logical_event,
				.operation = OB_PHY_A0BE4_PHY_WRITE,
				.source_site = 0xa0ce5u,
				.register_offset = 0x0026u,
				.width_bits = 16,
				.value = 0x0092u,
			};

			ret = ob_a0be4_write16(ops, ctx, &extra, 0x0092u, &callbacks);
			if (ret)
				return ret;
		}
	}

	completed.kind = OB_PHY_A0BE4_SELECTED_FIXTURE_COMPLETE;
	completed.logical_event_count = OB_PHY_A0BE4_VECTOR_COUNT;
	completed.callback_count = callbacks;
	completed.hardware_ready = false;
	*result = completed;
	return 0;
}

struct ob_a0be4_prefix_adapter {
	const struct ob_phy_a0be4_ops *ops;
	void *ctx;
	u32 callbacks;
};

static int ob_a0be4_prefix_read16(void *opaque, u8 event, u32 site,
				  enum ob_phy_post_lpf_domain domain,
				  u16 reg, u16 *value)
{
	struct ob_a0be4_prefix_adapter *adapter = opaque;

	adapter->callbacks++;
	return adapter->ops->phy.read16(adapter->ctx, event, site, domain,
					  reg, value);
}

static int ob_a0be4_prefix_write16(void *opaque, u8 event, u32 site,
				   enum ob_phy_post_lpf_domain domain,
				   u16 reg, u16 value)
{
	struct ob_a0be4_prefix_adapter *adapter = opaque;

	adapter->callbacks++;
	return adapter->ops->phy.write16(adapter->ctx, event, site, domain,
					   reg, value);
}

static int ob_a0be4_prefix_delay(void *opaque, u8 event, u32 site,
				 u16 usec)
{
	struct ob_a0be4_prefix_adapter *adapter = opaque;

	adapter->callbacks++;
	return adapter->ops->phy.delay_us(adapter->ctx, event, site, usec);
}

static int ob_a0be4_prefix_phyclk(void *opaque, u8 event, u32 site,
				  bool enabled)
{
	struct ob_a0be4_prefix_adapter *adapter = opaque;

	adapter->callbacks++;
	return adapter->ops->phy.phyclk_fgc(adapter->ctx, event, site, enabled);
}

int ob_phy_acphy_initial_fixture_through_a0be4_test_only(
	const struct ob_phy_a0be4_ops *ops, void *ctx,
	const struct ob_phy_a0be4_fixture *fixture,
	struct ob_phy_a0be4_composed_result *result)
{
	const struct ob_phy_post_lpf_ops prefix_ops = {
		.read16 = ob_a0be4_prefix_read16,
		.write16 = ob_a0be4_prefix_write16,
		.delay_us = ob_a0be4_prefix_delay,
		.phyclk_fgc = ob_a0be4_prefix_phyclk,
	};
	struct ob_a0be4_prefix_adapter adapter;
	struct ob_phy_a0be4_fixture selected_fixture;
	struct ob_phy_a0be4_composed_result completed = { 0 };
	int ret;

	if (!result)
		return -22;
	*result = (struct ob_phy_a0be4_composed_result) { 0 };
	if (!ops || !ops->phy.read16 || !ops->phy.write16 ||
	    !ops->phy.delay_us || !ops->phy.phyclk_fgc ||
	    !ops->read_ai_core32 || !ops->write_ai_core32)
		return -22;
	ret = ob_a0be4_validate_fixture(fixture, false);
	if (ret)
		return ret;
	adapter.ops = ops;
	adapter.ctx = ctx;
	adapter.callbacks = 0;
	ret = ob_phy_acphy_post_lpf_compose_fixture_test_only(
		&prefix_ops, &adapter, &fixture->phy, &completed.through_a7089);
	if (ret)
		return ret;
	selected_fixture = *fixture;
	selected_fixture.selected_a7089_fixture_completed = true;
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		ops, ctx, &selected_fixture, &completed.a0be4);
	if (ret)
		return ret;
	completed.callback_count = adapter.callbacks +
				   completed.a0be4.callback_count;
	completed.terminal_return_site = 0xa13eb;
	completed.hardware_ready = false;
	*result = completed;
	return 0;
}

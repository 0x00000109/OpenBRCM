/* SPDX-License-Identifier: GPL-2.0-only */
#include <string.h>

#include "ob_phy_9e378.h"

#include "ob_phy_9e378_vector.h"

#define OB_PHY_9E378_NO_SNAPSHOT 0xffu
#define OB_PHY_9E378_MAX_SNAPSHOTS 8u

static const struct ob_phy_9e378_variant *
ob_9e378_find_variant(const struct ob_phy_9e378_fixture *f, u8 *index)
{
	bool ext_eq2 = f->extpagain2g == 2;
	bool pdgain_write = f->nested_411 == 9 || f->nested_411 == 16;
	u8 i;

	for (i = 0; i < OB_PHY_9E378_VARIANT_COUNT; i++) {
		const struct ob_phy_9e378_variant *v = &ob_phy_9e378_variants[i];

		if (v->extpagain2g_eq2 == ext_eq2 &&
		    v->pdgain_write == pdgain_write &&
		    v->txchain_low2 == f->txchain_low2) {
			*index = i;
			return v;
		}
	}
	return NULL;
}

static int ob_9e378_validate_vector(const struct ob_phy_9e378_variant *v)
{
	bool snapshots[OB_PHY_9E378_MAX_SNAPSHOTS] = { false };
	u16 callbacks = 0;
	u16 i;

	if (!v || !v->events || !v->logical_event_count ||
	    v->snapshot_count > OB_PHY_9E378_MAX_SNAPSHOTS)
		return -22;
	for (i = 0; i < v->logical_event_count; i++) {
		const struct ob_phy_9e378_event *e = &v->events[i];

		if (e->logical_sequence != i + 1 || e->access_width_bits != 16)
			return -22;
		switch (e->operation) {
		case OB_PHY_9E378_READ:
			if (e->domain == OB_PHY_9E378_TABLE)
				return -95;
			callbacks++;
			if (e->snapshot_slot != OB_PHY_9E378_NO_SNAPSHOT) {
				if (e->snapshot_slot >= v->snapshot_count)
					return -22;
				snapshots[e->snapshot_slot] = true;
			}
			break;
		case OB_PHY_9E378_WRITE:
			if (e->domain == OB_PHY_9E378_TABLE)
				return -95;
			callbacks++;
			break;
		case OB_PHY_9E378_RMW:
			if ((e->domain == OB_PHY_9E378_PHY &&
			     e->rmw_mode != OB_PHY_9E378_RMW_PHY_MASKED_MERGE) ||
			    (e->domain == OB_PHY_9E378_RADIO &&
			     e->rmw_mode != OB_PHY_9E378_RMW_RADIO_CLEAR_VALUE) ||
			    (e->domain != OB_PHY_9E378_PHY &&
			     e->domain != OB_PHY_9E378_RADIO))
				return -95;
			if (e->expression == OB_PHY_9E378_SNAPSHOT_BIT1 &&
			    (e->snapshot_slot >= v->snapshot_count ||
			     !snapshots[e->snapshot_slot]))
				return -22;
			if (e->expression != OB_PHY_9E378_LITERAL &&
			    e->expression != OB_PHY_9E378_SNAPSHOT_BIT1)
				return -95;
			callbacks += 2;
			break;
		case OB_PHY_9E378_TABLE_WRITE:
			if (e->domain != OB_PHY_9E378_TABLE || !e->element_count ||
			    !e->payload_word_count)
				return -22;
			if (e->element_width_bits == 48) {
				if (e->table_id != 0x20 || e->element_count != 128 ||
				    e->payload_word_count != 384 ||
				    (e->payload_id != OB_PHY_9E378_PAYLOAD_IPA &&
				     e->payload_id != OB_PHY_9E378_PAYLOAD_EPA))
					return -22;
			} else if (e->element_width_bits == 16) {
				if (e->payload_word_count != e->element_count ||
				    (e->payload_id != OB_PHY_9E378_PAYLOAD_ZERO1 &&
				     e->payload_id != OB_PHY_9E378_PAYLOAD_ZERO2))
					return -22;
			} else {
				return -95;
			}
			callbacks++;
			break;
		default:
			return -95;
		}
	}
	if (callbacks != v->callback_count)
		return -22;
	return 0;
}

static const u16 *ob_9e378_payload(const struct ob_phy_9e378_event *e)
{
	switch (e->payload_id) {
	case OB_PHY_9E378_PAYLOAD_IPA:
		return ob_phy_9e378_gain_ipa;
	case OB_PHY_9E378_PAYLOAD_EPA:
		return ob_phy_9e378_gain_epa;
	case OB_PHY_9E378_PAYLOAD_ZERO1:
		return ob_phy_9e378_zero1;
	case OB_PHY_9E378_PAYLOAD_ZERO2:
		return ob_phy_9e378_zero2;
	default:
		return NULL;
	}
}

static int ob_9e378_validate_fixture(const struct ob_phy_9e378_fixture *f)
{
	if (!f || !f->explicitly_synthetic)
		return -22;
	if (f->chip_id != 0x4352 || f->chip_revision != 3 ||
	    (f->phy_revision != 1 && f->phy_revision != 0x0a) ||
	    f->radio_id != 0x2069 ||
	    f->radio_revision != 4 || f->radio_class != 0 ||
	    f->chain_count != 2)
		return -95;
	if (f->caller_gate_79 != f->nested_32c)
		return -22;
	return 0;
}

int ob_phy_acphy_9e378_fixture_test_only(
	const struct ob_phy_9e378_ops *ops, void *ctx,
	const struct ob_phy_9e378_fixture *fixture,
	struct ob_phy_9e378_result *result)
{
	struct ob_phy_9e378_result completed = { 0 };
	const struct ob_phy_9e378_variant *variant;
	u16 snapshots[OB_PHY_9E378_MAX_SNAPSHOTS] = { 0 };
	bool snapshot_valid[OB_PHY_9E378_MAX_SNAPSHOTS] = { false };
	u16 callbacks = 0;
	u8 variant_index = 0;
	u16 i;
	int ret;

	if (!result)
		return -22;
	memset(result, 0, sizeof(*result));
	ret = ob_9e378_validate_fixture(fixture);
	if (ret)
		return ret;
	if (!fixture->caller_gate_7b && !fixture->caller_gate_79) {
		completed.kind = OB_PHY_9E378_RESULT_SKIPPED;
		completed.hardware_ready = false;
		*result = completed;
		return 0;
	}
	/* This runner admits the known selected initial profile only. The skip
	 * arm remains available to test the caller branch without callbacks. */
	if (fixture->nested_32c != 1 || !fixture->chanspec_supplied ||
	    fixture->requested_chanspec != 0x1001 ||
	    !fixture->selectors_supplied || !fixture->shared_80_supplied ||
	    fixture->shared_80 != 0 || fixture->txchain_low2 > 3)
		return -22;
	if (!ops || !ops->read16 || !ops->write16 || !ops->table_write)
		return -22;
	variant = ob_9e378_find_variant(fixture, &variant_index);
	if (!variant)
		return -95;
	ret = ob_9e378_validate_vector(variant);
	if (ret)
		return ret;
	for (i = 0; i < variant->logical_event_count; i++) {
		const struct ob_phy_9e378_event *e = &variant->events[i];
		u16 old, value;

		switch (e->operation) {
		case OB_PHY_9E378_READ:
			callbacks++;
			ret = ops->read16(ctx, (u8)e->logical_sequence,
					  e->source_site, e->domain, e->reg, &value);
			if (ret)
				return ret;
			if (e->snapshot_slot != OB_PHY_9E378_NO_SNAPSHOT) {
				snapshots[e->snapshot_slot] = value;
				snapshot_valid[e->snapshot_slot] = true;
			}
			break;
		case OB_PHY_9E378_WRITE:
			callbacks++;
			ret = ops->write16(ctx, (u8)e->logical_sequence,
					   e->source_site, e->domain, e->reg,
					   e->value);
			if (ret)
				return ret;
			break;
		case OB_PHY_9E378_RMW:
			callbacks++;
			ret = ops->read16(ctx, (u8)e->logical_sequence,
					  e->source_site, e->domain, e->reg, &old);
			if (ret)
				return ret;
			if (e->expression == OB_PHY_9E378_SNAPSHOT_BIT1) {
				if (e->snapshot_slot >= variant->snapshot_count ||
				    !snapshot_valid[e->snapshot_slot])
					return -22;
				value = snapshots[e->snapshot_slot] & 0x0002;
			} else {
				value = e->value;
			}
			if (e->rmw_mode == OB_PHY_9E378_RMW_PHY_MASKED_MERGE)
				value = (u16)((old & (u16)~e->mask) | (value & e->mask));
			else
				value = (u16)((old & (u16)~value) | (value & e->mask));
			callbacks++;
			ret = ops->write16(ctx, (u8)e->logical_sequence,
					   e->source_site, e->domain, e->reg, value);
			if (ret)
				return ret;
			break;
		case OB_PHY_9E378_TABLE_WRITE: {
			const u16 *payload = ob_9e378_payload(e);

			if (!payload)
				return -95;
			callbacks++;
			ret = ops->table_write(ctx, (u8)e->logical_sequence,
					       e->source_site, e->table_id,
					       e->table_offset, e->element_count,
					       e->element_width_bits, payload,
					       e->payload_word_count);
			if (ret)
				return ret;
			break;
		}
		default:
			return -95;
		}
	}
	if (callbacks != variant->callback_count)
		return -22;
	completed.kind = OB_PHY_9E378_RESULT_SYNTHETIC_VARIANT_COMPLETE;
	completed.variant_index = variant_index;
	completed.logical_event_count = variant->logical_event_count;
	completed.callback_count = callbacks;
	completed.hardware_ready = false;
	*result = completed;
	return 0;
}

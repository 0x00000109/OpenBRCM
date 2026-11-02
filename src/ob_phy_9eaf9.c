/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_9eaf9.h"
#include "ob_phy_9eaf9_vector.h"

struct ob_9eaf9_sample_key {
	u16 logical_event;
	u32 source_site;
	u8 table_id;
	u16 offset;
	u8 chain;
	u8 ordinal;
};

static const struct ob_9eaf9_sample_key ob_9eaf9_sample_keys[] = {
	{ 40, 0x9da4b, 7, 0x14a, 0, 1 }, { 41, 0x9da73, 7, 0x36a, 0, 2 },
	{ 44, 0x9da4b, 7, 0x15a, 1, 3 }, { 45, 0x9da73, 7, 0x37a, 1, 4 },
};

static int ob_9eaf9_validate_target(const struct ob_phy_9eaf9_fixture *f)
{
	if (!f || !f->explicitly_synthetic ||
	    f->chip_id != 0x4352 || f->chip_revision != 3 ||
	    (f->phy_revision != 1 && f->phy_revision != 0x0a) ||
	    f->radio_id != 0x2069 ||
	    f->radio_revision != 4 || f->radio_class != 0 ||
	    f->chain_count != 2 || !f->chanspec_supplied ||
	    f->controlled_chanspec != 0x1001 ||
	    !f->caller_gate_inputs_supplied)
		return -EINVAL;
	if (f->policy != OB_PHY_9EAF9_HISTORICAL_STRICT &&
	    f->policy != OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED)
		return -EINVAL;
	return 0;
}

static int ob_9eaf9_validate_samples(const struct ob_phy_9eaf9_fixture *f)
{
	u8 i;

	if (!f->word2_present)
		return -EINVAL;
	for (i = 0; i < OB_PHY_9EAF9_SYNTHETIC_SAMPLE_COUNT; i++) {
		const struct ob_phy_9eaf9_runtime_sample *s = &f->runtime_samples[i];
		const struct ob_9eaf9_sample_key *key = &ob_9eaf9_sample_keys[i];

		if (!s->valid || s->logical_event != key->logical_event ||
		    s->source_site != key->source_site ||
		    s->table_id != key->table_id ||
		    s->offset != key->offset || s->element_count != 1 ||
		    s->element_width_bits != 16 || s->chain_index != key->chain ||
		    s->invocation_id != OB_PHY_9EAF9_INVOCATION_CONTROLLED_1001 ||
		    s->sample_ordinal != key->ordinal)
			return -EINVAL;
	}
	return 0;
}

static int ob_9eaf9_validate_vector(void)
{
	u16 i;
	u16 logical = 0;
	u16 callbacks = 0;

	if (OB_PHY_9EAF9_EVENT_COUNT != OB_PHY_9EAF9_LOGICAL_EVENT_COUNT)
		return -EINVAL;
	for (i = 0; i < OB_PHY_9EAF9_EVENT_COUNT; i++) {
		const struct ob_phy_9eaf9_event *e = &ob_phy_9eaf9_events[i];

		if (e->logical_event != i + 1 || e->element_width_bits == 0)
			return -EINVAL;
		logical++;
		switch (e->operation) {
		case OB_9EAF9_READ:
		case OB_9EAF9_WRITE:
		case OB_9EAF9_TABLE_READ:
			callbacks++;
			break;
		case OB_9EAF9_RMW:
			callbacks += 2;
			break;
		case OB_9EAF9_TABLE_WRITE:
			if (e->batch_count == 0)
				continue;
			if (i + e->batch_count > OB_PHY_9EAF9_EVENT_COUNT)
				return -EINVAL;
			callbacks++;
			break;
		default:
			return -EOPNOTSUPP;
		}
	}
	return logical == 136 && callbacks == 107 ? 0 : -EINVAL;
}

static u16 ob_9eaf9_derived_word(enum ob_phy_9eaf9_value_kind kind,
				const u16 samples[OB_PHY_9EAF9_SYNTHETIC_SAMPLE_COUNT])
{
	u8 a_slot, b_slot;
	u32 pair, value;

	if (kind == OB_9EAF9_VALUE_LPF_LOW0 || kind == OB_9EAF9_VALUE_LPF_HIGH0) {
		a_slot = 0;
		b_slot = 1;
	} else {
		a_slot = 2;
		b_slot = 3;
	}
	pair = ((u32)samples[b_slot] << 16) | samples[a_slot];
	value = (((pair & 0x01ffffc7u) | 0x00000028u) & 0x01fffe3fu) |
		0x00000140u;
	if (kind == OB_9EAF9_VALUE_LPF_LOW0 || kind == OB_9EAF9_VALUE_LPF_LOW1)
		return (u16)(value & 0xffffu);
	return (u16)((value >> 16) & 0x01ffu);
}

static int ob_9eaf9_table_value(const struct ob_phy_9eaf9_event *e,
				const struct ob_phy_9eaf9_fixture *f,
				const u16 read_values[OB_PHY_9EAF9_LOGICAL_EVENT_COUNT + 1],
				const bool read_valid[OB_PHY_9EAF9_LOGICAL_EVENT_COUNT + 1],
				const u16 samples[OB_PHY_9EAF9_SYNTHETIC_SAMPLE_COUNT],
				u16 *value)
{
	switch (e->value_kind) {
	case OB_9EAF9_VALUE_LITERAL:
		*value = e->value;
		return 0;
	case OB_9EAF9_VALUE_LPF_LOW0:
	case OB_9EAF9_VALUE_LPF_HIGH0:
	case OB_9EAF9_VALUE_LPF_LOW1:
	case OB_9EAF9_VALUE_LPF_HIGH1:
		*value = ob_9eaf9_derived_word(e->value_kind, samples);
		return 0;
	case OB_9EAF9_VALUE_WORD2:
		if (!f->word2_present)
			return -EINVAL;
		*value = f->synthetic_word2;
		return 0;
	case OB_9EAF9_VALUE_SAVED_FIRST_BIT1:
	case OB_9EAF9_VALUE_SAVED_LPF_BIT1:
		if (!e->saved_event_sequence || e->saved_event_sequence > OB_PHY_9EAF9_LOGICAL_EVENT_COUNT ||
		    !read_valid[e->saved_event_sequence])
			return -EINVAL;
		*value = read_values[e->saved_event_sequence] & 0x0002u;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

int ob_phy_acphy_9eaf9_fixture_test_only(
	const struct ob_phy_9eaf9_ops *ops, void *ctx,
	const struct ob_phy_9eaf9_fixture *fixture,
	struct ob_phy_9eaf9_result *result)
{
	struct ob_phy_9eaf9_result done = { 0 };
	u16 read_values[OB_PHY_9EAF9_LOGICAL_EVENT_COUNT + 1] = { 0 };
	bool read_valid[OB_PHY_9EAF9_LOGICAL_EVENT_COUNT + 1] = { false };
	u16 samples[OB_PHY_9EAF9_SYNTHETIC_SAMPLE_COUNT] = { 0 };
	u16 callbacks = 0;
	u16 i;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	ret = ob_9eaf9_validate_target(fixture);
	if (ret)
		return ret;
	if (!fixture->caller_local_7a && !fixture->nested_32c) {
		done.kind = OB_PHY_9EAF9_SKIPPED_BY_CALLER;
		done.hardware_ready = false;
		done.historical_word2_resolved = false;
		*result = done;
		return 0;
	}
	if (fixture->policy == OB_PHY_9EAF9_HISTORICAL_STRICT) {
		done.kind = OB_PHY_9EAF9_BLOCKED_HISTORICAL_UNKNOWN;
		*result = done;
		return -ENODATA;
	}
	ret = ob_9eaf9_validate_samples(fixture);
	if (ret)
		return ret;
	if (!ops || !ops->read16 || !ops->write16 ||
	    !ops->table_read16 || !ops->table_write)
		return -EINVAL;
	ret = ob_9eaf9_validate_vector();
	if (ret)
		return ret;
	for (i = 0; i < OB_PHY_9EAF9_SYNTHETIC_SAMPLE_COUNT; i++)
		samples[i] = fixture->runtime_samples[i].value;

	for (i = 0; i < OB_PHY_9EAF9_EVENT_COUNT; i++) {
		const struct ob_phy_9eaf9_event *e = &ob_phy_9eaf9_events[i];
		u16 old, value, next;
		int j;

		switch (e->operation) {
		case OB_9EAF9_READ:
			ret = ops->read16(ctx, e->logical_event, e->source_site,
					  e->reg_or_offset, &value);
			callbacks++;
			if (ret)
				goto failed;
			read_values[e->logical_event] = value;
			read_valid[e->logical_event] = true;
			break;
		case OB_9EAF9_WRITE:
			ret = ops->write16(ctx, e->logical_event, e->source_site,
					   e->reg_or_offset, e->value);
			callbacks++;
			if (ret)
				goto failed;
			break;
		case OB_9EAF9_RMW:
			ret = ops->read16(ctx, e->logical_event, e->source_site,
					  e->reg_or_offset, &old);
			callbacks++;
			if (ret)
				goto failed;
			if (e->value_kind == OB_9EAF9_VALUE_SAVED_FIRST_BIT1 ||
			    e->value_kind == OB_9EAF9_VALUE_SAVED_LPF_BIT1) {
				ret = ob_9eaf9_table_value(e, fixture, read_values,
							   read_valid, samples, &value);
				if (ret)
					return ret;
			} else {
				value = e->value;
			}
			/* Historical phy_reg_mod: MASKED_MERGE, arguments (mask,value). */
			next = (u16)((old & (u16)~e->mask) | (value & e->mask));
			ret = ops->write16(ctx, e->logical_event, e->source_site,
					   e->reg_or_offset, next);
			callbacks++;
			if (ret)
				goto failed;
			break;
		case OB_9EAF9_TABLE_READ: {
			const struct ob_phy_9eaf9_runtime_sample *s =
				&fixture->runtime_samples[e->sample_slot];
			ret = ops->table_read16(ctx, e->logical_event, e->source_site,
						e->table_id, e->reg_or_offset, 1,
						e->element_width_bits, (u8)e->chain_index,
						OB_PHY_9EAF9_INVOCATION_CONTROLLED_1001,
						s->sample_ordinal, s->value, &value);
			callbacks++;
			if (ret)
				goto failed;
			if (value != s->value) {
				ret = -EINVAL;
				goto failed;
			}
			samples[e->sample_slot] = value;
			read_values[e->logical_event] = value;
			read_valid[e->logical_event] = true;
			break;
		}
		case OB_9EAF9_TABLE_WRITE: {
			u16 payload[48];
			u8 order[3] = { 0, 1, 2 };
			u8 count = e->batch_count;
			u8 width = e->element_width_bits;
			u16 word_count;

			if (!count)
				break; /* consumed as a member of the preceding TABLE API call */
			word_count = width == 48 ? 3 : count;
			if (word_count > sizeof(payload) / sizeof(payload[0]))
				return -EINVAL;
			if (width == 48) {
				if (count != 1 || e->table_id != 0x14 ||
				    e->reg_or_offset != 0x33 ||
				    e->value_kind != OB_9EAF9_VALUE_WORD2 ||
				    !fixture->word2_present)
					return -EINVAL;
				payload[0] = e->value; /* accepted word 0: 0xe800 */
				payload[1] = e->payload_word1; /* accepted word 1: 0x0084 */
				payload[2] = fixture->synthetic_word2;
			} else {
				for (j = 0; j < count; j++) {
					const struct ob_phy_9eaf9_event *item = &ob_phy_9eaf9_events[i + j];
					if (item->operation != OB_9EAF9_TABLE_WRITE ||
					    item->table_id != e->table_id ||
					    item->element_width_bits != width ||
					    item->reg_or_offset != e->reg_or_offset + j)
						return -EINVAL;
					ret = ob_9eaf9_table_value(item, fixture, read_values,
								   read_valid, samples, &value);
					if (ret)
						return ret;
					payload[j] = value;
				}
			}
			ret = ops->table_write(ctx, e->logical_event, e->source_site,
					       e->table_id, e->reg_or_offset, count, width,
					       width == 48 ? 16 : 16,
					       width == 48 ? order : NULL,
					       width == 48 ? 3 : 0, payload, word_count);
			callbacks++;
			if (ret)
				goto failed;
			i += count - 1;
			break;
		}
		default:
			return -EOPNOTSUPP;
		}
	}
	done.kind = OB_PHY_9EAF9_HOST_SIMULATION_COMPLETE;
	done.logical_event_count = OB_PHY_9EAF9_EVENT_COUNT;
	done.callback_count = callbacks;
	done.historical_word2_resolved = false;
	done.hardware_ready = false;
	*result = done;
	return 0;

failed:
	done.kind = OB_PHY_9EAF9_FAILED_BACKEND_OPERATION;
	done.callback_count = callbacks;
	done.hardware_ready = false;
	done.historical_word2_resolved = false;
	*result = done;
	return ret;
}

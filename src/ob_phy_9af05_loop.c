/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_9af05_loop.h"

struct ob_9af05_read_desc {
	u32 site;
	u8 table;
	u8 offset;
	u8 count;
	u16 destination;
};

static const struct ob_9af05_read_desc ob_9af05_chain1_reads[] = {
	{ 0x9b63f, 0x65, 0x00, 1,  0x04a6 + 0x78 },
	{ 0x9b665, 0x65, 0x20, 10, 0x04c4 + 0x78 },
	{ 0x9b687, 0x64, 0x60, 8,  0x0492 + 0x78 },
	{ 0x9b6a9, 0x64, 0x70, 8,  0x049c + 0x78 },
	{ 0x9b6cf, 0x65, 0x60, 8,  0x04ce + 0x78 },
	{ 0x9b6f5, 0x65, 0x70, 8,  0x04d8 + 0x78 },
};

static int ob_9af05_validate(const struct ob_9af05_loop_input *in,
			     const struct ob_9af05_loop_ops *ops)
{
	unsigned int i;

	if (!in || !ops || !ops->phy_read16 || !ops->phy_write16 ||
	    !ops->table_write || !ops->table_read)
		return -EINVAL;
	if (!in->explicitly_synthetic)
		return -EPERM;
	if (in->chip_id != 0x4352 || in->chip_revision != 3 ||
	    in->phy_revision != 0x0a || in->radio_id != 0x2069 ||
	    in->radio_revision != 4 || in->radio_class != 0 ||
	    in->chain_count != 2)
		return -ENODEV;
	if (!in->chanspec_present || !in->caller_arg_sil_present ||
	    !in->caller_arg_dl_present || !in->nested_912_present ||
	    !in->nested_66e_present || !in->nested_3e3_present ||
	    !in->nested_3e4_present || !in->nested_3e5_present ||
	    !in->nested_64a_present || !in->saved_phy_016c_present ||
	    !in->saved_phy_019e_present)
		return -EINVAL;
	if ((in->chanspec & 0xc000u) || !in->caller_arg_sil ||
	    !in->caller_arg_dl)
		return -EOPNOTSUPP;
	if (in->nested_912)
		return -EOPNOTSUPP;
	/* The selected 2-byte memcpy source is [nested+0x3e3] repeated. */
	if (in->nested_64a != 2)
		return -EOPNOTSUPP;
	for (i = 0; i < OB_9AF05_SAMPLE_COUNT; i++) {
		const struct ob_9af05_read_desc *d = &ob_9af05_chain1_reads[i];
		const struct ob_9af05_table_sample *s = &in->samples[i];

		if (!s->present || s->source_site != d->site ||
		    s->table_id != d->table || s->offset != d->offset ||
		    s->count != d->count || s->width_bits != 8 ||
		    s->length != d->count)
			return -EINVAL;
	}
	return 0;
}

static int ob_9af05_write(const struct ob_9af05_loop_ops *ops, void *ctx,
			  struct ob_9af05_loop_result *r, u32 site,
			  u8 table, u8 offset, u8 count, const u8 *bytes)
{
	int ret = ops->table_write(ctx, site, 1, table, offset, count, 8,
				   bytes, count);

	r->callback_count++;
	r->logical_event_count++;
	return ret;
}

static int ob_9af05_phy_write(const struct ob_9af05_loop_ops *ops,
			      void *ctx, struct ob_9af05_loop_result *r,
			      u32 site, u16 reg, u16 value)
{
	int ret = ops->phy_write16(ctx, site, reg, value);

	r->callback_count++;
	r->logical_event_count++;
	return ret;
}

int ob_phy_9af05_chain1_test_only(const struct ob_9af05_loop_ops *ops,
				  void *ctx,
				  const struct ob_9af05_loop_input *input,
				  struct ob_9af05_loop_result *result)
{
	static const u8 table_64_values[10] = {
		3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	};
	static const u8 table_65_values[10] = {
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	};
	struct ob_9af05_loop_result done = { 0 };
	u8 first_payload[2];
	u16 reg_value;
	unsigned int i;
	u32 current_site = 0;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_9AF05_LOOP_INVALID;
	ret = ob_9af05_validate(input, ops);
	if (ret)
		return ret;

	first_payload[0] = input->nested_3e3;
	first_payload[1] = input->nested_3e3;
	current_site = 0x9b3dd;
	ret = ob_9af05_write(ops, ctx, &done, 0x9b3dd, 0x64, 0, 2,
			     first_payload);
	if (ret)
		goto failed;

	current_site = 0x9b477;
	ret = ob_9af05_phy_write(ops, ctx, &done, 0x9b477, 0x173b,
				 input->nested_66e ? 0x001c : 0x0018);
	if (ret)
		goto failed;
	current_site = 0x9b4b7;
	ret = ob_9af05_phy_write(ops, ctx, &done, 0x9b4b7, 0x1726,
				 0x000c);
	if (ret)
		goto failed;
	current_site = 0x9b4d9;
	ret = ob_9af05_write(ops, ctx, &done, 0x9b4d9, 0x64, 0x20, 10,
			     table_64_values);
	if (ret)
		goto failed;
	current_site = 0x9b4fc;
	ret = ob_9af05_write(ops, ctx, &done, 0x9b4fc, 0x65, 0x20, 10,
			     table_65_values);
	if (ret)
		goto failed;

	for (i = 0; i < OB_9AF05_SAMPLE_COUNT; i++) {
		const struct ob_9af05_read_desc *d = &ob_9af05_chain1_reads[i];
		const struct ob_9af05_table_sample *s = &input->samples[i];

		current_site = d->site;
		ret = ops->table_read(ctx, d->site, 1, d->table, d->offset,
				      d->count, 8, d->destination, s->bytes,
				      d->count);
		done.callback_count++;
		done.logical_event_count++;
		if (ret)
			goto failed;
	}

	current_site = 0x9b719;
	ret = ob_9af05_phy_write(ops, ctx, &done, 0x9b719, 0x016c,
				 input->saved_phy_016c);
	if (ret)
		goto failed;
	current_site = 0x9b73b;
	ret = ops->phy_read16(ctx, 0x9b73b, 0x019e, &reg_value);
	done.callback_count++;
	if (ret)
		goto failed;
	reg_value = (u16)((reg_value & (u16)~0x0002u) |
			  (input->saved_phy_019e & 0x0002u));
	ret = ops->phy_write16(ctx, 0x9b73b, 0x019e, reg_value);
	done.callback_count++;
	done.logical_event_count++;
	if (ret)
		goto failed;

	/* Publish the software-only state delta only after the bounded event vector
	 * returns successfully. Historical copies are +0x3e3..+0x3e5 to nested
	 * +0x461..+0x463 and the two-byte TABLE source copy lands at +0x4e2. */
	done.nested_copy_present = true;
	done.nested_copy_offset = 0x0461;
	done.nested_copy_length = 3;
	done.nested_copy[0] = input->nested_3e3;
	done.nested_copy[1] = input->nested_3e4;
	done.nested_copy[2] = input->nested_3e5;
	done.table_source_copy_present = true;
	done.table_source_copy_offset = 0x04e2;
	done.table_source_copy_length = 2;
	done.table_source_copy[0] = input->nested_3e3;
	done.table_source_copy[1] = input->nested_3e3;
	done.status = OB_9AF05_LOOP_SYNTHETIC_COMPLETE;
	done.terminal_boundary = 0x9b751;
	done.historical_exact = false;
	done.hardware_ready = false;
	done.phy_ready = false;
	done.channel_ready = false;
	*result = done;
	return 0;

failed:
	done.status = OB_9AF05_LOOP_FAILED;
	done.failed_site = current_site;
	done.terminal_boundary = 0;
	done.historical_exact = false;
	done.hardware_ready = false;
	done.phy_ready = false;
	done.channel_ready = false;
	done.nested_copy_present = false;
	done.table_source_copy_present = false;
	*result = done;
	return ret;
}

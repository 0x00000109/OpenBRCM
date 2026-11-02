/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_99658.h"

struct ob_99658_payloads {
	u8 a[OB_PHY_99658_MAX_TRANSFER_BYTES];
	u8 b[OB_PHY_99658_MAX_TRANSFER_BYTES];
	u8 transfer_length;
	u8 prefix_length;
	u8 width;
	bool copied_state_present;
	u16 copied_state_offset;
	u8 copied_state_value;
};

static bool ob_99658_target(u16 chip, u8 chip_rev, u8 phy_rev,
			    u16 radio, u8 radio_rev, u8 radio_class,
			    u8 chains)
{
	return chip == 0x4352 && chip_rev == 3 && phy_rev == 0x0a &&
		radio == 0x2069 && radio_rev == 4 && radio_class == 0 &&
		chains == 2;
}

static int ob_99658_select_source(u8 argument, u16 chanspec, u8 nested_66e,
				 enum ob_phy_99658_source_id *id)
{
	if (!id)
		return -EINVAL;
	if (argument == 1) {
		if (!(chanspec & 0xc000u))
			*id = OB_PHY_99658_ARG1_CHANSPEC_LOW;
		else if ((chanspec & 0x3800u) == 0x1000u)
			*id = OB_PHY_99658_ARG1_BAND_1000;
		else if ((chanspec & 0x3800u) == 0x1800u)
			*id = OB_PHY_99658_ARG1_BAND_1800;
		else
			*id = OB_PHY_99658_ARG1_OTHER_BAND;
		return 0;
	}
	if (argument == 2) {
		if (chanspec & 0xc000u)
			*id = OB_PHY_99658_ARG2_HIGH_BAND;
		else if (!nested_66e)
			*id = OB_PHY_99658_ARG2_LOW_66E_ZERO;
		else
			*id = OB_PHY_99658_ARG2_LOW_66E_NONZERO;
		return 0;
	}
	return -EINVAL;
}

static int ob_99658_prepare(const struct ob_phy_99658_input *in,
			    struct ob_99658_payloads *out)
{
	enum ob_phy_99658_source_id selected;
	u8 base, delta, i;

	if (!in || !out || !ob_99658_target(in->chip_id, in->chip_revision,
						    in->phy_revision, in->radio_id,
						    in->radio_revision,
						    in->radio_class,
						    in->chain_count) ||
	    (in->argument != 1 && in->argument != 2))
		return -EINVAL;
	if (in->policy == OB_PHY_99658_HISTORICAL_STRICT)
		return -EAGAIN;
	if (in->policy != OB_PHY_99658_SYNTHETIC_PARAMETERIZED ||
	    !in->explicitly_synthetic || !in->chanspec_present ||
	    !in->table_offset_present || !in->transfer_count_present ||
	    !in->source_bytes)
		return -EINVAL;
	if (in->table_offset != (in->argument == 1 ? 8 : 16))
		return -EINVAL;
	if (in->argument == 1 && !in->nested_66a_present)
		return -EINVAL;
	if (in->argument == 2 &&
	    (!in->nested_66b_present || !in->nested_66e_present ||
	     !in->nested_340_present || !in->nested_3e0_present))
		return -EINVAL;
	if (ob_99658_select_source(in->argument, in->chanspec,
					  in->argument == 2 ? in->nested_66e : 0,
					  &selected) ||
	    selected != in->source_id)
		return -EINVAL;
	if (in->source_length != (in->argument == 1 ? 6 : 7))
		return -EINVAL;
	if (in->transfer_count != in->source_length)
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->width = 8;
	out->prefix_length = in->argument == 1 ? 6 : 7;
	out->transfer_length = out->prefix_length;

	if (in->argument == 1) {
		base = 5;
		delta = base > in->nested_66a ? (u8)(base - in->nested_66a) : 0;
	} else if ((in->chanspec & 0xc000u) || in->nested_66e ||
		   !in->nested_340) {
		base = 6;
		delta = base > in->nested_66b ? (u8)(base - in->nested_66b) : 0;
	} else {
		base = in->nested_3e0 < 10 ? 5 : 4;
		delta = base > in->nested_66b ? (u8)(base - in->nested_66b) : 0;
	}

	for (i = 0; i < out->prefix_length; i++) {
		u8 source_index;
		u8 value;

		if (!i) {
			source_index = 1;
			value = 1;
		} else if (i <= delta) {
			source_index = i;
			value = i;
		} else {
			source_index = delta;
			value = delta;
		}
		if (source_index >= in->source_length)
			return -EINVAL;
		out->a[i] = in->source_bytes[source_index];
		out->b[i] = value;
	}
	return 0;
}

static int ob_998cc_prepare(const struct ob_phy_998cc_input *in,
			    struct ob_99658_payloads *out)
{
	u8 limit, start, i;

	if (!in || !out || !ob_99658_target(in->chip_id, in->chip_revision,
						    in->phy_revision, in->radio_id,
						    in->radio_revision,
						    in->radio_class,
						    in->chain_count) ||
	    (in->argument != 1 && in->argument != 2))
		return -EINVAL;
	if (in->policy == OB_PHY_99658_HISTORICAL_STRICT)
		return -EAGAIN;
	if (in->policy != OB_PHY_99658_SYNTHETIC_PARAMETERIZED ||
	    !in->explicitly_synthetic || !in->table_offset_present ||
	    !in->nested_66c_present || !in->nested_66d_present)
		return -EINVAL;
	if (in->table_offset != (in->argument == 1 ? 8 : 16))
		return -EINVAL;
	if (in->argument == 2 &&
	    (in->source_id != OB_PHY_99658_998CC_STATIC_ROW ||
	     !in->source_bytes || in->source_length != 7))
		return -EINVAL;

	memset(out, 0, sizeof(*out));
	out->width = 8;
	out->prefix_length = in->argument == 1 ? 6 : 7;
	out->transfer_length = out->prefix_length;
	out->copied_state_present = true;
	out->copied_state_offset = in->argument == 1 ? 0x065a : 0x065b;
	out->copied_state_value = in->argument == 1 ? in->nested_66c :
		in->nested_66d;
	if (in->argument == 1) {
		static const u8 literal_prefix[6] = {
			0x0b, 0x0c, 0x0e, 0x20, 0x24, 0x28,
		};

		memcpy(out->a, literal_prefix, sizeof(literal_prefix));
		limit = 6;
		start = in->nested_66c < 5 ? (u8)(5 - in->nested_66c) : 0;
	} else {
		memcpy(out->a, in->source_bytes, 7);
		limit = 7;
		start = in->nested_66d < 6 ? (u8)(6 - in->nested_66d) : 0;
	}
	for (i = start; i < limit; i++)
		out->a[i] = 0x7f;
	return 0;
}

static int ob_99658_emit(const struct ob_phy_99658_ops *ops, void *ctx,
			 u32 site, u8 argument, u8 chain, u8 table,
			 u8 offset, u8 width, const u8 *payload,
			 u8 payload_length, struct ob_phy_99658_result *result)
{
	int ret = ops->table_write(ctx, site, argument, chain, table, offset,
				   payload_length, width, payload,
				   payload_length);

	result->callback_count++;
	result->table_api_write_count++;
	return ret;
}

static void ob_99658_record_scratch(struct ob_phy_99658_result *result,
				    u8 chain, u16 base,
				    const u8 *bytes, u8 length)
{
	struct ob_phy_99658_scratch_copy *copy =
		&result->scratch_copies[result->scratch_copy_count++];

	copy->chain = chain;
	copy->destination_offset = (u16)(base + chain * 0x78u);
	copy->length = length;
	memcpy(copy->bytes, bytes, length);
}

int ob_phy_acphy_99658_test_only(
	const struct ob_phy_99658_ops *ops, void *ctx,
	const struct ob_phy_99658_input *input,
	struct ob_phy_99658_result *result)
{
	struct ob_99658_payloads payload;
	struct ob_phy_99658_result done = { 0 };
	u8 chain;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->kind = OB_PHY_99658_RESULT_INVALID;
	ret = ob_99658_prepare(input, &payload);
	if (ret == -EAGAIN) {
		result->kind = OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT;
		result->terminal_boundary = 0x99875;
		return ret;
	}
	if (ret)
		return ret;
	if (!ops || !ops->table_write)
		return -EINVAL;
	done.copied_state_present = true;
	done.copied_state_offset = input->argument == 1 ? 0x0658 : 0x0659;
	done.copied_state_value = input->argument == 1 ? input->nested_66a :
		input->nested_66b;
	for (chain = 0; chain < OB_PHY_99658_CHAINS; chain++) {
		u8 table_a = chain ? 0x64 : 0x44;
		u8 table_b = chain ? 0x65 : 0x45;
		u16 arg_offset = (u16)input->argument * 0x0au;

		ob_99658_record_scratch(&done, chain,
					       (u16)(0x046a + arg_offset),
					       payload.a, payload.prefix_length);
		ret = ob_99658_emit(ops, ctx, 0x99875, input->argument, chain,
				    table_a, input->table_offset, payload.width,
				    payload.a, payload.transfer_length, &done);
		if (ret)
			goto failed;
		ob_99658_record_scratch(&done, chain,
					       (u16)(0x04a6 + arg_offset),
					       payload.b, payload.prefix_length);
		ret = ob_99658_emit(ops, ctx, 0x998aa, input->argument, chain,
				    table_b, input->table_offset, payload.width,
				    payload.b, payload.transfer_length, &done);
		if (ret)
			goto failed;
	}
	done.kind = OB_PHY_99658_HOST_SIMULATION_COMPLETE;
	done.terminal_boundary = 0x998bd;
	done.historical_exact = false;
	done.hardware_ready = false;
	*result = done;
	return 0;

failed:
	done.kind = OB_PHY_99658_FAILED_BACKEND_OPERATION;
	done.terminal_boundary = 0x99875;
	done.historical_exact = false;
	done.hardware_ready = false;
	*result = done;
	return ret;
}

int ob_phy_acphy_998cc_test_only(
	const struct ob_phy_99658_ops *ops, void *ctx,
	const struct ob_phy_998cc_input *input,
	struct ob_phy_99658_result *result)
{
	struct ob_99658_payloads payload;
	struct ob_phy_99658_result done = { 0 };
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->kind = OB_PHY_99658_RESULT_INVALID;
	ret = ob_998cc_prepare(input, &payload);
	if (ret == -EAGAIN) {
		result->kind = OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT;
		result->terminal_boundary = 0x99981;
		return ret;
	}
	if (ret)
		return ret;
	if (!ops || !ops->table_write)
		return -EINVAL;
	ret = ops->table_write(ctx, 0x99981, input->argument, 0, 0x0b,
			       input->table_offset, payload.transfer_length,
			       payload.width,
			       payload.a, payload.transfer_length);
	done.callback_count = 1;
	done.table_api_write_count = 1;
	done.copied_state_present = payload.copied_state_present;
	done.copied_state_offset = payload.copied_state_offset;
	done.copied_state_value = payload.copied_state_value;
	if (ret) {
		done.kind = OB_PHY_99658_FAILED_BACKEND_OPERATION;
		done.terminal_boundary = 0x99981;
		done.hardware_ready = false;
		*result = done;
		return ret;
	}
	done.kind = OB_PHY_99658_HOST_SIMULATION_COMPLETE;
	done.terminal_boundary = 0x99987;
	done.historical_exact = false;
	done.hardware_ready = false;
	*result = done;
	return 0;
}

static int ob_99658_preflight(const struct ob_phy_99658_input *in)
{
	struct ob_99658_payloads payload;

	return ob_99658_prepare(in, &payload);
}

static int ob_998cc_preflight(const struct ob_phy_998cc_input *in)
{
	struct ob_99658_payloads payload;

	return ob_998cc_prepare(in, &payload);
}

int ob_phy_9af05_table_stage_test_only(
	const struct ob_phy_99658_ops *ops, void *ctx,
	const struct ob_phy_9af05_table_stage_input *input,
	struct ob_phy_9af05_table_stage_result *result)
{
	struct ob_phy_9af05_table_stage_result done = { 0 };
	struct ob_phy_99658_result child;
	const struct ob_phy_99658_input *a;
	const struct ob_phy_998cc_input *b;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->kind = OB_PHY_99658_RESULT_INVALID;
	if (!input || !input->arg1 || !input->between || !input->arg2 ||
	    !input->after || !ops || !ops->table_write)
		return -EINVAL;
	/* Validate all four call inputs before the first callback. */
	a = input->arg1;
	b = input->between;
	if (a->argument != 1 || b->argument != 1 ||
	    input->arg2->argument != 2 || input->after->argument != 2)
		return -EINVAL;
	ret = ob_99658_preflight(a);
	if (ret == -EAGAIN) {
		result->kind = OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT;
		result->terminal_boundary = 0x99875;
		return ret;
	}
	if (ret)
		return ret;
	ret = ob_998cc_preflight(b);
	if (ret == -EAGAIN) {
		result->kind = OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT;
		result->terminal_boundary = 0x99981;
		return ret;
	}
	if (ret)
		return ret;
	ret = ob_99658_preflight(input->arg2);
	if (ret == -EAGAIN) {
		result->kind = OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT;
		result->terminal_boundary = 0x99875;
		return ret;
	}
	if (ret)
		return ret;
	ret = ob_998cc_preflight(input->after);
	if (ret == -EAGAIN) {
		result->kind = OB_PHY_99658_BLOCKED_UNRESOLVED_INPUT;
		result->terminal_boundary = 0x99981;
		return ret;
	}
	if (ret)
		return ret;

	ret = ob_phy_acphy_99658_test_only(ops, ctx, a, &child);
	done.sub_99658_writes += child.table_api_write_count;
	done.callback_count += child.callback_count;
	if (ret)
		goto failed;
	ret = ob_phy_acphy_998cc_test_only(ops, ctx, b, &child);
	done.sub_998cc_writes += child.table_api_write_count;
	done.callback_count += child.callback_count;
	if (ret)
		goto failed;
	ret = ob_phy_acphy_99658_test_only(ops, ctx, input->arg2, &child);
	done.sub_99658_writes += child.table_api_write_count;
	done.callback_count += child.callback_count;
	if (ret)
		goto failed;
	ret = ob_phy_acphy_998cc_test_only(ops, ctx, input->after, &child);
	done.sub_998cc_writes += child.table_api_write_count;
	done.callback_count += child.callback_count;
	if (ret)
		goto failed;
	done.kind = OB_PHY_99658_HOST_SIMULATION_COMPLETE;
	done.terminal_boundary = 0x9b14f;
	done.historical_exact = false;
	done.hardware_ready = false;
	*result = done;
	return 0;

failed:
	done.kind = OB_PHY_99658_FAILED_BACKEND_OPERATION;
	done.terminal_boundary = 0x9b14f;
	done.historical_exact = false;
	done.hardware_ready = false;
	*result = done;
	return ret;
}

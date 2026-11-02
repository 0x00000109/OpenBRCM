/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_9a121.h"

static int ob_9a121_validate(const struct ob_phy_9a121_input *in,
			     const struct ob_phy_9a121_ops *ops)
{
	u8 core;

	if (!in || !ops || !ops->phy_read16 || !ops->phy_write16)
		return -EINVAL;
	if (in->chip_id != 0x4352 || in->chip_revision != 3 ||
	    in->phy_revision != 0x0a || in->radio_id != 0x2069 ||
	    in->radio_revision != 4 || in->radio_class != 0 ||
	    in->chain_count != 2)
		return -ENODEV;
	if (in->policy == OB_PHY_9A121_HISTORICAL_STRICT)
		return -EAGAIN;
	if (in->policy != OB_PHY_9A121_SYNTHETIC_PARAMETERIZED ||
	    !in->explicitly_synthetic || !in->chanspec_present ||
	    !in->nested_910_present || !in->nested_911_present ||
	    !in->nested_340_present || !in->nested_341_present ||
	    !in->nested_66e_present || !in->core_mask_present)
		return -EINVAL;
	if (in->core_mask & (u8)~((1u << in->chain_count) - 1u))
		return -EINVAL;
	for (core = 0; core < in->chain_count; core++) {
		u8 i;

		if (!(in->core_mask & (1u << core)))
			continue;
		if (!in->nested_coefficients_present ||
		    !in->nested_64a_64f_present || !in->nested_66f_present ||
		    !ops->table_write || !ops->radio_read16 ||
		    !ops->radio_write16)
			return -EINVAL;
		for (i = 0; i < 6; i++)
			if (!in->nested_64a_64f[i] || in->nested_64a_64f[i] > 0x22)
				return -EINVAL;
	}
	return 0;
}

static int ob_9a121_read(const struct ob_phy_9a121_ops *ops, void *ctx,
			 struct ob_phy_9a121_result *result, u32 site,
			 u16 reg, u16 *value);

static s16 ob_9a121_signed_byte(u8 value)
{
	return value < 0x80u ? (s16)value : (s16)value - 0x100;
}

/* Exact bounded body of sub_90493. Its single hardware operation supplies
 * the low-bit selector; all remaining work is software byte arithmetic. */
static int ob_9a121_run_90493(const struct ob_phy_9a121_ops *ops,
			      void *ctx,
			      const struct ob_phy_9a121_input *input,
			      struct ob_phy_9a121_result *result,
			      u8 core, u8 argument,
			      u8 mode, u8 *return_value,
			      u16 *sample)
{
	static const s8 delta[3] = { -66, -63, -60 };
	static const u8 threshold[3] = { 25, 22, 19 };
	u8 band = (u8)((input->chanspec & 0x3800u) == 0x1000u ? 0 :
		      (input->chanspec & 0x3800u) == 0x1800u ? 1 : 2);
	u16 reg = (u16)(0x06dcu + core * 0x0200u);
	u8 base, factor, x, candidate, bound;
	s16 candidate_signed, bound_signed;
	int ret;

	ret = ob_9a121_read(ops, ctx, result, 0x90506, reg, sample);
	if (ret)
		return ret;
	result->logical_event_count++;
	base = input->nested_coefficients[0x0f + core * 3u];
	factor = input->nested_coefficients[0x10 + core * 3u];
	x = input->nested_coefficients[core * 0x78u + 0x1au +
				       (*sample & 1u)];
	if (mode)
		candidate = (u8)(base + delta[band] - (u16)x * factor);
	else
		candidate = (u8)delta[band];
	candidate = (u8)(candidate + input->nested_66f);
	bound = (u8)(0u - (u8)(argument + threshold[band]));
	candidate_signed = ob_9a121_signed_byte(candidate);
	bound_signed = ob_9a121_signed_byte(bound);
	*return_value = candidate_signed >= bound_signed ? candidate : bound;
	return 0;
}

static s8 ob_9a121_finish_next_argument(u32 value)
{
	return (s8)(u8)value;
}

static void ob_9a121_continue_active_core(
		const struct ob_phy_9a121_input *input,
		struct ob_phy_9a121_result *result, u8 core,
		u8 selected_byte, u8 shared, u16 read_after_first,
		u16 read_after_second)
{
	u8 base = input->nested_coefficients[0x0fu + core * 3u];
	u8 x1 = input->nested_coefficients[core * 0x78u + 0x1au +
					  (read_after_first & 1u)];
	u8 x2 = input->nested_coefficients[core * 0x78u + 0x1au +
					  (read_after_second & 1u)];
	u8 d1 = (u8)((selected_byte ? (u8)(base - 0x10u) : 0xf0u) - x1);
	u8 d2 = (u8)((shared ? (u8)(base - 0x10u) : 0xf0u) - x2);
	u8 limit1 = (u8)(0x17u - result->child_returns[2]);
	u8 limit2 = (u8)(0x17u - result->child_returns[3]);
	s16 sd1 = ob_9a121_signed_byte(d1);
	s16 sd2 = ob_9a121_signed_byte(d2);
	s16 slimit1 = ob_9a121_signed_byte(limit1);
	s16 slimit2 = ob_9a121_signed_byte(limit2);
	u8 updated = sd1 <= slimit1 ? d1 : limit1;
	u8 capped = sd2 > slimit2 ? limit2 : d2;
	s16 sret1 = ob_9a121_signed_byte(result->sub_90493_returns[0]);
	s16 sret2 = ob_9a121_signed_byte(result->sub_90493_returns[1]);
	u32 arg1, arg2;

	if ((input->chanspec & 0x3800u) == 0x1000u) {
		arg1 = ((u32)(s32)ob_9a121_signed_byte(updated) +
			(u32)(s32)sret1) >> 1;
		if (result->branch_flag) {
			arg2 = (u32)((s32)ob_9a121_signed_byte(capped) * -39 +
				     (s32)sret2 * -26);
		} else {
			arg2 = ((u32)(s32)ob_9a121_signed_byte(capped) +
				(u32)(s32)sret2) >> 1;
		}
	} else {
		u32 sum1 = (u32)((s32)ob_9a121_signed_byte(updated) + sret1);
		u32 sum2;

		arg1 = (u32)(-(s32)((sum1 * (u32)-39 +
				       (u32)(s32)sret1 * (u32)-26) >> 6));
		sum2 = (u32)((s32)ob_9a121_signed_byte(capped) * -39 +
			     (s32)sret2 * -26);
		arg2 = (u32)(-(s32)(sum2 >> 6));
	}
	result->next_sub_9175a_arg = ob_9a121_finish_next_argument(arg1);
	result->next_sub_91b6e_arg = ob_9a121_finish_next_argument(arg2);
}

static int ob_9a121_continue_selected_core(
		const struct ob_phy_9a121_ops *ops, void *ctx,
		const struct ob_phy_9a121_input *input,
		struct ob_phy_9a121_result *result, u8 core)
{
	u8 selected_byte = (input->chanspec & 0xc000u) ? input->nested_341 :
		input->nested_340;
	u8 shared = (u8)(selected_byte & input->nested_66e);
	u16 reg = (u16)(0x06dcu + core * 0x0200u);
	u16 sample1, sample2;
	int ret;

	ret = ob_9a121_run_90493(ops, ctx, input, result, core,
				 result->child_returns[3], shared != 0,
				 &result->sub_90493_returns[0],
				 &result->sub_90493_samples[0]);
	if (ret)
		return ret;
	ret = ob_9a121_read(ops, ctx, result, 0x9a3b4, reg, &sample1);
	if (ret)
		return ret;
	result->logical_event_count++;
	result->caller_phy_samples[0] = sample1;
	ret = ob_9a121_run_90493(ops, ctx, input, result, core,
				 result->child_returns[4], 1,
				 &result->sub_90493_returns[1],
				 &result->sub_90493_samples[1]);
	if (ret)
		return ret;
	ret = ob_9a121_read(ops, ctx, result, 0x9a42c, reg, &sample2);
	if (ret)
		return ret;
	result->logical_event_count++;
	result->caller_phy_samples[1] = sample2;
	ob_9a121_continue_active_core(input, result, core, selected_byte,
				      shared != 0, sample1, sample2);
	return 0;
}

static int ob_9a121_read(const struct ob_phy_9a121_ops *ops, void *ctx,
			 struct ob_phy_9a121_result *result, u32 site,
			 u16 reg, u16 *value)
{
	int ret = ops->phy_read16(ctx, site, reg, value);

	result->callback_count++;
	if (ret)
		result->failed_site = site;
	return ret;
}

static int ob_9a121_write(const struct ob_phy_9a121_ops *ops, void *ctx,
			  struct ob_phy_9a121_result *result, u32 site,
			  u16 reg, u16 value)
{
	int ret = ops->phy_write16(ctx, site, reg, value);

	result->callback_count++;
	if (ret)
		result->failed_site = site;
	return ret;
}

static int ob_9a121_phy_mod(const struct ob_phy_9a121_ops *ops, void *ctx,
			    struct ob_phy_9a121_result *result, u32 site,
			    u16 reg, u16 mask, u16 value)
{
	u16 old_value;
	int ret;

	ret = ob_9a121_read(ops, ctx, result, site, reg, &old_value);
	if (ret)
		return ret;
	ret = ob_9a121_write(ops, ctx, result, site, reg,
			     (u16)((old_value & (u16)~mask) | (value & mask)));
	if (!ret)
		result->logical_event_count++;
	return ret;
}

static int ob_9a121_radio_mod(const struct ob_phy_9a121_ops *ops, void *ctx,
			      struct ob_phy_9a121_result *result, u32 site,
			      u16 reg, u16 mask, u16 value)
{
	u16 old_value;
	int ret;

	ret = ops->radio_read16(ctx, site, reg, &old_value);
	result->callback_count++;
	if (ret) {
		result->failed_site = site;
		return ret;
	}
	ret = ops->radio_write16(ctx, site, reg,
				 (u16)((old_value & (u16)~mask) | (value & mask)));
	result->callback_count++;
	if (ret) {
		result->failed_site = site;
		return ret;
	}
	result->logical_event_count++;
	return 0;
}

static s32 ob_9a121_s8_at(const u8 *row, u16 offset)
{
	u8 value = row[offset];

	return value < 0x80u ? (s32)value : (s32)value - 0x100;
}

/* Selected sub_9175a dataflow. The historical category-name pointer table
 * has relocated "low" entries 0..1, "mid" entries 2..5, and "high" entries
 * 6..7. */
static int ob_9a121_run_9175a(const struct ob_phy_9a121_ops *ops, void *ctx,
			      const struct ob_phy_9a121_input *input,
			      struct ob_phy_9a121_result *result, u8 core,
			      s8 signed_argument)
{
	static const s32 limits[8] = { -40, -5, 20, 40, 55, 80, 100, 116 };
	static const u8 phy_values[8] = { 0, 0x23, 0x3c, 0x50,
					   0x5f, 0x78, 0x8c, 0x9c };
	static const u8 radio_values[8] = { 1, 0, 1, 2, 0, 3, 1, 0 };
	const u8 *nested = input->nested_coefficients + core * 0x78u;
	u16 reg_dc = (u16)(0x06dcu + core * 0x0200u);
	u16 reg_dd = (u16)(0x06ddu + core * 0x0200u);
	u16 samples[3];
	u8 index_bit, index_1_3, index_4_6, index_7_10, index_dd_4_6;
	u8 index_dd_8_10, category = 0, i;
	s32 measurement;
	bool selected_interval = false;
	int ret;

	ret = ob_9a121_read(ops, ctx, result, 0x91825, reg_dc, &samples[0]);
	if (ret)
		return ret;
	result->logical_event_count++;
	ret = ob_9a121_read(ops, ctx, result, 0x9185b, reg_dd, &samples[1]);
	if (ret)
		return ret;
	result->logical_event_count++;
	ret = ob_9a121_read(ops, ctx, result, 0x91879, reg_dd, &samples[2]);
	if (ret)
		return ret;
	result->logical_event_count++;
	memcpy(result->sub_9175a_samples[core], samples, sizeof(samples));
	index_bit = (u8)(samples[0] & 1u);
	index_1_3 = (u8)((samples[0] >> 1) & 7u);
	index_4_6 = (u8)((samples[0] >> 4) & 7u);
	index_7_10 = (u8)((samples[0] >> 7) & 15u);
	index_dd_4_6 = (u8)((samples[1] >> 4) & 7u);
	index_dd_8_10 = (u8)((samples[2] >> 8) & 7u);
	measurement = (s32)signed_argument +
		(s32)ob_9a121_s8_at(nested, (u16)(0x1a + index_bit));
	measurement += ob_9a121_s8_at(nested, (u16)(0x24 + index_1_3));
	measurement += ob_9a121_s8_at(nested, (u16)(0x2e + index_4_6));
	measurement += ob_9a121_s8_at(nested, (u16)(0x38 + index_7_10));
	measurement += ob_9a121_s8_at(nested, (u16)(0x42 +
								   ((samples[1] >> 4) & 7u)));
	if (result->nested_65c == 1)
		measurement -= input->nested_coefficients[0x0f + core * 3u];
	measurement *= 10;
	if (measurement <= limits[0]) {
		category = 0;
	} else if (measurement > limits[7]) {
		u32 difference = (u32)(measurement - limits[7]);

		if (difference > 20u && index_dd_4_6 && index_dd_8_10 != 7u) {
			u16 value = (u16)((((index_dd_4_6 - 1u) & 7u) << 4));

			ret = ob_9a121_phy_mod(ops, ctx, result, 0x9197d,
					       reg_dd, 0x0070, value);
			if (ret)
				return ret;
			ret = ob_9a121_phy_mod(ops, ctx, result, 0x919b2,
					       reg_dd, 0x0700,
					       (u16)((index_dd_8_10 << 8) | 0x0100));
			if (ret)
				return ret;
		}
		category = 7;
	} else {
		for (i = 0; i < 7; i++) {
			if (measurement < limits[i] || measurement > limits[i + 1])
				continue;
			category = (u8)(measurement - limits[i] <
					 limits[i + 1] - measurement ? i : i + 1u);
			selected_interval = true;
			break;
		}
		if (!selected_interval)
			category = 0;
	}
	result->sub_9175a_category[core] = category;
	ret = ob_9a121_phy_mod(ops, ctx, result,
			       0x91a4a, (u16)(0x06eeu + core * 0x0200u),
			       0x0003, (u16)(phy_values[category] & 3u));
	if (ret)
		return ret;
	if (category <= 1u) {
		ret = ob_9a121_radio_mod(ops, ctx, result, 0x91b57,
					 (u16)(0x0045u + core * 0x0200u),
					 0x0080,
					 (u16)(radio_values[category] << 7));
	} else if (category <= 5u) {
		ret = ob_9a121_radio_mod(ops, ctx, result, 0x91b57,
					 (u16)(0x0045u + core * 0x0200u),
					 0x0300,
					 (u16)(radio_values[category] << 8));
	} else {
		ret = ob_9a121_radio_mod(ops, ctx, result, 0x91b57,
					 (u16)(0x0045u + core * 0x0200u),
					 0x0040,
					 (u16)(radio_values[category] << 6));
	}
	return ret;
}

/* Selected sub_91b6e threshold/interpolation path. */
static int ob_9a121_run_91b6e(const struct ob_phy_9a121_ops *ops, void *ctx,
			      const struct ob_phy_9a121_input *input,
			      struct ob_phy_9a121_result *result, u8 core,
			      s8 signed_argument)
{
	static const u8 thresholds_a[12] = {
		0x00, 0x13, 0x23, 0x31, 0x3c, 0x46,
		0x50, 0x58, 0x5f, 0x66, 0x6d, 0x73,
	};
	static const u8 thresholds_c[12] = {
		0x00, 0x13, 0x23, 0x31, 0x3c, 0x46,
		0x50, 0x5c, 0x69, 0x78, 0x82, 0x8c,
	};
	const u8 *nested = input->nested_coefficients + core * 0x78u;
	s32 low[12], middle[12], high[12], measurement;
	u16 sample;
	u8 group = 0, category = 0, i;
	u16 reg_dc = (u16)(0x06dcu + core * 0x0200u);
	const s32 *selected;
	int ret;

	ret = ob_9a121_read(ops, ctx, result, 0x91cb9, reg_dc, &sample);
	if (ret)
		return ret;
	result->logical_event_count++;
	result->sub_91b6e_samples[core] = sample;
	for (i = 0; i < 12; i++) {
		low[i] = (s32)thresholds_a[i] - 0x154;
		middle[i] = low[i] + 0x3c;
		high[i] = (s32)(((input->chanspec & 0xc000u) ?
				 thresholds_a[i] : thresholds_c[i])) - 0xdc;
	}
	measurement = (s32)signed_argument + nested[0x1a + (sample & 1u)];
	measurement += ob_9a121_s8_at(nested,
				       (u16)(0x24 + ((sample >> 1) & 7u)));
	measurement -= ob_9a121_s8_at(nested, 0x29);
	if (result->nested_65c == 1)
		measurement -= input->nested_coefficients[0x0f + core * 3u];
	measurement *= 10;
	if (measurement <= low[0]) {
		group = 0;
		category = 0;
	} else if (measurement >= high[11]) {
		group = 2;
		category = 11;
	} else if (measurement > middle[11]) {
		group = 2;
		selected = high;
		goto scan;
	} else if (measurement < middle[0]) {
		group = 0;
		selected = low;
		goto scan;
	} else {
		group = 1;
		selected = middle;
		goto scan;
	}
	result->sub_91b6e_category[core] = category;
	result->sub_91b6e_band[core] = group;
	ret = ob_9a121_phy_mod(ops, ctx, result, 0x91db8,
			       (u16)(0x06eeu + core * 0x0200u), 0x000c,
			       (u16)(group << 2));
	if (ret)
		return ret;
	return ob_9a121_radio_mod(ops, ctx, result, 0x91e3f,
				  (u16)(((input->chanspec & 0xc000u) ?
					  0x0033u : 0x002cu) + core * 0x0200u),
				  0x00f0, (u16)((category + 4u) << 4));

scan:
	for (i = 0; i < 11; i++) {
		s32 lower_distance, upper_distance;

		if (measurement < selected[i] || measurement > selected[i + 1])
			continue;
		lower_distance = measurement - selected[i];
		upper_distance = selected[i + 1] - measurement;
		category = (u8)(upper_distance <= lower_distance ? i + 1u : i);
		break;
	}
	result->sub_91b6e_category[core] = category;
	result->sub_91b6e_band[core] = group;
	ret = ob_9a121_phy_mod(ops, ctx, result, 0x91db8,
			       (u16)(0x06eeu + core * 0x0200u), 0x000c,
			       (u16)(group << 2));
	if (ret)
		return ret;
	return ob_9a121_radio_mod(ops, ctx, result, 0x91e3f,
				  (u16)(((input->chanspec & 0xc000u) ?
					  0x0033u : 0x002cu) + core * 0x0200u),
				  0x00f0, (u16)((category + 4u) << 4));
}

static int ob_9a121_merge_bit1(const struct ob_phy_9a121_ops *ops,
			       void *ctx,
			       struct ob_phy_9a121_result *result,
			       u32 site, u16 value)
{
	u16 old_value;
	int ret;

	ret = ob_9a121_read(ops, ctx, result, site, 0x019e, &old_value);
	if (ret)
		return ret;
	ret = ob_9a121_write(ops, ctx, result, site, 0x019e,
			     (u16)((old_value & (u16)~0x0002u) | (value & 0x0002u)));
	if (!ret)
		result->logical_event_count++;
	return ret;
}

/* sub_8f086 is the software-only byte selector called by sub_99f29. The
 * snapshot is bounded to the historical per-core 0x78-byte stride. */
static u8 ob_9a121_8f086(const u8 nested_coefficients[0x122],
			 const u8 candidate_counts[6], u8 core,
			 u8 delta, u8 format, const u8 initial[6],
			 u8 output[6])
{
	const u8 *row = nested_coefficients + core * 0x78u;
	u8 base = nested_coefficients[0x0f + core * 3u];
	s32 accumulator = 0;
	s32 target = (s32)base + delta;
	u8 i;

	for (i = 0; i < 6; i++)
		output[i] = (u8)(initial[i] + (format ? base : 0));

	for (i = 0; i < 6; i++) {
		s32 sum = 0;
		u8 k, candidate;
		u8 current = output[i];
		bool found = false;

		/* r10b==4 is the fourth generated byte. The divisor operand is
		 * the core index (r13d); the historical target then caps CX at 30. */
		if (i == 3) {
			if (core % 3u == 2u)
				target++;
			if ((u16)target > 0x1e)
				target = 0x1e;
		}
		for (k = 1; k <= 5; k++)
			sum += row[0x1a + k * 0x0a];
		{
			s32 residual = (s32)(u8)target - sum;
			u8 limit = candidate_counts[i];

			for (candidate = limit; candidate > 0; candidate--) {
				u8 index = (u8)(candidate - 1u);
				s8 x = (s8)row[0x1a + (u8)(i * 0x0a) + index];
				u8 y = row[0x56 + (u8)(i * 0x0a) + index];
				u8 y0 = row[0x56 + (u8)(i * 0x0a)];

				if (y != y0 && (x > (s8)(u8)residual ||
				    (s32)x + accumulator > current))
					continue;
				output[i] = y;
				accumulator += x;
				target -= x;
				found = true;
				break;
			}
		}
		/* With a nonzero candidate count, index zero has y==y0 and is
		 * always accepted. This guard prevents a fabricated output if a
		 * malformed snapshot ever violates the validated loop bound. */
		if (!found)
			return 0;
	}
	return (u8)(accumulator - base);
}

int ob_phy_9a121_sub8f086_test_only(
	const u8 nested_coefficients[0x122], const u8 candidate_counts[6],
	const u8 initial[6], u8 core, u8 delta, u8 format,
	u8 output[6], u8 *helper_return)
{
	u8 i;

	if (!nested_coefficients || !candidate_counts || !initial || !output ||
	    !helper_return || core > 1)
		return -EINVAL;
	for (i = 0; i < 6; i++)
		if (!candidate_counts[i] || candidate_counts[i] > 6)
			return -EINVAL;
	*helper_return = ob_9a121_8f086(nested_coefficients, candidate_counts,
					 core, delta, format, initial, output);
	return 0;
}

static int ob_9a121_child_phy(const struct ob_phy_9a121_ops *ops,
			      void *ctx, struct ob_phy_9a121_result *result,
			      u32 site, u16 reg, u16 value)
{
	int ret = ob_9a121_write(ops, ctx, result, site, reg, value);

	if (!ret)
		result->logical_event_count++;
	return ret;
}

static int ob_9a121_run_99f29(const struct ob_phy_9a121_ops *ops,
			      void *ctx, const struct ob_phy_9a121_input *input,
			      struct ob_phy_9a121_result *result, u8 core,
			      u8 selector, u8 delta, u8 format,
			      u8 *child_return)
{
	u8 generated[6];
	u8 helper_return;
	u16 reg_base, value_a, value_b;
	u32 first_site, second_site;
	int ret;

	helper_return = ob_9a121_8f086(input->nested_coefficients,
				       input->nested_64a_64f, core, delta, format,
				       result->nested_650_655, generated);
	memcpy(result->sub99f29_generated, generated, sizeof(generated));
	/* sub_99f29 packs its six software bytes into two PHY words. */
	value_a = (u16)(((u16)generated[3] << 7) |
			((u16)generated[2] << 4) |
			((u16)generated[1] << 1));
	value_b = (u16)(((u16)generated[4] << 4) |
			((u16)generated[5] << 8) |
			(format ? 8u : 4u));
	*child_return = helper_return;

	if (selector == 0) {
		reg_base = (u16)(0x06dc + core * 0x0200u);
		first_site = 0x99fef;
		second_site = 0x99fff;
		ret = ob_9a121_child_phy(ops, ctx, result, first_site,
					 reg_base, value_a);
		if (ret)
			return ret;
		ret = ob_9a121_child_phy(ops, ctx, result, second_site,
					 (u16)(reg_base + 1u), value_b);
		if (ret)
			return ret;
		{
			u16 table_word = (u16)(((u16)generated[4] << 14) |
					       ((u16)generated[5] << 13) |
					       generated[1] |
					       ((u16)generated[3] << 6) |
					       ((u16)generated[2] << 3));
			u8 bytes[2] = { (u8)table_word, (u8)(table_word >> 8) };

			result->callback_count++;
			ret = ops->table_write(ctx, 0x9a04f,
					       (u8)(0xf9u + core), 7, 1, 16,
					       bytes, sizeof(bytes));
			if (ret) {
				result->failed_site = 0x9a04f;
				return ret;
			}
			result->logical_event_count++;
		}
		return 0;
	}

	if (selector == 1)
		reg_base = (u16)(0x06de + core * 0x0200u);
	else if (selector == 2)
		reg_base = (u16)(0x06e0 + core * 0x0200u);
	else if (selector == 3)
		reg_base = (u16)(0x06e2 + core * 0x0200u);
	else if (selector == 4)
		reg_base = (u16)(0x06e4 + core * 0x0200u);
	else
		return -EINVAL;
	if (selector == 1) {
		first_site = 0x9a079;
		second_site = 0x9a10a;
	} else if (selector == 2) {
		first_site = 0x9a0a4;
		second_site = 0x9a10a;
	} else if (selector == 3) {
		first_site = 0x9a0cf;
		second_site = 0x9a10a;
	} else {
		first_site = 0x9a0fa;
		second_site = 0x9a10a;
	}
	ret = ob_9a121_child_phy(ops, ctx, result, first_site,
				 reg_base, value_a);
	if (ret)
		return ret;
	return ob_9a121_child_phy(ops, ctx, result, second_site,
				  (u16)(reg_base + 1u), value_b);
}

int ob_phy_9a121_sub99f29_selector0_test_only(
	const struct ob_phy_9a121_ops *ops, void *ctx,
	const struct ob_phy_9a121_input *input, const u8 seed[6],
	u8 core, u8 delta, u8 format, struct ob_phy_9a121_result *result)
{
	u8 i;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_9A121_INVALID;
	if (!input || !seed || !ops || !ops->phy_write16 ||
	    !ops->table_write)
		return -EINVAL;
	if (input->chip_id != 0x4352 || input->chip_revision != 3 ||
	    input->phy_revision != 0x0a || input->radio_id != 0x2069 ||
	    input->radio_revision != 4 || input->radio_class != 0 ||
	    input->chain_count != 2)
		return -ENODEV;
	if (input->policy != OB_PHY_9A121_SYNTHETIC_PARAMETERIZED ||
	    !input->explicitly_synthetic || core >= input->chain_count ||
	    !input->nested_coefficients_present ||
	    !input->nested_64a_64f_present)
		return -EINVAL;
	for (i = 0; i < 6; i++)
		if (!input->nested_64a_64f[i] ||
		    input->nested_64a_64f[i] > 0x22)
			return -EINVAL;
	memcpy(result->nested_650_655, seed, 6);
	result->nested_state_present = true;
	ret = ob_9a121_run_99f29(ops, ctx, input, result, core, 0,
				 delta, format, &result->child_returns[0]);
	if (ret) {
		result->status = OB_9A121_FAILED_BACKEND_OPERATION;
		result->terminal_boundary = 0;
		return ret;
	}
	result->status = OB_9A121_SYNTHETIC_COMPLETE;
	result->terminal_boundary = 0x9a7aa;
	result->hardware_ready = false;
	result->phy_ready = false;
	result->channel_ready = false;
	return 0;
}

static int ob_9a121_active_child_prefix(const struct ob_phy_9a121_ops *ops,
					void *ctx,
					const struct ob_phy_9a121_input *input,
					struct ob_phy_9a121_result *result,
					u8 core, u32 *site)
{
	u8 selected_byte = (input->chanspec & 0xc000u) ? input->nested_341 :
		input->nested_340;
	u8 config = input->nested_66e;
	u8 shared = (u8)(selected_byte & config);
	u8 mode = (u8)((config | 1u) & selected_byte);
	u8 delta4, delta5;
	static const u8 selector_order[5] = { 0, 1, 2, 4, 3 };
	u8 deltas[5] = { 0x45, 0x30, 0x23, 0, 0 };
	u8 formats[5] = { 0, 0, 0, 0, 1 };
	u8 i;
	int ret;

	if (shared) {
		delta4 = 15;
		delta5 = 15;
	} else if (!selected_byte) {
		delta4 = 19;
		delta5 = 3;
	} else {
		delta4 = 32;
		delta5 = 30;
	}
	deltas[3] = delta4;
	deltas[4] = delta5;
	formats[2] = mode;
	formats[3] = mode;

	*site = 0x9a304;
	{
		u16 discarded;
		ret = ob_9a121_read(ops, ctx, result, *site,
				    (u16)(0x06dc + core * 0x0200u), &discarded);
		if (ret)
			return ret;
		result->logical_event_count++;
	}
	for (i = 0; i < 5; i++) {
		switch (i) {
		case 0: *site = 0x9a319; break;
		case 1: *site = 0x9a331; break;
		case 2: *site = 0x9a34c; break;
		case 3: *site = 0x9a368; break;
		default: *site = 0x9a383; break;
		}
		ret = ob_9a121_run_99f29(ops, ctx, input, result, core,
					  selector_order[i], deltas[i], formats[i],
					  &result->child_returns[i]);
		if (ret)
			return ret;
	}
	return 0;
}

int ob_phy_9a121_test_only(const struct ob_phy_9a121_ops *ops, void *ctx,
			   const struct ob_phy_9a121_input *input,
			   struct ob_phy_9a121_result *result)
{
	static const u8 low_band[5] = { 0x2b, 0x2b, 0x2b, 0x34, 0x34 };
	static const u8 high_band[5] = { 0x2f, 0x2f, 0x2f, 0x34, 0x34 };
	struct ob_phy_9a121_result done = { 0 };
	u16 saved_019e;
	u32 band;
	bool flag;
	u8 core;
	u32 site = 0;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_9A121_INVALID;
	result->selected_core = 0xff;
	ret = ob_9a121_validate(input, ops);
	if (ret)
		return ret;
	if (input->policy == OB_PHY_9A121_HISTORICAL_STRICT)
		return -EAGAIN;

	/* Caller initialization, selected profile, chanspec and external nested
	 * fields remain explicit synthetic inputs. */
	band = input->chanspec & 0xc000u;
	flag = (!band && (input->nested_910 & 0x01u) != 0) ||
		(band == 0xc000u && (input->nested_911 & 0x01u) != 0);
	done.branch_flag = flag;
	site = 0x9a1af;
	ret = ob_9a121_read(ops, ctx, &done, site, 0x019e, &saved_019e);
	if (ret)
		goto failed;
	done.logical_event_count++;

	site = 0x9a1ca;
	ret = ob_9a121_merge_bit1(ops, ctx, &done, site, 0x0002);
	if (ret)
		goto failed;

	if (!band)
		memcpy(done.nested_650_655, low_band, sizeof(low_band));
	else
		memcpy(done.nested_650_655, high_band, sizeof(high_band));
	/* The sixth stored byte is the fixed 0x64. The nested+0x340/+0x341
	 * inputs are consumed by later arithmetic, not copied into this array. */
	done.nested_650_655[5] = 0x64;
	done.nested_65c = input->nested_66e;
	done.nested_state_present = true;

	/* Each core iteration tests PI+0xa7. A zero mask has no child hardware
	 * effects, so the complete selected helper path is known for that explicit
	 * synthetic scenario. Active-core child calls begin at 0x9a319 and are not
	 * hidden behind an opaque callback. */
	for (core = 0; core < input->chain_count; core++) {
		if (!(input->core_mask & (1u << core)))
			continue;
		done.selected_core = core;
		ret = ob_9a121_active_child_prefix(ops, ctx, input, &done,
						   core, &site);
		if (ret)
			goto failed;
		ret = ob_9a121_continue_selected_core(ops, ctx, input, &done,
						      core);
		if (ret)
			goto failed;
		ret = ob_9a121_run_9175a(ops, ctx, input, &done, core,
					 done.next_sub_9175a_arg);
		if (ret) {
			if (ret == -EOPNOTSUPP &&
			    done.status == OB_9A121_HOST_SUPPORTED_PREFIX) {
				done.selected_core = core;
				done.hardware_ready = false;
				done.phy_ready = false;
				done.channel_ready = false;
				*result = done;
				return ret;
			}
			goto failed;
		}
		ret = ob_9a121_run_91b6e(ops, ctx, input, &done, core,
					 done.next_sub_91b6e_arg);
		if (ret)
			goto failed;
	}

	site = 0x9a525;
	ret = ob_9a121_merge_bit1(ops, ctx, &done, site, saved_019e);
	if (ret)
		goto failed;
	done.status = OB_9A121_SYNTHETIC_COMPLETE;
	done.terminal_boundary = 0x9a538;
	done.hardware_ready = false;
	done.phy_ready = false;
	done.channel_ready = false;
	*result = done;
	return 0;

failed:
	done.status = OB_9A121_FAILED_BACKEND_OPERATION;
	if (!done.failed_site)
		done.failed_site = site;
	done.terminal_boundary = 0;
	done.hardware_ready = false;
	done.phy_ready = false;
	done.channel_ready = false;
	done.nested_state_present = false;
	*result = done;
	return ret;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_txpwr_index.h"

#define PHY_019E 0x019eu

struct ob_txpwr_scratch {
	u16 word0;
	u16 word2;
	u16 word4;
	u16 word8;
};

struct ob_txpwr_exec {
	const struct ob_phy_txpwr_index_ops *ops;
	void *ctx;
	struct ob_phy_txpwr_index_result *result;
};

static int ob_txpwr_read(struct ob_txpwr_exec *exec, u32 site, u16 reg,
			u16 *value, bool logical)
{
	int ret;

	if (logical)
		exec->result->logical_event_count++;
	exec->result->callback_count++;
	ret = exec->ops->phy_read16(exec->ctx, site, reg, value);
	if (ret)
		exec->result->failed_site = site;
	return ret;
}

static int ob_txpwr_write(struct ob_txpwr_exec *exec, u32 site, u16 reg,
			 u16 value)
{
	int ret;

	exec->result->callback_count++;
	ret = exec->ops->phy_write16(exec->ctx, site, reg, value);
	if (ret)
		exec->result->failed_site = site;
	return ret;
}

static int ob_txpwr_rmw019e(struct ob_txpwr_exec *exec, u32 site,
			    u16 mask, u16 value)
{
	u16 old, next;
	int ret;

	exec->result->logical_event_count++;
	ret = ob_txpwr_read(exec, site, PHY_019E, &old, false);
	if (ret)
		return ret;
	next = (u16)((old & (u16)~mask) | (value & mask));
	return ob_txpwr_write(exec, site, PHY_019E, next);
}

static int ob_txpwr_table_read(struct ob_txpwr_exec *exec, u32 site,
			       s8 offset, u8 payload[OB_PHY_TXPWR_INDEX_TABLE_READ_BYTES])
{
	int ret;

	exec->result->logical_event_count++;
	exec->result->callback_count++;
	ret = exec->ops->table_read(exec->ctx, site, 0x20, (s16)offset,
				    1, 48, payload,
				    OB_PHY_TXPWR_INDEX_TABLE_READ_BYTES);
	if (ret)
		exec->result->failed_site = site;
	return ret;
}

static int ob_txpwr_table_write16(struct ob_txpwr_exec *exec, u32 site,
				  u8 table, u16 offset, u16 value)
{
	u8 payload[2] = { (u8)value, (u8)(value >> 8) };
	int ret;

	exec->result->logical_event_count++;
	exec->result->callback_count++;
	ret = exec->ops->table_write(exec->ctx, site, table, offset, 1, 16,
				     payload, sizeof(payload));
	if (ret)
		exec->result->failed_site = site;
	return ret;
}

static int ob_txpwr_sub9868f(struct ob_txpwr_exec *exec, s8 power_index,
			     struct ob_txpwr_scratch *scratch)
{
	u8 bytes[OB_PHY_TXPWR_INDEX_TABLE_READ_BYTES];
	u16 saved;
	int ret;

	ret = ob_txpwr_read(exec, 0x986ac, PHY_019E, &saved, true);
	if (ret)
		return ret;
	ret = ob_txpwr_rmw019e(exec, 0x986c6, 0x0002, 0x0002);
	if (ret)
		return ret;
	ret = ob_txpwr_table_read(exec, 0x986e6, power_index, bytes);
	if (ret)
		return ret;

	/* Direct instructions at 0x986ff..0x9871b: d1 + (d2 << 8),
	 * truncated to AX. The d3 contribution above bit 15 is discarded. */
	scratch->word0 = (u16)((u16)bytes[1] + ((u16)bytes[2] << 8));
	scratch->word2 = (u16)((u16)bytes[3] + ((u16)bytes[4] << 8));
	scratch->word4 = bytes[5];
	scratch->word8 = bytes[0];

	return ob_txpwr_rmw019e(exec, 0x9873f, 0x0002,
				(u16)(((saved >> 1) & 1u) << 1));
}

static void ob_txpwr_sub9c4e4_offsets(u8 core, u8 *first, u8 *second)
{
	static const u8 first_by_core[OB_PHY_TXPWR_INDEX_MAX_CORES] = {
		0x63, 0x67, 0x6b, 0x6f,
	};
	static const u8 second_by_core[OB_PHY_TXPWR_INDEX_MAX_CORES] = {
		0x73, 0x77, 0x7b, 0x7f,
	};

	*first = first_by_core[core];
	*second = second_by_core[core];
}

static int ob_txpwr_sub9c4e4(struct ob_txpwr_exec *exec,
			     const struct ob_txpwr_scratch *scratch, u8 core)
{
	u16 saved;
	u8 first, second;
	int ret;

	ob_txpwr_sub9c4e4_offsets(core, &first, &second);
	ret = ob_txpwr_read(exec, 0x9c535, PHY_019E, &saved, true);
	if (ret)
		return ret;
	ret = ob_txpwr_rmw019e(exec, 0x9c54f, 0x0002, 0x0002);
	if (ret)
		return ret;
	ret = ob_txpwr_table_write16(exec, 0x9c573, 0x0c, first,
				     scratch->word8);
	if (ret)
		return ret;
	ret = ob_txpwr_table_write16(exec, 0x9c594, 0x0c, second,
				     scratch->word8);
	if (ret)
		return ret;
	return ob_txpwr_rmw019e(exec, 0x9c5b1, 0x0002,
				(u16)(((saved >> 1) & 1u) << 1));
}

static int ob_txpwr_valid_input(const struct ob_phy_txpwr_index_ops *ops,
				const struct ob_phy_txpwr_index_input *input)
{
	bool selected = false;
	u8 core;

	if (!ops || !ops->phy_read16 || !ops->phy_write16 ||
	    !ops->table_read || !ops->table_write || !input ||
	    !input->explicitly_synthetic || input->chip_id != 0x4352 ||
	    input->chip_revision != 3 || input->phy_type != 0x0b ||
	    input->radio_id != 0x2069 || input->radio_revision != 4 ||
	    input->radio_class != 0 ||
	    input->core_bound > OB_PHY_TXPWR_INDEX_MAX_CORES)
		return -EINVAL;
	for (core = 0; core < input->core_bound; core++)
		if (input->core_mask & (u8)(1u << core))
			selected = true;
	if (selected && (!input->nested_indices ||
			 input->nested_indices_length < input->core_bound))
		return -EINVAL;
	return 0;
}

int ob_phy_txpwr_by_index_test_only(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	const struct ob_phy_txpwr_index_input *input,
	struct ob_phy_txpwr_index_result *result)
{
	struct ob_phy_txpwr_index_result done = { 0 };
	struct ob_txpwr_exec exec = { .ops = ops, .ctx = ctx,
				      .result = &done };
	u16 saved;
	u16 core;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_PHY_TXPWR_INDEX_INVALID;
	ret = ob_txpwr_valid_input(ops, input);
	if (ret)
		return ret;

	ret = ob_txpwr_read(&exec, 0x9ce1e, PHY_019E, &saved, true);
	if (ret)
		goto failed;
	ret = ob_txpwr_rmw019e(&exec, 0x9ce39, 0x0002, 0x0002);
	if (ret)
		goto failed;

	for (core = 0; core < input->core_bound; core++) {
		struct ob_txpwr_scratch scratch = { 0 };
		u8 *index_byte;

		if (!(input->core_mask & (u8)(1u << core)))
			continue;
		ret = ob_txpwr_sub9868f(&exec, input->power_index, &scratch);
		if (ret)
			goto failed;
		ret = ob_txpwr_table_write16(&exec, 0x9cea3, 7,
					     (u16)(0x100 + core),
					     scratch.word0);
		if (ret)
			goto failed;
		ret = ob_txpwr_table_write16(&exec, 0x9cec7, 7,
					     (u16)(0x103 + core),
					     scratch.word2);
		if (ret)
			goto failed;
		ret = ob_txpwr_table_write16(&exec, 0x9ceeb, 7,
					     (u16)(0x106 + core),
					     scratch.word4);
		if (ret)
			goto failed;
		ret = ob_txpwr_sub9c4e4(&exec, &scratch, (u8)core);
		if (ret)
			goto failed;
		index_byte = &input->nested_indices[core];
		*index_byte = (u8)input->power_index;
		done.logical_event_count++;
		done.selected_core_count++;
	}

	ret = ob_txpwr_rmw019e(&exec, 0x9cf3f, 0x0002,
				(u16)(((saved >> 1) & 1u) << 1));
	if (ret)
		goto failed;
	done.status = OB_PHY_TXPWR_INDEX_SYNTHETIC_COMPLETE;
	done.terminal_boundary = 0x9cf52;
	done.hardware_ready = false;
	*result = done;
	return 0;

failed:
	done.status = OB_PHY_TXPWR_INDEX_FAILED_BACKEND;
	done.terminal_boundary = done.failed_site;
	done.hardware_ready = false;
	*result = done;
	return ret;
}

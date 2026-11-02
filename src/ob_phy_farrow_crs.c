/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_farrow_crs.h"

/* Keep callback provenance on the immutable through-CRS sequence rather than
 * renumbering this extracted local block from one. */
#define OB_EVENT(n) ((u16)(4263u + (n)))

static const u16 ob_phy_farrow_registers[12] = {
	0x019a, 0x019b, 0x019c, 0x0199,
	0x01a1, 0x01a2, 0x01a3, 0x01a0,
	0x1603, 0x1602, 0x1607, 0x1606,
};

static const u32 ob_phy_farrow_row_sites[12] = {
	0x0a4d36, 0x0a4d4b, 0x0a4d62, 0x0a4d79,
	0x0a4d8e, 0x0a4da3, 0x0a4dba, 0x0a4dd1,
	0x0a4eab, 0x0a4eef, 0x0a4f33, 0x0a4f77,
};

static const u32 ob_phy_farrow_tail_sites[6] = {
	0x0a5c40, 0x0a5c58, 0x0a5c70,
	0x0a5c88, 0x0a5ca0, 0x0a5cb8,
};

static const u32 ob_phy_revision_sites[4] = {
	0x0a5ddd, 0x0a5df0, 0x0a5e00, 0x0a5e10,
};

static const u16 ob_phy_crs_registers[16] = {
	0x0324, 0x0330, 0x0321, 0x032d,
	0x032a, 0x0336, 0x0327, 0x0333,
	0x0910, 0x0910, 0x0912, 0x0912,
	0x0911, 0x0911, 0x0913, 0x0913,
};

static const u16 ob_phy_crs_masks[16] = {
	0x00ff, 0x00ff, 0x00ff, 0x00ff,
	0x00ff, 0x00ff, 0x00ff, 0x00ff,
	0xff00, 0x00ff, 0xff00, 0x00ff,
	0x00ff, 0xff00, 0x00ff, 0xff00,
};

static int ob_farrow_validate_profile(const struct ob_phy_farrow_fixture *f)
{
	if (!f || !f->explicitly_synthetic || f->chip_id != 0x4352 ||
	    f->chip_revision != 3 || f->phy_revision != 0x0a ||
	    f->radio_id != 0x2069 || f->radio_revision != 4 ||
	    f->radio_class != 0 || f->chanspec != 0x1001 ||
	    f->pi_16e != 0 || f->pi_168 != 2 || f->pi_164 != 0x0a ||
	    f->nested_348 != 0 || f->nested_912 != 0 || f->shared_a5 != 0x03)
		return -EINVAL;
	return 0;
}

static int ob_farrow_validate_selected_inputs(const struct ob_phy_farrow_fixture *f)
{
	u8 i;
	static const u16 offsets[2] = { 0x03cd, 0x03dd };
	static const u8 chains[2] = { 0, 1 };

	for (i = 0; i < OB_PHY_FARROW_TABLE_SAMPLES; i++) {
		const struct ob_phy_farrow_table_sample *s = &f->table_samples[i];

		if (!s->valid || s->table_id != 7 || s->offset != offsets[i] ||
		    s->element_count != 1 || s->element_width_bits != 16 ||
		    s->chain_index != chains[i] || s->invocation_id != 1 ||
		    s->sample_ordinal != i + 1)
			return -EINVAL;
	}
	if (!f->qword_40_row.present || f->qword_40_row.channel_key != 1 ||
	    f->qword_40_row.row_index != 63 ||
	    f->qword_40_row.frequency_word != 0x096c ||
	    !f->rx_farrow_row.present || f->rx_farrow_row.channel_key != 1 ||
	    f->rx_farrow_row.row_index != 109 ||
	    !f->tx_farrow_row.present || f->tx_farrow_row.channel_key != 1 ||
	    f->tx_farrow_row.row_index != 109)
		return -EINVAL;
	return 0;
}

static int ob_farrow_require_ops(const struct ob_phy_farrow_crs_ops *ops)
{
	if (!ops || !ops->read16 || !ops->write16 || !ops->table_read16 ||
	    !ops->table_write16 || !ops->api_call)
		return -EINVAL;
	return 0;
}

static int ob_farrow_read(const struct ob_phy_farrow_crs_ops *ops, void *ctx,
			  u16 event, u32 site, u16 reg, u16 *value,
			  u16 *callbacks)
{
	int ret = ops->read16(ctx, event, site, reg, value);

	(*callbacks)++;
	return ret;
}

static int ob_farrow_write(const struct ob_phy_farrow_crs_ops *ops, void *ctx,
			   u16 event, u32 site, u16 reg, u16 value,
			   u16 *callbacks)
{
	int ret = ops->write16(ctx, event, site, reg, value);

	(*callbacks)++;
	return ret;
}

static int ob_farrow_rmw(const struct ob_phy_farrow_crs_ops *ops, void *ctx,
			 u16 event, u32 site, u16 reg, u16 mask, u16 insert,
			 u16 *callbacks)
{
	u16 old;
	u16 next;
	int ret;

	ret = ob_farrow_read(ops, ctx, event, site, reg, &old, callbacks);
	if (ret)
		return ret;
	/* All selected helpers use the accepted phy_reg_mod MASKED_MERGE mode. */
	next = (u16)((old & (u16)~mask) | (insert & mask));
	return ob_farrow_write(ops, ctx, event, site, reg, next, callbacks);
}

static int ob_farrow_api(const struct ob_phy_farrow_crs_ops *ops, void *ctx,
			 u16 event, u32 site,
			 enum ob_phy_farrow_api_call operation, u32 max_wait,
			 u16 *callbacks)
{
	int ret = ops->api_call(ctx, event, site, operation, max_wait);

	(*callbacks)++;
	return ret;
}

static int ob_farrow_table_read(const struct ob_phy_farrow_crs_ops *ops,
				void *ctx, u16 event, u32 site,
				const struct ob_phy_farrow_table_sample *s,
				u16 *callbacks)
{
	u16 discarded;
	int ret = ops->table_read16(ctx, event, site, s->table_id, s->offset,
				    s->element_count, s->element_width_bits,
				    s->chain_index, s->invocation_id,
				    s->sample_ordinal, s->value, &discarded);

	(*callbacks)++;
	/* Direct slicing shows this local destination is not consumed on rev 0x0a. */
	return ret;
}

static int ob_farrow_table_write(const struct ob_phy_farrow_crs_ops *ops,
				 void *ctx, u16 event, u32 site, u16 offset,
				 u8 chain, u16 *callbacks)
{
	int ret = ops->table_write16(ctx, event, site, 7, offset, 1, 16,
				     chain, 0);
	(*callbacks)++;
	return ret;
}

int ob_phy_acphy_farrow_crs_fixture_test_only(
	const struct ob_phy_farrow_crs_ops *ops, void *ctx,
	const struct ob_phy_farrow_fixture *fixture,
	struct ob_phy_farrow_crs_result *result)
{
	struct ob_phy_farrow_crs_result done = { 0 };
	u16 callbacks = 0;
	u16 logical = 0;
	u16 saved_0601;
	u16 saved_019e;
	u16 i;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	ret = ob_farrow_validate_profile(fixture);
	if (ret)
		return ret;
	if (!fixture->qword_40_nonnull && !fixture->qword_48_nonnull &&
	    !fixture->qword_50_nonnull && !fixture->qword_58_nonnull) {
		done.kind = OB_PHY_FARROW_CRS_SKIPPED_BY_CALLER;
		*result = done;
		return 0;
	}
	if (!fixture->qword_40_nonnull || fixture->qword_48_nonnull ||
	    fixture->qword_50_nonnull || fixture->qword_58_nonnull)
		return -EOPNOTSUPP;
	ret = ob_farrow_validate_selected_inputs(fixture);
	if (ret)
		return ret;
	ret = ob_farrow_require_ops(ops);
	if (ret)
		return ret;

	/* Twelve ordered selected row-backed PHY writes. */
	for (i = 0; i < 8; i++) {
		const u16 value = fixture->rx_farrow_row.words[i & 3u];

		ret = ob_farrow_write(ops, ctx, OB_EVENT(i + 1),
				      ob_phy_farrow_row_sites[i],
				      ob_phy_farrow_registers[i], value,
				      &callbacks);
		if (ret)
			goto failed;
		logical++;
	}
	for (i = 0; i < 4; i++) {
		const u16 value = fixture->tx_farrow_row.words[i];
		u16 index = (u16)(i + 8);

		ret = ob_farrow_write(ops, ctx, OB_EVENT(index + 1),
				      ob_phy_farrow_row_sites[index],
				      ob_phy_farrow_registers[index], value,
				      &callbacks);
		if (ret)
			goto failed;
		logical++;
	}

	ret = ob_farrow_read(ops, ctx, OB_EVENT(13), 0x0a53b1, 0x0601,
			     &saved_0601, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_write(ops, ctx, OB_EVENT(14), 0x0a53fc, 0x1601,
			      saved_0601, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_read(ops, ctx, OB_EVENT(15), 0x0a54f0, 0x019e,
			     &saved_019e, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_rmw(ops, ctx, OB_EVENT(16), 0x0a5512, 0x019e,
			    0x0002, 0x0002, &callbacks);
	if (ret)
		goto failed;
	logical++;

	ret = ob_farrow_table_read(ops, ctx, OB_EVENT(17), 0x0a55a7,
				    &fixture->table_samples[0], &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_table_write(ops, ctx, OB_EVENT(18), 0x0a55e6,
				     0x03cd, 0, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_table_read(ops, ctx, OB_EVENT(19), 0x0a55a7,
				    &fixture->table_samples[1], &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_table_write(ops, ctx, OB_EVENT(20), 0x0a55e6,
				     0x03dd, 1, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_rmw(ops, ctx, OB_EVENT(21), 0x0a5660, 0x019e,
			    0x0002, (u16)(saved_019e & 0x0002), &callbacks);
	if (ret)
		goto failed;
	logical++;

	for (i = 0; i < 6; i++) {
		ret = ob_farrow_write(ops, ctx, OB_EVENT(22 + i),
				      ob_phy_farrow_tail_sites[i],
				      (u16)(0x0371 + i),
				      fixture->qword_40_row.phy_tail[i],
				      &callbacks);
		if (ret)
			goto failed;
		logical++;
	}
	ret = ob_farrow_rmw(ops, ctx, OB_EVENT(28), 0x0a5d54, 0x0164,
			    0x0010, 0x0000, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_rmw(ops, ctx, OB_EVENT(29), 0x0a5d68, 0x030f,
			    0xc000, 0x0000, &callbacks);
	if (ret)
		goto failed;
	logical++;
	for (i = 0; i < 4; i++) {
		ret = ob_farrow_write(ops, ctx, OB_EVENT(30 + i),
				      ob_phy_revision_sites[i],
				      (u16)(0x031c + i), 0x00ff, &callbacks);
		if (ret)
			goto failed;
		logical++;
	}

	/* Accepted CRS wrapper: two entry API calls, 16 PHY RMWs, two exits. */
	ret = ob_farrow_api(ops, ctx, OB_EVENT(34), 0x0092e22,
			    OB_PHY_FARROW_SUSPEND_MAC_AND_WAIT, 148580, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_api(ops, ctx, OB_EVENT(35), 0x0092e2a,
			    OB_PHY_FARROW_PHYREG_ENTER, 0, &callbacks);
	if (ret)
		goto failed;
	logical++;
	for (i = 0; i < 16; i++) {
		const u16 insert = i < 8 ? 0x0050 : 0;

		ret = ob_farrow_rmw(ops, ctx, OB_EVENT(36 + i), 0x0008f41b,
				    ob_phy_crs_registers[i], ob_phy_crs_masks[i],
				    insert, &callbacks);
		if (ret)
			goto failed;
		logical++;
	}
	ret = ob_farrow_api(ops, ctx, OB_EVENT(52), 0x0092e46,
			    OB_PHY_FARROW_PHYREG_EXIT, 0, &callbacks);
	if (ret)
		goto failed;
	logical++;
	ret = ob_farrow_api(ops, ctx, OB_EVENT(53), 0x0092e53,
			    OB_PHY_FARROW_ENABLE_MAC, 0, &callbacks);
	if (ret)
		goto failed;
	logical++;

	done.kind = OB_PHY_FARROW_CRS_HOST_SIMULATION_COMPLETE;
	done.logical_events_completed = logical;
	done.callback_records = callbacks;
	done.terminal_return_site = 0x0a5fad;
	done.historically_exact = false;
	done.hardware_ready = false;
	done.phy_ready = false;
	done.channel_ready = false;
	*result = done;
	return 0;

failed:
	done.kind = OB_PHY_FARROW_CRS_FAILED_BACKEND;
	done.logical_events_completed = logical;
	done.callback_records = callbacks;
	*result = done;
	return ret;
}

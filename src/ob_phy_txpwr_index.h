/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_TXPWR_INDEX_H_
#define _OB_PHY_TXPWR_INDEX_H_

#include "ob_phy.h"

#define OB_PHY_TXPWR_INDEX_MAX_CORES 4u
#define OB_PHY_TXPWR_INDEX_TABLE_READ_BYTES 6u

enum ob_phy_txpwr_index_status {
	OB_PHY_TXPWR_INDEX_INVALID = 0,
	OB_PHY_TXPWR_INDEX_SYNTHETIC_COMPLETE,
	OB_PHY_TXPWR_INDEX_FAILED_BACKEND,
};

struct ob_phy_txpwr_index_input {
	/* This runner is deliberately synthetic-only and has no production caller. */
	bool explicitly_synthetic;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_type;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u8 core_mask; /* historical arg1, narrowed to u8 */
	u8 core_bound; /* PI+0x168 snapshot, bounded by sub_9c4e4's four entries */
	s8 power_index; /* historical arg2, sign-extended from u8 */
	u8 *nested_indices; /* mutable nested+0x10+core byte state */
	u8 nested_indices_length;
};

struct ob_phy_txpwr_index_ops {
	int (*phy_read16)(void *ctx, u32 source_site, u16 reg, u16 *value);
	int (*phy_write16)(void *ctx, u32 source_site, u16 reg, u16 value);
	int (*table_read)(void *ctx, u32 source_site, u8 table_id,
			   s16 offset, u8 count, u8 width_bits,
			   u8 *payload, u8 payload_length);
	int (*table_write)(void *ctx, u32 source_site, u8 table_id,
			    u16 offset, u8 count, u8 width_bits,
			    const u8 *payload, u8 payload_length);
};

struct ob_phy_txpwr_index_result {
	enum ob_phy_txpwr_index_status status;
	u16 logical_event_count;
	u16 callback_count;
	u8 selected_core_count;
	u32 terminal_boundary;
	u32 failed_site;
	bool hardware_ready;
};

/* Execute wlc_phy_txpwr_by_index_acphy@0x9cdf8 and its selected
 * sub_9868f/sub_9c4e4 children for explicit synthetic state only. */
int ob_phy_txpwr_by_index_test_only(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	const struct ob_phy_txpwr_index_input *input,
	struct ob_phy_txpwr_index_result *result);

#endif /* _OB_PHY_TXPWR_INDEX_H_ */

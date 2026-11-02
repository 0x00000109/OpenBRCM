/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_FARROW_CRS_H_
#define _OB_PHY_FARROW_CRS_H_

#include "ob_phy_post_lpf.h"

#define OB_PHY_FARROW_RX_WORDS 4u
#define OB_PHY_FARROW_TX_WORDS 4u
#define OB_PHY_FARROW_TABLE_SAMPLES 2u
#define OB_PHY_FARROW_CRS_LOGICAL_EVENTS 53u
#define OB_PHY_FARROW_CRS_CALLBACKS 73u

enum ob_phy_farrow_api_call {
	OB_PHY_FARROW_SUSPEND_MAC_AND_WAIT,
	OB_PHY_FARROW_PHYREG_ENTER,
	OB_PHY_FARROW_PHYREG_EXIT,
	OB_PHY_FARROW_ENABLE_MAC,
};

struct ob_phy_farrow_channel_row {
	bool present;
	u8 channel_key;
	u16 row_index;
	u16 frequency_word;
	u16 phy_tail[6];
};

struct ob_phy_farrow_static_row {
	bool present;
	u8 channel_key;
	u16 row_index;
	u16 words[4];
};

struct ob_phy_farrow_table_sample {
	bool valid;
	u8 table_id;
	u16 offset;
	u8 element_count;
	u8 element_width_bits;
	u8 chain_index;
	u8 invocation_id;
	u8 sample_ordinal;
	u16 value;
};

/* Qword locals are modeled as typed output-object presence plus bounded row
 * storage. No synthetic integer is ever reinterpreted as a C pointer. */
struct ob_phy_farrow_fixture {
	bool explicitly_synthetic;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u16 chanspec;
	u8 pi_16e;
	u8 pi_168;
	u8 pi_164;
	u8 nested_348;
	u8 nested_912;
	u8 shared_a5;

	bool qword_40_nonnull;
	bool qword_48_nonnull;
	bool qword_50_nonnull;
	bool qword_58_nonnull;
	struct ob_phy_farrow_channel_row qword_40_row;
	struct ob_phy_farrow_static_row rx_farrow_row;
	struct ob_phy_farrow_static_row tx_farrow_row;
	struct ob_phy_farrow_table_sample table_samples[OB_PHY_FARROW_TABLE_SAMPLES];
};

struct ob_phy_farrow_crs_ops {
	int (*read16)(void *ctx, u16 event, u32 source_site, u16 reg,
		      u16 *value);
	int (*write16)(void *ctx, u16 event, u32 source_site, u16 reg,
		       u16 value);
	int (*table_read16)(void *ctx, u16 event, u32 source_site,
			    u8 table_id, u16 offset, u8 count,
			    u8 width_bits, u8 chain, u8 invocation,
			    u8 sample_ordinal, u16 supplied_value,
			    u16 *observed_value);
	int (*table_write16)(void *ctx, u16 event, u32 source_site,
			     u8 table_id, u16 offset, u8 count,
			     u8 width_bits, u8 chain, u16 value);
	/* API-level calls retain their accepted bounded contract. This test-only
	 * interface does not implement or invoke the live MAC/PHY access backend. */
	int (*api_call)(void *ctx, u16 event, u32 source_site,
			enum ob_phy_farrow_api_call operation,
			u32 max_declared_wait_us);
};

enum ob_phy_farrow_crs_result_kind {
	OB_PHY_FARROW_CRS_INVALID = 0,
	OB_PHY_FARROW_CRS_SKIPPED_BY_CALLER,
	OB_PHY_FARROW_CRS_HOST_SIMULATION_COMPLETE,
	OB_PHY_FARROW_CRS_FAILED_BACKEND,
};

struct ob_phy_farrow_crs_result {
	enum ob_phy_farrow_crs_result_kind kind;
	u16 logical_events_completed;
	u16 callback_records;
	u32 terminal_return_site;
	bool historically_exact;
	bool hardware_ready;
	bool phy_ready;
	bool channel_ready;
};

/* Executes only the explicitly supplied controlled Farrow/CRS scenario.
 * There is intentionally no normal-probe caller or hardware backend. */
int ob_phy_acphy_farrow_crs_fixture_test_only(
	const struct ob_phy_farrow_crs_ops *ops, void *ctx,
	const struct ob_phy_farrow_fixture *fixture,
	struct ob_phy_farrow_crs_result *result);

#endif /* _OB_PHY_FARROW_CRS_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_9AF05_LOOP_H_
#define _OB_PHY_9AF05_LOOP_H_

#include "ob_phy.h"

#define OB_9AF05_SAMPLE_BYTES 10u
#define OB_9AF05_SAMPLE_COUNT 6u

enum ob_9af05_loop_status {
	OB_9AF05_LOOP_INVALID = 0,
	OB_9AF05_LOOP_SYNTHETIC_COMPLETE,
	OB_9AF05_LOOP_FAILED,
};

struct ob_9af05_table_sample {
	bool present;
	u32 source_site;
	u8 table_id;
	u8 offset;
	u8 count;
	u8 width_bits;
	u8 length;
	u8 bytes[OB_9AF05_SAMPLE_BYTES];
};

struct ob_9af05_loop_input {
	bool explicitly_synthetic;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u8 chain_count;
	bool chanspec_present;
	u16 chanspec;
	bool caller_arg_sil_present;
	u8 caller_arg_sil;
	bool caller_arg_dl_present;
	u8 caller_arg_dl;
	bool nested_912_present;
	u8 nested_912;
	bool nested_66e_present;
	u8 nested_66e;
	bool nested_3e3_present;
	u8 nested_3e3;
	bool nested_3e4_present;
	u8 nested_3e4;
	bool nested_3e5_present;
	u8 nested_3e5;
	bool nested_64a_present;
	u8 nested_64a;
	bool saved_phy_016c_present;
	u16 saved_phy_016c;
	bool saved_phy_019e_present;
	u16 saved_phy_019e;
	struct ob_9af05_table_sample samples[OB_9AF05_SAMPLE_COUNT];
};

struct ob_9af05_loop_ops {
	int (*phy_read16)(void *ctx, u32 site, u16 reg, u16 *value);
	int (*phy_write16)(void *ctx, u32 site, u16 reg, u16 value);
	int (*table_write)(void *ctx, u32 site, u8 chain, u8 table_id,
			   u8 offset, u8 count, u8 width_bits,
			   const u8 *payload, u8 length);
	int (*table_read)(void *ctx, u32 site, u8 chain, u8 table_id,
			  u8 offset, u8 count, u8 width_bits,
			  u16 destination_offset, const u8 *snapshot,
			  u8 length);
};

struct ob_9af05_loop_result {
	enum ob_9af05_loop_status status;
	u8 callback_count;
	u8 logical_event_count;
	u32 terminal_boundary;
	u32 failed_site;
	bool historical_exact;
	bool hardware_ready;
	bool phy_ready;
	bool channel_ready;
	bool nested_copy_present;
	u16 nested_copy_offset;
	u8 nested_copy_length;
	u8 nested_copy[3];
	bool table_source_copy_present;
	u16 table_source_copy_offset;
	u8 table_source_copy_length;
	u8 table_source_copy[2];
};

/* Isolated host/fake-backend model of the chain-index-1 continuation after
 * the ten-call sub_99658/sub_998cc stage. No live dispatcher calls this API. */
int ob_phy_9af05_chain1_test_only(const struct ob_9af05_loop_ops *ops,
				  void *ctx,
				  const struct ob_9af05_loop_input *input,
				  struct ob_9af05_loop_result *result);

#endif

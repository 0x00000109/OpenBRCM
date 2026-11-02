/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_9A539_H_
#define _OB_PHY_9A539_H_

#include "ob_phy.h"
#include "ob_phy_99658.h"
#include "ob_phy_post_farrow_continuation.h"

enum ob_phy_9a539_policy {
	OB_9A539_HISTORICAL_STRICT = 0,
	OB_9A539_SYNTHETIC_PARAMETERIZED,
};
enum ob_phy_9a539_status {
	OB_9A539_INVALID = 0,
	OB_9A539_SYNTHETIC_COMPLETE,
	OB_9A539_UNSUPPORTED_ARGUMENT,
	OB_9A539_UNSUPPORTED_HISTORICAL_OPERATION,
	OB_9A539_BLOCKED_HISTORICAL_INPUT,
	OB_9A539_FAILED_BACKEND_OPERATION,
};
enum ob_phy_9a539_low_band_row_source {
	OB_9A539_LOW_BAND_ROWS_UNSPECIFIED = 0,
	OB_9A539_LOW_BAND_ROWS_EXPLICIT_SYNTHETIC_FIXTURE,
};
struct ob_phy_9a539_input {
	bool explicitly_synthetic;
	enum ob_phy_9a539_policy policy;
	u16 chip_id;
	/* Synthetic target-profile guard only. The historical runtime PI+0x164
	 * value is separately represented by phy_mode below and remains unknown. */
	u8 chip_revision, phy_revision;
	u16 radio_id;
	u8 radio_revision, radio_class, chain_count;
	bool argument_present;
	u8 argument;
	bool chanspec_present;
	u16 chanspec;
	/* Typed model of the optional caller-owned record; no fake pointer. */
	bool nested_8a8_present;
	u8 nested_8a8_byte18, nested_8a8_byte47;
	/* The caller's first sub_909fd run derives the actual argument byte. */
	bool entry_software_state_present;
	bool entry_software_bytes_present;
	bool entry_software_flags_present;
	struct ob_phy_post_farrow_software_input entry_software_state;
	/* Explicit state at the second, in-helper sub_909fd invocation. */
	bool inner_software_state_present;
	bool inner_software_bytes_present;
	bool inner_software_flags_present;
	struct ob_phy_post_farrow_software_input inner_software_state;
	bool nested_8a8_state_present;
	/* The supported nonzero scenario currently avoids relocated table data by
	 * selecting the high chanspec band, equal configuration fields and no
	 * active cores. These are typed software-state snapshots, not pointers. */
	bool active_core_mask_present;
	u8 active_core_mask;
	bool phy_mode_present;
	u32 phy_mode; /* Explicit synthetic PI+0x164 value for sub_8f41b path. */
	bool nested_42_present;
	u8 nested_42;
	bool nested_compare_state_present;
	u8 nested_65c, nested_658, nested_659, nested_65a, nested_65b;
	/* Explicit synthetic source bytes for the selected sub_99658(PI,1)
	 * TABLE builder. Literal historical rows are not embedded here. */
	bool arg1_table_source_present;
	u8 arg1_table_source[6];
	bool arg2_table_source_present;
	u8 arg2_table_source[7];
	bool nested_340_present;
	bool nested_3e0_present;
	u8 nested_3e0;
	bool arg2_998cc_source_present;
	u8 arg2_998cc_source[7];
	/* Dynamic TABLE element counts read from nested+0x64b/0x64c. The
	 * currently admitted payload providers support only their full 6/7-byte
	 * initialized prefixes; other counts stop before callbacks. */
	bool nested_table_counts_present;
	u8 nested_64b_table_count, nested_64c_table_count;
	/* The low-band path copies three relocation-backed static rows to local
	 * stack buffers before indexing them at 0x9a687/0x9a824/0x9a832. Keep
	 * those bytes as an explicit, provenance-labeled host input; proprietary
	 * row literals are not embedded in this runner. The u16 source at 0x9a832
	 * is supplied as its original 26 memory bytes, in target little-endian
	 * order, rather than as host-endian words. */
	bool low_band_rows_present;
	enum ob_phy_9a539_low_band_row_source low_band_row_source;
	u8 low_band_row_2cc700[13];
	u8 low_band_row_2cc710[26];
	u8 low_band_row_2cc730[13];
	/* Explicit entry snapshot rooted at nested+0x450. Selected 99658 children
	 * mutate the result copy in historical order before later consumers. */
	bool per_core_matrix_present;
	u8 per_core_matrix[0x122];
	bool per_core_limits_present;
	u8 per_core_limits[6];
	/* Explicit state consumed only when a selected TABLE comparison causes
	 * the caller to invoke sub_9a121. nested+66c/66e come from inner state. */
	bool a121_state_present;
	u8 nested_910, nested_911, nested_340;
};
struct ob_phy_9a539_ops {
	int (*mac_suspend_and_wait)(void *ctx, u32 site);
	int (*mac_enable)(void *ctx, u32 site);
	int (*aci_update)(void *ctx, u32 site, u8 mode);
	int (*phy_read16)(void *ctx, u32 site, u16 reg, u16 *value);
	int (*phy_write16)(void *ctx, u32 site, u16 reg, u16 value);
	int (*table_write)(void *ctx, u32 site, u8 argument, u8 chain,
			   u8 table_id, u8 offset, u8 count, u8 width_bits,
			   const u8 *payload, u8 payload_length);
	int (*radio_read16)(void *ctx, u32 site, u16 reg, u16 *value);
	int (*radio_write16)(void *ctx, u32 site, u16 reg, u16 value);
};
struct ob_phy_9a539_result {
	enum ob_phy_9a539_status status;
	u8 callback_count, logical_event_count;
	u32 terminal_boundary, first_unsupported_site;
	u32 first_unimplemented_operation, failed_site;
	bool hardware_ready, phy_ready, channel_ready;
	bool mac_suspended;
	bool entry_nested_670_present, inner_nested_670_present;
	u8 entry_nested_670, inner_nested_670;
	u8 nested_656_after, nested_657_after;
	u8 sub_909fd_invocations;
	bool sub_998cc_arg1_called;
	bool sub_998cc_arg2_called;
	bool sub_99658_arg1_called;
	bool sub_99658_arg2_called;
	bool sub_9a121_called;
	u8 nested_658_after;
	u8 nested_659_after;
	u8 nested_65a_after;
	u8 nested_65b_after;
	u8 per_core_return[2];
	u8 per_core_generated[2][6];
	u8 per_core_selector0_argument[2];
	u8 per_core_called_mask;
	bool low_band_row_selected;
	u8 low_band_row_index;
	u8 low_band_row_count;
	u8 low_band_phy_0299_value;
	u16 low_band_phy_03c1_value;
	bool coefficient_matrix_after_present;
	u8 coefficient_matrix_after[0x122];
	u16 saved_phy_019e;
	u8 aci_mode;
};

/* Test-only callback runner; it has no normal-probe caller. */
int ob_phy_9a539_test_only(const struct ob_phy_9a539_ops *ops, void *ctx,
			   const struct ob_phy_9a539_input *input,
			   struct ob_phy_9a539_result *result);

#endif

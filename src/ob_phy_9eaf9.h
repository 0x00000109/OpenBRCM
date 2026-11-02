/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_9EAF9_H_
#define _OB_PHY_9EAF9_H_

#include "ob_phy_post_lpf.h"

#define OB_PHY_9EAF9_SYNTHETIC_SAMPLE_COUNT 4u
#define OB_PHY_9EAF9_INVOCATION_CONTROLLED_1001 1u

enum ob_phy_9eaf9_policy {
	OB_PHY_9EAF9_HISTORICAL_STRICT = 0,
	OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED = 1,
};

enum ob_phy_9eaf9_operation {
	OB_9EAF9_READ,
	OB_9EAF9_WRITE,
	OB_9EAF9_RMW,
	OB_9EAF9_TABLE_READ,
	OB_9EAF9_TABLE_WRITE,
};

enum ob_phy_9eaf9_value_kind {
	OB_9EAF9_VALUE_LITERAL,
	OB_9EAF9_VALUE_READ_SAMPLE,
	OB_9EAF9_VALUE_SAVED_FIRST_BIT1,
	OB_9EAF9_VALUE_SAVED_LPF_BIT1,
	OB_9EAF9_VALUE_SAMPLE,
	OB_9EAF9_VALUE_LPF_LOW0,
	OB_9EAF9_VALUE_LPF_HIGH0,
	OB_9EAF9_VALUE_LPF_LOW1,
	OB_9EAF9_VALUE_LPF_HIGH1,
	OB_9EAF9_VALUE_WORD2,
};

struct ob_phy_9eaf9_event {
	u16 logical_event;
	u32 source_site;
	enum ob_phy_9eaf9_operation operation;
	enum ob_phy_9eaf9_value_kind value_kind;
	u16 reg_or_offset;
	u16 mask;
	u16 value;
	u8 table_id;
	u8 element_width_bits;
	u8 batch_count;
	u8 sample_slot;
	s8 chain_index;
	u8 saved_event_sequence;
	u16 payload_word1;
};

enum ob_phy_9eaf9_result_kind {
	OB_PHY_9EAF9_RESULT_INVALID = 0,
	OB_PHY_9EAF9_SKIPPED_BY_CALLER,
	OB_PHY_9EAF9_HOST_SIMULATION_COMPLETE,
	OB_PHY_9EAF9_HOST_SUPPORTED_PREFIX,
	OB_PHY_9EAF9_BLOCKED_HISTORICAL_UNKNOWN,
	OB_PHY_9EAF9_FAILED_BACKEND_OPERATION,
};

struct ob_phy_9eaf9_runtime_sample {
	bool valid;
	u16 logical_event;
	u32 source_site;
	u8 table_id;
	u16 offset;
	u8 element_count;
	u8 element_width_bits;
	u8 chain_index;
	u8 invocation_id;
	u8 sample_ordinal;
	u16 value;
};

struct ob_phy_9eaf9_fixture {
	bool explicitly_synthetic;
	enum ob_phy_9eaf9_policy policy;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u8 chain_count;
	bool chanspec_supplied;
	u16 controlled_chanspec;
	bool caller_gate_inputs_supplied;
	u8 caller_local_7a;
	u8 nested_32c;
	bool word2_present;
	u16 synthetic_word2;
	struct ob_phy_9eaf9_runtime_sample runtime_samples[OB_PHY_9EAF9_SYNTHETIC_SAMPLE_COUNT];
};

struct ob_phy_9eaf9_ops {
	int (*read16)(void *ctx, u16 logical_event, u32 source_site,
		      u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 logical_event, u32 source_site,
		       u16 reg, u16 value);
	/* Callback-only TABLE_READ API. The supplied sample is synthetic input,
	 * explicitly passed to the fake backend and never acquired from hardware. */
	int (*table_read16)(void *ctx, u16 logical_event, u32 source_site,
			    u8 table_id, u16 offset, u8 element_count,
			    u8 element_width_bits, u8 chain_index,
			    u8 invocation_id, u8 sample_ordinal,
			    u16 supplied_synthetic_value, u16 *value);
	int (*table_write)(void *ctx, u16 logical_event, u32 source_site,
			   u8 table_id, u16 offset, u8 element_count,
			   u8 element_width_bits, u8 payload_word_width_bits,
			   const u8 *payload_word_order, u8 payload_word_order_count,
			   const u16 *payload_words, u16 payload_word_count);
};

struct ob_phy_9eaf9_result {
	enum ob_phy_9eaf9_result_kind kind;
	u16 logical_event_count;
	u16 callback_count;
	bool historical_word2_resolved;
	bool hardware_ready;
};

/* Offline, callback-only test interface; there is deliberately no driver
 * backend or normal-probe caller. Synthetic success does not resolve the
 * historical word2 and does not claim any readiness state. */
int ob_phy_acphy_9eaf9_fixture_test_only(
	const struct ob_phy_9eaf9_ops *ops, void *ctx,
	const struct ob_phy_9eaf9_fixture *fixture,
	struct ob_phy_9eaf9_result *result);

#endif /* _OB_PHY_9EAF9_H_ */

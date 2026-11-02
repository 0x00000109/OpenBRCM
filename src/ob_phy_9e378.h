/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_9E378_H_
#define _OB_PHY_9E378_H_

#include "ob_phy_post_lpf.h"

enum ob_phy_9e378_domain {
	OB_PHY_9E378_PHY,
	OB_PHY_9E378_RADIO,
	OB_PHY_9E378_TABLE,
};

enum ob_phy_9e378_operation {
	OB_PHY_9E378_READ,
	OB_PHY_9E378_WRITE,
	OB_PHY_9E378_RMW,
	OB_PHY_9E378_TABLE_WRITE,
};

enum ob_phy_9e378_expression {
	OB_PHY_9E378_LITERAL,
	OB_PHY_9E378_SNAPSHOT_BIT1,
};

enum ob_phy_9e378_rmw_mode {
	OB_PHY_9E378_RMW_PHY_MASKED_MERGE,
	OB_PHY_9E378_RMW_RADIO_CLEAR_VALUE,
};

enum ob_phy_9e378_payload_id {
	OB_PHY_9E378_PAYLOAD_NONE,
	OB_PHY_9E378_PAYLOAD_IPA,
	OB_PHY_9E378_PAYLOAD_EPA,
	OB_PHY_9E378_PAYLOAD_ZERO1,
	OB_PHY_9E378_PAYLOAD_ZERO2,
};

struct ob_phy_9e378_event {
	u16 logical_sequence;
	u32 source_site;
	enum ob_phy_9e378_domain domain;
	enum ob_phy_9e378_operation operation;
	u8 access_width_bits;
	u16 reg;
	u16 mask;
	u16 value;
	enum ob_phy_9e378_expression expression;
	u8 snapshot_slot;
	enum ob_phy_9e378_rmw_mode rmw_mode;
	u8 table_id;
	u16 table_offset;
	u16 element_count;
	u8 element_width_bits;
	enum ob_phy_9e378_payload_id payload_id;
	u16 payload_word_count;
};

struct ob_phy_9e378_variant {
	const char *id;
	bool extpagain2g_eq2;
	bool pdgain_write;
	u8 txchain_low2;
	const struct ob_phy_9e378_event *events;
	u16 logical_event_count;
	u16 callback_count;
	u8 snapshot_count;
};

struct ob_phy_9e378_ops {
	int (*read16)(void *ctx, u8 logical_event, u32 source_site,
		      enum ob_phy_9e378_domain domain, u16 reg, u16 *value);
	int (*write16)(void *ctx, u8 logical_event, u32 source_site,
		       enum ob_phy_9e378_domain domain, u16 reg, u16 value);
	/* One callback is one historical TABLE API operation. For width 48,
	 * payload_word_count is three u16 words per element in accepted order. */
	int (*table_write)(void *ctx, u8 logical_event, u32 source_site,
			   u8 table_id, u16 offset, u16 element_count,
			   u8 element_width_bits, const u16 *payload_words,
			   u16 payload_word_count);
};

/* Every selected input is synthetic. Actual target selector values and the
 * target's initial chanspec remain unknown. */
struct ob_phy_9e378_fixture {
	bool explicitly_synthetic;
	u16 chip_id;
	u8 chip_revision;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_class;
	u8 chain_count;
	bool chanspec_supplied;
	u16 requested_chanspec;
	u8 caller_gate_7b;
	u8 caller_gate_79;
	u8 nested_32c;
	bool selectors_supplied;
	u32 extpagain2g;
	u8 nested_411;
	u8 txchain_low2;
	bool shared_80_supplied;
	u8 shared_80;
};

enum ob_phy_9e378_result_kind {
	OB_PHY_9E378_RESULT_INVALID = 0,
	OB_PHY_9E378_RESULT_SKIPPED = 1,
	OB_PHY_9E378_RESULT_SYNTHETIC_VARIANT_COMPLETE = 2,
};

struct ob_phy_9e378_result {
	enum ob_phy_9e378_result_kind kind;
	u8 variant_index;
	u16 logical_event_count;
	u16 callback_count;
	bool hardware_ready;
};

/* Callback-only execution for explicit synthetic inputs; no probe caller. */
int ob_phy_acphy_9e378_fixture_test_only(
	const struct ob_phy_9e378_ops *ops, void *ctx,
	const struct ob_phy_9e378_fixture *fixture,
	struct ob_phy_9e378_result *result);

#endif /* _OB_PHY_9E378_H_ */

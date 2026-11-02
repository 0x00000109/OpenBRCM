/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_ACPHY_CAL_CONTRACT_H_
#define _OB_ACPHY_CAL_CONTRACT_H_

#include <linux/types.h>

/*
 * Admission boundary for the future BCM4352 calibration/channel/PLL
 * transaction.  This is deliberately a pure policy object: it performs no
 * MMIO and never converts an available radio identity into a calibration or
 * channel-ready claim.
 */
#define OB_ACPHY_CAL_CONTROLLED_CHANSPEC          0x1001u
#define OB_ACPHY_CAL_CONTROLLED_CHANNEL_KEY       0x01u
#define OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW       63u
#define OB_ACPHY_CAL_CONTROLLED_CHANNEL_ROW_OFFSET 0x1c8cu
#define OB_ACPHY_CAL_RADIO_SELECTOR_CLASS         0u

/*
 * Provenance of the ACPHY temperature-delta calibration input.
 *
 * The vendor code reads the PHY variable "phycal_tempdelta" from the
 * name=value variable blob handed to wlc_phy_attach(), parses it as an
 * integer (strtoul base 0) and stores the low byte in the PHY info block.
 * The ACPHY SROM-rev-11 TX-power reader then presets the fallback byte to 40
 * and re-runs the store, which clamps the value:
 *
 *   configured = (u8)value;
 *   effective  = configured > 64 ? 40 : configured;
 *
 * The byte is a PERIODIC recalibration policy threshold, not an initial
 * calibration operand (Phase 36S):
 *
 *   - wlc_init() enters wlc_phy_cal_perical() with reason 6 (jump-table slot
 *     4 -> 0xb586a).  The pi+0xf9c test at 0xb58a2 only selects whether a
 *     temperature sample is cached; both arms rejoin at 0xb58fa and reach
 *     wlc_phy_cals_acphy() unconditionally.  Zero skips the sample and the
 *     initial calibration still runs.
 *   - the periodic wlc_phy_watchdog() entry uses reason 2, where the byte is
 *     the |T_now - T_at_last_cal| >= threshold test (0xb59f7..0xb5a02).
 *   - wlc_phy_cals_acphy() contains no +0xf9c access, and the only reader of
 *     the cached temperature (calstate+0xcc) outside perical is the attach
 *     initialisation, so the sample consumes nothing the calibration body
 *     reads.
 *
 * No OpenBRCM input supplies the vendor variable blob yet, so the resolver
 * distinguishes "unread" from "absent" rather than silently defaulting to
 * zero.  The value stays UNKNOWN for periodic policy and is never an initial
 * calibration admission input.
 */
#define OB_ACPHY_CAL_TEMPDELTA_CLAMP_LIMIT    64u
#define OB_ACPHY_CAL_TEMPDELTA_ACPHY_FALLBACK 40u

enum ob_acphy_cal_tempdelta_source {
	OB_ACPHY_CAL_TEMPDELTA_UNREAD = 0,
	OB_ACPHY_CAL_TEMPDELTA_DEFAULT,
	OB_ACPHY_CAL_TEMPDELTA_VAR,
};

struct ob_acphy_cal_tempdelta_input {
	/* True only when the caller could actually read the variable blob. */
	bool vars_available;
	bool var_present;
	bool text_parsed;
	u64 raw;
};

struct ob_acphy_cal_tempdelta_result {
	/* False means the value was not observable; it is not a zero value. */
	bool known;
	bool clamped;
	u8 configured;
	u8 effective;
	enum ob_acphy_cal_tempdelta_source source;
};

enum ob_acphy_cal_contract_blocker {
	OB_ACPHY_CAL_BLOCK_NONE = 0,
	OB_ACPHY_CAL_BLOCK_TARGET,
	OB_ACPHY_CAL_BLOCK_RADIO_COMPLETE,
	OB_ACPHY_CAL_BLOCK_CHANSPEC,
	OB_ACPHY_CAL_BLOCK_PLL_SELECTOR,
	OB_ACPHY_CAL_BLOCK_CHANNEL_ROW,
};

/* Only inputs that gate the INITIAL calibration transaction. */
#define OB_ACPHY_CAL_UNKNOWN_PLL_SELECTOR     (1u << 0)
#define OB_ACPHY_CAL_UNKNOWN_CHANNEL_ROW      (1u << 1)

struct ob_acphy_cal_contract_input {
	bool target_identity_valid;
	bool radio_complete;
	bool chanspec_valid;
	u16 chanspec;
	/* Periodic recalibration threshold: reported, never an admission gate. */
	bool periodic_tempdelta_known;
	/* The selector must come from a completed, valid attach identity. */
	bool pll_selector_known;
	bool pll_selector_from_identity;
	u8 pll_selector;
	/* This tuple is only the admitted controlled 0x1001/rev4 fixture. */
	bool channel_row_known;
	u8 channel_key;
	u8 channel_row_index;
	u16 channel_row_offset;
};

struct ob_acphy_cal_contract_result {
	enum ob_acphy_cal_contract_blocker first_blocker;
	u8 unknown_mask;
	/* Carried for the future periodic policy; it never affects first_blocker. */
	bool periodic_tempdelta_known;
	bool calibration_started;
	bool pll_started;
	bool channel_started;
};

/*
 * Return 0 only when every execution-affecting initial-calibration input is
 * admitted.  The periodic phycal_tempdelta threshold is deliberately not part
 * of that set.  A zero result is an admission decision, not permission to
 * issue an operation: the exact operation ledger and its bounded polls remain
 * a later implementation.
 * Return -EOPNOTSUPP (-95) for missing selected-path inputs and -EINVAL
 * (-22) for an invalid predecessor or controlled fixture.
 */
int ob_acphy_cal_contract_admit(const struct ob_acphy_cal_contract_input *in,
				 struct ob_acphy_cal_contract_result *result);

/*
 * Resolve the proven ACPHY temperature-delta rules from an already-read
 * variable blob.  Pure: no MMIO and no kernel state.  Returns 0 with
 * result->known false when the blob was not readable, and -EINVAL (-22) for
 * a NULL argument.  An absent key yields the vendor getintvar() default of
 * zero; a present but unparsable key yields strtoul()'s zero.
 */
int ob_acphy_cal_tempdelta_acphy_resolve(
	const struct ob_acphy_cal_tempdelta_input *in,
	struct ob_acphy_cal_tempdelta_result *out);

#endif /* _OB_ACPHY_CAL_CONTRACT_H_ */

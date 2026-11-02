/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_FABRIC_BASELINE_H_
#define _OB_FABRIC_BASELINE_H_

/*
 * Classification of the fixed ChipCommon/PMU snapshot already collected by
 * ob_si_dump().  READABLE is deliberately narrow: it means that none of the
 * trusted fixed-window observations returned all ones.  It is not a claim
 * that the whole PCIe, BCMA, D11, clock, or radio fabric is healthy.
 */
enum ob_fixed_window_baseline {
	OB_FIXED_WINDOW_READABLE = 0,
	OB_FIXED_WINDOW_ALL_ONES,
	OB_FIXED_WINDOW_INDETERMINATE,
};

struct ob_fixed_window_snapshot {
	u32 cc_cap;
	u32 cc_otps;
	u32 cc_otpl;
	u32 cc_srom_ctl;
	u32 cc_clkctlst;
	u32 pmu_cap;
	u32 pmu_ctl;
};

static inline enum ob_fixed_window_baseline
ob_fixed_window_classify(const struct ob_fixed_window_snapshot *snapshot)
{
	const u32 *values;
	unsigned int all_ones = 0;
	unsigned int i;

	if (!snapshot)
		return OB_FIXED_WINDOW_INDETERMINATE;

	values = &snapshot->cc_cap;
	for (i = 0; i < 7; i++)
		all_ones += values[i] == 0xffffffffu;

	if (all_ones == 7)
		return OB_FIXED_WINDOW_ALL_ONES;
	if (all_ones != 0)
		return OB_FIXED_WINDOW_INDETERMINATE;
	return OB_FIXED_WINDOW_READABLE;
}

#endif /* _OB_FABRIC_BASELINE_H_ */

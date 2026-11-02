/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_PHY_A7089_CAL_H_
#define _OB_PHY_A7089_CAL_H_

#include "ob_phy_post_lpf.h"

#define OB_A7089_CAL_REG_COUNT 50u
#define OB_A7089_CAL_ROW_COUNT 12u

struct ob_a7089_cal_ops {
	int (*write16)(void *ctx, u8 logical_event, u16 reg, u16 value);
	int (*read16)(void *ctx, u8 logical_event, u16 reg, u16 *value);
	int (*write16_phy)(void *ctx, u8 logical_event, u16 reg, u16 value);
	int (*read16_phy)(void *ctx, u8 logical_event, u16 reg, u16 *value);
	void (*delay_us)(void *ctx, u16 usec);
	void (*write_dp)(void *ctx, u16 value);
	int (*read_dp)(void *ctx, u16 *value);
	void (*write16_d11)(void *ctx, u16 off, u16 value);
	void (*write32_d11)(void *ctx, u16 off, u32 value);
	u16 (*read16_d11)(void *ctx, u16 off);
	u32 (*read32_d11)(void *ctx, u16 off);
};

struct ob_a7089_cal_result {
	u16 callback_count;
	u16 logical_event_count;
	u16 row_count;
	bool hardware_ready;
};

/*
 * 2069-rev4 radio-cal block inside sub_a7089: a fixed chip-conditional radio
 * register list (OB_A7089_CAL_REG_COUNT for 0x4352) written once per
 * chanspec-selected row (sub_8e9b1 fills the row). Rows recovered from the
 * oracle (P10c, chanspec 0x1001): 12 distinct rows, reg list identical
 * across rows. HARDWARE PROVEN for row0 (50 writes); loop-over-rows added.
 */
int ob_acphy_a7089_postcal_prefix_run(const struct ob_a7089_cal_ops *ops,
				      void *ctx,
				      struct ob_a7089_cal_result *result);

int ob_acphy_a7089_golden_unit_run(const struct ob_a7089_cal_ops *ops,
				   void *ctx,
				   struct ob_a7089_cal_result *result,
				   u32 cap);

int ob_acphy_a7089_radio_cal_prefix_run(const struct ob_a7089_cal_ops *ops,
					void *ctx,
					struct ob_a7089_cal_result *result);

#endif /* _OB_PHY_A7089_CAL_H_ */

// SPDX-License-Identifier: GPL-2.0-only

#include <stdio.h>

#include "ob_phy_a7089_cal.h"

static u16 last_reg, last_val;
static int calls;

static int mock_write16(void *ctx, u8 ev, u16 reg, u16 value)
{
	int *fail = ctx;

	(void)ev;
	calls++;
	if (*fail && calls == *fail)
		return -5;
	last_reg = reg;
	last_val = value;
	return 0;
}

int main(void)
{
	const struct ob_a7089_cal_ops ops = { .write16 = mock_write16 };
	struct ob_a7089_cal_result res;
	int fail = 0;
	int ret;

	calls = 0;
	ret = ob_acphy_a7089_radio_cal_prefix_run(&ops, &fail, &res);
	if (ret != 0 || res.row_count != OB_A7089_CAL_ROW_COUNT ||
	    res.callback_count != OB_A7089_CAL_REG_COUNT * OB_A7089_CAL_ROW_COUNT ||
	    calls != res.callback_count || res.hardware_ready) {
		printf("FAIL: ret=%d rows=%u writes=%u calls=%d\n", ret,
		       res.row_count, res.callback_count, calls);
		return 1;
	}
	/* last write of the block = last reg of the last row */
	if (last_reg != 0x723 || last_val != 0x3e0) {
		printf("FAIL: last %#x=%#x\n", last_reg, last_val);
		return 1;
	}

	/* fail-stop */
	calls = 0;
	fail = 7;
	ret = ob_acphy_a7089_radio_cal_prefix_run(&ops, &fail, &res);
	if (ret == 0 || calls != 7) {
		printf("FAIL: fail-stop ret=%d calls=%d\n", ret, calls);
		return 1;
	}

	printf("ob_phy_a7089_cal: all checks passed (%u rows x %u regs = %u writes)\n",
	       OB_A7089_CAL_ROW_COUNT, OB_A7089_CAL_REG_COUNT,
	       OB_A7089_CAL_REG_COUNT * OB_A7089_CAL_ROW_COUNT);
	return 0;
}

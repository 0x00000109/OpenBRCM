/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — isolated rev42 common-initvals test (M3.4D2B).
 *
 * The D2B mode reuses the hardware-proven D2A core (ob_ucode_run_d2a) and then
 * applies exactly the 610 data records of `bcm4352-d11ac1initvals42.bin` in
 * strict original order, before reading a small set of provenance-backed
 * postconditions. It stops before bsinitvals / sub_6656c / PHY / radio /
 * channel / DMA / IRQ / mac80211.
 *
 * This header carries the exact expected table shape and the pure (host-testable)
 * plan and postcondition predicates. It deliberately contains no kernel API in
 * the pure section so the counts and validation logic can be unit-tested on the
 * host and in KUnit.
 *
 * Provenance: docs/m34d2b_common_initvals.md (vendor applier sub_60f67 @0x60fce;
 * 8-byte little-endian records {u16 offset, u16 width, u32 value}, terminator
 * offset == 0xffff; width 2 -> 16-bit D11 write, width 4 -> 32-bit D11 write).
 */
#ifndef _OB_INITVALS_H_
#define _OB_INITVALS_H_

#include <linux/types.h>
#include "ob_fw.h"

/* ---- expected common-initvals table shape (M3.4D2B analysis) ---- */
#define OB_INITVALS_RECORDS		610u
#define OB_INITVALS_W16			113u
#define OB_INITVALS_W32			497u
#define OB_INITVALS_TERMINATOR_INDEX	610u

/*
 * Proven postconditions (docs/m34d2b_common_initvals.md §F9/F10). No equality
 * check outside this set may be invented for the isolated path.
 */
#define OB_INITVALS_FIFOSIZE0_EXPECTED	0x01c4u
#define OB_INITVALS_FIFOSIZE1_EXPECTED	0x0000u
#define OB_INITVALS_FIFOSIZE2_EXPECTED	0x0000u
#define OB_INITVALS_FIFOSIZE3_EXPECTED	0x079eu
#define OB_INITVALS_MACINTMASK_EXPECTED	0x00000000u
#define OB_INITVALS_MACCONTROL_EXPECTED	0x04020402u
#define OB_INITVALS_SHM14_EXPECTED	0x000000b4u

/* SHM byte offsets for the proven 32-bit SHM 0x0014 postcondition read. */
#define OB_INITVALS_SHM14_LO		0x0014u
#define OB_INITVALS_SHM14_HI		0x0016u

/* ---- pure plan accounting (host-testable) ---- */
struct ob_initvals_plan {
	u32 records;	/* data records applied (terminator excluded) */
	u32 w16;	/* width-2 records applied */
	u32 w32;	/* width-4 records applied */
};

/*
 * Walk @data exactly as the applier does: parse the terminator-delimited table,
 * then visit every data record in order and count writes by width. The
 * terminator is never counted (M3.4D2B requirement 14). Returns 0 on success,
 * -EINVAL on malformed input and -ENODATA when no terminator is present.
 */
static inline int ob_initvals_plan_from_table(const u8 *data, size_t size,
					      struct ob_initvals_plan *plan)
{
	struct ob_fw_iv_stats st;
	u32 i;
	int ret;

	if (!plan)
		return -EINVAL;

	plan->records = 0;
	plan->w16 = 0;
	plan->w32 = 0;

	ret = ob_fw_iv_parse(data, size, &st);
	if (ret)
		return ret;

	for (i = 0; i < st.terminator_index; i++) {
		struct ob_fw_iv iv;

		if (ob_fw_iv_at(data, size, i, &iv))
			return -EINVAL;
		if (iv.width == OB_FW_IV_WIDTH16)
			plan->w16++;
		else
			plan->w32++;
		plan->records++;
	}
	return 0;
}

/* The table shape must match the M3.4D2B analysis exactly. */
static inline bool ob_initvals_plan_ok(const struct ob_initvals_plan *plan)
{
	return plan && plan->records == OB_INITVALS_RECORDS &&
	       plan->w16 == OB_INITVALS_W16 && plan->w32 == OB_INITVALS_W32;
}

/* Runtime write-count invariant: total == 610, 16-bit == 113, 32-bit == 497. */
static inline bool ob_initvals_counts_ok(u32 total, u32 w16, u32 w32)
{
	return total == OB_INITVALS_RECORDS && w16 == OB_INITVALS_W16 &&
	       w32 == OB_INITVALS_W32;
}

/* ---- pure postcondition predicate (host-testable) ---- */
struct ob_initvals_post {
	u32 fifosize0;
	u32 fifosize1;
	u32 fifosize2;
	u32 fifosize3;
	u32 macintmask;
	u32 maccontrol;
	u32 shm14;	/* assembled 32-bit value at SHM byte offset 0x0014 */
};

static inline bool ob_initvals_post_ok(const struct ob_initvals_post *post)
{
	return post &&
	       post->fifosize0 == OB_INITVALS_FIFOSIZE0_EXPECTED &&
	       post->fifosize1 == OB_INITVALS_FIFOSIZE1_EXPECTED &&
	       post->fifosize2 == OB_INITVALS_FIFOSIZE2_EXPECTED &&
	       post->fifosize3 == OB_INITVALS_FIFOSIZE3_EXPECTED &&
	       post->macintmask == OB_INITVALS_MACINTMASK_EXPECTED &&
	       post->maccontrol == OB_INITVALS_MACCONTROL_EXPECTED &&
	       post->shm14 == OB_INITVALS_SHM14_EXPECTED;
}

#ifdef __KERNEL__
struct ob_hw;

/*
 * Run the isolated common-initvals test. Requires explicit human approval to
 * execute on hardware; this symbol performs real MMIO (the proven D2A core plus
 * the 610 common-initvals writes). It never proceeds into normal bring-up.
 */
int ob_initvals_test(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_INITVALS_H_ */

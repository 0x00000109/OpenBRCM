/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — BCM2069 radio identity observation (D4-BLOCKER-PLL-BRANCH-HW-PROBE).
 *
 * Clean-room, specification-driven. The radio identity is read from the D11
 * radio indirect window that the vendor `read_radio_reg` and `wlc_phy_attach`
 * use:
 *
 *     write16(D11 + 0x3d8, 0);  reg0 = read16(D11 + 0x3da);   // radio reg 0
 *     write16(D11 + 0x3d8, 1);  reg1 = read16(D11 + 0x3da);   // radio reg 1
 *
 * and decoded exactly as the AC path of `wlc_phy_attach @0xbeff8`:
 *
 *     radio_id       = reg1                       (radio chip id; BCM2069)
 *     radio_rev      = reg0 & 0xff                (full 2069 revision byte)
 *     rev_low        = reg0 & 0x0f
 *     revision_class = (reg0 >> 4) & 0xff         (pi+0x16e == radio_rev >> 4)
 *
 * The revision class selects the first-radio-ON PLL/synth branch
 * (`wlc_phy_switch_radio_acphy @0xaa782`): 1 -> sequence A, 2 -> sequence B,
 * anything else -> both skipped. This is the ONLY hardware fact missing to
 * close blocker `d4.pll_synth.rev42.branch` (see
 * docs/m34d4/pll_selector_provenance.json).
 *
 * The sequence is pure and host-testable. The backend supplies the register
 * access; in the kernel it is routed through the central dev_lost guard
 * (ob_d11_*), and in host tests through a scripted fake. The sequence never
 * issues a hardware access after the monotonic `lost` latch is set, and it is
 * bounded (exactly two selector writes + two data reads, no polling).
 */
#ifndef _OB_RADIO_H_
#define _OB_RADIO_H_

#include <linux/types.h>
#include "ob_guard.h"

/* D11 core offsets of the proven radio indirect access window. */
#define OB_RADIO_REG_ADDR_LATCH	0x03d8u	/* selects the radio register index */
#define OB_RADIO_REG_DATA	0x03dau	/* 16-bit data port of the latched reg */
/*
 * Trusted accessibility sentinel: a direct 32-bit D11 read whose all-ones value
 * unambiguously means an inaccessible window (MACCONTROL). It is the exact
 * trusted class used by the central guard; SHM/OBJDATA payloads are never used.
 */
#define OB_RADIO_REG_SENTINEL	0x0120u	/* MACCONTROL (trusted all-ones class) */

/* Accepted AC radio ids (wlc_phy_attach AC acceptance table). */
#define OB_RADIO_BCM2069_ID	0x2069u
#define OB_RADIO_AC_ALT_ID	0x030bu

/* pi+0x16e revision classes that select a PLL sequence. */
#define OB_RADIO_REV_CLASS_A	1u	/* 2069 rev 16..31 -> sequence A */
#define OB_RADIO_REV_CLASS_B	2u	/* 2069 rev 32..47 -> sequence B */

enum ob_radio_branch {
	OB_RADIO_BRANCH_SKIP = 0,
	OB_RADIO_BRANCH_A,
	OB_RADIO_BRANCH_B,
	OB_RADIO_BRANCH_UNKNOWN,
};

struct ob_radio_observation {
	u16 reg0_raw;
	u16 reg1_raw;
	u16 radio_id;		/* reg1 */
	u8  radio_rev;		/* reg0 & 0xff */
	u8  rev_low;		/* reg0 & 0x0f */
	u8  revision_class;	/* (reg0 >> 4) & 0xff == pi+0x16e */
	bool id_is_2069;
	bool id_accepted;	/* reg1 in {0x2069, 0x030B} */
	bool all_ones_pair;	/* both registers read 0xffff */
	enum ob_radio_branch branch;
};

/* ---- pure decode (host-testable) ---------------------------------------- */

static inline struct ob_radio_observation ob_radio_decode(u16 reg0, u16 reg1)
{
	struct ob_radio_observation o;

	o.reg0_raw = reg0;
	o.reg1_raw = reg1;
	o.radio_id = reg1;
	o.radio_rev = (u8)(reg0 & 0xffu);
	o.rev_low = (u8)(reg0 & 0x0fu);
	o.revision_class = (u8)((reg0 >> 4) & 0xffu);
	o.id_is_2069 = (reg1 == OB_RADIO_BCM2069_ID);
	o.id_accepted = (reg1 == OB_RADIO_BCM2069_ID) ||
			(reg1 == OB_RADIO_AC_ALT_ID);
	o.all_ones_pair = (reg0 == 0xffffu) && (reg1 == 0xffffu);

	if (!o.id_is_2069) {
		/* class semantics are 2069-specific; do not guess. */
		o.branch = OB_RADIO_BRANCH_UNKNOWN;
	} else if (o.revision_class == OB_RADIO_REV_CLASS_A) {
		o.branch = OB_RADIO_BRANCH_A;
	} else if (o.revision_class == OB_RADIO_REV_CLASS_B) {
		o.branch = OB_RADIO_BRANCH_B;
	} else {
		o.branch = OB_RADIO_BRANCH_SKIP;
	}
	return o;
}

static inline const char *ob_radio_branch_name(enum ob_radio_branch b)
{
	switch (b) {
	case OB_RADIO_BRANCH_A:
		return "A";
	case OB_RADIO_BRANCH_B:
		return "B";
	case OB_RADIO_BRANCH_SKIP:
		return "SKIP";
	default:
		return "UNKNOWN";
	}
}

/* ---- pure sequence (host-testable) -------------------------------------- */

struct ob_radio_io_ops {
	u32  (*read32_trusted)(void *ctx, u16 off);
	u16  (*read16)(void *ctx, u16 off);
	void (*write16)(void *ctx, u16 off, u16 val);
};

struct ob_radio_seq_result {
	bool ran;		/* the two-register observation was executed */
	bool stopped_dev_lost;	/* latch seen before/at/after the access */
	bool dev_lost_after;	/* latch only after a completed observation */
	bool valid;		/* observed values are a decodable identity */
	u32  pre_access;	/* trusted pre-access sentinel value */
	u32  post_access;	/* trusted post-access sentinel value */
	u16  reg0_raw;
	u16  reg1_raw;
	u32  radio_writes;	/* selector writes actually issued (0 or 2) */
	u32  radio_reads;	/* data reads actually issued (0 or 2) */
	struct ob_radio_observation obs;
	int  rc;
};

/*
 * Single-shot radio identity observation. Order:
 *   1. refuse if already lost (no MMIO)
 *   2. PRE_ACCESS trusted sentinel (all-ones => latch + stop, no writes)
 *   3. reg0: selector write 0 + data read
 *   4. reg1: selector write 1 + data read
 *   5. decode
 *   6. POST_ACCESS trusted sentinel (all-ones => latch + stop)
 * On the kernel path `*lost` aliases `hw->dev_lost`, so the central guard also
 * suppresses any access if the latch flips independently.
 */
static inline int ob_radio_sequence(const struct ob_radio_io_ops *ops, void *ctx,
				    bool *lost,
				    struct ob_radio_seq_result *res)
{
	u32 pre, post;

	res->ran = false;
	res->stopped_dev_lost = false;
	res->dev_lost_after = false;
	res->valid = false;
	res->pre_access = 0;
	res->post_access = 0;
	res->reg0_raw = 0;
	res->reg1_raw = 0;
	res->radio_writes = 0;
	res->radio_reads = 0;
	res->rc = 0;

	if (*lost) {
		res->stopped_dev_lost = true;
		res->rc = -5;	/* -EIO without the kernel header */
		return res->rc;
	}

	pre = ops->read32_trusted(ctx, OB_RADIO_REG_SENTINEL);
	res->pre_access = pre;
	if (*lost || pre == OB_GUARD_ALL_ONES32) {
		*lost = true;
		res->stopped_dev_lost = true;
		res->rc = -5;
		return res->rc;
	}

	if (*lost)
		goto lost_mid;

	ops->write16(ctx, OB_RADIO_REG_ADDR_LATCH, 0);
	res->radio_writes++;
	res->reg0_raw = ops->read16(ctx, OB_RADIO_REG_DATA);
	res->radio_reads++;

	if (*lost)
		goto lost_mid;

	ops->write16(ctx, OB_RADIO_REG_ADDR_LATCH, 1);
	res->radio_writes++;
	res->reg1_raw = ops->read16(ctx, OB_RADIO_REG_DATA);
	res->radio_reads++;

	res->ran = true;
	res->obs = ob_radio_decode(res->reg0_raw, res->reg1_raw);
	res->valid = !res->obs.all_ones_pair;

	post = ops->read32_trusted(ctx, OB_RADIO_REG_SENTINEL);
	res->post_access = post;
	if (*lost || post == OB_GUARD_ALL_ONES32) {
		*lost = true;
		res->dev_lost_after = true;
		res->stopped_dev_lost = true;
		res->valid = false;
		res->rc = -5;
		return res->rc;
	}
	return 0;

lost_mid:
	*lost = true;
	res->stopped_dev_lost = true;
	res->valid = false;
	res->rc = -5;
	return res->rc;
}

/* ---- kernel entry + remove hook (implemented in ob_radio.c) ------------- */
#ifdef __KERNEL__
struct ob_hw;

int  ob_radio_probe_test(struct ob_hw *hw);
void ob_radio_remove(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_RADIO_H_ */

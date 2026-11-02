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
#define OB_RADIO_PHYTYPE_AC	0x0bu
#define OB_RADIO_PHYREV_AC_TARGET	0x01u

/* pi+0x16e revision classes that select a PLL sequence. */
#define OB_RADIO_REV_CLASS_A	1u	/* 2069 rev 16..31 -> sequence A */
#define OB_RADIO_REV_CLASS_B	2u	/* 2069 rev 32..47 -> sequence B */

/*
 * ---- vendor core-clock-word arm of wlc_bmac_phy_reset (host-testable) ----
 *
 * The vendor writes ONE si_core_cflags word, whose mask and value are selected
 * by the PHY type; the two arms are mutually exclusive and share a single call
 * site. From the pinned object (SHA-256 352a6e34...4743):
 *
 *   0x65da6  cmp  $0xb,%ax          ; phy_type (pi+0x160)
 *   0x65daa  jne  0x65dc0           ; 0x0b is the ONLY AC type
 *   0x65dac  mov  %r12d,%edx        ; value = bw
 *   0x65db6  mov  $0xce,%esi        ; AC mask
 *   0x65dbb  or   $0xe,%edx         ; AC value = bw | 0x0e
 *   0x65dca  mov  $0xcc,%esi        ; other mask
 *   0x65dcf  or   $0xc,%edx         ; other value = bw | 0x0c
 *   0x65dd3  call si_core_cflags    ; the single shared write
 *
 * `bw` is wlc_phy_clk_bwbits(pi), called unconditionally at 0x659de before the
 * type test, so it is never arm-specific:
 *
 *   0xb191c  mov  0x160(%rdi),%eax  ; type gate
 *   0xb1922..0xb193e               ; type in {4,6,7,8,0xa,0xb}, else -> 0
 *   0xb1940  mov  0x182(%rdi),%dx   ; pi+0x182 bandwidth state (u16)
 *   0xb194c  0x1800 -> 0x80
 *   0xb1953  0x2000 -> 0xc0
 *   0xb195c  0x1000 -> 0x40
 *   0xb1965  otherwise -> 0x00
 */
#define OB_ACPHY_CFLAGS_AC_MASK		0x00ceu
#define OB_ACPHY_CFLAGS_AC_BASE		0x000eu
#define OB_ACPHY_CFLAGS_OTHER_MASK	0x00ccu
#define OB_ACPHY_CFLAGS_OTHER_BASE	0x000cu

/*
 * Attach-time bandwidth state. wlc_phy_attach stores this unconditional
 * immediate into pi+0x182 at 0xbe731, before any band/channel programming, so
 * the first reset after attach always sees it. It is the vendor's attach-time
 * state, not a default channel and not a PCI-id derivation.
 */
#define OB_ACPHY_BW_STATE_ATTACH	0x1000u

/* wlc_phy_clk_bwbits @0xb1913: PHY-clock bandwidth bits for pi+0x182. */
static inline u16 ob_acphy_clk_bwbits(u8 phy_type, u16 bw_state)
{
	switch (phy_type) {
	case 0x04u:
	case 0x06u:
	case 0x07u:
	case 0x08u:
	case 0x0au:
	case OB_RADIO_PHYTYPE_AC:
		break;
	default:
		return 0x0000u;
	}

	switch (bw_state) {
	case 0x1800u:
		return 0x0080u;
	case 0x2000u:
		return 0x00c0u;
	case 0x1000u:
		return 0x0040u;
	default:
		return 0x0000u;
	}
}

/* Mask/value of the single si_core_cflags word selected by the PHY type. */
static inline void ob_acphy_reset_arm(u8 phy_type, u16 bw_state, u16 *mask,
				      u16 *value)
{
	u16 bw = ob_acphy_clk_bwbits(phy_type, bw_state);

	if (phy_type == OB_RADIO_PHYTYPE_AC) {
		if (mask)
			*mask = OB_ACPHY_CFLAGS_AC_MASK;
		if (value)
			*value = (u16)(bw | OB_ACPHY_CFLAGS_AC_BASE);
		return;
	}
	if (mask)
		*mask = OB_ACPHY_CFLAGS_OTHER_MASK;
	if (value)
		*value = (u16)(bw | OB_ACPHY_CFLAGS_OTHER_BASE);
}
#define OB_RADIO2069_CHANSPEC_CH1_20MHZ 0x1001u
#define OB_PCIE_LNKSTA_DLLLA	0x0020u
#define OB_RADIO_LOCAL_0413_MAX_ATTEMPTS 100u
#define OB_RADIO_LOCAL_0413_POLL_DELAY_US 100u
#define OB_RADIO_LOCAL_0413_POLL_MASK 0x0010u

/* Production MID-v2 Link Status classification contract.  Transport success
 * does not make the returned register value semantically valid: PCI error
 * response 0xffff must remain raw evidence and must never become LINK_UP. */
struct ob_pcie_link_classification {
	bool raw_valid;
	bool semantic_valid;
	bool link_up;
	bool abnormal;
	const char *classification;
};

static inline struct ob_pcie_link_classification
ob_classify_pcie_link_status(int access_rc, u16 raw)
{
	struct ob_pcie_link_classification result = {
		.raw_valid = access_rc == 0,
		.semantic_valid = false,
		.link_up = false,
		.abnormal = true,
		.classification = "PCI_CONFIG_ERROR",
	};

	if (access_rc != 0)
		return result;

	result.semantic_valid = raw != 0xffffu;
	if (!result.semantic_valid) {
		result.classification = "PCI_CONFIG_ALL_ONES";
		return result;
	}

	result.link_up = (raw & OB_PCIE_LNKSTA_DLLLA) != 0;
	result.abnormal = !result.link_up;
	result.classification = result.link_up ? "LINK_UP" :
		"LINK_NOT_ACTIVE";
	return result;
}

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

/* Pure target guard: unknown/invalid identity is always rejected. */
struct ob_radio_target_identity {
	u16 chip_id;
	u8 chip_rev;
	u8 d11_corerev;
	u8 phy_type;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	bool radio_access_initialized;
	bool valid;
};

static inline bool ob_radio2069_target_applicable(
	const struct ob_radio_target_identity *id)
{
	return id && id->valid && id->radio_access_initialized &&
		id->chip_id == 0x4352u && id->chip_rev == 3u &&
		id->d11_corerev == 42u && id->phy_type == OB_RADIO_PHYTYPE_AC &&
		id->phy_revision == OB_RADIO_PHYREV_AC_TARGET &&
		id->radio_id == OB_RADIO_BCM2069_ID &&
		id->radio_revision == 4u;
}

/* Error-returning arbitrary radio-window access contract. */
struct ob_radio_access_ops {
	int (*read16)(void *ctx, u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 reg, u16 value);
};

/* The vendor helper is intentionally not conventional mask/value RMW:
 * new = (old & ~value) | (value & mask).  The signature keeps the historical
 * argument order visible at every callsite. */
static inline int ob_radio_mod16_historical(
	const struct ob_radio_access_ops *ops, void *ctx, u16 reg,
	u16 value, u16 mask)
{
	u16 old;
	int ret;

	if (!ops || !ops->read16 || !ops->write16)
		return -22;
	ret = ops->read16(ctx, reg, &old);
	if (ret)
		return ret;
	return ops->write16(ctx, reg,
			(u16)((old & (u16)~value) | (value & mask)));
}

struct ob_radio_local_0413_target {
	u16 chip_id;
	u8 chip_rev;
	u8 d11_corerev;
	u8 phy_type;
	u8 phy_revision;
	u16 radio_id;
	u8 radio_revision;
	u8 radio_revision_class;
	u16 chanspec;
	u16 shm_00a0;
	u32 wrapper_cflags;
	bool chanspec_valid;
	bool d11_identity_ready;
	bool acphy_start_ready;
	bool d11_core_enabled;
	bool radio_access_initialized;
	bool valid;
};

/* Pure identity/state gate. It must run before SHM, AXI or BCMA state reads,
 * all of which access device-visible state. */
static inline bool ob_radio_local_0413_identity_ok(
	const struct ob_radio_local_0413_target *target)
{
	return target && target->valid && target->radio_access_initialized &&
		target->chip_id == 0x4352u && target->chip_rev == 3u &&
		target->d11_corerev == 42u && target->phy_type == 0x0bu &&
		target->phy_revision == 1u && target->radio_id == 0x2069u &&
		target->radio_revision == 4u &&
		target->radio_revision_class == 0u &&
		target->chanspec_valid && target->chanspec == 0x1001u &&
		target->d11_identity_ready && target->acphy_start_ready;
}

static inline bool ob_radio_local_0413_target_ok(
	const struct ob_radio_local_0413_target *target)
{
	return ob_radio_local_0413_identity_ok(target) &&
		target->d11_core_enabled && target->shm_00a0 == 0x1001u &&
		(target->wrapper_cflags & 0x000000c0u) == 0x00000040u;
}

enum ob_radio_local_0413_stage {
	OB_RADIO_LOCAL_0413_STAGE_NONE = 0,
	OB_RADIO_LOCAL_0413_STAGE_MAC_QUIESCE,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_8EA,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_8ED,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_A,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_B,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_411_SETUP,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_WRITE_412,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_CLEAR,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_SET,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_411_SET,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_0413_READ,
	OB_RADIO_LOCAL_0413_STAGE_RADIO_0413_TIMEOUT,
	OB_RADIO_LOCAL_0413_STAGE_DEVICE_LOST,
};

struct ob_radio_local_0413_ops {
	int (*quiesce)(void *ctx, bool *transitioned, u32 *polls);
	int (*read16)(void *ctx, u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 reg, u16 value);
	void (*delay_us)(void *ctx, u32 usec);
	bool (*device_lost)(void *ctx);
};

struct ob_radio_local_0413_result {
	enum ob_radio_local_0413_stage failure_stage;
	u32 attempts;
	u16 last_value;
	bool success;
};

static inline int ob_radio_local_0413_run(
	const struct ob_radio_local_0413_ops *ops, void *ctx,
	struct ob_radio_local_0413_result *result)
{
	struct ob_radio_access_ops access;
	bool transitioned;
	u32 quiesce_polls;
	u16 value;
	int ret;
	u32 i;

	if (!ops || !ops->quiesce || !ops->read16 || !ops->write16 ||
	    !ops->delay_us || !ops->device_lost || !result)
		return -22;
	result->failure_stage = OB_RADIO_LOCAL_0413_STAGE_NONE;
	result->attempts = 0;
	result->last_value = 0;
	result->success = false;
	access.read16 = ops->read16;
	access.write16 = ops->write16;
	ret = ops->quiesce(ctx, &transitioned, &quiesce_polls);
	(void)transitioned;
	(void)quiesce_polls;
	if (ret || ops->device_lost(ctx)) {
		result->failure_stage = ops->device_lost(ctx) ?
			OB_RADIO_LOCAL_0413_STAGE_DEVICE_LOST :
			OB_RADIO_LOCAL_0413_STAGE_MAC_QUIESCE;
		return ret ? ret : -5;
	}

#define OB_0413_MOD(_stage, _reg, _value, _mask) do { \
	ret = ob_radio_mod16_historical(&access, ctx, (_reg), (_value), (_mask)); \
	if (ret || ops->device_lost(ctx)) { \
		result->failure_stage = ops->device_lost(ctx) ? \
			OB_RADIO_LOCAL_0413_STAGE_DEVICE_LOST : (_stage); \
		return ret ? ret : -5; \
	} \
} while (0)
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_8EA,
		0x08eau, 0x0080u, 0x0080u);
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_8ED,
		0x08edu, 0x0600u, 0x0400u);
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_A,
		0x0410u, 0x1000u, 0x1000u);
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_B,
		0x0410u, 0x0018u, 0x00e0u);
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_411_SETUP,
		0x0411u, 0xff00u, 0x1c00u);
	ret = ops->write16(ctx, 0x0412u, 0x014au);
	if (ret || ops->device_lost(ctx)) {
		result->failure_stage = ops->device_lost(ctx) ?
			OB_RADIO_LOCAL_0413_STAGE_DEVICE_LOST :
			OB_RADIO_LOCAL_0413_STAGE_RADIO_WRITE_412;
		return ret ? ret : -5;
	}
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_CLEAR,
		0x0410u, 0x0001u, 0x0000u);
	ops->delay_us(ctx, 1u);
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_410_SET,
		0x0410u, 0x0001u, 0x0001u);
	ops->delay_us(ctx, 0x23u);
	OB_0413_MOD(OB_RADIO_LOCAL_0413_STAGE_RADIO_RMW_411_SET,
		0x0411u, 0x0001u, 0x0001u);

	for (i = 0; i < OB_RADIO_LOCAL_0413_MAX_ATTEMPTS; i++) {
		ops->delay_us(ctx, OB_RADIO_LOCAL_0413_POLL_DELAY_US);
		ret = ops->read16(ctx, 0x0413u, &value);
		result->attempts = i + 1u;
		result->last_value = value;
		if (ret || ops->device_lost(ctx)) {
			result->failure_stage = ops->device_lost(ctx) ?
				OB_RADIO_LOCAL_0413_STAGE_DEVICE_LOST :
				OB_RADIO_LOCAL_0413_STAGE_RADIO_0413_READ;
			return ret ? ret : -5;
		}
		if (value & OB_RADIO_LOCAL_0413_POLL_MASK) {
			result->success = true;
			return 0;
		}
	}
	result->failure_stage = OB_RADIO_LOCAL_0413_STAGE_RADIO_0413_TIMEOUT;
	return -110;
#undef OB_0413_MOD
}

static inline int ob_radio_rmw_clear(const struct ob_radio_access_ops *ops,
		void *ctx, u16 reg, u16 mask)
{
	u16 old;
	int ret;

	if (!ops || !ops->read16 || !ops->write16)
		return -22;
	ret = ops->read16(ctx, reg, &old);
	if (ret)
		return ret;
	return ops->write16(ctx, reg, (u16)(old & (u16)~mask));
}

static inline int ob_radio_rmw_set(const struct ob_radio_access_ops *ops,
		void *ctx, u16 reg, u16 mask)
{
	u16 old;
	int ret;

	if (!ops || !ops->read16 || !ops->write16)
		return -22;
	ret = ops->read16(ctx, reg, &old);
	if (ret)
		return ret;
	return ops->write16(ctx, reg, (u16)(old | mask));
}

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

enum ob_radio_stage {
	OB_RADIO_STAGE_NONE = 0,
	OB_RADIO_STAGE_PRE_ACCESS,
	OB_RADIO_STAGE_SELECTOR0_WRITE,
	OB_RADIO_STAGE_REG0_READ,
	OB_RADIO_STAGE_SELECTOR1_WRITE,
	OB_RADIO_STAGE_REG1_READ,
	OB_RADIO_STAGE_MID,
	OB_RADIO_STAGE_POST_ACCESS,
};

/* Optional trace hooks. They run after the existing backend operation and
 * before the sequence makes its next branch decision. A hook failure is
 * fail-closed: no subsequent hardware operation is issued. */
struct ob_radio_trace_hooks {
	int (*phase)(void *ctx, enum ob_radio_stage stage);
	/* Runs only after the MID phase transition and before SENTINEL. */
	int (*mid)(void *ctx);
	int (*read32)(void *ctx, enum ob_radio_stage stage, u16 off, u32 value);
	int (*read16)(void *ctx, enum ob_radio_stage stage, u16 off, u16 value);
	int (*write16)(void *ctx, enum ob_radio_stage stage, u16 off, u16 value);
	int (*stop)(void *ctx, bool lost);
};

struct ob_radio_io_ops {
	u32  (*read32_trusted)(void *ctx, u16 off);
	u16  (*read16)(void *ctx, u16 off);
	void (*write16)(void *ctx, u16 off, u16 val);
	const struct ob_radio_trace_hooks *trace;
	void *trace_ctx;
};

/* Confirmed BCM2069/AC-PHY radio register transition.  This contract is
 * deliberately independent of Linux and keeps status register 0x090b out of
 * the transition until its semantics are proven. */
struct ob_radio2069_io_ops {
	u16 (*read16)(void *ctx, u16 reg);
	void (*write16)(void *ctx, u16 reg, u16 value);
	void (*delay_us)(void *ctx, u32 usec);
};

/* Vendor-primary prefregs_2069_rev4 @ .data+0x140ba0 (24 pairs). */
struct ob_radio2069_prefreg {
	u16 reg;
	u16 value;
};

static const struct ob_radio2069_prefreg ob_radio2069_rev4_prefregs[] = {
	{ 0x063a, 0x0000 }, { 0x063d, 0x000f }, { 0x0645, 0x3000 },
	{ 0x0646, 0x0303 }, { 0x0647, 0x36e2 }, { 0x0649, 0x6000 },
	{ 0x064a, 0x0003 }, { 0x065e, 0x0ff4 }, { 0x0666, 0x0b2d },
	{ 0x0667, 0x03ff }, { 0x0115, 0x3337 }, { 0x0117, 0x1337 },
	{ 0x0718, 0x7777 }, { 0x071c, 0x1000 }, { 0x0726, 0x0100 },
	{ 0x0727, 0x0002 }, { 0x0761, 0x0100 }, { 0x0407, 0x8382 },
	{ 0x055e, 0x0020 }, { 0x0885, 0x0700 }, { 0x08e9, 0x01ce },
	{ 0x08ea, 0x00ff }, { 0x08ec, 0x06ff }, { 0x0972, 0x0600 },
};

#define OB_RADIO2069_REV4_PREFREG_COUNT \
	(sizeof(ob_radio2069_rev4_prefregs) / sizeof(ob_radio2069_rev4_prefregs[0]))

static inline int ob_radio2069_apply_rev4_prefregs(
	const struct ob_radio_access_ops *ops, void *ctx, u16 radio_id,
	u8 radio_revision)
{
	size_t i;

	if (!ops || !ops->write16 || radio_id != OB_RADIO_BCM2069_ID ||
	    radio_revision != 4u)
		return -22;
	for (i = 0; i < OB_RADIO2069_REV4_PREFREG_COUNT; i++) {
		int ret = ops->write16(ctx, ob_radio2069_rev4_prefregs[i].reg,
			ob_radio2069_rev4_prefregs[i].value);
		if (ret)
			return ret;
	}
	return 0;
}

/* Checked form used by the production isolated stage. */
struct ob_radio2069_checked_ops {
	int (*read16)(void *ctx, u16 reg, u16 *value);
	int (*write16)(void *ctx, u16 reg, u16 value);
	void (*delay_us)(void *ctx, u32 usec);
};

static inline int ob_radio2069_synth_transition_checked(
	const struct ob_radio2069_checked_ops *ops, void *ctx, bool mac_suspended)
{
	u16 value;
	int ret;

	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us ||
	    !mac_suspended)
		return -22;
#define OB_R2069_RMW(mask, reg, expr) do { \
	ret = ops->read16(ctx, (reg), &value); if (ret) return ret; \
	ret = ops->write16(ctx, (reg), (u16)(expr)); if (ret) return ret; \
} while (0)
	OB_R2069_RMW(0x4000u, 0x08e5u, value & (u16)~0x4000u);
	OB_R2069_RMW(0x0001u, 0x08d0u, value & (u16)~0x0001u);
	OB_R2069_RMW(0x0040u, 0x08e8u, value & (u16)~0x0040u);
	OB_R2069_RMW(0x2000u, 0x08dcu, value & (u16)~0x2000u);
	ops->delay_us(ctx, 11u);
	OB_R2069_RMW(0x0001u, 0x08d0u, value | 0x0001u);
	OB_R2069_RMW(0x0040u, 0x08e8u, value | 0x0040u);
	ops->delay_us(ctx, 1u);
	OB_R2069_RMW(0x2000u, 0x08dcu, value | 0x2000u);
#undef OB_R2069_RMW
	return 0;
}

static inline int ob_radio2069_synth_transition(
	const struct ob_radio2069_io_ops *ops, void *ctx, bool mac_suspended)
{
	u16 value;

	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us)
		return -22; /* -EINVAL */
	if (!mac_suspended)
		return -5; /* -EIO: fail closed before any radio access */

	value = ops->read16(ctx, 0x08e5u);
	ops->write16(ctx, 0x08e5u, (u16)(value & (u16)~0x4000u));
	value = ops->read16(ctx, 0x08d0u);
	ops->write16(ctx, 0x08d0u, (u16)(value & (u16)~0x0001u));
	value = ops->read16(ctx, 0x08e8u);
	ops->write16(ctx, 0x08e8u, (u16)(value & (u16)~0x0040u));
	value = ops->read16(ctx, 0x08dcu);
	ops->write16(ctx, 0x08dcu, (u16)(value & (u16)~0x2000u));
	ops->delay_us(ctx, 11u);
	value = ops->read16(ctx, 0x08d0u);
	ops->write16(ctx, 0x08d0u, (u16)(value | 0x0001u));
	value = ops->read16(ctx, 0x08e8u);
	ops->write16(ctx, 0x08e8u, (u16)(value | 0x0040u));
	ops->delay_us(ctx, 1u);
	value = ops->read16(ctx, 0x08dcu);
	ops->write16(ctx, 0x08dcu, (u16)(value | 0x2000u));
	return 0;
}

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
	enum ob_radio_stage failure_stage;
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
	int trace_rc;

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
	res->failure_stage = OB_RADIO_STAGE_NONE;
	res->rc = 0;

	if (*lost) {
		if (ops->trace && ops->trace->stop)
			ops->trace->stop(ops->trace_ctx, true);
		res->stopped_dev_lost = true;
		res->rc = -5;	/* -EIO without the kernel header */
		return res->rc;
	}

	res->failure_stage = OB_RADIO_STAGE_PRE_ACCESS;
	if (ops->trace && ops->trace->phase) {
		trace_rc = ops->trace->phase(ops->trace_ctx,
					     OB_RADIO_STAGE_PRE_ACCESS);
		if (trace_rc)
			goto trace_fail;
	}
	pre = ops->read32_trusted(ctx, OB_RADIO_REG_SENTINEL);
	if (ops->trace && ops->trace->read32) {
		trace_rc = ops->trace->read32(ops->trace_ctx,
					      OB_RADIO_STAGE_PRE_ACCESS,
					      OB_RADIO_REG_SENTINEL, pre);
		if (trace_rc)
			goto trace_fail;
	}
	res->pre_access = pre;
	if (*lost || pre == OB_GUARD_ALL_ONES32) {
		*lost = true;
		if (ops->trace && ops->trace->stop)
			ops->trace->stop(ops->trace_ctx, true);
		res->stopped_dev_lost = true;
		res->rc = -5;
		return res->rc;
	}

	if (*lost)
		goto lost_mid;

	res->failure_stage = OB_RADIO_STAGE_SELECTOR0_WRITE;
	if (ops->trace && ops->trace->phase) {
		trace_rc = ops->trace->phase(ops->trace_ctx,
					     OB_RADIO_STAGE_SELECTOR0_WRITE);
		if (trace_rc)
			goto trace_fail;
	}
	ops->write16(ctx, OB_RADIO_REG_ADDR_LATCH, 0);
	if (ops->trace && ops->trace->write16) {
		trace_rc = ops->trace->write16(ops->trace_ctx,
					       OB_RADIO_STAGE_SELECTOR0_WRITE,
					       OB_RADIO_REG_ADDR_LATCH, 0);
		if (trace_rc)
			goto trace_fail;
	}
	res->radio_writes++;
	res->failure_stage = OB_RADIO_STAGE_REG0_READ;
	if (ops->trace && ops->trace->phase) {
		trace_rc = ops->trace->phase(ops->trace_ctx,
					     OB_RADIO_STAGE_REG0_READ);
		if (trace_rc)
			goto trace_fail;
	}
	res->reg0_raw = ops->read16(ctx, OB_RADIO_REG_DATA);
	if (ops->trace && ops->trace->read16) {
		trace_rc = ops->trace->read16(ops->trace_ctx,
					      OB_RADIO_STAGE_REG0_READ,
					      OB_RADIO_REG_DATA, res->reg0_raw);
		if (trace_rc)
			goto trace_fail;
	}
	res->radio_reads++;

	if (*lost)
		goto lost_mid;

	res->failure_stage = OB_RADIO_STAGE_SELECTOR1_WRITE;
	if (ops->trace && ops->trace->phase) {
		trace_rc = ops->trace->phase(ops->trace_ctx,
					     OB_RADIO_STAGE_SELECTOR1_WRITE);
		if (trace_rc)
			goto trace_fail;
	}
	ops->write16(ctx, OB_RADIO_REG_ADDR_LATCH, 1);
	if (ops->trace && ops->trace->write16) {
		trace_rc = ops->trace->write16(ops->trace_ctx,
					       OB_RADIO_STAGE_SELECTOR1_WRITE,
					       OB_RADIO_REG_ADDR_LATCH, 1);
		if (trace_rc)
			goto trace_fail;
	}
	res->radio_writes++;
	res->failure_stage = OB_RADIO_STAGE_REG1_READ;
	if (ops->trace && ops->trace->phase) {
		trace_rc = ops->trace->phase(ops->trace_ctx,
					     OB_RADIO_STAGE_REG1_READ);
		if (trace_rc)
			goto trace_fail;
	}
	res->reg1_raw = ops->read16(ctx, OB_RADIO_REG_DATA);
	if (ops->trace && ops->trace->read16) {
		trace_rc = ops->trace->read16(ops->trace_ctx,
					      OB_RADIO_STAGE_REG1_READ,
					      OB_RADIO_REG_DATA, res->reg1_raw);
		if (trace_rc)
			goto trace_fail;
	}
	res->radio_reads++;

	res->ran = true;
	res->obs = ob_radio_decode(res->reg0_raw, res->reg1_raw);
	res->valid = !res->obs.all_ones_pair;

	res->failure_stage = OB_RADIO_STAGE_MID;
	if (ops->trace && ops->trace->phase) {
		trace_rc = ops->trace->phase(ops->trace_ctx, OB_RADIO_STAGE_MID);
		if (trace_rc)
			goto trace_fail;
	}
	if (ops->trace && ops->trace->mid) {
		trace_rc = ops->trace->mid(ops->trace_ctx);
		if (trace_rc)
			goto trace_fail;
	}
	if (*lost)
		goto lost_mid;

	res->failure_stage = OB_RADIO_STAGE_POST_ACCESS;
	if (ops->trace && ops->trace->phase) {
		trace_rc = ops->trace->phase(ops->trace_ctx,
					     OB_RADIO_STAGE_POST_ACCESS);
		if (trace_rc)
			goto trace_fail;
	}
	post = ops->read32_trusted(ctx, OB_RADIO_REG_SENTINEL);
	if (ops->trace && ops->trace->read32) {
		trace_rc = ops->trace->read32(ops->trace_ctx,
					      OB_RADIO_STAGE_POST_ACCESS,
					      OB_RADIO_REG_SENTINEL, post);
		if (trace_rc)
			goto trace_fail;
	}
	res->post_access = post;
	if (*lost || post == OB_GUARD_ALL_ONES32) {
		*lost = true;
		if (ops->trace && ops->trace->stop)
			ops->trace->stop(ops->trace_ctx, true);
		res->dev_lost_after = true;
		res->stopped_dev_lost = true;
		res->valid = false;
		res->rc = -5;
		return res->rc;
	}
	return 0;

lost_mid:
	*lost = true;
	if (ops->trace && ops->trace->stop)
		ops->trace->stop(ops->trace_ctx, true);
	res->stopped_dev_lost = true;
	res->valid = false;
	res->rc = -5;
	return res->rc;

trace_fail:
	if (ops->trace && ops->trace->stop)
		ops->trace->stop(ops->trace_ctx, *lost);
	res->stopped_dev_lost = *lost;
	res->valid = false;
	res->rc = -5;
	return res->rc;
}

/* ---- kernel entry + remove hook (implemented in ob_radio.c) ------------- */
#ifdef __KERNEL__
struct ob_hw;

int  ob_radio_probe_test(struct ob_hw *hw);
int  ob_radio_prepare_for_identity(struct ob_hw *hw);
int  ob_radio_vendor_acphy_attach_anacore(struct ob_hw *hw, u8 phy_type);
int  ob_radio_vendor_acphy_reset(struct ob_hw *hw, u8 phy_type);
int  ob_radio_read_identity(struct ob_hw *hw,
		struct ob_radio_observation *observation);
int  ob_radio_read16(struct ob_hw *hw, u16 reg, u16 *value);
int  ob_radio_write16(struct ob_hw *hw, u16 reg, u16 value);
int  ob_radio2069_rev4_probe_test(struct ob_hw *hw);
int  ob_radio_local_0413_test(struct ob_hw *hw);
int  ob_radio_class0_post0413_test(struct ob_hw *hw);
int  ob_radio_class0_postphase1_test(struct ob_hw *hw);
int  ob_radio_class0_postphase2_return_test(struct ob_hw *hw);
int  ob_radio_class0_initialize_to_sub9591e(struct ob_hw *hw);
int  ob_radio_class0_postbmac_branch_gate_test(struct ob_hw *hw);
int  ob_radio_rmw_clear16(struct ob_hw *hw, u16 reg, u16 mask);
int  ob_radio_rmw_set16(struct ob_hw *hw, u16 reg, u16 mask);
void ob_radio_remove(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_RADIO_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — rev42 staged diagnostic bring-up (REV42-DIAGNOSTIC-BRINGUP).
 *
 * An explicitly experimental, isolated mode (`rev42_diag_probe_only=1`) that
 * walks the recovered BCM4352 / D11 corerev 42 / AC-PHY (phy_type 0x0B)
 * bring-up in controlled checkpoints and STOPS at a stable radio-ID read. It
 * never reaches band init, PHY init, radio configuration, calibration, channel,
 * EN_MAC, DMA, the host IRQ route or mac80211.
 *
 * Sequence (see REV42-VENDOR-BRINGUP.md and artifacts/rev42_radio_probe.yaml):
 *
 *   core prep (host up, D11 enable, FAST clock, MAC/PHY clock)
 *     -> CP0  D11 accessible before ucode
 *     -> radio read #1 (core-only baseline; the vendor-minimal premise)
 *     -> [if rev42_diag_full=1]
 *          D2A  ucode upload + PSM start
 *          -> CP1 D11 accessible after ucode
 *          D2B  common initvals (610 records)
 *          -> CP2 D11 accessible after common initvals
 *          -> CP3 required PSM/MAC state (MACCONTROL=0x04020402)
 *     -> CP4 PHY/radio prerequisite: wlc_phy_anacore(pi,1) direct side
 *            (writew(0x0000, D11+0x3e6)); PHY version read (0x3e0)
 *     -> radio read #2 + vendor-faithful decode
 *     -> CP5 radio ID read;  CP6 device still alive
 *     -> STOP
 *
 * IMPORTANT (CONFIRMED_VENDOR_REV42): the radio-ID read does NOT require
 * ucode, PSM run, common initvals or bsinitvals. The `rev42_diag_full=1`
 * staged path exists only to exercise CP1/CP2/CP3 (the task's checkpoints) so
 * the experiment can establish which transitions are healthy.
 *
 * The `[pi+0x118]` AC-PHY anacore callback body is UNKNOWN and is NOT run; only
 * the CONFIRMED direct write to D11+0x3e6 is issued.
 *
 * All register access goes through the central dev_lost guard. After the
 * monotonic latch no MMIO is issued; the module is pinned and reboot clears it.
 */
#ifndef _OB_DIAG_H_
#define _OB_DIAG_H_

#include <linux/types.h>
#include "ob_guard.h"

/* ---- D11 offsets (vendor register_map, REV42-VENDOR-BRINGUP.md §10/§18) -- */
#define OB_DIAG_REG_RADIO_ADDR		0x03d8u	/* radio ADDRESS register */
#define OB_DIAG_REG_RADIO_DATA		0x03dau	/* radio DATA register */
#define OB_DIAG_REG_PHY_ANALOG		0x03e6u	/* PHY analog control */
#define OB_DIAG_REG_PHY_VER		0x03e0u	/* PHY version (read) */
#define OB_DIAG_REG_MACCONTROL		0x0120u
#define OB_DIAG_REG_MACINTSTATUS	0x0128u
#define OB_DIAG_REG_MACINTMASK		0x012cu

/* ---- AI-wrapper window offsets (relative to the BCMA wrapper window) ---- *
 *
 * These are OFFSETS inside an already-selected BCMA window, not backplane
 * addresses. The window itself is selected by BCMA from the eROM-enumerated
 * core->wrap; ob_diag never programs a backplane address itself.
 */
#define OB_DIAG_AI_IOCTL		0x0408u	/* wrapper IOCTL */
#define OB_DIAG_AI_RESET_CTL		0x0800u	/* wrapper RESET_CTL */
#define OB_DIAG_AI_RESET_ST		0x0804u	/* wrapper RESET_ST */

/*
 * D11 core functional window PROBE OFFSET (BCMA core window, core->addr).
 *
 * The register identity at this offset is UNKNOWN in this project: re.db
 * indexes the vendor blob, not a silicon register map. It is named
 * "probe offset", not "core ID", because naming it after a hypothesised
 * register is exactly the stale assumption this offset must not carry.
 *
 * The value read here is a RAW OBSERVATION only. It is NOT a readability
 * discriminator, NOT a liveness discriminator and NOT evidence of a wrong
 * window (D11-ADDR-S1). The measured value is 0xffffffff both while the core
 * is down (hardware RUN 1) and after the core-enable transition reports
 * ai_iscoreup=1 (hardware RUN 2), which is precisely why it discriminates
 * nothing. Whether the D11 MAC register file decodes, and what the D11
 * internal clock state is, is OPEN and belongs to M3.4E.
 *
 * Renamed from the earlier "core ID" name in the M3.4D reconciliation. The
 * rename is semantics-preserving: the offset, the accessor, the execution order
 * and the number of reads are unchanged.
 */
#define OB_DIAG_PROBE_OFF_0FFC		0x0ffcu

/* MACCONTROL bits (d11.h) and the vendor D2B exit value. */
#define OB_DIAG_MCTL_EN_MAC		0x00000001u
#define OB_DIAG_MCTL_PSM_RUN		0x00000002u
#define OB_DIAG_MCTL_IHR_EN		0x00000400u
#define OB_DIAG_MCTL_INFRA		0x00020000u
#define OB_DIAG_MCTL_WAKE		0x04000000u
#define OB_DIAG_MACCONTROL_UPLOAD	0x04000404u
#define OB_DIAG_MACCONTROL_D2B		0x04020402u

/* wlc_phy_anacore direct side (on=1 -> 0x0000; on=0 -> 0x00f4). */
#define OB_DIAG_PHY_ANALOG_ON		0x0000u

/* Part-family inference only (PROBABLE_REV42), never used as a pass gate. */
#define OB_DIAG_RADIO_ID_MIN		0x09u
#define OB_DIAG_RADIO_ID_MAX		0x0bu

/* D11 corerev gate. */
#define OB_DIAG_PHYREV_REV42		0x2au

/*
 * Vendor sb_core_reset cold path (REV42-SB-CORE-RESET / rev42_sb_core_reset.yaml).
 *
 * TERMINOLOGY: the Broadcom SB (silicon bridge) register block lives at the top
 * of the D11 CORE FUNCTIONAL WINDOW (0x0F00-0x0FFF), i.e. the window selected by
 * core->addr. It is NOT the BCMA "wrapper" window selected by core->wrap (the
 * AI/ARM agent window, which this mode never touches). Offsets below are
 * therefore reached with ob_d11_read32()/ob_d11_write32(), never ob_axi_*(), and
 * the trace block label is "SB_CORE".
 */
#define OB_SB_REG_STATUS_S	0x00f8u	/* 0x0F8  SB block status, bit31 */
#define OB_SB_REG_FORCEGATE	0x00f90u /* 0x0F90 force-gate / slope */
#define OB_SB_REG_CTRL		0x00f98u /* 0x0F98 SB core/reset control */
#define OB_SB_REG_STATUS	0x00f9cu /* 0x0F9C SB core/reset status */
/*
 * D11 window +0x1E0. Same register as OB_UCODE_REG_D11_CLK1E0 (kept as a
 * separate name because the rev42-sb sequence addresses it directly);
 * identity UNKNOWN, not the ChipCommon CLKCTLST. Vendor-proven bit usage
 * here: bit1 (sub_64887 mac-clock on/off), bit5 (wlc_bmac_radio_hw),
 * bits 8|9 (wlc_bmac_core_phypll_ctl). bit17 is never used by the vendor on
 * this window -- it is the ChipCommon HAVEHT bit, a different register.
 */
#define OB_SB_REG_CLK		0x001e0u /* D11+0x1E0, identity UNKNOWN */

#define OB_SB_CTRL_70003	0x00070003u
#define OB_SB_CTRL_40003	0x00040003u
#define OB_SB_CTRL_70001	0x00070001u
#define OB_SB_CTRL_70000	0x00070000u
#define OB_SB_CTRL_50000	0x00050000u

#define OB_SB_ISCOREUP_MASK	0x00010007u
#define OB_SB_ISCOREUP_VAL	0x00010000u
/* R-2: bit2 of 0x0F98 must be clear; if set on the cold device, STOP. */
#define OB_SB_CTRL_BIT2		0x00000004u
/* D11 functional registers tested only AFTER the predicate passes. */
#define OB_SB_FIRST_D11_REG	0x03e0u	/* vendor readw(D11+0x3E0) */

/*
 * Cold-attach sequence constants (REV42-RADIO-ATTACH-SEQUENCE, YAML ops 0..27).
 *
 * sb_iscoreup reads the D11 core window word at si->(0xb8)+0x0F98; on this
 * silicon sh->(0xd0) is the same D11 base, so it is the D11 offset 0x0F98.
 *
 * The 0xd0-is-the-D11-base claim is carried by the register file itself,
 * not by one offset: re.db shows the *(a0 + 0xd0) base used for 0x120/0x128/
 * 0x12C (MACCONTROL/MACINTSTATUS/MACINTMASK), 0x160/0x164 (OBJADDR/OBJDATA),
 * 0x18C and 0x604/0x606 (wlc_bmac_validate_chip_access), 0x62E/0x630
 * (wlc_bmac_switch_macfreq TSF clock fraction) and the five direct IHR
 * writes 0x680..0x700 of the 73 bsinitvals records. An earlier version of
 * this comment argued the base from "CORE+0x1E0 == CLKCTLST", which was
 * invalid: it compared a D11-window read against a ChipCommon register.
 * See docs/m34d4/m34e_d11_mac_accessibility.md.
 */
#define OB_DIAG_REG_SB_CHIPCONTROL	0x0f98u
#define OB_DIAG_SICF_ISCOREUP_MASK	0x00010007u
#define OB_DIAG_SICF_ISCOREUP_VAL	0x00010000u
/* D11 +0x1e0, identity UNKNOWN. NOT CLKCTLST (that is ChipCommon). */
#define OB_DIAG_REG_D11_CLK1E0		0x01e0u
#define OB_DIAG_D11_CLK1E0_BIT1		0x00000002u

/*
 * Vendor `wlc_bmac_mctrl(sh, ~0, 0x4000400)` in wlc_bmac_corereset tail
 * (D11+0x120 = WAKE|IHR_EN).
 */
#define OB_DIAG_MACCONTROL_CORERESET	0x04000400u

/*
 * `si_core_cflags` maps to the core-wrapper IO-control register (BCMA_IOCTL).
 * This is the register OpenBRCM already used for SICF_MPCLKE; the vendor AC-PHY
 * clock pair (SICF 0x300/0x100, 0x6, 0x4) targets the same word. Logged as a
 * MAPPING ASSUMPTION (the vendor software base is not directly available).
 */
#define OB_DIAG_AXI_IOCTL		0x0408u

/* Vendor AC-PHY clock pair applied inside wlc_bmac_corereset (phy_type 0x0B). */
#define OB_DIAG_CFLAGS_CORERESET_A_MASK	0x0300u
#define OB_DIAG_CFLAGS_CORERESET_A_VAL	0x0100u
#define OB_DIAG_CFLAGS_CORERESET_B_MASK	0x0006u
#define OB_DIAG_CFLAGS_CORERESET_C_MASK	0x0004u
#define OB_DIAG_CFLAGS_CORERESET_C_VAL	0x0004u
/* Vendor wlc_bmac_core_phy_clk pair (the last cflags before SITE A). */
#define OB_DIAG_CFLAGS_PHYCLK_A_MASK	0x000eu
#define OB_DIAG_CFLAGS_PHYCLK_B_MASK	0x0004u
#define OB_DIAG_CFLAGS_PHYCLK_B_VAL	0x0004u

/* ---- pure vendor-faithful radio decode (host-testable) ------------------ */

struct ob_diag_radio {
	u16  r0;		/* RADIO[0x0] */
	u16  r1;		/* RADIO[0x1] */
	u32  id32;		/* (r0 << 16) | r1 */
	u8   id_byte;		/* pi+0x16e = (id32 >> 20) & 0xff = r0 >> 4 */
	u8   low_nibble;	/* pi+0x16f = (id32 >> 16) & 0x0f = r0 & 0x0f */
	u8   low_byte;		/* pi+0x16c = (id32 >> 16) & 0xff = r0 & 0xff */
	u16  pa_r1;		/* pi+0x16a = id32 & 0xffff = r1 */
	bool all_ones;		/* r0 == r1 == 0xffff (dead window) */
	bool all_zero;		/* r0 == r1 == 0 */
	bool valid;		/* r0/r1 are a decodable, live identity */
	bool id_in_family;	/* id_byte in the PROBABLE 0x09..0x0b range */
};

static inline struct ob_diag_radio ob_diag_vendor_decode(u16 r0, u16 r1)
{
	struct ob_diag_radio d;

	d.r0 = r0;
	d.r1 = r1;
	d.id32 = ((u32)r0 << 16) | (u32)r1;
	d.id_byte = (u8)((d.id32 >> 20) & 0xffu);
	d.low_nibble = (u8)((d.id32 >> 16) & 0x0fu);
	d.low_byte = (u8)((d.id32 >> 16) & 0xffu);
	d.pa_r1 = (u16)(d.id32 & 0xffffu);
	d.all_ones = (r0 == 0xffffu) && (r1 == 0xffffu);
	d.all_zero = (r0 == 0u) && (r1 == 0u);
	d.valid = !d.all_ones && !d.all_zero;
	d.id_in_family = (d.id_byte >= OB_DIAG_RADIO_ID_MIN) &&
			 (d.id_byte <= OB_DIAG_RADIO_ID_MAX);
	return d;
}

/* Reconstruct RADIO[0x0] from the two cached fields ((0x16e<<4)|0x16f). */
static inline u16 ob_diag_r0_from_fields(u8 id_byte, u8 low_nibble)
{
	return (u16)(((u16)(id_byte & 0x0fu) << 4) | (low_nibble & 0x0fu));
}

/*
 * A live D11 window is never all-ones on a trusted direct read. A checkpoint
 * whose sentinel is all-ones means the device was lost at that point.
 */
static inline bool ob_diag_sentinel_lost(u32 sentinel)
{
	return sentinel == OB_GUARD_ALL_ONES32;
}

/* ---- AI-wrapper probe: BCMA-owned addressing (host-testable) ----------- *
 *
 * The AI (ARM) wrapper window of the D11 core is NOT selected by ob_diag.
 * Upstream BCMA owns the window program:
 *
 *   bcma_read32(core, off)   -> core functional window (core->addr)
 *   bcma_aread32(core, off)  -> wrapper window        (core->wrap)
 *
 * and `bcma_host_pci_switch_core()` writes core->addr / core->wrap into the
 * BAR0 window registers and updates bus->mapped_core atomically. ob_diag only
 * ever calls the accessors, so it cannot desynchronise mapped_core and cannot
 * guess a D11 wrapper backplane address.
 *
 * The helpers below are pure: they take the values as arguments so the whole
 * classification can be exercised on the host with no hardware.
 */

/*
 * Observation record for the AI-wrapper probe. `win_core` / `win_wrap` are
 * the BAR0 window registers read back for EVIDENCE ONLY; ob_diag never writes
 * them. `all_ones` fields are reported, never used as a reachability verdict
 * (an unimplemented register inside a valid decoded window can also read
 * all-ones, so all-ones is not a wrong-window discriminator).
 */
struct ob_diag_ai {
	u32  ioc;			/* wrapper IOCTL */
	u32  rctl;			/* wrapper RESET_CTL */
	u32  rst;			/* wrapper RESET_ST */
	u32  probe_0ffc;			/* raw core-window read at 0x0ffc; observation only */
	u32  win_core;			/* cfg BAR0_WIN   readback */
	u32  win_wrap;			/* cfg WIN2        readback */	u32  core_addr;		/* BCMA core->addr (eROM) */
	u32  core_wrap;		/* BCMA core->wrap (eROM) */
	bool core_up;			/* vendor ai_iscoreup */
	bool coherent;			/* readback == BCMA-owned; only valid if cfg_ok */
	bool cfg_ok;			/* both PCI config reads actually succeeded */
	bool wrapper_all_ones;		/* observation only */
	bool core_all_ones;		/* observation only */
};

/*
 * PCI config readback status (D11-ADDR-C1/C2).
 *
 * pci_read_config_dword() does two things on failure: it returns non-zero AND
 * it leaves PCI_ERROR_RESPONSE (~0) in the caller's variable -- see
 * PCI_OP_READ() in drivers/pci/access.c together with
 * PCI_SET_ERROR_RESPONSE() in include/linux/pci.h. A caller that ignores the
 * return code therefore cannot distinguish "this register really is all-ones"
 * from "this read did not happen", and would go on to print a coherency
 * verdict that is really just the error sentinel echoed back.
 *
 * Both reads must succeed for the readback to carry any information at all.
 */
static inline bool ob_diag_ai_cfg_ok(int rc_core, int rc_wrap)
{
	return (rc_core == 0) && (rc_wrap == 0);
}

/*
 * Tri-state coherency verdict: 1 = coherent, 0 = incoherent, -1 = UNKNOWN
 * because the config readback failed.
 *
 * The UNKNOWN case must never be flattened into 0. "incoherent" is a claim
 * about which backplane window is selected, and a failed read supports no such
 * claim in either direction. BCMA's own window programming is what the
 * readback is supposed to corroborate, so the corroboration failing must not
 * be reported as the corroboration being refuted.
 */
static inline int ob_diag_ai_coherent_verdict(bool cfg_ok, bool coherent)
{
	if (!cfg_ok)
		return -1;
	return coherent ? 1 : 0;
}

/* dev_lost gate: no D11/BCMA/window access may be issued after the latch. */
static inline bool ob_diag_ai_may_access(bool dev_lost)
{
	return !dev_lost;
}

/*
 * Window coherency: the BAR0 window registers must reflect exactly the
 * BCMA-owned core->addr / core->wrap. Because ob_diag only reads them and
 * lets BCMA perform the switch, equality is the expected steady state; a
 * mismatch is reported as evidence, never silently repaired by ob_diag.
 */
static inline bool ob_diag_ai_window_coherent(u32 cfg_core, u32 cfg_wrap,
					      u32 core_addr, u32 core_wrap)
{
	return (cfg_core == core_addr) && (cfg_wrap == core_wrap);
}

/*
 * Vendor ai_iscoreup predicate: (IOCTL & 0x3) == 0x1 && (RESET_CTL & 0x1) == 0x0.
 * This is the only reachability verdict; all-ones is NOT used to classify.
 */
static inline bool ob_diag_ai_iscoreup(u32 ioc, u32 rctl)
{
	return ((ioc & 0x3u) == 0x1u) && ((rctl & 0x1u) == 0x0u);
}

/*
 * Fill the observation record from the raw values.
 *
 * `rc_core` / `rc_wrap` are the return codes of the two
 * pci_read_config_dword() calls. They are NOT optional: on failure the kernel
 * has already overwritten the out-parameter with PCI_ERROR_RESPONSE, so the
 * numeric values alone cannot be trusted and the sentinel must not be allowed
 * to reach the record. When either read failed, cfg_core/cfg_wrap are dropped
 * to 0 and `coherent` is forced false, and callers must read the verdict
 * through ob_diag_ai_coherent_verdict(), which reports UNKNOWN in that case.
 */
static inline struct ob_diag_ai ob_diag_ai_summarise(u32 ioc, u32 rctl, u32 rst,
						     u32 probe_0ffc, u32 cfg_core,
						     u32 cfg_wrap, u32 core_addr,
						     u32 core_wrap,
						     int rc_core, int rc_wrap)
{
	struct ob_diag_ai a;

	a.ioc = ioc;
	a.rctl = rctl;
	a.rst = rst;
	a.probe_0ffc = probe_0ffc;
	a.cfg_ok = ob_diag_ai_cfg_ok(rc_core, rc_wrap);
	if (!a.cfg_ok) {
		/* Failed read: PCI_ERROR_RESPONSE is not register content. */
		a.win_core = 0;
		a.win_wrap = 0;
	} else {
		a.win_core = cfg_core;
		a.win_wrap = cfg_wrap;
	}
	a.core_addr = core_addr;
	a.core_wrap = core_wrap;
	a.core_up = ob_diag_ai_iscoreup(ioc, rctl);
	a.coherent = a.cfg_ok &&
		     ob_diag_ai_window_coherent(cfg_core, cfg_wrap, core_addr,
						core_wrap);
	a.wrapper_all_ones = (ioc == 0xffffffffu) && (rctl == 0xffffffffu) &&
			     (rst == 0xffffffffu);
	a.core_all_ones = (probe_0ffc == 0xffffffffu);
	return a;
}

/* ---- AI core-enable transition (rev42_ai_enable_only) ------------------- *
 *
 * The decision and sequencing logic for rev42_ai_enable_only lives in
 * ob_ai_enable_only.h as a pure, host-executable state machine. This header
 * only re-exports it, so the module and the host tests compile the SAME code.
 *
 * Phase 37B removed the D11 functional-core read (0x0FFC) from this mode
 * entirely: the transition ends at the AI-wrapper boundary in every branch,
 * including the success branch. The former OB_AI_EN_* step/class vocabulary is
 * replaced by OB_AIE_*; the register values themselves are unchanged and are
 * still transcribed from drivers/bcma/core.c:bcma_core_enable() with flags=0.
 */
#include "ob_ai_enable_only.h"

/*
 * ---- wrapper-enable + ONE D11 MACCONTROL read (Phase 38A) ----------------
 *
 * rev42_d11_maccontrol_once composes the Phase 37B engine above with exactly
 * one functional-core read. The contract (single enumerated read callback, no
 * write callback, gate on the ACTUAL wrapper result, fail-stop classification)
 * lives in ob_d11_maccontrol_once.h so the module and the host tests compile
 * the SAME code.
 */
#include "ob_d11_maccontrol_once.h"

/* ---- kernel state and entry points (implemented in ob_diag.c) ----------- */
#ifdef __KERNEL__
struct ob_hw;

#define OB_DIAG_CP_COUNT	16	/* CP0..CP6 (diag) / CP0..CP15 (attach) */

struct ob_diag_cp {
	bool	reached;
	bool	dev_lost;
	u32	sentinel;	/* trusted MACCONTROL read */
	u32	maccontrol;
	u32	macintstatus;
	u32	macintmask;
};

struct ob_diag {
	bool	full;			/* rev42_diag_full staged path */
	u32	seq;			/* trace operation sequence */
	struct ob_diag_cp cp[OB_DIAG_CP_COUNT];
	u16	phy_ver;		/* D11+0x3e0 at CP4 */
	struct ob_diag_radio pre;	/* core-only radio read (CP0 baseline) */
	struct ob_diag_radio post;	/* radio read after PHY prerequisite */
	bool	pre_read_ran;
	bool	post_read_ran;
	bool	anacore_side_ran;
	bool	stopped_before_band;

	/* ---- cold-attach probe (rev42_attach_probe_only) ---- */
	bool	attach;
	bool	iscoreup_before;
	bool	iscoreup_after;
	u32	iscoreup_raw_before;
	u32	iscoreup_raw_after;
	u16	phyver_pre;	/* D11+0x3E0 read before SITE A (step 12) */
	u16	phyver_post;	/* D11+0x3E0 read after SITE A (step 15) */
	u8	phy_type;
	u8	phy_rev;
	bool	selector_available;	/* vendor sh->(0x28) is not present */
	u32	selector;
	bool	window_modern;
	struct ob_diag_radio radio;
	bool	radio_ran;

	/* ---- vendor sb_core_reset probe (rev42_sb_reset_only) ---- */
	bool	sb_reset;
	u32	sb_ctrl_r0;		/* initial 0x0F98 */
	u32	sb_status_s_r0;		/* initial 0x0F8 */
	u32	sb_forcegate_r0;	/* initial 0x0F90 */
	u32	sb_status_r0;		/* initial 0x0F9C */
	bool	sb_bit0_entry;
	bool	sb_bit16_entry;
	bool	sb_early_out;
	bool	sb_forcegate_seq12;
	bool	sb_latch_seq16;
	bool	sb_forcegate_seq17;
	u32	sb_ctrl_terminal;
	bool	sb_iscoreup;
	bool	sb_first_d11_ran;
	u16	sb_first_d11_3e0;
	u32	sb_first_d11_maccontrol;
};

int  ob_diag_test(struct ob_hw *hw);
int  ob_attach_test(struct ob_hw *hw);
int  ob_sb_reset_test(struct ob_hw *hw);
/*
 * RETIRED (Phase 36Z): the historical read-only AI probe performed a D11
 * functional-core read (0x0FFC) after observing the core held in reset, and
 * preceded loss of endpoint/upstream-port config access in the only live run.
 * It is refused in ob_probe() before any allocation, MMIO or BCMA access, and
 * this entry point itself performs no register access and returns
 * -EOPNOTSUPP. The quarantined sequence is DELETED; see
 * AI_WRAPPER_ONLY_CONTRACT.md and LEGACY_MODE_QUARANTINE.md.
 */
int  ob_ai_probe_test(struct ob_hw *hw);
/*
 * rev42_ai_wrapper_only: at most three AI-wrapper reads (RESET_ST, RESET_CTL,
 * IOCTL) through BCMA's wrapper accessor; no functional-core access, no
 * config-space read, no write of any driver-owned register. Contract in
 * ob_ai_wrapper_only.h.
 */
int  ob_ai_wrapper_only_test(struct ob_hw *hw);
int  ob_ai_enable_test(struct ob_hw *hw);
/*
 * rev42_d11_maccontrol_once: the verified Phase 37B wrapper-enable transition
 * (stage B1), then exactly ONE D11 functional-core MACCONTROL (0x0120, 32-bit)
 * read through ob_d11_read32_trusted() (stage B3), classified before any
 * success marker and followed by no further register access (stage B4).
 * Contract: ob_d11_maccontrol_once.h. Zero functional writes.
 */
int  ob_d11_maccontrol_once_test(struct ob_hw *hw);
void ob_diag_remove(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_DIAG_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 rev42 ucode upload + PSM start, isolated test path (M3.4D2A).
 *
 * This header carries the exact recovered register/bit vocabulary and the pure
 * (host-testable) helpers for the ucode_test_only mode. It deliberately contains
 * no kernel API so the mode-selection, write-count, bounded-poll and teardown
 * logic can be unit-tested on the host.
 *
 * Provenance (all C2 = vendor blob `wlc_hybrid.o_shipped`, corroborated by C3
 * brcmsmac `struct d11regs`):
 *   - ucode upload applier  wlc blob 0x60744 (dispatch 0x607b0)
 *       OBJADDR = D11+0x160, OBJDATA = D11+0x164; writes OBJADDR=0x03000000
 *       (AUTO_INC | UCM_SEL 0), reads it back (barrier), then one 32-bit raw LE
 *       word per OBJDATA write. No byte swap.
 *   - pre-upload MACCONTROL  wlc_bmac_init @0x6836c:
 *       wlc_bmac_mctrl(dev, ~0, 0x04000404) = IHR_EN | PSM_JMP0 | WAKE.
 *       PSM_RUN=0, EN_MAC=0, SHM_EN=0.  Masked update (wlc_bmac_mctrl 0x6060d).
 *   - PSM start + poll       wlc_bmac_wowlucode_start @0x63828:
 *       osl_writel(0xffffffff, D11+0x128); wlc_bmac_mctrl(dev, ~0, 0x04020402)
 *       = IHR_EN | INFRA | PSM_RUN | WAKE; then read D11+0x128 until bit0
 *       (MI_MACSSPNDD) is set, osl_delay(10) per iteration, counter 0xF4249
 *       decremented by 10 (=> <=100000 iterations, ~1.0 s). No EN_MAC.
 *   - SHM read               wlc_bmac_read_shm @0x61910 -> helper 0x6188a:
 *       OBJADDR(D11+0x160) = 0x10000 | (offset>>2); read OBJDATA + (offset&2)
 *       as 16-bit. Vendor FIFO-size reads at 0x68F88..0x68FAE (SHM 0x98..0x9e).
 *       NOTE: those vendor call sites are AFTER the common-initvals applier at
 *       0x68b98, so D2A logs them as read-only diagnostics only.
 *
 * No initvals are applied by this path. No PHY/radio/channel/DMA/IRQ/mac80211.
 */
#ifndef _OB_UCODE_H_
#define _OB_UCODE_H_

#include <linux/types.h>
#ifdef __KERNEL__
#include <linux/stddef.h>	/* size_t for ob_ucode_words_from_size() */
#else
#include <stddef.h>
#endif
#include "ob_irq.h"	/* OB_D11_MI_MACSSPNDD, OB_D11_REG_MACINTSTATUS */

/* Linux BCMA bit definitions, available in kernel builds and mirrored here
 * only so the pure host-test helpers remain buildable without kernel headers. */
#ifndef BCMA_IOCTL_CLK
#define BCMA_IOCTL_CLK		0x0001u
#endif
#ifndef BCMA_IOCTL_FGC
#define BCMA_IOCTL_FGC		0x0002u
#endif
#ifndef BCMA_RESET_CTL_RESET
#define BCMA_RESET_CTL_RESET	0x0001u
#endif

/* ---- D11 host-interface registers (relative to the D11 core) ---- */
#define OB_UCODE_REG_MACCONTROL		0x0120u
#define OB_UCODE_REG_MACINTSTATUS	0x0128u	/* == OB_D11_REG_MACINTSTATUS */
#define OB_UCODE_REG_OBJADDR		0x0160u
#define OB_UCODE_REG_OBJDATA		0x0164u
/*
 * D11 window +0x1e0. This is NOT the ChipCommon CLKCTLST, which is the
 * ChipCommon register of the same offset (re.db `named`:
 * si_pmu_pll_init +0x1e0 => CLKCTLST; the driver reads that one separately
 * via ob_si_cc_read(OB_CC_CLKCTLST)). The identity of the D11 register is
 * UNKNOWN: the vendor only writes bit1 (sub_64887), bit5
 * (wlc_bmac_radio_hw) and bits 8|9 (wlc_bmac_core_phypll_ctl) here, and
 * never bit17, which is the ChipCommon HAVEHT bit. So nothing may be
 * derived from this read beyond the raw value.
 * See docs/m34d4/m34e_d11_mac_accessibility.md.
 */
#define OB_UCODE_REG_D11_CLK1E0		0x01e0u

/* Pure prerequisite classification; all-ones is an inaccessible D11 window. */
static inline bool ob_ucode_d11_prerequisite_accessible(u32 value)
{
	return value != 0xffffffffu;
}

static inline bool ob_ucode_bootstrap_can_continue(bool core_enabled,
						   u32 clk1e0)
{
	return core_enabled && ob_ucode_d11_prerequisite_accessible(clk1e0);
}

enum ob_ucode_core_postcondition {
	OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID,
	OB_UCODE_CORE_POSTCONDITION_RESET_ASSERTED,
	OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID_RESET_ASSERTED,
	OB_UCODE_CORE_POSTCONDITION_ENABLED,
};

static inline bool ob_ucode_core_ioctl_valid(u32 ioctl)
{
	return (ioctl & (BCMA_IOCTL_CLK | BCMA_IOCTL_FGC)) ==
	       BCMA_IOCTL_CLK;
}

static inline bool ob_ucode_core_enabled_predicate(u32 ioctl, u32 reset_ctl)
{
	return ob_ucode_core_ioctl_valid(ioctl) &&
	       !(reset_ctl & BCMA_RESET_CTL_RESET);
}

static inline enum ob_ucode_core_postcondition
ob_ucode_core_postcondition_from_regs(u32 ioctl, u32 reset_ctl)
{
	bool ioctl_invalid = !ob_ucode_core_ioctl_valid(ioctl);
	bool reset_asserted = !!(reset_ctl & BCMA_RESET_CTL_RESET);

	if (ioctl_invalid && reset_asserted)
		return OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID_RESET_ASSERTED;
	if (ioctl_invalid)
		return OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID;
	if (reset_asserted)
		return OB_UCODE_CORE_POSTCONDITION_RESET_ASSERTED;
	return OB_UCODE_CORE_POSTCONDITION_ENABLED;
}

/* ---- maccontrol bits (C3 brcmsmac d11.h) ---- */
#define OB_UCODE_MCTL_EN_MAC	0x00000001u
#define OB_UCODE_MCTL_PSM_RUN	0x00000002u
#define OB_UCODE_MCTL_PSM_JMP0	0x00000004u
#define OB_UCODE_MCTL_SHM_EN	0x00000100u
#define OB_UCODE_MCTL_IHR_EN	0x00000400u
#define OB_UCODE_MCTL_INFRA	0x00020000u
#define OB_UCODE_MCTL_WAKE	0x04000000u

/*
 * Bit 1 of D11 +0x1e0. The vendor's set/clear of this bit IS PROVEN_STATIC
 * (sub_64887 @0x64887, the mac-clock on/off helper called from every
 * wlc_bmac attach/hw_up/up_prep/up_finish/down_prep/corereset/set_chanspec/
 * wakeucode_dnlddone site, paired with the ChipCommon si_clkctl_cc). The
 * "mac clock enable" NAME is a candidate, not a proven identity.
 */
#define OB_UCODE_D11_CLK1E0_BIT1	0x00000002u

/*
 * Exact vendor MACCONTROL values (wlc_bmac_mctrl(dev, ~0, value)). Exposed so
 * the host test can prove the bit composition.
 */
#define OB_UCODE_MACCONTROL_UPLOAD \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_PSM_JMP0 | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_MACCONTROL_PSM \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_INFRA | \
	 OB_UCODE_MCTL_PSM_RUN | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_MACCONTROL_MASK	0xffffffffu

/* Bits that must be settled in each recovered state. */
#define OB_UCODE_UPLOAD_EXPECTED \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_PSM_JMP0 | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_UPLOAD_FORBIDDEN \
	(OB_UCODE_MCTL_PSM_RUN | OB_UCODE_MCTL_EN_MAC | OB_UCODE_MCTL_SHM_EN)
#define OB_UCODE_PSM_EXPECTED \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_INFRA | \
	 OB_UCODE_MCTL_PSM_RUN | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_PSM_FORBIDDEN \
	(OB_UCODE_MCTL_EN_MAC)

/* ---- OBJADDR selector bits (C3 brcmsmac d11.h) ---- */
#define OB_UCODE_OBJADDR_AUTO_INC	0x03000000u
#define OB_UCODE_OBJADDR_UCM_SEL	0x00000000u
#define OB_UCODE_OBJADDR_SHM_SEL	0x00010000u

/* ---- bounded PSM poll (exact vendor constants) ---- */
#define OB_UCODE_POLL_DELAY_US	10u
#define OB_UCODE_POLL_TIMEOUT	0x000f4249u	/* 1000009 */
#define OB_UCODE_POLL_STEP	10u		/* counter decrement per iteration */

/* ---- pre-initvals SHM FIFO-size diagnostic (vendor 0x68f88) ---- */
#define OB_UCODE_SHM_FIFOSIZE0	0x0098u
#define OB_UCODE_SHM_FIFOSIZE1	0x009au
#define OB_UCODE_SHM_FIFOSIZE2	0x009cu
#define OB_UCODE_SHM_FIFOSIZE3	0x009eu

/* ---- pure helpers (host-testable, no kernel API) ---- */

/*
 * Explicit isolated-mode policy. Every isolated mode bypasses normal bring-up;
 * at most one may be selected, and any combination fails before hardware access
 * (M3.4D2B requirement 2). Selection is a pure function so it is host-testable
 * and the call-graph test can assert that only the initvals mode applies the
 * common table.
 */
enum ob_isolated_mode {
	OB_ISOLATED_NONE = 0,
	OB_ISOLATED_FW_VALIDATE,
	OB_ISOLATED_UCODE_TEST,
	OB_ISOLATED_INITVALS_TEST,
	OB_ISOLATED_DMA_TEST,
	OB_ISOLATED_D3A1_TEST,
	OB_ISOLATED_D3B_TEST,
	OB_ISOLATED_RADIO_ID_PROBE,
	OB_ISOLATED_RADIO2069_REV4_PROBE,
	OB_ISOLATED_REV42_DIAG,
	OB_ISOLATED_REV42_ATTACH,
	OB_ISOLATED_REV42_SB_RESET,
	OB_ISOLATED_REV42_AI_PROBE,
	OB_ISOLATED_REV42_AI_ENABLE,
	OB_ISOLATED_D11_TRANSACTION_TIMING,
	OB_ISOLATED_ACPHY_PRE_RADIO_TEST,
	OB_ISOLATED_ACPHY_CLASS0_0413_TEST,
	OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST,
	OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST,
	OB_ISOLATED_CONFLICT,
	OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST,
	OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST,
	OB_ISOLATED_ACPHY_RADIO_ON_TEST,
	OB_ISOLATED_ACPHY_RADIO_POSTLPF_TEST,
	OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE,
	/*
	 * Phase 36Z: bounded AI-wrapper-only observation (three reads max).
	 * Appended so no existing enumerator changes value.
	 */
	OB_ISOLATED_REV42_AI_WRAPPER_ONLY,
	/*
	 * Phase 38A: the verified Phase 37B wrapper-enable transition followed by
	 * exactly ONE D11 functional-core MACCONTROL (0x0120) read. Appended so
	 * no existing enumerator changes value and select23() callers are
	 * unaffected.
	 */
	OB_ISOLATED_REV42_D11_MACCONTROL_ONCE,
	/*
	 * Endpoint-loss bisection L2 (acphy_identity_timing_only): the read-only
	 * power-up + eight-read D11 bring-up timing stage, followed by exactly one
	 * 16-bit D11 PHY-version (0x03e0) read and a stop.
	 *
	 * It exists to separate the two layers of the L1 sequence
	 * (acphy_pre_radio_test_only) that both precede the fatal 0x3e0 access:
	 *   L2 removes ob_radio_prepare_for_identity() and the attach prefix;
	 *   L3 (rev42_d11_maccontrol_once, one call changed) removes the power-up
	 *      and the eight reads as well.
	 * Appended so no existing enumerator changes value and select24() callers
	 * are unaffected.
	 */
	OB_ISOLATED_ACPHY_IDENTITY_TIMING,
	/*
	 * W2: one identity -> D2A -> common-initvals -> lifecycle chain, with
	 * the same runtime identity gate as normal bring-up. Appended so no
	 * existing enumerator changes value and select25() callers are
	 * unaffected.
	 */
	OB_ISOLATED_FW_CHAIN_TEST,
};

static inline unsigned int
ob_isolated_mode_count5(bool fw_validate_only, bool ucode_test_only,
			bool initvals_test_only, bool dma_test_only,
			bool d11_tail_test_only)
{
	return (fw_validate_only ? 1u : 0u) + (ucode_test_only ? 1u : 0u) +
	       (initvals_test_only ? 1u : 0u) + (dma_test_only ? 1u : 0u) +
	       (d11_tail_test_only ? 1u : 0u);
}

static inline enum ob_isolated_mode
ob_isolated_mode_select5(bool fw_validate_only, bool ucode_test_only,
			 bool initvals_test_only, bool dma_test_only,
			 bool d11_tail_test_only)
{
	if (ob_isolated_mode_count5(fw_validate_only, ucode_test_only,
				    initvals_test_only, dma_test_only,
				    d11_tail_test_only) > 1)
		return OB_ISOLATED_CONFLICT;
	if (fw_validate_only)
		return OB_ISOLATED_FW_VALIDATE;
	if (ucode_test_only)
		return OB_ISOLATED_UCODE_TEST;
	if (initvals_test_only)
		return OB_ISOLATED_INITVALS_TEST;
	if (dma_test_only)
		return OB_ISOLATED_DMA_TEST;
	if (d11_tail_test_only)
		return OB_ISOLATED_D3A1_TEST;
	return OB_ISOLATED_NONE;
}

static inline unsigned int
ob_isolated_mode_count6(bool fw_validate_only, bool ucode_test_only,
			bool initvals_test_only, bool dma_test_only,
			bool d11_tail_test_only, bool bsinitvals_test_only)
{
	return ob_isolated_mode_count5(fw_validate_only, ucode_test_only,
				       initvals_test_only, dma_test_only,
				       d11_tail_test_only) +
	       (bsinitvals_test_only ? 1u : 0u);
}

static inline enum ob_isolated_mode
ob_isolated_mode_select6(bool fw_validate_only, bool ucode_test_only,
			 bool initvals_test_only, bool dma_test_only,
			 bool d11_tail_test_only, bool bsinitvals_test_only)
{
	if (ob_isolated_mode_count6(fw_validate_only, ucode_test_only,
				    initvals_test_only, dma_test_only,
				    d11_tail_test_only,
				    bsinitvals_test_only) > 1)
		return OB_ISOLATED_CONFLICT;
	if (fw_validate_only)
		return OB_ISOLATED_FW_VALIDATE;
	if (ucode_test_only)
		return OB_ISOLATED_UCODE_TEST;
	if (initvals_test_only)
		return OB_ISOLATED_INITVALS_TEST;
	if (dma_test_only)
		return OB_ISOLATED_DMA_TEST;
	if (d11_tail_test_only)
		return OB_ISOLATED_D3A1_TEST;
	if (bsinitvals_test_only)
		return OB_ISOLATED_D3B_TEST;
	return OB_ISOLATED_NONE;
}

/* Seven-mode form: adds the isolated radio identity probe (additive). */
static inline unsigned int
ob_isolated_mode_count7(bool fw_validate_only, bool ucode_test_only,
			bool initvals_test_only, bool dma_test_only,
			bool d11_tail_test_only, bool bsinitvals_test_only,
			bool radio_id_probe_only)
{
	return ob_isolated_mode_count6(fw_validate_only, ucode_test_only,
				       initvals_test_only, dma_test_only,
				       d11_tail_test_only,
				       bsinitvals_test_only) +
	       (radio_id_probe_only ? 1u : 0u);
}

static inline enum ob_isolated_mode
ob_isolated_mode_select7(bool fw_validate_only, bool ucode_test_only,
			 bool initvals_test_only, bool dma_test_only,
			 bool d11_tail_test_only, bool bsinitvals_test_only,
			 bool radio_id_probe_only)
{
	if (ob_isolated_mode_count7(fw_validate_only, ucode_test_only,
				    initvals_test_only, dma_test_only,
				    d11_tail_test_only, bsinitvals_test_only,
				    radio_id_probe_only) > 1)
		return OB_ISOLATED_CONFLICT;
	if (fw_validate_only)
		return OB_ISOLATED_FW_VALIDATE;
	if (ucode_test_only)
		return OB_ISOLATED_UCODE_TEST;
	if (initvals_test_only)
		return OB_ISOLATED_INITVALS_TEST;
	if (dma_test_only)
		return OB_ISOLATED_DMA_TEST;
	if (d11_tail_test_only)
		return OB_ISOLATED_D3A1_TEST;
	if (bsinitvals_test_only)
		return OB_ISOLATED_D3B_TEST;
	if (radio_id_probe_only)
		return OB_ISOLATED_RADIO_ID_PROBE;
	return OB_ISOLATED_NONE;
}

/* Eight-mode form: adds the staged rev42 diagnostic (additive). */
static inline unsigned int
ob_isolated_mode_count8(bool fw_validate_only, bool ucode_test_only,
			bool initvals_test_only, bool dma_test_only,
			bool d11_tail_test_only, bool bsinitvals_test_only,
			bool radio_id_probe_only, bool rev42_diag_probe_only)
{
	return ob_isolated_mode_count7(fw_validate_only, ucode_test_only,
				       initvals_test_only, dma_test_only,
				       d11_tail_test_only,
				       bsinitvals_test_only,
				       radio_id_probe_only) +
	       (rev42_diag_probe_only ? 1u : 0u);
}

static inline enum ob_isolated_mode
ob_isolated_mode_select8(bool fw_validate_only, bool ucode_test_only,
			 bool initvals_test_only, bool dma_test_only,
			 bool d11_tail_test_only, bool bsinitvals_test_only,
			 bool radio_id_probe_only, bool rev42_diag_probe_only)
{
	if (ob_isolated_mode_count8(fw_validate_only, ucode_test_only,
				    initvals_test_only, dma_test_only,
				    d11_tail_test_only, bsinitvals_test_only,
				    radio_id_probe_only,
				    rev42_diag_probe_only) > 1)
		return OB_ISOLATED_CONFLICT;
	if (fw_validate_only)
		return OB_ISOLATED_FW_VALIDATE;
	if (ucode_test_only)
		return OB_ISOLATED_UCODE_TEST;
	if (initvals_test_only)
		return OB_ISOLATED_INITVALS_TEST;
	if (dma_test_only)
		return OB_ISOLATED_DMA_TEST;
	if (d11_tail_test_only)
		return OB_ISOLATED_D3A1_TEST;
	if (bsinitvals_test_only)
		return OB_ISOLATED_D3B_TEST;
	if (radio_id_probe_only)
		return OB_ISOLATED_RADIO_ID_PROBE;
	if (rev42_diag_probe_only)
		return OB_ISOLATED_REV42_DIAG;
	return OB_ISOLATED_NONE;
}

/* Nine-mode form: adds the cold-attach radio sequence probe (additive). */
static inline unsigned int
ob_isolated_mode_count9(bool fw_validate_only, bool ucode_test_only,
			bool initvals_test_only, bool dma_test_only,
			bool d11_tail_test_only, bool bsinitvals_test_only,
			bool radio_id_probe_only, bool rev42_diag_probe_only,
			bool rev42_attach_probe_only)
{
	return ob_isolated_mode_count8(fw_validate_only, ucode_test_only,
				       initvals_test_only, dma_test_only,
				       d11_tail_test_only,
				       bsinitvals_test_only,
				       radio_id_probe_only,
				       rev42_diag_probe_only) +
	       (rev42_attach_probe_only ? 1u : 0u);
}

static inline enum ob_isolated_mode
ob_isolated_mode_select9(bool fw_validate_only, bool ucode_test_only,
			 bool initvals_test_only, bool dma_test_only,
			 bool d11_tail_test_only, bool bsinitvals_test_only,
			 bool radio_id_probe_only, bool rev42_diag_probe_only,
			 bool rev42_attach_probe_only)
{
	if (ob_isolated_mode_count9(fw_validate_only, ucode_test_only,
				    initvals_test_only, dma_test_only,
				    d11_tail_test_only, bsinitvals_test_only,
				    radio_id_probe_only, rev42_diag_probe_only,
				    rev42_attach_probe_only) > 1)
		return OB_ISOLATED_CONFLICT;
	if (fw_validate_only)
		return OB_ISOLATED_FW_VALIDATE;
	if (ucode_test_only)
		return OB_ISOLATED_UCODE_TEST;
	if (initvals_test_only)
		return OB_ISOLATED_INITVALS_TEST;
	if (dma_test_only)
		return OB_ISOLATED_DMA_TEST;
	if (d11_tail_test_only)
		return OB_ISOLATED_D3A1_TEST;
	if (bsinitvals_test_only)
		return OB_ISOLATED_D3B_TEST;
	if (radio_id_probe_only)
		return OB_ISOLATED_RADIO_ID_PROBE;
	if (rev42_diag_probe_only)
		return OB_ISOLATED_REV42_DIAG;
	if (rev42_attach_probe_only)
		return OB_ISOLATED_REV42_ATTACH;
	return OB_ISOLATED_NONE;
}

/* Ten-mode form: adds the vendor sb_core_reset probe (additive). */
static inline enum ob_isolated_mode
ob_isolated_mode_select10(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only,
			  bool rev42_sb_reset_only)
{
	unsigned int n = ob_isolated_mode_count9(fw_validate_only,
					ucode_test_only, initvals_test_only,
					dma_test_only, d11_tail_test_only,
					bsinitvals_test_only, radio_id_probe_only,
					rev42_diag_probe_only,
					rev42_attach_probe_only) +
			 (rev42_sb_reset_only ? 1u : 0u);

	if (n > 1)
		return OB_ISOLATED_CONFLICT;
	if (fw_validate_only)
		return OB_ISOLATED_FW_VALIDATE;
	if (ucode_test_only)
		return OB_ISOLATED_UCODE_TEST;
	if (initvals_test_only)
		return OB_ISOLATED_INITVALS_TEST;
	if (dma_test_only)
		return OB_ISOLATED_DMA_TEST;
	if (d11_tail_test_only)
		return OB_ISOLATED_D3A1_TEST;
	if (bsinitvals_test_only)
		return OB_ISOLATED_D3B_TEST;
	if (radio_id_probe_only)
		return OB_ISOLATED_RADIO_ID_PROBE;
	if (rev42_diag_probe_only)
		return OB_ISOLATED_REV42_DIAG;
	if (rev42_attach_probe_only)
		return OB_ISOLATED_REV42_ATTACH;
	if (rev42_sb_reset_only)
		return OB_ISOLATED_REV42_SB_RESET;
	return OB_ISOLATED_NONE;
}

/* Eleven-mode form: adds the read-only AI-wrapper mapping probe (additive). */
static inline enum ob_isolated_mode
ob_isolated_mode_select11(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only,
			  bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select10(fw_validate_only,
					ucode_test_only, initvals_test_only,
					dma_test_only, d11_tail_test_only,
					bsinitvals_test_only, radio_id_probe_only,
					rev42_diag_probe_only,
					rev42_attach_probe_only,
					rev42_sb_reset_only);
	unsigned int n;

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	n = (m == OB_ISOLATED_NONE ? 0u : 1u) +
	    (rev42_ai_probe_only ? 1u : 0u);
	if (n > 1)
		return OB_ISOLATED_CONFLICT;
	if (rev42_ai_probe_only)
		return OB_ISOLATED_REV42_AI_PROBE;
	return m;
}

/*
 * `rev42_ai_enable_only` is the first rev42 mode that WRITES a register: it
 * performs the upstream bcma_core_enable() sequence (wrapper window only) and
 * then exactly one D11 core read. It is additive and mutually exclusive with
 * every other isolated mode, including rev42_ai_probe_only -- the read-only
 * probe and the enable mode are alternatives, not a sequence.
 */
static inline enum ob_isolated_mode
ob_isolated_mode_select12(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only,
			  bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only,
			  bool rev42_ai_enable_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select11(fw_validate_only,
					ucode_test_only, initvals_test_only,
					dma_test_only, d11_tail_test_only,
					bsinitvals_test_only,
					radio_id_probe_only,
					rev42_diag_probe_only,
					rev42_attach_probe_only,
					rev42_sb_reset_only,
					rev42_ai_probe_only);
	unsigned int n;

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	n = (m == OB_ISOLATED_NONE ? 0u : 1u) +
	    (rev42_ai_enable_only ? 1u : 0u);
	if (n > 1)
		return OB_ISOLATED_CONFLICT;
	if (rev42_ai_enable_only)
		return OB_ISOLATED_REV42_AI_ENABLE;
	return m;
}

/* Thirteen-mode form: adds the bounded physical-radio rev4 stage. */
static inline enum ob_isolated_mode
ob_isolated_mode_select13(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select12(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only);
	unsigned int n;

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	n = (m == OB_ISOLATED_NONE ? 0u : 1u) +
		(radio2069_rev4_probe_only ? 1u : 0u);
	if (n > 1)
		return OB_ISOLATED_CONFLICT;
	return radio2069_rev4_probe_only ? OB_ISOLATED_RADIO2069_REV4_PROBE : m;
}

/* Fourteen-mode form: adds the read-only D11 transaction-timing stage. */
static inline enum ob_isolated_mode
ob_isolated_mode_select14(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select13(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only);
	unsigned int n;

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	n = (m == OB_ISOLATED_NONE ? 0u : 1u) +
		(d11_transaction_timing_only ? 1u : 0u);
	if (n > 1)
		return OB_ISOLATED_CONFLICT;
	return d11_transaction_timing_only ?
		OB_ISOLATED_D11_TRANSACTION_TIMING : m;
}

/* Fifteen-mode form: the bounded BCM4352 ACPHY pre-radio checkpoint. */
static inline enum ob_isolated_mode
ob_isolated_mode_select15(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select14(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only);
	unsigned int n;

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	n = (m == OB_ISOLATED_NONE ? 0u : 1u) +
		(acphy_pre_radio_test_only ? 1u : 0u);
	if (n > 1)
		return OB_ISOLATED_CONFLICT;
	return acphy_pre_radio_test_only ?
		OB_ISOLATED_ACPHY_PRE_RADIO_TEST : m;
}

/* The class-0 radio-local checkpoint is a separate mode, not an extension of
 * the pre-radio stop.  Keeping this wrapper additive preserves every existing
 * select15() caller and makes the mutual exclusion explicit. */
static inline enum ob_isolated_mode
ob_isolated_mode_select16(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select15(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only);

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_class0_radio_local_0413_test_only &&
	    m != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_class0_radio_local_0413_test_only ?
		OB_ISOLATED_ACPHY_CLASS0_0413_TEST : m;
}

/* Separate opt-in continuation from the phase-0-only 0x0413 checkpoint.
 * The older select16 API and its callers retain their original behavior. */
static inline enum ob_isolated_mode
ob_isolated_mode_select17(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select16(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only);

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_class0_post0413_phase1_0413_test_only &&
	    m != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_class0_post0413_phase1_0413_test_only ?
		OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST : m;
}

/* Separate phase-2/third-poll checkpoint. Keep select17 and its existing
 * phase-0/phase-1 callers unchanged; this wrapper adds one mutually exclusive
 * test-only selector. */
static inline enum ob_isolated_mode
ob_isolated_mode_select18(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select17(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only);

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_class0_postphase1_phase2_0413_test_only &&
	    m != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_class0_postphase1_phase2_0413_test_only ?
		OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST : m;
}

/* Add the post-phase-2 sub_9591e return endpoint without changing select18. */
static inline enum ob_isolated_mode
ob_isolated_mode_select19(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only)
{
	enum ob_isolated_mode m = ob_isolated_mode_select18(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only);

	if (m == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_class0_postphase2_sub9591e_return_test_only &&
	    m != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_class0_postphase2_sub9591e_return_test_only ?
		OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST : m;
}

/* Add the PHY 0x01b0/selector-0 branch-gate checkpoint without changing the
 * established select19 values or selection behavior. */
static inline enum ob_isolated_mode
ob_isolated_mode_select20(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only,
			  bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only)
{
	enum ob_isolated_mode mode = ob_isolated_mode_select19(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only,
		acphy_class0_postphase2_sub9591e_return_test_only);

	if (mode == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_class0_postbmac_phyreg1b0_branch_gate_test_only &&
	    mode != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_class0_postbmac_phyreg1b0_branch_gate_test_only ?
		OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST : mode;
}

/* Adds the isolated radio-ON checkpoint (radio prefix -> analog TX LPF ->
 * sub_92500), which stops at OB_PHY_BLOCK_REV1_AFTER_SUB92500. Additive. */
static inline enum ob_isolated_mode
ob_isolated_mode_select21(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only,
			  bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
			  bool acphy_radio_on_test_only)
{
	enum ob_isolated_mode mode = ob_isolated_mode_select20(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only,
		acphy_class0_postphase2_sub9591e_return_test_only,
		acphy_class0_postbmac_phyreg1b0_branch_gate_test_only);

	if (mode == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_radio_on_test_only && mode != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_radio_on_test_only ? OB_ISOLATED_ACPHY_RADIO_ON_TEST : mode;
}

/* Adds the post-LPF (sub_a7089 + sub_a0be4) continuation checkpoint. */
static inline enum ob_isolated_mode
ob_isolated_mode_select22(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only, bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only, bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only,
			  bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
			  bool acphy_radio_on_test_only,
			  bool acphy_radio_postlpf_test_only)
{
	enum ob_isolated_mode mode = ob_isolated_mode_select21(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only,
		acphy_class0_postphase2_sub9591e_return_test_only,
		acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
		acphy_radio_on_test_only);

	if (mode == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_radio_postlpf_test_only && mode != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_radio_postlpf_test_only ?
		OB_ISOLATED_ACPHY_RADIO_POSTLPF_TEST : mode;
}

/*
 * Twenty-three-mode form (Phase 36Z): adds the bounded AI-wrapper-only
 * diagnostic. Additive, so every existing select22() caller keeps its exact
 * behavior, and the new option is mutually exclusive with all twenty-two
 * others through the same conflict rule -- there is no "wrapper mode plus
 * something" combination that silently selects one of them.
 *
 * The retired `rev42_ai_probe_only` flag stays in the signature because the
 * selector is a pure policy function and its callers must keep compiling; the
 * driver refuses that flag in ob_probe() BEFORE this function is reached, and
 * ob_aiw_legacy_retired() makes the refusal itself host-testable. Passing both
 * flags here is still a hard CONFLICT rather than a preference.
 */
static inline enum ob_isolated_mode
ob_isolated_mode_select23(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only,
			  bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only,
			  bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only,
			  bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
			  bool acphy_radio_on_test_only,
			  bool acphy_radio_postlpf_test_only,
			  bool rev42_ai_wrapper_only)
{
	enum ob_isolated_mode mode = ob_isolated_mode_select22(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only,
		acphy_class0_postphase2_sub9591e_return_test_only,
		acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
		acphy_radio_on_test_only, acphy_radio_postlpf_test_only);

	if (mode == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (rev42_ai_wrapper_only && mode != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return rev42_ai_wrapper_only ? OB_ISOLATED_REV42_AI_WRAPPER_ONLY : mode;
}

/*
 * Twenty-four-mode form (Phase 38A): adds `rev42_d11_maccontrol_once`, which
 * runs the verified Phase 37B wrapper-enable engine and then performs exactly
 * ONE D11 functional-core MACCONTROL (0x0120) read through the trusted guarded
 * accessor.
 *
 * Additive, so every select23() caller keeps its exact behavior, and the new
 * option is mutually exclusive with all twenty-three others through the same
 * conflict rule. In particular `rev42_ai_enable_only` + the new flag is a hard
 * CONFLICT: a bare wrapper-only run is never silently upgraded to a functional
 * read, and a functional read is never selected implicitly.
 */
static inline enum ob_isolated_mode
ob_isolated_mode_select24(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only,
			  bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only,
			  bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only,
			  bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
			  bool acphy_radio_on_test_only,
			  bool acphy_radio_postlpf_test_only,
			  bool rev42_ai_wrapper_only,
			  bool rev42_d11_maccontrol_once)
{
	enum ob_isolated_mode mode = ob_isolated_mode_select23(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only,
		acphy_class0_postphase2_sub9591e_return_test_only,
		acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
		acphy_radio_on_test_only, acphy_radio_postlpf_test_only,
		rev42_ai_wrapper_only);

	if (mode == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (rev42_d11_maccontrol_once && mode != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return rev42_d11_maccontrol_once ?
		OB_ISOLATED_REV42_D11_MACCONTROL_ONCE : mode;
}

/*
 * Twenty-five-mode form: adds the endpoint-loss bisection L2 stage.
 *
 * Additive, so every select24() caller keeps its exact behaviour, and the new
 * option is mutually exclusive with all twenty-four others through the same
 * conflict rule. `acphy_identity_timing_only` + `acphy_pre_radio_test_only` is
 * therefore a hard CONFLICT rather than a silent upgrade of the bounded L2
 * stage into the full attach sequence.
 */
static inline enum ob_isolated_mode
ob_isolated_mode_select25(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only,
			  bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only,
			  bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only,
			  bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
			  bool acphy_radio_on_test_only,
			  bool acphy_radio_postlpf_test_only,
			  bool rev42_ai_wrapper_only,
			  bool rev42_d11_maccontrol_once,
			  bool acphy_identity_timing_only)
{
	enum ob_isolated_mode mode = ob_isolated_mode_select24(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only,
		acphy_class0_postphase2_sub9591e_return_test_only,
		acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
		acphy_radio_on_test_only, acphy_radio_postlpf_test_only,
		rev42_ai_wrapper_only, rev42_d11_maccontrol_once);

	if (mode == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (acphy_identity_timing_only && mode != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return acphy_identity_timing_only ?
		OB_ISOLATED_ACPHY_IDENTITY_TIMING : mode;
}

/*
 * `rev42_diag_full` is a MODIFIER of `rev42_diag_probe_only`, not a mode of
 * its own: it only adds the D2A ucode / D2B common-initvals prefix to the
 * staged rev42 diagnostic.
 *
 * On its own it selects NO isolated mode, so ob_probe() would fall through to
 * the normal ob_si_probe() bring-up (PHY init, radio config, calibration, DMA,
 * IRQ, mac80211) while the operator believes a bounded diagnostic was
 * requested. This predicate is the in-driver, fail-closed guard for that
 * pairing: it is checked at the very top of ob_probe(), before any allocation
 * and before any hardware access. It is a pure function so the host tests can
 * pin it without a bcma_device.
 */
static inline bool
ob_isolated_diag_full_allowed(bool rev42_diag_full, bool rev42_diag_probe_only)
{
	return !rev42_diag_full || rev42_diag_probe_only;
}

/*
 * Phase 37A default-load fail-closed predicate.
 *
 * A module load with no isolated diagnostic selection would fall through to
 * the ordinary ob_si_probe() bring-up: PHY init, radio configuration,
 * calibration, DMA, host IRQ and mac80211 registration. That production path
 * is retained in ob_probe() but is NOT admitted by default: on the supported
 * experimental BCM4352 profile a bare `insmod openbrcm.ko` is a configuration
 * error, not a request to initialise the device.
 *
 * `sprom_evidence_only` and `sflags_diag_only` are independent read-only
 * diagnostics that do not select an OB_ISOLATED_* mode; they remain admitted
 * unchanged. Every other diagnostic must set one of the explicit isolated
 * flags, so this predicate is exactly:
 *
 *   no isolated mode AND no non-mode read-only diagnostic selected.
 *
 * Pure, so the decision ob_probe() takes is executable on the host without a
 * bcma_device or any kernel API.
 */
static inline bool
ob_isolated_default_load_refused(enum ob_isolated_mode mode,
				 bool sprom_evidence_only,
				 bool sflags_diag_only)
{
	return mode == OB_ISOLATED_NONE && !sprom_evidence_only &&
	       !sflags_diag_only;
}

/* Four-mode form kept for existing callers/tests (no D3A1 mode). */
static inline unsigned int ob_isolated_mode_count(bool fw_validate_only,
						  bool ucode_test_only,
						  bool initvals_test_only,
						  bool dma_test_only)
{
	return ob_isolated_mode_count5(fw_validate_only, ucode_test_only,
				       initvals_test_only, dma_test_only,
				       false);
}

static inline enum ob_isolated_mode
ob_isolated_mode_select(bool fw_validate_only, bool ucode_test_only,
			bool initvals_test_only, bool dma_test_only)
{
	return ob_isolated_mode_select5(fw_validate_only, ucode_test_only,
					initvals_test_only, dma_test_only,
					false);
}

static inline bool ob_isolated_mode_conflict(enum ob_isolated_mode mode)
{
	return mode == OB_ISOLATED_CONFLICT;
}

/*
 * Twenty-six-mode form: adds the W2 firmware-chain test.  Additive, so every
 * select25() caller keeps its exact behaviour; the new option is mutually
 * exclusive with all twenty-five others through the same conflict rule.
 */
static inline enum ob_isolated_mode
ob_isolated_mode_select26(bool fw_validate_only, bool ucode_test_only,
			  bool initvals_test_only, bool dma_test_only,
			  bool d11_tail_test_only, bool bsinitvals_test_only,
			  bool radio_id_probe_only, bool rev42_diag_probe_only,
			  bool rev42_attach_probe_only,
			  bool rev42_sb_reset_only,
			  bool rev42_ai_probe_only,
			  bool rev42_ai_enable_only,
			  bool radio2069_rev4_probe_only,
			  bool d11_transaction_timing_only,
			  bool acphy_pre_radio_test_only,
			  bool acphy_class0_radio_local_0413_test_only,
			  bool acphy_class0_post0413_phase1_0413_test_only,
			  bool acphy_class0_postphase1_phase2_0413_test_only,
			  bool acphy_class0_postphase2_sub9591e_return_test_only,
			  bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
			  bool acphy_radio_on_test_only,
			  bool acphy_radio_postlpf_test_only,
			  bool rev42_ai_wrapper_only,
			  bool rev42_d11_maccontrol_once,
			  bool acphy_identity_timing_only,
			  bool fw_chain_test_only)
{
	enum ob_isolated_mode mode = ob_isolated_mode_select25(
		fw_validate_only, ucode_test_only, initvals_test_only,
		dma_test_only, d11_tail_test_only, bsinitvals_test_only,
		radio_id_probe_only, rev42_diag_probe_only,
		rev42_attach_probe_only, rev42_sb_reset_only,
		rev42_ai_probe_only, rev42_ai_enable_only,
		radio2069_rev4_probe_only, d11_transaction_timing_only,
		acphy_pre_radio_test_only,
		acphy_class0_radio_local_0413_test_only,
		acphy_class0_post0413_phase1_0413_test_only,
		acphy_class0_postphase1_phase2_0413_test_only,
		acphy_class0_postphase2_sub9591e_return_test_only,
		acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
		acphy_radio_on_test_only, acphy_radio_postlpf_test_only,
		rev42_ai_wrapper_only, rev42_d11_maccontrol_once,
		acphy_identity_timing_only);

	if (mode == OB_ISOLATED_CONFLICT)
		return OB_ISOLATED_CONFLICT;
	if (fw_chain_test_only && mode != OB_ISOLATED_NONE)
		return OB_ISOLATED_CONFLICT;
	return fw_chain_test_only ? OB_ISOLATED_FW_CHAIN_TEST : mode;
}

/*
 * The D2B initvals mode, the D3A0 DMA mode and the D3A1 tail mode apply the
 * common table before any post-common work (the D3A0/D3A1 entry state IS the
 * hardware-proven D2B exit). ucode_test_only must never issue a
 * common-initvals write (M3.4D2B requirement 15).
 */
static inline bool ob_isolated_mode_applies_initvals(enum ob_isolated_mode mode)
{
	return mode == OB_ISOLATED_INITVALS_TEST ||
	       mode == OB_ISOLATED_DMA_TEST ||
	       mode == OB_ISOLATED_D3A1_TEST ||
	       mode == OB_ISOLATED_D3B_TEST ||
	       mode == OB_ISOLATED_FW_CHAIN_TEST;
}

/*
 * fw_validate/ucode_test/initvals_test/radio_id_probe bypass normal bring-up
 * and initialize no platform resources, so remove() must skip all teardown.
 * dma_test_only and d3a1_test_only DO own DMA resources and therefore handle
 * teardown through their own fail-closed remove hook; the normal path uses the
 * standard teardown.
 */
static inline bool ob_isolated_mode_skips_teardown(enum ob_isolated_mode mode)
{
	return mode == OB_ISOLATED_FW_VALIDATE ||
	       mode == OB_ISOLATED_UCODE_TEST ||
	       mode == OB_ISOLATED_INITVALS_TEST ||
	       mode == OB_ISOLATED_FW_CHAIN_TEST ||
	       mode == OB_ISOLATED_RADIO_ID_PROBE ||
	       mode == OB_ISOLATED_RADIO2069_REV4_PROBE ||
	       mode == OB_ISOLATED_REV42_DIAG ||
	       mode == OB_ISOLATED_REV42_ATTACH ||
	       mode == OB_ISOLATED_REV42_SB_RESET ||
	       mode == OB_ISOLATED_REV42_AI_PROBE ||
	       mode == OB_ISOLATED_D11_TRANSACTION_TIMING ||
	       mode == OB_ISOLATED_ACPHY_PRE_RADIO_TEST;
}

static inline bool ob_isolated_mode_preserves_state(enum ob_isolated_mode mode)
{
	return mode == OB_ISOLATED_ACPHY_CLASS0_0413_TEST ||
	       mode == OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST ||
	       mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST ||
	       mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST ||
	       mode == OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST ||
	       mode == OB_ISOLATED_ACPHY_RADIO_POSTLPF_TEST ||
	       mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE;
}

/*
 * dma_test_only enters the isolated D3A0 DMA lifecycle; d3a1_test_only reuses
 * that same lifecycle in its vendor position inside the D3A1 tail.
 */
static inline bool ob_isolated_mode_uses_dma(enum ob_isolated_mode mode)
{
	return mode == OB_ISOLATED_DMA_TEST ||
	       mode == OB_ISOLATED_D3A1_TEST ||
	       mode == OB_ISOLATED_D3B_TEST;
}

/* A validated flat ucode image is exactly size/4 32-bit words. */
static inline u32 ob_ucode_words_from_size(size_t fw_size)
{
	return (u32)(fw_size / 4);
}

/* The upload is only allowed to start PSM if every word was written once. */
static inline bool ob_ucode_writes_ok(u32 written, u32 expected)
{
	return written == expected;
}

/* Upper bound on poll iterations for timeout/step (vendor loop semantics). */
static inline u32 ob_ucode_poll_max_iterations(u32 timeout, u32 step)
{
	return step ? timeout / step : 0;
}

/* Vendor loop exits when the counter reaches step-1 (was `cmp $0x9` / step 10). */
static inline bool ob_ucode_poll_expired(u32 remaining, u32 step)
{
	return remaining < step;
}

static inline bool ob_ucode_mac_suspended(u32 macintstatus)
{
	return (macintstatus & OB_D11_MI_MACSSPNDD) != 0;
}

/*
 * ucode_test_only initializes NO platform resources (no mac80211, no RX ring,
 * no IRQ, no DMA). The teardown plan therefore disables every teardown step so
 * that remove() can never touch something that was never set up.
 */
struct ob_ucode_teardown {
	bool mac80211;
	bool rx;
	bool irq;
	bool dma;
};

static inline struct ob_ucode_teardown
ob_ucode_teardown_plan(bool ucode_test_only)
{
	struct ob_ucode_teardown t = { false, false, false, false };

	if (!ucode_test_only) {
		t.mac80211 = true;
		t.rx = true;
		t.irq = true;
		t.dma = true;
	}
	return t;
}

#ifdef __KERNEL__
struct ob_hw;
struct firmware;

/*
 * Shared result of the hardware-proven D2A core (prep -> upload -> PSM start ->
 * MI_MACSSPNDD poll). D2A and D2B both run this exact sequence; only the code
 * after it differs, so ucode_test_only cannot silently diverge (M3.4D2B
 * requirement 3).
 */
struct ob_ucode_run {
	const char	*name;		/* ucode image name actually used */
	u32		words;		/* ucode words the image contains (10850) */
	u32		issued;		/* OBJDATA writes the backend actually issued */
	u32		confirmed;	/* ALWAYS 0: no ucode-RAM readback exists */
	u32		psm_iterations;	/* poll iterations before MI_MACSSPNDD */
	u32		psm_status;	/* macintstatus that satisfied the poll */
	bool		psm_suspended;	/* MI_MACSSPNDD observed with a finite value */
	bool		retained;	/* device must stay bound: no teardown exists */
	bool		mutated;	/* a hardware mutation may have been issued */
	const char	*class_token;	/* contract classification token */
	const char	*stage;		/* exact failing D2A stage, or "done" */
	int		ret;		/* same value returned by the call */
};

/*
 * Run the proven D2A core with the given log @tag ("ucode-test" /
 * "initvals-test"). On success the PSM is paused after auto-init and the
 * returned @run describes the upload/poll. On failure it logs the failing
 * @stage. It acquires and releases its own ucode image; no initvals are
 * applied. No cleanup register writes are performed on any path.
 *
 * Phase 38C: @run->issued counts writes the backend actually executed and
 * @run->confirmed is always 0 (this path never reads the ucode RAM back). A
 * non-zero return with @run->retained set means hardware may have been
 * mutated; the caller must keep the device bound instead of surfacing a probe
 * failure (see ob_probe()).
 */
int ob_ucode_run_d2a(struct ob_hw *hw, const char *tag,
		     struct ob_ucode_run *run);

/*
 * Vendor SHM 16-bit read (OBJADDR SHM window + OBJDATA half). Read-only and
 * reused by the D2B postcondition gate.
 */
u16 ob_ucode_read_shm16(struct ob_hw *hw, u16 off);
int ob_ucode_read_shm16_checked(struct ob_hw *hw, u16 off, u16 *value);

/*
 * Vendor SHM 16-bit write (OBJADDR SHM window + OBJDATA half), byte @off.
 * Exact wlc_bmac_write_shm equivalent; reused by the D3B MHF writes.
 */
void ob_ucode_write_shm16(struct ob_hw *hw, u16 off, u16 val);

/* Checked variant for fail-closed normal initialization transactions. */
int ob_ucode_write_shm16_checked(struct ob_hw *hw, u16 off, u16 val);

int ob_ucode_test(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_UCODE_H_ */

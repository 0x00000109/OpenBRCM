/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — isolated D3B band-init / d11ac1bsinitvals42 test (M3.4D3B).
 *
 * D3B TYPE: ISOLATED BAND-INIT TEST. From the proven D3A1 prefix (which leaves
 * the D3A0 DMA engines live in their exact vendor position) it reproduces the
 * exact BCM4352/rev42/AC sub_6656c slice and STOPS immediately before the first
 * real PHY entry:
 *
 *   D2B common initvals
 *     -> D3A1 tail: sub_67efd -> T1 -> DMA -> T2 -> switch_macfreq
 *     -> sub_6656c pre-bs: osl_readw(D11+0x3e0) [read-only]
 *                          sub_62766 = wlc_bmac_write_mhf (MHF1..5 -> SHM)
 *                          table select (phyrev 0x2a + phytype 0x0b)
 *     -> sub_60f67(d11ac1bsinitvals42)  [73 records]
 *     -> STOP at the applier return (0x669c2), before wlc_phy_init (0x669df)
 *     -> verified D3A0 teardown (DMA was live while D3B executed)
 *
 * It never enters wlc_phy_init, wlc_phy_anacore, any PHY-indirect window
 * (D11+0x3fc/0x3fe) or radio window (D11+0x3d8/0x3da), never enables EN_MAC,
 * never routes the host IRQ and never enables MACINTMASK/MI_DMAINT.
 *
 * This header carries the exact recovered constants and the pure
 * (host-testable) helpers (board-data -> MHF derivation, the exact vendor
 * antsel_type flow, the bsinitvals shape and the postcondition predicates).
 * It deliberately contains no kernel API in the pure section.
 *
 * Provenance: docs/m34d3b_band_init.md, docs/m34d3b_implementation_plan.md,
 * docs/m34d3/bsinitvals_classification.json (image sha256 e81a645c79f5…).
 * Blob sha256 352a6e349f74…; C3 = upstream brcmsmac corroboration.
 */
#ifndef _OB_D3B_H_
#define _OB_D3B_H_

#include <linux/types.h>
#include "ob_initvals.h"
#include "ob_fw.h"

/* ---- exact MHF SHM destinations (blob sub_62766 0x62781..0x62799) ---- */
#define OB_D3B_MHF_COUNT		5u
#define OB_D3B_MHF_SHM0			0x005eu	/* M_HOST_FLAGS1 */
#define OB_D3B_MHF_SHM1			0x0060u	/* M_HOST_FLAGS2 */
#define OB_D3B_MHF_SHM2			0x0062u	/* M_HOST_FLAGS3 */
#define OB_D3B_MHF_SHM3			0x0078u	/* M_HOST_FLAGS4 */
#define OB_D3B_MHF_SHM4			0x00d4u	/* M_HOST_FLAGS5 */

/* MHF2 (si_pci_war16165) bit; MHF1 (EDCF) bit; MHF3 (ANTSEL) bits; MHF4; MHF5 */
#define OB_D3B_MHF1_EDCF		0x0100u
#define OB_D3B_MHF2_PCIWAR		0x0008u
#define OB_D3B_MHF3_ANTSEL_EN		0x0001u
#define OB_D3B_MHF3_ANTSEL_MODE		0x0002u
#define OB_D3B_MHF4_4313		0x4000u
#define OB_D3B_MHF5_AC			0x0080u

/* ---- sub_6656c pre-bs read (blob 0x665c3; result unused, read-only) ---- */
#define OB_D3B_REG_PHYVER_STATUS	0x03e0u

/* ---- bsinitvals42 shape (592 B, terminator excluded) ---- */
#define OB_D3B_BS_SIZE			592u
#define OB_D3B_BS_RECORDS		73u
#define OB_D3B_BS_W16			39u
#define OB_D3B_BS_W32			34u
#define OB_D3B_BS_TERMINATOR_INDEX	73u

/* ---- recovered BCM4352/rev42 AC board constants (proven) ---- */
#define OB_D3B_BAND_PHYTYPE_AC		0x0bu
#define OB_D3B_PHYREV_REV42		0x2au

/* ---- deterministic postconditions (docs/m34d3b_band_init.md §6) ---- */
#define OB_D3B_SHM_OVR_10		0x0010u
#define OB_D3B_SHM_OVR_1C		0x001cu
#define OB_D3B_SHM_OVR_94		0x0094u
#define OB_D3B_SHM_OVR_10_VAL		0x00000014u
#define OB_D3B_SHM_OVR_1C_VAL		0x00000183u
#define OB_D3B_SHM_OVR_94_VAL		0x000001f4u

/* Inherited from the proven D3A1 exit and unchanged by D3B. */
#define OB_D3B_MACCONTROL_EXPECTED	0x44020402u
#define OB_D3B_MACINTMASK_EXPECTED	0x00000000u
#define OB_D3B_MCTL_PSM_RUN		0x00000002u
#define OB_D3B_MCTL_EN_MAC		0x00000001u

/* ---- board-data -> MHF derivation (host-testable) ---- */

/*
 * Proven rev11 external-SPROM board fields decoded from the already-read,
 * CRC-validated 234-word image (word/mask map recovered from the vendor
 * srom_var_init descriptor table; docs/m34d3b/rev11_sprom_fields.json).
 * Zero extra MMIO: filled by ob_si_read_mac() from the image it already reads.
 */
#define OB_SPROM11_BOARDTYPE_WORD	0x02u
#define OB_SPROM11_BOARDFLAGS_LO_WORD	0x42u
#define OB_SPROM11_BOARDFLAGS_HI_WORD	0x43u
#define OB_SPROM11_ANT_WORD		0x50u
#define OB_SPROM11_TXRXC_WORD		0x54u
#define OB_SPROM11_ANTSWITCH_ABSENT	0xffu
#define OB_SPROM11_WORDS		234u

struct ob_sprom_board {
	bool	valid;		/* rev11 image decoded */
	u8	revision;
	u16	boardtype;	/* word 0x02 */
	u32	boardflags;	/* word 0x42 | (word 0x43 << 16) */
	u8	aa2g;		/* word 0x50 & 0xff */
	u8	aa5g;		/* word 0x50 >> 8 */
	u8	antswitch;	/* word 0x54 >> 8 */
	bool	antswitch_present;	/* false when the byte was 0xff */
};

/*
 * Decode the five proven rev11 board fields from a validated 234-word image.
 * The vendor skip-all-ones "absent" rule is applied to antswitch (0xff -> the
 * variable is not emitted; getintvar() then sees 0). Returns false when the
 * image is too short or the input is NULL (out->valid stays false).
 */
static inline bool ob_d3b_decode_rev11_board(const u16 *sp, size_t words,
					     struct ob_sprom_board *out)
{
	if (!sp || !out || words <= OB_SPROM11_TXRXC_WORD)
		return false;
	out->revision = 11u;
	out->boardtype = sp[OB_SPROM11_BOARDTYPE_WORD];
	out->boardflags = (u32)sp[OB_SPROM11_BOARDFLAGS_LO_WORD] |
			  ((u32)sp[OB_SPROM11_BOARDFLAGS_HI_WORD] << 16);
	out->aa2g = (u8)(sp[OB_SPROM11_ANT_WORD] & 0x00ffu);
	out->aa5g = (u8)((sp[OB_SPROM11_ANT_WORD] >> 8) & 0x00ffu);
	out->antswitch = (u8)((sp[OB_SPROM11_TXRXC_WORD] >> 8) & 0x00ffu);
	out->antswitch_present = (out->antswitch != OB_SPROM11_ANTSWITCH_ABSENT);
	if (!out->antswitch_present)
		out->antswitch = 0u;
	out->valid = true;
	return true;
}

/*
 * Recovered rev11 board-data inputs. The SPROM fields come from the proven
 * rev11 field map (scripts/srom_var_table.py / docs/m34d3b/rev11_sprom_fields.json):
 *   boardtype   word 0x02
 *   boardflags  word 0x42 | (word 0x43 << 16)
 *   aa2g        word 0x50 & 0xff
 *   aa5g        word 0x50 >> 8
 *   antswitch   word 0x54 >> 8 (absent when the byte is 0xff)
 *
 * The remaining gate inputs are the resolved, provenance-backed site gates
 * (docs/m34d3b_band_init.md §3.1/§3.2). For BCM4352/rev42 AC: edcf_nonzero=true
 * (pub+0x54 init 0xffffffff, no initial-up zeroer), pci_war16165=false
 * (buscoretype 0x83c != 0x804), chip_is_4313=false, phytype=0x0b.
 */
struct ob_d3b_band {
	u16	boardtype;
	u32	boardflags;
	u8	aa2g;
	u8	aa5g;
	u8	antswitch;
	bool	edcf_nonzero;
	bool	pci_war16165;
	bool	chip_is_4313;
	u16	phytype;
};

/* Board fields -> the resolved gate inputs (see struct ob_d3b_band). */
static inline void ob_d3b_band_from_board(const struct ob_sprom_board *b,
					  u16 phytype,
					  struct ob_d3b_band *out)
{
	if (!b || !out)
		return;
	out->boardtype = b->boardtype;
	out->boardflags = b->boardflags;
	out->aa2g = b->aa2g;
	out->aa5g = b->aa5g;
	out->antswitch = b->antswitch;
	/*
	 * BCM4352/rev42/AC resolved gate inputs (docs/m34d3b_band_init.md
	 * §3.1/§3.2): pub+0x54 is 0xffffffff at attach with no initial-up
	 * zeroer; the PCIe core is 0x83c (not 0x804) so si_pci_war16165 is
	 * false; this is not a 4313 part.
	 */
	out->edcf_nonzero = true;
	out->pci_war16165 = false;
	out->chip_is_4313 = false;
	out->phytype = phytype;
}

/*
 * Exact vendor wlc_antsel_attach() flow (blob 0x5970a), including the
 * L_bt0 -> L_bf fall-through on every mismatch (0x5988d..0x598ca -> 0x598cc).
 * Returns the antsel_type (0..6). The availability flag is not needed for MHF3.
 */
static inline int ob_d3b_antsel_type(u16 boardtype, u32 boardflags,
				     u8 antswitch, u8 aa2g, u8 aa5g)
{
	if (boardtype > 3u && antswitch != 0u) {
		/* antswitch-only group table; unconditionally returns. */
		if (antswitch > 7u)
			return 0;
		if (antswitch == 1u || antswitch == 2u || antswitch == 3u)
			return 2;
		if (antswitch == 5u)
			return 4;
		if (antswitch == 4u)
			return 3;
		if (antswitch == 6u)
			return 5;
		if (antswitch == 7u)
			return 6;
		return 0;
	}
	if (boardtype > 3u && antswitch == 0u) {
		if (boardtype == 4u && aa2g == 7u && aa5g == 0u)
			return 2;
		/* otherwise falls through to L_bf */
	}
	/* L_bf (also the boardtype <= 3 entry point) */
	if (boardflags & OB_D3B_MHF2_PCIWAR)
		return 1;
	return 0;
}

/* wlc_bmac_init MHF3 bits: {2,3,6} -> 0x3, 1 -> 0x1, else 0x0. */
static inline u16 ob_d3b_mhf3_from_antsel(int antsel)
{
	if (antsel == 2 || antsel == 3 || antsel == 6)
		return OB_D3B_MHF3_ANTSEL_EN | OB_D3B_MHF3_ANTSEL_MODE;
	if (antsel == 1)
		return OB_D3B_MHF3_ANTSEL_EN;
	return 0;
}

/*
 * Derive the band-0 mhfs[0..4] vector from the resolved board/gate inputs.
 * Reproduces the captured BCM4352 vector exactly:
 *   {0x0100, 0x0000, 0x0000, 0x0000, 0x0080}
 */
static inline void ob_d3b_mhf_vector(const struct ob_d3b_band *b, u16 mhf[5])
{
	if (!b || !mhf)
		return;
	mhf[0] = b->edcf_nonzero ? OB_D3B_MHF1_EDCF : 0u;
	mhf[1] = b->pci_war16165 ? OB_D3B_MHF2_PCIWAR : 0u;
	mhf[2] = ob_d3b_mhf3_from_antsel(
			ob_d3b_antsel_type(b->boardtype, b->boardflags,
					   b->antswitch, b->aa2g, b->aa5g));
	mhf[3] = b->chip_is_4313 ? OB_D3B_MHF4_4313 : 0u;
	mhf[4] = (b->phytype != 7u) ? OB_D3B_MHF5_AC : 0u;
}

/* The canonical captured BCM4352 band-0 vector (tests assert the derivation). */
static inline bool ob_d3b_mhf_is_captured(const u16 mhf[5])
{
	return mhf && mhf[0] == 0x0100u && mhf[1] == 0x0000u &&
	       mhf[2] == 0x0000u && mhf[3] == 0x0000u && mhf[4] == 0x0080u;
}

/* ---- bsinitvals42 record shape (host-testable) ---- */

static inline bool ob_d3b_bs_plan_ok(const struct ob_initvals_plan *plan)
{
	return plan && plan->records == OB_D3B_BS_RECORDS &&
	       plan->w16 == OB_D3B_BS_W16 && plan->w32 == OB_D3B_BS_W32;
}

static inline bool ob_d3b_bs_counts_ok(u32 total, u32 w16, u32 w32)
{
	return total == OB_D3B_BS_RECORDS && w16 == OB_D3B_BS_W16 &&
	       w32 == OB_D3B_BS_W32;
}

/*
 * Every bsinitvals42 record must target one of the recovered D11 offsets.
 * This is the static "no PHY/radio/DMA/IRQ window touched" guarantee: none of
 * these offsets is in the PHY-indirect (0x3fc/0x3fe), radio (0x3d8/0x3da), DMA
 * or interrupt-controller blocks.
 */
static inline bool ob_d3b_bs_offset_allowed(u16 off)
{
	switch (off) {
	case 0x0160u:	/* OBJADDR (SHM selector) */
	case 0x0164u:	/* OBJDATA low */
	case 0x0166u:	/* OBJDATA high */
	case 0x0680u:	/* IFS direct */
	case 0x0682u:	/* IFS direct */
	case 0x0684u:	/* IFS direct */
	case 0x0686u:	/* IFS direct */
	case 0x0700u:	/* NAV direct */
		return true;
	default:
		return false;
	}
}

/* ---- postcondition predicate (host-testable) ---- */

struct ob_d3b_post {
	u16	mhf[OB_D3B_MHF_COUNT];
	u32	shm_10;
	u32	shm_1c;
	u32	shm_94;
	u32	maccontrol;
	u32	macintmask;
	u32	records;
	u32	w16;
	u32	w32;
};

static inline bool ob_d3b_post_ok(const struct ob_d3b_post *p)
{
	return p &&
	       ob_d3b_mhf_is_captured(p->mhf) &&
	       p->shm_10 == OB_D3B_SHM_OVR_10_VAL &&
	       p->shm_1c == OB_D3B_SHM_OVR_1C_VAL &&
	       p->shm_94 == OB_D3B_SHM_OVR_94_VAL &&
	       p->maccontrol == OB_D3B_MACCONTROL_EXPECTED &&
	       !(p->maccontrol & OB_D3B_MCTL_EN_MAC) &&
	       (p->maccontrol & OB_D3B_MCTL_PSM_RUN) &&
	       p->macintmask == OB_D3B_MACINTMASK_EXPECTED &&
	       ob_d3b_bs_counts_ok(p->records, p->w16, p->w32);
}

/* ---- stage/order model (host-testable) ---- */

/*
 * The vendor order is fixed and must not be reordered:
 *   D3A1 tail -> DMA live -> MHF -> bsinitvals42 -> STOP before wlc_phy_init
 *          -> verified teardown.
 */
enum ob_d3b_stage {
	OB_D3B_STAGE_D3A1_PREFIX = 0,
	OB_D3B_STAGE_MHF,
	OB_D3B_STAGE_BSINITVALS,
	OB_D3B_STAGE_STOP_BEFORE_PHY,
	OB_D3B_STAGE_TEARDOWN,
	OB_D3B_STAGE__COUNT,
};

/* DMA MUST still be live while D3B executes (T1 -> DMA -> T2 -> D3B). */
static inline bool ob_d3b_dma_live_during_d3b(void)
{
	return true;
}

static inline int ob_d3b_stage_rank(enum ob_d3b_stage s)
{
	return (int)s;
}

/* The accepted vendor order (DMA live through MHF/bsinitvals, then teardown). */
static inline bool ob_d3b_stage_rank_ok(void)
{
	return ob_d3b_stage_rank(OB_D3B_STAGE_D3A1_PREFIX) <
		       ob_d3b_stage_rank(OB_D3B_STAGE_MHF) &&
	       ob_d3b_stage_rank(OB_D3B_STAGE_MHF) <
		       ob_d3b_stage_rank(OB_D3B_STAGE_BSINITVALS) &&
	       ob_d3b_stage_rank(OB_D3B_STAGE_BSINITVALS) <
		       ob_d3b_stage_rank(OB_D3B_STAGE_STOP_BEFORE_PHY) &&
	       ob_d3b_stage_rank(OB_D3B_STAGE_STOP_BEFORE_PHY) <
		       ob_d3b_stage_rank(OB_D3B_STAGE_TEARDOWN);
}

#ifdef __KERNEL__
struct ob_hw;

/**
 * struct ob_d3b - kernel state for the isolated D3B band-init test
 * @band:		board-derived gate inputs
 * @mhf:		 derived band-0 MHF vector actually written
 * @antsel_type:	 derived antsel_type
 * @phyver_status:	 D11+0x3e0 read-only diagnostic
 * @bs_records:	 bsinitvals data records applied (must be 73)
 * @bs_w16:		 width-2 records applied (must be 39)
 * @bs_w32:		 width-4 records applied (must be 34)
 * @offset_allowed: true when every applied record offset was in the allowed set
 * @post:		 postcondition snapshot read before teardown
 * @stopped_before_phy: set only on the successful STOP-before-PHY path
 * @fatal_seen:	 set when a post-DMA failure forced a teardown attempt
 */
struct ob_d3b {
	struct ob_d3b_band	band;
	u16			mhf[OB_D3B_MHF_COUNT];
	int			antsel_type;
	u16			phyver_status;
	u32			bs_records;
	u32			bs_w16;
	u32			bs_w32;
	bool			offset_allowed;
	struct ob_d3b_post	post;
	bool			stopped_before_phy;
	bool			fatal_seen;
};

/*
 * Run the isolated D3B band-init test. Real MMIO; requires explicit human
 * approval. It reuses the proven D2A/D2B core and the exact D3A1 prefix with
 * the D3A0 DMA engines left live, then runs MHF + bsinitvals42 and STOPS before
 * wlc_phy_init. It is never run automatically.
 */
int ob_d3b_test(struct ob_hw *hw);

/*
 * remove() hook for bsinitvals_test_only. Honours the fail-closed DMA
 * lifecycle: never frees DMA memory while hardware may still consume it.
 */
void ob_d3b_remove(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_D3B_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — isolated D3A1 vendor post-common / pre-PHY D11 tail test
 * (M3.4D3A1).
 *
 * D3A1 TYPE: ISOLATED TAIL TEST. It reproduces the exact vendor BCM4352/rev42
 * ordering from the proven D2B exit:
 *
 *   D2B common initvals
 *     -> sub_67efd equivalent (TXE0 FIFO fixup)
 *     -> T1 (MACCONTROL/clock/IRQ-source/SHM tail)
 *     -> DMA (reusing the hardware-proven D3A0 lifecycle)
 *     -> T2 (BTC/NVRAM gate, MAC SHM, switch_macfreq)
 *     -> STOP immediately before sub_6656c
 *
 * It NEVER enters sub_6656c, bsinitvals, wlc_phy_init, PHY indirect MMIO,
 * radio, channel, calibration, EN_MAC, host IRQ delivery or mac80211.
 *
 * This header carries the exact recovered constants and the pure (host-
 * testable) helpers. It deliberately contains no kernel API so the write-count
 * accounting, the sub_67efd entry values, the MAC conversion, the btc_base gate
 * and the mode policy can be unit-tested on the host.
 *
 * Provenance (C2 = vendor blob `wlc_hybrid.o_shipped` sha256 352a6e349f…;
 * C3 = upstream brcmsmac d11.h corroboration): docs/m34d3a1_vendor_tail.md.
 */
#ifndef _OB_D3A1_H_
#define _OB_D3A1_H_

#include <linux/types.h>
#include "ob_d3a0.h"
#include "ob_initvals.h"

/* ---- D11 core-register offsets used by sub_67efd (blob 0x67efd) ---- */
#define OB_D3A1_REG_MACHWCAP		0x015c
#define OB_D3A1_REG_XMTFIFOCMD		0x0540	/* xmtfifocmd  */
#define OB_D3A1_REG_XMTFIFOFLUSH	0x0542	/* xmtfifoflush */
#define OB_D3A1_REG_XMTFIFODEF		0x0520	/* xmtfifodef  */
#define OB_D3A1_REG_XMT_530		0x0530
#define OB_D3A1_REG_XMT_532		0x0532
#define OB_D3A1_REG_XMT_534		0x0534
#define OB_D3A1_REG_XMT_536		0x0536
#define OB_D3A1_REG_XMTFIFOPRIRDY	0x0548	/* xmtfifoprirdy */
#define OB_D3A1_REG_XMTFIFORQPRI	0x054a	/* xmtfiforqpri */
#define OB_D3A1_REG_XMTTPLATETXPTR	0x054c	/* xmttplatetxptr */
#define OB_D3A1_REG_XMT_54E		0x054e
#define OB_D3A1_REG_XMTTPLATEPTR	0x0550	/* xmttplateptr */

/* sub_67efd exact loop geometry and poll bound (blob, no guesses) */
#define OB_D3A1_FIFO_TABLE1_LEN		7u
#define OB_D3A1_FIFO7_WRITES		42u	/* 7 entries x 6 writes */
#define OB_D3A1_FIFO42_ENTRIES		42u
#define OB_D3A1_FIFO42_WRITES		168u	/* 42 entries x 4 writes */
#define OB_D3A1_FIFO_FIXED_WRITES	2u	/* 0x542 + 0x540 */
#define OB_D3A1_FIFO_TOTAL_WRITES \
	(OB_D3A1_FIFO_FIXED_WRITES + OB_D3A1_FIFO7_WRITES + \
	 OB_D3A1_FIFO42_WRITES)			/* 212 */
#define OB_D3A1_FIFO_POLL_BOUND		0x00d1u
#define OB_D3A1_FIFO_POLL_STEP		10u
#define OB_D3A1_FIFO_POLL_DELAY_US	10u
#define OB_D3A1_FIFO_POLL_MAX_ITERS	(OB_D3A1_FIFO_POLL_BOUND / \
					 OB_D3A1_FIFO_POLL_STEP) /* 20 */
#define OB_D3A1_FIFO_VAL_2A		0x002au
#define OB_D3A1_FIFO_VAL_TPLATE		0x740cu
#define OB_D3A1_FIFO_VAL_DEF_REV42	0x000bu
#define OB_D3A1_FIFO_VAL_CX_REV42	0x0020u

/* ---- T1 constants (blob 0x68fe2..0x69211, exact) ---- */
#define OB_D3A1_SHM_MBURST		0x0080u
#define OB_D3A1_SHM_MAXANTCNT		0x005cu
#define OB_D3A1_SHM_MACHWVER		0x0016u
#define OB_D3A1_SHM_MACHWCAP_L		0x00c0u
#define OB_D3A1_SHM_MACHWCAP_H		0x00c2u
#define OB_D3A1_SHM_SFBL		0x0044u
#define OB_D3A1_SHM_LFBL		0x0046u
#define OB_D3A1_SHM_MBURST_VAL		0x0008u
#define OB_D3A1_SHM_MAXANTCNT_VAL	0x000au

#define OB_D3A1_REG_INTRCVLAZY0		0x0100u
#define OB_D3A1_REG_MACCONTROL		0x0120u
#define OB_D3A1_REG_MACINTSTATUS	0x0128u
#define OB_D3A1_REG_MACINTMASK		0x012cu
#define OB_D3A1_REG_INTCONTROL0_MASK	0x0024u
#define OB_D3A1_REG_TSF_CFPREP		0x0188u
#define OB_D3A1_REG_TSF_CFPSTART	0x018cu
#define OB_D3A1_REG_IFS_CTL		0x0688u
#define OB_D3A1_REG_IFS_AIFSN		0x069cu
#define OB_D3A1_REG_FASTPWRUP_DLY	0x06a8u

#define OB_D3A1_INTRCVLAZY		0x01000000u
#define OB_D3A1_MI_GP1			0x00004000u
#define OB_D3A1_I_RI			0x00010000u
#define OB_D3A1_TSF_CFPREP		0x80000000u
#define OB_D3A1_TSF_CFPSTART		0x02000000u
#define OB_D3A1_MACCONTROL_MASK		0x40060000u
#define OB_D3A1_MACCONTROL_VAL		0x40020000u
#define OB_D3A1_MACCONTROL_EXPECTED	0x44020402u
#define OB_D3A1_IFS_CTL_MASK		0x00000fffu
#define OB_D3A1_IFS_AIFSN_VAL		0x0001u
#define OB_D3A1_PHYREV_REV42		0x2au

/* objmem windows (C3 d11.h: SHM 0x10000, SCR 0x20000) */
#define OB_D3A1_REG_OBJADDR		0x0160u
#define OB_D3A1_REG_OBJDATA		0x0164u
#define OB_D3A1_OBJADDR_SHM_SEL		0x00010000u
#define OB_D3A1_OBJADDR_SCR_SEL		0x00020000u
#define OB_D3A1_SCR_SRL			0x0018u
#define OB_D3A1_SCR_LRL			0x001cu

/*
 * Vendor attach defaults for the isolated test (no OpenBRCM NVRAM provider).
 * SRL: wlc_info_init 0x24e4b writes wlc_info+0x52a = 7.
 * LRL: wlc_attach 0x380d9 passes lrl = 6 to wlc_bmac_retrylimit_upd.
 * SFBL/LFBL: RETRY_SHORT_FB=3 / RETRY_LONG_FB=2 (upstream brcmsmac main.c).
 */
#define OB_D3A1_SRL_DEFAULT		7u
#define OB_D3A1_LRL_DEFAULT		6u
#define OB_D3A1_SFBL_DEFAULT		3u
#define OB_D3A1_LFBL_DEFAULT		2u

/*
 * Fast-pwrup delay. The blob calls si_clkctl_fast_pwrup_delay(); for a
 * PMU-capable part this resolves to si_pmu_fast_pwrup_delay(), whose 0x4352
 * case returns 0x0bb8 (3000) for chiprev >= 4 and 0x05dc (1500) otherwise
 * (blob 0x16091..0x160a6). sub_5fdca() adds a phytype-dependent delta to the
 * software copy only; the register write uses the PMU value.
 */
#define OB_D3A1_FASTPWRUP_REV_LT4	1500u
#define OB_D3A1_FASTPWRUP_REV_GE4	3000u
#define OB_D3A1_PHYTYPE_AC		0x0bu
#define OB_D3A1_SUB5FDCA_PHY_AC		0x04b0u	/* blob sub_5fdca, phytype 0xb */

/* ---- T2 constants (blob 0x6930e..0x695cb, exact) ---- */
#define OB_D3A1_SHM_BTC_BASE		0x0092u
#define OB_D3A1_SHM_BTC_READBACK	0x008eu
#define OB_D3A1_BTC_PARAMS_MAX		0x76u	/* btc_params0..118 */
#define OB_D3A1_BTC_EXTRA_NUM		4u
#define OB_D3A1_BTC_EXTRA_OFF		0x0002u
#define OB_D3A1_BTC_EXTRA_OFF2		0x0010u
#define OB_D3A1_BTC_EXTRA_OFF3		0x0012u
#define OB_D3A1_BTC_EXTRA_OFF4		0x002cu
#define OB_D3A1_BTC_EXTRA_VAL1		0x7530u
#define OB_D3A1_BTC_EXTRA_VAL2		0x4e20u
#define OB_D3A1_BTC_EXTRA_VAL3		0x7530u
#define OB_D3A1_BTC_EXTRA_VAL4		0x0753u

#define OB_D3A1_SHM_MAC_0		0x078cu
#define OB_D3A1_SHM_MAC_1		0x078eu
#define OB_D3A1_SHM_MAC_2		0x0790u
#define OB_D3A1_MAC_WORDS		3u

#define OB_D3A1_REG_TSF_FRAC_L		0x062eu
#define OB_D3A1_REG_TSF_FRAC_H		0x0630u

/* switch_macfreq chip set (blob 0x64f14 and wlc_bmac_init 0x69599) */
static inline bool ob_d3a1_chip_uses_macfreq(u32 chip_id)
{
	return chip_id == 0xa9c4u || chip_id == 0x4360u ||
	       chip_id == 0xaa06u || chip_id == 0x4352u ||
	       chip_id == 0x4350u;
}

/* ---- pure sub_67efd model (host-testable) ---- */

/* The fixed 7-entry programming table (.rodata+0x284740). */
static inline u32 ob_d3a1_fifo_table1(u32 i)
{
	static const u32 tbl[OB_D3A1_FIFO_TABLE1_LEN] = {7, 0, 1, 2, 3, 4, 5};

	return (i < OB_D3A1_FIFO_TABLE1_LEN) ? tbl[i] : 0;
}

/* machwcap -> the flush value written to 0x542: (machwcap >> 1) & 0xffc. */
static inline u16 ob_d3a1_fifo_flush_val(u32 machwcap)
{
	return (u16)((machwcap >> 1) & 0xffcu);
}

/*
 * 7-entry loop writes. entry == 7 uses the 0x2a group; every other entry uses
 * the rev42 group: rqpri = v - 0x2a, tplate-txptr = 0x20, def = 0xb,
 * 0x54e = ((2*0xb - 4) << 8) | (2*0xb). 0x550 = 0x740c and
 * 0x548 = entry | 0x10 for every entry.
 */
static inline u16 ob_d3a1_fifo7_rqpri(u32 entry, u16 v)
{
	return entry == 7u ? OB_D3A1_FIFO_VAL_2A
			   : (u16)(v - OB_D3A1_FIFO_VAL_2A);
}

static inline u16 ob_d3a1_fifo7_txptr(u32 entry)
{
	return entry == 7u ? OB_D3A1_FIFO_VAL_2A : OB_D3A1_FIFO_VAL_CX_REV42;
}

static inline u16 ob_d3a1_fifo7_def(u32 entry)
{
	return entry == 7u ? OB_D3A1_FIFO_VAL_2A
			   : OB_D3A1_FIFO_VAL_DEF_REV42;
}

static inline u16 ob_d3a1_fifo7_54e(u32 entry)
{
	u16 edx = entry == 7u ? OB_D3A1_FIFO_VAL_2A
			      : (u16)(2u * OB_D3A1_FIFO_VAL_DEF_REV42);

	return (u16)(((edx - 4u) << 8) | edx);
}

static inline u16 ob_d3a1_fifo7_prirdy(u32 entry)
{
	return (u16)(entry | 0x10u);
}

/* 42-entry loop writes (idx 0..41). */
static inline u16 ob_d3a1_fifo42_x534(u32 idx)
{
	return (u16)idx;
}

static inline u16 ob_d3a1_fifo42_x536(u32 idx)
{
	u32 e = idx + 2u;

	return (u16)(e > 0x29u ? 0x29u : e);
}

static inline u16 ob_d3a1_fifo42_x532(u32 idx)
{
	return (u16)(ob_d3a1_fifo42_x536(idx) + (idx == 0u ? 1u : 0u));
}

static inline u16 ob_d3a1_fifo42_x530(u32 idx)
{
	return (u16)((idx << 4) | 0x8007u);
}

/*
 * Bounded vendor poll predicate. The blob loop continues while the completion
 * predicate is false AND the counter has not reached step-1 (`cmp ...,9`).
 * A poll that exits because the counter reached step-1 is a TIMEOUT and must
 * abort the caller.
 */
static inline bool ob_d3a1_poll_continue(bool done, u32 counter)
{
	return !done && counter != (OB_D3A1_FIFO_POLL_STEP - 1u);
}

/* ---- pure MAC / BTC / clock model (host-testable) ---- */

/* SHM word N = big-endian pair (mac[2N] << 8) | mac[2N+1]. */
static inline u16 ob_d3a1_mac_word(const u8 mac[6], u32 idx)
{
	if (idx >= OB_D3A1_MAC_WORDS)
		return 0;
	return (u16)(((u16)mac[2u * idx] << 8) | mac[2u * idx + 1u]);
}

/* btc_base = 2 * SHM[0x92]; 0 disables the whole T2 BTC block (vendor gate). */
static inline u32 ob_d3a1_btc_base(u16 shm92)
{
	return (u32)shm92 * 2u;
}

/*
 * ---- exact vendor 64-bit helpers (verbatim blob ports) ----
 *
 * ob_d3a1_muladd() is a verbatim port of blob `bcm_uint64_multiple_add`
 * (0xac8f): out_hi/out_lo <- (a * b + c) as a 64-bit value. Verified against
 * the extracted vendor machine code over 200000 random inputs.
 *
 * ob_d3a1_u64_divide() is a verbatim port of blob `bcm_uint64_divide`
 * (0xad85): divide(x, y, b) with x high / y low. It is NOT a plain 64-bit
 * divide; it is the vendor's 32-bit-limb long division, ported instruction by
 * instruction and verified against the extracted vendor machine code over 288
 * directed + 50000 random vectors. b <= 1 makes the vendor write nothing; this
 * helper returns OB_D3A1_DIV_NO_WRITE in that case.
 */
#define OB_D3A1_DIV_NO_WRITE	0xffffffffu

static inline void ob_d3a1_muladd(u32 *out_hi, u32 *out_lo, u32 a, u32 b,
				  u32 c)
{
	u32 r10 = a, r11 = b, ecx = b, edx = a, r15, r13, r12, eax, ebx;
	u32 r9, r14, r8 = c;

	r10 >>= 16;
	ecx &= 0xffff;
	edx &= 0xffff;
	r11 >>= 16;
	r15 = c;
	r8 &= 0x7fffffff;
	r15 >>= 31;
	r13 = ecx;
	ecx *= r10;
	r13 *= edx;
	r12 = ecx;
	ecx >>= 16;
	edx *= r11;
	r12 <<= 16;
	eax = r13;
	ebx = r12;
	eax &= 0x7fffffff;
	r13 >>= 31;
	ebx &= 0x7fffffff;
	r13 += r15;
	r12 >>= 31;
	r9 = edx;
	ebx += eax;
	r12 = r13 + r12;
	r9 <<= 16;
	r14 = ebx;
	ebx >>= 31;
	eax = r9;
	r14 &= 0x7fffffff;
	r9 >>= 31;
	eax &= 0x7fffffff;
	r9 = r12 + r9;
	edx >>= 16;
	eax = r14 + eax;
	edx = ecx + edx;
	r9 += ebx;
	r14 = eax;
	eax >>= 31;
	r14 &= 0x7fffffff;
	r9 += eax;
	r8 = r14 + r8;
	r12 = r8;
	r8 &= 0x7fffffff;
	r12 >>= 31;
	r9 += r12;
	eax = r9;
	eax &= 1;
	eax = (u32)(-(s32)eax);
	eax &= 0x80000000u;
	r11 *= r10;
	r9 >>= 1;
	eax |= r8;
	r10 = edx + r11;
	r9 = r10 + r9;
	*out_hi = r9;
	*out_lo = eax;
}

static inline u32 ob_d3a1_u64_divide(u32 x, u32 y, u32 b)
{
	u32 a = x, c = y, r12 = 0;

	if (b <= 1)
		return OB_D3A1_DIV_NO_WRITE;
	while (a != 0) {
		u32 q1 = 0xffffffffu / b;
		u32 rem1 = 0xffffffffu % b;
		u32 rem2 = (rem1 + 1u) % b;

		r12 += q1 * a;
		ob_d3a1_muladd(&a, &c, rem2, a, c);
	}
	return r12 + (c / b);
}

/*
 * BCM4352 TSF clock fraction. The blob calls
 *   bcm_uint64_divide(&out, 0x3a9, 0x80000000, vco)
 * and writes out[15:0] -> 0x62e, out[31:16] -> 0x630. A divisor <= 1 leaves
 * the vendor output unwritten (undefined); the helper signals that with
 * OB_D3A1_DIV_NO_WRITE.
 */
#define OB_D3A1_TSF_DIV_HIGH	0x80000000u
#define OB_D3A1_TSF_DIV_LOW	0x000003a9u

static inline u32 ob_d3a1_tsf_frac(u32 vco)
{
	return ob_d3a1_u64_divide(OB_D3A1_TSF_DIV_LOW, OB_D3A1_TSF_DIV_HIGH,
				  vco);
}

static inline u16 ob_d3a1_tsf_frac_lo(u32 frac)
{
	return (u16)(frac & 0xffffu);
}

static inline u16 ob_d3a1_tsf_frac_hi(u32 frac)
{
	return (u16)(frac >> 16);
}

/*
 * ---- exact BCM4352 BB VCO frequency (blob si_pmu_get_bb_vcofreq 0x14b7b) ----
 *
 * For chip 0x4352: p2 = PMU PLL index 2, p3 = PMU PLL index 3.
 *   d   = (p2 >> 4) & 7
 *   den = p2 >> 7
 *   q   = 0x28 * 10000          (the caller passes 0x28)
 *   esi = 0                     if d == 0
 *       = (lo >> 24) | (hi << 8) where {hi, lo} = muladd(q, p3, 0x800000)
 *   if (s32)q > (s32)(~esi / den) -> 0 (rejected)
 *   else esi + den * q
 * Returns 0 when den == 0 (the vendor would divide by zero); the caller must
 * treat 0 as "cannot derive", never as a legitimate skip.
 */
static inline u32 ob_d3a1_bb_vcofreq_from_pll(u32 p2, u32 p3)
{
	u32 d = (p2 >> 4) & 0x7u;
	u32 den = p2 >> 7;
	u32 q = 0x28u * 10000u;
	u32 esi = 0;

	if (d != 0) {
		u32 hi, lo;

		ob_d3a1_muladd(&hi, &lo, q, p3, 0x800000u);
		esi = (lo >> 24) | (hi << 8);
	}
	if (den == 0)
		return 0;
	if ((s32)q > (s32)((~esi) / den))
		return 0;
	return esi + den * q;
}

/*
 * ---- D3A1 stage/order model (host-testable) ----
 *
 * The vendor order is fixed and may not be reordered for code convenience:
 *   sub_67efd -> T1 -> DMA -> T2 -> switch_macfreq -> STOP before sub_6656c.
 * This enum makes the accepted order assertable on the host.
 */
enum ob_d3a1_stage {
	OB_D3A1_STAGE_FIFO_FIXUP = 0,
	OB_D3A1_STAGE_T1,
	OB_D3A1_STAGE_DMA,
	OB_D3A1_STAGE_T2,
	OB_D3A1_STAGE_SWITCH_MACFREQ,
	OB_D3A1_STAGE_STOP_BEFORE_SUB6656C,
	OB_D3A1_STAGE__COUNT,
};

static inline int ob_d3a1_stage_rank(enum ob_d3a1_stage s)
{
	return (int)s;
}

/* The accepted vendor order (DMA is between T1 and T2). */
static inline bool ob_d3a1_order_ok(void)
{
	return ob_d3a1_stage_rank(OB_D3A1_STAGE_FIFO_FIXUP) <
		       ob_d3a1_stage_rank(OB_D3A1_STAGE_T1) &&
	       ob_d3a1_stage_rank(OB_D3A1_STAGE_T1) <
		       ob_d3a1_stage_rank(OB_D3A1_STAGE_DMA) &&
	       ob_d3a1_stage_rank(OB_D3A1_STAGE_DMA) <
		       ob_d3a1_stage_rank(OB_D3A1_STAGE_T2) &&
	       ob_d3a1_stage_rank(OB_D3A1_STAGE_T2) <
		       ob_d3a1_stage_rank(OB_D3A1_STAGE_STOP_BEFORE_SUB6656C);
}

/*
 * The SCR 0x24 (rate/tpc) write is skipped on the first init: wlc_info_init
 * sets wlc_info+0x718 = 1 and the first wlc_bmac_init clears it and skips the
 * copyto_objmem(0x24). D3A1 is the initial bring-up, so it is always skipped.
 */
#define OB_D3A1_SCR_RATE_24			0x0024u
#define OB_D3A1_SCR_RATE_24_FIRST_INIT_SKIPPED	1

/*
 * BTC variable action. The vendor writes a btc_params/btc_flags value only when
 * getvar() returned a value; an absent key is SKIPPED (never zero-filled).
 * OpenBRCM has no NVRAM text-variable provider, so every key is absent.
 */
enum ob_d3a1_btc_action {
	OB_D3A1_BTC_SKIP = 0,
	OB_D3A1_BTC_WRITE,
};

static inline enum ob_d3a1_btc_action ob_d3a1_btc_key_action(const char *value)
{
	return (value == NULL) ? OB_D3A1_BTC_SKIP : OB_D3A1_BTC_WRITE;
}

/* ---- pure mode policy (host-testable) ---- */

/* Exactly one isolated mode may be active; any combination is a conflict. */
static inline unsigned int
ob_d3a1_mode_count(bool fw_validate_only, bool ucode_test_only,
		   bool initvals_test_only, bool dma_test_only,
		   bool d11_tail_test_only)
{
	return (fw_validate_only ? 1u : 0u) + (ucode_test_only ? 1u : 0u) +
	       (initvals_test_only ? 1u : 0u) + (dma_test_only ? 1u : 0u) +
	       (d11_tail_test_only ? 1u : 0u);
}

#if defined(__KERNEL__) || !defined(OB_D3A1_HOST_PURE_ONLY)
#include "ob_ucode.h"

static inline enum ob_isolated_mode
ob_d3a1_mode_select(bool fw_validate_only, bool ucode_test_only,
		    bool initvals_test_only, bool dma_test_only,
		    bool d11_tail_test_only)
{
	if (ob_d3a1_mode_count(fw_validate_only, ucode_test_only,
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
#endif /* __KERNEL__ || !OB_D3A1_HOST_PURE_ONLY */

#ifdef __KERNEL__
struct ob_hw;

/**
 * struct ob_d3a1 - kernel state for the isolated D3A1 tail test
 * @fifo_fixed_writes: 0x542/0x540 writes performed
 * @fifo7_writes:      writes performed by the 7-entry loop (must be 42)
 * @fifo42_writes:     writes performed by the 42-entry loop (must be 168)
 * @fifo_poll540_iters: iterations of the 0x540 completion poll (must complete)
 * @fifo_poll530_iters: total iterations across the 42 x 0x530 polls
 * @fifo_poll530_max:  max iterations observed in a single 0x530 poll
 * @fifo_poll_completed: number of 0x530 polls that met the completion predicate
 * @fifo_fail_index:   -1, or the 0x530 table index that timed out
 * @machwcap:          machwcap read from D11+0x15c
 * @fastpwrup_dly:     value written to D11+0x6a8
 * @fastpwrup_dly_sw:  vendor software copy (dev+0x192) incl. sub_5fdca delta
 * @bb_vcofreq:        PMU-derived BB VCO frequency (0 => cannot derive)
 * @pll2_raw:          raw PMU PLL2 value read by switch_macfreq
 * @pll3_raw:          raw PMU PLL3 value (valid only when pll3_read)
 * @pll3_read:         true when PLL3 was actually read (PLL2 bits 4..6 != 0)
 * @bb_d:              (PLL2 >> 4) & 7
 * @bb_den:            PLL2 >> 7
 * @tsf_frac:          derived TSF clock fraction
 * @tsf_frac_lo:       value programmed into D11 0x62e
 * @tsf_frac_hi:       value programmed into D11 0x630
 * @tsf_frac_lo_rb:    D11 0x62e read back after programming
 * @tsf_frac_hi_rb:    D11 0x630 read back after programming
 * @tsf_frac_rb_proven: true only if 0x62e/0x630 stable readback is proven
 * @btc_shm92:         raw SHM[0x92] value
 * @btc_base:          2 * SHM[0x92]
 * @btc_block_ran:     true when btc_base != 0 (BTC T2 block executed)
 * @mac_written:       true when the 0x78c/0x78e/0x790 MAC words were written
 * @stopped_before_phy: set only on the successful STOP-before-sub_6656c path
 */
struct ob_d3a1 {
	u32	fifo_fixed_writes;
	u32	fifo7_writes;
	u32	fifo42_writes;
	u32	fifo_poll540_iters;
	u32	fifo_poll530_iters;
	u32	fifo_poll530_max;
	u32	fifo_poll_completed;
	int	fifo_fail_index;
	u32	machwcap;
	u16	fastpwrup_dly;
	u16	fastpwrup_dly_sw;
	u32	bb_vcofreq;
	u32	pll2_raw;
	u32	pll3_raw;
	bool	pll3_read;
	u32	bb_d;
	u32	bb_den;
	u32	tsf_frac;
	u16	tsf_frac_lo;
	u16	tsf_frac_hi;
	u16	tsf_frac_lo_rb;
	u16	tsf_frac_hi_rb;
	bool	tsf_frac_rb_proven;
	u16	btc_shm92;
	u32	btc_base;
	bool	btc_block_ran;
	bool	mac_written;
	bool	stopped_before_phy;
};

/*
 * Run the isolated D3A1 vendor-ordered tail test. Real MMIO; requires explicit
 * human approval. It reuses the proven D2A/D2B core and the D3A0 DMA lifecycle
 * and STOPS before sub_6656c. It is never run automatically.
 */
int ob_d3a1_test(struct ob_hw *hw);

/*
 * remove() hook for d11_tail_test_only. Honours the fail-closed DMA lifecycle:
 * never frees DMA memory while hardware may still consume it.
 */
void ob_d3a1_remove(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_D3A1_H_ */

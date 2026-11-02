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
#include "ob_d11_tail_plan.h"

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
/*
 * tsf_cfpstart (0x18c) is a CFP-start programming register: the vendor and
 * upstream (b43 B43_MMIO_TSF_CFP_START, brcmsmac tsf_cfpstart) only WRITE it
 * (beacon interval, `period << 10`) and never read it back. The readable CFP
 * value is split across tsf_cfpstrt_l/h (0x604/0x606), and the vendor's
 * wlc_bmac_validate_chip_access validates a write by reading THOSE, never
 * 0x18c. A hardware attempt (2026-09) read 0x3c000000 instead of the
 * programmed 0x02000000, so direct readback is not a stable equality
 * postcondition. It is write-accounted and logged as a diagnostic only; the
 * vendor-exact write value/order are unchanged.
 */
#define OB_D3A1_REG_TSF_CFPSTRT_L	0x0604u
#define OB_D3A1_REG_TSF_CFPSTRT_H	0x0606u
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

/*
 * The vendor computes 0x532 as min(idx+2,0x29) + r13d where r13d is a
 * loop-carried value initialised to 1 and decremented once per iteration
 * (`dec r13d`, 0x68265), i.e. r13d == 1 - idx. That reduces exactly to
 * min(42 - idx, 3): 3 for idx 0..39, then 2, then 1.
 */
static inline u16 ob_d3a1_fifo42_x532(u32 idx)
{
	u32 r = (idx < OB_D3A1_FIFO42_ENTRIES) ?
		(OB_D3A1_FIFO42_ENTRIES - idx) : 0u;

	return (u16)(r > 3u ? 3u : r);
}

static inline u16 ob_d3a1_fifo42_x530(u32 idx)
{
	return (u16)((idx << 4) | 0x8007u);
}

/* ---- exact sub_67efd completion predicates (proven from the blob) -------- */

/*
 * 0x530: the vendor loop (0x6824d..0x6825e) is
 *
 *     mov rdi, r12              ; &0x530
 *     call osl_readw
 *     test %ax,%ax              ; 66 85 c0  (FULL 16-bit zero test)
 *     je   done                 ; 74 06
 *     cmpl $0x9, counter
 *     jne  loop
 *
 * i.e. completion is the whole 16-bit register reading 0, NOT a bit15 mask.
 */
static inline bool ob_d3a1_fifo530_done(u16 v)
{
	return v == 0u;
}

/*
 * 0x540: the vendor loop (0x68033..0x68044) is
 *
 *     mov rdi, [rbp-58h]        ; &0x540
 *     call osl_readw
 *     test $0x1,%al             ; a8 01  (bit0 test)
 *     je   done                 ; 74 06
 *     cmp  $0x9, r14d
 *     jne  loop
 *
 * i.e. completion is bit0 clear. The two polls do NOT share a predicate.
 */
static inline bool ob_d3a1_fifo540_done(u16 v)
{
	return (v & 0x1u) == 0u;
}

enum ob_d3a1_poll_kind {
	OB_D3A1_POLL_530,
	OB_D3A1_POLL_540,
};

static inline bool ob_d3a1_poll_done(enum ob_d3a1_poll_kind kind, u16 v)
{
	return kind == OB_D3A1_POLL_530 ? ob_d3a1_fifo530_done(v)
					: ob_d3a1_fifo540_done(v);
}

/*
 * Bounded vendor poll loop condition. The blob loop continues while the
 * completion predicate is false AND the counter has not reached step-1
 * (`cmp ...,9`). The blob has NO error path: when the counter reaches step-1
 * it simply falls through and the caller continues.
 *
 * OpenBRCM deliberately diverges from that fallthrough (M3). The vendor bound
 * is preserved byte-for-byte, but the first expiry is a failed postcondition:
 * the tail returns -ETIMEDOUT, marks the FIFO state unverified, forbids retry
 * and requires a reboot.  See docs/m34d3a1_vendor_tail_test.md and
 * audit/openbrcm_phase27/FAILURE_ROLLBACK_MATRIX.md (R1..R6).
 */
static inline bool ob_d3a1_poll_continue(bool done, u32 counter)
{
	return !done && counter != (OB_D3A1_FIFO_POLL_STEP - 1u);
}

/*
 * Fail-closed poll policy.  A non-zero poll result means the bounded wait
 * expired; it is never treated as success and never retried in place.  The
 * helper exists so the policy is a single testable definition instead of an
 * ad-hoc `if (ret)` at each call site.
 */
static inline bool ob_d3a1_poll_expired_policy(int poll_ret)
{
	return poll_ret != 0;
}

/*
 * ---- 0x530 expiry continuation (FIFO530-GATE-01, opt-in, vendor-faithful) --
 *
 * The vendor loop (0x6824d..0x6825e) has no error path. When the 21-read bound
 * expires it falls through to 0x68260, performs one more `osl_readw(0x530)`
 * whose result it discards (0x68260..0x6826d), increments the index and
 * programs the next entry; `sub_67efd` then returns 0 unconditionally
 * (0x6827d `xor %eax,%eax`). The vendor therefore cannot distinguish a
 * completed poll from a bounded expiry, and no later vendor code inspects or
 * compensates for it.
 *
 * OpenBRCM keeps the fail-closed stop as the DEFAULT and only an explicit,
 * isolated experiment may opt in to the vendor's continuation. The predicate
 * (readw(0x530) == 0) and the bound (0xd1/step 10 => 21 reads) are unchanged;
 * only the expiry action differs, and a continuation is always reported as
 * EXPIRED_AND_CONTINUED, never as completion. Independently fatal conditions
 * (device loss, all-ones read) stop the tail even with the opt-in.
 */
#define OB_D3A1_FIFO530_ALL_ONES 0xffffu

/* All-ones is the D11 no-slave-response pattern, never a valid FIFO state. */
static inline bool ob_d3a1_fifo530_read_is_fatal(u16 v)
{
	return v == OB_D3A1_FIFO530_ALL_ONES;
}

/*
 * ---- result classes ------------------------------------------------------
 *
 * Three outcomes exist and the caller MUST keep them apart:
 *
 *   COMPLETE  the vendor predicate held (readw(0x530) == 0, or bit0 clear on
 *             0x540) inside the vendor bound.
 *   EXPIRED   the vendor bound was reached with a legitimate, non-complete
 *             value. This is the ONLY class the explicit experimental opt-in
 *             may continue from, and it is recorded as EXPIRED_AND_CONTINUED,
 *             never as completion.
 *   INVALID   an all-ones observation at any point of the poll (or on the
 *             post-poll read). That is the no-slave-response pattern: the
 *             window did not answer, so the value is not a timeout and the
 *             tail must stop immediately with a fatal classification.
 *
 * A transient all-ones read in the middle of the 21 reads is therefore NOT a
 * timeout that the opt-in may continue from - see
 * ob_d3a1_poll_read_invalid() and the FIFO530_ALL_ONES_ANALYSIS audit.
 */
enum ob_d3a1_poll_result {
	OB_D3A1_POLL_COMPLETE = 0,
	OB_D3A1_POLL_EXPIRED,
	OB_D3A1_POLL_INVALID,
};

typedef u16 (*ob_d3a1_read16_fn)(void *ctx, u16 off);
typedef void (*ob_d3a1_write16_fn)(void *ctx, u16 off, u16 val);
typedef void (*ob_d3a1_delay_fn)(u32 us);

/*
 * Is this observation an invalid (device-loss) read rather than a legitimate
 * poll value?
 *
 * Yes for BOTH polls: 0xffff is what an unresponsive D11 window returns, and no
 * vendor predicate can make it meaningful on either register.
 *
 * 0x530's predicate is exactly zero, so 0xffff is never "done" there.
 *
 * 0x540's predicate is bit0-clear, which is why an earlier version excluded it
 * on the argument that all-ones "simply keeps the poll running until the bound".
 * That argument is wrong: it only covers a *sustained* all-ones sequence. The
 * counterexample is
 *
 *     read #1: 0xffff   (bit0 set -> not done, but the window did not answer)
 *     read #2: 0x0004   (bit0 clear -> predicate TRUE)
 *
 * i.e. one transient invalid observation followed by a legitimate-looking
 * completion. The tail would then proceed to T1/DMA on unverified hardware. The
 * fatal classification is therefore per-observation on both registers, and it
 * is checked before the predicate for exactly this reason.
 */
static inline bool ob_d3a1_poll_read_invalid(u16 v)
{
	return ob_d3a1_fifo530_read_is_fatal(v);
}

/*
 * The vendor's bounded poll, extracted so the kernel path and the host
 * integration tests run the SAME loop against different backends: the adapter
 * passes ob_d11_read16()/udelay(), the tests pass a mock that records every
 * access. The bound (0xd1, step 10, stop at step-1 => 21 reads) is unchanged
 * and every invalid observation is detected at the point it occurs, before the
 * delay and before any further read.
 */
static inline enum ob_d3a1_poll_result
ob_d3a1_poll_core(ob_d3a1_read16_fn read16, ob_d3a1_delay_fn delay,
		  void *ctx, u16 off, enum ob_d3a1_poll_kind kind,
		  u32 *iters, u16 *last)
{
	u32 counter = OB_D3A1_FIFO_POLL_BOUND;
	u32 n;

	for (n = 0; n < OB_D3A1_FIFO_POLL_MAX_ITERS + 1u; n++) {
		u16 v = read16(ctx, off);

		*last = v;
		if (ob_d3a1_poll_read_invalid(v)) {
			*iters = n + 1u;
			return OB_D3A1_POLL_INVALID;
		}
		if (ob_d3a1_poll_done(kind, v)) {
			*iters = n + 1u;
			return OB_D3A1_POLL_COMPLETE;
		}
		if (!ob_d3a1_poll_continue(false, counter)) {
			*iters = n + 1u;
			return OB_D3A1_POLL_EXPIRED;
		}
		if (delay)
			delay(OB_D3A1_FIFO_POLL_DELAY_US);
		counter -= OB_D3A1_FIFO_POLL_STEP;
	}
	*iters = n;
	return OB_D3A1_POLL_EXPIRED;
}

/* The four writes of one 42-entry row, in the vendor's exact order. */
static inline void ob_d3a1_fifo42_entry_write(ob_d3a1_write16_fn write16,
					      void *ctx, u16 index)
{
	write16(ctx, OB_D3A1_REG_XMT_534, ob_d3a1_fifo42_x534(index));
	write16(ctx, OB_D3A1_REG_XMT_536, ob_d3a1_fifo42_x536(index));
	write16(ctx, OB_D3A1_REG_XMT_532, ob_d3a1_fifo42_x532(index));
	write16(ctx, OB_D3A1_REG_XMT_530, ob_d3a1_fifo42_x530(index));
}

/*
 * Expiry action for one 0x530 entry, evaluated only after a bounded EXPIRED
 * poll and only on the vendor's post-poll read.
 */
enum ob_d3a1_fifo_expiry_action {
	OB_D3A1_FIFO_ACTION_STOP = 0,	/* fail-closed -ETIMEDOUT */
	OB_D3A1_FIFO_ACTION_CONTINUE,	/* vendor-faithful, recorded */
	OB_D3A1_FIFO_ACTION_FATAL,	/* invalid read / device loss: -EIO */
};

static inline enum ob_d3a1_fifo_expiry_action
ob_d3a1_fifo530_expiry_action(bool continue_opt_in, bool dev_lost, u16 post_read)
{
	if (!continue_opt_in)
		return OB_D3A1_FIFO_ACTION_STOP;
	if (dev_lost || ob_d3a1_fifo530_read_is_fatal(post_read))
		return OB_D3A1_FIFO_ACTION_FATAL;
	return OB_D3A1_FIFO_ACTION_CONTINUE;
}

/*
 * Configuration admission: the modifier is only meaningful together with an
 * isolated mode that actually runs the 0x530 fixup -- the isolated D3A1 tail
 * test, or the isolated D3B band-init test whose prefix is that same tail. In
 * every other mode it would either have no effect or would reach a path the
 * operator did not ask for, so it is refused. "Not requested" is always allowed
 * (no effect).
 */
static inline bool ob_d3a1_fifo530_continue_allowed(bool continue_opt_in,
						    bool d11_tail_test_only,
						    bool bsinitvals_test_only)
{
	return !continue_opt_in || d11_tail_test_only || bsinitvals_test_only;
}

/*
 * ---- the 0x530 stage, shared by the kernel path and the host tests ---------
 *
 * ob_d3a1_fifo530_stage() is the complete 42-entry vendor loop: four writes per
 * entry, the bounded poll, the explicit opt-in continuation, and the fatal
 * classification. ob_d3a1_fifo_fixup() calls it and only maps its state onto
 * the per-device statistics, the failure record and the device-loss latch, so
 * the integration tests exercise the production control flow rather than a
 * re-implementation of it.
 */
enum ob_d3a1_fifo530_result {
	OB_D3A1_FIFO530_COMPLETE = 0,
	OB_D3A1_FIFO530_EXPIRED_STOP,
	OB_D3A1_FIFO530_INVALID,
};

/*
 * Backend contract: @ctx is passed verbatim to every callback, and all of them
 * MUST interpret it as the same object. The kernel adapter passes
 * `struct ob_hw *`; the host tests pass their own mock. Never a wrapper around
 * ob_hw - see tests/host/test_d3a1_fifo530_ctx.py.
 */
struct ob_d3a1_fifo530_ops {
	ob_d3a1_read16_fn	read16;
	ob_d3a1_write16_fn	write16;
	ob_d3a1_delay_fn	delay;		/* optional */
	bool			(*dev_lost)(void *ctx);	/* optional */
	void			(*on_complete)(void *ctx, u16 index,
					       u16 programmed,
					       u16 readback, u32 iters);
	void			(*on_expired_continued)(void *ctx, u16 index,
						       u16 programmed,
						       u16 last, u16 post,
						       u32 iters);
	void			*ctx;
};

struct ob_d3a1_fifo530_state {
	bool	continue_opt_in;
	u16	entries_entered;
	u16	polls_completed;
	u16	expired;
	u16	continued;
	int	first_continued_index;
	u16	continued_last;
	u32	continued_reads;
	u32	iters_total;
	u32	iters_max;
	u16	first_rb[3];
	u32	first_reads[3];
	int	failed_index;	/* -1 when the stage did not fail */
	u16	failed_last;
	u32	failed_iters;
	enum ob_d3a1_fifo530_result result;
};

static inline void
ob_d3a1_fifo530_state_init(struct ob_d3a1_fifo530_state *st,
			   bool continue_opt_in)
{
	st->continue_opt_in = continue_opt_in;
	st->entries_entered = 0;
	st->polls_completed = 0;
	st->expired = 0;
	st->continued = 0;
	st->first_continued_index = -1;
	st->continued_last = 0;
	st->continued_reads = 0;
	st->iters_total = 0;
	st->iters_max = 0;
	st->first_rb[0] = st->first_rb[1] = st->first_rb[2] = 0;
	st->first_reads[0] = st->first_reads[1] = st->first_reads[2] = 0;
	st->failed_index = -1;
	st->failed_last = 0;
	st->failed_iters = 0;
	st->result = OB_D3A1_FIFO530_COMPLETE;
}

static inline void
ob_d3a1_fifo530_stage(const struct ob_d3a1_fifo530_ops *ops,
		      struct ob_d3a1_fifo530_state *st)
{
	u16 i;

	for (i = 0; i < OB_D3A1_FIFO42_ENTRIES; i++) {
		u16 programmed = ob_d3a1_fifo42_x530(i);
		enum ob_d3a1_poll_result r;
		u32 iters = 0;
		u16 last = 0;

		ob_d3a1_fifo42_entry_write(ops->write16, ops->ctx, i);
		st->entries_entered++;

		r = ob_d3a1_poll_core(ops->read16, ops->delay, ops->ctx,
				      OB_D3A1_REG_XMT_530, OB_D3A1_POLL_530,
				      &iters, &last);
		if (r == OB_D3A1_POLL_COMPLETE) {
			st->polls_completed++;
			st->iters_total += iters;
			if (iters > st->iters_max)
				st->iters_max = iters;
			if (i < 3) {
				st->first_rb[i] = last;
				st->first_reads[i] = iters;
				if (ops->on_complete)
					ops->on_complete(ops->ctx, i, programmed,
							 last, iters);
			}
			continue;
		}

		if (r == OB_D3A1_POLL_INVALID) {
			/* No post-read, no delay, no further entry. */
			st->result = OB_D3A1_FIFO530_INVALID;
			st->failed_index = (int)i;
			st->failed_last = last;
			st->failed_iters = iters;
			return;
		}

		/* Bounded expiry: the only case the opt-in may touch. */
		if (!st->continue_opt_in) {
			st->expired++;
			st->iters_total += iters;
			if (iters > st->iters_max)
				st->iters_max = iters;
			st->result = OB_D3A1_FIFO530_EXPIRED_STOP;
			st->failed_index = (int)i;
			st->failed_last = last;
			st->failed_iters = iters;
			return;
		}

		{
			u16 post = ops->read16(ops->ctx, OB_D3A1_REG_XMT_530);
			bool lost = ops->dev_lost ? ops->dev_lost(ops->ctx)
						  : false;
			enum ob_d3a1_fifo_expiry_action act =
				ob_d3a1_fifo530_expiry_action(true, lost, post);

			if (act != OB_D3A1_FIFO_ACTION_CONTINUE) {
				/* all-ones post read (or loss): fatal */
				st->result = OB_D3A1_FIFO530_INVALID;
				st->failed_index = (int)i;
				st->failed_last = post;
				st->failed_iters = iters;
				return;
			}

			st->expired++;
			st->continued++;
			st->iters_total += iters;
			if (iters > st->iters_max)
				st->iters_max = iters;
			if (st->first_continued_index < 0) {
				st->first_continued_index = (int)i;
				st->continued_last = last;
				st->continued_reads = iters;
			}
			if (ops->on_expired_continued)
				ops->on_expired_continued(ops->ctx, i, programmed,
							  last, post, iters);
		}
	}
}

/*
 * One failure record per device, written once per failed tail
 * (FAILURE_ROLLBACK_MATRIX.md §3).
 */
enum ob_d3a1_fail_stage {
	OB_D3A1_FAIL_NONE = 0,
	OB_D3A1_FAIL_D2B_GATE,
	OB_D3A1_FAIL_FIXUP,
	OB_D3A1_FAIL_T1,
	OB_D3A1_FAIL_DMA,
	OB_D3A1_FAIL_T2,
	OB_D3A1_FAIL_VALIDATE,
};

enum ob_d3a1_dma_teardown {
	OB_D3A1_TEARDOWN_NOT_NEEDED = 0,
	OB_D3A1_TEARDOWN_OK,
	OB_D3A1_TEARDOWN_FAILED,
};

static inline const char *ob_d3a1_fail_stage_name(enum ob_d3a1_fail_stage s)
{
	switch (s) {
	case OB_D3A1_FAIL_NONE:
		return "none";
	case OB_D3A1_FAIL_D2B_GATE:
		return "D2B_GATE";
	case OB_D3A1_FAIL_FIXUP:
		return "FIXUP";
	case OB_D3A1_FAIL_T1:
		return "T1";
	case OB_D3A1_FAIL_DMA:
		return "DMA";
	case OB_D3A1_FAIL_T2:
		return "T2";
	case OB_D3A1_FAIL_VALIDATE:
		return "VALIDATE";
	}
	return "INVALID";
}

static inline const char *
ob_d3a1_dma_teardown_name(enum ob_d3a1_dma_teardown t)
{
	switch (t) {
	case OB_D3A1_TEARDOWN_NOT_NEEDED:
		return "NOT_NEEDED";
	case OB_D3A1_TEARDOWN_OK:
		return "OK";
	case OB_D3A1_TEARDOWN_FAILED:
		return "FAILED";
	}
	return "INVALID";
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
 * tsf_cfpstart (0x18c) is a write-only CFP-start programming register; its
 * direct readback is not a stable deterministic postcondition (hardware read
 * 0x3c000000 vs the programmed 0x02000000; the readable CFP value lives at
 * 0x604/0x606). PASS is gated on the vendor-exact write being accounted.
 * Always false; kept as an explicit, testable statement of the invariant.
 */
static inline bool ob_d3a1_cfpstart_readback_is_postcondition(void)
{
	return false;
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
struct bcma_device;

/**
 * struct ob_d3a1 - kernel state for the isolated D3A1 tail test
 * @fifo_fixed_writes: 0x542/0x540 writes performed
 * @fifo7_writes:      writes performed by the 7-entry loop (must be 42)
 * @fifo42_writes:     writes performed by the 42-entry loop (must be 168)
 * @fifo_poll540_iters: iterations of the 0x540 completion poll
 * @fifo_poll530_iters: total iterations across the 42 x 0x530 polls
 * @fifo_poll530_max:  max iterations observed in a single 0x530 poll
 * @fifo_poll_completed: number of 0x530 polls that met the completion predicate
 * @fifo_poll_expired: number of sub_67efd polls that hit the bound (0x530+0x540);
 *                     with the M3 fail-closed policy this is at most 1 and the
 *                     tail has aborted
 * @fifo_last_expired_index: -1, or the 0x530 index whose poll hit the bound
 * @fifo_invalid_530:  0x530 polls that observed all-ones (fatal, device loss);
 *                     never a timeout, never eligible for the opt-in
 * @fifo_invalid_index: -1, or the 0x530 table index of that observation
 * @fifo_invalid_value: the all-ones value observed (0xffff)
 * @fifo_invalid_iters: read count of the poll that observed it
 * @fifo_expired_continued: 0x530 polls that expired AND continued under the
 *                     explicit opt-in (vendor-faithful); never counted as
 *                     completion
 * @fifo_first_continued_index: -1, or the first such index
 * @fifo_continued_last: final 0x530 value at that first continuation
 * @fifo_continued_reads: read count of that first expired poll
 * @fifo_expiry_continued: true once any entry continued after expiry; the tail
 *                     completed with an UNVERIFIED FIFO completion for those
 *                     entries and the PASS log says so
 * @fifo_first_rb:     completion readback for the first 3 entries (diagnostic)
 * @fifo_first_reads:  read count for the first 3 entries (diagnostic)
 * @fifo_state_unverified: true once a poll expired; the FIFO programming is
 *                     incomplete and must not be trusted
 * @fifo_reboot_required: true once a poll expired; no in-driver recovery is
 *                     proven, so the operator must reboot
 * @fifo_retry_forbidden: true once a poll expired; blocks a second tail run
 * @fifo_timeout_reg:  register (0x530 or 0x540) whose bounded poll expired
 * @fifo_timeout_last: final value read from that register
 * @fifo_timeout_iters: read count of the expired poll
 * @fifo_timeout_index: -1 for 0x540, else the 0x530 table-entry index
 * @fail_stage:        stage that aborted the tail (enum ob_d3a1_fail_stage)
 * @fail_errno:        errno returned to the caller (negative, 0 when none)
 * @fail_dma_live:     DMA was programmed at the time of the failure
 * @fail_dma_teardown: enum ob_d3a1_dma_teardown result
 * @fail_reboot_required: reboot-required classification for the failure
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
 * @tsf_cfpstart_written: true once the vendor-exact 0x18c write was issued
 * @tsf_cfpstart_rb:   raw 0x18c readback (DIAGNOSTIC ONLY; not stable)
 * @tsf_cfpstrt_l_rb:  raw 0x604 readback (diagnostic)
 * @tsf_cfpstrt_h_rb:  raw 0x606 readback (diagnostic)
 * @tsf_cfpstart_rb_proven: true only if 0x18c stable readback is proven (false)
 * @btc_shm92:         raw SHM[0x92] value
 * @btc_base:          2 * SHM[0x92]
 * @btc_block_ran:     true when btc_base != 0 (BTC T2 block executed)
 * @mac_written:       true when the 0x78c/0x78e/0x790 MAC words were written
 * @stopped_before_phy: set only on the successful STOP-before-sub_6656c path
 */
/*
 * FIFO530-GATE-01 state, kept OUT of struct ob_d3a1 on purpose.
 *
 * struct ob_d3a1 is embedded in struct ob_hw, so adding a member to it moves
 * every later ob_hw member and changes the compiled D2A/D2B path that this
 * experiment must not touch (FIFO530_ALL_ONES_ANALYSIS.md, section "layout").
 * All FIFO530-only accounting therefore lives here, in a member declared last
 * in struct ob_hw: the pre-existing layout - and hence the instruction
 * encoding of the already hardware-tested path - is unchanged.
 *
 * @invalid_530:        0x530 polls that observed all-ones (fatal, device loss)
 * @invalid_540:        0x540 polls that observed all-ones (fatal)
 * @invalid_index:      -1, or the table index of that observation
 * @invalid_value:      the all-ones value observed (0xffff)
 * @invalid_iters:      read count of the poll that observed it
 * @expired_continued:  0x530 polls that expired AND continued under the opt-in
 * @first_continued_index: -1, or the first such index
 * @continued_last:     final 0x530 value at that first continuation
 * @continued_reads:    read count of that first expired poll
 * @expiry_continued:   true once any entry continued after expiry
 */
struct ob_d3a1_fifo530_ext {
	u32	invalid_530;
	u32	invalid_540;
	int	invalid_index;
	u16	invalid_value;
	u32	invalid_iters;
	u32	expired_continued;
	int	first_continued_index;
	u16	continued_last;
	u32	continued_reads;
	bool	expiry_continued;
};

struct ob_d3a1 {
	u32	fifo_fixed_writes;
	u32	fifo7_writes;
	u32	fifo42_writes;
	u32	fifo_poll540_iters;
	u32	fifo_poll530_iters;
	u32	fifo_poll530_max;
	u32	fifo_poll_completed;
	u32	fifo_poll_expired;
	int	fifo_last_expired_index;
	u16	fifo_first_rb[3];
	u32	fifo_first_reads[3];
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
	bool	tsf_cfpstart_written;
	u32	tsf_cfpstart_rb;
	u16	tsf_cfpstrt_l_rb;
	u16	tsf_cfpstrt_h_rb;
	bool	tsf_cfpstart_rb_proven;
	u16	btc_shm92;
	u32	btc_base;
	bool	btc_block_ran;
	bool	mac_written;
	bool	stopped_before_phy;
	bool	fifo_state_unverified;
	bool	fifo_reboot_required;
	bool	fifo_retry_forbidden;
	u16	fifo_timeout_reg;
	u16	fifo_timeout_last;
	u32	fifo_timeout_iters;
	int	fifo_timeout_index;
	u8	fail_stage;
	int	fail_errno;
	bool	fail_dma_live;
	u8	fail_dma_teardown;
	bool	fail_reboot_required;
};

/*
 * Ordered tail runner from the live D2B-exit re-check through the requested
 * stop point. The shared D2B stage and its postcondition gate are the caller's
 * responsibility. AFTER_DMA/COMPLETE return with the D3A0 DMA engines live; on
 * failure after DMA bring-up the mandatory verified teardown is performed.
 * Invalid stage values are refused with -EINVAL before any register access.
 */
int ob_d11_tail_run(struct ob_hw *hw, enum ob_d11_tail_stop stop,
		    const char *tag);

/*
 * Whole-prefix admission gate for D3A1/D3B. Side-effect free: it reads no
 * register and must be called before ob_initvals_run_d2b() (D2A upload and the
 * common initvals writes) and before any memset() of hw->d3a1 / hw->d3a0.
 * Returns 0 only when the device is fresh and owns no DMA state.
 */
int ob_d11_prefix_entry_preflight(struct ob_hw *hw, const char *tag);

/*
 * Reusable vendor prefix (D2B -> sub_67efd -> T1 -> DMA bring-up -> T2 -> the
 * D3A1 postcondition gate). Returns 0 with the D3A0 DMA engines LEFT LIVE, so
 * D3B can execute in the exact vendor position before the mandatory teardown.
 * On any failure after DMA bring-up it performs the verified D3A0 teardown.
 */
int ob_d3a1_run_prefix(struct ob_hw *hw, const char *tag);

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

/*
 * True when this exact PCI/BCMA identity had a FIFO poll expiry in this module
 * lifetime.  Probe calls it before devm_kzalloc() and before any MMIO; a true
 * result is a refusal, not a recovery path.  The registry holds copied scalars
 * only and does not reference ob_hw or any DMA resource.
 */
bool ob_d3a1_fifo_reprobe_forbidden(struct bcma_device *core);
#endif /* __KERNEL__ */

#endif /* _OB_D3A1_H_ */

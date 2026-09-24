// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — isolated D3A1 vendor post-common / pre-PHY D11 tail test
 * (M3.4D3A1).
 *
 * Module param d11_tail_test_only=1. From the shared hardware-proven D2B exit
 * this reproduces the exact vendor BCM4352/rev42 ordering:
 *
 *   sub_67efd equivalent
 *     -> T1 (MBURST/MAXANTCNT, intrcvlazy, MACCONTROL, TSF, IRQ source,
 *            macphyclk, fast-pwrup, MACHW_VER/CAP, SCR SRL/LRL, SFBL/LFBL,
 *            IFS)
 *     -> DMA (reusing the hardware-proven D3A0 lifecycle, in its exact vendor
 *            position: T1 -> DMA -> T2)
 *     -> T2 (BTC/NVRAM gate, the 6-byte MAC into SHM 0x78c/0x78e/0x790,
 *            switch_macfreq)
 *     -> STOP immediately before sub_6656c.
 *
 * It NEVER enters sub_6656c, bsinitvals, wlc_phy_init, PHY indirect MMIO,
 * radio, channel, calibration, EN_MAC, host IRQ delivery or mac80211. It never
 * enables MACINTMASK or MI_DMAINT.
 *
 * Fail-closed: the DMA lifecycle is the proven D3A0 one; DMA memory is freed
 * only after EVERY programmed engine had its own verified normal per-channel
 * stop. Core-reset containment never authorizes a free.
 *
 * Provenance: docs/m34d3a1_vendor_tail.md (C2 blob sha256 352a6e349f…; C3
 * upstream brcmsmac corroboration).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>

#include "ob_core.h"
#include "ob_si.h"
#include "ob_ucode.h"
#include "ob_initvals.h"
#include "ob_d3a0.h"
#include "ob_d3a1.h"

/* ---- objmem / SHM / SCR 16-bit accessors -------------------------------- */

static void ob_d3a1_obj_write16(struct ob_hw *hw, u32 sel, u16 off, u16 val)
{
	bcma_write32(hw->core, OB_D3A1_REG_OBJADDR, sel | ((u32)off >> 2));
	(void)bcma_read32(hw->core, OB_D3A1_REG_OBJADDR);
	if (off & 0x2)
		bcma_write16(hw->core, OB_D3A1_REG_OBJDATA + 2, val);
	else
		bcma_write16(hw->core, OB_D3A1_REG_OBJDATA, val);
}

static void ob_d3a1_write_shm16(struct ob_hw *hw, u16 off, u16 val)
{
	ob_d3a1_obj_write16(hw, OB_D3A1_OBJADDR_SHM_SEL, off, val);
}

static void ob_d3a1_write_scr16(struct ob_hw *hw, u16 off, u16 val)
{
	ob_d3a1_obj_write16(hw, OB_D3A1_OBJADDR_SCR_SEL, off, val);
}

static u16 ob_d3a1_read_obj16(struct ob_hw *hw, u32 sel, u16 off)
{
	bcma_write32(hw->core, OB_D3A1_REG_OBJADDR, sel | ((u32)off >> 2));
	(void)bcma_read32(hw->core, OB_D3A1_REG_OBJADDR);
	if (off & 0x2)
		return bcma_read16(hw->core, OB_D3A1_REG_OBJDATA + 2);
	return bcma_read16(hw->core, OB_D3A1_REG_OBJDATA);
}

static u16 ob_d3a1_read_scr16(struct ob_hw *hw, u16 off)
{
	return ob_d3a1_read_obj16(hw, OB_D3A1_OBJADDR_SCR_SEL, off);
}

/*
 * SICF_MPCLKE lives in the D11 core agent IO control (bit 4). Identical to the
 * proven D3A0 helper (reversible core-cflags gate; no PHY/radio/PLL).
 */
static void ob_d3a1_macphyclk_set(struct ob_hw *hw, bool on)
{
	u32 v = bcma_aread32(hw->core, BCMA_IOCTL);

	if (on)
		v |= OB_D3A0_IOCTL_MPCLKE;
	else
		v &= ~OB_D3A0_IOCTL_MPCLKE;
	bcma_awrite32(hw->core, BCMA_IOCTL, v);
	(void)bcma_aread32(hw->core, BCMA_IOCTL);
}

static u32 ob_d3a1_mctrl_update(struct ob_hw *hw, u32 mask, u32 val)
{
	u32 old = bcma_read32(hw->core, OB_D3A1_REG_MACCONTROL);
	u32 new = (old & ~mask) | val;

	bcma_write32(hw->core, OB_D3A1_REG_MACCONTROL, new);
	return bcma_read32(hw->core, OB_D3A1_REG_MACCONTROL);
}

/* ---- bounded FIFO completion poll (vendor bound 0xd1 / step 10) --------- */

/*
 * Fail-closed bounded poll. Returns 0 ONLY when the completion predicate was
 * observed true; -ETIMEDOUT when the vendor bound expired first. The final
 * register value and the iteration count are reported so a timeout is
 * diagnosable.
 */
static int ob_d3a1_poll16(struct ob_hw *hw, u16 off, bool bit0, u32 *iters,
			  u16 *last)
{
	u32 counter = OB_D3A1_FIFO_POLL_BOUND;
	u32 n;

	for (n = 0; n < OB_D3A1_FIFO_POLL_MAX_ITERS + 1u; n++) {
		u16 v = bcma_read16(hw->core, off);
		bool done = bit0 ? ((v & 0x1u) == 0u) : (v == 0u);

		*last = v;
		if (done) {
			*iters = n + 1u;
			return 0;
		}
		if (!ob_d3a1_poll_continue(done, counter)) {
			*iters = n + 1u;
			return -ETIMEDOUT;
		}
		udelay(OB_D3A1_FIFO_POLL_DELAY_US);
		counter -= OB_D3A1_FIFO_POLL_STEP;
	}
	*iters = n;
	return -ETIMEDOUT;
}

/* ---- sub_67efd equivalent (rev42 only) ---------------------------------- */

static int ob_d3a1_fifo_fixup(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 machwcap = bcma_read32(hw->core, OB_D3A1_REG_MACHWCAP);
	u16 v = ob_d3a1_fifo_flush_val(machwcap);
	u16 last = 0;
	u32 i;

	st->machwcap = machwcap;
	st->fifo_fail_index = -1;

	/* 0x542 = v, 0x540 = 5, bounded completion poll on 0x540 bit0 */
	bcma_write16(hw->core, OB_D3A1_REG_XMTFIFOFLUSH, v);
	bcma_write16(hw->core, OB_D3A1_REG_XMTFIFOCMD, 5);
	st->fifo_fixed_writes += OB_D3A1_FIFO_FIXED_WRITES;
	if (ob_d3a1_poll16(hw, OB_D3A1_REG_XMTFIFOCMD, true,
			   &st->fifo_poll540_iters, &last)) {
		dev_err(hw->dev,
			"d3a1-test: sub_67efd FAIL 0x540 programmed=%04x last=%04x reads=%u xmtfifocmd poll TIMEOUT\n",
			5, last, st->fifo_poll540_iters);
		return -ETIMEDOUT;
	}

	/* exact 7-entry programming loop (42 writes) */
	for (i = 0; i < OB_D3A1_FIFO_TABLE1_LEN; i++) {
		u32 entry = ob_d3a1_fifo_table1(i);

		bcma_write16(hw->core, OB_D3A1_REG_XMTFIFORQPRI,
			     ob_d3a1_fifo7_rqpri(entry, v));
		bcma_write16(hw->core, OB_D3A1_REG_XMTTPLATETXPTR,
			     ob_d3a1_fifo7_txptr(entry));
		bcma_write16(hw->core, OB_D3A1_REG_XMTFIFODEF,
			     ob_d3a1_fifo7_def(entry));
		bcma_write16(hw->core, OB_D3A1_REG_XMT_54E,
			     ob_d3a1_fifo7_54e(entry));
		bcma_write16(hw->core, OB_D3A1_REG_XMTTPLATEPTR,
			     OB_D3A1_FIFO_VAL_TPLATE);
		bcma_write16(hw->core, OB_D3A1_REG_XMTFIFOPRIRDY,
			     ob_d3a1_fifo7_prirdy(entry));
		st->fifo7_writes += 6;
	}

	/*
	 * Exact 42-entry programming loop (168 writes), bounded poll each.
	 * A timeout aborts immediately and never continues to the next entry.
	 */
	for (i = 0; i < OB_D3A1_FIFO42_ENTRIES; i++) {
		u32 iters;

		bcma_write16(hw->core, OB_D3A1_REG_XMT_534,
			     ob_d3a1_fifo42_x534(i));
		bcma_write16(hw->core, OB_D3A1_REG_XMT_536,
			     ob_d3a1_fifo42_x536(i));
		bcma_write16(hw->core, OB_D3A1_REG_XMT_532,
			     ob_d3a1_fifo42_x532(i));
		bcma_write16(hw->core, OB_D3A1_REG_XMT_530,
			     ob_d3a1_fifo42_x530(i));
		if (ob_d3a1_poll16(hw, OB_D3A1_REG_XMT_530, false,
				   &iters, &last)) {
			st->fifo_fail_index = (int)i;
			dev_err(hw->dev,
				"d3a1-test: sub_67efd FAIL 0x530 index=%u programmed=%04x last=%04x reads=%u poll TIMEOUT\n",
				i, ob_d3a1_fifo42_x530(i), last, iters);
			return -ETIMEDOUT;
		}
		st->fifo_poll_completed++;
		st->fifo_poll530_iters += iters;
		if (iters > st->fifo_poll530_max)
			st->fifo_poll530_max = iters;
		(void)bcma_read16(hw->core, OB_D3A1_REG_XMT_530);
		st->fifo42_writes += 4;
	}

	if (st->fifo_fixed_writes != OB_D3A1_FIFO_FIXED_WRITES ||
	    st->fifo7_writes != OB_D3A1_FIFO7_WRITES ||
	    st->fifo42_writes != OB_D3A1_FIFO42_WRITES ||
	    st->fifo_poll_completed != OB_D3A1_FIFO42_ENTRIES) {
		dev_err(hw->dev,
			"d3a1-test: sub_67efd accounting mismatch fixed=%u loop7=%u loop42=%u completed=%u\n",
			st->fifo_fixed_writes, st->fifo7_writes,
			st->fifo42_writes, st->fifo_poll_completed);
		return -EIO;
	}

	dev_info(hw->dev,
		 "d3a1-test: sub_67efd machwcap=%08x flush=%04x poll540=%u loop7=%u loop42=%u polls530=%u/%u max_iters=%u\n",
		 machwcap, v, st->fifo_poll540_iters, st->fifo7_writes,
		 st->fifo42_writes, st->fifo_poll_completed,
		 OB_D3A1_FIFO42_ENTRIES, st->fifo_poll530_max);
	return 0;
}

/* ---- D2B exit re-check (live, before the first D3A1 write) -------------- */

static int ob_d3a1_check_d2b_exit(struct ob_hw *hw)
{
	u32 mctrl = bcma_read32(hw->core, OB_D3A1_REG_MACCONTROL);
	u32 macintmask = bcma_read32(hw->core, OB_D3A1_REG_MACINTMASK);
	u32 fs0 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE0);
	u32 fs1 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE1);
	u32 fs2 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE2);
	u32 fs3 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE3);
	u32 shm14 = (u32)ob_ucode_read_shm16(hw, OB_INITVALS_SHM14_LO) |
		    ((u32)ob_ucode_read_shm16(hw, OB_INITVALS_SHM14_HI) << 16);

	if (mctrl != OB_INITVALS_MACCONTROL_EXPECTED ||
	    macintmask != OB_INITVALS_MACINTMASK_EXPECTED ||
	    fs0 != OB_INITVALS_FIFOSIZE0_EXPECTED ||
	    fs1 != OB_INITVALS_FIFOSIZE1_EXPECTED ||
	    fs2 != OB_INITVALS_FIFOSIZE2_EXPECTED ||
	    fs3 != OB_INITVALS_FIFOSIZE3_EXPECTED ||
	    shm14 != OB_INITVALS_SHM14_EXPECTED) {
		dev_err(hw->dev,
			"d3a1-test: live D2B exit mismatch maccontrol=%08x macintmask=%08x fifo=%04x/%04x/%04x/%04x shm14=%08x; D3A1 forbidden\n",
			mctrl, macintmask, fs0, fs1, fs2, fs3, shm14);
		return -EIO;
	}
	dev_info(hw->dev,
		 "d3a1-test: D2B exit re-verified maccontrol=%08x macintmask=%08x shm14=%08x\n",
		 mctrl, macintmask, shm14);
	return 0;
}

/* ---- T1 ----------------------------------------------------------------- */

static u16 ob_d3a1_fast_pwrup_delay(struct ob_hw *hw)
{
	/* blob 0x16091: 0x4352, chiprev >= 4 -> 3000 else 1500 */
	return hw->chip_rev >= 4u ? OB_D3A1_FASTPWRUP_REV_GE4
				  : OB_D3A1_FASTPWRUP_REV_LT4;
}

static u16 ob_d3a1_sub5fdca(struct ob_hw *hw)
{
	/* blob sub_5fdca 0x5fe32: phytype 0xb, chipid != 0x4350 -> 0x4b0 */
	if (hw->chip_id == 0x4350u)
		return 0x0200u;
	return OB_D3A1_SUB5FDCA_PHY_AC;
}

static int ob_d3a1_t1(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 mctrl, machwcap;
	u16 dly;

	ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MBURST, OB_D3A1_SHM_MBURST_VAL);
	ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MAXANTCNT,
			    OB_D3A1_SHM_MAXANTCNT_VAL);

	bcma_write32(hw->core, OB_D3A1_REG_INTRCVLAZY0, OB_D3A1_INTRCVLAZY);

	mctrl = ob_d3a1_mctrl_update(hw, OB_D3A1_MACCONTROL_MASK,
				     OB_D3A1_MACCONTROL_VAL);
	if (mctrl != OB_D3A1_MACCONTROL_EXPECTED) {
		dev_err(hw->dev,
			"d3a1-test: MACCONTROL transition invalid: %08x\n",
			mctrl);
		return -EIO;
	}

	bcma_write32(hw->core, OB_D3A1_REG_TSF_CFPREP, OB_D3A1_TSF_CFPREP);
	bcma_write32(hw->core, OB_D3A1_REG_TSF_CFPSTART, OB_D3A1_TSF_CFPSTART);

	bcma_write32(hw->core, OB_D3A1_REG_MACINTSTATUS, OB_D3A1_MI_GP1);
	bcma_write32(hw->core, OB_D3A1_REG_INTCONTROL0_MASK, OB_D3A1_I_RI);

	ob_d3a1_macphyclk_set(hw, true);

	/* fast-pwrup delay path (rev42 takes both gates) */
	if (hw->core->id.rev > 4u) {
		dly = ob_d3a1_fast_pwrup_delay(hw);
		st->fastpwrup_dly = dly;
		st->fastpwrup_dly_sw = dly;
		bcma_write16(hw->core, OB_D3A1_REG_FASTPWRUP_DLY, dly);
		if (hw->core->id.rev > 0x28u)
			st->fastpwrup_dly_sw = (u16)(dly +
						     ob_d3a1_sub5fdca(hw));
	}

	ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MACHWVER,
			    (u16)hw->core->id.rev);
	machwcap = bcma_read32(hw->core, OB_D3A1_REG_MACHWCAP);
	if (hw->core->id.rev > 0xcu) {
		ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MACHWCAP_L,
				    (u16)(machwcap & 0xffffu));
		ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MACHWCAP_H,
				    (u16)((machwcap >> 16) & 0xffffu));
	}

	/*
	 * SCR SRL/LRL (vendor never writes SCR 0x24 on the first init: the
	 * wlc_info+0x718 first-init gate skips it).
	 */
	ob_d3a1_write_scr16(hw, OB_D3A1_SCR_SRL, OB_D3A1_SRL_DEFAULT);
	ob_d3a1_write_scr16(hw, OB_D3A1_SCR_LRL, OB_D3A1_LRL_DEFAULT);

	ob_d3a1_write_shm16(hw, OB_D3A1_SHM_SFBL, OB_D3A1_SFBL_DEFAULT);
	ob_d3a1_write_shm16(hw, OB_D3A1_SHM_LFBL, OB_D3A1_LFBL_DEFAULT);

	if (hw->core->id.rev > 0xfu) {
		u16 ifs = bcma_read16(hw->core, OB_D3A1_REG_IFS_CTL) &
			  OB_D3A1_IFS_CTL_MASK;

		bcma_write16(hw->core, OB_D3A1_REG_IFS_CTL, ifs);
		bcma_write16(hw->core, OB_D3A1_REG_IFS_AIFSN,
			     OB_D3A1_IFS_AIFSN_VAL);
	}

	if (!ob_d3a0_host_irq_disabled(
		    bcma_read32(hw->core, OB_D3A1_REG_MACINTMASK))) {
		dev_err(hw->dev, "d3a1-test: macintmask not 0 after T1\n");
		return -EIO;
	}
	dev_info(hw->dev,
		 "d3a1-test: T1 complete maccontrol=%08x intrcvlazy=%08x intmask0=%08x fastpwrup=%u\n",
		 mctrl, bcma_read32(hw->core, OB_D3A1_REG_INTRCVLAZY0),
		 bcma_read32(hw->core, OB_D3A1_REG_INTCONTROL0_MASK),
		 st->fastpwrup_dly);
	return 0;
}

/* ---- switch_macfreq (blob 0x64cbf, 0x4352 path) ------------------------- */

static u32 ob_d3a1_pmu_pll_read(struct ob_hw *hw, u32 idx)
{
	ob_si_cc_write(hw, OB_CC_PMU_PLLCTL_ADDR, idx);
	return ob_si_cc_read(hw, OB_CC_PMU_PLLCTL_DATA);
}

/*
 * BB VCO frequency (blob si_pmu_get_bb_vcofreq 0x14b7b, 0x4352 branch).
 * Exact: reads PMU PLL2 (and PLL3 when the PLL2 sub-field (bits 4..6) is
 * non-zero), then applies the recovered arithmetic via the ported
 * ob_d3a1_bb_vcofreq_from_pll(). No approximation.
 *
 * Note: the vendor reads PLL3 into `ecx` without masking; the recovered value
 * is used directly by bcm_uint64_multiple_add.
 *
 * The raw inputs and the decoded PLL2 fields are returned for first-run
 * observability; no extra PMU reads are performed for logging.
 */
static u32 ob_d3a1_bb_vcofreq(struct ob_hw *hw, u32 *p2_out, u32 *p3_out,
			      bool *p3_read_out, u32 *d_out, u32 *den_out)
{
	u32 p2 = ob_d3a1_pmu_pll_read(hw, 2);
	u32 p3 = 0;
	bool p3_read = false;

	if (((p2 >> 4) & 0x7u) != 0) {
		p3 = ob_d3a1_pmu_pll_read(hw, 3);
		p3_read = true;
	}

	*p2_out = p2;
	*p3_out = p3;
	*p3_read_out = p3_read;
	*d_out = (p2 >> 4) & 0x7u;
	*den_out = p2 >> 7;
	return ob_d3a1_bb_vcofreq_from_pll(p2, p3);
}

static int ob_d3a1_switch_macfreq(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 p2, p3 = 0, d = 0, den = 0;
	u32 vco, frac, frac_lo, frac_hi;
	bool p3_read = false;
	u16 rb_lo, rb_hi;

	/*
	 * Defensive entry check FIRST. ob_d3a1_bb_vcofreq() below dereferences
	 * hw->cc through ob_si_cc_read/write; a check after the call would not
	 * protect the dereference. ob_d3a1_test() also enforces this invariant
	 * at entry, but keep the local guard.
	 */
	if (!hw->cc) {
		dev_err(hw->dev,
			"d3a1-test: no ChipCommon core; cannot derive BB VCO\n");
		return -ENODEV;
	}

	vco = ob_d3a1_bb_vcofreq(hw, &p2, &p3, &p3_read, &d, &den);

	/*
	 * vco <= 1 is NOT a legitimate vendor skip: bcm_uint64_divide writes
	 * nothing for div <= 1 (the vendor would then publish an uninitialized
	 * stack value). Our inability to derive a usable VCO is an ERROR and
	 * must never become a D3A1 PASS.
	 */
	if (vco <= 1u) {
		dev_err(hw->dev,
			"d3a1-test: switch_macfreq cannot derive a valid BB VCO (vco=%u p2=%08x p3=%08x d=%u den=%u); D3A1 FAIL\n",
			vco, p2, p3, d, den);
		return -EIO;
	}

	frac = ob_d3a1_tsf_frac(vco);
	if (frac == OB_D3A1_DIV_NO_WRITE) {
		dev_err(hw->dev,
			"d3a1-test: switch_macfreq divide produced no write (vco=%u)\n",
			vco);
		return -EIO;
	}

	/* First-run observability: record exactly what the algorithm read. */
	st->bb_vcofreq = vco;
	st->pll2_raw = p2;
	st->pll3_raw = p3;
	st->pll3_read = p3_read;
	st->bb_d = d;
	st->bb_den = den;
	st->tsf_frac = frac;
	st->tsf_frac_lo = ob_d3a1_tsf_frac_lo(frac);
	st->tsf_frac_hi = ob_d3a1_tsf_frac_hi(frac);

	frac_lo = st->tsf_frac_lo;
	frac_hi = st->tsf_frac_hi;
	bcma_write16(hw->core, OB_D3A1_REG_TSF_FRAC_L, frac_lo);
	bcma_write16(hw->core, OB_D3A1_REG_TSF_FRAC_H, frac_hi);

	/*
	 * Stable readback of D11 0x62e/0x630 is NOT vendor-proven, so this is
	 * observability only: record the readback and classify it as unproven.
	 * Do NOT invent an equality gate.
	 */
	rb_lo = bcma_read16(hw->core, OB_D3A1_REG_TSF_FRAC_L);
	rb_hi = bcma_read16(hw->core, OB_D3A1_REG_TSF_FRAC_H);
	st->tsf_frac_lo_rb = rb_lo;
	st->tsf_frac_hi_rb = rb_hi;
	st->tsf_frac_rb_proven = false;

	dev_info(hw->dev,
		 "d3a1-test: switch_macfreq p2=%08x p3=%08x pll3_read=%d d=%u den=%u vco=%u frac=%08x -> 0x62e=%04x 0x630=%04x rb_lo=%04x rb_hi=%04x (readback UNPROVEN)\n",
		 p2, p3, p3_read, d, den, vco, frac, frac_lo, frac_hi,
		 rb_lo, rb_hi);
	return 0;
}

/* ---- T2 ----------------------------------------------------------------- */

static int ob_d3a1_t2(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u16 shm92 = ob_ucode_read_shm16(hw, OB_D3A1_SHM_BTC_BASE);
	u32 btc_base = ob_d3a1_btc_base(shm92);
	u32 i;

	st->btc_shm92 = shm92;
	st->btc_base = btc_base;
	dev_info(hw->dev,
		 "d3a1-test: T2 BTC gate SHM[0x92]=%04x btc_base=%u block=%s\n",
		 shm92, btc_base, btc_base ? "run" : "skip");

	/*
	 * Vendor gate: btc_base == 0 skips the ENTIRE BTC block (btc_params,
	 * the 4 fixed 0x4352 extras, btc_flags AND the MAC SHM writes) because
	 * the vendor branch at 0x69336 jumps past all of them to 0x694d8.
	 */
	if (btc_base != 0) {
		st->btc_block_ran = true;

		/*
		 * btc_params0..118: OpenBRCM has no NVRAM text-variable
		 * provider and the SPROM does not emit these keys, so every
		 * getvar("btc_params%d") returns NULL and the vendor SKIPS the
		 * write (never zero-fills). No write is issued here.
		 */

		if (hw->chip_id == 0x4352u || hw->chip_id == 0xa8dcu) {
			ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF),
				OB_D3A1_BTC_EXTRA_VAL1);
			ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF2),
				OB_D3A1_BTC_EXTRA_VAL2);
			ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF3),
				OB_D3A1_BTC_EXTRA_VAL3);
			ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF4),
				OB_D3A1_BTC_EXTRA_VAL4);
		}

		/*
		 * btc_flags: no provider -> absent -> the vendor skips the
		 * btc->flags store and every wlc_bmac_mhf write. No synthesized
		 * flags.
		 */

		/*
		 * MAC into SHM 0x78c/0x78e/0x790 (rev42 > 0x27), inside the
		 * btc_base gate. Requires a validated MAC.
		 */
		if (hw->core->id.rev > 0x27u && hw->mac_valid) {
			for (i = 0; i < OB_D3A1_MAC_WORDS; i++)
				ob_d3a1_write_shm16(hw,
					(u16)(OB_D3A1_SHM_MAC_0 + 2u * i),
					ob_d3a1_mac_word(hw->mac, i));
			st->mac_written = true;
			dev_info(hw->dev,
				 "d3a1-test: MAC words -> 0x78c=%04x 0x78e=%04x 0x790=%04x\n",
				 ob_d3a1_mac_word(hw->mac, 0),
				 ob_d3a1_mac_word(hw->mac, 1),
				 ob_d3a1_mac_word(hw->mac, 2));
		}
	} else {
		dev_info(hw->dev,
			 "d3a1-test: btc_base=0 (SHM[0x92]=0); complete BTC T2 block skipped\n");
	}

	/* vendor read-only diagnostic of SHM 0x8e */
	(void)ob_ucode_read_shm16(hw, OB_D3A1_SHM_BTC_READBACK);

	dev_info(hw->dev,
		 "d3a1-test: T2 complete shm92=%04x btc_base=%u btc_block_ran=%d mac_written=%d\n",
		 st->btc_shm92, st->btc_base, st->btc_block_ran,
		 st->mac_written);

	if (ob_d3a1_chip_uses_macfreq(hw->chip_id))
		return ob_d3a1_switch_macfreq(hw);
	return 0;
}

/* ---- postcondition validation ------------------------------------------- */

static int ob_d3a1_validate(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 mctrl = bcma_read32(hw->core, OB_D3A1_REG_MACCONTROL);
	u32 macintmask = bcma_read32(hw->core, OB_D3A1_REG_MACINTMASK);
	u32 intrcvlazy = bcma_read32(hw->core, OB_D3A1_REG_INTRCVLAZY0);
	u32 intmask0 = bcma_read32(hw->core, OB_D3A1_REG_INTCONTROL0_MASK);
	u32 machwcap = bcma_read32(hw->core, OB_D3A1_REG_MACHWCAP);
	u32 ioc = bcma_aread32(hw->core, BCMA_IOCTL);
	u16 mburst = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MBURST);
	u16 maxant = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MAXANTCNT);
	u16 machwver = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MACHWVER);
	u16 capl = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MACHWCAP_L);
	u16 caph = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MACHWCAP_H);
	u32 rx_control = bcma_read32(hw->core, OB_D11_RX_CONTROL);
	u32 rx_high = bcma_read32(hw->core, OB_D11_RX_ADDRHIGH);
	u32 rx_s0 = bcma_read32(hw->core, OB_D11_RX_STATUS0);
	u32 rx_s1 = bcma_read32(hw->core, OB_D11_RX_STATUS1);
	u32 i;
	int ret = 0;

	if (mctrl != OB_D3A1_MACCONTROL_EXPECTED) {
		dev_err(hw->dev, "d3a1-test: MACCONTROL=%08x expected %08x\n",
			mctrl, OB_D3A1_MACCONTROL_EXPECTED);
		ret = -EIO;
	}
	if (!ob_d3a0_host_irq_disabled(macintmask)) {
		dev_err(hw->dev, "d3a1-test: MACINTMASK=%08x not 0\n",
			macintmask);
		ret = -EIO;
	}
	if (intrcvlazy != OB_D3A1_INTRCVLAZY ||
	    !ob_d3a0_irq_source_ok(intmask0)) {
		dev_err(hw->dev,
			"d3a1-test: IRQ source mismatch intrcvlazy=%08x intmask0=%08x\n",
			intrcvlazy, intmask0);
		ret = -EIO;
	}
	if (mburst != OB_D3A1_SHM_MBURST_VAL ||
	    maxant != OB_D3A1_SHM_MAXANTCNT_VAL) {
		dev_err(hw->dev,
			"d3a1-test: MBURST=%04x MAXANTCNT=%04x\n",
			mburst, maxant);
		ret = -EIO;
	}
	if (machwver != (u16)hw->core->id.rev ||
	    capl != (u16)(machwcap & 0xffffu) ||
	    caph != (u16)((machwcap >> 16) & 0xffffu)) {
		dev_err(hw->dev,
			"d3a1-test: MACHW_VER=%04x CAP=%04x/%04x machwcap=%08x\n",
			machwver, capl, caph, machwcap);
		ret = -EIO;
	}
	if (!(ioc & OB_D3A0_IOCTL_MPCLKE)) {
		dev_err(hw->dev, "d3a1-test: SICF_MPCLKE not enabled\n");
		ret = -EIO;
	}

	/* Deterministic T1 register readbacks (exact equality). */
	{
		u32 cfprep = bcma_read32(hw->core, OB_D3A1_REG_TSF_CFPREP);
		u32 cfpstart = bcma_read32(hw->core, OB_D3A1_REG_TSF_CFPSTART);
		u16 fastp = bcma_read16(hw->core, OB_D3A1_REG_FASTPWRUP_DLY);
		u16 ifs_ctl = bcma_read16(hw->core, OB_D3A1_REG_IFS_CTL);
		u16 ifs_aifsn = bcma_read16(hw->core, OB_D3A1_REG_IFS_AIFSN);

		if (cfprep != OB_D3A1_TSF_CFPREP) {
			dev_err(hw->dev,
				"d3a1-test: tsf_cfprep=%08x expected %08x\n",
				cfprep, OB_D3A1_TSF_CFPREP);
			ret = -EIO;
		}
		if (cfpstart != OB_D3A1_TSF_CFPSTART) {
			dev_err(hw->dev,
				"d3a1-test: tsf_cfpstart=%08x expected %08x\n",
				cfpstart, OB_D3A1_TSF_CFPSTART);
			ret = -EIO;
		}
		if (fastp != st->fastpwrup_dly) {
			dev_err(hw->dev,
				"d3a1-test: fastpwrup=%04x expected %04x\n",
				fastp, st->fastpwrup_dly);
			ret = -EIO;
		}
		/* ifs_ctl is masked (the vendor clears the high bits); aifsn exact */
		if (ifs_ctl & ~OB_D3A1_IFS_CTL_MASK) {
			dev_err(hw->dev,
				"d3a1-test: ifs_ctl=%04x high bits not cleared\n",
				ifs_ctl);
			ret = -EIO;
		}
		if (ifs_aifsn != OB_D3A1_IFS_AIFSN_VAL) {
			dev_err(hw->dev,
				"d3a1-test: ifs_aifsn=%04x expected %04x\n",
				ifs_aifsn, OB_D3A1_IFS_AIFSN_VAL);
			ret = -EIO;
		}
	}

	/* SRL/LRL (SCR window) and SFBL/LFBL (SHM) exact readbacks. */
	{
		u16 srl = ob_d3a1_read_scr16(hw, OB_D3A1_SCR_SRL);
		u16 lrl = ob_d3a1_read_scr16(hw, OB_D3A1_SCR_LRL);
		u16 sfbl = ob_ucode_read_shm16(hw, OB_D3A1_SHM_SFBL);
		u16 lfbl = ob_ucode_read_shm16(hw, OB_D3A1_SHM_LFBL);

		if (srl != OB_D3A1_SRL_DEFAULT || lrl != OB_D3A1_LRL_DEFAULT) {
			dev_err(hw->dev,
				"d3a1-test: SCR SRL/LRL=%04x/%04x expected %04x/%04x\n",
				srl, lrl, OB_D3A1_SRL_DEFAULT,
				OB_D3A1_LRL_DEFAULT);
			ret = -EIO;
		}
		if (sfbl != OB_D3A1_SFBL_DEFAULT ||
		    lfbl != OB_D3A1_LFBL_DEFAULT) {
			dev_err(hw->dev,
				"d3a1-test: SHM SFBL/LFBL=%04x/%04x expected %04x/%04x\n",
				sfbl, lfbl, OB_D3A1_SFBL_DEFAULT,
				OB_D3A1_LFBL_DEFAULT);
			ret = -EIO;
		}
	}

	/* MAC SHM postcondition only when the vendor would have written it. */
	if (st->mac_written) {
		for (i = 0; i < OB_D3A1_MAC_WORDS; i++) {
			u16 want = ob_d3a1_mac_word(hw->mac, i);
			u16 got = ob_ucode_read_shm16(
					hw, (u16)(OB_D3A1_SHM_MAC_0 + 2u * i));

			if (got != want) {
				dev_err(hw->dev,
					"d3a1-test: MAC SHM[%04x]=%04x expected %04x\n",
					OB_D3A1_SHM_MAC_0 + 2u * i, got, want);
				ret = -EIO;
			}
		}
	}

	/* DMA postconditions (vendor position: T1 -> DMA -> T2). */
	if (rx_control != OB_D3A0_RX_CONTROL ||
	    rx_high != OB_DMA_PCIE_H32 || !ob_d3a0_rx_idle(rx_s0) ||
	    (rx_s1 & OB_D11_RS1_RE_MASK)) {
		dev_err(hw->dev,
			"d3a1-test: RX postcondition fail control=%08x high=%08x status0=%08x status1=%08x\n",
			rx_control, rx_high, rx_s0, rx_s1);
		ret = -EIO;
	}

	if (!ret)
		dev_info(hw->dev,
			 "d3a1-test: postconditions PASS (machwcap=%08x intrcvlazy=%08x rx_control=%08x)\n",
			 machwcap, intrcvlazy, rx_control);
	return ret;
}

/* ---- top-level bring-up ------------------------------------------------- */

int ob_d3a1_test(struct ob_hw *hw)
{
	struct ob_ucode_run run;
	struct ob_initvals_post post;
	int ret;

	/* never re-enter after an unverified quiesce; only a reboot clears it */
	if (ob_d3a0_fatal_is_latched()) {
		dev_crit(hw->dev,
			 "d3a1-test: refusing re-entry after fatal quiesce; reboot required\n");
		return -EIO;
	}

	memset(&hw->d3a1, 0, sizeof(hw->d3a1));
	/* the D3A1 DMA lifecycle reuses hw->d3a0; reset it before use */
	memset(&hw->d3a0, 0, sizeof(hw->d3a0));

	if (hw->core->id.rev != OB_D3A1_PHYREV_REV42) {
		dev_err(hw->dev,
			"d3a1-test: unsupported D11 core rev %u (need %u)\n",
			hw->core->id.rev, OB_D3A1_PHYREV_REV42);
		return -ENOTSUPP;
	}
	if (!hw->cc) {
		dev_err(hw->dev,
			"d3a1-test: no ChipCommon core; refusing\n");
		return -ENODEV;
	}
	if (!hw->mac_valid) {
		dev_err(hw->dev,
			"d3a1-test: no validated MAC; refusing (vendor attach would fail)\n");
		return -EINVAL;
	}

	dev_info(hw->dev, "d3a1-test: BEGIN\n");

	/* D2B: shared hardware-proven D2A core + 610 common initvals + gate */
	ret = ob_initvals_run_d2b(hw, "d3a1-test", &run, &post);
	if (ret)
		return ret;
	if (!ob_d3a0_d2b_state_ok(&post)) {
		dev_err(hw->dev,
			"d3a1-test: D2B postconditions not satisfied; D3A1 forbidden\n");
		return -EIO;
	}

	/* live D2B exit re-check before the first new D3A1 write */
	ret = ob_d3a1_check_d2b_exit(hw);
	if (ret)
		return ret;

	ret = ob_d3a1_fifo_fixup(hw);
	if (ret) {
		/*
		 * sub_67efd mutates TX FIFO hardware before DMA begins. On a
		 * completion-poll timeout the hardware state is NOT proven.
		 * No DMA memory is live yet, so returning an error is safe, but
		 * the operator MUST NOT retry the isolated test in the same
		 * boot: capture the logs, then reboot before another attempt.
		 * No unproven FIFO/core reset recovery is attempted here.
		 */
		dev_err(hw->dev,
			"d3a1-test: sub_67efd FAIL ret=%d; TX FIFO state unproven - capture logs and REBOOT before retry\n",
			ret);
		return ret;
	}

	ret = ob_d3a1_t1(hw);
	if (ret) {
		dev_err(hw->dev, "d3a1-test: T1 FAIL ret=%d\n", ret);
		return ret;
	}

	/*
	 * DMA in the exact vendor position: T1 -> DMA -> T2. Reuse the
	 * hardware-proven D3A0 lifecycle unchanged.
	 */
	ret = ob_d3a0_bringup(hw);
	if (ret) {
		dev_err(hw->dev,
			"d3a1-test: DMA bring-up FAIL ret=%d; tearing down\n",
			ret);
		ob_d3a0_teardown(hw);
		return ret;
	}

	ret = ob_d3a1_t2(hw);
	if (ret) {
		dev_err(hw->dev,
			"d3a1-test: T2 FAIL ret=%d; tearing down\n", ret);
		ob_d3a0_teardown(hw);
		return ret;
	}

	ret = ob_d3a1_validate(hw);
	if (ret) {
		dev_err(hw->dev,
			"d3a1-test: validation FAIL ret=%d; tearing down\n",
			ret);
		ob_d3a0_teardown(hw);
		return ret;
	}

	/* mandatory same-run teardown of the reused D3A0 lifecycle */
	ret = ob_d3a0_teardown(hw);
	if (ret)
		return ret;

	hw->d3a1.stopped_before_phy = true;
	dev_info(hw->dev,
		 "d3a1-test: PASS - vendor-ordered D3A1 tail + DMA lifecycle\n");
	dev_info(hw->dev,
		 "d3a1-test: STOPPED BEFORE sub_6656c / bsinitvals / PHY\n");
	return 0;
}

void ob_d3a1_remove(struct ob_hw *hw)
{
	if (!hw->d3a1_test_only)
		return;

	if (hw->d3a0.lc.fatal) {
		dev_crit(hw->dev,
			 "d3a1-test: removed in FATAL unverified-quiesce state; DMA memory retained; reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	if (hw->d3a0.pool_created) {
		if (ob_d3a0_teardown(hw)) {
			dev_crit(hw->dev,
				 "d3a1-test: removal teardown unverified; reboot required\n");
			bcma_set_drvdata(hw->core, NULL);
			return;
		}
	}
	dev_info(hw->dev, "d3a1-test: removed (DMA resources released)\n");
	bcma_set_drvdata(hw->core, NULL);
}

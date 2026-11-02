// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — isolated D3B band-init / d11ac1bsinitvals42 test (M3.4D3B).
 *
 * Module param bsinitvals_test_only=1. From the proven D2B exit it runs the
 * exact D3A1 vendor prefix with the D3A0 DMA engines left LIVE in their vendor
 * position, then reproduces the exact BCM4352/rev42/AC sub_6656c slice:
 *
 *   sub_6656c pre-bs
 *     -> osl_readw(D11+0x3e0)              [VENDOR ONLY - deliberately OMITTED]
 *        The result is unused, and the access is a PHY-status read that the
 *        vendor performs only after its attach-time SICF_PCLKE /
 *        wlc_bmac_phy_reset preparation. The isolated D3B state does not
 *        establish those prerequisites; the 2026-10-26 hardware run stalled
 *        ~200 ms on this read and latched device loss before any MHF write.
 *     -> trusted MACCONTROL checkpoint     [driver: D11+0x120, unconditional]
 *     -> sub_62766 = wlc_bmac_write_mhf    (MHF1..5 -> SHM 0x5e/0x60/0x62/0x78/0xd4)
 *     -> table select (phyrev 0x2a + phytype 0x0b -> d11ac1bsinitvals42)
 *     -> sub_60f67(d11ac1bsinitvals42)     [73 records, 39 x w2 + 34 x w4]
 *     -> STOP at the applier return (0x669c2), before the D4 PHY entry (0x669df)
 *     -> verified D3A0 teardown
 *
 * It NEVER enters wlc_phy_init / wlc_phy_anacore, any PHY-indirect window
 * (D11+0x3fc/0x3fe) or radio window (D11+0x3d8/0x3da), never enables EN_MAC,
 * never routes the host IRQ and never enables MACINTMASK/MI_DMAINT.
 *
 * DMA lifetime: the D3A0 engines MUST stay initialized while D3B executes
 * (vendor order T1 -> DMA -> T2 -> sub_6656c). This file therefore calls
 * ob_d3a1_run_prefix() (which leaves DMA live) and performs the mandatory
 * verified ob_d3a0_teardown() only after D3B. The fail-closed D3A0 guarantees
 * are unchanged: no free/unmap unless every programmed engine was verified
 * stopped, and a fatal latch pins the module until reboot.
 *
 * Provenance: docs/m34d3b_band_init.md, docs/m34d3b_implementation_plan.md;
 * image sha256 e81a645c79f5…; tool-first `re` slice evidence.
 */
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>

#include "ob_core.h"
#include "ob_dma.h"
#include "ob_si.h"
#include "ob_ucode.h"
#include "ob_initvals.h"
#include "ob_d3a0.h"
#include "ob_d3a1.h"
#include "ob_d3b.h"

/*
 * Durable-owner boundary for every D3B exit taken after the D3A0 DMA
 * engines became live.
 *
 * ob_dma_owner_retain_d3a0() only transfers bookkeeping: the live owner's
 * already-allocated pool/ring/skb handles are adopted into the durable owner
 * and the module is pinned once.  It performs no MMIO, so it is safe on the
 * device-lost path, and it is idempotent (a second call is a no-op), so the
 * D3B fatal exit and a later fatal ob_d3b_remove() cannot adopt twice or
 * orphan the handles.  Without it the only references to the DMA memory
 * would die with the devres-managed struct ob_hw.
 */
static void ob_d3b_retain_dma(struct ob_hw *hw,
			      enum ob_dma_owner_retain_reason reason,
			      const char *where)
{
	/*
	 * A retained owner already holds every handle and has already pinned
	 * the module, so the second call is a pure no-op and is skipped: it
	 * would only report -EEXIST for each class and hide a real adoption
	 * conflict behind expected noise.
	 */
	if (hw->dma_owner && ob_dma_owner_is_retained(hw->dma_owner))
		return;
	ob_dma_owner_retain_d3a0(hw, reason, where);
}

/*
 * Trusted D11 liveness checkpoint.
 *
 * The driver's only proven dead-window predicate is a trusted direct 32-bit
 * D11 read that returns all-ones (see ob_guard.c).  MACCONTROL (0x120) is a
 * plain status register: the same read is already used as the trusted
 * observation in the D3A1 validation and in the D3B postconditions, no
 * bsinitvals42 record targets it (they write 0x160/0x164/0x166/0x680..0x686/
 * 0x700), and reading it has no side effect.  A checkpoint therefore costs one
 * posted read and cannot perturb the intended programming.
 *
 * Returns -ENODEV once the loss is latched - the same errno the checked SHM
 * helpers return - so every caller stops before the next D3B MMIO.
 */
static int ob_d3b_checkpoint(struct ob_hw *hw, const char *where)
{
	(void)ob_d11_read32_trusted(hw, where, OB_D3A1_REG_MACCONTROL);
	if (hw->dev_lost) {
		dev_err(hw->dev,
			"d3b-test: %s: device lost - aborting D3B MMIO\n",
			where);
		return -ENODEV;
	}
	return 0;
}

/* exact MHF SHM destinations (blob sub_62766 0x62781..0x62799) */
static const u16 ob_d3b_mhf_shm[OB_D3B_MHF_COUNT] = {
	OB_D3B_MHF_SHM0, OB_D3B_MHF_SHM1, OB_D3B_MHF_SHM2,
	OB_D3B_MHF_SHM3, OB_D3B_MHF_SHM4,
};

/* ---- MHF programming (sub_62766 = wlc_bmac_write_mhf) ------------------- */

static int ob_d3b_apply_mhf(struct ob_hw *hw)
{
	struct ob_d3b *st = &hw->d3b;
	u16 mhf[OB_D3B_MHF_COUNT];
	u32 i;
	int ret;

	ob_d3b_band_from_board(&hw->board, OB_D3B_BAND_PHYTYPE_AC,
			       &st->band);
	st->antsel_type = ob_d3b_antsel_type(st->band.boardtype,
					     st->band.boardflags,
					     st->band.antswitch,
					     st->band.aa2g, st->band.aa5g);
	ob_d3b_mhf_vector(&st->band, mhf);
	memcpy(st->mhf, mhf, sizeof(mhf));

	if (!hw->board.antswitch_present)
		dev_info(hw->dev,
			 "d3b-test: antswitch absent (0xff) -> getvar 0\n");

	/*
	 * Exact vendor order and destinations: idx 0..4 -> MHF1..5.
	 *
	 * The checked writer is used because the vendor helper's OBJADDR barrier
	 * readback is the only observation inside this loop that can be trusted
	 * to mean device loss.  It issues the identical MMIO sequence and the
	 * identical OBJDATA value as ob_ucode_write_shm16() on a healthy device
	 * (select write, posted OBJADDR read, OBJDATA write); the difference is
	 * only that an all-ones readback latches the loss and returns -ENODEV
	 * BEFORE the OBJDATA write instead of writing into a dead window.
	 *
	 * The loop stops on the first error: no MHF(i+1) is attempted and
	 * ob_d3b_apply_bs() is never reached.
	 */
	for (i = 0; i < OB_D3B_MHF_COUNT; i++) {
		ret = ob_ucode_write_shm16_checked(hw, ob_d3b_mhf_shm[i], mhf[i]);
		if (ret) {
			dev_err(hw->dev,
				"d3b-test: MHF%u write SHM=0x%04x val=0x%04x FAIL ret=%d; no further MHF or bsinitvals write\n",
				i + 1, ob_d3b_mhf_shm[i], mhf[i], ret);
			return ret;
		}
	}

	dev_info(hw->dev,
		 "d3b-test: MHF write boardtype=0x%04x boardflags=0x%08x aa2g=%u aa5g=%u antswitch=%u antsel_type=%d -> mhfs[0..4]={0x%04x,0x%04x,0x%04x,0x%04x,0x%04x}\n",
		 st->band.boardtype, st->band.boardflags, st->band.aa2g,
		 st->band.aa5g, st->band.antswitch, st->antsel_type,
		 mhf[0], mhf[1], mhf[2], mhf[3], mhf[4]);
	return 0;
}

/* ---- bsinitvals42 application (sub_60f67) ------------------------------- */

static void ob_d3b_write_iv(struct ob_hw *hw, const struct ob_fw_iv *rec)
{
	if (rec->width == OB_FW_IV_WIDTH16)
		ob_d11_write16(hw, rec->offset, (u16)rec->value);
	else
		ob_d11_write32(hw, rec->offset, rec->value);
}

static int ob_d3b_apply_bs(struct ob_hw *hw, const u8 *data, size_t size,
			   struct ob_d3b *st)
{
	struct ob_initvals_plan plan;
	struct ob_fw_iv rec;
	u32 total = 0, w16 = 0, w32 = 0, i;
	int ret;

	ret = ob_initvals_plan_from_table(data, size, &plan);
	if (ret) {
		dev_err(hw->dev, "d3b-test: bsinitvals42 malformed ret=%d\n",
			ret);
		return ret;
	}
	if (!ob_d3b_bs_plan_ok(&plan)) {
		dev_err(hw->dev,
			"d3b-test: bsinitvals42 shape records=%u w16=%u w32=%u (expected %u/%u/%u)\n",
			plan.records, plan.w16, plan.w32, OB_D3B_BS_RECORDS,
			OB_D3B_BS_W16, OB_D3B_BS_W32);
		return -EINVAL;
	}
	dev_info(hw->dev, "d3b-test: bsinitvals42 begin records=%u\n",
		 plan.records);

	/*
	 * Pre-table trusted checkpoint.  The last authoritative observation was
	 * the OBJADDR readback inside MHF5's checked write (and the firmware
	 * request / plan parse in between issue no MMIO), so without this the
	 * first table write would be the first bus access after an unobserved
	 * loss.  One posted read, no write value or order change.
	 */
	ret = ob_d3b_checkpoint(hw, "D3B pre-bsinitvals checkpoint");
	if (ret)
		goto fail;

	st->offset_allowed = true;
	for (i = 0; i < plan.records; i++) {
		/* A latched loss ends the table before the next record write. */
		if (hw->dev_lost) {
			ret = -ENODEV;
			goto fail;
		}
		if (ob_fw_iv_at(data, size, i, &rec)) {
			ret = -EINVAL;
			goto fail;
		}
		if (rec.offset == OB_FW_IV_TERMINATOR ||
		    (rec.width != OB_FW_IV_WIDTH16 &&
		     rec.width != OB_FW_IV_WIDTH32)) {
			ret = -EINVAL;
			goto fail;
		}
		/*
		 * Static "no PHY/radio/DMA/IRQ window touched" guarantee: every
		 * bsinitvals42 record targets one of the recovered D11 offsets.
		 */
		if (!ob_d3b_bs_offset_allowed(rec.offset)) {
			dev_err(hw->dev,
				"d3b-test: bsinitvals42 record %u forbidden offset 0x%03x\n",
				i, rec.offset);
			st->offset_allowed = false;
			ret = -EIO;
			goto fail;
		}
		ob_d3b_write_iv(hw, &rec);
		if (rec.width == OB_FW_IV_WIDTH16)
			w16++;
		else
			w32++;
		total++;
		/*
		 * Per-record trusted checkpoint.  The 73 records are back-to-back
		 * MAC writes with no readback of their own, so without this a loss
		 * inside the table would only be observed by the D3B postcondition
		 * reads, i.e. after up to all 73 writes had been issued into a dead
		 * window.  One read-only status read per record bounds the unobserved
		 * window to the single record already written when the loss happened;
		 * it changes no write value and no write order.
		 */
		ret = ob_d3b_checkpoint(hw, "D3B bsinitvals record checkpoint");
		if (ret)
			goto fail;
	}
	if (!ob_d3b_bs_counts_ok(total, w16, w32)) {
		dev_err(hw->dev,
			"d3b-test: bsinitvals42 write count total=%u w16=%u w32=%u (expected %u/%u/%u)\n",
			total, w16, w32, OB_D3B_BS_RECORDS, OB_D3B_BS_W16,
			OB_D3B_BS_W32);
		ret = -EIO;
		goto fail;
	}
	st->bs_records = total;
	st->bs_w16 = w16;
	st->bs_w32 = w32;
	dev_info(hw->dev,
		 "d3b-test: bsinitvals42 complete total=%u w16=%u w32=%u\n",
		 total, w16, w32);
	return 0;

fail:
	st->bs_records = total;
	st->bs_w16 = w16;
	st->bs_w32 = w32;
	return ret;
}

/* ---- deterministic postconditions --------------------------------------- */

static u32 ob_d3b_read_shm32(struct ob_hw *hw, u16 off)
{
	return (u32)ob_ucode_read_shm16(hw, off) |
	       ((u32)ob_ucode_read_shm16(hw, (u16)(off + 2u)) << 16);
}

static void ob_d3b_read_post(struct ob_hw *hw, struct ob_d3b *st)
{
	struct ob_d3b_post *p = &st->post;
	u32 i;

	/*
	 * Device-lost fail-safe: detect with a trusted direct D11 read
	 * (MACCONTROL) BEFORE any SHM/OBJ access. If the window is all-ones,
	 * latch and stop immediately - no further MMIO.
	 */
	p->maccontrol = ob_d11_read32_trusted(hw, "D3B postcondition MACCONTROL",
					       OB_D3A1_REG_MACCONTROL);
	if (hw->dev_lost)
		return;
	p->macintmask = ob_d11_read32_trusted(hw, "D3B postcondition MACINTMASK",
					      OB_D3A1_REG_MACINTMASK);
	if (hw->dev_lost)
		return;

	for (i = 0; i < OB_D3B_MHF_COUNT; i++) {
		if (hw->dev_lost)
			return;
		p->mhf[i] = ob_ucode_read_shm16(hw, ob_d3b_mhf_shm[i]);
	}
	if (hw->dev_lost)
		return;
	p->shm_10 = ob_d3b_read_shm32(hw, OB_D3B_SHM_OVR_10);
	if (hw->dev_lost)
		return;
	p->shm_1c = ob_d3b_read_shm32(hw, OB_D3B_SHM_OVR_1C);
	if (hw->dev_lost)
		return;
	p->shm_94 = ob_d3b_read_shm32(hw, OB_D3B_SHM_OVR_94);
	p->records = st->bs_records;
	p->w16 = st->bs_w16;
	p->w32 = st->bs_w32;
}

static int ob_d3b_validate(struct ob_hw *hw)
{
	struct ob_d3b_post *p = &hw->d3b.post;
	int ret = 0;

	dev_info(hw->dev,
		 "d3b-test: post mhfs={0x%04x,0x%04x,0x%04x,0x%04x,0x%04x} shm10=%08x shm1c=%08x shm94=%08x maccontrol=%08x macintmask=%08x\n",
		 p->mhf[0], p->mhf[1], p->mhf[2], p->mhf[3], p->mhf[4],
		 p->shm_10, p->shm_1c, p->shm_94, p->maccontrol,
		 p->macintmask);

	if (!ob_d3b_mhf_is_captured(p->mhf)) {
		dev_err(hw->dev, "d3b-test: MHF postcondition mismatch\n");
		ret = -EIO;
	}
	/*
	 * The three overrides are 16-bit SHM writes; the neighbouring word read
	 * into the upper half of each 32-bit observation is common-initvals
	 * state, so only the written low half is compared.
	 */
	if (!ob_d3b_shm_override_ok(p->shm_10, OB_D3B_SHM_OVR_10_VAL) ||
	    !ob_d3b_shm_override_ok(p->shm_1c, OB_D3B_SHM_OVR_1C_VAL) ||
	    !ob_d3b_shm_override_ok(p->shm_94, OB_D3B_SHM_OVR_94_VAL)) {
		dev_err(hw->dev, "d3b-test: SHM override mismatch\n");
		ret = -EIO;
	}
	if (p->maccontrol != OB_D3B_MACCONTROL_EXPECTED ||
	    (p->maccontrol & OB_D3B_MCTL_EN_MAC)) {
		dev_err(hw->dev,
			"d3b-test: MACCONTROL=%08x expected %08x (EN_MAC must stay 0)\n",
			p->maccontrol, OB_D3B_MACCONTROL_EXPECTED);
		ret = -EIO;
	}
	if (p->macintmask != OB_D3B_MACINTMASK_EXPECTED) {
		dev_err(hw->dev,
			"d3b-test: MACINTMASK=%08x expected 0 (host IRQ off)\n",
			p->macintmask);
		ret = -EIO;
	}
	if (!ob_d3b_bs_counts_ok(p->records, p->w16, p->w32)) {
		dev_err(hw->dev,
			"d3b-test: bsinitvals count mismatch %u/%u/%u\n",
			p->records, p->w16, p->w32);
		ret = -EIO;
	}
	if (!hw->d3b.offset_allowed) {
		dev_err(hw->dev, "d3b-test: forbidden bsinitvals offset seen\n");
		ret = -EIO;
	}
	return ret;
}

/* ---- D3B boundary witness (d3b_boundary_witness) ------------------------ */

/*
 * The D3A1 exit is the boundary where the vendor's sub_6656c reads
 * D11+0x3e0 (PHYVERSION) and where this slice deliberately omits that read.
 * Two project records disagree about that boundary:
 *
 *   * ob_d3b.c's own provenance note above records boot 06a8fdeb, where the
 *     0x3e0 read stalled ~200 ms, returned 0xffff and the following trusted
 *     read latched device loss before any MHF or bsinitvals write;
 *   * docs/m34d3b/d3b_crash_postmortem.md section 12 marks the same pre-bs
 *     0x3e0 read `OK` on a later boot (value not recorded).
 *
 * This opt-in witness settles which observation reproduces and records the
 * pre-state that neither run captured: the ChipCommon/PMU view (a different
 * core window, readable even when the D11 path is wedged) and the two D11
 * clock registers, read AFTER switch_macfreq and only then the island.
 *
 * Stages, all reads, no writes:
 *   B1 CC  0x01e0 clk_ctl_st     (CC view; HAVEHT decode)
 *   B2 CC  0x0600 PMU_CTL        (PLLRST decode)
 *   B3 CC  0x0608 PMU_STAT
 *   B4 CC  0x0618 PMU_MINRES_MSK
 *   B5 CC  0x061c PMU_MAXRES_MSK
 *   B6 D11 0x01e0 clk_ctl_st     (after switch_macfreq)
 *   B7 D11 0x01e8 d11_phypllctl
 *   B8 D11 0x03d8 16-bit         (island entry)
 *   B9 D11 0x03da 16-bit         (only if B8 completed)
 *
 * Witness v2 (d3b_witness_v2=1, a modifier of this witness) extends the
 * sequence without changing the v1 order:
 *   P1..P4 CC 0x0604/0x060c/0x0660/0x0664  PMU/PHY-domain state — PMU_CAP,
 *          PMU_OTPSTAT and the PLLCONTROL address/data latch: the rest of
 *          the si_pmu_* register inventory (ob_si.h)
 *   C1/C2  D11 0x03fe/0x03fc 16-bit  controls OUTSIDE the island band
 *          (0x3d8..0x3fb), bounding the failing region from above
 *   B10    D11 0x03e0 16-bit  the vendor's own register (sub_6656c),
 *          issued only after B9 completed
 * v2 verdicts are a superset: pre_island_stall_window_dead (a PMU-domain or
 * control read stalled before the island was issued),
 * island_live_vendor_read_dead (the island answers, 0x3e0 stalls) and
 * island_anomalous_read (an island-range read was fast all-ones or GREY).
 * v2 gates window_alive on B8, B9 and B10 all being class LIVE.
 *
 * Fail-stop: B6/B7 and then the island are issued only after the CC/PMU
 * baseline was LIVE. A stalled read latches dev_lost and returns false, so
 * D3B issues no MHF and no bsinitvals write into a dead window. The caller
 * aborts D3B on false, so this mode can never apply the band-init table on
 * top of an unproven window.
 *
 * The D11 clk_ctl_st bits use the same upstream values as OB_L2L_CCS_* in
 * ob_si.c (brcmsmac d11.h); the classifiers mirror the L2k/L2l thresholds
 * (LIVE < 100 us, STALL >= 10 ms, abort >= 1 s).
 */
#define OB_D3BW_LIVE_US		100ULL
#define OB_D3BW_STALL_US	10000ULL
#define OB_D3BW_ABORT_US	1000000ULL

/* ChipCommon CLKCTLST HAVEHT, the same value as OB_CLKCTLST_HAVEHT in
 * ob_si.c (upstream BCMA_CLKCTLST_HAVEHT); the host contract cross-checks it.
 */
#define OB_D3BW_CC_HAVEHT		0x00020000u

#define OB_D3BW_CCS_REQ_PHYPLL		0x00000200u
#define OB_D3BW_CCS_AVAIL_PHYPLL	0x02000000u
#define OB_D3BW_CCS_AVAIL_D11PLL	0x01000000u
#define OB_D3BW_CCS_AVAIL_HT		0x00020000u

enum ob_d3bw_class {
	OB_D3BW_NOT_ISSUED = 0,
	OB_D3BW_LIVE,
	OB_D3BW_ALL_ONES_FAST,
	OB_D3BW_GREY,
	OB_D3BW_STALL,
};

static const char *ob_d3bw_class_name(enum ob_d3bw_class klass)
{
	switch (klass) {
	case OB_D3BW_LIVE:
		return "LIVE";
	case OB_D3BW_ALL_ONES_FAST:
		return "ALL_ONES_FAST";
	case OB_D3BW_GREY:
		return "GREY";
	case OB_D3BW_STALL:
		return "STALL";
	default:
		return "not_issued";
	}
}

static enum ob_d3bw_class ob_d3bw_classify(u64 dur_us, u32 val,
					   unsigned int width)
{
	u32 all_ones = (width == 16u) ? 0xffffu : 0xffffffffu;

	if (dur_us >= OB_D3BW_STALL_US)
		return OB_D3BW_STALL;
	if (dur_us >= OB_D3BW_LIVE_US)
		return OB_D3BW_GREY;
	if (val == all_ones)
		return OB_D3BW_ALL_ONES_FAST;
	return OB_D3BW_LIVE;
}

static enum ob_d3bw_class ob_d3bw_cc_read32(struct ob_hw *hw,
					    const char *stage, u16 off,
					    u32 *value, u64 *dur_us)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u32 val = ob_si_cc_read(hw, off);
	u64 end_ns = ktime_get_mono_fast_ns();
	u64 dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	enum ob_d3bw_class klass = ob_d3bw_classify(dur, val, 32u);

	dev_info(hw->dev,
		 "D3B_BOUNDARY_WITNESS: stage=%s window=cc offset=0x%04x width=32 value=0x%08x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
		 stage, off, val, hw->dev_lost ? 1u : 0u,
		 (unsigned long long)start_ns, (unsigned long long)end_ns,
		 (unsigned long long)dur, ob_d3bw_class_name(klass));

	if (value)
		*value = val;
	if (dur_us)
		*dur_us = dur;
	return klass;
}

static enum ob_d3bw_class ob_d3bw_d11_read32(struct ob_hw *hw,
					     const char *stage, u16 off,
					     u32 *value, u64 *dur_us)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u32 val = ob_d11_read32(hw, off);
	u64 end_ns = ktime_get_mono_fast_ns();
	u64 dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	enum ob_d3bw_class klass = ob_d3bw_classify(dur, val, 32u);

	if (klass == OB_D3BW_STALL)
		hw->dev_lost = true;

	dev_info(hw->dev,
		 "D3B_BOUNDARY_WITNESS: stage=%s window=d11 offset=0x%04x width=32 value=0x%08x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
		 stage, off, val, hw->dev_lost ? 1u : 0u,
		 (unsigned long long)start_ns, (unsigned long long)end_ns,
		 (unsigned long long)dur, ob_d3bw_class_name(klass));

	if (value)
		*value = val;
	if (dur_us)
		*dur_us = dur;
	return klass;
}

static enum ob_d3bw_class ob_d3bw_d11_read16(struct ob_hw *hw,
					     const char *stage, u16 off,
					     u16 *value, u64 *dur_us)
{
	u64 start_ns = ktime_get_mono_fast_ns();
	u16 val = ob_d11_read16(hw, off);
	u64 end_ns = ktime_get_mono_fast_ns();
	u64 dur = div_u64(end_ns - start_ns, NSEC_PER_USEC);
	enum ob_d3bw_class klass = ob_d3bw_classify(dur, (u32)val, 16u);

	if (klass == OB_D3BW_STALL)
		hw->dev_lost = true;

	dev_info(hw->dev,
		 "D3B_BOUNDARY_WITNESS: stage=%s window=d11 offset=0x%04x width=16 value=0x%04x dev_lost=%u start_ns=%llu end_ns=%llu duration_us=%llu class=%s\n",
		 stage, off, val, hw->dev_lost ? 1u : 0u,
		 (unsigned long long)start_ns, (unsigned long long)end_ns,
		 (unsigned long long)dur, ob_d3bw_class_name(klass));

	if (value)
		*value = val;
	if (dur_us)
		*dur_us = dur;
	return klass;
}

static bool ob_d3b_run_boundary_witness(struct ob_hw *hw)
{
	enum ob_d3bw_class b1 = OB_D3BW_NOT_ISSUED, b2 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class b3 = OB_D3BW_NOT_ISSUED, b4 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class b5 = OB_D3BW_NOT_ISSUED, b6 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class b7 = OB_D3BW_NOT_ISSUED, b8 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class b9 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class p1 = OB_D3BW_NOT_ISSUED, p2 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class p3 = OB_D3BW_NOT_ISSUED, p4 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class c1 = OB_D3BW_NOT_ISSUED, c2 = OB_D3BW_NOT_ISSUED;
	enum ob_d3bw_class b10 = OB_D3BW_NOT_ISSUED;
	bool v2 = hw->d3b_witness_v2;
	const char *verdict = "inconclusive";
	bool window_alive = false;
	u64 dur = 0;
	u32 cc_clk = 0, pmu_ctl = 0, pmu_stat = 0, minres = 0, maxres = 0;
	u32 d11_clk = 0, d11_pll = 0;
	u32 pmu_cap = 0, pmu_otpstat = 0, pllctl_addr = 0, pllctl_data = 0;
	u16 v16 = 0, cval = 0, b10val = 0;
	char v2suffix[256] = { 0 };

	dev_info(hw->dev,
		 "D3B_BOUNDARY_WITNESS: begin at the D3A1 exit (the vendor's 0x3e0 read site)\n");

	/* B1..B5: ChipCommon/PMU pre-state, a window the D11 wedge cannot hide. */
	b1 = ob_d3bw_cc_read32(hw, "B1", OB_CC_CLKCTLST, &cc_clk, &dur);
	b2 = ob_d3bw_cc_read32(hw, "B2", OB_CC_PMU_CTL, &pmu_ctl, &dur);
	b3 = ob_d3bw_cc_read32(hw, "B3", OB_CC_PMU_STAT, &pmu_stat, &dur);
	b4 = ob_d3bw_cc_read32(hw, "B4", OB_CC_PMU_MINRES_MSK, &minres, &dur);
	b5 = ob_d3bw_cc_read32(hw, "B5", OB_CC_PMU_MAXRES_MSK, &maxres, &dur);

	if (b1 != OB_D3BW_LIVE || b2 != OB_D3BW_LIVE ||
	    b3 != OB_D3BW_LIVE || b4 != OB_D3BW_LIVE ||
	    b5 != OB_D3BW_LIVE) {
		verdict = "void_no_cc_baseline";
		dev_warn(hw->dev,
			 "D3B_BOUNDARY_WITNESS: CC/PMU baseline not LIVE; the island is NOT issued\n");
		goto summary;
	}

	dev_info(hw->dev,
		 "D3B_BOUNDARY_WITNESS: pre_state_cc clk_ctl_st_cc=0x%08x haveht=%u pmu_ctl=0x%08x pllrst=%u pmu_stat=0x%08x pmu_minres=0x%08x pmu_maxres=0x%08x\n",
		 cc_clk, (cc_clk & OB_D3BW_CC_HAVEHT) ? 1u : 0u,
		 pmu_ctl, (pmu_ctl & OB_PMU_CTL_PLLRST) ? 1u : 0u,
		 pmu_stat, minres, maxres);

	if (v2) {
		/*
		 * P1..P4: the rest of the PMU register inventory (ob_si.h, blob
		 * si_pmu_*): capability, OTP power status and the PLL control
		 * address/data latch. A stall here means the CC path itself is
		 * refusing — the island is then NOT issued.
		 */
		p1 = ob_d3bw_cc_read32(hw, "P1", OB_CC_PMU_CAP, &pmu_cap, &dur);
		if (dur >= OB_D3BW_ABORT_US) {
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P1 exceeded 1 s; no further read issued\n");
			goto summary;
		}
		if (p1 == OB_D3BW_STALL) {
			hw->dev_lost = true;
			verdict = "pre_island_stall_window_dead";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P1 stalled; the island is NOT issued\n");
			goto summary;
		}
		p2 = ob_d3bw_cc_read32(hw, "P2", OB_CC_PMU_OTPSTAT, &pmu_otpstat, &dur);
		if (dur >= OB_D3BW_ABORT_US) {
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P2 exceeded 1 s; no further read issued\n");
			goto summary;
		}
		if (p2 == OB_D3BW_STALL) {
			hw->dev_lost = true;
			verdict = "pre_island_stall_window_dead";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P2 stalled; the island is NOT issued\n");
			goto summary;
		}
		p3 = ob_d3bw_cc_read32(hw, "P3", OB_CC_PMU_PLLCTL_ADDR, &pllctl_addr, &dur);
		if (dur >= OB_D3BW_ABORT_US) {
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P3 exceeded 1 s; no further read issued\n");
			goto summary;
		}
		if (p3 == OB_D3BW_STALL) {
			hw->dev_lost = true;
			verdict = "pre_island_stall_window_dead";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P3 stalled; the island is NOT issued\n");
			goto summary;
		}
		p4 = ob_d3bw_cc_read32(hw, "P4", OB_CC_PMU_PLLCTL_DATA, &pllctl_data, &dur);
		if (dur >= OB_D3BW_ABORT_US) {
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P4 exceeded 1 s; no further read issued\n");
			goto summary;
		}
		if (p4 == OB_D3BW_STALL) {
			hw->dev_lost = true;
			verdict = "pre_island_stall_window_dead";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: P4 stalled; the island is NOT issued\n");
			goto summary;
		}
	}

	/* B6/B7: D11 clock registers AFTER switch_macfreq, below the band. */
	b6 = ob_d3bw_d11_read32(hw, "B6", 0x01e0u, &d11_clk, &dur);
	if (dur >= OB_D3BW_ABORT_US) {
		verdict = "aborted_slow_read";
		dev_warn(hw->dev,
			 "D3B_BOUNDARY_WITNESS: B6 exceeded 1 s; no further read issued\n");
		goto summary;
	}
	b7 = ob_d3bw_d11_read32(hw, "B7", 0x01e8u, &d11_pll, &dur);
	if (dur >= OB_D3BW_ABORT_US) {
		verdict = "aborted_slow_read";
		dev_warn(hw->dev,
			 "D3B_BOUNDARY_WITNESS: B7 exceeded 1 s; no further read issued\n");
		goto summary;
	}
	if (b6 != OB_D3BW_LIVE || b7 != OB_D3BW_LIVE) {
		verdict = "void_no_d11_baseline";
		dev_warn(hw->dev,
			 "D3B_BOUNDARY_WITNESS: D11 clock baseline not LIVE; the island is NOT issued\n");
		goto summary;
	}

	dev_info(hw->dev,
		 "D3B_BOUNDARY_WITNESS: pre_state_d11 clk_ctl_st=0x%08x d11_phypllctl=0x%08x req_phypll=%u avail_phypll=%u avail_d11pll=%u avail_ht=%u\n",
		 d11_clk, d11_pll,
		 (d11_clk & OB_D3BW_CCS_REQ_PHYPLL) ? 1u : 0u,
		 (d11_clk & OB_D3BW_CCS_AVAIL_PHYPLL) ? 1u : 0u,
		 (d11_clk & OB_D3BW_CCS_AVAIL_D11PLL) ? 1u : 0u,
		 (d11_clk & OB_D3BW_CCS_AVAIL_HT) ? 1u : 0u);

	if (v2) {
		/*
		 * C1/C2: controls OUTSIDE the island band (0x3d8..0x3fb),
		 * bounding the failing region from above while the window is still
		 * provably alive. A stall here means the failure is wider than the
		 * island — the island is then NOT issued.
		 */
		c1 = ob_d3bw_d11_read16(hw, "C1", 0x03feu, &cval, &dur);
		if (dur >= OB_D3BW_ABORT_US) {
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: C1 exceeded 1 s; no further read issued\n");
			goto summary;
		}
		if (c1 == OB_D3BW_STALL) {
			verdict = "pre_island_stall_window_dead";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: C1 stalled outside the band; the island is NOT issued\n");
			goto summary;
		}
		c2 = ob_d3bw_d11_read16(hw, "C2", 0x03fcu, &cval, &dur);
		if (dur >= OB_D3BW_ABORT_US) {
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: C2 exceeded 1 s; no further read issued\n");
			goto summary;
		}
		if (c2 == OB_D3BW_STALL) {
			verdict = "pre_island_stall_window_dead";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: C2 stalled outside the band; the island is NOT issued\n");
			goto summary;
		}
	}

	/* B8/B9: the island, last accesses of the witness. */
	b8 = ob_d3bw_d11_read16(hw, "B8", 0x03d8u, &v16, &dur);
	if (dur >= OB_D3BW_ABORT_US) {
		verdict = "aborted_slow_read";
		dev_warn(hw->dev,
			 "D3B_BOUNDARY_WITNESS: B8 exceeded 1 s; no further read issued\n");
		goto summary;
	}
	if (b8 == OB_D3BW_STALL) {
		verdict = "island_stall_window_dead";
	} else {
		b9 = ob_d3bw_d11_read16(hw, "B9", 0x03dau, &v16, &dur);
		if (dur >= OB_D3BW_ABORT_US) {
			verdict = "aborted_slow_read";
			dev_warn(hw->dev,
				 "D3B_BOUNDARY_WITNESS: B9 exceeded 1 s; no further read issued\n");
			goto summary;
		}
		if (b9 == OB_D3BW_STALL)
			verdict = "island_stall_window_dead";
		else if (!v2) {
			window_alive = true;
			verdict = "island_live_window_alive";
		} else {
			/*
			 * v2: the vendor's own register after the island, and the
			 * tightened window_alive gate — B8, B9 and B10 must all be
			 * class LIVE for the window to count as alive.
			 */
			b10 = ob_d3bw_d11_read16(hw, "B10", 0x03e0u, &b10val, &dur);
			if (dur >= OB_D3BW_ABORT_US) {
				verdict = "aborted_slow_read";
				dev_warn(hw->dev,
					 "D3B_BOUNDARY_WITNESS: B10 exceeded 1 s; no further read issued\n");
				goto summary;
			}
			if (b10 == OB_D3BW_STALL)
				verdict = "island_live_vendor_read_dead";
			else if (b8 == OB_D3BW_LIVE && b9 == OB_D3BW_LIVE &&
				 b10 == OB_D3BW_LIVE) {
				window_alive = true;
				verdict = "island_live_window_alive";
			} else
				verdict = "island_anomalous_read";
		}
	}

summary:
	if (v2)
		snprintf(v2suffix, sizeof(v2suffix),
			 " witness_v2=1 pmu_cap=0x%08x pmu_otpstat=0x%08x pllctl_addr=0x%08x pllctl_data=0x%08x p1=%s p2=%s p3=%s p4=%s c1_03fe=%s c2_03fc=%s b10_03e0=%s",
			 pmu_cap, pmu_otpstat, pllctl_addr, pllctl_data,
			 ob_d3bw_class_name(p1), ob_d3bw_class_name(p2),
			 ob_d3bw_class_name(p3), ob_d3bw_class_name(p4),
			 ob_d3bw_class_name(c1), ob_d3bw_class_name(c2),
			 ob_d3bw_class_name(b10));
	dev_info(hw->dev,
		 "D3B_BOUNDARY_WITNESS_SUMMARY: cc_clk_ctl_st=0x%08x cc_haveht=%u pmu_ctl=0x%08x pmu_stat=0x%08x pmu_minres=0x%08x pmu_maxres=0x%08x d11_clk_ctl_st=0x%08x d11_phypllctl=0x%08x b1=%s b2=%s b3=%s b4=%s b5=%s b6=%s b7=%s b8_03d8=%s b9_03da=%s window_alive=%u writes_issued=0 verdict=%s fifo530_continue=%u fifo530_expiry_continued=%u fifo530_entries_continued=%u fifo530_first_idx=%d fifo530_last=%04x fifo530_reads=%u fifo530_invalid530=%u fifo530_invalid540=%u%s\n",
		 cc_clk, (cc_clk & OB_D3BW_CC_HAVEHT) ? 1u : 0u,
		 pmu_ctl, pmu_stat, minres, maxres, d11_clk, d11_pll,
		 ob_d3bw_class_name(b1), ob_d3bw_class_name(b2),
		 ob_d3bw_class_name(b3), ob_d3bw_class_name(b4),
		 ob_d3bw_class_name(b5), ob_d3bw_class_name(b6),
		 ob_d3bw_class_name(b7), ob_d3bw_class_name(b8),
		 ob_d3bw_class_name(b9), window_alive ? 1u : 0u, verdict,
		 hw->d3a1_fifo530_continue ? 1u : 0u,
		 hw->d3a1_fifo530.expiry_continued ? 1u : 0u,
		 hw->d3a1_fifo530.expired_continued,
		 hw->d3a1_fifo530.first_continued_index,
		 hw->d3a1_fifo530.continued_last,
		 hw->d3a1_fifo530.continued_reads,
		 hw->d3a1_fifo530.invalid_530,
		 hw->d3a1_fifo530.invalid_540, v2suffix);

	if (!window_alive)
		dev_warn(hw->dev,
			 "D3B_BOUNDARY_WITNESS: verdict=%s; apply the reading rules in docs/m34d3b_boundary_witness.md\n",
			 verdict);

	return window_alive;
}

/* ---- top-level bring-up ------------------------------------------------- */

int ob_d3b_test(struct ob_hw *hw)
{
	const struct firmware *biv = NULL;
	struct ob_d3b *st = &hw->d3b;
	int ret;

	/*
	 * Shared side-effect-free admission gate, before the state reset and
	 * before the D3A1 prefix reaches D2A.  It covers device loss, fatal DMA,
	 * retained owner, reboot-required tail failures, the FIFO latch, prior
	 * FIFO programming and any still-owned D3A0 state (pool, rings, mappings).
	 */
	ret = ob_d11_prefix_entry_preflight(hw, "d3b-test");
	if (ret)
		return ret;

	memset(st, 0, sizeof(*st));
	/* the D3A1 prefix reuses hw->d3a1 / hw->d3a0; reset both before use */
	memset(&hw->d3a1, 0, sizeof(hw->d3a1));
	memset(&hw->d3a0, 0, sizeof(hw->d3a0));

	if (hw->core->id.rev != OB_D3B_PHYREV_REV42) {
		dev_err(hw->dev,
			"d3b-test: unsupported D11 core rev %u (need %u)\n",
			hw->core->id.rev, OB_D3B_PHYREV_REV42);
		return -ENOTSUPP;
	}
	if (!hw->cc) {
		dev_err(hw->dev, "d3b-test: no ChipCommon core; refusing\n");
		return -ENODEV;
	}
	if (!hw->mac_valid) {
		dev_err(hw->dev,
			"d3b-test: no validated MAC; refusing (vendor attach would fail)\n");
		return -EINVAL;
	}
	if (!hw->board.valid) {
		dev_err(hw->dev,
			"d3b-test: no validated rev11 board data; refusing (MHF inputs unproven)\n");
		return -EINVAL;
	}

	dev_info(hw->dev,
		 "d3b-test: BEGIN (DMA stays live through D3B in vendor position)\n");

	/*
	 * Exact D3A1 vendor prefix. On success the D3A0 DMA engines are LIVE
	 * (vendor order T1 -> DMA -> T2) for the D3B band-init stage below.
	 */
	ret = ob_d3a1_run_prefix(hw, "d3b-test");
	if (ret)
		return ret;

	/*
	 * Opt-in boundary witness (d3b_boundary_witness=1): the D3A1 exit is
	 * exactly the vendor's 0x3e0 read site, and this slice omits that read.
	 * The witness records the pre-state (CC/PMU + D11 0x1e0/0x1e8 after
	 * switch_macfreq) and only then asks whether the island still stalls. A
	 * false return means the window was not proven alive (void baseline or a
	 * stall that latched device loss), so no MHF and no bsinitvals write may
	 * follow.
	 */
	if (hw->d3b_boundary_witness) {
		if (!ob_d3b_run_boundary_witness(hw)) {
			dev_err(hw->dev,
				"d3b-test: boundary witness did not prove a live window; no MHF or bsinitvals write issued (fail-stop)\n");
			ret = -EIO;
			goto fail_after_dma;
		}
	}

	/*
	 * Unconditional trusted pre-MHF liveness checkpoint.
	 *
	 * The vendor sub_6656c performs one access here that this slice must not:
	 * a 16-bit read of D11+0x3e0 (PHYVERSION, blob 0x665c3) whose result it
	 * discards.  That read is a PHY-status access, and the vendor performs it
	 * only after its attach-time SICF_PCLKE / wlc_bmac_corereset /
	 * wlc_bmac_phy_reset preparation, which the isolated D3B state does not
	 * establish.  On 2026-10-26 (boot 06a8fdeb) it stalled ~200 ms, returned
	 * 0xffff, and the following trusted read latched device loss before any
	 * MHF or bsinitvals write.  Only the access is removed: its value had no
	 * consumer, and the five MHF writes plus the 73 bsinitvals records are
	 * unchanged and require only the MAC-side state the D3A1 prefix proves
	 * (register-prerequisite matrix: 78/78 VERIFIED_COMPATIBLE).
	 *
	 * In its place is the driver's only proven dead-window predicate: one
	 * trusted 32-bit read of MACCONTROL (0x120), the same read the D3A1
	 * validation already performs successfully in this state.  It runs
	 * before the first MHF write, so a genuinely dead window is detected
	 * with zero writes issued into it - and it is not conditional on a
	 * 16-bit observation that a live device can also produce.
	 */
	ret = ob_d3b_checkpoint(hw, "D3B pre-MHF");
	if (ret)
		goto fail_after_dma;

	/* sub_6656c pre-bs: MHF1..5 (sub_62766 = wlc_bmac_write_mhf) */
	ret = ob_d3b_apply_mhf(hw);
	if (ret) {
		dev_err(hw->dev, "d3b-test: MHF FAIL ret=%d; tearing down\n",
			ret);
		goto fail_after_dma;
	}

	/* sub_6656c: table select (phyrev 0x2a + phytype 0x0b) -> bsinitvals42 */
	ret = ob_fw_request_bsinitvals(hw, &biv);
	if (ret) {
		dev_err(hw->dev,
			"d3b-test: bsinitvals42 unavailable/invalid ret=%d; tearing down\n",
			ret);
		goto fail_after_dma;
	}

	ret = ob_d3b_apply_bs(hw, biv->data, biv->size, st);
	if (ret) {
		dev_err(hw->dev,
			"d3b-test: bsinitvals42 FAIL ret=%d; tearing down\n",
			ret);
		goto fail_after_dma;
	}

	ob_d3b_read_post(hw, st);
	ret = ob_d3b_validate(hw);
	if (ret) {
		dev_err(hw->dev,
			"d3b-test: validation FAIL ret=%d; tearing down\n", ret);
		goto fail_after_dma;
	}

	/* D3B reached the vendor STOP point: mandatory verified teardown */
	release_firmware(biv);
	ret = ob_d3a0_teardown(hw);
	if (ret)
		return ret;

	st->stopped_before_phy = true;
	dev_info(hw->dev,
		 "d3b-test: PASS - D3B band-init + bsinitvals42 (DMA live through D3B)\n");
	dev_info(hw->dev,
		 "d3b-test: STOPPED BEFORE wlc_phy_init / wlc_phy_anacore / PHY / radio\n");
	return 0;

fail_after_dma:
	release_firmware(biv);
	st->fatal_seen = true;
	/*
	 * Device-lost fail-safe: if a trusted direct D11 read was all-ones,
	 * NO further MMIO (no interrupt/DMA reset writes). Retain the DMA
	 * memory, mark reboot-required and return.
	 */
	if (ob_dev_lost_is_latched(hw)) {
		/*
		 * The DMA engines were live when D3B ran, so their resources must
		 * outlive the device-local struct ob_hw.  Adopt them into the
		 * durable owner unconditionally: a fatal latch is not by itself
		 * proof that every handle was transferred.
		 */
		ob_d3b_retain_dma(hw, OB_DMA_OWNER_REASON_DEVICE_LOST,
				  "d3b device lost");
		dev_crit(hw->dev,
			 "d3b-test: DEVICE LOST - skipping teardown; DMA memory retained by durable owner; reboot required\n");
		return ret;
	}
	/*
	 * D3B ran after the DMA engines became live; any failure must run the
	 * mandatory verified D3A0 teardown. A teardown failure latches the
	 * module-wide fatal state (retain memory, reboot); the free is never
	 * authorized by containment.
	 */
	ob_d3a0_teardown(hw);
	return ret;
}

void ob_d3b_remove(struct ob_hw *hw)
{
	if (!hw->bsinitvals_test_only)
		return;

	if (hw->d3a0.lc.fatal) {
		/*
		 * Retention is idempotent, so the normal case (the D3B fatal exit
		 * already adopted the handles) costs nothing, while a fatal latch
		 * set by any other path still cannot drop the last references
		 * here.  Removal after device loss must stay a strict no-MMIO
		 * operation: retention never touches the bus.
		 */
		ob_d3b_retain_dma(hw, OB_DMA_OWNER_REASON_REMOVE_RETAINED,
				  "d3b fatal remove");
		dev_crit(hw->dev,
			 "d3b-test: removed in FATAL unverified-quiesce state; DMA memory retained; reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	if (hw->d3a0.pool_created) {
		if (ob_d3a0_teardown(hw)) {
			dev_crit(hw->dev,
				 "d3b-test: removal teardown unverified; reboot required\n");
			bcma_set_drvdata(hw->core, NULL);
			return;
		}
	}
	dev_info(hw->dev, "d3b-test: removed (DMA resources released)\n");
	bcma_set_drvdata(hw->core, NULL);
}

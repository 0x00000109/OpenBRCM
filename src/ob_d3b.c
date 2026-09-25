// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — isolated D3B band-init / d11ac1bsinitvals42 test (M3.4D3B).
 *
 * Module param bsinitvals_test_only=1. From the proven D2B exit it runs the
 * exact D3A1 vendor prefix with the D3A0 DMA engines left LIVE in their vendor
 * position, then reproduces the exact BCM4352/rev42/AC sub_6656c slice:
 *
 *   sub_6656c pre-bs
 *     -> osl_readw(D11+0x3e0)              [read-only, result unused]
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
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>

#include "ob_core.h"
#include "ob_si.h"
#include "ob_ucode.h"
#include "ob_initvals.h"
#include "ob_d3a0.h"
#include "ob_d3a1.h"
#include "ob_d3b.h"

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

	/* exact vendor order and destinations: idx 0..4 -> MHF1..5 */
	for (i = 0; i < OB_D3B_MHF_COUNT; i++)
		ob_ucode_write_shm16(hw, ob_d3b_mhf_shm[i], mhf[i]);

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
		bcma_write16(hw->core, rec->offset, (u16)rec->value);
	else
		bcma_write32(hw->core, rec->offset, rec->value);
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

	st->offset_allowed = true;
	for (i = 0; i < plan.records; i++) {
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

	for (i = 0; i < OB_D3B_MHF_COUNT; i++)
		p->mhf[i] = ob_ucode_read_shm16(hw, ob_d3b_mhf_shm[i]);
	p->shm_10 = ob_d3b_read_shm32(hw, OB_D3B_SHM_OVR_10);
	p->shm_1c = ob_d3b_read_shm32(hw, OB_D3B_SHM_OVR_1C);
	p->shm_94 = ob_d3b_read_shm32(hw, OB_D3B_SHM_OVR_94);
	p->maccontrol = bcma_read32(hw->core, OB_D3A1_REG_MACCONTROL);
	p->macintmask = bcma_read32(hw->core, OB_D3A1_REG_MACINTMASK);
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
	if (p->shm_10 != OB_D3B_SHM_OVR_10_VAL ||
	    p->shm_1c != OB_D3B_SHM_OVR_1C_VAL ||
	    p->shm_94 != OB_D3B_SHM_OVR_94_VAL) {
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

/* ---- top-level bring-up ------------------------------------------------- */

int ob_d3b_test(struct ob_hw *hw)
{
	const struct firmware *biv = NULL;
	struct ob_d3b *st = &hw->d3b;
	int ret;

	/* never re-enter after an unverified quiesce; only a reboot clears it */
	if (ob_d3a0_fatal_is_latched()) {
		dev_crit(hw->dev,
			 "d3b-test: refusing re-entry after fatal quiesce; reboot required\n");
		return -EIO;
	}

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

	/* sub_6656c pre-bs: read-only PHY-version status (blob 0x665c3) */
	st->phyver_status = bcma_read16(hw->core, OB_D3B_REG_PHYVER_STATUS);
	dev_info(hw->dev,
		 "d3b-test: sub_6656c pre-bs phyver_status(0x3e0)=%04x (read-only)\n",
		 st->phyver_status);

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

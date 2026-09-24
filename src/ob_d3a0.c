// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — isolated vendor pre-PHY DMA bring-up test (M3.4D3A0).
 *
 * Module param dma_test_only=1. After the shared hardware-proven D2B prefix
 * it performs, in the exact vendor order, the exactly-pinned D11/IRQ-source
 * writes, then the four TX DMA channels and FIFO0 RX (64 buffers), validates a
 * small provenance-backed set, executes the mandatory quiesce and frees the
 * Linux DMA resources. It STOPS before band init / bsinitvals / PHY / radio /
 * channel / mac80211 and never enables EN_MAC, MACINTMASK, MI_DMAINT or the
 * host IRQ route.
 *
 * Fail-closed: DMA memory is only freed after a verified per-channel stop (or
 * verified core-reset containment). If quiesce cannot be verified, the module
 * enters a fatal/reboot-required state and never frees DMA memory.
 *
 * Provenance: docs/m34d3_bsinitvals.md Appendices C/D,
 * docs/d3a0_dma_test_design.md.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>

#include "ob_core.h"
#include "ob_dma.h"
#include "ob_irq.h"
#include "ob_ucode.h"
#include "ob_d3a0.h"

/* ---- tiny helpers ------------------------------------------------------ */

static void ob_d3a0_write_shm16(struct ob_hw *hw, u16 off, u16 val)
{
	bcma_write32(hw->core, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	(void)bcma_read32(hw->core, OB_UCODE_REG_OBJADDR);
	bcma_write16(hw->core, OB_UCODE_REG_OBJDATA + (off & 0x2), val);
}

/* SICF_MPCLKE lives in the D11 core agent IO control (bit 4). */
static void ob_d3a0_macphyclk_set(struct ob_hw *hw, bool on)
{
	u32 v = bcma_aread32(hw->core, BCMA_IOCTL);

	if (on)
		v |= OB_D3A0_IOCTL_MPCLKE;
	else
		v &= ~OB_D3A0_IOCTL_MPCLKE;
	bcma_awrite32(hw->core, BCMA_IOCTL, v);
	(void)bcma_aread32(hw->core, BCMA_IOCTL);
}

static u32 ob_d3a0_mctrl_update(struct ob_hw *hw, u32 mask, u32 val)
{
	u32 old = bcma_read32(hw->core, OB_D3A0_REG_MACCONTROL);
	u32 new = (old & ~mask) | val;

	bcma_write32(hw->core, OB_D3A0_REG_MACCONTROL, new);
	return bcma_read32(hw->core, OB_D3A0_REG_MACCONTROL);
}

/* ---- exactly-pinned pre-DMA D11 / IRQ-source prefix (vendor order) ------ */

static int ob_d3a0_prefix(struct ob_hw *hw)
{
	u32 machwcap, mctrl, irq;

	/* small SHM tables that precede the interrupt setup in vendor order */
	ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MBURST, OB_D3A0_SHM_MBURST_VAL);
	ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MAXANTCNT,
			    OB_D3A0_SHM_MAXANTCNT_VAL);

	/* INTRCVLAZY[0] = 1 << 24 */
	bcma_write32(hw->core, OB_D3A0_REG_INTRCVLAZY0, OB_D3A0_INTRCVLAZY);

	/* MACCONTROL masked transition: set DISCARD_PMQ, keep PSM_RUN, no EN_MAC */
	mctrl = ob_d3a0_mctrl_update(hw, OB_D3A0_MACCONTROL_MASK,
				     OB_D3A0_MACCONTROL_VAL);
	if (!ob_d3a0_maccontrol_ok(mctrl)) {
		dev_err(hw->dev,
			"dma-test: MACCONTROL transition invalid: %08x\n", mctrl);
		return -EIO;
	}

	/* TSF clock preparation */
	bcma_write32(hw->core, OB_D3A0_REG_TSF_CFPREP, OB_D3A0_TSF_CFPREP);
	bcma_write32(hw->core, OB_D3A0_REG_TSF_CFPSTART, OB_D3A0_TSF_CFPSTART);

	/* MACINTSTATUS W1C of the vendor bit, then per-FIFO source = I_RI */
	bcma_write32(hw->core, OB_D3A0_REG_MACINTSTATUS, OB_D3A0_MI_GP1);
	bcma_write32(hw->core, OB_D3A0_REG_INTCONTROL0_MASK, OB_D3A0_I_RI);
	hw->d3a0.lc.irq_source = true;

	/* MAC-PHY clock control enable (SICF_MPCLKE) */
	ob_d3a0_macphyclk_set(hw, true);

	/* MAC capabilities exposed to ucode via SHM (M_MACHW_VER/CAP) */
	machwcap = bcma_read32(hw->core, OB_D3A0_REG_MACHWCAP);
	ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MACHWVER,
			    (u16)hw->core->id.rev);
	ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MACHWCAP_L,
			    (u16)(machwcap & 0xffff));
	ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MACHWCAP_H,
			    (u16)((machwcap >> 16) & 0xffff));

	/* MACINTMASK must remain 0; the host route is never enabled. */
	irq = bcma_read32(hw->core, OB_D3A0_REG_MACINTMASK);
	if (!ob_d3a0_host_irq_disabled(irq)) {
		dev_err(hw->dev,
			"dma-test: macintmask not 0 before DMA (%08x)\n", irq);
		return -EIO;
	}
	dev_info(hw->dev,
		 "dma-test: irq-source configured intrcvlazy=%08x intmask0=%08x macintmask=%08x\n",
		 bcma_read32(hw->core, OB_D3A0_REG_INTRCVLAZY0),
		 bcma_read32(hw->core, OB_D3A0_REG_INTCONTROL0_MASK),
		 irq);
	return 0;
}

/* ---- DMA ring allocation ---------------------------------------------- */

static int ob_d3a0_ring_alloc(struct ob_hw *hw, struct ob_dma_ring *ring,
			      enum ob_dma_ring_role role, u16 n)
{
	memset(ring, 0, sizeof(*ring));
	ring->role = role;
	ring->n = n;

	ring->alloc_cpu = dma_pool_alloc(hw->d3a0.pool, GFP_KERNEL,
					 &ring->alloc_dma);
	if (!ring->alloc_cpu)
		return -ENOMEM;
	ring->desc_cpu = ring->alloc_cpu;
	ring->desc_dma = ring->alloc_dma;

	if (!IS_ALIGNED((unsigned long)ring->desc_dma, OB_D3A0_RING_ALIGN) ||
	    !ob_dma_addr_in_window(ring->desc_dma)) {
		dev_err(hw->dev,
			"dma-test: ring %d dma=%pad not 8K-aligned/32-bit\n",
			(int)role, &ring->desc_dma);
		dma_pool_free(hw->d3a0.pool, ring->alloc_cpu, ring->alloc_dma);
		memset(ring, 0, sizeof(*ring));
		return -ERANGE;
	}

	memset(ring->desc_cpu, 0, OB_DMA_RING_BYTES);

	if (role == OB_DMA_RING_RX) {
		ring->slot = kcalloc(n, sizeof(*ring->slot), GFP_KERNEL);
		if (!ring->slot) {
			dma_pool_free(hw->d3a0.pool, ring->alloc_cpu,
				      ring->alloc_dma);
			memset(ring, 0, sizeof(*ring));
			return -ENOMEM;
		}
	}
	ring->allocated = true;
	return 0;
}

static void ob_d3a0_ring_release(struct ob_hw *hw, struct ob_dma_ring *ring)
{
	kfree(ring->slot);
	ring->slot = NULL;
	if (ring->alloc_cpu && hw->d3a0.pool)
		dma_pool_free(hw->d3a0.pool, ring->alloc_cpu, ring->alloc_dma);
	ring->alloc_cpu = NULL;
	ring->desc_cpu = NULL;
	ring->desc_dma = 0;
	ring->alloc_dma = 0;
	ring->allocated = false;
}

/* ---- TX channels ------------------------------------------------------- */

static int ob_d3a0_tx_program(struct ob_hw *hw, u32 ch, u32 *old_control)
{
	struct ob_dma_ring *ring = &hw->d3a0.tx[ch];
	u16 base = ob_d3a0_tx_base(ch);
	u32 old, now;

	bcma_write32(hw->core, base + OB_D3A0_D64_ADDRLOW,
		     (u32)ring->desc_dma);
	bcma_write32(hw->core, base + OB_D3A0_D64_ADDRHIGH, OB_DMA_PCIE_H32);

	old = bcma_read32(hw->core, base + OB_D3A0_D64_CONTROL);
	now = ob_d3a0_tx_control(old);
	bcma_write32(hw->core, base + OB_D3A0_D64_CONTROL, now);

	if (old_control)
		*old_control = old;
	dev_info(hw->dev,
		 "dma-test: TX%u programmed base=%03x addrlow=%08x addrhigh=%08x control=%08x->%08x\n",
		 ch, base, (u32)ring->desc_dma, OB_DMA_PCIE_H32, old, now);
	return 0;
}

/* ---- RX ---------------------------------------------------------------- */

static int ob_d3a0_rx_map(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->d3a0.rx;
	struct device *dev = hw->core->dma_dev;
	u32 i;

	for (i = 0; i < OB_DMA_RX_POST_INIT; i++) {
		struct sk_buff *skb;
		dma_addr_t dma;

		skb = alloc_skb(OB_DMA_RX_BUFSZ, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;
		skb_put(skb, OB_DMA_RX_BUFSZ);
		memset(skb->data, 0, OB_RX_HDR_LEN);

		dma = dma_map_single(dev, skb->data, OB_DMA_RX_BUFSZ,
				     DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, dma)) {
			kfree_skb(skb);
			return -EIO;
		}
		if (!ob_dma_addr_in_window(dma) ||
		    !ob_dma_slot_claim(&ring->slot[i], skb, dma)) {
			dma_unmap_single(dev, dma, OB_DMA_RX_BUFSZ,
					 DMA_FROM_DEVICE);
			kfree_skb(skb);
			return -ERANGE;
		}
		hw->d3a0.lc.rx_mapped++;
	}
	return 0;
}

static void ob_d3a0_rx_build_desc(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->d3a0.rx;
	u32 i;

	for (i = 0; i < OB_DMA_RX_POST_INIT; i++)
		ob_rx_desc_build(ob_dma_desc_at(ring, (u16)i),
				 ring->slot[i].dma, false);
	ob_rx_desc_build(ob_dma_desc_at(ring, OB_DMA_RING_DESC_COUNT_RX - 1),
			 0, true);

	dma_wmb();
}

static int ob_d3a0_rx_program(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->d3a0.rx;
	u32 ring_lo = (u32)ring->desc_dma;
	u32 control, lo, hi, status0, status1, ptr;

	dma_wmb();
	bcma_write32(hw->core, OB_D11_RX_ADDRLOW, ring_lo);
	bcma_write32(hw->core, OB_D11_RX_ADDRHIGH, OB_DMA_PCIE_H32);
	bcma_write32(hw->core, OB_D11_RX_PTR, OB_D3A0_RX_PTR);
	bcma_write32(hw->core, OB_D11_RX_CONTROL, OB_D3A0_RX_CONTROL);

	control = bcma_read32(hw->core, OB_D11_RX_CONTROL);
	lo = bcma_read32(hw->core, OB_D11_RX_ADDRLOW);
	hi = bcma_read32(hw->core, OB_D11_RX_ADDRHIGH);
	ptr = bcma_read32(hw->core, OB_D11_RX_PTR);
	status0 = bcma_read32(hw->core, OB_D11_RX_STATUS0);
	status1 = bcma_read32(hw->core, OB_D11_RX_STATUS1);

	dev_info(hw->dev,
		 "dma-test: RX programmed addrlow=%08x addrhigh=%08x ptr=%08x control=%08x status0=%08x status1=%08x (rb ptr_field=%05x)\n",
		 ring_lo, OB_DMA_PCIE_H32, OB_D3A0_RX_PTR, control, status0,
		 status1, ptr & OB_D11_RS0_CD_MASK);

	if ((control & OB_D3A0_RC_RE) == 0 || lo != ring_lo ||
	    hi != OB_DMA_PCIE_H32 || status0 == 0xffffffffu ||
	    ob_d3a0_rx_disabled(status0) || (status1 & OB_D11_RS1_RE_MASK)) {
		dev_err(hw->dev,
			"dma-test: RX programming readback inconsistent\n");
		return -EIO;
	}
	return 0;
}

/* ---- validation -------------------------------------------------------- */

static int ob_d3a0_validate(struct ob_hw *hw, const u32 *tx_old)
{
	u32 ch, i;

	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++) {
		u16 base = ob_d3a0_tx_base(ch);
		u32 ring_lo = (u32)hw->d3a0.tx[ch].desc_dma;
		u32 control = bcma_read32(hw->core,
					  base + OB_D3A0_D64_CONTROL);
		u32 lo = bcma_read32(hw->core, base + OB_D3A0_D64_ADDRLOW);
		u32 hi = bcma_read32(hw->core, base + OB_D3A0_D64_ADDRHIGH);
		u32 s0 = bcma_read32(hw->core, base + OB_D3A0_D64_STATUS0);

		if (!ob_d3a0_tx_control_ok(tx_old[ch], control) ||
		    lo != ring_lo || hi != OB_DMA_PCIE_H32 ||
		    s0 == 0xffffffffu || ob_d3a0_rx_disabled(s0)) {
			dev_err(hw->dev,
				"dma-test: TX%u postcondition fail control=%08x lo=%08x hi=%08x status0=%08x\n",
				ch, control, lo, hi, s0);
			return -EIO;
		}
		dev_info(hw->dev,
			 "dma-test: TX%u validate control=%08x addrhigh=%08x status0=%08x state=%u\n",
			 ch, control, hi, s0, ob_d3a0_status_state(s0) >> 28);
	}

	{
		u32 control = bcma_read32(hw->core, OB_D11_RX_CONTROL);
		u32 hi = bcma_read32(hw->core, OB_D11_RX_ADDRHIGH);
		u32 s0 = bcma_read32(hw->core, OB_D11_RX_STATUS0);
		u32 s1 = bcma_read32(hw->core, OB_D11_RX_STATUS1);

		if (control != OB_D3A0_RX_CONTROL ||
		    hi != OB_DMA_PCIE_H32 || !ob_d3a0_rx_idle(s0) ||
		    (s1 & OB_D11_RS1_RE_MASK)) {
			dev_err(hw->dev,
				"dma-test: RX postcondition fail control=%08x hi=%08x status0=%08x status1=%08x\n",
				control, hi, s0, s1);
			return -EIO;
		}
		dev_info(hw->dev,
			 "dma-test: RX validate control=%08x addrhigh=%08x status0=%08x state=IDLE\n",
			 control, hi, s0);
	}

	i = bcma_read32(hw->core, OB_D3A0_REG_MACINTMASK);
	if (!ob_d3a0_host_irq_disabled(i)) {
		dev_err(hw->dev, "dma-test: macintmask changed: %08x\n", i);
		return -EIO;
	}
	if (bcma_read32(hw->core, OB_D3A0_REG_INTRCVLAZY0) !=
	    OB_D3A0_INTRCVLAZY) {
		dev_err(hw->dev, "dma-test: intrcvlazy[0] mismatch\n");
		return -EIO;
	}
	if (!ob_d3a0_irq_source_ok(
		    bcma_read32(hw->core, OB_D3A0_REG_INTCONTROL0_MASK))) {
		dev_err(hw->dev, "dma-test: I_RI source missing\n");
		return -EIO;
	}
	if (!ob_d3a0_maccontrol_ok(
		    bcma_read32(hw->core, OB_D3A0_REG_MACCONTROL))) {
		dev_err(hw->dev, "dma-test: MACCONTROL postcondition fail\n");
		return -EIO;
	}
	dev_info(hw->dev,
		 "dma-test: bring-up validation PASS (macintmask=%08x intrcvlazy=%08x)\n",
		 i, bcma_read32(hw->core, OB_D3A0_REG_INTRCVLAZY0));
	return 0;
}

/* ---- quiesce ----------------------------------------------------------- */

static int ob_d3a0_tx_reset(struct ob_hw *hw, u32 ch)
{
	u16 base = ob_d3a0_tx_base(ch);
	u32 remaining, s0;

	bcma_write32(hw->core, base + OB_D3A0_D64_CONTROL, OB_D3A0_XC_SE);
	remaining = OB_D3A0_RESET_TIMEOUT;
	for (;;) {
		s0 = bcma_read32(hw->core, base + OB_D3A0_D64_STATUS0);
		if (ob_d3a0_tx_reset_settled(s0))
			break;
		if (ob_d3a0_poll_expired(remaining))
			return -ETIMEDOUT;
		udelay(OB_D3A0_RESET_DELAY_US);
		remaining -= OB_D3A0_RESET_STEP;
	}

	bcma_write32(hw->core, base + OB_D3A0_D64_CONTROL, 0);
	remaining = OB_D3A0_RESET_TIMEOUT;
	for (;;) {
		s0 = bcma_read32(hw->core, base + OB_D3A0_D64_STATUS0);
		if (ob_d3a0_rx_disabled(s0))
			return 0;
		if (ob_d3a0_poll_expired(remaining)) {
			udelay(OB_D3A0_RESET_FINAL_US);
			return -ETIMEDOUT;
		}
		udelay(OB_D3A0_RESET_DELAY_US);
		remaining -= OB_D3A0_RESET_STEP;
	}
}

static int ob_d3a0_rx_reset(struct ob_hw *hw)
{
	u32 remaining, s0;

	bcma_write32(hw->core, OB_D11_RX_CONTROL, 0);
	remaining = OB_D3A0_RESET_TIMEOUT;
	for (;;) {
		s0 = bcma_read32(hw->core, OB_D11_RX_STATUS0);
		if (ob_d3a0_rx_disabled(s0))
			return 0;
		if (ob_d3a0_poll_expired(remaining))
			return -ETIMEDOUT;
		udelay(OB_D3A0_RESET_DELAY_US);
		remaining -= OB_D3A0_RESET_STEP;
	}
}

/*
 * Verified containment fallback: assert the D11 core reset. Returns true only
 * when the core is observed disabled (bcma_core_disable waits RESET_ST and
 * reads back RESET_CTL/IOCTL).
 */
static bool ob_d3a0_core_contain(struct ob_hw *hw)
{
	bcma_core_disable(hw->core, 0);
	return !bcma_core_is_enabled(hw->core);
}

static int ob_d3a0_quiesce(struct ob_hw *hw)
{
	struct ob_d3a0_lifecycle *lc = &hw->d3a0.lc;
	bool all_ok = true;
	u32 ch;

	/* mask the per-FIFO source; MACINTMASK stays 0 (host route disabled) */
	bcma_write32(hw->core, OB_D3A0_REG_INTCONTROL0_MASK,
		     bcma_read32(hw->core, OB_D3A0_REG_INTCONTROL0_MASK) &
		     ~OB_D3A0_I_RI);
	bcma_write32(hw->core, OB_D3A0_REG_MACINTMASK, 0);

	if (lc->rx == OB_D3A0_PROGRAMMED) {
		if (ob_d3a0_rx_reset(hw) == 0)
			dev_info(hw->dev, "dma-test: RX reset PASS\n");
		else {
			dev_err(hw->dev, "dma-test: RX reset TIMEOUT\n");
			all_ok = false;
		}
	}
	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++) {
		if (lc->tx[ch] != OB_D3A0_PROGRAMMED)
			continue;
		if (ob_d3a0_tx_reset(hw, ch) == 0)
			dev_info(hw->dev, "dma-test: TX%u reset PASS\n", ch);
		else {
			dev_err(hw->dev, "dma-test: TX%u reset TIMEOUT\n", ch);
			all_ok = false;
		}
	}

	if (all_ok) {
		lc->quiesced = true;
		dev_info(hw->dev, "dma-test: all DMA engines stopped\n");
		return 0;
	}

	dev_warn(hw->dev,
		 "dma-test: per-channel reset failed; core-reset containment\n");
	if (ob_d3a0_core_contain(hw)) {
		lc->quiesced = true;
		dev_info(hw->dev,
			 "dma-test: core containment verified; safe to free\n");
		return 0;
	}

	lc->fatal = true;
	dev_crit(hw->dev,
		 "dma-test: quiesce NOT verified; DMA memory retained, reboot required\n");
	return -EIO;
}

/* ---- free (only after verified quiesce) -------------------------------- */

static void ob_d3a0_unmap_rx(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->d3a0.rx;
	struct device *dev = hw->core->dma_dev;
	u32 i;

	if (!ring->slot)
		return;
	for (i = 0; i < ring->n; i++) {
		struct sk_buff *skb;
		dma_addr_t dma;

		if (!ob_dma_slot_release(&ring->slot[i], &skb, &dma))
			continue;
		dma_unmap_single(dev, dma, OB_DMA_RX_BUFSZ, DMA_FROM_DEVICE);
		kfree_skb(skb);
	}
	hw->d3a0.lc.rx_mapped = 0;
}

static int ob_d3a0_free_mem(struct ob_hw *hw)
{
	u32 ch;

	if (!ob_d3a0_can_free(&hw->d3a0.lc)) {
		dev_crit(hw->dev,
			 "dma-test: refusing to free under unverified quiesce\n");
		return -EBUSY;
	}

	ob_d3a0_unmap_rx(hw);

	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++)
		if (hw->d3a0.tx[ch].allocated)
			ob_d3a0_ring_release(hw, &hw->d3a0.tx[ch]);
	if (hw->d3a0.rx.allocated)
		ob_d3a0_ring_release(hw, &hw->d3a0.rx);

	if (hw->d3a0.pool_created) {
		dma_pool_destroy(hw->d3a0.pool);
		hw->d3a0.pool_created = false;
	}
	dev_info(hw->dev, "dma-test: rings released\n");
	return 0;
}

static int ob_d3a0_teardown(struct ob_hw *hw)
{
	dev_info(hw->dev, "dma-test: quiesce begin\n");
	if (ob_d3a0_quiesce(hw))
		return -EIO;	/* fatal set; never free */
	return ob_d3a0_free_mem(hw);
}

/* ---- bring-up ---------------------------------------------------------- */

static int ob_d3a0_bringup(struct ob_hw *hw)
{
	struct ob_d3a0_lifecycle *lc = &hw->d3a0.lc;
	u32 tx_old[OB_D3A0_TX_CHANNELS];
	u32 ch;
	int ret;

	hw->d3a0.pool = dma_pool_create("ob-d3a0-ring", hw->core->dma_dev,
					OB_DMA_RING_BYTES, OB_D3A0_RING_ALIGN,
					OB_D3A0_RING_ALIGN);
	if (!hw->d3a0.pool) {
		dev_err(hw->dev, "dma-test: dma_pool_create failed\n");
		return -ENOMEM;
	}
	hw->d3a0.pool_created = true;

	/* allocate the five rings first (software-only) */
	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++) {
		ret = ob_d3a0_ring_alloc(hw, &hw->d3a0.tx[ch],
					 OB_DMA_RING_TX, OB_D3A0_TX_NTXD);
		if (ret)
			return ret;
		lc->tx[ch] = OB_D3A0_ALLOCATED;
	}
	ret = ob_d3a0_ring_alloc(hw, &hw->d3a0.rx, OB_DMA_RING_RX,
				 OB_DMA_RING_DESC_COUNT_RX);
	if (ret)
		return ret;
	lc->rx = OB_D3A0_ALLOCATED;

	/* map + build RX descriptors */
	ret = ob_d3a0_rx_map(hw);
	if (ret) {
		dev_err(hw->dev, "dma-test: RX buffer map failed: %d\n", ret);
		return ret;
	}
	dev_info(hw->dev, "dma-test: RX buffers mapped=%u\n", lc->rx_mapped);
	ob_d3a0_rx_build_desc(hw);

	/* program the four TX channels (vendor dma_txinit) */
	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++) {
		ret = ob_d3a0_tx_program(hw, ch, &tx_old[ch]);
		if (ret)
			return ret;
		lc->tx[ch] = OB_D3A0_PROGRAMMED;
	}

	/* program FIFO0 RX and post the 64 buffers */
	ret = ob_d3a0_rx_program(hw);
	if (ret)
		return ret;
	lc->rx = OB_D3A0_PROGRAMMED;
	dev_info(hw->dev, "dma-test: RX buffers posted=%u\n",
		 OB_DMA_RX_POST_INIT);

	return ob_d3a0_validate(hw, tx_old);
}

int ob_d3a0_test(struct ob_hw *hw)
{
	struct ob_ucode_run run;
	int ret;

	memset(&hw->d3a0, 0, sizeof(hw->d3a0));

	if (hw->core->id.rev != OB_D3A0_PHYREV_REV42) {
		dev_err(hw->dev,
			"dma-test: unsupported D11 core rev %u (need %u)\n",
			hw->core->id.rev, OB_D3A0_PHYREV_REV42);
		return -ENOTSUPP;
	}

	dev_info(hw->dev, "dma-test: BEGIN\n");

	/* proven D2B prefix (ucode upload + PSM start + common initvals) */
	ret = ob_ucode_run_d2a(hw, "dma-test", &run);
	if (ret)
		return ret;
	dev_info(hw->dev,
		 "dma-test: D2B prefix complete writes=%u psm_iter=%u\n",
		 run.written, run.psm_iterations);

	/* exactly-pinned D11 / IRQ-source prefix, before DMA (vendor order) */
	ret = ob_d3a0_prefix(hw);
	if (ret) {
		dev_err(hw->dev, "dma-test: prefix FAIL ret=%d\n", ret);
		return ret;
	}

	/* allocate, program, post and validate */
	ret = ob_d3a0_bringup(hw);
	if (ret) {
		dev_err(hw->dev,
			"dma-test: bring-up FAIL ret=%d; tearing down\n", ret);
		ob_d3a0_teardown(hw);
		return ret;
	}

	/* mandatory same-run teardown */
	ret = ob_d3a0_teardown(hw);
	if (ret)
		return ret;

	dev_info(hw->dev,
		 "dma-test: PASS - bring-up + teardown proven\n");
	dev_info(hw->dev,
		 "dma-test: STOP before remaining D3A1/band/PHY\n");
	return 0;
}

void ob_d3a0_remove(struct ob_hw *hw)
{
	if (!hw->dma_test_only)
		return;

	if (hw->d3a0.lc.fatal) {
		dev_crit(hw->dev,
			 "dma-test: removed in FATAL unverified-quiesce state; DMA memory retained; reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	if (hw->d3a0.pool_created) {
		/* test aborted without teardown: quiesce + free now */
		if (ob_d3a0_teardown(hw)) {
			dev_crit(hw->dev,
				 "dma-test: removal teardown unverified; reboot required\n");
			bcma_set_drvdata(hw->core, NULL);
			return;
		}
	}
	dev_info(hw->dev, "dma-test: removed (DMA resources released)\n");
	bcma_set_drvdata(hw->core, NULL);
}

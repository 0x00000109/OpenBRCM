// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — isolated D3A0 DMA lifecycle test (M3.4D3A0).
 *
 * D3A0 TYPE: ISOLATED DMA LIFECYCLE TEST. NOT a full vendor-prefix
 * reproduction.
 *
 * Module param dma_test_only=1. Starting from the shared hardware-proven D2B
 * exit it programs ONLY the provenance-pinned D11/clock/IRQ-source
 * prerequisites needed to exercise the vendor DMA engines independently: the
 * four TX DMA channels and FIFO0 RX (64 buffers). It validates a small
 * provenance-backed set, executes the mandatory quiesce and frees the Linux
 * DMA resources.
 *
 * Vendor-before-DMA stages that are NOT pinned for implementation are
 * intentionally omitted: `sub_67efd` (TXE0/FIFO fixup) and the runtime
 * NVRAM/BTC/rate/power SHM tail. Final normal-driver integration MUST restore
 * the complete vendor ordering before band init / bsinitvals / PHY bring-up;
 * D3A1 is that integration task. D3A0 STOPS before all of it and never enables
 * EN_MAC, MACINTMASK, MI_DMAINT or the host IRQ route.
 *
 * Fail-closed: DMA memory is freed only after EVERY programmed engine had its
 * own verified normal per-channel stop. Core-reset containment after a failed
 * reset NEVER authorizes a free: it records containment, sets a
 * fatal/reboot-required state, pins the module and retains the DMA memory.
 *
 * The prefix itself is fail-closed too: every trusted direct-register read it
 * depends on (MACCONTROL, MACINTMASK, MACHWCAP, OBJADDR readback, BCMA IOCTL)
 * is validated, and an all-ones result latches device loss and aborts BEFORE
 * the dependent write. No DMA resource is allocated or programmed once the
 * device is gone (Phase 36M).
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
#include "ob_initvals.h"
#include "ob_d3a0.h"
#include "ob_dma_fatal.h"

/*
 * Per-device fatal evidence (Phase 34). A D3A0 quiesce that cannot be verified
 * leaves the DMA engine possibly referencing its retained DMA memory. The
 * record is keyed by the same full admission identity the durable owner uses,
 * so it refuses the failed device (and a replacement board in the same PCI
 * slot) but does not stop a different adapter. The module is pinned once so
 * the state cannot silently disappear through rmmod; only a reboot clears it.
 */

/* ---- tiny helpers ------------------------------------------------------ */

static int ob_d3a0_write_shm16(struct ob_hw *hw, u16 off, u16 val)
{
	u32 back;

	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	/*
	 * The posted readback is the selected SHM word address, a trusted direct
	 * D11 register, never a payload. All-ones means the window is gone: latch
	 * and do NOT issue the dependent OBJDATA write into a dead D11 core.
	 */
	back = ob_d11_read32(hw, OB_UCODE_REG_OBJADDR);
	if (ob_dev_lost_observe32(hw, "D3A0 SHM OBJADDR readback", back))
		return -EIO;
	ob_d11_write16(hw, OB_UCODE_REG_OBJDATA + (off & 0x2), val);
	return 0;
}

/* SICF_MPCLKE lives in the D11 core agent IO control (bit 4). */
static int ob_d3a0_macphyclk_set(struct ob_hw *hw, bool on)
{
	u32 v = ob_axi_read32(hw, BCMA_IOCTL);

	/*
	 * BCMA IOCTL is a trusted direct 32-bit control register: the vendor
	 * sequence never leaves every clock/reset bit asserted, so all-ones is an
	 * inaccessible backplane window and must never be read-modify-written as
	 * if it were a valid configuration.
	 */
	if (ob_dev_lost_observe32(hw, "D3A0 MAC-PHY clock IOCTL read", v))
		return -EIO;
	if (on)
		v |= OB_D3A0_IOCTL_MPCLKE;
	else
		v &= ~OB_D3A0_IOCTL_MPCLKE;
	ob_axi_write32(hw, BCMA_IOCTL, v);
	v = ob_axi_read32(hw, BCMA_IOCTL);
	if (ob_dev_lost_observe32(hw, "D3A0 MAC-PHY clock IOCTL readback", v))
		return -EIO;
	return 0;
}

static int ob_d3a0_mctrl_update(struct ob_hw *hw, u32 mask, u32 val,
				u32 *now)
{
	u32 old = ob_d11_read32(hw, OB_D3A0_REG_MACCONTROL);
	u32 new;

	/*
	 * Fail closed BEFORE the write. An all-ones MACCONTROL is device loss, not
	 * a register state to preserve: writing `(all-ones & ~mask) | val` would
	 * program an arbitrary value into a dead D11 window and then hide the
	 * failure behind a readback. The caller gets -EIO; the loss is never
	 * encoded as a fabricated register value.
	 */
	if (ob_dev_lost_observe32(hw, "D3A0 MACCONTROL read-modify-write read",
				  old))
		return -EIO;

	new = (old & ~mask) | val;
	ob_d11_write32(hw, OB_D3A0_REG_MACCONTROL, new);
	old = ob_d11_read32(hw, OB_D3A0_REG_MACCONTROL);
	if (ob_dev_lost_observe32(hw, "D3A0 MACCONTROL read-modify-write readback",
				  old))
		return -EIO;
	if (now)
		*now = old;
	return 0;
}

/* ---- fatal latch (fail-closed lifetime protection) --------------------- */

bool ob_d3a0_fatal_is_latched(void)
{
	/* Module-wide view for diagnostics only; admission is per device. */
	return ob_dma_fatal_any();
}

static void ob_d3a0_latch_fatal(struct ob_hw *hw, const char *where)
{
	struct ob_dma_owner_identity id;
	struct ob_dma_fatal_evidence ev;
	int ret;
	u32 ch;

	memset(&ev, 0, sizeof(ev));
	ev.tx_channels = OB_D3A0_TX_CHANNELS;
	ev.rx_ring = hw->d3a0.rx.desc_dma;
	ev.rx_mapped = hw->d3a0.lc.rx_mapped;
	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++)
		ev.tx_ring[ch] = hw->d3a0.tx[ch].desc_dma;

	ob_dma_owner_core_identity(hw->core, &id);
	ret = ob_dma_fatal_latch(&id, &ev, where ? where : "ob_d3a0_quiesce",
				 NULL);

	/* The registry takes the module reference itself on the first record;
	 * a full registry cannot store the evidence but must still pin.
	 */
	if (ret == -ENOSPC)
		dev_crit(hw->dev,
			 "dma-test: fatal-evidence registry full; evidence not stored; all further probes refused\n");

	dev_crit(hw->dev,
		 "dma-test: FATAL latch (per device): retained rx_ring=%pad rx_mapped=%u tx_ring[%pad %pad %pad %pad]; module pinned, reboot required\n",
		 &ev.rx_ring, ev.rx_mapped,
		 &ev.tx_ring[0], &ev.tx_ring[1],
		 &ev.tx_ring[2], &ev.tx_ring[3]);
}

/* ---- device-lost fail-safe (central, monotonic) ------------------------ */

bool ob_dev_lost_is_latched(const struct ob_hw *hw)
{
	return hw && hw->dev_lost;
}

void ob_dev_lost_latch(struct ob_hw *hw, const char *where)
{
	if (!hw || hw->dev_lost)
		return;

	hw->dev_lost = true;
	/* Reuse the module-wide fail-closed path: no free, module pinned. */
	hw->d3a0.lc.fatal = true;
	hw->d3a0.lc.engines_stopped = false;
	hw->d3a0.lc.free_allowed = false;
	ob_d3a0_latch_fatal(hw, where ? where : "device-lost");

	dev_crit(hw->dev,
		 "openbrcm: DEVICE LOST (%s) - D11/BCMA/PCIe inaccessible; no further D11 MMIO; DMA memory retained; reboot required\n",
		 where ? where : "all-ones direct read");
}

bool ob_dev_lost_observe32(struct ob_hw *hw, const char *where, u32 val)
{
	if (!hw || hw->dev_lost)
		return hw && hw->dev_lost;
	if (ob_d3a0_mmio_is_all_ones(val)) {
		ob_dev_lost_latch(hw, where);
		return true;
	}
	return false;
}

/*
 * ---- pinned D11/clock/IRQ-source prerequisites for the DMA test ---------
 * (provenance-pinned register values; NOT the complete vendor prefix order)
 */

/*
 * State gate: immediately before the first post-common D3A0 write, the live
 * D11 state MUST equal the hardware-proven D2B exit state. This is read back
 * from the hardware; no approximate state is reconstructed.
 */
static int ob_d3a0_check_d2b_exit(struct ob_hw *hw)
{
	static const u16 shm_off[] = {
		OB_UCODE_SHM_FIFOSIZE0, OB_UCODE_SHM_FIFOSIZE1,
		OB_UCODE_SHM_FIFOSIZE2, OB_UCODE_SHM_FIFOSIZE3,
		OB_INITVALS_SHM14_LO, OB_INITVALS_SHM14_HI,
	};
	u16 shm_val[sizeof(shm_off) / sizeof(shm_off[0])];
	u32 mctrl, macintmask, shm14;
	unsigned int i;
	int ret;

	mctrl = ob_d11_read32(hw, OB_D3A0_REG_MACCONTROL);
	if (ob_dev_lost_observe32(hw, "D3A0 D2B exit MACCONTROL read", mctrl)) {
		dev_err(hw->dev,
			"dma-test: D2B exit state unreadable (MACCONTROL all-ones); DMA forbidden\n");
		return -EIO;
	}
	macintmask = ob_d11_read32(hw, OB_D3A0_REG_MACINTMASK);
	if (ob_dev_lost_observe32(hw, "D3A0 D2B exit MACINTMASK read",
				  macintmask)) {
		dev_err(hw->dev,
			"dma-test: D2B exit state unreadable (MACINTMASK all-ones); DMA forbidden\n");
		return -EIO;
	}

	/*
	 * The 16-bit SHM payload words are never classified as device loss, but
	 * the OBJADDR selection/readback they go through is a trusted direct
	 * register: the checked helper latches and fails there, before the next
	 * OBJADDR write of the following word.
	 */
	for (i = 0; i < sizeof(shm_off) / sizeof(shm_off[0]); i++) {
		ret = ob_ucode_read_shm16_checked(hw, shm_off[i], &shm_val[i]);
		if (ret) {
			dev_err(hw->dev,
				"dma-test: D2B exit SHM read %03x failed (%d); DMA forbidden\n",
				shm_off[i], ret);
			return ret;
		}
	}
	shm14 = (u32)shm_val[4] | ((u32)shm_val[5] << 16);

	if (mctrl != OB_INITVALS_MACCONTROL_EXPECTED ||
	    macintmask != OB_INITVALS_MACINTMASK_EXPECTED ||
	    shm_val[0] != OB_INITVALS_FIFOSIZE0_EXPECTED ||
	    shm_val[1] != OB_INITVALS_FIFOSIZE1_EXPECTED ||
	    shm_val[2] != OB_INITVALS_FIFOSIZE2_EXPECTED ||
	    shm_val[3] != OB_INITVALS_FIFOSIZE3_EXPECTED ||
	    shm14 != OB_INITVALS_SHM14_EXPECTED) {
		dev_err(hw->dev,
			"dma-test: D2B exit state mismatch maccontrol=%08x macintmask=%08x fifo=%04x/%04x/%04x/%04x shm14=%08x; DMA forbidden\n",
			mctrl, macintmask, shm_val[0], shm_val[1], shm_val[2],
			shm_val[3], shm14);
		return -EIO;
	}

	dev_info(hw->dev,
		 "dma-test: D2B exit verified maccontrol=%08x macintmask=%08x fifo=%04x/%04x/%04x/%04x shm14=%08x\n",
		 mctrl, macintmask, shm_val[0], shm_val[1], shm_val[2],
		 shm_val[3], shm14);
	return 0;
}

static int ob_d3a0_prefix(struct ob_hw *hw)
{
	u32 machwcap, mctrl, irq, lazy, intmask0;
	int ret;

	/* hard gate: no post-common write until the D2B exit state is proven */
	ret = ob_d3a0_check_d2b_exit(hw);
	if (ret)
		return ret;

	/* small SHM tables that precede the interrupt setup in vendor order */
	if (ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MBURST,
				OB_D3A0_SHM_MBURST_VAL))
		goto lost;
	if (ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MAXANTCNT,
				OB_D3A0_SHM_MAXANTCNT_VAL))
		goto lost;

	/* INTRCVLAZY[0] = 1 << 24 */
	ob_d11_write32(hw, OB_D3A0_REG_INTRCVLAZY0, OB_D3A0_INTRCVLAZY);

	/* MACCONTROL masked transition: set DISCARD_PMQ, keep PSM_RUN, no EN_MAC */
	ret = ob_d3a0_mctrl_update(hw, OB_D3A0_MACCONTROL_MASK,
				   OB_D3A0_MACCONTROL_VAL, &mctrl);
	if (ret)
		goto lost;
	if (!ob_d3a0_maccontrol_ok(mctrl)) {
		dev_err(hw->dev,
			"dma-test: MACCONTROL transition invalid: %08x\n", mctrl);
		return -EIO;
	}

	/* TSF clock preparation */
	ob_d11_write32(hw, OB_D3A0_REG_TSF_CFPREP, OB_D3A0_TSF_CFPREP);
	ob_d11_write32(hw, OB_D3A0_REG_TSF_CFPSTART, OB_D3A0_TSF_CFPSTART);

	/* MACINTSTATUS W1C of the vendor bit, then per-FIFO source = I_RI */
	ob_d11_write32(hw, OB_D3A0_REG_MACINTSTATUS, OB_D3A0_MI_GP1);
	ob_d11_write32(hw, OB_D3A0_REG_INTCONTROL0_MASK, OB_D3A0_I_RI);
	hw->d3a0.lc.irq_source = true;

	/* MAC-PHY clock control enable (SICF_MPCLKE) */
	if (ob_d3a0_macphyclk_set(hw, true))
		goto lost;

	/* MAC capabilities exposed to ucode via SHM (M_MACHW_VER/CAP) */
	machwcap = ob_d11_read32(hw, OB_D3A0_REG_MACHWCAP);
	if (ob_dev_lost_observe32(hw, "D3A0 prefix MACHWCAP read", machwcap)) {
		dev_err(hw->dev,
			"dma-test: MACHWCAP unreadable (all-ones); no SHM capability write\n");
		goto lost;
	}
	if (ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MACHWVER,
				(u16)hw->core->id.rev))
		goto lost;
	if (ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MACHWCAP_L,
				(u16)(machwcap & 0xffff)))
		goto lost;
	if (ob_d3a0_write_shm16(hw, OB_D3A0_SHM_MACHWCAP_H,
				(u16)((machwcap >> 16) & 0xffff)))
		goto lost;

	/* MACINTMASK must remain 0; the host route is never enabled. */
	irq = ob_d11_read32(hw, OB_D3A0_REG_MACINTMASK);
	if (ob_dev_lost_observe32(hw, "D3A0 prefix MACINTMASK read", irq))
		goto lost;
	if (!ob_d3a0_host_irq_disabled(irq)) {
		dev_err(hw->dev,
			"dma-test: macintmask not 0 before DMA (%08x)\n", irq);
		return -EIO;
	}

	/*
	 * The readbacks used for the trace are separate statements: C leaves the
	 * evaluation order of function arguments unspecified, so an access whose
	 * all-ones result must abort the prefix can never live inside a dev_*()
	 * argument list.
	 */
	lazy = ob_d11_read32(hw, OB_D3A0_REG_INTRCVLAZY0);
	if (ob_dev_lost_observe32(hw, "D3A0 prefix INTRCVLAZY readback", lazy))
		goto lost;
	intmask0 = ob_d11_read32(hw, OB_D3A0_REG_INTCONTROL0_MASK);
	if (ob_dev_lost_observe32(hw, "D3A0 prefix INTCONTROL0_MASK readback",
				  intmask0))
		goto lost;
	dev_info(hw->dev,
		 "dma-test: irq-source configured intrcvlazy=%08x intmask0=%08x macintmask=%08x\n",
		 lazy, intmask0, irq);
	return 0;

lost:
	dev_err(hw->dev,
		"dma-test: prefix aborted on device loss; no further D11/BCMA access, DMA forbidden\n");
	return -EIO;
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

/*
 * Phase 36N trusted-read rule for the DMA programming/validation stage.
 *
 * None of the registers read by this stage can legally read all-ones:
 *   - D64_CONTROL/PTR/ADDRLOW/ADDRHIGH are written by this driver one
 *     instruction earlier, so their readback is fully determined by the
 *     values written here (and by the prefix for CONTROL's other bits);
 *   - STATUS0/STATUS1 carry a 4-bit engine state in bits 31:28 and never
 *     settle to all-ones (rev42 RS_MASK states are DISABLED/STOPPED/IDLE/
 *     ACTIVE only);
 *   - MACINTMASK/INTRCVLAZY0/INTCONTROL0_MASK/MACCONTROL are programmed by
 *     the prefix and are constant while D3A0 runs.
 * A 0xffffffff readback is therefore an authoritative bus-loss observation,
 * not a programming error. Latch it once and return false so the caller can
 * stop before issuing any further MMIO or dependent write.
 */
static bool ob_d3a0_read_trusted(struct ob_hw *hw, u32 val, const char *what)
{
	if (val != 0xffffffffu)
		return true;
	ob_dev_lost_latch(hw, what);
	return false;
}

/* ---- TX channels ------------------------------------------------------- */

static int ob_d3a0_tx_program(struct ob_hw *hw, u32 ch, u32 *old_control)
{
	struct ob_dma_ring *ring = &hw->d3a0.tx[ch];
	u16 base = ob_d3a0_tx_base(ch);
	u32 old, now;

	ob_d11_write32(hw, base + OB_D3A0_D64_ADDRLOW,
		     (u32)ring->desc_dma);
	ob_d11_write32(hw, base + OB_D3A0_D64_ADDRHIGH, OB_DMA_PCIE_H32);

	/*
	 * The RMW base is trusted: it is never all-ones on a live bus, and the
	 * dependent CONTROL write below must not be issued from such a value.
	 */
	old = ob_d11_read32(hw, base + OB_D3A0_D64_CONTROL);
	if (!ob_d3a0_read_trusted(hw, old, "D3A0 TX D64_CONTROL")) {
		dev_err(hw->dev,
			"dma-test: TX%u control read lost, not programmed\n", ch);
		return -EIO;
	}
	now = ob_d3a0_tx_control(old);
	ob_d11_write32(hw, base + OB_D3A0_D64_CONTROL, now);

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
	ob_d11_write32(hw, OB_D11_RX_ADDRLOW, ring_lo);
	ob_d11_write32(hw, OB_D11_RX_ADDRHIGH, OB_DMA_PCIE_H32);
	ob_d11_write32(hw, OB_D11_RX_PTR, OB_D3A0_RX_PTR);
	ob_d11_write32(hw, OB_D11_RX_CONTROL, OB_D3A0_RX_CONTROL);

	/*
	 * Each readback is validated before the next register is touched, so a
	 * loss observed here costs exactly one read and no write at all.
	 */
	control = ob_d11_read32(hw, OB_D11_RX_CONTROL);
	if (!ob_d3a0_read_trusted(hw, control, "D3A0 RX control"))
		goto lost;
	if ((control & OB_D3A0_RC_RE) == 0) {
		dev_err(hw->dev, "dma-test: RX control RE clear: %08x\n",
			control);
		return -EIO;
	}
	lo = ob_d11_read32(hw, OB_D11_RX_ADDRLOW);
	if (!ob_d3a0_read_trusted(hw, lo, "D3A0 RX addrlow"))
		goto lost;
	if (lo != ring_lo) {
		dev_err(hw->dev, "dma-test: RX addrlow %08x != %08x\n",
			lo, ring_lo);
		return -EIO;
	}
	hi = ob_d11_read32(hw, OB_D11_RX_ADDRHIGH);
	if (!ob_d3a0_read_trusted(hw, hi, "D3A0 RX addrhigh"))
		goto lost;
	if (hi != OB_DMA_PCIE_H32) {
		dev_err(hw->dev, "dma-test: RX addrhigh %08x != %08x\n",
			hi, OB_DMA_PCIE_H32);
		return -EIO;
	}
	ptr = ob_d11_read32(hw, OB_D11_RX_PTR);
	if (!ob_d3a0_read_trusted(hw, ptr, "D3A0 RX ptr"))
		goto lost;
	status0 = ob_d11_read32(hw, OB_D11_RX_STATUS0);
	if (!ob_d3a0_read_trusted(hw, status0, "D3A0 RX status0"))
		goto lost;
	if (ob_d3a0_rx_disabled(status0)) {
		dev_err(hw->dev, "dma-test: RX status0 %08x disabled\n",
			status0);
		return -EIO;
	}
	status1 = ob_d11_read32(hw, OB_D11_RX_STATUS1);
	if (!ob_d3a0_read_trusted(hw, status1, "D3A0 RX status1"))
		goto lost;
	if (status1 & OB_D11_RS1_RE_MASK) {
		dev_err(hw->dev, "dma-test: RX status1 %08x RE set\n", status1);
		return -EIO;
	}

	dev_info(hw->dev,
		 "dma-test: RX programmed addrlow=%08x addrhigh=%08x ptr=%08x control=%08x status0=%08x status1=%08x (rb ptr_field=%05x)\n",
		 ring_lo, OB_DMA_PCIE_H32, OB_D3A0_RX_PTR, control, status0,
		 status1, ptr & OB_D11_RS0_CD_MASK);
	return 0;

lost:
	dev_err(hw->dev, "dma-test: RX programming lost, readback untrusted\n");
	return -EIO;
}

/* ---- validation -------------------------------------------------------- */

static int ob_d3a0_validate(struct ob_hw *hw, const u32 *tx_old)
{
	u32 ch, i, macintmask;

	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++) {
		u16 base = ob_d3a0_tx_base(ch);
		u32 ring_lo = (u32)hw->d3a0.tx[ch].desc_dma;
		u32 control, lo, hi, s0;

		/*
		 * Trusted-read sequence: every readback is validated before the
		 * next register is touched, so the first loss costs one read and
		 * no further MMIO.
		 */
		control = ob_d11_read32(hw, base + OB_D3A0_D64_CONTROL);
		if (!ob_d3a0_read_trusted(hw, control,
					  "D3A0 TX validate control"))
			return -EIO;
		if (!ob_d3a0_tx_control_ok(tx_old[ch], control)) {
			dev_err(hw->dev,
				"dma-test: TX%u control postcondition fail %08x (old %08x)\n",
				ch, control, tx_old[ch]);
			return -EIO;
		}
		lo = ob_d11_read32(hw, base + OB_D3A0_D64_ADDRLOW);
		if (!ob_d3a0_read_trusted(hw, lo, "D3A0 TX validate addrlow"))
			return -EIO;
		if (lo != ring_lo) {
			dev_err(hw->dev, "dma-test: TX%u addrlow %08x != %08x\n",
				ch, lo, ring_lo);
			return -EIO;
		}
		hi = ob_d11_read32(hw, base + OB_D3A0_D64_ADDRHIGH);
		if (!ob_d3a0_read_trusted(hw, hi, "D3A0 TX validate addrhigh"))
			return -EIO;
		if (hi != OB_DMA_PCIE_H32) {
			dev_err(hw->dev, "dma-test: TX%u addrhigh %08x != %08x\n",
				ch, hi, OB_DMA_PCIE_H32);
			return -EIO;
		}
		s0 = ob_d11_read32(hw, base + OB_D3A0_D64_STATUS0);
		if (!ob_d3a0_read_trusted(hw, s0, "D3A0 TX validate status0"))
			return -EIO;
		if (ob_d3a0_rx_disabled(s0)) {
			dev_err(hw->dev, "dma-test: TX%u status0 %08x disabled\n",
				ch, s0);
			return -EIO;
		}
		dev_info(hw->dev,
			 "dma-test: TX%u validate control=%08x addrhigh=%08x status0=%08x state=%u\n",
			 ch, control, hi, s0, ob_d3a0_status_state(s0) >> 28);
	}

	{
		u32 control, hi, s0, s1;

		control = ob_d11_read32(hw, OB_D11_RX_CONTROL);
		if (!ob_d3a0_read_trusted(hw, control,
					  "D3A0 RX validate control"))
			return -EIO;
		if (control != OB_D3A0_RX_CONTROL) {
			dev_err(hw->dev, "dma-test: RX control %08x != %08x\n",
				control, OB_D3A0_RX_CONTROL);
			return -EIO;
		}
		hi = ob_d11_read32(hw, OB_D11_RX_ADDRHIGH);
		if (!ob_d3a0_read_trusted(hw, hi, "D3A0 RX validate addrhigh"))
			return -EIO;
		if (hi != OB_DMA_PCIE_H32) {
			dev_err(hw->dev, "dma-test: RX addrhigh %08x != %08x\n",
				hi, OB_DMA_PCIE_H32);
			return -EIO;
		}
		s0 = ob_d11_read32(hw, OB_D11_RX_STATUS0);
		if (!ob_d3a0_read_trusted(hw, s0, "D3A0 RX validate status0"))
			return -EIO;
		if (!ob_d3a0_rx_idle(s0)) {
			dev_err(hw->dev, "dma-test: RX status0 %08x not idle\n",
				s0);
			return -EIO;
		}
		s1 = ob_d11_read32(hw, OB_D11_RX_STATUS1);
		if (!ob_d3a0_read_trusted(hw, s1, "D3A0 RX validate status1"))
			return -EIO;
		if (s1 & OB_D11_RS1_RE_MASK) {
			dev_err(hw->dev, "dma-test: RX status1 %08x RE set\n", s1);
			return -EIO;
		}
		dev_info(hw->dev,
			 "dma-test: RX validate control=%08x addrhigh=%08x status0=%08x state=IDLE\n",
			 control, hi, s0);
	}

	macintmask = ob_d11_read32(hw, OB_D3A0_REG_MACINTMASK);
	if (!ob_d3a0_read_trusted(hw, macintmask,
				  "D3A0 validate macintmask"))
		return -EIO;
	if (!ob_d3a0_host_irq_disabled(macintmask)) {
		dev_err(hw->dev, "dma-test: macintmask changed: %08x\n",
			macintmask);
		return -EIO;
	}
	i = ob_d11_read32(hw, OB_D3A0_REG_INTRCVLAZY0);
	if (!ob_d3a0_read_trusted(hw, i, "D3A0 validate intrcvlazy0"))
		return -EIO;
	if (i != OB_D3A0_INTRCVLAZY) {
		dev_err(hw->dev, "dma-test: intrcvlazy[0] mismatch\n");
		return -EIO;
	}
	i = ob_d11_read32(hw, OB_D3A0_REG_INTCONTROL0_MASK);
	if (!ob_d3a0_read_trusted(hw, i, "D3A0 validate intcontrol0_mask"))
		return -EIO;
	if (!ob_d3a0_irq_source_ok(i)) {
		dev_err(hw->dev, "dma-test: I_RI source missing\n");
		return -EIO;
	}
	i = ob_d11_read32(hw, OB_D3A0_REG_MACCONTROL);
	if (!ob_d3a0_read_trusted(hw, i, "D3A0 validate maccontrol"))
		return -EIO;
	if (!ob_d3a0_maccontrol_ok(i)) {
		dev_err(hw->dev, "dma-test: MACCONTROL postcondition fail\n");
		return -EIO;
	}
	dev_info(hw->dev,
		 "dma-test: bring-up validation PASS (macintmask=%08x intrcvlazy=%08x)\n",
		 macintmask, ob_d11_read32(hw, OB_D3A0_REG_INTRCVLAZY0));
	return 0;
}

/* ---- quiesce ----------------------------------------------------------- */

static int ob_d3a0_tx_reset(struct ob_hw *hw, u32 ch)
{
	u16 base = ob_d3a0_tx_base(ch);
	u32 remaining, s0;

	if (hw->dev_lost)
		return -EIO;
	ob_d11_write32(hw, base + OB_D3A0_D64_CONTROL, OB_D3A0_XC_SE);
	remaining = OB_D3A0_RESET_TIMEOUT;
	for (;;) {
		s0 = ob_d11_read32(hw, base + OB_D3A0_D64_STATUS0);
		if (ob_d3a0_tx_reset_settled(s0))
			break;
		if (ob_d3a0_poll_expired(remaining))
			return -ETIMEDOUT;
		udelay(OB_D3A0_RESET_DELAY_US);
		remaining -= OB_D3A0_RESET_STEP;
	}

	ob_d11_write32(hw, base + OB_D3A0_D64_CONTROL, 0);
	remaining = OB_D3A0_RESET_TIMEOUT;
	for (;;) {
		s0 = ob_d11_read32(hw, base + OB_D3A0_D64_STATUS0);
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

	if (hw->dev_lost)
		return -EIO;
	ob_d11_write32(hw, OB_D11_RX_CONTROL, 0);
	remaining = OB_D3A0_RESET_TIMEOUT;
	for (;;) {
		s0 = ob_d11_read32(hw, OB_D11_RX_STATUS0);
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
	/* Device-loss guard: no core reset/disable after the latch. */
	if (hw->dev_lost)
		return false;
	bcma_core_disable(hw->core, 0);
	return !bcma_core_is_enabled(hw->core);
}

static int ob_d3a0_quiesce(struct ob_hw *hw)
{
	struct ob_d3a0_lifecycle *lc = &hw->d3a0.lc;
	bool all_ok = true;
	u32 ch, intmask0;

	/*
	 * Device-lost fail-safe: NEVER write an interrupt/DMA register after the
	 * D11 window was observed all-ones. Latch fatal and retain everything.
	 */
	if (hw->dev_lost) {
		dev_crit(hw->dev,
			 "dma-test: refusing quiesce after device loss; DMA memory retained; reboot required\n");
		lc->engines_stopped = false;
		lc->free_allowed = false;
		lc->fatal = true;
		return -EIO;
	}

	/*
	 * The first access is a read; an all-ones result means device loss and
	 * must abort BEFORE the read-modify-write below (no garbage write).
	 */
	intmask0 = ob_d11_read32(hw, OB_D3A0_REG_INTCONTROL0_MASK);
	if (ob_dev_lost_observe32(hw, "D3A0 quiesce intmask0", intmask0)) {
		lc->engines_stopped = false;
		lc->free_allowed = false;
		lc->fatal = true;
		return -EIO;
	}
	/* mask the per-FIFO source; MACINTMASK stays 0 (host route disabled) */
	ob_d11_write32(hw, OB_D3A0_REG_INTCONTROL0_MASK,
		     intmask0 & ~OB_D3A0_I_RI);
	ob_d11_write32(hw, OB_D3A0_REG_MACINTMASK, 0);

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
		/* Normal path: every programmed engine verified stopped. */
		lc->engines_stopped = true;
		lc->free_allowed = true;
		dev_info(hw->dev, "dma-test: all DMA engines stopped\n");
		return 0;
	}

	/*
	 * A per-channel reset failed. Core disable is attempted as CONTAINMENT
	 * only: it may reduce further DMA, but it does NOT prove that already
	 * issued PCIe transactions have drained, so it must NEVER authorize a
	 * free. The retained DMA memory is what makes any late/outstanding
	 * transaction harmless. No PCIe flush is invented.
	 */
	dev_warn(hw->dev,
		 "dma-test: per-channel reset failed; attempting core containment (never authorizes free)\n");
	if (ob_d3a0_core_contain(hw)) {
		lc->core_contained = true;
		dev_warn(hw->dev,
			 "dma-test: core containment observed (bcma_core_is_enabled==false); free still forbidden\n");
	} else {
		dev_err(hw->dev,
			"dma-test: core containment not verified either\n");
	}

	lc->engines_stopped = false;
	lc->free_allowed = false;
	lc->fatal = true;
	ob_d3a0_latch_fatal(hw, "ob_d3a0_quiesce");
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
		/* Ownership transfer, never a free: the registry-held owner keeps the
		 * pool/ring handles reachable after `struct ob_hw` is gone and
		 * refuses every later release. */
		ob_dma_owner_retain_d3a0(hw, OB_DMA_OWNER_REASON_FATAL_TEARDOWN,
					 "ob_d3a0_free_mem");
		dev_crit(hw->dev,
			 "dma-test: refusing to free under unverified quiesce (owner retained)\n");
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
	/* Every class is released and the per-channel stop was verified; only now
	 * may the owner's census be cleared and the owner dropped. */
	ob_dma_owner_finish(hw, OB_DMA_OWNER_FAMILY_D3A0, true);
	return 0;
}

int ob_d3a0_teardown(struct ob_hw *hw)
{
	/*
	 * Device-lost fail-safe: no MMIO at all once the D11 window read
	 * all-ones. Latch fatal, retain DMA memory, require reboot.
	 */
	if (hw->dev_lost) {
		ob_dma_owner_retain_d3a0(hw, OB_DMA_OWNER_REASON_DEVICE_LOST,
					 "ob_d3a0_teardown:dev-lost");
		dev_crit(hw->dev,
			 "dma-test: refusing teardown after device loss; DMA memory retained under a registry-held owner; reboot required\n");
		hw->d3a0.lc.engines_stopped = false;
		hw->d3a0.lc.free_allowed = false;
		hw->d3a0.lc.fatal = true;
		return -EIO;
	}
	dev_info(hw->dev, "dma-test: quiesce begin\n");
	if (ob_d3a0_quiesce(hw)) {
		/* fatal set; never free */
		ob_dma_owner_retain_d3a0(hw,
					 OB_DMA_OWNER_REASON_QUIESCE_UNCONFIRMED,
					 "ob_d3a0_teardown:quiesce");
		return -EIO;
	}
	return ob_d3a0_free_mem(hw);
}

/* ---- bring-up ---------------------------------------------------------- */

int ob_d3a0_bringup(struct ob_hw *hw)
{
	struct ob_d3a0_lifecycle *lc = &hw->d3a0.lc;
	u32 tx_old[OB_D3A0_TX_CHANNELS];
	u32 ch;
	int ret;

	/*
	 * Durable owner first: every DMA allocation below needs an accountable
	 * holder that survives the devres release of `struct ob_hw`. A refused
	 * owner (retained record for this identity, or a saturated registry) is
	 * fail-closed: no pool, no ring, no DMA programming.
	 */
	ret = ob_dma_owner_ensure(hw, OB_DMA_OWNER_FAMILY_D3A0);
	if (ret)
		return ret;

	hw->d3a0.pool = dma_pool_create("ob-d3a0-ring", hw->core->dma_dev,
					OB_DMA_RING_BYTES, OB_D3A0_RING_ALIGN,
					OB_D3A0_RING_ALIGN);
	if (!hw->d3a0.pool) {
		dev_err(hw->dev, "dma-test: dma_pool_create failed\n");
		/* No DMA-visible allocation exists; the owner may be released. */
		ob_dma_owner_finish(hw, OB_DMA_OWNER_FAMILY_D3A0, true);
		return -ENOMEM;
	}
	hw->d3a0.pool_created = true;
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_POOL, 1);

	/* allocate the five rings first (software-only) */
	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++) {
		ret = ob_d3a0_ring_alloc(hw, &hw->d3a0.tx[ch],
					 OB_DMA_RING_TX, OB_D3A0_TX_NTXD);
		if (ret)
			return ret;
		lc->tx[ch] = OB_D3A0_ALLOCATED;
		(void)ob_dma_owner_add(hw->dma_owner,
				       OB_DMA_OWNER_CLASS_RING_DESC, 1);
		(void)ob_dma_owner_add(hw->dma_owner,
				       OB_DMA_OWNER_CLASS_RING_SLOT, 1);
	}
	ret = ob_d3a0_ring_alloc(hw, &hw->d3a0.rx, OB_DMA_RING_RX,
				 OB_DMA_RING_DESC_COUNT_RX);
	if (ret)
		return ret;
	lc->rx = OB_D3A0_ALLOCATED;
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_DESC, 1);
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_RING_SLOT, 1);

	/* map + build RX descriptors */
	ret = ob_d3a0_rx_map(hw);
	if (ret) {
		dev_err(hw->dev, "dma-test: RX buffer map failed: %d\n", ret);
		return ret;
	}
	(void)ob_dma_owner_add(hw->dma_owner, OB_DMA_OWNER_CLASS_RX_BUFFER,
			       lc->rx_mapped);
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
	struct ob_initvals_post post;
	int ret;

	/*
	 * Shared, side-effect-free admission gate (Phase 36K, closes R7).
	 *
	 * Previously this entry checked only the identity-keyed fatal record and
	 * then erased hw->d3a0.  The fatal implication was the only thing keeping
	 * a live pool, allocated rings, RX mappings or an attached/retained owner
	 * from being lost by that memset.  The same predicate used before the
	 * D3A1/D3B prefix now runs here, before the reset, and also rejects a
	 * lost device, a FIFO latch, a reboot-required tail record, prior FIFO
	 * programming and any still-owned D3A0 state.
	 *
	 * It returns -EINVAL for a missing hw/core, -EIO for loss/fatal/retained/
	 * reboot-required states and -EBUSY for FIFO, duplicate-run and live-DMA
	 * states.  No register is read and no state is modified on refusal.
	 */
	ret = ob_d11_prefix_entry_preflight(hw, "dma-test");
	if (ret)
		return ret;

	memset(&hw->d3a0, 0, sizeof(hw->d3a0));

	if (hw->core->id.rev != OB_D3A0_PHYREV_REV42) {
		dev_err(hw->dev,
			"dma-test: unsupported D11 core rev %u (need %u)\n",
			hw->core->id.rev, OB_D3A0_PHYREV_REV42);
		return -ENOTSUPP;
	}

	dev_info(hw->dev, "dma-test: BEGIN\n");

	/*
	 * D2B: the shared hardware-proven D2A core followed by EXACTLY the 610
	 * common-initvals records (113 x 16-bit, 497 x 32-bit) and the
	 * provenance-backed postcondition gate. The D3A0 entry state IS the D2B
	 * exit; every post-common/DMA action is forbidden unless this succeeds.
	 */
	ret = ob_initvals_run_d2b(hw, "dma-test", &run, &post);
	if (ret)
		return ret;
	if (!ob_d3a0_d2b_state_ok(&post)) {
		dev_err(hw->dev,
			"dma-test: D2B postconditions not satisfied; DMA forbidden\n");
		return -EIO;
	}
	dev_info(hw->dev,
		 "dma-test: D2B prefix complete common_records=%u writes_issued=%u writes_confirmed=%u psm_iter=%u\n",
		 OB_INITVALS_RECORDS, run.issued, run.confirmed,
		 run.psm_iterations);

	/*
	 * Minimal pinned D11/clock/IRQ-source prerequisites for independent DMA
	 * programming. This is NOT the complete vendor post-common prefix (see
	 * the file header): sub_67efd and the NVRAM/BTC/rate/power SHM tail are
	 * omitted and must be restored by D3A1 before normal PHY bring-up.
	 */
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
		ob_dma_owner_retain_d3a0(hw, OB_DMA_OWNER_REASON_REMOVE_RETAINED,
					 "ob_d3a0_remove");
		dev_crit(hw->dev,
			 "dma-test: removed in FATAL unverified-quiesce state; DMA memory retained under a registry-held owner; reboot required\n");
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

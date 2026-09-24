// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — FIFO0 RX engine bring-up (M3.4B, safe first RX).
 *
 * Programs only the proven FIFO0 RX DMA block (D11 + 0x220) with 64 mapped
 * buffers and the recovered 8 KiB-aligned 256-descriptor ring. No TX, no scan,
 * no association, and no ieee80211_rx*(): completed frames are only logged
 * (at most 5) for diagnostics.
 *
 * Provenance: docs/rx_path.md (M3.4A).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_dma.h"
#include "ob_irq.h"
#include "ob_rx.h"

static void ob_rx_log_frame(struct ob_hw *hw, u32 idx, u32 status0, u32 status1,
			    const u8 *hdr, u32 flen)
{
	const u8 *frame = hdr + OB_RX_HDR_LEN;
	u16 fc = ob_rx_fc(frame);
	u8 type = ob_rx_fc_type(fc);
	u8 subtype = ob_rx_fc_subtype(fc);
	char hex[3 * 32 + 1];
	int n = 0;
	u32 dump = min_t(u32, flen, 32);
	u32 i;

	for (i = 0; i < dump; i++)
		n += scnprintf(hex + n, sizeof(hex) - n, "%02x", frame[i]);
	hex[n] = '\0';

	dev_info(hw->dev,
		 "rx: frame idx=%u status0=%08x status1=%08x rxlen=%u fc=%04x type=%u subtype=%u chan=%04x\n",
		 idx, status0, status1, flen, fc, type, subtype,
		 hdr[22] | (hdr[23] << 8));
	dev_info(hw->dev, "rx: bytes=%s\n", hex);
	if (type == 0 && subtype == 8)
		dev_info(hw->dev, "rx: beacon detected\n");
}

/*
 * Allocate and map exactly OB_DMA_RX_POST_INIT buffers. Any failure unwinds
 * every previously mapped buffer and leaves no partially posted ring.
 */
static int ob_rx_alloc_buffers(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->dma.rx;
	struct device *dev = hw->core->dma_dev;
	u32 i;

	for (i = 0; i < OB_DMA_RX_POST_INIT; i++) {
		struct sk_buff *skb;
		dma_addr_t dma;

		skb = alloc_skb(OB_DMA_RX_BUFSZ, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;
		skb_put(skb, OB_DMA_RX_BUFSZ);
		/* Initialise before the mapping transfers ownership to the device. */
		memset(skb->data, 0, OB_RX_HDR_LEN);

		dma = dma_map_single(dev, skb->data, OB_DMA_RX_BUFSZ,
				     DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, dma)) {
			kfree_skb(skb);
			return -EIO;
		}
		if (!ob_dma_addr_in_window(dma)) {
			dma_unmap_single(dev, dma, OB_DMA_RX_BUFSZ,
					 DMA_FROM_DEVICE);
			kfree_skb(skb);
			return -ERANGE;
		}
		if (!ob_dma_slot_claim(&ring->slot[i], skb, dma)) {
			dma_unmap_single(dev, dma, OB_DMA_RX_BUFSZ,
					 DMA_FROM_DEVICE);
			kfree_skb(skb);
			return -EBUSY;
		}
	}

	ring->head = OB_DMA_RX_POST_INIT;	/* producer */
	ring->tail = 0;				/* consumer */
	hw->rx.posted = OB_DMA_RX_POST_INIT;
	hw->rx.tail = 0;
	return 0;
}

void ob_rx_free_buffers(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->dma.rx;
	struct device *dev = hw->core->dma_dev;
	u32 i;

	for (i = 0; i < ring->n; i++) {
		struct sk_buff *skb;
		dma_addr_t dma;

		if (!ob_dma_slot_release(&ring->slot[i], &skb, &dma))
			continue;
		dma_unmap_single(dev, dma, OB_DMA_RX_BUFSZ, DMA_FROM_DEVICE);
		kfree_skb(skb);
		hw->rx.unmapped++;
		hw->rx.freed++;
	}
}

static void ob_rx_build_descriptors(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->dma.rx;
	u32 i;

	for (i = 0; i < OB_DMA_RX_POST_INIT; i++)
		ob_rx_desc_build(ob_dma_desc_at(ring, (u16)i),
				 ring->slot[i].dma, false);

	/* descriptor 255 is the structural EOT slot, with no live buffer. */
	ob_rx_desc_build(ob_dma_desc_at(ring, OB_DMA_RING_DESC_COUNT_RX - 1),
			 0, true);
}

static void ob_rx_dump_desc(struct ob_hw *hw, u16 idx)
{
	struct ob_dma_desc *d = ob_dma_desc_at(&hw->dma.rx, idx);

	dev_info(hw->dev,
		 "rx: desc%u ctrl1=%08x ctrl2=%08x addrlow=%08x addrhigh=%08x\n",
		 idx, ob_dma_desc_ctrl1(d), ob_dma_desc_len(d),
		 ob_dma_desc_addrlow(d), ob_dma_desc_addrhigh(d));
}

/* Disable RX and bounded-poll until the engine reports DISABLED. */
static int ob_rx_disable_hw(struct ob_hw *hw)
{
	u32 control, status0 = 0, status1;
	int i;

	bcma_write32(hw->core, OB_D11_RX_CONTROL, 0);
	control = bcma_read32(hw->core, OB_D11_RX_CONTROL);

	for (i = 0; i < OB_RX_DISABLE_POLL; i++) {
		status0 = bcma_read32(hw->core, OB_D11_RX_STATUS0);
		if ((status0 & OB_D11_RS0_RS_MASK) ==
		    OB_D11_RS0_RS_DISABLED)
			return 0;
		udelay(10);
	}
	status1 = bcma_read32(hw->core, OB_D11_RX_STATUS1);
	dev_err(hw->dev,
		"rx: disable timeout control=%08x status0=%08x status1=%08x\n",
		control, status0, status1);
	return -ETIMEDOUT;
}

static int ob_rx_program(struct ob_hw *hw)
{
	struct ob_dma_ring *ring = &hw->dma.rx;
	u32 ring_lo = (u32)ring->desc_dma;
	u32 ptr = ring_lo + OB_DMA_RX_POST_INIT * OB_DMA_DESC_SIZE;
	u32 control, rb_ptr, rb_lo, rb_hi, status0, status1;

	dev_info(hw->dev,
		 "rx: ring dma=0x%llx addrlow=%08x addrhigh=%08x\n",
		 (unsigned long long)ring->desc_dma, ring_lo, OB_DMA_PCIE_H32);

	ob_rx_dump_desc(hw, 0);
	ob_rx_dump_desc(hw, OB_DMA_RING_DESC_COUNT_RX - 1);

	/* Make every descriptor store (and the mappings) visible first. */
	dma_wmb();

	dev_info(hw->dev, "rx: program addrlow=%08x addrhigh=%08x ptr=%08x control=%08x\n",
		 ring_lo, OB_DMA_PCIE_H32, ptr, OB_D11_RX_CONTROL_INIT);

	bcma_write32(hw->core, OB_D11_RX_ADDRLOW, ring_lo);
	bcma_write32(hw->core, OB_D11_RX_ADDRHIGH, OB_DMA_PCIE_H32);
	bcma_write32(hw->core, OB_D11_RX_PTR, ptr);
	bcma_write32(hw->core, OB_D11_RX_CONTROL, OB_D11_RX_CONTROL_INIT);

	control = bcma_read32(hw->core, OB_D11_RX_CONTROL);
	rb_ptr = bcma_read32(hw->core, OB_D11_RX_PTR);
	rb_lo = bcma_read32(hw->core, OB_D11_RX_ADDRLOW);
	rb_hi = bcma_read32(hw->core, OB_D11_RX_ADDRHIGH);
	status0 = bcma_read32(hw->core, OB_D11_RX_STATUS0);
	status1 = bcma_read32(hw->core, OB_D11_RX_STATUS1);
	hw->rx.last_status0 = status0;
	hw->rx.last_status1 = status1;

	dev_info(hw->dev,
		 "rx: readback control=%08x ptr=%08x addrlow=%08x addrhigh=%08x status0=%08x status1=%08x\n",
		 control, rb_ptr, rb_lo, rb_hi, status0, status1);

	if ((control & OB_D11_RC_RE) == 0 ||
	    (rb_ptr & OB_D11_RS0_CD_MASK) != (ptr & OB_D11_RS0_CD_MASK) ||
	    rb_lo != ring_lo || rb_hi != OB_DMA_PCIE_H32 ||
	    status0 == 0xffffffffu) {
		dev_err(hw->dev, "rx: programming readback inconsistent\n");
		return -EIO;
	}
	if (status1 & OB_D11_RS1_RE_MASK) {
		dev_err(hw->dev, "rx: STATUS1 error at enable: %08x\n", status1);
		return -EIO;
	}
	return 0;
}

static int ob_rx_enable_irq(struct ob_hw *hw)
{
	u32 im, mm;
	int ret;

	/* Handler is already installed; route the D11 IRQ to the PCI line. */
	ret = bcma_host_pci_irq_ctl(hw->bus, hw->core, true);
	if (ret) {
		dev_err(hw->dev, "rx: bcma_host_pci_irq_ctl(true) failed: %d\n",
			ret);
		return ret;
	}
	hw->rx.route = true;
	dev_info(hw->dev, "rx: host irq route enabled\n");

	im = bcma_read32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4);
	dev_info(hw->dev, "rx: fifo mask before=%08x\n", im);
	bcma_write32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4,
		     im | OB_D11_FIFO_I_RI);

	mm = bcma_read32(hw->core, OB_D11_REG_MACINTMASK);
	dev_info(hw->dev, "rx: mac mask before=%08x\n", mm);
	bcma_write32(hw->core, OB_D11_REG_MACINTMASK, mm | OB_D11_MI_DMAINT);

	dev_info(hw->dev, "rx: fifo mask=%08x mac mask=%08x\n",
		 bcma_read32(hw->core,
			     OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4),
		 bcma_read32(hw->core, OB_D11_REG_MACINTMASK));
	return 0;
}

void ob_rx_irq(struct ob_hw *hw)
{
	u32 ist;

	if (!hw->rx.running)
		return;

	ist = bcma_read32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX));
	if (!(ist & OB_D11_FIFO_I_RI))
		return;

	/* Mask the RX source while deferred work drains the ring. */
	bcma_write32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4,
		     bcma_read32(hw->core,
				 OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4) &
		     ~OB_D11_FIFO_I_RI);
	/* Ack only I_RI (write-1-to-clear). */
	bcma_write32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX),
		     OB_D11_FIFO_I_RI);
	(void)bcma_read32(hw->core,
			  OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX));
	hw->rx.irq_masked = true;

	tasklet_schedule(&hw->rx.tasklet);
}

static void ob_rx_tasklet(struct tasklet_struct *t)
{
	struct ob_rx *rx = from_tasklet(rx, t, tasklet);
	struct ob_hw *hw = container_of(rx, struct ob_hw, rx);
	struct ob_dma_ring *ring = &hw->dma.rx;
	struct device *dev = hw->core->dma_dev;
	u32 status0, status1, curr, done = 0;

	if (!rx->running)
		return;

	status0 = bcma_read32(hw->core, OB_D11_RX_STATUS0);
	status1 = bcma_read32(hw->core, OB_D11_RX_STATUS1);
	rx->last_status0 = status0;
	rx->last_status1 = status1;
	curr = ob_rx_status_index(status0, ring->desc_dma);

	if (!ob_rx_index_ok(curr, OB_DMA_RING_DESC_COUNT_RX)) {
		dev_err(hw->dev,
			"rx: impossible STATUS0 index %u (status0=%08x)\n",
			curr, status0);
		goto abort;
	}
	if (curr > ring->head) {
		dev_err(hw->dev, "rx: engine past PTR curr=%u posted=%u\n",
			curr, ring->head);
		goto abort;
	}
	if (curr < ring->tail) {
		dev_err(hw->dev,
			"rx: wrapped completion curr=%u tail=%u\n",
			curr, ring->tail);
		goto abort;
	}

	while (ring->tail < curr && done < OB_RX_DRAIN_MAX) {
		u32 i = ring->tail;
		struct sk_buff *skb;
		dma_addr_t dma;
		const u8 *hdr;
		u32 flen;

		if (!ob_dma_slot_release(&ring->slot[i], &skb, &dma)) {
			dev_err(hw->dev, "rx: slot %u ownership mismatch\n", i);
			rx->errors++;
			goto abort;
		}
		dma_unmap_single(dev, dma, OB_DMA_RX_BUFSZ,
				 DMA_FROM_DEVICE);
		rx->unmapped++;

		hdr = skb->data;
		flen = hdr[0] | (hdr[1] << 8);
		if (!ob_rx_frame_len_ok(flen)) {
			dev_warn(hw->dev,
				 "rx: desc %u impossible length %u (status0=%08x status1=%08x)\n",
				 i, flen, status0, status1);
			rx->errors++;
		} else if (rx->logged < OB_RX_LOG_MAX) {
			ob_rx_log_frame(hw, i, status0, status1, hdr, flen);
			rx->logged++;
		}

		/* No refill: clear the consumed descriptor. */
		ob_dma_desc_zero(ob_dma_desc_at(ring, (u16)i));
		kfree_skb(skb);
		rx->freed++;
		ring->tail++;
		rx->processed++;
		done++;
	}

	if (ring->tail >= ring->head) {
		/* All posted descriptors consumed: stop RX (no repost). */
		dev_info(hw->dev,
			 "rx: drained all posted descriptors (processed=%u)\n",
			 rx->processed);
		goto stop;
	}

	/* More completions may arrive: re-arm the RX source. */
	bcma_write32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4,
		     bcma_read32(hw->core,
				 OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4) |
		     OB_D11_FIFO_I_RI);
	rx->irq_masked = false;
	return;

abort:
	/* Failure containment: mask, unroute, stop; no auto-recovery. */
	bcma_write32(hw->core, OB_D11_REG_MACINTMASK,
		     bcma_read32(hw->core, OB_D11_REG_MACINTMASK) &
		     ~OB_D11_MI_DMAINT);
	if (rx->route) {
		bcma_host_pci_irq_ctl(hw->bus, hw->core, false);
		rx->route = false;
		dev_info(hw->dev, "rx: host route disabled\n");
	}

stop:
	bcma_write32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4,
		     bcma_read32(hw->core,
				 OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4) &
		     ~OB_D11_FIFO_I_RI);
	rx->irq_masked = true;
	ob_rx_disable_hw(hw);
	rx->running = false;
}

int ob_rx_init(struct ob_hw *hw)
{
	int ret;

	memset(&hw->rx, 0, sizeof(hw->rx));
	tasklet_setup(&hw->rx.tasklet, ob_rx_tasklet);
	hw->rx.initialized = true;

	ret = ob_rx_alloc_buffers(hw);
	if (ret) {
		dev_err(hw->dev, "rx: buffer mapping failed: %d\n", ret);
		ob_rx_free_buffers(hw);
		return ret;
	}
	dev_info(hw->dev, "rx: mapped=%u map_fail=0 high32_fail=0\n",
		 OB_DMA_RX_POST_INIT);

	ob_rx_build_descriptors(hw);

	ret = ob_rx_program(hw);
	if (ret) {
		dev_err(hw->dev, "rx: ring programming failed: %d\n", ret);
		ob_rx_disable_hw(hw);
		ob_rx_free_buffers(hw);
		return ret;
	}

	hw->rx.running = true;
	ret = ob_rx_enable_irq(hw);
	if (ret) {
		dev_err(hw->dev, "rx: interrupt enable failed: %d\n", ret);
		ob_rx_quiesce(hw);
		ob_rx_free_buffers(hw);
		return ret;
	}
	return 0;
}

void ob_rx_quiesce(struct ob_hw *hw)
{
	u32 v;

	if (!hw->rx.initialized)
		return;

	/* 1. mask FIFO0 I_RI */
	v = bcma_read32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4);
	bcma_write32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4,
		     v & ~OB_D11_FIFO_I_RI);
	dev_info(hw->dev, "rx: interrupts masked\n");

	/* 2. mask MAC MI_DMAINT */
	v = bcma_read32(hw->core, OB_D11_REG_MACINTMASK);
	bcma_write32(hw->core, OB_D11_REG_MACINTMASK,
		     v & ~OB_D11_MI_DMAINT);

	/* 3. disable host routing */
	if (hw->rx.route) {
		bcma_host_pci_irq_ctl(hw->bus, hw->core, false);
		hw->rx.route = false;
		dev_info(hw->dev, "rx: host route disabled\n");
	}

	/* 4-5. disable RX and bounded-poll for DISABLED */
	ob_rx_disable_hw(hw);
	dev_info(hw->dev, "rx: dma disabled\n");

	/* 6. no in-flight hard IRQ can schedule new deferred work */
	if (hw->irq.irq >= 0)
		synchronize_irq(hw->irq.irq);
	dev_info(hw->dev, "rx: irq synchronized\n");

	/* 7. stop deferred RX processing */
	tasklet_kill(&hw->rx.tasklet);
	dev_info(hw->dev, "rx: deferred processing stopped\n");

	/*
	 * The deferred path may have re-armed I_RI just before it stopped;
	 * mask both owned sources again so nothing is left enabled.
	 */
	bcma_write32(hw->core, OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4,
		     bcma_read32(hw->core,
				 OB_D11_REG_INTCONTROL(OB_D11_FIFO_RX) + 4) &
		     ~OB_D11_FIFO_I_RI);
	bcma_write32(hw->core, OB_D11_REG_MACINTMASK,
		     bcma_read32(hw->core, OB_D11_REG_MACINTMASK) &
		     ~OB_D11_MI_DMAINT);

	hw->rx.running = false;
	hw->rx.irq_masked = true;
}

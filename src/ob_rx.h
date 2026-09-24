/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — FIFO0 RX path (M3.4B).
 *
 * Pure, host-testable helpers for the recovered BCM4352 / D11 rev42 RX
 * descriptor and STATUS0 semantics, plus the kernel-side bring-up API
 * implemented in ob_rx.c.
 *
 * Provenance: docs/rx_path.md (M3.4A).
 */
#ifndef _OB_RX_H_
#define _OB_RX_H_

#include <linux/types.h>
#include "ob_dma.h"

#ifdef __KERNEL__
#include <linux/interrupt.h>
#endif

/* ---- FIFO0 RX DMA registers (D11 + 0x220 block) ---- */
#define OB_D11_RX_CONTROL	0x0220
#define OB_D11_RX_PTR		0x0224
#define OB_D11_RX_ADDRLOW	0x0228
#define OB_D11_RX_ADDRHIGH	0x022c
#define OB_D11_RX_STATUS0	0x0230
#define OB_D11_RX_STATUS1	0x0234

/* RX control bits */
#define OB_D11_RC_RE		0x00000001u	/* receive enable */
#define OB_D11_RC_RO_SHIFT	1
#define OB_D11_RC_OC		0x00000400u
#define OB_D11_RC_PD		0x00000800u
/* RE | PD | (rxoffset=38 << 1): proven blob value. */
#define OB_D11_RX_CONTROL_INIT	0x0000084du

/* STATUS0/STATUS1 fields */
#define OB_D11_RS0_CD_MASK	0x00001fffu	/* current descriptor address */
#define OB_D11_RS0_CD_SHIFT	4		/* /16 bytes per descriptor */
#define OB_D11_RS0_RS_MASK	0xf0000000u	/* receive state */
#define OB_D11_RS0_RS_DISABLED	0x00000000u
#define OB_D11_RS1_RE_MASK	0xf0000000u	/* receive error code */

/* RX geometry */
#define OB_RX_HDR_LEN		38
#define OB_RX_MIN_FRAME		10
#define OB_RX_MAX_FRAME		(OB_DMA_RX_BUFSZ - OB_RX_HDR_LEN)
#define OB_RX_DRAIN_MAX		16	/* completions per tasklet run */
#define OB_RX_LOG_MAX		5
#define OB_RX_DISABLE_POLL	1000	/* bounded, 10 us steps */

/* ---- pure helpers (host-testable) ---- */

/*
 * Convert STATUS0 into a descriptor index. The ring base is 8 KiB aligned and
 * the recovered blob keeps only the low 13 address bits, so the 32-bit
 * difference masked to 0x1fff divided by 16 is the index (0..511). Callers must
 * still validate the result is < the ring descriptor count.
 */
static inline u32 ob_rx_status_index(u32 status0, dma_addr_t ring_dma)
{
	u32 cd = status0 & OB_D11_RS0_CD_MASK;
	u32 base = (u32)ring_dma & OB_D11_RS0_CD_MASK;

	return ((cd - base) & OB_D11_RS0_CD_MASK) >> OB_D11_RS0_CD_SHIFT;
}

static inline bool ob_rx_index_ok(u32 idx, u16 n)
{
	return idx < n;
}

static inline bool ob_rx_frame_len_ok(u32 len)
{
	return len >= OB_RX_MIN_FRAME && len <= OB_RX_MAX_FRAME;
}

static inline void ob_rx_desc_build(struct ob_dma_desc *d, dma_addr_t buf_dma,
				    bool eot)
{
	/*
	 * A structural EOT-only descriptor carries no live buffer: address 0
	 * with a zero byte count, so even if it were ever read it would not DMA.
	 * Ordinary descriptors use the proven rxbufsize.
	 */
	ob_dma_desc_encode(d, buf_dma, OB_DMA_PCIE_H32,
			   eot ? OB_DMA_CTRL1_EOT : 0u,
			   eot ? 0u : OB_DMA_RX_BUFSZ);
}

/* 802.11 frame_control decode (first 2 bytes of the frame). */
static inline u16 ob_rx_fc(const u8 *frame)
{
	return (u16)(frame[0] | (frame[1] << 8));
}

static inline u8 ob_rx_fc_type(u16 fc)
{
	return (u8)((fc >> 2) & 0x3);
}

static inline u8 ob_rx_fc_subtype(u16 fc)
{
	return (u8)((fc >> 4) & 0xf);
}

#ifdef __KERNEL__
struct ob_hw;

/**
 * struct ob_rx - FIFO0 RX bring-up and diagnostic state
 * @tasklet:		deferred (bounded) RX processing
 * @posted:		descriptors posted (64)
 * @tail:		next descriptor to consume
 * @processed:		completions consumed
 * @logged:		frames logged (<= OB_RX_LOG_MAX)
 * @errors:		bounded error events
 * @unmapped:		buffers unmapped
 * @freed:		buffers freed
 * @last_status0:	last STATUS0 seen
 * @last_status1:	last STATUS1 seen
 * @initialized:	tasklet set up
 * @running:		RX engine enabled
 * @route:		host D11 IRQ routing enabled
 * @irq_masked:		FIFO0 I_RI masked in intmask
 */
struct ob_rx {
	struct tasklet_struct	tasklet;
	u32			posted;
	u32			tail;
	u32			processed;
	u32			logged;
	u32			errors;
	u32			unmapped;
	u32			freed;
	u32			last_status0;
	u32			last_status1;
	bool			initialized;
	bool			running;
	bool			route;
	bool			irq_masked;
};

int  ob_rx_init(struct ob_hw *hw);
void ob_rx_quiesce(struct ob_hw *hw);
void ob_rx_free_buffers(struct ob_hw *hw);
void ob_rx_irq(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_RX_H_ */

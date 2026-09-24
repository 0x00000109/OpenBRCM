/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 interrupt plumbing (M3.3).
 *
 * Pure, host-testable model of the D11 top-level interrupt controller plus the
 * kernel-side registration API implemented in ob_irq.c.
 *
 * M3.3 only establishes a safe IRQ path: it identifies the PCI/BCMA IRQ,
 * registers a shared handler and tears it down cleanly. It does NOT enable any
 * interrupt source, enable DMA, publish a ring address or process frames.
 *
 * Register/bit provenance: the register offsets are blob-confirmed (C2) and
 * match upstream `struct d11regs` (C3). The macintstatus/macintmask bit names
 * are upstream C3 (brcmsmac d11.h). NOTE: I_RI/I_XI (1<<16 / 1<<24) are NOT
 * macintstatus bits — they live in the per-FIFO intctrlregs at D11+0x20; on
 * macintstatus bit 16 is MI_TXSTOP.
 */
#ifndef _OB_IRQ_H_
#define _OB_IRQ_H_

#include <linux/types.h>

/* ---- D11 interrupt registers (relative to the D11 core) ---- */
#define OB_D11_REG_MACINTSTATUS	0x0128	/* C2 blob / C3 d11.h */
#define OB_D11_REG_MACINTMASK	0x012c	/* C2 blob / C3 d11.h */

/*
 * Per-FIFO interrupt control: 8 pairs of {intstatus, intmask} starting at
 * D11+0x20 (C3 struct d11regs). intstatus is write-1-to-clear.
 */
#define OB_D11_REG_INTCONTROL(i)	(0x0020 + ((i) * 8))
#define OB_D11_FIFO_RX		0	/* RX_FIFO: data and control frames */
#define OB_D11_FIFO_TX_CTL	3	/* TX_AC_VO_FIFO == TX_CTL_FIFO */

/* ---- macintstatus / macintmask bits (C3: brcmsmac d11.h) ---- */
#define OB_D11_MI_MACSSPNDD	(1u << 0)	/* gracefully suspended */
#define OB_D11_MI_BCNTPL	(1u << 1)	/* beacon template available */
#define OB_D11_MI_TBTT		(1u << 2)	/* TBTT indication */
#define OB_D11_MI_BCNSUCCESS	(1u << 3)	/* beacon tx'd */
#define OB_D11_MI_BCNCANCLD	(1u << 4)	/* beacon canceled (IBSS) */
#define OB_D11_MI_ATIMWINEND	(1u << 5)	/* end of ATIM window */
#define OB_D11_MI_PMQ		(1u << 6)	/* PMQ entries available */
#define OB_D11_MI_NSPECGEN_0	(1u << 7)	/* PSM gen-stat */
#define OB_D11_MI_NSPECGEN_1	(1u << 8)	/* PSM gen-stat */
#define OB_D11_MI_MACTXERR	(1u << 9)	/* MAC level Tx error */
#define OB_D11_MI_NSPECGEN_3	(1u << 10)	/* PSM gen-stat */
#define OB_D11_MI_PHYTXERR	(1u << 11)	/* PHY Tx error */
#define OB_D11_MI_PME		(1u << 12)	/* power management event */
#define OB_D11_MI_GP0		(1u << 13)	/* general-purpose timer0 */
#define OB_D11_MI_GP1		(1u << 14)	/* general-purpose timer1 */
#define OB_D11_MI_DMAINT	(1u << 15)	/* OR of the DMA interrupts */
#define OB_D11_MI_TXSTOP	(1u << 16)	/* TX FIFO suspend/flush done */
#define OB_D11_MI_CCA		(1u << 17)	/* CCA measurement done */
#define OB_D11_MI_BG_NOISE	(1u << 18)	/* background noise samples */
#define OB_D11_MI_DTIM_TBTT	(1u << 19)	/* MBSS DTIM TBTT */
#define OB_D11_MI_PRQ		(1u << 20)	/* probe response queue */
#define OB_D11_MI_PWRUP		(1u << 21)	/* radio/PHY powered back up */
#define OB_D11_MI_RFDISABLE	(1u << 28)	/* RF disable input changed */
#define OB_D11_MI_TFS		(1u << 29)	/* MAC completed a TX */
#define OB_D11_MI_PHYCHANGED	(1u << 30)	/* G-mode phy change */
#define OB_D11_MI_TO		(1u << 31)	/* general purpose timeout */

/* Every named (known) macintstatus bit. Unknown/reserved bits stay out. */
#define OB_D11_IRQ_KNOWN_MASK	(OB_D11_MI_MACSSPNDD | OB_D11_MI_BCNTPL | \
				 OB_D11_MI_TBTT | OB_D11_MI_BCNSUCCESS | \
				 OB_D11_MI_BCNCANCLD | OB_D11_MI_ATIMWINEND | \
				 OB_D11_MI_PMQ | OB_D11_MI_NSPECGEN_0 | \
				 OB_D11_MI_NSPECGEN_1 | OB_D11_MI_MACTXERR | \
				 OB_D11_MI_NSPECGEN_3 | OB_D11_MI_PHYTXERR | \
				 OB_D11_MI_PME | OB_D11_MI_GP0 | \
				 OB_D11_MI_GP1 | OB_D11_MI_DMAINT | \
				 OB_D11_MI_TXSTOP | OB_D11_MI_CCA | \
				 OB_D11_MI_BG_NOISE | OB_D11_MI_DTIM_TBTT | \
				 OB_D11_MI_PRQ | OB_D11_MI_PWRUP | \
				 OB_D11_MI_RFDISABLE | OB_D11_MI_TFS | \
				 OB_D11_MI_PHYCHANGED | OB_D11_MI_TO)

/*
 * Interrupt sources OpenBRCM owns. M3.3 owns only the DMA interrupt summary
 * (MI_DMAINT); the per-FIFO RX/TX sources it summarizes (I_RI/I_XI) are the
 * ones enabled in M3.4. This mask is deliberately a subset of KNOWN, so the
 * handler never acknowledges an unproven bit.
 */
#define OB_D11_IRQ_OWNED_MASK	(OB_D11_MI_DMAINT)

/* 0xffffffff means the core is in reset / the device was removed (brcmsmac). */
#define OB_D11_IRQ_STATUS_INVALID	0xffffffffu

/**
 * struct ob_irq - IRQ registration and diagnostic counters
 * @irq:		requested Linux IRQ number (<0 when not installed)
 * @installed:		true between request_irq() success and free_irq()
 * @active:		handler work gate; cleared before synchronize_irq()
 * @shared:		IRQ is requested with IRQF_SHARED
 * @owned_mask:		macintstatus bits OpenBRCM owns
 * @initial_status:	macintstatus snapshot before masking (read-only)
 * @initial_mask:	macintmask snapshot before masking (read-only)
 * @last_status:	last raw macintstatus seen by the handler
 * @total:		handler invocations
 * @handled:		IRQ_HANDLED returns
 * @none:		IRQ_NONE returns
 * @unexpected:		invocations with known-but-unowned status bits
 */
struct ob_irq {
	int	irq;
	bool	installed;
	bool	active;
	bool	shared;
	u32	owned_mask;
	u32	initial_status;
	u32	initial_mask;
	u32	last_status;
	u32	total;
	u32	handled;
	u32	none;
	u32	unexpected;
};

/* ---- pure helpers (host-testable, no kernel API) ---- */

/* 0xffffffff is not a real status; treat as "not for us". */
static inline bool ob_d11_irq_status_valid(u32 raw)
{
	return raw != OB_D11_IRQ_STATUS_INVALID;
}

/* Owned bits currently pending. */
static inline u32 ob_d11_irq_pending(u32 raw, u32 owned)
{
	return raw & owned;
}

/* Should the handler claim (and acknowledge) this interrupt? */
static inline bool ob_d11_irq_has_work(u32 raw, u32 owned)
{
	return ob_d11_irq_status_valid(raw) &&
	       ob_d11_irq_pending(raw, owned) != 0;
}

/* Bits to write back to clear (never includes an unowned/unknown bit). */
static inline u32 ob_d11_irq_ack_bits(u32 raw, u32 owned)
{
	return raw & owned;
}

/* Known-but-unowned (unexpected) bits, for rate-limited diagnostics. */
static inline u32 ob_d11_irq_unexpected(u32 raw, u32 owned, u32 known)
{
	return raw & known & ~owned;
}

/* Disable the owned sources while preserving every unrelated mask bit. */
static inline u32 ob_d11_irq_mask_clear(u32 mask, u32 owned)
{
	return mask & ~owned;
}

/* ---- kernel API (ob_irq.c) ---- */
struct ob_hw;

int  ob_irq_init(struct ob_hw *hw);
void ob_irq_free(struct ob_hw *hw);

#endif /* _OB_IRQ_H_ */

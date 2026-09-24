/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — isolated vendor pre-PHY DMA bring-up test (M3.4D3A0).
 *
 * `dma_test_only=1` reproduces the vendor post-common DMA/IRQ-source stage:
 * the proven D2B prefix, the exactly-pinned D11/IRQ-source writes in vendor
 * order, then four TX DMA channels + FIFO0 RX (64 buffers), deterministic
 * validation, a mandatory verified quiesce and a safe free. It never reaches
 * bsinitvals/PHY/radio/channel/mac80211, never enables EN_MAC, never routes the
 * host IRQ and never enables MACINTMASK/MI_DMAINT.
 *
 * The pure section below is host-testable and contains no kernel API.
 *
 * Provenance: docs/m34d3_bsinitvals.md Appendices C/D and
 * docs/d3a0_dma_test_design.md.
 */
#ifndef _OB_D3A0_H_
#define _OB_D3A0_H_

#include <linux/types.h>
#include "ob_dma.h"
#include "ob_initvals.h"

/* ---- DMA64 channel register block, relative to a channel base ---- */
#define OB_D3A0_D64_CONTROL	0x00u
#define OB_D3A0_D64_PTR		0x04u
#define OB_D3A0_D64_ADDRLOW	0x08u
#define OB_D3A0_D64_ADDRHIGH	0x0cu
#define OB_D3A0_D64_STATUS0	0x10u
#define OB_D3A0_D64_STATUS1	0x14u

/* ---- four TX channels (vendor-verified BCM4352 bases) ---- */
#define OB_D3A0_TX_CHANNELS	4u
#define OB_D3A0_TX_BASE_BK	0x0200u	/* TX_AC_BK_FIFO */
#define OB_D3A0_TX_BASE_BE	0x0240u	/* TX_AC_BE_FIFO */
#define OB_D3A0_TX_BASE_VI	0x0280u	/* TX_AC_VI_FIFO */
#define OB_D3A0_TX_BASE_VO	0x02c0u	/* TX_AC_VO / TX_CTL_FIFO */

/* ---- FIFO0 RX block ---- */
#define OB_D3A0_RX_BASE		0x0220u

/* ---- TX geometry (proven) ---- */
#define OB_D3A0_TX_NTXD		512u
#define OB_D3A0_TX_RING_BYTES	(OB_D3A0_TX_NTXD * OB_DMA_DESC_SIZE) /* 8192 */
#define OB_D3A0_RING_ALIGN	8192u

/*
 * Ownership accounting: the TX rings publish ONLY base + CONTROL (no pointer,
 * no posted descriptors), so dma_test_only performs ZERO TX payload mappings.
 * All streaming mappings in D3A0 belong to FIFO0 RX (OB_DMA_RX_POST_INIT = 64
 * DMA_FROM_DEVICE buffers).
 */
#define OB_D3A0_TX_PAYLOAD_MAPPINGS	0u
#define OB_D3A0_RX_MAPPINGS		OB_DMA_RX_POST_INIT

/* ---- TX control bits (proven) ---- */
#define OB_D3A0_XC_XE		0x00000001u	/* transmit enable */
#define OB_D3A0_XC_SE		0x00000002u	/* suspend */
#define OB_D3A0_XC_PD		0x00000800u	/* parity disable */

/* ---- RX control (proven: RE | PD | (rxoffset=38 << 1)) ---- */
#define OB_D3A0_RC_RE		0x00000001u
#define OB_D3A0_RC_PD		0x00000800u
#define OB_D3A0_RX_CONTROL	0x0000084du
#define OB_D3A0_RX_PTR		0x00000400u	/* rxpost=64 * 16 */

/* ---- STATUS0 state field (RX and TX share the 0xf0000000 state field) ---- */
#define OB_D3A0_RS_MASK		0xf0000000u
#define OB_D3A0_RS_DISABLED	0x00000000u
#define OB_D3A0_RS_ACTIVE	0x10000000u
#define OB_D3A0_RS_IDLE		0x20000000u
#define OB_D3A0_RS_STOPPED	0x30000000u

/* ---- TX reset bounded poll (vendor dma_txreset 0xf64a) ---- */
#define OB_D3A0_RESET_TIMEOUT	0x00002719u	/* 10009 */
#define OB_D3A0_RESET_STEP	10u
#define OB_D3A0_RESET_DELAY_US	10u
#define OB_D3A0_RESET_FINAL_US	300u

/* ---- D11 host registers used by the pinned pre-DMA prefix ---- */
#define OB_D3A0_REG_INTRCVLAZY0	0x0100u
#define OB_D3A0_REG_MACCONTROL	0x0120u
#define OB_D3A0_REG_MACINTSTATUS 0x0128u
#define OB_D3A0_REG_MACINTMASK	0x012cu
#define OB_D3A0_REG_INTCONTROL0	0x0020u
#define OB_D3A0_REG_INTCONTROL0_MASK 0x0024u
#define OB_D3A0_REG_TSF_CFPREP	0x0188u
#define OB_D3A0_REG_TSF_CFPSTART 0x018cu
#define OB_D3A0_REG_MACHWCAP	0x015cu

/* ---- exactly-pinned values (Appendices B/D) ---- */
#define OB_D3A0_INTRCVLAZY	0x01000000u	/* 1 << 24 */
#define OB_D3A0_MI_GP1		0x00004000u	/* macintstatus W1C */
#define OB_D3A0_I_RI		0x00010000u	/* per-FIFO RX source */
#define OB_D3A0_MI_DMAINT	0x00008000u	/* MAC aggregate (never enabled) */
#define OB_D3A0_TSF_CFPREP	0x80000000u
#define OB_D3A0_TSF_CFPSTART	0x02000000u
#define OB_D3A0_MACCONTROL_MASK	0x40060000u
#define OB_D3A0_MACCONTROL_VAL	0x40020000u
#define OB_D3A0_MACCONTROL_EXPECTED 0x44020402u
#define OB_D3A0_MCTL_PSM_RUN	0x00000002u
#define OB_D3A0_MCTL_EN_MAC	0x00000001u
#define OB_D3A0_MCTL_SHM_EN	0x00000100u
#define OB_D3A0_MCTL_DISCARD_PMQ 0x40000000u
#define OB_D3A0_MCTL_INFRA	0x00020000u

/* SICF_MPCLKE (D11 core cflags bit4 == BCMA_IOCTL bit4) */
#define OB_D3A0_IOCTL_MPCLKE	0x00000010u

/* ---- pinned SMALL SHM writes in the prefix (exact values) ---- */
#define OB_D3A0_SHM_MBURST	0x0080u		/* M_MBURST_SIZE = 8 */
#define OB_D3A0_SHM_MAXANTCNT	0x005cu		/* M_MAX_ANTCNT  = 0x0a */
#define OB_D3A0_SHM_MACHWVER	0x0016u		/* M_MACHW_VER  = phyrev */
#define OB_D3A0_SHM_MACHWCAP_L	0x00c0u		/* M_MACHW_CAP_L */
#define OB_D3A0_SHM_MACHWCAP_H	0x00c2u		/* M_MACHW_CAP_H */
#define OB_D3A0_SHM_MBURST_VAL	0x0008u
#define OB_D3A0_SHM_MAXANTCNT_VAL 0x000au
#define OB_D3A0_PHYREV_REV42	0x2au

/* ---- per-channel / per-resource lifecycle state (pure) ---- */
enum ob_d3a0_state {
	OB_D3A0_NONE = 0,	/* never touched */
	OB_D3A0_ALLOCATED,	/* software resources exist */
	OB_D3A0_PROGRAMMED,	/* hardware registers written */
};

/**
 * struct ob_d3a0_lifecycle - independent per-resource initialization state
 * @tx:		per-TX-channel state (OB_D3A0_NONE/ALLOCATED/PROGRAMMED)
 * @rx:		RX ring state
 * @rx_mapped:	RX buffers currently mapped (0..OB_DMA_RX_POST_INIT)
 * @irq_source: true once the per-FIFO I_RI source was written
 * @quiesced:	true once every programmed engine was verified stopped (or
 *		verified containment)
 * @fatal:	true if quiesce/containment could not be verified; DMA memory
 *		must then never be freed (reboot required)
 */
struct ob_d3a0_lifecycle {
	u8	tx[OB_D3A0_TX_CHANNELS];
	u8	rx;
	u32	rx_mapped;
	bool	irq_source;
	bool	quiesced;
	bool	fatal;
};

/* ---- pure helpers (host-testable) ---- */

static inline u16 ob_d3a0_tx_base(u32 ch)
{
	switch (ch) {
	case 0: return OB_D3A0_TX_BASE_BK;
	case 1: return OB_D3A0_TX_BASE_BE;
	case 2: return OB_D3A0_TX_BASE_VI;
	case 3: return OB_D3A0_TX_BASE_VO;
	default: return 0;
	}
}

/*
 * Exact TX control: the vendor RMW re-asserts the capability fields read from
 * the same register, so it is equivalent to preserving every bit and OR-ing XE
 * plus PD (parity not enabled on the D3A0 path).
 */
static inline u32 ob_d3a0_tx_control(u32 old)
{
	return old | OB_D3A0_XC_XE | OB_D3A0_XC_PD;
}

/* The only allowed change from @old is XE and PD being set. */
static inline bool ob_d3a0_tx_control_ok(u32 old, u32 now)
{
	return (now & OB_D3A0_XC_XE) &&
	       (now & ~(OB_D3A0_XC_XE | OB_D3A0_XC_PD)) ==
	       (old & ~(OB_D3A0_XC_XE | OB_D3A0_XC_PD));
}

static inline u32 ob_d3a0_status_state(u32 status0)
{
	return status0 & OB_D3A0_RS_MASK;
}

static inline bool ob_d3a0_rx_disabled(u32 status0)
{
	return ob_d3a0_status_state(status0) == OB_D3A0_RS_DISABLED;
}

static inline bool ob_d3a0_rx_idle(u32 status0)
{
	return ob_d3a0_status_state(status0) == OB_D3A0_RS_IDLE;
}

/* dma_txreset step 1 settles on DISABLED, STOPPED or IDLE. */
static inline bool ob_d3a0_tx_reset_settled(u32 status0)
{
	u32 s = ob_d3a0_status_state(status0);

	return s == OB_D3A0_RS_DISABLED || s == OB_D3A0_RS_STOPPED ||
	       s == OB_D3A0_RS_IDLE;
}

static inline u32 ob_d3a0_reset_max_iters(void)
{
	return OB_D3A0_RESET_TIMEOUT / OB_D3A0_RESET_STEP;
}

static inline bool ob_d3a0_poll_expired(u32 remaining)
{
	return remaining < OB_D3A0_RESET_STEP;
}

/* Is any hardware engine still active (not yet verified stopped)? */
static inline bool ob_d3a0_hw_active(const struct ob_d3a0_lifecycle *lc)
{
	u32 i;

	if (!lc)
		return false;
	if (lc->rx == OB_D3A0_PROGRAMMED && !lc->quiesced)
		return true;
	for (i = 0; i < OB_D3A0_TX_CHANNELS; i++)
		if (lc->tx[i] == OB_D3A0_PROGRAMMED && !lc->quiesced)
			return true;
	return false;
}

/*
 * Free is permitted only when no engine is active and quiesce (or verified
 * containment) succeeded. A fatal state forbids any free/unmap.
 */
static inline bool ob_d3a0_can_free(const struct ob_d3a0_lifecycle *lc)
{
	if (!lc)
		return false;
	if (lc->fatal)
		return false;
	return !ob_d3a0_hw_active(lc);
}

/* Postcondition: MACCONTROL keeps PSM_RUN, clears EN_MAC/SHM_EN. */
static inline bool ob_d3a0_maccontrol_ok(u32 mctrl)
{
	return (mctrl & OB_D3A0_MCTL_PSM_RUN) &&
	       !(mctrl & OB_D3A0_MCTL_EN_MAC) &&
	       !(mctrl & OB_D3A0_MCTL_SHM_EN) &&
	       (mctrl & OB_D3A0_MCTL_DISCARD_PMQ) &&
	       (mctrl & OB_D3A0_MCTL_INFRA);
}

/* The host IRQ route must remain disabled: MACINTMASK stays 0. */
static inline bool ob_d3a0_host_irq_disabled(u32 macintmask)
{
	return macintmask == 0;
}

/* The per-FIFO source must contain I_RI but must not equal MI_DMAINT. */
static inline bool ob_d3a0_irq_source_ok(u32 intmask0)
{
	return (intmask0 & OB_D3A0_I_RI) != 0;
}

/*
 * Hard ordering gate. The post-common D3A0 prefix (and therefore every DMA
 * action) may run ONLY after the shared D2B stage produced the exact
 * provenance-backed common-initvals postconditions. Any deviation means the
 * common-initvals stage did not complete correctly and DMA MUST NOT start.
 */
static inline bool ob_d3a0_d2b_state_ok(const struct ob_initvals_post *post)
{
	return ob_initvals_post_ok(post);
}

#ifdef __KERNEL__
#include <linux/dmapool.h>

struct ob_hw;

/**
 * struct ob_d3a0 - kernel DMA lifecycle state for dma_test_only
 * @pool:	8 KiB-aligned coherent descriptor pool (D3A0-owned)
 * @pool_created: true between creation and destruction
 * @rx:		FIFO0 RX descriptor ring
 * @tx:		four TX descriptor rings
 * @lc:		per-resource lifecycle state
 */
struct ob_d3a0 {
	struct dma_pool		*pool;
	bool			pool_created;
	struct ob_dma_ring	rx;
	struct ob_dma_ring	tx[OB_D3A0_TX_CHANNELS];
	struct ob_d3a0_lifecycle lc;
};

/*
 * Run the isolated D3A0 bring-up + teardown. Performs real MMIO (D2B prefix +
 * pinned D11/IRQ-source writes + DMA programming) and executes real DMA engine
 * enables and the mandatory quiesce. Requires explicit human approval; never
 * proceeds into band init/PHY/radio/channel/mac80211.
 */
int ob_d3a0_test(struct ob_hw *hw);

/*
 * remove() hook for dma_test_only. Honours the fail-closed lifecycle: never
 * frees DMA memory while hardware may still consume it.
 */
void ob_d3a0_remove(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_D3A0_H_ */

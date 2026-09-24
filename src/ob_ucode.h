/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — D11 rev42 ucode upload + PSM start, isolated test path (M3.4D2A).
 *
 * This header carries the exact recovered register/bit vocabulary and the pure
 * (host-testable) helpers for the ucode_test_only mode. It deliberately contains
 * no kernel API so the mode-selection, write-count, bounded-poll and teardown
 * logic can be unit-tested on the host.
 *
 * Provenance (all C2 = vendor blob `wlc_hybrid.o_shipped`, corroborated by C3
 * brcmsmac `struct d11regs`):
 *   - ucode upload applier  wlc blob 0x60744 (dispatch 0x607b0)
 *       OBJADDR = D11+0x160, OBJDATA = D11+0x164; writes OBJADDR=0x03000000
 *       (AUTO_INC | UCM_SEL 0), reads it back (barrier), then one 32-bit raw LE
 *       word per OBJDATA write. No byte swap.
 *   - pre-upload MACCONTROL  wlc_bmac_init @0x6836c:
 *       wlc_bmac_mctrl(dev, ~0, 0x04000404) = IHR_EN | PSM_JMP0 | WAKE.
 *       PSM_RUN=0, EN_MAC=0, SHM_EN=0.  Masked update (wlc_bmac_mctrl 0x6060d).
 *   - PSM start + poll       wlc_bmac_wowlucode_start @0x63828:
 *       osl_writel(0xffffffff, D11+0x128); wlc_bmac_mctrl(dev, ~0, 0x04020402)
 *       = IHR_EN | INFRA | PSM_RUN | WAKE; then read D11+0x128 until bit0
 *       (MI_MACSSPNDD) is set, osl_delay(10) per iteration, counter 0xF4249
 *       decremented by 10 (=> <=100000 iterations, ~1.0 s). No EN_MAC.
 *   - SHM read               wlc_bmac_read_shm @0x61910 -> helper 0x6188a:
 *       OBJADDR(D11+0x160) = 0x10000 | (offset>>2); read OBJDATA + (offset&2)
 *       as 16-bit. Vendor FIFO-size reads at 0x68F88..0x68FAE (SHM 0x98..0x9e).
 *       NOTE: those vendor call sites are AFTER the common-initvals applier at
 *       0x68b98, so D2A logs them as read-only diagnostics only.
 *
 * No initvals are applied by this path. No PHY/radio/channel/DMA/IRQ/mac80211.
 */
#ifndef _OB_UCODE_H_
#define _OB_UCODE_H_

#include <linux/types.h>
#include "ob_irq.h"	/* OB_D11_MI_MACSSPNDD, OB_D11_REG_MACINTSTATUS */

/* ---- D11 host-interface registers (relative to the D11 core) ---- */
#define OB_UCODE_REG_MACCONTROL		0x0120u
#define OB_UCODE_REG_MACINTSTATUS	0x0128u	/* == OB_D11_REG_MACINTSTATUS */
#define OB_UCODE_REG_OBJADDR		0x0160u
#define OB_UCODE_REG_OBJDATA		0x0164u
#define OB_UCODE_REG_CLKCTLST		0x01e0u

/* ---- maccontrol bits (C3 brcmsmac d11.h) ---- */
#define OB_UCODE_MCTL_EN_MAC	0x00000001u
#define OB_UCODE_MCTL_PSM_RUN	0x00000002u
#define OB_UCODE_MCTL_PSM_JMP0	0x00000004u
#define OB_UCODE_MCTL_SHM_EN	0x00000100u
#define OB_UCODE_MCTL_IHR_EN	0x00000400u
#define OB_UCODE_MCTL_INFRA	0x00020000u
#define OB_UCODE_MCTL_WAKE	0x04000000u

/* CLKCTLST.HAVEHT (C3 bcma) */
#define OB_UCODE_CLKCTLST_HAVEHT	0x00020000u

/*
 * Exact vendor MACCONTROL values (wlc_bmac_mctrl(dev, ~0, value)). Exposed so
 * the host test can prove the bit composition.
 */
#define OB_UCODE_MACCONTROL_UPLOAD \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_PSM_JMP0 | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_MACCONTROL_PSM \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_INFRA | \
	 OB_UCODE_MCTL_PSM_RUN | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_MACCONTROL_MASK	0xffffffffu

/* Bits that must be settled in each recovered state. */
#define OB_UCODE_UPLOAD_EXPECTED \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_PSM_JMP0 | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_UPLOAD_FORBIDDEN \
	(OB_UCODE_MCTL_PSM_RUN | OB_UCODE_MCTL_EN_MAC | OB_UCODE_MCTL_SHM_EN)
#define OB_UCODE_PSM_EXPECTED \
	(OB_UCODE_MCTL_IHR_EN | OB_UCODE_MCTL_INFRA | \
	 OB_UCODE_MCTL_PSM_RUN | OB_UCODE_MCTL_WAKE)
#define OB_UCODE_PSM_FORBIDDEN \
	(OB_UCODE_MCTL_EN_MAC)

/* ---- OBJADDR selector bits (C3 brcmsmac d11.h) ---- */
#define OB_UCODE_OBJADDR_AUTO_INC	0x03000000u
#define OB_UCODE_OBJADDR_UCM_SEL	0x00000000u
#define OB_UCODE_OBJADDR_SHM_SEL	0x00010000u

/* ---- bounded PSM poll (exact vendor constants) ---- */
#define OB_UCODE_POLL_DELAY_US	10u
#define OB_UCODE_POLL_TIMEOUT	0x000f4249u	/* 1000009 */
#define OB_UCODE_POLL_STEP	10u		/* counter decrement per iteration */

/* ---- pre-initvals SHM FIFO-size diagnostic (vendor 0x68f88) ---- */
#define OB_UCODE_SHM_FIFOSIZE0	0x0098u
#define OB_UCODE_SHM_FIFOSIZE1	0x009au
#define OB_UCODE_SHM_FIFOSIZE2	0x009cu
#define OB_UCODE_SHM_FIFOSIZE3	0x009eu

/* ---- pure helpers (host-testable, no kernel API) ---- */

/*
 * Explicit mode policy: the two hardware-avoiding/hardware-test modes are
 * mutually exclusive and must never be combined.
 */
static inline bool ob_ucode_mode_conflict(bool fw_validate_only,
					  bool ucode_test_only)
{
	return fw_validate_only && ucode_test_only;
}

/* A validated flat ucode image is exactly size/4 32-bit words. */
static inline u32 ob_ucode_words_from_size(size_t fw_size)
{
	return (u32)(fw_size / 4);
}

/* The upload is only allowed to start PSM if every word was written once. */
static inline bool ob_ucode_writes_ok(u32 written, u32 expected)
{
	return written == expected;
}

/* Upper bound on poll iterations for timeout/step (vendor loop semantics). */
static inline u32 ob_ucode_poll_max_iterations(u32 timeout, u32 step)
{
	return step ? timeout / step : 0;
}

/* Vendor loop exits when the counter reaches step-1 (was `cmp $0x9` / step 10). */
static inline bool ob_ucode_poll_expired(u32 remaining, u32 step)
{
	return remaining < step;
}

static inline bool ob_ucode_mac_suspended(u32 macintstatus)
{
	return (macintstatus & OB_D11_MI_MACSSPNDD) != 0;
}

/*
 * ucode_test_only initializes NO platform resources (no mac80211, no RX ring,
 * no IRQ, no DMA). The teardown plan therefore disables every teardown step so
 * that remove() can never touch something that was never set up.
 */
struct ob_ucode_teardown {
	bool mac80211;
	bool rx;
	bool irq;
	bool dma;
};

static inline struct ob_ucode_teardown
ob_ucode_teardown_plan(bool ucode_test_only)
{
	struct ob_ucode_teardown t = { false, false, false, false };

	if (!ucode_test_only) {
		t.mac80211 = true;
		t.rx = true;
		t.irq = true;
		t.dma = true;
	}
	return t;
}

#ifdef __KERNEL__
struct ob_hw;

int ob_ucode_test(struct ob_hw *hw);
#endif /* __KERNEL__ */

#endif /* _OB_UCODE_H_ */

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * OpenBRCM — core state.
 *
 * Clean-room implementation derived from the OpenBRCM RE specification
 * (see docs/provenance.md). No code copied from the proprietary blob or from
 * upstream drivers.
 */
#ifndef _OB_CORE_H_
#define _OB_CORE_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/bcma/bcma.h>
#include "ob_guard.h"
#include "ob_dma.h"
#include "ob_irq.h"
#include "ob_rx.h"
#include "ob_fw.h"
#include "ob_d3a0.h"
#include "ob_d3a1.h"
#include "ob_d3b.h"
#include "ob_diag.h"
#include "ob_device_loss_trace.h"
#include "ob_phy_tripoint.h"
#include "ob_lifecycle.h"
#include "ob_phy.h"
#include "ob_chip_profile.h"

#define OB_DRV_NAME	"openbrcm"

struct ieee80211_hw;
struct bcm43xx_chan_tuning_tables;

enum ob_mac_source {
	OB_MAC_SOURCE_UNKNOWN = 0,
	OB_MAC_SOURCE_BCMA_PARSED,
	OB_MAC_SOURCE_REV11_CORRECTED,
	OB_MAC_SOURCE_DIRECT_VALIDATED,
};

/**
 * struct ob_hw - per-device state
 * @core:	the D11 (80211) bcma core
 * @dev:	backing bcma device
 * @bus:	the silicon backplane bus
 * @chip_id:	chip id (e.g. 0x4352)
 * @chip_rev:	chip revision
 * @validate_only: true when probe ran with fw_validate_only=1: the device is
 *		bound and the rev42 images were validated, but no OpenBRCM
 *		hardware bring-up happened and @remove must skip all teardown
 * @ucode_test_only: true when probe ran with ucode_test_only=1: the device is
 *		bound, minimum core prep + ucode upload + PSM start ran, and
 *		@remove must skip all RX/IRQ/DMA/mac80211 teardown
 * @initvals_test_only: true when probe ran with initvals_test_only=1: the
 *		device is bound, the proven D2A core ran, the 610 common
 *		initvals were applied and postconditions verified; @remove must
 *		skip all RX/IRQ/DMA/mac80211 teardown
 * @dma_test_only: true when probe ran with dma_test_only=1: the device is
 *		bound, the D2B prefix plus the pinned D11/clock/IRQ-source
 *		prerequisites ran and the isolated DMA lifecycle (4 TX + FIFO0
 *		RX) was brought up, validated, quiesced and freed; @remove
 *		handles the fail-closed DMA lifecycle via ob_d3a0_remove()
 * @d3a1_test_only: true when probe ran with d11_tail_test_only=1: the device
 *		is bound, the proven D2A/D2B core ran, the exact vendor rev42
 *		post-common tail (sub_67efd + T1 + DMA in vendor position + T2 +
 *		switch_macfreq) ran and STOPPED before sub_6656c/bsinitvals/PHY;
 *		@remove handles the fail-closed DMA lifecycle via ob_d3a1_remove()
 * @d3a1_fifo530_continue: true when probe ran with d11_fifo530_continue=1:
 *		the isolated D3A1 tail treats a bounded 0x530 completion-poll expiry as
 *		the vendor does (record EXPIRED_AND_CONTINUED, program the next entry)
 *		instead of aborting. Only valid together with d11_tail_test_only=1;
 *		the default is the fail-closed stop.
 * @bsinitvals_test_only: true when probe ran with bsinitvals_test_only=1: the
 *		device is bound, the proven D2A/D2B core and the exact D3A1
 *		vendor prefix ran with the D3A0 DMA engines left LIVE, then the
 *		exact sub_6656c MHF + d11ac1bsinitvals42 (73 records) ran and
 *		STOPPED before wlc_phy_init; @remove handles the fail-closed DMA
 *		lifecycle via ob_d3b_remove()
 * @sprom_evidence_only: true when probe ran with sprom_evidence_only=1: the
 *		device is bound and only the READ-ONLY external-SPROM diagnostic
 *		runs (it emits the already-read 234-word rev11 image); no power-up,
 *		D11/core write, DMA, IRQ, PHY/radio, firmware or mac80211; @remove
 *		skips all teardown
 * @radio_id_probe_only: true when probe ran with radio_id_probe_only=1: the
 *		device is bound, the minimum proven core prep ran (host up + D11
 *		core enable + FAST clock + D11 MAC-PHY clock), the D11 radio
 *		indirect window read radio reg 0/1 (two selector writes + two
 *		16-bit reads) and the probe STOPPED before any PLL/radio/PHY/
 *		calibration/channel/DMA/IRQ/mac80211; @remove skips all teardown
 * @cc:		chipcommon core (register window for CC/PMU/SPROM)
 * @mac:	factory MAC when a trusted source exists
 * @mac_valid:	true only when @mac passed provenance and eth checks
 * @mac_source:	explicit source for @mac; UNKNOWN never becomes usable
 * @ieee:	mac80211 hw, once registered
 * @dma:	DMA64 descriptor rings and DMA capability (M3.2, software only)
 * @irq:	D11 interrupt registration and counters (M3.3)
 * @rx:		FIFO0 RX engine state (M3.4B)
 * @fw:		rev42 firmware validation state (M3.4D1, acquisition only)
 */
struct ob_hw {
	struct bcma_device	*core;
	struct device		*dev;
	struct bcma_bus		*bus;
	/* Immutable silicon capabilities resolved independently per BCMA device. */
	const struct ob_chip_profile *profile;
	u16			chip_id;
	u8			chip_rev;
	bool			validate_only;
	bool			ucode_test_only;
	bool			initvals_test_only;
	/* W2: one chain owner; teardown is skipped on every path. */
	bool			fw_chain_test_only;
	/*
	 * Phase 38C (D5): D2A retention state, published by ob_ucode_run_d2a()
	 * from the contract's own verdict. @ucode_retain means a hardware
	 * mutation may have been issued, so the device must stay bound: no
	 * teardown exists for that partial state and a reboot is the only
	 * documented recovery. ob_probe() returns success instead of a probe
	 * error in that case.
	 */
	bool			ucode_retain;
	bool			ucode_mutated;
	bool			ucode_psm_suspended;
	const char		*ucode_class;
	bool			dma_test_only;
	bool			d3a1_test_only;
	bool			bsinitvals_test_only;
	/*
	 * d3b_boundary_witness: opt-in witness at the D3A1 exit (the D3B
	 * boundary), where the vendor's sub_6656c reads D11+0x3e0 and where this
	 * slice omits that read. It records the pre-state neither D3B run
	 * captured — the ChipCommon/PMU view and the two D11 clock registers
	 * after switch_macfreq — and only then enters the island at 0x3d8,
	 * falling through to 0x3da. Requires bsinitvals_test_only=1; a stalled
	 * island read aborts D3B before any MHF or bsinitvals write.
	 */
	bool			d3b_boundary_witness;
	/*
	 * d3b_witness_v2: modifier of d3b_boundary_witness=1 (refused without
	 * it). Extends the boundary witness without changing the v1 order:
	 * four PMU power-domain reads in the ChipCommon window (PMU_CAP,
	 * PMU_OTPSTAT, PMU_PLLCTL_ADDR/DATA — the rest of the si_pmu_*
	 * inventory), two control reads outside the island band (0x3fe/0x3fc,
	 * bounding it from above), and the vendor's own register 0x3e0 (B10)
	 * after the island. window_alive is then gated on all three
	 * island-range reads being class LIVE.
	 */
	bool			d3b_witness_v2;
	bool			sprom_evidence_only;
	bool			sflags_diag_only;
	bool			radio_id_probe_only;
	bool			radio2069_rev4_probe_only;
	bool			d11_transaction_timing_only;
	/*
	 * Endpoint-loss bisection L2: the eight-read D11 bring-up timing stage
	 * plus exactly one 16-bit D11 PHY-version read, and nothing else. Removes
	 * ob_radio_prepare_for_identity() and the whole attach prefix from the L1
	 * (acphy_pre_radio_test_only) sequence.
	 */
	bool			acphy_identity_timing_only;
	/*
	 * L2b modifier (requires acphy_identity_timing_only): read the upper half
	 * of the same 32-bit word, D11+0x03e2, before the 0x03e0 read.
	 *
	 * 0x03e0 and 0x03e2 are two halves of one D11 dword, so the fabric-level
	 * transaction is the same width and the same word either way. A difference
	 * between them therefore localises the failure to the device's 16-bit
	 * decode rather than to the dword, the window or the access width.
	 */
	bool			acphy_identity_3e2_first;
	/* Set when 0x03e2 failed first and 0x03e0 was deliberately not issued. */
	bool			acphy_identity_3e2_failed;
	/*
	 * L2c modifier (requires acphy_identity_timing_only): read the dword above
	 * the PHY-version word, 0x03e4 and 0x03e6, before the 0x3e2/0x3e0 pair.
	 *
	 * L2 and L2b showed both halves of the 0x03e0 dword fail identically, so
	 * the address is not the unit. This probe asks whether the next dword in
	 * the same window behaves the same way, which separates "one bad dword"
	 * from "a wider register-block failure".
	 */
	bool			acphy_identity_span_probe;
	/* Set when the span probe failed first and 0x3e2/0x3e0 were not issued. */
	bool			acphy_identity_span_failed;
	/*
	 * L2d control modifier (requires acphy_identity_timing_only): re-read a
	 * known-good offset, D11+0x0120 (MACCONTROL), as the FIRST access after
	 * the eight-read block.
	 *
	 * Every PHY-block probe so far was also the first access after that block,
	 * so none of them separated "this block is bad" from "the endpoint stopped
	 * answering at all". Re-reading an offset that worked 2 us earlier does.
	 */
	bool			acphy_identity_control_reprobe;
	bool			acphy_identity_control_failed;
	/*
	 * L2e control modifier (requires acphy_identity_timing_only): read the SAME
	 * known-good offset, D11+0x0120, with a 16-bit access, before everything
	 * else. L2d proved a 32-bit read of 0x0120 completes 6.9 us before 0x3e0
	 * times out, so the endpoint is alive; but every failing access so far was
	 * 16-bit, and no successful 16-bit read exists anywhere in the L2 family.
	 * That leaves "the 0x03e0 block" and "16-bit MMIO to this function"
	 * confounded. Same address, same width, same helper path as the failure:
	 * fast here -> the block is the determinant; stall here -> width is.
	 */
	bool			acphy_identity_control16_reprobe;
	bool			acphy_identity_control16_failed;
	/*
	 * L2f upper-bound modifier (requires acphy_identity_timing_only): read the
	 * 16-bit word ABOVE the failing dword, D11+0x03e6, and only if that
	 * completes also D11+0x03e8, as the first accesses after the block.
	 *
	 * L2/L2b/L2c bounded the region from below (0x3e0, 0x3e2, 0x3e4 all stall
	 * ~201.7 ms) and L2d/L2e removed endpoint death and access width as
	 * explanations. Nothing has ever been read above 0x3e7, so where the region
	 * ends is unmeasured. 0x03e6 is a functional register (the recovered radio
	 * path writes it), which makes it the natural first step across the edge.
	 */
	bool			acphy_identity_upper_probe;
	bool			acphy_identity_upper_failed;
	/*
	 * L2g edge bracket (requires acphy_identity_timing_only): walk a descending
	 * ladder 0x03f0 -> 0x03ec -> 0x03e8 as the first accesses after the block,
	 * stopping at the first stall.
	 *
	 * L2f showed 0x03e6 stalls, so the region reaches at least 0x3e7 and nothing
	 * is known about 0x3e8..0x3ef. A single descending ladder brackets that gap in
	 * one insertion: the last address that completed and the first that stalled
	 * are the two edges of the boundary. Sequential stepping upward would need
	 * one reboot per word; this needs at most one and usually none.
	 */
	bool			acphy_identity_edge_probe;
	bool			acphy_identity_edge_failed;
	/*
	 * L2h wide bracket (requires acphy_identity_timing_only): jump to 0x0800
	 * first, then descend 0x0600 -> 0x0500 -> 0x0480 -> 0x0440 -> 0x0420 ->
	 * 0x0410 -> 0x0408, stopping at the first stall.
	 *
	 * L2g showed the region reaches at least 0x3f1, so descending by one word
	 * only buys ~200 ms and a reboot per attempt. This ladder crosses the rest
	 * of the D11 window in one insertion and leaves only an 8-byte gap between
	 * the last completing rung and the first stalled one.
	 */
	bool			acphy_identity_wide_probe;
	bool			acphy_identity_wide_failed;
	/*
	 * L2i fine edge (requires acphy_identity_timing_only): two addresses only,
	 * 0x0400 then 0x03f8, fail-stop at the first stall.
	 *
	 * L2h bracketed the upper edge of the timeout band to 0x3f2..0x407 using
	 * 0x0408 (no timeout) and L2g's 0x3f0 (timeout). Two bisection words settle
	 * it to 8 bytes: 0x0400 splits 0x3f2..0x407 in half, 0x03f8 splits the lower
	 * half again.
	 */
	bool			acphy_identity_edge2_probe;
	bool			acphy_identity_edge2_failed;
	/*
	 * L2j lower-edge bisection (requires acphy_identity_timing_only): start at
	 * 0x02b0, then halve the bracket [0x0184 completes, 0x03e0 stalls] toward
	 * the first stalling word.
	 *
	 * The upper edge is known to 6 bytes (0x3fa..0x3ff) but the lower edge is not
	 * bounded at all: known-good stops at 0x0184 and known-bad starts at 0x03e0.
	 * A halving search inside one insertion converges in <=12 cheap reads when
	 * the probes complete, and stops honestly at the first stall otherwise.
	 */
	bool			acphy_identity_lower_probe;
	bool			acphy_identity_lower_failed;
	/*
	 * L2k scope witness (requires acphy_identity_timing_only): wrapper-window
	 * baseline (BCMA_IOCTL, BCMA_RESET_ST in core->wrap), the known-good D11
	 * control read at 0x0120, the measured island entry at 0x03da, then
	 * unguarded post-stall witness reads of D11+0x0120 and of the BCMA wrapper
	 * (IOCTL/RESET_ST/RESET_CTL), and finally 0x0120 again after the switch
	 * back. It replaces the legacy single 0x3e0 read.
	 *
	 * The post-stall reads bypass the device-loss guard on purpose: once the
	 * island latches dev_lost the guard returns 0xffffffff WITHOUT issuing
	 * MMIO, so a guarded read cannot witness the silicon at all. Every
	 * witness line logs guarded=0 for that reason.
	 */
	bool			acphy_identity_wrapper_witness;
	/*
	 * L2l (acphy_identity_pll_witness): PHY-PLL pre-state witness. After the
	 * eight bring-up reads it reads clk_ctl_st (0x01e0, 32-bit) and
	 * d11_phypllctl (0x01e8, 32-bit) — both BELOW the measured timeout band —
	 * and only then enters the island at 0x03d8, falling through to 0x03da if
	 * that word still completes. It answers whether the PHY PLL was requested
	 * and available BEFORE the first stalling access, which the address
	 * bisection could not observe. It replaces the legacy single 0x3e0 read
	 * and is exclusive with every other L2 modifier, including L2k.
	 */
	bool			acphy_identity_pll_witness;
	/*
	 * T4 (acphy_identity_3e0_first): the vendor PHY-version read with NO
	 * bring-up block. The eight-read ob_si_d11_diag() block is not issued at
	 * all, so the only D11 accesses of the run are the F1 liveness bracket on
	 * 0x01e0 (32-bit, clk_ctl_st) and the F2 16-bit read of 0x03e0, which is
	 * therefore the FIRST access to the 0x3xx window anywhere in the run.
	 *
	 * It answers the one variable every previous minimal-state run still
	 * carried: whether ANY prior D11 traffic is needed for the stall (L2 ran
	 * 0x3e0 after eight 0x1xx reads; D1'/HIL ran it after radio prepare and
	 * the cflags write; exec03 ran a control before it). It replaces the
	 * legacy single 0x3e0 read, is exclusive with every other L2 modifier and
	 * stops the run at F2 by design.
	 */
	bool			acphy_identity_3e0_first;
	/* Set when a stage was not LIVE and the sequence stopped (fail-stop). */
	bool			acphy_identity_witness_failed;
	/* Set when S1/S2 had no baseline, so the island was never entered. */
	bool			acphy_identity_witness_void;
	/* Set by ANY fail-stop, so the completion marker has one condition. */
	bool			acphy_identity_early_stop;
	bool			acphy_pre_radio_test_only;
	/*
	 * P1-B witness (requires acphy_pre_radio_test_only=1): reproduce the
	 * vendor's pre-read SHM RAM proof (OBJADDR = 0x00010000, three 32-bit
	 * patterns 0xaa5555aa / 0x55aaaa55 / 0x007f0340 written through
	 * 0x164/0x166 and read back) immediately before the 0x3e0 read. A
	 * mismatch or a stall stops the run before the read.
	 *
	 * The P1-A divergence (wrapper IOCTL bit 0x4) is no longer a switch: it is
	 * unconditional in the attach prefix and gated on the 0x408 == 0x5
	 * readback (src/ob_radio.c, OB_ATTACH_CP1_IOCTL4).
	 */
	bool			p1_shm_roundtrip;
	bool			acphy_class0_radio_local_0413_test_only;
	bool			acphy_class0_post0413_phase1_0413_test_only;
	bool			acphy_class0_postphase1_phase2_0413_test_only;
	bool			acphy_class0_postphase2_sub9591e_return_test_only;
	bool			acphy_class0_postbmac_phyreg1b0_branch_gate_test_only;
	bool			acphy_radio_on_test_only;
	bool			acphy_radio_postlpf_test_only;
	bool			acphy_cal_inputs_probe_only;
	bool			acphy_96203_live;
	u16			acphy_96203_gate;	/* 0xffff = unowned (refuse) */
	/* M1-3 modifier: read-only pre-state witness after the admitted contract. */
	bool			acphy_cal_stage_witness;
	/* M1-4 modifier: first owned PHY write arm after the same pre-state. */
	bool			acphy_cal_write_stage;
	bool			acphy_cal_w2_replay;
	bool			acphy_a7089_cal_continue;
	bool			acphy_a7089_golden_unit;
	u32			acphy_a7089_golden_cap;
	bool			acphy_table_probe;
	bool			postbmac_otp8e2_valid;
	u16			postbmac_otp8e2_word;
	u8			postbmac_otp8e2_value;
	bool			rev42_diag_probe_only;
	bool			rev42_diag_full;
	bool			rev42_attach_probe_only;
	bool			rev42_sb_reset_only;
	/* retired in Phase 36Z: refused before any hardware access */
	bool			rev42_ai_probe_only;
	/* Phase 36Z: at most three AI-wrapper reads, no functional-core access */
	bool			rev42_ai_wrapper_only;
	bool			rev42_ai_enable_only;
	/*
	 * Phase 38A: wrapper-enable then exactly one D11 functional-core
	 * MACCONTROL (0x0120) read. Set at dispatch so ob_remove() routes to the
	 * diag retention hook, and so no another stage can claim the device.
	 */
	bool			rev42_d11_maccontrol_once;
	/*
	 * Phase 37B: set once rev42_ai_enable_only has issued a wrapper write (or
	 * latched a loss). There is no teardown for a post-write core state, so
	 * this retains the device across ob_probe()/ob_diag_remove().
	 */
	bool			ai_enable_retain;
	bool			ai_enable_core_up;
	/*
	 * Phase 38A: the VERIFIED result of the one wrapper-enable engine, as
	 * executed by ob_ai_enable_test(). Stage B2 gates the single functional
	 * read on this classification and its counters, never on an insmod exit
	 * status (a post-write failure deliberately returns success to retain the
	 * bound device).
	 */
	struct ob_aie_result	ai_enable_last;
	struct bcma_device	*cc;
	u8			mac[6];
	bool			mac_valid;
	enum ob_mac_source	mac_source;
	struct ob_sprom_board	board;
	struct ieee80211_hw	*ieee;
	struct ob_dma		dma;
	/*
	 * Non-owning handle to the independently allocated, registry-held DMA
	 * resource owner. This object is NOT released by devres_release_all()
	 * and must never be freed through @hw; a retained record keeps it (and
	 * the opaque resource handles) alive for the rest of the module lifetime.
	 */
	struct ob_dma_owner	*dma_owner;
	struct ob_irq		irq;
	struct ob_rx		rx;
	/* Serializes RX init/quiesce/remove/free ownership transitions. */
	struct mutex		rx_lifecycle_lock;
	struct ob_fw		fw;
	struct ob_d3a0		d3a0;
	struct ob_d3a1		d3a1;
	struct ob_d3b		d3b;
	struct ob_diag		diag;
	/* source-side openbrcm-device-loss-trace/v1 state; no hardware access */
	struct ob_dl_trace	dl_trace;
	/* monotonic device-lost latch (D11/BCMA/PCIe inaccessible) */
	bool			dev_lost;
	/* Last confirmed normal-path D11 bootstrap state; never a call marker. */
	enum ob_d11_lifecycle_state d11_state;
	bool			runtime_mac_suspended;
	bool			radio_stage_owns_suspend;
	u32			radio_quiesce_entry_maccontrol;
	u32			radio_quiesce_entry_macintstatus;
	u32			radio_quiesce_polls;
	/* Validated normal-path PHY/radio identity; unknown until attach stage. */
	u8			phy_type;
	u8			phy_revision;
	u16			radio_id;
	u8			radio_revision;
	u8			radio_revision_class;
	u16			chanspec;
	bool			chanspec_valid;
	/* PHY reference clock (vendor pi+0xc24 equivalent) and row descriptors. */
	u32			refclk_hz;
	const struct bcm43xx_chan_tuning_tables *chan_tuning_tables;
	enum ob_acphy_start_state acphy_start_state;
	struct ob_phy_init_lifecycle phy_init;
	struct ob_acphy_pre_radio_state acphy_pre_radio;
	bool			phy_radio_identity_valid;
	bool			radio_access_initialized;
	/* central access-guard accounting (diagnostics; latch is dev_lost) */
	struct ob_guard_stat	guard_stat;
	/*
	 * FIFO530-GATE-01 opt-in. DECLARED LAST ON PURPOSE: every pre-existing
	 * member keeps its offset, so the compiled D2A/D2B path of this build is
	 * instruction-identical to the hardware-tested artifact instead of being
	 * perturbed by a struct-layout shift. Do not move this member into the
	 * bool block above.
	 */
	/* FIFO530-GATE-01 accounting; trailing so no pre-existing member moves. */
	struct ob_d3a1_fifo530_ext d3a1_fifo530;
	bool			d3a1_fifo530_continue;
	/*
	 * P0/P1/P2 read-only PHY witness; appended for the same reason as the
	 * FIFO-530 block, so the already-tested D2A/D2B layout is unchanged.
	 */
	bool			phy_tripoint_probe;
	struct ob_phy_tripoint	phy_tripoint;
	/*
	 * M1-6, appended for the same reason as the blocks above: the pre-existing
	 * layout stays unchanged.  txpwrctrl_enable_shadow is OpenBRCM's own shadow
	 * of the vendor pi+0xfa0 byte (this driver has never issued the enable arm,
	 * so it is 0 by construction; markers say "source=ob_hw_shadow").
	 * phyreg_refcount / wake_override are the vendor 0xbafd1 / 0xbafac state.
	 */
	u8			txpwrctrl_enable_shadow;
	u8			phyreg_refcount;
	u32			wake_override;
	/*
	 * Controlled, explicitly requested incomplete bring-up. This is NOT
	 * production readiness: it admits the proven pre-radio/radio-prefix
	 * stages and the recovered channel-tuning row writes, then publishes
	 * base_ready for mac80211 while calibration, the channel transition and
	 * the RX/TX data path remain closed. Off by default; a bare load is still
	 * refused before allocation.
	 */
	bool			bringup_test_only;
	bool			base_ready;
	u16			base_chanspec;
};

int ob_probe(struct bcma_device *core);
void ob_remove(struct bcma_device *core);

#endif /* _OB_CORE_H_ */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — core lifecycle and bcma glue.
 *
 * Clean-room, specification-driven (see docs/provenance.md).
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "ob_core.h"
#include "ob_d3a1.h"
#include "ob_ai_wrapper_only.h"
#include "ob_si.h"
#include "ob_mac80211.h"
#include "ob_ucode.h"
#include "ob_fw_chain.h"
#include "ob_initvals.h"
#include "ob_radio.h"

/*
 * Explicit firmware-validation-only mode.
 *
 * When set, probe binds through BCMA, performs the read-only identity check
 * below, acquires/hashes/parses the three rev42 images and returns
 * successfully. It deliberately skips every later bring-up stage: no
 * OpenBRCM MMIO access at all (not even reads), no bcma_host_pci_up(),
 * bcma_core_enable()/reset, DMA/IRQ/RX setup, and no mac80211 registration.
 * ob_remove() recognises hw->validate_only and skips teardown of resources
 * that were never initialized.
 *
 * Read-only via sysfs; set at load time:  insmod openbrcm.ko fw_validate_only=1
 */
static bool fw_validate_only;
module_param(fw_validate_only, bool, 0444);
MODULE_PARM_DESC(fw_validate_only,
		 "validate rev42 firmware only; skip all hardware bring-up (default: 0)");

/* Read the selected BCM4352 D11 wrapper SFLAGS word once, then stop. This
 * reports the current wrapper snapshot and historical bit-12 predicate only;
 * it does not execute dma_attach or claim an installed DMA backend.
 */
static bool sflags_diag_only;
module_param(sflags_diag_only, bool, 0444);
MODULE_PARM_DESC(sflags_diag_only,
		 "read selected D11 wrapper SFLAGS once and stop before bring-up (default: 0)");

/*
 * Controlled incomplete bring-up for the first hardware smoke test.
 *
 * This is deliberately a non-mode modifier, not a production default. It
 * permits a bare load to reach the normal probe path, runs the proven bus,
 * D11 reset, firmware and pre-radio/radio-prefix stages, then stops at the
 * documented REV1_AFTER_SUB92500 boundary. The recovered channel-tuning row
 * for the requested channel is applied there; calibration and the channel
 * transition are NOT run. mac80211 may bring the interface administratively
 * up, but RX/TX is not admitted. Without this option the existing fail-closed
 * production gate is unchanged.
 */
static bool bringup_test_only;
module_param(bringup_test_only, bool, 0444);
MODULE_PARM_DESC(bringup_test_only,
		 "BCM4352 only: controlled incomplete bring-up through radio prefix + channel-tuning row; calibration/data-path not run (default: 0)");

/*
 * Explicit D11 rev42 ucode-upload + PSM-start hardware test (M3.4D2A).
 *
 * When set, probe performs the minimum proven core preparation, uploads the
 * validated rev42 ucode, starts the PSM and waits (bounded) for MI_MACSSPNDD,
 * then STOPS. It never applies initvals and never reaches PHY/radio/channel,
 * RX/TX DMA, IRQ registration or mac80211. Mutually exclusive with
 * fw_validate_only (mode policy is explicit; conflicting modes fail probe).
 */
static bool ucode_test_only;
module_param(ucode_test_only, bool, 0444);
MODULE_PARM_DESC(ucode_test_only,
		 "D11 rev42 ucode upload + PSM start only; stops before initvals/PHY/DMA (default: 0)");

/*
 * Explicit isolated D11 rev42 common-initvals test (M3.4D2B).
 *
 * When set, probe runs the hardware-proven D2A core (ucode upload + PSM start),
 * applies exactly the 610 `d11ac1initvals42` common-initvals records, reads the
 * deterministic postconditions, then STOPS before bsinitvals/sub_6656c/PHY/
 * radio/channel/RX/TX DMA/IRQ/mac80211. Mutually exclusive with the other
 * isolated modes; any conflicting combination fails probe before hardware
 * access.
 */
static bool initvals_test_only;
module_param(initvals_test_only, bool, 0444);
MODULE_PARM_DESC(initvals_test_only,
		 "D11 rev42 common initvals + PSM only; stops before bsinitvals/PHY/DMA (default: 0)");

/*
 * W2 isolated firmware-chain test.
 *
 * One identity -> D2A -> common-initvals -> POST_UCODE_READY chain, with the
 * same runtime PHY/radio identity gate as normal bring-up. It stops before
 * PHY/radio/channel/DMA/IRQ/mac80211 and is mutually exclusive with every
 * other isolated mode. Unlike the production path, a retained failure is
 * surfaced as probe success so the device stays bound.
 */
static bool fw_chain_test_only;
module_param(fw_chain_test_only, bool, 0444);
MODULE_PARM_DESC(fw_chain_test_only,
		 "W2 identity + D2A + common-initvals chain; stops at POST_UCODE_READY (default: 0)");

/*
 * Explicit isolated D3A0 DMA lifecycle test (M3.4D3A0).
 *
 * D3A0 TYPE: ISOLATED DMA LIFECYCLE TEST (not a full vendor-prefix
 * reproduction). When set, probe runs the proven D2B prefix, the
 * provenance-pinned D11/clock/IRQ-source prerequisites, then the four TX DMA
 * channels and FIFO0 RX (64 buffers), validates deterministic postconditions,
 * executes the mandatory verified quiesce and frees the Linux DMA resources
 * only after every programmed engine had its own verified normal stop. It
 * never reaches band init/bsinitvals/PHY/radio/channel/mac80211 and never
 * enables EN_MAC, MACINTMASK, MI_DMAINT or the host IRQ route. Mutually
 * exclusive with the other isolated modes; any conflict fails probe before
 * hardware access.
 */
static bool dma_test_only;
module_param(dma_test_only, bool, 0444);
MODULE_PARM_DESC(dma_test_only,
		 "isolated D3A0 DMA lifecycle test (4 TX + FIFO0 RX) + verified quiesce; stops before band/PHY/mac80211 (default: 0)");

/*
 * Explicit isolated D3A1 vendor post-common / pre-PHY D11 tail test
 * (M3.4D3A1).
 *
 * BCM4352 rev42 vendor-ordered post-common/pre-PHY D11 tail test; includes the
 * proven DMA lifecycle in its vendor position (T1 -> DMA -> T2); stops before
 * sub_6656c/bsinitvals/PHY. When set, probe runs the proven D2A/D2B core, the
 * exact rev42 tail (sub_67efd equivalent, T1, DMA, T2, switch_macfreq), the
 * deterministic postconditions and the mandatory verified quiesce, then STOPS
 * before sub_6656c/wlc_phy_init/PHY/radio/channel/mac80211. It never enables
 * EN_MAC, MACINTMASK, MI_DMAINT or the host IRQ route. Mutually exclusive with
 * the other isolated modes; any conflict fails probe before hardware access.
 */
static bool d11_tail_test_only;
module_param(d11_tail_test_only, bool, 0444);
MODULE_PARM_DESC(d11_tail_test_only,
		 "isolated D3A1 vendor-ordered post-common/pre-PHY D11 tail test; includes proven DMA lifecycle; stops before bsinitvals/PHY (default: 0)");

/*
 * FIFO530-GATE-01: explicit, isolated opt-in to the VENDOR's 0x530 expiry
 * behaviour.
 *
 * The vendor `sub_67efd` 0x530 loop has no error path: its bounded poll
 * (21 reads, counters 0xd1..9 step 10) falls through on expiry, performs the
 * vendor's own post-poll read of 0x530 (result discarded at 0x68260..0x6826d),
 * increments the index and programs the next FIFO entry. On this adapter the
 * poll settles at 0x0007, so the fail-closed policy stops the whole tail before
 * T1/DMA (PR-DMA-HW-01, 2026-10-25).
 *
 * This modifier makes the tail follow the vendor in that one respect, but only
 * inside the isolated D3A1 test: every expiry is still recorded and reported as
 * EXPIRED_AND_CONTINUED (never as completion), device loss and an all-ones
 * 0x530 read still abort, and the DMA teardown contract is untouched. The
 * default remains the fail-closed stop; the pairing with d11_tail_test_only=1
 * is mandatory and is enforced before any allocation or hardware access.
 */
static bool d11_fifo530_continue;
module_param(d11_fifo530_continue, bool, 0444);
MODULE_PARM_DESC(d11_fifo530_continue,
		 "D3A1 tail or D3B band-init only: continue after a bounded 0x530 completion-poll expiry exactly as the vendor does (EXPIRED_AND_CONTINUED); requires d11_tail_test_only=1 or bsinitvals_test_only=1 (default: 0)");

/*
 * Explicit isolated read-only external-SPROM evidence capture (D3B MHF3).
 *
 * Reuses the existing external-SPROM diagnostic (ob_si_sprom_diag) to emit the
 * already-read, CRC-validated 234-word rev11 image. It performs NO hardware
 * writes at all: no power-up, no D11/core/clock write, no firmware upload, no
 * PSM start, no DMA, no IRQ, no PHY/radio/channel, no MAC enable and no
 * mac80211. Mutually exclusive with the other isolated modes.
 */
static bool sprom_evidence_only;
module_param(sprom_evidence_only, bool, 0444);
MODULE_PARM_DESC(sprom_evidence_only,
		 "read-only external-SPROM evidence capture (emits the 234-word rev11 image); no bring-up (default: 0)");

/*
 * Explicit isolated BCM2069 radio identity probe (D4-BLOCKER-PLL-BRANCH-HW-PROBE).
 *
 * Reads only the D11 radio-indirect window (radio reg 0 and 1) after the
 * minimum proven core prep, then STOPS. Exactly two selector writes and two
 * 16-bit reads; no PLL sequence, no calibration, no channel, no PHY tables, no
 * DMA, no IRQ, no TX/RX and no mac80211. It is not a radio register dump. The
 * captured revision class selects the first-radio-ON PLL branch A/B/SKIP.
 * Mutually exclusive with every other isolated mode; any conflict fails probe
 * before hardware access.
 */
static bool radio_id_probe_only;
module_param(radio_id_probe_only, bool, 0444);
MODULE_PARM_DESC(radio_id_probe_only,
		 "isolated BCM2069 radio id/revision probe (radio reg 0/1 only); stops before PLL/radio init (default: 0)");

static bool radio2069_rev4_probe_only;
module_param(radio2069_rev4_probe_only, bool, 0444);
MODULE_PARM_DESC(radio2069_rev4_probe_only,
		 "isolated BCM2069 rev4 preference + channel-1 synth stage; bounded stop (default: 0)");

static bool d11_transaction_timing_only;
module_param(d11_transaction_timing_only, bool, 0444);
MODULE_PARM_DESC(d11_transaction_timing_only,
		 "isolated timing of the existing nine-read D11 diagnostic and wrapper read; stops before firmware (default: 0)");

/*
 * Endpoint-loss bisection L2. The power-up + eight-read D11 bring-up timing
 * stage, then exactly ONE 16-bit D11 PHY-version (0x03e0) read, then a stop.
 *
 * It is the L1 (acphy_pre_radio_test_only) sequence with
 * ob_radio_prepare_for_identity() and the whole attach prefix removed, so the
 * one access that kills the endpoint can be compared with and without the
 * radio preparation immediately before it. It performs no write beyond those
 * the shared power-up path already performs, never reaches the attach
 * identity, firmware, ucode, radio, PLL, calibration, DMA, IRQ or mac80211,
 * and owns no resource that needs teardown.
 *
 * Mutually exclusive with every other isolated mode (select25).
 */
static bool acphy_identity_timing_only;
module_param(acphy_identity_timing_only, bool, 0444);
MODULE_PARM_DESC(acphy_identity_timing_only,
		 "endpoint-loss bisection L2: power-up + eight D11 bring-up timing reads + ONE 16-bit 0x3e0 read; stops before radio prepare/attach identity/firmware (default: 0)");

/*
 * L2b modifier: read D11+0x03e2 — the upper half of the same 32-bit word as
 * the PHY-version register — before the 0x03e0 read.
 *
 * The fabric sees one word-granular transaction for either half, so this
 * separates "this 16-bit address" from "this dword / this window / this
 * access width". It is a modifier, never a mode: it does nothing and is
 * rejected unless acphy_identity_timing_only=1, so no existing mode changes
 * behaviour and the validated L2 helper keeps exactly one 16-bit read.
 *
 * Fail-stop: if 0x03e2 stalls or the device is already lost, 0x03e0 is NOT
 * issued and the run ends with its own terminal marker. That keeps the second
 * address unprobed once the first has reproduced the failure.
 */
static bool acphy_identity_3e2_first;
module_param(acphy_identity_3e2_first, bool, 0444);
MODULE_PARM_DESC(acphy_identity_3e2_first,
		 "L2b modifier: one 16-bit D11 0x03e2 read before the 0x3e0 read, fail-stop before 0x3e0 if it stalls; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2c modifier: read 0x03e4 and 0x03e6 — the dword immediately above the
 * PHY-version word — before the 0x3e2/0x3e0 pair, in that order.
 *
 * Order matters. 0x03e2 is known to stall ~201.7 ms, so a probe placed after
 * it would never run: the span must be measured first. 0x03e6 is written by
 * the vendor radio path (ob_radio.c), so the dword is a functional register
 * and not a hole. Both accesses here are reads.
 *
 * Fail-stop at each of the two addresses, in the same 10 ms sense as L2b.
 */
static bool acphy_identity_span_probe;
module_param(acphy_identity_span_probe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_span_probe,
		 "L2c modifier: 16-bit D11 0x03e4 then 0x03e6 reads before the 0x3e2/0x3e0 pair, fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2d control modifier: re-read D11+0x0120 (MACCONTROL) as the FIRST access
 * after the eight-read block. That offset returned a real value in 2 us a few
 * microseconds earlier, so this is the only probe that can tell "the 0x3e0
 * register block is bad" from "the endpoint stopped answering altogether".
 *
 * All three PHY-block probes were the first post-block access, so all three
 * confounded those two explanations. This one does not.
 */
static bool acphy_identity_control_reprobe;
module_param(acphy_identity_control_reprobe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_control_reprobe,
		 "L2d modifier: 32-bit D11 0x0120 re-read as the first access after the bring-up block, fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2e control modifier: read the SAME known-good offset, D11+0x0120, with a
 * 16-bit access, as the first post-block access.
 *
 * L2d showed a 32-bit read of 0x0120 completing in 3 us, 6.9 us before the
 * 0x3e0 access was entered, so the endpoint is alive and "it stopped
 * answering" is refuted. But every failing access was 16-bit and no
 * successful 16-bit read exists in the L2 family, so two explanations remain:
 * the 0x03e0 block, or 16-bit MMIO to this function. This probe keeps the
 * address that is known to work and changes only the width.
 */
static bool acphy_identity_control16_reprobe;
module_param(acphy_identity_control16_reprobe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_control16_reprobe,
		 "L2e modifier: 16-bit D11 0x0120 read as the first access after the bring-up block, fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2f upper-bound modifier: read D11+0x03e6 and, only if that completes, the
 * next word 0x03e8 — the first accesses after the bring-up block.
 *
 * The failing region is bounded from below (0x3e0/0x3e2/0x3e4 all stall) and
 * width and endpoint liveness are excluded, but nothing above 0x3e7 has ever
 * been read. This probe walks one word at a time across the edge under
 * fail-stop, so a stall at 0x03e6 and a stall at 0x03e8 are different answers.
 */
static bool acphy_identity_upper_probe;
module_param(acphy_identity_upper_probe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_upper_probe,
		 "L2f modifier: 16-bit D11 0x03e6 then 0x03e8 reads as the first accesses after the bring-up block, fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2g edge-bracket modifier: descending ladder 0x03f0 -> 0x03ec -> 0x03e8 as
 * the first accesses after the bring-up block, fail-stop at the first stall.
 *
 * L2f proved 0x03e6 stalls, so the region reaches at least 0x3e7 and nothing is
 * known above it. Reading downward from a coarse jump turns that gap into two
 * edges in ONE insertion (last completed address, first stalled address) instead
 * of one reboot per word.
 */
static bool acphy_identity_edge_probe;
module_param(acphy_identity_edge_probe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_edge_probe,
		 "L2g modifier: 16-bit D11 0x03f0 -> 0x03ec -> 0x03e8 descending ladder as the first accesses after the bring-up block, fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2h wide-bracket modifier: jump to 0x0800 first, then descend 0x0600 ->
 * 0x0500 -> 0x0480 -> 0x0440 -> 0x0420 -> 0x0410 -> 0x0408, fail-stop at the
 * first stall.
 *
 * L2g proved the region reaches at least 0x3f1, so word-by-word descent buys
 * ~200 ms and a reboot per step. This ladder covers the rest of the D11 window
 * in one insertion and leaves an 8-byte gap between the last completing rung and
 * the first stalled one.
 */
static bool acphy_identity_wide_probe;
module_param(acphy_identity_wide_probe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_wide_probe,
		 "L2h modifier: 16-bit D11 0x0800 first then descending 0x0600 0x0500 0x0480 0x0440 0x0420 0x0410 0x0408, fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2i fine-edge modifier: exactly two 16-bit reads, 0x0400 then 0x03f8,
 * fail-stop at the first stall.
 *
 * L2h bracketed the upper edge of the timeout band to 0x3f2..0x407 (0x3f0 times
 * out, 0x0408 does not). 0x0400 splits that interval in half and 0x03f8 splits
 * the lower half again, so the edge lands on an 8-byte boundary in one insertion.
 */
static bool acphy_identity_edge2_probe;
module_param(acphy_identity_edge2_probe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_edge2_probe,
		 "L2i modifier: 16-bit D11 0x0400 then 0x03f8 as the first accesses after the bring-up block, fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2j lower-edge bisection modifier: probe 0x02b0 first, then halve the bracket
 * [0x0184 completes, 0x03e0 stalls] toward the first stalling word.
 *
 * The upper edge is measured to 6 bytes; the lower edge is not measured at all
 * (known-good ends at 0x0184, known-bad starts at 0x03e0). Halving converges in
 * at most 12 cheap reads while the probes complete, and stops honestly at the
 * first stall with the bracket it has.
 */
static bool acphy_identity_lower_probe;
module_param(acphy_identity_lower_probe, bool, 0444);
MODULE_PARM_DESC(acphy_identity_lower_probe,
		 "L2j modifier: 16-bit D11 lower-edge bisection starting at 0x02b0 inside [0x0184, 0x03e0], fail-stop on stall; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2k scope witness (requires acphy_identity_timing_only): after the eight
 * bring-up reads, take a wrapper-window baseline (BCMA_IOCTL, BCMA_RESET_ST in
 * core->wrap), re-read the known-good D11+0x0120, then enter the measured
 * island at 0x03da and ask the question the address bisection cannot answer:
 * is the ~201.7 ms failure confined to the island, or has the whole function
 * stopped completing MMIO?
 *
 * The post-stall reads deliberately BYPASS the device-loss guard: the guard
 * synthesises 0xffffffff without issuing MMIO once dev_lost is latched, so a
 * guarded read after the stall would measure the driver, not the silicon.
 * Every witness line logs guarded=0 for exactly that reason.
 *
 * It is a modifier, never a mode; it is refused unless
 * acphy_identity_timing_only=1, and refused in combination with any other L2
 * probe modifier, because it must be the first access after the block and it
 * replaces the legacy single 0x3e0 read. No write is issued by this mode; the
 * only writes in the run remain bcma's own two config dwords per window switch
 * that every prior run already performed.
 */
static bool acphy_identity_wrapper_witness;
module_param(acphy_identity_wrapper_witness, bool, 0444);
MODULE_PARM_DESC(acphy_identity_wrapper_witness,
		 "L2k modifier: wrapper baseline 0x0408/0x0804, D11 0x0120, island 0x03da, then unguarded post-stall witnesses 0x0120 / wrapper / 0x0120; requires acphy_identity_timing_only=1 (default: 0)");

/*
 * L2l PHY-PLL pre-state witness (requires acphy_identity_timing_only): after
 * the eight bring-up reads, read the two clock registers that sit BELOW the
 * measured timeout band and decide the question the bisection could not answer:
 * was the PHY PLL requested/available before the first stalling access?
 *
 *   0x01e0 clk_ctl_st      32-bit; CCS_ERSRC_REQ/AVAIL_PHYPLL live here on
 *                          corerev >= 40, i.e. on our rev 0x2a
 *   0x01e8 d11_phypllctl    32-bit; legacy PLL control whose request/avail
 *                          bits moved into clk_ctl_st on these cores
 *
 * Only after both witnesses are LIVE does it enter the island at 0x03d8 and
 * fall through to 0x03da, the L2j-measured word, so the run also settles which
 * of the two is the lower edge. No write, no window switch, no retry.
 */
static bool acphy_identity_pll_witness;
module_param(acphy_identity_pll_witness, bool, 0444);
MODULE_PARM_DESC(acphy_identity_pll_witness,
		 "L2l modifier: PHY-PLL pre-state witness 0x01e0/0x01e8 before the island 0x03d8/0x03da; requires acphy_identity_timing_only=1 and is exclusive with every other L2 modifier (default: 0)");

/*
 * T4 modifier (acphy_identity_3e0_first, requires
 * acphy_identity_timing_only): the vendor PHY-version read with NO bring-up
 * block.
 *
 * Every executed minimal-state run still carried prior D11 traffic before the
 * first 0x3xx access: L2 read the eight 0x1xx timing words first, D1'/HIL ran
 * radio prepare and the cflags write first, exec03 read a control first. This
 * modifier removes the last of those variables: the eight-read
 * ob_si_d11_diag() block is NOT issued, and the run's only D11 accesses are
 *
 *   F1  D11 0x01e0 32-bit  clk_ctl_st   liveness bracket, guarded
 *   F2  D11 0x03e0 16-bit  PHY version  the vendor read; first 0x3xx access
 *
 * with F2 issued only when F1 was LIVE, the stall latched into dev_lost and
 * the run stopped there (no legacy read, no later stage). One question, one
 * variable: does the stall need any prior D11 traffic at all?
 */
static bool acphy_identity_3e0_first;
module_param(acphy_identity_3e0_first, bool, 0444);
MODULE_PARM_DESC(acphy_identity_3e0_first,
		 "T4 modifier: NO eight-read bring-up block; 32-bit D11 0x01e0 liveness bracket, then ONE 16-bit 0x3e0 read as the first and only 0x3xx access; witness classes, fail-stop; requires acphy_identity_timing_only=1 and is exclusive with every other L2 modifier (default: 0)");

/*
 * One-shot bounded BCM4352 ACPHY pre-radio checkpoint.  This consumes the
 * normal proven prefix through identity attachment, runs only the frozen
 * chanspec/SHM/D11/wrapper transaction, validates its two allowed readbacks,
 * then stops at READY_FOR_RADIO_ON.  It is never a production channel
 * default, never reaches radio-on/PLL/calibration, and owns no DMA/IRQ/RX or
 * mac80211 resource.
 */
static bool acphy_pre_radio_test_only;
module_param(acphy_pre_radio_test_only, bool, 0444);
MODULE_PARM_DESC(acphy_pre_radio_test_only,
		 "isolated BCM4352 ACPHY pre-radio checkpoint (fixture chanspec 0x1001); stops before radio-on/PLL/calibration/DMA/IRQ/mac80211 (default: 0)");

/*
 * P1-B: the vendor's pre-read SHM RAM proof, the second divergence the P0
 * like-for-like trace measured (`p0_attach_trace01/exec-20261028T001326Z`).
 * It is a modifier of acphy_pre_radio_test_only and is refused without that
 * mode before any hardware access.
 *
 * The first divergence — the wrapper IOCTL bit 0x4 — is no longer a switch:
 * the A-only window measured it sufficient on its own (P1_A_ONLY_LIVE,
 * 2026-10-28) and it is now part of the default attach prefix with a hard
 * readback gate (src/ob_radio.c, OB_ATTACH_CP1_IOCTL4).
 */
static bool p1_shm_roundtrip;
module_param(p1_shm_roundtrip, bool, 0444);
MODULE_PARM_DESC(p1_shm_roundtrip,
		 "P1-B: vendor SHM RAM round-trip (OBJADDR 0x160, patterns through 0x164/0x166 with readback) immediately before the 0x3e0 read; mismatch/stall stops before 0x3e0; requires acphy_pre_radio_test_only=1 (default: 0)");

/* Isolated BCM4352 selector-class-0 local radio checkpoint.  This mode owns
 * real radio mutations and therefore preserves the device on both PASS and
 * FAIL; it never enters the historical continuation/calibration tail. */
static bool acphy_class0_radio_local_0413_test_only;
module_param(acphy_class0_radio_local_0413_test_only, bool, 0444);
MODULE_PARM_DESC(acphy_class0_radio_local_0413_test_only,
		 "isolated BCM4352 class-0 radio-local 0x413 poll; stops before radio continuation/calibration (default: 0)");

/* Separate opt-in phase-1 continuation after the proven phase-0 0x0413 poll.
 * This mode performs local radio writes and preserves state on PASS/FAIL. */
static bool acphy_class0_post0413_phase1_0413_test_only;
module_param(acphy_class0_post0413_phase1_0413_test_only, bool, 0444);
MODULE_PARM_DESC(acphy_class0_post0413_phase1_0413_test_only,
		 "isolated BCM4352 class-0 post-0x0413 phase-1 poll; stops before later phases/calibration (default: 0)");

/* Separate same-insertion phase-2 continuation after the proven phase-1 poll.
 * It owns RF mutations and leaves the module/device state preserved. */
static bool acphy_class0_postphase1_phase2_0413_test_only;
module_param(acphy_class0_postphase1_phase2_0413_test_only, bool, 0444);
MODULE_PARM_DESC(acphy_class0_postphase1_phase2_0413_test_only,
		 "isolated BCM4352 class-0 phase-2 local 0x413 poll after same-run phase-1 success; preserves state and stops after the third poll (default: 0)");

/* Separate endpoint after a same-insertion phase-2 third-poll PASS. */
static bool acphy_class0_postphase2_sub9591e_return_test_only;
module_param(acphy_class0_postphase2_sub9591e_return_test_only, bool, 0444);
MODULE_PARM_DESC(acphy_class0_postphase2_sub9591e_return_test_only,
		 "isolated BCM4352 class-0 post-phase-2 sub_9591e return checkpoint; preserves state at the historical return boundary (default: 0)");

/* Extend the isolated path through the selected post-BMAC SI operations.
 * It follows only the accepted OTP/PHY branch, accesses PMU/GPIO through the
 * BCMA ChipCommon core, and stops before the next analog-PHY block. */
static bool acphy_class0_postbmac_phyreg1b0_branch_gate_test_only;
module_param(acphy_class0_postbmac_phyreg1b0_branch_gate_test_only, bool, 0444);
MODULE_PARM_DESC(acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
		 "isolated BCM4352 class-0 post-BMAC continuation through ChipCommon PMU/GPIO; preserves state and stops before analog TX LPF (default: 0)");

static bool acphy_radio_on_test_only;
module_param(acphy_radio_on_test_only, bool, 0444);
MODULE_PARM_DESC(acphy_radio_on_test_only,
		 "isolated BCM4352 radio-ON checkpoint: pre-radio -> radio prefix -> analog TX LPF -> sub_92500; stops at REV1_AFTER_SUB92500, fail-stop on device loss (default: 0)");

static bool acphy_radio_postlpf_test_only;
module_param(acphy_radio_postlpf_test_only, bool, 0444);
static bool acphy_cal_inputs_probe_only;
module_param(acphy_cal_inputs_probe_only, bool, 0444);
static bool acphy_96203_live;
module_param(acphy_96203_live, bool, 0444);
static int acphy_96203_gate = -1;
module_param(acphy_96203_gate, int, 0444);
static bool acphy_cal_stage_witness;
module_param(acphy_cal_stage_witness, bool, 0444);
static bool acphy_cal_write_stage;
module_param(acphy_cal_write_stage, bool, 0444);
static bool acphy_cal_w2_replay;
module_param(acphy_cal_w2_replay, bool, 0444);
static bool phy_tripoint_probe;
module_param(phy_tripoint_probe, bool, 0444);
static bool acphy_a7089_cal_continue;
module_param(acphy_a7089_cal_continue, bool, 0444);
static bool acphy_a7089_golden_unit;
module_param(acphy_a7089_golden_unit, bool, 0444);
static uint acphy_a7089_golden_cap;
module_param(acphy_a7089_golden_cap, uint, 0444);
static bool acphy_table_probe;
module_param(acphy_table_probe, bool, 0444);
MODULE_PARM_DESC(acphy_radio_postlpf_test_only,
		 "isolated BCM4352 post-LPF continuation: radio prefix + LPF + sub_92500 then the pinned sub_a7089 + sub_a0be4 vectors; fail-stop on device loss (default: 0)");
MODULE_PARM_DESC(acphy_cal_inputs_probe_only,
		 "isolated BCM4352 calibration-input contract: reaches RADIO_COMPLETE, records unresolved vendor runtime inputs and stops before calibration/channel/PLL writes (default: 0)");
MODULE_PARM_DESC(acphy_cal_stage_witness,
		 "M1-3 modifier of acphy_cal_inputs_probe_only: after the admitted contract, sample PHY 0x0401/0x0400/0x0070 and run the bounded PHY 0x0380 readiness poll read-only; no calibration write is issued (default: 0)");
MODULE_PARM_DESC(acphy_cal_write_stage,
		 "M1-4 modifier of acphy_cal_inputs_probe_only: after the admitted contract and a matching M1-3 pre-state, execute the vendor txpwrctrl disable arm on PHY 0x0070 (value-identity RMW) twice; refuses with zero writes on any other state (default: 0)");
MODULE_PARM_DESC(acphy_cal_w2_replay,
		 "W2 modifier of acphy_cal_inputs_probe_only: run the vendor calibration-entry window (rxcore_setstate, then the 0x0070 bracket held open across the PHY[0x0380] kick and poll, then restore); refuses with zero writes unless the boundary is 0x7777 at entry and 0x7733 after setstate (default: 0)");
MODULE_PARM_DESC(acphy_96203_live,
		 "W96: run the reconstructed sub_96203 block on both chains before the golden radio replay (PHY 0x0739/0x073a/0x0725 per chain: RMW 0x0080/0x0004 then restore); requires acphy_cal_inputs_probe_only=1 and acphy_96203_gate, fail-stop (default: 0)");
MODULE_PARM_DESC(acphy_96203_gate,
		 "W96: PI+0x16c equivalent supplied to the live sub_96203 block (0..255; the vendor value is not owned by the driver, so passing one is a recorded hypothesis). Default -1 = unowned: the block refuses before any MMIO (default: -1)");
MODULE_PARM_DESC(phy_tripoint_probe,
		 "read-only PM1/P0/P1/P2 witness of PHY 0x0739/0x073a/0x0725/0x0729/0x0721/0x0728/0x0720 + 0x0408 (after the 0x408==0x5 gate, before the 0x03e0 identity read, post-ucode, and before the LPF block; the core mirror runs in the attach prefix); requires acphy_cal_inputs_probe_only=1 and issues no write (default: 0)");

static bool ob_post0413_probe_failure(struct ob_hw *hw, const char *stage,
				      int ret)
{
	if (!hw || (!hw->acphy_class0_post0413_phase1_0413_test_only &&
	    !hw->acphy_class0_postphase1_phase2_0413_test_only &&
	    !hw->acphy_class0_postphase2_sub9591e_return_test_only &&
	    !hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only) ||
	    (!ret && !hw->dev_lost))
		return false;
	if (hw->acphy_class0_postphase1_phase2_0413_test_only ||
	    hw->acphy_class0_postphase2_sub9591e_return_test_only ||
	    hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only) {
		dev_info(hw->dev,
			 "ACPHY_CLASS0_POSTPHASE1_FAIL stage=%s ret=%d\n",
			 stage, ret ? ret : -5);
		dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_STOP\n");
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-postphase1 FATAL/dev-lost; device kept bound, reboot required\n");
			return true;
		}
		return false;
	}
	dev_info(hw->dev,
		 "ACPHY_CLASS0_POST0413_FAIL stage=%s ret=%d\n",
		 stage, ret ? ret : -5);
	dev_info(hw->dev, "ACPHY_CLASS0_POST0413_STOP\n");
	if (hw->dev_lost) {
		dev_crit(hw->dev,
			 OB_DRV_NAME ": acphy-post0413 FATAL/dev-lost; device kept bound, reboot required\n");
		return true;
	}
	return false;
}

/*
 * Explicit isolated D3B band-init test (M3.4D3B).
 *
 * Runs the proven D2A/D2B core and the exact D3A1 vendor prefix with the D3A0
 * DMA engines left LIVE (vendor order T1 -> DMA -> T2), then the exact
 * sub_6656c MHF writes + d11ac1bsinitvals42 (73 records) and STOPS before
 * wlc_phy_init. It never enters PHY/radio/channel, never enables EN_MAC, never
 * routes the host IRQ and never registers mac80211. Mutually exclusive with the
 * other isolated modes; any conflict fails probe before hardware access.
 */
static bool bsinitvals_test_only;
module_param(bsinitvals_test_only, bool, 0444);
MODULE_PARM_DESC(bsinitvals_test_only,
		 "isolated D3B band-init + d11ac1bsinitvals42 test; DMA stays live through band init; stops before wlc_phy_init/PHY (default: 0)");

/*
 * D3B boundary witness (requires bsinitvals_test_only=1). At the D3A1 exit —
 * exactly where the vendor's sub_6656c reads D11+0x3e0 and where this slice
 * omits that read — it records the pre-state that neither D3B run captured:
 * the ChipCommon/PMU view of the clock/power domain (a different core window,
 * readable even when the D11 path is wedged), the two D11 clock registers read
 * AFTER switch_macfreq, and only then the island at 0x3d8 (falling through to
 * 0x3da). Read-only; a stalled island read aborts D3B before any MHF or
 * bsinitvals write.
 */
static bool d3b_boundary_witness;
module_param(d3b_boundary_witness, bool, 0444);
MODULE_PARM_DESC(d3b_boundary_witness,
		 "D3B boundary witness: CC/PMU + D11 0x1e0/0x1e8 pre-state, then the island 0x3d8/0x3da at the D3A1 exit; requires bsinitvals_test_only=1 (default: 0)");

/*
 * Witness v2 (requires d3b_boundary_witness=1). Extends the boundary witness
 * with the four remaining PMU power-domain registers of the si_pmu_* inventory
 * (CC window: PMU_CAP, PMU_OTPSTAT, PMU_PLLCTL_ADDR/DATA), two control reads
 * outside the island band (0x3fe/0x3fc, bounding it from above), and the
 * vendor's own 0x3e0 read (B10) after the island; window_alive is gated on all
 * three island-range reads being LIVE. Refused without the witness, before any
 * hardware access.
 */
static bool d3b_witness_v2;
module_param(d3b_witness_v2, bool, 0444);
MODULE_PARM_DESC(d3b_witness_v2,
		 "modifier for d3b_boundary_witness=1: PMU-domain P1..P4 reads, island controls C1/C2 (0x3fe/0x3fc), vendor read B10 (0x3e0); requires d3b_boundary_witness=1 (default: 0)");

/*
 * Explicit staged BCM4352 / D11 rev42 diagnostic bring-up
 * (REV42-DIAGNOSTIC-BRINGUP). Independent experimental mode that walks the
 * recovered vendor checkpoints CP0..CP6 up to a stable radio-ID read and STOPS.
 *
 *   rev42_diag_probe_only=1          vendor-minimal radio-ID path
 *   rev42_diag_probe_only=1 rev42_diag_full=1
 *                                    staged path: + D2A ucode / D2B common
 *                                    initvals (CP1/CP2/CP3)
 *
 * It never reaches band init, PHY init, radio configuration, calibration,
 * channel, EN_MAC, DMA, the host IRQ route or mac80211. Mutually exclusive with
 * the other isolated modes; any conflict fails probe before hardware access.
 */
static bool rev42_diag_probe_only;
module_param(rev42_diag_probe_only, bool, 0444);
MODULE_PARM_DESC(rev42_diag_probe_only,
		 "staged rev42 diagnostic to a stable radio-ID read (CP0..CP6); stops before band/PHY/EN_MAC/DMA/IRQ (default: 0)");
static bool rev42_diag_full;
module_param(rev42_diag_full, bool, 0444);
MODULE_PARM_DESC(rev42_diag_full,
		 "modifier for rev42_diag_probe_only: also run D2A ucode + D2B common initvals (default: 0)");

/*
 * Explicit cold-attach radio-sequence probe (REV42-RADIO-ATTACH-SEQUENCE).
 * Reproduces the vendor wlc_bmac_corereset -> wlc_bmac_phy_reset -> SITE A ->
 * PHY-version -> AC-PHY attach -> SITE B -> radio-window sequence for a cold
 * BCM4352/rev42 attach, with checkpoints CP0..CP15. It never runs ucode,
 * initvals, bsinitvals, DMA, RX, TX, host IRQ or mac80211. Mutually exclusive
 * with the other isolated modes.
 */
static bool rev42_attach_probe_only;
module_param(rev42_attach_probe_only, bool, 0444);
MODULE_PARM_DESC(rev42_attach_probe_only,
		 "isolated cold-attach radio sequence (CP0..CP15) through SITE A/B to the radio read (default: 0)");

/*
 * Explicit vendor sb_core_reset cold-path probe (REV42-SB-CORE-RESET).
 *
 * TERMINOLOGY -- the "SB" block is the Broadcom silicon bridge register block
 * that lives at the TOP OF THE D11 CORE FUNCTIONAL WINDOW (offsets
 * 0x0F00-0x0FFF), i.e. the window selected by core->addr. It is NOT the BCMA
 * "wrapper" window selected by core->wrap (that is the AI/ARM agent window this
 * mode deliberately never touches). Every access below therefore uses
 * ob_d11_read32()/ob_d11_write32(), never ob_axi_*(). The trace block label is
 * "SB_CORE" for the same reason.
 *
 * Reproduces the recovered Broadcom SB reset sequence (SB+0x0F98 etc.)
 * instead of bcma_core_enable, confines itself to SB-block registers inside the
 * D11 core window until the predicate passes, and tests one transition:
 * CORE_DOWN -> CORE_UP_AND_D11_ACCESSIBLE. No PHY reset, no SITE A, no
 * ucode/initvals/DMA/IRQ/mac80211, and no BCMA wrapper-window access.
 */
static bool rev42_sb_reset_only;
module_param(rev42_sb_reset_only, bool, 0444);
MODULE_PARM_DESC(rev42_sb_reset_only,
		 "isolated vendor sb_core_reset cold path (R0..R7); touches only SB-block registers (0x0F00-0x0FFF) inside the D11 CORE window core->addr, never the BCMA wrapper window core->wrap (default: 0)");

/*
 * RETIRED (Phase 36Z) — rev42_ai_probe_only.
 *
 * The historical read-only AI probe read the D11 FUNCTIONAL-CORE window
 * (core->addr + 0x0FFC) right after observing the core held in reset, then
 * read two PCI config dwords. In the only live run it preceded loss of config
 * access for the endpoint and its upstream port. Its sequence is DELETED from
 * ob_diag.c. The parameter is kept only so the retired value is refused with a
 * documented error instead of failing insmod with an unknown parameter: the
 * rejection happens in ob_probe() BEFORE any allocation, MMIO or BCMA access
 * and never falls through to ordinary OpenBRCM initialization.
 */
static bool rev42_ai_probe_only;
module_param(rev42_ai_probe_only, bool, 0444);
MODULE_PARM_DESC(rev42_ai_probe_only,
		 "RETIRED (Phase 36Z) after the live PCIe failure; set to 1 it is refused with -EOPNOTSUPP before any register access. Use rev42_ai_wrapper_only=1 (default: 0)");

/*
 * rev42_ai_wrapper_only (Phase 36Z): at most THREE reads, all in the
 * BCMA-selected AI wrapper window (core->wrap) — RESET_ST, RESET_CTL, IOCTL.
 * No D11 functional-core access (notably NOT 0x0FFC), no PCI config readback,
 * no window programming, no reset/enable write, no firmware/PHY/radio/DMA/IRQ/
 * mac80211. Stops on the device-loss latch, on a callback error and on an
 * all-ones read of any of the three control/status registers. Mutually
 * exclusive with every other isolated mode. Contract and host-tested
 * sequencing: src/ob_ai_wrapper_only.h.
 */
static bool rev42_ai_wrapper_only;
module_param(rev42_ai_wrapper_only, bool, 0444);
MODULE_PARM_DESC(rev42_ai_wrapper_only,
		 "isolated AI-wrapper observation: exactly RESET_ST/RESET_CTL/IOCTL through BCMA, max 3 reads, stop on dev-loss or all-ones; no functional-core read (default: 0)");

/*
 * AI core-enable transition (upstream bcma_core_enable, flags=0). THE FIRST
 * rev42 MODE THAT WRITES A REGISTER. Three writes, all in the BCMA wrapper
 * window (core->wrap) through ob_axi_write32(): IOCTL=0x3 (CLK|FGC),
 * RESET_CTL=0x0 (de-assert reset), IOCTL=0x1 (drop FGC), each followed by
 * exactly one readback. Then it re-evaluates the vendor ai_iscoreup predicate.
 * Phase 37B removed the 0x0FFC D11 functional-core read entirely: this mode
 * ends at the wrapper boundary in every branch, including a successful
 * core-up. The MODULE_PARM_DESC below said "+ one D11 core read" until
 * amendment 01; that string was stale and is corrected here.
 *
 * Every write is transcribed from upstream Linux drivers/bcma/core.c, not
 * invented. No PCI config write of its own, no pci_iomap, no polling loop and
 * no unbounded wait: bcma_core_disable() early-returns when the core is
 * already held in reset, so its bounded RESET_ST poll is skipped. It aborts
 * rather than improvise if the core is not already reset-held.
 *
 * BCMA's own side effect is real, is not issued by this driver, and is not a
 * D11 functional-core access: every first access after a window switch runs
 * bcma_host_pci_switch_core(), which performs TWO
 * pci_write_config_dword() calls -- 0x80 (BCMA_PCI_BAR0_WIN) = core->addr,
 * and 0x70 (BCMA_PCIE2_BAR0_WIN2; this device has a PCIe Gen2 core, so
 * host_is_pcie2 is true) or 0xAC (BCMA_PCI_BAR0_WIN2) = core->wrap. Never
 * touches D11 functional core, PHY/radio/PLL/synth/ucode/initvals/
 * calibration/DMA/IRQ/SHM/mac80211.
 *
 * LIFECYCLE: retaining the device after probe() keeps it bound and keeps this
 * module loaded; it does NOT guarantee that the binding survives `rmmod`, and
 * it cannot restore the pre-transition state. Module unload is forbidden after
 * any enable write (LIFECYCLE_POLICY.md, amendment 01).
 */
static bool rev42_ai_enable_only;
module_param(rev42_ai_enable_only, bool, 0444);
MODULE_PARM_DESC(rev42_ai_enable_only,
	 "isolated AI core-enable: 3 wrapper pre-state reads; 3 wrapper writes each with its readback; no D11 functional-core access; no firmware/PHY/radio/DMA/IRQ; rmmod forbidden after any write (default: 0)");

/*
 * Phase 38A — wrapper enable, then exactly ONE D11 functional-core read.
 *
 * Stage B1 is the already verified Phase 37B wrapper-enable transition, using
 * the SAME engine (src/ob_ai_enable_only.h); this mode does not re-implement
 * it. Stage B2 refuses the functional read unless the actual engine result is
 * D11_WRAPPER_CORE_UP with its documented counters (6 reads / 3 writes) and no
 * device-loss or fatal state. Stage B3 performs exactly one 32-bit read of D11
 * MACCONTROL (0x0120) through ob_d11_read32_trusted(), i.e. through BCMA's
 * functional core window (core->addr) and not the AI wrapper window. Stage B4
 * classifies that read and stops: 0xffffffff or a latched loss is fail-stop and
 * is never reported as a readable observation.
 *
 * Total on the successful path: 7 driver-initiated register reads (6 wrapper +
 * 1 D11), 3 writes (all wrapper), 0 D11 writes. BCMA's own window selection may
 * write PCI config 0x80 and 0x70/0xAC on the switch from core->wrap to
 * core->addr; those are BCMA's writes, are documented separately and are NOT
 * included in these counters. No firmware/PHY/radio/PLL/cal/DMA/IRQ/SHM/
 * mac80211 operation is reachable. Mutually exclusive with every other mode.
 */
static bool rev42_d11_maccontrol_once;
module_param(rev42_d11_maccontrol_once, bool, 0444);
MODULE_PARM_DESC(rev42_d11_maccontrol_once,
	 "isolated wrapper-enable plus exactly ONE D11 MACCONTROL (0x0120) 32-bit read; requires a verified D11_WRAPPER_CORE_UP; fail-stop on all-ones/loss; no second functional read; 0 D11 writes (default: 0)");

/*
 * Phase 38C (D5): a D2A failure after the first hardware mutation must not be
 * returned as a probe error.
 *
 * Returning it lets BCMA release the device and makes the module removable,
 * while the hardware stays in a partially programmed D11 state (wrapper
 * enabled, MACCONTROL written, possibly half an upload) for which this driver
 * has no teardown contract: no recovery write is provenance-backed, so any
 * "cleanup" would be an invented register sequence. The retained state is the
 * designed failure mode, not a cleanup opportunity.
 *
 * Returns true when the caller must stop with probe success (device bound).
 * A failure BEFORE any mutation (firmware absent/mismatched, admission
 * rejection) is still a normal probe error and is reported as one.
 */
static bool ob_d2a_retain_failure(struct ob_hw *hw, int ret)
{
	if (!ret)
		return false;

	if (hw->dev_lost) {
		dev_crit(hw->dev,
			 OB_DRV_NAME ": D2A DEVICE_LOSS_SUSPECTED class=%s; device kept bound, no cleanup, reboot required\n",
			 hw->ucode_class ? hw->ucode_class : "UNKNOWN");
		return true;
	}
	if (hw->ucode_retain) {
		dev_emerg(hw->dev,
			  OB_DRV_NAME ": D2A POST_WRITE_RETAINED class=%s mutated=%d psm_suspended=%d; no teardown contract exists for this state, device kept bound, do not rmmod, reboot to recover\n",
			  hw->ucode_class ? hw->ucode_class : "UNKNOWN",
			  hw->ucode_mutated ? 1 : 0,
			  hw->ucode_psm_suspended ? 1 : 0);
		return true;
	}
	return false;
}

int ob_probe(struct bcma_device *core)
{
	struct ob_hw *hw;
	struct pci_dev *pdev;
	struct ob_chip_identity identity = { 0 };
	const struct ob_chip_profile *profile;
	enum ob_isolated_mode mode;
	enum ob_chip_profile_status profile_status;
	int ret;

	if (!core || !core->bus)
		return -ENODEV;
	if (core->id.manuf != BCMA_MANUF_BCM ||
	    core->id.id != BCMA_CORE_80211)
		return -ENODEV;

	/*
	 * Phase 36Z quarantine gate (FIRST device-independent decision).
	 *
	 * rev42_ai_probe_only read the D11 functional-core window (0x0FFC) after
	 * observing the core held in reset; in the only live run it preceded
	 * loss of config access for the endpoint and its upstream port. The
	 * sequence is deleted, and the option is refused HERE — before
	 * devm_kzalloc(), before any driver-initiated MMIO, before any BCMA
	 * access and before every normal bring-up stage — with a documented
	 * error. It is never silently redirected to another mode and never
	 * falls through to ordinary initialization. This deliberately precedes
	 * the silicon-profile guard: a retired configuration is a configuration
	 * error regardless of which adapter is present.
	 */
	if (ob_aiw_legacy_retired(rev42_ai_probe_only)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": rev42_ai_probe_only is RETIRED after the live PCIe failure (Phase 36Z); refused with -EOPNOTSUPP before any register access. Use rev42_ai_wrapper_only=1.\n");
		return -EOPNOTSUPP;
	}

	/* This is the first OpenBRCM device-admission gate. PCI identity comes
	 * from BCMA's already-owned host_pci metadata; chip/core identity was
	 * populated by BCMA enumeration. No config-space or D11 access occurs.
	 * Resolve before every diagnostic/test-mode dispatch so no mode can
	 * bypass the silicon profile restriction. */
	pdev = core->bus->hosttype == BCMA_HOSTTYPE_PCI ?
		core->bus->host_pci : NULL;
	if (!pdev) {
		dev_err(&core->dev,
			OB_DRV_NAME ": cannot admit device without PCI/BCMA identity\n");
		return -ENODEV;
	}
	identity.pci_present = true;
	identity.bcma_present = true;
	identity.d11_present = true;
	identity.pci_vendor = pdev->vendor;
	identity.pci_device = pdev->device;
	identity.pci_revision = pdev->revision;
	identity.chip_id = core->bus->chipinfo.id;
	identity.chip_revision = core->bus->chipinfo.rev;
	identity.d11_manufacturer = core->id.manuf;
	identity.d11_core_id = core->id.id;
	identity.d11_revision = core->id.rev;
	identity.d11_class = core->id.class;
	profile_status = ob_chip_profile_resolve(&identity, &profile);
	if (profile_status != OB_CHIP_PROFILE_OK) {
		dev_err(&core->dev,
			OB_DRV_NAME ": device profile rejected status=%u pci=%04x:%04x rev=%02x chip=%04x/%u d11=%04x:%04x rev=%u class=%u\n",
			(unsigned int)profile_status, identity.pci_vendor,
			identity.pci_device, identity.pci_revision,
			identity.chip_id, identity.chip_revision,
			identity.d11_manufacturer, identity.d11_core_id,
			identity.d11_revision, identity.d11_class);
		return -ENODEV;
	}

	/*
	 * A FIFO poll expiry in an earlier probe of this exact PCI/BCMA device
	 * left partially programmed TX FIFO state with no verified recovery.
	 * Refuse the new probe before devm_kzalloc() and before any MMIO.  This
	 * is per-identity: a different adapter is not affected.  The check is
	 * deliberately before every mode dispatch so no diagnostic mode can
	 * bypass it.
	 */
	if (ob_d3a1_fifo_reprobe_forbidden(core)) {
		dev_crit(&core->dev,
			 OB_DRV_NAME ": refusing probe: prior FIFO failure on this device; reboot required\n");
		return -EBUSY;
	}

	/*
	 * A retained DMA allocation on this exact identity, or a replacement in
	 * the same PCI slot, means DMA quiescence was never proven for the
	 * previous device. Refuse before devm_kzalloc() and before every stage
	 * that drives the D11/PHY/firmware: ob_si_probe(), attach reset, radio
	 * preparation, request_firmware(), ucode upload/PSM start and initvals.
	 * The key comes from BCMA-enumerated identity already used for profile
	 * admission; this guard reads no device register. It is checked before
	 * every mode dispatch so no diagnostic mode can re-initialize a device
	 * whose DMA memory is still retained.
	 */
	if (ob_dma_owner_core_probe_refused(core)) {
		dev_crit(&core->dev,
			 OB_DRV_NAME ": refusing probe: retained DMA allocation for this device/slot; reboot required\n");
		return -EBUSY;
	}

	/*
	 * A fatal DMA state with no retained owner (for example a device lost
	 * before any allocation) still forbids re-initializing THIS device. The
	 * evidence is keyed by the same identity as the owner, so a record on
	 * another PCI function does not refuse this adapter (Phase 34). Checked
	 * before every mode dispatch for the same reason as the FIFO guard.
	 */
	if (ob_dma_fatal_core_refused(core)) {
		dev_crit(&core->dev,
			 OB_DRV_NAME ": refusing probe: prior fatal DMA state on this device/slot; reboot required\n");
		return -EBUSY;
	}

	/*
	 * Fail-closed coupling: rev42_diag_full is a MODIFIER of
	 * rev42_diag_probe_only, never a mode of its own. On its own it
	 * selects no isolated mode, so ob_probe() would fall through to the
	 * normal ob_si_probe() bring-up (PHY, radio, calibration, DMA, IRQ,
	 * mac80211) with the operator believing a bounded diagnostic was
	 * requested. Reject it here -- before any allocation and before any
	 * hardware access -- so the invalid pairing can never reach bring-up.
	 * This is an in-driver invariant, not a harness convention.
	 */
	if (!ob_isolated_diag_full_allowed(rev42_diag_full,
					   rev42_diag_probe_only)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": rev42_diag_full=1 requires rev42_diag_probe_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}

	/*
	 * FIFO530-GATE-01 coupling: d11_fifo530_continue is a MODIFIER of the
	 * isolated D3A1 tail test or of the isolated D3B band-init test (whose
	 * prefix is the same D3A1 tail), never a mode of its own. A bare opt-in
	 * would select no isolated mode (and so would be refused below anyway),
	 * but it must also never reach the default production path or any other
	 * isolated mode, so the invalid pairing is rejected here before
	 * devm_kzalloc(), before any driver-initiated MMIO and before every
	 * dispatch branch.
	 */
	if (!ob_d3a1_fifo530_continue_allowed(d11_fifo530_continue,
					      d11_tail_test_only,
					      bsinitvals_test_only)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": d11_fifo530_continue=1 requires d11_tail_test_only=1 or bsinitvals_test_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	/*
	 * The boundary witness is a modifier of D3B, never a mode of its own;
	 * it is refused outside bsinitvals_test_only before any hardware access.
	 *
	 * M1a (2026-10-27): the witness MAY share a run with the FIFO-530
	 * continuation opt-in. The 0x530 expiry is deterministic on this bench
	 * and the default prefix refuses on expiry, so the continuation is the
	 * only vendor-faithful way to reach the D3A1 exit this witness
	 * measures; the witness records the continuation state it ran on (the
	 * summary carries fifo530_* fields), so the confound is measured rather
	 * than hidden (docs/m34d3b_boundary_witness.md).
	 */
	if (d3b_boundary_witness && !bsinitvals_test_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": d3b_boundary_witness=1 requires bsinitvals_test_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (d11_fifo530_continue && d3b_boundary_witness) {
		dev_info(&core->dev,
			 OB_DRV_NAME ": d3b-test: boundary witness armed with d11_fifo530_continue=1 (vendor-faithful continuation; the FIFO state is recorded at the boundary)\n");
	}
	/*
	 * Witness v2 is a modifier of the witness, never a mode. It is refused
	 * on its own before any hardware access; when armed it is logged, like
	 * the FIFO-530 combination, so the run's mode is explicit in the log.
	 */
	if (d3b_witness_v2 && !d3b_boundary_witness) {
		dev_err(&core->dev,
			OB_DRV_NAME ": d3b_witness_v2=1 requires d3b_boundary_witness=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (d3b_witness_v2) {
		dev_info(&core->dev,
			 OB_DRV_NAME ": d3b-test: witness v2 armed (P1..P4 PMU-domain CC reads, C1/C2 controls 0x3fe/0x3fc, B10 vendor read 0x3e0; window_alive requires three LIVE island reads)\n");
	}

	/*
	 * L2b is a modifier of the L2 mode, not a mode. Requesting it on its own
	 * would load the module with no bounded path selected, so the pairing is
	 * rejected before devm_kzalloc() and before any hardware access.
	 */
	if (acphy_cal_stage_witness && !acphy_cal_inputs_probe_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_cal_stage_witness=1 requires acphy_cal_inputs_probe_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_cal_write_stage && !acphy_cal_inputs_probe_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_cal_write_stage=1 requires acphy_cal_inputs_probe_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_cal_write_stage && acphy_cal_stage_witness) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_cal_write_stage=1 and acphy_cal_stage_witness=1 are mutually exclusive; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_cal_w2_replay && !acphy_cal_inputs_probe_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_cal_w2_replay=1 requires acphy_cal_inputs_probe_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_cal_w2_replay &&
	    (acphy_cal_write_stage || acphy_cal_stage_witness)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_cal_w2_replay=1 is mutually exclusive with acphy_cal_write_stage=1 and acphy_cal_stage_witness=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (phy_tripoint_probe && !acphy_cal_inputs_probe_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": phy_tripoint_probe=1 requires acphy_cal_inputs_probe_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_96203_live && !acphy_cal_inputs_probe_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_96203_live=1 requires acphy_cal_inputs_probe_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_96203_gate < -1 || acphy_96203_gate > 0xff) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_96203_gate must be -1 (unowned) or 0..255; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_96203_gate >= 0 && !acphy_96203_live) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_96203_gate=1 requires acphy_96203_live=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_3e2_first && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_3e2_first=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_span_probe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_span_probe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_control_reprobe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_control_reprobe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_control16_reprobe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_control16_reprobe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_upper_probe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_upper_probe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_edge_probe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_edge_probe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_wide_probe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_wide_probe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_edge2_probe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_edge2_probe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_lower_probe && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_lower_probe=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_wrapper_witness && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_wrapper_witness=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_pll_witness && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_pll_witness=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	if (acphy_identity_3e0_first && !acphy_identity_timing_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_3e0_first=1 requires acphy_identity_timing_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	/*
	 * L2k is a scope witness, not another address probe: it must be the first
	 * access after the eight-read block, so sharing a run with any other L2
	 * modifier is rejected before any hardware access.
	 */
	if (acphy_identity_wrapper_witness &&
	    (acphy_identity_3e2_first || acphy_identity_span_probe ||
	     acphy_identity_control_reprobe || acphy_identity_control16_reprobe ||
	     acphy_identity_upper_probe || acphy_identity_edge_probe ||
	     acphy_identity_wide_probe || acphy_identity_edge2_probe ||
	     acphy_identity_lower_probe || acphy_identity_pll_witness ||
	     acphy_identity_3e0_first)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_wrapper_witness=1 is exclusive with every other acphy_identity_* probe modifier; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	/*
	 * L2l has the same shape: it must be the first access after the block, and
	 * it replaces the legacy 0x3e0 read, so it cannot share a run either.
	 */
	if (acphy_identity_pll_witness &&
	    (acphy_identity_3e2_first || acphy_identity_span_probe ||
	     acphy_identity_control_reprobe || acphy_identity_control16_reprobe ||
	     acphy_identity_upper_probe || acphy_identity_edge_probe ||
	     acphy_identity_wide_probe || acphy_identity_edge2_probe ||
	     acphy_identity_lower_probe || acphy_identity_wrapper_witness ||
	     acphy_identity_3e0_first)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_pll_witness=1 is exclusive with every other acphy_identity_* probe modifier; configuration rejected before any hardware access\n");
		return -EINVAL;
	}
	/*
	 * T4 removes the bring-up block itself: it must be the run's first D11
	 * traffic, so sharing a run with any other acphy_identity_* modifier is
	 * rejected before any hardware access.
	 */
	if (acphy_identity_3e0_first &&
	    (acphy_identity_3e2_first || acphy_identity_span_probe ||
	     acphy_identity_control_reprobe || acphy_identity_control16_reprobe ||
	     acphy_identity_upper_probe || acphy_identity_edge_probe ||
	     acphy_identity_wide_probe || acphy_identity_edge2_probe ||
	     acphy_identity_lower_probe || acphy_identity_wrapper_witness ||
	     acphy_identity_pll_witness)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": acphy_identity_3e0_first=1 is exclusive with every other acphy_identity_* probe modifier; configuration rejected before any hardware access\n");
		return -EINVAL;
	}

	/*
	 * The P1 switches are modifiers of the attach-prefix mode, not modes: they
	 * are refused without it, before any allocation or hardware access.
	 */
	if (p1_shm_roundtrip && !acphy_pre_radio_test_only) {
		dev_err(&core->dev,
			OB_DRV_NAME ": p1_shm_roundtrip=1 requires acphy_pre_radio_test_only=1; configuration rejected before any hardware access\n");
		return -EINVAL;
	}

	/* Explicit mode policy: at most one isolated mode may be selected. */
	mode = ob_isolated_mode_select26(fw_validate_only, ucode_test_only,
					 initvals_test_only, dma_test_only,
					 d11_tail_test_only,
					 bsinitvals_test_only,
					 radio_id_probe_only,
					 rev42_diag_probe_only,
					 rev42_attach_probe_only,
					 rev42_sb_reset_only,
					 rev42_ai_probe_only,
					 rev42_ai_enable_only,
					 radio2069_rev4_probe_only,
					 d11_transaction_timing_only,
					 acphy_pre_radio_test_only,
					 acphy_class0_radio_local_0413_test_only,
					 acphy_class0_post0413_phase1_0413_test_only,
					 acphy_class0_postphase1_phase2_0413_test_only,
					 acphy_class0_postphase2_sub9591e_return_test_only,
					 acphy_class0_postbmac_phyreg1b0_branch_gate_test_only,
					 acphy_radio_on_test_only,
					 acphy_radio_postlpf_test_only,
					 rev42_ai_wrapper_only,
					 rev42_d11_maccontrol_once,
					 acphy_identity_timing_only,
					 fw_chain_test_only);
	if (acphy_cal_inputs_probe_only) {
		if (mode != OB_ISOLATED_NONE)
			mode = OB_ISOLATED_CONFLICT;
		else
			mode = OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE;
	}
	if (ob_isolated_mode_conflict(mode)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": isolated modes are mutually exclusive\n");
		return -EINVAL;
	}
	if (sprom_evidence_only && mode != OB_ISOLATED_NONE) {
		dev_err(&core->dev,
			OB_DRV_NAME ": sprom_evidence_only conflicts with other modes\n");
		return -EINVAL;
	}
	if (sflags_diag_only &&
	    (mode != OB_ISOLATED_NONE || sprom_evidence_only)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": sflags_diag_only conflicts with other diagnostic modes\n");
		return -EINVAL;
	}
	if (bringup_test_only &&
	    (mode != OB_ISOLATED_NONE || sprom_evidence_only || sflags_diag_only)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": bringup_test_only is exclusive with every isolated diagnostic mode\n");
		return -EINVAL;
	}

	/*
	 * Phase 37A default-load fail-closed gate.
	 *
	 * No isolated diagnostic flag means the operator issued a bare load that
	 * would reach the ordinary ob_si_probe() production bring-up (PHY,
	 * radio, calibration, DMA, IRQ, mac80211). On this experimental BCM4352
	 * driver that is not an admitted default: refuse with -EOPNOTSUPP before
	 * devm_kzalloc(), before bcma_set_drvdata(), before any driver-initiated
	 * MMIO and before every dispatch branch. The production path remains in
	 * ob_probe() unchanged but is unreachable without a separate
	 * production-readiness decision.
	 *
	 * The two read-only diagnostics that do not select an OB_ISOLATED_* mode
	 * (sprom_evidence_only, sflags_diag_only) are deliberately still admitted.
	 */
	if (!bringup_test_only &&
	    ob_isolated_default_load_refused(mode, sprom_evidence_only,
					     sflags_diag_only)) {
		dev_err(&core->dev,
			OB_DRV_NAME ": no isolated diagnostic mode selected; refusing the default production load with -EOPNOTSUPP before any allocation or hardware access. Select an explicit isolated mode.\n");
		return -EOPNOTSUPP;
	}

	hw = devm_kzalloc(&core->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;
	mutex_init(&hw->rx_lifecycle_lock);

	hw->core = core;
	hw->dev = &core->dev;
	hw->bus = core->bus;
	hw->profile = profile;
	hw->chip_id = core->bus->chipinfo.id;
	hw->chip_rev = core->bus->chipinfo.rev;
	hw->bringup_test_only = bringup_test_only;
	/* The SHM witness exists only on the attach-prefix mode; the param is
	 * already validated against it above. Bit 0x4 is part of the default
	 * prefix and needs no field. */
	if (mode == OB_ISOLATED_ACPHY_PRE_RADIO_TEST)
		hw->p1_shm_roundtrip = p1_shm_roundtrip;
	if (mode == OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST)
		hw->acphy_class0_post0413_phase1_0413_test_only = true;
	if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST)
		hw->acphy_class0_postphase1_phase2_0413_test_only = true;
	if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST)
		hw->acphy_class0_postphase2_sub9591e_return_test_only = true;
	if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST)
		hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only = true;
	if (mode == OB_ISOLATED_ACPHY_RADIO_ON_TEST)
		hw->acphy_radio_on_test_only = true;
	if (mode == OB_ISOLATED_ACPHY_RADIO_POSTLPF_TEST)
		hw->acphy_radio_postlpf_test_only = true;
	if (mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE)
		hw->acphy_cal_inputs_probe_only = true;
	hw->fw_chain_test_only = mode == OB_ISOLATED_FW_CHAIN_TEST;
	hw->acphy_cal_stage_witness =
		mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE &&
		acphy_cal_stage_witness;
	hw->phy_tripoint_probe =
		mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE && phy_tripoint_probe;
	if (hw->phy_tripoint_probe)
		dev_info(hw->dev,
			 "TRIPOINT_ARM points=%u regs=%u writes=0 predicate=none\n",
			 (unsigned int)OB_PHY_TRIPOINT_POINTS,
			 (unsigned int)OB_PHY_TRIPOINT_REGS);
	hw->acphy_cal_write_stage =
		mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE &&
		acphy_cal_write_stage;
	hw->acphy_cal_w2_replay =
		mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE &&
		acphy_cal_w2_replay;
	/* W96: devm_kzalloc zeroes the struct, and gate 0 would read as owned. */
	hw->acphy_96203_gate = 0xffffu;
	if (mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE && acphy_96203_live) {
		hw->acphy_96203_live = true;
		hw->acphy_96203_gate = acphy_96203_gate < 0 ? 0xffffu :
			(u16)acphy_96203_gate;
	}
	hw->acphy_a7089_cal_continue = acphy_a7089_cal_continue;
	hw->d3a1_fifo530_continue = d11_fifo530_continue;
	hw->acphy_a7089_golden_unit = acphy_a7089_golden_unit;
	hw->acphy_a7089_golden_cap = acphy_a7089_golden_cap;
	hw->acphy_table_probe = acphy_table_probe;

	bcma_set_drvdata(core, hw);

	dev_info(hw->dev,
		 OB_DRV_NAME ": chip 0x%04x rev %u, d11 core rev %u\n",
		 hw->chip_id, hw->chip_rev, core->id.rev);

	/* The BCMA probe supplied the selected D11 core and its enumerated wrapper.
	 * This isolated read is deliberately before power-up and every normal
	 * initialization stage; it observes the wrapper, not a vendor dma_attach
	 * return value.
	 */
	if (sflags_diag_only) {
		hw->sflags_diag_only = true;
		ret = ob_si_sflags_evidence(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * sprom_evidence_only: bind, then run ONLY the read-only external-SPROM
	 * diagnostic and return. Reuses ob_si_sprom_diag(); no power-up, no
	 * D11/clock write, no firmware/PSM/DMA/IRQ/PHY/MAC/mac80211.
	 */
	if (sprom_evidence_only) {
		hw->sprom_evidence_only = true;
		ret = ob_si_sprom_evidence(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * fw_validate_only: BCMA has bound the device and the identity check
	 * above passed. Validate the rev42 images and return. ob_fw_probe() only
	 * calls request_firmware()/release_firmware() and the size-bounded
	 * parsers; it never touches an OpenBRCM register. No mac80211.
	 */
	if (mode == OB_ISOLATED_FW_VALIDATE) {
		hw->validate_only = true;
		ret = ob_fw_probe(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		dev_info(hw->dev,
			 "fw: validation-only complete; hardware bring-up skipped\n");
		return 0;
	}

	/*
	 * ucode_test_only: isolated D11 ucode upload + PSM start. Performs only
	 * the minimum core prep and the recovered upload/start sequence, then
	 * returns before any normal OpenBRCM bring-up (no ob_si_probe, no DMA,
	 * no IRQ, no RX, no mac80211).
	 */
	if (mode == OB_ISOLATED_UCODE_TEST) {
		hw->ucode_test_only = true;
		ret = ob_ucode_test(hw);
		/*
		 * D5: a failure after a hardware mutation is RETAINED, not
		 * returned. Only a mutation-free failure releases the device.
		 */
		if (ob_d2a_retain_failure(hw, ret))
			return 0;
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * initvals_test_only: isolated D11 common-initvals test. Runs the proven
	 * D2A core, applies the 610 common-initvals records and verifies the
	 * deterministic postconditions, then returns before any normal OpenBRCM
	 * bring-up (no ob_si_probe, no bsinitvals/PHY/radio, no DMA, no IRQ, no
	 * RX, no mac80211).
	 */
	if (mode == OB_ISOLATED_INITVALS_TEST) {
		hw->initvals_test_only = true;
		ret = ob_initvals_test(hw);
		/* Shares the D2A core, so it shares the D2A retention rule (D5). */
		if (ob_d2a_retain_failure(hw, ret))
			return 0;
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * dma_test_only: isolated D3A0 DMA lifecycle test. Runs the proven D2B
	 * prefix, the pinned D11/clock/IRQ-source prerequisites, the four TX +
	 * FIFO0 RX DMA lifecycle, validation, mandatory verified quiesce and safe
	 * free. It is not a full vendor-prefix reproduction (see
	 * docs/d3a0_dma_test_design.md §0) and never reaches band
	 * init/bsinitvals/PHY/radio/channel, never enables EN_MAC or the host IRQ
	 * route, and never registers mac80211. On success all DMA resources are
	 * already released; @remove is still consulted for the fail-closed fatal
	 * state.
	 */
	if (mode == OB_ISOLATED_DMA_TEST) {
		hw->dma_test_only = true;
		ret = ob_d3a0_test(hw);
		if (hw->d3a0.lc.fatal) {
			/*
			 * Fail-closed: the DMA engine could not be verified
			 * stopped. Keep probe SUCCESSFUL so the device stays
			 * bound and @hw (devres) is retained -- the fatal state
			 * and the retained DMA memory must not silently
			 * disappear through a failed probe/unbind. ob_d3a0_test()
			 * latched a per-device re-entry block and pinned the
			 * module; only a reboot clears this device's record.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": dma-test FATAL unverified quiesce; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			/* No live DMA resources (teardown ran or none created). */
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * d11_tail_test_only: isolated D3A1 vendor post-common / pre-PHY D11
	 * tail test. Runs the proven D2A/D2B core, the exact rev42 tail
	 * (sub_67efd, T1, DMA in vendor position, T2, switch_macfreq), the
	 * deterministic postconditions and the mandatory verified quiesce, then
	 * STOPS before sub_6656c/bsinitvals/PHY/radio/channel. It never
	 * registers mac80211 and never enables EN_MAC or the host IRQ route.
	 */
	if (mode == OB_ISOLATED_D3A1_TEST) {
		hw->d3a1_test_only = true;
		/*
		 * Minimal board-data preparation (ChipCommon pointer + validated
		 * external-SPROM MAC) BEFORE D2A/D2B. It must not run the
		 * normal ob_si_probe() path.
		 */
		ret = ob_si_prepare_board_data_for_d3a1(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_d3a1_test(hw);
		if (hw->d3a0.lc.fatal) {
			/* Fail-closed: keep the device bound and the DMA memory
			 * retained; ob_d3a1_test latched a per-device re-entry
			 * block and pinned the module. Only a reboot clears it.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": d3a1-test FATAL unverified quiesce; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * bsinitvals_test_only: isolated D3B band-init test. Runs the proven
	 * D2A/D2B core and the exact D3A1 vendor prefix with the D3A0 DMA engines
	 * left LIVE (vendor order T1 -> DMA -> T2), then the exact sub_6656c MHF
	 * writes + d11ac1bsinitvals42 (73 records), the deterministic
	 * postconditions and the mandatory verified quiesce, then STOPS before
	 * wlc_phy_init/PHY/radio/channel. It never registers mac80211 and never
	 * enables EN_MAC or the host IRQ route.
	 */
	if (mode == OB_ISOLATED_D3B_TEST) {
		hw->bsinitvals_test_only = true;
		hw->d3b_boundary_witness = d3b_boundary_witness;
		hw->d3b_witness_v2 = d3b_witness_v2;
		/*
		 * Minimal board-data preparation (ChipCommon pointer + validated
		 * external-SPROM MAC and rev11 board fields) BEFORE D2A/D2B. It
		 * must not run the normal ob_si_probe() path.
		 */
		ret = ob_si_prepare_board_data_for_d3b(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_d3b_test(hw);
		if (hw->d3a0.lc.fatal) {
			/* Fail-closed: keep the device bound and the DMA memory
			 * retained; ob_d3b_test latched a per-device re-entry
			 * block and pinned the module. Only a reboot clears it.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": d3b-test FATAL unverified quiesce; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * radio_id_probe_only: isolated BCM2069 radio identity probe. Runs the
	 * minimum proven core prep, reads radio reg 0/1 through the D11 radio
	 * indirect window, decodes the revision class and STOPS before any
	 * PLL/radio/PHY/calibration/channel/DMA/IRQ/mac80211. It never runs the
	 * normal ob_si_probe() path, never uploads ucode, never applies
	 * initvals and never touches DMA.
	 */
	if (mode == OB_ISOLATED_RADIO_ID_PROBE) {
		hw->radio_id_probe_only = true;
		ret = ob_radio_probe_test(hw);
		if (hw->dev_lost || hw->d3a0.lc.fatal) {
			/* Fail-closed: keep the device bound and the module
			 * pinned; no teardown and no MMIO after device loss.
			 * Only a reboot clears the latch.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": radio-probe FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * rev42_diag_probe_only: staged rev42 diagnostic to a stable radio-ID
	 * read. Runs the minimum proven core prep, an optional hardware-proven
	 * D2A/D2B prefix (rev42_diag_full), the CONFIRMED vendor PHY prerequisite
	 * (wlc_phy_anacore direct side) and the radio window read, recording
	 * checkpoints CP0..CP6, then STOPS before band init/PHY init/radio config/
	 * EN_MAC/DMA/IRQ/mac80211. It never runs the normal ob_si_probe() path.
	 */
	if (mode == OB_ISOLATED_REV42_DIAG) {
		hw->rev42_diag_probe_only = true;
		hw->rev42_diag_full = rev42_diag_full;
		ret = ob_diag_test(hw);
		if (hw->dev_lost || hw->d3a0.lc.fatal) {
			/* Fail-closed: keep the device bound and the module
			 * pinned; no teardown and no MMIO after device loss.
			 * Only a reboot clears the latch.
			 */
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-diag FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * rev42_attach_probe_only: cold-attach radio sequence probe. Reproduces
	 * the vendor wlc_bmac_corereset/phy_reset/SITE A/SITE B/radio-window
	 * sequence with checkpoints CP0..CP15, then STOPS before band init/PHY
	 * init/radio config/cal/EN_MAC/DMA/IRQ/mac80211.
	 */
	if (mode == OB_ISOLATED_REV42_ATTACH) {
		hw->rev42_attach_probe_only = true;
		ret = ob_attach_test(hw);
		if (hw->dev_lost || hw->d3a0.lc.fatal) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-attach FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * rev42_sb_reset_only: vendor sb_core_reset cold-path probe. Runs the
	 * recovered Broadcom wrapper reset sequence (SB+0x0F98 etc.) instead of
	 * bcma_core_enable, reads only wrapper/SB registers until si_iscoreup is
	 * true, then a single first D11 functional read, and STOPS before PHY
	 * reset/SITE A/radio.
	 */
	if (mode == OB_ISOLATED_REV42_SB_RESET) {
		hw->rev42_sb_reset_only = true;
		ret = ob_sb_reset_test(hw);
		if (hw->dev_lost || hw->d3a0.lc.fatal) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-sb FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * rev42_ai_probe_only: read-only AI-wrapper validation. Reads the AI
	 * wrapper window (core->wrap) and the core window (core->addr) through
	 * the BCMA accessors, and reads the BAR0 window registers back for
	 * coherency evidence only. It writes no window register and maps no
	 * BAR of its own; never resets the core or touches D11 functional/
	 * PHY/radio/ucode/initvals/DMA/IRQ/mac80211.
	 */
	/*
	 * Phase 36Z: bounded AI-wrapper-only observation. Three reads at most,
	 * all in the BCMA-selected wrapper window; no functional-core access in
	 * either core state, no config readback, no write of its own. A
	 * suspicious (all-ones) wrapper read latches hw->dev_lost inside the
	 * diagnostic, so the retention rule below keeps the device bound instead
	 * of attempting cleanup.
	 */
	if (mode == OB_ISOLATED_REV42_AI_WRAPPER_ONLY) {
		hw->rev42_ai_wrapper_only = true;
		ret = ob_ai_wrapper_only_test(hw);
		if (hw->dev_lost || hw->d3a0.lc.fatal) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-ai-wrapper FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * RETIRED (Phase 36Z): unreachable by construction — the quarantine gate
	 * at the top of ob_probe() already rejected this option with
	 * -EOPNOTSUPP before devm_kzalloc(). This branch is the SECOND,
	 * independent barrier: the entry point it calls performs no register
	 * access (no hw->core dereference, no accessor), so even if the first
	 * gate were removed this path would still return -EOPNOTSUPP without
	 * touching hardware and without falling through to ob_si_probe().
	 */
	if (mode == OB_ISOLATED_REV42_AI_PROBE) {
		hw->rev42_ai_probe_only = true;
		ret = ob_ai_probe_test(hw);
		dev_err(hw->dev,
			OB_DRV_NAME ": rev42_ai_probe_only is retired; refusing before hardware access (-EOPNOTSUPP)\n");
		bcma_set_drvdata(core, NULL);
		return ret ? ret : -EOPNOTSUPP;
	}

	/*
	 * rev42_ai_enable_only: the bounded AI-wrapper core-enable transition.
	 * Upstream bcma_core_enable() with flags=0, entirely in the BCMA wrapper
	 * window: IOCTL=0x3, RESET_CTL=0x0, IOCTL=0x1, each validated by its own
	 * documented readback. Phase 37B removed the D11 0x0FFC probe read: this
	 * mode now stops at the wrapper boundary in every branch, including a
	 * successful core-up. No window write of its own, no iomap, no poll, no
	 * retry; never touches D11 functional/PHY/radio/ucode/initvals/DMA/IRQ/
	 * mac80211.
	 */
	if (mode == OB_ISOLATED_REV42_AI_ENABLE) {
		hw->rev42_ai_enable_only = true;
		ret = ob_ai_enable_test(hw);
		/*
		 * Retention, not cleanup. A latch, or any state reached after the
		 * first wrapper write, has no teardown contract: keep the device
		 * bound and return without releasing anything or falling into any
		 * other stage. Only a write-free failure may surface as a probe
		 * error.
		 *
		 * Phase 38A (Task C): these are three DIFFERENT conditions and each
		 * now reports itself. The previous code printed one shared
		 * "FATAL/dev-lost/post-write state" string for all of them, so a
		 * successful wrapper-core-up that is merely retained looked like a
		 * device-loss fault. Retention is the designed post-write state, not
		 * a failure, and it is never reported as device loss.
		 */
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-ai-en DEVICE_LOSS_SUSPECTED (hw->dev_lost latched); device kept bound, no cleanup, reboot required\n");
			return 0;
		}
		if (hw->d3a0.lc.fatal) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-ai-en FATAL_LIFECYCLE_STATE (d3a0 fatal latch); device kept bound, no cleanup, reboot required\n");
			return 0;
		}
		if (hw->ai_enable_retain) {
			dev_emerg(hw->dev,
				  OB_DRV_NAME ": rev42-ai-en POST_WRITE_RETAINED classification=%s core_up=%d; wrapper transition finished, no device loss; device kept bound, no teardown, reboot to recover\n",
				  hw->ai_enable_core_up ?
				  "D11_WRAPPER_CORE_UP" :
				  "NOT_CORE_UP",
				  hw->ai_enable_core_up ? 1 : 0);
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * rev42_d11_maccontrol_once (Phase 38A): the verified Phase 37B wrapper
	 * transition followed by exactly ONE D11 functional-core MACCONTROL
	 * (0x0120) read. Contract and host-tested classification:
	 * src/ob_d11_maccontrol_once.h. The entry point reports success after any
	 * wrapper write attempt so no probe-failure cleanup becomes reachable;
	 * only a write-free wrapper rejection can surface here as an error.
	 */
	if (mode == OB_ISOLATED_REV42_D11_MACCONTROL_ONCE) {
		hw->rev42_d11_maccontrol_once = true;
		ret = ob_d11_maccontrol_once_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-d11-mc DEVICE_LOSS_SUSPECTED (hw->dev_lost latched by the functional read); device kept bound, no cleanup, reboot required\n");
			return 0;
		}
		if (hw->d3a0.lc.fatal) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": rev42-d11-mc FATAL_LIFECYCLE_STATE (d3a0 fatal latch); device kept bound, no cleanup, reboot required\n");
			return 0;
		}
		if (hw->ai_enable_retain) {
			dev_emerg(hw->dev,
				  OB_DRV_NAME ": rev42-d11-mc POST_WRITE_RETAINED core_up=%d; wrapper write issued, device kept bound, no teardown, reboot to recover\n",
				  hw->ai_enable_core_up ? 1 : 0);
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	if (mode == OB_ISOLATED_RADIO2069_REV4_PROBE ||
	    mode == OB_ISOLATED_D11_TRANSACTION_TIMING)
		/* These bounded modes do not consume the post-power-up direct
		 * SPROM reread. */
		hw->radio2069_rev4_probe_only =
			mode == OB_ISOLATED_RADIO2069_REV4_PROBE;
	if (mode == OB_ISOLATED_D11_TRANSACTION_TIMING)
		hw->d11_transaction_timing_only = true;
	/* L2b is carried as a plain expression, not as another braced block for
	 * this mode: the structural host test keys on the first braced match in
	 * this file and expects the L2 stop block to be it. */
	hw->acphy_identity_3e2_first =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_3e2_first;
	hw->acphy_identity_span_probe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_span_probe;
	hw->acphy_identity_control_reprobe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_control_reprobe;
	hw->acphy_identity_control16_reprobe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_control16_reprobe;
	hw->acphy_identity_upper_probe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_upper_probe;
	hw->acphy_identity_edge_probe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_edge_probe;
	hw->acphy_identity_wide_probe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_wide_probe;
	hw->acphy_identity_edge2_probe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_edge2_probe;
	hw->acphy_identity_lower_probe =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_lower_probe;
	hw->acphy_identity_wrapper_witness =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_wrapper_witness;
	hw->acphy_identity_pll_witness =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_pll_witness;
	hw->acphy_identity_3e0_first =
		mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_3e0_first;
	if (mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING)
		hw->acphy_identity_timing_only = true;

	ret = ob_si_probe(hw);
	if (ob_post0413_probe_failure(hw, "BUS_SI", ret))
		return 0;
	if (ret)
		return ret;
	if (mode == OB_ISOLATED_D11_TRANSACTION_TIMING) {
		dev_info(hw->dev,
			 "D11_TRANSACTION_TIMING_COMPLETE: stopped before firmware/ucode/radio\n");
		return 0;
	}
	if (mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING) {
		if (!hw->acphy_identity_early_stop)
			dev_info(hw->dev, "ACPHY_IDENTITY_TIMING_COMPLETE: stopped before radio prepare/attach identity/firmware\n");
		return 0;
	}

	/* Attach reset and both identities must precede any ucode hardware work. */
	ret = ob_acphy_attach_identity(hw);
	if (ob_post0413_probe_failure(hw, "ATTACH_IDENTITY", ret))
		return 0;
	if (ret)
		return ret;
	/*
	 * W2 identity gate: profile chip/D11 plus the runtime PHY/radio identity
	 * published by attach. The same gate is repeated inside ob_fw_chain_run()
	 * so no future caller can skip it; here it ensures request_firmware() is
	 * never reached on an unvalidated identity.
	 */
	ret = ob_fw_chain_identity_gate(hw, true);
	if (ret)
		return ret;
	/*
	 * M3.4D1: acquire and validate the exact vendor rev42 firmware through
	 * request_firmware(). Acquisition + dry-run only — no hardware writes.
	 * Present-but-invalid firmware fails probe; absent firmware is reported
	 * but does not disturb the validated SPROM/MAC/DMA/IRQ paths.
	 */
	ret = ob_fw_probe(hw);
	if (ob_post0413_probe_failure(hw, "FIRMWARE", ret))
		return 0;
	if (ret)
		return ret;
	/*
	 * W2 isolated chain test: the exact production chain, stopped at
	 * POST_UCODE_READY.  A retained failure returns probe success so the
	 * device stays bound; no teardown is allowed after the first mutation.
	 */
	if (mode == OB_ISOLATED_FW_CHAIN_TEST) {
		ret = ob_fw_chain_test(hw);
		if (hw->ucode_retain)
			return 0;
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}
	/* M3.4D post-ucode bootstrap.  This is the single normal-path call;
	 * it stops before PHY/radio/DMA/IRQ/mac80211. */
	ret = ob_d11_post_ucode_init(hw);
	if (ob_post0413_probe_failure(hw, "POST_UCODE", ret))
		return 0;
	/* A post-D2A failure is retained: keep probe successful and the device bound. */
	if (hw->ucode_retain)
		return 0;
	if (ret)
		return ret;
	/*
	 * P1 of the tripoint witness: the post-ucode state, before the radio
	 * prefix and the mirror. Read-only; any callback error is fail-stop.
	 */
	if (hw->phy_tripoint_probe) {
		ret = ob_phy_tripoint_observe(hw, OB_PHY_TRIPOINT_P1);
		if (ret) {
			if (hw->dev_lost) {
				dev_crit(hw->dev,
					 OB_DRV_NAME ": tripoint P1 FATAL/dev-lost; device kept bound, reboot required\n");
				return 0;
			}
			dev_info(hw->dev, "TRIPOINT_STOP point=P1 ret=%d\n", ret);
			bcma_set_drvdata(core, NULL);
			return ret;
		}
	}
	if (mode == OB_ISOLATED_ACPHY_PRE_RADIO_TEST) {
		hw->acphy_pre_radio_test_only = true;
		ret = ob_acphy_pre_radio_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-pre-radio FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}
	if (mode == OB_ISOLATED_ACPHY_CLASS0_0413_TEST) {
		hw->acphy_class0_radio_local_0413_test_only = true;
		/* Reuse the established owner that performs the pre-radio
		 * transaction, validates its postconditions, and publishes
		 * OB_ACPHY_START_READY_FOR_RADIO_ON.  The class-0 checkpoint must
		 * never manufacture or bypass that lifecycle state. */
		ret = ob_acphy_pre_radio_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-class0-0413 pre-radio FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_radio_local_0413_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-class0-0413 FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret)
			return ret;
		return 0;
	}
	if (mode == OB_ISOLATED_ACPHY_CLASS0_POST0413_TEST) {
		ret = ob_acphy_pre_radio_test(hw);
		if (hw->dev_lost) {
			dev_info(hw->dev,
				 "ACPHY_CLASS0_POST0413_FAIL stage=DEVICE_LOST ret=-5\n");
			dev_info(hw->dev, "ACPHY_CLASS0_POST0413_STOP\n");
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-post0413 pre-radio FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			dev_info(hw->dev,
				 "ACPHY_CLASS0_POST0413_FAIL stage=PRE_RADIO ret=%d\n",
				 ret);
			dev_info(hw->dev, "ACPHY_CLASS0_POST0413_STOP\n");
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_radio_class0_post0413_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-post0413 FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret)
			return ret;
		return 0;
	}
	if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE1_TEST ||
	    mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST ||
	    mode == OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST) {
		ret = ob_acphy_pre_radio_test(hw);
		if (hw->dev_lost) {
			dev_info(hw->dev,
				 "ACPHY_CLASS0_POSTPHASE1_FAIL stage=DEVICE_LOST ret=-5\n");
			dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_STOP\n");
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-postphase1 pre-radio FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			dev_info(hw->dev,
				 "ACPHY_CLASS0_POSTPHASE1_FAIL stage=PRE_RADIO ret=%d\n",
				 ret);
			dev_info(hw->dev, "ACPHY_CLASS0_POSTPHASE1_STOP\n");
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTPHASE2_RETURN_TEST)
			ret = ob_radio_class0_postphase2_return_test(hw);
		else if (mode == OB_ISOLATED_ACPHY_CLASS0_POSTBMAC_TEST)
			ret = ob_radio_class0_postbmac_branch_gate_test(hw);
		else
			ret = ob_radio_class0_postphase1_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev, OB_DRV_NAME ": %s FATAL/dev-lost; device kept bound, reboot required\n",
				 hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only ?
				 "acphy-postbmac-branch-gate" :
				 hw->acphy_class0_postphase2_sub9591e_return_test_only ?
				 "acphy-postphase2-return" : "acphy-postphase1-phase2");
			return 0;
		}
		if (ret)
			return ret;
		return 0;
	}
	if (mode == OB_ISOLATED_ACPHY_RADIO_ON_TEST) {
		ret = ob_acphy_pre_radio_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-radio-on pre-radio FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_acphy_radio_on_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-radio-on FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}
	if (mode == OB_ISOLATED_ACPHY_RADIO_POSTLPF_TEST) {
		ret = ob_acphy_pre_radio_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-postlpf pre-radio FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_acphy_radio_postlpf_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-postlpf FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}
	if (mode == OB_ISOLATED_ACPHY_CAL_INPUTS_PROBE) {
		ret = ob_acphy_pre_radio_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-cal-inputs pre-radio FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_acphy_radio_postlpf_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-cal-inputs FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		ret = ob_acphy_calibration_inputs_probe(hw);
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		if (hw->acphy_cal_w2_replay) {
			/*
			 * W2: calibration-entry replay on the same admitted
			 * contract.  The bracket stays open across the kick and
			 * its poll; any callback failure is fail-stop.
			 */
			ret = ob_acphy_calibration_w2_replay(hw);
			if (hw->dev_lost) {
				dev_crit(hw->dev,
					 OB_DRV_NAME ": acphy-cal-w2 FATAL/dev-lost; device kept bound, reboot required\n");
				return 0;
			}
			if (ret) {
				bcma_set_drvdata(core, NULL);
				return ret;
			}
			return 0;
		}
		if (hw->acphy_cal_write_stage) {
			/*
			 * M1-4: first calibration write arm on the same admitted
			 * contract.  Value-identity by construction; any callback
			 * failure is fail-stop.
			 */
			ret = ob_acphy_calibration_write_stage(hw);
			if (hw->dev_lost) {
				dev_crit(hw->dev,
					 OB_DRV_NAME ": acphy-cal-write FATAL/dev-lost; device kept bound, reboot required\n");
				return 0;
			}
			if (ret) {
				bcma_set_drvdata(core, NULL);
				return ret;
			}
			return 0;
		}
		if (!hw->acphy_cal_stage_witness)
			return 0;
		/*
		 * M1-3: read-only pre-state witness on the same admitted contract.
		 * Any callback failure is fail-stop; expected stops return 0.
		 */
		ret = ob_acphy_calibration_stage_witness(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": acphy-cal-stage FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}
	if (mode == OB_ISOLATED_RADIO2069_REV4_PROBE) {
		ret = ob_radio2069_rev4_probe_test(hw);
		if (hw->dev_lost) {
			dev_crit(hw->dev,
				 OB_DRV_NAME ": radio2069-rev4 FATAL/dev-lost; device kept bound, reboot required\n");
			return 0;
		}
		if (ret) {
			bcma_set_drvdata(core, NULL);
			return ret;
		}
		return 0;
	}

	/*
	 * M3.2: allocate the DMA64 descriptor rings (software model only). The
	 * D11 DMA register blocks are left untouched and no engine is enabled.
	 */
	ret = ob_dma_init(hw);
	if (ret)
		return ret;

	/*
	 * Register the shared IRQ handler with D11 service explicitly gated.
	 * A hard-IRQ invocation before first-up returns before any D11 access.
	 */
	ret = ob_irq_init(hw);
	if (ret) {
		ob_dma_free(hw);
		return ret;
	}

	/*
	 * Keep probe-time RX activation fail-closed. RX is not routed or enabled by
	 * normal probe; the diagnostic path remains explicitly separate.
	 */

	ret = ob_mac80211_register(hw);
	if (ret) {
		ob_irq_free(hw);
		ob_dma_free(hw);
		return ret;
	}

	return 0;
}

void ob_remove(struct bcma_device *core)
{
	struct ob_hw *hw = bcma_get_drvdata(core);
	int ret;

	if (!hw)
		return;

	/* W2: the chain test owns no platform resources and never tears down. */
	if (hw->fw_chain_test_only) {
		if (hw->ucode_retain)
			dev_emerg(hw->dev,
				  OB_DRV_NAME ": removed while fw-chain RETAINED stage=%s mutated=%d psm_suspended=%d; no teardown, hardware left as-is, reboot required\n",
				  hw->ucode_class ? hw->ucode_class : "UNKNOWN",
				  hw->ucode_mutated ? 1 : 0,
				  hw->ucode_psm_suspended ? 1 : 0);
		else
			dev_info(hw->dev,
				 OB_DRV_NAME ": removed (fw-chain-test; no resource teardown, hardware left as-is)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	if (hw->sflags_diag_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (SFLAGS diagnostic; no resources initialized)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/*
	 * sprom_evidence_only is read-only and initialized no platform
	 * resources; skip every teardown step.
	 */
	if (hw->sprom_evidence_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (sprom-evidence; nothing to tear down)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/*
	 * fw_validate_only never initialized mac80211, DMA, IRQ or RX. Skip
	 * every teardown step so no uninitialized resource is touched.
	 */
	if (hw->validate_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (validation-only; nothing to tear down)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/*
	 * ucode_test_only never initialized mac80211, IRQ, DMA or RX; skip all
	 * teardown. No MAC/PHY register is restored because no recovery write is
	 * provenance-backed for this partial state (see docs/milestones.md).
	 */
	if (hw->ucode_test_only) {
		/*
		 * Phase 38C (D5/T15): no normal teardown may run on a retained
		 * post-write state. There is no recovery write for a partially
		 * programmed D11, so removal only reports what is being left
		 * behind; unloading does not restore anything.
		 */
		if (hw->ucode_retain)
			dev_emerg(hw->dev,
				  OB_DRV_NAME ": removed while D2A RETAINED class=%s mutated=%d psm_suspended=%d; no teardown, no register restore, hardware left as-is, reboot required\n",
				  hw->ucode_class ? hw->ucode_class : "UNKNOWN",
				  hw->ucode_mutated ? 1 : 0,
				  hw->ucode_psm_suspended ? 1 : 0);
		else
			dev_info(hw->dev,
				 OB_DRV_NAME ": removed (ucode-test; no resource teardown, hardware left as-is)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/*
	 * initvals_test_only never initialized mac80211, IRQ, DMA or RX; skip all
	 * teardown. Like ucode_test_only, no MAC/PHY register is restored because
	 * no recovery write is provenance-backed for this partial state.
	 */
	if (hw->initvals_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (initvals-test; no resource teardown, hardware left as-is)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/*
	 * dma_test_only owns its own DMA lifecycle. ob_d3a0_remove() applies the
	 * fail-closed policy: it never frees DMA memory while hardware may still
	 * consume it, and enters a reboot-required fatal state otherwise.
	 */
	if (hw->dma_test_only) {
		ob_d3a0_remove(hw);
		return;
	}

	/*
	 * d3a1_test_only owns its own DMA lifecycle (shared with D3A0, in the
	 * vendor T1 -> DMA -> T2 position). ob_d3a1_remove() applies the same
	 * fail-closed policy.
	 */
	if (hw->d3a1_test_only) {
		ob_d3a1_remove(hw);
		return;
	}

	/*
	 * bsinitvals_test_only owns its own DMA lifecycle (shared with D3A0, in
	 * the vendor T1 -> DMA -> T2 -> D3B position). ob_d3b_remove() applies
	 * the same fail-closed policy.
	 */
	if (hw->bsinitvals_test_only) {
		ob_d3b_remove(hw);
		return;
	}

	/*
	 * radio_id_probe_only owns no platform resources; its remove hook only
	 * reports the retained (fatal/dev-lost) state or the clean STOP.
	 */
	if (hw->d11_transaction_timing_only || hw->acphy_identity_timing_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (%s; no resource teardown)\n",
			 hw->acphy_identity_timing_only ?
				"acphy identity timing L2" : "D11 transaction timing");
		bcma_set_drvdata(core, NULL);
		return;
	}

	/* No normal resources exist in the bounded ACPHY checkpoint.  A clean
	 * removal deliberately performs no speculative hardware restoration. */
	if (hw->acphy_pre_radio_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-pre-radio-test; hardware left as-is)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_class0_radio_local_0413_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-class0-0413; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_class0_post0413_phase1_0413_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-post0413-phase1; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_class0_postphase1_phase2_0413_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-postphase1-phase2; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_class0_postphase2_sub9591e_return_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-postphase2-return; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-postbmac-branch-gate; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_radio_on_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-radio-on; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_radio_postlpf_test_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-postlpf; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}
	if (hw->acphy_cal_inputs_probe_only) {
		dev_info(hw->dev,
			 OB_DRV_NAME ": removed (acphy-cal-inputs; radio state preserved)\n");
		bcma_set_drvdata(core, NULL);
		return;
	}

	if (hw->radio_id_probe_only || hw->radio2069_rev4_probe_only) {
		ob_radio_remove(hw);
		return;
	}

	/*
	 * rev42_diag_probe_only owns no platform resources; its remove hook only
	 * reports the retained (fatal/dev-lost) state or the clean STOP.
	 */
	if (hw->rev42_diag_probe_only || hw->rev42_attach_probe_only ||
	    hw->rev42_sb_reset_only || hw->rev42_ai_probe_only ||
	    hw->rev42_ai_wrapper_only || hw->rev42_ai_enable_only ||
	    hw->rev42_d11_maccontrol_once) {
		ob_diag_remove(hw);
		return;
	}

	ob_mac80211_unregister(hw);
	/* Quiesce deferred/IRQ access before disable; retain all DMA memory on an
	 * unconfirmed stop rather than freeing memory that hardware may still own.
	 */
	ret = ob_rx_remove_begin(hw);
	ob_irq_free(hw);
	/* A handler already in flight on a device-lost path can only be ruled
	 * out after free_irq(); close that final scheduling window as well.
	 */
	if (hw->rx.initialized)
		tasklet_kill(&hw->rx.tasklet);
	if (!ret) {
		ob_rx_free_buffers(hw);
		ob_dma_free(hw);
	} else {
		/*
		 * Neither buffer nor ring memory is freed here, so the retained
		 * allocation must be transferred to the durable owner before
		 * devres releases @hw. The owner keeps the opaque handles reachable
		 * through the registry and refuses every later release; the module
		 * pin it takes makes rmmod unable to clear that state.
		 */
		ob_dma_owner_retain_m32(hw,
					OB_DMA_OWNER_REASON_REMOVE_RETAINED,
					"ob_remove:rx-quiescence");
		dev_crit(hw->dev,
			 OB_DRV_NAME ": RX DMA resources quarantined after unconfirmed quiescence; retained under a registry-held owner, recovery requires a separately proven reset/stop contract\n");
	}
	/* A D3A0/D3A1/D3B fatal state also retains DMA memory. The per-mode
	 * remove hook normally transfers it; this is the fail-closed backstop for
	 * a fatal latch that happened outside those hooks (for example from an
	 * interrupt-context device-loss observation). It is idempotent.
	 */
	if (hw->d3a0.lc.fatal)
		ob_dma_owner_retain_d3a0(hw,
					OB_DMA_OWNER_REASON_DEVICE_LOST,
					"ob_remove:fatal");
	dev_info(hw->dev, OB_DRV_NAME ": removed\n");
	bcma_set_drvdata(core, NULL);
}

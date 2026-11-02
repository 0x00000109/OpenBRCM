// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host tests for the pure helpers of the staged rev42 diagnostic:
 *   - vendor-faithful radio-ID decode (ob_diag_vendor_decode)
 *   - r0 reconstruction from the two cached fields
 *   - checkpoint sentinel predicate
 *   - the eight-way isolated-mode policy (no conflict / correct selection)
 *   - the eleven-way policy, specifically OB_ISOLATED_REV42_AI_PROBE
 *   - the rev42_diag_full / rev42_diag_probe_only fail-closed coupling
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "../../src/ob_ucode.h"
#include "../../src/ob_diag.h"

static int failures;

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
		failures++; \
	} \
} while (0)

static void test_decode(void)
{
	/* BCM2069-class: R0=0x009e -> id_byte 0x09; R1=0x2069. */
	struct ob_diag_radio d = ob_diag_vendor_decode(0x009e, 0x2069);

	CHECK(d.id32 == 0x009e2069u, "id32 = (r0<<16)|r1");
	CHECK(d.id_byte == 0x09u, "id_byte = r0>>4");
	CHECK(d.low_nibble == 0x0eu, "low_nibble = r0&0xf");
	CHECK(d.low_byte == 0x9eu, "low_byte = r0&0xff");
	CHECK(d.pa_r1 == 0x2069u, "pa_r1 = r1");
	CHECK(d.valid, "live identity is valid");
	CHECK(d.id_in_family, "0x09 is in the PROBABLE family");
	CHECK(!d.all_ones && !d.all_zero, "not all ones/zero");

	/* Upper edge of the PROBABLE family: R0=0x00be -> 0x0b. */
	d = ob_diag_vendor_decode(0x00be, 0x1234);
	CHECK(d.id_byte == 0x0bu, "id_byte 0x0b");
	CHECK(d.id_in_family, "0x0b is in the PROBABLE family");

	/* Outside the family must not be flagged as expected. */
	d = ob_diag_vendor_decode(0x00c0, 0x2069);
	CHECK(d.id_byte == 0x0cu, "id_byte 0x0c");
	CHECK(!d.id_in_family, "0x0c is outside the PROBABLE family");

	/* Dead window. */
	d = ob_diag_vendor_decode(0xffff, 0xffff);
	CHECK(d.all_ones, "all-ones detected");
	CHECK(!d.valid, "all-ones is not a valid identity");

	/* Unwritten window. */
	d = ob_diag_vendor_decode(0x0000, 0x0000);
	CHECK(d.all_zero, "all-zero detected");
	CHECK(!d.valid, "all-zero is not a valid identity");
}

static void test_reconstruct(void)
{
	CHECK(ob_diag_r0_from_fields(0x09, 0x0e) == 0x009eu,
	      "reconstruct r0 = (id<<4)|low_nibble");
	CHECK(ob_diag_r0_from_fields(0x0b, 0x0e) == 0x00beu,
	      "reconstruct r0 (0x0b)");
	CHECK(ob_diag_r0_from_fields(0xff, 0x0f) == 0x00ffu,
	      "reconstruct masks to a nibble");
}

static void test_sentinel(void)
{
	CHECK(ob_diag_sentinel_lost(0xffffffffu), "all-ones sentinel is loss");
	CHECK(!ob_diag_sentinel_lost(0x04020402u), "D2B value is not loss");
	CHECK(!ob_diag_sentinel_lost(0x00000000u), "zero is not loss");
}

static void test_mode_policy(void)
{
	enum ob_isolated_mode m;

	m = ob_isolated_mode_select8(false, false, false, false, false, false,
				     false, false);
	CHECK(m == OB_ISOLATED_NONE, "no mode selected");

	m = ob_isolated_mode_select8(false, false, false, false, false, false,
				     false, true);
	CHECK(m == OB_ISOLATED_REV42_DIAG, "rev42 diag selected");

	m = ob_isolated_mode_select8(true, false, false, false, false, false,
				     false, false);
	CHECK(m == OB_ISOLATED_FW_VALIDATE, "fw validate selected");

	m = ob_isolated_mode_select8(false, false, false, false, false, false,
				     true, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "radio probe + rev42 diag conflict");

	m = ob_isolated_mode_select8(false, true, false, false, false, false,
				     false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ucode test + rev42 diag conflict");

	CHECK(ob_isolated_mode_skips_teardown(OB_ISOLATED_REV42_DIAG),
	      "rev42 diag skips teardown");
	CHECK(!ob_isolated_mode_uses_dma(OB_ISOLATED_REV42_DIAG),
	      "rev42 diag owns no DMA");

	/* Nine-way form: cold-attach probe. */
	m = ob_isolated_mode_select9(false, false, false, false, false, false,
				     false, false, true);
	CHECK(m == OB_ISOLATED_REV42_ATTACH, "rev42 attach selected");
	m = ob_isolated_mode_select9(false, false, false, false, false, false,
				     false, true, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "diag + attach conflict");
	m = ob_isolated_mode_select9(true, false, false, false, false, false,
				     false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "fw validate + attach conflict");
	CHECK(ob_isolated_mode_skips_teardown(OB_ISOLATED_REV42_ATTACH),
	      "rev42 attach skips teardown");
	CHECK(!ob_isolated_mode_uses_dma(OB_ISOLATED_REV42_ATTACH),
	      "rev42 attach owns no DMA");

	/* Ten-way form: vendor sb_core_reset probe. */
	m = ob_isolated_mode_select10(false, false, false, false, false, false,
				      false, false, false, true);
	CHECK(m == OB_ISOLATED_REV42_SB_RESET, "rev42 sb reset selected");
	m = ob_isolated_mode_select10(false, false, false, false, false, false,
				      false, false, true, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "attach + sb reset conflict");
	CHECK(ob_isolated_mode_skips_teardown(OB_ISOLATED_REV42_SB_RESET),
	      "rev42 sb reset skips teardown");
	CHECK(!ob_isolated_mode_uses_dma(OB_ISOLATED_REV42_SB_RESET),
	      "rev42 sb reset owns no DMA");
}

/*
 * A/B/D/E: the AI-wrapper probe must use the already-enumerated BCMA
 * core->addr / core->wrap, must not need a hardcoded D11 selector, must not be
 * able to leave bus->mapped_core stale, and must be gated by dev_lost.
 * F: all of it is pure -- these run on the host with no hardware at all.
 */
static void test_ai_addr_window(void)
{
	/* The register offsets are WINDOW OFFSETS, not backplane addresses. */
	CHECK(OB_DIAG_AI_IOCTL == 0x0408u, "wrapper IOCTL offset 0x408");
	CHECK(OB_DIAG_AI_RESET_CTL == 0x0800u, "wrapper RESET_CTL offset 0x800");
	CHECK(OB_DIAG_AI_RESET_ST == 0x0804u, "wrapper RESET_ST offset 0x804");
	CHECK(OB_DIAG_PROBE_OFF_0FFC == 0x0ffcu, "core 0xffc probe offset");
}

static void test_ai_coherency(void)
{
	/*
	 * The vectors below use the MEASURED D11-ADDR-U1 addresses: the D11
	 * core functional window is core->addr = 0x18001000 and the BCMA
	 * wrapper window is core->wrap = 0x18101000 (run
	 * openbrcm-test-20260927-014301-8836). They used to be 0x18000000 /
	 * 0x18001000, which encoded the since-superseded belief that
	 * 0x18001000 was the wrapper. 0x18100000 is retained below ONLY as a
	 * deliberately WRONG value -- it is not the wrapper base.
	 */
	/* A: core access is bound to core->addr. */
	/* B: wrapper access is bound to core->wrap. */
	/* D: the programmed windows must equal the BCMA-owned values. */
	CHECK(ob_diag_ai_window_coherent(0x18001000u, 0x18001000u,
					 0x18001000u, 0x18001000u),
	      "addr-only probe: windows match BCMA-owned values");
	/* Independent values (the real BCMA shape) are equally coherent. */
	CHECK(ob_diag_ai_window_coherent(0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u),
	      "addr/wrap are independent; both match");
	/* A mismatch in either window is reported, not silently accepted. */
	CHECK(!ob_diag_ai_window_coherent(0x18101000u, 0x18101000u,
					  0x18001000u, 0x18101000u),
	      "wrong core window is not coherent");
	/* 0x18100000 is a plausible-but-wrong wrapper: it was once believed to
	 * BE the wrapper base, so it is the sharper negative test. */
	CHECK(!ob_diag_ai_window_coherent(0x18001000u, 0x18100000u,
					  0x18001000u, 0x18101000u),
	      "wrong wrapper window is not coherent");
}

static void test_ai_iscoreup(void)
{
	struct ob_diag_ai a;

	/* Vendor predicate is the only reachability verdict. */
	CHECK(ob_diag_ai_iscoreup(0x1u, 0x0u), "IOCTL=1 RESET_CTL=0 -> core up");
	CHECK(!ob_diag_ai_iscoreup(0x0u, 0x0u), "IOCTL=0 -> core down");
	CHECK(!ob_diag_ai_iscoreup(0x1u, 0x1u), "RESET_CTL=1 -> core down");
	CHECK(!ob_diag_ai_iscoreup(0x3u, 0x0u), "IOCTL=3 -> core down");

	/* All-ones is an OBSERVATION, never a wrong-window verdict. */
	a = ob_diag_ai_summarise(0xffffffffu, 0xffffffffu, 0xffffffffu,
				 0xffffffffu, 0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0);
	CHECK(a.wrapper_all_ones, "wrapper all-ones observed");
	CHECK(a.core_all_ones, "core all-ones observed");
	CHECK(!a.core_up, "all-ones does not assert core up");
	CHECK(a.coherent, "window coherency is independent of the values read");
}

/*
 * The PCI config readback status (D11-ADDR-C1/C2, blocker
 * d4.rev42.cfg_readback.status).
 *
 * pci_read_config_dword() on failure returns non-zero AND stores
 * PCI_ERROR_RESPONSE (~0) into the out-parameter. The 2026-09-27 run read
 * 0xffffffff from BOTH window registers and the code ignored the status, so
 * the kernel's error sentinel was printed as register content and a bogus
 * "coherent=0" was derived from it. These tests pin the fix: the sentinel must
 * not reach the record, and a failed read must yield UNKNOWN, never 0.
 */
static void test_ai_cfg_readback_status(void)
{
	struct ob_diag_ai ok, bad;

	/* Successful reads: the verdict is the real comparison. */
	CHECK(ob_diag_ai_cfg_ok(0, 0), "both reads succeeded -> cfg_ok");
	CHECK(ob_diag_ai_coherent_verdict(true, true) == 1, "coherent -> 1");
	CHECK(ob_diag_ai_coherent_verdict(true, false) == 0, "incoherent -> 0");

	/* A failed read is not evidence in either direction. */
	CHECK(!ob_diag_ai_cfg_ok(-2, 0), "core read failed -> not cfg_ok");
	CHECK(!ob_diag_ai_cfg_ok(0, -2), "wrap read failed -> not cfg_ok");
	CHECK(!ob_diag_ai_cfg_ok(-2, -2), "both failed -> not cfg_ok");
	CHECK(ob_diag_ai_coherent_verdict(false, false) == -1,
	      "failed read -> UNKNOWN, not 0");
	CHECK(ob_diag_ai_coherent_verdict(false, true) == -1,
	      "failed read -> UNKNOWN even if the raw compare would match");

	/* The sentinel must not survive into the record. This is the exact
	 * shape of the observed run: 0xffffffff in, status ignored. */
	bad = ob_diag_ai_summarise(0x0u, 0x1u, 0x0u, 0xffffffffu,
				   0xffffffffu, 0xffffffffu,
				   0x18001000u, 0x18101000u, -2, -2);
	CHECK(!bad.cfg_ok, "failed readback recorded as not cfg_ok");
	CHECK(bad.win_core == 0 && bad.win_wrap == 0,
	      "PCI_ERROR_RESPONSE is not stored as window content");
	CHECK(!bad.coherent, "coherent is false when the read failed");
	CHECK(ob_diag_ai_coherent_verdict(bad.cfg_ok, bad.coherent) == -1,
	      "the printed verdict is UNKNOWN, not 0");
	/* And it must not have disturbed the real verdicts. */
	CHECK(!bad.core_up, "core_up still evaluated from the predicate");
	CHECK(bad.core_all_ones, "core all-ones still observed as a fact");

	/* A genuinely coherent run is unaffected. */
	ok = ob_diag_ai_summarise(0x1u, 0x0u, 0x0u, 0x0000ffffu,
				  0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0);
	CHECK(ok.cfg_ok && ok.coherent, "successful coherent readback");
	CHECK(ob_diag_ai_coherent_verdict(ok.cfg_ok, ok.coherent) == 1,
	      "coherent reported as 1");

	/* An incoherent SUCCESSFUL read is still reported as 0 -- the fix must
	 * not turn a real mismatch into UNKNOWN. Here the core window matches
	 * and only the wrapper is wrong (0x18100000 != 0x18101000). */
	ok = ob_diag_ai_summarise(0x1u, 0x0u, 0x0u, 0x0u,
				  0x18001000u, 0x18100000u,
				  0x18001000u, 0x18101000u, 0, 0);
	CHECK(ok.cfg_ok && !ok.coherent, "real mismatch still detected");
	CHECK(ob_diag_ai_coherent_verdict(ok.cfg_ok, ok.coherent) == 0,
	      "real mismatch reported as 0, not UNKNOWN");
}

/*
 * The obsolete rule was: repeated wrapper all-ones => AI_WRAPPER_UNREACHABLE.
 * Classification must now depend ONLY on the vendor ai_iscoreup terms, so the
 * same (IOCTL, RESET_CTL) must classify identically no matter which other
 * register happens to read all-ones.
 */
static void test_ai_all_ones_does_not_classify(void)
{
	struct ob_diag_ai live, ones;

	live = ob_diag_ai_summarise(0x1u, 0x0u, 0x11111111u, 0x22222222u,
				    0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0);
	ones = ob_diag_ai_summarise(0x1u, 0x0u, 0xffffffffu, 0xffffffffu,
				    0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0);
	CHECK(live.core_up == ones.core_up,
	      "all-ones in RESET_ST/core does not change core_up");
	CHECK(ones.core_all_ones, "the core all-ones case is still observed");
	CHECK(!ones.wrapper_all_ones,
	      "wrapper_all_ones needs all three wrapper registers all-ones");

	/* And an all-ones IOCTL/RESET_CTL is not "unreachable": it just fails
	 * the predicate, exactly like any other non-matching value. */
	ones = ob_diag_ai_summarise(0xffffffffu, 0xffffffffu, 0xffffffffu, 0x0u,
				    0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0);
	CHECK(ones.wrapper_all_ones, "wrapper flagged all-ones");
	CHECK(!ones.core_up, "all-ones IOCTL/RESET_CTL does not assert core up");
	CHECK(ob_diag_ai_summarise(0x0u, 0x0u, 0xffffffffu, 0xffffffffu,
				   0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0).core_up ==
	      ob_diag_ai_summarise(0x0u, 0x0u, 0x0u, 0x0u,
				   0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0).core_up,
	      "all-ones never manufactures a reachable verdict either");
}

/* dev_lost is dominant: it forbids access regardless of any other state. */
static void test_ai_dev_lost_dominates(void)
{
	struct ob_guard_stat st;

	CHECK(!ob_diag_ai_may_access(true),
	      "dev_lost forbids access even when the predicate would say core up");

	memset(&st, 0, sizeof(st));
	/* ops == NULL would crash if the latch did not suppress the call. */
	CHECK(ob_guard_do_read32(true, &st, NULL, NULL, 0) == OB_GUARD_DEAD32,
	      "guarded 32-bit read returns the dead sentinel after the latch");
	CHECK(st.reads_blocked == 1u && st.reads_issued == 0u,
	      "blocked read is accounted and never issued");

	memset(&st, 0, sizeof(st));
	ob_guard_do_write32(true, &st, NULL, NULL, 0, 0xdeadbeefu);
	CHECK(st.writes_blocked == 1u && st.writes_issued == 0u,
	      "guarded 32-bit write is suppressed after the latch");
	/* The live path is covered exhaustively by tests/host/ob_guard_test.c. */
}

static void test_ai_dev_lost(void)
{
	/* E: after the latch, no D11/BCMA/window access may be issued. */
	CHECK(ob_diag_ai_may_access(false), "access permitted while live");
	CHECK(!ob_diag_ai_may_access(true),
	      "dev_lost forbids any further D11/BCMA/window access");
}

static void test_ai_summarise(void)
{
	struct ob_diag_ai a;

	a = ob_diag_ai_summarise(0x1u, 0x0u, 0x12345678u, 0xdeadbeefu,
				 0x18001000u, 0x18101000u,
				  0x18001000u, 0x18101000u, 0, 0);
	CHECK(a.core_up, "ai_iscoreup recorded");
	CHECK(a.coherent, "coherent recorded");
	CHECK(!a.wrapper_all_ones && !a.core_all_ones, "no all-ones");
	/* The measured D11-ADDR-U1 assignment: core->addr is the D11 core
	 * window, core->wrap is the BCMA wrapper window. Asserting the fields
	 * separately is what stops the two being transposed again. */
	CHECK(a.core_addr == 0x18001000u && a.core_wrap == 0x18101000u,
	      "BCMA-owned addresses carried through for evidence");
	CHECK(a.ioc == 0x1u && a.rctl == 0x0u && a.rst == 0x12345678u &&
	      a.probe_0ffc == 0xdeadbeefu, "raw values carried through");
}

/*
 * Eleven-way form: the read-only AI-wrapper probe.
 *
 * A. rev42_ai_probe_only alone selects OB_ISOLATED_REV42_AI_PROBE.
 * B. it participates in mutual exclusion against every other mode.
 * C. skips_teardown() is true, uses_dma() is false.
 *
 * This is the direct regression for the mutation that deleted
 *     if (rev42_ai_probe_only) return OB_ISOLATED_REV42_AI_PROBE;
 * from ob_isolated_mode_select11(). That deletion made the mode fall through to
 * OB_ISOLATED_NONE, i.e. to the full ob_si_probe() bring-up (PHY, radio,
 * calibration, DMA, IRQ, mac80211) while every other test still passed.
 */
static void test_ai_mode_policy(void)
{
	enum ob_isolated_mode m;

	/* A. alone. */
	m = ob_isolated_mode_select11(false, false, false, false, false, false,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_REV42_AI_PROBE, "rev42 ai probe selected alone");

	/* Nothing selected. */
	m = ob_isolated_mode_select11(false, false, false, false, false, false,
				      false, false, false, false, false);
	CHECK(m == OB_ISOLATED_NONE, "no mode -> OB_ISOLATED_NONE");

	/* B. conflicts with every other isolated mode, one at a time. */
	m = ob_isolated_mode_select11(true, false, false, false, false, false,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + fw_validate conflict");
	m = ob_isolated_mode_select11(false, true, false, false, false, false,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + ucode_test conflict");
	m = ob_isolated_mode_select11(false, false, true, false, false, false,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + initvals_test conflict");
	m = ob_isolated_mode_select11(false, false, false, true, false, false,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + dma_test conflict");
	m = ob_isolated_mode_select11(false, false, false, false, true, false,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + d11_tail conflict");
	m = ob_isolated_mode_select11(false, false, false, false, false, true,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + bsinitvals conflict");
	m = ob_isolated_mode_select11(false, false, false, false, false, false,
				      true, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + radio_id_probe conflict");
	m = ob_isolated_mode_select11(false, false, false, false, false, false,
				      false, true, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + rev42_diag conflict");
	m = ob_isolated_mode_select11(false, false, false, false, false, false,
				      false, false, true, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + rev42_attach conflict");
	m = ob_isolated_mode_select11(false, false, false, false, false, false,
				      false, false, false, true, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai + rev42_sb_reset conflict");

	/* A conflict among the other modes still wins over the AI arm. */
	m = ob_isolated_mode_select11(true, true, false, false, false, false,
				      false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "pre-existing conflict still reported");

	/* C. teardown / DMA ownership. */
	CHECK(ob_isolated_mode_skips_teardown(OB_ISOLATED_REV42_AI_PROBE),
	      "rev42 ai probe skips teardown");
	CHECK(!ob_isolated_mode_uses_dma(OB_ISOLATED_REV42_AI_PROBE),
	      "rev42 ai probe owns no DMA");
	CHECK(ob_isolated_mode_conflict(OB_ISOLATED_REV42_AI_PROBE) == false,
	      "ai probe is not itself a conflict state");
}

/*
 * rev42_diag_full is a MODIFIER of rev42_diag_probe_only, never a mode of its
 * own. On its own it selects no isolated mode, so ob_probe() would fall
 * through to the full ob_si_probe() bring-up. The in-driver guard rejects that
 * pairing before any allocation and before any hardware access.
 */
/*
 * M-D3B-CONTINUE-GATE-01: the 0x530 continuation modifier is admitted for the
 * isolated D3A1 tail test and for the isolated D3B band-init test (whose prefix
 * is that same tail). Admitting the modifier must not make the two isolated
 * modes combinable: asking for both is still a mode CONFLICT, refused before any
 * hardware access.
 */
static void test_d3b_continue_mode_policy(void)
{
	enum ob_isolated_mode m;

	m = ob_isolated_mode_select11(false, false, false, false, true, false,
				      false, false, false, false, false);
	CHECK(m == OB_ISOLATED_D3A1_TEST, "d11 tail alone selected");

	m = ob_isolated_mode_select11(false, false, false, false, false, true,
				      false, false, false, false, false);
	CHECK(m == OB_ISOLATED_D3B_TEST, "bsinitvals alone selected");

	m = ob_isolated_mode_select11(false, false, false, false, true, true,
				      false, false, false, false, false);
	CHECK(m == OB_ISOLATED_CONFLICT, "d11 tail + bsinitvals conflict");
}

static void test_diag_full_coupling(void)
{
	/* full=1 with the diagnostic NOT selected -> rejected. */
	CHECK(!ob_isolated_diag_full_allowed(true, false),
	      "rev42_diag_full=1 alone is rejected");
	/* full=1 with the diagnostic selected -> allowed. */
	CHECK(ob_isolated_diag_full_allowed(true, true),
	      "rev42_diag_full=1 with rev42_diag_probe_only=1 is allowed");
	/* full=0 -> existing behaviour unchanged, whatever the base mode. */
	CHECK(ob_isolated_diag_full_allowed(false, false),
	      "full=0 without the diagnostic is unchanged");
	CHECK(ob_isolated_diag_full_allowed(false, true),
	      "full=0 with the diagnostic is unchanged");

	/* The rejected pairing must not accidentally select a mode. */
	CHECK(ob_isolated_mode_select11(false, false, false, false, false, false,
					false, false, false, false, false) ==
	      OB_ISOLATED_NONE,
	      "full alone selects no mode (why the guard is required)");
}

/*
 * rev42_ai_enable_only: the core-enable transition.
 *
 * This is the only mode that WRITES a register, so the write values are the
 * load-bearing part of the test: they are transcribed from upstream
 * drivers/bcma/core.c:bcma_core_enable() with flags=0, and must never drift
 * towards a value nobody traced. Phase 37B moved the sequencing itself into
 * src/ob_ai_enable_only.h; the values and the predicate are unchanged.
 */
static void test_ai_en_sequence_values(void)
{
	/* Upstream bits: BCMA_IOCTL_CLK=0x1, BCMA_IOCTL_FGC=0x2,
	 * BCMA_RESET_CTL_RESET=0x1. */
	CHECK(OB_AIE_IOCTL_CLK == 0x1u, "CLK bit is 0x1");
	CHECK(OB_AIE_IOCTL_FGC == 0x2u, "FGC bit is 0x2");
	CHECK(OB_AIE_RCTL_RESET == 0x1u, "RESET_CTL RESET bit is 0x1");

	/* Upstream agent-register offsets. */
	CHECK(OB_AIE_OFF_IOCTL == 0x0408u, "BCMA_IOCTL is 0x0408");
	CHECK(OB_AIE_OFF_RESET_CTL == 0x0800u, "BCMA_RESET_CTL is 0x0800");
	CHECK(OB_AIE_OFF_RESET_ST == 0x0804u, "BCMA_RESET_ST is 0x0804");

	/* E1 raises CLK and FGC; E3 keeps CLK and drops FGC. */
	CHECK(OB_AIE_E1_IOCTL_VALUE == 0x3u, "IOCTL=CLK|FGC -> 0x3");
	CHECK(OB_AIE_E3_IOCTL_VALUE == 0x1u, "IOCTL=CLK -> 0x1");
	/* E2 is a plain zero: de-assert reset. */
	CHECK(OB_AIE_E2_RCTL_VALUE == 0x0u, "RESET_CTL de-assert -> 0x0");
	CHECK(OB_AIE_DELAY_US == 1u, "documented delay is udelay(1)");

	/* Exactly three writes, preceded by three pre-state reads. */
	CHECK(OB_AIE_MAX_WRITES == 3u, "exactly three writes");
	CHECK(OB_AIE_MAX_READS == 6u, "three pre-state reads + three readbacks");
	CHECK(OB_AIE_STEP_PRE_RESET_ST < OB_AIE_STEP_PRE_RESET_CTL &&
	      OB_AIE_STEP_PRE_RESET_CTL < OB_AIE_STEP_PRE_IOCTL,
	      "pre-state order is RESET_ST -> RESET_CTL -> IOCTL");
	CHECK(OB_AIE_STEP_PRE_IOCTL < OB_AIE_STEP_E1 &&
	      OB_AIE_STEP_E1 < OB_AIE_STEP_E2 &&
	      OB_AIE_STEP_E2 < OB_AIE_STEP_E3,
	      "order is E1 -> E2 -> E3");

	/* The final IOCTL value satisfies the vendor predicate's clock term
	 * ((IOCTL & 0x3) == 0x1) and the de-asserted RESET_CTL satisfies its
	 * reset term ((RESET_CTL & 0x1) == 0). */
	CHECK(ob_aie_core_up(OB_AIE_E3_IOCTL_VALUE, OB_AIE_E2_RCTL_VALUE),
	      "the sequence's own end state satisfies ai_iscoreup");
	CHECK(ob_diag_ai_iscoreup(OB_AIE_E3_IOCTL_VALUE,
				  OB_AIE_E2_RCTL_VALUE),
	      "ob_diag_ai_iscoreup agrees with ob_aie_core_up");
}

/*
 * The E3/E2 acceptance masks ARE the vendor predicate. This is a property, not
 * a coincidence to preserve by hand: no accepted readback pair can leave
 * ob_aie_core_up() false, so "readback matched" and "core enabled" cannot
 * diverge.
 */
static void test_ai_en_acceptance(void)
{
	u32 i, r;

	for (i = 0; i < 4; i++) {
		for (r = 0; r < 2; r++) {
			bool accepted = ob_aie_accept_e3(i) &&
					ob_aie_accept_e2(r);
			CHECK(accepted == ob_aie_core_up(i, r),
			      "E3/E2 acceptance equals the vendor predicate");
		}
	}
	CHECK(!ob_aie_accept_e1(0x0u), "E1 rejects CLK|FGC clear");
	CHECK(!ob_aie_accept_e1(0x1u), "E1 rejects FGC clear");
	CHECK(ob_aie_accept_e1(0x3u), "E1 accepts CLK|FGC set");
	/* Only the documented control bits are compared. */
	CHECK(ob_aie_accept_e1(0x3003u),
	      "E1 ignores the core-specific IOCTL bits");
	CHECK(!ob_aie_accept_e2(0x1u), "E2 rejects RESET still asserted");
	CHECK(ob_aie_accept_e2(0x40u),
	      "E2 agrees with (RESET_CTL & 0x1) == 0 regardless of other bits");
}

/*
 * The measured 2026-10-24 entry state was RESET_CTL = 0x1, i.e. the core is
 * ALREADY held in reset. bcma_core_disable() therefore early-returns and its
 * bounded RESET_ST poll never runs -- which is what lets this transition
 * contain no polling loop and no disable write at all.
 */
static void test_ai_en_disable_is_noop(void)
{
	CHECK(ob_aie_disable_is_noop(0x1u), "RESET set -> disable step skipped");
	CHECK(ob_aie_disable_is_noop(0xffffffffu),
	      "RESET set in an all-ones word -> still skipped");
	CHECK(ob_aie_disable_is_noop(0x3u),
	      "RESET set with other bits -> skipped");
	CHECK(!ob_aie_disable_is_noop(0x0u), "RESET clear -> not a no-op");
	CHECK(!ob_aie_disable_is_noop(0x2u),
	      "RESET clear, FGC set -> not a no-op");
}

/*
 * Pre-state admission and the removal policy. Both are fail-closed: an
 * unexpected register state is refused before the first write, and any state
 * reached after a write is retained rather than torn down.
 */
static void test_ai_en_prestate_and_lifecycle(void)
{
	struct ob_aie_result res;

	/* The live Phase 37A state is admitted. */
	CHECK(ob_aie_prestate_ok(0x0u, 0x1u, 0x0u),
	      "measured reset-held state admitted");
	/* Reset already released: this contract covers only a reset-held core. */
	CHECK(!ob_aie_prestate_ok(0x0u, 0x0u, 0x0u),
	      "reset released -> rejected");
	/* Already clocked or force-gated: not the measured state. */
	CHECK(!ob_aie_prestate_ok(0x0u, 0x1u, 0x1u),
	      "clocked core -> rejected");
	CHECK(!ob_aie_prestate_ok(0x0u, 0x1u, 0x3u),
	      "force-gated core -> rejected");
	/* A reset in progress is never written through. */
	CHECK(!ob_aie_prestate_ok(0x1u, 0x1u, 0x0u),
	      "RESET_ST non-zero -> rejected");
	/* Only the documented control bits are compared, exactly as upstream. */
	CHECK(ob_aie_prestate_ok(0x0u, 0x1u, 0x1000u),
	      "IOCTL core-specific bits are not constrained");

	/* Removal policy. */
	ob_aie_result_init(&res);
	res.class = OB_AIE_CLASS_PRECHECK_REJECTED;
	CHECK(ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_CLEAN,
	      "write-free pre-check rejection -> CLEAN");

	res.writes_attempted = 1;
	CHECK(ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN,
	      "any attempted write -> RETAIN");

	ob_aie_result_init(&res);
	res.class = OB_AIE_CLASS_CORE_UP;
	res.core_up = true;
	res.writes_attempted = 3;
	res.writes_confirmed = 3;
	CHECK(ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN,
	      "core up -> RETAIN (no teardown contract exists)");

	ob_aie_result_init(&res);
	res.device_lost = true;
	CHECK(ob_aie_remove_policy_of(&res) == OB_AIE_REMOVE_RETAIN,
	      "device loss -> RETAIN");
	CHECK(ob_aie_remove_policy_of(NULL) == OB_AIE_REMOVE_RETAIN,
	      "missing result -> RETAIN (fail-closed)");

	/* Names must be distinct and stable: the harness greps them. */
	CHECK(strcmp(ob_aie_class_token(OB_AIE_CLASS_CORE_UP),
		     "D11_WRAPPER_CORE_UP") == 0,
	      "token D11_WRAPPER_CORE_UP");
	CHECK(strcmp(ob_aie_class_token(OB_AIE_CLASS_PRECHECK_REJECTED),
		     "ENABLE_PRECHECK_REJECTED") == 0,
	      "token ENABLE_PRECHECK_REJECTED");
	CHECK(strcmp(ob_aie_class_token(OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED),
		     "ENABLE_DEVICE_LOSS_SUSPECTED") == 0,
	      "token ENABLE_DEVICE_LOSS_SUSPECTED");
	CHECK(strcmp(ob_aie_class_token(OB_AIE_CLASS_TRANSITION_UNCERTAIN),
		     "ENABLE_TRANSITION_UNCERTAIN") == 0,
	      "token ENABLE_TRANSITION_UNCERTAIN");
}

/*
 * rev42_ai_enable_only must be mutually exclusive with EVERY other isolated
 * mode. This matters more than for the read-only probe: this mode writes
 * registers, so a conflict that slipped through would issue real writes
 * inside a run the operator believed belonged to a different mode.
 */
static void test_ai_en_mode_policy(void)
{
	enum ob_isolated_mode m;

	/* Alone. */
	m = ob_isolated_mode_select12(false, false, false, false, false, false,
				       false, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_REV42_AI_ENABLE, "ai enable selected alone");

	/* Nothing selected. */
	m = ob_isolated_mode_select12(false, false, false, false, false, false,
				       false, false, false, false, false, false);
	CHECK(m == OB_ISOLATED_NONE, "no mode -> OB_ISOLATED_NONE");

	/* Conflicts with all eleven other modes, one at a time. */
	m = ob_isolated_mode_select12(true, false, false, false, false, false,
				       false, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + fw_validate conflict");
	m = ob_isolated_mode_select12(false, true, false, false, false, false,
				       false, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + ucode_test conflict");
	m = ob_isolated_mode_select12(false, false, true, false, false, false,
				       false, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + initvals_test conflict");
	m = ob_isolated_mode_select12(false, false, false, true, false, false,
				       false, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + dma_test conflict");
	m = ob_isolated_mode_select12(false, false, false, false, true, false,
				       false, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + d11_tail conflict");
	m = ob_isolated_mode_select12(false, false, false, false, false, true,
				       false, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + bsinitvals conflict");
	m = ob_isolated_mode_select12(false, false, false, false, false, false,
				       true, false, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + radio_id_probe conflict");
	m = ob_isolated_mode_select12(false, false, false, false, false, false,
				       false, true, false, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + rev42_diag conflict");
	m = ob_isolated_mode_select12(false, false, false, false, false, false,
				       false, false, true, false, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + rev42_attach conflict");
	m = ob_isolated_mode_select12(false, false, false, false, false, false,
				       false, false, false, true, false, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + rev42_sb_reset conflict");
	/* The read-only AI probe is the closest neighbour: the two are
	 * alternatives (observe vs enable), never a sequence. */
	m = ob_isolated_mode_select12(false, false, false, false, false, false,
				       false, false, false, false, true, true);
	CHECK(m == OB_ISOLATED_CONFLICT, "ai-en + ai probe conflict");
}

int main(void)
{
	test_decode();
	test_reconstruct();
	test_sentinel();
	test_mode_policy();
	test_ai_mode_policy();
	test_d3b_continue_mode_policy();
	test_ai_en_mode_policy();
	test_diag_full_coupling();
	test_ai_addr_window();
	test_ai_coherency();
	test_ai_iscoreup();
	test_ai_all_ones_does_not_classify();
	test_ai_cfg_readback_status();
	test_ai_en_sequence_values();
	test_ai_en_acceptance();
	test_ai_en_disable_is_noop();
	test_ai_en_prestate_and_lifecycle();
	test_ai_dev_lost();
	test_ai_dev_lost_dominates();
	test_ai_summarise();

	if (failures) {
		printf("ob_diag_test: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ob_diag_test: OK\n");
	return 0;
}

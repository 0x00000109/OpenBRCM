#!/usr/bin/env python3
"""Structural acceptance for T4 (acphy_identity_3e0_first).

Every executed minimal-state run still carried prior D11 traffic before the
first 0x3xx access: L2 read the eight 0x1xx timing words first, D1'/HIL ran
radio prepare and the cflags write first, exec03 read a control first. T4
removes the last of those variables and asks one question:

    is any prior D11 traffic needed for the 0x3e0 stall?

The modifier is only sound if:

  * it is a modifier of L2, refused without acphy_identity_timing_only, and
    refused in combination with any other L2 probe modifier;
  * the eight-read ob_si_d11_diag() block is NOT issued when it is armed, so
    the only D11 accesses of the run are F1 (0x01e0, 32-bit) and F2
    (0x03e0, 16-bit, the first and only 0x3xx access);
  * F2 is issued only when F1 was LIVE; a missing bracket is VOID and never
    followed by the fatal read;
  * a stall at F2 latches dev_lost exactly like L2j/L2k and the run stops
    there (no legacy read, no later stage);
  * the F2 timing line keeps the L2 format so the two traces compare
    character for character;
  * no write symbol is referenced by the mode.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")

OTHER_L2_MODIFIERS = (
    "acphy_identity_3e2_first",
    "acphy_identity_span_probe",
    "acphy_identity_control_reprobe",
    "acphy_identity_control16_reprobe",
    "acphy_identity_upper_probe",
    "acphy_identity_edge_probe",
    "acphy_identity_wide_probe",
    "acphy_identity_edge2_probe",
    "acphy_identity_lower_probe",
    "acphy_identity_wrapper_witness",
    "acphy_identity_pll_witness",
)


def helper(name: str) -> str:
    start = SI.index(f"static void {name}(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentity3e0FirstTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_3e0_first;", CORE)
        self.assertIn("module_param(acphy_identity_3e0_first, bool, 0444);", CORE)
        self.assertIn(
            "if (acphy_identity_3e0_first && !acphy_identity_timing_only)",
            CORE,
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_3e0_first", select)

    def test_modifier_is_exclusive_with_every_other_l2_modifier(self):
        start = CORE.index("if (acphy_identity_3e0_first &&\n")
        conflict = CORE[start:CORE.index("return -EINVAL;", start)]
        self.assertIn("exclusive", conflict)
        for name in OTHER_L2_MODIFIERS:
            self.assertIn(name, conflict, f"{name} is not covered by the exclusion")
        # The exclusion must be symmetric: L2k's and L2l's own lists cover T4.
        for anchor in ("if (acphy_identity_wrapper_witness &&\n",
                       "if (acphy_identity_pll_witness &&\n"):
            block_start = CORE.index(anchor)
            block = CORE[block_start:CORE.index("return -EINVAL;", block_start)]
            self.assertIn("acphy_identity_3e0_first", block)

    def test_bring_up_block_is_skipped_only_for_t4(self):
        self.assertIn(
            "\tif (!hw->acphy_identity_3e0_first)\n\t\tob_si_d11_diag(hw);",
            SI,
        )
        # The flag is set from the mode, so it can never leak into another path.
        self.assertIn(
            "hw->acphy_identity_3e0_first =\n"
            "\t\tmode == OB_ISOLATED_ACPHY_IDENTITY_TIMING && acphy_identity_3e0_first;",
            CORE,
        )

    def test_stage_order_offsets_and_widths(self):
        body = helper("ob_si_d11_identity_3e0_first")
        f1 = body.index("ob_d11_read32(hw, 0x01e0u)")
        f2 = body.index("ob_d11_read16(hw, 0x03e0u)")
        self.assertLess(f1, f2, "F2 must follow F1")
        # F1 is the 32-bit, guarded liveness bracket on clk_ctl_st.
        self.assertIn('stage=F1 offset=0x%04x window=d11 width=32 guarded=1', body)
        # F2 is the production vendor read: 16-bit, same offset, same window.
        self.assertIn('stage=F2 offset=0x%04x window=d11 width=16 guarded=1', body)
        # The F2 timing line must be byte-identical in format to L2's line.
        self.assertIn(
            "D11_TRANSACTION_TIMING offset=0x%04x width=%u value=0x%08x "
            "start_ns=%llu end_ns=%llu duration_us=%llu",
            body,
        )
        self.assertIn("0x03e0u, 16u, (u32)val", body)

    def test_f2_is_issued_only_when_f1_was_live(self):
        body = helper("ob_si_d11_identity_3e0_first")
        void_branch = body.index("if (f1 != OB_L2K_LIVE)")
        f2 = body.index("ob_d11_read16(hw, 0x03e0u)")
        self.assertLess(void_branch, f2)
        between = body[void_branch:f2]
        self.assertIn("goto summary;", between)
        self.assertIn("hw->acphy_identity_witness_void = true;", between)
        self.assertIn('verdict = "void_no_liveness_bracket";', between)

    def test_stall_latches_device_loss(self):
        body = helper("ob_si_d11_identity_3e0_first")
        self.assertIn("if (f2 == OB_L2K_STALL)", body)
        self.assertIn("hw->dev_lost = true;", body)
        # The latch is followed by the summary, never by another read.
        self.assertLess(
            body.index("hw->dev_lost = true;"),
            body.index("ACPHY_IDENTITY_3E0FIRST_SUMMARY:"),
        )

    def test_verdict_vocabulary(self):
        body = helper("ob_si_d11_identity_3e0_first")
        for verdict in (
            "vendor_read_stall_first_access",
            "vendor_read_completed_first_access",
            "vendor_read_all_ones_fast",
            "vendor_read_grey",
            "void_no_liveness_bracket",
        ):
            self.assertIn(verdict, body)
        self.assertIn("bracket_01e0=0x%08x f1=%s f2=%s", body)
        self.assertIn("value_03e0=0x%04x", body)
        self.assertIn("reads_0x3xx=1", body)
        self.assertIn("writes_issued=0", body)

    def test_no_write_symbol_is_referenced_by_the_mode(self):
        body = helper("ob_si_d11_identity_3e0_first")
        for forbidden in (
            "bcma_write",
            "bcma_awrite",
            "ob_d11_write",
            "ob_axi_write",
            "pci_write",
            "iowrite",
            "awrite",
        ):
            self.assertNotIn(forbidden, body)

    def test_t4_replaces_the_legacy_read_and_stops_the_run(self):
        block = l2_block()
        t4 = block.index("ob_si_d11_identity_3e0_first(hw)")
        for later in (
            "ob_si_d11_identity_pll_witness(hw)",
            "ob_si_d11_identity_wrapper_witness(hw)",
            "ob_si_d11_identity_lower_probe(hw)",
            "ob_si_d11_identity_timing(hw);",
        ):
            self.assertLess(t4, block.index(later), f"{later} is issued before T4")
        self.assertIn("hw->acphy_identity_early_stop = true;", block)
        # The T4 arm returns immediately: no later stage can run after F2.
        after_t4 = block[t4:]
        self.assertIn("return 0;", after_t4)
        self.assertLess(
            after_t4.index("return 0;"),
            after_t4.index("ob_si_d11_identity_pll_witness(hw)"),
        )

    def test_state_flag_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_3e0_first;", CORE_H)
        # Shared fail-stop state is reused, not duplicated.
        self.assertIn("bool\t\t\tacphy_identity_witness_failed;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_witness_void;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_early_stop;", CORE_H)

    def test_harness_arms_t4_only_on_request(self):
        self.assertIn('"${OB_L2_3E0_FIRST:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_3e0_first=1"', HARNESS)
        # Both load paths (dry run and the real insmod) must carry the parameter.
        self.assertGreaterEqual(HARNESS.count("acphy_identity_3e0_first=1"), 3)
        self.assertIn("acphy_identity_3e0first_markers_complete", HARNESS)
        self.assertIn("ACPHY_IDENTITY_3E0FIRST_SUMMARY", HARNESS)
        # The contract must prove the block was absent, not merely accept a
        # trace that happens not to contain the eight lines.
        self.assertIn(
            "for off in 0120 0124 0128 012c 0154 0158 0180 0184; do", HARNESS
        )
        self.assertIn("the eight-read block ran; this is not T4", HARNESS)
        self.assertIn("no liveness bracket; run is VOID", HARNESS)

    def test_harness_refuses_t4_without_the_mode_or_with_a_sibling(self):
        self.assertIn(
            'die "OB_L2_3E0_FIRST=1 requires --mode acphy_identity_timing_only"',
            HARNESS,
        )
        self.assertIn(
            'die "OB_L2_3E0_FIRST=1 is exclusive with every other OB_L2_* '
            'arming variable"',
            HARNESS,
        )
        for sibling in (
            "OB_L2_PLL_WITNESS",
            "OB_L2_WRAPPER_WITNESS",
            "OB_L2_LOWER_PROBE",
            "OB_L2_EDGE2_PROBE",
            "OB_L2_WIDE_PROBE",
            "OB_L2_EDGE_PROBE",
            "OB_L2_UPPER_PROBE",
            "OB_L2_CONTROL16_REPROBE",
            "OB_L2_CONTROL_REPROBE",
            "OB_L2_SPAN_PROBE",
            "OB_L2_3E2_FIRST",
        ):
            self.assertIn(sibling, HARNESS)


if __name__ == "__main__":
    unittest.main()

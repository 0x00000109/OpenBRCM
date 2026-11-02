#!/usr/bin/env python3
"""Structural acceptance for the L2k scope witness (acphy_identity_wrapper_witness).

The address bisection (L2..L2j) measured WHERE the ~201.7 ms timeout band is;
the witness asks its SCOPE: after the island, does the whole PCIe function stop
completing MMIO, or only that block?

The witness is only sound if:

  * it is a modifier of L2, refused without acphy_identity_timing_only, and
    refused in combination with any other L2 probe modifier (it must be the
    first access after the eight-read block and it replaces the legacy 0x3e0
    read);
  * it reads the wrapper registers through core->wrap (BCMA_IOCTL,
    BCMA_RESET_ST, BCMA_RESET_CTL) and the D11 control at 0x0120;
  * the island entry is the L2j-measured 0x03da, and its stall latches
    dev_lost exactly like L2j;
  * every post-stall read BYPASSES the device-loss guard: the guard returns
    0xffffffff without issuing MMIO once dev_lost is latched, so a guarded
    read would measure the driver, not the silicon;
  * the sequence fail-stops at the first non-LIVE stage, and a missing baseline
    stops before the island and marks the run VOID;
  * no write symbol is referenced by the mode.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")

L2_MODIFIERS = (
    "acphy_identity_3e2_first",
    "acphy_identity_span_probe",
    "acphy_identity_control_reprobe",
    "acphy_identity_control16_reprobe",
    "acphy_identity_upper_probe",
    "acphy_identity_edge_probe",
    "acphy_identity_wide_probe",
    "acphy_identity_edge2_probe",
    "acphy_identity_lower_probe",
)


def witness_helper() -> str:
    start = SI.index(
        "static void ob_si_d11_identity_wrapper_witness(struct ob_hw *hw)"
    )
    return SI[start:SI.index("\n}\n", start)]


def read32_helper() -> str:
    start = SI.index("static enum ob_l2k_class ob_si_l2k_read32(struct ob_hw *hw")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentityWrapperWitnessTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_wrapper_witness;", CORE)
        self.assertIn(
            "module_param(acphy_identity_wrapper_witness, bool, 0444);", CORE
        )
        self.assertIn(
            "if (acphy_identity_wrapper_witness && !acphy_identity_timing_only)",
            CORE,
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_wrapper_witness", select)

    def test_witness_is_exclusive_with_every_other_l2_modifier(self):
        first = CORE.index("acphy_identity_wrapper_witness &&")
        second = CORE.index("acphy_identity_wrapper_witness &&", first + 1)
        conflict = CORE[second:CORE.index("return -EINVAL;", second)]
        self.assertIn("exclusive", conflict)
        for name in L2_MODIFIERS:
            self.assertIn(name, conflict, f"{name} is not covered by the exclusion")

    def test_stage_order_windows_and_widths(self):
        body = witness_helper()
        order = [
            body.index('"S1", OB_L2K_WIN_WRAP, BCMA_IOCTL'),
            body.index('"S2", OB_L2K_WIN_D11'),
            body.index("ob_d11_read16(hw, 0x03dau)"),
            body.index('"S4", OB_L2K_WIN_D11, 0x0120u, false'),
            body.index('"S5", OB_L2K_WIN_WRAP, BCMA_IOCTL, false'),
            body.index('"S6", OB_L2K_WIN_D11'),
        ]
        self.assertEqual(order, sorted(order), "stages are out of order")
        # S1: wrapper baseline, guarded (safe: dev_lost is still 0).
        self.assertIn('"S1", OB_L2K_WIN_WRAP, BCMA_IOCTL, true', body)
        self.assertIn('"S1", OB_L2K_WIN_WRAP, BCMA_RESET_ST', body)
        # S2: pre-island D11 control.
        self.assertIn('"S2", OB_L2K_WIN_D11, 0x0120u, true', body)
        # S3: the L2j-measured island entry, 16-bit, same latch.
        self.assertIn("ob_d11_read16(hw, 0x03dau)", body)
        self.assertIn("hw->dev_lost = true;", body)
        # S4: the clean post-stall witness, unguarded, no switch.
        self.assertIn('"S4", OB_L2K_WIN_D11, 0x0120u, false', body)
        # S5: the wrapper witness, unguarded, all three registers.
        self.assertIn('"S5", OB_L2K_WIN_WRAP, BCMA_IOCTL, false', body)
        self.assertIn('"S5", OB_L2K_WIN_WRAP, BCMA_RESET_ST', body)
        self.assertIn('"S5", OB_L2K_WIN_WRAP, BCMA_RESET_CTL', body)
        # S6: control after switching back to the D11 window.
        self.assertIn('"S6", OB_L2K_WIN_D11, 0x0120u, false', body)

    def test_unguarded_reads_bypass_the_guard(self):
        body = read32_helper()
        self.assertNotIn("ob_guard_do_", body)
        # The guard wrappers may only appear on the guarded side of the
        # ternary; the unguarded side is the dedicated witness helper, which
        # is the only place raw bcma access is allowed (ob_guard.c).
        self.assertIn("ob_axi_read32(hw, off)", body)
        self.assertIn(": ob_axi_witness_read32(hw, off)", body)
        self.assertIn("ob_d11_read32(hw, off)", body)
        self.assertIn(": ob_d11_witness_read32(hw, off)", body)
        self.assertIn("guarded ? ", body)
        self.assertIn("start_ns=%llu end_ns=%llu", body)
        # The raw access itself lives in the guard backend, in two functions
        # with no other caller anywhere in the tree.
        guard = (ROOT / "src/ob_guard.c").read_text(encoding="utf-8")
        self.assertIn("u32 ob_d11_witness_read32(struct ob_hw *hw, u16 off)", guard)
        self.assertIn("u32 ob_axi_witness_read32(struct ob_hw *hw, u16 off)", guard)
        self.assertIn("bcma_read32(hw->core, off)", guard)
        self.assertIn("bcma_aread32(hw->core, off)", guard)
        self.assertEqual(SI.count("ob_d11_witness_read32("), 1)
        self.assertEqual(SI.count("ob_axi_witness_read32("), 1)

    def test_no_write_symbol_is_referenced_by_the_mode(self):
        for body in (witness_helper(), read32_helper()):
            for forbidden in ("bcma_write", "bcma_awrite", "ob_d11_write",
                              "ob_axi_write", "pci_write", "iowrite", "awrite"):
                self.assertNotIn(forbidden, body)

    def test_classes_and_abort_threshold(self):
        helper = witness_helper()
        self.assertIn("OB_L2_3E2_STALL_US", SI[SI.index("static enum ob_l2k_class ob_si_l2k_classify"):])
        self.assertIn('return "ALL_ONES_FAST";', SI)
        self.assertIn('return "GREY";', SI)
        self.assertIn("OB_L2K_ABORT_US", helper)
        self.assertIn("S3 exceeded 1 s", helper)

    def test_missing_baseline_voids_the_run_before_the_island(self):
        body = witness_helper()
        self.assertIn("hw->acphy_identity_witness_void = true;", body)
        self.assertIn('verdict = "void_no_baseline";', body)
        self.assertIn("the island is NOT issued", body)
        # The void branch must precede the island read.
        self.assertLess(
            body.index("void_no_baseline"), body.index("ob_d11_read16(hw, 0x03dau)")
        )

    def test_stages_fail_stop_before_the_next_one(self):
        body = witness_helper()
        s4_gate = body.index("if (s4 != OB_L2K_LIVE)")
        s5_read = body.index('"S5", OB_L2K_WIN_WRAP, BCMA_IOCTL')
        s5_gate = body.index("if (s5 != OB_L2K_LIVE)")
        s6_read = body.index('"S6", OB_L2K_WIN_D11')
        self.assertLess(s4_gate, s5_read)
        self.assertLess(s5_gate, s6_read)
        self.assertIn("hw->acphy_identity_witness_failed = true;", body)
        self.assertEqual(body.count("hw->acphy_identity_witness_failed = true;"), 3)

    def test_window_switch_is_paid_only_when_s4_is_live(self):
        body = witness_helper()
        # window_changes counts the S1/S2 pair unconditionally, then S5 and S6.
        self.assertIn("window_changes = 2;", body)
        self.assertLess(body.index("window_changes = 2;"),
                        body.index("window_changes++;"))
        self.assertEqual(body.count("window_changes++;"), 2)

    def test_summary_carries_the_verdict_vocabulary(self):
        body = witness_helper()
        self.assertIn("ACPHY_IDENTITY_WITNESS_SUMMARY:", body)
        for verdict in ("island_local", "window_specific", "function_wide",
                        "not_applicable_no_reproduction",
                        "inconclusive_switch_confounded"):
            self.assertIn(verdict, body)
        self.assertIn("writes_issued=0", body)
        self.assertIn("ob_si_l2k_class_name(s1)", body)

    def test_witness_is_first_and_replaces_the_legacy_0x3e0_read(self):
        block = l2_block()
        witness = block.index("ob_si_d11_identity_wrapper_witness(hw)")
        for later in (
            "ob_si_d11_identity_lower_probe(hw)",
            "ob_si_d11_identity_edge2_probe(hw)",
            "ob_si_d11_identity_wide_probe(hw)",
            "ob_si_d11_identity_control16_reprobe(hw)",
            "ob_si_d11_identity_control_reprobe(hw)",
            "ob_si_d11_identity_span_probe(hw)",
            "ob_si_d11_identity_3e2_probe(hw)",
        ):
            self.assertLess(witness, block.index(later))
        # The legacy 0x3e0 read is now skipped by two modes (L2k and the L2l
        # PHY-PLL witness), so the guard must name both.
        self.assertIn("if (!hw->acphy_identity_wrapper_witness &&", block)
        self.assertIn("!hw->acphy_identity_pll_witness)", block)
        self.assertLess(witness, block.index("ob_si_d11_identity_timing(hw);"))
        stop = block.index("hw->acphy_identity_witness_failed ||")
        self.assertIn("hw->acphy_identity_early_stop = true;", block[stop:])

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_wrapper_witness;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_witness_failed;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_witness_void;", CORE_H)

    def test_harness_enables_the_witness_only_on_request(self):
        self.assertIn('"${OB_L2_WRAPPER_WITNESS:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_wrapper_witness=1"', HARNESS)
        self.assertIn("acphy_identity_witness_markers_complete", HARNESS)
        self.assertIn("ACPHY_IDENTITY_WITNESS_SUMMARY", HARNESS)
        # The L2 marker must not be claimed when the 0x3e0 read is skipped.
        self.assertIn("0x3e0 NOT read", HARNESS)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Structural acceptance for the L2f upper-bound modifier (acphy_identity_upper_probe).

The failing region is bounded from below (0x03e0, 0x03e2, 0x03e4 all stall) and
both endpoint death and access width are excluded, but nothing above 0x03e7 has
ever been read. This probe measures the upper edge:

  * reads exactly 0x03e6 first, then 0x03e8 only if the first completed;
  * 16 bits wide, through ob_d11_read16(), never a write;
  * is the FIRST access after the eight-read block;
  * fails closed: a stall latches device loss, sets the shared early-stop latch
    and suppresses the rest of the run (no 0x3e2, no 0x3e0).
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def upper_helper() -> str:
    start = SI.index("static bool ob_si_d11_identity_upper_probe(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentityUpperProbeTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_upper_probe;", CORE)
        self.assertIn("module_param(acphy_identity_upper_probe, bool, 0444);", CORE)
        self.assertIn(
            "if (acphy_identity_upper_probe && !acphy_identity_timing_only)", CORE
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_upper_probe", select)

    def test_helper_walks_both_words_in_order_and_never_writes(self):
        body = upper_helper()
        self.assertEqual(body.count("{ 0x03e6u, 0x03e8u }"), 1)
        self.assertEqual(body.count("ob_d11_read16(hw, offs[i])"), 1)
        self.assertNotIn("write", body)
        self.assertNotIn("0x03e0", body)
        self.assertNotIn("0x03e2", body)
        self.assertIn("OB_L2_3E2_STALL_US", body)
        self.assertIn("D11_TRANSACTION_TIMING offset=", body)
        self.assertIn("ACPHY_IDENTITY_UPPER:", body)

    def test_a_stall_stops_before_the_next_word(self):
        body = upper_helper()
        self.assertIn("hw->dev_lost = true;", body)
        self.assertLess(
            body.index("hw->dev_lost = true;"), body.index("ACPHY_IDENTITY_UPPER:")
        )
        # The stop must be inside the loop, after the marker, and return false.
        loop = body.index("for (i = 0; i < ARRAY_SIZE(offs); i++)")
        self.assertIn("return false;", body[loop:])

    def test_upper_probe_is_the_first_access_after_the_block(self):
        block = l2_block()
        first = block.index("ob_si_d11_identity_upper_probe(hw)")
        for later in (
            "ob_si_d11_identity_control16_reprobe(hw)",
            "ob_si_d11_identity_control_reprobe(hw)",
            "ob_si_d11_identity_span_probe(hw)",
            "ob_si_d11_identity_3e2_probe(hw)",
            "ob_si_d11_identity_timing(hw)",
        ):
            self.assertLess(first, block.index(later))
        self.assertIn("hw->acphy_identity_upper_failed = true;", block)
        stop = block.index("hw->acphy_identity_upper_failed = true;")
        self.assertIn("hw->acphy_identity_early_stop = true;", block[stop:])

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_upper_probe;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_upper_failed;", CORE_H)

    def test_harness_enables_the_probe_only_on_request(self):
        self.assertIn('"${OB_L2_UPPER_PROBE:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_upper_probe=1"', HARNESS)


if __name__ == "__main__":
    unittest.main()

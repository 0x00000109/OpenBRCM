#!/usr/bin/env python3
"""Structural acceptance for the L2j lower-edge bisection (acphy_identity_lower_probe).

The upper edge is measured to 6 bytes; the lower edge is not measured at all.
This probe starts at 0x02b0 and halves the bracket [0x0184 completes
(0x03e0 stalls)] toward the first stalling word, inside one insertion.

The search is only sound if:

  * it starts at exactly 0x02b0 and the bracket is exactly [0x0184, 0x03e0];
  * every probe is a 16-bit read through ob_d11_read16, never a write;
  * it is the FIRST access after the eight-read block;
  * a completing probe moves the lower bound up and the next probe is a midpoint
    of the remaining interval, 16-bit aligned;
  * a stall latches device loss, sets the shared early-stop latch and reports the
    bracket it has instead of continuing.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def lower_helper() -> str:
    start = SI.index("static bool ob_si_d11_identity_lower_probe(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentityLowerProbeTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_lower_probe;", CORE)
        self.assertIn("module_param(acphy_identity_lower_probe, bool, 0444);", CORE)
        self.assertIn(
            "if (acphy_identity_lower_probe && !acphy_identity_timing_only)", CORE
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_lower_probe", select)

    def test_bracket_and_first_probe_are_the_requested_ones(self):
        body = lower_helper()
        self.assertIn("u32 lo = 0x0184u;", body)
        self.assertIn("u32 hi = 0x03e0u;", body)
        self.assertIn("u32 off = 0x02b0u;", body)
        # The first read must use `off`, not a midpoint.
        first_read = body.index("ob_d11_read16(hw, (u16)off)")
        self.assertLess(first_read, body.index("lo = off;"))

    def test_search_halves_the_interval_and_aligns_to_16_bits(self):
        body = lower_helper()
        self.assertIn("off = ((lo + hi) / 2u) & ~1u;", body)
        self.assertIn("lo = off;", body)
        self.assertIn("if (hi - lo <= 2u)", body)
        self.assertEqual(body.count("ob_d11_read16(hw, (u16)off)"), 1)
        self.assertNotIn("write", body)

    def test_a_stall_reports_the_bracket_and_stops(self):
        body = lower_helper()
        self.assertIn("hw->dev_lost = true;", body)
        loop = body.index("for (step = 0; step < 12u; step++)")
        stop = body.index("return false;")
        self.assertLess(loop, stop)
        self.assertLess(body.index("ACPHY_IDENTITY_LOWER:"), stop)
        self.assertLess(
            body.index("hw->dev_lost = true;"), body.index("ACPHY_IDENTITY_LOWER:")
        )
        self.assertIn("step < 12u", body)

    def test_lower_probe_is_the_first_access_after_the_block(self):
        block = l2_block()
        first = block.index("ob_si_d11_identity_lower_probe(hw)")
        for later in (
            "ob_si_d11_identity_edge2_probe(hw)",
            "ob_si_d11_identity_wide_probe(hw)",
            "ob_si_d11_identity_control16_reprobe(hw)",
            "ob_si_d11_identity_control_reprobe(hw)",
            "ob_si_d11_identity_span_probe(hw)",
            "ob_si_d11_identity_3e2_probe(hw)",
            "ob_si_d11_identity_timing(hw)",
        ):
            self.assertLess(first, block.index(later))
        self.assertIn("hw->acphy_identity_lower_failed = true;", block)
        stop = block.index("hw->acphy_identity_lower_failed = true;")
        self.assertIn("hw->acphy_identity_early_stop = true;", block[stop:])

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_lower_probe;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_lower_failed;", CORE_H)

    def test_harness_enables_the_probe_only_on_request(self):
        self.assertIn('"${OB_L2_LOWER_PROBE:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_lower_probe=1"', HARNESS)


if __name__ == "__main__":
    unittest.main()

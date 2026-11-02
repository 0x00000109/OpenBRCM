#!/usr/bin/env python3
"""Structural acceptance for the L2g edge-bracket modifier (acphy_identity_edge_probe).

The failing region reaches at least 0x03e7 but nothing is known above it. An
ascending walk costs a timeout plus a reboot per word, so this probe reads
DOWNWARD from a coarse jump: 0x03f0, then 0x03ec, then 0x03e8, stopping at the
first stall. The last completing address and the first stalled one are the two
edges of the boundary, which is only true if:

  * exactly those three offsets are read, in that order, 16 bits wide;
  * it is the FIRST access after the eight-read block;
  * no write is ever issued;
  * a stall latches device loss, sets the shared early-stop latch and issues
    nothing further.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def edge_helper() -> str:
    start = SI.index("static bool ob_si_d11_identity_edge_probe(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentityEdgeProbeTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_edge_probe;", CORE)
        self.assertIn("module_param(acphy_identity_edge_probe, bool, 0444);", CORE)
        self.assertIn(
            "if (acphy_identity_edge_probe && !acphy_identity_timing_only)", CORE
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_edge_probe", select)

    def test_helper_walks_downward_three_words_and_never_writes(self):
        body = edge_helper()
        self.assertEqual(body.count("{ 0x03f0u, 0x03ecu, 0x03e8u }"), 1)
        self.assertEqual(body.count("ob_d11_read16(hw, offs[i])"), 1)
        self.assertNotIn("write", body)
        self.assertNotIn("0x03e6", body)
        self.assertNotIn("0x03e0", body)
        self.assertIn("OB_L2_3E2_STALL_US", body)
        self.assertIn("ACPHY_IDENTITY_EDGE:", body)

    def test_a_stall_stops_before_the_next_word(self):
        body = edge_helper()
        self.assertIn("hw->dev_lost = true;", body)
        loop = body.index("for (i = 0; i < ARRAY_SIZE(offs); i++)")
        stop = body.index("return false;")
        self.assertLess(loop, stop)
        # The stop sits after the marker and inside the loop body.
        self.assertLess(body.index("ACPHY_IDENTITY_EDGE:"), stop)
        self.assertLess(body.index("hw->dev_lost = true;"), body.index("ACPHY_IDENTITY_EDGE:"))

    def test_edge_probe_is_the_first_access_after_the_block(self):
        block = l2_block()
        first = block.index("ob_si_d11_identity_edge_probe(hw)")
        for later in (
            "ob_si_d11_identity_upper_probe(hw)",
            "ob_si_d11_identity_control16_reprobe(hw)",
            "ob_si_d11_identity_control_reprobe(hw)",
            "ob_si_d11_identity_span_probe(hw)",
            "ob_si_d11_identity_3e2_probe(hw)",
            "ob_si_d11_identity_timing(hw)",
        ):
            self.assertLess(first, block.index(later))
        self.assertIn("hw->acphy_identity_edge_failed = true;", block)
        stop = block.index("hw->acphy_identity_edge_failed = true;")
        self.assertIn("hw->acphy_identity_early_stop = true;", block[stop:])

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_edge_probe;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_edge_failed;", CORE_H)

    def test_harness_enables_the_probe_only_on_request(self):
        self.assertIn('"${OB_L2_EDGE_PROBE:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_edge_probe=1"', HARNESS)


if __name__ == "__main__":
    unittest.main()

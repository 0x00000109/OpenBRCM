#!/usr/bin/env python3
"""Structural acceptance for the L2c span modifier (acphy_identity_span_probe).

L2 and L2b showed both halves of the 0x03e0 dword fail identically, so the
address is not the failing unit. L2c asks whether the next dword shares the
failure. It is only meaningful if it reads exactly 0x03e4 and 0x03e6, in that
order, before the 0x3e2/0x3e0 pair (0x3e2 is known to stall, so a probe placed
after it would never run), and stops at the first stall.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def span_helper() -> str:
    start = SI.index("static bool ob_si_d11_identity_span_probe(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


class AcphyIdentitySpanProbeTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_span_probe;", CORE)
        self.assertIn("module_param(acphy_identity_span_probe, bool, 0444);", CORE)
        self.assertIn(
            "if (acphy_identity_span_probe && !acphy_identity_timing_only)", CORE
        )
        # Never handed to the mode selector.
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_span_probe", select)

    def test_helper_reads_the_next_dword_in_order_and_never_writes(self):
        body = span_helper()
        self.assertEqual(body.count('{ 0x03e4u, 0x03e6u }'), 1)
        self.assertIn("ob_d11_read16(hw, offs[i])", body)
        self.assertNotIn("0x03e0", body)
        self.assertNotIn("0x03e2", body)
        self.assertNotIn("write", body)
        self.assertIn("D11_TRANSACTION_TIMING offset=", body)
        self.assertIn("ACPHY_IDENTITY_SPAN:", body)

    def test_span_runs_before_the_3e2_probe_and_the_3e0_read(self):
        block_start = SI.index("if (hw->acphy_identity_timing_only) {")
        block = SI[block_start:SI.index("\n\t}\n", block_start)]
        span = block.index("ob_si_d11_identity_span_probe(hw)")
        three_e_two = block.index("ob_si_d11_identity_3e2_probe(hw)")
        three_e_zero = block.index("ob_si_d11_identity_timing(hw)")
        self.assertLess(span, three_e_two)
        self.assertLess(three_e_two, three_e_zero)
        # Fail-stop before anything else is issued.
        self.assertIn("hw->acphy_identity_span_failed = true;", block)
        self.assertLess(block.index("return 0;"), three_e_zero)

    def test_completion_marker_is_suppressed_when_the_span_failed(self):
        # The marker has ONE condition for every fail-stop, so a span stop must
        # set the shared latch that ob_core.c keys the marker on.
        self.assertIn("if (!hw->acphy_identity_early_stop)", CORE)
        body = span_helper()
        self.assertNotIn("early_stop", body)
        block_start = SI.index("if (hw->acphy_identity_timing_only) {")
        block = SI[block_start:SI.index("\n\t}\n", block_start)]
        stop = block.index("hw->acphy_identity_span_failed = true;")
        self.assertIn("hw->acphy_identity_early_stop = true;", block[stop:])

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_span_probe;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_span_failed;", CORE_H)

    def test_harness_enables_the_span_only_on_request(self):
        self.assertIn('"${OB_L2_SPAN_PROBE:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_span_probe=1"', HARNESS)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Structural acceptance for the L2d control modifier (acphy_identity_control_reprobe).

Every PHY-block probe so far (0x03e0, 0x03e2, 0x03e4) was also the first access
after the eight-read bring-up block, so each of them confounded "this address or
block is bad" with "the endpoint stopped answering altogether". Re-reading
D11+0x0120 — an offset that returned a live value microseconds earlier — is the
only access that separates the two.

The modifier is only meaningful if it reads exactly 0x0120, 32 bits wide, as the
FIRST offset access after the block, through the same ob_d11_read32() the
bring-up reads use, and stops at the first stall.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def control_helper() -> str:
    start = SI.index("static bool ob_si_d11_identity_control_reprobe(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentityControlReprobeTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_control_reprobe;", CORE)
        self.assertIn("module_param(acphy_identity_control_reprobe, bool, 0444);", CORE)
        self.assertIn(
            "if (acphy_identity_control_reprobe && !acphy_identity_timing_only)", CORE
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_control_reprobe", select)

    def test_helper_reads_the_known_good_offset_once_and_never_writes(self):
        body = control_helper()
        self.assertEqual(body.count("ob_d11_read32(hw, 0x0120u)"), 1)
        self.assertNotIn("write", body)
        self.assertNotIn("0x03e", body)
        self.assertIn("OB_L2_3E2_STALL_US", body)
        self.assertIn("D11_TRANSACTION_TIMING offset=", body)
        self.assertIn("ACPHY_IDENTITY_CONTROL:", body)

    def test_control_is_the_first_access_after_the_block(self):
        block = l2_block()
        control = block.index("ob_si_d11_identity_control_reprobe(hw)")
        for later in ("ob_si_d11_identity_span_probe(hw)",
                      "ob_si_d11_identity_3e2_probe(hw)",
                      "ob_si_d11_identity_timing(hw)"):
            self.assertLess(control, block.index(later))
        # Fail-stop: the control failure ends the run and sets the shared latch.
        self.assertIn("hw->acphy_identity_control_failed = true;", block)
        stop = block.index("hw->acphy_identity_control_failed = true;")
        self.assertIn("hw->acphy_identity_early_stop = true;", block[stop:])

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_control_reprobe;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_control_failed;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_early_stop;", CORE_H)

    def test_harness_enables_the_control_only_on_request(self):
        self.assertIn('"${OB_L2_CONTROL_REPROBE:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_control_reprobe=1"', HARNESS)


if __name__ == "__main__":
    unittest.main()

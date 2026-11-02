#!/usr/bin/env python3
"""Structural acceptance for the L2e control modifier (acphy_identity_control16_reprobe).

L2d proved a 32-bit read of the known-good D11 offset 0x0120 completes ~6.9 us
before the 0x03e0 access times out, so the endpoint is alive. Every failing
access so far, however, was 16-bit, and no successful 16-bit read exists in the
L2 family — so "the 0x03e0 block" and "16-bit MMIO to this function" stayed
confounded. This probe holds the address fixed and changes only the width, so it
is only meaningful if it:

  * reads exactly 0x0120, 16 bits wide, through ob_d11_read16();
  * is the FIRST access after the eight-read block (ahead of the 32-bit control);
  * never writes;
  * latches device loss on a stall, so the retention path runs instead of
    teardown, and sets the shared fail-stop latch.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def control16_helper() -> str:
    start = SI.index("static bool ob_si_d11_identity_control16_reprobe(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


def l2_block() -> str:
    start = SI.index("if (hw->acphy_identity_timing_only) {")
    return SI[start:SI.index("\n\t}\n", start)]


class AcphyIdentityControl16ReprobeTests(unittest.TestCase):
    def test_modifier_is_a_param_requiring_l2(self):
        self.assertIn("static bool acphy_identity_control16_reprobe;", CORE)
        self.assertIn(
            "module_param(acphy_identity_control16_reprobe, bool, 0444);", CORE
        )
        self.assertIn(
            "if (acphy_identity_control16_reprobe && !acphy_identity_timing_only)",
            CORE,
        )
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_control16_reprobe", select)

    def test_helper_reads_the_same_offset_16_bits_wide_and_never_writes(self):
        body = control16_helper()
        self.assertEqual(body.count("ob_d11_read16(hw, 0x0120u)"), 1)
        self.assertNotIn("ob_d11_read32", body)
        self.assertNotIn("write", body)
        self.assertNotIn("0x03e", body)
        self.assertIn("OB_L2_3E2_STALL_US", body)
        self.assertIn("D11_TRANSACTION_TIMING offset=", body)
        self.assertIn("ACPHY_IDENTITY_CONTROL16:", body)

    def test_a_stall_latches_device_loss_so_no_teardown_runs(self):
        body = control16_helper()
        self.assertIn("hw->dev_lost = true;", body)
        # The latch must be set before the marker reports it.
        self.assertLess(
            body.index("hw->dev_lost = true;"), body.index("ACPHY_IDENTITY_CONTROL16:")
        )

    def test_control16_is_the_first_access_after_the_block(self):
        block = l2_block()
        sixteen = block.index("ob_si_d11_identity_control16_reprobe(hw)")
        thirty_two = block.index("ob_si_d11_identity_control_reprobe(hw)")
        later = [
            block.index(name)
            for name in (
                "ob_si_d11_identity_span_probe(hw)",
                "ob_si_d11_identity_3e2_probe(hw)",
                "ob_si_d11_identity_timing(hw)",
            )
        ]
        self.assertLess(sixteen, thirty_two)
        for position in later:
            self.assertLess(thirty_two, position)
        self.assertIn("hw->acphy_identity_control16_failed = true;", block)
        stop = block.index("hw->acphy_identity_control16_failed = true;")
        self.assertIn("hw->acphy_identity_early_stop = true;", block[stop:])

    def test_state_flags_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_control16_reprobe;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_control16_failed;", CORE_H)

    def test_harness_pairs_the_16bit_control_with_the_32bit_one(self):
        self.assertIn('"${OB_L2_CONTROL16_REPROBE:-0}" = "1"', HARNESS)
        self.assertIn('"acphy_identity_control16_reprobe=1"', HARNESS)
        self.assertIn('"acphy_identity_control_reprobe=1"', HARNESS)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Structural acceptance for the L2b modifier (acphy_identity_3e2_first).

L2b exists to answer one question: is the failure specific to the 16-bit address
0x03e0, or is it dword/window/width-wide? It is therefore only meaningful if it
adds exactly one access — a 16-bit read of 0x03e2, the other half of the same
dword — and does not disturb the validated L2 helper or any default path.

These checks pin that negative side, because the positive side cannot be
observed without the device.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
CORE_H = (ROOT / "src/ob_core.h").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


def helper_body() -> str:
    start = SI.index("static bool ob_si_d11_identity_3e2_probe(struct ob_hw *hw)")
    return SI[start:SI.index("\n}\n", start)]


class AcphyIdentity3e2FirstTests(unittest.TestCase):
    def test_modifier_is_a_param_and_not_a_mode(self):
        self.assertIn("static bool acphy_identity_3e2_first;", CORE)
        self.assertIn(
            "module_param(acphy_identity_3e2_first, bool, 0444);", CORE
        )
        # It must never be handed to the isolated-mode selector: it is a
        # modifier of L2, not another way to reach hardware.
        select = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        select = select[:select.index(");")]
        self.assertNotIn("acphy_identity_3e2_first", select)

    def test_modifier_alone_is_rejected_before_hardware(self):
        guard = CORE.index("if (acphy_identity_3e2_first && !acphy_identity_timing_only)")
        block = CORE[guard:guard + 400]
        self.assertIn("return -EINVAL;", block)
        # Fail closed before allocation and before the mode policy dispatch.
        policy = CORE.index("/* Explicit mode policy: at most one isolated mode may be selected. */")
        self.assertLess(guard, policy)

    def test_flag_reaches_hw_only_under_the_l2_mode(self):
        assign = CORE.index("hw->acphy_identity_3e2_first =")
        stmt = CORE[assign:CORE.index(";", assign)]
        self.assertIn("mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING", stmt)

    def test_helper_is_exactly_one_16bit_read_of_3e2_and_no_write(self):
        body = helper_body()
        self.assertEqual(body.count("ob_d11_read16(hw, 0x03e2u)"), 1)
        self.assertNotIn("0x03e0", body)          # never reads the target first
        self.assertNotIn("write", body)           # no write of any kind
        self.assertIn("D11_TRANSACTION_TIMING offset=", body)
        self.assertIn("ACPHY_IDENTITY_3E2:", body)

    def test_fail_stop_precedes_the_3e0_read(self):
        block_start = SI.index("if (hw->acphy_identity_timing_only) {")
        block = SI[block_start:SI.index("\n\t}\n", block_start)]
        self.assertIn("if (hw->acphy_identity_3e2_first &&", block)
        self.assertIn("hw->acphy_identity_3e2_failed = true;", block)
        # The 0x3e0 call must sit after the fail-stop return, i.e. it is only
        # reached when 0x3e2 came back healthy.
        stop = block.index("return 0;")
        self.assertGreater(block.index("ob_si_d11_identity_timing(hw);"), stop)

    def test_validated_l2_helper_is_untouched(self):
        start = SI.index("static void ob_si_d11_identity_timing(struct ob_hw *hw)")
        l2 = SI[start:SI.index("\n}\n", start)]
        self.assertEqual(l2.count("ob_d11_read16(hw, 0x03e0u)"), 1)
        self.assertNotIn("03e2", l2)

    def test_harness_passes_the_modifier_only_on_request(self):
        self.assertIn('"${OB_L2_3E2_FIRST:-0}" = "1"', HARNESS)
        self.assertIn('insmod "$KO" "${MODE}=1" "acphy_identity_3e2_first=1"', HARNESS)
        # The default (env unset) insmod line is unchanged.
        self.assertIn('insmod "$KO" "${MODE}=1"\nfi', HARNESS)

    def test_state_flags_are_declared(self):
        self.assertIn("bool\t\t\tacphy_identity_3e2_first;", CORE_H)
        self.assertIn("bool\t\t\tacphy_identity_3e2_failed;", CORE_H)


if __name__ == "__main__":
    unittest.main()

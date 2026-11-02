#!/usr/bin/env python3
"""Structural acceptance for the endpoint-loss bisection L2 mode.

L2 is only scientifically useful if it differs from L1 by exactly one layer.
These checks therefore assert the *negative* side of the contract as hard as
the positive one: the attach prefix must be absent, the firmware upload must
be absent, and nothing may sit between the eight bring-up reads and the single
0x3e0 read.
"""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CORE = (ROOT / "src/ob_core.c").read_text(encoding="utf-8")
SI = (ROOT / "src/ob_si.c").read_text(encoding="utf-8")
UCODE_H = (ROOT / "src/ob_ucode.h").read_text(encoding="utf-8")
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text(encoding="utf-8")


class AcphyIdentityTimingTests(unittest.TestCase):
    def test_mode_is_isolated_and_cannot_coexist_with_attach(self):
        self.assertIn("acphy_identity_timing_only", CORE)
        self.assertIn("OB_ISOLATED_ACPHY_IDENTITY_TIMING", UCODE_H)
        # Mutual exclusion with every other isolated mode: no silent upgrade
        # of the bounded L2 stage into the full attach sequence.
        self.assertIn(
            "if (acphy_identity_timing_only && mode != OB_ISOLATED_NONE)",
            UCODE_H,
        )
        self.assertIn("return acphy_identity_timing_only ?", UCODE_H)
        call = CORE[CORE.index("mode = ob_isolated_mode_select25("):]
        call = call[:call.index(");")]
        self.assertIn("acphy_identity_timing_only", call)

    def test_mode_stops_before_attach_identity_and_firmware(self):
        stop = CORE.index("if (mode == OB_ISOLATED_ACPHY_IDENTITY_TIMING) {")
        block = CORE[stop:stop + 260]
        self.assertIn("ACPHY_IDENTITY_TIMING_COMPLETE", block)
        self.assertIn("return 0;", block)
        # The stop must precede the shared attach-identity call.
        attach = CORE.index("ret = ob_acphy_attach_identity(hw);", stop)
        self.assertLess(stop, attach)
        # And the flag must never reach the firmware path.
        self.assertNotRegex(
            CORE, r"acphy_identity_timing_only.*?ob_fw_probe\(hw\)"
        )

    def test_one_phyversion_read_adjacent_to_the_eight(self):
        probe = SI[SI.index("int ob_si_probe(struct ob_hw *hw)"):]
        # Adjacency is the whole point: no access may sit between the eight
        # timing reads and the L2 read.
        after_diag = probe.index("ob_si_d11_diag(hw);") + len(
            "ob_si_d11_diag(hw);"
        )
        at_l2 = probe.index("ob_si_d11_identity_timing(hw);")
        between = probe[after_diag:at_l2]
        for forbidden in (
            "ob_axi_read", "ob_axi_write", "ob_d11_read", "ob_d11_write",
            "ob_cc_read", "ob_cc_write", "bcma_aread", "bcma_awrite",
        ):
            self.assertNotIn(forbidden, between)

    def test_helper_performs_exactly_one_16bit_read_and_no_write(self):
        start = SI.index("static void ob_si_d11_identity_timing(struct ob_hw *hw)")
        helper = SI[start:SI.index("\n}\n", start)]
        self.assertEqual(helper.count("ob_d11_read16(hw, 0x03e0u)"), 1)
        self.assertNotIn("0x0408", helper)          # no wrapper access
        self.assertNotIn("write", helper)           # no write of any kind
        self.assertNotIn("ob_radio_prepare_for_identity(hw)", helper)
        self.assertIn("D11_TRANSACTION_TIMING offset=", helper)

    def test_validated_d11_timing_engine_is_unmodified(self):
        # L2 must not satisfy itself by widening the already validated mode.
        body = SI[SI.index("static void ob_si_d11_diag(struct ob_hw *hw)"):
                  SI.index("/* Read-only wrapper bracket",
                           SI.index("static void ob_si_d11_diag(struct ob_hw *hw)"))]
        self.assertEqual(body.count("ob_d11_read32("), 8)
        self.assertNotIn("ob_d11_read16", body)
        self.assertNotIn("03e0", body)

    def test_remove_keeps_the_device_bound_without_teardown(self):
        rm = CORE[CORE.index("void ob_remove(struct bcma_device *core)"):]
        self.assertIn("hw->acphy_identity_timing_only", rm)
        self.assertIn("acphy identity timing L2", rm)

    def test_harness_requires_l2_markers_and_rejects_l1_trace(self):
        allowed = HARNESS.split("ALLOWED_MODES=")[1].split("\n")[0]
        self.assertIn("acphy_identity_timing_only", allowed)
        start = HARNESS.index("acphy_identity_timing_markers_complete() {")
        fn = HARNESS[start:HARNESS.index("\n}\n", start)]
        self.assertIn("ACPHY_IDENTITY_TIMING_COMPLETE", fn)
        self.assertIn("ACPHY_IDENTITY_TIMING: phyver=", fn)
        for off in ("0120", "0124", "0128", "012c",
                    "0154", "0158", "0180", "0184", "03e0"):
            self.assertIn(" %s" % off, fn)
        self.assertIn('"D11_TRANSACTION_TIMING offset=0x$off"', fn)
        # A run that executed L1 by accident must be rejected, not graded.
        self.assertIn("OB_ATTACH_CP0_ENTRY", fn)
        self.assertIn("this is L1, not L2", fn)


if __name__ == "__main__":
    unittest.main()

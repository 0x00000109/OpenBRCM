#!/usr/bin/env python3
"""Source contract for the isolated OTP word-0x10 observation."""

import pathlib
import re
import unittest


REPO = pathlib.Path(__file__).resolve().parents[2]


class Otp8e2ModeContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (REPO / "src" / "ob_si.c").read_text(encoding="utf-8")
        cls.header = (REPO / "src" / "ob_otp8e2.h").read_text(encoding="utf-8")

    def test_word10_reader_is_single_and_uses_historical_type1_divisor(self):
        self.assertIn("OB_OTP8E2_WORD_INDEX\t0x10u", self.header)
        self.assertIn("OB_OTP8E2_COLUMNS\t0x20u", self.header)
        self.assertEqual(self.source.count("ob_otp8e2_observe_word10("), 1)
        self.assertEqual(self.source.count(
            "ob_otp_read_word16(observation->hw, columns, word_index, word)"), 1)
        self.assertIn("word_index != OB_OTP8E2_WORD_INDEX", self.source)
        self.assertIn("columns != OB_OTP8E2_COLUMNS", self.source)

    def test_observation_is_gated_to_exact_postbmac_target_mode(self):
        probe = re.search(
            r"static int ob_si_otp8e2_probe\(.*?\n}\n", self.source, re.S)
        self.assertIsNotNone(probe, "missing isolated OTP8E2 probe")
        body = probe.group(0)
        for required in (
            "hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only",
            "hw->chip_id != 0x4352",
            "hw->chip_rev != 3",
            "hw->core->id.id != BCMA_CORE_80211",
            "hw->core->id.rev != 42",
            "OB_CC_CAP_OTPSIZE_MASK",
            "OB_OTPL_WRAP_TYPE_40NM",
            "ACPHY_OTP8E2_OBS word=0x%04x value=0x%02x jbe_le3=%u",
            "ACPHY_OTP8E2_FAIL stage=%s ret=%d",
        ):
            self.assertIn(required, body)
        self.assertNotIn("hwbase + 0x26", body)
        self.assertNotIn("0xb021d", body.lower())
        self.assertNotIn("0xb0234", body.lower())

    def test_probe_dispatch_keeps_legacy_diag_out_of_new_mode(self):
        probe = re.search(r"int ob_si_probe\(.*?\n}\n", self.source, re.S)
        self.assertIsNotNone(probe)
        body = probe.group(0)
        mode_gate = body.index(
            "if (hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only)")
        new_probe = body.index("sprom_ret = ob_si_otp8e2_probe(hw);")
        legacy_diag = body.index("else if (otp_diag)")
        self.assertLess(mode_gate, new_probe)
        self.assertLess(new_probe, legacy_diag)
        self.assertEqual(body.count("ob_si_otp8e2_probe(hw)"), 1)
        self.assertEqual(body.count("ob_si_otp_diag(hw)"), 1)

    def test_otp_bit_reader_fails_immediately_after_loss(self):
        reader = re.search(
            r"static int ob_otp_read_bit\(.*?\n}\n", self.source, re.S)
        self.assertIsNotNone(reader)
        body = reader.group(0)
        self.assertGreaterEqual(body.count("if (hw->dev_lost)"), 5)


if __name__ == "__main__":
    unittest.main()

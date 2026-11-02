#!/usr/bin/env python3
"""Structural acceptance for the bounded D11 transaction-timing mode."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SI = (ROOT / "src/ob_si.c").read_text()
CORE = (ROOT / "src/ob_core.c").read_text()
UCODE_H = (ROOT / "src/ob_ucode.h").read_text()
HARNESS = (ROOT / "scripts/runtime-test.sh").read_text()


class D11TransactionTimingTests(unittest.TestCase):
    def test_mode_isolated_and_stops_before_firmware(self):
        self.assertIn("d11_transaction_timing_only", CORE)
        self.assertIn("OB_ISOLATED_D11_TRANSACTION_TIMING", UCODE_H)
        self.assertIn("if (mode == OB_ISOLATED_D11_TRANSACTION_TIMING) {", CORE)
        self.assertIn(
            '"D11_TRANSACTION_TIMING_COMPLETE: stopped before firmware/ucode/radio',
            CORE,
        )
        self.assertNotRegex(
            CORE,
            r"d11_transaction_timing_only.*?ob_fw_probe\(hw\)",
        )

    def test_exact_eight_early_reads_without_phyversion(self):
        body = re.search(
            r"static void ob_si_d11_diag\(struct ob_hw \*hw\)\n\{(.*?)\n\}",
            SI,
            re.S,
        ).group(1)
        self.assertEqual(len(re.findall(r"ob_d11_read(?:16|32)\(", body)), 8)
        self.assertEqual(body.count("ktime_get_mono_fast_ns()"), 16)
        self.assertNotIn("OB_D11_PHYVERSION", body)
        self.assertNotIn("ob_d11_read16", body)
        self.assertNotIn("D11_PHYVER_", body)
        for offset in (
            "OB_D11_MACCONTROL", "OB_D11_MACCOMMAND", "OB_D11_MACINTSTATUS",
            "OB_D11_MACINTMASK", "OB_D11_PSMDEBUG", "OB_D11_PHYDEBUG",
            "OB_D11_TSF_LOW", "OB_D11_TSF_HIGH",
        ):
            self.assertIn(offset, body)

    def test_no_extra_wrapper_and_existing_fail_stop_remain(self):
        bracket = re.search(
            r"int ob_si_d11_enable_bracket\(.*?\n\}", SI, re.S
        ).group(0)
        self.assertEqual(bracket.count("ob_axi_read32"), 2)
        self.assertIn("return -EIO", bracket)
        self.assertIn("duration_us", bracket)
        self.assertNotIn("ob_d11_read", bracket)
        self.assertIn("D11_ENABLE_BRACKET_SI_PROBE_EXIT", SI)

    def test_harness_has_timing_mode(self):
        self.assertIn("d11_transaction_timing_only", HARNESS)
        self.assertIn("D11_TRANSACTION_TIMING_COMPLETE", HARNESS)


if __name__ == "__main__":
    unittest.main()

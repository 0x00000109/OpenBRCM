#!/usr/bin/env python3
"""Host tests for removal of the unjustified early PHYVERSION read."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SI = (ROOT / "src" / "ob_si.c").read_text(encoding="utf-8")


class D11PhyversionBoundaryTests(unittest.TestCase):
    def test_normal_diag_has_no_early_phyversion_access_or_snapshot(self):
        body = SI[SI.index("static void ob_si_d11_diag"):SI.index(
            "/* Read-only wrapper bracket", SI.index("static void ob_si_d11_diag")
        )]
        self.assertEqual(body.count("ob_d11_read32("), 8)
        self.assertNotIn("ob_d11_read16", body)
        self.assertNotIn("OB_D11_PHYVERSION", body)
        self.assertNotIn("D11_PHYVER_", body)

    def test_later_authoritative_identity_read_is_preserved(self):
        lifecycle = (ROOT / "src" / "ob_lifecycle.c").read_text(encoding="utf-8")
        identity = lifecycle[lifecycle.index("int ob_acphy_attach_identity") :]
        self.assertIn("ob_radio_prepare_for_identity(hw)", identity)
        self.assertIn("phyver = ob_d11_read16(hw, 0x03e0u)", identity)
        self.assertIn("OB_D11_CORE_READY", identity)
        self.assertIn("OB_D11_ATTACH_IDENTITY_READY", identity)

    def test_early_diag_has_no_wrapper_write_or_recovery_path(self):
        body = SI[SI.index("static void ob_si_d11_diag"):SI.index(
            "/* Read-only wrapper bracket", SI.index("static void ob_si_d11_diag")
        )]
        for forbidden in ("bcma_awrite32", "ob_axi_write32", "bcma_write32",
                          "bcma_write16", "udelay", "msleep"):
            self.assertNotIn(forbidden, body)
        self.assertNotIn("ob_d11_read16", body)
        self.assertNotIn("ob_axi_write32", body)
        self.assertNotIn("bcma_awrite32", body)
        self.assertNotIn("udelay", body)
        self.assertNotIn("msleep", body)


if __name__ == "__main__":
    unittest.main()

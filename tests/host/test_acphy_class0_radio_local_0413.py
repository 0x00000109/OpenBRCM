#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Offline contract tests for the class-0 radio-local 0x413 checkpoint."""

import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "runtime-test.sh"
RADIO = (ROOT / "src" / "ob_radio.h").read_text(encoding="utf-8")
RADIO_C = (ROOT / "src" / "ob_radio.c").read_text(encoding="utf-8")
CORE = (ROOT / "src" / "ob_core.c").read_text(encoding="utf-8")
UCODE = (ROOT / "src" / "ob_ucode.h").read_text(encoding="utf-8")


PASS_LOG = """\
ACPHY_CLASS0_0413_CP0_ENTRY
ACPHY_CLASS0_0413_TARGET_OK
ACPHY_CLASS0_0413_MAC_QUIESCE result=ALREADY_QUIESCED polls=0
ACPHY_CLASS0_0413_PRE_MUTATION
ACPHY_CLASS0_0413_POLL attempts=1 last_value=0x0010 success=1
ACPHY_CLASS0_0413_PASS
ACPHY_CLASS0_0413_STOP
"""

FAIL_LOG = """\
ACPHY_CLASS0_0413_CP0_ENTRY
ACPHY_CLASS0_0413_TARGET_OK
ACPHY_CLASS0_0413_MAC_QUIESCE result=ALREADY_QUIESCED polls=0
ACPHY_CLASS0_0413_PRE_MUTATION
ACPHY_CLASS0_0413_POLL attempts=100 last_value=0x0000 success=0
ACPHY_CLASS0_0413_FAIL stage=RADIO_0413_TIMEOUT ret=-110
ACPHY_CLASS0_0413_STOP
"""


class Class0RadioLocal0413Tests(unittest.TestCase):
    def evaluate(self, text):
        path = ROOT / "tests" / "host" / ".class0-0413.log"
        path.write_text(text, encoding="utf-8")
        try:
            return subprocess.run(
                [str(SCRIPT), "--evaluate-log",
                 "acphy_class0_radio_local_0413_test_only", str(path)],
                cwd=ROOT, capture_output=True, text=True, check=False,
            )
        finally:
            path.unlink(missing_ok=True)

    def test_pass_parser_preserves_state(self):
        result = self.evaluate(PASS_LOG)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("CLEAN PASS (radio state preserved; no rmmod)", result.stdout)

    def test_timeout_parser_is_diagnostic_fail_and_preserves_state(self):
        result = self.evaluate(FAIL_LOG)
        self.assertEqual(result.returncode, 4, result.stdout + result.stderr)
        self.assertIn("DIAGNOSTIC FAIL (radio state preserved; no rmmod)", result.stdout)

    def test_mode_is_separate_and_target_gate_is_exact(self):
        self.assertIn("OB_ISOLATED_ACPHY_CLASS0_0413_TEST", CORE)
        self.assertIn("radio_revision_class == 0u", RADIO)
        self.assertIn("target->chanspec == 0x1001u", RADIO)
        self.assertIn("OB_RADIO_BCM2069_ID", RADIO)

    def test_exact_sequence_and_stop_boundary(self):
        body = RADIO[ RADIO.index("static inline int ob_radio_local_0413_run") : ]
        expected = [
            "0x08eau, 0x0080u, 0x0080u",
            "0x08edu, 0x0600u, 0x0400u",
            "0x0410u, 0x1000u, 0x1000u",
            "0x0410u, 0x0018u, 0x00e0u",
            "0x0411u, 0xff00u, 0x1c00u",
            "ops->write16(ctx, 0x0412u, 0x014au)",
            "0x0410u, 0x0001u, 0x0000u",
            "0x0410u, 0x0001u, 0x0001u",
            "0x0411u, 0x0001u, 0x0001u",
            "ops->read16(ctx, 0x0413u, &value)",
        ]
        positions = [body.index(item) for item in expected]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("OB_RADIO_LOCAL_0413_MAX_ATTEMPTS", body)
        self.assertIn("OB_RADIO_LOCAL_0413_POLL_DELAY_US", body)
        self.assertNotIn("0x0414u", body)
        self.assertNotIn("0x0415u", body)
        self.assertNotIn("ob_radio2069_rev4_synth", body)

    def test_no_old_mode_extension_or_normal_probe_integration(self):
        self.assertIn("if (mode == OB_ISOLATED_ACPHY_PRE_RADIO_TEST)", CORE)
        self.assertIn("if (mode == OB_ISOLATED_ACPHY_CLASS0_0413_TEST)", CORE)
        self.assertIn("ob_radio_local_0413_test(hw)", CORE)
        # The selector's generation suffix advances whenever a mode is added
        # (select22 -> select24 so far). What must hold is that ob_probe()
        # calls the current selector and that the selector carries this mode's
        # option; the literal generation number is not the contract.
        self.assertRegex(CORE, r"ob_isolated_mode_select\d+\(")
        self.assertRegex(
            UCODE,
            r"ob_isolated_mode_select\d+\([\s\S]*?"
            r"acphy_class0_radio_local_0413_test_only",
        )
        self.assertRegex(
            UCODE,
            r"ob_isolated_mode_select22\([\s\S]*?"
            r"ob_isolated_mode_select21\(",
        )
        self.assertIn("ob_isolated_mode_select16", UCODE)
        self.assertIn("acphy_class0_radio_local_0413_test_only", SCRIPT.read_text(encoding="utf-8"))

    def test_no_raw_bar_or_recovery_path(self):
        self.assertNotIn("pci_iomap", RADIO_C[RADIO_C.index("int ob_radio_local_0413_test") :])
        self.assertNotIn("ob_axi_write32", RADIO_C[RADIO_C.index("int ob_radio_local_0413_test") :])
        start = RADIO_C.index("int ob_radio_local_0413_test")
        end = RADIO_C.index("static int ob_radio_checked_read16", start)
        body = RADIO_C[start:end]
        self.assertNotIn("ob_d11_enable_mac", body)


if __name__ == "__main__":
    unittest.main()

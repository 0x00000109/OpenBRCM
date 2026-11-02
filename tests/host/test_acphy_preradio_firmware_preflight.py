#!/usr/bin/env python3
"""Regression contract for the ACPHY pre-radio firmware preflight."""

import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PREFLIGHT = ROOT / "scripts" / "acphy-pre-radio-firmware-preflight.sh"
SYSTEM_FIRMWARE = Path("/lib/firmware/brcm")


class AcphyPreRadioFirmwarePreflightTests(unittest.TestCase):
    def test_runtime_harness_runs_preflight_before_insmod(self):
        harness = (ROOT / "scripts" / "runtime-test.sh").read_text(
            encoding="utf-8"
        )
        preflight = 'acphy-pre-radio-firmware-preflight.sh'
        self.assertIn(preflight, harness)
        self.assertLess(harness.index(preflight), harness.index("# --- load ("))

    def copy_required_fallback_set(self, root: Path) -> None:
        for name in (
            "bcm43xx-ucode.fw",
            "bcm4352-d11ac1initvals42.bin",
            "bcm4352-d11ac1bsinitvals42.bin",
        ):
            shutil.copyfile(SYSTEM_FIRMWARE / name, root / name)

    def run_preflight(self, root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(PREFLIGHT), "--firmware-dir", str(root)],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_missing_primary_with_valid_legacy_fallback_passes(self):
        """The runtime fallback path, not the preferred filename, is required."""
        with tempfile.TemporaryDirectory() as temp:
            firmware = Path(temp)
            self.copy_required_fallback_set(firmware)

            result = self.run_preflight(firmware)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("UCODE_ARTIFACT_USED=brcm/bcm43xx-ucode.fw", result.stdout)
        self.assertIn("FIRMWARE_PREFLIGHT: PASS", result.stdout)

    def test_invalid_legacy_fallback_fails_closed(self):
        with tempfile.TemporaryDirectory() as temp:
            firmware = Path(temp)
            self.copy_required_fallback_set(firmware)
            (firmware / "bcm43xx-ucode.fw").write_bytes(b"invalid")

            result = self.run_preflight(firmware)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("FIRMWARE_PREFLIGHT: FAIL", result.stdout)


if __name__ == "__main__":
    unittest.main()

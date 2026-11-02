#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Guard the kernel and checkpatch inputs used by the GitHub build matrix."""

import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/build.yml"
OB_DIAG_ADDRESSING = ROOT / "tests/host/test_ob_diag_addressing.py"
RE_BOOTSTRAP = ROOT / "scripts/re-bootstrap.sh"


class CiReproducibilityTests(unittest.TestCase):
    def test_ob_diag_preprocessor_runs_from_hosttest_working_directory(self):
        result = subprocess.run(
            ["python3", str(OB_DIAG_ADDRESSING)],
            cwd=ROOT / "tests/host", capture_output=True, text=True,
        )
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("preprocessed scan: no backplane address", output)
        self.assertNotIn("preprocessed scan skipped: preprocessor failed", output)

    def test_re_bootstrap_pins_and_checks_the_tooling_commit(self):
        script = RE_BOOTSTRAP.read_text(encoding="utf-8")
        self.assertIn("EXPECTED_TOOLING_COMMIT", script)
        self.assertIn('git -C "$TOOLING_ROOT" rev-parse HEAD', script)
        self.assertIn('git -C "$TOOLING_ROOT" diff --quiet', script)

    def test_kernel_archives_are_checksum_pinned(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        for digest in (
            "bb7f6d80b387c757b7d14bb93028fcb90f793c5c0d367736ee815a100b3891f0",
            "f9fef3d14c0df53819026f4be74459835c2a0b0dcbf5b5bbd9ea19f0829402b3",
        ):
            self.assertIn(digest, workflow)
        self.assertIn("sha256sum --check", workflow)

    def test_checkpatch_comes_from_the_pinned_kernel_source(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("/tmp/linux/scripts/checkpatch.pl", workflow)
        self.assertNotIn("raw.githubusercontent.com/torvalds/linux/master", workflow)
        self.assertIn("::warning::checkpatch reported findings", workflow)
        self.assertNotIn("|| true", workflow)


if __name__ == "__main__":
    unittest.main()

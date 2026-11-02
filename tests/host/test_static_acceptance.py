#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Contract tests for the ordered local static acceptance runner."""

import pathlib
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/static-acceptance.sh"


class StaticAcceptanceTests(unittest.TestCase):
    def test_list_names_every_required_gate_in_execution_order(self):
        result = subprocess.run(
            ["sh", str(RUNNER), "--list"],
            cwd=ROOT, capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = [
            "scripts/re-bootstrap.sh",
            "cargo build --release --manifest-path",
            "scripts/verify_object_edges.py",
            "scripts/verify_induction.py",
            "re regress --db",
            "make hosttest",
            "make step-ledger-test",
            "scripts/docs-check.sh",
            "generate-current-context.py --check",
            "generate-current-context.py --validate",
            "git diff --check",
        ]
        positions = [result.stdout.index(item) for item in expected]
        self.assertEqual(positions, sorted(positions), result.stdout)


if __name__ == "__main__":
    unittest.main()

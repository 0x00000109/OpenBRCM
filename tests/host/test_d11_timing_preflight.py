#!/usr/bin/env python3
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GATE = ROOT / "scripts" / "d11-timing-preflight.py"
EXPECTED = {
    "kernel": "7.0.0-38-generic",
    "worktree_head": "04b289f67bf68e6e62a8bac990a9b3b0a30453b2",
    "worktree_tree": "59518eb5e2425ca39d3688ab859678aa47340d7f",
    "signed_sha256": "0cf60fc2d4f6bad24a848a0e5a53fed889735709d9d15c1ef6739ad4112d726",
    "unsigned_sha256": "2636945154f8628aaca465362c9df514a6078e5cbb27eb0e7e14b8d572d48dc9",
    "pci_identity": "0000:21:00.0 / 14e4:43b1",
    "pci_parent": "bcma-pci-bridge",
    "openbrcm": "absent",
    "b43": "absent",
    "d11_owner": "unbound",
    "d11_manuf": "0x4BF",
    "d11_id": "0x812",
    "d11_rev": "0x2A",
    "harness_mode": "supported",
    "module_parameter": "supported",
}


class D11TimingPreflightTests(unittest.TestCase):
    def run_gate(self, values):
        with tempfile.TemporaryDirectory() as td:
            fixture = Path(td) / "fixture.json"
            fixture.write_text(json.dumps(values), encoding="utf-8")
            return subprocess.run(
                ["python3", str(GATE), "--fixture", str(fixture)],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )

    def test_all_predicates_pass_without_harness_invocation(self):
        result = self.run_gate(EXPECTED)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PREFLIGHT_RESULT=PASS", result.stdout)
        self.assertNotIn("runtime-test.sh", result.stdout)
        self.assertEqual(result.stdout.count(" result=PASS"), 15)

    def test_each_fixture_stops_at_one_named_first_failure(self):
        failures = {
            "kernel": "6.8.0",
            "worktree_head": "wrong-head",
            "worktree_tree": "wrong-tree",
            "signed_sha256": "wrong-signed",
            "unsigned_sha256": "wrong-unsigned",
            "pci_identity": "0000:21:00.0 / 14e4:ffff",
            "pci_parent": "other",
            "openbrcm": "present",
            "b43": "present",
            "d11_owner": "bound",
            "d11_manuf": "0x999",
            "d11_id": "0x999",
            "d11_rev": "0x99",
            "harness_mode": "unsupported",
            "module_parameter": "missing",
        }
        for name, actual in failures.items():
            with self.subTest(name=name):
                values = dict(EXPECTED)
                values[name] = actual
                result = self.run_gate(values)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"PREFLIGHT_FIRST_FAILURE={name}", result.stdout)
                self.assertIn(f"actual={actual}", result.stdout)
                self.assertIn("PREFLIGHT_RESULT=FAIL", result.stdout)
                self.assertNotIn("runtime-test.sh", result.stdout)
                self.assertEqual(
                    result.stdout.count("PREFLIGHT_FIRST_FAILURE="), 1
                )


if __name__ == "__main__":
    unittest.main()

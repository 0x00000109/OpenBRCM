#!/usr/bin/env python3
"""Offline contract tests for the rebooted stand radio-checkpoint runner."""

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts" / "stand-radio-checkpoint-run.sh"
SHA = "a" * 64


def run(*args):
    return subprocess.run(["sh", str(RUNNER), *args], cwd=ROOT,
                          capture_output=True, text=True)


class StandRadioCheckpointRunnerTests(unittest.TestCase):
    def test_dry_run_declares_one_reboot_and_one_postlpf_insertion(self):
        result = run("--dry-run", "--candidate-sha256", SHA)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("systemctl reboot", result.stdout)
        self.assertEqual(result.stdout.count("systemctl reboot"), 1)
        self.assertIn("acphy_radio_postlpf_test_only", result.stdout)
        self.assertIn("acphy_table_probe=0", result.stdout)
        self.assertIn("acphy_a7089_golden_cap=0", result.stdout)
        self.assertIn("exactly one candidate insertion", result.stdout)
        self.assertIn("no rmmod openbrcm", result.stdout)

    def test_dry_run_declares_a_bounded_candidate_run(self):
        result = run("--dry-run", "--candidate-sha256", SHA)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("timeout 180", result.stdout)

    def test_runner_waits_for_terminal_log_collection_without_another_reboot(self):
        source = RUNNER.read_text(encoding="utf-8")
        self.assertIn("wait_for_terminal_connection", source)
        self.assertIn("post-run-state-unavailable.txt", source)
        self.assertNotIn("systemctl reboot", source.split("say \"candidate run\"")[1])

    def test_rejects_non_sha256_candidate_identity(self):
        result = run("--dry-run", "--candidate-sha256", "not-a-sha")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("64 lowercase hexadecimal", result.stderr)


if __name__ == "__main__":
    unittest.main()

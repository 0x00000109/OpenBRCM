#!/usr/bin/env python3
"""Host tests for scripts/runtime-test.sh argument parsing and --dry-run.

These tests never load/unload the module and never touch hardware; they only
exercise the CLI (--help, --dry-run) as an unprivileged user.
"""
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts" / "runtime-test.sh"

MODES = [
    "fw_validate_only",
    "ucode_test_only",
    "initvals_test_only",
    "dma_test_only",
    "d11_tail_test_only",
    "sprom_evidence_only",
    "bsinitvals_test_only",
]


def run(*args):
    return subprocess.run(
        ["bash", str(SCRIPT), *args],
        cwd=str(REPO),
        capture_output=True,
        text=True,
    )


def head_rev():
    return subprocess.check_output(
        ["git", "-C", str(REPO), "rev-parse", "HEAD"], text=True
    ).strip()


class RuntimeTestCli(unittest.TestCase):
    def test_help_lists_all_modes(self):
        r = run("--help")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Usage:", r.stdout)
        for m in MODES:
            self.assertIn(m, r.stdout)

    def test_missing_mode_rejected(self):
        r = run("--dry-run")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("exactly one --mode", r.stderr)

    def test_invalid_mode_rejected(self):
        r = run("--dry-run", "--mode", "bogus")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("invalid --mode", r.stderr)

    def test_unknown_option_rejected(self):
        r = run("--dry-run", "--bogus")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("unknown option", r.stderr)

    def test_requires_argument(self):
        for flag in ("--mode", "--candidate"):
            r = run("--dry-run", flag)
            self.assertNotEqual(r.returncode, 0, flag)
            self.assertIn("requires an argument", r.stderr)

    def test_dry_run_uses_mode_unity_and_never_bare_insmod(self):
        for m in MODES:
            r = run("--dry-run", "--mode", m)
            self.assertEqual(r.returncode, 0, (m, r.stderr))
            self.assertIn('insmod', r.stdout)
            self.assertIn(f'"{m}=1"', r.stdout)
            # no printed insmod line may lack the mode=1 argument
            for line in r.stdout.splitlines():
                if line.lstrip().startswith("insmod"):
                    self.assertIn(f"{m}=1", line, f"bare insmod for {m}: {line!r}")

    def test_dry_run_prints_dependency_commands(self):
        r = run("--dry-run", "--mode", "dma_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        for dep in ("cfg80211", "mac80211", "bcma"):
            self.assertIn(f"modprobe {dep}", r.stdout)

    def test_dry_run_does_not_require_root(self):
        r = run("--dry-run", "--mode", "dma_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("must run as root", r.stderr)
        self.assertNotIn("REBOOT REQUIRED", r.stdout)

    def test_mode_equals_form(self):
        r = run("--dry-run", "--mode=initvals_test_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn('"initvals_test_only=1"', r.stdout)

    def test_positional_module_path(self):
        r = run("--dry-run", "--mode", "ucode_test_only", "/tmp/openbrcm-x.ko")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("/tmp/openbrcm-x.ko", r.stdout)

    def test_candidate_match_passes(self):
        r = run("--dry-run", "--mode", "ucode_test_only", "--candidate", head_rev())
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_candidate_mismatch_aborts(self):
        parent = subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "HEAD~1"], text=True
        ).strip()
        r = run(
            "--dry-run",
            "--mode",
            "ucode_test_only",
            "--candidate",
            parent,
        )
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("mismatch", r.stderr)

    def test_candidate_unresolvable_aborts(self):
        r = run("--dry-run", "--mode", "ucode_test_only", "--candidate", "not-a-rev")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("cannot resolve", r.stderr)


if __name__ == "__main__":
    sys.exit(0 if unittest.main(verbosity=2).result.wasSuccessful() else 1)

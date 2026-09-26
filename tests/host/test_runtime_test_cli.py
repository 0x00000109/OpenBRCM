#!/usr/bin/env python3
"""Host tests for scripts/runtime-test.sh argument parsing and --dry-run.

These tests never load/unload the module and never touch hardware; they only
exercise the CLI (--help, --dry-run) as an unprivileged user.
"""
import subprocess
import sys
import tempfile
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
    "radio_id_probe_only",
]

# A complete, clean radio_id_probe_only current-run log (exact marker strings).
RADIO_CLEAN = """\
radio-probe: BEGIN
radio-probe: prep host up (host_is_pcie2=1)
radio-probe: clkctlst=070b0042 HAVEHT=1 core_enabled=1
radio-probe: ioctl=00000010 mpclke=1
radio-probe: pre_access=0x04020402
radio-probe: reg0_raw=0x0010
radio-probe: reg1_raw=0x2069
radio-probe: radioid=0x2069
radio-probe: radiorev=0x10
radio-probe: revision_class=1
radio-probe: id_accepted=1 id_is_2069=1
radio-probe: pll_branch=A
radio-probe: post_access=0x04020402
radio-probe: PASS
radio-probe: STOPPED BEFORE PLL/RADIO INIT
"""


def run(*args):
    return subprocess.run(
        ["bash", str(SCRIPT), *args],
        cwd=str(REPO),
        capture_output=True,
        text=True,
    )


def evaluate(mode, text, marker=None):
    with tempfile.NamedTemporaryFile("w", suffix=".log", delete=False) as fh:
        fh.write(text)
        path = fh.name
    args = ["--evaluate-log", mode, path]
    if marker:
        args += ["--marker", marker]
    return run(*args)


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

    # ---- radio_id_probe_only --------------------------------------------

    def test_radio_mode_accepted_by_cli(self):
        r = run("--help")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("radio_id_probe_only", r.stdout)

    def test_radio_dry_run_emits_exactly_one_isolated_insmod(self):
        r = run("--dry-run", "--mode", "radio_id_probe_only")
        self.assertEqual(r.returncode, 0, r.stderr)
        insmods = [
            ln for ln in r.stdout.splitlines()
            if ln.lstrip().startswith("insmod")
        ]
        self.assertEqual(len(insmods), 1, r.stdout)
        self.assertIn("radio_id_probe_only=1", insmods[0])

    def test_radio_candidate_mismatch_aborts_before_hardware(self):
        parent = subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "HEAD~1"], text=True
        ).strip()
        r = run("--dry-run", "--mode", "radio_id_probe_only",
                "--candidate", parent)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("mismatch", r.stderr)

    def test_radio_success_markers_permit_rmmod(self):
        r = evaluate("radio_id_probe_only", RADIO_CLEAN)
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_radio_missing_marker_prohibits_rmmod(self):
        text = "".join(
            ln for ln in RADIO_CLEAN.splitlines(True)
            if "post_access=" not in ln
        )
        r = evaluate("radio_id_probe_only", text)
        self.assertEqual(r.returncode, 4, (r.stdout, r.stderr))
        self.assertIn("INCOMPLETE", r.stdout)

    def test_radio_dev_lost_prohibits_rmmod(self):
        text = RADIO_CLEAN + (
            "openbrcm: DEVICE LOST (radio-probe) - reboot required\n"
        )
        r = evaluate("radio_id_probe_only", text)
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))
        self.assertIn("REBOOT REQUIRED", r.stdout)

    def test_radio_current_run_fail_line_detected(self):
        text = RADIO_CLEAN.replace(
            "radio-probe: PASS\n",
            "radio-probe: FAIL unexpected radioid=0x1234 (AC accepts 0x2069/0x030b)\n",
        )
        r = evaluate("radio_id_probe_only", text)
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))

    def test_stale_fault_above_marker_ignored(self):
        full = (
            "historical noise\n"
            "BUG: stale crash from a previous boot\n"
            "Call Trace:\n"
            "WARNING: stale warning above the marker\n"
            "[openbrcm-test] marker TESTMARK\n"
            + RADIO_CLEAN
        )
        r = evaluate("radio_id_probe_only", full, marker="TESTMARK")
        self.assertEqual(r.returncode, 0, (r.stdout, r.stderr))
        self.assertIn("CLEAN PASS (rmmod permitted)", r.stdout)

    def test_current_run_fatal_below_marker_detected(self):
        full = (
            "[openbrcm-test] marker TESTMARK\n"
            "radio-probe: BEGIN\n"
            "radio-probe: pre_access=0xffffffff\n"
            "openbrcm: DEVICE LOST (radio-probe) - reboot required\n"
        )
        r = evaluate("radio_id_probe_only", full, marker="TESTMARK")
        self.assertEqual(r.returncode, 3, (r.stdout, r.stderr))

    def test_marker_absent_aborts(self):
        r = evaluate("radio_id_probe_only", RADIO_CLEAN, marker="NOPE")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("marker not found", r.stderr)

    def test_old_modes_verdict_unchanged(self):
        # ucode mode: marker present -> pass; removed -> incomplete
        ok = evaluate(
            "ucode_test_only",
            "ucode-test: PASS - stopped before initvals/PHY/radio/DMA\n",
        )
        self.assertEqual(ok.returncode, 0, (ok.stdout, ok.stderr))
        bad = evaluate("ucode_test_only", "ucode-test: BEGIN\n")
        self.assertEqual(bad.returncode, 4, (bad.stdout, bad.stderr))


if __name__ == "__main__":
    sys.exit(0 if unittest.main(verbosity=2).result.wasSuccessful() else 1)

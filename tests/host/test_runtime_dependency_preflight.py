#!/usr/bin/env python3
"""Offline contract tests for runtime dependency preparation ordering."""

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "runtime-test.sh"


def run_dry_run():
    return subprocess.run(
        [str(SCRIPT), "--dry-run", "--mode", "acphy_pre_radio_test_only"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


class RuntimeDependencyPreflightTests(unittest.TestCase):
    def setUp(self):
        self.source = SCRIPT.read_text(encoding="utf-8")
        self.real_run = self.source.index("# ---------------------------------------------------------------- real run")

    def test_dependency_preparation_precedes_topology_gate(self):
        deps = self.source.index(
            "# ---------------------------------------------------------------- kernel-owned dependency preparation",
            self.real_run,
        )
        topology = self.source.index(
            'if [ "$MODE" = "radio_id_probe_only" ]; then',
            self.real_run,
        )
        self.assertLess(deps, topology)
        self.assertIn("DEPENDENCY_PREPARATION_REQUIRED", self.source)
        self.assertIn(
            "POST_REBOOT_DEPENDENCY_PREPARATION_PRECEDES_TOPOLOGY_GATE",
            self.source,
        )

    def test_case_a_absent_dependency_has_a_preparation_path(self):
        deps = self.source.index(
            "# ---------------------------------------------------------------- kernel-owned dependency preparation",
            self.real_run,
        )
        self.assertLess(
            self.source.index('modprobe "$m"', deps),
            self.source.index("check_radio_identity_topology", deps),
        )

    def test_case_b_dependency_failure_stops_before_topology(self):
        failed = self.source.index("DEPENDENCY_PREPARATION_FAILED", self.real_run)
        topology = self.source.index("check_radio_identity_topology", failed)
        self.assertLess(failed, topology)

    def test_dependency_failure_is_distinct_and_fail_closed(self):
        self.assertIn("DEPENDENCY_PREPARATION_FAILED", self.source)
        self.assertIn("dependency preparation failed", self.source)
        self.assertLess(
            self.source.index("DEPENDENCY_PREPARATION_FAILED"),
            self.source.index("die \"dependency preparation failed"),
        )

    def test_ready_boundary_is_after_topology_and_before_insmod(self):
        topology = self.source.index(
            'if [ "$MODE" = "radio_id_probe_only" ]; then',
            self.real_run,
        )
        ready = self.source.index("READY_FOR_OPENBRCM_INSERTION", self.real_run)
        insmod = self.source.index('insmod "$KO"', ready)
        self.assertLess(topology, ready)
        self.assertLess(ready, insmod)

    def test_openbrcm_containment_precedes_dependency_preparation(self):
        containment = self.source.index(
            'echo "OPENBRCM_ALREADY_LOADED"',
            self.real_run,
        )
        deps = self.source.index(
            "# ---------------------------------------------------------------- kernel-owned dependency preparation",
            self.real_run,
        )
        self.assertLess(containment, deps)
        self.assertIn("OPENBRCM_ALREADY_LOADED", self.source)

    def test_case_c_topology_gate_remains_after_dependency_stage(self):
        deps = self.source.index(
            "DEPENDENCY_PREPARATION_READY", self.real_run,
        )
        topology = self.source.index("check_radio_identity_topology", deps)
        self.assertLess(deps, topology)

    def test_case_d_loaded_openbrcm_stops_before_dependency_stage(self):
        self.test_openbrcm_containment_precedes_dependency_preparation()

    def test_dry_run_declares_dependency_preparation_without_loading(self):
        result = run_dry_run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("DEPENDENCY_PREPARATION_REQUIRED", result.stdout)
        self.assertIn("modprobe cfg80211", result.stdout)
        self.assertIn("modprobe mac80211", result.stdout)
        self.assertIn("modprobe bcma", result.stdout)
        self.assertNotIn("DEPENDENCY_PREPARATION_FAILED", result.stdout)
        self.assertNotIn("insmod rc=", result.stdout)

    def test_case_e_dry_run_never_executes_dependency_preparation(self):
        result = run_dry_run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("DRY-RUN (no modprobe/insmod/rmmod/MMIO/bind will run)", result.stdout)
        self.assertNotIn("DEPENDENCY_PREPARATION_READY", result.stdout)

    def test_case_f_prepared_dependencies_are_idempotent(self):
        self.test_already_prepared_dependency_is_skipped_idempotently()

    def test_already_prepared_dependency_is_skipped_idempotently(self):
        self.assertIn('if [ -d "/sys/module/$m" ]; then', self.source)
        self.assertIn("already loaded", self.source)
        self.assertIn("continue", self.source)


if __name__ == "__main__":
    unittest.main()

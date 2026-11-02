#!/usr/bin/env python3
"""Offline fixture tests for radio identity topology preflight.

The bench's PCI bus number is assigned at enumeration and is not stable across
boots (0000:05:00.0 and 0000:21:00.0 have both been observed).  The gate must
therefore resolve the endpoint by identity, refuse ambiguity, and still pin the
bcma child and driver topology.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts" / "runtime-test.sh"


def make_device(root, bdf):
    pci = root / "bus/pci/devices" / bdf
    child = root / "bus/bcma/devices/bcma0:1"
    (pci / "driver").parent.mkdir(parents=True)
    child.mkdir(parents=True, exist_ok=True)
    (pci / "vendor").write_text("0x14e4\n")
    (pci / "device").write_text("0x43b1\n")
    (root / "drivers").mkdir(exist_ok=True)
    (root / "drivers/bcma-pci-bridge").mkdir(exist_ok=True)
    (pci / "driver").symlink_to(root / "drivers/bcma-pci-bridge")
    (child / "manuf").write_text("0x4BF\n")
    (child / "id").write_text("0x812\n")
    (child / "rev").write_text("0x2A\n")
    return pci, child


def run_fixture(mutator=None, bdf="0000:05:00.0"):
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        pci, child = make_device(root, bdf)
        if mutator:
            mutator(root, pci, child)
        env = os.environ.copy()
        env["OPENBRCM_TEST_TOPOLOGY_ROOT"] = str(root)
        env["OPENBRCM_TEST_TOPOLOGY_MODE"] = "1"
        return subprocess.run(
            [str(SCRIPT), "--topology-only", "--mode", "radio_id_probe_only"],
            cwd=REPO, env=env, text=True, capture_output=True,
        )


class RuntimeTopology(unittest.TestCase):
    def test_topology_gate_resolves_the_endpoint_by_identity(self):
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertIn("TOPOLOGY_PCI_AMBIGUOUS", source)
        self.assertNotIn("devices/0000:05:00.0", source)
        self.assertNotIn("devices/0000:21:00.0", source)

    def test_exact_unbound_topology_passes(self):
        r = run_fixture()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("TOPOLOGY PASS", r.stdout)
        self.assertNotIn("insmod", r.stdout)

    def test_the_same_device_passes_at_another_bus_number(self):
        for bdf in ("0000:21:00.0", "0000:02:00.0"):
            with self.subTest(bdf=bdf):
                r = run_fixture(bdf=bdf)
                self.assertEqual(r.returncode, 0, (bdf, r.stdout, r.stderr))
                self.assertIn("TOPOLOGY PASS", r.stdout)

    def test_two_matching_devices_are_ambiguous(self):
        def add_second(root, pci, child):
            second = root / "bus/pci/devices/0000:22:00.0"
            second.mkdir(parents=True)
            (second / "vendor").write_text("0x14e4\n")
            (second / "device").write_text("0x43b1\n")

        r = run_fixture(add_second)
        self.assertEqual(r.returncode, 1, (r.stdout, r.stderr))
        self.assertIn("TOPOLOGY_PCI_AMBIGUOUS:2", r.stdout)

    def test_rejects_each_required_topology_failure(self):
        cases = [
            ("TOPOLOGY_PCI_MISSING", lambda root, pci, child: pci.rename(root / "missing")),
            ("TOPOLOGY_PCI_ID_MISMATCH", lambda root, pci, child: (pci / "vendor").write_text("0x1234\n")),
            ("TOPOLOGY_PCI_ID_MISMATCH", lambda root, pci, child: (pci / "device").write_text("0x9999\n")),
            ("TOPOLOGY_PARENT_DRIVER_MISSING", lambda root, pci, child: (pci / "driver").unlink()),
            ("TOPOLOGY_PARENT_DRIVER_MISMATCH", lambda root, pci, child: (pci / "driver").unlink() or (root / "drivers/other").mkdir() or (pci / "driver").symlink_to(root / "drivers/other")),
            ("TOPOLOGY_BCMA_CHILD_MISSING", lambda root, pci, child: child.rename(root / "missing-child")),
            ("TOPOLOGY_BCMA_IDENTITY_MISMATCH", lambda root, pci, child: (child / "manuf").write_text("0x999\n")),
            ("TOPOLOGY_BCMA_IDENTITY_MISMATCH", lambda root, pci, child: (child / "id").write_text("0x813\n")),
            ("TOPOLOGY_BCMA_IDENTITY_MISMATCH", lambda root, pci, child: (child / "rev").write_text("0x29\n")),
            ("TOPOLOGY_BCMA_CHILD_ALREADY_BOUND", lambda root, pci, child: (root / "drivers/b43").mkdir() or (child / "driver").symlink_to(root / "drivers/b43")),
            ("TOPOLOGY_OPENBRCM_ALREADY_LOADED", lambda root, pci, child: (root / "module/openbrcm").mkdir(parents=True)),
        ]
        for expected, mutator in cases:
            with self.subTest(expected=expected):
                r = run_fixture(mutator)
                self.assertEqual(r.returncode, 1, (expected, r.stdout, r.stderr))
                self.assertIn(expected, r.stdout)

    def test_test_override_is_not_accepted_without_test_mode(self):
        with tempfile.TemporaryDirectory() as td:
            env = os.environ.copy()
            env["OPENBRCM_TEST_TOPOLOGY_ROOT"] = td
            env.pop("OPENBRCM_TEST_TOPOLOGY_MODE", None)
            r = subprocess.run(
                [str(SCRIPT), "--topology-only", "--mode", "radio_id_probe_only"],
                cwd=REPO, env=env, text=True, capture_output=True,
            )
        self.assertNotEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main()

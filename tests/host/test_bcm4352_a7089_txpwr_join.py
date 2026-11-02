#!/usr/bin/env python3
"""Compare the caller seam against a separately authored instruction oracle."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/host"))
from a7089_txpwr_oracle import expected_suffix

BINARY = ROOT / "tests/host/ob_phy_a7089_txpwr_test"


class A7089TxPowerJoinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", "tests/host", "ob_phy_a7089_txpwr_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)

    def assert_trace(self, pi164, first, second, txpwrctrl_arm="equal",
                     phy_samples=None):
        command = [str(BINARY), "--trace", str(pi164), hex(first), hex(second)]
        if txpwrctrl_arm != "equal" or phy_samples is not None:
            command.append(txpwrctrl_arm)
        if phy_samples is not None:
            command.extend(hex(sample) for sample in phy_samples)
        actual = json.loads(subprocess.run(
            command,
            cwd=ROOT, check=True, capture_output=True, text=True).stdout)
        expected = expected_suffix(pi164, (first, second))
        if txpwrctrl_arm == "skip":
            expected = expected_suffix(pi164, (first, second), phy_0070=0x1234)
        if phy_samples is not None:
            expected = expected_suffix(pi164, (first, second),
                                       phy_0640=phy_samples[0],
                                       phy_0840=phy_samples[1])
        self.assertEqual(actual, expected)

    def test_pi164_set_selects_only_040a_rmw_and_preserves_two_core_loop(self):
        for pi164 in (2, 3, 5, 6):
            with self.subTest(pi164=pi164):
                self.assert_trace(pi164, 0x80, 0x7f)

    def test_pi164_other_values_skip_040a_but_keep_txpower_children(self):
        for pi164 in (0, 1, 4, 7, 0x0a, 0x101):
            with self.subTest(pi164=pi164):
                self.assert_trace(pi164, 0xff, 0x80)

    def test_signed_byte_boundaries_and_core_specific_payloads(self):
        self.assert_trace(2, 0x80, 0x7f)
        self.assert_trace(3, 0xff, 0x80)

    def test_txpwrctrl_equal_arm_preserves_two_per_core_reads(self):
        trace = expected_suffix(2, (0x80, 0x7f))
        tx_events = [event for event in trace if event["site"] in
                     (0x9071a, 0x906d7, 0x90760)]
        self.assertEqual([event["reg"] for event in tx_events],
                         [0x0070, 0x0640, 0x0840, 0x0070, 0x0070])
        self.assert_trace(2, 0x80, 0x7f, "equal")

    def test_txpwrctrl_non_equal_arm_skips_per_core_reads(self):
        trace = expected_suffix(2, (0x80, 0x7f), phy_0070=0x1234)
        self.assertEqual([event["site"] for event in trace[-4:]],
                         [0x9071a, 0x90760, 0x90760, 0xaa61e])
        self.assert_trace(2, 0x80, 0x7f, "skip")

    def test_threshold_and_write_value_follow_explicit_inputs(self):
        cases = ((0, 0x0203, 0x26), (1, 0, 0x18), (2, 0, 0x80),
                 (3, 0, 0x1c), (5, 0, 0x80), (0x0a, 0, 0x80))
        for pi164, nested_342, threshold in cases:
            with self.subTest(pi164=pi164, nested_342=nested_342):
                event = expected_suffix(pi164, (0x80, 0x7f),
                                         nested_342=nested_342)[-1]
                self.assertEqual(event["site"], 0xaa61e)
                self.assertEqual(event["reg"], 0x1641)
                self.assertEqual(event["value"], 0x7f00 + threshold)

    def test_txpwrctrl_samples_are_preserved_as_distinct_reads(self):
        self.assert_trace(2, 0x80, 0x7f, phy_samples=(0x7f55, 0x8000))


if __name__ == "__main__":
    unittest.main()

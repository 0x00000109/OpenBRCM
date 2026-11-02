#!/usr/bin/env python3
"""Check the post-Farrow suffix and byte-for-byte predecessor preservation."""

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / "tests/host"
BIN = HOST / "ob_phy_a6b0f_test"


def trace(mode):
    completed = subprocess.run([str(BIN), mode], cwd=ROOT, check=True,
                               capture_output=True, text=True)
    return completed.stdout.splitlines()


class PostFarrowCompositionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-B", "-C", str(HOST), "ob_phy_a6b0f_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)

    def check_scenario(self, old_mode, new_mode, old_count, new_count):
        predecessor = trace(old_mode)
        composed = trace(new_mode)
        self.assertEqual(len(predecessor), old_count)
        self.assertEqual(len(composed), new_count)
        self.assertEqual(composed[:old_count], predecessor)
        suffix = [line.split(",") for line in composed[old_count:]]
        self.assertEqual(len(suffix), 6)
        expected_sites = ["0x9b0cd", "0x9b0eb", "0x9b0eb",
                          "0x9b0f8", "0x9b116", "0x9b116"]
        self.assertEqual([record[3] for record in suffix], expected_sites)
        self.assertEqual([record[0] for record in suffix], ["R", "R", "W", "R", "R", "W"])
        self.assertEqual([record[1] for record in suffix], ["PHY"] * 6)
        self.assertEqual([int(record[2]) for record in suffix], [1, 2, 2, 3, 4, 4])
        self.assertEqual([record[4] for record in suffix],
                         ["0x019e", "0x019e", "0x019e", "0x016c", "0x016c", "0x016c"])
        old_019e = int(suffix[1][5], 16)
        new_019e = int(suffix[2][5], 16)
        old_016c = int(suffix[4][5], 16)
        new_016c = int(suffix[5][5], 16)
        self.assertEqual(new_019e, (old_019e & ~0x0002) | 0x0002)
        self.assertEqual(new_016c, (old_016c & ~0x0040) | 0x0040)
        self.assertEqual([record[6] for record in suffix], ["16"] * 6)

    def test_clear_mask_predecessor_is_unchanged_and_prefix_is_appended(self):
        self.check_scenario("--dump-composed-farrow-clear",
                            "--dump-composed-postfarrow-clear", 4350, 4356)

    def test_set_mask_predecessor_is_unchanged_and_prefix_is_appended(self):
        self.check_scenario("--dump-composed-farrow-set",
                            "--dump-composed-postfarrow-set", 4378, 4384)


if __name__ == "__main__":
    unittest.main(verbosity=2)

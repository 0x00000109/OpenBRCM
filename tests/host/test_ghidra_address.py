#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regression tests for address-oriented offline Ghidra analysis.

The test invokes the local headless importer/decompiler only.  It never loads
the kernel module and never accesses the live BCM device.
"""
import os
import subprocess
import unittest


HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
GHIDRA = os.path.join(ROOT, "scripts", "ghidra_headless.sh")
BLOB = os.path.join(ROOT, "old", "amd64", "lib", "wlc_hybrid.o_shipped")
GHIDRA_HOME = os.environ.get("GHIDRA_HOME", "/home/kartashoff/projects/ghidra")
HEADLESS = os.path.join(GHIDRA_HOME, "support", "analyzeHeadless")


@unittest.skipUnless(
    os.path.isfile(GHIDRA) and os.path.isfile(BLOB) and os.path.isfile(HEADLESS),
    "local Ghidra headless or read-only old AMD64 blob is not available",
)
class GhidraAddressTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        result = subprocess.run(
            [GHIDRA, "DecompileAt.java", "0x37024", "0x2d080", "0x7fffff"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=180,
        )
        cls.output = result.stdout + result.stderr
        if result.returncode != 0:
            raise AssertionError(cls.output)

    def test_reports_address_status_boundaries_and_graph(self):
        out = self.output
        self.assertIn("TARGET=0x00037024", out)
        self.assertIn("FUNCTION_ENTRY=0x00137024", out)
        self.assertIn("FUNCTION_SIZE=56", out)
        self.assertRegex(out, r"STATUS=(KNOWN_ENTRY|RECOVERED_ENTRY)")
        self.assertRegex(out, r"CALLEE.*wlc_hw_deviceremoved")
        self.assertIn("TARGET=0x0002d080", out)
        self.assertIn("FUNCTION_ENTRY=0x0012d080", out)
        self.assertIn("FUNCTION_SIZE=353", out)
        self.assertRegex(out, r"CALLEE.*wlc_bsscfg_find_by_wlcif")
        self.assertIn("DECOMPILE_BEGIN", out)
        self.assertIn("DECOMPILE_END", out)

    def test_reports_unmapped_address_without_fabricating_a_function(self):
        out = self.output
        self.assertIn("TARGET=0x007fffff", out)
        self.assertIn("STATUS=UNDEFINED", out)
        self.assertIn("FUNCTION_ENTRY=UNKNOWN", out)


if __name__ == "__main__":
    unittest.main()

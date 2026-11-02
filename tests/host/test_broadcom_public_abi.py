#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Compile public Broadcom ABI assertions without private binaries or hardware."""
from __future__ import annotations

import hashlib
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "tests/host/abi/broadcom_public_abi.c"
INCLUDE = ROOT / "docs/old/amd64/src/include"
COMMON = ROOT / "docs/old/amd64/src/common/include"
SHARED = ROOT / "docs/old/amd64/src/shared"
CHANNELS = ROOT / "docs/old/amd64/src/shared/bcmwifi/include"
INPUTS = {
    "docs/old/amd64/src/include/wlioctl.h": "c950bce9e5dc59d5fb1a9e06e87130b0d7910f05eff3986d23939d9f7404b656",
    "docs/old/amd64/src/include/typedefs.h": "b3f1e78949d69174b64f280daf1fe7cdbbd391f2166f693257778af6357fd7c5",
    "docs/old/amd64/src/include/pcicfg.h": "cde28c6c8d19ddcd600f50fd224d7e8c343e89dbd086aea99af04a1071e9b949",
    "docs/old/amd64/src/shared/bcmwifi/include/bcmwifi_channels.h": "6a4576ccd85f58a44eb9a2bdd16509701110c998c4717534d1b59a584a6c6bd1",
}


class BroadcomPublicAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        missing = [relative for relative in INPUTS if not (ROOT / relative).is_file()]
        if missing:
            raise unittest.SkipTest("optional historical public headers are absent: " + ", ".join(missing))
        cls.cc = shutil.which("gcc")
        if not cls.cc:
            raise unittest.SkipTest("gcc is unavailable")
        cls.version = subprocess.run([cls.cc, "--version"], text=True, capture_output=True, check=True).stdout.splitlines()[0]

    def test_pinned_header_inputs_match(self):
        for relative, expected in INPUTS.items():
            actual = hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
            self.assertEqual(actual, expected, f"header provenance changed: {relative}")

    def test_public_layouts_compile_for_native_and_i386(self):
        common = [
            self.cc, "-std=gnu11", "-Werror", "-fsyntax-only", "-D__BOB__=1",
            f"-I{INCLUDE}", f"-I{COMMON}", f"-I{SHARED}", f"-I{CHANNELS}", str(SOURCE),
        ]
        native = subprocess.run(common, text=True, capture_output=True, check=False)
        self.assertEqual(native.returncode, 0, f"{self.version} native target failed:\n{native.stderr}")

        # Probe only whether GCC can parse a trivial translation unit for i386.
        # Once this capability exists, failures in the real assertion TU are
        # ABI regressions and must fail the test rather than become a skip.
        capability = subprocess.run(
            [self.cc, "-m32", "-std=gnu11", "-Werror", "-fsyntax-only", "-x", "c", "-"],
            input="int abi_target_probe;\n",
            text=True,
            capture_output=True,
            check=False,
        )
        if capability.returncode:
            self.skipTest(
                f"{self.version} has no usable -m32 syntax target: {capability.stderr.strip()}"
            )
        i386 = subprocess.run([self.cc, "-m32", *common[1:]], text=True, capture_output=True, check=False)
        self.assertEqual(i386.returncode, 0, f"{self.version} i386 ABI assertions failed:\n{i386.stderr}")


if __name__ == "__main__":
    unittest.main()

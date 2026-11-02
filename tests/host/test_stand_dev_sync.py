#!/usr/bin/env python3
"""Offline contract tests for synchronising the stand runtime harness."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "stand-dev-sync.sh"


class StandDevSyncTests(unittest.TestCase):
    def test_sync_copies_runtime_harness_into_the_remote_scripts_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            log = tmp_path / "commands.log"
            fake_bin = tmp_path / "bin"
            fake_bin.mkdir()
            for command in ("ssh", "rsync"):
                tool = fake_bin / command
                tool.write_text(
                    "#!/bin/sh\nprintf '%s\\n' \"$0 $*\" >> \"$LOG\"\n",
                    encoding="utf-8",
                )
                tool.chmod(0o755)
            env = os.environ.copy()
            env.update({
                "PATH": f"{fake_bin}:{env['PATH']}",
                "LOG": str(log),
                "STAND": "root@stand.example",
            })
            result = subprocess.run(
                ["sh", str(SCRIPT), "/srv/openbrcm-dev"], cwd=ROOT,
                env=env, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            commands = log.read_text(encoding="utf-8")
            self.assertIn("mkdir -p '/srv/openbrcm-dev/scripts'", commands)
            self.assertIn("scripts/runtime-test.sh", commands)
            self.assertIn("root@stand.example:/srv/openbrcm-dev/scripts/", commands)


if __name__ == "__main__":
    unittest.main()

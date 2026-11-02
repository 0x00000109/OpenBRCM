#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise scripts/check.sh optional-check reporting with failing tools."""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class CheckScriptTests(unittest.TestCase):
    def make_sandbox(self, root, make_contents):
        (root / "scripts").mkdir()
        shutil.copy2(ROOT / "scripts/check.sh", root / "scripts/check.sh")
        # This sandbox isolates checkpatch/sparse behavior; make the unrelated
        # knowledge-base gate succeed without copying the full docs tree.
        (root / "scripts/kb").mkdir()
        (root / "scripts/kb/validate.py").write_text("raise SystemExit(0)\n", encoding="utf-8")
        (root / "tests/host").mkdir(parents=True)
        bindir = root / "bin"
        bindir.mkdir()
        make = bindir / "make"
        make.write_text(make_contents, encoding="utf-8")
        make.chmod(0o755)
        return bindir

    def test_checkpatch_failure_is_reported_as_advisory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            bindir = self.make_sandbox(root, "#!/bin/sh\nexit 0\n")
            checkpatch = root / "checkpatch.pl"
            checkpatch.write_text(
                "#!/bin/sh\necho 'synthetic checkpatch error'\nexit 7\n",
                encoding="utf-8",
            )
            checkpatch.chmod(0o755)
            env = os.environ.copy()
            env["PATH"] = str(bindir) + ":/usr/bin:/bin"
            env["CHECKPATCH"] = str(checkpatch)
            env.pop("KDIR", None)
            result = subprocess.run(
                ["sh", str(root / "scripts/check.sh")],
                cwd=root, env=env, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("ADVISORY: checkpatch reported findings (exit 7)", result.stdout)
            self.assertIn("synthetic checkpatch error", result.stdout)

    def test_sparse_failure_fails_the_check_script(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            bindir = self.make_sandbox(
                root,
                "#!/bin/sh\ncase \" $* \" in *' hosttest '*) exit 0;; *) exit 9;; esac\n",
            )
            sparse = bindir / "sparse"
            sparse.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            sparse.chmod(0o755)
            env = os.environ.copy()
            env["PATH"] = str(bindir) + ":/usr/bin:/bin"
            env["KDIR"] = "/kernel"
            env.pop("CHECKPATCH", None)
            result = subprocess.run(
                ["sh", str(root / "scripts/check.sh")],
                cwd=root, env=env, capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 9, result.stdout + result.stderr)
            self.assertIn("== sparse ==", result.stdout)


if __name__ == "__main__":
    unittest.main()

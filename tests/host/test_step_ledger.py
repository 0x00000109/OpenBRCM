#!/usr/bin/env python3
"""Black-box checks for the Rust step-ledger contract."""
import json
import os
import subprocess
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
BIN = os.path.join(ROOT, "tools/step-ledger/target/debug/step-ledger")


class StepLedgerTests(unittest.TestCase):
    def run_tool(self, tmp, *args, cwd=ROOT):
        return subprocess.run([BIN, *args, "--ledger", os.path.join(tmp, "ledger.jsonl")],
                              cwd=cwd, capture_output=True, text=True)

    @staticmethod
    def dirty_repo(tmp):
        """A throwaway git repo with one tracked-and-modified path.

        The collector reports the dirty paths of the repository it runs in, so
        the contract can only be asserted against a repo whose dirt is created
        by the test. Using ROOT made the test pass or fail depending on the
        caller's working tree (a clean frozen tree has no changed_paths at all).
        """
        repo = os.path.join(tmp, "repo")
        os.makedirs(repo)
        with open(os.path.join(repo, "tracked.txt"), "w") as fh:
            fh.write("one\n")
        for cmd in (("init", "-q"), ("add", "tracked.txt"),
                    ("-c", "user.email=t@example.invalid",
                     "-c", "user.name=t", "commit", "-qm", "seed")):
            subprocess.run(["git", "-C", repo, *cmd], check=True,
                           capture_output=True, text=True)
        with open(os.path.join(repo, "tracked.txt"), "w") as fh:
            fh.write("two\n")
        return repo

    def test_begin_record_close_validate_and_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = self.dirty_repo(tmp)
            for cmd in (
                ("begin", "--blocker", "b", "--step", "s"),
                ("record", "--blocker", "b", "--step", "s", "--phase", "test",
                 "--status", "PASS", "--command", "true", "--exit-code", "0"),
                ("close", "--blocker", "b", "--step", "s", "--status", "CLOSED"),
            ):
                result = self.run_tool(tmp, *cmd, cwd=repo)
                self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.run_tool(tmp, "validate", cwd=repo).returncode, 0)
            report = self.run_tool(tmp, "report", cwd=repo)
            self.assertEqual(report.returncode, 0, report.stderr)
            self.assertIn("b | s | CLOSED", report.stdout)
            with open(os.path.join(tmp, "ledger.jsonl")) as fh:
                rows = [json.loads(line) for line in fh if line.strip()]
            self.assertEqual([row["seq"] for row in rows], [1, 2, 3])
            self.assertTrue(all(row["collector_version"] == "porcelain-v1-z/v1" for row in rows))
            # The collector must report the repository's actual dirty paths;
            # this test itself must not depend on a particular unrelated file
            # being modified in the caller's working tree.
            for row in rows:
                self.assertEqual(row["changed_paths"], ["tracked.txt"])

    def test_close_without_begin_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = self.run_tool(tmp, "close", "--blocker", "b", "--step", "s")
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()

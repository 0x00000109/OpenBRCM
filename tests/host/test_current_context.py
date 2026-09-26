#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for scripts/generate-current-context.py.

Covers Part H: deterministic generation, stable ordering, stale HEAD/binary
identity detection, source-conflict detection, broken artifact-reference
detection, duplicate fact-ID rejection, SUPERSEDED-evidence rejection, a bounded
output size and no loss of canonical references.
"""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, "..", ".."))
_GEN = os.path.join(_ROOT, "scripts", "generate-current-context.py")
_OUT = os.path.join(_ROOT, "docs", "current-context.json")

_spec = importlib.util.spec_from_file_location("genctx", _GEN)
genctx = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(genctx)


def run(*args):
    return subprocess.run(
        [sys.executable, _GEN, *args], capture_output=True, text=True
    )


def base_state():
    return {
        "schema": "openbrcm-current-state/v1",
        "current_milestone": {"id": "M3.4D4", "status": "ANALYSIS ONLY",
                              "artifact": "docs/ev.md"},
        "last_hardware_proven_milestone": {"id": "M3.4D3A1", "evidence": "docs/ev.md"},
        "hardware_test_go": "NO",
        "active_blocker": {"id": "b1", "summary": "s", "evidence": ["docs/ev.md"]},
        "open_blockers": [{"id": "b1", "summary": "s", "evidence": ["docs/ev.md"]}],
        "proven_facts": [
            {"id": "f1", "class": "hardware", "status": "PROVEN",
             "summary": "s", "source": "docs/ev.md", "record": "R1"}
        ],
        "current_call_path": ["a", "b"],
        "canonical_sources": ["docs/ev.md"],
    }


def base_ledger():
    return {
        "schema": "openbrcm-artifact-ledger/v1",
        "repos": {},
        "records": [{"id": "R1", "status": "PROVEN", "artifacts": ["docs/ev.md"]}],
        "supersession_map": [],
    }


def make_fixture(root, state=None, ledger=None, agent_state="M3.4D4 milestone\n"):
    os.makedirs(os.path.join(root, "docs", "state"), exist_ok=True)
    os.makedirs(os.path.join(root, "scripts"), exist_ok=True)
    if state is None:
        state = base_state()
    if ledger is None:
        ledger = base_ledger()
    with open(os.path.join(root, "docs", "state", "current-state.json"), "w") as fh:
        json.dump(state, fh)
    with open(os.path.join(root, "docs", "artifact-ledger.json"), "w") as fh:
        json.dump(ledger, fh)
    with open(os.path.join(root, "docs", "agent-state.md"), "w") as fh:
        fh.write(agent_state)
    with open(os.path.join(root, "docs", "ev.md"), "w") as fh:
        fh.write("evidence\n")
    return root


class DeterminismTests(unittest.TestCase):
    def test_double_build_identical(self):
        ctx1 = genctx.build_context(_ROOT, "/nonexistent", "/nonexistent")
        ctx2 = genctx.build_context(_ROOT, "/nonexistent", "/nonexistent")
        self.assertEqual(
            json.dumps(ctx1, sort_keys=True), json.dumps(ctx2, sort_keys=True)
        )

    def test_stable_ordering(self):
        ctx = genctx.build_context(_ROOT, "/nonexistent", "/nonexistent")
        fact_ids = [f["id"] for f in ctx["proven_facts"]]
        self.assertEqual(fact_ids, sorted(fact_ids))
        blocker_ids = [b["id"] for b in ctx["open_blockers"]]
        self.assertEqual(blocker_ids, sorted(blocker_ids))
        self.assertEqual(ctx["canonical_sources"], sorted(ctx["canonical_sources"]))

    def test_real_generation_bounded_and_refs_present(self):
        ctx = genctx.build_context(_ROOT, "/nonexistent", "/nonexistent")
        text = json.dumps(ctx, indent=2, sort_keys=True) + "\n"
        self.assertLessEqual(len(text.encode()), genctx.MAX_CONTEXT_BYTES)
        for rel in ctx["canonical_sources"] + ctx["artifact_references"]:
            self.assertTrue(os.path.exists(os.path.join(_ROOT, rel)), rel)


class StaleDetectionTests(unittest.TestCase):
    def _fresh_out(self, tmp):
        out = os.path.join(tmp, "current-context.json")
        shutil.copy(_OUT, out)
        return out

    def test_check_fresh(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = self._fresh_out(tmp)
            r = run("--root", _ROOT, "--out", out, "--check")
            self.assertEqual(r.returncode, 0, r.stderr)

    def test_check_detects_tampered_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = self._fresh_out(tmp)
            with open(out) as fh:
                doc = json.load(fh)
            doc["integrity"]["sources_hash"] = "0" * 64
            with open(out, "w") as fh:
                json.dump(doc, fh)
            r = run("--root", _ROOT, "--out", out, "--check")
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("STALE", r.stderr)

    def test_check_detects_binary_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = self._fresh_out(tmp)
            with open(out) as fh:
                doc = json.load(fh)
            doc["binary_identity"]["blob_sha256"] = "deadbeef"
            with open(out, "w") as fh:
                json.dump(doc, fh)
            r = run("--root", _ROOT, "--out", out, "--check")
            self.assertNotEqual(r.returncode, 0)


class IntegrityTests(unittest.TestCase):
    def _validate(self, state=None, ledger=None, agent_state="M3.4D4\n"):
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, state, ledger, agent_state)
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"), "--validate")
            return r

    def test_valid_fixture_passes(self):
        r = self._validate()
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_broken_artifact_reference(self):
        s = base_state()
        s["proven_facts"][0]["source"] = "docs/missing.md"
        r = self._validate(state=s)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("missing path", r.stderr)

    def test_duplicate_fact_id(self):
        s = base_state()
        s["proven_facts"].append(dict(s["proven_facts"][0]))
        r = self._validate(state=s)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("duplicate fact id", r.stderr)

    def test_active_blocker_not_in_open_blockers(self):
        s = base_state()
        s["active_blocker"]["id"] = "not-listed"
        r = self._validate(state=s)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("active_blocker", r.stderr)

    def test_missing_milestone_in_narrative(self):
        r = self._validate(agent_state="nothing relevant here\n")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("current_milestone", r.stderr)

    def test_superseded_record_rejected(self):
        led = base_ledger()
        led["records"][0]["superseded_by"] = "R2"
        r = self._validate(ledger=led)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("SUPERSEDED", r.stderr)

    def test_unsupported_schema_rejected(self):
        # A re.db with an unknown schema must fail validation.
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp)
            tooling = os.path.join(tmp, "tooling")
            os.makedirs(tooling, exist_ok=True)
            import sqlite3
            dbp = os.path.join(tooling, "re.db")
            c = sqlite3.connect(dbp)
            c.execute("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)")
            c.execute("INSERT INTO meta VALUES('sha256','abc')")
            c.execute("INSERT INTO meta VALUES('schema_version','99')")
            c.commit()
            c.close()
            r = run("--root", tmp, "--tooling", tooling, "--validate")
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("schema_version", r.stderr)


class LinkScanTests(unittest.TestCase):
    def test_link_scan_flags_broken(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.makedirs(os.path.join(tmp, "docs"), exist_ok=True)
            p = os.path.join(tmp, "docs", "x.md")
            with open(p, "w") as fh:
                fh.write("[ok](y.md)\n[bad](nope.md)\n")
            with open(os.path.join(tmp, "docs", "y.md"), "w") as fh:
                fh.write("y\n")
            r = run("--root", tmp, "--scan-links", "docs/x.md")
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("nope.md", r.stderr)


if __name__ == "__main__":
    unittest.main()

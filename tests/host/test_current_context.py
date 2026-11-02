#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for scripts/generate-current-context.py.

Covers Part H: deterministic generation, stable ordering, stale HEAD/binary
identity detection, source-conflict detection, broken artifact-reference
detection, duplicate fact-ID rejection, SUPERSEDED-evidence rejection, a bounded
output size and no loss of canonical references.
"""
import importlib.util
import hashlib
import json
import os
import shutil
import sqlite3
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


def run(*args, env=None):
    if env is None:
        env = os.environ.copy()
        # Temporary fixture roots intentionally exercise source validation
        # without the developer machine's external historical-image input.
        # Explicit-manifest coverage is tested separately below.
        if "--root" in args:
            root_arg = os.path.abspath(args[args.index("--root") + 1])
            if root_arg != _ROOT:
                env.pop("RE_IMAGE_MANIFEST", None)
    return subprocess.run(
        [sys.executable, _GEN, *args], capture_output=True, text=True, env=env
    )


def build_context(root, tooling, ghidra):
    # Isolate synthetic roots from an ambient external manifest. The real-root
    # tests still inherit it, and explicit-manifest tests pass it deliberately.
    root = os.path.abspath(root)
    if root == _ROOT:
        return genctx.build_context(root, tooling, ghidra)
    manifest = os.environ.pop("RE_IMAGE_MANIFEST", None)
    try:
        return genctx.build_context(root, tooling, ghidra)
    finally:
        if manifest is not None:
            os.environ["RE_IMAGE_MANIFEST"] = manifest


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
        ctx1 = build_context(_ROOT, "/nonexistent", "/nonexistent")
        ctx2 = build_context(_ROOT, "/nonexistent", "/nonexistent")
        self.assertEqual(
            json.dumps(ctx1, sort_keys=True), json.dumps(ctx2, sort_keys=True)
        )

    def test_stable_ordering(self):
        ctx = build_context(_ROOT, "/nonexistent", "/nonexistent")
        fact_ids = [f["id"] for f in ctx["proven_facts"]]
        self.assertEqual(fact_ids, sorted(fact_ids))
        blocker_ids = [b["id"] for b in ctx["open_blockers"]]
        self.assertEqual(blocker_ids, sorted(blocker_ids))
        self.assertEqual(ctx["canonical_sources"], sorted(ctx["canonical_sources"]))

    def test_real_generation_bounded_and_refs_present(self):
        ctx = build_context(_ROOT, "/nonexistent", "/nonexistent")
        text = json.dumps(ctx, indent=2, sort_keys=True) + "\n"
        self.assertLessEqual(len(text.encode()), genctx.MAX_CONTEXT_BYTES)
        self.assertEqual(ctx["research_registry"]["path"], "docs/artifact-ledger.json")
        self.assertIn("scripts/research-index.py locate", ctx["research_registry"]["lookup_command"])
        for rel in ctx["canonical_sources"] + ctx["artifact_references"]:
            self.assertTrue(os.path.exists(os.path.join(_ROOT, rel)), rel)


class StaleDetectionTests(unittest.TestCase):
    def _fresh_out(self, tmp):
        out = os.path.join(tmp, "current-context.json")
        # Build against the active provenance selection instead of copying the
        # checked-in context cache, which can be stale after source/ledger edits.
        # When RE_IMAGE_MANIFEST is set this exercises that exact manifest; the
        # generator has no fallback if explicit admission fails.
        result = run("--root", _ROOT, "--out", out)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
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


class ExplicitManifestTests(unittest.TestCase):
    def test_explicit_historical_manifest_drives_context_without_default_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            vendor = os.path.join(tmp, "vendor.bin")
            db_path = os.path.join(tmp, "re.db")
            manifest = os.path.join(tmp, "manifest.json")
            out = os.path.join(tmp, "current-context.json")
            with open(vendor, "wb") as fh:
                fh.write(b"pinned fixture image")
            with open(vendor, "rb") as fh:
                digest = hashlib.sha256(fh.read()).hexdigest()
            with sqlite3.connect(db_path) as conn:
                conn.execute("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)")
                conn.executemany(
                    "INSERT INTO meta VALUES (?, ?)",
                    [("schema_version", "5"), ("sha256", digest)],
                )
            with open(manifest, "w", encoding="utf-8") as fh:
                json.dump({
                    "schema": "openbrcm-re-image-manifest/v1",
                    "default_image": "vendor",
                    "images": {"vendor": {
                        "artifact_id": "vendor",
                        "artifact_kind": "proprietary_vendor_object",
                        "path": vendor,
                        "sha256": digest,
                        "database_path": db_path,
                        "database_schema": 5,
                        "canonical": False,
                    }},
                }, fh)
            env = os.environ.copy()
            env["RE_IMAGE_MANIFEST"] = manifest
            env.pop("RE_DB", None)
            result = run("--root", _ROOT, "--out", out, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with open(out, encoding="utf-8") as fh:
                context = json.load(fh)
            identity = context["binary_identity"]
            self.assertEqual(identity["blob_path"], vendor)
            self.assertEqual(identity["blob_sha256"], digest)
            self.assertEqual(identity["re_db"]["path"], db_path)
            self.assertEqual(identity["selection_mode"], "explicit stable manifest input")

    def test_invalid_explicit_manifest_fails_without_default_fallback(self):
        env = os.environ.copy()
        env["RE_IMAGE_MANIFEST"] = "/no/such/openbrcm/manifest.json"
        result = run("--root", _ROOT, "--out", os.devnull, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("explicit manifest admission failed", result.stderr + result.stdout)


class PointerProjectionTests(unittest.TestCase):
    """`index: pointer` keeps detail out of the index without losing it.

    The index is a startup prefix, so a fact/blocker may opt out of the full
    projection: the id and its artifact stay, the narrative is read from the
    artifact on demand. The opt-out must not become a way to smuggle a fact
    past validation, and it must not collide with an indexed id.
    """

    def _ctx(self, state):
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, state)
            return build_context(tmp, os.path.join(tmp, "t"), "/nonexistent")

    def test_pointer_fact_is_deferred_not_dropped(self):
        s = base_state()
        s["proven_facts"].append(
            {"id": "f2", "status": "PROVEN", "summary": "long narrative",
             "source": "docs/ev.md", "record": "R1", "index": "pointer"}
        )
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s)
            ctx = build_context(tmp, os.path.join(tmp, "t"), "/nonexistent")
            # the full text is still in the authoritative state file
            with open(os.path.join(tmp, "docs", "state",
                                   "current-state.json")) as fh:
                raw = json.load(fh)
        self.assertNotIn("f2", [f["id"] for f in ctx["proven_facts"]])
        self.assertIn("f2", [f["id"] for f in ctx["deferred_facts"]])
        # the summary must not leak into the index
        self.assertNotIn("long narrative", json.dumps(ctx))
        kept = [f for f in raw["proven_facts"] if f["id"] == "f2"]
        self.assertEqual(kept and kept[0]["summary"], "long narrative")

    def test_pointer_blocker_and_active_blocker_project_to_id(self):
        s = base_state()
        s["active_blocker"]["summary"] = "narrative that must not be indexed"
        s["active_blocker"]["index"] = "pointer"
        s["open_blockers"].append(
            {"id": "b2", "summary": "another narrative", "index": "pointer"}
        )
        ctx = self._ctx(s)
        self.assertEqual(ctx["active_blocker"], {"id": "b1"})
        self.assertEqual(ctx["deferred_blockers"], [{"id": "b2"}])
        self.assertNotIn("must not be indexed", json.dumps(ctx))

    def test_pointer_items_still_contribute_their_artifacts(self):
        # A pointer-projected item drops its narrative from the index but its
        # evidence path must still be resolvable from the index.
        s = base_state()
        s["active_blocker"]["index"] = "pointer"
        s["open_blockers"].append(
            {"id": "b2", "summary": "narrative", "index": "pointer",
             "evidence": ["docs/other.md"]}
        )
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s)
            with open(os.path.join(tmp, "docs", "other.md"), "w") as fh:
                fh.write("other\n")
            ctx = build_context(tmp, os.path.join(tmp, "t"), "/nonexistent")
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"),
                    "--validate")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("docs/other.md", ctx["artifact_references"])

    def test_pointer_hardware_run_projects_to_identity_only(self):
        # The run list is the index's largest block, so a spent run may be
        # reduced to its identity while the state file keeps every field.
        s = base_state()
        s["hardware_runs"] = [
            {"run": 1, "mode": "m_full", "executed": True,
             "marker": "openbrcm-test-1", "commit": "abcdef1234567890",
             "signed_module_sha256": "a" * 64,
             "classification": "CLASSIFICATION_IN_THE_INDEX",
             "evidence": ["docs/ev.md"]},
            {"run": 2, "mode": "m_pointer", "executed": True,
             "marker": "openbrcm-test-2", "commit": "abcdef1234567890",
             "signed_module_sha256": "b" * 64,
             "classification": "DETAIL_STAYS_IN_THE_STATE_FILE",
             "index": "pointer"},
        ]
        ctx = self._ctx(s)
        self.assertIn("CLASSIFICATION_IN_THE_INDEX", json.dumps(ctx))
        self.assertNotIn("DETAIL_STAYS_IN_THE_STATE_FILE", json.dumps(ctx))
        self.assertNotIn("openbrcm-test-2", json.dumps(ctx))
        pointer = [r for r in ctx["hardware_runs"] if r["run"] == 2]
        self.assertEqual(
            pointer,
            [{"run": 2, "mode": "m_pointer", "executed": True}],
        )

    def test_pointer_still_validated(self):
        # a pointer-projected fact whose artifact does not exist must FAIL
        s = base_state()
        s["proven_facts"].append(
            {"id": "f2", "status": "PROVEN", "summary": "s",
             "source": "docs/missing.md", "record": "R1", "index": "pointer"}
        )
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s)
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"),
                    "--validate")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("missing path", r.stderr)

    def test_pointer_against_superseded_record_rejected(self):
        s = base_state()
        s["proven_facts"].append(
            {"id": "f2", "status": "PROVEN", "summary": "s",
             "source": "docs/ev.md", "record": "R1", "index": "pointer"}
        )
        led = base_ledger()
        led["records"][0]["superseded_by"] = "R2"
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s, led)
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"),
                    "--validate")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("SUPERSEDED", r.stderr)

    def test_duplicate_id_across_indexed_and_deferred(self):
        s = base_state()
        s["proven_facts"].append(
            {"id": "f1", "status": "PROVEN", "summary": "s",
             "source": "docs/ev.md", "record": "R1", "index": "pointer"}
        )
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s)
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"),
                    "--validate")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("duplicate fact id", r.stderr)

    def test_blocker_cannot_be_open_and_closed_at_once(self):
        s = base_state()
        s["closed_blockers"] = [{"id": "b1", "status": "CLOSED"}]
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s)
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"),
                    "--validate")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("both open and closed", r.stderr)

    def test_closed_status_cannot_remain_in_open_blockers(self):
        s = base_state()
        s["open_blockers"][0]["status"] = "CLOSED — ANALYSIS ONLY"
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s)
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"),
                    "--validate")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("closed status in open_blockers", r.stderr)

    def test_closed_blockers_project_as_id_and_status_only(self):
        s = base_state()
        s["closed_blockers"] = [{"id": "b9", "status": "CLOSED",
                                 "summary": "resolution narrative that must "
                                            "not be indexed"}]
        ctx = self._ctx(s)
        self.assertEqual(ctx["closed_blockers"],
                         [{"id": "b9", "status": "CLOSED"}])
        self.assertNotIn("must not be indexed", json.dumps(ctx))

    def test_pointer_fact_requires_a_source(self):
        s = base_state()
        s["proven_facts"].append(
            {"id": "f2", "status": "PROVEN", "summary": "s",
             "record": "R1", "index": "pointer"}
        )
        with tempfile.TemporaryDirectory() as tmp:
            make_fixture(tmp, s)
            r = run("--root", tmp, "--tooling", os.path.join(tmp, "tooling"),
                    "--validate")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("deferred_fact", r.stderr)


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

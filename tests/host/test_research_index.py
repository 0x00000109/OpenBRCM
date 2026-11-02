#!/usr/bin/env python3
"""Behavioral tests for the OpenBRCM artifact-ledger research workflow."""
from __future__ import annotations

import hashlib
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "scripts" / "research-index.py"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fixture_entry(report: str = "docs/report.md", evidence: str = "docs/evidence.json") -> dict:
    return {
        "research_id": "fixture.phy-revision",
        "title": "PHY revision provenance",
        "subjects": ["PI+0x164", "PHY revision"],
        "devices": ["BCM4352"],
        "device_identities": [],
        "primary_report": report,
        "supporting_artifacts": [{
            "path": evidence,
            "kind": "STATIC_RE_RESULT",
            "evidence_category": "HISTORICAL_BINARY_PROVEN",
        }],
        "source_identities": [],
        "status": "PARTIAL",
        "confidence": "MEDIUM",
        "evidence_categories": ["HISTORICAL_BINARY_PROVEN", "SYNTHETIC_TESTED"],
        "source_date": "2026-10-11",
        "source_revision": "fixture-revision",
        "dependencies": [],
        "contradictions": ["Synthetic 0x0a input does not establish target revision."],
        "supersedes": [],
        "superseded_by": [],
        "unresolved_questions": ["What revision reaches the physical target?"],
        "next_investigation": "Locate an identity-linked target observation.",
    }


def make_root(root: Path, entries: list[dict] | None = None, schema: str = "openbrcm-artifact-ledger/v1") -> None:
    (root / "docs").mkdir(parents=True)
    (root / ".git").mkdir()
    (root / "docs" / "report.md").write_text("report\n")
    (root / "docs" / "evidence.json").write_text("{}\n")
    ledger = {
        "schema": schema,
        "generated": "2026-10-11",
        "repos": {},
        "records": [{"id": "legacy-1", "status": "ANALYSIS ONLY", "artifacts": ["docs/report.md"]}],
        "supersession_map": [],
    }
    if entries is not None:
        ledger["research_entries"] = entries or []
    (root / "docs" / "artifact-ledger.json").write_text(json.dumps(ledger, indent=2) + "\n")
    (root / "docs" / "current-context.json").write_text(json.dumps({"current_milestone": {"id": "M-1"}}))
    subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)


def run_cli(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CLI), "--root", str(root), *args],
        text=True, capture_output=True, check=False,
    )


def make_proposal(root: Path, name: str, entry: dict) -> Path:
    ledger_path = root / "docs" / "artifact-ledger.json"
    path = root / name
    path.write_text(json.dumps({
        "schema": "openbrcm-research-proposal/v1",
        "base_ledger_sha256": digest(ledger_path),
        "entries": [entry],
    }))
    return path


class ResearchIndexTests(unittest.TestCase):
    def test_existing_v1_ledger_is_read_without_rewriting(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, schema="openbrcm-artifact-ledger/v1")
            ledger = root / "docs" / "artifact-ledger.json"
            before = ledger.read_bytes()
            result = run_cli(root, "status")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("research_entries=0", result.stdout)
            self.assertEqual(ledger.read_bytes(), before)

    def test_v1_ledger_accepts_optional_research_entries_without_schema_migration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, schema="openbrcm-artifact-ledger/v1")
            proposal_path = make_proposal(root, "proposal.json", fixture_entry())
            result = run_cli(root, "update", "--proposal", proposal_path.name)
            self.assertEqual(result.returncode, 0, result.stderr)
            ledger = json.loads((root / "docs" / "artifact-ledger.json").read_text())
            self.assertEqual(ledger["schema"], "openbrcm-artifact-ledger/v1")
            self.assertEqual(len(ledger["records"]), 1)
            self.assertEqual(len(ledger["research_entries"]), 1)

    def test_locate_matches_topic_chip_and_capture_time_bdf(self):
        entry = fixture_entry()
        entry["device_identities"] = [{"chip": "BCM4360", "capture_id": "old-up", "capture_time_bdf": "0000:05:00.0"}]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, [entry])
            by_topic = run_cli(root, "locate", "--topic", "0x164")
            self.assertEqual(by_topic.returncode, 0, by_topic.stderr)
            self.assertIn("fixture.phy-revision", by_topic.stdout)
            by_chip = run_cli(root, "locate", "--chip", "BCM4360")
            self.assertIn("fixture.phy-revision", by_chip.stdout)
            by_bdf = run_cli(root, "locate", "--bdf", "0000:05:00.0")
            self.assertIn("old-up", by_bdf.stdout)

    def test_verify_preserves_conflicting_and_superseded_evidence_labels(self):
        entry = fixture_entry()
        entry["superseded_by"] = ["fixture.phy-revision.v2"]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, [entry, {**entry, "research_id": "fixture.phy-revision.v2", "status": "PROVEN", "superseded_by": [], "supersedes": ["fixture.phy-revision"]}])
            result = run_cli(root, "verify", "--full")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("2 research entries", result.stdout)
            located = run_cli(root, "locate", "--topic", "revision")
            self.assertIn("Synthetic 0x0a input", located.stdout)
            self.assertIn("superseded", located.stdout.lower())

    def test_duplicate_identifiers_and_invalid_relative_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bad = fixture_entry(report="../outside.md")
            make_root(root, [bad, bad.copy()])
            result = run_cli(root, "verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("duplicate research_id", result.stderr)
            self.assertIn("unsafe relative path", result.stderr)

    def test_dot_component_paths_and_unknown_dependencies_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bad = fixture_entry(report="./docs/report.md")
            bad["dependencies"] = ["missing.research-id"]
            make_root(root, [bad])
            result = run_cli(root, "verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unsafe relative path", result.stderr)
            self.assertIn("unknown research_id", result.stderr)

    def test_symlink_escape_is_rejected_without_following_it(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as outside:
            root = Path(tmp)
            make_root(root)
            (root / "docs" / "escape").symlink_to(outside, target_is_directory=True)
            entry = fixture_entry(report="docs/escape/secret.md")
            ledger_path = root / "docs" / "artifact-ledger.json"
            ledger = json.loads(ledger_path.read_text())
            ledger["research_entries"] = [entry]
            ledger_path.write_text(json.dumps(ledger))
            result = run_cli(root, "verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("escapes project root", result.stderr)

    def test_missing_indexed_artifact_is_reported(self):
        entry = fixture_entry(evidence="docs/missing.json")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, [entry])
            result = run_cli(root, "verify")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing indexed artifact", result.stderr)

    def test_explicit_artifact_hash_mismatch_is_rejected(self):
        entry = fixture_entry()
        entry["supporting_artifacts"][0]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, [entry])
            fast = run_cli(root, "verify")
            self.assertEqual(fast.returncode, 0, fast.stderr)
            result = run_cli(root, "verify", "--full")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("hash mismatch", result.stderr)

    def test_proposal_is_separate_until_primary_update(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root)
            ledger_path = root / "docs" / "artifact-ledger.json"
            before = digest(ledger_path)
            entry_path = root / "entry.json"
            entry_path.write_text(json.dumps(fixture_entry()))
            proposal_path = root / "proposal.json"
            proposal = run_cli(root, "propose", "--entries", "entry.json", "--out", "proposal.json")
            self.assertEqual(proposal.returncode, 0, proposal.stderr)
            self.assertEqual(digest(ledger_path), before)
            payload = json.loads(proposal_path.read_text())
            self.assertEqual(payload["base_ledger_sha256"], before)
            self.assertEqual(len(payload["entries"]), 1)

    def test_proposed_amendment_requires_exact_previous_entry_hash_and_preserves_legacy_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = fixture_entry()
            make_root(root, [original])
            ledger_path = root / "docs" / "artifact-ledger.json"
            before = json.loads(ledger_path.read_text())
            amended = json.loads(json.dumps(original))
            amended["contradictions"] = [
                "PHY type 0x0b is AC-PHY; retained PHYVERSION 0xcb01 has low nibble 1, while selected tests use PI+0x164=0x0a; the relationship and reaching value remain unresolved."
            ]
            input_path = root / "amended.json"
            input_path.write_text(json.dumps(amended))
            proposal_path = root / "proposal.json"

            proposed = run_cli(root, "propose", "--entries", input_path.name, "--out", proposal_path.name)
            self.assertEqual(proposed.returncode, 0, proposed.stderr)
            proposal = json.loads(proposal_path.read_text())
            old_digest = hashlib.sha256(
                (json.dumps(original, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode()
            ).hexdigest()
            self.assertEqual(proposal["replaces"], {original["research_id"]: old_digest})
            self.assertEqual(json.loads(ledger_path.read_text()), before)

            updated = run_cli(root, "update", "--proposal", proposal_path.name)
            self.assertEqual(updated.returncode, 0, updated.stderr)
            after = json.loads(ledger_path.read_text())
            self.assertEqual(after["records"], before["records"])
            self.assertEqual(after["research_entries"], [amended])

    def test_changed_existing_entry_without_replacement_hash_conflicts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            original = fixture_entry()
            make_root(root, [original])
            ledger_path = root / "docs" / "artifact-ledger.json"
            before = ledger_path.read_bytes()
            amended = json.loads(json.dumps(original))
            amended["title"] = "Changed without reviewed base"
            proposal_path = root / "proposal.json"
            proposal_path.write_text(json.dumps({
                "schema": "openbrcm-research-proposal/v1",
                "base_ledger_sha256": digest(ledger_path),
                "entries": [amended],
            }))
            result = run_cli(root, "update", "--proposal", proposal_path.name)
            self.assertEqual(result.returncode, 3)
            self.assertIn("previous-entry SHA256", result.stderr)
            self.assertEqual(ledger_path.read_bytes(), before)

    def test_proposal_collision_does_not_overwrite_existing_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root)
            ledger_path = root / "docs" / "artifact-ledger.json"
            input_path = root / "entry.json"
            input_path.write_text(json.dumps(fixture_entry()))
            output_path = root / "proposal.json"
            output_path.write_text("owner data\n")
            before = output_path.read_bytes()
            result = run_cli(root, "propose", "--entries", input_path.name, "--out", output_path.name)
            self.assertEqual(result.returncode, 3)
            self.assertIn("already exists with different content", result.stderr)
            self.assertEqual(output_path.read_bytes(), before)
            self.assertTrue(ledger_path.exists())

    def test_concurrent_proposals_for_same_output_are_serialized(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root)
            first = fixture_entry()
            second = fixture_entry()
            second["title"] = "A distinct concurrent proposal"
            (root / "first.json").write_text(json.dumps(first))
            (root / "second.json").write_text(json.dumps(second))
            lock_rel = subprocess.run(
                ["git", "-C", str(root), "rev-parse", "--git-path", "openbrcm-research-ledger.lock"],
                check=True, text=True, capture_output=True,
            ).stdout.strip()
            lock_path = Path(lock_rel)
            if not lock_path.is_absolute():
                lock_path = root / lock_path
            shared_output = "shared-proposal.json"

            with lock_path.open("a+b") as lock_stream:
                fcntl.flock(lock_stream.fileno(), fcntl.LOCK_EX)
                command = [sys.executable, str(CLI), "--root", str(root), "propose"]
                a = subprocess.Popen(command + ["--entries", "first.json", "--out", shared_output], cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                b = subprocess.Popen(command + ["--entries", "second.json", "--out", shared_output], cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                time.sleep(0.2)
                waited_for_lock = a.poll() is None and b.poll() is None
                fcntl.flock(lock_stream.fileno(), fcntl.LOCK_UN)
                out_a, err_a = a.communicate(timeout=10)
                out_b, err_b = b.communicate(timeout=10)

            self.assertTrue(waited_for_lock, (out_a, err_a, out_b, err_b))
            self.assertEqual(sorted([a.returncode, b.returncode]), [0, 3], (out_a, err_a, out_b, err_b))
            payload = json.loads((root / shared_output).read_text())
            self.assertIn(payload["entries"][0]["title"], {first["title"], second["title"]})

    def test_update_is_atomic_and_idempotent_for_same_entry(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root)
            ledger_path = root / "docs" / "artifact-ledger.json"
            proposal_path = root / "proposal.json"
            proposal_path.write_text(json.dumps({"schema": "openbrcm-research-proposal/v1", "base_ledger_sha256": digest(ledger_path), "entries": [fixture_entry()]}))
            first = run_cli(root, "update", "--proposal", "proposal.json")
            self.assertEqual(first.returncode, 0, first.stderr)
            after = digest(ledger_path)
            second = run_cli(root, "update", "--proposal", "proposal.json")
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertIn("no change", second.stdout.lower())
            self.assertEqual(digest(ledger_path), after)

    def test_stale_proposal_conflicts_instead_of_last_writer_wins(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root)
            ledger_path = root / "docs" / "artifact-ledger.json"
            base = digest(ledger_path)
            p1 = root / "p1.json"
            p2 = root / "p2.json"
            p1.write_text(json.dumps({"schema": "openbrcm-research-proposal/v1", "base_ledger_sha256": base, "entries": [fixture_entry()]}))
            other = fixture_entry()
            other["research_id"] = "fixture.other"
            p2.write_text(json.dumps({"schema": "openbrcm-research-proposal/v1", "base_ledger_sha256": base, "entries": [other]}))
            one = run_cli(root, "update", "--proposal", "p1.json")
            two = run_cli(root, "update", "--proposal", "p2.json")
            self.assertEqual(one.returncode, 0, one.stderr)
            self.assertNotEqual(two.returncode, 0)
            self.assertIn("conflict", two.stderr.lower())
            saved = json.loads(ledger_path.read_text())
            self.assertEqual([x["research_id"] for x in saved["research_entries"]], ["fixture.phy-revision"])

    def test_two_process_writers_conflict_instead_of_losing_an_update(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root)
            p1 = make_proposal(root, "p1.json", fixture_entry())
            other = fixture_entry()
            other["research_id"] = "fixture.second"
            p2 = make_proposal(root, "p2.json", other)
            command = [sys.executable, str(CLI), "--root", str(root), "update"]
            a = subprocess.Popen(command + ["--proposal", p1.name], cwd=root, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            b = subprocess.Popen(command + ["--proposal", p2.name], cwd=root, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            out_a, err_a = a.communicate()
            out_b, err_b = b.communicate()
            self.assertEqual(sorted([a.returncode, b.returncode]), [0, 3], (out_a, err_a, out_b, err_b))
            saved = json.loads((root / "docs" / "artifact-ledger.json").read_text())
            self.assertEqual(len(saved["research_entries"]), 1)

    def test_ignored_contract_is_reported_without_claiming_persistence(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root)
            (root / ".gitignore").write_text("contracts/\n")
            (root / "contracts").mkdir()
            (root / "contracts" / "accepted.json").write_text("{}\n")
            entry = fixture_entry(report="contracts/accepted.json", evidence="docs/evidence.json")
            ledger_path = root / "docs" / "artifact-ledger.json"
            ledger = json.loads(ledger_path.read_text())
            ledger["research_entries"] = [entry]
            ledger_path.write_text(json.dumps(ledger))
            result = run_cli(root, "verify")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Git-ignored", result.stdout)

    def test_tree_is_deterministic_reports_staleness_and_never_follows_symlinks(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as outside:
            root = Path(tmp)
            make_root(root)
            (root / "docs" / "public.txt").write_text("public\n")
            (root / "docs" / "firmware.bin").write_bytes(b"private\0data")
            (root / "docs" / "escape").symlink_to(outside, target_is_directory=True)
            (Path(outside) / "never-list.txt").write_text("secret")
            (root / ".gitignore").write_text("docs/firmware.bin\n")
            first = run_cli(root, "tree", "--out", "project_structure.txt", "--write")
            self.assertEqual(first.returncode, 0, first.stderr)
            tree_path = root / "project_structure.txt"
            text = tree_path.read_text()
            self.assertIn("IGNORED", text)
            self.assertIn("SENSITIVE", text)
            self.assertIn("tracked", text.lower())
            self.assertNotIn("never-list.txt", text)
            mtime = tree_path.stat().st_mtime_ns
            again = run_cli(root, "tree", "--out", "project_structure.txt", "--write")
            self.assertEqual(again.returncode, 0, again.stderr)
            self.assertEqual(tree_path.stat().st_mtime_ns, mtime)
            (root / "docs" / "public.txt").write_text("changed\n")
            stale = run_cli(root, "tree", "--out", "project_structure.txt", "--check")
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("stale", stale.stderr.lower())

    def test_preflight_reports_topic_and_registry_state(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, [fixture_entry()])
            result = run_cli(root, "preflight", "--topic", "PHY revision")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("research_registry=docs/artifact-ledger.json entries=1 status=PASS", result.stdout)
            self.assertIn("fixture.phy-revision", result.stdout)

    def test_preflight_separates_registry_availability_from_historical_admission(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_root(root, [fixture_entry()])
            result = run_cli(root, "preflight")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("research_index_available=YES", result.stdout)
            self.assertIn("historical_re_admission=NOT_PERFORMED_BY_PREFLIGHT", result.stdout)


class AtomicWriteTests(unittest.TestCase):
    def test_failed_replace_leaves_original_file_intact(self):
        module_path = ROOT / "scripts" / "research-index-core.py"
        spec = importlib.util.spec_from_file_location("research_index_core_test", module_path)
        self.assertIsNotNone(spec)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "ledger.json"
            target.write_bytes(b"old\n")
            original_replace = module.os.replace
            def fail_replace(*_args, **_kwargs):
                raise OSError("simulated interrupted atomic replacement")
            module.os.replace = fail_replace
            try:
                with self.assertRaises(OSError):
                    module.atomic_write(target, b"new\n")
            finally:
                module.os.replace = original_replace
            self.assertEqual(target.read_bytes(), b"old\n")


class SkillIntegrationTests(unittest.TestCase):
    def test_project_skill_is_present_and_points_to_authoritative_ledger(self):
        skill = ROOT / ".agents" / "skills" / "openbrcm-research" / "SKILL.md"
        self.assertTrue(skill.is_file())
        text = skill.read_text()
        self.assertIn("docs/artifact-ledger.json", text)
        self.assertIn("scripts/research-index.py preflight", text)

    def test_curated_firmware_record_points_to_retained_report_not_binary_payload(self):
        result = run_cli(ROOT, "locate", "--topic", "ARM and D11 firmware runtime")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("bcm43xx.firmware-runtime", result.stdout)
        self.assertIn("43xx/openbrcm-wl-runtime-investigation-20261011T200000Z/wl_firmware_runtime_report.md", result.stdout)
        self.assertNotIn("non_redistributable_research_inputs", result.stdout)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regression tests for the D4 PHY/radio op inventory and pi_fptr tooling.

Protected behaviour
-------------------
A. `scripts/analyze_phy_radio_ops.py` guards:
   - it refuses to run against a re.db whose blob identity or schema differs;
   - `--check` is non-zero when the generated output is stale or missing;
   - the generated JSON really is whole-blob (row count, max PC, and the
     explicit statement that the CLI would have truncated it);
   - every chain address is resolved from re.db, never hard-coded;
   - the acphy closure is asserted not to reach the abgphy family (D4-R1);
   - a function in AC_CHAIN that is missing from re.db is a hard failure,
     not a silently dropped step.

B. `scripts/check_pifptr_installers.py`:
   - it REFUTES the "attach_acphy zeroes pi_fptr" claim;
   - `--expect-zeroed` therefore exits non-zero (the refutation is falsifiable);
   - the acphy store at 0xa3001 resolves to sub_b018f, i.e. an install;
   - the relocation really does land on the instruction's immediate field.

C. The two new analysis artifacts are internally consistent and every path
   they reference exists.

D. The three superseded inventories carry a SUPERSEDED banner and stay
   parseable JSON.

Test architecture
-----------------
Everything runs against the real repository as read-only input, plus a
TemporaryDirectory copy for the mutation cases (stale output, wrong re.db).
The real tree is never written to. Assertions target the stable `ok:` /
`FAIL:` marker strings rather than full output.
"""

import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
INVENTORY = ROOT / "scripts" / "analyze_phy_radio_ops.py"
INSTALLERS = ROOT / "scripts" / "check_pifptr_installers.py"
GEN_JSON = ROOT / "docs" / "m34d4" / "phy_radio_ops.json"
GEN_MD = ROOT / "docs" / "m34d4" / "phy_radio_ops.md"
PROOF = ROOT / "docs" / "m34d4" / "d4_pifptr_relocation_proof.md"

SUPERSEDED_INVENTORIES = (
    "docs/m34d4/phy_operations_rev42.json",
    "docs/m34d4/radio_operations_rev42.json",
    "docs/m34d4b/acphy_phy_ops.json",
    "docs/m34d4b/acphy_radio_ops.json",
)


def load(path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def admitted_vendor():
    """Resolve the vendor image and its index from the repository manifest.

    The tooling root keeps its own copy of both, and a copy can predate the
    current image: its re.db still indexes the pre-repair blob (a2a4ce12...).
    The suite therefore takes its defaults from docs/re-image-manifest.json --
    the repository's single identity source -- rather than from those copies.
    If admission fails the empty record is returned and each tool keeps its own
    path, where its identity guard still fails closed.
    """
    if getattr(admitted_vendor, "cache", None) is None:
        r = subprocess.run(
            [sys.executable, str(ROOT / "scripts" / "re-image.py"),
             "admit", "--image", "vendor"],
            capture_output=True, text=True, cwd=str(ROOT),
            env={k: v for k, v in os.environ.items() if k != "RE_IMAGE_MANIFEST"},
        )
        record = {}
        if r.returncode == 0:
            try:
                record = json.loads(r.stdout)
            except json.JSONDecodeError:
                record = {}
        admitted_vendor.cache = record
    return admitted_vendor.cache


def run(script: pathlib.Path, *args, cwd=None):
    command = [sys.executable, str(script), *args]
    record = admitted_vendor()
    # The inventory generator predates RE_DB and selects the canonical DB by
    # default. Forward an admitted DB to subprocesses, including the temporary
    # repository copy used by the stale-output test. An explicit RE_DB wins.
    pinned_db = os.environ.get("RE_DB") or record.get("database_path")
    if script.name == INVENTORY.name and pinned_db and "--db" not in args:
        command.extend(("--db", pinned_db))
    # Keep the default-path provenance test fail-closed, but let an admitted
    # vendor record select its pinned blob for the integration suite. The
    # analyzer and the installer check still verify the image hash themselves.
    pinned_blob = os.environ.get("OPENBRCM_RE_BLOB") or record.get("selected_image_path")
    if script == INSTALLERS and pinned_blob:
        command.extend(("--blob", pinned_blob))
    return subprocess.run(
        command,
        capture_output=True, text=True, cwd=str(cwd or ROOT),
    )


class InventoryGeneratorTests(unittest.TestCase):
    """A."""

    def test_module_imports_and_exposes_guards(self):
        mod = load(INVENTORY)
        self.assertTrue(callable(mod.main))
        self.assertTrue(callable(mod.resolve_addresses))
        self.assertTrue(callable(mod.legacy_inventory_survey))
        # The abgphy exclusion is the executable form of the D4-R1 correction.
        self.assertIn("wlc_phy_init_aphy", mod.ABGPHY_MUST_BE_ABSENT)
        self.assertIn("wlc_phy_attach_abgphy", mod.ABGPHY_MUST_BE_ABSENT)

    def test_check_respects_committed_db_provenance(self):
        r = run(INVENTORY, "--check")
        committed_db = json.loads(GEN_JSON.read_text(encoding="utf-8"))["source"]["re_db"]
        selected_db = os.environ.get("RE_DB")
        if selected_db and pathlib.Path(selected_db).resolve() != pathlib.Path(committed_db).resolve():
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("stale", r.stdout)
            return
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ok: docs/m34d4/phy_radio_ops.json is current", r.stdout)

    def test_generated_json_is_whole_blob(self):
        doc = json.loads(GEN_JSON.read_text(encoding="utf-8"))
        comp = doc["completeness"]
        self.assertGreater(comp["phy_ops_rows"], 2000)
        self.assertTrue(comp["cli_would_truncate"])
        self.assertGreater(comp["rows_above_cli_cut"], 0)
        # Whole-blob means it reaches past the CLI's last PC into the radio
        # region, which is exactly where the CLI silently stopped.
        self.assertGreater(int(comp["max_pc"], 16), 0xC0000)
        self.assertEqual(doc["source"]["schema_version"], "5")

    def test_chain_addresses_are_resolved_not_hardcoded(self):
        """AC_CHAIN must carry no address literals.

        An earlier revision hard-coded them and three of six were wrong. The
        regression is that the tuple shape stays (name, role).
        """
        mod = load(INVENTORY)
        for entry in mod.AC_CHAIN:
            self.assertEqual(len(entry), 2, entry)
            self.assertNotIn("0x", entry[0])
        src = INVENTORY.read_text(encoding="utf-8")
        self.assertNotIn('("wlc_phy_init", "0x', src)

    def test_resolved_chain_addresses_match_re_db(self):
        doc = json.loads(GEN_JSON.read_text(encoding="utf-8"))
        for step in doc["ac_path"]["chain"]:
            self.assertRegex(step["addr"], r"^0x[0-9a-f]+$")
            self.assertGreater(step["size"], 0)
            self.assertTrue(step["in_closure"], step["function"])

    def test_abgphy_absent_from_ac_closure(self):
        doc = json.loads(GEN_JSON.read_text(encoding="utf-8"))
        names = {r["function"] for r in doc["ac_path"]["per_function"]}
        for forbidden in doc["ac_path"]["abgphy_absent_from_closure"]:
            self.assertNotIn(forbidden, names)

    def test_dispatcher_step_reports_callee_attribution(self):
        """wlc_phy_cal_perical has 0 direct ops; that must be explained."""
        doc = json.loads(GEN_JSON.read_text(encoding="utf-8"))
        steps = {s["function"]: s for s in doc["ac_path"]["chain"]}
        perical = steps["wlc_phy_cal_perical"]
        self.assertEqual(perical["direct_ops"], 0)
        self.assertTrue(perical["ops_via_callees"])
        self.assertIn("sub_b018f", steps)

    def test_legacy_survey_reads_files_not_claims(self):
        doc = json.loads(GEN_JSON.read_text(encoding="utf-8"))
        cov = doc["legacy_inventory_coverage"]
        self.assertEqual(len(cov["files"]), len(SUPERSEDED_INVENTORIES))
        for rec in cov["files"]:
            self.assertTrue(rec["present"], rec["file"])
        # The whole point: these three were absent from every legacy file.
        for missing in ("wlc_phy_init", "sub_b018f", "wlc_phy_cal_perical"):
            self.assertIn(missing, cov["ac_path_functions_absent"])

    def test_check_fails_on_stale_output(self):
        with tempfile.TemporaryDirectory() as td:
            copy = pathlib.Path(td) / "repo"
            shutil.copytree(
                ROOT, copy, symlinks=True,
                ignore=shutil.ignore_patterns(".git", "__pycache__", "*.o"),
            )
            target = copy / "docs" / "m34d4" / "phy_radio_ops.md"
            target.write_text("stale\n", encoding="utf-8")
            r = run(copy / "scripts" / "analyze_phy_radio_ops.py", "--check",
                    cwd=copy)
            self.assertEqual(r.returncode, 1)
            self.assertIn("stale", r.stdout)

    def test_refuses_wrong_schema(self):
        with tempfile.TemporaryDirectory() as td:
            fake = pathlib.Path(td) / "re.db"
            import sqlite3

            con = sqlite3.connect(fake)
            con.execute("create table meta (key text, value text)")
            con.execute("insert into meta values ('sha256', ?)",
                        ("0" * 64,))
            con.execute("insert into meta values ('schema_version', '5')")
            con.commit()
            con.close()
            r = run(INVENTORY, "--check", "--db", str(fake))
            self.assertEqual(r.returncode, 1)
            self.assertIn("FAIL", r.stderr)


class PifptrRelocationTests(unittest.TestCase):
    """B."""

    def test_refutes_the_zeroing_claim(self):
        r = run(INSTALLERS)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("VERDICT         : REFUTED", r.stdout)
        self.assertIn("-> INSTALLS   : 48", r.stdout)
        self.assertIn("-> REAL ZEROS : 0", r.stdout)

    def test_expect_zeroed_flag_fails(self):
        """The refutation must be falsifiable, not a hardcoded verdict."""
        r = run(INSTALLERS, "--expect-zeroed")
        self.assertEqual(r.returncode, 1)
        self.assertIn("refutes", r.stderr)

    def test_acphy_site_installs_sub_b018f(self):
        r = run(INSTALLERS, "--json")
        doc = json.loads(r.stdout)
        self.assertEqual(doc["verdict"], "REFUTED")
        by_insn = {x["insn"]: x for x in doc["results"]}
        site = by_insn["0xa3001"]
        self.assertEqual(site["field"], "+0x28")
        self.assertEqual(site["resolved_target"], "0xb018f")
        self.assertEqual(site["placeholder_printed_by_objdump"], "0x0")
        # The relocation must sit on the immediate field, four bytes in.
        self.assertEqual(int(site["immediate_field_at"], 16),
                         int(site["insn"], 16) + 4)
        self.assertTrue(site["reloc_lands_on_immediate"])

    def test_all_four_acphy_slots_installed(self):
        r = run(INSTALLERS, "--json")
        doc = json.loads(r.stdout)
        by_insn = {x["insn"]: x for x in doc["results"]}
        expect = {
            "0xa3001": ("+0x28", "0xb018f"),
            "0xa3009": ("+0x30", "0x8e77a"),
            "0xa3016": ("+0x38", "0xa7089"),
            "0xa301e": ("+0x40", "0x9949f"),
        }
        for insn, (field, target) in expect.items():
            self.assertEqual(by_insn[insn]["field"], field)
            self.assertEqual(by_insn[insn]["resolved_target"], target)

    def test_no_real_zero_stores_anywhere(self):
        r = run(INSTALLERS, "--json")
        doc = json.loads(r.stdout)
        self.assertEqual(doc["true_zero_stores"], 0)
        for rec in doc["results"]:
            self.assertTrue(rec["reloc_lands_on_immediate"], rec["insn"])

    def test_blob_identity_is_verified(self):
        mod = load(INSTALLERS)
        self.assertEqual(
            mod.EXPECTED_BLOB_SHA256,
            "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743",
        )


class ArtifactConsistencyTests(unittest.TestCase):
    """C."""

    def test_proof_exists_and_names_the_key_evidence(self):
        text = PROOF.read_text(encoding="utf-8")
        for token in ("0xa3005", "0xb018f", "sub_b018f", "R_X86_64_32S",
                      "0x8c3f9", "check_pifptr_installers.py"):
            self.assertIn(token, text)

    def test_proof_does_not_claim_hardware_proven(self):
        text = PROOF.read_text(encoding="utf-8")
        self.assertIn("HARDWARE PROVEN` is NOT claimed", text)

    def test_corrected_docs_point_at_the_proof(self):
        targets = [
            "docs/m34d3_bsinitvals.md",
            "docs/m34d4/d4_checkpoint_analysis_v2.md",
            "docs/m34d4/wlc_phy_init_rev42_flow_v2.json",
            "docs/m34d4b/acphy_function_table.json",
            "docs/m34d4b/ghidra_augmentation.md",
        ]
        for rel in targets:
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIn(
                "d4_pifptr_relocation_proof", text,
                "%s does not reference the proof" % rel,
            )

    def test_bsinitvals_no_longer_names_init_aphy_as_the_ac_target(self):
        """Every surviving mention must be MARKED as corrected.

        Line numbers are not the test: what matters is that no mention stands
        as an unmarked assertion. A mention is acceptable only on a line that
        carries a correction marker, or inside the A.9a correction section.
        """
        text = (ROOT / "docs" / "m34d3_bsinitvals.md").read_text(encoding="utf-8")
        lines = text.splitlines()
        start = next(
            i for i, l in enumerate(lines) if l.startswith("## A.9a")
        )
        markers = ("[Corrected", "SUPERSEDED", "WRONG", "abgphy",
                   "wlc_phy_attach_abgphy", "not merely")
        for i, line in enumerate(lines, 1):
            if "wlc_phy_init_aphy" not in line:
                continue
            in_section = i - 1 >= start
            marked = any(m in line for m in markers)
            self.assertTrue(
                in_section or marked,
                "line %d asserts wlc_phy_init_aphy with no correction "
                "marker: %s" % (i, line.strip()),
            )

    def test_bsinitvals_correction_section_states_both_errors(self):
        text = (ROOT / "docs" / "m34d3_bsinitvals.md").read_text(encoding="utf-8")
        self.assertIn("## A.9a", text)
        self.assertIn("0xa3005", text)
        self.assertIn("sub_b018f", text)
        # The finding that SURVIVES must be stated too, so a future reader
        # does not over-correct and null out +0x110/+0x118 as well.
        self.assertIn("+0x110", text)

    def test_flow_v2_marks_the_verdict_superseded(self):
        doc = json.loads(
            (ROOT / "docs" / "m34d4" /
             "wlc_phy_init_rev42_flow_v2.json").read_text(encoding="utf-8")
        )
        v = doc["ac_path_verdict"]
        self.assertEqual(v["_status"], "SUPERSEDED")
        self.assertIn("SUPERSEDED", v["installer_proof"])
        self.assertIn("SUPERSEDED", v["conclusion"])
        # The surviving finding must not be swept away with the false one.
        self.assertIn("UNCHANGED", v["indirect_0x118"])

    def test_json_artifacts_stay_parseable(self):
        for rel in ("docs/m34d4/wlc_phy_init_rev42_flow_v2.json",
                    "docs/m34d4b/acphy_function_table.json",
                    "docs/m34d4/phy_radio_ops.json"):
            json.loads((ROOT / rel).read_text(encoding="utf-8"))


class SupersededInventoryTests(unittest.TestCase):
    """D."""

    def test_each_legacy_inventory_is_bannerd_and_valid(self):
        for rel in SUPERSEDED_INVENTORIES:
            path = ROOT / rel
            doc = json.loads(path.read_text(encoding="utf-8"))
            banner = doc.get("_superseded")
            self.assertIsNotNone(banner, "%s has no banner" % rel)
            self.assertEqual(banner["status"], "SUPERSEDED")
            self.assertEqual(banner["by"], "docs/m34d4/phy_radio_ops.json")
            self.assertIn("ledger_record", banner)
            # Banner must be the FIRST key so it is what a reader sees.
            first = list(doc.keys())[0]
            self.assertEqual(first, "_superseded", rel)

    def test_canonical_inventory_names_what_it_supersedes(self):
        text = GEN_MD.read_text(encoding="utf-8")
        for rel in SUPERSEDED_INVENTORIES:
            self.assertIn(rel, text)


if __name__ == "__main__":
    unittest.main(verbosity=2)

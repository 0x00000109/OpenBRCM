#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Tests for the source-first Broadcom lookup subordinate to research-index."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import research_source_index as source_index

CLI = ROOT / "scripts" / "research-index.py"
INDEX_REL = Path("docs/research/source-catalog/source-index-v2.json")


def run_cli(root: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CLI), "--root", str(root), *args],
        text=True, capture_output=True, check=False,
    )


def seed_source_tree(root: Path) -> None:
    primary = root / "docs/old/amd64/src/include"
    primary.mkdir(parents=True)
    (primary / "pcicfg.h").write_text(
        """/* Copyright Broadcom Corporation. Permission to use, copy, modify and distribute. */
#define PCI_BAR0_WIN 0x80
#define PCI_BAR0_WINSZ (16 * 1024)
#if defined(BCMPCI) && !defined(BCMDBG)
typedef unsigned short u16;
struct si_pub { u16 chip; unsigned int corerev; };
extern int si_setcoreidx(struct si_pub *sih, unsigned int idx);
#endif
"""
    )
    (primary / "si.c").write_text(
        """#include \"pcicfg.h\"
int si_setcoreidx(struct si_pub *sih, unsigned int idx)
{
    return (int)idx + (int)sih->corerev;
}
"""
    )
    duplicate = root / "old/amd64/src/include"
    duplicate.mkdir(parents=True)
    (duplicate / "pcicfg.h").write_text((primary / "pcicfg.h").read_text())
    (root / "external/b43-ac-wip").mkdir(parents=True)
    (root / "external/b43-ac-wip/phy.h").write_text(
        "/* SPDX-License-Identifier: GPL-3.0-only */\n#define PHY_TEST 7\n"
    )
    (root / "external/b43-ac-wip/pcicfg.h").write_text("#define PCI_BAR0_WIN 0x90\n")
    (root / "docs/old/amd64/lib").mkdir(parents=True)
    (root / "docs/old/amd64/lib/wlc_hybrid.o_shipped").write_bytes(b"\x7fELF" + b"x" * 128)
    subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)


class BroadcomSourceIndexTests(unittest.TestCase):
    def test_build_is_deterministic_idempotent_and_check_detects_staleness(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            output = root / INDEX_REL
            first_hash = hashlib.sha256(output.read_bytes()).hexdigest()
            second = run_cli(root, "source", "build", "--write")
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), first_hash)
            self.assertEqual(run_cli(root, "source", "build", "--check").returncode, 0)
            (root / "docs/old/amd64/src/include/pcicfg.h").write_text("#define PCI_BAR0_WIN 0x88\n")
            stale = run_cli(root, "source", "build", "--check")
            self.assertNotEqual(stale.returncode, 0)
            self.assertIn("STALE", stale.stderr)

    def test_generated_bytes_do_not_change_when_git_admits_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            built = run_cli(root, "source", "build", "--write")
            self.assertEqual(built.returncode, 0, built.stderr)
            index_hash = hashlib.sha256((root / INDEX_REL).read_bytes()).hexdigest()
            catalog_hash = hashlib.sha256((root / "docs/research/source-catalog/README.md").read_bytes()).hexdigest()
            subprocess.run(["git", "-C", str(root), "add", "docs/old/amd64"], check=True)
            self.assertEqual(run_cli(root, "source", "build", "--check").returncode, 0)
            rebuilt = run_cli(root, "source", "build", "--write")
            self.assertEqual(rebuilt.returncode, 0, rebuilt.stderr)
            self.assertEqual(hashlib.sha256((root / INDEX_REL).read_bytes()).hexdigest(), index_hash)
            self.assertEqual(hashlib.sha256((root / "docs/research/source-catalog/README.md").read_bytes()).hexdigest(), catalog_hash)
            payload = json.loads((root / INDEX_REL).read_text())
            self.assertNotIn("git_state", json.dumps(payload))

    def test_missing_optional_roots_are_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)
            payload = source_index.build_index(root)
            self.assertEqual(payload["availability_status"], "NONE_PRESENT")
            self.assertEqual(payload["files_inventoried"], 0)
            states = {item["path"]: item["status"] for item in payload["source_availability"]}
            self.assertEqual(states["docs/old/amd64"], "MISSING")
            self.assertEqual(states["43xx/**/refs"], "MISSING")
            missing_index = run_cli(root, "source", "locate", "--query", "PCI_BAR0_WIN")
            self.assertNotEqual(missing_index.returncode, 0)
            self.assertIn("MISSING_SOURCE_INDEX", missing_index.stderr)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            no_sources = run_cli(root, "source", "locate", "--query", "PCI_BAR0_WIN")
            self.assertEqual(no_sources.returncode, 0, no_sources.stderr)
            self.assertIn("NO SOURCE MATCH; source_availability=NONE_PRESENT", no_sources.stdout)

    def test_partial_source_roots_are_labelled_and_locate_reports_availability(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            (root / "old/amd64").rename(root / "old/amd64.saved")
            built = run_cli(root, "source", "build", "--write")
            self.assertEqual(built.returncode, 0, built.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            self.assertEqual(payload["availability_status"], "PARTIAL")
            located = run_cli(root, "source", "locate", "--query", "PCI_BAR0_WIN")
            self.assertEqual(located.returncode, 0, located.stderr)
            self.assertIn("source_availability=PARTIAL", located.stdout)

    def test_locate_refuses_stale_or_disappeared_source_roots(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as outside:
            root = Path(tmp)
            seed_source_tree(root)
            self.assertEqual(run_cli(root, "source", "build", "--write").returncode, 0)
            (root / "docs/old/amd64").rename(root / "docs/old/amd64.saved")
            outside_header = Path(outside) / "escape.h"
            outside_header.write_text("#define PCI_BAR0_WIN 0xdead\n")
            (root / "docs/old/amd64").symlink_to(Path(outside), target_is_directory=True)
            result = run_cli(root, "source", "locate", "--query", "PCI_BAR0_WIN")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("UNSAFE_INPUT", result.stderr)
            self.assertNotIn("0xdead", result.stdout)

    def test_locate_refuses_index_after_source_content_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            self.assertEqual(run_cli(root, "source", "build", "--write").returncode, 0)
            header = root / "docs/old/amd64/src/include/pcicfg.h"
            header.write_text(header.read_text().replace("0x80", "0x81"))
            result = run_cli(root, "source", "locate", "--query", "PCI_BAR0_WIN")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("STALE_SOURCE_INDEX", result.stderr)

    def test_build_marks_symlinked_source_root_unsafe_without_following(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as outside:
            root = Path(tmp)
            subprocess.run(["git", "-C", str(root), "init", "-q"], check=True)
            (Path(outside) / "secret.h").write_text("#define OUTSIDE_SECRET 42\n")
            (root / "docs").mkdir()
            (root / "docs/old").symlink_to(Path(outside), target_is_directory=True)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            self.assertEqual(payload["availability_status"], "UNSAFE_INPUT")
            self.assertNotIn("OUTSIDE_SECRET", json.dumps(payload))
            located = run_cli(root, "source", "locate", "--query", "OUTSIDE_SECRET")
            self.assertNotEqual(located.returncode, 0)
            self.assertIn("UNSAFE_INPUT", located.stderr)

    def test_walk_errors_fail_instead_of_silently_indexing_a_partial_tree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "docs/old/amd64").mkdir(parents=True)

            def failing_walk(_base, *, onerror, **_kwargs):
                onerror(PermissionError(13, "permission denied", str(root / "docs/old/amd64/locked")))
                return iter(())

            with mock.patch.object(source_index.os, "walk", side_effect=failing_walk):
                with self.assertRaisesRegex(OSError, "incomplete traversal.*errno=13"):
                    source_index._walk_entries(root, "docs/old/amd64")

    def test_lookup_finds_constants_functions_struct_fields_and_conditions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            built = run_cli(root, "source", "build", "--write")
            self.assertEqual(built.returncode, 0, built.stderr)
            result = run_cli(root, "source", "locate", "--query", "PCI_BAR0_WIN")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(result.stdout.splitlines()[1].startswith("PCI_BAR0_WIN ["))
            self.assertIn("query_match=EXACT", result.stdout.splitlines()[1])
            self.assertIn("source_to_pinned_binary=UNKNOWN", result.stdout)
            self.assertIn("docs/old/amd64/src/include/pcicfg.h", result.stdout)
            payload = json.loads((root / INDEX_REL).read_text())
            self.assertTrue(any(f["path"].endswith("si.c") for f in payload["files"]))
            self.assertIn("si_setcoreidx", payload["symbols"])
            self.assertIn("struct:si_pub", payload["symbols"])
            self.assertIn("field:si_pub.corerev", payload["symbols"])
            self.assertTrue(payload["files"][0]["sha256"])
            header = next(f for f in payload["files"] if f["path"].endswith("pcicfg.h"))
            self.assertTrue(header["preprocessor_conditions"])
            self.assertEqual(header["license_classification"], "BROADCOM_SOURCE_NOTICE")
            self.assertNotIn("git_state", header)
            self.assertIn("PCI_BAR0_WINSZ", payload["symbols"])
            self.assertTrue(header["copyright_notice_lines"])
            self.assertIn("function_declaration", payload["symbols"]["si_setcoreidx"]["kinds"])
            self.assertIn("function_definition", payload["symbols"]["si_setcoreidx"]["kinds"])

    def test_duplicate_content_and_divergent_duplicate_names_are_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            self.assertTrue(payload["duplicate_content_groups"])
            self.assertTrue(payload["divergent_duplicate_basenames"])

    def test_identical_mirror_definitions_are_not_reported_as_conflicts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            replicas = payload["replicated_symbol_definitions"]
            self.assertTrue(any(item["symbol"] == "PCI_BAR0_WIN" for item in replicas))
            conflicts = payload["divergent_symbol_definitions"]
            bar_conflict = next(item for item in conflicts if item["symbol"] == "PCI_BAR0_WIN")
            self.assertEqual({item["detail"] for item in bar_conflict["definitions"]}, {"0x80", "0x90"})
            self.assertFalse(any(item["symbol"] == "si_setcoreidx" for item in conflicts))

    def test_inventory_covers_python_patch_and_build_source_materials(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            tools = root / "external/b43-ac-wip/tools"
            tools.mkdir()
            (tools / "decode.py").write_text("def decode_phy_port(value):\n    return value\n")
            (tools / "0001-fix.patch").write_text("--- a/phy.c\n+++ b/phy.c\n@@ -1 +1 @@\n-old\n+new\n")
            (root / "external/b43-ac-wip/Makefile").write_text("all:\n\t@echo source\n")
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            by_path = {item["path"]: item for item in payload["files"]}
            self.assertEqual(by_path["external/b43-ac-wip/tools/decode.py"]["language"], "python")
            self.assertEqual(by_path["external/b43-ac-wip/tools/0001-fix.patch"]["language"], "patch")
            self.assertEqual(by_path["external/b43-ac-wip/Makefile"]["language"], "make")
            self.assertIn("decode_phy_port", payload["symbols"])
            self.assertEqual(payload["files_inventoried"], len(payload["files"]))
            lookup = run_cli(root, "source", "locate", "--query", "decode_phy_port", "--kind", "function")
            self.assertEqual(lookup.returncode, 0, lookup.stderr)
            self.assertIn("external/b43-ac-wip/tools/decode.py", lookup.stdout)

    def test_collapsed_untracked_directory_is_reported_for_nested_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            subprocess.run(["git", "-C", str(root / "external/b43-ac-wip"), "init", "-q"], check=True)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            nested = next(
                item for item in payload["files"]
                if item["path"] == "external/b43-ac-wip/phy.h"
            )
            self.assertNotIn("git_state", nested)

    def test_compile_time_macro_alternatives_are_labeled_conditional(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            header = root / "docs/old/amd64/src/include/config_variant.h"
            header.write_text("#if defined(CONFIG_A)\n#define MODE_ID 1\n#else\n#define MODE_ID 2\n#endif\n")
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            variant = next(item for item in payload["conditional_symbol_variants"] if item["symbol"] == "MODE_ID")
            self.assertTrue(all(item["conditional_path"] for item in variant["definitions"]))

    def test_abi_lookup_preserves_pointer_width_and_conditional_typedef_context(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            header = root / "docs/old/amd64/src/include/abi.h"
            header.write_text(
                "#if defined(__x86_64__)\ntypedef unsigned long long uintptr;\n"
                "#else\ntypedef unsigned int uintptr;\n#endif\n"
                "typedef unsigned int uint32;\n"
                "struct public_abi { uintptr address; void *buffer; uint32 length; };\n"
            )
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            uintptr_defs = payload["symbols"]["uintptr"]["definitions"]
            self.assertTrue(any(d.get("conditional_path") for d in uintptr_defs))
            fields = payload["symbols"]
            self.assertIn("uintptr address", fields["field:public_abi.address"]["definitions"][0]["detail"])
            self.assertIn("void *buffer", fields["field:public_abi.buffer"]["definitions"][0]["detail"])

    def test_divergent_typedefs_are_reported_as_abi_candidates_not_resolved_layouts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            (root / "docs/old/amd64/src/include/abi_a.h").write_text("typedef unsigned int device_word;\n")
            (root / "external/b43-ac-wip/abi_b.h").write_text("typedef unsigned long device_word;\n")
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            candidate = next(item for item in payload["incompatible_typedef_candidates"] if item["symbol"] == "device_word")
            self.assertEqual(len(candidate["definitions"]), 2)
            self.assertTrue(any("ABI layout is not compiler-resolved" in item for item in payload["limits"]))

    def test_symlinked_source_is_recorded_without_following_or_hashing(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as outside:
            root = Path(tmp)
            seed_source_tree(root)
            (root / "43xx/test/refs").mkdir(parents=True)
            secret = Path(outside) / "outside.h"
            secret.write_text("#define OUTSIDE_SECRET 99\n")
            (root / "docs/old/amd64/src/include/escape.h").symlink_to(secret)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            escaped = next(f for f in payload["files"] if f["path"].endswith("escape.h"))
            self.assertEqual(escaped["kind"], "SYMLINK_NOT_FOLLOWED")
            self.assertIsNone(escaped["sha256"])
            self.assertNotIn("OUTSIDE_SECRET", (root / INDEX_REL).read_text())
            self.assertEqual(payload["availability_status"], "PARTIAL")
            root_status = next(
                item for item in payload["source_availability"]
                if item["path"] == "docs/old/amd64"
            )
            self.assertEqual(root_status["status"], "PRESENT_WITH_EXCLUSIONS")
            self.assertEqual(
                root_status["excluded_symlink_source_files"],
                ["docs/old/amd64/src/include/escape.h"],
            )

    def test_secure_reader_rejects_symlinked_parent_component(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as outside:
            root = Path(tmp)
            secret_dir = Path(outside)
            (secret_dir / "escape.h").write_text("#define OUTSIDE_SECRET 99\n")
            (root / "docs").mkdir()
            (root / "docs/old").symlink_to(secret_dir, target_is_directory=True)
            text, issue, raw = source_index._read_project_text(root, "docs/old/escape.h")
            self.assertIsNone(text)
            self.assertIsNotNone(issue)
            self.assertIsNone(raw)

    def test_proprietary_object_is_inventoried_without_read_or_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            files = payload["tree_snapshots"][0]["non_source_files"]
            obj = next(f for f in files if f["path"].endswith("wlc_hybrid.o_shipped"))
            self.assertEqual(obj["kind"], "PROPRIETARY_OR_BINARY_NOT_READ")
            self.assertIsNone(obj["sha256"])
            self.assertEqual(obj["bytes"], 132)

    def test_legacy_and_modern_chanspec_masks_remain_distinct(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            seed_source_tree(root)
            source = root / "docs/old/amd64/src/shared/bcmwifi/include"
            source.mkdir(parents=True)
            (source / "bcmwifi_channels.h").write_text(
                "#define WL_CHANSPEC_BW_MASK 0x3800\n#define WL_LCHANSPEC_BW_MASK 0x0c00\n"
            )
            result = run_cli(root, "source", "build", "--write")
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((root / INDEX_REL).read_text())
            self.assertIn("0x3800", payload["symbols"]["WL_CHANSPEC_BW_MASK"]["definitions"][0]["detail"])
            self.assertIn("0x0c00", payload["symbols"]["WL_LCHANSPEC_BW_MASK"]["definitions"][0]["detail"])
            self.assertTrue(any("ABI" in limit for limit in payload["limits"]))

    def test_original_research_locate_remains_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "docs").mkdir()
            (root / "docs/artifact-ledger.json").write_text(json.dumps({
                "schema": "openbrcm-artifact-ledger/v1", "records": [],
                "research_entries": [],
            }))
            result = run_cli(root, "locate", "--topic", "anything")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("NO MATCH", result.stdout)


if __name__ == "__main__":
    unittest.main()

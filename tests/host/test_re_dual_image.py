#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host-only tests for explicit vendor/wlko RE image selection."""

import hashlib
import json
import os
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
RESOLVER = ROOT / "scripts" / "re-image.py"
WRAPPER = ROOT / "scripts" / "re.sh"
REAL_RE = pathlib.Path("/media/kartashoff/Storage/opensource/iced/test/binary_analyzer/target/release/re")
REAL_VENDOR_DB = pathlib.Path("/media/kartashoff/Storage/opensource/iced/test/re.db")
REAL_WLKO_DB = pathlib.Path("/media/kartashoff/Storage/opensource/iced/test/re-wlko.db")


def write_db(path, functions, calls=()):
    conn = sqlite3.connect(path)
    conn.executescript(
        """
        CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
        CREATE TABLE sections(name TEXT, shndx INTEGER, addr INTEGER, size INTEGER,
                              offset INTEGER, kind TEXT);
        CREATE TABLE functions(id INTEGER PRIMARY KEY, name TEXT, addr INTEGER,
                               size INTEGER, shndx INTEGER, bind TEXT, origin TEXT,
                               confidence TEXT);
        CREATE TABLE calls(id INTEGER PRIMARY KEY, site INTEGER, src_id INTEGER,
                           src_addr INTEGER, dst_id INTEGER, dst_addr INTEGER,
                           dst_name TEXT, kind TEXT, target_kind TEXT,
                           via_reloc INTEGER, confidence TEXT);
        """
    )
    conn.executemany("INSERT INTO meta VALUES (?, ?)", [("schema_version", "5")])
    conn.executemany(
        "INSERT INTO sections VALUES (?, ?, ?, ?, ?, ?)",
        [(".text", 1, 0, 0x1000, 0, "code")],
    )
    conn.executemany(
        "INSERT INTO functions VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
        functions,
    )
    conn.executemany(
        "INSERT INTO calls VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        calls,
    )
    conn.commit()
    conn.close()


class DualImageTests(unittest.TestCase):
    def run_tool(self, manifest, *args):
        env = os.environ.copy()
        env["RE_IMAGE_MANIFEST"] = str(manifest)
        return subprocess.run(
            [sys.executable, str(RESOLVER), *args],
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
        )

    def make_manifest(self, root, vendor_db, wlko_db, vendor_blob, wlko_blob):
        vendor_blob.write_bytes(b"vendor")
        wlko_blob.write_bytes(b"wlko")
        return_data = {
            "schema": "openbrcm-re-image-manifest/v1",
            "default_image": "vendor",
            "images": {
                "vendor": {
                    "artifact_id": "vendor",
                    "artifact_kind": "proprietary_object",
                    "path": str(vendor_blob),
                    "sha256": hashlib.sha256(vendor_blob.read_bytes()).hexdigest(),
                    "database_path": str(vendor_db),
                    "database_schema": 5,
                    "ghidra_program": "Broadcom_vendor_wlc_hybrid",
                    "known_wrapper_symbols": [],
                },
                "wlko": {
                    "artifact_id": "wlko",
                    "artifact_kind": "linked_kernel_module",
                    "path": str(wlko_blob),
                    "sha256": hashlib.sha256(wlko_blob.read_bytes()).hexdigest(),
                    "database_path": str(wlko_db),
                    "database_schema": 5,
                    "ghidra_program": "Broadcom_linked_wlko",
                    "known_wrapper_symbols": ["wl_attach"],
                },
            },
        }
        manifest = root / "manifest.json"
        manifest.write_text(json.dumps(return_data), encoding="utf-8")
        return manifest

    def test_resolve_default_vendor_and_preserve_namespace(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            vendor_db = root / "vendor.db"
            wlko_db = root / "wlko.db"
            write_db(vendor_db, [(1, "wlc_phy_init", 0x10, 4, 1, "LOCAL", "symbol", "exact")])
            write_db(wlko_db, [(1, "wlc_phy_init", 0x20, 4, 1, "LOCAL", "symbol", "exact")])
            manifest = self.make_manifest(root, vendor_db, wlko_db, root / "vendor", root / "wlko")
            result = self.run_tool(manifest, "resolve")
            self.assertEqual(result.returncode, 0, result.stderr)
            value = json.loads(result.stdout)
            self.assertEqual(value["image"], "vendor")
            self.assertEqual(value["artifact_id"], "vendor")

    def test_unknown_image_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            vendor_db = root / "vendor.db"
            wlko_db = root / "wlko.db"
            write_db(vendor_db, [])
            write_db(wlko_db, [])
            manifest = self.make_manifest(root, vendor_db, wlko_db, root / "vendor", root / "wlko")
            result = self.run_tool(manifest, "resolve", "--image", "missing")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unknown image", result.stderr)

    def test_wlko_missing_artifact_does_not_fallback_to_vendor(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            vendor_db = root / "vendor.db"
            wlko_db = root / "wlko.db"
            write_db(vendor_db, [])
            write_db(wlko_db, [])
            manifest = self.make_manifest(root, vendor_db, wlko_db, root / "vendor", root / "wlko")
            data = json.loads(manifest.read_text(encoding="utf-8"))
            data["images"]["wlko"]["path"] = str(root / "missing-wlko")
            manifest.write_text(json.dumps(data), encoding="utf-8")
            result = self.run_tool(manifest, "resolve", "--image", "wlko")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("artifact does not exist", result.stderr)

    def test_exact_symbol_correlation_namespaces_addresses(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            vendor_db = root / "vendor.db"
            wlko_db = root / "wlko.db"
            write_db(vendor_db, [(1, "wlc_phy_init", 0x10, 4, 1, "LOCAL", "symbol", "exact")])
            write_db(
                wlko_db,
                [
                    (1, "wlc_phy_init", 0x20, 4, 1, "LOCAL", "symbol", "exact"),
                    (2, "wl_attach", 0x30, 4, 1, "GLOBAL", "symbol", "exact"),
                ],
                [(1, 0x31, 2, 0x30, 1, 0x20, "wlc_phy_init", "direct", "internal", 1, "exact")],
            )
            manifest = self.make_manifest(root, vendor_db, wlko_db, root / "vendor", root / "wlko")
            result = self.run_tool(manifest, "correlate", "wlc_phy_init")
            self.assertEqual(result.returncode, 0, result.stderr)
            value = json.loads(result.stdout)
            self.assertEqual(value["match_method"], "exact_symbol_identity")
            self.assertEqual(value["vendor"]["namespace"], "vendor")
            self.assertEqual(value["wlko"]["namespace"], "wlko")
            self.assertEqual(value["vendor"]["address"], "0x10")
            self.assertEqual(value["wlko"]["address"], "0x20")

    @unittest.skipUnless(
        REAL_RE.is_file() and REAL_VENDOR_DB.is_file() and REAL_WLKO_DB.is_file(),
        "retained vendor/wlko RE databases are not available",
    )
    def test_real_vendor_default_and_wlko_selection(self):
        vendor = subprocess.run(
            [str(WRAPPER), "fn", "wlc_phy_init"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        selected = subprocess.run(
            [str(WRAPPER), "--image", "vendor", "fn", "wlc_phy_init"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        wlko = subprocess.run(
            [str(WRAPPER), "--image", "wlko", "fn", "wlc_phy_init"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(vendor.returncode, 0, vendor.stderr)
        self.assertEqual(selected.returncode, 0, selected.stderr)
        self.assertEqual(wlko.returncode, 0, wlko.stderr)
        self.assertEqual(vendor.stdout, selected.stdout)
        self.assertIn("wlc_phy_init (0xbabf5)", wlko.stdout)

    @unittest.skipUnless(
        REAL_RE.is_file() and REAL_VENDOR_DB.is_file() and REAL_WLKO_DB.is_file(),
        "retained vendor/wlko RE databases are not available",
    )
    def test_real_cross_image_correlation(self):
        result = subprocess.run(
            [str(WRAPPER), "correlate", "wlc_phy_init"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        value = json.loads(result.stdout)
        self.assertEqual(value["match_method"], "exact_symbol_identity")
        self.assertEqual(value["vendor"]["namespace"], "vendor")
        self.assertEqual(value["wlko"]["namespace"], "wlko")

    @unittest.skipUnless(
        REAL_RE.is_file() and REAL_VENDOR_DB.is_file() and REAL_WLKO_DB.is_file(),
        "retained vendor/wlko RE databases are not available",
    )
    def test_real_wlko_has_image_aware_validation(self):
        result = subprocess.run(
            [str(WRAPPER), "--image", "wlko", "verify"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        value = json.loads(result.stdout)
        self.assertEqual(value["status"], "PASS")
        self.assertEqual(value["image"], "wlko")


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""End-to-end identity-admission tests for explicit historical RE manifests."""

import hashlib
import json
import os
import pathlib
import shutil
import sqlite3
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
BOOTSTRAP = ROOT / "scripts" / "re-bootstrap.sh"
PINNED_SHA = "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"
TOOLING_ROOT = pathlib.Path("/media/kartashoff/Storage/opensource/iced/test")
TOOLING_COMMIT = "f19c428847bd1d490d9d68a4a0bf1cdff18c3fd5"
CANONICAL_DB = TOOLING_ROOT / "re.db"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_db(path, sha):
    with sqlite3.connect(path) as conn:
        conn.execute("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT)")
        conn.executemany(
            "INSERT INTO meta VALUES (?,?)",
            [("schema_version", "5"), ("sha256", sha)],
        )
        conn.execute("CREATE TABLE sections(name TEXT)")
        conn.execute("CREATE TABLE functions(name TEXT)")
        conn.execute("CREATE TABLE calls(name TEXT)")
        conn.execute("CREATE TABLE externals(name TEXT)")


class ReBootstrapIdentityTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.re = self.root / "fake-re"
        self.re.write_text(
            "#!/bin/sh\n"
            "if [ $# -eq 0 ]; then echo 're packet'; exit 0; fi\n"
            "if [ \"$1\" = stats ]; then\n"
            "  echo 'functions: 5000'\n"
            "  echo 'reloc-site mismatches: 0'\n"
            "  exit 0\n"
            "fi\n"
            "exit 2\n",
            encoding="utf-8",
        )
        self.re.chmod(0o755)
        self.vendor = self.root / "vendor.bin"
        self.wlko = self.root / "wlko.bin"
        self.vendor.write_bytes(b"accepted-vendor")
        self.wlko.write_bytes(b"accepted-wlko")
        self.vendor_sha = digest(self.vendor)
        self.wlko_sha = digest(self.wlko)
        self.vendor_db = self.root / "vendor.db"
        self.wlko_db = self.root / "wlko.db"
        make_db(self.vendor_db, self.vendor_sha)
        make_db(self.wlko_db, self.wlko_sha)
        self.manifest = self.root / "manifest.json"
        self.write_manifest()

    def tearDown(self):
        self.tmp.cleanup()

    def write_manifest(self, *, vendor_sha=None, vendor_path=None,
                       canonical=True, omit_vendor_sha=False):
        vendor = {
            "artifact_id": "vendor",
            "artifact_kind": "proprietary_vendor_object",
            "path": str(vendor_path or self.vendor),
            "database_path": str(self.vendor_db),
            "database_schema": 5,
            "canonical": canonical,
        }
        if not omit_vendor_sha:
            vendor["sha256"] = vendor_sha or self.vendor_sha
        data = {
            "schema": "openbrcm-re-image-manifest/v1",
            "default_image": "vendor",
            "images": {
                "vendor": vendor,
                "wlko": {
                    "artifact_id": "wlko",
                    "artifact_kind": "historical_linked_kernel_module",
                    "path": str(self.wlko),
                    "sha256": self.wlko_sha,
                    "database_path": str(self.wlko_db),
                    "database_schema": 5,
                    "canonical": True,
                },
            },
        }
        self.manifest.write_text(json.dumps(data), encoding="utf-8")

    def run_bootstrap(self):
        env = os.environ.copy()
        env.update({
            "RE_IMAGE_MANIFEST": str(self.manifest),
            "RE_BIN": str(self.re),
            "TOOLING_ROOT": str(TOOLING_ROOT),
            "EXPECTED_TOOLING_COMMIT": TOOLING_COMMIT,
        })
        env.pop("RE_DB", None)
        env.pop("RE_BLOB", None)
        env.pop("EXPECTED_SHA", None)
        return subprocess.run(
            ["sh", str(BOOTSTRAP)], cwd=ROOT, env=env,
            capture_output=True, text=True,
        )

    def test_canonical_manifest_record_with_matching_image_passes(self):
        result = self.run_bootstrap()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"input_mode": "explicit manifest selecting a canonical record"', result.stdout)
        self.assertIn('"identity_expectation_source":', result.stdout)

    def test_canonical_record_with_wrong_image_fails(self):
        self.vendor.write_bytes(b"wrong-canonical-image")
        result = self.run_bootstrap()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("selected image sha256 does not match manifest identity", result.stdout)

    def test_explicit_stable_alternate_image_passes(self):
        self.write_manifest(canonical=False)
        before = digest(self.vendor_db)
        result = self.run_bootstrap()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('"input_mode": "explicit stable manifest input"', result.stdout)
        self.assertEqual(digest(self.vendor_db), before)

    def test_explicit_manifest_wrong_sha_fails(self):
        self.write_manifest(vendor_sha="0" * 64, canonical=False)
        result = self.run_bootstrap()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("selected image sha256 does not match manifest identity", result.stdout)

    def test_missing_manifest_image_fails_without_fallback(self):
        self.write_manifest(vendor_path=self.root / "missing-vendor", canonical=False)
        result = self.run_bootstrap()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("selected image does not exist", result.stdout)
        self.assertIn("no fallback", result.stdout)

    def test_missing_expected_identity_fails(self):
        self.write_manifest(omit_vendor_sha=True, canonical=False)
        result = self.run_bootstrap()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected sha256 is missing or invalid", result.stdout)

    def test_explicit_selection_does_not_rewrite_any_index(self):
        vendor_before = digest(self.vendor_db)
        wlko_before = digest(self.wlko_db)
        canonical_before = digest(CANONICAL_DB) if CANONICAL_DB.is_file() else None
        result = self.run_bootstrap()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(digest(self.vendor_db), vendor_before)
        self.assertEqual(digest(self.wlko_db), wlko_before)
        if canonical_before is not None:
            self.assertEqual(digest(CANONICAL_DB), canonical_before)

    def admit_default_vendor(self, env_manifest=None):
        """Run admission against the repository's own image manifest."""
        env = {k: v for k, v in os.environ.items() if k != "RE_IMAGE_MANIFEST"}
        if env_manifest is not None:
            env["RE_IMAGE_MANIFEST"] = str(env_manifest)
        return subprocess.run(
            ["python3", str(ROOT / "scripts/re-image.py"), "admit", "--image", "vendor"],
            cwd=ROOT, env=env, capture_output=True, text=True,
        )

    def test_canonical_default_is_admitted_only_on_identity_match(self):
        # The shipped manifest is the default input, so this exercises the
        # real vendor artifact on disk. Admission may pass only because the
        # observed digest equals the pinned identity; the pin itself is the
        # project's expected vendor hash and must stay untouched here.
        result = self.admit_default_vendor()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        payload = json.loads(result.stdout)
        self.assertIn('"input_mode": "canonical default"', result.stdout)
        self.assertEqual(payload["status"], "PASS")
        self.assertEqual(payload["expected_sha256"], PINNED_SHA)
        self.assertEqual(payload["observed_sha256"], PINNED_SHA)
        self.assertEqual(payload["observed_sha256"], payload["expected_sha256"])

    def test_canonical_default_mismatch_is_not_silently_accepted(self):
        # Same real artifact, same canonical record: only the expected digest
        # differs. A mismatch must fail rather than fall back to another copy.
        # The salted manifest is a temporary copy; the shipped pin is unchanged.
        with tempfile.TemporaryDirectory() as tmp:
            original = json.loads((ROOT / "docs/re-image-manifest.json").read_text())
            original["images"]["vendor"]["sha256"] = "0" * 64
            salted = pathlib.Path(tmp) / "manifest-salted.json"
            salted.write_text(json.dumps(original), encoding="utf-8")
            result = self.admit_default_vendor(env_manifest=salted)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('"input_mode": "explicit manifest selecting a canonical record"',
                      result.stdout)
        self.assertIn("selected image sha256 does not match manifest identity", result.stdout)


if __name__ == "__main__":
    unittest.main()

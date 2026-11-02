#!/usr/bin/env python3
"""Verify the byte-identical, public-metadata contract retention set."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs/research/contracts/20261011/preservation-manifest.json"


class ResearchContractRetentionTests(unittest.TestCase):
    def test_all_contract_copy_hashes_and_absent_prior_txpower_copy_are_explicit(self):
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(manifest["schema"], "openbrcm-research-contract-retention/v1")
        contracts = manifest["contracts"]
        self.assertEqual(len(contracts), 14)
        self.assertEqual(len({item["contract_id"] for item in contracts}), 14)

        for item in contracts:
            retained = ROOT / item["retained_copy_path"]
            self.assertTrue(retained.is_file(), item["retained_copy_path"])
            retained_sha = hashlib.sha256(retained.read_bytes()).hexdigest()
            self.assertEqual(retained_sha, item["original_sha256"], item["contract_id"])
            self.assertEqual(retained_sha, item["retained_copy_sha256"], item["contract_id"])
            self.assertTrue(item["source_bytes_present_at_admission"])
            screen = item["content_screen"]
            self.assertFalse(screen["raw_firmware_bytes"])
            self.assertFalse(screen["raw_capture_bytes"])
            self.assertFalse(screen["literal_calibration_payload_arrays"])
            self.assertFalse(screen["private_network_or_credential_data"])

        txpower = next(item for item in contracts if item["contract_id"].startswith("bcm4352-txpwr"))
        self.assertIsNone(txpower["previous_retained_copy_path"])
        self.assertEqual(txpower["retention_origin"], "copied_from_canonical_source_this_task")


if __name__ == "__main__":
    unittest.main()

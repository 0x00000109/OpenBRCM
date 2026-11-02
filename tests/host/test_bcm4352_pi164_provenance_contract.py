#!/usr/bin/env python3
"""Keep instruction provenance separate from hardware and synthetic values."""
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "docs/research/contracts/20261011/bcm4352-pi164-instruction-provenance-v1.json"
TXPWR_CORRECTION = ROOT / "docs/research/contracts/20261011/bcm4352-sub-9868f-arithmetic-correction-v1.json"


class Pi164ProvenanceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT.read_text(encoding="utf-8"))

    def test_producer_width_extraction_and_all_direct_stores(self):
        field = self.contract["field"]
        producer = self.contract["producer"]
        self.assertEqual(field["storage_width_bits"], 32)
        self.assertEqual(field["base_source"],
                         "16-bit PHYVERSION read from PHY register 0x03e0")
        self.assertEqual(field["base_transform"],
                         "zero_extend(raw16 & 0x000f)")
        self.assertEqual([x["assignment_pc"] for x in producer["conditional_overrides"][:1]],
                         ["0x0be61f"])
        self.assertEqual([x["pc"] for x in producer["base_assignments"]],
                         ["0x0be5ef", "0x0be5f2"])
        self.assertIn("0x0be632", producer["selected_path_writer_scope"])

    def test_bcm4352_formula_is_proven_but_numeric_value_is_not(self):
        selected = self.contract["bcm4352_rev03_selected_formula"]
        self.assertFalse(selected["override_0xbe61f_applies"])
        self.assertFalse(selected["override_0xbe632_applies"])
        self.assertEqual(selected["runtime_numeric_value"], "UNKNOWN")

    def test_hardware_observation_and_synthetic_fixture_are_separate(self):
        values = self.contract["value_classification"]
        self.assertEqual(values["0x0a"]["class"], "SYNTHETIC_TEST_INPUT")
        self.assertEqual(values["0x01"]["class"],
                         "HARDWARE_OBSERVED_FOR_SEPARATE_OPENBRCM_RUN")
        self.assertEqual(values["0x01"]["historical_vendor_callback_value"],
                         "UNKNOWN")
        self.assertEqual(values["historical_vendor_callback_value"], "UNKNOWN")

    def test_revision_sensitive_consumers_and_nonconsumers_are_explicit(self):
        consumers = self.contract["consumers"]
        sites = {x.get("function"): x for x in consumers}
        self.assertEqual(sites["sub_a7089"]["sites"],
                         ["0x0a719b", "0x0aa22a", "0x0aa54f", "0x0aa729"])
        self.assertEqual(sites["sub_8f41b"]["sites"],
                         ["0x08f438", "0x08f440", "0x08f49f", "0x08f516"])
        self.assertEqual(sites["sub_8f41b"]["caller"],
                         "sub_9a539 calls it at 0x09a7de after computing RSI from its local loop state and zeroing RDX/RCX")
        self.assertIn("no direct PI+0x164 consumer",
                      sites["sub_96203"]["operation"])
        self.assertIn("no direct PI+0x164 read in this body",
                      sites["sub_9a539"]["operation"])

    def test_txpower_arithmetic_correction_pins_unchanged_predecessor(self):
        correction = json.loads(TXPWR_CORRECTION.read_text(encoding="utf-8"))
        self.assertEqual(correction["supersedes_contract_sha256"],
                         "d6c4601ee7ac208950d1928eee01088bc4b2111b164ea12eb5e941d48a0888ab")
        self.assertEqual(correction["corrected_packing"]["scratch_plus_0"],
                         "u16(d1 + (d2 << 8)); d3 << 16 is discarded by the AX store")


if __name__ == "__main__":
    unittest.main()

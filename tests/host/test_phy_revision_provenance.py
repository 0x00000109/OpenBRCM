#!/usr/bin/env python3
"""Keep PHY, D11, and synthetic revision provenance distinct."""
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
IDENTITY_MAP = json.loads(
    (ROOT / "docs/m34d4/radio_identity_map.json").read_text(encoding="utf-8")
)


class PhyRevisionProvenanceTests(unittest.TestCase):
    def test_phyrev_map_does_not_equate_phy_revision_with_d11_revision(self):
        phyrev = next(field for field in IDENTITY_MAP["fields"]
                      if field["name"] == "phyrev")
        self.assertEqual(phyrev["object_field"], "pi+0x164 (u32)")
        self.assertEqual(phyrev["raw_source"], "PHY reg 0x3e0, bits[3:0]")
        self.assertEqual(phyrev["board_value_status"], "UNKNOWN")
        self.assertEqual(phyrev["legacy_claim_status"], "CONFLICTING")
        self.assertIn("0x0A", phyrev["legacy_board_value_claim"])
        self.assertIn("0xcb01", phyrev["provenance_warning"])
        self.assertIn("D11 core revision", phyrev["provenance_warning"])
        self.assertIn("selected attach-flow field source",
                      phyrev["confidence_scope"])
        self.assertEqual(phyrev["historical_assignment_sites"],
                         ["0xbe5f2", "0xbe61f", "0xbe632"])
        self.assertIn("low nibble of the runtime 16-bit PHYVERSION read",
                      phyrev["bcm4352_rev03_selected_formula"])

    def test_decoder_summary_carries_the_same_value_warning(self):
        phyrev = IDENTITY_MAP["decoder_relations"]["phyrev"]
        self.assertEqual(phyrev["board_value_status"], "UNKNOWN")
        self.assertIn("0xcb01", phyrev["provenance_warning"])

    def test_current_attach_extracts_phy_type_and_revision_from_different_bits(self):
        lifecycle = (ROOT / "src/ob_lifecycle.c").read_text(encoding="utf-8")
        attach = lifecycle[lifecycle.index("int ob_acphy_attach_identity") :]
        self.assertIn("phyver = ob_d11_read16(hw, 0x03e0u)", attach)
        self.assertIn("(phyver >> 8) & 0x0fu", attach)
        self.assertIn("hw->phy_revision = (u8)(phyver & 0x0fu)", attach)

    def test_production_identity_gate_remains_phy_revision_one_and_d11_42(self):
        phy = (ROOT / "src/ob_phy.c").read_text(encoding="utf-8")
        self.assertIn("hw->core->id.rev != 42", phy)
        self.assertIn("hw->phy_type != 0x0b || hw->phy_revision != 1", phy)

    def test_zero_a_cleanroom_inputs_are_explicitly_synthetic(self):
        runner = (ROOT / "src/ob_phy_9a539.c").read_text(encoding="utf-8")
        helper = (ROOT / "src/ob_phy_96203.c").read_text(encoding="utf-8")
        self.assertIn("in->phy_revision != 0x0a", runner)
        self.assertIn("!in->explicitly_synthetic", runner)
        self.assertIn("in->phy_revision != 0x0a", helper)
        self.assertIn("!in->explicitly_synthetic", helper)


if __name__ == "__main__":
    unittest.main()

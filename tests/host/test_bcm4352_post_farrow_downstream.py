#!/usr/bin/env python3
"""Independent structural checks for the post-Farrow caller and PHY prefix."""

import hashlib
import json
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from scripts.bcm4352_contract_ir import validate_typed_ir

SOFTWARE = ROOT / "artifacts/contracts/bcm4352-post-farrow-crs-software-state-v1.json"
BLOCK = ROOT / "artifacts/contracts/bcm4352-first-downstream-phy-block-v1.json"
CORRECTION = ROOT / "artifacts/contracts/bcm4352-sub-99658-calltarget-correction-v1.json"
OLD_SUFFIX = ROOT / "artifacts/contracts/bcm4352-post-crs-selected-suffix.json"


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


class PostFarrowSoftwareContract(unittest.TestCase):
    def test_cache_predicate_and_unconditional_join_are_structured(self):
        contract = load(SOFTWARE)
        gate = contract["caller_cache_refresh"]
        self.assertEqual(gate["predicate_ast"]["op"], "OR")
        self.assertEqual(gate["predicate_ast"]["args"][0]["op"], "BITMASK_EQ")
        self.assertEqual(gate["predicate_ast"]["args"][0]["value"]["symbol"],
                         "PI_PLUS_0x19c")
        self.assertEqual(gate["predicate_ast"]["args"][1]["left"]["symbol"],
                         "INITIAL_CHANSPEC_LOW8")
        self.assertEqual(gate["both_arms_rejoin"], "sub_a7089 @0xaa4f2")
        self.assertTrue(contract["unconditional_software_call"]["predicate"])
        self.assertEqual(contract["unconditional_software_call"]["site"], "0xaa4f2")

    def test_external_8b0_axis_has_two_arms_and_no_hardware_events(self):
        contract = load(SOFTWARE)
        axis = contract["unconditional_software_call"]["configuration_axis"]
        arms = contract["sub_909fd"]["arms"]
        self.assertEqual(axis["class"], "EXTERNAL_CONFIGURATION")
        self.assertEqual(axis["value"], "UNKNOWN")
        self.assertEqual(axis["width_bits"], 32)
        self.assertEqual([arm["id"] for arm in arms], ["ZERO", "NONZERO"])
        self.assertTrue(all(arm["hardware_events"] == [] for arm in arms))
        self.assertEqual({arm["rejoin"] for arm in arms}, {"0x90b2e"})
        self.assertEqual(arms[0]["state_transition"]["op"], "COPY_BYTES")
        self.assertEqual(arms[1]["state_transition"]["op"], "IF")
        self.assertEqual(arms[1]["state_transition"]["else"]["operator"], "UNSIGNED_MAX")
        self.assertIn("last_byte_operator", arms[1]["state_transition"]["else"])
        self.assertIn("ZERO and NONZERO", contract["sub_909fd"]["common_tail"]["applies_after"])
        self.assertEqual(contract["sub_909fd"]["return"], "0x90b76")

    def test_cache_record_is_an_object_and_channel_is_not_selected(self):
        contract = load(SOFTWARE)
        source = contract["sub_909fd"]["source_selection"]
        self.assertIn("typed cache record", source["nonnull_source"])
        self.assertIn("Never use an integer", source["pointer_policy"])
        self.assertEqual(contract["channel_inputs"]["initial_chanspec"],
                         "UNKNOWN; supplied to historical wlc_phy_init from invocation state")
        self.assertEqual(contract["channel_inputs"]["channel_row_hit"],
                         "UNKNOWN; controlled chanspec 0x1001 / key 1 / row 63 is not target evidence")

    def test_immutable_config_axis_reference_hashes(self):
        contract = load(SOFTWARE)
        ref = next(ref for ref in contract["immutable_references"]
                   if ref["path"].endswith("config-axis-8b0.json"))
        actual = hashlib.sha256((ROOT / ref["path"]).read_bytes()).hexdigest()
        self.assertEqual(actual, ref["sha256"])

    def test_existing_typed_ir_validator_accepts_both_new_scopes(self):
        for path in (SOFTWARE, BLOCK):
            with self.subTest(path=path.name):
                self.assertEqual(validate_typed_ir(load(path)["typed_ir"]), [])


class DownstreamBlockOracle(unittest.TestCase):
    # Independent hand-authored oracle from the admitted call sites.
    EXPECTED = [
        ("0x9b0cd", "READ", "PHY", "0x019e", 16, None, None),
        ("0x9b0eb", "RMW", "PHY", "0x019e", 16, "0x0002", "0x0002"),
        ("0x9b0f8", "READ", "PHY", "0x016c", 16, None, None),
        ("0x9b116", "RMW", "PHY", "0x016c", 16, "0x0040", "0x0040"),
    ]

    def test_exact_four_event_prefix_and_callback_expansion(self):
        contract = load(BLOCK)
        actual = [
            (op["source_site"], op["operation_type"], op["domain"],
             op["register"], op["width_bits"], op.get("mask"), op.get("value"))
            for op in contract["operations"]
        ]
        self.assertEqual(actual, self.EXPECTED)
        self.assertEqual(contract["expected_accounting"]["logical_events"], 4)
        self.assertEqual(contract["expected_accounting"]["callback_records"], 6)
        self.assertEqual(contract["scope"]["terminal_boundary"],
                         "before selected call at sub_9af05 @0x9b123 to distinct sub_99658 @0x99658")
        self.assertEqual(contract["scope"]["completion_status"], "PARTIAL_WITH_EXACT_GAP")
        self.assertFalse(contract["hardware_ready"])
        boundary = contract["unresolved_boundary"]
        self.assertEqual(boundary["first_unimplemented_callsite"],
                         "sub_9af05 @0x9b123 -> sub_99658 @0x99658")
        self.assertEqual(boundary["first_unimplemented_operation"],
                         "sub_99658 payload-source selection @0x9968f")
        self.assertEqual(boundary["first_unimplemented_hardware_operation"],
                         "TABLE write @0x99875; subsequent selected TABLE call @0x998aa")

    def test_rmw_formula_and_argument_order_are_explicit(self):
        contract = load(BLOCK)
        for op in (contract["operations"][1], contract["operations"][3]):
            self.assertEqual(op["semantic_mode"], "MASKED_MERGE")
            self.assertEqual(op["argument_order"], ["mask", "value"])
            self.assertEqual(op["formula"], "(old & ~mask) | (value & mask)")
            self.assertEqual(op["callback_expansion"], ["PHY_READ16", "PHY_WRITE16"])

    def test_correction_is_additive_and_pins_immutable_predecessor(self):
        correction = load(CORRECTION)
        self.assertFalse(correction["immutable_predecessor_modified"])
        actual = hashlib.sha256(OLD_SUFFIX.read_bytes()).hexdigest()
        self.assertEqual(actual, correction["immutable_predecessor"]["sha256"])
        self.assertEqual(correction["corrected_chronology"][4]["site"], "0x9b123")
        self.assertIn("sub_99658(PI, 1)", correction["corrected_chronology"][4]["operation"])
        self.assertEqual(correction["facts"][2]["status"], "PROVEN")
        self.assertEqual(correction["facts"][3]["status"], "UNKNOWN")

    def test_no_production_dispatch_to_test_only_runner(self):
        source = (ROOT / "src/ob_phy_post_farrow_continuation.c").read_text(encoding="utf-8")
        self.assertIn("ob_phy_desense_9af05_prefix_test_only", source)
        self.assertNotIn("ob_phy_desense_9af05_prefix_test_only(",
                         (ROOT / "src/ob_phy.c").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main(verbosity=2)

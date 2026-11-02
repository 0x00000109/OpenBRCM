import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONTRACTS = ROOT / "artifacts/contracts"
PACKET = ROOT / "docs/research/packets/bcm4352-postcrs-config-axis-channel-commit-20261009.md"

THROUGH_CRS_SHA256 = {
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-skip-txchain-0.json": "1cce5a4cf5366e1e2bc5b7a1db64a0b50dfd7a4cad91b046fb42c7597ca218f2",
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-skip-txchain-1.json": "611610f0a5c276174532b99f9399bbf89199c459ff57ac923606d7e513c63f38",
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-skip-txchain-2.json": "1a5d6acf599e8ae59ac0ead84d3397d5462989ef745df1a7d7693cab0ccaa4e5",
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-skip-txchain-3.json": "2cf9a9d6133689ab85fbd60ebb9e14a7eed185b75ae3129d1b5b49273f77f7c4",
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-write-txchain-0.json": "18e0c4d4eaef2aad5eebb83638f0f693a21cf778cefaa012fa09ce4073bf52e4",
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-write-txchain-1.json": "202a970cc131ad5691219d7d8a8edc315b11fbf02a4b33e861fb7b2c09fd52d7",
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-write-txchain-2.json": "d894f3c427e91a13a39523b8c0c216c2ccef97d0b437f7e35ff4f94806909221",
    "bcm4352-through-crs-param-extpagain-eq2-pdgain5g-write-txchain-3.json": "ee693c36b2f4df44ae6bb2955521db236eeb418a2b36a3b5e63d3ae4cdbcb15b",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-skip-txchain-0.json": "3375a22740f15be74bb465404d9e226d904b5de79c03e0d13454a609437ceab9",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-skip-txchain-1.json": "9ae401b73f01c1ccf695df3dccbb143240740a86c78da38451dfaae9ce4803e3",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-skip-txchain-2.json": "1f2bc93d4993aa4a7b2d8427228ad27f037382f056a8495d4762178c0ac12de2",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-skip-txchain-3.json": "cec16f9a4e611be60cf251e7eec1d21ad13015d30b93c30c2d6833a6f7a44e71",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-write-txchain-0.json": "ba04a607f9dc622fc018b96c95d95e025fdb8f672884823153ee878f945af6b7",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-write-txchain-1.json": "6d32059eab38fcd78af5d8fd9c9a11bd79516f38439c246c3aa674662b595f05",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-write-txchain-2.json": "c1c0ef0e5e2a62f9d63ce17d4be03bb76049bee847c77bd3853b4a882ffb7043",
    "bcm4352-through-crs-param-extpagain-ne2-pdgain5g-write-txchain-3.json": "7333c98e7dde1ecc78736e0e1c3a8a33e4f28fcd329e49a6d0fc1930f64201e2",
}


def load_contract(name):
    return json.loads((CONTRACTS / name).read_text())


class PostCrsAciRuntimeContinuationTests(unittest.TestCase):
    def test_crs_return_and_external_configuration_axis(self):
        contract = load_contract("bcm4352-post-crs-runtime-parameterized.json")
        self.assertEqual(contract["crs_return"]["wrapper_return"], "0xa5fad")
        self.assertEqual(contract["crs_return"]["caller"], "sub_a7089")
        self.assertEqual(contract["crs_return"]["continuation_site"], "0xaa496")
        self.assertTrue(contract["crs_to_postcrs_join_proven"])
        boundary = contract["first_external_configuration_selector"]
        self.assertEqual(boundary["site"], "sub_909fd @0x90a27")
        self.assertEqual(boundary["field"], "nested+0x8b0")
        self.assertEqual(boundary["class"], "EXTERNAL_CONFIGURATION")
        self.assertEqual(boundary["selected_value"], "UNKNOWN because setter may run before selected sub_909fd call")

        axis = load_contract("bcm4352-post-crs-config-axis-8b0.json")
        self.assertTrue(axis["writer_set_complete"])
        self.assertEqual(axis["selected_value"], "UNKNOWN")
        self.assertEqual(axis["class"], "EXTERNAL_CONFIGURATION")
        self.assertEqual(axis["default_value"]["value"], 0)
        self.assertTrue(any(w["function"] == "wlc_phy_attach_acphy" for w in axis["writers"]))
        self.assertTrue(any(w["function"] == "wlc_phy_desense_btcoex_acphy" for w in axis["writers"]))
        self.assertTrue(axis["zero_arm"]["complete"])
        self.assertTrue(axis["nonzero_arm"]["complete"])
        self.assertEqual(axis["zero_arm"]["rejoin"], "0x90b2e")
        self.assertEqual(axis["nonzero_arm"]["rejoin"], "0x90b2e")
        self.assertEqual(axis["zero_arm"]["hardware_events_before_rejoin"], [])
        self.assertEqual(axis["nonzero_arm"]["hardware_events_before_rejoin"], [])

    def test_selected_post_crs_fragments_and_remaining_desense_gap(self):
        contract = load_contract("bcm4352-post-crs-runtime-parameterized.json")
        joins = contract["post_crs_fragment_joins"]
        self.assertTrue(joins["desense_suffix_joined"])
        self.assertTrue(joins["desense_both_config_arms_joined"])
        self.assertFalse(joins["desense_selected_downstream_consumers_complete"])
        self.assertTrue(joins["rfpll_join_validated"])
        self.assertTrue(joins["txpwr_join_validated"])
        self.assertTrue(joins["phy0070_branch_contract_present"])
        self.assertTrue(joins["phy0070_eq_arm_complete"])
        self.assertTrue(joins["phy0070_ne_arm_complete"])
        self.assertTrue(joins["phy0070_branch_rejoin_proven"])
        self.assertTrue(joins["dummy_tx_join_validated"])
        self.assertFalse(contract["post_crs_vector_through_dummy_complete"])
        self.assertIn("nested+0x492", contract["first_uncomposed_post_crs_operation"])
        self.assertIsNone(contract["first_unexpanded_selected_helper"])

    def test_post_dummy_tree_runtime_slices_and_exclusions(self):
        contract = load_contract("bcm4352-post-dummy-aci-selected.json")
        self.assertTrue(contract["tree_complete"])
        self.assertIsNone(contract["event_count"])
        self.assertTrue(contract["runtime_input_set_complete"])
        self.assertTrue(contract["finite_runtime_arms_complete"])
        self.assertIsNone(contract["first_unexpanded_helper"])
        self.assertEqual(contract["recursive_tree"]["return"], "0x9a88a")
        self.assertTrue(contract["recursive_tree"]["selected_direct_descendants_complete"])
        self.assertTrue(any("0x90506" in str(r["site"]) for r in contract["runtime_reads"]))
        discarded = contract["discarded_reads"]
        self.assertTrue(any(x["site"] == "0x9a304" for x in discarded))
        reads = json.dumps(contract["runtime_reads"])
        self.assertNotIn("0x0550", reads)
        self.assertNotIn("0x0551", reads)
        self.assertIn("0x06dc + 0x0200*core", reads)
        self.assertIn("0x06dd + 0x0200*core", reads)
        self.assertIn("0x63ae4", contract["first_non_enumerable_runtime_dependency"])

    def test_runtime_telemetry_is_complete_to_post_dummy_tree_boundary(self):
        batch = load_contract("bcm4352-runtime-telemetry-batch.json")
        self.assertTrue(batch["complete_to_boundary"])
        self.assertEqual(batch["boundary"], "sub_9a539 selected reset-tree return @0x9a88a or terminal suspend failure upcall @0x63ae4")
        self.assertEqual(len(batch["items"]), 16)
        excluded = json.dumps(batch["excluded"])
        self.assertIn("0x9a304", excluded)
        self.assertIn("0x0550", excluded)
        self.assertIn("0x0551", excluded)
        self.assertIn("nested+0x8b0", excluded)
        self.assertIn("TABLE14_OFFSET33_WORD2", excluded)

    def test_bmac_gate_callback_and_channel_row_commit_candidate(self):
        contract = load_contract("bcm4352-channel-commit-candidate.json")
        self.assertTrue(contract["branch_inputs"]["complete"])
        selected = contract["branch_inputs"]["callback_selected"]
        self.assertTrue(selected.startswith("UNKNOWN without the selected lifecycle byte"))
        self.assertIn("@0x67ca3 iff lifecycle is nonzero", selected)
        self.assertIn("requested != current PHY chanspec", selected)
        self.assertEqual(contract["callback"]["site"], "wlc_phy_chanspec_set @0x67ca3; indirect call inside wrapper @0xb5db6 via [pi+0x38]")
        self.assertEqual(contract["callback"]["target"], "sub_a7089")
        self.assertTrue(contract["callback"]["target_proven"])
        commit = contract["commit"]
        self.assertTrue(commit["found"])
        self.assertTrue(commit["conditional_on_callback_and_row_match"])
        self.assertEqual(commit["site"], "sub_a7089 @0xa753d")
        self.assertEqual(commit["domain"], "RADIO")
        self.assertEqual(commit["register"], "0x08e0 for BCM4352")
        self.assertEqual(commit["operation"], "WRITE via write_radio_reg")
        self.assertTrue(commit["semantics_proven"])
        self.assertFalse(contract["ready_boundary"]["predicate_found"])
        self.assertFalse(contract["ready_boundary"]["ready_semantics_proven"])

    def test_closed_through_crs_contracts_are_byte_unchanged(self):
        observed = {}
        for name in THROUGH_CRS_SHA256:
            observed[name] = hashlib.sha256((CONTRACTS / name).read_bytes()).hexdigest()
        self.assertEqual(observed, THROUGH_CRS_SHA256)

    def test_packet_records_current_boundary_and_conditional_commit(self):
        packet = PACKET.read_text()
        for line in (
            "FIELD_8B0_WRITER_SET_COMPLETE: YES",
            "FIELD_8B0_SELECTED_VALUE: UNKNOWN",
            "FIELD_8B0_CLASS: EXTERNAL_CONFIGURATION",
            "CONFIG_AXIS_8B0_PARAMETERIZED: YES",
            "CONFIG_AXIS_8B0_ZERO_ARM_COMPLETE: YES",
            "CONFIG_AXIS_8B0_NONZERO_ARM_COMPLETE: YES",
            "CONFIG_AXIS_8B0_REJOIN_PROVEN: YES",
            "DESENSE_SELECTED_CONSUMERS_COMPLETE: NO",
            "POST_CRS_VECTOR_THROUGH_DUMMY_COMPLETE: NO",
            "POST_DUMMY_RESET_TREE_COMPLETE: YES",
            "POST_DUMMY_RUNTIME_INPUT_SET_COMPLETE: YES",
            "POST_DUMMY_ALL_FINITE_ARMS_COMPLETE: YES",
            "BMAC_CHANSPEC_BRANCH_INPUTS_COMPLETE: YES",
            "WLC_PHY_CHANSPEC_SET_SELECTED: UNKNOWN",
            "CHANSPEC_SET_CALLBACK_TARGET: sub_a7089",
            "CHANSPEC_SET_CALLBACK_TARGET_PROVEN: YES",
            "CHANNEL_1_HW_COMMIT_FOUND: YES",
            "CHANNEL_1_HW_COMMIT_SITE: sub_a7089 @0xa753d",
            "CHANNEL_COMMIT_SEMANTICS_PROVEN: YES",
            "CHANNEL_READY_PREDICATE_FOUND: NO",
            "NEXT_HARDWARE_RUN_JUSTIFIED: NO",
            "HARDWARE_ACTIONS: NONE",
            "MODULE_DEVICE_STATE_TOUCHED: NO",
        ):
            self.assertIn(line, packet)


if __name__ == "__main__":
    unittest.main()

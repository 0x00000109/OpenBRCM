import json
from pathlib import Path
import unittest


ARTIFACT = Path(__file__).resolve().parents[2] / "artifacts/contracts/bcm4352-post-crs-selected-suffix.json"


class PostCrsSuffixProvenanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.doc = json.loads(ARTIFACT.read_text())
        cls.blocks = cls.doc["recovered_blocks"]

    def test_sub998cc_consumes_only_initialized_table_ranges(self):
        desense = self.blocks["desense_btcoex"]
        values = desense["nested_selected_values"]
        self.assertEqual(values["0x66c"]["value"], "0x00")
        self.assertEqual(values["0x66d"]["value"], "0x00")
        payloads = desense["sub_998cc_selected_payloads"]
        self.assertEqual((payloads["arg1"]["offset"], payloads["arg1"]["width_bits"],
                          payloads["arg1"]["count"]), ("0x08", 8, 6))
        self.assertEqual(payloads["arg1"]["payload_bytes"], [
            "0x0b", "0x0c", "0x0e", "0x20", "0x24", "0x7f",
        ])
        self.assertEqual((payloads["arg2"]["offset"], payloads["arg2"]["width_bits"],
                          payloads["arg2"]["count"]), ("0x10", 8, 7))
        self.assertEqual(payloads["arg2"]["payload_bytes"], [
            "0x00", "0x00", "0x00", "0x03", "0x03", "0x03", "0x7f",
        ])
        self.assertTrue(payloads["fully_deterministic"])
        self.assertFalse(payloads["uninitialized_stack_bytes_consumed"])
        # The enclosing desense helper remains incomplete for later table-0x64 work.
        self.assertFalse(desense["complete"])

    def test_txpower_indices_and_hardware_read_boundary_are_explicit(self):
        post = self.blocks["sub_a7089_post_rfpll"]
        self.assertEqual(post["txpwr_index_inputs"]["chain0"], "0x00")
        self.assertEqual(post["txpwr_index_inputs"]["chain1"], "0x00")
        self.assertEqual(len(post["txpwr_by_index_contract"]["selected_invocations"]), 2)
        txpwr = post["txpwr_by_index_contract"]
        self.assertTrue(txpwr["complete"])
        self.assertEqual(txpwr["two_invocation_totals"], {
            "phy_reads": 6, "phy_rmws": 12, "table_reads": 2, "table_writes": 10,
        })
        self.assertEqual([x["offset"] for x in txpwr["selected_invocations"][0]["table0c_writes"]],
                         ["0x0063", "0x0073"])
        self.assertEqual([x["offset"] for x in txpwr["selected_invocations"][1]["table0c_writes"]],
                         ["0x0067", "0x0077"])
        boundary = post["txpwrctrl_enable_runtime_boundary"]
        self.assertEqual((boundary["domain"], boundary["selector"], boundary["predicate"]),
                         ("PHY", "0x0070", "(sample & 0xe000) == 0xe000"))
        self.assertFalse(post["complete"])
        variants = boundary["variants"]
        self.assertEqual(variants["predicate"], "(sample & 0xe000) == 0xe000")
        self.assertEqual(variants["equal_arm"]["extra_runtime_samples"],
                         ["PHY 0x0640", "PHY 0x0840"])
        self.assertFalse(variants["hardware_equivalent"])
        self.assertTrue(variants["exact_sample_required"])

    def test_subagent_call_map_is_integrated_without_claiming_aci_completion(self):
        aci = self.blocks["post_dummy_aci_handoff"]
        self.assertEqual(aci["entry"],
                         "wlc_phy_desense_aci_reset_params_acphy(pi,1,1,1) @0xb7862")
        calls = aci["selected_direct_calls"]
        self.assertEqual([x["site"] for x in calls],
                         ["0xb7862", "0xb786e", "0xb7878", "0xb7897", "0xb78b1"])
        self.assertEqual([x["selected"] for x in calls], [True, True, True, False, False])
        self.assertIn("incomplete", aci["integration_status"])
        self.assertFalse(self.doc["fragment"]["complete"])

    def test_corrected_sub_9a539_branch_and_first_runtime_boundary(self):
        aci = self.blocks["post_dummy_aci_handoff"]["sub_9a539_selected_map"]
        calls = aci["selected_calls"]
        selected_998cc = [x for x in calls
                          if x.get("target", "").startswith("sub_998cc") and x["selected"]]
        self.assertEqual(selected_998cc, [])
        self.assertEqual(aci["selected_nested_668_670"], [
            "0x00", "0x00", "0x00", "0x00", "0x00", "0x00",
            "0x01", "0x00", "0x01",
        ])
        boundary = aci["next_deep_re_boundary"]
        self.assertEqual((boundary["site"], boundary["selector"]),
                         ("sub_9a121 @0x9a304", "0x06dc + core*0x0200"))
        self.assertFalse(aci["completion"])
        batch = self.blocks["post_dummy_aci_handoff"]["runtime_input_batch"]
        self.assertEqual(batch["complete_for_local_suffix"], "PARTIAL")
        sites = [item["site"] for item in batch["inputs"]]
        self.assertIn("direct reads @0x9a3b4 and @0x9a42c", sites)
        self.assertIn("sub_91b6e read @0x91cb9", sites)
        self.assertIn("discarded read @0x9a304", sites)


if __name__ == "__main__":
    unittest.main()

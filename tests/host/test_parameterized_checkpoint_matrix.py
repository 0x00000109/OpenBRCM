import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
CONTRACTS = ROOT / "artifacts/contracts"


class ParameterizedCheckpointMatrixTests(unittest.TestCase):
    def test_all_16_materialized_vectors_preserve_symbol_and_are_contiguous(self):
        files = sorted(CONTRACTS.glob("bcm4352-through-crs-param-*.json"))
        self.assertEqual(len(files), 16)
        variants = set()
        for path in files:
            data = json.loads(path.read_text())
            self.assertTrue(data["structurally_complete"])
            self.assertFalse(data["executable_exact_historical"])
            self.assertFalse(data["historically_exact"])
            self.assertFalse(data["hardware_ready"])
            self.assertIsInstance(data["events"], list)
            self.assertGreater(len(data["events"]), 4000)
            self.assertEqual(data["event_count"], len(data["events"]))
            self.assertEqual([e["seq"] for e in data["events"]], list(range(1, len(data["events"]) + 1)))
            self.assertTrue(data["event_vector_is_contiguous"])
            self.assertEqual(data["events"][0]["site"], "0xa04e9")
            self.assertEqual(data["historical_semantic_parameters"][0]["name"], "TABLE14_OFFSET33_WORD2")
            self.assertEqual(data["historical_semantic_parameters"][0]["historical_value"], "UNKNOWN")
            self.assertFalse(data["historical_semantic_parameters"][0]["candidate_selected"])
            s = data["selectors"]
            variants.add((s["extpagain2g"], s["pdgain5g_class"], s["txchain_low2"]))
            self.assertEqual(data["events"][0]["domain"], "PHY")
            self.assertEqual(data["events"][0]["register"], "0x0410")
            self.assertEqual(data["events"][0]["value"], "0x0077")
        self.assertEqual(len(variants), 16)

    def test_symbolic_word2_is_not_zero_filled(self):
        data = json.loads((CONTRACTS / "bcm4352-sub-9eaf9-table14-word2-parameterized.json").read_text())
        op = data["operations"][3]
        self.assertEqual(op["words_u16"], ["0xe800", "0x0084", "TABLE14_OFFSET33_WORD2"])
        meta = data["parameter_semantics"]
        self.assertEqual(meta["historical_value"], "UNKNOWN")
        self.assertEqual(meta["cleanroom_candidate"], "0x0000")
        self.assertFalse(meta["cleanroom_policy_selected"])

    def test_sub9eaf9_selected_vector_is_complete_and_preserves_symbolic_word(self):
        data = json.loads((CONTRACTS / "bcm4352-sub-9eaf9-parameterized-complete.json").read_text())
        self.assertTrue(data["complete"])
        self.assertTrue(data["structurally_complete"])
        self.assertFalse(data["historically_exact"])
        self.assertFalse(data["hardware_ready"])
        self.assertEqual(data["event_count"], 136)
        self.assertEqual([e["seq"] for e in data["events"]], list(range(1, 137)))
        self.assertEqual([x["name"] for x in data["historical_semantic_parameters"]],
                         ["TABLE14_OFFSET33_WORD2"])
        table14 = [e for e in data["events"]
                   if isinstance(e["register_or_table"], dict)
                   and e["register_or_table"].get("table_id") == 0x14]
        self.assertEqual(len(table14), 1)
        self.assertEqual(table14[0]["value"], ["0xe800", "0x0084", "TABLE14_OFFSET33_WORD2"])
        self.assertFalse(any(e["register_or_table"].get("offset") in {"0x030", "0x031", "0x032"}
                             for e in table14))

    def test_postcrs_runtime_artifact_corrects_hwaci_zero_argument_gate(self):
        data = json.loads((CONTRACTS / "bcm4352-post-crs-runtime-parameterized.json").read_text())
        self.assertFalse(data["complete"])
        self.assertTrue(data["telemetry_batch_complete_to_boundary"])
        self.assertEqual(data["first_external_configuration_selector"]["site"], "sub_909fd @0x90a27")
        self.assertEqual(data["first_external_configuration_selector"]["class"], "EXTERNAL_CONFIGURATION")
        claim = data["falsification_correction"]
        self.assertEqual(claim["status"], "DISPROVEN")
        self.assertIn("jumps directly to return", claim["counterexample"])
        call = next(x for x in data["selected_calls"] if x["site"] == "0xb786e")
        self.assertEqual(call["args"], "(pi,0,0)")
        self.assertNotIn("0x0550", {x["register"] for x in data["runtime_inputs"]})
        self.assertEqual(len(data["selected_zero_flag_hardware_effects"]), 2)
        self.assertNotIn("0x9a304", {x["site"] for x in data["runtime_inputs"]})
        self.assertEqual(data["discarded_runtime_reads"][0]["site"], "0x9a304")
        reads = {x["site"]: x["register"] for x in data["runtime_inputs"]}
        self.assertEqual(reads["0x90506"], "0x06dc + 0x0200*core")
        self.assertEqual(reads["0x9185b"], "0x06dd + 0x0200*core")

    def test_desense_9b3bb_vector_has_correct_table_id_offset_and_payloads(self):
        data = json.loads((CONTRACTS / "bcm4352-desense-9b3bb-selected-suffix.json").read_text())
        self.assertTrue(data["complete"])
        self.assertTrue(data["event_vector_is_contiguous"])
        self.assertEqual(data["event_count"], 24)
        self.assertEqual([e["seq"] for e in data["events"]], list(range(1, 25)))
        chain0 = [e for e in data["events"] if e["chain"] == 0]
        chain1 = [e for e in data["events"] if e["chain"] == 1]
        self.assertEqual(chain0[0]["site"], "0x9b3dd")
        self.assertEqual(chain0[0]["register_or_table"], {"table_id": 0x44, "offset": 0, "count": 2, "width_bits": 8})
        self.assertEqual(chain1[0]["register_or_table"], {"table_id": 0x64, "offset": 0, "count": 2, "width_bits": 8})
        self.assertEqual(chain0[0]["value"], ["nested+0x3e0", "nested+0x3e0"])
        self.assertEqual(chain1[0]["value"], ["nested+0x3e3", "nested+0x3e3"])
        writes0 = [e for e in chain0 if e["operation"] == "TABLE_WRITE"]
        writes1 = [e for e in chain1 if e["operation"] == "TABLE_WRITE"]
        self.assertEqual([(e["register_or_table"]["table_id"], e["register_or_table"]["offset"], e["register_or_table"]["count"]) for e in writes0], [(0x44, 0, 2), (0x44, 0x20, 10), (0x45, 0x20, 10)])
        self.assertEqual([(e["register_or_table"]["table_id"], e["register_or_table"]["offset"], e["register_or_table"]["count"]) for e in writes1], [(0x64, 0, 2), (0x64, 0x20, 10), (0x65, 0x20, 10)])
        self.assertEqual(writes0[1]["value"], ["0x03"] * 10)
        self.assertEqual(writes0[2]["value"], ["0x02"] * 10)
        self.assertEqual(len([e for e in data["events"] if e["operation"] == "TABLE_READ"]), 12)
        self.assertEqual(data["events"][-2]["site"], "0x9b719")


if __name__ == "__main__":
    unittest.main()

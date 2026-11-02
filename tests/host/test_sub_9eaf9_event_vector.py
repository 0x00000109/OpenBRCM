import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ARTIFACT = ROOT / "artifacts/contracts/bcm4352-sub-9eaf9-parameterized-complete.json"


class Sub9eaf9EventVectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(ARTIFACT.read_text())
        cls.events = cls.contract["events"]

    def test_full_selected_vector_and_only_symbolic_word(self):
        self.assertTrue(self.contract["complete"])
        self.assertTrue(self.contract["structurally_complete"])
        self.assertTrue(self.contract["event_vector_is_contiguous"])
        self.assertFalse(self.contract["historically_exact"])
        self.assertFalse(self.contract["hardware_ready"])
        self.assertEqual(len(self.events), 136)
        self.assertEqual([e["seq"] for e in self.events], list(range(1, 137)))
        symbolic = [e for e in self.events if e["value"] == [
            "0xe800", "0x0084", "TABLE14_OFFSET33_WORD2"]]
        self.assertEqual(len(symbolic), 1)
        self.assertEqual([x["name"] for x in self.contract["historical_semantic_parameters"]],
                         ["TABLE14_OFFSET33_WORD2"])
        self.assertEqual([x["name"] for x in self.contract["runtime_symbolic_inputs"]],
                         ["LPF_TABLE_A0", "LPF_TABLE_B0", "LPF_TABLE_A1", "LPF_TABLE_B1"])
        self.assertEqual(symbolic[0]["site"], "0x9f9fa")
        self.assertEqual(symbolic[0]["register_or_table"], {
            "table_id": 20, "offset": "0x033", "width_bits": 48,
        })

    def test_chip_id_gate_skips_4335_only_rmw(self):
        proof = next(p for p in self.contract["selected_branch_proofs"] if p["site"] == "0x9f231")
        self.assertEqual(proof["selected_value"], "0x4352")
        self.assertIn("false", proof["selected_result"])
        self.assertFalse(any(e["site"] == "0x9f26f" for e in self.events))

    def test_corrected_selected_phy_rmw_and_analog_lpf_position(self):
        bysite = {}
        for i, item in enumerate(self.events):
            bysite.setdefault(item["site"], []).append((i, item))
        self.assertEqual(bysite["0x9f1f6"][0][1]["register_or_table"], "0x0312")
        self.assertEqual(bysite["0x9f22c"][0][1]["register_or_table"], "0x0313")
        self.assertEqual(bysite["0x9f471"][0][1]["value"], 5)
        self.assertEqual(bysite["0x9f471"][1][1]["value"], 5)
        self.assertEqual(bysite["0x9f87f"][0][1]["register_or_table"], "0x073a")
        self.assertEqual(bysite["0x9f87f"][1][1]["register_or_table"], "0x093a")
        self.assertEqual(bysite["0x9d991"][0][0] < bysite["0x9f8d7"][0][0], True)
        self.assertEqual(bysite["0x9f8d7"][0][0] < bysite["0x9f9fa"][0][0], True)
        self.assertEqual(bysite["0x9f9fa"][0][0] < bysite["0x9fa1e"][0][0], True)
        self.assertEqual(self.events[-1]["site"], "0x9fb5b")
        self.assertEqual(self.contract["end_boundary"], "sub_9eaf9 return @0x9fb71")

    def test_selected_nested_lpf_offsets_formula_and_bit_restore(self):
        def events_at(site):
            return [e for e in self.events if e["site"] == site]

        read_a = events_at("0x9da4b")
        read_b = events_at("0x9da73")
        write_a = events_at("0x9dafe")
        write_b = events_at("0x9db1d")
        self.assertEqual([int(e["register_or_table"]["offset"], 16) for e in read_a], [0x14a, 0x15a])
        self.assertEqual([int(e["register_or_table"]["offset"], 16) for e in read_b], [0x36a, 0x37a])
        self.assertEqual([int(e["register_or_table"]["offset"], 16) for e in write_a], [0x14a, 0x15a])
        self.assertEqual([int(e["register_or_table"]["offset"], 16) for e in write_b], [0x36a, 0x37a])
        self.assertEqual(events_at("0x9d9ac")[0]["value"], "0x0002")
        self.assertEqual(events_at("0x9db59")[0]["value"], "LPF_PHY019E_INITIAL & 0x0002")
        for chain in (0, 1):
            low = events_at("0x9dafe")[chain]["value"]
            high = events_at("0x9db1d")[chain]["value"]
            self.assertIn("0x01ffffc7", low)
            self.assertIn("0x01fffe3f", low)
            self.assertIn("0x00000028", low)
            self.assertIn("0x00000140", low)
            self.assertIn("0x01ffffc7", high)
            self.assertIn("0x01fffe3f", high)
            self.assertIn("0x00000028", high)
            self.assertIn("0x00000140", high)
        all_offsets = {
            int(e["register_or_table"]["offset"], 16)
            for e in self.events
            if e["domain"] == "TABLE" and e["register_or_table"]["table_id"] == 20
        }
        self.assertFalse({0x30, 0x31, 0x32} & all_offsets)

    def test_revision_and_band_branch_proofs_exclude_alternate_events(self):
        proofs = {p["site"]: p for p in self.contract["selected_branch_proofs"]}
        self.assertEqual(proofs["0x9f54e"]["selected_value"], "0x0a")
        self.assertIn("skip", proofs["0x9f54e"]["selected_result"])
        self.assertEqual(proofs["0x9f5ce"]["selected_value"], "0x1000")
        self.assertIn("5-GHz path", proofs["0x9f5ce"]["selected_result"])
        self.assertEqual(proofs["0x9fafa"]["selected_value"], "0x1000")
        self.assertIn("0x9fb0e", proofs["0x9fafa"]["selected_result"])
        self.assertIn("0x9fa47", proofs)
        self.assertFalse(any(e["site"] in {"0x9f731", "0x9f752", "0x9f776"} for e in self.events))

    def test_table7_relocation_payloads_and_tail_vectors(self):
        table_events = [e for e in self.events if e["domain"] == "TABLE"]
        self.assertEqual(len(table_events), 89)
        sites = {e["site"] for e in table_events}
        self.assertTrue({"0x9f8d7", "0x9f8fb", "0x9f91f", "0x9f943",
                         "0x9f967", "0x9f98b", "0x9fa1e", "0x9fa42"}.issubset(sites))
        self.assertEqual(len([e for e in self.events if e["domain"] == "PHY"]), 47)
        values = [e["value"] for e in table_events]
        self.assertNotIn(["0xe800", "0x0084", "0x0000"], values)


if __name__ == "__main__":
    unittest.main()

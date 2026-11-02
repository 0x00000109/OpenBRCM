#!/usr/bin/env python3
"""Independent checks for the selected table payload contradiction and 9e378 matrix."""
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTRA = json.loads((ROOT / "artifacts/contracts/bcm4352-9eaf9-contradiction-20261009.json").read_text())
MATRIX = json.loads((ROOT / "artifacts/contracts/bcm4352-sub-9e378-16-way-fragment-20261009.json").read_text())


class TableAbiAndContradiction(unittest.TestCase):
    def test_stable_input_and_abi_register_mapping(self):
        abi = CONTRA["table_write_abi"]
        self.assertTrue(abi["proven"])
        self.assertEqual(abi["wlc_phy_table_write_acphy"], {
            "rdi": "pi", "rsi": "table_id", "rdx": "count",
            "rcx": "offset", "r8": "width", "r9": "data"})
        self.assertEqual(abi["descriptor_offsets"], {
            "data": "+0x00", "count": "+0x08", "table_id": "+0x0c",
            "offset": "+0x10", "width": "+0x14"})
        self.assertEqual(len(abi["independent_nonnull_callsites"]), 3)
        self.assertTrue(all(x["count"] == "0x01" for x in abi["independent_nonnull_callsites"]))
        self.assertTrue(all(x["width"] == "0x30" for x in abi["independent_nonnull_callsites"]))

    def test_selected_raw_zero_route_is_relocation_resolved(self):
        path = CONTRA["selected_9eaf9_path"]
        self.assertFalse(path["reaches_first_null_call"])
        self.assertEqual(path["literal_null_selected_call_count"], 0)
        calls = path["relocated_static_payload_calls"]
        self.assertEqual(len(calls), 6)
        self.assertEqual(calls[0]["site"], "0x9f8d7")
        self.assertEqual(calls[0]["pointer_resolution"]["resolved_object"], ".data+0xda080")
        null = CONTRA["table7_null_dereference"]
        self.assertTrue(null["reaches_element_loop"])
        self.assertTrue(null["dereferenced"])
        self.assertFalse(null["sentinel_semantics"])
        self.assertEqual(null["width16_dereference_site"], "0xb6afa")
        self.assertFalse(null["selected_calls_are_null"])
        self.assertTrue(null["hypothetical_literal_null_would_dereference"])

    def test_earlier_raw_zero_calls_are_relocated_and_precede_9eaf9(self):
        calls = CONTRA["other_raw_zero_r9_table_calls"]["selected_positive_count_calls_before_9eaf9"]
        self.assertEqual(len(calls), 7)
        self.assertEqual(calls[0]["site"], "0xa6c9e")
        self.assertTrue(all(x["count"] not in ("0x0", "0x00") for x in calls))
        self.assertTrue(all(x["width"] == "0x10" for x in calls))
        self.assertEqual(CONTRA["relocation_audit"]["literal_null_selected_calls"], 0)
        self.assertEqual(CONTRA["relocation_audit"]["classification"], "RELOCATION_ARTIFACT_RESOLVED")
        self.assertEqual(CONTRA["relocation_audit"]["previous_classification_status"], "SUPERSEDED")
        self.assertFalse(CONTRA["classification"]["through_crs_contract_complete"])

    def test_width48_consumes_indeterminate_third_word(self):
        x = CONTRA["table14_width48_payload"]
        self.assertTrue(x["width30_consumes_three_16bit_words"])
        self.assertEqual(x["words"][0]["value"], "0xe800")
        self.assertEqual(x["words"][1]["value"], "0x0084")
        self.assertIsNone(x["words"][2]["value"])
        self.assertFalse(x["third_word_proven"])
        self.assertTrue(x["indeterminate_payload_real"])
        self.assertEqual(CONTRA["historical_admissibility"]["cleanroom_policy"], "UNRESOLVED")
        self.assertEqual(CONTRA["first_real_unknown"]["element_word"], 2)


class Sub9e378Matrix(unittest.TestCase):
    def test_sixteen_complete_variants_and_contiguous_sequences(self):
        self.assertTrue(MATRIX["complete"])
        vs = MATRIX["variants"]
        self.assertEqual(len(vs), 16)
        self.assertEqual(len({v["id"] for v in vs}), 16)
        self.assertEqual({v["selectors"]["extpagain2g"] for v in vs}, {"EQ2", "NE2"})
        self.assertEqual({v["selectors"]["pdgain5g"] for v in vs}, {"9-or-16", "other byte value"})
        self.assertEqual({v["selectors"]["txchain_low2"] for v in vs}, {0, 1, 2, 3})
        variable_selectors = {key for v in vs for key in v["selectors"]
                              if len({w["selectors"][key] for w in vs}) > 1}
        self.assertEqual(variable_selectors, {"extpagain2g", "pdgain5g", "txchain_low2"})
        for v in vs:
            events = v["events"]
            self.assertEqual([e["seq"] for e in events], list(range(1, len(events)+1)))
            tx = v["selectors"]["txchain_low2"]
            pdwrite = int(v["selectors"]["pdgain5g"] == "9-or-16")
            self.assertEqual(v["event_count"], 52 + 7 * tx.bit_count() + pdwrite)
            self.assertEqual(v["phy_ops"], 33)
            self.assertEqual(v["radio_ops"], 14 + 7 * tx.bit_count())
            self.assertEqual(v["table_writes"], 5 + pdwrite)
            self.assertEqual(sum(e["domain"] == "DELAY" for e in events), 0)
            self.assertEqual(sum(e["domain"] == "POLL" for e in events), 0)

    def test_independent_payload_and_optional_write_vectors(self):
        byid = {v["id"]: v for v in MATRIX["variants"]}
        eq = byid["EQ2_PDGWRITE_TXCHAIN0"]["events"]
        ne = byid["NE2_PDGWRITE_TXCHAIN0"]["events"]
        gain_eq = next(e for e in eq if e["site"] == "0x9e711")
        gain_ne = next(e for e in ne if e["site"] == "0x9e711")
        self.assertEqual((gain_eq["payload_identity"]["symbol"], gain_eq["payload_identity"]["size_bytes"], gain_eq["payload_identity"]["sha256"]),
                         ("acphy_txgain_ipa_2g_2069rev0", 768, "d218af33aee8ebc6f3a885513267495cfd0c4f7fe10ca75696eb324545625c11"))
        self.assertEqual((gain_ne["payload_identity"]["symbol"], gain_ne["payload_identity"]["size_bytes"], gain_ne["payload_identity"]["sha256"]),
                         ("acphy_txgain_epa_2g_2069rev4", 768, "8854a71af6b79ad488510598391aefbcebe183f5d04489882335447ca6511c60"))
        pd = [e for e in eq if e["site"] == "0x9e92b"]
        self.assertEqual(len(pd), 1)
        self.assertEqual((pd[0]["table_id"], pd[0]["offset"], pd[0]["count"], pd[0]["width"], pd[0]["value"]),
                         ("0x07", "0x018e", 1, 16, "0x0000"))
        self.assertFalse(any(e["site"] == "0x9e92b" for e in byid["EQ2_PDGSKIP_TXCHAIN0"]["events"]))

    def test_corrected_txchain_paths_and_common_helper_rmw(self):
        byid = {v["id"]: v for v in MATRIX["variants"]}
        expected_counts = {0: 52, 1: 59, 2: 59, 3: 66}
        for tx, expected in expected_counts.items():
            v = byid[f"EQ2_PDGSKIP_TXCHAIN{tx}"]
            self.assertEqual(v["event_count"], expected)
            radio = [e for e in v["events"] if e["caller"] == "sub_940f8"]
            self.assertEqual(len(radio), 6 + 7 * tx.bit_count())
            self.assertEqual([e["register"] for e in radio[:6]],
                             ["0x095b", "0x095c", "0x095d", "0x095e", "0x095f", "0x0810"])
            per_core = []
            for c in (0, 1):
                if tx & (1 << c):
                    b = c * 0x200
                    per_core.extend([
                        (f"0x{0x001f+b:04x}", "0x00f0", "0x0010"),
                        (f"0x{0x001f+b:04x}", "0x0004", "0x0000"),
                        (f"0x{0x055e if c == 0 else 0x095e:04x}", "0xff00", "0x0100"),
                        (f"0x{0x001f+b:04x}", "0x0300", "0x0000"),
                        (f"0x{0x001c+b:04x}", "0x0002", "0x0000"),
                        (f"0x{0x0024+b:04x}", "0x0004", "0x0000"),
                        (f"0x{tx:04x}", "0x4000", "0x4000"),
                    ])
            self.assertEqual([(e["register"], e["mask"], e["value"]) for e in radio[6:]], per_core)
        common = byid["EQ2_PDGSKIP_TXCHAIN0"]["events"]
        hwaci = [e for e in common if e["caller"] == "wlc_phy_hwaci_setup_acphy"]
        w2nb = [e for e in common if e["caller"] == "wlc_phy_aci_w2nb_setup_acphy"]
        self.assertEqual(len(hwaci), 4)
        self.assertEqual(len(w2nb), 4)
        self.assertEqual([e["register"] for e in hwaci], ["0x0728", "0x0721", "0x0928", "0x0921"])
        self.assertEqual([e["register"] for e in w2nb], ["0x0729", "0x0721", "0x0929", "0x0921"])
        self.assertEqual(MATRIX["branch_map"][3]["selected_value"], "0")
        self.assertEqual(MATRIX["external_selector_policy"]["extpagain2g"].split(';')[0], "required")

    def test_sub9c161_table_and_save_restore_stream(self):
        v = next(v for v in MATRIX["variants"] if v["id"] == "EQ2_PDGSKIP_TXCHAIN0")
        e = v["events"]
        tables = [x for x in e if x["caller"] == "sub_9c161" and x["domain"] == "TABLE"]
        self.assertEqual([(x["offset"], x["count"], x["value"]) for x in tables],
                         [("0x0060",2,["0x0000","0x0000"]),("0x0062",1,["0x0000"]),
                          ("0x0064",2,["0x0000","0x0000"]),("0x0066",1,["0x0000"])])
        helper_phy = [x for x in e if x["caller"] == "sub_9bf99" and x["register"] == "0x019e"]
        self.assertEqual(len(helper_phy), 12)
        self.assertEqual(len([x for x in e if x["caller"] == "sub_9c161" and x["domain"] == "RADIO"]), 8)
        self.assertEqual(len([x for x in e if x["caller"] == "sub_9c161" and x["domain"] == "PHY"]), 4)

    def test_no_hidden_default_or_claim_of_through_crs(self):
        self.assertEqual(MATRIX["external_selector_policy"]["extpagain2g"], "required; target value unknown; no default")
        self.assertEqual(MATRIX["external_selector_policy"]["pdgain5g"], "required; target value unknown; no default")
        self.assertEqual(MATRIX["external_selector_policy"]["txchain_low2"], "required; target value unknown; no default")
        self.assertTrue(any("No through-CRS contract is complete" in x for x in MATRIX["forbidden_claims"]))


if __name__ == "__main__":
    unittest.main()

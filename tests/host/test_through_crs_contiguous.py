#!/usr/bin/env python3
"""Regression checks for materialized BCM4352 through-CRS event vectors."""

import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONTRACTS = ROOT / "artifacts/contracts"
SYMBOL = "TABLE14_OFFSET33_WORD2"


def load(name):
    return json.loads((CONTRACTS / name).read_text())


def without_seq(events):
    return [{k: v for k, v in event.items() if k != "seq"} for event in events]


class ThroughCrsContiguousTests(unittest.TestCase):
    def test_sub_a7089_selected_vector(self):
        contract = load("bcm4352-sub-a7089-selected-event-vector.json")
        events = contract["events"]
        self.assertTrue(contract["complete"])
        self.assertTrue(contract["event_vector_is_contiguous"])
        self.assertEqual(contract["event_count"], 88)
        self.assertEqual(len(events), 88)
        self.assertEqual(events[0]["site"], "0xa7262")
        self.assertEqual(events[0]["operation"], "READ")
        self.assertTrue(all(e.get("row_index") is None for e in events[:25]))
        self.assertEqual(events[25]["row_index"], 63)
        self.assertEqual(events[50]["row_index"], 63)
        self.assertEqual(sum(e.get("row_index") == 63 for e in events), 50)
        self.assertEqual(len(events[75:]), 13)
        self.assertEqual(events[-1]["site"], "0xaa404")
        self.assertEqual(events[-1]["register"], "0x019e")
        self.assertEqual(
            [e["register"] for e in events if e["source_helper"] == "sub_979bc"],
            ["0x06d4", "0x08d4"],
        )
        self.assertEqual(sum(e["source_helper"] == "sub_9311b" for e in events), 9)
        skipped_nonrow_sites = {"0xa9f32", "0xa9f66", "0xa9fa4", "0xa9fe3", "0xaa021"}
        self.assertFalse(any(e["site"] in skipped_nonrow_sites for e in events))
        self.assertTrue(any(e.get("register") == "0x08d6" and e.get("value") == "0x0cef"
                            and e.get("row_index") == 63 for e in events))
        self.assertFalse(any(e.get("register") in {"0x0323", "0x0523", "0x08ec"}
                             for e in events))
        self.assertFalse(any(e.get("register") == "0x0892" and e["operation"] == "RMW"
                             for e in events))
        self.assertTrue(any(e["site"] == "0xaa05f" and e.get("register") == "0x0645"
                            and e["operation"] == "RMW" for e in events))
        self.assertTrue(any(e["site"] == "0xaa0bc" and e.get("register") == "0x0723"
                            and e["value"] == "0x83e0" for e in events))

    def test_sub_a0be4_selected_vector_and_skips(self):
        contract = load("bcm4352-sub-a0be4-selected-event-vector.json")
        events = contract["events"]
        self.assertTrue(contract["complete"])
        self.assertEqual(contract["event_count"], 40)
        self.assertEqual(len(events), 40)
        self.assertEqual(contract["delay_count"], 2)
        self.assertEqual(contract["hwaci_event_count"], 4)
        self.assertEqual(events[0]["site"], "0xa0c00")
        self.assertEqual(events[-1]["site"], "0xa13da")
        self.assertEqual([e["register"] for e in events if e["source_helper"] == "wlc_phy_hwaci_setup_acphy"],
                         ["0x0728", "0x0721", "0x0928", "0x0921"])
        self.assertEqual([e["value"] for e in events if e.get("mask") == "0x0200" and e["register"] in {"0x0690", "0x0890"}],
                         ["0x0200", "0x0200"])
        self.assertNotIn("0xa1031", [e["site"] for e in events])
        self.assertEqual(contract["selected_skips"][3]["table2_call_0xa1031"], "SKIPPED")
        self.assertEqual(contract["count_reconciliation"]["non_delay_events"], 38)

    def test_common_prefix_joins_accepted_fragments(self):
        common = load("bcm4352-common-prefix-through-a6b0f.json")
        events = common["events"]
        self.assertTrue(common["complete"])
        self.assertTrue(common["structurally_complete"])
        self.assertTrue(common["event_vector_is_contiguous"])
        self.assertEqual(common["event_count"], 4074)
        self.assertEqual(len(events), 4074)
        self.assertEqual([e["seq"] for e in events], list(range(1, 4075)))
        self.assertEqual(common["component_counts"], {
            "sub_a04c2": 3734,
            "sub_a7089_selected_to_a0be4": 88,
            "sub_a0be4": 40,
            "sub_a6b0f": 212,
        })
        self.assertEqual(events[0]["site"], "0xa04e9")
        self.assertEqual(events[3734]["site"], "0xa7262")
        self.assertEqual(events[3734 + 88]["site"], "0xa0c00")
        self.assertEqual(events[3734 + 88 + 40]["site"], "0xa6b3c")
        self.assertEqual(common["join_map"], [
            {"from": "sub_a04c2 return @0xa0b39", "to": "sub_a7089 first selected hardware op @0xa7262", "intervening": "selected software lookup/control flow only", "validated": True},
            {"from": "sub_a7089 PHY restore @0xaa404", "to": "sub_a0be4 call/entry @0xaa412/0xa0be4", "validated": True},
            {"from": "sub_a0be4 return @0xa13eb", "to": "sub_a6b0f call @0xaa41a", "intervening": "caller transfer only", "validated": True},
            {"from": "sub_a6b0f return", "to": "selected sub_9e378 variant entry", "intervening": "no selected hardware event", "validated": True},
        ])

    def test_all_sixteen_actual_streams_and_only_expected_differences(self):
        common = load("bcm4352-common-prefix-through-a6b0f.json")["events"]
        n_prefix = len(common)
        predecessor = load("bcm4352-sub-9e378-16-way-fragment-20261009.json")
        source_by_id = {v["id"]: v for v in predecessor["variants"]}
        files = sorted(CONTRACTS.glob("bcm4352-through-crs-param-*.json"))
        self.assertEqual(len(files), 16)
        contracts = [json.loads(path.read_text()) for path in files]
        self.assertEqual({c["variant_source"]["variant_id"] for c in contracts}, set(source_by_id))
        self.assertEqual(len({c["selectors"]["extpagain2g"] for c in contracts}), 2)
        self.assertEqual({c["selectors"]["pdgain5g_class"] for c in contracts}, {"WRITE", "SKIP"})
        self.assertEqual({c["selectors"]["txchain_low2"] for c in contracts}, {0, 1, 2, 3})

        common_prefix = without_seq(common)
        reference_suffix = None
        for contract in contracts:
            events = contract["events"]
            self.assertIsInstance(events, list)
            self.assertGreater(len(events), 4000)
            self.assertTrue(contract["structurally_complete"])
            self.assertTrue(contract["event_vector_is_contiguous"])
            self.assertFalse(contract["executable_exact_historical"])
            self.assertFalse(contract["historically_exact"])
            self.assertFalse(contract["hardware_ready"])
            self.assertEqual(contract["event_count"], len(events))
            self.assertEqual([e["seq"] for e in events], list(range(1, len(events) + 1)))
            self.assertEqual(without_seq(events[:n_prefix]), common_prefix)
            self.assertEqual(contract["historical_semantic_parameters"][0]["name"], SYMBOL)

            variant_id = contract["variant_source"]["variant_id"]
            source_events = source_by_id[variant_id]["events"]
            n_variant = len(source_events)
            self.assertEqual(contract["variant_source"]["event_count"], n_variant)
            self.assertEqual(without_seq(events[n_prefix:n_prefix + n_variant]), without_seq(source_events))

            suffix = events[n_prefix + n_variant:]
            self.assertEqual(len(suffix), 189)  # 136 sub_9eaf9 + 33 Farrow + 20 CRS wrapper
            if reference_suffix is None:
                reference_suffix = without_seq(suffix)
            else:
                self.assertEqual(without_seq(suffix), reference_suffix)

            symbolic = [e for e in events if SYMBOL in json.dumps(e, sort_keys=True)]
            self.assertEqual(len(symbolic), 1)
            self.assertEqual(symbolic[0]["value"], ["0xe800", "0x0084", SYMBOL])
            runtime_inputs = set(contract["runtime_symbolic_inputs"])
            self.assertTrue({
                "R_A04C2_019E", "R_A04C2_1725", "R_A04C2_173A",
                "A7089_PHY019E_SNAPSHOT_0", "A7089_PHY019E_SNAPSHOT_1",
                "A7089_CLASSIFIER_PHY0140_INITIAL", "A7089_CARRIER_RESETCCA_PHY0001_SNAPSHOT",
                "A0BE4_RESETCCA_PHY0001_SNAPSHOT", "A6B0F_PHY019E_SNAPSHOT@0xa6b3c",
                "SUB_9E378_PHY019E_ENTRY_SNAPSHOT", "LPF_PHY019E_INITIAL",
                "LPF_TABLE_A0", "LPF_TABLE_B0", "LPF_TABLE_A1", "LPF_TABLE_B1",
                "R_FARROW_0601", "R_FARROW_019E", "R_FARROW_T7_C0", "R_FARROW_T7_C1",
            }.issubset(runtime_inputs))

            nine_eaf9 = suffix[:136]
            self.assertEqual(sum(e["domain"] == "PHY" for e in nine_eaf9), 47)
            self.assertEqual(sum(e["domain"] == "TABLE" for e in nine_eaf9), 89)
            self.assertEqual(sum(e["domain"] == "TABLE" and e.get("register_or_table", {}).get("table_id") == 20
                                 and e.get("register_or_table", {}).get("offset") in {"0x030", "0x031", "0x032"}
                                 for e in nine_eaf9), 0)
            self.assertEqual(sum(e.get("site") == "0x9f9fa" for e in nine_eaf9), 1)
            self.assertEqual(suffix[-1]["target"], "wlapi_enable_mac")

    def test_crs_wrapper_and_symbolic_field_are_not_silent_defaults(self):
        contracts = [json.loads(p.read_text()) for p in CONTRACTS.glob("bcm4352-through-crs-param-*.json")]
        for contract in contracts:
            events = contract["events"]
            wrapper = events[-20:]
            self.assertEqual([e.get("target") for e in wrapper if e["operation"] == "API_CALL"], [
                "wlapi_suspend_mac_and_wait", "wlc_phyreg_enter", "wlc_phyreg_exit", "wlapi_enable_mac"
            ])
            self.assertEqual(sum(e["source_helper"] == "sub_8f41b" and e["operation"] == "RMW" for e in wrapper), 16)
            self.assertTrue(any(e.get("max_declared_wait_us") == 148580 for e in wrapper))
            event_data = json.dumps(events, sort_keys=True)
            self.assertIn(SYMBOL, event_data)
            self.assertNotIn('"0xe800",\n          "0x0084",\n          "0x0000"', event_data)
            self.assertFalse(contract["historical_semantic_parameters"][0]["candidate_selected"])


if __name__ == "__main__":
    unittest.main()

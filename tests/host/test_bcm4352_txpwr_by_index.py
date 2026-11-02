#!/usr/bin/env python3
"""Instruction-derived independent oracle for the isolated TX-power helper."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/host"))
from txpwr_index_oracle import expected_callbacks


CONTRACT = ROOT / "docs/research/contracts/20261011/bcm4352-sub-9868f-arithmetic-correction-v1.json"
OLD_CONTRACT = ROOT / "docs/research/contracts/20261011/bcm4352-txpwr-by-index-continuation-research-v1.json"
BINARY = ROOT / "tests/host/ob_phy_txpwr_index_test"


class TxPowerByIndexOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", "tests/host", "ob_phy_txpwr_index_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)

    def test_direct_ax_store_truncation_corrects_the_old_shift_claim(self):
        correction = json.loads(CONTRACT.read_text(encoding="utf-8"))
        self.assertEqual(correction["supersedes_contract_sha256"],
                         "d6c4601ee7ac208950d1928eee01088bc4b2111b164ea12eb5e941d48a0888ab")
        self.assertEqual(correction["corrected_packing"]["scratch_plus_0"],
                         "u16(d1 + (d2 << 8)); d3 << 16 is discarded by the AX store")
        original = json.loads(OLD_CONTRACT.read_text(encoding="utf-8"))
        old_text = json.dumps(original, sort_keys=True)
        self.assertIn("d2<<9", old_text)

    def test_final_bcm4352_ac_attach_bound_overrides_generic_type_bound(self):
        implementation = json.loads((ROOT / "docs/research/contracts/20261011/"
                                     "bcm4352-txpwr-by-index-cleanroom-implementation-v1.json")
                                    .read_text(encoding="utf-8"))
        attach = implementation["historical_attach_core_bound"]
        self.assertEqual(attach["generic_type_formula"]["other_types_including_0x0b_bound"], 1)
        self.assertEqual(attach["acphy_child"]["phy_read"], {
            "site": "0x0a21af", "register": "0x000b", "width_bits": 16,
        })
        self.assertEqual(attach["acphy_child"]["intermediate_bound_store"], {
            "site": "0x0a21bb",
            "expression": "u8(PHY_READ16(0x000b) & 0x0007)"})
        self.assertEqual(attach["bcm4352_override"], {
            "site": "0x0a21e3", "predicate": "u32(SI+0x3c) == 0x4352",
            "stored_value": 2})
        self.assertEqual(attach["final_bcm4352_acphy_bound"], 2)
        scenarios = implementation["executed_synthetic_scenarios"]
        self.assertEqual(scenarios[1]["core_bound"], 1)
        self.assertEqual(scenarios[1]["classification"], "SYNTHETIC_TEST_INPUT")
        self.assertEqual(scenarios[2]["core_bound"], 2)
        self.assertEqual(scenarios[2]["classification"], "SYNTHETIC_TEST_INPUT")

    def test_c_trace_matches_instruction_order_and_payloads(self):
        sample = bytes((0x10, 0x21, 0x32, 0x43, 0x54, 0x65))
        expected, final_phy = expected_callbacks(1, 1, -7, sample)
        output = subprocess.run([str(BINARY), "--trace"], cwd=ROOT, check=True,
                                capture_output=True, text=True).stdout
        actual = json.loads(output)
        self.assertEqual(actual, expected)
        self.assertEqual(len(actual), 21)
        self.assertEqual(final_phy, 0xa5a4)
        self.assertEqual(actual[9]["payload"], [0x21, 0x32])
        self.assertEqual(actual[10]["payload"], [0x43, 0x54])
        self.assertEqual(actual[11]["payload"], [0x65, 0x00])

    def test_multicore_traces_match_independent_instruction_oracle(self):
        sample = bytes((0x10, 0x21, 0x32, 0x43, 0x54, 0x65))
        for mode, mask, bound, index, initial in (
                ("--trace-two-core", 3, 2, 127, 0xa5a4),
                ("--trace-four-core", 0x0f, 4, -128, 0xa5a4)):
            with self.subTest(mode=mode):
                expected, final_phy = expected_callbacks(
                    mask, bound, index, sample, initial)
                actual = json.loads(subprocess.run(
                    [str(BINARY), mode], cwd=ROOT, check=True,
                    capture_output=True, text=True).stdout)
                self.assertEqual(actual, expected)
                self.assertEqual(final_phy, initial)

    def test_core1_and_runtime_sample_match_independent_oracle(self):
        sample = bytes((0x91, 0x82, 0x73, 0x64, 0x55, 0x46))
        expected, final_phy = expected_callbacks(2, 2, 5, sample, 0x7a06)
        actual = json.loads(subprocess.run(
            [str(BINARY), "--trace-core1"], cwd=ROOT, check=True,
            capture_output=True, text=True).stdout)
        self.assertEqual(actual, expected)
        self.assertEqual(final_phy, 0x7a06)

    def test_signed_offset_and_little_endian_pack_are_preserved(self):
        sample = bytes((0xf1, 0xe2, 0xd3, 0xc4, 0xb5, 0xa6))
        events, _ = expected_callbacks(1, 1, -128, sample, 0x1236)
        read = next(event for event in events if event["kind"] == "TABLE_READ")
        self.assertEqual(read["offset"], -128)
        self.assertEqual(events[9]["payload"], [0xe2, 0xd3])
        self.assertEqual(events[10]["payload"], [0xc4, 0xb5])
        self.assertEqual(events[11]["payload"], [0xa6, 0x00])
        self.assertEqual(events[-1]["value"], 0x1236)


if __name__ == "__main__":
    unittest.main()

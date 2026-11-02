#!/usr/bin/env python3
"""Independent instruction-derived oracle for the bounded 9a121 continuation."""
import json
import hashlib
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SUB = ROOT / "artifacts/contracts/bcm4352-sub-90493-cleanroom-execution-v1.json"
CONT = ROOT / "artifacts/contracts/bcm4352-sub-9a121-active-core-continuation-v1.json"
PINNED = "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"


def s8(value):
    value &= 0xff
    return value - 256 if value & 0x80 else value


def helper(chanspec, argument, mode, core, sample, base, factor, curve, extra):
    band = chanspec & 0x3800
    idx = 0 if band == 0x1000 else 1 if band == 0x1800 else 2
    delta = (-66, -63, -60)[idx]
    threshold = (25, 22, 19)[idx]
    x = curve[(core * 0x78) + (sample & 1)]
    candidate = ((base + delta - x * factor) if mode else delta) + extra
    bound = -(argument + threshold)
    return (candidate & 0xff) if s8(candidate) >= s8(bound) else (bound & 0xff)


def caller_next_args(chanspec, selected, shared, core, child2, child4,
                     first_return, second_return, sample1, sample2, coeff):
    base = coeff[0x0f + core * 3]
    curve1 = coeff[core * 0x78 + 0x1a + (sample1 & 1)]
    curve2 = coeff[core * 0x78 + 0x1a + (sample2 & 1)]
    d1 = ((base - 16) if selected else 0xf0) - curve1
    d2 = ((base - 16) if shared else 0xf0) - curve2
    updated = min(s8(d1), s8(0x17 - child2)) & 0xff
    capped = min(s8(d2), s8(0x17 - child4)) & 0xff
    if chanspec & 0x3800 == 0x1000:
        arg1 = ((s8(updated) + s8(first_return)) & 0xffffffff) >> 1
        arg2 = ((s8(capped) + s8(second_return)) & 0xffffffff) >> 1
    else:
        arg1 = (-((((s8(updated) + s8(first_return)) * -39 +
                    s8(first_return) * -26) & 0xffffffff) >> 6)) & 0xffffffff
        arg2 = (-(((s8(capped) * -39 + s8(second_return) * -26) &
                   0xffffffff) >> 6)) & 0xffffffff
    return s8(arg1), s8(arg2)


class Sub90493ContinuationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sub = json.loads(SUB.read_text())
        cls.cont = json.loads(CONT.read_text())

    def test_helper_contract_has_exact_single_runtime_read_and_return(self):
        self.assertEqual(self.sub["identity"]["vendor_sha256"], PINNED)
        self.assertEqual(self.sub["entry"], "sub_90493 @0x90493")
        self.assertEqual(self.sub["return"], "sub_90493 @0x90579")
        self.assertEqual(len(self.sub["selected_operation_stream"]), 1)
        event = self.sub["selected_operation_stream"][0]
        self.assertEqual((event["site"], event["register"], event["callback_count"]),
                         ("0x90506", "0x06dc + 0x0200*core", 1))

    def test_callers_use_selector_four_then_selector_three_returns(self):
        calls = self.sub["selected_callers"]
        self.assertEqual([x["site"] for x in calls], ["sub_9a121 @0x9a398", "sub_9a121 @0x9a40d"])
        self.assertIn("child_returns[3] (selector 4)", calls[0]["arguments"])
        self.assertIn("child_returns[4] (selector 3)", calls[1]["arguments"])
        self.assertEqual(calls[0]["arguments"][2], "(nested+0x66e & selected_byte) != 0")
        self.assertEqual(calls[1]["arguments"][2], 1)

    def test_synthetic_formula_uses_runtime_bit_and_explicit_nested_fields(self):
        curve = [1, 0] + [0] * 0x90
        self.assertEqual(helper(0x1001, 19, 0, 0, 0, 2, 0, curve, 3), 0xd4)
        self.assertEqual(helper(0x1001, 19, 1, 0, 0, 2, 0, curve, 3), 0xd4)
        curve[0] = 1
        curve[1] = 0
        self.assertEqual(helper(0x1001, 19, 1, 0, 0, 2, 5, curve, 30), 0xd9)
        self.assertEqual(helper(0x1001, 19, 1, 0, 1, 2, 5, curve, 30), 0xde)

    def test_active_continuation_keeps_four_distinct_read_callbacks(self):
        expected_sites = [
            "0x9a1af", "0x9a1ca", "0x9a1ca", "0x9a304",
            "0x99fef", "0x99fff", "0x9a04f", "0x9a079", "0x9a10a",
            "0x9a0a4", "0x9a10a", "0x9a0fa", "0x9a10a", "0x9a0cf",
            "0x9a10a", "0x90506", "0x9a3b4", "0x90506", "0x9a42c",
        ]
        self.assertEqual(self.cont["callback_expansion"]["bounded_total"],
                         {"logical_events": 18, "callbacks": 19})
        self.assertEqual(self.cont["full_callback_sites"], expected_sites)
        self.assertEqual(self.cont["full_callback_sites"][-4:],
                         ["0x90506", "0x9a3b4", "0x90506", "0x9a42c"])
        self.assertEqual(self.cont["exit_boundary"], "sub_9a121 @0x9a4e7 immediately before sub_9175a call")
        self.assertEqual(self.cont["first_unimplemented_operation"]["site"], "sub_9175a @0x91825")
        self.assertFalse(self.cont["hardware_ready"])

    def test_software_handoff_arguments_match_instruction_widths(self):
        coeff = bytearray(0x122)
        coeff[0x0f] = 2
        coeff[0x1a] = 1
        self.assertEqual(caller_next_args(0x1001, 0x55, False, 0, 19, 19,
                                          0xd4, 0xd4, 0, 0, coeff),
                         (-30, -31))

    def test_unknown_target_inputs_are_not_promoted(self):
        unknowns = self.cont["unknown_inputs"]
        for value in ("actual PI+0xa7 mask", "actual initial chanspec PI+0x17e",
                      "TABLE14_OFFSET33_WORD2"):
            self.assertIn(value, unknowns)
        self.assertEqual(self.sub["inputs"]["chanspec"]["actual_value"], "UNKNOWN")
        self.assertEqual(self.sub["inputs"]["nested_66f"]["actual_value"], "UNKNOWN")

    def test_accepted_predecessor_contracts_remain_hash_pinned(self):
        for item in self.cont["accepted_predecessor_contracts"]:
            self.assertEqual(hashlib.sha256((ROOT / item["path"]).read_bytes()).hexdigest(),
                             item["sha256"], item["path"])


if __name__ == "__main__":
    unittest.main()

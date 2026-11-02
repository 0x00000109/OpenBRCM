#!/usr/bin/env python3
"""Independent assembly-derived oracle for the bounded 9a121 active prefix."""
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MATRIX = ROOT / "artifacts/contracts/bcm4352-sub-9a121-active-core-matrix-v1.json"
CHILD = ROOT / "artifacts/contracts/bcm4352-sub-99f29-cleanroom-execution-v1.json"


def s8(value):
    value &= 0xFF
    return value - 0x100 if value & 0x80 else value


def selected_sub_8f086(nested, counts, core, delta, format_byte, initial):
    """Interpret the indexed byte-selection loop independently from the C code."""
    row = core * 0x78
    base = nested[0x0F + core * 3]
    buf = [(value + (base if format_byte else 0)) & 0xFF
           for value in initial]
    target = base + delta
    acc = 0
    for i in range(6):
        if i == 3:
            if core % 3 == 2:
                target += 1
            if (target & 0xFFFF) > 0x1E:
                target = 0x1E
        coeff_sum = sum(nested[row + off] for off in (0x24, 0x2E, 0x38, 0x42, 0x4C))
        residual = (target - coeff_sum) & 0xFF
        current = buf[i]
        for candidate in range(counts[i], 0, -1):
            index = candidate - 1
            x = s8(nested[row + 0x1A + i * 0x0A + index])
            y = nested[row + 0x56 + i * 0x0A + index]
            y0 = nested[row + 0x56 + i * 0x0A]
            if y != y0 and (x > s8(residual) or x + acc > current):
                continue
            buf[i] = y
            acc += x
            target -= x
            break
        else:
            raise AssertionError("synthetic fixture has no proven candidate")
    return buf, (acc - base) & 0xFF


def pack_words(out, format_byte):
    first = ((out[3] << 7) | (out[2] << 4) | (out[1] << 1)) & 0xFFFF
    second = ((out[4] << 4) | (out[5] << 8) | (8 if format_byte else 4)) & 0xFFFF
    table = ((out[4] << 14) | (out[5] << 13) | out[1] |
             (out[3] << 6) | (out[2] << 3)) & 0xFFFF
    return first, second, table


class ActiveCoreContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.matrix = json.loads(MATRIX.read_text())
        cls.child = json.loads(CHILD.read_text())

    def test_mask_values_are_explicit_and_actual_mask_remains_unknown(self):
        self.assertIn("UNKNOWN", self.matrix["actual_pi_a7_mask"])
        self.assertEqual([x["mask"] for x in self.matrix["synthetic_scenarios"]],
                         ["0x00", "0x01", "0x02", "0x03"])
        self.assertEqual(self.matrix["synthetic_scenarios"][0]["callbacks"], 5)
        self.assertEqual(self.matrix["synthetic_scenarios"][1]["callbacks"], 15)

    def test_five_distinct_child_calls_and_callback_expansion(self):
        calls = self.child["caller_invocations_per_active_core"]
        self.assertEqual([x["selector"] for x in calls], [0, 1, 2, 4, 3])
        self.assertEqual([x["site"] for x in calls],
                         ["0x9a319", "0x9a331", "0x9a34c", "0x9a368", "0x9a383"])
        self.assertEqual(self.child["callback_expansion"],
                         {"selector_0": 3, "selectors_1_to_4_each": 2,
                          "five_invocations": 11})

    def test_candidate_count_is_a_six_byte_sequence_not_one_scalar(self):
        self.assertIn("nested_64a_through_64f", self.child["inputs"])
        self.assertNotIn("nested_64a", self.child["inputs"])
        self.assertTrue(self.child["inputs"]["nested_64a_through_64f"]
                        ["per_output_candidate_counts"])

    def test_source_derived_fixture_payload_and_register_words(self):
        nested = bytearray(0x122)
        nested[0x0F] = 2
        for i in range(6):
            nested[0x1A + i * 0x0A] = i + 1
            nested[0x56 + i * 0x0A] = i + 5
        initial = [0x2B, 0x2B, 0x2B, 0x34, 0x34, 0x64]
        generated, return_byte = selected_sub_8f086(nested, [1] * 6, 0, 0x45, 0, initial)
        self.assertEqual(generated, [5, 6, 7, 8, 9, 10])
        self.assertEqual(return_byte, 19)
        self.assertEqual(pack_words(generated, 0), (0x047C, 0x0A94, 0x423E))
        generated_mode, _ = selected_sub_8f086(nested, [1] * 6, 0, 0x23, 1, initial)
        self.assertEqual(generated_mode, generated)
        self.assertEqual(pack_words(generated_mode, 1)[:2], (0x047C, 0x0A98))

    def test_per_output_candidate_count_changes_only_that_selected_byte(self):
        nested = bytearray(0x122)
        nested[0x0F] = 2
        for i in range(6):
            nested[0x1A + i * 0x0A] = i + 1
            nested[0x56 + i * 0x0A] = i + 5
        nested[0x1A + 4 * 0x0A + 1] = 0
        nested[0x56 + 4 * 0x0A + 1] = 12
        initial = [0x2B, 0x2B, 0x2B, 0x34, 0x34, 0x64]
        counts = [1, 1, 1, 1, 2, 1]
        out, _ = selected_sub_8f086(nested, counts, 0, 0x45, 0, initial)
        self.assertEqual(out, [5, 6, 7, 8, 12, 10])
        self.assertEqual(pack_words(out, 0)[:2], (0x047C, 0x0AC4))

    def test_core_threshold_cap_rejects_high_candidate_at_output_four(self):
        nested = bytearray(0x122)
        nested[0x0F] = 2
        for i in range(6):
            nested[0x1A + i * 0x0A] = i + 1
            nested[0x56 + i * 0x0A] = i + 5
        nested[0x1A + 4 * 0x0A + 1] = 20
        nested[0x56 + 4 * 0x0A + 1] = 12
        initial = [0x2B, 0x2B, 0x2B, 0x34, 0x34, 0x64]
        out, _ = selected_sub_8f086(nested, [1, 1, 1, 1, 2, 1],
                                    0, 0x45, 0, initial)
        self.assertEqual(out, [5, 6, 7, 8, 9, 10])
        self.assertEqual(pack_words(out, 0)[1], 0x0A94)

    def test_table_wrapper_argument_mapping_and_stop_boundary(self):
        self.assertEqual(self.child["access_semantics"].split(".")[0],
                         "PHY writes and the one TABLE API call are individual checked callbacks")
        self.assertIn("offset=ESI, count=EDX, table_id=ECX, width=R8D, payload=R9",
                      self.child["access_semantics"])
        self.assertIn("0x90506", self.matrix["new_prefix"]["first_unimplemented_operation"])
        self.assertEqual(self.matrix["new_prefix"]["total_callbacks"], 15)


if __name__ == "__main__":
    unittest.main()

import importlib.util
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "analysis/tools/generate_b018f_a04c2_caller_join.py"
INPUT = ROOT / "artifacts/contracts/bcm4352-sub-a04c2-selected-event-vector.json"
OUTPUT = ROOT / "artifacts/contracts/bcm4352-post-pmu-through-a04c2-caller-segment.json"


spec = importlib.util.spec_from_file_location("caller_join", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CallerJoinTests(unittest.TestCase):
    def test_segment_contains_actual_ordered_a04c2_vector_and_exact_edges(self):
        data = module.build(INPUT)
        self.assertTrue(data["complete_for_declared_segment"])
        self.assertFalse(data["through_crs_complete"])
        self.assertTrue(data["event_vector_is_contiguous"])
        self.assertEqual(data["event_count"], 3734)
        self.assertEqual(data["events"][0]["site"], "0xa04e9")
        self.assertEqual(data["events"][-1]["site"], "0xa0b34")
        self.assertEqual([e["seq"] for e in data["events"]], list(range(1, 3735)))
        edges = {item["site"]: item for item in data["control_flow"]}
        self.assertEqual(edges["0xb0288"]["result"], "3734-event vector; returns at 0xa0be4")
        self.assertEqual(edges["0xb028d"]["target"], "nested+0x32c")
        self.assertEqual(edges["0xb02a9"]["target"], "sub_a7089")

    def test_generated_segment_is_stable(self):
        expected = module.build(INPUT)
        actual = json.loads(OUTPUT.read_text())
        self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()

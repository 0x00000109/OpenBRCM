import unittest

try:
    from scripts.evaluate_bcm4352_contract import (
        compare_traces,
        evaluate_scenario,
    )
except ImportError:
    compare_traces = None
    evaluate_scenario = None


class ContractEvaluatorTests(unittest.TestCase):
    def test_evaluator_api_exists(self):
        self.assertIsNotNone(evaluate_scenario, "host-only event evaluator is not implemented")

    def test_applies_width_limited_rmw_using_supplied_snapshot(self):
        program = {"steps": [{"kind": "RMW", "domain": "PHY", "register": "0x10", "width_bits": 16, "mask": "0x00f0", "value": {"symbol": "insert"}}]}
        trace = evaluate_scenario(program, {"insert": 0x50}, {"PHY:0x10": 0x1234})
        self.assertEqual(trace[0]["old_value"], 0x1234)
        self.assertEqual(trace[0]["new_value"], 0x1254)

    def test_rejects_missing_historical_parameter(self):
        program = {"steps": [{"kind": "WRITE", "domain": "RADIO", "register": "0x20", "width_bits": 16, "value": {"symbol": "word2"}}]}
        with self.assertRaisesRegex(ValueError, "unresolved symbol: word2"):
            evaluate_scenario(program, {}, {})

    def test_emits_table_write_and_delay_without_device_io(self):
        program = {"steps": [
            {"kind": "TABLE_WRITE", "table": 100, "offset": 32, "width_bits": 8, "payload": [1, 2]},
            {"kind": "DELAY", "usec": 2},
        ]}
        trace = evaluate_scenario(program, {}, {})
        self.assertEqual(trace[0]["payload"], [1, 2])
        self.assertEqual(trace[1], {"kind": "DELAY", "usec": 2})

    def test_rejects_missing_table_payload_instead_of_defaulting_empty(self):
        program = {"steps": [{"kind": "TABLE_WRITE", "table": 100, "offset": 32, "width_bits": 8}]}
        with self.assertRaisesRegex(ValueError, "requires table, offset, width_bits and payload"):
            evaluate_scenario(program, {}, {})

    def test_selects_finite_branch_and_describes_bounded_poll(self):
        program = {"steps": [
            {"kind": "BRANCH", "symbol": "arm", "arms": {"zero": [{"kind": "WRITE", "domain": "PHY", "register": "1", "width_bits": 16, "value": 4}], "nonzero": []}},
            {"kind": "POLL", "domain": "PHY", "register": "2", "mask": "0x1", "expected": "0x1", "max_polls": 10, "interval_us": 5},
        ]}
        trace = evaluate_scenario(program, {"arm": "zero"}, {})
        self.assertEqual(trace[0]["value"], 4)
        self.assertEqual(trace[1]["max_polls"], 10)

    def test_reports_first_event_difference(self):
        left = [{"kind": "WRITE", "register": "1"}, {"kind": "WRITE", "register": "2"}]
        right = [{"kind": "WRITE", "register": "1"}, {"kind": "WRITE", "register": "3"}]
        self.assertEqual(compare_traces(left, right), {"equal": False, "first_difference": 1, "left": left[1], "right": right[1]})


if __name__ == "__main__":
    unittest.main()

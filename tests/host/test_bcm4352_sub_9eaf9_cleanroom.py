import hashlib
import json
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "artifacts/contracts/bcm4352-sub-9eaf9-parameterized-complete.json"
EXECUTION = ROOT / "artifacts/contracts/bcm4352-sub-9eaf9-cleanroom-execution-v1.json"
SCENARIOS = ROOT / "artifacts/contracts/bcm4352-sub-9eaf9-synthetic-scenarios-v1.json"
GENERATOR = ROOT / "scripts/generate_bcm4352_9eaf9.py"


class Sub9eaf9CleanroomContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source_bytes = SOURCE.read_bytes()
        cls.source = json.loads(cls.source_bytes)

    def test_immutable_source_identity_and_bounded_vector(self):
        self.assertEqual(
            hashlib.sha256(self.source_bytes).hexdigest(),
            "982abc2fd2460c34371635ce2022d4fc0ed206fc7645264f45d7c874c60f236f",
        )
        self.assertEqual(self.source["event_count"], 136)
        self.assertEqual(self.source["events"][0]["site"], "0x9eb45")
        self.assertEqual(self.source["events"][-1]["site"], "0x9fb5b")
        self.assertEqual(self.source["end_boundary"], "sub_9eaf9 return @0x9fb71")

    def test_execution_contract_preserves_unknown_and_separate_policies(self):
        contract = json.loads(EXECUTION.read_text())
        unknown = contract["historical_semantic_parameters"][0]
        self.assertEqual(unknown["id"], "TABLE14_OFFSET33_WORD2")
        self.assertEqual(unknown["classification"], "HISTORICAL_SEMANTIC_UNKNOWN")
        self.assertTrue(unknown["unknown"])
        for forbidden in ("known_value", "value", "default", "fallback"):
            self.assertNotIn(forbidden, unknown)
        self.assertEqual(contract["execution_policies"]["HISTORICAL_STRICT"]["status"],
                         "BLOCKED_HISTORICAL_UNKNOWN")
        self.assertEqual(contract["execution_policies"]["SYNTHETIC_PARAMETERIZED"]["requires"],
                         ["explicit_word2_presence", "u16_word2", "explicit_runtime_samples"])
        self.assertFalse(contract["hardware_ready"])
        self.assertFalse(contract["historically_exact"])

    def test_execution_contract_models_the_exact_caller_fork(self):
        contract = json.loads(EXECUTION.read_text())
        gate = contract["caller_gate"]
        self.assertEqual(gate["callsite"], "sub_a7089@0xaa441")
        self.assertEqual(gate["selected_callee"], "sub_9eaf9")
        self.assertEqual(gate["selected_condition"]["op"], "OR")
        self.assertEqual([x["id"] for x in gate["source_fields"]],
                         ["caller_local_-0x7a", "nested+0x32c"])
        self.assertEqual(gate["skipped_arm"]["target"], "0xaa446")
        self.assertEqual(gate["skipped_arm"]["hardware_callbacks"], 0)

    def test_runtime_samples_are_keyed_and_not_telemetry_by_default(self):
        contract = json.loads(EXECUTION.read_text())
        samples = contract["runtime_table_samples"]
        self.assertEqual(len(samples), 4)
        self.assertEqual([x["symbol"] for x in samples],
                         ["LPF_TABLE_A0", "LPF_TABLE_B0", "LPF_TABLE_A1", "LPF_TABLE_B1"])
        self.assertEqual([x["chain_index"] for x in samples], [0, 0, 1, 1])
        self.assertEqual([x["offset"] for x in samples], ["0x14a", "0x36a", "0x15a", "0x37a"])
        self.assertTrue(all(x["element_count"] == 1 and x["element_width_bits"] == 16
                            for x in samples))
        self.assertTrue(all(x["telemetry_classification"] == "NOT_TELEMETRY_WITHOUT_PROVEN_CONSUMER"
                            for x in samples))
        self.assertTrue(all(x["destination"]["object_id"] == "sub_9eaf9_stack_scratch"
                            for x in samples))

    def test_callback_expansion_preserves_bulk_table_calls(self):
        contract = json.loads(EXECUTION.read_text())
        accounting = contract["operation_accounting"]
        self.assertEqual(accounting["logical_events"], 136)
        self.assertEqual(accounting["phy_callbacks"], 90)
        self.assertEqual(accounting["table_read_callbacks"], 4)
        self.assertEqual(accounting["table_write_callbacks"], 13)
        self.assertEqual(accounting["callback_records"], 107)
        self.assertEqual([x["element_count"] for x in contract["table_write_batches"]],
                         [1, 1, 1, 1, 8, 8, 8, 8, 8, 8, 1, 16, 16])

    def test_synthetic_scenarios_include_two_word2_values_and_skip_arm(self):
        scenarios = json.loads(SCENARIOS.read_text())
        self.assertEqual(scenarios["historical_word2"], "UNKNOWN")
        word_values = {x["word2"] for x in scenarios["scenarios"]
                       if x["mode"] == "SYNTHETIC_PARAMETERIZED" and x["word2_present"]}
        self.assertEqual(word_values, {"0x0000", "0x7b3c"})
        self.assertTrue(any(x["caller_gate"] == "SKIP" and x["expected_callbacks"] == 0
                            for x in scenarios["scenarios"]))

    def test_generated_contract_and_vector_are_deterministic(self):
        result = subprocess.run(
            ["python3", str(GENERATOR), "--check"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_runner_has_no_normal_source_caller(self):
        callers = []
        for path in (ROOT / "src").glob("*.c"):
            if "ob_phy_acphy_9eaf9_fixture_test_only(" in path.read_text():
                callers.append(path.name)
        self.assertEqual(callers, ["ob_phy_9eaf9.c"])


if __name__ == "__main__":
    unittest.main()

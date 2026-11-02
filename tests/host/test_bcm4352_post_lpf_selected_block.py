import copy
import hashlib
import json
import subprocess
import unittest
from pathlib import Path

from scripts.lower_bcm4352_contract import lower_contract
from scripts.evaluate_bcm4352_contract import evaluate_typed_scenario
from scripts.validate_bcm4352_contract import validate_contract


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "artifacts/contracts/bcm4352-post-lpf-next-selected-block-v1.json"
VECTOR = ROOT / "artifacts/contracts/bcm4352-sub-a7089-selected-event-vector.json"
VECTOR_SHA256 = "a8281047ae73e3ab415ff2ff9e0660f8d14f5c4098bc20c2c0879b950c1c97a2"
C_TEST = ROOT / "tests/host/ob_phy_post_lpf_test"
LPF_TEST = ROOT / "tests/host/ob_phy_test"


class PostLpfSelectedBlockTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT.read_text())
        cls.vector = json.loads(VECTOR.read_text())

    def test_accepted_fixture_block_is_complete_only_to_its_bounded_exit(self):
        contract = self.contract
        self.assertEqual(contract["completion_status"], "COMPLETE_CONDITIONAL")
        self.assertTrue(contract["structurally_complete"])
        self.assertFalse(contract["hardware_ready"])
        self.assertEqual(contract["declared_scope"]["logical_event_count"], 88)
        self.assertIn("before sub_a0be4 call @0xaa412", contract["declared_scope"]["exit"])
        self.assertEqual(len(contract["operations"]), 88)
        self.assertEqual(contract["operations"][0]["site"], "0xa7262")
        self.assertEqual(contract["operations"][-1]["site"], "0xaa404")

    def test_existing_validator_accepts_the_evidence_contract(self):
        self.assertEqual(validate_contract(self.contract, ROOT), [])

    def test_existing_lowerer_materializes_the_selected_block(self):
        lowered = lower_contract(self.contract, ROOT, CONTRACT)
        self.assertEqual(lowered["status"], "LOWERED_COMPLETE")
        self.assertEqual(lowered["lowered_operation_count"], 88)
        self.assertTrue(lowered["full_event_lowering_allowed"])
        self.assertFalse(lowered["channel_ready_proven"])
        self.assertFalse(lowered["hardware_ready"])
        self.assertEqual(lowered["unresolved_symbols"], ["ACTUAL_SUB_B018F_CHANSPEC"])
        self.assertEqual(lowered["synthetic_runtime_dependency_count"], 1)

    def test_typed_ir_evaluates_all_88_events_with_explicit_shadow_state(self):
        lowered = lower_contract(self.contract, ROOT, CONTRACT)
        ir = lowered["typed_ir"]
        models = {}
        for event in self.vector["events"]:
            if event["domain"] not in {"PHY", "RADIO"}:
                continue
            register = int(event["register"], 16)
            key = f"{event['domain']}:{register}"
            if key in models:
                continue
            if event["domain"] == "PHY" and register == 0x019e:
                initial = 0xa540
            elif event["domain"] == "PHY" and register == 0x0140:
                initial = 0x1237
            elif event["domain"] == "PHY" and register == 0x0001:
                initial = 0x5101
            else:
                initial = ((register * 37) ^
                           (0x5a5a if event["domain"] == "RADIO" else 0x1357)) & 0xffff
            models[key] = {"mode": "SHADOW_STATE", "initial": initial}
        trace = evaluate_typed_scenario(
            ir, inputs={}, snapshots={}, register_models=models,
            scenario="SYNTHETIC:POST_LPF_KEY1_ROW63", allow_supported_prefix=False)
        self.assertEqual(len(trace), 88)
        self.assertEqual(trace[0]["kind"], "READ")
        self.assertEqual(trace[7]["kind"], "WRITE_DERIVED")
        self.assertEqual(trace[12]["kind"], "OR")
        self.assertEqual(trace[15]["kind"], "PHYCLK_FGC_DESCRIPTION")
        self.assertEqual(trace[-1]["operation_id"], "a7089-088")
        self.assertNotIn("hardware_ready", trace[-1])

    def test_missing_or_reordered_historical_event_is_rejected(self):
        for mutate in (
            lambda value: value["operations"].pop(24),
            lambda value: value["operations"].reverse(),
        ):
            candidate = copy.deepcopy(self.contract)
            mutate(candidate)
            self.assertTrue(validate_contract(candidate, ROOT))

    def test_fixture_chanspec_is_not_promoted_to_historical_state(self):
        symbols = {item["name"]: item for item in self.contract["symbols"]}
        chanspec = symbols["CONTROLLED_FIXTURE_CHANSPEC"]
        self.assertEqual(chanspec["class"], "CONTROLLED_TEST_INPUT")
        self.assertEqual(chanspec["value"], "0x1001")
        self.assertNotIn("historical_value", chanspec)
        self.assertEqual(self.contract["actual_invocation"]["requested_chanspec"], "UNKNOWN")

    def test_contract_is_exactly_referenced_to_the_accepted_vector(self):
        reference = next(
            item for item in self.contract["immutable_contract_references"]
            if item["path"].endswith("bcm4352-sub-a7089-selected-event-vector.json")
        )
        self.assertEqual(reference["sha256"], VECTOR_SHA256)
        self.assertEqual(self.contract["operations"], self.vector["events"])

    def test_generator_keeps_contract_and_c_vector_deterministic(self):
        subprocess.run(["python3", "scripts/generate_bcm4352_post_lpf_block_contract.py", "--check"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        subprocess.run(["python3", "scripts/generate_bcm4352_post_lpf_vector.py", "--check"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        self.assertEqual(hashlib.sha256(VECTOR.read_bytes()).hexdigest(), VECTOR_SHA256)

    @staticmethod
    def _seed(domain, register):
        if domain == "PHY" and register == 0x019e:
            return 0xa540
        if domain == "PHY" and register == 0x0140:
            return 0x1237
        if domain == "PHY" and register == 0x0001:
            return 0x5101
        return ((register * 37) ^ (0x5a5a if domain == "RADIO" else 0x1357)) & 0xffff

    def _expected_callbacks(self):
        state = {}
        read_results = {}
        callbacks = []

        def read_reg(domain, register):
            key = (domain, register)
            if key not in state:
                state[key] = self._seed(domain, register)
            return state[key]

        def append(kind, domain, event, register, value, width):
            callbacks.append({
                "kind": kind, "domain": domain, "logical_event": event["seq"],
                "source_site": f"0x{int(event['site'], 16):05x}",
                "register": register, "value": value, "width_bits": width,
            })

        for event in self.vector["events"]:
            op, domain = event["operation"], event["domain"]
            if op == "READ":
                register = int(event["register"], 16)
                value = read_reg(domain, register)
                read_results[event["seq"]] = value
                append("R", domain, event, register, value, 16)
            elif op == "WRITE":
                register = int(event["register"], 16)
                value = int(event["value"], 0)
                append("W", domain, event, register, value, 16)
                state[(domain, register)] = value
            elif op == "WRITE_DERIVED":
                register = int(event["register"], 16)
                source = {8: 7, 18: 17, 20: 17}[event["seq"]]
                old = read_results[source]
                value = {8: (old & ~0x7) | 0x4,
                         18: old | 0x4000,
                         20: old & 0xbfff}[event["seq"]]
                append("W", domain, event, register, value, 16)
                state[(domain, register)] = value
            elif op == "RMW":
                register = int(event["register"], 16)
                mask = int(event["mask"], 16)
                if event["seq"] in {87, 88}:
                    value = read_results[1 if event["seq"] == 87 else 2] & mask
                else:
                    value = int(event["value"], 0)
                old = read_reg(domain, register)
                append("R", domain, event, register, old, 16)
                if domain == "PHY":
                    new = (old & (~mask & 0xffff)) | (value & mask)
                else:
                    new = (old & (~value & 0xffff)) | (value & mask)
                append("W", domain, event, register, new, 16)
                state[(domain, register)] = new
            elif op == "OR":
                register = int(event["register"], 16)
                value = int(event["value"], 0)
                old = read_reg(domain, register)
                append("R", domain, event, register, old, 16)
                new = old | value
                append("W", domain, event, register, new, 16)
                state[(domain, register)] = new
            elif op == "DELAY_US":
                callbacks.append({"kind": "D", "domain": "CONTROL",
                                  "logical_event": event["seq"],
                                  "source_site": f"0x{int(event['site'], 16):05x}",
                                  "register": 0, "value": int(event["duration_us"]),
                                  "width_bits": 0})
            elif op == "PHYCLK_FGC":
                callbacks.append({"kind": "F", "domain": "CONTROL",
                                  "logical_event": event["seq"],
                                  "source_site": f"0x{int(event['site'], 16):05x}",
                                  "register": 0, "value": int(event["value"]),
                                  "width_bits": 0})
            else:
                raise AssertionError(f"unhandled accepted operation {op}")
        return callbacks

    def test_c_fake_backend_trace_matches_independent_vector_expansion(self):
        subprocess.run(["make", "-C", "tests/host", "ob_phy_post_lpf_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        first = subprocess.run([str(C_TEST), "--emit-trace"], cwd=ROOT,
                               check=True, capture_output=True, text=True).stdout
        second = subprocess.run([str(C_TEST), "--emit-trace"], cwd=ROOT,
                                check=True, capture_output=True, text=True).stdout
        self.assertEqual(first, second)
        actual = json.loads(first)
        self.assertEqual(actual["scenario"], "SYNTHETIC_CONTROLLED_CHANSPEC_0x1001_KEY1_ROW63")
        self.assertEqual(actual["logical_event_count"], 88)
        self.assertEqual(actual["callback_count"], 109)
        self.assertFalse(actual["hardware_ready"])
        self.assertEqual(actual["events"], self._expected_callbacks())

    def test_composed_lpf_caller_and_selected_block_trace_has_exact_join(self):
        subprocess.run(["make", "-C", "tests/host", "ob_phy_test", "ob_phy_post_lpf_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        lpf = json.loads(subprocess.run(
            [str(LPF_TEST), "--emit-lpf-trace"], cwd=ROOT,
            check=True, capture_output=True, text=True).stdout)
        selected = json.loads(subprocess.run(
            [str(C_TEST), "--emit-trace"], cwd=ROOT,
            check=True, capture_output=True, text=True).stdout)
        composed = json.loads(subprocess.run(
            [str(C_TEST), "--emit-composed-trace"], cwd=ROOT,
            check=True, capture_output=True, text=True).stdout)
        self.assertEqual(composed["scenario"],
                         "SYNTHETIC_LPF_CALLER_STORES_A7089_FIXTURE")
        self.assertEqual(composed["callback_count"], 3845)
        self.assertEqual(composed["lpf_callback_count"], 3736)
        self.assertEqual(composed["selected_logical_event_count"], 88)
        self.assertEqual(composed["selected_callback_count"], 109)
        self.assertFalse(composed["hardware_ready"])
        prefix = composed["events"][:3736]
        selected_suffix = composed["events"][3736:]
        self.assertEqual(len(prefix), lpf["callback_count"])
        self.assertEqual(len(selected_suffix), selected["callback_count"])
        self.assertEqual(
            [{key: event[key] for key in ("kind", "register", "value")}
             for event in prefix],
            [{key: event[key] for key in ("kind", "register", "value")}
             for event in lpf["events"]],
        )
        self.assertEqual(selected_suffix, selected["events"])
        self.assertEqual(prefix[-1]["register"], 0x1645)
        self.assertEqual(prefix[-1]["value"], 0x025c)
        self.assertEqual(selected_suffix[0]["source_site"], "0xa7262")
        self.assertEqual(selected_suffix[0]["register"], 0x019e)

    def test_source_remains_isolated_from_normal_probe(self):
        users = []
        for path in (ROOT / "src").glob("*.[ch]"):
            if "ob_phy_acphy_a7089_key1_row63_test_only" in path.read_text():
                users.append(path.name)
        self.assertEqual(sorted(users), ["ob_phy_post_lpf.c", "ob_phy_post_lpf.h"])


if __name__ == "__main__":
    unittest.main()

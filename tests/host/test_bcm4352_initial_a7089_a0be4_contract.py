import copy
import csv
import hashlib
import io
import json
import subprocess
import unittest
from pathlib import Path

from scripts.validate_bcm4352_contract import validate_contract


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = ROOT / "artifacts/contracts/bcm4352-initial-a7089-through-a0be4-v1.json"
VECTOR_PATH = ROOT / "artifacts/contracts/bcm4352-sub-a0be4-selected-event-vector.json"
VECTOR_SHA256 = "6cc7d653badb1997df993368772700a2bc5d5be34984c4e0f4b5ad354c2ee2d1"
C_TEST = ROOT / "tests/host/ob_phy_a0be4_test"
PREFIX_C_TEST = ROOT / "tests/host/ob_phy_post_lpf_test"


class InitialA7089A0be4ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT_PATH.read_text())

    def test_controlled_composition_is_valid_and_not_hardware_ready(self):
        self.assertEqual(validate_contract(self.contract, ROOT), [])
        self.assertFalse(self.contract["hardware_ready"])
        self.assertEqual(self.contract["completion_status"], "COMPLETE_CONDITIONAL")
        self.assertEqual(self.contract["declared_scope"]["logical_events"], 3862)
        self.assertEqual(self.contract["declared_scope"]["callback_events"], 3912)

    def test_actual_chanspec_and_lookup_are_not_fixture_values(self):
        self.assertEqual(self.contract["actual_initial_selection"]["pi_17e_value"], "UNKNOWN")
        self.assertEqual(self.contract["actual_initial_selection"]["lookup_key_and_row"], "UNKNOWN")
        fixture = self.contract["controlled_scenario"]
        self.assertEqual(fixture["classification"], "CONTROLLED_TEST_INPUT")
        self.assertEqual(fixture["pi_17e_chanspec"], "0x1001")
        self.assertEqual(fixture["lookup"], {"key": 1, "matched": True, "row": 63})

    def test_nested_912_is_revision_derived_for_the_admitted_acphy_target(self):
        nested = self.contract["nested_912"]
        self.assertEqual(nested["producer_site"], "wlc_phy_attach_acphy@0xa1c37")
        self.assertEqual(nested["predicate_ast"]["op"], "UNSIGNED_LE")
        self.assertEqual(nested["predicate_ast"]["lhs"]["offset"], "0x164")
        self.assertEqual(nested["predicate_ast"]["rhs"]["value"], 1)
        self.assertEqual(nested["target_reaching_value"], 0)
        self.assertEqual(nested["classification"], "DETERMINISTIC_SOFTWARE_STATE")

    def test_callback_counts_and_terminal_boundary_are_explicit(self):
        self.assertEqual(self.contract["components"]["sub_a04c2"]["callback_events"], 3736)
        self.assertEqual(self.contract["components"]["sub_a7089"]["callback_events"], 109)
        self.assertEqual(self.contract["components"]["sub_a0be4"]["logical_events"], 40)
        self.assertEqual(self.contract["components"]["sub_a0be4"]["callback_events"], 67)
        self.assertEqual(self.contract["declared_scope"]["terminal_boundary"], "sub_a0be4 return @0xa13eb")
        self.assertEqual(self.contract["accepted_next_continuation"]["logical_events"], 212)
        self.assertEqual(self.contract["accepted_next_continuation"]["first_hardware_event"],
                         "PHY READ[0x019e] @0xa6b3c")
        self.assertEqual(self.contract["accepted_next_continuation"]["callback_events"], "NOT_MATERIALIZED")

    def test_rejects_callback_count_drift_and_fixture_promotion(self):
        changed = copy.deepcopy(self.contract)
        changed["declared_scope"]["callback_events"] -= 1
        self.assertTrue(validate_contract(changed, ROOT))

        changed = copy.deepcopy(self.contract)
        changed["actual_initial_selection"]["pi_17e_value"] = "0x1001"
        self.assertTrue(validate_contract(changed, ROOT))

    def test_rejects_a_missing_a0be4_reference(self):
        changed = copy.deepcopy(self.contract)
        changed["immutable_contract_references"] = [
            item for item in changed["immutable_contract_references"]
            if "sub-a0be4-selected-event-vector" not in item["path"]
        ]
        self.assertTrue(validate_contract(changed, ROOT))

    def test_rejects_false_nested_selector_provenance(self):
        changed = copy.deepcopy(self.contract)
        changed["nested_912"]["classification"] = "RUNTIME_HARDWARE_SAMPLE"
        self.assertTrue(validate_contract(changed, ROOT))

    def test_c_trace_matches_directly_pinned_a0be4_source_vector(self):
        raw = VECTOR_PATH.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), VECTOR_SHA256)
        vector = json.loads(raw)
        output = subprocess.run(
            [str(C_TEST), "--dump-a0be4"], check=True, capture_output=True, text=True
        ).stdout
        actual = [tuple(row) for row in csv.reader(io.StringIO(output))]
        self.assertEqual(len(actual), 67)
        self.assertEqual(actual, self._expected_callback_trace(vector["events"]))

    def test_composed_c_trace_is_exact_prefix_plus_accepted_a0be4_trace(self):
        raw = VECTOR_PATH.read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), VECTOR_SHA256)
        vector = json.loads(raw)
        actual_output = subprocess.run(
            [str(C_TEST), "--dump-composed-a0be4"], check=True,
            capture_output=True, text=True
        ).stdout
        actual = [tuple(row) for row in csv.reader(io.StringIO(actual_output))]

        prefix_json = json.loads(subprocess.run(
            [str(PREFIX_C_TEST), "--emit-composed-trace"], check=True,
            capture_output=True, text=True
        ).stdout)
        prefix = [
            (event["kind"], event["domain"], str(event["logical_event"]),
             event["source_site"], f"0x{event['register']:04x}",
             f"0x{event['value']:08x}", str(event["width_bits"]))
            for event in prefix_json["events"]
        ]
        prefix_state = {
            (event["domain"], event["register"]): event["value"]
            for event in prefix_json["events"]
            if event["kind"] == "W" and event["domain"] in {"PHY", "RADIO"}
        }
        a0be4 = self._expected_callback_trace(vector["events"], prefix_state)
        self.assertEqual(len(actual), 3912)
        self.assertEqual(len(prefix), 3845)
        self.assertEqual(actual, prefix + a0be4)
        self.assertEqual(actual[3844][3], "0xaa404")
        self.assertEqual(actual[3845][3], "0xa0c00")
        self.assertEqual(actual[-1][3], "0xa13da")

    @staticmethod
    def _expected_callback_trace(events, initial_state=None):
        state = dict(initial_state or {})
        trace = []
        resetcca_snapshot = None

        def reg_key(event):
            if event["operation"] == "PHYCLK_FGC" or event["domain"] == "DELAY":
                return "CONTROL", 0, 0
            if event["domain"] == "BCMA":
                return "AI_CORE", 0x0408, 32
            return event["domain"], int(event["register"], 0), event["width_bits"]

        def current(domain, register, width):
            key = (domain, register)
            if key not in state:
                if domain == "PHY" and register == 0x019e:
                    seed = 0xa540
                elif domain == "PHY" and register == 0x0140:
                    seed = 0x1237
                elif domain == "PHY" and register == 0x0001:
                    seed = 0x5101
                elif domain == "PHY" and register == 0x173a:
                    seed = 0x0201
                elif domain == "PHY" and register == 0x1725:
                    seed = 0x0005
                elif domain == "AI_CORE" and register == 0x0408:
                    seed = 0xa5a50001
                else:
                    salt = 0x5a5a if domain == "RADIO" else 0x1357
                    seed = register * 37 ^ salt
                state[key] = seed & ((1 << width) - 1)
            return state[key]

        def emit(kind, domain, event, register, value, width):
            trace.append((kind, domain, str(event["seq"]),
                          f"0x{int(event['site'], 0):05x}",
                          f"0x{register:04x}", f"0x{value:08x}", str(width)))

        for event in events:
            operation = event["operation"]
            domain, register, width = reg_key(event)
            if operation == "OR":
                old = current(domain, register, width)
                emit("R", domain, event, register, old, width)
                value = old | int(event["value"], 0)
                emit("W", domain, event, register, value, width)
                state[(domain, register)] = value & ((1 << width) - 1)
            elif operation == "WRITE":
                value = int(event["value"], 0)
                emit("W", domain, event, register, value, width)
                state[(domain, register)] = value & ((1 << width) - 1)
            elif operation == "RMW":
                old = current(domain, register, width)
                mask = int(event["mask"], 0)
                value = int(event["value"], 0)
                emit("R", domain, event, register, old, width)
                if domain == "AI_CORE":
                    new = (old & ~mask) | value
                else:
                    new = (old & ~mask) | (value & mask)
                new &= (1 << width) - 1
                emit("W", domain, event, register, new, width)
                state[(domain, register)] = new
                if domain == "AI_CORE":
                    emit("R", domain, event, register, new, width)
            elif operation == "READ":
                value = current(domain, register, width)
                emit("R", domain, event, register, value, width)
                resetcca_snapshot = value
            elif operation == "WRITE_DERIVED":
                if resetcca_snapshot is None:
                    raise AssertionError("derived reset-CCA write has no accepted prior snapshot")
                if "|" in event["value"]:
                    value = resetcca_snapshot | int(event["value"].rsplit("|", 1)[1], 0)
                else:
                    value = resetcca_snapshot & int(event["value"].rsplit("&", 1)[1], 0)
                emit("W", domain, event, register, value, width)
                state[(domain, register)] = value & ((1 << width) - 1)
            elif operation == "DELAY_US":
                emit("D", "CONTROL", event, 0, event["duration_us"], 0)
            elif operation == "PHYCLK_FGC":
                emit("F", "CONTROL", event, 0, int(event["value"]), 0)
            else:
                raise AssertionError(f"unsupported accepted operation {operation}")
        return trace


if __name__ == "__main__":
    unittest.main()

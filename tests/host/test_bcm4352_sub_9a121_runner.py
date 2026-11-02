#!/usr/bin/env python3
"""Independent pinned-binary and contract checks for the bounded 9a121 runner."""
import hashlib
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "artifacts/contracts/bcm4352-sub-9a121-cleanroom-execution-v1.json"
RETURN = ROOT / "artifacts/contracts/bcm4352-sub-9af05-selected-return-and-state-v1.json"
ABI = ROOT / "artifacts/contracts/bcm4352-sub-99658-table-api-abi-correction-v1.json"
LOOP = ROOT / "artifacts/contracts/bcm4352-sub-9af05-loop-continuation-v1.json"
SOURCE = Path("/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/vendor/wlc_hybrid.o_shipped")
SOURCE_SHA = "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"


class Sub9a121ContractTests(unittest.TestCase):
    def test_contract_pins_entry_gates_prefix_and_exact_stop(self):
        data = json.loads(CONTRACT.read_text())
        self.assertEqual(data["binary_sha256"], SOURCE_SHA)
        self.assertEqual(data["entry"], "sub_9a121 @0x9a121")
        self.assertEqual(data["caller"]["callsite"], "sub_a7089 @0xaa50e")
        self.assertEqual(data["first_hardware_event"], {
            "site": "0x9a1af", "domain": "PHY", "operation": "READ16",
            "register": "0x019e", "unconditional": True,
        })
        self.assertEqual(data["supported_scopes"]["zero_active_core_mask"]["status"],
                         "COMPLETE_SYNTHETIC_SCENARIO")
        self.assertEqual(data["supported_scopes"]["active_core_mask"]["first_unsupported_site"],
                         "sub_99f29 @0x9a319")
        self.assertFalse(data["hardware_ready"])
        self.assertEqual(data["return_site"], "0x9a538")
        self.assertEqual(data["next_caller_step"]["callsite"], "sub_a7089 @0xaa51f")
        self.assertEqual(data["next_caller_step"]["callee"], "sub_9a539")

    def test_contract_distinguishes_runtime_discard_and_consumed_reads(self):
        data = json.loads(CONTRACT.read_text())
        reads = {event["site"]: event for event in data["per_core_prefix"] + data["downstream_runtime_reads_after_nested_children"]}
        self.assertEqual(reads["0x9a304"]["value_use"], "DISCARDED")
        self.assertEqual(reads["0x9a3b4"]["value_use"], "RUNTIME_DEPENDENCY_FOR_LATER_NESTED_CALLS")
        self.assertEqual(reads["0x9a42c"]["value_use"], "RUNTIME_DEPENDENCY_FOR_LATER_NESTED_CALLS")
        self.assertEqual(data["first_unsupported_operation"], {
            "site": "0x9a319", "callee": "sub_99f29",
            "reason": "selected nested helper implementation is outside the bounded C scope",
        })

    def test_return_contract_proves_only_the_declared_chain1_gate_scenario(self):
        data = json.loads(RETURN.read_text())
        self.assertEqual(data["terminal"], "sub_9af05 return @0x9b751")
        self.assertTrue(data["selected_chain1_scenario"]["required_gates"]["caller_flag_at_0x9b419"])
        self.assertEqual(data["caller_rejoin"], {
            "return_address": "0xaa50b", "next_callsite": "0xaa50e",
            "next_callee": "sub_9a121", "arguments": ["PI"], "predicate": "unconditional",
        })
        self.assertFalse(data["universal_sub_9af05_closure"])

    def test_corrected_table_abi_predecessor_remains_pinned(self):
        data = json.loads(ABI.read_text())
        for path, digest in data["immutable_predecessors"].items():
            self.assertEqual(hashlib.sha256((ROOT / path).read_bytes()).hexdigest(), digest)

    def test_return_contract_source_hashes_are_immutable(self):
        data = json.loads(RETURN.read_text())
        for item in data["source_contracts"]:
            self.assertEqual(hashlib.sha256((ROOT / item["path"]).read_bytes()).hexdigest(),
                             item["sha256"], item["path"])

    def test_accepted_4380_4408_callback_projections_remain_exact(self):
        data = json.loads(LOOP.read_text())
        composition = data["composition"]
        self.assertEqual(composition["accepted_predecessor_prefix_callbacks"], [4356, 4384])
        self.assertEqual(composition["ten_call_table_stage_callbacks"], 10)
        self.assertEqual(composition["outer_loop_callbacks"], 14)
        self.assertEqual(composition["projected_totals"], [4380, 4408])

    def test_vendor_identity_is_stable(self):
        self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), SOURCE_SHA)


if __name__ == "__main__":
    unittest.main()

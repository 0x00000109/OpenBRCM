import copy
import hashlib
import json
import os
import tempfile
import unittest
from pathlib import Path

from scripts.validate_bcm4352_contract import validate_contract


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "artifacts/contracts/bcm4352-postcrs-channel-lifecycle-v1.json"


class PostCrsContractValidatorTests(unittest.TestCase):
    def setUp(self):
        self.contract = json.loads(CONTRACT.read_text())

    def test_current_partial_contract_is_valid(self):
        self.assertEqual(validate_contract(self.contract, ROOT), [])

    def test_rejects_implicit_table14_word2_zero(self):
        changed = copy.deepcopy(self.contract)
        word = next(s for s in changed["symbols"] if s["name"] == "TABLE14_OFFSET33_WORD2")
        word["value"] = "0x0000"
        self.assertTrue(any("implicit historical value" in error for error in validate_contract(changed, ROOT)))

    def test_rejects_incomplete_contract_claimed_complete(self):
        changed = copy.deepcopy(self.contract)
        changed["structurally_complete"] = True
        self.assertTrue(any("unknown boundary" in error for error in validate_contract(changed, ROOT)))

    def test_rejects_channel_ready_inferred_from_callback_return(self):
        changed = copy.deepcopy(self.contract)
        changed["completion_boundaries"]["channel_operational_ready"] = "PROVEN_AT_CALLBACK_RETURN"
        self.assertTrue(any("callback return cannot establish channel readiness" in error for error in validate_contract(changed, ROOT)))

    def test_rejects_unmatched_row_as_successful_commit(self):
        changed = copy.deepcopy(self.contract)
        row = next(b for b in changed["branch_model"] if b["id"] == "channel-row")
        row["unmatched_arm_status"] = "SUCCESSFUL_COMMIT"
        self.assertTrue(any("unmatched channel row" in error for error in validate_contract(changed, ROOT)))

    def test_requires_external_configuration_for_8b0(self):
        changed = copy.deepcopy(self.contract)
        axis = next(s for s in changed["symbols"] if s["name"] == "nested+0x8b0")
        axis["class"] = "RUNTIME_HARDWARE_SAMPLE"
        self.assertTrue(any("must remain external configuration" in error for error in validate_contract(changed, ROOT)))

    def test_rejects_discarded_sample_entering_telemetry(self):
        changed = copy.deepcopy(self.contract)
        changed["discarded_reads"][0]["telemetry_eligible"] = True
        self.assertTrue(any("cannot enter telemetry" in error for error in validate_contract(changed, ROOT)))

    def test_requires_callback_and_row_predicates_for_first_write(self):
        changed = copy.deepcopy(self.contract)
        operation = next(o for o in changed["operations"] if o["sequence_id"] == "postcrs-008")
        operation["source_site"] = "0xa753d"
        operation["branch_condition"] = "always"
        self.assertTrue(any("must remain conditional" in error for error in validate_contract(changed, ROOT)))

    def test_rejects_single_event_claimed_structurally_complete_and_hardware_ready(self):
        changed = copy.deepcopy(self.contract)
        changed.update(operations=[changed["operations"][0]], structurally_complete=True,
                      completion_status="COMPLETE_PARAMETERIZED", hardware_ready=True,
                      branch_model=[], immutable_contract_references=[])
        self.assertTrue(validate_contract(changed, ROOT))

    def test_requires_independent_row_stream_assertion(self):
        changed = copy.deepcopy(self.contract)
        changed["operations"] = [o for o in changed["operations"] if o["sequence_id"] != "postcrs-008"]
        self.assertTrue(any("row stream" in e.lower() for e in validate_contract(changed, ROOT)))

    def test_rejects_sorted_but_gapped_linear_sequence(self):
        changed = copy.deepcopy(self.contract)
        for operation in changed["operations"]:
            number = int(operation["sequence_id"].split("-")[-1])
            if number >= 9:
                operation["sequence_id"] = f"postcrs-{number + 1:03d}"
        self.assertTrue(any("contiguous" in e.lower() for e in validate_contract(changed, ROOT)))

    def test_rejects_callback_and_matched_words_on_unmatched_arm(self):
        changed = copy.deepcopy(self.contract)
        operation = next(o for o in changed["operations"] if o["sequence_id"] == "postcrs-008")
        operation["branch_condition"] = "callback selected AND matched row, but this arm is unmatched/skipped"
        self.assertTrue(validate_contract(changed, ROOT))

    def test_rejects_historical_unknown_default_or_fallback(self):
        for field in ("default", "fallback"):
            changed = copy.deepcopy(self.contract)
            word = next(s for s in changed["symbols"] if s["name"] == "TABLE14_OFFSET33_WORD2")
            word[field] = 0
            self.assertTrue(validate_contract(changed, ROOT), field)

    def test_rejects_hardware_ready_without_proven_postcondition(self):
        changed = copy.deepcopy(self.contract)
        changed["hardware_ready"] = True
        self.assertTrue(validate_contract(changed, ROOT))

    def test_self_asserted_readiness_provenance_is_not_an_admitted_postcondition(self):
        changed = copy.deepcopy(self.contract)
        changed["hardware_ready"] = True
        changed["readiness_postcondition"] = {"status": "PROVEN", "provenance": "self-asserted fixture"}
        self.assertTrue(any("no admitted BCM4352 readiness postcondition" in e for e in validate_contract(changed, ROOT)))

    def test_rejects_unsupported_rfpll_lock_claim(self):
        changed = copy.deepcopy(self.contract)
        changed["completion_boundaries"]["radio_synthesizer_ready"] = "PROVEN_RFPLL_LOCK"
        self.assertTrue(validate_contract(changed, ROOT))

    def test_rejects_absolute_reference_even_when_file_hash_matches(self):
        changed = copy.deepcopy(self.contract)
        outside = Path("/etc/hosts")
        changed["immutable_contract_references"].append({
            "path": str(outside), "sha256": hashlib.sha256(outside.read_bytes()).hexdigest()
        })
        self.assertTrue(validate_contract(changed, ROOT))

    def test_malformed_operation_returns_errors_not_traceback(self):
        changed = copy.deepcopy(self.contract)
        changed["operations"] = [None]
        errors = validate_contract(changed, ROOT)
        self.assertTrue(any("operation" in e.lower() and "object" in e.lower() for e in errors))

    def test_complete_branch_requires_resolved_arms_and_rejoin(self):
        changed = copy.deepcopy(self.contract)
        branch = next(b for b in changed["branch_model"] if b["id"] == "channel-row")
        branch["complete"] = True
        branch["common_rejoin"] = "0xdead"
        self.assertTrue(validate_contract(changed, ROOT))

    def test_rejects_unknown_symbol_reference(self):
        changed = copy.deepcopy(self.contract)
        changed["operations"][0]["value_or_symbol"] = {"symbol": "ABSENT_SYMBOL"}
        self.assertTrue(validate_contract(changed, ROOT))

    def test_rejects_row_stream_literal_replacing_symbolic_words(self):
        changed = copy.deepcopy(self.contract)
        stream = next(o for o in changed["operations"] if o["sequence_id"] == "postcrs-008")
        stream["value_or_symbol"] = "0x1234"
        self.assertTrue(validate_contract(changed, ROOT))

    def test_rejects_parent_traversal_and_symlink_reference_escapes(self):
        changed = copy.deepcopy(self.contract)
        changed["immutable_contract_references"] = [{"path": "../README.md", "sha256": "0" * 64}]
        self.assertTrue(any("escapes repository root" in e for e in validate_contract(changed, ROOT)))
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            link = temp_root / "outside"
            os.symlink("/etc/hosts", link)
            changed["immutable_contract_references"] = [{"path": "outside", "sha256": hashlib.sha256(Path("/etc/hosts").read_bytes()).hexdigest()}]
            self.assertTrue(any("symlink" in e for e in validate_contract(changed, temp_root)))

    def test_rejects_reference_hash_mismatch_and_unpinned_execution_critical_input(self):
        changed = copy.deepcopy(self.contract)
        changed["immutable_contract_references"] = [{"path": "README.md", "sha256": "0" * 64}]
        self.assertTrue(any("hash mismatch" in e for e in validate_contract(changed, ROOT)))
        changed["immutable_contract_references"] = [{"path": "README.md", "sha256": "not-a-sha256"}]
        self.assertTrue(any("invalid SHA-256" in e for e in validate_contract(changed, ROOT)))
        changed["immutable_contract_references"] = [{"path": "missing-contract.json", "sha256": "0" * 64}]
        self.assertTrue(any("missing provenance artifact" in e for e in validate_contract(changed, ROOT)))
        changed["immutable_contract_references"] = [{"path": "README.md", "execution_critical": True}]
        self.assertTrue(any("requires exact SHA-256" in e for e in validate_contract(changed, ROOT)))

    def test_expanded_reference_checks_declared_completion_and_boundary(self):
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            referenced = {"structurally_complete": True, "completion_status": "COMPLETE_PARAMETERIZED",
                          "declared_scope": {"exit": "boundary:end"}, "completion_evidence": {"end_boundary": "boundary:end"},
                          "immutable_contract_references": []}
            path = temp_root / "referenced.json"
            path.write_text(json.dumps(referenced))
            reference = {"path": "referenced.json", "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                         "expand": True, "execution_critical": True, "requires_structurally_complete": True,
                         "required_boundary": "boundary:end"}
            changed = copy.deepcopy(self.contract)
            changed["immutable_contract_references"] = [reference]
            self.assertFalse(any("referenced contract" in e for e in validate_contract(changed, temp_root)))
            reference["required_boundary"] = "boundary:wrong"
            self.assertTrue(any("boundary does not match" in e for e in validate_contract(changed, temp_root)))
            referenced["structurally_complete"] = False
            path.write_text(json.dumps(referenced))
            reference.update(required_boundary="boundary:end", sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertTrue(any("required complete boundary" in e for e in validate_contract(changed, temp_root)))

    def test_malformed_roots_branches_and_operation_values_return_validation_errors(self):
        self.assertTrue(validate_contract([], ROOT))
        changed = copy.deepcopy(self.contract)
        changed["operations"] = [{"sequence_id": [], "source_function": [], "source_site": {},
                                  "operation_type": [], "hardware_domain": [], "register_or_table": [],
                                  "mask": {}, "value_or_symbol": [], "branch_condition": [],
                                  "provenance": [], "confidence_status": [], "execution_scope": []}]
        self.assertTrue(validate_contract(changed, ROOT))
        changed = copy.deepcopy(self.contract)
        changed["branch_model"] = [{"id": "incomplete", "arms": ["taken"], "complete": True}]
        self.assertTrue(validate_contract(changed, ROOT))

    def test_known_symbol_reference_remains_valid_and_unknown_symbol_is_rejected(self):
        changed = copy.deepcopy(self.contract)
        changed["operations"][0]["value_or_symbol"] = {"symbol": "STORED_CHANSPEC"}
        self.assertFalse(any("unknown symbol" in error for error in validate_contract(changed, ROOT)))
        changed["operations"][0]["value_or_symbol"] = {"symbol": "NOT_IN_SYMBOL_TABLE"}
        self.assertTrue(any("unknown symbol" in error for error in validate_contract(changed, ROOT)))


if __name__ == "__main__":
    unittest.main()

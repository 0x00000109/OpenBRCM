import copy
import json
import re
import unittest
from pathlib import Path

from scripts.validate_bcm4352_contract import validate_contract


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = ROOT / "artifacts/contracts/bcm4352-next-post-pmu-gpio-block-v1.json"


class NextPostPmuContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT_PATH.read_text())

    def test_selected_post_stop_prefix_contract_validates(self):
        self.assertEqual(validate_contract(self.contract, ROOT), [])
        self.assertEqual(self.contract["completion_status"], "COMPLETE_PARAMETERIZED")
        self.assertTrue(self.contract["structurally_complete"])
        self.assertFalse(self.contract["hardware_ready"])
        self.assertEqual(len(self.contract["operations"]), 20)
        self.assertEqual(len(self.contract["runtime_inputs"]), 3)

    def test_missing_or_changed_event_fails_independent_source_assertion(self):
        for mutate in (lambda c: c["operations"].pop(),
                       lambda c: c["operations"][0].update(register="0x0411"),
                       lambda c: c["operations"][10].update(value="0x4080")):
            candidate = copy.deepcopy(self.contract)
            mutate(candidate)
            with self.subTest(candidate=candidate["operations"][:1]):
                self.assertTrue(validate_contract(candidate, ROOT))

    def test_reference_hash_path_and_readiness_claim_fail_closed(self):
        cases = []
        candidate = copy.deepcopy(self.contract)
        candidate["immutable_contract_references"][0]["sha256"] = "0" * 64
        cases.append(candidate)
        candidate = copy.deepcopy(self.contract)
        candidate["immutable_contract_references"][0]["path"] = "../escaped.json"
        cases.append(candidate)
        candidate = copy.deepcopy(self.contract)
        candidate["hardware_ready"] = True
        cases.append(candidate)
        for candidate in cases:
            with self.subTest(candidate=candidate.get("hardware_ready", candidate.get("immutable_contract_references"))):
                self.assertTrue(validate_contract(candidate, ROOT))

    def test_unselected_branch_or_rmw_semantics_are_rejected(self):
        wrong_predicate = copy.deepcopy(self.contract)
        wrong_predicate["selected_predicate"]["arm"] = "PHY_REVISION_EQ_0x03"
        wrong_rmw = copy.deepcopy(self.contract)
        wrong_rmw["rmw_semantics"][0]["semantic_mode"] = "CLEAR_VALUE_THEN_MASKED_INSERT"
        for candidate in (wrong_predicate, wrong_rmw):
            self.assertTrue(validate_contract(candidate, ROOT))

    def test_missing_runtime_sample_or_synthetic_read_default_is_rejected(self):
        missing = copy.deepcopy(self.contract)
        missing["runtime_inputs"].pop()
        self.assertTrue(validate_contract(missing, ROOT))
        unknown_default = copy.deepcopy(self.contract)
        unknown_default["historical_semantic_unknowns"] = [{"name": "R_A04C2_019E", "default": 0}]
        self.assertTrue(validate_contract(unknown_default, ROOT))

    def test_offline_prelude_runner_has_no_normal_c_dispatch_caller(self):
        name = "ob_phy_acphy_lpf_register_prefix_test_only"
        callers = []
        for source in (ROOT / "src").glob("*.c"):
            if re.search(rf"\b{name}\s*\(", source.read_text()):
                callers.append(source.relative_to(ROOT).as_posix())
        self.assertEqual(callers, [])


if __name__ == "__main__":
    unittest.main()

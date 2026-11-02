import copy
import hashlib
import json
import unittest
from pathlib import Path

from scripts.bcm4352_contract_ir import (
    HISTORICAL_RMW_HELPERS,
    OPERATION_KINDS,
    SCHEMA,
    apply_rmw,
    evaluate_predicate,
    evaluate_table_transaction,
    validate_typed_ir,
)
from scripts.evaluate_bcm4352_contract import compare_traces, evaluate_scenario, evaluate_typed_scenario
from scripts.lower_bcm4352_contract import lower_contract, lower_operation


ROOT = Path(__file__).resolve().parents[2]
REAL_CONTRACT = ROOT / "artifacts/contracts/bcm4352-postcrs-channel-lifecycle-v1.json"
LOWERING_RESULT = ROOT / "tests/fixtures/bcm4352-postcrs-channel-lifecycle-v1.lowering-result.json"
ORACLES = ROOT / "tests/fixtures/bcm4352-contract-engine-oracles.json"
THROUGH_CRS_HASHES = ROOT / "tests/fixtures/bcm4352-through-crs-contract-sha256-baseline.json"
TYPED_IR_SCHEMA = ROOT / "schemas/bcm4352-typed-event-ir-v1.schema.json"


def program(operations, symbols=None, status="LOWERED_COMPLETE"):
    first_id = operations[0].get("id") if operations and isinstance(operations[0], dict) else "op-000"
    last_id = operations[-1].get("id") if operations and isinstance(operations[-1], dict) else "op-end"
    return {
        "schema": "openbrcm-bcm4352-typed-event-ir/v1",
        "target": {"chip": "BCM4352"},
        "lowering_status": status,
        "declared_scope": {"entry": "synthetic-test-entry", "exit": "synthetic-test-exit"},
        "completion": {"status": "COMPLETE_PARAMETERIZED", "structurally_complete": True,
                       "hardware_ready": False, "readiness_postcondition": None},
        "symbols": symbols or [],
        "control_flow": {"entry": first_id, "exit": last_id, "edges": []},
        "operations": operations,
    }


def op(op_id, kind, **fields):
    return {"id": op_id, "kind": kind, "source_function": "synthetic",
            "source_site": "synthetic:0", "provenance": {"class": "SYNTHETIC_FIXTURE"},
            "condition": None, **fields}


class TypedContractEngineTests(unittest.TestCase):
    def test_validator_api_exists_and_rejects_non_object_root(self):
        self.assertTrue(validate_typed_ir([]))

    def test_structured_callback_selection_and_row_branches(self):
        symbols = {
            "lifecycle": {"value_type": "u8", "width_bits": 8, "signed": False},
            "requested": {"value_type": "u16", "width_bits": 16, "signed": False},
            "current": {"value_type": "u16", "width_bits": 16, "signed": False},
        }
        predicate = {"op": "AND", "args": [
            {"op": "NE", "left": {"symbol": "lifecycle"}, "right": {"const": {"value_type": "u8", "width_bits": 8, "signed": False, "value": 0}}},
            {"op": "NE", "left": {"symbol": "requested"}, "right": {"symbol": "current"}},
        ]}
        self.assertTrue(evaluate_predicate(predicate, symbols, {"lifecycle": 1, "requested": 0x1001, "current": 0x1000}))
        self.assertFalse(evaluate_predicate(predicate, symbols, {"lifecycle": 0, "requested": 0x1001, "current": 0x1000}))
        self.assertFalse(evaluate_predicate(predicate, symbols, {"lifecycle": 1, "requested": 1, "current": 1}))

    def test_duplicate_ids_dangling_edges_and_unknown_symbols_are_rejected(self):
        ir = program([op("op-000", "WRITE", domain="PHY", register="0x1", width_bits=16,
                          value={"symbol": "missing"}),
                      op("op-000", "RETURN_BOUNDARY", boundary="end")])
        ir["control_flow"]["edges"] = [{"from": "op-000", "to": "absent"}]
        errors = validate_typed_ir(ir)
        self.assertTrue(any("duplicate" in e.lower() for e in errors))
        self.assertTrue(any("symbol" in e.lower() for e in errors))
        self.assertTrue(any("edge" in e.lower() for e in errors))

    def test_branch_requires_complete_arms_and_valid_rejoin(self):
        branch = op("op-000", "BRANCH", predicate={"op": "EQ", "left": {"const": {"value_type": "u8", "width_bits": 8, "value": 0}},
                                                       "right": {"const": {"value_type": "u8", "width_bits": 8, "value": 0}}},
                    arms=[{"id": "zero", "target": "op-001"}], rejoin="missing")
        ir = program([branch, op("op-001", "RETURN_BOUNDARY", boundary="end")])
        self.assertTrue(validate_typed_ir(ir))

    def test_rmw_modes_are_independently_defined_at_8_16_32_bits(self):
        for width in (8, 16, 32):
            with self.subTest(width=width):
                self.assertEqual(apply_rmw(1, 0, 1, width, "MASKED_MERGE"), 1)
                self.assertEqual(apply_rmw(1, 0, 1, width, "CLEAR_VALUE_THEN_MASKED_INSERT"), 0)
                self.assertEqual(apply_rmw((1 << width) - 1, (1 << width) - 1, 0,
                                           width, "MASKED_MERGE"), 0)

    def test_rmw_rejects_overflow_negative_bool_and_unknown_mode(self):
        for args in ((0, 0x100, 0, 8, "MASKED_MERGE"), (-1, 0, 0, 16, "MASKED_MERGE"),
                     (True, 0, 0, 16, "MASKED_MERGE"), (0, 0, 0, 16, "UNKNOWN")):
            with self.subTest(args=args), self.assertRaises(ValueError):
                apply_rmw(*args)

    def test_rmw_modes_cover_zero_all_bits_and_nonstandard_formula(self):
        self.assertEqual(apply_rmw(0xff, 0xff, 0, 8, "MASKED_MERGE"), 0)
        self.assertEqual(apply_rmw(0xff, 0x0f, 0x03, 8, "MASKED_MERGE"), 0xf3)
        self.assertEqual(apply_rmw(0x01, 0x00, 0x01, 8, "CLEAR_VALUE_THEN_MASKED_INSERT"), 0)
        self.assertEqual(apply_rmw(0x00, 0x01, 0x01, 8, "CLEAR_MASK_THEN_OR"), 0x01)

    def test_historical_helper_registry_preserves_formula_and_argument_order(self):
        self.assertEqual(HISTORICAL_RMW_HELPERS["mod_radio_reg"]["argument_order"], ["value", "mask"])
        self.assertEqual(HISTORICAL_RMW_HELPERS["mod_radio_reg"]["semantic_mode"], "CLEAR_VALUE_THEN_MASKED_INSERT")
        self.assertEqual(HISTORICAL_RMW_HELPERS["phy_reg_mod"]["argument_order"], ["mask", "value"])
        self.assertEqual(HISTORICAL_RMW_HELPERS["phy_reg_mod"]["semantic_mode"], "MASKED_MERGE")
        self.assertEqual(HISTORICAL_RMW_HELPERS["ai_core_cflags"]["semantic_mode"], "CLEAR_MASK_THEN_OR")
        self.assertNotIn("sb_core_cflags", HISTORICAL_RMW_HELPERS)

    def test_lowerer_accepts_only_explicit_supported_historical_rmw_contracts(self):
        source = {"sequence_id": "fixture-001", "source_function": "sub_9591e", "source_site": "0x95df4",
                  "provenance": "accepted RMW packet", "confidence_status": "PROVEN",
                  "operation_type": "RMW", "hardware_domain": "RADIO", "register_or_table": "0x0411",
                  "width_bits": 16, "semantic_mode": "CLEAR_VALUE_THEN_MASKED_INSERT",
                  "argument_order": ["value", "mask"], "helper_api": "mod_radio_reg",
                  "mask": "0x0000", "value_or_symbol": "0x0001"}
        lowered, reason = lower_operation(source, [])
        self.assertIsNone(reason)
        self.assertEqual(lowered["rmw"]["semantic_mode"], "CLEAR_VALUE_THEN_MASKED_INSERT")
        self.assertEqual(lowered["rmw"]["argument_order"], ["value", "mask"])
        unresolved = dict(source, helper_api="sb_core_cflags", semantic_mode="CLEAR_MASK_THEN_OR",
                          argument_order=["mask", "value"])
        lowered, reason = lower_operation(unresolved, [])
        self.assertIsNone(lowered)
        self.assertRegex(reason, "unknown|registered")
        swapped = dict(source, argument_order=["mask", "value"])
        lowered, reason = lower_operation(swapped, [])
        self.assertIsNone(lowered)
        self.assertRegex(reason, "contradicts")

    def test_independent_fixture_oracles_match_rmw_table_and_trace_comparison(self):
        fixture = json.loads(ORACLES.read_text())
        for vector in fixture["rmw_vectors"]:
            with self.subTest(vector=vector["id"]):
                self.assertEqual(apply_rmw(vector["old"], vector["mask"], vector["value"],
                                           vector["width_bits"], vector["mode"]), vector["expected"])
        table = fixture["table14_offset33"]
        self.assertEqual(table["expected_simulation_words_with_fixture_word2_4660"], [0xe800, 0x84, 0x1234])
        diff = fixture["trace_difference"]
        self.assertEqual(compare_traces(diff["left"], diff["right"])["first_difference"], diff["first_difference_index"])

    def test_all_accepted_through_crs_contract_hashes_match_prechange_baseline(self):
        baseline = json.loads(THROUGH_CRS_HASHES.read_text())["contracts"]
        actual_paths = sorted(path.relative_to(ROOT).as_posix() for path in (ROOT / "artifacts/contracts").glob("bcm4352-through-crs-*.json"))
        self.assertEqual(actual_paths, sorted(baseline))
        actual = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in actual_paths}
        self.assertEqual(actual, baseline)

    def test_malformed_typed_json_shapes_return_errors_without_tracebacks(self):
        malformed = [None, [], {"schema": [], "lowering_status": []},
                     program([None]), program([op("op-000", [], source_site={})]),
                     program([op("op-000", "WRITE", domain=[], register=[], width_bits=[], value=[])])]
        for candidate in malformed:
            with self.subTest(candidate=candidate):
                self.assertTrue(validate_typed_ir(candidate))

    def test_typed_unknown_symbol_default_rejected_even_with_synthetic_input(self):
        unknown = {"id": "word2", "classification": "HISTORICAL_SEMANTIC_UNKNOWN", "value_type": "u16",
                   "width_bits": 16, "signed": False, "provenance": "accepted evidence", "known_value": None,
                   "required_input_policy": "SYNTHETIC_ONLY", "unknown": True, "default": 0}
        ir = program([op("op-000", "WRITE", domain="RADIO", register="1", width_bits=16, value={"symbol": "word2"})], [unknown])
        self.assertTrue(validate_typed_ir(ir))

    def test_false_ready_and_8b0_classification_are_rejected(self):
        ir = program([op("op-000", "RETURN_BOUNDARY", boundary="callback-return")])
        ir["completion"]["hardware_ready"] = True
        ir["completion"]["readiness_postcondition"] = {"status": "PROVEN", "provenance": "invented fixture"}
        self.assertTrue(any("no accepted BCM4352" in error for error in validate_typed_ir(ir)))
        config = {"id": "nested+0x8b0", "classification": "RUNTIME_HARDWARE_SAMPLE", "value_type": "u32",
                  "width_bits": 32, "signed": False, "provenance": "bad fixture", "known_value": None,
                  "required_input_policy": "REQUIRED", "unknown": True}
        ir = program([op("op-000", "RETURN_BOUNDARY", boundary="return")], [config])
        self.assertTrue(any("EXTERNAL_CONFIGURATION" in error for error in validate_typed_ir(ir)))

    def test_helper_argument_order_is_not_swapped(self):
        # mod_radio_reg's recovered order is value, mask: this clears bit 0.
        self.assertEqual(apply_rmw(1, 0, 1, 16, "CLEAR_VALUE_THEN_MASKED_INSERT"), 0)
        # phy_reg_mod's recovered order is mask, value: this sets bit 0.
        self.assertEqual(apply_rmw(0, 1, 1, 16, "MASKED_MERGE"), 1)

    def test_shadow_state_write_read_rmw_is_sequential(self):
        ir = program([
            op("op-000", "WRITE", domain="PHY", register="0x10", width_bits=16, value=0x1234),
            op("op-001", "READ", domain="PHY", register="0x10", width_bits=16),
            op("op-002", "RMW", domain="PHY", register="0x10", width_bits=16,
               rmw={"semantic_mode": "MASKED_MERGE", "argument_order": ["mask", "value"], "mask": 0x00ff, "value": 0x0056}),
        ])
        trace = evaluate_typed_scenario(ir, {}, {}, {"PHY:0x10": {"mode": "SHADOW_STATE", "initial": 0}}, scenario="SYNTHETIC:shadow")
        self.assertEqual(trace[1]["value"], 0x1234)
        self.assertEqual(trace[2]["old_value"], 0x1234)
        self.assertEqual(trace[2]["new_value"], 0x1256)

    def test_snapshot_only_does_not_assume_write_persistence(self):
        ir = program([
            op("op-000", "WRITE", domain="PHY", register="0x10", width_bits=16, value=0x1234),
            op("op-001", "READ", domain="PHY", register="0x10", width_bits=16),
            op("op-002", "RMW", domain="PHY", register="0x10", width_bits=16,
               rmw={"semantic_mode": "MASKED_MERGE", "argument_order": ["mask", "value"], "mask": 0xff, "value": 0x56}),
        ])
        trace = evaluate_typed_scenario(ir, {}, {"PHY:0x10": 0},
                                        {"PHY:0x10": {"mode": "SNAPSHOT_ONLY"}}, scenario="SYNTHETIC:snapshot")
        self.assertEqual(trace[1]["value"], 0)
        self.assertEqual(trace[2]["old_value"], 0)

    def test_fresh_read_sequence_and_exhaustion(self):
        ir = program([op("op-000", "READ", domain="RADIO", register="0x20", width_bits=16),
                      op("op-001", "READ", domain="RADIO", register="0x20", width_bits=16)])
        model = {"RADIO:0x20": {"mode": "FRESH_READ_SEQUENCE", "values": [1, 2]}}
        self.assertEqual([e["value"] for e in evaluate_typed_scenario(ir, {}, {}, model, scenario="SYNTHETIC:fresh")], [1, 2])
        model["RADIO:0x20"]["values"] = [1]
        with self.assertRaisesRegex(ValueError, "exhaust"):
            evaluate_typed_scenario(ir, {}, {}, model, scenario="SYNTHETIC:fresh")

    def test_register_model_must_be_explicit_for_typed_program(self):
        ir = program([op("op-000", "READ", domain="PHY", register="0x10", width_bits=16)])
        with self.assertRaisesRegex(ValueError, "register model"):
            evaluate_typed_scenario(ir, {}, {"PHY:0x10": 0}, {}, scenario="SYNTHETIC:missing-model")

    def test_width_48_table_is_one_element_of_three_words_in_transfer_order(self):
        table = {"kind": "TABLE_WRITE", "table_id": 0x14, "offset": 0x33, "element_count": 1,
                 "element_width_bits": 48, "payload_encoding": "PACKED_16BIT_WORDS",
                 "payload_word_width_bits": 16, "payload_word_order": [0, 1, 2],
                 "payload": [{"literal": 0xe800}, {"literal": 0x0084}, {"symbol": "TABLE14_OFFSET33_WORD2"}],
                 "source_site": "0x9f9fa", "provenance": {"class": "ACCEPTED_FACT_BIN"}}
        symbol = {"id": "TABLE14_OFFSET33_WORD2", "classification": "HISTORICAL_SEMANTIC_UNKNOWN",
                  "value_type": "u16", "width_bits": 16, "signed": False, "provenance": "accepted table14 packet",
                  "known_value": None, "required_input_policy": "SYNTHETIC_ONLY", "unknown": True}
        result = evaluate_table_transaction(table, [symbol], {"TABLE14_OFFSET33_WORD2": 0x1234},
                                            scenario="SYNTHETIC: packed table fixture")
        self.assertEqual(result["element_count"], 1)
        self.assertEqual(result["payload_word_order"], [0, 1, 2])
        self.assertEqual(result["payload_words"], [0xe800, 0x84, 0x1234])
        self.assertFalse(result["historical_semantics_resolved"])
        self.assertTrue(result["simulation_only"])
        self.assertFalse(result["hardware_observation"])

    def test_table14_unknown_word_never_defaults_and_requires_labeled_simulation(self):
        table = {"kind": "TABLE_WRITE", "table_id": 0x14, "offset": 0x33, "element_count": 1,
                 "element_width_bits": 48, "payload_encoding": "PACKED_16BIT_WORDS",
                 "payload_word_width_bits": 16, "payload_word_order": [0, 1, 2],
                 "payload": [{"literal": 0xe800}, {"literal": 0x84}, {"symbol": "TABLE14_OFFSET33_WORD2"}],
                 "source_site": "0x9f9fa", "provenance": {"class": "ACCEPTED_FACT_BIN"}}
        symbol = {"id": "TABLE14_OFFSET33_WORD2", "classification": "HISTORICAL_SEMANTIC_UNKNOWN", "value_type": "u16",
                  "width_bits": 16, "signed": False, "provenance": "test", "known_value": None,
                  "required_input_policy": "SYNTHETIC_ONLY", "unknown": True, "default": 0}
        with self.assertRaisesRegex(ValueError, "default|unknown"):
            evaluate_table_transaction(table, [symbol], {}, scenario="SYNTHETIC:test")
        symbol_without_default = dict(symbol)
        symbol_without_default.pop("default")
        with self.assertRaisesRegex(ValueError, "SYNTHETIC"):
            evaluate_table_transaction(table, [symbol_without_default], {"TABLE14_OFFSET33_WORD2": 0x1234})

    def test_table_shape_negative_cases(self):
        good = {"kind": "TABLE_WRITE", "table_id": 0x14, "offset": 0x33, "element_count": 1,
                "element_width_bits": 48, "payload_encoding": "PACKED_16BIT_WORDS",
                "payload_word_width_bits": 16, "payload_word_order": [0, 1, 2],
                "payload": [{"literal": 1}, {"literal": 2}, {"literal": 3}],
                "source_site": "fixture", "provenance": {"class": "SYNTHETIC_FIXTURE"}}
        cases = [
            {k: v for k, v in good.items() if k != "element_count"},
            {k: v for k, v in good.items() if k != "payload"},
            {k: v for k, v in good.items() if k != "element_width_bits"},
            dict(good, element_width_bits=24), dict(good, offset=-1),
            dict(good, payload=good["payload"][:2]), dict(good, payload=[{"literal": 0x10000}, *good["payload"][1:]]),
            dict(good, payload=[{"literal": 1}, {"literal": 2}, {"literal": 3}], source_site="fixture",
                 payload_encoding="ELEMENT_VALUES", payload_word_width_bits=None, payload_word_order=[]),
            dict(good, payload=[*good["payload"], {"literal": 4}]),
        ]
        for case in cases:
            with self.subTest(case=case):
                case = dict(case)
                case.pop("kind", None)
                self.assertTrue(validate_typed_ir(program([op("op-000", "TABLE_WRITE", **case)])))

    def test_table14_rejects_wrong_transfer_order_without_integer_packing(self):
        operation = {"kind": "TABLE_WRITE", "table_id": 0x14, "offset": 0x33, "element_count": 1,
                     "element_width_bits": 48, "payload_encoding": "PACKED_16BIT_WORDS",
                     "payload_word_width_bits": 16, "payload_word_order": [2, 1, 0],
                     "payload": [{"literal": 0xe800}, {"literal": 0x84}, {"symbol": "TABLE14_OFFSET33_WORD2"}],
                     "source_site": "0x9f9fa", "provenance": {"class": "ACCEPTED_FACT_BIN"}}
        unknown = {"id": "TABLE14_OFFSET33_WORD2", "classification": "HISTORICAL_SEMANTIC_UNKNOWN",
                   "value_type": "u16", "width_bits": 16, "signed": False, "provenance": "accepted packet",
                   "known_value": None, "required_input_policy": "SYNTHETIC_ONLY", "unknown": True}
        errors = validate_typed_ir(program([op("op-000", "TABLE_WRITE", **{k: v for k, v in operation.items() if k != "kind"})], [unknown]))
        self.assertTrue(any("ordered 48-bit" in error for error in errors))

    def test_table_read_requires_explicit_destination_and_no_write_payload(self):
        transaction = {"kind": "TABLE_READ", "table_id": 1, "offset": 2, "element_count": 1,
                       "element_width_bits": 16, "payload_encoding": "ELEMENT_VALUES",
                       "payload_word_width_bits": None, "payload_word_order": [], "payload": [],
                       "destination": {"object_id": "nested", "base_relation": "[pi+0x138]",
                                       "destination_offset": 0x492,
                                       "chain_index": {"symbol": "chain"}, "chain_stride": 0x78},
                       "read_context": {"invocation_id": "invocation-0", "sample_ordinal": 0,
                                        "core_index": None},
                       "source_site": "fixture", "provenance": "synthetic"}
        chain_symbol = {"id": "chain", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u8",
                        "width_bits": 8, "signed": False, "provenance": "synthetic", "known_value": None,
                        "required_input_policy": "REQUIRED", "unknown": True}
        self.assertEqual(validate_typed_ir(program([op("op-000", "TABLE_READ", **{k: v for k, v in transaction.items() if k != "kind"})], [chain_symbol])), [])
        transaction["payload"] = [{"literal": 0}]
        self.assertTrue(validate_typed_ir(program([op("op-000", "TABLE_READ", **{k: v for k, v in transaction.items() if k != "kind"})], [chain_symbol])))

    def test_synthetic_table_read_materializes_chain_strided_nested_destinations(self):
        operations = []
        samples = []
        for index, (base, table_offset, chain, value) in enumerate(((0x492, 7, 0, 0x1234),
                                                                       (0x49c, 8, 0, 0xabcd),
                                                                       (0x492, 7, 1, 0x5678),
                                                                       (0x49c, 8, 1, 0xef01))):
            op_id = f"op-{index:03d}"
            operations.append(op(op_id, "TABLE_READ", table_id=100 + index % 2,
                                 offset=table_offset, element_count=1, element_width_bits=16,
                                 payload_encoding="ELEMENT_VALUES", payload_word_width_bits=None,
                                 payload_word_order=[], payload=[],
                                 destination={"object_id": "nested", "base_relation": "[pi+0x138]",
                                              "destination_offset": base, "chain_index": chain,
                                              "chain_stride": 0x78},
                                 read_context={"invocation_id": "desense-call-0", "sample_ordinal": index,
                                               "core_index": None}, source_site=f"0x9b6{index:02x}7",
                                 provenance="synthetic desense fixture"))
            samples.append({"operation_id": op_id, "table_id": 100 + index % 2, "offset": table_offset,
                            "invocation_id": "desense-call-0", "sample_ordinal": index,
                            "core_index": None, "chain_index": chain,
                            "element_count": 1, "element_width_bits": 16,
                            "payload_encoding": "ELEMENT_VALUES", "values": [value],
                            "payload_word_order": [],
                            "sample_kind": "SYNTHETIC_TABLE_SNAPSHOT", "hardware_observation": False})
        ir = program(operations)
        self.assertEqual(validate_typed_ir(ir), [])
        input_snapshots = copy.deepcopy(samples)
        first = evaluate_typed_scenario(ir, {}, {}, {}, scenario="SYNTHETIC:desense-chain-0",
                                        table_snapshots=samples)
        second = evaluate_typed_scenario(ir, {}, {}, {}, scenario="SYNTHETIC:desense-chain-0",
                                         table_snapshots=samples)
        self.assertEqual(first, second)
        self.assertEqual(samples, input_snapshots)
        reads = [event for event in first if event["kind"] == "TABLE_READ_SIMULATION"]
        self.assertEqual([event["destination_write"]["effective_offset"] for event in reads],
                         [0x492, 0x49c, 0x492 + 0x78, 0x49c + 0x78])
        self.assertEqual([event["destination_write"]["value"] for event in reads],
                         [[0x1234], [0xabcd], [0x5678], [0xef01]])
        self.assertTrue(all(event["destination_write"]["object_id"] == "nested" and
                            event["destination_write"]["base_relation"] == "[pi+0x138]" and
                            event["telemetry_eligible"] is False and
                            event["hardware_observation"] is False for event in reads))
        self.assertEqual(len({(event["destination_write"]["effective_offset"],
                               event["destination_write"]["byte_length"]) for event in reads}), 4)

    def test_synthetic_table_read_requires_exact_snapshot_context_and_rejects_bad_samples(self):
        symbol = {"id": "chain", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u8",
                  "width_bits": 8, "signed": False, "provenance": "synthetic", "known_value": None,
                  "required_input_policy": "REQUIRED", "unknown": True}
        read = op("op-000", "TABLE_READ", table_id=100, offset=3, element_count=1,
                  element_width_bits=16, payload_encoding="ELEMENT_VALUES", payload_word_width_bits=None,
                  payload_word_order=[], payload=[],
                  destination={"object_id": "nested", "base_relation": "[pi+0x138]",
                               "destination_offset": 0x492, "chain_index": 0,
                               "chain_stride": 0x78},
                  read_context={"invocation_id": "call-0", "sample_ordinal": 0, "core_index": None},
                  source_site="0x9b63f", provenance="accepted table-read callsite fixture")
        ir = program([read])
        valid = {"operation_id": "op-000", "table_id": 100, "offset": 3, "invocation_id": "call-0",
                 "sample_ordinal": 0, "core_index": None, "chain_index": 0,
                 "element_count": 1, "element_width_bits": 16, "payload_encoding": "ELEMENT_VALUES",
                 "values": [0x4321], "payload_word_order": [],
                 "sample_kind": "SYNTHETIC_TABLE_SNAPSHOT", "hardware_observation": False}
        with self.assertRaisesRegex(ValueError, "snapshot"):
            evaluate_typed_scenario(ir, {}, {}, {}, scenario="SYNTHETIC:missing-table", table_snapshots=[])
        with self.assertRaisesRegex(ValueError, "exhausted|ordinal"):
            evaluate_typed_scenario(ir, {}, {}, {}, scenario="SYNTHETIC:exhausted-table",
                                    table_snapshots=[dict(valid, sample_ordinal=1)])
        for change, message in (({"element_count": 2}, "element_count"),
                                ({"element_width_bits": 8}, "element_width_bits"),
                                ({"values": [0x10000]}, "fit|width"),
                                ({"chain_index": None}, "chain"),
                                ({"offset": 4}, "context")):
            sample = dict(valid, **change)
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, message):
                evaluate_typed_scenario(ir, {}, {}, {}, scenario="SYNTHETIC:bad-table", table_snapshots=[sample])
        malformed_ir = copy.deepcopy(ir)
        malformed_ir["operations"][0]["destination"]["destination_offset"] = -1
        self.assertTrue(any("destination_offset" in error for error in validate_typed_ir(malformed_ir)))
        malformed_ir = copy.deepcopy(ir)
        malformed_ir["operations"][0]["destination"]["chain_index"] = None
        self.assertTrue(any("chain" in error.lower() for error in validate_typed_ir(malformed_ir)))

    def test_synthetic_table_read_distinguishes_repeated_same_location_and_packed_words(self):
        reads = []
        samples = []
        for index, words in enumerate(([0xe800, 0x0084, 0x1234], [0xe800, 0x0084, 0x5678])):
            op_id = f"op-{index:03d}"
            reads.append(op(op_id, "TABLE_READ", table_id=0x14, offset=0x33, element_count=1,
                            element_width_bits=48, payload_encoding="PACKED_16BIT_WORDS",
                            payload_word_width_bits=16, payload_word_order=[0, 1, 2], payload=[],
                            destination={"object_id": "table-buffer", "base_relation": "synthetic-object",
                                         "destination_offset": 0, "chain_index": None, "chain_stride": 0},
                            read_context={"invocation_id": "same-table-different-moments", "sample_ordinal": index,
                                          "core_index": 0}, source_site="synthetic:table14-read",
                            provenance="synthetic snapshot fixture"))
            samples.append({"operation_id": op_id, "table_id": 0x14, "offset": 0x33,
                            "invocation_id": "same-table-different-moments", "sample_ordinal": index,
                            "core_index": 0, "chain_index": None, "element_count": 1,
                            "element_width_bits": 48, "payload_encoding": "PACKED_16BIT_WORDS", "values": words,
                            "payload_word_order": [0, 1, 2],
                            "sample_kind": "SYNTHETIC_TABLE_SNAPSHOT", "hardware_observation": False})
        trace = evaluate_typed_scenario(program(reads), {}, {}, {}, scenario="SYNTHETIC:packed-read",
                                        table_snapshots=samples)
        self.assertEqual([event["destination_write"]["value"] for event in trace],
                         [[[0xe800, 0x0084, 0x1234]], [[0xe800, 0x0084, 0x5678]]])
        self.assertNotEqual(trace[0]["destination_write"]["value"], trace[1]["destination_write"]["value"])
        malformed = copy.deepcopy(reads[0])
        sample_with_wrong_order = copy.deepcopy(samples[0])
        sample_with_wrong_order["payload_word_order"] = [2, 1, 0]
        with self.assertRaisesRegex(ValueError, "packing order"):
            evaluate_typed_scenario(program([reads[0]]), {}, {}, {}, scenario="SYNTHETIC:wrong-order",
                                    table_snapshots=[sample_with_wrong_order])

    def test_lowered_8b0_fork_is_typed_external_config_and_blocks_unexpanded_arm_bodies(self):
        result = lower_contract(json.loads(REAL_CONTRACT.read_text()), ROOT)
        self.assertEqual(result["typed_ir"]["operations"][3]["kind"], "BRANCH")
        branch = result["typed_ir"]["operations"][3]
        rejoin = next(event for event in result["typed_ir"]["operations"] if event["id"] == branch["rejoin"])
        self.assertEqual(rejoin["source_site"], "0x90b2e")
        self.assertEqual([arm["id"] for arm in branch["arms"]], ["ZERO", "NONZERO"])
        self.assertEqual([arm["operations"][0]["kind"] for arm in branch["arms"]],
                         ["REFERENCE_BLOCK", "REFERENCE_BLOCK"])
        config_symbol = next(symbol for symbol in result["typed_ir"]["symbols"] if symbol["id"] == "nested+0x8b0")
        self.assertEqual(config_symbol["classification"], "EXTERNAL_CONFIGURATION")
        self.assertIsNone(config_symbol["known_value"])
        self.assertEqual(result["first_failure"]["operation_id"], "postcrs-004")
        self.assertFalse(result["full_event_lowering_allowed"])
        self.assertTrue(validate_typed_ir(result["typed_ir"]) == [])
        for value, arm_name in ((0, "ZERO"), (1, "NONZERO"), (0x23, "NONZERO")):
            with self.subTest(value=value):
                evaluated = evaluate_typed_scenario(result["typed_ir"], {"nested+0x8b0": value}, {}, {},
                                                    scenario=f"SYNTHETIC:8b0-{value}",
                                                    allow_supported_prefix=True)
                self.assertEqual(evaluated["status"], "SUPPORTED_PREFIX_ONLY")
                selected = next(event for event in evaluated["trace"] if event["kind"] == "BRANCH")
                self.assertEqual(selected["arm"], arm_name)
                self.assertEqual(selected["rejoin_site"], "0x90b2e")
                self.assertEqual(evaluated["boundary"]["kind"], "REFERENCE_BLOCK")
                self.assertFalse(any(event.get("kind") in {"WRITE", "RMW", "TABLE_WRITE"}
                                     for event in evaluated["trace"]))
        with self.assertRaisesRegex(ValueError, "unresolved symbol"):
            evaluate_typed_scenario(result["typed_ir"], {}, {}, {}, scenario="SYNTHETIC:8b0-missing",
                                    allow_supported_prefix=True)

    def test_legacy_event_program_refuses_ambiguous_48_bit_table_payload(self):
        legacy = {"steps": [{"kind": "TABLE_WRITE", "table": 0x14, "offset": 0x33,
                             "width_bits": 48, "payload": [0xe800, 0x84, 0]}]}
        with self.assertRaisesRegex(ValueError, "invalid table element width"):
            evaluate_scenario(legacy, {}, {})

    def test_poll_description_does_not_claim_observation_and_simulation_fails_closed(self):
        poll = op("op-000", "POLL", domain="RADIO", register="0x0413", width_bits=16,
                  poll_mode="POLL_DESCRIPTION", mask=0x10, expected=0x10, max_attempts=3,
                  delay_interval_us=100, termination_rule="MASK_MATCH", timeout_behavior="LOCAL_FALSE")
        ir = program([poll])
        description = evaluate_typed_scenario(ir, {}, {}, {}, scenario="SYNTHETIC:poll-description")
        self.assertEqual(description[0]["kind"], "POLL_DESCRIPTION")
        self.assertNotIn("outcome", description[0])
        simulated = dict(poll, poll_mode="POLL_SIMULATION")
        sim_ir = program([simulated])
        result = evaluate_typed_scenario(sim_ir, {"poll:op-000": [0, 0x10]}, {}, {}, scenario="SYNTHETIC:poll-sim")
        self.assertEqual(result[0]["outcome"], "MATCHED")
        with self.assertRaisesRegex(ValueError, "exhausted before"):
            evaluate_typed_scenario(sim_ir, {"poll:op-000": [0]}, {}, {}, scenario="SYNTHETIC:poll-timeout-incomplete")
        with self.assertRaisesRegex(ValueError, "outside 16 bits"):
            evaluate_typed_scenario(sim_ir, {"poll:op-000": [0x10000]}, {}, {}, scenario="SYNTHETIC:poll-overflow")

    def test_predicate_width_and_input_fail_closed(self):
        predicate = {"op": "EQ", "left": {"symbol": "x"},
                     "right": {"const": {"value_type": "u8", "width_bits": 8, "signed": False, "value": 1}}}
        with self.assertRaisesRegex(ValueError, "unresolved"):
            evaluate_predicate(predicate, {"x": {"value_type": "u8", "width_bits": 8, "signed": False}}, {})
        with self.assertRaisesRegex(ValueError, "fit|width"):
            evaluate_predicate(predicate, {"x": {"value_type": "u8", "width_bits": 8, "signed": False}}, {"x": 0x100})

    def test_validator_rejects_predicate_width_mismatch_before_evaluation(self):
        symbol = {"id": "x", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u8",
                  "width_bits": 8, "signed": False, "provenance": "synthetic", "known_value": None,
                  "required_input_policy": "REQUIRED", "unknown": True}
        condition = {"op": "EQ", "left": {"symbol": "x"},
                     "right": {"const": {"value_type": "u16", "width_bits": 16, "signed": False, "value": 1}}}
        ir = program([op("op-000", "WRITE", domain="PHY", register="1", width_bits=16, value=1, condition=condition)], [symbol])
        self.assertTrue(any("incompatible width" in error for error in validate_typed_ir(ir)))

    def test_callback_skip_matched_and_unmatched_traces_are_distinct(self):
        symbols = [
            {"id": "life", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u8", "width_bits": 8, "signed": False,
             "provenance": "synthetic", "known_value": None, "required_input_policy": "REQUIRED", "unknown": True},
            {"id": "requested", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u16", "width_bits": 16, "signed": False,
             "provenance": "synthetic", "known_value": None, "required_input_policy": "REQUIRED", "unknown": True},
            {"id": "current", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u16", "width_bits": 16, "signed": False,
             "provenance": "synthetic", "known_value": None, "required_input_policy": "REQUIRED", "unknown": True},
            {"id": "row_match", "classification": "CONTROLLED_TEST_INPUT", "value_type": "bool", "width_bits": 1, "signed": False,
             "provenance": "synthetic", "known_value": None, "required_input_policy": "REQUIRED", "unknown": True},
            {"id": "row_word", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u16", "width_bits": 16, "signed": False,
             "provenance": "synthetic", "known_value": None, "required_input_policy": "REQUIRED", "unknown": True},
        ]
        def sym(name): return {"symbol": name}
        def const(value, width): return {"const": {"value_type": f"u{width}", "width_bits": width, "signed": False, "value": value}}
        selected = {"op": "AND", "args": [
            {"op": "NE", "left": sym("life"), "right": const(0, 8)},
            {"op": "NE", "left": sym("requested"), "right": sym("current")},
            {"op": "EQ", "left": sym("row_match"), "right": const(1, 1)},
        ]}
        unmatched = {"op": "AND", "args": [
            {"op": "NE", "left": sym("life"), "right": const(0, 8)},
            {"op": "NE", "left": sym("requested"), "right": sym("current")},
            {"op": "EQ", "left": sym("row_match"), "right": const(0, 1)},
        ]}
        skipped = {"op": "OR", "args": [
            {"op": "EQ", "left": sym("life"), "right": const(0, 8)},
            {"op": "EQ", "left": sym("requested"), "right": sym("current")},
        ]}
        branch = op("op-000", "BRANCH", arms=[
            {"id": "matched", "when": selected, "operations": [op("row-write", "WRITE", domain="RADIO", register="0x08e0", width_bits=16, value=sym("row_word")).copy()]},
            {"id": "unmatched", "when": unmatched, "operations": []},
            {"id": "skipped", "when": skipped, "operations": []},
        ], arm_coverage="PROVEN_EXHAUSTIVE", rejoin="op-end")
        # The source-site identity on this synthetic write is deliberately local.
        branch["arms"][0]["operations"][0]["source_site"] = "synthetic:row-write"
        ir = program([branch, op("op-end", "RETURN_BOUNDARY", boundary="synthetic-end")], symbols)
        ir["control_flow"]["entry"] = "op-000"
        ir["control_flow"]["exit"] = "op-end"
        ir["control_flow"]["edges"] = [
            {"from": "op-000", "to": "row-write", "arm": "matched"},
            {"from": "op-000", "to": "op-end", "arm": "unmatched"},
            {"from": "op-000", "to": "op-end", "arm": "skipped"},
            {"from": "row-write", "to": "op-end"},
        ]
        model = {"RADIO:0x08e0": {"mode": "SNAPSHOT_ONLY"}}
        def run(life, requested, current, row_match, row_word=0x1234):
            return evaluate_typed_scenario(ir, {"life": life, "requested": requested, "current": current,
                                                "row_match": row_match, "row_word": row_word}, {}, model,
                                           scenario="SYNTHETIC:channel-fixture")
        matched = run(1, 0x1001, 0x1000, 1, 0x1234)
        self.assertTrue(any(item["kind"] == "WRITE" and item["value"] == 0x1234 for item in matched))
        changed_payload = run(1, 0x1001, 0x1000, 1, 0x4321)
        self.assertEqual([item["register"] for item in matched if item["kind"] == "WRITE"],
                         [item["register"] for item in changed_payload if item["kind"] == "WRITE"])
        self.assertEqual([item["value"] for item in changed_payload if item["kind"] == "WRITE"], [0x4321])
        for inputs in ((0, 0x1001, 0x1000, 1), (1, 0x1000, 0x1000, 1), (1, 0x1001, 0x1000, 0)):
            trace = run(*inputs)
            self.assertFalse(any(item["kind"] == "WRITE" for item in trace))
        self.assertTrue(validate_typed_ir(ir) == [])

    def test_actual_partial_contract_validates_and_lowers_only_supported_prefix(self):
        contract = json.loads(REAL_CONTRACT.read_text())
        result = lower_contract(contract, ROOT)
        self.assertEqual(result["status"], "LOWERED_SUPPORTED_PREFIX")
        self.assertGreater(result["lowered_operation_count"], 0)
        self.assertEqual(result["typed_ir"]["schema"], "openbrcm-bcm4352-typed-event-ir/v1")
        self.assertTrue(result["unlowered_operations"])
        self.assertIn("TABLE14_OFFSET33_WORD2", result["required_symbols"])
        self.assertIn("postcrs-008", [x["operation_id"] for x in result["unlowered_operations"]])
        self.assertEqual(result["typed_ir"]["completion"]["status"], "PARTIAL_WITH_EXACT_GAP")
        self.assertFalse(result["typed_ir"]["completion"]["hardware_ready"])
        self.assertEqual(result["first_failure"]["operation_id"], "postcrs-004")
        self.assertIn("CURRENT_PHY_CHANSPEC", result["required_snapshots"])
        self.assertTrue(result["evidence_contract_valid"])
        self.assertFalse(result["full_event_lowering_allowed"])
        self.assertTrue(result["supported_prefix_available"])
        self.assertFalse(result["channel_ready_proven"])
        self.assertFalse(result["hardware_ready"])

    def test_checked_in_partial_lowering_result_matches_current_generator(self):
        contract = json.loads(REAL_CONTRACT.read_text())
        result = json.loads(LOWERING_RESULT.read_text())
        self.assertEqual(result, lower_contract(contract, ROOT))
        self.assertEqual(result["status"], "LOWERED_SUPPORTED_PREFIX")
        self.assertEqual(result["typed_ir"]["completion"]["status"], "PARTIAL_WITH_EXACT_GAP")
        self.assertFalse(result["typed_ir"]["completion"]["hardware_ready"])

    def test_typed_schema_definition_matches_validator_and_generated_ir(self):
        schema = json.loads(TYPED_IR_SCHEMA.read_text())
        result = json.loads(LOWERING_RESULT.read_text())
        self.assertEqual(schema["$id"], SCHEMA)
        self.assertEqual(set(schema["$defs"]["operation"]["properties"]["kind"]["enum"]), OPERATION_KINDS)
        self.assertEqual(validate_typed_ir(result["typed_ir"]), [])

    def test_partial_ir_cannot_be_run_as_complete_event_program(self):
        result = lower_contract(json.loads(REAL_CONTRACT.read_text()), ROOT)
        with self.assertRaisesRegex(ValueError, "partial|complete|lowered"):
            evaluate_typed_scenario(result["typed_ir"], {}, {}, {}, scenario="SYNTHETIC:test")

    def test_partial_supported_prefix_requires_and_stops_at_unknown_boundary(self):
        result = lower_contract(json.loads(REAL_CONTRACT.read_text()), ROOT)
        evaluated = evaluate_typed_scenario(result["typed_ir"], {"nested+0x8b0": 0}, {}, {},
                                            scenario="SYNTHETIC:accepted-prefix", allow_supported_prefix=True)
        self.assertEqual(evaluated["status"], "SUPPORTED_PREFIX_ONLY")
        self.assertEqual(evaluated["boundary"]["kind"], "REFERENCE_BLOCK")
        self.assertFalse(evaluated["hardware_ready"])
        self.assertFalse(any(item.get("kind") in {"WRITE", "RMW", "TABLE_WRITE"} for item in evaluated["trace"]))

    def test_partial_prefix_without_explicit_unknown_boundary_is_invalid(self):
        ir = program([op("op-000", "WRITE", domain="PHY", register="1", width_bits=16, value=1)],
                     status="LOWERED_SUPPORTED_PREFIX")
        ir["completion"].update(status="PARTIAL_WITH_EXACT_GAP", structurally_complete=False)
        self.assertTrue(any("terminal explicit UNKNOWN_BOUNDARY" in error for error in validate_typed_ir(ir)))

    def test_trace_comparison_and_evaluation_are_deterministic_without_mutation(self):
        symbol = {"id": "payload", "classification": "CONTROLLED_TEST_INPUT", "value_type": "u16",
                  "width_bits": 16, "signed": False, "provenance": "synthetic fixture", "known_value": None,
                  "required_input_policy": "REQUIRED", "unknown": True}
        ir = program([op("op-000", "WRITE", domain="PHY", register="0x1", width_bits=16, value={"symbol": "payload"})], [symbol])
        before = copy.deepcopy(ir)
        models = {"PHY:0x1": {"mode": "SNAPSHOT_ONLY"}}
        inputs = {"payload": 2}
        snapshots = {"PHY:0x1": 7}
        inputs_before = copy.deepcopy(inputs)
        snapshots_before = copy.deepcopy(snapshots)
        models_before = copy.deepcopy(models)
        first = evaluate_typed_scenario(ir, inputs, snapshots, models, scenario="SYNTHETIC:determinism")
        second = evaluate_typed_scenario(ir, inputs, snapshots, models, scenario="SYNTHETIC:determinism")
        self.assertEqual(first, second)
        self.assertEqual(ir, before)
        self.assertEqual(inputs, inputs_before)
        self.assertEqual(snapshots, snapshots_before)
        self.assertEqual(models, models_before)

    def test_reference_and_unknown_boundaries_never_noop(self):
        for kind in ("REFERENCE_BLOCK", "UNKNOWN_BOUNDARY"):
            ir = program([op("op-000", kind, reference="missing", boundary="unknown")])
            with self.assertRaisesRegex(ValueError, "unsupported|unresolved|boundary|reference"):
                evaluate_typed_scenario(ir, {}, {}, {}, scenario="SYNTHETIC:boundary")


if __name__ == "__main__":
    unittest.main()

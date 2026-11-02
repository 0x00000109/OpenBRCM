import copy
import hashlib
import json
import subprocess
import unittest
from collections import Counter
from pathlib import Path

from scripts.bcm4352_contract_ir import (
    evaluate_table_transaction,
    validate_typed_ir,
)


ROOT = Path(__file__).resolve().parents[2]
PREFIX = ROOT / "artifacts/contracts/bcm4352-common-prefix-through-a6b0f.json"
OVERLAY = ROOT / "artifacts/contracts/bcm4352-a6b0f-correction-overlay-v1.json"
A6_CONTRACT = ROOT / "artifacts/contracts/bcm4352-sub-a6b0f-cleanroom-execution-v1.json"
A0_TEST = ROOT / "tests/host/ob_phy_a0be4_test"
A6_TEST = ROOT / "tests/host/ob_phy_a6b0f_test"
COMMON_SHA = "a64519eee205cecebf4f7aecc18a444b80b4cd21bbb17a864a55e137ef913c84"
OVERLAY_SHA = "baaf270326064c0e8805168d1c4d49dace4b6e11f79594e62a5c5d50729c77f7"
BATCHES = {
    "0xa6c9e": (16, 0x20), "0xa6cc2": (16, 0x90),
    "0xa6ce6": (2, 0x121), "0xa6d0a": (2, 0x131),
    "0xa6d2e": (2, 0x124), "0xa6d52": (2, 0x137),
    "0xa6e2d": (16, 0),
}


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def a6_event_id(source_sequence, branch_selected):
    local = source_sequence - 3862
    is_final = local == 212
    if local > 2:
        local += 3
    if is_final and branch_selected:
        local += 1
    return local


def table_initial_value(offset, table):
    return (offset * 37 + table * 0x5a5a) & 0xffff


def standard_trace(kind, event, site, reg, value, width=16, domain="PHY"):
    return f"{kind},{domain},{event},0x{int(site, 0) if isinstance(site, str) else site:05x},0x{reg:04x},0x{value:08x},{width}"


def table_trace(kind, event, site, table, offset, values, width=16):
    op = "READ" if kind == "T" else "WRITE"
    return ",".join([
        "T", op, str(event), f"0x{int(site, 0) if isinstance(site, str) else site:05x}",
        str(table), f"0x{offset:04x}", str(len(values)), str(width),
        *[f"0x{word:04x}" for word in values],
    ])


def independent_a6_suffix(events, prefix_lines, branch_selected=True):
    """Expand the immutable logical vector, overlay and explicit fake samples."""
    saved_019e = None
    current_019e = None
    for line in prefix_lines:
        fields = line.split(",")
        if len(fields) == 7 and fields[1] == "PHY" and int(fields[4], 0) == 0x019e:
            if fields[0] == "W":
                current_019e = int(fields[5], 0) & 0xffff
    if current_019e is None:
        raise AssertionError("independent prefix trace has no PHY[0x019e] write")

    table_state = {}
    snapshots = {}
    out = []

    def add_child():
        nonlocal current_019e
        saved = current_019e
        out.append(standard_trace("R", 3, 0xa6052, 0x019e, current_019e))
        out.append(standard_trace("R", 4, 0xa6070, 0x019e, current_019e))
        current_019e = (current_019e & ~0x0002) | 0x0002
        out.append(standard_trace("W", 4, 0xa6070, 0x019e, current_019e))
        out.append(standard_trace("R", 5, 0xa6af8, 0x019e, current_019e))
        current_019e = (current_019e & ~0x0002) | (saved & 0x0002)
        out.append(standard_trace("W", 5, 0xa6af8, 0x019e, current_019e))

    i = 0
    while i < len(events):
        event = events[i]
        source = event["seq"]
        local = source - 3862
        event_id = a6_event_id(source, branch_selected)
        operation = event["operation"]
        site = int(event["site"], 0)
        if source == 4074 and branch_selected:
            out.append(table_trace("t", 215, 0xa6f35, 7, 0x80, [0x78]))
        if operation == "READ":
            current_019e = current_019e & 0xffff
            snapshots[event["save_restore_group"]] = current_019e
            out.append(standard_trace("R", event_id, site, 0x019e, current_019e))
        elif operation == "RMW":
            old = current_019e
            mask = number(event["mask"])
            if event["value"] == "set bit 1":
                insert = 2
            elif event["value"] == "restore saved bit 1":
                insert = snapshots[event["save_restore_group"]] & mask
            else:
                raise AssertionError(f"unhandled accepted RMW: {event}")
            out.append(standard_trace("R", event_id, site, 0x019e, old))
            current_019e = (old & ~mask) | (insert & mask)
            out.append(standard_trace("W", event_id, site, 0x019e, current_019e))
        elif operation == "TABLE_READ":
            table, offset = number(event["table_id"]), number(event["offset"])
            sample = table_state.setdefault((table, offset),
                                            table_initial_value(offset, table))
            out.append(table_trace("T", event_id, site, table, offset, [sample]))
        elif operation == "TABLE_WRITE":
            table, offset = number(event["table_id"]), number(event["offset"])
            site_key = event["site"]
            count = BATCHES[site_key][0] if site_key in BATCHES and offset == BATCHES[site_key][1] else 1
            if count > 1:
                group = events[i:i + count]
                if len(group) != count or any(
                    row["site"] != event["site"] or
                    number(row["offset"]) != offset + index or
                    row["operation"] != "TABLE_WRITE" for index, row in enumerate(group)
                ):
                    raise AssertionError(f"bad oracle batch at {event['site']}")
            else:
                group = [event]
            values = []
            for row in group:
                value = row["value"]
                if isinstance(value, str) and value.startswith("0x"):
                    values.append(number(value))
                    continue
                source_read = next(
                    previous for previous in reversed(events[:events.index(row)])
                    if previous["operation"] == "TABLE_READ" and
                    previous["save_restore_group"] == row["save_restore_group"] and
                    number(previous["table_id"]) == number(row["table_id"]) and
                    number(previous["offset"]) == number(row["offset"])
                )
                read_key = (number(source_read["table_id"]),
                            number(source_read["offset"]))
                read_value = table_state[read_key]
                if value in {"old32 & 0x000001ff", "old32 & 0xffff"}:
                    mask = 0x01ff if "001ff" in value else 0xffff
                    values.append(read_value & mask)
                elif value == "((old & 0x0fc0) | ((old & 0x20) | 0x0c))":
                    values.append((read_value & 0x0fe0) | 0x000c)
                elif value == "((old & 0x003f) | ((((old >> 6) & 0x20) | 0x0c) << 6))":
                    values.append((read_value & 0x003f) | (read_value & 0x0800) | 0x0300)
                else:
                    raise AssertionError(f"unhandled accepted table expression: {value}")
            out.append(table_trace("t", event_id, site, table, offset, values))
            for index, value in enumerate(values):
                table_state[(table, offset + index)] = value
            i += count - 1

        if source == 3864:
            add_child()
        i += 1
    return out


class A6B0FCommonPrefixTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-C", "tests/host", "ob_phy_a0be4_test",
                        "ob_phy_a6b0f_test"], cwd=ROOT, check=True,
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def test_accepted_vector_is_not_rewritten_and_overlay_records_selected_child(self):
        original = json.loads(PREFIX.read_text())
        self.assertEqual(original["event_count"], 4074)
        self.assertTrue(original["structurally_complete"])
        overlay_bytes = OVERLAY.read_bytes()
        self.assertEqual(hashlib.sha256(overlay_bytes).hexdigest(), OVERLAY_SHA)
        overlay = json.loads(overlay_bytes)
        self.assertEqual(overlay["source_vector_event_count"], 4074)
        self.assertEqual(
            [event["source_site"] for event in overlay["inserted_operations"]],
            ["0xa6052", "0xa6070", "0xa6af8"],
        )
        self.assertEqual(overlay["first_insertion_after_source_sequence"], 3864)

    def test_boardflags_literal_selects_a6f35_when_masked_bit_is_set(self):
        overlay = json.loads(OVERLAY.read_text())
        branch = overlay["a6f35_branch"]
        flags = int(branch["accepted_external_configuration_value"], 0)
        mask = int(branch["mask"], 0)
        self.assertEqual(flags & mask, mask)
        self.assertEqual(branch["selected_for_accepted_literal"], True)
        self.assertEqual(branch["source_site"], "0xa6f35")
        self.assertEqual(branch["operation"], "TABLE_WRITE")
        self.assertEqual(branch["table_offset"], "0x0080")
        self.assertEqual(branch["value"], "0x0078")

    def test_a6_contract_keeps_both_flag_arms_and_does_not_claim_readiness(self):
        contract = json.loads(A6_CONTRACT.read_text())
        ir = contract["typed_ir"]
        self.assertEqual(validate_typed_ir(ir), [])
        self.assertFalse(ir["completion"]["hardware_ready"])
        self.assertFalse(contract["hardware_ready"])
        branch = next(op for op in ir["operations"] if op["kind"] == "BRANCH")
        self.assertEqual(branch["arm_coverage"], "PROVEN_EXHAUSTIVE")
        self.assertEqual({arm["id"] for arm in branch["arms"]}, {"MASK_CLEAR", "MASK_SET"})

    def test_typed_table_write_can_use_a_prior_synthetic_table_read(self):
        read = {
            "id": "read-low", "kind": "TABLE_READ", "source_function": "fixture",
            "source_site": "0x1000", "provenance": "synthetic fixture",
            "table_id": 7, "offset": 0x142, "element_count": 1,
            "element_width_bits": 16, "payload_encoding": "ELEMENT_VALUES",
            "payload_word_width_bits": 16, "payload_word_order": [], "payload": [],
            "destination": {
                "object_id": "fixture-read", "base_relation": "synthetic table sample",
                "destination_offset": 0, "chain_index": None, "chain_stride": 0,
            },
            "read_context": {"invocation_id": "test", "sample_ordinal": 1, "core_index": None},
        }
        expression = {
            "op": "BIT_AND",
            "left": {"op": "READ_RESULT", "operation_id": "read-low"},
            "right": {"literal": 0x01ff},
        }
        write = {
            "id": "write-low", "kind": "TABLE_WRITE", "source_function": "fixture",
            "source_site": "0x1001", "provenance": "synthetic fixture",
            "table_id": 7, "offset": 0x142, "element_count": 1,
            "element_width_bits": 16, "payload_encoding": "ELEMENT_VALUES",
            "payload_word_width_bits": 16, "payload_word_order": [],
            "payload": [{"expression": expression}], "destination": None,
        }
        ir = {
            "schema": "openbrcm-bcm4352-typed-event-ir/v1",
            "target": {"chip": "0x4352"},
            "lowering_status": "LOWERED_COMPLETE",
            "declared_scope": {"entry": "read-low", "exit": "return"},
            "completion": {
                "status": "COMPLETE_PARAMETERIZED", "structurally_complete": True,
                "hardware_ready": False,
            },
            "symbols": [],
            "control_flow": {"entry": "read-low", "exit": "return", "edges": []},
            "operations": [read, write],
        }
        self.assertEqual(validate_typed_ir(ir), [])
        evaluated = evaluate_table_transaction(
            {key: value for key, value in write.items() if key not in {"id", "source_function", "condition"}},
            [], {}, scenario="SYNTHETIC:table-read-expression",
            read_results={"read-low": 0xa5ff},
        )
        self.assertEqual(evaluated["payload_words"], [0x01ff])
        self.assertTrue(evaluated["simulation_only"])

    def test_missing_prior_read_rejects_table_expression(self):
        contract = json.loads(A6_CONTRACT.read_text())
        changed = copy.deepcopy(contract["typed_ir"])
        expression_write = next(
            op for op in changed["operations"]
            if op["kind"] == "TABLE_WRITE" and
            isinstance(op.get("payload"), list) and op["payload"] and
            "expression" in op["payload"][0]
        )
        expression_write["payload"][0]["expression"]["left"]["operation_id"] = "missing-read"
        errors = validate_typed_ir(changed)
        self.assertTrue(any("earlier READ" in error for error in errors), errors)

    def test_full_composed_trace_matches_independent_immutable_vector_expansion(self):
        original_bytes = PREFIX.read_bytes()
        self.assertEqual(hashlib.sha256(original_bytes).hexdigest(), COMMON_SHA)
        source = json.loads(original_bytes)
        events = source["events"][3862:4074]
        self.assertEqual(len(events), 212)
        self.assertEqual(Counter(event["operation"] for event in events), {
            "READ": 6, "RMW": 12, "TABLE_READ": 66, "TABLE_WRITE": 128,
        })
        old_prefix = subprocess.run([str(A0_TEST), "--dump-composed-a0be4"],
                                    cwd=ROOT, check=True, capture_output=True,
                                    text=True).stdout.splitlines()
        actual = subprocess.run([str(A6_TEST), "--dump-composed"], cwd=ROOT,
                                 check=True, capture_output=True,
                                 text=True).stdout.splitlines()
        self.assertEqual(len(old_prefix), 3912)
        self.assertEqual(actual[:3912], old_prefix,
                         "the previously accepted 3,912-callback prefix changed")
        expected_suffix = independent_a6_suffix(events, old_prefix, True)
        self.assertEqual(len(expected_suffix), 181)
        actual_suffix = actual[3912:]
        if actual_suffix != expected_suffix:
            first = next((index for index, pair in enumerate(zip(expected_suffix,
                                                                  actual_suffix))
                          if pair[0] != pair[1]), min(len(expected_suffix),
                                                     len(actual_suffix)))
            self.fail("A6 trace first divergence at callback " + str(first + 1) +
                      "; expected=" + (expected_suffix[first] if first < len(expected_suffix) else "<end>") +
                      "; actual=" + (actual_suffix[first] if first < len(actual_suffix) else "<end>"))
        self.assertEqual(actual[-1], expected_suffix[-1])
        self.assertIn("T,WRITE,215,0xa6f35,7,0x0080,1,16,0x0078", actual_suffix)

        clear_actual = subprocess.run([str(A6_TEST), "--dump-composed-clear"],
                                      cwd=ROOT, check=True, capture_output=True,
                                      text=True).stdout.splitlines()
        self.assertEqual(clear_actual[:3912], old_prefix)
        clear_expected = independent_a6_suffix(events, old_prefix, False)
        self.assertEqual(len(clear_expected), 180)
        self.assertEqual(clear_actual[3912:], clear_expected)
        self.assertFalse(any("0xa6f35" in line for line in clear_actual[3912:]))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""Independent source-vector oracle for the synthetic Farrow/CRS runner."""

import hashlib
import json
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONTRACT_DIR = ROOT / "artifacts/contracts"
BLOCK = json.loads((CONTRACT_DIR / "bcm4352-next-farrow-crs-selected-block-v1.json").read_text())
CALLER = json.loads((CONTRACT_DIR / "bcm4352-post-9eaf9-caller-state-v1.json").read_text())
SOURCE_PATH = ROOT / BLOCK["source_contract"]["path"]
SOURCE_BYTES = SOURCE_PATH.read_bytes()
SOURCE = json.loads(SOURCE_BYTES)
BIN = ROOT / "tests/host/ob_phy_farrow_crs_test"
PREFIX_BIN = ROOT / "tests/host/ob_phy_a6b0f_test"


def number(value):
    return value if isinstance(value, int) else int(value, 0)


def fake_initial(reg, initial_019e, initial_0601):
    if reg == 0x019e:
        return initial_019e
    if reg == 0x0601:
        return initial_0601
    return ((reg * 37) ^ 0x1357) & 0xFFFF


def expected_callback_trace(initial_019e, initial_0601, table0, table1):
    """Expand the accepted source events; this does not read the C vector."""
    source_events = SOURCE["events"][4263:4316]
    typed_events = BLOCK["events"]
    if len(source_events) != 53 or len(typed_events) != 53:
        raise AssertionError("accepted Farrow/CRS source slice must contain 53 events")

    regs = {}
    saved_019e = None
    table_samples = {(7, 0x03CD, 0, 1, 1): table0,
                     (7, 0x03DD, 1, 1, 2): table1}
    trace = []

    def current(reg):
        if reg not in regs:
            regs[reg] = fake_initial(reg, initial_019e, initial_0601)
        return regs[reg]

    def add_phy(kind, event, site, reg, value):
        trace.append({
            "kind": kind,
            "logical_event": event,
            "source_site": f"0x{site:x}",
            "domain": "PHY",
            "register": f"0x{reg:04x}",
            "width_bits": 16,
            "value": f"0x{value:04x}",
        })

    api_targets = {
        "wlapi_suspend_mac_and_wait": "SUSPEND_MAC_AND_WAIT",
        "wlc_phyreg_enter": "PHYREG_ENTER",
        "wlc_phyreg_exit": "PHYREG_EXIT",
        "wlapi_enable_mac": "ENABLE_MAC",
    }

    for src, typed in zip(source_events, typed_events):
        event = src["seq"]
        if event != typed["source_contract_sequence"]:
            raise AssertionError(f"source event seam mismatch at {event}")
        site = number(typed.get("instruction_site", src["site"]))
        operation = src["operation"]

        if operation == "API_CALL":
            target = src["target"]
            trace.append({
                "kind": "API_CALL",
                "logical_event": event,
                "source_site": f"0x{site:x}",
                "operation": api_targets[target],
                "max_declared_wait_us": src.get("max_declared_wait_us", 0),
            })
            continue

        if src["domain"] == "TABLE":
            table = src["table_id"]
            offset = number(src["offset"])
            chain = src["chain"]
            if operation == "TABLE_READ":
                sample_key = (table, offset, chain, 1, 1 if chain == 0 else 2)
                sample = table_samples[sample_key]
                trace.append({
                    "kind": "TABLE_READ",
                    "logical_event": event,
                    "source_site": f"0x{site:x}",
                    "domain": "TABLE",
                    "table_id": table,
                    "offset": f"0x{offset:03x}",
                    "element_count": 1,
                    "element_width_bits": 16,
                    "chain_index": chain,
                    "invocation_id": 1,
                    "sample_ordinal": 1 if chain == 0 else 2,
                    "value": f"0x{sample:04x}",
                })
            else:
                trace.append({
                    "kind": "TABLE_WRITE",
                    "logical_event": event,
                    "source_site": f"0x{site:x}",
                    "domain": "TABLE",
                    "table_id": table,
                    "offset": f"0x{offset:03x}",
                    "element_count": 1,
                    "element_width_bits": 16,
                    "chain_index": chain,
                    "value": f"0x{number(src['value']):04x}",
                })
            continue

        reg = number(src.get("selector", src.get("register")))
        if operation == "READ":
            value = current(reg)
            if event == 4278:
                saved_019e = value
            add_phy("R", event, site, reg, value)
        elif operation == "WRITE":
            source_value = src["value"]
            if source_value == "R_FARROW_0601":
                value = current(0x0601)
            else:
                value = number(source_value)
            add_phy("W", event, site, reg, value)
            regs[reg] = value
        elif operation == "RMW":
            old = current(reg)
            mask = number(src["mask"])
            if event == 4279:
                insert = 0x0002
            elif event == 4284:
                if saved_019e is None:
                    raise AssertionError("restore used without accepted save event")
                insert = saved_019e & 0x0002
            else:
                # Accepted source expressions for the remaining selected
                # RMWs carry constant masked inserts in their value operand.
                value_expr = src["value"]
                if isinstance(value_expr, str) and value_expr.startswith("(old &"):
                    match = re.search(r"\|\s*(0x[0-9a-fA-F]+)", value_expr)
                    if not match:
                        raise AssertionError(f"unsupported accepted RMW expression {value_expr}")
                    insert = number(match.group(1))
                else:
                    insert = number(value_expr)
            add_phy("R", event, site, reg, old)
            new = ((old & (~mask & 0xFFFF)) | (insert & mask)) & 0xFFFF
            add_phy("W", event, site, reg, new)
            regs[reg] = new
        else:
            raise AssertionError(f"unsupported accepted source operation {operation}")

    return trace


def c_trace(initial_0601, initial_019e, table0, table1):
    subprocess.run(["make", "-C", str(ROOT / "tests/host"), "ob_phy_farrow_crs_test"],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    completed = subprocess.run(
        [str(BIN), "--dump", hex(initial_0601), hex(initial_019e),
         hex(table0), hex(table1)],
        cwd=ROOT, check=True, capture_output=True, text=True)
    return json.loads(completed.stdout)


def first_difference(expected, actual):
    for index, pair in enumerate(zip(expected, actual)):
        if pair[0] != pair[1]:
            return index, pair[0], pair[1]
    if len(expected) != len(actual):
        return min(len(expected), len(actual)), (
            expected[len(actual)] if len(actual) < len(expected) else "<end>"), (
            actual[len(expected)] if len(expected) < len(actual) else "<end>")
    return None


class Post9eaf9CallerFarrowCrsTests(unittest.TestCase):
    def test_source_contract_hash_and_sixteen_suffixes_are_pinned(self):
        digest = hashlib.sha256(SOURCE_BYTES).hexdigest()
        self.assertEqual(digest, BLOCK["source_contract"]["sha256"])
        self.assertEqual(len(BLOCK["source_contract"]["all_16_variant_source_hashes"]), 16)
        expected_suffix = BLOCK["source_contract"]["all_16_through_crs_suffix_semantics_sha256"]
        suffix_digest = None
        for item in BLOCK["source_contract"]["all_16_variant_source_hashes"]:
            path = ROOT / item["path"]
            raw = path.read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), item["sha256"])
            events = json.loads(raw)["events"][-53:]
            normalized = [{key: value for key, value in event.items() if key != "seq"}
                          for event in events]
            actual = hashlib.sha256(json.dumps(
                normalized, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            self.assertEqual(actual, item["suffix_semantics_sha256"])
            self.assertEqual(actual, expected_suffix)
            suffix_digest = actual
        self.assertEqual(len(SOURCE["events"][-53:]), 53)
        self.assertEqual(SOURCE["events"][-53]["seq"], 4264)
        self.assertEqual(SOURCE["events"][-1]["seq"], 4316)
        self.assertIsNotNone(suffix_digest)

    def test_selected_trace_matches_independent_source_expansion(self):
        cases = [
            (0x2468, 0xA540, 0x1234, 0x5678),
            (0xBEEF, 0xA542, 0xABCD, 0x9876),
        ]
        for initial_0601, initial_019e, table0, table1 in cases:
            with self.subTest(initial_019e=initial_019e, table0=table0, table1=table1):
                actual = c_trace(initial_0601, initial_019e, table0, table1)
                expected = expected_callback_trace(
                    initial_019e, initial_0601, table0, table1)
                difference = first_difference(expected, actual["records"])
                self.assertIsNone(difference, f"FIRST_TRACE_DIVERGENCE={difference}")
                self.assertEqual(actual["result"], 2)
                self.assertEqual(actual["logical_events"], 53)
                self.assertEqual(actual["callbacks"], 73)
                self.assertFalse(actual["historically_exact"])
                self.assertFalse(actual["hardware_ready"])

    def test_runtime_table_read_samples_do_not_change_later_effects(self):
        a = c_trace(0x2468, 0xA540, 0x1234, 0x5678)["records"]
        b = c_trace(0x2468, 0xA540, 0xABCD, 0x9876)["records"]
        self.assertEqual(len(a), len(b))
        changed = [i for i, (left, right) in enumerate(zip(a, b)) if left != right]
        self.assertEqual(changed, [17, 19])
        self.assertEqual(a[17]["kind"], "TABLE_READ")
        self.assertEqual(a[19]["kind"], "TABLE_READ")

    def test_word2_remains_unknown_and_actual_selection_unproven(self):
        predecessor = json.loads((CONTRACT_DIR /
            "bcm4352-sub-9eaf9-cleanroom-execution-v1.json").read_text())
        parameter = predecessor["historical_semantic_parameters"][0]
        self.assertEqual(parameter["id"], "TABLE14_OFFSET33_WORD2")
        self.assertEqual(parameter["classification"], "HISTORICAL_SEMANTIC_UNKNOWN")
        self.assertTrue(parameter["unknown"])
        self.assertFalse(BLOCK["scope"]["historically_exact_for_actual_device"])
        self.assertIn("actual invocation chanspec", " ".join(CALLER["classification"]["UNKNOWN"]))
        self.assertIn("0x1001", BLOCK["scope"]["selected_scenario"])

    def test_caller_qword_gate_and_first_unknown_are_typed_in_contract(self):
        self.assertEqual([q["slot"] for q in CALLER["qwords"]],
                         ["rbp-0x40", "rbp-0x48", "rbp-0x50", "rbp-0x58"])
        self.assertEqual(CALLER["qwords"][0]["selected_value_class"],
                         "POINTER_TO_STATIC_CHAN_TUNING_ROW")
        self.assertIn("actual chanspec", CALLER["qwords"][0]["reaching_value"])
        self.assertEqual([q["selected_reaching_value"] for q in CALLER["qwords"][1:]],
                         [0, 0, 0])
        predicate = CALLER["gate"]["predicate"]
        self.assertEqual(predicate["op"], "OR")
        self.assertEqual([arg["op"] for arg in predicate["args"]],
                         ["NON_NULL"] * 4)
        self.assertEqual(CALLER["gate"]["join"], "sub_a7089 @0xaa496")
        self.assertEqual(CALLER["hirssi_gate"]["accepted_profile_value"], 0)
        self.assertEqual(CALLER["hirssi_gate"]["effect"], "SKIPPED")
        post_gate = CALLER["post_return"]["sub_92efb_gate"]
        self.assertEqual(post_gate["callsite"], "sub_a7089 @0xaa4e2")
        self.assertEqual(post_gate["arguments"][1]["source_local"], "rbp-0x88")
        self.assertIn("PI+0x19c", post_gate["skip_predicate"])
        self.assertEqual(post_gate["helper"], "sub_92efb")
        self.assertEqual(post_gate["classification"], "SOFTWARE_ONLY")
        self.assertIn("none indexed for PHY/RADIO/TABLE", post_gate["hardware_effects"])
        self.assertIn("reached after either", CALLER["post_return"]["next_helper_callsite"])
        self.assertIn("nested+0x8b0", CALLER["post_return"]["next_helper_configuration_axis"])
        self.assertIn("PHY[0x019e] READ @0x9b0cd",
                      CALLER["post_return"]["first_downstream_hardware_event"])
        self.assertEqual(BLOCK["scope"]["logical_event_count"], 53)
        self.assertEqual(BLOCK["scope"]["callback_expansion"]["total"], 73)
        self.assertTrue(BLOCK["scope"]["structurally_complete_for_declared_scenario"])
        self.assertFalse(BLOCK["scope"]["hardware_ready"])
        self.assertFalse(BLOCK["scope"]["phy_ready"])
        self.assertFalse(BLOCK["scope"]["channel_ready"])
        self.assertIn("both arms reach sub_909fd", BLOCK["first_unresolved_after_boundary"])

    def test_composition_preserves_predecessor_prefix_and_appends_exact_block(self):
        subprocess.run(["make", "-C", str(ROOT / "tests/host"), "ob_phy_a6b0f_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        cases = [
            ("clear", "--dump-composed-9eaf9-clear", "--dump-composed-farrow-clear", 4277),
            ("set", "--dump-composed-9eaf9-set", "--dump-composed-farrow-set", 4305),
        ]
        for label, old_mode, new_mode, old_count in cases:
            with self.subTest(selector=label):
                old = subprocess.run([str(PREFIX_BIN), old_mode], cwd=ROOT,
                                     check=True, capture_output=True, text=True).stdout.splitlines()
                new = subprocess.run([str(PREFIX_BIN), new_mode], cwd=ROOT,
                                     check=True, capture_output=True, text=True).stdout.splitlines()
                self.assertEqual(len(old), old_count)
                self.assertEqual(new[:old_count], old)
                self.assertEqual(len(new), old_count + 73)
                self.assertTrue(new[old_count].startswith("W,PHY,4264,0xa4d36,0x019a,0x0000cccd,16"))
                self.assertTrue(new[-1].startswith("A,CONTROL,4316,0x92e53,"))


if __name__ == "__main__":
    unittest.main()

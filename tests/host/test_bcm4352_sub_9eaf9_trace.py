"""Independent event-to-callback oracle for the callback-only 9eaf9 runner."""
import json
import subprocess
import unittest
from pathlib import Path

from scripts.bcm4352_contract_ir import (
    evaluate_predicate, evaluate_table_transaction, validate_typed_ir,
)

ROOT = Path(__file__).resolve().parents[2]
SOURCE = json.loads((ROOT / "artifacts/contracts/bcm4352-sub-9eaf9-parameterized-complete.json").read_text())
CONTRACT = json.loads((ROOT / "artifacts/contracts/bcm4352-sub-9eaf9-cleanroom-execution-v1.json").read_text())
SCENARIOS = json.loads((ROOT / "artifacts/contracts/bcm4352-sub-9eaf9-synthetic-scenarios-v1.json").read_text())
BIN = ROOT / "tests/host/ob_phy_9eaf9_test"
TABLE_BATCHES = [(42, 1), (43, 1), (46, 1), (47, 1), (53, 8), (61, 8),
                 (69, 8), (77, 8), (85, 8), (93, 8), (101, 1), (102, 16), (118, 16)]


def parse_int(value):
    if isinstance(value, int):
        return value
    return int(value, 0)


def fixture_samples():
    return {"LPF_TABLE_A0": 0x1234, "LPF_TABLE_B0": 0x5678,
            "LPF_TABLE_A1": 0x9ABC, "LPF_TABLE_B1": 0xDEF0}


def lpf_pair_word(a, b, part):
    # Independently written mathematical oracle from the accepted formula.
    packed = (b << 16) | a
    result = ((packed & 0x01FFFFC7) | 0x28)
    result = (result & 0x01FFFE3F) | 0x140
    return result & 0xFFFF if part == "LOW" else (result >> 16) & 0x1FF


def expected_trace(word2, samples, initial_regs=None):
    events = SOURCE["events"]
    table_groups = {first: count for first, count in TABLE_BATCHES}
    regs = dict(initial_regs or {})
    read_values = {}
    sampled = {}
    trace = []

    def current(reg):
        return regs.setdefault(reg, 0xA540 if reg == 0x019E else ((reg * 37) ^ 0x1357) & 0xFFFF)

    def event_value(event):
        value = event["value"]
        if isinstance(value, int):
            return value
        if isinstance(value, list):
            return [0xE800, 0x0084, word2]
        if value.startswith("0x"):
            return int(value, 0)
        if value in {"PHY019E_INITIAL & 0x0002", "LPF_PHY019E_INITIAL & 0x0002"}:
            source_seq = 1 if value.startswith("PHY019E") else 38
            return read_values[source_seq] & 2
        if value.startswith("low16("):
            chain = int(value.split("LPF_TABLE_")[1][1])
            return lpf_pair_word(samples[f"LPF_TABLE_A{chain}"],
                                 samples[f"LPF_TABLE_B{chain}"], "LOW")
        if "LPF_TABLE_" in value:
            chain = int(value.split("LPF_TABLE_")[1][1])
            return lpf_pair_word(samples[f"LPF_TABLE_A{chain}"],
                                 samples[f"LPF_TABLE_B{chain}"], "HIGH")
        return samples[value]

    index = 0
    while index < len(events):
        event = events[index]
        seq = event["seq"]
        site = event["site"]
        op = event["operation"]
        info = event["register_or_table"]
        common = {"logical_event": seq, "source_site": site}
        if op == "READ":
            reg = int(info, 0)
            value = current(reg)
            read_values[seq] = value
            trace.append({"kind": "R", **common, "domain": "PHY", "register": f"0x{reg:04x}",
                          "width_bits": 16, "value": f"0x{value:04x}"})
        elif op == "WRITE":
            reg = int(info, 0)
            value = event_value(event)
            regs[reg] = value
            trace.append({"kind": "W", **common, "domain": "PHY", "register": f"0x{reg:04x}",
                          "width_bits": 16, "value": f"0x{value:04x}"})
        elif op == "RMW":
            reg = int(info, 0)
            old = current(reg)
            trace.append({"kind": "R", **common, "domain": "PHY", "register": f"0x{reg:04x}",
                          "width_bits": 16, "value": f"0x{old:04x}"})
            mask = parse_int(event["mask"])
            insert = event_value(event)
            new = ((old & (~mask & 0xFFFF)) | (insert & mask)) & 0xFFFF
            regs[reg] = new
            trace.append({"kind": "W", **common, "domain": "PHY", "register": f"0x{reg:04x}",
                          "width_bits": 16, "value": f"0x{new:04x}"})
        elif op == "TABLE_READ":
            table = info["table_id"]
            offset = int(info["offset"], 0)
            name = event["value"]
            chain = 0 if name.endswith("0") else 1
            ordinal = {"LPF_TABLE_A0": 1, "LPF_TABLE_B0": 2,
                       "LPF_TABLE_A1": 3, "LPF_TABLE_B1": 4}[name]
            value = samples[name]
            sampled[name] = value
            trace.append({"kind": "TABLE_READ", **common, "domain": "TABLE", "table_id": table,
                          "offset": f"0x{offset:03x}", "element_count": 1,
                          "element_width_bits": 16, "chain_index": chain,
                          "invocation_id": 1, "sample_ordinal": ordinal,
                          "value": f"0x{value:04x}"})
        elif op == "TABLE_WRITE":
            count = table_groups.get(seq, 1)
            group = events[index:index + count]
            if info["width_bits"] == 48:
                words = [0xE800, 0x0084, word2]
                order = [0, 1, 2]
            else:
                words = [event_value(e) for e in group]
                order = []
            offset = int(info["offset"], 0)
            trace.append({"kind": "TABLE_WRITE", **common, "domain": "TABLE",
                          "table_id": info["table_id"], "offset": f"0x{offset:03x}",
                          "element_count": count, "element_width_bits": info["width_bits"],
                          "payload_word_width_bits": 16, "payload_word_order": order,
                          "payload_words": [f"0x{x:04x}" for x in words]})
            index += count - 1
        index += 1
    return trace, sampled


def c_trace(word2, samples):
    subprocess.run(["make", "-C", str(ROOT / "tests/host"), "ob_phy_9eaf9_test"],
                   cwd=ROOT, check=True, capture_output=True, text=True)
    args = [str(BIN), "--dump", hex(word2), *(hex(samples[x]) for x in
            ("LPF_TABLE_A0", "LPF_TABLE_B0", "LPF_TABLE_A1", "LPF_TABLE_B1"))]
    result = subprocess.run(args, cwd=ROOT, check=True, capture_output=True, text=True)
    data = json.loads(result.stdout)
    return data["records"], data


class Sub9eaf9TraceOracleTests(unittest.TestCase):
    def test_typed_ir_and_caller_predicate_are_valid(self):
        self.assertEqual(validate_typed_ir(CONTRACT["typed_ir"]), [])
        gate = CONTRACT["caller_gate"]["selected_condition"]
        symbols = CONTRACT["typed_ir"]["symbols"]
        self.assertTrue(evaluate_predicate(gate, symbols,
                                           {"caller_local_-0x7a": 0, "nested+0x32c": 1}))
        self.assertTrue(evaluate_predicate(gate, symbols,
                                           {"caller_local_-0x7a": 1, "nested+0x32c": 0}))
        self.assertFalse(evaluate_predicate(gate, symbols,
                                            {"caller_local_-0x7a": 0, "nested+0x32c": 0}))

    def test_full_callback_trace_matches_source_oracle_for_both_word2_fixtures(self):
        for word2 in (0, 0x7B3C):
            with self.subTest(word2=hex(word2)):
                expected, sampled = expected_trace(word2, fixture_samples())
                actual, result = c_trace(word2, fixture_samples())
                self.assertEqual(result["result"], 2)
                self.assertEqual(result["logical_events"], 136)
                self.assertEqual(result["callbacks"], 107)
                self.assertEqual(len(actual), 107)
                self.assertEqual(actual, expected)
                self.assertEqual(sampled, fixture_samples())
                self.assertEqual(actual[0]["source_site"], "0x9eb45")
                self.assertEqual(actual[-1]["source_site"], "0x9fb5b")

    def test_word2_changes_only_the_single_48bit_payload_word(self):
        trace0, _ = c_trace(0, fixture_samples())
        trace1, _ = c_trace(0x7B3C, fixture_samples())
        differences = [(i, a, b) for i, (a, b) in enumerate(zip(trace0, trace1)) if a != b]
        self.assertEqual(len(differences), 1)
        _, old, new = differences[0]
        self.assertEqual(old["table_id"], 0x14)
        self.assertEqual(old["offset"], "0x033")
        self.assertEqual(old["payload_words"], ["0xe800", "0x0084", "0x0000"])
        self.assertEqual(new["payload_words"], ["0xe800", "0x0084", "0x7b3c"])
        self.assertEqual(CONTRACT["historical_semantic_parameters"][0]["unknown"], True)

    def test_runtime_sample_changes_only_read_and_derived_chain_outputs(self):
        baseline = fixture_samples()
        changed = dict(baseline, LPF_TABLE_A0=0x4321)
        base_trace, _ = c_trace(0, baseline)
        changed_trace, _ = c_trace(0, changed)
        differences = [(a, b) for a, b in zip(base_trace, changed_trace) if a != b]
        self.assertTrue(differences)
        allowed = {0x9DA4B, 0x9DAFE}
        self.assertTrue(all(int(a["source_site"], 16) in allowed and
                            int(b["source_site"], 16) in allowed for a, b in differences))
        self.assertFalse(any(a.get("offset") in {"0x015a", "0x037a"} for a, _ in differences))

    def test_snapshot_keys_and_unknown_policy_are_explicit(self):
        samples = CONTRACT["runtime_table_samples"]
        self.assertEqual(len(samples), 4)
        self.assertEqual([(x["table_id"], x["offset"], x["chain_index"], x["sample_ordinal"])
                          for x in samples],
                         [(7, "0x14a", 0, 1), (7, "0x36a", 0, 2),
                          (7, "0x15a", 1, 3), (7, "0x37a", 1, 4)])
        self.assertTrue(all(x["telemetry_classification"] ==
                            "NOT_TELEMETRY_WITHOUT_PROVEN_CONSUMER" for x in samples))
        unknown = CONTRACT["historical_semantic_parameters"][0]
        self.assertEqual(unknown["classification"], "HISTORICAL_SEMANTIC_UNKNOWN")
        self.assertTrue(unknown["unknown"])
        self.assertFalse(any(k in unknown for k in ("value", "default", "fallback", "known_value")))
        self.assertEqual(CONTRACT["execution_policies"]["HISTORICAL_STRICT"]["callbacks_before_block"], 0)
        self.assertEqual({s["id"] for s in SCENARIOS["scenarios"]},
                         {"selected-word2-0000", "selected-word2-7b3c",
                          "caller-skip-no-word2", "strict-selected-blocked"})

    def test_typed_lpf_pair_expression_uses_explicit_prior_table_reads(self):
        operation = {
            "kind": "TABLE_WRITE", "table_id": 7, "offset": 0x14A,
            "element_count": 1, "element_width_bits": 16,
            "payload_encoding": "ELEMENT_VALUES", "payload_word_width_bits": 16,
            "payload_word_order": [],
            "payload": [{"expression": {"op": "LPF_PAIR_WORD",
                                         "table_a_operation_id": "read-a",
                                         "table_b_operation_id": "read-b",
                                         "part": "LOW"}}],
            "source_site": "0x9dafe", "provenance": "accepted synthetic fixture",
        }
        result = evaluate_table_transaction(
            operation, [], {}, scenario="SYNTHETIC:sub_9eaf9",
            read_results={"read-a": 0x1234, "read-b": 0x5678})
        self.assertEqual(result["payload_words"], [lpf_pair_word(0x1234, 0x5678, "LOW")])
        with self.assertRaisesRegex(ValueError, "earlier 16-bit TABLE_READ"):
            evaluate_table_transaction(
                operation, [], {}, scenario="SYNTHETIC:sub_9eaf9",
                read_results={"read-a": 0x1234})

    @staticmethod
    def parse_dump_record(line):
        row = line.split(",")
        if row[0] != "T":
            return {"kind": row[0], "domain": row[1],
                    "logical_event": int(row[2]), "source_site": row[3],
                    "register": row[4],
                    "value": f"0x{int(row[5], 0):04x}" if int(row[6]) == 16 else row[5],
                    "width_bits": int(row[6])}
        operation = row[1]
        record = {"kind": "TABLE_READ" if operation == "READ" else "TABLE_WRITE",
                  "logical_event": int(row[2]), "source_site": row[3],
                  "domain": "TABLE", "table_id": int(row[4]),
                  "offset": f"0x{int(row[5], 0):03x}", "element_count": int(row[6]),
                  "element_width_bits": int(row[7])}
        if operation == "READ":
            record["value"] = row[8]
            event = SOURCE["events"][record["logical_event"] - 1]
            name = event["value"]
            record.update({"chain_index": 0 if name.endswith("0") else 1,
                           "invocation_id": 1,
                           "sample_ordinal": {"LPF_TABLE_A0": 1,
                                              "LPF_TABLE_B0": 2,
                                              "LPF_TABLE_A1": 3,
                                              "LPF_TABLE_B1": 4}[name]})
        else:
            if record["element_width_bits"] == 48:
                word_count = int(row[8])
                words = row[9:9 + word_count]
                record["payload_word_order"] = [0, 1, 2]
            else:
                words = row[8:]
                record["payload_word_order"] = []
            record.update({"payload_word_width_bits": 16,
                           "payload_words": words})
        return record

    def test_composed_prefix_to_caller_selected_sub_9eaf9(self):
        matrix = json.loads((ROOT / "artifacts/contracts/bcm4352-sub-9e378-variant-matrix-v1.json").read_text())
        by_id = {v["VARIANT_ID"]: v for v in matrix["SCENARIOS"]}
        samples = fixture_samples()
        cases = (("clear", "EQ2_PDGWRITE_TXCHAIN0", 4092),
                 ("set", "NE2_PDGSKIP_TXCHAIN3", 4093))
        subprocess.run(["make", "-C", str(ROOT / "tests/host"),
                        "ob_phy_a6b0f_test"], cwd=ROOT, check=True,
                       capture_output=True, text=True)
        for suffix, variant_id, prefix_count in cases:
            with self.subTest(boardflags=suffix):
                old = subprocess.run([str(ROOT / "tests/host/ob_phy_a6b0f_test"),
                                      f"--dump-composed-9e378-{suffix}", variant_id],
                                     cwd=ROOT, check=True, capture_output=True,
                                     text=True).stdout.splitlines()
                combined = subprocess.run([str(ROOT / "tests/host/ob_phy_a6b0f_test"),
                                           f"--dump-composed-9eaf9-{suffix}"],
                                          cwd=ROOT, check=True, capture_output=True,
                                          text=True).stdout.splitlines()
                predecessor_count = prefix_count + by_id[variant_id]["EXPECTED_CALLBACK_COUNT"]
                self.assertEqual(len(old), predecessor_count)
                self.assertEqual(combined[:predecessor_count], old)
                initial_regs = {}
                for line in old:
                    row = line.split(",")
                    if row[0] == "W" and row[1] == "PHY":
                        initial_regs[int(row[4], 0)] = int(row[5], 0)
                expected, _ = expected_trace(0, samples, initial_regs)
                actual = [self.parse_dump_record(line)
                          for line in combined[predecessor_count:]]
                self.assertEqual(len(actual), 107)
                self.assertEqual(actual, expected)
                self.assertEqual(actual[0]["source_site"], "0x9eb45")
                self.assertEqual(actual[-1]["source_site"], "0x9fb5b")
                self.assertEqual(len(combined), predecessor_count + 107)


if __name__ == "__main__":
    unittest.main()

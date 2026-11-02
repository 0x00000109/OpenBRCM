#!/usr/bin/env python3
"""Independent contract oracle for the conditional sub_9e378 clean-room runner."""

import hashlib
import json
import re
import subprocess
import sys
import unittest
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
CONTRACTS = ROOT / "artifacts/contracts"
FRAGMENT_PATH = CONTRACTS / "bcm4352-sub-9e378-16-way-fragment-20261009.json"
MATRIX_PATH = CONTRACTS / "bcm4352-sub-9e378-variant-matrix-v1.json"
EXECUTION_PATH = CONTRACTS / "bcm4352-sub-9e378-cleanroom-execution-v1.json"
FRAGMENT_SHA256 = "f6eb759d2e4e8672d44c50a23b2b85f3ff88f6a60dfcc5a375b8c69a23d4893a"

THROUGH_CRS_HASHES = {
    "eq2-skip-0": "1cce5a4cf5366e1e2bc5b7a1db64a0b50dfd7a4cad91b046fb42c7597ca218f2",
    "eq2-skip-1": "611610f0a5c276174532b99f9399bbf89199c459ff57ac923606d7e513c63f38",
    "eq2-skip-2": "1a5d6acf599e8ae59ac0ead84d3397d5462989ef745df1a7d7693cab0ccaa4e5",
    "eq2-skip-3": "2cf9a9d6133689ab85fbd60ebb9e14a7eed185b75ae3129d1b5b49273f77f7c4",
    "eq2-write-0": "18e0c4d4eaef2aad5eebb83638f0f693a21cf778cefaa012fa09ce4073bf52e4",
    "eq2-write-1": "202a970cc131ad5691219d7d8a8edc315b11fbf02a4b33e861fb7b2c09fd52d7",
    "eq2-write-2": "d894f3c427e91a13a39523b8c0c216c2ccef97d0b437f7e35ff4f94806909221",
    "eq2-write-3": "ee693c36b2f4df44ae6bb2955521db236eeb418a2b36a3b5e63d3ae4cdbcb15b",
    "ne2-skip-0": "3375a22740f15be74bb465404d9e226d904b5de79c03e0d13454a609437ceab9",
    "ne2-skip-1": "9ae401b73f01c1ccf695df3dccbb143240740a86c78da38451dfaae9ce4803e3",
    "ne2-skip-2": "1f2bc93d4993aa4a7b2d8427228ad27f037382f056a8495d4762178c0ac12de2",
    "ne2-skip-3": "cec16f9a4e611be60cf251e7eec1d21ad13015d30b93c30c2d6833a6f7a44e71",
    "ne2-write-0": "ba04a607f9dc622fc018b96c95d95e025fdb8f672884823153ee878f945af6b7",
    "ne2-write-1": "6d32059eab38fcd78af5d8fd9c9a11bd79516f38439c246c3aa674662b595f05",
    "ne2-write-2": "c1c0ef0e5e2a62f9d63ce17d4be03bb76049bee847c77bd3853b4a882ffb7043",
    "ne2-write-3": "7333c98e7dde1ecc78736e0e1c3a8a33e4f28fcd329e49a6d0fc1930f64201e2",
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalized_event(event):
    return (
        event.get("site"), event.get("domain"), event.get("operation"),
        event.get("register"), event.get("table_id"), event.get("offset"),
        event.get("count"), event.get("width", event.get("width_bits")),
        event.get("mask"), event.get("value"), event.get("payload_identity"),
    )


class Sub9e378VariantContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fragment = json.loads(FRAGMENT_PATH.read_text())
        cls.matrix = json.loads(MATRIX_PATH.read_text())
        cls.execution = json.loads(EXECUTION_PATH.read_text())
        cls.variants = {variant["id"]: variant for variant in cls.fragment["variants"]}
        build = subprocess.run(
            ["make", "-C", "tests/host", "ob_phy_a6b0f_test", "ob_phy_9e378_test"], cwd=ROOT,
            text=True, capture_output=True)
        if build.returncode:
            raise RuntimeError(build.stdout + build.stderr)
        manifest = json.loads(Path(
            "/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/re-image-manifest.json"
        ).read_text())
        vendor = Path(manifest["images"]["vendor"]["path"])
        with vendor.open("rb") as stream:
            section = ELFFile(stream).get_section_by_name(".data").data()
        cls.gain_words = {}
        for symbol, identity in {
            "acphy_txgain_ipa_2g_2069rev0":
                (0x14A7F0, 768, "d218af33aee8ebc6f3a885513267495cfd0c4f7fe10ca75696eb324545625c11"),
            "acphy_txgain_epa_2g_2069rev4":
                (0x149EF0, 768, "8854a71af6b79ad488510598391aefbcebe183f5d04489882335447ca6511c60"),
        }.items():
            offset, size, digest = identity
            block = section[offset:offset + size]
            if hashlib.sha256(block).hexdigest() != digest:
                raise RuntimeError(f"stable payload hash mismatch for {symbol}")
            cls.gain_words[symbol] = [int.from_bytes(block[i:i + 2], "little")
                                      for i in range(0, size, 2)]

    @staticmethod
    def _number(value):
        return int(value, 0) if isinstance(value, str) else value

    @staticmethod
    def _initial_register(domain, register):
        if domain == "PHY" and register == 0x019E:
            return 0xA540
        salt = 0x5A5A if domain == "RADIO" else 0x1357
        return ((register * 37) ^ salt) & 0xFFFF

    def _oracle_trace(self, variant, initial_registers=None):
        registers = dict(initial_registers or {})
        snapshots = {}
        lines = []

        def get(domain, register):
            key = (domain, register)
            if key not in registers:
                registers[key] = self._initial_register(domain, register)
            return registers[key]

        def rw_line(kind, event, site, register, value):
            return (f"{kind},{event['domain']},{event['seq']},"
                    f"0x{self._number(site):05x},0x{register:04x},"
                    f"0x{value:08x},{event['width']}\n")

        for event in variant["events"]:
            op = event["operation"]
            site = event["site"]
            domain = event["domain"]
            if op == "READ":
                reg = self._number(event["register"])
                value = get(domain, reg)
                lines.append(rw_line("R", event, site, reg, value))
                group = event.get("save_restore_group")
                if group:
                    snapshots[group] = value
                elif event["seq"] == 1 and domain == "PHY" and reg == 0x019E:
                    snapshots["outer-019e-snapshot"] = value
            elif op == "WRITE":
                reg = self._number(event["register"])
                value = self._number(event["value"])
                lines.append(rw_line("W", event, site, reg, value))
                registers[(domain, reg)] = value
            elif op == "RMW":
                reg = self._number(event["register"])
                mask = self._number(event["mask"])
                old = get(domain, reg)
                lines.append(rw_line("R", event, site, reg, old))
                raw_value = event["value"]
                if isinstance(raw_value, str) and "snapshot" in raw_value:
                    group = event.get("save_restore_group")
                    if group is None:
                        group = "outer-019e-snapshot"
                    value = snapshots[group] & mask
                else:
                    value = self._number(raw_value)
                if domain == "PHY":
                    new = (old & (~mask & 0xFFFF)) | (value & mask)
                else:
                    new = (old & (~value & 0xFFFF)) | (value & mask)
                lines.append(rw_line("W", event, site, reg, new))
                registers[(domain, reg)] = new
            elif op == "TABLE_WRITE":
                payload_identity = event.get("payload_identity")
                if payload_identity:
                    words = self.gain_words[payload_identity["symbol"]]
                else:
                    raw = event["value"]
                    words = [self._number(value) for value in raw] if isinstance(raw, list) else [self._number(raw)]
                line = (f"T,WRITE,{event['seq']},0x{self._number(site):05x},"
                        f"{self._number(event['table_id'])},"
                        f"0x{self._number(event['offset']):04x},{event['count']},"
                        f"{event['width']}")
                if event["width"] == 48:
                    line += f",{len(words)}"
                lines.append(line + "".join(f",0x{word:04x}" for word in words) + "\n")
            else:
                self.fail(f"independent oracle does not support {op}")
        return "".join(lines)

    def test_immutable_fragment_and_sixteen_full_stream_references_are_pinned(self):
        self.assertEqual(sha256(FRAGMENT_PATH), FRAGMENT_SHA256)
        paths = sorted(CONTRACTS.glob("bcm4352-through-crs-param-extpagain-*.json"))
        self.assertEqual(len(paths), 16)
        observed = {}
        for path in paths:
            match = re.fullmatch(
                r"bcm4352-through-crs-param-extpagain-(eq2|ne2)-pdgain5g-(skip|write)-txchain-([0-3])",
                path.stem)
            self.assertIsNotNone(match, path.name)
            observed[f"{match.group(1)}-{match.group(2)}-{match.group(3)}"] = sha256(path)
        self.assertEqual(observed, THROUGH_CRS_HASHES)

    def test_each_full_stream_contains_the_exact_accepted_9e378_fragment(self):
        for path in sorted(CONTRACTS.glob("bcm4352-through-crs-param-extpagain-*.json")):
            contract = json.loads(path.read_text())
            selectors = contract["selectors"]
            variant_id = (
                f"{selectors['extpagain2g']}_PDG"
                f"{'WRITE' if selectors['pdgain5g_class'] == 'WRITE' else 'SKIP'}_"
                f"TXCHAIN{selectors['txchain_low2']}"
            )
            expected = self.variants[variant_id]["events"]
            start = next(i for i, event in enumerate(contract["events"])
                         if event.get("site") == "0x9e395")
            end = next(i for i in range(start, len(contract["events"]))
                       if contract["events"][i].get("site") == "0x9eae7")
            actual = contract["events"][start:end + 1]
            with self.subTest(contract=path.name):
                self.assertEqual(len(actual), len(expected))
                self.assertEqual([normalized_event(e) for e in actual],
                                 [normalized_event(e) for e in expected])

    def test_matrix_has_sixteen_symbolic_variants_and_an_explicit_skip_arm(self):
        scenarios = self.matrix["SCENARIOS"]
        self.assertEqual(len(scenarios), 17)
        by_id = {scenario["VARIANT_ID"]: scenario for scenario in scenarios}
        self.assertEqual(len(by_id), 17)
        self.assertIn("CALL_SKIPPED", by_id)
        for variant_id, variant in self.variants.items():
            scenario = by_id[variant_id]
            callbacks = sum(2 if event["operation"] == "RMW" else 1
                            for event in variant["events"])
            self.assertEqual(scenario["LOGICAL_EVENT_COUNT"], variant["event_count"])
            self.assertEqual(scenario["EXPECTED_CALLBACK_COUNT"], callbacks)
            self.assertEqual(scenario["SOURCE_CONTRACT"],
                             "artifacts/contracts/bcm4352-sub-9e378-16-way-fragment-20261009.json")
            self.assertEqual(scenario["ENTRY_BOUNDARY"], "sub_9e378 @0x9e378")
            self.assertEqual(scenario["EXIT_BOUNDARY"], "normal return @0x9eaf8; caller join @0xaa432")
            self.assertTrue(scenario["HISTORICAL_UNKNOWNS"])
        skipped = by_id["CALL_SKIPPED"]
        self.assertEqual(skipped["LOGICAL_EVENT_COUNT"], 0)
        self.assertEqual(skipped["EXPECTED_CALLBACK_COUNT"], 0)
        self.assertEqual(skipped["EXIT_BOUNDARY"], "caller join @0xaa432")
        self.assertEqual(skipped["COMPOSED_SCENARIOS"]["A6B0F_BOARD_FLAGS_MASK_CLEAR"], {
            "PREDECESSOR_LOGICAL_COUNT": 4077,
            "PREDECESSOR_CALLBACK_COUNT": 4092,
            "NEW_COMPOSED_LOGICAL_COUNT": 4077,
            "NEW_COMPOSED_CALLBACK_COUNT": 4092,
        })

    def test_matrix_uses_structured_predicates_and_records_composed_counts(self):
        rows = {row["VARIANT_ID"]: row for row in self.matrix["SCENARIOS"]}
        for variant_id, variant in self.variants.items():
            row = rows[variant_id]
            self.assertIn("predicate_ast", row["CALLER_GATE"])
            self.assertEqual(row["PREDICATE_AST"][0]["op"],
                             "EQ" if variant["selectors"]["extpagain2g"] == "EQ2" else "NE")
            self.assertEqual(len(row["PREDICATE_AST"]), 4)
            logical = variant["event_count"]
            callbacks = sum(2 if event["operation"] == "RMW" else 1
                            for event in variant["events"])
            for boardflags, predecessor_logical, predecessor_callbacks in (
                ("A6B0F_BOARD_FLAGS_MASK_CLEAR", 4077, 4092),
                ("A6B0F_BOARD_FLAGS_MASK_SET", 4078, 4093),
            ):
                composed = row["COMPOSED_SCENARIOS"][boardflags]
                self.assertEqual(composed["PREDECESSOR_LOGICAL_COUNT"], predecessor_logical)
                self.assertEqual(composed["PREDECESSOR_CALLBACK_COUNT"], predecessor_callbacks)
                self.assertEqual(composed["NEW_COMPOSED_LOGICAL_COUNT"], predecessor_logical + logical)
                self.assertEqual(composed["NEW_COMPOSED_CALLBACK_COUNT"], predecessor_callbacks + callbacks)

    def test_execution_contract_keeps_actual_target_selector_values_unknown(self):
        self.assertEqual(self.execution["status"], "COMPLETE_CONDITIONAL_FOR_EXPLICIT_SYNTHETIC_SCENARIOS")
        self.assertIs(self.execution["hardware_ready"], False)
        self.assertIs(self.execution["actual_target_variant_proven"], False)
        ir = self.execution["typed_ir"]
        self.assertEqual(ir["schema"], "openbrcm-bcm4352-typed-event-ir/v1")
        self.assertTrue(ir["completion"]["structurally_complete"])
        self.assertIs(ir["completion"]["hardware_ready"], False)
        symbols = {symbol["id"]: symbol for symbol in ir["symbols"]}
        for name in ("extpagain2g", "nested+0x411", "txchain_low2"):
            self.assertEqual(symbols[name]["classification"], "EXTERNAL_CONFIGURATION")
            self.assertIsNone(symbols[name].get("known_value"))
            self.assertNotIn("default", symbols[name])
        gate = next(op for op in ir["operations"] if op["kind"] == "BRANCH")
        arms = {arm["id"]: arm for arm in gate["arms"]}
        self.assertIn("CALL_SKIPPED", arms)
        for variant_id in self.variants:
            self.assertIn(variant_id, arms)
        self.assertEqual(gate["rejoin_site"], "0xaa432")

    def test_source_call_arguments_and_width48_transfer_shape(self):
        variant = self.variants["EQ2_PDGWRITE_TXCHAIN0"]["events"]
        gain = next(event for event in variant if event["site"] == "0x9e711")
        self.assertEqual((gain["table_id"], gain["offset"], gain["count"], gain["width"]),
                         ("0x20", "0x0000", 128, 48))
        self.assertEqual(gain["payload_identity"]["size_bytes"], 768)
        self.assertEqual(gain["payload_identity"]["symbol"], "acphy_txgain_ipa_2g_2069rev0")
        # Direct historical instructions @0xb6a77..@0xb6ad1 transfer each
        # 48-bit element as three u16 words in increasing pointer order.
        self.assertEqual(self.execution["table_transfer"]["payload_word_order"], [0, 1, 2])
        self.assertEqual(self.execution["table_transfer"]["payload_word_width_bits"], 16)

    def test_gain_payloads_are_bound_to_exact_stable_relocations(self):
        expected = {
            "acphy_txgain_ipa_2g_2069rev0": ("0x9e439", "0x14a7f0"),
            "acphy_txgain_epa_2g_2069rev4": ("0x9e4f8", "0x149ef0"),
        }
        matrix_payloads = {entry["symbol"]: entry
                           for entry in self.matrix["PAYLOAD_IDENTITIES"]}
        execution_payloads = {entry["symbol"]: entry
                              for entry in self.execution["table_transfer"]["payloads"]}
        self.assertEqual(set(matrix_payloads), set(expected))
        self.assertEqual(set(execution_payloads), set(expected))
        for symbol, (site, offset) in expected.items():
            for payload in (matrix_payloads[symbol], execution_payloads[symbol]):
                relocation = payload["relocation_provenance"]
                self.assertEqual(relocation["section"], ".rela.text")
                self.assertEqual(relocation["offset"], site)
                self.assertEqual(relocation["type_name"], "R_X86_64_32S")
                self.assertEqual(relocation["symbol"], symbol)
                self.assertEqual(relocation["symbol_section"], ".data")
                self.assertEqual(relocation["symbol_section_offset"], offset)
                self.assertEqual(relocation["addend"], "0x0")

    def test_generated_vector_and_payloads_match_stable_source(self):
        result = subprocess.run(
            ["python3", "scripts/generate_bcm4352_9e378.py", "--check"],
            cwd=ROOT, text=True, capture_output=True,
            env={**__import__("os").environ,
                 "RE_IMAGE_MANIFEST": "/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/re-image-manifest.json"},
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_all_sixteen_c_callback_streams_match_independent_event_expansion(self):
        binary = ROOT / "tests/host/ob_phy_9e378_test"
        for variant_id, variant in self.variants.items():
            result = subprocess.run([str(binary), "--dump", variant_id],
                                    cwd=ROOT, text=True, capture_output=True)
            with self.subTest(variant=variant_id):
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, self._oracle_trace(variant))

    @staticmethod
    def _register_state_after_prefix(lines):
        registers = {}
        for line in lines:
            fields = line.rstrip("\n").split(",")
            if fields[0] not in ("R", "W") or fields[1] not in ("PHY", "RADIO"):
                continue
            key = (fields[1], int(fields[4], 0))
            value = int(fields[5], 0) & 0xFFFF
            if fields[0] == "W":
                registers[key] = value
            elif key not in registers:
                registers[key] = value
        return registers

    def test_composed_trace_preserves_both_corrected_prefixes_for_all_variants(self):
        binary = ROOT / "tests/host/ob_phy_a6b0f_test"
        for boardflags, old_arg, composed_arg, prefix_count in (
            ("clear", "--dump-composed-clear", "--dump-composed-9e378-clear", 4092),
            ("set", "--dump-composed", "--dump-composed-9e378-set", 4093),
        ):
            prefix = subprocess.run([str(binary), old_arg], cwd=ROOT,
                                    text=True, capture_output=True)
            self.assertEqual(prefix.returncode, 0, prefix.stderr)
            prefix_lines = prefix.stdout.splitlines(keepends=True)
            self.assertEqual(len(prefix_lines), prefix_count)
            state = self._register_state_after_prefix(prefix_lines)
            for variant_id, variant in self.variants.items():
                composed = subprocess.run([str(binary), composed_arg, variant_id],
                                          cwd=ROOT, text=True, capture_output=True)
                with self.subTest(boardflags=boardflags, variant=variant_id):
                    self.assertEqual(composed.returncode, 0, composed.stderr)
                    all_lines = composed.stdout.splitlines(keepends=True)
                    self.assertEqual(all_lines[:prefix_count], prefix_lines)
                    expected_suffix = self._oracle_trace(variant, state).splitlines(keepends=True)
                    self.assertEqual(all_lines[prefix_count:], expected_suffix)
                    self.assertEqual(len(all_lines), prefix_count +
                                     sum(2 if e["operation"] == "RMW" else 1
                                         for e in variant["events"]))

    def test_contract_ir_passes_the_shared_validator(self):
        from scripts.bcm4352_contract_ir import validate_typed_ir

        self.assertEqual(validate_typed_ir(self.execution["typed_ir"]), [])


if __name__ == "__main__":
    unittest.main()

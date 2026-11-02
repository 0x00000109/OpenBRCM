#!/usr/bin/env python3
"""Independent RE and contract checks for the post-table outer-loop body."""
import hashlib
import json
import subprocess
import unittest
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
VENDOR = Path("/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/vendor/wlc_hybrid.o_shipped")
EXPECTED_SHA = "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"


class Sub9af05Continuation(unittest.TestCase):
    def test_liveness_contracts_have_no_consumed_unknown_tail(self):
        for helper in ("99658", "998cc"):
            path = ROOT / f"artifacts/contracts/bcm4352-sub-{helper}-stack-tail-liveness-v1.json"
            data = json.loads(path.read_text())
            self.assertEqual(data["binary_sha256"], EXPECTED_SHA)
            self.assertEqual(data["unknown_transfer_byte_positions"], [])
            self.assertEqual(data["unknown_transfer_byte_occurrences"], 0)
            self.assertTrue(all("PROVEN_INITIALIZED" in json.dumps(row)
                                for row in data.get("liveness", data.get("byte_definitions", []))))
            self.assertIn("NOT_ACTUALLY_CONSUMED", json.dumps(data))

    def test_corrected_table_api_map_is_pinned_to_old_contracts(self):
        correction = json.loads((ROOT / "artifacts/contracts/bcm4352-sub-99658-table-api-abi-correction-v1.json").read_text())
        for rel, expected in correction["immutable_predecessors"].items():
            self.assertEqual(hashlib.sha256((ROOT / rel).read_bytes()).hexdigest(), expected)
        self.assertFalse(correction["strict_replay"]["stack_tail_blocker"])

    def test_loop_contract_event_vector_and_boundary(self):
        path = ROOT / "artifacts/contracts/bcm4352-sub-9af05-loop-continuation-v1.json"
        data = json.loads(path.read_text())
        self.assertEqual(data["source_event_contract"]["sha256"],
                         hashlib.sha256((ROOT / data["source_event_contract"]["path"]).read_bytes()).hexdigest())
        self.assertEqual(data["outer_loop"]["selected_index"], 1)
        self.assertEqual(data["counts"]["new_logical_events"], 13)
        self.assertEqual(data["counts"]["new_callback_records"], 14)
        self.assertEqual(data["events"][0]["site"], "0x9b3dd")
        self.assertEqual(data["events"][0]["payload"], ["nested+0x3e3", "nested+0x3e3"])
        self.assertEqual(data["events"][-1]["site"], "0x9b73b")
        self.assertEqual(data["boundary"]["terminal"], "sub_9af05 return @0x9b751")

    def test_direct_instruction_call_and_loop_sites(self):
        self.assertEqual(hashlib.sha256(VENDOR.read_bytes()).hexdigest(), EXPECTED_SHA)
        dis = subprocess.run(["objdump", "-d", "-Mintel", str(VENDOR)],
                             check=True, capture_output=True, text=True).stdout.lower()
        # objdump uses section-relative addresses for the vendor ET_REL text.
        for needle in ("9b14f:", "9b6fa:", "9b6fd:", "9b154:",
                       "9b3dd:", "9b477:", "9b4b7:", "9b63f:", "9b73b:"):
            self.assertIn(needle, dis)

    def test_998cc_static_copy_source_is_exactly_seven_bytes(self):
        contract = json.loads((ROOT / "artifacts/contracts/bcm4352-sub-998cc-stack-tail-liveness-v1.json").read_text())
        source = contract["relocation_source"]
        with VENDOR.open("rb") as stream:
            elf = ELFFile(stream)
            raw = elf.get_section_by_name(".rodata").data()[0x2CC73D:0x2CC744]
        self.assertEqual(raw.hex(), source["bytes_hex"])
        self.assertEqual(hashlib.sha256(raw).hexdigest(), source["sha256"])
        self.assertEqual(len(raw), source["arg2_copy_bytes"])


if __name__ == "__main__":
    unittest.main()

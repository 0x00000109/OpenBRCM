#!/usr/bin/env python3
"""Relocation-aware regression tests for selected ACPHY table payloads."""
import hashlib
import json
import subprocess
import unittest
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
CONTRACTS = ROOT / "artifacts/contracts"
INPUT = Path("/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008")
VENDOR = INPUT / "vendor/wlc_hybrid.o_shipped"
WLKO = INPUT / "wlko/wl.ko"
EXPECTED_VENDOR = "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"
EXPECTED_WLKO = "72668bdb65d0108682781dc23b5bc544d5c6eea4ae1646eb03f4f23c6bebbcec"

A6_CALLS = {
    0xA6C9E: (0xA6C82, 0xD9F70, 32),
    0xA6CC2: (0xA6CA6, 0xD9F90, 32),
    0xA6CE6: (0xA6CCA, 0xD9FB0, 4),
    0xA6D0A: (0xA6CEE, 0xD9FB0, 4),
    0xA6D2E: (0xA6D12, 0xD9FB4, 4),
    0xA6D52: (0xA6D36, 0xD9FB4, 4),
    0xA6E2D: (0xA6E10, 0xDA000, 32),
}
AF_CALLS = {
    0x9F8D7: (0x9F8BB, 0xDA080, 16),
    0x9F8FB: (0x9F8DF, 0xDA090, 16),
    0x9F91F: (0x9F903, 0xDA0A0, 16),
    0x9F943: (0x9F927, 0xDA0B0, 16),
    0x9F967: (0x9F94B, 0xDA0C0, 16),
    0x9F98B: (0x9F96F, 0xDA0D0, 16),
}


def _sections(path):
    stream = path.open("rb")
    elf = ELFFile(stream)
    text = elf.get_section_by_name(".text")
    data = elf.get_section_by_name(".data")
    rel = elf.get_section_by_name(".rela.text")
    symtab = elf.get_section(rel["sh_link"])
    relocations = {r["r_offset"]: r for r in rel.iter_relocations()}
    return stream, elf, text, data, symtab, relocations


def _resolved(path, relocation_offset, addend, length):
    stream, elf, text, data, symtab, relocs = _sections(path)
    try:
        reloc = relocs[relocation_offset]
        symbol = symtab.get_symbol(reloc["r_info_sym"])
        raw = text.data()[relocation_offset:relocation_offset + 4]
        payload = data.data()[addend:addend + length]
        return {
            "type": reloc.entry.r_info_type,
            "symbol_section": symbol["st_shndx"],
            "data_section_index": elf.get_section_index(".data"),
            "addend": reloc["r_addend"],
            "raw": raw,
            "payload": payload,
        }
    finally:
        stream.close()


class RelocatedPayloadTests(unittest.TestCase):
    def test_pinned_binary_identities(self):
        self.assertEqual(hashlib.sha256(VENDOR.read_bytes()).hexdigest(), EXPECTED_VENDOR)
        self.assertEqual(hashlib.sha256(WLKO.read_bytes()).hexdigest(), EXPECTED_WLKO)

    def test_all_seven_sub_a6b0f_raw_zero_r9_fields_are_data_relocations(self):
        for site, (reloc_offset, addend, length) in A6_CALLS.items():
            with self.subTest(site=hex(site)):
                item = _resolved(VENDOR, reloc_offset, addend, length)
                self.assertEqual(item["type"], 11)  # R_X86_64_32S
                self.assertEqual(item["symbol_section"], item["data_section_index"])
                self.assertEqual(item["addend"], addend)
                self.assertEqual(item["raw"], b"\0" * 4)
                self.assertEqual(len(item["payload"]), length)

    def test_six_selected_sub_9eaf9_raw_zero_r9_fields_are_data_relocations(self):
        for site, (reloc_offset, addend, length) in AF_CALLS.items():
            with self.subTest(site=hex(site)):
                item = _resolved(VENDOR, reloc_offset, addend, length)
                self.assertEqual(item["type"], 11)
                self.assertEqual(item["symbol_section"], item["data_section_index"])
                self.assertEqual(item["addend"], addend)
                self.assertEqual(item["raw"], b"\0" * 4)

    def test_every_raw_zero_r9_data_relocation_is_relocated_not_literal(self):
        stream, elf, text, _, symtab, relocs = _sections(VENDOR)
        try:
            calls = []
            raw_zero_r9 = []
            for reloc in relocs.values():
                symbol = symtab.get_symbol(reloc["r_info_sym"])
                if symbol.name != "wlc_phy_table_write_acphy":
                    continue
                call = reloc["r_offset"] - 1
                calls.append(call)
                producers = [
                    (off, candidate) for off, candidate in relocs.items()
                    if call - 0x40 <= off < call and off >= 3
                    and text.data()[off - 3:off] == bytes.fromhex("49c7c1")
                ]
                if not producers:
                    continue
                off, producer = max(producers, key=lambda item: item[0])
                if text.data()[off:off + 4] != b"\0" * 4:
                    continue
                raw_zero_r9.append(off)
                target = symtab.get_symbol(producer["r_info_sym"])
                self.assertEqual(producer.entry.r_info_type, 11)
                self.assertEqual(target["st_shndx"], elf.get_section_index(".data"))
            self.assertEqual(len(calls), 143)
            self.assertEqual(len(raw_zero_r9), 24)
        finally:
            stream.close()

    def test_linked_wlko_relocations_and_payloads_match_vendor_object(self):
        for site, (reloc_offset, addend, length) in {**A6_CALLS, **AF_CALLS}.items():
            with self.subTest(site=hex(site)):
                obj = _resolved(VENDOR, reloc_offset, addend, length)
                ko = _resolved(WLKO, reloc_offset, addend, length)
                self.assertEqual(ko["type"], 11)
                self.assertEqual(ko["symbol_section"], ko["data_section_index"])
                self.assertEqual((ko["addend"], ko["payload"]),
                                 (obj["addend"], obj["payload"]))

    def test_payload_vectors_are_exact_and_not_synthetic_zero_fill(self):
        expected = {
            0xD9F70: bytes.fromhex("04000300060005000200010008002a002b000f001f001f001f001f001f001f00"),
            0xD9F90: bytes.fromhex("0c00020002000400040006000100040001000200010001000100010001000100"),
            0xD9FB0: bytes.fromhex("aa0aaa0a"),
            0xD9FB4: bytes.fromhex("22022202"),
            0xDA000: bytes.fromhex("000001000200080005000000060003000f000400000035000f00000036001f00"),
            0xDA080: bytes.fromhex("2a0007000a00000008002b001f001f00"),
            0xDA090: bytes.fromhex("01000200020002001000010001000100"),
            0xDA0A0: bytes.fromhex("2a00070008000c000e002b001f001f00"),
            0xDA0B0: bytes.fromhex("01000600120008001000010001000100"),
            0xDA0C0: bytes.fromhex("2a00070008000e002b001f001f001f00"),
            0xDA0D0: bytes.fromhex("010006001e001c000100010001000100"),
        }
        stream, elf, _, data, _, _ = _sections(VENDOR)
        try:
            for addend, payload in expected.items():
                with self.subTest(addend=hex(addend)):
                    self.assertEqual(data.data()[addend:addend + len(payload)], payload)
                    self.assertNotEqual(payload, b"\0" * len(payload))
        finally:
            stream.close()

    def test_table14_third_word_still_has_no_reaching_store(self):
        for image in (VENDOR, WLKO):
            result = subprocess.run([
                "objdump", "-d", "--start-address=0x9eaf9", "--stop-address=0x9fb72", str(image)
            ], check=True, text=True, capture_output=True).stdout
            self.assertIn("$0xe800,-0xa0(%rbp)", result)
            self.assertIn("$0x84,-0x9e(%rbp)", result)
            self.assertIn("lea    -0xa0(%rbp),%r9", result)
            self.assertIn("mov    $0x30,%r8d", result)
            self.assertIn("mov    $0x33,%ecx", result)
            self.assertIn("mov    $0x1,%edx", result)
            self.assertIn("mov    $0x14,%esi", result)
            self.assertIn("mov    %rbx,%rdi", result)
            self.assertNotIn("-0x9c(%rbp)", result)

    def test_width48_writer_loads_word2_and_sends_it_to_table_port(self):
        for image in (VENDOR, WLKO):
            result = subprocess.run([
                "objdump", "-dr", "--start-address=0xb6a77", "--stop-address=0xb6ad4", str(image)
            ], check=True, text=True, capture_output=True).stdout
            self.assertIn("movzwl (%r14,%rdx,2),%edi", result)
            self.assertIn("cmp    $0x3,%eax", result)
            self.assertIn("R_X86_64_PC32\tosl_writew-0x4", result)

    def test_adjacent_selected_table14_calls_have_zero_third_word_only_as_corroboration(self):
        result = subprocess.run([
            "objdump", "-d", "--start-address=0x9f630", "--stop-address=0x9f9ff", str(VENDOR)
        ], check=True, text=True, capture_output=True).stdout
        for store in ("$0x0,-0x6c(%rbp)", "$0x0,-0x7c(%rbp)", "$0x0,-0x8c(%rbp)"):
            self.assertIn(store, result)
        self.assertNotIn("$0x0,-0x9c(%rbp)", result)

    def test_table14_offset33_has_no_other_same_binary_producer(self):
        # The index's table-call inventory and direct selected function both
        # show only four table-0x14 writes, at offsets 0x30..0x33.  This test
        # anchors the target site and the three explicit sibling word2 zeros;
        # it intentionally does not promote zero to the missing value.
        result = subprocess.run([
            "objdump", "-d", "--start-address=0x9f630", "--stop-address=0x9fa00", str(VENDOR)
        ], check=True, text=True, capture_output=True).stdout
        for offset in ("$0x30,%ecx", "$0x31,%ecx", "$0x32,%ecx", "$0x33,%ecx"):
            self.assertIn(offset, result)
        self.assertIn("$0x30,%r8d", result)
        self.assertEqual(result.count("$0x14,%esi"), 4)
        self.assertIn("$0xe800,-0xa0(%rbp)", result)
        self.assertIn("$0x84,-0x9e(%rbp)", result)
        self.assertNotIn("$0x0,-0x9c(%rbp)", result)

    def test_parameterized_table14_fragment_keeps_word2_unresolved(self):
        fragment = json.loads((CONTRACTS / "bcm4352-sub-9eaf9-table14-word2-parameterized.json").read_text())
        self.assertFalse(fragment["complete"])
        self.assertTrue(fragment["scope_complete_except_parameter"])
        self.assertEqual(fragment["operation_count"], 4)
        self.assertEqual([op["offset"] for op in fragment["operations"]],
                         ["0x30", "0x31", "0x32", "0x33"])
        self.assertEqual([op["words_u16"][2] for op in fragment["operations"][:3]],
                         ["0x0000"] * 3)
        unknown = fragment["operations"][3]["unresolved_parameter"]
        self.assertEqual(unknown["name"], "TABLE14_OFFSET33_WORD2")
        self.assertEqual(unknown["classification"], "HISTORICAL_INDETERMINATE_SEMANTIC_PARAMETER")
        self.assertFalse(unknown["is_runtime_hardware_sample"])
        self.assertFalse(unknown["is_external_board_selector"])
        self.assertEqual(fragment["operations"][3]["words_u16"][2], "TABLE14_OFFSET33_WORD2")
        self.assertIsNone(fragment["semantic_conclusion"]["cleanroom_word2"])

    def test_corrected_artifacts_do_not_claim_table14_word2_or_through_crs_complete(self):
        a6 = json.loads((CONTRACTS / "bcm4352-sub-a6b0f-corrected.json").read_text())
        af = json.loads((CONTRACTS / "bcm4352-sub-9eaf9-selected.json").read_text())
        prior = json.loads((CONTRACTS / "bcm4352-9eaf9-contradiction-20261009.json").read_text())
        self.assertEqual(len(a6["parent_table_calls"]), 7)
        self.assertTrue(a6["payloads_complete"])
        self.assertFalse(a6["complete"])
        self.assertEqual(len(af["resolved_table7_writes"]), 6)
        self.assertFalse(af["complete"])
        self.assertEqual((af["table14_write"]["table_id"], af["table14_write"]["offset"],
                          af["table14_write"]["count"], af["table14_write"]["width_bits"]),
                         ("0x14", "0x33", 1, 48))
        self.assertEqual(af["table14_write"]["words"][2], "TABLE14_OFFSET33_WORD2")
        self.assertFalse(af["table14_write"]["word2_proven"])
        self.assertEqual(af["word2_falsification"]["claim_after_falsification"], "PROVEN_INDETERMINATE")
        self.assertEqual(af["word2_falsification"]["counterexample"], None)
        self.assertTrue(af["word2_falsification"]["width48_word2_reaches_hw_data_port"])
        self.assertFalse(af["word2_falsification"]["candidate_is_proven_for_offset_0x33"])
        self.assertEqual(prior["classification"]["terminal"], "RELOCATION_ARTIFACT_RESOLVED")
        self.assertEqual(prior["relocation_audit"]["previous_classification_status"], "SUPERSEDED")
        self.assertFalse(prior["classification"]["through_crs_contract_complete"])


if __name__ == "__main__":
    unittest.main()

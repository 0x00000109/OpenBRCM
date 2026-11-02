"""Host tests for the fail-closed BCM4352 ET_REL observer admission tool."""

from __future__ import annotations

import hashlib
import importlib.util
import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL_PATH = ROOT / "tools" / "etrel-observer" / "rewrite.py"
SPEC = importlib.util.spec_from_file_location("etrel_observer_rewrite", TOOL_PATH)
assert SPEC and SPEC.loader
rewrite = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = rewrite
SPEC.loader.exec_module(rewrite)


def make_fixture() -> bytes:
    """Build a minimal ELF64 ET_REL with the three admitted instruction sites."""
    text = bytearray(0xB58A9)
    text[0x9868F:0x9869F] = bytes.fromhex("554889e541564188d6415541544989fc")
    text[0xB58A2:0xB58A9] = bytes.fromhex("80bb9c0f000000")
    text[0xB1471:0xB1476] = bytes.fromhex("6639d07506")
    text[0x9873F:0x98744] = bytes.fromhex("e800000000")

    shstr = b"\0.text\0.rela.text\0.shstrtab\0.symtab\0.strtab\0"
    shname = {name: shstr.index(name.encode()) for name in
              (".text", ".rela.text", ".shstrtab", ".symtab", ".strtab")}
    strtab = b"\0wlc_phy_cal_perical\0wlc_phy_cals_acphy\0phy_reg_mod\0"
    sname = {name: strtab.index(name.encode()) for name in
             ("wlc_phy_cal_perical", "wlc_phy_cals_acphy", "phy_reg_mod")}
    # ELF symbol: st_name, st_info, st_other, st_shndx, st_value, st_size.
    symbols = [bytes(24)]
    symbols += [struct.pack("<IBBHQQ", sname["wlc_phy_cal_perical"], 0x02, 0, 1,
                            0xB57D3, 0x387)]
    symbols += [struct.pack("<IBBHQQ", sname["wlc_phy_cals_acphy"], 0x02, 0, 1,
                            0xB13B5, 0x4EC)]
    symbols += [struct.pack("<IBBHQQ", sname["phy_reg_mod"], 0x02, 0, 1,
                            0xB2D55, 102)]
    symtab = b"".join(symbols)
    # r_offset, r_info=(symbol index 3, R_X86_64_PC32), r_addend=-4.
    rela = struct.pack("<QQq", 0x98740, (3 << 32) | rewrite.R_X86_64_PC32, -4)

    file_data = bytearray(64)
    sections: list[tuple[int, int, int, int, int, int, int, int, int, int]] = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    ]

    def append_section(name: str, typ: int, payload: bytes, *, flags: int = 0,
                       link: int = 0, info: int = 0, align: int = 1, entsize: int = 0) -> int:
        while len(file_data) % align:
            file_data.append(0)
        offset = len(file_data)
        file_data.extend(payload)
        sections.append((shname[name], typ, flags, 0, offset, len(payload), link,
                         info, align, entsize))
        return len(sections) - 1

    text_idx = append_section(".text", rewrite.SHT_PROGBITS, bytes(text),
                              flags=rewrite.SHF_EXECINSTR, align=4)
    rela_idx = append_section(".rela.text", rewrite.SHT_RELA, rela,
                              link=4, info=text_idx, align=8, entsize=24)
    shstr_idx = append_section(".shstrtab", rewrite.SHT_STRTAB, shstr)
    symtab_idx = append_section(".symtab", rewrite.SHT_SYMTAB, symtab,
                                link=5, info=1, align=8, entsize=24)
    strtab_idx = append_section(".strtab", rewrite.SHT_STRTAB, strtab)
    assert (text_idx, rela_idx, shstr_idx, symtab_idx, strtab_idx) == (1, 2, 3, 4, 5)
    while len(file_data) % 8:
        file_data.append(0)
    shoff = len(file_data)
    for item in sections:
        file_data.extend(struct.pack("<IIQQQQIIQQ", *item))
    ident = b"\x7fELF" + bytes((2, 1, 1, 0, 0)) + bytes(7)
    header = struct.pack("<HHIQQQIHHHHHH", rewrite.ET_REL, rewrite.EM_X86_64, 1,
                         0, 0, shoff, 0, 64, 0, 0, 64, len(sections), 3)
    file_data[:64] = ident + header
    return bytes(file_data)


class ETRELObserverTests(unittest.TestCase):
    def test_admitted_fixture_verifies_sites_and_relocation_contract(self) -> None:
        data = make_fixture()
        manifest = rewrite.inspect_admitted(data, hashlib.sha256(data).hexdigest())
        self.assertEqual(manifest["rewrite"]["candidate_status"], "NO_CANDIDATE")
        self.assertEqual([s["section_offset"] for s in manifest["sites"]],
                         ["0xb58a2", "0xb1471", "0x9873f"])
        self.assertEqual(manifest["sites"][1]["original_bytes"], "6639d07506")
        self.assertEqual(manifest["sites"][1]["overwrite_length"], 5)
        self.assertIn("JNE target 0xb147c", manifest["sites"][1]["replay_requirement"])
        self.assertEqual(manifest["sites"][2]["call_relocation"]["symbol"], "phy_reg_mod")
        self.assertEqual(manifest["sites"][2]["call_relocation"]["addend"], -4)
        self.assertEqual(manifest["payload_entry"], {
            "symbol": "obsv_sub_9868f_base", "section": ".text",
            "section_offset": "0x9868f",
            "entry_bytes": "554889e541564188d6415541544989fc",
            "verified": True,
        })

    def test_rewrite_adds_hooks_moves_payload_relocation_and_is_deterministic(self) -> None:
        data = make_fixture()
        before = hashlib.sha256(data).hexdigest()
        expected_hash = before
        candidate, manifest = rewrite.rewrite_admitted(data, expected_hash)
        candidate_again, manifest_again = rewrite.rewrite_admitted(data, expected_hash)
        self.assertEqual(candidate, candidate_again)
        self.assertEqual(manifest["rewrite"]["candidate_sha256"],
                         hashlib.sha256(candidate).hexdigest())
        self.assertEqual(manifest["rewrite"]["candidate_status"],
                         "CANDIDATE_PENDING_STATIC_REVIEW")
        self.assertFalse(manifest["rewrite"]["output_is_runnable"])
        self.assertEqual(hashlib.sha256(data).hexdigest(), before)
        self.assertEqual(manifest["rewrite"], manifest_again["rewrite"])

        elf = rewrite.ELF64Relocatable(candidate)
        text = elf.text()
        self.assertEqual(elf.read_text(0xB58A2, 7), bytes.fromhex("e9000000009090"))
        self.assertEqual(elf.read_text(0xB1471, 5), bytes.fromhex("e900000000"))
        self.assertEqual(elf.read_text(0x9873F, 5), bytes.fromhex("e900000000"))
        symbol_table = elf.symbol_tables[next(iter(elf.symbol_tables))]
        by_name = {s.name: s for s in symbol_table if s.name}
        for name in ("obsv_f9c_thunk", "obsv_calstate_thunk", "obsv_payload_thunk"):
            self.assertEqual(by_name[name].shndx, 0)
            self.assertEqual(by_name[name].info >> 4, 1)
        rela = next(s for s in elf.sections if s.typ == rewrite.SHT_RELA and s.info == text.index)
        records = rewrite._rela_records(elf, rela)
        hook_at = {r.offset: symbol_table[r.symbol_index].name for r in records}
        self.assertEqual(hook_at[0xB58A3], "obsv_f9c_thunk")
        self.assertEqual(hook_at[0xB1472], "obsv_calstate_thunk")
        self.assertEqual(hook_at[0x98740], "obsv_payload_thunk")
        self.assertEqual(by_name["obsv_phy_reg_mod"].value, 0xB2D55)
        self.assertEqual(by_name["obsv_sub_9868f_base"].shndx, text.index)
        self.assertEqual(by_name["obsv_sub_9868f_base"].value, 0x9868F)
        self.assertEqual(by_name["obsv_sub_9868f_base"].info >> 4, 1)
        self.assertEqual(manifest["rewrite"]["thunk_contract"]["payload"]["caller_identity"], {
            "value": "*(RBP+8) - obsv_sub_9868f_base",
            "stored_form": "signed caller_delta only; no raw kernel pointer",
        })

    def test_rejects_unadmitted_hash_before_elf_parse(self) -> None:
        with self.assertRaisesRegex(rewrite.AdmissionError, "not admitted"):
            rewrite.inspect_admitted(b"not elf")

    def test_rejects_changed_site_bytes(self) -> None:
        data = bytearray(make_fixture())
        elf = rewrite.ELF64Relocatable(bytes(data))
        text = elf.text()
        data[text.offset + 0xB58A4] ^= 1
        mutated = bytes(data)
        with self.assertRaisesRegex(rewrite.AdmissionError, "instruction bytes mismatch"):
            rewrite.inspect_admitted(mutated, hashlib.sha256(mutated).hexdigest())

    def test_rejects_changed_sub_9868f_entry_bytes(self) -> None:
        data = bytearray(make_fixture())
        elf = rewrite.ELF64Relocatable(bytes(data))
        data[elf.text().offset + rewrite.SUB_9868F_ENTRY_OFFSET] ^= 1
        mutated = bytes(data)
        with self.assertRaisesRegex(rewrite.AdmissionError, "sub_9868f entry bytes mismatch"):
            rewrite.inspect_admitted(mutated, hashlib.sha256(mutated).hexdigest())

    def test_rejects_missing_or_wrong_payload_relocation(self) -> None:
        data = bytearray(make_fixture())
        # Relocation section is section 2; the addend is the final signed qword.
        elf = rewrite.ELF64Relocatable(bytes(data))
        rela = elf.sections[2]
        struct.pack_into("<q", data, rela.offset + 16, -3)
        mutated = bytes(data)
        with self.assertRaisesRegex(rewrite.AdmissionError, "exactly one"):
            rewrite.inspect_admitted(mutated, hashlib.sha256(mutated).hexdigest())

    def test_rejects_truncated_section_table(self) -> None:
        data = make_fixture()[:80]
        with self.assertRaisesRegex(rewrite.AdmissionError, "outside the input"):
            rewrite.ELF64Relocatable(data)


if __name__ == "__main__":
    unittest.main()

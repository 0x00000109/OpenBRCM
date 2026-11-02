#!/usr/bin/env python3
"""Independent checks for corrected sub_99658/sub_998cc TABLE ABI."""
import hashlib
import json
import subprocess
import unittest
from pathlib import Path

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
HOST = ROOT / "tests/host"
VENDOR = Path("/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/vendor/wlc_hybrid.o_shipped")
WLKO = Path("/media/kartashoff/Storage/research-inputs/openbrcm/historical-20261008/wlko/wl.ko")
VENDOR_SHA = "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"
WLKO_SHA = "72668bdb65d0108682781dc23b5bc544d5c6eea4ae1646eb03f4f23c6bebbcec"
ROWS = {
    1: (0x99699, 0xD9DC0, 6, "76c786a55e3e3ce06353278061379d16748512fce0fcd1483dc118ce5789ade4"),
    2: (0x996AF, 0xD9DC6, 6, "525b1f4d63aae24ba2d93ab8e67ea46124c73fe2af29a4f18533a8d937334d77"),
    3: (0x996BE, 0xD9DCC, 6, "525b1f4d63aae24ba2d93ab8e67ea46124c73fe2af29a4f18533a8d937334d77"),
    4: (0x996C5, 0xD9DD2, 6, "525b1f4d63aae24ba2d93ab8e67ea46124c73fe2af29a4f18533a8d937334d77"),
    5: (0x996FC, 0xD9DDF, 7, "6aeb250c5df98103b55ab1a7443693a9744ae50a3ee93fb420a4cbf9924a5d51"),
    6: (0x996F5, 0xD9DD8, 7, "9c0bda47e5ec86170b34f70fd703236094b995dd34944f0a8ae3b5781dea18d4"),
    7: (0x996E3, 0xD9DE6, 7, "0915b07e80ebf162618bd0008b5402d2b6a8f26f54904a0c22d01e535105ea90"),
}


def historical_row(row_id):
    addend, length = ROWS[row_id][1:3]
    with VENDOR.open("rb") as stream:
        elf = ELFFile(stream)
        return elf.get_section_by_name(".data").data()[addend:addend + length]


def independent_payload(arg, source, chanspec, nested_66a, nested_66b,
                        nested_66e, nested_340, nested_3e0):
    """The loop only materializes the count bytes passed to the TABLE API."""
    count = 6 if arg == 1 else 7
    if arg == 1:
        base = 5
        delta = max(5 - nested_66a, 0)
    elif (chanspec & 0xC000) or nested_66e or not nested_340:
        base = 6
        delta = max(base - nested_66b, 0)
    else:
        base = 5 if nested_3e0 < 10 else 4
        delta = max(base - nested_66b, 0)
    a, b = bytearray(), bytearray()
    for i in range(count):
        if i == 0:
            source_index, index_value = 1, 1
        elif i <= delta:
            source_index, index_value = i, i
        else:
            source_index, index_value = delta, delta
        a.append(source[source_index])
        b.append(index_value)
    return bytes(a), bytes(b)


class Sub99658EvidenceAndOracle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "-B", "-C", str(HOST), "ob_phy_99658_test"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        cls.probe = HOST / "ob_phy_99658_test"
        subprocess.run([str(cls.probe)], cwd=ROOT, check=True,
                       capture_output=True, text=True)

    def test_binary_admission_and_call_displacements(self):
        self.assertEqual(hashlib.sha256(VENDOR.read_bytes()).hexdigest(), VENDOR_SHA)
        self.assertEqual(hashlib.sha256(WLKO.read_bytes()).hexdigest(), WLKO_SHA)
        for image in (VENDOR, WLKO):
            with image.open("rb") as stream:
                text = ELFFile(stream).get_section_by_name(".text").data()
            for site in (0x9B123, 0x9B13D):
                insn = text[site:site + 5]
                self.assertEqual(insn[0], 0xE8)
                disp = int.from_bytes(insn[1:5], "little", signed=True)
                self.assertEqual(site + 5 + disp, 0x99658)
            self.assertEqual(text[0x99795:0x9979A],
                             bytes.fromhex("44 38 c7 76 0c"))
            self.assertEqual(text[0x99763:0x99773],
                             bytes.fromhex("45 29 c8 bf 00 00 00 00 4c 8d 55 c0 44 0f 48 c7"))

    def test_static_payload_relocations_are_pinned(self):
        with VENDOR.open("rb") as stream:
            elf = ELFFile(stream)
            data = elf.get_section_by_name(".data")
            relsec = elf.get_section_by_name(".rela.text")
            symtab = elf.get_section(relsec["sh_link"])
            relocs = {r["r_offset"]: r for r in relsec.iter_relocations()}
            for row_id, (site, addend, length, digest) in ROWS.items():
                reloc = relocs[site]
                symbol = symtab.get_symbol(reloc["r_info_sym"])
                self.assertEqual(reloc.entry.r_info_type, 11)
                self.assertEqual(symbol["st_shndx"], elf.get_section_index(".data"))
                self.assertEqual(reloc["r_addend"], addend)
                self.assertEqual(hashlib.sha256(data.data()[addend:addend + length]).hexdigest(), digest)

    def test_corrected_api_semantics_and_immutable_predecessors(self):
        correction_path = ROOT / "artifacts/contracts/bcm4352-sub-99658-table-api-abi-correction-v1.json"
        correction = json.loads(correction_path.read_text())
        self.assertEqual(correction["schema"], "openbrcm-bcm4352-sub-99658-table-api-correction/v1")
        self.assertEqual(correction["corrected_api_arguments"], {
            "table_id": "ESI", "element_count": "EDX", "element_offset": "ECX",
            "element_width_bits": "R8D", "payload_pointer": "R9",
        })
        self.assertEqual([(op["argument"], op["element_count"], op["offset"],
                           op["width_bits"], op["payload_bytes"])
                          for op in correction["selected_transfers"][:2]],
                         [(1, 6, 8, 8, 6), (2, 7, 16, 8, 7)])
        self.assertTrue(all(op.get("helper") == "sub_998cc"
                            for op in correction["selected_transfers"][2:]))
        for rel, expected in correction["immutable_predecessors"].items():
            actual = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
            self.assertEqual(actual, expected, rel)

    def test_c_matches_independent_selected_payload_for_all_rows(self):
        scenarios = [
            (1, 1, 0x1001, 2, 2, 0, 1, 7, 8),
            (1, 2, 0x5001, 2, 2, 0, 1, 7, 8),
            (1, 3, 0x5801, 2, 2, 0, 1, 7, 8),
            (1, 4, 0x6001, 0, 2, 0, 1, 7, 8),
            (2, 5, 0x1001, 2, 2, 0, 1, 7, 16),
            (2, 6, 0x1001, 2, 2, 1, 1, 7, 16),
            (2, 7, 0x5001, 2, 2, 0, 1, 7, 16),
            (2, 5, 0x1001, 2, 2, 0, 1, 10, 16),
        ]
        for arg, row, chanspec, a66a, b66b, e66e, b340, b3e0, offset in scenarios:
            source = historical_row(row)
            a, b = independent_payload(arg, source, chanspec, a66a, b66b, e66e, b340, b3e0)
            out = subprocess.run([str(self.probe), "--probe", str(arg), str(row),
                                  hex(chanspec), str(a66a), str(b66b), str(e66e),
                                  str(b340), str(b3e0), str(offset), source.hex()],
                                 cwd=ROOT, check=True, capture_output=True,
                                 text=True).stdout.splitlines()
            expected = []
            for chain, table, site, payload in (
                (0, 0x44, 0x99875, a), (0, 0x45, 0x998AA, b),
                (1, 0x64, 0x99875, a), (1, 0x65, 0x998AA, b),
            ):
                expected.append(
                    f"{site:08x},{arg},{chain},{table:02x},{offset},{len(payload)},8,{len(payload)},{payload.hex()}"
                )
            self.assertEqual(out, expected)

    def test_table_stage_uses_exact_initialized_byte_counts(self):
        lines = subprocess.run([str(self.probe), "--dump-stage"], cwd=ROOT,
                               check=True, capture_output=True, text=True).stdout.splitlines()
        self.assertEqual(len(lines), 10)
        actual = [tuple([int(x, 16) if i in (0, 3, 8) else int(x)
                         for i, x in enumerate(line.split(",")[:8])])
                  for line in lines]
        expected = [
            (0x99875, 1, 0, 0x44, 8, 6, 8, 6),
            (0x998AA, 1, 0, 0x45, 8, 6, 8, 6),
            (0x99875, 1, 1, 0x64, 8, 6, 8, 6),
            (0x998AA, 1, 1, 0x65, 8, 6, 8, 6),
            (0x99981, 1, 0, 0x0B, 8, 6, 8, 6),
            (0x99875, 2, 0, 0x44, 16, 7, 8, 7),
            (0x998AA, 2, 0, 0x45, 16, 7, 8, 7),
            (0x99875, 2, 1, 0x64, 16, 7, 8, 7),
            (0x998AA, 2, 1, 0x65, 16, 7, 8, 7),
            (0x99981, 2, 0, 0x0B, 16, 7, 8, 7),
        ]
        self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()

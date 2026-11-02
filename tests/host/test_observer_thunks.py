"""Static admission tests for the standalone x86-64 observer thunk object."""

from __future__ import annotations

import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ASM = ROOT / "tools" / "etrel-observer" / "observer_thunks.S"
VERIFY = ROOT / "tools" / "etrel-observer" / "verify_thunks.py"


@unittest.skipUnless(shutil.which("gcc") and shutil.which("objdump") and
                     shutil.which("readelf") and shutil.which("objcopy"),
                     "requires x86-64 binutils and gcc")
class ObserverThunkStaticTests(unittest.TestCase):
    def assemble(self, out: Path) -> None:
        subprocess.run(["gcc", "-c", "-x", "assembler", str(ASM), "-o", str(out)],
                       check=True, capture_output=True, text=True)

    @staticmethod
    def mutate_section_byte(obj: Path, section_name: str, relative_offset: int, value: int) -> None:
        raw = bytearray(obj.read_bytes())
        shoff = struct.unpack_from("<Q", raw, 40)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", raw, 58)
        shstr_header = struct.unpack_from("<IIQQQQIIQQ", raw, shoff + shstrndx * shentsize)
        strings = raw[shstr_header[4]:shstr_header[4] + shstr_header[5]]
        for index in range(shnum):
            header = struct.unpack_from("<IIQQQQIIQQ", raw, shoff + index * shentsize)
            name_start = header[0]
            name_end = strings.index(0, name_start)
            if strings[name_start:name_end].decode("ascii") == section_name:
                if relative_offset >= header[5]:
                    raise AssertionError("mutation offset falls outside section")
                raw[header[4] + relative_offset] = value
                obj.write_bytes(raw)
                return
        raise AssertionError(f"section not found: {section_name}")

    @staticmethod
    def mutate_section_pattern(obj: Path, section_name: str, pattern: bytes,
                               replacement: bytes, occurrence: int = 0) -> None:
        raw = bytearray(obj.read_bytes())
        shoff = struct.unpack_from("<Q", raw, 40)[0]
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", raw, 58)
        shstr_header = struct.unpack_from("<IIQQQQIIQQ", raw, shoff + shstrndx * shentsize)
        strings = raw[shstr_header[4]:shstr_header[4] + shstr_header[5]]
        for index in range(shnum):
            header = struct.unpack_from("<IIQQQQIIQQ", raw, shoff + index * shentsize)
            begin, end = header[4], header[4] + header[5]
            name_start = header[0]
            name_end = strings.index(0, name_start)
            if strings[name_start:name_end].decode("ascii") != section_name:
                continue
            section = raw[begin:end]
            hits = []
            offset = 0
            while True:
                offset = section.find(pattern, offset)
                if offset < 0:
                    break
                hits.append(offset)
                offset += 1
            if occurrence >= len(hits):
                raise AssertionError(f"pattern not found in {section_name}: {pattern.hex()}")
            position = begin + hits[occurrence]
            if len(pattern) != len(replacement):
                raise AssertionError("replacement must preserve instruction-section size")
            raw[position:position + len(replacement)] = replacement
            obj.write_bytes(raw)
            return
        raise AssertionError(f"section not found: {section_name}")

    def test_thunk_object_passes_static_contract(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            result = subprocess.run(["python3", str(VERIFY), str(obj)],
                                    check=False, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("OBSERVER_THUNKS_STATIC_PASS", result.stdout)
            self.assertIn("payload=return-PC-minus-sub_9868f-base", result.stdout)
            self.assertIn("f9c/cal=zero", result.stdout)

    def test_verifier_rejects_changed_opcode(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            # Corrupt an instruction byte in a thunk's executable section.
            # The verifier must reject the disassembly contract, not accept a
            # symbol-only object.
            text_dump = subprocess.run(["objdump", "-h", str(obj)], check=True,
                                       capture_output=True, text=True).stdout
            self.assertIn(".text.obsv_f9c_thunk", text_dump)
            self.mutate_section_byte(obj, ".text.obsv_f9c_thunk", 0, 0x90)
            result = subprocess.run(["python3", str(VERIFY), str(obj)],
                                    check=False, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("FAIL", result.stderr)

    def test_verifier_requires_payload_rflags_save_at_entry(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            # PUSHF is the first byte (0x9c); replacing it with an otherwise
            # allowed PUSH must fail the verifier's entry-RFLAGS requirement.
            payload = subprocess.run(["objcopy", "--dump-section",
                                      f".text.obsv_payload_thunk={tmp}/payload.bin", str(obj)],
                                     check=True, capture_output=True, text=True)
            self.assertEqual(payload.returncode, 0)
            self.assertEqual((Path(tmp) / "payload.bin").read_bytes()[0], 0x9C)
            self.mutate_section_byte(obj, ".text.obsv_payload_thunk", 0, 0x50)
            result = subprocess.run(["python3", str(VERIFY), str(obj)],
                                    check=False, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("register-only instruction sequence changed", result.stderr)

    def test_verifier_requires_lock_prefixed_bounded_reservation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            self.mutate_section_pattern(obj, ".text.obsv_f9c_thunk",
                                        bytes.fromhex("f0 0f b1 15"),
                                        bytes.fromhex("90 0f b1 15"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("reservation, overflow, or publish", result.stderr)

    def test_verifier_rejects_retry_bound_change(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            self.mutate_section_pattern(obj, ".text.obsv_f9c_thunk",
                                        bytes.fromhex("41 ba 04 00 00 00"),
                                        bytes.fromhex("41 ba 05 00 00 00"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("retry/capacity bound changed", result.stderr)

    def test_verifier_rejects_wrong_sub9868f_caller_frame_offset(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            self.mutate_section_pattern(obj, ".text.obsv_payload_thunk",
                                        bytes.fromhex("48 8b 45 08"),
                                        bytes.fromhex("48 8b 45 10"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("memory access/read-write allowlist changed", result.stderr)

    def test_verifier_rejects_raw_payload_return_address_storage(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            # Without normalization, the next store would leak a raw kernel PC.
            self.mutate_section_pattern(obj, ".text.obsv_payload_thunk",
                                        bytes.fromhex("48 29 d0"),
                                        bytes.fromhex("90 90 90"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("FAIL", result.stderr)

    def test_verifier_requires_original_calstate_registers(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            self.mutate_section_pattern(obj, ".text.obsv_calstate_thunk",
                                        bytes.fromhex("0f b7 44 24 18"),
                                        bytes.fromhex("0f b7 44 24 20"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("memory access/read-write allowlist changed", result.stderr)

    def test_verifier_rejects_ring_capacity_change(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            self.mutate_section_pattern(obj, ".text.obsv_f9c_thunk",
                                        bytes.fromhex("83 f8 10"), bytes.fromhex("83 f8 0f"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("retry/capacity bound changed", result.stderr)

    def test_verifier_rejects_missing_publish_last_sequence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            self.mutate_section_pattern(obj, ".text.obsv_f9c_thunk",
                                        bytes.fromhex("45 89 1a"), bytes.fromhex("90 90 90"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("reservation, overflow, or publish", result.stderr)

    def test_verifier_rejects_write_to_predicate_source(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / "observer_thunks.o"
            self.assemble(obj)
            # Replace the f9c byte load with a store using the same-sized
            # instruction window. The source field must remain read-only.
            self.mutate_section_pattern(obj, ".text.obsv_f9c_thunk",
                                        bytes.fromhex("0f b6 83 9c 0f 00 00"),
                                        bytes.fromhex("88 9c 23 9c 0f 00 00"))
            result = subprocess.run(["python3", str(VERIFY), str(obj)], check=False,
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("FAIL:", result.stderr)


if __name__ == "__main__":
    unittest.main()

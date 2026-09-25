#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""
Deterministic decoder for the vendor ``srom_var_init`` NVRAM-synthesis table.

``srom_var_init`` (vendor blob, function ``0x9704``) converts the external
SPROM word image into the textual NVRAM variables that ``getvar()``/
``getintvar()`` later scan.  It does NOT decode fixed raw offsets in code:
it walks a 24-byte-per-entry descriptor table in ``.rodata`` (base
``.rodata+0x1b00``) whose entries name a NVRAM variable and point at a 16-bit
word of the already-read SPROM image plus a bit mask.  This is the exact
"computed cursor" that could not be explained with a hand-maintained offset
table.

This tool reconstructs that table from the *pinned* vendor blob and, given a
234-word image, reproduces the vendor extraction exactly (including multi-word
continuations and the vendor "field equals all-ones -> variable absent" rule).
It performs NO hardware access.

Provenance (all from the pinned blob, sha256 in ``BLOB_SHA256``):
  * dispatcher: ``srom_var_init`` ``0x9704`` -> table base relocation at
    ``0x9a14`` (``.rodata+0x1b00``), entry stride ``0x18``;
  * field extractor: ``0x9af7`` (word = sprom[w10], mask = w12);
  * continuation: ``0x9bc2`` bit0 of the entry flags extends to the next entry;
  * absent rule:   ``0x9bcd`` bit1 -> skip when value == (1<<width)-1;
  * revision gate: ``0x9a37`` (``revmask & (1<<rev)``), rev from the word
    image tail (word 233 low byte), as read by ``ob_si_read_mac()``.

The 24-byte entry layout (little-endian):
    +0x00  u64  name (relocated pointer into .rodata.str1.1)
    +0x08  u32  revmask   (bit r => applies to SPROM revision r)
    +0x0c  u16  flags + u16 high word reserved  (only low byte used)
    +0x10  u16  word_index  (index into the 16-bit SPROM image)
    +0x12  u16  mask
    +0x14  u16  reserved (0)

Usage:
    srom_var_table.py --blob wlc_hybrid.o_shipped --json
    srom_var_table.py --blob wlc_hybrid.o_shipped --rev 11 --json
    srom_var_table.py --image sprom11.json --rev 11 --json   # synthesize vars
"""
from __future__ import annotations

import argparse
import json
import struct
import sys

BLOB_SHA256 = "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"
DEFAULT_BLOB = "/media/kartashoff/Storage/opensource/iced/test/wlc_hybrid.o_shipped"
TABLE_RODATA_OFF = 0x1B00
ENTRY_SIZE = 24
WORD_BITS = 16


class TableError(Exception):
    pass


def blob_sha256(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _ctz(mask):
    if mask == 0:
        return 0
    return (mask & -mask).bit_length() - 1


def _width(mask):
    if mask == 0:
        return 0
    return mask.bit_length() - _ctz(mask)


def _parse_elf(path):
    try:
        from elftools.elf.elffile import ELFFile
    except Exception as exc:  # pragma: no cover - environment dependency
        raise TableError("pyelftools is required: %s" % exc)

    with open(path, "rb") as fh:
        elf = ELFFile(fh)
        ro = elf.get_section_by_name(".rodata")
        rs = elf.get_section_by_name(".rodata.str1.1")
        rela = elf.get_section_by_name(".rela.rodata")
        symtab = elf.get_section_by_name(".symtab")
        if not (ro and rs and rela and symtab):
            raise TableError("required ELF sections missing")
        rodata = ro.data()
        strings = rs.data()

        def cstr_at(off):
            if off >= len(strings):
                return ""
            end = strings.find(b"\x00", off)
            return strings[off:end if end >= 0 else len(strings)].decode(
                "latin1")

        # offset in .rodata -> (section-name, addend) of the 64-bit relocation
        relocs = {}
        for r in rela.iter_relocations():
            si = r["r_info_sym"]
            if si == 0:
                continue
            sym = symtab.get_symbol(si)
            try:
                sec = elf.get_section(sym["st_shndx"]).name
            except Exception:
                sec = "?"
            relocs[r["r_offset"]] = (sec, r["r_addend"])

        return rodata, relocs, cstr_at


def parse_table(blob, expected_sha256=BLOB_SHA256, table_off=TABLE_RODATA_OFF):
    """Parse the descriptor table.  Returns (meta, entries)."""
    if expected_sha256:
        got = blob_sha256(blob)
        if got != expected_sha256:
            raise TableError(
                "vendor blob sha256 %s != pinned %s" % (got, expected_sha256))

    rodata, relocs, cstr_at = _parse_elf(blob)
    entries = []
    index = 0
    while True:
        off = table_off + index * ENTRY_SIZE
        if off + ENTRY_SIZE > len(rodata):
            raise TableError("table ran past .rodata without a terminator")
        raw = rodata[off:off + ENTRY_SIZE]
        name = ""
        if off in relocs:
            sec, addend = relocs[off]
            if sec == ".rodata.str1.1":
                name = cstr_at(addend)
            else:
                name = "%s+0x%x" % (sec, addend)
        revmask, flags = struct.unpack_from("<II", raw, 8)
        word_index, mask, reserved = struct.unpack_from("<HHH", raw, 16)
        if not name and revmask == 0 and flags == 0 and \
                word_index == 0 and mask == 0 and reserved == 0:
            break
        entries.append({
            "index": index,
            "name": name,
            "revmask": revmask,
            "flags": flags,
            "word_index": word_index,
            "mask": mask,
            "shift": _ctz(mask),
            "width": _width(mask),
            "continuation": bool(flags & 0x1),
            "skip_all_ones": bool(flags & 0x2),
            "hex_output": bool(flags & 0x4),
            "output_transform": _output_transform(flags),
            "rodata_offset": off,
        })
        index += 1

    meta = {
        "blob": blob,
        "blob_sha256": expected_sha256 if expected_sha256 else blob_sha256(blob),
        "table": ".rodata+0x%x" % table_off,
        "entry_size": ENTRY_SIZE,
        "entry_count": len(entries),
    }
    return meta, entries


def _output_transform(flags):
    if flags & 0x0100:
        return "multi_word_string"
    if flags & 0x0040:
        return "u16_byteswap"
    if flags & 0x0020:
        return "macaddr_big_endian"
    if flags & 0x0010:
        return "ccode_chars"
    if flags & 0x0008:
        return "bit_expand"
    return "direct"


def fields_for_revision(entries, revision):
    """Collapse continuation groups into one field per emitted variable."""
    bit = 1 << revision
    fields = []
    i = 0
    n = len(entries)
    while i < n:
        e = entries[i]
        if not (e["revmask"] & bit):
            i += 1
            continue
        parts = [{
            "word_index": e["word_index"],
            "mask": e["mask"],
            "shift": e["shift"],
            "width": e["width"],
            "bit_offset": 0,
        }]
        j = i
        while entries[j]["continuation"] and j + 1 < n:
            nxt = entries[j + 1]
            parts.append({
                "word_index": nxt["word_index"],
                "mask": nxt["mask"],
                "shift": nxt["shift"],
                "width": nxt["width"],
                "bit_offset": parts[-1]["bit_offset"] + parts[-1]["width"],
            })
            j += 1
        fields.append({
            "name": e["name"],
            "table_entry": e["index"],
            "rodata_offset": e["rodata_offset"],
            "revmask": e["revmask"],
            "flags": e["flags"],
            "hex_output": e["hex_output"],
            "skip_all_ones": e["skip_all_ones"],
            "output_transform": e["output_transform"],
            "word_index": e["word_index"],
            "mask": e["mask"],
            "shift": e["shift"],
            "width": parts[0]["width"],
            "parts": parts,
            "provenance": (
                "vendor srom_var_init 0x9704; table .rodata+0x%x[%d]"
                % (TABLE_RODATA_OFF, e["index"])),
        })
        i = j + 1
    return fields


def field_value(words, field):
    """Reproduce the vendor extraction.  Returns (value, present).

    ``value`` is always the extracted field; ``present`` is False when the
    vendor's skip-all-ones rule (0x9bcd) omits the NVRAM variable (a consumer
    such as getvar() then sees it as 0).
    """
    value = 0
    for part in field["parts"]:
        wi = part["word_index"]
        if wi >= len(words):
            return 0, False
        raw = words[wi] & 0xFFFF
        v = (raw & part["mask"]) >> part["shift"]
        value |= v << part["bit_offset"]
    present = True
    if field["skip_all_ones"]:
        allones = (1 << sum(p["width"] for p in field["parts"])) - 1
        if value == allones:
            present = False
    return value, present


def synthesize(words, entries, revision):
    """Return {name: {value, present, field}} for one revision."""
    out = {}
    for field in fields_for_revision(entries, revision):
        value, present = field_value(words, field)
        out[field["name"]] = {"value": value, "present": present,
                              "field": field}
    return out


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def _load_image(path):
    with open(path, "r", encoding="utf-8") as fh:
        doc = json.load(fh)
    if isinstance(doc, list):
        return [int(v) for v in doc]
    for key in ("raw_image", "words", "image"):
        if key in doc:
            return [int(v) for v in doc[key]]
    raise TableError("image JSON has no raw_image/words list")


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--blob", default=DEFAULT_BLOB)
    ap.add_argument("--rev", type=int, default=None,
                    help="SPROM revision (e.g. 11); emits the field map")
    ap.add_argument("--image", metavar="JSON",
                    help="234-word image JSON to synthesize variables from")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--no-sha-check", action="store_true")
    args = ap.parse_args(argv)

    meta, entries = parse_table(
        args.blob, None if args.no_sha_check else BLOB_SHA256)

    result = dict(meta)
    result["entries"] = entries
    if args.rev is not None:
        result["fields"] = fields_for_revision(entries, args.rev)
    if args.image:
        words = _load_image(args.image)
        rev = args.rev
        if rev is None:
            rev = words[-1] & 0xFF
        syn = synthesize(words, entries, rev)
        result["revision"] = rev
        result["variables"] = {
            k: {"value": v["value"], "present": v["present"]}
            for k, v in syn.items()}

    if args.json:
        print(json.dumps(result, indent=2, sort_keys=False))
    else:
        print("blob sha256 %s" % result["blob_sha256"])
        print("table %s entries=%d" % (result["table"],
                                       result["entry_count"]))
        if args.rev is not None:
            print("fields for revision %d: %d" % (args.rev,
                                                  len(result["fields"])))
            for f in result["fields"]:
                print("  %-24s word=0x%-3x mask=0x%04x present_rule=%s" % (
                    f["name"], f["word_index"], f["mask"],
                    "skip-all-ones" if f["skip_all_ones"] else "always"))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except TableError as exc:
        print("srom_var_table: error: %s" % exc, file=sys.stderr)
        sys.exit(2)

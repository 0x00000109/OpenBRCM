#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""
OpenBRCM rev11 external-SPROM evidence decoder (D3B / MHF3).

Consumes the raw 234-word image emitted by the kernel as
``openbrcm: sprom11: ...`` log lines (see ``ob_si_emit_sprom11()`` in
``src/ob_si.c``).  It performs NO hardware access.

It:
  * parses the BEGIN/data/END block deterministically;
  * validates the word count (234), the revision (11) and the CRC using the
    exact OpenBRCM/bcma rule (``ob_sprom_crc`` in ``src/ob_si.c``);
  * preserves the complete raw image (JSON);
  * decodes the proven rev11 field map recovered from the vendor
    ``srom_var_init`` descriptor table (``srom_var_table.py`` /
    ``docs/m34d3b/rev11_sprom_fields.json``) -- never a guessed layout;
  * reproduces the vendor ``wlc_antsel_attach`` ``antsel_type`` function
    exactly and maps it to the D3B MHF3 value.

The rev11 raw mapping that was the MHF3 blocker is now recovered; a captured
234-word image decodes directly to boardtype/boardflags/aa2g/aa5g/antswitch,
antsel_type and MHF3.  See ``docs/m34d3b_sprom_evidence.md`` and
``docs/m34d3b_band_init.md``.
"""
from __future__ import annotations

import argparse
import json
import re
import sys

WORDS_R11 = 234
REV11 = 11

# --------------------------------------------------------------------------
# CRC: identical to OpenBRCM ob_sprom_crc8()/ob_sprom_crc() and bcma_sprom_crc8
# (reflected CRC-8, polynomial 0xAB; table[crc ^ data], final ^0xFF).
# --------------------------------------------------------------------------
def _crc8_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = ((c >> 1) ^ 0xAB) & 0xFF if (c & 1) else (c >> 1)
        table.append(c)
    return table


_CRC8_TABLE = _crc8_table()


def sprom_crc(words):
    """OpenBRCM/BCMA SPROM CRC over ``words`` (final word low byte only)."""
    crc = 0xFF
    for w in words[:-1]:
        crc = _CRC8_TABLE[(crc ^ (w & 0xFF)) & 0xFF]
        crc = _CRC8_TABLE[(crc ^ ((w >> 8) & 0xFF)) & 0xFF]
    crc = _CRC8_TABLE[(crc ^ (words[-1] & 0xFF)) & 0xFF]
    return crc ^ 0xFF


# --------------------------------------------------------------------------
# Log parsing / serialization
# --------------------------------------------------------------------------
_BEGIN_RE = re.compile(
    r"openbrcm: sprom11: BEGIN words=(\d+) revision=(\d+) "
    r"crc=([0-9a-fA-F]{1,2}) calc=([0-9a-fA-F]{1,2}) valid=(\d+)")
_DATA_RE = re.compile(r"openbrcm: sprom11: (\d+):((?:\s+[0-9a-fA-F]{1,4})+)\s*$")
_END_RE = re.compile(r"openbrcm: sprom11: END words=(\d+)")


class SpromError(Exception):
    pass


def parse_log(text):
    """Parse one ``sprom11`` block.  Returns (header, words)."""
    header = None
    words = {}
    in_block = False
    saw_end = False

    for line in text.splitlines():
        line = line.rstrip("\r")
        m = _BEGIN_RE.search(line)
        if m:
            if in_block:
                raise SpromError("nested sprom11 BEGIN")
            in_block = True
            header = {
                "words": int(m.group(1)),
                "revision": int(m.group(2)),
                "crc": int(m.group(3), 16),
                "calc": int(m.group(4), 16),
                "valid": int(m.group(5)),
            }
            continue
        if not in_block:
            continue
        m = _END_RE.search(line)
        if m:
            saw_end = True
            in_block = False
            continue
        m = _DATA_RE.search(line)
        if m:
            idx = int(m.group(1))
            toks = m.group(2).split()
            for k, tok in enumerate(toks):
                words[idx + k] = int(tok, 16)

    if header is None:
        raise SpromError("no 'openbrcm: sprom11: BEGIN' block found")
    if not saw_end:
        raise SpromError("missing 'openbrcm: sprom11: END'")

    n = header["words"]
    if n != WORDS_R11:
        raise SpromError(f"word count {n} != {WORDS_R11}")
    if sorted(words) != list(range(n)):
        missing = [i for i in range(n) if i not in words]
        raise SpromError(f"non-contiguous/duplicate image; first missing={missing[:3]}")
    return header, [words[i] for i in range(n)]


def serialize(header, words):
    """Deterministic re-serialization (canonical 8-words-per-line form)."""
    out = []
    out.append("openbrcm: sprom11: BEGIN words=%d revision=%d crc=%02x "
               "calc=%02x valid=%d" % (header["words"], header["revision"],
                                       header["crc"], header["calc"],
                                       header["valid"]))
    for i in range(0, len(words), 8):
        chunk = words[i:i + 8]
        out.append("openbrcm: sprom11: %03d:%s" % (
            i, "".join(" %04x" % w for w in chunk)))
    out.append("openbrcm: sprom11: END words=%d" % len(words))
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# Validation
# --------------------------------------------------------------------------
def validate(header, words):
    """Return a dict with recomputed rev/crc and hard errors."""
    errors = []
    if len(words) != WORDS_R11:
        errors.append("word_count")
    rev = words[WORDS_R11 - 1] & 0xFF
    ecrc = (words[WORDS_R11 - 1] >> 8) & 0xFF
    crc = sprom_crc(words)
    if rev != REV11:
        errors.append("revision")
    if crc != ecrc:
        errors.append("crc")
    if header["revision"] != rev:
        errors.append("header_revision")
    if header["crc"] != ecrc:
        errors.append("header_crc")
    if header["calc"] != crc:
        errors.append("header_calc")
    if header["valid"] != (1 if (rev == REV11 and crc == ecrc) else 0):
        errors.append("header_valid")
    return {"revision": rev, "stored_crc": ecrc, "calculated_crc": crc,
            "crc_match": crc == ecrc, "errors": errors,
            "ok": not errors}


# --------------------------------------------------------------------------
# Field decoding (provenance table)
# --------------------------------------------------------------------------
# PROVEN rev11 field map.  Recovered from the vendor srom_var_init
# NVRAM-synthesis descriptor table (``.rodata+0x1b00``), not from a guessed
# layout: see scripts/srom_var_table.py, docs/m34d3b/rev11_sprom_fields.json
# and docs/m34d3b_band_init.md.  Word indices are into the already-read,
# CRC-validated 234-word image (the same array ob_si_read_mac() builds; the
# rev11 MAC anchor at word 0x48 / byte 0x90 is hardware-proven).
#
# Each field is one or more 16-bit "parts"; ``bit_offset`` places a part's
# extracted value in the combined value.  ``skip_all_ones`` reproduces the
# vendor rule at 0x9bcd (value all-ones -> variable not emitted; getvar()
# then sees it as absent/0).
PROVEN_REV11_FIELDS = {
    "boardtype": {
        "parts": [{"word": 0x02, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 0}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[21] (w10=0x02 mask=0xffff); "
                      "= SSB_SPROM1_SPID@0x04",
    },
    "boardflags": {
        "parts": [{"word": 0x42, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 0},
                  {"word": 0x43, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 16}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[13]+[14] continuation "
                      "(SSB_SPROM8_BFLLO/HI@0x84/0x86)",
    },
    "boardflags2": {
        "parts": [{"word": 0x44, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 0},
                  {"word": 0x45, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 16}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[19]+[20] continuation",
    },
    "boardflags3": {
        "parts": [{"word": 0x46, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 0},
                  {"word": 0x47, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 16}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[374]+[375] continuation (rev>=11)",
    },
    "boardrev": {
        "parts": [{"word": 0x41, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 0}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[3] (SSB_SPROM8_BOARDREV@0x82)",
    },
    "boardnum": {
        "parts": [{"word": 0x4A, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 0}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[376] (rev>=11)",
    },
    "ccode": {
        "parts": [{"word": 0x4B, "mask": 0xFFFF, "shift": 0, "width": 16,
                   "bit_offset": 0}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[378] (rev>=11)",
    },
    "regrev": {
        "parts": [{"word": 0x4C, "mask": 0x00FF, "shift": 0, "width": 8,
                   "bit_offset": 0}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[379] (rev>=11)",
    },
    "aa2g": {
        "parts": [{"word": 0x50, "mask": 0x00FF, "shift": 0, "width": 8,
                   "bit_offset": 0}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[385] (rev>=11) = ANTAVAIL_BG",
    },
    "aa5g": {
        "parts": [{"word": 0x50, "mask": 0xFF00, "shift": 8, "width": 8,
                   "bit_offset": 0}],
        "skip_all_ones": False,
        "provenance": "srom_var_init table[386] (rev>=11) = ANTAVAIL_A",
    },
    "txchain": {
        "parts": [{"word": 0x54, "mask": 0x000F, "shift": 0, "width": 4,
                   "bit_offset": 0}],
        "skip_all_ones": True,
        "provenance": "srom_var_init table[393] (rev>=11) = TXRXC_TXCHAIN",
    },
    "rxchain": {
        "parts": [{"word": 0x54, "mask": 0x00F0, "shift": 4, "width": 4,
                   "bit_offset": 0}],
        "skip_all_ones": True,
        "provenance": "srom_var_init table[394] (rev>=11) = TXRXC_RXCHAIN",
    },
    "antswitch": {
        "parts": [{"word": 0x54, "mask": 0xFF00, "shift": 8, "width": 8,
                   "bit_offset": 0}],
        "skip_all_ones": True,
        "provenance": "srom_var_init table[395] (rev>=11) = TXRXC_SWITCH",
    },
}

# Backwards-compatible alias used by older callers/tests.
PROVEN_OFFSETS = PROVEN_REV11_FIELDS

# Candidate offsets from Linux bcma rev8 (NOT rev11-proven; the rev11 MAC is
# known to be +0x90, not the rev8 +0x8C, so the layouts differ).  Never used
# unless --allow-candidate-offsets is given explicitly.
CANDIDATE_OFFSETS_BCMA_REV8 = {
    "boardtype":  {"byte_offset": 0x04, "width": 16, "shift": 0},
    "aa2g":       {"byte_offset": 0x9C, "width": 8,  "shift": 0},
    "aa5g":       {"byte_offset": 0x9C, "width": 8,  "shift": 8},
    # boardflags/antswitch: offset not carried by bcma rev8 extraction.
}


def _read_le(words, byte_offset, width):
    nbytes = (width + 7) // 8
    window = [words[(byte_offset + 2 * i) // 2] for i in range(nbytes // 2)]
    raw = b"".join(w.to_bytes(2, "little") for w in window)
    return int.from_bytes(raw[:nbytes], "little")


def _spec_from_byte_offset(name, spec):
    """Convert a legacy byte-offset spec into the part-based form."""
    bo = spec["byte_offset"]
    width = spec.get("width", 16)
    shift = spec.get("shift", 0)
    nwords = max(1, (width + 15) // 16)
    parts = []
    for i in range(nwords):
        mask = ((1 << min(16, width - 16 * i)) - 1) << shift
        parts.append({"word": (bo + 2 * i) // 2, "mask": mask,
                      "shift": shift, "width": min(16, width - 16 * i),
                      "bit_offset": 16 * i})
    return {"parts": parts, "skip_all_ones": False,
            "provenance": spec.get("provenance", "supplied")}


def decode_field(words, name, spec):
    if "byte_offset" in spec and "parts" not in spec:
        spec = _spec_from_byte_offset(name, spec)
    value = 0
    word_indices = []
    parts_out = []
    for part in spec["parts"]:
        wi = part["word"]
        word_indices.append(wi)
        parts_out.append({"word_index": wi, "mask": part["mask"],
                          "shift": part["shift"], "width": part["width"],
                          "bit_offset": part["bit_offset"]})
        if wi >= len(words):
            return {"field": name, "parts": parts_out, "value": None,
                    "present": False, "error": "word_index_out_of_range",
                    "provenance": spec.get("provenance", "supplied")}
        v = (words[wi] & part["mask"]) >> part["shift"]
        value |= v << part["bit_offset"]
    present = True
    if spec.get("skip_all_ones"):
        total = sum(p["width"] for p in spec["parts"])
        if value == (1 << total) - 1:
            present = False
    return {
        "field": name,
        "word_indices": word_indices,
        "parts": parts_out,
        "value": value,
        "present": present,
        "skip_all_ones": bool(spec.get("skip_all_ones")),
        "provenance": spec.get("provenance", "supplied"),
    }


# --------------------------------------------------------------------------
# Vendor antsel_type + D3B MHF3 (exact, from docs/m34d3b_band_init.md §3.3)
# --------------------------------------------------------------------------
def antsel_type(boardtype, boardflags, antswitch, aa2g, aa5g):
    """Return (antsel_type, antsel_avail) exactly as ``wlc_antsel_attach()``.

    Vendor control flow (``re fn 0x5970a --asm``, verified against the blob):

        boardtype <= 3          -> L_bf
        antswitch == 0          -> L_bt0
        antswitch in 1..7       -> group table, then end (never reaches L_bf)
        antswitch > 7           -> end (type 0, avail 0)

        L_bt0: boardtype==4 && aa2g==7 && aa5g==0 -> type 2, avail 1
               otherwise -> **falls through to L_bf**
        L_bf : boardflags & 0x8 -> type 1, avail 1 ; else type 0, avail 0

    The L_bt0 -> L_bf fall-through is real: every mismatch instruction in
    ``0x5988d..0x598ca`` branches to ``0x598cc`` (L_bf).  An earlier
    approximation returned type 0 directly from L_bt0, which is wrong whenever
    ``boardtype > 3``, ``antswitch == 0`` and ``boardflags & 0x8`` is set.
    """
    if boardtype > 3 and antswitch != 0:
        # antswitch-only group table; unconditionally returns (no L_bf).
        if antswitch > 7:
            return 0, False
        if antswitch in (1, 2, 3):
            return 2, (aa2g == 7 or aa5g == 7)
        if antswitch == 5:
            return 4, (aa2g == 7 or aa5g == 7)
        if antswitch in (4, 6, 7):
            atype = {4: 3, 6: 5, 7: 6}[antswitch]
            return atype, (aa2g == 6 or aa5g == 6)
        return 0, False
    if boardtype > 3 and antswitch == 0:
        # L_bt0
        if boardtype == 4 and aa2g == 7 and aa5g == 0:
            return 2, True
        # fall through to L_bf
    # L_bf (also the boardtype <= 3 entry point)
    if boardflags & 0x8:
        return 1, True
    return 0, False


def mhf3(antsel):
    """D3B MHF3 bits: wlc_bmac_init sites 5..8 (ANTSEL_EN/MODE)."""
    if antsel in (2, 3, 6):
        return 0x3
    if antsel == 1:
        return 0x1
    return 0x0


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
def _load_offsets(path, allow_candidate):
    if path:
        with open(path, "r", encoding="utf-8") as fh:
            table = json.load(fh)
        table = {k: dict(v, provenance=v.get("provenance", "external-table"))
                 for k, v in table.items()}
        return table
    if allow_candidate:
        return {k: dict(v, provenance="candidate:bcma-rev8")
                for k, v in CANDIDATE_OFFSETS_BCMA_REV8.items()}
    return dict(PROVEN_OFFSETS)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", nargs="?", help="log/text file (default: stdin)")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--offsets", metavar="FILE",
                    help="proven rev11 field-offset table (JSON)")
    ap.add_argument("--allow-candidate-offsets", action="store_true",
                    help="use UNPROVEN bcma rev8 candidate offsets (labelled)")
    ap.add_argument("--decode", action="store_true",
                    help="decode the five antsel fields (needs offsets)")
    ap.add_argument("--antsel-inputs", metavar="K=V,...",
                    help="compute antsel_type/MHF3 from explicit values, "
                         "e.g. boardtype=0x8f,boardflags=0x0,antswitch=0,aa2g=3,aa5g=3")
    args = ap.parse_args(argv)

    if args.antsel_inputs:
        vals = {}
        for part in args.antsel_inputs.split(","):
            k, _, v = part.partition("=")
            vals[k.strip()] = int(v, 0)
        atype, avail = antsel_type(vals.get("boardtype", 0),
                                   vals.get("boardflags", 0),
                                   vals.get("antswitch", 0),
                                   vals.get("aa2g", 0), vals.get("aa5g", 0))
        result = {"inputs": vals, "antsel_type": atype, "antsel_avail": avail,
                  "MHF3": mhf3(atype)}
        print(json.dumps(result, indent=2) if args.json else result)
        return 0

    text = open(args.log, "r", encoding="utf-8", errors="replace").read() \
        if args.log else sys.stdin.read()

    header, words = parse_log(text)
    v = validate(header, words)
    result = {
        "raw_image": words,
        "header": header,
        "validation": v,
    }
    if args.decode:
        table = _load_offsets(args.offsets, args.allow_candidate_offsets)
        fields = {name: decode_field(words, name, spec)
                  for name, spec in table.items()}
        result["fields"] = fields
        if not table:
            result["decode_note"] = (
                "no proven rev11 offsets supplied; fields not decoded. "
                "Recover them from vendor srom_parsecis and pass --offsets.")
        # Only compute antsel_type/MHF3 if all inputs decoded.  A variable the
        # vendor omitted (skip-all-ones) is what getintvar() sees as 0.
        need = ("boardtype", "boardflags", "antswitch", "aa2g", "aa5g")
        if all(n in fields and fields[n]["value"] is not None for n in need):
            vals = {n: (fields[n]["value"] if fields[n]["present"] else 0)
                    for n in need}
            atype, avail = antsel_type(*(vals[n] for n in need))
            result["antsel_inputs"] = vals
            result["antsel_type"] = atype
            result["antsel_avail"] = avail
            result["MHF3"] = mhf3(atype)

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print("revision=%d crc=%02x calc=%02x ok=%s" % (
            v["revision"], v["stored_crc"], v["calculated_crc"], v["ok"]))
        if v["errors"]:
            print("errors:", ",".join(v["errors"]))
        if args.decode:
            print(json.dumps(result.get("fields", {}), indent=2))
        if "MHF3" in result:
            print("antsel_type=%d MHF3=0x%04x" % (result["antsel_type"],
                                                  result["MHF3"]))
    return 0 if v["ok"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except SpromError as exc:
        print("sprom11_decode: error: %s" % exc, file=sys.stderr)
        sys.exit(2)

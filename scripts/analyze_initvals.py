#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# M3.4D2B — machine-generated classification of the rev42 common/band-switch
# initvals tables. ANALYSIS ONLY: this script reads the *vendor firmware images*
# (byte-exact slices of wlc_hybrid.o_shipped) and never touches hardware.
#
# Purpose: enumerate every record of `d11ac1initvals42` (610 data records) and
# `d11ac1bsinitvals42` (73 data records), classify each by D11 target
# address/window, decode the OBJADDR/OBJDATA indirect-write windows, and emit
# deterministic JSON + Markdown artifacts under docs/m34d2b/.
#
# Provenance / confidence:
#   C2 — offset, width, value, terminator and window selectors: read directly
#        from the vendor image (same blob slice used by M3.4C.1/M3.4D1).
#   C3 — region / field *names* (MAC core, IHR, PSM, TXE, TSF, IFS, SHM, ...):
#        cross-referenced against publicly documented D11 register layout
#        (upstream brcmsmac `d11.h`). Names are hypotheses only; no code or
#        identifiers are copied into the driver.
#   UNKNOWN — an offset that does not map into a documented region. Never
#        guessed; emitted as UNKNOWN.
#
# Record format (proven from the vendor applier `sub_60f67`, see
# docs/m34d2b_common_initvals.md): 8-byte little-endian {u16 offset, u16 width,
# u32 value}, width in {2,4}, terminated by offset == 0xffff. The applier writes
# width==2 via osl_writew and width==4 via osl_writel at
# (DEV_D11_BASE + offset).

import argparse
import hashlib
import json
import os
import struct
import sys
from collections import Counter

# --- exact vendor images (staged names; hashes from M3.4C.1) -----------------
IMAGES = {
    "common": {
        "path": "/lib/firmware/brcm/bcm4352-d11ac1initvals42.bin",
        "size": 4888,
        "sha256": "b5a2735d89aab8d230297779004c2b989f6dc4e7c2e19902a173efea5983d938",
        "records": 610,
    },
    "bs": {
        "path": "/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin",
        "size": 592,
        "sha256": "e81a645c79f55557c87f4662702d7c57599918c9b2340bc4e440ce1bdbd014da",
        "records": 73,
    },
}

REC_SIZE = 8
TERMINATOR = 0xFFFF

# OBJADDR selector bits (C3, brcmsmac d11.h `OBJADDR_*`).
OBJ_SEL_MASK = 0x000F0000
OBJ_SEL = {
    0x00000000: "UCM",
    0x00010000: "SHM",
    0x00020000: "SCR",
    0x00030000: "IHR",
    0x00040000: "RCMTA",
    0x00060000: "SRCHM",
}
OBJ_AUTO_INC = 0x03000000
OBJ_WINC = 0x01000000
OBJ_RINC = 0x02000000

# Named offsets already recovered/used by OpenBRCM (C2). Kept deliberately
# small: everything else stays region- or UNKNOWN-level.
KNOWN = {
    0x0100: "INTRCVLAZY0",
    0x0120: "MACCONTROL",
    0x0124: "MACCOMMAND",
    0x0128: "MACINTSTATUS",
    0x012C: "MACINTMASK",
    0x0130: "TPLATEWRPTR",
    0x0134: "TPLATEWRDATA",
    0x0160: "OBJADDR",
    0x0164: "OBJDATA",
    0x0166: "OBJDATA+2",
}

# PSM scratch-pad register names by OBJADDR_SCR_SEL index (C3, brcmsmac d11.h
# `enum _ePsmScratchPadRegDefinitions`). The index is the low 16 bits of the
# SCR selector (no >>2 shift for SCR).
SCR_NAMES = {
    0: "S_RSV0", 1: "S_RSV1", 2: "S_RSV2", 3: "S_DOT11_CWMIN",
    4: "S_DOT11_CWMAX", 5: "S_DOT11_CWCUR", 6: "S_DOT11_SRC_LMT",
    7: "S_DOT11_LRC_LMT", 8: "S_DOT11_DTIMCOUNT", 9: "S_SEQ_NUM",
    10: "S_SEQ_NUM_FRAG", 11: "S_FRMRETX_CNT", 12: "S_SSRC", 13: "S_SLRC",
    14: "S_EXP_RSP", 15: "S_OLD_BREM", 16: "S_OLD_CWWIN", 17: "S_TXECTL",
    18: "S_CTXTST", 19: "S_RXTST", 20: "S_STREG", 21: "S_TXPWR_SUM",
    22: "S_TXPWR_ITER", 23: "S_RX_FRMTYPE", 24: "S_THIS_AGG",
    25: "S_KEYINDX", 26: "S_RXFRMLEN",
}

# Region map (C3, structural). Ordered, first match wins.
# (lo, hi, region, category) with hi exclusive.
REGIONS = [
    (0x000, 0x020, "core-id/timers", "MAC_CORE"),
    (0x020, 0x060, "intctrlregs[0..7]", "MACINT"),
    (0x100, 0x110, "intrcvlazy[0..3]", "MACINT"),
    (0x110, 0x120, "pad/unknown", "UNKNOWN"),
    (0x120, 0x128, "MAC core", "MAC_CORE"),
    (0x128, 0x130, "MACINTSTATUS/MASK", "MACINT"),
    (0x130, 0x138, "TX template access", "TEMPLATE"),
    (0x140, 0x150, "PMQ", "MAC_CORE"),
    (0x150, 0x160, "MAC status/caps", "MAC_CORE"),
    (0x160, 0x170, "OBJ window", "OBJ"),
    (0x170, 0x178, "FRMTXSTATUS", "MAC_CORE"),
    (0x180, 0x1A0, "TSF host access", "TSF"),
    (0x1A0, 0x1A8, "MACCONTROL1/HWCAP1", "MAC_CORE"),
    (0x1E0, 0x1E4, "CLK_CTL_ST", "MAC_CORE"),
    (0x200, 0x380, "DMA/PIO", "DMA"),
    (0x380, 0x3D8, "DMA FIFO diag/agg", "DMA"),
    (0x3D8, 0x3E0, "radio access", "RADIO"),
    (0x3E0, 0x400, "PHY direct", "PHY"),
    (0x400, 0x480, "IHR/RXE", "IHR"),
    (0x480, 0x500, "IHR/PSM", "IHR"),
    (0x500, 0x580, "IHR/TXE0", "IHR"),
    (0x580, 0x600, "IHR/TXE1", "IHR"),
    (0x600, 0x680, "IHR/TSF", "IHR"),
    (0x680, 0x6A0, "IHR/IFS", "IHR"),
    (0x6A0, 0x6C0, "IHR/slow-clock", "IHR"),
    (0x700, 0x780, "IHR/NAV", "IHR"),
    (0x780, 0x800, "IHR/WEP-PMQ", "IHR"),
    (0x800, 0xF00, "SHM (direct window)", "SHM"),
]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_table(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) == 0 or len(data) % REC_SIZE:
        raise ValueError("size %d is not a multiple of %d" % (len(data), REC_SIZE))
    records = []
    n = len(data) // REC_SIZE
    for i in range(n):
        off, width = struct.unpack_from("<HH", data, i * REC_SIZE)
        if off == TERMINATOR:
            if i != n - 1:
                raise ValueError("records follow the 0xffff terminator at %d" % i)
            return records, i
        if width not in (2, 4):
            raise ValueError("record %d: illegal width %d" % (i, width))
        value = struct.unpack_from("<I", data, i * REC_SIZE + 4)[0]
        records.append({"index": i, "offset": off, "width": width, "value": value})
    raise ValueError("missing 0xffff terminator")


def classify_direct(off):
    for lo, hi, region, category in REGIONS:
        if lo <= off < hi:
            return region, category
    return "UNKNOWN", "UNKNOWN"


def side_effect(rec):
    off, kind, space, cat = (rec["offset"], rec["kind"], rec["space"],
                             rec["category"])
    if kind == "obj_sel":
        return "object-memory selector"
    if kind == "obj_data":
        return "PSM scratch config" if space == "SCR" else "SHM state (config)"
    if cat == "MAC_CORE":
        return "MAC control (command)"
    if cat == "MACINT":
        return "status clear" if off == 0x128 else "interrupt control"
    if cat == "TEMPLATE":
        return "template/object-memory"
    if space == "SHM (direct window)":
        return "SHM state (config)"
    if space == "IHR/PSM":
        return "PSM configuration"
    if space in ("IHR/TXE0", "IHR/TXE1", "IHR/RXE"):
        return "FIFO configuration"
    if space == "IHR/TSF":
        return "timer/TSF"
    if space == "IHR/IFS":
        return "timing/IFS configuration"
    if cat in ("DMA", "PHY", "RADIO"):
        return "DMA" if cat == "DMA" else "PHY/radio"
    return "unknown side effect"


def classify_records(records):
    out = []
    window = None
    for r in records:
        off, width, value = r["offset"], r["width"], r["value"]
        rec = dict(r)
        rec["kind"] = "direct"
        rec["space"] = "-"
        rec["target_offset"] = None
        rec["target"] = None
        rec["category"] = None
        rec["name"] = KNOWN.get(off, "UNKNOWN")
        rec["confidence"] = "C2/C3"

        if off == 0x0160 and width == 4:
            sel = value & OBJ_SEL_MASK
            space = OBJ_SEL.get(sel, "UNKNOWN(0x%05x)" % sel)
            inc = value & 0x03000000
            base = value & 0x0000FFFF
            window = {
                "space": space,
                "auto_inc": inc == OBJ_AUTO_INC,
                "inc_raw": inc,
                "base": base,
                "writes": 0,
            }
            rec["kind"] = "obj_sel"
            rec["space"] = space
            rec["target_offset"] = base * 4
            rec["target"] = "%s window, base byte offset 0x%04x, %s" % (
                space, base * 4,
                "auto-inc" if window["auto_inc"] else "no auto-inc")
            rec["category"] = "OBJ"
            rec["name"] = "OBJADDR selector"
        elif off in (0x0164, 0x0166) and window is not None:
            if window["auto_inc"]:
                tgt = window["base"] * 4 + window["writes"] * 4
            else:
                tgt = window["base"] * 4
            window["writes"] += 1
            rec["kind"] = "obj_data"
            rec["space"] = window["space"]
            rec["target_offset"] = tgt
            rec["target"] = "%s byte offset 0x%04x (%s half)" % (
                window["space"], tgt, "high" if off == 0x0166 else "low")
            rec["category"] = "OBJ"
            rec["name"] = "OBJDATA" if off == 0x0164 else "OBJDATA+2"
            if window["space"] == "SCR":
                idx = window["base"]
                rec["name"] = "SCR[%d] %s" % (idx, SCR_NAMES.get(idx, "UNKNOWN"))
                rec["target"] = "SCR index %d (byte 0x%04x)" % (idx, idx * 4)
        else:
            region, category = classify_direct(off)
            rec["space"] = region
            rec["category"] = category
            rec["target"] = region
            if category == "UNKNOWN":
                rec["confidence"] = "C2 (offset/value); region UNKNOWN"
            elif rec["name"] == "UNKNOWN":
                rec["confidence"] = "C2 (offset/value); region C3; field UNKNOWN"
            else:
                rec["confidence"] = "C2/C3"
        out.append(rec)

    direct_counts = Counter(r["offset"] for r in out if r["kind"] == "direct")
    for r in out:
        r["side_effect"] = side_effect(r)
        r["repeat"] = direct_counts.get(r["offset"], 0) \
            if r["kind"] == "direct" else 0
    return out


def summarise(recs):
    by_kind = {}
    by_category = {}
    by_space = {}
    by_side_effect = {}
    for r in recs:
        by_kind[r["kind"]] = by_kind.get(r["kind"], 0) + 1
        by_category[r["category"]] = by_category.get(r["category"], 0) + 1
        key = r["space"] if r["kind"] != "direct" else r["space"]
        by_space[key] = by_space.get(key, 0) + 1
        by_side_effect[r["side_effect"]] = \
            by_side_effect.get(r["side_effect"], 0) + 1
    return {
        "records": len(recs),
        "by_kind": by_kind,
        "by_category": by_category,
        "by_space": by_space,
        "by_side_effect": by_side_effect,
    }


def direct_offset_summary(recs):
    groups = {}
    for r in recs:
        if r["kind"] != "direct":
            continue
        g = groups.setdefault(r["offset"], {
            "offset": r["offset"], "width": r["width"], "region": r["space"],
            "name": r["name"], "side_effect": r["side_effect"],
            "count": 0, "values": [],
        })
        g["count"] += 1
        if r["value"] not in g["values"]:
            g["values"].append(r["value"])
    return [groups[k] for k in sorted(groups)]


def compare(common, bs):
    c_targets = {}
    for r in common:
        if r["kind"] == "obj_data":
            c_targets.setdefault((r["space"], r["target_offset"]), []).append(r)
    b_targets = {}
    for r in bs:
        if r["kind"] == "obj_data":
            b_targets.setdefault((r["space"], r["target_offset"]), []).append(r)

    direct_c = {}
    for r in common:
        if r["kind"] == "direct":
            direct_c.setdefault(r["offset"], []).append(r)
    direct_b = {}
    for r in bs:
        if r["kind"] == "direct":
            direct_b.setdefault(r["offset"], []).append(r)

    return {
        "common_obj_targets": len(c_targets),
        "bs_obj_targets": len(b_targets),
        "obj_target_overlap": sorted(
            set(c_targets) & set(b_targets)),
        "direct_offset_overlap": sorted(
            set(direct_c) & set(direct_b)),
        "common_window_spaces": sorted({r["space"] for r in common
                                        if r["kind"] == "obj_sel"}),
        "bs_window_spaces": sorted({r["space"] for r in bs
                                    if r["kind"] == "obj_sel"}),
    }


def render_markdown(name, recs, summary, image):
    L = []
    L.append("# %s — machine-generated record classification" % name)
    L.append("")
    L.append("- source: `%s`" % image["path"])
    L.append("- size: %d bytes, sha256 `%s`" % (image["size"], image["sha256"]))
    L.append("- data records: **%d** (terminator at index %d)"
             % (image["records"], image["records"]))
    L.append("- status: **ANALYSIS ONLY** (no hardware access)")
    L.append("")
    L.append("## Summary by kind")
    L.append("")
    L.append("| kind | count |")
    L.append("|---|---|")
    for k in ("direct", "obj_sel", "obj_data"):
        L.append("| %s | %d |" % (k, summary["by_kind"].get(k, 0)))
    L.append("")
    L.append("## Summary by category")
    L.append("")
    L.append("| category | count |")
    L.append("|---|---|")
    for k in sorted(summary["by_category"]):
        L.append("| %s | %d |" % (k, summary["by_category"][k]))
    L.append("")
    L.append("## Summary by target space/region")
    L.append("")
    L.append("| space/region | count |")
    L.append("|---|---|")
    for k in sorted(summary["by_space"]):
        L.append("| %s | %d |" % (k, summary["by_space"][k]))
    L.append("")
    L.append("## Summary by side-effect class")
    L.append("")
    L.append("| side effect | count |")
    L.append("|---|---|")
    total = 0
    for k in sorted(summary["by_side_effect"]):
        L.append("| %s | %d |" % (k, summary["by_side_effect"][k]))
        total += summary["by_side_effect"][k]
    L.append("| **TOTAL** | **%d** |" % total)
    L.append("")
    L.append("## Full per-record classification")
    L.append("")
    L.append("| # | offset | w | value | kind | space | target | category | name | side effect | conf |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for r in recs:
        tgt = r["target"] or "-"
        L.append("| %d | 0x%04x | %d | 0x%08x | %s | %s | %s | %s | %s | %s | %s |" % (
            r["index"], r["offset"], r["width"], r["value"], r["kind"],
            r["space"], tgt, r["category"], r["name"] or "-",
            r["side_effect"], r["confidence"]))
    L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--common", default=IMAGES["common"]["path"])
    ap.add_argument("--bs", default=IMAGES["bs"]["path"])
    ap.add_argument("--outdir", default=None,
                    help="output directory (default: <repo>/docs/m34d2b)")
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    outdir = args.outdir or os.path.join(here, os.pardir, "docs", "m34d2b")
    outdir = os.path.abspath(outdir)

    result = {}
    for key, src in (("common", args.common), ("bs", args.bs)):
        img = IMAGES[key]
        if not os.path.exists(src):
            print("ERROR: missing image %s" % src, file=sys.stderr)
            return 2
        size = os.path.getsize(src)
        digest = sha256_file(src)
        if size != img["size"] or digest != img["sha256"]:
            print("ERROR: %s is not the expected vendor image "
                  "(size %d/%d, sha256 %s)" % (src, size, img["size"], digest),
                  file=sys.stderr)
            return 3
        recs, term = parse_table(src)
        if len(recs) != img["records"]:
            print("ERROR: %s has %d records, expected %d"
                  % (src, len(recs), img["records"]), file=sys.stderr)
            return 4
        classified = classify_records(recs)
        summary = summarise(classified)
        result[key] = {"records": classified, "summary": summary,
                       "terminator_index": term, "image": img}

    cmp = compare(result["common"]["records"], result["bs"]["records"])

    os.makedirs(outdir, exist_ok=True)
    with open(os.path.join(outdir, "initvals_classification.json"), "w") as f:
        json.dump({
            "milestone": "M3.4D2B",
            "status": "ANALYSIS ONLY",
            "hardware_access": False,
            "record_format": "{u16 offset, u16 width, u32 value}, terminator 0xffff",
            "common": {
                "image": result["common"]["image"],
                "summary": result["common"]["summary"],
                "direct_offsets": direct_offset_summary(
                    result["common"]["records"]),
                "records": result["common"]["records"],
            },
            "bs": {
                "image": result["bs"]["image"],
                "summary": result["bs"]["summary"],
                "direct_offsets": direct_offset_summary(
                    result["bs"]["records"]),
                "records": result["bs"]["records"],
            },
            "comparison": cmp,
        }, f, indent=1, sort_keys=True)

    with open(os.path.join(outdir, "initvals_classification.md"), "w") as f:
        f.write(render_markdown("d11ac1initvals42 (common)", 
                                result["common"]["records"],
                                result["common"]["summary"], IMAGES["common"]))
        f.write("\n---\n\n")
        f.write(render_markdown("d11ac1bsinitvals42 (band-switch)",
                                result["bs"]["records"],
                                result["bs"]["summary"], IMAGES["bs"]))

    for key in ("common", "bs"):
        s = result[key]["summary"]
        print("%s: %d records; kind=%s" % (key, s["records"], s["by_kind"]))
        print("  category:    %s" % s["by_category"])
        print("  space:       %s" % s["by_space"])
        print("  side-effect: %s" % s["by_side_effect"])
    print("comparison: %s" % cmp)
    print("wrote %s" % os.path.join(outdir, "initvals_classification.{json,md}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

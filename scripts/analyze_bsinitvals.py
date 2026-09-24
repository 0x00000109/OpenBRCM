#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# M3.4D3 — machine-generated classification of `d11ac1bsinitvals42`
# (the rev42 band-switch / band-init initvals table). ANALYSIS ONLY: this
# script reads the *vendor firmware image* (a byte-exact slice of
# wlc_hybrid.o_shipped) and never touches hardware.
#
# It reuses the proven record parser/classifier from analyze_initvals.py and
# adds: (a) logical indirect-transaction grouping (OBJADDR selector + OBJDATA
# low/high halves), (b) side-effect accounting that must reconcile to all 73
# records, (c) a common-vs-band-switch override comparison, and (d) deterministic
# Markdown + JSON artifacts under docs/m34d3/.
#
# Provenance:
#   C2 — offset/width/value/terminator/selectors read from the vendor image.
#   C3 — region/field names cross-referenced against brcmsmac (d11.h / main.c).
#   UNKNOWN — never guessed.

import argparse
import json
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import analyze_initvals as A  # noqa: E402  (same directory, deterministic)

BS = A.IMAGES["bs"]
COMMON = A.IMAGES["common"]

# C3 (brcmsmac main.c brcms_c_ucode_bsinit): the band-switch initvals are the
# "band-specific ucode IHR, SHM, and SCR inits" applied with the band's MHF host
# flags, immediately before wlc_phy_init().
SEMANTIC = {
    "obj_sel": "object-memory selector",
    "obj_data": "band/ucode SHM state (config)",
    "IHR/IFS": "band timing/IFS configuration (D11)",
    "IHR/NAV": "band NAV configuration (D11)",
    "UNKNOWN": "UNKNOWN",
}


def build_transactions(recs):
    """Group selector + following data records into logical transactions.

    The table is non-auto-increment, so every data record is preceded by its
    own selector; a repeated selector with OBJDATA+2 is the high half of a
    32-bit SHM value whose low half was written by the preceding transaction.
    """
    txs = []
    i = 0
    n = len(recs)
    while i < n:
        r = recs[i]
        if r["kind"] == "obj_sel":
            data = []
            j = i + 1
            while j < n and recs[j]["kind"] == "obj_data":
                data.append(recs[j])
                j += 1
            # The vendored SHM writes are per-16-bit-half (0x164 low / 0x166
            # high); a high half does not require a low half in the same group.
            writes = []
            for d in data:
                half = "high" if d["offset"] == 0x0166 else "low"
                byte = r["target_offset"] + (2 if half == "high" else 0)
                writes.append({
                    "record_index": d["index"],
                    "half": half,
                    "byte": byte,
                    "value": d["value"] & 0xFFFF,
                })
            txs.append({
                "kind": "indirect",
                "record_index": r["index"],
                "selector": r["value"],
                "space": r["space"],
                "target_offset": r["target_offset"],
                "auto_inc": r["target"].endswith("auto-inc"),
                "data": [d["index"] for d in data],
                "width": 2,
                "value": writes[0]["value"] if len(writes) == 1 else None,
                "writes": writes,
            })
            i = j
        else:
            txs.append({
                "kind": "direct",
                "record_index": r["index"],
                "offset": r["offset"],
                "width": r["width"],
                "value": r["value"],
                "space": r["space"],
            })
            i += 1
    return txs


def compare_override(common_recs, bs_recs):
    """Compare common vs band-switch by logical SHM target and direct offset.

    Band-switch runs after common (wlc_bmac_init -> band init), so any shared
    target is a last-wins override by the band-switch value.
    """
    def shm_map(recs):
        m = {}
        for r in recs:
            if r["kind"] == "obj_data":
                half = 2 if r["offset"] == 0x0166 else 0
                key = (r["space"], r["target_offset"] + half)
                m.setdefault(key, []).append(r)
        return m

    cm = shm_map(common_recs)
    bm = shm_map(bs_recs)
    shared = sorted(set(cm) & set(bm), key=lambda k: (k[0], k[1]))
    rows = []
    for key in shared:
        cvals = [r["value"] for r in cm[key]]
        bvals = [r["value"] for r in bm[key]]
        rows.append({
            "space": key[0],
            "byte": key[1],
            "common_values": cvals,
            "bs_values": bvals,
            "override": cvals != bvals,
        })

    direct_c = {r["offset"] for r in common_recs if r["kind"] == "direct"}
    direct_b = {r["offset"] for r in bs_recs if r["kind"] == "direct"}
    return {
        "shared_targets": len(shared),
        "overrides": sum(1 for r in rows if r["override"]),
        "rows": rows,
        "direct_offset_overlap": sorted(direct_c & direct_b),
    }


def render_md(recs, txs, summary, side, cmp, image):
    L = []
    L.append("# d11ac1bsinitvals42 — machine-generated classification (M3.4D3)")
    L.append("")
    L.append("- source: `%s`" % image["path"])
    L.append("- size: %d bytes, sha256 `%s`" % (image["size"], image["sha256"]))
    L.append("- data records: **%d** (terminator at index %d)"
             % (len(recs), len(recs)))
    L.append("- status: **ANALYSIS ONLY** (no hardware access)")
    L.append("")
    L.append("## Summary")
    L.append("")
    L.append("| kind | count |")
    L.append("|---|---|")
    for k in ("direct", "obj_sel", "obj_data"):
        L.append("| %s | %d |" % (k, summary["by_kind"].get(k, 0)))
    L.append("")
    L.append("| category | count |")
    L.append("|---|---|")
    for k in sorted(summary["by_category"]):
        L.append("| %s | %d |" % (k, summary["by_category"][k]))
    L.append("")
    L.append("| side-effect class | count |")
    L.append("|---|---|")
    tot = 0
    for k in sorted(side):
        L.append("| %s | %d |" % (k, side[k]))
        tot += side[k]
    L.append("| **TOTAL** | **%d** |" % tot)
    L.append("")
    L.append("## Full per-record classification")
    L.append("")
    L.append("| # | offset | w | value | kind | space | target | category | ctx | side effect | conf |")
    L.append("|---|---|---|---|---|---|---|---|---|---|---|")
    for r in recs:
        L.append("| %d | 0x%04x | %d | 0x%08x | %s | %s | %s | %s | %s | %s | %s |" % (
            r["index"], r["offset"], r["width"], r["value"], r["kind"],
            r["space"], r["target"] or "-", r["category"], r["name"] or "-",
            r["side_effect"], r["confidence"]))
    L.append("")
    L.append("## Logical indirect transactions and direct writes")
    L.append("")
    L.append("| tx | kind | selector/offset | space | byte target | writes | value(s) |")
    L.append("|---|---|---|---|---|---|---|")
    for t in txs:
        if t["kind"] == "indirect":
            wtxt = ",".join("%s@0x%04x=0x%04x" % (w["half"], w["byte"], w["value"])
                            for w in t["writes"])
            L.append("| %d | indirect | 0x%08x | %s | 0x%04x | %s | %s |" % (
                t["record_index"], t["selector"], t["space"],
                t["target_offset"], len(t["writes"]), wtxt))
        else:
            L.append("| %d | direct | 0x%04x | %s | 0x%04x | 1 | 0x%08x |" % (
                t["record_index"], t["offset"], t["space"], t["offset"],
                t["value"]))
    L.append("")
    L.append("## Common-initvals vs band-switch override")
    L.append("")
    L.append("- shared logical SHM targets: **%d**" % cmp["shared_targets"])
    L.append("- overrides (band-switch value differs): **%d**" % cmp["overrides"])
    L.append("- shared direct offsets: **%s**"
             % (cmp["direct_offset_overlap"] or "none"))
    L.append("")
    L.append("| space | byte target | common values | band-switch values | override |")
    L.append("|---|---|---|---|---|")
    for row in cmp["rows"]:
        L.append("| %s | 0x%04x | %s | %s | %s |" % (
            row["space"], row["byte"],
            ",".join("0x%08x" % v for v in row["common_values"]),
            ",".join("0x%08x" % v for v in row["bs_values"]),
            "yes" if row["override"] else "no"))
    L.append("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--bs", default=BS["path"])
    ap.add_argument("--common", default=COMMON["path"])
    ap.add_argument("--outdir", default=None,
                    help="output directory (default: <repo>/docs/m34d3)")
    args = ap.parse_args()

    outdir = args.outdir or os.path.join(HERE, os.pardir, "docs", "m34d3")
    outdir = os.path.abspath(outdir)

    for path, img in ((args.bs, BS), (args.common, COMMON)):
        if not os.path.exists(path):
            print("ERROR: missing image %s" % path, file=sys.stderr)
            return 2
        if os.path.getsize(path) != img["size"] or \
           A.sha256_file(path) != img["sha256"]:
            print("ERROR: %s is not the expected vendor image" % path,
                  file=sys.stderr)
            return 3

    bs_records, bs_term = A.parse_table(args.bs)
    common_records, _ = A.parse_table(args.common)
    if len(bs_records) != BS["records"]:
        print("ERROR: bs has %d records, expected %d"
              % (len(bs_records), BS["records"]), file=sys.stderr)
        return 4

    bs = A.classify_records(bs_records)
    common = A.classify_records(common_records)
    summary = A.summarise(bs)
    side = Counter(r["side_effect"] for r in bs)
    txs = build_transactions(bs)
    cmp = compare_override(common, bs)

    if sum(side.values()) != len(bs):
        print("ERROR: side-effect accounting does not reconcile", file=sys.stderr)
        return 5

    os.makedirs(outdir, exist_ok=True)
    payload = {
        "milestone": "M3.4D3",
        "status": "ANALYSIS ONLY",
        "hardware_access": False,
        "record_format": "{u16 offset, u16 width, u32 value}, terminator 0xffff",
        "table": "d11ac1bsinitvals42",
        "image": BS,
        "terminator_index": bs_term,
        "summary": summary,
        "side_effect_accounting": dict(sorted(side.items())),
        "transaction_count": len(txs),
        "indirect_transactions": sum(1 for t in txs if t["kind"] == "indirect"),
        "direct_writes": sum(1 for t in txs if t["kind"] == "direct"),
        "transactions": txs,
        "comparison_vs_common": cmp,
        "records": bs,
    }
    with open(os.path.join(outdir, "bsinitvals_classification.json"), "w") as f:
        json.dump(payload, f, indent=1, sort_keys=True)
    with open(os.path.join(outdir, "bsinitvals_classification.md"), "w") as f:
        f.write(render_md(bs, txs, summary, side, cmp, BS))

    print("bs: %d records; kind=%s" % (len(bs), summary["by_kind"]))
    print("  side-effect: %s (total %d)" % (dict(sorted(side.items())), sum(side.values())))
    print("  transactions: %d (%d indirect, %d direct)"
          % (len(txs), payload["indirect_transactions"], payload["direct_writes"]))
    print("  shared SHM targets vs common: %d (overrides %d)"
          % (cmp["shared_targets"], cmp["overrides"]))
    print("wrote %s" % os.path.join(outdir, "bsinitvals_classification.{json,md}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

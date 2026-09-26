#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Deterministic offline decoder for the isolated BCM2069 radio identity probe.

Consumes the `radio-probe:` lines emitted by
`insmod openbrcm.ko radio_id_probe_only=1` and produces a compact JSON
artifact. It never needs an LLM and never reads the vendor blob: the two raw
radio registers are decoded with the exact extraction proven in
`wlc_phy_attach @0xbeff8` and recorded in
docs/m34d4/radio_identity_map.json / docs/m34d4/pll_selector_provenance.json.

    radio_id       = reg1
    radio_rev      = reg0 & 0xff
    rev_low        = reg0 & 0x0f
    revision_class = (reg0 >> 4) & 0xff          (pi+0x16e)
    branch         = A (class 1) | B (class 2) | SKIP (2069, other class)
                     | UNKNOWN (radio_id != 0x2069)

It rejects: malformed logs, missing values, duplicate conflicting values,
candidate mismatch, impossible extraction (declared decode disagrees with the
independently recomputed one) and any explicit dev_lost/fault result.

Usage:
    decode_radio_probe.py --log capture.txt --json out.json
    decode_radio_probe.py < capture.txt
    decode_radio_probe.py --log capture.txt --expect-candidate <sha256>
    decode_radio_probe.py --log capture.txt --expect-module-sha256 <sha256>

Exit status: 0 accepted, 1 rejected (reason on stderr), 2 usage error.
"""
import argparse
import json
import re
import sys

PREFIX = "radio-probe:"
BLOB_SHA256_FULL = (
    "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"
)

REQUIRED_KEYS = (
    "reg0_raw",
    "reg1_raw",
    "radioid",
    "radiorev",
    "revision_class",
    "pll_branch",
)

# Recovered BCM2069 revision domain (docs/m34d4/radio_identity_map.json):
# the vendor rev jump table indexes R-3 over [0,0x23] (R in 3..38) plus the
# special revs {0,1,2,254}. A BCM2069 read whose revision byte is outside this
# domain is treated as AMBIGUOUS and rejected, so the PML/PLL-reset omission
# cannot silently turn a stuck/invalid radio window into a "valid" revision.
KNOWN_2069_REVS = frozenset({0, 1, 2, 254} | set(range(3, 39)))

# Fields whose duplicate lines must agree (conflict => reject).
CONFLICT_KEYS = REQUIRED_KEYS + ("pre_access", "post_access", "candidate",
                                 "module", "module_sha256")

KNOWN_BRANCHES = ("A", "B", "SKIP", "UNKNOWN")


class DecodeError(Exception):
    pass


def _to_int(text):
    text = text.strip()
    try:
        return int(text, 0)
    except ValueError as exc:
        raise DecodeError("not an integer: %r" % text) from exc


def decode(reg0, reg1):
    """Exact extraction shared with src/ob_radio.h ob_radio_decode()."""
    radio_id = reg1
    radio_rev = reg0 & 0xFF
    rev_low = reg0 & 0x0F
    revision_class = (reg0 >> 4) & 0xFF
    id_is_2069 = radio_id == 0x2069
    id_accepted = radio_id in (0x2069, 0x030B)
    all_ones_pair = reg0 == 0xFFFF and reg1 == 0xFFFF
    if not id_is_2069:
        branch = "UNKNOWN"
    elif revision_class == 1:
        branch = "A"
    elif revision_class == 2:
        branch = "B"
    else:
        branch = "SKIP"
    return {
        "radio_id": radio_id,
        "radio_rev": radio_rev,
        "rev_low": rev_low,
        "revision_class": revision_class,
        "id_is_2069": id_is_2069,
        "id_accepted": id_accepted,
        "all_ones_pair": all_ones_pair,
        "pll_branch": branch,
    }


def parse_log(text):
    """Return (fields, fault) from the captured log text.

    fields: last-seen value per key; duplicate differing values raise
    DecodeError (conflict). fault: human reason string if a fault/dev_lost or
    FAIL line is present.
    """
    fields = {}
    seen = {}
    fault = None
    saw_begin = False
    saw_pass = False
    saw_stop = False

    for raw in text.splitlines():
        line = raw.strip()
        if "DEVICE LOST" in line or "DEVICE LOST" in raw:
            fault = "dev_lost line: %s" % line
        if PREFIX not in line:
            continue
        body = line.split(PREFIX, 1)[1].strip()
        if not body:
            continue
        if body.startswith("BEGIN"):
            saw_begin = True
            continue
        if body.startswith("PASS"):
            saw_pass = True
            continue
        if body.startswith("STOPPED BEFORE"):
            saw_stop = True
            continue
        if body.startswith("FAIL"):
            fault = body
            continue
        if "=" not in body:
            # free-form progress line (prep/clkctl/ioctl); ignore
            continue
        key, _, val = body.partition("=")
        key = key.strip()
        val = val.strip()
        if key not in CONFLICT_KEYS:
            continue
        if key in seen and seen[key] != val:
            raise DecodeError(
                "duplicate conflicting value for %s: %r vs %r"
                % (key, seen[key], val)
            )
        seen[key] = val
        fields[key] = val

    if fault:
        raise DecodeError("fault/dev_lost result: %s" % fault)
    if not saw_begin:
        raise DecodeError("malformed log: no '%s BEGIN' line" % PREFIX)
    if not saw_pass:
        raise DecodeError("malformed log: no '%s PASS' line" % PREFIX)
    if not saw_stop:
        raise DecodeError(
            "malformed log: no '%s STOPPED BEFORE PLL/RADIO INIT' line"
            % PREFIX
        )
    missing = [k for k in REQUIRED_KEYS if k not in fields]
    if missing:
        raise DecodeError("missing values: %s" % ", ".join(missing))
    return fields


def build_artifact(fields, kernel_lines, expect_candidate=None,
                   expect_module_sha256=None):
    reg0 = _to_int(fields["reg0_raw"])
    reg1 = _to_int(fields["reg1_raw"])
    if not (0 <= reg0 <= 0xFFFF) or not (0 <= reg1 <= 0xFFFF):
        raise DecodeError("raw register out of 16-bit range")

    dec = decode(reg0, reg1)

    # Impossible extraction / tamper detection: declared decode must match.
    declared_id = _to_int(fields["radioid"])
    declared_rev = _to_int(fields["radiorev"])
    declared_class = _to_int(fields["revision_class"])
    declared_branch = fields["pll_branch"]
    if declared_id != dec["radio_id"]:
        raise DecodeError(
            "impossible extraction: radioid=%#x != reg1=%#x"
            % (declared_id, dec["radio_id"])
        )
    if declared_rev != dec["radio_rev"]:
        raise DecodeError(
            "impossible extraction: radiorev=%#x != reg0&0xff=%#x"
            % (declared_rev, dec["radio_rev"])
        )
    if declared_class != dec["revision_class"]:
        raise DecodeError(
            "impossible extraction: revision_class=%d != (reg0>>4)&0xff=%d"
            % (declared_class, dec["revision_class"])
        )
    if declared_branch not in KNOWN_BRANCHES:
        raise DecodeError("unknown pll_branch %r" % declared_branch)
    if declared_branch != dec["pll_branch"]:
        raise DecodeError(
            "impossible extraction: pll_branch=%s != derived %s"
            % (declared_branch, dec["pll_branch"])
        )

    if dec["all_ones_pair"]:
        raise DecodeError("invalid observation: all-ones register pair")

    # PML/PLL-reset-omission guard: a BCM2069 revision outside the recovered
    # domain is ambiguous (the window may be unclocked/stuck) and must never be
    # accepted as a valid revision that closes the PLL blocker.
    if dec["id_is_2069"] and dec["radio_rev"] not in KNOWN_2069_REVS:
        raise DecodeError(
            "ambiguous revision 0x%02x not in the recovered BCM2069 revision "
            "domain (PML/PLL-reset omission cannot be distinguished); "
            "blocker NOT closed" % dec["radio_rev"]
        )

    candidate = fields.get("candidate")
    module_sha = fields.get("module_sha256")
    if expect_candidate and candidate and candidate != expect_candidate:
        raise DecodeError(
            "candidate mismatch: log=%s expected=%s"
            % (candidate, expect_candidate)
        )
    if expect_module_sha256 and module_sha and \
            module_sha != expect_module_sha256:
        raise DecodeError(
            "module sha256 mismatch: log=%s expected=%s"
            % (module_sha, expect_module_sha256)
        )

    if not dec["id_accepted"]:
        raise DecodeError(
            "unexpected radio id %#x (AC accepts 0x2069/0x030b)"
            % dec["radio_id"]
        )

    kernel = None
    for line in kernel_lines:
        if line.startswith("Linux version"):
            kernel = line.strip()
            break

    if dec["pll_branch"] in ("A", "B", "SKIP"):
        to_status = "PROVEN_%s" % dec["pll_branch"]
    else:
        to_status = "UNKNOWN"

    return {
        "artifact": "radio_probe_capture",
        "schema_version": 1,
        "status": "PASS",
        "raw": {
            "reg0_raw": reg0,
            "reg1_raw": reg1,
            "pre_access": _to_int(fields["pre_access"])
            if "pre_access" in fields else None,
            "post_access": _to_int(fields["post_access"])
            if "post_access" in fields else None,
        },
        "decoded": dec,
        "identity": {
            "radio_id": "0x%04x" % dec["radio_id"],
            "radio_rev": "0x%02x" % dec["radio_rev"],
            "revision_class": dec["revision_class"],
            "pll_branch": dec["pll_branch"],
        },
        "provenance": {
            "blob_sha256": BLOB_SHA256_FULL,
            "extraction": "wlc_phy_attach @0xbeff8 (AC): radio_id=reg1; "
                          "radio_rev=reg0&0xff; revision_class=(reg0>>4)&0xff",
            "branch_mapping": "class 1 -> sequence A, class 2 -> sequence B, "
                              "else SKIP (2069 only)",
            "revision_domain": sorted(KNOWN_2069_REVS),
            "ambiguous_policy": "a BCM2069 revision outside revision_domain is "
                                "rejected (PML/PLL-reset omission cannot be "
                                "distinguished from a stuck/invalid window)",
            "evidence": [
                "docs/m34d4/pll_selector_provenance.json",
                "docs/m34d4/radio_identity_map.json",
                "docs/m34d4/pll_decision_matrix.json",
            ],
        },
        "candidate": candidate,
        "module_sha256": module_sha,
        "kernel": kernel,
        "state_transition": {
            "prepared": True,
            "applied": False,
            "blocker": "d4.pll_branch.hw_probe",
            "blocker_from": "OPEN",
            "blocker_to": "CLOSED",
            "selector": "d4.pll_synth.rev42.branch",
            "selector_from": "HARDWARE_REQUIRED",
            "selector_to": to_status,
            "note": "transition is prepared only; apply only with this valid "
                    "capture attached",
        },
    }


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--log", help="captured dmesg/journal file (default stdin)")
    ap.add_argument("--json", help="write the JSON artifact here")
    ap.add_argument("--expect-candidate",
                    help="reject if the log candidate differs")
    ap.add_argument("--expect-module-sha256",
                    help="reject if the log module sha256 differs")
    args = ap.parse_args(argv)

    if args.log:
        with open(args.log, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    else:
        text = sys.stdin.read()

    try:
        fields = parse_log(text)
        artifact = build_artifact(fields, text.splitlines(),
                                  args.expect_candidate,
                                  args.expect_module_sha256)
    except DecodeError as exc:
        print("REJECT: %s" % exc, file=sys.stderr)
        return 1

    out = json.dumps(artifact, indent=2, sort_keys=True) + "\n"
    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            fh.write(out)
    else:
        sys.stdout.write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

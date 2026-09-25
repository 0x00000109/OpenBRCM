#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Deterministic generator for docs/current-context.json (OpenBRCM).

`docs/current-context.json` is a generated COMPACT VIEW / CACHE / INDEX over the
existing canonical project state. It is deliberately NOT a source of truth:

  * the hand-maintained machine-readable state lives in
    ``docs/state/current-state.json`` (the smallest possible companion/marker);
  * milestone/evidence lineage lives in ``docs/artifact-ledger.json``;
  * narrative state lives in ``docs/agent-state.md`` / ``docs/milestones.md``.

This generator merges those plus the binary/tooling identity into one small
(2-8 KiB) index that a fresh session can read instead of recursively reading
the docs tree. It never moves authoritative evidence, never invents state, and
fails loudly on a source conflict.

Usage:
  scripts/generate-current-context.py                 # write the context
  scripts/generate-current-context.py --check         # exit 1 if stale
  scripts/generate-current-context.py --validate      # integrity checks only
  scripts/generate-current-context.py --scan-links F  # check local md links

Determinism: identical (companion, ledger, blob, re.db) inputs produce
byte-identical output. No wall-clock timestamps are embedded. Stable ordering
is enforced; stale detection is via ``integrity.sources_hash`` and the recorded
``current_git.head``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys
from typing import Any

# --------------------------------------------------------------------------- #
# constants
# --------------------------------------------------------------------------- #

DEFAULT_TOOLING = "/media/kartashoff/Storage/opensource/iced/test"
DEFAULT_GHIDRA = "/home/kartashoff/projects/ghidra"
SUPPORTED_DB_SCHEMAS = {"5"}
MAX_CONTEXT_BYTES = 8192

STATE_REL = "docs/state/current-state.json"
LEDGER_REL = "docs/artifact-ledger.json"
OUT_REL = "docs/current-context.json"


# --------------------------------------------------------------------------- #
# small helpers
# --------------------------------------------------------------------------- #

def sha256_file(path: str) -> str | None:
    try:
        h = hashlib.sha256()
        with open(path, "rb") as fh:
            for chunk in iter(lambda: fh.read(65536), b""):
                h.update(chunk)
        return h.hexdigest()
    except OSError:
        return None


def read_json(path: str) -> Any:
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


def as_list(value: Any) -> list:
    if value is None:
        return []
    if isinstance(value, list):
        return value
    return [value]


def run_git(root: str, *args: str) -> str | None:
    try:
        out = subprocess.run(
            ["git", "-C", root, *args],
            capture_output=True, text=True, timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    return out.stdout.strip()


def canonical(obj: Any) -> str:
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def sources_hash(state: Any, ledger: Any, blob_sha: str | None,
                 db_meta_sha: str | None, schema_version: str | None) -> str:
    """Stable hash of the semantic sources.

    Deliberately excludes the mutable re.db FILE sha256 and the re binary
    sha256: a logically-identical rebuild must not make the context stale.
    Blob identity (``meta_sha256``) + schema + companion + ledger are what
    actually determine the index.
    """
    payload = canonical({
        "state": state,
        "ledger": ledger,
        "blob_sha256": blob_sha,
        "re_db_meta_sha256": db_meta_sha,
        "re_db_schema_version": schema_version,
    })
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


# --------------------------------------------------------------------------- #
# source loading
# --------------------------------------------------------------------------- #

def db_identity(tooling_dir: str) -> dict:
    db_path = os.path.join(tooling_dir, "re.db")
    info = {
        "path": db_path,
        "exists": os.path.isfile(db_path),
        "file_sha256": sha256_file(db_path),
        "meta_sha256": None,
        "schema_version": None,
    }
    if not info["exists"]:
        return info
    try:
        conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
        for key, col in (("sha256", "meta_sha256"), ("schema_version", "schema_version")):
            row = conn.execute(
                "SELECT value FROM meta WHERE key=?", (key,)
            ).fetchone()
            info[col] = row[0] if row else None
        conn.close()
    except sqlite3.Error as exc:  # pragma: no cover - corrupt db is exceptional
        info["error"] = str(exc)
    return info


def git_identity(root: str) -> dict:
    head = run_git(root, "rev-parse", "HEAD")
    branch = run_git(root, "rev-parse", "--abbrev-ref", "HEAD")
    dirty = run_git(root, "--no-pager", "status", "--porcelain", "--untracked-files=no")
    return {
        "branch": branch,
        "head": head,
        "dirty_tracked": bool(dirty),
    }


# --------------------------------------------------------------------------- #
# context build
# --------------------------------------------------------------------------- #

def build_context(root: str, tooling_dir: str, ghidra_home: str) -> dict:
    state_path = os.path.join(root, STATE_REL)
    ledger_path = os.path.join(root, LEDGER_REL)
    state = read_json(state_path)
    ledger = read_json(ledger_path)

    blob_path = os.path.join(tooling_dir, "wlc_hybrid.o_shipped")
    blob_sha = sha256_file(blob_path)
    db = db_identity(tooling_dir)

    ghidra_headless = os.path.join(ghidra_home, "support", "analyzeHeadless")

    facts = sorted(state.get("proven_facts", []), key=lambda f: f.get("id", ""))
    blockers = sorted(state.get("open_blockers", []), key=lambda b: b.get("id", ""))

    superseded = []
    for rec in ledger.get("records", []):
        for sup in as_list(rec.get("superseded_by")):
            superseded.append({"claim": rec.get("id"), "superseded_by": sup})

    artifact_refs: set[str] = set()
    for fact in facts:
        artifact_refs.add(fact.get("source", ""))
    for blk in blockers:
        artifact_refs.update(as_list(blk.get("evidence")))
    artifact_refs.update(as_list(state.get("active_blocker", {}).get("evidence")))
    artifact_refs.add(state.get("current_milestone", {}).get("artifact", ""))
    artifact_refs.add(state.get("last_hardware_proven_milestone", {}).get("evidence", ""))
    artifact_refs.discard("")

    tooling = {
        "re": {
            "path": os.path.join(tooling_dir, "binary_analyzer", "target", "release", "re"),
            "exists": os.path.isfile(
                os.path.join(tooling_dir, "binary_analyzer", "target", "release", "re")
            ),
            "sha256": sha256_file(
                os.path.join(tooling_dir, "binary_analyzer", "target", "release", "re")
            ),
        },
        "re_db": db,
        "re_bootstrap": {
            "script": "scripts/re-bootstrap.sh",
            "present": os.path.isfile(os.path.join(root, "scripts", "re-bootstrap.sh")),
        },
        "ghidra": {
            "path": ghidra_home,
            "available": os.path.isfile(ghidra_headless),
            "wrapper": "scripts/ghidra_headless.sh",
            "wrapper_present": os.path.isfile(
                os.path.join(root, "scripts", "ghidra_headless.sh")
            ),
        },
    }

    ctx = {
        "schema_version": 1,
        "project": "OpenBRCM",
        "generated_from_head": git_identity(root)["head"],
        "current_git": git_identity(root),
        "binary_identity": {
            "blob_path": blob_path,
            "blob_sha256": blob_sha,
            "re_db": db,
            "tooling_commit": (ledger.get("repos", {}).get("re_tooling", {}) or {}).get("head"),
        },
        "current_milestone": state.get("current_milestone"),
        "last_hardware_proven_milestone": state.get("last_hardware_proven_milestone"),
        "hardware_test_go": state.get("hardware_test_go"),
        "active_blocker": state.get("active_blocker"),
        "proven_facts": facts,
        "open_blockers": blockers,
        "superseded_claims": superseded,
        "current_call_path": state.get("current_call_path", []),
        "tooling": tooling,
        "canonical_sources": sorted(state.get("canonical_sources", [])),
        "artifact_references": sorted(artifact_refs),
        "integrity": {
            "sources_hash": sources_hash(
                state, ledger, blob_sha, db.get("meta_sha256"),
                db.get("schema_version"),
            ),
        },
    }
    return ctx


# --------------------------------------------------------------------------- #
# validation (Part F integrity)
# --------------------------------------------------------------------------- #

def validate_context(ctx: dict, root: str, state: dict, ledger: dict) -> list[str]:
    problems: list[str] = []

    facts = ctx.get("proven_facts", [])
    ids = [f.get("id") for f in facts]
    if len(ids) != len(set(ids)):
        dupes = sorted({i for i in ids if ids.count(i) > 1})
        problems.append(f"duplicate fact id(s): {', '.join(dupes)}")

    blockers = ctx.get("open_blockers", [])
    bids = [b.get("id") for b in blockers]
    if len(bids) != len(set(bids)):
        dupes = sorted({i for i in bids if bids.count(i) > 1})
        problems.append(f"duplicate blocker id(s): {', '.join(dupes)}")

    active = (ctx.get("active_blocker") or {}).get("id")
    if active and active not in bids:
        problems.append(f"active_blocker '{active}' is not in open_blockers")

    # referenced paths must resolve (artifact reference integrity)
    def check_paths(label: str, paths: Any) -> None:
        for p in as_list(paths):
            if not p:
                continue
            if p.startswith(("http://", "https://", "mailto:", "external:")):
                continue
            if not os.path.exists(os.path.join(root, p)):
                problems.append(f"{label} references missing path: {p}")

    for fact in facts:
        check_paths(f"proven_fact {fact.get('id')}", fact.get("source"))
        check_paths(f"proven_fact {fact.get('id')}", fact.get("evidence"))
    for blk in blockers:
        check_paths(f"blocker {blk.get('id')}", blk.get("evidence"))
    check_paths("active_blocker", (ctx.get("active_blocker") or {}).get("evidence"))
    check_paths("current_milestone", (ctx.get("current_milestone") or {}).get("artifact"))
    check_paths("last_hardware_proven_milestone",
                (ctx.get("last_hardware_proven_milestone") or {}).get("evidence"))
    check_paths("canonical_sources", ctx.get("canonical_sources"))

    # no PROVEN fact may point at a SUPERSEDED ledger record
    superseded_records = {
        rec.get("id")
        for rec in ledger.get("records", [])
        if as_list(rec.get("superseded_by"))
    }
    for fact in facts:
        rec = fact.get("record")
        if rec and rec in superseded_records and not fact.get("revalidation_ok"):
            problems.append(
                f"proven_fact {fact.get('id')} points at SUPERSEDED record {rec}"
            )

    # current milestone must be named in the canonical narrative state
    ms_id = (ctx.get("current_milestone") or {}).get("id")
    if ms_id:
        blob = ""
        for rel in ("docs/agent-state.md", "docs/milestones.md"):
            try:
                with open(os.path.join(root, rel), "r", encoding="utf-8") as fh:
                    blob += fh.read()
            except OSError:
                pass
        if ms_id not in blob:
            problems.append(f"current_milestone '{ms_id}' not found in agent-state.md/milestones.md")

    # binary identity + schema support
    blob_sha = (ctx.get("binary_identity") or {}).get("blob_sha256")
    meta_sha = ((ctx.get("binary_identity") or {}).get("re_db") or {}).get("meta_sha256")
    if blob_sha and meta_sha and blob_sha != meta_sha:
        problems.append(
            f"binary identity mismatch: blob sha {blob_sha} != re.db indexed sha {meta_sha}"
        )
    schema = ((ctx.get("binary_identity") or {}).get("re_db") or {}).get("schema_version")
    if schema is not None and str(schema) not in SUPPORTED_DB_SCHEMAS:
        problems.append(f"unsupported re.db schema_version {schema}")

    return problems


# --------------------------------------------------------------------------- #
# markdown local-link scan
# --------------------------------------------------------------------------- #

_LINK_RE = re.compile(r"\]\(([^)]+)\)")


def scan_links(root: str, paths: list[str]) -> list[str]:
    problems: list[str] = []
    for rel in paths:
        full = rel if os.path.isabs(rel) else os.path.join(root, rel)
        if not os.path.isfile(full):
            continue
        try:
            with open(full, "r", encoding="utf-8") as fh:
                text = fh.read()
        except OSError:
            continue
        base = os.path.dirname(full)
        for raw in _LINK_RE.findall(text):
            target = raw.split("#", 1)[0].strip()
            if not target or target.startswith(("http://", "https://", "mailto:", "tel:")):
                continue
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1]
            resolved = os.path.normpath(os.path.join(base, target))
            if not os.path.exists(resolved):
                problems.append(f"{rel}: broken local link -> {raw}")
    return problems


# --------------------------------------------------------------------------- #
# main
# --------------------------------------------------------------------------- #

def default_root() -> str:
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=default_root())
    ap.add_argument("--tooling", default=None,
                    help="canonical iced/test tooling workspace (default: from ledger)")
    ap.add_argument("--state", default=None)
    ap.add_argument("--ledger", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--ghidra", default=DEFAULT_GHIDRA)
    ap.add_argument("--check", action="store_true",
                    help="exit non-zero if the checked-in context is stale")
    ap.add_argument("--validate", action="store_true",
                    help="run integrity checks only; do not write")
    ap.add_argument("--scan-links", nargs="*", default=None,
                    help="check local markdown links resolve; paths relative to root")
    args = ap.parse_args(argv)

    root = os.path.abspath(args.root)

    if args.scan_links is not None:
        problems = scan_links(root, args.scan_links)
        for p in problems:
            print(f"  FAIL: {p}", file=sys.stderr)
        if not problems:
            print("== link scan PASS ==")
        return 1 if problems else 0

    state_path = args.state or os.path.join(root, STATE_REL)
    ledger_path = args.ledger or os.path.join(root, LEDGER_REL)
    out_path = args.out or os.path.join(root, OUT_REL)

    try:
        state = read_json(state_path)
        ledger = read_json(ledger_path)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"  FAIL: cannot read source state: {exc}", file=sys.stderr)
        return 2

    tooling = args.tooling or (
        (ledger.get("repos", {}).get("re_tooling", {}) or {}).get("path")
        or DEFAULT_TOOLING
    )

    ctx = build_context(root, tooling, args.ghidra)
    problems = validate_context(ctx, root, state, ledger)

    if args.validate:
        for p in problems:
            print(f"  FAIL: {p}", file=sys.stderr)
        if not problems:
            print("== current-context validation PASS ==")
        return 1 if problems else 0

    if problems:
        print("== current-context generation FAILED (source conflict) ==", file=sys.stderr)
        for p in problems:
            print(f"  FAIL: {p}", file=sys.stderr)
        return 2

    if args.check:
        try:
            existing = read_json(out_path)
        except (OSError, json.JSONDecodeError) as exc:
            print(f"  FAIL: no readable {OUT_REL}: {exc}", file=sys.stderr)
            return 1
        stale = []
        if existing.get("integrity", {}).get("sources_hash") != ctx["integrity"]["sources_hash"]:
            stale.append("sources_hash mismatch (state/ledger/binary/tooling changed)")
        old_bi = existing.get("binary_identity") or {}
        new_bi = ctx.get("binary_identity") or {}
        for key in ("blob_sha256",):
            if old_bi.get(key) != new_bi.get(key):
                stale.append(f"binary identity mismatch: {key}")
        old_db = old_bi.get("re_db") or {}
        new_db = new_bi.get("re_db") or {}
        for key in ("meta_sha256", "schema_version"):
            if old_db.get(key) != new_db.get(key):
                stale.append(f"re.db identity mismatch: {key}")
        if existing.get("schema_version") != ctx.get("schema_version"):
            stale.append("schema_version mismatch")
        if stale:
            print("== current-context STALE ==", file=sys.stderr)
            for s in stale:
                print(f"  FAIL: {s}", file=sys.stderr)
            print(f"        regenerate: scripts/generate-current-context.py", file=sys.stderr)
            return 1
        old_head = existing.get("current_git", {}).get("head")
        new_head = ctx.get("current_git", {}).get("head")
        if old_head != new_head:
            print(f"  note: HEAD advanced {str(old_head)[:12]} -> {str(new_head)[:12]} "
                  f"(sources unchanged; not stale)")
        print("== current-context fresh ==")
        return 0

    text = json.dumps(ctx, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    if len(text.encode("utf-8")) > MAX_CONTEXT_BYTES:
        print(
            f"  FAIL: generated context is {len(text)} bytes > {MAX_CONTEXT_BYTES} "
            f"(keep it a compact index; move detail to artifacts)",
            file=sys.stderr,
        )
        return 2
    with open(out_path, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(f"wrote {out_path} ({len(text.encode('utf-8'))} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

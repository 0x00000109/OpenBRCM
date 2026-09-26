#!/usr/bin/env python3
"""Canonical cache-telemetry model + analysis (stdlib only).

This is the authoritative, testable specification of the OpenBRCM cache layer:

* the hash contract (``hash_parts``) that ``.opencode/plugins/openbrcm-cache.ts``
  mirrors,
* the frozen-epoch snapshot semantics (``FrozenEpochStore``) that
  ``.opencode/plugins/openbrcm-guard.ts`` implements at runtime,
* the JSONL parser / aggregator / fragmentation detector used by
  ``scripts/cache-report.py``.

Telemetry is optimization metadata only. Deleting every file this module reads
MUST NOT lose any reverse-engineering knowledge (that lives in re.db, milestone
artifacts, the ledger and docs).
"""

from __future__ import annotations

import hashlib
import json
from typing import Any, Dict, Iterable, List, Optional, Tuple

# --------------------------------------------------------------------- hashes


def sha256_hex(text) -> str:
    if isinstance(text, bytes):
        return hashlib.sha256(text).hexdigest()
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def hash_parts(parts: Iterable[str]) -> str:
    """Canonical length-prefixed ordered hash (mirrors the TS plugin).

    ``sha256( for each p: len(p) + ":" + p + "\\n" )``
    """
    h = hashlib.sha256()
    for p in parts:
        s = str(p)
        h.update(str(len(s)).encode("utf-8"))
        h.update(b":")
        h.update(s.encode("utf-8"))
        h.update(b"\n")
    return h.hexdigest()


def tools_hash(defs: Iterable[Tuple[str, str]]) -> str:
    """defs = (toolID, per_tool_hash) pairs; order-independent."""
    return hash_parts(sorted(f"{tid}|{h}" for tid, h in defs))


def tool_order_hash(ordered_ids: Iterable[str]) -> str:
    return hash_parts(list(ordered_ids))


def stable_system_hash(system: Iterable[str]) -> str:
    return hash_parts(list(system))


# ------------------------------------------------------------- frozen epochs


class FrozenEpochStore:
    """Reference model of the frozen session snapshot semantics.

    * a snapshot is captured once per (session, blocker) cache epoch;
    * re-reading the same epoch returns the identical bytes;
    * a blocker change or an explicit next-session starts a NEW epoch and
      captures a fresh snapshot;
    * current state changes within an epoch are carried as append-only deltas.
    """

    def __init__(self) -> None:
        self._frozen: Dict[Tuple[str, str], Dict[str, Any]] = {}

    def snapshot(
        self,
        session_id: str,
        blocker_id: Optional[str],
        read_context,  # callable() -> (context_bytes, state_bytes)
    ) -> Dict[str, Any]:
        key = (session_id, blocker_id or "")
        if key in self._frozen:
            return self._frozen[key]
        ctx, state = read_context()
        snap = {
            "session_id": session_id,
            "blocker_id": blocker_id,
            "current_context_sha256": sha256_hex(ctx),
            "frozen_context_sha256": sha256_hex(state),
            "current_context_bytes": len(ctx),
        }
        self._frozen[key] = snap
        return snap

    @staticmethod
    def state_delta(changes: Dict[str, Any]) -> str:
        """Render a compact append-only delta (never rewrites prior context)."""
        lines = ["STATE DELTA:"]
        if "commit" in changes:
            a, b = changes["commit"]
            lines.append(f"  commit: {a} -> {b}")
        for fact, (old, new) in (changes.get("facts") or {}).items():
            lines.append(f"  fact {fact}: {old} -> {new}")
        for blocker, (old, new) in (changes.get("blockers") or {}).items():
            lines.append(f"  blocker {blocker}: {old} -> {new}")
        return "\n".join(lines)


# ---------------------------------------------------------------- JSONL I/O


def load_records(path: str) -> List[Dict[str, Any]]:
    """Parse a telemetry JSONL file, skipping blank/malformed lines."""
    records: List[Dict[str, Any]] = []
    try:
        with open(path, "r", encoding="utf-8") as fh:
            for line in fh:
                line = line.strip()
                if not line:
                    continue
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if isinstance(obj, dict):
                    records.append(obj)
    except FileNotFoundError:
        pass
    return records


def _num(rec: Dict[str, Any], key: str) -> Optional[int]:
    v = rec.get(key)
    if v is None:
        return None
    try:
        return int(v)
    except (TypeError, ValueError):
        return None


def _ratio(hit: Optional[int], miss: Optional[int]) -> Optional[float]:
    if hit is None or miss is None:
        return None
    total = hit + miss
    if total <= 0:
        return None
    return hit / total


# ---------------------------------------------------------------- aggregation


def summarize(records: Iterable[Dict[str, Any]]) -> Dict[str, Any]:
    records = list(records)
    requests = [r for r in records if r.get("kind") == "request"]
    compactions = [r for r in records if r.get("kind") == "compaction"]

    total_hit = total_miss = total_out = total_reason = 0
    per_session: Dict[str, Dict[str, Any]] = {}
    per_blocker: Dict[str, Dict[str, Any]] = {}

    def bucket(d: Dict[str, Dict[str, Any]], key: Optional[str]) -> Dict[str, Any]:
        k = str(key)
        if k not in d:
            d[k] = {"requests": 0, "hit": 0, "miss": 0, "output": 0}
        return d[k]

    for idx, r in enumerate(requests):
        hit = _num(r, "cache_hit_tokens") or 0
        miss = _num(r, "cache_miss_tokens")
        if miss is None:
            miss = _num(r, "input_tokens") or 0
        out = _num(r, "output_tokens") or 0
        reason = _num(r, "reasoning_tokens") or 0
        total_hit += hit
        total_miss += miss
        total_out += out
        total_reason += reason
        for d, key in ((per_session, r.get("session")), (per_blocker, r.get("blocker_id"))):
            b = bucket(d, key)
            b["requests"] += 1
            b["hit"] += hit
            b["miss"] += miss
            b["output"] += out

    for d in (per_session, per_blocker):
        for b in d.values():
            b["hit_ratio"] = _ratio(b["hit"], b["miss"])

    first = requests[0] if requests else None
    subsequent = requests[1:]

    def agg(rs: List[Dict[str, Any]]) -> Optional[float]:
        h = sum((_num(r, "cache_hit_tokens") or 0) for r in rs)
        m = sum(((_num(r, "cache_miss_tokens") if _num(r, "cache_miss_tokens") is not None
                 else _num(r, "input_tokens")) or 0) for r in rs)
        return _ratio(h, m)

    return {
        "total_records": len(records),
        "total_requests": len(requests),
        "total_input_tokens": total_miss,
        "total_cache_hit_tokens": total_hit,
        "total_cache_miss_tokens": total_miss,
        "total_output_tokens": total_out,
        "total_reasoning_tokens": total_reason,
        "aggregate_hit_ratio": _ratio(total_hit, total_miss),
        "first_request_hit_ratio": _ratio(
            _num(first, "cache_hit_tokens") if first else None,
            (_num(first, "cache_miss_tokens") if first else None),
        ) if first else None,
        "subsequent_hit_ratio": agg(subsequent),
        "per_session": per_session,
        "per_blocker": per_blocker,
        "compaction_events": len(compactions),
    }


# -------------------------------------------------------------- instabilities


def _first_change(a: Optional[str], b: Optional[str], name: str) -> Optional[str]:
    if a is not None and b is not None and a != b:
        return name
    return None


def detect_fragmentation(
    records: Iterable[Dict[str, Any]], drop_threshold: float = 0.5
) -> List[Dict[str, Any]]:
    """Flag requests whose comparable hit ratio fell sharply.

    Correlation is NOT causation: the report lists the nearest structural
    change and explicitly does not claim a cause.
    """
    requests = [r for r in records if r.get("kind") == "request"]
    out: List[Dict[str, Any]] = []
    prev: Optional[Dict[str, Any]] = None
    for r in requests:
        cur = _ratio(_num(r, "cache_hit_tokens"), _num(r, "cache_miss_tokens"))
        if prev is not None:
            prev_r = _ratio(_num(prev, "cache_hit_tokens"), _num(prev, "cache_miss_tokens"))
            if prev_r is not None and cur is not None and prev_r >= 0.8 and cur < prev_r * drop_threshold:
                candidates = []
                for name, field in (
                    ("SYSTEM_HASH_CHANGED", "stable_system_hash"),
                    ("TOOLS_HASH_CHANGED", "tools_hash"),
                    ("TOOLS_ORDER_CHANGED", "tool_order_hash"),
                    ("USER_ID_CHANGED", "user_id_state"),
                    ("CONTEXT_SNAPSHOT_CHANGED", "current_context_file_hash"),
                    ("FROZEN_CONTEXT_CHANGED", "frozen_context_hash"),
                    ("MODEL_CHANGED", "model"),
                    ("MODEL_FINGERPRINT_CHANGED", "model_fingerprint"),
                ):
                    ch = _first_change(prev.get(field), r.get(field), name)
                    if ch:
                        candidates.append(ch)
                if r.get("compaction_generation", 0) != prev.get("compaction_generation", 0):
                    candidates.append("COMPACTION_OCCURRED")
                out.append({
                    "session": r.get("session"),
                    "seq": r.get("seq"),
                    "previous_hit_ratio": prev_r,
                    "current_hit_ratio": cur,
                    "structural_changes": candidates,
                    "causality": "NOT_CLAIMED",
                })
        prev = r
    return out


def detect_user_id_volatility(records: Iterable[Dict[str, Any]]) -> List[str]:
    seen = []
    for r in records:
        if r.get("kind") != "request":
            continue
        v = r.get("user_id_state", "absent")
        if v not in seen:
            seen.append(v)
    return seen


def detect_tool_order_volatility(records: Iterable[Dict[str, Any]]) -> bool:
    hashes = {r.get("tool_order_hash") for r in records if r.get("kind") == "request"}
    hashes.discard(None)
    return len(hashes) > 1


def detect_compaction_events(records: Iterable[Dict[str, Any]]) -> List[int]:
    seqs = []
    for r in records:
        if r.get("kind") == "compaction" or r.get("request_kind") == "compaction":
            seqs.append(r.get("seq", -1))
    return seqs

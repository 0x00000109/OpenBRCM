#!/usr/bin/env python3
"""Deterministic local DeepSeek cache report.

Reads the out-of-band telemetry written by
``.opencode/plugins/openbrcm-cache.ts`` and prints an aggregate hit/miss view,
per-session/per-blocker statistics, compaction boundaries and cache-
fragmentation suspects. Local only; no network.

Usage:
    scripts/cache-report.py [--file PATH] [--json] [--tail N]

Exit status is always 0 for a successful parse; this tool is diagnostic and is
never a CI correctness gate (DeepSeek caching is best-effort).
"""

from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cache_telemetry as ct  # noqa: E402

DEFAULT = ".openbrcm-local/cache-telemetry.jsonl"


def pct(x):
    return "n/a" if x is None else f"{x * 100:.1f}%"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="OpenBRCM DeepSeek cache report")
    ap.add_argument("--file", default=DEFAULT, help="telemetry JSONL path")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    ap.add_argument("--tail", type=int, default=20, help="per-request rows to show")
    args = ap.parse_args(argv)

    records = ct.load_records(args.file)
    summary = ct.summarize(records)
    frag = ct.detect_fragmentation(records)
    uid = ct.detect_user_id_volatility(records)
    tool_order_volatile = ct.detect_tool_order_volatility(records)
    compactions = ct.detect_compaction_events(records)

    if args.json:
        print(json.dumps({
            "file": args.file,
            "summary": summary,
            "fragmentation": frag,
            "user_id_states": uid,
            "tool_order_volatile": tool_order_volatile,
            "compaction_events": compactions,
        }, indent=2, sort_keys=True))
        return 0

    print(f"# OpenBRCM DeepSeek cache report — {args.file}")
    print(f"records={summary['total_records']} requests={summary['total_requests']}")
    print(
        f"input(miss)={summary['total_input_tokens']} "
        f"cache_hit={summary['total_cache_hit_tokens']} "
        f"output={summary['total_output_tokens']} "
        f"reasoning={summary['total_reasoning_tokens']}"
    )
    print(f"aggregate_hit_ratio={pct(summary['aggregate_hit_ratio'])}")
    print(
        f"first_request={pct(summary['first_request_hit_ratio'])} "
        f"subsequent={pct(summary['subsequent_hit_ratio'])}"
    )
    print(f"compaction_events={summary['compaction_events']}")
    print(f"user_id_states={uid}  tool_order_volatile={tool_order_volatile}")

    print("\n## per-blocker")
    for k, v in sorted(summary["per_blocker"].items()):
        print(f"  {k}: req={v['requests']} hit={pct(v['hit_ratio'])}")

    print("\n## per-session")
    for k, v in sorted(summary["per_session"].items()):
        print(f"  {k}: req={v['requests']} hit={pct(v['hit_ratio'])}")

    print("\n## last requests")
    reqs = [r for r in records if r.get("kind") == "request"][-args.tail:]
    for r in reqs:
        hit = ct._num(r, "cache_hit_tokens")
        miss = ct._num(r, "cache_miss_tokens")
        print(
            f"  seq={r.get('seq')} kind={r.get('request_kind')} "
            f"hit={hit} miss={miss} ratio={pct(ct._ratio(hit, miss))} "
            f"sys={r.get('stable_system_hash')} tools={r.get('tools_hash')} "
            f"ctx={r.get('current_context_file_hash')}"
        )

    if frag:
        print("\n## CACHE FRAGMENTATION SUSPECTED (correlation, not causation)")
        for f in frag:
            print(
                f"  session={f['session']} seq={f['seq']} "
                f"{pct(f['previous_hit_ratio'])} -> {pct(f['current_hit_ratio'])} "
                f"changes={f['structural_changes'] or ['NONE_OBSERVED']}"
            )
    else:
        print("\nno sharp fragmentation events detected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

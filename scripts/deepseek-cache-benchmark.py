#!/usr/bin/env python3
"""Bounded DeepSeek prompt-cache benchmark (diagnostic only).

Sends at most 6 tiny Chat Completions requests with a stable, sufficiently
large synthetic prefix and a changing suffix, and prints the observed
prompt/cache-hit/cache-miss tokens. It is a diagnostic for prefix stability —
NEVER a CI gate (DeepSeek caching is best-effort).

Safety / cost bounds:
  * max 6 requests, max ~48 output tokens each;
  * no tool calls, no file content, no source code, no historical artifacts;
  * the API key is read from $DEEPSEEK_API_KEY only — never printed, never
    scraped from config or auth files;
  * if the key is absent the benchmark is NOT run (and that is not a failure).

Usage:
    scripts/deepseek-cache-benchmark.py --mock
    scripts/deepseek-cache-benchmark.py                 # live if key present
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.request

API_URL = os.environ.get("DEEPSEEK_BASE_URL", "https://api.deepseek.com") + "/chat/completions"
MODEL = os.environ.get("DEEPSEEK_BENCH_MODEL", "deepseek-chat")

# ~3k-token stable prefix: neutral, non-proprietary instruction text. No source.
_STABLE_UNIT = (
    "The following is a stable reference block used only to measure prefix "
    "cache behaviour. It contains no secrets, no source code and no user data. "
    "It is repeated to reach a stable prefix length. "
) * 120
STABLE_PREFIX = "STABLE-PREFIX-BEGIN\n" + _STABLE_UNIT + "\nSTABLE-PREFIX-END"


def _estimate_tokens(text: str) -> int:
    return (len(text) + 3) // 4


def build_messages(probe: str):
    return [
        {"role": "system", "content": "You are a cache probe. Reply with one word."},
        {"role": "user", "content": STABLE_PREFIX + "\nprobe=" + probe},
    ]


def live_request(probe: str, key: str, max_tokens: int) -> dict:
    body = json.dumps({
        "model": MODEL,
        "messages": build_messages(probe),
        "max_tokens": max_tokens,
        "temperature": 0,
        "stream": False,
    }).encode("utf-8")
    req = urllib.request.Request(
        API_URL, data=body,
        headers={"Content-Type": "application/json", "Authorization": "Bearer " + key},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    usage = data.get("usage", {}) or {}
    hit = usage.get("prompt_cache_hit_tokens")
    miss = usage.get("prompt_cache_miss_tokens")
    if hit is None and miss is None:
        details = usage.get("prompt_tokens_details") or {}
        hit = details.get("cached_tokens", 0)
        miss = (usage.get("prompt_tokens", 0) or 0) - (hit or 0)
    return {
        "prompt_tokens": usage.get("prompt_tokens"),
        "cache_hit_tokens": hit,
        "cache_miss_tokens": miss,
        "output_tokens": usage.get("completion_tokens"),
    }


def mock_request(probe: str, index: int) -> dict:
    # Deterministic simulation: first request warms the prefix, later ones hit.
    prefix = _estimate_tokens(STABLE_PREFIX)
    miss = prefix if index == 0 else int(prefix * 0.05)
    hit = 0 if index == 0 else prefix - miss
    return {
        "prompt_tokens": prefix + 4,
        "cache_hit_tokens": hit,
        "cache_miss_tokens": miss,
        "output_tokens": 2,
    }


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="bounded DeepSeek cache benchmark")
    ap.add_argument("--mock", action="store_true", help="no network; simulate usage")
    ap.add_argument("--max-requests", type=int, default=3)
    ap.add_argument("--max-tokens", type=int, default=8)
    args = ap.parse_args(argv)

    n = max(1, min(args.max_requests, 6))
    max_tokens = max(1, min(args.max_tokens, 48))
    probes = ["A", "B", "C", "D", "E", "F"][:n]

    prefix_tokens = _estimate_tokens(STABLE_PREFIX)
    print(f"model={MODEL} api={API_URL}")
    print(f"prefix_bytes={len(STABLE_PREFIX)} est_prefix_tokens={prefix_tokens}")
    print(f"requests={n} max_output_tokens={max_tokens}")
    print(f"estimated max input tokens total={prefix_tokens * n} "
          f"output tokens total={max_tokens * n}")
    print("no tools, no file content, no source code, short output only")

    key = os.environ.get("DEEPSEEK_API_KEY")
    if not args.mock and not key:
        print("LIVE BENCHMARK NOT RUN: $DEEPSEEK_API_KEY is not set "
              "(key is never scraped from config). Use --mock to validate logic.")
        return 0

    print("\n#   probe  prompt  hit  miss  hit%")
    rows = []
    for i, probe in enumerate(probes):
        u = mock_request(probe, i) if args.mock else live_request(probe, key, max_tokens)
        hit = u.get("cache_hit_tokens") or 0
        miss = u.get("cache_miss_tokens") or 0
        ratio = (hit / (hit + miss)) if (hit + miss) else None
        rows.append(u)
        print(f"{i + 1:<3} {probe:<6} {u.get('prompt_tokens')}  {hit}  {miss}  "
              f"{'n/a' if ratio is None else f'{ratio * 100:.1f}%'}")
    print("\nnote: request 1 need not hit; caching is best-effort and diagnostic.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

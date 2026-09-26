# Cache telemetry

Out-of-band, local-only observation of the DeepSeek request path. It exists to
**observe before optimizing** and to detect prefix fragmentation. It is never a
correctness gate.

## Where

| Item | Path |
|---|---|
| Capture plugin | `.opencode/plugins/openbrcm-cache.ts` (auto-discovered) |
| Telemetry log | `.openbrcm-local/cache-telemetry.jsonl` (gitignored) |
| Report tool | `scripts/cache-report.py` |
| Canonical model / hashes | `scripts/cache_telemetry.py` |
| Offline tests | `tests/host/test_cache_telemetry.py`, `tests/host/test_cache_plugin.py` |
| Benchmark (bounded) | `scripts/deepseek-cache-benchmark.py` |

`.openbrcm-local/` is gitignored. Telemetry is **never** transmitted anywhere.

## Privacy / non-perturbation guarantees

The capture plugin:

- records **only** token counts, hashes and booleans;
- never records API keys, `Authorization`/`Bearer` headers, raw prompts, source
  code, tool outputs, message parts/content or personal data;
- never mutates the `system` or tool arrays it observes;
- never injects timestamps/request ids into the model context;
- is fail-open: any telemetry error is swallowed and never breaks a request.

These are enforced by `tests/host/test_cache_plugin.py` (static audit) and by
the bounded record schema asserted in `tests/host/test_cache_telemetry.py`.

## Record schema (kind = "request")

| field | meaning |
|---|---|
| `seq`, `session` (hash), `blocker_id` | ordering / grouping |
| `request_kind`, `mode` | agent / compaction |
| `protocol`, `provider`, `model` | `openai-chat-completions`, `deepseek/deepseek-flash` |
| `prompt_tokens` | hit + miss |
| `input_tokens`, `cache_miss_tokens` | `prompt_cache_miss_tokens` |
| `cache_hit_tokens` | `prompt_cache_hit_tokens` |
| `cache_hit_ratio` | hit / (hit + miss) |
| `output_tokens`, `reasoning_tokens` | completion / reasoning usage |
| `stable_system_hash` | hash of the observed system array in order |
| `tools_hash`, `tool_order_hash`, `tool_count` | tool-definition stability |
| `frozen_context_hash`, `current_context_file_hash` | frozen session snapshot |
| `model_fingerprint`, `user_id_state` | request-shaping stability |

**Token semantics:** OpenCode stores `tokens.input` as the *uncached* prompt
tokens (DeepSeek `prompt_cache_miss_tokens`) and `tokens.cache.read` as the hit
tokens (`prompt_cache_hit_tokens`). Confirmed against live usage.

## Report

```sh
scripts/cache-report.py                 # human summary
scripts/cache-report.py --json          # machine summary
```

It reports totals, aggregate/first/subsequent hit ratios, per-session and
per-blocker stats, compaction boundaries, and `user_id`/tool-order volatility.
It never claims causality: fragmentation entries list the nearest structural
change and mark `causality: NOT_CLAIMED`.

## Fragmentation detector

The report flags a request when a previously-high comparable reuse drops sharply
and lists candidate structural changes:

`SYSTEM_HASH_CHANGED`, `TOOLS_HASH_CHANGED`, `TOOLS_ORDER_CHANGED`,
`USER_ID_CHANGED`, `CONTEXT_SNAPSHOT_CHANGED`, `FROZEN_CONTEXT_CHANGED`,
`MODEL_CHANGED`, `MODEL_FINGERPRINT_CHANGED`, `COMPACTION_OCCURRED`.

If a sharp drop has **no** listed structural change it prints `NONE_OBSERVED`
(e.g. best-effort cache expiry) and does not invent a cause.

## Benchmark

`scripts/deepseek-cache-benchmark.py` sends at most **6** tiny requests with a
stable synthetic prefix and a changing suffix (`probe=A/B/C`), short output, no
tools and no file/source content. It prints an input/output token upper bound
before running. It uses `$DEEPSEEK_API_KEY` only; if unset it prints
`LIVE BENCHMARK NOT RUN` (never scrapes config) and `--mock` validates the logic
offline.

## Response / reasoning economics (telemetry only)

The records carry `output_tokens`, `reasoning_tokens`, `request_kind` and
`blocker_id`. This is enough to support a **later** model/reasoning router but
this layer does **not** switch models or effort. Recommendation only: mechanical
tasks may use lower/non-thinking effort where supported; ambiguous RE work
benefits from higher effort. The user's selected model and variant are never
changed.

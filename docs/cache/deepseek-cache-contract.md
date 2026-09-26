# DeepSeek prompt-cache contract (verified)

Short, authoritative record of the DeepSeek behaviour the OpenBRCM cache layer
is allowed to assume. Verified 2026-09 against the live official docs
(`api-docs.deepseek.com`). If this file and the live docs disagree, the live
docs win and this file must be corrected.

## Facts used

| # | Fact | Source |
|---|------|--------|
| 1 | Context caching is **automatic** and enabled by default for all users; no code change needed. | Context Caching guide |
| 2 | Cache matching is **prefix-based**: the overlapping prefix of a later request is served from cache ("cache hit"). | Context Caching guide |
| 3 | Usage exposes `prompt_cache_hit_tokens` (hit) and `prompt_cache_miss_tokens` (miss) on the Chat Completions path. On the Responses path it is `input_tokens` + `input_tokens_details.cached_tokens`. | Context Caching guide / Responses guide |
| 4 | Caching is **best-effort**; 100% hit rate is not guaranteed. | Context Caching guide |
| 5 | Persisted cache is cleared when unused, **usually within hours to a few days**; not guaranteed. | Context Caching guide |
| 6 | Cache is persisted at **request boundaries** (end of input, end of output), at a **detected common prefix**, and at **fixed token intervals** for long inputs/outputs. A later request hits only if it **fully matches a persisted prefix unit**. | Context Caching guide |
| 7 | The current guide (Sliding Window Attention) does **not** state a 64-token cache block. The historical mandatory-64-token rule is **not** relied on. | Context Caching guide (no "64" present) |
| 8 | DeepSeek **Responses API is stateless**: `previous_response_id` / `conversation` / `store` are unsupported (`store` always `false`); clients resend context each call. | Responses API guide |
| 9 | `prompt_cache_key` / `prompt_cache_retention` are **not supported**. Caching is managed automatically; no manual cache key is required or accepted. | Responses API guide |
| 10 | `user_id` is **not mentioned** in the context-caching guide. It appears only as an optional API-field identifier (abuse/rate-limit), not as a documented KVCache isolation boundary. Treat any claim that user_id partitions the cache as **unconfirmed**. | Context Caching guide (absent) / API field |

## Compliance rules for OpenBRCM

- Keep a **stable prefix** (`AGENTS.md` governance) and a **dynamic tail**
  (transcript, turn, tool results). Never move volatile bytes into the prefix.
- Do not rely on a fixed cache block size; measure the real hit ratio.
- A live hit-% is **diagnostic only** and MUST NOT be a CI correctness gate.
- Do not send a manual cache key; it is unsupported.
- Do not add a personal `user_id`. If one is ever needed it must be a stable,
  non-personal project identifier (`openbrcm-re`) and documented.
- Provider/protocol in use: `deepseek/deepseek-flash` via
  `@ai-sdk/openai-compatible` → **OpenAI-compatible Chat Completions**.

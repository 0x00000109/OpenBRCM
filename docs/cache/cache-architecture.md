# OpenBRCM prompt-cache architecture

How OpenBRCM keeps the DeepSeek request prefix stable so prefix-based context
caching can work, without changing any driver, hardware or RE behaviour. See
[`deepseek-cache-contract.md`](deepseek-cache-contract.md) for the verified
provider facts and [`cache-telemetry.md`](cache-telemetry.md) for the
observation layer.

## Stable prefix / dynamic tail

A DeepSeek request is matched as a single prefix. Everything before the first
changed byte is a cache hit; everything after it is a miss. Therefore the model
context is split into exactly two regions:

| Region | Contents | Change rate |
|---|---|---|
| **Stable prefix** | `AGENTS.md` governance (status vocabulary, safety, evidence/provenance, PROVEN-fact immutability, one-blocker-one-task, delta-only output), `docs/re-tooling.md`, the frozen project snapshot, the tool schemas. | per release / per session |
| **Dynamic tail** | the user turn, tool calls and results, appended `STATE DELTA`s, assistant output. | every turn |

Rules:

- No timestamps, HEAD hashes, active-blocker ids, GO/NO-GO, request ids or live
  token statistics in the prefix.
- The prefix is **append-only within an epoch**: never rewrite, reorder or
  re-summarise earlier stable bytes mid-blocker.
- Detailed results live in tracked artifacts; the tail references them instead
  of repasting them. Use minimal `re packet` evidence.

## Frozen session snapshot

`docs/current-context.json` remains **canonical-current on disk**. A running
session uses a **frozen snapshot** plus append-only deltas:

```
new session / new blocker
        |
        v
read docs/current-context.json  ->  validate  ->  FREEZE (hash recorded)
        |
        v
... blocker work (HEAD/artifacts/state may change) ...
        |
        +--> append "STATE DELTA:" turns; do NOT re-inject a regenerated snapshot
        |
        v
blocker closed -> regenerate + validate current-context on disk -> next session
```

Implementation:

- `.opencode/plugins/openbrcm-guard.ts` injects a compact project context from
  `docs/agent-state.md`. It now reads and freezes that text **once per
  `sessionID`** (`frozenStateText`) instead of re-reading it on every request.
  A documentation commit mid-blocker no longer rewrites the earliest system
  bytes.
- `.opencode/plugins/openbrcm-cache.ts` freezes `current-context.json` /
  `agent-state.md` **hashes** per session and records them with every request so
  a mismatch is visible in telemetry.
- `docs/current-context.json` itself is consumed with the `read` tool
  (option A), once at startup — it is **not** injected into the system prompt,
  so it cannot fragment the prefix by itself.
- The reference model and its offline tests live in
  `scripts/cache_telemetry.py` (`FrozenEpochStore`).

## One blocker = one cache epoch

```
NEW SESSION
  stable cache spine
  frozen current-context snapshot
  exactly ONE active blocker
  minimal re packet -> append-only evidence loop
  persist artifacts
  close blocker
  regenerate + validate current-context
  END CACHE EPOCH        (prefer a new session for the next blocker)
```

This is a workflow rule; it never deletes or forces a user's session. A blocker
change or an explicit new session is what starts a new epoch. If compaction is
unavoidable it is treated as a **cache-epoch reset** and recorded by telemetry.

## Observed request path (OpenCode 1.18.32)

```
USER PROMPT
  -> OpenCode built-in system (env, tools, agent prompt)        [opencode-controlled]
  -> instructions: AGENTS.md, docs/agent-state.md, docs/re-tooling.md
  -> plugins:
       re-hooks.ts            (loop guard; no system text)
       openbrcm-guard.ts      (frozen agent-state snapshot push)
       openbrcm-re.ts         (constant tool-first RE block)
       openbrcm-cache.ts      (OBSERVE ONLY - never mutates)
  -> docs/current-context.json   (read via tool, NOT injected)
  -> tool schemas (tool.definition)
  -> provider lowering: @ai-sdk/openai-compatible -> Chat Completions
  -> DeepSeek HTTP request (prefix: system + tools + messages)
```

## Non-goals / boundaries

- Cache files, snapshots and telemetry are **optimization metadata**, never a
  source of truth. Deleting them loses no reverse-engineering knowledge.
- A live cache-hit percentage is **diagnostic only** and is never a CI gate
  (caching is best-effort).
- No driver runtime, MMIO, hardware or `re.db` change is involved.

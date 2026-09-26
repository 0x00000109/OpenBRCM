/**
 * OpenBRCM DeepSeek prompt-cache telemetry (project-local, auto-discovered).
 *
 * Purpose: observe the ACTUAL request path (system prefix, tools, model
 * options, DeepSeek cache-hit usage) and append an out-of-band JSONL record so
 * prefix instability can be detected. It never changes what the model sees.
 *
 * Hard constraints (enforced here, audited by tests/host/test_cache_plugin.py):
 *   - never records API keys / Authorization headers / raw prompts / source /
 *     tool output / personal data;
 *   - never transmits telemetry anywhere (local append-only file only);
 *   - never modifies model output or the system/tools arrays it observes;
 *   - never injects timestamps / request ids into the model context;
 *   - fail-open: any telemetry error is swallowed and never breaks a request.
 *
 * Canonical hash spec (mirrored in scripts/cache_telemetry.py; if this comment
 * and the Python module disagree, the Python module + tests are authoritative):
 *   hashParts(parts) = sha256( for each p: len(p) + ":" + p + "\n" )
 *   tools_hash       = hashParts(sorted("id|sha256(desc)|sha256(json(params))"))
 *   tool_order_hash  = hashParts(ordered tool ids)
 *   stable_system_hash = hashParts(system array in order)
 *
 * Audited against OpenCode 1.18.32 / @opencode-ai/plugin 1.18.30.
 */

import type { Plugin } from "@opencode-ai/plugin"
import { createHash } from "node:crypto"
import { appendFileSync, existsSync, mkdirSync, readFileSync } from "node:fs"
import { join } from "node:path"

const PROTOCOL = "openai-chat-completions"
const PROMPT_CACHE_NOTE =
  "tokens.input = prompt_cache_miss_tokens; tokens.cache.read = prompt_cache_hit_tokens"

function sha256(text: string): string {
  return createHash("sha256").update(text, "utf8").digest("hex")
}

/** Canonical length-prefixed hash over an ordered list of strings. */
export function hashParts(parts: string[]): string {
  const h = createHash("sha256")
  for (const p of parts) {
    const s = String(p)
    h.update(String(s.length), "utf8")
    h.update(":", "utf8")
    h.update(s, "utf8")
    h.update("\n", "utf8")
  }
  return h.digest("hex")
}

function readText(path: string): string | null {
  try {
    return readFileSync(path, "utf8")
  } catch {
    return null
  }
}

type SessionState = {
  sessionHash: string
  blockerId: string | null
  frozenContextHash: string | null
  currentContextFileHash: string | null
  stableSystemHash: string | null
  modelFingerprint: string | null
  userIDState: string
  compactionGeneration: number
  seq: number
  turn: number
}

export default (async (input: any) => {
  const repo: string = input?.worktree || input?.directory || process.cwd()
  const dir = join(repo, ".openbrcm-local")
  const file = join(dir, "cache-telemetry.jsonl")

  let writeEnabled = true
  try {
    if (!existsSync(dir)) mkdirSync(dir, { recursive: true })
  } catch {
    writeEnabled = false
  }

  const sessions = new Map<string, SessionState>()
  const seenMessages = new Set<string>()

  const toolDefs = new Map<string, string>() // toolID -> per-tool hash
  const toolOrder: string[] = []

  function sessionState(sessionID: string): SessionState {
    let st = sessions.get(sessionID)
    if (st) return st
    st = {
      sessionHash: sha256(sessionID).slice(0, 16),
      blockerId: null,
      frozenContextHash: null,
      currentContextFileHash: null,
      stableSystemHash: null,
      modelFingerprint: null,
      userIDState: "absent",
      compactionGeneration: 0,
      seq: 0,
      turn: 0,
    }
    // Freeze the project state snapshot ONCE per session/cache epoch.
    let ctxText = readText(join(repo, "docs", "current-context.json"))
    if (ctxText) {
      st.currentContextFileHash = sha256(ctxText).slice(0, 16)
      try {
        const parsed = JSON.parse(ctxText)
        const b = parsed?.active_blocker ?? parsed?.activeBlocker
        st.blockerId = typeof b === "string" ? b : (b?.id ?? null)
      } catch {
        /* keep null */
      }
    }
    const stateText = readText(join(repo, "docs", "agent-state.md"))
    if (stateText) st.frozenContextHash = sha256(stateText).slice(0, 16)
    sessions.set(sessionID, st)
    return st
  }

  function record(rec: Record<string, unknown>): void {
    if (!writeEnabled) return
    try {
      appendFileSync(file, JSON.stringify(rec) + "\n", "utf8")
    } catch {
      writeEnabled = false
    }
  }

  return {
    // Observe the project state snapshot at session start; never re-read it
    // mid-blocker (that is the frozen-session-snapshot contract).
    // NOTE: hashes the system array as seen at this plugin's transform point;
    // hook order may exclude later plugin pushes. The OpenCode `instructions`
    // (the main fragmentation source) are always included.
    "experimental.chat.system.transform": async (args: any, output: any) => {
      try {
        const sid: string = args?.sessionID ?? "unknown"
        const st = sessionState(sid)
        if (Array.isArray(output?.system)) {
          st.stableSystemHash = hashParts(output.system as string[]).slice(0, 16)
        }
      } catch {
        /* fail open */
      }
    },

    // Capture tool definitions and order (global; tool registry is per process).
    "tool.definition": async (args: any, output: any) => {
      try {
        const id: string = args?.toolID ?? "?"
        const per = sha256(
          "desc=" + sha256(String(output?.description ?? "")) + ";params=" +
            sha256(JSON.stringify(output?.parameters ?? null)),
        ).slice(0, 16)
        toolDefs.set(id, per)
        if (!toolOrder.includes(id)) toolOrder.push(id)
      } catch {
        /* fail open */
      }
    },

    // Capture model options fingerprint + user_id state (body), per session.
    "chat.params": async (args: any, output: any) => {
      try {
        const sid: string = args?.sessionID ?? "unknown"
        const st = sessionState(sid)
        const opts = output?.options ?? {}
        const fp = {
          temperature: output?.temperature ?? null,
          topP: output?.topP ?? null,
          topK: output?.topK ?? null,
          maxOutputTokens: output?.maxOutputTokens ?? null,
          optionKeys: Object.keys(opts).sort(),
        }
        st.modelFingerprint = sha256(JSON.stringify(fp)).slice(0, 16)
        const uid = opts.user ?? opts.user_id
        st.userIDState = typeof uid === "string" ? "present" : "absent"
      } catch {
        /* fail open */
      }
    },

    // One record per completed assistant message: token/cache usage + hashes.
    event: async (args: any) => {
      try {
        const ev = args?.event
        if (!ev || typeof ev !== "object") return

        if (ev.type === "session.compacted") {
          const sid = ev.properties?.sessionID
          if (typeof sid === "string") {
            const st = sessionState(sid)
            st.compactionGeneration += 1
            record({
              ts: Date.now(),
              kind: "compaction",
              session: st.sessionHash,
              blocker_id: st.blockerId,
              compaction_generation: st.compactionGeneration,
              epoch_reset: true,
            })
          }
          return
        }

        if (ev.type !== "message.updated") return
        const info = ev.properties?.info
        if (!info || info.role !== "assistant") return
        if (!info.tokens || !info.time?.completed) return
        const sid: string = info.sessionID ?? "unknown"
        const st = sessionState(sid)

        // Deduplicate: message.updated fires repeatedly while streaming.
        const dedupeKey = sid + ":" + (info.id ?? "")
        if (info.id && seenMessages.has(dedupeKey)) return
        if (info.id) seenMessages.add(dedupeKey)

        const miss: number = Number(info.tokens.input) || 0
        const hit: number = Number(info.tokens.cache?.read) || 0
        const prompt = miss + hit
        st.seq += 1
        record({
          ts: Date.now(),
          kind: "request",
          seq: st.seq,
          session: st.sessionHash,
          blocker_id: st.blockerId,
          request_kind: info.summary === true ? "compaction" : "agent",
          mode: info.mode ?? null,
          protocol: PROTOCOL,
          provider: info.providerID ?? null,
          model: info.modelID ? `${info.providerID}/${info.modelID}` : null,
          prompt_tokens: prompt,
          input_tokens: miss,
          cache_hit_tokens: hit,
          cache_miss_tokens: miss,
          cache_hit_ratio: prompt > 0 ? hit / prompt : null,
          output_tokens: Number(info.tokens.output) || 0,
          reasoning_tokens: Number(info.tokens.reasoning) || 0,
          system_fingerprint: null,
          stable_system_hash: st.stableSystemHash,
          tools_hash: hashParts(
            Array.from(toolDefs.entries())
              .map(([id, h]) => `${id}|${h}`)
              .sort(),
          ).slice(0, 16),
          tool_order_hash: hashParts(toolOrder).slice(0, 16),
          tool_count: toolOrder.length,
          frozen_context_hash: st.frozenContextHash,
          current_context_file_hash: st.currentContextFileHash,
          model_fingerprint: st.modelFingerprint,
          user_id_state: st.userIDState,
          compaction_generation: st.compactionGeneration,
          note: PROMPT_CACHE_NOTE,
        })
      } catch {
        /* fail open */
      }
    },

    // Turn counter (append-only growth boundary). No model-visible effect.
    "chat.message": async (args: any) => {
      try {
        const sid: string = args?.sessionID ?? "unknown"
        const st = sessionState(sid)
        st.turn += 1
      } catch {
        /* fail open */
      }
    },
  }
}) satisfies Plugin

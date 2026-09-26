/**
 * OpenBRCM tool-first RE enforcement (project-local, auto-discovered).
 *
 * Purpose: stop the process regression where an agent reverse-engineering from
 * OpenBRCM falls back to manual `objdump`/`grep` and never touches the
 * deterministic `re` index.
 *
 * It is deliberately soft: it warns, it never blocks manual verification.
 * Policy: deterministic query first, manual fallback second.
 *
 * The generic loop/time guards live in the canonical tooling-repo plugin
 * (re-hooks.ts), which `.opencode/opencode.json` also loads. This file only
 * adds OpenBRCM-specific context and the manual-RE-vs-`re` warning.
 *
 * No network, no hardware, no git mutation. Audited against OpenCode 1.18.32.
 */

import type { Plugin } from "@opencode-ai/plugin"

const RE_BIN =
  "/media/kartashoff/Storage/opensource/iced/test/binary_analyzer/target/release/re"
const RE_DB = "/media/kartashoff/Storage/opensource/iced/test/re.db"
const RE_BLOB_SHA =
  "352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743"

// Any genuine use of the deterministic index.
const RE_USE_RE = new RegExp(
  [
    RE_BIN.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"),
    "\\bre\\s+(fn|card|fields|switch|seq|flow|data|gstruct|tables|phy|refs|",
    "packet|clusters|stats|reach|named|verify|db|osl|gaps|externals|map|modules|",
    "ptrtable|params|stage[4-8])\\b",
    "generate-current-context\\.py",
    "re-bootstrap\\.sh",
  ].join("|"),
)

// Manual disassembly / binary inspection tools.
const MANUAL_RE =
  /\b(objdump|readelf|ndisasm|radare2|rizin|rz-bin|rz-asm|ghidra|r2)\b|\.dis\b|\bdisasm\b/i

const GAP_HINT = "docs/re-tooling.md"

export default (async (_input) => {
  let reUsed = false
  let manualRe = 0
  let warnedAt = 0

  const reOk = () => {
    reUsed = true
  }

  const warnIfRegressing = () => {
    // Escalating, non-blocking reminder: after 3 and again after 8 manual
    // RE invocations without ever querying the index.
    if (reUsed) return
    if ((manualRe === 3 && warnedAt < 3) || (manualRe >= 8 && warnedAt < 8)) {
      warnedAt = manualRe
      console.warn(
        "[openbrcm-re] WARNING: " +
          manualRe +
          " manual RE command(s) and no `re` query yet. " +
          "Query the deterministic index first (re fn/card/fields/switch/seq), " +
          "then use objdump only to verify or for facts re.db lacks. " +
          "Canonical paths + commands: " +
          GAP_HINT +
          ". Verify with scripts/re-bootstrap.sh.",
      )
    }
  }

  return {
    "experimental.chat.system.transform": async (_in, output: any) => {
      if (!Array.isArray(output?.system)) return
      output.system.push(
        [
          "## OpenBRCM tool-first RE (persistent project rule)",
          "Before ANY manual objdump/readelf/r2/grep over the vendor blob, query",
          "the deterministic index and its Rust CLI `re`:",
          "  re:     " + RE_BIN,
          "  db:     " + RE_DB + "  (blob sha256 " + RE_BLOB_SHA.slice(0, 12) + "…)",
          "  doc:    " + GAP_HINT,
          "  check:  scripts/re-bootstrap.sh",
          "Use `scripts/re.sh fn|card|fields|switch|seq|flow|data|gstruct|phy|tables`",
          "(it cd's to the tooling workspace so the relative blob path resolves).",
          "Read `docs/current-context.json` first (generated compact index; verify",
          "with `scripts/generate-current-context.py --check`, regenerate with",
          "`scripts/generate-current-context.py`). Query ONE target with",
          "`scripts/re.sh packet --fn <name|0xADDR>` before broader re queries; it",
          "is bounded, deterministic and machine-readable.",
          "Manual disassembly is allowed only when `re`/`re.db` cannot answer the",
          "question, or as independent verification. Facts already indexed MUST come",
          "from `re`; record any missing fact as a tooling gap so the work is not",
          "repeated. Hardware writes remain governed by AGENTS.md.",
        ].join("\n"),
      )
    },

    "tool.execute.before": async (input: any, output: any) => {
      if (input?.tool !== "bash") return
      const cmd = typeof output?.args?.command === "string" ? output.args.command : ""
      if (!cmd) return
      if (RE_USE_RE.test(cmd)) reOk()
      else if (MANUAL_RE.test(cmd)) {
        manualRe++
        warnIfRegressing()
      }
    },
  }
}) satisfies Plugin

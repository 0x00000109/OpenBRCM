/**
 * OpenBRCM project plugin (single file, project-local).
 *
 * Responsibilities (exactly three):
 *   1. Context hook      - inject a compact project context before every model
 *                          request, sourced from docs/agent-state.md. The text
 *                          is FROZEN per session/cache epoch (stable prefix);
 *                          later state changes are append-only deltas, not
 *                          prefix rewrites.
 *   2. Module preflight  - before `insmod`/`modprobe`/`rmmod`/`make signed`,
 *                          detect obvious workflow mistakes and warn.
 *   3. Write/key safety  - repo writes only under the OpenBRCM tree; never
 *                          touch the MOK signing material.
 *
 * Design constraints: no network, no auto-git, no auto-sudo, no hardware
 * writes, no background processes. Fail open for ordinary source work; block
 * only on concrete safety/integrity invariants. Logs every block/warning.
 *
 * Audited against OpenCode 1.18.32 / @opencode-ai/plugin 1.18.30.
 * Hooks used: "experimental.chat.system.transform", "tool.execute.before".
 */

import { existsSync, readFileSync, statSync, readdirSync } from "node:fs"
import { spawnSync } from "node:child_process"
import { join, resolve, sep } from "node:path"

/* ------------------------------------------------------------------ config */

const REPO = "/media/kartashoff/Storage/opensource/driver/OpenBRCM"
const MOC = "/media/kartashoff/Storage/opensource/iced/MOC"
// Canonical RE tooling workspace (Rust `re`, re.db pipeline). Narrowly added
// for the "RE TOOLING D4 ACCELERATION" milestone so the agent can extend and
// test the canonical tool. MOC lives one level up (iced/MOC) and stays blocked.
const TOOLING = "/media/kartashoff/Storage/opensource/iced/test"
const STATE_FILE = join(REPO, "docs/agent-state.md")

// Writes are permitted only under these roots (scratch under /tmp allowed).
const ALLOWED_WRITE_ROOTS = [REPO, TOOLING, "/tmp"]

const KO_NAME = "openbrcm.ko"
const EXPECTED_SIGNER = "Broadcom Driver MOK"
const EXPECTED_ALIAS_FRAGMENT = "rev2A"
const MAX_CONTEXT_CHARS = 8000

/* ----------------------------------------------------------------- helpers */

function log(message: string): void {
  console.error("[openbrcm-guard] " + message)
}

function resolvePath(p: string, base: string): string {
  if (!p) return ""
  return resolve(p.startsWith("/") ? p : join(base, p))
}

function under(p: string, root: string): boolean {
  const a = resolve(p)
  const b = resolve(root)
  return a === b || a.startsWith(b + sep)
}

type RunResult = { ok: boolean; out: string; err: string }

function run(cmd: string, args: string[]): RunResult {
  const r = spawnSync(cmd, args, { encoding: "utf8", timeout: 5000 })
  return {
    ok: r.status === 0 && !r.error,
    out: (r.stdout || "").trim(),
    err: r.stderr || r.error?.message || "",
  }
}

function shq(s: string): string {
  return "'" + s.replace(/'/g, "'\\''") + "'"
}

function warnPrefix(warnings: string[]): string {
  return (
    warnings.map((w) => "echo " + shq("[openbrcm-guard] " + w)).join(" ; ") + " ; "
  )
}

/* ------------------------------------------------------- 1. context hook */

let stateCache = { mtimeMs: -1, text: "" }

function stateText(): string {
  try {
    const st = statSync(STATE_FILE)
    if (st.mtimeMs !== stateCache.mtimeMs) {
      stateCache = {
        mtimeMs: st.mtimeMs,
        text: readFileSync(STATE_FILE, "utf8").trim(),
      }
    }
    return stateCache.text
  } catch {
    return ""
  }
}

const CONTEXT_HEADER =
  "## Injected OpenBRCM project context (docs/agent-state.md)\n" +
  "This is project-owned state, not conversation history. Treat the " +
  "provenance labels literally; do not promote a hypothesis to a fact.\n\n"

/* -------------------------------------------------- 1b. frozen snapshot */
//
// CACHE CONTRACT (STABLE PREFIX / DYNAMIC TAIL): the injected project state is
// read ONCE per session/cache epoch, not re-read on every request. A mid-
// blocker documentation change must NOT rewrite the earliest system bytes;
// current state is carried by append-only STATE DELTA turns instead. A new
// session (new sessionID) re-reads and freezes a fresh snapshot. Keyed by
// sessionID, so concurrent sessions do not clobber each other.
const frozenState = new Map<string, string>()

function frozenStateText(sessionID: string | undefined): string {
  const key = sessionID || "default"
  const hit = frozenState.get(key)
  if (hit !== undefined && hit !== "") return hit
  const text = stateText()
  if (text) frozenState.set(key, text)
  return text
}


/* ---------------------------------------------------- 3. key-safety rules */

const DESTRUCTIVE = /\b(rm|rmdir|unlink|shred|truncate|mv|chmod|chown|ln|dd|tee)\b/
const PRIV_DUMP =
  /\b(cat|tac|head|tail|xxd|od|hexdump|strings|base64|gpg|openssl|less|more|vi|vim|nano|sed|awk|grep)\b/
const PRIV_WRITE = /(>|>>)\s*\S*MOK\.priv/

function keySafety(cmd: string): void {
  const touchesMoc = cmd.includes(MOC) || /MOK\.(priv|der)\b/.test(cmd)
  if (!touchesMoc) return

  const sanctioned =
    /\bsign-file\b/.test(cmd) ||
    /scripts\/sign\.sh\b/.test(cmd) ||
    /\bmake\b[^\n]*\bsigned\b/.test(cmd)
  if (sanctioned) return // the only allowed consumer of the private key

  if (DESTRUCTIVE.test(cmd)) {
    throw new Error(
      "[openbrcm-guard BLOCK] destructive/management operation on signing " +
        "material (" + MOC + "). MOK files must never be modified, " +
        "regenerated, renamed, moved or deleted.",
    )
  }
  if (PRIV_WRITE.test(cmd)) {
    throw new Error(
      "[openbrcm-guard BLOCK] refusing to write to MOK.priv.",
    )
  }
  if (/MOK\.priv\b/.test(cmd) && PRIV_DUMP.test(cmd)) {
    throw new Error(
      "[openbrcm-guard BLOCK] refusing to read/print private key material " +
        "(MOK.priv). Reading MOK.der metadata and signing via sign-file are " +
        "allowed.",
    )
  }
}

/* ---------------------------------------------- 3. write-scope enforcement */

function checkWrite(p: string, base: string): void {
  const abs = resolvePath(p, base)
  if (under(abs, MOC)) {
    throw new Error(
      "[openbrcm-guard BLOCK] refusing to write into signing material (" +
        abs + ").",
    )
  }
  if (ALLOWED_WRITE_ROOTS.some((r) => under(abs, r))) return
  throw new Error(
    "[openbrcm-guard BLOCK] write outside the allowed scope: " + abs +
      " (allowed roots: " + ALLOWED_WRITE_ROOTS.join(", ") + "). " +
      "Reads outside the repo are allowed; writes are not.",
  )
}

/* --------------------------------------------- 2. module preflight checks */

function newestSourceFile(): { mtimeMs: number; name: string } {
  let best = { mtimeMs: 0, name: "" }
  for (const dir of [join(REPO, "src"), REPO]) {
    let entries: string[]
    try {
      entries = readdirSync(dir)
    } catch {
      continue
    }
    for (const f of entries) {
      if (!/\.(c|h)$/.test(f) && f !== "Makefile" && f !== "Kconfig") continue
      try {
        const st = statSync(join(dir, f))
        if (st.mtimeMs > best.mtimeMs) best = { mtimeMs: st.mtimeMs, name: join(dir, f) }
      } catch {
        /* ignore */
      }
    }
  }
  return best
}

function modulePreflight(cmd: string, base: string): string[] {
  const warnings: string[] = []

  if (!under(base, REPO)) {
    warnings.push(
      "working directory (" + base + ") is not inside the OpenBRCM repository",
    )
  }

  const m = cmd.match(/((?:\/|\.{1,2}\/)?[^\s"';|&]*openbrcm\.ko)/)
  const koPath = m ? resolvePath(m[1], base) : join(base, KO_NAME)

  if (!existsSync(koPath)) {
    warnings.push("module not found: " + koPath + " (run make / make signed first)")
  } else {
    const koMtime = statSync(koPath).mtimeMs
    const newest = newestSourceFile()
    if (newest.mtimeMs > koMtime + 1000) {
      warnings.push("module may be stale: " + newest.name + " is newer than " + KO_NAME)
    }

    const signer = run("modinfo", ["-F", "signer", koPath])
    if (!signer.ok) {
      warnings.push("cannot verify signature (modinfo failed)")
    } else if (!signer.out) {
      warnings.push("module is not signed (expected signer '" + EXPECTED_SIGNER + "')")
    } else if (signer.out !== EXPECTED_SIGNER) {
      warnings.push(
        "unexpected signer '" + signer.out + "' (expected '" + EXPECTED_SIGNER + "')",
      )
    }

    const alias = run("modinfo", ["-F", "alias", koPath])
    if (alias.ok && !alias.out.includes(EXPECTED_ALIAS_FRAGMENT)) {
      warnings.push("BCMA alias " + EXPECTED_ALIAS_FRAGMENT + " not found for " + KO_NAME)
    }

    const dep = run("modinfo", ["-F", "depends", koPath])
    if (dep.ok) {
      if (!dep.out) {
        warnings.push("module declares no dependencies (expected bcma, mac80211)")
      } else {
        // cfg80211 is normally transitive via mac80211, so only require the
        // direct backplane/mac80211 dependencies.
        for (const need of ["bcma", "mac80211"]) {
          if (!dep.out.includes(need)) warnings.push("depends= does not list " + need)
        }
      }
    }
  }

  const ls = run("lsmod", [])
  if (ls.ok) {
    const loaded = new Set(ls.out.split("\n").map((l) => l.split(/\s+/)[0]))
    for (const bad of ["brcm_native", "wl", "brcmfmac", "b43", "bcma_private"]) {
      if (loaded.has(bad)) warnings.push("conflicting BCM4352 driver loaded: " + bad)
    }
    for (const need of ["mac80211", "cfg80211", "bcma"]) {
      if (!loaded.has(need) && !existsSync("/sys/module/" + need)) {
        warnings.push(need + " not currently loaded (may be built-in)")
      }
    }
  }

  return warnings
}

function signedPreflight(base: string): string[] {
  const warnings: string[] = []
  if (!under(base, REPO)) warnings.push("run `make signed` from the OpenBRCM repository")
  if (!existsSync(join(MOC, "MOK.priv")) || !existsSync(join(MOC, "MOK.der"))) {
    warnings.push("signing material missing under " + MOC)
  }
  if (!existsSync(join(REPO, "scripts/sign.sh"))) {
    warnings.push("scripts/sign.sh not found")
  }
  return warnings
}

function modprobeWarnings(cmd: string): string[] {
  if (!/\bmodprobe\b/.test(cmd)) return []
  const toks = cmd.trim().split(/\s+/)
  const i = toks.findIndex((t) => t === "modprobe" || t.endsWith("/modprobe"))
  if (i < 0) return []
  const after = toks.slice(i + 1)
  if (after.some((t) => t === "-a" || t === "--all")) return []
  const mods = after.filter(
    (t) =>
      !t.startsWith("-") &&
      /^[A-Za-z0-9_\-]+$/.test(t) &&
      !t.includes("=") &&
      t !== "&&" &&
      t !== ";" &&
      t !== "|",
  )
  if (mods.length >= 2) {
    return [
      "modprobe takes one module per invocation; '" + mods.join(" ") +
        "' would be parsed as module parameters. Use `modprobe -a " +
        mods.join(" ") + "` or separate invocations.",
    ]
  }
  return []
}

function guardBash(cmd: string, base: string): string {
  keySafety(cmd) // throws on hard key-safety violations

  let warnings: string[] = []
  if (/\binsmod\b/.test(cmd) && /openbrcm\.ko/.test(cmd)) {
    warnings = modulePreflight(cmd, base)
  } else if (/\bmake\b[^\n]*\bsigned\b/.test(cmd)) {
    warnings = signedPreflight(base)
  } else if (/\bmodprobe\b/.test(cmd)) {
    warnings = modprobeWarnings(cmd)
  } else if (/\b(rmmod|modprobe\s+-r)\b/.test(cmd) && /openbrcm/.test(cmd)) {
    if (!existsSync("/sys/module/openbrcm")) {
      warnings = ["openbrcm is not currently loaded (nothing to remove)"]
    }
  }

  if (warnings.length === 0) return cmd

  for (const w of warnings) log("warning: " + w)
  return warnPrefix(warnings) + cmd
}

/* --------------------------------------------------------------- plugin */

export default async function openbrcmGuard(ctx: any) {
  const base: string = ctx?.worktree || ctx?.directory || process.cwd()

  return {
    "experimental.chat.system.transform": async (input: any, output: any) => {
      const text = frozenStateText(input?.sessionID)
      if (!text) return
      if (Array.isArray(output?.system)) {
        output.system.push(CONTEXT_HEADER + text.slice(0, MAX_CONTEXT_CHARS))
      }
    },

    "tool.execute.before": async (input: any, output: any) => {
      const args = output?.args ?? {}
      const tool: string = input?.tool

      if (tool === "bash" && typeof args.command === "string") {
        args.command = guardBash(args.command, base)
        return
      }

      if (
        tool === "edit" ||
        tool === "write" ||
        tool === "patch" ||
        tool === "multiedit"
      ) {
        const p = args.filePath || args.path || args.file
        if (typeof p === "string" && p) checkWrite(p, base)
      }
    },
  }
}

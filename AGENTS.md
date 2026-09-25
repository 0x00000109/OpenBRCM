# AGENTS.md — OpenBRCM agent operating rules

This file is **binding** for any agent (human or automated) working in this
repository. The repository contents and Git history are the single source of
truth. Conversation memory is **not** authoritative, especially after a reboot
or context loss.

## 0. Startup procedure (mandatory after reboot / new session)

Before modifying anything:

1. `pwd` — must be this repository.
2. `git status --short --branch`; `git rev-parse HEAD`; `git log --oneline -15`.
3. Read `AGENTS.md` (this file).
4. Verify and read the compact current-state index
   `docs/current-context.json` (generated; see §10). Verify freshness with
   `scripts/generate-current-context.py --check`; if stale, regenerate with
   `scripts/generate-current-context.py`. This is the **first** state read:
   it names the active milestone, active blocker, proven facts, superseded
   claims, the current call path, tooling paths and the exact evidence files.
5. Consult `docs/artifact-ledger.md` (the persistence index — consult it before
   re-deriving any fact), then `docs/agent-state.md`, `docs/milestones.md` and
   `docs/re-tooling.md` **only as needed for the active blocker** and for the
   reconciliation required by §2. Do **not** recursively read the whole `docs/`
   tree or historical milestone files at startup (§10).
6. Identify the **active blocker** (current-context) and the exact **STOP
   boundary** (§9 one-blocker discipline).
7. Check hook activation:
   `git config --local --get core.hooksPath`; `ls -l .githooks/`.
8. Run the lightweight read-only RE-tooling bootstrap: `scripts/re-bootstrap.sh`.
   It must PASS; do not begin analysis if it FAILs (fix the index, not the
   symptom).
9. Load only the blocker-specific evidence referenced by current-context, and
   query the index with `re packet` first (see `docs/re-tooling.md` §4).
10. Confirm no uncommitted work is about to be overwritten or discarded.

Only then may code be changed. The OpenCode project integration
(`.opencode/opencode.json`, `.opencode/plugins/`, `.opencode/skills/openbrcm-re`)
surfaces these paths automatically; do not rely on conversation memory.

## 1. Non-negotiable safety rules

- Never invent register writes. Every hardware write needs provenance from the
  recovered vendor blob or another explicitly accepted source.
- Bounded polling only; never introduce an unbounded hardware wait.
- Never run `insmod` / `rmmod` / `modprobe` or any hardware test without
  explicit human approval in the current task.
- **`fw_dryrun` is a logging/dry-run parser option only; it does not isolate
  hardware bring-up.** A prior test that treated `fw_dryrun=1` as isolation ran
  the full normal bring-up and caused a long hang followed by a **hard system
  reset**. Isolation must be an explicit early-return mode (e.g.
  `fw_validate_only`, `ucode_test_only`).
- Do not add proprietary firmware or blob artifacts to Git.

## 2. Documentation governance (mandatory)

Any milestone-changing work requires a documentation reconciliation **before**
the task is declared complete. This covers:

- code changes
- new reverse-engineering findings
- changed register semantics
- hardware tests (pass **and** fail)
- corrected assumptions
- milestone transitions
- firmware provenance
- safety boundaries
- the next-step / STOP boundary

The reconciliation updates `docs/milestones.md` and/or `docs/agent-state.md`
using the status vocabulary below. A change is not "done" until the
documentation says which status it reached.

## 3. Status vocabulary (use these exact terms)

| status | meaning |
|---|---|
| `ANALYSIS ONLY` | recovered/documented, no code |
| `IMPLEMENTED` | code exists |
| `STATIC TESTED` | builds and/or host/static tests pass |
| `RUNTIME TESTED` | executed on the target kernel |
| `HARDWARE PROVEN` | executed on the target hardware with an observed gate |
| `FAILED` | attempted and did not meet the gate |
| `SUPERSEDED` | replaced by a later result |
| `UNKNOWN` | not yet recovered |

**`IMPLEMENTED` is never equivalent to `HARDWARE PROVEN`.**

## 4. Automated checks

- `scripts/docs-check.sh` — deterministic documentation/constant consistency
  checks (also run by CI and the pre-commit hook).
- `.githooks/pre-commit` — runs the checker and enforces that source/milestone
  changes also move the state documentation. Activate once per clone with
  `git config --local core.hooksPath .githooks`.
- `scripts/re-bootstrap.sh` — read-only verification that the deterministic
  RE tooling (the `re` binary, `re.db`, its blob identity, the gate scripts) and
  the persistent OpenCode integration are present and current.
- Intentional bypass: `OPENBRCM_SKIP_DOCS_CHECK=1 git commit ...`.

## 5. Current state and next action

`docs/agent-state.md` is the authoritative handoff: target hardware, current
milestone, last completed hardware test, last failure/reset, active safety
constraints, current next action and the exact STOP boundary. Always read it
before acting, and update it when the state changes.

## 6. Repository hosting

Branching, commit format, pull-request/review rules, releases and SSH-key
handling are defined in [`docs/github-workflow.md`](docs/github-workflow.md).

## 7. Tool-first reverse engineering (mandatory)

Reverse engineering must use the deterministic tooling documented in
[`docs/re-tooling.md`](docs/re-tooling.md) **before** manual inspection.

**Rule:** before any manual `objdump` / `readelf` / `r2` / `grep` over the
vendor blob, check whether the fact already exists in the index (`re.db`). A
fact already indexed **must** be queried from `re`; manual disassembly alone is
not acceptable evidence when the index can answer the question.

Manual disassembly is permitted only when:

- a) `re`/`re.db` cannot answer the question, or
- b) it is being used as independent verification of an indexed fact.

Before falling back to manual disassembly, use the **tier-2 Ghidra headless**
augmentation (decompiler + reference manager + CFG-aware value flow) via
`scripts/ghidra_headless.sh` and the reusable `scripts/ghidra/*.java`; it is the
sanctioned tool for indirect/vtable targets, interprocedural constant flow,
struct-field aliasing and loop/engine-array base resolution that `re` leaves
PARTIAL/CONDITIONAL/UNRESOLVED (`docs/re-tooling.md` §1.1). Ghidra under-
segments this ET_REL blob, so cross-check its *negative* results against `re`.

When a useful fact is missing from `re`, record it as a **tooling gap** (see
`docs/re-tooling.md` §9) so the same manual work is not repeated indefinitely.
The minimum workflow is:

```
re.db -> re query -> compact evidence packet -> reasoning -> targeted manual check
```

Query the index with the repository wrapper, e.g.
`scripts/re.sh fn|card|fields|switch|seq|flow|data|gstruct|phy|tables <fn>`
(it cd's to the tooling workspace so the relative vendor-blob path resolves).
The OpenCode project integration (`docs/re-tooling.md`, the `openbrcm-re`
skill, and the project plugin) surfaces this automatically; it is still the
agent's responsibility to follow it. `scripts/re-bootstrap.sh` must PASS before
analysis begins.

## 8. PROVEN FACT IMMUTABILITY (mandatory)

If a fact is marked `PROVEN` in `docs/current-context.json` **and** its
referenced canonical evidence:

- exists, and
- is reachable from the current repository state, and
- matches the relevant binary identity (`binary_identity.blob_sha256`), and
- has not been superseded (`docs/artifact-ledger.json`),

then the agent **MUST NOT by default**:

- re-derive it,
- re-disassemble it,
- rerun broad searches for it,
- rewrite the proof, or
- restate the full proof in chat.

Instead: cite/reference the canonical artifact, consume the compact fact, and
continue from it.

Re-validation is allowed **only** if one of these applies:

- contradictory evidence appears;
- the binary/blob identity changes;
- a relevant tooling bug is discovered (`docs/re-tooling.md` §9);
- the source artifact is missing or stale;
- the fact is explicitly marked `revalidation_ok` / `NEEDS_REVALIDATION`;
- the current task explicitly requires revalidation.

If a `PROVEN` fact conflicts with new evidence: **do not silently overwrite
it.** Mark the conflict, open a focused blocker, preserve both evidence chains,
and supersede only after proof. `scripts/generate-current-context.py --validate`
mechanically rejects a `PROVEN` fact whose `record` is `SUPERSEDED`.

## 9. ONE BLOCKER = ONE TASK (mandatory)

Default behavior: **one blocker = one task.** A task has exactly one primary
unresolved question.

GOOD: *Resolve `pi+0x16e` value provenance.*
GOOD: *Resolve the indirect target at `0x6923d`.*
GOOD: *Recover initial-chanspec provenance.*

BAD: *Resolve PLL + chanspec + calibration + dev_lost + implement D4.*

During one blocker task the agent MAY inspect dependencies necessary to answer
the blocker, but must not expand the task into unrelated blockers. New
unrelated discoveries are **recorded**, assigned blocker IDs, added to
`docs/state/current-state.json` (open blockers), and **deferred** — unless they
invalidate the current task. The active blocker lives in
`current-context.json → active_blocker` and must also appear in
`open_blockers`.

## 10. Token-efficient startup and the current-context index

`docs/current-context.json` is a **generated compact view/cache/index**, not a
source of truth. It is produced by `scripts/generate-current-context.py` from
the machine-readable state (`docs/state/current-state.json`), the artifact
ledger, and the binary/tooling identity. Authoritative evidence is never moved
into it.

- Regenerate: `scripts/generate-current-context.py`
- Freshness / staleness: `scripts/generate-current-context.py --check`
  (non-zero when `sources_hash` or the binary identity changed).
- Integrity only: `scripts/generate-current-context.py --validate`
  (resolves artifact references, rejects duplicate fact IDs, rejects a
  `PROVEN` fact pointing at a `SUPERSEDED` record, requires the milestone to
  exist, requires a supported `re.db` schema, enforces the size bound).
- Local link scan: `scripts/generate-current-context.py --scan-links <files>`

Do **not** silently regenerate during unrelated builds. Update
`docs/state/current-state.json` only when the milestone/blocker state changes,
together with `docs/agent-state.md` / `docs/milestones.md`.

At startup, do **not**: recursively read the whole `docs/` tree; recursively
scan historical milestone files; or load the entire artifact-ledger history
into context. Use current-context plus the references it names.

## 11. Token-efficient final output policy

A normal task final response is **DELTA ONLY**:

```
TASK:
RESULT:
NEW FACTS:
CHANGED CONCLUSIONS:
BLOCKER CLOSED?:
NEW BLOCKERS:
ARTIFACTS:
COMMIT:
GO/NO-GO:
```

Do not reproduce unchanged milestone history, huge call graphs, full JSON,
long disassembly, or entire proof chains — those belong in tracked artifacts.
Target ≤ 80 lines unless the user explicitly requests a full audit/report.

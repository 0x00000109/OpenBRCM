# AGENTS.md — OpenBRCM agent operating rules

This file is **binding** for any agent (human or automated) working in this
repository. The repository contents and Git history are the single source of
truth. Conversation memory is **not** authoritative, especially after a reboot
or context loss.

## 0. Startup procedure (mandatory after reboot / new session)

Before modifying anything:

1. `pwd` — must be this repository.
2. `git status --short --branch`; `git rev-parse HEAD`; `git log --oneline -15`.
3. Read `AGENTS.md` (this file), then `docs/agent-state.md`, then
   `docs/milestones.md`.
4. Identify the **active milestone** and its exact **STOP boundary**.
5. Check hook activation:
   `git config --local --get core.hooksPath`; `ls -l .githooks/`.
6. Confirm no uncommitted work is about to be overwritten or discarded.

Only then may code be changed.

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
- Intentional bypass: `OPENBRCM_SKIP_DOCS_CHECK=1 git commit ...`.

## 5. Current state and next action

`docs/agent-state.md` is the authoritative handoff: target hardware, current
milestone, last completed hardware test, last failure/reset, active safety
constraints, current next action and the exact STOP boundary. Always read it
before acting, and update it when the state changes.

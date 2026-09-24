# GitHub workflow

Repository: `https://github.com/0x00000109/OpenBRCM` (canonical remote).
This document defines how changes are branched, committed, reviewed, uploaded
and released. It complements [`../AGENTS.md`](../AGENTS.md) (agent rules and
documentation governance) and [`provenance.md`](provenance.md) (clean-room).

## 1. Roles and access
- **Owner:** `@0x00000109`.
- The agent authors/commits with the repository-local identity
  (`user.name`/`user.email`) and pushes over SSH as the owner.
- Dedicated SSH key: `~/.ssh/openbrcm_github_ed25519` (public key registered on
  the owner account). The private key is stored **outside the repository**,
  mode `0600`, and must never be committed or printed.

## 2. Remotes
- `origin` = `git@github.com:0x00000109/OpenBRCM.git` (SSH).
- `core.sshCommand` is set repo-locally to select the dedicated key.
- No additional remotes without owner approval.

## 3. Branching model
- `main` is the integration branch and is protected once enabled.
- Short-lived branches only:
  `m<milestone>-<slug>` (e.g. `m34d2a-ucode-upload`), `fix/<slug>`,
  `docs/<slug>`, `ci/<slug>`, `scripts/<slug>`.
- One logical change per branch / pull request.

## 4. Synchronisation rules
- Start from up-to-date `main`:
  `git fetch origin && git switch main && git pull --ff-only`.
- Rebase feature branches onto `main`; do **not** merge `main` into a feature
  branch (keeps linear history).
- **Never force-push `main` or shared branches.** Force-push only your own
  unmerged branch, and only after telling the owner.
- After every rebase re-run `scripts/docs-check.sh` and `make hosttest`.

## 5. Commit rules
- Every commit is DCO-signed: `git commit -s`.
- Subject: `area: imperative summary`, where `area` is the milestone id
  (`m34d2a`), or `src` / `docs` / `tests` / `ci` / `scripts`.
- Body: what and why, evidence, and the status from the `AGENTS.md` vocabulary.
- One logical change per commit; no build artifacts, generated junk, firmware
  or secrets.
- **Documentation reconciliation is required before a milestone-changing
  commit** (`AGENTS.md` §2); the pre-commit hook enforces it.

## 6. Pull request rules
- PR title mirrors the primary commit subject.
- PR body must state: milestone, status reached
  (`STATIC TESTED` / `RUNTIME TESTED` / `HARDWARE PROVEN`), exact commands and
  observed output, the safety boundary, and open risks.
- Required checks before merge: `docs-check`, host tests, kernel-matrix build,
  checkpatch (see CI).
- Hardware-affecting changes require owner review; the agent never runs
  hardware tests autonomously.
- Use **draft** PRs while a milestone is not yet hardware-proven.
- Merge with rebase (linear) unless history is noisy, then squash.

## 7. Review / comment rules
- The agent reports, for every upload: what changed, why, provenance, test
  evidence, what is **not** proven, and the STOP boundary.
- The owner reviews; the agent addresses comments with new commits (no silent
  force-push, no editing someone else's commits).
- Approval and hardware-run approval are explicit and separate.

## 8. Tags and releases
- Annotated tags `vMAJOR.MINOR.PATCH` after owner approval.
- Release notes list **HARDWARE PROVEN** milestones, `SUPERSEDED` items and
  known limitations.
- Never move or delete a published tag.

## 9. Safety and secrets
- Never commit SSH private keys, MOK/MOC signing material, firmware blobs,
  tokens or credentials. `scripts/docs-check.sh` rejects tracked firmware.
- Keep the private key in `~/.ssh`, mode `0600`; rotate and remove it from
  GitHub when no longer needed.
- Once branch protection is enabled, agents push via PR only — no direct pushes
  to `main`.

## 10. CI
- `.github/workflows/build.yml`: host tests, kernel-matrix build, checkpatch.
- Recommended: add a `scripts/docs-check.sh` step (fast, deterministic).
- CI must be green before merge.

## 11. Bootstrap plan (step by step)
1. **Phase 0 — local (agent):** dedicated SSH key, repo-local identity,
   `origin`, workflow docs. *(done)*
2. **Phase 1 — owner:** add the public key to GitHub; create the empty
   `0x00000109/OpenBRCM` repository.
3. **Phase 2 — agent:** verify SSH auth; push `main`; verify remote state.
4. **Phase 3 — owner:** enable `main` branch protection and secret scanning.
5. **Phase 4 — ongoing:** milestone loop — branch → commit(s) → PR → review →
   merge → tag; keep `docs/agent-state.md` current.
6. **Phase 5 — safety audit:** periodic re-check of hooks, secrets, provenance.

## 12. Rollback policy
- Undo published history with `git revert` on `main`; never rewrite published
  `main`.
- Tags are immutable; supersede with a new tag.

# AGENTS.md — OpenBRCM accelerated agent operating rules

This file is **binding** for every human or automated agent working in this
repository.

Repository state, tracked evidence, pinned historical binaries and captured
hardware artifacts are authoritative.

Conversation memory is useful context but is **never** authoritative evidence.

The project is time-constrained. Optimize for:

1. correctness at hardware-mutating boundaries;
2. maximum useful information per RE / hardware cycle;
3. reuse of already proven facts;
4. parallel read-only investigation;
5. minimum administrative overhead.

The goal is not to maximize the number of reports. The goal is to move the
driver toward working hardware efficiently without inventing silicon behavior.

---

# 0. Agent roles and authority

There is one **PRIMARY** agent for each engineering task.

The PRIMARY agent:

- owns the task objective;
- owns final synthesis;
- decides whether evidence is sufficient;
- controls write authority;
- is the only agent allowed to authorize progression inside the task;
- is the only agent that may execute an explicitly authorized hardware-mutating
  action.

Subagents are normally **READ-ONLY investigators**.

Recommended roles:

- `binary_re` — historical binary control-flow / hardware semantics;
- `dataflow` — fields, writers, aliases, object lifetime, runtime predicates;
- `evidence` — captured logs, reports, hardware artifacts and existing values;
- `verifier` — independent adversarial verification;
- `writer` — implementation after the behavior has been synthesized.

Not every task needs every role.

Do not spawn agents merely to repeat identical work.

---

# 1. Primary-session startup

Before modifying anything, the PRIMARY agent must:

1. Confirm repository:
   `pwd`

2. Inspect repository state:
   `git status --short --branch`
   `git rev-parse HEAD`
   `git log --oneline -15`

3. Read this `AGENTS.md`.

4. Verify compact current state:

   `scripts/generate-current-context.py --check`

   If this fails on a known canonical vendor/re.db identity mismatch, preserve
   the fail-closed result; do not regenerate through the default path. When the
   task explicitly admits a verified stable image manifest, check the same
   context with:

   ```sh
   RE_IMAGE_MANIFEST=/path/to/accepted-manifest.json \
     scripts/generate-current-context.py --check
   ```

   Check research-registry availability separately with
   `scripts/research-index.py preflight`; a
   usable research index does not imply historical-image admission.

   Read:

   `docs/current-context.json`

   If stale, diagnose the source conflict first. Regenerate only when the
   authoritative source inputs validate; never change pinned binary identities
   or weaken admission merely to make the generated view pass.

   For research-dependent work, run the registry preflight below before RE or
   implementation. Do not load the full project tree into context.

   If authoritative state changed and validates, regenerate explicitly:

   `scripts/generate-current-context.py`

5. Read only the evidence referenced by the active current state.

6. Consult, as needed:

   - `docs/artifact-ledger.md`
   - `docs/agent-state.md`
   - `docs/milestones.md`
   - `docs/re-tooling.md`

   Do not recursively load the entire `docs/` tree.

7. Check local hooks:

   `git config --local --get core.hooksPath`

8. Run:

   `scripts/re-bootstrap.sh`

   It must PASS before historical RE.

9. Identify:

   - current milestone;
   - current execution path;
   - accepted PROVEN facts;
   - current meaningful engineering objective;
   - hardware STOP boundary.

10. Confirm no existing work will be overwritten.

Only then may source be changed.

---

# 2. Lightweight subagent startup

Read-only subagents must not repeat the entire primary startup.

The PRIMARY should give each subagent:

- repository root;
- task/objective;
- relevant frozen current-context facts;
- exact evidence files;
- pinned binary identities;
- requested output.

A subagent must at minimum:

1. confirm repository path;
2. read `AGENTS.md`;
3. read only its assigned evidence;
4. verify pinned binaries if its conclusion depends on them.

If the PRIMARY already recorded a successful `re-bootstrap.sh` in the same
unchanged worktree/session, subagents may consume that result instead of each
running another bootstrap.

Subagents must not regenerate project state or rewrite handoff documentation.

---

# 3. Non-negotiable hardware safety

Never invent a hardware write.

Every execution-affecting hardware operation needs provenance from:

- pinned historical machine instructions; or
- another explicitly accepted evidence source.

Rules:

- bounded polling only;
- no unbounded hardware wait;
- no speculative recovery sequence;
- no speculative reset;
- no retry unless historical behavior or the authorized test contract requires
  it;
- no proprietary firmware/blob artifacts added to Git.

`fw_dryrun` is only a parser/logging option and is **not hardware isolation**.

Isolation requires an explicit test-only early-return path.

## Hardware mutation authority

Subagents must NEVER independently execute:

- `insmod`;
- `rmmod`;
- target `modprobe`;
- PCI reset/remove/rescan;
- BCMA bind/unbind;
- live PHY/radio/OTP/MMIO experiments;
- runtime hardware tests;
- reboot.

A hardware-mutating task requires explicit authorization in the current task.

Only the PRIMARY agent executes it.

Unless explicitly authorized otherwise:

- maximum one insertion/run;
- no retry;
- no second insertion;
- no recovery run;
- preserve the terminal hardware/module state.

If current state already satisfies a clean-state admission, **do not reboot
merely because an older procedure used a reboot**.

Reboot is a state-transition tool, not an administrative ritual.

---

# 4. Multi-agent workflow

Use subagents aggressively for independent READ-ONLY work when the task is
substantial.

Good parallel decomposition:

- historical path in `wlc_hybrid.o_shipped`;
- independent path/cross-check in `wl.ko`;
- field/writer/dataflow provenance;
- search of captured hardware evidence;
- independent adversarial verification.

Typical pattern:

```text
binary RE A ─┐
binary RE B ─┤
dataflow ────┼─> PRIMARY synthesis ─> verifier
evidence ────┘
```

Run independent read-only work in parallel.

Do not serialize work that has no dependency.

## Independent verification

The verifier must not merely summarize the PRIMARY result.

It should actively try to falsify execution-critical conclusions by searching
for:

- earlier hardware access;
- alternate branch;
- hidden writer;
- alias;
- alternate call target;
- different width;
- different mask/value;
- different behavior in the second historical binary;
- unaccounted runtime predicate;
- hidden retry/poll/delay;
- mismatched hardware evidence.

Disagreement is useful.

If PRIMARY and verifier disagree on an execution-critical point, implementation
or hardware progression is blocked until resolved.

---

# 5. Write ownership

Default rule:

**one writer per coherent source change.**

Read-only subagents must not modify the worktree.

The PRIMARY synthesizes historical behavior before assigning implementation.

The writer receives:

- accepted behavior;
- exact implementation boundary;
- explicit non-goals;
- required tests;
- STOP semantics.

## Parallel writers

Parallel source writers are allowed only when the PRIMARY explicitly assigns:

- separate worktrees or branches;
- non-overlapping file/interface ownership;
- clearly independent changes.

Hardware-sensitive register sequencing must not be implemented concurrently by
multiple writers.

Merge only after independent tests pass.

---

# 6. Task sizing: one coherent objective, not one instruction

The old rule "one blocker = one task" is replaced by:

**ONE COHERENT ENGINEERING OBJECTIVE = ONE TASK.**

A task may include multiple tightly coupled unknowns when they belong to the
same:

- control-flow region;
- object/dataflow chain;
- hardware initialization block;
- telemetry opportunity;
- hardware run.

Do NOT stop merely because another conditional branch appears.

Resolve every branch whose input is available from:

- constants;
- target identity;
- PROVEN historical facts;
- already captured hardware values;
- recoverable software state.

Continue RE until reaching a meaningful boundary:

1. a genuinely unresolved runtime dependency;
2. a distinct coherent hardware block;
3. a calibration/polling engine requiring separate treatment;
4. a different hardware subsystem;
5. function return.

GOOD:

> Recover the selected continuation through PMU, GPIO and HIRSSI until the next
> genuine unresolved hardware dependency.

BAD:

> Recover one `jcc`, stop, write a report, start another task.

---

# 7. Batch unknowns aggressively

When several runtime values can be observed safely in one execution, batch them.

Prefer:

```text
one telemetry implementation
→ one freeze/build/sign
→ one hardware run
→ multiple branches resolved
```

over:

```text
one field
→ build/sign/run
→ next field
→ build/sign/run
```

Software-only telemetry may batch:

- ordinary structure fields;
- cached values;
- object state;
- already computed predicates;
- counters;
- branch inputs.

Telemetry must not introduce extra MMIO merely for convenience unless the task
explicitly authorizes that acquisition.

Log enough nearby software-only state to avoid an obvious second hardware run.

---

# 8. Reverse engineering: tool first

Historical RE uses the deterministic tooling described in:

`docs/re-tooling.md`

Future substantial RE or verification work must also consult
[`docs/openbrcm-investigation-playbook.md`](docs/openbrcm-investigation-playbook.md)
for the cheapest-first evidence ladder, investigation workflows, and
escalation rules.

Start with the index.

Before broad manual binary inspection query:

`scripts/re.sh`

using the appropriate:

`fn`
`card`
`fields`
`switch`
`seq`
`flow`
`data`
`gstruct`
`phy`
`tables`
`packet`

If `re.db` answers the question, consume it first.

Manual disassembly is used when:

- the index cannot answer;
- the indexed result is partial/conditional;
- or independent verification is required.

For complex unresolved dataflow use tier-2 Ghidra headless:

`scripts/ghidra_headless.sh`

Direct machine instructions and relocations control.

Decompiler output is supporting evidence only.

Because Ghidra may under-segment the historical ET_REL image, negative Ghidra
results must not override direct binary evidence.

When repeated manual work exposes a missing reusable capability, record a
tooling gap.

---

# 9. Evidence classes

Use these labels for execution-critical claims:

- `FACT_BIN` — direct historical binary evidence;
- `FACT_HW` — captured target hardware observation;
- `FACT_SRC` — current OpenBRCM source behavior;
- `INFERENCE` — conclusion derived from facts;
- `UNKNOWN` — not proven.

Never use current OpenBRCM source as proof of proprietary historical semantics.

Never transfer a value from another board/device and call it target evidence.

---

# 10. PROVEN fact immutability

If a fact is marked `PROVEN` in `docs/current-context.json` and its canonical
evidence:

- exists;
- matches the pinned binary/source identity;
- is reachable;
- is not superseded;

then agents should consume it, not re-prove it.

Do NOT by default:

- re-disassemble it;
- rerun broad searches;
- rewrite the proof;
- reproduce the entire evidence chain.

Revalidation is warranted only when:

- contradictory evidence appears;
- binary identity changes;
- tooling is discovered to be wrong;
- canonical evidence is missing/stale;
- the fact is marked for revalidation;
- or the current task specifically depends on validating it again.

If new evidence conflicts with a PROVEN fact:

1. preserve both evidence chains;
2. mark the conflict;
3. resolve it explicitly;
4. supersede only after proof.

Never silently rewrite history.

---

# 11. Hardware checkpoint granularity

A new hardware checkpoint should normally represent a **coherent historical
block**, not a single trivial register access.

Before implementation, recover enough to know:

- entry predicate;
- access domain;
- address/selector;
- width;
- value;
- mask;
- RMW formula;
- ordering;
- delays;
- polls;
- failure semantics;
- next STOP boundary.

Adjacent operations that clearly belong to one historical sequence should
normally be recovered and tested together.

Do not cross an unresolved execution-affecting dependency.

---

# 12. Development pipeline

Default accelerated pipeline:

```text
substantial offline RE
→ batched runtime-state observation if needed
→ implementation + offline tests
→ freeze/build/sign
→ short hardware admission
→ one bounded hardware run
→ substantial next RE block
```

Do NOT create separate administrative tasks for:

- signing preparation;
- signing verification;
- source-freeze verification;
- postboot admission;

when those checks can safely be combined into one deterministic release/hardware
boundary.

Keep hardware execution itself separately bounded.

---

# 13. Stagnation rule

Do not spend repeated tasks applying the same method to the same unknown.

If an execution-critical unknown survives:

- deterministic index search;
- targeted direct disassembly;
- independent second-binary analysis;
- dataflow/xref investigation;
- existing-evidence search;

then explicitly choose one of:

1. broader historical analysis;
2. another evidence source;
3. batched software telemetry;
4. bounded hardware observation.

Do not create another report that merely restates the same UNKNOWN.

---

# 14. Documentation governance

Documentation exists to preserve engineering knowledge, not to slow execution.

Create/update a durable evidence packet when there is a meaningful change in:

- recovered execution semantics;
- implementation;
- hardware result;
- accepted/superseded assumption;
- milestone;
- safety boundary;
- next meaningful blocker.

Prefer **one packet per coherent engineering block**.

Do not create a new packet solely to repeat unchanged identities or revalidate
administrative facts unless required by a release/hardware gate.

At task completion reconcile as appropriate:

- `docs/state/current-state.json`
- `docs/agent-state.md`
- `docs/milestones.md`
- artifact ledger
- generated `docs/current-context.json`

A task is not complete when repository state changed but handoff state still
describes the old milestone.

---

# 15. Status vocabulary

Use these terms exactly:

| Status | Meaning |
|---|---|
| `ANALYSIS ONLY` | recovered/documented, no implementation |
| `IMPLEMENTED` | source implementation exists |
| `STATIC TESTED` | build/host/static tests pass |
| `RUNTIME TESTED` | executed on target kernel |
| `HARDWARE PROVEN` | target hardware reached the observed gate |
| `FAILED` | attempted and gate failed |
| `SUPERSEDED` | replaced by later accepted evidence |
| `UNKNOWN` | not yet proven |

`IMPLEMENTED` is never equivalent to `HARDWARE PROVEN`.

A log marker is not automatically proof of physical semantic meaning beyond the
explicit checkpoint contract.

---

# 16. Automated checks

Relevant repository checks include:

- `scripts/docs-check.sh`
- `scripts/static-acceptance.sh`
- `scripts/re-bootstrap.sh`
- host tests
- runtime/parser tests
- target build
- `W=1` build
- `git diff --check`

Run the checks appropriate to the files and behavior changed.

Do not rerun expensive unrelated suites merely because they exist.

Before a hardware candidate is frozen, the relevant complete acceptance suite
must pass.

---

# 17. Current-state index

`docs/current-context.json` is a generated compact index/cache.

It is not primary evidence.

Generated from authoritative state and artifact records.

Commands:

- regenerate:
  `scripts/generate-current-context.py`

- freshness:
  `scripts/generate-current-context.py --check`

- integrity:
  `scripts/generate-current-context.py --validate`

- link scan:
  `scripts/generate-current-context.py --scan-links <files>`

At startup, load current-context and only referenced evidence.

Do not recursively read historical documentation.

---

# 18. Context and prompt efficiency

Keep stable governance stable.

Do not inject rapidly changing values into long-lived global instructions when
they belong in current-context or the task prompt.

Examples of dynamic state:

- HEAD SHA;
- active blocker;
- latest hardware run;
- current signed artifact;
- current GO/NO-GO.

These belong in the task/context tail, not in permanent policy.

Subagents should receive only the context required for their role.

Avoid giving every investigator the entire project history.

This reduces context pollution and makes independent analysis more meaningful.

---

# 19. Hardware-run evidence

For every authorized hardware run preserve:

- run ID;
- exact candidate identity;
- kernel;
- target identity;
- selected test mode;
- raw current-run log;
- relevant dmesg/journal slice;
- terminal state;
- whether module remains loaded;
- insertion count;
- retry/reboot/reset count;
- first failure if any.

A PASS must state exactly what was proven and what was **not** proven.

Do not promote:

`LOCAL_ANALOG_STATE`

into:

`RADIO ON`

or any other stronger semantic state without evidence.

---

# 20. Negative-proof discipline

Negative claims that affect execution require scoped evidence.

Examples:

- no earlier hardware access;
- no alternate writer;
- no second OTP read;
- no retry;
- no post-STOP MMIO.

State the relevant scope:

- function/range;
- xrefs;
- widths;
- binary identities;
- selected call graph.

Do not demand exhaustive negative proof for administrative or low-risk facts.

Focus rigor where an error could change hardware execution.

---

# 21. Final response policy

Normal task output is DELTA ONLY.

Preferred format:

```text
TASK:
RESULT:
NEW FACTS:
CHANGED CONCLUSIONS:
BLOCK CLOSED?:
NEW UNKNOWNS:
ARTIFACTS:
TESTS:
HARDWARE ACTIONS:
GO/NO-GO:
NEXT ACTION:
```

Do not paste:

- unchanged milestone history;
- giant disassemblies;
- complete JSON state;
- full proof chains.

Those belong in tracked evidence packets.

Final chat output should normally be concise.

---

# 22. Default decision principle

When choosing between two workflows, prefer the one that:

1. preserves hardware safety;
2. resolves more execution-affecting unknowns per cycle;
3. reuses PROVEN evidence;
4. avoids duplicate analysis;
5. minimizes rebuild/sign/reboot/hardware cycles;
6. keeps conclusions independently verifiable.

The project should move by **coherent blocks**, not by individual instructions.

The standard question is not:

> "What is the smallest next thing we can prove?"

It is:

> "What is the largest coherent next block we can prove safely with the evidence
> and hardware authority currently available?"

---

# 23. Research session retrieval and completion

For a task that depends on previous OpenBRCM research, after reading this file
and `docs/current-context.json`, run:

```sh
scripts/research-index.py preflight --topic '<subject>'
scripts/research-index.py locate --topic '<subject>'
```

When readable Broadcom or Linux reference source is present, perform a local
source lookup before spending effort on binary reconstruction:

```sh
scripts/research-index.py source build --check
scripts/research-index.py source locate --query '<symbol-or-constant>'
```

Use the returned source definitions for source-version-specific types,
constants, prototypes and algorithms. This lexical catalog is a navigation
aid only: it does not establish compiler ABI or prove that a source revision
produced the pinned vendor object. For binary-specific behavior, verify the
image manifest and query `scripts/re.sh`/RE.DB as required by Section 8.

The source catalog is subordinate generated metadata, not a research registry.
If stale, the PRIMARY may regenerate it with
`scripts/research-index.py source build --write`; read-only agents must hand
back findings without writing the catalog or authoritative ledger.

Use `--chip` or `--bdf` when device identity matters. Open the returned primary
reports and evidence paths selectively, check their current Git state, and
retain `FACT_BIN`, `FACT_HW`, `FACT_SRC`, `INFERENCE`, and `UNKNOWN` distinctions.
The authoritative catalog is the optional `research_entries` field in
`docs/artifact-ledger.json`; `docs/artifact-ledger.md` and
`project_structure.txt` are generated navigation views. Do not create a second
research registry or infer conclusions from filenames.

When a research task materially changes a conclusion, the PRIMARY must save a
permanent report, prepare a validated proposal, review it, and admit it through
the locked update path:

```sh
scripts/research-index.py propose --entries <project-relative-json> --out <project-relative-proposal>
scripts/research-index.py update --proposal <project-relative-proposal>
scripts/research-index.py verify
scripts/generate-artifact-ledger-md.py --check
```

Regenerate derived context/tree views only through their scripts and preserve
fail-closed provenance conflicts. A read-only secondary agent must provide an
importable proposal or handoff and must not update shared ledger/context files.
An update proposal is pinned to the ledger hash; amendments to existing IDs
must also carry the exact previous-entry SHA256. Concurrent or stale writers
must resolve a reported conflict by reviewing and recreating the proposal.

For simple code-only changes with no historical research dependency, this
preflight is not required. A project-local Codex `SessionStart` hook also runs
the read-only registry preflight after that exact hook definition is reviewed
and trusted in Codex (`/hooks`). It is a convenience, may be disabled or
unavailable in some Codex surfaces, and does not replace these explicit task
instructions or the agent's evidence review.

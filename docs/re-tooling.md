# RE tooling (canonical)

This document is the **single source of truth** for the deterministic reverse-
engineering tooling used by OpenBRCM. A new agent session must read it before
doing any reverse engineering. It complements [`../AGENTS.md`](../AGENTS.md)
(operating rules) and [`agent-state.md`](agent-state.md) (current milestone).

The rule is **tool first**: query the persistent index before manually
disassembling anything. See [AGENTS.md §7](../AGENTS.md).

## 1. Canonical paths

| Item | Path |
|---|---|
| Tool workspace ("Repo A") | `/media/kartashoff/Storage/opensource/iced/test` |
| Rust workspace | `/media/kartashoff/Storage/opensource/iced/test/binary_analyzer` |
| Rust binary `re` | `/media/kartashoff/Storage/opensource/iced/test/binary_analyzer/target/release/re` |
| Persistent index | `/media/kartashoff/Storage/opensource/iced/test/re.db` |
| Vendor blob | `/media/kartashoff/Storage/opensource/iced/test/wlc_hybrid.o_shipped` |
| Blob sha256 | `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743` |
| Verification gates | `/media/kartashoff/Storage/opensource/iced/test/scripts/` |
| Cross-OS matcher | `/home/kartashoff/brcm-re/scripts/bcm4352_recover.py` |
| Startup verifier | `scripts/re-bootstrap.sh` (this repo) |
| `re` wrapper (cwd-safe) | `scripts/re.sh` (this repo) |
| Project config | `.opencode/opencode.json` |
| Skill | `.opencode/skills/openbrcm-re/SKILL.md` |

`re.db` is a generated artifact and is intentionally **not** committed to Git
(see the tooling workspace `.gitignore`). Its `meta.sha256` records the sha256
of the vendor blob that was indexed.

### 1.1 Tier-2: Ghidra headless augmentation

`re`/`re.db` is the first and authoritative tier, but a few questions are
statically hard for a single linear pass. The **second tier** is Ghidra
headless (decompiler + reference manager + CFG-aware value flow), used only for
those residual facts: indirect/vtable call targets, interprocedural constant
flow, struct-field aliasing, and loop/engine-array base resolution.

| Item | Path |
|---|---|
| Ghidra root | `/home/kartashoff/projects/ghidra` (12.1.3) |
| headless | `support/analyzeHeadless` |
| wrapper (this repo) | `scripts/ghidra_headless.sh` |
| reusable scripts | `scripts/ghidra/*.java` (`Decompile`, `Refs`, `Vtable`) |
| scratch project | `/tmp/openbrcm_ghidra` (generated; not committed) |

```sh
scripts/ghidra_headless.sh Refs.java wlc_phy_btc_adjust_acphy wlc_phy_init
scripts/ghidra_headless.sh Vtable.java dma64proc
scripts/ghidra_headless.sh Decompile.java wlc_phy_anacore wlc_phy_switch_radio
```

Rules: (a) query `re` first; (b) if a fact stays PARTIAL/CONDITIONAL/UNRESOLVED,
use Ghidra; (c) manual `objdump`/`readelf`/`r2` only if both cannot answer, or to
settle a conflict between them — and record why. Ghidra import under-segments
this ET_REL blob, so treat Ghidra *negative* results with care and cross-check
against `re`. Record any fact that neither tier can express as a tooling gap
(§9). The wrapper is read-only with respect to hardware and Git; it imports the
canonical vendor blob, which is never committed.

## 2. Verify the index matches the blob

The bootstrap script performs this check. Manually:

```sh
BLOB=/media/kartashoff/Storage/opensource/iced/test/wlc_hybrid.o_shipped
DB=/media/kartashoff/Storage/opensource/iced/test/re.db
sha256sum "$BLOB"                                   # must equal meta.sha256
python3 - "$DB" <<'PY'
import sqlite3, sys
c = sqlite3.connect("file:%s?mode=ro" % sys.argv[1], uri=True)
print(c.execute("select value from meta where key='sha256'").fetchone()[0])
PY
```

If they differ, the index is **stale**. Rebuild it (this is the only sanctioned
rebuild command; it does not touch hardware):

```sh
cd /media/kartashoff/Storage/opensource/iced/test
./binary_analyzer/target/release/re db build --elf wlc_hybrid.o_shipped --db re.db
```

Then re-run the gates in §5.

## 3. Expected index shape

From the current `re.db` (`re stats --db re.db`):

| Entity | Count |
|---|---|
| functions | 5095 (2373 discovered) |
| calls | 51193 (internal 44362, external 6816, local-no-reloc 5169) |
| cp_tables | 63 |
| jump tables | 116 |
| indirect calls | 212 |
| globals | 1284 |
| data refs | 6842 |
| strings | 1462 |
| decoded instructions | 574042 |
| reloc-site mismatches | 0 |

Counts are informational; treat a large drop or a non-zero
`reloc-site mismatches` as a failure and investigate before trusting results.
`clusters` is empty until `re clusters --write` has been run; that is expected
and not an error.

## 4. Important `re` commands

`re` reads the vendor blob from the relative path stored in `re.db`
(`meta.elf_path = wlc_hybrid.o_shipped`). Any subcommand that decodes
instructions (`fn`, `card` callers, `switch`, `fields`, `seq`, `data`, …) must
therefore run with cwd = the tooling workspace (`…/iced/test`), where the blob
lives; otherwise it fails with `No such file or directory`.

Use the repository wrapper `scripts/re.sh`, which cd's to the tooling workspace
and execs `re`, so it works from OpenBRCM:

```sh
scripts/re.sh fn 0x67efd
scripts/re.sh card sub_67efd
scripts/re.sh switch 0x67efd
```

When invoking the binary directly (only from the tooling workspace), pass
`--db` explicitly. Command summary:

| Purpose | Command |
|---|---|
| index summary | `re stats --db re.db` |
| function boundary + calls + MMIO | `re fn <name\|0xaddr> --db re.db` |
| callers / callees | `re card <name\|0xaddr> --db re.db` |
| structure-field accesses of a function | `re fields <fn> --db re.db` |
| named registers / globals | `re named --db re.db` |
| branch predicates / switch | `re switch <fn> --db re.db` |
| ordered call flow | `re flow <fn> --db re.db` / `re seq <fn> --db re.db` |
| MMIO / register map by cluster | `re fields --cluster ... --db re.db` |
| PHY / radio / table access | `re phy`, `re tables`, `re ptrtable`, `re gstruct` |
| data / string at a symbol | `re data <global> --db re.db` |
| xrefs to a global | `re refs <global> --db re.db` |
| reachability from an entry | `re reach <fn> --db re.db` |
| per-function value provenance | `re fn <fn> --values --db re.db` |
| stage pipelines (4–8) | `re stage4` … `re stage8 --db re.db` |
| whole-blob MMIO search | `re mmio [--offset 0xOFF] [--fn F] [--access read\|write] --db re.db` |
| whole-blob immediate search | `re imm 0xV --db re.db` |
| struct-field writers | `re field-writers --field 0xOFF [--base-load 0xLOAD] [--arg N] --db re.db` |
| indirect call/branch targets + candidates | `re indirect [fn] [--field 0xOFF] [--installer SUBSTR] [--family SUBSTR] --db re.db` |
| register-program tables (initvals family) | `re table <name\|0xADDR>` · `re regtables [--consumer FN]` |
| constant arguments reaching a callee | `re const --callee FN` |
| normalized PHY/radio ops | `re phyops [fn] [--class PHY\|RADIO\|PHY_TABLE]` |
| per-function JSON evidence packet | `re dump <fn> --json` |
| bounded one-target evidence packet | `re packet --fn <name\|0xADDR> [--callers] [--callees] [--fields] [--mmio] [--phy] [--radio] [--tables] [--branches] [--indirect] [--constants] [--provenance]` |
| regression self-check | `re regress --db re.db` |
| verifier | `re verify --db re.db` |

`re db build` now also populates the v5 tables (`mmio_sites`, `imm_sites`,
`field_sites`, `indirect_targets`, `const_props`, `phy_ops`, `reg_tables`,
`reg_table_ops`) via the in-tree `re_support` module. v4 adds provenance
`confidence` (`mmio_sites`, `field_sites`) and `confidence,candidates`
(`const_props`): relocated immediates are exposed as symbols (never literal
zero) and a linearly-ambiguous value is `CONDITIONAL` with a candidate set,
never `EXACT`. **v5** (tooling commit `f5d03da`, store-vs-read follow-up
`cf9738e`) adds base-aware indirect
resolution — `indirect_targets` carries `base_arg`, `base_load`, `field_offset`,
`installer`, `family`, and candidates come from the object constructor's
relocation-covered function-pointer stores (`field_sites`) at the exact
`(base_arg, base_load, field)` triple, not from field-offset-only
`address_taken`; `EXACT` only when a single constructor candidate matches.
v5 also demotes spurious mid-instruction discovered fragments so a container's
size is not clamped (e.g. `sub_b018f` = 0x2c6, not 0x2d), and classifies
store-vs-read with iced operand access (`cmp` memory reads are no longer
stores). `re regress` must PASS (reproduces the 610/113/497 common-initvals and
73/39/34 bsinitvals shapes, sub_67efd 0x530/0x540, switch_macfreq 0x62e/0x630,
DMA/MAC access facts, the `phy+0xF8` relocation fixture, the `dma_attach`
non-EXACT fixture, the AC `wlc_phy_attach_acphy` pi-fptr table, `sub_b018f`
size, and the `0x16e` store-vs-read fixture).

Machine-readable evidence: append `--json` (e.g. `re fn <fn> --json`,
`re switch <fn> --json`). Use that output in evidence packets.

Examples (from the OpenBRCM repo root, via the wrapper):

```sh
scripts/re.sh card sub_67efd              # callers/callees
scripts/re.sh fn   sub_67efd              # boundary + MMIO + calls
scripts/re.sh switch sub_67efd            # predicates (0x530/0x540)
scripts/re.sh fn   0x67efd --json         # evidence packet
```

## 5. Verification / gate scripts

Run from the tooling workspace. These are independent oracles; all must PASS
before an RE finding is recorded.

| Script | What it proves |
|---|---|
| `scripts/verify_edges.py` | symbol ↔ relocation edges (pyelftools vs goblin) |
| `scripts/verify_decode.py` | capstone vs iced-x86 decode consensus |
| `scripts/verify_tables.py` | code-pointer / table classification |
| `scripts/verify_crossarch.py` | i386 cross-architecture decode check |
| `scripts/oracle_exec.py` | executes real vendor code for arithmetic checks |
| `scripts/verify_repro.sh` | deterministic regeneration of artifacts |
| `scripts/coverage.py` | MVP attach/init → RX/TX coverage matrix |

`re verify` (strict) must also PASS. Residual unresolved edges are documented in
the tooling workspace; they are not a licence to hand-wave a value.

## 6. Cross-binary matching

`/home/kartashoff/brcm-re/scripts/bcm4352_recover.py` is the **only** tool for
cross-image work (Windows `wl7-64.sys` / `wl10-64.sys`, Linux `wl.ko`,
`wlc_hybrid.o_shipped`). It uses normalized instruction-type-sequence hashing.

```sh
cd /home/kartashoff/brcm-re
python3 scripts/bcm4352_recover.py resolve-symbols wlc_hybrid.o_shipped wl.ko --out symbol_recovery.json
python3 scripts/bcm4352_recover.py diff-versions  wl7-64.sys wl10-64.sys --out version_diff.json
python3 scripts/bcm4352_recover.py cross-match    wl.ko wl7-64.sys --out cross_os_equiv.json
```

## 7. Authoritative vs legacy

**Authoritative (use these):**

- `re` + `re.db`
- `scripts/verify_*` and `scripts/oracle_exec.py`
- `analyze_initvals.py` / `analyze_bsinitvals.py` in this repo
- `~/brcm-re/scripts/bcm4352_recover.py`

**Legacy — do NOT use as a source of truth:**

- `binary_analyzer` default binary and its `output/*.json` (pre-relocation)
- `binary_analyzer/analysis.db` (functions only, `calls = 0`)
- `binary_analyzer/call_graph.dot`
- `tools/analyzer` (empty stub)
- `scanner`, `mapper`, `graph`, `contract` binaries

## 8. Evidence-packet convention

For every RE milestone, produce a compact, machine-readable packet:

```
docs/<milestone>/evidence.jsonl    # one JSON object per fact
```

Each record: `{ "claim": ..., "command": "re ... --json", "result": {...},
"gate": "verify_edges PASS" }`. The milestone document cites the packet and the
`re` commands used. Do not paste raw disassembly as the primary evidence.

## 8.1 Current-context index and `re packet` (token efficiency)

`docs/current-context.json` is a **generated compact index**, not a source of
truth. It is produced deterministically by `scripts/generate-current-context.py`
from `docs/state/current-state.json` (the small hand-maintained machine-readable
state), `docs/artifact-ledger.json` and the binary/tooling identity. It names the
current milestone, the active blocker, proven facts, superseded claims, the
relevant call path, tooling paths and the exact evidence files, and stays within
8 KiB. Authoritative evidence is never moved into it.

```sh
scripts/generate-current-context.py            # regenerate (deterministic)
scripts/generate-current-context.py --check    # non-zero if stale
scripts/generate-current-context.py --validate # integrity only
scripts/generate-current-context.py --scan-links <md files>
```

`--check` fails when `integrity.sources_hash` (companion + ledger + blob
identity + re.db schema) or the binary identity changed. `--validate` resolves
every referenced artifact path, rejects duplicate fact IDs, rejects a `PROVEN`
fact whose ledger `record` is `SUPERSEDED`, requires the current milestone in
`agent-state.md`/`milestones.md`, and requires a supported `re.db` schema. A
source conflict fails generation rather than being silently resolved.

`re packet` is an **additive** subcommand that emits one bounded, deterministic,
machine-readable evidence packet for a single function/address, composed only
from the existing re.db tables (plus the same linear branch pass `re switch`
uses). It carries the index confidence/provenance verbatim and never upgrades
it. Every list is wrapped as `{total, shown, truncated, items}` so truncation is
explicit.

```sh
scripts/re.sh packet --fn wlc_phy_switch_radio_acphy                 # all sections
scripts/re.sh packet --fn sub_67efd --mmio --branches --max-sites 20 # selective
scripts/re.sh packet --addr 0x6923d --indirect --max-sites 10        # by address
```

Flags: `--callers --callees --fields --mmio --phy --radio --tables --branches
--indirect --constants --provenance`, bounded by `--max-callers --max-callees
--max-sites --max-branches` (defaults 30/40/40/30). With no section flag every
section is included. It does not replace `fn`/`card`/`dump`; use `packet` for a
compact blocker-scoped question and the older commands for full dumps.

## 8.2 Prompt-cache telemetry (local, out-of-band)

`re packet` keeps RE evidence bounded; the cache layer keeps the *request prefix*
stable so repeated turns hit the DeepSeek prompt cache. See
[`cache/cache-architecture.md`](cache/cache-architecture.md) and
[`cache/cache-telemetry.md`](cache/cache-telemetry.md). Capture is
`.opencode/plugins/openbrcm-cache.ts`; local log `.openbrcm-local/cache-telemetry.jsonl`
(gitignored); report `scripts/cache-report.py`. It is optimization metadata and
never a source of truth or a CI gate.

## 9. Filing tooling gaps

If `re`/`re.db` cannot answer a question and manual `objdump`/`readelf`/`r2` was
required, record the gap so the same manual work is not repeated:

1. Open an issue or note in the milestone document under "tooling gap".
2. State the exact question, the manual command used, and the desired `re`
   subcommand/field.
3. Continue with the manual result **marked as manual**, until the tool covers
   it (then re-verify against `re`).

Known gap (2026-09): loop-carried MMIO offsets sourced from anonymous `.rodata`
tables print as `? + 0x0` in `re fn`; resolve with `re gstruct`/`re data`/the
table commands, or add table-aware MMIO resolution to `re`.

Known gap (2026-09): `re` panics with `failed printing to stdout: Broken pipe`
when output is truncated by a pipe (e.g. `re card … | head`). Redirect to a file
or use `--json` instead of piping to a short consumer.

Tooling gaps filed during the M3.4D3B MHF value closure (analysis only; see
`docs/m34d3b_band_init.md` §3.8.6). These are **scoped `re` improvements**, not
requests to redesign the tool; each was worked around manually and the workaround
is recorded in the milestone document:

- **T1 — cross-function struct-field writer enumeration.** `re fields <fn>` is
  per-function, so finding *every* writer of `wlc_info->pub+0x54` required a
  whole-`.text` `,0x54(%r*)` store scan. Desired: `re fields --struct <name>
  --writers` returning function + site + base provenance.
- **T2 — structure-field aliasing / pointer provenance.** In `wlc_set_gmode`,
  `re fields` attributed `band+0x54` to `*(a0 + 0x0)` (pub), flattening the
  `band` pointer loaded from `wlc+0x40`; manual disasm was needed to separate
  `band+0x54` from `pub+0x54`. Desired: exact base-pointer provenance.
- **T3 — immediate/value propagation.** `re` did not show stored immediates
  (`sub_62766` MHF offsets `0x5e/0x60/…`; `wlc_info_init` `0xffffffff`;
  `si+0x8 = 0x804`); manual objdump was required. Desired: immediate/constant
  value tracking in `re fn`.
- **T4 — string-literal cross-references.** Finding the `srom_parsecis`
  references to the literal field-format strings (`aa2g=0x%x`,
  `antswitch=0x%x`, …) required `readelf -r` plus a manual `.rodata`→file-offset
  map. Desired: `re refs <string-literal>`.
- **T5 — reach output count vs function set.** `re reach <root>` prints only the
  reachable *count*, so it could not be intersected with field writers. Desired:
  `re reach --list`.
- **T6 — overlapping ELF function symbols.** `srom_parsecis` (0x46a3, size
  19103) and `srom_var_init` (0x9704) overlap (0x9704 < 0xb12e); `re fn`
  inherits the ambiguous boundary. Desired: overlapping-symbol detection/flagging
  and a disambiguation policy.
- **T7 — table-derived MMIO offsets** (already noted above) blocked a direct
  `re`-only reconstruction of the vendor `srom_parsecis` raw-field cursor;
  recovering the rev11 field offsets needs the table-aware resolution.
  **RESOLVED (2026-09, M3.4D3B rev11 field map).** The BCM4352/PCIe path does
  not use `srom_parsecis` (that is the PCMCIA/CIS path); `srom_var_init 0x9704`
  synthesises the NVRAM variables from a **24-byte descriptor table** at
  `.rodata+0x1b00` (573 entries, terminator index 573), reached by the
  `mov r12, <reloc .rodata+0x1b00>` at `0x9a14` and decoded with
  `re fn srom_var_init --asm` + `readelf -r`. The reusable, pinned decoder is
  `scripts/srom_var_table.py` (JSON output; reproduces the vendor extraction
  incl. multi-word continuations and the skip-all-ones absent rule); the
  machine-readable map is `docs/m34d3b/rev11_sprom_fields.json`. Closing this
  needed *no* change to `re` itself: the missing capability was file/data
  parsing (`.rodata` + `.rela.rodata` + `.rodata.str1.1`), which the analysis
  script now provides deterministically.
  - Remaining `re` feature request (so this is not re-done manually): a
    `re data --at <section>+<off> --reloc-names` (or `re table`)
    subcommand that resolves section-relative data relocations and prints the
    targeted strings, i.e. the exact step `srom_var_table.py` performs now.


Tooling gaps filed during the M3.4D4B value-provenance closure (analysis only;
see `docs/m34d4b/d4b_value_provenance_closure.md`):

- **D4B-G1 — `re field-writers` store-vs-read misclassification.**
  `0xab3d8` is `cmp byte ptr [r12+32Dh],0` (a read), yet `re field-writers
  --field 0x32d` reports it as a store. **RESOLVED (2026-09, `cf9738e`):**
  `analyze_facts` now uses iced `InstructionInfoFactory`/`UsedMemory` access;
  a `field_sites` store row is emitted only when the memory operand is
  actually written. Regression fixture: `wlc_phy_switch_radio_acphy` has 0
  stores to `0x16e` (the `cmpb` sites are reads).
- **D4B-G2 — path-sensitive reachability.** `re` reports a function's callees
  but not *which* callees execute for a given argument/branch. Determining that
  `wlc_phy_attach` takes the **radio-OFF** branch of
  `wlc_phy_switch_radio_acphy` (so `sub_9591e`/`sub_a4adc` are not executed on
  the initial attach path) required manual control-flow over `re --asm` +
  Ghidra. Desired: a `re path-reach <fn> --arg <n>=<v>` or branch-predicate
  annotation on call edges.

Tooling gaps filed during the M3.4D4D tooling-gap closure (analysis only; see
`docs/m34d4/tooling_gap_closure.md`):

- **V5-G1 — multi-level pointer provenance.** The local tracker collapses
  `**(base+off)` (`mov 0x20(%rbx),%rdi; mov (%rdi),%rax; call *0xa0(%rax)`) to
  `base_load=0`, so vtable-dispatched sites (`wlc_bmac_init @0x6923d/0x6924a`)
  match unrelated field constructors. Desired: preserve the parent load chain
  so `(base_arg, base_load, field)` is exact for nested dereference.
- **V5-G2 — vtable / ops-table resolution.** `call *(*(obj)+slot)` where the
  ops table is runtime-assembled (no static reloc at the slot) cannot be
  resolved by `re` or Ghidra. Desired: `re indirect --vtable` that binds an
  object's constructor and follows store-installed ops tables where possible;
  otherwise report `UNRESOLVED:runtime-vtable` distinctly from
  `UNRESOLVED:no-installer`.
- **V5-G3 — heap-object base provenance.** `wlc_phy_attach` allocates `pi`
  (`osl_malloc`+`osl_memset`) and stores `pi+0x16e` (`mov %al,0x16e(%rbx)`),
  but `re field-writers --field 0x16e` does not index it because the allocator
  return is `V::Unk` and `rbx` loses the base. Desired: track a returned
  allocation as an anonymous object base (`ret_of(osl_malloc)`), or at least
  surface the store with `base=rbx/unknown`.

Tooling gap filed during the M3.4 lifecycle reconstruction (analysis only; see
`docs/lifecycle/bcm4352_rev42_lifecycle.md`):

- **L-C1 — external / cross-binary / RPC-dispatch call edges.** `re`/`re.db`
  models only in-object call edges. `wlc_bmac_radio_hw` is an RPC-dispatched
  target (`WLRPC_WLC_BMAC_RADIO_HW_ID`) with no in-object caller; this fact has
  no DB representation. A future `external_symbols` / `cross_image_calls`
  table (external symbol/API, candidate caller, normalized function identity,
  confidence/provenance) would prevent re-deriving it by four-tool
  cross-checking. Not implemented (not required for this analysis).

Tooling gap filed during the M3.4D4A reachability recovery (analysis only; see
`docs/m34d4/d4a_reachability_recovery.md`):

- **D4A-G1 — `re switch` does not print jump-table targets.** For
  `wlc_phy_cal_perical` the reason-argument jump table (case 4/5/6 → AC
  calibration) had to be mapped with the Ghidra decompiler. Desired:
  `re switch <fn> --targets` listing each indirect jump-table target.

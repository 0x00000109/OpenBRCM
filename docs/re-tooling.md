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
| verifier | `re verify --db re.db` |

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

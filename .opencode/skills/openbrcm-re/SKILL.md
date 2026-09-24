---
name: openbrcm-re
description: Use when reverse-engineering the Broadcom/Brcm vendor blob for OpenBRCM (wlc_hybrid.o_shipped, re.db, re), or when investigating functions, call graphs, structure fields, MMIO/register writes, PHY/radio tables, branch predicates, value provenance, or cross-binary matching (wl7-64.sys / wl10-64.sys / wl.ko / bcm4352_recover.py). Tool-first: query the re.db index before manual objdump/readelf/r2/grep.
---

# OpenBRCM reverse engineering (tool first)

Canonical manual: [`docs/re-tooling.md`](../../../../docs/re-tooling.md).
Read it first. This skill is the workflow; that document is the reference.

## Absolute rule: query the index before manual disassembly

The vendor blob is indexed in a persistent SQLite database and queried with a
Rust CLI. Facts already in the index **must** come from `re`; do not rediscover
them with `objdump`/`grep`.

```
re:  /media/kartashoff/Storage/opensource/iced/test/binary_analyzer/target/release/re
db:  /media/kartashoff/Storage/opensource/iced/test/re.db
blob sha256: 352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743
```

Verify the index is current before trusting it:

```sh
scripts/re-bootstrap.sh          # from the OpenBRCM repo root, read-only
```

Manual disassembly is permitted only when (a) `re`/`re.db` cannot answer the
question, or (b) it is independent verification. File any missing fact as a
tooling gap in `docs/re-tooling.md` §9.

## Workflow

```
deterministic capture (re.db) -> re query -> compact evidence packet ->
reasoning -> targeted manual check (only if needed) -> documentation
```

Every RE finding carries a `re` command and, where possible, `--json` output.

## 1. Querying re.db

Use the repository wrapper, which cd's to the tooling workspace so the relative
vendor-blob path in `re.db` resolves:

```sh
scripts/re.sh stats                    # index shape
scripts/re.sh fn <name|0xaddr>         # boundary, calls, MMIO
scripts/re.sh card <name|0xaddr>       # callers / callees
```

Direct binary (only from the tooling workspace, with `--db`):
`re` = `/media/kartashoff/Storage/opensource/iced/test/binary_analyzer/target/release/re`,
`re.db` = `/media/kartashoff/Storage/opensource/iced/test/re.db`.

Throughout this skill, `re <cmd>` means `scripts/re.sh <cmd>`.

## 2. Function / call-graph analysis

`re card <fn>` gives callers and callees; `re flow`/`re seq` give ordered call
flow; `re reach <fn>` gives reachability from an entry. Use these instead of
walking `call` instructions by hand.

## 3. Structure fields

`re fields <fn>` lists `base+offset` field accesses (which struct field a
function touches). `re named` resolves known named registers/globals. Use
`re gstruct` to reconstruct nested/global structures.

## 4. MMIO / register accesses

`re fn <fn>` prints the MMIO list with `base`, `offset`, `value`, `width`, and
`site`. Group by cluster with `re fields --cluster ...`. Do not guess a
register write: every write needs an indexed or gate-proven provenance.

## 5. Indirect PHY / radio / table accesses

`re phy`, `re tables`, `re ptrtable`, `re gstruct`, and `re data <global>` cover
indirect register helpers (`phy_reg_write`, `write_radio_reg`,
`wlc_phy_table_write_*`) and table-driven sequences.

## 6. Branch predicates

`re switch <fn>` extracts compare/test conditions and jump tables. Capture the
exact predicate (e.g. word-zero vs bit-test) — do not paraphrase.

## 7. Value provenance

`re fn <fn> --values` shows constant-load / read-modify-write / poll tracking.
Use it to trace where a gate value comes from before claiming it.

## 8. Cross-binary matching

For a different image (Windows `wl7-64.sys` / `wl10-64.sys`, Linux `wl.ko`),
use the normalized-fingerprint matcher — not byte diffing:

```sh
cd /home/kartashoff/brcm-re
python3 scripts/bcm4352_recover.py resolve-symbols wlc_hybrid.o_shipped wl.ko --out symbol_recovery.json
python3 scripts/bcm4352_recover.py diff-versions  wl7-64.sys wl10-64.sys --out version_diff.json
python3 scripts/bcm4352_recover.py cross-match    wl.ko wl7-64.sys --out cross_os_equiv.json
```

## 9. Machine-readable evidence

Append `--json` to `re` commands. Record facts as JSONL in the milestone
directory (`docs/<milestone>/evidence.jsonl`): `claim`, `command`, `result`,
`gate`. Cite the packet from the milestone document.

## 10. Escalation to manual tools

Only after 1–7 fail. State which question `re` could not answer, run the minimal
`objdump`/`readelf` needed, mark the result **manual**, and add a tooling gap so
the next agent queries `re` instead. Then re-verify against `re` once covered.

## Gates

Before recording a finding, run the independent gates from the tooling
workspace: `verify_edges.py`, `verify_decode.py`, `verify_tables.py`,
`verify_crossarch.py`, `oracle_exec.py`, and `re verify` (strict). All must PASS.

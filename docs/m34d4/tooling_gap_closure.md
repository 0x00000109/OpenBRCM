# M3.4D4D — tooling-gap closure (re v5)

Status: `IMPLEMENTED` / `STATIC TESTED` (all gates PASS). **ANALYSIS ONLY** — no
hardware, no MMIO, no driver execution, no D4 implementation, no candidate.

This closes the two `re` defects exposed by the post-D3B reachability
correction (OpenBRCM `593061d`). Rust changes are committed **separately** in
the tooling workspace (`/media/kartashoff/Storage/opensource/iced/test`) as
**`f5d03da`** (`re: v5 base-aware indirect resolution + discovered-fragment
boundaries`), with the store-vs-read follow-up **`cf9738e`**
(`re: classify field stores via iced operand access (D4B-G1)`). The vendor blob
and `re.db` are unchanged
(`352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`).

## 1. Why

D4D proved the `pi_fptr` slots installed by `wlc_phy_attach_acphy` are
relocation-covered `imm32` (not NULL). `re` mis-resolved the consumer
`wlc_phy_init @0xbad4c` to `wlc_phy_init_lpphy` and mis-sized `sub_b018f`
(45 B instead of 710 B). Both blocked further D4 analysis.

## 2. PHASE 1 — base-aware indirect resolution

Defect: `populate_indirect_candidates` matched installers by `store_disp`
(field offset) **only**, ignoring the base object. `address_taken` only
contained *named* targets, so the unnamed AC callback `sub_b018f` was invisible
and the named LPPhy one won.

Fix (schema **v5**):
- `indirect_targets` now carries `base_arg`, `base_load`, `field_offset`,
  `installer`, `family`.
- `analyze_facts` captures the full base identity. Memory `[reg+disp]`: the
  parent object load is `base_load`, the dereferenced field is `disp`
  (`field_offset`); register `call *reg` through a loaded pointer keeps
  `base_load=0`, `field_offset=load+off`.
- Candidate resolution walks `field_sites` **constructor stores** at the exact
  `(base_arg, base_load, field)` triple. Relocation-covered stores (`reloc`,
  with `value_target`) are strong; `address_taken` (lea/imm) are weak
  fallbacks. Targets are named from `functions` when the symbol is unnamed
  (`sub_b018f`).
- Confidence: `EXACT` only when a single constructor candidate matches;
  otherwise `CONDITIONAL` with the **full candidate set** (never a wrong
  singleton); `UNRESOLVED` when no installer exists.
- New query selectors: `re indirect … --installer <substr> --family <substr>`.

### Regression fixture — the AC constructor table (BCM4352 rev42)

`wlc_phy_attach_acphy` installs (reloc EXACT, `base_arg=0`, `base_load=0`):

| pi offset | installer target | family |
|---|---|---|
| `+0x28` | `sub_b018f` | acphy |
| `+0x30` | `sub_8e77a` | acphy |
| `+0x38` | `sub_a7089` | acphy |
| `+0x40` | `sub_9949f` | acphy |
| `+0xc0` | `sub_97e2b` | acphy |
| `+0xc8` | `sub_92e67` | acphy |
| `+0xd0` | `sub_99528` | acphy |
| `+0xf8` | `wlc_phy_btc_adjust_acphy` | acphy |
| `+0x100` | `sub_9737d` | acphy |
| `+0x110`, `+0x118` | (no installer → `UNRESOLVED`) | — |

All ten checks are enforced by `re regress`. `re indirect wlc_phy_init` now
lists the full candidate set (abgphy/htphy/lcnphy/lcn40phy/lpphy/**acphy**);
`re indirect --field 0x28 --family acphy` returns `sub_b018f`.

**Remaining Phase-1 gap (filed, not fixed):** the local tracker still collapses
**multi-level dereference** provenance — `**(base+off)` (`mov 0x20(%rbx),%rdi;
mov (%rdi),%rax; call *0xa0(%rax)`) is reported with `base_load=0` instead of
`0x20`, which produces false family candidates for such sites (see §4, Phase 3).

## 3. PHASE 2 — discovered-fragment boundaries

Defect: the linear-sweep local-call heuristic seeded spurious entries at
mid-instruction addresses (e.g. `sub_b01bc @0xb01bc`, `sub_b02ae @0xb02ae`
inside `sub_b018f`). The next-symbol clamp then truncated the **real**
containing function to the fragment start (`sub_b018f` → `0x2d`).

Fix: track, during the CFG decode, the root-most (smallest-address) function
that (a) starts an instruction (`insn_root`) and (b) covers each byte
(`byte_owner`). A discovered entry is demoted when a smaller function covers
its start byte but did not start an instruction there (mid-instruction), or
when it lies inside the **terminally-bounded** extent of a known entry
(`trusted`: a symbol, code-pointer target, or `.rela.text` address-taken
target whose extent does not run into the next known entry). Demoted rows
(`620`) are removed from `functions`; their relocation-proven call edges are
preserved **unattributed** so the `elf ↔ db` edge multiset is unchanged.

| function | old | corrected | reason | confidence |
|---|---|---|---|---|
| `sub_b018f` | `0xb018f..0xb01bc` (0x2d) | `0xb018f..0xb0455` (0x2c6) | nested mid-instruction fragments removed | high |
| `sub_8e77a` | 6 B | 6 B (unchanged) | ret stub | high |

## 4. Verification (all PASS)

| gate | result |
|---|---|
| `re regress` | PASS (incl. AC table + `sub_b018f` size) |
| `re verify --strict` | PASS (0 unresolved internal calls, 0 reloc mismatches) |
| `scripts/verify_edges.py` | PASS (pyelftools == goblin) |
| `scripts/verify_decode.py` | PASS (capstone == iced) |
| `scripts/verify_tables.py` | PASS (cp_tables == reloc truth) |

Index shape: functions `4475` (`628` sym + `1753` discovered), calls `51193`
(unchanged), indirect `209`, field_sites `20557`. `reloc-site mismatches: 0`.

## 5. Residual tooling gaps (filed)

1. **V5-G1 — multi-level pointer provenance.** `**(base+off)` collapses the
   parent load (`base_load=0`), so vtable-dispatched sites
   (`wlc_bmac_init @0x6923d/0x6924a`) get false family candidates.
2. **V5-G2 — vtable/ops-table resolution.** `call *(*(obj)+slot)` where the ops
   table is runtime-assembled cannot be resolved statically by `re` or Ghidra
   (no static reloc at the slot). Desired: `re indirect --vtable`.

# M3.4D4B Phase 0 — two `re` false-positive classes fixed

**Scope:** correctness fixes demonstrated by the Ghidra augmentation. No
hardware, no MMIO, no driver code. Tooling change committed separately in the
canonical workspace (`iced/test`, commit `f334e21`).

Baseline after fix: `re` `0c7cf875…`, `re.db` `44dae60d…`, schema **v4**,
blob `352a6e34…`, `re regress` **PASS**.

## A. Relocation-covered immediates

**Defect:** `movq $imm, 0xf8(%rbx)` decodes with immediate `0`; without
relocation context `re` rendered/classified it as literal **0**. The field is
actually `wlc_phy_btc_adjust_acphy` (`R_X86_64_32S` at `0xa3059`).

**Fix:** `text_relocs()` maps `R_X86_64_64/32/32S` `.text` fields to
`(symbol, value)`; `analyze_facts` gained a `V::R(symbol, value)` variant so a
relocation-covered immediate/pointer is never a literal:

- `field_sites` row: `kind='store'`, `value='&wlc_phy_btc_adjust_acphy'`,
  `provenance='reloc'`, `confidence='EXACT'`;
- `const_props` row with `kind='reloc'` for relocated `mov $sym,%reg`;
- standalone relocated immediates are recorded with `kind='reloc'`.

**Regression fixture:** `phy+0xF8 reloc install indexed` — the store at
`0xa3052` must resolve to `&wlc_phy_btc_adjust_acphy`, never literal zero.

## B. Linear-dataflow false exactness

**Defect:** a single linear register scan emitted **EXACT** constants across
branches/loops/arrays. Demonstrated by `re const --callee dma_attach` reporting
`arg3 = 0x240/0x280/0x2c0` as exact, when RDX is reloaded from memory on other
paths (Ghidra: the real bases are `dev+0x200/0x220/0x240/0x260`).

**Fix:** per-register `(write_count, branch_epoch)` tracking. A value is
`EXACT` **only** when it has a single reaching definition that executes before
any conditional branch (`reg_exact`); otherwise `CONDITIONAL` with the
candidate set. Ambiguous reaches are recorded as `kind='arg-candidates'`,
`confidence='CONDITIONAL'`, `candidates=<set>` — never `EXACT`.

**Regression fixture:** `dma_attach consts not EXACT` — zero rows with
`callee='dma_attach' AND confidence='EXACT'` (was 3 spurious EXACT rows).

## Schema

`re.db` schema v4 adds `confidence` to `mmio_sites`/`field_sites`,
`confidence,candidates` to `const_props`. `re db build` repopulates all v3
tables with the corrected provenance. `re regress` covers the two fixtures and
the pre-existing proven facts (initvals 610/113/497, bsinitvals 73/39/34,
sub_67efd, switch_macfreq, DMA/MAC).

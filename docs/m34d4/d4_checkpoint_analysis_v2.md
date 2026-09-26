# M3.4D4A v2 — `wlc_phy_init` re-analysis with the accelerated `re` tooling

**Milestone:** RE TOOLING D4 ACCELERATION (Phases 9–11).
**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.
The canonical `re` tool (+`re.db`) was extended; this document records the
re-run of the D4 flow with those capabilities.

Machine-readable evidence:
[`wlc_phy_init_rev42_flow_v2.json`](wlc_phy_init_rev42_flow_v2.json),
[`phy_operations_rev42.json`](phy_operations_rev42.json),
[`radio_operations_rev42.json`](radio_operations_rev42.json).
Prior analysis: [`d4a_decomposition.md`](d4a_decomposition.md).

## 1. Tooling baseline (Phase 0)

| item | value |
| :--- | :--- |
| `re` sha256 (after) | rebuilt from source in `iced/test/binary_analyzer` |
| `re.db` sha256 | regenerated (v3 schema) |
| blob sha256 | `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743` |
| functions / calls | 5095 / 51193 |
| reloc-site mismatches | 0 |
| `scripts/re-bootstrap.sh` | PASS |

The new modules populate `mmio_sites`, `imm_sites`, `field_sites`,
`indirect_targets`, `const_props`, `phy_ops`, `reg_tables`, `reg_table_ops`
(schema version 3) and add the commands `re mmio`, `re imm`,
`re field-writers`, `re indirect`, `re table`, `re regtables`, `re const`,
`re phyops`, `re dump --json`, `re regress`.

## 2. Regression (Phase 8) — `re regress`

```
PASS  common records 610 / w16 113 / w32 497
PASS  bsinitvals records 73 / w16 39 / w32 34
PASS  sub_67efd 0x530, 0x540
PASS  switch_macfreq 0x62e, lea 0x630
PASS  DMA FIFO0 RX 0x220, DMA reset helpers sub_f5ef/sub_f64a
GAP   DMA TX base 0x200/0x240/0x280/0x2c0 (loop-carried channel offset)
PASS  MAC reg 0x120 / 0x124 / 0x128 present
```

The initvals/bsinitvals regression is **exact** (the tool reproduces the
610/113/497 and 73/39/34 shapes). Two honest limitations remain, recorded as
gaps rather than silent passes: loop-carried DMA TX channel offsets, and
control-flow-insensitive value tracking (see §6).

## 3. Phase 9 — D4 re-run result

### 3.1 The two indirect targets

| target | tooling result |
| :--- | :--- |
| `wlc_phy_init` `0xbad4c` `call r13` (= `[phy+0x28]`) | candidate set = `address_taken store_disp=0x28` family inits (`wlc_phy_init_htphy`, `_lcnphy`, `_aphy`, `_gphy`, `_lpphy`, `_sslpnphy`, `_nphy`); **no `acphy` installer** → `CONDITIONAL` |
| `wlc_phy_anacore` `0xbabc7` `call rax` (= `[phy+0x118]`) | **`UNRESOLVED`** — no store to `+0x118` anywhere in `.text` (`re field-writers --field 0x118`) |

Both sites are now **located with provenance** (`re indirect`), but neither is
uniquely resolved for AC.

### 3.2 The decisive finding

`wlc_phy_init` gates its whole body on `[phy+0x28]`:

```
0xbac6f  mov r13,[rbx+28h]
0xbac73  test r13,r13
0xbac76  je 0xbaecE           ; -> pop/leave/ret, no work
```

`re field-writers --field 0x28` + the vendor attach shows
`wlc_phy_attach_acphy` **unconditionally zeros** `[phy+0x28]`:

```
0xa3001  mov qword ptr [rbx+28h],0
0xa3009  mov qword ptr [rbx+30h],0
```

Therefore, on the BCM4352 rev42 **AC** path, `[phy+0x28] == 0` and the entire
`wlc_phy_init` body (anacore, `wlapi_bmac_bw_set`, `wlc_phy_switch_radio`,
`do_dummy_tx`, `ant_rxdiv_set`, `sub_b740d`, final SHM read) is **skipped** at
the band-init call site (`sub_6656c` @ `0x669df`). The two previously
"unresolved indirect targets" are consequently **not on the rev42 AC execution
path**: `0x118` is only reachable from inside the skipped body.

**Scope correction:** the D4A assumption "AC PHY bring-up = `wlc_phy_init`" is
wrong for this design. On AC the PHY is initialised at attach time
(`wlc_phy_attach_acphy`, 6189 B, + its acphy callees) and in the radio/anacore
helpers; `wlc_phy_init` at band init is effectively a no-op for rev42 AC.

### 3.3 Operation accounting (tool-derived)

| class | recovered |
| :--- | ---: |
| `PHY` ops (`phy_reg_*`, acphy/htphy/nphy helpers) | 8157 |
| `RADIO` ops (`*_radio_reg`) | 3414 |
| `PHY_TABLE` ops (`wlc_phy_table_write_*`) | 1610 |
| MMIO sites (whole blob) | 1515 |
| immediates (whole blob) | 97992 |
| register tables (initvals family) | 161 |
| indirect sites (CONDITIONAL / UNRESOLVED) | 44 / 104 |

Relevant rev42-AC subsets: `wlc_phy_attach_acphy` 11 PHY ops;
`wlc_phy_switch_radio_acphy` 62 RADIO ops; `wlc_phy_init` body 2 (`phy_reg_read`
on the phytype==2 branch, not AC) and 1 indirect (`0x28`, skipped);
`wlc_phy_anacore` 1 indirect (`0x118`, unreachable on AC).

**Unknown write values before CP-F:** none are *executed* on the rev42 AC path
because the body is skipped. On a hypothetical non-AC execution the unresolved
values are the `[phy+0x118]` callee and the radio opcode streams in
`wlc_phy_switch_radio_acphy` — still `UNKNOWN`/`UNRESOLVED`.

## 4. Phase 10 — checkpoint reassessment (not inherited)

Evaluation criteria per candidate checkpoint (AC rev42 band init):

| checkpoint | outstanding PHY async | MAC suspended | radio transitional | PLL | calib | DMA | deterministic status | teardown | device-lost detect | verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| A chanspec SHM | n/a | no | no | no | no | n/a | no | n/a | latch available | INVALID |
| B anacore | n/a (skipped on AC) | no | no | no | no | n/a | no | n/a | yes | NOT ON AC PATH |
| C bw_set | n/a (skipped) | no | no | no | no | n/a | no | n/a | yes | NOT ON AC PATH |
| D radio switch | n/a (skipped) | no | no | no | no | n/a | no | n/a | yes | NOT ON AC PATH |
| E before final SHM read | n/a (skipped) | no | no | no | no | n/a | no | n/a | yes | NOT ON AC PATH |
| **F `wlc_phy_init` return `0x669e4`** | none | no | no | no | no | B/W live (D3A1) | no (returns immediately) | verified | yes | **STABLE but TRIVIAL for AC** |
| G full `sub_6656c` tail | — | — | — | — | — | — | — | — | — | LATE |

**CP-F is a real, stable vendor boundary**, but on AC it is a *no-op return*:
it proves no outstanding `wlc_phy_init` PHY work, because none ran. It does
**not** prove "AC PHY initialised at this point". The previous `PROPOSED STRONG`
is therefore **downgraded**: CP-F is `PROVEN VENDOR-STABLE (return boundary)`
but `NOT A PHY-INIT CHECKPOINT FOR AC`.

Consequence for D4: the next milestone must target the **actual AC init**
(`wlc_phy_attach_acphy` + acphy radio/anacore graph), not `wlc_phy_init`. A new
decomposition of `wlc_phy_attach_acphy` is required before any D4 code.

## 5. Phase 11 — performance

| metric | value |
| :--- | :--- |
| `re db build` (full, incl. v3 populate) | **3.5 s** |
| `re.db` size | 5.7 MB (v2) → **17.1 MB** (v3) |
| raw `objdump`/manual-disasm invocations this task | 2 (one `nm -C` symbol listing, one one-off byte scan used only to *motivate* a tooling query) |
| `re` queries for the D4 re-run | `regress`, `mmio`, `imm`, `field-writers` (0x28/0x118), `indirect` (×4), `regtables`, `table`, `phyops`, `dump` (×7) |
| D4 artifact generation | < 1 s (7 `re dump` calls) |
| unresolved operations | 104 indirect `UNRESOLVED`, DMA TX loop offsets `GAP` |

The acceleration is real: the facts that previously required whole-`.text`
`objdump` sweeps (field writers, MMIO/immediate search, indirect candidates,
initvals decoding) are now single indexed queries, and the initvals regression
is machine-checked.

## 6. Tooling gaps (filed, not hidden)

1. **Loop-carried MMIO offsets** (T7): DMA TX base `0x200/0x240/0x280/0x2c0`
   computed from a channel index is not resolved; `re regress` reports `GAP`.
2. **Control-flow-insensitive tracker**: value tracking is a single linear pass.
   It is partially mitigated by preserving argument-derived provenance across
   calls (which made `[phy+0x118]` visible), but branch-merged values can still
   be attributed to the wrong path.
3. **No installer for `phy+0x118`**: the ops pointer is installed by a
   mechanism outside the modelled store forms (likely an ops-template copy);
   `re field-writers` correctly reports none rather than guessing.
4. **`re imm | head` broken pipe** (pre-existing): use `--json` or redirect.

## 7. Answers (Phase 9/10)

- WLC_PHY_INIT INDIRECT TARGETS RESOLVED? **PARTIAL** — `0x28` resolved as
  *zero for rev42 AC* (proven by `wlc_phy_attach_acphy` `0xa3001`); `0x118`
  `UNRESOLVED` but unreachable on the AC path.
- CP-F VENDOR-STABLE PROVEN? **NO as an AC PHY-init checkpoint** (the return is
  stable but the body does not run for AC). Do not build D4 on `wlc_phy_init`.
- D4 IMPLEMENTATION GO? **NO** (scope must be re-derived from
  `wlc_phy_attach_acphy`).
- HARDWARE TEST GO? **NO.**

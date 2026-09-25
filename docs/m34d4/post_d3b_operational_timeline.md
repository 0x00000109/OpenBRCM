# M3.4D4 — post-D3B operational PHY/radio timeline (ANALYSIS ONLY)

**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.
**Tool revision:** `re` v4 (`f6601944…`), `re.db` schema v4 (`185f0bd8…`,
gitignored), vendor blob sha256
`352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`.
**Prior commit:** `db151c8`.
**Machine-readable:** [`operational_callgraph.json`](operational_callgraph.json),
[`radio_on_transition.json`](radio_on_transition.json),
[`initial_chanspec_provenance.json`](initial_chanspec_provenance.json),
[`calibration_path.json`](calibration_path.json),
[`evidence_post_d3b_reachability.json`](evidence_post_d3b_reachability.json).

## 0. Headline — a major correction

The prior D4A/D4B/D4C conclusions are **superseded**:

> `wlc_phy_init` is **not** a no-op for BCM4352 rev42 AC. It is the **real AC
> PHY/radio init entry**, reached directly from `sub_6656c` on the vendor up
> path, and it performs the **first vendor-executed radio-ON transition**.

The previous error: the `pi_fptr` function-pointer slots installed by
`wlc_phy_attach_acphy` use **relocation-covered `imm32`** (`movq $imm,disp(%rbx)`).
In the relocatable object the raw bytes are `0`, and they were read as `NULL`.
The relocations install real functions:

| field | consumer | installed target | evidence |
| :--- | :--- | :--- | :--- |
| `pi+0x28` | `wlc_phy_init @0xbad4c` | **`sub_b018f`** (`R_X86_64_32S .text+0xb018f`) | `objdump -r`, readelf, `re field_sites` (reloc EXACT) |
| `pi+0x30` | `wlc_phy_cal_init @0xb1bc9` | `sub_8e77a` (6-byte `ret` stub → effective no-op) | `objdump -r` |
| `pi+0x38` | (chanspec/init helper) | `sub_a7089` | `objdump -r` |
| `pi+0x40` | — | `sub_9949f` | `objdump -r` |
| `pi+0xc0` | `wlc_phy_detach @0xbaf78` | `sub_97e2b` | `objdump -r` |
| `pi+0xc8` | — | `sub_92e67` | `objdump -r` |
| `pi+0xd0` | — | `sub_99528` | `objdump -r` |
| `pi+0x100` | — | `sub_9737d` | `objdump -r` |
| `pi+0xf8` | `wlc_phy_watchdog @0xbc770` | `wlc_phy_btc_adjust_acphy` | D4B (correct) |

Only `pi+0x110` (radio dispatch callback, bypassed by the explicit AC branch at
`0xba3a1`) and `pi+0x118` (anacore callback, fallback D11 `0x3e6`) are genuinely
uninstalled. The D4B `acphy_function_table.json` "10 INTENTIONALLY_NULL" fields
are **wrong** and are superseded by
[`evidence_post_d3b_reachability.json`](evidence_post_d3b_reachability.json).

Method: the relocated-`imm32` conflict was settled with `objdump -r` (sanctioned
manual fallback; `re`/`re.db` was queried first and its `field_sites` already
carried `provenance=reloc, confidence=EXACT`, so `re` was authoritative and
`objdump` confirmed it).

## 1. Exact post-D3B continuous call graph

Initial up: `wl_init -> wlc_up -> wlc_init @0x3c46f`.

```
wlc_init 0x3c6f1  -> wlc_bmac_init(hw, chanspec, 0, 0)
wlc_bmac_init 0x695c? wlc_bmac_switch_macfreq        (D3A1 tail)
wlc_bmac_init 0x695d8 -> sub_6656c(dev, chanspec, dl=0)
  sub_6656c 0x669bd  -> sub_60f67(d11ac1bsinitvals42)          [D3B applier]
  sub_6656c 0x669c2  test r13b,r13b ; je 0x669d0              [dl==0 -> call]
  sub_6656c 0x669df  -> wlc_phy_init(pi=[dev+0xE8][+0x28], chanspec)
    wlc_phy_init 0xbac6f  r13=[pi+0x28]=sub_b018f (non-null)
    wlc_phy_init 0xbac84  -> wlc_phy_anacore(pi, ON)          [D11 0x3e6]
    wlc_phy_init 0xbad0e  [AC] [pi+0x138]+0x32d=0 ; chipid{4352,4360}: [pi+0xF88]=0
    wlc_phy_init 0xbad44  -> wlc_phy_switch_radio(pi, ON=1)   [FIRST RADIO-ON]
      wlc_phy_switch_radio 0xba3a1 -> wlc_phy_switch_radio_acphy(pi, ON)
        wlapi_suspend_mac_and_wait
        sub_8fb39               (82 PHY writes; first phy_reg_mod(0x830,0x7,0x3))
        mod_radio_reg 0xaa80f   (FIRST RADIO WRITE: 0x80b set 0x80)
        PLL: [pi+0x16e]==1 -> 0x80b seq + lock poll 0xaaab6
             [pi+0x16e]==2 -> sub_9027d + write_radio_reg(0x60c,0x9e) + lock poll
        sub_9fb72               (radio/PHY trims)
        sub_9591e               (mandatory radio RX calibration, bounded poll)
        sub_a04c2 ; sub_a7089   (chanspec_radio_set + rfpll + tables)
        wlapi_enable_mac
    wlc_phy_init 0xbad4c  -> sub_b018f(pi)                      [AC PHY init callback]
        phy_reg_mod(0x1b0 set 0x8000); si_pmu_regcontrol; si_gpiocontrol;
        wlc_phy_hirssi_elnabypass_init_acphy; sub_a04c2; sub_a7089(chanspec);
        18 phy_reg_write (0x33a..0x349, 0x16e..0x170); sub_affa9
    wlc_phy_init 0xbad6f.. -> wlc_phy_do_dummy_tx; txpower_update_shm; ant_rxdiv_set
  sub_6656c 0x669e7.. -> sub_62684; set_cwmin/max; mhf; extlna ...
wlc_init 0x3ca27  -> wlc_set_home_chanspec
wlc_init 0x3cde0  -> wlc_phy_cal_perical(pi, reason=6)
  wlc_phy_cal_perical 0xb5aac -> wlc_phy_cals_acphy(pi)
```

Per-node detail (address, caller, callee, predicate, reachability,
chanspec/band dependency, access classes) is in
[`operational_callgraph.json`](operational_callgraph.json).

**Key guard (why `wlc_phy_init` really runs):** `sub_6656c` calls it unless the
third argument (`dl`) is nonzero *and* `phyrev > 0x27`. On initial up,
`wlc_bmac_init @0x695d3` sets `edx=0`, so `dl==0` and the call is taken for
rev42. The D4A claim that the AC path returns at `0xbac76` was based on the
relocation misread.

**`wlc_phy_cal_init` nuance:** its callback `pi+0x30` is non-null (`sub_8e77a`)
but that target is a 6-byte `ret` stub, so `wlc_phy_cal_init` is *effectively* a
no-op for AC — the D4C conclusion was right in effect, wrong in mechanism.

## 2. Radio OFF → ON transition (Phase 2)

- **First vendor-executed `wlc_phy_switch_radio(..., ON)`:**
  `wlc_phy_init @0xbad44`, argument `esi=1` (`mov esi,1 @0xbad3c`).
- Dispatch: `wlc_phy_switch_radio 0xba395` (`phytype==0xb`) ->
  `wlc_phy_switch_radio_acphy @0xba3a1`, arg `sil=on`.
- `wlc_phy_switch_radio_acphy`: `0xaa796 test sil,sil; je 0xAB413` (OFF) — the
  ON branch starts at `0xaa7bb`.
- Attach OFF path (`wlc_phy_attach 0xbed84 anacore(1)`, `0xbf113
  switch_radio(OFF)`) is the attach/probe phase; `wlc_phy_init` is the later,
  matching ON transition.
- The RPC-only `wlc_bmac_radio_hw @0x63ec8` (no in-blob caller) remains a valid
  code path but is **not** the first vendor-executed radio-ON; the D4C
  "radio-ON is RPC-only / no vendor-time transition" conclusion is superseded.

First operations on the ON path:
- **first PHY indirect write:** `phy_reg_mod(0x830, 0x7, mask 0x3) @0x8fb54`
  (inside `sub_8fb39`, called from `switch_radio_acphy @0xaa7d1`).
- **first radio write:** `mod_radio_reg(0x80b, 0x80, 0x80) @0xaa80f`.
- **first PLL/synth:** branch-dependent on runtime `pi+0x16e`:
  `==2` → `sub_9027d` + `write_radio_reg(0x60c, 0x9e) @0xaab62` (+ lock polls
  on radio `0x0b`/`0x20b`); `==1` → radio `0x80b` PLL sequence + lock poll
  `read_radio_reg(0x80b)&1 @0xaaab6`. Exact = `UNKNOWN` (runtime predicate).
- **first PHY-table op:** `wlc_phy_table_write_acphy(table 0x3, val 0x1,
  mask=*(pi+0x8be)) @0xaa2ab` inside `sub_a7089` (called at `0xab3f5`).
- **first delay:** `osl_delay(100) @0xaa911`; **first bounded poll:**
  `0xaaab6`, ≤101 iterations.

## 3. Initial chanspec provenance (Phase 3)

`wlc_init` computes `r14` = the validated current chanspec, falling back to
`wlc_default_chanspec(wlc->band, 1)` (`0x3c5b7`/`0x3c602`), and passes it to
`wlc_bmac_init` (`@0x3c6ee`) and `wlc_set_home_chanspec`.

`wlc_default_chanspec @0x702a0` → `wlc_create_chspec` (locale-validated) →
`wlc_next_chanspec` → fallback `wlc_phy_chanspec_band_firstch @0xb1d3e` (walks a
static channel table using phytype/phyrev and band).

Classes: initial band = `COMPUTED_RUNTIME` (SPROM `aa2g`/`aa5g` +
NVRAM country); channel/bandwidth/sideband = `COMPUTED_RUNTIME` over a
`STATIC_TABLE` channel list with `NVRAM` locale; default chanspec =
`COMPUTED_RUNTIME`. **No guessed default.** Detail:
[`initial_chanspec_provenance.json`](initial_chanspec_provenance.json).

## 4. Calibration path (Phase 4)

Entry `wlc_phy_cal_perical(pi, 6)` from `wlc_init @0x3cde0`; gate
`phytype in {4,7,0xb}`, `[pi+0xf89] != 0,3`, reason−2 ≤ 9 jump-table
(base `0x2cdb28`); reason 6 → **`wlc_phy_cals_acphy @0xb5aac`**.

`wlc_phy_cals_acphy` (ordered, 68 functions) begins
`wlc_phy_rxcore_setstate_acphy` → `wlc_phy_noise_sample_request_crsmincal` →
`... sub_abc76/sub_addfa ...` → PHY `0x19e` ops + `wlc_phy_table_write_acphy
table 0xc` → `sub_9311b` (radio) → `sub_93e47` → mphase reset →
`wlc_phy_txpwrctrl_enable_acphy` → `wlc_phyreg_exit` → `wlapi_enable_mac`.

First calibration ops:
- first PHY: `phy_reg_mod(0x160, 0x7, mask=*(pi+0xa7)) @0x97d50`;
- first PHY-table: table `0xc` `@0xb156f`;
- first radio: `mod_radio_reg(0x8e5, 0x4000) @0x9315d`.

Completion = synchronous return (`mphase_reset` inside); no unbounded wait at
this level. Verified callees and per-value classes (`STATIC_LITERAL`,
`STATIC_TABLE`, `HARDWARE_RMW`, `COMPUTED_RUNTIME`, `UNKNOWN`) are in
[`calibration_path.json`](calibration_path.json).

## 5. Tables and generated values (Phase 5)

- `re regtables`: AC family tables (`d11ac1initvals42` 610/113/497, etc.)
  consumed by `wlc_bmac_init`; no separate AC PHY table name resolution for the
  `wlc_phy_table_write_acphy` stream (`table 0xc`, `table 0x3`).
- `re phyops` resolved the first PHY/radio/table operations listed above;
  `sub_8fb39` (82 PHY), `sub_9027d` (11 PHY), `sub_9fb72` (radio+PHY),
  `sub_a7089` (PHY/RADIO/PHY_TABLE, table `0x3`, masks `*(pi+0x8be/0x8bf)`).
- `re indirect` base-unaware candidate for `pi+0x28` is `wlc_phy_init_lpphy`
  (CONDITIONAL); the **AC-specific** install is `sub_b018f` from
  `re field_sites` (`src=wlc_phy_attach_acphy`, reloc EXACT).
- Farro/coefficient arrays (`sub_a4adc`) remain runtime-compacted
  (`COMPUTED_RUNTIME`); not manually reconstructed (re/`re table` did not index
  them; recorded as a gap below where relevant).

## 6. dev_lost coverage (Phase 6)

The proposed path introduces new **direct D11 MMIO** sites that must both
*observe* a trusted all-ones direct read (latch `hw->dev_lost`) and then
guarantee **zero** subsequent BAR writes:

| site | fn | access | offset |
| :--- | :--- | :--- | :--- |
| `0xbac44` | `wlc_phy_init` | `osl_readl` read | D11 `0x120` |
| `0xba31d` | `wlc_phy_switch_radio` | `osl_readl` read | D11 `0x120` |
| `0xbabee` | `wlc_phy_anacore` | `osl_writew` write | D11 `0x3e6` (`on?0x0:0xf4`) |
| PHY-window | `phy_reg_read/mod/write`, `sub_8fb39` chain | PHY indirect | D11 `0x3e0/0x3e6` + PHY window |
| MAC | `wlapi_suspend_mac_and_wait` / `wlapi_enable_mac` | read/write | D11 maccontrol |

**Status: NOT WIRED.** The existing monotonic latch is in
`src/ob_d3a0.c` (`ob_dev_lost_observe32`), guarded into `ob_d3a0_teardown`,
`ob_d3a0_quiesce`, `ob_d3a0_rx_reset`, `ob_d3a0_tx_reset`,
`ob_d3a0_can_free` and the remove hooks. It does not yet cover the new D4 sites
above. Per this task, no implementation was made (no candidate). The invariant
must hold: after the first trusted all-ones direct read — no D11 BAR write, no
DMA reset, no SHM/OBJ access, no PHY/radio access, no retry, no free. The D3B
crash demonstrates why: teardown after a suspected device loss is unsafe
(`docs/m34d3b/d3b_crash_postmortem.md`).

## 7. Checkpoint analysis (Phase 7)

See [`operational_checkpoint_analysis.md`](operational_checkpoint_analysis.md).
Summary: CP-O2 is `INVALID` (RPC-only); CP-D4.1 (after `wlc_phy_init`) is
`WEAK`; **CP-F** (after `wlc_phy_cal_perical`) is the best candidate but still
`WEAK`; **no STRONG checkpoint** exists (unresolved `wlc_bmac_init` indirect
callbacks `[rax+0xA0]`/`[rax+0xD8]`, runtime-only write masks, unwired
dev_lost).

## 8. Value closure / GO verdict (Phase 8)

Executed-write closure on the corrected path is **incomplete**:
- `pi+0x16e` selects the PLL branch → one executed PLL write value is `UNKNOWN`;
- masks `pi+0xa7`, `pi+0x8be`, `pi+0x8bf` and the farrow coefficient arrays are
  `COMPUTED_RUNTIME`;
- `wlc_bmac_init` reachable indirect calls `0x6923d [rax+0xA0]` and
  `0x6924a [rax+0xD8]` are `UNRESOLVED` (base-unaware candidates only);
- the initial band/channel are `COMPUTED_RUNTIME`, not a fixed constant.

**`D4 IMPLEMENTATION GO = NO`.** The entry point and scope are now corrected
and much narrower than before; the remaining blockers are value closure and
dev_lost wiring, not reachability.

## 9. Final answers

- **EXACT POST-D3B CALL GRAPH?** Recovered: `sub_6656c` →
  `wlc_phy_init(anacore→switch_radio(ON)→sub_b018f)` → rest of `sub_6656c` →
  `wlc_set_home_chanspec` → `wlc_phy_cal_perical` → `wlc_phy_cals_acphy`
  (`operational_callgraph.json`).
- **REAL AC INIT ENTRY?** `wlc_phy_init @0xbabf5` from `sub_6656c @0x669df`;
  its `pi+0x28` callback = **`sub_b018f`** (the actual AC init body).
- **FIRST RADIO-ON CALL?** `wlc_phy_init @0xbad44` →
  `wlc_phy_switch_radio(pi, ON=1)` → `wlc_phy_switch_radio_acphy(pi, ON=1)`.
- **INITIAL CHANSPEC?** `wlc_default_chanspec(wlc->band,1)` (`COMPUTED_RUNTIME`;
  locale/SPROM/NVRAM). No guessed default.
- **FIRST RADIO WRITE?** `mod_radio_reg(0x80b, 0x80, 0x80) @0xaa80f`.
- **FIRST PHY WRITE?** `phy_reg_mod(0x830, 0x7, mask 0x3) @0x8fb54`.
- **FIRST PHY TABLE OP?** `wlc_phy_table_write_acphy(table 0x3, 0x1,
  mask=*(pi+0x8be)) @0xaa2ab`.
- **FIRST PLL/SYNTH OP?** `UNKNOWN` branch on `pi+0x16e`; candidates
  `write_radio_reg(0x60c,0x9e) @0xaab62` (==2) or radio `0x80b` PLL sequence +
  lock poll `@0xaaab6` (==1).
- **FIRST CALIBRATION?** `wlc_phy_cals_acphy`; earlier mandatory radio RX cal =
  `sub_9591e`.
- **CALIBRATION COMPLETION CONDITION?** synchronous return of
  `wlc_phy_cals_acphy` (`mphase_reset` inside); `sub_9591e` uses a bounded
  `read_radio_reg()&1` poll (≤101 iters).
- **REACHABLE INDIRECT CALLS UNRESOLVED?** Yes — `wlc_bmac_init` `0x6923d
  [rax+0xA0]`, `0x6924a [rax+0xD8]` (base-unaware candidates only);
  `pi+0x28`/`pi+0x30`/`pi+0xc0` are **resolved**.
- **UNKNOWN EXECUTED WRITE VALUES?** Yes — `pi+0x16e` PLL branch, runtime masks,
  farrow coefficients.
- **DEV_LOST COVERAGE COMPLETE?** **No** (new D11 reads/writes not wired).
- **EARLIEST STRONG OPERATIONAL CHECKPOINT?** **None.** Best candidate = CP-F
  after `wlc_phy_cal_perical` returns (WEAK).
- **D4 IMPLEMENTATION GO?** **NO** (value closure + dev_lost).
- **HARDWARE TEST GO?** **NO.**

## 10. Method and tooling

- **re used?** Yes: `fn`, `card`, `field-writers`, `field_sites` (sqlite),
  `phyops`, `flow`, `switch`, `mmio`, `indirect`, `regtables`, `refs`,
  `jump_tables` (sqlite). Index queried before any manual disassembly.
- **Ghidra used?** No new Ghidra pass was required: `re` v4 reloc/field
  provenance plus `objdump -r` settled every ambiguity. (Ghidra remains the
  fallback for the residual `wlc_bmac_init` indirect callbacks.)
- **Manual RE required?** Yes, minimally: `objdump -r`/`objdump -d` to settle
  the relocated-`imm32` vs `NULL` conflict (sanctioned by AGENTS §7 as
  independent verification of an indexed fact, and because `re fn` cannot print
  the resolved reloc target for an unnamed local target).
- **New tooling gaps:**
  - `re indirect` is not base-aware: for `pi+0x28` in `wlc_phy_init` it reports
    `wlc_phy_init_lpphy` (a lpphy address-taken candidate) instead of the
    AC-specific `sub_b018f` that `re field_sites` knows. Desired:
    base-aware indirect candidate filtering.
  - `re fn <fn>` reports an incorrect `size` for discovered fragments
    (`sub_b018f` size `0x2d` vs real `0x2c5`); call lists are complete but the
    size field misleads. Desired: correct discovered-function extents (or
    overlap flagging; cf. T6).
- **Files created/changed:** `docs/m34d4/evidence_post_d3b_reachability.json`,
  `radio_on_transition.json`, `initial_chanspec_provenance.json`,
  `calibration_path.json`, `operational_callgraph.json`,
  `operational_checkpoint_analysis.md`, this report; plus the supersession
  updates in `docs/artifact-ledger.{md,json}`, `docs/agent-state.md`,
  `docs/milestones.md`.
- **Commit hash:** recorded at the end of this task.

## 11. Superseded facts (do not quote as current)

| obsolete | current | correcting artifact |
| :--- | :--- | :--- |
| `wlc_phy_init` is a no-op for AC | real AC init entry; callback `sub_b018f` | this report §0/§1 |
| `pi+0x28`/`+0x30`/`+0x38`/`+0x40`/`+0xc0`/`+0xc8`/`+0xd0`/`+0x100` are NULL | installed functions (table §0) | `evidence_post_d3b_reachability.json` |
| radio-ON is RPC-only; no vendor-time transition | `wlc_phy_init → switch_radio(ON)` | `radio_on_transition.json` |
| "D4 follows D3B via `wlc_phy_cal_perical` only" | `wlc_phy_init` + `sub_b018f` precede it | `operational_callgraph.json` |
| STOP-before-`wlc_phy_init` boundary invalid | boundary now valid in scope, but WEAK (CP-D4.1) | `operational_checkpoint_analysis.md` |

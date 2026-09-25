# M3.4D4C — radio-OFF attach → first operational BCM4352 AC state

**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.
Baseline `re` v4 (`0c7cf875…`), `re.db` `44dae60d…`, prior commit `84f9073`.
Machine-readable records:
[`d4c_radio_on_transition.json`](d4c_radio_on_transition.json).

**This corrects the D4B "186 PHY / 301 RADIO" figures: they are whole-family /
radio-ON-inclusive, NOT the on=0 attach path.** The actual attach path is 12 PHY
writes + 8 radio RMWs (see §3).

## 1. First real `wlc_phy_switch_radio(...,1)`

All 8 call sites (ELF relocations to `0xba302`, cross-checked by `re` + Ghidra +
`objdump`):

| caller | site | `on` | AC reached? | phase |
| :--- | :--- | :--- | :--- | :--- |
| `wlc_phy_attach` | `0xbf113` | **0** (`xor esi,esi` `0xbf10a`) | yes | attach/probe |
| `wlc_bmac_init` | `0x69594` | 0 (`xor esi,esi`) | **no** — `phy_type==7` gate | init |
| `wlc_bmac_set_chanspec` | `0x67a3a` | 0 | **no** — `phy_type==0xb` skip (`0x67a25`) | channel |
| `wlc_phy_init` | `0xbad44` | **1** (`mov esi,1`) | **no** — AC `[phy+0x28]==0` early return `0xbac76` | band init |
| `wlc_bmac_radio_hw` | `0x63ec8` | **1** (`mov esi,1` `0x63ebf`) | yes (the API) | radio enable |
| `wlc_bmac_radio_hw` | `0x63eec` | 0 | yes | radio disable |
| `wlc_coredisable` | `0x637c0` | 0 | n/a | teardown |
| `wlc_phy_periodic_cal_lpphy` | `0x110160` | – | no (lpphy) | periodic |

**First real on=1 site: `wlc_bmac_radio_hw @0x63ec8`** (arg proven: `r13b=sil`
`on`, tested `0x63e58`; `esi=1` `0x63ebf`). Chain:
`wlc_bmac_radio_hw(on=1) → si_pmu_radio_enable → wlc_phy_anacore(1) →
wlc_phy_switch_radio(1) → wlc_phy_switch_radio_acphy(1) → ON branch`.

**In-blob caller of `wlc_bmac_radio_hw`: none.** It is `FUNC LOCAL`, has no
relocation (`readelf -r`), no `re` caller (`calls` table empty for `0x63e2a`),
and no Ghidra reference — three independent oracles agree. It is the
hardware-layer radio on/off API invoked from outside `wlc_hybrid.o_shipped`
(the same is true of its twin `wlc_bmac_minimal_radio_hw @0x60cb4`). So the
**in-blob continuous path reaches radio-OFF only**; the on=1 transition is
owned by that API.

`wlc_phy_init @0xbad44` (on=1) is not AC-reachable: `wlc_phy_init` returns at
`0xbac76` when `[phy+0x28]==0` (the null AC init callback), before `0xbad3c`.
`wlc_phy_cal_init @0x6834b` likewise tests `[phy+0x30]` (`0xb1b61`) — null for
AC.

## 2. Continuous vendor timeline (chronology from the call graph)

```
PROBE / ATTACH              wlc_attach -> wlc_bmac_attach (0x6984f)
  SPROM/board             :   rev11 SPROM, MAC, board flags (M2)
  PHY object attach       :   wlc_phy_attach(phy,0) 0xbe426
                              -> wlc_phy_attach_acphy (0xbec39, software/board)
                              -> wlc_phy_anacore(phy,1) (0xbed84, D11 0x3e6=0)
                              -> wlc_phy_switch_radio(phy,0) (0xbf113) OFF branch
  CP-A3                   :   end of wlc_phy_attach (radio OFF)        <== HERE
  DMA alloc              :   dma_attach (0x6a3d6.., after phy_attach)

UP / INIT                   wlc_init (0x3c46f) -> wlc_bmac_init (0x6828a)
  chanspec/band          :   wlc_setxband + wlc_phy_chanspec_radio_set 0x6833b
  cal_init (AC no-op)    :   wlc_phy_cal_init 0x6834b
  ucode prep/upload      :   sub_607b0 0x684c4, sub_60c2f 0x684f8,
                             wlc_bmac_wowlucode_start 0x68500          [D2A]
  common initvals        :   sub_60f67 0x6888a + MHF                 [D2B]
  D11 tail               :   sub_67efd 0x68bab                       [D3A1]
  DMA/pll/NVRAM tail      :   macphyclk_set 0x690b1, sub_62b79 0x69461 [D3A0]
  switch_macfreq         :   0x695cb
  band init / bsinitvals :   sub_6656c 0x695d8                       [D3B]
                             (wlc_phy_init inside is a no-op for AC)

RADIO ENABLE (later, external API)
  wlc_bmac_radio_hw(on=1) -> si_pmu_radio_enable -> wlc_phy_anacore(1)
                          -> wlc_phy_switch_radio(1) -> AC ON branch
  CP-O1                   :   ON branch returns (radio ON, PLL set, cal done)
```

**CP-A3 is temporally BEFORE D2/D3.** It is an earlier attach phase
(`wlc_bmac_attach`), while D2A/D2B/D3A/D3B run in `wlc_bmac_init`, a later
`wlc_init`/up phase. Radio-ON follows D3B.

Boundary classification: CP-A3 vs D2/D3 = `EARLIER ATTACH PHASE` (not a
continuation). D2A/D2B/D3A/D3B = `LATER UP/INIT PHASE`. Radio-ON = `LATER
UP/INIT PHASE`, after D3B.

## 3. Operation-count reconciliation (categories A–D)

Per-function op counts (`re phyops`, whole function):

| function | PHY | RADIO | TABLE |
| :--- | ---: | ---: | ---: |
| `wlc_phy_attach_acphy` | 11 (reads) | 0 | 0 |
| `wlc_phy_anacore` | 2 | 0 | 0 |
| `wlc_phy_switch_radio` | 6 | 0 | 0 |
| `wlc_phy_switch_radio_acphy` | 13 | 62 | 0 |
| `sub_8fb39` | 82 | 0 | 0 |
| `sub_9027d` | 11 | 0 | 0 |
| `sub_9fb72` | 20 | 30 | 0 |
| `sub_9591e` | 0 | 22 | 0 |
| `sub_a04c2` | 25 | 0 | 0 |
| `sub_a7089` | 16 | 187 | 3 |
| `sub_a4adc` | 86 | 3 | 5 |
| **A. whole family total** | **272** | **304** | **8** |

| category | PHY | RADIO | TABLE |
| :--- | ---: | ---: | ---: |
| **A. whole discovered AC family** | 272 | 304 | 8 |
| **B. actual initial attach `on=0`** | **12** | **8** | **0** |
| **C. first initial radio-ON `on=1`** | **242** | **296** | **8** |
| D. later calibration/channel/runtime | subset of C re-executed | — | — |

- B = `wlc_phy_anacore` (1 write) + `switch_radio_acphy` OFF branch (11 PHY +
  8 RADIO). `sub_9591e`/`sub_a4adc` excluded.
- C = `switch_radio_acphy` ON direct (2 PHY + 54 RADIO) + `sub_8fb39` (82 PHY)
  + `sub_9027d` (11 PHY) + `sub_9fb72` (20 PHY + 30 RADIO) + `sub_9591e`
  (22 RADIO) + `sub_a04c2` (25 PHY, `+0x32d`) + `sub_a7089` (16 PHY + 187
  RADIO + 3 TABLE, `+0x32d`) + `sub_a4adc` (86 PHY + 3 RADIO + 5 TABLE).
- The old "186 PHY / 301 RADIO" is between A and C and must not be quoted as
  the attach scope. **Attach = B.**

## 4. Radio-ON path (`wlc_phy_switch_radio_acphy` ON, `0xaa7a6..0xab412`)

72 ordered call sites plus sub-callbacks. Substages:

| stage | entry condition | content | completion |
| :--- | :--- | :--- | :--- |
| MAC quiesce | always | `wlapi_suspend_mac_and_wait 0xaa7bb` | synchronous |
| PHY/RF setup | always | `sub_8fb39` (82 PHY), `mod_radio_reg` block, `osl_delay`×, `read_radio_reg` | synchronous |
| PLL/LO | always | `sub_9027d` (11 PHY), `write_radio_reg 0xaab62..` (0x60c seq), reads, `sub_9fb72` (radio+2 tables), `phy_reg_mod/write`, delays | delays |
| radio cal | always | `sub_9591e` (22 radio; 3-iteration loop) | bounded poll `read_radio_reg()&0x10`, ≤100 iters (`0x95cff`) |
| PHY cal | `phy+0x32d != 0` | `sub_a04c2` (25 PHY) | synchronous |
| farrow/radio-set | `phy+0x32d != 0` | `sub_a7089` (`chanspec_radio_set 0xa739f`, `wlc_2069_rfpll_150khz 0xaa07a`, txpwrctrl×2, 187 radio, 3 TABLE) → `sub_a4adc` (farrow + recip) | synchronous |
| MAC resume | always | `wlapi_enable_mac 0xab409` | synchronous |

Runtime-derived inputs: ALP clock (`phy+0xc24`), phy_rev (`+0x164`), band
(`+0x17e`), chain (`+0x168`), `+0x16e`, `+0x32d`, and the coefficient arrays
passed into `sub_a4adc`.

## 5. Channel dependency

- A chanspec is programmed **before** radio-ON: `wlc_bmac_init @0x6833b`
  `wlc_phy_chanspec_radio_set(phy, chanspec)` (chanspec = `wlc_bmac_init`
  argument).
- The ON path re-applies it: `sub_a7089 @0xa739f wlc_phy_chanspec_radio_set`
  and programs the PLL (`wlc_2069_rfpll_150khz`). So **radio-ON is
  channel-dependent; a valid chanspec must exist before it.**
- Band selection: `wlc_setxband` (`wlc_bmac_init @0x68328`), band derived from
  `chanspec & 0xc000`.
- Default chanspec provenance: `wlc_init` computes
  `r14d = wlc_default_chanspec(wlc->band, 1)` (`0x3c5b7`/`0x3c602`) and passes
  it to `wlc_bmac_init @0x3c6f1`. **Not a guessed channel.**
- First channel-dependent hardware write: `wlc_phy_chanspec_radio_set`
  (`0x6833b`).

## 6. Calibration

| call | where | class |
| :--- | :--- | :--- |
| `wlc_phy_cal_init @0x6834b` | `wlc_bmac_init` | **no-op for AC** (`+0x30` null) |
| `sub_9591e @0xab3d3` | ON path, unconditional | mandatory radio RX calibration |
| `wlc_phy_crs_min_pwr_cal_acphy` | inside `sub_a4adc` (`+0x32d`) | board/PHY cal |
| `wlc_phy_cal_perical`/`wlc_phy_cals_acphy` | not on this path | deferred/periodic |

Mandatory initial calibration = `sub_9591e`. Completion: bounded poll of a
`read_radio_reg()` for bit `0x10` (vendor read, `0x95cff`), ≤100 iterations,
then `bVar2 = success`.

## 7. Farrow / table compact representation

`sub_a4adc` (`FUN_001a4adc`): three tables `rx_farrow_tbl`,
`rx_farrow_tbl_rev3`, `rx_farrow_tbl_40_rev3`, each **123 entries × 12 B**
(stride `0x5C4`). Entry: `byte[0]` = selector `== (phy+0x17e)&0xff`,
`word[+4..+0xA]` = coefficients. Selection: `phy+0x164 == 3` → rev3 tables
(`40 MHz ALP` → `_40_rev3`, else `_rev3`), otherwise the base table.
Bandwidth dependency = the 40 MHz ALP-clock branch. Generated hardware
transactions = 12 farrow writes + cooperative coefficient writes; source
records = 123×3 + caller coefficient arrays.

`sub_9591e`: 3-iteration loop; static descriptor arrays `local_48={1,0,0}`,
`local_58={0,2,1}`, `local_70={0x14A,0x101,0x11A}` (40 MHz) /
`{0x22D,0xF0,0x10A}` (37.4 MHz). Source records = 9; generated transactions =
~22 radio ops.

## 8. PMU/OTP

`si_pmu_otp_power` stays outside the AC PHY bring-up path: its predicate is
`sih+0x1b & 0x10` (OTP present) and `!si_is_otp_disabled()`. Semantics remain
the proven conditional RMW (`old|(0x100|v)` / `old&~(0x100|v)`). **Not a D4
blocker for this board** (SPROM, not OTP).

## 9. First true operational checkpoint

| candidate | after | FOR | AGAINST |
| :--- | :--- | :--- | :--- |
| CP-O1 | `wlc_phy_switch_radio_acphy` ON returns (`0xab412`) | radio ON, PHY configured, PLL set, cal done, MAC resumed; no pending op | reached before MAC-layer finish; channel set separately |
| CP-O2 | `wlc_bmac_radio_hw` returns (`0x63f43`) | adds PMU radio enable + `si_pmu_radio_enable`; MAC-layer complete | same ops, one frame later |
| CP-O3 | after `wlc_bmac_set_chanspec` | channel explicitly reprogrammed | later runtime event |

**Chosen: CP-O2** (the `wlc_bmac_radio_hw` API boundary) as the first state with
radio ON, PHY initialized, PLL settled, valid pre-programmed chanspec, mandatory
calibration complete, no pending op, PSM running (D2A ucode), DMA initialized
(D3A0), MAC enabled. It is vendor-stable because every operation is synchronous
with bounded polls. Not hardware-proven here.

## 10. Observable postconditions

| read | width | expected | side effect | 0xffffffff | vendor uses |
| :--- | :--- | :--- | :--- | :--- | :--- |
| radio read in `sub_9591e` (`0x95cff`) | 16 | `& 0x10` set | none | meaningful (no bit) | yes (cal completion) |
| `osl_readl(D11 0x120)` (`0xba31d`) | 32 | MAC control snapshot | none | meaningful | yes (entry) |
| `read_radio_reg 0x413/0x414/0x415/0x416` | 16 | cal intermediates | none | meaningful | yes |

No proven postcondition is asserted as a D4 gate; the cal-completion bit is the
nearest vendor-compatible observable.

## 11. Device-lost safety

Future isolated path reads/writes: D11 `0x120` (`osl_readl`), D11 `0x3e0`,
`0x3e6`, the PHY/radio MMIO window, and the radio reads above. All are via the
bare `osl_read*`/`osl_write*`/`phy_reg_*` helpers; none observes `dev_lost`
today. Invariant must be wired at every such site before any BAR write:
monotonic latch on first `0xffffffff`, then **zero** further BAR writes; no
teardown write, no core reset, no free of DMA-referenced memory unless engines
were verified stopped pre-loss.

## 12. Tool cross-check

| claim | re | Ghidra | objdump | agree |
| :--- | :--- | :--- | :--- | :--- |
| `sub_9591e`/`sub_a4adc` radio-ON only | yes | yes | yes | ✓ |
| `wlc_bmac_radio_hw` has no in-blob caller | yes | yes | yes | ✓ |
| `wlc_phy_init` on=1 unreachable for AC | yes | yes | yes | ✓ |
| `wlc_bmac_radio_hw` LO C AL, no relocation | – | no refs | no refs | ✓ |

No disagreements. The D4B overestimate was a whole-function/path bug, now
corrected.

## Final answers

- **CP-A3 TEMPORAL POSITION RELATIVE TO D2/D3?** **Before** — attach/probe
  phase; D2A/D2B/D3A/D3B are later in `wlc_bmac_init`.
- **FIRST REAL `wlc_phy_switch_radio(...,1)` CALLER?** `wlc_bmac_radio_hw`
  (`0x63ec8`).
- **FULL CALL CHAIN?** `wlc_bmac_radio_hw(1) → si_pmu_radio_enable →
  wlc_phy_anacore(1) → wlc_phy_switch_radio(1) → switch_radio_acphy(1)` →
  ON branch. No in-blob caller of `wlc_bmac_radio_hw` (hw-layer API).
- **ON ARGUMENT PROVEN?** Yes (`esi=1 @0x63ebf`, `r13b=sil`).
- **INITIAL ATTACH:** PHY **12**, RADIO **8**, table **0**.
- **FIRST RADIO-ON:** PHY **242**, RADIO **296**, table **8**, delays **8+**
  (direct) plus sub-call delays, polls (**sub_9591e** 100-bound + PLL waits).
- **SUB_A4ADC REACHED?** Yes on radio-ON when `phy+0x32d != 0`.
- **SUB_9591E REACHED?** Yes on radio-ON (unconditional).
- **EXACT TABLE/LOOP REPRESENTATIONS?** farrow 3×123×12 B + descriptor arrays
  (§7).
- **CHANNEL FIRST REQUIRED WHERE?** `wlc_bmac_init @0x6833b`; re-applied in ON
  path `@0xa739f`.
- **INITIAL CHANSPEC SOURCE?** `wlc_default_chanspec(wlc->band,1)`.
- **MANDATORY INITIAL CALIBRATION?** Yes — `sub_9591e` (radio RX); `wlc_phy_cal_init`
  is a no-op for AC.
- **FIRST CALIBRATION FUNCTION?** `sub_9591e` (`0xab3d3`).
- **CALIBRATION COMPLETION CONDITION?** bounded `read_radio_reg() & 0x10`, ≤100
  iters.
- **FIRST TRUE OPERATIONAL CHECKPOINT?** **CP-O2** (after `wlc_bmac_radio_hw`).
- **WHY VENDOR-STABLE?** synchronous, bounded polls, radio ON, PLL set, cal
  done, chanspec valid, MAC enabled.
- **SAFE OBSERVABLE POSTCONDITIONS?** cal-completion radio read; D11 `0x120`
  snapshot (proposed, not proven).
- **PSM/MAC/DMA STATE?** PSM running (D2A), MAC enabled, DMA initialized (D3A0).
- **UNKNOWN EXECUTED VALUES REMAINING?** None raw; ON-path values are
  table/static/computed, several runtime-derived (ALP clock, phy_rev, +0x16e,
  +0x32d, coeff arrays).
- **RE/GHIDRA DISAGREEMENTS?** None.
- **MINIMUM NEXT IMPLEMENTATION SCOPE?** the radio-ON branch + sub-callbacks
  (B→C delta), gated on a real on=1 entry and a programmed chanspec.
- **D4 IMPLEMENTATION GO?** **NO** (radio-ON entry is an external hw-layer API;
  runtime-derived values not yet scoped).
- **HARDWARE TEST GO = NO.**

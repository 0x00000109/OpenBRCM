# M3.4D4D — PHASE 7: operational checkpoint re-evaluation

Status: **ANALYSIS ONLY**. Re-evaluated after the v5 tooling fixes (`f5d03da`)
and the Phase 3/4/5 closures.

## Candidate checkpoints

| checkpoint | location | verdict | why |
|---|---|---|---|
| CP-D4.1 | after `wlc_phy_init` returns | **WEAK** | `pi+0x16e` PLL selector unproven; dev_lost unwired (the `wlc_bmac_init` ops-table indirects are now resolved) |
| CP-F | after `wlc_phy_cal_perical(6)` returns | **WEAK (best)** | calibration is synchronous, but cal table/radio values are runtime and dev_lost unwired |
| CP-O2 | `wlc_bmac_radio_hw` return | **INVALID** | RPC-only, not executed on the vendor Linux attach path (D4 lifecycle) |
| CP-A3 | after `wlc_phy_attach` returns | **INVALID** | attach is radio-OFF; not an operational state |

**No STRONG checkpoint exists.**

## STRONG-checkpoint criteria

| criterion | satisfied? | evidence |
|---|---|---|
| all executed write values proven | **NO** | `pi+0x16e` = radio-rev (hardware, from reg `0x3da`); `pi+0x20+0xa7` = runtime; `(*(pi+0x138))+0x8be/0x8bf` now **RESOLVED** constants `0x19`/`0x1a` (`docs/m34d4/pi_8bf_provenance.md`) |
| all reachable indirect calls resolved | **PARTIAL** | `wlc_bmac_init 0x6923d`→`sub_f897` (`dma64_rxinit`), `0x6924a`→`sub_f14d` (`dma64_rxfill`) now EXACT via `dma64proc`; other indirection not re-audited |
| radio/PLL state understood | **PARTIAL** | both PLL sequences and completion predicates recovered; selector value unknown |
| initial chanspec understood | **PARTIAL** | `wlc_default_chanspec` path recovered; locale/SPROM inputs runtime |
| calibration complete (synchronous, observable) | **PARTIAL** | `wlc_phy_cals_acphy` synchronous; write values runtime |
| no pending async work | **UNKNOWN** | vendor PHY runs timers/upcalls; not bounded for the D4 prefix |
| MAC/PSM state understood | **PARTIAL** | suspend/enable MAC via `wlapi_*`; full state machine not proven |
| DMA state understood | **YES** | M3.4D3A0/D3A1 DMA lifecycle hardware-proven |
| dev_lost coverage complete | **NO** | the D3B→cal path's new D11/PHY/radio/table accesses are not observed |
| deterministic observable postconditions | **NO** | no proven deterministic gate at CP-D4.1/CP-F |

## Verdict

- **`D4 IMPLEMENTATION GO = NO`**
- **`HARDWARE TEST GO = NO`**

Blocking items: (1) `pi+0x16e` radio-revision selector is hardware-derived;
(2) the dev_lost monotonic latch is not wired to the new path; (3) no
deterministic postcondition. These are prerequisites for any D4 bring-up
code; none may be guessed. (The former blocker "two reachable vtable
indirects in `wlc_bmac_init` remain UNRESOLVED" is **resolved**:
`di[0]->ops = dma64proc`, `+0xa0`=`sub_f897`, `+0xd8`=`sub_f14d`.)

## Superseded

- D4D's claim that `pi+0x16e` writers are `{0,1,2}` (they were `cmp` reads).
- D4D's claim of two UNRESOLVED reachable vtable indirects in `wlc_bmac_init`
  (resolved via `docs/m34d4/indirect_ops_table_resolution.md`).
- D4D's classification of `pi+0x8be`/`pi+0x8bf` as `COMPUTED_RUNTIME`/`UNKNOWN`
  (resolved to constants `0x19`/`0x1a` via `docs/m34d4/pi_8bf_provenance.md`).
- D4D's claim that the first radio write is `mod_radio_reg(0x80b,0x80,0x80)`
  — that write is inside the `pi+0x16e==1` sequence and executes only if the
  radio revision selects sequence A.

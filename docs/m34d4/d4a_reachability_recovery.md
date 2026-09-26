# M3.4D4A — actual BCM4352 rev42 PHY-init reachability (post-D3B)

**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.
Baseline `re` v4 (`0c7cf875…`), `re.db` `44dae60d…`, prior commit `00f22d8`.
Machine-readable: [`wlc_phy_init_bcm4352_reachable.json`](wlc_phy_init_bcm4352_reachable.json).

**Headline:** `wlc_phy_init` is a **no-op for BCM4352 AC** (`[phy+0x28]==0`).
The real vendor-executed PHY/radio work that follows D3B is
**`wlc_phy_cal_perical → wlc_phy_cals_acphy`**, called from **`wlc_init` after
`wlc_bmac_init`**. The standalone "D4 prefix through `wlc_phy_init`" boundary
is invalid.

## 1. RPC classification of `wlc_bmac_radio_hw`

**`OUT_OF_BAND_RPC_NOT_PART_OF_LOCAL_INIT`.** `WLRPC_WLC_BMAC_RADIO_HW_ID`
exists in all corpora; the function has no in-object caller, no relocation, and
is a unique `LOCAL` symbol. It is an RPC/offload target, not local operational
code for BCM4352 init. Its 242 PHY / 296 radio writes are **out of scope for
D4** (not executed in the local path). Bounded pass; the RPC framework was not
reverse-engineered further.

## 2. Real D4 entry

The proven vendor flow is `D3B (sub_6656c) → return → wlc_phy_init`. Path
specialization shows:

```
sub_6656c 0x669d0: rdi = [[wlc+0xE8]+0x28] = pi
sub_6656c 0x669df: call wlc_phy_init(pi, chanspec)
wlc_phy_init 0xbabf5:
  0xbac05  if [pi+0x186]!=0 return
  0xbac19  [pi+0x17E] = chanspec
  0xbac20  [pi+0x186] = 1 ; [pi+0x18F] = 0
  0xbac31  call wlc_phy_chanspec_shm_set       <-- SHM only
  0xbac44  osl_readl(D11 0x120)
  0xbac6f  r13 = [pi+0x28] ; test ; je 0xbaecE <-- AC returns here
```

`[pi+0x28]` is **NULL for AC** (`wlc_phy_attach_acphy 0xa3001`), so the body
(`wlc_phy_anacore`, `call r13`) is skipped. `wlc_phy_init` only writes SHM.

**The real entry is `wlc_phy_cal_perical`**:

```
wlc_init 0x3c6f1  call wlc_bmac_init        (D2A/D2B/D3A/D3B inside)
wlc_init 0x3ca27  call wlc_set_home_chanspec
wlc_init 0x3cde0  call wlc_phy_cal_perical(pi, 6)
wlc_init callers: wl_init 0x184a60 (wl.ko); wl_init <- wlc_up 0x3acce
```

Entry state (separated):

| item | value | class |
| :--- | :--- | :--- |
| caller PC / callee | `0x3cde0` / `wlc_phy_cal_perical 0xb57d3` | PROVEN |
| pi | `[[wlc+0x40]+0x10]` (band->pi) | PROVEN RUNTIME-DERIVED |
| phytype | `0xb` (AC) | PROVEN RUNTIME-DERIVED (`[pi+0x160]` ← D11 `0x3e0`[11:8]) |
| phyrev | runtime (`[pi+0x164]` ← D11 `0x3e0`&0xf) | PROVEN RUNTIME-DERIVED |
| chipid / chiprev | `0x4352` / `3` | PROVEN CONSTANT |
| band / chanspec | runtime / `wlc_default_chanspec(band,1)` | PROVEN RUNTIME-DERIVED |
| `[pi+0xf89]` (perical state) | `2` (`attach_acphy 0xa1a2b`) | PROVEN CONSTANT |
| PSM_RUN | true (D2A) | PROVEN RUNTIME-DERIVED |
| DMA | initialized (D3A0) | PROVEN RUNTIME-DERIVED |
| MACCONTROL / EN_MAC | UNKNOWN | UNKNOWN |
| radio | UNKNOWN (RPC/out-of-band) | UNKNOWN |

No value is inherited from the withdrawn CP-O2 analysis.

## 3. Path-specialized `wlc_phy_cal_perical`

`re switch`/Ghidra branch trace (machine-readable in the JSON): only
`phy_type ∈ {4,7,0xb}` proceed; `[pi+0xf89]` must be nonzero and ≠3 (it is `2`);
reason ∈ [2..11] indexes a jump table; `reason=6` → **case 4/5/6** →
`joined_r0x001b5908` → `phy_type==0xb` → `wlc_phy_cals_acphy`. The
`phy_type==4`/`==7` siblings (`wlc_phy_cal_perical_nphy_run`, `wlc_phy_cals_htphy`)
are pruned.

## 4. Reachable call graph (BCM4352 AC)

`wlc_phy_cal_perical` → `wlc_phy_tempsense_acphy` (if `[pi+0xf9c]!=0`),
`wlc_phy_cals_acphy`.

`wlc_phy_cals_acphy (0xb13b5)` ordered: `wlc_phy_rxcore_setstate_acphy` →
`wlc_phy_noise_sample_request_crsmincal` → `wlc_phy_cal_perical_mphase_restart`
→ `sub_925cc` → `sub_affa9` (6 PHY) → `sub_b1227` → `sub_abc76`×2 (107 PHY +
1 radio read) → `sub_addfa` (104 PHY) → `phy_reg_* 0x19e` →
`wlc_phy_table_write_acphy` (table `0xc`) → `wlc_phy_scanroam_cache_cal_acphy`
→ `sub_9311b` (7 RADIO) → `sub_93e47` (radio read) → … →
`wlc_phy_txpwrctrl_enable_acphy` → `wlc_phyreg_exit` → `wlapi_enable_mac` →
`wlc_phy_rxcore_setstate_acphy`.

Dead/sibling-PHY paths excluded. Full record in the JSON.

## 5. Indirect targets

- `[pi+0x28]` (init callback): **INTENTIONALLY_NULL** — zeroed
  `wlc_phy_attach_acphy 0xa3001`; no other AC writer. This is why
  `wlc_phy_init` is a no-op.
- `[pi+0x118]` (anacore callback): **INTENTIONALLY_NULL** — zeroed
  `0xa3009`; `wlc_phy_anacore` uses the D11 `0x3e6` fallback.
- **Unresolved indirect calls on the AC path: 0.** No candidates remain.

## 6. First real PHY write (D4 path)

`wlc_phy_rxcore_setstate_acphy`, `phy_reg_mod(0x160, 0x7, mask=*(pi+0xa7))`
at `0x97d50`, reached from `wlc_phy_cals_acphy @0xb1428`. Provenance
`COMPUTED_RUNTIME` (mask `pi+0xa7`). This is not the old attach-time anacore.

## 7. First real radio write (D4 path)

`sub_9311b`, `mod_radio_reg(0x8e5, 0x4000)` at `0x9315d`, reached from
`wlc_phy_cals_acphy @0xb1749`. `STATIC`. No radio write occurs before it on the
path.

## 8. Table operations

- `wlc_phy_cals_acphy` → `wlc_phy_table_write_acphy` table `0xc` (2 ops).
- `sub_abc76`/`sub_addfa` use acphy tables with runtime-computed indices
  (`RUNTIME_COMPUTED`).

## 9. Real radio initialization

No `wlc_phy_switch_radio`/`wlc_phy_switch_radio_acphy` on the local path. The
radio programming is `sub_9311b` (radio RMWs) plus table writes inside
`wlc_phy_cals_acphy`. Symbol existence ≠ execution; the switch_radio AC path is
RPC-only.

## 10. Calibration

First executed calibration = **`wlc_phy_cals_acphy`**, caller
`wlc_phy_cal_perical` (case 4/5/6), entry condition `phy_type==0xb ∧
[pi+0xf89]==2`, synchronous, completion = function return
(`wlc_phy_cal_perical_mphase_reset` called inside). No poll/timeout at this
level. It is followed later by periodic calibration via `wlc_phy_watchdog`.

## 11. Checkpoints (real path)

- **CP-D4.0** entry to `wlc_phy_cal_perical`: PSM running, DMA initialized,
  radio possibly OFF, no pending op — but the vendor continues immediately into
  calibration.
- **CP-D4.F** return of `wlc_phy_cal_perical`: radio/PLL programmed, MAC
  re-enabled by `wlapi_enable_mac`.
- Neither is proven vendor-stable without hardware. No earlier safe checkpoint.

## 12. D3B crash hypotheses

| hypothesis | verdict | basis |
| :--- | :--- | :--- |
| H1 bsinitvals consumed by `wlc_phy_init` | **REJECTED** | `wlc_phy_init` is a no-op for AC |
| H2 a bsinitvals SHM value triggers PSM while PHY uninit | **PLAUSIBLE/UNKNOWN** | no per-value evidence |
| H3 contract requires immediate continuation | **WEAKENED** | continuation is calibration, not bsinitvals-dependent |
| H4 unrelated device loss | **PLAUSIBLE** | all-ones ~4.84 s later + data-fabric sync flood |

No causation claimed.

## 13. Smallest next vendor-faithful unit

`D3B (full sub_6656c) + remainder of wlc_bmac_init + wlc_set_home_chanspec +
wlc_phy_cal_perical` (the AC calibration), in one continuous unit. The
previously proposed "D3B + minimal D4 prefix through `wlc_phy_init`" boundary is
invalid because `wlc_phy_init` performs no PHY work for AC. Not implemented.

## 14. Tool quality

`re` queries: `card`, `fn --asm`, `phyops`, `field-writers`, `switch`,
`indirect`. Manual objdump: **0**. Unresolved indirect calls: before 2, after
**0**. Two gaps recorded: `re` cannot represent RPC/external dispatch edges
(L-C1), and `re switch` does not print jump-table targets (Ghidra needed to map
`reason=6`).

## Final answers

- RPC RADIO_HW LOCAL TO BCM4352 INIT? **No.**
- RPC PATH CLASSIFICATION? `OUT_OF_BAND_RPC_NOT_PART_OF_LOCAL_INIT`.
- REAL D4 ENTRY PROVEN? **Yes** — `wlc_phy_cal_perical @0x3cde0` (not `wlc_phy_init`).
- PHYTYPE? `0xb` (AC), runtime-derived. PHYREV? runtime (`[pi+0x164]`).
- INITIAL BAND / CHANSPEC? runtime; `wlc_default_chanspec(band,1)`.
- REACHABLE WLC_PHY_INIT FUNCTIONS? `wlc_phy_init` prefix only = `wlc_phy_chanspec_shm_set`; real work in `wlc_phy_cal_perical → wlc_phy_cals_acphy` subtree.
- REACHABLE INDIRECT CALLS? none. UNRESOLVED INDIRECT CALLS? **0**.
- FIRST REAL PHY WRITE? `wlc_phy_rxcore_setstate_acphy phy_reg_mod(0x160,0x7)` `@0x97d50`.
- FIRST REAL RADIO WRITE? `sub_9311b mod_radio_reg(0x8e5,0x4000)` `@0x9315d`.
- STATIC PHY TABLE OPS? table `0xc` (2). STATIC RADIO TABLE OPS? none proven. RUNTIME-DERIVED OPS? masks/indices from `pi+0xa5/a7/8be/8bf`, phyrev.
- REAL RADIO INIT FUNCTION? `sub_9311b` (+`sub_abc76` radio read).
- FIRST REAL PLL/SYNTH OP? inside the cal radio sub-calls — **UNKNOWN exact**.
- FIRST REAL CALIBRATION? `wlc_phy_cals_acphy`. COMPLETION CONDITION? synchronous return (mphase reset inside).
- CP-D4.F VENDOR-STABLE? **UNKNOWN**. EARLIER SAFE CHECKPOINT? none established.
- D3B STANDALONE INVALID? **Yes.** D3B→D4 CONTINUATION REQUIRED? Yes (into `wlc_phy_cal_perical`, not `wlc_phy_init`).
- D3B CRASH H1/H2/H3/H4? H1 REJECTED, H2 PLAUSIBLE/UNKNOWN, H3 WEAKENED, H4 PLAUSIBLE.
- SMALLEST NEXT VENDOR-FAITHFUL UNIT? `D3B + wlc_bmac_init remainder + wlc_set_home_chanspec + wlc_phy_cal_perical`.
- D4 IMPLEMENTATION GO? **NO** (executed cal values/tables and the PLL/synth first op not fully resolved).
- HARDWARE TEST GO? **NO — STOP.**

> **SUPERSEDED (2026-09):** the `186 PHY / 301 RADIO` counts and the
> "executed UNKNOWN ⇒ blocker" statement in this document are **radio-ON /
> whole-family inclusive** and **wrong for the on=0 attach path**. The corrected
> result (attach = 12 PHY + 8 RADIO; sub_a4adc/sub_9591e not reached) is in
> [`d4b_value_provenance_closure.md`](d4b_value_provenance_closure.md) and
> [`d4c_radio_on_transition.md`](d4c_radio_on_transition.md). See
> [`../artifact-ledger.md`](../artifact-ledger.md) §5. Kept for history.

# M3.4D4B resume — actual BCM4352 rev42 AC-PHY initialization lineage

**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.
Baseline: `re` v4 (`0c7cf875…`), `re.db` `44dae60d…`, Ghidra augmentation
([`ghidra_augmentation.md`](ghidra_augmentation.md)), Phase-0 fixes
([`re_false_positive_fixes.md`](re_false_positive_fixes.md)), D3A/D3B hardware
evidence. This document does not restart the analysis; it refines the D4B
conclusions with the corrected tooling.

## Phase 1 — what `wlc_phy_attach_acphy` is

Classification of its behaviour (evidence: `re` counts + Ghidra decompile):

| bucket | evidence |
| :--- | :--- |
| object allocation/setup | `osl_malloc`×1, `osl_memset`×9 |
| structure initialization | 238 field writes |
| callback installation | **1** (`+0xF8 = wlc_phy_btc_adjust_acphy`) |
| board/NVRAM state | 99 board calls (`phy_getintvar`×34, `phy_getvar_fabid`×31, `getintvararray`×24, `phy_getintvararray`×4, srom×3, otp×2, `wlc_phy_txpwr_srom11_read`×1) |
| static tables | none consumed from `reg_tables` |
| direct hardware access | **0 MMIO writes**, 0 reads |
| indirect PHY access | 11 `phy_reg_read` (capability) |
| radio access | 0 |
| calibration setup | 0 |
| runtime init dispatch | 0 |

**Role: software object construction + capability discovery + board/NVRAM/OTP
parsing + one callback install. It performs no AC PHY hardware programming.**

## Phase 2 — AC function-pointer/vector table

Full record: [`acphy_function_table.json`](acphy_function_table.json).
Scheme `INSTALLED_EXACT | INTENTIONALLY_NULL | UNRESOLVED`.

**Installed exact (1):**

| off | target | installer | instruction | reloc | consumer |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `+0xF8` | `wlc_phy_btc_adjust_acphy` | `wlc_phy_attach_acphy` | `movq $imm,0xf8(%rbx)` @`0xa3052` | `R_X86_64_32S` @`0xa3059` | `wlc_phy_watchdog` @`0xbc770` |

**Intentionally null (10)** — proven zero by construction (the PHY object is
`osl_malloc`+`osl_memset` zeroed) and/or an explicit `movq $0,disp(%rbx)`:

| off | name | zero site | consumer (not taken for AC) |
| :--- | :--- | :--- | :--- |
| `+0x28` | init | `0xa3001` | `wlc_phy_init` `0xbad4c` (`je 0xbaecE`) |
| `+0x30` | cal_init | `0xa3009` | `wlc_phy_cal_init` `0xb1bc9` (`je`) |
| `+0x38` | chanspec_set | `0xa3016` | — |
| `+0x40` | txpower_recalc | `0xa301e` | — |
| `+0xC0` | detach | `0xa3031` | — |
| `+0xC8` | ? | `0xa3047` | — |
| `+0xD0` | ? | `0xa303c` | — |
| `+0x100` | ? | `0xa3026` | — |
| `+0x110` | radio cb | zero-fill, no writer | `wlc_phy_switch_radio` `0xba532` (AC takes explicit `0xba3a1`) |
| `+0x118` | anacore cb | zero-fill, no writer | `wlc_phy_anacore` `0xbabc7` → D11 `0x3e6` fallback |

**UNRESOLVED: 0.** Every absent installer is construction-proven zero (never
inferred from a missing writer alone).

## Phase 3 — consumers of the AC vector

```
+0xF8  install wlc_phy_attach_acphy 0xa3052
         -> call [phy+0xF8]  wlc_phy_watchdog 0xbc770
+0x28  zeroed 0xa3001            -> call r13  wlc_phy_init 0xbad4c   (not taken)
+0x30  zeroed 0xa3009            -> call rax  wlc_phy_cal_init 0xb1bc9 (not taken)
+0x110 zero-fill                 -> call rax  wlc_phy_switch_radio 0xba532 (not taken for AC)
+0x118 zero-fill                 -> call rax  wlc_phy_anacore 0xbabc7 (fallback D11 0x3e6)
```

The only live callback is `btc_adjust` (periodic, watchdog), **not** an init
entry. All init/cal/chanspec vector slots are null for AC.

## Phase 4 — real AC hardware-init entry

In vendor runtime order at **probe**:

```
wlc_attach -> wlc_bmac_attach -> wlc_phy_attach (0xbe426)
    0xbec39  wlc_phy_attach_acphy     (object/board/caps; no writes)
    0xbed84  wlc_phy_anacore          FIRST PHY write (D11 0x3e6 fallback)
    0xbedb1+ direct osl_writew(0x3d8/0x3da/0x3f6/0x3fa/0x3f8)
    0xbf113  wlc_phy_switch_radio(phy,0) -> 0xba3a1 wlc_phy_switch_radio_acphy
                                        SUBSTANTIAL block (radio + PHY + tables)
```

**Earliest function performing substantial BCM4352 AC PHY programming:
`wlc_phy_switch_radio_acphy` (`0xaa782`)**, invoked from `wlc_phy_attach`
through `wlc_phy_switch_radio`. Strictly, the first hardware *op* is
`wlc_phy_anacore` (`0xbabee`, D11 `0x3e6`), but it is one register; the
substantial programming is the `wlc_phy_switch_radio_acphy` block. This is
**not** `wlc_phy_init` (null callback) and **not** `wlc_phy_attach_acphy`
itself.

Firsts on that path:

| first | function | site | target |
| :--- | :--- | :--- | :--- |
| PHY write | `wlc_phy_anacore` | `0xbabee` | D11 `0x3e6` (fallback, `0xf4`) |
| PHY indirect write | `sub_8fb39` | `0x8fb54` | `phy_reg_mod(0x830,0x7,mask 0x3)` |
| PHY table load | `sub_9fb72` | `0x9fc30+` | `wlc_phy_table_write_acphy` |
| RADIO write | `wlc_phy_switch_radio_acphy` | `0xaa80f` (RMW) / `0xaab62` (`0x60c<-0x9e`) | radio window |
| PLL/synth | `wlc_phy_switch_radio_acphy` | `0xaab62` | `0x60c` sequence |
| delay | `sub_9fb72`/`sub_9591e`/`sub_a7089`/`switch_radio_acphy` | 17 × `osl_delay` | — |
| MAC suspend/resume | `switch_radio_acphy` | `0xaa7bb` suspend / `0xab409` enable | `MACINTSTATUS` bit0 |
| calibration | — | — | none on this path |

## Phase 5 — actual vendor order beyond D3B

The D3B STOP (before `wlc_phy_init`) is invalid; the vendor continues:

```
... bsinitvals last op (sub_60f67 @0x669bd)
 -> 0x669df wlc_phy_init            ; AC: [phy+0x28]==0 -> je 0xbaecE, RETURNS (no work)
 -> 0x669e7 sub_62684 (txant)
 -> 0x669fa wlc_bmac_set_cwmin
 -> 0x66a0d wlc_bmac_set_cwmax
 -> 0x66a2d sub_62716 ; 0x66a45/0x66a5d write_shm ; 0x66a65 sub_62403
 -> 0x66a8e write_shm  ; 0x66a96 sub_627c9 ; 0x66ac9 wlc_bmac_mhf
 -> 0x66ad1 sub_6106b  ; 0x66b28 sub_62766 ; 0x66b30 set_extlna_pwrsave_shmem
 -> return 0x66b3a
```

For rev42 AC this continuation is **band/MAC SHM state**, not PHY hardware
programming (the PHY was already programmed at attach). There is no artificial
STOP needed; the honest boundary between "PHY init" and "band init" is the
attach/band-init split.

## Phase 6 — AC initial-path operation extraction (path counts, not whole-blob)

Path set: `wlc_phy_attach`, `wlc_phy_attach_acphy`, `wlc_phy_anacore`,
`wlc_phy_switch_radio`, `wlc_phy_switch_radio_acphy` + direct sub-callbacks
`sub_8fb39`, `sub_9027d`, `sub_9591e`, `sub_9fb72`, `sub_a04c2`, `sub_a7089`
(partial artifacts [`acphy_phy_ops.json`](acphy_phy_ops.json),
[`acphy_radio_ops.json`](acphy_radio_ops.json)).

| class | path count |
| :--- | ---: |
| PHY writes/RMW/reads (indirect) | 186 |
| RADIO writes/RMW/reads | 301 |
| PHY_TABLE ops | 3 |
| direct D11 writes (`wlc_phy_attach`) | 8 writes + 10 reads |
| `osl_delay` calls | 17 |
| bounded polls | `wlapi_suspend_mac_and_wait` (+ acphy internal waits) |
| SHM operations | present in `sub_9fb72`/`sub_a7089` (acphy SHM) |

(Whole-blob totals 8157/3414/1610 are **not** path counts.)

## Phase 7 — table / opcode streams

- No initvals-family `reg_tables` are consumed on the AC path
  ([`acphy_tables.json`](acphy_tables.json)).
- AC table programming is argument-passed tables inside
  `wlc_phy_switch_radio_acphy`'s sub-callbacks, i.e. **RUNTIME-DERIVED**
  (values partly `*(a0+off)`), with 3 recovered `wlc_phy_table_write_acphy`
  ops. The radio sequence is literal (not a table/opcode stream).
- Classification: STATIC = the literal radio register/value pairs and
  `phy_reg_mod(0x830,0x7,0x3)`; CHANNEL_DATA / HARDWARE_DERIVED =
  sub-callback values; **UNKNOWN** = `sub_a4adc`/`sub_9591e` `val=?`
  (runtime PHY-state reads). **An executed UNKNOWN remains ⇒ blocker.**

## Phase 8 — radio initialization

`wlc_phy_switch_radio_acphy` is a single long literal sequence (62 ops in the
function plus `sub_a4adc`/`sub_a7089`/`sub_9591e` sub-sequences). It is invoked
from `wlc_phy_attach` with `on=0` (radio **off** during attach). It stages:
`wlapi_suspend_mac_and_wait` → `[phy+0x830]` RMW (`sub_8fb39`) → `0x80b`
bitfield RMWs → `sub_9027d` → `0x60c` PLL/LO writes → `sub_9fb72` (radio+tables)
→ `0x16b/0x175` PHY RMW/write → more radio → `sub_9591e`/`sub_a04c2`/`sub_a7089`
→ `wlapi_enable_mac` → `0x800`-range PHY writes → more radio RMW → return.
So: **radio power-off/init at attach; the on-state / band-channel programming
is the band-init + later channel path.** All BCM4352-relevant radio ops are in
this function and its direct sub-callbacks.

## Phase 9 — calibration boundary

`wlc_phy_cals_acphy` is referenced only from `wlc_phy_cal_perical` and
`wlc_phy_init_test_acphy` — periodic/test, **not** attach or band init. The
`+0x30` cal-init callback is null for AC. **No calibration operation is
mandatory in the initial bring-up path**; periodic calibration (and PAPD/ACI
cal) can remain outside the first OpenBRCM PHY milestone.

## Phase 10 — real stable checkpoint

| id | after | MAC/PSM | PHY/radio | async | observable postcondition | class |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| CP-A0 | `wlc_phy_attach_acphy` returns `0xbec3e` | not up | untouched | OTP/SROM reads only | phy object + 11 cap reads | POSSIBLE (pre-HW) |
| **CP-A3** | **`wlc_phy_attach` returns `0xbf3d6`** | not up | programmed (radio seq + PHY window) | none | PHY object, radio-window state, `0x3d8`/`0x3f6` writes, MAC resumed by `0xab409` | **STRONG (proposed)** |
| CP-B | `sub_6656c` returns `0x66b3a` | attach/up context | already programmed | DMA live | band SHM/`MACCONTROL` | STRONG-LATE, but post-DMA (D3B crash risk) |

**Earliest true vendor-stable state after mandatory initial AC hardware setup =
CP-A3** (end of `wlc_phy_attach` at probe). Caveat: a *live, hardware-proven*
postcondition for the AC write set has **not** yet been measured, so CP-A3 is
`STRONG (proposed)`, not `PROVEN`. CP-B is later and sits after DMA (the D3B
instability), so it is not the D4 target.

## Phase 11 — `dev_lost` coverage through D4

Forward requirement for the proposed D4 sequence (`wlc_phy_anacore` → direct
`0x3d8` writes → `wlc_phy_switch_radio`→`..._acphy`):

- every D11/BCMA BAR read that can observe `0xffffffff` must first latch
  `dev_lost`; after a latch, **no** BAR write is attempted;
- use the existing monotonic `ob_dev_lost_latch` / `ob_dev_lost_observe32` and
  the `ob_d3a0_*` guards (already `IMPLEMENTED`/`STATIC TESTED`, see
  `docs/m34d3b/d3b_crash_postmortem.md` Part A);
- `wlc_phy_switch_radio_acphy` already contains `wlapi_suspend_mac_and_wait`,
  which itself reads `MACCONTROL`/`MACINTSTATUS`; those reads must be observed
  for device loss before the radio/PHY writes that follow.

The proposed sequence is structurally coverable by the existing invariant; no
new mechanism is required, but the D4 implementation must call the observers on
the specific new read sites (audit required at implementation time).

## Phase 12 — minimum OpenBRCM D4 scope (to CP-A3)

| item | count |
| :--- | ---: |
| core functions | 5 |
| direct sub-callbacks | 6 |
| PHY ops | 186 |
| RADIO ops | 301 |
| PHY_TABLE ops | 3 |
| `osl_delay` | 17 |
| bounded polls | `wlapi_suspend_mac_and_wait` + acphy waits |
| board inputs | 99 board calls (SPROM/OTP/NVRAM) |
| channel inputs | 0 (attach is band/channel independent) |
| runtime-derived values | sub-callback `val=?` (UNKNOWN) |

Split: **framework/helper** (PHY/radio helpers, delays, MAC suspend/resume);
**BCM4352-specific data** (SPROM/OTP board values); **AC-PHY generic logic**
(the literal radio sequence + `phy_reg_mod(0x830,0x7,0x3)`); **board-specific
values** (txpower/srom11, OTP words). This is a clean-room plan, not a vendor
source translation.

## Phase 13 — tooling effectiveness / oracle per fact

| fact | oracle |
| :--- | :--- |
| initvals/bsinitvals shapes, sub_67efd, switch_macfreq, MAC/DMA offsets | RE |
| phy+0x28/0x30/0x38/0x40/0xC0/0xC8/0xD0/0x100 zeroing | RE |
| phy+0x118/+0x110 absent installer | BOTH (re address_taken + Ghidra refs) |
| phy+0xF8 `btc_adjust` install | ELF_RELOCATION (re + Ghidra ref + `objdump -r`) |
| AC dispatch (`switch_radio`→`..._acphy`), anacore null-check | BOTH |
| DMA bases `dev+0x200/0x220/0x240/0x260` (+rev>10) | GHIDRA |
| `dma64proc` vtable → `sub_f947`/`sub_f64a`/`sub_f5ef` | GHIDRA |
| `wlc_phy_cals_acphy` consumers | BOTH |

Manual disassembly uses: **1** (`objdump -r`/`-d` to settle the `0xa3052`
`re`/Ghidra representation conflict; recorded in
[`ghidra_augmentation.md`](ghidra_augmentation.md) §C). No new reusable tooling
gap: the relocation and confidence defects were fixed in Phase 0.

## Final answers (see the response)

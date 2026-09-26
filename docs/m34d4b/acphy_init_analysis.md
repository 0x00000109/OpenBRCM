> **SUPERSEDED (2026-09):** the `186 PHY` path count and the `CP-A3` "full AC
> init / STRONG (post-HW)" checkpoint here are **radio-ON inclusive** and
> **withdrawn**. CP-A3 is **NON-OPERATIONAL (radio OFF)**; the on=0 attach path
> is 12 PHY writes + 8 radio RMWs. Corrections:
> [`d4b_value_provenance_closure.md`](d4b_value_provenance_closure.md),
> [`d4c_radio_on_transition.md`](d4c_radio_on_transition.md). See
> [`../artifact-ledger.md`](../artifact-ledger.md) §5.

# M3.4D4B — BCM4352 rev42 AC-PHY initialization lineage

**Milestone:** recover the AC-PHY init lineage from `wlc_phy_attach_acphy` and
find the first true hardware-programming entry point.
**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no driver code, no candidate.
Tool-first: every fact below comes from the canonical `re` v3 / `re.db`
(schema v3) unless marked `[asm]`/manual.

Machine-readable artifacts: [`acphy_attach_callgraph.json`](acphy_attach_callgraph.json),
[`acphy_function_table.json`](acphy_function_table.json),
[`acphy_hw_init_flow.json`](acphy_hw_init_flow.json),
[`acphy_phy_ops.json`](acphy_phy_ops.json),
[`acphy_radio_ops.json`](acphy_radio_ops.json),
[`acphy_tables.json`](acphy_tables.json).

## 1. Tool baseline

| item | value |
| :--- | :--- |
| `re` sha256 | `9eaac7cf4903754b23aaa0cac39925fb34ba59fed2c734c3c7b244e06e05044f` |
| `re.db` sha256 | `b0c14bd326b8b4c9b988637d50bef4502641bfbfd8e8fd578efbcf8fc87f5089` |
| blob sha256 | `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743` |
| schema version | **3** (all v3 tables present) |
| functions / calls | 5095 / 51193 |
| mmio / imm sites | 1515 / 97992 |
| PHY / RADIO / PHY_TABLE ops | 8157 / 3414 / 1610 |
| `re regress` | **PASS** (initvals 610/113/497, bsinitvals 73/39/34, sub_67efd, switch_macfreq, DMA, MAC) |
| `scripts/re-bootstrap.sh` | PASS |

## 2. What `wlc_phy_attach_acphy` actually is

`wlc_phy_attach_acphy` (`0xa194f`, 6189 B, id 697): 1 caller (`wlc_phy_attach`
`0xbe426`), 136 call sites, 16 unique callees.

| metric | count |
| :--- | ---: |
| direct MMIO reads | 0 |
| **direct MMIO writes** | **0** |
| PHY indirect reads (`phy_reg_read`) | 11 |
| PHY indirect writes | 0 |
| RADIO reads/writes | 0 |
| table operations (initvals family) | 0 |
| structure-field writes | 238 |
| function-pointer installs | 1 (`wlc_phy_btc_adjust_acphy` @ `+0xF8`) |
| allocations (`osl_malloc`) | 1 |
| `osl_memset` | 9 |
| delays / polls | 0 / 0 |

The 73-node AC-reachable subgraph
([`acphy_attach_callgraph.json`](acphy_attach_callgraph.json)) is **28
SOFTWARE-ONLY, 20 BOARD-DATA, 10 PHY-MMIO** (the PHY-MMIO nodes are OTP/SROM
power control and capability reads; no radio, no table, no PHY writes).

**Classification: `A` (software object construction) + `C` (capability
discovery) + `D` (board/NVRAM parameter parsing)**, with a single callback
install. It is **not** `E`/`F` — the name is misleading. It performs **no AC PHY
hardware programming**.

## 3. The split: attach builds the object, the *caller* programs hardware

The generic `wlc_phy_attach` (`0xbe426`, 4034 B) is the hardware-init
continuation. It dispatches on phytype (`[phy+0x160]`, `0xbec0d`) to the family
attach, then continues:

```
0xbec39  call wlc_phy_attach_acphy     ; software object + caps + board (no writes)
0xbed84  call wlc_phy_anacore          ; FIRST AC PHY write
0xbedb1+ direct osl_writew(0x3d8/0x3da), 0x3f6/0x3fa/0x3f8  ; PHY/radio windows
0xbf113  call wlc_phy_switch_radio(phy, 0)  ; -> AC branch 0xba395 -> 0xba3a1
0xbf118+ ...
```

`wlc_phy_attach` is called from `wlc_bmac_attach` (`0x6984f`), itself called
from `wlc_attach` (`0x37d10`) — i.e. the whole AC PHY initialisation runs at
**driver attach/probe time**, before any `up`/channel set.

## 4. AC PHY function-pointer table

Full evidence: [`acphy_function_table.json`](acphy_function_table.json).

Generic family vtable (installed by the *other* family attaches via
`address_taken`): `+0x28` init, `+0x30` cal_init, `+0x38` chanspec_set,
`+0x40` txpower_recalc, `+0xC0` detach.

For **acphy** (`wlc_phy_attach_acphy`):

| slot | name | installer / state | rev42 AC reachable |
| :--- | :--- | :--- | :--- |
| `+0x28` | init | **zeroed** `0xa3001` `mov qword [rbx+28h],0` | **no** (null → `wlc_phy_init` skips body) |
| `+0x30` | cal_init | **zeroed** `0xa3009` | **no** (null → `wlc_phy_cal_init` skips `call rax`) |
| `+0x38` | chanspec_set | not installed by attach_acphy | n/a at attach |
| `+0x40` | txpower_recalc | not installed | n/a |
| `+0xF8` | btc_adjust | `wlc_phy_btc_adjust_acphy` @ `0xa3058` | yes |
| `+0x110` | radio cb | `mov rax,[rbx+110h]` in `wlc_phy_switch_radio` `0xba532`; **no installer** | **no** (AC takes explicit `0xba3a1`) |
| `+0x118` | anacore cb | `mov rax,[rdi+118h]` in `wlc_phy_anacore` `0xbabc7`; **no installer** | yes (but null → fallback) |

Because `+0x28`/`+0x30` are zeroed, the AC path **does not** use
`wlc_phy_init` (callback null → body skipped) nor `wlc_phy_cal_init`'s callback.
The two previously "unresolved D4 indirect targets" (`+0x28`, `+0x118`) are
therefore **not executed** for rev42 AC (this confirms and sharpens M3.4D4A v2).

## 5. First hardware operations (rev42 AC)

| first | function | address | call chain | target |
| :--- | :--- | :--- | :--- | :--- |
| AC PHY MMIO write | `wlc_phy_anacore` | `0xbabee` | `wlc_bmac_attach→wlc_phy_attach→0xbed84` | D11 `0x3e6` fallback (asm) / tool-attributed `*(a0+0x148)+0x7cc`; value `0xf4` |
| AC PHY direct window write | `wlc_phy_attach` | `0xbedb1` | same | `osl_writew(0x3d8, 0)` |
| AC PHY indirect write | `sub_8fb39` | `0x8fb54` | `wlc_phy_switch_radio_acphy@0xaa7d1` | `phy_reg_mod(0x830, 0x7, mask 0x3)` |
| AC PHY table program | `sub_9fb72` | `0x9fc..` | `wlc_phy_switch_radio_acphy@0xaabf3` | `wlc_phy_table_write_acphy` |
| first radio op | `wlc_phy_switch_radio_acphy` | `0xaa80f` | `wlc_phy_attach@0xbf113` | `mod_radio_reg(0x80b, +0x80)` RMW |
| first radio write | `wlc_phy_switch_radio_acphy` | `0xaab62` | same | `write_radio_reg(0x60c, 0x9e)` |
| first synth/PLL-class write | `wlc_phy_switch_radio_acphy` | `0xaab62` | same | `0x60c` sequence `0x9e/0xbe/0x20be/0x60be` |
| first calibration write | **none on the attach path** | — | — | `wlc_phy_cals_acphy` not reached; `+0x30` null |

`wlc_phy_switch_radio` first performs `wlapi_suspend_mac_and_wait`
(`0xaa7bb`) and ends by `wlapi_enable_mac` (`0xab409`) after the main sequence.

## 6. Attach vs init boundary

- **Attach runs at probe** (`wlc_attach → wlc_bmac_attach → wlc_phy_attach`),
  **once**, while MAC/PSM are *not* up (the D11 is enabled/reset by the bmac
  attach). The radio is switched **off** (`wlc_phy_switch_radio(phy, 0)`).
- **Hardware init is not invoked later** for AC: `wlc_phy_init`/`wlc_phy_cal_init`
  callbacks are null. The PHY object state built by `wlc_phy_attach_acphy`
  (238 field writes, capability/board/OTP-derived tables) **must survive**
  attach → later channel set / cal / txpower.
- Function pointers installed during attach (`+0xF8` btc_adjust, and any
  non-relocated stores) are consumed later by their call sites.

## 7. Installer → field → callsite → target chain

```
wlc_phy_attach_acphy 0xa3001  mov qword [rbx+28h],0   (field +0x28 := 0)
        └─ field +0x28
             └─ wlc_phy_init 0xbac6f  mov r13,[rbx+28h]
                  └─ 0xbad4c  call r13        (NOT TAKEN on AC: r13==0)
                       └─ target: (family inits for other PHYs; no acphy target)

wlc_phy_anacore 0xbabb7  mov rax,[rdi+118h]
        └─ field +0x118
             └─ 0xbabc7  call rax              (not reached on AC in wlc_phy_init)
                  └─ installer: NONE FOUND (link UNRESOLVED)
```

**Unresolved link:** the writer of `phy+0x118` is not present in the modelled
store forms; the field is neither installed via `address_taken` nor a direct
`mov [base+0x118],reg` captured anywhere in `.text`. It is only *read* in
`wlc_phy_anacore`; on AC it is null and the `D11+0x3e6` fallback is used.

## 8. Tables on the rev42 AC path

[`acphy_tables.json`](acphy_tables.json): **zero** initvals-family `reg_tables`
are consumed by the AC attach path. AC table programming is via
`wlc_phy_table_write_acphy` inside the `wlc_phy_switch_radio_acphy`
sub-callbacks `sub_9fb72`, `sub_a7089`, `sub_a4adc` (argument-passed tables,
values partly `val=0x1 mask=*(a0+…)`). These tables are **RUNTIME-DERIVED**
(phy-object state), not statically-known initvals. (Tooling gap: argument/pointer
tables are not indexed as `reg_tables`.)

## 9. Radio opcode streams

[`acphy_radio_ops.json`](acphy_radio_ops.json): the 62 ops in
`wlc_phy_switch_radio_acphy` are a **literal register/value sequence**
(`mod_radio_reg`/`write_radio_reg`/`read_radio_reg` with constant args) plus
`osl_delay`; it is **not** a table or an opcode stream. The tool recovered the
exact sequence (e.g. `0x80b` bit-field RMW, then `0x60c` PLL sequence). No
opcode-decoder gap. Remaining dynamic values are in sub-callbacks (`sub_9591e`
`val=?`, `sub_a4adc` `val=?`), i.e. runtime-derived, not an encoding gap.

## 10. Loop-carried address gap

`re regress` still reports `GAP` for DMA TX bases `0x200/0x240/0x280/0x2c0`.
Missing analysis (characterized, **not** implemented — a reusable fix needs a
real CFG dataflow pass, which is not "small"):
1. **induction-variable recognition** (loop counter);
2. **scaled index propagation** (`ch * 0x40`);
3. **phi-like merge** at loop back-edges (values from two predecessors);
4. **table-index propagation**.
The tracker is a single linear pass with no CFG/phi, so `base = 0x200 + ch*0x40`
degenerates to `?`. No small safe fix was added (regression unchanged: still PASS
with the explicit `GAP` line).

## 11. Constant-propagation gap on the AC path

The literal AC radio/PHY writes **are** recovered by the tool. The remaining
unresolved writes are **runtime-derived from PHY-object fields**, not failures
of interprocedural constant propagation:

| function | register | source expression | missing input | caller |
| :--- | :--- | :--- | :--- | :--- |
| `sub_a4adc` | `0x19a..0x1a3`, `0x1602/3/6/7` | `val=?` | phy state loaded from `[a0+off]` | `sub_a7089`/switch_radio_acphy |
| `sub_9591e` | `0x412`, `0x416`, `0x12f` | `val=?` / `*(a0+0x168)` | radio read-back / phy state | switch_radio_acphy |
| `si_pmu_otp_power` | PMU regs | `val=?` | OTP/board power state | attach_acphy |

**CONSTANT PROPAGATION BLOCKS D4? NO** as a *tooling* gap; but the
**runtime-derived values are UNKNOWN write inputs**, which independently blocks
implementation (see §13).

## 12. Earliest true AC-PHY checkpoint

Checkpoints in vendor order:

| id | after | last op | next op | MAC/PSM | PHY | radio | PLL | DMA | async | postcondition | dev-lost | teardown | class |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| CP-A0 | `wlc_phy_attach_acphy` return `0xbec3e` | object built, caps read | `wlc_phy_anacore` | not up / attach | untouched | untouched | untouched | off | none (attach) | phy object + 11 PHY read values | yes | none needed | **STRONG (pre-HW)** |
| CP-A1 | `wlc_phy_anacore` return | PHY0 fallback write | direct `0x3d8` writes | attach | analog touched | untouched | untouched | off | none | PHY0/`0x3d8` | yes | reset | POSSIBLE |
| CP-A2 | `wlc_phy_switch_radio_acphy` `wlapi_enable_mac` `0xab409` | MAC re-enabled, radio sequence done | final PHY/radio writes | attach | programmed | programmed (off→on) | programmed | off | MAC re-enabled | radio window + MAC | yes | reset | POSSIBLE |
| CP-A3 | `wlc_phy_attach` return `0xbf3d6` | full AC init done | return to bmac_attach | attach | programmed | programmed | programmed | off | none | full phy object | yes | reset | **STRONG (post-HW)** |

**Earliest true AC-PHY checkpoint = CP-A0** (after `wlc_phy_attach_acphy`
returns, before the first PHY write). It is vendor-stable (attach-time, no MAC
suspend held, no radio/PLL transition, no DMA, asynchronous work only in the
preceding OTP/SROM reads), and it is the boundary between the **software
PHY-object construction** and the **hardware programming** phases. It is *not*
a "PHY initialised" checkpoint; the first "PHY initialised" point is **CP-A3**.

CP-A0's scope includes the OTP/SROM power-control writes
(`si_pmu_otp_power` writes with `val=?`), so it is not hardware-free.

## 13. Implementation scope

To CP-A0 (software object + capability/board/OTP): replicate
`wlc_phy_attach_acphy`'s 238 field writes + 11 PHY reads + board/NVRAM/OTP
inputs; writes are board-derived. To CP-A3 (full AC init) add the hardware
sequence:

| item | count |
| :--- | ---: |
| functions (core) | 5 (`wlc_phy_attach`, `_attach_acphy`, `wlc_phy_anacore`, `wlc_phy_switch_radio`, `_switch_radio_acphy`) |
| direct sub-callbacks | 6 (`sub_8fb39`, `sub_9027d`, `sub_9591e`, `sub_9fb72`, `sub_a04c2`, `sub_a7089`) |
| PHY ops (core+direct sub) | 186 |
| RADIO ops (core+direct sub) | 301 |
| PHY_TABLE ops | 3 (`sub_a7089`) |
| SHM writes | (anacore/attach) few |
| polls | `wlapi_suspend_mac_and_wait` (bounded) |
| delays | acphy `osl_delay` set |
| board inputs | SPROM/OTP/NVRAM (`boardtype/flags/aa2g/aa5g/antswitch`, OTP words) |
| channel inputs | not yet (attach is band/channel independent) |

Value classification:

| class | examples |
| :--- | :--- |
| PROVEN STATIC | the 62 literal radio ops (`0x80b`, `0x60c=0x9e`, …); `phy_reg_mod 0x830` |
| PROVEN RUNTIME | MAC resume state; radio read-back (`0x80b` read) |
| PROVEN BOARD DATA | txpower/srom11 tables, OTP words |
| PROVEN CHANNEL DATA | none on attach path |
| **UNKNOWN** | `sub_a4adc` `phy_reg_write val=?` (0x19a..0x1a3, 0x1602/3/6/7); `sub_9591e` `val=?`; `si_pmu_otp_power` PMU writes `val=?` |

**An UNKNOWN write value exists (runtime-derived PHY-state register values) ⇒
`IMPLEMENTATION GO = NO`.**

## 14. Answers (Phase 15)

See the final response; summary: AC PHY init happens **at attach** via
`wlc_phy_attach` (not `wlc_phy_init`); `wlc_phy_attach_acphy` is software +
board + capability only; earliest stable checkpoint is **CP-A0** (pre-hardware,
post-object), first "PHY initialised" is **CP-A3**; unknown runtime-derived write
values block implementation.

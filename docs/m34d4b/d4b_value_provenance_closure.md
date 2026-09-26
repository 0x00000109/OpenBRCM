# M3.4D4B — value-provenance closure + CP-A3 checkpoint proof

**Status:** `ANALYSIS ONLY`. No hardware, no MMIO, no OpenBRCM runtime code, no
candidate. Baseline `re` v4 (`0c7cf875…`), `re.db` `44dae60d…`, analysis commit
`83e542b`. Machine-readable records:
[`d4b_value_provenance.json`](d4b_value_provenance.json).

This closes the three "UNKNOWN executed write value" items and re-evaluates
CP-A3. It does **not** redo the AC call graph or the proven callback/null fields.

## 0. Headline

The three items were **not actually executed on the initial attach path**:

- `sub_a4adc` and `sub_9591e` live in the **radio-ON** branch of
  `wlc_phy_switch_radio_acphy`. `wlc_phy_attach` enters the **radio-OFF**
  branch (`xor esi,esi` @`0xbf10a` → `wlc_phy_switch_radio(phy,0)`).
- `si_pmu_otp_power` is conditional and, when reached, is an **RMW**, not a
  literal.

Therefore **no UNKNOWN executed write value remains on the BCM4352 rev42 AC
initial attach path**, and the executed write set is entirely `STATIC`.

## Phase 1 — exact reachability

`wlc_phy_attach` is called **only** from `wlc_bmac_attach` (`0x6a138`), and
`wlc_bmac_attach` calls `dma_attach` *after* it (`0x6a3d6`+). `wlc_phy_attach`
calls `wlc_phy_switch_radio(phy, 0)` (`0xbf10a` `xor esi,esi` → `0xbf113`).

`wlc_phy_switch_radio_acphy(phy, on)` dispatches at `0xaa796`
`test sil,sil` / `0xaa7a0 je 0xab413`:

| branch | range | contents |
| :--- | :--- | :--- |
| **ON** (`on != 0`) | `0xaa7a6..0xab412` | `wlapi_suspend_mac_and_wait`, calibration (`sub_9591e` `0xab3d3`), `sub_a04c2`/`sub_a7089`→`sub_a4adc` (`0xab3e6`/`0xab3f5`, gated by `cmp byte [phy+0x32d],0`), radio power-up, `wlapi_enable_mac` (`0xab409`) |
| **OFF** (`on == 0`, attach) | `0xab413..0xab82b` | 11 PHY writes, 8 radio RMWs, chip tail; **no** calibration, **no** `sub_9591e`, **no** `sub_a7089`/`sub_a4adc`, **no** MAC enable |

| function | caller | chain | branch/path predicate | write | executed initially |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `sub_9591e` | `switch_radio_acphy @0xab3d3` | attach→switch_radio(0)→… | `on != 0` | radio cal | **NO** |
| `sub_a4adc` | `sub_a7089 @0xaa491` | attach→…→`0xab3f5` | `on != 0` ∧ `phy+0x32d != 0` ∧ 4 coeff ptrs non-null | farrow/coeff | **NO** |
| `sub_a7089`/`sub_a04c2` | `switch_radio_acphy` | — | `on != 0` ∧ `phy+0x32d != 0` | — | **NO** |
| `si_pmu_otp_power` | `si_otp_power @0x1e976` | `attach_acphy → otp_read_word → si_otp_power` | `sih+0x1b & 0x10` ∧ `!si_is_otp_disabled` | PMU RMW | **CONDITIONAL** |

**Unreachable writes are removed from the blocker list** (per instruction).

## Phase 2–3 — `sub_a4adc`

Ghidra resolves the `val=?` operands to **table rows**. `FUN_001a4adc`:

```
rx_farrow_tbl[_rev3|_40_rev3] + phy_rev_group*0x5C4   (3 tables, 123 entries, stride 0xC)
entry: byte[0] = selector (== (phy+0x17e)&0xff); word[+4..+0xA] = coefficients
```

Register numbers are immediate (`0x199..0x1a3`); values are single table words
(`0x19a←[+4]`, `0x19b←[+6]`, `0x19c←[+8]`, `0x199←[+A]`, repeated for the second
filter bank). The `0x1602/3/6/7` group comes from caller coefficient arrays
(`[arg+0x54/0x58/0x5c]` or `[arg+0x68/0x6c/0x70]`).

- Provenance: `STATIC_TABLE` (farrow) + `COMPUTED_RUNTIME` (coefficient
  arrays). No UNKNOWN.
- Table reconstruction was **not** required because the writes are not executed
  on the initial path.

## Phase 4 — `sub_9591e`

`FUN_0019591e` is a 3-iteration radio calibration/measurement loop:

- static descriptor arrays: `local_48={1,0,0}`, `local_58={0,2,1}`,
  `local_70={0x14A,0x101,0x11A}` (40 MHz) / `{0x22D,0xF0,0x10A}` (37.4 MHz).
- `0x412` ← `local_70[cVar9]` → **`0x14A`** (40 MHz) / `0x22D` (37.4 MHz),
  `STATIC`.
- `0x12f` mod `0x1f`, register generated from a `local_88`/base — `STATIC`.
- `0x416` is a **read** (used to compute `lVar1+0x339/0x33a/0x33b`,
  `COMPUTED_RUNTIME`), not a write.
- `0x410/0x411` mods are static descriptor bits.

No UNKNOWN.

## Phase 5 — `si_pmu_otp_power`

```
chipid = sih->chip (sih+0x3c); BCM4352 (0x4352) -> otp_power_mask = 0x100
on : writel(reg,  readl(reg) | (mask | sub_118f2_value)); bounded poll (read & mask)!=0
off: writel(reg,  readl(reg) & ~(mask | sub_118f2_value))
final bounded poll on bit 0x1000
```

- Registers: PMU control (`r13+0x60c`, `corebase+0x10`) + `sub_118f2`/`sub_111b0`
  PMU setup.
- Provenance: `STATIC` (mask `0x100`) + `PMU_DERIVED` (`sub_118f2` return) +
  `HARDWARE_READ` (old register value).
- **RMW, not a constant.** UNKNOWN removed. Reachability conditional on
  `sih+0x1b & 0x10` and OTP-not-disabled.

## Phase 6 — data compression

- 12 expanded PHY writes (1 anacore + 11 OFF) and 8 radio RMWs reduce to **19
  literal source records** (`LITERAL_SEQUENCE`); 0 generated loops, 0 computed
  runtime, 0 board-dependent on the initial path.
- The 186/301 figures from the D4B analysis were **ON-path inclusive** (radio
  enable), not attach.

## Phase 7 — CP-A3 checkpoint proof

CP-A3 (end of `wlc_phy_attach`):

| domain | state |
| :--- | :--- |
| MAC | untouched by the OFF path (no suspend/enable); attach context |
| PSM | **not running** (ucode not yet downloaded; `wlc_bmac_init` is later) |
| PHY | core register `0x3e6=0` written; OFF-path PHY regs `0x1720..0x173e` configured; no pending indirect/table op |
| RADIO | **OFF** (power-down); RMWs configure only |
| DMA | not attached yet (`dma_attach` follows `wlc_phy_attach` in `wlc_bmac_attach`) |
| calibration | none pending (cal is deferred to radio-ON) |
| channel | **not required** — attach is channel/band independent |

CP-A3 is synchronous (no delays/polls on the path), radio-off and
channel-independent, so it is **logically stable**. But it is **not an
operational state**: the radio is off and PSM is not running. The vendor makes
the PHY operational later via `wlc_phy_switch_radio(phy,1)`, which is the
**radio-ON** branch (calibration + farrow).

**Verdict: CP-A3 = LOGICALLY STABLE / PARTIALLY OBSERVABLE.** It is a valid
quiescent stopping point, but any "PHY is operational" claim requires the later
radio-ON checkpoint (`CP-A3R`), which is a different call site.

## Phase 8 — observable postconditions

The OFF path writes PHY/radio registers and reads nothing back. Candidate safe
reads (no side effects) exist in principle (`osl_readw` of `0x3e6`,
`0x1720..0x173e`), but the vendor never reads them back and their readback is
**unproven**, so no postcondition is asserted.

**No safe, provenance-backed observable postcondition exists for CP-A3 →
`CP-A3 = LOGICALLY STABLE / NOT DIRECTLY OBSERVABLE`.** No diagnostic read is
introduced.

## Phase 9 — `dev_lost` coverage

Reads on the future D4 sequence:

| reader | address | purpose | 0xffff possible | observer |
| :--- | :--- | :--- | :--- | :--- |
| `wlc_phy_attach` | D11 `0x3e0` (`0xbe5ba`) | phyversion/type/rev | yes | must latch; `0xffff` → type `0xf` prevents family attach |
| `wlc_phy_attach_acphy` | 11 `phy_reg_read` | capability | yes | must latch before any write |
| `wlc_phy_anacore` | — (writes only) | — | n/a | — |
| OFF branch | — (writes only) | — | n/a | — |

All reads precede the first PHY write, so a latch at the first read prevents all
subsequent writes; the existing monotonic `ob_dev_lost_latch`/`ob_dev_lost_observe32`
invariant covers it. **Coverage complete, pending implementation-time wiring of
the `0x3e0` read and the 11 `phy_reg_read` sites.**

## Phase 10 — implementation size

EXPANDED: 12 PHY writes, 8 radio RMWs, 0 table ops, 12 reads, 0 delays, 0 polls.
SOURCE: ~19 literal records, 0 tables, 0 generated-loop descriptors, 0
computed-runtime expressions.

Split: generic AC-PHY code = the anacore fallback + OFF register sequence;
BCM4352-specific data = the `0x1xxx`/`0x8ea` chip-selected literals; board/SPROM
= none on this path; PMU logic = none (OTP is conditional and belongs to
core/OTP bring-up); safety = `dev_lost` observers on the 1 + 11 reads.

## Tooling audit

| fact | oracle |
| :--- | :--- |
| `sub_9591e`/`sub_a4adc` live in the ON branch | BOTH (`re` + Ghidra control flow) |
| `sub_a4adc` table bases (`rx_farrow_tbl*`), entry format | GHIDRA |
| `sub_9591e` descriptor arrays and `0x412=0x14A` | GHIDRA + `re --asm` |
| `si_pmu_otp_power` RMW formula, mask `0x100` | GHIDRA + `re mmio` |
| `+0x160`/`+0x164` = phy_type/phy_rev from D11 `0x3e0` | BOTH (upstream PV masks) |
| OFF-path write list/values | RE (`--asm`) |
| `+0x32d` gate | RE (`--asm`) / Ghidra |

Manual disassembly interventions: **0**. Reusable gaps filed:
1. `re field-writers` misclassified the `cmp byte [r12+32d],0` at `0xab3d8` as
   a store to `+0x32d` (needs store-vs-read verification).
2. `re` has no path-sensitive reachability (which callees execute for a given
   argument/branch); the radio-OFF/ON split required manual/Ghidra control flow.

## Final answers

- **SUB_A4ADC reachable writes?** No — radio-ON only.
- **SUB_A4ADC all values proven?** n/a for D4; values are `STATIC_TABLE` +
  `COMPUTED_RUNTIME` (no UNKNOWN).
- **VALUE REPRESENTATION?** farrow table rows + caller coefficient arrays.
- **SUB_9591E reachable writes?** No — radio-ON only.
- **SUB_9591E all values proven?** n/a for D4; `STATIC`/`COMPUTED_RUNTIME`.
- **SI_PMU_OTP_POWER reachable?** Conditional (`sih+0x1b & 0x10` ∧ OTP enabled).
- **EXACT RMW/VALUE FORMULA?** `on: old|(0x100|v)`, `off: old&~(0x100|v)`,
  `v=sub_118f2()`.
- **PROVENANCE?** STATIC + PMU_DERIVED + HARDWARE_READ.
- **UNKNOWN EXECUTED WRITE VALUES REMAINING?** **None** on the initial attach
  path.
- **186 PHY WRITES:** ON-path inclusive; initial attach = **12** PHY writes,
  **19** source records, **0** generated loops, **0** computed runtime.
- **301 RADIO WRITES:** ON-path inclusive; initial attach = **8** radio RMWs,
  all literal.
- **3 PHY TABLE OPS:** ON-path (radio-ON) only; not executed at attach.
- **CP-A3 VENDOR-STABLE?** `LOGICALLY STABLE` at the attach endpoint; **not
  operational** (radio OFF, PSM not running).
- **WHY?** All writes synchronous, radio-off, channel-independent; the
  operational state requires the later `wlc_phy_switch_radio(phy,1)`.
- **SAFE OBSERVABLE POSTCONDITIONS?** None proven → `NOT DIRECTLY OBSERVABLE`.
- **MAC RESUMED?** n/a (OFF path does not touch MAC).
- **PSM STABLE?** Not running (later).
- **PHY STABLE?** Configured, no pending indirect/table op.
- **RADIO/PLL STABLE?** Radio OFF.
- **MANDATORY CALIBRATION PENDING?** Not at CP-A3; deferred to radio-ON.
- **CHANNEL REQUIRED?** No.
- **DMA SAFE?** Not attached yet; independent.
- **DEV_LOST COVERAGE COMPLETE?** Yes structurally (1 + 11 read sites to wire).
- **EXPANDED D4 OP COUNT?** 12 PHY writes + 8 radio RMWs + 12 reads.
- **MINIMUM SOURCE REPRESENTATION?** ~19 literal records.
- **RE/GHIDRA/MANUAL COUNTS?** RE 3 facts, GHIDRA 4, BOTH 3, TABLE_PROVENANCE
  1, MANUAL 0.
- **D4 IMPLEMENTATION GO?** **NO** (the meaningful operational checkpoint is the
  later radio-ON state, whose values are static/table-driven but not yet scoped
  as a milestone).
- **HARDWARE TEST GO = NO.**

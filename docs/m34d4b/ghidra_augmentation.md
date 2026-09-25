# M3.4D4B — Ghidra augmentation pass

**Scope:** only the D4B facts that were PARTIAL / CONDITIONAL / UNRESOLVED /
UNKNOWN. No re-run of proven D4B facts. No hardware, no MMIO, no driver code.
Proven D4B facts are left intact unless Ghidra produced contradictory evidence.

**Tools (tier order):**
1. Rust `re` / `re.db` schema v3 (unchanged this pass).
2. Ghidra 12.1.3 headless (`/home/kartashoff/projects/ghidra`), Java GhidraScript
   + decompiler + reference manager.
3. Manual `objdump` only to settle one re/Ghidra conflict (`0xa3052`).

Baseline: `re` `9eaac7cf…`, `re.db` `b0c14bd3…`, blob `352a6e34…`,
`re regress` PASS. Ghidra import + auto-analysis 41 s, 394 354 instructions
disassembled (fewer than `re`'s 574 042; Ghidra under-segments this ET_REL
blob — its *negative* results were therefore cross-checked against `re`).

> Address mapping: Ghidra numbers `.text` with a `+0x100000` base relative to
> `re` (`wlc_phy_init` = re `0xbabf5` = Ghidra `0x1babf5`).

Machine-readable: [`ghidra_augmentation.json`](ghidra_augmentation.json).

---

## A. `[phy+0x118]` installer (was UNRESOLVED)

**OLD:** `phy+0x118` read by `wlc_phy_anacore` (`0xbabc7 call rax`), no installer
found; null → D11 `0x3e6` fallback assumed.

**GHIDRA EVIDENCE:**
- Reference manager: no reference installs any function at `+0x118`; the
  decompiler renders `if (*(code **)(pi + 0x118) != (code *)0x0) (**(code **)(pi + 0x118))();` else `osl_writew();` — a null-guarded callback.
- Whole-program operand scan found 0 stores to `[reg+0x118]`.
- `re address_taken` (relocation-based) has **no** `store_disp = 0x118` row.

**NEW:** UNCHANGED. `phy+0x118` has **no function-pointer installer**; it is
zero from the PHY object zero-fill and the AC path uses the D11 `0x3e6`
fallback. Independently confirmed (not just `re`).

**WHY IT DID NOT CHANGE:** two independent methods (Ghidra refs, `re`
relocation table) agree; no writer exists.

---

## B. `[phy+0x28]` / `[phy+0x110]` installers (was CONDITIONAL / not-taken)

**OLD:** `+0x28` candidates = family init pointers; AC zeroes it
(`wlc_phy_attach_acphy 0xa3001`). `+0x110` radio callback never installed.

**GHIDRA EVIDENCE:**
- Reference manager: `wlc_phy_init` has only CALL refs (`sub_6656c 0x1669df`,
  `wlc_bmac_bw_set 0x1660ff`) — **no DATA ref**, so it is never installed as a
  callback at `+0x28`.
- `wlc_phy_attach_acphy` decompile + `re` asm `0xa3001/0xa3009`: `+0x28`/`+0x30`
  zeroed.
- Decompiler `wlc_phy_switch_radio`: AC (`param==0xb`) calls
  `wlc_phy_switch_radio_acphy()` directly; the `*(code**)(pi+0x110)` branch is
  the `else` (unreachable for AC).

**NEW:** UNCHANGED (AC `+0x28`/`+0x30`/`+0x110` are null and not used).

**WHY IT DID NOT CHANGE:** Ghidra confirms the null/zeroed state and the AC
dispatch branch.

---

## C. `wlc_phy_attach_acphy` callback installation (was PARTIAL)

**OLD:** 1 install (`wlc_phy_btc_adjust_acphy` @ `+0xF8`); `+0x28`/`+0x30`
zeroed.

**GHIDRA EVIDENCE:**
- Reference manager: `wlc_phy_btc_adjust_acphy` (`0x199609`) has exactly one
  reference, from `0x1a3052` in `wlc_phy_attach_acphy`, type `DATA`.
- re/objdump conflict at `0xa3052`: `re --asm` printed `mov qword [rbx+0F8h],0`
  and objdump printed `movq $0x0,0xf8(%rbx)`; **manual** `objdump -r` shows
  `00000000000a3059 R_X86_64_32S wlc_phy_btc_adjust_acphy` — the relocation sits
  on the **imm32 field** of that instruction. The true instruction is
  `movq $wlc_phy_btc_adjust_acphy, 0xf8(%rbx)`.

**NEW / ADDED:** the `+0xF8` install is **CONFIRMED** (the immediate is a
relocated function address). Additionally the decompile shows `+0x38`, `+0x40`,
`+0xC0`, `+0xC8`, `+0xD0`, `+0x100` are **also zeroed** at the same site (no
relocation), so the AC vtable has *no* generic chanspec_set/txpower/detach
callbacks — AC uses direct calls.

**WHY IT CHANGED:** a `re`/objdump disassembly representation ambiguity was
resolved by inspecting the ELF relocation table; the conclusion (D4B was right)
is now proven, with extra zeroed slots identified. This corrects a *method*
caveat (zero-looking immediates can be relocations).

---

## D. DMA TX base `0x200/0x240/0x280/0x2c0` (was GAP, PARTIAL regression)

**OLD:** `re regress` reported `GAP` for the DMA TX bases (loop-carried
offset not resolved). `re const --callee dma_attach` suggested `arg3 =
0x240/0x280/0x2c0`.

**GHIDRA EVIDENCE (decompiler, CFG-aware):** in `wlc_bmac_attach`,
```
dma_attach(base + 0x200, ..., (rev<11 ? +0x210 : +0x220), ...)
dma_attach(base + 0x220, ...)  /* rev>10 -> +0x240 */
dma_attach(base + 0x240, ...)  /* rev>10 -> +0x280 */
dma_attach(base + 0x260, ...)  /* rev>10 -> +0x2c0 */
```
and `dma_attach` installs the engine vtable `*engine = &dma64proc`
(BCM4352 64-bit DMA) or `PTR_FUN_004fe8e0`; the TX init is
`engine->vfn(+0x08)` = `sub_f947` (`0xf947`, `dma64_txinit`, 59 B), the reset is
`vfn(+0x10)` = `sub_f64a`, RX reset `vfn(+0xa8)` = `sub_f5ef` (matching `re`).
`wlc_bmac_init 0x6921c` runs the engine loop `di[i]->vfn(+0x08)`.

**NEW:** the DMA base offsets **are literal `dev+0x200/0x220/0x240/0x260`
(`rev>10` → `+0x240/0x280/0x2c0`)** passed to `dma_attach`, and the per-engine
register writes are in `sub_f947`/`sub_fa57`. The `re const` values
(`0x240/0x280/0x2c0`) are **linear-scan artifacts** (RDX is reloaded from memory
before each call); the true form is `base+offset`.

**WHY IT CHANGED:** Ghidra's CFG-aware decompiler resolves the engine array and
the `dev+offset` arithmetic that `re`'s linear pass folds/corrupts. This closes
the "loop-carried address" gap conceptually (it is a vtable + engine-array
model, not loop arithmetic) and gives the exact constants.

---

## E. Interprocedural constant propagation (was PARTIAL)

**OLD:** bounded per-call literal capture only.

**GHIDRA EVIDENCE:** the DMA engine bases (§D) are an interprocedural case:
constants created in `wlc_bmac_attach` are consumed in `dma_attach` and then in
`dma64_txinit` (`sub_f947`). Ghidra resolves it; `re` mis-attributes one leg.

**NEW:** the missing propagation **did** hide a real value (DMA bases). The
remaining AC-path unknowns (`sub_a4adc`/`sub_9591e` `val=?`) are runtime PHY
state, which Ghidra also cannot resolve statically — not a propagation gap.

---

## F. Radio opcode/table streams (was PARTIAL)

**OLD:** `wlc_phy_switch_radio_acphy` = 62 literal register/value ops.

**GHIDRA EVIDENCE:** decompile shows straight-line `mod_radio_reg`/
`write_radio_reg` calls with constant register/value arguments and `osl_delay`s;
no loop over a table.

**NEW:** UNCHANGED (literal sequence; not table/opcode driven).

---

## G. Actual AC hardware-init entry / lineage (was proven)

**GHIDRA EVIDENCE (reference manager, cross-checks D4B):**
- `wlc_phy_anacore` ← `wlc_phy_attach 0x1bed84` (and `wlc_bmac_phy_reset`,
  `wlc_coredisable`, `wlc_bmac_radio_hw`).
- `wlc_phy_switch_radio_acphy` ← `wlc_phy_switch_radio 0x1ba3a1` (AC branch).
- `wlc_phy_init` ← `sub_6656c 0x1669df` (call only).
- `wlc_phy_cals_acphy` ← `wlc_phy_cal_perical`, `wlc_phy_init_test_acphy` — i.e.
  calibration is **not** on the attach path.

**NEW:** UNCHANGED. Lineage: `wlc_attach → wlc_bmac_attach → wlc_phy_attach →
(wlc_phy_attach_acphy; wlc_phy_anacore; wlc_phy_switch_radio →
wlc_phy_switch_radio_acphy)` at probe time.

---

## Resulting status deltas

| item | before | after |
| :--- | :--- | :--- |
| `phy+0x118` installer | UNRESOLVED | **CONFIRMED absent** (null → fallback) |
| `phy+0x110` installer | not-taken | **CONFIRMED absent** |
| `phy+0x28`/`+0x30` | zeroed (re) | **CONFIRMED zeroed**, no DATA ref |
| `phy+0xF8` install | CONDITIONAL (re heuristic) | **CONFIRMED** (`R_X86_64_32S` imm) |
| AC vtable zeroed slots | 0x28/0x30 | 0x28/0x30/0x38/0x40/0xC0/0xC8/0xD0/0x100 |
| DMA TX bases | GAP / artifact consts | **RESOLVED** `dev+{0x200,0x220,0x240,0x260}`, rev>10 `{0x240,0x280,0x2c0}` |
| DMA engine vtable | unknown to `re` as a *call* | `dma64proc`+0x08 → `sub_f947` |
| interproc const-prop | PARTIAL | PARTIAL (real DMA case resolved; AC runtime values remain) |
| radio ops | literal | literal (unchanged) |
| AC init entry | `wlc_phy_attach` | unchanged |

No D4B proven fact was invalidated; one tooling method caveat was corrected
(relocated immediates can look like zeros), and the DMA gap is resolved.

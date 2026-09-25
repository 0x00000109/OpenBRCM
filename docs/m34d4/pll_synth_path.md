# M3.4D4D — PHASE 5: first PLL/synth path

Status: **ANALYSIS ONLY**. No hardware, no MMIO. Tooling: `re` v5 (`f5d03da`),
`re.db` schema v5, plus `objdump` verification.

## 1. The branch (first radio-ON)

`wlc_phy_init @0xbad44` calls `wlc_phy_switch_radio(ON=1)`, which dispatches by
`phytype` to `wlc_phy_switch_radio_acphy @0xaa782`. Its entry:

```
0xaa796  test %sil,%sil           ; on?
0xaa799  mov 0x138(%rdi),%r12
0xaa7a0  je  0xab413              ; OFF path
0xaa7a6  cmpb $0,0xf88(%rdi)
0xaa7ad  jne 0xab7cd
0xaa7b3  mov 0x20(%rdi),%rax
0xaa7b7  mov 0x20(%rax),%rdi
0xaa7bb  call wlapi_suspend_mac_and_wait
0xaa7c0  mov 0x16e(%rbx),%al     ; <-- radio-revision byte
0xaa7c6  cmp $0x1,%al
0xaa7c8  jne 0xaab42
   (al==1) ... PLL sequence A ... jmp 0xaabf0
0xaab42  cmp $0x2,%al
0xaab44  jne 0xaabf0             ; al not in {1,2} -> skip
   (al==2) ... PLL sequence B ...
0xaabf0  call sub_9fb72
0xaac07  phy_reg_mod(0x16b,0x400,0)     ; common continuation
```

So the PLL/synth selection is gated by `pi+0x16e ∈ {1, 2}`; any other value
skips both sequences.

## 2. What `pi+0x16e` is (corrected)

`pi+0x16e` is **not** a free state variable. In `wlc_phy_attach`, the AC path
(`phytype == 0xb`) builds `edx = (r13d << 16) | r14d`, where `r13d` and `r14d`
are 16-bit reads of `*(pi+0x148) + 0x3da` (a **radio revision register**; the
reads are bracketed by writes to `+0x3d8`), then stores:

```
0xbeff8  mov %al,0x16e(%rbx)     ; al = (edx >> 0x14) & 0xff = (r13d >> 4) & 0xff
```

There is no other store to `pi+0x16e` on the AC path; the single other store
(`wlc_phy_periodic_cal_lpphy @0x10fb0a`, 16-bit) is LPPHY-only. The
previously-reported writers `{0,1,2}` were **`cmpb` reads misclassified as
stores** (see `tooling_gap_closure.md`; fixed in `f5d03da`, with a regression
fixture). Verified by whole-`.text` scan: exactly two true stores to `0x16e`.

**Consequence:** `pi+0x16e` is a **hardware-derived radio-revision value**, not
statically known. `wlc_phy_attach` runs at probe (before `wlc_phy_init`), so the
value at the first radio-ON is the radio revision read from `0x3da`.

## 3. The two PLL/synth sequences (fully recovered)

`mod_radio_reg` = RMW; `write_radio_reg` = WRITE; `read_radio_reg` = READ.
Every `0x80b`/`0x80c` write is gated by a `mov 0x0(%rip),%eax` chip-id check
against `{0x4352, 0x4360, 0xa9c4, 0xaa06}` (choosing radio `0x80b` for BCM4352
`0x4352`, else `0x80c`).

### Sequence A (`pi+0x16e == 1`), `0xaa7ce..0xaab3d`

| pc | op |
|---|---|
| `0xaa7d1` | call `sub_8fb39` |
| `0xaa80f..0xaaa99` | chip-id-gated radio RMWs on `0x80b`: `0x80/0x80`, `0x10/0x10`, `0x8/0x8`, `0x4/0x4`, `0x2/0x2`, `0x20/0x20`, `0x2000/0x2000`, `0x4000/0x4000` |
| `0xaa88b..0xaa9df` | radio RMWs `0x97f=0x10`, `0x8ea=1`, `0x8ea=0x100`, `0x8ea=0x10` |
| `0xaa911` | `osl_delay(100)` |
| `0xaaabe` | **lock poll**: `read_radio_reg(0x80b) & 1`, ≤ `0x65` (101) iterations, 100 µs each |
| `0xaaafd`, `0xaab38` | chip-id-gated `mod_radio_reg(0x80b, 0x4000/0x2000, mask 0)` |

### Sequence B (`pi+0x16e == 2`), `0xaab4a..0xaabeb`

| pc | op |
|---|---|
| `0xaab50` | call `sub_9027d` |
| `0xaab62` | `write_radio_reg(0x60c, 0x9e)` |
| `0xaab67` | `osl_delay(100)` |
| `0xaab7e` | `write_radio_reg(0x60c, 0xbe)` |
| `0xaab90` | `write_radio_reg(0x60c, 0x20be)` |
| `0xaaba2` | `write_radio_reg(0x60c, 0x60be)` |
| `0xaabc4` | **lock poll**: `read_radio_reg(0x0b) & 1` |
| `0xaabd5` | **lock poll**: `read_radio_reg(0x20b) & 1` |
| `0xaabeb` | `write_radio_reg(0x60c, 0xbe)` |

**Completion predicate (both sequences):** the lock poll loop is bounded
`≤ 0x65` iterations with a 100 µs delay per iteration (`osl_delay(100)`); it
exits when the enable bit(s) are observed (`0x80b` bit0 for A; `0x0b` bit0 and
`0x20b` bit0 for B). No unbounded wait.

## 4. Which branch does BCM4352 rev42 take?

**UNKNOWN — hardware-derived.** The selector `pi+0x16e` is the radio-revision
byte decoded from radio register `0x3da` at attach; it is not a static literal
and no vendor constant pins it. It cannot be resolved without reading the radio
(or a radio-revision table keyed to this board). **No guessed default** is
recorded: the three outcomes are sequence A (`==1`), sequence B (`==2`), or
neither.

This is a live value blocker for D4.

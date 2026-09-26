# M3.4D3B — isolated band-init + `d11ac1bsinitvals42` test

**Status: `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE ATTEMPTED` /
NOT HARDWARE PROVEN.**

**Failure point of the 2026-09 hardware attempt (frozen candidate
`5fa5e5ba8762cc98a853b0b3bc4db9150f90e88e`): the pre-D3B `tsf_cfpstart`
(D11 0x18c) postcondition in the shared `ob_d3a1_validate()`.** D2A, D2B, the
D3A1 T1/T2 tail, the 4 TX DMA channels, FIFO0 RX and `switch_macfreq` all
completed; the mandatory teardown then succeeded (RX/TX0..TX3 reset PASS, all
engines stopped, rings released). **No bsinitvals record was applied** — the
run never entered the D3B slice. The `0x18c` equality gate was invalid: it is a
write-only CFP-start programming register whose readable value lives at
`0x604/0x606`; the gate is removed and replaced with write-accounting plus
diagnostics (provenance in `m34d3a1_vendor_tail_test.md` §17 and §12 below).

The D3B band-init milestone extends the hardware-proven D3A1 tail with the exact
vendor `sub_6656c` pre-bs slice and the `d11ac1bsinitvals42` (73-record) table,
and STOPS immediately before `wlc_phy_init`.

Module param: **`bsinitvals_test_only=1`**, mutually exclusive with
`fw_validate_only` / `ucode_test_only` / `initvals_test_only` / `dma_test_only` /
`d11_tail_test_only` / `sprom_evidence_only` (conflict -> `-EINVAL` before any
hardware access).

Design/analysis: [`m34d3b_band_init.md`](m34d3b_band_init.md),
[`m34d3b_implementation_plan.md`](m34d3b_implementation_plan.md).

## 1. Exact vendor D3B call graph (`re` slice, blob sha256 352a6e349f…)

```
wlc_bmac_init (0x6828a)
  0x695cb  wlc_bmac_switch_macfreq          [D3A1, HARDWARE PROVEN]
  0x695d8  sub_6656c(dev, chanspec, band=0) *** D3B starts ***
            0x66582  chip 0xa9a7/0x4331 si_seci_upd   [SKIPPED for BCM4352]
            0x665c3  osl_readw(D11+0x3e0)             [read-only, unused]
            0x665d6  sub_62766  = wlc_bmac_write_mhf  (5 x SHM writes)
            0x665db  table select: [dev+0x84]==0x2a && [band+0x1c]==0x0b
            0x669bd  sub_60f67(dev, d11ac1bsinitvals42)  (73 records)
            0x669c2  *** STOP (applier return) ***
            0x669df  wlc_phy_init(...)                [D4, never entered]
```

`sub_60f67` (`0x60f67`, size 103) is the shared 8-byte-record applier:
`{u16 offset, u16 width, u32 value}`, `width==2 -> osl_writew(D11+offset)`,
`width==4 -> osl_writel(D11+offset)`, stop at `offset==0xffff`.

## 2. Exact D3A1 -> D3B ordering

The vendor order is fixed and reproduced without reordering:

```
D2A (ucode+PSM) -> D2B (610 common initvals)
  -> sub_67efd -> T1 -> DMA -> T2 -> switch_macfreq      [D3A1 prefix, DMA live]
  -> sub_6656c pre-bs (read-only phyver, MHF1..5)
  -> table select (phyrev 0x2a + phytype 0x0b)
  -> bsinitvals42 (73 records)
  -> STOP before wlc_phy_init
  -> verified D3A0 teardown
```

## 3. DMA lifetime verdict and refactor

**SHOULD DMA STILL BE INITIALIZED WHILE D3B EXECUTES? YES.** The vendor enables
and posts the four TX DMA engines and FIFO0 RX inside the D3A1 tail (T1 -> DMA
-> T2) and only reaches `sub_6656c` afterwards, so the engines are live while
band init runs.

The earlier D3A1 helper `ob_d3a1_test()` teared DMA down before `sub_6656c`. D3B
therefore reuses a new extracted prefix, **`ob_d3a1_run_prefix()`**, which runs
D2B -> `sub_67efd` -> T1 -> DMA bring-up -> T2 -> the D3A1 postcondition gate and
returns with the D3A0 engines **left live**. `ob_d3a1_test()` is unchanged in
behaviour (prefix + teardown); `ob_d3b_test()` calls the same prefix, runs D3B,
then performs the mandatory verified teardown. No D3A0 lifecycle guarantee was
weakened (see §7).

## 4. Board-data -> MHF derivation (not hard-coded)

`struct ob_sprom_board` is decoded by `ob_si_read_mac()` from the SAME
already-read, CRC-validated 234-word rev11 image (zero extra MMIO) using the
proven field map: `boardtype` w0x02, `boardflags` w0x42|w0x43<<16, `aa2g`
w0x50&0xff, `aa5g` w0x50>>8, `antswitch` w0x54>>8 (absent when 0xff).

`ob_d3b_antsel_type()` reproduces the exact `wlc_antsel_attach` (`0x5970a`) flow
including the L_bt0 -> L_bf fall-through; `ob_d3b_mhf_vector()` derives
`mhfs[0..4]`. For the captured BCM4352 board
(`boardtype=0x85ba`, `boardflags=0x10001000`, `aa2g=aa5g=7`, `antswitch=0`,
`antsel_type=0`) the derivation yields the proven vector
`{0x0100, 0x0000, 0x0000, 0x0000, 0x0080}`. Gates: MHF1 `pub+0x54 != 0`
(0xffffffff, no initial-up zeroer), MHF2 `si_pci_war16165` false (core 0x83c),
MHF4 non-4313 skip, MHF5 `phytype != 7`.

## 5. Exact MHF write sequence (`sub_62766`)

Confirmed by `re fn 0x62766 --asm`: five iterations, `value = band->mhfs[i]`,
`offset` from the in-image table `{0x5e,0x60,0x62,0x78,0xd4}`, each via
`wlc_bmac_write_shm` (= `ob_ucode_write_shm16`: `OBJADDR = 0x10000 | (off>>2)`,
barrier, `OBJDATA + (off&2)`).

| idx | SHM | value (captured BCM4352) |
|---|---|---|
| 0 | 0x5e | 0x0100 |
| 1 | 0x60 | 0x0000 |
| 2 | 0x62 | 0x0000 |
| 3 | 0x78 | 0x0000 |
| 4 | 0xd4 | 0x0080 |

## 6. Exact bsinitvals42 application

`brcm/bcm4352-d11ac1bsinitvals42.bin` (592 B, sha256 `e81a645c79f5…`): **73**
records (terminator excluded), **39 x w2 + 34 x w4**, applied in strict original
order by the reused 8-byte-record applier. Recovered offsets:
`0x160` w4 x34 (OBJADDR/SHM selectors), `0x164` w2 x15 + `0x166` w2 x19
(OBJDATA halves), and 5 direct IHR writes `0x680/0x682/0x684/0x686/0x700` w2.
Every applied offset is checked against that recovered allowlist; none is in the
PHY-indirect (0x3fc/0x3fe) or radio (0x3d8/0x3da) windows.

## 7. Isolated-mode call graph, STOP boundary, failure model

```
ob_probe(bsinitvals_test_only=1)
  -> ob_isolated_mode_select6(...) == OB_ISOLATED_D3B_TEST  (else -EINVAL)
  -> ob_si_prepare_board_data_for_d3b (cc + MAC + rev11 board fields; read-only)
  -> ob_d3b_test
       -> ob_d3a1_run_prefix("d3b-test")   [DMA left live; on post-DMA failure:
                                             verified ob_d3a0_teardown]
       -> bcma_read16(D11+0x3e0)           [read-only]
       -> MHF writes                        [5 SHM]
       -> bsinitvals42                      [73 records]
       -> ob_d3b_validate                   [postconditions]
       -> ob_d3a0_teardown                  [verified; fatal latch on failure]
       -> STOP (never wlc_phy_init)
```

**STOP:** the `sub_60f67` applier return (`0x669c2`), before `wlc_phy_init`
(`0x669df`), `wlc_phy_anacore`, `wlc_phy_switch_radio_acphy`, any PHY-indirect
window (`D11+0x3fc/0x3fe`) or radio window (`D11+0x3d8/0x3da`). No code path
enables EN_MAC, MACINTMASK or host IRQ delivery.

| failure point | DMA live? | action |
|---|---|---|
| before DMA bring-up | no | return error, no teardown needed (prefix) |
| after DMA, before D3B | yes | prefix performs verified `ob_d3a0_teardown` |
| any D3B / postcondition failure | yes | `ob_d3b_test` performs verified `ob_d3a0_teardown` |
| teardown cannot verify a stop | yes | D3A0 fatal latch: retain memory, pin module, reboot; **no free** |
| success | yes -> stopped | verified teardown, then free |

## 8. Deterministic postconditions

Read after the applier, before `wlc_phy_init`:

| observable | expected |
|---|---|
| MHF1..5 (SHM 0x5e/0x60/0x62/0x78/0xd4) | `{0x0100,0,0,0,0x0080}` |
| SHM 0x0010 (u32) | `0x00000014` |
| SHM 0x001c (u32) | `0x00000183` |
| SHM 0x0094 (u32) | `0x000001f4` |
| `MACCONTROL` (0x120) | `0x44020402` (EN_MAC=0, PSM_RUN=1) |
| `MACINTMASK` (0x12c) | `0` |
| bsinitvals records / w16 / w32 | `73 / 39 / 34` |

All SHM values are SHM RAM (exact readback); the inherited MACCONTROL/MACINTMASK
are the proven D3A1 exit values. No equality check is invented for a
hardware-updated IHR field.

## 9. Tests and validation

- `tests/host/ob_d3b_test.c`: rev11 board decode (+absent), antsel_type all
  branches incl. L_bt0 -> L_bf, captured vector `{0x0100,0,0,0,0x0080}`, MHF SHM
  destinations/order, bsinitvals 73/39/34, terminator exclusion, malformed/
  truncated rejection, PHY/radio offsets forbidden, stage order, DMA live
  during D3B, fail-closed free (incl. fatal), mode mutual exclusion, post
  predicate.
- `scripts/docs-check.sh` §4g: implementation/prefix/73-39-34/MHF-map/forbidden
  PHY/mode guards.
- `scripts/re-bootstrap.sh` PASS; `make clean && make` clean;
  `make hosttest` PASS; `checkpatch --strict --terse -f` 0 errors/0 warnings;
  `make signed` produces a signed `openbrcm.ko`.

## 10. Pre-hardware audit (Phase 12)

1. Full required vendor prefix executed? **YES** (D2A -> D2B -> D3A1 prefix ->
   MHF -> bsinitvals42).
2. D3A0 DMA live at the vendor-required point? **YES** (`ob_d3a1_run_prefix`
   leaves it live).
3. D3B executes before teardown? **YES**.
4. Teardown always executed after a post-DMA error? **YES** (prefix and D3B
   failure paths both teardown).
5. MHF values derived, not hard-coded? **YES** (SPROM board -> antsel_type ->
   vector; gates documented).
6. Exactly 73 bsinitvals records applied? **YES** (plan + runtime counter).
7. Any path reach PHY/radio/channel? **NO** (allowlist + no PHY calls).
8. Any path enable EN_MAC? **NO** (MACCONTROL postcondition keeps it 0).
9. Any path enable host IRQ delivery? **NO** (MACINTMASK stays 0; no
   `request_irq`).
10. Any failure free DMA memory before verified stop? **NO** (D3A0 fail-closed).
11. Normal OpenBRCM behaviour unchanged? **YES** (isolated branch only; the
    normal path is untouched).
12. Older isolated modes unchanged? **YES** (D3A1 now calls the extracted
    prefix + teardown; the sequence and postconditions are identical).

## 11. Remaining blockers before hardware test

- Human approval and the one-shot hardware run (no other value/provenance/safety
  blocker).
- The D3B run depends on the already-proven D2B/D3A0/D3A1 prefix; no additional
  board value is missing.

## 12. 2026-09 hardware attempt — `tsf_cfpstart` postcondition correction

The one-shot `bsinitvals_test_only=1` attempt on frozen candidate
`5fa5e5ba…` (module SHA256 `3a10aff9047afa601271e61973ae4ca1857a9aa260644830c530f5ce13218a8c`)
reached, in order: D2A PASS -> D2B PASS -> D3A1 T1 PASS -> 4 TX DMA channels
programmed/validated -> FIFO0 RX programmed/validated IDLE -> DMA bring-up
validation PASS -> T2 complete -> `switch_macfreq` complete. The shared
`ob_d3a1_validate()` then failed **only** on:

```
tsf_cfpstart = 0x3c000000   expected = 0x02000000
```

The mandatory DMA teardown then succeeded (RX reset PASS, TX0..TX3 reset PASS,
all DMA engines stopped, rings released). **No bsinitvals record was applied.**

Root cause (provenance, not guesswork): `tsf_cfpstart` (D11 `0x18c`) is a
**write-only CFP-start programming register**. The vendor and upstream only
write it (beacon interval `period << 10`); the readable CFP value is
`tsf_cfpstrt_l/h` at `0x604/0x606`, which is exactly what the vendor's
`wlc_bmac_validate_chip_access` reads back (and it never reads `0x18c`). A
direct read of `0x18c` is therefore not a stable equality postcondition. Full
evidence: [`m34d3a1_vendor_tail_test.md`](m34d3a1_vendor_tail_test.md) §17.

Correction (minimal; the vendor-exact write value and order are unchanged):
`ob_d3a1_validate()` now **write-accounts** the `0x18c = 0x02000000` write and
logs the raw `0x18c`/`0x604`/`0x606` reads as diagnostics. `tsf_cfprep`
(`0x188`) equality is **retained** (the same run read it back exact; it is a
config field, not a live counter). No other postcondition was equality-gated on
a live/hardware-updated register (audit §17.5).

Retest recommendation: **GO for one D3B retest** after this fix (no PHY).

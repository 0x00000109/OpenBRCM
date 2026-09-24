# M3.4D3 — band-switch initvals (`d11ac1bsinitvals42`) and PHY-boundary analysis

**Status: `ANALYSIS ONLY` — NOT IMPLEMENTED — NOT HARDWARE PROVEN.**

This document reconstructs the vendor path from the proven common-initvals state
through band initialization, the `d11ac1bsinitvals42` table, and the exact entry
into real AC PHY initialization. No hardware was accessed and no code was
implemented.

- Last hardware-proven milestone: **M3.4D2B** (isolated rev42 common-initvals
  test, BCM4352, candidate `f27286f`).
- Machine-generated record classification:
  `docs/m34d3/bsinitvals_classification.{md,json}`
  (`scripts/analyze_bsinitvals.py`, read-only, deterministic).
- Vendor blob: `wlc_hybrid.o_shipped`
  (sha256 `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`),
  same object used for M3.4C/D1/D2B.
- C3 = upstream `brcmsmac` (`main.c`, `d11.h`) for corroboration only; no code
  copied.

## 1. Post-common-initvals vendor call graph

The common applier call is at `wlc_bmac_init` `0x68b98`. The same function then
runs a long D11 setup tail and finally calls the band-init helper:

```
wlc_bmac_init (0x6828a, size 5071)
├─ 0x68b98  sub_60f67(dev, d11ac1initvals42)          [M3.4D2B, PROVEN]
├─ 0x68bab  sub_67efd                                  RX-queue/FIFO config (D11 IHR)
├─ 0x68c43/0x68c99 osl_memcpy                          build xmtfifo_sz[]
├─ 0x68d04..0x68d57 osl_writew x4                      TX FIFO control/flush
├─ 0x68d97/0x68daf/0x68dd0/0x68df2 wlc_bmac_write_shm  M_FIFOSIZE0..3 (host override)
├─ 0x68e3b..0x68f7a  osl_readw/osl_writew              IHR/PHY-ctl touch-ups
├─ 0x68f87..0x68fae  wlc_bmac_read_shm x4              M_FIFOSIZE read-back
├─ 0x68fef/0x69001   wlc_bmac_write_shm                SHM config
├─ 0x6901a/0x69035   osl_writel                        objmem/aux writes
├─ 0x69047  wlc_bmac_mctrl(dev, ~0x40060000, 0x40020000)   MACCONTROL change
├─ 0x69059..0x690a4  osl_writel x5                     IHR/timer config
├─ 0x690b1  wlc_bmac_macphyclk_set(dev, 1)             MAC-PHY clock
├─ 0x690dd..0x69556  osl_writew / wlc_bmac_write_shm   band/rate/power SHM tables
├─ 0x6957b  wlc_bmac_mute(dev)                         mute TX
├─ 0x69594  wlc_phy_switch_radio(pi, ...)              *** PHY/radio access ***
├─ 0x695cb  wlc_bmac_switch_macfreq(dev)               MAC freq
├─ 0x695d8  sub_6656c(dev, chanspec, band)             *** band init ***
│           ├─ 0x665c3  osl_readw(D11+0x3e0)           PHY version read (D11 window)
│           ├─ 0x665d6  sub_62766
│           ├─ 0x669bd  sub_60f67(dev, d11ac1bsinitvals42)  [conditional]
│           ├─ 0x669df  wlc_phy_init(pi, chanspec)     *** real PHY init entry ***
│           ├─ 0x669e7  sub_62684
│           ├─ 0x669fa  wlc_bmac_set_cwmin
│           ├─ 0x66a0d  wlc_bmac_set_cwmax
│           ├─ 0x66a45/0x66a5d/0x66a8e wlc_bmac_write_shm
│           ├─ 0x66ac9  wlc_bmac_mhf
│           ├─ 0x66ad1  sub_6106b                       GPIO
│           └─ 0x66b30  wlc_bmac_set_extlna_pwrsave_shmem
└─ 0x695ee  wl_intrsrestore
   ├─ 0x6960b  sub_64887
   └─ 0x6962c/0x69642 si_pcielcreg
```

`sub_6656c` (size 1490, unnamed local immediately after `wlc_bmac_up_prep`)
has exactly **two callers**:
- `wlc_bmac_init` `0x695d8` (initial bring-up), and
- `wlc_bmac_set_chanspec` `0x67bd0` (band/channel change).

C3 mapping (`brcmsmac/main.c`): the same shape is `brcms_b_bsinit()`
(`main.c:1659`) = `brcms_c_ucode_bsinit()` (write band MHF host flags, then the
PHY-specific bsinitvals) -> `wlc_phy_init()` -> txant/cwmin/cwmax/timing/
phytype/phyver. `brcms_b_bsinit` is called from `brcms_b_setband()` (band
switch, `main.c:1795`) and from the initial bring-up path (`main.c:3324`).

Note: real PHY/radio activity (`wlc_phy_switch_radio` `0x69594`) already occurs
**before** the bsinitvals call in the full vendor `wlc_bmac_init` tail.

## 2. Exact bsinitvals consumer

- **Table:** `d11ac1bsinitvals42` (`.rodata` `0x99430`, size `0x250` = 592 B;
  equals `/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin`, sha256
  `e81a645c79f55557c87f4662702d7c57599918c9b2340bc4e440ce1bdbd014da`).
- **Reference:** instruction `mov $d11ac1bsinitvals42,%rsi` at code address
  `0x66610` (relocation at `0x66613`), consumed by the applier call at
  `0x669bd` into `sub_60f67`.
- **Applier `sub_60f67` (0x60f67, `wlc_bmac_write_inits`):**
  - `rsi` = record pointer; `r12 = *(dev+0xd0)` = D11 base.
  - Walk 8-byte LE records `{u16 offset, u16 width, u32 value}`; stop at
    `offset == 0xffff` (terminator; never written).
  - `width==2` -> `osl_writew(value, D11base+offset)`;
    `width==4` -> `osl_writel(value, D11base+offset)`.
  - Strict ascending order, no de-duplication, no branch/patch.
- **Selection condition (rev42):** within `sub_6656c`, the bsinitvals block at
  `0x66604` is taken only when `[dev+0x84] == 0x2A` (D11/PHY rev 42) **and**
  `[[dev+0xE8]+0x1C] == 0xB`. C3: `[dev+0x84]` is the PHY revision; the
  `+0x1C` word is the PHY type (`0xB` = 11 = AC-class; C3 `pub.h` names G=2,
  N=4, LP=5, SSN=6, HT=7, LCN=8, LCNXN=9, and the AC value is out of the
  C3-supported set). The same selector expands to many sibling tables
  (`d11a0g0bsinitvals5` etc.) for other PHY rev/type combinations. So only
  **one** table is applied for a given rev/type; there is no per-band symbol
  switch here.
- **Reconfirmed shape (image + blob):** 592 B, **73** data records, terminator
  index 73, **39 x 16-bit / 34 x 32-bit**. The 34 "32-bit" records are the
  `OBJDATA (0x164)`/`OBJADDR (0x160)` accesses (width 4); the 39 width-2 records
  are the 5 direct IHR writes plus the 34 `OBJDATA (0x164/0x166)` 16-bit SHM
  halves. (The classification counts by record width, so the 34 SHM data
  records are width 2.)

## 3. Full 73-record classification

No record omitted; full per-record table in
`docs/m34d3/bsinitvals_classification.md`. Summary:

| kind | count |
|---|---|
| direct | 5 |
| obj_sel (`OBJADDR 0x160`) | 34 |
| obj_data (`OBJDATA 0x164/0x166`) | 34 |

| category | count |
|---|---|
| OBJ (SHM window) | 68 |
| IHR/IFS | 4 |
| IHR/NAV | 1 |

| side-effect class | count |
|---|---|
| object-memory selector | 34 |
| band/ucode SHM state (config) | 34 |
| band timing/IFS configuration (D11) | 4 |
| band NAV configuration (D11) | 1 |
| **TOTAL** | **73** |

The 5 direct records:

| idx | offset | w | value | region | meaning |
|---|---|---|---|---|---|
| 0 | `0x0686` | 2 | `0x09d0` | IHR/IFS | slot/IFS timing (UNKNOWN field) |
| 1 | `0x0680` | 2 | `0x3e3e` | IHR/IFS | slot/IFS timing (UNKNOWN field) |
| 2 | `0x0682` | 2 | `0x023e` | IHR/IFS | slot/IFS timing (UNKNOWN field) |
| 3 | `0x0700` | 2 | `0x003c` | IHR/NAV | NAV limit (UNKNOWN field) |
| 4 | `0x0684` | 2 | `0x0212` | IHR/IFS | slot/IFS timing (UNKNOWN field) |

All 68 SHM records use `OBJADDR = 0x0001xxxx` (SHM space, **no** auto-inc),
each followed by one 16-bit `OBJDATA` (low) or `OBJDATA+2` (high) write. See §4.

## 4. Indirect transaction groups

The table is non-auto-increment, so 34 selector writes precede 34 data writes.
The transaction grouper in `scripts/analyze_bsinitvals.py` yields **39 logical
transactions** (34 indirect + 5 direct). Each indirect transaction is a selector
plus its 1-2 16-bit half-writes:

- Every `OBJADDR` value is `0x0001xxxx` -> **SHM space**, low 16 bits are the
  word index (`base*4` = byte offset), no `AUTO_INC`/`WINC`/`RINC` bits.
- Where a selector is repeated with the other `OBJDATA` half, the pair forms a
  32-bit SHM value; where only the high half is written, it is a standalone
  16-bit write to `byte+2` (the grouper records `half`/`byte` per write).
- No selector in this table uses auto-increment, so there is no hidden window
  advance.

Byte targets written by the table (all SHM, all `config`): `0x000c`, `0x0010`,
`0x001c`, `0x0094`, `0x0990`, `0x099c`, `0x09a0`, `0x09a4`, `0x09b0`, `0x09b8`,
`0x09c4`, `0x09cc`, `0x09d8`, `0x09e0`, `0x09ec`, `0x09f4`, `0x0a00`, `0x0a08`,
`0x0a14`, `0x0a1c`, `0x0a28`, `0x17d0`, `0x17d4` (low and/or high halves).

## 5. What "bsinitvals" actually means

Not "band-switch" in the sense of a per-channel synthesizer switch, and not
PHY/radio code. Recovered meaning:

- **Band-specific ucode IHR/SHM/SCR initialization** (C3 `brcms_c_ucode_bsinit`
  comment: *"do band-specific ucode IHR, SHM, and SCR inits"*). The table sets
  the PSM/ucode-side band parameters and timing/NAV registers.
- Applied:
  - once during initial bring-up (`wlc_bmac_init` `0x695d8`), and
  - on every band/channel change that re-runs band init
    (`wlc_bmac_set_chanspec` `0x67bd0`).
- It is applied **once per band-init call**, immediately before `wlc_phy_init`.
- The **same** table (`d11ac1bsinitvals42`) is used for the selected PHY
  rev/type regardless of 2.4 vs 5 GHz; band-specific values (MHF host flags,
  CWmin/CWmax, `M_PHYTYPE`/`M_PHYVER`) are written separately by the enclosing
  helper (C3 `brcms_c_write_mhf` + `brcms_b_bsinit`).
- Different symbols are selected only by **PHY rev/type**, not by band.

## 6. Band / chanspec dependency

- The band-init helper `sub_6656c(dev, chanspec, band)` is invoked with the
  current chanspec and band. In `wlc_bmac_set_chanspec` it is reached after
  `wlc_phy_chanspec_radio_set` and `wlc_bmac_mctrl`; in `wlc_bmac_init` it is
  reached at the end of the initial D11 setup.
- The table itself contains **no chanspec/channel value** and no synthesizer/PLL
  write (no radio window, no PHY window). It only writes D11 SHM and IHR timing.
- Band-specificity is carried by the surrounding code, not the 73 records:
  MHF host flags (`wlc_bmac_mhf` -> SHM `M_HOST_FLAGS*`), CWmin/CWmax from
  `[phy+...]`, and `M_PHYTYPE`/`M_PHYVER`.
- For the initial BCM4352 bring-up the band state is whatever the caller set
  (`wlc_setxband`/`wlc_phy_chanspec_radio_set` earlier in `wlc_bmac_init`);
  the table does not read it.
- Implication for isolation: the records can be written without a programmed
  channel, but their **correct values** are band-dependent, so an isolated test
  must fix and document the band/MHF state (or reproduce the enclosing writes).

## 7. MACCONTROL / PSM state

- The 73 records contain **no `MACCONTROL (0x120)`** write.
- In the proven D2B exit state `MACCONTROL = 0x04020402`
  (`MCTL_WAKE|MCTL_INFRA|MCTL_PSM_RUN|MCTL_IHR_EN`; `EN_MAC=0`, `SHM_EN=0`;
  PSM suspended, `MI_MACSSPNDD` observed).
- **However, the vendor changes `MACCONTROL` before band init.** At
  `wlc_bmac_init` `0x69047`:
  `wlc_bmac_mctrl(dev, mask=0x40060000, val=0x40020000)` ->
  `cur = (cur & ~0x40060000) | 0x40020000`:
  sets bit30 (`0x40000000`, unnamed in C3 = **UNKNOWN**), keeps
  `MCTL_INFRA (0x20000)`, clears `MCTL_AP (0x40000)`. `EN_MAC`, `PSM_RUN`,
  `SHM_EN`, `WAKE`, `IHR_EN` are unchanged (`EN_MAC` stays 0, `PSM_RUN` stays 1,
  `SHM_EN` stays 0).
- During bsinitvals the PSM remains suspended exactly as in D2B; the table does
  not touch `MACCONTROL`. The first later `MACCONTROL`-relevant activity is
  inside `wlc_phy_init` (`wlapi_bmac_bw_set`) and the still-later
  `enable_mac` path (not reached here).
- Consequence: the isolated D2B exit state is **not** the vendor's exact
  entry state for band init (bit30/AP differ). This must be resolved before a
  bsinitvals-only hardware test.

## 8. Interrupt effects

```
IRQ ENABLE EFFECT = NONE
```
- No record targets `intctrlregs` (`0x20-0x5f`), `MACINTMASK (0x12c)`,
  `MACINTSTATUS (0x128)`, per-FIFO `intmask`/`intstatus` (`0x200-0x37f`), PSM
  interrupt mirrors (`0x484-0x48a`), or any BCMA/PCI IRQ routing register.
- Surrounding helper code: `wlc_bmac_mhf` only writes SHM `M_HOST_FLAGS`;
  `sub_6656c` does not call `request_irq` or touch IRQ masks. The enclosing
  `wlc_bmac_init` calls `wl_intrsoff`/`wl_intrsrestore` around the sequence and
  `si_pcielcreg` at the very end, but those are outside the table and not part of
  a bsinitvals-isolated boundary.
- `MACINTMASK` remains 0 in the D2B exit state and is not changed by the table.

## 9. DMA effects

```
DMA ENABLE EFFECT = NONE
```
- No record targets the DMA/PIO block (`0x200-0x3d7`) or the DMA FIFO
  diag/aggregation range (`0x380-0x3d7`), direct or through the SHM window.
- No ring pointer/base/control, no `MI_DMAINT` enable, no datapath engine
  enable, no `ob_dma`/`ob_rx`/`ob_tx` equivalent. All SHM writes are configuration
  state consumed by the PSM/ucode.
- Note: the *enclosing* `wlc_bmac_init` tail runs `sub_67efd` (RX-queue/FIFO
  config) before band init; that is D11 IHR configuration, not DMA-ring
  activation, but it is still outside the table and must be handled by the D3
  boundary decision.

## 10. PHY / radio effects

The table itself:

- No PHY window (`0x3e0-0x3fe`), no PHY indirect selector/data (`0x3e0/0x3fc/
  0x3fe`), no radio window (`0x3d8-0x3db`), no synthesizer/PLL, no calibration
  trigger, no channel-programming write.
- The `0x3e0` access seen in `sub_6656c` is an `osl_readw` **before** the table
  (PHY version read via the D11 window), not a table record.

Therefore the 73 records are **D11/MAC-side only**. Real PHY/RF begins only
**after** the table, inside `wlc_phy_init` (see §11). This is the key result:
the table is safe in itself, but in the vendor flow it is not the only thing
that happens before PHY init.

## 11. Exact entry into real AC PHY initialization

- Transition point: the direct call `wlc_phy_init(pi, chanspec)` at
  `sub_6656c` `0x669df`, taken immediately after the `sub_60f67` applier returns.
- `wlc_phy_init` (`0xbabf5`, size 738):
  1. `wlc_phy_chanspec_shm_set` (`0xbac31`) — D11 **SHM** only (0 MMIO).
  2. `osl_readl(*(pi+0x148)+0x120)` (`0xbac44`) — reads **D11 MACCONTROL**
     (`0x148` is the D11 base, `+0x120` MACCONTROL), still not PHY.
  3. `wlc_phy_anacore(pi, 1)` (`0xbac84`) — **first real PHY MMIO**:
     `phy_reg_write_array`/`phy_reg_write`/`phy_reg_and` through the D11 PHY
     indirect window `D11+0x3e0/0x3fc/0x3fe` (`phy_reg_write` does
     `osl_readw(D11+0x3e0)` then `osl_writel(D11+0x3fc, addr|data)`).
  4. `wlc_phy_switch_radio(pi, 1)` (`0xbad44`) — radio band select
     (`wlc_phy_switch_radio_acphy` for AC).
  5. `call *[pi+0x28]` (`0xbad4c`) — **AC PHY init body**; `wlc_phy_attach_abgphy`
     installs `[pi+0x28] = wlc_phy_init_aphy` (relocation at `0x899dd`), i.e.
     `wlc_phy_init_aphy` (`0x8c3f9`, size 3041, 129 calls) is entered here.
- So the exact first hardware operation crossing into PHY is:
  **`wlc_phy_anacore`'s `phy_reg_write`/`phy_reg_write_array` on the D11 PHY
  indirect window**, at `wlc_phy_init+0x8f`; the AC-specific init body starts at
  the indirect call to `wlc_phy_init_aphy` (`wlc_phy_init+0x157`).
- **Caveat:** in the full vendor path, `wlc_phy_switch_radio` is already called
  at `wlc_bmac_init` `0x69594`, i.e. **before** band init/bsinitvals. A clean
  "before first real PHY MMIO" boundary therefore also has to exclude or
  account for that earlier call.

## 12. Common initvals vs bsinitvals

- Object spaces: common uses SHM + SCR; band-switch uses **SHM only**.
- Direct-offset overlap: **none** (`git`/script: `direct_offset_overlap: []`).
- Shared logical SHM bytes (low/high resolved): **3** —
  `0x0010`, `0x001c`, `0x0094` — and in all 3 the band-switch table writes a
  different value than the common table, i.e. **3 last-wins overrides**
  (band-switch runs after common). Values:

| SHM byte | common value(s) | band-switch value(s) |
|---|---|---|
| `0x0010` | `0x00000000` | `0x00000014` |
| `0x001c` | `0x00640000` | `0x00000183` |
| `0x0094` | `0x00320000` | `0x000001f4` |

- Conclusion: the band-switch table **intentionally overrides** a small set of
  common SHM state (band-dependent parameters). Applying it after common
  initvals is required for correctness; applying it alone on the D2B state is
  not equivalent to the vendor path.

## 13. Deterministic postconditions (proposed)

Candidates **after** bsinitvals and **before** `wlc_phy_init`, with write source
and overwrite analysis. None has been validated on hardware; these are proposals
only, and several are band-dependent.

| observable | source record | expected | later overwrite? |
|---|---|---|---|
| SHM `0x0010` (u32) | rec 9/10 | `0x00000014` | band-init only; stable to `wlc_phy_init` |
| SHM `0x001c` (u32) | rec 11/12 | `0x00000183` | stable to `wlc_phy_init` |
| SHM `0x0094` (u32) | rec 13/14 | `0x000001f4` | stable to `wlc_phy_init` |
| IHR `0x0680` | rec 1 | `0x3e3e` | band-init only |
| IHR `0x0686` | rec 0 | `0x09d0` | band-init only |
| IHR `0x0700` | rec 3 | `0x003c` | band-init only |

Caveats:
- `M_FIFOSIZE*`, `MACINTMASK`, `MACCONTROL` from D2B are **not** valid
  bsinitvals gates: `M_FIFOSIZE*` is overwritten by the host before band init,
  and `MACCONTROL` is changed at `0x69047`.
- A safe read method: `wlc_bmac_read_shm` (16-bit SHM window, proven in D2A/D2B)
  for SHM; `osl_readw(D11+offset)` for IHR.
- Do **not** adopt any expected value until the D3 entry state is fixed; the
  three SHM values shown are **common-table overrides**, so they depend on the
  common table being applied first.

## 14. Idempotence / retry behavior

- Called once during initial bring-up and on each band/channel change; not on
  every `wlc_phy_init`.
- Re-applying the 73 records is a pure rewrite of the same band state (all plain
  writes; no W1C/W1S, no counters, no self-triggering values, no auto-increment
  windows). Re-application after a fresh band init is therefore idempotent.
- However, the **surrounding** band-init helper is not idempotent in isolation:
  it also calls `wlc_phy_init` (PHY/radio re-init) and writes MHF/SHM state.
- Retry policy recommendation: keep D2B's "no retry" policy; a failed bsinitvals
  attempt may be retried only by a full re-run from a known state.

## 15. Side-effect accounting (all 73)

```
SHM state (config)           34
object-memory selector       34
band timing/IFS (D11)         4
band NAV (D11)                1
------------------------------
TOTAL                        73
```
No DMA, IRQ, PHY, radio, MAC-control or self-triggering records. This
reconciles exactly to the 73 data records.

## 16. Formal isolation decision

```
CAN BSINITVALS BE ISOLATED SAFELY BEFORE REAL PHY INIT?  NOT YET
```

Blockers (all required for a YES):

1. **Entry state not isolated.** The vendor applies bsinitvals deep inside band
   init, after a long `wlc_bmac_init` tail (`0x68bab..0x695cb`: `sub_67efd`,
   the host `xmtfifo_sz`/`M_FIFOSIZE` overwrite, SHM/rate/power tables,
   `MACCONTROL` change at `0x69047`, `wlc_bmac_macphyclk_set`, `wlc_bmac_mute`,
   `wlc_phy_switch_radio`, `wlc_bmac_switch_macfreq`) that is largely
   unanalyzed. The D2B exit state is not the band-init entry state.
2. **Band/MHF dependency.** Correct values depend on the band's MHF host flags
   and CWmin/CWmax, which are written by the enclosing helper, not the table.
3. **Selection state.** The table is selected by PHY rev (`0x2A`) and PHY type
   (`0xB` AC); an isolated harness must force/document this.
4. **No clean PHY separation.** Real PHY/radio activity
   (`wlc_phy_switch_radio` at `0x69594`) already precedes band init, and the
   band-init helper unconditionally proceeds into `wlc_phy_init`
   (conditional only on the 3rd arg / PHY rev), whose first PHY MMIO follows
   immediately. "Stop before first real PHY MMIO" is not a natural break in the
   vendor flow without reproducing/omitting preceding steps.
5. **Postconditions not proven.** The proposed §13 gates are band-dependent and
   unvalidated; no provenance-backed gate has been confirmed against a hardware
   run.

Non-blockers (already proven): the 73 records are D11/MAC-side only
(no PHY/radio/DMA/IRQ/MACCONTROL), the table loop is bounded (73 records /
terminator), and re-application is idempotent.

## 17. Smallest safe combined boundary (proposal)

Since bsinitvals cannot be cleanly separated from its enclosing band-init state,
the next milestone should **not** be "73 records only". The smallest defensible
increment is the **post-common D11 setup through bsinitvals, stopping before the
`wlc_phy_init` call and before `wlc_phy_switch_radio`** — i.e. reproduce the
`wlc_bmac_init` tail from `0x68bab` to `0x695d8` (excluding the `0x69594`
switch_radio and the `0x695d8` helper's `wlc_phy_init`), then apply the 73
records. Concretely:

- **Starting state:** proven D2B exit (`MACCONTROL=0x04020402`, PSM suspended).
- **New work:** a bounded, provenance-backed reproduction of the untranslated
  `wlc_bmac_init` tail records (`sub_67efd`, M_FIFOSIZE host override, the
  `0x68fxx-0x690a4` SHM/IHR writes, the `0x69047` MACCONTROL update, rate/power
  SHM tables), then the 73 bsinitvals records.
- **Explicit STOP:** before `wlc_phy_switch_radio (0x69594)`, before
  `sub_6656c` `0x669df` (`wlc_phy_init`), before any PHY/radio window.
- **Runtime bounds:** the new writes are all bounded straight-line records; the
  same bounded D2A poll as D2B; no loops beyond the record counts.
- **Residual state:** PSM_RUN=1, EN_MAC=0, core enabled, common + tail + band
  initvals applied; no PHY/radio/DMA/IRQ.
- This is a **design proposal only**; it must itself be analyzed and approved
  before implementation, because it touches more registers than D2B.

Alternatively, if the goal is to reach real PHY init, the boundary must include
`wlc_phy_init` and therefore leaves D11-only isolation and cannot be validated
with the current D2B-style deterministic gates.

## 18. AC PHY follow-on roadmap (no implementation)

Keep these separate; do not collapse:

1. **M3.4D3a — post-common D11 setup + bsinitvals** (§17): D11-only, stop before
   `wlc_phy_test`.
2. **AC PHY attach/state setup** — `wlc_phy_attach_abgphy` / `[pi+0x28]`
   installation, software PHY state, no RF.
3. **PHY register initialization** — `wlc_phy_init` -> `wlc_phy_anacore` /
   `wlc_phy_init_aphy` first PHY indirect writes (D11 `0x3e0/0x3fc/0x3fe`).
4. **PHY table loading** — the aphy table arrays (`phy_reg_write_array`).
5. **Radio initialization** — `wlc_phy_switch_radio` / `wlc_phy_switch_radio_acphy`.
6. **Synthesizer / PLL / channel setup** — chanspec -> radio, `wlc_phy_chanspec_set`.
7. **Calibration** — `wlc_phy_cal_init` / `wlc_phy_initcal_enable` / per-PHY cal.
8. **Receive-path enable** — RX DMA/FIFO engable and mac80211 RX.

Each stage needs its own analysis and isolated gate; each is a separate
milestone.

## 19. Unresolved unknowns

1. Meaning of the 5 direct IHR fields (`0x680/0x682/0x684/0x686` IFS,
   `0x700` NAV) and the SHM rate/power table bytes at `0x0990..0x0a28`,
   `0x17d0/0x17d4` — values proven, field names UNKNOWN.
2. `MACCONTROL` bit30 (`0x40000000`) set by `wlc_bmac_init` `0x69047`; no C3
   name. Also `MCTL_AP` clear.
3. Which enclosing `wlc_bmac_init`-tail writes are strictly required for a
   correct bsinitvals application.
4. The exact band/MHF state used during the initial BCM4352 bring-up (not
   measured on hardware; the D2A/D2B runs did not record MHF/SHM band state).
5. Whether `sub_6656c`'s `osl_readw(D11+0x3e0)` result gates the bsinitvals
   selection (it is written to a local but its use is not fully traced).
6. Exact consumer of the `0x0990..0x0a28` / `0x17d0` SHM regions (ucode vs PSM).

## 20. Analysis commit hash

(recorded after commit; script + docs on `m34d3-bsinitvals-analysis`.)

## 21. Draft PR

(recorded after push; Draft PR titled
`M3.4D3: analyze rev42 band-switch initvals and PHY boundary`.)

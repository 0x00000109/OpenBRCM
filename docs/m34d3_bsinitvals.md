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

## 0. Ordering resolution and revised decision (correction)

This section resolves the ordering contradiction in the first revision and
supersedes §11's caveat, §16 and §17.

**Question:** on the exact BCM4352 rev42 / AC initial bring-up path, is
`wlc_phy_switch_radio` executed **before** `d11ac1bsinitvals42`?

**Answer: NO.**

- `wlc_bmac_init` `0x69580`:
  `mov 0xe8(%rbx),%rax; cmpw $0x7,0x1c(%rax); jne 0x69599`.
  The `call wlc_phy_switch_radio` at `0x69594` sits on the **fall-through of
  `phy_type == 7` (NPHY/HT)** only. For BCM4352 the phy type is `0xB` (AC), so
  the branch is not taken. The call does **not** occur.
- `wlc_bmac_init` `0x6957b` `wlc_bmac_mute(dev,1,1)` is likewise guarded by
  `cmpb $0x0,-0x99(%rbp)` (`0x6955b`), where `-0x99(%rbp)` is argument #3 of
  `wlc_bmac_init`. The sole caller `wlc_init` (`0x3c6ec`) passes `edx=0`, so
  mute is also **not** executed on initial up.
- The only `wlc_phy_switch_radio` on the AC initial path is the
  **unconditional** `wlc_phy_switch_radio(pi, 1)` inside `wlc_phy_init`
  (`0xbad44`), i.e. **after** the bsinitvals write.

**Consequence:** the previously proposed vendor-ordered boundary
"post-common D11 tail -> 73 bsinitvals records -> STOP before
`wlc_phy_switch_radio`/`wlc_phy_init`" is, for the AC path, **correct and
vendor-ordered** — the `0x69594` radio switch is not part of the AC prefix. The
first real PHY/RF register writes on the AC path happen *inside*
`wlc_phy_init`, after bsinitvals:

1. `wlc_phy_chanspec_shm_set` — D11 SHM only.
2. `wlc_phy_anacore(pi, 1)` (`0xbac84`) — **first PHY indirect write**
   (`D11+0x3fc/0x3fe`).
3. `wlc_phy_switch_radio(pi, 1)` (`0xbad44`) -> `wlc_phy_switch_radio_acphy`
   (`0xaa782`) — **first radio-window writes** (`D11+0x3d8/0x3da`).
4. `call *[pi+0x28]` (`0xbad4c`) -> `wlc_phy_init_aphy` (`0x8c3f9`).

The formal isolation answer therefore becomes **YES** for "can a vendor-ordered
bsinitvals test stop before real PHY/RF writes?" — but the isolated unit is
**not** "the 73 records alone"; it is the **vendor `wlc_bmac_init` prefix
through the bsinitvals write** (D11/MAC/SHM only). Full details are in
**Appendix A** (A.1 switch_radio, A.2 tail stages A-L, A.3 MACCONTROL bit 30,
A.4 macphyclk_set, A.5 mute, A.6 switch_macfreq, A.7 initial band/MHF state,
A.8 minimum prefix, A.9 revised postconditions/roadmap). M3.4D3 remains
**ANALYSIS ONLY**.

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
├─ 0x6957b  wlc_bmac_mute(dev,1,1)  [SKIPPED on initial up: gated on
│                                     wlc_bmac_init arg#3==0 from wlc_init]
├─ 0x69580  if ([[dev+0xE8]+0x1C] == 7)   // NPHY/HT only
│            0x69594 wlc_phy_switch_radio(pi, 0)  [NOT TAKEN for phy type 11/AC]
├─ 0x695cb  wlc_bmac_switch_macfreq(dev, 0)            MAC freq (D11 0x62e/0x630)
├─ 0x695d8  sub_6656c(dev, chanspec, band=0)           *** band init ***
│           ├─ 0x665c3  osl_readw(D11+0x3e0)           D11 window read (no write)
│           ├─ 0x665d6  sub_62766                      5 x wlc_bmac_write_shm only
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

**CORRECTION (supersedes the earlier claim).** The earlier revision of this
report stated that `wlc_phy_switch_radio` at `0x69594` runs before bsinitvals.
That is **false for the BCM4352 rev42 / AC path**. The call is inside an
`if ([[dev+0xE8]+0x1C] == 7)` branch (`0x69580`), i.e. **NPHY/HT only**; phy
type for this device is `0xB` (AC), so the branch is not taken and no
`wlc_phy_switch_radio` executes before band init. The `0x6957b` mute call is
also skipped (gated on `wlc_bmac_init` argument #3, which `wlc_init` passes as
`0`). The only `wlc_phy_switch_radio` on the AC initial path is the
unconditional one inside `wlc_phy_init` (`0xbad44`), i.e. **after** bsinitvals.
See the new "Ordering resolution" section above §1.

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
- **Corrected caveat (was wrong):** the earlier `wlc_phy_switch_radio` at
  `wlc_bmac_init` `0x69594` is **NPHY/HT-gated** (`phy_type == 7`) and is
  **not** taken for the BCM4352 AC path. There is therefore **no** PHY/radio
  write before band init/bsinitvals; the only AC `wlc_phy_switch_radio` is
  step 4 above (`0xbad44`), after bsinitvals. See §0 / §A.1.

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

## 16. Formal isolation decision (revised)

**Question 1 — does `wlc_phy_switch_radio` precede `d11ac1bsinitvals42`?**
**NO** (BCM4352 rev42 / AC). The `0x69594` call is NPHY/HT-gated. See §0/A.1.

**Question 2 — where does real PHY/RF begin?**
Inside `wlc_phy_init`, **after** bsinitvals: first PHY-indirect write at
`wlc_phy_anacore` (`0xbac84`), first radio-window write at
`wlc_phy_switch_radio_acphy` (`0xaa782`, reached from `0xbad44`).

**Question 3 — can a vendor-ordered bsinitvals test stop before real PHY/RF
writes?**

```
YES
```

for the BCM4352/AC path: the entire vendor prefix
`wlc_init -> wlc_bmac_init(hw,chanspec,0) -> sub_6656c -> bsinitvals` is
D11/MAC/SHM/si only (no PHY-indirect write, no radio write). It is, however,
**not** "the 73 records alone" — it is the full vendor prefix (see §17/A.8).

Residual (now non-blocking) caveats:

1. The prefix is large (`0x68bab..0x695d8` plus sub_6656c pre-bs); each stage is
   classified in §A.2, with a few stages still "UNKNOWN whether strictly
   required" (A/C/H). They are all D11/SHM and do not touch PHY/radio.
2. Band/MHF dependency remains: the bs values are band state, so the prefix must
   establish the band (`dev+0xE8`, MHF) faithfully. The exact numeric initial
   band/chanspec is config-derived and not statically fixed (§A.7).
3. The proposed postconditions are not yet hardware-validated.
4. `wlc_bmac_macphyclk_set(1)` (D11 core clock gate) runs before bs, and
   `MACCONTROL` gains `MCTL_DISCARD_PMQ` at `0x69047`; neither is a PHY/RF
   register write but both change MAC/clock state and must be included.

## 17. Smallest faithful vendor-ordered boundary

The next milestone should **not** be "73 records only", and should **not** stop
early trying to avoid `wlc_phy_switch_radio` (it is not on the path). The
smallest faithful increment is the **vendor `wlc_bmac_init` prefix through the
bsinitvals write**, then STOP before `wlc_phy_init`. Concretely:

- **Starting state:** proven D2B exit (`MACCONTROL=0x04020402`, PSM suspended).
- **New work:** vendor-order reproduction of stages A-K (§A.2) from `0x68bab`
  to `0x695d8`, then `sub_6656c` up to and including
  `sub_60f67(dev, d11ac1bsinitvals42)` at `0x669bd`.
- **Explicit STOP:** before `sub_6656c` `0x669df` (`wlc_phy_init`); therefore
  before `wlc_phy_anacore`, before `wlc_phy_switch_radio_acphy`, before any
  PHY-indirect window (`D11+0x3fc/0x3fe`) and any radio window
  (`D11+0x3d8/0x3da`).
- **Stages intentionally included although not PHY:** `MACCONTROL` update
  (`0x69047`), `wlc_bmac_macphyclk_set(1)` (`0x690b1`),
  `wlc_bmac_switch_macfreq(dev,0)` (`0x695cb`). `wlc_bmac_mute` and the
  `0x69594` `wlc_phy_switch_radio` are **not** part of the AC path.
- **MMIO write classes:** D11 register writes (`osl_writel`/`osl_writew` to
  `0x120,0x24,0x128,0x188,0x18c,0x62e,0x630,…`), D11 core cflags via `si_core_cflags`,
  `wlc_bmac_write_shm`/`copyto_objmem` (SHM/objmem), `si_pmu_get_bb_vcofreq`
  (PMU read). **No PHY-indirect write, no radio write.**
- **Delays/polls:** only the D2A PSM poll already proven in D2B; `switch_macfreq`
  has no delay. No new unbounded poll.
- **Deterministic postconditions:** §13 SHM `0x10/0x1c/0x94` and IHR
  `0x680/0x686/0x700`.
- **Residual state:** `MACCONTROL` ≈ `0x44020402` (or `0x44000404`; see §A.3),
  PSM_RUN=1, EN_MAC=0, MAC-PHY clock enabled, D11 core enabled, common + tail +
  bsinitvals applied; **no PHY/radio/DMA/IRQ**.
- This is a **design proposal only**; it touches more registers than D2B and
  must itself be analyzed and approved before any implementation.

## 18. AC PHY follow-on roadmap (revised, no implementation)

Keep these separate; do not collapse (vendor order preserved):

- **D3A — post-common D11 setup tail** (`0x68bab..0x695d8`): stages A-K, all
  D11/SHM/si; includes `MACCONTROL` `0x69047`, `macphyclk_set` `0x690b1`,
  `switch_macfreq` `0x695cb`. No PHY/radio.
- **D3B — band init + bsinitvals** (`sub_6656c` through `0x669bd`): apply the 73
  records; STOP before `wlc_phy_init`.
- **D4 — AC PHY register init** (`wlc_phy_init`: `wlc_phy_chanspec_shm_set` ->
  `wlc_phy_anacore` -> `wlc_phy_switch_radio_acphy` -> `wlc_phy_init_aphy`).
- **D5 — PHY table loading** (`phy_reg_write_array` aphy tables).
- **D6 — radio/channel/synth** (`wlc_phy_switch_radio_acphy`,
  `wlc_phy_chanspec_set`).
- **D7 — calibration** (`wlc_phy_cal_init`/per-PHY cal).
- **D8 — receive-path enable** (RX DMA/FIFO enable + mac80211 RX).

Each stage needs its own analysis and isolated gate; each is a separate
milestone.

## 19. Unresolved unknowns

Resolved by this revision:
- ~~`MACCONTROL` bit30~~ -> **`MCTL_DISCARD_PMQ` = `1<<30`** (`d11.h:459`; C3
  `main.c:3240` uses `MCTL_INFRA | MCTL_DISCARD_PMQ`). See §A.3.
- ~~`wlc_phy_switch_radio` ordering and first PHY/RF boundary~~ -> the
  `0x69594` call is NPHY-gated and not taken for AC; real PHY/RF begins inside
  `wlc_phy_init` after bsinitvals. See §0 / §A.1.
- ~~`wlc_bmac_mute` on initial up~~ -> skipped (`wlc_bmac_init` arg #3 = 0).
  See §A.5.
- ~~`wlc_bmac_macphyclk_set` / `wlc_bmac_switch_macfreq`~~ -> D11/si core
  cflags and PMU VCO-derived D11 `0x62e/0x630` writes; no PHY/radio/PLL write.
  See §A.4 / §A.6.

Still unknown:
1. Meaning of the 5 direct IHR fields (`0x680/0x682/0x684/0x686` IFS,
   `0x700` NAV) and the SHM rate/power table bytes at `0x0990..0x0a28`,
   `0x17d0/0x17d4` — values proven, field names UNKNOWN.
2. Which enclosing `wlc_bmac_init`-tail writes are *strictly* required for a
   correct bsinitvals application (all are D11/SHM; see §A.2/§A.8).
3. The exact band/MHF state used during the initial BCM4352 bring-up (not
   measured on hardware; the D2A/D2B runs did not record MHF/SHM band state).
   `wlc_default_chanspec()` is function-derived, not a static constant.
4. Whether `sub_6656c`'s `osl_readw(D11+0x3e0)` result gates the bsinitvals
   selection (its value is not used in the observed path).
5. Exact consumer of the `0x0990..0x0a28` / `0x17d0` SHM regions (ucode vs PSM).
6. Whether `wlc_phy_cal_init`'s optional `*(pi+0x30)` callback is NULL for AC
   (no `wlc_phy_cal_init_acphy` symbol exists; the other PHYs install one).

## 20. Analysis commit hash

- `ef4be46` — `m34d3: analyze rev42 band-switch initvals and PHY boundary`
  (initial report + classifier).
- `b7d4376` — `m34d3: resolve switch_radio ordering; correct PHY/RF boundary`
  (this correction: §0, Appendix A, revised §16/§17/§18/§19).

Both DCO signed, on `m34d3-bsinitvals-analysis`; no driver code and no hardware
interaction.

## 21. Draft PR

PR **#7** `M3.4D3: analyze rev42 band-switch initvals and PHY boundary` —
Draft/OPEN, base `main`, head `m34d3-bsinitvals-analysis`. Kept Draft; not
Ready, not merged. Status is `ANALYSIS ONLY` / NOT IMPLEMENTED /
NOT HARDWARE PROVEN.

---

# Appendix A — ordering / boundary / shared-tail reversal (correction)

All addresses are `.text` offsets in `wlc_hybrid.o_shipped` (sha256
`352a6e349f…`). No hardware accessed.

## A.1 `wlc_phy_switch_radio` — full reversal (BCM4352/AC path)

Generic wrapper `wlc_phy_switch_radio(pi, on)` @ `0xba302`:

1. `osl_readl(*(pi+0x148) + 0x120)` — reads D11 MACCONTROL; **result unused**
   (posted-write flush only).
2. `wlapi_update_bt_chanspec` (`0x152956`) -> `wlc_bmac_update_bt_chanspec`.
3. Dispatch on `pi->phy_type` (`pi+0x160`): `4`->`_nphy` (`0x1519d2`),
   `7`->`_htphy` (`0xcac35`), **`0xB`->`_acphy` (`0xaa782`)**, `5/6/8`->inline
   `phy_reg_write_array`, `2/0`->`_abgphy` (`0x887be`), else `pi->switch_radio`
   (`pi+0x110`).

**AC dispatch is `wlc_phy_switch_radio_acphy(pi, on)` @ `0xaa782`** (size
`0x10aa` = 4266 B). Window helpers: `read_radio_reg` (`0xb305c`) /
`write_radio_reg` (`0xb6101`) access D11 base **`+0x3d8` (data) / `+0x3da`
(ctrl)** (some chips `+0x3f6`); `mod_radio_reg` (`0xb631c`) =
`read_radio_reg` then `write_radio_reg`; `phy_reg_write` (`0xb6650`) /
`phy_reg_mod` access D11 `+0x3fc/0x3fe`. This is genuine PHY-indirect and
radio-window hardware.

ON path (`on=1`) for BCM4352 (`acphychipid==0x4352` selects register addresses
`0x80b`/`0x8ea`; sibling parts use `0x80c`/`0x8f2`):

- guard: if `pi+0xf88 != 0` jump to `0xab7cd`; else
  `wlapi_suspend_mac_and_wait(pi->wl)` (`0xaa7bb`).
- `mod_radio_reg`: `(0x80b,0x80,0x80)`, `(0x80b,0x10,0x10)`,
  `(0x80b,0x8,0x8)`, `(0x80b,0x4,0x4)`, `(0x80b,0x2,0x2)`,
  `osl_delay(100)`, `(0x97f,0x10,0x2)`, `(0x8ea,0x1,0x1)`,
  `(0x8ea,0x100,0x100)`, `(0x8ea,0x10,0x10)`, `(0x80b,0x20,0x20)`,
  `(0x80b,0x2000,0x2000)`, `(0x80b,0x4000,0x4000)`, `osl_delay(100)`,
  read `0x80b` … then `write_radio_reg` to `0x60c` (`0x9e/0xbe/0x20be/0x60be`),
  `read_radio_reg 0xb/0x20b`, `write_radio_reg 0x60c`, `phy_reg_mod(0x16b,
  0x400,…)`, `phy_reg_write(0x175,…)`, …
- later `write_radio_reg` bursts to `0x75c..0x75f`, `0x960/0x961/0x963`,
  `0x400`; `phy_reg_write` to `0x173e,0x1739,0x173a,0x1725,0x1729,0x1721,
  0x1728,0x1720`, `phy_reg_mod 0x408`, `phy_reg_write 0x417/0x416`; final
  `mod_radio_reg(0x8f2,0x200,0x10)`. Delays: `0x64`(=100), `3`, `1`, `0xa`.
- terminates with `wlapi_enable_mac` (`0xab409`).

The matching OFF path is at `0xab413`. Conclusion: not a no-op in general; it
suspends the MAC, writes radio and PHY registers, and delays. **However it is
not invoked on the AC initial path before bsinitvals** (see §0).

Call sites of `wlc_phy_switch_radio` and their containing functions:
`0x637c0` `wlc_coredisable`; `0x63ec9/0x63eed` `wlc_bmac_radio_hw`;
`0x67a3b` `wlc_bmac_set_chanspec`; **`0x69595` `wlc_bmac_init` (NPHY-gated)**;
**`0xbad45` `wlc_phy_init` (unconditional, AC reaches here)**; `0xbf114`
`wlc_phy_attach`; `0x110160` (PHY watchdog path). Only `0xbad45` is on the AC
initial sequence.

## A.2 Post-common tail stages A-L (exact classification)

Between the common applier (`0x68b98`) and `sub_6656c` (`0x695d8`):

| # | Stage | Address(es) | D11/SHM writes | PHY/radio | Required before bs | D2B satisfied | Readback candidate |
|---|---|---|---|---|---|---|---|
| A | RX queue/FIFO config `sub_67efd` | `0x68bab` | D11 RX regs | no | UNKNOWN | no | RX regs |
| B | `xmtfifo_sz[]` build | `0x68c43/0x68c99` | host array from `[dev+0x150]` | no | only feeds D | no | shadow |
| C | TX FIFO ctrl/flush | `0x68d04..0x68d57` | D11 TX FIFO regs | no | UNKNOWN | no | FIFO regs |
| D | M_FIFOSIZE override | `0x68d97/0x68daf/0x68dd0/0x68df2` | SHM `0x98/0x9a/0x9c/0x9e` | no | NOT REQUIRED | no | read_shm |
| E | M_FIFOSIZE read-back | `0x68f87..0x68fae` | reads only | no | no | no | – |
| F | MACCONTROL update | `0x69047` | D11 `0x120` (shadow) | no | NOT REQUIRED (D11 only) | no (differs) | MACCONTROL |
| G | `macphyclk_set(1)` | `0x690b1` | D11 core cflags bit4 | clock gate (no reg write) | likely before PHY init | no | cflags |
| H | SHM/rate/power tables | `0x690dd..0x69556` | SHM/objmem | no | faithful-prefix only | partial | read_shm |
| I | `wlc_bmac_mute` | `0x6957b` | **skipped** (arg#3=0) | (would call `wlc_phy_mute_upd`) | NOT REQUIRED | no | – |
| J | `wlc_phy_switch_radio` | `0x69594` | **skipped** (NPHY-gated) | (would be radio) | NOT REQUIRED for AC | no | – |
| K | `wlc_bmac_switch_macfreq` | `0x695cb` | D11 `0x62e/0x630` from PMU VCO | no | before PHY init | no | D11 regs |
| L | entry `sub_6656c` | `0x695d8` | – | – | – | – | – |

Inside `sub_6656c` **before** bsinitvals: `si_seci_upd` (4331 only, not 4352),
`osl_readw(D11+0x3e0)` (read only), `sub_62766` = five `wlc_bmac_write_shm` to
SHM `0x5e,0x60,0x62,0x78,0xd4` (no PHY/radio). So **no PHY/radio register write
exists anywhere between the common applier and `d11ac1bsinitvals42`** on the AC
path.

## A.3 MACCONTROL transition at `0x69047`

`wlc_bmac_mctrl(dev, mask, val)` (`0x6066d`): `shadow=*(dev+0x168)`;
`new=(shadow & ~mask) | val`; if changed, store shadow and write D11 `+0x120`
(`0x605ad`). At `0x69047`: `mask=0x40060000`, `val=0x40020000`.

Bit meanings (`d11.h`): `MCTL_WAKE=1<<26`, **`MCTL_DISCARD_PMQ=1<<30`**,
`MCTL_AP=1<<18`, `MCTL_INFRA=1<<17`, `MCTL_IHR_EN=1<<10`, `MCTL_PSM_RUN=1<<1`.

- Cleared: `MCTL_AP`. Set: `MCTL_DISCARD_PMQ`, `MCTL_INFRA`.
- Result from the D2B exit value `0x04020402`:
  `0x04020402 & ~0x40060000 | 0x40020000 = 0x44020402`.
- If only the vendor `wlc_bmac_mctrl` shadow is considered (the three
  `wlc_bmac_mctrl` calls in `wlc_bmac_init` leave `0x04000404`), the result is
  `0x44000404`.
- `MCTL_DISCARD_PMQ` makes the MAC drop PMQ frames (station-mode BSS config;
  C3 `main.c:3239-3241` performs the identical operation). It does **not**
  enable PHY/radio/IRQ, does not alter PHY clocking, and does not change how SHM
  is interpreted; it only affects MAC frame handling.

## A.4 `wlc_bmac_macphyclk_set` (`0x65006`)

`macphyclk_set(dev, on)`: `si_core_cflags(dev+0xb8 /*D11 si*/, 0x10,
on==1?0x10:0)`. It sets/clears **D11 core cflags bit 4** ("MAC-PHY clock
enable"). No PHY-indirect MMIO, no radio, no PLL. Called with `on=1` at
`0x690b1` (before bs). It gates the MAC<->PHY clock; whether it is required
strictly before bsinitvals is UNKNOWN, but bsinitvals itself writes only D11
SHM/IHR and needs no PHY clock.

## A.5 `wlc_bmac_mute` (`0x64562`)

On the initial path it is **skipped** (`wlc_bmac_init` arg #3 = `0`, from
`wlc_init` `0x3c6ec`). If invoked (`mute=1`), it: `wlc_bmac_tx_fifo_suspend`
FIFOs `1/3/0/2`; `wlc_bmac_set_addrmatch` (phy rev <= 0x27) or
`wlc_bmac_write_amt(0x3f)`; **`wlc_phy_mute_upd(pi,1,1)`** (`0xb1c2e`); sets
`dev+0x174=1`. It therefore is not software-only, but it does not run here.

## A.6 `wlc_bmac_switch_macfreq` (`0x64cbf`)

For chip id `0x4352` (`0x64f14..0x64f34`): `si_pmu_get_bb_vcofreq(si,0x28)`,
`bcm_uint64_divide`, then `osl_writew` of the two halves to D11 `+0x62e` and
`+0x630`. Reads PMU BB VCO frequency; writes D11 MAC clock/rate divisors. No
PHY/radio, no PLL/synth write. Chip-dependent, not band/chanspec-dependent.
Called with `freq=0` at `0x695cb`.

## A.7 Initial band / MHF / chanspec state

- Band: `wlc_bmac_init` `0x6830a-0x68329` derives it from the chanspec passed
  in: `band = ((chanspec & 0xc000) == 0xc000) ? 1 : 0`.
- Chanspec: `wlc_init` `0x3c6ee` passes `r14d`, computed via
  `wlc_default_chanspec()` (`0x702a0`) = `wlc_create_chspec` /
  `wlc_next_chanspec` / `wlc_phy_chanspec_band_firstch` -> **runtime/config
  derived, not a static constant**.
- PHY type/rev: for BCM4352 AC the bsinitvals selection requires
  `[dev+0x84]==0x2A` (rev 42) and `[[dev+0xE8]+0x1C]==0xB` (AC type 11).
- bandunit: `dev+0xE8` selected by `wlc_setxband(dev,band)` from
  `dev+0x0f0[band]` (`0x64fb5`).
- MHF host flags: written by `wlc_bmac_mhf` at `0x6855b/0x68578` (before common
  initvals) and `0x66ac9` (after `wlc_phy_init`); the value present during bs
  is whatever the early calls established (SHM `M_HOST_FLAGS`).
- Current MAC frequency: `wlc_bmac_switch_macfreq(dev,0)` immediately before
  band init.
- Radio: **off** (no radio switch on the AC path).

Honest limitation: the exact numeric band/chanspec/MHF used during the initial
BCM4352 bring-up is **not statically recoverable** (config-derived) and was not
recorded by the M3.4D2A/D2B hardware runs.

## A.8 Minimum vendor-faithful prefix before bsinitvals

NOT "the 73 records alone". The faithful prefix is:

`wlc_init` -> `wlc_bmac_init(hw, chanspec, 0)` full body through `0x695d8`,
then `sub_6656c` through the bsinitvals call `0x669bd`; STOP before
`wlc_phy_init` (`0x669df`).

That prefix contains only D11/SHM/objmem/si-PMU accesses plus one D11 core
clock gate (`macphyclk_set`) and the PMU VCO read in `switch_macfreq`. It
contains **no** PHY-indirect write and **no** radio write. (Earlier in
`wlc_bmac_init`, before the common applier, `wlc_phy_chanspec_radio_set`
`0xb1c8a` is a pure store, `wlc_phy_cal_init` `0xb19f7` is software-state only
with no `wlc_phy_cal_init_acphy` callback symbol, and `wlc_phy_antsel_init`
`0xb3351` is a no-op for non-NPHY.)

## A.9 Revised postconditions and roadmap

Postconditions for the 73 records (unchanged, and no post-common stage writes
these targets before bs): SHM `0x0010=0x0014`, `0x001c=0x0183`,
`0x0094=0x01f4`; IHR `0x0680=0x3e3e`, `0x0686=0x09d0`, `0x0700=0x003c`. (These
remain the strongest read-back candidates; the three SHM values depend on the
common initvals having been applied first.)

Revised roadmap (vendor order preserved):

- **D3A** post-common D11 setup tail (`0x68bab..0x695d8`): stages A-K.
- **D3B** band init `sub_6656c` through `d11ac1bsinitvals42` (`0x669bd`);
  STOP before `wlc_phy_init`.
- **D4** AC PHY register init (`wlc_phy_init`: `chanspec_shm_set`,
  `wlc_phy_anacore`, `wlc_phy_switch_radio_acphy`, `wlc_phy_init_aphy`).
- **D5** PHY tables; **D6** radio/channel/synth; **D7** calibration;
  **D8** RX bring-up.

## A.10 Safety disposition of the remaining unknown IHR/SHM values

The five direct IHR records (`0x680,0x682,0x684,0x686` IFS; `0x700` NAV) and
the SHM writes (`0x0990..0x0a28`, `0x17d0/0x17d4`) are passive register/SHM
stores: none can trigger execution, start TX/RX, enable an interrupt, enable
PHY/radio, alter DMA, or initiate calibration (all evidence is `write_shm`/
`osl_writew` to passive targets). Disposition:
**SAFETY-RESOLVED / SEMANTIC-NAME UNKNOWN.**

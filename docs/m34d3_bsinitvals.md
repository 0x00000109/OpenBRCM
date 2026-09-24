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

The formal PHY/RF question is therefore **YES** for "can a vendor-ordered
bsinitvals test stop before real PHY/RF writes?" — but the isolated unit is
**not** "the 73 records alone"; it is the **vendor `wlc_bmac_init` prefix
through the bsinitvals write**. Full details are in **Appendix A** (A.1
switch_radio, A.2 tail stages A-L, A.3 MACCONTROL bit 30, A.4 macphyclk_set,
A.5 mute, A.6 switch_macfreq, A.7 initial band/MHF state, A.8 minimum prefix,
A.9 revised postconditions/roadmap). M3.4D3 remains **ANALYSIS ONLY**.

**SECOND CORRECTION — the tail is NOT D11/SHM-only.** A full reversal of the
rev42 tail (Appendix B) shows that, for BCM4352 rev42:
- the legacy `xmtfifo_sz[]`/`M_FIFOSIZE`/TX-FIFO-flush block is **not
  executed** (gated `phyrev <= 0x27`; §B.0);
- the rev42 FIFO stage is `sub_67efd` (TXE0 FIFO fixup; no DMA/IRQ/PHY; §B.1);
- the tail also writes interrupt source config (`intrcvlazy[0]`,
  `intctrlregs[0].intmask = I_RI`) and **initializes the DMA engines**
  (`dma_txinit` x4, `dma_rxinit`, `dma_rxfill`, posting RX buffers) before band
  init (§B.4/§B.7).

Consequently the D3A/D3B milestones are **NOT YET** (they are PHY/RF-clean but
DMA/IRQ-active), while the "stop before real PHY/RF" boundary remains **YES**.
See §B.11. The earlier "D11/MAC/SHM only" phrasing above and in §A.2/§A.8 is
superseded by Appendix B.

**THIRD CORRECTION — the vendor DMA/IRQ stage is now fully reversed
(Appendix C).** Findings: only **4** TX channels are attached (di[0..3];
`dma_txinit` x4, not x6), the TX engines are enabled but idle (no descriptors
posted, no transmission possible), FIFO0 RX is enabled with 64 buffers posted
(idle), and host IRQ delivery is **not** possible at this point
(`macintmask=0`, `wl_intrsoff` active; `wl_intrson` only in
`wlc_bmac_up_finish` after `wlc_phy_init`). The vendor quiesce is core
reset/disable before freeing. Formal decomposition (Appendix C.18):
**A YES (D3A0), B YES, C YES, D YES**, subject to keeping the host IRQ route
disabled and providing a core-reset quiesce. The earlier "dma_txinit x6" is
corrected to **x4**.

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

YES for the BCM4352/AC path regarding PHY/RF: no PHY-indirect write and no
radio write occurs before bsinitvals.

The vendor prefix is **not** D11/SHM-only: the rev42 tail also initializes the
DMA engines / posts RX buffers (`dma_txinit` x4, `dma_rxinit`, `dma_rxfill`) and
writes interrupt-source config (`intrcvlazy[0]`,
`intctrlregs[0].intmask = I_RI`). Appendix C reverses this fully and shows:
- the TX engines are enabled but **idle** (no descriptors posted → no
  transmission possible); FIFO0 RX is enabled with 64 buffers posted (idle);
- `macintmask` (0x12C) stays 0 and `wl_intrsoff` is active, so **host IRQ
  delivery is not possible** at this point (`wl_intrson` only in
  `wlc_bmac_up_finish`, after `wlc_phy_init`);
- formal decomposition A/B/C/D = **YES** (§C.18), conditional on keeping the
  host IRQ route disabled and providing a core-reset/reboot quiesce.

Remaining caveats:

1. **`ob_dma_quiesce`** not yet implemented/proven → D3A0 reboot-required until
   then (§C.17/C.20).
2. Band/MHF dependency and config-derived initial band/chanspec (§A.7).
3. Postconditions not yet hardware-validated.

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
- **NEW (Appendix B) — the tail is NOT D11/SHM-only.** For rev42 the legacy
  `xmtfifo_sz`/`M_FIFOSIZE`/TX-flush block is skipped (phyrev gate, §B.0); the
  rev42 FIFO stage is `sub_67efd` (§B.1, safe); but the tail also runs
  `dma_txinit` x4 + `dma_rxinit` + `dma_rxfill` (§B.7) and writes interrupt
  source config `intrcvlazy[0]`/`intctrlregs[0].intmask` (§B.4).
- **Stages intentionally included although not PHY:** `MACCONTROL` update
  (`0x69047`), `wlc_bmac_macphyclk_set(1)` (`0x690b1`),
  `wlc_bmac_switch_macfreq(dev,0)` (`0x695cb`), and the DMA-engine init
  (`0x6921c`). `wlc_bmac_mute` and the `0x69594` `wlc_phy_switch_radio` are
  **not** part of the AC path.
- **MMIO write classes:** D11 register writes (`osl_writel`/`osl_writew` to
  `0x24,0x100,0x120,0x128,0x188,0x18c,0x62e,0x630,…`), TXE0 FIFO regs
  (`0x52x/0x54x` via `sub_67efd`), D11 core cflags (`si_core_cflags`),
  `write_shm`/`copyto_objmem` (SHM/objmem), PMU read (`get_bb_vcofreq`), and
  **DMA channel registers via `dma_txinit`/`dma_rxinit`/`dma_rxfill`**.
  **No PHY-indirect write, no radio write.**
- **Delays/polls:** D2A PSM poll (D2B) + 2 bounded `sub_67efd` polls;
  `switch_macfreq` has no delay. No unbounded poll.
- **Deterministic postconditions:** §B.12 (D3A) and §B.12 (D3B).
- **Residual state:** `MACCONTROL = 0x44020402`, PSM_RUN=1, EN_MAC=0,
  `macintmask=0`, MAC-PHY clock on, D11 core on, DMA engines initialized / RX
  buffers posted, common + tail + bsinitvals applied; **no PHY/radio**.
- **Decision:** the DMA/IRQ content is now fully reversed (Appendix C); the
  vendor-faithful split is D3A0 (DMA/IRQ-source, host route off) → D3A1
  (remaining tail) → D3B, all **YES** isolatable (§C.18), conditional on
  `ob_dma_quiesce`/reboot policy. This remains a **design proposal only** and
  must not be implemented as-is.

## 18. AC PHY follow-on roadmap (revised, no implementation)

Keep these separate; do not collapse (vendor order preserved):

- **D3A0 — vendor DMA/IRQ-source bring-up** (Appendices C/D): program the **4**
  TX channels (BK/BE/VI/VO) and FIFO0 RX (64 buffers posted) exactly as the
  vendor, with the **host IRQ route kept disabled** (`macintmask=0`,
  `bcma_host_pci_irq_ctl=false`, no `I_RI`/`MI_DMAINT`); read-only
  postconditions; then `ob_dma_quiesce` (per-channel reset + core disable) and
  free. Status: **`D3A0 IMPLEMENTATION GO: YES`** with same-run teardown
  (§D.19); still `NOT IMPLEMENTED` / `NOT HARDWARE PROVEN`.
- **D3A1 — remaining rev42 tail** (`0x6930e..0x695d8`): NVRAM/BTC SHM tables,
  `tsf`, `MACCONTROL`, `macphyclk_set`, `switch_macfreq`. D11/SHM only.
  Status: **YES** (§C.18).
- **D3B — band init + bsinitvals** (`sub_6656c` through `0x669bd`): apply the 73
  records; STOP before `wlc_phy_init`. Status: **YES** (§C.18).
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
  See §A.6 / §B.6 / §B.8.
- ~~`sub_67efd` / TX-FIFO stage~~ -> `sub_67efd` = TXE0 FIFO fixup + `machwcap`
  read; the legacy `xmtfifo_sz`/`M_FIFOSIZE`/TX-flush block is skipped for
  rev42. See §B.1/§B.3.
- ~~Is the tail D11/SHM-only?~~ -> **NO**; it also inits DMA engines and writes
  interrupt-source masks. See §B.4/§B.7.

Resolved by Appendix C (DMA reversal):
- ~~"dma_txinit x6"~~ -> **x4** (only di[0..3] attached; §C.0).
- ~~DMA/IRQ blocks D3A/D3B?~~ -> DMA init is **isolatable as D3A0**; host IRQ
  delivery is **not** possible there (`macintmask=0`, `wl_intrsoff`);
  formal A/B/C/D = **YES** (§C.18).
- ~~D3A0 blockers~~ -> closed in Appendix D: 4-TX map, TX CONTROL RMW formula,
  `ddoffsethigh=0x80000000`, `intrcvlazy[0]=0x01000000`, `dma_txreset 0xf64a` /
  `dma_rxreset 0xf5ef`, quiesce = per-channel reset + `bcma_core_disable`.
  **`D3A0 IMPLEMENTATION GO: YES`** (§D.19).

Still unknown / open decisions:
1. Meaning of the 5 direct IHR fields (`0x680/0x682/0x684/0x686` IFS,
   `0x700` NAV) — values proven, field names UNKNOWN.
3. The exact band/MHF state used during the initial BCM4352 bring-up (not
   measured on hardware). `wlc_default_chanspec()` is function-derived, not a
   static constant.
4. Whether `sub_6656c`'s `osl_readw(D11+0x3e0)` result gates the bsinitvals
   selection (its value is not used in the observed path).
5. Exact consumer of the SHM rate/BTC regions (`base+2i`, `0x78c..0x790`).
6. Whether `wlc_phy_cal_init`'s optional `*(pi+0x30)` callback is NULL for AC
   (no `wlc_phy_cal_init_acphy` symbol exists; the other PHYs install one).

## 20. Analysis commit hash

- `ef4be46` — `m34d3: analyze rev42 band-switch initvals and PHY boundary`
  (initial report + classifier).
- `b7d4376` — `m34d3: resolve switch_radio ordering; correct PHY/RF boundary`
  (first correction: §0, Appendix A, revised §16/§17/§18/§19).
- `da2bb4a` — `m34d3: reverse post-common tail; DMA/IRQ content blocks D3A/D3B`
  (second correction: Appendix B, §0/§16/§17/§18/§19).
- `1f32300` — `m34d3: reverse vendor DMA/IRQ stage; A/B/C/D isolation YES`
  (third correction: Appendix C, §0/§16/§17/§18/§19).

All DCO signed, on `m34d3-bsinitvals-analysis`; no driver code and no hardware
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

---

# Appendix B — post-common tail reversal (D3A/D3B safety)

Read-only RE of `wlc_hybrid.o_shipped` (sha256 `352a6e349f…`). No hardware/MMIO
executed. Addresses are `.text` offsets. This appendix **supersedes** the
"tail is D11/SHM-only" simplification in §A.2/§A.8.

## B.0 rev42 path correction (phyrev gating)

The `xmtfifo_sz[]` build / TX-FIFO ctrl-flush / `M_FIFOSIZE` host override block
(`0x68bb5..0x68df7`) is reached only when `phyrev <= 0x27`
(`wlc_bmac_init` `0x68b9d`: `cmp $0x27,%edi; jbe 0x68bb5`). BCM4352 rev42
(`0x2a > 0x27`) takes:

```
0x68b98  sub_60f67(dev, d11ac1initvals42)     [D2B]
0x68b9d  if (phyrev > 0x27) {
0x68bab      sub_67efd(dev)                    ; TXE0 FIFO fixup
0x68bb0      jmp 0x68fe2
         }
```
So for rev42: **no `xmtfifo_sz[]` build, no host `M_FIFOSIZE` override, no
`0x68d04..0x68d57` TX-FIFO flush**. Those belong to legacy (corerev <= 0x27)
parts. The §A.2 table rows B/C/D/E and the earlier "M_FIFOSIZE host override"
claim do **not** apply to BCM4352 rev42.

## B.1 `sub_67efd` — TXE0 FIFO fixup (the real rev42 FIFO stage)

`sub_67efd` (`0x67efd`, size `0x38d`) is the vendor equivalent of C3
`brcms_b_corerev_fifofixup()` (`main.c:2036`): it resets and defines the TXE0
transmit FIFOs and reads `machwcap` to size the RX header.

Operations (exact):

- `osl_readl(D11+0x15c)` = `machwcap` (read-only); result `>>1 & 0xffc` stored
  to two driver globals (`.bss+0xb34`, `.data+0x16c`). **No write.**
- phyrev `<= 0x2a` or `== 0x2c` -> skip the RXE block; rev42 takes this branch.
  (The RXE writes `0x406 rcv_fifo_ctl`, `0x42c/0x42e`, `0x43a/0x43c` at
  `0x67f73..0x67fd6` are for rev > 0x2a and are **not** executed for rev42.)
- `osl_writew(D11+0x542)` `xmtfifoflush` <- machwcap-derived value.
- `osl_writew(D11+0x540)` `xmtfifocmd` <- `5`; poll `osl_readw(D11+0x540)` bit0
  until clear, bounded (`0xd1` down in 10s, <= ~20 x 10us).
- loop 7 entries (`0x680b8..0x6819f`): per entry `osl_writew` to
  `0x54a xmtfiforqpri`, `0x54c xmttplatetxptr`, `0x520 xmtfifodef`,
  `0x54e`, `0x550 xmttplateptr`, `0x548 xmtfifoprirdy` -> 42 writes.
- loop 42 entries (`0x681da..0x6826d`): per entry `osl_writew` to
  `0x534`, `0x536`, `0x532`, `0x530` -> 168 writes; poll `osl_readw(D11+0x530)`
  until 0, bounded (`0xd1` down in 10s).

All offsets are D11 **TXE0 transmit-control** (`0x520..0x550`) and unnamed PAD
regs `0x530..0x536`. Runtime write count ~212 `osl_writew`, 4 `osl_readw`
(polls), 1 `osl_readl`; two bounded polls; 2 `osl_delay`.

```
DMA ENGINE START EFFECT = NONE
RX ENGINE START EFFECT  = NONE
TX ENGINE START EFFECT  = NONE
```
(No access to the DMA register block `0x200-0x37F`, no `maccontrol`, no
`intctrlregs`/`macintstatus`/`macintmask`, no PHY/radio. TXE0 FIFO reset/flush
only; `EN_MAC` unaffected; C3 calls the equivalent with MAC not enabled.)

**sub_67efd safe to include in a D11-side step: YES** (bounded, deterministic,
no DMA/IRQ/PHY).

## B.2 TX FIFO ctrl/flush stage

For rev42 the `0x68d04..0x68d57` TX-FIFO flush sequence is **not executed**
(§B.0). The only rev42 FIFO control is `sub_67efd` (§B.1).

```
TX FIFO STAGE SAFE FOR ISOLATED TEST = YES   (rev42: only sub_67efd, EN_MAC=0)
```
Caveat: `sub_67efd` resets/flushes TXE0 FIFO definitions; it does not arm DMA
and does not set `EN_MAC`.

## B.3 `xmtfifo_sz[]` / `M_FIFOSIZE` host override

**Not executed for rev42** (§B.0). Therefore the vendor rev42 tail does not
overwrite `M_FIFOSIZE0..3`; the values observed after D2B
(`01c4 / 0000 / 0000 / 079e`) **persist** through the tail into band init.
They remain valid D3A postconditions (`CONSTANT` w.r.t. the rev42 tail; their
origin is the ucode/common-initvals, not this tail).

## B.4 rev42 tail inventory (`0x68fe2..0x695d8`)

Ordered (chip = BCM4352, so all chip-specific branches are noted):

1. `wlc_bmac_write_shm(0x80, 8)`.
2. `wlc_bmac_write_shm(0x5c, 0xa)`.
3. `osl_writel(D11+0x100, *(dev+0x1ac))` — `intrcvlazy[0]` (RX interrupt
   coalescing count; runtime value).
4. `wlc_bmac_mctrl(dev, 0x40060000, 0x40020000)` (§B.5).
5. `osl_writel(D11+0x188, 0x80000000)` — `tsf_cfprep`.
6. `osl_writel(D11+0x18c, 0x02000000)` — `tsf_cfpstart`.
7. `osl_writel(D11+0x128, 0x4000)` — `macintstatus` (W1C, `MI_GP1`).
8. `osl_writel(D11+0x24, 0x10000)` — `intctrlregs[0].intmask = I_RI`.
9. `wlc_bmac_macphyclk_set(dev, 1)` (§B.6).
10. `si_clkctl_fast_pwrup_delay` -> `osl_writew(D11+0x6a8, ax)`
    (`scc_fastpwrup_dly`; PMU-dependent).
11. `sub_5fdca`/`wlc_bmac_mhf_get` value added to `*(dev+0x192)`.
12. `write_shm(0x16, phyrev=0x2a)` (`M_MACHW_VER`).
13. `write_shm(0xc0, *(dev+0xa4))`, `write_shm(0xc2, *(dev+0xa6))`
    (`M_MACHW_CAP_L/H`, from `machwcap`).
14. `wlc_bmac_copyto_objmem` x3 (SCR retry limits / rate table; D11 objmem).
15. `osl_readw`/masked `osl_writew(D11+0x688)` (`ifs_ctl`),
    `osl_writew(D11+0x69c, 1)` (`ifs_aifsn`).
16. **DMA engine init** (§B.7): `for i in 0..5 if (di[i]) dma_txinit(di[i])`;
    `dma_rxinit(di[0])`; `dma_rxfill(di[0])`.
17. Chip-specific TSF-clock blocks (`0x69273..0x6930d`) skipped for `0x4352`.
18. `read_shm(0x92)` -> base; NVRAM `"btc_params%d"` loop over 119 rates ->
    `write_shm(base + 2*i)`; for `0x4352` four extra `write_shm`
    (`0x7530`, `0x4e20`, `0x7530`, `0x753`); NVRAM `"btc_flags"` ->
    `wlc_bmac_btc_flags_idx_set`.
19. phyrev > 0x27: `write_shm(0x78c/0x78e/0x790, <6 wlc bytes>)`.
20. `read_shm(0x8e)`; phyrev == 0x21 block skipped for rev42.
21. `wlc_bmac_mute` **skipped** (arg#3 = 0).
22. `wlc_phy_switch_radio` **skipped** (NPHY/HT `phy_type == 7` only).
23. `wlc_bmac_switch_macfreq(dev, 0)` (§B.8).
24. `sub_6656c(dev, chanspec, band=0)` (§B.9).

No PHY-indirect write and no radio write occurs anywhere in this list.

## B.5 MACCONTROL transition (exact) and persistence

- `wlc_bmac_mctrl` (`0x6066d`) is cached: `shadow=*(dev+0x168)`;
  `new = (shadow & ~mask) | val`; if changed, store shadow and write
  `D11+0x120` (`0x605ad`).
- At `0x69047`: `mask = 0x40060000`, `val = 0x40020000`.
- Bits (`d11.h`): `MCTL_DISCARD_PMQ=1<<30`, `MCTL_AP=1<<18`, `MCTL_INFRA=1<<17`,
  `MCTL_WAKE=1<<26`, `MCTL_IHR_EN=1<<10`, `MCTL_PSM_RUN=1<<1`.
- From the D2B exit value `0x04020402`:
  `old=0x04020402`, `new=0x44020402` (bit30 set, AP cleared, INFRA set,
  PSM_RUN=1, EN_MAC(bit0)=0, SHM_EN(bit8)=0).
- Persistence: `macphyclk_set` writes core cflags (not MACCONTROL);
  `switch_macfreq` writes D11 `0x62e/0x630`; `sub_6656c` pre-bs writes only
  SHM; the bsinitvals applier writes `0x160/0x164/0x166/0x680..0x700`.
  **`MACCONTROL` is unchanged from `0x69047` until `wlc_phy_init`**
  (`wlc_phy_init` merely reads it at `0xbac44` before the first PHY write).

## B.6 `wlc_bmac_macphyclk_set` safety

`wlc_bmac_macphyclk_set(dev,on)` (`0x65006`) =
`si_core_cflags(D11 si, SICF_MPCLKE=0x10, on?0x10:0)` (`d11.h:1740`:
"MAC PHY clock control enable"; C3 `main.c:1729`). It is a D11 **core cflags**
gate, fully reversible, no PHY/radio MMIO, no PLL; it lets the MAC dynamically
gate the PHY clock. Called ON at `0x690b1` before band init.

```
MACPHYCLK_SET SAFE FOR D11-ONLY TEST = YES
```
It is not strictly needed for the pure D11 SHM/IHR bsinitvals writes, but it is
vendor-order and does not itself cause PHY activity.

## B.7 DMA engine initialization (NEW HARD BLOCKER)

`wlc_bmac_init` `0x6921c..0x69243`:
```
for (i = 0; i < 6; i++)            ; dev+0x20 = di[0..5] (wlc_hw_set_di)
    if (dev->di[i]) di[i]->vfn(+0x08)(di[i]);   ; == dma_txinit
di[0]->vfn(+0xa0)(di[0]);          ; == dma64_rxinit
di[0]->vfn(+0xd8)(di[0]);          ; == dma64_rxfill
```
- `di[]` verified: `wlc_bmac_attach` calls `dma_attach` (4 sites) and
  `wlc_hw_set_di` (`0x796a7`: `mov %rdx,0x20(%rdi,%rsi,8)`).
- `dma_attach` stores vtable `&dma64proc` (`0x27d620`) into `obj+0`.
  `dma64proc+0x08 = 0xf947` (dma64_txinit: writes DMA channel ctrl at
  `obj+0x48`), `+0xa0 = 0xf897` (dma64_rxinit: resets RX ring), `+0xd8 = 0xf14d`
  (dma64_rxfill: `osl_pktget` + posts RX buffers).
- This touches the DMA register block and posts RX buffers **before** band
  init/bsinitvals.

```
DMA ENGINE START EFFECT = TX DMA channels reset/initialized (no frames queued);
                          RX DMA channel initialized and enabled; RX buffers posted
                          (dma_rxinit + dma_rxfill)
RX ENGINE START EFFECT  = RX DMA channel enabled + RX buffers posted
TX ENGINE START EFFECT  = TX channels initialized (not armed with data)
```
With `EN_MAC=0` the MAC delivers no frames, so no DMA data transfer is expected,
but the DMA engines are no longer in the D2B "untouched" state.

## B.8 `wlc_bmac_switch_macfreq` safety (`0x64cbf`, chip 0x4352)

- `si_pmu_get_bb_vcofreq(si, 0x28)` (PMU read) -> `bcm_uint64_divide` ->
  `osl_writew(D11+0x62e, lo)` and `osl_writew(D11+0x630, hi)`.
- `0x62e/0x630 = tsf_clk_frac_l / tsf_clk_frac_h` (TSF/MAC timer fractional
  clock). Values are **runtime PMU-VCO-derived**, not band/chanspec-dependent.
  No PHY/radio write, no PLL/synth write, no state-machine start.

```
SWITCH_MACFREQ SAFE FOR ISOLATED TEST = YES
```
Readback is meaningful (`0x62e/0x630`); use equality only if the driver can
derive the same value from `si_pmu_get_bb_vcofreq`, otherwise treat as
`DERIVED / runtime`.

## B.9 `sub_6656c` pre-bs (proof of D11/SHM-only)

Before the bsinitvals call `0x669bd`, `sub_6656c` executes only:

1. `si_seci_upd` only for chip `0xa9a7`/`0x4331` (`0x66582..0x665b0`); **skipped
   for BCM4352**.
2. `osl_readw(D11+0x3e0)` = `phyversion` (read-only status; result not used).
3. `sub_62766(dev, phy+8)` (`0x665d6`) = five `wlc_bmac_write_shm` to
   `0x5e, 0x60, 0x62, 0x78, 0xd4` from the table at `*(phy+8)`. No PHY/radio.
4. The bsinitvals table-selection switch (`0x665db..0x669ba`): only `mov`
   immediates + compares on `dev+0x84`/`phy+0x1c`.

```
sub_6656c PRE-BS IS D11/SHM-ONLY = PROVEN
```
(The `D11+0x3e0` access is a plain read of the PHY-version status register, not
a PHY indirect command; no write to `0x3fc/0x3fe`.)

## B.10 bsinitvals table accounting (re-proven)

Parsed from `bcm4352-d11ac1bsinitvals42.bin` (592 B, terminator `0xffff` at
record index 73, byte 584):

| record kind | count | offset/width | access |
|---|---|---|---|
| OBJADDR | 34 | `0x160`, w4 | `osl_writel(D11+0x160, 0x10000\|idx)` |
| OBJDATA low | 15 | `0x164`, w2 | `osl_writew(D11+0x164, val)` |
| OBJDATA high | 19 | `0x166`, w2 | `osl_writew(D11+0x166, val)` |
| direct IHR | 5 | `0x680,0x682,0x684,0x686,0x700`, w2 | `osl_writew(D11+off, val)` |
| **total** | **73** | 34 x w4 + 39 x w2 | strict order, terminator skipped |

Applier `sub_60f67` (`0x60f67`, wrapper `wlc_bmac_write_inits` `0x60fce`):
writes `osl_writew`/`osl_writel` directly to `D11_base + offset`, one MMIO
access per record. **Total table MMIO writes = 73** (34 `writel` + 39
`writew`). No auto-increment, no loop beyond 73 records, terminator never
written. This is a **D11-only** sequence: `0x160/0x164/0x166` are the
object-memory window registers and `0x680..0x700` the IFS/NAV block; no DMA,
IRQ, PHY or radio offsets.

## B.11 D3A / D3B boundaries and GO decisions

Proposed split (vendor order preserved):

- **D3A** = D2B state -> full rev42 post-common tail (§B.1 `sub_67efd` through
  §B.8 `switch_macfreq`), STOP immediately before `sub_6656c`.
- **D3B** = D3A state -> `sub_6656c` pre-bs (§B.9) -> exactly the 73 bsinitvals
  records -> deterministic readback -> STOP before `wlc_phy_init`.

The split preserves vendor semantics (D3A ends at `0x695d8`; D3B starts at
`0x6656c`). Both are real link points.

Formal decisions:

```
D3A POST-COMMON TAIL ISOLATABLE?                    NOT YET
D3B BAND INIT + BSINITVALS ISOLATABLE FROM D3A?     NOT YET
COMPLETE VENDOR PREFIX THROUGH BSINITVALS CAN STOP
BEFORE REAL PHY/RF?                                 YES
```

Rationale:
- PHY/RF: **none** of the tail or bsinitvals writes any PHY-indirect or radio
  register; the first such writes are inside `wlc_phy_init` after bsinitvals.
  So the PHY/RF boundary holds (YES).
- DMA/IRQ: the tail contains `dma_txinit` x4, `dma_rxinit`, `dma_rxfill`
  (§B.7) and interrupt-source writes (`intrcvlazy[0]`, `intctrlregs[0].intmask
  = I_RI`) (§B.4 items 3/8). These exceed the D2B envelope and violate the
  "no uncontrolled DMA start / no IRQ enable" criteria, so D3A/D3B are NOT YET
  until either (a) the DMA/IRQ tail operations are analyzed and explicitly
  accepted as controlled, or (b) they are covered by a separately proven
  milestone. (`MACINTMASK`/`macintmask` `0x12C` remains 0, so host interrupt
  delivery stays masked; `MCTL_EN_MAC` stays 0.)

## B.12 Deterministic postconditions

D3A (after the tail, before `sub_6656c`):

| item | expected | class |
|---|---|---|
| `MACCONTROL` (0x120) | `0x44020402` | DERIVED (from D2B state) |
| `MACINTMASK` (0x12C) | `0` | CONSTANT |
| `M_FIFOSIZE0..3` | `01c4/0000/0000/079e` | CONSTANT w.r.t. rev42 tail |
| `intctrlregs[0].intmask` (0x24) | `0x10000` | CONSTANT |
| `intrcvlazy[0]` (0x100) | `*(dev+0x1ac)` | RUNTIME (derive from same input) |
| `tsf_cfprep` (0x188) | `0x80000000` | CONSTANT |
| `tsf_cfpstart` (0x18c) | `0x02000000` | CONSTANT |
| `tsf_clk_frac_l/h` (0x62e/0x630) | PMU-VCO-derived | DERIVED (needs PMU read) |
| D11 core cflags bit4 | 1 | CONSTANT (`SICF_MPCLKE`) |
| TXE0 regs (0x52x/0x54x) | FIFO-fixup values | DERIVED (bounded) |
| BTC/rate SHM (`base+2i`, `0x78c..0x790`, `0xc0/0xc2`) | NVRAM-derived | BOARD-DEPENDENT |

D3B (after bsinitvals, before `wlc_phy_init`):

| item | expected | class |
|---|---|---|
| SHM `0x0010` | `0x0014` | CONSTANT (common overwritten by bs) |
| SHM `0x001c` | `0x0183` | CONSTANT |
| SHM `0x0094` | `0x01f4` | CONSTANT |
| IHR `0x0680` | `0x3e3e` | CONSTANT |
| IHR `0x0686` | `0x09d0` | CONSTANT |
| IHR `0x0700` | `0x003c` | CONSTANT |
| `MACCONTROL` (0x120) | `0x44020402` | DERIVED |
| `MACINTMASK` (0x12C) | `0` | CONSTANT |

None of the D3A tail writes nor the `sub_6656c` pre-bs SHM writes
(`0x5e/60/62/78/d4`) touch the six D3B targets before the applier runs, so they
survive to readback.

## B.13 Failure / residual-state matrix

| stage fails at | residual PSM | EN_MAC | clock | FIFO | SHM | retry w/o reset |
|---|---|---|---|---|---|---|
| `sub_67efd` mid-loop | PSM_RUN=1 | 0 | core on | TXE0 partial | D2B | NOT proven |
| `M_FIFOSIZE` | n/a (skipped rev42) | 0 | core on | n/a | D2B | n/a |
| MACCONTROL | PSM_RUN | 0 | core on | TXE0 done | D2B | unknown |
| macphyclk_set | PSM_RUN | 0 | MAC-PHY clk on | done | D2B | reversible |
| DMA init | PSM_RUN | 0 | on | TX/RX DMA init'd, RX buffers posted | D2B | **NOT safe** |
| switch_macfreq | PSM_RUN | 0 | on | done | D2B | unknown |
| D3B pre-bs SHM | PSM_RUN | 0 | on | done | partial | unknown |
| partial bsinitvals | PSM_RUN | 0 | on | done | partial mix | reset-only |
| postcondition mismatch | PSM_RUN | 0 | on | done | applied | reset-only |

Policy: because exact unwind of DMA-engine init and partial SHM writes is not
proven, use a **one-shot / reboot-or-full-reinit** policy. Do not invent reset
logic. (DMA init is the strongest argument for one-shot.)

## B.14 Side-effect accounting (separate per stage)

- **D3A tail** (`0x68fe2..0x695d8`, static call sites; runtime loops noted):
  - direct D11 MMIO writes: `intrcvlazy[0]`, mctrl(1), `tsf_cfprep`,
    `tsf_cfpstart`, `macintstatus`, `intctrlregs[0].intmask`,
    `scc_fastpwrup_dly`, `ifs_ctl` (RMW), `ifs_aifsn`, `tsf_clk_frac_l/h` = 10
    (plus `sub_67efd` ~212 bounded writew).
  - SHM writes: 18 `wlc_bmac_write_shm` call sites (incl. NVRAM 119-loop and
    BTC/rate values) + 3 `copyto_objmem`.
  - core-control writes: 1 (`si_core_cflags` via `macphyclk_set`) +
    `si_clkctl_fast_pwrup_delay` (read).
  - FIFO-control writes: ~212 `osl_writew` in `sub_67efd` (TXE0).
  - MACCONTROL writes: 1.
  - DMA-control writes: 6 `dma_txinit` + 1 `dma_rxinit` + 1 `dma_rxfill`
    (each writes DMA channel regs internally).
  - interrupt-enable writes: 2 (`intrcvlazy[0]`, `intctrlregs[0].intmask`).
  - PHY writes: 0. radio writes: 0.
  - bounded polls: 2 (in `sub_67efd`) + PMU read.
- **D3B pre-bs** (`sub_6656c` `0x6656c..0x669ba`):
  - direct MMIO writes: 0; reads: 1 (`osl_readw D11+0x3e0`).
  - SHM writes: 5 (`sub_62766`).
  - PHY/radio/DMA/IRQ: 0. polls: 0.
- **73 bsinitvals**: 34 OBJADDR `writel` + 15 `writew` @0x164 + 19 `writew`
  @0x166 + 5 IHR `writew` = **73 MMIO writes**; PHY/radio/DMA/IRQ: 0; polls: 0.

Nothing is omitted: the only non-trivial additions versus §A.2 are the DMA
engine init and the two interrupt-config writes.

## B.15 Corrected roadmap

Because D3A/D3B are NOT YET, and the blocker is the DMA/IRQ content of the
vendor tail (not PHY/RF), the recommended decomposition is:

1. **D3A0 (analysis/design)** — decide how the driver's own DMA bring-up
   (M3.2/M3.3/M3.4B) maps onto the vendor tail's `dma_txinit/rxinit/rxfill`,
   and whether `intrcvlazy/intctrlregs` are acceptable with `macintmask=0`.
2. **D3A** — post-common tail reusing the proven driver DMA layer, when the
   DMA/IRQ question is resolved.
3. **D3B** — band init + 73 bsinitvals, STOP before `wlc_phy_init` (PHY/RF-CLEAN
   already).
4. **D4** — `wlc_phy_init` -> `wlc_phy_anacore` (first PHY indirect MMIO).
5. D5 PHY tables; D6 radio/channel/synth; D7 calibration; D8 RX.

If D3A/D3B cannot be separated, run one combined D3 test that includes the DMA
init; but that test is **not** hardware-safe under the current no-DMA policy and
must not be attempted without resolving D3A0.

---

# Appendix C — vendor DMA/IRQ reversal (D3A0)

Read-only RE of `wlc_hybrid.o_shipped` (sha256 `352a6e349f…`) plus inspection of
the current OpenBRCM `src/ob_dma.*`, `src/ob_irq.*`, `src/ob_rx.*` and
`docs/dma_architecture.md` / `docs/rx_path.md`. No hardware, no MMIO, no
implementation. This supersedes the "dma_txinit x6" wording of §B.7/B.11.

## C.0 Correction: four TX DMA channels, not six

`wlc_bmac_attach` has exactly **4** `dma_attach` calls (`0x6a3d7`, `0x6a4c7`,
`0x6a566`, `0x6a65e`) and exactly **4** `wlc_hw_set_di` stores with
`fifo = 0,1,2,3` (`0x6a459`, `0x6a4f8`, `0x6a597`, `0x6a69d`). `di[4]` and
`di[5]` stay NULL. The init loop `wlc_bmac_init 0x6921c` iterates 6 slots with
`if (di[i])`, so on the BCM4352 initial path it executes **4 x `dma_txinit`**
(not 6), then `dma_rxinit(di[0])` (`0x69236`) and `dma_rxfill(di[0])`
(`0x69243`).

Vendor FIFO mapping (C3 `brcms_b_attach_dmapio` + attachment order):

| di | role | TX reg base | RX reg base |
|---|---|---|---|
| 0 | `TX_AC_BK_FIFO` + `RX_FIFO` | `0x200` | `0x220` |
| 1 | `TX_AC_BE_FIFO` | `0x240` | – |
| 2 | `TX_AC_VI_FIFO` | `0x280` | – |
| 3 | `TX_AC_VO`/`TX_CTL_FIFO` | `0x2C0` | – |
| 4,5 | not attached | – | – |

(Older `corerev <= 0xA` use 0x20 spacing 0x200/0x220/0x240/0x260; BCM4352
`phyrev 0x2A > 0xA` uses the 0x40 spacing above. The attach code selects by
`[dev+0x84] > 0xA`.)

## C.1 Exact vendor DMA/IRQ ordering in the rev42 tail

```
0x68b98  sub_60f67(dev, d11ac1initvals42)              [D2B]
0x68bab  sub_67efd(dev)                                 TXE0 FIFO fixup
0x68fe2  wlc_bmac_write_shm(0x80, 8); (0x5c, 0xa)
0x69006  osl_writel(D11+0x100, *(dev+0x1ac))            intrcvlazy[0]
0x69047  wlc_bmac_mctrl(mask=0x40060000, val=0x40020000)
0x6904c  osl_writel(D11+0x188, 0x80000000)              tsf_cfprep
0x6905e  osl_writel(D11+0x18c, 0x02000000)              tsf_cfpstart
0x69070  osl_writel(D11+0x128, 0x4000)                  macintstatus W1C (MI_GP1)
0x69082  osl_writel(D11+0x24, 0x10000)                  intctrlregs[0].intmask = I_RI
0x690b1  wlc_bmac_macphyclk_set(dev, 1)                 SICF_MPCLKE
0x690b6  si_clkctl_fast_pwrup_delay -> D11+0x6a8
0x690e2  mhf_get -> dev+0x192
0x690fa  write_shm(0x16, phyrev); (0xc0, ..); (0xc2, ..)
0x6913f  copyto_objmem x3 (SCR retry / rate)
0x691d6  ifs_ctl RMW; 0x691ff ifs_aifsn <- 1
0x6921c  for i in 0..5: if (di[i]) dma_txinit(di[i])   -> 4 calls
0x69236  dma_rxinit(di[0])
0x69243  dma_rxfill(di[0])                              posts RX buffers
0x6930e  read_shm(0x92) + NVRAM "btc_params%d" x119 -> write_shm
0x69466  write_shm(0x78c/0x78e/0x790)
0x694d8  read_shm(0x8e)
0x6955b  wlc_bmac_mute                                 SKIPPED (arg#3=0)
0x69580  wlc_phy_switch_radio                          SKIPPED (phy_type!=7)
0x695cb  wlc_bmac_switch_macfreq(dev, 0)               D11 0x62e/0x630
0x695d8  sub_6656c -> bsinitvals (0x669bd) -> wlc_phy_init (0x669df)
```

`wl_intrsoff` is called at `wlc_bmac_init 0x682d6` (before all of the above) and
`wl_intrsrestore` at `0x695ee` (after `wlc_phy_init`). So MAC aggregate
interrupts are masked across the whole DMA init / bsinitvals window.

## C.2 The four TX DMA channels (`dma64_txinit` @ `0xf947`)

Object (`dma_info`, 0x130 B): `+0x48` TX reg base, `+0x50` RX reg base,
`+0x58` TX ring ptr, `+0x60` RX ring ptr, `+0x6a` `ntxd`, `+0xa4` `nrxd`,
`+0xe4` `rxbufsize`, `+0xe8` extra headroom, `+0xec` `nrxpost`,
`+0xf0` `rxoffset`, `+0x104` `aligndesc_4k`, `+0xf4/0xf8` `ddoffsetlow/high`.

`dma64_txinit(di)`:
1. if `ntxd == 0` return.
2. `di->txin = di->txout = 0`; `txavail = ntxd - 1`; `obj+0x8 = ntxd-1`.
3. `memset(tx-ring, 0, ntxd*16)` (descriptor = 16 B).
4. RMW `control` (`regbase+0x00`) with capability fields extracted from
   `obj+0x106/107/108/109`, then `OR 0x1` (`XE`, transmit enable) `[| 0x800`
   (`PD`) when parity not supported]`.
5. `_dma_ddtable_init(di, TX, ring_pa)` (`0xe66f`): `addrlow = pa + ddoffsetlow`
   (`regbase+0x08`), `addrhigh = ddoffsethigh` (`regbase+0x0C`), and address
   extension bits into `control AE`.
6. `ptr` (`regbase+0x04`) set to the ring base; **no descriptor is posted**.

Per-FIFO TX register set is identical (same dma64 layout, different base):
`control`/`ptr`/`addrlow`/`addrhigh`/`status0`/`status1` at `base+0x00/0x04/
0x08/0x0C/0x10/0x14`. Ring size `ntxd`, ring bytes `ntxd*16`, alignment is
governed by `aligndesc_4k` (`obj+0x104`; BCM4352 takes the aligned path).

## C.3 Vendor TX DMA vs current OpenBRCM TX model

| property | vendor | OpenBRCM (`ob_dma.h`) | status |
|---|---|---|---|
| descriptor format | DMA64 16 B (`dma64desc`) | 16 B `ob_dma_desc` | MATCH |
| TX ring count | `ntxd` (runtime; C3 NTXD=512) | 512 | MATCH |
| ring bytes | `ntxd*16` (8192) | 8192 | MATCH |
| ring alignment | 8 KiB (aligned path) | 8192 | MATCH |
| addrhigh semantics | `ddoffsethigh` (0x80000000) | `OB_DMA_PCIE_H32=0x80000000` | MATCH (M3.4B) |
| 32-bit DMA window | yes | yes | MATCH |
| **number of TX channels** | **4** (BK/BE/VI/VO) | **1** | DIFFERENT |
| TX control programming | XE + capability fields (enabled) | not implemented | NOT IMPLEMENTED |
| TX ring base published | yes (`addrlow/high`, `ptr`) | no | NOT IMPLEMENTED |
| TX descriptors posted | no (empty ring) | no | MATCH |

Current OpenBRCM `ob_dma_init` **cannot** be used directly for the vendor-faithful
pre-PHY stage: it allocates only ONE TX ring and does no hardware programming.

## C.4 RX `dma64_rxinit` / `dma64_rxfill`

`dma64_rxinit(di)` @ `0xf897`: if `nrxd==0` return; `rxin=rxout=0`;
`memset(rx-ring, 0, nrxd*16)`; `_dma_ddtable_init(di, RX, ring_pa)`;
`_dma_rxenable(di)`.
`_dma_rxenable` @ `0xe5f0`: `control = RE(0x1) | (read(control) & AE) |
[PD=0x800] | (rxoffset << 1)` written to `regbase+0x00`.
`dma64_rxfill(di)` @ `0xf14d`: for `i < nrxpost`: allocate skb of `rxbufsize`,
`osl_pktget`, program descriptor (`ctrl2 = size`, EOT on last slot),
`addrlow/high`; then `ptr = rcvptrbase + rxout*16` (`regbase+0x04`).

## C.5 Vendor RX vs OpenBRCM M3.4B

| field | vendor (derived) | OpenBRCM / M3.4B (proven) | status |
|---|---|---|---|
| FIFO/reg block | FIFO0 RX `0x220` | `0x220..0x22C` | MATCH |
| ring descriptor count | `nrxd` (C3 NRXD=256) | 256 | MATCH |
| descriptor size | 16 B | 16 B | MATCH |
| ring alignment | 8 KiB | 8192 | MATCH |
| posted buffers | `nrxpost` (C3 NRXBUFPOST=64) | 64 | MATCH |
| buffer size | `rxbufsize` | 2048 | MATCH |
| rxoffset | `di+0xf0` | 38 | MATCH |
| CONTROL | `RE \| PD \| (rxoffset<<1)` | `0x0000084D` | MATCH |
| ADDRLOW | ring base | ring base | MATCH |
| ADDRHIGH | `ddoffsethigh` | `0x80000000` | MATCH (M3.4B) |
| initial PTR | `rxout*16` (rcvptrbase=0) | `0x400` | MATCH |
| DMA high32 | 0 (32-bit window) | 0 | MATCH |
| completion-index basis | `status0.CD` bits | `status0` (RS/CD) | MATCH |

No discrepancy found; the M3.4B RX model matches the vendor FIFO0 RX
programming. The vendor additionally programs 4 TX channels (not present in
M3.4B).

## C.6 TX post-init activity state

After `dma64_txinit`: engine **enabled** (`XE=1`), descriptor ring zeroed and
its base published (`addrlow/high`, `ptr = base`), `txin == txout` (ring empty),
no descriptors posted, no data path. `EN_MAC=0` and PSM idle.

```
TX PHYSICAL TRANSMISSION POSSIBLE BEFORE EN_MAC/PHY?   NO
```
(The engine is enabled but the ring is empty and the MAC cannot fetch/transmit
either.)

## C.7 RX post-init activity state

After `dma64_rxinit` + `dma64_rxfill`: RX engine **enabled** (`RE=1`, control
`0x84D`), ring armed, `nrxpost` buffers mapped and posted, `PTR = rxout*16`.
The engine is idle (STATUS0 RS=IDLE per M3.4B) and no RF data exists because the
PHY is not initialized and `EN_MAC=0`.

```
RX DMA ASYNC ACTIVITY POSSIBLE BEFORE PHY/EN_MAC?
  Engine enabled + rings armed + buffers posted = YES
  Actual DMA/data advance without EN_MAC/PHY    = NO
```
Relation to M3.4B: M3.4B observed exactly this idle enabled state
(STATUS0=0x2000e000, RS=IDLE) with `EN_MAC=0`; no RX completion occurred.

## C.8 Exact interrupt configuration

| # | register | offset | width | value | ordered |
|---|---|---|---|---|---|
| 1 | `intrcvlazy[0]` | `0x100` | 32 | `*(dev+0x1ac)` (runtime) | before MACCONTROL |
| 2 | `macintstatus` | `0x128` | 32 | `MI_GP1=0x4000` (W1C ack) | after MACCONTROL |
| 3 | `intctrlregs[0].intmask` | `0x24` | 32 | `I_RI=0x10000` | after #2 |
| 4 | `macintmask` | `0x12C` | 32 | **not written here** (held 0 by `wl_intrsoff`) | – |

Distinctions: `I_RI` (per-FIFO RX interrupt, `d11.h:449`) is the per-source bit
written at `0x24`; `MI_DMAINT=1<<15` is the MAC aggregate summary bit and is
**not** set here; `macintmask` (`0x12C`) is the aggregate enable and stays 0;
the BCMA/PCI route is controlled by `bcma_host_pci_irq_ctl`, which the vendor
bring-up toggles via `wl_intrsoff`/`wl_intrson` (host callbacks), not in this
tail.

## C.9 Is CPU IRQ delivery possible at this point? — NO

```
D11 SOURCE ARMED            = intctrlregs[0].intmask = I_RI (0x24)
MAC AGGREGATE ROUTE ENABLED = NO  (macintmask 0x12C = 0)
BCMA/PCI ROUTE ENABLED      = NO  (wl_intrsoff active since 0x682d6)
HOST HANDLER PRESENT        = host-driver responsibility (outside blob)
CPU INTERRUPT POSSIBLE      = NO
```

## C.10 Location of the vendor host IRQ enable

`wl_intrsoff` @ `wlc_bmac_init 0x682d6`; `wl_intrsrestore` @ `0x695ee` (after
`wlc_phy_init`); `wl_intrson` @ `wlc_bmac_up_finish 0x66446`. `wlc_intrsoff`
(`0x7a6b5`) zeroes `D11+0x12C` (MACINTMASK); `wlc_intrson` (`0x7a720`) restores
`wlc+0x9c` and calls `wlc_ol_enable_intrs`; `wlc_intrsrestore` (`0x7a684`)
restores the saved mask. Therefore **host interrupt delivery is enabled only
after `wlc_bmac_init` returns** (i.e. after DMA init, band init, bsinitvals and
`wlc_phy_init`). DMA init pre-PHY without host interrupt delivery is
vendor-consistent.

## C.11 Current `ob_dma_init` compatibility

`ob_dma_init` (`src/ob_dma.c:132`) allocates exactly two 8 KiB-aligned coherent
rings (RX 256-active-of-4096 B, TX 512) and sets a 32-bit mask; it programs no
D11 DMA register, posts no buffer and touches no IRQ. `ob_irq_init` installs the
handler (`request_irq`, `src/ob_irq.c:150`) with no source. The hardware RX
programming lives in `ob_rx_init` (M3.4B), which also does
`bcma_host_pci_irq_ctl(true)` + `I_RI` + `MI_DMAINT`. There is **no TX channel
programming** anywhere.

```
CURRENT ob_dma_init CAN SATISFY VENDOR PRE-PHY DMA REQUIREMENT?   NO (alone);
   the codebase can with a refactor (ob_dma_init + ob_rx_init)
```

## C.12 Software-vs-hardware DMA helper decomposition (proposed)

Split the future vendor-faithful DMA stage into explicit helpers so it does not
depend on mac80211 or normal probe state:

1. `ob_dma_alloc(hw)` — coherent ring/descriptor memory + 32-bit mask
   (= today's `ob_dma_init`, already implemented).
2. `ob_dma_desc_init(hw, ch)` — zero/prepare descriptors and bookkeeping.
3. `ob_dma_map_buffers(hw, ch)` — `dma_map_single` RX buffers (not for TX).
4. `ob_dma_program(hw, ch)` — publish `addrlow/addrhigh/ptr` and write
   `control` (enable) per channel; no IRQ, no mac80211.
5. `ob_dma_post_rx(hw, ch)` — post buffers and update `ptr`.
6. `ob_irq_route(enable)` — `bcma_host_pci_irq_ctl` and `macintmask`/`I_RI`
   (kept separate; **not** part of the pre-PHY DMA stage).
7. `ob_dma_quiesce(hw)` — core reset/disable (C.17) before any free.

## C.13 Precise M3.4B proven / not-proven scope

PROVEN (hardware): DMA API 32-bit window; RX ring alignment/address semantics;
`ADDRHIGH=0x80000000`; RX `CONTROL=0x84D`; `PTR=0x400`; RX engine accepted
programming; RX engine reached IDLE; IRQ 33 / host route mechanics separately
tested.
NOT PROVEN: actual receive completion; TX DMA hardware programming or
completion; all four TX FIFOs; firmware-driven RX traffic; vendor-order
integration after common initvals.

## C.14 Recommended IRQ strategy for an isolated pre-PHY DMA stage

Option **A** (program DMA, keep host IRQ route disabled) is the vendor-faithful
choice: at this point the vendor has `macintmask=0` and `wl_intrsoff` active.
`request_irq` may remain installed (M3.3 handler) but `bcma_host_pci_irq_ctl`
must stay `false`, `macintmask=0`, and `I_RI`/`MI_DMAINT` must not be enabled.
Do not choose **B/C** (route/handler enable) unless Linux integration proves it
necessary for safety.

## C.15 Linux DMA lifetime requirements

Before DMA is armed: coherent rings via `dma_alloc_coherent`/`dma_pool`;
`dma_map_single(DMA_FROM_DEVICE)` for RX buffers; `dma_wmb()`/`wmb()` before
publishing descriptors; descriptor visibility. On stop/failure: disable the
engine (bounded poll) or core-reset, `synchronize_irq` if a handler could run,
`tasklet_kill` if deferred work exists, then unmap (`dma_unmap_single`) and free.
If no handler is installed and the route is disabled, `synchronize_irq` is not
required but `dma_unmap_single` for every posted buffer is. Distinguish the
**hardware-faithful sequence** (C.1) from these **Linux resource-lifetime
obligations**, which apply regardless of what the vendor firmware would do.

## C.16 Deterministic postconditions (read-only)

| item | expected | class |
|---|---|---|
| TX[i].control bit0 (XE) | 1 | CONSTANT |
| TX[i].addrlow | ring base (`dma_addr_t` low) | DERIVED |
| TX[i].addrhigh | `0x80000000` | CONSTANT |
| TX[i].status0 | not DISABLED | DERIVED |
| RX.control | `0x0000084D` | CONSTANT |
| RX.addrlow | RX ring base | DERIVED |
| RX.addrhigh | `0x80000000` | CONSTANT |
| RX.ptr | `0x400` (64 x 16, rcvptrbase 0) | CONSTANT |
| RX.status0 | RS=IDLE (`0x2`) | CONSTANT |
| RX.status1 error bits | 0 | CONSTANT |
| MACINTMASK (0x12C) | 0 | CONSTANT |
| intctrlregs[0].intmask (0x24) | `0x10000` | CONSTANT |
| MACCONTROL (0x120) | `0x44020402` | DERIVED |

## C.17 Vendor DMA stop/reset path and unload policy

Vendor quiesce (`wlc_bmac_down_prep 0x66108` -> `wl_intrsoff`, `wlc_phy_down`;
`wlc_bmac_down_finish 0x66338` -> `wlc_bmac_suspend_mac_and_wait`, `wl_reset`,
**`wlc_coredisable 0x6378d`**, `wlc_bmac_hw_down`) runs **before**
`dma_detach` (`dma64_detach` @ `0xfecf`) frees ring memory. `wlc_coredisable`
resets/disables the D11 core (and switches the radio off), which guarantees the
DMA engines can no longer consume ring/buffer addresses.

C3 counterpart: `dma_txreset` (write `SE=0x2`, bounded poll, write `0`, bounded
poll, +300 us) and `dma_rxreset` (write `0`, bounded poll to `RS_DISABLED`).
Per-channel reset functions exist in the vendor vtable but their exact addresses
were not pinned in this pass (UNKNOWN).

CRITICAL: the current OpenBRCM `ob_dma_free` frees rings without disabling any
engine. If a future pre-PHY DMA test arms engines, then on unload it **must**
core-reset/disable (or keep the module loaded until reboot) before freeing, or
the hardware may DMA from freed memory. Because an exact OpenBRCM quiesce for
armed DMA is not yet implemented/proven, a D3A0 hardware test must use a
**one-shot / reboot-required** policy until `ob_dma_quiesce` is proven.

## C.18 Formal A/B/C/D decomposition decisions

```
A. CAN DMA INIT BE ITS OWN ISOLATED MILESTONE AFTER D2B?          YES (D3A0)
B. CAN THE REMAINDER OF THE D3A TAIL BE ISOLATED AFTER DMA INIT?  YES (D3A1)
C. CAN D3B BAND INIT + BSINITVALS THEN BE ISOLATED?               YES
D. CAN FULL PREFIX THROUGH BSINITVALS STOP BEFORE PHY/RF?         YES
```
Conditions for A: host IRQ route stays disabled (`macintmask=0`,
`bcma_host_pci_irq_ctl=false`, no `I_RI`/`MI_DMAINT`), and unload uses
core-reset/disable or a reboot-only policy. All four remain ANALYSIS ONLY.

## C.19 Corrected roadmap

```
D3A0  vendor DMA/IRQ-source bring-up (4 TX channels + FIFO0 RX + post 64
      buffers), host IRQ route DISABLED, then read-only postconditions
D3A1  remaining post-common D11 tail (SHM/NVRAM/TSF/MACCONTROL/macphyclk/
      switch_macfreq), no PHY/radio
D3B   sub_6656c pre-bs + 73 bsinitvals, STOP before wlc_phy_init
D4    wlc_phy_init -> wlc_phy_anacore (first PHY indirect MMIO)
D5    PHY tables; D6 radio/channel/synth; D7 calibration; D8 RX integration
```
Prerequisite for A: implement `ob_dma_alloc/desc_init/program/post_rx` and
`ob_dma_quiesce` (core reset/disable) and keep `ob_irq_route` out of D3A0.

## C.20 Remaining blockers

1. OpenBRCM `ob_dma_quiesce` (core reset/disable around armed DMA) is not
   implemented/proven → until then D3A0 is reboot-required.
2. TX channel programming (4 FIFOs) is absent from OpenBRCM.
3. Exact vendor per-channel `txreset`/`rxreset` vtable addresses not pinned.
4. `intrcvlazy[0]` source value (`*(dev+0x1ac)`) is runtime-derived.
5. `ddoffsethigh` static initialization not pinned (value `0x80000000` is
   M3.4B-proven but its blob source was not re-derived here).

---

# Appendix D — D3A0 blocker closure and implementation GO gate

Read-only RE of `wlc_hybrid.o_shipped` (sha256 `352a6e349f…`), Linux BCMA
sources and the current OpenBRCM tree. No hardware, no MMIO, no implementation.
This closes the Appendix C.20 blockers.

## D.1 Exact four TX DMA channel map (re-proven)

`wlc_bmac_attach` does exactly four `dma_attach` calls and four
`wlc_hw_set_di(fifo)` stores (fifo 0..3). Register bases are selected by
`[dev+0x84] > 0xA` (`phyrev`); BCM4352 `phyrev=0x2A` takes the 0x40 stride.

| ch | logical FIFO | TX base | RX base |
|---|---|---|---|
| TX0 | `TX_AC_BK_FIFO` | `D11+0x200` | (`RX_FIFO` `+0x220`) |
| TX1 | `TX_AC_BE_FIFO` | `D11+0x240` | – |
| TX2 | `TX_AC_VI_FIFO` | `D11+0x280` | – |
| TX3 | `TX_AC_VO`/`TX_CTL_FIFO` | `D11+0x2C0` | – |

Per-channel dma64 block: `control` +0x00, `ptr` +0x04, `addrlow` +0x08,
`addrhigh` +0x0C, `status0` +0x10, `status1` +0x14.

Per channel `dma64_txinit` (`0xf947`): `ntxd` descriptors, descriptor 16 B,
`memset(ring,0,ntxd*16)`, `txin=txout=0`, `txavail=ntxd-1`; writes `control`
twice (see D.2), `addrlow=ring_pa+ddoffsetlow`, `addrhigh=ddoffsethigh`;
**`ptr` is not written** and **no descriptor is posted**. Engine ends
ENABLED/IDLE (empty ring).

## D.2 Exact TX CONTROL semantics

Not a constant: `dma64_txinit` is a read-modify-write.

`dma_attach` caches four capability fields from each channel's `control` read:
`obj+0x106 = (control>>18)&7`, `obj+0x107 = (control>>6)&3`,
`obj+0x108 = (control>>21)&7`, `obj+0x109 = (control>>24)&3`.

`dma64_txinit` build:
```
c  = read(regbase+0x00)
c &= 0xffe3ff3f; c |= (obj+0x106)<<18; c |= (obj+0x107)<<6   // preserves all bits; re-asserts cap fields [20:18],[7:6]
c &= 0xff1fffff; c |= (obj+0x108)<<21                        // re-asserts [23:21]
c &= 0xfcffffff; c |= (obj+0x109)<<24                        // re-asserts [25:24]
write(regbase+0x00, c)
c2 = read(regbase+0x00) | ((obj+0x0c & 1) ? 0x1 : 0x801)
write(regbase+0x00, c2)
```
Net effect: **`control = read(control) | XE(0x1) | (PD(0x800) when parity is
not enabled)`**; the capability fields ([25:24],[23:21],[20:18],[7:6]) are
re-asserted to the values read from that channel at attach, and every other bit
is preserved. `XE` is always set; `PD` is set iff `(obj+0x0c & 1)==0`
(parity-not-enabled, C3 `DMA_CTRL_PEN`).

Consequences for OpenBRCM: the driver must perform the **same RMW per channel**
(`read control`, OR `XE`[|`PD`]); it must **not** write a hardcoded CONTROL
constant. There is no per-FIFO fixed numeric value.

## D.3 TX ring-base / PTR / address-high semantics

`_dma_ddtable_init` (`0xe66f`) writes `addrlow = ring_pa + ddoffsetlow`
(`obj+0xf4`) and `addrhigh = ddoffsethigh` (`obj+0xf8`); it does not write
`ptr`. `ddoffsetlow/high` and `dataoffsetlow/high` (`obj+0xfc/0x100`) are set
in `dma_attach` (`0x1060d..0x106cf`):

```
if (si+0x04 == 1) {
    if (si+0x08 == 0x83c || si+0x08 == 0x820) {      // bus core id
        if (dma64) { ddoffsetlow=0; ddoffsethigh=0x80000000; goto done; }
    }
    switch (si+0x3c /*chip id*/) {                   // BCM4352 = 0x4352 -> default
        specials...: ddoffsetlow=0x80000000;
        default:     ddoffsetlow=0x40000000;
    }
    ddoffsethigh = 0;
}
done: dataoffsetlow = ddoffsetlow; dataoffsethigh = ddoffsethigh;
```
BCM4352 (bus core `0x83C`, `dma64=1`, `si+0x04==1`) takes the first branch:
**`ddoffsetlow=0`, `ddoffsethigh=0x80000000`; `dataoffsethigh=0x80000000`**.
So TX and RX both use **ADDRHIGH = 0x80000000**, and `addrlow = pa` (32-bit DMA
window). Alignment mask is `0x1fff` (8 KiB); PTR semantics: `ptr = rcvptrbase +
rxout*16` for RX (rcvptrbase 0 on the aligned path); TX PTR unprogrammed by
init. `ddoffsethigh`/`dataoffsethigh` provenance is therefore fully pinned.

## D.4 `intrcvlazy[0]` derivation

`wlc_bmac_attach` `0x69faf` sets `*(dev+0x1ac) = 0x01000000` (= `1 << 24`),
unconditionally. `wlc_bmac_init 0x69006` writes it to `D11+0x100`
(`intrcvlazy[0]`). C3 `brcms_b_coreinit` writes the same `(1 << IRL_FC_SHIFT)`
with `IRL_FC_SHIFT=24`. So:

**`intrcvlazy[0] = 0x01000000` (constant; one RX interrupt per frame).**

## D.5 Exact interrupt-source register sequence

| order | address | register | value | width |
|---|---|---|---|---|
| 1 | `0x69006` | `D11+0x100 intrcvlazy[0]` | `0x01000000` | 32 |
| 2 | `0x69070` | `D11+0x128 macintstatus` | `0x4000` (W1C `MI_GP1`) | 32 |
| 3 | `0x69082` | `D11+0x24 intctrlregs[0].intmask` | `I_RI=0x10000` | 32 |
| – | (never) | `D11+0x12C macintmask` | – (held 0 by `wl_intrsoff`) | 32 |

All three occur **before** the DMA init loop (`0x6921c`), i.e. per-FIFO
`I_RI` is armed before the RX engine is enabled. `I_RI=0x00010000` (per-FIFO RX
source) and `MI_DMAINT=1<<15` (MAC aggregate) are distinct; only `I_RI` is
written here.

## D.6 Pinned reset functions

`dma_txreset` = **`0xf64a`** (vtable `dma64proc+0x10`):
- if `ntxd==0` return true;
- `write(control, SE=0x2)`;
- poll `status0` mask `0xf0000000` until `0`, `0x30000000` (STOPPED) or
  `0x20000000` (IDLE); `osl_delay(10)`; bound counter `0x2719`(10009)→9 step 10
  (≈1000 iters ≈ 10 ms);
- `write(control, 0)`;
- poll `status0` mask `0xf0000000` until `0`; same bound;
- on final timeout: one `osl_delay(300)`;
- return `(status0 & 0xf0000000) == 0`.

`dma_rxreset` = **`0xf5ef`** (vtable `dma64proc+0xa8`):
- if `nrxd==0` return true;
- `write(control, 0)`;
- poll `status0` mask `0xf0000000` until `0`; `osl_delay(10)`; bound
  `0x2719`→9 step 10;
- return `(status0 & 0xf0000000) == 0`.

Both match C3 `dma_txreset`/`dma_rxreset`. Vtable layout used: `+0x08` txinit,
`+0x10` txreset, `+0xa0` rxinit, `+0xa8` rxreset, `+0xd8` rxfill.

## D.7 Strongest DMA quiesce primitive

Comparison:
- **A. per-channel reset** (`dma_rxreset` then `dma_txreset`): stops each engine
  and verifies `status0` state field 0. Does not by itself guarantee no
  in-flight host-memory transaction.
- **B. D11 core reset/disable** (`bcma_core_disable`): asserts the core reset
  line; `bcma_core_wait_value(RESET_ST, ~0, 0, 300)` first waits for the core to
  become idle, then writes `RESET_CTL=RESET`, reads it back, writes `IOCTL`,
  reads it back, `udelay(10)`. A core held in reset cannot issue new descriptor
  fetches or buffer DMA.
- **C. vendor `wlc_coredisable`** (`0x6378d`): radio off + `si_core_disable`
  (+ `wlc_phy_anacore`, `wlc_bmac_core_phypll_ctl`), i.e. the C3/vendor wrapper
  around B.

**SAFE DMA QUIESCE GUARANTEE:** `IRQ mask (macintmask=0, clear I_RI) →
dma_rxreset → dma_txreset (both verified via status0 state==0) →
bcma_core_disable(d11core, 0) with its RESET_ST wait and REG readbacks`. The
core-disable step is the guarantee that the engines can no longer consume any
Linux-owned ring/buffer address; the per-channel resets are the orderly
per-engine stop. (Vendor normal bring-down uses core disable and does not call
the per-channel resets; per-channel resets are the safer belt-and-suspenders
because they are pinned and bounded.)

## D.8 `ob_dma_quiesce()` contract

Analysis only. Future helper:

- **Preconditions:** D11 core powered; `ob_dma_*` resources allocated; IRQ
  source may be armed but host route may or may not be enabled.
- **Steps (order fixed):**
  1. Disable interrupt sources: `macintmask` `0`, clear per-FIFO `I_RI`
     (`intctrlregs[0].intmask &= ~I_RI`), ack `macintstatus` owned bits.
  2. `dma_rxreset(di0)` (control=0, bounded poll `status0 & 0xf0000000`==0);
     verify.
  3. `dma_txreset(di3), (di2), (di1), (di0)` (control=SE, poll, control=0,
     poll); verify each.
  4. `bcma_core_disable(hw->core, 0)` (waits `RESET_ST`, asserts reset,
     readbacks).
  5. `dma_wmb()`/readback as needed.
- **Fallback:** if any per-channel reset times out, still proceed to
  `bcma_core_disable`; the core-reset is the non-optional backstop. Do not free
  if core-disable itself fails.
- **Postcondition (required before any free/unmap):** hardware cannot DMA to any
  Linux-owned ring/buffer address (all engines stopped or core in reset).
- **Error return:** report which stage failed; never free on unproven quiesce.

Only after the postcondition may the caller `dma_unmap_single`, free skbs,
`dma_pool_free`/`dma_free_coherent`, and free ring metadata.

## D.9 Failure hierarchy

```
try per-channel resets (bounded) -> verify status0 state==0
  on any timeout/failure:
      bcma_core_disable (assert reset, wait RESET_ST, readback)
  after guaranteed quiesce (or core reset asserted):
      synchronize_irq (if host route could deliver) / tasklet_kill
      dma_unmap_single each posted RX buffer
      free skbs, free descriptor rings
```
No invented recovery beyond pinned per-channel resets and `bcma_core_disable`.

## D.10 BCMA / core-reset DMA guarantee

`bcma_core_disable` (Linux `drivers/bcma/core.c`): returns early if already in
reset; otherwise `bcma_core_wait_value(RESET_ST, ~0, 0, 300)` (core idle),
asserts `RESET_CTL=RESET`, reads back, `udelay(1)`, writes `IOCTL`, reads back,
`udelay(10)`. Reset assertion stops the core's DMA. Caveat to record honestly:
neither BCMA nor the vendor adds an explicit "PCIe outstanding-DMA completion"
flush beyond the reset assertion + `RESET_ST` wait + register readbacks; the
readbacks order the backplane writes. This is the standard and vendor-used
guarantee; a D3A0 run must empirically confirm teardown.

## D.11 Linux memory-ordering requirements

- Coherent descriptor rings (`dma_pool`/`dma_alloc_coherent`): descriptor stores
  then `dma_wmb()` before the doorbell (writing `ptr`/`addrlow`/`control` via
  MMIO), so the device sees descriptors before it is told to fetch them.
- Streaming RX buffers (`dma_map_single(DMA_FROM_DEVICE)`): map, then
  `dma_wmb()` before publishing the descriptor; on reclaim, `dma_unmap_single`
  synchronizes.
- Ring base/`ptr`/`control` writes: MMIO (posted) writes; a readback is the
  robust ordering point if a strict barrier is required.
- brcmsmac uses no explicit `dma_wmb()` in `dma64_dd_upd`/`dma_rxfill`
  (relies on coherent memory + MMIO ordering). OpenBRCM should nonetheless use
  `dma_wmb()` before the doorbell (as already proposed in `docs/rx_path.md`).

## D.12 TX software ring state for D3A0

Minimum: `ntxd=512`; descriptors zeroed (`memset`); ring base published
(`addrlow=pa`, `addrhigh=0x80000000`); `control = read | XE[|PD]`;
`txin=txout=0`; `txavail=ntxd-1`; skb pointer array present but **no mappings
and no payload buffers**; `ptr` not programmed by init. **No TX payload
buffers/mappings are required for D3A0** (nothing is transmitted).

## D.13 RX software state / lifetime

256-descriptor ring; 64 posted buffers of 2048 B each, `dma_map_single(...,
DMA_FROM_DEVICE)`, `skb` owned by the driver until completed, descriptor
ownership flips when posted; `control=0x84D`; `ptr=0x400`. Cleanup order after
quiesce: stop engine + confirm disabled → `synchronize_irq`/`tasklet_kill` if a
route/handler could run → `dma_unmap_single` each posted buffer → free skbs →
free descriptor ring metadata.

## D.14 Deterministic D3A0 postconditions

| channel | field | expected | class |
|---|---|---|---|
| TX0..TX3 | control bit0 (XE) | 1 | CONSTANT |
| TX0..TX3 | control bits [25:24],[23:21],[20:18],[7:6] | equal to attach-time read | DERIVED (RMW) |
| TX0..TX3 | addrlow | ring base | DERIVED |
| TX0..TX3 | addrhigh | `0x80000000` | CONSTANT |
| TX0..TX3 | status0 state | not DISABLED | DERIVED |
| RX | control | `0x0000084D` | CONSTANT |
| RX | addrlow | ring base | DERIVED |
| RX | addrhigh | `0x80000000` | CONSTANT |
| RX | ptr | `0x400` | CONSTANT |
| RX | status0 | RS=IDLE (`0x2`) | CONSTANT |
| RX | status1 err | 0 | CONSTANT |
| global | `macintmask` (0x12C) | 0 | CONSTANT |
| global | `intctrlregs[0].intmask` (0x24) | `0x10000` | CONSTANT |
| global | `intrcvlazy[0]` (0x100) | `0x01000000` | CONSTANT |
| global | `maccontrol` (0x120) | `0x44020402` | DERIVED |

Do not require raw RX `ptr` equality (field/current bits); use the state fields.

## D.15 Proposed D3A0 runtime boundary (analysis)

```
proven D2B prefix (ucode + common initvals)
  -> allocate DMA resources (2..5 rings) + 8 KiB align + 32-bit mask
  -> memset descriptors; map + post 64 RX buffers (DMA_FROM_DEVICE)
  -> program TX0..TX3: addrlow/high + control RMW (XE[|PD]); no ptr/descriptors
  -> program RX: addrlow/high + ptr=0x400 + control=0x84D (enable)
  -> interrupt source only: intrcvlazy[0]=0x01000000; intctrlregs[0].intmask|=I_RI
     (HOST ROUTE DISABLED; macintmask stays 0)
  -> validate D3A0 postconditions (D.14)
  -> ob_dma_quiesce (per-channel reset -> verify -> bcma_core_disable)
  -> verify engines stopped / core in reset
  -> dma_unmap/free, free skbs/rings
  -> STOP
```
Bring-up and safe teardown must be proven **in the same run**.

## D.16 Normal-unload vs reboot-only verdict

```
SAFE TO IMPLEMENT D3A0 WITH NORMAL QUIESCE/UNLOAD?   YES
SAFE ONLY AS ONE-SHOT UNTIL REBOOT?                  YES (fallback, not the design)
```
The quiesce sequence (D.7/D.8) is built only from pinned vendor/BCMA operations
and makes normal unload possible; reboot-only remains an emergency fallback
until the first D3A0 run empirically confirms teardown.

## D.17 OpenBRCM refactor plan (analysis only)

| proposed helper | reuse | change from today |
|---|---|---|
| `ob_dma_alloc()` | `ob_dma_init` (mask, pool, ring alloc) | split out hardware-free allocation; add per-role rings (up to 5) |
| `ob_dma_desc_init()` | new (same as ring `memset`) | explicit helper |
| `ob_dma_map_rx()` | `ob_rx` buffer map loop | extract from `ob_rx_init` |
| `ob_dma_program_tx()` | **new** | 4 channels: addrlow/high + control RMW; no ptr/descriptors |
| `ob_dma_program_rx()` | `ob_rx_init` register writes | FIFO0 `0x220/0x224/0x228/0x22C`, control `0x84D` |
| `ob_dma_post_rx()` | `ob_rx` post loop | 64 buffers, ptr `0x400` |
| `ob_dma_config_irq_source()` | part of `ob_rx_init` | `intrcvlazy`/`I_RI` only; **no** `bcma_host_pci_irq_ctl`, no `MI_DMAINT` |
| `ob_dma_quiesce()` | **new** | per-channel resets + core disable (D.7/D.8) |
| `ob_dma_free()` | `ob_dma_free` | call only after quiesce; free rings/skbs |
| `ob_irq_route()` | `ob_rx.c` `bcma_host_pci_irq_ctl` | keep **out** of the D3A0 path (used by M3.4B/normal only) |

Behavioral changes: `ob_dma_init` becomes allocation-only (already close);
`ob_rx_init` splits into program/post + IRQ routing; `ob_irq_init` unchanged
(handler only) but must not be required by D3A0.

## D.18 Regression protection

New isolated mode (e.g. `dma_test_only`) must be mutually exclusive with
`fw_validate_only`/`ucode_test_only`/`initvals_test_only` and must:
- never call `ob_mac80211_register`, `bcma_host_pci_irq_ctl(true)`, or enable
  `MI_DMAINT`;
- keep `ob_remove` running `ob_dma_quiesce` before any free.
Regression checks: the three earlier isolated modes must still skip DMA/IRQ
allocation and programming entirely (assert by absence of the new code paths),
and `make hosttest` must continue to pass unchanged.

## D.19 Final D3A0 implementation GO gate

Every required item is now resolved:

- exact 4 TX channel programming known: **YES** (D.1);
- exact TX CONTROL known as RMW formula: **YES** (D.2);
- `dataoffsethigh`/`ddoffsethigh` provenance resolved: **YES** (D.3, `0x80000000`);
- `intrcvlazy` derivation resolved: **YES** (D.4, `0x01000000` constant);
- exact TX/RX reset functions pinned: **YES** (D.6, `0xf64a`/`0xf5ef`);
- safe quiesce sequence proven: **YES** (D.7/D.8, per-channel reset + core disable);
- Linux cleanup ordering proven: **YES** (D.8/D.9/D.13);
- deterministic postconditions defined: **YES** (D.14);
- no host IRQ delivery: **YES** (D.5, `macintmask=0`, route off);
- prior milestone isolation preserved: **design** (D.18).

```
D3A0 IMPLEMENTATION GO:  YES
```
Scope of the GO: implement D3A0 **analysis-to-code** as a new isolated mode with
the D.15 boundary and mandatory same-run teardown; it remains
`NOT IMPLEMENTED` / `NOT HARDWARE PROVEN` until a hardware run proves bring-up
and teardown. If that run cannot prove teardown, fall back to reboot-only.

Remaining (implementation-time, not RE blockers): write the new helpers; add the
isolated mode; prove teardown on hardware; confirm BCMA reset DMA guarantee
empirically.

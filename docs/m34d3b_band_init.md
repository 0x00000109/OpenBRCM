# M3.4D3B — band init + `d11ac1bsinitvals42` (analysis / design)

**Status: `ANALYSIS ONLY` — NOT IMPLEMENTED — NOT HARDWARE PROVEN.**

This document formalizes the **next** isolated milestone after the
hardware-proven D3A1. It fixes the exact vendor boundary, the operations that
must be reproduced, the postconditions, the failure policy and the remaining
open item. No hardware was accessed and no code was implemented.

- Entry state: the **hardware-proven D3A1 exit** (`M3.4D3A1 = HARDWARE RUNTIME
  PROVEN`, candidate `42d74b8`, module `6ba2d853…`; see
  [`m34d3a1_vendor_tail_test.md`](m34d3a1_vendor_tail_test.md) §14.1).
- Recovered ordering/mechanism: this document + Appendix B of
  [`m34d3_bsinitvals.md`](m34d3_bsinitvals.md); classification
  `docs/m34d3/bsinitvals_classification.{md,json}`.
- Vendor blob: `wlc_hybrid.o_shipped`
  (sha256 `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`),
  `.text` VMA 0, file offset `0x40`.
- C3 = upstream `brcmsmac` (`main.c`, `d11.h`) for corroboration only; no code
  copied.

## 1. Exact D3B boundary

D3B continues **after** the proven D3A1 tail. The vendor link point is
`wlc_bmac_init` `0x695d8` -> `sub_6656c` (band-init helper).

```
wlc_bmac_init (0x6828a)
├─ ... D3A1 tail through wlc_bmac_switch_macfreq (0x695cb)   [HARDWARE PROVEN]
└─ 0x695d8  sub_6656c(dev, chanspec, band = 0)   *** D3B starts ***
            0x66582  si_seci_upd          [SKIP: chip != 0xa9a7/0x4331]
            0x665c3  osl_readw(D11+0x3e0) ; phyversion (read-only, result unused)
            0x665d6  sub_62766(dev, band+8)  ; 5 x write_shm -> MHF1..5
            0x665db  select bsinitvals table by phyrev (dev+0x84) + phytype (band+0x1c)
            0x669bd  sub_60f67(dev, d11ac1bsinitvals42)  ; 73 records
            0x669c2  *** STOP ***
            0x669df  wlc_phy_init(*(band+0x28), chanspec)   [D4, not entered]
```

- **First included op:** the `sub_6656c` entry / `osl_readw(D11+0x3e0)`.
- **Last included op:** the `sub_60f67` applier return at `0x669c2`.
- **Explicit STOP:** before the `test r13b` / `wlc_phy_init` block; therefore
  before `wlc_phy_anacore` (`0xbac84`), before any PHY-indirect window
  (`D11+0x3fc/0x3fe`) and any radio window (`D11+0x3d8/0x3da`).

`sub_6656c` has exactly two callers: `wlc_bmac_init` `0x695d8` (initial
bring-up; `band=0`) and `wlc_bmac_set_chanspec` `0x67bd0` (band switch). D3B is
the initial-up caller only.

## 2. `sub_6656c` pre-bs (proven D11/SHM-only)

Before the applier, `sub_6656c` executes only:

1. `si_seci_upd` for chip `0xa9a7`/`0x4331` (`0x66582..0x665b0`) — **skipped
   for BCM4352**.
2. `osl_readw(D11 + 0x3e0)` (`0x665c3`) — a plain read of the PHY-version status
   register; the value is not used in the observed rev42 path (no write to
   `0x3fc/0x3fe`).
3. `sub_62766(dev, *(dev+0xe8) + 8)` (`0x665d6`) — see §3.
4. The table-selection switch (`0x665db..0x669ba`): compares on `dev+0x84`
   (`phyrev`) and `*(dev+0xe8)+0x1c` (`phytype`); loads a `mov` immediate.
   No MMIO.

```
sub_6656c PRE-BS IS D11/SHM-ONLY = PROVEN
```

## 3. `sub_62766` = `wlc_bmac_write_mhf` (newly pinned)

Disassembly (`sub_62766` `0x62766..0x627c8`):

```
62781  movw $0x5e,-0x30(%rbp)   ; M_HOST_FLAGS1 (0x2f*2)
62787  movw $0x60,-0x2e(%rbp)   ; M_HOST_FLAGS2 (0x30*2)
6278d  movw $0x62,-0x2c(%rbp)   ; M_HOST_FLAGS3 (0x31*2)
62793  movw $0x78,-0x2a(%rbp)   ; M_HOST_FLAGS4 (0x3c*2)
62799  movw $0xd4,-0x28(%rbp)   ; M_HOST_FLAGS5 (0x6a*2)
6279f  movzwl 0x0(%r13,%rbx,1),%edx   ; value = table[i]   (r13 = args rsi)
627a5  movzwl (%r12,%rbx,1),%esi       ; offset = offs[i]
627ad  rdi = dev ; rbx += 2 ; call wlc_bmac_write_shm(dev, offset, value)
627b6  cmp $0xa,%rbx ; jne 6279f       ; 5 iterations
```

The caller `sub_6656c` `0x665c8..0x665d6`:

```
665c8  rsi = *(dev+0xe8)   ; band0 pointer
665cf  rdi = dev
665d2  rsi += 8            ; &band->mhfs[0]
665d6  call sub_62766
```

`wlc_bmac_write_shm(dev, off, val)` (`0x61fb0` -> helper `0x61ed5`) is the
proven SHM applier: `OBJADDR = 0x10000 | (off >> 2)`, readback, then
`OBJ_DATAlo (0x164)` when `(off & 2) == 0` else `OBJ_DATAhi (0x166)`. `off` is a
**byte offset** (calibrated against `M_FIFOSIZE0 = 0x98`).

Equivalent C3: `brcms_c_write_mhf()` (`main.c:668`) writes
`{M_HOST_FLAGS1..5}` from `band->mhfs[0..4]`. `MHFMAX = 5`.

**Conclusion:** the first D3B action writes the five **band-0 MHF host-flag
words** to SHM `0x5e/0x60/0x62/0x78/0xd4` (u16, low halves). This is the
`wlc_bmac_write_mhf` that is **not** the same as the D3A1 `btc_flags` MHF
applier (`sub_62b79`), which is skipped because `btc_flags` is absent.

### 3.1 The band-0 MHF values — reconstructed init call graph

`band->mhfs[0..4]` is a host-side `u16[5]` array at `band+8`. It is **not**
initialized in the blob: the band structs are allocated zeroed by
`wlc_hw_attach` (`0x798ff`, `wlc_calloc`, `dev+0xf0` size `0x70`, `dev+0xf8 =
0xf0+0x38`), so the array starts at `0`. The blob **does** contain in-image
`wlc_bmac_mhf` callers (contrary to the earlier assumption); no `wl`-layer
`wlapi_bmac_mhf` caller exists in the blob, but the internal callers fully
drive the array before the D3B read.

`wlc_bmac_mhf(dev, idx, mask, val, band)` (`0x62995`) is
`band->mhfs[idx] = (band->mhfs[idx] & ~mask) | val` (`band` 0/1/2/all). The
SHM writer `sub_62766` maps `idx 0..4` -> `M_HOST_FLAGS1..5`
(`0x5e/0x60/0x62/0x78/0xd4`).

**Initial-up order** (`wl_open` -> `wl_up` -> `wlc_up`, `0x3aac3`; the
D3B read is later, inside `wlc_bmac_init` -> `sub_6656c`):

| # | site | idx (MHF) | mask | val | condition | provenance |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `wlc_up 0x3ab85` | 3 (MHF4) | `0x4000` | `0x4000` | chip `== 0x4313` | **SKIPPED** (0x4352) |
| 2 | `wlc_up 0x3abb5` | 4 (MHF5) | `0x80` | `0x80`/`0` | `*(*(wlc+0x550)+0x59) >= 1` | opaque `wlc_info` field |
| 3 | `wlc_up 0x3ac88` | 0 (MHF1) | `0x100` (EDCF) | `0x100`/`0` | `*(wlc+0x54) != 0` | opaque `wlc_info` field |
| 4 | `wlc_up 0x3acbc` | 1 (MHF2) | `0x8` | `0x8` | `*(*(wlc+0x100)+4)==1 && *(wlc+0x60)!=0` | opaque `wlc_info` fields |
| 5 | `wlc_bmac_init 0x6855b` | 2 (MHF3) | `0x1` (ANTSEL_EN) | `0x1` | `antsel_type ∈ {2,3,6}` | `dev+0x1a1` |
| 6 | `wlc_bmac_init 0x68578` | 2 (MHF3) | `0x2` (ANTSEL_MODE) | `0x2` | `antsel_type ∈ {2,3,6}` | `dev+0x1a1` |
| 7 | `wlc_bmac_init 0x685f7` | 2 (MHF3) | `0x1` | `0x1` | `antsel_type == 1` | `dev+0x1a1` |
| 8 | `wlc_bmac_init 0x68611` | 2 (MHF3) | `0x2` | `0x0` | `antsel_type == 1` | `dev+0x1a1` |
| 9 | `wlc_bmac_init 0x688a8` | 4 (MHF5) | `0x800` | `0x800` | LCN/PHY-rev path | **not rev42 AC** |
| 10 | `sub_62b79` via `0x69461` | 0..4 | varied | `btc_flags`-driven | `btc_flags` present | **absent -> skip** |

`wlc_antsel_attach` (`0x5970a`) sets `antsel_type` (via
`wlc_bmac_antsel_type_set`, which stores `sil` at `dev+0x1a1`) but writes no
MHF itself. `sub_62b79` (`wlc_bmac_btc_flags_idx_set`) writes MHFs only under
the `btc_flags` gate, which is absent on the ASUS PCE-AC56 (D3A1 §15.6).

**`antsel_type` derivation** (`wlc_antsel_attach`, `0x5970a`): reads NVRAM
`antswitch`, `aa2g`, `aa5g` (`getintvar`) and compares boardtype (`wlc+0x94`)
and boardflags (`wlc+0x9c & 0x8`); `antsel`/`phy_rxantsel` are also read. It
sets `antsel_type` in `{1,2,3,4,5,6}`. The blob's `wlc_bmac_init` gate accepts
`{2,3,6}` (EN+MODE) and `1` (EN only) and ignores `{0,4,5}`.

### 3.2 Verdict — required inputs remain unproven

```
BAND-0 MHF INITIAL VALUE (zero)                      PROVEN
MHF WRITE SITES + VALUE EXPRESSIONS                  PROVEN
antsel_type (SPROM/NVRAM aa2g/aa5g/antswitch/board*) NOT AVAILABLE
wlc_up opaque-field gates (wlc+0x54/0x60/0x550/0x100) NOT AVAILABLE
pio_mhf2-equivalent closed default                   NOT RECOVERABLE
C3 brcmsmac as provenance for masks 0x80/0x8         NO (AC-era, not in C3)
D3B IMPLEMENTATION GO:                               NO (blocked)
```

Reasons (do **not** invent a default):

1. The final `mhfs[0..4]` is a function of runtime board/NVRAM data
   (`antsel_type`, `wlc_info` flags) that OpenBRCM neither reads nor records.
   No source in this workspace exposes the required values.
2. Two masks have **no C3/`brcmsmac` constant** (`MHF5 0x80`, `MHF2 0x8`), so
   the open lineage cannot supply provenance for them; and the closed
   `pio_mhf2`-equivalent default has no blob initializer.
3. The `brcms_c_mhfdef` all-zero simplification is therefore **not** proven for
   this driver/board; assuming zero would be an invented default.

To unblock, one of the following must be provided and accepted first:

- a read-only probe of the device SPROM/NVRAM (`aa2g`, `aa5g`, `antswitch`,
  `antsel`, `phy_rxantsel`, `boardtype`, `boardflags`) plus the field mapping
  for the closed `wlc_info` gates; or
- the Broadcom `wl`-layer MHF initializer (source or trace) for BCM4352 rev42;
  or
- an explicit product decision to make D3B a logged runtime probe whose MHF
  gate is validated rather than reproduced.

Until then D3B stops at this boundary. The D3B analysis (boundary, table,
postconditions, policy) is otherwise complete.

## 4. The `d11ac1bsinitvals42` table (73 records)

- Table: `.rodata 0x99430`, `0x250` = 592 B; equals
  `/lib/firmware/brcm/bcm4352-d11ac1bsinitvals42.bin`
  (sha256 `e81a645c79f55557c87f4662702d7c57599918c9b2340bc4e440ce1bdbd014da`).
- Selected at `0x66604`..`0x66622` only when `phyrev(dev+0x84) == 0x2A` **and**
  `phytype(band+0x1c) == 0xB`; the wrong path jumps to `0x669c2` (skips the
  applier). All other rev/type combinations select sibling tables.
- Applier `sub_60f67` (`wlc_bmac_write_inits`): walk 8-byte LE records
  `{u16 offset, u16 width, u32 value}`, stop at `offset == 0xffff`; `width==2`
  -> `osl_writew(value, D11base+offset)`, `width==4` -> `osl_writel(...)`.
  Strict ascending order, no de-dup, no patch.
- Shape: **73** data records (terminator at index 73), **34 x w4 + 39 x w2**:
  34 `OBJADDR (0x160)` selectors (`0x0001xxxx`, SHM space, no auto-inc),
  15 `OBJDATA-lo (0x164)` + 19 `OBJDATA-hi (0x166)` halves, and **5 direct IHR
  `writew`** at `0x680/0x682/0x684/0x686/0x700`.
- **73 MMIO writes total.** No PHY/radio, no DMA, no IRQ, no `MACCONTROL`.

Full per-record classification: `docs/m34d3/bsinitvals_classification.md`.
Common-vs-band overlap: 3 SHM bytes are **overridden** (`0x0010 <- 0x0014`,
`0x001c <- 0x0183`, `0x0094 <- 0x01f4`), so the common table must have been
applied first (D2B) — which the D3A1 entry state guarantees.

## 5. Side-effect accounting (D3B only)

| class | count | detail |
| :--- | :--- | :--- |
| direct D11 MMIO reads | 1 | `osl_readw(D11+0x3e0)` |
| SHM writes (`sub_62766`) | 5 | MHF1..5 at `0x5e/0x60/0x62/0x78/0xd4` |
| SHM writes (bsinitvals) | 68 | 34 selectors + 34 `OBJDATA` halves |
| direct D11 `writew` (bsinitvals) | 5 | IHR `0x680/0x682/0x684/0x686/0x700` |
| PHY-indirect / radio writes | 0 | real PHY starts in D4 |
| DMA / IRQ writes | 0 | DMA was already done in D3A1; none here |
| polls / delays | 0 | none |

No record or pre-bs op targets `intctrlregs`, `MACINTMASK`, the DMA/FIFO
blocks, or any BCMA/PCI routing register.

## 6. Deterministic postconditions (proposed, not hardware-validated)

Read after the applier, before `wlc_phy_init`:

| observable | source | expected | class |
| :--- | :--- | :--- | :--- |
| SHM `0x0010` (u32) | rec 9/10 | `0x00000014` | CONSTANT |
| SHM `0x001c` (u32) | rec 11/12 | `0x00000183` | CONSTANT |
| SHM `0x0094` (u32) | rec 13/14 | `0x000001f4` | CONSTANT |
| D11 `0x0680` (u16) | rec 1 | `0x3e3e` | CONSTANT |
| D11 `0x0686` (u16) | rec 0 | `0x09d0` | CONSTANT |
| D11 `0x0700` (u16) | rec 3 | `0x003c` | CONSTANT |
| SHM MHF1..5 | `sub_62766` | §3.1 values | BOARD-DEPENDENT |
| `MACCONTROL` (`0x120`) | inherited | `0x44020402` | DERIVED (D3A1 exit) |
| `MACINTMASK` (`0x12c`) | inherited | `0` | CONSTANT |

Read method: `ob_ucode_read_shm16()` for SHM (proven), `bcma_read16` for the
IHR block. The 5 pre-bs MHF writes and the common/D3A1 writes do not touch the
six SHM/IHR gates before the applier, so they survive to readback.

## 7. Failure / residual-state policy

D3B performs no DMA/PHY/IRQ activity, but it runs after D3A1's DMA engines are
initialized. Keep the D3A1 **one-shot / reboot-or-full-reinit** policy: exact
unwind of a partially applied 73-record table is not proven. On any
postcondition mismatch, do not retry in the same boot.

| stage fails at | residual | retry without reset |
| :--- | :--- | :--- |
| pre-bs read | D3A1 state | n/a |
| MHF writes (partial) | D3A1 + partial MHF | unknown → reboot |
| applier mid-table | D3A1 + partial bsinitvals | **reset-only** |
| postcondition mismatch | applied | **reset-only** |

## 8. Open items

1. **Band-0 MHF values** (`mhfs[0..4]`) — §3.1/§3.2; the blocking value item.
   Initial value (`0`) and all write expressions are proven, but the runtime
   inputs (`antsel_type` from SPROM/NVRAM; opaque `wlc_info` gates) are not
   available. **D3B is blocked on this.**
2. Symbolic names of the 5 direct IHR fields (`0x680/0x682/0x684/0x686` IFS,
   `0x700` NAV) — values proven, names UNKNOWN (non-blocking).
3. Exact initial band / chanspec used by the initial bring-up (`dev+0x84`,
   `band+0x1c` are proven; the chanspec datum passed to `wlc_phy_init` is D4).
4. `sub_6656c`'s `osl_readw(D11+0x3e0)` result is unused in the rev42 path
   (non-blocking).

## 9. D3B implementation gate

```
BAND-0 MHF INITIAL VALUE AND WRITE EXPRESSIONS                      PROVEN
BAND-0 MHF RUNTIME INPUTS (antsel_type, wlc_info gates)             NOT AVAILABLE
D3B BAND INIT + BSINITVALS ISOLATABLE FROM THE PROVEN D3A1 EXIT?   YES
D3B STOPS BEFORE REAL PHY/RF?                                      YES
D3B IMPLEMENTATION GO:        NO — blocked on the MHF inputs (§3.2)
```

D3B must **not** be implemented with an invented MHF default. Unblock by
providing the SPROM/NVRAM inputs + field mapping, the `wl`-layer initializer, or
an explicit decision to make the MHF gate a logged runtime probe (see §3.2).

After §3.1 is resolved, implementation follows the D3A1 pattern: a new isolated
mode (`bsinitvals_test_only=1`), the proven D3A1 prefix, the §2/§3/§4 sequence,
the §6 postconditions, a STOP before `wlc_phy_init`, host + KUnit tests and
docs-check guards. D4 (`wlc_phy_init` -> `wlc_phy_anacore`, first PHY-indirect
MMIO) is the milestone after D3B.

## 10. Unresolved / not covered

D3B does **not** cover: `wlc_phy_init`, `wlc_phy_anacore`,
`wlc_phy_switch_radio_acphy`, `wlc_phy_init_aphy`, PHY tables, radio/channel/
synth, calibration, RX/TX, scan or association.

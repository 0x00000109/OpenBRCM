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

### 3.1 The band-0 MHF values — origin of every input

`band->mhfs[0..4]` is a host-side `u16[5]` array at `band+8`. It is **not**
initialized in the blob: the band structs are allocated zeroed by
`wlc_hw_attach` (`0x798ff`, `wlc_calloc`, `dev+0xf0` size `0x70`,
`dev+0xf8 = 0xf0+0x38`), so the array starts at `0`. The blob **does** contain
in-image `wlc_bmac_mhf` callers (contrary to the earlier assumption); no
`wl`-layer `wlapi_bmac_mhf` caller exists in the blob, but the internal callers
fully drive the array before the D3B read.

`wlc_bmac_mhf(dev, idx, mask, val, band)` (`0x62995`) is
`band->mhfs[idx] = (band->mhfs[idx] & ~mask) | val` (`band` 0/1/2/all). The
SHM writer `sub_62766` maps `idx 0..4` -> `M_HOST_FLAGS1..5`
(`0x5e/0x60/0x62/0x78/0xd4`).

#### 3.1.1 Complete MHF write call graph (initial up)

`wl_open` -> `wl_up` -> `wlc_up` (`0x3aac3`) -> `wl_init` -> `wlc_init`
(`0x3c46f`) -> `wlc_bmac_init` (`0x6828a`) -> `sub_6656c` -> `sub_62766`. The
`wlc_up` writes execute **before** `wl_init`/`wlc_bmac_init`; the D3B read is
last.

| # | site | idx (MHF) | mask | value | exact condition | class |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1a | `wlc_up 0x3ab49` | 3 (MHF4) | `0x4000` | `0x4000` | `!(boardflags & 0x400000)` path | 4313-era |
| 1b | `wlc_up 0x3ab6b` | 4 (MHF5) | `0x1` | `0x1` | `(boardflags & 0x400000)` path | 4313-era |
| 2 | `wlc_up 0x3abb5` | 4 (MHF5) | `0x80` | `0x80`/`0` | `*(wlc->stf + 0x59) >= 1` | resolved §3.2 |
| 3 | `wlc_up 0x3ac88` | 0 (MHF1) | `0x100` (EDCF) | `0x100`/`0` | `*(pub + 0x54) != 0` | resolved §3.2 |
| 4 | `wlc_up 0x3acbc` | 1 (MHF2) | `0x8` | `0x8`/`0` | `*(pub->sih + 4) == 1 && *(wlc + 0x60) != 0` | resolved §3.2 |
| 5 | `wlc_bmac_init 0x6855b` | 2 (MHF3) | `0x1` (ANTSEL_EN) | `0x1` | `antsel_type ∈ {2,3,6}` | board/NVRAM §3.3 |
| 6 | `wlc_bmac_init 0x68578` | 2 (MHF3) | `0x2` (ANTSEL_MODE) | `0x2` | `antsel_type ∈ {2,3,6}` | board/NVRAM §3.3 |
| 7 | `wlc_bmac_init 0x685f7` | 2 (MHF3) | `0x1` | `0x1` | `antsel_type == 1` | board/NVRAM §3.3 |
| 8 | `wlc_bmac_init 0x68611` | 2 (MHF3) | `0x2` | `0x0` | `antsel_type == 1` | board/NVRAM §3.3 |
| 9 | `wlc_bmac_init 0x688a8` | 4 (MHF5) | `0x800` | `0x800` | LCN/PHY-rev path | **not rev42 AC** |
| 10 | `sub_62b79` via `0x69461` | 0..4 | varied | `btc_flags`-driven | `btc_flags` present | **absent -> skip** |

Site 1 is reached only on chip `0x4313` (the preceding `wlc_bmac_4331_epa_init`
gate); it does **not** run for `0x4352`. `wlc_antsel_attach` (`0x5970a`) sets
`antsel_type` (via `wlc_bmac_antsel_type_set`, which stores the byte at
`dev+0x1a1`) but writes no MHF itself. `sub_62b79`
(`wlc_bmac_btc_flags_idx_set`) writes MHFs only under the `btc_flags` gate,
absent on the ASUS PCE-AC56 (D3A1 §15.6).

### 3.2 Semantic resolution of the formerly-opaque gates

All four gate fields are now resolved to named origins (not "opaque offsets").

**`wlc + 0x550` = `wlc->stf`** (`stf_info`, 0x120 B, `wlc_calloc` in
`wlc_attach_malloc 0x4ee97`). Proof: `wlc_stf_attach` (`0x173d74`) is the only
initial-up function that dereferences `*(wlc+0x550)` and writes its fields
(`+0x7/+0x8` in `wlc_info_init 0x24dd6/0x24de1`; `+0x2a/+0x3c/+0x56/+0x6c/
+0x80/+0x88/+0x59` in `wlc_stf_attach`).

**`*(wlc->stf + 0x59)`** (gate 2) — written by `wlc_stf_attach 0x173f15` to
`1` iff `*(wlc->band + 8) != 7`. `wlc + 0x40 = wlc->band` (the current band):
`wlc_stf_ss_update` (`0x173cb5`) and `wlc_stf_attach` (`0x173e42`) load
`*(wlc+0x40)` and then `band + 0x10` as the `wlc_phy_t` pointer passed to
`wlc_phy_cap_get`/`wlc_phy_txpower_hw_ctrl_get`. `band + 8` is the band
**phytype** (`u16`), compared to `7` (HT/N) and `4`. For this board
`phytype == 0xB` (AC, the same value that selects `d11ac1bsinitvals42`), so
`0xB != 7` -> **`stf+0x59 == 1`**. The only other writer is
`wlc_stf_txchain_set` (`0x175246`), a runtime iovar not on the initial-up path.

**`pub = *(wlc)`; `pub + 0x100 = si_t *sih`** (gate 4, first half). Proof:
`wlc_attach 0x37de7` stores the `wlc_bmac_si_attach`/`si_attach` return at
`pub+0x100`; `pub+0x108` is the synthesized NVRAM text buffer (it is the first
arg to `getvar`, which scans `name=value`) and `pub+0x110` is its size.
`si_pub`/`si_t` begins `socitype` then `bustype` (`+4`), `buscoretype` (`+8`),
`buscorerev` (`+0xc`): `si_pci_war16165` (`0x1e1e1`) tests exactly
`bustype==PCI(1)`, `buscoretype==0x804`, `buscorerev<=0xA`, and
`wlc_bmac_attach 0x69ce8` tests `*(sih+4)==1` before calling it. For BCM4352
(PCIe) `*(sih+4) == 1` is **true**.

**`wlc + 0x60`** (gate 4, second half) — a byte flag set to `1` only in
`wlc_bmac_attach 0x69cf7`, iff `*(sih+4)==1 && si_pci_war16165(sih)`. It is `0`
from the `wlc_calloc`, so the gate is `1` iff the PCIe core is
`buscoretype==0x804 && buscorerev<=10` (the PCIe WAR 16165). The BCM4352 PCIe
core revision is **not yet recovered from repository evidence** (it is a
hardware constant, not a SPROM field) -> this half stays UNKNOWN.

**`pub + 0x54`** (gate 3) — `wlc_info_init 0x24e99` sets it to `0xffffffff`
(non-zero) at attach. Other `+0x54` stores exist (`wlc_set_gmode`,
`wlc_set_nmode`, `wlc_statsupd`, ...) but none is proven to run on the
initial-up path; the known writers that touch the **bandstate** structs
(`wlc_attach 0x3945c` = `*(wlc+0x50)+0x59`, `wlc_set_nmode 0x36874` =
`*(wlc+0x50)+0x54`) are a different base. So `pub+0x54` is most likely still
`0xffffffff` -> **MHF1_EDCF set**, but this is not yet a hard proof.

### 3.3 `antsel_type` reconstruction (`wlc_antsel_attach` 0x5970a)

Inputs: `boardtype = pub+0x94` (byte), `boardflags = pub+0x9c` (`u32`),
`antswitch = getintvar("antswitch")`, `aa2g = getintvar("aa2g")`,
`aa5g = getintvar("aa5g")`. `asi->antsel_type` is `asi+0x10`,
`asi->antsel_avail` is `asi+0x12`; then `wlc_bmac_antsel_type_set(hw,
antsel_type)` stores it at `dev+0x1a1` and calls
`wlc_phy_antsel_type_set(band->phy, antsel_type)`.

```
if boardtype <= 3 -> L_bf
else if antswitch == 0 -> L_bt0
else if antswitch > 7 -> END (type 0)
else bit = 1 << antswitch
  switch group of antswitch:
    {1,2,3} -> type 2 ; {5} -> type 4   ; then avail iff (aa2g==7||aa5g==7)
    {4}     -> type 3 ; {6} -> type 5 ; {7} -> type 6 ; avail iff (aa2g==6||aa5g==6)
    (other -> END type 0)
L_bt0: if boardtype == 4 and aa2g == 7 and aa5g == 0 -> type 2, avail 1
L_bf : if boardflags & 0x8 -> type 1, avail 1 ; else type 0, avail 0
```

Consequences for the D3B MHF3 gate:
- `antswitch == 0` (typical when the NVRAM var is absent) reduces to:
  `type = 2` iff `(boardtype==4 && aa2g==7 && aa5g==0)`, else `type = 1` iff
  `boardflags & 0x8` (`BFL_RFANTS`), else `type = 0`.
- `wlc_bmac_init` sets MHF3 `0x1|0x2` for `type ∈ {2,3,6}`, MHF3 `0x1` for
  `type == 1`, and nothing for `{0,4,5}`.

### 3.4 SPROM / NVRAM provenance for the `antsel` inputs

The textual vars come from the closed `bcmsrom` synthesis, not from an external
file:
- `srom_var_init` (`0x9704`) builds the NVRAM text and `getvar`/`getintvar`
  only *scan* it (`getvar 0xb807` matches `name=` in a text buffer;
  `pub+0x108` is that buffer). The synthesis format strings are in
  `.rodata.str1.1`: `"%s=%u"` `0x5a1`, `"%s=0x%x"` `0x593`, `"%s%d=%s"`
  `0x5a7`, `"%s%d=0x%x"` `0x5af`, `"ccode="` `0x578`, `"sromrev=%d"` `0x559`.
- The rev11 raw-field -> name mapping lives in the closed `srom_parsecis`
  (`0x46a3`, ~19 KiB) and `srom_var_init`/helper `0x4523`; the CIS signature is
  `0x0634` at `+0x80`. **The exact rev11 byte/word offsets for `boardflags`,
  `boardtype`, `ant_available_bg/a`, `antswitch` are NOT recovered from any
  available open source or from the repository**, and are therefore recorded as
  UNKNOWN rather than guessed.
- C3 `bcm47xx_sprom` (`drivers/firmware/broadcom/bcm47xx_sprom.c`) maps the
  *parsed `ssb_sprom` fields* (`ant_available_bg/a`, `antswitch`, `boardflags`)
  to names; `bcma/sprom.c` extracts them only for **rev 8**
  (`SSB_SPROM8_TXRXC`, ...). These validate the *semantics* but not the rev11
  raw offsets.
- The repository preserves **only** the rev11 MAC words (`2cfd a161 4025` at
  SPROM `0x90`); it does **not** preserve a full SPROM image. Consequently
  `boardtype`/`boardflags`/`aa2g`/`aa5g`/`antswitch` cannot be decoded offline
  today.

### 3.5 Symbolic replay of the band-0 MHF vector

Start `mhfs[0..4] = {0,0,0,0,0}`; apply the executed sites in order:

| word | expression | status |
| :--- | :--- | :--- |
| MHF1 (`mhfs[0]`) | `pub+0x54 ? 0x100 : 0` | **PROVEN `0x100`** (§3.8) |
| MHF2 (`mhfs[1]`) | `(bustype==1 && si_pci_war16165) ? 0x8 : 0` | **PROVEN `0x0`** (§3.8) |
| MHF3 (`mhfs[2]`) | `antsel_type∈{2,3,6}` -> `0x3`; `==1` -> `0x1`; else `0x0` | **UNKNOWN** (rev11 SPROM values) |
| MHF4 (`mhfs[3]`) | `0x0` (site 1 is 4313-only) | **PROVEN `0x0`** |
| MHF5 (`mhfs[4]`) | `(band phytype != 7) ? 0x80 : 0` | **PROVEN `0x80`** (§3.8) |

No default-zero substitution is permitted for **MHF3**. MHF1/MHF2/MHF4/MHF5 are
closed (§3.8); the vector is now `{0x0100, 0x0000, ?, 0x0000, 0x0080}`.

### 3.6 C3 (`brcmsmac`) consistency check

| bit | C3 constant | C3 behavior | this path | verdict |
| :--- | :--- | :--- | :--- | :--- |
| MHF1 `0x100` | `MHF1_EDCF` | set unconditionally (`main.c:5020`) | set iff `pub+0x54!=0` (init `0xffffffff`) | MATCH (PROVEN; sole initial-up writer) |
| MHF2 `0x8` | none | absent | PCIe WAR16165 only | DIFFERENCE (PROVEN `0`: BCM4352 `buscoretype=0x83c`, WAR false) |
| MHF3 `0x1/0x2` | `MHF3_ANTSEL_EN/MODE` | set iff `wlc_hw->antsel_type` antdiv | same masks/order | MATCH (mechanism); value UNKNOWN |
| MHF4 `0x4000` | `MHF4_EXTPA_ENABLE` | 4313/extPA path | 4313-only, skipped | MATCH (skip) |
| MHF5 `0x800` | `MHF5_4313_GPIOCTRL`? no | — | LCN path only, skipped | MATCH (skip) |
| MHF5 `0x80` | none | absent | set iff phytype != HT | DIFFERENCE (PROVEN `0x80`: AC phytype `0x0b != 7`) |

The two DIFFERENCE rows are exactly the AC-era bits absent from C3; C3 cannot
supply their provenance.

### 3.7 Verdict, GO/NO-GO, and the smallest read-only capture

Value closure (§3.8) closes four of the five words: the vector is
`{0x0100, 0x0000, ?, 0x0000, 0x0080}`.

```
BAND-0 MHF INITIAL VALUE (zero)                                PROVEN
MHF WRITE SITES + VALUE EXPRESSIONS                            PROVEN
stf / sih / bustype / buscoretype field mapping                PROVEN
MHF1 (mhfs[0]) pub+0x54 (EDCF gate)                            PROVEN 0x0100
MHF2 (mhfs[1]) si_pci_war16165 / wlc+0x60                      PROVEN 0x0000
MHF4 (mhfs[3]) (4313-only site skipped)                        PROVEN 0x0000
MHF5 (mhfs[4]) stf+0x59 <- wlc_band phytype                    PROVEN 0x0080
MHF3 (mhfs[2]) antsel_type <- rev11 SPROM                       UNKNOWN
D3B IMPLEMENTATION GO:                                         NO (blocked)
```

This remains case **B — VALUE PARTIALLY PROVEN**: every write expression and
every gate's semantic origin is known and four concrete `mhfs[]` values are now
closed; exactly **one** concrete *input value* remains missing:

1. `antsel_type` <- `boardtype`, `boardflags & 0x8`, and the SPROM-synthesized
   `antswitch`/`aa2g`/`aa5g` (the raw rev11 SPROM values are not retained; §3.8).

The smallest **read-only** capture (design only; not implemented or run here) —
no D11/PHY/radio writes, no DMA, no IRQ, no MAC enable, no firmware upload,
external-SPROM/struct reads only, bounded — needs **no new hardware mode**: the
already-present read-only path `ob_si_read_mac()` (enabled by default via
`sprom_diag=1`) already reads and CRC-validates the full **234-word** rev11
image; only the emission of those already-read words is missing (§3.8.5):

- log the already-read, CRC-validated external rev11 SPROM 234 words
  (`CC+0x800`, 16-bit reads), plus `sromrev`/CRC;
- no verdict is asserted by the capture; it only records the raw input.

Unblock paths, in preference order: (a) recover the rev11 raw offsets for
`boardtype`/`boardflags`/`aa2g`/`aa5g`/`antswitch` from `srom_parsecis`, read
the raw words through the existing read-only SPROM path, then re-evaluate §3.5;
(b) the `wl`-layer MHF initializer/trace for BCM4352 rev42; (c) an explicit
decision to make D3B a logged runtime probe whose MHF gate is validated, not
reproduced.

Until then D3B stops at this boundary. The D3B analysis (boundary, table,
postconditions, policy) is otherwise complete.

### 3.8 Value-closure analysis (this branch)

All facts below were re-queried with the deterministic index first; two facts
`re` cannot represent (stack-immediate constants and a `true`-value store) were
independently verified with targeted `objdump` and are marked **[manual]**.
Tooling gaps are recorded in §3.8.6.

#### 3.8.1 MHF1 (`pub+0x54`) — PROVEN `0x0100`

- MHF1 site `wlc_up 0x3ac87` (`re switch wlc_up` `0x3ac65`,
  `[manual] 0x3ac50..0x3ac8b`): `val = (*(wlc)+0x54 != 0) ? 0x100 : 0`.
- Enclosing object/field: `wlc_info` first member `pub` (`*(wlc)` at `wlc+0x0`);
  offset `+0x54` is a `u32`. Symbolic field name not recovered (non-blocking).
- Initializer: `wlc_info_init 0x24e99` `[manual] movl $0xffffffff,0x54(%rax)`;
  sole caller `wlc_attach` (`re card wlc_info_init`).
- All `pub+0x54` writers found by whole-`.text` `0x54(%r*)` store scan
  (`[manual]`, because `re` cannot enumerate struct-field writers):
  `wlc_info_init` (init `0xffffffff`) and the runtime iovar dispatcher
  `wlc_doiovar 0x47eba` (`mov %r14d,0x54(%rax)` with `r14 ∈ {0,1}`). The other
  `+0x54` stores are different bases: `wlc_attach 0x383d4` is `stf+0x54`
  (`r13 = *(wlc+0x550)`), `wlc_set_gmode 0x36658` and `wlc_set_nmode 0x36874`
  are `band+0x54` (`r12 = *(wlc+0x40)` / band pointer).
- Ordering (`re fn wlc_attach`): `wlc_info_init` (`0x37e74`) -> `wlc_bmac_attach`
  (`0x37f8e`) -> `wlc_stf_attach` (`0x38ef6`); the MHF1 site is in `wlc_up`,
  which runs after attach and *before* `wl_init`/`wlc_init`/`wlc_bmac_init`.
- `wlc_set_gmode`/`wlc_set_nmode` are iovar/chanspec handlers, not on the
  attach->`wlc_up` path; `wlc_doiovar` is runtime ioctl only.
- **Result: zero is not reachable at the D3B point => MHF1 = `0x0100`.**

#### 3.8.2 MHF5 (`stf+0x59`) — PROVEN `0x0080`

- MHF5 site `wlc_up 0x3abb4` (`[manual] 0x3ab89..0x3abb8`):
  `val = (*(wlc->stf + 0x59) != 0) ? 0x80 : 0` (`wlc->stf = *(wlc+0x550)`).
- `wlc_stf_attach 0x173f15` (`[manual] 0x173f03..0x173f16`) writes
  `stf+0x59 = 1` iff `*(wlc->band + 0x8) != 7` (`wlc->band = *(wlc+0x40)`).
- Object identity (C3 `brcmsmac/main.h`): `struct brcms_band` offset `0x8` is
  `u16 phytype`; `struct brcms_hw_band` offset `0x1c` is `u16 phytype` (the same
  field the D3B bsinitvals selector tests for `==0x0b`).
- `brcms_b_attach` (`main.c:4592`) copies `wlc->band->phytype =
  wlc_hw->band->phytype`; `wlc_bmac_attach` is called (`0x37f8e`) **before**
  `wlc_stf_attach` (`0x38ef6`), so the copy is in place at the setter.
- **Result: active band phytype `= 0x0b` (AC) != 7 => `stf+0x59 = 1` =>
  MHF5 = `0x0080`.**

#### 3.8.3 MHF2 (`si_pci_war16165` / `wlc+0x60`) — PROVEN `0x0000`

- MHF2 site `wlc_up 0x3acbb` (`[manual] 0x3ac8c..0x3acbf`):
  `val = (*(pub->sih+0x4)==1 && *(wlc+0x60)!=0) ? 0x8 : 0`.
- `wlc_bmac_attach 0x69ce1..0x69cf7` sets `wlc+0x60 = 1` iff
  `*(sih+0x4)==1 && si_pci_war16165(sih)`.
- `si_pci_war16165 0x1e1e1` (`re switch`, `[manual]`) returns
  `bustype==1 && buscoretype==0x804 && buscorerev<=0xA`.
- The vendor si constructor `sub_22b10` (called by `si_attach 0x23844`) sets
  `si+0x4 bustype`, `si+0x8 buscoretype`, `si+0xc buscorerev` from the
  enumerated bus core: the `0x83c`/`0x820` (PCIe) path for
  `buscoretype`, the `0x804` (PCI) path otherwise, `0x80d` (SDIO) for
  `bustype==2`.
- For BCM4352 the host bus is PCIe Gen2 with `bustype==1` and
  `buscoretype==0x83c` (already established by
  `docs/dma_architecture.md` §9/§11 and `docs/m34d3_bsinitvals.md` §C; the
  blob's DMA path branches on `si+0x8 ∈ {0x83c,0x820}` and this board takes the
  `0x83c` branch).
- **`0x83c != 0x804` => `si_pci_war16165 = 0` => `wlc+0x60 = 0` =>
  MHF2 = `0x0000`.** The exact PCIe core `buscorerev` is *not* needed for this
  result (the core-type compare fails first).

#### 3.8.4 MHF4 — PROVEN `0x0000`

Site 1 (`wlc_up 0x3ab84`) is reached only after the `chip == 0x4313` test
(`re switch wlc_up` `0x3ab23 Cmp [*(a0+0x100)+0x3c],0x4313 -> 0x3ab89`), so it
does not run for `0x4352`; `mhfs[3]` stays at its `0` initial value
(overwritten only by the 4313 path). Confirmed by the branch's original
analysis.

#### 3.8.5 MHF3 (`antsel_type`) — UNKNOWN

- Inputs: `boardtype = pub+0x94`, `boardflags = pub+0x9c` (SPROM-derived, set by
  the SI/attach path), and `antswitch`/`aa2g`/`aa5g` read by
  `wlc_antsel_attach` (`0x5970a`) via `getintvar` (`re card`).
- **Correction to §3.4:** `aa2g`/`aa5g`/`antswitch` are **not** NVRAM-only. The
  vendor SPROM parser `srom_parsecis` **synthesizes** them from raw SPROM bytes:
  code refs to the literal names at `0x5431` (`aa2g`), `0x5972` (`antswitch`,
  high nibble of a raw byte) and following, via the `name=value` helper
  `0x4523` (`[manual]` relocation + `.rodata` map; `readelf -r`/`objdump -dr`).
  C3 `bcm47xx_sprom.c` corroborates the semantics: `aa2g = ant_available_bg`,
  `aa5g = ant_available_a`, `antswitch = antswitch`, `boardtype = board_type`,
  `boardflags = boardflags`.
- No complete rev11 SPROM image is retained in the repository, its docs, Git
  history, the RE workspace or test logs (only the MAC words and `rev`/CRC were
  logged by `ob_si_read_mac`). The exact rev11 raw offsets for those fields are
  not yet recovered (`srom_parsecis` is a 19 KiB parser).
- Existing read-only path: `ob_si_read_mac()` already reads and CRC-validates
  all **234** words every probe (`sprom_diag=1` default); `ob_si_dump_sprom()`
  logs only four words. **No new hardware mode is required** to obtain the raw
  inputs; only an emission (log) of the already-read words is missing.
- **Result: MHF3 = UNKNOWN — REQUIRES NEW READ-ONLY HARDWARE FACT (raw rev11
  SPROM words), plus static recovery of the rev11 field offsets.**

#### 3.8.6 `re` tooling gaps filed (§9 of `docs/re-tooling.md`)

1. Struct-field writer enumeration: `re fields` is per-function only; finding
   *all* writers of `pub+0x54` required a whole-`.text` store scan. Desired:
   `re fields --struct <name> --writers` (function + site + base-provenance).
2. Structure-field aliasing: in `wlc_set_gmode` `re fields` attributed
   `band+0x54` to `*(a0+0x0)` (pub), i.e. the `band` pointer loaded from
   `wlc+0x40` was flattened to offset 0; manual disasm was needed to separate
   `band+0x54` from `pub+0x54`.
3. Constant/stack-immediate values: `re` did not show the stored immediates
   (`sub_62766` MHF offsets `0x5e/0x60/...`; `wlc_info_init` `0xffffffff`;
   `si+0x8 = 0x804`) — manual objdump required. Desired: immediate/value
   propagation in `re fn`.
4. No string-literal xref: finding the `srom_parsecis` references to the
   literal names `aa2g`/`antswitch` required `readelf -r` + manual
   `.rodata`->file-offset mapping. Desired: `re refs <string-literal>`.
5. `re reach` prints only a count, not the reachable function set, so it could
   not be intersected with field writers. Desired: `re reach --list`.
6. Overlapping ELF function symbols: `srom_parsecis` (0x46a3, size 19103) and
   `srom_var_init` (0x9704) overlap (0x9704 < 0xb12e); `re fn` inherits the
   ambiguous boundary. Desired: overlapping-symbol arbitration/flagging.

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

1. **Band-0 MHF values** (`mhfs[0..4]`) — §3.5/§3.8: MHF1 `0x0100`, MHF2
   `0x0000`, MHF4 `0x0000`, MHF5 `0x0080` are **PROVEN statically**; only
   **MHF3** (`antsel_type`) remains UNKNOWN because the raw rev11 SPROM values
   are not retained. **D3B is blocked on MHF3 only.**
2. Symbolic names of the 5 direct IHR fields (`0x680/0x682/0x684/0x686` IFS,
   `0x700` NAV) — values proven, names UNKNOWN (non-blocking).
3. Exact initial band / chanspec used by the initial bring-up (`dev+0x84`,
   `band+0x1c` are proven; the chanspec datum passed to `wlc_phy_init` is D4).
4. `sub_6656c`'s `osl_readw(D11+0x3e0)` result is unused in the rev42 path
   (non-blocking).

## 9. D3B implementation gate

```
BAND-0 MHF INITIAL VALUE AND WRITE EXPRESSIONS                      PROVEN
BAND-0 MHF VALUE-CLOSURE (MHF1/MHF2/MHF4/MHF5)                      PROVEN
BAND-0 MHF3 RUNTIME INPUT (antsel_type <- rev11 SPROM)              NOT AVAILABLE
D3B BAND INIT + BSINITVALS ISOLATABLE FROM THE PROVEN D3A1 EXIT?   YES
D3B STOPS BEFORE REAL PHY/RF?                                      YES
D3B IMPLEMENTATION GO:        NO — blocked on the MHF3 input (§3.8)
```

D3B must **not** be implemented with an invented MHF3 default. Unblock by
recovering the rev11 raw offsets (`srom_parsecis`) and reading the raw words
through the existing read-only SPROM path (§3.8.5), by the `wl`-layer MHF
initializer, or by an explicit decision to make the MHF gate a logged runtime
probe (see §3.7).

After §3.1 is resolved, implementation follows the D3A1 pattern: a new isolated
mode (`bsinitvals_test_only=1`), the proven D3A1 prefix, the §2/§3/§4 sequence,
the §6 postconditions, a STOP before `wlc_phy_init`, host + KUnit tests and
docs-check guards. D4 (`wlc_phy_init` -> `wlc_phy_anacore`, first PHY-indirect
MMIO) is the milestone after D3B.

## 10. Unresolved / not covered

D3B does **not** cover: `wlc_phy_init`, `wlc_phy_anacore`,
`wlc_phy_switch_radio_acphy`, `wlc_phy_init_aphy`, PHY tables, radio/channel/
synth, calibration, RX/TX, scan or association.

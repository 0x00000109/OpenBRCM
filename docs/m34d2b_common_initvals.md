# M3.4D2B — rev42 common initvals analysis

**Status: `ANALYSIS ONLY` — NOT IMPLEMENTED — NOT HARDWARE PROVEN.**

Canonical status (exact):
- M3.4D2A = HARDWARE RUNTIME PROVEN
- M3.4D2B analysis = COMPLETE
- M3.4D2B implementation = NOT IMPLEMENTED
- M3.4D2B hardware status = NOT HARDWARE PROVEN

The formal decision `CAN COMMON INITVALS BE ISOLATED SAFELY? YES` means **safe
to design/implement an isolated D2B test**; it is **not** permission to run
hardware and **not** a hardware proof.

No hardware write, no `insmod`/`rmmod`/`modprobe`, no second D2A run. The last
hardware-proven milestone remains **M3.4D2A** (ucode upload + PSM start only;
see `docs/ucode_test.md`).

Scope: understand the exact role, ordering and address semantics of every
record of `d11ac1initvals42` (610 data records) and `d11ac1bsinitvals42`
(73 data records) **before** designing any D2B hardware test. initvals /
band-switch / AC-PHY / radio / calibration / channel / RX / TX remain
**unproven**.

Sources (confidence):
- **C2** — the vendor relocatable object `wlc_hybrid.o_shipped`
  (sha256 `352a6e34…4743`), via the Repo A index (`re.db`) and the byte-exact
  vendor image slices used by M3.4C.1/M3.4D1.
- **C3** — publicly documented D11 register layout (upstream `brcmsmac/d11.h`)
  used **only** for region/field *names*, never copied as code.

Machine-generated artifacts for this report:
- `docs/m34d2b/initvals_classification.md` — full 610 + 73 record table.
- `docs/m34d2b/initvals_classification.json` — same, structured.
- `scripts/analyze_initvals.py` — the deterministic generator.

---

## 1. Exact post-PSM vendor call graph (C2)

Call graph recovered from `re card`/`re seq` and the `wlc_bmac_init`
disassembly (`re fn wlc_bmac_init --asm`). `wlc_bmac_init` (`0x6828a`) is called
from `wlc_init` (`0x3c46f`). Call sites in ascending code address = execution
order:

| # | site | callee | meaning |
|---|---|---|---|
| 1 | `0x6834b` | `wlc_phy_cal_init` | **before** ucode upload (see §9/UNKNOWN) |
| 2 | `0x6836c` | `wlc_bmac_mctrl` | pre-upload `MACCONTROL=0x04000404` (mask `~0`) |
| 3 | `0x684c4` | `sub_607b0` | ucode upload dispatch -> applier `0x60744` |
| 4 | `0x684f8` | `sub_60c2f` | PCM helper (rev <= 10 only; N/A rev42) |
| 5 | `0x68500` | `wlc_bmac_wowlucode_start` | `MACINTSTATUS=0xffffffff`; `MACCONTROL=0x04020402`; bounded `MI_MACSSPNDD` poll |
| 6 | `0x68516` | `wlc_bmac_mctrl` | mask `0xC000`, val `0` (clear GPOUT_SEL; already 0) |
| 7 | `0x6888a` | `sub_60f67` (applier) | non-rev42 table branch (not taken for rev42) |
| 8 | **`0x68b98`** | **`sub_60f67` (applier)** | **common `d11ac1initvals42` (rev42+AC)** |
| 9 | `0x68bab` | `sub_67efd` | post-initvals setup |
| 10 | `0x68f87…0x68fae` | `wlc_bmac_read_shm` | read SHM `0x98/0x9a/0x9c/0x9e` (M_FIFOSIZE0..3) |
| 11 | `0x69047` | `wlc_bmac_mctrl` | mask `0x40060000`, val `0x40020000` |
| 12 | `0x695d8` | `sub_6656c` | **band init**: `d11ac1bsinitvals42` -> `wlc_phy_init` |

rev42 + AC selection for the common table (C2):
- `0x687c9`: `cmp edx, 0x2a` (rev == 42); `0x687dc`: `cmp word [rax+0x1c], 0x0b`
  (`B43_PHYTYPE_AC`);
- `0x687d7`: relocation site `d11ac1initvals42`; on match `jmp 0x68b95`
  (`mov rdi,rbx; call sub_60f67`).

band init (`sub_6656c`, called from `wlc_bmac_init` `0x695d8` and from
`wlc_bmac_set_chanspec`):
- `0x66604`: `cmp eax, 0x2a`; `0x66612`: relocation site `d11ac1bsinitvals42`;
- `0x669ba`: `mov rdi,rbx`; `0x669bd`: `call sub_60f67`;
- `0x669df`: `call wlc_phy_init` — the band table is applied **immediately
  before** PHY init.

Ordering is proven by code addresses, **not** by file names:
`ucode -> PSM start -> common initvals -> (post setup / SHM reads) -> band init
+ PHY init`. Common initvals provably precede any `wlc_phy_init`.

---

## 2. Exact common-initvals consumer (C2)

- Consumer wrapper: `wlc_bmac_write_inits` (`0x60fce`, 13 bytes) — `call
  sub_60f67` then `return 0`.
- Applier: **`sub_60f67`** (`rdi = dev`, `rsi = table`).
- Register base: `r12 = *(dev + 0xd0)` = D11 base.
- Loop disassembly (`0x60f80..0x60fc3`):
  `rax = table + i*8`; `offset = u16[rax]`; if `offset == 0xffff` -> stop;
  `width = u16[rax+2]`; if `width == 2` -> `osl_writew(u16[rax+4], D11+offset)`;
  if `width == 4` -> `osl_writel(u32[rax+4], D11+offset)`;
  no other width; `i++`.
- Argument source: for rev42, a **constant symbol pointer**
  (`d11ac1initvals42`, reloc site `0x687d7`), no runtime patching.
- Termination: `offset == 0xffff` (record #610), enforced by the vendor loop.
- Ordering guarantees: strictly table order, one write per record, no
  reordering, no delay, no branch, no bounds check inside the applier. OpenBRCM
  adds a defensive `offset <= 0xfff` check in the pure parser (`ob_fw.h`).

Record format **re-confirmed** (C2):

```
struct ob_iv { u16 offset; u16 width; u32 value; };  /* little-endian */
/* width in {2,4}; terminator: offset == 0xffff */
```

`d11ac1initvals42`: 611 records = 610 data (113x16-bit, 497x32-bit) + 1
terminator. `d11ac1bsinitvals42`: 74 = 73 data (39x16-bit, 34x32-bit) + 1.

---

## 3. Complete 610-record classification (machine-generated)

Generated by `scripts/analyze_initvals.py`; full table in
`docs/m34d2b/initvals_classification.md`.

`d11ac1initvals42` (610 records) by kind:

| kind | count |
|---|---|
| direct register write | 194 |
| OBJADDR selector write | 76 |
| OBJDATA data write | 340 |

By semantic category:

| category | count |
|---|---|
| OBJ (indirect object window) | 416 |
| TEMPLATE (TX template access 0x130/0x134) | 77 |
| IHR (0x400-0x7fe: RXE/PSM/TXE0/TXE1/TSF/IFS) | 80 |
| SHM (direct window 0x800-0xefe) | 33 |
| MACINT (0x100/0x128/0x12c) | 3 |
| MAC_CORE (0x124) | 1 |

By target space / region:

| space/region | count |
|---|---|
| SHM object window (56 selectors + 320 data) | 376 |
| IHR/TXE1 (0x580-0x5a6) | 20 |
| IHR/PSM (0x480-0x4ff) | 20 |
| SCR object window (20 selectors + 20 data) | 40 |
| SHM direct window (0x800-0xa40) | 33 |
| IHR/RXE (0x400-0x47f) | 12 |
| IHR/TXE0 (0x500-0x510/0x554) | 12 |
| IHR/IFS (0x680-0x69f) | 12 |
| TEMPLATE (0x130/0x134) | 77 |
| IHR/TSF (0x600-0x630) | 4 |
| MACINTSTATUS/MASK + intrcvlazy | 3 |
| MAC core (0x124) | 1 |

Field-level names are only asserted where already recovered by OpenBRCM
(`MACCOMMAND 0x124`, `MACINTSTATUS 0x128`, `MACINTMASK 0x12c`,
`INTRCVLAZY0 0x100`, `TPLATE* 0x130/0x134`, `OBJADDR 0x160`, `OBJDATA 0x164`);
all other offsets are classified to **region** level and their field semantics
are marked **UNKNOWN** (never guessed). The 74 direct offsets with UNKNOWN
field semantics are listed in the generated table.

Note that 194 (direct) + 76 (selector) + 340 (data) = 610, and
320 (SHM data) + 20 (SCR data) = 340.

---

## 4. Indirect / windowed write transactions (C2)

The common table performs all object-memory access through `OBJADDR`
(`0x160`) / `OBJDATA` (`0x164`, `0x166`). `OBJADDR` selector bits (C3):
`SEL_MASK 0x000F0000`, `UCM 0x0 / SHM 0x1 / SCR 0x2 / IHR 0x3 / RCMTA 0x4 /
SRCHM 0x6`, and `WINC 0x01000000 / RINC 0x02000000 / AUTO_INC 0x03000000`.
`OBJADDR = sel | (byte_offset >> 2)`.

- **76 selector transactions** in the common table:
  - **56 SHM windows**, selector `0x0301_xxxx` = `AUTO_INC | SHM_SEL | off/4`,
    auto-incrementing, **320** 32-bit data writes total;
  - **20 SCR windows**, selector `0x0002_xxxx` = `SCR_SEL | off/4`, **no**
    auto-inc, exactly **one** 32-bit data write each.
- **No** UCM (`0x0`), IHR (`0x3`), RCMTA (`0x4`) or SRCHM (`0x6`) selector
  exists in the common table: it does **not** write microcode memory, IHR
  object memory, or match/RCM tables through the window.
- `0x166` (`OBJDATA+2`) is **not** used by the common table (32-bit words
  only); it is used by `d11ac1bsinitvals42` for halfword pair writes.
- One SHM target is intentionally written twice (last wins):
  SHM `0x0014` = `0x002a0000` (rec 1) then `0x000000b4` (rec 198).

---

## 5. Dependencies on the running PSM / MAC state (C2)

At the exact moment the common applier runs (`0x68b98`), the vendor has already
executed PSM start (`0x68500`) and `wlc_bmac_mctrl(mask 0xC000, 0)` (`0x68516`).
No `MACCONTROL` (`0x120`) write occurs between `0x68516` and `0x68b98`, and the
common table itself contains **no `0x120` write**. Therefore the state at the
applier is exactly the D2A exit state:

| bit | value | meaning |
|---|---|---|
| `PSM_RUN (1<<1)` | **1** | PSM is running |
| `EN_MAC (1<<0)` | **0** | MAC is **not** enabled |
| `IHR_EN (1<<10)` | 1 | IHR enabled |
| `INFRA (1<<17)` | 1 | infra |
| `WAKE (1<<26)` | 1 | wake |
| `GMODE (1<<31)` | 0 | not set here |
| `SHM_EN (1<<8)` | **0** | not set by the vendor before initvals |

The vendor applies common initvals with `PSM_RUN=1`, `EN_MAC=0`, MAC suspended
(`MI_MACSSPNDD`), and `SHM_EN=0`. The D2A path reaches exactly this state, so
the D2B precondition is the **proven** D2A success state — not an assumption
based on call order alone.

The table writes SHM through `OBJADDR/OBJDATA` while `MCTL_SHM_EN=0`; the vendor
does the same, so `SHM_EN` is not required for the (indirect) SHM access. (D2A
read SHM and saw `0` because nothing had been written yet, not because access
was blocked — flagged as a residual UNKNOWN, §14.)

---

## 6. MACCONTROL transitions around initvals (C2)

| # | site | mask | value | effect |
|---|---|---|---|---|
| 1 | `0x6836c` | `~0` | `0x04000404` | upload: `IHR_EN|PSM_JMP0|WAKE`, `PSM_RUN=0`, `EN_MAC=0` |
| 2 | PSM start (`0x63854`) | `~0` | `0x04020402` | `IHR_EN|INFRA|PSM_RUN|WAKE`, `EN_MAC=0` |
| 3 | `0x68516` | `0xC000` | `0` | clear `GPOUT_SEL` (already 0) -> `0x04020402` |
| — | **common applier `0x68b98`** | — | — | **table writes no `0x120`; value stays `0x04020402`** |
| 4 | `0x69047` | `0x40060000` | `0x40020000` | after initvals: set `DISCARD_PMQ|INFRA`, clear `AP` |
| 5 | band init `0x695d8` | — | — | `EN_MAC` still clear at PHY init |

The common table changes no `MACCONTROL` bit; `EN_MAC` is never set in the
common stage.

---

## 7. Interrupt-affecting records (C2)

| rec | offset | width | value | name |
|---|---|---|---|---|
| 2 | `0x124` | 4 | `0x00000004` | MACCOMMAND |
| 3 | `0x128` | 4 | `0x00000000` | MACINTSTATUS |
| 4 | `0x12c` | 4 | `0x00000000` | MACINTMASK |
| 82 | `0x100` | 4 | `0x01000000` | INTRCVLAZY0 |

There are **no** writes to `intctrlregs` (`0x20-0x5f`), PSM mirror interrupt
registers (`0x484`, `0x486`, `0x488`, `0x48a`), FIFO interrupt status/mask, or
any per-FIFO `intstatus`/`intmask`. Conclusion: common initvals clears
`MACINTSTATUS` and leaves `MACINTMASK = 0` (**all MAC interrupt sources masked**)
and programs `intrcvlazy[0]`. It does **not** enable any interrupt source.

---

## 8. DMA-affecting records (C2)

No record targets the DMA/PIO block (`0x200-0x37f`), the FIFO diagnostic/agg
block (`0x380-0x394`), and no window selects UCM/RCMTA/SRCHM. Common initvals
contains **no DMA register write**. The separately proven M3.4B FIFO0 RX model
is not touched by this table.

---

## 9. PHY / radio-affecting records (C2)

- No record targets the PHY direct window (`0x3e0-0x3fe`) or the radio access
  registers (`0x3d8-0x3db`).
- No window selects a PHY object space: the common table touches only SHM
  (`0x1`) and SCR (`0x2`).
- The `SHM` values written here are MAC/PSM shared variables (rates, FIFO
  sizes, template pointers, timing); this is **not** PHY register I/O.

Consequence: the common table can be treated as a **D11/MAC-side** step.
`wlc_phy_init` is invoked later, inside band init (`sub_6656c`), and that is
where PHY/radio bring-up begins. This is a genuine boundary, not a convenience:
it is where the vendor code itself separates common initvals from band init.

Residual UNKNOWN: whether any SHM value written here is *consumed by the PSM*
before `EN_MAC`/PHY setup; the table itself is MAC-only.

---

## 10. common vs band-switch initvals (C2)

| property | `d11ac1initvals42` (common) | `d11ac1bsinitvals42` (band) |
|---|---|---|
| data records | 610 | 73 |
| direct writes | 194 (MACINT/MAC core/template/IHR/SHM-direct) | 5 (IHR/IFS `0x680/0x682/0x684/0x686`, IHR/NAV `0x700`) |
| selector windows | 76 (56 SHM auto-inc, 20 SCR) | 34 (SHM, **no auto-inc**) |
| data writes | 340 (all 32-bit) | 34 (all 16-bit; lo `0x164` / hi `0x166`) |
| object spaces | SHM, SCR | SHM only |
| applied at | `wlc_bmac_init` `0x68b98` (bring-up) | `sub_6656c` `0x669bd`, from `wlc_bmac_init` **and** `wlc_bmac_set_chanspec` |
| immediately followed by | `sub_67efd` / SHM reads | **`wlc_phy_init` (`0x669df`)** |

Overlapping SHM byte targets: `0x0010`, `0x001c`, `0x0094`, `0x0990`,
`0x09b8`, `0x09cc`, `0x09dc`, `0x0a00`, `0x0a14`, `0x0a28`. No direct-register
offset overlap, and no conflicting values were found for the overlapping
targets (common writes 32-bit words; band writes 16-bit halves). Direct
offsets are disjoint.

Meaning of "bsinitvals" from call-site behaviour (not the name): it is a
**band/channel-dependent** table applied every time the band is initialised
(initial bring-up and `set_chanspec`), immediately before `wlc_phy_init`. Its
content (IFS slot/SIFS/NAV timing plus per-band SHM parameters, e.g. the
repeated `0x0a00..0x0a28` window) matches a band-switch parameter set, not a
one-time power-on table.

---

## 11. Are all 610 records mandatory? (C2)

Once the rev42+AC path selects the pointer at `0x687d7`, the applier `sub_60f67`
is a single straight-line loop: it applies **all** 610 records, in order, with
**no** branch on chip revision, PHY revision, band or record index, and **no**
value patching before the write. There is exactly one call (`0x68b98`) for this
table on the rev42 path, with a constant symbol address. Therefore, on the
rev42+AC path, all 610 records are applied unconditionally. (The conditional
part is only the *table selection*, `0x687c9`; the non-rev42 table is applied
at the mutually-exclusive `0x6888a` site.)

Some records write **the same target more than once** (e.g. SHM `0x0014`) or
repeated per-antenna/per-rate blocks (`0x0834…0x08e4`, `0x01c0…0x023c`). These
are distinct, ordered writes, not duplicates to drop; OpenBRCM must replicate
the sequence, not de-duplicate it.

---

## 12. Expected post-initvals state and safe read-only gates (C2)

Immediately after the common applier, the vendor (`0x68f87…0x68fae`) reads SHM
`M_FIFOSIZE0..3` (`0x98/0x9a/0x9c/0x9e`). The common table itself writes those
locations inside the window based at SHM `0x90`:

| SHM | width-4 write | resulting 16-bit value |
|---|---|---|
| `0x98` | `0x000001c4` | `M_FIFOSIZE0 = 0x01c4` |
| `0x9a` | (high half) | `M_FIFOSIZE1 = 0x0000` |
| `0x9c` | `0x079e0000` | `M_FIFOSIZE2 = 0x0000` |
| `0x9e` | (high half) | `M_FIFOSIZE3 = 0x079e` |

Other provenance-backed read-only expectations at that point:
- `MACINTMASK == 0` and `MACINTSTATUS == 0` (written by rec 3/4);
- `MACCONTROL` still `0x04020402` (table writes none);
- `PSM_RUN=1`, `EN_MAC=0`.

These are **written by the table itself**, so they are legitimate read-back
gates (no invented validation register). They are the recommended D2B runtime
observables.

### Overwrite caveat (C2)

In the **full vendor path**, the values are not read straight after the table:
at `wlc_bmac_init` `0x68d84…0x68df2` the vendor **re-writes** `M_FIFOSIZE0..3`
(and the TX FIFO control registers) from its in-memory `xmtfifo_sz[]` array
(`[dev+0x150]`) and only then reads them back at `0x68f87…0x68fae`. An isolated
D2B test that applies only the 610-record table and stops does **not** run that
host block, so the table's own values remain in SHM and are a valid,
deterministic postcondition. See the follow-up section "Postcondition
validation" for the exact argument.

Additional deterministic read-backs (written by the table, untouched in the
isolated path): SHM `0x0014 = 0x000000b4` (last of two writes) and
`MACINTMASK == 0`. `MACINTSTATUS` is cleared to `0` by rec 3 but may be
re-asserted by the suspended PSM and is therefore **informational only**.

---

## 13. Proposed smallest D2B hardware boundary (ANALYSIS ONLY)

The vendor call graph proves common initvals **can** be isolated: it is applied
after PSM start and before `sub_6656c` (band init + `wlc_phy_init`), and the
table itself touches no PHY/radio/DMA/interrupt source. Proposed future test
(not implemented here):

1. Prepare D11 exactly as in the proven D2A path
   (`bcma_host_pci_up` + `bcma_core_enable` + FAST clock).
2. `MACCONTROL = 0x04000404` (masked).
3. `OBJADDR = 0x03000000`; upload the vendor ucode (10850 words, count gate).
4. `MACINTSTATUS = 0xffffffff`; `MACCONTROL = 0x04020402`; bounded
   `MI_MACSSPNDD` poll (10 us x <= 100000).
5. Apply **only** `d11ac1initvals42` with the vendor 8-byte applier
   (width2 -> `writew`, width4 -> `writel`; no de-duplication; preserve order).
6. Read-only validation (no equality test on anything not written by the
   table): `M_FIFOSIZE0..3 == 01c4/0000/0000/079e`; `MACINTMASK == 0`;
   `MACINTSTATUS & MI_MACSSPNDD`.
7. **STOP** before `sub_6656c`: no bsinitvals, no `wlc_phy_init`, no
   PHY/radio/calibration/channel, no EN_MAC, no DMA/IRQ.
8. Dedicated `remove()` guard (as D2A) with no RX/IRQ/DMA/mac80211 teardown.

This is safe to *propose only* if the D2B plan also resolves the open items in
§14 first; if the closed-loop check shows the table depends on skipped
pre-steps, the boundary must be revised.

---

## 14. Safety analysis (for the future test)

| item | value |
|---|---|
| MMIO writes | 194 direct (+ 76 selector + 340 data = 610 total writes) |
| indirect object-memory writes | 76 selector + 340 data (SHM/SCR only) |
| enables MAC? | **no** (no `0x120`; `EN_MAC=0`) |
| enables interrupts? | **no** (`MACINTMASK=0`) |
| enables DMA? | **no** (no `0x200-0x3d7`) |
| touches PHY/radio? | **no** (no `0x3d8-0x3fe`; no UCM/IHR/RCMTA windows) |
| loops / waits in table | **none** (single straight-line pass) |
| bounded? | yes, trivially (610 iterations, no polling) |
| expected execution time | microseconds (no delay in the applier) |
| safest failure boundary | the proven D2A STOP, i.e. **before** the applier |
| safest unwind | none: like D2A, create no host resource and write no cleanup register |

### Unresolved UNKNOWNs (must not be guessed)

1. **PHY calibration ordering** — the vendor calls `wlc_phy_cal_init` (`0x6834b`)
   *before* ucode upload; D2A skipped it and PSM start still passed. Whether
   common initvals depend on it is unresolved.
2. **Skipped pre-steps** — D2A also omits `si_eci_init`/`si_seci_init`/`si_gci_init`,
   AMT setup, GPIO and `macphyclk_set`. Whether any affects initvals effect is
   unresolved.
3. **`MCTL_SHM_EN`** — the vendor never sets it before initvals; whether the D11
   stores indirect SHM writes with `SHM_EN=0` is inferred (vendor behaviour) but
   not independently verified.
4. **Field semantics** — 74 direct offsets (IHR/PSM/TXE/SHM) have UNKNOWN
   field-level meaning; only their region is classified.
5. **SCR semantics** — the 20 SCR addresses (`0x0c…0x58`) are classified as
   scratch space; individual meaning is UNKNOWN.
6. **PSM consumption** — whether the running PSM reads any SHM written here
   before the next `MACCONTROL` update is UNKNOWN.

---

## 15. Artifacts and status

| artifact | content |
|---|---|
| `docs/m34d2b/initvals_classification.md` | full 610 + 73 record table (kind/space/target/category/name/confidence) |
| `docs/m34d2b/initvals_classification.json` | structured form + summary + common-vs-bs comparison |
| `scripts/analyze_initvals.py` | deterministic generator (verifies size + sha256 before parsing) |

`scripts/analyze_initvals.py` verifies the exact vendor sizes/sha256
(`b5a2735d…3938` common, `e81a645c…14da` band) and fails closed on any
mismatch. It performs no hardware access.

**M3.4D2B = `ANALYSIS ONLY` / NOT IMPLEMENTED / NOT HARDWARE PROVEN.**
The last hardware-proven milestone remains **M3.4D2A** (ucode upload + PSM
start only).

---

# M3.4D2B follow-up — blocker resolution and GO/NO-GO

Continues the analysis above. Still `ANALYSIS ONLY` / NOT IMPLEMENTED / NOT
HARDWARE PROVEN. No hardware access.

## F1. The pre-ucode `wlc_phy_cal_init` call (resolved)

- Symbol `wlc_phy_cal_init` (`0xb19f7`, C2) is called from `wlc_bmac_init`
  `0x6834b` with `rdi = [[rbx+0xe8]+0x28]` (the PHY software object), the same
  argument as the preceding `wlc_phy_chanspec_radio_set`.
- Its body (`re fn wlc_phy_cal_init --asm`) performs **0 MMIO accesses**. All
  stores are to *software structure fields* of the PHY object (`+0x160`,
  `+0x164`, `+0x187`, `+0x488…`, `+0x3e8…`, `+0x2e4…`, `+0x300`, `+0x408…`).
  These are the PHY's calibration *bookkeeping* (per-chain cal timers / values)
  and the PHY type/revision mirrors, not BCM4352 registers.
- The only possible hardware path is a conditional indirect call
  (`call rax` at `0x0b1bc9`) taken only when `[phy+0x30] != 0`. At this point in
  bring-up `wlc_phy_init` (`0xbabf5`) has **not** run; the target of `+0x30`
  (a PHY-ops slot) is not resolved here and is deliberately not part of any D2B
  boundary.
- Conclusion: the name is misleading. `wlc_phy_cal_init` here initializes PHY
  **software calibration state**; it does **not** perform RF/PHY calibration and
  touches **no** D11/PHY/radio MMIO directly.
- Mandatory before common initvals? **No.** Common initvals is a pure D11
  register/RAM write path with no read of PHY software state. M3.4D2A succeeded
  without this call because D2A never invokes any PHY-object code.
- Documentation correction applied: this report no longer treats `0x6834b` as a
  hardware calibration step.

## F2. Omitted vendor pre-steps — classification

Order is ascending in `wlc_bmac_init`; any step after the common applier
(`0x68b98`) is irrelevant to D2B by construction.

| site | step | MPIO? | needed for rev42/4352? | for D2B | class |
|---|---|---|---|---|---|
| `0x682c6` | `sub_64887` (clkctl/mhf/wake override) | yes (CC/clock) | clock housekeeping | already done by D2A prep (FAST clock) | ALREADY SATISFIED |
| `0x682d6` | `wl_intrsoff` | no (OSL) | host IRQ off | no MAC IRQ used | NOT REQUIRED FOR D2B |
| `0x68305` | `si_pmu_rfldo(1)` | PMU | chip 4352 matches; RF LDO | analog/RF power; not a MAC-side table dependency | NOT REQUIRED FOR D2B |
| `0x68328` | `wlc_setxband` | no (sw) | band sw state | not read by table | NOT REQUIRED FOR D2B |
| `0x6833b` | `wlc_phy_chanspec_radio_set` | no (sw) | radio sw state | not read by table | NOT REQUIRED FOR D2B |
| `0x6834b` | `wlc_phy_cal_init` | **no** | PHY sw state | not read by table | NOT REQUIRED FOR D2B |
| `0x6837d` | `wlc_bmac_btc_mode_set` | ? (BTC) | BT coex | board/BT-specific | NOT REQUIRED FOR D2B |
| `0x683ae/0x683e0` | `si_btc_enable_chipcontrol` | CC | BT coex | BT-specific | NOT REQUIRED FOR D2B |
| `0x683fb` | `si_pmu_chipcontrol` | PMU | analog | not table-dependent | NOT REQUIRED FOR D2B |
| `0x6842e` | `wlc_phy_btclock_war` | ? | BT coex WAR | BT-specific | NOT REQUIRED FOR D2B |
| `0x68485` | `si_eci_init` | coex | conditional (chip caps) | coex | NOT REQUIRED FOR D2B |
| `0x684a8` | `si_seci_init` | coex | conditional (chip caps) | coex | NOT REQUIRED FOR D2B |
| `0x684bc` | `si_gci_init` | coex | conditional (chip caps) | coex | NOT REQUIRED FOR D2B |
| `0x684c4` | `sub_607b0` ucode upload | D11 | yes | **REQUIRED FOR D2B** | (D2A) |
| `0x68500` | `wlc_bmac_wowlucode_start` | D11 | yes | **REQUIRED FOR D2B** | (D2A) |
| `0x68516` | `wlc_bmac_mctrl(0xC000,0)` | D11 | GPOUT_SEL clear | runs in D2A path; keep | REQUIRED FOR D2B (cheap) |
| **`0x68b98`** | **common initvals applier** | D11 | yes | **the D2B step** | — |
| `0x68bab` | `sub_67efd` (RXE/FIFO config) | D11 | post-initvals | not needed for the table | NOT REQUIRED FOR D2B |
| `0x68d84…` | host `xmtfifo_sz` -> `M_FIFOSIZE*` | D11 obj | post-initvals | not needed (isolated path) | NOT REQUIRED FOR D2B |
| `0x69047` | `wlc_bmac_mctrl(0x40060000,0x40020000)` | D11 | post-initvals | after D2B stop | NOT REQUIRED FOR D2B |
| `0x695d8` | `sub_6656c` band init + `wlc_phy_init` | PHY | later stage | must NOT run | out of scope |

`si_eci_init`/`si_seci_init`/`si_gci_init` are conditional (corerev `> 0x0e`
plus chip-core capability bits `sih+0x1b/0x1c` and `dev+0xa7`), i.e. they are
not driven by the initvals table. They are coexistence interfaces, orthogonal
to the MAC-side table.

Verdict: **no omitted pre-step is required to apply the common table**, other
than the D11 clock/core state already proven in D2A. The only previously open
item was SHM access (F3).

## F3. SHM_EN semantics — HARD BLOCKER RESOLVED

- Vendor: pre-upload `MACCONTROL=0x04000404` and PSM-start
  `MACCONTROL=0x04020402` both leave `SHM_EN (1<<8) = 0`; the common applier
  writes SHM through `OBJADDR/OBJDATA` in exactly that state.
- Upstream corroboration (C3, `brcmsmac/main.c brcms_b_coreinit`):
  - `brcms_b_mctrl(~0, MCTL_IHR_EN | MCTL_PSM_JMP_0 | MCTL_WAKE)` (reset PSM),
  - `brcms_ucode_download()`,
  - `macintstatus = -1`, `brcms_b_mctrl(~0, MCTL_IHR_EN | MCTL_INFRA | MCTL_PSM_RUN | MCTL_WAKE)`,
  - `SPINWAIT(MI_MACSSPNDD)` — comment: *"let the PSM run to the suspended
    state"* / *"wait for ucode to self-suspend after auto-init"*,
  - `brcms_c_write_inits(...)` (initvals),
  - `brcms_b_write_shm(M_FIFOSIZE0..3, ...)`.
  `MCTL_SHM_EN` is **defined but never set** anywhere in brcmsmac
  (`grep MCTL_SHM_EN` finds only the `#define`). Its SHM writes use the same
  `OBJADDR_SHM_SEL` object window with `SHM_EN=0`.

Conclusion: `MCTL_SHM_EN` is **not** required for CPU/object-window SHM
accesses; common initvals are intentionally applied with `SHM_EN=0`. The SHM_EN
bit gates a different (in-MAC/PSM) SHM behavior, not the host object window.
This is now proven from both the vendor blob and upstream, and is no longer a
blocker.

MACCONTROL state immediately before / during / after the common applier:
- immediately before (`0x68516` result): `0x04020402`
  (`IHR_EN|INFRA|PSM_RUN|WAKE`, `EN_MAC=0`, `SHM_EN=0`);
- during: unchanged (the table writes no `0x120`);
- immediately after: unchanged `0x04020402`; the next change is `0x69047`
  (`0x40020000`-valued RMW) **after** the applier.

## F4. Disposition of the direct-offset records (side-effect safety)

The classifier now assigns a side-effect class to every record; the full
per-record table and the `direct_offsets` summary are in
`docs/m34d2b/initvals_classification.{md,json}`. Common-table side-effect
counts reconcile to 610 (F11).

Of the 194 direct writes, 74 distinct offsets have UNKNOWN *field* semantics.
All 74 fall into one of these provably-bounded regions, none of which is
`MACCONTROL`, the DMA/PIO block (`0x200-0x3d7`), the PHY window
(`0x3e0-0x3fe`) or the radio window (`0x3d8-0x3db`):

- `IHR/RXE` (`0x402/0x404/0x406/0x40c/0x428/0x450/0x452`) — receive-engine /
  RCM config; no descriptor pointer or engine-enable bit is in the table.
- `IHR/PSM` (`0x490-0x4bc`, `0x4e4`) — PSM block (backoff/BRC/postcard/base);
  configuration only.
- `IHR/TXE0/TXE1` (`0x500/0x502/0x504/0x510/0x554`, `0x580-0x5a6`) — TXE and
  TX-FIFO configuration. `0x500 = 0x4000` and `0x554 = 0xafff`
  (`smpl_clct_stpptr`) have no bit-level name in the C3 reference, so their
  fields are marked **UNKNOWN**, but they are FIFO pointers/control, not
  `EN_MAC`/DMA/IRQ.
- `IHR/TSF` (`0x600/0x612/0x62e/0x630`) and `IHR/IFS`
  (`0x688/0x696/0x69a/0x69c/0x69e`) — timers/IFS timing config.
- `SHM direct window` (`0x800-0xa40`) — shared-memory state.

No direct write can **enable MAC** (no `0x120`), **enable DMA** (no
`0x200-0x3d7`), **unmask IRQ** (`MACINTMASK=0`), **start PHY/radio** (no
`0x3d8-0x3fe`), **reset the core** (no BCMA agent window), or start a MAC
state machine (MAC suspended; `EN_MAC=0`). The remaining low-level UNKNOWNs
(`0x500` bit14, `0x554`, `MACCOMMAND` bit2) are neutralised by the absent
enabling prerequisites (no EN_MAC, no DMA, no descriptor rings published,
MAC suspended). They are recorded as residual UNKNOWNs (F14), not as
engine-start hazards.

## F5. The 20 SCR transactions — meaning

`SCR` = `OBJADDR_SCR_SEL (0x00020000)`, the PSM **scratch-pad** object space
(C3, `brcmsmac d11.h enum _ePsmScratchPadRegDefinitions`). The selector's low
16 bits are the scratch *index* directly (`OBJADDR = SCR_SEL | index`; the
index is confirmed because the C3 reference ORs the enum value without a `>>2`
shift). All 20 common SCR windows are single 32-bit writes, no auto-inc:

| rec | selector | index | name | value |
|---|---|---|---|---|
| 570/571 | `0x00020003` | 3 | `S_DOT11_CWMIN` | `0x1f` |
| 572/573 | `0x00020004` | 4 | `S_DOT11_CWMAX` | `0x3ff` |
| 574/575 | `0x00020005` | 5 | `S_DOT11_CWCUR` | `0x1f` |
| 576/577 | `0x00020006` | 6 | `S_DOT11_SRC_LMT` | `7` |
| 578/579 | `0x00020007` | 7 | `S_DOT11_LRC_LMT` | `4` |
| 580/581 | `0x00020008` | 8 | `S_DOT11_DTIMCOUNT` | `0xffff` |
| 582/583 | `0x00020018` | 24 | `S_THIS_AGG` | `7` |
| 584/585 | `0x00020009` | 9 | `S_SEQ_NUM` | `0` |
| 586/587 | `0x0002000a` | 10 | `S_SEQ_NUM_FRAG` | `0` |
| 588/589 | `0x0002000b` | 11 | `S_FRMRETX_CNT` | `0` |
| 590/591 | `0x0002000c` | 12 | `S_SSRC` | `0` |
| 592/593 | `0x0002000d` | 13 | `S_SLRC` | `0` |
| 594/595 | `0x0002000e` | 14 | `S_EXP_RSP` | `0` |
| 596/597 | `0x0002000f` | 15 | `S_OLD_BREM` | `0` |
| 598/599 | `0x00020010` | 16 | `S_OLD_CWWIN` | `0x1f` |
| 600/601 | `0x00020011` | 17 | `S_TXECTL` | `0` |
| 602/603 | `0x00020012` | 18 | `S_CTXTST` | `0` |
| 604/605 | `0x00020013` | 19 | `S_RXTST` | `0` |
| 606/607 | `0x00020015` | 21 | `S_TXPWR_SUM` | `0` |
| 608/609 | `0x00020016` | 22 | `S_TXPWR_ITER` | `0` |

The values are exactly the 802.11 DCF defaults (`CWmin=31`, `CWmax=1023`,
`SRC=7`, `LRC=4`, `DTIM=0xffff`), confirming SCR is the PSM's per-BSS scratch
state. Consumer: the PSM microcode (and host via scratch read/write); writing
it does not start anything. PSM reads it when it resumes with `EN_MAC` later.

## F6. PSM concurrency / suspension semantics

- `PSM_RUN=1` means the PSM microcode is **loaded and running**.
- `MI_MACSSPNDD` (bit 0 of `MACINTSTATUS`, observed in D2A) means the PSM has
  **self-suspended** after auto-init. Upstream's own wording: *"let the PSM run
  to the suspended state"* and *"wait for ucode to self-suspend after
  auto-init"* (`brcms_b_coreinit`).
- Therefore, at the common applier the PSM is loaded but parked in its idle /
  suspended state, and `EN_MAC=0`. The vendor does **not** pre-suspend it
  further; it relies on `MAC_SUSPENDED`, writes SHM/SCR in that state, and only
  later (after the post-initvals setup) changes `MACCONTROL`.
- There is **no** explicit barrier/sync around the table other than the bounded
  `MI_MACSSPNDD` poll before it. This is the exact state our D2A run reproduces,
  so the ordering is safe to reproduce.

## F7. Interrupt effect

```
IRQ ENABLE EFFECT = NONE
```
Checked across all 610 records: no write to `intctrlregs[0..7]` (`0x20-0x5f`), to per-FIFO
`intmask`/`intstatus` in the DMA block (`0x200-0x37f`), to PSM interrupt mirror
registers (`0x484/0x486/0x488/0x48a`), to BCMA/PCI IRQ routing, or to any
alternate D11 interrupt-enable register. The only interrupt-related writes are
`MACINTSTATUS=0` (status; write-0, no effect on mask), `MACINTMASK=0` (masks
everything), and `INTRCVLAZY0=0x01000000` (lazy coalescing config, not an
enable).

## F8. DMA effect

```
DMA ENABLE EFFECT = NONE
```
Checked both direct offsets and every indirect SHM/SCR target: no `0x200-0x3d7`
write, no descriptor ring pointer/control published, no FIFO DMA enable, no
UCM/RCMTA window. `M_FIFOSIZE*`, TXE/FIFO sizing and PSM scratch are
config/state, not DMA activation. The proven M3.4B FIFO0 RX model is untouched.

## F9. Postcondition validation

| SHM byte | access | value | note |
|---|---|---|---|
| `0x98` | 16-bit write (part of a 32-bit `0x164` window write at SHM `0x98`) | `M_FIFOSIZE0 = 0x01c4` | written by rec 225 |
| `0x9a` | high half | `M_FIFOSIZE1 = 0x0000` | rec 225 |
| `0x9c` | 16-bit (window write at SHM `0x9c`) | `M_FIFOSIZE2 = 0x0000` | rec 226 |
| `0x9e` | high half | `M_FIFOSIZE3 = 0x079e` | rec 226 |

- Exact offsets `0x98/0x9a/0x9c/0x9e` (C3: `M_FIFOSIZE0..3 = 0x4c..0x4f * 2`).
- The table writes these as two 32-bit OBJDATA words inside the window based at
  SHM `0x90`; read-back is 16-bit per `wlc_bmac_read_shm`.
- **Overwrite:** in the full vendor path the host block at `0x68d84…0x68df2`
  rewrites `M_FIFOSIZE0..3` from `xmtfifo_sz[]` before the `0x68f87` read. In
  the isolated D2B path (table only, then stop) that block does not run, so the
  table values remain. Reading them immediately after the applier is a
  read-only SHM access (proven safe in D2A) and directly tests the SHM_EN
  question: if SHM writes were gated, the read would be `0`.
- Recommended strong gates (2–5): (1) `M_FIFOSIZE0..3 =
  01c4/0000/0000/079e`; (2) `MACINTMASK == 0`; (3) `MACCONTROL ==
  0x04020402`; (4) SHM `0x0014 == 0x000000b4`. `MACINTSTATUS` is informational
  only (the suspended PSM may re-assert `MI_MACSSPNDD`).

## F10. Idempotence / retry

- The table is applied exactly **once per core init** (`wlc_bmac_init` ->
  `0x68b98`); it is not applied per channel or per reset in the vendor path.
- Re-applying the same 610 records after a fresh ucode load re-writes the same
  values: all writes are plain configuration/state. No `W1C`-with-1 and no
  self-triggering/self-decrementing counter is written. `MACINTSTATUS=0` is a
  write-0 to a `W1C` register (no effect on any set bit).
- Therefore a failed D2B application can be retried by reloading (ucode rewrite
  + table re-apply) with no cumulative state change. Residual unknown: bit-level
  effects of `txe_ctl (0x500)` and `MACCOMMAND (0x124)` are not proven, but the
  values are constants applied identically on every attempt.

## F11. Complete side-effect accounting (common, must sum to 610)

| side effect | count |
|---|---|
| SHM state (config) — 33 direct + 320 window | 353 |
| object-memory selector (`OBJADDR`, SHM+SCR) | 76 |
| template/object-memory (`0x130/0x134`) | 77 |
| FIFO configuration (RXE/TXE0/TXE1) | 44 |
| PSM configuration (IHR/PSM) | 20 |
| PSM scratch config (SCR window data) | 20 |
| timing/IFS configuration | 12 |
| timer/TSF | 4 |
| interrupt control (`INTRCVLAZY0`,`MACINTMASK`) | 2 |
| status clear (`MACINTSTATUS`) | 1 |
| MAC control (command `0x124`) | 1 |
| **TOTAL** | **610** |

Band-switch table (`73`): SHM state 34, object selector 34, IFS 4, NAV 1 →
**73**. No record is unaccounted for.

## F12. Formal decision

```
CAN COMMON INITVALS BE ISOLATED SAFELY?  YES
```

All required conditions are proven:
- entry MACCONTROL/PSM state known: `0x04020402`, `PSM_RUN=1`, `EN_MAC=0`,
  `SHM_EN=0`, `MI_MACSSPNDD` observed (D2A exit state);
- `SHM_EN` semantics resolved (F3): not required for the object window;
- no PHY/radio activation (F4/F9);
- no DMA enable (F8);
- no IRQ enable (F7);
- no `EN_MAC` activation (F3/F6);
- no unresolved write that can start an uncontrolled engine **given** the
  absent enabling prerequisites (no EN_MAC, no DMA, no IRQ, no reset, MAC
  suspended) — see F4/F14 for the residual bit-level UNKNOWNs;
- table loop bounded (610 straight-line writes, no wait);
- deterministic postconditions available (F9);
- failure boundary understood (D2A STOP before the applier).

This is a GO for **designing** an isolated D2B test. It is not an
implementation: the test still requires explicit human approval and must not be
run by an agent.

## F13. Future isolated D2B test (DESIGN ONLY — do NOT implement)

1. Preparation: exactly the proven D2A subset — `bcma_host_pci_up`,
   D11 `bcma_core_enable`, `bcma_core_set_clockmode(FAST)` (no SPROM/DMA/IRQ).
2. `MACCONTROL = 0x04000404` (masked RMW).
3. `OBJADDR = 0x03000000`; upload 10850 ucode words (count gate); read back
   `OBJADDR`.
4. `MACINTSTATUS = 0xffffffff`; `MACCONTROL = 0x04020402`; bounded
   `MI_MACSSPNDD` poll (10 us x <= 100000).
5. Apply `d11ac1initvals42` via the vendor 8-byte applier (width2 -> `writew`,
   width4 -> `writel`; strictly in order; no de-duplication).
6. Read-only gates: `M_FIFOSIZE0..3 = 01c4/0000/0000/079e`; `MACINTMASK == 0`;
   `MACCONTROL == 0x04020402`; SHM `0x0014 == 0x000000b4`. Optionally read back
   the 20 SCR values.
7. `psm` success invariant: `MACINTSTATUS & MI_MACSSPNDD` (informational).
8. **STOP** before `sub_6656c`: no bsinitvals, no `wlc_phy_init`, no
   PHY/radio/calibration/channel, no EN_MAC, no DMA/IRQ, no `sub_67efd`, no host
   `xmtfifo_sz` write.

Bounds and policy:

| item | bound |
|---|---|
| MMIO writes | 610 (194 direct + 76 selector + 340 data) plus the D2A subset (~10850 OBJDATA + <=10 control writes) |
| indirect transactions | 76 selector + 340 data = 416 |
| loops | ucode 10850 (count), PSM poll <= 100000, table 610 — all bounded |
| expected runtime | PSM poll dominates (D2A: 11 iterations); table = microseconds |
| failure policy | same as D2A: no cleanup register writes, no reset; return `-ETIMEDOUT`/`-EIO`; retry by reload |
| post-success residual state | `PSM_RUN=1`, `EN_MAC=0`, D11 core enabled, common initvals applied; MAC suspended; no DMA/IRQ/PHY |

## F14. Remaining UNKNOWNs (non-blocking, recorded)

1. Bit-level semantics of `txe_ctl (0x500 = 0x4000)`, `smpl_clct_stpptr
   (0x554 = 0xafff)`, `MACCOMMAND (0x124 = DIRFRMQVAL)`, and several RXE/PSM
   fields — names unknown in the C3 reference.
2. Meaning of the `0x8ec/0x8ee` SHM write pairs (`0x4004..0x400e`/`0xffff`).
3. Whether the PSM reads any SHM/SCR between the applier and the next
   `MACCONTROL` update (vendor does no explicit wait).
4. The optional indirect `call rax` inside `wlc_phy_cal_init` (`phy+0x30`) when
   non-null; not invoked by any D2B path.
5. Exact host `xmtfifo_sz[]` default that the full vendor path later writes over
   `M_FIFOSIZE*` (irrelevant to the isolated path).

# REV42-PCIE2-WINDOW-MAPPING — definitive BCM4352 BAR0 window mapping

> ## ⛔ HISTORICAL ONLY — NOT A CURRENT PROCEDURE, NOT A CURRENT FACT
>
> **Canonical replacement for every address, mode and status claim in this
> file:**
>
> | topic | canonical source |
> | :--- | :--- |
> | D11 address model, `core->addr`, `core->wrap`, superseded models | [`docs/m34d4/addr_provenance.md`](docs/m34d4/addr_provenance.md) |
> | the RUN 1 measurement and `0x0FFC` semantics | [`docs/m34d4/d11_addr_u1_measured.md`](docs/m34d4/d11_addr_u1_measured.md) |
> | the RUN 2 execution and the core-enable proof | [`docs/m34d4/rev42_ai_enable_freeze.md`](docs/m34d4/rev42_ai_enable_freeze.md) |
> | the five rev42 probe modes and their per-mode hardware status | [`docs/m34d4/rev42_diag_modes.md`](docs/m34d4/rev42_diag_modes.md) |
> | live project state and STOP boundary | [`docs/agent-state.md`](docs/agent-state.md) |
>
> **What changed after this file was written (2026-09-27 M3.4D reconciliation):**
>
> - `core->addr` (D11 core functional) was **measured** as **`0x18001000`** and
>   `core->wrap` (BCMA wrapper) as **`0x18101000`**. These are
>   `PROVEN_HARDWARE` (RUN 1, marker `openbrcm-test-20260927-014301-8836`).
>   Any statement here that a window base is "UNKNOWN", or that proposes
>   `0x18000000` / `0x18001000` / `0x18100000` as the base, is **SUPERSEDED**.
> - The `+0x1000` (ROOT+slot) and "two-root" relations are **RETRACTED**. The two
>   window addresses are independent eROM-derived values in two independent PCI
>   config registers (`0x80` and `0x70` on PCIe2); no arithmetic relates them.
> - **No positive role is claimed for `0x18000000`** — specifically, it is *not*
>   claimed to be ChipCommon. The only canonical statements about it are
>   negative.
> - Raw `PCI_CFG_WRITE` of `0x80` / `0x70` / `0xAC` is **withdrawn**. The driver
>   performs no window-register write; BCMA owns the window switch.
> - `AI_WRAPPER_UNREACHABLE` and any "all-ones proves a wrong window" rule are
>   **RETRACTED**. All-ones is an observation only.
> - `0x0FFC` is a core-window **probe offset of UNKNOWN register identity**. It
>   is **not** a readability discriminator, **not** a liveness discriminator and
>   **not** evidence of a wrong window. Hardware measured `0xffffffff` there both
>   with the core down (RUN 1) and with `ai_iscoreup = 1` (RUN 2, marker
>   `openbrcm-test-20260927-100501-6116`) — which is exactly why it discriminates
>   nothing.
> - `rev42_ai_probe_only` (RUN 1) and `rev42_ai_enable_only` (RUN 2) have each
>   been executed **once**. Both candidates are **spent**; neither is to be
>   re-run merely for confirmation, and this file authorises no execution.
>
> The run records and analysis **inside this file are kept verbatim** as
> historical evidence. Nothing here should be read as a current fact or as an
> instruction to touch hardware.


> ### ⚠ THIS OLDER BANNER IS ITSELF SUPERSEDED
>
> The banner immediately below predates the 2026-09-27 hardware measurement and
> is retained only as history. Two of its statements are now **wrong** and are
> overridden by the banner above:
>
> - it says the numeric D11 AI-wrapper backplane base is `UNKNOWN` — it is now
>   **measured**: `core->wrap = 0x18101000` (`PROVEN_HARDWARE`, RUN 1);
> - it says the `+0x1000` relation is *retained* — that relation is now
>   **RETRACTED**. It conflated `BCMA_CORE_SIZE` (a BAR0-internal slot size)
>   with the wrapper's backplane address; the real delta is `0x100000`.

> ## ⚠ SUPERSEDED AS A PROCEDURE — HISTORICAL STATIC/HW EVIDENCE ONLY
>
> The static RE and the single read-only hardware result below are kept as
> evidence. The **window-base conclusions are SUPERSEDED** and must not be
> used as a current procedure or as a source of an address value:
>
> - The **numeric** D11 AI-wrapper backplane base is **`UNKNOWN`**, not
>   `0x18001000` and not `0x18100000`. Both numbers were *predictions*, never
>   measured on this board. See
>   [`docs/m34d4/addr_provenance.md`](docs/m34d4/addr_provenance.md) §2
>   (D11-ADDR-U1) and §3 (D11-ADDR-S3, D11-ADDR-S4).
> - The `+0x1000` **relation** and the PCIe2 `cfg 0x70` selector are retained
>   (addr_provenance D11-ADDR-2, D11-ADDR-5), but the driver does **not** need
>   any numeric base: BCMA enumerates `core->addr` / `core->wrap` and owns the
>   window switch.
> - The `0x70` / `0xAC` **write** experiments and the “sweep the wrapper base”
>   proposal below are **withdrawn**. Writing a window register behind BCMA's
>   back desynchronises `bus->mapped_core`; the driver no longer does this and
>   neither should an operator.
> - Current procedure: `scripts/runtime-test.sh --mode rev42_ai_probe_only`
>   (read-only; dry-run first). Never `insmod` a hand-written window address.

**Static RE + one read-only hardware result. No hardware executed for this
analysis. No driver behaviour changed.** This document **corrects and
supersedes** the `PCI cfg 0xAC` window mapping in
`/media/kartashoff/Storage/mrbin/REV42-AI-WRAPPER-RESET.md` and
`.../artifacts/rev42_ai_wrapper_reset.yaml` (those files live outside this
repository and are read-only here, so the correction is recorded here).

Vendor blob: `wlc_hybrid.o_shipped` sha256 `352a6e349f74…` (`elf`/`st_value`
addresses; radare2 vaddr = `st_value + 0x08000040`).

## 0. Headline

> **BCM4352 uses PCIe2 host semantics. The effective BAR0 second/wrapper window
> register is `PCI cfg 0x70` (`BCMA_PCIE2_BAR0_WIN2`), NOT `0xAC`. `0xAC` is
> DISPROVED for BCM4352 by both vendor control flow and a hardware readback.**

## 1. Vendor dispatch — how the vendor chooses the window register

`ai_setcoreidx` (`elf 0x0001bf6`), bustype==1 (PCI/PCIe) path:

```
0x1cb7 mov rdi,[rdi+0x58]                  ; PCI config handle
0x1cc0 mov esi,0x80                        ; offset 0x80 = BAR0_WIN
0x1cc5 call osl_pci_write_config           ; write 0x80 = core backplane base (ecx = si->[0x248+idx*4])
0x1cca cmp dword [rbx+4],1                 ; bustype == 1 ?
0x1cd5 jne 0x1cf3                           ; else -> 0xAC
0x1cd7 cmp dword [rbx+8],0x83C             ; si->(0x08) == 0x83C (PCIe2 core type) ?
0x1cde jne 0x1cf3                           ; else -> 0xAC
0x1cec mov esi,0x70                         ; BCMA_PCIE2_BAR0_WIN2
0x1d04 call osl_pci_write_config           ; write 0x70 = wrapper base (r14d = si->[0x650+idx*4])
0x1cf3 mov esi,0xAC                         ; BCMA_PCI_BAR0_WIN2  (legacy)
0x1d04 call osl_pci_write_config
```

**Exact condition for `0x70`: `bustype == 1 && si->(0x08) == 0x83C`.** Otherwise
`0xAC`.

### 1.1 `si->(0x08)` is a PCIe/PCIe2 core type, not `0x0804`

`si_doattach` region, `elf 0x00022ec9..0x00022ef7`:

```
0x22ec9 movl $0x804,0x8(%rbx)              ; one path sets 0x0804
...
0x22eed sbb  %eax,%eax                     ; CF ? 0 : -1
0x22eef and  $0xffffffe4,%eax              ; -> 0 or 0xffffffe4
0x22ef2 add  $0x83c,%eax                   ; -> 0x83c  or  0x820
0x22ef7 mov  %eax,0x8(%rbx)                ; si->(0x08) = 0x83c or 0x820
```

So the PCIe-family value is **`0x820` (PCIe1) or `0x83C` (PCIe2)** — **never
`0x0804`**. The earlier brief read `0x0804` from the SB path
(`sb_commit` `elf 0x0001c719/0x1c724`) and mis-attributed it to the D11 bus id.

BCM4352 has a PCIe2 core, so `si->(0x08) == 0x83C` → the vendor writes
`cfg 0x70`.

## 2. Task 1/2 — every vendor config window access

| function | offset | condition | width | value / source | purpose | host gen |
|---|---|---|---|---|---|---|
| `ai_setcoreidx` `0x1cc5` | `0x80` | bustype==1 | 4 | `si->(0x248+idx*4)` (0x18000000) | select CORE window | PCI/PCIe |
| `ai_setcoreidx` `0x1d04` | **`0x70`** | bustype==1 **&& si->(0x08)==0x83C** | 4 | `si->(0x650+idx*4)` (wrapper base) | select AI WRAPPER window | **PCIe2** |
| `ai_setcoreidx` `0x1d04` | `0xAC` | otherwise | 4 | `si->(0x650+idx*4)` | select wrapper window | PCIe1/legacy |
| `si_doattach` (`mov $0x18000000,%edi` `0x22b8d`) | `0x80` | attach | 4 | `0x18000000` | initial CORE window | PCI/PCIe |

**Answer:** on BCM4352 the effective BAR0 WIN2 register is **`0x70`**, proven
from `ai_setcoreidx` control flow (not from Linux names) and corroborated by the
`si->(0x08)` value being a PCIe2 core type.

## 3. Task 3 — Linux bcma comparison

`/media/kartashoff/Storage/Install/core/linux-7.2.2`:

```c
/* drivers/bcma/host_pci.c:14-24 */
int win2 = core->bus->host_is_pcie2 ? BCMA_PCIE2_BAR0_WIN2 : BCMA_PCI_BAR0_WIN2;
pci_write_config_dword(core->bus->host_pci, BCMA_PCI_BAR0_WIN,  core->addr);
pci_write_config_dword(core->bus->host_pci, win2,               core->wrap);
/* host_pci.c:219-220 */
if (bcma_find_core(bus, BCMA_CORE_PCIE2)) bus->host_is_pcie2 = true;
```

| host type | BAR0 core window | BAR0 wrapper/window2 | evidence |
|---|---|---|---|
| vendor, PCIe2 | `cfg 0x80` = core base | **`cfg 0x70`** = wrapper base | `ai_setcoreidx` `0x1cc5/0x1cec` |
| vendor, legacy | `cfg 0x80` | `cfg 0xAC` | `ai_setcoreidx` `0x1cff` |
| bcma, PCIe2 | `cfg 0x80` (`BCMA_PCI_BAR0_WIN`) | `cfg 0x70` (`BCMA_PCIE2_BAR0_WIN2`) | `host_pci.c:16-21`, `bcma_regs.h:70` |
| bcma, legacy | `cfg 0x80` | `cfg 0xAC` (`BCMA_PCI_BAR0_WIN2`) | `bcma_regs.h:61` |

bcma's `host_is_pcie2` (= `BCMA_CORE_PCIE2` present) is the same discriminator as
the vendor's `si->(0x08) == 0x83C`. **Linux and the vendor agree for BCM4352: the
second window is `0x70`.**

## 4. Task 4 — core base and wrapper base

| item | value | confidence |
|---|---|---|
| D11 core base (backplane) | **`0x18000000`** | CONFIRMED_VENDOR_REV42 (`si_doattach` literals; `si->(0x248+idx*4)`) |
| D11 AI wrapper base (backplane) | **`0x18001000`** = core base + `0x1000` | STRONGLY_SUPPORTED_REV42 (the `+0x1000` relation is CONFIRMED, `ai_scan` `elf 0x0001fb5`; the numeric wrapper address comes from the per-core `si->(0x650+idx*4)` table whose writer is outside the object — R-7a) |

`0x18001000` remains the expected wrapper base for this D11 instance; the hardware
run did not test it (the `0xAC` write was rejected first). It should be treated
as the primary candidate but re-confirmed by the read-only probe in §6.

## 5. Task 5 — BAR0 MMIO offsets for PCIe2

| window | PCI config selector | BAR0 MMIO offset |
|---|---|---|
| D11 core | `0x80` = `0x18000000` | **`BAR0 + 0x0000`** (core functional) |
| D11 AI wrapper | `0x70` = `0x18001000` | **`BAR0 + 0x1000`** |

bcma agrees: `bcma_host_pci_read32` → `bus->mmio + offset`;
`bcma_host_pci_aread32` → `bus->mmio + 0x1000 + offset` (`host_pci.c:56,134`).
The config selector offset (`0x70`) and the BAR0 MMIO offset (`0x1000`) are
**different numbers** and must not be conflated.

## 6. Task 6 — safe read-only probe design (DESIGNED, NOT EXECUTED)

No reset, no wrapper write, no D11 functional read.

```
read cfg 0x80 (before); read cfg 0x70 (before)         # window state
PRE/POST: write cfg 0x80 = 0x18000000 ; readback         # CORE window
PRE/POST: write cfg 0x70 = 0x18001000 ; readback         # AI WRAPPER window (PCIe2)
if readback mismatch -> STOP (AI_WRAPPER_UNREACHABLE)
PRE/POST: read BAR0 + 0x0FFC                              # core identity gate (SI_ID)
if 0xffffffff -> STOP (CORE_WINDOW_UNREACHABLE)
PRE/POST: read BAR0 + 0x1000 + 0x0804                     # RESET_ST  <-- safest identity/reachability gate
PRE/POST: read BAR0 + 0x1000 + 0x0800                     # RESET_CTL (bit0)
if either 0xffffffff -> STOP (AI_WRAPPER_UNREACHABLE)
PRE/POST: read BAR0 + 0x1000 + 0x0408                     # IOCTL (ai_iscoreup term 1)
ai_iscoreup = ((IOCTL & 3) == 1) && ((RESET_CTL & 1) == 0)
STOP
```

**Safest first wrapper register:** **`0x0804` (`RESET_ST`)** — read-only in the
entire object, and the vendor's own *first* action in both `ai_core_reset` and
`ai_core_disable`; a down core returns `0`/`1`, never all-ones (all-ones is the
positive signature of an unselected window). `0x0800` (`RESET_CTL`, read bit 0)
is the second safe gate.

## 7. Why `cfg 0xAC` ignored the write (Task 7)

`0xAC` is `BCMA_PCI_BAR0_WIN2`, the **legacy/PCIe1** second-window register. On
this PCIe2 part the hardware decodes the second BAR0 window through the PCIe2
window register `0x70`; the `0xAC` location is not the active window selector, so
the write had no effect (readback unchanged `0x00020010`). The vendor never
selects `0xAC` for this chip either (`si->(0x08) == 0x83C` → `0x70`).

## 8. Final answers

1. **Is BCM4352 definitely using PCIe2 host semantics?** **Yes** — PCIe2 core
   present (`host_is_pcie2 = true`), vendor `si->(0x08)` in the PCIe family
   `{0x820, 0x83C}` and equal to `0x83C` for this part.
2. **Exact BAR0 core-window PCI config offset:** **`0x80`**.
3. **Exact BAR0 second/wrapper-window PCI config offset:** **`0x70`**
   (`BCMA_PCIE2_BAR0_WIN2`). `0xAC` is DISPROVED for BCM4352.
4. **Exact D11 core base:** **`0x18000000`** (CONFIRMED).
5. **Exact D11 AI-wrapper base:** **`0x18001000`** (STRONGLY_SUPPORTED; `+0x1000`).
6. **BAR0 MMIO location of the wrapper after selection:** **`BAR0 + 0x1000`**.
7. **Why `0xAC` ignored the write:** it is the legacy/PCIe1 window register; the
   active PCIe2 second-window selector on this part is `0x70`.
8. **Is `cfg 0x70` CONFIRMED for this chip?** **Yes** — by vendor `ai_setcoreidx`
   control flow, by bcma's `host_is_pcie2`, and consistent with the hardware
   `0xAC` rejection.
9. **Safest first wrapper register to read:** **`0x0804` (`RESET_ST`)** (then
   `0x0800`).
10. **Is another READ-ONLY hardware validation now fully specified?** **Yes** —
    §6. It selects the wrapper window via `0x70` and reads only
    `0x0FFC`/`0x1000+0x0804/0x0800/0x0408`, with no reset and no wrapper write.

## 9. Hardware evidence (2026-09, read-only)

| run | cfg | before | write | readback | result |
|---|---|---|---|---|---|
| A | `0x80` | `0x18003000` | `0x18000000` | `0x18000000` | accepted |
| A | `0xAC` | `0x00020010` | `0x18001000` | `0x00020010` | **rejected/no effect** |
| B/C | `0x80` | `0x18000000` | `0x18000000` | `0x18000000` | accepted |
| B/C | `0x70` | `0x18103000` | `0x18001000` | `0x18001000` | **accepted** |

Run A established that `0xAC` is the wrong register. Runs B/C wrote the correct
register (`0x70`, accepted) and then attempted the wrapper reads:

| read | value |
|---|---|
| `BAR0 + 0x1000 + 0x0804` (RESET_ST) | `0xffffffff` |
| `BAR0 + 0x1000 + 0x0800` (RESET_CTL) | `0xffffffff` |
| `BAR0 + 0x1000 + 0x0408` (IOCTL) | `0xffffffff` |
| `BAR0 + 0x0FFC` (core SI_ID) | `0xffffffff` |

Classification: `AI_WRAPPER_UNREACHABLE`. No reset, no wrapper write, no
DEVICE_LOST, no AER; device remained enumerated, `rmmod` clean.

### 9.1 The remaining question — wrapper BASE / BAR0 offset

`cfg 0x70` is now confirmed writable, but the wrapper registers still read
all-ones at `BAR0 + 0x1000` with the inferred wrapper base `0x18001000`. Two
candidates remain, to be resolved before any `ai_core_reset` design:

1. **Wrapper base value.** bcma's last-selected core showed `cfg 0x70 =
   0x18103000` for a core whose `cfg 0x80 = 0x18003000` — i.e. wrapper base =
   core base + `0x100000`, **not** core base + `0x1000`. The D11 (`core base
   0x18000000`) wrapper base may therefore be **`0x18100000`**, not `0x18001000`.
   (The `+0x1000` in `ai_scan` and the `si->(0x650)` table need re-examination;
   the `si->(0x548)`/agent relation may differ from the per-core window base.)
2. **BAR0 offset / enabling.** The PCIe2 wrapper window may not appear at
   `BAR0 + 0x1000` the same way as the legacy PCIe1 agent window; it may need a
   different offset or the PCIe2 backplane window enabled first.

The next read-only run should sweep the wrapper base (`0x18001000` vs
`0x18100000`) and/or confirm the BAR0 wrapper offset, reading only `0x0804`
first.

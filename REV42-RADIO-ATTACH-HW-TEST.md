# REV42-RADIO-ATTACH-HW-TEST — controlled hardware validation of the recovered cold-attach radio sequence

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


**Status: RUN EXECUTED on a fresh cold boot. The corrected conditional
`bcma_core_enable` path returned success but the D11 functional window stayed
all-ones, so the soft accessibility gate STOPPED (no latch). Classified
`CORE_RESET/ENABLE SEMANTICS DIVERGENCE`. SITE A not reached. No device loss.**

- Spec: `/media/kartashoff/Storage/mrbin/REV42-RADIO-ATTACH-SEQUENCE.md`,
  `/media/kartashoff/Storage/mrbin/artifacts/rev42_radio_attach_sequence.yaml`.
- Mode: `rev42_attach_probe_only=1` (isolated).
- Tested module: `openbrcm.ko` sha256
  `508fa029e54167281748660ffbee53dead2094ba8e0f37f2381eb032c33cc28d` (signed).
- Trace: `artifacts/rev42_radio_attach_probe.json`.

## 0. Preconditions (all passed)

| Check | Result |
| --- | --- |
| Fresh cold boot | YES — `uptime -s` = `2026-09-26 19:50:42` (prev boot `19:40:40`), uptime 1 min |
| PCI `14e4:43b1` | YES |
| Conflicting driver | NONE |
| DEVICE_LOST before run | NONE (clean) |
| Module hash | `508fa029…` (corrected), signed `Broadcom Driver MOK` |

## 1. Executed sequence (authoritative log)

```
CP0 soft: MACCONTROL=0xffffffff  (core down; NOT loss)                 [OK]
si_iscoreup(before)=false  raw=0xffffffff  (D11+0xF98)                 [OK]
CP1 soft: MACCONTROL=0xffffffff  (core down; NOT loss)                 [OK]
sh->0x184 = 0 (cold, asserted)
bcma_core_set_clockmode(FAST)
sb_core_reset: bcma_core_is_enabled == false -> bcma_core_enable(core, 0) rc=0
soft gate: MACCONTROL = 0xffffffff  -> D11 window not accessible
STOP (soft, no latch).  SITE A never reached.
```

No bcma/PCIe2 errors; PCI device still enumerated; module refcount **0**, no
fatal latch.

## 2. Required report

| # | Item | Result |
| --- | --- | --- |
| 1 | `si_iscoreup` before | **false**, raw `0xffffffff` (D11 window word `0x0F98`) |
| 2 | core-enable/reset operation | conditional: `bcma_core_is_enabled()` was false, so `bcma_core_enable(core, 0)` was called. **No forced `bcma_core_disable`.** Returned rc=0 |
| 3 | raw core-state register before/after | before (`D11+0x0F98`) = `0xffffffff`; after, the only post-enable window value captured is `MACCONTROL = 0xffffffff` (the post-enable `si_iscoreup` read was not reached — the gate stopped first). All-ones at `0x0F98` implies `(v & 0x10007) != 0x10000` → `si_iscoreup` would be false |
| 4 | `si_iscoreup` after | not directly read (gate stopped); implied **false** |
| 5 | D11 functionally accessible? | **NO** — `MACCONTROL` remained `0xffffffff` after `bcma_core_enable` returned 0 |
| 6 | first valid MACCONTROL | none (all-ones) |
| 7 | SITE A reached? | **NO** |
| 8 | SITE A survived? | not reached |
| 9 | last successful checkpoint | **CP1** (soft, pre-`sb_core_reset`); stopped at the post-enable soft gate before CP2 |
| 10 | first divergence | **`CORE_RESET/ENABLE SEMANTICS DIVERGENCE`**: `bcma_core_enable()` reports success but the D11 functional window is not readable. bcma's generic enable is **not** the vendor `sb_core_reset` (no terminal `0x00050000` to `SB+0xF98`, no vendor core-state transition) |

## 3. Classification and next action

```
CORE_DOWN                                    (CP0/CP1, pre-enable)
-> bcma_core_enable() rc=0
-> CORE_ENABLED_BUT_D11_INACCESSIBLE         (soft gate: MACCONTROL=0xffffffff)
STOP.
```

Per the brief: **do not proceed to PHY reset or SITE A; do not invent another
reset sequence.** This is recorded as `CORE_RESET/ENABLE SEMANTICS DIVERGENCE`.

State left on the machine: **no DEVICE_LOST, no fatal latch** — the D11 window is
all-ones because the core is down / not brought up, not because the device is
lost. The module is loaded with refcount 0 and the D11 core is unbound from
`openbrcm` (`bcma0:1 -> driver`). A reboot (or `rmmod openbrcm`) is needed before
another attempt; a reboot is not required for hardware safety.

**Next task (per the brief):** static reconstruction of the exact vendor
`sb_core_reset` / core-state register sequence (the `SB+0x0F98` write path), and
of how OpenBRCM should reach `CORE_ENABLED + D11_ACCESSIBLE` on BCM4352/PCIe.

No ucode, initvals, bsinitvals, DMA, RX, TX, host IRQ or mac80211 were used.
Unrelated production code unchanged.

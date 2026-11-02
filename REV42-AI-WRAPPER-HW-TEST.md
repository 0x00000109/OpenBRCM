# REV42-AI-WRAPPER-HW-TEST — read-only AI-wrapper mapping validation (BCM4352)

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
> The banner immediately below was written before the 2026-09-27 hardware
> measurement and is retained only as history. Two of its statements are now
> **wrong** and are overridden by the banner above:
>
> - it says the wrapper base is *not* `0x18001000` and not `0x18100000` as
>   though unresolved — it is now **measured**: `core->wrap = 0x18101000`;
> - it says "the current execution procedure is
>   `scripts/runtime-test.sh --mode rev42_ai_probe_only`" — that mode has since
>   been **executed once** (RUN 1) and its candidate is **spent**.

> ## ⚠ SUPERSEDED — HISTORICAL RUN RECORD, NOT THE CURRENT PROCEDURE
>
> The **run record** below is genuine historical evidence and is kept as-is.
> The **method** it describes is **SUPERSEDED and MUST NOT be re-run**:
>
> - `PCI_CFG_WRITE` of `0x80` / `0xAC`, and the literal wrapper base
>   `0x18001000`, are **withdrawn**. The current `rev42_ai_probe_only` mode
>   writes **no** window register at all and embeds **no** backplane address.
> - `0xAC` (`BCMA_PCI_BAR0_WIN2`) is the **legacy PCI** register. This board is
>   **PCIe2**, where the wrapper window register is **`0x70`**
>   (`BCMA_PCIE2_BAR0_WIN2`). Reading `0xAC` here is a latent bug, not a
>   fallback.
> - The classification `AI_WRAPPER_UNREACHABLE` is **withdrawn**: all-ones is an
>   **observation only**, never a wrong-window discriminator. Classification is
>   taken solely from the vendor `ai_iscoreup` predicate.
> - The current addressing contract is BCMA-owned: `core->addr` /
>   `core->wrap` read through `bcma_read32` / `bcma_aread32`, with the BAR0
>   window registers only ever **read back** as coherency evidence. See
>   [`docs/m34d4/addr_provenance.md`](docs/m34d4/addr_provenance.md) §1–§4 and
>   [`REV42-PCIE2-WINDOW-MAPPING.md`](REV42-PCIE2-WINDOW-MAPPING.md).
> - The current execution procedure is
>   `scripts/runtime-test.sh --mode rev42_ai_probe_only` (dry-run first).
>
> Historical `0x18001000` / `0x18100000` numbers below are retained **only** as
> a record of what was tried; both are `UNKNOWN`/withdrawn in
> `addr_provenance.md` (D11-ADDR-U1, D11-ADDR-S3, D11-ADDR-S4).

**Status: RUN EXECUTED. STOPPED at window readback. Classification:
`AI_WRAPPER_UNREACHABLE`. No reset; no device loss; module `rmmod` clean.**

- Spec (read in full): `/media/kartashoff/Storage/mrbin/REV42-AI-WRAPPER-RESET.md`,
  `/media/kartashoff/Storage/mrbin/artifacts/rev42_ai_wrapper_reset.yaml`.
- Mode: new read-only isolated `rev42_ai_probe_only=1` (`openbrcm.ko`).
- Method: writes ONLY PCI config `0x80`/`0xAC` (BAR windows); every MMIO access
  is a read; no wrapper register is written; `ai_core_reset` is never run.
- Crash capture active: netconsole → Android `260f3268` (`/sdcard/OpenBRCM/netconsole.log`);
  persistent journal `Storage=persistent`, `SyncIntervalSec=1s`.
- Trace artifact: `artifacts/rev42_ai_wrapper_probe.json`.

## 1. PCI cfg 0x80 before/after

| | value |
|---|---|
| before | `0x18003000` |
| written | `0x18000000` |
| after (readback) | **`0x18000000`** — **accepted** |

## 2. PCI cfg 0xAC before/after

| | value |
|---|---|
| before | `0x00020010` |
| written | `0x18001000` |
| after (readback) | **`0x00020010`** — **NOT accepted (unchanged)** |

The `0xAC` write had **no effect**: the readback was identical to the pre-write
value.

## 3. Core identity gate

**Not reached.** The run STOPPED on the `0xAC` readback mismatch before reading
`BAR0 + 0x0FFC`/`0x0FF8` (per the experiment rule "if the readback does not
match: STOP").

## 4. Raw AI-wrapper register values

**Not read.** `BAR0 + 0x1000 + 0x0408/0x0800/0x0804` were not accessed (the
wrapper window could not be selected).

## 5. `ai_iscoreup` result

**Not evaluated** (predicate inputs not readable).

## 6. Final classification

**`AI_WRAPPER_UNREACHABLE`.**

Good news: the **core** window register (`0x80`) accepted `0x18000000` and read
back correctly → the D11 core window mapping is writable and the `0x80` half of
the mapping works.

The failure is the **AI wrapper** window register: `0xAC` rejected the documented
value. This directly **refutes** the YAML's `0xAC` selection for this part.

**Root-cause hypothesis (STRONGLY_SUPPORTED):** BCM4352 is a **PCIe2** device
(`host_is_pcie2=1`, bus id `0x0804`), and the second BAR window register for
PCIe2 is **`BCMA_PCIE2_BAR0_WIN2 = 0x70`** — the register Linux bcma actually
writes in `bcma_host_pci_switch_core`. The YAML used `0xAC`
(`BCMA_PCI_BAR0_WIN2`) based on `si->(0x08) != 0x83C`, which does not match this
part. `0x00020010` is likely an unrelated config register (the write is not
absorbed).

## 7. Last PRE and POST sequence observed locally

`PRE seq=4 phase=ai_wrapper_validate op=PCI_CFG_WRITE offset=0xAC value=0x18001000`
→ `POST seq=4 result=ok readback=0x00020010` (last pair in the local dmesg).

## 8. Last PRE and POST sequence confirmed in the Android log

Identical — the full run was captured off-box on the Android receiver
(`POST seq=4 result=ok readback=0x00020010`), confirming the off-box tail works.

## 9. Messages lost?

No. All four PRE/POST pairs (seq 1–4) plus BEGIN/classification appear both
locally and in the Android log, in order.

## 10. Safe to design the next single-transition `ai_core_reset` test?

**NO.** The AI wrapper window is not confirmed reachable: the documented `0xAC`
register does not select it. Before any `ai_core_reset` experiment the wrapper
window mapping must be resolved — most likely by validating `BCMA_PCIE2_BAR0_WIN2
= 0x70` (the register bcma itself uses) and re-reading `BAR0+0x1000+0x0408/0x0800/0x0804`.

## Safety / health

- **No DEVICE_LOST, no AER**, PCI device still enumerated; `rmmod openbrcm` clean.
- No wrapper register written; no `ai_core_reset`; no PHY/radio/ucode/initvals/
  DMA/RX/TX/IRQ/mac80211.
- The device appears healthy, but per the experiment rule the run STOPPED at the
  first mismatch and no alternate window was probed.

## Next step (recommended, separate task)

Statically confirm the PCIe2 second-window register (`0x70` vs `0xAC`) against
the vendor blob and bcma, then re-run this read-only validation selecting the
wrapper window with the correct register before ever designing an
`ai_core_reset` transition.

# OpenBRCM

A **clean-room**, GPL-2.0 open-source Linux driver for Broadcom 43xx PCIe
SoftMAC Wi-Fi controllers, built on `mac80211` / `cfg80211`.

OpenBRCM is derived **solely** from the OpenBRCM reverse-engineering
specification and publicly documented silicon behaviour — it contains no code
from the proprietary `wlc_hybrid.o_shipped` blob and no code copied from other
drivers. See [`docs/provenance.md`](docs/provenance.md).

> Status: **early** — M0–M2.5 (bus/silicon/mac80211) and M3.1–M3.4D1
> (DMA64 model, IRQ plumbing, FIFO0 RX bring-up, exact rev42 firmware
> acquisition + validation) are present on `main`. FIFO0 RX is DMA Level-1
> proven; no RX frame completion yet. 5 GHz and interface bring-up are not
> enabled. The isolated `fw_validate_only` runtime PASS and the `ucode_test_only`
> (M3.4D2A) **BCM4352 runtime PASS** are merged (PR #2, `main` @ `85d3013`);
> D2A proves ucode upload + PSM start **only** — initvals, PHY, radio, channel,
> RX and TX remain unproven. M3.4D2B analysis merged (PR #5, `main` @
> `65d61ce`). The isolated `initvals_test_only` test is `IMPLEMENTED` /
> `STATIC TESTED` / `SIGNED` and **HARDWARE RUNTIME PROVEN on BCM4352**
> (candidate `f27286f`; PR #6) — it proves the common-initvals sequence
> **only**: bsinitvals, band init, AC PHY, radio, calibration, channel, RX and
> TX remain unproven. M3.4D3 (band-switch initvals + PHY boundary) analysis is
> merged. M3.4D3A0 (`dma_test_only=1`) is **`ISOLATED DMA LIFECYCLE TEST`**
> and is **`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / `HARDWARE RUNTIME
> PROVEN` on BCM4352** (candidate `4fa1b57`, runtime `7e68fe24`, PR #8). It is
> NOT a full vendor-prefix reproduction: it starts from the full D2B entry
> (proven D2A + exactly 610 common initvals + gate) and programs only the
> provenance-pinned D11/clock/IRQ-source prerequisites (host IRQ route off,
> `MACINTMASK=0`), 4 TX DMA channels (zero TX payload mappings) + FIFO0 RX
> (exactly 64 buffers), then proves the complete isolated lifecycle:
> allocate/map → program → hardware validation → verified stop → release
> (`PASS - bring-up + teardown proven`). Fail-closed quiesce: free only after
> every programmed engine's own verified normal stop; core-reset containment
> after a failed reset never authorizes a free; fatal state latched + module
> pinned + probe kept bound. Vendor-before-DMA stages not pinned (`sub_67efd`,
> NVRAM/BTC/rate/power SHM tail) are omitted and MUST be restored by D3A1
> integration before PHY bring-up. It STOPS before band
> init/bsinitvals/PHY/radio/channel/mac80211. Evidence:
> [`docs/m34d3a0_dma_test.md`](docs/m34d3a0_dma_test.md).
> M3.4D3A1 (vendor post-common / pre-PHY tail) is **`ANALYSIS COMPLETE`** /
> NOT IMPLEMENTED / NOT HARDWARE PROVEN; `D3A1 IMPLEMENTATION GO: YES` (analysis
> decision only). It re-proves `sub_67efd` for rev42, recovers the omitted
> SHM/NVRAM/BTC groups, establishes the vendor order **T1 → DMA → T2** (DMA is
> interleaved inside the tail), STOP before
> `sub_6656c`/bsinitvals/`wlc_phy_init`, and closes the value-source blockers
> (the `0x78c/0x78e/0x790` six bytes are the `cur_etheraddr` MAC; the SCR `0x24`
> write is skipped on the first init; `btc_params`/`btc_flags` absent ⇒ skip).
> Report:
> [`docs/m34d3a1_vendor_tail.md`](docs/m34d3a1_vendor_tail.md).
> See `docs/agent-state.md`. Agent rules: [`AGENTS.md`](AGENTS.md).
> MVP target: **BCM4352 `14e4:43b1`** (acphy, 2×2), kernel **7.x** (6.12 build
> compatibility pending, tracked in
> [#3](https://github.com/0x00000109/OpenBRCM/issues/3)).

## Why
Broadcom's SoftMAC driver ships a thin open OS wrapper plus a large proprietary
object; it cannot be built for modern kernels. OpenBRCM is a from-scratch
`mac80211` SoftMAC driver for the same silicon.

## Design philosophy
The driver is a **thin hardware-specific core**; everything generic is delegated
to the kernel, as is standard practice for modern wireless drivers:

| Handled by the kernel | OpenBRCM |
|---|---|
| `mac80211`: MLME, scan, auth/assoc, rate control (minstrel_ht), software crypto, block-ack, power save, TXQ scheduling, chanctx emulation | hardware bring-up, PHY, DMA/TX/RX, D11/ucode, HW key offload |
| `cfg80211`: regulatory, `iw`/nl80211 | advertise supported bands/channels/rates |
| `bcma`: bus/core windows | PMU/PLL/clock, OTP/SPROM specifics |
| `dma-mapping`, `request_threaded_irq`, `napi`, `pm_runtime`, `request_firmware` | ring/descriptor programming, ISR→DPC, ucode upload |

See [`docs/architecture.md`](docs/architecture.md).

## Build (out-of-tree)
```sh
make            # uses /lib/modules/$(uname -r)/build
make hosttest   # host unit tests for pure helpers (no kernel needed)
sudo make install
```
DKMS: see [`dkms.conf`](dkms.conf).

Requirements: `CONFIG_BCMA`, `CONFIG_MAC80211`, `CONFIG_FW_LOADER`.

## Layout
```
src/        driver sources (ob_*.c/.h)
  ob_regs.h ob_dma_regs.h ob_rates.h ob_iovar.h ob_wlc.h   # generated from the RE spec
tests/host/ host unit tests for pure helpers
tests/kunit/ KUnit suite (kernel)
docs/       architecture, provenance, firmware, milestones, agent state
scripts/    header regeneration / checks / documentation consistency
AGENTS.md   binding agent operating + documentation-governance rules
```

## Provenance & clean-room
Implementation is specification-driven. `src/ob_regs.h`, `ob_dma_regs.h`,
`ob_rates.h`, `ob_iovar.h`, `ob_wlc.h` are **generated** from the reverse-
engineering specification and must not be hand-edited. See
[`docs/provenance.md`](docs/provenance.md).

## Contributing
Maintainer: [`@0x00000109`](https://github.com/0x00000109). See [`AGENTS.md`](AGENTS.md)
for the agent/documentation rules and
[`docs/github-workflow.md`](docs/github-workflow.md) for the branch, commit,
pull-request, review and release process.

## License
GPL-2.0-only. See [`COPYING`](COPYING).

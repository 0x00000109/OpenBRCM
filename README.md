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
> (M3.4D2A) **BCM4352 runtime PASS** are merged (PR #2); D2A proves ucode
> upload + PSM start **only**. M3.4D2B (PR #6, `initvals_test_only=1`) proves
> the common-initvals sequence **only**. M3.4D3A0 (`dma_test_only=1`) is
> **`ISOLATED DMA LIFECYCLE TEST`** and is **`HARDWARE RUNTIME PROVEN` on
> BCM4352** (candidate `4fa1b57`, runtime `7e68fe24`, PR #8): it starts from the
> full D2B entry (proven D2A + exactly 610 common initvals + gate), programs
> only the provenance-pinned D11/clock/IRQ-source prerequisites (host IRQ route
> off, `MACINTMASK=0`), 4 TX DMA channels (zero TX payload mappings) + FIFO0 RX
> (exactly 64 buffers), then proves the complete isolated lifecycle:
> allocate/map → program → hardware validation → verified stop → release. It is
> NOT a full vendor-prefix reproduction. Evidence:
> [`docs/m34d3a0_dma_test.md`](docs/m34d3a0_dma_test.md).
> M3.4D3A1 (isolated vendor post-common / pre-PHY D11 tail test,
> `d11_tail_test_only=1`) is **`IMPLEMENTED` / `STATIC TESTED` / `SIGNED` /
> `HARDWARE RUNTIME PROVEN` on BCM4352** (candidate `42d74b8`, module
> `6ba2d853…`, PR #10). It reproduces the exact rev42 order
> (`sub_67efd → T1 → DMA → T2 → switch_macfreq`), reuses the proven D3A0 DMA
> lifecycle in its vendor position, validates deterministic postconditions,
> runs the verified DMA teardown, unloads cleanly and **STOPS before
> `sub_6656c`/bsinitvals/PHY**. Reports:
> [`docs/m34d3a1_vendor_tail_test.md`](docs/m34d3a1_vendor_tail_test.md)
> (implementation) and
> [`docs/m34d3a1_vendor_tail.md`](docs/m34d3a1_vendor_tail.md) (analysis).
> M3.4D3B (band init + `d11ac1bsinitvals42`) is **`ANALYSIS ONLY` / NOT
> IMPLEMENTED / NOT HARDWARE PROVEN**; design
> [`docs/m34d3b_band_init.md`](docs/m34d3b_band_init.md). Its
> `D3B IMPLEMENTATION GO: YES` — `VALUE FULLY PROVEN`: all five band-0 MHF
> values are now proven — `mhfs[0..4] = {0x0100, 0x0000, 0x0000, 0x0000,
> 0x0080}`. MHF3 (`antsel_type`) was closed by the 2026-09 read-only
> SPROM-evidence capture: the already-read 234-word rev11 image
> (`sprom_evidence_only=1`, zero extra MMIO; decoder
> `scripts/sprom11_decode.py`) decodes to `antsel_type = 0` -> **MHF3 =
> `0x0000`**. The capture is `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` /
> **`HARDWARE RUNTIME PROVEN`** (BCM4352, frozen candidate `739273c`,
> `openbrcm.ko` sha256 `538588e2…`); evidence
> [`docs/m34d3b/d3b_sprom_capture.json`](docs/m34d3b/d3b_sprom_capture.json).
> D3B analysis merged via PR #11, value closure via PR #13 (`main` @
> `5f6c3d2`). Band init, bsinitvals, AC PHY, radio, calibration, channel, RX
> and TX remain unproven.
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

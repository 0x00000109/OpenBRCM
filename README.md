# OpenBRCM

A **clean-room**, GPL-2.0 open-source Linux driver for Broadcom 43xx PCIe
SoftMAC Wi-Fi controllers, built on `mac80211` / `cfg80211`.

OpenBRCM is derived **solely** from the recovered OpenBRCM specification and
publicly documented silicon behaviour. It contains no code copied from the
proprietary `wlc_hybrid.o_shipped` blob or from another driver. See
[`docs/provenance.md`](docs/provenance.md).

> **Status: hardware bring-up, test-only.** The BCM4352 path reaches the D11
> rev42 ucode/PSM/common-initvals boundary, registers with mac80211, completes
> the recovered LPF/radio prefix, and applies recovered per-channel tuning rows.
> The explicit hardware entry point is `bringup_test_only=1`; a bare production
> load remains fail-closed. Calibration, a managed-station channel transition,
> RX/TX and association remain **NOT RUN**. BCM4360 is `UNSUPPORTED` and is
> present only as an evidence profile.

## Overview

The driver is a **thin hardware-specific core**; generic Wi-Fi behaviour is
delegated to the kernel:

| Handled by the kernel | OpenBRCM |
|---|---|
| `mac80211`: MLME, scan, auth/assoc, rate control, software crypto, block-ack, power save, TXQ scheduling | hardware bring-up, PHY/radio lifecycle, DMA/TX/RX, D11/ucode |
| `cfg80211`: regulatory, `iw`/nl80211 | advertise supported bands/channels and test-only monitor capability |
| `bcma`: bus/core windows | PMU/PLL/clock, OTP/SPROM, core reset and windows |
| `dma-mapping`, `request_threaded_irq`, `request_firmware` | ring/descriptor programming, ISR/DPC, rev42 firmware upload |

The current BCM4352 profile is PCI `14e4:43b1`, chip revision 3, D11 `0x812`
revision 42, PHY type `0x0b`, radio `0x2069` revision 4 (class 0). BCM4360 is
PCI `14e4:43a0`; it is not admitted.

## Verified Features

| Feature | Status | Evidence |
|---|---|---|
| D11 reset, rev42 ucode upload, PSM start, common initvals | `HARDWARE RUNTIME PROVEN` | isolated W2/W4 hardware runs; `docs/agent-state.md`, `docs/m34d3a0_dma_test.md` |
| DMA64 rings, IRQ registration, mac80211 registration | `HARDWARE RUNTIME PROVEN` | D3A0/D3A1 runs and the 2026-11-02 bring-up run |
| BCM4352 rev11 external SPROM validation | `CONFIRMED` | CRC `c0/c0` MATCH; `boardflags3=0x00000000`; `source=VALIDATED_REV11` |
| Channel/refclk tuning tables | `CONFIRMED` for recovered bytes and geometry | 14 arrays, 77 rows each; 20/40/80 MHz encodings; 40 MHz refclk selector in `src/bcm43xx_chan_tuning_data.c` |
| `chan_tuning` row write path | `HARDWARE RUNTIME PROVEN` for issued MMIO writes | monitor-only run: `0xe024` at 5180 MHz and `0x1001` at 2412 MHz |
| `phy_bw[6]` writes to `0x371..0x376` | `CONFIRMED` addresses and six-write count; functional `phy_bw` role `INFERRED` | `bcm43xx_apply_chan_tuning()`, `CHAN_TUNING_APPLIED` records |
| LPF / radio prefix | `HARDWARE RUNTIME PROVEN` to the calibration boundary | `lpf_status=0`, `boardflags3_valid=1`, `ACPHY_RADIO_COMPLETE`, stop at `CALIBRATION_INPUTS` |
| Calibration, managed-station channel transition, RX/TX | `NOT RUN` | explicit STOP boundary; no data path is admitted |

The six PHY writes are **issued-write accounting** from the driver. No readback
of `0x371..0x376` is performed, so this is not a silicon register-readback
proof.

## Hardware Validation

The 2026-11-02 BCM4352 run used the explicit test mode:

```text
insmod openbrcm.ko bringup_test_only=1 dyndbg=+p
```

After the validated rev11 board data and radio prefix, the run stopped at
`CALIBRATION_INPUTS` and applied the recovered base row. The test harness then
selected monitor mode because cfg80211 rejects per-device `iw ... set channel`
on a managed station:

```text
ip link set wlan0 down
iw dev wlan0 set type monitor
ip link set wlan0 up
iw dev wlan0 set channel 36 80MHz
iw dev wlan0 set channel 1
```

Observed:

```text
BRINGUP_TEST_GATE_BYPASS lpf_status=0 boardflags3_valid=1 boardflags3=0x00000000
ACPHY_RADIO_COMPLETE
CHAN_TUNING_APPLIED freq=5180 chanspec=0xe024 radio_writes=50 phy_writes=6 phy_bw_regs=0x371..0x376
CHAN_TUNING_APPLIED freq=2412 chanspec=0x1001 radio_writes=50 phy_writes=6 phy_bw_regs=0x371..0x376
```

The final dmesg contains three `CHAN_TUNING_APPLIED` records and 18
`chan_tuning phy[0x371..0x376]` lines (initial base row plus both explicit
requests). This validates the monitor-only physical row path, not a managed
station transition or production 5 GHz capability. Full evidence lives in the
companion analysis workspace (`wiki/Hardware-Bringup-Test.md` and
`audit/openbrcm_bringup_test_20261102T222955Z/`); it is not vendored here.

> **Safety:** an `insmod` is destructive on this bench. Read
> [`docs/bench-run-safety.md`](docs/bench-run-safety.md) before any hardware
> run. Use one insertion per boot, no retry after a fault, and no
> `setpci`/reset/rescan.

## Building

Requirements: a matching kernel build tree, `CONFIG_BCMA`,
`CONFIG_MAC80211`, `CONFIG_FW_LOADER`, and the four rev42 firmware files listed
by `modinfo openbrcm.ko`.

```sh
make -j"$(nproc)" modules
make hosttest          # pure host tests; no kernel module load
```

The build produces `openbrcm.ko`. Do not run the module on hardware unless you
are following the guarded, explicitly authorized bring-up procedure.

### KUnit

KUnit sources live in `tests/kunit/`. They are mirrors for a KUnit-enabled
kernel/UML build; this out-of-tree Makefile does not expose a general KUnit
runner (`make kunit` is not a recipe). The 2026-11-02 channel-row UML run
reported 5/5 PASS with filter `openbrcm_chan_tuning*`.

After integrating `tests/kunit/` and the relevant `src/` objects into a
KUnit-enabled 7.2.2 kernel tree, the low-level reproduction is:

```sh
make ARCH=um O=/tmp/kunit-build olddefconfig
make ARCH=um O=/tmp/kunit-build all --jobs="$(nproc)"
/tmp/kunit-build/linux \
  'kunit.filter_glob=openbrcm_chan_tuning*' \
  kunit.enable=1 mem=1G console=tty kunit_shutdown=halt
```

The exact historical log is in the companion analysis workspace:
`audit/openbrcm_w5c_m34d3p4_wp2_01/ops/CHAN_TUNING_KUNIT_STAGE2.log`.

## Documentation

| Document | Purpose |
|---|---|
| [`docs/architecture.md`](docs/architecture.md) | driver layers and ownership boundaries |
| [`docs/agent-state.md`](docs/agent-state.md) | current milestone, evidence and STOP boundary |
| [`docs/milestones.md`](docs/milestones.md) | milestone history |
| [`docs/provenance.md`](docs/provenance.md) | clean-room provenance rules |
| [`AGENTS.md`](AGENTS.md) | agent and documentation-governance rules |

The companion engineering wiki is maintained in the analysis checkout under
`wiki/` and is not vendored into this driver repository.

## Repository Layout

```text
src/                 driver sources and generated register headers
src/bcm43xx_chan_tuning*  recovered per-channel tuning rows and applier
tests/host/          host unit tests (no kernel needed)
tests/kunit/         KUnit mirrors for a KUnit-enabled kernel
docs/                architecture, provenance, safety, milestones, state
scripts/             documentation checks and build helpers
```

## Provenance and License

Implementation is specification-driven. Generated headers must not be
hand-edited. The pinned vendor identity and clean-room boundaries are in
[`docs/provenance.md`](docs/provenance.md) and
[`docs/re-binary-crossmap.md`](docs/re-binary-crossmap.md).

GPL-2.0-only. See [`COPYING`](COPYING).

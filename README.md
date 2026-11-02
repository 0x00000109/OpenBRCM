# OpenBRCM

OpenBRCM is an open-source, clean-room Linux driver designed to replace the
proprietary wl.ko for Broadcom BCM4352/BCM4360 PCIe Wi-Fi chips. Built for
modern kernels (6.x/7.x), it integrates directly with mac80211/cfg80211 and
includes verified chan_tuning calibration tables, SPROM rev11 decoding, and
reliable PHY 0x371–0x376 register programming.

> **Current phase:** Hardware Bring-Up & Channel Tuning Verified (Monitor Mode)
>
> **Scope:** test-only. The confirmed hardware path is the BCM4352 (`14e4:43b1`); production operation and a full data path are not claimed.

## Status

| Capability | Status |
|---|---|
| D11 core reset, rev42 ucode loading, DMA rings, IRQ 33 handling | **CONFIRMED** |
| SPROM rev11 CRC validation and `boardflags3` decoding (`boardflags3_valid=1`) | **CONFIRMED** |
| Exact six-write PHY register programming (`0x371`..`0x376`) for per-channel tuning across 14 static `chan_tuning` calibration tables | **CONFIRMED** |
| Hardware-verified physical channel switches on BCM4352 (`14e4:43b1`) in Monitor Mode: 2.4 GHz channel 1 and 5 GHz channel 36 (VHT80) | **CONFIRMED** |
| Full operational data path (TX/RX frames) and dynamic PHY calibrations (`CALIBRATION_INPUTS`) | **NOT RUN / IN PROGRESS** |

## Architecture

OpenBRCM is a thin, hardware-specific Linux module. It owns D11 initialization and rev42 firmware upload, DMA rings, IRQ 33 handling, SPROM rev11 decoding, PHY/radio programming, and per-channel tuning. Generic Wi-Fi behavior is delegated to the kernel through `mac80211` and `cfg80211`.

The confirmed hardware validation in this README is for the BCM4352; no equivalent BCM4360 hardware result is claimed here.

## Building

### Requirements

- A Linux 6.x or 7.x kernel build tree matching the target kernel.
- Kernel configuration with `CONFIG_BCMA`, `CONFIG_MAC80211`, `CONFIG_CFG80211`, and `CONFIG_FW_LOADER`.
- A C compiler and the standard kernel module build dependencies.

### Build the module

From the directory containing the OpenBRCM checkout:

```sh
make -C OpenBRCM modules
```

Or from the repository root:

```sh
make modules
```

To build against a specific kernel tree:

```sh
make -C OpenBRCM modules KDIR=/path/to/kernel/build
```

The build produces `openbrcm.ko`.

## KUnit Testing

KUnit sources live in `tests/kunit/`. The `bcm43xx_chan_tuning_kunit` test set reports **5/5 PASSED** in the UML run, covering 2.4 GHz and 5 GHz channels, VHT40/VHT80 operation, and 20/40 MHz refclk selection.

The out-of-tree module Makefile does not provide a `kunit` target. To run the tests, integrate `tests/kunit/` and the required `src/` objects into a KUnit-enabled Linux 7.2.2 tree, then build and boot UML from that kernel tree:

```sh
make ARCH=um O=/tmp/kunit-build olddefconfig
make ARCH=um O=/tmp/kunit-build all --jobs="$(nproc)"
/tmp/kunit-build/linux \
  'kunit.filter_glob=openbrcm_chan_tuning*' \
  kunit.enable=1 mem=1G console=tty kunit_shutdown=halt
```

## Hardware Validation

> **Test-only:** `bringup_test_only=1` is the explicit hardware gate. Use a dedicated test system and do not load the module in production.

Load the module with verbose dynamic debug enabled:

```sh
insmod openbrcm.ko bringup_test_only=1 dyndbg=+p
```

Switch the interface to monitor mode before changing channels. `cfg80211` rejects per-device channel changes on a managed interface with `-EBUSY`, so monitor mode is required for this validation:

```sh
ip link set wlan0 down
iw dev wlan0 set type monitor
ip link set wlan0 up

# 5 GHz, channel 36, VHT80
iw dev wlan0 set channel 36 80MHz

# 2.4 GHz, channel 1
iw dev wlan0 set channel 1
```

Replace `wlan0` with the actual interface name. On BCM4352, this sequence has been hardware-verified for channel 1 (2.4 GHz) and channel 36 (5 GHz, VHT80).

The driver stops at `CALIBRATION_INPUTS`; dynamic PHY calibration and TX/RX data-path operation remain outside the validated scope.

## Repository Layout

```text
src/           driver sources, D11/PHY/radio, and chan_tuning tables
tests/kunit/   KUnit suites for a KUnit-enabled kernel
tests/host/    host-side unit tests
.github/       CI workflows, including CodeQL
```

## License

GPL-2.0-only. See [COPYING](COPYING).

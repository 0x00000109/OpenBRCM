# Firmware

SoftMAC Broadcom 43xx chips require a D11 microcode + initialization values
("ucode"/initvals) at bring-up. OpenBRCM loads them through the standard kernel
firmware API (`request_firmware()`) in `src/ob_fw.c`.

OpenBRCM does **not** bundle firmware. The exact vendor-derived rev42 images —
slices of the same `wlc_hybrid.o_shipped` blob this project reverse-engineers
(provenance A) — are authoritative. They are **not** the upstream `b43` /
`brcmsmac` images; see [`firmware_reconciliation.md`](firmware_reconciliation.md).

## Required files

| file | role |
|---|---|
| `brcm/bcm4352-d11ucode42.bin` | rev42 D11 microcode (primary name) |
| `brcm/bcm43xx-ucode.fw` | documented fallback for the same ucode image |
| `brcm/bcm4352-d11ac1initvals42.bin` | common AC initvals |
| `brcm/bcm4352-d11ac1bsinitvals42.bin` | band-switch initvals |

`MODULE_FIRMWARE` declarations are in `src/ob_main.c`; the exact names, sizes and
FNV-1a-64 guards are in `src/ob_fw.h`:

- ucode: `43400` bytes / `10850` words
- common initvals: `4888` bytes (`610` records: `113` x 16-bit, `497` x 32-bit)
- band-switch initvals: `592` bytes (`73` records: `39` x 16-bit, `34` x 32-bit)

## Validation

`ob_fw_probe()` acquires each image, then strictly validates the exact size and
the FNV-1a-64 guard and structurally parses the **vendor 8-byte initvals
records** (`{u16 offset, u16 width, u32 value}`, terminator `0xffff`). This is
**not** the `b43` on-disk IV format.

- present but invalid/mismatched image -> probe fails (`-EINVAL` / `-EILSEQ`),
  so it can never be uploaded;
- absent image -> logged, and the other validated paths are undisturbed.

Host tests: `tests/host/ob_fw_test.c`. KUnit: `tests/kunit/ob_fw_kunit.c`.

## `fw_dryrun` is a log option only

`fw_dryrun=1` (default) logs only the first/last parsed records. It is a
**parser log option and does not isolate hardware bring-up**. Use the explicit
isolated modes (`fw_validate_only`, `ucode_test_only`) for hardware tests; see
[`../AGENTS.md`](../AGENTS.md) and [`agent-state.md`](agent-state.md).

## If firmware is missing

The driver logs the missing file and continues (missing firmware does not fail
probe by itself). Place the files under `/lib/firmware/brcm/`.

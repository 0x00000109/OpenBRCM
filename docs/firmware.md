# Firmware

SoftMAC Broadcom 43xx chips require a D11 microcode + initialization values
("ucode"/initvals) uploaded at bring-up. OpenBRCM loads these with the standard
kernel firmware API:

```c
request_firmware(&fw, "brcm/bcm43xx-0.fw", dev);
```

`MODULE_FIRMWARE` declarations are in `src/ob_main.c`. The files are provided by
**linux-firmware** (the same files used by other Broadcom SoftMAC drivers);
OpenBRCM does not bundle firmware.

## Required files
| file | purpose |
|---|---|
| `brcm/bcm43xx-0.fw` | D11 microcode + initvals (per-revision sections) |
| `brcm/bcm43xx_hdr-0.fw` | microcode header |

## Upload (RE Stage 6)
Ucode and initvals are written to the D11 instruction/data RAM through the core
register window. Exact addresses/ordering come from the reverse-engineering
specification; unresolved values are marked rather than guessed.

## If firmware is missing
The driver fails probe with `-ENOENT` and logs the missing file. Install
`linux-firmware` (or place the file under `/lib/firmware/brcm/`).

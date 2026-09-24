# M3.4D3A0 — isolated DMA lifecycle test: BCM4352 hardware proof

**Status: `HARDWARE RUNTIME PROVEN` on BCM4352 (BCM4352 `14e4:43b1`, chip
revision 3, D11 rev 42, kernel `7.0.0-34-generic`).**

Scope is unchanged and exact:

> **D3A0 = `ISOLATED DMA LIFECYCLE TEST`.** It is **NOT** a complete vendor
> post-common prefix. It proves the isolated DMA lifecycle only:
> allocate/map → program → hardware validation → verified stop → release.

Identity of the tested artifact:

| item | value |
| :--- | :--- |
| tested candidate | `4fa1b57ac39f8d31977bebc70d903b76bda42bfd` |
| runtime implementation commit | `7e68fe24fb1409c1f4d29ad8fb25ab642f7170e4` |
| signed module SHA256 | `0282d9b253b40ca13eba3420058b6314be629cdf50d540e549510f726cd6af08` |
| kernel | `7.0.0-34-generic` |
| module param | `dma_test_only=1` (only; no other isolated mode) |
| `insmod` rc | 0 |

Successful real attempt was identified by the marker in the log:

```
OPENBRCM M3.4D3A0
4fa1b57ac39f8d31977bebc70d903b76bda42bfd
DMA LIFECYCLE TEST - REAL ATTEMPT
```

## 1. Identity

- chip `0x4352`, chip rev 3; D11 core rev 42.

## 2. D2A evidence (shared hardware-proven prefix)

| item | value |
| :--- | :--- |
| firmware | `brcm/bcm43xx-ucode.fw` |
| size | 43400 bytes |
| words | 10850 |
| upload writes | 10850 |
| PSM | PASS |
| MI_MACSSPNDD | `0x00000001` |
| poll iterations | 11 |

## 3. D2B evidence (610 common initvals + postconditions)

| item | value |
| :--- | :--- |
| common initvals | 610 |
| width-16 | 113 |
| width-32 | 497 |
| M_FIFOSIZE0 | `0x01c4` |
| M_FIFOSIZE1 | `0x0000` |
| M_FIFOSIZE2 | `0x0000` |
| M_FIFOSIZE3 | `0x079e` |
| MACINTMASK | `0x00000000` |
| MACCONTROL | `0x04020402` |
| SHM[0x14] | `0x000000b4` |

D2B EXIT VERIFIED (live re-read matched the provenance-backed gate).

## 4. D3A0 IRQ-source-only evidence

| item | value |
| :--- | :--- |
| INTRCVLAZY | `0x01000000` |
| intctrlregs[0].intmask | `I_RI = 0x00010000` |
| MACINTMASK | `0` |
| host IRQ route | not enabled (no `bcma_host_pci_irq_ctl`, no `request_irq`) |

## 5. DMA addressing

- Linux DMA window: 32-bit.
- hardware `ADDRHIGH`: `0x80000000`.

## 6. TX0..TX3 evidence

| ch | block | ADDRLOW | ADDRHIGH | CONTROL | STATUS0 | state |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| TX0 | `0x200` | `0xfe89a000` | `0x80000000` | `0x00000000` -> `0x00000801` | `0x2000a000` | 2 |
| TX1 | `0x240` | `0xfe896000` | `0x80000000` | `0x00000000` -> `0x00000801` | `0x20006000` | 2 |
| TX2 | `0x280` | `0xfe8b6000` | `0x80000000` | `0x00000000` -> `0x00000801` | `0x20006000` | 2 |
| TX3 | `0x2c0` | `0xfe88c000` | `0x80000000` | `0x00000000` -> `0x00000801` | `0x2000c000` | 2 |

`CONTROL` shows XE (`0x1`) and PD (`0x800`) set by the RMW; the pre-write value
was 0 so no capability bits existed to preserve in this run. **No TX payload
buffers were posted** (TX payload mappings = 0).

## 7. RX evidence

| item | value |
| :--- | :--- |
| block | `0x220` |
| mapped buffers | 64 |
| ADDRLOW | `0xfe88a000` |
| ADDRHIGH | `0x80000000` |
| programmed PTR | `0x00000400` |
| CONTROL | `0x0000084d` |
| STATUS0 | `0x2000a000` |
| STATUS1 | `0x0000a000` |
| state | IDLE |

### PTR write/readback semantics (important)

- **programmed producer PTR = `0x400`** (the value written).
- The runtime log additionally reports `rb ptr_field=0x00000` (the raw PTR
  readback masked by `OB_D11_RS0_CD_MASK`). The hardware updates the current
  PTR as it consumes descriptors, so the **raw/current PTR readback is
  hardware-updated and is NOT validated by raw equality**.
- This must **not** be rewritten as a PTR programming failure, and the claim
  "readback PTR == `0x400`" must **not** be made.

## 8. Bring-up result

`bring-up validation PASS` with:

- `MACINTMASK = 0x00000000`
- `INTRCVLAZY = 0x01000000`
- no TX payload buffers posted
- RX mappings: 64

## 9. Teardown result

Exact runtime result:

```
quiesce begin
RX reset PASS
TX0 reset PASS
TX1 reset PASS
TX2 reset PASS
TX3 reset PASS
all DMA engines stopped
rings released
PASS - bring-up + teardown proven
```

The proven result is the **complete isolated lifecycle**:
allocate/map → program → hardware validation → verified stop → release.
It is not merely "DMA registers accepted".

## 10. Kernel safety review

Across the supplied full `dmesg`/journal evidence for the real attempt, no
D3A0-associated `BUG`, `Oops`, `WARNING`, DMA-API error, `Call Trace`, lockup,
reset timeout, `FATAL`, `reboot-required`, or PCIe AER fault was observed.

Note: an **earlier same-boot attempt** failed with an Unknown-symbol error
because `bcma`/`mac80211` dependencies were not yet loaded; it never reached
probe/MMIO/DMA. That earlier attempt is unrelated to the real D3A0 run and is
not the provenance of this milestone. The real attempt is the later
`OPENBRCM M3.4D3A0 … DMA LIFECYCLE TEST - REAL ATTEMPT` marker above (after the
dependencies were loaded and the module was re-inserted).

## 11. Scope boundary — what remains NOT proven

D3A0 proves the isolated DMA lifecycle only. The following remain unproven:

- complete vendor post-common tail
- `sub_67efd` integration
- runtime NVRAM/BTC/rate/power tail
- band init
- bsinitvals
- AC PHY init
- PHY tables
- radio
- synth/PLL
- channel
- calibration
- real RX completion/frame
- TX frame completion
- scan
- association

Normal-driver integration (D3A1) MUST restore the complete vendor ordering
before band/bsinitvals/PHY bring-up; D3A0's isolated ordering is not asserted to
be the final normal-driver order.

## 12. Operator safety rule

If D3A0 logs any `FATAL` / `reboot-required` / `quiesce NOT verified` /
`reset TIMEOUT` / `core containment` fallback: capture `dmesg` + journal and
**reboot**. Do not `rmmod`, unbind the BCMA device, bind another driver, repeat
`dma_test_only`, or run normal OpenBRCM. The module pin blocks `rmmod` but not a
manual sysfs unbind/rebind.

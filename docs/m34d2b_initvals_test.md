# M3.4D2B — isolated rev42 common-initvals test (implementation)

**Status: `IMPLEMENTED` — `STATIC TESTED` — `SIGNED` — `HARDWARE RUNTIME PROVEN`
on BCM4352 (candidate `f27286f`).**

This is the implementation of the D2B test designed and cleared in
`docs/m34d2b_common_initvals.md` (§F13). It passed on real BCM4352 hardware; the
evidence is in §0 below. This proves the **common-initvals sequence only** —
bsinitvals, band init, AC PHY, radio, calibration, channel, RX and TX remain
**unproven**.

- Mode: module param `initvals_test_only=1` (default 0).
- Reuses the hardware-proven D2A core byte-for-byte.
- Applies exactly the 610 `d11ac1initvals42` common-initvals records, in strict
  order, excluding the terminator.
- Reads only the provenance-backed postconditions, then STOPS.
- The test is **ONE SHOT**: do not repeat; recover logs and analyze on failure.

## 0. Runtime evidence (BCM4352, chip rev 3, D11 rev 42)

- Tested candidate `f27286f6f7e817a58fd1ae6da311cc4281a10a0b`; signed module
  SHA256 `1258290cb491ea551a9fb4c4e820ecf3450ae7c23957b41e5eeada14f1d98290`;
  base `main` @ `65d61ce`; PR #6.
- `insmod rc=0`; `initvals-test: BEGIN`; ucode `brcm/bcm43xx-ucode.fw`
  `size=43400`, `words=10850`, upload `writes=10850`.
- D11 prep `host_is_pcie2=1`, `clkctlst=070b0042`, `HAVEHT=1`, `core_enabled=1`;
  `MACCONTROL 00000000 -> 04000404`; PSM start `04020402`.
- PSM poll `delay=10us max_iter=100000 max_total_us=1000000` PASS
  `iterations=11` `MACINTSTATUS=0x00000001`.
- Common initvals `records=610`, `total=610`, `w16=113`, `w32=497`.
- Postconditions `M_FIFOSIZE0=01c4`, `M_FIFOSIZE1=0000`, `M_FIFOSIZE2=0000`,
  `M_FIFOSIZE3=079e`, `MACINTMASK=00000000`, `MACCONTROL=04020402`,
  `SHM[0014]=000000b4`.
- Final `PASS - stopped before bsinitvals/PHY/radio/channel/DMA`. No
  timeout/BUG/Oops/lockup/reset; no RX/TX DMA; no IRQ bring-up; no PHY/radio/
  channel initialization.
- **Negative boundary (NOT proven):** `d11ac1bsinitvals42`, band
  initialization, AC PHY initialization, radio initialization, calibration,
  channel selection, RX frame reception, TX.

## 1. Call graph (initvals_test_only)

```
ob_probe (src/ob_core.c)
├─ ob_isolated_mode_select/ob_isolated_mode_conflict      [pure policy]
└─ if mode == OB_ISOLATED_INITVALS_TEST
   ├─ hw->initvals_test_only = true
   └─ ob_initvals_test (src/ob_initvals.c)
      ├─ ob_ucode_run_d2a (src/ob_ucode.c)                 [shared D2A core]
      │  ├─ ob_fw_request_ucode (src/ob_fw.c)              -> request_firmware/release_firmware
      │  ├─ ob_ucode_prepare
      │  │  └─ bcma_host_pci_up; bcma_core_is_enabled/enable;
      │  │     bcma_core_set_clockmode(FAST); bcma_read32(CLKCTLST)
      │  ├─ ob_ucode_mctrl_update                          -> bcma_read32/bcma_write32(MACCONTROL)
      │  ├─ bcma_write32(OBJADDR); loop bcma_write32(OBJDATA) x10850
      │  ├─ bcma_write32(MACINTSTATUS); ob_ucode_mctrl_update(PSM)
      │  └─ bcma_read32(MACINTSTATUS) bounded poll + udelay
      ├─ ob_fw_request_initvals (src/ob_fw.c)              -> request_firmware/release_firmware
      ├─ ob_initvals_plan_from_table / ob_initvals_plan_ok [pure plan]
      ├─ loop: ob_fw_iv_at [pure] + ob_initvals_write      -> bcma_write16 x113 / bcma_write32 x497
      ├─ ob_ucode_read_shm16 (x6)                          -> bcma_write32(OBJADDR)+bcma_read32+bcma_read16(OBJDATA)
      ├─ bcma_read32(MACINTMASK); bcma_read32(MACCONTROL)
      └─ ob_initvals_post_ok                               [pure postcondition]
```

The D2A path (`ob_ucode_test`) shares the same `ob_ucode_run_d2a` and adds only
the read-only SHM diagnostic. There is no second copy of the register sequence.

### Unreachable from initvals_test_only

`ob_initvals_test` and `ob_ucode_run_d2a` never call, directly or indirectly:
`ob_si_probe`, `ob_dma_init`, `ob_irq_init`, `ob_rx_init`,
`ob_mac80211_register`, `request_irq`, `bcma_host_pci_irq_ctl(..., true)`,
`sub_6656c`, the band-switch/bsinitvals applier, or `wlc_phy_init`. The probe
dispatches to exactly one mode and the normal bring-up block is not reachable
from the initvals branch (it is a guard `if (mode == ...) { ...; return 0; }`).

## 2. New MMIO write classes introduced by D2B

All writes are through `bcma_write16`/`bcma_write32` on the D11 core; targets
are `D11 base + offset` from the vendor table. The table's 610 records are
classified (see `docs/m34d2b/initvals_classification.md`) as:

| class | count | notes |
|---|---|---|
| `bcma_write16` (width 2) | 113 | exact vendor `osl_writew` equivalent |
| `bcma_write32` (width 4) | 497 | exact vendor `osl_writel` equivalent |
| of which `OBJADDR (0x160)` selectors | 76 | indirect window select |
| of which `OBJDATA (0x164)` data | 340 | SHM (56 auto-inc + SCR 20) indirect writes |
| other direct D11 offsets | 194 | IHR/PSM/TXE/TSF/IFS/MACINT/template |

No new register class is invented: D2B writes only the recovered table. The
postcondition phase adds only **reads** (SHM window, `MACINTMASK`,
`MACCONTROL`).

## 3. Record iterator

- Format: 8-byte LE `{u16 offset, u16 width, u32 value}`, terminated by
  `offset == 0xffff` (vendor applier `sub_60f67`).
- `ob_initvals_plan_from_table()` parses the table and counts the data records;
  the terminator is never counted.
- The apply loop iterates `i = 0 .. plan.records-1` (exactly `0..609`), decodes
  each record with `ob_fw_iv_at()`, and writes it. A defensive check rejects any
  record whose offset is `0xffff` or whose width is not 2 or 4, so the
  terminator can never be written.
- No coalescing, no reordering, no de-duplication, no byte swap, no value
  normalization.

## 4. Write-count invariants

```
total_records_written == 610
writes16              == 113
writes32              == 497
```

`ob_initvals_counts_ok()` is checked after the loop. On mismatch the code logs
`total/w16/w32` against expected and returns `-EIO`; it never continues to the
postcondition PASS. There is no automatic retry.

## 5. Postcondition read/validation

| observable | access | expected |
|---|---|---|
| `M_FIFOSIZE0` | `ob_ucode_read_shm16(0x98)` | `0x01c4` |
| `M_FIFOSIZE1` | `ob_ucode_read_shm16(0x9a)` | `0x0000` |
| `M_FIFOSIZE2` | `ob_ucode_read_shm16(0x9c)` | `0x0000` |
| `M_FIFOSIZE3` | `ob_ucode_read_shm16(0x9e)` | `0x079e` |
| `MACINTMASK` | `bcma_read32(0x12c)` | `0x00000000` |
| `MACCONTROL` | `bcma_read32(0x120)` | `0x04020402` |
| SHM `0x0014` | `read16(0x14) \| read16(0x16)<<16` | `0x000000b4` |

`ob_initvals_post_ok()` requires all seven. On mismatch it logs exact
got/expected values and returns `-EIO`; no further bring-up happens. No equality
check outside this set is invented.

## 6. Mode-conflict logic

`ob_isolated_mode_select(fw_validate_only, ucode_test_only, initvals_test_only)`
is a pure function:

| combination | result |
|---|---|
| all zero | `OB_ISOLATED_NONE` (normal path) |
| `fw_validate_only` only | `OB_ISOLATED_FW_VALIDATE` |
| `ucode_test_only` only | `OB_ISOLATED_UCODE_TEST` |
| `initvals_test_only` only | `OB_ISOLATED_INITVALS_TEST` |
| any two or all three | `OB_ISOLATED_CONFLICT` -> probe returns `-EINVAL` before any hardware access |

`ob_probe` rejects a conflict immediately after the identity check, before
`devm_kzalloc`/any register access.

## 7. D2A regression proof

- D2A and D2B call the **same** `ob_ucode_run_d2a`; there is one copy of the
  register sequence.
- `ob_isolated_mode_applies_initvals()` is true only for
  `OB_ISOLATED_INITVALS_TEST`; `ob_ucode_test` never calls the initvals applier.
- Host test `tests/host/ob_ucode_test.c` and the initvals test assert that
  `OB_ISOLATED_UCODE_TEST` does **not** apply the common table.
- The D2A hardware-proven tested candidate (`7265f9d`) and implementation
  (`47e0883`) remain the reference; `ob_ucode_test`'s observable register
  sequence and boundary are unchanged.

## 8. Safety boundaries and failure/residual-state matrix

| # | case | behavior |
|---|---|---|
| A | failure before table application (request/prep/maccontrol/upload/PSM) | D2A core logs `FAIL stage=...`, returns the error; no initvals write; no cleanup register writes |
| B | malformed table (bad size/hash/width/truncated/missing terminator/data after terminator) | `ob_fw_request_initvals` / `ob_initvals_plan_from_table` fail; return `-EINVAL`/`-EILSEQ`/`-ENODATA`/`-ENOENT`; no writes |
| C | failure during table iteration | defensive decode/width/terminator check returns `-EINVAL`; writes stop at the failing index |
| D | write-count mismatch | `ob_initvals_counts_ok` fails -> `-EIO`; no postcondition PASS |
| E | postcondition mismatch | exact got/expected logged -> `-EIO`; STOP |
| F | complete PASS | all counts and postconditions hold; log `PASS - stopped before bsinitvals/PHY/radio/channel/DMA` |

Policy: no retry, no hidden reset, no automatic second upload. After any
failure the chip may hold `PSM_RUN=1`, `EN_MAC=0`, core enabled and a
partially/fully applied initvals table. Therefore a second run requires
analysis, or a reboot/reset to a known state; it must not be re-run blindly.

## 9. Dedicated remove guard

`ob_remove` returns early for `hw->initvals_test_only` and performs **no**
mac80211/RX/IRQ/DMA teardown and **no** cleanup register writes (same policy as
`ucode_test_only`; no recovery write is provenance-backed for this partial
state). Host tests assert every isolated mode skips teardown.

## 10. Verification performed (static only)

```
make clean && make          -> PASS (openbrcm.ko, no warnings in new files)
make hosttest               -> PASS (math/caps/dma/irq/rx/fw/ucode/initvals)
scripts/docs-check.sh       -> PASS
checkpatch (per-file)       -> 0 errors, 0 warnings
scripts/sign.sh             -> Signed, signer "Broadcom Driver MOK", sha256
KUnit                       -> NOT EXECUTED (no in-tree KUnit runner)
```

`modinfo` parameters (relevant):
```
parm: fw_validate_only:validate rev42 firmware only; skip all hardware bring-up (default: 0) (bool)
parm: ucode_test_only:D11 rev42 ucode upload + PSM start only; stops before initvals/PHY/DMA (default: 0) (bool)
parm: initvals_test_only:D11 rev42 common initvals + PSM only; stops before bsinitvals/PHY/DMA (default: 0) (bool)
```

## 11. Manual hardware procedure (EXECUTED ONCE — PASS; DO NOT REPEAT)

This exact procedure was executed once under explicit human approval on the
frozen candidate and produced the §0 evidence. It is **ONE SHOT**: do not repeat
without a reboot/reset to a known state (see §8), and do not combine
`initvals_test_only` with any other isolated mode.

```
# preconditions: openbrcm.ko signed (Broadcom Driver MOK) and installed
# 1. run the isolated test
sudo insmod openbrcm.ko initvals_test_only=1
# 2. inspect the initvals-test: log lines and expect:
#      initvals-test: common initvals complete total=610 w16=113 w32=497
#      initvals-test: M_FIFOSIZE0=01c4 ... M_FIFOSIZE3=079e
#      initvals-test: MACINTMASK=00000000
#      initvals-test: MACCONTROL=04020402
#      initvals-test: SHM[0014]=000000b4
#      initvals-test: PASS - stopped before bsinitvals/PHY/radio/channel/DMA
sudo dmesg | grep 'initvals-test'
# 3. unload (no teardown; hardware left as-is)
sudo rmmod openbrcm
```

Do not repeat the run without a reboot/reset to a known state (see §8). Do not
combine `initvals_test_only` with any other isolated mode (`-EINVAL`).

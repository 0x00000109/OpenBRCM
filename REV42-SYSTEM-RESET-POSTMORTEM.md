# REV42-SYSTEM-RESET-POSTMORTEM — abrupt platform reset after the BCM4352 window experiments

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


**Scope: read-only post-mortem of the boot that reset. No BCM4352 MMIO, no PCI
configuration writes, no OpenBRCM load. No production code modified.**

- Current (post-reset) boot: `6191aa86c8b145eba5b6fa83880c1eed`, started
  `2026-09-26 20:43:38 CEST` (uptime 3 min at analysis time).
- Machine: `vinga`, kernel `7.0.0-34-generic`, Secure Boot enabled, AMD host,
  AMD dGPU (`amdgpu 0000:30:00.0`).

## 1. Previous boot identification

- Boot `-1`: **`57af2c61186c4d138e3fb9678ad462a9`**, `Sat 2026-09-26 19:50:50`
  → `Sat 2026-09-26 20:43:00 CEST`.
- `last -x`: `kartasho tty7 :0  Sat Sep 26 19:51 - crash (00:52)` — the session
  is recorded as **crash**, and the boot that started 19:50 ends at the next
  boot start (20:43:38).

## 2. Approximate reset time

**≈ 20:43:00 CEST.** The persistent journal for boot `-1` ends at
`20:43:00` (`workqueue: psi_avgs_work hogged CPU …`); the next boot starts
`20:43:38`. Reset-to-reboot window ≈ 38 s.

## 3. Orderly or abrupt

**Abrupt.** There is **no** systemd shutdown/reboot sequence (no
`Stopping …`, no `Reached target shutdown.target`, no `systemd-shutdown`) at
the end of boot `-1` — the last such messages are from `19:51` (the *start* of
that boot's session churn), not the end. The journal simply stops mid-stream at
`20:43:00`. `last -x` marks the session `crash`, not a clean logout.

## 4. Last persistent kernel messages (boot -1)

```
20:41:29 workqueue: dm_handle_vmin_vmax_update [amdgpu] hogged CPU … 11 times
20:41:30 workqueue: pci_pme_list_scan hogged CPU for >10000us 7 times
20:41:35 workqueue: dm_handle_vmin_vmax_update [amdgpu] hogged CPU … 19 times
20:41:35 workqueue: vmstat_update hogged CPU … 4 times
20:41:57 amdgpu 0000:30:00.0: Fence fallback timer expired on ring comp_1.1.0
20:42:28 workqueue: pci_pme_list_scan hogged CPU for >10000us 19 times
20:43:00 workqueue: psi_avgs_work hogged CPU for >10000us 4 times   <-- last line
```

These are **CPU-starvation / workqueue-stall** warnings, not a panic. Note
`pci_pme_list_scan` repeatedly hogging CPU — the kernel's PCIe PME poll was
spending long stretches inside device access. There is **no** `panic`, `Oops`,
`BUG:`, `Call Trace`, `kernel NULL pointer`, `MCE`, `soft lockup`, `hard
lockup`, `NMI`, or `watchdog` line anywhere in the boot-`-1` kernel log.

## 5. Last OpenBRCM diagnostic checkpoint

20:23:08 — the isolated `rev42_sb_reset_only` probe's R0 checkpoint:

```
20:23:08 rev42-sb: R0 raw 0x0F98=0xffffffff 0x0F8=0xffffffff 0x0F90=0xffffffff 0x0F9C=0xffffffff
20:23:08 rev42-sb: R0 0x0F98 bit0=1 bit1=1 bit2=1 bit16=1 wrapper_reachable=0
20:23:08 rev42-sb: WRAPPER UNREACHABLE … STOPPING before writes.
20:23:08 rev42-sb: FAIL ret=-5 (no cleanup writes)
20:23:08 probe with driver openbrcm failed with error -5
```

YAML sequence IDs reached: 0 (envelope), 1 (host up), then read-only R0 checks
of seq 7/12/17/16. **No writes; no writes were attempted.** The probe
soft-stopped and the module was `rmmod`'d cleanly.

## 6. Last confirmed hardware operation

- **Last OpenBRCM MMIO:** `20:23:08` — reads of `0x0F98/0x0F8/0x0F90/0x0F9C`
  (all `0xffffffff`). Read-only.
- **Last OpenBRCM MMIO write:** `19:52:16` — `bcma_core_enable` /
  `bcma_core_set_clockmode` in the `rev42_attach_probe_only` run (≈ **51 min**
  before the reset).
- **Last access of any kind to the BCM4352 before the reset: a PCI
  configuration-space READ** —
  `sudo xxd -l 64 /sys/bus/pci/devices/0000:21:00.0/config` at `20:42:21`
  (preceded by `setpci -s 21:00.0 0x00.l`/`0x08.b` reads at `20:42:09`).
  The reset followed ≈ **39 s** later. These were **reads only**; no
  configuration write was issued.

**Buffering caveat:** journald persists kernel messages (`/var/log/journal`,
517 MB), and the last line is at `20:43:00`. An abrupt sync-flood reset can
still lose the final ms of in-flight printk/logging, so operations after the
last persisted line cannot be excluded — but there is no evidence of any.

## 7. PCIe / AER evidence

- **Boot `-1`: none logged.** No `pcieport … AER`, `correctable`,
  `uncorrectable`, `Completion Timeout`, `Fatal`, or `data link` lines.
- **Current boot's platform reset-reason register** (this is the decisive
  record):

```
[0.592327] x86/amd: Previous system reset reason [0x08000800]:
           an uncorrected error caused a data fabric sync flood event
```

  This is the AMD SoC reporting that its **data fabric received an uncorrected
  error and flooded**, i.e. a platform-level escalation that took the machine
  down before an AER handler could log. For contrast, boot `-1` reported its own
  predecessor as `[0x00080800]: software wrote 0x6 to reset control register
  0xCF9` (an orderly software reboot) — so `0x08000800` here is specific to this
  event.

## 8. Kernel panic / oops evidence

**None.** No `panic`, `Oops`, `BUG`, `Call Trace`, `general protection`,
`NULL pointer dereference`, or `kernel BUG` anywhere in boot `-1`.

## 9. Watchdog / lockup evidence

**None.** No `soft lockup`, `hard lockup`, `NMI watchdog: … detected`, `rcu …`,
or hung-task message. The only watchdog-adjacent line is benign:
`19:52:42 clocksource: Long readout interval, skipping watchdog check`
(a skipped check, not a fired watchdog).

## 10. pstore / ramoops

- `efi_pstore` **is** registered (`pstore: Registered efi_pstore as persistent
  store backend`), and the BIOS exposes EFI runtime/efivars.
- `/sys/fs/pstore/` is **EMPTY** (confirmed with root).
- `/var/lib/systemd/pstore/` contains **no record newer than 2026-09-24**;
  `systemd-pstore.service` was skipped at this boot
  (`ConditionDirectoryNotEmpty=/sys/fs/pstore` unmet).
- No `ramoops` module, no `pstore` cmdline args. No kdump
  (`/proc/vmcore` absent, `kdump-tools` inactive).

**Verdict: `RESET CAUSE UNKNOWN — NO PERSISTENT CRASH RECORD`** (for a *kernel*
crash record). The **platform** reset-reason register still gives the immediate
cause (§7).

## 11. Cause confidence

| Layer | Finding | Confidence |
|---|---|---|
| Immediate platform cause | AMD data-fabric **sync flood from an uncorrected error** (`0x08000800`) | **CONFIRMED** (reset-reason register) |
| Category | Not a kernel panic/oops, not a watchdog/CPU lockup, not a software reboot | **STRONGLY_SUPPORTED** (no such records; reason register says uncorrected error) |
| Source device | The **wedged BCM4352** (config space returned all-ones, `Unknown header type 7f`; `pci_pme_list_scan` thrashing; config reads at 20:42) | **STRONGLY_SUPPORTED** |
| Exact triggering operation | Unknown; no AER record; last BCM4352 access was a config **read** 39 s earlier | **UNKNOWN** |

**Single strongest explanation:** the earlier isolated BCM4352 experiments left
the device wedged (its PCIe config space and BAR windows returned all-ones /
unknown-header-type). Continued PCIe accesses to that wedged device — the
kernel's `pci_pme_list_scan` thrash plus read-only config-space accesses at
`20:42` — produced an **uncorrected error** that the AMD SoC escalated to a
**data-fabric sync flood**, abruptly resetting the platform at ≈`20:43:00`.
This is the same failure signature (`0x08000800`) as the earlier M3.4D3B crash,
so it is a recurring platform failure mode around the wedged BCM4352.

## 12. Is another hardware run currently safe?

**NO.** The device is now freshly re-enumerated (`pci 0000:21:00.0 [14e4:43b1]
type 00 … BAR 0 32K, BAR 2 2M` at boot), but (a) there is no persistent crash
capture, so a repeat would again be undiagnosable; (b) the root cause (wrong
window causing an uncorrected error) is not yet resolved; (c) the previous run
was not a clean cold boot. Another run should wait until the instrumentation
below is in place and the BAR0/AI-wrapper access path is statically resolved.

## 13. Instrumentation required before another run

1. **Verify pstore actually captures** — confirm `/sys/fs/pstore` receives an
   entry on a controlled fault; if it does not, enable **ramoops** (`ramoops`
   module + `memmap`/`pstore` cmdline) or a serial/netconsole path. EFI pstore
   is registered but produced nothing.
2. **Netconsole or serial console** to a second host, so a sync flood leaves an
   external log tail independent of the reset machine.
3. **Synchronous pre-op markers**: emit a marker to the persistent journal AND
   to netconsole *before* each dangerous operation, and a readback marker
   *after* — never rely on buffered dmesg.
4. **One transition per run**, with an explicit STOP and no further MMIO once a
   checkpoint is ambiguous.
5. **Never read the PCI config space of a device whose config already returns
   all-ones / unknown-header-type** — treat that as a dead endpoint and stop
   all access; the earlier config reads are the last-known BCM4352 accesses
   before the reset.
6. **Cold-boot discipline**: validate on a fresh boot only, and quiesce PCIe
   PME scanning to the device if possible.
7. **Static first**: finish the AI-wrapper (`ai_iscoreup` / `wrapper+0x408`)
   reconstruction and the exact BAR0 window programming before any further
   BCM4352 access.

## Provenance

```
journalctl --list-boots                -> boot -1 = 57af2c61… 19:50:50–20:43:00
journalctl -b -1 -k | tail             -> workqueue-hog lines, ends 20:43:00
journalctl -b -1 -p err                -> only openbrcm soft-stop lines
journalctl -b -1 | tail                -> sudo xxd config 20:42:21, then stalls
last -x                                -> ":0 19:51 - crash (00:52)"
dmesg (boot 0)                         -> "Previous system reset reason [0x08000800]: an uncorrected error caused a data fabric sync flood event"
/sys/fs/pstore, /var/lib/systemd/pstore, /proc/vmcore -> empty / none
```

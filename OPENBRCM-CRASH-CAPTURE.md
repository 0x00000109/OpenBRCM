# OPENBRCM-CRASH-CAPTURE — host instrumentation for BCM4352 experiments

**Scope: host-side instrumentation audit only. No BCM4352 access of any kind
(no OpenBRCM load, no bind, no MMIO, no PCI config read/write, no setpci/lspci,
no BAR windows, no D11/BCMA registers). No production OpenBRCM code changed.**

Host: `vinga`, `7.0.0-34-generic`, AMD host + `amdgpu` dGPU, Secure Boot on.

## 1. Previous reset context

The prior BCM4352 boot (`57af2c61…`, 2026-09-26 19:50:50→20:43:00) ended in an
**abrupt platform reset**: no panic/oops/BUG, no AER line, no pstore record.
The current boot's reset-reason register reports
`[0x08000800]: an uncorrected error caused a data fabric sync flood event`
(the same signature as the earlier M3.4D3B crash). The BCM4352 was wedged
(config space all-ones / unknown-header-type) and later PCI/config traffic is
suspected to have contributed. Full analysis:
`REV42-SYSTEM-RESET-POSTMORTEM.md`.

Goal of this task: make the **next** failure leave evidence that survives a
total board reset.

## 2. Current logging capabilities

| Mechanism | Present | Enabled | Persistent across hard reset? | Useful here? | Notes |
|---|---|---|---|---|---|
| systemd-journald | yes | yes | journal files yes; **unflushed tail lost** | yes | `/var/log/journal` present, `Storage=auto`→persistent; `SyncIntervalSec=5m` too coarse |
| previous-boot kernel log | yes | yes | yes (on disk) | yes | `journalctl -b -1 -k` works |
| pstore (`efi_pstore`) | yes | yes | **only on clean EFI save; did NOT capture sync flood** | marginal | `/sys/fs/pstore` empty; `CONFIG_PSTORE_CONSOLE`/`PMSG` **off** |
| ramoops (`CONFIG_PSTORE_RAM=m`) | module present | **no** | maybe (warm reset) | maybe | no reserved region; safe address not proven |
| netconsole (`CONFIG_NETCONSOLE=m`) | yes | no | yes (off-box, survives anything on the wire) | **best** | wired `enp42s0` 192.168.1.18/24 available |
| serial console | ttyS0 present | no | yes (if cabled) | good if wired | no `console=` on cmdline; cable unknown |
| kdump/kexec | config yes, userspace **no** | no | helps panic/oops; **not** sync flood | low | no `crashkernel=`, no tools |
| NMI watchdog | yes | yes | detection only | yes | `nmi_watchdog=1` |
| USB serial | n/a | — | **not equivalent** for hard lockup | no | excluded per task |

## 3. Persistent journal

- `/etc/systemd/journald.conf` contains only `[Journal]` (all defaults); no
  `/etc/systemd/journald.conf.d/`.
- `/var/log/journal` exists → with the default `Storage=auto`, logging **is
  persistent**. `journalctl -b -1 -k` works (verified).
- Weakness: default `SyncIntervalSec=5m` means up to ~5 minutes of entries can
  be lost on a hard reset. A drop-in reducing this is recommended (see §13).

## 4. pstore status

- `pstore` is mounted at `/sys/fs/pstore` (rw).
- Active backend: **`efi_pstore`** (`pstore: Registered efi_pstore as persistent
  store backend`). `efivars` and EFI runtime are available (`efibootmgr` works).
- **No records** in `/sys/fs/pstore`; `/var/lib/systemd/pstore` newest is
  `1790207315` (2026-09-24), i.e. nothing from the reset.
- Kernel config: `CONFIG_PSTORE=y`, `CONFIG_EFI_VARS_PSTORE=m`,
  `CONFIG_PSTORE_RAM=m`; **`CONFIG_PSTORE_CONSOLE` and `CONFIG_PSTORE_PMSG`
  are NOT set** (so console/dmesg capture and userspace pmsg into pstore are
  unavailable).
- Limitation: EFI pstore relies on the firmware persisting a variable at
  crash time; during an **uncorrected-error sync flood** the kernel never gets
  to save, and the firmware did not persist a record. EFI pstore is therefore
  **not reliable for this failure class**.

## 5. ramoops feasibility

- `CONFIG_PSTORE_RAM=m` and `/lib/modules/$(uname -r)/kernel/fs/pstore/ramoops.ko.zst`
  exist; the module is **not** loaded.
- **No reserved region is configured** (no `ramoops.*` on the cmdline, no
  reserved hole proven safe). `/proc/iomem` shows 16 `Reserved` lines, but none
  can be proven safe to reclaim for ramoops without a full e820/ACPI memory-map
  analysis.
- **Conclusion: ramoops is mechanically feasible but NOT safely configurable
  from the available information. Do not invent a physical address.** A correct
  configuration needs `memmap=<size>$<addr>` (or an ACPI-reserved region) plus
  `ramoops.mem_address=… ramoops.mem_size=… ramoops.ecc=1`, and a reboot.
- Caveat: even when configured, ramoops may not survive a **data-fabric sync
  flood** if DRAM is re-initialised on reset.

## 6. netconsole

- `CONFIG_NETCONSOLE=m`, module available, not loaded.
- Transport: **wired `enp42s0`** (UP, `192.168.1.18/24`, gw `192.168.1.1`).
  `tap-tor` (`10.10.10.1/24`, DOWN) is a VPN tap — **not** suitable.
- The BCM4352 Wi-Fi device is **never** used as transport.
- **Recommended as the primary crash tail.** It survives any local reset because
  the log leaves the box before the reset. Needs a receiver host on
  `192.168.1.0/24`; provide its IP+MAC (placeholder below). Runtime configurable
  via `configfs` (no reboot).

## 7. serial console

- Real 16550A UART `ttyS0` at `0x3f8 irq4` exists; `ttyS1` also enumerated.
- **No `console=` on the kernel cmdline** → serial console is not activated.
- Whether a physical COM header/cable is connected is **unknown**. If wired,
  serial is the most robust capture for hard lockups. Recommended to verify
  physically; USB-serial is explicitly not equivalent.

## 8. kdump

- `CONFIG_KEXEC=y`, `CONFIG_KEXEC_FILE=y`, `CONFIG_CRASH_DUMP=y`, but **no
  `crashkernel=` reservation**, no `kexec-tools`/`kdump-tools`/`makedumpfile`.
- kdump would help for a normal **panic/oops** (capture kernel boots and saves a
  vmcore) but **not** for an immediate SoC/data-fabric reset (the board dies
  before the capture kernel runs). Given the observed failure class, **kdump is
  low value here** and should not be installed/enabled now.

## 9. panic / lockup settings

Current: `kernel.panic=0`, `panic_on_oops=0`, `panic_on_warn=0`,
`hardlockup_panic=0`, `softlockup_panic=0`, `nmi_watchdog=1`, `sysrq=176`,
`panic_print=0`.

Tradeoff for this project: we want **evidence**, not a reset on every warning.
- Keep `panic_on_oops=0` and `panic_on_warn=0` (let oops print and the box limp;
  netconsole captures it).
- Keep `nmi_watchdog=1` (detects hard lockups and logs them → netconsole).
- **Do not** set `hardlockup_panic=1`/`softlockup_panic=1` by default: the
  observed failure is a platform reset, not a lockup, and forcing a panic could
  mask the real cause. (Optional: enabling them only makes sense once netconsole
  is verified, so a lockup becomes a capturable panic.)
- `kernel.panic=0` means a panic will **hang** rather than reboot; that is
  intentional for live inspection, but it relies on netconsole/serial to have
  captured the log. If unattended runs are wanted, `kernel.panic=30` (reboot
  after 30 s, flushing the journal) is a reasonable, mild alternative.

## 10. Recommended OpenBRCM PRE/POST trace format

Every dangerous operation must be logged **before** and **after** it executes:

```
PRE  seq=<N> cp=<CPn/Rn> fn=<ob_sb_reset_test> block=<WRAPPER|D11|PCI|SI>
     op=<READ|WRITE|RMW|PCI_CFG> off=<0x...> width=<16|32> val=<0x...>
POST seq=<N> result=<ok|all_ones|timeout|aborted> rb=<0x...>
```

Requirements:
- strictly monotonic `seq` (across the whole boot);
- checkpoint id and source function;
- block, offset, width, value;
- the `PRE` marker emitted **before** the access; the `POST` immediately after,
  with the readback;
- a distinguishing token (e.g. `openbrcm-trace:`) so the tail can be grepped.

If the machine dies after `PRE N` and before `POST N`, operation `N` is the one
in flight.

**Is printk alone sufficient? No.** printk goes to the kernel ring buffer, whose
tail can be lost on an abrupt reset and only reaches disk via journald's 5-minute
sync. Use a layered scheme:
1. **printk `PRE/POST`** (already how the driver logs) — good for orderly runs;
2. **netconsole** — the authoritative off-box tail (survives any reset);
3. **journald with a short `SyncIntervalSec`** — best-effort on-disk tail;
4. **serial console** if wired — hardware-level tail;
5. `pstore`/`pmsg` only where actually available (pmsg is off here);
6. `trace_printk`/ftrace only for latency analysis, not for crash survivability.

## 11. One-transition-per-boot policy

Until a vendor-confirmed cleanup/reset exists:

1. **Cold boot required** for each experiment.
2. Record the **previous boot ID** and confirm it differs.
3. Confirm **no pre-existing `DEVICE_LOST`**/wedged state.
4. **Instrumentation active before loading OpenBRCM** (netconsole up, journald
   syncing, `collect-previous-crash.sh` ready).
5. Execute **exactly one previously untested state-changing transition**.
6. Take an **immediate postcondition readback**.
7. **STOP** after that one transition (no continuing down the sequence).
8. **No unrelated PCI config probing** afterward, especially if the device is
   wedged.
9. **Reboot if hardware state becomes uncertain.**
10. **Never** run a second experiment in the same boot after `DEVICE_LOST`.

## 12. Post-DEVICE_LOST policy

Default (project policy, conservative — not a proven hardware requirement):

- **STOP all accesses to the endpoint** the moment `DEVICE_LOST`,
  `WRAPPER_UNREACHABLE` after state-changing writes, PCI config all-ones, or D11
  all-ones-after-valid access is observed.
- **Avoid `lspci`/`setpci`/rescan** against the wedged device; do not walk its
  config space (that is the last access before the prior reset).
- **Do not `rmmod`/reprobe repeatedly.**
- **Preserve logs** (run `scripts/collect-previous-crash.sh`).
- **Reboot the host.**

This is a **project safety policy**, chosen because a wedged BCM4352 has now
twice preceded a data-fabric sync flood. It is not claimed to be a vendor
requirement.

## 13. Setup commands

All optional; none auto-reboot. Commands that change system state are marked.

### 13.1 journald (persistent + frequent sync)
```sh
sudo mkdir -p /etc/systemd/journald.conf.d
sudo tee /etc/systemd/journald.conf.d/90-openbrcm-crash.conf >/dev/null <<'EOF'
[Journal]
Storage=persistent
SyncIntervalSec=1s
EOF
sudo systemctl restart systemd-journald
```

### 13.2 netconsole (preferred; runtime, no reboot)
On the **receiver** host (choose an IP on 192.168.1.0/24, e.g. 192.168.1.50):
```sh
# receiver
nc -u -l -p 6666          # or: socat -u UDP-RECV:6666 -
```
On `vinga`, discover the receiver MAC, then enable:
```sh
ip neigh show 192.168.1.50            # get aa:bb:cc:dd:ee:ff
sudo modprobe netconsole
sudo mkdir -p /sys/kernel/config/netconsole/target1
cd /sys/kernel/config/netconsole/target1
echo enp42s0            | sudo tee dev_name
echo 192.168.1.18       | sudo tee local_ip
echo 6666               | sudo tee local_port
echo 192.168.1.50       | sudo tee remote_ip
echo aa:bb:cc:dd:ee:ff  | sudo tee remote_mac
echo 6666               | sudo tee remote_port
echo 1                  | sudo tee enabled
```
Alternative one-shot module load (examples; adjust MAC/IP):
```sh
sudo modprobe netconsole netconsole=6666@192.168.1.18/enp42s0,6666@192.168.1.50/aa:bb:cc:dd:ee:ff
```

### 13.3 serial console (only if physically wired)
Add to the kernel cmdline (GRUB): `console=tty0 console=ttyS0,115200n8`, then
reboot. Receiver: `picocom -b 115200 /dev/ttyUSB0` on the other machine.

### 13.4 ramoops
**Not configured** (no safe reserved region proven). If pursued: determine a safe
e820 region, add `memmap=<size>$<addr>` and
`ramoops.mem_address=<addr> mem_size=<size> ramoops.ecc=1` to the cmdline, and
reboot. Requires explicit approval and memory-map analysis.

## 14. Verification commands

```sh
# readiness audit (read-only, never touches BCM4352)
scripts/check-crash-capture.sh

# journal persistence + previous boot
journalctl -b -1 -k --no-pager | tail
journalctl --list-boots

# netconsole self-test (harmless; does NOT crash/hang the host)
echo "openbrcm-netconsole-test $(date -Is)" | sudo tee /dev/kmsg >/dev/null
#   -> the line must appear on the receiver; then proceed.

# after a reset: collect evidence
scripts/collect-previous-crash.sh
```

## 15. Remaining limitations

- **EFI pstore is unreliable for a sync flood**; it did not capture the last
  event. Its presence must not be treated as coverage.
- **ramoops is not configured** and cannot be safely configured without a proven
  reserved region; even then it may not survive a fabric reset.
- **No off-box tail is active today**; netconsole is recommended but needs a
  receiver host/IP+MAC.
- **Serial console** presence/connection is unverified.
- **kdump** is effectively unavailable and would not help this failure class.
- Hard resets can still lose the last few ms even with a 1 s journal sync; only
  netconsole/serial are truly reset-proof.
- `CONFIG_PSTORE_CONSOLE`/`PMSG` being off means pstore cannot be used as a
  generic console tail; enabling them would require a kernel rebuild.
- A sync flood is a **platform** event; nothing on the host can guarantee a
  post-mortem, which is why the off-box channel is the priority.

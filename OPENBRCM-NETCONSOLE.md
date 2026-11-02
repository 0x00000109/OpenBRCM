# OPENBRCM-NETCONSOLE — off-box kernel logging over wired Ethernet to the Android phone

**Scope: host/ADB/network configuration only. ZERO BCM4352 access** (no OpenBRCM,
no runtime-test.sh, no lspci/setpci/MMIO/PCI-config against the BCM4352).

Status: **WORKING.** `sudo scripts/openbrcm-crash-capture-start.sh` brings up the
whole stack and prints `OPENBRCM CRASH CAPTURE READY` after proving a kernel
message reaches the persistent Android log.

```
Linux kernel / OpenBRCM
        |  netconsole (udp/6666)
        v
  wired Ethernet enp42s0 (192.168.1.18)   <-- never BCM4352
        |  LAN / router
        v
  Android wlan0 192.168.1.20
        |  udplog (autonomous, setsid)
        v
  /sdcard/OpenBRCM/netconsole.log          <-- survives PC reset / ADB loss
```

## 1. Wired interface

| item | value |
|---|---|
| name | `enp42s0` |
| driver | `r8169` (Realtek RTL8168, PCI `10EC:8168` @ `0000:2a:00.0`) |
| independence | **not** BCM4352 (`14e4:43b1` @ `0000:21:00.0`); no `wl*` iface exists |
| state | up, carrier 1 |

## 2. Source IP / MAC

| item | value |
|---|---|
| source IPv4 | `192.168.1.18/24` |
| source MAC | `d8:43:ae:42:58:96` |
| route | `default via 192.168.1.1 dev enp42s0` |

## 3. Receiver IP / MAC

| item | value |
|---|---|
| receiver | **Android phone, adb serial `260f3268`** (Android 12, arm64) |
| receiver IPv4 | `192.168.1.20/24` (wlan0) |
| receiver MAC | `62:9f:37:92:c4:35` (resolved via the wired ARP table; Wi-Fi MAC may be randomized, so the start script re-reads it each run) |
| transport | wired `enp42s0` only; the BCM4352 is never used |

## 4. Receiver implementation

Android's built-in `nc` **cannot** be used: toybox 0.8.4 `nc -u -l -p 6666` (and
every variant, including `-L`) fails with `nc: listen` — it calls `listen()` on a
UDP socket. No `busybox`/`socat`/`ncat`/Python/Go/NDK are available on the phone.

Therefore the receiver is a **tiny freestanding static aarch64 UDP logger built
from our own source** (`scripts/android/udplog.c`, ~2 KB): raw aarch64 syscalls
only, no libc. It binds `0.0.0.0:6666`, and for each datagram appends it (with a
single trailing newline) to `/sdcard/OpenBRCM/netconsole.log` and `fsync`s.

It is built on the PC with `clang --target=aarch64-linux-gnu -nostdlib -static
-fuse-ld=lld`, pushed to `/data/local/tmp/udplog`, and started detached with
`setsid` so it **survives ADB session loss and the Linux PC disappearing** while
Android stays powered. Trusted: our own source, no downloaded binaries.

## 5. Netconsole configuration (configfs)

| field | value |
|---|---|
| `dev_name` | `enp42s0` |
| `local_ip` / `local_port` | `192.168.1.18` / `6666` |
| `remote_ip` / `remote_port` | `192.168.1.20` / `6666` |
| `remote_mac` | `62:9f:37:92:c4:35` |

Classic module-param form also works (`netconsole=6666@192.168.1.18/enp42s0,6666@192.168.1.20/<mac>`),
but configfs is preferred for explicit enable/disable before a hardware test.

## 6. Verification results

- Android UDP listener: **verified** (probe datagram logged).
- ADB-loss independence: **verified** — after `adb kill-server`, UDP messages
  sent with no ADB were still written; reconnecting showed them.
- Kernel netconsole delivery: **verified**. A 10-message spaced test delivered
  **9/10** (netconsole is best-effort); a subsequent 3-message + PRE/POST test
  delivered **all** in order.
- Direct-UDP cross-check: `nc -u` to the phone works (path validated).

Delivered Android-log sample:
```
udplog: listening on udp/6666
[ 4594.013362] OPENBRCM-NETCONSOLE-TEST-1
[ 4594.424063] OPENBRCM-NETCONSOLE-TEST-2
[ 4594.834066] OPENBRCM-NETCONSOLE-TEST-3
[ 4595.243462] PRE seq=100 phase=test op=MMIO block=WRAPPER offset=0x1000+0x408 value=0x1
[ 4595.652587] POST seq=100 result=ok readback=0x00000000
```

## 7. PRE / POST marker convention

```
PRE  seq=<N> cp=<CPn/Rn> fn=<fn> block=<WRAPPER|D11|PCI|SI>
     op=<READ|WRITE|RMW|PCI_CFG> off=<0x...> width=<16|32> val=<0x...>
POST seq=<N> result=<ok|all_ones|timeout|aborted> rb=<0x...>
```

`PRE` before the operation, `POST` after; a missing `POST N` on the receiver
identifies the operation in flight at reset (verified ordering above).

## 8. Re-enable after each reboot (one command)

```sh
sudo scripts/openbrcm-crash-capture-start.sh
```

It verifies the device, (re)starts the Android receiver, verifies the listener,
resolves the wired path + Android MAC, configures netconsole, sends a probe, and
prints `OPENBRCM CRASH CAPTURE READY`.

## 9. Disable

```sh
sudo scripts/openbrcm-crash-capture-stop.sh
```

Disables the netconsole target, unloads the module, and stops the Android
receiver (the log is preserved).

After a PC reset, collect the surviving evidence with:

```sh
scripts/openbrcm-crash-capture-collect.sh
```
which pulls `/sdcard/OpenBRCM/netconsole.log`, the previous-boot journal,
`dmesg`, the reset reason, pstore and a focused sweep into one timestamped
artifact directory.

## 10. Limitations

- **Netconsole is best-effort and lossy** (observed 9/10). It may also be lost
  entirely if a severe AMD data-fabric/PCIe failure takes down the wired NIC or
  the platform before the packet leaves. The persistent journal
  (`Storage=persistent`, `SyncIntervalSec=1s`) remains the on-box record; the
  Android tail is the off-box complement.
- The Android logger must **not** be required to survive an Android reboot; it
  only needs to survive a **Linux PC reset / ADB loss** while Android stays
  powered. Re-run the start script before each experiment.
- The phone's Wi-Fi MAC is randomized and can change; the start script re-reads
  it every run.
- Android may apply Wi-Fi power save to the receiver; delivery has been reliable
  in testing with spaced traffic.
- `check-openbrcm-netconsole.sh` reports the current netconsole/journal state.

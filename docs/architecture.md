# Architecture

OpenBRCM is a SoftMAC `mac80211` driver. It owns only what is specific to the
Broadcom 43xx silicon and delegates everything generic to the kernel.

```
        ┌───────────────────────── mac80211 / cfg80211 ─────────────────────────┐
        │  MLME · scan · rate(rc) · crypto · BA · PM · cfg80211 regd/iw          │
        └──────────────────────────────▲────────────────────────────────────────┘
                                       │ ieee80211_ops (ob_mac80211.c)
   ┌───────────────────────────────────┴───────────────────────────────────────┐
   │ ob_core: lifecycle, vif/scb, chanctx, TXQ, work/DPC                        │
   ├───────────────┬───────────────┬────────────────┬───────────────┬──────────┤
   │ ob_tx / ob_rx │ ob_dma (dma64)│ ob_d11 (MAC,   │ ob_phy (ops)  │ ob_si    │
   │ descriptors   │ rings/NAPI    │ ucode, FIFO)   │ + acphy/...   │ backplane│
   ├───────────────┴───────────────┴────────────────┴───────────────┴──────────┤
   │ ob_osl · ob_ucode · ob_channel/ob_rate                                     │
   ├────────────────────────────────────────────────────────────────────────────┤
   │ bus: bcma (in-kernel) · PCIe/DMA/IRQ/PM · debugfs/tracepoints               │
   └────────────────────────────────────────────────────────────────────────────┘
```

> **Implementation note (current):** the diagram and layers describe the
> **target** architecture. The modules that exist today are `ob_main`,
> `ob_core`, `ob_si`, `ob_dma`, `ob_irq`, `ob_rx`, `ob_fw`, `ob_ucode`,
> `ob_mac80211`, `ob_channel`, `ob_rate`. `ob_tx`, `ob_d11`, `ob_phy*` and
> `ob_osl` do **not** exist yet. See `docs/agent-state.md`.

## Layers
- **Bus (`ob_main.c`, bcma):** registered as a `bcma_driver` for the 80211 core
  (revs 17/23/24). ChipCommon provides the CC/PMU/OTP/SPROM window.
- **Backplane (`ob_si.c`):** PMU/PLL/clock/OTP→SPROM and the MAC read. Register
  map and values come from RE Stage 4/5 (spec-generated `ob_regs.h`).
- **MAC (`ob_d11.c`):** D11 init, microcode upload, FIFO, AMPDU, address match.
- **DMA (`ob_dma.c`):** dma64 rings and 16-byte descriptors, NAPI, threaded IRQ.
- **Data path (`ob_tx.c`, `ob_rx.c`):** mac80211 TXQ → descriptors → FIFO;
  ring → `ieee80211_rx`.
- **PHY (`ob_phy*.c`):** `struct ob_phy_ops` recovered from the blob; MVP acphy,
  other families pluggable.
- **Math (`ob_channel.c`, `ob_rate.c`):** pure, unit-tested (channels, MCS).

## Delegation rules
- The driver never implements MLME, rate selection, or software crypto; it
  exposes hardware knobs through `ieee80211_ops`.
- The blob's ioctl/IOVar surface is **not** reimplemented; it informs which
  hardware action each mac80211 callback must perform.

## Kernel compatibility
Primary target Linux 7.x; 6.12 LTS is also supported. CI builds against a small
matrix and runs `sparse`, `checkpatch`, and KUnit/host tests.

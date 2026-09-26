# M3.4D4 — isolated BCM2069 radio identity probe (`radio_id_probe_only=1`)

Blocker: **`D4-BLOCKER-PLL-BRANCH-HW-PROBE`** (`d4.pll_branch.hw_probe`).

Status: `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` / **frozen candidate** —
**NOT HARDWARE PROVEN / NOT EXECUTED**. Machine-readable contract:
[`radio_probe_contract.json`](radio_probe_contract.json); decoder:
[`radio_probe_decoder.md`](radio_probe_decoder.md).

`D4 IMPLEMENTATION GO: NO`; `HARDWARE TEST GO: YES` for this isolated probe
**only**.

## 0. What this probe is

The previous PLL-selector analysis (`pll_selector_provenance.*`) closed as
`HARDWARE_REQUIRED`: `pi+0x16e` is the **BCM2069 radio revision class =
`radio_rev >> 4`**, read from radio register 0, and it cannot be obtained
statically (no NVRAM/SPROM override; bogus id/rev fails the vendor attach). This
probe is the smallest isolated observation that recovers it.

It runs the minimum proven core prep, reads **only** radio registers 0 and 1
through the D11 radio indirect window, decodes the identity and **STOPS**. It is
not a bring-up and not a register dump.

## 1. Part A — minimum prerequisite (shortest safe prefix)

Vendor evidence (canonical `re`):

- `wlc_phy_attach @0xbe426` is called **only** from `wlc_bmac_attach @0x6a138`.
- In `wlc_bmac_attach` (`re flow`) the order is: `si_setcore @0x69a09` →
  `wlc_bmac_corereset @0x69b1c` → `wlc_bmac_validate_chip_access @0x69b24` →
  `wlc_setxband @0x6a024` → **`wlc_phy_attach @0x6a138`** → `dma_attach
  @0x6a3d6` → … → `si_pmu_rfldo @0x6a90a` → `si_clkctl_init @0x6aa20`.
- The radio reg 0/1 read is inside `wlc_phy_attach` at `0xbeec8..0xbef0a`
  (`re fn wlc_phy_attach --asm`):
  `write16(regs+0x3d8,0); r0=read16(regs+0x3da); write16(regs+0x3d8,1);
  r1=read16(regs+0x3da)`, then the AC packing at `0xbefd1..0xbeffe`.
- `wlc_bmac_core_phypll_ctl` (called by `wlc_bmac_corereset`) **early-returns
  for rev42**: `re fn wlc_bmac_core_phypll_ctl --asm` shows
  `cmp [wlc_hw+0x84],0x27 ; ja return` (phyrev 42 = 0x2a > 0x27). So no PHY PLL
  control write happens on this board.

Therefore the prefix is the D11-side core bring-up only. Classification:

| prerequisite | class | evidence |
|---|---|---|
| PCI/BCMA host up | REQUIRED | radio window is a D11 register access |
| D11 core enabled | REQUIRED | `wlc_bmac_corereset` precedes the read |
| HT/FAST clock | REQUIRED | vendor sets FAST before the read |
| D11 PHY clock (SICF_MPCLKE) | REQUIRED | vendor sets D11 core cflags on the AC branch before the read; proven safe in D3A0/D3A1 |
| ucode upload / PSM | NOT REQUIRED | read precedes upload/PSM |
| common initvals | NOT REQUIRED | read precedes initvals |
| D3A1 tail | NOT REQUIRED | read precedes the post-common tail |
| DMA | NOT REQUIRED | `dma_attach` is after `wlc_phy_attach` |
| D3B MHF / bsinitvals | NOT REQUIRED | band init runs after `wlc_bmac_init` |
| `wlc_phy_init` / AC PHY | NOT REQUIRED | read precedes `wlc_phy_init` |
| attach software object (`pi`) | NOT REQUIRED | the window is raw D11 |
| `wlc_bmac_phy_reset` (PMU PLL) | UNKNOWN | vendor runs it before the read; the probe omits all PHY/PLL writes and validates the window via the radio-id check (a bounded, non-crashing result either way) |

**Earliest safe probe point:** after host up + D11 core enable + FAST clock +
SICF_MPCLKE — i.e. before ucode/PSM/initvals/DMA. No larger milestone is needed.

## 2. Part B — is "read-only" literally read-only?

`re fn read_radio_reg --asm` shows the primitive: for phyrev > 0x17 (BCM4352
rev42) it selects the window `*(pi+0x148)+0x3d8` (addr latch) / `+0x3da` (data)
and performs one 16-bit write + one 16-bit read. The `0x3d8` write is
unavoidable (the data port is only meaningful for the latched register) and
selects a read window; it does not change RF/PLL/radio state.

**Classification: `READ_WITH_NONDESTRUCTIVE_SELECTOR_WRITE`.** A strictly
write-free observation is impossible. The selector write is the exact vendor
primitive used by `wlc_phy_attach` itself.

## 3. Part C — the isolated mode

`radio_id_probe_only=1` (`src/ob_core.c` param; `src/ob_radio.{c,h}`), additive:

- mutually exclusive with every other isolated mode (`ob_isolated_mode_select7`);
  any conflict → `-EINVAL` before hardware;
- normal probe and all older isolated modes are byte-for-byte unchanged
  (`select5`/`select6` retained; `select7` only adds the radio arm);
- stops before PLL A/B, radio-on programming, calibration, chanspec, PHY tables,
  TX/RX, mac80211 and IRQ enable;
- `@remove` uses `ob_radio_remove()`: clean STOP → nothing to tear down;
  fatal/dev_lost → retained + reboot.

## 4. Part D — minimum read set

Exactly two reads: radio register 0 (revision) and radio register 1 (id). No
register dump. Register 1 is kept because it is the only material validation
that the window is the BCM2069 radio (accepted AC ids `{0x2069, 0x030B}`).

## 5. Part E/F — dev_lost safety and accessibility sentinel

- All access goes through the central guard (`ob_d11_*`).
- **PRE_ACCESS**: `ob_d11_read32_trusted(MACCONTROL 0x0120)`; an all-ones value
  latches `dev_lost` and stops before any write.
- **POST_ACCESS**: the same trusted read after the observation, to prove the
  access did not lose the device.
- No arbitrary SHM/OBJDATA/16-bit `0xffff` is used as loss evidence.
- After the latch: zero hardware writes, zero reset/quiesce MMIO, resources
  retained, module pinned, reboot required.

## 6. Part G — exact output

See `radio_probe_contract.json→log_format`. The decode uses the exact proven
expressions; no guessed semantic value is printed.

## 7. Part I — branch closure (prepared, NOT applied)

After a valid capture the decoder emits a `state_transition` block:
`d4.pll_branch.hw_probe` OPEN → CLOSED and `d4.pll_synth.rev42.branch`
HARDWARE_REQUIRED → `PROVEN_A|PROVEN_B|PROVEN_SKIP`. It is **prepared only**;
the on-disk state stays `HARDWARE_REQUIRED / NOT HARDWARE PROVEN` until a real
capture exists.

## 8. Part N — static safety audit

`scripts/radio_probe_audit.py` (comments stripped) PASS. Runtime counts of the
isolated mode: `d11_reads=5`, `d11_writes=2` (both the selector latch),
`radio_address_writes=2`, `radio_reads=2`, `phy_writes=0`,
`radio_state_changing_writes=0`, `pll_writes=0`, `calibration_ops=0`,
`shm_writes=0`, `dma_programming_ops=0`, `irq_programming_ops=0`,
`unbounded_polls=0`, `bounded_polls=0`, `axi_ioctl_writes=1` (MAC-PHY clock).
All dangerous categories are zero except the unavoidable selector/address write.

## 9. Part P — HUMAN ONE-SHOT PROCEDURE (do not run from an agent)

STOP: an agent must not execute any of these commands. Run only with explicit
owner approval, on the target host, once. **Do not clear the kernel log** — the
harness writes a unique marker and isolates the current run from it, so
historical messages can never be misclassified.

### Preferred: the deterministic harness

```sh
# dry run first (no hardware; emits exactly one isolated insmod)
scripts/runtime-test.sh --mode radio_id_probe_only --dry-run ./openbrcm.ko

# real one-shot (owner approval)
sudo scripts/runtime-test.sh --mode radio_id_probe_only \
     --candidate 185408d64a3b373c9dda9e9055ac74f89abecd9b ./openbrcm.ko
echo "harness rc=$?"
```

The harness:
- refuses if `HEAD != --candidate`, the module is missing, `openbrcm` is already
  loaded, or the expected parameter/preconditions are invalid;
- issues exactly `insmod ./openbrcm.ko "radio_id_probe_only=1"` (never bare);
- does **not** clear dmesg; writes `[openbrcm-test] marker <stamp>` and captures
  full dmesg `/tmp/openbrcm-runtime-<stamp>.log`, journal
  `/tmp/openbrcm-journal-<stamp>.log`, current-run slice
  `/tmp/openbrcm-runtime-<stamp>.run.log`, focused radio log
  `/tmp/openbrcm-radio-<stamp>.log`, and decoded JSON
  `/tmp/openbrcm-radio-<stamp>.json` (paths printed at the end);
- runs the offline decoder automatically (read-only; it never influences
  hardware);
- `rmmod openbrcm` **only** after a deterministic clean PASS (all ten markers,
  `pre_access`/`post_access` not all-ones) and `insmod rc=0`.

Exit status: `0` clean PASS + decode ok; `3` fault → **REBOOT REQUIRED**, module
left loaded, no rmmod/unbind/retry; `4` incomplete or `CAPTURE CLEAN / DECODE
FAILED` (blocker NOT proven) after a safe rmmod.

### Manual fallback (same semantics)

```sh
uname -r                                    # expect 7.0.0-34-generic
sha256sum openbrcm.ko                       # must equal 706b3407…06dc8d
modinfo -F signer openbrcm.ko               # Broadcom Driver MOK
lsmod | grep -w openbrcm || echo "not loaded"
# marker (do NOT clear dmesg)
logger -t openbrcm-test "[openbrcm-test] marker manual-$$"
sudo insmod ./openbrcm.ko radio_id_probe_only=1; echo "insmod rc=$?"
sudo dmesg >/tmp/radio_probe.dmesg
sudo journalctl -k -n 4000 --no-pager >/tmp/radio_probe.journal
grep -aE 'radio-probe:|openbrcm:|BUG:|Oops|WARNING|Call Trace|AER|DMA-API|DEVICE LOST' /tmp/radio_probe.dmesg
python3 scripts/decode_radio_probe.py --log /tmp/radio_probe.dmesg --json /tmp/radio_probe_capture.json
```

**Clean PASS** (all markers + decode rc=0): a verified `rmmod openbrcm` is
permitted. **ANY of** `DEVICE LOST`/`dev_lost`, `radio-probe: FAIL`, `BUG`,
`Oops`, a test-attributable `WARNING`, a `Call Trace`, `AER`, a DMA-API fault,
`lockup`/`hang`, an accessibility failure, or an unverified teardown ⇒ **DO NOT
`rmmod`, DO NOT unbind, DO NOT retry; capture the logs and reboot.**

## 10. Frozen candidate

See `radio_probe_contract.json→build_identity` and the ledger record.

## 11. PML/PLL-reset omission audit (existing open question only)

The probe omits the vendor `wlc_bmac_phy_reset` (PMU/PLL) step that
`wlc_bmac_corereset` runs before the radio read. This section audits **only**
whether that omission is safe for diagnostic classification.

- **Boundedness.** The radio read is two 16-bit D11-window accesses (one
  selector write + one data read per register) with no polling, no
  self-modifying sequence and no unbounded loop. A non-responsive/unclocked
  radio interface therefore cannot hang or fault the access; it can only yield
  a bounded value.
- **Outcomes.** Omission can result only in **(A)** a valid radio identity read,
  or **(B)** a bounded invalid/unsupported result. (B) is caught by: the all-ones
  pair check, the accepted-id check (`{0x2069, 0x030B}`), the live pre/post
  accessibility sentinels, and the **revision-domain** check below.
- **Silent-acceptance guard.** A BCM2069 read is accepted only if its revision
  byte is inside the recovered domain
  `{0,1,2} ∪ [3..38] ∪ {254}` (`radio_identity_map.json`). Any other revision is
  **rejected as ambiguous** by `scripts/decode_radio_probe.py`
  (`KNOWN_2069_REVS`), so a stuck/unclocked window cannot be silently accepted
  as a valid revision, and the blocker is never closed from an ambiguous value.
- **Reset not added.** The reset is deliberately **not** added; the probe stays
  minimal. `wlc_bmac_core_phypll_ctl` is already a proven no-op for rev42. A
  valid capture therefore additionally confirms that the PMU/PLL reset is not a
  prerequisite for reading the radio identity on this board.

Residual limitation (stated honestly): a stuck window that coincidentally
returns `reg1 == 0x2069` **and** an in-domain `reg0` revision cannot be excluded
by a single two-read observation without extra reads; adding reads would change
the frozen hardware sequence, which this task must not do. The domain criterion
removes the practical silent-acceptance path and never weakens the safety gates.

## 12. Harness integration

`scripts/runtime-test.sh --mode radio_id_probe_only` implements the success /
fatal rules and capture list above; `--evaluate-log <mode> <file> [--marker m]`
classifies an existing capture offline (used by the host tests). Every older
mode is unchanged (same `success_marker`/`owns_dma` semantics and exit codes).


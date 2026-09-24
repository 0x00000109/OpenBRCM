# M3.4D3B — external-SPROM evidence capture for MHF3

**Status: `IMPLEMENTED` / `STATIC TESTED` / `SIGNED` (frozen candidate, NOT
executed on hardware).** D3B remains `ANALYSIS ONLY` / NOT HARDWARE PROVEN;
`D3B IMPLEMENTATION GO: NO` (blocked on **MHF3** only).

This milestone adds the read-only capture of the **already-read,
CRC-validated rev11 external SPROM image** required to resolve MHF3
(`antsel_type`). It is **not** a new hardware-probe architecture: it reuses the
existing OpenBRCM SPROM read path and adds an emission (log) of the words that
path already reads. It does **not** implement band init / bsinitvals / D3B.

Context: `docs/m34d3b_band_init.md` §3.8 (value closure) and
`docs/m34d3b/evidence.jsonl`.

## 1. Existing SPROM read path and data lifetime (Phase 1 audit)

Normal probe call graph (`src/ob_core.c:260` → `src/ob_si.c`):

```
ob_probe (ob_core.c)                       [identity check, hw alloc, drvdata]
└─ ob_si_probe (ob_si.c)
   ├─ hw->cc = hw->bus->drv_cc.core                       [ChipCommon window]
   ├─ ob_si_powerup
   │    └─ ob_si_dump_sprom("before"/"after")  4 fixed reads (CC+0x800/0x84c…)
   ├─ ob_si_d11_diag                            read-only D11 host-if reads
   ├─ ob_si_dump_bus_sprom                      logs bcma->sprom (already parsed)
   ├─ hw->mac_valid = (ob_si_read_mac(hw, hw->mac) == 0)
   │    └─ u16 *sp = kcalloc(234)
   │       for words in {220, 230, 234}:
   │           for k in 0..words-1:
   │               sp[k] = ob_si_cc_read16(CC + 0x800 + k*2)   <-- the 234 reads
   │           crc = ob_sprom_crc(sp, words); rev = sp[words-1]&0xff
   │       extract MAC from sp at rev11 +0x90; kfree(sp)
   └─ if (sprom_diag) ob_si_sprom_diag(hw)        [default sprom_diag = true]
        ├─ ob_si_dump_sprom_window(hw, 0, 0xa0)   reads 80 words (16-bit)
        ├─ read rev word at 0x7e (16-bit)
        ├─ u16 *sp = kcalloc(234)
        │    for words in {220, 230, 234}:
        │        for k in 0..words-1:
        │            sp[k] = ob_si_cc_read16(CC + 0x800 + k*2)  <-- the 234 reads
        │        crc = ob_sprom_crc(sp, words); erev/ecrc from sp[words-1]
        ├─ ob_si_emit_sprom11(hw, sp, 234, rev, ecrc, crc)   [NEW: zero MMIO]
        └─ kfree(sp)
```

Facts proven by inspection:

1. **Exact number of words read (already, before this change):** the size loop
   reads `220`, then `230`, then `234` words; the array is sized
   `OB_SPROM_WORDS_R11 = 234` (`src/ob_si.h:77`). The 234-word read already
   existed in both `ob_si_read_mac()` and `ob_si_sprom_diag()`.
2. **Storage:** `u16 *sp = kcalloc(OB_SPROM_WORDS_R11, sizeof(*sp), GFP_KERNEL)`
   (local to each function).
3. **All 234 words remain available after CRC validation:** the loop is not
   short-circuited; after it, `sp[0..233]` holds the full 234-word image
   (the last iteration is the 234-word read).
4. **`ob_si_dump_sprom` does NOT receive that array:** it does its own four
   fixed reads (rev4 MAC window) and is unchanged.
5. **Expanding output requires ZERO extra ChipCommon/SPROM MMIO reads:**
   `ob_si_emit_sprom11()` only formats a copy of `sp[]` and calls
   `ob_sprom_crc()` (pure). No `ob_si_cc_read*`/`bcma_read*` is added.

## 2. Smallest evidence-emission change (Phase 2)

`src/ob_si.c` gains `ob_si_emit_sprom11()` (a pure formatter over the passed
array) and a single call at the end of `ob_si_sprom_diag()` — after the existing
CRC/revision validation, before MAC handling elsewhere, before `kfree(sp)`.
`ob_si_read_mac()` and MAC-extraction semantics are untouched. `sprom_diag=0`
emits nothing (the call is inside the `sprom_diag`-gated diagnostic); the dump is
never unconditional.

Log format (8 words/line; stable, parseable; raw only — no field
interpretation):

```
openbrcm: sprom11: BEGIN words=234 revision=11 crc=XX calc=YY valid=1
openbrcm: sprom11: 000: xxxx xxxx xxxx xxxx xxxx xxxx xxxx xxxx
openbrcm: sprom11: 008: xxxx ...
...
openbrcm: sprom11: END words=234
```

## 3. Offline decoder (Phase 3)

`scripts/sprom11_decode.py` (no hardware access):

- parses the `openbrcm: sprom11:` BEGIN/data/END block deterministically;
- validates word count = 234, revision = 11 and recomputes the CRC with the
  exact OpenBRCM/bcma rule (reflected CRC-8, poly `0xAB`, final `^0xFF`;
  identical to `ob_sprom_crc()` in `src/ob_si.c`);
- preserves the complete raw image (JSON);
- decodes **only** fields whose rev11 raw offset is supplied as provenance
  (`--offsets FILE`); unknown offsets are reported `null` with reason — offsets
  are never guessed. An UNPROVEN bcma-rev8 candidate table exists but is behind
  `--allow-candidate-offsets` and is labelled in the output;
- reproduces the vendor `wlc_antsel_attach` `antsel_type` function **exactly**
  (`docs/m34d3b_band_init.md` §3.3) and maps it to MHF3
  (`{2,3,6}→0x3`, `1→0x1`, else `0x0`).

Each decoded field records: `field`, `byte_offset`, `word_indices`,
`bit_width`, `shift`, `raw`, `value`, `transform`, `provenance`.

Example (no offsets supplied yet):

```sh
python3 scripts/sprom11_decode.py --json /tmp/openbrcm-sprom.log > sprom11.json
python3 scripts/sprom11_decode.py --antsel-inputs boardtype=0x8f,boardflags=0x0,antswitch=0,aa2g=0,aa5g=0
```

## 4. Rev11 raw offsets — remaining static blocker

The five target fields are proven **SPROM-derived** (not NVRAM-only): the vendor
`srom_parsecis` emits `boardtype=0x%x`, `boardflags=0x%x`, `aa2g=0x%x`,
`aa5g=0x%x`, `antswitch=0x%x` from its parse cursor
(`docs/m34d3b_band_init.md` §3.8.5), and C3 (`bcm47xx_sprom.c`) corroborates the
semantics (`aa2g = ant_available_bg`, `aa5g = ant_available_a`). However the
parse cursor is a computed byte index into the vendor parser's working buffer,
not a fixed raw SPROM offset, so the exact rev11 word indices are **not yet
recovered**; the decoder therefore refuses to decode them until supplied. This
is a static RE task (tooling gap T7), not a hardware blocker. The captured image
is the necessary input.

## 5. Tests (Phase 4)

`tests/host/test_sprom11_decode.py` (run by `make hosttest`): parse exactly 234
words, truncated-image rejection, bad word-count header rejection, malformed
word rejection, missing BEGIN rejection, CRC failure detection, revision != 11
rejection, deterministic re-serialization, `antsel_type` known vectors (all
branches) and the exact MHF3 mapping. 10/10 pass.

## 6. Frozen read-only hardware candidate (Phase 5)

The capture runs in an isolated, read-only mode so the future hardware run does
**not** initialize DMA/IRQ or load firmware. `src/ob_core.c` gains
`sprom_evidence_only=1`: it sets the ChipCommon window and calls
`ob_si_sprom_evidence()` → the existing `ob_si_sprom_diag()` (read-only), then
returns before `ob_fw_probe`/`ob_dma_init`/`ob_irq_init`/`ob_rx_init`/
`ob_mac80211_register`. It is mutually exclusive with the other isolated modes.
`ob_remove()` skips all teardown for it.

Exact future one-shot command — **NOT EXECUTED** (no hardware was accessed):

```sh
# frozen candidate: commit 739273c857d185f861467536a639fad4aa5b0bee
#   openbrcm.ko sha256 538588e29634287971abcd568aae208281b22055ed1aa3e1ae6699eaed4741d5
#   srcversion AC97164D23C815F7BF5D349  vermagic 7.0.0-34-generic SMP preempt mod_unload modversions
#   signer "Broadcom Driver MOK"
sudo dmesg -C
sudo insmod openbrcm.ko sprom_evidence_only=1
sudo dmesg | grep -E 'openbrcm: sprom11:|sprom-evidence'
sudo rmmod openbrcm
# offline:
#   sudo dmesg > /tmp/openbrcm-sprom.log
#   python3 scripts/sprom11_decode.py --json /tmp/openbrcm-sprom.log > sprom11.json
```

The run does **not** enable `ucode_test_only`/`initvals_test_only`/
`dma_test_only`/`d11_tail_test_only`, does not upload firmware, does not start
PSM, does not initialize DMA, does not configure IRQ, does not touch
PHY/radio/channel and does not enable MAC.

## 7. `re` tooling gaps (Phase 6)

The gaps filed during the MHF closure (T1–T6) are recorded in
`docs/re-tooling.md` §9 with their scoped `re` improvements. They do **not**
expand this milestone. T7 (rev11 cursor resolution) is the next static task.

## 8. STOP boundary

This milestone stops after the capture implementation + offline decoder +
tests, the signed frozen candidate, and documentation. It does not execute any
hardware command, does not implement D3B, and does not touch PHY/radio/channel.
Next: the one-shot read-only `sprom_evidence_only=1` run (owner approval), then
static recovery of the rev11 offsets and MHF3 evaluation.

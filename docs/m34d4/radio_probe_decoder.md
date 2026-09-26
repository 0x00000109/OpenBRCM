# M3.4D4 — radio probe offline decoder

Tool: [`scripts/decode_radio_probe.py`](../../scripts/decode_radio_probe.py)
(tests: [`tests/host/test_radio_probe_decoder.py`](../../tests/host/test_radio_probe_decoder.py)).

It consumes the `radio-probe:` lines from a captured dmesg/journal and produces
a compact JSON artifact. It is deterministic, needs no LLM and does not read the
vendor blob: the exact extraction already proven in
`wlc_phy_attach @0xbeff8` (AC path) is re-applied independently.

## Usage

```sh
python3 scripts/decode_radio_probe.py --log capture.txt --json capture.json
python3 scripts/decode_radio_probe.py < capture.txt
python3 scripts/decode_radio_probe.py --log capture.txt --expect-candidate <sha256>
python3 scripts/decode_radio_probe.py --log capture.txt --expect-module-sha256 <sha256>
```

Exit status: `0` accepted, `1` rejected (reason on stderr), `2` usage error.

## Decode (shared with `src/ob_radio.h::ob_radio_decode`)

| field | expression |
|---|---|
| `radio_id` | `reg1` |
| `radio_rev` | `reg0 & 0xff` |
| `rev_low` | `reg0 & 0x0f` |
| `revision_class` | `(reg0 >> 4) & 0xff` (`pi+0x16e`) |
| `pll_branch` | class 1 → `A`; class 2 → `B`; 2069 other class → `SKIP`; `radio_id != 0x2069` → `UNKNOWN` |

## Rejections (Part H)

- malformed log (no `BEGIN` / no `PASS` / no `STOPPED BEFORE PLL/RADIO INIT`);
- missing required values (`reg0_raw`, `reg1_raw`, `radioid`, `radiorev`,
  `revision_class`, `pll_branch`);
- duplicate conflicting values for the same key;
- candidate mismatch (`--expect-candidate`) or module sha256 mismatch;
- **impossible extraction**: any declared decode that disagrees with the
  independently recomputed one (tamper/consistency guard);
- explicit dev_lost / `FAIL` result (including any `DEVICE LOST` line);
- all-ones register pair;
- unexpected radio id (AC accepts `0x2069` / `0x030B`);
- **ambiguous BCM2069 revision** outside the recovered domain
  `{0,1,2} ∪ [3..38] ∪ {254}` — this is the PML/PLL-reset-omission guard: a
  stuck/unclocked radio window cannot be silently accepted as a valid revision
  (and the PLL blocker is never closed from it).

## Output

`artifact=radio_probe_capture` with `raw`, `decoded`, `identity`, `provenance`
(blob sha256 + extraction + branch mapping + evidence paths), optional
`candidate`/`module_sha256`/`kernel`, and a `state_transition` block that is
**prepared but not applied**:

```
d4.pll_branch.hw_probe : OPEN   -> CLOSED
d4.pll_synth.rev42.branch : HARDWARE_REQUIRED -> PROVEN_A | PROVEN_B | PROVEN_SKIP
```

The state transition is only carried out in the repo state once a real,
decoder-accepted capture is attached to the evidence.

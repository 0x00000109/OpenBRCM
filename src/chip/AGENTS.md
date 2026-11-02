# Module: chip (device_profile)

This is the first physically separated OpenBRCM source module (M8-PILOT). Until
Phase 31 the profile implementation lived directly under `src/`; the shared
public interface intentionally remains there.

## Responsibilities

One immutable descriptor per known hardware identity, plus the pure resolver
that admits or rejects an identity:

- PCI vendor/device/revision; subsystem and board identity as evidence only.
- Broadcom chip ID/revision.
- BCMA D11 manufacturer/core/revision/class.
- PHY type/revision and radio ID/revision/class.
- Firmware/initvals selection keys (names, sizes, FNV pins).
- Revision-specific behaviour flags.
- Admission state and required evidence for an evidence-only profile.

The module answers identity and policy questions. It does not execute an
initialization pipeline and does not decide when a pipeline stage runs.

## Interfaces

- `src/ob_profile.h` — the public interface. It stays at its historical path;
  include it as `"../ob_profile.h"` from this directory.
- `src/ob_chip_profile.h` — compatibility include; do not add new users.
- `ob_chip_profile_resolve()` — the single admission point. It is pure and
  returns `OK`, `INVALID_ARGUMENT`, `MISSING_IDENTITY`,
  `UNSUPPORTED_DEVICE`, `UNSUPPORTED_CHIP_REVISION`,
  `UNSUPPORTED_D11_REVISION` or `IDENTITY_CONTRADICTION`.
- `ob_profile_bcm4352_rev03()` / `ob_profile_bcm4360_evidence_only()` — the
  immutable descriptors. A consumer must only receive a descriptor through the
  resolver; the direct accessors exist for evidence tests and audit artifacts.

## Source of truth

Device data is not authoritative here by itself. It mirrors:

- `audit/openbrcm_integration/BCM4352_PROFILE.json` (per-field provenance).
- `audit/openbrcm_integration/BCM4360_REQUIREMENTS.json` (E1-E8, UNKNOWNs).
- `docs/data/verification/stand-readonly-checks.md` (observed PCI IDs and
  subsystems).
- `docs/data/verification/on-stand-dev-runs.md` (identity per run).
- `src/ob_fw.h` for the firmware names, sizes and FNV values; that header
  remains the validator, the descriptor only mirrors it.

`src/ob_main.c` owns the BCMA id table. It must advertise only revisions with an
enabled profile. D11 revisions 17, 23 and 24 are historical future candidates
and are deliberately not bound; their records are kept in the table comment.

## Permitted dependencies

None. This is a leaf module:

- no MMIO or register access;
- no allocation, sleeping, locking or logging side effects;
- no PCI config-space read;
- no BCMA enumeration;
- no firmware request or validation;
- no call back into `ob_core`, `ob_si`, PHY/radio, DMA, IRQ or mac80211.

Headers may include `../ob_profile.h` and the host shim `linux/types.h` in
tests only.

## Device-specific versus shared data

- `ob_profile.c` is shared logic: registry lookup, rejection precedence and the
  disabled-profile rule. It must not grow chip-specific branches.
- `ob_profile_bcm4352.c` is the only admitted device: BCM4352 PCI `14e4:43b1`
  rev03, chip `0x4352` rev3, D11 rev42 class0, PHY `0x0b` rev1, radio `0x2069`
  rev4, firmware rev42 pins.
- `ob_profile_bcm4360.c` is evidence only: `enabled = false`, PCI
  `14e4:43a0` rev03, and chip/D11/PHY/radio/firmware/board values left
  UNKNOWN with E1-E8. Never infer a value from the model number.

Adding a device means adding a separate data file and a registry entry, not a
new pipeline and not a branch in the resolver. A new profile starts disabled
until its evidence is complete.

## Validation

From the repository root:

```sh
make -C tests/host ob_chip_profile_test ob_profile_evidence_test \
     ob_admission_policy_test
tests/host/ob_chip_profile_test
tests/host/ob_profile_evidence_test
tests/host/ob_admission_policy_test
python3 tests/host/test_chip_profile_admission.py
python3 tests/host/test_admission_policy.py

make -C tests/host   # full host suite; known pre-existing blockers are listed
```

The M7 admission and M8 module-interface gates are recorded under
`audit/openbrcm_phase31/`. A source move is not complete until the isolated
kernel build, symbol index and Phase 29 full-state differential pass.

## Prohibited assumptions

- That a BCMA match admits a device. Matching only calls `ob_probe()`; the
  profile resolver decides, and a rejection must return before allocation,
  mode dispatch, firmware, DMA, PHY/radio or mac80211 work.
- That `14e4:43a0` is a BCM4352 (Broadcom README error; the stand shows 4352 =
  `43b1` and 4360 = `43a0`).
- That BCM4360 is ready because its PCI ID or a guessed D11 revision is known.
- That an UNKNOWN value may be replaced by a default to satisfy a C
  initializer.
- That subsystem/board ID proves silicon identity.
- That `BCMA_ANY_REV` is acceptable. It is not.

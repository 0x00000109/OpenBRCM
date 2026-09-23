# Provenance & clean-room policy

OpenBRCM is a **clean-room reimplementation**. The following rules are binding
for all contributions.

## What the code is derived from
1. The **OpenBRCM reverse-engineering specification** (Repo B: analysis of the
   relocatable object `wlc_hybrid.o_shipped`, sha256
   `352a6e349f74c69b78e76f68c63752c99b8f6b22dc942af531b754211d7f4743`).
2. **Publicly documented silicon behaviour** and Linux kernel APIs.

Generated headers (`src/ob_regs.h`, `ob_dma_regs.h`, `ob_rates.h`, `ob_iovar.h`,
`ob_wlc.h`) are produced from the specification and must be regenerated, not
hand-edited.

## What must NOT be used as a source of code
- The proprietary blob's machine code beyond reading it for the specification.
- **Any code copied from in-tree drivers** (`brcmsmac`, `b43`, `bcma`,
  `brcmfmac`, etc.). They may be consulted for *architecture and API usage as
  hypotheses only*, and each such consultation is logged in the analysis
  reference ledger — but no code, comments, identifiers, or expressions may be
  transferred.

## Attribution
All contributions are under GPL-2.0-only and require a `Signed-off-by` (DCO).
By contributing you assert the above rules were followed.

## Reporting
If you believe a file violates this policy, open a confidential issue
(see `SECURITY.md`).

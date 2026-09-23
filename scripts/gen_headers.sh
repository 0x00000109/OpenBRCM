#!/usr/bin/env sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Regenerate the generated headers from the OpenBRCM reverse-engineering
# specification (Repo B). Do not hand-edit src/ob_regs.h, ob_dma_regs.h,
# ob_rates.h, ob_iovar.h, ob_wlc.h.
#
# Usage:  OBDP=/path/to/OBDP/project ./scripts/gen_headers.sh
set -eu

SRC_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SPEC="${OBDP:-$SRC_DIR/../../driver/broadcom/OBDP/project}"

need() {
	[ -f "$SPEC/$1" ] || { echo "missing $SPEC/$1" >&2; exit 1; }
}

need brcm_ppc.h
need brcm_dma.h
need brcm_rates.h
need brcm_iovar.h
need brcm_wlc.h

cp "$SPEC/brcm_ppc.h"   "$SRC_DIR/src/ob_regs.h"
cp "$SPEC/brcm_dma.h"   "$SRC_DIR/src/ob_dma_regs.h"
cp "$SPEC/brcm_rates.h" "$SRC_DIR/src/ob_rates.h"
cp "$SPEC/brcm_iovar.h" "$SRC_DIR/src/ob_iovar.h"
cp "$SPEC/brcm_wlc.h"   "$SRC_DIR/src/ob_wlc.h"

echo "Regenerated ob_*.h from $SPEC"

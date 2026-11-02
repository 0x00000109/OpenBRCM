// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — rev42 staged diagnostic bring-up (REV42-DIAGNOSTIC-BRINGUP).
 *
 * Isolated mode `rev42_diag_probe_only=1`, optional staged modifier
 * `rev42_diag_full=1`. See src/ob_diag.h for the full contract.
 *
 * The vendor-minimal path is the CONFIRMED_VENDOR_REV42 radio-ID prerequisite:
 * D11 core out of reset + MAC/PHY clock + wlc_phy_anacore(pi,1) direct side +
 * the radio window read (0x3d8 address / 0x3da data). It does NOT require
 * ucode, PSM run, common initvals, bsinitvals, EN_MAC, DMA or interrupts.
 *
 * The staged path (rev42_diag_full=1) additionally runs the hardware-proven
 * D2A (ucode + PSM) and D2B (common initvals) stages so CP1/CP2/CP3 can be
 * recorded. Those stages are reused verbatim; this file adds no new
 * ucode/initval semantics.
 *
 * Safety: no EN_MAC, no MACINTMASK, no host IRQ route, no DMA engine, no TX.
 * Every access goes through the central dev_lost guard; once the monotonic
 * latch is set, no further MMIO is issued and the module is pinned.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>

#include "ob_core.h"
#include "ob_diag.h"
#include "ob_ai_wrapper_only.h"
#include "ob_guard.h"
#include "ob_ucode.h"
#include "ob_si.h"
#include "ob_d3a0.h"
#include "ob_initvals.h"

/* ---- AI-wrapper-only diagnostic (Phase 36Z) ----------------------------- *
 *
 * REPLACES the retired `rev42_ai_probe_only` sequence. That mode read the D11
 * functional-core window (core->addr + 0x0FFC) immediately after observing the
 * core still held in reset; in the single live experiment it preceded loss of
 * PCI configuration-space access for the endpoint and its upstream port. The
 * electrical cause is UNKNOWN and no cause is claimed here.
 *
 * The machine-checked contract lives in src/ob_ai_wrapper_only.h. In one
 * sentence: at most THREE reads, all in the BCMA-selected AI wrapper window
 * (core->wrap) — RESET_ST, RESET_CTL, IOCTL — with a stop on the device-loss
 * latch, on a callback error, and on an all-ones read of any of the three.
 *
 * Why this is not "the same probe with one read removed":
 *   - the functional-core accessor is not called anywhere in this file's new
 *     path, and `struct ob_aiw_ops` has no write or config-space member;
 *   - the accessor is reached through BCMA, which owns window selection and
 *     bus->mapped_core; this code does not program a PCI window register
 *     itself and never iomaps BAR0, but BCMA programs two of them on its
 *     behalf (see the side-effect note below);
 *   - the sequence is straight-line and terminates at the wrapper boundary in
 *     BOTH core states: a passing ai_iscoreup predicate does NOT authorise a
 *     functional-core read.
 *
 * BCMA side effect, stated rather than hidden (corrected in amendment 01):
 * ob_axi_read32() -> bcma_aread32() -> bcma_host_pci_switch_core() performs
 * TWO pci_write_config_dword() calls whenever BCMA's mapped_core differs from
 * this core: 0x80 (BCMA_PCI_BAR0_WIN) = core->addr, and 0x70
 * (BCMA_PCIE2_BAR0_WIN2) or 0xAC (BCMA_PCI_BAR0_WIN2) = core->wrap, selected
 * by bus->host_is_pcie2. This device enumerates a PCIe Gen2 core (id 0x83C),
 * so host_is_pcie2 is true and the second write lands at 0x70. These are PCI
 * configuration-space writes performed by BCMA: not MMIO, and not D11
 * functional-core accesses. The driver neither issues nor bypasses them.
 *
 * (Amendment 01 also corrected the earlier "0x70/0x74" wording: 0x74 is not a
 * window selector in the running kernel's bcma.ko. The two writes are 0x80 and
 * 0x70-or-0xAC, verified by disassembling that bcma.ko.)
 */
static int ob_aiw_read_bcma(void *ctx, enum ob_aiw_window win, u16 off,
			    u32 *val)
{
	struct ob_hw *hw = ctx;

	if (!hw || !val)
		return -EINVAL;
	/* Exactly one window exists in the contract; anything else is a bug,
	 * not a feature. This is the guard a "just add a core read" mutation
	 * has to defeat. */
	if (win != OB_AIW_WIN_WRAPPER)
		return -EINVAL;
	if (hw->dev_lost)
		return -EIO;
	*val = ob_axi_read32(hw, off);
	/* ob_axi_read32() has no error channel: if the latch set during the
	 * access the accessor returns the dead sentinel without touching MMIO.
	 * Report that as a failed read instead of presenting it as a value. */
	if (hw->dev_lost)
		return -EIO;
	return 0;
}

static void ob_aiw_log_dev(void *ctx, enum ob_aiw_event ev,
			   const struct ob_aiw_log *rec)
{
	struct ob_hw *hw = ctx;

	switch (ev) {
	case OB_AIW_EV_BEGIN:
		dev_emerg(hw->dev,
			  "rev42-aiw: BEGIN (subset rev42_ai_wrapper_only; BCMA-owned addressing; max 3 wrapper reads; no functional-core access)\n");
		break;
	case OB_AIW_EV_PRE:
		dev_emerg(hw->dev,
			  "PRE  seq=%u phase=ai_wrapper_only op=BCMA_AREAD32 off=0x%04x window=wrapper\n",
			  (unsigned int)rec->seq, rec->off);
		break;		case OB_AIW_EV_POST:
			/* Only a completed, non-suspicious observation reaches this
			 * event; an all-ones value is classified before POST and
			 * logged as PRE -> STOP reason=all_ones instead. */
			dev_emerg(hw->dev,
				  "POST seq=%u status=ok readback=0x%08x\n",
				  (unsigned int)rec->seq, rec->value);
			break;
	case OB_AIW_EV_STOP:
		dev_emerg(hw->dev,
			  "STOP seq=%u reason=%s error=%d (no further register access)\n",
			  (unsigned int)rec->seq,
			  rec->reason ? rec->reason : "unspecified", rec->error);
		break;
	case OB_AIW_EV_END:
		dev_emerg(hw->dev,
			  "rev42-aiw: END rev42_ai_wrapper_only %s - three wrapper reads only; no D11 core access, no reset/enable, no firmware/PHY/radio/DMA/IRQ/mac80211\n",
			  OB_AIW_TOKEN_COMPLETE);
		break;
	}
}

static bool ob_aiw_dev_lost_bcma(void *ctx)
{
	struct ob_hw *hw = ctx;

	return hw && hw->dev_lost;
}

/*
 * rev42_ai_wrapper_only: the ONLY live-capable rev42 diagnostic implemented in
 * this phase. Returns 0 when the three-read sequence completed, the negative
 * callback errno when a read failed, and -EIO on a suppressed/suspicious
 * observation. A completed sequence is an OBSERVATION, never an
 * initialisation: no lifecycle state is published, no firmware is loaded, and
 * the D11 functional window is not touched in either core state.
 */
int ob_ai_wrapper_only_test(struct ob_hw *hw)
{
	struct ob_aiw_ops ops;
	struct ob_aiw_result res;
	int ret;

	if (!hw || !hw->dev)
		return -ENODEV;

	ops.ctx = hw;
	ops.read32 = ob_aiw_read_bcma;
	ops.log = ob_aiw_log_dev;
	ops.dev_lost = ob_aiw_dev_lost_bcma;

	/* Missing core/bus or a missing callback issues ZERO device callbacks.
	 * The gate is pure so the host tests execute this exact predicate. */
	ret = ob_aiw_entry_check(true, hw->core != NULL, hw->bus != NULL, &ops);
	if (ret) {
		dev_err(hw->dev,
			"rev42-aiw: prerequisite missing (core=%d bus=%d); no register access\n",
			hw->core ? 1 : 0, hw->bus ? 1 : 0);
		return ret;
	}

	/* The entry latch is read here, once, and passed in: zero callbacks
	 * when the device is already lost. */
	ret = ob_aiw_run(&ops, hw->dev_lost, &res);

	if (!res.complete) {
		/* Attempts and completed observations are reported separately:
		 * a callback error on seq 2 was still ATTEMPTED twice, even though
		 * only one observation completed. */
		dev_crit(hw->dev,
			 "rev42-aiw: %s after %u attempted, %u completed of %u reads; seq=%u off=0x%04x reason=%s error=%d\n",
			 OB_AIW_TOKEN_STOP, (unsigned int)res.reads_attempted,
			 (unsigned int)res.reads_completed,
			 (unsigned int)OB_AIW_MAX_READS,
			 (unsigned int)res.failed_seq, res.failed_off,
			 res.stop_reason ? res.stop_reason : "unspecified",
			 res.last_error);
		/*
		 * A suspicious wrapper read is a device-loss indication for
		 * these three known control/status registers. Latch it so the
		 * central guard blocks every later MMIO, and let ob_probe()
		 * apply the existing retention rule (keep the device bound).
		 */
		if (res.suspicious) {
			hw->dev_lost = true;
			dev_crit(hw->dev,
				 "rev42-aiw: all-ones on a wrapper control/status register; device-loss latch set, device retained, no cleanup attempted\n");
		}
		dev_emerg(hw->dev,
			  "rev42-aiw: fault-latch=%d (0 = no device-loss latch was set during this diagnostic)\n",
			  hw->dev_lost ? 1 : 0);
		return ret;
	}

	dev_emerg(hw->dev,
		  "rev42-aiw: RESET_ST=0x%08x RESET_CTL=0x%08x IOCTL=0x%08x ai_iscoreup=%d\n",
		  res.reset_st, res.reset_ctl, res.ioctl,
		  res.core_up ? 1 : 0);
	/*
	 * Explicit core-state observation. D11_CORE_IN_RESET is an expected
	 * diagnostic RESULT, not an error: the experiment exists to record
	 * what the wrapper reports while the core is held in reset. Neither
	 * token authorises a functional-core read.
	 */
	dev_emerg(hw->dev, "rev42-aiw: core_state=%s\n",
		  res.core_up ? OB_AIW_TOKEN_CORE_UP :
				OB_AIW_TOKEN_CORE_IN_RESET);
	dev_emerg(hw->dev,
		  "rev42-aiw: classification %s\n",
		  res.core_up ? "WRAPPER_ONLY_REACHABLE_CORE_UP" :
				"WRAPPER_ONLY_REACHABLE_CORE_IN_RESET");
	dev_emerg(hw->dev,
		  "rev42-aiw: fault-latch=%d (0 = no device-loss latch was set during this diagnostic)\n",
		  hw->dev_lost ? 1 : 0);
	return 0;
}

/* ---- RETIRED: rev42_ai_probe_only (quarantined, Phase 36Z) -------------- *
 *
 * The historical four-read sequence (three wrapper reads, the D11 core-window
 * read at 0x0FFC, then two PCI config readbacks) is DELETED, not disabled by
 * a flag. The option itself is refused in ob_probe() before any allocation,
 * MMIO or BCMA access; this entry point is the second, independent barrier and
 * remains only because the documentation/wiring checks pin the declared entry
 * point. It logs and returns -EOPNOTSUPP: it never reads hw->core, never calls
 * an accessor, and cannot be made to access hardware by a parameter value.
 */
int ob_ai_probe_test(struct ob_hw *hw)
{
	if (!hw || !hw->dev)
		return -ENODEV;
	dev_err(hw->dev,
		"rev42-ai: RETIRED after the live PCIe failure (Phase 36Z); this diagnostic performs no register access and returns -EOPNOTSUPP. Use rev42_ai_wrapper_only=1.\n");
	return -EOPNOTSUPP;
}

/* ------------------------------------------------------------------------- *
 * rev42_ai_enable_only: the bounded AI-wrapper core-enable transition.
 *
 * This is the only rev42 mode that WRITES a register. The permitted writes are
 * transcribed from upstream Linux drivers/bcma/core.c:bcma_core_enable() with
 * flags = 0, land in the BCMA WRAPPER window (core->wrap) through
 * ob_axi_write32() -> bcma_awrite32(), and are enumerated ONE CALLBACK PER
 * PERMITTED OPERATION in ob_ai_enable_only.h. There is no generic
 * "read/write this offset" callback, so a new register cannot be reached
 * without editing the contract.
 *
 * The sequencing, classification and lifecycle policy live in that pure header
 * and are executed by the compiled host tests. Everything below is the kernel
 * adapter: BCMA-owned accessors, dev_emerg formatting, and the retention flag
 * consumed by ob_probe() and ob_diag_remove().
 *
 * Phase 37B D4: the 0x0FFC D11 functional-core read is GONE from this mode. It
 * stops at the wrapper boundary whether the core comes up or not, and no
 * firmware / PHY / radio / PLL / calibration / DMA / IRQ / mac80211 path is
 * reachable from here in any branch.
 *
 * The engine's offsets are tied to the reviewed wrapper constants at compile
 * time, so a divergence is a build failure rather than a silent wrong-window
 * access.
 * ------------------------------------------------------------------------- */

_Static_assert(OB_AIE_OFF_IOCTL == OB_DIAG_AI_IOCTL,
	       "enable engine IOCTL offset must be BCMA_IOCTL 0x0408");
_Static_assert(OB_AIE_OFF_RESET_CTL == OB_DIAG_AI_RESET_CTL,
	       "enable engine RESET_CTL offset must be BCMA_RESET_CTL 0x0800");
_Static_assert(OB_AIE_OFF_RESET_ST == OB_DIAG_AI_RESET_ST,
	       "enable engine RESET_ST offset must be BCMA_RESET_ST 0x0804");

/*
 * Adapter context shared by every wrapper callback. `begin_note` only selects
 * the human-readable BEGIN suffix, so the SAME engine serves the wrapper-only
 * mode and the wrapper+one-read mode without duplicating the sequence.
 */
struct ob_aie_dev {
	struct ob_hw *hw;
	const char *begin_note;
};

static int ob_aie_read_reset_st(void *ctx, u32 *val)
{
	struct ob_aie_dev *d = ctx;
	struct ob_hw *hw = d ? d->hw : NULL;

	if (!hw || !hw->core || !val)
		return -ENODEV;
	*val = ob_axi_read32(hw, OB_DIAG_AI_RESET_ST);
	return hw->dev_lost ? -EIO : 0;
}

static int ob_aie_read_reset_ctl(void *ctx, u32 *val)
{
	struct ob_aie_dev *d = ctx;
	struct ob_hw *hw = d ? d->hw : NULL;

	if (!hw || !hw->core || !val)
		return -ENODEV;
	*val = ob_axi_read32(hw, OB_DIAG_AI_RESET_CTL);
	return hw->dev_lost ? -EIO : 0;
}

static int ob_aie_read_ioctl(void *ctx, u32 *val)
{
	struct ob_aie_dev *d = ctx;
	struct ob_hw *hw = d ? d->hw : NULL;

	if (!hw || !hw->core || !val)
		return -ENODEV;
	*val = ob_axi_read32(hw, OB_DIAG_AI_IOCTL);
	return hw->dev_lost ? -EIO : 0;
}

static int ob_aie_write_ioctl_set_fgc(void *ctx)
{
	struct ob_aie_dev *d = ctx;
	struct ob_hw *hw = d ? d->hw : NULL;

	if (!hw || !hw->core)
		return -ENODEV;
	ob_axi_write32(hw, OB_DIAG_AI_IOCTL, OB_AIE_E1_IOCTL_VALUE);
	/* ob_axi_write32() is void: a dropped write is only observable through
	 * the latch it was dropped for, and that must stop the sequence. */
	return hw->dev_lost ? -EIO : 0;
}

static int ob_aie_write_reset_ctl_deassert(void *ctx)
{
	struct ob_aie_dev *d = ctx;
	struct ob_hw *hw = d ? d->hw : NULL;

	if (!hw || !hw->core)
		return -ENODEV;
	ob_axi_write32(hw, OB_DIAG_AI_RESET_CTL, OB_AIE_E2_RCTL_VALUE);
	return hw->dev_lost ? -EIO : 0;
}

static int ob_aie_write_ioctl_clr_fgc(void *ctx)
{
	struct ob_aie_dev *d = ctx;
	struct ob_hw *hw = d ? d->hw : NULL;

	if (!hw || !hw->core)
		return -ENODEV;
	ob_axi_write32(hw, OB_DIAG_AI_IOCTL, OB_AIE_E3_IOCTL_VALUE);
	return hw->dev_lost ? -EIO : 0;
}

static void ob_aie_delay(void *ctx, unsigned int us)
{
	(void)ctx;
	udelay(us);
}

static bool ob_aie_dev_lost_bcma(void *ctx)
{
	struct ob_aie_dev *d = ctx;

	return d && d->hw && d->hw->dev_lost;
}

static const char *ob_aie_step_name(u8 step)
{
	switch ((enum ob_aie_step)step) {
	case OB_AIE_STEP_ADMISSION:
		return "admission";
	case OB_AIE_STEP_PRE_RESET_ST:
		return "PRE_RESET_ST";
	case OB_AIE_STEP_PRE_RESET_CTL:
		return "PRE_RESET_CTL";
	case OB_AIE_STEP_PRE_IOCTL:
		return "PRE_IOCTL";
	case OB_AIE_STEP_E1:
		return "E1_IOCTL_SET_FGC";
	case OB_AIE_STEP_E2:
		return "E2_RESET_CTL_DEASSERT";
	case OB_AIE_STEP_E3:
		return "E3_IOCTL_CLR_FGC";
	default:
		return "unknown";
	}
}

static void ob_aie_log_dev(void *ctx, enum ob_aie_event ev,
			   const struct ob_aie_log *rec)
{
	struct ob_aie_dev *d = ctx;
	struct ob_hw *hw = d ? d->hw : NULL;

	if (!hw || !hw->dev)
		return;

	switch (ev) {
	case OB_AIE_EV_BEGIN:
		/* The BCMA-enumerated windows are named here for the same reason
		 * Phase 37A named them: the addressing must come from the bus
		 * device, never from a reconstructed backplane constant. */
		dev_emerg(hw->dev,
			  "rev42-ai-en: BEGIN (AI wrapper only; BCMA-owned addressing; core->addr=0x%08x core->wrap=0x%08x; %s)\n",
			  hw->core ? hw->core->addr : 0,
			  hw->core ? hw->core->wrap : 0,
			  d->begin_note ? d->begin_note :
			  "no D11 functional-core access in this mode");
		break;
	case OB_AIE_EV_PRE_READ:
		dev_emerg(hw->dev,
			  "rev42-ai-en: PRE  seq=%u step=%s op=BCMA_AREAD32 off=0x%04x window=core->wrap\n",
			  (unsigned int)rec->seq, ob_aie_step_name(rec->seq),
			  rec->off);
		break;
	case OB_AIE_EV_POST_READ:
		dev_emerg(hw->dev,
			  "rev42-ai-en: POST seq=%u step=%s op=BCMA_AREAD32 status=ok readback=0x%08x\n",
			  (unsigned int)rec->seq, ob_aie_step_name(rec->seq),
			  rec->value);
		break;
	case OB_AIE_EV_PRE_WRITE:
		dev_emerg(hw->dev,
			  "rev42-ai-en: PRE  seq=%u step=%s op=BCMA_AWRITE32 off=0x%04x value=0x%08x window=core->wrap\n",
			  (unsigned int)rec->seq, ob_aie_step_name(rec->seq),
			  rec->off, rec->value);
		break;
	case OB_AIE_EV_POST_WRITE:
		/* Reached only when the documented readback matched the
		 * acceptance mask. An accessor returning is never enough. */
		dev_emerg(hw->dev,
			  "rev42-ai-en: POST seq=%u step=%s op=BCMA_AWRITE32 status=confirmed wrote=0x%08x readback=0x%08x\n",
			  (unsigned int)rec->seq, ob_aie_step_name(rec->seq),
			  rec->written, rec->value);
		break;
	case OB_AIE_EV_DELAY:
		dev_emerg(hw->dev, "rev42-ai-en: DELAY seq=%u step=%s udelay=%u\n",
			  (unsigned int)rec->seq, ob_aie_step_name(rec->seq),
			  rec->value);
		break;
	case OB_AIE_EV_STOP:
		dev_emerg(hw->dev,
			  "rev42-ai-en: STOP seq=%u step=%s reason=%s error=%d (no further register access)\n",
			  (unsigned int)rec->seq, ob_aie_step_name(rec->seq),
			  rec->reason ? rec->reason : "unspecified", rec->error);
		break;
	case OB_AIE_EV_END:
		break;
	}
}

/*
 * Stage B1 of Phase 38A (and the whole of Phase 37B): the ONE wrapper-enable
 * implementation. The sequence, its constants, its guards and its failure
 * semantics are the engine in ob_ai_enable_only.h, called exactly once here.
 *
 * The verified engine result is published in hw->ai_enable_last so a caller can
 * gate a later stage on the ACTUAL classification and counters rather than on
 * an `insmod` exit status (Phase 38A stage B2). Publishing it changes no
 * behaviour of this function.
 */
int ob_ai_enable_test(struct ob_hw *hw)
{
	struct ob_aie_ops ops;
	struct ob_aie_dev dev_ctx;
	struct ob_aie_result res;
	enum ob_aie_remove_policy policy;
	int ret;

	ob_aie_result_init(&res);

	if (!hw || !hw->dev) {
		res.class = OB_AIE_CLASS_PRECHECK_REJECTED;
		res.stop_reason = "missing_device";
		res.last_error = -ENODEV;
		if (hw)
			hw->ai_enable_last = res;
		return -ENODEV;
	}

	/* A missing core/bus is a contract defect, not a device state: it must
	 * never be reported as a device-loss result. Zero callbacks below. */
	if (!hw->core || !hw->bus) {
		dev_err(hw->dev,
			"rev42-ai-en: prerequisite missing (core=%d bus=%d); no register access\n",
			hw->core ? 1 : 0, hw->bus ? 1 : 0);
		res.stop_reason = "prerequisite_missing";
		res.last_error = -ENODEV;
		hw->ai_enable_last = res;
		return -ENODEV;
	}

	/* Monotonic device-loss gate: no D11/BCMA/window access below. */
	if (!ob_diag_ai_may_access(hw->dev_lost)) {
		dev_emerg(hw->dev,
			  "rev42-ai-en: SKIPPED (dev_lost latched; no D11/BCMA/window access)\n");
		hw->ai_enable_retain = true;
		res.class = OB_AIE_CLASS_DEVICE_LOSS_SUSPECTED;
		res.device_lost = true;
		res.stop_reason = "device_loss_latched";
		res.last_error = -EIO;
		hw->ai_enable_last = res;
		return -EIO;
	}

	dev_ctx.hw = hw;
	dev_ctx.begin_note =
		"this stage performs no D11 functional-core access";
	ops.ctx = &dev_ctx;
	ops.read_reset_st = ob_aie_read_reset_st;
	ops.read_reset_ctl = ob_aie_read_reset_ctl;
	ops.read_ioctl = ob_aie_read_ioctl;
	ops.write_ioctl_set_fgc = ob_aie_write_ioctl_set_fgc;
	ops.write_reset_ctl_deassert = ob_aie_write_reset_ctl_deassert;
	ops.write_ioctl_clr_fgc = ob_aie_write_ioctl_clr_fgc;
	ops.delay_us = ob_aie_delay;
	ops.log = ob_aie_log_dev;
	ops.dev_lost = ob_aie_dev_lost_bcma;

	ret = ob_aie_run(&ops, hw->dev_lost, &res);

	/* An all-ones wrapper readback is a device-loss indication for these
	 * three known control/status registers. Latch it so the central guard
	 * blocks every later MMIO, and let ob_probe() apply the existing
	 * retention rule (keep the device bound). */
	if (res.device_lost && !hw->dev_lost) {
		hw->dev_lost = true;
		dev_crit(hw->dev,
			 "rev42-ai-en: all-ones on a wrapper control/status register; device-loss latch set, device retained, no cleanup attempted\n");
	}

	if (res.writes_attempted > 0 || res.prestate_ok) {
		dev_emerg(hw->dev,
			  "rev42-ai-en: pre RESET_ST=0x%08x RESET_CTL=0x%08x IOCTL=0x%08x\n",
			  res.reset_st, res.reset_ctl, res.ioctl);
		dev_emerg(hw->dev,
			  "rev42-ai-en: post IOCTL_E1=0x%08x RESET_CTL_E2=0x%08x IOCTL_E3=0x%08x ai_iscoreup=%d\n",
			  res.e1_ioctl, res.e2_reset_ctl, res.e3_ioctl,
			  res.core_up ? 1 : 0);
	}

	/* Counters are reported separately on every path: an accessor that was
	 * invoked and failed still counts as an attempt, and a write is only
	 * confirmed when its documented readback matched. */
	dev_emerg(hw->dev,
		  "rev42-ai-en: counters reads_attempted=%u reads_completed=%u writes_attempted=%u writes_confirmed=%u (max %u reads / %u writes)\n",
		  (unsigned int)res.reads_attempted,
		  (unsigned int)res.reads_completed,
		  (unsigned int)res.writes_attempted,
		  (unsigned int)res.writes_confirmed,
		  (unsigned int)OB_AIE_MAX_READS,
		  (unsigned int)OB_AIE_MAX_WRITES);

	if (res.class != OB_AIE_CLASS_CORE_UP) {
		dev_crit(hw->dev,
			 "rev42-ai-en: %s step=%s op=%s off=0x%04x reason=%s last_value=0x%08x error=%d\n",
			 ob_aie_class_token(res.class),
			 ob_aie_step_name(res.failed_step),
			 res.failed_op == OB_AIE_OP_WRITE ? "write" :
			 res.failed_op == OB_AIE_OP_READ ? "read" : "none",
			 res.failed_off, res.stop_reason ? res.stop_reason :
			 "unspecified", res.last_value, res.last_error);
	} else {
		dev_emerg(hw->dev,
			  "rev42-ai-en: %s validated on the documented readbacks (wrapper predicate only)\n",
			  ob_aie_class_token(res.class));
	}

	/* Ownership policy. Anything after the first write attempt retains the
	 * device: there is no teardown for a post-write state, and no cleanup
	 * write is ever issued on a failure path. */
	policy = ob_aie_remove_policy_of(&res);
	hw->ai_enable_retain = (policy == OB_AIE_REMOVE_RETAIN);
	hw->ai_enable_core_up = (res.class == OB_AIE_CLASS_CORE_UP);

	dev_emerg(hw->dev, "rev42-ai-en: classification %s\n",
		  ob_aie_class_token(res.class));
	dev_emerg(hw->dev,
		  "rev42-ai-en: remove-policy=%s (RETAIN = device kept bound, no teardown, reboot to recover)\n",
		  policy == OB_AIE_REMOVE_RETAIN ? "RETAIN" : "CLEAN");
	/* Spelled "fault-latch" on purpose: a clean run must not trip the
	 * harness FAULT_RE. */
	dev_emerg(hw->dev,
		  "rev42-ai-en: fault-latch=%d (0 = no device-loss latch was set during this mode)\n",
		  hw->dev_lost ? 1 : 0);

	dev_emerg(hw->dev,
		  "rev42-ai-en: END rev42_ai_enable_only %s - wrapper writes only (IOCTL x2, RESET_CTL); no D11 functional-core access in any branch; no PHY/radio/PLL/cal/DMA/IRQ/mac80211\n",
		  ob_aie_class_token(res.class));

	hw->ai_enable_last = res;

	/*
	 * A write-free failure is safe to hand back to ob_probe() as a probe
	 * failure: nothing was modified. Anything after the first write
	 * attempt reports success so ob_probe() returns without releasing the
	 * device or falling through to any other stage.
	 */
	if (res.writes_attempted == 0)
		return ret ? ret : 0;
	return 0;
}

/*
 * ------------------------------------------------------------------------- *
 * rev42_d11_maccontrol_once (Phase 38A) — kernel adapter.
 *
 * Stage B1 is ob_ai_enable_test() above, the single Phase 37B implementation.
 * Stage B2/B3/B4 are the pure contract in ob_d11_maccontrol_once.h. The only
 * hardware callback it can reach is ONE read of D11 MACCONTROL (0x0120, 32
 * bits) through the trusted guarded accessor, i.e. through BCMA's functional
 * core window (core->addr), NOT the AI wrapper window (core->wrap).
 *
 * There is no functional write, no second functional read, no PHY/radio/PLL/
 * firmware/DMA/IRQ/SHM/mac80211 access and no cleanup write in any branch.
 * ------------------------------------------------------------------------- */

static int ob_d11m_read_maccontrol(void *ctx, u32 *val)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->core || !val)
		return -ENODEV;
	/*
	 * ob_d11_read32_trusted() latches device loss itself when the functional
	 * window returns all-ones. That latch is the CONTRACT's classification
	 * input, so this callback records the value and returns 0: turning the
	 * latch into a callback error would report UNCERTAIN instead of the
	 * required WINDOW_INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED.
	 */
	*val = ob_d11_read32_trusted(hw, "rev42-d11-mc:MACCONTROL+0x0120",
				     OB_D11M_OFF_MACCONTROL);
	return 0;
}

static bool ob_d11m_dev_lost(void *ctx)
{
	struct ob_hw *hw = ctx;

	return hw && hw->dev_lost;
}

static void ob_d11m_log_dev(void *ctx, enum ob_d11m_event ev,
			    const struct ob_d11m_log *rec)
{
	struct ob_hw *hw = ctx;

	if (!hw || !hw->dev)
		return;

	switch (ev) {
	case OB_D11M_EV_BEGIN:
		dev_emerg(hw->dev,
			  "rev42-d11-mc: BEGIN (single functional read; BCMA-owned addressing; core->addr=0x%08x core->wrap=0x%08x)\n",
			  hw->core ? hw->core->addr : 0,
			  hw->core ? hw->core->wrap : 0);
		break;
	case OB_D11M_EV_GATE:
		dev_emerg(hw->dev,
			  "rev42-d11-mc: GATE functional_access=%s reason=%s\n",
			  (rec && rec->gate_ok) ? "ADMITTED" : "REFUSED",
			  rec && rec->reason ? rec->reason : "unspecified");
		break;
	case OB_D11M_EV_PRE_READ:
		dev_emerg(hw->dev,
			  "rev42-d11-mc: PRE  reg=D11_MACCONTROL off=0x%04x width=32 window=core->addr op=BCMA_READ32 count=1/1\n",
			  rec ? rec->off : OB_D11M_OFF_MACCONTROL);
		break;
	case OB_D11M_EV_POST_READ: {
		/*
		 * The line is formatted by the SAME function the host tests
		 * exercise (ob_d11m_format_post_read): it says
		 * status=pending_classification, never "ok", because this event
		 * is emitted before the value and the device-loss latch are
		 * classified. An all-ones value reaches this point on its way to
		 * D11_WINDOW_INACCESSIBLE_OR_DEVICE_LOSS_SUSPECTED.
		 */
		char line[160];

		if (ob_d11m_format_post_read(line, sizeof(line),
					     rec ? rec->value : 0) < 0)
			break;
		dev_emerg(hw->dev, "%s", line);
		break;
	}
	case OB_D11M_EV_STOP:
		dev_emerg(hw->dev,
			  "rev42-d11-mc: STOP reason=%s reads_attempted=%u reads_completed=%u (no further register access)\n",
			  rec && rec->reason ? rec->reason : "unspecified",
			  rec ? rec->reads_attempted : 0,
			  rec ? rec->reads_completed : 0);
		break;
	case OB_D11M_EV_END:
		dev_emerg(hw->dev,
			  "rev42-d11-mc: END single functional read complete; no further register access in this mode\n");
		break;
	}
}

int ob_d11_maccontrol_once_test(struct ob_hw *hw)
{
	struct ob_aie_result wrapper;
	struct ob_d11m_result res;
	struct ob_d11m_ops ops;
	int wret;

	if (!hw || !hw->dev || !hw->core || !hw->bus) {
		if (hw && hw->dev)
			dev_err(hw->dev,
				"rev42-d11-mc: ADMISSION_REJECTED prerequisite missing (dev=%d core=%d bus=%d); no register access\n",
				hw->dev ? 1 : 0, hw->core ? 1 : 0,
				hw->bus ? 1 : 0);
		return -ENODEV;
	}

	/*
	 * Stage B0. A latched device-loss or fatal lifecycle state is an
	 * admission failure: no wrapper read, no functional read, and the device
	 * stays exactly as the earlier stage left it.
	 */
	if (hw->dev_lost || hw->d3a0.lc.fatal) {
		dev_err(hw->dev,
			"rev42-d11-mc: ADMISSION_REJECTED reason=%s; no register access, device retained as-is\n",
			hw->dev_lost ? "device_loss_latched" :
			"fatal_lifecycle_state");
		return 0;
	}

	/* Stage B1: the ONE Phase 37B wrapper-enable implementation. */
	wret = ob_ai_enable_test(hw);
	wrapper = hw->ai_enable_last;
	dev_emerg(hw->dev,
		  "rev42-ai-en: END rev42_d11_maccontrol_once/stage-B1 %s - wrapper writes only (IOCTL x2, RESET_CTL); no D11 functional-core access in stage B1; the single MACCONTROL read is stage B3\n",
		  ob_aie_class_token(wrapper.class));

	/* Stage B2..B4: gate on the VERIFIED result, one read, classify, stop. */
	ops.ctx = hw;
	ops.read_maccontrol = ob_d11m_read_maccontrol;
	ops.log = ob_d11m_log_dev;
	ops.dev_lost = ob_d11m_dev_lost;

	ob_d11m_run(&ops, &wrapper, hw->dev_lost, hw->d3a0.lc.fatal, &res);

	dev_emerg(hw->dev,
		  "rev42-d11-mc: wrapper engine ret=%d class=%s counters reads=%u/%u writes=%u/%u\n",
		  wret, ob_aie_class_token(wrapper.class),
		  (unsigned int)wrapper.reads_attempted,
		  (unsigned int)wrapper.reads_completed,
		  (unsigned int)wrapper.writes_attempted,
		  (unsigned int)wrapper.writes_confirmed);
	dev_emerg(hw->dev,
		  "rev42-d11-mc: functional reads_attempted=%u reads_completed=%u writes=%u value=%s0x%08x\n",
		  (unsigned int)res.reads_attempted,
		  (unsigned int)res.reads_completed,
		  (unsigned int)res.writes_attempted,
		  res.have_value ? "" : "unobserved/",
		  res.have_value ? res.value : 0);
	dev_emerg(hw->dev, "rev42-d11-mc: classification %s\n",
		  ob_d11m_class_token(res.class));
	dev_emerg(hw->dev,
		  "rev42-d11-mc: remove-policy=%s (RETAIN = device kept bound, no teardown, reboot to recover)\n",
		  ob_d11m_retained(&wrapper) ? "RETAIN" : "CLEAN");
	dev_emerg(hw->dev,
		  "rev42-d11-mc: fault-latch=%d (0 = no device-loss latch was set during this mode)\n",
		  hw->dev_lost ? 1 : 0);
	dev_emerg(hw->dev,
		  "rev42-d11-mc: END rev42_d11_maccontrol_once %s - %s\n",
		  ob_d11m_class_token(res.class),
		  res.class == OB_D11M_CLASS_READABLE ?
		  "one readable D11 functional register; NOT firmware, PHY, radio, DMA, IRQ or MAC operation" :
		  "stopped before any further register access");

	/*
	 * Post-write retention: after any wrapper write attempt there is no
	 * teardown contract, so report success and keep the device bound. A
	 * write-free wrapper rejection is still a safe probe failure.
	 */
	if (wrapper.writes_attempted == 0)
		return wret ? wret : 0;
	hw->ai_enable_retain = true;
	return 0;
}

/* ---- structured per-operation trace (parsed offline by a host script) ---- */

static void ob_diag_op(struct ob_hw *hw, const char *phase, const char *fn,
		       const char *block, u32 off, u32 width,
		       const char *wr, const char *rd,
		       const char *result, const char *cp)
{
	dev_info(hw->dev,
		 "rev42-diag-op: {\"seq\":%u,\"phase\":\"%s\",\"function\":\"%s\",\"block\":\"%s\",\"offset\":%u,\"width\":%u,\"value_written\":\"%s\",\"value_read\":\"%s\",\"result\":\"%s\",\"checkpoint\":\"%s\",\"device_alive\":%d}\n",
		 hw->diag.seq++, phase, fn, block, off, width, wr, rd, result,
		 cp, hw->dev_lost ? 0 : 1);
}

static void ob_diag_op_read32(struct ob_hw *hw, const char *phase,
			      const char *fn, u16 off, u32 val,
			      const char *cp)
{
	char rd[16];

	scnprintf(rd, sizeof(rd), "0x%08x", val);
	ob_diag_op(hw, phase, fn, "D11", off, 32, "none", rd,
		   hw->dev_lost ? "dev_lost" : "ok", cp);
}

/* ---- core preparation (identical minimal set to ob_radio_prepare) ------- */

static int ob_diag_prepare(struct ob_hw *hw)
{
	u32 clk, ioc;
	char v[16];
	int err;

	if (hw->dev_lost)
		return -EIO;

	dev_info(hw->dev, "rev42-diag: prep host up (host_is_pcie2=%d)\n",
		 hw->bus->host_is_pcie2 ? 1 : 0);
	bcma_host_pci_up(hw->bus);
	ob_diag_op(hw, "prep", "bcma_host_pci_up", "PCI", 0, 0, "host-up",
		   "none", "ok", "CP0");

	if (!bcma_core_is_enabled(hw->core)) {
		err = bcma_core_enable(hw->core, 0);
		if (err) {
			dev_err(hw->dev,
				"rev42-diag: D11 core enable failed: %d\n", err);
			return err;
		}
	}
	ob_diag_op(hw, "prep", "bcma_core_enable", "D11", 0, 0, "enable",
		   "enabled", "ok", "CP0");

	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);
	/*
	 * Raw observation of D11+0x1e0 only. The old "clkctlst=/HAVEHT=" label
	 * was false: HAVEHT is bit17 of the ChipCommon CLKCTLST, a different
	 * register in a different window, and the vendor never manipulates bit17
	 * on the D11 one.
	 */
	clk = ob_d11_read32(hw, OB_UCODE_REG_D11_CLK1E0);
	scnprintf(v, sizeof(v), "0x%08x", clk);
	ob_diag_op(hw, "prep", "d11_read_1e0", "D11",
		   OB_UCODE_REG_D11_CLK1E0, 32, "none", v, "ok", "CP0");
	dev_info(hw->dev,
		 "rev42-diag: d11_clk1e0=%08x bit1=%d core_enabled=%d\n",
		 clk, !!(clk & OB_UCODE_D11_CLK1E0_BIT1),
		 bcma_core_is_enabled(hw->core) ? 1 : 0);

	/* D11 MAC/PHY clock gate (vendor wlc_bmac_macphyclk_set(1)). */
	ioc = ob_axi_read32(hw, BCMA_IOCTL);
	ob_axi_write32(hw, BCMA_IOCTL, ioc | OB_D3A0_IOCTL_MPCLKE);
	ioc = ob_axi_read32(hw, BCMA_IOCTL);
	scnprintf(v, sizeof(v), "0x%08x", ioc);
	ob_diag_op(hw, "prep", "wlc_bmac_macphyclk_set", "SI", BCMA_IOCTL, 32,
		   "MPCLKE", v, "ok", "CP0");
	dev_info(hw->dev, "rev42-diag: ioctl=%08x mpclke=%d\n",
		 ioc, !!(ioc & OB_D3A0_IOCTL_MPCLKE) ? 1 : 0);
	return 0;
}

/* ---- checkpoints -------------------------------------------------------- */

static void ob_diag_checkpoint(struct ob_hw *hw, int n, const char *name)
{
	struct ob_diag_cp *cp = &hw->diag.cp[n];
	char cpn[8], rd[16];

	scnprintf(cpn, sizeof(cpn), "CP%d", n);
	cp->reached = true;

	/* Trusted direct read: an all-ones value latches hw->dev_lost. */
	cp->sentinel = ob_d11_read32_trusted(hw, name, OB_DIAG_REG_MACCONTROL);
	if (hw->dev_lost || ob_diag_sentinel_lost(cp->sentinel)) {
		hw->dev_lost = true;
		cp->dev_lost = true;
		scnprintf(rd, sizeof(rd), "0x%08x", cp->sentinel);
		ob_diag_op(hw, "checkpoint", "checkpoint", "D11",
			   OB_DIAG_REG_MACCONTROL, 32, "none", rd, "dev_lost",
			   cpn);
		dev_crit(hw->dev,
			 "rev42-diag: %s DEVICE LOST sentinel=%s; no further MMIO\n",
			 cpn, rd);
		return;
	}

	cp->maccontrol = cp->sentinel;
	cp->macintstatus = ob_d11_read32(hw, OB_DIAG_REG_MACINTSTATUS);
	cp->macintmask = ob_d11_read32(hw, OB_DIAG_REG_MACINTMASK);
	cp->dev_lost = hw->dev_lost;
	dev_info(hw->dev,
		 "rev42-diag: %s %s alive=1 maccontrol=%08x macintstatus=%08x macintmask=%08x\n",
		 cpn, name, cp->maccontrol, cp->macintstatus, cp->macintmask);
	ob_diag_op_read32(hw, "checkpoint", "checkpoint",
			  OB_DIAG_REG_MACCONTROL, cp->maccontrol, cpn);
}

/* ---- PHY prerequisite: wlc_phy_anacore(pi,1) direct side ---------------- */

static void ob_diag_anacore_side(struct ob_hw *hw)
{
	char rd[16];

	ob_d11_write16(hw, OB_DIAG_REG_PHY_ANALOG, OB_DIAG_PHY_ANALOG_ON);
	ob_diag_op(hw, "phy", "wlc_phy_anacore", "PHY",
		   OB_DIAG_REG_PHY_ANALOG, 16, "0x0000", "none",
		   hw->dev_lost ? "dev_lost" : "ok", "CP4");
	if (hw->dev_lost)
		return;

	hw->diag.phy_ver = ob_d11_read16(hw, OB_DIAG_REG_PHY_VER);
	scnprintf(rd, sizeof(rd), "0x%04x", hw->diag.phy_ver);
	ob_diag_op(hw, "phy", "phyversion", "PHY", OB_DIAG_REG_PHY_VER, 16,
		   "none", rd, hw->dev_lost ? "dev_lost" : "ok", "CP4");
	hw->diag.anacore_side_ran = true;
}

/* ---- radio window read + vendor-faithful decode ------------------------- */

static struct ob_diag_radio ob_diag_radio_read(struct ob_hw *hw,
					       const char *phase,
					       const char *cp, bool *ran)
{
	u16 r0 = 0, r1 = 0;
	char rd[16];

	*ran = false;

	ob_d11_write16(hw, OB_DIAG_REG_RADIO_ADDR, 0);
	ob_diag_op(hw, phase, "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_ADDR, 16, "0x0000", "none",
		   hw->dev_lost ? "dev_lost" : "ok", cp);
	if (hw->dev_lost)
		return ob_diag_vendor_decode(0, 0);

	r0 = ob_d11_read16(hw, OB_DIAG_REG_RADIO_DATA);
	scnprintf(rd, sizeof(rd), "0x%04x", r0);
	ob_diag_op(hw, phase, "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_DATA, 16, "none", rd,
		   hw->dev_lost ? "dev_lost" : "ok", cp);
	if (hw->dev_lost)
		return ob_diag_vendor_decode(0, 0);

	ob_d11_write16(hw, OB_DIAG_REG_RADIO_ADDR, 1);
	ob_diag_op(hw, phase, "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_ADDR, 16, "0x0001", "none",
		   hw->dev_lost ? "dev_lost" : "ok", cp);
	if (hw->dev_lost)
		return ob_diag_vendor_decode(0, 0);

	r1 = ob_d11_read16(hw, OB_DIAG_REG_RADIO_DATA);
	scnprintf(rd, sizeof(rd), "0x%04x", r1);
	ob_diag_op(hw, phase, "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_DATA, 16, "none", rd,
		   hw->dev_lost ? "dev_lost" : "ok", cp);

	*ran = !hw->dev_lost;
	return ob_diag_vendor_decode(r0, r1);
}

static void ob_diag_radio_log(struct ob_hw *hw, const char *tag,
			      const struct ob_diag_radio *d)
{
	dev_info(hw->dev,
		 "rev42-diag: %s r0=0x%04x r1=0x%04x id32=0x%08x id_byte=0x%02x low_nibble=0x%x low_byte=0x%02x pa_r1=0x%04x valid=%d all_ones=%d all_zero=%d family=%d\n",
		 tag, d->r0, d->r1, d->id32, d->id_byte, d->low_nibble,
		 d->low_byte, d->pa_r1, d->valid ? 1 : 0,
		 d->all_ones ? 1 : 0, d->all_zero ? 1 : 0,
		 d->id_in_family ? 1 : 0);
	dev_info(hw->dev,
		 "rev42-diag: %s reconstruct r0_from_fields=0x%04x (id_byte<<4|low_nibble)\n",
		 tag, ob_diag_r0_from_fields(d->id_byte, d->low_nibble));
}

/* ---- top level ---------------------------------------------------------- */

int ob_diag_test(struct ob_hw *hw)
{
	struct ob_diag *st = &hw->diag;
	struct ob_ucode_run run;
	struct ob_initvals_post post;
	int ret;

	memset(st, 0, sizeof(*st));
	st->full = hw->rev42_diag_full;

	if (hw->core->id.rev != OB_DIAG_PHYREV_REV42) {
		dev_err(hw->dev,
			"rev42-diag: unsupported D11 core rev %u (need %u)\n",
			hw->core->id.rev, OB_DIAG_PHYREV_REV42);
		return -ENOTSUPP;
	}
	if (hw->dev_lost) {
		dev_err(hw->dev, "rev42-diag: refusing: dev_lost already latched\n");
		return -EIO;
	}

	dev_info(hw->dev,
		 "rev42-diag: BEGIN full=%d (radio-ID milestone; no band/PHY init/radio config/EN_MAC/DMA/IRQ/mac80211)\n",
		 st->full ? 1 : 0);
	ob_diag_op(hw, "start", "ob_diag_test", "-", 0, 0, "none", "none",
		   "ok", "CP0");

	ret = ob_diag_prepare(hw);
	if (ret)
		goto fail;

	ob_diag_checkpoint(hw, 0, "pre-ucode");
	if (hw->dev_lost) {
		ret = -EIO;
		goto fail;
	}

	/* Core-only radio baseline (the vendor-minimal premise). */
	st->pre = ob_diag_radio_read(hw, "radio_id_pre", "CP0",
				     &st->pre_read_ran);
	ob_diag_radio_log(hw, "core-only", &st->pre);
	if (hw->dev_lost) {
		ret = -EIO;
		goto fail;
	}

	/* Optional staged path: D2A ucode + D2B common initvals for CP1..CP3. */
	if (st->full) {
		ret = ob_ucode_run_d2a(hw, "rev42-diag", &run);
		if (ret)
			goto fail;
		ob_diag_op(hw, "ucode", "ob_ucode_run_d2a", "OBJ",
			   OB_UCODE_REG_OBJDATA, 32, "ucode-stream", "done",
			   "ok", "CP1");
		ob_diag_checkpoint(hw, 1, "post-ucode");
		if (hw->dev_lost) {
			ret = -EIO;
			goto fail;
		}

		ret = ob_initvals_apply_common(hw, "rev42-diag", &post);
		if (ret)
			goto fail;
		ob_diag_op(hw, "initvals", "ob_initvals_apply_common", "D11",
			   0, 0, "610-records", "done", "ok", "CP2");
		ob_diag_checkpoint(hw, 2, "post-common-initvals");
		if (hw->dev_lost) {
			ret = -EIO;
			goto fail;
		}
		ob_diag_checkpoint(hw, 3, "psm-mac-state");
		if (hw->dev_lost) {
			ret = -EIO;
			goto fail;
		}
	}

	/* PHY/radio prerequisite (CONFIRMED_VENDOR_REV42 direct anacore side). */
	ob_diag_anacore_side(hw);
	ob_diag_checkpoint(hw, 4, "phy-iface");
	if (hw->dev_lost) {
		ret = -EIO;
		goto fail;
	}

	/* Radio ID read + vendor decode. */
	st->post = ob_diag_radio_read(hw, "radio_id_post", "CP5",
				      &st->post_read_ran);
	ob_diag_radio_log(hw, "post-anacore", &st->post);
	ob_diag_checkpoint(hw, 5, "radio-id");
	if (hw->dev_lost) {
		ret = -EIO;
		goto fail;
	}
	ob_diag_checkpoint(hw, 6, "post-read");

	st->stopped_before_band = true;

	if (hw->dev_lost) {
		ret = -EIO;
		goto fail;
	}

	if (st->post_read_ran && !st->post.valid) {
		dev_err(hw->dev,
			"rev42-diag: FAIL radio window returned 0x%04x/0x%04x (all-zero/all-ones)\n",
			st->post.r0, st->post.r1);
		ret = -EIO;
		goto fail;
	}

	dev_info(hw->dev,
		 "rev42-diag: PASS radio_id_byte=0x%02x (reconstructed r0=0x%04x)\n",
		 st->post.id_byte,
		 ob_diag_r0_from_fields(st->post.id_byte, st->post.low_nibble));
	dev_info(hw->dev,
		 "rev42-diag: STOPPED BEFORE BAND INIT / PHY INIT / RADIO CONFIG\n");
	return 0;

fail:
	dev_err(hw->dev,
		"rev42-diag: FAIL ret=%d (no cleanup writes; hardware left as-is)\n",
		ret);
	return ret;
}

/* ====================================================================== */
/* Cold-attach probe (REV42-RADIO-ATTACH-SEQUENCE, YAML ops 0..27)        */
/* ====================================================================== */

static bool ob_attach_iscoreup(struct ob_hw *hw, u32 *raw)
{
	u32 v = ob_d11_read32(hw, OB_DIAG_REG_SB_CHIPCONTROL);

	if (raw)
		*raw = v;
	return (v & OB_DIAG_SICF_ISCOREUP_MASK) == OB_DIAG_SICF_ISCOREUP_VAL;
}

/*
 * Pre-enable checkpoint. On a cold core whose functional window is not yet
 * clocked, MACCONTROL legitimately reads all-ones; that is "core down", NOT
 * device loss. These checkpoints therefore use the NON-trusted read (which
 * never latches dev_lost) and never gate the run on the sentinel.
 */
static void ob_attach_checkpoint_soft(struct ob_hw *hw, int n, const char *name)
{
	struct ob_diag_cp *cp = &hw->diag.cp[n];
	char cpn[8], rd[16];

	scnprintf(cpn, sizeof(cpn), "CP%d", n);
	cp->reached = true;
	cp->sentinel = ob_d11_read32(hw, OB_DIAG_REG_MACCONTROL);
	cp->maccontrol = cp->sentinel;
	cp->macintstatus = ob_d11_read32(hw, OB_DIAG_REG_MACINTSTATUS);
	cp->macintmask = ob_d11_read32(hw, OB_DIAG_REG_MACINTMASK);
	cp->dev_lost = hw->dev_lost;
	scnprintf(rd, sizeof(rd), "0x%08x", cp->sentinel);
	dev_info(hw->dev,
		 "rev42-diag: %s %s alive=%d maccontrol=%s macintstatus=%08x macintmask=%08x (pre-enable: all-ones = core down, not loss)\n",
		 cpn, name, hw->dev_lost ? 0 : 1, rd, cp->macintstatus,
		 cp->macintmask);
	ob_diag_op(hw, "checkpoint", "checkpoint", "D11",
		   OB_DIAG_REG_MACCONTROL, 32, "none", rd,
		   hw->dev_lost ? "dev_lost" : "ok", cpn);
}

static void ob_attach_cflags(struct ob_hw *hw, const char *tag, u16 mask,
			     u16 val, const char *cp)
{
	u32 cur, new;
	char w[48], r[16];

	cur = ob_axi_read32(hw, OB_DIAG_AXI_IOCTL);
	new = (cur & ~(u32)mask) | (u32)val;
	ob_axi_write32(hw, OB_DIAG_AXI_IOCTL, new);
	cur = ob_axi_read32(hw, OB_DIAG_AXI_IOCTL);
	scnprintf(w, sizeof(w), "mask=0x%x val=0x%x (mapping_assumed)",
		  mask, val);
	scnprintf(r, sizeof(r), "0x%08x", cur);
	ob_diag_op(hw, "cflags", tag, "SI", OB_DIAG_AXI_IOCTL, 32, w, r,
		   hw->dev_lost ? "dev_lost" : "ok", cp);
}

static void ob_attach_radio_read(struct ob_hw *hw)
{
	u16 r0 = 0, r1 = 0;
	char rd[16];

	ob_d11_write16(hw, OB_DIAG_REG_RADIO_ADDR, 0);
	ob_diag_op(hw, "radio", "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_ADDR, 16, "0x0000", "none",
		   hw->dev_lost ? "dev_lost" : "ok", "CP12");
	if (hw->dev_lost)
		return;
	r0 = ob_d11_read16(hw, OB_DIAG_REG_RADIO_DATA);
	scnprintf(rd, sizeof(rd), "0x%04x", r0);
	ob_diag_op(hw, "radio", "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_DATA, 16, "none", rd,
		   hw->dev_lost ? "dev_lost" : "ok", "CP13");
	if (hw->dev_lost)
		return;

	ob_d11_write16(hw, OB_DIAG_REG_RADIO_ADDR, 1);
	ob_diag_op(hw, "radio", "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_ADDR, 16, "0x0001", "none",
		   hw->dev_lost ? "dev_lost" : "ok", "CP13");
	if (hw->dev_lost)
		return;
	r1 = ob_d11_read16(hw, OB_DIAG_REG_RADIO_DATA);
	scnprintf(rd, sizeof(rd), "0x%04x", r1);
	ob_diag_op(hw, "radio", "read_radio_reg", "RADIO",
		   OB_DIAG_REG_RADIO_DATA, 16, "none", rd,
		   hw->dev_lost ? "dev_lost" : "ok", "CP14");

	hw->diag.radio = ob_diag_vendor_decode(r0, r1);
	hw->diag.radio_ran = !hw->dev_lost;
}

int ob_attach_test(struct ob_hw *hw)
{
	struct ob_diag *st = &hw->diag;
	u32 mc_old, mc_new;
	int ret = 0;

	memset(st, 0, sizeof(*st));
	st->attach = true;

	if (hw->core->id.rev != OB_DIAG_PHYREV_REV42) {
		dev_err(hw->dev, "rev42-attach: unsupported D11 rev %u (need %u)\n",
			hw->core->id.rev, OB_DIAG_PHYREV_REV42);
		return -ENOTSUPP;
	}
	if (hw->dev_lost) {
		dev_err(hw->dev, "rev42-attach: refusing: dev_lost latched\n");
		return -EIO;
	}

	dev_info(hw->dev,
		 "rev42-attach: BEGIN (cold-attach radio sequence; no ucode/initvals/bsinitvals/DMA/RX/TX/IRQ/mac80211)\n");
	ob_diag_op(hw, "start", "ob_attach_test", "-", 0, 0, "none", "none",
		   "ok", "CP0");

	/* seq 1: host up (PCI/BCMA). */
	bcma_host_pci_up(hw->bus);
	ob_diag_op(hw, "prep", "bcma_host_pci_up", "PCI", 0, 0, "host-up",
		   "none", "ok", "CP0");
	ob_attach_checkpoint_soft(hw, 0, "pci-bcma");

	/*
	 * The D11 functional window is NOT clocked until the core is
	 * enabled/reset (below). si_iscoreup is read from the core window word;
	 * a cold/disabled core reports it false (all-ones & mask != 0x10000).
	 */
	st->iscoreup_before = ob_attach_iscoreup(hw, &st->iscoreup_raw_before);
	ob_diag_op_read32(hw, "reset", "si_iscoreup",
			  OB_DIAG_REG_SB_CHIPCONTROL,
			  st->iscoreup_raw_before, "CP1");
	dev_info(hw->dev,
		 "rev42-attach: si_iscoreup(before)=%d raw=0x%08x\n",
		 st->iscoreup_before ? 1 : 0, st->iscoreup_raw_before);
	ob_attach_checkpoint_soft(hw, 1, "pre-sb-core-reset");

	/*
	 * seq 3: read sh->(0x184) (noreset). The vendor struct is a software
	 * object OpenBRCM does not carry; a fresh cold attach (fresh module
	 * load, cold enumeration) never calls wlc_bmac_set_noreset, so the
	 * documented cold value 0 is asserted and logged. If it could be
	 * non-zero the vendor skips the whole reset and this test is void.
	 */
	dev_info(hw->dev,
		 "rev42-attach: sh->0x184 = 0 (cold attach; noreset flag; vendor software field, CONFIRMED cold value 0)\n");
	ob_diag_op(hw, "reset", "sh_0x184", "control", 0x184, 8,
		   "read", "0x00 (cold, asserted)", "ok", "CP1");

	/*
	 * seq 5/8: pre-reset core clock. The FORCEHT write is performed by
	 * bcma_core_set_clockmode() on the CHIPCOMMON CLKCTLST; nothing is
	 * written to D11+0x1e0 here, so this row records the ChipCommon
	 * effect and must not name a D11 offset or a D11 bit. (It previously
	 * logged CORE+0x1e0 "FORCEHT", which attributed a ChipCommon write to
	 * a D11 register of different identity.)
	 */
	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);
	ob_diag_op(hw, "reset", "bcma_core_set_clockmode", "CHIPCOMMON",
		   OB_CC_CLKCTLST, 32, "FORCEHT", "FAST",
		   hw->dev_lost ? "dev_lost" : "ok", "CP2");

	/*
	 * seq 9: sb_core_reset. Use the proven kernel core-enable primitive (as
	 * the previously hardware-proven bring-up paths did): only enable when
	 * bcma reports the core down. A forced bcma_core_disable()+enable() was
	 * shown on hardware to leave the D11 functional window returning
	 * all-ones, so it is NOT a safe substitute for the vendor reset.
	 */
	if (!bcma_core_is_enabled(hw->core)) {
		ret = bcma_core_enable(hw->core, 0);
		ob_diag_op(hw, "reset", "sb_core_reset", "CORE",
			   OB_DIAG_REG_SB_CHIPCONTROL, 32, "enable",
			   ret ? "fail" : "enabled", ret ? "fail" : "ok",
			   "CP2");
		if (ret) {
			dev_err(hw->dev,
				"rev42-attach: bcma_core_enable failed: %d\n",
				ret);
			goto fail;
		}
	} else {
		ob_diag_op(hw, "reset", "sb_core_reset", "CORE",
			   OB_DIAG_REG_SB_CHIPCONTROL, 32, "enable",
			   "already-enabled", "ok", "CP2");
	}
	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);

	/*
	 * Soft accessibility gate: after the core is enabled the D11 functional
	 * window must be readable. All-ones here means the core is down (a
	 * failed enable), NOT device loss, so this is a NON-trusted read and it
	 * never latches. Only past this gate do trusted sentinels apply.
	 */
	{
		u32 mc = ob_d11_read32(hw, OB_DIAG_REG_MACCONTROL);

		dev_info(hw->dev,
			 "rev42-attach: core accessibility after enable: maccontrol=0x%08x\n",
			 mc);
		if (mc == OB_GUARD_ALL_ONES32) {
			dev_err(hw->dev,
				"rev42-attach: D11 window still all-ones after core enable; core not accessible; STOPPING (soft, no latch)\n");
			ret = -EIO;
			goto fail;
		}
	}

	/* Vendor wlc_bmac_corereset AC-PHY clock pair (phy_type 0x0B gate). */
	ob_attach_cflags(hw, "core-cflags-a", OB_DIAG_CFLAGS_CORERESET_A_MASK,
			 OB_DIAG_CFLAGS_CORERESET_A_VAL, "CP2");
	ob_attach_cflags(hw, "core-cflags-b", OB_DIAG_CFLAGS_CORERESET_B_MASK,
			 0x0000, "CP2");
	ob_attach_cflags(hw, "core-cflags-c", OB_DIAG_CFLAGS_CORERESET_C_MASK,
			 OB_DIAG_CFLAGS_CORERESET_C_VAL, "CP2");

	st->iscoreup_after = ob_attach_iscoreup(hw, &st->iscoreup_raw_after);
	ob_diag_op_read32(hw, "reset", "si_iscoreup",
			  OB_DIAG_REG_SB_CHIPCONTROL,
			  st->iscoreup_raw_after, "CP2");
	dev_info(hw->dev,
		 "rev42-attach: si_iscoreup(after)=%d raw=0x%08x\n",
		 st->iscoreup_after ? 1 : 0, st->iscoreup_raw_after);
	ob_diag_checkpoint(hw, 2, "post-sb-core-reset");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* seq 11: MACCONTROL = 0x04000400 (wlc_bmac_mctrl(sh, ~0, 0x4000400)). */
	mc_old = ob_d11_read32(hw, OB_DIAG_REG_MACCONTROL);
	ob_d11_write32(hw, OB_DIAG_REG_MACCONTROL,
		       OB_DIAG_MACCONTROL_CORERESET);
	mc_new = ob_d11_read32(hw, OB_DIAG_REG_MACCONTROL);
	{
		char w[24], r[24];
		scnprintf(w, sizeof(w), "0x%08x", OB_DIAG_MACCONTROL_CORERESET);
		scnprintf(r, sizeof(r), "0x%08x (was 0x%08x)", mc_new, mc_old);
		ob_diag_op(hw, "mac", "wlc_bmac_mctrl", "MACCONTROL",
			   OB_DIAG_REG_MACCONTROL, 32, w, r,
			   hw->dev_lost ? "dev_lost" : "ok", "CP3");
	}
	dev_info(hw->dev, "rev42-attach: MACCONTROL 0x%08x -> 0x%08x\n",
		 mc_old, mc_new);
	ob_diag_checkpoint(hw, 3, "post-maccontrol");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* seq 12: wlc_bmac_phy_reset reads D11+0x3E0 (pre SITE A). */
	st->phyver_pre = ob_d11_read16(hw, OB_DIAG_REG_PHY_VER);
	{
		char rd[16];
		scnprintf(rd, sizeof(rd), "0x%04x", st->phyver_pre);
		ob_diag_op(hw, "phy", "wlc_bmac_phy_reset", "PHY",
			   OB_DIAG_REG_PHY_VER, 16, "none", rd,
			   hw->dev_lost ? "dev_lost" : "ok", "CP4");
	}

	/* seq 13: wlc_bmac_core_phy_clk - last cflags pair before SITE A. */
	ob_attach_cflags(hw, "phy-clk-a", OB_DIAG_CFLAGS_PHYCLK_A_MASK,
			 0x0000, "CP4");
	udelay(1);
	ob_attach_cflags(hw, "phy-clk-b", OB_DIAG_CFLAGS_PHYCLK_B_MASK,
			 OB_DIAG_CFLAGS_PHYCLK_B_VAL, "CP4");
	ob_diag_checkpoint(hw, 4, "post-phy-clk");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* CP5: immediately BEFORE SITE A. */
	ob_diag_checkpoint(hw, 5, "pre-site-a");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* seq 14: SITE A - writew(0x0000, D11+0x3E6). */
	ob_d11_write16(hw, OB_DIAG_REG_PHY_ANALOG, OB_DIAG_PHY_ANALOG_ON);
	ob_diag_op(hw, "site_a", "wlc_phy_anacore", "PHY_ANALOG",
		   OB_DIAG_REG_PHY_ANALOG, 16, "0x0000", "none",
		   hw->dev_lost ? "dev_lost" : "ok", "CP5");

	/* CP6: immediately AFTER SITE A - trusted sentinel. */
	ob_diag_checkpoint(hw, 6, "post-site-a");
	if (hw->dev_lost) {
		dev_err(hw->dev, "rev42-attach: DEVICE LOST at SITE A\n");
		ret = -EIO;
		goto fail;
	}

	/* seq 15: B - read D11+0x3E0; phy_type = (v>>8)&0x0F. */
	st->phyver_post = ob_d11_read16(hw, OB_DIAG_REG_PHY_VER);
	st->phy_type = (u8)((st->phyver_post >> 8) & 0x0fu);
	st->phy_rev = (u8)(st->phyver_post & 0x0fu);
	{
		char rd[24];
		scnprintf(rd, sizeof(rd), "0x%04x (phy_type=0x%02x rev=0x%x)",
			  st->phyver_post, st->phy_type, st->phy_rev);
		ob_diag_op(hw, "phy", "phyversion", "PHY", OB_DIAG_REG_PHY_VER,
			   16, "none", rd, hw->dev_lost ? "dev_lost" : "ok",
			   "CP7");
	}
	dev_info(hw->dev,
		 "rev42-attach: PHY version=0x%04x phy_type=0x%02x phy_rev=0x%x (expect phy_type 0x0B)\n",
		 st->phyver_post, st->phy_type, st->phy_rev);
	ob_diag_checkpoint(hw, 7, "post-phy-version");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* seq 16: wlc_phy_attach_acphy - callback table install, no MMIO. */
	ob_diag_op(hw, "acphy", "wlc_phy_attach_acphy", "-", 0, 0,
		   "callback-table", "no-mmio", "ok", "CP8");
	ob_diag_checkpoint(hw, 8, "post-acphy-attach");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* CP9: immediately BEFORE SITE B. */
	ob_diag_checkpoint(hw, 9, "pre-site-b");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* seq 17: SITE B - writew(0x0000, D11+0x3E6), idempotent. */
	ob_d11_write16(hw, OB_DIAG_REG_PHY_ANALOG, OB_DIAG_PHY_ANALOG_ON);
	ob_diag_op(hw, "site_b", "wlc_phy_anacore", "PHY_ANALOG",
		   OB_DIAG_REG_PHY_ANALOG, 16, "0x0000", "none",
		   hw->dev_lost ? "dev_lost" : "ok", "CP9");

	/* CP10: immediately AFTER SITE B - trusted sentinel. */
	ob_diag_checkpoint(hw, 10, "post-site-b");
	if (hw->dev_lost) {
		dev_err(hw->dev, "rev42-attach: DEVICE LOST at SITE B\n");
		ret = -EIO;
		goto fail;
	}

	/*
	 * seq 18: radio-window predicate sel = *(wlc + 0x28). That selector is
	 * a vendor software state field OpenBRCM does not carry, so it is
	 * UNAVAILABLE here. The YAML's explicitly sanctioned safe fallback for
	 * an unknown selector is the MODERN 2-word read validated against the
	 * 2069 format, aborting (never switching windows) on 0xFFFF/0xFFFF.
	 */
	st->selector_available = false;
	st->window_modern = true;
	dev_info(hw->dev,
		 "rev42-attach: radio-window selector sh->0x28 UNAVAILABLE (vendor software field); using YAML fallback -> MODERN 0x3D8/0x3DA, abort on 0xFFFF (never switch window)\n");
	ob_diag_op(hw, "radio", "radio_window_predicate", "control", 0, 32,
		   "read", "unavailable -> MODERN", "ok", "CP11");
	ob_diag_checkpoint(hw, 11, "radio-window-predicate");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* CP12: before first radio access. */
	ob_diag_checkpoint(hw, 12, "pre-radio");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* seq 19/24: MODERN radio ID read. */
	ob_attach_radio_read(hw);
	ob_diag_radio_log(hw, "attach", &st->radio);
	ob_diag_checkpoint(hw, 13, "post-radio-0");
	if (hw->dev_lost) { ret = -EIO; goto fail; }
	ob_diag_checkpoint(hw, 14, "post-radio-1");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* CP15: final D11-alive check. */
	ob_diag_checkpoint(hw, 15, "final");
	if (hw->dev_lost) { ret = -EIO; goto fail; }

	/* D11 survived every dangerous transition (CP15 final sentinel passed). */
	if (st->radio_ran && !st->radio.valid) {
		dev_err(hw->dev,
			"rev42-attach: PARTIAL - SITE A+SITE B survived, D11 alive, but radio window returned 0x%04x/0x%04x (not reachable; attach failed upstream)\n",
			st->radio.r0, st->radio.r1);
	} else {
		dev_info(hw->dev,
			 "rev42-attach: PASS through SITE B; radio id_byte=0x%02x r0=0x%04x r1=0x%04x valid=%d\n",
			 st->radio.id_byte, st->radio.r0, st->radio.r1,
			 st->radio.valid ? 1 : 0);
	}
	dev_info(hw->dev,
		 "rev42-attach: STOPPED BEFORE band init / PHY init / radio config / cal / EN_MAC\n");
	return 0;

fail:
	dev_err(hw->dev,
		"rev42-attach: FAIL ret=%d last_cp=%u (no cleanup writes; hardware left as-is)\n",
		ret, st->seq);
	return ret;
}

/* ====================================================================== */
/* Vendor sb_core_reset cold path (rev42_sb_core_reset.yaml)             */
/* ====================================================================== */

static void ob_sb_op(struct ob_hw *hw, int yseq, const char *phase,
		     const char *fn, u32 off, const char *wr, const char *rd,
		     const char *result, const char *cp)
{
	dev_info(hw->dev,
		 "rev42-diag-op: {\"seq\":%u,\"yaml_seq\":%d,\"phase\":\"%s\",\"function\":\"%s\",\"block\":\"SB_CORE\",\"offset\":%u,\"width\":32,\"value_written\":\"%s\",\"value_read\":\"%s\",\"result\":\"%s\",\"checkpoint\":\"%s\",\"device_alive\":%d}\n",
		 hw->diag.seq++, yseq, phase, fn, off, wr, rd, result, cp,
		 hw->dev_lost ? 0 : 1);
}

static void ob_sb_read_op(struct ob_hw *hw, int yseq, const char *phase,
			  const char *fn, u32 off, u32 val, const char *cp)
{
	char r[16];

	scnprintf(r, sizeof(r), "0x%08x", val);
	ob_sb_op(hw, yseq, phase, fn, off, "none", r, "ok", cp);
}

static void ob_sb_write_op(struct ob_hw *hw, int yseq, const char *phase,
			   const char *fn, u32 off, u32 val, u32 rb,
			   const char *cp)
{
	char w[16], r[16];

	scnprintf(w, sizeof(w), "0x%08x", val);
	scnprintf(r, sizeof(r), "0x%08x", rb);
	ob_sb_op(hw, yseq, phase, fn, off, w, r, "ok", cp);
}

int ob_sb_reset_test(struct ob_hw *hw)
{
	struct ob_diag *st = &hw->diag;
	u32 v, s8, fg, s9c, rb;
	int ret = 0, i;

	memset(st, 0, sizeof(*st));
	st->sb_reset = true;

	if (hw->core->id.rev != OB_DIAG_PHYREV_REV42) {
		dev_err(hw->dev, "rev42-sb: unsupported D11 rev %u\n",
			hw->core->id.rev);
		return -ENOTSUPP;
	}
	if (hw->dev_lost) {
		dev_err(hw->dev, "rev42-sb: refusing: dev_lost latched\n");
		return -EIO;
	}

	dev_info(hw->dev,
		 "rev42-sb: BEGIN (vendor sb_core_reset cold path ONLY; SB-block registers (0x0F00-0x0FFF) INSIDE the D11 core window core->addr; the BCMA wrapper window core->wrap is never touched; no PHY/SITE A)\n");
	ob_sb_op(hw, 0, "envelope", "ob_sb_reset_test", 0, "none", "none",
		 "ok", "R0");

	/* seq 1: host/backplane up (BCMA already enumerated the core). */
	bcma_host_pci_up(hw->bus);
	ob_sb_op(hw, 1, "envelope", "bcma_host_pci_up", 0, "host-up", "none",
		 "ok", "R0");
	dev_info(hw->dev, "rev42-sb: chip 0x%04x d11 rev %u\n",
		 hw->chip_id, hw->core->id.rev);

	/* R0: initial wrapper state (SB registers only). */
	st->sb_ctrl_r0 = ob_d11_read32(hw, OB_SB_REG_CTRL);
	st->sb_status_s_r0 = ob_d11_read32(hw, OB_SB_REG_STATUS_S);
	st->sb_forcegate_r0 = ob_d11_read32(hw, OB_SB_REG_FORCEGATE);
	st->sb_status_r0 = ob_d11_read32(hw, OB_SB_REG_STATUS);
	ob_sb_read_op(hw, 7, "sb_block", "sb_core_disable(0x0F98)", OB_SB_REG_CTRL,
		      st->sb_ctrl_r0, "R0");
	ob_sb_read_op(hw, 12, "sb_block", "0x0F8 status", OB_SB_REG_STATUS_S,
		      st->sb_status_s_r0, "R0");
	ob_sb_read_op(hw, 17, "sb_block", "0x0F90 force-gate", OB_SB_REG_FORCEGATE,
		      st->sb_forcegate_r0, "R0");
	ob_sb_read_op(hw, 16, "sb_block", "0x0F9C status", OB_SB_REG_STATUS,
		      st->sb_status_r0, "R0");
	dev_info(hw->dev,
		 "rev42-sb: R0 raw 0x0F98=0x%08x 0x0F8=0x%08x 0x0F90=0x%08x 0x0F9C=0x%08x\n",
		 st->sb_ctrl_r0, st->sb_status_s_r0, st->sb_forcegate_r0,
		 st->sb_status_r0);
	dev_info(hw->dev,
		 "rev42-sb: R0 0x0F98 bit0=%d bit1=%d bit2=%d bit16=%d sb_block_reachable=%d\n",
		 !!(st->sb_ctrl_r0 & 1), !!(st->sb_ctrl_r0 & 2),
		 !!(st->sb_ctrl_r0 & 4), !!(st->sb_ctrl_r0 & 0x10000),
		 st->sb_ctrl_r0 != OB_GUARD_ALL_ONES32);

	if (st->sb_ctrl_r0 == OB_GUARD_ALL_ONES32) {
		dev_err(hw->dev,
			"rev42-sb: SB_BLOCK_UNREACHABLE: 0x0F98 reads all-ones (D11 core window core->addr not decoded). STOPPING before writes.\n");
		ret = -EIO;
		goto fail;
	}

	/* R-2: bit2 must be clear; if set, STOP BEFORE WRITES. */
	if (st->sb_ctrl_r0 & OB_SB_CTRL_BIT2) {
		dev_err(hw->dev,
			"rev42-sb: R-2 CAVEAT: 0x0F98 bit2 is SET on the cold device; the predicate can never be satisfied. STOPPING BEFORE WRITES.\n");
		ret = -EIO;
		goto fail;
	}

	/*
	 * seq 4: D11 +0x1e0 bit1 (vendor sub_64887, the mac-clock on/off
	 * helper). The WRITE is vendor-proven and unchanged: sub_64887 does
	 * read(D11+0x1e0); write(D11+0x1e0, v | 0x2).
	 *
	 * Two corrections to the previous version of this step:
	 * - it was labelled "si_clkctl_cc", but si_clkctl_cc has zero MMIO
	 *   accesses in the blob (it delegates to 0x2105c) and is a ChipCommon
	 *   helper. This is a D11 write, so it is logged under the D11 offset.
	 * - it polled bit17 (0x20000) for up to 200 ms. bit17 is the ChipCommon
	 *   HAVEHT bit; the vendor never manipulates bit17 on the D11 window,
	 *   and the observed 0x070b0042 already has it set, so the loop exited
	 *   on iteration 0 and proved nothing. The vendor's own bounded wait
	 *   here is a delay, not a bit17 wait, so the loop now waits for the
	 *   bit that was actually written (bit1) and reports the raw value.
	 */
	v = ob_d11_read32(hw, OB_SB_REG_CLK);
	ob_d11_write32(hw, OB_SB_REG_CLK, v | OB_DIAG_D11_CLK1E0_BIT1);
	udelay(64);
	for (i = 0; i < 20000; i++) {
		v = ob_d11_read32(hw, OB_SB_REG_CLK);
		if (v & OB_DIAG_D11_CLK1E0_BIT1)
			break;
		udelay(10);
	}
	{
		char r[24];

		scnprintf(r, sizeof(r), "0x%08x bit1=%d", v,
			  !!(v & OB_DIAG_D11_CLK1E0_BIT1));
		ob_sb_op(hw, 4, "reset", "d11_clk1e0_bit1", OB_SB_REG_CLK,
			 "|0x2,poll(bit1,0x20000)", r, "ok", "R1");
	}

	/* seq 5: si_iscoreup (already sampled at R0). */
	st->sb_bit0_entry = !!(st->sb_ctrl_r0 & 1);
	st->sb_bit16_entry = !!(st->sb_ctrl_r0 & 0x10000);
	st->sb_iscoreup = (st->sb_ctrl_r0 & OB_SB_ISCOREUP_MASK) ==
			  OB_SB_ISCOREUP_VAL;
	dev_info(hw->dev,
		 "rev42-sb: si_iscoreup(before)=%d bit0=%d bit16=%d -> %s\n",
		 st->sb_iscoreup ? 1 : 0, st->sb_bit0_entry ? 1 : 0,
		 st->sb_bit16_entry ? 1 : 0,
		 st->sb_bit0_entry ? "DISABLE EARLY-OUT (bit0 set)" :
		 (st->sb_bit16_entry ? "WARM (bit16 set)" : "COLD path (bit16 clear)"));

	/* seq 6: sh->0x184 noreset (cold attach) - asserted, vendor software. */
	ob_sb_op(hw, 6, "reset", "sh_0x184", 0x184, "read", "0x00 (cold)",
		 "ok", "R1");

	/* ---- sb_core_disable cold sub-path (D1/D2/D7/D8) ---- */
	if (st->sb_bit0_entry) {
		/* D1: bit0 set -> sb_core_disable returns immediately, no writes. */
		st->sb_early_out = true;
		dev_info(hw->dev,
			 "rev42-sb: bit0 set -> sb_core_disable EARLY-OUT (no D7/D8 writes); A1..A8 still run\n");
		ob_sb_op(hw, 7, "reset", "sb_core_disable(D1)", OB_SB_REG_CTRL,
			 "none", "bit0=1 early-out", "ok", "R1");
	} else if (st->sb_bit16_entry) {
		/* D2: bit16 set -> not a cold core; warm path not covered. */
		dev_err(hw->dev,
			"rev42-sb: NOT A COLD CORE (bit16 set, bit0 clear); warm path D3-D6 not covered. STOPPING.\n");
		ret = -EIO;
		goto fail;
	} else {
		/* D2: cold -> skip D3-D6, run D7/D8. */
		ob_sb_op(hw, 8, "reset", "sb_core_disable(D2)", OB_SB_REG_CTRL,
			 "none", "bit16=0 cold", "ok", "R1");

		/* D7: write 0x00070003. */
		ob_d11_write32(hw, OB_SB_REG_CTRL, OB_SB_CTRL_70003);
		/* D7t: readback + delay(10). */
		rb = ob_d11_read32(hw, OB_SB_REG_CTRL);
		udelay(10);
		ob_sb_write_op(hw, 10, "reset", "sb_core_disable(D7)",
			       OB_SB_REG_CTRL, OB_SB_CTRL_70003, rb, "R1");
		/* R1 recorded. */

		/* D7u/D7v: read 0x0F8; if bit31, clear bit25 of 0x0F90. */
		s8 = ob_d11_read32(hw, OB_SB_REG_STATUS_S);
		ob_sb_read_op(hw, 12, "reset", "0x0F8 status", OB_SB_REG_STATUS_S,
			      s8, "R2");
		if (s8 & 0x80000000u) {
			fg = ob_d11_read32(hw, OB_SB_REG_FORCEGATE);
			ob_d11_write32(hw, OB_SB_REG_FORCEGATE,
				       fg & 0xFDFFFFFFu);
			ob_sb_write_op(hw, 12, "reset", "0x0F90 clear bit25",
				       OB_SB_REG_FORCEGATE, fg & 0xFDFFFFFFu,
				       fg, "R2");
			st->sb_forcegate_seq12 = true;
		} else {
			dev_info(hw->dev,
				 "rev42-sb: 0x0F8 bit31 clear -> D7v skipped\n");
		}

		/* D8: write 0x00040003 + delay(1). */
		ob_d11_write32(hw, OB_SB_REG_CTRL, OB_SB_CTRL_40003);
		udelay(1);
		ob_sb_write_op(hw, 13, "reset", "sb_core_disable(D8)",
			       OB_SB_REG_CTRL, OB_SB_CTRL_40003,
			       ob_d11_read32(hw, OB_SB_REG_CTRL), "R3");
	}

	/* ---- sb_core_reset A1..A8 ---- */
	/* A1: write 0x00070001 (assert reset). */
	ob_d11_write32(hw, OB_SB_REG_CTRL, OB_SB_CTRL_70001);
	/* A2: readback + delay(1). */
	rb = ob_d11_read32(hw, OB_SB_REG_CTRL);
	udelay(1);
	ob_sb_write_op(hw, 14, "reset", "sb_core_reset(A1)", OB_SB_REG_CTRL,
		       OB_SB_CTRL_70001, rb, "R3");

	/* A3: read 0x0F9C; if bit0, write 0. */
	s9c = ob_d11_read32(hw, OB_SB_REG_STATUS);
	ob_sb_read_op(hw, 16, "reset", "0x0F9C status", OB_SB_REG_STATUS, s9c,
		      "R3");
	if (s9c & 1u) {
		ob_d11_write32(hw, OB_SB_REG_STATUS, 0x00000000u);
		ob_sb_write_op(hw, 16, "reset", "0x0F9C clear latch",
			       OB_SB_REG_STATUS, 0, s9c, "R3");
		st->sb_latch_seq16 = true;
	} else {
		dev_info(hw->dev, "rev42-sb: 0x0F9C bit0 clear -> A3 skipped\n");
	}

	/* A4: read 0x0F90; if bits17/18, re-read and clear them. */
	fg = ob_d11_read32(hw, OB_SB_REG_FORCEGATE);
	ob_sb_read_op(hw, 17, "reset", "0x0F90 force-gate", OB_SB_REG_FORCEGATE,
		      fg, "R3");
	if (fg & 0x00060000u) {
		u32 fg2 = ob_d11_read32(hw, OB_SB_REG_FORCEGATE);

		ob_d11_write32(hw, OB_SB_REG_FORCEGATE, fg2 & 0xFFF9FFFFu);
		ob_sb_write_op(hw, 17, "reset", "0x0F90 clear 17/18",
			       OB_SB_REG_FORCEGATE, fg2 & 0xFFF9FFFFu, fg2,
			       "R3");
		st->sb_forcegate_seq17 = true;
	} else {
		dev_info(hw->dev,
			 "rev42-sb: 0x0F90 bits17/18 clear -> A4 skipped\n");
	}

	/* A5: write 0x00070000 (de-assert reset) + readback + delay(1). */
	ob_d11_write32(hw, OB_SB_REG_CTRL, OB_SB_CTRL_70000);
	rb = ob_d11_read32(hw, OB_SB_REG_CTRL);
	udelay(1);
	ob_sb_write_op(hw, 18, "reset", "sb_core_reset(A5)", OB_SB_REG_CTRL,
		       OB_SB_CTRL_70000, rb, "R4");
	dev_info(hw->dev, "rev42-sb: R4 reset-release 0x0F98=0x%08x\n", rb);

	/* A7: TERMINAL write 0x00050000 + readback + delay(1). */
	ob_d11_write32(hw, OB_SB_REG_CTRL, OB_SB_CTRL_50000);
	rb = ob_d11_read32(hw, OB_SB_REG_CTRL);
	udelay(1);
	ob_sb_write_op(hw, 20, "reset", "sb_core_reset(A7)", OB_SB_REG_CTRL,
		       OB_SB_CTRL_50000, rb, "R5");
	st->sb_ctrl_terminal = rb;
	dev_info(hw->dev, "rev42-sb: R5 terminal 0x0F98=0x%08x\n", rb);

	/* R6: si_iscoreup evaluation. */
	st->sb_iscoreup = (st->sb_ctrl_terminal & OB_SB_ISCOREUP_MASK) ==
			  OB_SB_ISCOREUP_VAL;
	ob_sb_read_op(hw, 13, "verify", "si_iscoreup", OB_SB_REG_CTRL,
		      st->sb_ctrl_terminal, "R6");
	dev_info(hw->dev,
		 "rev42-sb: R6 si_iscoreup=%d (0x%08x & 0x%05x == 0x%05x)\n",
		 st->sb_iscoreup ? 1 : 0, st->sb_ctrl_terminal,
		 OB_SB_ISCOREUP_MASK, OB_SB_ISCOREUP_VAL);

	if (!st->sb_iscoreup) {
		dev_err(hw->dev,
			"rev42-sb: VENDOR_CORE_RESET_FAILED (predicate false); NOT touching the D11 functional window.\n");
		ret = -EIO;
		goto fail;
	}

	/* R7: FIRST D11 functional read - only now that the predicate is true. */
	ob_sb_op(hw, 14, "first_d11", "readw(D11+0x3E0)", OB_SB_FIRST_D11_REG,
		 "none", "read", "ok", "R7");
	st->sb_first_d11_3e0 = ob_d11_read16(hw, OB_SB_FIRST_D11_REG);
	{
		char r[16];
		scnprintf(r, sizeof(r), "0x%04x", st->sb_first_d11_3e0);
		ob_sb_op(hw, 14, "first_d11", "phyversion", OB_SB_FIRST_D11_REG,
			 "none", r, "ok", "R7");
	}
	st->sb_first_d11_maccontrol = ob_d11_read32(hw, OB_DIAG_REG_MACCONTROL);
	{
		char r[16];
		scnprintf(r, sizeof(r), "0x%08x",
			  st->sb_first_d11_maccontrol);
		ob_sb_op(hw, 14, "first_d11", "MACCONTROL",
			 OB_DIAG_REG_MACCONTROL, "none", r, "ok", "R7");
	}
	st->sb_first_d11_ran = true;
	dev_info(hw->dev,
		 "rev42-sb: R7 first D11 functional read: 0x3E0=0x%04x MACCONTROL=0x%08x\n",
		 st->sb_first_d11_3e0, st->sb_first_d11_maccontrol);

	if (st->sb_first_d11_3e0 == 0xffffu &&
	    st->sb_first_d11_maccontrol == 0xffffffffu) {
		dev_err(hw->dev,
			"rev42-sb: CORE_UP_BUT_D11_WINDOW_INACCESSIBLE (si_iscoreup true, window all-ones); STOPPING.\n");
		ret = -EIO;
		goto fail;
	}

	dev_info(hw->dev,
		 "rev42-sb: D11_FUNCTIONAL_WINDOW_ACCESSIBLE; STOPPED BEFORE PHY reset / SITE A / radio\n");
	return 0;

fail:
	dev_err(hw->dev, "rev42-sb: FAIL ret=%d (no cleanup writes)\n", ret);
	return ret;
}

void ob_diag_remove(struct ob_hw *hw)
{
	if (hw->dev_lost || hw->d3a0.lc.fatal) {
		dev_crit(hw->dev,
			 "rev42-diag: removed in FATAL/dev-lost state; no teardown, reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	/*
	 * Phase 37B: rev42_ai_enable_only issued at least one wrapper write. The
	 * core is in a post-write state this driver has no teardown contract for:
	 * no disable write, no reset, no resource release is performed here. The
	 * device is retained and a reboot is the only proven way back to the
	 * known reset-held state.
	 */
	if (hw->ai_enable_retain) {
		dev_crit(hw->dev,
			 "rev42-diag: removed after a POST-WRITE enable transition (core_up=%d); hardware left as-is, no teardown, no disable/reset write; reboot required\n",
			 hw->ai_enable_core_up ? 1 : 0);
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	dev_info(hw->dev,
		 "rev42-diag: removed (no resource teardown, hardware left as-is)\n");
	bcma_set_drvdata(hw->core, NULL);
}

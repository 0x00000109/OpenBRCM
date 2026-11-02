/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _OB_LIFECYCLE_H_
#define _OB_LIFECYCLE_H_

#include <linux/types.h>
#include "ob_irq.h"
#include "ob_d11_tail_plan.h"

/*
 * Per-device D11 lifecycle state.  Attach identity is established after the
 * BCMA-owned attach reset and before any firmware/ucode hardware work.  The
 * post-ucode state is published only after ucode, PSM startup, and common
 * initvals complete.  This is intentionally startup-paused state, not the
 * later runtime MAC-suspend contract.
 */
enum ob_d11_lifecycle_state {
	OB_D11_STATE_NONE = 0,
	OB_D11_CORE_READY,
	/* Attach reset and PHY/radio identity are valid; no ucode claim. */
	OB_D11_ATTACH_IDENTITY_READY,
	OB_D11_UCODE_LOADED,
	OB_D11_PSM_STARTED,
	OB_D11_STARTUP_SUSPEND_CONFIRMED,
	OB_D11_COMMON_INITVALS_APPLIED,
	/* Identity is already valid when this state is published. */
	OB_D11_POST_UCODE_READY,
	/*
	 * M4 post-ucode tail states.  Appended so every numeric value above keeps
	 * its old meaning and comparisons such as
	 * `d11_state < OB_D11_POST_UCODE_READY` remain valid.  These states are
	 * published only after the whole stage returned success; a failed stage
	 * leaves the last proven state and records failure evidence instead.
	 * The normal mac80211 .start path does not use them yet (M5).
	 */
	OB_D11_TAIL_FIXUP_APPLIED,
	OB_D11_DMA_PROGRAMMED,
	OB_D11_TAIL_COMPLETE,
};

#define OB_D11_REG_MACCONTROL 0x0120u
#define OB_D11_MCTL_EN_MAC 0x00000001u

/*
 * Calibration bracket MAC precondition (vendor rule at the 0x0925f6 call):
 * wlapi_suspend_mac_and_wait treats an already-suspended MAC as satisfied, so
 * EN_MAC == 0 is the whole requirement.  The exact word is deliberately not
 * constrained: this bench's attach leaves 0x04020402, and the bracket-open
 * read returned 0x84020402 (bit 31 set on the read, never written by this
 * driver) — an equality test refused that and burned the M1-6 window with zero
 * writes (exec-20261028T222049Z, M16-B).  All-ones is a dead bus, not a state.
 */
static inline bool ob_d11_mac_already_suspended(u32 maccontrol)
{
	return maccontrol != 0xffffffffu &&
	       (maccontrol & OB_D11_MCTL_EN_MAC) == 0;
}
/* Same bit as OB_DIAG_MCTL_WAKE; set while a wake override is active. */
#define OB_D11_MCTL_WAKE 0x04000000u
#define OB_D11_STARTUP_QUIESCED_MACCONTROL 0x04020402u

/*
 * M1-6D, vendor wake-override model (wlc_ucode_wake_override_set @0x0639c3,
 * clear @0x0605f6, composer sub_605ad @0x0605ad, wait_for_wake @0x063946):
 *
 *  - the bitmap lives at hw+0x170; the vendor masks are SUSPEND = 0x4 (set by
 *    wlc_bmac_suspend_mac_and_wait @0x063a04) and PHYREG = 0x2 (set by
 *    wlc_phyreg_enter @0x0bafd1);
 *  - MACCONTROL bit 26 follows the bitmap transitions only: set when it goes
 *    0 -> nonzero, cleared when it returns to 0.  A nonzero bitmap makes any
 *    further override set bookkeeping-only, with no register write at all;
 *  - the wait is a bounded spin WHILE SHM 0x40 reads 4, it ends on any other
 *    value, and it cannot fail.  The vendor derives the bound from hw+0x192
 *    and has an initial delay branch; OpenBRCM models the common 40 us delay
 *    and a fixed 200-poll bound.
 *
 * The consequence that matters for the calibration bracket: with the suspend
 * override active, wlc_phyreg_enter performs no MMIO, exactly as the vendor's
 * does (the frozen trace shows no MACCONTROL write and no SHM 0x40 access at
 * the 0x0925fe call site).
 */
#define OB_D11_WAKE_OVERRIDE_SUSPEND 0x00000004u
#define OB_D11_WAKE_OVERRIDE_PHYREG 0x00000002u
#define OB_D11_WAKE_OVERRIDE_ALL \
	(OB_D11_WAKE_OVERRIDE_SUSPEND | OB_D11_WAKE_OVERRIDE_PHYREG)
#define OB_D11_WAKE_SHM 0x0040u
#define OB_D11_WAKE_PENDING 0x0004u
#define OB_D11_WAKE_FIRST_US 40u
#define OB_D11_WAKE_POLLS 200u
#define OB_D11_WAKE_POLL_US 10u
#define OB_D11_RUNTIME_POLL_START 0x00014441u
#define OB_D11_RUNTIME_POLL_STEP 10u
#define OB_D11_RUNTIME_POLL_DELAY_US 10u

/* Pure guarded-script interface for host tests and the kernel adapter. */
struct ob_d11_runtime_io {
	int (*read32)(void *ctx, u16 off, u32 *value);
	int (*write32)(void *ctx, u16 off, u32 value);
	void (*delay_us)(void *ctx, u32 usec);
};

static inline int ob_d11_runtime_quiesce_script(
	const struct ob_d11_runtime_io *io, void *ctx, u32 *polls,
	bool *transitioned)
{
	u32 maccontrol, status, remaining = OB_D11_RUNTIME_POLL_START;
	int ret;

	if (!io || !io->read32 || !io->write32 || !io->delay_us)
		return -22;
	if (transitioned)
		*transitioned = false;
	ret = io->read32(ctx, OB_D11_REG_MACCONTROL, &maccontrol);
	if (ret || maccontrol == 0xffffffffu)
		return ret ? ret : -5;
	/* Preserve the vendor ordering: status observation precedes the request. */
	ret = io->read32(ctx, OB_D11_REG_MACINTSTATUS, &status);
	if (ret || status == 0xffffffffu)
		return ret ? ret : -5;
	/* Identity preparation deliberately leaves this exact startup-paused
	 * state.  There is no new EN_MAC transition to generate MI_MACSSPNDD. */
	if (!(maccontrol & OB_D11_MCTL_EN_MAC)) {
		if (maccontrol != OB_D11_STARTUP_QUIESCED_MACCONTROL)
			return -22;
		if (polls)
			*polls = 0;
		return 0;
	}
	if (transitioned)
		*transitioned = true;
	ret = io->write32(ctx, OB_D11_REG_MACCONTROL,
			  (maccontrol & ~OB_D11_MCTL_EN_MAC));
	if (ret)
		return ret;
	if (polls)
		*polls = 0;
	for (;;) {
		ret = io->read32(ctx, OB_D11_REG_MACINTSTATUS, &status);
		if (ret || status == 0xffffffffu)
			return ret ? ret : -5;
		if (status & OB_D11_MI_MACSSPNDD) {
			/* Vendor verifies both registers after the bounded loop. */
			ret = io->read32(ctx, OB_D11_REG_MACINTSTATUS, &status);
			if (ret || status == 0xffffffffu)
				return ret ? ret : -5;
			ret = io->read32(ctx, OB_D11_REG_MACCONTROL, &maccontrol);
			if (ret || maccontrol == 0xffffffffu)
				return ret ? ret : -5;
			return 0;
		}
		if (remaining <= OB_D11_RUNTIME_POLL_STEP)
			return -110;
		io->delay_us(ctx, OB_D11_RUNTIME_POLL_DELAY_US);
		remaining -= OB_D11_RUNTIME_POLL_STEP;
		if (polls)
			(*polls)++;
	}
}

static inline int ob_d11_runtime_suspend_script(
	const struct ob_d11_runtime_io *io, void *ctx, u32 *polls)
{
	bool transitioned = false;
	int ret;

	ret = ob_d11_runtime_quiesce_script(io, ctx, polls, &transitioned);
	return ret ? ret : (transitioned ? 0 : -22);
}

static inline int ob_d11_runtime_resume_script(
	const struct ob_d11_runtime_io *io, void *ctx)
{
	u32 maccontrol, status;
	int ret;

	if (!io || !io->read32 || !io->write32)
		return -22;
	ret = io->read32(ctx, OB_D11_REG_MACCONTROL, &maccontrol);
	if (ret || maccontrol == 0xffffffffu)
		return ret ? ret : -5;
	ret = io->write32(ctx, OB_D11_REG_MACCONTROL,
			  maccontrol | OB_D11_MCTL_EN_MAC);
	if (ret)
		return ret;
	/* Vendor W1C acknowledgement of the suspend indication. */
	ret = io->write32(ctx, OB_D11_REG_MACINTSTATUS,
			  OB_D11_MI_MACSSPNDD);
	if (ret)
		return ret;
	ret = io->read32(ctx, OB_D11_REG_MACCONTROL, &maccontrol);
	if (ret || maccontrol == 0xffffffffu)
		return ret ? ret : -5;
	ret = io->read32(ctx, OB_D11_REG_MACINTSTATUS, &status);
	if (ret || status == 0xffffffffu)
		return ret ? ret : -5;
	return 0;
}

/* Guarded IO for the vendor wake-override scripts (host-testable). */
struct ob_d11_wake_io {
	int (*read32)(void *ctx, u16 off, u32 *value);
	int (*write32)(void *ctx, u16 off, u32 value);
	int (*read_shm16)(void *ctx, u16 off, u16 *value);
	void (*delay_us)(void *ctx, u32 usec);
};

/*
 * wlc_bmac_wait_for_wake @0x063946, predicate direction preserved: delay once,
 * then keep polling WHILE SHM 0x40 reads 4 and stop on any other value or when
 * the bound is spent.  The vendor function is void; so is this one.  A read
 * failure also ends the wait without turning it into an error.
 */
static inline void ob_d11_wait_for_wake_script(const struct ob_d11_wake_io *io,
					       void *ctx)
{
	u32 polls;
	u16 v;

	if (!io || !io->read_shm16 || !io->delay_us)
		return;
	io->delay_us(ctx, OB_D11_WAKE_FIRST_US);
	for (polls = 0; polls < OB_D11_WAKE_POLLS; polls++) {
		if (io->read_shm16(ctx, OB_D11_WAKE_SHM, &v))
			return;
		if (v != OB_D11_WAKE_PENDING)
			return;
		if (polls + 1u == OB_D11_WAKE_POLLS)
			return;
		io->delay_us(ctx, OB_D11_WAKE_POLL_US);
	}
}

/*
 * wlc_ucode_wake_override_set @0x0639c3: the 0 -> nonzero transition writes
 * MACCONTROL with bit 26 and waits; a nonzero bitmap only ORs the mask.
 * Documented deviations from the vendor: the register is read fresh instead of
 * being composed from the cached hw+0x168, and the hw+0x16b bit 2 "skip the
 * write" gate is not modelled (its meaning is UNKNOWN; m1_6d report).
 */
static inline int ob_d11_wake_override_set_script(
	const struct ob_d11_wake_io *io, void *ctx, u32 *override, u32 mask)
{
	u32 mctrl;
	int ret;

	if (!io || !override || !mask || (mask & ~OB_D11_WAKE_OVERRIDE_ALL))
		return -22;
	if (!io->read32 || !io->write32)
		return -22;
	/* Vendor 0x0639d7: bitmap already nonzero -> bookkeeping only. */
	if (*override) {
		*override |= mask;
		return 0;
	}
	*override = mask;
	ret = io->read32(ctx, OB_D11_REG_MACCONTROL, &mctrl);
	if (ret)
		goto undo;
	ret = io->write32(ctx, OB_D11_REG_MACCONTROL,
			  mctrl | OB_D11_MCTL_WAKE);
	if (ret)
		goto undo;
	ob_d11_wait_for_wake_script(io, ctx);
	return 0;
undo:
	*override = 0;
	return ret;
}

/* wlc_ucode_wake_override_clear @0x0605f6: write only when the bitmap empties. */
static inline int ob_d11_wake_override_clear_script(
	const struct ob_d11_wake_io *io, void *ctx, u32 *override, u32 mask)
{
	u32 mctrl;
	int ret;

	if (!io || !override || !mask || (mask & ~OB_D11_WAKE_OVERRIDE_ALL))
		return -22;
	if (!io->read32 || !io->write32)
		return -22;
	*override &= ~mask;
	if (*override)
		return 0;
	ret = io->read32(ctx, OB_D11_REG_MACCONTROL, &mctrl);
	if (ret)
		return ret;
	return io->write32(ctx, OB_D11_REG_MACCONTROL,
			   mctrl & ~OB_D11_MCTL_WAKE);
}

/* wlc_phyreg_enter @0x0bafd1: refcount on pi+0x113d, override mask 2 at 0 -> 1. */
static inline int ob_d11_phyreg_enter_script(
	const struct ob_d11_wake_io *io, void *ctx, u32 *override, u8 *refcount)
{
	int ret;

	if (!refcount)
		return -22;
	if (*refcount == 0xffu)
		return -22;
	(*refcount)++;
	if (*refcount > 1u)
		return 0;
	ret = ob_d11_wake_override_set_script(io, ctx, override,
					      OB_D11_WAKE_OVERRIDE_PHYREG);
	if (ret)
		(*refcount)--;
	return ret;
}

/* wlc_phyreg_exit @0x0bafac: only the 1 -> 0 transition touches the bitmap. */
static inline int ob_d11_phyreg_exit_script(
	const struct ob_d11_wake_io *io, void *ctx, u32 *override, u8 *refcount)
{
	if (!refcount || !*refcount)
		return -22;
	(*refcount)--;
	if (*refcount)
		return 0;
	return ob_d11_wake_override_clear_script(io, ctx, override,
						OB_D11_WAKE_OVERRIDE_PHYREG);
}

/* Pure transition guard used by host tests and the production stage. */
static inline bool ob_d11_lifecycle_step_allowed(
	enum ob_d11_lifecycle_state state,
	enum ob_d11_lifecycle_state next)
{
	if (next == OB_D11_STATE_NONE || next <= state)
		return false;
	if (state == OB_D11_CORE_READY &&
		next == OB_D11_ATTACH_IDENTITY_READY)
		return true;
	if (state == OB_D11_ATTACH_IDENTITY_READY &&
		next == OB_D11_UCODE_LOADED)
		return true;
	if (state == OB_D11_UCODE_LOADED &&
		next == OB_D11_PSM_STARTED)
		return true;
	if (state == OB_D11_PSM_STARTED &&
		next == OB_D11_STARTUP_SUSPEND_CONFIRMED)
		return true;
	if (state == OB_D11_STARTUP_SUSPEND_CONFIRMED &&
		next == OB_D11_COMMON_INITVALS_APPLIED)
		return true;
	if (state == OB_D11_COMMON_INITVALS_APPLIED &&
		next == OB_D11_POST_UCODE_READY)
		return true;
	if (state == OB_D11_POST_UCODE_READY &&
		next == OB_D11_TAIL_FIXUP_APPLIED)
		return true;
	if (state == OB_D11_TAIL_FIXUP_APPLIED &&
		next == OB_D11_DMA_PROGRAMMED)
		return true;
	if (state == OB_D11_DMA_PROGRAMMED &&
		next == OB_D11_TAIL_COMPLETE)
		return true;
	return false;
}

static inline bool ob_d11_lifecycle_advance(
	enum ob_d11_lifecycle_state *state,
	enum ob_d11_lifecycle_state next)
{
	if (!state || !ob_d11_lifecycle_step_allowed(*state, next))
		return false;
	*state = next;
	return true;
}

/* True only for the three states appended by M4. */
static inline bool ob_d11_lifecycle_is_tail_state(
	enum ob_d11_lifecycle_state state)
{
	return state == OB_D11_TAIL_FIXUP_APPLIED ||
	       state == OB_D11_DMA_PROGRAMMED ||
	       state == OB_D11_TAIL_COMPLETE;
}

/*
 * Normative mapping from the Phase 27 tail stop point to the lifecycle state
 * that may be published only after that stage fully succeeded.  DISABLED runs
 * no stage and therefore leaves the caller in POST_UCODE_READY.  An invalid
 * stop value maps to NONE, which can never be published.
 */
static inline enum ob_d11_lifecycle_state
ob_d11_lifecycle_state_for_tail_stop(enum ob_d11_tail_stop stop)
{
	switch (stop) {
	case OB_D11_TAIL_STOP_DISABLED:
		return OB_D11_POST_UCODE_READY;
	case OB_D11_TAIL_STOP_AFTER_FIXUP:
		return OB_D11_TAIL_FIXUP_APPLIED;
	case OB_D11_TAIL_STOP_AFTER_DMA:
		return OB_D11_DMA_PROGRAMMED;
	case OB_D11_TAIL_STOP_COMPLETE:
		return OB_D11_TAIL_COMPLETE;
	}
	return OB_D11_STATE_NONE;
}

/*
 * Failure evidence for the D11 tail.  A failed stage never advances the
 * lifecycle state; it records the last stage that was attempted.  Stages that
 * issued writes before failing leave the hardware state unknown, because no
 * vendor recovery script exists (Phase 29 M3).  D2B_GATE runs before the first
 * tail write and is therefore the only non-unknown failure.
 */
enum ob_d11_stage_failure {
	OB_D11_FAIL_NONE = 0,
	OB_D11_FAIL_D2B_GATE,
	OB_D11_FAIL_FIXUP,
	OB_D11_FAIL_T1,
	OB_D11_FAIL_DMA,
	OB_D11_FAIL_T2,
	OB_D11_FAIL_VALIDATE,
	OB_D11_FAIL_DEVICE_LOST,
};

static inline bool ob_d11_failure_stage_valid(enum ob_d11_stage_failure s)
{
	switch (s) {
	case OB_D11_FAIL_NONE:
	case OB_D11_FAIL_D2B_GATE:
	case OB_D11_FAIL_FIXUP:
	case OB_D11_FAIL_T1:
	case OB_D11_FAIL_DMA:
	case OB_D11_FAIL_T2:
	case OB_D11_FAIL_VALIDATE:
	case OB_D11_FAIL_DEVICE_LOST:
		return true;
	}
	return false;
}

/* Fail closed: an unknown/invalid stage is treated as unknown hardware. */
static inline bool ob_d11_failure_hardware_state_unknown(
	enum ob_d11_stage_failure s)
{
	switch (s) {
	case OB_D11_FAIL_NONE:
	case OB_D11_FAIL_D2B_GATE:
		return false;
	case OB_D11_FAIL_FIXUP:
	case OB_D11_FAIL_T1:
	case OB_D11_FAIL_DMA:
	case OB_D11_FAIL_T2:
	case OB_D11_FAIL_VALIDATE:
	case OB_D11_FAIL_DEVICE_LOST:
		return true;
	}
	return true;
}

static inline bool ob_d11_failure_reboot_required(
	enum ob_d11_stage_failure s)
{
	return ob_d11_failure_hardware_state_unknown(s);
}

/*
 * Per-device lifecycle record.  Every field is device-local: two adapters in
 * one system must never share a tail state or a fatal latch.
 */
struct ob_d11_lifecycle {
	enum ob_d11_lifecycle_state state;
	enum ob_d11_stage_failure failure_stage;
	int failure_errno;
	/* A failed postcondition whose hardware state is unknown. */
	bool fatal_evidence;
	bool hardware_state_unknown;
	bool reboot_required;
};

static inline void ob_d11_lifecycle_init(struct ob_d11_lifecycle *lc)
{
	if (!lc)
		return;
	lc->state = OB_D11_STATE_NONE;
	lc->failure_stage = OB_D11_FAIL_NONE;
	lc->failure_errno = 0;
	lc->fatal_evidence = false;
	lc->hardware_state_unknown = false;
	lc->reboot_required = false;
}

/*
 * Publish a state only after its stage succeeded.  Any recorded failure blocks
 * progression until a non-fatal record is explicitly reset.
 */
static inline bool ob_d11_lifecycle_publish(struct ob_d11_lifecycle *lc,
					    enum ob_d11_lifecycle_state next)
{
	if (!lc || lc->fatal_evidence ||
	    lc->failure_stage != OB_D11_FAIL_NONE)
		return false;
	if (!ob_d11_lifecycle_step_allowed(lc->state, next))
		return false;
	lc->state = next;
	return true;
}

/*
 * Record a failed stage.  The first fatal record is preserved; later failures
 * cannot overwrite the evidence that made the hardware state unknown.
 */
static inline bool ob_d11_lifecycle_fail(struct ob_d11_lifecycle *lc,
					 enum ob_d11_stage_failure stage,
					 int err)
{
	if (!lc || stage == OB_D11_FAIL_NONE ||
	    !ob_d11_failure_stage_valid(stage))
		return false;
	if (lc->fatal_evidence)
		return false;
	lc->failure_stage = stage;
	lc->failure_errno = err;
	lc->hardware_state_unknown =
		ob_d11_failure_hardware_state_unknown(stage);
	lc->reboot_required = ob_d11_failure_reboot_required(stage);
	lc->fatal_evidence = lc->hardware_state_unknown;
	return true;
}

/*
 * Reset a device for a new probe.  Refused while fatal evidence is latched: a
 * FIFO/DMA failure may not be cleared by a software reset or module reload.
 */
static inline bool ob_d11_lifecycle_reset(struct ob_d11_lifecycle *lc)
{
	if (!lc || lc->fatal_evidence)
		return false;
	ob_d11_lifecycle_init(lc);
	return true;
}

struct ob_hw;

/* Normal-path D11 bootstrap.  It stops before PHY/radio/DMA/IRQ/mac80211. */
int ob_d11_post_ucode_init(struct ob_hw *hw);
int ob_d11_suspend_mac_and_wait(struct ob_hw *hw);
int ob_d11_quiesce_for_radio(struct ob_hw *hw);
int ob_d11_enable_mac(struct ob_hw *hw);
/*
 * Vendor wlc_phyreg_enter/exit @0x0bafd1 / 0x0bafac on top of the M1-6D wake
 * model: a refcount on pi+0x113d whose 0->1 transition sets override mask 2
 * and whose 1->0 transition clears it.  With another override active (the
 * suspend bracket always has mask 4), both calls are MMIO-free, exactly as the
 * vendor's are; the wait only runs when this call itself creates the 0 -> 1
 * transition of the bitmap, and it is the bounded, non-failing spin described
 * above.
 */
int ob_d11_phyreg_enter(struct ob_hw *hw);
int ob_d11_phyreg_exit(struct ob_hw *hw);
/* Vendor wlc_ucode_wake_override_set/clear @0x0639c3 / 0x0605f6. */
int ob_d11_wake_override_set(struct ob_hw *hw, u32 mask);
int ob_d11_wake_override_clear(struct ob_hw *hw, u32 mask);
/*
 * Suspend bracket: wlc_bmac_suspend_mac_and_wait sets mask 4 on its 0->1
 * suspend refcount even when EN_MAC is already clear, and wlc_bmac_enable_mac
 * clears it again.  The calibration bracket opens with enter and lets the
 * resume path clear it; a MAC the stage leaves suspended keeps the override
 * active, which is the vendor state after a suspend that was never resumed.
 */
int ob_d11_suspend_wake_override_enter(struct ob_hw *hw);
int ob_d11_suspend_wake_override_leave(struct ob_hw *hw);
int ob_acphy_attach_identity(struct ob_hw *hw);

#endif /* _OB_LIFECYCLE_H_ */

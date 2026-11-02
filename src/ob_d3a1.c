// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — isolated D3A1 vendor post-common / pre-PHY D11 tail test
 * (M3.4D3A1).
 *
 * Module param d11_tail_test_only=1. From the shared hardware-proven D2B exit
 * this reproduces the exact vendor BCM4352/rev42 ordering:
 *
 *   sub_67efd equivalent
 *     -> T1 (MBURST/MAXANTCNT, intrcvlazy, MACCONTROL, TSF, IRQ source,
 *            macphyclk, fast-pwrup, MACHW_VER/CAP, SCR SRL/LRL, SFBL/LFBL,
 *            IFS)
 *     -> DMA (reusing the hardware-proven D3A0 lifecycle, in its exact vendor
 *            position: T1 -> DMA -> T2)
 *     -> T2 (BTC/NVRAM gate, the 6-byte MAC into SHM 0x78c/0x78e/0x790,
 *            switch_macfreq)
 *     -> STOP immediately before sub_6656c.
 *
 * It NEVER enters sub_6656c, bsinitvals, wlc_phy_init, PHY indirect MMIO,
 * radio, channel, calibration, EN_MAC, host IRQ delivery or mac80211. It never
 * enables MACINTMASK or MI_DMAINT.
 *
 * Fail-closed: the DMA lifecycle is the proven D3A0 one; DMA memory is freed
 * only after EVERY programmed engine had its own verified normal per-channel
 * stop. Core-reset containment never authorizes a free.
 *
 * Provenance: docs/m34d3a1_vendor_tail.md (C2 blob sha256 352a6e349f…; C3
 * upstream brcmsmac corroboration).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/bcma/bcma.h>
#include <linux/bcma/bcma_regs.h>

#include "ob_core.h"
#include "ob_dma.h"
#include "ob_si.h"
#include "ob_ucode.h"
#include "ob_initvals.h"
#include "ob_d3a0.h"
#include "ob_d3a1.h"

/* ---- objmem / SHM / SCR 16-bit accessors -------------------------------- */

/*
 * Shared object-memory writer for both windows (SHM and SCR).
 *
 * The vendor sequence is preserved exactly: select OBJADDR -> readback
 * (barrier) -> 16-bit OBJDATA write, same offsets, same width, same order.
 * What changes is the readback: it is the authoritative all-ones test defined
 * by src/ob_guard.c (ob_d11_read32_trusted), so a dead D11 window is detected
 * at the select step and the dependent OBJDATA write is never issued.  A
 * non-zero return means NOTHING was written.
 *
 * The existing ob_ucode_write_shm16_checked() cannot be reused here: it
 * hardcodes the SHM selector, and SCR needs 0x00020000.  Substituting the SHM
 * selector would silently corrupt the SCR window, so the selector stays a
 * parameter and only the trust policy is shared.
 */
static int ob_d3a1_obj_write16(struct ob_hw *hw, u32 sel, u16 off, u16 val)
{
	if (!hw)
		return -EINVAL;
	if (hw->dev_lost)
		return -ENODEV;

	ob_d11_write32(hw, OB_D3A1_REG_OBJADDR, sel | ((u32)off >> 2));
	if (hw->dev_lost)
		return -ENODEV;
	(void)ob_d11_read32_trusted(hw, "D3A1 object OBJADDR readback",
				    OB_D3A1_REG_OBJADDR);
	if (hw->dev_lost)
		return -ENODEV;

	ob_d11_write16(hw, OB_D3A1_REG_OBJDATA + (off & 0x2), val);
	return hw->dev_lost ? -ENODEV : 0;
}

static int ob_d3a1_write_shm16(struct ob_hw *hw, u16 off, u16 val)
{
	return ob_d3a1_obj_write16(hw, OB_D3A1_OBJADDR_SHM_SEL, off, val);
}

static int ob_d3a1_write_scr16(struct ob_hw *hw, u16 off, u16 val)
{
	return ob_d3a1_obj_write16(hw, OB_D3A1_OBJADDR_SCR_SEL, off, val);
}

static u16 ob_d3a1_read_obj16(struct ob_hw *hw, u32 sel, u16 off)
{
	ob_d11_write32(hw, OB_D3A1_REG_OBJADDR, sel | ((u32)off >> 2));
	(void)ob_d11_read32(hw, OB_D3A1_REG_OBJADDR);
	if (off & 0x2)
		return ob_d11_read16(hw, OB_D3A1_REG_OBJDATA + 2);
	return ob_d11_read16(hw, OB_D3A1_REG_OBJDATA);
}

static u16 ob_d3a1_read_scr16(struct ob_hw *hw, u16 off)
{
	return ob_d3a1_read_obj16(hw, OB_D3A1_OBJADDR_SCR_SEL, off);
}

/*
 * SICF_MPCLKE lives in the D11 core agent IO control (bit 4). Identical to the
 * proven D3A0 helper (reversible core-cflags gate; no PHY/radio/PLL).
 */
static void ob_d3a1_macphyclk_set(struct ob_hw *hw, bool on)
{
	u32 v = ob_axi_read32(hw, BCMA_IOCTL);

	if (on)
		v |= OB_D3A0_IOCTL_MPCLKE;
	else
		v &= ~OB_D3A0_IOCTL_MPCLKE;
	ob_axi_write32(hw, BCMA_IOCTL, v);
	(void)ob_axi_read32(hw, BCMA_IOCTL);
}

static u32 ob_d3a1_mctrl_update(struct ob_hw *hw, u32 mask, u32 val)
{
	u32 old = ob_d11_read32(hw, OB_D3A1_REG_MACCONTROL);
	u32 new = (old & ~mask) | val;

	ob_d11_write32(hw, OB_D3A1_REG_MACCONTROL, new);
	return ob_d11_read32(hw, OB_D3A1_REG_MACCONTROL);
}

/* ---- bounded FIFO completion poll (vendor bound 0xd1 / step 10) --------- */

/*
 * Bounded vendor poll. Returns 0 when the exact vendor completion predicate was
 * observed true; -ETIMEDOUT when the vendor bound expired first. The final
 * register value and the iteration count are reported so an expiry is
 * diagnosable. The predicate is NOT shared between registers: 0x530 completes
 * on the whole word reading 0, 0x540 on bit0 clear (see ob_d3a1.h).
 *
 * The vendor blob has no error path for these polls: on bound expiry it falls
 * through and continues. OpenBRCM deliberately does not (M3, fail-closed):
 * every caller treats a non-zero result as a failed postcondition and aborts
 * the tail. This helper only reports the poll result; the abort policy lives
 * in ob_d3a1_fifo_fixup() and is defined by ob_d3a1_poll_expired_policy().
 */
/* ---- backend adapters: the only place the shared poll touches hardware -- */

static u16 ob_d3a1_poll_read(void *ctx, u16 off)
{
	return ob_d11_read16((struct ob_hw *)ctx, off);
}

static void ob_d3a1_poll_write(void *ctx, u16 off, u16 val)
{
	ob_d11_write16((struct ob_hw *)ctx, off, val);
}

static void ob_d3a1_poll_delay(u32 us)
{
	udelay(us);
}

static bool ob_d3a1_poll_dev_lost(void *ctx)
{
	return ((const struct ob_hw *)ctx)->dev_lost;
}

/*
 * The loop, the vendor bound and the per-observation validity test all live in
 * ob_d3a1.h (ob_d3a1_poll_core), so the kernel path and the host integration
 * tests run the same code against different backends. This adapter supplies
 * MMIO and the delay only; it adds no policy.
 */
static enum ob_d3a1_poll_result
ob_d3a1_poll16(struct ob_hw *hw, u16 off, enum ob_d3a1_poll_kind kind,
	       u32 *iters, u16 *last)
{
	return ob_d3a1_poll_core(ob_d3a1_poll_read, ob_d3a1_poll_delay, hw,
				 off, kind, iters, last);
}

/* ---- sub_67efd equivalent (rev42 only) ---------------------------------- */

/*
 * Per-device FIFO retry prohibition.
 *
 * The latch itself lives in struct ob_d3a1, so a timeout on adapter A cannot
 * block adapter B.  A second software lifetime boundary remains: ob_hw is
 * devres-allocated and disappears on remove, so a same-load reprobe would
 * otherwise create a fresh zeroed struct ob_d3a1 and silently authorize a
 * retry of hardware that was left partially programmed.
 *
 * The registry below closes that boundary.  It is a fixed-size module-lifetime
 * array keyed by the BCMA device's PCI domain/bus/devfn plus the immutable
 * chip and D11 revisions.  It contains only copied scalars: no pointer to
 * ob_hw, no DMA address and no allocation, so it cannot become ownerless.  A
 * matching key refuses probe before devm_kzalloc() and before any MMIO.
 *
 * It is not a recovery mechanism.  The record is never cleared in this module
 * lifetime, and the first failure pins the module so rmmod/insmod cannot clear
 * it either; only a reboot recovers the machine.
 */
#define OB_D3A1_FIFO_FAIL_MAX 8u

struct ob_d3a1_fifo_key {
	u32 domain;
	u32 bus;
	u32 devfn;
	u16 chip_id;
	u8 chip_rev;
	u8 d11_rev;
};

struct ob_d3a1_fifo_failure {
	bool used;
	struct ob_d3a1_fifo_key key;
	u16 timeout_reg;
	u16 timeout_last;
	u32 timeout_iters;
	u32 poll_expired;
	int timeout_index;
};

static struct ob_d3a1_fifo_failure ob_d3a1_fifo_failures[OB_D3A1_FIFO_FAIL_MAX];
static bool ob_d3a1_fifo_registry_poisoned;
static bool ob_d3a1_fifo_module_pinned;
static DEFINE_MUTEX(ob_d3a1_fifo_registry_lock);

static bool ob_d3a1_fifo_key_from_core(const struct bcma_device *core,
				       struct ob_d3a1_fifo_key *key)
{
	struct pci_dev *pdev;

	if (!core || !core->bus || !core->bus->host_pci ||
	    !core->bus->host_pci->bus)
		return false;
	pdev = core->bus->host_pci;
	key->domain = (u32)pci_domain_nr(pdev->bus);
	key->bus = (u32)pdev->bus->number;
	key->devfn = (u32)pdev->devfn;
	key->chip_id = core->bus->chipinfo.id;
	key->chip_rev = core->bus->chipinfo.rev;
	key->d11_rev = core->id.rev;
	return true;
}

static bool ob_d3a1_fifo_key_equal(const struct ob_d3a1_fifo_key *a,
				   const struct ob_d3a1_fifo_key *b)
{
	return a->domain == b->domain && a->bus == b->bus &&
	       a->devfn == b->devfn && a->chip_id == b->chip_id &&
	       a->chip_rev == b->chip_rev && a->d11_rev == b->d11_rev;
}

static void ob_d3a1_fifo_record_locked(const struct ob_d3a1_fifo_key *key,
				       const struct ob_d3a1 *st)
{
	struct ob_d3a1_fifo_failure *slot = NULL;
	u32 i;

	for (i = 0; i < OB_D3A1_FIFO_FAIL_MAX; i++) {
		if (ob_d3a1_fifo_failures[i].used &&
		    ob_d3a1_fifo_key_equal(&ob_d3a1_fifo_failures[i].key, key)) {
			slot = &ob_d3a1_fifo_failures[i];
			break;
		}
	}
	if (!slot) {
		for (i = 0; i < OB_D3A1_FIFO_FAIL_MAX; i++) {
			if (!ob_d3a1_fifo_failures[i].used) {
				slot = &ob_d3a1_fifo_failures[i];
				break;
			}
		}
	}
	if (!slot) {
		/* Fail closed: an unrecordable failure forbids all later probes. */
		ob_d3a1_fifo_registry_poisoned = true;
		return;
	}

	slot->used = true;
	slot->key = *key;
	slot->timeout_reg = st->fifo_timeout_reg;
	slot->timeout_last = st->fifo_timeout_last;
	slot->timeout_iters = st->fifo_timeout_iters;
	slot->poll_expired = st->fifo_poll_expired;
	slot->timeout_index = st->fifo_timeout_index;
}

/* Record a FIFO poll expiry before returning to the probe caller. */
static void ob_d3a1_fifo_failure_record(struct ob_hw *hw)
{
	struct ob_d3a1_fifo_key key;
	bool key_ok, pin = false;

	if (!hw)
		return;
	key_ok = ob_d3a1_fifo_key_from_core(hw->core, &key);
	mutex_lock(&ob_d3a1_fifo_registry_lock);
	/*
	 * Pin the module exactly once, including the unkeyed/poisoned case.
	 * The registry alone only protects the current module load; without
	 * the pin, rmmod/insmod would silently clear the refusal and allow a
	 * reprobe of hardware with partially programmed FIFO state.  The pin
	 * is module-wide, but it does not mark any other device failed.
	 */
	if (!ob_d3a1_fifo_module_pinned) {
		ob_d3a1_fifo_module_pinned = true;
		pin = true;
	}
	if (key_ok)
		ob_d3a1_fifo_record_locked(&key, &hw->d3a1);
	else
		ob_d3a1_fifo_registry_poisoned = true;
	mutex_unlock(&ob_d3a1_fifo_registry_lock);

	if (pin) {
#ifdef MODULE
		__module_get(THIS_MODULE);
#endif
		dev_crit(hw->dev,
			 "d3a1-test: FIFO failure pins the module; rmmod/insmod cannot clear the refusal, reboot required\n");
	}

	if (!key_ok) {
		dev_crit(hw->dev,
			 "d3a1-test: FIFO failure has no stable PCI key; all later probes refused until reboot\n");
		return;
	}
	dev_crit(hw->dev,
		 "d3a1-test: FIFO failure recorded for pci=%08x:%02x:%02x.%u chip=0x%04x/%u d11=%u; reprobe refused until reboot\n",
		 key.domain, key.bus, (key.devfn >> 3) & 0x1f, key.devfn & 0x7,
		 key.chip_id, key.chip_rev, key.d11_rev);
}

/* Called from probe before devm_kzalloc() and before any hardware access. */
bool ob_d3a1_fifo_reprobe_forbidden(struct bcma_device *core)
{
	struct ob_d3a1_fifo_key key;
	struct ob_d3a1_fifo_failure found = { 0 };
	bool poisoned, matched = false;
	u32 i;

	if (!ob_d3a1_fifo_key_from_core(core, &key)) {
		/*
		 * Fail closed.  A probe that cannot form the exact PCI/chip/D11
		 * key cannot prove it is not the failed device, so it must not
		 * be admitted.  The supported target is PCIe BCMA; a missing
		 * host_pci is outside the admitted topology and is refused here
		 * rather than silently bypassing the registry.
		 */
		dev_crit(&core->dev,
			 "d3a1-test: no stable PCI key for probe; refusing until reboot\n");
		return true;
	}

	mutex_lock(&ob_d3a1_fifo_registry_lock);
	poisoned = ob_d3a1_fifo_registry_poisoned;
	if (!poisoned) {
		for (i = 0; i < OB_D3A1_FIFO_FAIL_MAX; i++) {
			if (ob_d3a1_fifo_failures[i].used &&
			    ob_d3a1_fifo_key_equal(
				    &ob_d3a1_fifo_failures[i].key, &key)) {
				found = ob_d3a1_fifo_failures[i];
				matched = true;
				break;
			}
		}
	}
	mutex_unlock(&ob_d3a1_fifo_registry_lock);

	if (poisoned) {
		dev_crit(&core->dev,
			 "d3a1-test: FIFO registry exhausted/unkeyed; refusing probe until reboot\n");
		return true;
	}
	if (matched) {
		dev_crit(&core->dev,
			 "d3a1-test: refusing reprobe of pci=%08x:%02x:%02x.%u: prior FIFO expiry reg=0x%04x last=%04x reads=%u index=%d; reboot required\n",
			 found.key.domain, found.key.bus,
			 (found.key.devfn >> 3) & 0x1f,
			 found.key.devfn & 0x7, found.timeout_reg,
			 found.timeout_last, found.timeout_iters,
			 found.timeout_index);
		return true;
	}
	return false;
}

/*
 * ---- 0x530 stage callbacks ----------------------------------------------
 *
 * ops->ctx is `struct ob_hw *` and nothing else. Every kernel callback - the
 * MMIO adapters above and these two log helpers - must interpret it the same
 * way, so there is deliberately no wrapper struct: passing one while the
 * adapters cast to struct ob_hw * is a type confusion that would use the
 * wrapper's first bytes as ob_hw.core and issue MMIO at a bogus base.
 * tests/host/test_d3a1_fifo530_ctx.py pins this discipline.
 */

static void ob_d3a1_fifo530_log_complete(void *ctx, u16 index, u16 programmed,
					 u16 readback, u32 iters)
{
	struct ob_hw *hw = ctx;

	dev_info(hw->dev,
		 "d3a1-test: sub_67efd 0x530[%u] complete programmed=%04x readback=%04x reads=%u\n",
		 index, programmed, readback, iters);
}

static void ob_d3a1_fifo530_log_continued(void *ctx, u16 index, u16 programmed,
					  u16 last, u16 post, u32 iters)
{
	struct ob_hw *hw = ctx;

	dev_warn(hw->dev,
		 "d3a1-test: sub_67efd 0x530[%u] EXPIRED_AND_CONTINUED programmed=%04x last=%04x post=%04x reads=%u (vendor-faithful opt-in; completion NOT verified)\n",
		 index, programmed, last, post, iters);
}

static int ob_d3a1_fifo_fixup(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 machwcap = ob_d11_read32(hw, OB_D3A1_REG_MACHWCAP);
	u16 v = ob_d3a1_fifo_flush_val(machwcap);
	u16 last = 0;
	enum ob_d3a1_poll_result r540;
	u32 i;

	st->machwcap = machwcap;
	st->fifo_last_expired_index = -1;
	hw->d3a1_fifo530.first_continued_index = -1;
	hw->d3a1_fifo530.invalid_index = -1;

	/* 0x542 = v, 0x540 = 5, bounded completion poll on 0x540 bit0 */
	ob_d11_write16(hw, OB_D3A1_REG_XMTFIFOFLUSH, v);
	ob_d11_write16(hw, OB_D3A1_REG_XMTFIFOCMD, 5);
	st->fifo_fixed_writes += OB_D3A1_FIFO_FIXED_WRITES;
	/*
	 * 0x540 is polled with the same per-observation validity rule as 0x530.
	 * Excluding it was wrong: a transient 0xffff followed by a bit0-clear
	 * value (e.g. 0x0004) would satisfy the completion predicate and the tail
	 * would proceed on unverified hardware. An invalid observation is fatal
	 * here too, and is checked before the predicate.
	 */
	r540 = ob_d3a1_poll16(hw, OB_D3A1_REG_XMTFIFOCMD, OB_D3A1_POLL_540,
			      &st->fifo_poll540_iters, &last);
	if (r540 == OB_D3A1_POLL_INVALID) {
		/*
		 * Fatal, not a timeout: the flush command was never acknowledged.
		 * No 7-entry loop, no 42-entry loop, no T1/DMA/T2. Device loss is
		 * latched so every later access is refused, and -EIO keeps this
		 * class apart from the bounded expiry below.
		 */
		hw->d3a1_fifo530.invalid_540++;
		st->fifo_state_unverified = true;
		st->fifo_reboot_required = true;
		st->fifo_retry_forbidden = true;
		st->fifo_timeout_reg = OB_D3A1_REG_XMTFIFOCMD;
		st->fifo_timeout_last = last;
		st->fifo_timeout_iters = st->fifo_poll540_iters;
		st->fifo_timeout_index = -1;
		ob_dev_lost_latch(hw, "d3a1-540-all-ones");
		ob_d3a1_fifo_failure_record(hw);
		dev_crit(hw->dev,
			 "d3a1-test: sub_67efd 0x540 poll INVALID reg=0x%04x programmed=%04x last=%04x reads=%u: all-ones (no slave response), device loss latched, NO further FIFO programming, NO retry, reboot required\n",
			 OB_D3A1_REG_XMTFIFOCMD, 5, last,
			 st->fifo_poll540_iters);
		return -EIO;
	}
	if (r540 != OB_D3A1_POLL_COMPLETE) {
		/*
		 * Fail-closed (M3). The vendor falls through here; OpenBRCM does
		 * not. No further FIFO write is issued, no T1/DMA/T2 follows, and
		 * the resulting hardware state is reported as unverified. DMA has
		 * not been programmed yet, so no DMA teardown is applicable.
		 */
		st->fifo_poll_expired++;
		st->fifo_state_unverified = true;
		st->fifo_reboot_required = true;
		st->fifo_retry_forbidden = true;
		st->fifo_timeout_reg = OB_D3A1_REG_XMTFIFOCMD;
		st->fifo_timeout_last = last;
		st->fifo_timeout_iters = st->fifo_poll540_iters;
		st->fifo_timeout_index = -1;
		ob_d3a1_fifo_failure_record(hw);
		dev_crit(hw->dev,
			 "d3a1-test: sub_67efd 0x540 poll EXPIRED reg=0x%04x programmed=%04x last=%04x reads=%u; FIFO state unverified, no further FIFO programming, no retry, reboot required\n",
			 OB_D3A1_REG_XMTFIFOCMD, 5, last,
			 st->fifo_poll540_iters);
		return -ETIMEDOUT;
	}

	/* exact 7-entry programming loop (42 writes) */
	for (i = 0; i < OB_D3A1_FIFO_TABLE1_LEN; i++) {
		u32 entry = ob_d3a1_fifo_table1(i);

		ob_d11_write16(hw, OB_D3A1_REG_XMTFIFORQPRI,
			     ob_d3a1_fifo7_rqpri(entry, v));
		ob_d11_write16(hw, OB_D3A1_REG_XMTTPLATETXPTR,
			     ob_d3a1_fifo7_txptr(entry));
		ob_d11_write16(hw, OB_D3A1_REG_XMTFIFODEF,
			     ob_d3a1_fifo7_def(entry));
		ob_d11_write16(hw, OB_D3A1_REG_XMT_54E,
			     ob_d3a1_fifo7_54e(entry));
		ob_d11_write16(hw, OB_D3A1_REG_XMTTPLATEPTR,
			     OB_D3A1_FIFO_VAL_TPLATE);
		ob_d11_write16(hw, OB_D3A1_REG_XMTFIFOPRIRDY,
			     ob_d3a1_fifo7_prirdy(entry));
		st->fifo7_writes += 6;
	}

	/*
	 * Exact 42-entry programming loop (168 writes), bounded poll each.
	 *
	 * The control flow lives in ob_d3a1.h (ob_d3a1_fifo530_stage) so the host
	 * integration tests drive the production loop against a mock backend.
	 * This function only maps the stage state onto the per-device statistics,
	 * the failure record and the device-loss latch.
	 *
	 * Predicate is the exact `readw(0x530) == 0`; no weaker mask, no 0x0007
	 * special case. Every one of the 21 observations is checked for all-ones
	 * at the point it occurs, so a transient invalid read cannot be masked by
	 * a later legitimate value (FIFO530_ALL_ONES_ANALYSIS.md).
	 */
	{
		/*
		 * ops->ctx is passed verbatim to every callback and is ALWAYS the
		 * struct ob_hw * itself (never a wrapper - see the callback comment
		 * above and tests/host/test_d3a1_fifo530_ctx.py).
		 */
		struct ob_d3a1_fifo530_ops ops = {
			.read16 = ob_d3a1_poll_read,
			.write16 = ob_d3a1_poll_write,
			.delay = ob_d3a1_poll_delay,
			.dev_lost = ob_d3a1_poll_dev_lost,
			.on_complete = ob_d3a1_fifo530_log_complete,
			.on_expired_continued = ob_d3a1_fifo530_log_continued,
			.ctx = hw,
		};
		struct ob_d3a1_fifo530_state fs;
		u32 k;

		ob_d3a1_fifo530_state_init(&fs, hw->d3a1_fifo530_continue);
		ob_d3a1_fifo530_stage(&ops, &fs);

		/* Every entry the stage entered issued its four writes. */
		st->fifo42_writes += 4u * (u32)fs.entries_entered;
		st->fifo_poll_completed += fs.polls_completed;
		st->fifo_poll_expired += fs.expired;
		hw->d3a1_fifo530.expired_continued += fs.continued;
		st->fifo_poll530_iters += fs.iters_total;
		if (fs.iters_max > st->fifo_poll530_max)
			st->fifo_poll530_max = fs.iters_max;
		for (k = 0; k < 3u; k++) {
			st->fifo_first_rb[k] = fs.first_rb[k];
			st->fifo_first_reads[k] = fs.first_reads[k];
		}
		if (fs.continued) {
			hw->d3a1_fifo530.expiry_continued = true;
			if (hw->d3a1_fifo530.first_continued_index < 0) {
				hw->d3a1_fifo530.first_continued_index =
					fs.first_continued_index;
				hw->d3a1_fifo530.continued_last = fs.continued_last;
				hw->d3a1_fifo530.continued_reads = fs.continued_reads;
			}
		}

		st->fifo_timeout_reg = OB_D3A1_REG_XMT_530;
		st->fifo_timeout_last = fs.failed_last;
		st->fifo_timeout_iters = fs.failed_iters;
		st->fifo_timeout_index = fs.failed_index;

		if (fs.result == OB_D3A1_FIFO530_INVALID) {
			/*
			 * Fatal, and NOT a timeout: an all-ones observation is the
			 * D11 no-slave-response pattern. The stage stopped at that
			 * observation: no post-read, no delay, no further entry
			 * write. Latch device loss so every later access is blocked
			 * by the guard, mark the tail failed and return -EIO. The
			 * experimental continuation policy never sees this class.
			 */
			hw->d3a1_fifo530.invalid_530++;
			hw->d3a1_fifo530.invalid_index = fs.failed_index;
			hw->d3a1_fifo530.invalid_value = fs.failed_last;
			hw->d3a1_fifo530.invalid_iters = fs.failed_iters;
			st->fifo_state_unverified = true;
			st->fifo_reboot_required = true;
			st->fifo_retry_forbidden = true;
			ob_dev_lost_latch(hw, "d3a1-530-all-ones");
			ob_d3a1_fifo_failure_record(hw);
			dev_crit(hw->dev,
				 "d3a1-test: sub_67efd 0x530 INVALID index=%d reg=0x%04x value=%04x reads=%u: all-ones (no slave response), device loss latched, NO post-read, NO further FIFO programming, NO retry, reboot required\n",
				 fs.failed_index, OB_D3A1_REG_XMT_530,
				 fs.failed_last, fs.failed_iters);
			return -EIO;
		}

		if (fs.result == OB_D3A1_FIFO530_EXPIRED_STOP) {
			/*
			 * Fail-closed (M3): record the register, final value,
			 * read count and table index, then abort WITHOUT the
			 * diagnostic readback and without programming any later
			 * entry. No T1/DMA/T2/validation follows.
			 */
			st->fifo_last_expired_index = fs.failed_index;
			st->fifo_state_unverified = true;
			st->fifo_reboot_required = true;
			st->fifo_retry_forbidden = true;
			ob_d3a1_fifo_failure_record(hw);
			dev_crit(hw->dev,
				 "d3a1-test: sub_67efd 0x530 poll EXPIRED index=%d reg=0x%04x programmed=%04x last=%04x reads=%u; FIFO state unverified, no further FIFO programming, no retry, reboot required\n",
				 fs.failed_index, OB_D3A1_REG_XMT_530,
				 ob_d3a1_fifo42_x530((u16)fs.failed_index),
				 fs.failed_last, fs.failed_iters);
			return -ETIMEDOUT;
		}
	}

	/*
	 * A recorded expiry can never be overwritten by a later success: if any
	 * future edit reintroduces a post-loop success path, this guard keeps the
	 * tail failed.  With the fail-closed returns above it is unreachable.
	 */
	if (st->fifo_state_unverified)
		return -ETIMEDOUT;
	/*
	 * Every recorded expiry must have continued under the explicit opt-in;
	 * an expiry that did not continue is a failure that must never be
	 * overwritten by a later success. With the fail-closed returns above
	 * this is unreachable unless the opt-in took every expiry.
	 */
	if (st->fifo_poll_expired != hw->d3a1_fifo530.expired_continued)
		return -ETIMEDOUT;

	if (st->fifo_fixed_writes != OB_D3A1_FIFO_FIXED_WRITES ||
	    st->fifo7_writes != OB_D3A1_FIFO7_WRITES ||
	    st->fifo42_writes != OB_D3A1_FIFO42_WRITES) {
		dev_err(hw->dev,
			"d3a1-test: sub_67efd accounting mismatch fixed=%u loop7=%u loop42=%u\n",
			st->fifo_fixed_writes, st->fifo7_writes,
			st->fifo42_writes);
		return -EIO;
	}

	dev_info(hw->dev,
		 "d3a1-test: sub_67efd machwcap=%08x flush=%04x poll540=%u loop7=%u loop42=%u polls530=%u/%u expired=%u continued=%u invalid530=%u invalid540=%u max_iters=%u\n",
		 machwcap, v, st->fifo_poll540_iters, st->fifo7_writes,
		 st->fifo42_writes, st->fifo_poll_completed,
		 OB_D3A1_FIFO42_ENTRIES, st->fifo_poll_expired,
		 hw->d3a1_fifo530.expired_continued, hw->d3a1_fifo530.invalid_530,
		 hw->d3a1_fifo530.invalid_540, st->fifo_poll530_max);
	return 0;
}

/* ---- D2B exit re-check (live, before the first D3A1 write) -------------- */

static int ob_d3a1_check_d2b_exit(struct ob_hw *hw)
{
	u32 mctrl = ob_d11_read32(hw, OB_D3A1_REG_MACCONTROL);
	u32 macintmask = ob_d11_read32(hw, OB_D3A1_REG_MACINTMASK);
	u32 fs0 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE0);
	u32 fs1 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE1);
	u32 fs2 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE2);
	u32 fs3 = ob_ucode_read_shm16(hw, OB_UCODE_SHM_FIFOSIZE3);
	u32 shm14 = (u32)ob_ucode_read_shm16(hw, OB_INITVALS_SHM14_LO) |
		    ((u32)ob_ucode_read_shm16(hw, OB_INITVALS_SHM14_HI) << 16);

	if (mctrl != OB_INITVALS_MACCONTROL_EXPECTED ||
	    macintmask != OB_INITVALS_MACINTMASK_EXPECTED ||
	    fs0 != OB_INITVALS_FIFOSIZE0_EXPECTED ||
	    fs1 != OB_INITVALS_FIFOSIZE1_EXPECTED ||
	    fs2 != OB_INITVALS_FIFOSIZE2_EXPECTED ||
	    fs3 != OB_INITVALS_FIFOSIZE3_EXPECTED ||
	    shm14 != OB_INITVALS_SHM14_EXPECTED) {
		dev_err(hw->dev,
			"d3a1-test: live D2B exit mismatch maccontrol=%08x macintmask=%08x fifo=%04x/%04x/%04x/%04x shm14=%08x; D3A1 forbidden\n",
			mctrl, macintmask, fs0, fs1, fs2, fs3, shm14);
		return -EIO;
	}
	dev_info(hw->dev,
		 "d3a1-test: D2B exit re-verified maccontrol=%08x macintmask=%08x shm14=%08x\n",
		 mctrl, macintmask, shm14);
	return 0;
}

/* ---- T1 ----------------------------------------------------------------- */

static u16 ob_d3a1_fast_pwrup_delay(struct ob_hw *hw)
{
	/* blob 0x16091: 0x4352, chiprev >= 4 -> 3000 else 1500 */
	return hw->chip_rev >= 4u ? OB_D3A1_FASTPWRUP_REV_GE4
				  : OB_D3A1_FASTPWRUP_REV_LT4;
}

static u16 ob_d3a1_sub5fdca(struct ob_hw *hw)
{
	/* blob sub_5fdca 0x5fe32: phytype 0xb, chipid != 0x4350 -> 0x4b0 */
	if (hw->chip_id == 0x4350u)
		return 0x0200u;
	return OB_D3A1_SUB5FDCA_PHY_AC;
}

static int ob_d3a1_t1(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 mctrl, machwcap;
	u16 dly;
	int ret;

	/*
	 * Fail-closed object-memory programming: a detected loss stops T1 at
	 * once, so the DMA bring-up that follows T1 is never reached.  On a
	 * healthy device the issued sequence is byte-for-byte the previous one.
	 */
	ret = ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MBURST,
				  OB_D3A1_SHM_MBURST_VAL);
	if (ret)
		return ret;
	ret = ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MAXANTCNT,
				  OB_D3A1_SHM_MAXANTCNT_VAL);
	if (ret)
		return ret;

	ob_d11_write32(hw, OB_D3A1_REG_INTRCVLAZY0, OB_D3A1_INTRCVLAZY);

	mctrl = ob_d3a1_mctrl_update(hw, OB_D3A1_MACCONTROL_MASK,
				     OB_D3A1_MACCONTROL_VAL);
	if (mctrl != OB_D3A1_MACCONTROL_EXPECTED) {
		dev_err(hw->dev,
			"d3a1-test: MACCONTROL transition invalid: %08x\n",
			mctrl);
		return -EIO;
	}

	ob_d11_write32(hw, OB_D3A1_REG_TSF_CFPREP, OB_D3A1_TSF_CFPREP);
	ob_d11_write32(hw, OB_D3A1_REG_TSF_CFPSTART, OB_D3A1_TSF_CFPSTART);
	st->tsf_cfpstart_written = true;

	ob_d11_write32(hw, OB_D3A1_REG_MACINTSTATUS, OB_D3A1_MI_GP1);
	ob_d11_write32(hw, OB_D3A1_REG_INTCONTROL0_MASK, OB_D3A1_I_RI);

	ob_d3a1_macphyclk_set(hw, true);

	/* fast-pwrup delay path (rev42 takes both gates) */
	if (hw->core->id.rev > 4u) {
		dly = ob_d3a1_fast_pwrup_delay(hw);
		st->fastpwrup_dly = dly;
		st->fastpwrup_dly_sw = dly;
		ob_d11_write16(hw, OB_D3A1_REG_FASTPWRUP_DLY, dly);
		if (hw->core->id.rev > 0x28u)
			st->fastpwrup_dly_sw = (u16)(dly +
						     ob_d3a1_sub5fdca(hw));
	}

	ret = ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MACHWVER,
				  (u16)hw->core->id.rev);
	if (ret)
		return ret;
	machwcap = ob_d11_read32(hw, OB_D3A1_REG_MACHWCAP);
	if (hw->core->id.rev > 0xcu) {
		ret = ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MACHWCAP_L,
					  (u16)(machwcap & 0xffffu));
		if (ret)
			return ret;
		ret = ob_d3a1_write_shm16(hw, OB_D3A1_SHM_MACHWCAP_H,
					  (u16)((machwcap >> 16) & 0xffffu));
		if (ret)
			return ret;
	}

	/*
	 * SCR SRL/LRL (vendor never writes SCR 0x24 on the first init: the
	 * wlc_info+0x718 first-init gate skips it).
	 */
	ret = ob_d3a1_write_scr16(hw, OB_D3A1_SCR_SRL, OB_D3A1_SRL_DEFAULT);
	if (ret)
		return ret;
	ret = ob_d3a1_write_scr16(hw, OB_D3A1_SCR_LRL, OB_D3A1_LRL_DEFAULT);
	if (ret)
		return ret;

	ret = ob_d3a1_write_shm16(hw, OB_D3A1_SHM_SFBL, OB_D3A1_SFBL_DEFAULT);
	if (ret)
		return ret;
	ret = ob_d3a1_write_shm16(hw, OB_D3A1_SHM_LFBL, OB_D3A1_LFBL_DEFAULT);
	if (ret)
		return ret;

	if (hw->core->id.rev > 0xfu) {
		u16 ifs = ob_d11_read16(hw, OB_D3A1_REG_IFS_CTL) &
			  OB_D3A1_IFS_CTL_MASK;

		ob_d11_write16(hw, OB_D3A1_REG_IFS_CTL, ifs);
		ob_d11_write16(hw, OB_D3A1_REG_IFS_AIFSN,
			     OB_D3A1_IFS_AIFSN_VAL);
	}

	if (!ob_d3a0_host_irq_disabled(
		    ob_d11_read32(hw, OB_D3A1_REG_MACINTMASK))) {
		dev_err(hw->dev, "d3a1-test: macintmask not 0 after T1\n");
		return -EIO;
	}
	dev_info(hw->dev,
		 "d3a1-test: T1 complete maccontrol=%08x intrcvlazy=%08x intmask0=%08x fastpwrup=%u\n",
		 mctrl, ob_d11_read32(hw, OB_D3A1_REG_INTRCVLAZY0),
		 ob_d11_read32(hw, OB_D3A1_REG_INTCONTROL0_MASK),
		 st->fastpwrup_dly);
	return 0;
}

/* ---- switch_macfreq (blob 0x64cbf, 0x4352 path) ------------------------- */

static u32 ob_d3a1_pmu_pll_read(struct ob_hw *hw, u32 idx)
{
	ob_si_cc_write(hw, OB_CC_PMU_PLLCTL_ADDR, idx);
	return ob_si_cc_read(hw, OB_CC_PMU_PLLCTL_DATA);
}

/*
 * BB VCO frequency (blob si_pmu_get_bb_vcofreq 0x14b7b, 0x4352 branch).
 * Exact: reads PMU PLL2 (and PLL3 when the PLL2 sub-field (bits 4..6) is
 * non-zero), then applies the recovered arithmetic via the ported
 * ob_d3a1_bb_vcofreq_from_pll(). No approximation.
 *
 * Note: the vendor reads PLL3 into `ecx` without masking; the recovered value
 * is used directly by bcm_uint64_multiple_add.
 *
 * The raw inputs and the decoded PLL2 fields are returned for first-run
 * observability; no extra PMU reads are performed for logging.
 */
static u32 ob_d3a1_bb_vcofreq(struct ob_hw *hw, u32 *p2_out, u32 *p3_out,
			      bool *p3_read_out, u32 *d_out, u32 *den_out)
{
	u32 p2 = ob_d3a1_pmu_pll_read(hw, 2);
	u32 p3 = 0;
	bool p3_read = false;

	if (((p2 >> 4) & 0x7u) != 0) {
		p3 = ob_d3a1_pmu_pll_read(hw, 3);
		p3_read = true;
	}

	*p2_out = p2;
	*p3_out = p3;
	*p3_read_out = p3_read;
	*d_out = (p2 >> 4) & 0x7u;
	*den_out = p2 >> 7;
	return ob_d3a1_bb_vcofreq_from_pll(p2, p3);
}

static int ob_d3a1_switch_macfreq(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 p2, p3 = 0, d = 0, den = 0;
	u32 vco, frac, frac_lo, frac_hi;
	bool p3_read = false;
	u16 rb_lo, rb_hi;

	/*
	 * Defensive entry check FIRST. ob_d3a1_bb_vcofreq() below dereferences
	 * hw->cc through ob_si_cc_read/write; a check after the call would not
	 * protect the dereference. ob_d3a1_test() also enforces this invariant
	 * at entry, but keep the local guard.
	 */
	if (!hw->cc) {
		dev_err(hw->dev,
			"d3a1-test: no ChipCommon core; cannot derive BB VCO\n");
		return -ENODEV;
	}

	vco = ob_d3a1_bb_vcofreq(hw, &p2, &p3, &p3_read, &d, &den);

	/*
	 * vco <= 1 is NOT a legitimate vendor skip: bcm_uint64_divide writes
	 * nothing for div <= 1 (the vendor would then publish an uninitialized
	 * stack value). Our inability to derive a usable VCO is an ERROR and
	 * must never become a D3A1 PASS.
	 */
	if (vco <= 1u) {
		dev_err(hw->dev,
			"d3a1-test: switch_macfreq cannot derive a valid BB VCO (vco=%u p2=%08x p3=%08x d=%u den=%u); D3A1 FAIL\n",
			vco, p2, p3, d, den);
		return -EIO;
	}

	frac = ob_d3a1_tsf_frac(vco);
	if (frac == OB_D3A1_DIV_NO_WRITE) {
		dev_err(hw->dev,
			"d3a1-test: switch_macfreq divide produced no write (vco=%u)\n",
			vco);
		return -EIO;
	}

	/* First-run observability: record exactly what the algorithm read. */
	st->bb_vcofreq = vco;
	st->pll2_raw = p2;
	st->pll3_raw = p3;
	st->pll3_read = p3_read;
	st->bb_d = d;
	st->bb_den = den;
	st->tsf_frac = frac;
	st->tsf_frac_lo = ob_d3a1_tsf_frac_lo(frac);
	st->tsf_frac_hi = ob_d3a1_tsf_frac_hi(frac);

	frac_lo = st->tsf_frac_lo;
	frac_hi = st->tsf_frac_hi;
	ob_d11_write16(hw, OB_D3A1_REG_TSF_FRAC_L, frac_lo);
	ob_d11_write16(hw, OB_D3A1_REG_TSF_FRAC_H, frac_hi);

	/*
	 * Stable readback of D11 0x62e/0x630 is NOT vendor-proven, so this is
	 * observability only: record the readback and classify it as unproven.
	 * Do NOT invent an equality gate.
	 */
	rb_lo = ob_d11_read16(hw, OB_D3A1_REG_TSF_FRAC_L);
	rb_hi = ob_d11_read16(hw, OB_D3A1_REG_TSF_FRAC_H);
	st->tsf_frac_lo_rb = rb_lo;
	st->tsf_frac_hi_rb = rb_hi;
	st->tsf_frac_rb_proven = false;

	dev_info(hw->dev,
		 "d3a1-test: switch_macfreq p2=%08x p3=%08x pll3_read=%d d=%u den=%u vco=%u frac=%08x -> 0x62e=%04x 0x630=%04x rb_lo=%04x rb_hi=%04x (readback UNPROVEN)\n",
		 p2, p3, p3_read, d, den, vco, frac, frac_lo, frac_hi,
		 rb_lo, rb_hi);
	return 0;
}

/* ---- T2 ----------------------------------------------------------------- */

static int ob_d3a1_t2(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u16 shm92 = ob_ucode_read_shm16(hw, OB_D3A1_SHM_BTC_BASE);
	u32 btc_base = ob_d3a1_btc_base(shm92);
	u32 i;
	int ret;

	st->btc_shm92 = shm92;
	st->btc_base = btc_base;
	dev_info(hw->dev,
		 "d3a1-test: T2 BTC gate SHM[0x92]=%04x btc_base=%u block=%s\n",
		 shm92, btc_base, btc_base ? "run" : "skip");

	/*
	 * Vendor gate: btc_base == 0 skips the ENTIRE BTC block (btc_params,
	 * the 4 fixed 0x4352 extras, btc_flags AND the MAC SHM writes) because
	 * the vendor branch at 0x69336 jumps past all of them to 0x694d8.
	 */
	if (btc_base != 0) {
		st->btc_block_ran = true;

		/*
		 * btc_params0..118: OpenBRCM has no NVRAM text-variable
		 * provider and the SPROM does not emit these keys, so every
		 * getvar("btc_params%d") returns NULL and the vendor SKIPS the
		 * write (never zero-fills). No write is issued here.
		 */

		if (hw->chip_id == 0x4352u || hw->chip_id == 0xa8dcu) {
			ret = ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF),
				OB_D3A1_BTC_EXTRA_VAL1);
			if (ret)
				return ret;
			ret = ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF2),
				OB_D3A1_BTC_EXTRA_VAL2);
			if (ret)
				return ret;
			ret = ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF3),
				OB_D3A1_BTC_EXTRA_VAL3);
			if (ret)
				return ret;
			ret = ob_d3a1_write_shm16(hw,
				(u16)(btc_base + OB_D3A1_BTC_EXTRA_OFF4),
				OB_D3A1_BTC_EXTRA_VAL4);
			if (ret)
				return ret;
		}

		/*
		 * btc_flags: no provider -> absent -> the vendor skips the
		 * btc->flags store and every wlc_bmac_mhf write. No synthesized
		 * flags.
		 */

		/*
		 * MAC into SHM 0x78c/0x78e/0x790 (rev42 > 0x27), inside the
		 * btc_base gate. Requires a validated MAC.
		 */
		if (hw->core->id.rev > 0x27u && hw->mac_valid) {
			for (i = 0; i < OB_D3A1_MAC_WORDS; i++) {
				ret = ob_d3a1_write_shm16(hw,
					(u16)(OB_D3A1_SHM_MAC_0 + 2u * i),
					ob_d3a1_mac_word(hw->mac, i));
				if (ret)
					return ret;
			}
			st->mac_written = true;
			dev_info(hw->dev,
				 "d3a1-test: MAC words -> 0x78c=%04x 0x78e=%04x 0x790=%04x\n",
				 ob_d3a1_mac_word(hw->mac, 0),
				 ob_d3a1_mac_word(hw->mac, 1),
				 ob_d3a1_mac_word(hw->mac, 2));
		}
	} else {
		dev_info(hw->dev,
			 "d3a1-test: btc_base=0 (SHM[0x92]=0); complete BTC T2 block skipped\n");
	}

	/* vendor read-only diagnostic of SHM 0x8e */
	(void)ob_ucode_read_shm16(hw, OB_D3A1_SHM_BTC_READBACK);

	dev_info(hw->dev,
		 "d3a1-test: T2 complete shm92=%04x btc_base=%u btc_block_ran=%d mac_written=%d\n",
		 st->btc_shm92, st->btc_base, st->btc_block_ran,
		 st->mac_written);

	if (ob_d3a1_chip_uses_macfreq(hw->chip_id))
		return ob_d3a1_switch_macfreq(hw);
	return 0;
}

/* ---- postcondition validation ------------------------------------------- */

static int ob_d3a1_validate(struct ob_hw *hw)
{
	struct ob_d3a1 *st = &hw->d3a1;
	u32 mctrl, macintmask;
	u32 intrcvlazy, intmask0, machwcap;
	u32 ioc, mburst, maxant, machwver, capl, caph;
	u32 rx_control, rx_high, rx_s0, rx_s1;
	u32 i;
	int ret = 0;

	/*
	 * Device-lost fail-safe: read the two trusted direct D11 control
	 * registers FIRST; an all-ones value latches the central device-lost
	 * state and skips the rest of the validation (no further MMIO).
	 */
	mctrl = ob_d11_read32_trusted(hw, "D3A1 validate MACCONTROL",
				      OB_D3A1_REG_MACCONTROL);
	if (hw->dev_lost)
		return -EIO;
	macintmask = ob_d11_read32_trusted(hw, "D3A1 validate MACINTMASK",
					   OB_D3A1_REG_MACINTMASK);
	if (hw->dev_lost)
		return -EIO;

	intrcvlazy = ob_d11_read32(hw, OB_D3A1_REG_INTRCVLAZY0);
	intmask0 = ob_d11_read32(hw, OB_D3A1_REG_INTCONTROL0_MASK);
	machwcap = ob_d11_read32(hw, OB_D3A1_REG_MACHWCAP);
	ioc = ob_axi_read32(hw, BCMA_IOCTL);
	mburst = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MBURST);
	maxant = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MAXANTCNT);
	machwver = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MACHWVER);
	capl = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MACHWCAP_L);
	caph = ob_ucode_read_shm16(hw, OB_D3A1_SHM_MACHWCAP_H);
	rx_control = ob_d11_read32(hw, OB_D11_RX_CONTROL);
	rx_high = ob_d11_read32(hw, OB_D11_RX_ADDRHIGH);
	rx_s0 = ob_d11_read32_trusted(hw, "D3A1 validate RX_STATUS0",
				      OB_D11_RX_STATUS0);
	rx_s1 = ob_d11_read32(hw, OB_D11_RX_STATUS1);
	if (hw->dev_lost)
		return -EIO;

	if (mctrl != OB_D3A1_MACCONTROL_EXPECTED) {
		dev_err(hw->dev, "d3a1-test: MACCONTROL=%08x expected %08x\n",
			mctrl, OB_D3A1_MACCONTROL_EXPECTED);
		ret = -EIO;
	}
	if (!ob_d3a0_host_irq_disabled(macintmask)) {
		dev_err(hw->dev, "d3a1-test: MACINTMASK=%08x not 0\n",
			macintmask);
		ret = -EIO;
	}
	if (intrcvlazy != OB_D3A1_INTRCVLAZY ||
	    !ob_d3a0_irq_source_ok(intmask0)) {
		dev_err(hw->dev,
			"d3a1-test: IRQ source mismatch intrcvlazy=%08x intmask0=%08x\n",
			intrcvlazy, intmask0);
		ret = -EIO;
	}
	if (mburst != OB_D3A1_SHM_MBURST_VAL ||
	    maxant != OB_D3A1_SHM_MAXANTCNT_VAL) {
		dev_err(hw->dev,
			"d3a1-test: MBURST=%04x MAXANTCNT=%04x\n",
			mburst, maxant);
		ret = -EIO;
	}
	if (machwver != (u16)hw->core->id.rev ||
	    capl != (u16)(machwcap & 0xffffu) ||
	    caph != (u16)((machwcap >> 16) & 0xffffu)) {
		dev_err(hw->dev,
			"d3a1-test: MACHW_VER=%04x CAP=%04x/%04x machwcap=%08x\n",
			machwver, capl, caph, machwcap);
		ret = -EIO;
	}
	if (!(ioc & OB_D3A0_IOCTL_MPCLKE)) {
		dev_err(hw->dev, "d3a1-test: SICF_MPCLKE not enabled\n");
		ret = -EIO;
	}

	/* Deterministic T1 register readbacks (exact equality). */
	{
		u32 cfprep = ob_d11_read32(hw, OB_D3A1_REG_TSF_CFPREP);
		u16 fastp = ob_d11_read16(hw, OB_D3A1_REG_FASTPWRUP_DLY);
		u16 ifs_ctl = ob_d11_read16(hw, OB_D3A1_REG_IFS_CTL);
		u16 ifs_aifsn = ob_d11_read16(hw, OB_D3A1_REG_IFS_AIFSN);

		if (cfprep != OB_D3A1_TSF_CFPREP) {
			dev_err(hw->dev,
				"d3a1-test: tsf_cfprep=%08x expected %08x\n",
				cfprep, OB_D3A1_TSF_CFPREP);
			ret = -EIO;
		}
		/*
		 * tsf_cfpstart (0x18c) has NO stable readback: the vendor and
		 * upstream only write it and read the CFP value at 0x604/0x606.
		 * A 2026-09 hardware attempt read 0x3c000000 instead of the
		 * programmed 0x02000000. The deterministic postcondition is
		 * write-accounting only; the raw reads are diagnostics and never
		 * gate PASS. The vendor-exact write value/order are unchanged.
		 */
		if (!st->tsf_cfpstart_written) {
			dev_err(hw->dev,
				"d3a1-test: tsf_cfpstart write not accounted\n");
			ret = -EIO;
		}
		st->tsf_cfpstart_rb = ob_d11_read32(hw,
						  OB_D3A1_REG_TSF_CFPSTART);
		st->tsf_cfpstrt_l_rb = ob_d11_read16(hw, 
						   OB_D3A1_REG_TSF_CFPSTRT_L);
		st->tsf_cfpstrt_h_rb = ob_d11_read16(hw, 
						   OB_D3A1_REG_TSF_CFPSTRT_H);
		st->tsf_cfpstart_rb_proven = false;
		dev_info(hw->dev,
			 "d3a1-test: tsf_cfpstart diag(written=%08x cfpstart_rb=%08x cfpstrt=0x%04x%04x) readback not a postcondition\n",
			 OB_D3A1_TSF_CFPSTART, st->tsf_cfpstart_rb,
			 st->tsf_cfpstrt_h_rb, st->tsf_cfpstrt_l_rb);
		if (fastp != st->fastpwrup_dly) {
			dev_err(hw->dev,
				"d3a1-test: fastpwrup=%04x expected %04x\n",
				fastp, st->fastpwrup_dly);
			ret = -EIO;
		}
		/* ifs_ctl is masked (the vendor clears the high bits); aifsn exact */
		if (ifs_ctl & ~OB_D3A1_IFS_CTL_MASK) {
			dev_err(hw->dev,
				"d3a1-test: ifs_ctl=%04x high bits not cleared\n",
				ifs_ctl);
			ret = -EIO;
		}
		if (ifs_aifsn != OB_D3A1_IFS_AIFSN_VAL) {
			dev_err(hw->dev,
				"d3a1-test: ifs_aifsn=%04x expected %04x\n",
				ifs_aifsn, OB_D3A1_IFS_AIFSN_VAL);
			ret = -EIO;
		}
	}

	/* SRL/LRL (SCR window) and SFBL/LFBL (SHM) exact readbacks. */
	{
		u16 srl = ob_d3a1_read_scr16(hw, OB_D3A1_SCR_SRL);
		u16 lrl = ob_d3a1_read_scr16(hw, OB_D3A1_SCR_LRL);
		u16 sfbl = ob_ucode_read_shm16(hw, OB_D3A1_SHM_SFBL);
		u16 lfbl = ob_ucode_read_shm16(hw, OB_D3A1_SHM_LFBL);

		if (srl != OB_D3A1_SRL_DEFAULT || lrl != OB_D3A1_LRL_DEFAULT) {
			dev_err(hw->dev,
				"d3a1-test: SCR SRL/LRL=%04x/%04x expected %04x/%04x\n",
				srl, lrl, OB_D3A1_SRL_DEFAULT,
				OB_D3A1_LRL_DEFAULT);
			ret = -EIO;
		}
		if (sfbl != OB_D3A1_SFBL_DEFAULT ||
		    lfbl != OB_D3A1_LFBL_DEFAULT) {
			dev_err(hw->dev,
				"d3a1-test: SHM SFBL/LFBL=%04x/%04x expected %04x/%04x\n",
				sfbl, lfbl, OB_D3A1_SFBL_DEFAULT,
				OB_D3A1_LFBL_DEFAULT);
			ret = -EIO;
		}
	}

	/* MAC SHM postcondition only when the vendor would have written it. */
	if (st->mac_written) {
		for (i = 0; i < OB_D3A1_MAC_WORDS; i++) {
			u16 want = ob_d3a1_mac_word(hw->mac, i);
			u16 got = ob_ucode_read_shm16(
					hw, (u16)(OB_D3A1_SHM_MAC_0 + 2u * i));

			if (got != want) {
				dev_err(hw->dev,
					"d3a1-test: MAC SHM[%04x]=%04x expected %04x\n",
					OB_D3A1_SHM_MAC_0 + 2u * i, got, want);
				ret = -EIO;
			}
		}
	}

	/* DMA postconditions (vendor position: T1 -> DMA -> T2). */
	if (rx_control != OB_D3A0_RX_CONTROL ||
	    rx_high != OB_DMA_PCIE_H32 || !ob_d3a0_rx_idle(rx_s0) ||
	    (rx_s1 & OB_D11_RS1_RE_MASK)) {
		dev_err(hw->dev,
			"d3a1-test: RX postcondition fail control=%08x high=%08x status0=%08x status1=%08x\n",
			rx_control, rx_high, rx_s0, rx_s1);
		ret = -EIO;
	}

	if (!ret)
		dev_info(hw->dev,
			 "d3a1-test: postconditions PASS (machwcap=%08x intrcvlazy=%08x rx_control=%08x)\n",
			 machwcap, intrcvlazy, rx_control);
	return ret;
}

/* ---- top-level bring-up ------------------------------------------------- */

/*
 * Write the single failure record (FAILURE_ROLLBACK_MATRIX.md §3).  The
 * original errno is recorded unchanged; the caller returns it as-is and never
 * rewrites a failure into success.  A DMA teardown return is classified, not
 * allowed to mask the stage error.
 */
static void ob_d3a1_fail_record(struct ob_hw *hw, enum ob_d3a1_fail_stage stage,
				int err, bool dma_live,
				enum ob_d3a1_dma_teardown teardown)
{
	struct ob_d3a1 *st = &hw->d3a1;

	st->fail_stage = (u8)stage;
	st->fail_errno = err;
	st->fail_dma_live = dma_live;
	st->fail_dma_teardown = (u8)teardown;
	st->fail_reboot_required =
		st->fifo_reboot_required ||
		(dma_live && teardown != OB_D3A1_TEARDOWN_OK) ||
		stage == OB_D3A1_FAIL_T1 || stage == OB_D3A1_FAIL_T2 ||
		stage == OB_D3A1_FAIL_VALIDATE;
	dev_crit(hw->dev,
		 "d3a1-test: fail record stage=%s errno=%d dma_live=%d teardown=%s reboot_required=%d\n",
		 ob_d3a1_fail_stage_name(stage), err, dma_live,
		 ob_d3a1_dma_teardown_name(teardown),
		 st->fail_reboot_required);
}

/*
 * Software-only preflight for every tail entry.  It reads state that is
 * already in memory and performs NO device access.  A clean result does not
 * prove hardware readiness; it only permits the live D2B-exit re-check below.
 */
static int ob_d11_tail_entry_preflight(struct ob_hw *hw,
				       enum ob_d11_tail_stop stop,
				       const char *tag)
{
	struct ob_d11_tail_entry_guard guard = {
		.dev_lost = hw->dev_lost,
		.dma_fatal = ob_dma_fatal_core_refused(hw->core),
		.fifo_retry_forbidden = hw->d3a1.fifo_retry_forbidden,
		.reboot_required = hw->d3a1.fail_reboot_required,
		.tail_started = hw->d3a1.fifo_fixed_writes != 0,
		/* Conservative: D3A0 DMA resources are owned until a verified
		 * teardown clears pool_created.  A refused teardown sets fatal and
		 * is caught above. */
		.dma_live = hw->d3a0.pool_created,
	};
	enum ob_d11_tail_entry_denial denial =
		ob_d11_tail_entry_check(stop, &guard);

	switch (denial) {
	case OB_D11_TAIL_ENTRY_OK:
		return 0;
	case OB_D11_TAIL_ENTRY_INVALID_STOP:
		dev_err(hw->dev, "%s: invalid D11 tail stop %d; refusing\n",
			tag, (int)stop);
		return -EINVAL;
	case OB_D11_TAIL_ENTRY_INVALID_STATE:
		dev_err(hw->dev, "%s: missing D11 tail entry state; refusing\n",
			tag);
		return -EINVAL;
	case OB_D11_TAIL_ENTRY_DEVICE_LOST:
		dev_crit(hw->dev,
			 "%s: refusing D11 tail after device loss; reboot required\n",
			tag);
		return -EIO;
	case OB_D11_TAIL_ENTRY_DMA_FATAL:
		dev_crit(hw->dev,
			 "%s: refusing D11 tail after an unverified DMA quiesce on this device; reboot required\n",
			tag);
		return -EIO;
	case OB_D11_TAIL_ENTRY_FIFO_LATCHED:
		dev_crit(hw->dev,
			 "%s: refusing D11 tail after a FIFO poll expiry; reboot required\n",
			tag);
		return -EBUSY;
	case OB_D11_TAIL_ENTRY_REBOOT_REQUIRED:
		dev_crit(hw->dev,
			 "%s: refusing D11 tail after a reboot-required stage failure; reboot required\n",
			tag);
		return -EIO;
	case OB_D11_TAIL_ENTRY_ALREADY_STARTED:
		dev_crit(hw->dev,
			 "%s: refusing duplicate D11 tail after FIFO programming began; no staged re-entry is proven\n",
			tag);
		return -EBUSY;	case OB_D11_TAIL_ENTRY_DMA_LIVE:
		dev_crit(hw->dev,
			 "%s: refusing duplicate D11 tail while DMA is live; teardown must complete first\n",
			 tag);
		return -EBUSY;
	}
	return -EINVAL;
}

/*
 * Whole-prefix admission gate (F1/F2).  Both isolated prefix entries erase
 * hw->d3a1/hw->d3a0 and then run D2A + the common initvals before the tail
 * guard is reached.  This gate runs first: no D2A upload, no common-initvals
 * write, no state reset and no register access happens for a device that is
 * fatal, reboot-required, non-fresh or still owning DMA resources.
 *
 * Pure software: hw->dma_owner, hw->d3a0 and the identity-keyed registries are
 * already in memory, so no register is read to decide.  A clean result is not
 * a hardware-readiness claim.
 */
int ob_d11_prefix_entry_preflight(struct ob_hw *hw, const char *tag)
{
	struct ob_d11_prefix_entry_guard guard;
	enum ob_d11_prefix_entry_denial denial;
	u32 ch;
	bool rings = false;

	if (!hw || !hw->core)
		return -EINVAL;

	for (ch = 0; ch < OB_D3A0_TX_CHANNELS; ch++)
		if (hw->d3a0.tx[ch].allocated)
			rings = true;
	if (hw->d3a0.rx.allocated || hw->d3a0.lc.rx_mapped)
		rings = true;

	guard.dev_lost = hw->dev_lost;
	guard.dma_fatal = ob_dma_fatal_core_refused(hw->core);
	guard.fifo_retry_forbidden = hw->d3a1.fifo_retry_forbidden;
	guard.reboot_required = hw->d3a1.fail_reboot_required;
	guard.tail_started = hw->d3a1.fifo_fixed_writes != 0;
	guard.dma_owner_present = ob_dma_owner_active(hw);
	guard.dma_pool_created = hw->d3a0.pool_created;
	guard.dma_resources_present = rings;
	guard.owner_registry_refused = ob_dma_owner_core_probe_refused(hw->core);

	denial = ob_d11_prefix_entry_check(&guard);

	switch (denial) {
	case OB_D11_PREFIX_ENTRY_OK:
		return 0;
	case OB_D11_PREFIX_ENTRY_INVALID_STATE:
		dev_err(hw->dev, "%s: missing prefix entry state; refusing\n",
			tag);
		return -EINVAL;
	case OB_D11_PREFIX_ENTRY_DEVICE_LOST:
		dev_crit(hw->dev,
			 "%s: refusing D3A1/D3B prefix after device loss; no D2A, reboot required\n",
			tag);
		return -EIO;
	case OB_D11_PREFIX_ENTRY_DMA_FATAL:
		dev_crit(hw->dev,
			 "%s: refusing D3A1/D3B prefix after an unverified DMA quiesce; no D2A, reboot required\n",
			tag);
		return -EIO;
	case OB_D11_PREFIX_ENTRY_OWNER_RETAINED:
		dev_crit(hw->dev,
			 "%s: refusing D3A1/D3B prefix with a retained DMA owner; no D2A, reboot required\n",
			tag);
		return -EIO;
	case OB_D11_PREFIX_ENTRY_REBOOT_REQUIRED:
		dev_crit(hw->dev,
			 "%s: refusing D3A1/D3B prefix after a reboot-required tail failure; no D2A\n",
			tag);
		return -EIO;
	case OB_D11_PREFIX_ENTRY_FIFO_LATCHED:
		dev_crit(hw->dev,
			 "%s: refusing D3A1/D3B prefix after a FIFO poll expiry; no D2A\n",
			tag);
		return -EBUSY;
	case OB_D11_PREFIX_ENTRY_ALREADY_STARTED:
		dev_crit(hw->dev,
			 "%s: refusing D3A1/D3B prefix after FIFO programming began; no staged re-entry\n",
			tag);
		return -EBUSY;
	case OB_D11_PREFIX_ENTRY_DMA_OWNED:
		dev_crit(hw->dev,
			 "%s: refusing D3A1/D3B prefix while DMA state is still owned; verified teardown must complete first\n",
			tag);
		return -EBUSY;
	}
	return -EINVAL;
}

/*
 * Ordered tail runner.  The caller has already completed the shared D2B
 * common-initvals stage and its postcondition gate; this function starts at
 * the live D2B-exit re-check, exactly where the pre-M2 ob_d3a1_run_prefix
 * started its tail.  The stage names are exhaustive; an invalid value is
 * refused before any register access.  AFTER_DMA returns with the D3A0 DMA
 * engines live, matching the vendor T1 -> DMA -> T2 position; COMPLETE adds
 * T2 and validation and leaves DMA live for the caller's final teardown.
 */
int ob_d11_tail_run(struct ob_hw *hw, enum ob_d11_tail_stop stop,
		    const char *tag)
{
	int ret;

	if (!hw || !hw->core)
		return -EINVAL;

	/* Side-effect-free admission: no invalid stop, dev-lost, fatal, FIFO
	 * latch or live-DMA re-entry reaches a register access. */
	ret = ob_d11_tail_entry_preflight(hw, stop, tag);
	if (ret)
		return ret;
	if (stop == OB_D11_TAIL_STOP_DISABLED)
		return 0;

	/* live D2B exit re-check before the first new post-common write */
	ret = ob_d3a1_check_d2b_exit(hw);
	if (ret) {
		ob_d3a1_fail_record(hw, OB_D3A1_FAIL_D2B_GATE, ret, false,
				    OB_D3A1_TEARDOWN_NOT_NEEDED);
		return ret;
	}

	ret = ob_d3a1_fifo_fixup(hw);
	if (ret) {
		/*
		 * DMA is not programmed at this point (fixup runs before T1 and
		 * the DMA bring-up), so no teardown is applicable and none is
		 * invoked.  A timeout has already marked the state unverified.
		 */
		ob_d3a1_fail_record(hw, OB_D3A1_FAIL_FIXUP, ret, false,
				    OB_D3A1_TEARDOWN_NOT_NEEDED);
		dev_err(hw->dev,
			"%s: sub_67efd FAIL ret=%d; TX FIFO state unproven - capture logs and REBOOT before retry\n",
			tag, ret);
		return ret;
	}

	if (stop == OB_D11_TAIL_STOP_AFTER_FIXUP)
		return 0;

	ret = ob_d3a1_t1(hw);
	if (ret) {
		ob_d3a1_fail_record(hw, OB_D3A1_FAIL_T1, ret, false,
				    OB_D3A1_TEARDOWN_NOT_NEEDED);
		dev_err(hw->dev, "%s: T1 FAIL ret=%d\n", tag, ret);
		return ret;
	}

	/* DMA in the exact vendor position: T1 -> DMA -> T2. */
	ret = ob_d3a0_bringup(hw);
	if (ret) {
		int td = ob_d3a0_teardown(hw);

		ob_d3a1_fail_record(hw, OB_D3A1_FAIL_DMA, ret, true,
				    td ? OB_D3A1_TEARDOWN_FAILED
				       : OB_D3A1_TEARDOWN_OK);
		dev_err(hw->dev,
			"%s: DMA bring-up FAIL ret=%d; tearing down\n", tag,
			ret);
		return ret;
	}

	if (stop == OB_D11_TAIL_STOP_AFTER_DMA)
		return 0;

	ret = ob_d3a1_t2(hw);
	if (ret) {
		int td = ob_d3a0_teardown(hw);

		ob_d3a1_fail_record(hw, OB_D3A1_FAIL_T2, ret, true,
				    td ? OB_D3A1_TEARDOWN_FAILED
				       : OB_D3A1_TEARDOWN_OK);
		dev_err(hw->dev, "%s: T2 FAIL ret=%d; tearing down\n", tag,
			ret);
		return ret;
	}

	ret = ob_d3a1_validate(hw);
	if (ret) {
		int td = ob_d3a0_teardown(hw);

		ob_d3a1_fail_record(hw, OB_D3A1_FAIL_VALIDATE, ret, true,
				    td ? OB_D3A1_TEARDOWN_FAILED
				       : OB_D3A1_TEARDOWN_OK);
		dev_err(hw->dev,
			"%s: validation FAIL ret=%d; tearing down\n", tag, ret);
		return ret;
	}

	/* success: the caller decides the next vendor stage (D3A1 STOP vs D3B) */
	return 0;
}

/*
 * Reusable vendor prefix shared by D3A1 and D3B: from the proven D2B exit run
 * sub_67efd -> T1 -> DMA (bring-up) -> T2 -> the D3A1 postcondition gate, and
 * return with the D3A0 DMA engines LEFT LIVE (the vendor T1 -> DMA -> T2 order).
 * On any failure after DMA bring-up ob_d11_tail_run performs the mandatory
 * verified D3A0 teardown; a teardown failure latches the module-wide fatal
 * state and the caller must not free. @tag is the D2B log prefix. The caller
 * owns the hw->d3a0 / hw->d3a1 reset and the final teardown.
 */
int ob_d3a1_run_prefix(struct ob_hw *hw, const char *tag)
{
	struct ob_ucode_run run;
	struct ob_initvals_post post;
	int ret;

	/*
	 * Admission before D2A.  The caller may already have memset() the state,
	 * but a future caller must not: D2A performs firmware upload and the 610
	 * common-initvals writes, so every unsafe or DMA-owning state is refused
	 * here, before the first write, regardless of caller pre-checks.
	 */
	ret = ob_d11_prefix_entry_preflight(hw, tag);
	if (ret)
		return ret;

	/* D2B: shared hardware-proven D2A core + 610 common initvals + gate */
	ret = ob_initvals_run_d2b(hw, tag, &run, &post);
	if (ret)
		return ret;
	if (!ob_d3a0_d2b_state_ok(&post)) {
		dev_err(hw->dev,
			"%s: D2B postconditions not satisfied; D3A1 prefix forbidden\n",
			tag);
		return -EIO;
	}

	return ob_d11_tail_run(hw, OB_D11_TAIL_STOP_COMPLETE, tag);
}

int ob_d3a1_test(struct ob_hw *hw)
{
	int ret;

	/*
	 * One shared, side-effect-free admission gate: device loss, fatal DMA
	 * record, retained owner, reboot-required classification, FIFO latch,
	 * prior FIFO programming and any still-owned D3A0 state.  It runs before
	 * the memset() below and before ob_d3a1_run_prefix() reaches D2A, so an
	 * unsafe device cannot restart firmware upload, the common initvals or
	 * lose its DMA references to a state reset.
	 */
	ret = ob_d11_prefix_entry_preflight(hw, "d3a1-test");
	if (ret)
		return ret;

	memset(&hw->d3a1, 0, sizeof(hw->d3a1));
	/* the D3A1 DMA lifecycle reuses hw->d3a0; reset it before use */
	memset(&hw->d3a0, 0, sizeof(hw->d3a0));

	if (hw->core->id.rev != OB_D3A1_PHYREV_REV42) {
		dev_err(hw->dev,
			"d3a1-test: unsupported D11 core rev %u (need %u)\n",
			hw->core->id.rev, OB_D3A1_PHYREV_REV42);
		return -ENOTSUPP;
	}
	if (!hw->cc) {
		dev_err(hw->dev,
			"d3a1-test: no ChipCommon core; refusing\n");
		return -ENODEV;
	}
	if (!hw->mac_valid) {
		dev_err(hw->dev,
			"d3a1-test: no validated MAC; refusing (vendor attach would fail)\n");
		return -EINVAL;
	}

	dev_info(hw->dev, "d3a1-test: BEGIN\n");

	/* exact vendor prefix (D2B -> sub_67efd -> T1 -> DMA -> T2 -> gate) */
	ret = ob_d3a1_run_prefix(hw, "d3a1-test");
	if (ret)
		return ret;

	/* mandatory same-run teardown of the reused D3A0 lifecycle */
	ret = ob_d3a0_teardown(hw);
	if (ret)
		return ret;

	if (hw->d3a1_fifo530.expiry_continued)
		dev_warn(hw->dev,
			 "d3a1-test: PASS WITH UNVERIFIED FIFO COMPLETION - 0x530 expired and continued at %u of %u entries (first index %d last=%04x reads=%u) under d11_fifo530_continue=1; the tail ran vendor-faithfully but the FIFO completion was never proven\n",
			 hw->d3a1_fifo530.expired_continued,
			 OB_D3A1_FIFO42_ENTRIES,
			 hw->d3a1_fifo530.first_continued_index,
			 hw->d3a1_fifo530.continued_last,
			 hw->d3a1_fifo530.continued_reads);

	hw->d3a1.stopped_before_phy = true;
	dev_info(hw->dev,
		 "d3a1-test: PASS - vendor-ordered D3A1 tail + DMA lifecycle\n");
	dev_info(hw->dev,
		 "d3a1-test: STOPPED BEFORE sub_6656c / bsinitvals / PHY\n");
	return 0;
}

void ob_d3a1_remove(struct ob_hw *hw)
{
	if (!hw->d3a1_test_only)
		return;

	if (hw->d3a0.lc.fatal) {
		/*
		 * Same durable-owner boundary as D3B.  The teardown-failure path
		 * already adopts the live owner's handles, and retention is
		 * idempotent, so this is a no-op there; it only matters when a
		 * fatal latch reached removal without an adoption, where the
		 * devres teardown of struct ob_hw would otherwise drop the last
		 * references.  It performs no MMIO.
		 */
		if (!hw->dma_owner ||
		    !ob_dma_owner_is_retained(hw->dma_owner))
			ob_dma_owner_retain_d3a0(
				hw, OB_DMA_OWNER_REASON_REMOVE_RETAINED,
				"d3a1 fatal remove");
		dev_crit(hw->dev,
			 "d3a1-test: removed in FATAL unverified-quiesce state; DMA memory retained; reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	/*
	 * A FIFO poll expiry leaves partially programmed TX FIFO state and no
	 * DMA was started.  Nothing can be safely released or restored; report
	 * the reboot requirement once and leave the hardware untouched.
	 */
	if (hw->d3a1.fifo_state_unverified) {
		dev_crit(hw->dev,
			 "d3a1-test: removed with unverified FIFO state; no recovery attempted, reboot required\n");
		bcma_set_drvdata(hw->core, NULL);
		return;
	}
	if (hw->d3a0.pool_created) {
		if (ob_d3a0_teardown(hw)) {
			dev_crit(hw->dev,
				 "d3a1-test: removal teardown unverified; reboot required\n");
			bcma_set_drvdata(hw->core, NULL);
			return;
		}
	}
	dev_info(hw->dev, "d3a1-test: removed (DMA resources released)\n");
	bcma_set_drvdata(hw->core, NULL);
}

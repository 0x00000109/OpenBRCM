// SPDX-License-Identifier: GPL-2.0-only
/*
 * OpenBRCM — D11 rev42 ucode upload + PSM start (M3.4D2A), shared by the
 * isolated ucode_test_only and initvals_test_only (M3.4D2B) paths.
 *
 * Phase 38C: the recovered register sequence is unchanged. What changed is
 * WHERE the sequencing lives and HOW it fails. The state machine, its gates and
 * its failure classes are the pure contract in ob_ucode_d2a_contract.h, so the
 * host tests execute the same code this file runs; this file is the kernel
 * adapter (accessors + logging) and nothing else.
 *
 * The recovered sequence (exact vendor order, see ob_ucode.h for per-step
 * provenance):
 *   1. minimal core prep: bcma_host_pci_up + D11 core enable/reset + FAST clock
 *   2. pre-upload MACCONTROL  = 0x04000404 (IHR_EN | PSM_JMP0 | WAKE), masked RMW
 *   3. ucode upload via OBJADDR=0x03000000 (auto-inc) + 10850 OBJDATA writes
 *   4. macintstatus = 0xffffffff, then PSM start MACCONTROL = 0x04020402
 *   5. bounded poll of macintstatus & MI_MACSSPNDD (10 us, <=100000 iters)
 *
 * ucode_test_only (M3.4D2A) then reads the SHM FIFO-size diagnostic and STOPS.
 * initvals_test_only (M3.4D2B, ob_initvals.c) then applies the common table and
 * verifies the deterministic postconditions. Both share this file so the
 * proven D2A register sequence cannot silently diverge.
 *
 * Neither path applies bsinitvals, EN_MAC, request_irq, DMA or mac80211. No
 * cleanup register writes are performed on any failure path: a failure after
 * the first hardware mutation RETAINS the device (see ob_probe()/ob_remove()).
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/bcma/bcma.h>

#include "ob_core.h"
#include "ob_fw.h"
#include "ob_ucode.h"
#include "ob_ucode_d2a_contract.h"
#include "ob_ucore_prep.h"

/*
 * Minimum proven D11 preparation for the ucode upload, in the proven M2.5
 * order (ob_si_powerup stages A/B/C): host/backplane up, D11 core enable/reset,
 * then FAST (HT) clock. The vendor reaches this state through
 * wlc_bmac_up_prep()/wlc_bmac_corereset() before wlc_bmac_init() uploads ucode.
 *
 * @core_up is set only when the wrapper readback predicate confirms an enabled,
 * clocked core; @mutated is set as soon as any step that can change hardware
 * state is entered (bcma_host_pci_up() alone touches PCI config space).
 */
/*
 * Phase 38C Amendment 01 (A2): the CP0..CP3 wrapper-window preparation is the
 * pure state machine in ob_ucore_prep.h. This file is the kernel adapter: one
 * callback per wrapper read, core operation and log line.
 *
 * The adapter CANNOT change the sequence, and it cannot read the wrapper twice
 * after a suspicious first read: `read_ioctl` and `read_reset` are separate
 * callbacks and the contract decides which of them is invoked. The only raw
 * BCMA register reads in this file are the two callbacks below.
 */
struct ob_ucore_hw_ctx {
	struct ob_hw *hw;
};

static bool ob_ucore_hw_dev_lost(void *ctx)
{
	return ((struct ob_ucore_hw_ctx *)ctx)->hw->dev_lost;
}

static void ob_ucore_hw_latch(void *ctx, const char *reason)
{
	ob_dev_lost_latch(((struct ob_ucore_hw_ctx *)ctx)->hw, reason);
}

static u32 ob_ucore_hw_read_ioctl(void *ctx)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	return bcma_aread32(hw->core, BCMA_IOCTL);
}

static u32 ob_ucore_hw_read_reset(void *ctx)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	return bcma_aread32(hw->core, BCMA_RESET_CTL);
}

static void ob_ucore_hw_host_up(void *ctx)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	bcma_host_pci_up(hw->bus);
}

static int ob_ucore_hw_core_enable(void *ctx)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	return bcma_core_enable(hw->core, 0);
}

static void ob_ucore_hw_clockmode_fast(void *ctx)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	bcma_core_set_clockmode(hw->core, BCMA_CLKMODE_FAST);
}

static u32 ob_ucore_hw_read_clk1e0(void *ctx)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	return ob_d11_read32_trusted(hw, "ucode_prepare_clk1e0",
				     OB_UCODE_REG_D11_CLK1E0);
}

static bool ob_ucore_hw_core_is_enabled(void *ctx)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	return bcma_core_is_enabled(hw->core);
}

static const char *ob_ucode_core_postcondition_name(
		enum ob_ucode_core_postcondition postcondition)
{
	switch (postcondition) {
	case OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID:
		return "IOCTL_INVALID";
	case OB_UCODE_CORE_POSTCONDITION_RESET_ASSERTED:
		return "RESET_ASSERTED";
	case OB_UCODE_CORE_POSTCONDITION_IOCTL_INVALID_RESET_ASSERTED:
		return "IOCTL_INVALID_RESET_ASSERTED";
	case OB_UCODE_CORE_POSTCONDITION_ENABLED:
		return "ENABLED";
	default:
		return "UNKNOWN";
	}
}

/*
 * The isolated D2/D2B/D3 diagnostic modes run their own core preparation and
 * start from OB_D11_STATE_NONE; production reaches this function with
 * CORE_READY already published by ob_si_probe(). Keep the fail-closed state
 * requirement for every other caller.
 */
static bool ob_ucode_isolated_core_owner(const struct ob_hw *hw)
{
	return hw->ucode_test_only || hw->initvals_test_only ||
	       hw->dma_test_only || hw->d3a1_test_only ||
	       hw->bsinitvals_test_only ||
	       hw->acphy_pre_radio_test_only ||
	       hw->acphy_class0_radio_local_0413_test_only ||
	       hw->acphy_class0_post0413_phase1_0413_test_only ||
	       hw->acphy_class0_postphase1_phase2_0413_test_only ||
	       hw->acphy_class0_postphase2_sub9591e_return_test_only ||
	       hw->acphy_class0_postbmac_phyreg1b0_branch_gate_test_only;
}

static void ob_ucore_hw_log(void *ctx, const struct ob_ucore_log *rec)
{
	struct ob_hw *hw = ((struct ob_ucore_hw_ctx *)ctx)->hw;

	switch (rec->ev) {
	case OB_UCORE_EV_SNAPSHOT:
		if (rec->flag)
			dev_info(hw->dev,
				 "D11_CORE_STATE_%s ioctl=0x%08x reset=0x%08x enabled=%d\n",
				 rec->point, rec->value, rec->value2,
				 ob_ucode_core_enabled_predicate(rec->value,
								 rec->value2));
		else
			dev_info(hw->dev,
				 "D11_CORE_STATE_%s ioctl=0x%08x reset=SUPPRESSED enabled=0\n",
				 rec->point, rec->value);
		break;
	case OB_UCORE_EV_SUSPICIOUS:
		dev_err(hw->dev,
			"D11_CORE_STATE_SUSPICIOUS point=%s%s; device-loss latch set, no further wrapper or functional access\n",
			rec->point,
			rec->flag ? " (second read)" : " (first read suppressed)");
		break;
	case OB_UCORE_EV_LIFECYCLE:
		switch (rec->idx) {
		case 0:
			dev_info(hw->dev,
				 "D11_LIFECYCLE_0_ENTRY core=%px mapped=%px mapped_is_core=%d index=%u state=%u enabled=%d\n",
				 hw->core, hw->bus->mapped_core,
				 hw->bus->mapped_core == hw->core,
				 hw->core->core_index, hw->d11_state,
				 rec->flag);
			break;
		case 1:
			dev_info(hw->dev,
				 "D11_LIFECYCLE_1_AFTER_HOST_UP core=%px mapped=%px mapped_is_core=%d index=%u state=%u enabled=%d\n",
				 hw->core, hw->bus->mapped_core,
				 hw->bus->mapped_core == hw->core,
				 hw->core->core_index, hw->d11_state,
				 rec->flag);
			break;
		case 2:
			dev_info(hw->dev,
				 "D11_LIFECYCLE_2_BEFORE_CORE_ENABLE core=%px mapped=%px mapped_is_core=%d index=%u state=%u enabled=%d\n",
				 hw->core, hw->bus->mapped_core,
				 hw->bus->mapped_core == hw->core,
				 hw->core->core_index, hw->d11_state,
				 rec->flag);
			break;
		case 3:
			dev_info(hw->dev,
				 "D11_LIFECYCLE_3_AFTER_CORE_ENABLE core=%px mapped=%px mapped_is_core=%d index=%u state=%u enabled=%d\n",
				 hw->core, hw->bus->mapped_core,
				 hw->bus->mapped_core == hw->core,
				 hw->core->core_index, hw->d11_state,
				 rec->flag);
			break;
		case 4:
			dev_info(hw->dev,
				 "D11_LIFECYCLE_4_AFTER_CLOCKMODE_FAST core=%px mapped=%px mapped_is_core=%d index=%u state=%u enabled=%d\n",
				 hw->core, hw->bus->mapped_core,
				 hw->bus->mapped_core == hw->core,
				 hw->core->core_index, hw->d11_state,
				 rec->flag);
			break;
		case 5:
			dev_info(hw->dev,
				 "D11_LIFECYCLE_5_BEFORE_CLK1E0 core=%px mapped=%px mapped_is_core=%d index=%u state=%u enabled=%d\n",
				 hw->core, hw->bus->mapped_core,
				 hw->bus->mapped_core == hw->core,
				 hw->core->core_index, hw->d11_state,
				 rec->flag);
			break;
		case 6:
			dev_info(hw->dev,
				 "D11_LIFECYCLE_6_CLK1E0_RESULT value=0x%08x core=%px mapped=%px mapped_is_core=%d index=%u state=%u enabled=%d\n",
				 rec->value, hw->core, hw->bus->mapped_core,
				 hw->bus->mapped_core == hw->core,
				 hw->core->core_index, hw->d11_state,
				 rec->flag);
			break;
		default:
			break;
		}
		break;
	case OB_UCORE_EV_HOST_UP:
		dev_info(hw->dev, "ucode-test: prep host up (host_is_pcie2=%d)\n",
			 hw->bus->host_is_pcie2);
		break;
	case OB_UCORE_EV_ENABLE_CALLED:
		dev_info(hw->dev, "D11_CORE_ENABLE_CALLED=%d\n", rec->flag ? 1 : 0);
		break;
	case OB_UCORE_EV_POSTCONDITION:
		dev_info(hw->dev, "D11_CORE_POSTCONDITION=%s\n",
			 ob_ucode_core_postcondition_name(rec->idx));
		break;
	case OB_UCORE_EV_POSTCONDITION_FAILED:
		dev_err(hw->dev,
			"D11_CORE_POSTCONDITION_FAILED before functional D11 access\n");
		break;
	case OB_UCORE_EV_CLK1E0:
		dev_info(hw->dev,
			 "ucode-test: d11_clk1e0=%08x bit1=%d core_enabled=%d\n",
			 rec->value,
			 !!(rec->value & OB_UCODE_D11_CLK1E0_BIT1),
			 rec->flag ? 1 : 0);
		break;
	case OB_UCORE_EV_CLK1E0_UNAVAILABLE:
		if (rec->flag)
			dev_err(hw->dev,
				"D11_ACCESS_UNAVAILABLE stage=ucode_prepare_clk1e0 value=0x%08x (latching read; device retained)\n",
				rec->value);
		else
			dev_err(hw->dev,
				"D11_ACCESS_UNAVAILABLE stage=ucode_prepare_clk1e0 value=0x%08x\n",
				rec->value);
		break;
	case OB_UCORE_EV_STATE_PUBLISHED:
		/* Isolated D2/D2B/D3 started from NONE; the core is now up. */
		if (hw->d11_state == OB_D11_STATE_NONE &&
		    ob_ucode_isolated_core_owner(hw))
			hw->d11_state = OB_D11_CORE_READY;
		break;
	default:
		break;
	}
}

/*
 * Wrapper-window core preparation. Reports the two facts the D2A contract
 * needs and cannot observe itself: whether the core is CONFIRMED up (wrapper
 * readback predicate, not a function return) and whether hardware may have
 * been mutated (which decides the retention policy on failure, D5).
 *
 * Amendment 01: all sequencing and every fail-stop decision live in
 * ob_ucore_prep.h; this function only gates admission and maps the callbacks.
 */
static int ob_ucode_prepare(struct ob_hw *hw, bool *core_up, bool *mutated)
{
	struct ob_ucore_hw_ctx c;
	struct ob_ucore_ops ops;
	struct ob_ucore_result out;
	int ret;

	*core_up = false;
	*mutated = false;

	/* Device-loss guard: no core/host bring-up after the latch. */
	if (hw->dev_lost)
		return -EIO;
	/*
	 * Production reaches this with CORE_READY (or ATTACH_IDENTITY_READY)
	 * already published by ob_si_probe(). The isolated D2/D2B test modes own
	 * their own core preparation and start from NONE; admit them explicitly
	 * and publish CORE_READY once the prep below confirms the core. Every
	 * other state is still rejected fail-closed.
	 */
	if (hw->d11_state != OB_D11_CORE_READY &&
	    hw->d11_state != OB_D11_ATTACH_IDENTITY_READY &&
	    !ob_ucode_isolated_core_owner(hw))
		return -EINVAL;

	c.hw = hw;
	ops.ctx = &c;
	ops.dev_lost = ob_ucore_hw_dev_lost;
	ops.latch = ob_ucore_hw_latch;
	ops.read_ioctl = ob_ucore_hw_read_ioctl;
	ops.read_reset = ob_ucore_hw_read_reset;
	ops.host_up = ob_ucore_hw_host_up;
	ops.core_enable = ob_ucore_hw_core_enable;
	ops.clockmode_fast = ob_ucore_hw_clockmode_fast;
	ops.read_clk1e0 = ob_ucore_hw_read_clk1e0;
	ops.core_is_enabled = ob_ucore_hw_core_is_enabled;
	ops.log = ob_ucore_hw_log;

	ret = ob_ucore_prepare(&ops, &out);
	*core_up = out.core_up;
	*mutated = out.mutated;
	if (ret)
		dev_err(hw->dev,
			"D11_CORE_PREP_FAIL point=%s reason=%s rc=%d wrapper_reads ioctl=%u reset=%u snapshots=%u enable=%u/%d clockmode=%u functional=%u\n",
			out.has_failed_point ?
				ob_ucore_point_name(out.failed_point) : "none",
			out.reason, ret, out.ioctl_reads, out.reset_reads,
			out.snapshots, out.enable_calls, out.enable_errors,
			out.clockmode_calls, out.functional_reads);
	return ret;
}

/*
 * Vendor SHM read (wlc_bmac_read_shm @0x61910 -> helper 0x6188a): select the
 * SHM window in OBJADDR, read it back (barrier), then read the 16-bit half
 * selected by offset bit1 from OBJDATA. offset is a byte offset.
 */
u16 ob_ucode_read_shm16(struct ob_hw *hw, u16 off)
{
	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	(void)ob_d11_read32(hw, OB_UCODE_REG_OBJADDR);
	return ob_d11_read16(hw, OB_UCODE_REG_OBJDATA + (off & 0x2));
}

int ob_ucode_read_shm16_checked(struct ob_hw *hw, u16 off, u16 *value)
{
	if (!hw || !hw->core || !value)
		return -22;
	if (hw->dev_lost)
		return -19;

	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	if (hw->dev_lost)
		return -19;
	(void)ob_d11_read32_trusted(hw, "acphy_sub_92500_shm_objaddr",
				    OB_UCODE_REG_OBJADDR);
	if (hw->dev_lost)
		return -19;
	*value = ob_d11_read16(hw, OB_UCODE_REG_OBJDATA + (off & 0x2));
	return hw->dev_lost ? -19 : 0;
}

/*
 * Vendor SHM 16-bit write (wlc_bmac_write_shm equivalent): select the SHM
 * window in OBJADDR, barrier, then write the 16-bit half selected by offset
 * bit1 into OBJDATA. offset is a byte offset. Reused by the D3B MHF writes.
 */
void ob_ucode_write_shm16(struct ob_hw *hw, u16 off, u16 val)
{
	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	(void)ob_d11_read32(hw, OB_UCODE_REG_OBJADDR);
	ob_d11_write16(hw, OB_UCODE_REG_OBJDATA + (off & 0x2), val);
}

int ob_ucode_write_shm16_checked(struct ob_hw *hw, u16 off, u16 val)
{
	if (!hw || !hw->core)
		return -22;
	if (hw->dev_lost)
		return -19;

	ob_d11_write32(hw, OB_UCODE_REG_OBJADDR,
		     OB_UCODE_OBJADDR_SHM_SEL | ((u32)off >> 2));
	if (hw->dev_lost)
		return -19;
	(void)ob_d11_read32_trusted(hw, "acphy_pre_radio_shm_objaddr",
				    OB_UCODE_REG_OBJADDR);
	if (hw->dev_lost)
		return -19;
	ob_d11_write16(hw, OB_UCODE_REG_OBJDATA + (off & 0x2), val);
	return hw->dev_lost ? -19 : 0;
}

/* ------------------------------------------------------------------------ *
 * D2A kernel adapter: accessors + logging for ob_ucode_d2a_contract.h.
 *
 * The adapter cannot change the sequence: every accessor below is bound to the
 * register the contract asks for, and the contract decides when it is called.
 * ------------------------------------------------------------------------ */

struct ob_ucode_d2a_ctx {
	struct ob_hw *hw;
	const char *tag;
	const struct firmware *fw;
	const char *name;      /* image name actually used */
};

static int ob_ucode_d2a_fw_request(void *ctx, const u8 **data, size_t *size,
				   const char **name)
{
	struct ob_ucode_d2a_ctx *c = ctx;
	const struct firmware *fw = NULL;
	const char *used = NULL;
	int ret;

	ret = ob_fw_request_ucode(c->hw, &fw, &used);
	if (ret)
		return ret;

	c->fw = fw;
	c->name = used;
	*data = fw->data;
	*size = fw->size;
	*name = used;
	return 0;
}

static int ob_ucode_d2a_core_prepare(void *ctx, bool *core_up, bool *mutated)
{
	struct ob_ucode_d2a_ctx *c = ctx;

	return ob_ucode_prepare(c->hw, core_up, mutated);
}

/*
 * Latching accessor. The value is always reported: an all-ones read is the
 * observation the contract classifies as device loss (D1/D4), so turning it
 * into a callback error would lose the classification.
 */
static int ob_ucode_d2a_read32(void *ctx, u16 off, u32 *val)
{
	struct ob_ucode_d2a_ctx *c = ctx;

	*val = ob_d11_read32_trusted(c->hw, "ucode_d2a_control", off);
	return 0;
}

static int ob_ucode_d2a_read16(void *ctx, u16 off, u16 *val)
{
	struct ob_ucode_d2a_ctx *c = ctx;

	if (c->hw->dev_lost)
		return -EIO;
	*val = ob_d11_read16(c->hw, off);
	return c->hw->dev_lost ? -EIO : 0;
}

static int ob_ucode_d2a_write32(void *ctx, u16 off, u32 val)
{
	struct ob_ucode_d2a_ctx *c = ctx;

	if (c->hw->dev_lost)
		return -EIO;
	ob_d11_write32(c->hw, off, val);
	return c->hw->dev_lost ? -EIO : 0;
}

static void ob_ucode_d2a_delay(void *ctx, unsigned int us)
{
	(void)ctx;
	udelay(us);
}

static bool ob_ucode_d2a_dev_lost(void *ctx)
{
	return ((struct ob_ucode_d2a_ctx *)ctx)->hw->dev_lost;
}

static void ob_ucode_d2a_log_word_bounds(const struct ob_ucode_d2a_ctx *c)
{
	const u8 *data;
	size_t words, i;

	if (!c->fw)
		return;
	data = c->fw->data;
	words = c->fw->size / 4;
	if (words < 4)
		return;
	dev_info(c->hw->dev, "%s: first %08x %08x %08x %08x\n", c->tag,
		 ob_fw_le32(data), ob_fw_le32(data + 4), ob_fw_le32(data + 8),
		 ob_fw_le32(data + 12));
	i = (words - 4) * 4;
	dev_info(c->hw->dev, "%s: last  %08x %08x %08x %08x\n", c->tag,
		 ob_fw_le32(data + i), ob_fw_le32(data + i + 4),
		 ob_fw_le32(data + i + 8), ob_fw_le32(data + i + 12));
}

static void ob_ucode_d2a_log(void *ctx, enum ob_d2a_event ev,
			     const struct ob_d2a_log *rec)
{
	struct ob_ucode_d2a_ctx *c = ctx;
	struct ob_hw *hw = c->hw;

	switch (ev) {
	case OB_D2A_EV_BEGIN:
		dev_info(hw->dev, "%s: BEGIN\n", c->tag);
		break;
	case OB_D2A_EV_FIRMWARE:
		if (!rec->reason)
			break;
		if (!strcmp(rec->reason, "image_validated"))
			dev_info(hw->dev, "%s: image %s size=%zu words=%u\n",
				 c->tag, c->name ? c->name : "?",
				 c->fw ? c->fw->size : 0, rec->counter);
		else if (!strcmp(rec->reason, "request_failed"))
			dev_err(hw->dev, "%s: ucode unavailable/invalid: %d\n",
				c->tag, rec->error);
		else if (!strcmp(rec->reason, "image_mismatch"))
			dev_err(hw->dev,
				"%s: ucode image rejected: size=%zu (expected %u) or FNV mismatch; no hardware access\n",
				c->tag, (size_t)rec->counter,
				(unsigned int)OB_FW_UCODE42_SIZE);
		break;
	case OB_D2A_EV_CORE_PREP:
		if (rec->reason && !strcmp(rec->reason, "core_up"))
			dev_info(hw->dev, "%s: core prepared\n", c->tag);
		break;
	case OB_D2A_EV_PRE_WRITE:
	case OB_D2A_EV_POST_WRITE:
		/*
		 * The per-word OBJDATA writes are 10850 events: they are counted,
		 * never logged one per line. Control writes keep their line.
		 */
		if (rec->stage == OB_D2A_STAGE_UPLOAD)
			break;
		if (rec->stage == OB_D2A_STAGE_MACCONTROL_UPLOAD &&
		    ev == OB_D2A_EV_POST_WRITE)
			dev_info(hw->dev,
				 "ucode: maccontrol upload-state written=%08x (issued; validated by the separate readback)\n",
				 rec->value);
		if (rec->stage == OB_D2A_STAGE_PSM_START &&
		    ev == OB_D2A_EV_POST_WRITE &&
		    rec->off == OB_UCODE_REG_MACCONTROL) {
			dev_info(hw->dev,
				 "ucode: PSM start maccontrol written=%08x (issued; start confirmed by readback and poll)\n",
				 rec->value);
			dev_info(hw->dev, "%s: PSM start issued\n", c->tag);
		}
		break;
	case OB_D2A_EV_POST_READ:
		if (rec->reason && !strcmp(rec->reason, "maccontrol_before"))
			dev_info(hw->dev, "ucode: maccontrol before=%08x\n",
				 rec->value);
		else if (rec->reason &&
			 !strcmp(rec->reason, "maccontrol_upload_validated"))
			dev_info(hw->dev,
				 "ucode: maccontrol upload-state readback=%08x (validated)\n",
				 rec->value);
		else if (rec->reason &&
			 !strcmp(rec->reason, "maccontrol_psm_validated"))
			dev_info(hw->dev,
				 "ucode: PSM start readback validated maccontrol=%08x\n",
				 rec->value);
		break;
	case OB_D2A_EV_UPLOAD_PROGRESS:
		if (rec->reason && !strcmp(rec->reason, "progress"))
			dev_info(hw->dev,
				 "%s: upload progress issued=%u/%u (unconfirmed)\n",
				 c->tag, rec->counter,
				 (unsigned int)OB_D2A_EXPECTED_WORDS);
		else if (rec->reason &&
			 !strcmp(rec->reason, "upload_complete"))
			dev_info(hw->dev,
				 "%s: upload complete words=%u issued=%u confirmed=0 (no ucode-RAM readback exists)\n",
				 c->tag, (unsigned int)OB_D2A_EXPECTED_WORDS,
				 rec->counter);
		break;
	case OB_D2A_EV_STOP:
		if (rec->reason && !strcmp(rec->reason, "poll_timeout"))
			dev_err(hw->dev,
				"%s: PSM poll TIMEOUT iterations=%u status=%08x\n",
				c->tag, rec->counter, rec->value);
		break;
	default:
		break;
	}
}

static const char *ob_ucode_stage_token(enum ob_d2a_stage s)
{
	return ob_d2a_stage_name(s);
}

/*
 * Hardware path, shared by ucode_test_only (M3.4D2A) and initvals_test_only
 * (M3.4D2B). It stops with the PSM paused after auto-init and applies no
 * initvals; @tag only selects the log prefix. No cleanup writes on any path.
 *
 * Phase 38C: the returned @run carries the contract's own verdict. A non-zero
 * return with @run->retained set means hardware may have been mutated and the
 * caller MUST keep the device bound (see ob_probe()).
 */
static void ob_ucode_d2a_ops_init(struct ob_ucode_d2a_ctx *c,
				  struct ob_d2a_ops *ops, struct ob_hw *hw,
				  const char *tag)
{
	c->hw = hw;
	c->tag = tag;
	c->fw = NULL;
	c->name = NULL;

	memset(ops, 0, sizeof(*ops));
	ops->ctx = c;
	ops->fw_request = ob_ucode_d2a_fw_request;
	ops->core_prepare = ob_ucode_d2a_core_prepare;
	ops->read32_trusted = ob_ucode_d2a_read32;
	ops->read16 = ob_ucode_d2a_read16;
	ops->write32 = ob_ucode_d2a_write32;
	ops->delay_us = ob_ucode_d2a_delay;
	ops->log = ob_ucode_d2a_log;
	ops->dev_lost = ob_ucode_d2a_dev_lost;
}

/*
 * The shared D2A core. Amendment 01 (A1): it ends with the PSM suspended and
 * performs NO SHM observation, so the ucode_test_only caller owns the four
 * FIFO-size reads and the initvals caller reaches common initvals without any.
 */
static int ob_ucode_d2a_exec(struct ob_hw *hw, const char *tag,
			     struct ob_ucode_run *run, struct ob_d2a_result *res)
{
	struct ob_ucode_d2a_ctx c;
	struct ob_d2a_ops ops;
	bool entry_dev_lost;
	int ret;

	memset(run, 0, sizeof(*run));
	ob_ucode_d2a_ops_init(&c, &ops, hw, tag);

	/*
	 * CP0 start marker, emitted by the adapter before the contract is entered
	 * (the historical position: before any register access). The contract
	 * itself cannot log before its first callback returns.
	 */
	dev_info(hw->dev, "OB_UCODE_CP0_START\n");

	entry_dev_lost = hw->dev_lost;
	ret = ob_d2a_run(&ops, entry_dev_lost, res);

	ob_ucode_d2a_log_word_bounds(&c);
	if (res->stage == OB_D2A_STAGE_PSM_POLL ||
	    res->stage == OB_D2A_STAGE_DONE) {
		if (res->psm_suspended)
			dev_info(hw->dev,
				 "%s: PSM poll PASS iterations=%u status=%08x\n",
				 tag, res->psm_iterations, res->psm_status);
	}

	/*
	 * Every field is a distinct measurement. 2026-10-25: this line used to
	 * print `writes_issued` a second time as `poke`, never printed the
	 * control-write total, and reported fixed and poll reads as one number.
	 * `writes_total` is every register write (upload + control), `reads_poll`
	 * is the poll subset of `reads_trusted`, so the fixed read cost is
	 * `reads_trusted - reads_poll`.
	 */
	dev_info(hw->dev,
		 "%s: counters writes_issued=%u writes_confirmed=%u writes_total=%u reads_trusted=%u reads_poll=%u reads_payload=%u\n",
		 tag, res->writes_issued, res->writes_confirmed,
		 res->writes_issued_total, res->reads_trusted, res->reads_poll,
		 res->reads_payload);

	if (ret) {
		dev_err(hw->dev,
			"%s: FAIL stage=%s class=%s reason=%s ret=%d (no cleanup writes)\n",
			tag, ob_ucode_stage_token(res->failed_stage),
			ob_d2a_class_token(res->class),
			res->reason ? res->reason : "unspecified", ret);
		if (res->retained)
			dev_emerg(hw->dev,
				  "%s: RETAINED after hardware mutation; device must stay bound, no teardown, reboot to recover\n",
				  tag);
	} else {
		dev_info(hw->dev, "%s: %s\n", tag,
			 ob_d2a_class_token(res->class));
	}

	run->name = res->image_name;
	run->words = res->words;
	run->issued = res->writes_issued;
	run->confirmed = res->writes_confirmed;
	run->psm_iterations = res->psm_iterations;
	run->psm_status = res->psm_status;
	run->psm_suspended = res->psm_suspended;
	run->retained = res->retained;
	run->mutated = res->mutated;
	run->class_token = ob_d2a_class_token(res->class);
	run->stage = ret ? ob_ucode_stage_token(res->failed_stage) : "done";
	run->ret = ret;

	/* Publish the retention decision for ob_probe()/ob_remove(). */
	hw->ucode_retain = res->retained;
	hw->ucode_mutated = res->mutated;
	hw->ucode_psm_suspended = res->psm_suspended;
	hw->ucode_class = run->class_token;

	release_firmware(c.fw);
	c.fw = NULL;
	return ret;
}

int ob_ucode_run_d2a(struct ob_hw *hw, const char *tag, struct ob_ucode_run *run)
{
	struct ob_d2a_result res;

	return ob_ucode_d2a_exec(hw, tag, run, &res);
}

int ob_ucode_test(struct ob_hw *hw)
{
	struct ob_ucode_d2a_ctx c;
	struct ob_d2a_ops ops;
	struct ob_d2a_result res;
	struct ob_ucode_run run;
	u16 fifo[4] = { 0, 0, 0, 0 };
	int ret;

	ret = ob_ucode_d2a_exec(hw, "ucode-test", &run, &res);
	if (ret)
		return ret;

	/*
	 * Amendment 01 (A1/A4): the read-only SHM FIFO-size diagnostic is the
	 * ucode_test_only tail and is performed EXACTLY ONCE here, through the
	 * contract's fail-stop helper. The shared D2A path performs none, so the
	 * initvals mode never issues these reads before common initvals.
	 *
	 * The vendor reads these after the common-initvals applier
	 * (0x68f88 > 0x68b98); D2A logs them without an equality test and does
	 * not treat them as a pass/fail condition of the firmware.
	 */
	ob_ucode_d2a_ops_init(&c, &ops, hw, "ucode-test");
	ret = ob_d2a_shm_diag(&ops, &res, fifo);

	dev_info(hw->dev,
		 "ucode-test: SHM M_FIFOSIZE0..3=%04x %04x %04x %04x observations=%u/4%s\n",
		 fifo[0], fifo[1], fifo[2], fifo[3], res.shm_observations,
		 res.shm_read ? "" : " (incomplete)");
	if (ret) {
		dev_err(hw->dev,
			"ucode-test: SHM diagnostic stopped at observation %u (class=%s reason=%s ret=%d); no later FIFO read was attempted\n",
			(unsigned int)res.shm_failed_index + 1u,
			ob_d2a_class_token(res.class),
			res.reason ? res.reason : "unspecified", ret);
		if (res.device_lost)
			hw->ucode_retain = true;
		return ret;
	}

	dev_info(hw->dev,
		 "ucode-test: PASS - stopped before initvals/PHY/radio/DMA\n");
	return 0;
}

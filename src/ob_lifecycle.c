// SPDX-License-Identifier: GPL-2.0-only
/* Normal-path D11 bootstrap: accepted D2A/D2B stages, then stop. */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include "ob_core.h"
#include "ob_fw_chain.h"
#include "ob_ucode.h"
#include "ob_initvals.h"
#include "ob_radio.h"
#include "ob_si.h"

static int ob_runtime_read32(void *ctx, u16 off, u32 *value)
{
	struct ob_hw *hw = ctx;

	if (!hw || !value || hw->dev_lost)
		return -5;
	*value = ob_d11_read32(hw, off);
	if (hw->dev_lost || *value == 0xffffffffu)
		return -5;
	return 0;
}

static int ob_runtime_write32(void *ctx, u16 off, u32 value)
{
	struct ob_hw *hw = ctx;

	if (!hw || hw->dev_lost)
		return -5;
	ob_d11_write32(hw, off, value);
	return hw->dev_lost ? -5 : 0;
}

static void ob_runtime_delay(void *ctx, u32 usec)
{
	(void)ctx;
	udelay(usec);
}

static const struct ob_d11_runtime_io ob_runtime_io = {
	.read32 = ob_runtime_read32,
	.write32 = ob_runtime_write32,
	.delay_us = ob_runtime_delay,
};

int ob_d11_suspend_mac_and_wait(struct ob_hw *hw)
{
	u32 polls = 0;
	bool transitioned = false;
	int ret;

	if (!hw || hw->dev_lost ||
	    hw->d11_state != OB_D11_POST_UCODE_READY ||
	    hw->runtime_mac_suspended)
		return -22;
	ret = ob_d11_runtime_quiesce_script(&ob_runtime_io, hw, &polls,
						&transitioned);
	if (!ret && transitioned)
		hw->runtime_mac_suspended = true;
	return ret ? ret : (transitioned ? 0 : -22);
}

int ob_d11_quiesce_for_radio(struct ob_hw *hw)
{
	u32 polls = 0;
	bool transitioned = false;
	int ret;

	if (!hw || hw->dev_lost ||
	    hw->d11_state != OB_D11_POST_UCODE_READY)
		return -22;
	ret = ob_runtime_read32(hw, OB_D11_REG_MACCONTROL,
				&hw->radio_quiesce_entry_maccontrol);
	if (ret)
		return ret;
	ret = ob_runtime_read32(hw, OB_D11_REG_MACINTSTATUS,
				&hw->radio_quiesce_entry_macintstatus);
	if (ret)
		return ret;
	dev_info(hw->dev,
		 "radio2069-rev4: RADIO_QUIESCE_ENTRY maccontrol=0x%08x macintstatus=0x%08x en_mac=%u path=%s\n",
		 hw->radio_quiesce_entry_maccontrol,
		 hw->radio_quiesce_entry_macintstatus,
		 !!(hw->radio_quiesce_entry_maccontrol & OB_D11_MCTL_EN_MAC),
		 (hw->radio_quiesce_entry_maccontrol & OB_D11_MCTL_EN_MAC) ?
		 "runtime_suspend" : "already_quiesced");
	ret = ob_d11_runtime_quiesce_script(&ob_runtime_io, hw, &polls,
						&transitioned);
	hw->radio_quiesce_polls = polls;
	if (ret)
		return ret;
	hw->runtime_mac_suspended = transitioned;
	hw->radio_stage_owns_suspend = transitioned;
	dev_info(hw->dev,
		 "radio2069-rev4: RADIO_QUIESCE_COMPLETE path=%s polls=%u\n",
		 transitioned ? "runtime_suspend" : "already_quiesced", polls);
	return 0;
}

int ob_d11_enable_mac(struct ob_hw *hw)
{
	int ret;

	if (!hw || hw->dev_lost || !hw->runtime_mac_suspended)
		return -22;
	ret = ob_d11_runtime_resume_script(&ob_runtime_io, hw);
	if (ret)
		return ret;
	hw->runtime_mac_suspended = false;
	/* wlc_bmac_enable_mac @0x060dd7: clear the suspend wake override. */
	return ob_d11_suspend_wake_override_leave(hw);
}

static int ob_d11_wake_read_shm16(void *ctx, u16 off, u16 *value)
{
	struct ob_hw *hw = ctx;

	if (!hw || !value || hw->dev_lost)
		return -5;
	return ob_ucode_read_shm16_checked(hw, off, value);
}

/* Kernel adapter for the vendor wake scripts (M1-6D model). */
static const struct ob_d11_wake_io ob_wake_io = {
	.read32 = ob_runtime_read32,
	.write32 = ob_runtime_write32,
	.read_shm16 = ob_d11_wake_read_shm16,
	.delay_us = ob_runtime_delay,
};

int ob_d11_wake_override_set(struct ob_hw *hw, u32 mask)
{
	if (!hw || hw->dev_lost)
		return -EIO;
	return ob_d11_wake_override_set_script(&ob_wake_io, hw,
					       &hw->wake_override, mask);
}

int ob_d11_wake_override_clear(struct ob_hw *hw, u32 mask)
{
	if (!hw || hw->dev_lost)
		return -EIO;
	return ob_d11_wake_override_clear_script(&ob_wake_io, hw,
						 &hw->wake_override, mask);
}

int ob_d11_suspend_wake_override_enter(struct ob_hw *hw)
{
	return ob_d11_wake_override_set(hw, OB_D11_WAKE_OVERRIDE_SUSPEND);
}

int ob_d11_suspend_wake_override_leave(struct ob_hw *hw)
{
	if (!hw || hw->dev_lost)
		return -EIO;
	/* Only a set suspend mask changes anything; the vendor's clear is part
	 * of wlc_bmac_enable_mac and must not clear bit 26 this driver owns
	 * for another reason. */
	if (!(hw->wake_override & OB_D11_WAKE_OVERRIDE_SUSPEND))
		return 0;
	return ob_d11_wake_override_clear(hw, OB_D11_WAKE_OVERRIDE_SUSPEND);
}

/* wlc_phyreg_enter @0x0bafd1 (refcount) -> ucode wake override set. */
int ob_d11_phyreg_enter(struct ob_hw *hw)
{
	if (!hw || hw->dev_lost)
		return -EIO;
	return ob_d11_phyreg_enter_script(&ob_wake_io, hw,
					  &hw->wake_override,
					  &hw->phyreg_refcount);
}

/* wlc_phyreg_exit @0x0bafac: refcount down, clear the override on 1 -> 0. */
int ob_d11_phyreg_exit(struct ob_hw *hw)
{
	if (!hw || hw->dev_lost)
		return -EIO;
	return ob_d11_phyreg_exit_script(&ob_wake_io, hw,
					 &hw->wake_override,
					 &hw->phyreg_refcount);}

int ob_d11_post_ucode_init(struct ob_hw *hw)
{
	const struct ob_fw_chain_policy policy = {
		.runtime_identity_required = true,
		.publish_lifecycle = true,
	};
	struct ob_fw_chain_result result;
	int ret;

	if (!hw || hw->dev_lost)
		return -EIO;
	if (hw->d11_state != OB_D11_ATTACH_IDENTITY_READY)
		return -EINVAL;

	/*
	 * W2: one owner for D2A -> common -> lifecycle.  The former ad-hoc
	 * sequence is retained only inside ob_fw_chain.c, so no caller can
	 * apply common initvals without the identity gate or lose the
	 * post-D2A retention rule.
	 */
	ret = ob_fw_chain_run(hw, "normal-bootstrap", &policy, &result);
	if (ret)
		return ret;

	dev_info(hw->dev,
		 "normal-bootstrap: POST_UCODE_READY state=%u; PHY/radio/DMA/IRQ/mac80211 stopped\n",
		 hw->d11_state);
	return 0;
}

int ob_acphy_attach_identity(struct ob_hw *hw)
{
	struct ob_radio_observation radio;
	u16 phyver;
	int ret;

	if (!hw || hw->dev_lost ||
	    hw->d11_state != OB_D11_CORE_READY)
		return -22;
	dev_info(hw->dev,
		 "OB_ATTACH_CP0_ENTRY state=%u chip=0x%04x chiprev=%u d11rev=%u\n",
		 hw->d11_state, hw->chip_id, hw->chip_rev,
		 hw->core ? hw->core->id.rev : 0u);

	/* The complete BCMA-owned attach reset and target PHY/radio preparation
	 * run here, before firmware/ucode hardware initialization. */
	ret = ob_radio_prepare_for_identity(hw);
	if (ret)
		return ret;
	dev_info(hw->dev, "OB_ATTACH_CP2_PHY_PREREQS_READY\n");
	/*
	 * P1-B: the vendor proves its SHM RAM (OBJADDR/OBJDATA patterns with
	 * readback) immediately before touching the PHY window; our attach path
	 * never did. The probe runs only when the operator selected it, and any
	 * failure stops the run BEFORE the read — the read is the experiment's
	 * subject and must not be spent on an unproven state.
	 */
	if (hw->p1_shm_roundtrip) {
		u32 wioctl = ob_axi_read32(hw, BCMA_IOCTL);

		dev_info(hw->dev,
			 "P1_WITNESS param=shm begin wrapper_0x408=0x%08x\n",
			 wioctl);
		if (hw->dev_lost) {
			dev_err(hw->dev,
				"P1_FAIL_STOP reason=shm_wrapper_read dev_lost=1; the 0x3e0 read is not issued\n");
			return -EIO;
		}
		ret = ob_si_p1_shm_roundtrip(hw);
		if (ret)
			return ret;
	}
	/*
	 * P0 of the tripoint witness: the attach-prefix state, sampled at the
	 * first legal post-prefix read site and before the identity is
	 * published. Read-only; fail-stop like the SHM proof above.
	 */
	if (hw->phy_tripoint_probe) {
		ret = ob_phy_tripoint_observe(hw, OB_PHY_TRIPOINT_P0);
		if (ret) {
			dev_info(hw->dev, "TRIPOINT_STOP point=P0 ret=%d\n", ret);
			return ret;
		}
	}
	phyver = ob_d11_read16(hw, 0x03e0u);
	if (hw->dev_lost || phyver == 0xffffu) {
		dev_info(hw->dev, "OB_ATTACH_FAIL stage=PHYVERSION ret=-5\n");
		return -5;
	}
	if (((phyver >> 8) & 0x0fu) != OB_RADIO_PHYTYPE_AC) {
		dev_info(hw->dev, "OB_ATTACH_FAIL stage=PHYVERSION ret=-95\n");
		return -95;
	}
	dev_info(hw->dev,
		 "OB_ATTACH_CP3_PHY_IDENTITY raw=0x%04x type=0x%02x rev=%u\n",
		 phyver, (phyver >> 8) & 0x0fu, phyver & 0x0fu);

	ret = ob_radio_read_identity(hw, &radio);
	if (ret) {
		dev_info(hw->dev, "OB_ATTACH_FAIL stage=RADIO_IDENTITY ret=%d\n",
			 ret);
		return ret;
	}
	if (!radio.id_is_2069 || radio.radio_rev != 4u) {
		dev_info(hw->dev, "OB_ATTACH_FAIL stage=RADIO_IDENTITY ret=-95\n");
		return -95;
	}
	dev_info(hw->dev,
		 "OB_ATTACH_CP4_RADIO_IDENTITY id=0x%04x rev=%u class=%u\n",
		 radio.radio_id, radio.radio_rev, radio.revision_class);

	/* wlc_phy_attach 0xbed85: attach-path anacore write, after the radio
	 * identity block and before band->pi is published (0x6a140). */
	ret = ob_radio_vendor_acphy_attach_anacore(hw,
						   (u8)((phyver >> 8) & 0x0fu));
	if (ret)
		return ret;

	hw->phy_type = (u8)((phyver >> 8) & 0x0fu);
	hw->phy_revision = (u8)(phyver & 0x0fu);
	hw->radio_id = radio.radio_id;
	hw->radio_revision = radio.radio_rev;
	hw->radio_revision_class = radio.revision_class;
	hw->phy_radio_identity_valid = true;
	hw->radio_access_initialized = true;
	if (!ob_d11_lifecycle_advance(&hw->d11_state,
				OB_D11_ATTACH_IDENTITY_READY)) {
		dev_info(hw->dev,
			 "OB_ATTACH_FAIL stage=STATE_PUBLICATION ret=-5\n");
		hw->phy_radio_identity_valid = false;
		hw->radio_access_initialized = false;
		return -5;
	}

	/* wlc_bmac_up_prep 0x66561: the effective AC reset needs the published
	 * identity (0x6a140/0x6a19c), so it follows the state publication. */
	ret = ob_radio_vendor_acphy_reset(hw, hw->phy_type);
	if (ret)
		return ret;

	dev_info(hw->dev, "OB_ATTACH_CP5_IDENTITY_READY state=%u\n",
		 hw->d11_state);
	return 0;
}

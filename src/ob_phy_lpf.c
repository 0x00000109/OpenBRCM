/* SPDX-License-Identifier: GPL-2.0-only */
#include "ob_phy_lpf.h"
#include "ob_phy_lpf_payloads.h"

static int ob_phy_acphy_lpf_complete(
	const struct ob_phy_access_ops *ops, void *ctx, u16 chip_id,
	u8 phy_revision, u16 *saved_019e_out)
{
	/* In the pinned sub_a04c2 body, revisions 1 and 0x0a take the same
	 * BCM4352 register arm and rev0 table arm. This equivalence ends at the
	 * helper return; it does not admit the rev0x0a sub_a7089 test vector. */
	u16 saved_019e;
	unsigned int descriptor_index, element_index;
	int ret;

	if (!ops || !ops->read16 || !ops->write16 || !saved_019e_out)
		return -22;
	if (chip_id != 0x4352 ||
	    (phy_revision != 1 && phy_revision != 0x0a))
		return -95;
	if (OB_PHY_LPF_DESCRIPTOR_COUNT != 23)
		return -22;

	ret = ob_phy_acphy_lpf_register_prefix_run(
		ops, ctx, chip_id, phy_revision, &saved_019e);
	if (ret)
		return ret;

	for (descriptor_index = 0;
	     descriptor_index < OB_PHY_LPF_DESCRIPTOR_COUNT;
	     descriptor_index++) {
		const struct ob_phy_lpf_descriptor *descriptor =
			&ob_phy_lpf_descriptors[descriptor_index];

		if (!descriptor->element_values || !descriptor->element_count ||
		    (descriptor->element_width_bits != 8 &&
		     descriptor->element_width_bits != 16 &&
		     descriptor->element_width_bits != 32))
			return -22;

		ret = ops->write16(ctx, 0x000d, descriptor->table_id);
		if (ret)
			return ret;
		ret = ops->write16(ctx, 0x000e, descriptor->table_offset);
		if (ret)
			return ret;

		for (element_index = 0;
		     element_index < descriptor->element_count;
		     element_index++) {
			u32 element = descriptor->element_values[element_index];

			if (descriptor->element_width_bits == 8) {
				if (element > 0xff)
					return -22;
				ret = ops->write16(ctx, 0x000f, (u16)element);
				if (ret)
					return ret;
			} else if (descriptor->element_width_bits == 16) {
				if (element > 0xffff)
					return -22;
				ret = ops->write16(ctx, 0x000f, (u16)element);
				if (ret)
					return ret;
			} else {
				ret = ops->write16(ctx, 0x0010, (u16)(element >> 16));
				if (ret)
					return ret;
				ret = ops->write16(ctx, 0x000f, (u16)element);
				if (ret)
					return ret;
			}
		}
	}

	/* Historical phy_reg_mod(mask=2, value=saved bit 1), MASKED_MERGE. */
	ret = ob_phy_rmw16(ops, ctx, 0x019e, 0x0002,
			   (u16)(saved_019e & 0x0002));
	if (ret)
		return ret;

	ret = ops->write16(ctx, 0x1645, 0x025c);
	if (ret)
		return ret;

	*saved_019e_out = saved_019e;
	return 0;
}

enum ob_phy_lpf_input_status ob_phy_acphy_lpf_input_status(
	const struct ob_phy_lpf_target *target,
	bool boardflags3_valid, u32 boardflags3)
{
	if (!target || !target->identity_valid || target->chip_id != 0x4352 ||
	    target->chip_revision != 3 || target->d11_core_id != 0x812 ||
	    target->d11_revision != 42 || target->phy_type != 0x0b ||
	    target->radio_id != 0x2069 || target->radio_revision != 4 ||
	    target->radio_revision_class != 0)
		return OB_PHY_LPF_INPUT_TARGET;
	if (target->phy_revision != 1 && target->phy_revision != 0x0a)
		return OB_PHY_LPF_INPUT_PI164_BRANCH;
	if (!boardflags3_valid)
		return OB_PHY_LPF_INPUT_BOARDFLAGS3;
	if (boardflags3 & OB_PHY_LPF_NESTED_348_BOARD_BIT)
		return OB_PHY_LPF_INPUT_BOARDFLAGS3_BRANCH;
	return OB_PHY_LPF_INPUTS_SUPPORTED;
}

bool ob_phy_acphy_lpf_inputs_supported(
	const struct ob_phy_lpf_target *target,
	bool boardflags3_valid, u32 boardflags3)
{
	return ob_phy_acphy_lpf_input_status(target, boardflags3_valid,
					     boardflags3) ==
		OB_PHY_LPF_INPUTS_SUPPORTED;
}

int ob_phy_acphy_lpf_complete_test_only(
	const struct ob_phy_access_ops *ops, void *ctx, u16 chip_id,
	u8 phy_revision, u16 *saved_019e_out)
{
	return ob_phy_acphy_lpf_complete(ops, ctx, chip_id, phy_revision,
					 saved_019e_out);
}

int ob_phy_acphy_lpf_stage_run(
	const struct ob_phy_access_ops *ops, void *ctx,
	const struct ob_phy_lpf_target *target,
	bool boardflags3_valid, u32 boardflags3,
	struct ob_phy_init_lifecycle *lifecycle,
	u16 *saved_019e_out)
{
	u16 saved_019e;
	int ret;

	if (!ops || !ops->read16 || !ops->write16 || !target || !lifecycle ||
	    !saved_019e_out)
		return -22;
	if (!ob_phy_acphy_lpf_inputs_supported(target, boardflags3_valid,
						boardflags3))
		return -95;
	if (lifecycle->phase != OB_PHY_INIT_RADIO_IN_PROGRESS ||
	    !lifecycle->radio_prefix_complete ||
	    lifecycle->analog_tx_lpf_complete ||
	    lifecycle->radio_sequence_complete)
		return -95;

	/*
	 * The S1 core-mirror block is NOT here. Since the 2026-10-28 relocation it
	 * runs in ob_radio_vendor_pre_id(), right after the prologue: the vendor
	 * issues it 80 ms before this block with the whole 0x164 table stream in
	 * between, and running it here made it mirror the post-ucode generation of
	 * the tuple while this block's own 0x1725 = 0x0000 write erased the 0x0600
	 * the device had produced. See audit/.../phy_prefix_point01/ORDERING_DELTA.md.
	 */
	ret = ob_phy_acphy_lpf_complete(ops, ctx, target->chip_id,
					target->phy_revision, &saved_019e);
	if (ret)
		return ret;
	lifecycle->analog_tx_lpf_complete = true;
	*saved_019e_out = saved_019e;
	return 0;
}

/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_a7089_txpwr.h"
#include "ob_phy_9a121.h"

#define OB_A7089_PI164_PHY040A_SITE 0x0aa57bu
#define OB_A7089_TSSI_PHY_WRITE_SITE 0x0aa61eu
#define OB_A7089_NEXT_UNSUPPORTED_SITE 0x0aa62du
#define OB_A7089_SUB995AB_SITE 0x0aa62du
#define OB_A7089_MAC_SUSPEND_SITE 0x09958bu
#define OB_A7089_SUB9949F_SITE 0x099593u
#define OB_A7089_NEXT_TXPWRCTRL_SITE 0x0aa639u
#define OB_A7089_AFTER_TXPWRCTRL_SITE 0x0aa63eu
#define OB_A7089_POST_GATE_PHY_READ_SITE 0x0aa672u
#define OB_A7089_POST_RXCORE_SITE 0x0aa6a5u
#define OB_A7089_RXCORE_ENTRY_SITE 0x097ce5u
#define OB_A7089_RXCORE_SUSPEND_SITE 0x097d13u
#define OB_A7089_RXCORE_READ_0401_SITE 0x097d20u
#define OB_A7089_RXCORE_READ_0400_SITE 0x097d30u
#define OB_A7089_RXCORE_MOD_0160_SITE 0x097d50u
#define OB_A7089_RXCORE_MOD_0401_70_SITE 0x097d6eu
#define OB_A7089_RXCORE_MOD_0401_7000_SITE 0x097d85u
#define OB_A7089_RXCORE_MOD_0401_7_SITE 0x097d99u
#define OB_A7089_RXCORE_MOD_0400_SITE 0x097db0u
#define OB_A7089_RXCORE_RFSEQ0_SITE 0x097dbau
#define OB_A7089_RXCORE_RFSEQ1_SITE 0x097dc7u
#define OB_A7089_RXCORE_RESTORE_0401_7_SITE 0x097de4u
#define OB_A7089_RXCORE_RESTORE_0401_7000_SITE 0x097dffu
#define OB_A7089_RXCORE_RESTORE_0400_SITE 0x097e10u
#define OB_A7089_RXCORE_ENABLE_SITE 0x097e1du
#define OB_A7089_RFSEQ_READ_0400_SITE 0x097c1fu
#define OB_A7089_RFSEQ_READ_019E_SITE 0x097c2fu
#define OB_A7089_RFSEQ_MOD_019E_2_SITE 0x097c49u
#define OB_A7089_RFSEQ_MOD_019E_1_SITE 0x097c60u
#define OB_A7089_RFSEQ_OR_0400_SITE 0x097c72u
#define OB_A7089_RFSEQ_OR_0402_SITE 0x097c83u
#define OB_A7089_RFSEQ_DELAY_SITE 0x097c99u
#define OB_A7089_RFSEQ_READ_0403_SITE 0x097ca6u
#define OB_A7089_RFSEQ_RESTORE_0400_SITE 0x097cc3u
#define OB_A7089_RFSEQ_RESTORE_019E_SITE 0x097cd4u
#define OB_A7089_POST_RESETCCA_SITE 0x0aa6adu
#define OB_A7089_POST_RXGAINERR_SITE 0x0aa6b9u
#define OB_A7089_POST_PHY0400_SITE 0x0aa74du
#define OB_A7089_POST_CARRIERSEARCH_SITE 0x0aa757u
#define OB_A7089_CARRIER_CLASSIFIER_READ_SITE 0x091273u
#define OB_A7089_CARRIER_CLASSIFIER_WRITE_SITE 0x091293u
#define OB_A7089_CARRIER_OFDM_2ED_SITE 0x08faaeu
#define OB_A7089_CARRIER_OFDM_2F1_SITE 0x08fac5u
#define OB_A7089_CARRIER_OFDM_2F5_SITE 0x08fadcu
#define OB_A7089_CARRIER_OFDM_2F9_SITE 0x08fb30u
#define OB_A7089_CARRIER_SUB979BC_WRITE_SITE 0x097a16u
#define OB_A7089_CARRIER_SUB979BC_AND_SITE 0x097a2eu
#define OB_A7089_CARRIER_PHY339_WRITE_SITE 0x097b2eu
#define OB_A7089_TXPWRCTRL_NONZERO_SITE 0x09077cu
#define OB_A7089_TXPWRCTRL_CORE_SITE 0x08f414u
#define OB_A7089_SUB93E47_DELAY_SITE 0x093e61u
#define OB_A7089_SUB93E47_RADIO_SITE 0x093e95u
#define OB_A7089_CALL_LOOP_BOUND 2u
#define OB_A7089_NESTED_CONFIG_LENGTH 4u
#define OB_A7089_SUB93E47_MAX_POLLS 100u
#define OB_A7089_RFSEQ_MAX_READS 20001u

static int ob_a7089_validate(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_txpwr_input *input)
{
	const struct ob_phy_96203_input *p;
	u8 i;

	if (!prefix_ops || !prefix_ops->read16 || !prefix_ops->write16 ||
	    !prefix_ops->delay_us || !prefix_ops->phyclk_fgc || !tx_ops ||
	    !tx_ops->phy_read16 || !tx_ops->phy_write16 ||
	    !tx_ops->table_read || !tx_ops->table_write || !input ||
	    !input->nested_power_indices ||
	    input->nested_power_indices_length < OB_A7089_CALL_LOOP_BOUND ||
	    !input->nested_txpwrctrl_values ||
	    input->nested_txpwrctrl_values_length < OB_A7089_CALL_LOOP_BOUND ||
	    !input->nested_342_present)
		return -EINVAL;
	if (input->sub995ab_tail_present &&
	    (!input->pi_fa2_present || !input->pi_17e_present ||
	     !input->nested_456 ||
	     input->nested_456_length < OB_A7089_NESTED_CONFIG_LENGTH ||
	     !input->phy_context_31_present || !input->saved_pi_fa0_present ||
	     !input->mac_state_present ||
	     input->mac_state_at_99528 < OB_A7089_MAC_UNKNOWN ||
	     input->mac_state_at_99528 > OB_A7089_MAC_SUSPENDED))
		return -EINVAL;
	if (input->post_aa63e_inputs_present &&
	    (!input->sub995ab_tail_present ||
	     !input->post_aa63e_lookup_mask_present ||
	     input->post_aa63e_lookup_nonnull_mask != 1u ||
	     !input->post_aa63e_nested_32c_snapshot_present ||
	     !input->post_aa63e_rxgainerr_state_present ||
	     input->post_aa63e_phy_type != 0x0b ||
	     !input->post_aa63e_sub8f086_state_present ||
	     !input->nested_candidate_counts_6c2_through_6c7_present ||
	     !input->nested_gainerr_state_211 ||
	     input->nested_gainerr_state_211_length < 5 ||
	     !input->post_aa63e_carriersearch_state_present ||
	     !input->nested_carriersearch_count ||
	     !input->post_aa63e_nested_904_present ||
	     (input->post_resetcca.pi_164 == 0 &&
	      !input->post_aa63e_nested_902_present) ||
	     (input->post_aa63e_nested_32c_snapshot != 0 &&
	      (!input->post_aa63e_pi_a7_present ||
		       !input->post_aa63e_pi_a4_present ||
		       !input->post_aa63e_pi_a6_present))))
		return -EINVAL;
	if (input->post_aa63e_inputs_present) {
		for (i = 0; i < 6; i++)
			if (!input->nested_candidate_counts_64a_through_64f[i] ||
			    input->nested_candidate_counts_64a_through_64f[i] > 6 ||
			    !input->nested_candidate_counts_6c2_through_6c7[i] ||
			    input->nested_candidate_counts_6c2_through_6c7[i] > 6)
				return -EINVAL;
	}

	p = &input->post_resetcca;
	if (!p->explicitly_synthetic || p->chip_id != 0x4352 ||
	    p->chip_revision != 3 || p->phy_revision != 0x0a ||
	    p->radio_id != 0x2069 || p->radio_revision != 4 ||
	    p->radio_class != 0 || !p->acphychipid_present ||
	    p->acphychipid != 0x4352 || !p->loop_bound_present ||
	    p->loop_bound != OB_A7089_CALL_LOOP_BOUND ||
	    !p->second_loop_gate_present || !p->caller_gate_present ||
	    !p->pi_164_present)
		return -EINVAL;

	return 0;
}

static int ob_a7089_phy040a_rmw(const struct ob_phy_post_lpf_ops *ops,
				void *ctx,
				struct ob_phy_a7089_txpwr_result *result)
{
	u16 old;
	u8 logical_event;
	int ret;

	result->logical_event_count++;
	logical_event = (u8)result->logical_event_count;
	ret = ops->read16(ctx, logical_event, OB_A7089_PI164_PHY040A_SITE,
			  OB_PHY_POST_LPF_PHY, 0x040a, &old);
	result->callback_count++;
	if (ret) {
		result->failed_site = OB_A7089_PI164_PHY040A_SITE;
		return ret;
	}
	ret = ops->write16(ctx, logical_event, OB_A7089_PI164_PHY040A_SITE,
			   OB_PHY_POST_LPF_PHY, 0x040a,
			   (u16)((old & (u16)~0x0200u) | 0x0200u));
	result->callback_count++;
	if (ret)
		result->failed_site = OB_A7089_PI164_PHY040A_SITE;
	return ret;
}

static s8 ob_a7089_sign_extend_u8(u8 value)
{
	int signed_value = value;

	if (value & 0x80u)
		signed_value -= 0x100;
	return (s8)signed_value;
}

static int ob_a7089_txpwrctrl_arg0(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result)
{
	u16 sample;
	u8 core;
	int ret;

	/* Caller @0xaa5cb passes arg=0. The callee stores PI+0x0fa0=0. */
	result->txpwrctrl_arg0_written = true;
	result->pi_fa0_after_txpwrctrl = 0;
	ret = ops->phy_read16(ctx, 0x9071a, 0x0070, &sample);
	result->txpwrctrl_callback_count++;
	result->callback_count++;
	result->txpwrctrl_logical_event_count++;
	result->logical_event_count++;
	if (ret) {
		result->failed_site = 0x9071a;
		return ret;
	}

	/* JNE @0x90727 skips sub_906ae when the masked sample is not 0xe000.
	 * On equality, the loop is tested at @0x90748 and visits each PI+0x168
	 * core, storing the returned byte to nested+0x45a+core @0x90740. */
	if ((sample & 0xe000u) == 0xe000u) {
		result->txpwrctrl_per_core_reads = true;
		for (core = 0; core < input->post_resetcca.loop_bound; core++) {
			static const u16 phy_reg[2] = {0x0640, 0x0840};
			u16 core_sample;

			ret = ops->phy_read16(ctx, 0x906d7, phy_reg[core],
					      &core_sample);
			result->txpwrctrl_callback_count++;
			result->callback_count++;
			result->txpwrctrl_logical_event_count++;
			result->logical_event_count++;
			if (ret) {
				result->failed_site = 0x906d7;
				return ret;
			}
			input->nested_txpwrctrl_values[core] =
				(u8)((core_sample & 0x7f00u) >> 8);
		}
	}

	/* Common selected tail @0x90751/@0x90760 on both PHY[0x0070] arms. */
	result->txpwrctrl_logical_event_count++;
	result->logical_event_count++;
	ret = ops->phy_read16(ctx, 0x90760, 0x0070, &sample);
	result->txpwrctrl_callback_count++;
	result->callback_count++;
	if (ret) {
		result->failed_site = 0x90760;
		return ret;
	}
	ret = ops->phy_write16(ctx, 0x90760, 0x0070,
			       (u16)(sample & (u16)~0xe000u));
	result->txpwrctrl_callback_count++;
	result->callback_count++;
	if (ret)
		result->failed_site = 0x90760;
	return ret;
}

static int ob_a7089_txpwrctrl_rmw(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 site, u16 reg,
	u16 mask, u16 value)
{
	u16 old;
	int ret;

	/* One logical phy_reg_mod, expanded into its historical read/write backend
	 * callbacks. The write uses the value actually read from persistent state. */
	result->logical_event_count++;
	result->txpwrctrl_logical_event_count++;
	ret = ops->phy_read16(ctx, site, reg, &old);
	result->callback_count++;
	result->txpwrctrl_callback_count++;
	if (ret) {
		result->failed_site = site;
		return ret;
	}
	ret = ops->phy_write16(ctx, site, reg,
			       (u16)((old & (u16)~mask) | (value & mask)));
	result->callback_count++;
	result->txpwrctrl_callback_count++;
	if (ret)
		result->failed_site = site;
	return ret;
}

static int ob_a7089_txpwrctrl_nonzero(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result)
{
	static const u16 core_reg[OB_A7089_CALL_LOOP_BOUND] = {
		0x0644, 0x0844,
	};
	u8 arg = input->saved_pi_fa0;
	u8 core;
	int ret;

	result->txpwrctrl_nonzero_path = true;
	result->txpwrctrl_nonzero_argument = arg;
	/* The earlier caller invocation used arg=0 and already stored zero. */
	result->pi_fa0_after_txpwrctrl = 0;
	if (arg == 1) {
		/* cmp sil,1; ja skips this byte store only for values greater than 1. */
		result->pi_fa0_after_txpwrctrl = 1;
		result->txpwrctrl_nonzero_store = true;
	}

	/* All nonzero bytes join at @0x90767 and set the PHY[0x70] field. */
	ret = ob_a7089_txpwrctrl_rmw(ops, ctx, result,
				       OB_A7089_TXPWRCTRL_NONZERO_SITE,
				       0x0070, 0xe000, 0xe000);
	if (ret)
		return ret;

	/* sub_8f3d4 is called for each non-0x80 byte. The selected BCM4352
	 * attach bound is two, so the proven caller indices are only 0 and 1. */
	for (core = 0; core < input->post_resetcca.loop_bound; core++) {
		u8 byte = input->nested_txpwrctrl_values[core];

		if (byte == 0x80)
			continue;
		ret = ob_a7089_txpwrctrl_rmw(ops, ctx, result,
					       OB_A7089_TXPWRCTRL_CORE_SITE,
					       core_reg[core], 0x007f,
					       (u16)byte);
		if (ret)
			return ret;
		result->txpwrctrl_core_updates++;
	}
	return 0;
}

static int ob_a7089_delay(
	const struct ob_phy_post_lpf_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 site, u16 usec)
{
	u8 logical_event = (u8)(result->logical_event_count + 1u);
	int ret;

	result->logical_event_count++;
	result->tail_logical_event_count++;
	ret = ops->delay_us(ctx, logical_event, site, usec);
	result->callback_count++;
	result->tail_callback_count++;
	if (ret)
		result->failed_site = site;
	return ret;
}

static int ob_a7089_sub93e47(
	const struct ob_phy_post_lpf_ops *ops, void *ctx,
	const struct ob_phy_96203_input *profile,
	struct ob_phy_a7089_txpwr_result *result)
{
	u16 radio_reg;
	u8 attempt;

	/* The global CHIPID dispatch at @0x93e66 selects 0x090b for these IDs;
	 * the explicit BCM4352 synthetic profile therefore uses 0x090b. */
	switch (profile->chip_id) {
	case 0x4352:
	case 0x4360:
	case 0xa9c4:
	case 0xaa06:
		radio_reg = 0x090b;
		break;
	default:
		radio_reg = 0x091d;
		break;
	}
	result->post_aa63e_radio_poll_selected = true;
	for (attempt = 0; attempt < OB_A7089_SUB93E47_MAX_POLLS; attempt++) {
		u8 logical_event;
		u16 sample;
		int ret;

		result->post_aa63e_poll_attempts = (u8)(attempt + 1u);
		ret = ob_a7089_delay(ops, ctx, result,
				     OB_A7089_SUB93E47_DELAY_SITE, 10);
		if (ret)
			return ret;

		logical_event = (u8)(result->logical_event_count + 1u);
		result->logical_event_count++;
		result->tail_logical_event_count++;
		ret = ops->read16(ctx, logical_event,
				  OB_A7089_SUB93E47_RADIO_SITE,
				  OB_PHY_POST_LPF_RADIO, radio_reg, &sample);
		result->callback_count++;
		result->tail_callback_count++;
		if (ret) {
			result->failed_site = OB_A7089_SUB93E47_RADIO_SITE;
			return ret;
		}
		if (sample & 0x0100u) {
			result->post_aa63e_poll_satisfied = true;
			return 0;
		}
	}
	/* Poll exhaustion falls through normally in the vendor helper. The caller
	 * passes arg=0, so the optional 120-us delay arm is not selected. */
	result->post_aa63e_poll_exhausted = true;
	return 0;
}

static u8 ob_a7089_rxgainerr_band(u8 chanspec)
{
	if (chanspec <= 0x0e)
		return 0;
	if (chanspec <= 0x30)
		return 1;
	if (chanspec <= 0x40)
		return 2;
	if (chanspec <= 0x80)
		return 3;
	return 4;
}

static int ob_a7089_rxcore_read(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 site, u16 reg,
	u16 *value)
{
	int ret = ops->phy_read16(ctx, site, reg, value);

	result->logical_event_count++;
	result->tail_logical_event_count++;
	result->callback_count++;
	result->tail_callback_count++;
	if (ret)
		result->failed_site = site;
	return ret;
}

static int ob_a7089_rxcore_write(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 site, u16 reg,
	u16 value)
{
	int ret = ops->phy_write16(ctx, site, reg, value);

	result->logical_event_count++;
	result->tail_logical_event_count++;
	result->callback_count++;
	result->tail_callback_count++;
	if (ret)
		result->failed_site = site;
	return ret;
}

static int ob_a7089_rxcore_rmw(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 site, u16 reg,
	u16 mask, u16 value)
{
	u16 old;
	int ret;

	ret = ob_a7089_rxcore_read(ops, ctx, result, site, reg, &old);
	if (ret)
		return ret;
	ret = ob_a7089_rxcore_write(ops, ctx, result, site, reg,
		(u16)((old & (u16)~mask) | (value & mask)));
	/* One logical phy_reg_mod expands to two backend callbacks. */
	result->logical_event_count--;
	result->tail_logical_event_count--;
	return ret;
}

static int ob_a7089_rxcore_or(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 site, u16 reg,
	u16 value)
{
	u16 old;
	int ret;

	ret = ob_a7089_rxcore_read(ops, ctx, result, site, reg, &old);
	if (ret)
		return ret;
	ret = ob_a7089_rxcore_write(ops, ctx, result, site, reg,
					 (u16)(old | value));
	/* phy_reg_or is also a single logical helper around read plus write. */
	result->logical_event_count--;
	result->tail_logical_event_count--;
	return ret;
}

static int ob_a7089_rfseq(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u8 call_index, u8 argument)
{
	static const u16 force_mask[2] = { 2u, 1u };
	u32 counter = 0x30d49u;
	u16 saved_0400;
	u16 saved_019e;
	u16 sample;
	u32 read_index;
	int ret;

	/* Only the two caller-proven invocations, argument 0 then 1, are admitted. */
	if (call_index > 1u || argument != call_index)
		return -EINVAL;
	ret = ob_a7089_rxcore_read(tx_ops, ctx, result,
					OB_A7089_RFSEQ_READ_0400_SITE, 0x0400,
					&saved_0400);
	if (ret)
		return ret;
	ret = ob_a7089_rxcore_read(tx_ops, ctx, result,
					OB_A7089_RFSEQ_READ_019E_SITE, 0x019e,
					&saved_019e);
	if (ret)
		return ret;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RFSEQ_MOD_019E_2_SITE,
					 0x019e, 0x0002, 0x0002);
	if (ret)
		return ret;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RFSEQ_MOD_019E_1_SITE,
					 0x019e, 0x0001, 0x0001);
	if (ret)
		return ret;
	ret = ob_a7089_rxcore_or(tx_ops, ctx, result,
				  OB_A7089_RFSEQ_OR_0400_SITE, 0x0400, 0x0003);
	if (ret)
		return ret;
	ret = ob_a7089_rxcore_or(tx_ops, ctx, result,
				  OB_A7089_RFSEQ_OR_0402_SITE, 0x0402,
				  force_mask[argument]);
	if (ret)
		return ret;

	/* Historical code reads first, then delays 10 us after each clear sample;
	 * the 0x30d49 countdown permits 20,001 reads and 20,000 delays. */
	for (read_index = 0; read_index < OB_A7089_RFSEQ_MAX_READS;
	     read_index++) {
		ret = ob_a7089_rxcore_read(tx_ops, ctx, result,
						 OB_A7089_RFSEQ_READ_0403_SITE,
						 0x0403, &sample);
		if (ret)
			return ret;
		result->post_aa63e_rfseq_poll_reads[call_index]++;
		/* Vendor wlc_phy_force_rfseq_acphy @0x97cab..0x97cb5: the poll
		 * exits when the force bit CLEARS (0x97caf je) and loops while it
		 * is still set (0x97cb5 jne).  The former predicate was inverted
		 * (W2-P1) and burned the whole 20001-read bound on a clear
		 * sample, adding ~200 ms per call the vendor never spends. */
		if (!(sample & force_mask[argument]))
			break;
		if (counter == 9u) {
			result->post_aa63e_rfseq_poll_exhausted[call_index] = true;
			break;
		}
		counter -= 10u;
		{
			u8 logical = (u8)(result->logical_event_count + 1u);
			ret = prefix_ops->delay_us(ctx, logical,
						  OB_A7089_RFSEQ_DELAY_SITE, 10);
			result->logical_event_count++;
			result->tail_logical_event_count++;
			result->callback_count++;
			result->tail_callback_count++;
			result->post_aa63e_rfseq_poll_delays[call_index]++;
			if (ret) {
				result->failed_site = OB_A7089_RFSEQ_DELAY_SITE;
				return ret;
			}
		}
	}
	ret = ob_a7089_rxcore_write(tx_ops, ctx, result,
					 OB_A7089_RFSEQ_RESTORE_0400_SITE,
					 0x0400, saved_0400);
	if (ret)
		return ret;
	return ob_a7089_rxcore_write(tx_ops, ctx, result,
					OB_A7089_RFSEQ_RESTORE_019E_SITE,
					0x019e, saved_019e);
}

static int ob_a7089_rxcore_setstate(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_platform_ops *platform_ops, void *ctx,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result, u8 state)
{
	u16 saved_0401;
	u16 saved_0400;
	int ret;

	result->post_aa63e_rxcore_setstate_called = true;
	result->post_aa63e_rxcore_argument = state;
	/* @0x97cfa stores the argument to shared+0xa7 unconditionally, before the
	 * gate at @0x97d05, so both arms share this software effect. */
	result->post_aa63e_rxcore_a7_store = true;

	/* @0x97d05/@0x97d09: je to the epilogue @0x97e22 when shared+0x31 is
	 * zero. That arm performs no PHY access and no MAC operation. */
	if (input->phy_context_31 == 0) {
		result->post_aa63e_rxcore_software_only = true;
		return 0;
	}

	/* Nonzero selects the hardware arm. Both MAC callbacks are mandatory:
	 * the vendor brackets the sequence with wlapi_suspend_mac_and_wait
	 * @0x097d13 and wlapi_enable_mac @0x097e1d. Without them the arm stays
	 * fail-closed rather than running the sequence with an unowned MAC. */
	if (!platform_ops || !platform_ops->suspend_mac_and_wait ||
	    !platform_ops->enable_mac) {
		result->status = OB_A7089_TXPWR_UNSUPPORTED;
		result->unsupported_site =
			(!platform_ops || !platform_ops->suspend_mac_and_wait) ?
			OB_A7089_RXCORE_SUSPEND_SITE : OB_A7089_RXCORE_ENABLE_SITE;
		result->terminal_boundary = result->unsupported_site;
		return -EOPNOTSUPP;
	}
	result->post_aa63e_rxcore_mac_sequence = true;
	{
		u8 logical_event = (u8)(result->logical_event_count + 1u);

		result->logical_event_count++;
		result->tail_logical_event_count++;
		result->callback_count++;
		result->tail_callback_count++;
		ret = platform_ops->suspend_mac_and_wait(ctx, logical_event,
							OB_A7089_RXCORE_SUSPEND_SITE);
		if (ret) {
			result->status = OB_A7089_TXPWR_FAILED_BACKEND;
			result->failed_site = OB_A7089_RXCORE_SUSPEND_SITE;
			result->terminal_boundary = result->failed_site;
			result->mac_state = OB_A7089_MAC_UNKNOWN;
			return ret;
		}
	}
	result->post_aa63e_rxcore_mac_suspended = true;
	result->mac_state = OB_A7089_MAC_SUSPENDED;

	ret = ob_a7089_rxcore_read(tx_ops, ctx, result,
					OB_A7089_RXCORE_READ_0401_SITE, 0x0401,
					&saved_0401);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_read(tx_ops, ctx, result,
					OB_A7089_RXCORE_READ_0400_SITE, 0x0400,
					&saved_0400);
	if (ret)
		goto mac_left_suspended;
	result->post_aa63e_rxcore_saved_0401 = saved_0401;
	result->post_aa63e_rxcore_saved_0400 = saved_0400;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RXCORE_MOD_0160_SITE,
					 0x0160, 0x0007, state);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RXCORE_MOD_0401_70_SITE,
					 0x0401, 0x0070,
					 (u16)(((u16)state << 4) & 0x0ff0));
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RXCORE_MOD_0401_7000_SITE,
					 0x0401, 0x7000, 0x7000);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RXCORE_MOD_0401_7_SITE,
					 0x0401, 0x0007, 0);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RXCORE_MOD_0400_SITE,
					 0x0400, 0x0001, 1);
	if (ret)
		goto mac_left_suspended;
	/* @0x097dba and @0x097dc7 delegate wlc_phy_force_rfseq_acphy(pi, 0|1). */
	ret = ob_a7089_rfseq(prefix_ops, tx_ops, ctx, result, 0, 0);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rfseq(prefix_ops, tx_ops, ctx, result, 1, 1);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RXCORE_RESTORE_0401_7_SITE,
					 0x0401, 0x0007,
					 input->post_aa63e_pi_a6);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_rmw(tx_ops, ctx, result,
					 OB_A7089_RXCORE_RESTORE_0401_7000_SITE,
					 0x0401, 0x7000, saved_0401 & 0x7000);
	if (ret)
		goto mac_left_suspended;
	ret = ob_a7089_rxcore_write(tx_ops, ctx, result,
					OB_A7089_RXCORE_RESTORE_0400_SITE,
					0x0400, saved_0400);
	if (ret)
		goto mac_left_suspended;

	/* @0x097e1d delegates wlapi_enable_mac, the reference-counted
	 * counterpart of the suspend above. */
	{
		u8 logical_event = (u8)(result->logical_event_count + 1u);

		result->logical_event_count++;
		result->tail_logical_event_count++;
		result->callback_count++;
		result->tail_callback_count++;
		ret = platform_ops->enable_mac(ctx, logical_event,
					       OB_A7089_RXCORE_ENABLE_SITE);
		if (ret) {
			result->status = OB_A7089_TXPWR_FAILED_BACKEND;
			result->failed_site = OB_A7089_RXCORE_ENABLE_SITE;
			result->terminal_boundary = result->failed_site;
			result->post_aa63e_rxcore_reentry_forbidden = true;
			return ret;
		}
	}
	result->post_aa63e_rxcore_mac_restored = true;
	result->mac_state = OB_A7089_MAC_ENABLED;
	return 0;

mac_left_suspended:
	/* The pinned function has no error return between suspend and enable, so
	 * there is no vendor recovery to copy. OpenBRCM fail-stops here, records
	 * that the MAC is still suspended, and forbids a second run instead of
	 * synthesizing a resume the original never performs. */
	result->post_aa63e_rxcore_reentry_forbidden = true;
	return ret;
}

static int ob_a7089_post_aa63e_rmw0400(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result)
{
	u8 logical_event = (u8)(result->logical_event_count + 1u);
	u16 old;
	int ret;

	result->post_aa63e_0400_selected = true;
	result->logical_event_count++;
	result->tail_logical_event_count++;
	ret = ops->phy_read16(ctx, OB_A7089_POST_PHY0400_SITE, 0x0400, &old);
	result->callback_count++;
	result->tail_callback_count++;
	if (ret) {
		result->failed_site = OB_A7089_POST_PHY0400_SITE;
		return ret;
	}
	ret = ops->phy_write16(ctx, OB_A7089_POST_PHY0400_SITE, 0x0400,
			       (u16)(old & (u16)~1u));
	result->callback_count++;
	result->tail_callback_count++;
	if (ret)
		result->failed_site = OB_A7089_POST_PHY0400_SITE;
	(void)logical_event; /* txpwr callbacks carry source site, not event index. */
	return ret;
}

static int ob_a7089_carrier_rmw(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 read_site,
	u32 write_site, u16 reg, u16 and_mask, u16 or_value)
{
	u16 old;
	int ret;

	result->logical_event_count++;
	result->tail_logical_event_count++;
	ret = ops->phy_read16(ctx, read_site, reg, &old);
	result->callback_count++;
	result->tail_callback_count++;
	if (ret) {
		result->failed_site = read_site;
		return ret;
	}
	ret = ops->phy_write16(ctx, write_site, reg,
			       (u16)((old & and_mask) | or_value));
	result->callback_count++;
	result->tail_callback_count++;
	if (ret)
		result->failed_site = write_site;
	return ret;
}

static int ob_a7089_carrier_write(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	struct ob_phy_a7089_txpwr_result *result, u32 site, u16 reg,
	u16 value)
{
	int ret;

	result->logical_event_count++;
	result->tail_logical_event_count++;
	ret = ops->phy_write16(ctx, site, reg, value);
	result->callback_count++;
	result->tail_callback_count++;
	if (ret)
		result->failed_site = site;
	return ret;
}

static int ob_a7089_stay_in_carriersearch_arg0(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	const struct ob_phy_96203_input *profile,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result)
{
	static const u16 ofdm_regs[4] = {0x02ed, 0x02f1, 0x02f5, 0x02f9};
	static const u32 ofdm_sites[4] = {
		OB_A7089_CARRIER_OFDM_2ED_SITE,
		OB_A7089_CARRIER_OFDM_2F1_SITE,
		OB_A7089_CARRIER_OFDM_2F5_SITE,
		OB_A7089_CARRIER_OFDM_2F9_SITE,
	};
	u16 chanspec_flags;
	u8 core, i;
	int ret;

	result->post_aa63e_carriersearch_entered = true;
	/* The callee reads a dword but decrements/stores/tests AX only. Model the
	 * observed low word with historical u16 wraparound. */
	*input->nested_carriersearch_count =
		(u16)(*input->nested_carriersearch_count - 1u);
	result->post_aa63e_carriersearch_count_after =
		*input->nested_carriersearch_count;
	if (*input->nested_carriersearch_count != 0) {
		result->post_aa63e_carriersearch_completed = true;
		return 0;
	}
	result->post_aa63e_carriersearch_effects_selected = true;

	/* @0x97ae0 derives the classifier mask from (u16(PI+0x17e)&0xc000)<1;
	 * the exact cmp/sbb/not/add sequence yields 7 for zero and 6 otherwise. */
	chanspec_flags = (u16)(input->pi_17e & 0xc000u);
	result->post_aa63e_classifier_mask =
		(chanspec_flags < 1u) ? 7u : 6u;
	ret = ob_a7089_carrier_rmw(
		ops, ctx, result, OB_A7089_CARRIER_CLASSIFIER_READ_SITE,
		OB_A7089_CARRIER_CLASSIFIER_WRITE_SITE, 0x0140,
		(u16)~result->post_aa63e_classifier_mask,
		(u16)(7u & result->post_aa63e_classifier_mask));
	if (ret)
		return ret;

	/* wlc_phy_ofdm_crs_acphy(PI,1) sets bit 4 in three CRS registers and
	 * the common fourth register, in the instruction order below. */
	for (i = 0; i < 4; i++) {
		ret = ob_a7089_carrier_rmw(
			ops, ctx, result, ofdm_sites[i], ofdm_sites[i],
			ofdm_regs[i], (u16)~0x0010u, 0x0010u);
		if (ret)
			return ret;
	}

	/* sub_979bc(PI,1) uses PI+0x168 iterations and the dword PI+0x164
	 * discriminator. The zero branch writes nested+0x902; the nonzero
	 * branch ANDs each core-strided PHY register with 0xbfff. */
	for (core = 0; core < profile->loop_bound; core++) {
		if (profile->pi_164 == 0) {
			ret = ob_a7089_carrier_write(
				ops, ctx, result,
				OB_A7089_CARRIER_SUB979BC_WRITE_SITE,
				(u16)(0x06dau + core * 0x0200u),
				input->post_aa63e_nested_902);
		} else {
			ret = ob_a7089_carrier_rmw(
				ops, ctx, result,
				OB_A7089_CARRIER_SUB979BC_AND_SITE,
				OB_A7089_CARRIER_SUB979BC_AND_SITE,
				(u16)(0x06d4u + core * 0x0200u),
				0xbfffu, 0);
		}
		if (ret)
			return ret;
	}

	/* Caller @0x97b1d loads the unstrided nested+0x904 word. */
	ret = ob_a7089_carrier_write(ops, ctx, result,
				     OB_A7089_CARRIER_PHY339_WRITE_SITE,
				     0x0339,
				     input->post_aa63e_nested_904);
	if (ret)
		return ret;
	result->post_aa63e_carriersearch_completed = true;
	return 0;
}

static int ob_a7089_post_aa63e_continue(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_platform_ops *platform_ops, void *ctx,
	const struct ob_phy_96203_input *profile,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result)
{
	static const u8 rxgainerr_sources[5] = { 0, 5, 10, 15, 20 };
	static const u8 rxgainerr_flags[5] = { 4, 9, 14, 19, 24 };
	u16 rxgainerr_phy[2] = { 0, 0 };
	u8 band;
	u8 core;
	int ret;

	result->post_aa63e_entered = true;
	result->post_aa63e_lookup_nonnull_mask =
		input->post_aa63e_lookup_nonnull_mask;
	memcpy(result->nested_gainerr_state_after,
	       input->nested_gainerr_state_211,
	       sizeof(result->nested_gainerr_state_after));
	/* Reaching this continuation requires sub_8e9b1 to have returned AL=1.
	 * That return writes exactly one of the four output row pointers, so the
	 * earlier caller's any-nonnull test is necessarily taken. */
	ret = ob_a7089_sub93e47(prefix_ops, ctx, profile, result);
	if (ret)
		return ret;

	/* [rbp-0x79] is captured from nested+0x32c at function entry @0xa70ca.
	 * The live nested byte used at @0xaa52a remains an independent input. */
	result->post_aa63e_nested_gate_nonzero =
		input->post_aa63e_nested_32c_snapshot != 0;
	if (input->post_aa63e_nested_32c_snapshot) {
		u16 sample;
		u8 logical_event = (u8)(result->logical_event_count + 1u);
		u8 mask;

		result->logical_event_count++;
		result->tail_logical_event_count++;
		ret = prefix_ops->read16(ctx, logical_event,
					 OB_A7089_POST_GATE_PHY_READ_SITE,
					 OB_PHY_POST_LPF_PHY, 0x000b, &sample);
		result->callback_count++;
		result->tail_callback_count++;
		if (ret) {
			result->failed_site = OB_A7089_POST_GATE_PHY_READ_SITE;
			return ret;
		}
		mask = (u8)((1u << (sample & 7u)) - 1u);
		if (input->post_aa63e_pi_a7 != mask ||
		    input->post_aa63e_pi_a4 != input->post_aa63e_pi_a7) {
			ret = ob_a7089_rxcore_setstate(prefix_ops, tx_ops,
							platform_ops, ctx, input,
							result,
							input->post_aa63e_pi_a7);
			if (ret) {
				if (result->status != OB_A7089_TXPWR_UNSUPPORTED) {
					result->status = OB_A7089_TXPWR_FAILED_BACKEND;
					result->terminal_boundary = result->failed_site;
				}
				return ret;
			}
		}
	}

	/* Reuse the same verified resetCCA body as the caller at @0xaa538. */
	ret = ob_phy_96203_resetcca_test_only(prefix_ops, ctx, profile,
					      result->logical_event_count,
					      &result->post_aa63e_resetcca);
	result->logical_event_count +=
		result->post_aa63e_resetcca.logical_event_count;
	result->callback_count += result->post_aa63e_resetcca.callback_count;
	result->tail_logical_event_count +=
		result->post_aa63e_resetcca.logical_event_count;
	result->tail_callback_count += result->post_aa63e_resetcca.callback_count;
	if (ret) {
		result->failed_site = result->post_aa63e_resetcca.failed_site;
		return ret;
	}

	/* wlc_phy_get_rxgainerr_phy is software-only. This BCM4352 AC-profile
	 * model consumes an explicit copy of the five PI rows it can select. */
	band = ob_a7089_rxgainerr_band((u8)input->pi_17e);
	result->post_aa63e_rxgainerr_band = band;
	for (core = 0; core < profile->loop_bound; core++)
		rxgainerr_phy[core] = (u16)(s16)(s8)
			input->pi_rxgainerr_1e3_through_1fb[
				rxgainerr_sources[band] + core];
	result->post_aa63e_rxgainerr_return =
		input->pi_rxgainerr_1e3_through_1fb[rxgainerr_flags[band]];
	memcpy(result->post_aa63e_rxgainerr_values, rxgainerr_phy,
	       sizeof(rxgainerr_phy));
	result->post_aa63e_rxgainerr_completed = true;

	if (result->post_aa63e_rxgainerr_return) {
		/* The helper's true return selects the byte-clear arm at @0xaa6d8. */
		for (core = 0; core < profile->loop_bound; core++) {
			input->nested_gainerr_state_211[core] = 0;
			result->post_aa63e_cores_completed++;
		}
	} else {
		for (core = 0; core < profile->loop_bound; core++) {
			u8 generated[6];
			u8 helper_return;
			u8 adjusted;
			u8 stored;

			ret = ob_phy_9a121_sub8f086_test_only(
				input->nested_coefficients_450_through_571,
				core == 0 ?
				input->nested_candidate_counts_64a_through_64f :
				input->nested_candidate_counts_6c2_through_6c7,
				input->nested_initial_650_through_655,
				core, 0x45, 0, generated, &helper_return);
			if (ret) {
				result->status = OB_A7089_TXPWR_INVALID;
				result->terminal_boundary = OB_A7089_POST_RXGAINERR_SITE;
				return ret;
			}
			memcpy(result->post_aa63e_sub8f086_output[core], generated,
			       sizeof(generated));
			result->post_aa63e_sub8f086_returns[core] = helper_return;
			adjusted = (u8)((u16)helper_return * 2u - 4u);
			stored = (u8)((u16)rxgainerr_phy[core] - 0x7au - adjusted);
			input->nested_gainerr_state_211[1u + core] = stored;
			result->post_aa63e_cores_completed++;
		}
	}
	memcpy(result->nested_gainerr_state_after,
	       input->nested_gainerr_state_211,
	       sizeof(result->nested_gainerr_state_after));

	if (profile->pi_164 == 2 || profile->pi_164 == 5 ||
	    profile->pi_164 == 6) {
		ret = ob_a7089_post_aa63e_rmw0400(tx_ops, ctx, result);
		if (ret)
			return ret;
	}

	/* The caller passes arg0 to wlc_phy_stay_in_carriersearch_acphy @0xaa757.
	 * Its counter and nested+0x904 state are explicit inputs because this
	 * runner starts mid-function and does not fabricate the earlier entry. */
	ret = ob_a7089_stay_in_carriersearch_arg0(tx_ops, ctx, profile, input,
						  result);
	if (ret)
		return ret;
	result->status = OB_A7089_TXPWR_SYNTHETIC_COMPLETE;
	result->terminal_boundary = 0x0aa781u;
	result->hardware_ready = false;
	return 0;
}

static int ob_a7089_sub995ab_tail(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_platform_ops *platform_ops, void *ctx,
	const struct ob_phy_96203_input *prefix,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result)
{
	u8 source[OB_A7089_NESTED_CONFIG_LENGTH];
	bool changed = false;
	u8 i;

	result->sub995ab_entered = true;
	result->mac_state = input->mac_state_at_99528;
	result->tail_callback_start = result->callback_count;
	memcpy(result->nested_456_after, input->nested_456,
	       OB_A7089_NESTED_CONFIG_LENGTH);

	/* sub_995ab @0x995b3 checks the BCM4352 chip id. Its four stack bytes
	 * are zero for arg0==0 or chanspec mask 0xc000, F8 for a nonzero arg
	 * and zero mask, and otherwise the child is skipped. */
	if (input->post_resetcca.chip_id != 0x4352)
		goto caller_join;
	if (input->pi_fa2 == 0 || (input->pi_17e & 0xc000u) == 0xc000u) {
		memset(source, 0, sizeof(source));
	} else if ((input->pi_17e & 0xc000u) == 0) {
		memset(source, 0xf8, sizeof(source));
	} else {
		goto caller_join;
	}
	result->sub99528_called = true;

	/* sub_99528 processes the four bytes in order. A nonzero byte at index 3
	 * exceeds the selected attach bound (2) and returns -2 after preserving
	 * prior mutations. Zero bytes bypass that bounds check in the binary. */
	for (i = 0; i < OB_A7089_NESTED_CONFIG_LENGTH; i++) {
		u8 value = source[i];

		if (value != 0 && i > input->post_resetcca.loop_bound) {
			result->sub99528_return = -2;
			result->sub99528_return_valid = true;
			goto caller_join;
		}
		if (input->nested_456[i] != value) {
			input->nested_456[i] = value;
			changed = true;
			result->nested_456_mutations++;
		}
	}
	memcpy(result->nested_456_after, input->nested_456,
	       OB_A7089_NESTED_CONFIG_LENGTH);

	/* The helper delegates MAC suspension only if bytes changed and the
	 * adjacent context byte is nonzero. The vendor sequence then calls
	 * sub_9949f @0x99593 before MAC enable; that child is not implemented, so
	 * stop at its exact entry without inventing a successful return or enable. */
	if (changed && input->phy_context_31 != 0) {
		u8 logical_event = (u8)(result->logical_event_count + 1u);
		int ret;

		if (!platform_ops || !platform_ops->suspend_mac_and_wait) {
			result->status = OB_A7089_TXPWR_UNSUPPORTED;
			result->unsupported_site = OB_A7089_MAC_SUSPEND_SITE;
			result->terminal_boundary = result->unsupported_site;
			return -EOPNOTSUPP;
		}
		result->mac_suspend_attempted = true;
		result->logical_event_count++;
		result->tail_logical_event_count++;
		result->callback_count++;
		result->tail_callback_count++;
		ret = platform_ops->suspend_mac_and_wait(ctx, logical_event,
						 OB_A7089_MAC_SUSPEND_SITE);
		if (ret) {
			result->status = OB_A7089_TXPWR_FAILED_BACKEND;
			result->failed_site = OB_A7089_MAC_SUSPEND_SITE;
			result->terminal_boundary = result->failed_site;
			/* A failed delegated suspend does not establish either MAC state. */
			result->mac_state = OB_A7089_MAC_UNKNOWN;
			return ret;
		}
		result->mac_state = OB_A7089_MAC_SUSPENDED;
		result->status = OB_A7089_TXPWR_UNSUPPORTED;
		result->unsupported_site = OB_A7089_SUB9949F_SITE;
		result->terminal_boundary = result->unsupported_site;
		return -EOPNOTSUPP;
	}

	/* Common epilogue @0x995a5 explicitly zeros EAX on normal child return. */
	result->sub99528_return = 0;
	result->sub99528_return_valid = true;

caller_join:
	memcpy(result->nested_456_after, input->nested_456,
	       OB_A7089_NESTED_CONFIG_LENGTH);
	{
		u32 logical_before = result->txpwrctrl_logical_event_count;
		u32 callbacks_before = result->txpwrctrl_callback_count;
		int ret;

		if (input->saved_pi_fa0 == 0)
			ret = ob_a7089_txpwrctrl_arg0(tx_ops, ctx, input, result);
		else
			ret = ob_a7089_txpwrctrl_nonzero(tx_ops, ctx, input, result);

		result->tail_logical_event_count +=
			result->txpwrctrl_logical_event_count - logical_before;
		result->tail_callback_count +=
			result->txpwrctrl_callback_count - callbacks_before;
		if (ret) {
			result->status = OB_A7089_TXPWR_FAILED_BACKEND;
			result->terminal_boundary = result->failed_site;
			return ret;
		}
	}
	result->tail_completed = true;
	result->status = OB_A7089_TXPWR_SYNTHETIC_COMPLETE;
	result->terminal_boundary = OB_A7089_AFTER_TXPWRCTRL_SITE;
	if (input->post_aa63e_inputs_present) {
		int ret = ob_a7089_post_aa63e_continue(prefix_ops, tx_ops,
						      platform_ops, ctx,
						      prefix, input, result);

		if (ret) {
			if (result->status != OB_A7089_TXPWR_UNSUPPORTED &&
			    result->status != OB_A7089_TXPWR_INVALID) {
				result->status = OB_A7089_TXPWR_FAILED_BACKEND;
				result->terminal_boundary = result->failed_site;
			}
			return ret;
		}
	}
	result->hardware_ready = false;
	return 0;
}

static u8 ob_a7089_tssivisible_threshold(u32 pi_164, u16 nested_342)
{
	if (pi_164 > 1)
		return pi_164 == 3 ? 0x1c : 0x80;
	if (nested_342 == 0x0203)
		return 0x26;
	/* The supported input profile is BCM4352, outside the BCM4360-only
	 * nested channel/revision branches in this software helper. */
	return 0x18;
}

static int ob_a7089_tssi_phy_write(
	const struct ob_phy_txpwr_index_ops *ops, void *ctx,
	const struct ob_phy_96203_input *prefix,
	struct ob_phy_a7089_txpwr_result *result)
{
	u16 reg;
	u16 value;
	int ret;

	/* The caller loads the relocated acphychipid global at @0xaa5d8. The
	 * runner requires it explicitly and admits only the BCM4352 value. */
	switch (prefix->acphychipid) {
	case 0x4352:
	case 0x4360:
	case 0xa9c4:
	case 0xaa06:
	case 0x4350:
		reg = 0x1641;
		break;
	default:
		reg = 0x0641;
		break;
	}
	value = (u16)(0x7f00u + (u16)result->tssi_threshold);
	result->tssi_write_reg = reg;
	result->tssi_write_value = value;
	result->logical_event_count++;
	ret = ops->phy_write16(ctx, OB_A7089_TSSI_PHY_WRITE_SITE, reg, value);
	result->callback_count++;
	if (ret) {
		result->failed_site = OB_A7089_TSSI_PHY_WRITE_SITE;
		return ret;
	}
	result->tssi_phy_write_complete = true;
	return 0;
}

int ob_phy_a7089_txpwr_test_only(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_platform_ops *platform_ops, void *ctx,
	const struct ob_phy_a7089_txpwr_input *input,
	struct ob_phy_a7089_txpwr_result *result)
{
	static const u8 selectors[OB_A7089_CALL_LOOP_BOUND] = { 1u, 2u };
	const struct ob_phy_96203_input *prefix;
	u8 i;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_A7089_TXPWR_INVALID;
	result->mac_state = OB_A7089_MAC_UNKNOWN;
	result->hardware_ready = false;
	ret = ob_a7089_validate(prefix_ops, tx_ops, input);
	if (ret)
		return ret;

	prefix = &input->post_resetcca;
	ret = ob_phy_96203_caller_test_only(prefix_ops, ctx, prefix,
					   &result->post_resetcca);
	result->logical_event_count +=
		result->post_resetcca.logical_event_count;
	result->callback_count += result->post_resetcca.callback_count;
	if (ret) {
		result->status = OB_A7089_TXPWR_FAILED_BACKEND;
		result->failed_site = result->post_resetcca.failed_site;
		result->terminal_boundary = result->failed_site;
		return ret;
	}

	/* @0xaa54f..0xaa57b: PI+0x164 gates this RMW only. */
	result->phy_040a_selected = prefix->pi_164 == 2 ||
		prefix->pi_164 == 3 || prefix->pi_164 == 5 ||
		prefix->pi_164 == 6;
	if (result->phy_040a_selected) {
		ret = ob_a7089_phy040a_rmw(prefix_ops, ctx, result);
		if (ret) {
			result->status = OB_A7089_TXPWR_FAILED_BACKEND;
			result->terminal_boundary = result->failed_site;
			return ret;
		}
	}

	/* @0xaa580..0xaa5bd: the caller creates a one-hot u8 selector and
	 * sign-extends each nested power-index byte independently. */
	for (i = 0; i < OB_A7089_CALL_LOOP_BOUND; i++) {
		struct ob_phy_txpwr_index_input helper = { 0 };
		struct ob_phy_txpwr_index_result *helper_result =
			&result->helper[i];

		result->core_selector[i] = selectors[i];
		result->power_index[i] =
			ob_a7089_sign_extend_u8(input->nested_power_indices[i]);
		helper.explicitly_synthetic = true;
		helper.chip_id = (u16)prefix->chip_id;
		helper.chip_revision = prefix->chip_revision;
		helper.phy_type = 0x0b;
		helper.radio_id = prefix->radio_id;
		helper.radio_revision = prefix->radio_revision;
		helper.radio_class = prefix->radio_class;
		helper.core_mask = selectors[i];
		helper.core_bound = prefix->loop_bound;
		helper.power_index = result->power_index[i];
		helper.nested_indices = input->nested_power_indices;
		helper.nested_indices_length = input->nested_power_indices_length;

		ret = ob_phy_txpwr_by_index_test_only(tx_ops, ctx, &helper,
						      helper_result);
		result->logical_event_count += helper_result->logical_event_count;
		result->callback_count += helper_result->callback_count;
		if (ret) {
			result->status = OB_A7089_TXPWR_FAILED_BACKEND;
			result->failed_site = helper_result->failed_site;
			result->terminal_boundary = result->failed_site;
			return ret;
		}
		result->helper_calls_completed++;
	}

	ret = ob_a7089_txpwrctrl_arg0(tx_ops, ctx, input, result);
	if (ret) {
		result->status = OB_A7089_TXPWR_FAILED_BACKEND;
		result->terminal_boundary = result->failed_site;
		return ret;
	}
	result->tssi_threshold = ob_a7089_tssivisible_threshold(
		prefix->pi_164, input->nested_342);
	result->tssi_threshold_valid = true;
	ret = ob_a7089_tssi_phy_write(tx_ops, ctx, prefix, result);
	if (ret) {
		result->status = OB_A7089_TXPWR_FAILED_BACKEND;
		result->terminal_boundary = result->failed_site;
		return ret;
	}
	if (input->sub995ab_tail_present) {
		ret = ob_a7089_sub995ab_tail(prefix_ops, tx_ops, platform_ops,
					     ctx, prefix, input, result);
		if (ret)
			return ret;
		return 0;
	}

	result->status = OB_A7089_TXPWR_SYNTHETIC_COMPLETE;
	result->terminal_boundary = OB_A7089_NEXT_UNSUPPORTED_SITE;
	result->hardware_ready = false;
	return 0;
}

/*
 * W2 export: bind the already reconstructed wlc_phy_rxcore_setstate_acphy to a
 * caller that owns only the window.  The vendor body above is unchanged; this
 * wrapper exists so the W2 stage does not duplicate its 43-operation ledger.
 */
int ob_phy_a7089_rxcore_setstate_export(
	const struct ob_phy_post_lpf_ops *prefix_ops,
	const struct ob_phy_txpwr_index_ops *tx_ops,
	const struct ob_phy_a7089_platform_ops *platform_ops, void *ctx,
	u8 phy_context_31, u8 pi_a6, u8 state,
	struct ob_phy_a7089_rxcore_export *out)
{
	struct ob_phy_a7089_txpwr_input input = { 0 };
	struct ob_phy_a7089_txpwr_result result = { 0 };
	int ret;

	if (!prefix_ops || !tx_ops || !out)
		return -EINVAL;
	input.phy_context_31_present = true;
	input.phy_context_31 = phy_context_31;
	input.post_aa63e_pi_a6_present = true;
	input.post_aa63e_pi_a6 = pi_a6;
	ret = ob_a7089_rxcore_setstate(prefix_ops, tx_ops, platform_ops, ctx,
				       &input, &result, state);
	out->called = result.post_aa63e_rxcore_setstate_called;
	out->software_only = result.post_aa63e_rxcore_software_only;
	out->mac_sequence = result.post_aa63e_rxcore_mac_sequence;
	out->mac_restored = result.post_aa63e_rxcore_mac_restored;
	out->reentry_forbidden = result.post_aa63e_rxcore_reentry_forbidden;
	out->callback_count = result.callback_count;
	out->logical_event_count = result.logical_event_count;
	out->saved_0401 = result.post_aa63e_rxcore_saved_0401;
	out->saved_0400 = result.post_aa63e_rxcore_saved_0400;
	out->poll_reads[0] = result.post_aa63e_rfseq_poll_reads[0];
	out->poll_reads[1] = result.post_aa63e_rfseq_poll_reads[1];
	out->poll_delays[0] = result.post_aa63e_rfseq_poll_delays[0];
	out->poll_delays[1] = result.post_aa63e_rfseq_poll_delays[1];
	out->poll_exhausted[0] = result.post_aa63e_rfseq_poll_exhausted[0];
	out->poll_exhausted[1] = result.post_aa63e_rfseq_poll_exhausted[1];
	return ret;
}

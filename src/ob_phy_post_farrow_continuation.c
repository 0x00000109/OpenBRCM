/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <string.h>

#include "ob_phy_post_farrow_continuation.h"

#define OB_POST_FARROW_EVENT(n) ((u8)(n))

static int ob_post_farrow_validate_input(
	const struct ob_phy_post_farrow_software_input *in)
{
	if (!in || !in->explicitly_synthetic || !in->entry_after_farrow_join ||
	    in->chip_id != 0x4352 || in->chip_revision != 3 ||
	    in->phy_revision != 0x0a || in->radio_id != 0x2069 ||
	    in->radio_revision != 4 || !in->pi_cache_240_present ||
	    !in->nested_341_present || in->nested_341 != 1 ||
	    !in->nested_912_present || in->nested_912 != 0 ||
	    !in->nested_8b0_present || !in->nested_914_present ||
	    !in->nested_916_present)
		return -EINVAL;

	/* A selected cache refresh consumes the helper's returned record. The
	 * synthetic scenario must carry that object explicitly; it cannot invent a
	 * pointer or silently use a zero-filled record. */
	if ((in->phy_flags_19c & 0x0206u) == 0 ||
	    (u8)in->initial_chanspec == in->pi_cache_240) {
		if (!in->cache_record_result_supplied ||
		    (in->cache_record_result_present &&
		     !in->cache_record_payload_present))
			return -EINVAL;
	}
	return 0;
}

int ob_phy_post_farrow_continue_software_test_only(
	const struct ob_phy_post_farrow_software_input *input,
	struct ob_phy_post_farrow_state *state,
	struct ob_phy_post_farrow_software_result *result)
{
	struct ob_phy_post_farrow_state next = { 0 };
	struct ob_phy_post_farrow_software_result done = { 0 };
	const u8 *source;
	bool cache_refresh;
	bool inner_copy;
	u8 i;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->kind = OB_PHY_POST_FARROW_SW_INVALID;
	if (!state)
		return -EINVAL;
	ret = ob_post_farrow_validate_input(input);
	if (ret)
		return ret;

	cache_refresh = (input->phy_flags_19c & 0x0206u) == 0 ||
		(u8)input->initial_chanspec == input->pi_cache_240;
	next.explicitly_synthetic = true;
	next.chip_id = input->chip_id;
	next.chip_revision = input->chip_revision;
	next.phy_revision = input->phy_revision;
	next.radio_id = input->radio_id;
	next.radio_revision = input->radio_revision;
	next.phy_flags_19c = input->phy_flags_19c;
	next.initial_chanspec = input->initial_chanspec;
	next.pi_cache_240 = input->pi_cache_240;
	next.nested_341 = input->nested_341;
	next.nested_912 = input->nested_912;
	next.nested_8b0 = input->nested_8b0;
	if (cache_refresh) {
		next.pi_cache_240 = (u8)input->initial_chanspec;
		next.nested_8a8_has_record = input->cache_record_result_present;
		source = input->cache_record_result_present ?
			input->cache_record_payload : input->nested_65f;
		done.cache_helper_called = true;
	} else {
		/* Caller stored NULL in nested+0x8a8 before the cache predicate. */
		next.nested_8a8_has_record = false;
		source = input->nested_65f;
	}

	if (input->nested_8b0 == 0) {
		done.configuration_arm = OB_PHY_POST_FARROW_CONFIG_ZERO;
		for (i = 0; i < OB_PHY_POST_FARROW_BYTES; i++)
			next.nested_668[i] = source[i];
		done.copy_selected = true;
	} else {
		done.configuration_arm = OB_PHY_POST_FARROW_CONFIG_NONZERO;
		inner_copy = ((input->phy_flags_19c & 0x0206u) != 0 &&
			      (u8)input->initial_chanspec != next.pi_cache_240) ||
			     (input->initial_chanspec & 0xc000u) == 0xc000u;
		if (inner_copy) {
			for (i = 0; i < OB_PHY_POST_FARROW_BYTES; i++)
				next.nested_668[i] = source[i];
			done.copy_selected = true;
		} else {
			for (i = 0; i < 8; i++)
				next.nested_668[i] = input->nested_8b4[i] > source[i] ?
					input->nested_8b4[i] : source[i];
			next.nested_668[8] = input->nested_8b4[8] | source[8];
		}
	}

	/* Common sub_909fd tail: either signed-band condition may force +0x66e. */
	if (((input->initial_chanspec & 0xc000u) == 0 &&
	     input->nested_914 >= 0) ||
	    ((input->initial_chanspec & 0xc000u) == 0xc000u &&
	     input->nested_916 >= 0))
		next.nested_668[6] = 1;

	next.valid = true;
	next.post_909fd_complete = true;
	done.kind = OB_PHY_POST_FARROW_SW_COMPLETE;
	done.caller_joined = true;
	done.sub_909fd_called = true;
	done.common_rejoin_site = 0x90b2e;
	done.helper_return_site = 0x90b76;
	done.hardware_callbacks = 0;
	done.hardware_ready = false;
	*state = next;
	*result = done;
	return 0;
}

static int ob_post_farrow_read(const struct ob_phy_post_farrow_hw_ops *ops,
			       void *ctx, u8 event, u32 site, u16 reg,
			       u16 *value,
			       struct ob_phy_desense_prefix_result *result)
{
	int ret = ops->read16(ctx, event, site, reg, value);

	result->callback_records++;
	return ret;
}

static int ob_post_farrow_write(const struct ob_phy_post_farrow_hw_ops *ops,
				void *ctx, u8 event, u32 site, u16 reg,
				u16 value,
				struct ob_phy_desense_prefix_result *result)
{
	int ret = ops->write16(ctx, event, site, reg, value);

	result->callback_records++;
	return ret;
}

static int ob_post_farrow_rmw(const struct ob_phy_post_farrow_hw_ops *ops,
			      void *ctx, u8 event, u32 site, u16 reg,
			      u16 mask, u16 insert,
			      struct ob_phy_desense_prefix_result *result)
{
	u16 old;
	u16 next;
	int ret;

	ret = ob_post_farrow_read(ops, ctx, event, site, reg, &old, result);
	if (ret)
		return ret;
	/* Historical helper is phy_reg_mod: MASKED_MERGE, (mask, value). */
	next = (u16)((old & (u16)~mask) | (insert & mask));
	return ob_post_farrow_write(ops, ctx, event, site, reg, next, result);
}

int ob_phy_desense_9af05_prefix_test_only(
	const struct ob_phy_post_farrow_hw_ops *ops, void *ctx,
	const struct ob_phy_post_farrow_state *state,
	struct ob_phy_desense_prefix_result *result)
{
	struct ob_phy_desense_prefix_result done = { 0 };
	u16 value;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->kind = OB_PHY_DESENSE_PREFIX_INVALID;
	if (!state || !state->valid || !state->explicitly_synthetic ||
	    !state->post_909fd_complete || state->chip_id != 0x4352 ||
	    state->chip_revision != 3 || state->phy_revision != 0x0a ||
	    state->radio_id != 0x2069 || state->radio_revision != 4 ||
	    state->nested_912 != 0 || state->nested_341 != 1 || !ops ||
	    !ops->read16 || !ops->write16)
		return -EINVAL;

	/* Independent C fixture executes exactly the initial sub_9af05 prologue.
	 * Return before its selected sub_99658 call at 0x9b123. */
	ret = ob_post_farrow_read(ops, ctx, OB_POST_FARROW_EVENT(1),
				  0x9b0cd, 0x019e, &value, &done);
	if (ret)
		goto failed;
	done.saved_019e = value;
	done.logical_events++;

	ret = ob_post_farrow_rmw(ops, ctx, OB_POST_FARROW_EVENT(2),
				 0x9b0eb, 0x019e, 0x0002, 0x0002, &done);
	if (ret)
		goto failed;
	done.logical_events++;

	ret = ob_post_farrow_read(ops, ctx, OB_POST_FARROW_EVENT(3),
				  0x9b0f8, 0x016c, &value, &done);
	if (ret)
		goto failed;
	done.saved_016c = value;
	done.logical_events++;

	ret = ob_post_farrow_rmw(ops, ctx, OB_POST_FARROW_EVENT(4),
				 0x9b116, 0x016c, 0x0040, 0x0040, &done);
	if (ret)
		goto failed;
	done.logical_events++;

	done.kind = OB_PHY_DESENSE_PREFIX_COMPLETE;
	done.terminal_boundary = 0x9b123;
	done.desense_helper_complete = false;
	done.hardware_ready = false;
	*result = done;
	return 0;

failed:
	done.kind = OB_PHY_DESENSE_PREFIX_FAILED;
	done.terminal_boundary = 0x9b123;
	done.desense_helper_complete = false;
	done.hardware_ready = false;
	*result = done;
	return ret;
}

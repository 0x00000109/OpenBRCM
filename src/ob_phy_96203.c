/* SPDX-License-Identifier: GPL-2.0-only */
#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/kernel.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "ob_phy_96203.h"

struct ob_96203_radio_map {
	u16 gate;
	u16 outer_control;
	u16 outer_data;
	u16 poll;
	u16 inner_control;
	u16 pair_read;
	u16 pair_write;
};

struct ob_96203_exec {
	const struct ob_phy_post_lpf_ops *ops;
	void *ctx;
	struct ob_phy_96203_result *r;
	u8 event;
	const struct ob_phy_96203_input *in;
	struct ob_96203_radio_map map;
};

static void ob_96203_begin(struct ob_96203_exec *x)
{
	x->r->logical_event_count++;
	x->event++;
}

static int ob_96203_validate(const struct ob_phy_post_lpf_ops *ops,
			     const struct ob_phy_96203_input *in)
{
	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us || !in ||
	    !in->explicitly_synthetic || in->chip_id != 0x4352 ||
	    in->chip_revision != 3 || in->phy_revision != 0x0a ||
	    in->radio_id != 0x2069 || in->radio_revision != 4 ||
	    in->radio_class != 0 || !in->acphychipid_present ||
	    !in->loop_bound_present || in->loop_bound > 2 ||
	    !in->second_loop_gate_present)
		return -EINVAL;
	return 0;
}

static int ob_96203_read(struct ob_96203_exec *x,
			 enum ob_phy_post_lpf_domain domain, u32 site,
			 u16 reg, u16 *value)
{
	int ret;

	ret = x->ops->read16(x->ctx, x->event, site, domain, reg, value);
	x->r->callback_count++;
	if (ret && !x->r->failed_site)
		x->r->failed_site = site;
	return ret;
}

static int ob_96203_write(struct ob_96203_exec *x,
			  enum ob_phy_post_lpf_domain domain, u32 site,
			  u16 reg, u16 value)
{
	int ret;

	ret = x->ops->write16(x->ctx, x->event, site, domain, reg, value);
	x->r->callback_count++;
	if (ret && !x->r->failed_site)
		x->r->failed_site = site;
	return ret;
}

static int ob_96203_delay(struct ob_96203_exec *x, u32 site, u16 usec)
{
	int ret;

	ob_96203_begin(x);
	ret = x->ops->delay_us(x->ctx, x->event, site, usec);
	x->r->callback_count++;
	if (ret && !x->r->failed_site)
		x->r->failed_site = site;
	return ret;
}

static int ob_96203_fgc(struct ob_96203_exec *x, u32 site, bool enabled)
{
	int ret;

	if (!x->ops->phyclk_fgc)
		return -EINVAL;
	ob_96203_begin(x);
	ret = x->ops->phyclk_fgc(x->ctx, x->event, site, enabled);
	x->r->callback_count++;
	if (!ret)
		x->r->clock_fgc_enabled = enabled;
	else if (!x->r->failed_site)
		x->r->failed_site = site;
	return ret;
}

static int ob_96203_rmw(struct ob_96203_exec *x,
			enum ob_phy_post_lpf_domain domain, u32 site,
			u16 reg, u16 mask, u16 value)
{
	u16 old;
	int ret;

	ob_96203_begin(x);
	ret = ob_96203_read(x, domain, site, reg, &old);
	if (ret)
		return ret;
	return ob_96203_write(x, domain, site, reg,
			      (u16)((old & (u16)~mask) | (value & mask)));
}

static int ob_96203_radio_write(struct ob_96203_exec *x, u32 site,
				u16 reg, u16 value)
{
	ob_96203_begin(x);
	return ob_96203_write(x, OB_PHY_POST_LPF_RADIO, site, reg, value);
}

static int ob_96203_radio_read(struct ob_96203_exec *x, u32 site,
			       u16 reg, u16 *value)
{
	ob_96203_begin(x);
	return ob_96203_read(x, OB_PHY_POST_LPF_RADIO, site, reg, value);
}

static int ob_96203_phy_read(struct ob_96203_exec *x, u32 site,
			     u16 reg, u16 *value)
{
	ob_96203_begin(x);
	return ob_96203_read(x, OB_PHY_POST_LPF_PHY, site, reg, value);
}

static int ob_96203_phy_write(struct ob_96203_exec *x, u32 site,
			      u16 reg, u16 value)
{
	ob_96203_begin(x);
	return ob_96203_write(x, OB_PHY_POST_LPF_PHY, site, reg, value);
}

static void ob_96203_set_map(struct ob_96203_exec *x, u32 selector)
{
	if (selector == 0x4352 || selector == 0x4360 ||
	    selector == 0xa9c4 || selector == 0xaa06) {
		x->map = (struct ob_96203_radio_map) {
			.gate = 0x08ea, .outer_control = 0x0121,
			.outer_data = 0x0122, .poll = 0x0144,
			.inner_control = 0x015f, .pair_read = 0x0143,
			.pair_write = 0x0135,
		};
	} else {
		x->map = (struct ob_96203_radio_map) {
			.gate = 0x08f2, .outer_control = 0x0133,
			.outer_data = 0x0134, .poll = 0x0156,
			.inner_control = 0x0172, .pair_read = 0x0155,
			.pair_write = 0x0147,
		};
	}
}

static u16 ob_96203_core_phy_reg(u8 core, u16 reg0, u16 reg1, u16 regn)
{
	return core == 0 ? reg0 : core == 1 ? reg1 : regn;
}

static int ob_96203_helper(struct ob_96203_exec *x)
{
	static const u32 pair_read_sites[14] = {
		0x9668a, 0x96709, 0x96788, 0x96807, 0x96886, 0x96905,
		0x96984, 0x96a03, 0x96a82, 0x96b01, 0x96b80, 0x96bff,
		0x96c7e, 0x96cfd,
	};
	static const u32 pair_write_sites[14] = {
		0x966cf, 0x9674e, 0x967cd, 0x9684c, 0x968cb, 0x9694a,
		0x969c9, 0x96a48, 0x96ac7, 0x96b46, 0x96bc5, 0x96c44,
		0x96cc3, 0x96d45,
	};
	const u8 bound = x->in->loop_bound;
	u8 i, j, attempt, pair;
	u16 saved_739[2], saved_73a[2], saved_725[2];
	int ret;

	/* @0x96250: chip-family-selected gate RMW. */
	ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x96250,
			   x->map.gate, 0x0080, 0x0080);
	if (ret)
		return ret;

	for (i = 0; i < bound; i++) {
		u16 stride = (u16)(i * 0x0200u);
		u16 data_reg = (u16)(x->map.outer_data | stride);
		u16 poll_reg = (u16)(x->map.poll | stride);
		u16 ctrl_reg = (u16)(x->map.outer_control | stride);
		u16 sample, first, second;
		bool poll_done = false;

		ret = ob_96203_phy_read(x, 0x96275,
					 (u16)(0x0739 + stride), &saved_739[i]);
		if (ret) return ret;
		ret = ob_96203_phy_read(x, 0x9629a,
					 (u16)(0x073a + stride), &saved_73a[i]);
		if (ret) return ret;
		ret = ob_96203_phy_read(x, 0x962a9,
					 (u16)(0x0725 + stride), &saved_725[i]);
		if (ret) return ret;

		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_PHY, 0x962d9,
				   ob_96203_core_phy_reg(i, 0x0739, 0x0939, 0x0b39),
				   0x0080, 0x0080);
		if (ret) return ret;
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_PHY, 0x96305,
				   ob_96203_core_phy_reg(i, 0x0725, 0x0925, 0x0b25),
				   0x0004, 0x0004);
		if (ret) return ret;
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x9634d,
				   ctrl_reg, 0x1000, 0);
		if (ret) return ret;
		ret = ob_96203_delay(x, 0x96357, 100);
		if (ret) return ret;
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x9639b,
				   ctrl_reg, 0x1000, 0x1000);
		if (ret) return ret;
		ret = ob_96203_radio_read(x, 0x963d5, data_reg, &sample);
		if (ret) return ret;
		ret = ob_96203_radio_write(x, 0x9641c, data_reg,
					   (u16)(sample | 0x000f));
		if (ret) return ret;

		for (attempt = 0; attempt < 10; attempt++) {
			ret = ob_96203_delay(x, 0x9642a, 10);
			if (ret) return ret;
			ret = ob_96203_radio_read(x, 0x96464, poll_reg, &first);
			if (ret) return ret;
			ret = ob_96203_radio_read(x, 0x964a2, poll_reg, &second);
			if (ret) return ret;
			if ((second & 1u) && (first & 2u)) {
				x->r->poll_success_mask |= (u8)(1u << i);
				x->r->poll_success_attempt[i] = (u8)(attempt + 1u);
				poll_done = true;
				break;
			}
		}
		if (!poll_done)
			x->r->poll_exhausted_mask |= (u8)(1u << i);

		/* Historical site 0x964ff is RADIO write; PHY restores follow. */
		ret = ob_96203_radio_write(x, 0x964ff, data_reg,
					   (u16)(sample & 0xfff0));
		if (ret) return ret;
		ret = ob_96203_phy_write(x, 0x9650e,
					 (u16)(0x0725 + stride), saved_725[i]);
		if (ret) return ret;
		ret = ob_96203_phy_write(x, 0x9651d,
					 (u16)(0x0739 + stride), saved_739[i]);
		if (ret) return ret;
		ret = ob_96203_phy_write(x, 0x9652c,
					 (u16)(0x073a + stride), saved_73a[i]);
		if (ret) return ret;
		x->r->outer_cores_completed++;
	}

	ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x96574,
			   x->map.gate, 0x0080, 0);
	if (ret) return ret;
	if (x->in->second_loop_gate > 3) {
		x->r->terminal_boundary = 0x96da0;
		return 0;
	}

	ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x965bf,
			   x->map.gate, 0x0080, 0x0080);
	if (ret) return ret;
	for (j = 0; j < bound; j++) {
		u16 stride = (u16)(j * 0x0200u);
		u16 ctrl_reg = (u16)(x->map.inner_control | stride);
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x9660c,
				   ctrl_reg, 0x0020, 0x0020);
		if (ret) return ret;
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x96650,
				   ctrl_reg, 0x0010, 0x0010);
		if (ret) return ret;
		for (pair = 0; pair < 14; pair++) {
			u16 read_reg = (u16)(x->map.pair_read - pair + stride);
			u16 write_reg = (u16)(x->map.pair_write - pair + stride);
			u16 value;
			ret = ob_96203_radio_read(x, pair_read_sites[pair],
						  read_reg, &value);
			if (ret) return ret;
			ret = ob_96203_radio_write(x, pair_write_sites[pair],
						   write_reg, (u16)~value);
			if (ret) return ret;
		}
		x->r->inner_cores_completed++;
	}
	ret = ob_96203_rmw(x, OB_PHY_POST_LPF_RADIO, 0x96d8d,
			   x->map.gate, 0x0080, 0);
	if (ret) return ret;
	x->r->terminal_boundary = 0x96da0;
	return 0;
}

int ob_phy_96203_test_only(const struct ob_phy_post_lpf_ops *ops, void *ctx,
			   const struct ob_phy_96203_input *input,
			   struct ob_phy_96203_result *result)
{
	struct ob_96203_exec x;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_96203_INVALID;
	result->hardware_ready = false;
	result->phy_ready = false;
	result->channel_ready = false;
	ret = ob_96203_validate(ops, input);
	if (ret)
		return ret;
	memset(&x, 0, sizeof(x));
	x.ops = ops;
	x.ctx = ctx;
	x.r = result;
	x.in = input;
	ob_96203_set_map(&x, input->acphychipid);
	ret = ob_96203_helper(&x);
	if (ret) {
		result->status = OB_96203_FAILED_BACKEND_OPERATION;
		return ret;
	}
	result->status = OB_96203_SYNTHETIC_COMPLETE;
	return 0;
}

static int ob_96203_resetcca(struct ob_96203_exec *x)
{
	u16 saved;
	int ret;
	bool special = x->in->pi_164 == 2 || x->in->pi_164 == 5 ||
		x->in->pi_164 == 6;

	if (special) {
		ret = ob_96203_fgc(x, 0x9742b, true); if (ret) return ret;
		ret = ob_96203_phy_read(x, 0x97438, 1, &saved); if (ret) return ret;
		ret = ob_96203_phy_write(x, 0x97450, 1, (u16)(saved | 0x4000)); if (ret) return ret;
		ret = ob_96203_delay(x, 0x97455, 1); if (ret) return ret;
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_PHY, 0x9746e, 0x019e, 0x003c, 0); if (ret) return ret;
		ret = ob_96203_delay(x, 0x97478, 1); if (ret) return ret;
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_PHY, 0x9748f, 0x019e, 1, 1); if (ret) return ret;
		ret = ob_96203_delay(x, 0x97499, 1); if (ret) return ret;
		ret = ob_96203_fgc(x, 0x974a8, false); if (ret) return ret;
		ret = ob_96203_delay(x, 0x974b2, 1); if (ret) return ret;
		ret = ob_96203_phy_write(x, 0x974c2, 1, (u16)(saved & 0xbfff)); if (ret) return ret;
		ret = ob_96203_delay(x, 0x974d2, 1); if (ret) return ret;
		ret = ob_96203_rmw(x, OB_PHY_POST_LPF_PHY, 0x974e6, 0x019e, 1, 0); if (ret) return ret;
	} else {
		ret = ob_96203_fgc(x, 0x974fa, true); if (ret) return ret;
		ret = ob_96203_phy_read(x, 0x97507, 1, &saved); if (ret) return ret;
		ret = ob_96203_phy_write(x, 0x9751f, 1, (u16)(saved | 0x4000)); if (ret) return ret;
		ret = ob_96203_delay(x, 0x97529, 1); if (ret) return ret;
		ret = ob_96203_phy_write(x, 0x97539, 1, (u16)(saved & 0xbfff)); if (ret) return ret;
		ret = ob_96203_fgc(x, 0x9754e, false); if (ret) return ret;
	}
	return ob_96203_delay(x, 0x97555, 2);
}

int ob_phy_96203_resetcca_test_only(const struct ob_phy_post_lpf_ops *ops,
				    void *ctx,
				    const struct ob_phy_96203_input *input,
				    u32 prior_logical_events,
				    struct ob_phy_96203_result *result)
{
	struct ob_96203_exec x;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_96203_INVALID;
	if (!input || !input->pi_164_present)
		return -EINVAL;
	ret = ob_96203_validate(ops, input);
	if (ret)
		return ret;
	memset(&x, 0, sizeof(x));
	x.ops = ops;
	x.ctx = ctx;
	x.r = result;
	x.in = input;
	x.event = (u8)prior_logical_events;
	ret = ob_96203_resetcca(&x);
	if (ret) {
		result->status = OB_96203_FAILED_BACKEND_OPERATION;
		return ret;
	}
	result->status = OB_96203_SYNTHETIC_COMPLETE;
	result->terminal_boundary = 0x0aa6b2;
	return 0;
}

int ob_phy_96203_caller_test_only(const struct ob_phy_post_lpf_ops *ops,
				  void *ctx,
				  const struct ob_phy_96203_input *input,
				  struct ob_phy_96203_result *result)
{
	struct ob_96203_exec x;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_96203_INVALID;
	if (!input || !input->caller_gate_present || !input->pi_164_present ||
	    !ops || !ops->read16 || !ops->write16 || !ops->delay_us ||
	    !ops->phyclk_fgc)
		return -EINVAL;
	ret = ob_96203_validate(ops, input);
	if (ret)
		return ret;
	result->caller_selected = input->reset_marker != 0 || input->nested_32c != 0;
	if (!result->caller_selected) {
		result->status = OB_96203_SKIPPED_BY_CALLER;
		result->terminal_boundary = 0xaa54f;
		return 0;
	}
	memset(&x, 0, sizeof(x));
	x.ops = ops;
	x.ctx = ctx;
	x.r = result;
	x.in = input;
	ret = ob_96203_resetcca(&x);
	if (ret) goto failed;
	ret = ob_96203_delay(&x, 0xaa542, 1);
	if (ret) goto failed;
	ob_96203_set_map(&x, input->acphychipid);
	ret = ob_96203_helper(&x);
	if (ret) goto failed;
	result->terminal_boundary = 0xaa54f;
	result->status = OB_96203_SYNTHETIC_COMPLETE;
	return 0;
failed:
	result->status = OB_96203_FAILED_BACKEND_OPERATION;
	return ret;
}

static int ob_96203_validate_live(const struct ob_phy_post_lpf_ops *ops,
				  const struct ob_phy_96203_live_input *in)
{
	if (!ops || !ops->read16 || !ops->write16 || !ops->delay_us || !in)
		return -EINVAL;
	if (!in->identity_valid || !in->acphychipid_owned)
		return -EINVAL;
	if (in->loop_bound < 1 || in->loop_bound > 2)
		return -EINVAL;
	/* PI+0x16c has no owned source here: refuse rather than invent it. */
	if (!in->second_loop_gate_owned)
		return -EINVAL;
	return 0;
}

/*
 * Live path. The helper reads exactly three input fields; the rest of the
 * synthetic struct is unused, and explicitly_synthetic stays false so this
 * path can never be mistaken for a fixture entry.
 */
int ob_phy_96203_live_run(const struct ob_phy_post_lpf_ops *ops, void *ctx,
			  const struct ob_phy_96203_live_input *input,
			  struct ob_phy_96203_result *result)
{
	struct ob_96203_exec x;
	struct ob_phy_96203_input in;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->status = OB_96203_INVALID;
	ret = ob_96203_validate_live(ops, input);
	if (ret)
		return ret;
	memset(&in, 0, sizeof(in));
	in.explicitly_synthetic = false;
	in.acphychipid_present = true;
	in.acphychipid = input->acphychipid;
	in.loop_bound_present = true;
	in.loop_bound = input->loop_bound;
	in.second_loop_gate_present = true;
	in.second_loop_gate = input->second_loop_gate;
	memset(&x, 0, sizeof(x));
	x.ops = ops;
	x.ctx = ctx;
	x.r = result;
	x.in = &in;
	ob_96203_set_map(&x, in.acphychipid);
	ret = ob_96203_helper(&x);
	if (ret) {
		result->status = OB_96203_FAILED_BACKEND_OPERATION;
		return ret;
	}
	result->status = OB_96203_LIVE_COMPLETE;
	return 0;
}

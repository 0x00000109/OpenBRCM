/* SPDX-License-Identifier: GPL-2.0-only */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/ob_phy_a7089_txpwr.h"

#define MAX_CALLBACKS 85000u
#define REG_COUNT 0x2000u

enum fake_kind {
	FAKE_PHY_READ, FAKE_PHY_WRITE, FAKE_RADIO_READ, FAKE_RADIO_WRITE,
	FAKE_DELAY, FAKE_FGC, FAKE_TABLE_READ, FAKE_TABLE_WRITE,
	FAKE_MAC_SUSPEND, FAKE_MAC_ENABLE,
};

struct fake_event {
	enum fake_kind kind;
	u8 logical_event;
	u32 site;
	u16 reg;
	u8 table;
	s16 offset;
	u8 count;
	u8 width;
	u8 payload_length;
	u16 value;
	u8 payload[6];
};

struct fake_backend {
	struct fake_event events[MAX_CALLBACKS];
	u32 event_count;
	u32 fail_at;
	u16 phy[REG_COUNT];
	u16 radio[REG_COUNT];
	u16 table[16][512];
	u8 nested_txpwrctrl_values[2];
	u8 nested_456[4];
	u8 nested_gainerr_state_211[8];
	u16 nested_carriersearch_count;
	u16 radio_090b_script[100];
	u8 radio_090b_script_length;
	u8 radio_090b_script_index;
	u8 table_sample_neg128[6];
	u8 table_sample_pos127[6];
	u8 table_sample_neg1[6];
	bool fgc_enabled;
};

static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static struct fake_event *new_event(struct fake_backend *fake,
				    enum fake_kind kind, u32 site)
{
	struct fake_event *event;

	if (!fake || fake->event_count >= MAX_CALLBACKS)
		return NULL;
	event = &fake->events[fake->event_count++];
	memset(event, 0, sizeof(*event));
	event->kind = kind;
	event->site = site;
	return event;
}

static bool fail_now(const struct fake_backend *fake)
{
	return fake->fail_at && fake->fail_at == fake->event_count;
}

static int find_event_site(const struct fake_backend *fake, u32 site,
			  unsigned int occurrence)
{
	u32 i;

	for (i = 0; i < fake->event_count; i++) {
		if (fake->events[i].site == site) {
			if (!occurrence)
				return (int)i;
			occurrence--;
		}
	}
	return -1;
}

static int fake_read16(void *ctx, u8 logical, u32 site,
		      enum ob_phy_post_lpf_domain domain, u16 reg, u16 *value)
{
	struct fake_backend *fake = ctx;
	enum fake_kind kind;
	struct fake_event *event;

	if (domain != OB_PHY_POST_LPF_PHY && domain != OB_PHY_POST_LPF_RADIO)
		return -EINVAL;
	kind = domain == OB_PHY_POST_LPF_PHY ? FAKE_PHY_READ : FAKE_RADIO_READ;
	event = new_event(fake, kind, site);
	if (!event || !value || reg >= REG_COUNT)
		return -EINVAL;
	event->logical_event = logical;
	event->reg = reg;
	event->width = 16;
	if (fail_now(fake))
		return -EIO;
	if (domain == OB_PHY_POST_LPF_RADIO && reg == 0x090b &&
	    fake->radio_090b_script_index < fake->radio_090b_script_length) {
		*value = fake->radio_090b_script[fake->radio_090b_script_index++];
	} else {
		*value = domain == OB_PHY_POST_LPF_PHY ?
			fake->phy[reg] : fake->radio[reg];
	}
	event->value = *value;
	return 0;
}

static int fake_write16(void *ctx, u8 logical, u32 site,
		       enum ob_phy_post_lpf_domain domain, u16 reg, u16 value)
{
	struct fake_backend *fake = ctx;
	enum fake_kind kind;
	struct fake_event *event;

	if (domain != OB_PHY_POST_LPF_PHY && domain != OB_PHY_POST_LPF_RADIO)
		return -EINVAL;
	kind = domain == OB_PHY_POST_LPF_PHY ? FAKE_PHY_WRITE : FAKE_RADIO_WRITE;
	event = new_event(fake, kind, site);
	if (!event || reg >= REG_COUNT)
		return -EINVAL;
	event->logical_event = logical;
	event->reg = reg;
	event->width = 16;
	event->value = value;
	if (fail_now(fake))
		return -EIO;
	if (domain == OB_PHY_POST_LPF_PHY)
		fake->phy[reg] = value;
	else
		fake->radio[reg] = value;
	return 0;
}

static int fake_delay(void *ctx, u8 logical, u32 site, u16 usec)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, FAKE_DELAY, site);
	(void)logical;
	if (!event)
		return -EINVAL;
	event->value = usec;
	return fail_now(fake) ? -EIO : 0;
}

static int fake_fgc(void *ctx, u8 logical, u32 site, bool enabled)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, FAKE_FGC, site);
	(void)logical;
	if (!event)
		return -EINVAL;
	event->value = enabled;
	if (fail_now(fake))
		return -EIO;
	fake->fgc_enabled = enabled;
	return 0;
}

static int fake_mac_suspend(void *ctx, u8 logical, u32 site)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, FAKE_MAC_SUSPEND, site);

	if (!event)
		return -EINVAL;
	event->logical_event = logical;
	return fail_now(fake) ? -EIO : 0;
}


static int fake_tx_phy_read(void *ctx, u32 site, u16 reg, u16 *value)
{
	return fake_read16(ctx, 0, site, OB_PHY_POST_LPF_PHY, reg, value);
}

static int fake_tx_phy_write(void *ctx, u32 site, u16 reg, u16 value)
{
	return fake_write16(ctx, 0, site, OB_PHY_POST_LPF_PHY, reg, value);
}

static int fake_table_read(void *ctx, u32 site, u8 table, s16 offset,
			   u8 count, u8 width, u8 *payload, u8 length)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, FAKE_TABLE_READ, site);
	const u8 *sample;
	if (!event || !payload || length > sizeof(event->payload))
		return -EINVAL;
	event->table = table;
	event->offset = offset;
	event->count = count;
	event->width = width;
	event->payload_length = length;
	if (fail_now(fake))
		return -EIO;
	if (table != 0x20 || count != 1 || width != 48 || length != 6)
		return -EINVAL;
	if (offset == -128)
		sample = fake->table_sample_neg128;
	else if (offset == 127)
		sample = fake->table_sample_pos127;
	else if (offset == -1)
		sample = fake->table_sample_neg1;
	else
		return -EINVAL;
	memcpy(payload, sample, length);
	memcpy(event->payload, sample, length);
	return 0;
}

static int fake_table_write(void *ctx, u32 site, u8 table, u16 offset,
			    u8 count, u8 width, const u8 *payload,
			    u8 length)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, FAKE_TABLE_WRITE, site);
	if (!event || !payload || length > sizeof(event->payload))
		return -EINVAL;
	event->table = table;
	event->offset = (s16)offset;
	event->count = count;
	event->width = width;
	event->payload_length = length;
	memcpy(event->payload, payload, length);
	if (fail_now(fake))
		return -EIO;
	if (count != 1 || width != 16 || length != 2 || table >= 16 ||
	    offset >= 512)
		return -EINVAL;
	fake->table[table][offset] =
		(u16)((u16)payload[0] | ((u16)payload[1] << 8));
	return 0;
}

static const struct ob_phy_post_lpf_ops prefix_ops = {
	.read16 = fake_read16,
	.write16 = fake_write16,
	.delay_us = fake_delay,
	.phyclk_fgc = fake_fgc,
};

static const struct ob_phy_txpwr_index_ops tx_ops = {
	.phy_read16 = fake_tx_phy_read,
	.phy_write16 = fake_tx_phy_write,
	.table_read = fake_table_read,
	.table_write = fake_table_write,
};

static int fake_mac_enable(void *ctx, u8 logical, u32 site)
{
	struct fake_backend *fake = ctx;
	struct fake_event *event = new_event(fake, FAKE_MAC_ENABLE, site);

	if (!event)
		return -EINVAL;
	event->logical_event = logical;
	return fail_now(fake) ? -EIO : 0;
}

static const struct ob_phy_a7089_platform_ops platform_ops = {
	.suspend_mac_and_wait = fake_mac_suspend,
	.enable_mac = fake_mac_enable,
};

static void seed(struct fake_backend *fake)
{
	memset(fake, 0, sizeof(*fake));
	fake->phy[0x019e] = 0xa5a4;
	fake->phy[0x0001] = 0x1234;
	fake->phy[0x040a] = 0x1034;
	fake->phy[0x0400] = 0x1235;
	fake->phy[0x0070] = 0xe000;
	fake->phy[0x0140] = 0xa500;
	fake->phy[0x0640] = 0x3a00;
	fake->phy[0x0840] = 0x4b00;
	fake->radio[0x0144] = 3;
	fake->radio[0x0344] = 3;
	memcpy(fake->table_sample_neg128,
	       (u8[]){0x11, 0x22, 0x33, 0x44, 0x55, 0x66}, 6);
	memcpy(fake->table_sample_pos127,
	       (u8[]){0x91, 0x82, 0x73, 0x64, 0x55, 0x46}, 6);
	memcpy(fake->table_sample_neg1,
	       (u8[]){0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6}, 6);
}

static struct ob_phy_a7089_txpwr_input input_for(u32 pi164,
							 u8 *indices,
							 bool prefix_selected)
{
	struct ob_phy_a7089_txpwr_input input = { 0 };
	struct ob_phy_96203_input *prefix = &input.post_resetcca;

	prefix->explicitly_synthetic = true;
	prefix->chip_id = 0x4352;
	prefix->chip_revision = 3;
	prefix->phy_revision = 0x0a; /* synthetic profile guard, not PI+0x164 */
	prefix->radio_id = 0x2069;
	prefix->radio_revision = 4;
	prefix->radio_class = 0;
	prefix->acphychipid_present = true;
	prefix->acphychipid = 0x4352;
	prefix->loop_bound_present = true;
	prefix->loop_bound = 2; /* final BCM4352 AC attach bound */
	prefix->second_loop_gate_present = true;
	prefix->second_loop_gate = 4; /* selected skip of the second sub_96203 loop */
	prefix->caller_gate_present = true;
	prefix->reset_marker = prefix_selected ? 1 : 0;
	prefix->nested_32c = 0;
	prefix->pi_164_present = true;
	prefix->pi_164 = pi164;
	input.nested_power_indices = indices;
	input.nested_power_indices_length = indices ? 2 : 0;
	input.nested_342_present = true;
	return input;
}

static void configure_post_aa63e_fixture(
	struct ob_phy_a7089_txpwr_input *input, struct fake_backend *fake,
	u8 nested_32c_snapshot, u8 rxgainerr_return, u8 pi_a7, u8 pi_a4)
{
	static const u8 band_offsets[5] = {0, 5, 10, 15, 20};
	u8 band;
	u8 core, i;

	input->post_aa63e_inputs_present = true;
	input->post_aa63e_lookup_mask_present = true;
	input->post_aa63e_nested_32c_snapshot_present = true;
	input->post_aa63e_nested_32c_snapshot = nested_32c_snapshot;
	input->post_aa63e_pi_a7_present = true;
	input->post_aa63e_pi_a7 = pi_a7;
	input->post_aa63e_pi_a4_present = true;
	input->post_aa63e_pi_a4 = pi_a4;
	input->post_aa63e_pi_a6_present = true;
	input->post_aa63e_pi_a6 = 0x0005;
	input->post_aa63e_rxgainerr_state_present = true;
	input->post_aa63e_phy_type = 0x0b;
	memset(input->pi_rxgainerr_1e3_through_1fb, 0,
	       sizeof(input->pi_rxgainerr_1e3_through_1fb));
	if ((u8)input->pi_17e <= 0x0e)
		band = 0;
	else if ((u8)input->pi_17e <= 0x30)
		band = 1;
	else if ((u8)input->pi_17e <= 0x40)
		band = 2;
	else if ((u8)input->pi_17e <= 0x80)
		band = 3;
	else
		band = 4;
	input->pi_rxgainerr_1e3_through_1fb[band_offsets[band]] = (u8)-2;
	input->pi_rxgainerr_1e3_through_1fb[band_offsets[band] + 1u] = 3;
	input->pi_rxgainerr_1e3_through_1fb[band_offsets[band] + 4u] =
		rxgainerr_return;
	input->post_aa63e_sub8f086_state_present = true;
	memset(input->nested_coefficients_450_through_571, 0,
	       sizeof(input->nested_coefficients_450_through_571));
	for (i = 0; i < 6; i++) {
		input->nested_candidate_counts_64a_through_64f[i] = 1;
		input->nested_candidate_counts_6c2_through_6c7[i] = 1;
		input->nested_initial_650_through_655[i] =
			(u8[]){0x2b, 0x2b, 0x2b, 0x34, 0x34, 0x64}[i];
	}
	input->nested_candidate_counts_6c2_through_6c7_present = true;
	for (core = 0; core < 2; core++) {
		size_t row = (size_t)core * 0x78u;

		input->nested_coefficients_450_through_571[0x0f + core * 3u] = 2;
		for (i = 0; i < 6; i++) {
			input->nested_coefficients_450_through_571[
				row + 0x1a + i * 0x0a] = (u8)(i + 1u);
			input->nested_coefficients_450_through_571[
				row + 0x56 + i * 0x0a] = (u8)(i + 5u);
		}
	}
	/* Explicit unused-by-default second candidate for core one's first byte. */
	input->nested_coefficients_450_through_571[0x78u + 0x1bu] = 2;
	input->nested_coefficients_450_through_571[0x78u + 0x57u] = 6;
	input->nested_gainerr_state_211 = fake->nested_gainerr_state_211;
	input->nested_gainerr_state_211_length =
		(u8)sizeof(fake->nested_gainerr_state_211);
	fake->nested_carriersearch_count = 2;
	input->post_aa63e_carriersearch_state_present = true;
	input->nested_carriersearch_count = &fake->nested_carriersearch_count;
	input->post_aa63e_nested_904_present = true;
	input->post_aa63e_nested_904 = 0x1357;
	input->post_aa63e_nested_902_present = true;
	input->post_aa63e_nested_902 = 0x2468;
	memcpy(fake->nested_gainerr_state_211,
	       (u8[]){0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0, 0, 0},
	       sizeof(fake->nested_gainerr_state_211));
}

static int run_case(struct fake_backend *fake, u32 pi164, u8 *indices,
		    bool prefix_selected,
		    struct ob_phy_a7089_txpwr_result *result)
{
	struct ob_phy_a7089_txpwr_input input = input_for(pi164, indices,
							  prefix_selected);
	seed(fake);
	input.nested_txpwrctrl_values = fake->nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length =
		(u8)sizeof(fake->nested_txpwrctrl_values);
	return ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, fake,
					    &input, result);
}

static void test_selected_post_resetcca_and_two_onehot_calls(void)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_result result;
	struct fake_backend fake;
	u16 second_first_read = 0xffff;
	u16 i;
	unsigned int helper_reads = 0;
	unsigned int phy040a_events = 0;

	check("caller return", run_case(&fake, 2, indices, true, &result), 0);
	check("caller status", result.status, OB_A7089_TXPWR_SYNTHETIC_COMPLETE);
	check("prefix selected", result.post_resetcca.caller_selected, 1);
	check("prefix completed", result.post_resetcca.status,
	      OB_96203_SYNTHETIC_COMPLETE);
	check("PI 164 selects PHY 040a RMW", result.phy_040a_selected, 1);
	check("derived helper calls", result.helper_calls_completed, 2);
	check("caller selector 0", result.core_selector[0], 1);
	check("caller selector 1", result.core_selector[1], 2);
	check("signed index 0", result.power_index[0], -128);
	check("signed index 1", result.power_index[1], 127);
	check("helper 0 core count", result.helper[0].selected_core_count, 1);
	check("helper 1 core count", result.helper[1].selected_core_count, 1);
	check("caller terminal", result.terminal_boundary, 0xaa62d);
	check("readiness false", result.hardware_ready, 0);
	check("txpwrctrl selected inner reads", result.txpwrctrl_per_core_reads, 1);
	check("txpwrctrl arg0 software field write", result.txpwrctrl_arg0_written, 1);
	check("txpwrctrl logical events", result.txpwrctrl_logical_event_count, 4);
	check("txpwrctrl callbacks", result.txpwrctrl_callback_count, 5);
	check("TSSI threshold", result.tssi_threshold, 0x80);
	check("TSSI threshold is software result", result.tssi_threshold_valid, 1);
	check("TSSI PHY write completed", result.tssi_phy_write_complete, 1);
	check("caller PHY write source site",
	      fake.events[fake.event_count - 1].site, 0xaa61e);
	check("caller PHY write register",
	      fake.events[fake.event_count - 1].reg, 0x1641);
	check("caller PHY write value",
	      fake.events[fake.event_count - 1].value, 0x7f80);
	check("TSSI PHY write visible in persistent backend",
	      fake.phy[0x1641], 0x7f80);
	check("txpwrctrl selected arm clears PHY 0x0070",
	      fake.phy[0x0070], 0x0000);
	check("joined logical events through TSSI PHY write",
	      result.logical_event_count, 89);
	check("joined callbacks through TSSI PHY write", result.callback_count, 114);
	check("nested txpwrctrl byte core0",
	      fake.nested_txpwrctrl_values[0], 0x3a);
	check("nested txpwrctrl byte core1",
	      fake.nested_txpwrctrl_values[1], 0x4b);
	check("single persistent callback stream", fake.event_count,
	      result.callback_count);
	/* The prefix changes PHY 0x019e; each TX helper restores that shared
	 * post-prefix value, not the initial fixture value. */
	check("persistent PHY restore", fake.phy[0x019e], 0xa580);
	check("PHY 040a updated", fake.phy[0x040a], 0x1234);
	check("power input bytes preserved", indices[0], 0x80);
	check("power input byte1 preserved", indices[1], 0x7f);
	check("TABLE7 core0 exists", fake.table[7][0x100], 0x3322);
	check("TABLE7 core1 exists", fake.table[7][0x101], 0x7382);
	check("TABLE0c core0 exists", fake.table[0x0c][0x63], 0x0011);
	check("TABLE0c core1 exists", fake.table[0x0c][0x67], 0x0091);
	for (i = 0; i < fake.event_count; i++) {
		if (fake.events[i].site == 0xaa57b) {
			check("PHY040a logical event id", fake.events[i].logical_event,
			      52);
			phy040a_events++;
		}
		if (fake.events[i].site == 0x9ce1e) {
			helper_reads++;
			if (helper_reads == 2)
				second_first_read = fake.events[i].value;
		}
	}
	check("two helper entries", helper_reads, 2);
	check("PHY040a RMW expands to two callbacks", phy040a_events, 2);
	check("second helper sees persistent restored PHY state",
	      second_first_read, 0xa580);
	printf("selected_join logical=%u callbacks=%u prefix_logical=%u "
	       "prefix_callbacks=%u suffix_logical=%u suffix_callbacks=%u\n",
	       result.logical_event_count, result.callback_count,
	       result.post_resetcca.logical_event_count,
	       result.post_resetcca.callback_count,
	       result.logical_event_count - result.post_resetcca.logical_event_count,
	       result.callback_count - result.post_resetcca.callback_count);
}

static void test_predicate_and_prefix_skip_are_independent(void)
{
	static const u32 selected[] = {2, 3, 5, 6};
	u8 indices[2] = {0xff, 0x80};
	struct ob_phy_a7089_txpwr_result result;
	struct fake_backend fake;
	unsigned int i;

	for (i = 0; i < sizeof(selected) / sizeof(selected[0]); i++) {
		check("selected predicate run",
		      run_case(&fake, selected[i], indices, false, &result), 0);
		check("selected predicate RMW", result.phy_040a_selected, 1);
		check("post-96203 loop still has two calls",
		      result.helper_calls_completed, 2);
	}
	check("unselected PI164 still reaches helper loop",
	      run_case(&fake, 0, indices, false, &result), 0);
	check("unselected PI164 skips only 040a", result.phy_040a_selected, 0);
	check("unselected PI164 keeps both helper calls",
	      result.helper_calls_completed, 2);
	check("caller prefix can be skipped", result.post_resetcca.status,
	      OB_96203_SKIPPED_BY_CALLER);
	check("skip has no prefix callbacks", result.post_resetcca.callback_count, 0);
}

static void test_invalid_inputs_reject_before_callbacks(void)
{
	u8 indices[2] = {1, 2};
	struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);
	struct ob_phy_a7089_txpwr_result result;
	struct fake_backend fake;

	seed(&fake);
	input.post_resetcca.loop_bound = 1;
	check("reject non-final attach bound",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), -EINVAL);
	check("no callback on invalid bound", fake.event_count, 0);
	input = input_for(2, indices, true);
	input.nested_power_indices_length = 1;
	check("reject short nested state",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), -EINVAL);
	check("no callback on missing nested byte", fake.event_count, 0);
	input = input_for(2, indices, true);
	input.post_resetcca.phy_revision = 1;
	check("reject mismatched synthetic sub96203 profile",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), -EINVAL);
	check("no callback on mismatched profile", fake.event_count, 0);
	input = input_for(2, indices, true);
	input.nested_txpwrctrl_values = NULL;
	input.nested_txpwrctrl_values_length = 0;
	check("reject missing txpwrctrl software output",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), -EINVAL);
	check("no callback on missing txpwrctrl output", fake.event_count, 0);
	input = input_for(2, indices, true);
	seed(&fake);
	input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.nested_342_present = false;
	check("reject missing nested 342 input",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), -EINVAL);
	check("no callback on missing nested 342", fake.event_count, 0);
	input = input_for(2, indices, true);
	seed(&fake);
	input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.post_resetcca.acphychipid = 0x4360;
	check("reject contradictory explicit acphychipid",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), -EINVAL);
	check("no callback on contradictory acphychipid", fake.event_count, 0);
	input = input_for(2, indices, true);
	seed(&fake);
	input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.sub995ab_tail_present = true;
	input.pi_17e_present = true;
	input.nested_456 = fake.nested_456;
	input.nested_456_length = sizeof(fake.nested_456);
	input.phy_context_31_present = true;
	input.saved_pi_fa0_present = true;
	input.mac_state_present = true;
	input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
	check("reject missing PI+0xfa2 before callbacks",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, &platform_ops,
					   &fake, &input, &result), -EINVAL);
	check("missing PI+0xfa2 does not start prefix callbacks",
	      fake.event_count, 0);
}

static void test_txpwrctrl_non_equal_branch_and_tssi_thresholds(void)
{
	static const struct {
		u32 pi164;
		u16 nested_342;
		u8 threshold;
	} cases[] = {
		{0, 0x0203, 0x26}, {1, 0, 0x18}, {2, 0, 0x80},
		{3, 0, 0x1c}, {5, 0, 0x80}, {0x0a, 0x0203, 0x80},
	};
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_result result;
	struct fake_backend fake;
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct ob_phy_a7089_txpwr_input input;

		seed(&fake);
		fake.phy[0x0070] = 0x1234;
		input = input_for(cases[i].pi164, indices, false);
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		input.nested_342 = cases[i].nested_342;
		check("non-equal txpwrctrl arm run",
		      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
						   &input, &result), 0);
		check("non-equal txpwrctrl skips child reads",
		      result.txpwrctrl_per_core_reads, 0);
		check("non-equal txpwrctrl callbacks", result.txpwrctrl_callback_count, 3);
		check("non-equal txpwrctrl logical", result.txpwrctrl_logical_event_count, 2);
		check("non-equal joined callback count", result.callback_count,
		      (cases[i].pi164 == 2 || cases[i].pi164 == 3 ||
		       cases[i].pi164 == 5 || cases[i].pi164 == 6) ? 48 : 46);
		check("non-equal joined logical count", result.logical_event_count,
		      (cases[i].pi164 == 2 || cases[i].pi164 == 3 ||
		       cases[i].pi164 == 5 || cases[i].pi164 == 6) ? 36 : 35);
		check("TSSI threshold branch", result.tssi_threshold, cases[i].threshold);
		check("TSSI write value narrows to u16",
		      fake.events[fake.event_count - 1].value,
		      (u16)(0x7f00u + cases[i].threshold));
		check("TSSI write register for explicit BCM4352 selector",
		      fake.events[fake.event_count - 1].reg, 0x1641);
		check("PHY 0070 mask clear preserves unrelated bits",
		      fake.phy[0x0070], 0x1234);
	}
}

static void test_txpwrctrl_runtime_samples_update_nested_bytes(void)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_input input;
	struct ob_phy_a7089_txpwr_result result;
	struct fake_backend fake;

	seed(&fake);
	fake.phy[0x0640] = 0x7f55;
	fake.phy[0x0840] = 0x8000;
	input = input_for(0, indices, false);
	input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	check("runtime sample variation executes",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), 0);
	check("runtime sample core0 value is masked and narrowed",
	      fake.nested_txpwrctrl_values[0], 0x7f);
	check("runtime sample core1 value is masked and narrowed",
	      fake.nested_txpwrctrl_values[1], 0x00);
}

static void test_selected_txpwrctrl_skip_arm_join_counts(void)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_input input;
	struct ob_phy_a7089_txpwr_result result, failed;
	struct fake_backend fake;
	unsigned int fail_at;

	seed(&fake);
	fake.phy[0x0070] = 0x1234;
	input = input_for(2, indices, true);
	input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	check("selected prefix with txpwrctrl skip branch",
	      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					   &input, &result), 0);
	check("skip branch selected result complete",
	      result.status, OB_A7089_TXPWR_SYNTHETIC_COMPLETE);
	check("skip branch per-core reads absent",
	      result.txpwrctrl_per_core_reads, 0);
	check("skip branch logical events", result.logical_event_count, 87);
	check("skip branch callbacks", result.callback_count, 112);
	check("skip branch callback stream count", fake.event_count, 112);
	for (fail_at = 1; fail_at <= result.callback_count; fail_at++) {
		int ret;

		seed(&fake);
		fake.phy[0x0070] = 0x1234;
		fake.fail_at = (u16)fail_at;
		input = input_for(2, indices, true);
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		ret = ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
						  &input, &failed);
		if (ret != -EIO || failed.status != OB_A7089_TXPWR_FAILED_BACKEND ||
		    fake.event_count != fail_at || failed.hardware_ready ||
		    failed.failed_site != fake.events[fail_at - 1].site ||
		    (fail_at == result.callback_count &&
		     failed.tssi_phy_write_complete)) {
			printf("FAIL: skip-arm fail-stop position %u ret=%d events=%u\n",
			       fail_at, ret, fake.event_count);
			failures++;
			return;
		}
	}
}

static void test_callback_failure_sweep(void)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_result success, failed;
	struct fake_backend fake;
	unsigned int fail_at;

	if (run_case(&fake, 2, indices, true, &success)) {
		failures++;
		return;
	}
	for (fail_at = 1; fail_at <= success.callback_count; fail_at++) {
		int ret;
		seed(&fake);
		fake.fail_at = (u16)fail_at;
		ret = ob_phy_a7089_txpwr_test_only(
			&prefix_ops, &tx_ops, NULL, &fake,
			&(struct ob_phy_a7089_txpwr_input){
				.post_resetcca = input_for(2, indices, true).post_resetcca,
				.nested_power_indices = indices,
				.nested_power_indices_length = 2,
				.nested_txpwrctrl_values = fake.nested_txpwrctrl_values,
				.nested_txpwrctrl_values_length = 2,
				.nested_342_present = true,
			}, &failed);
		if (ret != -EIO || failed.status != OB_A7089_TXPWR_FAILED_BACKEND ||
		    fake.event_count != fail_at || failed.hardware_ready ||
		    failed.failed_site != fake.events[fail_at - 1].site ||
		    (fail_at == success.callback_count &&
		     failed.tssi_phy_write_complete)) {
			printf("FAIL: fail-stop sweep position %u ret=%d events=%u\n",
			       fail_at, ret, fake.event_count);
			failures++;
			return;
		}
	}
}

static int run_tail_case(struct fake_backend *fake, u8 pi_fa2, u16 chanspec,
			u8 nested0, u8 nested1, u8 nested2, u8 nested3,
			u8 context31, u8 saved_fa0,
			enum ob_phy_a7089_mac_state mac_state,
			bool provide_platform,
			struct ob_phy_a7089_txpwr_result *result)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

	seed(fake);
	fake->nested_456[0] = nested0;
	fake->nested_456[1] = nested1;
	fake->nested_456[2] = nested2;
	fake->nested_456[3] = nested3;
	input.nested_txpwrctrl_values = fake->nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.sub995ab_tail_present = true;
	input.pi_fa2_present = true;
	input.pi_fa2 = pi_fa2;
	input.pi_17e_present = true;
	input.pi_17e = chanspec;
	input.nested_456 = fake->nested_456;
	input.nested_456_length = sizeof(fake->nested_456);
	input.phy_context_31_present = true;
	input.phy_context_31 = context31;
	input.saved_pi_fa0_present = true;
	input.saved_pi_fa0 = saved_fa0;
	input.mac_state_present = true;
	input.mac_state_at_99528 = mac_state;
	return ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
		provide_platform ? &platform_ops : NULL, fake, &input, result);
}

static int run_post_aa63e_variant_sample(
	struct fake_backend *fake, u8 saved_fa0, u8 lookup_mask,
	u16 radio_sample, u8 nested_32c_snapshot, u8 rxgainerr_return,
	u8 pi_a7, u8 pi_a4, u8 core1_candidate0, u16 rfseq_sample,
	u32 fail_at,
	struct ob_phy_a7089_txpwr_result *result)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

	seed(fake);
	fake->fail_at = fail_at;
	fake->radio[0x090b] = radio_sample;
	/* Explicit synthetic completion sample for selected RF-sequence polls. */
	fake->phy[0x0403] = rfseq_sample;
	input.nested_txpwrctrl_values = fake->nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.sub995ab_tail_present = true;
	input.pi_fa2_present = true;
	input.pi_fa2 = 0; /* unchanged zero vector: no MAC delegation */
	input.pi_17e_present = true;
	input.pi_17e = 0;
	input.nested_456 = fake->nested_456;
	input.nested_456_length = sizeof(fake->nested_456);
	input.phy_context_31_present = true;
	/* Nonzero selects the vendor's hardware arm of
	 * wlc_phy_rxcore_setstate_acphy; zero is software-only. The nested 4-byte
	 * vector below is unchanged, so sub_99528's own shared+0x31 gate does not
	 * take its suspend path. */
	input.phy_context_31 = 1;
	input.saved_pi_fa0_present = true;
	input.saved_pi_fa0 = saved_fa0;
	input.mac_state_present = true;
	input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
	input.post_aa63e_inputs_present = true;
	input.post_aa63e_lookup_nonnull_mask = lookup_mask;
	configure_post_aa63e_fixture(&input, fake, nested_32c_snapshot,
				     rxgainerr_return, pi_a7, pi_a4);
	input.nested_candidate_counts_6c2_through_6c7[0] = core1_candidate0;
	return ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
						    &platform_ops, fake, &input,
						    result);
}

static int run_post_aa63e_variant(
	struct fake_backend *fake, u8 saved_fa0, u8 lookup_mask,
	u16 radio_sample, u8 nested_32c_snapshot, u8 rxgainerr_return,
	u8 pi_a7, u8 pi_a4, u8 core1_candidate0, u16 fail_at,
	struct ob_phy_a7089_txpwr_result *result)
{
	return run_post_aa63e_variant_sample(fake, saved_fa0, lookup_mask,
		radio_sample, nested_32c_snapshot, rxgainerr_return, pi_a7,
		pi_a4, core1_candidate0, 0x0000, fail_at, result);
}

static int run_post_aa63e_case(struct fake_backend *fake, u8 saved_fa0,
			       u8 lookup_mask, u16 radio_sample,
			       struct ob_phy_a7089_txpwr_result *result)
{
	return run_post_aa63e_variant(fake, saved_fa0, lookup_mask, radio_sample,
				      0, 0, 0, 0, 1, 0, result);
}

static int run_post_aa63e_carriersearch(
	struct fake_backend *fake, u32 pi164, u8 saved_fa0,
	u16 carriersearch_count, u16 nested_904, u16 chanspec, u16 fail_at,
	struct ob_phy_a7089_txpwr_result *result)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_input input = input_for(pi164, indices, true);

	seed(fake);
	fake->fail_at = fail_at;
	input.nested_txpwrctrl_values = fake->nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.sub995ab_tail_present = true;
	input.pi_fa2_present = true;
	input.pi_fa2 = 0;
	input.pi_17e_present = true;
	input.pi_17e = chanspec;
	input.nested_456 = fake->nested_456;
	input.nested_456_length = sizeof(fake->nested_456);
	input.phy_context_31_present = true;
	input.phy_context_31 = 0;
	input.saved_pi_fa0_present = true;
	input.saved_pi_fa0 = saved_fa0;
	input.mac_state_present = true;
	input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
	input.post_aa63e_lookup_nonnull_mask = 1;
	fake->radio[0x090b] = 0x0100;
	configure_post_aa63e_fixture(&input, fake, 0, 0, 0, 0);
	fake->nested_carriersearch_count = carriersearch_count;
	input.post_aa63e_nested_904 = nested_904;
	return ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
						    &platform_ops, fake, &input,
						    result);
}

/* PHY[0x0403]=0 is an explicit synthetic fake-backend sample. This regression
 * verifies both bounded RFSEQ polls without claiming a target observation. */
static void test_rxcore_rfseq_20001_read_exhaustion(void)
{
	static struct fake_backend fake;
	struct ob_phy_a7089_txpwr_result result;
	struct ob_phy_a7089_txpwr_result failed;
	int ret;
	int last_delay;
	int final_read;
	int final_restore;
	u32 fail_at;

	ret = run_post_aa63e_variant_sample(&fake, 1, 1, 0x0100, 1, 0,
						  1, 1, 1, 0x0003, 0, &result);
	check("20,001-read RFSEQ exhaustion returns normally", ret, 0);
	check("20,001-read RFSEQ path reaches historical caller return",
	      result.terminal_boundary, 0xaa781);
	check("20,001-read RFSEQ path remains synthetic completion",
	      result.status, OB_A7089_TXPWR_SYNTHETIC_COMPLETE);
	check("first RFSEQ consumes exact read bound",
	      result.post_aa63e_rfseq_poll_reads[0], 20001);
	check("second RFSEQ consumes exact read bound",
	      result.post_aa63e_rfseq_poll_reads[1], 20001);
	check("both RFSEQ polls consume exact delay bound",
	      result.post_aa63e_rfseq_poll_delays[0] +
	      result.post_aa63e_rfseq_poll_delays[1], 40000);
	check("both RFSEQ polls record normal exhaustion",
	      result.post_aa63e_rfseq_poll_exhausted[0] &&
	      result.post_aa63e_rfseq_poll_exhausted[1], 1);
	check("exhaustion callback total", result.callback_count, 80187);
	check("exhaustion logical event total", result.logical_event_count, 80140);
	check("exhaustion backend callback total", fake.event_count, 80187);
	last_delay = find_event_site(&fake, 0x097c99, 39999);
	final_read = find_event_site(&fake, 0x097ca6, 40001);
	final_restore = find_event_site(&fake, 0x097cc3, 1);
	check("terminal poll and restoration callbacks are located",
	      last_delay >= 0 && final_read >= 0 && final_restore >= 0, 1);
	if (last_delay < 0 || final_read < 0 || final_restore < 0)
		return;
	check("last bounded-poll delay has 10us value",
	      fake.events[last_delay].value, 10);
	check("second RFSEQ restoration follows its final poll read",
	      final_restore > final_read, 1);

	/* Fail at the final permitted delay. The terminal read and restoration
	 * must not run after the backend reports an error. */
	fail_at = (u32)(last_delay + 1);
	ret = run_post_aa63e_variant_sample(&fake, 1, 1, 0x0100, 1, 0,
						  1, 1, 1, 0x0003, fail_at, &failed);
	check("final RFSEQ delay failure propagates", ret, -EIO);
	check("final RFSEQ delay failure stops at injected callback",
	      fake.event_count, fail_at);
	check("final RFSEQ delay failure site", failed.failed_site, 0x097c99);
	check("final RFSEQ delay failure prevents final poll read",
	      find_event_site(&fake, 0x097ca6, 40001), -1);
	check("final RFSEQ delay failure prevents restoration",
	      find_event_site(&fake, 0x097cc3, 1), -1);
	check("final RFSEQ delay failure retains 20,000 second-poll reads",
	      failed.post_aa63e_rfseq_poll_reads[1], 20000);
	check("final RFSEQ delay failure retains 20,000 second-poll delays",
	      failed.post_aa63e_rfseq_poll_delays[1], 20000);

	/* A failed final poll read is also fail-stop and is not counted as a
	 * successful sample; no subsequent restore callback is permitted. */
	fail_at = (u32)(final_read + 1);
	ret = run_post_aa63e_variant_sample(&fake, 1, 1, 0x0100, 1, 0,
						  1, 1, 1, 0x0003, fail_at, &failed);
	check("final RFSEQ read failure propagates", ret, -EIO);
	check("final RFSEQ read failure stops at injected callback",
	      fake.event_count, fail_at);
	check("final RFSEQ read failure site", failed.failed_site, 0x097ca6);
	check("final RFSEQ read failure prevents restoration",
	      find_event_site(&fake, 0x097cc3, 1), -1);
	check("final RFSEQ read failure has no later callbacks",
	      failed.callback_count, fail_at);
}

static void test_carriersearch_arg0_selected_return(void)
{
	struct fake_backend fake;
	struct ob_phy_a7089_txpwr_result result;
	unsigned int fail_at;
	int ret;

	ret = run_post_aa63e_carriersearch(&fake, 2, 2, 1, 0x9abc, 0, 0,
					   &result);
	check("carriersearch normal caller return", ret, 0);
	check("carriersearch helper completed", result.post_aa63e_carriersearch_completed, 1);
	check("carriersearch selected from depth one", result.post_aa63e_carriersearch_effects_selected, 1);
	check("carriersearch decremented nested depth", result.post_aa63e_carriersearch_count_after, 0);
	check("carriersearch caller return boundary", result.terminal_boundary, 0xaa781);
	check("carriersearch complete status", result.status, OB_A7089_TXPWR_SYNTHETIC_COMPLETE);
	check("carriersearch total callbacks", result.callback_count, 156);
	check("carriersearch total logical events", result.logical_event_count, 117);
	check("carriersearch classifier source read", fake.events[141].site, 0x91273);
	check("carriersearch classifier PHY register", fake.events[141].reg, 0x0140);
	check("carriersearch classifier source write", fake.events[142].site, 0x91293);
	check("carriersearch classifier mask seven value",
	      fake.events[142].value, 0xa507);
	check("carriersearch OFDM first register", fake.events[143].reg, 0x02ed);
	check("carriersearch OFDM first set value", fake.events[144].value, 0x0010);
	check("carriersearch OFDM final register", fake.events[149].reg, 0x02f9);
	check("carriersearch first core RMW register", fake.events[151].reg, 0x06d4);
	check("carriersearch second core RMW register", fake.events[153].reg, 0x08d4);
	check("carriersearch final PHY register", fake.events[155].reg, 0x0339);
	check("carriersearch final nested+904 value", fake.events[155].value, 0x9abc);
	check("carriersearch persistent OFDM state", fake.phy[0x02f9], 0x0010);
	check("carriersearch persistent final write", fake.phy[0x0339], 0x9abc);

	/* A count other than one is decremented modulo u16 and returns without
	 * touching PHY state. This is explicit software-state input, not a default. */
	ret = run_post_aa63e_carriersearch(&fake, 2, 2, 2, 0x9abc, 0, 0,
					   &result);
	check("carriersearch nonzero post-decrement returns normally", ret, 0);
	check("carriersearch depth two becomes one",
	      result.post_aa63e_carriersearch_count_after, 1);
	check("carriersearch depth two skips effects",
	      result.post_aa63e_carriersearch_effects_selected, 0);
	check("carriersearch depth two preserves prefix and lookup poll callbacks",
	      result.callback_count, 141);
	ret = run_post_aa63e_carriersearch(&fake, 2, 2, 0, 0x9abc, 0, 0,
					   &result);
	check("carriersearch zero underflows as u16 and returns", ret, 0);
	check("carriersearch zero wraps to ffff",
	      result.post_aa63e_carriersearch_count_after, 0xffff);
	check("carriersearch underflow skips hardware effects",
	      result.post_aa63e_carriersearch_effects_selected, 0);

	/* With PI+0x164==0, sub_979bc writes the unstrided nested+0x902
	 * word into 0x06da/core-stride and the classifier uses mask 6 for C000. */
	ret = run_post_aa63e_carriersearch(&fake, 0, 2, 1, 0x9abc,
					   0x4000, 0, &result);
	check("carriersearch PI164 zero branch returns", ret, 0);
	check("carriersearch PI164 zero full logical count",
	      result.logical_event_count, 101);
	check("carriersearch PI164 zero full callback count",
	      result.callback_count, 130);
	check("classifier mask six selected", result.post_aa63e_classifier_mask, 6);
	check("classifier mask six preserves bit zero", fake.phy[0x0140], 0xa506);
	check("PI164 zero writes nested+902 to core0", fake.phy[0x06da], 0x2468);
	check("PI164 zero writes nested+902 to core1", fake.phy[0x08da], 0x2468);
	check("PI164 zero preserves final nested+904 write",
	      fake.phy[0x0339], 0x9abc);

	{
		static const u32 carrier_sites[15] = {
			0x91273, 0x91293,
			0x8faae, 0x8faae, 0x8fac5, 0x8fac5,
			0x8fadc, 0x8fadc, 0x8fb30, 0x8fb30,
			0x97a2e, 0x97a2e, 0x97a2e, 0x97a2e, 0x97b2e,
		};

		for (fail_at = 142; fail_at <= 156; fail_at++) {
			ret = run_post_aa63e_carriersearch(
				&fake, 2, 2, 1, 0x9abc, 0, fail_at, &result);
			if (ret != -EIO ||
			    result.status != OB_A7089_TXPWR_FAILED_BACKEND ||
			    fake.event_count != fail_at || result.hardware_ready ||
			    result.failed_site != carrier_sites[fail_at - 142]) {
				printf("FAIL: carriersearch fail-stop %u ret=%d events=%u site=%x\n",
				       fail_at, ret, fake.event_count, result.failed_site);
				failures++;
				return;
			}
		}
	}

	{
		static const u32 pi164_zero_sites[13] = {
			0x91273, 0x91293,
			0x8faae, 0x8faae, 0x8fac5, 0x8fac5,
			0x8fadc, 0x8fadc, 0x8fb30, 0x8fb30,
			0x97a16, 0x97a16, 0x97b2e,
		};

		for (fail_at = 118; fail_at <= 130; fail_at++) {
			ret = run_post_aa63e_carriersearch(
				&fake, 0, 2, 1, 0x9abc, 0x4000, fail_at,
				&result);
			if (ret != -EIO ||
			    result.status != OB_A7089_TXPWR_FAILED_BACKEND ||
			    fake.event_count != fail_at || result.hardware_ready ||
			    result.failed_site != pi164_zero_sites[fail_at - 118]) {
				printf("FAIL: carriersearch PI164-zero fail-stop %u ret=%d events=%u site=%x\n",
				       fail_at, ret, fake.event_count, result.failed_site);
				failures++;
				return;
			}
		}
	}
}

static void test_nonzero_txpwrctrl_and_aa63e_poll(void)
{
	struct fake_backend fake, same;
	struct ob_phy_a7089_txpwr_result result, other;
	unsigned int fail_at;
	static const u32 new_sites[] = {
		0x9077c, 0x9077c, 0x8f414, 0x8f414,
		0x8f414, 0x8f414, 0x93e61, 0x93e95,
	};
	int ret;

	ret = run_post_aa63e_case(&fake, 1, 0x01, 0x0100, &result);
	check("arg1 reaches selected carriersearch normal return", ret, 0);
	check("arg1 carriersearch completes synthetic path",
	      result.post_aa63e_carriersearch_completed, 1);
	check("arg1 helper and caller terminal boundary",
	      result.terminal_boundary, 0xaa781);
	check("arg1 nonzero helper selected", result.txpwrctrl_nonzero_path, 1);
	check("arg1 preserved as unsigned byte", result.txpwrctrl_nonzero_argument, 1);
	check("arg1 writes PI fa0", result.txpwrctrl_nonzero_store, 1);
	check("arg1 PI fa0 state", result.pi_fa0_after_txpwrctrl, 1);
	check("arg1 nested core updates", result.txpwrctrl_core_updates, 2);
	check("arg1 callback count", result.callback_count, 141);
	check("arg1 logical count", result.logical_event_count, 109);
	check("lookup match selects radio poll", result.post_aa63e_radio_poll_selected, 1);
	check("early radio poll satisfies", result.post_aa63e_poll_satisfied, 1);
	check("early radio poll attempts", result.post_aa63e_poll_attempts, 1);
	check("persistent core0 txctrl write", fake.phy[0x0644], 0x003a);
	check("persistent core1 txctrl write", fake.phy[0x0844], 0x004b);
	check("nonzero RMW order and first value",
	      fake.events[114].site, 0x9077c);
	check("nonzero mask write value", fake.events[115].value, 0xe000);
	check("core0 helper register", fake.events[116].reg, 0x0644);
	check("core0 helper value", fake.events[117].value, 0x003a);
	check("core1 helper register", fake.events[118].reg, 0x0844);
	check("core1 helper value", fake.events[119].value, 0x004b);
	check("poll delay is 10 us", fake.events[120].value, 10);
	check("poll selector is radio 0x90b", fake.events[121].reg, 0x090b);
	check("entry nested+32c snapshot selects zero arm",
	      result.post_aa63e_nested_gate_nonzero, 0);
	check("rxgainerr software helper completes",
	      result.post_aa63e_rxgainerr_completed, 1);
	check("rxgainerr chooses low chanspec band",
	      result.post_aa63e_rxgainerr_band, 0);
	check("rxgainerr first signed sample",
	      result.post_aa63e_rxgainerr_values[0], -2);
	check("sub8f086 returned delta core0",
	      result.post_aa63e_sub8f086_returns[0], 19);
	check("sub8f086 returned delta core1",
	      result.post_aa63e_sub8f086_returns[1], 19);
	check("nested+212 core0 arithmetic",
	      fake.nested_gainerr_state_211[1], 0x62);
	check("nested+213 core1 arithmetic",
	      fake.nested_gainerr_state_211[2], 0x67);
	check("PHY 0x400 bit zero cleared after software loop",
	      fake.phy[0x0400], 0x1234);
	check("PHY 0x400 RMW selected", result.post_aa63e_0400_selected, 1);

	/* Values 2..255 take the same instruction branch, and do not write PI+fa0. */
	check("arg2 reaches caller return", run_post_aa63e_case(&fake, 2, 1, 0x0100, &result),
	      0);
	check("arg2 does not store PI fa0", result.txpwrctrl_nonzero_store, 0);
	check("arg2 PI fa0 remains cleared", result.pi_fa0_after_txpwrctrl, 0);
	check("arg2 selected class-0 poll reaches caller return", result.terminal_boundary, 0xaa781);
	check("arg2 callback count includes selected poll", result.callback_count, 141);
	check("arg255 reaches caller return",
	      run_post_aa63e_case(&same, 0xff, 1, 0x0100, &other), 0);
	check("arg2 and arg255 traces have same callback count",
	      other.callback_count, result.callback_count);
	if (memcmp(fake.events, same.events,
		   fake.event_count * sizeof(fake.events[0])) != 0) {
		printf("FAIL: nonzero args 2 and 255 did not share the proven branch\n");
		failures++;
	}
	check("arg255 preserved distinctly", other.txpwrctrl_nonzero_argument, 0xff);

	/* The core-one candidate row is nested+0x6c2..0x6c7, not an alias of
	 * core zero's nested+0x64a..0x64f row. Its second candidate is deliberately
	 * different so stale-row reuse changes the software result. */
	ret = run_post_aa63e_variant(&fake, 1, 1, 0x0100, 0, 0, 0, 0, 2, 0,
				     &result);
	check("distinct core-count row reaches caller return", ret, 0);
	check("core zero uses nested+64a candidate limit",
	      result.post_aa63e_sub8f086_returns[0], 19);
	check("core one uses nested+6c2 candidate limit",
	      result.post_aa63e_sub8f086_returns[1], 20);
	check("core one selects its row-specific candidate",
	      result.post_aa63e_sub8f086_output[1][0], 6);
	check("core one row changes nested+213 result",
	      fake.nested_gainerr_state_211[2], 0x65);
	ret = run_post_aa63e_variant(&fake, 1, 1, 0x0100, 1, 0, 0, 0, 1, 0,
				     &result);
	check("matching nested snapshot reaches caller return", ret, 0);
	check("matching nested snapshot executes PHY 0xb read",
	      result.terminal_boundary, 0xaa781);
	check("matching nested snapshot callback total", result.callback_count, 142);
	check("matching nested snapshot logical total", result.logical_event_count, 110);
	check("matching nested snapshot completes rxgainerr",
	      result.post_aa63e_rxgainerr_completed, 1);
	/* U13: pi_a7 == mask(PHY[0x0b]) and pi_a4 == pi_a7 is the vendor's
	 * @0xaa52a skip condition, so the setter must not be reached at all. */
	check("U13 matching caller state skips the rxcore setter",
	      result.post_aa63e_rxcore_setstate_called, 0);
	ret = run_post_aa63e_variant(&fake, 1, 1, 0x0100, 1, 0, 1, 1, 1, 0,
				     &result);
	check("mismatch rxcore child reaches caller return", ret, 0);
	check("mismatch invokes rxcore setter",
	      result.post_aa63e_rxcore_setstate_called, 1);
	check("U13 mismatching caller state invokes the rxcore setter",
	      result.post_aa63e_rxcore_setstate_called, 1);
	check("mismatch preserves caller state argument",
	      result.post_aa63e_rxcore_argument, 1);
	check("mismatch RF sequence 0 poll reads",
	      result.post_aa63e_rfseq_poll_reads[0], 1);
	check("mismatch RF sequence 1 poll reads",
	      result.post_aa63e_rfseq_poll_reads[1], 1);
	check("mismatch restores 0x0400 after child",
	      fake.events[find_event_site(&fake, 0x97e10, 0)].value,
	      result.post_aa63e_rxcore_saved_0400);
	check("mismatch final RF-sequence restore uses saved 0x019e",
	      fake.events[find_event_site(&fake, 0x97cd4, 1)].value,
	      fake.events[find_event_site(&fake, 0x97c2f, 1)].value);
	check("mismatch reaches normal caller return", result.terminal_boundary,
	      0xaa781);
	check("mismatch RX-core child starts at historical 0401 read",
	      fake.events[find_event_site(&fake, 0x97d20, 0)].site, 0x97d20);
	check("mismatch first child register",
	      fake.events[find_event_site(&fake, 0x97d20, 0)].reg, 0x0401);
	check("mismatch second child register",
	      fake.events[find_event_site(&fake, 0x97d30, 0)].reg, 0x0400);
	check("mismatch RX-core state RMW value",
	      fake.events[find_event_site(&fake, 0x97d50, 1)].value, 0x0001);
	check("mismatch RX-core shifted field value",
	      fake.events[find_event_site(&fake, 0x97d6e, 1)].value, 0x0010);
	check("mismatch RF-sequence zero runs before one",
	      find_event_site(&fake, 0x97c1f, 0) <
	      find_event_site(&fake, 0x97c1f, 1), 1);
	check("mismatch RF-sequence delay placement omitted after immediate poll",
	      result.post_aa63e_rfseq_poll_delays[0] +
	      result.post_aa63e_rfseq_poll_delays[1], 0);
	check("mismatch final child restore source",
	      fake.events[find_event_site(&fake, 0x97e10, 0)].site,
	      0x97e10);
	check("mismatch RX-core arm records the shared+0xa7 store",
	      result.post_aa63e_rxcore_a7_store, 1);
	check("mismatch RX-core selects the MAC sequence arm",
	      result.post_aa63e_rxcore_mac_sequence, 1);
	check("mismatch RX-core software-only arm is not taken",
	      result.post_aa63e_rxcore_software_only, 0);
	check("mismatch RX-core suspends before the first PHY read",
	      find_event_site(&fake, 0x97d13, 0) <
	      find_event_site(&fake, 0x97d20, 0), 1);
	check("mismatch RX-core enables the MAC after the 0400 restore",
	      find_event_site(&fake, 0x97e1d, 0) >
	      find_event_site(&fake, 0x97e10, 0), 1);
	check("mismatch RX-core records the restored MAC",
	      result.post_aa63e_rxcore_mac_restored, 1);
	check("mismatch RX-core forbids no re-entry after a clean run",
	      result.post_aa63e_rxcore_reentry_forbidden, 0);
	check("mismatch complete callback count", result.callback_count, 187);
	check("mismatch complete logical count", result.logical_event_count, 140);
	{
		u16 fail_at;
		/* The suspend callback is the first operation inside the arm and the
		 * enable callback is the last. */
		u16 first_new = (u16)(find_event_site(&fake, 0x97d13, 0) + 1);
		u16 last_new = (u16)(find_event_site(&fake, 0x97e1d, 0) + 1);

		/* The matched-path baseline ends after callback 142. Inject failure at
		 * every newly introduced setter/RF-sequence callback and require stop. */
		for (fail_at = first_new; fail_at <= last_new; fail_at++) {
			int fail_ret = run_post_aa63e_variant(
				&fake, 1, 1, 0x0100, 1, 0, 1, 1, 1,
				fail_at, &other);

			if (fail_ret != -EIO ||
			    other.status != OB_A7089_TXPWR_FAILED_BACKEND ||
			    fake.event_count != fail_at ||
			    other.callback_count != fail_at ||
			    other.failed_site != fake.events[fail_at - 1u].site ||
			    other.hardware_ready) {
				printf("FAIL: rxcore fail-stop callback %u ret=%d events=%u site=%x\n",
				       fail_at, fail_ret, fake.event_count,
				       other.failed_site);
				failures++;
				break;
			}
		}
		/* Exercise the delay callback too: a still-set synthetic PHY[0x0403]
		 * sample takes the 10-us delay after the read (vendor 0x97cb5 loops
		 * while the force bit is set; a clear sample exits at 0x97caf).
		 * The delay index is located in the trace instead of being derived from
		 * the callback order, because the two bounded polls differ in length. */
		{
			int delay_index;

			ret = run_post_aa63e_variant_sample(
				&fake, 1, 1, 0x0100, 1, 0, 1, 1, 1, 0x0003, 0,
				&other);
			check("RF-sequence probe run returns normally", ret, 0);
			delay_index = find_event_site(&fake, 0x097c99, 0);
			check("RF-sequence delay callback is present",
			      delay_index >= 0, 1);
			if (delay_index < 0)
				return;
			fail_at = (u16)(delay_index + 1);
		}
		ret = run_post_aa63e_variant_sample(
			&fake, 1, 1, 0x0100, 1, 0, 1, 1, 1, 0x0003,
			fail_at, &other);
		check("RF-sequence delay failure is propagated", ret, -EIO);
		check("RF-sequence delay failure is fail-stop",
		      fake.event_count, fail_at);
		check("RF-sequence delay failure source site",
		      other.failed_site, 0x097c99);
		check("RF-sequence delay failure publishes no readiness",
		      other.hardware_ready, 0);
	}
	{
		u8 indices[2] = {0x80, 0x7f};
		struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

		seed(&fake);
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		input.sub995ab_tail_present = true;
		input.pi_fa2_present = true;
		input.pi_17e_present = true;
		input.nested_456 = fake.nested_456;
		input.nested_456_length = 4;
		input.phy_context_31_present = true;
		input.saved_pi_fa0_present = true;
		input.saved_pi_fa0 = 1;
		input.mac_state_present = true;
		input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
		input.post_aa63e_lookup_nonnull_mask = 1;
		configure_post_aa63e_fixture(&input, &fake, 0, 0, 0, 0);
		input.nested_candidate_counts_6c2_through_6c7_present = false;
		check("missing core-one candidate row is rejected",
		      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
						   &platform_ops, &fake,
						   &input, &result), -EINVAL);
		check("missing core-one row rejects before callbacks",
		      fake.event_count, 0);
	}
	ret = run_post_aa63e_variant(&fake, 1, 1, 0x0100, 1, 0, 0, 0, 1, 123,
				     &result);
	check("PHY 0xb callback error is fail-stop", ret, -EIO);
	check("PHY 0xb callback error site", result.failed_site, 0xaa672);
	check("PHY 0xb callback error prevents later callbacks",
	      fake.event_count, 123);
	{
		static const u32 matching_snapshot_sites[] = {
			0xaa672, 0x9742b, 0x97438, 0x97450, 0x97455,
			0x9746e, 0x9746e, 0x97478, 0x9748f, 0x9748f,
			0x97499, 0x974a8, 0x974b2, 0x974c2, 0x974d2,
			0x974e6, 0x974e6, 0x97555, 0xaa74d, 0xaa74d,
		};

		for (fail_at = 123; fail_at <= 142; fail_at++) {
			ret = run_post_aa63e_variant(
				&fake, 1, 1, 0x0100, 1, 0, 0, 0, 1, fail_at,
				&other);
			if (ret != -EIO ||
			    other.status != OB_A7089_TXPWR_FAILED_BACKEND ||
			    fake.event_count != fail_at || other.hardware_ready ||
			    other.failed_site != matching_snapshot_sites[fail_at - 123]) {
				printf("FAIL: matching-snapshot fail-stop %u ret=%d events=%u site=%x\n",
				       fail_at, ret, fake.event_count, other.failed_site);
				failures++;
				return;
			}
		}
	}

	/* If the first arg-zero call skips sub_906ae, explicit nested state is
	 * carried into the nonzero loop. The 0x80 sentinel skips only core 0. */
	{
		u8 indices[2] = {0x80, 0x7f};
		struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

		seed(&fake);
		fake.phy[0x0070] = 0x1234;
		fake.nested_txpwrctrl_values[0] = 0x80;
		fake.nested_txpwrctrl_values[1] = 0x12;
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		input.sub995ab_tail_present = true;
		input.pi_fa2_present = true;
		input.pi_17e_present = true;
		input.nested_456 = fake.nested_456;
		input.nested_456_length = 4;
		input.phy_context_31_present = true;
		input.saved_pi_fa0_present = true;
		input.saved_pi_fa0 = 2;
		input.mac_state_present = true;
		input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
		input.post_aa63e_lookup_nonnull_mask = 1;
		fake.radio[0x090b] = 0x0100;
		input.pi_17e = 0;
		configure_post_aa63e_fixture(&input, &fake, 0, 0, 0, 0);
			check("sentinel path reaches caller return", ob_phy_a7089_txpwr_test_only(
			      &prefix_ops, &tx_ops, &platform_ops, &fake, &input,
				      &result), 0);
		check("sentinel skips core zero", fake.phy[0x0644], 0);
		check("sentinel permits core one", fake.phy[0x0844], 0x12);
		check("sentinel performs one core update",
		      result.txpwrctrl_core_updates, 1);
	check("sentinel selected profile reaches carriersearch",
		      result.terminal_boundary, 0xaa781);
	}

	/* Successful sub_8e9b1 return cannot leave every output pointer null. */
	check("null lookup output state is rejected",
	      run_post_aa63e_case(&fake, 0, 0, 0, &result), -EINVAL);
	check("null lookup output rejects before callback", fake.event_count, 0);

	/* A bounded 100-attempt poll is normal completion when the bit never sets. */
	ret = run_post_aa63e_case(&fake, 0, 1, 0, &result);
	check("poll exhaustion reaches caller return", ret, 0);
	check("poll exhaustion remains distinct from backend failure",
	      result.status, OB_A7089_TXPWR_SYNTHETIC_COMPLETE);
	check("poll exhaustion is recorded", result.post_aa63e_poll_exhausted, 1);
	check("poll exhaustion attempt count", result.post_aa63e_poll_attempts, 100);
	check("poll exhaustion callback count", result.callback_count, 336);
	check("poll exhaustion has no arg-zero 120us tail", fake.event_count, 336);
	check("poll exhaustion terminal boundary", result.terminal_boundary, 0xaa781);

	/* Test the final permitted poll attempt, still followed by ordinary return. */
	/* A scripted sample sets the bit only on the final permitted read. */
	{
		u8 indices[2] = {0x80, 0x7f};
		struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);
		seed(&fake);
		for (fail_at = 0; fail_at < 100; fail_at++)
			fake.radio_090b_script[fail_at] = fail_at == 99 ? 0x0100 : 0;
		fake.radio_090b_script_length = 100;
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		input.sub995ab_tail_present = true;
		input.pi_fa2_present = true;
		input.pi_fa2 = 0;
		input.pi_17e_present = true;
		input.nested_456 = fake.nested_456;
		input.nested_456_length = 4;
		input.phy_context_31_present = true;
		input.saved_pi_fa0_present = true;
		input.saved_pi_fa0 = 0;
		input.mac_state_present = true;
		input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
		input.post_aa63e_lookup_nonnull_mask = 1;
		input.pi_17e = 0;
		configure_post_aa63e_fixture(&input, &fake, 0, 0, 0, 0);
		ret = ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
							&platform_ops, &fake,
							&input, &result);
		check("last poll reaches caller return", ret, 0);
		check("last poll attempt index", result.post_aa63e_poll_attempts, 100);
		check("last poll success distinguished", result.post_aa63e_poll_satisfied, 1);
		check("last poll is not exhausted", result.post_aa63e_poll_exhausted, 0);
	}

	/* Poll exhaustion repeats two fallible callbacks 100 times. Inject failure
	 * at every delay/read position, not just the first attempt, and require an
	 * immediate stop with the matching historical source site. */
	for (fail_at = 121; fail_at <= 320; fail_at++) {
		u32 expected_site = (fail_at & 1u) ? 0x93e61u : 0x93e95u;

		ret = run_post_aa63e_variant(&fake, 1, 1, 0, 0, 0, 0, 0, 1,
					     (u16)fail_at, &other);
		if (ret != -EIO ||
		    other.status != OB_A7089_TXPWR_FAILED_BACKEND ||
		    fake.event_count != fail_at ||
		    other.callback_count != fail_at || other.hardware_ready ||
		    other.failed_site != expected_site) {
			printf("FAIL: poll fail-stop position %u ret=%d events=%u site=%x expected=%x\n",
			       fail_at, ret, fake.event_count, other.failed_site,
			       expected_site);
			failures++;
			return;
		}
	}

	/* Each callback added after the accepted 114-callback predecessor is
	 * independently fail-stopped; no later operation may execute. */
	for (fail_at = 115; fail_at <= 122; fail_at++) {
		ret = run_post_aa63e_case(&fake, 1, 1, 0x0100, &result);
		if (ret != 0) {
			printf("FAIL: failure-sweep setup at callback %u\n", fail_at);
			failures++;
			return;
		}
		/* Rebuild the same input with failure injected at this exact position. */
		{
			u8 indices[2] = {0x80, 0x7f};
			struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

			seed(&fake);
			fake.radio[0x090b] = 0x0100;
			fake.fail_at = (u16)fail_at;
			input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
			input.nested_txpwrctrl_values_length = 2;
			input.sub995ab_tail_present = true;
			input.pi_fa2_present = true;
			input.pi_fa2 = 0;
			input.pi_17e_present = true;
			input.nested_456 = fake.nested_456;
			input.nested_456_length = 4;
			input.phy_context_31_present = true;
			input.saved_pi_fa0_present = true;
			input.saved_pi_fa0 = 1;
			input.mac_state_present = true;
			input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
			input.post_aa63e_lookup_nonnull_mask = 1;
			input.pi_17e = 0;
			configure_post_aa63e_fixture(&input, &fake, 0, 0, 0, 0);
			ret = ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
							    &platform_ops, &fake,
							    &input, &other);
			if (ret != -EIO || other.status != OB_A7089_TXPWR_FAILED_BACKEND ||
			    fake.event_count != fail_at || other.hardware_ready ||
			    other.failed_site != new_sites[fail_at - 115]) {
				printf("FAIL: new fail-stop position %u ret=%d events=%u site=%x\n",
				       fail_at, ret, fake.event_count, other.failed_site);
				failures++;
				return;
			}
		}
	}

	/* Fail each callback added by resetCCA and the final PHY[0x400] RMW. */
	{
		static const u32 post_reset_sites[] = {
			0x9742b, 0x97438, 0x97450, 0x97455,
			0x9746e, 0x9746e, 0x97478, 0x9748f, 0x9748f,
			0x97499, 0x974a8, 0x974b2, 0x974c2, 0x974d2,
			0x974e6, 0x974e6, 0x97555, 0xaa74d, 0xaa74d,
		};

		for (fail_at = 123; fail_at <= 141; fail_at++) {
			ret = run_post_aa63e_case(&fake, 1, 1, 0x0100, &result);
			if (ret != 0) {
				printf("FAIL: post-resetCCA failure setup %u\n", fail_at);
				failures++;
				return;
			}
			{
				u8 indices[2] = {0x80, 0x7f};
				struct ob_phy_a7089_txpwr_input input =
					input_for(2, indices, true);

				seed(&fake);
				fake.radio[0x090b] = 0x0100;
				fake.fail_at = (u16)fail_at;
				input.nested_txpwrctrl_values =
					fake.nested_txpwrctrl_values;
				input.nested_txpwrctrl_values_length = 2;
				input.sub995ab_tail_present = true;
				input.pi_fa2_present = true;
				input.pi_17e_present = true;
				input.pi_17e = 0;
				input.nested_456 = fake.nested_456;
				input.nested_456_length = 4;
				input.phy_context_31_present = true;
				input.saved_pi_fa0_present = true;
				input.saved_pi_fa0 = 1;
				input.mac_state_present = true;
				input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
				input.post_aa63e_lookup_nonnull_mask = 1;
				configure_post_aa63e_fixture(&input, &fake, 0, 0, 0, 0);
				ret = ob_phy_a7089_txpwr_test_only(
					&prefix_ops, &tx_ops, &platform_ops, &fake,
					&input, &other);
				if (ret != -EIO ||
				    other.status != OB_A7089_TXPWR_FAILED_BACKEND ||
				    fake.event_count != fail_at || other.hardware_ready ||
				    other.failed_site != post_reset_sites[fail_at - 123]) {
					printf("FAIL: resetCCA/RMW fail-stop %u ret=%d events=%u site=%x\n",
					       fail_at, ret, fake.event_count,
					       other.failed_site);
					failures++;
					return;
				}
			}
		}
	}

	/* The supported profile's class-0 lookup only populates stack slot -0x40. */
	{
		u8 indices[2] = {0x80, 0x7f};
		struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

		seed(&fake);
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		input.sub995ab_tail_present = true;
		input.pi_fa2_present = true;
		input.pi_17e_present = true;
		input.nested_456 = fake.nested_456;
		input.nested_456_length = 4;
		input.phy_context_31_present = true;
		input.saved_pi_fa0_present = true;
		input.mac_state_present = true;
		input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
		input.post_aa63e_lookup_nonnull_mask = 2;
		input.pi_17e = 0;
		configure_post_aa63e_fixture(&input, &fake, 0, 0, 0, 0);
		check("reject pointer slots outside class-0 profile",
		      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
						   &platform_ops, &fake,
						   &input, &result), -EINVAL);
		check("invalid lookup mask rejected before callbacks", fake.event_count, 0);
	}

	{
		u8 indices[2] = {0x80, 0x7f};
		struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

		seed(&fake);
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		input.sub995ab_tail_present = true;
		input.pi_fa2_present = true;
		input.pi_17e_present = true;
		input.nested_456 = fake.nested_456;
		input.nested_456_length = sizeof(fake.nested_456);
		input.phy_context_31_present = true;
		input.saved_pi_fa0_present = true;
		input.mac_state_present = true;
		input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
		input.post_aa63e_lookup_nonnull_mask = 1;
		input.pi_17e = 0;
		configure_post_aa63e_fixture(&input, &fake, 0, 0, 0, 0);
		input.post_aa63e_carriersearch_state_present = false;
		input.nested_carriersearch_count = NULL;
		check("missing carrier-search counter rejected",
		      ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
						   &platform_ops, &fake,
						   &input, &result), -EINVAL);
		check("missing carrier counter rejects before callbacks",
		      fake.event_count, 0);
	}
}

static void test_sub995ab_mutation_and_caller_join(void)
{
	struct fake_backend fake;
	struct ob_phy_a7089_txpwr_result result;
	int ret;

	ret = run_tail_case(&fake, 0, 0, 1, 2, 3, 4, 0, 0,
			    OB_A7089_MAC_ENABLED, true, &result);
	check("arg-zero caller tail return", ret, 0);
	check("arg-zero tail complete", result.tail_completed, 1);
	check("arg-zero nested byte 0 cleared", fake.nested_456[0], 0);
	check("arg-zero nested byte 1 cleared", fake.nested_456[1], 0);
	check("arg-zero nested byte 2 cleared", fake.nested_456[2], 0);
	check("arg-zero nested byte 3 cleared", fake.nested_456[3], 0);
	check("arg-zero mutation count", result.nested_456_mutations, 4);
	check("arg-zero sub99528 normal return zero", result.sub99528_return, 0);
	check("arg-zero helper caller reaches aa63e",
	      result.terminal_boundary, 0xaa63e);
	check("arg-zero MAC state preserved", result.mac_state,
	      OB_A7089_MAC_ENABLED);
	check("joined through second txpwrctrl logical events",
	      result.logical_event_count, 91);
	check("joined through second txpwrctrl callbacks",
	      result.callback_count, 117);
	check("second txpwrctrl has no per-core reads",
	      result.txpwrctrl_logical_event_count, 6);
	check("single persistent callback stream after extension",
	      fake.event_count, result.callback_count);

	ret = run_tail_case(&fake, 1, 0, 0, 0, 0, 0x44, 1, 0,
			    OB_A7089_MAC_ENABLED, true, &result);
	check("F8 vector caller return", ret, 0);
	check("F8 vector completes caller tail", result.tail_completed, 1);
	check("F8 byte0 mutation", fake.nested_456[0], 0xf8);
	check("F8 byte1 mutation", fake.nested_456[1], 0xf8);
	check("F8 byte2 mutation", fake.nested_456[2], 0xf8);
	check("F8 byte3 preserved at bound return", fake.nested_456[3], 0x44);
	check("F8 helper returns historical -2", result.sub99528_return, -2);
	check("F8 bound return skips MAC delegation",
	      result.mac_suspend_attempted, 0);
	check("F8 caller ignores child -2 and continues", result.terminal_boundary,
	      0xaa63e);

	ret = run_tail_case(&fake, 1, 0xc123, 1, 2, 3, 4, 0, 0,
			    OB_A7089_MAC_SUSPENDED, true, &result);
	check("C000 mask zero-vector caller return", ret, 0);
	check("C000 mask clears all four bytes", result.nested_456_mutations, 4);
	check("C000 mask preserves suspended MAC state", result.mac_state,
	      OB_A7089_MAC_SUSPENDED);

	ret = run_tail_case(&fake, 1, 0x4001, 1, 2, 3, 4, 1, 0,
			    OB_A7089_MAC_ENABLED, true, &result);
	check("4000 chanspec arm caller return", ret, 0);
	check("4000 chanspec skips child", result.sub99528_called, 0);
	check("4000 chanspec leaves bytes unchanged", result.nested_456_mutations, 0);
	check("4000 chanspec skips MAC", result.mac_suspend_attempted, 0);
}

static void test_sub99528_mac_boundary_and_failure_ownership(void)
{
	struct fake_backend fake;
	struct ob_phy_a7089_txpwr_result result;
	struct ob_phy_a7089_txpwr_input input;
	int ret;

	ret = run_tail_case(&fake, 0, 0, 1, 2, 3, 4, 1, 0,
			    OB_A7089_MAC_ENABLED, true, &result);
	check("changed nested state reaches unsupported child", ret, -EOPNOTSUPP);
	check("MAC arm stops at sub9949f", result.terminal_boundary, 0x99593);
	check("MAC arm status unsupported", result.status,
	      OB_A7089_TXPWR_UNSUPPORTED);
	check("successful delegated suspend recorded", result.mac_state,
	      OB_A7089_MAC_SUSPENDED);
	check("MAC suspension was attempted", result.mac_suspend_attempted, 1);
	check("MAC arm emits only suspend callback after prefix",
	      result.callback_count, 115);
	check("no fabricated MAC enable after unsupported child",
	      fake.events[fake.event_count - 1].site, 0x9958b);
	check("no caller txpwrctrl after unsupported child",
	      result.tail_callback_count, 1);

	ret = run_tail_case(&fake, 0, 0, 1, 2, 3, 4, 1, 0,
			    OB_A7089_MAC_SUSPENDED, true, &result);
	check("already-suspended state still delegates historical suspend",
	      result.mac_suspend_attempted, 1);
	check("already-suspended delegated path stops at child",
	      result.terminal_boundary, 0x99593);

	ret = run_tail_case(&fake, 0, 0, 1, 2, 3, 4, 1, 0,
			    OB_A7089_MAC_ENABLED, false, &result);
	check("missing suspend callback stops at callback site", ret,
	      -EOPNOTSUPP);
	check("missing suspend callback boundary", result.terminal_boundary,
	      0x9958b);
	check("missing suspend callback emits no tail callback",
	      result.tail_callback_count, 0);

	ret = run_tail_case(&fake, 0, 0, 1, 2, 3, 4, 1, 0,
			    OB_A7089_MAC_ENABLED, true, &result);
	check("MAC success leaves exactly one tail operation",
	      result.tail_callback_count, 1);

	/* Inject failure specifically at the first MAC callback, after the
	 * unchanged 114-callback predecessor. */
	{
		u8 indices_local[2] = {0x80, 0x7f};
		input = input_for(2, indices_local, true);
		seed(&fake);
		fake.nested_456[0] = 1;
		fake.nested_456[1] = 2;
		fake.nested_456[2] = 3;
		fake.nested_456[3] = 4;
		input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
		input.nested_txpwrctrl_values_length = 2;
		input.sub995ab_tail_present = true;
		input.pi_fa2_present = true;
		input.pi_fa2 = 0;
		input.pi_17e_present = true;
		input.pi_17e = 0;
		input.nested_456 = fake.nested_456;
		input.nested_456_length = 4;
		input.phy_context_31_present = true;
		input.phy_context_31 = 1;
		input.saved_pi_fa0_present = true;
		input.saved_pi_fa0 = 0;
		input.mac_state_present = true;
		input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
		fake.fail_at = 115;
		ret = ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
						    &platform_ops, &fake,
						    &input, &result);
		check("suspend callback failure propagated", ret, -EIO);
		check("suspend failure is fail-stop", fake.event_count, 115);
		check("suspend failure does not claim MAC state",
		      result.mac_state, OB_A7089_MAC_UNKNOWN);
		check("suspend failure source site", result.failed_site, 0x9958b);
		check("suspend failure does not call sub9949f",
		      result.terminal_boundary, 0x9958b);
	}

	ret = run_tail_case(&fake, 0, 0, 0, 0, 0, 0, 0, 1,
			    OB_A7089_MAC_ENABLED, true, &result);
	check("nonzero saved arg helper returns", ret, 0);
	check("saved arg reaches aa63e", result.terminal_boundary, 0xaa63e);
	check("saved arg one adds three RMW logical events",
	      result.logical_event_count, 92);
	check("saved arg one adds six callbacks", result.callback_count, 120);
	check("saved arg one remains in PI field", result.pi_fa0_after_txpwrctrl, 1);
}

static void test_sub995ab_fail_stop_after_normal_software_branch(void)
{
	unsigned int fail_at;

	for (fail_at = 115; fail_at <= 117; fail_at++) {
		struct fake_backend fake;
		struct ob_phy_a7089_txpwr_result result;
		int ret;
		/* Re-run with the failure injected at each newly added callback. */
		{
			u8 indices[2] = {0x80, 0x7f};
			struct ob_phy_a7089_txpwr_input input =
				input_for(2, indices, true);

			seed(&fake);
			fake.nested_456[0] = 1;
			fake.nested_456[1] = 2;
			fake.nested_456[2] = 3;
			fake.nested_456[3] = 4;
			fake.fail_at = (u16)fail_at;
			input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
			input.nested_txpwrctrl_values_length = 2;
			input.sub995ab_tail_present = true;
			input.pi_fa2_present = true;
			input.pi_fa2 = 0;
			input.pi_17e_present = true;
			input.pi_17e = 0;
			input.nested_456 = fake.nested_456;
			input.nested_456_length = 4;
			input.phy_context_31_present = true;
			input.phy_context_31 = 0;
			input.saved_pi_fa0_present = true;
			input.saved_pi_fa0 = 0;
			input.mac_state_present = true;
			input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
			ret = ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops,
							    &platform_ops, &fake,
							    &input, &result);
		}
		if (ret != -EIO || result.status != OB_A7089_TXPWR_FAILED_BACKEND ||
		    fake.event_count != fail_at || result.hardware_ready ||
		    result.failed_site != fake.events[fail_at - 1].site) {
			printf("FAIL: sub995ab tail fail-stop position %u ret=%d events=%u\n",
			       fail_at, ret, fake.event_count);
			failures++;
			return;
		}
	}
}

static const char *kind_name(enum fake_kind kind)
{
	switch (kind) {
	case FAKE_PHY_READ: return "PHY_READ";
	case FAKE_PHY_WRITE: return "PHY_WRITE";
	case FAKE_RADIO_READ: return "RADIO_READ";
	case FAKE_RADIO_WRITE: return "RADIO_WRITE";
	case FAKE_TABLE_READ: return "TABLE_READ";
	case FAKE_TABLE_WRITE: return "TABLE_WRITE";
	case FAKE_MAC_SUSPEND: return "MAC_SUSPEND";
	case FAKE_DELAY: return "DELAY";
	case FAKE_FGC: return "FGC";
	default: return "PREFIX";
	}
}

static const char *domain_name(enum fake_kind kind)
{
	switch (kind) {
	case FAKE_PHY_READ:
	case FAKE_PHY_WRITE: return "PHY";
	case FAKE_RADIO_READ:
	case FAKE_RADIO_WRITE: return "RADIO";
	case FAKE_TABLE_READ:
	case FAKE_TABLE_WRITE: return "TABLE";
	case FAKE_MAC_SUSPEND: return "PLATFORM";
	case FAKE_DELAY: return "DELAY";
	case FAKE_FGC: return "PLATFORM";
	default: return "UNKNOWN";
	}
}

static void print_events_json(const struct fake_backend *fake, u16 first)
{
	u16 i;
	putchar('[');
	for (i = first; i < fake->event_count; i++) {
		const struct fake_event *e = &fake->events[i];
		u8 b;
		if (i != first)
			putchar(',');
		printf("{\"kind\":\"%s\",\"domain\":\"%s\","
		       "\"site\":%u,\"reg\":%u,"
		       "\"table\":%u,\"offset\":%d,\"count\":%u,"
		       "\"width\":%u,\"value\":%u,\"payload\":[",
		       kind_name(e->kind), domain_name(e->kind), e->site, e->reg,
		       e->table, e->offset, e->count, e->width, e->value);
		for (b = 0; b < e->payload_length; b++) {
			if (b)
				putchar(',');
			printf("%u", e->payload[b]);
		}
		printf("]}");
	}
	puts("]");
}

static const char *status_name(enum ob_phy_a7089_txpwr_status status)
{
	switch (status) {
	case OB_A7089_TXPWR_SYNTHETIC_COMPLETE: return "complete";
	case OB_A7089_TXPWR_FAILED_BACKEND: return "failed_backend";
	case OB_A7089_TXPWR_UNSUPPORTED: return "unsupported";
	default: return "invalid";
	}
}

static const char *mac_state_name(enum ob_phy_a7089_mac_state state)
{
	switch (state) {
	case OB_A7089_MAC_ENABLED: return "enabled";
	case OB_A7089_MAC_SUSPENDED: return "suspended";
	default: return "unknown";
	}
}

static int tail_trace_mode(int argc, char **argv)
{
	u8 indices[2] = {0x80, 0x7f};
	struct fake_backend fake;
	struct ob_phy_a7089_txpwr_result result;
	struct ob_phy_a7089_txpwr_input input;
	int ret;

	/* --tail PI164 idx0 idx1 fa2 chanspec nested0..3 context31 savedfa0 mac
	 *       [initial PHY[0x0070] [class0 lookup [RADIO 0x090b]
	 *        [entry nested+0x32c [rxgainerr flag [PI+a7 [PI+a4]]]]]]
	 */
	if (argc < 14 || (argc > 22 && argc != 24 && argc != 33 &&
			  argc != 36))
		return 2;
	input = input_for((u32)strtoul(argv[2], NULL, 0), indices, true);
	seed(&fake);
	input.nested_power_indices = indices;
	input.nested_power_indices_length = 2;
	input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.sub995ab_tail_present = true;
	input.pi_fa2_present = true;
	input.pi_fa2 = (u8)strtoul(argv[5], NULL, 0);
	input.pi_17e_present = true;
	input.pi_17e = (u16)strtoul(argv[6], NULL, 0);
	input.nested_456 = fake.nested_456;
	input.nested_456_length = sizeof(fake.nested_456);
	input.nested_456[0] = (u8)strtoul(argv[7], NULL, 0);
	input.nested_456[1] = (u8)strtoul(argv[8], NULL, 0);
	input.nested_456[2] = (u8)strtoul(argv[9], NULL, 0);
	input.nested_456[3] = (u8)strtoul(argv[10], NULL, 0);
	input.phy_context_31_present = true;
	input.phy_context_31 = (u8)strtoul(argv[11], NULL, 0);
	input.saved_pi_fa0_present = true;
	input.saved_pi_fa0 = (u8)strtoul(argv[12], NULL, 0);
	input.mac_state_present = true;
	if (!strcmp(argv[13], "enabled"))
		input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
	else if (!strcmp(argv[13], "suspended"))
		input.mac_state_at_99528 = OB_A7089_MAC_SUSPENDED;
	else if (!strcmp(argv[13], "unknown"))
		input.mac_state_at_99528 = OB_A7089_MAC_UNKNOWN;
	else
		return 2;
	if (argc >= 15)
		fake.phy[0x0070] = (u16)strtoul(argv[14], NULL, 0);
	/* Optional host-only completion sample for rxcore_setstate RFSEQ polls. */
	if (argc == 22)
		fake.phy[0x0403] = (u16)strtoul(argv[21], NULL, 0);
	if (argc >= 16) {
		input.post_aa63e_lookup_nonnull_mask =
			(u8)strtoul(argv[15], NULL, 0);
		if (argc >= 17)
			fake.radio[0x090b] = (u16)strtoul(argv[16], NULL, 0);
		configure_post_aa63e_fixture(
			&input, &fake,
			argc >= 18 ? (u8)strtoul(argv[17], NULL, 0) : 0,
			argc >= 19 ? (u8)strtoul(argv[18], NULL, 0) : 0,
			argc >= 20 ? (u8)strtoul(argv[19], NULL, 0) : 0,
			argc >= 21 ? (u8)strtoul(argv[20], NULL, 0) : 0);
		if (argc == 33 || argc == 36) {
			u8 i;

			for (i = 0; i < 6; i++) {
				input.nested_candidate_counts_64a_through_64f[i] =
					(u8)strtoul(argv[21 + i], NULL, 0);
				input.nested_candidate_counts_6c2_through_6c7[i] =
					(u8)strtoul(argv[27 + i], NULL, 0);
			}
		}
		if (argc == 24 || argc == 36) {
			int base = argc == 24 ? 21 : 33;

			fake.nested_carriersearch_count =
				(u16)strtoul(argv[base], NULL, 0);
			input.post_aa63e_nested_902 =
				(u16)strtoul(argv[base + 1], NULL, 0);
			input.post_aa63e_nested_904 =
				(u16)strtoul(argv[base + 2], NULL, 0);
		}
	}
	ret = ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, &platform_ops,
					  &fake, &input, &result);
	(void)ret;
	printf("{\"events\":");
	print_events_json(&fake, (u16)result.tail_callback_start);
	printf(",\"status\":\"%s\",\"terminal_boundary\":%u,"
	       "\"nested_456\":[%u,%u,%u,%u],\"mac_state\":\"%s\","
	       "\"sub99528_return\":",
	       status_name(result.status), result.terminal_boundary,
	       result.nested_456_after[0], result.nested_456_after[1],
	       result.nested_456_after[2], result.nested_456_after[3],
	       mac_state_name(result.mac_state));
	if (result.sub99528_return_valid)
		printf("%d", result.sub99528_return);
	else
		printf("null");
	printf(",\"tail_logical_events\":%u,\"tail_callbacks\":%u,"
	       "\"logical_event_count\":%u,\"callback_count\":%u",
	       result.tail_logical_event_count, result.tail_callback_count,
	       result.logical_event_count, result.callback_count);
	if (input.post_aa63e_inputs_present)
		printf(",\"txpwrctrl_nonzero_argument\":%u,"
		       "\"txpwrctrl_nonzero_path\":%u,"
		       "\"txpwrctrl_nonzero_store\":%u,"
		       "\"txpwrctrl_core_updates\":%u,"
		       "\"pi_fa0_after_txpwrctrl\":%u,"
		       "\"post_aa63e_entered\":%u,"
		       "\"post_aa63e_lookup_nonnull_mask\":%u,"
		       "\"post_aa63e_radio_poll_selected\":%u,"
		       "\"post_aa63e_poll_satisfied\":%u,"
		       "\"post_aa63e_poll_exhausted\":%u,"
		       "\"post_aa63e_poll_attempts\":%u,"
		       "\"unsupported_site\":%u,"
		       "\"post_aa63e_nested_gate_nonzero\":%u,"
		       "\"post_aa63e_rxgainerr_completed\":%u,"
		       "\"post_aa63e_0400_selected\":%u,"
		       "\"post_aa63e_rxgainerr_band\":%u,"
		       "\"post_aa63e_rxgainerr_return\":%u,"
		       "\"post_aa63e_cores_completed\":%u,"
		       "\"post_aa63e_rxgainerr_values\":[%d,%d],"
		       "\"post_aa63e_sub8f086_returns\":[%u,%u],"
		       "\"post_aa63e_sub8f086_output\":["
	       "[%u,%u,%u,%u,%u,%u],[%u,%u,%u,%u,%u,%u]],"
		       "\"nested_gainerr_state_after\":[%u,%u,%u,%u,%u],"
		       "\"post_aa63e_carriersearch_entered\":%u,"
		       "\"post_aa63e_carriersearch_effects_selected\":%u,"
		       "\"post_aa63e_carriersearch_completed\":%u,"
		       "\"post_aa63e_carriersearch_count_after\":%u,"
		       "\"post_aa63e_classifier_mask\":%u",
		       result.txpwrctrl_nonzero_argument,
		       result.txpwrctrl_nonzero_path,
		       result.txpwrctrl_nonzero_store,
		       result.txpwrctrl_core_updates,
		       result.pi_fa0_after_txpwrctrl,
		       result.post_aa63e_entered,
		       result.post_aa63e_lookup_nonnull_mask,
		       result.post_aa63e_radio_poll_selected,
		       result.post_aa63e_poll_satisfied,
		       result.post_aa63e_poll_exhausted,
		       result.post_aa63e_poll_attempts,
		       result.unsupported_site,
		       result.post_aa63e_nested_gate_nonzero,
		       result.post_aa63e_rxgainerr_completed,
		       result.post_aa63e_0400_selected,
		       result.post_aa63e_rxgainerr_band,
		       result.post_aa63e_rxgainerr_return,
		       result.post_aa63e_cores_completed,
		       result.post_aa63e_rxgainerr_values[0],
		       result.post_aa63e_rxgainerr_values[1],
		       result.post_aa63e_sub8f086_returns[0],
		       result.post_aa63e_sub8f086_returns[1],
		       result.post_aa63e_sub8f086_output[0][0],
		       result.post_aa63e_sub8f086_output[0][1],
		       result.post_aa63e_sub8f086_output[0][2],
		       result.post_aa63e_sub8f086_output[0][3],
		       result.post_aa63e_sub8f086_output[0][4],
		       result.post_aa63e_sub8f086_output[0][5],
		       result.post_aa63e_sub8f086_output[1][0],
		       result.post_aa63e_sub8f086_output[1][1],
		       result.post_aa63e_sub8f086_output[1][2],
		       result.post_aa63e_sub8f086_output[1][3],
		       result.post_aa63e_sub8f086_output[1][4],
		       result.post_aa63e_sub8f086_output[1][5],
		       result.nested_gainerr_state_after[0],
		       result.nested_gainerr_state_after[1],
		       result.nested_gainerr_state_after[2],
		       result.nested_gainerr_state_after[3],
		       result.nested_gainerr_state_after[4],
		       result.post_aa63e_carriersearch_entered,
		       result.post_aa63e_carriersearch_effects_selected,
		       result.post_aa63e_carriersearch_completed,
		       result.post_aa63e_carriersearch_count_after,
		       result.post_aa63e_classifier_mask);
	puts("}");
	return 0;
}

static int trace_mode(int argc, char **argv)
{
	u8 indices[2];
	u32 pi164;
	struct fake_backend fake;
	struct ob_phy_a7089_txpwr_result result;
	struct ob_phy_a7089_txpwr_input input;
	int ret;
	if (argc != 5 && argc != 6 && argc != 8)
		return 2;
	pi164 = (u32)strtoul(argv[2], NULL, 0);
	indices[0] = (u8)strtoul(argv[3], NULL, 0);
	indices[1] = (u8)strtoul(argv[4], NULL, 0);
	input = input_for(pi164, indices, true);
	seed(&fake);
	input.nested_txpwrctrl_values = fake.nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	if (argc >= 6) {
		if (!strcmp(argv[5], "skip"))
			fake.phy[0x0070] = 0x1234;
		else if (strcmp(argv[5], "equal"))
			return 2;
	}
	if (argc == 8) {
		fake.phy[0x0640] = (u16)strtoul(argv[6], NULL, 0);
		fake.phy[0x0840] = (u16)strtoul(argv[7], NULL, 0);
	}
	ret = ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, NULL, &fake,
					  &input, &result);
	if (ret || result.status != OB_A7089_TXPWR_SYNTHETIC_COMPLETE)
		return 1;
	print_events_json(&fake, (u16)result.post_resetcca.callback_count);
	return 0;
}

/* U01..U20: the corrected wlc_phy_rxcore_setstate_acphy contract.
 *
 * The fixture always selects the historical mismatch so the setter is reached
 * and always uses the unchanged nested 4-byte vector, which keeps sub_99528's
 * own shared+0x31 gate out of the path. */
static int run_rxcore_branch(struct fake_backend *fake, u8 context31,
			     const struct ob_phy_a7089_platform_ops *pops,
			     u32 fail_at,
			     struct ob_phy_a7089_txpwr_result *result)
{
	u8 indices[2] = {0x80, 0x7f};
	struct ob_phy_a7089_txpwr_input input = input_for(2, indices, true);

	seed(fake);
	fake->fail_at = fail_at;
	/* Explicit synthetic completion sample for the two bounded RFSEQ polls and
	 * deterministic RX-core snapshots for the restore assertions. */
	fake->phy[0x0403] = 0x0000;
	fake->phy[0x0401] = 0x1234;
	fake->phy[0x0400] = 0x5678;
	input.nested_txpwrctrl_values = fake->nested_txpwrctrl_values;
	input.nested_txpwrctrl_values_length = 2;
	input.sub995ab_tail_present = true;
	input.pi_fa2_present = true;
	input.pi_fa2 = 0;
	input.pi_17e_present = true;
	input.pi_17e = 0;
	input.nested_456 = fake->nested_456;
	input.nested_456_length = sizeof(fake->nested_456);
	input.phy_context_31_present = true;
	input.phy_context_31 = context31;
	input.saved_pi_fa0_present = true;
	input.saved_pi_fa0 = 1;
	input.mac_state_present = true;
	input.mac_state_at_99528 = OB_A7089_MAC_ENABLED;
	input.post_aa63e_lookup_nonnull_mask = 1;
	configure_post_aa63e_fixture(&input, fake, 1, 0, 1, 1);
	return ob_phy_a7089_txpwr_test_only(&prefix_ops, &tx_ops, pops, fake,
					    &input, result);
}

static unsigned int count_kind(const struct fake_backend *fake,
			       enum fake_kind kind)
{
	unsigned int i, n = 0;

	for (i = 0; i < fake->event_count; i++)
		if (fake->events[i].kind == kind)
			n++;
	return n;
}

static unsigned int count_reg(const struct fake_backend *fake, u16 reg)
{
	unsigned int i, n = 0;

	for (i = 0; i < fake->event_count; i++)
		if ((fake->events[i].kind == FAKE_PHY_READ ||
		     fake->events[i].kind == FAKE_PHY_WRITE) &&
		    fake->events[i].reg == reg)
			n++;
	return n;
}

/* PHY accesses issued from inside wlc_phy_rxcore_setstate_acphy only. Later
 * helpers of the same diagnostic tail legitimately touch PHY[0x0400]. */
static unsigned int count_rxcore_phy(const struct fake_backend *fake)
{
	unsigned int i, n = 0;

	for (i = 0; i < fake->event_count; i++)
		if ((fake->events[i].kind == FAKE_PHY_READ ||
		     fake->events[i].kind == FAKE_PHY_WRITE) &&
		    fake->events[i].site >= 0x97d0fu &&
		    fake->events[i].site <= 0x97e22u)
			n++;
	return n;
}

static void test_rxcore_branch_contract(void)
{
	static const u32 sequence_sites[] = {
		0x97d13, 0x97d20, 0x97d30, 0x97d50, 0x97d6e, 0x97d85,
		0x97d99, 0x97db0, 0x97de4, 0x97dff, 0x97e10, 0x97e1d,
	};
	static struct fake_backend fake, second;
	struct ob_phy_a7089_txpwr_result result, other, other_b;
	struct ob_phy_a7089_platform_ops no_enable = platform_ops;
	u32 i, matched = 0;
	int ret, suspend_index, read_index, mod0160_index, enable_index;

	/* U01/U04/U05/U06 — shared+0x31 == 0 is software-only. */
	ret = run_rxcore_branch(&fake, 0, &platform_ops, 0, &result);
	check("U01 zero arm returns success", ret, 0);
	check("U01 zero arm reaches the historical caller return",
	      result.terminal_boundary, 0xaa781);
	check("U01 zero arm reports software-only",
	      result.post_aa63e_rxcore_software_only, 1);
	check("U01 zero arm does not run the MAC sequence",
	      result.post_aa63e_rxcore_mac_sequence, 0);
	check("U06 zero arm stores the shared+0xa7 byte",
	      result.post_aa63e_rxcore_a7_store, 1);
	check("U04 zero arm issues no RX-core PHY access",
	      count_rxcore_phy(&fake), 0);
	/* PHY[0x0400] is also touched later by the diagnostic @0xaa74d pair, so
	 * the setter's own access is counted by site range above. */
	check("U04 zero arm touches no PHY[0x0401]", count_reg(&fake, 0x0401), 0);
	check("U04 zero arm touches no PHY[0x0160]", count_reg(&fake, 0x0160), 0);
	check("U05 zero arm issues no MAC suspend",
	      count_kind(&fake, FAKE_MAC_SUSPEND), 0);
	check("U05 zero arm issues no MAC enable",
	      count_kind(&fake, FAKE_MAC_ENABLE), 0);
	check("U05 zero arm records no suspended MAC",
	      result.post_aa63e_rxcore_mac_suspended, 0);

	/* U02/U03/U08 — any nonzero value selects the hardware arm, in the exact
	 * recovered order: suspend, read 0401, read 0400, five RMWs, both RFSEQ
	 * children, three restores, enable. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops, 0, &result);
	check("U02 nonzero arm completes", ret, 0);
	check("U02 nonzero arm selects the MAC sequence",
	      result.post_aa63e_rxcore_mac_sequence, 1);
	check("U02 nonzero arm restores the MAC",
	      result.post_aa63e_rxcore_mac_restored, 1);
	check("U02 nonzero arm is not software-only",
	      result.post_aa63e_rxcore_software_only, 0);
	check("U02 nonzero arm allows no re-entry latch",
	      result.post_aa63e_rxcore_reentry_forbidden, 0);
	for (i = 0; i < fake.event_count && matched < 12u; i++) {
		if (fake.events[i].site == sequence_sites[matched])
			matched++;
	}
	check("U08 ordered operation trace matches the vendor ledger", matched,
	      12);
	check("U02 suspend precedes the first read",
	      find_event_site(&fake, 0x97d13, 0) <
	      find_event_site(&fake, 0x97d20, 0), 1);
	check("U02 enable follows the 0400 restore",
	      find_event_site(&fake, 0x97e1d, 0) >
	      find_event_site(&fake, 0x97e10, 0), 1);
	check("U02 saved 0401 snapshot is published",
	      result.post_aa63e_rxcore_saved_0401, 0x1234);
	check("U02 saved 0400 snapshot is published",
	      result.post_aa63e_rxcore_saved_0400, 0x5678);

	/* U03 — 0xff follows the same arm as 1. */
	ret = run_rxcore_branch(&fake, 0xff, &platform_ops, 0, &other);
	check("U03 0xff selects the MAC sequence",
	      other.post_aa63e_rxcore_mac_sequence, 1);
	check("U03 0xff matches the 0x01 callback total",
	      other.callback_count, result.callback_count);
	check("U03 0xff matches the 0x01 logical total",
	      other.logical_event_count, result.logical_event_count);

	/* U07 — without both MAC callbacks the arm stays fail-closed. */
	ret = run_rxcore_branch(&fake, 1, NULL, 0, &other);
	check("U07 no platform owner is refused", ret, -EOPNOTSUPP);
	check("U07 no platform owner is unsupported", other.status,
	      OB_A7089_TXPWR_UNSUPPORTED);
	check("U07 no platform owner stops at the suspend site",
	      other.unsupported_site, 0x97d13);
	check("U07 no platform owner issues no MAC callback",
	      count_kind(&fake, FAKE_MAC_SUSPEND), 0);
	check("U07 no platform owner issues no PHY sequence access",
	      count_reg(&fake, 0x0401), 0);
	no_enable.enable_mac = NULL;
	ret = run_rxcore_branch(&fake, 1, &no_enable, 0, &other);
	check("U07 missing enable callback is refused", ret, -EOPNOTSUPP);
	check("U07 missing enable stops at the enable site",
	      other.unsupported_site, 0x97e1d);
	check("U07 missing enable issues no suspend",
	      count_kind(&fake, FAKE_MAC_SUSPEND), 0);

	/* Locate the arm's callbacks once; the indices are derived, not assumed. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops, 0, &result);
	check("U08 probe run completes", ret, 0);
	suspend_index = find_event_site(&fake, 0x97d13, 0);
	read_index = find_event_site(&fake, 0x97d20, 0);
	mod0160_index = find_event_site(&fake, 0x97d50, 0);
	enable_index = find_event_site(&fake, 0x97e1d, 0);
	check("U08 arm callbacks are present",
	      suspend_index >= 0 && read_index > suspend_index &&
	      mod0160_index > read_index && enable_index > mod0160_index, 1);
	if (suspend_index < 0 || read_index < 0 || mod0160_index < 0 ||
	    enable_index < 0)
		return;

	/* U09 — a failing suspend stops before any dependent PHY operation. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops,
				(u32)suspend_index + 1u, &other);
	check("U09 suspend failure propagates", ret, -EIO);
	check("U09 suspend failure is fail-stop", fake.event_count,
	      (u32)suspend_index + 1u);
	check("U09 suspend failure site", other.failed_site, 0x97d13);
	check("U09 suspend failure publishes no snapshots",
	      other.post_aa63e_rxcore_saved_0401, 0);
	check("U09 suspend failure leaves no PHY access", count_reg(&fake, 0x0401), 0);
	check("U09 suspend failure records no suspended MAC",
	      other.post_aa63e_rxcore_mac_suspended, 0);

	/* U10 — a failing first read stops before every dependent write. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops,
				(u32)read_index + 1u, &other);
	check("U10 first read failure propagates", ret, -EIO);
	check("U10 first read failure is fail-stop", fake.event_count,
	      (u32)read_index + 1u);
	check("U10 first read failure site", other.failed_site, 0x97d20);
	check("U10 first read failure issues no 0160 write",
	      count_reg(&fake, 0x0160), 0);
	check("U10 first read failure keeps the MAC suspended flag",
	      other.post_aa63e_rxcore_mac_suspended, 1);

	/* U11/U17 — an intermediate failure reports the true state and forbids a
	 * retry; nothing is issued after the failing callback. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops,
				(u32)mod0160_index + 1u, &other);
	check("U11 intermediate failure propagates", ret, -EIO);
	check("U11 intermediate failure status", other.status,
	      OB_A7089_TXPWR_FAILED_BACKEND);
	check("U11 intermediate failure site", other.failed_site, 0x97d50);
	check("U11 intermediate failure forbids re-entry",
	      other.post_aa63e_rxcore_reentry_forbidden, 1);
	check("U11 intermediate failure records the suspended MAC",
	      other.post_aa63e_rxcore_mac_suspended, 1);
	check("U11 intermediate failure restores nothing",
	      other.post_aa63e_rxcore_mac_restored, 0);
	check("U11 intermediate failure issues no enable callback",
	      count_kind(&fake, FAKE_MAC_ENABLE), 0);
	check("U17 intermediate failure issues no retry", fake.event_count,
	      (u32)mod0160_index + 1u);

	/* U12 — a failing restore/enable is never reported as success. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops,
				(u32)enable_index + 1u, &other);
	check("U12 enable failure propagates", ret, -EIO);
	check("U12 enable failure site", other.failed_site, 0x97e1d);
	check("U12 enable failure status", other.status,
	      OB_A7089_TXPWR_FAILED_BACKEND);
	check("U12 enable failure is not reported as restored",
	      other.post_aa63e_rxcore_mac_restored, 0);
	check("U12 enable failure forbids re-entry",
	      other.post_aa63e_rxcore_reentry_forbidden, 1);
	check("U12 enable failure publishes no readiness",
	      other.hardware_ready, 0);

	/* U16 — a device loss recognised at a mid-sequence read stops all further
	 * PHY MMIO for that device. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops,
				(u32)mod0160_index + 2u, &other);
	check("U16 device loss during the sequence propagates", ret, -EIO);
	check("U16 device loss stops all further callbacks", fake.event_count,
	      (u32)mod0160_index + 2u);

	/* U18 — device A failure does not alter device B. */
	ret = run_rxcore_branch(&fake, 1, &platform_ops,
				(u32)mod0160_index + 1u, &other);
	check("U18 device A fails", ret, -EIO);
	ret = run_rxcore_branch(&second, 1, &platform_ops, 0, &other_b);
	check("U18 device B completes", ret, 0);
	check("U18 device B selects its own MAC sequence",
	      other_b.post_aa63e_rxcore_mac_sequence, 1);
	check("U18 device B restores its own MAC",
	      other_b.post_aa63e_rxcore_mac_restored, 1);
	check("U18 device B keeps its own callback count",
	      other_b.callback_count, result.callback_count);

	/* U13 — the matching-state diagnostic path still skips the setter, and
	 * U19 — no path publishes calibration completion or hardware readiness. */
	check("U19 nonzero arm publishes no readiness", result.hardware_ready, 0);
	check("U19 failing arm publishes no readiness", other.hardware_ready, 0);
	check("U19 status is never the invalid sentinel", result.status !=
	      OB_A7089_TXPWR_INVALID, 1);
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--trace"))
		return trace_mode(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "--tail"))
		return tail_trace_mode(argc, argv);
	test_selected_post_resetcca_and_two_onehot_calls();
	test_predicate_and_prefix_skip_are_independent();
	test_invalid_inputs_reject_before_callbacks();
	test_txpwrctrl_non_equal_branch_and_tssi_thresholds();
	test_txpwrctrl_runtime_samples_update_nested_bytes();
	test_selected_txpwrctrl_skip_arm_join_counts();
	test_callback_failure_sweep();
	test_nonzero_txpwrctrl_and_aa63e_poll();
	test_rxcore_rfseq_20001_read_exhaustion();
	test_rxcore_branch_contract();
	test_carriersearch_arg0_selected_return();
	test_sub995ab_mutation_and_caller_join();
	test_sub99528_mac_boundary_and_failure_ownership();
	test_sub995ab_fail_stop_after_normal_software_branch();
	if (failures) {
		printf("ob_phy_a7089_txpwr_test: %u failure(s)\n", failures);
		return 1;
	}
	puts("ob_phy_a7089_txpwr_test: PASS");
	return 0;
}

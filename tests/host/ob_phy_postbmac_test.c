/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>

#include "../../src/ob_radio_postbmac.h"

enum fake_access {
	FAKE_SELECT,
	FAKE_READ_DATA,
	FAKE_WRITE_DATA,
};

struct fake_event {
	enum fake_access access;
	u16 value;
};

struct fake_marker {
	enum ob_radio_postbmac_marker marker;
	u16 sample;
	u8 low4;
	bool ja_gt1;
	enum ob_radio_postbmac_stage stage;
	int ret;
	u32 old_value;
	u32 write_value;
	u32 readback;
};

struct fake_cc_event {
	bool write;
	u16 reg;
	u32 value;
};

struct fake_window {
	struct fake_event events[8];
	unsigned int event_count;
	unsigned int fail_event;
	unsigned int lost_check;
	unsigned int lost_checks;
	u16 read_values[2];
	unsigned int read_count;
	bool ready;
	bool lost;
	bool cc_ready;
	struct fake_cc_event cc_events[12];
	unsigned int cc_event_count;
	u32 cc_read_values[6];
	unsigned int cc_read_count;
	unsigned int cc_lost_after_write;
	struct fake_marker markers[12];
	unsigned int marker_count;
};

static int failures;

static void check(const char *name, bool got, bool expected)
{
	if (got != expected) {
		printf("FAIL: %s got %u expected %u\n", name,
		       got ? 1u : 0u, expected ? 1u : 0u);
		failures++;
	}
}

static void check_u32(const char *name, unsigned int got,
		      unsigned int expected)
{
	if (got != expected) {
		printf("FAIL: %s got %u expected %u\n", name, got, expected);
		failures++;
	}
}

static int fake_record(struct fake_window *fake, enum fake_access access,
		       u16 value)
{
	unsigned int index = fake->event_count;

	if (index >= sizeof(fake->events) / sizeof(fake->events[0]))
		return -12;
	fake->events[index] = (struct fake_event){ access, value };
	fake->event_count++;
	return fake->fail_event == fake->event_count ? -5 : 0;
}

static bool fake_ready(void *ctx)
{
	return ((struct fake_window *)ctx)->ready;
}

static bool fake_lost(void *ctx)
{
	struct fake_window *fake = ctx;

	fake->lost_checks++;
	if (fake->lost_check && fake->lost_checks == fake->lost_check)
		fake->lost = true;
	return fake->lost;
}

static int fake_select(void *ctx, u16 selector)
{
	return fake_record(ctx, FAKE_SELECT, selector);
}

static int fake_read_data(void *ctx, u16 *value)
{
	struct fake_window *fake = ctx;
	int ret;

	ret = fake_record(fake, FAKE_READ_DATA, 0);
	if (ret)
		return ret;
	if (fake->read_count >= 2)
		return -22;
	*value = fake->read_values[fake->read_count++];
	return 0;
}

static int fake_write_data(void *ctx, u16 value)
{
	return fake_record(ctx, FAKE_WRITE_DATA, value);
}

static const struct ob_phy_window_ops fake_window_ops = {
	.ready = fake_ready,
	.device_lost = fake_lost,
	.select16 = fake_select,
	.read_data16 = fake_read_data,
	.write_data16 = fake_write_data,
};

static bool fake_cc_ready(void *ctx)
{
	return ((struct fake_window *)ctx)->cc_ready;
}

static bool fake_cc_lost(void *ctx)
{
	return ((struct fake_window *)ctx)->lost;
}

static u32 fake_cc_read32(void *ctx, u16 reg)
{
	struct fake_window *fake = ctx;
	unsigned int index = fake->cc_event_count;
	u32 value;

	if (index >= sizeof(fake->cc_events) / sizeof(fake->cc_events[0])) {
		failures++;
		return 0xffffffffu;
	}
	if (fake->cc_read_count >= sizeof(fake->cc_read_values) /
						 sizeof(fake->cc_read_values[0]))
		value = 0xffffffffu;
	else
		value = fake->cc_read_values[fake->cc_read_count++];
	fake->cc_events[index] = (struct fake_cc_event){
		.write = false, .reg = reg, .value = value,
	};
	fake->cc_event_count++;
	return value;
}

static void fake_cc_write32(void *ctx, u16 reg, u32 value)
{
	struct fake_window *fake = ctx;
	unsigned int index = fake->cc_event_count;

	if (index >= sizeof(fake->cc_events) / sizeof(fake->cc_events[0])) {
		failures++;
		return;
	}
	fake->cc_events[index] = (struct fake_cc_event){
		.write = true, .reg = reg, .value = value,
	};
	fake->cc_event_count++;
	if (fake->cc_lost_after_write &&
	    fake->cc_event_count == fake->cc_lost_after_write)
		fake->lost = true;
}

static const struct ob_radio_postbmac_cc_ops fake_cc_ops = {
	.ready = fake_cc_ready,
	.device_lost = fake_cc_lost,
	.read32 = fake_cc_read32,
	.write32 = fake_cc_write32,
};

static void fake_marker(void *ctx, enum ob_radio_postbmac_marker marker,
			u16 sample, u8 low4, bool ja_gt1,
			enum ob_radio_postbmac_stage stage, int ret,
			u32 old_value, u32 write_value, u32 readback)
{
	struct fake_window *fake = ctx;
	unsigned int index = fake->marker_count;

	if (index >= sizeof(fake->markers) / sizeof(fake->markers[0])) {
		failures++;
		return;
	}
	fake->markers[index] = (struct fake_marker){
		.marker = marker,
		.sample = sample,
		.low4 = low4,
		.ja_gt1 = ja_gt1,
		.stage = stage,
		.ret = ret,
		.old_value = old_value,
		.write_value = write_value,
		.readback = readback,
	};
	fake->marker_count++;
}

static const struct ob_radio_postbmac_ops fake_ops = {
	.window = &fake_window_ops,
	.cc = &fake_cc_ops,
	.marker = fake_marker,
};

static struct ob_radio_postbmac_gate valid_gate(void)
{
	return (struct ob_radio_postbmac_gate){
		.mode_selected = true,
		.identity_valid = true,
		.chip_id = 0x4352,
		.chip_rev = 3,
		.d11_core_rev = 42,
		.phy_type = 0x0b,
		.phy_revision = 1,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_revision_class = 0,
		.d11_post_ucode_ready = true,
		.acphy_start_ready = true,
		.radio_access_initialized = true,
		.d11_core_enabled = true,
		.chanspec_valid = true,
		.chanspec = 0x1001,
		.phase2_success = true,
		.postphase2_success = true,
		.otp8e2_observed = true,
		.otp8e2_word = 0x0600,
		.otp8e2_value = 0x06,
	};
}

static struct fake_window valid_fake(u16 old_value, u16 sample)
{
	struct fake_window fake = {
		.read_values = { old_value, sample },
		.ready = true,
		.cc_ready = true,
		.cc_read_values = {
			0x12345678u, 0x00000000u,
			0xa5fabcdeu, 0x00300000u,
			0x89abcdefu, 0x89ab0000u,
		},
	};

	return fake;
}

static int run_sequence(struct fake_window *fake,
			const struct ob_radio_postbmac_gate *gate,
			struct ob_radio_postbmac_result *result)
{
	return ob_radio_postbmac_run(&fake_ops, fake, gate, result);
}

static void check_success_markers(const struct fake_window *fake,
				  u16 sample, u8 low4, bool ja_gt1)
{
	static const enum ob_radio_postbmac_marker expected[] = {
		OB_RADIO_POSTBMAC_MARK_CP0_ENTRY,
		OB_RADIO_POSTBMAC_MARK_CP1_PHY1B0_RMW_DONE,
		OB_RADIO_POSTBMAC_MARK_CP2_PHY0_SAMPLE,
		OB_RADIO_POSTBMAC_MARK_CP3_BRANCH_GATE,
		OB_RADIO_POSTBMAC_MARK_CP4_PMU_0658_DONE,
		OB_RADIO_POSTBMAC_MARK_CP5_PMU_065C_DONE,
		OB_RADIO_POSTBMAC_MARK_CP6_GPIO_006C_DONE,
		OB_RADIO_POSTBMAC_MARK_PASS,
		OB_RADIO_POSTBMAC_MARK_STOP,
	};
	unsigned int i;

	check_u32("success marker count", fake->marker_count,
		  sizeof(expected) / sizeof(expected[0]));
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
		check_u32("success marker order", fake->markers[i].marker,
			  expected[i]);
	check_u32("sample marker raw value", fake->markers[2].sample, sample);
	check_u32("sample marker low nibble", fake->markers[2].low4, low4);
	check("sample marker predicate", fake->markers[2].ja_gt1, ja_gt1);
	check_u32("PMU 0x658 old marker", fake->markers[4].old_value,
		  0x12345678u);
	check_u32("PMU 0x658 write marker", fake->markers[4].write_value, 0);
	check_u32("PMU 0x658 readback marker", fake->markers[4].readback, 0);
	check_u32("PMU 0x65c write formula",
		  fake->markers[5].write_value,
		  (0xa5fabcdeu & ~0x01f00000u) | 0x00300000u);
	check_u32("GPIO 0x6c write formula",
		  fake->markers[6].write_value, 0x89ab0000u);
}

static void check_cc_trace(const struct fake_window *fake)
{
	static const u16 regs[] = {
		0x0658, 0x0658, 0x0658,
		0x065c, 0x065c, 0x065c,
		0x006c, 0x006c, 0x006c,
	};
	static const bool writes[] = {
		false, true, false, false, true, false, false, true, false,
	};
	unsigned int i;
	u32 expected_65c = (0xa5fabcdeu & ~0x01f00000u) | 0x00300000u;

	check_u32("ChipCommon logical access callback count",
		  fake->cc_event_count, 9);
	for (i = 0; i < sizeof(regs) / sizeof(regs[0]) &&
	     i < fake->cc_event_count; i++) {
		check_u32("ChipCommon access register", fake->cc_events[i].reg,
			  regs[i]);
		check("ChipCommon read/write order", fake->cc_events[i].write,
		      writes[i]);
	}
	check_u32("PMU index full write", fake->cc_events[1].value, 0);
	check_u32("PMU control RMW write", fake->cc_events[4].value,
		  expected_65c);
	check_u32("GPIO mask RMW write", fake->cc_events[7].value,
		  0x89ab0000u);
}

static void test_exact_window_trace_and_rmw(void)
{
	static const u16 old_values[] = {
		0x0000, 0x7fff, 0x8000, 0xffff, 0x25a3, 0x55aa,
	};
	unsigned int i;

	for (i = 0; i < sizeof(old_values) / sizeof(old_values[0]); i++) {
		struct fake_window fake = valid_fake(old_values[i], 0x0001);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;
		u16 expected_write = (u16)(old_values[i] | 0x8000u);
		int ret = run_sequence(&fake, &gate, &result);

		check_u32("success return", (unsigned int)ret, 0);
		check_u32("success PHY access count", fake.event_count, 5);
		check_u32("selector one", fake.events[0].value, 0x01b0);
		check_u32("RMW data read", fake.events[1].access, FAKE_READ_DATA);
		check_u32("RMW write value", fake.events[2].value, expected_write);
		check_u32("selector zero", fake.events[3].value, 0x0000);
		check_u32("fresh data read", fake.events[4].access,
			  FAKE_READ_DATA);
		check("bit 15 set", !!(fake.events[2].value & 0x8000u), true);
		check_u32("other bits preserved",
			  fake.events[2].value & 0x7fffu,
			  old_values[i] & 0x7fffu);
		check("sequence success", result.success, true);
		check_u32("logical operation count", result.logical_ops, 5);
		check_u32("window callback count", result.window_callbacks, 5);
		check_u32("ChipCommon callback count", result.cc_callbacks, 9);
		check("selected historical branch executed",
		      result.historical_branch_executed, true);
		check_cc_trace(&fake);
		check_success_markers(&fake, 0x0001, 1, false);
	}
}

static void test_dynamic_sample_selects_only_accepted_branch(void)
{
	static const struct {
		u16 sample;
		u8 low4;
		bool ja_gt1;
	} cases[] = {
		{ 0x0000, 0x0, false },
		{ 0x0001, 0x1, false },
		{ 0x0002, 0x2, true },
		{ 0x000f, 0xf, true },
		{ 0x1234, 0x4, true },
		{ 0xffff, 0xf, true },
	};
	unsigned int i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct fake_window fake = valid_fake(0x3141, cases[i].sample);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;
		int ret = run_sequence(&fake, &gate, &result);

		check_u32("dynamic low4", result.low4, cases[i].low4);
		check("dynamic unsigned predicate", result.ja_gt1,
		      cases[i].ja_gt1);
		if (cases[i].low4 == 1) {
			check_u32("selected dynamic return", (unsigned int)ret, 0);
			check_u32("selected PHY callback count", fake.event_count, 5);
			check("historical branch executed",
			      result.historical_branch_executed, true);
			check_cc_trace(&fake);
			check_success_markers(&fake, cases[i].sample,
					      cases[i].low4, cases[i].ja_gt1);
		} else {
			check("unselected dynamic branch stops", ret != 0, true);
			check_u32("unselected branch has no ChipCommon access",
				  fake.cc_event_count, 0);
			check("unselected branch not executed",
			      result.historical_branch_executed, false);
			check_u32("unselected branch failure stage",
				  result.failure_stage,
				  OB_RADIO_POSTBMAC_STAGE_HISTORICAL_BRANCH);
		}
	}
}

static void test_each_callback_failure_stops(void)
{
	static const enum ob_radio_postbmac_stage stages[] = {
		OB_RADIO_POSTBMAC_STAGE_OP1_SELECTOR_01B0,
		OB_RADIO_POSTBMAC_STAGE_OP1_DATA_READ,
		OB_RADIO_POSTBMAC_STAGE_OP1_DATA_WRITE,
		OB_RADIO_POSTBMAC_STAGE_OP2_SELECTOR_0000,
		OB_RADIO_POSTBMAC_STAGE_OP2_DATA_READ,
	};
	unsigned int i;

	for (i = 1; i <= 5; i++) {
		struct fake_window fake = valid_fake(0x1234, 0x5678);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;
		unsigned int j, fail_count = 0, stop_count = 0, pass_count = 0;
		int ret;

		fake.fail_event = i;
		ret = run_sequence(&fake, &gate, &result);
		check("callback failure returned", ret != 0, true);
		check_u32("first callback failure stops trace", fake.event_count,
			  i);
		check_u32("callback failure stage", result.failure_stage,
			  stages[i - 1]);
		for (j = 0; j < fake.marker_count; j++) {
			fail_count += fake.markers[j].marker ==
				OB_RADIO_POSTBMAC_MARK_FAIL;
			stop_count += fake.markers[j].marker ==
				OB_RADIO_POSTBMAC_MARK_STOP;
			pass_count += fake.markers[j].marker ==
				OB_RADIO_POSTBMAC_MARK_PASS;
		}
		check_u32("one failure marker", fail_count, 1);
		check_u32("one failure stop marker", stop_count, 1);
		check_u32("no pass after failure", pass_count, 0);
	}
}

static void test_loss_checks_stop_without_sentinel_policy(void)
{
	static const unsigned int loss_checks[] = { 1, 5, 6, 7, 8, 11, 12 };
	unsigned int i;

	for (i = 0; i < sizeof(loss_checks) / sizeof(loss_checks[0]); i++) {
		struct fake_window fake = valid_fake(0xffff, 0x0001);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;
		unsigned int j, pass_count = 0;
		int ret;

		fake.lost_check = loss_checks[i];
		ret = run_sequence(&fake, &gate, &result);
		check("loss fails sequence", ret != 0, true);
		check("loss failure stage", result.failure_stage ==
		      OB_RADIO_POSTBMAC_STAGE_DEVICE_LOST, true);
		check("loss is latched", fake.lost, true);
		for (j = 0; j < fake.marker_count; j++)
			pass_count += fake.markers[j].marker ==
				OB_RADIO_POSTBMAC_MARK_PASS;
		check_u32("loss has no pass", pass_count, 0);
		check("loss has fail and stop", fake.marker_count >= 2, true);
	}

	{
		struct fake_window fake = valid_fake(0xffff, 0x0001);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;

		check_u32("all ones PHY RMW input accepted", (unsigned int)run_sequence(
			  &fake, &gate, &result), 0);
		check_u32("selected sample low4", result.low4, 1);
		check("selected sample passes with live latch", result.success,
		      true);
	}
}

static void test_chipcommon_failures_are_terminal(void)
{
	static const unsigned int event_counts[] = { 1, 3, 4, 6, 7, 9 };
	static const enum ob_radio_postbmac_stage stages[] = {
		OB_RADIO_POSTBMAC_STAGE_PMU_0658_READ,
		OB_RADIO_POSTBMAC_STAGE_PMU_0658_READBACK,
		OB_RADIO_POSTBMAC_STAGE_PMU_065C_READ,
		OB_RADIO_POSTBMAC_STAGE_PMU_065C_READBACK,
		OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READ,
		OB_RADIO_POSTBMAC_STAGE_GPIO_006C_READBACK,
	};
	unsigned int i;

	for (i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
		struct fake_window fake = valid_fake(0x0000, 0x0001);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;

		fake.cc_read_values[i] = 0xffffffffu;
		check("all ones ChipCommon read fails closed",
		      run_sequence(&fake, &gate, &result) != 0, true);
		check_u32("all ones read stops at access",
			  fake.cc_event_count, event_counts[i]);
		check_u32("all ones read failure stage", result.failure_stage,
			  stages[i]);
		check("all ones read has no PASS", result.success, false);
	}

	{
		struct fake_window fake = valid_fake(0x0000, 0x0001);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;

		fake.cc_ready = false;
		check("missing ChipCommon handle fails closed",
		      run_sequence(&fake, &gate, &result) != 0, true);
		check_u32("missing ChipCommon handle has no CC access",
			  fake.cc_event_count, 0);
		check_u32("missing ChipCommon failure stage",
			  result.failure_stage,
			  OB_RADIO_POSTBMAC_STAGE_CHIPCOMMON_GATE);
	}

	{
		struct fake_window fake = valid_fake(0x0000, 0x0001);
		struct ob_radio_postbmac_gate gate = valid_gate();
		struct ob_radio_postbmac_result result;

		fake.cc_lost_after_write = 2;
		check("device loss after PMU write stops",
		      run_sequence(&fake, &gate, &result) != 0, true);
		check_u32("device loss stops after write callback",
			  fake.cc_event_count, 2);
		check_u32("device loss at PMU write stage",
			  result.failure_stage,
			  OB_RADIO_POSTBMAC_STAGE_PMU_0658_WRITE);
	}
}

static void test_same_insertion_and_target_gate(void)
{
	struct ob_radio_postbmac_gate gate = valid_gate();
	struct ob_radio_postbmac_result result;
	struct fake_window fake;
	unsigned int i;

	for (i = 0; i < 6; i++) {
		gate = valid_gate();
		fake = valid_fake(0x0000, 0x0000);
		switch (i) {
		case 0: gate.mode_selected = false; break;
		case 1: gate.phase2_success = false; break;
		case 2: gate.postphase2_success = false; break;
		case 3: gate.chip_id = 0x4353; break;
		case 4: fake.lost = true; break;
		case 5: gate.otp8e2_value = 0x05; break;
		}
		check("invalid entry gate rejected", run_sequence(&fake, &gate,
		      &result) != 0, true);
		check_u32("invalid gate has no PHY callback", fake.event_count, 0);
		check("invalid gate does not emit CP0",
		      fake.marker_count > 0 && fake.markers[0].marker ==
		      OB_RADIO_POSTBMAC_MARK_CP0_ENTRY, false);
		check("invalid gate does not pass", result.success, false);
	}

	gate = valid_gate();
	fake = valid_fake(0x7fff, 0x0001);
	check_u32("valid same-insertion gate", (unsigned int)run_sequence(
		  &fake, &gate, &result), 0);
	check_u32("valid gate PHY callback count", fake.event_count, 5);
	check_cc_trace(&fake);
	check("valid gate begins at CP0", fake.markers[0].marker ==
	      OB_RADIO_POSTBMAC_MARK_CP0_ENTRY, true);
}

int main(void)
{
	test_exact_window_trace_and_rmw();
	test_dynamic_sample_selects_only_accepted_branch();
	test_each_callback_failure_stops();
	test_loss_checks_stop_without_sentinel_policy();
	test_chipcommon_failures_are_terminal();
	test_same_insertion_and_target_gate();
	if (failures) {
		printf("ob_phy_postbmac_test: %d failures\n", failures);
		return 1;
	}
	puts("ob_phy_postbmac_test: PASS");
	return 0;
}

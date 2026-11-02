#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/ob_phy_post_lpf.h"

#define FAKE_MAX_REGS 160
#define FAKE_MAX_EVENTS 4000

struct fake_reg {
	enum ob_phy_post_lpf_domain domain;
	u16 reg;
	u16 value;
};

struct fake_event {
	char kind;
	enum ob_phy_post_lpf_domain domain;
	u8 logical_event;
	u32 site;
	u16 reg;
	u16 value;
	u8 width_bits;
};

struct fake_backend {
	struct fake_reg regs[FAKE_MAX_REGS];
	unsigned int reg_count;
	struct fake_event events[FAKE_MAX_EVENTS];
	unsigned int count;
	unsigned int fail_at;
};

static int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static const char *domain_name(enum ob_phy_post_lpf_domain domain)
{
	switch (domain) {
	case OB_PHY_POST_LPF_PHY: return "PHY";
	case OB_PHY_POST_LPF_RADIO: return "RADIO";
	case OB_PHY_POST_LPF_CONTROL: return "CONTROL";
	default: return "INVALID";
	}
}

static u16 initial_value(enum ob_phy_post_lpf_domain domain, u16 reg)
{
	if (domain == OB_PHY_POST_LPF_PHY && reg == 0x019e)
		return 0xa540;
	if (domain == OB_PHY_POST_LPF_PHY && reg == 0x0140)
		return 0x1237;
	if (domain == OB_PHY_POST_LPF_PHY && reg == 0x0001)
		return 0x5101;
	if (domain == OB_PHY_POST_LPF_PHY && reg == 0x173a)
		return 0x0201;
	if (domain == OB_PHY_POST_LPF_PHY && reg == 0x1725)
		return 0x0005;
	return (u16)(((u32)reg * 37u) ^
		     (domain == OB_PHY_POST_LPF_RADIO ? 0x5a5au : 0x1357u));
}

static struct fake_reg *fake_reg_find(struct fake_backend *fake,
				      enum ob_phy_post_lpf_domain domain,
				      u16 reg)
{
	unsigned int i;

	for (i = 0; i < fake->reg_count; i++)
		if (fake->regs[i].domain == domain && fake->regs[i].reg == reg)
			return &fake->regs[i];
	if (fake->reg_count >= FAKE_MAX_REGS)
		return NULL;
	fake->regs[fake->reg_count].domain = domain;
	fake->regs[fake->reg_count].reg = reg;
	fake->regs[fake->reg_count].value = initial_value(domain, reg);
	return &fake->regs[fake->reg_count++];
}

static int fake_record(struct fake_backend *fake, char kind,
		       u8 logical_event, u32 site,
		       enum ob_phy_post_lpf_domain domain,
		       u16 reg, u16 value, u8 width_bits)
{
	if (fake->count >= FAKE_MAX_EVENTS)
		return -12;
	fake->events[fake->count++] = (struct fake_event) {
		.kind = kind, .domain = domain, .logical_event = logical_event,
		.site = site, .reg = reg, .value = value,
		.width_bits = width_bits,
	};
	return fake->fail_at == fake->count ? -5 : 0;
}

static int fake_read16(void *ctx, u8 logical_event, u32 site,
		       enum ob_phy_post_lpf_domain domain, u16 reg, u16 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	u16 sampled;
	int ret;

	if (!value || domain == OB_PHY_POST_LPF_CONTROL)
		return -22;
	entry = fake_reg_find(fake, domain, reg);
	if (!entry)
		return -12;
	sampled = entry->value;
	/* Same explicit volatile LPF samples used by its independent oracle. */
	if (logical_event == 0 && domain == OB_PHY_POST_LPF_PHY) {
		if (reg == 0x173a)
			sampled = 0x0201;
		else if (reg == 0x1725)
			sampled = 0x0005;
	}
	ret = fake_record(fake, 'R', logical_event, site, domain, reg,
			  sampled, 16);
	if (ret)
		return ret;
	*value = sampled;
	return 0;
}

static int fake_write16(void *ctx, u8 logical_event, u32 site,
			enum ob_phy_post_lpf_domain domain, u16 reg, u16 value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	int ret;

	if (domain == OB_PHY_POST_LPF_CONTROL)
		return -22;
	ret = fake_record(fake, 'W', logical_event, site, domain, reg,
			  value, 16);
	if (ret)
		return ret;
	entry = fake_reg_find(fake, domain, reg);
	if (!entry)
		return -12;
	entry->value = value;
	return 0;
}

static int fake_delay_us(void *ctx, u8 logical_event, u32 site, u16 usec)
{
	return fake_record(ctx, 'D', logical_event, site,
			   OB_PHY_POST_LPF_CONTROL, 0, usec, 0);
}

static int fake_phyclk_fgc(void *ctx, u8 logical_event, u32 site, bool enabled)
{
	return fake_record(ctx, 'F', logical_event, site,
			   OB_PHY_POST_LPF_CONTROL, 0, enabled ? 1 : 0, 0);
}

static const struct ob_phy_post_lpf_ops fake_ops = {
	.read16 = fake_read16,
	.write16 = fake_write16,
	.delay_us = fake_delay_us,
	.phyclk_fgc = fake_phyclk_fgc,
};

static struct ob_phy_post_lpf_fixture fixture(void)
{
	return (struct ob_phy_post_lpf_fixture) {
		.explicitly_synthetic = true,
		.chip_id = 0x4352,
		.chip_revision = 3,
		.phy_revision = 0x0a,
		.radio_id = 0x2069,
		.radio_revision = 4,
		.radio_class = 0,
		.requested_chanspec = 0x1001,
		.lookup_key = 1,
		.row_index = 63,
		.lookup_matched = true,
		.pi_16e_class = 0,
		.pi_164_phy_revision = 0x0a,
		.pi_168_radio_class = 2,
		.nested_912 = 0,
	};
}

static struct ob_phy_post_lpf_entry_state entry_state(void)
{
	return (struct ob_phy_post_lpf_entry_state) {
		.nested_32c = 1, .nested_32e = 0xff, .nested_912 = 0,
	};
}

static int run(struct fake_backend *fake,
	       const struct ob_phy_post_lpf_fixture *scenario,
	       struct ob_phy_post_lpf_result *result)
{
	struct ob_phy_post_lpf_entry_state state = entry_state();

	return ob_phy_acphy_a7089_key1_row63_test_only(
		&fake_ops, fake, scenario, &state, result);
}

static void fake_init(struct fake_backend *fake)
{
	memset(fake, 0, sizeof(*fake));
}

static void test_selected_vector_counts_and_boundaries(void)
{
	struct fake_backend fake;
	struct ob_phy_post_lpf_result result = { 0 };
	struct ob_phy_post_lpf_fixture scenario = fixture();
	unsigned int i;
	int ret;

	fake_init(&fake);
	ret = run(&fake, &scenario, &result);
	check("selected fixture succeeds", ret, 0);
	check("selected fixture logical count", result.logical_event_count, 88);
	check("selected fixture callback count", result.callback_count, 109);
	check("selected fixture completion kind",
	      result.kind, OB_PHY_POST_LPF_SELECTED_FIXTURE_COMPLETE);
	check("selected fixture does not establish readiness",
	      result.hardware_ready, false);
	check("first selected operation", fake.events[0].site, 0xa7262);
	check("first selected operation register", fake.events[0].reg, 0x019e);
	check("last selected logical event site", fake.events[fake.count - 1].site,
	      0xaa404);
	check("last selected operation register", fake.events[fake.count - 1].reg,
	      0x019e);
	check("last selected operation kind", fake.events[fake.count - 1].kind,
	      'W');
	check("exact callback total", fake.count, 109);

	/* 50 row writes stay individually ordered at their accepted source sites. */
	for (i = 0; i < 50; i++) {
		const struct fake_event *event = &fake.events[36 + i];

		if (event->logical_event != 26 + i || event->kind != 'W' ||
		    event->domain != OB_PHY_POST_LPF_RADIO) {
			check("row event order and domain", 0, 1);
			break;
		}
	}
	check("first row write source site", fake.events[36].site, 0xa92a7);
	check("last row write source site", fake.events[85].site, 0xa9ee7);

	/* The first mod_radio_reg call has value=0x4000, mask=0: it clears bit 14. */
	check("radio historical RMW source read", fake.events[89].kind, 'R');
	check("radio historical RMW register", fake.events[89].reg, 0x08e5);
	check("radio historical RMW follows row zero", fake.events[89].value, 0x0000);
	check("radio historical RMW write", fake.events[90].value, 0x0000);
}

static void test_entry_guards_do_not_emit_callbacks(void)
{
	struct fake_backend fake;
	struct ob_phy_post_lpf_result result;
	struct ob_phy_post_lpf_result before;
	struct ob_phy_post_lpf_fixture scenario = fixture();
	struct ob_phy_post_lpf_entry_state state = entry_state();

	fake_init(&fake);
	memset(&result, 0x5a, sizeof(result));
	before = result;
	scenario.lookup_matched = false;
	check("unmatched row is not a commit", 
	      ob_phy_acphy_a7089_key1_row63_test_only(&fake_ops, &fake,
						      &scenario, &state, &result), -2);
	check("unmatched row emits no callbacks", fake.count, 0);
	check("unmatched row does not publish result",
	      memcmp(&result, &before, sizeof(result)), 0);

	scenario = fixture();
	scenario.explicitly_synthetic = false;
	check("historical input cannot masquerade as fixture",
	      ob_phy_acphy_a7089_key1_row63_test_only(&fake_ops, &fake,
						      &scenario, &state, &result), -22);
	check("invalid scenario emits no callbacks", fake.count, 0);

	scenario = fixture();
	scenario.requested_chanspec = 0x1000;
	check("different chanspec cannot enter row-63 fixture",
	      ob_phy_acphy_a7089_key1_row63_test_only(&fake_ops, &fake,
						      &scenario, &state, &result), -95);
	check("different chanspec emits no callbacks", fake.count, 0);

	scenario = fixture();
	scenario.nested_912 = 1;
	check("nonzero nested selector cannot enter zero-arm fixture",
	      ob_phy_acphy_a7089_key1_row63_test_only(&fake_ops, &fake,
						      &scenario, &state, &result), -95);
	check("nonzero nested selector emits no callbacks", fake.count, 0);

	scenario = fixture();
	scenario.row_index = 62;
	check("different matched row cannot enter row-63 fixture",
	      ob_phy_acphy_a7089_key1_row63_test_only(&fake_ops, &fake,
						      &scenario, &state, &result), -95);
	check("different row emits no callbacks", fake.count, 0);

	scenario = fixture();
	state.nested_32c = 0;
	check("missing post-LPF caller state rejected",
	      ob_phy_acphy_a7089_key1_row63_test_only(&fake_ops, &fake,
						      &scenario, &state, &result), -95);
	check("invalid caller state emits no callbacks", fake.count, 0);
}

static void test_exhaustive_fail_stop(void)
{
	struct ob_phy_post_lpf_fixture scenario = fixture();
	struct ob_phy_post_lpf_result result;
	unsigned int fail_at;

	for (fail_at = 1; fail_at <= 109; fail_at++) {
		struct fake_backend fake;
		struct ob_phy_post_lpf_result before;
		int ret;

		fake_init(&fake);
		fake.fail_at = fail_at;
		memset(&result, 0xa5, sizeof(result));
		before = result;
		ret = run(&fake, &scenario, &result);
		if (ret != -5 || fake.count != fail_at ||
		    memcmp(&result, &before, sizeof(result)) != 0) {
			char label[80];
			snprintf(label, sizeof(label), "fail-stop at callback %u", fail_at);
			check(label, 0, 1);
			break;
		}
	}
}

static void test_lpf_to_selected_fixture_composition(void)
{
	struct fake_backend fake;
	struct ob_phy_post_lpf_fixture scenario = fixture();
	struct ob_phy_post_lpf_composed_result result;
	struct ob_phy_post_lpf_composed_result before;
	int ret;

	fake_init(&fake);
	memset(&result, 0, sizeof(result));
	ret = ob_phy_acphy_post_lpf_compose_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("composed LPF then selected block succeeds", ret, 0);
	check("composed callback count", fake.count, 3845);
	check("composed LPF saved register", result.lpf_saved_019e, 0xa540);
	check("composed selected event count",
	      result.selected_block.logical_event_count, 88);
	check("LPF tail is final prefix write", fake.events[3735].kind, 'W');
	check("LPF tail register", fake.events[3735].reg, 0x1645);
	check("LPF tail value", fake.events[3735].value, 0x025c);
	check("next event is first selected sub_a7089 read",
	      fake.events[3736].site, 0xa7262);
	check("selected first event register", fake.events[3736].reg, 0x019e);

	fake_init(&fake);
	fake.fail_at = 3737;
	memset(&result, 0x3c, sizeof(result));
	before = result;
	ret = ob_phy_acphy_post_lpf_compose_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("composed first post-LPF failure propagated", ret, -5);
	check("composed execution stops at first post-LPF callback", fake.count,
	      3737);
	check("composed failure does not publish stage result",
	      memcmp(&result, &before, sizeof(result)), 0);
}

static void emit_trace(void)
{
	struct fake_backend fake;
	struct ob_phy_post_lpf_fixture scenario = fixture();
	struct ob_phy_post_lpf_result result = { 0 };
	unsigned int i;
	int ret;

	fake_init(&fake);
	ret = run(&fake, &scenario, &result);
	if (ret) {
		fprintf(stderr, "selected fixture failed: %d\n", ret);
		exit(1);
	}
	printf("{\"scenario\":\"SYNTHETIC_CONTROLLED_CHANSPEC_0x1001_KEY1_ROW63\",\"logical_event_count\":%u,\"callback_count\":%u,\"hardware_ready\":false,\"events\":[",
	       result.logical_event_count, fake.count);
	for (i = 0; i < fake.count; i++) {
		const struct fake_event *event = &fake.events[i];
		if (i)
			putchar(',');
		printf("{\"kind\":\"%c\",\"domain\":\"%s\",\"logical_event\":%u,\"source_site\":\"0x%05x\",\"register\":%u,\"value\":%u,\"width_bits\":%u}",
		       event->kind, domain_name(event->domain),
		       event->logical_event, event->site, event->reg,
		       event->value, event->width_bits);
	}
	printf("]}\n");
}

static void emit_composed_trace(void)
{
	struct fake_backend fake;
	struct ob_phy_post_lpf_fixture scenario = fixture();
	struct ob_phy_post_lpf_composed_result result = { 0 };
	unsigned int i;
	int ret;

	fake_init(&fake);
	ret = ob_phy_acphy_post_lpf_compose_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	if (ret) {
		fprintf(stderr, "composed fixture failed: %d\n", ret);
		exit(1);
	}
	printf("{\"scenario\":\"SYNTHETIC_LPF_CALLER_STORES_A7089_FIXTURE\",\"lpf_callback_count\":3736,\"selected_logical_event_count\":%u,\"selected_callback_count\":%u,\"callback_count\":%u,\"hardware_ready\":false,\"events\":[",
	       result.selected_block.logical_event_count,
	       result.selected_block.callback_count, fake.count);
	for (i = 0; i < fake.count; i++) {
		const struct fake_event *event = &fake.events[i];
		if (i)
			putchar(',');
		printf("{\"kind\":\"%c\",\"domain\":\"%s\",\"logical_event\":%u,\"source_site\":\"0x%05x\",\"register\":%u,\"value\":%u,\"width_bits\":%u}",
		       event->kind, domain_name(event->domain),
		       event->logical_event, event->site, event->reg,
		       event->value, event->width_bits);
	}
	printf("]}\n");
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--emit-trace") == 0) {
		emit_trace();
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--emit-composed-trace") == 0) {
		emit_composed_trace();
		return 0;
	}
	test_selected_vector_counts_and_boundaries();
	test_entry_guards_do_not_emit_callbacks();
	test_exhaustive_fail_stop();
	test_lpf_to_selected_fixture_composition();
	printf("ob_phy_post_lpf_test: %s\n", failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}

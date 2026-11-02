/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <string.h>

#include "../../src/ob_phy_a0be4.h"
#include "../../src/ob_phy_a0be4_vector.h"

#define MAX_TRACE 5000
#define MAX_REGS 256

enum fake_domain {
	FAKE_PHY,
	FAKE_RADIO,
	FAKE_AI_CORE,
	FAKE_CONTROL,
};

struct fake_record {
	char kind;
	enum fake_domain domain;
	u8 event;
	u32 site;
	u16 reg;
	u32 value;
	u8 width;
};

struct fake_reg {
	enum fake_domain domain;
	u16 reg;
	u32 value;
};

struct fake_backend {
	struct fake_record trace[MAX_TRACE];
	struct fake_reg regs[MAX_REGS];
	unsigned int trace_count;
	unsigned int reg_count;
	unsigned int fail_at;
};

static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static struct fake_reg *reg_find(struct fake_backend *fake,
				 enum fake_domain domain, u16 reg)
{
	unsigned int i;

	for (i = 0; i < fake->reg_count; i++)
		if (fake->regs[i].domain == domain && fake->regs[i].reg == reg)
			return &fake->regs[i];
	if (fake->reg_count == MAX_REGS)
		return NULL;
	fake->regs[fake->reg_count].domain = domain;
	fake->regs[fake->reg_count].reg = reg;
	if (domain == FAKE_PHY && reg == 0x019e)
		fake->regs[fake->reg_count].value = 0xa540;
	else if (domain == FAKE_PHY && reg == 0x0140)
		fake->regs[fake->reg_count].value = 0x1237;
	else if (domain == FAKE_PHY && reg == 0x0001)
		fake->regs[fake->reg_count].value = 0x5101;
	else if (domain == FAKE_PHY && reg == 0x173a)
		fake->regs[fake->reg_count].value = 0x0201;
	else if (domain == FAKE_PHY && reg == 0x1725)
		fake->regs[fake->reg_count].value = 0x0005;
	else if (domain == FAKE_AI_CORE && reg == 0x0408)
		fake->regs[fake->reg_count].value = 0xa5a50001;
	else
		fake->regs[fake->reg_count].value =
			((u32)reg * 37u) ^ (domain == FAKE_RADIO ? 0x5a5au : 0x1357u);
	return &fake->regs[fake->reg_count++];
}

static int record(struct fake_backend *fake, char kind,
		  enum fake_domain domain, u8 event, u32 site,
		  u16 reg, u32 value, u8 width)
{
	if (fake->trace_count == MAX_TRACE)
		return -12;
	fake->trace[fake->trace_count++] = (struct fake_record) {
		.kind = kind, .domain = domain, .event = event, .site = site,
		.reg = reg, .value = value, .width = width,
	};
	return fake->fail_at == fake->trace_count ? -5 : 0;
}

static enum fake_domain map_domain(enum ob_phy_post_lpf_domain domain)
{
	switch (domain) {
	case OB_PHY_POST_LPF_PHY: return FAKE_PHY;
	case OB_PHY_POST_LPF_RADIO: return FAKE_RADIO;
	case OB_PHY_POST_LPF_CONTROL: return FAKE_CONTROL;
	default: return FAKE_CONTROL;
	}
}

static int fake_read16(void *ctx, u8 event, u32 site,
		       enum ob_phy_post_lpf_domain domain, u16 reg, u16 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	u16 sampled;
	int ret;

	if (!value || domain == OB_PHY_POST_LPF_CONTROL)
		return -22;
	entry = reg_find(fake, map_domain(domain), reg);
	if (!entry)
		return -12;
	sampled = (u16)entry->value;
	if (event == 0 && domain == OB_PHY_POST_LPF_PHY) {
		if (reg == 0x173a)
			sampled = 0x0201;
		else if (reg == 0x1725)
			sampled = 0x0005;
	}
	ret = record(fake, 'R', map_domain(domain), event, site, reg,
		     sampled, 16);
	if (ret)
		return ret;
	*value = sampled;
	return 0;
}

static int fake_write16(void *ctx, u8 event, u32 site,
			enum ob_phy_post_lpf_domain domain, u16 reg, u16 value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	int ret;

	if (domain == OB_PHY_POST_LPF_CONTROL)
		return -22;
	ret = record(fake, 'W', map_domain(domain), event, site, reg, value, 16);
	if (ret)
		return ret;
	entry = reg_find(fake, map_domain(domain), reg);
	if (!entry)
		return -12;
	entry->value = value;
	return 0;
}

static int fake_read_ai_core32(void *ctx, u8 event, u32 site,
			       u16 reg, u32 *value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry = reg_find(fake, FAKE_AI_CORE, reg);
	int ret;

	if (!value || !entry)
		return -22;
	ret = record(fake, 'R', FAKE_AI_CORE, event, site, reg, entry->value, 32);
	if (ret)
		return ret;
	*value = entry->value;
	return 0;
}

static int fake_write_ai_core32(void *ctx, u8 event, u32 site,
				u16 reg, u32 value)
{
	struct fake_backend *fake = ctx;
	struct fake_reg *entry;
	int ret;

	ret = record(fake, 'W', FAKE_AI_CORE, event, site, reg, value, 32);
	if (ret)
		return ret;
	entry = reg_find(fake, FAKE_AI_CORE, reg);
	if (!entry)
		return -12;
	entry->value = value;
	return 0;
}

static int fake_delay(void *ctx, u8 event, u32 site, u16 usec)
{
	return record(ctx, 'D', FAKE_CONTROL, event, site, 0, usec, 0);
}

static int fake_phyclk(void *ctx, u8 event, u32 site, bool enabled)
{
	return record(ctx, 'F', FAKE_CONTROL, event, site, 0,
		      enabled ? 1u : 0u, 0);
}

struct trace_oracle {
	struct fake_record expected[MAX_TRACE];
	struct fake_reg regs[MAX_REGS];
	unsigned int expected_count;
	unsigned int reg_count;
	u16 resetcca_snapshot;
	bool resetcca_snapshot_valid;
};

static struct fake_reg *oracle_reg(struct trace_oracle *oracle,
				   enum fake_domain domain, u16 reg)
{
	unsigned int i;

	for (i = 0; i < oracle->reg_count; i++)
		if (oracle->regs[i].domain == domain && oracle->regs[i].reg == reg)
			return &oracle->regs[i];
	if (oracle->reg_count == MAX_REGS)
		return NULL;
	oracle->regs[oracle->reg_count].domain = domain;
	oracle->regs[oracle->reg_count].reg = reg;
	if (domain == FAKE_PHY && reg == 0x019e)
		oracle->regs[oracle->reg_count].value = 0xa540;
	else if (domain == FAKE_PHY && reg == 0x0140)
		oracle->regs[oracle->reg_count].value = 0x1237;
	else if (domain == FAKE_PHY && reg == 0x0001)
		oracle->regs[oracle->reg_count].value = 0x5101;
	else if (domain == FAKE_PHY && reg == 0x173a)
		oracle->regs[oracle->reg_count].value = 0x0201;
	else if (domain == FAKE_PHY && reg == 0x1725)
		oracle->regs[oracle->reg_count].value = 0x0005;
	else if (domain == FAKE_AI_CORE && reg == 0x0408)
		oracle->regs[oracle->reg_count].value = 0xa5a50001;
	else
		oracle->regs[oracle->reg_count].value =
			((u32)reg * 37u) ^ (domain == FAKE_RADIO ? 0x5a5au : 0x1357u);
	return &oracle->regs[oracle->reg_count++];
}

static void oracle_emit(struct trace_oracle *oracle, char kind,
			enum fake_domain domain, const struct ob_phy_a0be4_event *event,
			u16 reg, u32 value, u8 width)
{
	oracle->expected[oracle->expected_count++] = (struct fake_record) {
		.kind = kind, .domain = domain, .event = event->logical_event,
		.site = event->source_site, .reg = reg, .value = value,
		.width = width,
	};
}

/* Independent callback expansion from accepted logical event semantics. */
static void build_independent_oracle(struct trace_oracle *oracle)
{
	unsigned int i;

	memset(oracle, 0, sizeof(*oracle));
	for (i = 0; i < OB_PHY_A0BE4_VECTOR_COUNT; i++) {
		const struct ob_phy_a0be4_event *event = &ob_phy_a0be4_events[i];
		struct fake_reg *reg;
		u16 old16, new16;
		u32 old32, new32;

		switch (event->operation) {
		case OB_PHY_A0BE4_PHY_OR:
			reg = oracle_reg(oracle, FAKE_PHY, event->register_offset);
			old16 = (u16)reg->value;
			oracle_emit(oracle, 'R', FAKE_PHY, event,
				    event->register_offset, old16, 16);
			new16 = (u16)(old16 | event->value);
			oracle_emit(oracle, 'W', FAKE_PHY, event,
				    event->register_offset, new16, 16);
			reg->value = new16;
			break;
		case OB_PHY_A0BE4_PHY_WRITE:
			oracle_emit(oracle, 'W', FAKE_PHY, event,
				    event->register_offset, event->value, 16);
			reg = oracle_reg(oracle, FAKE_PHY, event->register_offset);
			reg->value = event->value;
			break;
		case OB_PHY_A0BE4_PHY_RMW_MASKED_MERGE:
			reg = oracle_reg(oracle, FAKE_PHY, event->register_offset);
			old16 = (u16)reg->value;
			oracle_emit(oracle, 'R', FAKE_PHY, event,
				    event->register_offset, old16, 16);
			new16 = (u16)((old16 & (u16)~event->mask) |
				       ((u16)event->value & (u16)event->mask));
			oracle_emit(oracle, 'W', FAKE_PHY, event,
				    event->register_offset, new16, 16);
			reg->value = new16;
			break;
		case OB_PHY_A0BE4_AI_CORE_CFLAGS:
			reg = oracle_reg(oracle, FAKE_AI_CORE, event->register_offset);
			old32 = reg->value;
			oracle_emit(oracle, 'R', FAKE_AI_CORE, event,
				    event->register_offset, old32, 32);
			new32 = (old32 & ~event->mask) | event->value;
			oracle_emit(oracle, 'W', FAKE_AI_CORE, event,
				    event->register_offset, new32, 32);
			reg->value = new32;
			oracle_emit(oracle, 'R', FAKE_AI_CORE, event,
				    event->register_offset, new32, 32);
			break;
		case OB_PHY_A0BE4_PHY_READ:
			reg = oracle_reg(oracle, FAKE_PHY, event->register_offset);
			oracle->resetcca_snapshot = (u16)reg->value;
			oracle->resetcca_snapshot_valid = true;
			oracle_emit(oracle, 'R', FAKE_PHY, event,
				    event->register_offset, reg->value, 16);
			break;
		case OB_PHY_A0BE4_PHY_WRITE_DERIVED:
			if (event->expression == OB_PHY_A0BE4_EXPR_SNAPSHOT_OR)
				new16 = (u16)(oracle->resetcca_snapshot | event->value);
			else
				new16 = (u16)(oracle->resetcca_snapshot & event->value);
			oracle_emit(oracle, 'W', FAKE_PHY, event,
				    event->register_offset, new16, 16);
			reg = oracle_reg(oracle, FAKE_PHY, event->register_offset);
			reg->value = new16;
			break;
		case OB_PHY_A0BE4_DELAY_US:
			oracle_emit(oracle, 'D', FAKE_CONTROL, event, 0,
				    event->duration_us, 0);
			break;
		case OB_PHY_A0BE4_PHYCLK_FGC:
			oracle_emit(oracle, 'F', FAKE_CONTROL, event, 0,
				    event->value != 0, 0);
			break;
		}
	}
}

static void compare_trace_to_oracle(const struct fake_backend *fake)
{
	struct trace_oracle oracle;
	unsigned int i;

	build_independent_oracle(&oracle);
	check("oracle callback count", oracle.expected_count, 67);
	check("implementation callback count", fake->trace_count,
	      oracle.expected_count);
	for (i = 0; i < oracle.expected_count && i < fake->trace_count; i++) {
		const struct fake_record *got = &fake->trace[i];
		const struct fake_record *expected = &oracle.expected[i];
		if (got->kind != expected->kind || got->domain != expected->domain ||
		    got->event != expected->event || got->site != expected->site ||
		    got->reg != expected->reg || got->value != expected->value ||
		    got->width != expected->width) {
			printf("FAIL: trace mismatch at callback %u; site=%#x/%#x reg=%#x/%#x value=%#x/%#x\n",
			       i + 1, got->site, expected->site, got->reg,
			       expected->reg, got->value, expected->value);
			failures++;
			return;
		}
	}
}

static const struct ob_phy_a0be4_ops fake_ops = {
	.phy = {
		.read16 = fake_read16,
		.write16 = fake_write16,
		.delay_us = fake_delay,
		.phyclk_fgc = fake_phyclk,
	},
	.read_ai_core32 = fake_read_ai_core32,
	.write_ai_core32 = fake_write_ai_core32,
};

static struct ob_phy_a0be4_fixture fixture(void)
{
	return (struct ob_phy_a0be4_fixture) {
		.selected_a7089_fixture_completed = true,
		.si_core_type = 1,
		.phy = {
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
		},
		.entry = {
			.nested_32c = 1,
			.nested_32e = 0xff,
			.nested_912 = 0,
		},
	};
}

static void test_selected_vector_boundaries_and_cflags_expansion(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_result result = { 0 };
	int ret;

	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("selected a0be4 fixture succeeds", ret, 0);
	check("selected a0be4 logical count", result.logical_event_count, 40);
	check("selected a0be4 callback count includes cflags readback",
	      result.callback_count, 67);
	check("a0be4 completion kind", result.kind,
	      OB_PHY_A0BE4_SELECTED_FIXTURE_COMPLETE);
	check("a0be4 does not establish readiness", result.hardware_ready, false);
	check("first callback count", fake.trace_count, 67);
	check("first callback kind", fake.trace[0].kind, 'R');
	check("first callback site", fake.trace[0].site, 0xa0c00);
	check("first callback register", fake.trace[0].reg, 0x019e);
	check("first callback width", fake.trace[0].width, 16);
	check("first callback initial sample", fake.trace[0].value, 0xa540);
	check("first OR write kind", fake.trace[1].kind, 'W');
	check("first OR write value", fake.trace[1].value, 0xa5c0);
	check("AI cflags read site", fake.trace[10].site, 0xa0d20);
	check("AI cflags first access width", fake.trace[10].width, 32);
	check("AI cflags register", fake.trace[10].reg, 0x0408);
	check("AI cflags old value", fake.trace[10].value, 0xa5a50001);
	check("AI cflags write value", fake.trace[11].value, 0xa5a50011);
	check("AI cflags readback kind", fake.trace[12].kind, 'R');
	check("AI cflags readback value", fake.trace[12].value, 0xa5a50011);
	check("last callback site", fake.trace[66].site, 0xa13da);
	check("last callback register", fake.trace[66].reg, 0x0358);
	check("last callback value", fake.trace[66].value, 0xc07f);
	check("last callback width", fake.trace[66].width, 16);
	compare_trace_to_oracle(&fake);
}

static void test_si_dispatch_equivalent_arm(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_result result = { 0 };
	int ret;

	scenario.si_core_type = 3;
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("AI-compatible SI type 3 succeeds", ret, 0);
	check("AI-compatible SI type 3 callback count", fake.trace_count, 67);
}

static void test_invalid_fixture_fails_before_callback(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_result result;
	int ret;

	scenario.phy.requested_chanspec = 0x1002;
	memset(&result, 0xa5, sizeof(result));
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("wrong chanspec rejected", ret, -95);
	check("wrong chanspec has no callback", fake.trace_count, 0);
	check("wrong chanspec result invalid", result.kind,
	      OB_PHY_A0BE4_RESULT_INVALID);
	check("wrong chanspec no ready status", result.hardware_ready, false);

	scenario = fixture();
	scenario.phy.nested_912 = 1;
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("nonzero nested 912 rejected", ret, -95);
	check("nonzero nested 912 has no callback", fake.trace_count, 0);

	scenario = fixture();
	scenario.phy.chip_id = 0x4353;
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("wrong chip identity rejected", ret, -95);
	check("wrong chip identity has no callback", fake.trace_count, 0);
}

static void test_missing_callback_fails_before_first_access(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_result result;
	struct ob_phy_a0be4_ops incomplete = fake_ops;
	int ret;

	incomplete.phy.delay_us = NULL;
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		&incomplete, &fake, &scenario, &result);
	check("missing delay callback rejected", ret, -22);
	check("missing callback issues no access", fake.trace_count, 0);
	check("missing callback leaves result invalid", result.kind,
	      OB_PHY_A0BE4_RESULT_INVALID);
}

static void test_exhaustive_fail_stop(void)
{
	unsigned int fail_at;

	for (fail_at = 1; fail_at <= 67; fail_at++) {
		struct fake_backend fake = { .fail_at = fail_at };
		struct ob_phy_a0be4_fixture scenario = fixture();
		struct ob_phy_a0be4_result result;
		int ret;

		memset(&result, 0xa5, sizeof(result));
		ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
			&fake_ops, &fake, &scenario, &result);
		check("injected error returned unchanged", ret, -5);
		check("no callback after first error", fake.trace_count, fail_at);
		check("failure does not publish complete state", result.kind,
		      OB_PHY_A0BE4_RESULT_INVALID);
		check("failure does not publish readiness", result.hardware_ready,
		      false);
	}
}

static void test_composed_prefix_through_a0be4(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_composed_result result = { 0 };
	int ret;

	ret = ob_phy_acphy_initial_fixture_through_a0be4_test_only(
		&fake_ops, &fake, &scenario, &result);
	check("composed prefix succeeds", ret, 0);
	check("prior selected sub_a7089 callbacks", 
	      result.through_a7089.selected_block.callback_count, 109);
	check("new a0be4 callbacks", result.a0be4.callback_count, 67);
	check("composed runner callback count", result.callback_count, 3912);
	check("actual fake trace callback count", fake.trace_count, 3912);
	check("join predecessor is a7089 final restore site",
	      fake.trace[3844].site, 0xaa404);
	check("join first a0be4 callback site", fake.trace[3845].site, 0xa0c00);
	check("last callback is a0be4 event 40", fake.trace[3911].site, 0xa13da);
	check("terminal return boundary", result.terminal_return_site, 0xa13eb);
	check("composed result does not establish readiness",
	      result.hardware_ready, false);
}

static int dump_selected_trace(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_result result = { 0 };
	unsigned int i;
	int ret;

	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(
		&fake_ops, &fake, &scenario, &result);
	if (ret || result.logical_event_count != 40 || fake.trace_count != 67)
		return 1;
	for (i = 0; i < fake.trace_count; i++) {
		const struct fake_record *record = &fake.trace[i];
		const char *domain;

		switch (record->domain) {
		case FAKE_PHY: domain = "PHY"; break;
		case FAKE_RADIO: domain = "RADIO"; break;
		case FAKE_AI_CORE: domain = "AI_CORE"; break;
		default: domain = "CONTROL"; break;
		}
		printf("%c,%s,%u,0x%05x,0x%04x,0x%08x,%u\n",
		       record->kind, domain, record->event, record->site,
		       record->reg, record->value, record->width);
	}
	return 0;
}

static int dump_composed_trace(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_composed_result result = { 0 };
	unsigned int i;
	int ret;

	ret = ob_phy_acphy_initial_fixture_through_a0be4_test_only(
		&fake_ops, &fake, &scenario, &result);
	if (ret || result.callback_count != 3912 || fake.trace_count != 3912 ||
	    result.terminal_return_site != 0xa13eb)
		return 1;
	for (i = 0; i < fake.trace_count; i++) {
		const struct fake_record *record = &fake.trace[i];
		const char *domain;

		switch (record->domain) {
		case FAKE_PHY: domain = "PHY"; break;
		case FAKE_RADIO: domain = "RADIO"; break;
		case FAKE_AI_CORE: domain = "AI_CORE"; break;
		default: domain = "CONTROL"; break;
		}
		printf("%c,%s,%u,0x%05x,0x%04x,0x%08x,%u\n",
		       record->kind, domain, record->event, record->site,
		       record->reg, record->value, record->width);
	}
	return 0;
}

/* PI+0x164 == 1 (BCM4352 target) takes the extra phy_reg_write(0x26,0x92). */
static void test_rev1_extra_write(void)
{
	struct fake_backend fake = { 0 };
	struct ob_phy_a0be4_fixture scenario = fixture();
	struct ob_phy_a0be4_result result = { 0 };
	bool found_rev1 = false, found_rev0a = false;
	unsigned int i;
	int ret;

	scenario.phy.phy_revision = 1;
	scenario.phy.pi_164_phy_revision = 1;
	scenario.phy.nested_912 = 1; /* = ([PI+0x164] <= 1) */
	scenario.entry.nested_912 = 1;
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(&fake_ops, &fake,
							    &scenario, &result);
	check("rev1 a0be4 succeeds", ret, 0);
	for (i = 0; i < fake.trace_count; i++)
		if (fake.trace[i].domain == FAKE_PHY &&
		    fake.trace[i].kind == 'W' && fake.trace[i].reg == 0x0026 &&
		    fake.trace[i].value == 0x0092)
			found_rev1 = true;
	check("rev1 takes extra phy_reg_write(0x26,0x92)", found_rev1 ? 1 : 0, 1);

	memset(&fake, 0, sizeof(fake));
	scenario = fixture(); /* phy_revision = 0x0a (rev > 1 arm) */
	ret = ob_phy_acphy_a0be4_selected_fixture_test_only(&fake_ops, &fake,
							    &scenario, &result);
	check("rev0x0a a0be4 succeeds", ret, 0);
	for (i = 0; i < fake.trace_count; i++)
		if (fake.trace[i].domain == FAKE_PHY &&
		    fake.trace[i].kind == 'W' && fake.trace[i].reg == 0x0026 &&
		    fake.trace[i].value == 0x0092)
			found_rev0a = true;
	check("rev0x0a omits the extra write", found_rev0a ? 1 : 0, 0);
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--dump-a0be4") == 0)
		return dump_selected_trace();
	if (argc == 2 && strcmp(argv[1], "--dump-composed-a0be4") == 0)
		return dump_composed_trace();
	if (argc != 1)
		return 2;
	test_selected_vector_boundaries_and_cflags_expansion();
	test_si_dispatch_equivalent_arm();
	test_invalid_fixture_fails_before_callback();
	test_missing_callback_fails_before_first_access();
	test_exhaustive_fail_stop();
	test_composed_prefix_through_a0be4();
	test_rev1_extra_write();
	if (failures) {
		printf("ob_phy_a0be4_test: %u failures\n", failures);
		return 1;
	}
	puts("ob_phy_a0be4_test: PASS");
	return 0;
}

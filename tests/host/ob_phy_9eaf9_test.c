/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "../../src/ob_phy_9eaf9.h"

#define MAX_TRACE 128u
#define MAX_REGS 128u
#define MAX_WORDS 48u

struct record {
	char kind;
	u16 logical_event;
	u32 source_site;
	u16 reg;
	u16 value;
	u8 width;
	u8 table_id;
	u16 offset;
	u8 count;
	u8 chain;
	u8 invocation;
	u8 ordinal;
	u8 word_order_count;
	u8 word_order[3];
	u16 word_count;
	u16 words[MAX_WORDS];
};

struct reg_cell {
	u16 reg;
	u16 value;
};

struct fake {
	struct record trace[MAX_TRACE];
	struct reg_cell regs[MAX_REGS];
	u16 trace_count;
	u16 reg_count;
	u16 fail_at;
};

static unsigned int failures;

static void check(const char *name, long got, long expected)
{
	if (got != expected) {
		printf("FAIL: %s got %ld expected %ld\n", name, got, expected);
		failures++;
	}
}

static struct reg_cell *find_reg(struct fake *f, u16 reg)
{
	u16 i;

	for (i = 0; i < f->reg_count; i++)
		if (f->regs[i].reg == reg)
			return &f->regs[i];
	if (f->reg_count == MAX_REGS)
		return NULL;
	f->regs[i].reg = reg;
	f->regs[i].value = reg == 0x019e ? 0xa540 : (u16)((reg * 37u) ^ 0x1357u);
	f->reg_count++;
	return &f->regs[i];
}

static int append(struct fake *f, const struct record *record)
{
	if (f->trace_count == MAX_TRACE)
		return -12;
	f->trace[f->trace_count++] = *record;
	return f->fail_at == f->trace_count ? -5 : 0;
}

static int fake_read(void *ctx, u16 event, u32 site, u16 reg, u16 *value)
{
	struct fake *f = ctx;
	struct reg_cell *cell = find_reg(f, reg);
	struct record r = { .kind = 'R', .logical_event = event,
		.source_site = site, .reg = reg, .width = 16 };
	int ret;

	if (!cell || !value)
		return -22;
	r.value = cell->value;
	ret = append(f, &r);
	if (!ret)
		*value = cell->value;
	return ret;
}

static int fake_write(void *ctx, u16 event, u32 site, u16 reg, u16 value)
{
	struct fake *f = ctx;
	struct reg_cell *cell;
	struct record r = { .kind = 'W', .logical_event = event,
		.source_site = site, .reg = reg, .value = value, .width = 16 };
	int ret = append(f, &r);

	if (ret)
		return ret;
	cell = find_reg(f, reg);
	if (!cell)
		return -12;
	cell->value = value;
	return 0;
}

static int fake_table_read(void *ctx, u16 event, u32 site, u8 table,
			   u16 offset, u8 count, u8 width, u8 chain,
			   u8 invocation, u8 ordinal, u16 supplied, u16 *value)
{
	struct fake *f = ctx;
	struct record r = { .kind = 'T', .logical_event = event,
		.source_site = site, .value = supplied, .width = width,
		.table_id = table, .offset = offset, .count = count,
		.chain = chain, .invocation = invocation, .ordinal = ordinal };
	int ret = append(f, &r);

	if (!ret)
		*value = supplied;
	return ret;
}

static int fake_table_write(void *ctx, u16 event, u32 site, u8 table,
			    u16 offset, u8 count, u8 width, u8 word_width,
			    const u8 *order, u8 order_count,
			    const u16 *words, u16 word_count)
{
	struct fake *f = ctx;
	struct record r = { .kind = 't', .logical_event = event,
		.source_site = site, .width = width, .table_id = table,
		.offset = offset, .count = count, .word_count = word_count };
	int ret;

	(void)word_width;
	if (word_count > MAX_WORDS)
		return -22;
	memcpy(r.words, words, word_count * sizeof(words[0]));
	if (order_count > 3)
		return -22;
	r.word_order_count = order_count;
	if (order_count)
		memcpy(r.word_order, order, order_count);
	ret = append(f, &r);
	return ret;
}

static const struct ob_phy_9eaf9_ops fake_ops = {
	.read16 = fake_read,
	.write16 = fake_write,
	.table_read16 = fake_table_read,
	.table_write = fake_table_write,
};

static struct ob_phy_9eaf9_fixture fixture(enum ob_phy_9eaf9_policy policy,
						  bool word2, u16 word)
{
	struct ob_phy_9eaf9_fixture f = {
		.explicitly_synthetic = true,
		.policy = policy,
		.chip_id = 0x4352, .chip_revision = 3,
		.phy_revision = 0x0a, .radio_id = 0x2069,
		.radio_revision = 4, .radio_class = 0, .chain_count = 2,
		.chanspec_supplied = true, .controlled_chanspec = 0x1001,
		.caller_gate_inputs_supplied = true,
		.caller_local_7a = 0, .nested_32c = 1,
		.word2_present = word2, .synthetic_word2 = word,
	};
	static const u16 events[4] = { 40, 41, 44, 45 };
	static const u32 sites[4] = { 0x9da4b, 0x9da73, 0x9da4b, 0x9da73 };
	static const u8 tables[4] = { 7, 7, 7, 7 };
	static const u16 offsets[4] = { 0x14a, 0x36a, 0x15a, 0x37a };
	static const u8 chains[4] = { 0, 0, 1, 1 };
	static const u8 ordinals[4] = { 1, 2, 3, 4 };
	u8 i;

	for (i = 0; i < 4; i++) {
		f.runtime_samples[i].valid = true;
		f.runtime_samples[i].logical_event = events[i];
		f.runtime_samples[i].source_site = sites[i];
		f.runtime_samples[i].table_id = tables[i];
		f.runtime_samples[i].offset = offsets[i];
		f.runtime_samples[i].element_count = 1;
		f.runtime_samples[i].element_width_bits = 16;
		f.runtime_samples[i].chain_index = chains[i];
		f.runtime_samples[i].invocation_id = OB_PHY_9EAF9_INVOCATION_CONTROLLED_1001;
		f.runtime_samples[i].sample_ordinal = ordinals[i];
		f.runtime_samples[i].value = (u16)(0x1234u + (u16)i * 0x2222u);
	}
	return f;
}

static const struct record *find_table14(const struct fake *f)
{
	u16 i;
	for (i = 0; i < f->trace_count; i++)
		if (f->trace[i].kind == 't' && f->trace[i].table_id == 0x14)
			return &f->trace[i];
	return NULL;
}

static void test_selected_and_word2_differential(void)
{
	struct ob_phy_9eaf9_fixture f0 = fixture(OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED, true, 0);
	struct ob_phy_9eaf9_fixture f1 = fixture(OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED, true, 0x7b3c);
	struct ob_phy_9eaf9_result result;
	struct fake a = { 0 }, b = { 0 };
	const struct record *ra, *rb;
	int ret;

	ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &a, &f0, &result);
	check("selected word2=0 result", ret, 0);
	check("selected logical count", result.logical_event_count, 136);
	check("selected callback count", result.callback_count, 107);
	check("simulation result kind", result.kind, OB_PHY_9EAF9_HOST_SIMULATION_COMPLETE);
	check("historical word2 remains unknown", result.historical_word2_resolved, 0);
	check("no ready flag", result.hardware_ready, 0);
	check("first selected callback", a.trace[0].kind, 'R');
	check("first selected event", a.trace[0].logical_event, 1);
	check("last selected callback", a.trace[a.trace_count - 1].kind, 'W');
	check("last selected event", a.trace[a.trace_count - 1].logical_event, 136);
	ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &b, &f1, &result);
	check("selected word2=7b3c result", ret, 0);
	ra = find_table14(&a);
	rb = find_table14(&b);
	check("TABLE14 record exists", ra != NULL && rb != NULL, 1);
	if (ra && rb) {
		check("TABLE14 one element", ra->count, 1);
		check("TABLE14 48-bit width", ra->width, 48);
		check("TABLE14 payload word count", ra->word_count, 3);
		check("TABLE14 word0", ra->words[0], 0xe800);
		check("TABLE14 word1", ra->words[1], 0x0084);
		check("TABLE14 word2 zero fixture", ra->words[2], 0);
		check("TABLE14 word2 alternate fixture", rb->words[2], 0x7b3c);
	}
}

static void test_gate_strict_and_prevalidation(void)
{
	struct ob_phy_9eaf9_fixture f = fixture(OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED, false, 0);
	struct ob_phy_9eaf9_result result;
	struct fake fake = { 0 };
	int ret;

	f.caller_local_7a = 0;
	f.nested_32c = 0;
	ret = ob_phy_acphy_9eaf9_fixture_test_only(NULL, NULL, &f, &result);
	check("caller skip succeeds without backend or unknown", ret, 0);
	check("caller skip result", result.kind, OB_PHY_9EAF9_SKIPPED_BY_CALLER);
	check("caller skip callbacks", result.callback_count, 0);

	f.nested_32c = 1;
	f.policy = OB_PHY_9EAF9_HISTORICAL_STRICT;
	ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &fake, &f, &result);
	check("strict historical mode blocks", ret, -ENODATA);
	check("strict result status", result.kind, OB_PHY_9EAF9_BLOCKED_HISTORICAL_UNKNOWN);
	check("strict no callbacks", fake.trace_count, 0);

	f.policy = OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED;
	ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &fake, &f, &result);
	check("missing word2 rejected", ret, -EINVAL);
	check("missing word2 before access", fake.trace_count, 0);

	f.word2_present = true;
	{
		unsigned int field;
		for (field = 0; field < 9; field++) {
			struct ob_phy_9eaf9_fixture bad = f;
			struct fake empty = { 0 };
			switch (field) {
			case 0: bad.runtime_samples[2].table_id++; break;
			case 1: bad.runtime_samples[2].offset++; break;
			case 2: bad.runtime_samples[2].element_width_bits++; break;
			case 3: bad.runtime_samples[2].element_count++; break;
			case 4: bad.runtime_samples[2].chain_index = 0; break;
			case 5: bad.runtime_samples[2].invocation_id++; break;
			case 6: bad.runtime_samples[2].sample_ordinal++; break;
			case 7: bad.runtime_samples[2].logical_event++; break;
			case 8: bad.runtime_samples[2].source_site++; break;
			}
			ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &empty,
								   &bad, &result);
			check("malformed TABLE_READ key rejected", ret, -EINVAL);
			check("malformed TABLE_READ key before access", empty.trace_count, 0);
		}
	}
	f.chip_revision = 2;
	ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &fake, &f, &result);
	check("invalid target rejected", ret, -EINVAL);
	check("invalid target before access", fake.trace_count, 0);

	f.chip_revision = 3;
	ret = ob_phy_acphy_9eaf9_fixture_test_only(NULL, NULL, &f, &result);
	check("missing callbacks rejected", ret, -EINVAL);
	check("missing callbacks before access", fake.trace_count, 0);
}

static void test_exhaustive_fail_stop(void)
{
	unsigned int failure_index;
	unsigned int tested = 0;

	for (failure_index = 1; failure_index <= 107; failure_index++) {
		struct ob_phy_9eaf9_fixture f = fixture(OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED,
							 true, 0x7b3c);
		struct ob_phy_9eaf9_result result;
		struct fake fake = { .fail_at = (u16)failure_index };
		int ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &fake, &f, &result);

		if (ret != -5 || fake.trace_count != failure_index ||
		    result.kind != OB_PHY_9EAF9_FAILED_BACKEND_OPERATION ||
		    result.callback_count != failure_index || result.hardware_ready ||
		    result.historical_word2_resolved) {
			printf("FAIL: fail-stop index %u ret=%d callbacks=%u kind=%d\n",
			       failure_index, ret, fake.trace_count, result.kind);
			failures++;
		}
		tested++;
	}
	check("exhaustive callback fail-stop positions", tested, 107);
}

static struct ob_phy_9eaf9_fixture dump_fixture(int argc, char **argv)
{
	struct ob_phy_9eaf9_fixture f = fixture(OB_PHY_9EAF9_SYNTHETIC_PARAMETERIZED,
						       true,
						       (u16)strtoul(argv[2], NULL, 0));
	u8 i;
	for (i = 0; i < 4; i++)
		f.runtime_samples[i].value = (u16)strtoul(argv[3 + i], NULL, 0);
	(void)argc;
	return f;
}

static void dump_json(const struct fake *f, const struct ob_phy_9eaf9_result *result)
{
	u16 i;
	printf("{\"result\":%d,\"logical_events\":%u,\"callbacks\":%u,\"records\":[",
	       result->kind, result->logical_event_count, result->callback_count);
	for (i = 0; i < f->trace_count; i++) {
		const struct record *r = &f->trace[i];
		u16 j;
		if (i)
			putchar(',');
		if (r->kind == 'R' || r->kind == 'W') {
			printf("{\"kind\":\"%c\",\"logical_event\":%u,\"source_site\":\"0x%x\",\"domain\":\"PHY\",\"register\":\"0x%04x\",\"width_bits\":16,\"value\":\"0x%04x\"}",
			       r->kind, r->logical_event, r->source_site, r->reg, r->value);
		} else if (r->kind == 'T') {
			printf("{\"kind\":\"TABLE_READ\",\"logical_event\":%u,\"source_site\":\"0x%x\",\"domain\":\"TABLE\",\"table_id\":%u,\"offset\":\"0x%03x\",\"element_count\":%u,\"element_width_bits\":%u,\"chain_index\":%u,\"invocation_id\":%u,\"sample_ordinal\":%u,\"value\":\"0x%04x\"}",
			       r->logical_event, r->source_site, r->table_id, r->offset,
			       r->count, r->width, r->chain, r->invocation, r->ordinal, r->value);
		} else {
			printf("{\"kind\":\"TABLE_WRITE\",\"logical_event\":%u,\"source_site\":\"0x%x\",\"domain\":\"TABLE\",\"table_id\":%u,\"offset\":\"0x%03x\",\"element_count\":%u,\"element_width_bits\":%u,\"payload_word_width_bits\":16,\"payload_word_order\":[",
			       r->logical_event, r->source_site, r->table_id, r->offset,
			       r->count, r->width);
			for (j = 0; j < r->word_order_count; j++) {
				if (j)
					putchar(',');
				printf("%u", r->word_order[j]);
			}
			printf("],\"payload_words\":[");
			for (j = 0; j < r->word_count; j++) {
				if (j)
					putchar(',');
				printf("\"0x%04x\"", r->words[j]);
			}
			printf("]}");
		}
	}
	puts("]}");
}

int main(int argc, char **argv)
{
	if (argc == 7 && !strcmp(argv[1], "--dump")) {
		struct ob_phy_9eaf9_fixture f = dump_fixture(argc, argv);
		struct ob_phy_9eaf9_result result;
		struct fake fake = { 0 };
		int ret;
		f.word2_present = true;
		ret = ob_phy_acphy_9eaf9_fixture_test_only(&fake_ops, &fake, &f, &result);
		if (ret)
			return 2;
		dump_json(&fake, &result);
		return 0;
	}
	test_selected_and_word2_differential();
	test_gate_strict_and_prevalidation();
	test_exhaustive_fail_stop();
	if (failures) {
		printf("%u sub_9eaf9 C checks failed\n", failures);
		return 1;
	}
	puts("sub_9eaf9 C fake-backend checks passed; failure positions=107");
	return 0;
}
